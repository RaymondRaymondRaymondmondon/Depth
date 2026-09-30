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

// a quad that is never culled
void Q4(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) { DrawTri(a, b, c, col); DrawTri(a, c, d, col); }

// the drowned sailor on the squeezebox: kelp grown through his beard (chains), a brass earring, a peacoat crusted
// with barnacles; the bellows open and close on each two-bar phrase and his head sways with the tune
void Sailor(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, 1, [&](Vector2 ft) {
        Instance& I = Get(90001); Tick(I, in.t);
        float s = 1.1f, f = 1, t = in.t;
        Build b; b.thigh = 38; b.shin = 36; b.upper = 25; b.fore = 24; b.spine = 25; b.chest = 24; b.head = 12; b.shoulderW = 17; b.stanceF = 13; b.stanceB = -11;
        RPose P;
        P[C_HIPY] = 5; P[C_LEAN] = 0.14f; P[C_HEAD] = 0.12f;
        float open = in.bandPlaying ? 0.5f + 0.5f * sinf(bt.bar * PI) : 0.05f;   // a phrase is two bars: out, then in
        P[C_HFX] = 20 + 12 * open; P[C_HFY] = 30; P[C_HBX] = 6; P[C_HBY] = 33;
        P[C_HEAD] += in.bandPlaying ? 0.12f * sinf(bt.bar * PI * 2) : 0.05f * sinf(t * 0.4f);
        P[C_HIPY] += 1.5f * sinf(t * 1.4f);
        if (in.bandPlaying) P[C_FFY] = -3 * bt.pulse;   // a foot keeping time
        Solved S = SolveHumanoid(b, P, ft, s, f);
        Color coat{70, 86, 112, 255}, coatDk = Tone(coat, -0.3f), skin{150, 176, 160, 255}, cap{140, 44, 38, 255}, kelp{64, 112, 56, 255}, knit{220, 210, 186, 255};
        Hang(I, 0, S.Head(3, 13), 7, 5 * s, {0.05f, 1}, kelp, 3.2f, 1.2f);
        Hang(I, 1, S.Head(8, 12), 6, 5 * s, {0.25f, 1}, Tone(kelp, 0.25f), 2.6f, 1);
        Hang(I, 2, S.Head(-1, 11), 5, 5 * s, {-0.2f, 1}, Tone(kelp, -0.15f), 2.4f, 0.9f);
        Parts parts;
        auto leg = [&](int hip, int kn, int an, Color col) {
            MLimb(S.p[hip], S.p[kn], 9.5f * s, 8 * s, col, CLOTH); MLimb(S.p[kn], S.p[an], 8 * s, 6.5f * s, col, CLOTH);
            MLimb(Off2(S.p[an], -3 * s, 1), Off2(S.p[an], 11 * s, 3), 6 * s, 5.5f * s, Color{40, 30, 24, 255}, WET);
        };
        parts.Add(-1, [&] { leg(HIP_B, KN_B, AN_B, Color{40, 40, 48, 255}); });
        parts.Add(-0.9f, [&] { leg(HIP_F, KN_F, AN_F, Color{50, 50, 60, 255}); });
        parts.Add(-0.5f, [&] { MLimb(S.p[SH_B], S.p[EL_B], 7.5f * s, 6.5f * s, coatDk, CLOTH); MLimb(S.p[EL_B], S.p[WR_B], 6.5f * s, 5.5f * s, coatDk, CLOTH); });
        parts.Add(0, [&] {
            MQuad(S.Chest(-18, -22), S.Chest(19, -22), S.Hips(16, 6), S.Hips(-16, 6), coat, CLOTH);         // the peacoat, down past the hips
            MQuad(S.Chest(4, -23), S.Chest(14, -23), S.Chest(9, -4), S.Chest(3, -6), Tone(coat, 0.2f), CLOTH);   // a broad lapel
            MLimb(S.Chest(0, -26), S.Chest(10, -26), 6 * s, 6 * s, knit, CLOTH);                              // the roll-neck jumper
            for (int k = 0; k < 3; k++) { MBall(S.Chest(12, -10 + k * 9.0f), 1.8f * s, Pal::Brass, METAL); MBall(S.Chest(18, -10 + k * 9.0f), 1.8f * s, Pal::Brass, METAL); }
            for (int k = 0; k < 6; k++) { Vector2 bp = S.Chest(-13 + (k % 3) * 7.0f, -2 + (k / 3) * 9.0f); DrawCircleV(bp, 2.2f * s, Color{150, 170, 140, 255}); DrawCircleV(bp, 0.9f * s, Color{60, 70, 60, 255}); }   // barnacles
            DrawLineEx(S.Chest(-10, -16), S.Hips(-10, 2), 1.2f * s, Fade(Color{10, 10, 20, 255}, 0.55f));
        });
        parts.Add(0.4f, [&] {
            MLimb(S.Chest(3, -26), S.Head(1, 8), 7 * s, 6.5f * s, Tone(skin, -0.1f), SKIN);
            MBall(S.Head(-9, 1), 3.2f * s, Tone(skin, -0.1f), SKIN);                        // the ear
            DrawRing(S.Head(-9, 6), 1.6f * s, 2.6f * s, 0, 360, 12, Pal::Brass);            // a brass earring
            MBall(S.p[HEAD], 12.5f * s, skin, SKIN);
            MLimb(S.Head(9, -2), S.Head(13.5f, 5), 2.2f * s, 3.2f * s, Tone(skin, -0.05f), SKIN);   // a big nose
            DrawCircleV(S.Head(5, -3), 3.4f * s, Tone(skin, -0.45f));                       // sunken eyes
            Eyes(I, S.Head(4, -3), 0, 1.7f, s, Color{210, 225, 200, 255}, Color{30, 40, 40, 255});
            MLimb(S.Head(-3, 7), S.Head(10, 12), 7 * s, 6 * s, Color{120, 130, 110, 255}, CLOTH);   // the beard, kelp grown through it
            for (int k = 0; k < 3; k++) I.chains[k].Draw(s);
            MQuad(S.Head(-13, -13), S.Head(13, -14), S.Head(12, -4), S.Head(-12, -4), cap, CLOTH);   // a knit watch cap...
            MLimb(S.Head(-13, -5), S.Head(13, -5), 3 * s, 3 * s, Tone(cap, -0.2f), CLOTH);           // ...with a rolled brim
            for (int k = 0; k < 4; k++) DrawLineEx(S.Head(-9 + k * 6.0f, -13), S.Head(-9 + k * 6.0f, -6), 0.8f * s, Tone(cap, -0.35f));
        });
        parts.Add(0.8f, [&] {   // the squeezebox: carved end boxes with buttons, the bellows' folds, a strap
            Vector2 a = S.p[WR_B], c = S.p[WR_F];
            Color box{120, 44, 32, 255};
            MQuad({a.x - 7 * s, a.y - 17 * s}, {a.x + 4 * s, a.y - 17 * s}, {a.x + 4 * s, a.y + 15 * s}, {a.x - 7 * s, a.y + 15 * s}, box, CLOTH);
            MQuad({c.x - 4 * s, c.y - 17 * s}, {c.x + 7 * s, c.y - 17 * s}, {c.x + 7 * s, c.y + 15 * s}, {c.x - 4 * s, c.y + 15 * s}, box, CLOTH);
            for (int k = 0; k < 5; k++) { DrawCircleV({c.x + 2 * s, c.y - 12 * s + k * 6 * s}, 1.3f * s, Color{230, 220, 196, 255}); DrawCircleV({a.x - 2 * s, a.y - 10 * s + k * 5 * s}, 1.1f * s, Color{230, 220, 196, 255}); }
            int folds = 7;
            for (int k = 0; k < folds; k++) {
                float u0 = k / (float)folds, u1 = (k + 1) / (float)folds;
                float x0 = a.x + 4 * s + (c.x - a.x - 8 * s) * u0, x1 = a.x + 4 * s + (c.x - a.x - 8 * s) * u1;
                MQuad({x0, a.y - 15 * s}, {x1, a.y - 14 * s}, {x1, a.y + 13 * s}, {x0, a.y + 14 * s}, k % 2 ? Color{214, 200, 168, 255} : Color{160, 146, 122, 255}, CLOTH);
            }
            MBall(a, 5.2f * s, skin, SKIN); MBall(c, 5.2f * s, skin, SKIN);
        });
        parts.Draw();
    });
}

// the merman on a coiled brass horn: a fan of fin crest (chains), fin ears, a shell necklace, a long scaled tail
// coiled on the boards; he leans back on long notes and taps his tail fin on the beat
void Merman(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90002); Tick(I, in.t);
        float s = 1.1f, f = -1, t = in.t;
        Build b; b.thigh = 30; b.shin = 30; b.upper = 26; b.fore = 25; b.spine = 30; b.chest = 25; b.head = 12; b.shoulderW = 18; b.stanceF = 4; b.stanceB = -4;
        RPose P;
        bool longNote = in.bandPlaying && fmodf(bt.bar, 4) >= 1 && fmodf(bt.bar, 4) < 3;
        P[C_LEAN] = longNote ? -0.18f : 0.02f + 0.02f * sinf(t * 0.5f);
        P[C_HEAD] = in.bandPlaying ? -0.25f : 0.12f;
        P[C_HFX] = in.bandPlaying ? 14 : 8; P[C_HFY] = in.bandPlaying ? -6 : 34; P[C_HBX] = in.bandPlaying ? 18 : -4; P[C_HBY] = in.bandPlaying ? 4 : 36;
        P[C_HIPY] = 18;
        Solved S = SolveHumanoid(b, P, ft, s, f);
        Color scale{56, 128, 116, 255}, skin{126, 170, 156, 255}, fin{190, 100, 60, 255};
        Hang(I, 0, S.Head(-2, -13), 7, 6 * s, {-0.75f * f, 0.55f}, fin, 5, 1.4f);
        Hang(I, 1, S.Head(-7, -9), 6, 6 * s, {-0.9f * f, 0.45f}, Tone(fin, -0.15f), 4, 1.2f);
        Hang(I, 2, S.Head(-10, -3), 5, 6 * s, {-0.95f * f, 0.3f}, Tone(fin, -0.3f), 3.2f, 1);
        float tap = in.bandPlaying ? bt.pulse * 7 : 0;
        Parts parts;
        parts.Add(-1, [&] {   // the tail, coiled on the boards
            Vector2 h = S.p[HIPS];
            Vector2 p1{h.x - 8 * f * s, h.y + 26 * s}, p2{h.x + 14 * s * f, h.y + 46 * s}, p3{h.x + 44 * f * s, h.y + 42 * s - tap};
            MLimb(h, p1, 15 * s, 13 * s, scale, WET); MLimb(p1, p2, 13 * s, 10 * s, scale, WET); MLimb(p2, p3, 10 * s, 5 * s, Tone(scale, -0.1f), WET);
            for (int k = 0; k < 9; k++) { float u = k / 9.0f; Vector2 q = u < 0.5f ? Vector2{p1.x + (p2.x - p1.x) * u * 2, p1.y + (p2.y - p1.y) * u * 2} : Vector2{p2.x + (p3.x - p2.x) * (u - 0.5f) * 2, p2.y + (p3.y - p2.y) * (u - 0.5f) * 2}; DrawCircleLinesV(q, 3.2f * s, Fade(Tone(scale, 0.35f), 0.7f)); }
            for (int k = 0; k < 5; k++) { float a = (-0.7f + k * 0.35f); DrawTri(p3, {p3.x + cosf(a) * 20 * f * s, p3.y + sinf(a) * 20 * s}, {p3.x + cosf(a + 0.3f) * 18 * f * s, p3.y + sinf(a + 0.3f) * 18 * s}, Tone(fin, k % 2 ? -0.1f : 0.05f)); }
        });
        parts.Add(-0.5f, [&] { MLimb(S.p[SH_B], S.p[EL_B], 7.5f * s, 6.5f * s, Tone(skin, -0.15f), SKIN); MLimb(S.p[EL_B], S.p[WR_B], 6.5f * s, 5.5f * s, Tone(skin, -0.15f), SKIN); });
        parts.Add(0, [&] {
            MQuad(S.Chest(-18, -22), S.Chest(18, -22), S.Hips(14, 2), S.Hips(-14, 2), skin, SKIN);
            for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-4, -6 + k * 8.0f), S.Chest(8, -6 + k * 8.0f), 1 * s, Fade(Tone(skin, -0.5f), 0.6f));   // a muscled front
            DrawLineEx(S.Chest(2, -14), S.Hips(2, -6), 1 * s, Fade(Tone(skin, -0.5f), 0.6f));
            for (int k = 0; k < 7; k++) DrawCircleLinesV(S.Chest(-14 + (k % 4) * 5.0f, -20 + (k / 4) * 5.0f), 2.2f * s, Fade(Tone(scale, 0.3f), 0.8f));   // scales over the shoulder
            MQuad(S.Hips(-15, -7), S.Hips(15, -7), S.Hips(14, 4), S.Hips(-14, 4), scale, WET);
            for (int k = 0; k < 5; k++) { Vector2 sp = S.Chest(-8 + k * 5.0f, -22 + (k % 2 ? 3.0f : 5.0f)); DrawCircleV(sp, 2.2f * s, Color{236, 214, 190, 255}); DrawCircleV(sp, 1 * s, Color{210, 150, 120, 255}); }   // a shell necklace
        });
        parts.Add(0.4f, [&] {
            MBall(S.p[HEAD], 12.5f * s, skin, SKIN);
            DrawTri(S.Head(-8, -2), S.Head(-19, -9), S.Head(-15, 4), fin);                    // a fin ear
            MLimb(S.Head(9, -1), S.Head(12, 5), 1.8f * s, 2.4f * s, Tone(skin, -0.1f), SKIN);
            Eyes(I, S.Head(4, -3), 0, 2.0f, s, Color{240, 236, 196, 255}, Color{16, 70, 64, 255});
            for (int k = 0; k < 3; k++) I.chains[k].Draw(s);
        });
        parts.Add(0.8f, [&] {   // the horn: a double coil, valves, a flared bell with a lip
            MLimb(S.p[SH_F], S.p[EL_F], 7.5f * s, 6.5f * s, skin, SKIN); MLimb(S.p[EL_F], S.p[WR_F], 6.5f * s, 5.5f * s, skin, SKIN);
            Vector2 c = in.bandPlaying ? S.Head(15, 9) : Off2(S.p[WR_F], 0, 12 * s);
            DrawRing(c, 9 * s, 12.5f * s, 0, 360, 28, Pal::Brass);
            DrawRing(c, 4.5f * s, 6.5f * s, 0, 360, 20, Tone(Pal::Brass, -0.2f));
            for (int k = 0; k < 3; k++) MBall({c.x + (k - 1) * 4.5f * s, c.y - 13 * s}, 1.8f * s, Tone(Pal::Brass, 0.2f), METAL);
            Vector2 bell{c.x + 22 * f * s, c.y - (in.bandPlaying ? 16 : -8) * s};
            MLimb(c, bell, 3 * s, 11 * s, Pal::Brass, METAL);
            MBall(bell, 11 * s, Tone(Pal::Brass, 0.1f), METAL);
            DrawCircleV(bell, 7 * s, Color{40, 26, 12, 255});
            MBall(S.p[WR_F], 4.8f * s, skin, SKIN); MBall(S.p[WR_B], 4.8f * s, skin, SKIN);
        });
        parts.Draw();
    });
}

// the hermit crab in a diving-bell shell, on the double bass: two claws pluck, eyestalks bob on the downbeat
void Crab(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90003); Tick(I, in.t);
        float t = in.t;
        Color shell = Pal::Brass, body{206, 86, 52, 255}, wood{150, 78, 36, 255};
        float bob = in.bandPlaying ? (bt.beatInBar == 0 ? bt.pulse * 5 : bt.pulse * 1.5f) : 1.5f * sinf(t * 0.5f);
        Vector2 c{ft.x + 20, ft.y - 54};
        Hang(I, 0, {c.x - 22, c.y - 50 - bob}, 6, 6, {-0.6f, -0.45f}, Tone(body, 0.2f), 1.6f, 0.5f);
        Hang(I, 1, {c.x - 10, c.y - 52 - bob}, 6, 6, {-0.25f, -0.65f}, Tone(body, 0.2f), 1.6f, 0.5f);
        // the double bass: two bouts and a waist, f-holes, bridge, tailpiece, a long black fingerboard, the scroll
        Vector2 bb{c.x - 66, ft.y - 62};
        ShadeBall({bb.x, bb.y}, 36, wood);
        ShadeBall({bb.x + 1, bb.y - 50}, 27, Tone(wood, 0.05f));
        Q4({bb.x - 20, bb.y - 30}, {bb.x + 20, bb.y - 30}, {bb.x + 24, bb.y - 10}, {bb.x - 24, bb.y - 10}, Tone(wood, -0.05f));
        for (int sgn = -1; sgn <= 1; sgn += 2) { Vector2 fh{bb.x + sgn * 13.0f, bb.y - 14}; DrawLineEx({fh.x, fh.y - 12}, {fh.x + sgn * 2.0f, fh.y + 10}, 1.6f, Color{30, 14, 8, 255}); DrawCircleV({fh.x, fh.y - 12}, 1.6f, Color{30, 14, 8, 255}); DrawCircleV({fh.x + sgn * 2.0f, fh.y + 10}, 1.6f, Color{30, 14, 8, 255}); }
        DrawRectangle((int)bb.x - 9, (int)bb.y + 2, 18, 4, Color{220, 190, 140, 255});                     // the bridge
        Q4({bb.x - 5, bb.y + 12}, {bb.x + 5, bb.y + 12}, {bb.x + 4, bb.y + 32}, {bb.x - 4, bb.y + 32}, Color{30, 20, 16, 255});   // the tailpiece
        DrawLineEx({bb.x, bb.y + 34}, {bb.x, ft.y}, 2, Color{120, 120, 120, 255});                           // the endpin
        ShadeLimb({bb.x, bb.y - 20}, {bb.x + 4, bb.y - 186}, 4.5f, 3.5f, Color{24, 18, 16, 255});             // the fingerboard
        DrawCircleV({bb.x + 5, bb.y - 192}, 7, Tone(wood, -0.2f)); DrawCircleLinesV({bb.x + 5, bb.y - 192}, 4, Color{40, 20, 10, 255});   // the scroll
        for (int k = 0; k < 2; k++) { DrawLineEx({bb.x - 4, bb.y - 170 + k * 8.0f}, {bb.x - 12, bb.y - 172 + k * 8.0f}, 2, Pal::Brass); DrawLineEx({bb.x + 10, bb.y - 166 + k * 8.0f}, {bb.x + 18, bb.y - 168 + k * 8.0f}, 2, Pal::Brass); }
        for (int k = -1; k <= 2; k++) DrawLineEx({bb.x + k * 2.5f, bb.y + 12}, {bb.x + 3 + k * 1.1f, bb.y - 180}, 0.9f, Color{230, 220, 196, 255});
        // the shell: a riveted brass diving bell, a band of bolts, a porthole glowing warm from inside
        ShadeBall({c.x + 26, c.y - 10}, 44, shell);
        ShadeLimb({c.x - 18, c.y + 22}, {c.x + 70, c.y + 22}, 7, 7, Tone(shell, -0.25f));
        Rivets(c.x - 12, c.x + 64, c.y + 22, 9, shell);
        DrawRing({c.x + 34, c.y - 18}, 12, 17, 0, 360, 28, Tone(shell, -0.35f));
        DrawCircleV({c.x + 34, c.y - 18}, 12, Color{120, 80, 40, 255});
        Vector2 pc{c.x + 34, c.y - 18};
        RigAfterInk([pc] { DrawCircleV(pc, 10, Color{120, 70, 20, 255}); DrawCircleV(pc, 5, Color{160, 100, 30, 255}); });
        // legs, the body, the claws on the strings
        for (int k = 0; k < 3; k++) { Vector2 hip{c.x - 6 + k * 12.0f, c.y + 24}; Vector2 knee{hip.x - 12 + k * 4.0f, hip.y + 14}; ShadeLimb(hip, knee, 4.5f, 3.5f, body); ShadeLimb(knee, {knee.x - 5, ft.y - 1}, 3.5f, 2, body); }
        ShadeBall({c.x - 18, c.y + 6}, 18, body);
        float pl = in.bandPlaying ? bt.pulse : 0;
        auto claw = [&](Vector2 from, Vector2 to, float open) {
            ShadeLimb(from, to, 6, 5, body);
            ShadeBall(to, 8, Tone(body, 0.1f));
            DrawTri(to, {to.x - 12, to.y - 6 - open * 4}, {to.x - 4, to.y - 9}, Tone(body, 0.15f));
            DrawTri(to, {to.x - 12, to.y + 3 + open * 4}, {to.x - 4, to.y + 7}, Tone(body, 0.0f));
        };
        claw({c.x - 26, c.y + 2}, {bb.x + 18, bb.y - 20 + pl * 6}, pl);
        claw({c.x - 24, c.y - 8}, {bb.x + 12, bb.y - 124 + 2 * sinf(t * 0.4f)}, 0.2f);
        for (int k = 0; k < 2; k++) {
            Vector2 base{c.x - 22 + k * 11.0f, c.y - 10}, eye{base.x - 4 + k * 5.0f, base.y - 34 - bob};
            ShadeLimb(base, eye, 2.6f, 2.2f, body);
            DrawCircleV(eye, 5.5f, I.face.Closed() ? Tone(body, -0.2f) : Color{16, 16, 20, 255});
            if (!I.face.Closed()) DrawCircleV({eye.x - 1.5f, eye.y - 2}, 1.6f, WHITE);
        }
        I.chains[0].Draw(1); I.chains[1].Draw(1);
    });
}

// the clockwork drummer at a brushed kit: pistons in the arms, a gauge in the chest, a winding key turning slowly;
// brushes swirl on the snare in time and a small steam puff rises on beats 2 and 4 (drawn by the caller)
void Drummer(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90004); Tick(I, in.t);
        float t = in.t;
        Color brass = Pal::Brass, dark{70, 62, 54, 255}, drum{130, 36, 32, 255};
        Vector2 hip{ft.x + 12, ft.y - 64}, chest{hip.x, hip.y - 54}, head{chest.x - 4, chest.y - 40};
        // the bass drum and hi-hat behind him, the seat
        Vector2 kick{ft.x - 44, ft.y - 36};
        DrawEllipse((int)kick.x, (int)kick.y, 30, 36, drum);
        DrawEllipse((int)kick.x, (int)kick.y, 25, 31, Color{226, 214, 190, 255});
        DrawTextCentered("L", kick.x, kick.y - 10, 20, Color{150, 40, 32, 255});
        DrawEllipseLines((int)kick.x, (int)kick.y, 30, 36, Pal::Brass);
        Vector2 hh{ft.x + 44, ft.y - 104};
        DrawLineEx({hh.x, hh.y}, {hh.x, ft.y}, 2, dark); DrawEllipse((int)hh.x, (int)hh.y, 20, 3, Color{210, 176, 96, 255}); DrawEllipse((int)hh.x, (int)hh.y + 4, 20, 3, Color{190, 156, 80, 255});
        Vector2 ride{ft.x - 10, ft.y - 136};
        DrawLineEx({ride.x, ride.y}, {ride.x + 8, ft.y - 70}, 2, dark); DrawEllipse((int)ride.x, (int)ride.y, 26, 5, Color{210, 176, 96, 255});
        DrawRectangle((int)hip.x - 10, (int)hip.y + 6, 34, 8, Color{60, 36, 22, 255});
        DrawLineEx({hip.x + 6, hip.y + 14}, {hip.x + 6, ft.y}, 3, dark);
        // the legs: jointed brass
        ShadeLimb({hip.x - 10, hip.y + 4}, {hip.x - 28, ft.y - 22}, 7, 6, dark); ShadeLimb({hip.x - 28, ft.y - 22}, {hip.x - 32, ft.y}, 6, 5, dark);
        MBall({hip.x - 28, ft.y - 22}, 5, brass, METAL);
        // the torso: a riveted boiler with a pressure gauge, a winding key on its back
        ShadeQuad({chest.x - 22, chest.y - 26}, {chest.x + 22, chest.y - 26}, {hip.x + 19, hip.y + 4}, {hip.x - 19, hip.y + 4}, brass);
        Rivets(chest.x - 18, chest.x + 18, chest.y - 20, 8, brass);
        Rivets(chest.x - 16, chest.x + 16, hip.y, 8, brass);
        DrawGauge({chest.x - 2, chest.y - 2}, 8, 0.4f + 0.1f * sinf(t * 0.3f), Color{230, 220, 196, 255});
        float key = t * 0.8f;
        Vector2 kc{chest.x + 26, chest.y - 6};
        DrawLineEx(kc, {kc.x + cosf(key) * 11, kc.y + sinf(key) * 11}, 3.5f, Tone(brass, -0.2f)); DrawLineEx(kc, {kc.x - cosf(key) * 11, kc.y - sinf(key) * 11}, 3.5f, Tone(brass, -0.2f));
        Hang(I, 0, {chest.x + 18, chest.y + 12}, 6, 5, {0.2f, 1}, Color{80, 78, 74, 255}, 1.6f, 1.2f, METAL);
        I.chains[0].Draw(1);
        // the head: a riveted box, two lens eyes behind a shutter that blinks, an antenna, the steam pipe
        ShadeQuad({head.x - 15, head.y - 17}, {head.x + 15, head.y - 17}, {head.x + 15, head.y + 15}, {head.x - 15, head.y + 15}, Tone(brass, -0.1f));
        Rivets(head.x - 12, head.x + 12, head.y + 11, 8, brass);
        for (int k = 0; k < 2; k++) {
            Vector2 e{head.x - 12 + k * 10.0f, head.y - 2};
            DrawCircleV(e, 5, Color{30, 26, 22, 255});
            DrawCircleV(e, 3.2f, I.face.Closed() ? Tone(brass, -0.3f) : Color{255, 200, 120, 255});
        }
        DrawLineEx({head.x + 8, head.y - 17}, {head.x + 12, head.y - 34}, 1.5f, dark); DrawCircleV({head.x + 12, head.y - 34}, 2.5f, Color{200, 60, 40, 255});
        DrawRectangle((int)head.x - 2, (int)head.y - 28, 6, 12, dark);
        // the snare in front
        Vector2 sn{ft.x - 58, ft.y - 50};
        ShadeQuad({sn.x - 28, sn.y}, {sn.x + 28, sn.y}, {sn.x + 28, sn.y + 20}, {sn.x - 28, sn.y + 20}, drum);
        for (int k = 0; k < 6; k++) DrawLineEx({sn.x - 25 + k * 10.0f, sn.y + 1}, {sn.x - 25 + k * 10.0f, sn.y + 19}, 1.4f, Pal::Brass);
        DrawEllipse((int)sn.x, (int)sn.y, 28, 6, Color{226, 220, 202, 255});
        DrawLineEx({sn.x - 22, sn.y + 20}, {sn.x - 28, ft.y}, 2, dark); DrawLineEx({sn.x + 22, sn.y + 20}, {sn.x + 28, ft.y}, 2, dark);
        // arms with pistons, brushes swirling in time
        float ang = in.bandPlaying ? (float)(in.beat * PI) : 0.3f, r = in.bandPlaying ? 10.0f : 0;
        Vector2 b1{sn.x - 9 + cosf(ang) * r, sn.y - 2 + sinf(ang) * r * 0.3f}, b2{sn.x + 11 - cosf(ang) * r, sn.y - 2 - sinf(ang) * r * 0.3f};
        if (!in.bandPlaying) { b1 = {sn.x - 6, sn.y + 4}; b2 = {sn.x + 14, sn.y + 4}; }
        for (Vector2 bp : {b1, b2}) {
            Vector2 sh{chest.x - 18, chest.y - 20}, el{(sh.x + bp.x) * 0.5f + 6, (sh.y + bp.y) * 0.5f + 16};
            ShadeLimb(sh, el, 6.5f, 5.5f, dark); ShadeLimb(el, {bp.x + 10, bp.y - 12}, 5.5f, 4.5f, dark);
            DrawLineEx({sh.x + 4, sh.y + 4}, {el.x + 2, el.y - 2}, 1.2f, Pal::Brass);   // the piston rod
            MBall(el, 4.5f, brass, METAL);
            DrawLineEx({bp.x + 10, bp.y - 12}, bp, 2, Color{120, 90, 60, 255});
            for (int k = -2; k <= 2; k++) DrawLineEx(bp, {bp.x - 6 + k * 2.0f, bp.y + 4}, 0.8f, Color{210, 210, 200, 255});
        }
    });
}

// the octopus behind the bar: a mottled mantle, big eyes with bar pupils, a white collar and a black bow tie; two
// arms polish glasses in slow circles, two drape over the bar top, the rest curl behind the counter
void Octopus(Vector2 feet, const SceneInputs& in) {
    Figure(feet, 1, [&](Vector2 ft) {
        Instance& I = Get(90005); Tick(I, in.t);
        float t = in.t;
        Color skin{176, 84, 96, 255}, skinDk = Tone(skin, -0.25f);
        Vector2 head{ft.x, ft.y - 130 + 2 * sinf(t * 0.5f)};
        for (int k = 0; k < 4; k++) Hang(I, k, {head.x - 30 + k * 20.0f, head.y + 34}, 7, 9, {(k - 1.5f) * 0.35f, 1}, skinDk, 8, 2.5f, WET);
        for (int k = 0; k < 4; k++) I.chains[k].Draw(1);
        // the mantle: a bulb leaning back over the eyes, mottled
        ShadeBall({head.x - 10, head.y - 34}, 33, skin);   // the mantle, leaning back...
        ShadeBall({head.x - 5, head.y - 18}, 32, skin);   // ...into the head
        ShadeBall({head.x, head.y - 2}, 31, skin);
        for (int k = 0; k < 14; k++) DrawCircleV({head.x - 30 + Hash(k) * 50, head.y - 66 + Hash(k + 30) * 60}, 2 + 2.5f * Hash(k + 60), Fade(skinDk, 0.7f));
        // the eyes: gold with horizontal bar pupils, heavy lids that blink
        for (int k = 0; k < 2; k++) {
            Vector2 e{head.x - 14 + k * 28.0f, head.y - 6};
            ShadeBall(e, 10, Tone(skin, 0.1f));
            if (I.face.Closed()) { DrawLineEx({e.x - 8, e.y}, {e.x + 8, e.y}, 2.5f, skinDk); continue; }
            DrawCircleV(e, 7.5f, Color{226, 190, 90, 255});
            DrawRectangle((int)e.x - 6, (int)e.y - 2, 12, 4, Color{16, 12, 12, 255});
            DrawCircleV({e.x - 2, e.y - 3}, 1.5f, WHITE);
        }
        // a starched collar and a bow tie
        Q4({head.x - 22, head.y + 22}, {head.x + 22, head.y + 22}, {head.x + 18, head.y + 32}, {head.x - 18, head.y + 32}, Color{236, 232, 220, 255});
        DrawTri({head.x, head.y + 28}, {head.x - 13, head.y + 21}, {head.x - 13, head.y + 35}, Color{20, 18, 20, 255});
        DrawTri({head.x, head.y + 28}, {head.x + 13, head.y + 21}, {head.x + 13, head.y + 35}, Color{20, 18, 20, 255});
        DrawCircleV({head.x, head.y + 28}, 3.5f, Color{40, 36, 40, 255});
        // two polishing arms: a slow circle each, a glass and a cloth in each curl
        for (int k = 0; k < 2; k++) {
            float a = t * 1.6f + k * PI;   // (0.25 Hz)
            float sg = k ? 1.0f : -1.0f;
            Vector2 root{head.x + sg * 18, head.y + 26}, mid{head.x + sg * 40, head.y + 48}, tip{head.x + sg * 56 + cosf(a) * 7, head.y + 34 + sinf(a) * 4};
            ShadeLimb(root, mid, 9, 7, skin); ShadeLimb(mid, tip, 7, 4, skin);
            for (int q = 0; q < 4; q++) { float u = 0.2f + q * 0.2f; DrawCircleV({mid.x + (tip.x - mid.x) * u, mid.y + (tip.y - mid.y) * u + 3}, 1.6f, Color{236, 200, 200, 255}); }   // suckers
            Vector2 g{tip.x + sg * 4, tip.y - 14};
            DrawTri({g.x - 9, g.y - 13}, {g.x + 9, g.y - 13}, {g.x, g.y + 4}, Fade(Color{200, 230, 240, 255}, 0.6f));
            DrawLineEx({g.x, g.y + 4}, {g.x, g.y + 13}, 1.5f, Fade(Color{200, 230, 240, 255}, 0.7f));
            DrawEllipse((int)g.x, (int)g.y + 13, 6, 2, Fade(Color{200, 230, 240, 255}, 0.7f));
            DrawCircleV({tip.x, tip.y - 2}, 6, Color{236, 230, 216, 255});   // the cloth
        }
    });
}

// the patrons: strange regulars in near-silhouette; slow breathing, a very occasional sip. Each has its own shape:
// a diver still in his brass helmet, a fish-headed sailor, a feathered hat, a top hat, a hood, a sou'wester, a bald brute
void Patron(int i, Vector2 feet, const SceneInputs& in) {
    Figure(feet, i % 2 ? 1.0f : -1.0f, [&](Vector2 ft) {
        Instance& I = Get(90100 + i); Tick(I, in.t);
        float t = in.t + i * 7.3f, f = i % 2 ? 1.0f : -1.0f;
        float br = 1.5f * sinf(t * 1.2f + i);                            // breathing, 0.19 Hz
        float cyc = fmodf(t + i * 13, 45.0f), act = cyc < 4 ? sinf(cyc / 4 * PI) : 0;   // once in 45 s: a sip, slow
        Color c = Mix(Color{62, 54, 62, 255}, Color{100, 84, 78, 255}, Hash(i) * 0.6f);
        float wide = i == 6 ? 1.3f : 1.0f;
        Vector2 hip{ft.x, ft.y - 46}, sh{hip.x + 4 * f, hip.y - 58 - br}, head{sh.x + 3 * f, sh.y - 21};
        ShadeQuad({hip.x - 19 * wide, hip.y}, {hip.x + 19 * wide, hip.y}, {sh.x + 22 * wide, sh.y}, {sh.x - 22 * wide, sh.y}, c);
        Color hc = Tone(c, 0.1f);
        switch (i) {
        case 0: ShadeBall(head, 17, Color{150, 116, 60, 255}); DrawRing(head, 6, 9, 0, 360, 20, Color{90, 70, 40, 255}); DrawCircleV(head, 6, Color{40, 60, 70, 255}); break;   // the diver's helmet
        case 1: ShadeBall(head, 12, Color{90, 110, 104, 255}); DrawTri({head.x - 10 * f, head.y - 8}, {head.x - 24 * f, head.y - 22}, {head.x - 6 * f, head.y - 14}, Color{80, 96, 90, 255}); DrawTri({head.x + 6 * f, head.y + 2}, {head.x + 20 * f, head.y + 6}, {head.x + 6 * f, head.y + 8}, Color{70, 86, 80, 255}); break;   // a fish head with a dorsal fin
        case 2: ShadeBall(head, 12, hc); Q4({head.x - 20, head.y - 6}, {head.x + 20, head.y - 6}, {head.x + 16, head.y - 12}, {head.x - 16, head.y - 12}, Color{90, 40, 60, 255}); ShadeLimb({head.x + 6 * f, head.y - 10}, {head.x - 14 * f, head.y - 34}, 3, 1, Color{200, 190, 170, 255}); break;   // a feathered hat
        case 3: ShadeBall(head, 12, hc); DrawRectangle((int)head.x - 18, (int)head.y - 10, 36, 4, Tone(c, -0.35f)); Q4({head.x - 10, head.y - 34}, {head.x + 10, head.y - 34}, {head.x + 10, head.y - 10}, {head.x - 10, head.y - 10}, Tone(c, -0.35f)); break;   // a top hat
        case 4: ShadeBall({head.x, head.y - 2}, 15, Tone(c, -0.15f)); DrawCircleV({head.x + 5 * f, head.y + 2}, 8, Color{20, 16, 16, 255}); break;   // a hood
        case 5: ShadeBall(head, 12, hc); Q4({head.x - 16, head.y - 4}, {head.x + 20, head.y - 2}, {head.x + 10, head.y - 18}, {head.x - 10, head.y - 18}, Color{190, 150, 50, 255}); break;   // a sou'wester
        default: ShadeBall(head, 13, hc); break;   // bald and broad
        }
        Hang(I, 0, {sh.x - 6 * f, sh.y + 4}, 5, 7, {-0.4f * f, 1}, i % 2 ? Color{130, 44, 40, 255} : Color{56, 78, 100, 255}, 4, 2);   // a scarf end
        I.chains[0].Draw(1);
        if (!I.face.Closed() && i != 0 && i != 4) DrawCircleV({head.x + 6 * f, head.y - 2}, 1.4f, Color{220, 196, 150, 220});   // an eye catching the lamp, and blinking
        Vector2 hand{sh.x + 24 * f, sh.y + 28 - act * 22};
        ShadeLimb({sh.x + 12 * f, sh.y + 6}, hand, 6.5f, 5, Tone(c, 0.05f));
        DrawRectangle((int)hand.x - 3, (int)hand.y - 9, 8, 11, Fade(Color{226, 180, 90, 255}, 0.75f));   // a glass of something amber
        DrawEllipse((int)(ft.x + 42 * f), (int)(ft.y - 44), 38, 9, Color{50, 32, 22, 255});   // the table
        DrawRectangle((int)(ft.x + 40 * f), (int)(ft.y - 44), 5, 44, Color{34, 22, 16, 255});
    });
}

void Lounge(const SceneInputs& in) {
    float t = in.t;
    Beat bt = BeatOf(in);
    SceneLight sl = SalonLight();
    sl.keyDir = {0.3f, -0.95f}; sl.key = {255, 190, 110, 255}; sl.fill = {70, 110, 160, 255}; sl.rim = {120, 170, 210, 255}; sl.fog = {20, 22, 30, 255};
    SetSceneLight(sl);
    SetInkLook(&SalonPalette(), 0.2f, 777);
    SetPost(0.55f, 0.02f, 0.25f);
    // the back: a curved riveted wall with three portholes onto the deep
    DrawVGradient({0, 0, (float)SCREEN_W, 470}, Color{46, 36, 32, 255}, Color{32, 24, 22, 255});
    DrawTiled(Tex::Metal, {0, 30, (float)SCREEN_W, 380}, 0.9f, Color{92, 72, 58, 255});
    for (int k = 0; k < 6; k++) DrawRectangleGradientH(k * 230 - 10, 30, 20, 380, Color{34, 26, 22, 255}, Color{100, 80, 62, 255});
    Rivets(0, SCREEN_W, 40, 22, Pal::Brass); Rivets(0, SCREEN_W, 400, 22, Pal::Brass);
    const float portX[2] = {540, 950};
    for (int k = 0; k < 2; k++) {
        Vector2 c{portX[k], 190};
        DrawCircleV(c, 66, Color{12, 44, 64, 255});
        DrawCircleV({c.x - 18, c.y - 22}, 40, Fade(Color{40, 90, 120, 255}, 0.35f));
        for (int f = 0; f < 3; f++) {   // slow fish crossing, fading in and out at the rim (nothing appears suddenly)
            float ph = fmodf(t * (0.012f + 0.004f * f) + Hash(k * 5 + f), 1.0f);
            float x = c.x - 80 + ph * 160, y = c.y - 30 + f * 26 + 5 * sinf(t * 0.3f + f);
            float a = std::clamp((60 - fabsf(x - c.x)) / 30, 0.0f, 1.0f) * 0.6f;
            DrawEllipse((int)x, (int)y, 11 - f * 2, 4, Fade(Color{24, 80, 100, 255}, a));
            DrawTri({x - 10, y}, {x - 17, y - 5}, {x - 17, y + 5}, Fade(Color{24, 80, 100, 255}, a));
        }
        DrawRing(c, 64, 80, 0, 360, 48, Tone(Pal::Brass, -0.1f));
        DrawRing(c, 76, 80, 200, 320, 24, Tone(Pal::Brass, 0.3f));   // a highlight on the rim
        for (int b = 0; b < 12; b++) { float a = b * PI / 6; DrawCircleV({c.x + cosf(a) * 72, c.y + sinf(a) * 72}, 3, Tone(Pal::Brass, -0.4f)); }
    }
    // the floor: planks running toward the stage, a round rug
    DrawVGradient({0, 440, (float)SCREEN_W, 280}, Color{66, 44, 30, 255}, Color{30, 20, 14, 255});
    for (int k = -12; k <= 12; k++) DrawLineEx({640 + k * 40.0f, 440}, {640 + k * 150.0f, 720}, 1.2f, Fade(Color{20, 12, 8, 255}, 0.5f));
    for (int y = 460; y < 720; y += 26 + (y - 440) / 8) DrawLine(0, y, SCREEN_W, y, Fade(Color{20, 12, 8, 255}, 0.35f));
    DrawEllipse(560, 560, 230, 40, Color{90, 34, 30, 255});
    DrawEllipseLines(560, 560, 214, 34, Fade(Pal::Brass, 0.45f));
    // the bar along the left: shelves of bottles, hanging glass vessels, lamps, copper taps on the counter
    Color wood{80, 46, 28, 255};
    DrawRectangle(0, 110, 380, 262, Tone(wood, -0.45f));
    DrawTiled(Tex::Wood, {6, 116, 368, 250}, 1.0f, Tone(wood, -0.5f));
    for (int s = 0; s < 3; s++) { DrawRectangle(10, 180 + s * 70, 356, 8, wood); for (int k = 0; k < 12; k++) { float x = 22 + k * 28; Color bc = Mix(Color{50, 100, 64, 255}, Color{160, 70, 44, 255}, Hash(s * 20 + k)); DrawRectangle((int)x, 180 + s * 70 - 34, 13, 34, Fade(bc, 0.9f)); DrawRectangle((int)x + 4, 180 + s * 70 - 46, 5, 12, Fade(bc, 0.9f)); DrawRectangle((int)x + 2, 180 + s * 70 - 26, 9, 10, Fade(Color{226, 214, 186, 255}, 0.7f)); } }
    for (int k = 0; k < 5; k++) {   // glass vessels hung from a pipe, swaying very slowly
        float x = 60 + k * 70, sw = 3 * sinf(t * 0.4f + k);
        DrawLineEx({x, 60}, {x + sw, 100}, 1, Color{60, 50, 40, 255});
        DrawCircleV({x + sw, 110}, 11, Fade(Color{140, 200, 190, 255}, 0.4f));
        DrawCircleLinesV({x + sw, 110}, 11, Fade(Color{220, 240, 230, 255}, 0.5f));
    }
    DrawRectangle(0, 56, 400, 8, Tone(Pal::Copper, -0.2f));
    Octopus({210, 405}, in);
    DrawRectangle(0, 372, 440, 158, wood);   // the counter's front: panels and a brass foot rail
    DrawTiled(Tex::Wood, {0, 384, 440, 146}, 0.9f, Tone(wood, -0.05f));
    for (int k = 0; k < 4; k++) DrawRectangleLinesEx({12 + k * 106.0f, 400, 92, 100}, 2, Fade(Color{30, 16, 10, 255}, 0.6f));
    DrawRectangle(0, 510, 440, 5, Pal::Brass);
    DrawRectangle(0, 364, 450, 14, Tone(wood, 0.35f));   // its top
    for (int k = 0; k < 3; k++) { float x = 90 + k * 90; DrawRectangle((int)x, 328, 8, 36, Pal::Copper); DrawCircleV({x + 4, 328}, 7, Pal::Copper); DrawRectangle((int)x - 6, 336, 20, 5, Tone(Pal::Copper, -0.3f)); }
    for (int k = 0; k < 2; k++) { float x = 110 + k * 160.0f; DrawLineEx({x, 0}, {x, 70}, 1.5f, Color{40, 34, 30, 255}); DrawTri({x - 16, 84}, {x + 16, 84}, {x, 66}, Tone(Pal::Copper, -0.1f)); }   // two lamps over the bar
    for (int k = 0; k < 3; k++) { float x = 90 + k * 120.0f; DrawEllipse((int)x, 548, 26, 7, Color{110, 40, 34, 255}); DrawLineEx({x, 552}, {x, 612}, 4, Color{40, 30, 24, 255}); DrawLineEx({x - 16, 612}, {x + 16, 612}, 3, Color{40, 30, 24, 255}); }   // bar stools
    // two small tables between the bar and the stage, each with a lamp
    for (int k = 0; k < 2; k++) {
        Vector2 tb{520.0f + k * 110, 520.0f + k * 14};
        DrawEllipse((int)tb.x, (int)tb.y, 34, 9, Color{70, 42, 26, 255});
        DrawRectangle((int)tb.x - 3, (int)tb.y, 6, 44, Color{40, 26, 18, 255});
        DrawRectangle((int)tb.x - 14, (int)tb.y + 42, 28, 4, Color{40, 26, 18, 255});
        DrawCircleV({tb.x, tb.y - 12}, 7, Fade(Color{255, 200, 120, 255}, 0.8f));
        DrawRectangle((int)tb.x - 3, (int)tb.y - 6, 6, 6, Pal::Brass);
    }
    // the stage: raised boards under a velvet valance with the house's name, side drapes, footlights
    DrawRectangle(660, 432, 580, 62, Color{64, 40, 26, 255});
    DrawTiled(Tex::Wood, {660, 424, 580, 16}, 0.8f, Color{120, 82, 50, 255});
    DrawRectangle(660, 438, 580, 4, Tone(Pal::Brass, -0.2f));
    Color velvet{120, 26, 34, 255};
    for (int side = 0; side < 2; side++) {
        float x0 = side ? 1196.0f : 662.0f, w = side ? 44.0f : 50.0f;
        Q4({x0, 44}, {x0 + w, 44}, {x0 + w * (side ? 0.1f : 0.9f), 430}, {x0 + w * (side ? 0.0f : 0.4f), 430}, velvet);
        for (int k = 1; k < 4; k++) DrawLineEx({x0 + w * k / 4, 50}, {x0 + w * k / 4 * (side ? 0.3f : 0.8f), 428}, 2, Tone(velvet, -0.35f));
        DrawLineEx({x0 + (side ? 6 : w - 6), 260}, {x0 + (side ? 30 : w - 30), 280}, 4, Pal::Brass);   // a tie-back
    }
    Q4({660, 36}, {1240, 36}, {1240, 70}, {660, 70}, velvet);   // the valance...
    for (int k = 0; k < 29; k++) { float x = 668 + k * 20.0f; DrawTri({x - 10, 70}, {x + 10, 70}, {x, 84}, velvet); DrawCircleV({x, 86}, 2.5f, Pal::Brass); }   // ...swagged, with tassels
    DrawRectangleRounded({840, 40, 220, 26}, 0.4f, 6, Color{40, 16, 18, 255});
    DrawRectangleRoundedLinesEx({840, 40, 220, 26}, 0.4f, 6, 2, Pal::Brass);

    DrawLineEx({950, 0}, {950, 96}, 2, Color{40, 34, 30, 255});
    DrawTri({920, 108}, {980, 108}, {950, 86}, Pal::Brass);
    DrawEllipse(950, 110, 30, 6, Tone(Pal::Brass, -0.3f));
    // the band
    Sailor({742, 430}, in, bt);
    Merman({905, 430}, in, bt);
    Crab({1040, 430}, in, bt);
    Drummer({1172, 430}, in, bt);
    for (int k = 0; k < 11; k++) DrawCircleV({690 + k * 50.0f, 478}, 4, Color{255, 214, 150, 255});   // the footlights
    // the patrons in the foreground
    const float px[7] = {90, 280, 470, 640, 820, 1010, 1190};
    for (int i = 0; i < 7; i++) Patron(i, {px[i], 700.0f + (i % 2) * 10}, in);
    for (int i = 0; i < 7; i++) DrawCircleV({px[i] + (i % 2 ? 42.0f : -42.0f), 650.0f + (i % 2) * 10}, 3, Color{255, 200, 120, 255});   // candles

    // light: the amber stage lamp is the key, the portholes the fill, the bar's lamps, the footlights, the candles
    LightsBegin(Color{88, 80, 88, 255});
    AddCone({950, 110}, PI / 2, 0.42f, 400, Color{255, 190, 110, 255});
    AddLight({950, 320}, 470, Color{255, 180, 100, 255}, 0.95f);
    AddLight({950, 470}, 300, Color{255, 200, 130, 255}, 0.5f);
    for (int k = 0; k < 2; k++) AddLight({portX[k], 190}, 230, Color{80, 150, 200, 255}, 0.55f);
    for (int k = 0; k < 2; k++) AddLight({110 + k * 160.0f, 110}, 230, Color{255, 170, 100, 255}, 0.55f);
    for (int k = 0; k < 2; k++) AddLight({520.0f + k * 110, 508.0f + k * 14}, 120, Color{255, 180, 110, 255}, 0.55f);
    for (int i = 0; i < 7; i++) AddLight({px[i] + (i % 2 ? 42.0f : -42.0f), 650.0f}, 160, Color{255, 180, 110, 255}, 0.6f);
    LightsEnd();
    InkPass(0.85f, 0.8f);
    // after the ink: the beam's haze, slow dust in it, marine snow in the portholes, the drummer's steam, small glows
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 8; k++) {   // a soft volumetric beam: stacked wedges, fading down
        float y0 = 110 + k * 40.0f, y1 = y0 + 40, w0 = 30 + k * 26.0f, w1 = w0 + 26;
        Color bc = Fade(Color{120, 80, 40, 255}, 0.05f * (1 - k / 9.0f));
        Q4({950 - w0, y0}, {950 + w0, y0}, {950 + w1, y1}, {950 - w1, y1}, bc);
    }
    EndBlendMode();
    for (int k = 0; k < 40; k++) {
        float life = fmodf(t * 0.02f + Hash(k), 1.0f);
        float x = 950 + (Hash(k + 3) - 0.5f) * 260 * (0.3f + life) + sinf(t * 0.2f + k) * 8, y = 110 + life * 320;
        DrawCircleV({x, y}, 1.2f, Fade(Color{255, 220, 170, 255}, 0.35f * sinf(life * PI)));
    }
    for (int k = 0; k < 2; k++) for (int m = 0; m < 6; m++) { float ph = fmodf(t * 0.03f + Hash(k * 9 + m), 1.0f); DrawCircleV({portX[k] + (Hash(m + k) - 0.5f) * 90, 130 + ph * 120}, 1, Fade(WHITE, 0.3f * sinf(ph * PI))); }
    if (in.bandPlaying && (bt.beatInBar == 1 || bt.beatInBar == 3)) {
        float u = bt.phase;
        for (int k = 0; k < 4; k++) DrawCircleV({1168 + k * 3.0f + u * 10, 272 - u * 30 - k * 4.0f}, 5 + u * 6, Fade(Color{230, 230, 230, 255}, 0.18f * (1 - u)));
    }
    DrawRectangleGradientV(0, 0, SCREEN_W, 120, Fade(Color{60, 50, 50, 255}, 0.12f), Fade(Color{60, 50, 50, 255}, 0.0f));   // smoke under the ceiling
    Glow({950, 108}, 60, Color{255, 200, 120, 70});
    DrawTextCenteredBold("THE LAMPLIGHT", 950, 44, 18, Color{236, 196, 120, 255});   // (after the ink and the lightmap: the sign is lit from within)
    for (int k = 0; k < 11; k++) Glow({690 + k * 50.0f, 478}, 16, Color{255, 200, 130, 60});
    for (int k = 0; k < 2; k++) Glow({110 + k * 160.0f, 86}, 30, Color{255, 180, 110, 60});
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
