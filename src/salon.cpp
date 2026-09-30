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
#include "sound.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
// ---------------------------------------------------------------- the room and the camera
// The room is framed left of centre: the roster stands along the right edge, as in Darkest Dungeon's hamlet.
constexpr float FOCAL = 440, CX = 560, CY = 320, EYE = 230;
constexpr float RW = 720, RH = 600, Z_NEAR = 290, Z_BACK = 1150;
constexpr float HUD_Y = 654, CREW_H = 190; // a crew member's height in room units
constexpr float PERI_X = 262, PERI_Z = 1060;  // the periscope rises at the seam between the great window's bay and the Ward's
constexpr float ARC_X = 340, ARC_Z = 560;     // the Deep Arcade cabinet, in the near right corner of the square (the card table faces it on the left)
constexpr float HATCH_Z = 470;               // the Study hatch, set into the deck, front and centre
constexpr float ROSTER_X = 1128;             // the roster column along the right edge
// The camera drifts a little opposite the mouse (up to ~1.5% of the screen), so the room has depth without moving.
float gCamX = 0, gCamY = 0;
constexpr float WARD_X = 420, ORGAN_X = -405, ALCOVE_Z = 1070; // the Ward's cabinet and the Sick Bay's organ, each in its bay of the apse

Vector2 Proj(float X, float Y, float Z) { return {CX + (X - gCamX) * FOCAL / Z, CY - (Y - EYE - gCamY) * FOCAL / Z}; }
Vector2 Proj(Vector3 p) { return Proj(p.x, p.y, p.z); }
float Px(float Z) { return FOCAL / Z; }
Vector3 Mix(Vector3 a, Vector3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// The back of the room: an apse of five bays (room X, Z), left to right as seen: the Library, the Sick Bay, the great
// window (the Helm before it), the Ward, the Workshop. A short stretch of straight wall at each edge holds Crew Quarters
// (left) and the Radar Room (right). The floor between is the open square, as in Darkest Dungeon's hamlet.
const Vector2 APSE[6] = {{-RW, 700}, {-560, 1000}, {-250, Z_BACK}, {250, Z_BACK}, {560, 1000}, {RW, 700}};
float ApseHalfW(float z) { return z <= 700 ? RW : z <= 1000 ? RW - (z - 700) * 160 / 300.0f : 560 - (z - 1000) * 310 / 150.0f; }
bool InApse(Vector2 p, float margin) { return fabsf(p.x) <= ApseHalfW(p.y) - margin && p.y <= Z_BACK - margin; }

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

// Paints flat art onto a bay of the apse: the canvas is centred along the wall from a to b (room X, Z; a is the end
// on the left of the screen), from height Y0 up. Returns where it landed on screen.
template <typename F>
Rectangle PaintPanel(Vector2 a, Vector2 b, float Y0, int w, int h, F draw) {
    RenderTexture2D& rt = ArtRT();
    BeginCanvas(rt);
    draw((float)w, (float)h);
    EndCanvas();
    float L = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)), half = std::min(0.5f, w / ART_PX / L * 0.5f);
    Vector2 A{a.x + (b.x - a.x) * (0.5f - half), a.y + (b.y - a.y) * (0.5f - half)}, B{a.x + (b.x - a.x) * (0.5f + half), a.y + (b.y - a.y) * (0.5f + half)};
    float Y1 = Y0 + h / ART_PX;
    Vector3 tl{A.x, Y1, A.y}, tr{B.x, Y1, B.y}, br{B.x, Y0, B.y}, bl{A.x, Y0, A.y};
    ProjQuad(rt.texture, tl, tr, br, bl, {0, 1, w / (float)rt.texture.width, -h / (float)rt.texture.height}, WHITE, 18, 1);
    return Bounds({Proj(tl), Proj(tr), Proj(br), Proj(bl)});
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
// a phase that advances at a rate you can change without it jumping (the radar sweep speeding up on hover ...)
float Spin(int slot, float rate) { static float acc[16] = {}; acc[slot] += GetFrameTime() * rate; return acc[slot]; }

// ---------------------------------------------------------------- stations
enum StationId { ST_CREW, ST_LIBRARY, ST_RADAR, ST_HELM, ST_PERISCOPE, ST_WORKSHOP, ST_SICKBAY, ST_WARD, ST_CARDS, ST_ARCADE, ST_STUDY, ST_COUNT };
struct Station { Scene target; const char* name; const char* hint; Vector2 stand; }; // stand = (X, Z) where crew gather
const Station STATIONS[ST_COUNT] = {
    {Scene::Crew, "Crew Quarters", "Choose your party, fit relics, and pick each crew member's abilities.", {-560, 620}},
    {Scene::Bookshelf, "Library", "Learn about the crew, conditions, and creatures of the deep.", {-470, 860}},
    {Scene::Radar, "Radar Room", "Pick up new recruits and buy relics from passing salvagers.", {560, 620}},
    {Scene::Helm, "The Helm", "Chart a course and send your party on an expedition.", {-150, 800}},
    {Scene::Periscope, "Periscope", "Take on a platforming run for gold.", {200, 1000}},
    {Scene::Workshop, "Workshop", "Upgrade the Nautilus: reflectors, bunks, sonar and infirmary gear.", {470, 860}},
    {Scene::SickLeave, "Sick Bay", "Rattled crew can rest by the organ and steady their nerves.", {-260, 930}},
    {Scene::Ward, "The Ward", "Patch up injured crew.", {260, 930}},
    {Scene::Cards, "The Card Table", "Play Flats against the ship's dealer: a card-sharp's way to earn gold when you're short.", {-180, 600}},
    {Scene::Arcade, "The Deep Arcade", "A brass cabinet in the corner of the salon: games for more than one player.", {200, 560}},
    {Scene::Study, "The Study Hatch", "A brass hatch in the deck, and a ladder down to the Study.", {0, 560}},
};
float gHov[ST_COUNT] = {};   // how hovered each station is, eased: drives its hover motion (the wheel spins, the door swings ...)
Rectangle stationRect[ST_COUNT];

// ---------------------------------------------------------------- life aboard
// Only the Nautilus's own hands walk the salon (your expedition crew stay in their quarters). Most have a
// post, where they stand working, and every so often they stretch their legs and come back.
struct Npc { const char* role; HeroClass build; int outfit; Vector2 post; bool faceRight; };
const Npc NPCS[] = {
    {"Helmsman", HeroClass::Captain, OUT_HELMSMAN, {-165, 870}, true},    // at the wheel
    {"Radio operator", HeroClass::Mechanic, OUT_RADIO, {600, 640}, true}, // listening at the sonar
    {"Engineer", HeroClass::Mechanic, OUT_ENGINEER, {500, 820}, true},    // hammering at the workbench
    {"Professor", HeroClass::Captain, OUT_PROFESSOR, {-500, 820}, false}, // browsing the shelves
    {"Steward", HeroClass::Captain, OUT_STEWARD, {0, 0}, true},           // no post: does the rounds with a tray
    {"Orderly", HeroClass::Nurse, OUT_ORDERLY, {260, 960}, true},         // tending the operating table
};
constexpr int NPC_COUNT = sizeof(NPCS) / sizeof(NPCS[0]);
struct Walker { int id; Vector2 pos, target; float wait, phase; bool right, atPost; };
struct Cat {
    Vector2 pos{120, 700}, target{120, 700}; float wait = 3, phase = 0, purr = 0; bool right = true, asleep = false;
    float y = 0;                 // height off the deck: it likes to sit on the arcade cabinet and the chart table
    int perch = 0, goal = 0;     // 0 the deck, 1 the arcade cabinet, 2 the chart table (goal: where it is heading)
    float hop = -1; Vector2 hopFrom{}, hopTo{}; float hopY0 = 0, hopY1 = 0;
};
Rectangle catRect{};   // where the cat is on screen, for clicking
Vector2 catHead{};     // where its head is on screen, for the hearts
std::vector<Walker> walkers;
Cat cat;

// Furniture on the floor, as circles (X, Z, radius) that people and the cat walk around.
struct Obstacle { float x, z, r; };
const Obstacle OBSTACLES[] = {{0, 900, 120}, {-340, 575, 115}, {340, 560, 50}, {-400, 1060, 170}, {420, 1060, 160}, {262, 1060, 40}};
constexpr float CARD_X = -340, CARD_Z = 540, DEALER_Z = 615; // the card table (near left corner of the square), and the dealer seated behind it

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
        if (!Blocked(p, 30) && InApse(p, 50)) return p;
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
    np.y = std::clamp(np.y, 540.0f, Z_BACK - 60);
    float hw = ApseHalfW(np.y) - 40;   // the walls of the apse close in toward the back
    np.x = std::clamp(np.x, -hw, hw);
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
    // the cat's perches: up on the arcade cabinet, or on the chart table by the helm
    const Vector2 PERCH_AT[3] = {{0, 0}, {ARC_X, ARC_Z - 2}, {-205, 890}}, PERCH_FOOT[3] = {{0, 0}, {ARC_X - 90, 560}, {-205, 790}};
    const float PERCH_Y[3] = {0, 212, 150};
    if (cat.hop >= 0) { // mid-leap
        cat.hop += dt / 0.5f;
        float u = std::min(1.0f, cat.hop);
        cat.pos = {cat.hopFrom.x + (cat.hopTo.x - cat.hopFrom.x) * u, cat.hopFrom.y + (cat.hopTo.y - cat.hopFrom.y) * u};
        cat.y = cat.hopY0 + (cat.hopY1 - cat.hopY0) * u + sinf(u * PI) * 60;
        cat.phase += dt * 10;
        if (cat.hop >= 1) { cat.hop = -1; cat.y = cat.hopY1; }
        return;
    }
    if (cat.purr > 0) { cat.asleep = false; cat.purr -= dt; cat.wait = std::max(cat.wait, cat.purr + 0.5f); } // a scratch behind the ears: it stays put
    if (cat.wait > 0) {
        cat.wait -= dt; cat.phase = 0;
        if (cat.wait <= 0) {
            cat.asleep = false;
            if (cat.perch) { // down off its perch first
                cat.hop = 0; cat.hopFrom = cat.pos; cat.hopTo = PERCH_FOOT[cat.perch]; cat.hopY0 = cat.y; cat.hopY1 = 0; cat.perch = 0; cat.goal = 0;
                cat.wait = RandF(0.5f, 1.5f);
                return;
            }
            int r = GetRandomValue(0, 99);
            cat.goal = r < 14 ? 1 : r < 24 ? 2 : 0;
            cat.target = cat.goal ? PERCH_FOOT[cat.goal] : FloorSpot();
        }
    } else {
        Vector2 d{cat.target.x - cat.pos.x, cat.target.y - cat.pos.y};
        float len = sqrtf(d.x * d.x + d.y * d.y), step = 55 * dt;
        if (len <= step + 2) {
            cat.pos = cat.target;
            if (cat.goal) { // up it goes, and settles there a while
                cat.hop = 0; cat.hopFrom = cat.pos; cat.hopTo = PERCH_AT[cat.goal]; cat.hopY0 = 0; cat.hopY1 = PERCH_Y[cat.goal];
                cat.perch = cat.goal; cat.right = cat.goal == 1 ? false : cat.right;
                cat.asleep = GetRandomValue(0, 99) < 40; cat.wait = RandF(12, 26);
                return;
            }
            cat.asleep = GetRandomValue(0, 99) < 30; cat.wait = cat.asleep ? RandF(16, 34) : RandF(3, 10);
        } else {
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
    if (w.wait <= 0) return p;
    float ph = t + w.id * 1.7f;
    auto bell = [](float u, float a, float b, float c) {
        auto sm = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };
        return u <= a || u >= c ? 0.0f : u < b ? sm((u - a) / (b - a)) : sm((c - u) / (c - b));
    };
    if (!w.atPost) { // resting between errands: an ambient idle, picked by who they are and how far the errand round has got
        switch ((w.id + (int)(w.pos.x * 0.013f + w.pos.y * 0.017f) + 3) % 3) {
            case 0: p.raise = 0.3f; p.weaponTilt = 22 * sinf(ph * 5); p.lean = 0.06f; p.headDown = 0.4f; break;                         // cleaning a weapon
            case 1: p.reach = 0.28f; p.lean = 0.22f; p.headDown = 0.55f + 0.1f * sinf(ph * 0.8f); p.backRaise = 0.15f; break;            // studying a chart
            default: { float c = fmodf(ph * 0.35f, 1.0f); p.backRaise = 0.85f * bell(c, 0.1f, 0.3f, 0.6f); p.headDown = -0.25f * bell(c, 0.25f, 0.4f, 0.55f); p.lean = -0.06f * bell(c, 0.2f, 0.35f, 0.6f); } break;   // a cup of tea
        }
        return p;
    }
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
// A stretch of wall from a to b (room X, Z), as seen left to right on screen: panelling above a mahogany wainscot,
// brass rails, a dark cornice and a baseboard.
void WallStrip(Vector2 a, Vector2 b, Texture2D metal, Texture2D wood, Color panel, Color woodTint) {
    float L = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    ProjQuad(metal, {a.x, RH, a.y}, {b.x, RH, b.y}, {b.x, 170, b.y}, {a.x, 170, a.y}, {0, 0, L / 170, 3}, panel, 16, 1);
    ProjQuad(wood, {a.x, 170, a.y}, {b.x, 170, b.y}, {b.x, 0, b.y}, {a.x, 0, a.y}, {0, 0, L / 170, 1}, woodTint, 16, 1);
    for (float y : {170.0f, 176.0f}) DrawLineEx(Proj(a.x, y, a.y), Proj(b.x, y, b.y), 2, Pal::Brass);
    DrawLineEx(Proj(a.x, RH - 6, a.y), Proj(b.x, RH - 6, b.y), 4, Pal::BrassDk);
    DrawLineEx(Proj(a.x, 0, a.y), Proj(b.x, 0, b.y), 3, Color{40, 26, 18, 255});
}

// A gilded pilaster standing at a corner of the room, with a brass capital and base.
void Pilaster(float X, float Z) {
    float k = Px(Z);
    Vector2 top = Proj(X, RH, Z), bot = Proj(X, 0, Z);
    DrawLineEx(top, bot, 30 * k, Color{84, 58, 32, 255});
    DrawLineEx({top.x - 4 * k, top.y}, {bot.x - 4 * k, bot.y}, 20 * k, Color{128, 92, 50, 255});
    DrawLineEx({top.x - 8 * k, top.y}, {bot.x - 8 * k, bot.y}, 3 * k, Pal::Brass);
    for (float y : {RH - 40, 170.0f, 14.0f}) { Vector2 c = Proj(X, y, Z); DrawRectangleRec({c.x - 20 * k, c.y - 8 * k, 40 * k, 16 * k}, Pal::BrassDk); DrawRectangleRec({c.x - 18 * k, c.y - 7 * k, 36 * k, 5 * k}, Pal::Brass); }
}

// The salon's shell. The back of the room is an apse of five bays (APSE), like the buildings round the square of
// Darkest Dungeon's hamlet: two angled bays each side and the great window in the middle, with a short stretch of
// straight side wall at either edge; the floor between them is the open square.
void DrawShell(float t) {
    Texture2D wood = GetTex(Tex::Wood), metal = GetTex(Tex::Metal);
    Color panel{58, 88, 84, 255}, woodTint{140, 92, 64, 255};
    // ceiling: coffered dark wood with brass-trimmed beams (its corners behind the apse are covered by the walls)
    ProjQuad(wood, {-RW, RH, Z_BACK}, {RW, RH, Z_BACK}, {RW, RH, Z_NEAR}, {-RW, RH, Z_NEAR}, {0, 0, 6, 5}, Color{80, 56, 42, 255}, 1, 20);
    for (float z = 420; z < Z_BACK; z += 180)
        ProjFill({-RW, RH, z}, {RW, RH, z}, {RW, RH, z - 26}, {-RW, RH, z - 26}, Color{50, 34, 24, 255});
    for (float x = -RW + 240; x < RW; x += 240)
        ProjFill({x - 12, RH, Z_BACK}, {x + 12, RH, Z_BACK}, {x + 12, RH, Z_NEAR}, {x - 12, RH, Z_NEAR}, Color{58, 40, 28, 255});
    // the floor: parquet, and a great patterned rug in the middle of the square
    ProjQuad(wood, {-RW, 0, Z_BACK}, {RW, 0, Z_BACK}, {RW, 0, Z_NEAR}, {-RW, 0, Z_NEAR}, {0, 0, 7, 9}, Color{150, 104, 70, 255}, 1, 28);
    ProjFill({-380, 0, 1000}, {380, 0, 1000}, {380, 0, 460}, {-380, 0, 460}, Color{110, 30, 32, 255});
    ProjFill({-350, 0, 980}, {350, 0, 980}, {350, 0, 480}, {-350, 0, 480}, Color{150, 48, 40, 255});
    ProjFill({-310, 0, 950}, {310, 0, 950}, {310, 0, 510}, {-310, 0, 510}, Color{120, 34, 34, 255});
    for (int k = 0; k < 5; k++) { // medallions woven into the rug
        float z = 580 + k * 80, w = 56;
        Vector2 c = Proj(0, 0, z), l = Proj(-w, 0, z), f = Proj(0, 0, z + 34), n = Proj(0, 0, z - 34);
        DrawTri(l, f, {2 * c.x - l.x, c.y}, Color{196, 150, 80, 255});
        DrawTri(l, {2 * c.x - l.x, c.y}, n, Color{196, 150, 80, 255});
    }
    // the walls: the near stretch of each side wall, then the five bays of the apse
    WallStrip({-RW, Z_NEAR}, APSE[0], metal, wood, panel, woodTint);
    for (int i = 0; i < 5; i++) WallStrip(APSE[i], APSE[i + 1], metal, wood, i == 2 ? Tone(panel, -0.1f) : panel, woodTint);
    WallStrip(APSE[5], {RW, Z_NEAR}, metal, wood, panel, woodTint);
    Pilaster(-RW, 520); Pilaster(RW, 520);
    for (int i = 0; i < 6; i++) Pilaster(APSE[i].x, APSE[i].y);
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
    // the door leaf, half open; it swings wider when you look at it
    float open = 0.3f + 0.45f * gHov[0];
    float lw = in.width * (1 - open);
    DrawTiled(Tex::Wood, {in.x, in.y, lw, in.height}, 0.6f, Color{96, 64, 44, 255});
    DrawRectangle((int)(in.x + lw - 6), (int)in.y, 6, (int)in.height, Color{40, 28, 20, 255});
    if (lw > 40) { DrawCircleV({in.x + lw * 0.5f, in.y + 90}, std::min(22.0f, lw * 0.3f), Pal::BrassDk); DrawCircleV({in.x + lw * 0.5f, in.y + 90}, std::min(16.0f, lw * 0.22f), Color{40, 70, 80, 255}); }
    DrawCircleV({in.x + lw - 16, in.y + in.height * 0.55f}, 5, Pal::Brass);
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
    float lx = w * 0.62f - gHov[1] * w * 0.34f; // the rolling ladder slides along its rail when you look at it
    DrawLineEx({12, 66}, {w - 12, 66}, 4, Pal::BrassDk);
    for (int k = 1; k < 16; k++) {
        float f = k / 16.0f;
        DrawLineEx({lx + f * 40, 70 + f * (h - 70)}, {lx + 40 + f * 40, 70 + f * (h - 70)}, 5, Color{110, 72, 44, 255});
    }
    DrawLineEx({lx, 70}, {lx + 40, h}, 6, Color{136, 90, 56, 255});
    DrawLineEx({lx + 40, 70}, {lx + 80, h}, 6, Color{136, 90, 56, 255});
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
    float a = Spin(0, 1.6f * (1 + 2.2f * gHov[2])), ad = fmodf(a * RAD2DEG, 360);
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
    float sp = Spin(1, 0.6f * (1 + 3 * gHov[5])), r1 = 70, r2 = 44, r3 = 34;
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
    Billboard(ORGAN_X, 0, ALCOVE_Z, [&] {
        rlPushMatrix(); rlScalef(0.85f, 0.85f, 1);   // sized to its bay of the apse
        // pipes rising behind the console
        for (int k = 0; k < 15; k++) {
            float x = -170 + k * 24, hh = 300 + sinf(k * 0.55f) * 90 + (7 - fabsf(k - 7.0f)) * 22;
            DrawRectangleGradientH((int)x, (int)(-160 - hh), 11, (int)hh, Color{150, 110, 50, 255}, Color{236, 196, 120, 255});
            DrawRectangleGradientH((int)x + 11, (int)(-160 - hh), 11, (int)hh, Color{236, 196, 120, 255}, Color{120, 84, 36, 255});
            DrawEllipse((int)x + 11, (int)(-160 - hh * 0.22f), 6, 3, Color{40, 28, 16, 255});
        }
        float hum = std::max(playing ? 0.6f : 0.0f, gHov[6]) * (0.7f + 0.3f * sinf(t * 3));
        if (hum > 0.02f) for (int k = 0; k < 15; k++) Glow({-159.0f + k * 24, -420}, 60, Color{255, 200, 120, (unsigned char)(50 * hum)});
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
        rlPopMatrix();
    });
}

void DrawWardWall(float t) {
    (void)t;
    Billboard(WARD_X, 0, ALCOVE_Z, [&] {
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
        // the dais: a round platform of dark wood, brass-rimmed, the centre of the square
        DrawEllipse(0, 4, 178, 30, Color{30, 20, 14, 255});
        DrawEllipse(0, -2, 170, 27, Pal::BrassDk);
        DrawEllipse(0, -5, 162, 24, Color{96, 62, 40, 255});
        for (int k = 0; k < 16; k++) { float a = k * PI / 8; DrawCircleV({cosf(a) * 166, -2 + sinf(a) * 25.5f}, 2.2f, Pal::Brass); }
        // the binnacle and pedestal
        DrawRectangleGradientH(-22, -190, 22, 190, Color{60, 38, 24, 255}, Color{130, 86, 54, 255});
        DrawRectangleGradientH(0, -190, 22, 190, Color{130, 86, 54, 255}, Color{50, 32, 20, 255});
        for (float y = -150; y < 0; y += 50) DrawPipeH(-26, 26, y, 5, Pal::Brass);
        DrawEllipse(0, -2, 60, 12, Color{50, 34, 22, 255});
        // the wheel
        Vector2 hc{0, -210};
        float rot = Spin(2, 0.12f + 1.4f * gHov[3]);
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
        if (gHov[3] > 0.05f) { // a glint runs round the brass rim
            float ga = t * 3;
            Vector2 gp{hc.x + cosf(ga) * 87, hc.y + sinf(ga) * 87};
            DrawCircleV(gp, 5 * gHov[3], Fade(WHITE, 0.8f * gHov[3]));
            DrawLineEx({gp.x - 12, gp.y}, {gp.x + 12, gp.y}, 2, Fade(WHITE, 0.6f * gHov[3]));
            DrawLineEx({gp.x, gp.y - 12}, {gp.x, gp.y + 12}, 2, Fade(WHITE, 0.6f * gHov[3]));
        }
        // the chart table beside it
        DrawRectangle(-248, -120, 10, 120, Color{90, 58, 36, 255});
        DrawRectangle(-172, -120, 10, 120, Color{90, 58, 36, 255});
        DrawTiled(Tex::Wood, {-262, -136, 114, 18}, 0.5f, Color{140, 94, 60, 255});
        DrawRectangle(-252, -148, 94, 14, Color{230, 214, 176, 255});
        DrawGauge({-205, -196}, 26, 0.55f + 0.04f * sinf(t * 1.3f), Color{236, 228, 206, 255});
        DrawLineEx({-205, -170}, {-205, -136}, 5, Pal::BrassDk);
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
        float fold = gHov[4]; // the handles fold out when you look at it
        for (int k = -1; k <= 1; k += 2) {
            float reach = 64 + 34 * fold;
            DrawPipeH(k < 0 ? -46 - reach : 46, k < 0 ? -46 : 46 + reach, -286 + 18 * (1 - fold), 6, Color{80, 84, 82, 255});
            DrawRectangleRounded({k < 0 ? -60 - reach : 34 + reach, -298 + 18 * (1 - fold), 26, 24}, 0.5f, 6, Color{30, 26, 24, 255});
        }
        DrawCircleV({-4 + sinf(t * 0.4f) * 3 + 6 * fold, -304}, 4, Color{150, 220, 230, 220}); // the lens catches the light as it turns
        DrawBrassPlate({-70, -400, 140, 26}, "PERISCOPE", 14);
    });
}

void DrawChaise(float t, const std::vector<const Hero*>& resting) {
    Billboard(-400, 0, 1060, [&] {
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
    Billboard(420, 0, 1060, [&] {
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
        DrawEllipse(0, -132, 26, 8, Pal::BrassDk);                        // the ceiling rosette it actually hangs from
        DrawEllipse(0, -133, 20, 5, Pal::Brass);
        DrawLineEx({0, -130}, {0, 0}, 5, Pal::Brass);                     // a bright chain, plainly connecting the two
        DrawLineEx({-2, -130}, {-2, 0}, 2, Pal::BrassDk);
        DrawEllipse(0, 0, 150, 26, Pal::BrassDk);
        DrawEllipse(0, -4, 140, 20, Pal::Brass);
        for (int k = 0; k < 8; k++) { // an even, mirrored ring of candles, so it reads level rather than lopsided
            float a = k * 2 * PI / 8;
            Vector2 p{cosf(a) * 132, sinf(a) * 20 - 14};
            DrawRectangle((int)p.x - 4, (int)p.y - 18, 8, 18, Color{236, 228, 200, 255});
            DrawEllipse((int)p.x, (int)p.y - 24, 4, 8 + sinf(t * 11 + k) * 1.5f, Color{255, 210, 130, 255});
        }
        for (int k = 0; k < 13; k++) DrawCircleV({-120 + k * 20.0f, 18 + fabsf(k - 6.0f) * 2}, 4, Color{200, 230, 240, 200}); // crystal drops, sagging evenly from the centre
    });
}

// The ship's cat: a ginger tabby in pixel art. It wanders, sits and watches (breathing, its tail swishing), sleeps curled up, and purrs
// when you click it. Each frame is composed on a small character canvas from sprite parts (head, body, tail, legs), then drawn as
// crisp squares with an ink outline. Returns where its head ended up on screen.
namespace catpx {
const int CW = 38, CH = 26;
struct Canvas {
    char c[CH][CW];
    Canvas() { for (auto& r : c) for (char& ch : r) ch = '.'; }
    void set(int x, int y, char ch) { if (x >= 0 && x < CW && y >= 0 && y < CH) c[y][x] = ch; }
    char get(int x, int y) const { return x >= 0 && x < CW && y >= 0 && y < CH ? c[y][x] : '.'; }
    void blit(const char* const* rows, int n, int x, int y) {
        for (int j = 0; j < n; j++) for (int i = 0; rows[j][i]; i++) if (rows[j][i] != '.') set(x + i, y + j, rows[j][i]);
    }
    void ellipse(float cx, float cy, float rx, float ry, char ch) {
        for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1) set(x, y, ch);
        }
    }
};
const char* const HEAD_OPEN[] = {".k........k.", ".kok....kok.", ".kopkkkkpok.", "kooooooooook", "koOooOOooOok", "kogeooooegok", "kooooooooook", "kowwwppwwwok", ".kowwwwwwok.", "..kooooook..", "...kkkkkk..."};
const char* const HEAD_SHUT[] = {".k........k.", ".kok....kok.", ".kopkkkkpok.", "kooooooooook", "koOooOOooOok", "koeeooooeeok", "kooooooooook", "kowwwppwwwok", ".kowwwwwwok.", "..kooooook..", "...kkkkkk..."};
const char* const BODY_SIT[] = {"...kkkkkkkk...", "..kooooOooook.", ".kooOooooooowk", ".kooOoooooowwk", "kooooOoooowwwk", "koooooooooowwk", "kooooooooowwwk", "kooooooooowwwk", ".koooooooowwk.", ".kwwkkkkkkwwk.", "..kkk....kkk.."};
const char* const BODY_WALK[] = {".kkkkkkkkkkkkkkkk.", "koooOoooOoooOooook", "kooooooooooooooook", "kooooooooooooooook", "kowwwwwwwwwwwwwwok", ".kwwwwwwwwwwwwwwk.", "..kkkkkkkkkkkkkk.."};
const char* const TAIL_A[] = {".kk.....", "koOk....", "kook....", ".kook...", "..kook..", "...kook.", "....kook", ".....kOk", "......kk"};
const char* const TAIL_B[] = {"..kk....", ".kook...", ".kOok...", ".kook...", "..kook..", "...kook.", "....kook", ".....kOk", "......kk"};
const char* const TAIL_C[] = {"kk......", "koOk....", ".kook...", "..kook..", "..kook..", "...kook.", "....kook", ".....kOk", "......kk"};
Color Pal(char ch) {
    switch (ch) {
        case 'k': return {92, 52, 30, 255};
        case 'o': return {238, 150, 66, 255};
        case 'O': return {186, 96, 40, 255};
        case 'w': return {252, 236, 208, 255};
        case 'p': return {244, 150, 156, 255};
        case 'g': return {150, 200, 84, 255};
        case 'e': return {28, 20, 16, 255};
        case 'l': return {252, 186, 108, 255};
        default: return {0, 0, 0, 0};
    }
}
void Leg(Canvas& cv, int x, float ph, bool far) {
    float sw = sinf(ph), lift = std::max(0.0f, cosf(ph));
    int xo = (int)lroundf(sw * 2), top = 19, bot = 23 - (lift > 0.55f ? 1 : 0);
    for (int y = top; y <= bot; y++) {
        cv.set(x + xo, y, 'k'); cv.set(x + xo + 1, y, y == bot ? 'w' : (far ? 'O' : 'o')); cv.set(x + xo + 2, y, 'k');
    }
    cv.set(x + xo, bot, 'k'); cv.set(x + xo + 2, bot, 'k');
}
// Scale2x: doubles resolution, rounding diagonal steps into smooth 45-degree edges instead of square blocks -
// the same trick the card art's EPX upscale uses, so the cat reads as drawn rather than pixelated.
struct Canvas2 {
    char c[CH * 2][CW * 2];
    char get(int x, int y) const { return x >= 0 && x < CW * 2 && y >= 0 && y < CH * 2 ? c[y][x] : '.'; }
};
Canvas2 Scale2x(const Canvas& in) {
    Canvas2 out{};
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
        char p = in.get(x, y), a = in.get(x, y - 1), b = in.get(x + 1, y), c = in.get(x - 1, y), d = in.get(x, y + 1);
        char e0 = (c == a && c != d && a != b) ? a : p, e1 = (a == b && a != c && b != d) ? b : p;
        char e2 = (c == d && c != b && d != a) ? c : p, e3 = (b == d && b != a && d != c) ? d : p;
        out.c[y * 2][x * 2] = e0; out.c[y * 2][x * 2 + 1] = e1; out.c[y * 2 + 1][x * 2] = e2; out.c[y * 2 + 1][x * 2 + 1] = e3;
    }
    return out;
}
}  // namespace catpx

Vector2 DrawCatAt(Vector2 feet, float k, bool right, bool sitting, bool purring, float phase, float t, float seed, bool asleep = false) {
    using namespace catpx;
    Canvas cv;
    bool blink = fmodf(t * 0.37f + seed * 0.01f, 1.0f) < 0.035f;
    int breath = sinf(t * 2.0f + seed) > 0.25f ? 1 : 0;   // a slow rise and fall
    int tf = (int)floorf(fmodf(t * (purring ? 1.6f : 1.1f) + seed * 0.07f, 4.0f));
    const char* const* tail = (tf == 0 || tf == 2) ? TAIL_B : (tf == 1 ? TAIL_A : TAIL_C);   // the swish: up, left, up, right
    int hx = 0, hy = 0;
    if (asleep) {
        cv.ellipse(15, 18.5f, 12, 5.6f + breath * 0.9f, 'o');   // a curled loaf that swells as it breathes
        for (int i = 0; i < 5; i++) for (int y = 0; y < 3; y++) cv.set(9 + i * 3, 13 - breath + y, 'O');
        for (int x = 4; x < 20; x++) { cv.set(x, 23, 'o'); cv.set(x, 22, 'o'); }   // the tail curled along the front
        for (int x = 4; x < 8; x++) cv.set(x, 22, 'O');
        cv.blit(HEAD_SHUT, 11, 20, 12 - breath); hx = 26; hy = 17;
    } else if (sitting) {
        cv.blit(tail, 9, 2, 12);
        cv.blit(BODY_SIT, 11, 10, 13);
        cv.blit(purring || blink ? HEAD_SHUT : HEAD_OPEN, 11, 16, 5 + (breath ? 0 : 1));
        hx = 22; hy = 10 + (breath ? 0 : 1);
    } else {
        float bob = fabsf(sinf(phase)) > 0.6f ? 1.0f : 0.0f;
        cv.blit(tail, 9, 1, 8);
        Leg(cv, 13, phase + PI, true); Leg(cv, 24, phase, true);
        cv.blit(BODY_WALK, 7, 9, 12 + (int)bob);
        Leg(cv, 11, phase, false); Leg(cv, 22, phase + PI, false);
        cv.blit(HEAD_OPEN, 11, 24, 6 + (int)bob);
        hx = 30; hy = 11 + (int)bob;
    }
    // the ink outline, drawn round anything that isn't already a dark line
    Canvas out = cv;
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) {
        if (cv.get(x, y) != '.') continue;
        bool nb = false;
        for (int d = 0; d < 4 && !nb; d++) { char q = cv.get(x + (d == 0) - (d == 1), y + (d == 2) - (d == 3)); nb = q != '.' && q != 'k'; }
        if (nb) out.set(x, y, 'k');
    }
    float f = right ? 1.0f : -1.0f, ps = 2.3f * k;
    int lastRow = 0;
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) if (out.get(x, y) != '.') lastRow = y;
    Vector2 origin{feet.x - f * (CW / 2) * ps, feet.y - (lastRow + 1) * ps};
    auto X = [&](int cx) { return roundf(origin.x + f * cx * ps); };
    DrawShadowBlob(feet, (asleep ? 26 : sitting ? 19 : 24) * k);
    float vib = purring ? (fmodf(t * 30, 2.0f) < 1 ? 0.0f : ps * 0.5f) : 0;
    catpx::Canvas2 out2 = catpx::Scale2x(out);
    float ps2 = ps / 2.0f;
    auto X2 = [&](int cx2) { return roundf(origin.x + f * cx2 * ps2); };
    int lastRow2 = lastRow * 2 + 1;
    for (int y = 0; y < CH * 2; y++) for (int x = 0; x < CW * 2; x++) {
        char ch = out2.get(x, y);
        if (ch == '.') continue;
        float x0 = X2(x), x1 = X2(x + 1), rowVib = vib * (y < lastRow2 - 8 ? 1 : 0);
        float yy0 = roundf(origin.y + y * ps2 + rowVib), yy1 = roundf(origin.y + (y + 1) * ps2 + rowVib);
        DrawRectangleRec({std::min(x0, x1), yy0, fabsf(x1 - x0) + 0.6f, yy1 - yy0 + 0.6f}, Pal(ch));
    }
    if (asleep) { // z z z
        for (int i = 0; i < 3; i++) {
            float ph = fmodf(t * 0.35f + i / 3.0f, 1.0f);
            TxtBold("z", X(hx) + f * ps * 2 + ph * 12 * f, origin.y + hy * ps - ph * 40 * k - 6, (int)(10 + ph * 8), Fade(Pal::Paper, (1 - ph) * 0.85f));
        }
    }
    return {X(hx), origin.y + hy * ps};
}void DrawCat(float t) {
    float Z = cat.pos.y, k = Px(Z) * 2.7f;
    Vector2 feet = Proj(cat.pos.x, cat.y, Z);
    catRect = {feet.x - 28 * k, feet.y - 46 * k, 56 * k, 48 * k};
    catHead = DrawCatAt(feet, k, cat.right, cat.wait > 0, cat.purr > 0, cat.phase, t, cat.pos.x, cat.asleep && cat.purr <= 0);
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

// The long runs round the top of the room, following the walls and the bays of the apse (drawn with the room,
// behind the furniture): a big copper main with bolted elbows at every corner, and a small steam line above it.
void DrawWallPipes(float t) {
    (void)t;
    auto run = [&](float inset, float Y, float R, Color c, bool flanges) {
        Vector3 pts[8] = {{-RW + inset, Y, Z_NEAR + 40}, {-RW + inset, Y, 700}};
        for (int i = 1; i <= 4; i++) { Vector2 p = APSE[i]; float s = (ApseHalfW(p.y) - inset) / std::max(1.0f, ApseHalfW(p.y)); pts[i + 1] = {p.x * s, Y, p.y - inset * 0.9f}; }
        pts[6] = {RW - inset, Y, 700}; pts[7] = {RW - inset, Y, Z_NEAR + 40};
        for (int i = 0; i < 7; i++) {
            Pipe3D(pts[i], pts[i + 1], R, c, 10);
            Vector3 mid = Mix(pts[i], pts[i + 1], 0.5f);
            Hanger(mid.x, Y, mid.z);
            if (flanges && i > 0) { Vector3 d{pts[i + 1].x - pts[i].x, 0, pts[i + 1].z - pts[i].z}; float l = sqrtf(d.x * d.x + d.z * d.z); Flange3D(pts[i], {d.x / l, 0, d.z / l}, R, c); }
        }
    };
    run(100, RH - 30, 8, Color{128, 134, 130, 255}, false);
    run(48, RH - 45, 17, Pal::Copper, true);
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
    const Leak leaks[] = {{{90, CROSS_Y, CROSS_Z}, 1.0f, 1}, {{-600, RH - 45, 830}, 0.7f, 1}, {{RW - 48, RH - 45, 600}, 0.8f, -1}};
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
// ---------------------------------------------------------------- the card table and its dealer
// The dealer sits across the table in a dark cloak and hood, face grey as old candle wax, eyes catching the
// light. A cold glow rises from the felt.
void DrawCardDealer(float t) {
    float k = Px(DEALER_Z) * 1.05f;
    Vector2 feet = Proj(CARD_X, 0, DEALER_Z), o = FigureFeet();
    auto P = [&](float dx, float dy) { return Vector2{o.x + dx * k, o.y + dy * k}; };
    float br = sinf(t * 1.2f) * 1.2f;
    Color cloak{26, 30, 48, 255}, glove{34, 34, 40, 255}, wax{132, 134, 132, 255}, brass{176, 140, 70, 255};
    BeginFigure();
    ShadeLimb(P(-42, -122 + br), P(42, -122 + br), 22, 22, Tone(cloak, -0.3f));                  // the back mantle, widest layer
    ShadeLimb(P(-30, -128 + br), P(30, -128 + br), 24, 24, cloak);                                 // shoulders
    ShadeLimb(P(0, -60), P(0, -130 + br), 30, 27, cloak);                                          // the body under the cloak
    for (int i = -2; i <= 2; i++) DrawLineEx(P(i * 9.0f, -116), P(i * 12.0f + (i > 0 ? 2.0f : -2.0f), -62), 1.3f * k, Tone(cloak, -0.4f)); // folds
    ShadeLimb(P(-38, -117 + br), P(38, -117 + br), 13, 13, Tone(cloak, 0.12f));                    // a mantle over the shoulders...
    DrawLineEx(P(-40, -110 + br), P(40, -110 + br), 1.7f * k, brass);                              // ...trimmed in brass
    for (int i = -2; i <= 2; i++) { DrawLineEx(P(i * 15.0f, -110 + br), P(i * 15.0f, -102 + br), 1.3f * k, brass); DrawCircleV(P(i * 15.0f, -101 + br), 1.3f * k, brass); } // tassels
    DrawTri(P(-32, -126 + br), P(32, -126 + br), P(0, -150 + br), Tone(cloak, -0.35f));            // a high collar behind the head
    DrawTri(P(-46, -118), P(46, -118), P(0, -180 + br), Tone(cloak, -0.3f));                       // the hood's peak
    DrawLineEx(P(-46, -118), P(0, -180 + br), 1.3f * k, Fade(brass, 0.6f));                        // its trim
    ShadeBall(P(0, -160 + br), 25, Tone(cloak, -0.2f));                                            // hood
    ShadeBall(P(0, -159 + br), 21, Tone(cloak, -0.6f));                                            // the lining, a darker layer inside
    ShadeBall(P(1, -158 + br), 15.5f, wax);                                                        // face
    for (int e = -1; e <= 1; e += 2) DrawEllipse((int)P(e * 7.5f, -150 + br).x, (int)P(e * 7.5f, -150 + br).y, 2.6f * k, 3.4f * k, Fade(BLACK, 0.3f)); // sunken cheeks
    DrawLineEx(P(1, -160 + br), P(2, -151 + br), 1.4f * k, Tone(wax, -0.4f));                      // the nose
    DrawLineEx(P(-12, -164 + br), P(12, -164 + br), 3 * k, Color{50, 52, 56, 255});               // heavy brow
    DrawLineEx(P(-8, -142 + br), P(8, -142 + br), 2.2f * k, Color{36, 32, 34, 255});               // flat mouth
    bool blink = fmodf(t, 4.3f) < 0.13f;
    for (int e = -1; e <= 1; e += 2)
        if (!blink) DrawEllipse((int)P(e * 6.5f, -159 + br).x, (int)P(e * 6.5f, -159 + br).y, 3.6f * k, 2.4f * k, Color{214, 226, 255, 255});
        else DrawLineEx(P(e * 6.5f - 3.5f, -159 + br), P(e * 6.5f + 3.5f, -158.5f + br), 1.2f * k, Color{30, 30, 36, 255});
    float shuf = sinf(t * 7) * 5 * (fmodf(t, 6.5f) > 1.2f ? 1.0f : 0.0f);                         // always shuffling, except while he flicks a card
    float hlx = -38 + 10 + shuf, hrx = 40 - 10 - shuf;
    ShadeLimb(P(-30, -128), P(hlx, -108), 10, 9, cloak);                                            // arms reaching to the felt
    ShadeLimb(P(30, -128), P(hrx, -108), 10, 9, cloak);
    ShadeBall(P(hlx, -111), 5.6f, brass); ShadeBall(P(hrx, -111), 5.6f, brass);                     // cuffs
    ShadeBall(P(hlx, -106), 7, glove);
    ShadeBall(P(hrx, -106), 7, glove);
    DrawRing(P(hlx - 3, -103), 1.6f * k, 2.6f * k, 0, 360, 8, brass); DrawRing(P(hrx + 3, -103), 1.6f * k, 2.6f * k, 0, 360, 8, brass); // rings on the gloves
    for (int c = 0; c < 4; c++) DrawRectanglePro({P(0, -106).x + shuf * k * (c % 2 ? 0.6f : -0.6f), P(0, -106 - c * 1.2f).y, 13 * k, 18 * k}, {6.5f * k, 9 * k}, 0, c == 3 ? Color{228, 196, 148, 255} : Color{90, 40, 40, 255}); // the deck between his hands
    ShadeBall(P(0, -124 + br), 4.5f, brass); DrawCircleV(P(0, -124 + br), 1.9f * k, Color{90, 200, 220, 255}); // a brooch at the throat, its gem the table's glow
    DrawLineEx(P(0, -121 + br), P(-16, -100), 0.9f * k, brass); DrawCircleV(P(-16, -99), 2.1f * k, brass); // a watch chain
    EndFigure(feet);
    if (fmodf(t, 4.3f) >= 0.13f) for (int e = -1; e <= 1; e += 2) { // his eyes catch the light
        Vector2 eye{feet.x + (P(e * 6.5f, -159).x - o.x), feet.y + (P(e * 6.5f, -159).y - o.y)};
        Glow(eye, 18 * k, Color{150, 170, 255, 110});
    }
    float fl = fmodf(t, 6.5f);
    if (fl < 1.2f) { // now and then he flicks a card into the air and catches it
        float u = fl / 1.2f;
        Vector2 c0{feet.x + (P(0, -110).x - o.x), feet.y + (P(0, -110).y - o.y)};
        Vector2 cp{c0.x + sinf(u * PI) * 8 * k, c0.y - sinf(u * PI) * 70 * k};
        DrawRectanglePro({cp.x, cp.y, 12 * k, 17 * k}, {6 * k, 8.5f * k}, u * 720, (int)(u * 4) % 2 ? Color{228, 196, 148, 255} : Color{90, 40, 40, 255});
    }
    Glow({feet.x + (P(0, -124).x - o.x), feet.y + (P(0, -124).y - o.y)}, 10 * k, Color{90, 200, 220, 120}); // the brooch glows
}
void DrawCardTable(float t) {
    Billboard(CARD_X, 0, CARD_Z, [&] {
        DrawEllipse(0, -3, 64, 12, Color{20, 14, 10, 255});                                         // foot
        DrawRectangleGradientH(-16, -104, 16, 104, Color{34, 22, 16, 255}, Color{76, 50, 34, 255}); // pedestal
        DrawRectangleGradientH(0, -104, 16, 104, Color{76, 50, 34, 255}, Color{28, 18, 12, 255});
        DrawEllipse(0, -104, 100, 25, Color{28, 18, 12, 255});                                      // the top and its rim
        DrawEllipse(0, -106, 96, 21, Color{74, 48, 32, 255});
        DrawEllipse(0, -107, 84, 17, Color{10, 34, 44, 255});                                       // felt, a deep cold teal
        for (int k = 0; k < 3; k++) { // three cards laid out
            float x = -34 + k * 34.0f, y = -107 + (k % 2) * 3.0f;
            DrawRectanglePro({x, y, 22, 9}, {11, 4.5f}, -14 + k * 14.0f, Color{228, 196, 148, 255});
            DrawRectanglePro({x, y, 22, 9}, {11, 4.5f}, -14 + k * 14.0f, Fade(BLACK, 0.0f));
        }
        for (int c = -1; c <= 1; c += 2) { // a candle at either end of the table
            float x = c * 66.0f;
            DrawRectangle((int)x - 3, -128, 6, 22, Color{236, 228, 200, 255});
            float flare = 1 + 0.9f * gHov[8];
            DrawEllipse((int)x, -132 - 3 * gHov[8], 3.2f * flare, (6.5f + sinf(t * 10 + c) * 1.2f) * flare, Color{255, 210, 120, 255});
            Glow({x, -132}, 34 * flare, Color{255, 190, 100, (unsigned char)(90 + 60 * gHov[8])});
        }
        DrawBrassPlate({-46, -60, 92, 22}, "FLATS", 14);
    });
}
// ---------------------------------------------------------------- the Deep Arcade cabinet, the Study hatch, the near lamps
// The Deep Arcade: mahogany panels in tarnished brass, riveted into the near bulkhead, copper pipes running into the
// wall, a porthole screen playing its attract loop, a marquee of etched glass lit by a tank of jellyfish, a throttle
// lever and valve wheels for controls, two pressure gauges (players online, open lobbies), and a steam vent.
void DrawArcadeCabinet(float t) {
    float h = gHov[ST_ARCADE];
    Billboard(ARC_X, 0, ARC_Z, [&] {
        rlPushMatrix(); rlScalef(0.6f, 0.6f, 1);     // a cabinet a man stands at, not a wardrobe
        Color mahog{96, 46, 30, 255}, mahogLt{140, 70, 42, 255}, brass{150, 112, 56, 255};
        DrawEllipse(0, -2, 76, 12, Color{20, 12, 8, 255});
        DrawRectangleGradientH(-62, -262, 62, 262, Tone(mahog, -0.3f), mahogLt);   // the body
        DrawRectangleGradientH(0, -262, 62, 262, mahogLt, Tone(mahog, -0.35f));
        DrawRectangleLinesEx({-62, -262, 124, 262}, 5, brass);
        for (int k = 0; k < 9; k++) { DrawCircleV({-56, -250 + k * 30.0f}, 2.2f, Pal::BrassDk); DrawCircleV({56, -250 + k * 30.0f}, 2.2f, Pal::BrassDk); }
        // the marquee: etched glass, backlit by jellyfish drifting in a tank
        Rectangle mq{-58, -312, 116, 50};
        DrawRectangleRec({mq.x - 4, mq.y - 4, mq.width + 8, mq.height + 8}, brass);
        DrawVGradient(mq, Color{14, 60, 72, 255}, Color{6, 24, 34, 255});
        for (int j = 0; j < 3; j++) { // the jellyfish
            float jx = -36 + j * 36 + sinf(t * 0.7f + j * 2) * 6, jy = -290 + sinf(t * 1.1f + j) * 6, pulse = 0.85f + 0.15f * sinf(t * 3 + j);
            Color jc{150, 220, 255, (unsigned char)(150 + 100 * h)};
            DrawCircleSector({jx, jy}, 8 * pulse, 180, 360, 12, jc);
            for (int k = 0; k < 4; k++) DrawLineEx({jx - 6 + k * 4.0f, jy}, {jx - 6 + k * 4.0f + sinf(t * 2 + k) * 2, jy + 12}, 1, jc);
            Glow({jx, jy}, 26 + 14 * h, Color{120, 210, 255, (unsigned char)(50 + 70 * h)});
        }
        TxtBold("THE DEEP ARCADE", -52, -272, 11, Color{220, 240, 236, 220});
        // the porthole screen, playing the attract loop of its four games
        Vector2 sc{0, -196};
        DrawCircleV(sc, 44, Pal::BrassDk);
        DrawRing(sc, 36, 44, 0, 360, 36, brass);
        for (int k = 0; k < 10; k++) { float a = k * PI / 5; DrawCircleV({sc.x + cosf(a) * 40, sc.y + sinf(a) * 40}, 2.4f, Pal::BrassDk); }
        DrawCircleV(sc, 35, Color{16, 84, 90, 255});
        Glow(sc, 60, Color{80, 230, 220, 60});
        int game = (int)fmodf(t / 3.0f, 4.0f);
        Color scr{150, 255, 236, 255};
        switch (game) {
            case 0: DrawRectangleLinesEx({-18, -210, 16, 24}, 2, scr); DrawRectangleLinesEx({2, -206, 16, 24}, 2, scr); break;                  // Flats Duel: two cards
            case 1: DrawLineEx({-20, -196}, {18, -196}, 2, scr); DrawTri({-14, -196}, {10, -196}, {-2, -188}, scr); DrawLineEx({8, -196}, {12, -176}, 1, scr); break; // the Trawl
            case 2: for (int c = 0; c < 3; c++) { float cx = -16 + fmodf(t * 20 + c * 11, 32.0f); DrawCircleV({cx, -188 - c * 8.0f}, 3, scr); } break;       // Scuttle: crabs racing
            default: DrawTri({-20, -182}, {-4, -182}, {-12, -196}, scr); DrawTri({2, -182}, {20, -182}, {11, -200}, scr); DrawLineEx({11, -200}, {11, -212}, 2, scr); break; // Fathoms
        }
        DrawCircleSector(sc, 33, 200, 240, 12, Fade(WHITE, 0.08f));
        // the controls: a slanted deck with a ship's throttle and two valve wheels
        DrawTri({-62, -140}, {62, -140}, {62, -118}, Tone(mahog, -0.2f));
        DrawTri({-62, -140}, {62, -118}, {-62, -118}, Tone(mahog, -0.2f));
        float lever = -0.4f + 0.25f * sinf(t * 0.8f) * (0.3f + h);
        DrawLineEx({-30, -128}, {-30 + sinf(lever) * 26, -128 - cosf(lever) * 26}, 4, Color{60, 60, 64, 255});
        DrawCircleV({-30 + sinf(lever) * 26, -128 - cosf(lever) * 26}, 5, Color{170, 40, 36, 255});
        for (int w = 0; w < 2; w++) {
            Vector2 wc{12 + w * 26.0f, -130};
            float a0 = Spin(3 + w, 0.4f + 3 * h) * (w ? -1 : 1);
            DrawRing(wc, 7, 9, 0, 360, 16, Color{150, 40, 36, 255});
            for (int k = 0; k < 3; k++) DrawLineEx(wc, {wc.x + cosf(a0 + k * 2.09f) * 8, wc.y + sinf(a0 + k * 2.09f) * 8}, 1.5f, Color{150, 40, 36, 255});
        }
        // the pressure gauges: players online, open lobbies (they twitch when you look)
        DrawGauge({-30, -88}, 13, 0.3f + h * 0.08f * sinf(t * 23), Color{236, 228, 206, 255});
        DrawGauge({30, -88}, 13, 0.55f + h * 0.08f * sinf(t * 19 + 1), Color{236, 228, 206, 255});
        // the token slot and its pneumatic tube, and a vent that puffs steam now and then
        DrawRectangle(-6, -60, 12, 5, Color{20, 16, 14, 255});
        DrawPipeV(50, -262, -60, 4, Tone(brass, 0.1f));
        DrawRectangle(-40, -30, 80, 10, Color{40, 30, 24, 255});
        if (fmodf(t, 7.3f) < 1.2f) for (int k = 0; k < 4; k++) { float ph = fmodf(t, 7.3f) / 1.2f; DrawCircleV({-40 - ph * 40 - k * 6, -25 - ph * 30 - k * 4}, 6 + ph * 8, Fade(WHITE, 0.25f * (1 - ph))); }
        rlPopMatrix();
    });
}

// The hatch to the Study: a brass ring in the deck with a wheel lock, a glowing chevron in front of it.
Rectangle DrawStudyHatch(float t) {
    float h = gHov[ST_STUDY];
    Vector2 c = Proj(0, 0, HATCH_Z), f = Proj(0, 0, HATCH_Z - 70), b = Proj(0, 0, HATCH_Z + 70);
    float rx = 82 * Px(HATCH_Z), ry = (f.y - b.y) * 0.5f * 82 / 70;
    DrawEllipse((int)c.x, (int)c.y, rx + 10, ry + 6, Color{30, 22, 16, 255});
    DrawEllipse((int)c.x, (int)c.y, rx + 6, ry + 4, Pal::BrassDk);
    DrawEllipse((int)c.x, (int)c.y, rx, ry, Pal::Brass);
    DrawEllipse((int)c.x, (int)c.y, rx - 6, ry - 4, Color{92, 96, 94, 255});                                  // the steel cover
    for (int k = 0; k < 12; k++) { float a = k * PI / 6; DrawCircleV({c.x + cosf(a) * (rx - 3), c.y + sinf(a) * (ry - 2)}, 2, Pal::BrassDk); }
    if (h > 0.02f) { // light rises from below as the wheel turns
        BeginBlendMode(BLEND_ADDITIVE);
        DrawEllipse((int)c.x, (int)c.y, rx * (0.4f + 0.6f * h), ry * (0.4f + 0.6f * h), Color{255, 200, 120, (unsigned char)(60 * h)});
        EndBlendMode();
        Glow({c.x, c.y - 20}, 90 * h, Color{255, 200, 120, (unsigned char)(90 * h)});
    }
    float a0 = Spin(5, 0.2f + 2.5f * h);
    for (int k = 0; k < 4; k++) DrawLineEx(c, {c.x + cosf(a0 + k * PI / 2) * rx * 0.45f, c.y + sinf(a0 + k * PI / 2) * ry * 0.45f}, 4, Color{150, 40, 36, 255});
    DrawEllipseLines((int)c.x, (int)c.y, rx * 0.45f, ry * 0.45f, Color{150, 40, 36, 255});
    DrawEllipse((int)c.x, (int)c.y, 6, 4, Pal::Brass);
    // the chevron on the deck in front, pointing down, glowing
    float pulse = 0.55f + 0.45f * sinf(t * 2.4f);
    Vector2 cv = Proj(0, 0, HATCH_Z - 100);
    Color gc{255, 200, 110, (unsigned char)(140 + 100 * pulse)};
    DrawTri({cv.x - 22, cv.y - 6}, {cv.x + 22, cv.y - 6}, {cv.x, cv.y + 8}, gc);
    DrawTri({cv.x - 12, cv.y - 6}, {cv.x + 12, cv.y - 6}, {cv.x, cv.y + 2}, Color{60, 40, 20, 255});
    Glow(cv, 26, Color{255, 190, 100, (unsigned char)(60 * pulse)});
    return {c.x - rx - 10, c.y - ry - 30, rx * 2 + 20, ry * 2 + 60};
}

// Two oil lamps hang in the foreground, swaying with the ship's roll.
float ShipRoll(float t) { return 0.055f * sinf(t * 0.45f) + 0.015f * sinf(t * 1.3f); }
Vector2 NearLampPos(int i, float t) {
    float X = i ? 170.0f : -170.0f, Z = 330, L = 170;
    Vector2 top = Proj(X, RH, Z);
    float a = ShipRoll(t + i * 0.3f), k = Px(Z);
    return {top.x + sinf(a) * L * k, top.y + cosf(a) * L * k};
}
void DrawNearLamps(float t) {
    for (int i = 0; i < 2; i++) {
        float X = i ? 170.0f : -170.0f, Z = 330, k = Px(Z);
        Vector2 top = Proj(X, RH, Z), lp = NearLampPos(i, t);
        for (int c = 0; c < 10; c++) { Vector2 p{top.x + (lp.x - top.x) * c / 10, top.y + (lp.y - top.y) * c / 10}; DrawRing(p, 1.5f * k, 3 * k, 0, 360, 8, Pal::BrassDk); }
        DrawEllipse((int)lp.x, (int)(lp.y + 6 * k), 18 * k, 5 * k, Pal::BrassDk);                  // the cap
        DrawRectangleRounded({lp.x - 14 * k, lp.y + 8 * k, 28 * k, 34 * k}, 0.4f, 6, Color{255, 214, 150, 230});   // the glass
        for (int s = -1; s <= 1; s++) DrawLineEx({lp.x + s * 12 * k, lp.y + 8 * k}, {lp.x + s * 12 * k, lp.y + 42 * k}, 2 * k, Pal::BrassDk); // the cage
        DrawEllipse((int)lp.x, (int)(lp.y + 44 * k), 16 * k, 5 * k, Pal::BrassDk);
        DrawEllipse((int)lp.x, (int)(lp.y + 26 * k), 4 * k, 8 * k + sinf(t * 11 + i) * k, Color{255, 240, 200, 255});
    }
}

// Light off the great window, rippling across the ceiling.
void DrawCeilingCaustics(float t) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 14; i++) {
        float z0 = 760 + i * 28.0f;
        Vector2 prev{};
        bool have = false;
        for (float x = -RW + 40; x <= RW - 40; x += 40) {
            float z = z0 + sinf(x * 0.012f + t * 0.9f + i) * 14 + sinf(x * 0.031f - t * 1.3f) * 6;
            if (fabsf(x) > ApseHalfW(z) - 30 || z > Z_BACK - 20) { have = false; continue; }   // past the walls of the apse
            Vector2 p = Proj(x, RH, z);
            if (have) DrawLineEx(prev, p, 1.6f, Color{120, 200, 220, (unsigned char)(14 + (i % 3) * 5)});
            prev = p; have = true;
        }
    }
    EndBlendMode();
}

// Bubbles rising out of the periscope well (drawn after the ink pass: bright specks).
void DrawPeriscopeBubbles(float t) {
    Vector2 base = Proj(PERI_X, 0, PERI_Z);
    float k = Px(PERI_Z);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 9; i++) {
        float ph = fmodf(t * 0.35f + i * 0.111f, 1.0f);
        DrawCircleV({base.x + (sinf(t * 2 + i * 1.7f) * 10 + (i % 3 - 1) * 22) * k, base.y - ph * 300 * k}, (1.6f + (i % 3)) * k, Color{200, 236, 244, (unsigned char)(120 * (1 - ph))});
    }
    EndBlendMode();
}
// ---------------------------------------------------------------- lighting
void DrawSalonLighting(float t, int hovered, const std::vector<Vector2>& personLights) {
    LightsBegin(Color{118, 118, 122, 255});
    // a soft warm fill on whoever's out on the floor, so the crew and cat read as lit figures wherever they
    // walk instead of going nearly silhouette-black between the room's fixed light pools
    for (const Vector2& at : personLights) AddLight(at, 250, Color{255, 220, 176, 255}, 1.0f);
    Color warm{255, 206, 140, 255}, sea{70, 150, 170, 255};
    float flick = 0.93f + 0.07f * sinf(t * 9) * sinf(t * 3.1f);
    Vector2 ch = Proj(0, 450, 760);
    AddLight(ch, 620, warm, 0.8f * flick);
    AddCone(ch, PI / 2, 0.9f, 460, Color{150, 118, 78, 255});
    AddLight(Proj(0, 330, Z_BACK), 330, sea, 0.95f);
    AddLight(Proj(0, 60, 1000), 260, sea, 0.45f);
    AddLight({CX, 640}, 520, warm, 0.4f); // lamplight spilling across the near floor
    const Vector3 SCONCE[4] = {{APSE[1].x, 400, APSE[1].y}, {APSE[4].x, 400, APSE[4].y}, {-RW + 10, 400, 640}, {RW - 10, 400, 640}};  // wall sconces on the pilasters
    for (const Vector3& sc : SCONCE) AddLight(Proj(sc), 440 * Px(sc.z), warm, 0.7f * flick);
    AddLight(Proj(ORGAN_X, 170, ALCOVE_Z), 150, warm, 0.8f);     // organ candles
    AddLight(Proj(RW, 440, 620), 180, Color{80, 255, 140, 255}, 0.5f); // the radar's glow
    AddLight(Proj(CARD_X, 110, CARD_Z), 190, Color{80, 170, 220, 255}, 0.7f);          // the card table's cold glow
    AddLight(Proj(CARD_X - 60, 130, CARD_Z), 90, Color{255, 190, 110, 255}, 0.6f);      // and its candle
    AddCone(Proj(420, 470, 1060), PI / 2, 0.45f, 180, Color{255, 250, 232, 255}); // surgical lamp
    if (fmodf(t, 6) < 2.2f) AddLight(Proj(630, 90, 860), 120, Color{255, 170, 80, 255}, 0.7f); // the grinder's sparks
    for (int i = 0; i < 2; i++) AddLight(NearLampPos(i, t), 260, Color{255, 206, 140, 255}, 0.55f * flick);   // the near lamps
    AddLight(Proj(ARC_X, 200, ARC_Z), 150 + 60 * gHov[ST_ARCADE], Color{90, 220, 230, 255}, 0.6f + 0.4f * gHov[ST_ARCADE]); // the arcade's glow
    AddLight(Proj(WARD_X, 470, ALCOVE_Z), 120, Color{255, 250, 232, 255}, 0.3f + 0.7f * gHov[ST_WARD]);                    // the ward's lamp brightens
    if (hovered >= 0) {
        Rectangle r = stationRect[hovered];
        AddLight({r.x + r.width / 2, r.y + r.height / 2}, std::max(r.width, r.height) * 0.8f + 60, Color{255, 230, 190, 255}, 0.4f);
    }
    LightsEnd();
    for (int i = 0; i < 2; i++) Glow(NearLampPos(i, t), 70, Color{255, 200, 120, 110});
    Glow(ch, 80, Color{255, 200, 120, 90});
    for (const Vector3& sc : SCONCE) Glow(Proj(sc), 48 * Px(sc.z), Color{255, 200, 120, 150});
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

// ---------------------------------------------------------------- HUD: Darkest Dungeon's framing
// The roster along the right edge, the ship's stores at the bottom left, the four party slots and one Embark
// button at the bottom. Drag a crew member from the roster onto a party slot; drag them off to remove them.
int gDragId = -1;           // the crew member being dragged (-1: none)
Vector2 gDragFrom{};
bool gDragMoved = false, gDragFromParty = false;
int gDebugHover = -1;       // --shots: pretend this station is hovered
Rectangle RosterCard(int i) { return {ROSTER_X, 86 + i * 46.0f, 146, 43}; }
Rectangle PartySlot(int k) { return {262 + k * 118.0f, HUD_Y + 7, 112, 54}; }
Rectangle EmbarkRect() { return {742, HUD_Y + 9, 186, 50}; }

bool Injured(const Hero& h) { return h.hp < GetStats(h).maxHp * 0.6f; }

// A small ink plate, as on the combat HUD
void InkPlate(Rectangle r, Color edge) {
    DrawRectangleRounded({r.x + 2, r.y + 3, r.width, r.height}, 0.18f, 6, Fade(BLACK, 0.5f));
    DrawRectangleRounded(r, 0.18f, 6, Color{18, 16, 14, 238});
    DrawRectangleRoundedLinesEx(r, 0.18f, 6, 1.6f, edge);
}

void DrawStatusGlyph(const Hero& h, Vector2 c) {
    if (h.onLeave > 0) { TxtBold("z", c.x - 4, c.y - 9, 16, Color{160, 170, 220, 255}); return; }
    if (h.rattled) { DrawRing(c, 3, 6, 0, 300, 10, Pal::Stress); return; }
    if (Injured(h)) { DrawRectangle((int)c.x - 1, (int)c.y - 5, 3, 11, Pal::Bad); DrawRectangle((int)c.x - 5, (int)c.y - 1, 11, 3, Pal::Bad); return; }
    DrawCircleV(c, 4, Pal::Good);
}

void DrawRosterCard(Game& g, const Hero& h, Rectangle r, float t, bool hot) {
    int rank = -1;
    for (int k = 0; k < PARTY_SIZE; k++) if (g.party[k] == h.id) rank = k;
    InkPlate(r, hot ? Pal::Brass : rank >= 0 ? Pal::BrassDk : Color{70, 60, 46, 255});
    Rectangle pr{r.x + 3, r.y + 3, 38, r.height - 6};
    DrawRectangleRec(pr, Color{38, 44, 46, 255});
    DrawPortrait(h, pr, t);
    DrawRectangleLinesEx(pr, 1, Pal::BrassDk);
    TxtBold(h.name, r.x + 46, r.y + 2, 13, Pal::Paper);
    Txt(ClassName(h.cls), r.x + 46, r.y + 17, 10, Color{190, 176, 140, 255});
    for (int l = 0; l < 6; l++) DrawCircleV({r.x + 50 + l * 7.0f, r.y + 32}, 2.2f, l < h.level ? Pal::Brass : Color{60, 54, 44, 255}); // level pips
    Stats s = GetStats(h);
    DrawBar({r.x + 94, r.y + 29, 44, 4}, (float)h.hp / s.maxHp, Pal::Good);
    DrawBar({r.x + 94, r.y + 35, 44, 3}, h.stress / 100.0f, Pal::Stress);
    DrawStatusGlyph(h, {r.x + r.width - 10, r.y + 11});
    if (rank >= 0) { DrawCircleV({r.x + r.width - 26, r.y + 11}, 7, Pal::BrassDk); TxtBold(std::to_string(rank + 1), r.x + r.width - 29, r.y + 4, 12, Pal::Paper); }
    if (h.rattled || h.onLeave > 0 || Injured(h)) { // dimmed, with an ink cross
        DrawRectangleRounded(r, 0.18f, 6, Color{0, 0, 0, 110});
        DrawLineEx({pr.x + 4, pr.y + 4}, {pr.x + pr.width - 4, pr.y + pr.height - 4}, 3, Color{12, 8, 8, 220});
        DrawLineEx({pr.x + pr.width - 4, pr.y + 4}, {pr.x + 4, pr.y + pr.height - 4}, 3, Color{12, 8, 8, 220});
    }
}

void DrawSalonHud(Game& g, int hovered, const std::string& hint, bool active) {
    float t = g.time;
    Vector2 m = GetMousePosition();
    // the top bar: just the ship's name and where she is
    DrawVGradient({0, 0, (float)SCREEN_W, 46}, Color{8, 12, 14, 235}, Color{16, 22, 26, 215});
    DrawRectangle(0, 44, SCREEN_W, 2, Pal::BrassDk);
    TxtShadow("THE NAUTILUS", 20, 9, 28, Pal::Brass, true);
    Txt(TextFormat("The grand salon   -   Depth %.0f fathoms", 212 + sinf(g.time * 0.05f) * 3), 262, 16, 15, Color{180, 190, 186, 255});
    // the hint, above the bottom bar
    float hw = (float)MeasureTxt(hint, 18);
    float hx = std::min(ROSTER_X - 12 - hw / 2 - 16, CX);
    DrawRectangleRounded({hx - hw / 2 - 16, HUD_Y - 38, hw + 32, 30}, 0.5f, 8, Color{8, 12, 14, 185});
    TxtShadow(hint, hx - hw / 2, HUD_Y - 33, 18, active ? Pal::Paper : Color{200, 196, 180, 200});
    // the bottom bar
    DrawVGradient({0, HUD_Y, (float)SCREEN_W, SCREEN_H - HUD_Y}, Color{14, 12, 10, 240}, Color{6, 6, 6, 252});
    DrawRectangle(0, (int)HUD_Y, SCREEN_W, 2, Pal::BrassDk);
    // the ship's stores, bottom left
    Rectangle res{12, HUD_Y + 8, 238, 52};
    InkPlate(res, Pal::BrassDk);
    DrawCircle((int)res.x + 20, (int)res.y + 18, 9, Pal::Brass); DrawCircle((int)res.x + 17, (int)res.y + 15, 3, Color{255, 240, 190, 255});
    TxtBold(TextFormat("%d", g.gold), res.x + 34, res.y + 8, 19, Pal::Brass);
    DrawRectangle((int)res.x + 110, (int)res.y + 10, 10, 16, Color{90, 120, 90, 255}); DrawRectangle((int)res.x + 112, (int)res.y + 7, 6, 3, Pal::BrassDk); // a battery
    Txt(TextFormat("%d", g.batteries), res.x + 126, res.y + 9, 17, Pal::Paper);
    DrawPoly({res.x + 178, res.y + 18}, 4, 8, 45, Color{150, 110, 200, 255});                                                         // a stored relic
    Txt(TextFormat("%d", (int)g.relicStorage.size()), res.x + 192, res.y + 9, 17, Pal::Paper);
    Txt("gold          batteries      relics", res.x + 14, res.y + 33, 11, Color{150, 140, 116, 255});
    // the four party slots, drop targets
    int partyCount = 0;
    for (int k = 0; k < PARTY_SIZE; k++) {
        Rectangle c = PartySlot(k);
        Hero* h = FindHero(g, g.party[k]);
        if (h) partyCount++;
        bool over = gDragId >= 0 && gDragMoved && CheckCollisionPointRec(m, c);
        InkPlate(c, over ? Pal::Brass : Pal::BrassDk);
        if (!h) { DrawTextCentered(TextFormat("Rank %d", k + 1), c.x + c.width / 2, c.y + 12, 14, Color{120, 112, 96, 255}); DrawTextCentered("drag crew here", c.x + c.width / 2, c.y + 30, 11, Color{100, 94, 80, 255}); continue; }
        Rectangle pr{c.x + 4, c.y + 4, 40, c.height - 8};
        DrawRectangleRec(pr, Color{38, 44, 46, 255});
        if (!(gDragId == h->id && gDragMoved)) DrawPortrait(*h, pr, t);
        DrawRectangleLinesEx(pr, 1, Pal::BrassDk);
        TxtBold(h->name, c.x + 49, c.y + 5, 12, Pal::Paper);
        Txt(TextFormat("Lv %d", h->level), c.x + 49, c.y + 21, 11, Color{190, 176, 140, 255});
        Stats s = GetStats(*h);
        DrawBar({c.x + 49, c.y + 38, 56, 4}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({c.x + 49, c.y + 44, 56, 3}, h->stress / 100.0f, Pal::Stress);
        TxtBold(std::to_string(k + 1), c.x + c.width - 14, c.y + 4, 12, Pal::BrassDk);
    }
    // Embark: red and brass, lit only when the party is full
    Rectangle e = EmbarkRect();
    bool ready = partyCount == PARTY_SIZE, hotE = ready && CheckCollisionPointRec(m, e);
    DrawRectangleRounded({e.x + 3, e.y + 4, e.width, e.height}, 0.3f, 8, Fade(BLACK, 0.5f));
    DrawRectangleRounded(e, 0.3f, 8, ready ? (hotE ? Color{190, 50, 40, 255} : Color{150, 36, 30, 255}) : Color{60, 30, 28, 255});
    DrawRectangleRoundedLinesEx(e, 0.3f, 8, 3, ready ? Pal::Brass : Color{90, 70, 50, 255});
    if (ready) Glow({e.x + e.width / 2, e.y + e.height / 2}, 70, Color{255, 120, 80, (unsigned char)(40 + 30 * sinf(t * 3))});
    DrawTextCenteredBold("EMBARK", e.x + e.width / 2, e.y + 8, 22, ready ? Pal::Paper : Color{140, 120, 100, 255});
    DrawTextCentered(ready ? "to the Helm" : TextFormat("party %d / 4", partyCount), e.x + e.width / 2, e.y + 32, 12, ready ? Color{240, 210, 170, 255} : Color{130, 110, 90, 255});
    // the roster, along the right edge
    DrawVGradient({ROSTER_X - 8, 46, (float)SCREEN_W - ROSTER_X + 8, HUD_Y - 46}, Color{10, 10, 10, 150}, Color{10, 10, 10, 210});
    DrawRectangle((int)ROSTER_X - 8, 46, 2, (int)HUD_Y - 46, Pal::BrassDk);
    TxtBold(TextFormat("ROSTER  %d / %d", (int)g.roster.size(), MaxRoster(g)), ROSTER_X + 6, 58, 15, Pal::Brass);
    for (size_t i = 0; i < g.roster.size() && i < 12; i++) {
        Rectangle r = RosterCard((int)i);
        const Hero& h = g.roster[i];
        if (gDragId == h.id && gDragMoved) { InkPlate(r, Color{50, 44, 36, 255}); continue; }
        DrawRosterCard(g, h, r, t, CheckCollisionPointRec(m, r) && gDragId < 0);
    }
    // the portrait being dragged
    if (gDragId >= 0 && gDragMoved)
        if (Hero* h = FindHero(g, gDragId)) {
            Rectangle r{m.x - 20, m.y - 26, 40, 50};
            InkPlate({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, Pal::Brass);
            DrawPortrait(*h, r, t);
        }
    (void)hovered;
}

// Mouse handling for the roster, the party slots and Embark. Returns true if the mouse is busy with them.
bool SalonHudInput(Game& g) {
    Vector2 m = GetMousePosition();
    auto slotAt = [&](Vector2 p) { for (int k = 0; k < PARTY_SIZE; k++) if (CheckCollisionPointRec(p, PartySlot(k))) return k; return -1; };
    auto rosterAt = [&](Vector2 p) { for (size_t i = 0; i < g.roster.size() && i < 12; i++) if (CheckCollisionPointRec(p, RosterCard((int)i))) return (int)i; return -1; };
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { int k = slotAt(m); if (k >= 0) { g.party[k] = -1; return true; } }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int i = rosterAt(m), k = slotAt(m);
        if (i >= 0) { gDragId = g.roster[i].id; gDragFrom = m; gDragMoved = false; gDragFromParty = false; PlayCue("ui.drag"); return true; }
        if (k >= 0 && g.party[k] >= 0) { gDragId = g.party[k]; gDragFrom = m; gDragMoved = false; gDragFromParty = true; return true; }
        if (CheckCollisionPointRec(m, EmbarkRect())) {
            int n = 0; for (int id : g.party) if (id >= 0) n++;
            if (n == PARTY_SIZE) { g.scene = Scene::Helm; PlayCue("hub.embark"); PlayCue("hub.tilt", 0.7f); }
            else { Toast(g, "Embark needs a full party of four: drag crew from the roster onto the slots."); PlayCue("ui.error"); }
            return true;
        }
    }
    if (gDragId >= 0) {
        if (fabsf(m.x - gDragFrom.x) + fabsf(m.y - gDragFrom.y) > 6) gDragMoved = true;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            Hero* h = FindHero(g, gDragId);
            if (!gDragMoved && h) { g.selectedHero = h->id; g.scene = Scene::Crew; }  // a click: open Crew Quarters on them
            else if (h) {
                int k = slotAt(m), from = -1;
                for (int j = 0; j < PARTY_SIZE; j++) if (g.party[j] == h->id) from = j;
                if (k >= 0) {
                    if (h->onLeave > 0) { Toast(g, h->name + " is resting in the Sick Bay and sits this voyage out."); PlayCue("ui.error"); }
                    else { if (from >= 0) g.party[from] = g.party[k]; g.party[k] = h->id; PlayCue("ui.drop"); }
                } else if (gDragFromParty && from >= 0 && m.y < HUD_Y) g.party[from] = -1;   // dragged off the bar: out of the party
            }
            gDragId = -1; gDragMoved = false;
        }
        return true;
    }
    return m.y >= HUD_Y || m.x >= ROSTER_X - 8;
}}  // namespace

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
// live: the hub itself (hover, clicks, the HUD). Otherwise the room is a backdrop behind a station's panel, with that
// station held in its "hovered" motion (the periscope's handles out, the organ humming ...).
static void SalonFrame(Game& g, bool live, int heldStation) {
    float dt = GetFrameTime(), t = g.time;
    SetPost(0.5f, 0.03f, 0.4f);
    SetSceneLight(SalonLight());               // oil lamps overhead, the window's blue fill, candle rim light
    SetInkLook(&SalonPalette(), 0.22f, 77);
    Vector2 m = GetMousePosition();
    bool hudBusy = live && SalonHudInput(g);
    bool mouseInRoom = live && !hudBusy && m.y > 46 && m.y < HUD_Y && m.x < ROSTER_X - 8;
    // the camera drifts opposite the cursor: up to ~1.5% of the screen at the near plane
    if (live) {
        float tx = std::clamp((m.x - CX) / 640.0f, -1.0f, 1.0f) * 13, ty = std::clamp((m.y - 360) / 360.0f, -1.0f, 1.0f) * -8;
        gCamX += (tx - gCamX) * std::min(1.0f, dt * 3); gCamY += (ty - gCamY) * std::min(1.0f, dt * 3);
    }
    UpdateLife(g, dt);
    std::vector<const Hero*> resting;
    for (auto& h : g.roster) if (h.onLeave > 0) resting.push_back(&h);

    // --- the far band: the window, the back wall, the organ, the ward
    DrawOcean(t);
    DrawShell(t);
    DrawCeilingCaustics(t);
    DrawGreatWindow(t);
    DrawOrgan(t, !resting.empty());
    DrawWardWall(t);
    Vector2 organBase = Proj(ORGAN_X, 0, ALCOVE_Z), wardBase = Proj(WARD_X, 0, ALCOVE_Z);
    float kb = Px(ALCOVE_Z);
    stationRect[ST_CREW] = PaintWall(-RW, 540, 700, 0, 240, 645, [&](float w, float h) { ArtCrewDoor(w, h, t); });
    stationRect[ST_LIBRARY] = PaintPanel(APSE[0], APSE[1], 0, 510, 750, [&](float w, float h) { ArtLibrary(w, h, t); });
    stationRect[ST_RADAR] = PaintWall(RW, 540, 700, 0, 240, 645, [&](float w, float h) { ArtRadar(w, h, t); });
    stationRect[ST_WORKSHOP] = PaintPanel(APSE[4], APSE[5], 0, 510, 645, [&](float w, float h) { ArtWorkshop(w, h, t); });
    DrawWallPipes(t);

    // --- the midground and the foreground: furniture and people on the floor, far to near, with veils of fog between
    struct Item { float z; std::function<void()> draw; };
    std::vector<Item> items;
    items.push_back({1040, [&] { FogVeil(0.05f); }});                     // fog between the far band and the midground
    items.push_back({1060, [&] { DrawChaise(t, resting); }});
    items.push_back({1060, [&] { DrawOperatingTable(t); }});
    items.push_back({900, [&] { DrawHelmFurniture(t); }});
    items.push_back({PERI_Z, [&] { DrawPeriscope(t); }});
    items.push_back({cat.perch == 1 ? ARC_Z - 1 : cat.pos.y, [&] { DrawCat(t); }});
    items.push_back({DEALER_Z, [&] { DrawCardDealer(t); }});
    items.push_back({CARD_Z, [&] { DrawCardTable(t); }});
    items.push_back({500, [&] { FogVeil(0.03f); }});                      // and a thinner one before the foreground
    items.push_back({ARC_Z, [&] { DrawArcadeCabinet(t); }});
    Rectangle hatchR{};
    items.push_back({HATCH_Z + 60, [&] { hatchR = DrawStudyHatch(t); }});
    struct Person { Walker* w; const Hero* h; Vector2 feet; float s; Rectangle r; Pose pose; };
    std::vector<Person> people;
    for (auto& w : walkers) {
        const Hero* h = &NpcHero(w.id);
        float s = CREW_H / 165.0f * Px(w.pos.y);
        Vector2 feet = Proj(w.pos.x, 0, w.pos.y);
        people.push_back({&w, h, feet, s, {feet.x - 22 * s, feet.y - 165 * s, 44 * s, 165 * s}, WorkPose(w, t)});
    }
    // now and then one of the off-duty hands is at the arcade cabinet, glancing back when you come near
    static Walker arcadeW{0, {ARC_X - 85, ARC_Z + 25}, {ARC_X - 85, ARC_Z + 25}, 5, 0, true, true};
    if (fmodf(t, 75) > 25 && fmodf(t, 75) < 55) {
        int who = -1;
        for (int i = 0; i < NPC_COUNT && who < 0; i++) { bool busy = false; for (auto& w : walkers) busy |= w.id == i; if (!busy) who = i; }
        if (who >= 0) {
            arcadeW.id = who;
            Vector2 feet = Proj(arcadeW.pos.x, 0, arcadeW.pos.y);
            float s = CREW_H / 165.0f * Px(arcadeW.pos.y);
            bool near = live && fabsf(m.x - feet.x) < 150 && m.y > feet.y - 260 && m.y < feet.y + 40;
            arcadeW.right = !near;                                   // playing, he faces the cabinet; he turns to look at you
            Pose p; p.reach = near ? 0.1f : 0.45f + 0.08f * sinf(t * 7); p.lean = near ? 0 : 0.14f; p.headDown = near ? -0.2f : 0.1f;
            people.push_back({&arcadeW, &NpcHero(who), feet, s, {feet.x - 22 * s, feet.y - 165 * s, 44 * s, 165 * s}, p});
        }
    }
    for (auto& p : people)
        items.push_back({p.w->pos.y, [&, pp = &p] {
            DrawShadowBlob(pp->feet, 30 * pp->s);
            DrawCrewFigureInked(*pp->h, pp->feet, pp->s, pp->w->right, pp->w->phase, t, pp->pose);
        }});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.z > b.z; });
    for (auto& it : items) it.draw();
    DrawChandelier(t);
    DrawCrossPipe(t);
    DrawNearLamps(t);

    // where the furniture stations sit on screen (for hovering)
    auto around = [&](float X, float Z, float w, float hgt) {
        Vector2 a = Proj(X - w / 2, hgt, Z), b = Proj(X + w / 2, 0, Z);
        return Rectangle{a.x, a.y, b.x - a.x, b.y - a.y};
    };
    stationRect[ST_HELM] = around(-20, 900, 380, 380);
    stationRect[ST_CARDS] = around(CARD_X, DEALER_Z - 40, 240, 200);
    stationRect[ST_PERISCOPE] = around(PERI_X, PERI_Z, 140, 420);
    stationRect[ST_ARCADE] = around(ARC_X, ARC_Z, 90, 200);
    stationRect[ST_STUDY] = hatchR;
    Rectangle organR{organBase.x - 170 * kb, organBase.y - 540 * kb, 340 * kb, 540 * kb}, chaise = around(-400, 1060, 330, 170);
    stationRect[ST_SICKBAY] = {std::min(organR.x, chaise.x), organR.y, std::max(organR.x + organR.width, chaise.x + chaise.width) - std::min(organR.x, chaise.x),
                               chaise.y + chaise.height - organR.y};
    Rectangle wardR{wardBase.x - 130 * kb, wardBase.y - 560 * kb, 260 * kb, 560 * kb}, table = around(420, 1060, 320, 170);
    stationRect[ST_WARD] = {std::min(wardR.x, table.x), wardR.y, std::max(wardR.x + wardR.width, table.x + table.width) - std::min(wardR.x, table.x),
                            table.y + table.height - wardR.y};

    // --- what's under the mouse: people first, then furniture, then the walls
    bool hovCat = mouseInRoom && CheckCollisionPointRec(m, catRect);
    const Person* hovPerson = nullptr;
    if (mouseInRoom && !hovCat)
        for (auto& p : people) if (CheckCollisionPointRec(m, p.r) && (!hovPerson || p.w->pos.y < hovPerson->w->pos.y)) hovPerson = &p;
    int hovered = -1;
    const int order[ST_COUNT] = {ST_STUDY, ST_ARCADE, ST_CARDS, ST_HELM, ST_PERISCOPE, ST_SICKBAY, ST_WARD, ST_CREW, ST_LIBRARY, ST_RADAR, ST_WORKSHOP};
    if (mouseInRoom && !hovPerson && !hovCat)
        for (int i : order) if (CheckCollisionPointRec(m, stationRect[i])) { hovered = i; break; }
    if (gDebugHover >= 0) hovered = gDebugHover;
    static int lastHover = -1;
    if (live && hovered != lastHover && hovered >= 0) { // a new station under the mouse: its plaque slides up, and it answers
        PlayCue("hub.plaque", 0.8f);
        const char* HOVER_CUE[ST_COUNT] = {"hub.door", "hub.ladder", "hub.sonar", "hub.wheel", "hub.periscope", "hub.gear", "hub.organ", "hub.candle", "hub.candle", "hub.jelly", "hub.hatch"};
        float pan = std::clamp((stationRect[hovered].x + stationRect[hovered].width / 2 - CX) / 640.0f, -1.0f, 1.0f);
        PlayCue(HOVER_CUE[hovered], 0.7f, pan);
        if (hovered == ST_ARCADE) PlayCue("hub.gauge", 0.5f, pan);
        if (hovered == ST_STUDY) PlayCue("hub.rungs", 0.5f, pan);
    }
    if (live) lastHover = hovered;
    for (int i = 0; i < ST_COUNT; i++) { // each station eases into and out of its hover motion
        float want = i == hovered || i == heldStation ? 1.0f : 0.0f;
        gHov[i] += (want - gHov[i]) * std::min(1.0f, dt * 6);
        if (gDebugHover >= 0 || !live) gHov[i] = want;
    }

    std::vector<Vector2> personLights;
    for (auto& p : people) personLights.push_back({p.feet.x, p.feet.y - 100 * p.s});
    personLights.push_back({Proj(cat.pos.x, 40 + cat.y, cat.pos.y)});
    DrawSalonLighting(t, live ? hovered : -1, personLights);
    for (auto& p : people)
        if (p.h->rattled) Glow({p.feet.x, p.r.y - 6}, 20 + sinf(t * 5) * 3, Fade(Pal::Stress, 0.5f));
    InkPass(1.0f, 1.0f);
    DrawDustMotes(t);
    DrawCeilingSteam(t);
    DrawPeriscopeBubbles(t);
    DrawPurrHearts(t);
    if (!live) return;

    // the hovered station: a warm edge and its brass plaque
    if (hovered >= 0) {
        Rectangle r = stationRect[hovered];
        float pulse = 0.45f + 0.35f * sinf(t * 4);
        DrawRectangleRoundedLinesEx({r.x - 6, r.y - 6, r.width + 12, r.height + 12}, 0.06f, 6, 2, Fade(Color{255, 214, 150, 255}, pulse * 0.6f));
        const char* nm = STATIONS[hovered].name;
        float pw = (float)MeasureTxt(nm, 17, true) + 44;
        float px = std::clamp(r.x + r.width / 2 - pw / 2, 8.0f, ROSTER_X - 16 - pw), py = std::max(52.0f, r.y - 38);
        DrawBrassPlate({px, py, pw, 30}, nm, 17);
    }
    // --- labels, hints, HUD
    std::string hint = "Every station in the salon can be clicked. Drag crew from the roster onto the party slots, then Embark.";
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

    // the game menu, for the mouse (Esc opens it too; a new game is started from there)
    if (Button({SCREEN_W - 170.0f, 8, 158, 30}, "Menu   (Esc)", true, 14)) GameMenuOpen();

    // --- clicks
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hovCat) {
        if (cat.purr <= 0) PlayPurr();
        cat.purr = 3.2f;
        cat.wait = std::max(cat.wait, 3.7f);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInRoom && !hovPerson && !hovCat && hovered >= 0) {
        g.scene = STATIONS[hovered].target;
        if (hovered == ST_ARCADE) PlayCue("hub.token");
        else if (hovered == ST_STUDY) { PlayCue("hub.hatch"); PlayCue("hub.rungs"); }
        else { PlayCue("hub.panel"); PlayCue("hub.latch"); }
        if (g.scene == Scene::Crew && !FindHero(g, g.selectedHero) && !g.roster.empty()) g.selectedHero = g.roster[0].id;
    }
}

void SceneHub(Game& g) {
    if (g.voyageEvent >= 0) { SalonFrame(g, false, -1); DrawVoyageEvent(g); return; }   // a voyage event holds the room until it's read
    SalonFrame(g, true, -1);
}
void ResetSalonLife() { walkers.clear(); }
void DebugSalonHover(int station) { gDebugHover = station; }
void DrawSalonBackdrop(Game& g, Scene station) {
    int held = -1;
    for (int i = 0; i < ST_COUNT; i++) if (STATIONS[i].target == station) held = i;
    SalonFrame(g, false, held);
}

// ============================================================ the Study (below the hatch): under refit for now
void SceneStudy(Game& g) {
    float t = g.time;
    SetPost(0.45f, 0.03f, 0.3f);
    SetSceneLight(SalonLight());
    SetInkLook(&SalonPalette(), 0.25f, 91);
    // a narrow iron room below the salon, lit by one lantern: a locked round door with a brass sign, the ladder back up
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{34, 30, 28, 255}, Color{12, 10, 10, 255});
    DrawTiled(Tex::Metal, {140, 60, 1000, 560}, 0.9f, Color{70, 74, 72, 255});
    for (int i = 0; i < 5; i++) { float x = 170 + i * 240.0f; DrawRectangleGradientH((int)x - 12, 60, 12, 560, Color{30, 32, 32, 255}, Color{80, 84, 82, 255}); DrawRectangleGradientH((int)x, 60, 12, 560, Color{80, 84, 82, 255}, Color{26, 28, 28, 255}); }
    DrawRectangle(0, 600, SCREEN_W, 120, Color{24, 18, 14, 255});
    DrawTiled(Tex::Wood, {0, 600, (float)SCREEN_W, 30}, 0.8f, Color{90, 60, 40, 255});
    Vector2 dc{700, 350};
    DrawCircleV({dc.x + 6, dc.y + 8}, 190, Fade(BLACK, 0.5f));
    DrawCircleV(dc, 190, Pal::BrassDk);
    DrawRing(dc, 168, 186, 0, 360, 64, Pal::Brass);
    DrawCircleV(dc, 166, Color{70, 64, 58, 255});
    for (int k = 0; k < 16; k++) { float a = k * PI / 8; DrawCircleV({dc.x + cosf(a) * 177, dc.y + sinf(a) * 177}, 4, Color{60, 44, 26, 255}); }
    for (int k = 0; k < 6; k++) { float a = k * PI / 3 + 0.3f; DrawLineEx(dc, {dc.x + cosf(a) * 70, dc.y + sinf(a) * 70}, 9, Color{120, 40, 36, 255}); }
    DrawRing(dc, 62, 74, 0, 360, 40, Color{120, 40, 36, 255});
    DrawCircleV(dc, 16, Pal::Brass);
    DrawLineEx({dc.x - 150, dc.y - 40}, {dc.x + 150, dc.y + 40}, 10, Color{60, 58, 56, 255});   // a bar chained across it
    DrawLineEx({dc.x - 150, dc.y + 40}, {dc.x + 150, dc.y - 40}, 10, Color{60, 58, 56, 255});
    DrawCircleV(dc, 20, Color{50, 50, 52, 255});
    DrawBrassPlate({dc.x - 110, dc.y + 120, 220, 40}, "UNDER REFIT", 22);
    // the ladder back up, on the left, in the light from the hatch
    for (int s = 0; s < 2; s++) DrawRectangle(190 + s * 70, 0, 10, 610, Color{120, 86, 50, 255});
    for (float y = 20; y < 600; y += 44) DrawRectangle(190, (int)y, 80, 8, Color{140, 100, 60, 255});
    LightsBegin(Color{112, 104, 96, 255});
    AddLight({235, 0}, 520, Color{255, 210, 150, 255}, 1.0f);
    AddLight({980, 170}, 520, Color{255, 190, 110, 255}, 0.9f + 0.1f * sinf(t * 9));
    AddLight({700, 350}, 420, Color{200, 170, 130, 255}, 0.6f);
    LightsEnd();
    Glow({980, 170}, 40, Color{255, 190, 110, 120});
    InkPass(1.0f, 1.0f);
    DrawSceneTitle("The Study", "Beneath the salon. The shipwrights are still at work down here.");
    DrawWrapped("A study with its own purpose is being fitted out below the salon. The door stays locked until the refit is done.", {740, 580, 500, 60}, 17, Pal::Paper);
    if (Button({160, 630, 220, 48}, "Climb the ladder", true, 18) || BackButton(g)) g.scene = Scene::Hub;
}

// ============================================================ the Deep Arcade (the cabinet; its games come aboard later)
static int gArcadeReelDebug = -1;
void DebugArcadeReel(int reel) { gArcadeReelDebug = reel; }
void SceneArcade(Game& g) {
    float t = g.time;
    DrawCabinBackground();
    if (BackButton(g)) return;
    // the porthole screen, close up: a rotating brass drum of four engraved reels
    Vector2 c{SCREEN_W / 2.0f, 380};
    DrawCircleV({c.x + 8, c.y + 10}, 300, Fade(BLACK, 0.5f));
    DrawCircleV(c, 300, Pal::BrassDk);
    DrawRing(c, 270, 296, 0, 360, 90, Pal::Brass);
    for (int k = 0; k < 20; k++) { float a = k * PI / 10; DrawCircleV({c.x + cosf(a) * 283, c.y + sinf(a) * 283}, 6, Pal::BrassDk); }
    DrawCircleV(c, 268, Color{10, 54, 60, 255});
    Glow(c, 360, Color{60, 220, 210, 50});
    TxtBold("THE DEEP ARCADE", c.x - MeasureTxt("THE DEEP ARCADE", 30, true) / 2.0f, c.y - 250, 30, Color{180, 255, 240, 255});
    struct Reel { const char* name; const char* players; const char* length; const char* line; };
    const int NREELS = 5;
    const Reel reels[NREELS] = {
        {"Flats Duel", "2 players", "8-12 min", "Flats against a person: a best of three at the table."},
        {"The Trawl", "1-6 co-op", "30-35 min", "Work a steam trawler by night: catch it, kill it, cook it, sell it, and meet the Owners' quota."},
        {"Scuttle", "2-4 players", "5-10 min", "A fast crab-racing card game anyone can learn in one hand."},
        {"Fathoms", "2-6 players", "20-30 min", "The island strategy game: six factions of the deep."},
        {"Red Tide", "1-4 co-op", "20-60 min", "Divers in living ecosystems: kill for scrip, and the blood in the water brings what eats everything."},
    };
    static int sel = 0;
    static float drum = 0;
    if (gArcadeReelDebug >= 0) { sel = gArcadeReelDebug; drum = (float)sel; gArcadeReelDebug = -1; }
    drum += (sel - drum) * std::min(1.0f, GetFrameTime() * 8);
    float wheel = GetMouseWheelMove();
    if (wheel < 0 || IsKeyPressed(KEY_DOWN)) sel = std::min(NREELS - 1, sel + 1);
    if (wheel > 0 || IsKeyPressed(KEY_UP)) sel = std::max(0, sel - 1);
    for (int i = 0; i < NREELS; i++) {
        float off = (i - drum) * 92;
        if (fabsf(off) > 120) continue;
        float sc = 1 - fabsf(off) / 400;
        Rectangle r{c.x - 230 * sc, c.y - 40 + off - 34 * sc, 460 * sc, 68 * sc};
        bool on = i == sel;
        DrawRectangleRounded(r, 0.25f, 8, on ? Color{30, 120, 118, 255} : Color{16, 60, 64, 255});
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, on ? Pal::Brass : Pal::BrassDk);
        DrawTextCenteredBold(reels[i].name, c.x, r.y + 8 * sc, (int)(26 * sc), on ? Color{220, 255, 244, 255} : Color{120, 170, 166, 255});
        if (on) DrawTextCentered(TextFormat("%s   -   %s", reels[i].players, reels[i].length), c.x, r.y + 40, 15, Color{180, 230, 220, 255});
        if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) sel = i;
    }
    DrawWrapped(reels[sel].line, {c.x - 200, c.y + 100, 400, 50}, 17, Color{200, 240, 232, 255});
    // the three valve-wheel buttons. Host starts a playable game solo (the shared networking layer comes with its
    // own stage); Join and Browse wait for it.
    bool playable = sel == 4;
    // Red Tide's maps (each opens as its stage is built)
    static const char* RT_MAPS[] = {"ship", "cave"};
    static const char* RT_TITLES[] = {"The Sunken Ship", "The Underwater Cave"};
    static int rtMap = 0;
    const int RT_N = (int)(sizeof(RT_MAPS) / sizeof(RT_MAPS[0]));
    if (playable) {
        Rectangle lt{c.x - 190, c.y + 128, 30, 26}, rtR{c.x + 160, c.y + 128, 30, 26};
        DrawTextCenteredBold(RT_TITLES[rtMap], c.x, c.y + 130, 20, Color{230, 200, 150, 255});
        DrawTextCenteredBold("<", lt.x + 15, lt.y, 22, Pal::Brass);
        DrawTextCenteredBold(">", rtR.x + 15, rtR.y, 22, Pal::Brass);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), lt)) rtMap = (rtMap + RT_N - 1) % RT_N;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), rtR)) rtMap = (rtMap + 1) % RT_N;
        if (IsKeyPressed(KEY_LEFT)) rtMap = (rtMap + RT_N - 1) % RT_N;
        if (IsKeyPressed(KEY_RIGHT)) rtMap = (rtMap + 1) % RT_N;
    }
    const char* valves[3] = {"Host", "Join", "Browse"};
    for (int k = 0; k < 3; k++) {
        Vector2 v{c.x - 120 + k * 120.0f, c.y + 180};
        bool live = playable && k == 0;
        bool hov = live && CheckCollisionPointCircle(GetMousePosition(), v, 30);
        Color vc = live ? (hov ? Color{220, 90, 70, 255} : Color{180, 60, 48, 255}) : Color{120, 40, 36, 255};
        DrawRing(v, 22, 28, 0, 360, 24, vc);
        for (int s = 0; s < 3; s++) DrawLineEx(v, {v.x + cosf(t * (hov ? 2.0f : 0.3f) + s * 2.09f) * 24, v.y + sinf(t * (hov ? 2.0f : 0.3f) + s * 2.09f) * 24}, 3, vc);
        DrawTextCentered(k == 0 && playable ? "Dive (solo)" : valves[k], v.x, v.y + 34, 15, live ? Color{230, 240, 236, 255} : Color{140, 170, 166, 255});
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { StartRedTide(g, RT_MAPS[rtMap]); return; }
    }
    DrawTextCentered(playable ? "Red Tide: solo dives are open. Choose a map with < >. Online play arrives with the arcade's networking."
                              : "The arcade's wiring is still being run. This game comes aboard in a later refit.", c.x, 700, 16, Pal::Paper);
}