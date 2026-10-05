// Warp Dodgeball's scene (Scene::Warp): first person, drawn with Red Tide's inked renderer. The rules are warp.cpp's;
// everything you do goes through wd::Input (Gather), the bots write theirs with wd::BotInput.
#include "game.h"
#include "input.h"
#include "redtide_render.h"
#include "figure3d.h"
#include "warp.h"
#include "warp_net.h"
#include "arcade_session.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace {
using namespace wd;

const Color TEAM[2] = {{236, 112, 60, 255}, {60, 168, 214, 255}};
const Color TEAM_DARK[2] = {{120, 50, 30, 255}, {26, 76, 104, 255}};
const char* TEAM_NAME[2] = {"Coral", "Tide"};
const char* BOT_NAMES[12] = {"Bosun", "Kess", "Marlow", "Pip", "Gully", "Rook", "Tamsin", "Ode", "Fen", "Bramble", "Skip", "Wren"};

struct Pop { std::string text; Color col; float t; };
struct Feed { std::string text; float t; };
struct WarpScene {
    bool active = false, shot = false, help = true;
    World Wm; World* hostW = nullptr; arcade::Session* net = nullptr; uint32_t evTotal = 0; uint64_t seenVersion = 0; bool helloSent = false; std::string netName; std::vector<Vector3> smooth;
    int me = 0, perTeam = 4, skill = 1, arena = AR_CLASSIC;
    std::vector<uint32_t> botRng;
    float camYaw = 0, camPitch = 0, t = 0, acc = 0, shake = 0, flash = 0;
    size_t evSeen = 0; std::deque<Feed> feed; std::vector<Pop> pops;
    int lastRound = 0; float banner = 0; std::string bannerText; Color bannerCol = WHITE;
    float curveHold = 0, flickX = 0;
    bool overSaved = false;
    Camera3D cam{};
} S;

Model& SphereModel() { static Model m = LoadModelFromMesh(GenMeshSphere(1, 16, 20)); return m; }
Model& RingModel() { static Model m = LoadModelFromMesh(GenMeshTorus(0.12f, 2.0f, 10, 32)); return m; }
Model& DiscModel() { static Model m = LoadModelFromMesh(GenMeshCylinder(1, 1, 28)); return m; }
Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
World& W() { return S.hostW ? *S.hostW : S.Wm; }
Player& Me() { return W().players[std::clamp(S.me, 0, std::max(0, (int)W().players.size() - 1))]; }
std::string NameOf(int id) { return id >= 0 && id < (int)W().players.size() ? W().players[id].name : std::string("the wall"); }

// ---------------------------------------------------------------- the world
// a matrix that lays a unit shape on a panel's plane: x along right, y along up, z out of it
Matrix PanelFrame(Vector3 c, Vector3 n, Vector3 u, float sx, float sy, float sz) {
    Vector3 r = Vector3CrossProduct(u, n);
    Matrix m = {r.x * sx, u.x * sy, n.x * sz, c.x, r.y * sx, u.y * sy, n.y * sz, c.y, r.z * sx, u.z * sy, n.z * sz, c.z, 0, 0, 0, 1};
    return m;
}
void DrawArena() {
    const Arena& a = W().arena; float X = a.OuterX(), Z = a.OuterZ();
    // the floor: the run-off dark, the court in two halves, the lines
    rt::DrawWorldCube({0, -0.1f, 0}, {X * 2, 0.2f, Z * 2}, {70, 62, 58, 255});
    for (int s = 0; s < 2; s++) rt::DrawWorldCube({(s ? 1 : -1) * a.halfL / 2, -0.04f, 0}, {a.halfL, 0.1f, a.halfW * 2}, Mix(Color{176, 132, 88, 255}, TEAM[s], 0.12f));
    auto line = [&](float x0, float z0, float x1, float z1, Color c) { rt::DrawWorldCube({(x0 + x1) / 2, 0.02f, (z0 + z1) / 2}, {std::max(0.06f, fabsf(x1 - x0)), 0.02f, std::max(0.06f, fabsf(z1 - z0))}, c); };
    Color W{236, 232, 220, 255};
    line(0, -a.halfW, 0, a.halfW, {250, 210, 80, 255});
    for (int s = -1; s <= 1; s += 2) { line(s * Cfg().attackLine, -a.halfW, s * Cfg().attackLine, a.halfW, W); line(s * a.halfL, -a.halfW, s * a.halfL, a.halfW, W); }
    line(-a.halfL, -a.halfW, a.halfL, -a.halfW, W); line(-a.halfL, a.halfW, a.halfL, a.halfW, W);
    // the walls (dark) and the ceiling
    Color wall{48, 54, 66, 255};
    rt::DrawWorldCube({-X - 0.15f, a.ceil / 2, 0}, {0.3f, a.ceil, Z * 2}, wall); rt::DrawWorldCube({X + 0.15f, a.ceil / 2, 0}, {0.3f, a.ceil, Z * 2}, wall);
    rt::DrawWorldCube({0, a.ceil / 2, -Z - 0.15f}, {X * 2, a.ceil, 0.3f}, wall); rt::DrawWorldCube({0, a.ceil / 2, Z + 0.15f}, {X * 2, a.ceil, 0.3f}, wall);
    rt::DrawWorldCube({0, a.ceil + 0.15f, 0}, {X * 2, 0.3f, Z * 2}, {40, 44, 54, 255});
    // the light grey portal panels (a raised plate, so they read from across the court)
    for (const auto& p : a.panels) rt::DrawCubeM(PanelFrame(Vector3Add(p.c, Vector3Scale(p.n, 0.015f)), p.n, p.u, p.hw * 2, p.hh * 2, 0.03f), {206, 210, 216, 255});
    // the pieces
    for (const auto& b : a.boxes) {
        Vector3 c{(b.lo.x + b.hi.x) / 2, (b.lo.y + b.hi.y) / 2, (b.lo.z + b.hi.z) / 2}, s{b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z};
        Color col = b.kind == 1 ? Color{96, 100, 110, 255} : b.kind == 2 ? Color{60, 64, 76, 255} : b.kind == 3 ? Color{130, 96, 62, 255} : Color{150, 160, 172, 255};
        rt::DrawWorldCube(c, s, col);
        if (b.kind == 3) for (int k = -1; k <= 1; k += 2) for (int j = -1; j <= 1; j += 2) rt::DrawWorldCube({c.x + k * (s.x / 2 - 0.08f), b.lo.y / 2, c.z + j * (s.z / 2 - 0.08f)}, {0.12f, b.lo.y, 0.12f}, {80, 70, 60, 255});   // (the nest's legs)
    }
    for (const auto& l : a.ladders) for (int r = 0; r < 8; r++) {
        rt::DrawWorldCube({l.base.x, (r + 0.5f) * l.top / 8, l.base.z}, {0.06f, 0.06f, 0.6f}, {200, 170, 80, 255});
        if (!r) for (int k = -1; k <= 1; k += 2) rt::DrawWorldCube({l.base.x, l.top / 2, l.base.z + k * 0.3f}, {0.07f, l.top, 0.07f}, {170, 140, 60, 255});
    }
    // banners over each end in the team's colour
    for (int s = 0; s < 2; s++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.1f, 0.5f, a.halfW * 1.4f), MatrixTranslate((s ? 1 : -1) * (X - 0.05f), a.wall + (a.ceil - a.wall) * 0.5f, 0)), TEAM[s], 0.25f);
}
void DrawPortals() {
    const Config& C = Cfg();
    for (const auto& p : W().players) for (int k = 0; k < 2; k++) {
        const Portal& o = p.portal[k]; if (!o.on) continue;
        bool both = p.portal[0].on && p.portal[1].on;
        Color c = k == 0 ? Mix(TEAM[p.team], Color{255, 200, 80, 255}, 0.45f) : Mix(TEAM[p.team], Color{120, 120, 255, 255}, 0.45f);
        float pulse = 0.8f + 0.2f * sinf(S.t * 5 + p.id + k);
        // the rim: a ring of glowing segments round the oval; the face: a flattened sphere, swirling when the pair is open
        Vector3 cc = Vector3Add(o.c, Vector3Scale(o.n, 0.03f)), rr = Vector3CrossProduct(o.u, o.n);
        for (int j = 0; j < 28; j++) {
            float a0 = j * 2 * PI / 28 + (both ? S.t * 0.8f : 0), ca = cosf(a0), sa = sinf(a0);
            Vector3 q = Vector3Add(cc, Vector3Add(Vector3Scale(rr, ca * C.portalW / 2), Vector3Scale(o.u, sa * C.portalH / 2)));
            Vector3 tng = Vector3Normalize(Vector3Add(Vector3Scale(rr, -sa * C.portalW / 2), Vector3Scale(o.u, ca * C.portalH / 2)));
            rt::DrawCubeGlow(PanelFrame(q, o.n, tng, 0.07f, 0.2f, 0.05f), j % 2 ? c : Mix(c, WHITE, 0.4f), (both ? 1.6f : 0.7f) * pulse);
        }
        if (both) rt::DrawStaticGlow(SphereModel(), PanelFrame(cc, o.n, o.u, C.portalW / 2 * 0.93f, C.portalH / 2 * 0.93f, 0.012f), Mix(c, Color{30, 20, 60, 255}, 0.5f + 0.1f * sinf(S.t * 3 + k)), 0.7f);
    }
}
const Model* Body() { return rt::LoadAsset("shared/crew/crew_diver.glb"); }
void DrawPlayer(const Player& p, bool sideline, int slot) {
    if (p.id == S.me && !sideline && p.alive) return;   // (your own body: first person)
    const Model* m = Body(); if (!m) return;
    fig::Pose P; fig::Build B; B.build = 1.0f + 0.04f * (p.id % 3);
    Vector3 feet = p.pos; float yaw = p.yaw;
    if (sideline) {   // out: standing along their side of the court, in catch order
        float s = p.team == 0 ? -1.0f : 1.0f; float z = W().arena.kind == AR_CLASSIC ? W().arena.halfW + 1.2f : W().arena.OuterZ() - 0.5f;
        feet = {s * (2.0f + slot * 0.9f), 0, z}; yaw = -PI / 2; P.breathe = S.t * 2 + p.id;
    } else {
        float spd = Vector3Length({p.vel.x, 0, p.vel.z});
        P.walk = std::min(1.0f, spd / 5); P.walkPh = S.t * (4 + spd) + p.id;
        if (p.po == PO_CROUCH) P.crouch = 0.6f; if (p.po == PO_SQUAT) P.crouch = 1;
        if (p.charging) { P.elbow = 0.4f + 0.6f * p.charge; P.reach = 0.2f; }
        if (p.releaseT > 0) P.swingT = 1 - p.releaseT / Cfg().releaseTime;
        if (p.po == PO_LADDER) { P.reach = 1; P.elbow = 1; }
        P.breathe = S.t * 2 + p.id; P.look = 0;
    }
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, S.t);
    Matrix frame = fig::Frame(feet, -yaw);
    if (!sideline && (p.po == PO_DIVE || p.po == PO_PRONE)) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(-PI * 0.5f), MatrixTranslate(0, 0.25f, 0)), fig::Frame(feet, -yaw));
    Color top = sideline ? Mix(TEAM[p.team], Color{90, 90, 96, 255}, 0.5f) : TEAM[p.team];
    std::vector<rt::Recolor> rc = {{"top", top}, {"trousers", TEAM_DARK[p.team]}, {"hat", TEAM[p.team]}, {"skin", Color{(unsigned char)(200 + 10 * (p.id % 3)), (unsigned char)(150 + 12 * (p.id % 4)), 120, 255}}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
}
void DrawBalls() {
    const Config& C = Cfg();
    for (const auto& b : W().balls) {
        if (b.st == BS_HELD && b.holder == S.me && Me().alive) continue;   // (your own: the viewmodel)
        Color c = b.st == BS_LIVE ? Color{240, 70, 60, 255} : b.st == BS_DEAD ? Color{150, 60, 56, 255} : Color{214, 80, 66, 255};
        float glow = b.st == BS_LIVE ? 0.5f : 0.0f;
        if (b.st == BS_REST && b.mustCarry) glow = 0.25f + 0.2f * sinf(S.t * 4);
        Matrix m = MatrixMultiply(MatrixScale(C.ballR, C.ballR, C.ballR), MatrixTranslate(b.p.x, b.p.y, b.p.z));
        if (glow > 0) rt::DrawStaticGlow(SphereModel(), m, c, glow); else rt::DrawStatic(SphereModel(), m, c);
        if (b.st == BS_LIVE) for (int k = 1; k <= 4; k++) {   // (a short trail)
            Vector3 q = Vector3Subtract(b.p, Vector3Scale(b.v, 0.012f * k)); float r = C.ballR * (1 - 0.18f * k);
            rt::DrawStaticGlow(SphereModel(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(q.x, q.y, q.z)), Mix(c, WHITE, 0.3f), 0.5f - 0.1f * k);
        }
    }
}
void DrawViewmodel() {
    const Player& p = Me(); if (!p.alive || p.ball < 0) return;
    // the ball in your right hand: low right, drawn back with the charge, flung forward on the release
    Vector3 f = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position)), r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
    float back = p.charging ? 0.15f * p.charge : 0, fling = p.releaseT > 0 ? (1 - p.releaseT / Cfg().releaseTime) * 0.4f : 0;
    float shiver = p.overT > Cfg().overAfter ? 0.006f * sinf(S.t * 60) : 0;
    Vector3 at = Vector3Add(S.cam.position, Vector3Add(Vector3Scale(f, 0.62f - back + fling), Vector3Add(Vector3Scale(r, 0.19f + shiver + S.flickX * 0.002f), Vector3Scale(u, -0.2f + back * 0.4f))));
    float R = Cfg().ballR * 0.8f;
    rt::DrawStatic(SphereModel(), MatrixMultiply(MatrixScale(R, R, R), MatrixTranslate(at.x, at.y, at.z)), {214, 80, 66, 255});
}

// ---------------------------------------------------------------- events: pops, the feed, sound
void ReadEvents() {
    auto& ev = W().events;
    if (S.evSeen > ev.size()) S.evSeen = 0;
    for (; S.evSeen < ev.size(); S.evSeen++) {
        const Event& e = ev[S.evSeen];
        bool mine = e.who == S.me, byMe = e.by == S.me;
        switch (e.kind) {
            case EV_THROW: if (!S.shot) PlayCue("hit.throw", byMe || mine ? 0.7f : 0.3f); break;
            case EV_CATCH: S.feed.push_back({NameOf(e.who) + " CAUGHT " + NameOf(e.by) + "'s throw", S.t}); if (mine) { S.pops.push_back({"CAUGHT!", {120, 255, 160, 255}, S.t}); } if (!S.shot) PlayCue("ui.click", 0.8f); break;
            case EV_FUMBLE: if (mine) S.pops.push_back({"FUMBLED", {255, 140, 90, 255}, S.t}); break;
            case EV_BLOCK: if (mine) S.pops.push_back({"BLOCKED", {200, 220, 255, 255}, S.t}); if (!S.shot) PlayCue("imp.shell", 0.6f); break;
            case EV_HIT: if (mine) { S.shake = 0.5f; S.flash = 0.6f; } if (byMe) S.pops.push_back({"HIT!", {255, 220, 90, 255}, S.t}); if (!S.shot) PlayCue("imp.flesh", mine || byMe ? 0.9f : 0.4f); break;
            case EV_OUT: { const Player& v = W().players[e.who]; S.feed.push_back({NameOf(e.who) + " out: " + v.outCause + (e.by >= 0 && e.by != e.who ? " (" + NameOf(e.by) + ")" : ""), S.t}); break; }
            case EV_TRANSIT: if (byMe) S.pops.push_back({"WARP!", {190, 150, 255, 255}, S.t}); break;
            case EV_PORTAL_FAIL: if (mine) S.pops.push_back({"No panel there", {200, 200, 200, 255}, S.t}); break;
            case EV_BOUNCE: if (!S.shot) { float d = Vector3Distance(e.at, S.cam.position); if (d < 14) PlayCue("imp.stone", 0.35f * (1 - d / 14)); } break;
            case EV_PICKUP: if (mine && !S.shot) PlayCue("ui.click", 0.4f); break;
            case EV_PORTAL_PLACE: if (!S.shot) { float d = Vector3Distance(e.at, S.cam.position); PlayCue("ui.click", mine ? 0.7f : std::max(0.0f, 0.4f - d / 60)); } break;
            case EV_RETURN: S.feed.push_back({NameOf(e.who) + " is back in", S.t}); break;
            case EV_WHISTLE: S.banner = 1.4f; S.bannerText = "RUSH!"; S.bannerCol = {250, 220, 90, 255}; if (!S.shot) PlayCue("ui.click", 1); break;
            case EV_LINE: if (mine) S.pops.push_back({"Over the line!", {255, 120, 90, 255}, S.t}); break;
            default: break;
        }
    }
    while (S.feed.size() > 5) S.feed.pop_front();
    while (!S.feed.empty() && S.t - S.feed.front().t > 8) S.feed.pop_front();
    S.pops.erase(std::remove_if(S.pops.begin(), S.pops.end(), [](const Pop& p) { return S.t - p.t > 1.2f; }), S.pops.end());
}

// ---------------------------------------------------------------- input
void Gather() {
    Player& p = Me(); Input in;
    Vector2 md = MouseLook(!S.shot && p.alive && W().phase != PH_MATCH_END);
    S.camYaw += md.x * 0.0026f; S.camPitch = std::clamp(S.camPitch - md.y * 0.0026f, -1.4f, 1.4f);
    S.flickX = S.flickX * 0.85f + md.x * 0.15f;
    in.yaw = S.camYaw; in.pitch = S.camPitch;
    in.moveX = (IsKeyDown(KEY_W) ? 1.0f : 0) - (IsKeyDown(KEY_S) ? 1.0f : 0); in.moveZ = (IsKeyDown(KEY_D) ? 1.0f : 0) - (IsKeyDown(KEY_A) ? 1.0f : 0);
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT); in.jump = IsKeyPressed(KEY_SPACE); in.crouch = IsKeyDown(KEY_LEFT_CONTROL);
    in.squat = IsKeyPressed(KEY_C); in.dive = IsKeyPressed(KEY_Q);
    in.throwHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT); in.cancel = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    // curve: Z or X held at the release, or a sideways flick of the mouse as you let go
    float c = (IsKeyDown(KEY_X) ? 1.0f : 0) - (IsKeyDown(KEY_Z) ? 1.0f : 0);
    if (c == 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) c = std::clamp(S.flickX / 25.0f, -1.0f, 1.0f);
    in.curve = c;
    in.portalA = IsKeyPressed(KEY_E); in.portalB = IsKeyPressed(KEY_R);
    in.catchP = IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    p.in = in;
}

// ---------------------------------------------------------------- the HUD
void DrawHud(Game& g) {
    const Config& C = Cfg(); const World& w = W(); const Player& p = Me();
    float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    if (S.flash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 60, 40, (unsigned char)(120 * S.flash)});
    // the scoreboard: round pips, the clock, who's standing
    Rectangle sb{cx - 210, 10, 420, 58}; DrawRectangleRounded(sb, 0.3f, 6, Color{14, 18, 26, 210});
    for (int s = 0; s < 2; s++) {
        float x = s ? cx + 60 : cx - 60; TxtBold(TEAM_NAME[s], s ? x : x - MeasureTxt(TEAM_NAME[s], 18, true), 16, 18, TEAM[s]);
        for (int k = 0; k < C.roundsToWin; k++) { float px = s ? x + 6 + k * 16 : x - 12 - k * 16; DrawCircle((int)px, 50, 6, k < w.wins[s] ? TEAM[s] : Color{60, 66, 80, 255}); }
        for (int k = 0; k < (int)w.players.size() / 2; k++) { const Player& q = w.players[s * ((int)w.players.size() / 2) + k]; float px = s ? cx + 200 - k * 13 : cx - 200 + k * 13; DrawRectangle((int)px - 4, 22, 8, 22, q.alive ? TEAM[s] : Color{50, 50, 60, 255}); }
    }
    float left = w.phase == PH_LIVE || w.phase == PH_RUSH ? std::max(0.0f, C.roundTime - w.phaseT) : C.roundTime;
    DrawTextCenteredBold(TextFormat("%d:%02d", (int)left / 60, (int)left % 60), cx, 18, 24, left < 20 ? Color{255, 120, 90, 255} : WHITE);
    DrawTextCentered(w.suddenDeath ? "SUDDEN DEATH" : TextFormat("Round %d", w.round), cx, 46, 13, Color{180, 190, 210, 255});
    // the feed
    for (size_t i = 0; i < S.feed.size(); i++) Txt(S.feed[i].text, SCREEN_W - 20 - MeasureTxt(S.feed[i].text, 14), 84 + i * 20, 14, Color{230, 230, 236, (unsigned char)(255 * std::min(1.0f, (8 - (S.t - S.feed[i].t)) / 1.5f))});
    // warm-up and banners
    if (w.phase == PH_WARMUP) DrawTextCenteredBold(TextFormat("%d", (int)ceilf(C.rushWhistle - w.phaseT)), cx, cy - 120, 54, {250, 220, 90, 255});
    if (S.banner > 0) DrawTextCenteredBold(S.bannerText, cx, cy - 130, 48, Fade(S.bannerCol, std::min(1.0f, S.banner)));
    if (w.phase == PH_ROUND_END && w.roundWinner >= 0) DrawTextCenteredBold(TextFormat("%s take round %d", TEAM_NAME[w.roundWinner], w.round), cx, cy - 140, 36, TEAM[w.roundWinner]);
    for (size_t i = 0; i < S.pops.size(); i++) { float a = S.t - S.pops[i].t; DrawTextCenteredBold(S.pops[i].text, cx, cy + 50 + i * 30 - a * 30, 26, Fade(S.pops[i].col, 1 - a / 1.2f)); }
    if (w.phase == PH_MATCH_END) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});
        bool won = w.champion == p.team;
        DrawTextCenteredBold(won ? "YOUR TEAM WINS" : TextFormat("%s WIN", TEAM_NAME[w.champion]), cx, cy - 120, 48, TEAM[w.champion]);
        DrawTextCentered(TextFormat("%d - %d", w.wins[0], w.wins[1]), cx, cy - 60, 28, WHITE);
        // the stat line per player
        for (size_t i = 0; i < w.players.size(); i++) { const Player& q = w.players[i]; float x = cx + (q.team ? 40 : -360), y = cy - 10 + (i % (w.players.size() / 2)) * 22; Txt(TextFormat("%-10s  catches %d   outs %d", q.name.c_str(), q.catches, q.outs), x, y, 15, i == (size_t)S.me ? WHITE : TEAM[q.team]); }
        if (S.net) {
            if (S.net->role == arcade::R_HOST) { if (Button({cx - 230, cy + 140, 200, 40}, "Rematch", true, 16)) { std::string why; S.net->Rematch(&why); S.lastRound = 0; return; } }
            else DrawTextCentered("Waiting for the host: a rematch, or back to the lobby", cx, cy + 150, 15, Color{200, 210, 230, 255});
            if (Button({cx + 30, cy + 140, 200, 40}, S.net->role == arcade::R_HOST ? "Back to the lobby" : "Leave", true, 16)) { LeaveWarp(g); return; }
            return;
        }
        if (Button({cx - 230, cy + 140, 200, 40}, "Again", true, 16)) { StartWarp(g, S.perTeam, S.skill, S.arena); return; }
        if (Button({cx + 30, cy + 140, 200, 40}, "Back to the arcade", true, 16)) { LeaveWarp(g); return; }
        return;
    }
    if (!p.alive) {
        DrawTextCenteredBold("OUT", cx, cy - 40, 40, {255, 120, 90, 255});
        DrawTextCentered(p.outCause.empty() ? "" : ("(" + p.outCause + ")").c_str(), cx, cy + 6, 18, WHITE);
        int pos = 0; for (size_t i = 0; i < w.sideline[p.team].size(); i++) if (w.sideline[p.team][i] == S.me) pos = (int)i + 1;
        DrawTextCentered(TextFormat("A catch by your team brings you back (you're %s in line)", pos == 1 ? "first" : pos == 2 ? "second" : pos == 3 ? "third" : "further back"), cx, cy + 32, 15, {200, 210, 230, 255});
        return;
    }
    // the crosshair and the charge ring
    DrawCircleLines((int)cx, (int)cy, 3, WHITE);
    if (p.charging || p.releaseT > 0) {
        float k = p.charge; Color c = p.overT > C.overAfter ? Color{255, 90, 70, 255} : Mix(Color{250, 230, 120, 255}, Color{255, 140, 60, 255}, k);
        DrawRing({cx, cy}, 18, 23, -90, -90 + 360 * k, 40, c);
        if (p.overT > C.overAfter) DrawTextCentered("shaking - let it go", cx, cy + 30, 13, c);
        float cv = (IsKeyDown(KEY_X) ? 1.0f : 0) - (IsKeyDown(KEY_Z) ? 1.0f : 0);
        if (cv != 0) DrawTextCentered(cv > 0 ? "curve >" : "< curve", cx, cy - 40, 14, {200, 200, 255, 255});
    }
    // the catch prompt: an enemy ball coming at you inside the cone, 0.6 s out (spec "Catching")
    int bi; float tc;
    if (w.Threat(p, &bi, &tc)) {
        float win = w.CatchWindow(Vector3Length(w.balls[bi].v));
        bool now = tc <= win * 0.5f;
        float r = 34 + 60 * (tc / C.lookahead);
        DrawRing({cx, cy}, r, r + 4, 0, 360, 40, now ? Color{120, 255, 150, 255} : Color{255, 255, 255, 160});
        DrawRing({cx, cy}, 34, 37, 0, 360, 40, Color{120, 255, 150, 200});
        DrawTextCenteredBold(now ? "F - CATCH!" : "F", cx, cy + 46, now ? 26 : 20, now ? Color{120, 255, 150, 255} : WHITE);
    }
    // the portals: A and B with their cooldowns
    for (int k = 0; k < 2; k++) {
        float x = 30 + k * 64, y = SCREEN_H - 70; Color c = k == 0 ? Color{255, 190, 90, 255} : Color{140, 140, 255, 255};
        DrawEllipseLines((int)x + 20, (int)y + 24, 14, 22, p.portal[k].on ? c : Fade(c, 0.35f));
        if (p.portal[k].on) DrawEllipse((int)x + 20, (int)y + 24, 10, 17, Fade(c, 0.45f));
        if (p.portal[k].cool > 0) DrawRectangle((int)x + 4, (int)y + 50, (int)(32 * p.portal[k].cool / C.portalCool), 4, c);
        TxtBold(k == 0 ? "E" : "R", x + 16, y - 16, 14, WHITE);
    }
    if (p.ball >= 0 && W().balls[p.ball].mustCarry) DrawTextCentered("Carry it back behind your attack line first", cx, SCREEN_H - 120.0f, 16, {250, 220, 90, 255});
    if (p.po == PO_SQUAT || p.po == PO_DIVE || p.po == PO_PRONE) DrawTextCentered(p.po == PO_SQUAT ? "squat" : p.po == PO_DIVE ? "dive!" : "getting up...", cx, SCREEN_H - 96.0f, 15, {200, 210, 230, 255});
    if (S.help) {
        Rectangle r{SCREEN_W - 300.0f, SCREEN_H - 250.0f, 284, 236}; DrawRectangleRounded(r, 0.08f, 6, Color{10, 14, 22, 200});
        const char* L[] = {"WASD move, Shift sprint, Space jump", "Ctrl crouch, C squat (quick duck), Q dive", "Hold LMB to charge, release to throw", "RMB cancels; Z / X (or a flick) curves", "E / R: portal A / B on a grey panel", "F: catch when the ring goes green", "Hit with a live ball: out. Caught: the", "thrower's out and a teammate is back.", "Your own ball off a wall or through", "a portal can get YOU out.  H hides this"};
        for (int i = 0; i < 10; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 22, 14, i < 6 ? WHITE : Color{200, 210, 230, 255});
    }
}

void StepCamera(float dt) {
    const Player& p = Me();
    if (p.alive) { S.cam.position = p.Eye(); }
    else {   // out: up in the gallery over your bench, looking across the court
        float s = p.team == 0 ? -1.0f : 1.0f; Vector3 want{s * (W().arena.OuterX() - 0.8f), W().arena.wall * 0.8f, 0};
        S.cam.position = Vector3Lerp(S.cam.position, want, std::min(1.0f, dt * 3));
    }
    if (S.shake > 0) { S.cam.position.x += sinf(S.t * 70) * 0.03f * S.shake; S.cam.position.y += cosf(S.t * 61) * 0.03f * S.shake; }
    Vector3 look{cosf(S.camPitch) * cosf(S.camYaw), sinf(S.camPitch), cosf(S.camPitch) * sinf(S.camYaw)};
    S.cam.target = Vector3Add(S.cam.position, look); S.cam.up = {0, 1, 0}; S.cam.fovy = 74; S.cam.projection = CAMERA_PERSPECTIVE;
}
void Render() {
    rt::SceneLight L;
    L.fog = {26, 30, 38, 255}; L.fogDensity = 0.006f; L.fill = {90, 96, 110, 255}; L.rim = {120, 150, 190, 255}; L.key = {255, 240, 220, 255};
    L.surfaceY = 1e5f; L.time = S.t;
    L.moonDir = Vector3Normalize({0.2f, -1, 0.3f}); L.moon = {255, 245, 230, 255}; L.moonK = 0.45f;
    L.ambK = 0.8f; L.skyAmb = {150, 156, 170, 255}; L.seaAmb = {70, 60, 56, 255};
    L.outline = 0.8f; L.outlineTint = {16, 18, 24, 255}; L.stipple = 0.3f; L.grain = 0.3f; L.aoK = 0.4f; L.aoRadius = 0.5f; L.filmic = 0.2f; L.saturation = 1.15f;
    const Arena& a = W().arena;
    for (int i = -1; i <= 1; i++) L.AddPoint({i * a.halfL * 0.66f, a.ceil - 0.6f, 0}, a.ceil * 1.8f, {255, 240, 220, 255}, 0.45f);
    int n = 0; for (const auto& p : W().players) for (int k = 0; k < 2 && n < 4; k++) if (p.portal[k].on && p.portal[0].on && p.portal[1].on) { L.AddPoint(Vector3Add(p.portal[k].c, Vector3Scale(p.portal[k].n, 0.6f)), 3.5f, k ? Color{140, 140, 255, 255} : Color{255, 190, 90, 255}, 0.6f); n++; }
    rt::ApplyGameQuality(); rt::RenderBegin(S.cam, L);
    DrawArena(); DrawPortals();
    int slot[2] = {0, 0};
    for (const auto& p : W().players) if (p.present) { if (p.alive) DrawPlayer(p, false, 0); else { int k = 0; for (size_t i = 0; i < W().sideline[p.team].size(); i++) if (W().sideline[p.team][i] == p.id) k = (int)i; DrawPlayer(p, true, k); slot[p.team]++; } }
    DrawBalls(); DrawViewmodel();
    rt::RenderEnd();
}
}  // namespace

int gWarpArena = 0, gWarpFill = 4;
void StartWarp(Game& g, int perTeam, int skill, int arena) {
    S = WarpScene{}; S.active = true; S.perTeam = std::clamp(perTeam, 1, 6); S.skill = std::clamp(skill, 0, 2); S.arena = arena;
    W().Init(arena, S.perTeam, (uint32_t)GetRandomValue(1, 1 << 30));
    for (auto& p : W().players) p.name = p.id == 0 ? "You" : BOT_NAMES[p.id % 12];
    S.botRng.resize(W().players.size()); for (size_t i = 0; i < S.botRng.size(); i++) S.botRng[i] = 99991u * (uint32_t)(i + 1) + (uint32_t)GetRandomValue(0, 1 << 20);
    S.camYaw = 0; S.camPitch = -0.05f; S.lastRound = W().round; S.banner = 0;
    g.scene = Scene::Warp;
}
void StartWarpNet(Game& g, arcade::Session* net, const char* name) {
    S = WarpScene{}; S.active = true; S.net = net; S.netName = name ? name : "Diver"; S.help = true;
    S.camYaw = 0; S.camPitch = -0.05f;
    g.scene = Scene::Warp;
}
void LeaveWarp(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade;
}
// a networked frame: the host steps the real world inside the session; a guest mirrors the snapshots (smoothed)
static bool NetFrame(Game& g, float dt) {
    arcade::Session& N = *S.net;
    N.Update(GetTime(), dt);
    if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade; return false; }
    int seat = N.MyPlayer();
    if (N.role == arcade::R_HOST) { S.hostW = WarpHostWorld(N.HostGame()); S.me = WarpSeatPlayer(N.HostGame(), seat); }
    else if (N.stateVersion != (int)S.seenVersion && !N.Snapshot().empty()) {
        S.seenVersion = (uint64_t)N.stateVersion;
        std::vector<Vector3> was; for (const auto& p : S.Wm.players) was.push_back(p.pos);
        Reader r(N.Snapshot()); ReadWorld(r, S.Wm, &S.evTotal);
        int per = (int)S.Wm.players.size() / 2; S.me = per > 0 && seat >= 0 && seat / 2 < per ? (seat % 2) * per + seat / 2 : 0;
        if (S.smooth.size() != S.Wm.players.size()) { S.smooth.clear(); for (const auto& p : S.Wm.players) S.smooth.push_back(p.pos); }
    }
    if (W().players.empty() || S.me < 0) { ClearBackground(Color{14, 18, 26, 255}); DrawTextCenteredBold("Into the arena...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE); return false; }
    if (!S.helloSent) { Writer o; o.U8(1); o.Str(S.netName); N.Act(o); S.helloSent = true; S.camYaw = Me().team == 0 ? 0 : PI; }
    Gather();
    Writer iw; iw.U8(0); WriteInput(Me().in, iw); N.Act(iw);
    if (N.role != arcade::R_HOST) {   // (between snapshots: bodies glide to where the host has them, live balls fly on)
        for (size_t i = 0; i < S.smooth.size() && i < S.Wm.players.size(); i++) {
            Vector3& sp = S.smooth[i]; Vector3 to = S.Wm.players[i].pos;
            if (Vector3Distance(sp, to) > 2.5f) sp = to; else sp = Vector3Lerp(sp, to, std::min(1.0f, dt * 18));
        }
        for (auto& b : S.Wm.balls) if (b.st == BS_LIVE || b.st == BS_DEAD) { b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); b.p.y = std::max(b.p.y, Cfg().ballR); }
    }
    if (W().round != S.lastRound) { S.lastRound = W().round; S.camYaw = Me().team == 0 ? 0 : PI; S.camPitch = -0.05f; }
    return true;
}
void SceneWarp(Game& g) {
    if (!S.active) { StartWarp(g, 4, 1, AR_CLASSIC); }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 20.0f);
    S.t += dt; S.shake = std::max(0.0f, S.shake - dt); S.flash = std::max(0.0f, S.flash - dt * 2); S.banner = std::max(0.0f, S.banner - dt);
    if (S.net) {
        if (!NetFrame(g, dt)) return;
        ReadEvents(); StepCamera(dt);
        // (a guest draws the smoothed positions)
        std::vector<Vector3> real; bool guest = !S.hostW && S.smooth.size() == S.Wm.players.size();
        if (guest) for (size_t i = 0; i < S.Wm.players.size(); i++) { real.push_back(S.Wm.players[i].pos); if ((int)i != S.me) S.Wm.players[i].pos = S.smooth[i]; }
        Render();
        if (guest) for (size_t i = 0; i < real.size(); i++) S.Wm.players[i].pos = real[i];
        DrawHud(g);
        return;
    }
    if (!S.shot) {
        Gather();
        // the fixed 120 Hz step: your input stays held across the sub-steps, presses fire once
        S.acc += dt; Input mine = Me().in; bool first = true;
        while (S.acc >= STEP) {
            for (auto& p : W().players) if (p.id != S.me) BotInput(W(), p.id, p.in, S.botRng[p.id], S.skill);
            Me().in = mine;
            if (!first) { Me().in.jump = Me().in.squat = Me().in.dive = Me().in.portalA = Me().in.portalB = Me().in.catchP = false; }
            W().Step(); S.acc -= STEP; first = false;
        }
        if (W().round != S.lastRound) { S.lastRound = W().round; S.camYaw = Me().team == 0 ? 0 : PI; S.camPitch = -0.05f; }
    }
    ReadEvents();
    StepCamera(dt);
    Render();
    DrawHud(g);
}
void WarpMenuTick(float) {}
bool WarpActive() { return S.active; }
// --shots: 0 the rush (classic), 1 mid-round with portals open, 2 the Extreme, 3 the catch prompt, 4 out on the bench, 5 the match won
void DebugWarpShot(Game& g, int which) {
    StartWarp(g, 4, 2, which == 2 ? AR_EXTREME : AR_CLASSIC); S.shot = true; S.help = which == 0;
    World& w = W(); uint32_t r = 5;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs / STEP) && w.phase != PH_MATCH_END; i++) { for (auto& p : w.players) BotInput(w, p.id, p.in, r, 2); w.Step(); } };
    if (which == 0) { run(2.6f); S.camYaw = 0; S.camPitch = -0.15f; }
    if (which == 1 || which == 2) {
        run(9); if (!Me().alive) { auto& sl = w.sideline[0]; sl.erase(std::remove(sl.begin(), sl.end(), S.me), sl.end()); Me().alive = true; } Me().pos = {-6, 0, -1};
        Player& q = w.players[5 % w.players.size()];   // (an enemy's portal pair, open, for the look)
        q.portal[0] = {true, {w.arena.OuterX() - 0.01f, 3.2f, 1.5f}, {-1, 0, 0}, {0, 1, 0}, 0}; q.portal[1] = {true, {-2.5f, w.arena.wall * 0.75f, w.arena.OuterZ() - 0.01f}, {0, 0, -1}, {0, 1, 0}, 0};
        Player& m = Me(); m.portal[0] = {true, {-4, w.arena.wall * 0.8f, -w.arena.OuterZ() + 0.01f}, {0, 0, 1}, {0, 1, 0}, 0};
        S.camYaw = 0.25f; S.camPitch = 0.05f;
        if (Me().ball < 0) for (size_t i = 0; i < w.balls.size(); i++) if (w.balls[i].st != BS_HELD) { w.balls[i].st = BS_HELD; w.balls[i].holder = S.me; Me().ball = (int)i; w.balls[i].mustCarry = false; break; }
        Me().charging = true; Me().charge = 0.7f;
    }
    if (which == 3) {
        run(3); Player& m = Me(); m.alive = true; m.ball = -1; m.pos = {-5, 0, 0}; m.yaw = 0; S.camYaw = 0; S.camPitch = 0;
        Ball b; b.p = {-1.2f, 1.3f, 0.1f}; b.v = {-14, 0, 0}; b.st = BS_LIVE; b.team = 1; b.thrower = w.players.size() / 2; w.balls.push_back(b);
    }
    if (which == 4) { run(4); w.Out(Me(), (int)w.players.size() / 2, "hit"); Me().outCause = "hit"; S.camYaw = 0; S.camPitch = -0.35f; for (int i = 0; i < 300; i++) StepCamera(1 / 60.0f); }
    if (which == 5) { w.wins[0] = 3; w.wins[1] = 1; w.champion = 0; w.phase = PH_MATCH_END; S.camPitch = 0; }
    ReadEvents();
}
