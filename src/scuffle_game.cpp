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
#include <vector>

namespace {
struct Blot { Vector2 p, v; float r, life, max; Color c; };
struct ScuffleScene {
    bool active = false, shot = false;
    sf::Match M;
    int players = 4, skill = 2, toWin = 5, arsenal = sf::AR_CLASSIC; int lastRound = 0;
    std::vector<uint32_t> botRng;
    float acc = 0, t = 0, wallMsgT = 0;
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
Vector2 W2S(Vector2 w) { return {(w.x - S.cam.x) * S.zoom + SCREEN_W / 2.0f, SCREEN_H / 2.0f - (w.y - S.cam.y) * S.zoom}; }
Vector2 S2W(Vector2 s) { return {(s.x - SCREEN_W / 2.0f) / S.zoom + S.cam.x, (SCREEN_H / 2.0f - s.y) / S.zoom + S.cam.y}; }


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
    in.gear = IsKeyDown(KEY_E) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);   // (the gear button)
    Vector2 m = S2W(GetMousePosition()), d = Vector2Subtract(m, k.pt[sf::J_NECK].p);
    in.aim = Vector2Length(d) > 0.05f ? Vector2Normalize(d) : Vector2{(float)k.face, 0};
    if (IsKeyDown(KEY_J) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.aim = {(float)k.face, 0};   // (the keyboard's fist: straight ahead)
}

#include "scuffle_worldart.inl"
#include "scuffle_arsenalart.inl"
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
        if (e.kind == sf::EV_EVENT) { gEventBanner = 2.2f; gEventKind = (int)e.a; }
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
// the world, in order: the stage, burning wood, the dead, the living with what they hold and what's happening to them,
// crates, loose weapons and bullets (and the water over all of it), the things the arsenal leaves, ink, and the screen's effects
void DrawWorld(float dt) {
    DrawStage();
    DrawFires();
    for (const auto& k : S.M.w.sticks) if (!k.alive) DrawStick(k);
    for (const auto& k : S.M.w.sticks) if (k.alive) { DrawStick(k); DrawHeld(k); DrawStickStatus(k); }
    DrawThings();
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
        Txt(TextFormat("%d", M.score[i]), x + 26, 33, 11, Color{200, 190, 170, 255});
        if (k.trinket != sf::TK_NONE) Txt(sf::TrinketName(k.trinket), x + 4, 0, 10, DarkWorld(M.w.stage.world) ? Color{220, 190, 120, 255} : Color{120, 80, 30, 255});   // (everyone can see what everyone took)
        for (int wn = 0; wn < S.toWin && wn < 20; wn++) DrawCircleV({x + 72 + (wn % 10) * 7.5f, 37.0f + (wn / 10) * 7}, 2.6f, wn < M.wins[i] ? StickColor(i) : Color{80, 74, 66, 255});
        x += 158;
    }
    DrawTextCentered(TextFormat("Round %d   -   first to %d   -   %s%s", M.round, S.toWin, M.w.stage.name.c_str(), M.w.finale ? "   (match point: the wall at 30 s)" : ""), SCREEN_W / 2.0f, 58, 14, NameInk());
    // your weapon and its ammo
    const sf::Stick& me = M.w.sticks[std::clamp(S.me, 0, (int)M.w.sticks.size() - 1)];
    if (me.weapon >= 0 && me.weapon < (int)M.w.items.size()) { const sf::Item& it = M.w.items[me.weapon]; const sf::WeaponDef& d = sf::Weapons()[it.weapon]; DrawTextCenteredBold(d.kind == "gun" ? TextFormat("%s   %d", d.name.c_str(), it.ammo) : d.name.c_str(), SCREEN_W / 2.0f, SCREEN_H - 50.0f, 18, it.ammo == 0 && d.kind == "gun" ? Color{170, 60, 40, 255} : NameInk()); if (it.ammo == 0 && d.kind == "gun") DrawTextCentered("(empty: click to throw it)", SCREEN_W / 2.0f, SCREEN_H - 30.0f, 12, NameInk()); }
    if (me.gear >= 0) DrawTextCentered(TextFormat("%s%s   (E or right mouse)", sf::GearName(me.gear), me.gear == sf::GR_JETPACK ? TextFormat(": %.0f%%", me.gearFuel / 3 * 100) : ""), SCREEN_W / 2.0f, SCREEN_H - 72.0f, 13, NameInk());
    if (me.carry >= 0 && me.carry < (int)M.w.items.size()) DrawTextCentered(TextFormat("on your back: %s (E swaps)", sf::Weapons()[M.w.items[me.carry].weapon].name.c_str()), SCREEN_W / 2.0f, SCREEN_H - 88.0f, 12, NameInk());
    if (S.wallMsgT > 0) DrawTextCenteredBold(WallLine(M.w.stage.world), SCREEN_W / 2.0f, 90, 26, DarkWorld(M.w.stage.world) ? Color{230, 210, 160, 255} : Color{40, 70, 110, 255});
    if (M.phase == sf::Match::P_COUNT) DrawTextCenteredBold("FIGHT!", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 56, Color{40, 30, 26, (unsigned char)(255 * std::clamp(1.4f - M.phaseT, 0.0f, 1.0f))});
    if (M.phase == sf::Match::P_WIN) DrawTextCenteredBold(M.roundWinner >= 0 ? TextFormat("%s %s the round", StickName(M.roundWinner), M.roundWinner == S.me && !S.net ? "take" : "takes") : "A draw: everyone at once", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 120, 34, M.roundWinner >= 0 ? StickColor(M.roundWinner) : INK);
    if (M.phase == sf::Match::P_OVER) {
        int best = std::max(0, M.champion);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(PAPER, 0.8f));
        DrawTextCenteredBold(TextFormat("%s %s the match", StickName(best), best == S.me && !S.net ? "win" : "wins"), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 90, 44, StickColor(best));
        for (int i = 0; i < S.players; i++) DrawTextCentered(TextFormat("%s: %d rounds, %d points", StickName(i), M.wins[i], M.score[i]), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 30 + i * 22.0f, 16, INK);
    }
    if (M.phase != sf::Match::P_OVER) DrawTextCentered("A/D run   W or Space jump   S duck (or dive in the air; duck on a gun to swap)   mouse aims, click fires or punches (hold against someone: grab)   T taunt", SCREEN_W / 2.0f, SCREEN_H - 14.0f, 11, ColorAlpha(NameInk(), 0.6f));
}
} // namespace

int gScuffleTrinket = -1, gScuffleRules = 0;   // (the arcade's picks: your trinket (-1: the game picks); the rules: 0 none, 1 Random each round, 2+ one mutator)
uint32_t ScuffleRulesMask(int r) { return r >= 2 ? 1u << (r - 2) : 0; }
void StartScuffle(Game& g, int bots, int skill, int toWin, int world) {
    S = ScuffleScene{};
    S.active = true; S.players = std::clamp(bots + 1, 2, sf::MAX_STICKS); S.skill = std::clamp(skill, 0, 2); S.toWin = std::clamp(toWin, 1, 20);
    S.M.world = world; S.M.mutators = ScuffleRulesMask(gScuffleRules); S.M.randomMutator = gScuffleRules == 1;
    S.M.trinkets.assign(S.players, -1); S.M.trinkets[0] = gScuffleTrinket;
    S.M.Start(S.players, S.toWin, (uint32_t)GetRandomValue(1, 1 << 30), S.arsenal);
    S.botRng.resize(S.players);
    for (int i = 0; i < S.players; i++) S.botRng[i] = 1234567u + i * 7919u + (uint32_t)GetRandomValue(0, 1 << 20);
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    S.cam = {S.M.w.stage.Width() / 2, S.M.w.stage.Height() / 2}; S.zoom = 60; S.lastRound = S.M.round;
    g.scene = Scene::Scuffle;
}
// ---------------------------------------------------------------- a networked match (stage 4)
void StartScuffleNet(Game& g, arcade::Session* net, const char* name) {
    S = ScuffleScene{};
    S.active = true; S.net = net; S.netName = name ? name : "Stick"; S.me = std::max(0, net->MyPlayer());
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    S.zoom = 60; S.lastRound = -1;
    g.scene = Scene::Scuffle;
}
std::string ScuffleOpts(int toWin, int arsenal, int skill, int world, uint32_t mutators, bool randomMutator) { return sf::ScuffleHostOpts(toWin, arsenal, skill, world, mutators, randomMutator); }
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
    if (!S.helloSent) { Writer o; sf::OrderHello(o, S.netName, gScuffleTrinket); N.Act(o); S.helloSent = true; }
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
    if (S.M.Over()) {
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
    if (S.active && S.net) { S.t += dt; S.wallMsgT = std::max(0.0f, S.wallMsgT - dt); NetFrame(g, dt); return; }
    if (EditorFrame(g)) return;
    if (EditorPlaying() && IsKeyPressed(KEY_P)) { EditorBackFromPlay(); return; }
    if (!S.active) { StartScuffle(g, 3, 2, 5); }
    if (S.shot) dt = 1 / 60.0f;
    S.t += dt; S.wallMsgT = std::max(0.0f, S.wallMsgT - dt);
    // the inputs (you and the bots), then the fixed steps (the match runs the rounds)
    if (!S.shot && !S.M.Over()) {
        S.acc += dt;
        while (S.acc >= sf::STEP) {
            S.acc -= sf::STEP;
            Gather(S.M.w.sticks[0].in, S.M.w.sticks[0]);
            for (int i = 1; i < S.players; i++) sf::BotInput(S.M.w, i, S.M.w.sticks[i].in, S.botRng[i], S.skill);
            S.M.Step();
            if (S.M.round != S.lastRound) { S.lastRound = S.M.round; S.blots.clear(); S.evSeen = S.M.w.eventBase; }
        }
    }
    ReadEvents();
    StepCamera(dt);
    DrawWorld(dt);
    DrawHud();
    if (S.M.Over() && EditorPlaying()) {
        if (Button({SCREEN_W / 2.0f - 100, SCREEN_H / 2.0f + 160, 200, 40}, "Back to the editor", true, 16)) EditorBackFromPlay();
    } else if (S.M.Over()) {
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

#include "scuffle_editor.inl"