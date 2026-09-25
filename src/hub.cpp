// ============================================================================
//  DEPTH - the Nautilus: the walkable deck and every station you can visit.
//
//  The deck is a long cross-section of the submarine that scrolls sideways.
//  Depth comes from layers that move at different speeds: the ocean outside
//  the windows (slow), the back wall and stations, the deck floor drawn in
//  perspective, the crew walking on it, and dark foreground silhouettes (fast).
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float DECK_W = 5000;   // length of the deck in world pixels
constexpr float CAM_MIN = -160, CAM_MAX = DECK_W - SCREEN_W + 160; // a little past each end, to see the bulkheads
constexpr float WALL_TOP = 46;   // below the HUD strip
constexpr float FLOOR_Y = 560;   // where the back wall meets the deck
constexpr float VP_X = SCREEN_W / 2.0f, VP_Y = 300; // vanishing point of the deck's perspective
constexpr float HUD_Y = 654;     // the bottom HUD starts here

// How far a point at screen height y moves with the camera, relative to the back wall.
float DepthK(float y) { return (y - VP_Y) / (FLOOR_Y - VP_Y); }
float DeckX(float wx, float cam, float y) { return VP_X + (wx - cam - VP_X) * DepthK(y); }

struct Station { float x, w, h; Scene target; const char* name; const char* hint; };
const Station STATIONS[] = {
    {360, 240, 330, Scene::Crew, "Crew Quarters", "Choose your party, fit relics, and pick each crew member's abilities."},
    {980, 330, 360, Scene::Bookshelf, "Library", "Learn about the crew, conditions, and creatures of the deep."},
    {1620, 360, 290, Scene::Radar, "Radar Room", "Pick up new recruits and buy relics from passing salvagers."},
    {2330, 440, 360, Scene::Helm, "The Helm", "Chart a course and send your party on an expedition."},
    {3000, 240, 470, Scene::Periscope, "Periscope", "Take on a platforming run for gold."},
    {3560, 380, 300, Scene::Workshop, "Workshop", "Upgrade the Nautilus: reflectors, bunks, sonar and infirmary gear."},
    {4160, 360, 300, Scene::SickLeave, "Sick Bay", "Rattled crew can rest and steady their nerves."},
    {4700, 330, 340, Scene::Ward, "The Ward", "Patch up injured crew."},
};
constexpr int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);
const float PORTHOLES[] = {650, 1292, 1942, 2722, 3251, 3865, 4437};
const float VENTS[] = {820, 1700, 2700, 3700, 4500};
constexpr float LAMP_START = 150, LAMP_GAP = 370;
// Foreground props sit halfway between stations, so they sweep past while walking but
// never cover a station you've stopped at.
const float FOREGROUND[] = {670, 1300, 1975, 2665, 3860, 4430};

// The Nautilus's own hands, who keep the ship running. They wander the deck but can't be hired.
struct Npc { const char* role; HeroClass look; };
const Npc NPCS[] = {{"Engineer", HeroClass::Mechanic}, {"Deckhand", HeroClass::Diver}, {"Orderly", HeroClass::Nurse}};
constexpr int NPC_COUNT = sizeof(NPCS) / sizeof(NPCS[0]);
constexpr int NPC_ID_BASE = 100000;

struct Walker { int id; float x, lane, laneTarget, target, wait, phase; bool right; bool npc = false; };
struct Puff { Vector2 p, v; float life, max, size; };
struct Spark { Vector2 p, v; float life; };
struct Cat { float x = 2300, target = 2300, wait = 3, phase = 0; bool right = true; };

std::vector<Walker> walkers;
std::vector<Puff> puffs;
std::vector<Spark> sparks;
Cat cat;
float ventTimer[5] = {1, 3, 2, 4.5f, 0.5f};
float wheelRot = 0;

float Hash01(int a, int b) {
    unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}
float RandF(float lo, float hi) { return lo + (hi - lo) * GetRandomValue(0, 10000) / 10000.0f; }
Rectangle StationRect(const Station& s, float cam) { return {s.x - cam - s.w / 2, FLOOR_Y - s.h, s.w, s.h}; }
}  // namespace

// ============================================================ the ocean outside
static void DrawOceanLayer(float cam, float t) {
    BeginLayer(OceanRT());
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{44, 118, 134, 255}, Color{6, 28, 46, 255});
    float o1 = cam * 0.12f, o2 = cam * 0.25f, o3 = cam * 0.4f;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 8; k++) { // sunlight from far above
        float x = fmodf(k * 250 - o1 + 8000, 2000) - 300 + sinf(t * 0.3f + k) * 30;
        float w = 40 + (k % 3) * 25;
        DrawTri({x, 0}, {x + w, 0}, {x - 150, 720}, Color{90, 170, 180, 12});
        DrawTri({x + w, 0}, {x - 150 + w * 1.8f, 720}, {x - 150, 720}, Color{90, 170, 180, 12});
    }
    EndBlendMode();
    // a whale drifting past every so often
    float ph = fmodf(t, 90) / 90;
    float wx = -600 + ph * 2600 - o1 * 0.4f, wy = 190 + sinf(ph * 9) * 16;
    Color whale{18, 56, 72, 255};
    DrawEllipse((int)wx, (int)wy, 190, 48, whale);
    DrawEllipse((int)(wx + 150), (int)(wy + 6), 70, 36, whale);
    DrawTri({wx - 170, wy}, {wx - 280, wy - 50 + sinf(t) * 10}, {wx - 270, wy + 40 + sinf(t) * 10}, whale);
    DrawTri({wx + 30, wy + 30}, {wx - 30, wy + 90}, {wx - 50, wy + 30}, whale);
    // the seabed and kelp far below
    for (int x = 0; x < SCREEN_W; x += 6) {
        float fx = x + o1 * 1.5f;
        float h = 560 + sinf(fx * 0.004f) * 40 + sinf(fx * 0.013f + 2) * 20 + powf(fabsf(sinf(fx * 0.007f)), 8) * -90;
        DrawRectangle(x, (int)h, 6, SCREEN_H - (int)h, Color{12, 40, 54, 255});
    }
    for (int k = 0; k < 12; k++) {
        float bx = fmodf(k * 290 - o2 + 9000, 3480) - 300, by = 640;
        Vector2 prev{bx, by};
        for (int s = 1; s <= 12; s++) {
            Vector2 p{bx + sinf(t * 0.8f + k + s * 0.4f) * s * 2.5f, by - s * 22.0f};
            DrawLineEx(prev, p, 7 - s * 0.45f, Color{16, 60, 56, 255});
            prev = p;
        }
    }
    // schools of fish
    for (int k = 0; k < 4; k++) {
        float dir = k % 2 ? 1.0f : -1.0f;
        float cx = fmodf(k * 900 + dir * t * (22 + k * 7) - o2 + 90000, 2800) - 700;
        float cy = 200 + k * 95 + sinf(t * 0.4f + k) * 30;
        Color fc = k < 2 ? Color{130, 175, 186, 255} : Color{84, 130, 146, 255};
        for (int f = 0; f < 18; f++) {
            float fx = cx + (Hash01(k, f) - 0.5f) * 180 + sinf(t * 1.5f + f) * 6;
            float fy = cy + (Hash01(k, f + 50) - 0.5f) * 70 + cosf(t * 1.2f + f) * 5;
            float sz = 1.0f - k * 0.15f;
            DrawEllipse((int)fx, (int)fy, 8 * sz, 3 * sz, fc);
            DrawTri({fx - dir * 7 * sz, fy}, {fx - dir * 13 * sz, fy - 4 * sz}, {fx - dir * 13 * sz, fy + 4 * sz}, fc);
        }
    }
    // marine snow and rising bubbles
    for (int k = 0; k < 70; k++) {
        float x = fmodf(k * 71.0f - o3 + t * 3 + 90000, (float)SCREEN_W);
        float y = fmodf(k * 37.0f + t * (6 + k % 5), (float)SCREEN_H);
        DrawCircle((int)x, (int)y, 1.2f + (k % 3) * 0.5f, Color{210, 235, 240, 110});
    }
    for (int k = 0; k < 24; k++) {
        float x = fmodf(k * 131.0f - o3 + 90000, (float)SCREEN_W) + sinf(t * 2 + k) * 4;
        float y = SCREEN_H - fmodf(t * (30 + k % 4 * 10) + k * 90, (float)SCREEN_H + 40);
        DrawCircleLines((int)x, (int)y, 2 + k % 3, Color{220, 245, 255, 150});
    }
    EndLayer();
}

static void DrawWindow(Vector2 c, float r) {
    DrawCircleV({c.x + 4, c.y + 6}, r + 20, Fade(BLACK, 0.4f));
    DrawCircleV(c, r + 20, Pal::BrassDk);
    DrawRing(c, r + 4, r + 17, 0, 360, 64, Pal::Brass);
    DrawRing(c, r + 11, r + 16, 200, 290, 24, Fade(WHITE, 0.25f));
    for (int k = 0; k < 12; k++) {
        float a = k * PI / 6;
        Vector2 b{c.x + cosf(a) * (r + 11), c.y + sinf(a) * (r + 11)};
        DrawCircleV(b, 3.2f, Pal::BrassDk);
        DrawCircleV({b.x - 0.8f, b.y - 0.8f}, 1.6f, Color{255, 226, 160, 255});
    }
    DrawTexturedCircle(OceanRT().texture, c, r + 4, true);
    DrawRing(c, r - 10, r + 4, 0, 360, 64, Fade(BLACK, 0.3f)); // the hull is thick
    DrawCircleSector(c, r * 0.9f, 200, 245, 16, Fade(WHITE, 0.07f));
    DrawRing(c, r * 0.72f, r * 0.8f, 215, 245, 12, Fade(WHITE, 0.12f));
}

// ============================================================ the back wall, ceiling and floor
static void DrawBackWall(float cam, float t) {
    DrawVGradient({0, 0, (float)SCREEN_W, WALL_TOP + 50}, Color{14, 18, 20, 255}, Color{36, 44, 46, 255});
    DrawTiled(Tex::Metal, {0, WALL_TOP, (float)SCREEN_W, FLOOR_Y - WALL_TOP}, 1.0f, Color{116, 132, 128, 255}, {cam, 0});
    // wood wainscot and a brass rail
    DrawTiled(Tex::Wood, {0, 470, (float)SCREEN_W, 90}, 0.7f, Color{150, 108, 86, 255}, {cam / 0.7f, 0});
    DrawRectangle(0, 464, SCREEN_W, 7, Pal::BrassDk);
    DrawRectangle(0, 465, SCREEN_W, 2, Color{250, 210, 140, 255});
    DrawVGradient({0, FLOOR_Y - 24, (float)SCREEN_W, 24}, Fade(BLACK, 0), Fade(BLACK, 0.45f));
    // hull ribs with rivets and knee brackets
    float first = floorf(cam / 280) * 280 + 140;
    for (float wx = first - 280; wx < cam + SCREEN_W + 280; wx += 280) {
        float sx = wx - cam;
        DrawRectangleGradientH((int)sx - 16, (int)WALL_TOP, 16, (int)(FLOOR_Y - WALL_TOP), Color{44, 54, 54, 255}, Color{108, 124, 120, 255});
        DrawRectangleGradientH((int)sx, (int)WALL_TOP, 16, (int)(FLOOR_Y - WALL_TOP), Color{108, 124, 120, 255}, Color{36, 44, 44, 255});
        for (float y = WALL_TOP + 70; y < FLOOR_Y - 10; y += 38) {
            DrawCircleV({sx - 8, y}, 2.6f, Color{50, 58, 58, 255});
            DrawCircleV({sx - 8.6f, y - 0.6f}, 1.3f, Color{170, 180, 176, 255});
        }
        DrawTri({sx - 16, WALL_TOP + 40}, {sx - 60, WALL_TOP + 40}, {sx - 16, WALL_TOP + 84}, Color{70, 82, 80, 255});
        DrawTri({sx + 16, WALL_TOP + 40}, {sx + 60, WALL_TOP + 40}, {sx + 16, WALL_TOP + 84}, Color{58, 68, 66, 255});
    }
    // pipes along the ceiling
    DrawPipeH(0, SCREEN_W, 62, 11, Pal::Copper);
    DrawPipeH(0, SCREEN_W, 84, 6, Color{130, 134, 128, 255});
    DrawPipeH(0, SCREEN_W, 99, 4, Color{168, 128, 62, 255});
    float ff = floorf(cam / 310) * 310;
    for (float wx = ff; wx < cam + SCREEN_W + 310; wx += 310) {
        DrawFlange({wx - cam, 62}, 11, false, Pal::BrassDk);
        DrawFlange({wx - cam + 150, 84}, 6, false, Color{90, 94, 90, 255});
    }
    for (float vx : VENTS) { // red valve wheels on the steam line
        Vector2 v{vx - cam, 62};
        if (v.x < -40 || v.x > SCREEN_W + 40) continue;
        DrawLineEx({v.x, v.y}, {v.x, v.y + 22}, 4, Color{80, 80, 80, 255});
        DrawRing({v.x, v.y + 24}, 7, 10, 0, 360, 20, Color{170, 40, 36, 255});
        for (int k = 0; k < 3; k++) {
            float a = k * PI / 3 + 0.3f;
            DrawLineEx({v.x - cosf(a) * 8, v.y + 24 - sinf(a) * 8}, {v.x + cosf(a) * 8, v.y + 24 + sinf(a) * 8}, 2, Color{170, 40, 36, 255});
        }
    }
    // hanging cage lamps
    for (float wx = LAMP_START; wx < DECK_W; wx += LAMP_GAP) {
        float sx = wx - cam;
        if (sx < -60 || sx > SCREEN_W + 60) continue;
        DrawLineEx({sx, 70}, {sx, 108}, 2, Color{30, 30, 30, 255});
        DrawTri({sx - 12, 118}, {sx + 12, 118}, {sx, 104}, Pal::BrassDk);
        DrawCircleV({sx, 126}, 9, Color{255, 222, 160, 255});
        for (int k = -1; k <= 1; k++) DrawLineEx({sx + k * 7.0f, 118}, {sx + k * 9.0f, 136}, 1.5f, Pal::BrassDk);
        DrawLineEx({sx - 10, 136}, {sx + 10, 136}, 2, Pal::BrassDk);
    }
    // the bow and stern bulkheads
    for (int e = 0; e < 2; e++) {
        float sx = e == 0 ? -cam : DECK_W - cam;
        if (sx < -200 || sx > SCREEN_W + 200) continue;
        float x0 = e == 0 ? sx - 400 : sx;
        DrawRectangleGradientH((int)x0, 0, 400, (int)FLOOR_Y, e == 0 ? Color{10, 12, 14, 255} : Color{58, 66, 66, 255},
                               e == 0 ? Color{58, 66, 66, 255} : Color{10, 12, 14, 255});
        float hx = e == 0 ? sx - 90 : sx + 90;
        DrawRectangleRounded({hx - 55, FLOOR_Y - 250, 110, 240}, 0.5f, 10, Color{70, 80, 78, 255});
        DrawRectangleRounded({hx - 45, FLOOR_Y - 240, 90, 222}, 0.5f, 10, Color{96, 106, 102, 255});
        DrawRing({hx, FLOOR_Y - 130}, 16, 22, 0, 360, 24, Color{170, 40, 36, 255});
    }
}

static void DrawDeckFloor(float cam) {
    const int ROWS = 9;
    Texture2D wood = GetTex(Tex::Wood);
    for (int j = 0; j < ROWS; j++) {
        float y0 = FLOOR_Y + (SCREEN_H - FLOOR_Y) * (powf(1.28f, (float)j) - 1) / (powf(1.28f, (float)ROWS) - 1);
        float y1 = FLOOR_Y + (SCREEN_H - FLOOR_Y) * (powf(1.28f, (float)j + 1) - 1) / (powf(1.28f, (float)ROWS) - 1);
        float k = DepthK((y0 + y1) / 2);
        float wx0 = cam + VP_X - VP_X / k; // world x that lands on the screen's left edge at this depth
        Rectangle src{wx0 + j * 131.0f, (j % 4) * 64.0f + 3, SCREEN_W / k, 58};
        unsigned char v = (unsigned char)(150 + j * 8);
        DrawTexturePro(wood, src, {0, y0, (float)SCREEN_W, y1 - y0 + 1}, {0, 0}, 0, Color{v, (unsigned char)(v * 0.9f), (unsigned char)(v * 0.85f), 255});
        DrawLineEx({0, y0}, {(float)SCREEN_W, y0}, 1 + j * 0.25f, Color{40, 24, 14, 200});
    }
    // a runner carpet down the deck
    float c0 = FLOOR_Y + 34, c1 = FLOOR_Y + 70;
    DrawRectangle(0, (int)c0, SCREEN_W, (int)(c1 - c0), Color{120, 34, 34, 235});
    DrawRectangle(0, (int)c0 + 3, SCREEN_W, 2, Color{210, 170, 90, 200});
    DrawRectangle(0, (int)c1 - 5, SCREEN_W, 2, Color{210, 170, 90, 200});
    float first = floorf(cam / 120) * 120;
    for (float wx = first - 240; wx < cam + SCREEN_W * 1.5f; wx += 120) { // diamond pattern in perspective
        float x = DeckX(wx, cam, (c0 + c1) / 2), k = DepthK((c0 + c1) / 2);
        DrawTri({x - 20 * k, (c0 + c1) / 2}, {x, c0 + 8}, {x + 20 * k, (c0 + c1) / 2}, Color{160, 60, 44, 255});
        DrawTri({x - 20 * k, (c0 + c1) / 2}, {x + 20 * k, (c0 + c1) / 2}, {x, c1 - 8}, Color{160, 60, 44, 255});
    }
    DrawVGradient({0, FLOOR_Y, (float)SCREEN_W, 40}, Fade(BLACK, 0.55f), Fade(BLACK, 0));
}

// ============================================================ stations
static void DrawCrewQuarters(float sx) {
    float fy = FLOOR_Y;
    Rectangle frame{sx - 112, fy - 322, 224, 322};
    DrawRectangleRounded({frame.x + 6, frame.y + 8, frame.width, frame.height}, 0.3f, 12, Fade(BLACK, 0.35f));
    DrawRectangleRounded(frame, 0.3f, 12, Color{88, 98, 96, 255});
    Rectangle in{sx - 92, fy - 302, 184, 302};
    DrawRectangleRounded(in, 0.3f, 12, Color{46, 34, 26, 255});
    DrawTiled(Tex::Wood, {in.x + 16, in.y + 44, in.width - 32, in.height - 44}, 0.6f, Color{130, 92, 66, 255});
    for (int k = 0; k < 2; k++) DrawRectangle((int)(in.x + 12 + k * (in.width - 30)), (int)in.y + 60, 6, (int)in.height - 60, Color{110, 116, 116, 255});
    for (int b = 0; b < 2; b++) {
        float by = fy - 64 - b * 112;
        DrawRectangle((int)in.x + 12, (int)by, (int)in.width - 24, 9, Color{100, 106, 108, 255});
        DrawRectangleRounded({in.x + 16, by - 18, in.width - 32, 20}, 0.4f, 6, Color{216, 208, 192, 255});
        DrawRectangleRounded({in.x + 62, by - 23, in.width - 80, 22}, 0.5f, 6, b ? Color{150, 58, 48, 255} : Color{58, 88, 122, 255});
        DrawEllipse((int)in.x + 40, (int)by - 20, 18, 8, Color{238, 234, 224, 255});
    }
    DrawRectangle((int)sx + 40, (int)fy - 290, 26, 34, Color{230, 220, 190, 255}); // a pinned-up photograph
    DrawRectangle((int)sx + 43, (int)fy - 287, 20, 22, Color{120, 110, 96, 255});
    // the hatch door, swung open toward us
    Vector2 d0{frame.x + frame.width - 4, frame.y + 24}, d1{d0.x + 62, d0.y + 28}, d2{d1.x, fy - 18}, d3{d0.x, fy - 4};
    DrawTri(d0, d1, d2, Color{104, 114, 110, 255});
    DrawTri(d0, d2, d3, Color{104, 114, 110, 255});
    DrawLineEx(d1, d2, 4, Color{60, 68, 66, 255});
    Vector2 wc{d0.x + 32, (d0.y + d3.y) / 2 + 10};
    DrawEllipse((int)wc.x, (int)wc.y, 11, 17, Color{170, 40, 36, 255});
    DrawEllipse((int)wc.x, (int)wc.y, 7, 12, Color{104, 114, 110, 255});
    for (float y = frame.y + 30; y < fy - 10; y += 34) {
        DrawCircleV({frame.x + 10, y}, 2.5f, Color{60, 66, 64, 255});
        DrawCircleV({frame.x + frame.width - 10, y}, 2.5f, Color{60, 66, 64, 255});
    }
    DrawBrassPlate({sx - 92, fy - 364, 184, 30}, "CREW QUARTERS", 15);
}

static void DrawLibrary(float sx) {
    float fy = FLOOR_Y;
    const Color books[7] = {{150, 50, 44, 255}, {50, 84, 130, 255}, {56, 110, 70, 255}, {176, 132, 56, 255},
                            {100, 60, 110, 255}, {200, 190, 170, 255}, {90, 60, 40, 255}};
    for (int c = 0; c < 2; c++) {
        Rectangle bc{sx - 160 + c * 165.0f, fy - 350, 145, 350};
        DrawRectangle((int)bc.x + 6, (int)bc.y + 8, (int)bc.width, (int)bc.height, Fade(BLACK, 0.35f));
        DrawTiled(Tex::Wood, bc, 0.5f, Color{128, 82, 54, 255});
        DrawRectangleRec({bc.x + 8, bc.y + 14, bc.width - 16, bc.height - 22}, Color{34, 22, 16, 255});
        for (int s = 0; s < 5; s++) {
            float shelfY = bc.y + 76 + s * 64.0f;
            float x = bc.x + 11;
            for (int b = 0; x < bc.x + bc.width - 20; b++) {
                int id = c * 1000 + s * 50 + b;
                int w = 7 + (int)(Hash01(id, 1) * 8), h = 36 + (int)(Hash01(id, 2) * 18);
                Color col = books[(int)(Hash01(id, 3) * 6.99f)];
                if (Hash01(id, 4) < 0.07f && x < bc.x + bc.width - 40) {
                    DrawRectanglePro({x + h * 0.2f + w, shelfY, (float)w, (float)h}, {(float)w, (float)h}, -14, col);
                    x += w + 11;
                    continue;
                }
                DrawRectangle((int)x, (int)(shelfY - h), w, h, col);
                DrawRectangle((int)x, (int)(shelfY - h), 2, h, Fade(WHITE, 0.14f));
                DrawRectangle((int)x, (int)(shelfY - h + 6), w, 2, Fade(Pal::Brass, 0.7f));
                DrawRectangle((int)x, (int)(shelfY - 10), w, 2, Fade(Pal::Brass, 0.5f));
                x += w + 1;
            }
            DrawRectangle((int)bc.x + 6, (int)shelfY, (int)bc.width - 12, 7, Color{104, 66, 42, 255});
            DrawRectangle((int)bc.x + 6, (int)shelfY + 7, (int)bc.width - 12, 3, Fade(BLACK, 0.4f));
        }
        DrawRectangle((int)bc.x - 6, (int)bc.y - 10, (int)bc.width + 12, 14, Color{96, 60, 38, 255});
        DrawRectangle((int)bc.x - 6, (int)bc.y - 10, (int)bc.width + 12, 3, Color{150, 104, 70, 255});
    }
    // a rolling ladder leaning on the right-hand case
    Vector2 t1{sx + 108, fy - 340}, b1{sx + 150, fy}, t2{sx + 134, fy - 340}, b2{sx + 176, fy};
    for (int k = 1; k < 10; k++) {
        float f = k / 10.0f;
        DrawLineEx({t1.x + (b1.x - t1.x) * f, t1.y + (b1.y - t1.y) * f}, {t2.x + (b2.x - t2.x) * f, t2.y + (b2.y - t2.y) * f}, 4, Color{110, 72, 44, 255});
    }
    DrawLineEx(t1, b1, 5, Color{136, 90, 56, 255});
    DrawLineEx(t2, b2, 5, Color{136, 90, 56, 255});
    // reading table with a banker's lamp, and a globe
    DrawRectangle((int)sx - 20, (int)fy - 72, 10, 72, Color{80, 50, 32, 255});
    DrawEllipse((int)sx - 15, (int)fy - 2, 34, 6, Color{60, 38, 24, 255});
    DrawEllipse((int)sx - 15, (int)fy - 74, 58, 10, Color{120, 78, 48, 255});
    DrawRectangle((int)sx - 60, (int)fy - 88, 30, 12, Color{150, 50, 44, 255});
    DrawRectangle((int)sx - 58, (int)fy - 97, 26, 9, Color{50, 84, 130, 255});
    DrawRectangle((int)sx + 6, (int)fy - 108, 4, 32, Pal::Brass);
    DrawEllipse((int)sx + 8, (int)fy - 78, 14, 4, Pal::BrassDk);
    DrawRectangleRounded({sx - 14, fy - 122, 44, 16}, 0.6f, 6, Color{30, 120, 70, 255});
    DrawRectangleRounded({sx - 12, fy - 121, 40, 5}, 0.6f, 6, Fade(WHITE, 0.25f));
    Vector2 gc{sx - 130, fy - 128};
    DrawLineEx({gc.x, gc.y + 26}, {gc.x, fy - 6}, 5, Pal::BrassDk);
    DrawEllipse((int)gc.x, (int)fy - 4, 24, 5, Pal::BrassDk);
    DrawCircleV(gc, 27, Color{60, 110, 150, 255});
    DrawEllipse((int)gc.x - 6, (int)gc.y - 6, 12, 9, Color{150, 140, 90, 255});
    DrawEllipse((int)gc.x + 9, (int)gc.y + 10, 9, 6, Color{150, 140, 90, 255});
    DrawCircleSector({gc.x - 8, gc.y - 8}, 12, 0, 360, 16, Fade(WHITE, 0.14f));
    DrawRing(gc, 29, 32, 100, 440, 32, Pal::Brass);
    DrawBrassPlate({sx - 62, fy - 396, 124, 30}, "LIBRARY", 16);
}

static void DrawRadarRoom(float sx, float t) {
    float fy = FLOOR_Y;
    Color steel{70, 90, 86, 255};
    // a speaking tube from the ceiling
    DrawPipeV(sx - 200, 104, fy - 250, 6, Pal::Brass);
    DrawEllipse((int)sx - 200, (int)fy - 246, 14, 8, Pal::BrassDk);
    DrawEllipse((int)sx - 200, (int)fy - 246, 9, 5, Color{20, 16, 12, 255});
    Rectangle body{sx - 170, fy - 140, 340, 140}, panel{sx - 170, fy - 280, 340, 140};
    DrawRectangle((int)body.x + 8, (int)panel.y + 8, (int)body.width, (int)(fy - panel.y), Fade(BLACK, 0.35f));
    DrawVGradient(panel, ColorBrightness(steel, -0.2f), steel);
    DrawVGradient(body, ColorBrightness(steel, 0.08f), ColorBrightness(steel, -0.35f));
    DrawRectangleLinesEx(panel, 3, ColorBrightness(steel, -0.5f));
    DrawRectangleLinesEx(body, 3, ColorBrightness(steel, -0.5f));
    DrawRectangle((int)body.x - 6, (int)body.y - 6, (int)body.width + 12, 10, ColorBrightness(steel, -0.3f));
    // the main scope with its sweeping beam
    Vector2 c{sx - 72, fy - 210};
    DrawCircleV(c, 60, Pal::BrassDk);
    DrawRing(c, 52, 60, 0, 360, 48, Pal::Brass);
    DrawCircleV(c, 52, Color{6, 30, 18, 255});
    for (int k = 1; k <= 3; k++) DrawRing(c, 17.0f * k - 0.7f, 17.0f * k + 0.7f, 0, 360, 48, Color{40, 150, 80, 140});
    DrawLineEx({c.x - 52, c.y}, {c.x + 52, c.y}, 1, Color{40, 150, 80, 110});
    DrawLineEx({c.x, c.y - 52}, {c.x, c.y + 52}, 1, Color{40, 150, 80, 110});
    float a = t * 1.6f, ad = fmodf(a * RAD2DEG, 360);
    for (int k = 0; k < 12; k++)
        DrawCircleSector(c, 51, ad - (k + 1) * 5, ad - k * 5, 3, Color{80, 255, 140, (unsigned char)(80 * (1 - k / 12.0f))});
    DrawLineEx(c, {c.x + cosf(a) * 51, c.y + sinf(a) * 51}, 2, Color{150, 255, 180, 255});
    const Vector2 blips[4] = {{22, -18}, {-30, 12}, {10, 34}, {-12, -38}};
    for (auto b : blips) {
        float since = fmodf(a - atan2f(b.y, b.x) + 20 * PI, 2 * PI);
        DrawCircleV({c.x + b.x, c.y + b.y}, 3.5f, Color{170, 255, 190, (unsigned char)(255 * std::max(0.0f, 1 - since / 3))});
    }
    DrawCircleSector(c, 48, 200, 250, 12, Fade(WHITE, 0.08f));
    // an oscilloscope
    Rectangle osc{sx + 12, fy - 262, 140, 76};
    DrawRectangleRounded({osc.x - 6, osc.y - 6, osc.width + 12, osc.height + 12}, 0.2f, 6, Pal::BrassDk);
    DrawRectangleRounded(osc, 0.15f, 6, Color{6, 30, 18, 255});
    Vector2 prev{osc.x + 4, osc.y + osc.height / 2};
    for (float x = 4; x < osc.width - 4; x += 3) {
        Vector2 p{osc.x + x, osc.y + osc.height / 2 + sinf(x * 0.11f + t * 6) * 18 * (0.6f + 0.4f * sinf(t * 0.7f)) * sinf(x * 0.02f + 0.3f)};
        DrawLineEx(prev, p, 2, Color{120, 255, 160, 255});
        prev = p;
    }
    // switches and blinking lamps
    for (int k = 0; k < 8; k++) {
        float x = sx + 20 + k * 17;
        DrawRectangle((int)x - 4, (int)fy - 170, 8, 12, Color{40, 44, 44, 255});
        float lean = Hash01(k, 9) > 0.5f ? 4.0f : -4.0f;
        DrawLineEx({x, fy - 164}, {x + lean, fy - 178}, 3, Color{200, 200, 196, 255});
    }
    const Color lamp[3] = {{255, 80, 60, 255}, {255, 190, 60, 255}, {90, 255, 120, 255}};
    for (int k = 0; k < 9; k++) {
        bool on = fmodf(t * (0.7f + Hash01(k, 3)) + k * 0.37f, 1.0f) < 0.55f;
        Vector2 p{sx - 150 + k * 18.0f, fy - 110};
        DrawCircleV(p, 5, Color{30, 30, 30, 255});
        DrawCircleV(p, 3.8f, on ? lamp[k % 3] : ColorBrightness(lamp[k % 3], -0.7f));
    }
    for (int k = 0; k < 3; k++) {
        Vector2 kc{sx + 40 + k * 44.0f, fy - 104};
        DrawCircleV(kc, 12, Color{36, 36, 38, 255});
        DrawCircleV({kc.x - 2, kc.y - 2}, 8, Color{70, 70, 74, 255});
        float ka = t * 0.3f + k;
        DrawLineEx(kc, {kc.x + cosf(ka) * 9, kc.y + sinf(ka) * 9}, 2, Pal::Paper);
    }
    DrawRectangle((int)sx - 150, (int)fy - 70, 300, 3, ColorBrightness(steel, -0.5f));
    // headphones hanging on a hook
    Vector2 hk{sx + 210, fy - 310};
    DrawLineEx({hk.x, hk.y - 6}, {hk.x, hk.y + 4}, 3, Color{40, 40, 40, 255});
    DrawRing({hk.x, hk.y + 22}, 17, 21, 180, 360, 20, Color{50, 40, 34, 255});
    DrawEllipse((int)hk.x - 19, (int)hk.y + 26, 7, 11, Color{80, 56, 40, 255});
    DrawEllipse((int)hk.x + 19, (int)hk.y + 26, 7, 11, Color{80, 56, 40, 255});
    DrawLineEx({hk.x + 19, hk.y + 36}, {sx + 150, fy - 140}, 2, Color{30, 30, 30, 255});
    // a stool
    DrawEllipse((int)sx + 60, (int)fy - 56, 30, 8, Color{120, 40, 36, 255});
    DrawLineEx({sx + 60, fy - 52}, {sx + 60, fy - 6}, 5, Color{60, 62, 64, 255});
    DrawEllipse((int)sx + 60, (int)fy - 3, 22, 4, Color{40, 42, 44, 255});
    DrawBrassPlate({sx - 78, fy - 322, 156, 30}, "RADAR ROOM", 15);
}

static void DrawHelmStation(float sx, float t) {
    float fy = FLOOR_Y;
    DrawWindow({sx, 244}, 136);
    DrawGauge({sx - 212, 180}, 30, 0.55f + 0.04f * sinf(t * 1.3f), Color{236, 228, 206, 255});
    DrawGauge({sx - 212, 268}, 24, 0.3f + 0.03f * sinf(t * 2.7f), Color{236, 228, 206, 255});
    DrawGauge({sx + 212, 180}, 30, 0.7f + 0.03f * sinf(t * 0.9f + 1), Color{236, 228, 206, 255});
    DrawGauge({sx + 212, 268}, 24, 0.45f + 0.05f * sinf(t * 1.9f), Color{236, 228, 206, 255});
    const char* labels[4] = {"DEPTH", "BALLAST", "PRESSURE", "KNOTS"};
    for (int k = 0; k < 4; k++) {
        float lx = k < 2 ? sx - 212 : sx + 212, ly = k % 2 ? 298 : 216;
        float w = (float)MeasureTxt(labels[k], 11, true);
        TxtBold(labels[k], lx - w / 2, ly, 11, Color{220, 200, 150, 220});
    }
    // engine order telegraph
    Vector2 tc{sx - 185, fy - 214};
    DrawPipeV(tc.x, tc.y + 30, fy - 6, 11, Pal::Brass);
    DrawEllipse((int)tc.x, (int)fy - 4, 30, 7, Pal::BrassDk);
    DrawCircleV({tc.x + 3, tc.y + 5}, 44, Fade(BLACK, 0.35f));
    DrawCircleV(tc, 44, Pal::BrassDk);
    DrawCircleV(tc, 38, Color{236, 226, 200, 255});
    for (int k = 0; k < 7; k++) {
        float a = (200 + k * 23.3f) * DEG2RAD;
        DrawLineEx(tc, {tc.x + cosf(a) * 38, tc.y + sinf(a) * 38}, 1, Color{120, 100, 70, 255});
    }
    DrawCircleSector(tc, 36, 200, 270, 12, Fade(Color{60, 140, 70, 255}, 0.35f));
    DrawCircleSector(tc, 36, 270, 340, 12, Fade(Color{170, 50, 40, 255}, 0.35f));
    float la = (262 + sinf(t * 0.2f) * 6) * DEG2RAD;
    DrawLineEx(tc, {tc.x + cosf(la) * 34, tc.y + sinf(la) * 34}, 4, Color{40, 30, 24, 255});
    DrawCircleV(tc, 6, Pal::Brass);
    // chart table
    DrawRectangle((int)sx + 138, (int)fy - 100, 8, 100, Color{90, 58, 36, 255});
    DrawRectangle((int)sx + 262, (int)fy - 100, 8, 100, Color{90, 58, 36, 255});
    DrawTiled(Tex::Wood, {sx + 126, fy - 112, 156, 16}, 0.5f, Color{140, 94, 60, 255});
    Rectangle map{sx + 138, fy - 128, 128, 18};
    DrawRectangleRec(map, Color{230, 214, 176, 255});
    for (int k = 0; k < 6; k++) DrawLineEx({map.x + 10 + k * 20.0f, map.y + 4 + (k % 2) * 8}, {map.x + 22 + k * 20.0f, map.y + 12 - (k % 2) * 6}, 1.5f, Color{120, 80, 50, 255});
    DrawLineEx({sx + 200, fy - 150}, {sx + 190, fy - 120}, 2, Color{160, 160, 170, 255});
    DrawLineEx({sx + 200, fy - 150}, {sx + 214, fy - 120}, 2, Color{160, 160, 170, 255});
    // the ship's wheel on its pedestal
    Vector2 hc{sx, fy - 160};
    DrawRectangleGradientH((int)sx - 20, (int)hc.y, 20, (int)(fy - hc.y), Color{60, 38, 24, 255}, Color{130, 86, 54, 255});
    DrawRectangleGradientH((int)sx, (int)hc.y, 20, (int)(fy - hc.y), Color{130, 86, 54, 255}, Color{50, 32, 20, 255});
    for (float y = hc.y + 30; y < fy; y += 50) DrawPipeH(sx - 22, sx + 22, y, 4, Pal::Brass);
    DrawEllipse((int)sx, (int)fy - 3, 46, 9, Color{50, 34, 22, 255});
    DrawBrassPlate({sx - 58, fy - 34, 116, 22}, "THE HELM", 12);
    Color wood{128, 82, 46, 255}, woodDk{84, 52, 30, 255};
    for (int k = 0; k < 8; k++) {
        float a = wheelRot + k * PI / 4;
        Vector2 in{hc.x + cosf(a) * 16, hc.y + sinf(a) * 16}, out{hc.x + cosf(a) * 116, hc.y + sinf(a) * 116};
        DrawLineEx(in, out, 9, woodDk);
        DrawLineEx(in, out, 5, wood);
        DrawCircleV(out, 9, woodDk);
        DrawCircleV({out.x - 2, out.y - 2}, 6, ColorBrightness(wood, 0.2f));
    }
    DrawRing(hc, 78, 94, 0, 360, 64, woodDk);
    DrawRing(hc, 80, 92, 0, 360, 64, wood);
    DrawRing(hc, 84, 86, 190, 280, 24, Fade(WHITE, 0.2f));
    DrawRing(hc, 74, 78, 0, 360, 64, Pal::Brass);
    DrawCircleV(hc, 20, Pal::BrassDk);
    DrawCircleV(hc, 15, Pal::Brass);
    DrawCircleV({hc.x - 4, hc.y - 4}, 5, Color{255, 236, 180, 255});
}

static void DrawPeriscopeStation(float sx, float t) {
    float fy = FLOOR_Y;
    DrawEllipse((int)sx, (int)fy - 2, 100, 16, Color{40, 44, 44, 255});
    DrawEllipse((int)sx, (int)fy - 6, 92, 13, Color{120, 110, 80, 255});
    DrawPipeV(sx, 40, fy - 290, 22, Color{150, 154, 148, 255});
    for (float y = 120; y < fy - 300; y += 90) DrawPipeH(sx - 28, sx + 28, y, 7, Pal::Brass);
    DrawPipeV(sx, fy - 220, fy - 8, 16, Color{130, 134, 128, 255});
    Rectangle hs{sx - 52, fy - 300, 104, 84};
    DrawRectangleRounded({hs.x + 5, hs.y + 6, hs.width, hs.height}, 0.25f, 8, Fade(BLACK, 0.4f));
    DrawRectangleRounded(hs, 0.25f, 8, Pal::BrassDk);
    DrawRectangleRounded({hs.x + 3, hs.y + 3, hs.width - 6, hs.height - 6}, 0.25f, 8, Pal::Brass);
    DrawRectangleRounded({hs.x + 6, hs.y + 5, hs.width - 12, 20}, 0.4f, 8, Fade(WHITE, 0.2f));
    DrawCircleV({sx, fy - 258}, 22, Color{30, 28, 26, 255});
    DrawCircleV({sx, fy - 258}, 13, Color{20, 40, 50, 255});
    DrawCircleV({sx - 4, fy - 262}, 4, Color{150, 220, 230, 220});
    for (int k = -1; k <= 1; k += 2) {
        DrawPipeH(k < 0 ? sx - 118 : sx + 52, k < 0 ? sx - 52 : sx + 118, fy - 244, 6, Color{80, 84, 82, 255});
        DrawRectangleRounded({k < 0 ? sx - 132 : sx + 104, fy - 256, 28, 24}, 0.5f, 6, Color{30, 26, 24, 255});
    }
    DrawBrassPlate({sx + 30, fy - 380, 128, 26}, "PERISCOPE", 13);
    (void)t;
}

static void DrawWorkshop(float sx, float t) {
    float fy = FLOOR_Y;
    Rectangle pb{sx - 180, fy - 310, 250, 150};
    DrawRectangle((int)pb.x + 5, (int)pb.y + 6, (int)pb.width, (int)pb.height, Fade(BLACK, 0.35f));
    DrawRectangleRec(pb, Color{140, 104, 68, 255});
    DrawRectangleLinesEx(pb, 4, Color{90, 62, 38, 255});
    for (float y = pb.y + 12; y < pb.y + pb.height - 6; y += 14)
        for (float x = pb.x + 12; x < pb.x + pb.width - 6; x += 14) DrawCircleV({x, y}, 1.6f, Color{70, 48, 30, 255});
    Color tool{150, 152, 160, 255};
    DrawLineEx({pb.x + 30, pb.y + 30}, {pb.x + 30, pb.y + 110}, 6, Color{120, 80, 50, 255}); // hammer
    DrawRectangle((int)pb.x + 16, (int)pb.y + 22, 30, 14, tool);
    DrawLineEx({pb.x + 70, pb.y + 26}, {pb.x + 70, pb.y + 110}, 5, tool); // wrench
    DrawRing({pb.x + 70, pb.y + 26}, 6, 11, 200, 520, 16, tool);
    DrawRectangle((int)pb.x + 100, (int)pb.y + 30, 70, 30, tool); // saw
    for (int k = 0; k < 10; k++) DrawTri({pb.x + 100 + k * 7.0f, pb.y + 60}, {pb.x + 107 + k * 7.0f, pb.y + 60}, {pb.x + 103 + k * 7.0f, pb.y + 66}, tool);
    DrawRectangleRounded({pb.x + 168, pb.y + 26, 24, 38}, 0.4f, 4, Color{120, 80, 50, 255});
    DrawLineEx({pb.x + 205, pb.y + 30}, {pb.x + 205, pb.y + 100}, 4, tool); // screwdriver
    DrawRectangleRounded({pb.x + 199, pb.y + 26, 12, 30}, 0.5f, 4, Color{170, 40, 36, 255});
    DrawRing({pb.x + 130, pb.y + 110}, 14, 20, 0, 360, 24, Color{170, 150, 100, 255}); // coil of rope
    // meshing gears on the wall
    float r1 = 50, r2 = 32, r3 = 26, sp = t * 0.6f;
    DrawGear({sx + 118, fy - 300}, r1, 12, sp, Pal::Brass);
    DrawGear({sx + 118 + r1 + r2 - 4, fy - 300 + 28}, r2, 8, -sp * r1 / r2 + 0.2f, Pal::Copper);
    DrawGear({sx + 78, fy - 300 + r1 + r3 - 2}, r3, 7, -sp * r1 / r3 + 0.4f, Color{164, 166, 158, 255});
    // workbench, vise and grinder
    for (float lx : {sx - 170.0f, sx + 160.0f}) DrawRectangle((int)lx, (int)fy - 110, 12, 110, Color{84, 56, 34, 255});
    DrawRectangle((int)sx - 170, (int)fy - 40, 342, 8, Color{84, 56, 34, 255});
    DrawTiled(Tex::Wood, {sx - 186, fy - 128, 372, 20}, 0.5f, Color{150, 104, 66, 255});
    DrawRectangle((int)sx - 186, (int)fy - 108, 372, 6, Color{70, 46, 28, 255});
    DrawRectangle((int)sx - 176, (int)fy - 156, 40, 28, Color{90, 96, 100, 255});
    DrawRectangle((int)sx - 180, (int)fy - 150, 48, 6, Color{70, 74, 78, 255});
    DrawLineEx({sx - 196, fy - 147}, {sx - 160, fy - 147}, 3, tool);
    DrawRectangleRounded({sx + 90, fy - 170, 52, 42}, 0.3f, 6, Color{60, 90, 80, 255});
    Vector2 gw{sx + 150, fy - 150};
    DrawCircleV(gw, 20, Color{120, 116, 110, 255});
    for (int k = 0; k < 4; k++) {
        float a = t * 20 + k * PI / 2;
        DrawLineEx(gw, {gw.x + cosf(a) * 18, gw.y + sinf(a) * 18}, 2, Color{80, 78, 74, 255});
    }
    DrawCircleV(gw, 5, Color{60, 60, 60, 255});
    // toolbox and an oil can on the floor
    DrawRectangleRounded({sx - 90, fy - 44, 90, 40}, 0.15f, 4, Color{170, 40, 36, 255});
    DrawRectangle((int)sx - 90, (int)fy - 32, 90, 3, Color{120, 26, 24, 255});
    DrawRing({sx - 45, fy - 46}, 8, 11, 180, 360, 12, Color{60, 60, 60, 255});
    DrawRectangleRounded({sx + 30, fy - 34, 24, 30}, 0.3f, 4, Color{60, 100, 70, 255});
    DrawLineEx({sx + 50, fy - 30}, {sx + 70, fy - 46}, 3, Color{60, 100, 70, 255});
    DrawBrassPlate({sx - 78, fy - 350, 156, 28}, "WORKSHOP", 15);
}

static void DrawSickBay(float sx, float t, const std::vector<const Hero*>& resting) {
    float fy = FLOOR_Y;
    // a curtain half drawn on the right
    DrawRectangle((int)sx - 170, (int)fy - 300, 350, 5, Pal::BrassDk);
    for (int i = 0; i < 8; i++) {
        float x = sx + 95 + i * 10.0f;
        DrawRectangleGradientH((int)x, (int)fy - 296, 10, 280, Color{186, 206, 184, 255}, Color{130, 156, 134, 255});
        DrawCircleV({x + 5, fy - 298}, 4, Pal::Brass);
    }
    // two cots, occupied by whoever is on leave
    for (int c = 0; c < 2; c++) {
        float cx = sx - 50 + c * 140;
        DrawLineEx({cx - 58, fy - 60}, {cx - 58, fy}, 4, Color{120, 124, 126, 255});
        DrawLineEx({cx + 58, fy - 60}, {cx + 58, fy}, 4, Color{120, 124, 126, 255});
        DrawRectangle((int)cx - 62, (int)fy - 64, 124, 6, Color{110, 114, 116, 255});
        DrawRectangleRounded({cx - 60, fy - 80, 120, 18}, 0.4f, 6, Color{232, 228, 216, 255});
        DrawEllipse((int)cx - 42, (int)fy - 82, 17, 8, Color{244, 242, 236, 255});
        if (c < (int)resting.size()) {
            DrawCircleV({cx - 42, fy - 92}, 11, Color{226, 188, 156, 255});
            DrawCircleSector({cx - 42, fy - 92}, 11.5f, 150, 330, 12, Color{80, 56, 40, 255});
            DrawRectangleRounded({cx - 32, fy - 98, 92, 24}, 0.6f, 6, Color{70, 100, 140, 255});
            for (int z = 0; z < 3; z++) {
                float ph = fmodf(t * 0.5f + z * 0.33f + c * 0.5f, 1.0f);
                TxtBold("z", cx - 40 + ph * 30 + z * 4, fy - 112 - ph * 60, 12 + z * 3, Fade(Pal::Paper, 1 - ph));
            }
        } else {
            DrawRectangleRounded({cx - 30, fy - 84, 88, 10}, 0.5f, 6, Color{70, 100, 140, 255});
        }
    }
    // a gramophone playing on a side table
    float gx = sx - 150;
    DrawLineEx({gx, fy - 76}, {gx, fy}, 4, Color{84, 56, 34, 255});
    DrawEllipse((int)gx, (int)fy - 78, 28, 6, Color{110, 72, 44, 255});
    DrawRectangle((int)gx - 22, (int)fy - 104, 44, 24, Color{104, 66, 40, 255});
    DrawEllipse((int)gx, (int)fy - 106, 22, 5, Color{20, 20, 22, 255});
    DrawEllipse((int)(gx + sinf(t * 8) * 4), (int)fy - 106, 6, 2, Fade(WHITE, 0.3f));
    Vector2 neck{gx + 8, fy - 110}, mouth{gx + 34, fy - 176};
    DrawTri(neck, {mouth.x - 26, mouth.y - 8}, {mouth.x + 18, mouth.y + 14}, Pal::Brass);
    DrawEllipse((int)mouth.x - 4, (int)mouth.y + 3, 26, 20, Pal::Brass);
    DrawEllipse((int)mouth.x - 2, (int)mouth.y + 5, 18, 13, Pal::BrassDk);
    for (int n = 0; n < 3; n++) {
        float ph = fmodf(t * 0.35f + n / 3.0f, 1.0f);
        Vector2 p{mouth.x + 14 + ph * 50 + sinf(t * 2 + n) * 8, mouth.y - 10 - ph * 110};
        Color nc = Fade(Pal::Paper, 1 - ph);
        DrawCircleV(p, 4.5f, nc);
        DrawLineEx({p.x + 4, p.y}, {p.x + 4, p.y - 16}, 1.5f, nc);
        DrawLineEx({p.x + 4, p.y - 16}, {p.x + 10, p.y - 12}, 1.5f, nc);
    }
    // a fern in a pot
    DrawRectangleRounded({sx + 150, fy - 40, 30, 38}, 0.2f, 4, Color{160, 90, 60, 255});
    for (int k = 0; k < 7; k++) {
        float a = -PI / 2 + (k - 3) * 0.32f + sinf(t * 0.8f + k) * 0.04f;
        DrawLineEx({sx + 165, fy - 40}, {sx + 165 + cosf(a) * 46, fy - 40 + sinf(a) * 46}, 3, Color{60, 120, 60, 255});
    }
    DrawBrassPlate({sx - 70, fy - 344, 140, 28}, "SICK BAY", 15);
}

static void DrawWard(float sx, float t) {
    float fy = FLOOR_Y;
    // medicine cabinet
    Rectangle cab{sx - 160, fy - 330, 100, 176};
    DrawRectangle((int)cab.x + 5, (int)cab.y + 6, (int)cab.width, (int)cab.height, Fade(BLACK, 0.35f));
    DrawRectangleRec(cab, Color{220, 224, 218, 255});
    DrawRectangleLinesEx(cab, 3, Color{150, 156, 150, 255});
    const Color glass[4] = {{120, 70, 30, 255}, {50, 90, 150, 255}, {60, 130, 80, 255}, {160, 50, 50, 255}};
    for (int s = 0; s < 3; s++) {
        float y = cab.y + 54 + s * 44;
        for (int b = 0; b < 5; b++) {
            float h = 18 + Hash01(s, b) * 12;
            DrawRectangle((int)(cab.x + 10 + b * 17), (int)(y - h), 12, (int)h, glass[(s + b) % 4]);
            DrawRectangle((int)(cab.x + 12 + b * 17), (int)(y - h - 5), 8, 5, Color{240, 236, 226, 255});
            DrawRectangle((int)(cab.x + 11 + b * 17), (int)(y - h + 2), 3, (int)h - 4, Fade(WHITE, 0.3f));
        }
        DrawRectangle((int)cab.x + 4, (int)y, (int)cab.width - 8, 3, Color{160, 166, 160, 255});
    }
    DrawRectangleRec({cab.x + cab.width / 2 - 1, cab.y + 6, 2, cab.height - 12}, Color{160, 166, 160, 255});
    DrawRectangleRec({cab.x + 6, cab.y + 6, cab.width - 12, cab.height - 12}, Fade(Color{180, 220, 230, 255}, 0.12f));
    DrawRectangle((int)(cab.x + cab.width / 2 - 4), (int)cab.y - 26, 8, 22, Color{190, 40, 40, 255});
    DrawRectangle((int)(cab.x + cab.width / 2 - 11), (int)cab.y - 19, 22, 8, Color{190, 40, 40, 255});
    // surgical lamp
    Vector2 lc{sx + 20, fy - 280};
    DrawLineEx({lc.x, 104}, {lc.x, lc.y - 30}, 4, Color{150, 154, 150, 255});
    DrawCircleSector(lc, 38, 180, 360, 24, Color{190, 196, 192, 255});
    DrawCircleSector({lc.x - 6, lc.y - 4}, 26, 200, 260, 12, Fade(WHITE, 0.3f));
    DrawEllipse((int)lc.x, (int)lc.y, 38, 7, Color{255, 250, 232, 255});
    // operating table
    DrawRectangle((int)sx - 20, (int)fy - 96, 16, 90, Color{170, 176, 174, 255});
    DrawEllipse((int)sx - 12, (int)fy - 4, 44, 7, Color{110, 116, 114, 255});
    DrawRectangleRounded({sx - 100, fy - 112, 230, 16}, 0.4f, 6, Color{238, 238, 232, 255});
    DrawRectangle((int)sx - 100, (int)fy - 98, 230, 24, Color{214, 218, 214, 255});
    for (int k = 0; k < 6; k++) DrawLineEx({sx - 94 + k * 40.0f, fy - 98}, {sx - 94 + k * 40.0f, fy - 76}, 1, Color{190, 194, 190, 255});
    DrawEllipse((int)sx - 82, (int)fy - 118, 20, 7, Color{250, 250, 246, 255});
    // IV stand
    float ix = sx + 158;
    DrawLineEx({ix, fy - 270}, {ix, fy - 6}, 3, Color{170, 176, 174, 255});
    DrawLineEx({ix - 20, fy - 4}, {ix + 20, fy - 4}, 4, Color{110, 116, 114, 255});
    DrawLineEx({ix - 14, fy - 270}, {ix + 14, fy - 270}, 3, Color{170, 176, 174, 255});
    DrawRectangleRounded({ix - 12, fy - 262, 24, 36}, 0.4f, 6, Color{200, 226, 236, 190});
    DrawRectangle((int)ix - 10, (int)fy - 244, 20, 16, Color{200, 90, 90, 150});
    DrawLineEx({ix, fy - 226}, {sx + 110, fy - 112}, 1.5f, Color{220, 230, 236, 200});
    DrawBrassPlate({sx - 66, fy - 382, 132, 28}, "THE WARD", 15);
    (void)t;
}

static void DrawStation(int i, float sx, float t, const std::vector<const Hero*>& resting) {
    switch (i) {
        case 0: DrawCrewQuarters(sx); break;
        case 1: DrawLibrary(sx); break;
        case 2: DrawRadarRoom(sx, t); break;
        case 3: DrawHelmStation(sx, t); break;
        case 4: DrawPeriscopeStation(sx, t); break;
        case 5: DrawWorkshop(sx, t); break;
        case 6: DrawSickBay(sx, t, resting); break;
        default: DrawWard(sx, t); break;
    }
}

// ============================================================ life aboard: crew, the cat, steam and sparks
static void PickWalkTarget(Walker& w) {
    w.target = GetRandomValue(0, 99) < 60 ? STATIONS[GetRandomValue(0, STATION_COUNT - 1)].x + RandF(-130, 130)
                                          : w.x + RandF(-350, 350);
    w.target = std::clamp(w.target, 120.0f, DECK_W - 120);
    w.laneTarget = RandF(590, 648);
}

static const Hero& NpcHero(int i) {
    static Hero heroes[NPC_COUNT];
    Hero& h = heroes[i];
    h.id = NPC_ID_BASE + i * 3 + 1; // only used to vary their looks
    h.cls = NPCS[i].look;
    return h;
}

static void UpdateLife(Game& g, float dt, float t) {
    walkers.erase(std::remove_if(walkers.begin(), walkers.end(), [&](const Walker& w) { return !w.npc && !FindHero(g, w.id); }), walkers.end());
    if (std::none_of(walkers.begin(), walkers.end(), [](const Walker& w) { return w.npc; }))
        for (int i = 0; i < NPC_COUNT; i++) {
            Walker w{i, STATIONS[(i * 3 + 1) % STATION_COUNT].x + RandF(-100, 100), 0, 0, 0, RandF(0, 4), 0, i % 2 == 0, true};
            w.lane = w.laneTarget = RandF(590, 648);
            w.target = w.x;
            walkers.push_back(w);
        }
    for (auto& h : g.roster) {
        bool known = std::any_of(walkers.begin(), walkers.end(), [&](const Walker& w) { return !w.npc && w.id == h.id; });
        if (known) continue;
        Walker w{h.id, STATIONS[GetRandomValue(0, STATION_COUNT - 1)].x + RandF(-150, 150), 0, 0, 0, RandF(0, 3), 0, GetRandomValue(0, 1) == 1};
        w.lane = w.laneTarget = RandF(590, 648);
        w.target = w.x;
        walkers.push_back(w);
    }
    for (auto& w : walkers) {
        w.lane += (w.laneTarget - w.lane) * std::min(1.0f, dt * 0.8f);
        if (w.wait > 0) {
            w.wait -= dt;
            w.phase = 0;
            if (w.wait <= 0) PickWalkTarget(w);
            continue;
        }
        float dx = w.target - w.x, step = 72 * dt;
        if (fabsf(dx) <= step) { w.x = w.target; w.wait = RandF(2, 7); w.phase = 0; }
        else { w.x += dx > 0 ? step : -step; w.right = dx > 0; w.phase += dt * 7.5f; }
    }
    // the ship's cat
    if (cat.wait > 0) { cat.wait -= dt; cat.phase = 0; if (cat.wait <= 0) cat.target = std::clamp(cat.x + RandF(-500, 500), 150.0f, DECK_W - 150); }
    else {
        float dx = cat.target - cat.x, step = 55 * dt;
        if (fabsf(dx) <= step) { cat.x = cat.target; cat.wait = RandF(3, 10); }
        else { cat.x += dx > 0 ? step : -step; cat.right = dx > 0; cat.phase += dt * 10; }
    }
    // steam venting from the valves
    for (int i = 0; i < 5; i++) {
        ventTimer[i] -= dt;
        if (ventTimer[i] > 0) continue;
        ventTimer[i] = RandF(3, 7);
        for (int k = 0; k < 16; k++) {
            float life = RandF(1.0f, 2.0f);
            puffs.push_back({{VENTS[i] + RandF(-4, 4), 70}, {RandF(-30, 30), RandF(10, 50)}, life, life, RandF(5, 9)});
        }
    }
    for (auto& p : puffs) {
        p.p.x += p.v.x * dt;
        p.p.y += p.v.y * dt;
        p.v.x *= 1 - dt * 1.2f;
        p.v.y = p.v.y * (1 - dt * 1.2f) - 6 * dt;
        p.size += 16 * dt;
        p.life -= dt;
    }
    puffs.erase(std::remove_if(puffs.begin(), puffs.end(), [](const Puff& p) { return p.life <= 0; }), puffs.end());
    // sparks fly from the grinder in bursts
    if (fmodf(t, 6) < 2.2f && GetRandomValue(0, 99) < 55)
        sparks.push_back({{STATIONS[5].x + 150 - 16, FLOOR_Y - 158}, {RandF(-200, -40), RandF(-190, -20)}, RandF(0.3f, 0.7f)});
    for (auto& s : sparks) {
        s.p.x += s.v.x * dt;
        s.p.y += s.v.y * dt;
        s.v.y += 700 * dt;
        s.life -= dt;
    }
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0; }), sparks.end());
}

static void DrawCat(float cam, float t) {
    float lane = 604, s = DepthK(lane) * 0.9f;
    float x = DeckX(cat.x, cam, lane), y = lane;
    if (x < -60 || x > SCREEN_W + 60) return;
    float f = cat.right ? 1.0f : -1.0f;
    Color fur{54, 50, 50, 255};
    DrawShadowBlob({x, y}, 22 * s);
    bool sitting = cat.wait > 0;
    for (int k = 0; k < 4; k++) { // legs
        float lx = x + (k < 2 ? -9 : 9) * s + (k % 2) * 3 * s, sw = sitting ? 0 : sinf(cat.phase + k * 1.6f) * 4 * s;
        DrawLineEx({lx, y - 12 * s}, {lx + sw, y}, 3 * s, fur);
    }
    Vector2 prev{x - f * 16 * s, y - 16 * s}; // tail
    for (int k = 1; k <= 6; k++) {
        Vector2 p{x - f * (16 + k * 3) * s, y - 16 * s - k * 5 * s + sinf(t * 2 + k * 0.6f) * 3 * s};
        DrawLineEx(prev, p, 3 * s, fur);
        prev = p;
    }
    if (sitting) DrawEllipse((int)(x - f * 4 * s), (int)(y - 14 * s), 14 * s, 11 * s, fur);
    else DrawEllipse((int)x, (int)(y - 16 * s), 18 * s, 8 * s, fur);
    Vector2 hd{x + f * 16 * s, y - (sitting ? 30 : 24) * s};
    DrawCircleV(hd, 8 * s, fur);
    DrawTri({hd.x - 6 * s, hd.y - 4 * s}, {hd.x - 2 * s, hd.y - 13 * s}, {hd.x + 1 * s, hd.y - 5 * s}, fur);
    DrawTri({hd.x + 1 * s, hd.y - 5 * s}, {hd.x + 5 * s, hd.y - 13 * s}, {hd.x + 7 * s, hd.y - 3 * s}, fur);
    DrawCircleV({hd.x + f * 3.5f * s, hd.y - 1 * s}, 1.6f * s, Color{230, 220, 90, 255});
    DrawEllipse((int)(hd.x + f * 1 * s), (int)(hd.y + 4 * s), 4 * s, 3 * s, Color{220, 216, 210, 255});
}

// ============================================================ lighting and foreground
static void DrawDeckLighting(float cam, float t, int hovered) {
    LightsBegin(Color{46, 54, 62, 255});
    Color warm{255, 206, 140, 255}, sea{70, 150, 170, 255};
    int li = 0;
    for (float wx = LAMP_START; wx < DECK_W; wx += LAMP_GAP, li++) {
        float sx = wx - cam;
        if (sx < -500 || sx > SCREEN_W + 500) continue;
        bool broken = li == 6 && fmodf(t, 7) < 1.3f && sinf(t * 40) > 0.2f; // one lamp on the fritz
        float k = broken ? 0.25f : 0.9f + 0.05f * sinf(t * 11 + li);
        AddLight({sx, 128}, 360, warm, 0.85f * k);
        AddCone({sx, 130}, PI / 2, 0.55f, 470, Color{(unsigned char)(150 * k), (unsigned char)(118 * k), (unsigned char)(78 * k), 255});
        AddLight({DeckX(wx, cam, 600), 610}, 260, warm, 0.35f * k);
    }
    for (float px : PORTHOLES) AddLight({px - cam, 250}, 230, sea, 0.8f);
    float s = STATIONS[3].x - cam;
    AddLight({s, 244}, 420, sea, 0.95f);
    s = STATIONS[0].x - cam; AddLight({s, FLOOR_Y - 180}, 170, warm, 0.8f);
    s = STATIONS[1].x - cam; AddLight({s + 8, FLOOR_Y - 104}, 190, Color{255, 226, 160, 255}, 0.9f);
    s = STATIONS[2].x - cam; AddLight({s - 72, FLOOR_Y - 210}, 240, Color{80, 255, 140, 255}, 0.55f);
    AddLight({s + 82, FLOOR_Y - 224}, 140, Color{80, 255, 140, 255}, 0.4f);
    s = STATIONS[4].x - cam; AddCone({s, 60}, PI / 2, 0.28f, 560, Color{255, 236, 196, 255});
    s = STATIONS[5].x - cam;
    if (fmodf(t, 6) < 2.2f) AddLight({s + 134, FLOOR_Y - 158}, 150, Color{255, 170, 80, 255}, 0.7f + 0.3f * sinf(t * 50));
    s = STATIONS[6].x - cam; AddLight({s, FLOOR_Y - 160}, 260, Color{255, 190, 150, 255}, 0.45f);
    s = STATIONS[7].x - cam;
    AddCone({s + 20, FLOOR_Y - 276}, PI / 2, 0.5f, 300, Color{255, 250, 232, 255});
    AddLight({s + 20, FLOOR_Y - 270}, 120, Color{255, 250, 232, 255}, 0.9f);
    if (hovered >= 0) {
        const Station& h = STATIONS[hovered];
        AddLight({h.x - cam, FLOOR_Y - h.h / 2}, h.w * 0.9f + 80, Color{255, 230, 190, 255}, 0.35f);
    }
    LightsEnd();

    // bright bits that glow on top of the lighting
    for (float wx = LAMP_START; wx < DECK_W; wx += LAMP_GAP) {
        float sx = wx - cam;
        if (sx > -60 && sx < SCREEN_W + 60) Glow({sx, 126}, 44, Color{255, 200, 120, 150});
    }
    s = STATIONS[2].x - cam; Glow({s - 72, FLOOR_Y - 210}, 70, Color{60, 255, 120, 60});
    s = STATIONS[1].x - cam; Glow({s + 8, FLOOR_Y - 106}, 30, Color{255, 220, 150, 120});
    s = STATIONS[7].x - cam; Glow({s + 20, FLOOR_Y - 276}, 50, Color{255, 250, 232, 140});
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& sp : sparks) {
        Vector2 p{sp.p.x - cam, sp.p.y};
        DrawLineEx(p, {p.x - sp.v.x * 0.02f, p.y - sp.v.y * 0.02f}, 2, Color{255, 190, 90, (unsigned char)(255 * std::min(1.0f, sp.life * 3))});
    }
    for (int k = 0; k < 70; k++) { // dust drifting in the lamplight
        float wx = LAMP_START + (k % 14) * LAMP_GAP + sinf(t * 0.2f + k) * 70;
        float sx = wx - cam;
        if (sx < -20 || sx > SCREEN_W + 20) continue;
        float y = 150 + fmodf(k * 53 + t * (5 + k % 4), 380);
        DrawCircleV({sx + cosf(t * 0.5f + k) * 20, y}, 1.2f, Color{255, 230, 180, 60});
    }
    EndBlendMode();
}

static void DrawForeground(float cam, float t) {
    const float K = 1.75f;
    Color c{9, 12, 15, 255}, rim{80, 70, 56, 255};
    for (int i = 0; i < 6; i++) {
        float sx = VP_X + (FOREGROUND[i] - cam - VP_X) * K;
        if (sx < -260 || sx > SCREEN_W + 260) continue;
        switch (i % 3) {
            case 0: // a thick pipe running floor to ceiling
                DrawRectangle((int)sx - 30, 0, 60, SCREEN_H, c);
                DrawRectangle((int)sx - 30, 0, 3, SCREEN_H, rim);
                for (float y : {120.0f, 430.0f}) {
                    DrawRectangle((int)sx - 40, (int)y, 80, 28, c);
                    DrawRectangle((int)sx - 40, (int)y, 80, 2, rim);
                }
                break;
            case 1: { // a chain and hook swaying from the ceiling
                float sway = sinf(t * 0.6f + i) * 8;
                for (int k = 0; k < 22; k++) {
                    float y = k * 17.0f, x = sx + sway * k / 22.0f;
                    if (k % 2) DrawRectangleRounded({x - 3, y, 6, 20}, 0.8f, 4, c);
                    else DrawRectangleRoundedLinesEx({x - 7, y, 14, 20}, 0.8f, 4, 4, c);
                }
                Vector2 hk{sx + sway, 390};
                DrawRing({hk.x, hk.y + 18}, 14, 22, 0, 200, 16, c);
            } break;
            default: // barrels and a coil of rope, low in the foreground
                for (int b = 0; b < 2; b++) {
                    float bx = sx - 60 + b * 96, top = 596 + b * 14;
                    DrawRectangleRounded({bx - 44, top, 88, SCREEN_H - top + 20}, 0.25f, 8, c);
                    DrawEllipse((int)bx, (int)top + 4, 44, 9, Color{16, 20, 24, 255});
                    DrawEllipse((int)bx, (int)top + 4, 44, 9, Fade(rim, 0.25f));
                    for (float hy = top + 22; hy < SCREEN_H; hy += 36) DrawRectangle((int)bx - 44, (int)hy, 88, 4, Color{20, 24, 28, 255});
                    DrawRectangle((int)bx - 44, (int)top + 6, 2, SCREEN_H, rim);
                }
                DrawRing({sx + 100, 660}, 18, 34, 0, 360, 24, c);
                DrawRing({sx + 100, 660}, 32, 34, 180, 270, 12, rim);
                break;
        }
    }
}

// ============================================================ HUD
static void DrawDeckHud(Game& g, float cam, int hovered) {
    DrawVGradient({0, 0, (float)SCREEN_W, WALL_TOP}, Color{8, 12, 14, 245}, Color{16, 22, 26, 225});
    DrawRectangle(0, (int)WALL_TOP - 2, SCREEN_W, 2, Pal::BrassDk);
    TxtShadow("THE NAUTILUS", 20, 9, 28, Pal::Brass, true);
    float depth = 212 + sinf(g.time * 0.05f) * 3;
    Txt(TextFormat("Depth %.0f fathoms   Heading 047", depth), 262, 16, 15, Color{180, 190, 186, 255});
    DrawCircle(890, 23, 10, Pal::Brass);
    DrawCircle(887, 20, 4, Color{255, 240, 190, 255});
    TxtBold(TextFormat("%d gold", g.gold), 908, 12, 21, Pal::Brass);
    Txt(TextFormat("Batteries %d", g.batteries), 1030, 15, 17, Pal::Paper);
    Txt(TextFormat("Relics %d", (int)g.relicStorage.size()), 1160, 15, 17, Pal::Paper);

    // bottom strip: the deck plan (click to travel) and the party
    DrawVGradient({0, HUD_Y, (float)SCREEN_W, SCREEN_H - HUD_Y}, Color{12, 18, 22, 235}, Color{6, 10, 12, 250});
    DrawRectangle(0, (int)HUD_Y, SCREEN_W, 2, Pal::BrassDk);
    Rectangle plan{20, HUD_Y + 14, 420, 38};
    DrawRectangleRounded(plan, 1.0f, 16, Color{30, 42, 46, 255});
    DrawRectangleRoundedLinesEx(plan, 1.0f, 16, 2, Pal::BrassDk);
    DrawRectangleRounded({plan.x + plan.width / 2 - 30, plan.y - 8, 60, 12}, 0.6f, 6, Color{30, 42, 46, 255});
    float span = plan.width - 40;
    auto planX = [&](float wx) { return plan.x + 20 + wx / DECK_W * span; };
    DrawRectangleRounded({planX(cam), plan.y + 4, SCREEN_W / DECK_W * span, plan.height - 8}, 0.4f, 6, Fade(Pal::Teal, 0.25f));
    Vector2 m = GetMousePosition();
    int planHover = -1;
    for (int i = 0; i < STATION_COUNT; i++) {
        Vector2 p{planX(STATIONS[i].x), plan.y + plan.height / 2};
        bool hov = CheckCollisionPointCircle(m, p, 9);
        if (hov) planHover = i;
        DrawCircleV(p, i == hovered || hov ? 7.0f : 5.0f, i == hovered || hov ? Pal::Brass : Pal::BrassDk);
    }
    if (planHover >= 0) {
        const char* n = STATIONS[planHover].name;
        float w = (float)MeasureTxt(n, 15, true);
        float px = planX(STATIONS[planHover].x);
        DrawRectangleRounded({px - w / 2 - 8, plan.y - 34, w + 16, 24}, 0.4f, 6, Color{12, 18, 22, 240});
        TxtBold(n, px - w / 2, plan.y - 30, 15, Pal::Brass);
    }
    if (CheckCollisionPointRec(m, plan) && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        g.hubCamTarget = std::clamp((m.x - plan.x - 20) / span * DECK_W - SCREEN_W / 2.0f, CAM_MIN, CAM_MAX);

    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        Rectangle c{462 + k * 202.0f, HUD_Y + 8, 194, 52};
        DrawRectangleRounded(c, 0.2f, 6, Color{24, 32, 36, 255});
        DrawRectangleRoundedLinesEx(c, 0.2f, 6, 1.5f, Color{70, 64, 50, 255});
        if (!h) { Txt(TextFormat("Rank %d: empty", k + 1), c.x + 12, c.y + 17, 15, Color{120, 124, 120, 255}); continue; }
        DrawRectangle((int)c.x + 4, (int)c.y + 8, 4, (int)c.height - 16, ClassColor(h->cls));
        TxtBold(TextFormat("%d. %s", k + 1, h->name.c_str()), c.x + 14, c.y + 5, 15, Pal::Paper);
        Txt(TextFormat("%s  Lv %d", ClassName(h->cls), h->level), c.x + 14, c.y + 23, 12, Color{190, 180, 150, 255});
        Stats s = GetStats(*h);
        DrawBar({c.x + 14, c.y + 40, 170, 5}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({c.x + 14, c.y + 47, 170, 3}, h->stress / 100.0f, Pal::Stress);
        if (h->rattled) TxtBold("RATTLED", c.x + c.width - 64, c.y + 7, 11, Pal::Bad);
    }
}

// ============================================================ the deck scene
void SceneHub(Game& g) {
    float dt = GetFrameTime(), t = g.time;
    SetPost(0.5f, 0.03f, 0.4f);
    Vector2 m = GetMousePosition();
    bool mouseInWorld = m.y > WALL_TOP && m.y < HUD_Y;

    // --- camera: screen edges, A/D or arrows, the mouse wheel, or the deck plan
    float pan = 0;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) pan -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) pan += 1;
    if (mouseInWorld && IsWindowFocused() && IsCursorOnScreen()) {
        if (m.x < 50) pan -= (50 - m.x) / 50;
        else if (m.x > SCREEN_W - 50) pan += (m.x - (SCREEN_W - 50)) / 50;
    }
    g.hubCamTarget = std::clamp(g.hubCamTarget + pan * 1100 * dt - GetMouseWheelMove() * 180, CAM_MIN, CAM_MAX);
    g.hubCam += (g.hubCamTarget - g.hubCam) * std::min(1.0f, dt * 7);
    float cam = g.hubCam;
    wheelRot += dt * 0.12f;

    UpdateLife(g, dt, t);
    std::vector<const Hero*> resting;
    for (auto& h : g.roster) if (h.onLeave > 0) resting.push_back(&h);

    // --- who is under the mouse? crew in front take priority over stations
    struct Drawn { Walker* w; const Hero* h; float sx, s; Rectangle r; };
    std::vector<Drawn> crew;
    for (auto& w : walkers) {
        const Hero* h = w.npc ? &NpcHero(w.id) : FindHero(g, w.id);
        if (!h || h->onLeave > 0) continue;
        float s = DepthK(w.lane) * 0.8f, sx = DeckX(w.x, cam, w.lane);
        if (sx < -80 || sx > SCREEN_W + 80) continue;
        crew.push_back({&w, h, sx, s, {sx - 20 * s, w.lane - 150 * s, 40 * s, 150 * s}});
    }
    std::sort(crew.begin(), crew.end(), [](const Drawn& a, const Drawn& b) { return a.w->lane < b.w->lane; });
    const Drawn* hovCrew = nullptr;
    if (mouseInWorld)
        for (auto& c : crew) if (CheckCollisionPointRec(m, c.r)) hovCrew = &c;
    int hovered = -1;
    if (mouseInWorld && !hovCrew)
        for (int i = 0; i < STATION_COUNT; i++)
            if (CheckCollisionPointRec(m, StationRect(STATIONS[i], cam))) hovered = i;

    // --- draw, back to front
    DrawOceanLayer(cam, t);
    DrawBackWall(cam, t);
    for (float px : PORTHOLES)
        if (px - cam > -120 && px - cam < SCREEN_W + 120) DrawWindow({px - cam, 250}, 62);
    for (int i = 0; i < STATION_COUNT; i++) {
        float sx = STATIONS[i].x - cam;
        if (sx + STATIONS[i].w / 2 + 120 < 0 || sx - STATIONS[i].w / 2 - 120 > SCREEN_W) continue;
        DrawStation(i, sx, t, resting);
    }
    for (auto& p : puffs) {
        float a = p.life / p.max;
        DrawCircleV({p.p.x - cam, p.p.y}, p.size, Color{226, 230, 230, (unsigned char)(120 * a * a)});
    }
    DrawDeckFloor(cam);
    bool catDrawn = false;
    for (auto& c : crew) {
        if (!catDrawn && c.w->lane > 604) { DrawCat(cam, t); catDrawn = true; }
        DrawShadowBlob({c.sx, c.w->lane}, 30 * c.s);
        DrawCrewFigure(*c.h, {c.sx, c.w->lane}, c.s, c.w->right, c.w->phase, t);
    }
    if (!catDrawn) DrawCat(cam, t);
    DrawDeckLighting(cam, t, hovered);

    for (auto& c : crew) { // rattled crew carry a cloud of worry
        if (!c.h->rattled) continue;
        Glow({c.sx, c.w->lane - 160 * c.s}, 26 + sinf(t * 5) * 4, Fade(Pal::Stress, 0.5f));
    }
    if (hovered >= 0) {
        Rectangle r = StationRect(STATIONS[hovered], cam);
        float pulse = 0.55f + 0.45f * sinf(t * 5);
        DrawRectangleRoundedLinesEx({r.x - 10, r.y - 44, r.width + 20, r.height + 50}, 0.06f, 6, 3, Fade(Color{255, 214, 150, 255}, pulse));
    }
    DrawForeground(cam, t);

    // --- labels and hints
    if (hovCrew) {
        const Hero& h = *hovCrew->h;
        std::string label = hovCrew->w->npc ? std::string(NPCS[hovCrew->w->id].role) + " of the Nautilus"
                                            : h.name + "  -  " + ClassName(h.cls) + (h.rattled ? "  (rattled)" : "");
        float w = (float)MeasureTxt(label, 16, true);
        Rectangle r{hovCrew->sx - w / 2 - 10, hovCrew->r.y - 34, w + 20, 26};
        DrawRectangleRounded(r, 0.4f, 6, Color{12, 18, 22, 230});
        DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Pal::BrassDk);
        TxtBold(label, r.x + 10, r.y + 4, 16, Pal::Paper);
    }
    std::string hint = hovered >= 0 ? std::string(STATIONS[hovered].name) + ":  " + STATIONS[hovered].hint
                     : hovCrew ? (hovCrew->w->npc ? std::string("One of the ship's own hands. They keep the Nautilus running.")
                                                  : "Click to open Crew Quarters with " + hovCrew->h->name + " selected.")
                               : "Move the mouse to the screen edges, use A/D, or click the deck plan to walk the deck.";
    float hw = (float)MeasureTxt(hint, 19);
    DrawRectangleRounded({SCREEN_W / 2 - hw / 2 - 16, HUD_Y - 40, hw + 32, 32}, 0.5f, 8, Color{8, 12, 14, 170});
    TxtShadow(hint, SCREEN_W / 2 - hw / 2, HUD_Y - 34, 19, hovered >= 0 || hovCrew ? Pal::Paper : Color{200, 196, 180, 200});
    if (cam > CAM_MIN + 4) DrawTri({14, 330}, {30, 314}, {30, 346}, Fade(Pal::Brass, 0.6f));
    if (cam < CAM_MAX - 4) DrawTri({SCREEN_W - 14.0f, 330}, {SCREEN_W - 30.0f, 314}, {SCREEN_W - 30.0f, 346}, Fade(Pal::Brass, 0.6f));
    DrawDeckHud(g, cam, hovered);

    // --- clicks
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInWorld) {
        if (hovCrew) {
            if (!hovCrew->w->npc) {
                g.selectedHero = hovCrew->h->id;
                g.scene = Scene::Crew;
            }
        } else if (hovered >= 0) {
            g.scene = STATIONS[hovered].target;
            if (g.scene == Scene::Crew && !FindHero(g, g.selectedHero) && !g.roster.empty()) g.selectedHero = g.roster[0].id;
        }
    }
}

// ============================================================ helm
void SceneHelm(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Helm", "Choose a destination for the expedition");
    DrawGoldBadge(g);

    struct Loc { const char* name; const char* desc; bool open; Color col; };
    const Loc locs[4] = {
        {"The Cave", "An undersea cave crawling with oversized crustaceans, sea bugs, and worms. Mini boss: the Lobster.", true, Color{60, 120, 140, 255}},
        {"The Island", "Sun-baked shores ruled by the Sun God and the Coconut Queen.", false, Color{200, 160, 80, 255}},
        {"The Weeds", "A kelp forest of merfolk, octopi, and barracuda. Neptune waits within.", false, Color{70, 140, 80, 255}},
        {"Atlantis", "A sunken city of lost ones who still worship something ancient.", false, Color{110, 90, 150, 255}},
    };
    int partyCount = 0;
    for (int id : g.party) if (id >= 0) partyCount++;

    for (int i = 0; i < 4; i++) {
        Rectangle c{50 + i * 300.0f, 100, 280, 330};
        Panel(c, locs[i].open ? Pal::Paper : Color{176, 168, 150, 255});
        DrawVGradient({c.x + 12, c.y + 12, c.width - 24, 90}, ColorBrightness(locs[i].col, 0.15f), ColorBrightness(locs[i].col, -0.35f));
        DrawTextCenteredBold(locs[i].name, c.x + c.width / 2 + 1, c.y + 40, 30, Fade(BLACK, 0.5f));
        DrawTextCenteredBold(locs[i].name, c.x + c.width / 2, c.y + 38, 30, Pal::Paper);
        DrawWrapped(locs[i].desc, {c.x + 16, c.y + 116, c.width - 32, 120}, 17, Pal::Ink);
        if (locs[i].open) {
            Txt("Depth: Shallows (levels 0-2)", c.x + 16, c.y + 228, 16, Pal::BrassDk);
            Txt("3 rooms + mini boss", c.x + 16, c.y + 250, 16, Pal::BrassDk);
            if (Button({c.x + 20, c.y + 276, c.width - 40, 42}, "Embark", partyCount > 0)) {
                StartDungeon(g);
                return;
            }
        } else {
            DrawTextCentered("Not yet charted", c.x + c.width / 2, c.y + 280, 21, Pal::BrassDk);
        }
    }

    Panel({50, 450, 880, 250});
    TxtBold("Expedition party", 70, 464, 23, Pal::Ink);
    bool anyEmptyLoadout = false;
    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        float x = 70 + k * 215.0f;
        Txt(TextFormat("Rank %d", k + 1), x, 502, 15, Pal::BrassDk);
        if (!h) { Txt("- empty -", x, 526, 19, Pal::BrassDk); continue; }
        if (LoadoutCount(*h) < LOADOUT_SIZE) anyEmptyLoadout = true;
        Stats s = GetStats(*h);
        TxtBold(h->name, x, 524, 21, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h->cls), h->level), x, 550, 16, Pal::BrassDk);
        Txt(TextFormat("HP %d/%d", h->hp, s.maxHp), x, 576, 15, Pal::Ink);
        DrawBar({x, 596, 170, 8}, (float)h->hp / s.maxHp, Pal::Good);
        Txt(TextFormat("Nerves %d/100%s", h->stress, h->rattled ? "  RATTLED" : ""), x, 610, 15, Pal::Ink);
        DrawBar({x, 630, 170, 6}, h->stress / 100.0f, Pal::Stress);
    }
    if (partyCount < 4)
        Txt("Tip: a full party of four is strongly recommended. Visit Crew Quarters.", 70, 660, 17, Pal::Coral);
    else if (anyEmptyLoadout)
        Txt("Someone has an empty ability slot. Fill it in Crew Quarters.", 70, 660, 17, Pal::Coral);
    else
        Txt("Change the marching order and abilities in Crew Quarters. Rank 1 is the front line.", 70, 660, 17, Pal::BrassDk);

    Panel({950, 450, 280, 250});
    TxtBold("Provisions", 970, 464, 23, Pal::Ink);
    DrawWrapped(TextFormat("Each room ahead drains %d light. Batteries recharge it by 40.", LightDrainPerRoom(g)), {970, 500, 240, 80}, 16, Pal::Ink);
    Txt(TextFormat("Batteries: %d", g.batteries), 970, 580, 21, Pal::Ink);
    if (Button({970, 620, 240, 44}, "Buy battery (15g)", g.gold >= 15)) {
        g.gold -= 15;
        g.batteries++;
    }
}

// ============================================================ crew quarters
static void DrawTooltip(const std::string& text, Vector2 at) {
    const float w = 300;
    int lines = 1 + MeasureTxt(text, 15) / (int)(w - 24);
    Rectangle r{std::min(at.x + 16, SCREEN_W - w - 10), at.y + 18, w, 20.0f * lines + 18};
    if (r.y + r.height > SCREEN_H - 6) r.y = at.y - r.height - 8;
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.15f, 6, Fade(BLACK, 0.4f));
    DrawRectangleRounded(r, 0.15f, 6, Color{16, 24, 28, 245});
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, 1.5f, Pal::BrassDk);
    DrawWrapped(text, {r.x + 12, r.y + 9, r.width - 24, r.height}, 15, Pal::Paper);
}

void SceneCrew(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Crew Quarters", "Choose your party, set the marching order, pick abilities, and fit relics");
    DrawGoldBadge(g);
    if (!FindHero(g, g.selectedHero)) g.selectedHero = g.roster.empty() ? -1 : g.roster[0].id;
    Vector2 m = GetMousePosition();
    std::string tooltip;

    // --- roster
    Panel({20, 90, 300, 612});
    TxtBold(TextFormat("Roster  %d/%d", (int)g.roster.size(), MaxRoster(g)), 36, 102, 21, Pal::Ink);
    float rowH = std::min(70.0f, 550.0f / std::max(1, (int)g.roster.size()));
    for (size_t i = 0; i < g.roster.size(); i++) {
        Hero& h = g.roster[i];
        Rectangle r{32, 134 + i * rowH, 276, rowH - 6};
        bool sel = h.id == g.selectedHero, hover = CheckCollisionPointRec(m, r);
        DrawRectangleRounded(r, 0.15f, 6, sel ? Color{252, 242, 214, 255} : hover ? Color{236, 222, 190, 255} : Color{222, 206, 170, 255});
        if (sel) DrawRectangleRoundedLinesEx(r, 0.15f, 6, 2, Pal::BrassDk);
        DrawRectangle((int)r.x, (int)r.y + 6, 6, (int)r.height - 12, ClassColor(h.cls));
        TxtBold(h.name, r.x + 14, r.y + 6, 18, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h.cls), h.level), r.x + 14, r.y + r.height - 24, 15, Pal::BrassDk);
        Stats s = GetStats(h);
        DrawBar({r.x + 150, r.y + r.height - 22, 116, 7}, (float)h.hp / s.maxHp, Pal::Good);
        DrawBar({r.x + 150, r.y + r.height - 11, 116, 5}, h.stress / 100.0f, Pal::Stress);
        const char* tag = InParty(g, h.id) ? "IN PARTY" : h.onLeave ? "ON LEAVE" : h.rattled ? "RATTLED" : "";
        TxtBold(tag, r.x + r.width - 8 - MeasureTxt(tag, 12, true), r.y + 8, 12,
                h.onLeave ? Pal::Stress : h.rattled ? Pal::Bad : Pal::Copper);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g.selectedHero = h.id; g.dismissArmed = -1; }
    }

    // --- party slots
    Panel({340, 90, 600, 145});
    TxtBold("Expedition party   (Rank 1 = front line)", 356, 100, 18, Pal::Ink);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Rectangle s{352 + k * 146.0f, 128, 136, 98};
        DrawRectangleRounded(s, 0.12f, 6, Color{214, 196, 158, 255});
        Txt(TextFormat("Rank %d", k + 1), s.x + 8, s.y + 6, 13, Pal::BrassDk);
        Hero* h = FindHero(g, g.party[k]);
        if (!h) { Txt("- empty -", s.x + 8, s.y + 30, 17, Pal::BrassDk); continue; }
        TxtBold(h->name, s.x + 8, s.y + 22, 17, Pal::Ink);
        Txt(ClassName(h->cls), s.x + 8, s.y + 42, 15, Pal::BrassDk);
        if (Button({s.x + 4, s.y + 64, 40, 28}, "<", k > 0)) std::swap(g.party[k], g.party[k - 1]);
        if (Button({s.x + 48, s.y + 64, 40, 28}, "x")) { g.party[k] = -1; CompactParty(g); }
        if (Button({s.x + 92, s.y + 64, 40, 28}, ">", k < 3 && g.party[k + 1] >= 0)) std::swap(g.party[k], g.party[k + 1]);
    }

    // --- details of the selected hero
    Panel({340, 245, 600, 457});
    Hero* sel = FindHero(g, g.selectedHero);
    if (sel) {
        Hero& h = *sel;
        Stats s = GetStats(h);
        TxtBold(h.name, 356, 254, 26, Pal::Ink);
        Txt(TextFormat("%s  -  Level %d", ClassName(h.cls), h.level), 356 + MeasureTxt(h.name, 26, true) + 16, 262, 18, Pal::BrassDk);
        DrawWrapped(ClassBlurb(h.cls), {356, 288, 570, 40}, 15, Pal::Ink);
        int next = XpForNextLevel(h);
        const std::string lines[8] = {
            TextFormat("HP  %d / %d%s", h.hp, s.maxHp, h.deathsDoor ? "  (Death's Door)" : ""),
            TextFormat("Nerves  %d / 100%s", h.stress, h.rattled ? "  RATTLED" : ""),
            TextFormat("Damage  %d - %d", s.dmgMin, s.dmgMax),
            next < 0 ? std::string("XP  max level") : std::string(TextFormat("XP  %d / %d", h.xp, next)),
            TextFormat("Accuracy  %d", s.acc),
            TextFormat("Dodge  %d", s.dodge),
            TextFormat("Protection  %d%%", s.prot),
            TextFormat("Speed  %d", s.speed),
        };
        for (int i = 0; i < 8; i++) Txt(lines[i], 356 + (i / 4) * 290.0f, 330 + (i % 4) * 21.0f, 16, Pal::Ink);

        // abilities: 8 per class, 4 slotted
        const auto& abs = ClassAbilities(h.cls);
        TxtBold(TextFormat("Abilities  %d/%d slotted", LoadoutCount(h), LOADOUT_SIZE), 356, 418, 16, Pal::Ink);
        Txt("Click to slot or unslot. New ones unlock as the crew member levels up.", 560, 420, 12, Pal::BrassDk);
        for (int i = 0; i < (int)abs.size(); i++) {
            const Ability& a = abs[i];
            Rectangle c{352 + (i % 2) * 292.0f, 442 + (i / 2) * 41.0f, 284, 37};
            int slot = -1;
            for (int k = 0; k < LOADOUT_SIZE; k++) if (h.loadout[k] == i) slot = k;
            bool locked = h.level < a.unlockLevel, hov = CheckCollisionPointRec(m, c);
            Color bg = slot >= 0 ? Color{196, 226, 212, 255} : locked ? Color{186, 178, 164, 255} : Color{226, 212, 178, 255};
            if (hov && !locked) bg = ColorBrightness(bg, 0.08f);
            DrawRectangleRounded(c, 0.25f, 6, bg);
            if (slot >= 0) DrawRectangleRoundedLinesEx(c, 0.25f, 6, 2, Color{40, 130, 120, 255});
            TxtBold(a.name, c.x + 9, c.y + 3, 15, locked ? Color{120, 112, 100, 255} : Pal::Ink);
            std::string where = "ranks " + RankString(a.usableFrom);
            if (a.target == Target::Enemy) where += "  >  " + RankString(a.hits);
            else where += a.target == Target::Self ? "  >  self" : a.target == Target::AllAllies ? "  >  party" : "  >  ally";
            Txt(where, c.x + 9, c.y + 20, 12, Pal::BrassDk);
            const char* tag = locked ? TextFormat("Lv %d", a.unlockLevel) : slot >= 0 ? "SLOTTED" : "";
            TxtBold(tag, c.x + c.width - 10 - MeasureTxt(tag, 11, true), c.y + 12, 11, locked ? Pal::BrassDk : Color{40, 130, 120, 255});
            if (!hov) continue;
            tooltip = a.desc;
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;
            if (locked) Toast(g, TextFormat("%s unlocks at level %d.", a.name.c_str(), a.unlockLevel));
            else if (slot >= 0) h.loadout[slot] = -1;
            else {
                int free = -1;
                for (int k = LOADOUT_SIZE - 1; k >= 0; k--) if (h.loadout[k] < 0) free = k;
                if (free >= 0) h.loadout[free] = i;
                else Toast(g, "All four slots are full. Unslot an ability first.");
            }
        }

        for (int k = 0; k < 2; k++) {
            Rectangle rs{352 + k * 204.0f, 612, 196, 40};
            DrawRectangleRounded(rs, 0.2f, 6, Color{214, 196, 158, 255});
            if (h.relics[k] >= 0) {
                const RelicDef& rd = Relics()[h.relics[k]];
                TxtBold(rd.name, rs.x + 8, rs.y + 3, 15, Pal::Ink);
                Txt(rd.desc, rs.x + 8, rs.y + 22, 12, Pal::BrassDk);
                if (Button({rs.x + rs.width - 34, rs.y + 6, 28, 28}, "x")) {
                    g.relicStorage.push_back(h.relics[k]);
                    h.hp = std::max(1, h.hp - rd.hp);
                    h.relics[k] = -1;
                    h.hp = std::min(h.hp, GetStats(h).maxHp);
                }
            } else {
                Txt("Empty relic slot", rs.x + 10, rs.y + 12, 15, Pal::BrassDk);
            }
        }

        bool inParty = InParty(g, h.id);
        int partyCount = 0;
        for (int id : g.party) if (id >= 0) partyCount++;
        if (inParty) {
            if (Button({352, 660, 190, 34}, "Remove from party")) {
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
            }
        } else if (Button({352, 660, 190, 34}, h.onLeave ? "On sick leave" : "Add to party", h.onLeave == 0 && partyCount < PARTY_SIZE)) {
            CompactParty(g);
            for (auto& id : g.party) if (id < 0) { id = h.id; break; }
        }

        bool armed = g.dismissArmed == h.id;
        if (Button({760, 660, 170, 34}, armed ? "Click to confirm" : "Dismiss", g.roster.size() > 1)) {
            if (!armed) {
                g.dismissArmed = h.id;
            } else {
                for (int r : h.relics) if (r >= 0) g.relicStorage.push_back(r);
                for (auto& id : g.party) if (id == h.id) id = -1;
                std::string name = h.name;
                int id = h.id;
                g.roster.erase(std::remove_if(g.roster.begin(), g.roster.end(), [id](const Hero& x) { return x.id == id; }), g.roster.end());
                CompactParty(g);
                g.selectedHero = -1;
                g.dismissArmed = -1;
                Toast(g, name + " has left the Nautilus. Their relics went to storage.");
                return;
            }
        }
    }

    // --- relic storage
    Panel({960, 90, 300, 612});
    TxtBold("Relic storage", 976, 102, 21, Pal::Ink);
    Txt("Two relics per crew member", 976, 128, 13, Pal::BrassDk);
    int n = (int)g.relicStorage.size();
    if (CheckCollisionPointRec(m, {960, 90, 300, 612})) g.relicScroll -= (int)GetMouseWheelMove();
    g.relicScroll = std::clamp(g.relicScroll, 0, std::max(0, n - 10));
    bool canEquip = sel && (sel->relics[0] < 0 || sel->relics[1] < 0);
    if (n == 0) Txt("Empty. Finish expeditions to find relics.", 976, 160, 14, Pal::BrassDk);
    for (int i = g.relicScroll; i < n && i < g.relicScroll + 10; i++) {
        int rid = g.relicStorage[i];
        const RelicDef& rd = Relics()[rid];
        Rectangle r{972, 150 + (i - g.relicScroll) * 54.0f, 276, 48};
        DrawRectangleRounded(r, 0.2f, 6, Color{222, 206, 170, 255});
        TxtBold(rd.name, r.x + 8, r.y + 5, 16, Pal::Ink);
        Txt(rd.desc, r.x + 8, r.y + 27, 12, Pal::BrassDk);
        if (Button({r.x + r.width - 70, r.y + 9, 62, 30}, "Equip", canEquip)) {
            int slot = sel->relics[0] < 0 ? 0 : 1;
            sel->relics[slot] = rid;
            sel->hp += rd.hp;
            g.relicStorage.erase(g.relicStorage.begin() + i);
            break;
        }
    }
    if (n > 10) Txt("Scroll for more", 976, 690, 13, Pal::BrassDk);
    if (!tooltip.empty()) DrawTooltip(tooltip, m);
}

// ============================================================ radar
void SceneRadar(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Radar Room", "Recruits signal from passing ships; salvagers sell relics");
    DrawGoldBadge(g);

    Panel({30, 90, 620, 612});
    TxtBold("Incoming signals: recruits (100g each)", 50, 104, 21, Pal::Ink);
    int n = (int)g.recruits.size();
    float step = std::min(150.0f, 480.0f / std::max(1, n)), cardH = step - 10;
    for (int i = 0; i < n; i++) {
        Hero& h = g.recruits[i];
        Rectangle c{48, 140 + i * step, 584, cardH};
        DrawRectangleRounded(c, 0.08f, 6, Color{222, 206, 170, 255});
        DrawRectangle((int)c.x, (int)c.y + 10, 8, (int)c.height - 20, ClassColor(h.cls));
        TxtBold(h.name, c.x + 20, c.y + 8, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 30 + MeasureTxt(h.name, 22, true), c.y + 13, 17, Pal::BrassDk);
        if (cardH >= 120) DrawWrapped(ClassBlurb(h.cls), {c.x + 20, c.y + 42, 400, 60}, 15, Pal::Ink);
        Stats s = GetStats(h);
        Txt(TextFormat("HP %d   Dmg %d-%d   Spd %d   Dodge %d   Prot %d", s.maxHp, s.dmgMin, s.dmgMax, s.speed, s.dodge, s.prot),
            c.x + 20, c.y + cardH - 28, 14, Pal::BrassDk);
        bool ok = g.gold >= 100 && (int)g.roster.size() < MaxRoster(g);
        if (Button({c.x + c.width - 140, c.y + cardH / 2 - 22, 124, 44}, "Hire", ok)) {
            g.gold -= 100;
            g.roster.push_back(h);
            Toast(g, h.name + " the " + ClassName(h.cls) + " joins the crew!");
            g.recruits.erase(g.recruits.begin() + i);
            break;
        }
    }
    if ((int)g.roster.size() >= MaxRoster(g))
        Txt(TextFormat("Crew quarters are full (%d). Dismiss someone, or extend the bunks in the Workshop.", MaxRoster(g)), 50, 612, 15, Pal::Coral);
    if (Button({48, 640, 300, 44}, TextFormat("Scan for new signals (%dg)", ScanCost(g)), g.gold >= ScanCost(g))) {
        g.gold -= ScanCost(g);
        g.recruits.clear();
        for (int i = 0; i < RecruitsPerScan(g); i++) g.recruits.push_back(MakeRandomHero(g));
    }

    Panel({670, 90, 580, 612});
    TxtBold("Salvage market: relics", 690, 104, 21, Pal::Ink);
    for (size_t i = 0; i < g.shopRelics.size(); i++) {
        const RelicDef& rd = Relics()[g.shopRelics[i]];
        Rectangle c{688, 140 + i * 100.0f, 544, 88};
        DrawRectangleRounded(c, 0.1f, 6, Color{222, 206, 170, 255});
        TxtBold(rd.name, c.x + 16, c.y + 12, 22, Pal::Ink);
        Txt(rd.desc, c.x + 16, c.y + 46, 17, Pal::BrassDk);
        if (Button({c.x + c.width - 150, c.y + 22, 134, 44}, TextFormat("Buy %dg", rd.price), g.gold >= rd.price)) {
            g.gold -= rd.price;
            g.relicStorage.push_back(g.shopRelics[i]);
            Toast(g, "Bought the " + rd.name + ". It's waiting in Crew Quarters.");
            g.shopRelics.erase(g.shopRelics.begin() + i);
            break;
        }
    }
    Rectangle b{688, 450, 544, 88};
    DrawRectangleRounded(b, 0.1f, 6, Color{222, 206, 170, 255});
    TxtBold("Flashlight battery", b.x + 16, b.y + 12, 22, Pal::Ink);
    Txt(TextFormat("+40 light on an expedition. You have %d.", g.batteries), b.x + 16, b.y + 46, 17, Pal::BrassDk);
    if (Button({b.x + b.width - 150, b.y + 22, 134, 44}, "Buy 15g", g.gold >= 15)) { g.gold -= 15; g.batteries++; }
    DrawWrapped("Stock refreshes after every expedition. Hull upgrades, bigger bunks and a better sonar array are built in the Workshop.",
                {690, 560, 540, 80}, 15, Pal::BrassDk);
}

// ============================================================ workshop
void SceneWorkshop(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Workshop", "Spend gold on lasting upgrades to the Nautilus");
    DrawGoldBadge(g);
    const char* flavor[UP_COUNT] = {
        "Polished mirrors and a stronger bulb. The flashlight lasts longer in the dark.",
        "Hammocks strung between the pipes. Room for more crew aboard.",
        "A bigger ear on the hull. More recruit signals, and cheaper scans.",
        "Better instruments and a steadier surgeon's hand. The Ward charges less.",
    };
    for (int u = 0; u < UP_COUNT; u++) {
        Rectangle c{70 + (u % 2) * 580.0f, 100 + (u / 2) * 290.0f, 560, 270};
        Panel(c);
        int lv = g.upgrades[u];
        TxtBold(UpgradeName(u), c.x + 24, c.y + 20, 26, Pal::Ink);
        for (int k = 0; k < UPGRADE_MAX; k++) {
            Vector2 p{c.x + c.width - 110 + k * 30.0f, c.y + 36};
            DrawCircleV(p, 10, Pal::BrassDk);
            DrawCircleV(p, 7, k < lv ? Pal::Brass : Color{90, 80, 64, 255});
        }
        DrawWrapped(flavor[u], {c.x + 24, c.y + 62, c.width - 48, 50}, 16, Pal::Ink);
        TxtBold("Now:", c.x + 24, c.y + 120, 16, Pal::BrassDk);
        Txt(UpgradeDesc(u, lv), c.x + 80, c.y + 120, 16, Pal::Ink);
        if (lv < UPGRADE_MAX) {
            TxtBold("Next:", c.x + 24, c.y + 146, 16, Pal::BrassDk);
            Txt(UpgradeDesc(u, lv + 1), c.x + 80, c.y + 146, 16, Color{30, 110, 90, 255});
            int price = UpgradePrice(lv + 1);
            if (Button({c.x + 24, c.y + 196, 260, 48}, TextFormat("Build level %d  (%dg)", lv + 1, price), g.gold >= price)) {
                g.gold -= price;
                g.upgrades[u]++;
                Toast(g, TextFormat("%s upgraded to level %d.", UpgradeName(u), g.upgrades[u]));
            }
        } else {
            TxtBold("Fully upgraded.", c.x + 24, c.y + 206, 20, Color{30, 110, 90, 255});
        }
    }
}

// ============================================================ ward
void SceneWard(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    int perHp = WardCostPerHp(g);
    DrawSceneTitle("The Ward", TextFormat("Injured crew can be patched up here (%d gold per HP)", perHp));
    DrawGoldBadge(g);
    int shown = 0, totalCost = 0;
    for (auto& h : g.roster) {
        Stats s = GetStats(h);
        int missing = s.maxHp - h.hp;
        if (missing <= 0) continue;
        int cost = missing * perHp;
        totalCost += cost;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        TxtBold(h.name, c.x + 18, c.y + 14, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureTxt(h.name, 22, true), c.y + 19, 17, Pal::BrassDk);
        Txt(TextFormat("HP %d / %d", h.hp, s.maxHp), c.x + 18, c.y + 50, 19, Pal::Ink);
        DrawBar({c.x + 18, c.y + 76, 240, 10}, (float)h.hp / s.maxHp, Pal::Good);
        if (Button({c.x + c.width - 170, c.y + 30, 150, 44}, TextFormat("Treat %dg", cost), g.gold >= cost)) {
            g.gold -= cost;
            h.hp = s.maxHp;
        }
        shown++;
    }
    if (shown == 0) {
        const char* msg = "Everyone is shipshape. No patients today.";
        TxtShadow(msg, SCREEN_W / 2.0f - MeasureTxt(msg, 28, true) / 2.0f, 320, 28, Pal::Paper, true);
    } else if (shown > 1 && Button({SCREEN_W / 2.0f - 150, 640, 300, 48}, TextFormat("Treat everyone (%dg)", totalCost), g.gold >= totalCost)) {
        g.gold -= totalCost;
        for (auto& h : g.roster) h.hp = GetStats(h).maxHp;
    }
}

// ============================================================ sick leave
void SceneSickLeave(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Sick Bay", "Rest clears all stress and cures Rattled, but they sit out the next expedition");
    DrawGoldBadge(g);
    int shown = 0;
    for (auto& h : g.roster) {
        if (h.stress <= 0 && !h.rattled && h.onLeave == 0) continue;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        TxtBold(h.name, c.x + 18, c.y + 14, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureTxt(h.name, 22, true), c.y + 19, 17, Pal::BrassDk);
        if (h.onLeave) {
            Txt("Resting. Back after the next expedition.", c.x + 18, c.y + 56, 17, Pal::Stress);
        } else {
            Txt(TextFormat("Nerves %d / 100%s", h.stress, h.rattled ? "   RATTLED" : ""), c.x + 18, c.y + 50, 19, h.rattled ? Pal::Bad : Pal::Ink);
            DrawBar({c.x + 18, c.y + 76, 240, 10}, h.stress / 100.0f, Pal::Stress);
            int cost = std::max(10, h.stress + (h.rattled ? 40 : 0));
            if (Button({c.x + c.width - 190, c.y + 30, 170, 44}, TextFormat("Grant leave %dg", cost), g.gold >= cost)) {
                g.gold -= cost;
                h.stress = 0;
                h.rattled = false;
                h.onLeave = 1;
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
                Toast(g, h.name + " is on sick leave and will miss the next expedition.");
            }
        }
        shown++;
    }
    if (shown == 0) {
        const char* msg = "Nerves of steel all round. Nobody needs leave.";
        TxtShadow(msg, SCREEN_W / 2.0f - MeasureTxt(msg, 28, true) / 2.0f, 320, 28, Pal::Paper, true);
    }
}

// ============================================================ library
void SceneBookshelf(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Library", "Everything the crew has learned about the deep");
    const char* tabs[] = {"Crew", "Conditions", "Light & Nerves", "Bestiary", "Platforming"};
    for (int i = 0; i < 5; i++) {
        Rectangle r{140 + i * 205.0f, 92, 195, 42};
        if (i == g.bookTab) DrawRectangleRounded({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 0.3f, 6, Pal::Teal);
        if (Button(r, tabs[i])) g.bookTab = i;
    }
    Rectangle page{80, 150, SCREEN_W - 160.0f, 550};
    Panel(page);
    Rectangle body{page.x + 30, page.y + 24, page.width - 60, page.height - 40};

    if (g.bookTab == 0) {
        for (int c = 0; c < (int)HeroClass::COUNT; c++) {
            float y = body.y + c * 128.0f;
            HeroClass hc = (HeroClass)c;
            DrawRectangle((int)body.x, (int)y + 4, 8, 110, ClassColor(hc));
            TxtBold(ClassName(hc), body.x + 20, y, 24, Pal::Ink);
            DrawWrapped(ClassBlurb(hc), {body.x + 20, y + 32, body.width - 20, 40}, 16, Pal::Ink);
            std::string ab;
            for (auto& a : ClassAbilities(hc)) ab += a.name + (a.unlockLevel ? TextFormat(" (Lv %d)", a.unlockLevel) : "") + "  |  ";
            DrawWrapped(ab.substr(0, ab.size() - 5), {body.x + 20, y + 72, body.width - 20, 40}, 14, Pal::Copper);
        }
    } else if (g.bookTab == 1) {
        DrawWrapped(
            "BLEED: loses health at the start of each turn for 3 turns.\n\n"
            "POISON: loses health each turn for 3 turns. Poison STACKS (up to three doses), and a poisoned crew member only gets half the benefit of healing.\n\n"
            "STUN: loses their next turn.        MARKED: takes 25% more damage for 3 turns.\n\n"
            "RALLIED: deals extra damage for a few turns.        DODGE+ / ARMOR+: harder to hit or hurt for 3 turns.\n\n"
            "GUARDING: enemies must attack this crew member, who takes reduced damage.\n\n"
            "DEATH'S DOOR: a crew member at 0 HP is still standing, but every further hit has a chance to finish them. Any healing pulls them back.\n\n"
            "RATTLED: a crew member whose Nerves reach 100 is Rattled: less accurate, easier to hit, and may freeze up in combat. Only the Sick Bay cures it.",
            body, 17, Pal::Ink);
    } else if (g.bookTab == 2) {
        DrawWrapped(
            "THE FLASHLIGHT\nEvery room you advance into drains the flashlight (a better reflector from the Workshop slows this). Swap in a battery to recharge it.\n\n"
            "Bright (75+): your crew aim better.\n"
            "Dim (50-74): normal conditions.\n"
            "Murky (1-49): enemies hit more often and your crew's nerves fray faster, but the loot is 40% richer.\n"
            "Pitch Black (0): enemies are deadly accurate and stress piles up quickly, but loot nearly doubles.\n\n"
            "NERVES\nScary attacks, critical hits and falling to Death's Door all add stress. The Nurse's Smelling Salts and the Captain's orders bring it down, and a critical hit by your own crew lifts everyone's spirits.\n"
            "Retreating from an expedition adds stress to the whole party.",
            body, 18, Pal::Ink);
    } else if (g.bookTab == 3) {
        DrawWrapped(
            "THE CAVE (Shallows)\n\n"
            "SEA LOUSE: fast, fragile nibblers. Their bites can cause bleeding.\n\n"
            "PISTOL SHRIMP: tough little shells. Their claw can stun, and their sonic pop rattles nerves from any range.\n\n"
            "BRINE WORM: slow, but spits poison at any rank. Its poison stacks, so cure it early.\n\n"
            "THE LOBSTER (mini boss): heavily armored. Crushing Claw can stun, Tail Sweep hits your front two ranks, and its clacking frays everyone's nerves. "
            "Bleed and poison ignore its armor, and marking it helps everyone hit harder.\n\n"
            "Still uncharted: the Island, the Weeds, and Atlantis.",
            body, 18, Pal::Ink);
    } else {
        DrawWrapped(
            "THE PIPES (easy): a run through the Nautilus's steam pipes. Coins are worth 2 gold each, plus 25 gold for reaching the valve. "
            "Falling or touching steam just sends you back to the last checkpoint. Nobody dies for real here.\n\n"
            "Each Pipes layout is stitched together from hand-built sections. Beat it and a new layout is shuffled in. Stuck on a layout? Pay 10 gold at the periscope to reshuffle it.\n\n"
            "Controls: A/D or arrow keys to move, Space/W/Up to jump (hold for higher), Esc to give up the run.\n\n"
            "THE HULL (medium, coming soon): outside the hull among the growths, with an optional Kraken fight for a relic.\n\n"
            "THE PIRATE SHIP (hard, coming soon): precise jumps, pirates, parakeets, and Blackbeard. Sometimes it's a ghost ship: faster foes, double rewards.",
            body, 18, Pal::Ink);
    }
}

// ============================================================ periscope
void ScenePeriscope(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Periscope", "Platforming runs: nobody dies here, and there's gold to be had");
    DrawGoldBadge(g);
    struct Lvl { const char* name; const char* diff; const char* desc; bool open; };
    const Lvl lv[3] = {
        {"The Pipes", "EASY", "Hop through the Nautilus's steam pipes. Dodge vents, stomp rats, grab coins, and turn the valve at the end.", true},
        {"The Hull", "MEDIUM", "Outside the hull among the growths and fishing lines. Optional Kraken boss for a chance at a relic.", false},
        {"The Pirate Ship", "HARD", "Precision jumps past pirates and parakeets up to Blackbeard. Sometimes it's a ghost ship.", false},
    };
    for (int i = 0; i < 3; i++) {
        Rectangle c{60 + i * 400.0f, 110, 370, 470};
        Panel(c, lv[i].open ? Pal::Paper : Color{176, 168, 150, 255});
        Color top = i == 0 ? Pal::Copper : i == 1 ? Color{50, 110, 130, 255} : Color{90, 70, 60, 255};
        DrawVGradient({c.x + 14, c.y + 14, c.width - 28, 120}, ColorBrightness(top, 0.15f), ColorBrightness(top, -0.35f));
        DrawTextCenteredBold(lv[i].name, c.x + c.width / 2, c.y + 46, 32, Pal::Paper);
        DrawTextCenteredBold(lv[i].diff, c.x + c.width / 2, c.y + 90, 18, Pal::Paper);
        DrawWrapped(lv[i].desc, {c.x + 20, c.y + 150, c.width - 40, 120}, 17, Pal::Ink);
        if (lv[i].open) {
            Txt("Reward: 2 gold per coin", c.x + 20, c.y + 270, 17, Pal::BrassDk);
            Txt("+25 gold for reaching the valve", c.x + 20, c.y + 294, 17, Pal::BrassDk);
            TxtBold(TextFormat("Current layout: %s", PipesLayoutCode(g).c_str()), c.x + 20, c.y + 330, 19, Pal::Ink);
            if (Button({c.x + 20, c.y + 364, c.width - 40, 44}, "Dive in")) { StartPipes(g); return; }
            if (Button({c.x + 20, c.y + 414, c.width - 40, 40}, "Reshuffle layout (10g)", g.gold >= 10)) {
                g.gold -= 10;
                GeneratePipesLayout(g);
                Toast(g, "The pipes rattle and rearrange themselves...");
            }
        } else {
            DrawTextCentered("Coming in a later build", c.x + c.width / 2, c.y + 400, 21, Pal::BrassDk);
        }
    }
    const char* help = "A/D or arrows to move   |   Space to jump (hold for height)   |   Esc to give up";
    TxtShadow(help, SCREEN_W / 2.0f - MeasureTxt(help, 19) / 2.0f, 610, 19, Pal::Paper);
}
