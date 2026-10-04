// Scuffle's scene (stage 1): a match against bots on the stone stage, drawn in Depth's ink (doc p. 20): stick figures in
// thick coloured lines with round joints and one face (two dots and a line) on a parchment stage, stone blocks inked
// and hatched, deaths as ink splashes. The camera frames the living sticks between a minimum and maximum zoom. All
// play goes through sf::Input (Gather); the engine is scuffle.cpp.
#include "game.h"
#include "scuffle.h"
#include "scuffle_net.h"
#include "arcade_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "sound.h"
#include <vector>

namespace {
struct Blot { Vector2 p, v; float r, life, max; Color c; };
struct ScuffleScene {
    bool active = false, shot = false;
    sf::Match M;
    int players = 4, skill = 2, toWin = 5, arsenal = sf::AR_CLASSIC; int lastRound = 0;
    std::vector<uint32_t> botRng;
    float acc = 0, t = 0, wallMsgT = 0, modeMsgT = 0; std::string modeMsg;
    uint64_t paidKey = 0; int paid = 0;
    // stage 9: replays: this round as it's played, the match's best three, and the viewer (the live match waits)
    sf::Replay rec; std::vector<sf::Replay> best; bool recEnded = false, savedBest = false; std::string savedMsg;
    bool viewing = false, fileView = false; sf::Replay view; sf::Match live; std::vector<std::string> liveNames; size_t vIdx = 0; float vAcc = 0;   // (stage 9: the match's tokens, paid once)
    bool boardDone = false; std::vector<std::string> board; int boardMine = -1;   // (the Gauntlet's local leaderboard, scuffle_gauntlet.txt)
    Vector2 cam{}; float zoom = 60;
    std::vector<Blot> blots; uint32_t evSeen = 0;
    std::vector<Vector2> grain;
    // a networked match (stage 4): the host draws a copy of its real match, a guest its predicted mirror
    arcade::Session* net = nullptr; sf::Predictor pred; int me = 0; std::vector<std::string> names;
    Vector2 vis[sf::MAX_STICKS] = {}; int seenVersion = -1; bool helloSent = false; std::string netName; uint32_t hostSeq = 1;
};
ScuffleScene S;
const Color PAPER{226, 214, 186, 255}, INK{30, 24, 20, 255}, STONE{150, 138, 120, 255};
Color StickColor(int i) {
    static const Color C[sf::MAX_STICKS] = {{200, 50, 50, 255}, {50, 90, 200, 255}, {60, 150, 70, 255}, {220, 170, 30, 255}, {140, 70, 170, 255}, {230, 120, 40, 255}, {40, 160, 160, 255}, {220, 100, 150, 255}};
    return C[std::clamp(i, 0, sf::MAX_STICKS - 1)];
}
const char* StickName(int i) {
    if (i >= 0 && i < (int)S.names.size() && !S.names[i].empty()) return S.names[i].c_str();
    static const char* N[sf::MAX_STICKS] = {"You", "Old Marlow", "Big Ruth", "Sly Pennick", "Cutter Jones", "Pip", "Nellie Bright", "Boxer Mags"}; return N[std::clamp(i, 0, sf::MAX_STICKS - 1)];
}
Color TeamColor(int t) { return t == 0 ? Color{200, 60, 50, 255} : t == 1 ? Color{50, 100, 210, 255} : t == 2 ? Color{60, 160, 70, 255} : Color{220, 170, 30, 255}; }
Vector2 W2S(Vector2 w) { return {(w.x - S.cam.x) * S.zoom + SCREEN_W / 2.0f, SCREEN_H / 2.0f - (w.y - S.cam.y) * S.zoom}; }
Vector2 S2W(Vector2 s) { return {(s.x - SCREEN_W / 2.0f) / S.zoom + S.cam.x, (SCREEN_H / 2.0f - s.y) / S.zoom + S.cam.y}; }


void Splash(Vector2 at, Color c, int n, float sp, float r) {
    uint32_t h = (uint32_t)(at.x * 977 + at.y * 131) + (uint32_t)S.blots.size() * 2654435761u;
    for (int i = 0; i < n; i++) {
        h = h * 1664525u + 1013904223u; float a = (h >> 8) % 6283 / 1000.0f; h = h * 1664525u + 1013904223u; float s = sp * (0.3f + (h >> 8) % 1000 / 1000.0f);
        S.blots.push_back({at, {cosf(a) * s, sinf(a) * s + 1}, r * (0.5f + (h >> 20) % 100 / 100.0f), 2.5f, 2.5f, c});
    }
}

Rectangle DuelCard(int q) { return {SCREEN_W / 2.0f - 270 + q * 190, SCREEN_H / 2.0f - 40, 160, 110}; }
// the Gauntlet's board: one line per run (stages, seconds, deaths, world, date), best first (more stages, then less time)
void GauntletRecord(int stages, float secs, int deaths, int world) {
    struct Run { int st; float s; int d, w; std::string when; };
    std::vector<Run> runs;
    if (FILE* f = fopen("scuffle_gauntlet.txt", "r")) { char line[256]; while (fgets(line, sizeof line, f)) { Run r{}; char when[64] = ""; if (sscanf(line, "%d %f %d %d %63s", &r.st, &r.s, &r.d, &r.w, when) >= 4) { r.when = when; runs.push_back(r); } } fclose(f); }
    time_t now = time(nullptr); char stamp[32]; strftime(stamp, sizeof stamp, "%Y-%m-%d", localtime(&now));
    runs.push_back({stages, secs, deaths, world, stamp}); size_t mine = runs.size() - 1;
    std::vector<size_t> order(runs.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return runs[a].st != runs[b].st ? runs[a].st > runs[b].st : runs[a].s < runs[b].s; });
    if (order.size() > 50) order.resize(50);
    if (FILE* f = fopen("scuffle_gauntlet.txt", "w")) { for (size_t i : order) fprintf(f, "%d %.2f %d %d %s\n", runs[i].st, runs[i].s, runs[i].d, runs[i].w, runs[i].when.c_str()); fclose(f); }
    S.board.clear(); S.boardMine = -1;
    for (size_t k = 0; k < order.size() && k < 10; k++) { const Run& r = runs[order[k]]; if (order[k] == mine) S.boardMine = (int)k; S.board.push_back(TextFormat("%2d.  %d stages   %.1f s   %d deaths   %s   %s", (int)k + 1, r.st, r.s, r.d, ScuffleWorldChoice(r.w), r.when.c_str())); }
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
    in.gear = IsKeyDown(KEY_E) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);   // (the gear button)
    if (IsKeyDown(KEY_ONE)) in.pick = 1; if (IsKeyDown(KEY_TWO)) in.pick = 2; if (IsKeyDown(KEY_THREE)) in.pick = 3;   // (the Duel: the weapon for the round)
    if (S.M.mode == sf::MD_DUEL && S.M.phase == sf::Match::P_COUNT && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) for (int q = 0; q < 3; q++) if (CheckCollisionPointRec(GetMousePosition(), DuelCard(q))) { in.pick = q + 1; in.fire = false; return; }
    Vector2 m = S2W(GetMousePosition()), d = Vector2Subtract(m, k.pt[sf::J_NECK].p);
    in.aim = Vector2Length(d) > 0.05f ? Vector2Normalize(d) : Vector2{(float)k.face, 0};
    if (IsKeyDown(KEY_J) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.aim = {(float)k.face, 0};   // (the keyboard's fist: straight ahead)
}

#include "scuffle_worldart.inl"
#include "scuffle_arsenalart.inl"
#include "scuffle_bossart.inl"
#include "scuffle_cosart.inl"
// ---------------------------------------------------------------- drawing
void DrawBackdrop(const sf::Stage& s) {
    if (s.world != sf::WD_NAUTILUS) { WorldBackdrop(s); return; }
    // the Nautilus's hull behind the fight: pipes along the walls, portholes with the blue water, a few rivet lines
    ClearBackground(PAPER);
    for (const auto& g : S.grain) DrawCircleV(g, 1.2f, Color{196, 182, 150, 255});
    Color pipe{184, 168, 136, 255}, line{150, 134, 104, 255};
    for (int k = 0; k < 3; k++) { float y = W2S({0, s.Height() * (0.3f + 0.25f * k)}).y; DrawRectangle(0, (int)y - 5, SCREEN_W, 10, pipe); DrawLineEx({0, y - 5}, {(float)SCREEN_W, y - 5}, 1.5f, line); DrawLineEx({0, y + 5}, {(float)SCREEN_W, y + 5}, 1.5f, line); for (int x = 40 + k * 70; x < SCREEN_W; x += 220) DrawRectangle(x, (int)y - 8, 8, 16, line); }
    for (int k = 0; k < 4; k++) { Vector2 c = W2S({s.Width() * (0.14f + 0.24f * k), s.Height() * 0.78f}); float r = S.zoom * 0.7f; DrawCircleV(c, r + 4, line); DrawCircleV(c, r, Color{150, 176, 190, 255}); DrawCircleLines((int)c.x, (int)c.y, r * 0.75f, Color{170, 196, 206, 255}); }
}
void DrawTile(const sf::Stage& s, int x, int y, uint8_t k) {
    float px = S.zoom * sf::TILE;
    Vector2 a = W2S({x * sf::TILE, (y + 1) * sf::TILE});
    float th = std::max(2.0f, px * 0.09f);
    if (DrawWorldTile(s, x, y, k, a, px, th)) return;
    auto edges = [&](Color ink) {
        auto solidish = [&](int xx, int yy) { uint8_t q = s.At(xx, yy); return q != sf::T_EMPTY && q != sf::T_ROPE && q != sf::T_GLASS; };
        if (!solidish(x, y + 1)) DrawLineEx({a.x - 1, a.y}, {a.x + px + 1, a.y}, th, ink);
        if (!solidish(x, y - 1)) DrawLineEx({a.x - 1, a.y + px}, {a.x + px + 1, a.y + px}, th, ink);
        if (!solidish(x - 1, y)) DrawLineEx({a.x, a.y - 1}, {a.x, a.y + px + 1}, th, ink);
        if (!solidish(x + 1, y)) DrawLineEx({a.x + px, a.y - 1}, {a.x + px, a.y + px + 1}, th, ink);
    };
    switch (k) {
        case sf::T_STONE: {
            bool steel = s.world == sf::WD_NAUTILUS;
            DrawRectangleV(a, {px + 1, px + 1}, steel ? Color{124, 128, 130, 255} : STONE);
            for (int q = 1; q < 4; q++) { float o = q * px / 4; DrawLineEx({a.x + o, a.y + px}, {a.x + px, a.y + o}, 1, steel ? Color{104, 108, 110, 255} : Color{120, 108, 92, 255}); }
            if (steel) for (float u : {0.18f, 0.82f}) DrawCircleV({a.x + px * u, a.y + px * 0.2f}, std::max(1.2f, px * 0.05f), Color{70, 72, 74, 255});   // (rivets)
            edges(INK); break;
        }
        case sf::T_WOOD: DrawRectangleV(a, {px + 1, px + 1}, Color{150, 104, 60, 255}); for (int q = 1; q < 3; q++) DrawLineEx({a.x, a.y + q * px / 3}, {a.x + px, a.y + q * px / 3}, 1.5f, Color{110, 74, 40, 255}); edges(INK); break;
        case sf::T_ICE: DrawRectangleV(a, {px + 1, px + 1}, Color{200, 226, 236, 255}); DrawLineEx({a.x + px * 0.2f, a.y + px * 0.3f}, {a.x + px * 0.6f, a.y + px * 0.15f}, 2, WHITE); edges(Color{70, 100, 120, 255}); break;
        case sf::T_GLASS: DrawRectangleV(a, {px + 1, px + 1}, Color{180, 214, 226, 140}); DrawLineEx({a.x + px * 0.15f, a.y + px * 0.8f}, {a.x + px * 0.5f, a.y + px * 0.2f}, 1.5f, ColorAlpha(WHITE, 0.8f)); DrawRectangleLinesEx({a.x, a.y, px, px}, 1.5f, Color{60, 90, 110, 255}); break;
        case sf::T_ROPE: { float yy = a.y + px * 0.15f; DrawLineEx({a.x, yy}, {a.x + px, yy}, 2.5f, Color{120, 90, 50, 255}); DrawLineEx({a.x, yy + px * 0.22f}, {a.x + px, yy + px * 0.22f}, 1.5f, Color{120, 90, 50, 255}); DrawRectangleV({a.x + px * 0.1f, yy - 1}, {px * 0.8f, px * 0.18f}, Color{160, 120, 70, 255}); DrawRectangleLinesEx({a.x + px * 0.1f, yy - 1, px * 0.8f, px * 0.18f}, 1, INK); break; }
        case sf::T_RAIL: {
            bool live = S.M.w.RailLive(x, y);
            DrawRectangleV(a, {px + 1, px + 1}, Color{60, 62, 66, 255});
            for (int q = 0; q < 4; q++) { float o = q * px / 4; DrawTriangle({a.x + o, a.y + px * 0.55f}, {a.x + o + px * 0.12f, a.y + px * 0.55f}, {a.x + o + px * 0.25f, a.y + px * 0.4f}, Color{230, 190, 40, 255}); }
            if (live) { DrawRectangleLinesEx({a.x - 1, a.y - 1, px + 2, px + 2}, 2, Color{120, 200, 255, 255}); for (int q = 0; q < 3; q++) { float o = (q + 0.5f) * px / 3; DrawLineEx({a.x + o, a.y}, {a.x + o + px * 0.08f * sinf(S.t * 40 + q), a.y - px * 0.3f}, 1.5f, Color{170, 230, 255, 255}); } }
            edges(INK); break;
        }
        case sf::T_CONV_L: case sf::T_CONV_R: {
            DrawRectangleV(a, {px + 1, px + 1}, Color{70, 64, 60, 255});
            float dir = k == sf::T_CONV_R ? 1.0f : -1.0f, sh = fmodf(S.t * 3 * dir / sf::TILE * px, px * 0.5f);
            for (int q = -1; q < 3; q++) { float o = q * px * 0.5f + sh; if (o < 0 || o > px - 4) continue; DrawLineEx({a.x + o, a.y + px * 0.2f}, {a.x + o + dir * px * 0.15f, a.y + px * 0.4f}, 2, Color{200, 180, 120, 255}); DrawLineEx({a.x + o + dir * px * 0.15f, a.y + px * 0.4f}, {a.x + o, a.y + px * 0.6f}, 2, Color{200, 180, 120, 255}); }
            edges(INK); break;
        }
        default: break;
    }
}
void DrawPieces(const sf::Stage& s) {
    float px = S.zoom * sf::TILE;
    for (const auto& p : s.pieces) {
        Vector2 a = W2S({p.x * sf::TILE + p.off.x, (p.y + p.h) * sf::TILE + p.off.y});
        Rectangle r{a.x, a.y, p.w * px, p.h * px};
        switch (p.kind) {
            case sf::PK_PISTON: case sf::PK_ELEVATOR: {
                if (p.kind == sf::PK_PISTON) { Vector2 home = W2S({(p.x + p.w * 0.5f) * sf::TILE, (p.y + p.h * 0.5f) * sf::TILE}), now = {r.x + r.width / 2, r.y + r.height / 2}; Vector2 back = Vector2Subtract(home, Vector2Scale(Vector2Normalize(Vector2Subtract(now, home)), px * 0.6f)); if (Vector2Distance(now, home) > 2) { DrawLineEx(back, now, px * 0.3f + 3, INK); DrawLineEx(back, now, px * 0.3f, Color{170, 170, 176, 255}); } }
                DrawRectangleRec({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, INK);
                DrawRectangleRec(r, p.kind == sf::PK_PISTON ? Color{150, 60, 50, 255} : Color{130, 120, 80, 255});
                for (float u = 0.15f; u < 1; u += 0.35f) DrawLineEx({r.x, r.y + r.height * u}, {r.x + r.width, r.y + r.height * u}, 2, ColorAlpha(INK, 0.5f));
                if (p.kind == sf::PK_ELEVATOR) for (float u : {0.1f, 0.9f}) DrawLineEx({r.x + r.width * u, r.y}, {r.x + r.width * u, 0}, 1.5f, ColorAlpha(INK, 0.6f));   // (the cables)
                break;
            }
            case sf::PK_VENT: {
                Vector2 g = W2S({p.x * sf::TILE, (p.y + p.h) * sf::TILE});
                for (int q = 0; q < 4; q++) DrawLineEx({g.x + (q + 0.5f) * p.w * px / 4, g.y + 2}, {g.x + (q + 0.5f) * p.w * px / 4, g.y + px * 0.4f}, 2, INK);
                float u = fmodf(S.M.w.t / std::max(0.2f, p.period) + p.phase, 1.0f); bool live = u < std::clamp(p.on / p.period, 0.05f, 0.95f);
                bool warn = !live && u > 0.85f;
                if (live || warn) for (int q = 0; q < 10; q++) { float h = fmodf(S.t * 6 + q * 0.37f, 1.0f); Vector2 c = W2S({(p.x + 0.5f * p.w + 0.25f * sinf(q * 2.3f + S.t * 3)) * sf::TILE, (p.y + p.h + h * p.travel) * sf::TILE}); DrawCircleV(c, px * (0.25f + 0.3f * h) * (live ? 1 : 0.4f), ColorAlpha(WHITE, (1 - h) * (live ? 0.7f : 0.3f))); }
                break;
            }
            case sf::PK_PROPELLER: {
                Vector2 c{r.x + r.width / 2, r.y + r.height / 2}; float rad = std::min(r.width, r.height) * 0.55f;
                DrawCircleV(c, rad + 3, INK); DrawCircleV(c, rad, Color{90, 96, 100, 255});
                for (int q = 0; q < 3; q++) { float an = S.t * 14 + q * 2.094f; DrawLineEx(c, {c.x + cosf(an) * rad * 0.95f, c.y + sinf(an) * rad * 0.95f}, px * 0.35f, Color{200, 170, 90, 255}); }
                DrawCircleV(c, rad * 0.18f, INK);
                break;
            }
            case sf::PK_TUBE: { Vector2 c = W2S({(p.x + 0.5f) * sf::TILE, (p.y + 0.5f) * sf::TILE}); DrawCircleV(c, px * 0.55f, Color{200, 160, 70, 255}); DrawCircleV(c, px * 0.42f, INK); DrawCircleV({c.x + p.dx * px * 0.12f, c.y}, px * 0.18f, Color{60, 50, 44, 255}); break; }
            case sf::PK_WINDOW: if (!p.broken) DrawRectangleLinesEx({r.x, r.y, r.width, r.height}, 2.5f, Color{200, 160, 70, 255}); break;
            default: DrawWorldPiece(p, r, px); break;
        }
    }
}
void DrawStage() {
    const sf::Stage& s = S.M.w.stage;
    DrawBackdrop(s);
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) { uint8_t k = s.At(x, y); if (k != sf::T_EMPTY) DrawTile(s, x, y, k); }
    DrawPieces(s);
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
    float skA = 1;   // (stage 9: a skin draws its own line; the ghost is see-through)
    if (!cosart::Limbs(k, c, th, P, SEG, 10, &skA)) {
        for (const auto& s : SEG) L(s[0], s[1]);   // (an ink outline under the colour)
        for (const auto& s : SEG) C(s[0], s[1]);
    }
    // the head: a ring, and the face (two dots and a line); dead eyes are crosses
    Vector2 h = P(sf::J_HEAD); float r = k.pt[sf::J_HEAD].r * S.zoom;
    DrawCircleV(h, r + 2, ColorAlpha(INK, skA)); DrawCircleV(h, r, ColorAlpha(PAPER, skA)); DrawRing(h, r - th * 0.6f, r, 0, 360, 24, ColorAlpha(c, skA));
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
    cosart::HatOn(k, skA);
    // a name over the living, and the round's grab
    if (k.alive) { Vector2 n = W2S(Vector2Add(k.pt[sf::J_HEAD].p, {0, 0.38f})); DrawTextCentered(StickName(k.id), n.x, n.y - 8, 12, ColorAlpha(NameInk(), 0.7f)); }
}
void DrawBlots(float dt) {
    for (auto& b : S.blots) {
        b.life -= dt;
        if (b.v.x != 0 || b.v.y != 0) {   // (ink flies, then lands on the stone and stays put)
            b.v.y -= 9 * dt; Vector2 np = Vector2Add(b.p, Vector2Scale(b.v, dt));
            if (S.M.w.stage.Solid((int)floorf(np.x / sf::TILE), (int)floorf(np.y / sf::TILE)) || np.y < 0) b.v = {0, 0}; else b.p = np;
        }
        Vector2 s = W2S(b.p); DrawCircleV(s, b.r * S.zoom * std::min(1.0f, b.life / b.max * 2), ColorAlpha(b.c, std::min(1.0f, b.life)));
    }
    S.blots.erase(std::remove_if(S.blots.begin(), S.blots.end(), [](const Blot& b) { return b.life <= 0; }), S.blots.end());
}
void ReadEvents() {
    const auto& E = S.M.w.events; uint32_t base = S.M.w.eventBase;
    if (S.evSeen < base) S.evSeen = base;
    for (uint32_t i = S.evSeen; i < base + E.size(); i++) {
        const sf::Event& e = E[i - base];
        if (e.kind == sf::EV_HIT) Splash(e.at, SplashInk(), 5, 2.5f, 0.05f);
        if (e.kind == sf::EV_HAYMAKER) Splash(e.at, SplashInk(), 9, 4.0f, 0.07f);
        if (e.kind == sf::EV_DIE) Splash(e.at, SplashInk(), 22, 5.0f, 0.1f);
        if (e.kind == sf::EV_LAND && e.a > 4) Splash(e.at, Color{150, 138, 120, 255}, 6, 2, 0.05f);
        if (e.kind == sf::EV_SHOT) Splash(e.at, Color{60, 50, 44, 255}, 3, 1.5f, 0.035f);
        if (e.kind == sf::EV_EXPLODE) { Splash(e.at, INK, 30, 6.0f + e.a, 0.14f); Splash(e.at, Color{200, 80, 40, 255}, 10, 4.0f, 0.1f); }
        if (e.kind == sf::EV_BLOCK) Splash(e.at, Color{250, 245, 225, 255}, 8, 3.0f, 0.05f);
        if (e.kind == sf::EV_CRATE_OPEN) Splash(e.at, Color{150, 110, 60, 255}, 8, 3.0f, 0.06f);
        if (e.kind == sf::EV_WALL) S.wallMsgT = 2.5f;
        if (e.kind == sf::EV_EVENT && e.a < sf::RE_COUNT) { gEventBanner = 2.2f; gEventKind = (int)e.a; }
        if (e.kind == sf::EV_EVENT && (int)e.a == 200) { S.modeMsg = "The plank moves!"; S.modeMsgT = 2; }
        if (e.kind == sf::EV_EVENT && (int)e.a == 410) { S.modeMsg = TextFormat("%s is beaten!", sf::BossName(S.M.w.boss.kind)); S.modeMsgT = 3; Splash(e.at, SplashInk(), 40, 7, 0.14f); }
        if (e.kind == sf::EV_EVENT && (int)e.a == 300) { S.modeMsg = TextFormat("%s is through the exit", StickName(e.who)); S.modeMsgT = 2; }
        if (e.kind == sf::EV_FREEZE) Splash(e.at, Color{200, 230, 245, 255}, e.a > 0 ? 14 : 6, 3, 0.06f);
        if (e.kind == sf::EV_ZAP) Splash(e.at, Color{170, 220, 255, 255}, 6, 3, 0.04f);
        if (e.kind == sf::EV_BUBBLE && e.a > 0) Splash(e.at, Color{250, 230, 250, 255}, 8, 2.5f, 0.05f);
        if (e.kind == sf::EV_INK) Splash(e.at, INK, 26, 5, 0.12f);
        if (e.kind == sf::EV_PORTAL) Splash(e.at, Color{120, 170, 250, 255}, 6, 2, 0.05f);
    }
    S.evSeen = base + (uint32_t)E.size();
}
void StepCamera(float dt) {
    // frame the living sticks (the whole stage at most, 95 px a metre at the closest)
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; int n = 0;
    for (const auto& k : S.M.w.sticks) if (k.present && k.alive) { Vector2 p = k.pt[sf::J_PELVIS].p; x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); n++; }
    if (n > 0 && S.M.w.boss.kind >= 0) for (const auto& p : S.M.w.boss.parts) {   // (the boss is in the picture too, as far as the stage goes)
        Vector2 q{std::clamp(p.p.x, 0.0f, S.M.w.stage.Width()), std::clamp(p.p.y, 0.0f, S.M.w.stage.Height())};
        x0 = std::min(x0, q.x - p.r); x1 = std::max(x1, q.x + p.r); y0 = std::min(y0, q.y - p.r); y1 = std::max(y1, q.y + p.r);
    }
    float sw = S.M.w.stage.Width(), sh = S.M.w.stage.Height();
    float fitAll = std::min(SCREEN_W / (sw + 1.5f), SCREEN_H / (sh + 1.0f));
    Vector2 want{sw / 2, sh / 2}; float z = fitAll;
    if (n > 0) { want = {(x0 + x1) / 2, (y0 + y1) / 2 + 0.6f}; z = std::clamp(std::min(SCREEN_W / (x1 - x0 + 9.0f), SCREEN_H / (y1 - y0 + 6.0f)), fitAll, 95.0f); }
    S.zoom += (z - S.zoom) * std::min(1.0f, dt * 2.5f);
    float hw = SCREEN_W / 2.0f / S.zoom, hh = SCREEN_H / 2.0f / S.zoom;   // (never past the stage's edges by much)
    want.x = sw > 2 * hw ? std::clamp(want.x, hw - 0.75f, sw - hw + 0.75f) : sw / 2;
    want.y = sh > 2 * hh ? std::clamp(want.y, hh - 0.5f, sh - hh + 0.5f) : sh / 2;
    S.cam = Vector2Lerp(S.cam, want, std::min(1.0f, dt * 4));
}
// ---------------------------------------------------------------- the arms: crates on parachutes, weapons, bullets, the flood
void DrawWeaponAt(int wi, Vector2 grip, Vector2 muzzle, float alpha, int count = 0) {
    if (wi < 0 || wi >= (int)sf::Weapons().size()) return;
    const sf::WeaponDef& d = sf::Weapons()[wi];
    Vector2 a = W2S(grip), b = W2S(muzzle), dir = Vector2Normalize(Vector2Subtract(b, a)), n{-dir.y, dir.x};
    if (n.y > 0) n = Vector2Negate(n);
    float px = S.zoom * 0.055f; Color ink = ColorAlpha(INK, alpha);
    if (DrawWeaponSpecial(d, a, b, dir, n, px, alpha, count)) return;   // (stage 6: the rest of the arsenal)
    auto line = [&](Vector2 p, Vector2 q, float w, Color c) { DrawLineEx(p, q, w + 2.5f, ink); DrawLineEx(p, q, w, ColorAlpha(c, alpha)); };
    Color brass{200, 160, 70, 255}, steel{150, 156, 160, 255}, wood{140, 96, 54, 255}, red{180, 60, 50, 255}, green{90, 130, 70, 255};
    if (d.kind == "melee") {
        if (d.key == "fryingpan") { line(a, Vector2Lerp(a, b, 0.55f), px * 0.9f, wood); Vector2 c = Vector2Lerp(a, b, 0.8f); DrawCircleV(c, px * 3.6f, ink); DrawCircleV(c, px * 3.0f, ColorAlpha(Color{60, 60, 64, 255}, alpha)); return; }
        line(a, Vector2Lerp(a, b, 0.25f), px * 1.2f, brass);
        line(Vector2Lerp(a, b, 0.22f), Vector2Add(b, Vector2Scale(n, px * 1.5f)), px * 1.4f, steel);   // (the blade, a little curved)
        DrawLineEx(Vector2Add(Vector2Lerp(a, b, 0.22f), Vector2Scale(n, px * 2)), Vector2Subtract(Vector2Lerp(a, b, 0.22f), Vector2Scale(n, px * 2)), px, ink);
        return;
    }
    Color body = d.key == "rocket" ? red : d.key == "grenadelauncher" ? green : (d.key == "gaspistol" || d.key == "twinpistols" || d.key == "needler") ? brass : steel;
    float len = d.key == "sniper" || d.key == "harpoon" ? 1.0f : d.key == "gaspistol" || d.key == "twinpistols" || d.key == "needler" ? 0.6f : 0.85f;
    Vector2 tip = Vector2Lerp(a, b, len), mid = Vector2Lerp(a, tip, 0.5f);
    float thick = d.key == "minigun" || d.key == "rocket" ? px * 2.4f : d.key == "scatter" ? px * 1.9f : px * 1.5f;
    line(Vector2Subtract(a, Vector2Scale(dir, px * 3)), tip, thick, body);
    line(mid, Vector2Add(mid, Vector2Scale(n, -px * 3.5f)), px * 1.1f, wood);   // (the grip)
    if (d.key == "sniper") DrawCircleV(Vector2Add(mid, Vector2Scale(n, px * 1.8f)), px * 1.2f, ink);   // (the scope)
    if (d.key == "harpoon") line(tip, Vector2Add(tip, Vector2Scale(dir, px * 4)), px * 0.6f, steel);
    if (d.key == "twinpistols") line(Vector2Add(a, Vector2Scale(n, px * 2)), Vector2Add(Vector2Lerp(a, b, 0.6f), Vector2Scale(n, px * 2)), px * 1.4f, brass);
}
void DrawArms() {
    const sf::World& w = S.M.w;
    for (const auto& it : w.items) {
        if (!it.alive) continue;
        if (it.crate) {
            Vector2 c = W2S(it.a.p); float s = it.a.r * S.zoom;
            if (it.chute) {   // the parachute: an inked dome on four lines
                Vector2 top = W2S(Vector2Add(it.a.p, {0, 0.9f})); float r = 0.55f * S.zoom;
                DrawCircleSector(top, r + 2, 180, 360, 20, INK); DrawCircleSector(top, r, 180, 360, 20, Color{236, 226, 200, 255});
                for (float u : {-1.0f, -0.35f, 0.35f, 1.0f}) DrawLineEx({top.x + u * r, top.y}, {c.x + u * s * 0.8f, c.y - s}, 1.5f, INK);
            }
            DrawRectangleRec({c.x - s - 2, c.y - s - 2, 2 * s + 4, 2 * s + 4}, INK);
            DrawRectangleRec({c.x - s, c.y - s, 2 * s, 2 * s}, Color{170, 120, 64, 255});
            DrawLineEx({c.x - s, c.y - s}, {c.x + s, c.y + s}, 2, INK); DrawLineEx({c.x + s, c.y - s}, {c.x - s, c.y + s}, 2, INK);
            continue;
        }
        if (it.holder >= 0) continue;   // (in a hand: drawn with the stick)
        DrawWeaponAt(it.weapon, it.a.p, it.b.p, 1, it.count);
    }
    for (const auto& b : w.bullets) {
        if (DrawHazardBullet(b)) continue;
        Vector2 p = W2S(b.p), q = W2S(Vector2Subtract(b.p, Vector2Scale(Vector2Normalize(b.v), std::min(0.6f, Vector2Length(b.v) * 0.012f))));
        if (b.explode) { DrawCircleV(p, S.zoom * 0.09f + 2, INK); DrawCircleV(p, S.zoom * 0.09f, b.fuse > 0 ? Color{90, 130, 70, 255} : Color{180, 60, 50, 255}); }
        else { DrawLineEx(q, p, 3.5f, INK); DrawLineEx(q, p, 1.5f, b.deflected ? Color{250, 240, 200, 255} : Color{120, 100, 80, 255}); }
    }
    DrawLiquids();   // (water over the sticks)
    DrawWallFx();    // (each world's wall)
}
void DrawHeld(const sf::Stick& k) {
    if (!k.present || k.weapon < 0 || k.weapon >= (int)S.M.w.items.size()) return;
    const sf::Item& it = S.M.w.items[k.weapon];
    DrawWeaponAt(it.weapon, it.a.p, it.b.p, 1, it.count);
}
// ---------------------------------------------------------------- the modes in the world (stage 7): the plank, the exit, the egg, mirrors, balloons, sharks
void DrawModeGround() {
    const sf::World& w = S.M.w; float px = S.zoom * sf::TILE;
    if (w.mode == sf::MD_KING) {   // the marked plank: gold rope along its top, flags at both ends; it flashes when it's about to move
        Vector2 a = W2S({w.plank.x * sf::TILE, w.plank.y * sf::TILE}), b = W2S({(w.plank.x + w.plank.width) * sf::TILE, w.plank.y * sf::TILE});
        float fl = w.plankT < 3 && fmodf(S.t * 4, 1.0f) < 0.5f ? 0.4f : 1;
        DrawRectangleV({a.x, a.y - 5}, {b.x - a.x, 7}, ColorAlpha(Color{240, 196, 60, 255}, 0.85f * fl));
        DrawLineEx({a.x, a.y - 5}, {b.x, a.y - 5}, 2, INK);
        for (Vector2 e : {a, b}) { DrawLineEx(e, {e.x, e.y - px * 1.6f}, 3, INK); DrawTri({e.x, e.y - px * 1.6f}, {e.x + px * 0.6f, e.y - px * 1.35f}, {e.x, e.y - px * 1.1f}, ColorAlpha(Color{240, 196, 60, 255}, fl)); }
    }
    if (w.mode == sf::MD_GAUNTLET) {   // the exit: a hatch in a brass frame with a chequered flag over it
        Vector2 g = W2S(w.goal);
        DrawRectangleV({g.x - px * 0.6f, g.y - px * 2.2f}, {px * 1.2f, px * 2.2f}, Color{40, 34, 30, 255});
        DrawRectangleLinesEx({g.x - px * 0.6f, g.y - px * 2.2f, px * 1.2f, px * 2.2f}, 3, Color{200, 160, 70, 255});
        Vector2 pole{g.x + px * 0.8f, g.y};
        DrawLineEx(pole, {pole.x, pole.y - px * 3}, 3, INK);
        float wave = sinf(S.t * 5) * px * 0.06f;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) DrawRectangleV({pole.x + i * px * 0.22f, pole.y - px * 3 + j * px * 0.22f + wave * i}, {px * 0.22f, px * 0.22f}, (i + j) % 2 ? WHITE : INK);
    }
}
void DrawModeOver() {
    const sf::World& w = S.M.w; float px = S.zoom * sf::TILE;
    for (const auto& th : w.things) {
        if (!th.alive) continue;
        if (th.kind == sf::TH_EGG && th.a <= 0) {   // the egg: speckled, wobbling when it's loose
            Vector2 c = W2S(th.p); float r = px * 0.32f, wob = th.hold < 0 ? sinf(S.t * 6) * 0.08f : 0;
            DrawEllipse((int)c.x, (int)c.y, r * 0.85f + 2, r * 1.1f + 2, INK); DrawEllipse((int)c.x, (int)c.y, r * 0.85f, r * 1.1f, Color{246, 238, 214, 255});
            for (int i = 0; i < 5; i++) DrawCircleV({c.x + sinf(i * 2.1f + wob) * r * 0.5f, c.y + cosf(i * 1.3f) * r * 0.6f}, 1.6f, Color{150, 120, 90, 255});
            if (th.hold < 0) { float u = 0.5f + 0.5f * sinf(S.t * 3); DrawRing(c, r * 1.5f, r * 1.5f + 2, 0, 360, 24, ColorAlpha(Color{240, 196, 60, 255}, u)); }
        }
        if (th.kind == sf::TH_MIRROR) {   // a mirror on a stand: silvered glass, a brass frame
            Vector2 b = W2S(th.p), t = W2S({th.p.x, th.p.y + 1.3f});
            DrawRectangleV({b.x - 5, t.y}, {10, b.y - t.y}, Color{200, 160, 70, 255});
            DrawRectangleV({b.x - 3, t.y + 2}, {6, b.y - t.y - 4}, ColorLerp(Color{200, 220, 230, 255}, WHITE, 0.4f + 0.3f * sinf(S.t * 2)));
            DrawRectangleLinesEx({b.x - 5, t.y, 10, b.y - t.y}, 1.5f, INK);
        }
    }
    for (const auto& k : w.sticks) {
        if (!k.present || !k.alive) continue;
        Vector2 h = W2S(k.pt[sf::J_HEAD].p);
        if (k.balloonT > 0) {   // a balloon on a string from the hand
            Vector2 hand = W2S(k.pt[sf::J_HAND_L].p), b{hand.x + sinf(S.t * 2) * 6, h.y - px * 1.8f};
            DrawLineEx(hand, b, 1.2f, INK); DrawEllipse((int)b.x, (int)b.y - 12, 13, 16, INK); DrawEllipse((int)b.x, (int)b.y - 12, 11, 14, ColorAlpha(StickColor(k.id), 0.9f));
        }
        if (k.gear == sf::GR_FISHBOWL) { float r = k.pt[sf::J_HEAD].r * S.zoom * 1.6f; DrawRing(h, r, r + 2, 0, 360, 24, ColorAlpha(Color{180, 220, 240, 255}, 0.8f)); DrawCircleV(h, r, ColorAlpha(Color{180, 220, 240, 255}, 0.15f)); }
        if (k.shark) { Vector2 f{h.x, h.y - k.pt[sf::J_HEAD].r * S.zoom - 4}; DrawTri({f.x - 9, f.y}, {f.x + 7, f.y}, {f.x + 4, f.y - 16}, Color{110, 130, 150, 255}); DrawLineEx({f.x - 9, f.y}, {f.x + 4, f.y - 16}, 1.5f, INK); }   // (the shark's fin)
        if (k.team >= 0 && S.M.mode != sf::MD_HUNT) DrawRing({h.x, h.y}, k.pt[sf::J_HEAD].r * S.zoom + 3, k.pt[sf::J_HEAD].r * S.zoom + 6, 0, 360, 24, TeamColor(k.team));
    }
}
// the world, in order: the stage, burning wood, the dead, the living with what they hold and what's happening to them,
// crates, loose weapons and bullets (and the water over all of it), the things the arsenal leaves, ink, and the screen's effects
void DrawWorld(float dt) {
    DrawStage();
    DrawModeGround();
    DrawFires();
    bossart::Danger(S.M.w.boss);
    bossart::Draw();
    for (const auto& k : S.M.w.sticks) if (!k.alive) DrawStick(k);
    for (const auto& k : S.M.w.sticks) if (k.alive) { DrawStick(k); DrawHeld(k); DrawStickStatus(k); }
    DrawThings();
    DrawModeOver();
    cosart::LooseHats();
    DrawArms();
    DrawBlots(dt);
    DrawScreenFx(dt);
}
void DrawHud() {
    const sf::Match& M = S.M;
    // the scoreboard: each stick's colour, its round wins as pips, and its points
    float x = 16;
    for (int i = 0; i < S.players; i++) {
        const sf::Stick& k = M.w.sticks[i];
        DrawRectangleRounded({x, 12, 150, 38}, 0.3f, 6, ColorAlpha(i == S.me && S.net ? Color{60, 44, 30, 255} : Color{20, 16, 14, 255}, 0.75f));
        DrawCircleV({x + 13, 25}, 7, k.alive ? StickColor(i) : Color{110, 106, 100, 255});
        Txt(StickName(i), x + 26, 16, 13, Color{240, 230, 210, 255});
        bool timed = M.mode == sf::MD_KING || M.mode == sf::MD_EGG;
        Txt(timed && i < (int)M.w.pts.size() ? TextFormat("%.0f / %.0f", M.w.pts[i], M.target) : k.shark ? "SHARK" : M.mode == sf::MD_GAUNTLET && k.finished >= 0 ? TextFormat("out at %.1f s", k.finished) : TextFormat("%d", M.score[i]), x + 26, 33, 11, k.shark ? Color{150, 190, 230, 255} : Color{200, 190, 170, 255});
        if (k.team >= 0 && M.mode != sf::MD_HUNT) DrawRectangle((int)x, 46, 150, 3, TeamColor(k.team));
        if (k.trinket != sf::TK_NONE) Txt(sf::TrinketName(k.trinket), x + 4, 0, 10, DarkWorld(M.w.stage.world) ? Color{220, 190, 120, 255} : Color{120, 80, 30, 255});   // (everyone can see what everyone took)
        for (int wn = 0; wn < S.toWin && wn < 20; wn++) DrawCircleV({x + 72 + (wn % 10) * 7.5f, 37.0f + (wn / 10) * 7}, 2.6f, wn < M.wins[i] ? StickColor(i) : Color{80, 74, 66, 255});
        x += 158;
    }
DrawTextCentered(sf::ModeName(M.mode), SCREEN_W / 2.0f, 74, 13, ColorAlpha(NameInk(), 0.8f));
    DrawTextCentered(TextFormat("Round %d   -   first to %d   -   %s%s", M.round, S.toWin, M.w.stage.name.c_str(), M.w.finale && M.mode != sf::MD_DUEL && M.mode != sf::MD_GAUNTLET ? "   (match point: the wall at 30 s)" : ""), SCREEN_W / 2.0f, 58, 14, NameInk());
    // your weapon and its ammo
    const sf::Stick& me = M.w.sticks[std::clamp(S.me, 0, (int)M.w.sticks.size() - 1)];
    if (me.weapon >= 0 && me.weapon < (int)M.w.items.size()) { const sf::Item& it = M.w.items[me.weapon]; const sf::WeaponDef& d = sf::Weapons()[it.weapon]; DrawTextCenteredBold(d.kind == "gun" ? TextFormat("%s   %d", d.name.c_str(), it.ammo) : d.name.c_str(), SCREEN_W / 2.0f, SCREEN_H - 50.0f, 18, it.ammo == 0 && d.kind == "gun" ? Color{170, 60, 40, 255} : NameInk()); if (it.ammo == 0 && d.kind == "gun") DrawTextCentered("(empty: click to throw it)", SCREEN_W / 2.0f, SCREEN_H - 30.0f, 12, NameInk()); }
    if (me.gear >= 0) DrawTextCentered(TextFormat("%s%s   (E or right mouse)", sf::GearName(me.gear), me.gear == sf::GR_JETPACK ? TextFormat(": %.0f%%", me.gearFuel / 3 * 100) : ""), SCREEN_W / 2.0f, SCREEN_H - 72.0f, 13, NameInk());
    if (me.carry >= 0 && me.carry < (int)M.w.items.size()) DrawTextCentered(TextFormat("on your back: %s (E swaps)", sf::Weapons()[M.w.items[me.carry].weapon].name.c_str()), SCREEN_W / 2.0f, SCREEN_H - 88.0f, 12, NameInk());
    if (M.mode == sf::MD_GAUNTLET) {   // the run: the stage, the clock, the deaths
        DrawTextCenteredBold(TextFormat("Stage %d   %.1f s   %d deaths   (%.0f s left)", M.gStage + 1, M.gTime + M.w.t, M.gDeaths, std::max(0.0f, std::max(25.0f, 45.0f - M.gStage) - M.w.t)), SCREEN_W / 2.0f, 92, 20, NameInk());
    }
    if (M.mode == sf::MD_KING && M.w.plankT < 3 && M.phase == sf::Match::P_FIGHT) DrawTextCentered(TextFormat("the plank moves in %.0f", ceilf(M.w.plankT)), SCREEN_W / 2.0f, 92, 14, NameInk());
    if (me.alive == false && me.respawnT > 0) DrawTextCenteredBold(TextFormat("Back in %.1f", me.respawnT), SCREEN_W / 2.0f, SCREEN_H / 2.0f + 60, 26, NameInk());
    if (S.modeMsgT > 0) DrawTextCenteredBold(S.modeMsg.c_str(), SCREEN_W / 2.0f, M.mode == sf::MD_BOSS ? 150.0f : 116.0f, 20, NameInk());
    if (M.mode == sf::MD_BOSS) bossart::Hud();
    if (M.mode == sf::MD_DUEL && M.phase == sf::Match::P_COUNT) {   // the Duel: three weapons on cards; 1-3 or a click picks yours
        DrawTextCenteredBold("Pick your weapon for the round (1, 2, 3)", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 74, 20, NameInk());
        for (int q = 0; q < 3; q++) {
            Rectangle r = DuelCard(q); int wi = M.duelOffer[q]; bool mine = S.me < sf::MAX_STICKS && M.duelPick[S.me] == q + 1;
            DrawRectangleRounded(r, 0.12f, 6, ColorAlpha(mine ? Color{70, 52, 30, 255} : Color{20, 16, 14, 255}, 0.85f));
            DrawRectangleRoundedLinesEx(r, 0.12f, 6, mine ? 3.0f : 1.5f, Color{200, 160, 70, 255});
            if (wi >= 0 && wi < (int)sf::Weapons().size()) {
                float z = S.zoom; S.zoom = 70; Vector2 c{r.x + r.width / 2, r.y + 50};
                Vector2 wc = S2W(c); DrawWeaponAt(wi, {wc.x - 0.4f, wc.y}, {wc.x + 0.6f, wc.y}, 1); S.zoom = z;
                DrawTextCentered(sf::Weapons()[wi].name.c_str(), r.x + r.width / 2, r.y + 80, 14, Color{240, 230, 210, 255});
            }
            DrawTextCenteredBold(TextFormat("%d", q + 1), r.x + 14, r.y + 6, 16, Color{200, 160, 70, 255});
        }
    }
    if (S.wallMsgT > 0) DrawTextCenteredBold(WallLine(M.w.stage.world), SCREEN_W / 2.0f, 90, 26, DarkWorld(M.w.stage.world) ? Color{230, 210, 160, 255} : Color{40, 70, 110, 255});
    if (M.phase == sf::Match::P_COUNT) DrawTextCenteredBold("FIGHT!", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 56, Color{40, 30, 26, (unsigned char)(255 * std::clamp(1.4f - M.phaseT, 0.0f, 1.0f))});
    if (M.phase == sf::Match::P_WIN) DrawTextCenteredBold(M.roundWinner >= 0 ? TextFormat("%s %s the round", StickName(M.roundWinner), M.roundWinner == S.me && !S.net ? "take" : "takes") : M.roundWinner == -2 && !M.log.empty() ? M.log.back().c_str() : M.mode == sf::MD_BOSS && !M.log.empty() ? M.log.back().c_str() : "A draw: everyone at once", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 34, M.roundWinner >= 0 ? StickColor(M.roundWinner) : INK);
    if (M.phase == sf::Match::P_WIN && !S.net && !S.shot && !S.rec.steps.empty()) DrawTextCentered("R: watch it again", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 80, 16, NameInk());
    if (M.phase == sf::Match::P_OVER && M.mode == sf::MD_BOSS) {   // the run's end: the bosses beaten, or the one that won
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(PAPER, 0.85f));
        DrawTextCenteredBold(M.gFailed ? TextFormat("%s wins", sf::BossName(M.w.boss.kind)) : M.toWin > 1 ? "Every boss beaten!" : TextFormat("%s is beaten!", sf::BossName(M.w.boss.kind)), SCREEN_W / 2.0f, 120, 40, INK);
        DrawTextCentered(TextFormat("%d of %d bosses down, %.0f s of fighting, %d deaths", M.gStage, M.toWin, M.gTime + (M.gFailed ? M.w.t : 0), M.gDeaths), SCREEN_W / 2.0f, 170, 18, INK);
        for (int i = 0; i < S.players && i < (int)M.w.sticks.size(); i++) DrawTextCentered(TextFormat("%s: %.0f damage to %s", StickName(i), M.w.boss.dmgBy[i], sf::BossName(M.w.boss.kind)), SCREEN_W / 2.0f, 220 + i * 22.0f, 15, StickColor(i));
        return;
    }
    if (M.phase == sf::Match::P_OVER && M.mode == sf::MD_GAUNTLET) {   // the run's end: how far, how fast, and the board
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(PAPER, 0.85f));
        DrawTextCenteredBold(TextFormat("The Gauntlet: %d stage%s cleared", M.gStage, M.gStage == 1 ? "" : "s"), SCREEN_W / 2.0f, 120, 40, INK);
        DrawTextCentered(TextFormat("%.1f s on the clock, %d deaths%s", M.gTime, M.gDeaths, M.gFailed ? ", out of time" : ""), SCREEN_W / 2.0f, 170, 18, INK);
        DrawTextCenteredBold("Best runs on this ship", SCREEN_W / 2.0f, 220, 18, INK);
        for (int i = 0; i < (int)S.board.size() && i < 10; i++) DrawTextCentered(S.board[i].c_str(), SCREEN_W / 2.0f, 250 + i * 22.0f, 15, i == S.boardMine ? Color{180, 60, 40, 255} : INK);
        return;
    }
    if (M.phase == sf::Match::P_OVER) {
        int best = std::max(0, M.champion);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(PAPER, 0.8f));
        DrawTextCenteredBold(TextFormat("%s %s the match", StickName(best), best == S.me && !S.net ? "win" : "wins"), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 90, 44, StickColor(best));
        for (int i = 0; i < S.players; i++) DrawTextCentered(TextFormat("%s: %d rounds, %d points", StickName(i), M.wins[i], M.score[i]), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 30 + i * 22.0f, 16, INK);
    }
    if (M.phase != sf::Match::P_OVER) DrawTextCentered("A/D run   W or Space jump   S duck (or dive in the air; duck on a gun to swap)   mouse aims, click fires or punches (hold against someone: grab)   T taunt", SCREEN_W / 2.0f, SCREEN_H - 14.0f, 11, ColorAlpha(NameInk(), 0.6f));
}
} // namespace

int gScuffleTrinket = -1, gScuffleRules = 0, gScuffleMode = 0;   // (the arcade's picks: your trinket (-1: the game picks); the rules: 0 none, 1 Random each round, 2+ one mutator)
uint32_t ScuffleRulesMask(int r) { return r >= 2 ? 1u << (r - 2) : 0; }
// ---------------------------------------------------------------- replays (stage 9): record each round; R at its end watches the last 10 s
static void RecBegin() { sf::ReplayBegin(S.rec, S.M, S.names, TextFormat("Round %d, %s", S.M.round, S.M.w.stage.name.c_str())); S.recEnded = false; }
static void RecEnd() {
    // the round's score (its kills, more for a close finish), and where the last death fell: the slow-motion's mark
    if (S.recEnded) return; S.recEnded = true;
    const auto& E = S.M.w.events; int kills = 0; for (const auto& e : E) if (e.kind == sf::EV_DIE) { kills++; S.rec.killAt = e.at; }
    S.rec.killT = kills ? (float)S.rec.steps.size() : -1; S.rec.score = kills + (S.M.w.t < 15 ? 1.0f : 0.0f) + (S.M.roundWinner == 0 ? 0.5f : 0.0f);
    S.best.push_back(S.rec); std::sort(S.best.begin(), S.best.end(), [](const sf::Replay& a, const sf::Replay& b) { return a.score > b.score; }); if (S.best.size() > 3) S.best.resize(3);
}
static void SaveBest() {
    if (S.savedBest || S.best.empty()) return; S.savedBest = true;
    MakeDirectory("scuffle_replays");
    time_t now = time(nullptr); char stamp[32]; strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", localtime(&now));
    int n = 0; for (size_t i = 0; i < S.best.size(); i++) if (sf::SaveReplay(TextFormat("scuffle_replays/%s_%d.scr", stamp, (int)i + 1), S.best[i])) n++;
    S.savedMsg = TextFormat("The match's best %d round%s saved (the locker plays them)", n, n == 1 ? "" : "s");
}
static void ViewStart(const sf::Replay& r, bool lastTen) {
    S.view = r; S.live = S.M; S.liveNames = S.names;
    std::vector<std::string> nm; if (!sf::ReplayStart(S.view, S.M, &nm)) { S.M = S.live; return; }
    if (!S.view.names.empty()) S.names = S.view.names;
    size_t from = lastTen && S.view.steps.size() > 1200 ? S.view.steps.size() - 1200 : 0;
    for (S.vIdx = 0; S.vIdx < from; S.vIdx++) sf::ReplayStep(S.view, S.vIdx, S.M);
    S.players = S.M.players; S.toWin = S.M.toWin; S.vAcc = 0; S.viewing = true; S.blots.clear(); S.evSeen = S.M.w.eventBase + (uint32_t)S.M.w.events.size();
}
static void ViewEnd(Game& g) {
    S.viewing = false; S.blots.clear();
    if (S.fileView) { S.active = false; S.fileView = false; g.scene = Scene::Arcade; return; }
    S.M = S.live; S.names = S.liveNames; S.players = S.M.players; S.toWin = S.M.toWin; S.evSeen = S.M.w.eventBase + (uint32_t)S.M.w.events.size();
}
static void ViewFrame(Game& g, float dt) {
    // real time, then a third of it over the last two seconds of a round that ended in a kill, the camera on the kill
    size_t n = S.view.steps.size();
    bool slow = S.view.killT >= 0 && S.vIdx + 240 > n && S.vIdx < n + 60;
    S.vAcc += dt * (slow ? 0.33f : 1.0f);
    while (S.vAcc >= sf::STEP && S.vIdx < n + 90) { S.vAcc -= sf::STEP; sf::ReplayStep(S.view, S.vIdx, S.M); S.vIdx++; }
    ReadEvents();
    StepCamera(dt);
    if (slow) { S.cam = Vector2Lerp(S.cam, S.view.killAt, std::min(1.0f, dt * 3)); S.zoom += (95 - S.zoom) * std::min(1.0f, dt * 2); }
    DrawWorld(dt);
    DrawRectangle(0, 0, SCREEN_W, 34, ColorAlpha(INK, 0.75f)); DrawRectangle(0, SCREEN_H - 34, SCREEN_W, 34, ColorAlpha(INK, 0.75f));
    DrawTextCenteredBold(TextFormat("REPLAY   %s%s", S.view.title.c_str(), slow ? "   (slow motion)" : ""), SCREEN_W / 2.0f, 7, 18, Color{240, 220, 170, 255});
    DrawTextCentered("Space, R or Esc: skip", SCREEN_W / 2.0f, SCREEN_H - 26.0f, 14, Color{220, 210, 190, 255});
    if (fmodf(S.t, 1.0f) < 0.6f) DrawCircleV({24, 17}, 7, Color{220, 40, 30, 255});
    if (S.vIdx >= n + 90 || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ESCAPE)) ViewEnd(g);
}
void StartScuffleReplay(Game& g, const std::string& path) {
    sf::Replay r; if (!sf::LoadReplay(path, r)) return;
    S = ScuffleScene{}; S.active = true; S.fileView = true; gEventBanner = 0;
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    ViewStart(r, false); if (!S.viewing) { S.active = false; return; }
    S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 60;
    g.scene = Scene::Scuffle;
}
void StartScuffle(Game& g, int bots, int skill, int toWin, int world) {
    S = ScuffleScene{};
    gEventBanner = 0; gEventKind = -1;   // (no banner left over from the last match)
    S.active = true; S.players = gScuffleMode == sf::MD_DUEL ? 2 : std::clamp(bots + 1, gScuffleMode == sf::MD_HUNT ? 3 : 2, sf::MAX_STICKS); S.skill = std::clamp(skill, 0, 2); S.toWin = std::clamp(toWin, 1, 20);
    S.M.mode = gScuffleMode; S.M.world = world; S.M.mutators = ScuffleRulesMask(gScuffleRules); S.M.randomMutator = gScuffleRules == 1;
    S.M.trinkets.assign(S.players, -1); S.M.trinkets[0] = gScuffleTrinket;
    S.M.skins.assign(S.players, -2); S.M.hats.assign(S.players, -2); S.M.skins[0] = sf::SkinIndex(sf::MyLocker().skin); S.M.hats[0] = sf::HatIndex(sf::MyLocker().hat);   // (yours from the locker; the bots dressed by the seed)
    S.M.Start(S.players, S.toWin, (uint32_t)GetRandomValue(1, 1 << 30), S.arsenal); S.toWin = S.M.toWin;
    S.botRng.resize(S.players);
    for (int i = 0; i < S.players; i++) S.botRng[i] = 1234567u + i * 7919u + (uint32_t)GetRandomValue(0, 1 << 20);
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 60; S.lastRound = S.M.round;
    RecBegin();
    g.scene = Scene::Scuffle;
}
// ---------------------------------------------------------------- a networked match (stage 4)
void StartScuffleNet(Game& g, arcade::Session* net, const char* name) {
    S = ScuffleScene{};
    gEventBanner = 0; gEventKind = -1;
    S.active = true; S.net = net; S.netName = name ? name : "Stick"; S.me = std::max(0, net->MyPlayer());
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    S.zoom = 60; S.lastRound = -1;
    g.scene = Scene::Scuffle;
}
std::string ScuffleOpts(int toWin, int arsenal, int skill, int world, uint32_t mutators, bool randomMutator) { return sf::ScuffleHostOpts(toWin, arsenal, skill, world, mutators, randomMutator, gScuffleMode); }
const char* ScuffleTrinketChoice(int t) { return t < 0 ? "the game picks your trinket" : TextFormat("trinket: %s", sf::TrinketName(t)); }
const char* ScuffleRulesChoice(int r) { return r <= 0 ? "no mutators" : r == 1 ? "a Random mutator each round" : sf::MutatorName(r - 2); }
const char* ScuffleWorldChoice(int w) { return w < 0 ? "all six worlds" : w >= sf::WD_COUNT ? "endless (the generator)" : sf::WorldName(w); }
// the host: its own stick's inputs go through the session numbered like anyone's; it draws a copy of the real match.
// A guest: reads each snapshot into the predictor (which replays what the host hasn't used yet), steps its own stick
// ahead, and glides whatever the snapshot moved (S.vis fades in about a tenth of a second)
static bool NetTick(float dt, bool steer) {
    arcade::Session& N = *S.net;
    N.Update(GetTime(), dt);
    if (N.stage != arcade::S_PLAYING) return false;
    if (!S.helloSent) { Writer o; sf::OrderHello(o, S.netName, gScuffleTrinket, sf::SkinIndex(sf::MyLocker().skin), sf::HatIndex(sf::MyLocker().hat)); N.Act(o); S.helloSent = true; }
    std::vector<sf::InputFrame> box;
    if (N.role == arcade::R_HOST) {
        sf::Match* hm = sf::ScuffleHostMatch(N.HostGame());
        if (!hm) return true;
        S.me = std::clamp(N.MyPlayer(), 0, (int)hm->w.sticks.size() - 1);
        S.acc += dt;
        while (S.acc >= sf::STEP) { S.acc -= sf::STEP; sf::Input in; if (steer) Gather(in, hm->w.sticks[S.me]); box.push_back({S.hostSeq++, sf::QuantizeInput(in)}); }
        if (!box.empty() && steer) { Writer w; sf::WriteInputs(box, w); N.Act(w); }
        S.M = *hm; if (const auto* nm = sf::ScuffleHostNames(N.HostGame())) S.names = *nm;
        return true;
    }
    sf::Predictor& P = S.pred;
    if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
        S.seenVersion = N.stateVersion;
        bool had = P.have; int rb = P.m.round; Vector2 before[sf::MAX_STICKS] = {};
        if (had) for (size_t i = 0; i < P.m.w.sticks.size() && i < (size_t)sf::MAX_STICKS; i++) before[i] = P.m.w.sticks[i].pt[sf::J_PELVIS].p;
        Reader r(N.Snapshot());
        if (P.Apply(r)) {
            for (size_t i = 0; i < P.m.w.sticks.size() && i < (size_t)sf::MAX_STICKS; i++) {
                Vector2 d = Vector2Subtract(before[i], P.m.w.sticks[i].pt[sf::J_PELVIS].p);
                S.vis[i] = had && rb == P.m.round && Vector2Length(Vector2Add(S.vis[i], d)) < 2.0f ? Vector2Add(S.vis[i], d) : Vector2{0, 0};
            }
        }
    }
    if (!P.have) return true;
    S.me = P.me;
    if (steer) {
        S.acc += dt;
        while (S.acc >= sf::STEP) { S.acc -= sf::STEP; sf::Input in; Gather(in, P.m.w.sticks[P.me]); P.Local(in, box); }
        if (!box.empty()) { Writer w; sf::WriteInputs(box, w); N.Act(w); }
    }
    return true;
}
static bool EditorPlaying();
// the match's tokens into the locker, once (a match is its seed and its last round)
static void PayOut() {
    if (S.shot || EditorPlaying() || !S.M.Over()) return;
    uint64_t key = ((uint64_t)S.M.seed << 16) ^ (uint64_t)S.M.round ^ 0x5F00000000ull; if (key == S.paidKey) return;
    S.paidKey = key; S.paid = sf::MatchTokens(S.M, S.me);
    sf::Locker& L = sf::MyLocker(); L.tokens += S.paid; L.matches++; if (S.M.champion == S.me || (S.M.mode == sf::MD_BOSS && !S.M.gFailed)) L.wins++; sf::SaveLocker();
}
static void NetFrame(Game& g, float dt) {
    if (!NetTick(dt, true)) { S.net = nullptr; S.active = false; g.scene = Scene::Arcade; return; }
    arcade::Session& N = *S.net;
    bool host = N.role == arcade::R_HOST;
    if (!host) {
        if (!S.pred.have) { ClearBackground(PAPER); DrawTextCenteredBold("Into the ring...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, INK); return; }
        S.M = S.pred.m; S.M.w.events = S.pred.hostEvents; S.M.w.eventBase = S.pred.hostEventBase; S.names = S.pred.names;
        // the glide: each stick (and what it holds) drawn where it was, easing to where it is
        float k = expf(-dt * 14);
        for (size_t i = 0; i < S.M.w.sticks.size() && i < (size_t)sf::MAX_STICKS; i++) {
            S.vis[i] = Vector2Scale(S.vis[i], k);
            sf::Stick& s = S.M.w.sticks[i];
            for (auto& p : s.pt) p.p = Vector2Add(p.p, S.vis[i]);
            if (s.weapon >= 0 && s.weapon < (int)S.M.w.items.size()) { auto& it = S.M.w.items[s.weapon]; it.a.p = Vector2Add(it.a.p, S.vis[i]); it.b.p = Vector2Add(it.b.p, S.vis[i]); }
        }
    }
    S.players = S.M.players; S.toWin = S.M.toWin;
    if (S.M.round != S.lastRound) { S.lastRound = S.M.round; S.blots.clear(); S.evSeen = S.M.w.eventBase + (uint32_t)S.M.w.events.size(); }
    ReadEvents();
    StepCamera(dt);
    DrawWorld(dt);
    DrawHud();
    if (N.paused) DrawTextCenteredBold(TextFormat("Waiting for a lost player (%.0f s)", N.pauseLeft), SCREEN_W / 2.0f, 120, 18, Color{150, 50, 40, 255});
    PayOut();
    if (S.M.Over()) {
        DrawTextCenteredBold(TextFormat("+%d tokens for the locker (%d in all)", S.paid, sf::MyLocker().tokens), SCREEN_W / 2.0f, SCREEN_H / 2.0f + 120, 16, Color{170, 110, 30, 255});
        if (host) {
            std::string why;
            if (Button({SCREEN_W / 2.0f - 230, SCREEN_H / 2.0f + 160, 200, 40}, "Rematch", true, 16)) { N.Rematch(&why); S.lastRound = -1; S.blots.clear(); }
            if (Button({SCREEN_W / 2.0f + 30, SCREEN_H / 2.0f + 160, 200, 40}, "Back to the lobby", true, 16)) { N.BackToLobby(); S.net = nullptr; S.active = false; g.scene = Scene::Arcade; }
        } else {
            DrawTextCentered("The host chooses: a rematch, or back to the lobby.", SCREEN_W / 2.0f, SCREEN_H / 2.0f + 140, 15, INK);
            if (Button({SCREEN_W / 2.0f - 100, SCREEN_H / 2.0f + 170, 200, 40}, "Leave the table", true, 16)) { N.Leave(); S.net = nullptr; S.active = false; g.scene = Scene::Arcade; }
        }
    }
}
void ScuffleMenuTick(float dt) { if (S.active && S.net) NetTick(dt, false); }   // (my stick stands; after 1.5 s the host's bot fights for me)

static bool EditorFrame(Game& g);
static bool EditorPlaying();
static void EditorBackFromPlay();
void SceneScuffle(Game& g) {
    float dt = std::min(GetFrameTime(), 1 / 20.0f);
    if (S.active && S.net) { S.t += dt; S.wallMsgT = std::max(0.0f, S.wallMsgT - dt); S.modeMsgT = std::max(0.0f, S.modeMsgT - dt); NetFrame(g, dt); return; }
    if (EditorFrame(g)) return;
    if (EditorPlaying() && IsKeyPressed(KEY_P)) { EditorBackFromPlay(); return; }
    if (!S.active) { StartScuffle(g, 3, 2, 5); }
    if (S.shot) dt = 1 / 60.0f;
    S.t += dt; S.wallMsgT = std::max(0.0f, S.wallMsgT - dt); S.modeMsgT = std::max(0.0f, S.modeMsgT - dt);
    if (S.viewing) { ViewFrame(g, dt); return; }
    if (S.M.phase == sf::Match::P_WIN && !S.shot && !EditorPlaying() && IsKeyPressed(KEY_R) && !S.rec.steps.empty()) { ViewStart(S.rec, true); return; }
    // the inputs (you and the bots), then the fixed steps (the match runs the rounds)
    if (!S.shot && !S.M.Over()) {
        S.acc += dt;
        while (S.acc >= sf::STEP) {
            S.acc -= sf::STEP;
            Gather(S.M.w.sticks[0].in, S.M.w.sticks[0]);
            for (int i = 1; i < S.players; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, S.botRng[i], S.skill);
            if (S.M.phase == sf::Match::P_COUNT || S.M.phase == sf::Match::P_FIGHT) sf::ReplayRecord(S.rec, S.M);
            S.M.Step();
            if (S.M.phase == sf::Match::P_WIN || S.M.phase == sf::Match::P_OVER) RecEnd();
            if (S.M.round != S.lastRound) { S.lastRound = S.M.round; S.blots.clear(); S.evSeen = S.M.w.eventBase; RecBegin(); }
        }
    }
    ReadEvents();
    if (S.M.Over() && S.M.mode == sf::MD_GAUNTLET && !S.boardDone && !S.shot && !EditorPlaying()) { S.boardDone = true; GauntletRecord(S.M.gStage, S.M.gTime, S.M.gDeaths, S.M.gWorld); }
    StepCamera(dt);
    DrawWorld(dt);
    DrawHud();
    if (S.M.Over() && EditorPlaying()) {
        if (Button({SCREEN_W / 2.0f - 100, SCREEN_H / 2.0f + 160, 200, 40}, "Back to the editor", true, 16)) EditorBackFromPlay();
    } else if (S.M.Over()) {
        PayOut(); SaveBest();
        if (!S.savedMsg.empty()) DrawTextCentered(S.savedMsg, SCREEN_W / 2.0f, SCREEN_H / 2.0f + 214, 14, NameInk());
        DrawTextCenteredBold(TextFormat("+%d tokens for the locker (%d in all)", S.paid, sf::MyLocker().tokens), SCREEN_W / 2.0f, SCREEN_H / 2.0f + 128, 16, Color{170, 110, 30, 255});
        if (Button({SCREEN_W / 2.0f - 230, SCREEN_H / 2.0f + 160, 200, 40}, "Again", true, 16)) { int b = S.players - 1, sk = S.skill, tw = S.toWin, wd = S.M.world; StartScuffle(g, b, sk, tw, wd); }
        if (Button({SCREEN_W / 2.0f + 30, SCREEN_H / 2.0f + 160, 200, 40}, "Back to the arcade", true, 16)) { S.active = false; g.scene = Scene::Arcade; }
    }
}
void LeaveScuffle(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.active = false; g.scene = Scene::Arcade;
}
// --shots: 0 mid-fight (four sticks, armed), 1 a haymaker or an explosion, 2 the match won, 3 the flood
void DebugScuffleShot(Game& g, int which) {
    StartScuffle(g, 3, 2, 5); S.shot = true;
    int frames = which == 1 ? 120 * 9 : which == 3 ? 120 * 50 : 120 * 12;
    uint32_t r[8] = {11, 22, 33, 44, 55, 66, 77, 88};
    for (int f = 0; f < frames && !S.M.Over(); f++) {
        for (int i = 0; i < S.players; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, r[i], 2);
        S.M.Step();
        if (S.M.phase != sf::Match::P_FIGHT && f > 200 && which != 3) break;
        if (which == 3 && S.M.w.wallY > 3 && S.M.w.Living() >= 2) break;
        if (which == 3 && S.M.phase == sf::Match::P_WIN) { S.M.NewRound(); S.M.w.t = 40; }
    }
    S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; ReadEvents();
    for (int i = 0; i < 90; i++) StepCamera(1 / 60.0f);
    if (which == 2) { S.M.wins = {5, 3, 2, 1}; S.M.champion = 0; S.M.phase = sf::Match::P_OVER; }
    if (which == 5 || which == 6) {   // the arsenal's look (5: weapons, things and statuses; 6: Blackout with an event's banner and the hot potato)
        S = ScuffleScene{}; S.active = true; S.shot = true; S.players = 8; S.toWin = 5;
        uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
        sf::Stage st = sf::StoneStage(); for (int x = 20; x < 26; x++) st.Set(x, 6, sf::T_WOOD);
        S.M.custom = {st}; S.M.Start(8, 5, 777); S.M.phase = sf::Match::P_FIGHT; S.M.w.nextCrate = 1e9f;
        sf::World& w = S.M.w;
        const char* hold[8] = {"flamethrower", "tesla", "blackhole", "trident", "beehive", "laser", "sledge", "portal"};
        for (int i = 0; i < 8; i++) { sf::Stick& k = w.sticks[i]; w.SpawnStick(k, {1.5f + i * 2.4f, 1.2f}, i % 2 ? -1 : 1); int it = w.SpawnWeapon(sf::WeaponIndex(hold[i]), k.pt[sf::J_HAND_R].p, {0, 0}); w.Pickup(k, it); k.trinket = 1 + i; }
        for (int f = 0; f < 60; f++) w.Step();
        w.sticks[0].burnT = 2; w.sticks[1].frozenT = 2; w.sticks[2].bubbleT = 2; w.sticks[3].netT = 2; w.sticks[4].gear = sf::GR_JETPACK; w.sticks[4].in.gear = true; w.sticks[5].gear = sf::GR_SHIELD; w.sticks[6].trapT = 2; w.sticks[7].gear = sf::GR_PARACHUTE;
        w.AddThing(sf::TH_SWARM, {6, 5}, {}, 5, -1); w.AddThing(sf::TH_HOLE, {14, 6.5f}, {}, 4, -1); w.Snakes({9, 1.3f}, 2, -1);
        int t1 = w.AddThing(sf::TH_TURRET, {11.5f, 1.2f}, {}, 9, 0); w.things[t1].a = 0.4f; w.AddThing(sf::TH_MINE, {3, 1.2f}, {}, 30, -1); w.AddThing(sf::TH_PEEL, {17, 1.2f}, {}, 30, -1); w.AddThing(sf::TH_SPRING, {19, 1.2f}, {}, 30, -1); w.AddThing(sf::TH_DECOY, {1, 1.2f}, {}, 30, -1);
        int p1 = w.AddThing(sf::TH_PORTAL, {0.3f, 3}, {}, 99, 7); w.things[p1].q = {1, 0}; int p2 = w.AddThing(sf::TH_PORTAL, {16, 4}, {}, 99, 7); w.things[p2].q = {0, -1}; w.things[p2].a = 1;
        int b1 = w.AddThing(sf::TH_BEAM, w.sticks[1].pt[sf::J_HAND_R].p, {}, 1, 1); w.things[b1].q = w.sticks[2].pt[sf::J_NECK].p; w.things[b1].a = 1;
        int b2 = w.AddThing(sf::TH_BEAM, w.sticks[5].pt[sf::J_HAND_R].p, {}, 1, 5); w.things[b2].q = {18, 6}; w.things[b2].a = 3;
        w.fireT.assign(w.stage.t.size(), 0); for (int x = 20; x < 24; x++) w.fireT[6 * w.stage.w + x] = 0.5f;
        if (which == 6) { w.mut = 1u << sf::MU_BLACKOUT; int pt = w.AddThing(sf::TH_POTATO, {}, {}, 2.5f, -1); w.things[pt].on = 3; w.things[pt].p = w.sticks[3].pt[sf::J_HAND_R].p; gEventBanner = 2; gEventKind = sf::RE_GRAVITY_FLIP; }
        S.cam = {w.stage.Width() / 2, w.stage.Height() / 2}; S.zoom = 30; for (int i = 0; i < 120; i++) StepCamera(1 / 60.0f);
        return;
    }
    if (which == 90) {   // the replay viewer: a bot round recorded, watched from its end, caught in the slow motion on the kill
        S.shot = false; uint32_t r[8] = {11, 22, 33, 44, 55, 66, 77, 88};
        for (int f = 0; f < 120 * 60 && S.M.phase != sf::Match::P_WIN; f++) { for (int i = 0; i < S.players; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, r[i], 2); sf::ReplayRecord(S.rec, S.M); S.M.Step(); }
        RecEnd(); ViewStart(S.rec, true);
        size_t n = S.view.steps.size(); while (S.vIdx + 150 < n) { sf::ReplayStep(S.view, S.vIdx, S.M); S.vIdx++; }
        S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; S.shot = true;
        for (int i = 0; i < 90; i++) StepCamera(1 / 60.0f);
        return;
    }
    if (which >= 80 && which < 85) {   // the wardrobe on parade (80 + page): eight sticks, each in the next skin and hat; page 4 knocks two hats off
        int page = which - 80;
        S = ScuffleScene{}; S.active = true; S.shot = true; S.players = 8; S.toWin = 5; gEventBanner = 0;
        sf::Stage st = sf::StoneStage(); S.M.custom = {st}; S.M.Start(8, 5, 99); S.M.phase = sf::Match::P_FIGHT; S.M.w.nextCrate = 1e9f; S.M.w.wallOn = false;
        sf::World& w = S.M.w;
        for (int i = 0; i < 8; i++) { sf::Stick& k = w.sticks[i]; w.SpawnStick(k, {1.6f + i * 2.3f, 1.2f}, 1); k.skin = (page * 8 + i) % (int)sf::Skins().size(); k.hat = (page * 8 + i) % (int)sf::Hats().size(); }
        for (int f = 0; f < 90; f++) w.Step();
        if (page == 4) { w.KnockHat(w.sticks[2], {3, 1}); w.KnockHat(w.sticks[5], {-3, 1}); for (int f = 0; f < 20; f++) w.Step(); }
        S.names.clear(); for (int i = 0; i < 8; i++) { const sf::Stick& k = w.sticks[i]; S.names.push_back(TextFormat("%s / %s", k.skin >= 0 ? sf::Skins()[k.skin].name.c_str() : "-", k.hat >= 0 ? sf::Hats()[k.hat].name.c_str() : "-")); }
        S.cam = {w.stage.Width() / 2, 2.5f}; S.zoom = 64;
        return;
    }
    if (which >= 60 && which < 60 + sf::BK_COUNT * 2) {   // a boss mid-fight (60 + boss: four bots, caught during an attack; 66 + boss: in its last phase)
        int kind = (which - 60) % sf::BK_COUNT; bool late = which >= 60 + sf::BK_COUNT;
        S = ScuffleScene{}; S.active = true; S.shot = true; S.players = 4; S.toWin = 1; gEventBanner = 0; gEventKind = -1;
        uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
        static const int WORLD_OF[sf::BK_COUNT] = {sf::WD_CAVE, sf::WD_NAUTILUS, sf::WD_ATLANTIS, sf::WD_REEF, sf::WD_VOID, sf::WD_SALON};
        S.M.mode = sf::MD_BOSS; S.M.world = WORLD_OF[kind]; S.M.Start(4, 1, 4040 + kind);
        uint32_t r[8] = {11, 22, 33, 44, 55, 66, 77, 88};
        float from = getenv("DEPTH_BOSST") ? (float)atof(getenv("DEPTH_BOSST")) : 9;
        for (int f = 0; f < 120 * 120 && !S.M.Over(); f++) {
            for (int i = 0; i < 4; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, r[i], 2);
            if (late && S.M.w.boss.phase < 2 && S.M.phase == sf::Match::P_FIGHT) S.M.w.boss.hp = std::min(S.M.w.boss.hp, S.M.w.boss.maxHp * 0.3f);
            S.M.Step();
            const sf::Boss& B = S.M.w.boss;
            if (S.M.phase == sf::Match::P_FIGHT && S.M.w.t > from && !B.danger.empty() && B.roarT <= 0 && (B.kind != sf::BK_KRAKEN || B.s[0] > 0.6f || B.act)) break;
        }
        S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; ReadEvents();
        S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 30;
        for (int i = 0; i < 120; i++) StepCamera(1 / 60.0f);
        return;
    }
    if (which >= 40 && which < 40 + sf::MD_COUNT) {   // a mode's look (40 + mode), bots playing it for a while (the Duel: on the pick)
        int mode = which - 40, n = mode == sf::MD_DUEL ? 2 : mode == sf::MD_GAUNTLET ? 2 : mode == sf::MD_HUNT ? 5 : 4;
        S = ScuffleScene{}; S.active = true; S.shot = true; S.players = n; S.toWin = 5;
        uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
        S.M.mode = mode; if (mode == sf::MD_TEAMS) S.M.friendlyFire = false; S.M.Start(n, 5, 2024);
        uint32_t r[8] = {11, 22, 33, 44, 55, 66, 77, 88};
        float until = mode == sf::MD_DUEL ? 0 : mode == sf::MD_GAUNTLET ? 4 : 14;
        for (int f = 0; f < 120 * 60 && !(mode == sf::MD_DUEL && S.M.phase == sf::Match::P_COUNT && S.M.phaseT > 0.5f) && !(S.M.phase == sf::Match::P_FIGHT && S.M.w.t >= until && mode != sf::MD_DUEL); f++) {
            for (int i = 0; i < n; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, r[i], 2);
            S.M.Step();
        }
        if (mode == sf::MD_DUEL) S.M.duelPick[0] = 2;
        S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; ReadEvents();
        S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 30;
        for (int i = 0; i < 120; i++) StepCamera(1 / 60.0f);
        return;
    }
    if (which >= 10) {   // a world's look (10 + world: a signature stage mid-fight; 20 + world: its wall closing in; 30 + world: a finale's set piece)
        int world = (which - 10) % 10, kind = (which - 10) / 10;
        std::vector<sf::Stage> pack = sf::StagePlaylist(world), fin = sf::FinalePlaylist(world);
        sf::Stage st = kind == 2 && !fin.empty() ? fin[0] : pack.empty() ? sf::StoneStage() : pack[getenv("DEPTH_STAGEIDX") ? atoi(getenv("DEPTH_STAGEIDX")) % pack.size() : 0];
        S = ScuffleScene{}; S.active = true; S.shot = true; S.players = 6; S.toWin = 5;
        uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
        S.M.custom = {st}; S.M.Start(6, 5, 4242); S.M.phase = sf::Match::P_FIGHT; S.M.w.finale = kind == 2;
        uint32_t r[8] = {11, 22, 33, 44, 55, 66, 77, 88};
        float until = kind == 0 ? 7.5f : kind == 1 ? 57 : 15.5f;
        while (S.M.w.t < until) { for (int i = 0; i < 6; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, r[i], 1); S.M.w.Step(); }
        S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; ReadEvents(); S.wallMsgT = kind == 1 ? 2 : 0;
        S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 30;
        for (int i = 0; i < 120; i++) StepCamera(1 / 60.0f);
        return;
    }
    if (which == 4) {   // a guest's screen: eight sticks run by a host for 12 s, read from the snapshot for stick 4
        auto h = sf::MakeScuffleHost(); h->Configure("5:0:2:test"); h->Start(8, 31337);
        for (int f = 0; f < 120 * 12; f++) h->Tick(sf::STEP, 0xFF);
        Writer s; h->Snapshot(3, s); Reader r(s.b);
        sf::Predictor P; P.Apply(r);
        S.M = P.m; S.names = P.names; S.names[3] = "Stick D (me)"; S.me = P.me; S.players = 8; S.toWin = 5;
        S.evSeen = S.M.w.eventBase > 64 ? S.M.w.eventBase : 0; S.blots.clear(); ReadEvents();
        for (int i = 0; i < 90; i++) StepCamera(1 / 60.0f);
    }
}

// ---------------------------------------------------------------- the locker (stage 9; the arcade's Scuffle panel opens it): the shop, the crate, what you wear
namespace {
std::string gLkMsg; float gLkMsgT = 0, gLkOpenT = 0; std::string gLkShow; int gLkShowTier = -1; bool gLkBanana = false; int gLkTab = 0;
void LockerStick(Vector2 feetScreen, float zoom, int skin, int hat) {
    // a stick on the locker's floor, breathing: a little world of its own, drawn through the scene's camera
    static sf::World pv; static bool made = false;
    if (!made) { pv.Init(sf::StoneStage(), 1, 3); made = true; }
    pv.sticks[0].skin = skin; pv.sticks[0].hat = hat; pv.sticks[0].in = sf::Input{}; pv.sticks[0].in.aim = {1, 0.15f};
    pv.Step(); pv.nextCrate = 1e9f; pv.wallOn = false;
    Vector2 cam = S.cam; float z = S.zoom; std::vector<std::string> names = S.names;
    const sf::Stick& k = pv.sticks[0]; S.zoom = zoom; S.names = {" "};
    S.cam = {k.pos.x - (feetScreen.x - SCREEN_W / 2.0f) / zoom, k.pos.y + (feetScreen.y - SCREEN_H / 2.0f) / zoom};
    DrawStick(k);
    S.cam = cam; S.zoom = z; S.names = names;
}
}
bool ScuffleLockerPage(Game& g) {
    sf::Locker& L = sf::MyLocker();
    float dt = GetFrameTime(); S.t += dt;
    Color bg{226, 214, 186, 255}, ink{40, 30, 24, 255}, dim{110, 96, 80, 255}, gold{170, 110, 30, 255};
    static const Color TC[5] = {{120, 110, 100, 255}, {90, 130, 100, 255}, {60, 110, 190, 255}, {150, 70, 190, 255}, {200, 140, 30, 255}};
    ClearBackground(bg);
    for (const auto& g : S.grain) DrawCircleV(g, 1.2f, Color{196, 182, 150, 255});
    DrawTextCenteredBold("The Scuffle locker", SCREEN_W / 2.0f, 16, 30, ink);
    DrawTextCentered(TextFormat("%d tokens   %d crate%s   %d banana%s   %d matches, %d won", L.tokens, L.crates, L.crates == 1 ? "" : "s", L.bananas, L.bananas == 1 ? "" : "s", L.matches, L.wins), SCREEN_W / 2.0f, 54, 16, dim);
    // the preview: you, in what you wear (or what you point at)
    std::string tryOn;
    // the tabs: skins, hats
    for (int t = 0; t < 2; t++) { Rectangle r{SCREEN_W / 2.0f - 150 + t * 160.0f, 80, 140, 30}; DrawRectangleRounded(r, 0.3f, 6, t == gLkTab ? Color{90, 60, 36, 255} : Color{200, 186, 156, 255}); DrawTextCenteredBold(t ? "Hats" : "Skins", r.x + 70, r.y + 6, 16, t == gLkTab ? WHITE : ink); if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gLkTab = t; }
    const auto& list = gLkTab ? sf::Hats() : sf::Skins();
    const std::string& worn = gLkTab ? L.hat : L.skin;
    // the shop (left): ten to buy
    DrawTextCenteredBold("The token shop", 250, 124, 18, gold);
    int i = 0;
    for (const auto& c : list) {
        if (c.tier != 0) continue;
        Rectangle r{40, 150 + i * 38.0f, 420, 34};
        bool own = L.Owns(c.id), on = worn == c.id, hov = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.2f, 6, on ? Color{150, 120, 70, 255} : hov ? Color{214, 198, 166, 255} : Color{206, 190, 158, 255});
        Txt(c.name, r.x + 12, r.y + 8, 15, ink);
        if (!own) { if (Button({r.x + r.width - 120, r.y + 4, 114, 26}, TextFormat("buy %d", c.cost), L.tokens >= c.cost, 13)) { std::string why; if (!sf::BuyCosmetic(c.id, &why)) { gLkMsg = why; gLkMsgT = 3; } else PlayCue("ui.click"); } }
        else if (Button({r.x + r.width - 120, r.y + 4, 114, 26}, on ? "take off" : "wear", true, 13)) sf::WearCosmetic(on ? (gLkTab ? "hat:" : "skin:") : c.id);
        if (hov) { tryOn = c.id; DrawTextCentered(c.name + ": " + c.look, SCREEN_W / 2.0f, SCREEN_H - 28.0f, 15, ink); }
        i++;
    }
    // the crate's (right): all of them, the ones you own lit (click to wear)
    DrawTextCenteredBold("From the crate", SCREEN_W - 260, 124, 18, gold);
    int k = 0;
    for (const auto& c : list) {
        if (c.tier == 0) continue;
        float x = SCREEN_W - 500 + (k % 4) * 120.0f, y = 150 + (k / 4) * 34.0f;
        Rectangle r{x, y, 114, 30};
        bool own = L.Owns(c.id), on = worn == c.id, hov = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.2f, 6, on ? Color{150, 120, 70, 255} : own ? Color{214, 198, 166, 255} : Color{190, 178, 150, 255});
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, own ? 2.0f : 1.0f, ColorAlpha(TC[c.tier], own ? 1.0f : 0.4f));
        std::string nm = c.name.rfind("the ", 0) == 0 ? c.name.substr(4) : c.name;
        DrawTextCentered(nm.size() > 15 ? nm.substr(0, 14) + "." : nm, x + 57, y + 8, 12, own ? ink : Color{150, 136, 116, 255});
        if (hov) { if (own) tryOn = c.id; DrawTextCentered(c.name + " (" + sf::CosTierName(c.tier) + (own ? "" : ", not found yet") + "): " + c.look, SCREEN_W / 2.0f, SCREEN_H - 28.0f, 15, ink); if (own && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) sf::WearCosmetic(on ? (gLkTab ? "hat:" : "skin:") : c.id); }
        k++;
    }
    // the preview in the middle
    std::string skin = L.skin, hat = L.hat; if (!tryOn.empty()) { if (gLkTab) hat = tryOn; else skin = tryOn; }
    DrawEllipse(SCREEN_W / 2, 470, 70, 10, Color{190, 172, 140, 255});
    LockerStick({SCREEN_W / 2.0f, 470}, 130, sf::SkinIndex(skin), sf::HatIndex(hat));
    DrawTextCentered(TextFormat("%s, %s", skin.empty() ? "no skin" : sf::Skins()[sf::SkinIndex(skin)].name.c_str(), hat.empty() ? "no hat" : sf::Hats()[sf::HatIndex(hat)].name.c_str()), SCREEN_W / 2.0f, 488, 15, dim);
    // the crate: one token; it comes down on its parachute and bursts
    float cy = SCREEN_H - 150.0f;
    if (Button({SCREEN_W - 500.0f, cy, 230, 36}, TextFormat("Buy a crate (%d token%s)", sf::CratePrice(), sf::CratePrice() == 1 ? "" : "s"), L.tokens >= sf::CratePrice(), 14)) { std::string why; if (!sf::BuyCrate(&why)) { gLkMsg = why; gLkMsgT = 3; } else PlayCue("ui.click"); }
    if (Button({SCREEN_W - 260.0f, cy, 220, 36}, TextFormat("Open a crate (%d)", L.crates), L.crates > 0 && gLkOpenT <= 0, 14)) {
        sf::CrateRoll r = sf::OpenCrate((uint32_t)(GetTime() * 1000) ^ (uint32_t)L.matches * 977u ^ (uint32_t)L.bananas * 31u);
        if (r.ok) { int si = sf::SkinIndex(r.id), hi = sf::HatIndex(r.id); gLkShow = r.banana ? "A banana." : si >= 0 ? sf::Skins()[si].name : hi >= 0 ? sf::Hats()[hi].name : r.id; gLkShowTier = r.tier; gLkBanana = r.banana; gLkOpenT = 3.2f; PlayCue("ui.click"); }
    }
    if (gLkOpenT > 0) {
        gLkOpenT -= dt;
        float u = 3.2f - gLkOpenT; Vector2 c{SCREEN_W - 270.0f, std::min(cy - 60, 150 + u * 260)};
        if (u < 1.0f) { DrawCircleSector({c.x, c.y - 70}, 52, 180, 360, 20, Color{236, 226, 200, 255}); DrawCircleSectorLines({c.x, c.y - 70}, 52, 180, 360, 20, ink); for (float s : {-1.0f, -0.35f, 0.35f, 1.0f}) DrawLineEx({c.x + s * 52, c.y - 70}, {c.x + s * 26, c.y - 26}, 1.5f, ink); DrawRectangleRec({c.x - 26, c.y - 26, 52, 52}, Color{170, 120, 64, 255}); DrawRectangleLinesEx({c.x - 26, c.y - 26, 52, 52}, 3, ink); }
        else {
            float b = std::min(1.0f, (u - 1.0f) * 3);
            for (int j = 0; j < 12; j++) { float a = j * PI / 6; DrawLineEx({c.x + cosf(a) * 30 * b, c.y + sinf(a) * 30 * b}, {c.x + cosf(a) * 70 * b, c.y + sinf(a) * 70 * b}, 3, gLkBanana ? Color{220, 200, 60, 255} : TC[std::clamp(gLkShowTier, 0, 4)]); }
            DrawTextCenteredBold(gLkShow, c.x, c.y - 12, 22, gLkBanana ? dim : TC[std::clamp(gLkShowTier, 0, 4)]);
            DrawTextCentered(gLkBanana ? "(you had it already: duplicates give a banana)" : sf::CosTierName(gLkShowTier), c.x, c.y + 16, 14, dim);
        }
    }
    DrawTextCentered("Tokens: 3 a match, +1 a round won, +5 for the match. Nothing here changes a stat.", 250, SCREEN_H - 150.0f, 13, dim);
    {   // the replays: each match's best three rounds, newest first (re-simulated from their inputs)
        static std::vector<std::string> files; static float scanT = 0;
        if ((scanT -= dt) <= 0) { scanT = 2; files.clear(); if (DirectoryExists("scuffle_replays")) { FilePathList l = LoadDirectoryFilesEx("scuffle_replays", ".scr", false); for (unsigned i = 0; i < l.count; i++) files.push_back(l.paths[i]); UnloadDirectoryFiles(l); } std::sort(files.rbegin(), files.rend()); }
        DrawTextCenteredBold(TextFormat("Replays (%d)", (int)files.size()), 250, SCREEN_H - 128.0f, 16, gold);
        for (int i = 0; i < (int)files.size() && i < 3; i++) {
            std::string nm = GetFileNameWithoutExt(files[i].c_str());
            if (Button({40 + i * 140.0f, SCREEN_H - 104.0f, 132, 28}, nm.size() > 9 ? ("Watch " + nm.substr(4, 4) + nm.substr(nm.size() - 2)).c_str() : "Watch", true, 12)) { StartScuffleReplay(g, files[i]); return true; }
        }
        if (files.empty()) DrawTextCentered("A match's best three rounds are kept here.", 250, SCREEN_H - 100.0f, 13, dim);
    }
    if (gLkMsgT > 0) { gLkMsgT -= dt; DrawTextCenteredBold(gLkMsg, SCREEN_W / 2.0f, SCREEN_H - 60.0f, 16, Color{170, 50, 30, 255}); }
    return Button({30, 18, 120, 34}, "Back", true, 15);
}
void DebugScuffleLocker(int tab) { gLkTab = tab; S.grain.clear(); uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); } }

#include "scuffle_editor.inl"