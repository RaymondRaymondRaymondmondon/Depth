// Scuffle's scene (stage 1): a match against bots on the stone stage, drawn in Depth's ink (doc p. 20): stick figures in
// thick coloured lines with round joints and one face (two dots and a line) on a parchment stage, stone blocks inked
// and hatched, deaths as ink splashes. The camera frames the living sticks between a minimum and maximum zoom. All
// play goes through sf::Input (Gather); the engine is scuffle.cpp.
#include "game.h"
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct Blot { Vector2 p, v; float r, life, max; Color c; };
struct ScuffleScene {
    bool active = false, shot = false;
    sf::World W; sf::Stage stage;
    int players = 4, skill = 2, toWin = 5, round = 0;
    std::vector<int> wins; std::vector<uint32_t> botRng;
    enum Phase { P_COUNT, P_FIGHT, P_WIN, P_OVER } phase = P_COUNT; float phaseT = 0; int roundWinner = -1;
    float acc = 0, t = 0;
    Vector2 cam{}; float zoom = 60;
    std::vector<Blot> blots; uint32_t evSeen = 0;
    std::vector<Vector2> grain;
};
ScuffleScene S;
const Color PAPER{226, 214, 186, 255}, INK{30, 24, 20, 255}, STONE{150, 138, 120, 255};
Color StickColor(int i) {
    static const Color C[sf::MAX_STICKS] = {{200, 50, 50, 255}, {50, 90, 200, 255}, {60, 150, 70, 255}, {220, 170, 30, 255}, {140, 70, 170, 255}, {230, 120, 40, 255}, {40, 160, 160, 255}, {220, 100, 150, 255}};
    return C[std::clamp(i, 0, sf::MAX_STICKS - 1)];
}
const char* StickName(int i) { static const char* N[sf::MAX_STICKS] = {"You", "Old Marlow", "Big Ruth", "Sly Pennick", "Cutter Jones", "Pip", "Nellie Bright", "Boxer Mags"}; return N[std::clamp(i, 0, sf::MAX_STICKS - 1)]; }
Vector2 W2S(Vector2 w) { return {(w.x - S.cam.x) * S.zoom + SCREEN_W / 2.0f, SCREEN_H / 2.0f - (w.y - S.cam.y) * S.zoom}; }
Vector2 S2W(Vector2 s) { return {(s.x - SCREEN_W / 2.0f) / S.zoom + S.cam.x, (SCREEN_H / 2.0f - s.y) / S.zoom + S.cam.y}; }

void NewRound() {
    S.round++;
    S.W.Init(S.stage, S.players, 0x5C0FF1Eu + S.round * 7919u);
    S.phase = ScuffleScene::P_COUNT; S.phaseT = 1.0f; S.roundWinner = -1; S.acc = 0;
    S.blots.clear(); S.evSeen = 0;
}
void Splash(Vector2 at, Color c, int n, float sp, float r) {
    uint32_t h = (uint32_t)(at.x * 977 + at.y * 131) + (uint32_t)S.blots.size() * 2654435761u;
    for (int i = 0; i < n; i++) {
        h = h * 1664525u + 1013904223u; float a = (h >> 8) % 6283 / 1000.0f; h = h * 1664525u + 1013904223u; float s = sp * (0.3f + (h >> 8) % 1000 / 1000.0f);
        S.blots.push_back({at, {cosf(a) * s, sinf(a) * s + 1}, r * (0.5f + (h >> 20) % 100 / 100.0f), 2.5f, 2.5f, c});
    }
}

// ---------------------------------------------------------------- input: the keys and the mouse into sf::Input
void Gather(sf::Input& in, const sf::Stick& k) {
    in = sf::Input{};
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) in.moveX -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.moveX += 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) in.moveY = -1;
    in.jump = IsKeyDown(KEY_W) || IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP);
    in.fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_J);
    in.taunt = IsKeyPressed(KEY_T);
    Vector2 m = S2W(GetMousePosition()), d = Vector2Subtract(m, k.pt[sf::J_NECK].p);
    in.aim = Vector2Length(d) > 0.05f ? Vector2Normalize(d) : Vector2{(float)k.face, 0};
    if (IsKeyDown(KEY_J) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.aim = {(float)k.face, 0};   // (the keyboard's fist: straight ahead)
}

// ---------------------------------------------------------------- drawing
void DrawStage() {
    const sf::Stage& s = S.W.stage;
    ClearBackground(PAPER);
    for (const auto& g : S.grain) DrawCircleV(g, 1.2f, Color{196, 182, 150, 255});
    float px = S.zoom * sf::TILE;
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) {
        if (!s.Solid(x, y)) continue;
        Vector2 a = W2S({x * sf::TILE, (y + 1) * sf::TILE});
        DrawRectangleV(a, {px + 1, px + 1}, STONE);
        // hatching: diagonal strokes, denser low on each block
        for (int k = 1; k < 4; k++) { float o = k * px / 4; DrawLineEx({a.x + o, a.y + px}, {a.x + px, a.y + o}, 1, Color{120, 108, 92, 255}); }
        // the ink edge on every exposed face
        float th = std::max(2.0f, px * 0.09f);
        if (!s.Solid(x, y + 1)) DrawLineEx({a.x - 1, a.y}, {a.x + px + 1, a.y}, th, INK);
        if (!s.Solid(x, y - 1)) DrawLineEx({a.x - 1, a.y + px}, {a.x + px + 1, a.y + px}, th, INK);
        if (!s.Solid(x - 1, y)) DrawLineEx({a.x, a.y - 1}, {a.x, a.y + px + 1}, th, INK);
        if (!s.Solid(x + 1, y)) DrawLineEx({a.x + px, a.y - 1}, {a.x + px, a.y + px + 1}, th, INK);
    }
}
void DrawStick(const sf::Stick& k) {
    if (!k.present) return;
    Color c = k.alive ? StickColor(k.id) : Color{120, 116, 110, 255};
    if (k.alive && k.hp < 35) { float w = 0.5f + 0.5f * sinf(S.t * 14); c = ColorLerp(c, Color{230, 30, 30, 255}, 0.35f * w); }   // (low health: a red pulse)
    float th = std::max(3.0f, S.zoom * 0.075f);
    auto P = [&](int j) { Vector2 p = W2S(k.pt[j].p); if (k.alive && k.hp < 35) p.x += sinf(S.t * 30 + j) * 0.8f; return p; };
    auto L = [&](int a, int b) { Vector2 A = P(a), B = P(b); DrawLineEx(A, B, th + 2.5f, INK); };
    auto C = [&](int a, int b) { Vector2 A = P(a), B = P(b); DrawLineEx(A, B, th, c); DrawCircleV(B, th * 0.5f, c); DrawCircleV(A, th * 0.5f, c); };
    static const int SEG[][2] = {{sf::J_NECK, sf::J_PELVIS}, {sf::J_NECK, sf::J_ELBOW_L}, {sf::J_ELBOW_L, sf::J_HAND_L}, {sf::J_NECK, sf::J_ELBOW_R}, {sf::J_ELBOW_R, sf::J_HAND_R},
                                 {sf::J_PELVIS, sf::J_KNEE_L}, {sf::J_KNEE_L, sf::J_FOOT_L}, {sf::J_PELVIS, sf::J_KNEE_R}, {sf::J_KNEE_R, sf::J_FOOT_R}, {sf::J_NECK, sf::J_HEAD}};
    for (const auto& s : SEG) L(s[0], s[1]);   // (an ink outline under the colour)
    for (const auto& s : SEG) C(s[0], s[1]);
    // the head: a ring, and the face (two dots and a line); dead eyes are crosses
    Vector2 h = P(sf::J_HEAD); float r = k.pt[sf::J_HEAD].r * S.zoom;
    DrawCircleV(h, r + 2, INK); DrawCircleV(h, r, PAPER); DrawRing(h, r - th * 0.6f, r, 0, 360, 24, c);
    Vector2 up = Vector2Normalize(Vector2Subtract(P(sf::J_HEAD), P(sf::J_NECK))); Vector2 fw{-up.y * k.face, up.x * k.face};
    if (fw.x * k.face < 0) fw = Vector2Negate(fw);
    Vector2 e = Vector2Add(h, Vector2Scale(fw, r * 0.35f));
    Vector2 ex = Vector2Scale(fw, r * 0.12f), ey = Vector2Scale(up, r * 0.18f);
    if (k.alive) {
        DrawCircleV(Vector2Add(Vector2Add(e, ey), Vector2Scale(fw, -r * 0.2f)), std::max(1.5f, r * 0.11f), INK);
        DrawCircleV(Vector2Add(Vector2Add(e, ey), Vector2Scale(fw, r * 0.2f)), std::max(1.5f, r * 0.11f), INK);
        float mouth = k.punchT > 0 || k.st == sf::S_RAGDOLL ? 0.25f : 0.0f;
        DrawLineEx(Vector2Add(Vector2Subtract(e, ey), Vector2Scale(fw, -r * 0.22f)), Vector2Add(Vector2Subtract(Vector2Subtract(e, ey), Vector2Scale(up, r * mouth)), Vector2Scale(fw, r * 0.22f)), std::max(1.5f, r * 0.09f), INK);
    } else {
        for (float s : {-0.2f, 0.2f}) { Vector2 q = Vector2Add(Vector2Add(e, ey), Vector2Scale(fw, r * s)); DrawLineEx(Vector2Subtract(q, Vector2Add(ex, ey)), Vector2Add(q, Vector2Add(ex, ey)), 1.5f, INK); DrawLineEx(Vector2Add(q, Vector2Subtract(ey, ex)), Vector2Subtract(q, Vector2Subtract(ey, ex)), 1.5f, INK); }
    }
    // a name over the living, and the round's grab
    if (k.alive) { Vector2 n = W2S(Vector2Add(k.pt[sf::J_HEAD].p, {0, 0.38f})); DrawTextCentered(StickName(k.id), n.x, n.y - 8, 12, ColorAlpha(INK, 0.6f)); }
}
void DrawBlots(float dt) {
    for (auto& b : S.blots) {
        b.life -= dt;
        if (b.v.x != 0 || b.v.y != 0) {   // (ink flies, then lands on the stone and stays put)
            b.v.y -= 9 * dt; Vector2 np = Vector2Add(b.p, Vector2Scale(b.v, dt));
            if (S.W.stage.Solid((int)floorf(np.x / sf::TILE), (int)floorf(np.y / sf::TILE)) || np.y < 0) b.v = {0, 0}; else b.p = np;
        }
        Vector2 s = W2S(b.p); DrawCircleV(s, b.r * S.zoom * std::min(1.0f, b.life / b.max * 2), ColorAlpha(b.c, std::min(1.0f, b.life)));
    }
    S.blots.erase(std::remove_if(S.blots.begin(), S.blots.end(), [](const Blot& b) { return b.life <= 0; }), S.blots.end());
}
void ReadEvents() {
    const auto& E = S.W.events; uint32_t base = S.W.eventBase;
    if (S.evSeen < base) S.evSeen = base;
    for (uint32_t i = S.evSeen; i < base + E.size(); i++) {
        const sf::Event& e = E[i - base];
        if (e.kind == sf::EV_HIT) Splash(e.at, INK, 5, 2.5f, 0.05f);
        if (e.kind == sf::EV_HAYMAKER) Splash(e.at, INK, 9, 4.0f, 0.07f);
        if (e.kind == sf::EV_DIE) Splash(e.at, INK, 22, 5.0f, 0.1f);
        if (e.kind == sf::EV_LAND && e.a > 4) Splash(e.at, Color{150, 138, 120, 255}, 6, 2, 0.05f);
    }
    S.evSeen = base + (uint32_t)E.size();
}
void StepCamera(float dt) {
    // frame the living sticks (the whole stage at most, 95 px a metre at the closest)
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; int n = 0;
    for (const auto& k : S.W.sticks) if (k.present && k.alive) { Vector2 p = k.pt[sf::J_PELVIS].p; x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); n++; }
    float sw = S.W.stage.Width(), sh = S.W.stage.Height();
    float fitAll = std::min(SCREEN_W / (sw + 1.5f), SCREEN_H / (sh + 1.0f));
    Vector2 want{sw / 2, sh / 2}; float z = fitAll;
    if (n > 0) { want = {(x0 + x1) / 2, (y0 + y1) / 2 + 0.6f}; z = std::clamp(std::min(SCREEN_W / (x1 - x0 + 9.0f), SCREEN_H / (y1 - y0 + 6.0f)), fitAll, 95.0f); }
    S.zoom += (z - S.zoom) * std::min(1.0f, dt * 2.5f);
    float hw = SCREEN_W / 2.0f / S.zoom, hh = SCREEN_H / 2.0f / S.zoom;   // (never past the stage's edges by much)
    want.x = sw > 2 * hw ? std::clamp(want.x, hw - 0.75f, sw - hw + 0.75f) : sw / 2;
    want.y = sh > 2 * hh ? std::clamp(want.y, hh - 0.5f, sh - hh + 0.5f) : sh / 2;
    S.cam = Vector2Lerp(S.cam, want, std::min(1.0f, dt * 4));
}
void DrawHud() {
    // the scoreboard: each stick's colour and its round wins as pips
    float x = 16;
    for (int i = 0; i < S.players; i++) {
        const sf::Stick& k = S.W.sticks[i];
        DrawRectangleRounded({x, 12, 150, 26}, 0.4f, 6, ColorAlpha(Color{20, 16, 14, 255}, 0.75f));
        DrawCircleV({x + 13, 25}, 7, k.alive ? StickColor(i) : Color{110, 106, 100, 255});
        Txt(StickName(i), x + 26, 16, 13, Color{240, 230, 210, 255});
        for (int w = 0; w < S.toWin; w++) DrawCircleV({x + 112 + (w % 5) * 8.0f, 20.0f + (w / 5) * 8}, 3, w < S.wins[i] ? StickColor(i) : Color{80, 74, 66, 255});
        x += 158;
    }
    DrawTextCentered(TextFormat("Round %d   -   first to %d", S.round, S.toWin), SCREEN_W / 2.0f, 46, 14, INK);
    if (S.phase == ScuffleScene::P_COUNT) DrawTextCenteredBold("FIGHT!", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 56, Color{40, 30, 26, (unsigned char)(255 * std::clamp(1.4f - S.phaseT, 0.0f, 1.0f))});
    if (S.phase == ScuffleScene::P_WIN) DrawTextCenteredBold(S.roundWinner >= 0 ? TextFormat("%s %s the round", StickName(S.roundWinner), S.roundWinner == 0 ? "take" : "takes") : "A draw: everyone at once", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 34, S.roundWinner >= 0 ? StickColor(S.roundWinner) : INK);
    if (S.phase == ScuffleScene::P_OVER) {
        int best = 0; for (int i = 1; i < S.players; i++) if (S.wins[i] > S.wins[best]) best = i;
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(PAPER, 0.75f));
        DrawTextCenteredBold(TextFormat("%s %s the match", StickName(best), best == 0 ? "win" : "wins"), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 80, 44, StickColor(best));
        for (int i = 0; i < S.players; i++) DrawTextCentered(TextFormat("%s: %d rounds, %d kills", StickName(i), S.wins[i], S.W.sticks[i].kills), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 20 + i * 22.0f, 16, INK);
    }
    DrawTextCentered("A/D run   W or Space jump (hold for height)   S duck, or dive in the air   mouse aims, click punches (hold against someone: grab, release: throw)   T taunt", SCREEN_W / 2.0f, SCREEN_H - 22.0f, 12, ColorAlpha(INK, 0.7f));
}
} // namespace

void StartScuffle(Game& g, int bots, int skill, int toWin) {
    S = ScuffleScene{};
    S.active = true; S.players = std::clamp(bots + 1, 2, sf::MAX_STICKS); S.skill = std::clamp(skill, 0, 2); S.toWin = std::clamp(toWin, 1, 20);
    S.stage = sf::StoneStage(); S.wins.assign(S.players, 0); S.botRng.resize(S.players);
    for (int i = 0; i < S.players; i++) S.botRng[i] = 1234567u + i * 7919u + (uint32_t)GetRandomValue(0, 1 << 20);
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (h >> 8) % SCREEN_W; h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    NewRound();
    S.cam = {S.stage.Width() / 2, S.stage.Height() / 2}; S.zoom = 60;
    g.scene = Scene::Scuffle;
}
void SceneScuffle(Game& g) {
    if (!S.active) { StartScuffle(g, 3, 2, 5); }
    float dt = std::min(GetFrameTime(), 1 / 20.0f);
    if (S.shot) dt = 1 / 60.0f;
    S.t += dt;
    // the match: a second's countdown (the sticks stretch), the fight, the winner's pose, the next round
    if (S.phase == ScuffleScene::P_COUNT && (S.phaseT -= dt) <= 0) S.phase = ScuffleScene::P_FIGHT;
    if (S.phase == ScuffleScene::P_FIGHT && S.W.Living() <= 1) {
        S.phase = ScuffleScene::P_WIN; S.phaseT = 1.5f; S.roundWinner = -1;
        for (const auto& k : S.W.sticks) if (k.alive && k.present) S.roundWinner = k.id;
        if (S.roundWinner >= 0) S.wins[S.roundWinner]++;
    }
    if (S.phase == ScuffleScene::P_WIN && (S.phaseT -= dt) <= 0) {
        bool over = false; for (int w : S.wins) over |= w >= S.toWin;
        if (over) S.phase = ScuffleScene::P_OVER; else NewRound();
    }
    // the inputs (you and the bots), then the fixed steps
    if (!S.shot && S.phase != ScuffleScene::P_OVER) {
        S.acc += dt;
        while (S.acc >= sf::STEP) {
            S.acc -= sf::STEP;
            if (S.phase == ScuffleScene::P_COUNT) { for (auto& k : S.W.sticks) k.in = sf::Input{}; }
            else {
                Gather(S.W.sticks[0].in, S.W.sticks[0]);
                for (int i = 1; i < S.players; i++) sf::BotInput(S.W, i, S.W.sticks[i].in, S.botRng[i], S.skill);
            }
            S.W.Step();
        }
    }
    ReadEvents();
    StepCamera(dt);
    DrawStage();
    for (const auto& k : S.W.sticks) if (!k.alive) DrawStick(k);
    for (const auto& k : S.W.sticks) if (k.alive) DrawStick(k);
    DrawBlots(dt);
    DrawHud();
    if (S.phase == ScuffleScene::P_OVER) {
        if (Button({SCREEN_W / 2.0f - 230, SCREEN_H / 2.0f + 160, 200, 40}, "Again", true, 16)) { int b = S.players - 1, sk = S.skill, tw = S.toWin; StartScuffle(g, b, sk, tw); }
        if (Button({SCREEN_W / 2.0f + 30, SCREEN_H / 2.0f + 160, 200, 40}, "Back to the arcade", true, 16)) { S.active = false; g.scene = Scene::Arcade; }
    }
}
void LeaveScuffle(Game& g) { S.active = false; g.scene = Scene::Arcade; }
// --shots: 0 mid-fight (four sticks), 1 a haymaker landing, 2 the match won
void DebugScuffleShot(Game& g, int which) {
    StartScuffle(g, 3, 2, 5); S.shot = true;
    S.phase = ScuffleScene::P_FIGHT;
    uint32_t r = 7;
    int frames = which == 1 ? 120 * 6 : 120 * 4;
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < S.players; i++) sf::BotInput(S.W, i, S.W.sticks[i].in, S.botRng[i] = S.botRng[i] ? S.botRng[i] : ++r, 2);
        S.W.Step();
        if (which == 1 && f > 120 * 3) { bool hay = false; for (const auto& e : S.W.events) hay |= e.kind == sf::EV_HAYMAKER; if (hay) break; }
    }
    ReadEvents();
    for (int i = 0; i < 90; i++) StepCamera(1 / 60.0f);
    if (which == 2) { S.wins = {5, 3, 2, 1}; S.phase = ScuffleScene::P_OVER; }
}
