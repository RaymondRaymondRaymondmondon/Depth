// ============================================================================
//  DEPTH - the Nautilus's grand salon: the hub.
//
//  Inspired by Captain Nemo's salon in "Twenty Thousand Leagues Under the Sea":
//  one room seen through a fixed camera, in true perspective. Every point in
//  the room is given in room units (X right, Y up, Z away from the camera) and
//  projected to the screen. Stations sit around the room: bookcases and doors
//  are painted flat onto a canvas, then mapped onto the side walls with a
//  perspective-correct textured quad; furniture on the floor is drawn at the
//  size its distance calls for. The crew and the ship's cat wander the floor.
// ============================================================================
#include "game.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
// ---------------------------------------------------------------- the room and the camera
constexpr float FOCAL = 440, CX = SCREEN_W / 2.0f, CY = 320, EYE = 230;
constexpr float RW = 720, RH = 600, Z_NEAR = 290, Z_BACK = 1150;
constexpr float HUD_Y = 654, CREW_H = 190; // a crew member's height in room units
constexpr float PERI_X = 150, PERI_Z = 560;
constexpr float WARD_X = 590; // the Ward's cabinet, in the back right corner // the periscope stands between the great window and the Ward

Vector2 Proj(float X, float Y, float Z) { return {CX + X * FOCAL / Z, CY - (Y - EYE) * FOCAL / Z}; }
Vector2 Proj(Vector3 p) { return Proj(p.x, p.y, p.z); }
float Px(float Z) { return FOCAL / Z; }
Vector3 Mix(Vector3 a, Vector3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// A textured quad given in room coordinates, split into a grid so the texture follows the perspective.
void ProjQuad(Texture2D tex, Vector3 tl, Vector3 tr, Vector3 br, Vector3 bl, Rectangle uv, Color tint, int nu, int nv) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    auto at = [&](float u, float v) { return Proj(Mix(Mix(tl, tr, u), Mix(bl, br, u), v)); };
    auto vtx = [&](float u, float v) {
        Vector2 p = at(u, v);
        rlTexCoord2f(uv.x + uv.width * u, uv.y + uv.height * v);
        rlVertex2f(p.x, p.y);
    };
    for (int i = 0; i < nu; i++)
        for (int j = 0; j < nv; j++) {
            float u0 = (float)i / nu, u1 = (float)(i + 1) / nu, v0 = (float)j / nv, v1 = (float)(j + 1) / nv;
            vtx(u0, v0); vtx(u0, v1); vtx(u1, v1); vtx(u1, v0);
        }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void ProjFill(Vector3 tl, Vector3 tr, Vector3 br, Vector3 bl, Color c) {
    Vector2 a = Proj(tl), b = Proj(tr), d = Proj(br), e = Proj(bl);
    DrawTri(a, b, d, c);
    DrawTri(a, d, e, c);
}

Rectangle Bounds(std::initializer_list<Vector2> pts) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (Vector2 p : pts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    return {x0, y0, x1 - x0, y1 - y0};
}

// Paints flat art (drawn by `draw` into a w x h canvas) onto a vertical stretch of a side wall.
// Returns where it landed on screen.
constexpr float ART_PX = 1.5f; // canvas pixels per room unit
template <typename F>
Rectangle PaintWall(float X, float Z0, float Z1, float Y0, int w, int h, F draw) {
    RenderTexture2D& rt = ArtRT();
    BeginCanvas(rt);
    draw((float)w, (float)h);
    EndCanvas();
    float Y1 = Y0 + h / ART_PX;
    bool left = X < 0; // on the left wall the art reads from the back of the room toward the front
    Vector3 nt{X, Y1, Z0}, ft{X, Y1, Z1}, fb{X, Y0, Z1}, nb{X, Y0, Z0};
    Vector3 tl = left ? ft : nt, tr = left ? nt : ft, br = left ? nb : fb, bl = left ? fb : nb;
    ProjQuad(rt.texture, tl, tr, br, bl, {0, 1, w / (float)rt.texture.width, -h / (float)rt.texture.height}, WHITE, 18, 1);
    return Bounds({Proj(nt), Proj(ft), Proj(fb), Proj(nb)});
}

// Draws something standing at (X, Y, Z) facing the camera, in room units: +x right, +y DOWN, origin at its base.
template <typename F>
void Billboard(float X, float Y, float Z, F draw) {
    Vector2 p = Proj(X, Y, Z);
    float k = Px(Z);
    rlPushMatrix();
    rlTranslatef(p.x, p.y, 0);
    rlScalef(k, k, 1);
    draw();
    rlPopMatrix();
}

float Hash01(int a, int b) {
    unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}
float RandF(float lo, float hi) { return lo + (hi - lo) * GetRandomValue(0, 10000) / 10000.0f; }

// ---------------------------------------------------------------- stations
enum StationId { ST_CREW, ST_LIBRARY, ST_RADAR, ST_HELM, ST_PERISCOPE, ST_WORKSHOP, ST_SICKBAY, ST_WARD, ST_COUNT };
struct Station { Scene target; const char* name; const char* hint; Vector2 stand; }; // stand = (X, Z) where crew gather
const Station STATIONS[ST_COUNT] = {
    {Scene::Crew, "Crew Quarters", "Choose your party, fit relics, and pick each crew member's abilities.", {-540, 620}},
    {Scene::Bookshelf, "Library", "Learn about the crew, conditions, and creatures of the deep.", {-540, 900}},
    {Scene::Radar, "Radar Room", "Pick up new recruits and buy relics from passing salvagers.", {540, 620}},
    {Scene::Helm, "The Helm", "Chart a course and send your party on an expedition.", {-110, 800}},
    {Scene::Periscope, "Periscope", "Take on a platforming run for gold.", {300, 600}},
    {Scene::Workshop, "Workshop", "Upgrade the Nautilus: reflectors, bunks, sonar and infirmary gear.", {540, 900}},
    {Scene::SickLeave, "Sick Bay", "Rattled crew can rest by the organ and steady their nerves.", {-400, 930}},
    {Scene::Ward, "The Ward", "Patch up injured crew.", {400, 930}},
};
Rectangle stationRect[ST_COUNT];

// ---------------------------------------------------------------- life aboard
// Only the Nautilus's own hands walk the salon (your expedition crew stay in their quarters). Most have a
// post, where they stand working, and every so often they stretch their legs and come back.
struct Npc { const char* role; HeroClass build; int outfit; Vector2 post; bool faceRight; };
const Npc NPCS[] = {
    {"Helmsman", HeroClass::Captain, OUT_HELMSMAN, {-70, 860}, true},    // at the wheel
    {"Radio operator", HeroClass::Mechanic, OUT_RADIO, {555, 625}, true}, // listening at the sonar
    {"Engineer", HeroClass::Mechanic, OUT_ENGINEER, {555, 910}, true},    // hammering at the workbench
    {"Professor", HeroClass::Captain, OUT_PROFESSOR, {-555, 930}, false}, // browsing the shelves
    {"Steward", HeroClass::Captain, OUT_STEWARD, {0, 0}, true},           // no post: does the rounds with a tray
    {"Orderly", HeroClass::Nurse, OUT_ORDERLY, {250, 985}, true},         // tending the operating table
};
constexpr int NPC_COUNT = sizeof(NPCS) / sizeof(NPCS[0]);
struct Walker { int id; Vector2 pos, target; float wait, phase; bool right, atPost; };
struct Cat { Vector2 pos{120, 700}, target{120, 700}; float wait = 3, phase = 0, purr = 0; bool right = true; };
Rectangle catRect{};   // where the cat is on screen, for clicking
Vector2 catHead{};     // where its head is on screen, for the hearts
std::vector<Walker> walkers;
Cat cat;

// Furniture on the floor, as circles (X, Z, radius) that people and the cat walk around.
struct Obstacle { float x, z, r; };
const Obstacle OBSTACLES[] = {{0, 900, 95}, {-255, 900, 105}, {150, 560, 45}, {-430, 1010, 175}, {430, 1010, 170}};

const Hero& NpcHero(int i) {
    static Hero heroes[NPC_COUNT];
    heroes[i].id = 100000 + i * 3 + 1; // only used to vary their looks
    heroes[i].cls = NPCS[i].build;
    heroes[i].outfit = NPCS[i].outfit;
    return heroes[i];
}

bool Blocked(Vector2 p, float margin) {
    for (auto& o : OBSTACLES) if ((p.x - o.x) * (p.x - o.x) + (p.y - o.z) * (p.y - o.z) < (o.r + margin) * (o.r + margin)) return true;
    return false;
}

Vector2 FloorSpot() {
    for (int tries = 0; tries < 30; tries++) {
        Vector2 p{RandF(-560, 560), RandF(560, 1060)};
        if (!Blocked(p, 30)) return p;
    }
    return {0, 640};
}

// Head for `target`, but slide around any furniture in the way.
Vector2 Steer(Vector2 pos, Vector2 target, float step) {
    Vector2 d{target.x - pos.x, target.y - pos.y};
    float len = sqrtf(d.x * d.x + d.y * d.y);
    if (len < 0.001f) return pos;
    d = {d.x / len, d.y / len};
    for (auto& o : OBSTACLES) {
        Vector2 away{pos.x - o.x, pos.y - o.z};
        float dist = sqrtf(away.x * away.x + away.y * away.y), reach = o.r + 45;
        if (dist >= reach || dist < 0.001f) continue;
        away = {away.x / dist, away.y / dist};
        float push = (reach - dist) / 45;
        Vector2 side{-away.y, away.x};                            // go round on whichever side leads toward the target
        if (side.x * d.x + side.y * d.y < 0) side = {-side.x, -side.y};
        d = {d.x + away.x * push * 1.2f + side.x * push, d.y + away.y * push * 1.2f + side.y * push};
    }
    float n = sqrtf(d.x * d.x + d.y * d.y);
    Vector2 np{pos.x + d.x / n * step, pos.y + d.y / n * step};
    for (auto& o : OBSTACLES) { // never end up inside anything
        Vector2 a{np.x - o.x, np.y - o.z};
        float dist = sqrtf(a.x * a.x + a.y * a.y);
        if (dist < o.r && dist > 0.001f) np = {o.x + a.x / dist * o.r, o.z + a.y / dist * o.r};
    }
    np.x = std::clamp(np.x, -600.0f, 600.0f);
    np.y = std::clamp(np.y, 540.0f, 1080.0f);
    return np;
}

void PickTarget(Walker& w) {
    const Npc& n = NPCS[w.id];
    bool hasPost = n.post.x != 0 || n.post.y != 0;
    if (hasPost && !w.atPost && GetRandomValue(0, 99) < 75) { w.target = n.post; w.atPost = true; return; }
    w.atPost = false;
    w.target = GetRandomValue(0, 99) < 60 ? STATIONS[GetRandomValue(0, ST_COUNT - 1)].stand : FloorSpot();
    if (Blocked(w.target, 20)) w.target = FloorSpot();
}

// Only two of the ship's hands are ever out on the floor at once (plus the cat): a full crowd made the
// room feel cramped. The rest are elsewhere on the boat, at their posts, off scene.
constexpr int ACTIVE_NPCS = 2;
void UpdateLife(Game& g, float dt) {
    (void)g;
    if (walkers.empty()) {
        std::vector<int> idx;
        for (int i = 0; i < NPC_COUNT; i++) idx.push_back(i);
        for (int i = (int)idx.size() - 1; i > 0; i--) std::swap(idx[i], idx[GetRandomValue(0, i)]);
        for (int k = 0; k < ACTIVE_NPCS; k++) {
            int i = idx[k];
            bool hasPost = NPCS[i].post.x != 0 || NPCS[i].post.y != 0;
            Vector2 start = hasPost ? NPCS[i].post : FloorSpot();
            walkers.push_back({i, start, start, RandF(2, 10), 0, NPCS[i].faceRight, hasPost});
        }
    }
    for (auto& w : walkers) {
        if (w.wait > 0) {
            w.wait -= dt;
            w.phase = 0;
            if (w.atPost) w.right = NPCS[w.id].faceRight;
            if (w.wait <= 0) PickTarget(w);
            continue;
        }
        Vector2 d{w.target.x - w.pos.x, w.target.y - w.pos.y};
        float len = sqrtf(d.x * d.x + d.y * d.y), step = 70 * dt;
        if (len <= step + 2) {
            w.pos = w.target;
            w.wait = w.atPost ? RandF(12, 30) : RandF(2, 6);
            w.phase = 0;
            continue;
        }
        Vector2 np = Steer(w.pos, w.target, step);
        if (fabsf(np.x - w.pos.x) > 0.05f) w.right = np.x > w.pos.x;
        w.pos = np;
        w.phase += dt * 7.5f;
    }
    if (cat.purr > 0) { cat.purr -= dt; cat.wait = std::max(cat.wait, cat.purr + 0.5f); } // a scratch behind the ears: it stays put
    if (cat.wait > 0) { cat.wait -= dt; cat.phase = 0; if (cat.wait <= 0) cat.target = FloorSpot(); }
    else {
        Vector2 d{cat.target.x - cat.pos.x, cat.target.y - cat.pos.y};
        float len = sqrtf(d.x * d.x + d.y * d.y), step = 55 * dt;
        if (len <= step + 2) { cat.pos = cat.target; cat.wait = RandF(3, 10); }
        else {
            Vector2 np = Steer(cat.pos, cat.target, step);
            cat.right = np.x > cat.pos.x;
            cat.pos = np;
            cat.phase += dt * 10;
        }
    }
}

// What someone is doing with their hands while they stand at their post.
Pose WorkPose(const Walker& w, float t) {
    Pose p;
    if (!w.atPost || w.wait <= 0) return p;
    float ph = t + w.id * 1.7f;
    auto bell = [](float u, float a, float b, float c) {
        auto sm = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };
        return u <= a || u >= c ? 0.0f : u < b ? sm((u - a) / (b - a)) : sm((c - u) / (c - b));
    };
    switch (NPCS[w.id].outfit) {
        case OUT_HELMSMAN: p.reach = 0.55f + 0.12f * sinf(ph * 0.9f); p.lean = 0.08f; break;           // hands on the wheel
        case OUT_RADIO: p.reach = 0.35f; p.lean = 0.14f + 0.04f * sinf(ph * 2); break;                  // tuning the set
        case OUT_ENGINEER: { float c = fmodf(ph * 1.1f, 1.0f); p.raise = bell(c, 0, 0.45f, 0.6f);       // hammering
                             p.weaponTilt = 70 * bell(c, 0.5f, 0.62f, 0.9f); p.lean = 0.25f * bell(c, 0.5f, 0.62f, 0.9f); } break;
        case OUT_PROFESSOR: p.backRaise = 0.55f + 0.15f * sinf(ph * 0.7f); break;                        // reaching for a book
        case OUT_ORDERLY: p.reach = 0.3f; p.lean = 0.2f + 0.05f * sinf(ph); p.crouch = 0.1f; break;      // tidying the table
        default: break;
    }
    return p;
}

// ---------------------------------------------------------------- the ocean beyond the great window
void DrawOcean(float t) {
    BeginLayer(OceanRT());
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{44, 118, 134, 255}, Color{6, 28, 46, 255});
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 7; k++) {
        float x = 300 + k * 110 + sinf(t * 0.3f + k) * 20, w = 20 + (k % 3) * 14;
        DrawTri({x, 0}, {x + w, 0}, {x - 90, 720}, Color{90, 170, 180, 14});
        DrawTri({x + w, 0}, {x - 90 + w * 1.8f, 720}, {x - 90, 720}, Color{90, 170, 180, 14});
    }
    EndBlendMode();
    float ph = fmodf(t, 70) / 70; // a whale drifts by now and then
    float wx = 200 + ph * 900, wy = 200 + sinf(ph * 9) * 10;
    Color whale{18, 56, 72, 255};
    DrawEllipse((int)wx, (int)wy, 80, 20, whale);
    DrawEllipse((int)(wx + 64), (int)(wy + 3), 30, 15, whale);
    DrawTri({wx - 72, wy}, {wx - 120, wy - 22 + sinf(t) * 5}, {wx - 116, wy + 18 + sinf(t) * 5}, whale);
    for (int x = 300; x < 980; x += 6) {
        float h = 380 + sinf(x * 0.02f) * 14 + sinf(x * 0.05f + 2) * 6;
        DrawRectangle(x, (int)h, 6, 400, Color{12, 40, 54, 255});
    }
    for (int k = 0; k < 7; k++) { // kelp
        float bx = 340 + k * 95, by = 440;
        Vector2 prev{bx, by};
        for (int s = 1; s <= 9; s++) {
            Vector2 p{bx + sinf(t * 0.8f + k + s * 0.4f) * s * 1.6f, by - s * 12.0f};
            DrawLineEx(prev, p, 4 - s * 0.3f, Color{16, 64, 58, 255});
            prev = p;
        }
    }
    for (int k = 0; k < 3; k++) { // schools of fish
        float dir = k % 2 ? 1.0f : -1.0f, cx = fmodf(k * 300 + dir * t * (18 + k * 6) + 90000, 900) + 200, cy = 250 + k * 50 + sinf(t * 0.4f + k) * 14;
        for (int f = 0; f < 14; f++) {
            float fx = cx + (Hash01(k, f) - 0.5f) * 90 + sinf(t * 1.5f + f) * 3, fy = cy + (Hash01(k, f + 50) - 0.5f) * 36;
            DrawEllipse((int)fx, (int)fy, 4, 1.6f, Color{140, 185, 196, 255});
            DrawTri({fx - dir * 4, fy}, {fx - dir * 7, fy - 2}, {fx - dir * 7, fy + 2}, Color{140, 185, 196, 255});
        }
    }
    for (int k = 0; k < 40; k++) {
        float x = 300 + fmodf(k * 71.0f + t * 3, 680.0f), y = fmodf(k * 37.0f + t * (6 + k % 5), 720.0f);
        DrawCircle((int)x, (int)y, 1, Color{210, 235, 240, 120});
    }
    EndLayer();
}

// ---------------------------------------------------------------- the room's shell
void DrawShell(float t) {
    Texture2D wood = GetTex(Tex::Wood), metal = GetTex(Tex::Metal);
    Color panel{58, 88, 84, 255}, woodTint{140, 92, 64, 255}, dark{90, 60, 44, 255};
    // ceiling: coffered dark wood with brass-trimmed beams
    ProjQuad(wood, {-RW, RH, Z_BACK}, {RW, RH, Z_BACK}, {RW, RH, Z_NEAR}, {-RW, RH, Z_NEAR}, {0, 0, 6, 5}, Color{80, 56, 42, 255}, 1, 20);
    for (float z = 420; z < Z_BACK; z += 180)
        ProjFill({-RW, RH, z}, {RW, RH, z}, {RW, RH, z - 26}, {-RW, RH, z - 26}, Color{50, 34, 24, 255});
    for (float x = -RW + 240; x < RW; x += 240)
        ProjFill({x - 12, RH, Z_BACK}, {x + 12, RH, Z_BACK}, {x + 12, RH, Z_NEAR}, {x - 12, RH, Z_NEAR}, Color{58, 40, 28, 255});
    // side walls: dark green panelling above a mahogany wainscot
    for (int s = -1; s <= 1; s += 2) {
        float X = s * RW;
        Vector3 ft{X, RH, Z_BACK}, nt{X, RH, Z_NEAR}, nb{X, 0, Z_NEAR}, fb{X, 0, Z_BACK};
        Vector3 fm{X, 170, Z_BACK}, nm{X, 170, Z_NEAR};
        if (s < 0) {
            ProjQuad(metal, ft, nt, nm, fm, {0, 0, 5, 3}, panel, 20, 1);
            ProjQuad(wood, fm, nm, nb, fb, {0, 0, 5, 1}, woodTint, 20, 1);
        } else {
            ProjQuad(metal, nt, ft, fm, nm, {0, 0, 5, 3}, panel, 20, 1);
            ProjQuad(wood, nm, fm, fb, nb, {0, 0, 5, 1}, woodTint, 20, 1);
        }
        for (float z = 500; z < Z_BACK; z += 220) // gilded pilasters
            ProjFill({X, RH, z - 14}, {X, RH, z + 14}, {X, 0, z + 14}, {X, 0, z - 14}, Color{112, 80, 44, 255});
        for (float y : {170.0f, 176.0f}) // brass rail on the wainscot
            DrawLineEx(Proj(X, y, Z_BACK), Proj(X, y, Z_NEAR), 2, Pal::Brass);
        DrawLineEx(Proj(X, RH - 6, Z_BACK), Proj(X, RH - 6, Z_NEAR), 4, Pal::BrassDk);
    }
    // the back wall
    Vector2 bl = Proj(-RW, 0, Z_BACK), tr = Proj(RW, RH, Z_BACK);
    Rectangle back{bl.x, tr.y, tr.x - bl.x, bl.y - tr.y};
    float kb = Px(Z_BACK);
    DrawTiled(Tex::Metal, {back.x, back.y, back.width, back.height - 170 * kb}, kb, panel);
    DrawTiled(Tex::Wood, {back.x, back.y + back.height - 170 * kb, back.width, 170 * kb}, kb, woodTint);
    DrawRectangleRec({back.x, back.y + back.height - 172 * kb, back.width, 3}, Pal::Brass);
    for (float x : {-300.0f, 300.0f}) {
        Vector2 a = Proj(x - 14, RH, Z_BACK), b = Proj(x + 14, 0, Z_BACK);
        DrawRectangleRec({a.x, a.y, b.x - a.x, b.y - a.y}, Color{112, 80, 44, 255});
    }
    // the floor: parquet, and a great patterned rug
    ProjQuad(wood, {-RW, 0, Z_BACK}, {RW, 0, Z_BACK}, {RW, 0, Z_NEAR}, {-RW, 0, Z_NEAR}, {0, 0, 7, 9}, Color{150, 104, 70, 255}, 1, 28);
    ProjFill({-420, 0, 1060}, {420, 0, 1060}, {420, 0, 470}, {-420, 0, 470}, Color{110, 30, 32, 255});
    ProjFill({-390, 0, 1040}, {390, 0, 1040}, {390, 0, 490}, {-390, 0, 490}, Color{150, 48, 40, 255});
    ProjFill({-350, 0, 1010}, {350, 0, 1010}, {350, 0, 520}, {-350, 0, 520}, Color{120, 34, 34, 255});
    for (int k = 0; k < 5; k++) { // medallions woven into the rug
        float z = 580 + k * 100, w = 60;
        Vector2 c = Proj(0, 0, z), l = Proj(-w, 0, z), f = Proj(0, 0, z + 40), n = Proj(0, 0, z - 40);
        DrawTri(l, f, {2 * c.x - l.x, c.y}, Color{196, 150, 80, 255});
        DrawTri(l, {2 * c.x - l.x, c.y}, n, Color{196, 150, 80, 255});
    }
    // baseboards and the dark corners where walls meet
    for (int s = -1; s <= 1; s += 2) DrawLineEx(Proj(s * RW, 0, Z_BACK), Proj(s * RW, 0, Z_NEAR), 3, Color{40, 26, 18, 255});
    DrawLineEx(Proj(-RW, 0, Z_BACK), Proj(RW, 0, Z_BACK), 3, Color{40, 26, 18, 255});
    (void)t;
}

// ---------------------------------------------------------------- wall art (painted flat, mapped onto walls)
void Plate(float x, float y, float w, float h, const char* text, int size) { DrawBrassPlate({x, y, w, h}, text, size); }

void ArtCrewDoor(float w, float h, float t) {
    Plate(20, 4, w - 40, 38, "CREW QUARTERS", 17);
    DrawRectangleRounded({14, 58, w - 28, h - 58}, 0.3f, 12, Color{88, 98, 96, 255});
    Rectangle in{38, 82, w - 76, h - 82};
    DrawRectangleRounded(in, 0.3f, 12, Color{46, 34, 26, 255});
    DrawTiled(Tex::Wood, {in.x + 14, in.y + 60, in.width - 28, in.height - 60}, 0.6f, Color{130, 92, 66, 255});
    for (int b = 0; b < 2; b++) {
        float by = h - 90 - b * 170;
        DrawRectangle((int)in.x + 8, (int)by, (int)in.width - 16, 12, Color{100, 106, 108, 255});
        DrawRectangleRounded({in.x + 12, by - 24, in.width - 24, 26}, 0.4f, 6, Color{216, 208, 192, 255});
        DrawRectangleRounded({in.x + 60, by - 30, in.width - 72, 28}, 0.5f, 6, b ? Color{150, 58, 48, 255} : Color{58, 88, 122, 255});
        DrawEllipse((int)in.x + 36, (int)by - 26, 22, 10, Color{238, 234, 224, 255});
    }
    DrawCircle((int)(w / 2), (int)in.y + 70, 10, Color{255, 214, 150, 255}); // lantern
    for (float y = 90; y < h - 10; y += 44) {
        DrawCircleV({26, y}, 3, Color{60, 66, 64, 255});
        DrawCircleV({w - 26, y}, 3, Color{60, 66, 64, 255});
    }
    DrawRing({w - 22, h * 0.55f}, 9, 14, 0, 360, 20, Color{170, 40, 36, 255});
    (void)t;
}

void ArtLibrary(float w, float h, float t) {
    Plate(w / 2 - 80, 4, 160, 38, "LIBRARY", 19);
    const Color books[7] = {{150, 50, 44, 255}, {50, 84, 130, 255}, {56, 110, 70, 255}, {176, 132, 56, 255},
                            {100, 60, 110, 255}, {200, 190, 170, 255}, {90, 60, 40, 255}};
    for (int c = 0; c < 2; c++) {
        Rectangle bc{12 + c * (w / 2 - 6), 56, w / 2 - 18, h - 56};
        DrawTiled(Tex::Wood, bc, 0.5f, Color{128, 82, 54, 255});
        DrawRectangleRec({bc.x + 10, bc.y + 18, bc.width - 20, bc.height - 26}, Color{34, 22, 16, 255});
        int shelves = 7;
        float sh = (bc.height - 30) / shelves;
        for (int s = 0; s < shelves; s++) {
            float shelfY = bc.y + 18 + (s + 1) * sh;
            float x = bc.x + 13;
            for (int b = 0; x < bc.x + bc.width - 22; b++) {
                int id = c * 1000 + s * 50 + b;
                int bw = 8 + (int)(Hash01(id, 1) * 9), bh = (int)(sh * (0.6f + Hash01(id, 2) * 0.3f));
                Color col = books[(int)(Hash01(id, 3) * 6.99f)];
                DrawRectangle((int)x, (int)(shelfY - bh), bw, bh, col);
                DrawRectangle((int)x, (int)(shelfY - bh), 2, bh, Fade(WHITE, 0.14f));
                DrawRectangle((int)x, (int)(shelfY - bh + 7), bw, 2, Fade(Pal::Brass, 0.7f));
                x += bw + 1;
            }
            DrawRectangle((int)bc.x + 6, (int)shelfY, (int)bc.width - 12, 8, Color{104, 66, 42, 255});
            DrawRectangle((int)bc.x + 6, (int)shelfY + 8, (int)bc.width - 12, 3, Fade(BLACK, 0.4f));
        }
        DrawRectangle((int)bc.x - 6, (int)bc.y - 10, (int)bc.width + 12, 16, Color{96, 60, 38, 255});
    }
    for (int k = 1; k < 16; k++) { // rolling ladder
        float f = k / 16.0f;
        DrawLineEx({w * 0.62f + f * 40, 70 + f * (h - 70)}, {w * 0.62f + 40 + f * 40, 70 + f * (h - 70)}, 5, Color{110, 72, 44, 255});
    }
    DrawLineEx({w * 0.62f, 70}, {w * 0.62f + 40, h}, 6, Color{136, 90, 56, 255});
    DrawLineEx({w * 0.62f + 40, 70}, {w * 0.62f + 80, h}, 6, Color{136, 90, 56, 255});
    (void)t;
}

void ArtRadar(float w, float h, float t) {
    Plate(10, 4, w - 20, 38, "RADAR ROOM", 16);
    Color steel{70, 90, 86, 255};
    DrawVGradient({10, 56, w - 20, h - 56}, ColorBrightness(steel, -0.15f), ColorBrightness(steel, -0.4f));
    DrawRectangleLinesEx({10, 56, w - 20, h - 56}, 4, ColorBrightness(steel, -0.55f));
    Vector2 c{w / 2, 170};
    DrawCircleV(c, 88, Pal::BrassDk);
    DrawRing(c, 78, 88, 0, 360, 48, Pal::Brass);
    DrawCircleV(c, 78, Color{6, 30, 18, 255});
    for (int k = 1; k <= 3; k++) DrawRing(c, 25.0f * k - 1, 25.0f * k + 1, 0, 360, 48, Color{40, 150, 80, 140});
    float a = t * 1.6f, ad = fmodf(a * RAD2DEG, 360);
    for (int k = 0; k < 12; k++) DrawCircleSector(c, 77, ad - (k + 1) * 5, ad - k * 5, 3, Color{80, 255, 140, (unsigned char)(80 * (1 - k / 12.0f))});
    DrawLineEx(c, {c.x + cosf(a) * 77, c.y + sinf(a) * 77}, 3, Color{150, 255, 180, 255});
    const Vector2 blips[4] = {{30, -26}, {-44, 16}, {14, 50}, {-18, -56}};
    for (auto b : blips) {
        float since = fmodf(a - atan2f(b.y, b.x) + 20 * PI, 2 * PI);
        DrawCircleV({c.x + b.x, c.y + b.y}, 5, Color{170, 255, 190, (unsigned char)(255 * std::max(0.0f, 1 - since / 3))});
    }
    Rectangle osc{30, 290, w - 60, 90};
    DrawRectangleRounded({osc.x - 6, osc.y - 6, osc.width + 12, osc.height + 12}, 0.2f, 6, Pal::BrassDk);
    DrawRectangleRounded(osc, 0.15f, 6, Color{6, 30, 18, 255});
    Vector2 prev{osc.x + 4, osc.y + osc.height / 2};
    for (float x = 4; x < osc.width - 4; x += 3) {
        Vector2 p{osc.x + x, osc.y + osc.height / 2 + sinf(x * 0.12f + t * 6) * 26 * sinf(x * 0.02f + 0.3f)};
        DrawLineEx(prev, p, 2, Color{120, 255, 160, 255});
        prev = p;
    }
    const Color lamp[3] = {{255, 80, 60, 255}, {255, 190, 60, 255}, {90, 255, 120, 255}};
    for (int k = 0; k < 8; k++) {
        bool on = fmodf(t * (0.7f + Hash01(k, 3)) + k * 0.37f, 1.0f) < 0.55f;
        Vector2 p{34 + (k % 4) * ((w - 68) / 3), 420 + (k / 4) * 34.0f};
        DrawCircleV(p, 9, Color{30, 30, 30, 255});
        DrawCircleV(p, 7, on ? lamp[k % 3] : ColorBrightness(lamp[k % 3], -0.7f));
    }
    for (int k = 0; k < 3; k++) {
        Vector2 kc{50 + k * (w - 100) / 2, 530};
        DrawCircleV(kc, 18, Color{36, 36, 38, 255});
        DrawLineEx(kc, {kc.x + cosf(t * 0.3f + k) * 14, kc.y + sinf(t * 0.3f + k) * 14}, 3, Pal::Paper);
    }
    DrawRectangle(10, (int)h - 60, (int)w - 20, 8, ColorBrightness(steel, -0.55f));
}

void ArtWorkshop(float w, float h, float t) {
    Plate(w / 2 - 90, 4, 180, 38, "WORKSHOP", 19);
    Rectangle pb{20, 70, w * 0.55f, 250};
    DrawRectangleRec(pb, Color{140, 104, 68, 255});
    DrawRectangleLinesEx(pb, 5, Color{90, 62, 38, 255});
    for (float y = pb.y + 14; y < pb.y + pb.height - 6; y += 18)
        for (float x = pb.x + 14; x < pb.x + pb.width - 6; x += 18) DrawCircleV({x, y}, 2, Color{70, 48, 30, 255});
    Color tool{150, 152, 160, 255};
    DrawLineEx({pb.x + 40, pb.y + 40}, {pb.x + 40, pb.y + 170}, 8, Color{120, 80, 50, 255});
    DrawRectangle((int)pb.x + 20, (int)pb.y + 28, 40, 20, tool);
    DrawLineEx({pb.x + 100, pb.y + 36}, {pb.x + 100, pb.y + 170}, 7, tool);
    DrawRing({pb.x + 100, pb.y + 36}, 8, 15, 200, 520, 16, tool);
    DrawRectangle((int)pb.x + 140, (int)pb.y + 40, 100, 40, tool);
    for (int k = 0; k < 14; k++) DrawTri({pb.x + 140 + k * 7.0f, pb.y + 80}, {pb.x + 147 + k * 7.0f, pb.y + 80}, {pb.x + 143 + k * 7.0f, pb.y + 88}, tool);
    DrawRing({pb.x + 190, pb.y + 170}, 20, 28, 0, 360, 24, Color{170, 150, 100, 255});
    float sp = t * 0.6f, r1 = 70, r2 = 44, r3 = 34;
    Vector2 g1{w * 0.78f, 170};
    DrawGear(g1, r1, 12, sp, Pal::Brass);
    DrawGear({g1.x + r1 * 0.55f, g1.y + r1 + r2 - 6}, r2, 8, -sp * r1 / r2 + 0.2f, Pal::Copper);
    DrawGear({g1.x - r1 * 0.9f, g1.y + r1 + r3 - 2}, r3, 7, -sp * r1 / r3 + 0.4f, Color{164, 166, 158, 255});
    // the bench, a vise, the grinder throwing sparks
    float by = h - 200;
    DrawRectangle(24, (int)by + 24, 18, 176, Color{84, 56, 34, 255});
    DrawRectangle((int)w - 42, (int)by + 24, 18, 176, Color{84, 56, 34, 255});
    DrawTiled(Tex::Wood, {10, by, w - 20, 30}, 0.5f, Color{150, 104, 66, 255});
    DrawRectangle(30, (int)by - 44, 60, 44, Color{90, 96, 100, 255});
    Vector2 gw{w - 90, by - 34};
    DrawRectangleRounded({gw.x - 80, gw.y - 30, 70, 64}, 0.3f, 6, Color{60, 90, 80, 255});
    DrawCircleV(gw, 32, Color{120, 116, 110, 255});
    for (int k = 0; k < 4; k++) DrawLineEx(gw, {gw.x + cosf(t * 20 + k * PI / 2) * 30, gw.y + sinf(t * 20 + k * PI / 2) * 30}, 3, Color{80, 78, 74, 255});
    if (fmodf(t, 6) < 2.2f)
        for (int k = 0; k < 14; k++) { // sparks, placed by time so they need no bookkeeping
            float ph = fmodf(t * 3 + k * 0.137f, 1.0f), vx = -80 - Hash01(k, 1) * 140, vy = -60 - Hash01(k, 2) * 120;
            Vector2 p{gw.x - 20 + vx * ph, gw.y + vy * ph + 220 * ph * ph};
            DrawLineEx(p, {p.x - vx * 0.03f, p.y - (vy + 440 * ph) * 0.03f}, 2, Color{255, 200, 100, (unsigned char)(255 * (1 - ph))});
        }
    DrawRectangleRounded({80, h - 70, 130, 64}, 0.15f, 4, Color{170, 40, 36, 255});
}

// ---------------------------------------------------------------- the back wall and the furniture
void DrawGreatWindow(float t) {
    (void)t;
    Vector2 c = Proj(0, 330, Z_BACK);
    float r = 250 * Px(Z_BACK);
    DrawCircleV({c.x + 3, c.y + 5}, r + 20, Fade(BLACK, 0.45f));
    DrawCircleV(c, r + 20, Pal::BrassDk);
    DrawRing(c, r + 4, r + 17, 0, 360, 72, Pal::Brass);
    for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        DrawCircleV({c.x + cosf(a) * (r + 11), c.y + sinf(a) * (r + 11)}, 2.6f, Pal::BrassDk);
    }
    DrawTexturedCircle(OceanRT().texture, c, r + 4, true);
    for (int k = 0; k < 4; k++) { // the window's brass mullions
        float a = k * PI / 4;
        DrawLineEx({c.x - cosf(a) * r, c.y - sinf(a) * r}, {c.x + cosf(a) * r, c.y + sinf(a) * r}, 3, Fade(Pal::BrassDk, 0.9f));
    }
    DrawCircleV(c, 10, Pal::Brass);
    DrawRing(c, r - 8, r + 4, 0, 360, 72, Fade(BLACK, 0.3f));
    DrawCircleSector(c, r * 0.9f, 200, 245, 16, Fade(WHITE, 0.07f));
}

void DrawOrgan(float t, bool playing) {
    Billboard(-520, 0, Z_BACK, [&] {
        // pipes rising behind the console
        for (int k = 0; k < 15; k++) {
            float x = -170 + k * 24, hh = 300 + sinf(k * 0.55f) * 90 + (7 - fabsf(k - 7.0f)) * 22;
            DrawRectangleGradientH((int)x, (int)(-160 - hh), 11, (int)hh, Color{150, 110, 50, 255}, Color{236, 196, 120, 255});
            DrawRectangleGradientH((int)x + 11, (int)(-160 - hh), 11, (int)hh, Color{236, 196, 120, 255}, Color{120, 84, 36, 255});
            DrawEllipse((int)x + 11, (int)(-160 - hh * 0.22f), 6, 3, Color{40, 28, 16, 255});
        }
        DrawRectangle(-190, -170, 380, 170, Color{70, 40, 26, 255});           // console
        DrawRectangle(-190, -170, 380, 12, Color{110, 70, 42, 255});
        DrawRectangle(-150, -110, 300, 22, Color{236, 230, 214, 255});         // keys
        for (int k = 0; k < 30; k++) DrawRectangle(-146 + k * 10, -110, 4, 13, Color{30, 26, 24, 255});
        for (int k = 0; k < 2; k++) {                                            // candles
            float x = k ? 160.0f : -160.0f;
            DrawRectangle((int)x - 5, -200, 10, 32, Color{236, 228, 200, 255});
            DrawEllipse((int)x, -206, 4, 8 + sinf(t * 12 + k) * 1.5f, Color{255, 200, 110, 255});
        }
        DrawBrassPlate({-80, -40, 160, 30}, "SICK BAY", 17);
        if (playing)
            for (int n = 0; n < 4; n++) { // someone on leave is playing: notes drift up
                float ph = fmodf(t * 0.35f + n / 4.0f, 1.0f);
                Vector2 p{-120 + n * 80 + sinf(t * 2 + n) * 12, -420 - ph * 150};
                Color nc = Fade(Pal::Paper, 1 - ph);
                DrawCircleV(p, 8, nc);
                DrawLineEx({p.x + 7, p.y}, {p.x + 7, p.y - 28}, 2.5f, nc);
                DrawLineEx({p.x + 7, p.y - 28}, {p.x + 17, p.y - 21}, 2.5f, nc);
            }
    });
}

void DrawWardWall(float t) {
    (void)t;
    Billboard(WARD_X, 0, Z_BACK, [&] {
        Rectangle cab{-120, -470, 240, 300};
        DrawRectangleRec(cab, Color{220, 224, 218, 255});
        DrawRectangleLinesEx(cab, 6, Color{150, 156, 150, 255});
        const Color glass[4] = {{120, 70, 30, 255}, {50, 90, 150, 255}, {60, 130, 80, 255}, {160, 50, 50, 255}};
        for (int s = 0; s < 3; s++) {
            float y = cab.y + 90 + s * 80;
            for (int b = 0; b < 8; b++) {
                float hh = 36 + Hash01(s, b) * 24;
                DrawRectangle((int)(cab.x + 18 + b * 27), (int)(y - hh), 18, (int)hh, glass[(s + b) % 4]);
                DrawRectangle((int)(cab.x + 21 + b * 27), (int)(y - hh - 8), 12, 8, Color{240, 236, 226, 255});
            }
            DrawRectangle((int)cab.x + 8, (int)y, (int)cab.width - 16, 5, Color{160, 166, 160, 255});
        }
        DrawRectangleRec({cab.x + 10, cab.y + 10, cab.width - 20, cab.height - 20}, Fade(Color{180, 220, 230, 255}, 0.12f));
        DrawRectangle(-10, -540, 20, 56, Color{190, 40, 40, 255});
        DrawRectangle(-28, -522, 56, 20, Color{190, 40, 40, 255});
        DrawBrassPlate({-80, -140, 160, 30}, "THE WARD", 17);
    });
}

void DrawPainting(float X, float Y, float w, float h, int kind) {
    Billboard(X, Y, Z_BACK, [&] {
        DrawRectangle((int)(-w / 2 - 12), (int)(-h - 12), (int)(w + 24), (int)(h + 24), Color{170, 128, 60, 255});
        DrawRectangleLinesEx({-w / 2 - 12, -h - 12, w + 24, h + 24}, 4, Pal::BrassDk);
        if (kind == 0) { // a seascape
            DrawVGradient({-w / 2, -h, w, h * 0.55f}, Color{200, 180, 150, 255}, Color{220, 170, 120, 255});
            DrawVGradient({-w / 2, -h * 0.45f, w, h * 0.45f}, Color{60, 90, 110, 255}, Color{30, 50, 70, 255});
            DrawTri({-20, -h * 0.45f}, {30, -h * 0.45f}, {0, -h * 0.85f}, Color{230, 220, 200, 255});
        } else { // a portrait of a stern gentleman
            DrawVGradient({-w / 2, -h, w, h}, Color{60, 50, 40, 255}, Color{30, 24, 20, 255});
            DrawEllipse(0, (int)(-h * 0.25f), (int)(w * 0.32f), (int)(h * 0.3f), Color{30, 30, 40, 255});
            DrawCircleV({0, -h * 0.62f}, w * 0.16f, Color{210, 170, 140, 255});
            DrawCircleV({0, -h * 0.5f}, w * 0.12f, Color{220, 220, 220, 255});
        }
    });
}

void DrawHelmFurniture(float t) {
    Billboard(0, 0, 900, [&] {
        // the binnacle and pedestal
        DrawRectangleGradientH(-22, -190, 22, 190, Color{60, 38, 24, 255}, Color{130, 86, 54, 255});
        DrawRectangleGradientH(0, -190, 22, 190, Color{130, 86, 54, 255}, Color{50, 32, 20, 255});
        for (float y = -150; y < 0; y += 50) DrawPipeH(-26, 26, y, 5, Pal::Brass);
        DrawEllipse(0, -2, 60, 12, Color{50, 34, 22, 255});
        // the wheel
        Vector2 hc{0, -210};
        float rot = t * 0.12f;
        Color wood{128, 82, 46, 255}, woodDk{84, 52, 30, 255};
        for (int k = 0; k < 8; k++) {
            float a = rot + k * PI / 4;
            Vector2 in{hc.x + cosf(a) * 20, hc.y + sinf(a) * 20}, out{hc.x + cosf(a) * 132, hc.y + sinf(a) * 132};
            DrawLineEx(in, out, 11, woodDk);
            DrawLineEx(in, out, 6, wood);
            DrawCircleV(out, 11, woodDk);
            DrawCircleV({out.x - 2, out.y - 2}, 7, ColorBrightness(wood, 0.2f));
        }
        DrawRing(hc, 90, 108, 0, 360, 64, woodDk);
        DrawRing(hc, 92, 106, 0, 360, 64, wood);
        DrawRing(hc, 84, 90, 0, 360, 64, Pal::Brass);
        DrawCircleV(hc, 24, Pal::BrassDk);
        DrawCircleV(hc, 18, Pal::Brass);
        DrawBrassPlate({-70, -44, 140, 28}, "THE HELM", 15);
        // the chart table beside it
        DrawRectangle(-342, -120, 12, 120, Color{90, 58, 36, 255});
        DrawRectangle(-182, -120, 12, 120, Color{90, 58, 36, 255});
        DrawTiled(Tex::Wood, {-360, -136, 210, 20}, 0.5f, Color{140, 94, 60, 255});
        DrawRectangle(-344, -150, 176, 16, Color{230, 214, 176, 255});
        DrawGauge({-255, -200}, 30, 0.55f + 0.04f * sinf(t * 1.3f), Color{236, 228, 206, 255});
        DrawLineEx({-255, -170}, {-255, -136}, 5, Pal::BrassDk);
    });
}

void DrawPeriscope(float t) {
    (void)t;
    const float X = PERI_X, Z = PERI_Z;
    Billboard(X, 0, Z, [&] {
        DrawEllipse(0, -4, 70, 12, Color{120, 110, 80, 255});
        DrawPipeV(0, -RH, -330, 18, Color{150, 154, 148, 255});
        for (float y = -RH + 60; y < -340; y += 70) DrawPipeH(-24, 24, y, 6, Pal::Brass);
        DrawPipeV(0, -250, -8, 14, Color{130, 134, 128, 255});
        Rectangle hs{-46, -340, 92, 86};
        DrawRectangleRounded(hs, 0.25f, 8, Pal::BrassDk);
        DrawRectangleRounded({hs.x + 3, hs.y + 3, hs.width - 6, hs.height - 6}, 0.25f, 8, Pal::Brass);
        DrawCircleV({0, -300}, 20, Color{30, 28, 26, 255});
        DrawCircleV({0, -300}, 12, Color{20, 40, 50, 255});
        DrawCircleV({-4, -304}, 4, Color{150, 220, 230, 220});
        for (int k = -1; k <= 1; k += 2) {
            DrawPipeH(k < 0 ? -110 : 46, k < 0 ? -46 : 110, -286, 6, Color{80, 84, 82, 255});
            DrawRectangleRounded({k < 0 ? -124.0f : 98.0f, -298, 26, 24}, 0.5f, 6, Color{30, 26, 24, 255});
        }
        DrawBrassPlate({-70, -400, 140, 26}, "PERISCOPE", 14);
    });
}

void DrawChaise(float t, const std::vector<const Hero*>& resting) {
    Billboard(-430, 0, 1010, [&] {
        DrawRectangleRounded({-150, -90, 300, 60}, 0.4f, 8, Color{120, 40, 60, 255});   // seat
        DrawRectangleRounded({-160, -160, 70, 130}, 0.5f, 8, Color{130, 44, 66, 255});  // raised end
        for (int k = 0; k < 4; k++) DrawCircleV({-100 + k * 60.0f, -60}, 3, Color{210, 170, 90, 255});
        DrawRectangle(-140, -30, 14, 30, Color{70, 44, 28, 255});
        DrawRectangle(126, -30, 14, 30, Color{70, 44, 28, 255});
        for (size_t i = 0; i < resting.size() && i < 2; i++) { // whoever is on leave, dozing under a blanket
            float x = -100 + i * 150.0f;
            DrawCircleV({x, -112}, 18, Color{226, 188, 156, 255});
            DrawCircleSector({x, -112}, 19, 150, 330, 12, Color{80, 56, 40, 255});
            DrawRectangleRounded({x + 8, -124, 120, 40}, 0.6f, 6, Color{70, 100, 140, 255});
            for (int z = 0; z < 3; z++) {
                float ph = fmodf(t * 0.5f + z * 0.33f + i * 0.5f, 1.0f);
                TxtBold("z", x + ph * 40 + z * 6, -150 - ph * 90, 20 + z * 5, Fade(Pal::Paper, 1 - ph));
            }
        }
    });
}

void DrawOperatingTable(float t) {
    Billboard(430, 0, 1010, [&] {
        DrawRectangle(-20, -110, 40, 110, Color{170, 176, 174, 255});
        DrawEllipse(0, -4, 70, 12, Color{110, 116, 114, 255});
        DrawRectangleRounded({-150, -134, 300, 26}, 0.4f, 6, Color{238, 238, 232, 255});
        DrawRectangle(-150, -112, 300, 34, Color{214, 218, 214, 255});
        DrawEllipse(-120, -140, 30, 10, Color{250, 250, 246, 255});
        Vector2 lc{0, -470}; // the surgical lamp
        DrawLineEx({0, -RH}, {0, -500}, 5, Color{150, 154, 150, 255});
        DrawCircleSector(lc, 56, 180, 360, 24, Color{190, 196, 192, 255});
        DrawEllipse(0, -470, 56, 10, Color{255, 250, 232, 255});
    });
    (void)t;
}

void DrawChandelier(float t) {
    Billboard(0, 470, 760, [&] {
        DrawLineEx({0, -130}, {0, 0}, 4, Pal::BrassDk);
        DrawEllipse(0, 0, 150, 26, Pal::BrassDk);
        DrawEllipse(0, -4, 140, 20, Pal::Brass);
        for (int k = 0; k < 9; k++) {
            float a = k * 2 * PI / 9 + 0.2f;
            Vector2 p{cosf(a) * 132, sinf(a) * 20 - 14};
            DrawRectangle((int)p.x - 4, (int)p.y - 18, 8, 18, Color{236, 228, 200, 255});
            DrawEllipse((int)p.x, (int)p.y - 24, 4, 8 + sinf(t * 11 + k) * 1.5f, Color{255, 210, 130, 255});
        }
        for (int k = 0; k < 12; k++) DrawCircleV({-110 + k * 20.0f, 18 + fabsf(k - 5.5f) * 2}, 4, Color{200, 230, 240, 200}); // crystal drops
    });
}

// The ship's cat: a ginger tabby. It wanders, sits and watches, and purrs when you click it.
// Draws the cat standing at `feet` (k = size), and returns where its head ended up on screen.
Vector2 DrawCatAt(Vector2 feet, float k, bool right, bool sitting, bool purring, float phase, float t, float seed) {
    Vector2 o = FigureFeet();
    float f = right ? 1.0f : -1.0f;
    float vib = purring ? sinf(t * 70) * 0.3f : 0; // the whole cat hums
    auto P = [&](float dx, float dy) { return Vector2{o.x + (dx + vib) * f * k, o.y + dy * k}; };
    Color fur{206, 124, 56, 255}, stripe{140, 68, 30, 255}, cream{240, 220, 186, 255}, pink{226, 140, 140, 255};
    DrawShadowBlob(feet, (sitting ? 20 : 26) * k);
    BeginFigure();
    Vector2 hc;
    float look = 0; // the head tilts now and then while it sits and watches
    if (!sitting) {
        float ph = phase;
        Vector2 prev = P(-16, -18); // tail held high, curling at the tip
        for (int i = 1; i <= 7; i++) {
            Vector2 p = P(-17 - i * 1.6f + sinf(t * 2.2f + i * 0.5f) * i * 0.35f, -18 - i * 4.0f + (i > 5 ? (i - 5) * 2.5f : 0));
            ShadeLimb(prev, p, (2.8f - i * 0.12f) * k, (2.7f - i * 0.12f) * k, i == 7 ? stripe : fur);
            prev = p;
        }
        for (int leg = 0; leg < 2; leg++) { // far legs, in shadow
            float lx = leg ? 10 : -11, sw = sinf(ph + PI + leg * PI);
            ShadeLimb(P(lx, -14), P(lx + sw * 5, -1 + std::min(0.0f, cosf(ph + PI + leg * PI)) * 2.5f), 2.6f * k, 1.9f * k, Tone(fur, -0.35f));
        }
        float bob = fabsf(sinf(ph)) * 0.8f;
        ShadeLimb(P(-12, -17 - bob), P(10, -18 - bob), 8.5f * k, 7.8f * k, fur);
        ShadeLimb(P(-6, -12 - bob), P(8, -12 - bob), 4 * k, 4.5f * k, Tone(cream, -0.1f)); // pale belly
        for (int i = 0; i < 5; i++) DrawLineEx(P(-10 + i * 4.6f, -25.5f - bob), P(-8.5f + i * 4.6f, -16 - bob), 1.7f * k, stripe);
        for (int leg = 0; leg < 2; leg++) { // near legs
            float lx = leg ? 11 : -10, sw = sinf(ph + leg * PI);
            ShadeLimb(P(lx, -14 - bob), P(lx + sw * 5, -1 + std::min(0.0f, cosf(ph + leg * PI)) * 2.5f), 2.9f * k, 2.1f * k, fur);
            ShadeBall(P(lx + sw * 5 + 1, -1 + std::min(0.0f, cosf(ph + leg * PI)) * 2.5f), 2.2f * k, cream);
        }
        hc = P(19, -25 - bob);
    } else {
        look = purring ? 0.25f : sinf(t * 0.5f + seed) * 0.5f;
        Vector2 prev = P(-12, -3); // tail wrapped round the paws, the tip flicking
        for (int i = 1; i <= 7; i++) {
            float tip = i > 4 ? sinf(t * (purring ? 1.2f : 2.6f)) * (i - 4) * 1.2f : 0;
            Vector2 p = P(-12 + i * 3.4f, -2.5f - sinf(i * 0.45f) * 1.2f - tip);
            ShadeLimb(prev, p, 2.7f * k, 2.6f * k, i == 7 ? stripe : fur);
            prev = p;
        }
        ShadeBall(P(-6, -10), 10 * k, fur); // haunch
        for (int i = 0; i < 3; i++) DrawLineEx(P(-13 + i * 4.0f, -17 + i * 0.5f), P(-9 + i * 4.5f, -6), 1.6f * k, stripe);
        ShadeLimb(P(-5, -11), P(4, -23), 8.8f * k, 7 * k, fur); // body rising to the chest
        ShadeBall(P(7, -18), 4.6f * k, cream);                   // white bib
        ShadeLimb(P(3, -18), P(3.5f, -1), 2.5f * k, 2.1f * k, Tone(fur, -0.3f));
        ShadeLimb(P(7, -17), P(7.5f, -1), 2.6f * k, 2.2f * k, fur);
        ShadeBall(P(8, -1.5f), 2.3f * k, cream);
        hc = P(6 + look, -32 + (purring ? 1 : 0));
    }
    // the head: a round skull, ears, a pale muzzle, stripes on the brow
    float hk = 7.6f * k;
    for (int e = -1; e <= 1; e += 2) {
        Vector2 b1{hc.x + (e * 5.5f - 2.5f) * k, hc.y - 4 * k}, b2{hc.x + (e * 5.5f + 2.5f) * k, hc.y - 4.5f * k}, tip{hc.x + e * 6.5f * k, hc.y - 13 * k};
        DrawTri(b1, b2, tip, Tone(fur, -0.1f));
        DrawTri({b1.x + 1.3f * k, b1.y}, {b2.x - 1.3f * k, b2.y}, {tip.x, tip.y + 3.5f * k}, pink);
    }
    ShadeBall(hc, hk, fur);
    for (int i = -1; i <= 1; i++) DrawLineEx({hc.x + i * 2.2f * k, hc.y - 7 * k}, {hc.x + i * 1.6f * k, hc.y - 3.5f * k}, 1.3f * k, stripe);
    Vector2 mz{hc.x + f * 2.5f * k, hc.y + 3 * k};
    ShadeBall(mz, 3.6f * k, cream);
    DrawTri({mz.x - 1.2f * k, mz.y - 1.6f * k}, {mz.x + 1.2f * k, mz.y - 1.6f * k}, {mz.x, mz.y - 0.2f * k}, pink);
    float blink = fmodf(t * 0.37f + seed * 0.01f, 1.0f) < 0.03f;
    for (int e = -1; e <= 1; e += 2) {
        Vector2 ec{hc.x + f * 1.5f * k + e * 3.2f * k, hc.y - 1 * k};
        if (purring || blink) DrawLineEx({ec.x - 1.6f * k, ec.y + 0.3f * k}, {ec.x + 1.6f * k, ec.y + 0.3f * k}, 1.0f * k, Color{60, 34, 20, 255});
        else {
            DrawEllipse((int)ec.x, (int)ec.y, 1.7f * k, 1.5f * k, Color{150, 200, 80, 255});
            DrawEllipse((int)ec.x, (int)ec.y, 0.45f * k, 1.3f * k, Color{20, 16, 12, 255});
        }
    }
    for (int w = -1; w <= 1; w += 2) // whiskers
        for (int i = 0; i < 2; i++)
            DrawLineEx({mz.x + w * 1.5f * k, mz.y + i * 0.8f * k}, {mz.x + w * 9 * k, mz.y - 1 * k + i * 2.2f * k}, 0.4f * k, Fade(WHITE, 0.7f));
    EndFigure(feet);
    return {feet.x + (hc.x - o.x), feet.y + (hc.y - o.y)};
}

void DrawCat(float t) {
    float Z = cat.pos.y, k = Px(Z) * 2.25f;
    Vector2 feet = Proj(cat.pos.x, 0, Z);
    catRect = {feet.x - 28 * k, feet.y - 46 * k, 56 * k, 48 * k};
    catHead = DrawCatAt(feet, k, cat.right, cat.wait > 0, cat.purr > 0, cat.phase, t, cat.pos.x);
}

// Hearts rise while the cat purrs (drawn after the ink pass, so they stay soft).
void DrawPurrHearts(float t) {
    if (cat.purr <= 0) return;
    for (int i = 0; i < 3; i++) {
        float ph = fmodf(t * 0.6f + i / 3.0f, 1.0f), a = std::min(1.0f, cat.purr) * (1 - ph) * std::min(1.0f, ph * 5);
        Vector2 p{catHead.x + sinf(ph * 6 + i * 2) * 8, catHead.y - 14 - ph * 46};
        float r = 3.2f + ph * 2;
        Color c = Fade(Color{255, 120, 140, 255}, a);
        DrawCircleV({p.x - r * 0.7f, p.y}, r, c);
        DrawCircleV({p.x + r * 0.7f, p.y}, r, c);
        DrawTri({p.x - r * 1.6f, p.y + r * 0.4f}, {p.x + r * 1.6f, p.y + r * 0.4f}, {p.x, p.y + r * 2.4f}, c);
    }
    float w = 1 - fmodf(t * 1.5f, 1.0f); // a little "prrr" beside it
    TxtBold("prrr", catHead.x + 14, catHead.y - 30 + w * 4, 14, Fade(Pal::Paper, std::min(1.0f, cat.purr) * 0.85f));
}

// A purr, made from scratch: low filtered noise, pulsed about 25 times a second, louder on the out-breath.
void PlayPurr() {
    static Sound purr{};
    static bool loaded = false;
    if (!IsAudioDeviceReady()) return;
    if (!loaded) {
        const int rate = 22050;
        const float dur = 3.0f;
        int n = (int)(rate * dur);
        std::vector<float> v(n);
        float lp = 0, lp2 = 0, peak = 0.001f;
        unsigned seed = 12345;
        for (int i = 0; i < n; i++) {
            float tt = (float)i / rate, cyc = fmodf(tt, 1.1f);
            bool out = cyc < 0.62f;
            float breath = out ? sinf(cyc / 0.62f * PI) : 0.55f * sinf((cyc - 0.62f) / 0.48f * PI);
            float hz = out ? 26.0f : 22.0f, pp = fmodf(tt * hz, 1.0f), pulse = expf(-pp * 7);
            seed = seed * 1664525u + 1013904223u;
            float noise = ((seed >> 9) & 0xffff) / 32768.0f - 1;
            lp += (noise - lp) * 0.07f;
            lp2 += (lp - lp2) * 0.07f;
            float fade = std::min(1.0f, tt / 0.15f) * std::min(1.0f, (dur - tt) / 0.5f);
            v[i] = (lp2 + 0.02f * sinf(2 * PI * hz * 2 * tt)) * pulse * breath * fade;
            peak = std::max(peak, fabsf(v[i]));
        }
        short* data = (short*)MemAlloc(n * sizeof(short));
        for (int i = 0; i < n; i++) data[i] = (short)(v[i] / peak * 0.8f * 32767);
        Wave w{(unsigned)n, rate, 16, 1, data};
        purr = LoadSoundFromWave(w);
        UnloadWave(w);
        loaded = true;
    }
    if (!IsSoundPlaying(purr)) PlaySound(purr);
}

// ---------------------------------------------------------------- pipes along the ceiling
// A pipe run in room space, shaded like a lit cylinder. Drawn from `a` to `b`, so give the far end first.
void Pipe3D(Vector3 a, Vector3 b, float R, Color c, int n = 14) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int i = 0; i < n; i++) {
        Vector3 p0 = Mix(a, b, (float)i / n), p1 = Mix(a, b, (float)(i + 1) / n);
        Vector2 s0 = Proj(p0), s1 = Proj(p1);
        float w0 = R * Px(p0.z), w1 = R * Px(p1.z), dx = s1.x - s0.x, dy = s1.y - s0.y, len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f) continue;
        Vector2 nn{-dy / len, dx / len};
        if (nn.y > 0) nn = {-nn.x, -nn.y}; // +1 across the pipe is its top, facing the lamps above
        const int S = 8;
        for (int j = 0; j < S; j++) {
            float u0 = -1 + 2.0f * j / S, u1 = -1 + 2.0f * (j + 1) / S;
            auto tone = [&](float u) {
                float nz = sqrtf(std::max(0.0f, 1 - u * u));
                return Tone(c, 0.3f * u + 0.25f * nz - 0.25f - (1 - nz) * 0.4f + 0.6f * expf(-(u - 0.5f) * (u - 0.5f) / 0.02f));
            };
            Vector2 A0{s0.x + nn.x * w0 * u0, s0.y + nn.y * w0 * u0}, A1{s0.x + nn.x * w0 * u1, s0.y + nn.y * w0 * u1};
            Vector2 B0{s1.x + nn.x * w1 * u0, s1.y + nn.y * w1 * u0}, B1{s1.x + nn.x * w1 * u1, s1.y + nn.y * w1 * u1};
            Color c0 = tone((u0 + u1) / 2);
            DrawTri(A0, A1, B1, c0);
            DrawTri(A0, B1, B0, c0);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

// A collar where two lengths of pipe are bolted together.
void Flange3D(Vector3 p, Vector3 dir, float R, Color c) {
    Pipe3D({p.x - dir.x * 5, p.y - dir.y * 5, p.z - dir.z * 5}, {p.x + dir.x * 5, p.y + dir.y * 5, p.z + dir.z * 5}, R * 1.45f, ColorBrightness(c, -0.25f), 1);
}

void Hanger(float X, float Y, float Z) { DrawLineEx(Proj(X, Y, Z), Proj(X, RH, Z), std::max(1.0f, 4 * Px(Z)), Color{50, 44, 38, 255}); }

constexpr float CROSS_Z = 520, CROSS_Y = 500;

// The long runs down both sides of the ceiling, drawn with the room (behind the furniture).
void DrawWallPipes(float t) {
    (void)t;
    for (int s = -1; s <= 1; s += 2) {
        float XA = s * (RW - 48), XB = s * (RW - 100);
        Color big = s < 0 ? Pal::Copper : Pal::Brass, small{128, 134, 130, 255};
        for (float z = 1100; z > 340; z -= 150) { Hanger(XA, RH - 45, z); Hanger(XB, RH - 30, z + 60); }
        Pipe3D({XB, RH - 30, Z_BACK}, {XB, RH - 30, 330}, 8, small);
        Pipe3D({XA, RH - 45, Z_BACK}, {XA, RH - 45, 330}, 17, big);
        for (float z = 1000; z > 380; z -= 200) Flange3D({XA, RH - 45, z}, {0, 0, 1}, 17, big);
        // a drop into the back wall, where the pipe turns down behind the panelling
        Pipe3D({XA, RH - 45, Z_BACK - 10}, {XA, RH - 150, Z_BACK - 10}, 17, big, 2);
        Flange3D({XA, RH - 150, Z_BACK - 10}, {0, 1, 0}, 17, big);
    }
}

// The pipe that crosses the room near the ceiling, in front of everything, with a valve and a gauge.
void DrawCrossPipe(float t) {
    for (float x = -600; x <= 600; x += 200) Hanger(x, CROSS_Y + 13, CROSS_Z);
    Pipe3D({-RW, CROSS_Y, CROSS_Z}, {RW, CROSS_Y, CROSS_Z}, 13, Pal::Copper, 24);
    Pipe3D({-RW, CROSS_Y + 34, CROSS_Z + 30}, {RW, CROSS_Y + 34, CROSS_Z + 30}, 6, Color{128, 134, 130, 255}, 24);
    for (float x : {-420.0f, 90.0f, 460.0f}) Flange3D({x, CROSS_Y, CROSS_Z}, {1, 0, 0}, 13, Pal::Copper);
    // a red handwheel on a stem
    Billboard(-240, CROSS_Y + 12, CROSS_Z - 20, [&] {
        DrawLineEx({0, 0}, {0, -26}, 6, Pal::BrassDk);
        Vector2 c{0, -30};
        for (int k = 0; k < 5; k++) {
            float a = k * 2 * PI / 5 + 0.3f;
            DrawLineEx(c, {c.x + cosf(a) * 22, c.y + sinf(a) * 7}, 3, Color{130, 30, 26, 255});
        }
        DrawEllipse((int)c.x, (int)c.y, 24, 8, Color{150, 34, 30, 255});
        DrawEllipse((int)c.x, (int)c.y - 1, 19, 5, Color{60, 20, 18, 255});
        DrawCircleV(c, 4, Pal::Brass);
    });
    // a pressure gauge hanging off a tee: the needle never quite sits still
    Billboard(300, CROSS_Y - 13, CROSS_Z - 16, [&] {
        DrawLineEx({0, 0}, {0, 18}, 7, Pal::BrassDk);
        float needle = 0.58f + 0.04f * sinf(t * 7.3f) * sinf(t * 1.1f) + (fmodf(t, 9) < 0.5f ? 0.12f : 0);
        DrawGauge({0, 40}, 20, needle, Color{236, 228, 206, 255});
    });
}

// Wisps from a leaky flange on the cross pipe, and from the wall run now and then.
void DrawCeilingSteam(float t) {
    struct Leak { Vector3 at; float rate, drift; };
    const Leak leaks[] = {{{90, CROSS_Y, CROSS_Z}, 1.0f, 1}, {{-(RW - 48), RH - 45, 800}, 0.7f, 1}, {{RW - 48, RH - 45, 600}, 0.8f, -1}};
    for (int L = 0; L < 3; L++) {
        const Leak& lk = leaks[L];
        Vector2 o = Proj(lk.at);
        float k = Px(lk.at.z), gust = 0.45f + 0.55f * std::max(0.0f, sinf(t * 0.6f * lk.rate + L * 2));
        for (int i = 0; i < 9; i++) {
            float ph = fmodf(t * 0.45f * lk.rate + i / 9.0f, 1.0f);
            Vector2 p{o.x + lk.drift * ph * 70 * k + sinf(t + i) * 6 * k, o.y + ph * 50 * k - ph * ph * 30 * k};
            DrawCircleV(p, (6 + ph * 26) * k, Fade(Color{226, 230, 228, 255}, 0.16f * (1 - ph) * gust));
        }
    }
}
// ---------------------------------------------------------------- lighting
void DrawSalonLighting(float t, int hovered) {
    LightsBegin(Color{66, 70, 78, 255});
    Color warm{255, 206, 140, 255}, sea{70, 150, 170, 255};
    float flick = 0.93f + 0.07f * sinf(t * 9) * sinf(t * 3.1f);
    Vector2 ch = Proj(0, 450, 760);
    AddLight(ch, 620, warm, 0.8f * flick);
    AddCone(ch, PI / 2, 0.9f, 460, Color{150, 118, 78, 255});
    AddLight(Proj(0, 330, Z_BACK), 330, sea, 0.95f);
    AddLight(Proj(0, 60, 1000), 260, sea, 0.45f);
    AddLight({CX, 640}, 520, warm, 0.4f); // lamplight spilling across the near floor
    for (int s = -1; s <= 1; s += 2) // wall sconces
        for (float z : {720.0f, 1100.0f}) AddLight(Proj(s * (RW - 10), 400, z), 200 * Px(z) * 2.2f, warm, 0.7f * flick);
    AddLight(Proj(-520, 190, Z_BACK), 150, warm, 0.8f);          // organ candles
    AddLight(Proj(RW, 440, 620), 180, Color{80, 255, 140, 255}, 0.5f); // the radar's glow
    AddCone(Proj(430, 470, 1010), PI / 2, 0.45f, 180, Color{255, 250, 232, 255}); // surgical lamp
    if (fmodf(t, 6) < 2.2f) AddLight(Proj(RW, 90, 1010), 120, Color{255, 170, 80, 255}, 0.7f);
    if (hovered >= 0) {
        Rectangle r = stationRect[hovered];
        AddLight({r.x + r.width / 2, r.y + r.height / 2}, std::max(r.width, r.height) * 0.8f + 60, Color{255, 230, 190, 255}, 0.4f);
    }
    LightsEnd();
    Glow(ch, 80, Color{255, 200, 120, 90});
    for (int s = -1; s <= 1; s += 2)
        for (float z : {720.0f, 1100.0f}) Glow(Proj(s * (RW - 10), 400, z), 24 * Px(z) * 2, Color{255, 200, 120, 150});
}

// Dust in the chandelier light: drawn after the ink pass, or every mote gets a dark ring (the "black dust").
void DrawDustMotes(float t) {
    Vector2 ch = Proj(0, 450, 760);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 50; k++) {
        Vector2 p{ch.x + sinf(t * 0.2f + k * 1.7f) * 260, ch.y + 30 + fmodf(k * 53 + t * (5 + k % 4), 330.0f)};
        DrawCircleV(p, 1.2f, Color{255, 230, 180, 60});
    }
    EndBlendMode();
}

// ---------------------------------------------------------------- HUD
void DrawSalonHud(Game& g, int hovered, const std::string& hint, bool active) {
    DrawVGradient({0, 0, (float)SCREEN_W, 46}, Color{8, 12, 14, 235}, Color{16, 22, 26, 215});
    DrawRectangle(0, 44, SCREEN_W, 2, Pal::BrassDk);
    TxtShadow("THE NAUTILUS", 20, 9, 28, Pal::Brass, true);
    Txt(TextFormat("The grand salon   -   Depth %.0f fathoms", 212 + sinf(g.time * 0.05f) * 3), 262, 16, 15, Color{180, 190, 186, 255});
    DrawCircle(890, 23, 10, Pal::Brass);
    DrawCircle(887, 20, 4, Color{255, 240, 190, 255});
    TxtBold(TextFormat("%d gold", g.gold), 908, 12, 21, Pal::Brass);
    Txt(TextFormat("Batteries %d", g.batteries), 1030, 15, 17, Pal::Paper);
    Txt(TextFormat("Relics %d", (int)g.relicStorage.size()), 1160, 15, 17, Pal::Paper);

    float hw = (float)MeasureTxt(hint, 19);
    DrawRectangleRounded({SCREEN_W / 2 - hw / 2 - 16, HUD_Y - 40, hw + 32, 32}, 0.5f, 8, Color{8, 12, 14, 180});
    TxtShadow(hint, SCREEN_W / 2 - hw / 2, HUD_Y - 34, 19, active ? Pal::Paper : Color{200, 196, 180, 200});

    DrawVGradient({0, HUD_Y, (float)SCREEN_W, SCREEN_H - HUD_Y}, Color{12, 18, 22, 235}, Color{6, 10, 12, 250});
    DrawRectangle(0, (int)HUD_Y, SCREEN_W, 2, Pal::BrassDk);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        Rectangle c{250 + k * 202.0f, HUD_Y + 8, 194, 52};
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
    (void)hovered;
}
}  // namespace

// For the sprite sheet: the ship's hands in their work poses, walking, and the cat.
void DrawSalonSpritePage(float t) {
    TxtBold("The Nautilus's crew (they never go on expeditions) and Barnacle the cat", 30, 16, 24, Pal::Brass);
    for (int i = 0; i < NPC_COUNT; i++) {
        const Hero& h = NpcHero(i);
        float x = 110 + i * 200.0f;
        Walker w{i, {0, 0}, {0, 0}, 5, 0, true, true};
        TxtBold(NPCS[i].role, x - MeasureTxt(NPCS[i].role, 17, true) / 2.0f, 60, 17, Pal::Paper);
        DrawShadowBlob({x - 40, 280}, 26);
        DrawCrewFigureInked(h, {x - 40, 280}, 1.1f, true, 0, t, WorkPose(w, t));
        DrawShadowBlob({x + 40, 280}, 26);
        DrawCrewFigureInked(h, {x + 40, 280}, 1.1f, false, 1.4f, t, Pose{});
    }
    Txt("At work (left) and walking (right)", 30, 300, 16, Color{190, 190, 180, 255});
    const char* modes[3] = {"Walking", "Sitting, watching", "Purring (click it)"};
    for (int m = 0; m < 3; m++) {
        float x = 240 + m * 360.0f;
        TxtBold(modes[m], x - MeasureTxt(modes[m], 18, true) / 2.0f, 380, 18, Pal::Paper);
        DrawShadowBlob({x, 600}, 60);
        Vector2 head = DrawCatAt({x, 600}, 3.6f, true, m > 0, m == 2, 1.2f, t, 40);
        if (m == 2) { cat.purr = 3; catHead = head; DrawPurrHearts(t); cat.purr = 0; }
    }
}

// For screenshots: put the cat front and centre, purring.
void DebugPetCat() { cat.pos = cat.target = {-150, 640}; cat.purr = 3; cat.wait = 4; cat.right = true; }

// ============================================================ the salon scene
void SceneHub(Game& g) {
    float dt = GetFrameTime(), t = g.time;
    SetPost(0.5f, 0.03f, 0.4f);
    Vector2 m = GetMousePosition();
    bool mouseInRoom = m.y > 46 && m.y < HUD_Y;
    UpdateLife(g, dt);
    std::vector<const Hero*> resting;
    for (auto& h : g.roster) if (h.onLeave > 0) resting.push_back(&h);

    // --- the room, back to front
    DrawOcean(t);
    DrawShell(t);
    DrawGreatWindow(t);
    DrawPainting(-520, 590, 130, 60, 0);
    DrawPainting(WARD_X, 590, 120, 80, 1);
    DrawOrgan(t, !resting.empty());
    DrawWardWall(t);
    Vector2 organBase = Proj(-520, 0, Z_BACK), wardBase = Proj(WARD_X, 0, Z_BACK);
    float kb = Px(Z_BACK);
    stationRect[ST_CREW] = PaintWall(-RW, 540, 700, 0, 240, 645, [&](float w, float h) { ArtCrewDoor(w, h, t); });
    stationRect[ST_LIBRARY] = PaintWall(-RW, 740, 1080, 0, 510, 750, [&](float w, float h) { ArtLibrary(w, h, t); });
    stationRect[ST_RADAR] = PaintWall(RW, 540, 700, 0, 240, 645, [&](float w, float h) { ArtRadar(w, h, t); });
    stationRect[ST_WORKSHOP] = PaintWall(RW, 740, 1080, 0, 510, 645, [&](float w, float h) { ArtWorkshop(w, h, t); });
    DrawWallPipes(t);

    // furniture and people on the floor, far to near
    struct Item { float z; std::function<void()> draw; };
    std::vector<Item> items;
    items.push_back({1010, [&] { DrawChaise(t, resting); }});
    items.push_back({1010, [&] { DrawOperatingTable(t); }});
    items.push_back({900, [&] { DrawHelmFurniture(t); }});
    items.push_back({PERI_Z, [&] { DrawPeriscope(t); }});
    items.push_back({cat.pos.y, [&] { DrawCat(t); }});
    struct Person { Walker* w; const Hero* h; Vector2 feet; float s; Rectangle r; };
    std::vector<Person> people;
    for (auto& w : walkers) {
        const Hero* h = &NpcHero(w.id);
        float s = CREW_H / 165.0f * Px(w.pos.y);
        Vector2 feet = Proj(w.pos.x, 0, w.pos.y);
        people.push_back({&w, h, feet, s, {feet.x - 22 * s, feet.y - 165 * s, 44 * s, 165 * s}});
    }
    for (auto& p : people)
        items.push_back({p.w->pos.y, [&, pp = &p] {
            DrawShadowBlob(pp->feet, 30 * pp->s);
            DrawCrewFigureInked(*pp->h, pp->feet, pp->s, pp->w->right, pp->w->phase, t, WorkPose(*pp->w, t));
        }});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.z > b.z; });
    for (auto& it : items) it.draw();
    DrawChandelier(t);
    DrawCrossPipe(t);

    // where the furniture stations sit on screen (for hovering)
    auto around = [&](float X, float Z, float w, float hgt) {
        Vector2 a = Proj(X - w / 2, hgt, Z), b = Proj(X + w / 2, 0, Z);
        return Rectangle{a.x, a.y, b.x - a.x, b.y - a.y};
    };
    stationRect[ST_HELM] = around(-90, 900, 480, 360);
    stationRect[ST_PERISCOPE] = around(PERI_X, PERI_Z, 140, 420);
    Rectangle organR{organBase.x - 200 * kb, organBase.y - 560 * kb, 400 * kb, 560 * kb}, chaise = around(-430, 1010, 330, 170);
    stationRect[ST_SICKBAY] = {std::min(organR.x, chaise.x), organR.y, std::max(organR.x + organR.width, chaise.x + chaise.width) - std::min(organR.x, chaise.x),
                               chaise.y + chaise.height - organR.y};
    Rectangle wardR{wardBase.x - 130 * kb, wardBase.y - 560 * kb, 260 * kb, 560 * kb}, table = around(430, 1010, 320, 170);
    stationRect[ST_WARD] = {std::min(wardR.x, table.x), wardR.y, std::max(wardR.x + wardR.width, table.x + table.width) - std::min(wardR.x, table.x),
                            table.y + table.height - wardR.y};

    // --- what's under the mouse: people first, then furniture, then the walls
    bool hovCat = mouseInRoom && CheckCollisionPointRec(m, catRect);
    const Person* hovPerson = nullptr;
    if (mouseInRoom && !hovCat)
        for (auto& p : people) if (CheckCollisionPointRec(m, p.r) && (!hovPerson || p.w->pos.y < hovPerson->w->pos.y)) hovPerson = &p;
    int hovered = -1;
    const int order[ST_COUNT] = {ST_HELM, ST_PERISCOPE, ST_SICKBAY, ST_WARD, ST_CREW, ST_LIBRARY, ST_RADAR, ST_WORKSHOP};
    if (mouseInRoom && !hovPerson && !hovCat)
        for (int i : order) if (CheckCollisionPointRec(m, stationRect[i])) { hovered = i; break; }

    DrawSalonLighting(t, hovered);
    for (auto& p : people)
        if (p.h->rattled) Glow({p.feet.x, p.r.y - 6}, 20 + sinf(t * 5) * 3, Fade(Pal::Stress, 0.5f));
    if (hovered >= 0) {
        Rectangle r = stationRect[hovered];
        float pulse = 0.55f + 0.45f * sinf(t * 5);
        DrawRectangleRoundedLinesEx({r.x - 8, r.y - 8, r.width + 16, r.height + 16}, 0.06f, 6, 3, Fade(Color{255, 214, 150, 255}, pulse));
    }
    InkPass(1.0f, 1.0f);
    DrawDustMotes(t);
    DrawCeilingSteam(t);
    DrawPurrHearts(t);

    // --- labels, hints, HUD
    std::string hint = "Every station in the salon can be clicked. Your expedition crew wait in Crew Quarters.";
    if (hovCat) {
        std::string label = "Barnacle, the ship's cat";
        float w = (float)MeasureTxt(label, 16, true);
        Rectangle r{catRect.x + catRect.width / 2 - w / 2 - 10, catRect.y - 30, w + 20, 26};
        DrawRectangleRounded(r, 0.4f, 6, Color{12, 18, 22, 230});
        DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Pal::BrassDk);
        TxtBold(label, r.x + 10, r.y + 4, 16, Pal::Paper);
        hint = cat.purr > 0 ? "Barnacle leans into your hand and purrs like a donkey engine." : "Click to give the cat a scratch behind the ears.";
    } else if (hovPerson) {
        const Npc& n = NPCS[hovPerson->w->id];
        std::string label = std::string(n.role) + " of the Nautilus";
        float w = (float)MeasureTxt(label, 16, true);
        Rectangle r{hovPerson->feet.x - w / 2 - 10, hovPerson->r.y - 34, w + 20, 26};
        DrawRectangleRounded(r, 0.4f, 6, Color{12, 18, 22, 230});
        DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Pal::BrassDk);
        TxtBold(label, r.x + 10, r.y + 4, 16, Pal::Paper);
        hint = "One of the ship's own hands. They keep the Nautilus running; they don't go on expeditions.";
    } else if (hovered >= 0) {
        hint = std::string(STATIONS[hovered].name) + ":  " + STATIONS[hovered].hint;
    }
    DrawSalonHud(g, hovered, hint, hovered >= 0 || hovPerson || hovCat);

    // New game, with a second click to confirm
    static float armed = 0;
    armed = std::max(0.0f, armed - dt);
    if (Button({18, HUD_Y + 12, 210, 44}, armed > 0 ? "Click again to confirm" : "Start a new game", true, 17)) {
        if (armed > 0) {
            DeleteSave();
            g = Game{};
            InitGame(g);
            walkers.clear();
            Toast(g, "A fresh crew steps aboard the Nautilus.");
            armed = 0;
            return;
        }
        armed = 3;
    }

    // --- clicks
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovCat) {
        if (cat.purr <= 0) PlayPurr();
        cat.purr = 3.2f;
        cat.wait = std::max(cat.wait, 3.7f);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInRoom && !hovPerson && !hovCat && hovered >= 0) {
        g.scene = STATIONS[hovered].target;
        if (g.scene == Scene::Crew && !FindHero(g, g.selectedHero) && !g.roster.empty()) g.selectedHero = g.roster[0].id;
    }
}
