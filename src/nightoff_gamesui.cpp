// A Night Off: the bar games' screens (stage 3). Darts (a reticle that drifts with the meter, released on a timing
// bar), pool (a cue line that wobbles, power on a bar, English on a small dial), mini golf (a swaying putting line and a
// power bar), the slot machines, scratch-offs (scratched with the cursor, messier the drunker you are) and the fortune
// teller's three cards. Everything you do goes out through p.in; the Night plays the opponents.
#include "nightoff_gamesui.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace nog {
namespace {

const Color INK{250, 238, 214, 255}, DIM{210, 190, 160, 255}, BRASS{230, 190, 110, 255}, PANEL{24, 16, 12, 255};
struct UiState {
    int menuKind = -1, menuMachine = 0, oppSel = 0, stakeSel = 0;
    bool charging = false; float chargeT = 0;
    float english = 0;
    float mask[3][16 * 10] = {}; bool masked = false;
    float revealT = 0; std::string readingKey;
    float t = 0;
};
UiState U;

float Tri(float t) { float f = t - floorf(t); return f < 0.5f ? f * 2 : 2 - f * 2; }   // a 0..1..0 wave
bool Btn(Rectangle r, const char* s, bool on = true, int size = 16) { return Button(r, s, on, size); }
void GPanel(Rectangle r) { DrawRectangleRounded(r, 0.04f, 6, Fade(PANEL, 0.94f)); DrawRectangleRoundedLinesEx(r, 0.04f, 6, 2, BRASS); }
bool Clicked() { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
void Caption(const no::GameSeat& g) { if (g.captionT > 0 && !g.caption.empty()) { float a = std::min(1.0f, g.captionT); DrawRectangleRounded({SCREEN_W / 2.0f - 420, 52, 840, 30}, 0.4f, 6, Fade(PANEL, 0.85f * a)); DrawTextCentered(g.caption, SCREEN_W / 2.0f, 58, 16, Fade(INK, a)); } }
// the end of a match: who won, and again or leave
void OverButtons(no::Player& p, Rectangle r) {
    const no::GameSeat& g = p.game;
    const char* res = g.result == 0 ? "You win." : g.result == 1 ? "You lose." : "A draw.";
    DrawTextCenteredBold(res, r.x + r.width / 2, r.y + r.height - 96, 26, g.result == 0 ? Color{255, 220, 120, 255} : Color{240, 160, 140, 255});
    bool can = g.opp != -2 && (g.opp == -1 || p.money >= g.stake);
    if (Btn({r.x + r.width / 2 - 170, r.y + r.height - 56, 160, 38}, g.sandbag && g.result == 0 ? "Double or nothing" : "Again", can)) p.in.gameAct = 4;
    if (Btn({r.x + r.width / 2 + 10, r.y + r.height - 56, 160, 38}, "Leave the table")) p.in.gameAct = 3;
}
void LeaveButton(no::Player& p, Rectangle r, bool live) {
    if (Btn({r.x + r.width - 150, r.y + 10, 136, 30}, live ? "Forfeit (Esc)" : "Leave (Esc)", true, 14) || IsKeyPressed(KEY_ESCAPE)) p.in.gameAct = 3;
}

// ---------------------------------------------------------------- the menu: who to play, for how much
void DrawMenu(no::Night& n, no::Player& p) {
    int kind = U.menuKind;
    Rectangle r{SCREEN_W / 2.0f - 300, 120, 600, 430};
    GPanel(r);
    static const char* TITLE[3] = {"Darts at the Gull", "The pool tables", "Nine holes in the yard"};
    DrawTextCenteredBold(TITLE[std::clamp(kind, 0, 2)], r.x + r.width / 2, r.y + 14, 24, BRASS);
    std::vector<int> who = n.Challengers(p, kind);
    std::vector<int> opts = {-1}; for (int c : who) opts.push_back(c);
    if (kind == no::GK_DARTS && !n.bartenderDarts && p.tab > 0) opts.push_back(-2);
    U.oppSel = std::clamp(U.oppSel, 0, (int)opts.size() - 1);
    Txt("Against", r.x + 24, r.y + 56, 16, DIM);
    for (int k = 0; k < (int)opts.size() && k < 7; k++) {
        Rectangle row{r.x + 24, r.y + 80 + k * 34.0f, r.width - 48, 30};
        bool hov = CheckCollisionPointRec(GetMousePosition(), row), sel = k == U.oppSel;
        DrawRectangleRounded(row, 0.2f, 6, sel ? Color{90, 64, 40, 255} : hov ? Color{60, 44, 30, 255} : Color{40, 30, 22, 255});
        std::string s;
        if (opts[k] == -1) s = "Practice, alone (no stake)";
        else if (opts[k] == -2) s = TextFormat("The bartender, for your tab (%.0f)", p.tab);
        else {   // (what they are only shows if you know their secret, or the absinthe is showing you)
            const no::Patron& c = n.patrons[opts[k]];
            bool known = p.visionsT > 0 || std::find(p.known.begin(), p.known.end(), c.name) != p.known.end();
            s = c.name + "  (" + (known ? std::string(no::TypeName(c.type)) + ", " : std::string()) + n.MoodName(c.mood) + ")";
        }
        Txt(s, row.x + 10, row.y + 6, 16, INK);
        if (hov && Clicked()) U.oppSel = k;
    }
    int opp = opts[U.oppSel];
    const auto& stakes = kind == no::GK_DARTS ? no::GD().dartStakes : kind == no::GK_POOL ? no::GD().poolStakes : no::GD().golfStakes;
    float y = r.y + 80 + 7 * 34.0f + 8;
    if (opp >= 0 && !stakes.empty()) {
        Txt(kind == no::GK_GOLF ? "Stake (a hole)" : "Stake", r.x + 24, y, 16, DIM);
        U.stakeSel = std::clamp(U.stakeSel, 0, (int)stakes.size() - 1);
        for (int k = 0; k < (int)stakes.size(); k++) {
            int st = kind == no::GK_GOLF ? stakes[k] * 9 : stakes[k];
            Rectangle b{r.x + 150 + k * 96.0f, y - 4, 86, 28};
            bool sel = k == U.stakeSel, ok = p.money >= st;
            DrawRectangleRounded(b, 0.3f, 6, sel ? Color{120, 84, 40, 255} : Color{50, 38, 28, 255});
            DrawTextCentered(TextFormat("%d", st), b.x + b.width / 2, b.y + 6, 15, ok ? INK : Fade(INK, 0.35f));
            if (ok && CheckCollisionPointRec(GetMousePosition(), b) && Clicked()) U.stakeSel = k;
        }
    }
    if (kind == no::GK_GOLF && n.Hour() >= no::GD().golfCloses) DrawTextCentered("The yard closed at 2 a.m.", r.x + r.width / 2, y + 34, 15, Color{240, 160, 140, 255});
    if (Btn({r.x + r.width / 2 - 170, r.y + r.height - 50, 160, 36}, "Play")) {
        p.in.startGame = kind; p.in.gameMachine = U.menuMachine; p.in.gameOpp = opp;
        p.in.gameStake = opp >= 0 && !stakes.empty() ? (kind == no::GK_GOLF ? stakes[U.stakeSel] * 9 : stakes[U.stakeSel]) : 0;
        U.menuKind = -1;
    }
    if (Btn({r.x + r.width / 2 + 10, r.y + r.height - 50, 160, 36}, "Not now") || IsKeyPressed(KEY_ESCAPE)) U.menuKind = -1;
}

// ---------------------------------------------------------------- darts
Vector2 BoardPx(Vector2 c, float sc, Vector2 mm) { return {c.x + mm.x * sc, c.y - mm.y * sc}; }
void DrawBoard(Vector2 c, float sc) {
    using namespace no::darts;
    DrawCircleV(c, (R_D_OUT + 28) * sc, Color{20, 14, 10, 255});
    for (int i = 0; i < 20; i++) {
        float a0 = -90 + i * 18 - 9, a1 = a0 + 18;
        bool even = i % 2 == 0;
        Color single = even ? Color{30, 26, 22, 255} : Color{232, 218, 180, 255}, ring = even ? Color{196, 40, 36, 255} : Color{40, 130, 70, 255};
        DrawRing(c, R_OUTER * sc, R_T_IN * sc, a0, a1, 6, single);
        DrawRing(c, R_T_IN * sc, R_T_OUT * sc, a0, a1, 6, ring);
        DrawRing(c, R_T_OUT * sc, R_D_IN * sc, a0, a1, 6, single);
        DrawRing(c, R_D_IN * sc, R_D_OUT * sc, a0, a1, 6, ring);
        float am = (a0 + 9) * DEG2RAD;
        DrawTextCenteredBold(TextFormat("%d", ORDER[i]), c.x + cosf(am) * (R_D_OUT + 15) * sc, c.y + sinf(am) * (R_D_OUT + 15) * sc - 8, 16, INK);
    }
    DrawCircleV(c, R_OUTER * sc, Color{40, 130, 70, 255});
    DrawCircleV(c, R_BULL * sc, Color{196, 40, 36, 255});
    for (int i = 0; i < 20; i++) { float a = (-90 + i * 18 - 9) * DEG2RAD; DrawLineEx({c.x + cosf(a) * R_OUTER * sc, c.y + sinf(a) * R_OUTER * sc}, {c.x + cosf(a) * R_D_OUT * sc, c.y + sinf(a) * R_D_OUT * sc}, 1, Color{160, 160, 160, 120}); }
}
void Darts(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; no::darts::Match& m = g.darts;
    Rectangle r{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f}; GPanel(r);
    Vector2 c{r.x + r.width * 0.38f, r.y + r.height * 0.52f}; float sc = 250.0f / 170.0f;
    DrawBoard(c, sc);
    for (size_t i = 0; i < m.marks.size(); i++) { Vector2 q = BoardPx(c, sc, m.marks[i]); DrawCircleV(q, 5, Color{240, 230, 210, 255}); DrawLineEx(q, {q.x + 10, q.y + 14}, 3, Color{60, 50, 40, 255}); DrawLineEx({q.x + 8, q.y + 12}, {q.x + 14, q.y + 20}, 4, m.turn == 0 ? Color{220, 120, 60, 255} : Color{90, 140, 220, 255}); }
    // the scores
    float x = r.x + r.width * 0.72f, y = r.y + 70;
    std::string them = g.opp == -1 ? "" : g.oppName;
    TxtBold(m.clock ? "Around the Clock" : "501", x, y, 22, BRASS); y += 40;
    TxtBold(TextFormat("You   %d", m.clock ? m.next[0] : m.left[0]), x, y, 26, m.turn == 0 ? INK : DIM); y += 36;
    if (!them.empty()) { TxtBold(TextFormat("%s   %d", them.c_str(), m.clock ? m.next[1] : m.left[1]), x, y, 22, m.turn == 1 ? INK : DIM); y += 36; }
    if (g.stake > 0) { Txt(TextFormat("for %d", g.stake), x, y, 16, DIM); y += 26; }
    if (m.bust) { TxtBold("Bust!", x, y, 20, Color{240, 140, 120, 255}); }
    else if (!m.marks.empty()) Txt(TextFormat("this turn: %d", m.turnPts), x, y, 16, DIM);
    y += 34;
    Txt(m.turn == 0 ? TextFormat("Your throw: dart %d of 3", m.dart + 1) : (them + " is throwing").c_str(), x, y, 16, INK);
    LeaveButton(p, r, !g.over && g.opp != -1);
    if (g.over) { OverButtons(p, r); return; }
    // your throw: the reticle drifts with the meter; hold to start the timing bar, release on the middle
    if (m.turn == 0 && g.botT <= 0) {
        float A = no::GD().dartSigma * n.PlayerAim(p) * 1.05f;
        Vector2 ms = GetMousePosition();
        Vector2 aim{(ms.x - c.x) / sc, (c.y - ms.y) / sc};
        Vector2 drift{A * (sinf(U.t * 1.7f) + 0.5f * sinf(U.t * 3.1f + 1)), A * (cosf(U.t * 1.3f + 0.4f) + 0.5f * sinf(U.t * 2.6f))};
        Vector2 at = Vector2Add(aim, drift);
        Vector2 q = BoardPx(c, sc, at);
        DrawCircleLines((int)q.x, (int)q.y, 12, Color{255, 255, 255, 220}); DrawLineEx({q.x - 18, q.y}, {q.x + 18, q.y}, 1.5f, WHITE); DrawLineEx({q.x, q.y - 18}, {q.x, q.y + 18}, 1.5f, WHITE);
        bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);
        bool onBoard = CheckCollisionPointCircle(ms, c, (no::darts::R_D_OUT + 40) * sc);
        if (down && !U.charging && onBoard) { U.charging = true; U.chargeT = 0; }
        if (U.charging) {
            U.chargeT += GetFrameTime();
            float k = Tri(U.chargeT * 0.9f);
            Rectangle bar{c.x - 160, r.y + r.height - 50, 320, 18};
            DrawRectangleRounded(bar, 0.5f, 6, Color{40, 30, 22, 255});
            DrawRectangle((int)(bar.x + bar.width / 2 - 12), (int)bar.y, 24, (int)bar.height, Color{90, 160, 90, 255});
            DrawRectangle((int)(bar.x + k * bar.width - 2), (int)bar.y - 4, 4, (int)bar.height + 8, WHITE);
            if (!down) {
                U.charging = false;
                float err = (k - 0.5f) * 2;   // -1 .. 1: early throws low, late high
                p.in.gameAim = {at.x, at.y + err * A * 1.6f};
                p.in.gameAct = 1;
            }
        } else DrawTextCentered("Hold to throw, release on the green", c.x, r.y + r.height - 48, 15, DIM);
    } else U.charging = false;
}

// ---------------------------------------------------------------- pool
Color BallColor(int i) {
    static const Color C[8] = {{236, 236, 226, 255}, {232, 190, 40, 255}, {40, 80, 190, 255}, {200, 40, 36, 255}, {110, 50, 150, 255}, {230, 120, 30, 255}, {30, 120, 60, 255}, {120, 30, 30, 255}};
    return i == 8 ? Color{20, 20, 20, 255} : C[i > 8 ? i - 8 : i];
}
void Pool(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; no::pool::Match& m = g.pool;
    using no::pool::W; using no::pool::H; using no::pool::BR;
    Rectangle r{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f}; GPanel(r);
    float sc = 900 / W; Vector2 o{SCREEN_W / 2.0f - W * sc / 2, r.y + 110};
    auto P = [&](Vector2 q) { return Vector2{o.x + q.x * sc, o.y + q.y * sc}; };
    DrawRectangleRounded({o.x - 34, o.y - 34, W * sc + 68, H * sc + 68}, 0.08f, 6, Color{92, 52, 28, 255});
    DrawRectangle((int)o.x, (int)o.y, (int)(W * sc), (int)(H * sc), Color{30, 104, 64, 255});
    DrawCircleV(P({W * 0.25f, H / 2}), 3, Fade(WHITE, 0.3f));
    for (int k = 0; k < 6; k++) DrawCircleV(P(no::pool::POCKETS[k]), 0.062f * sc, Color{10, 8, 6, 255});
    // the balls (the replay, while a shot runs)
    Vector2 pos[16]; bool in[16];
    for (int i = 0; i < 16; i++) { pos[i] = m.t.b[i].p; in[i] = m.t.b[i].in; }
    if (g.replayT > 0 && !m.t.frames.empty()) {
        int f = std::clamp((int)(g.replayLen - g.replayT * 60), 0, (int)m.t.frames.size() - 1);
        for (int i = 0; i < 16; i++) { pos[i] = m.t.frames[f][i]; in[i] = pos[i].x < -5; }
    }
    for (int i = 0; i < 16; i++) {
        if (in[i]) continue;
        Vector2 q = P(pos[i]); float rr = BR * sc;
        DrawCircleV({q.x + 2, q.y + 3}, rr, Fade(BLACK, 0.35f));
        if (i >= 9) { DrawCircleV(q, rr, Color{236, 236, 226, 255}); DrawRectangle((int)(q.x - rr), (int)(q.y - rr * 0.55f), (int)(rr * 2), (int)(rr * 1.1f), BallColor(i)); DrawCircleLines((int)q.x, (int)q.y, rr, Color{236, 236, 226, 255}); }
        else DrawCircleV(q, rr, BallColor(i));
        if (i > 0) { DrawCircleV(q, rr * 0.45f, Color{240, 240, 232, 255}); DrawTextCentered(TextFormat("%d", i), q.x, q.y - 5, 10, BLACK); }
        DrawCircleV({q.x - rr * 0.35f, q.y - rr * 0.35f}, rr * 0.22f, Fade(WHITE, 0.5f));
    }
    // who's on what
    auto grp = [&](int w) { return m.group[w] == 0 ? std::string("open table") : std::string(m.group[w] == 1 ? "solids" : "stripes") + TextFormat(" (%d left)", m.Left(w)); };
    Txt(TextFormat("You: %s", grp(0).c_str()), r.x + 30, r.y + 20, 18, m.turn == 0 ? INK : DIM);
    if (g.opp != -1) Txt(TextFormat("%s: %s", g.oppName.c_str(), grp(1).c_str()), r.x + 30, r.y + 46, 18, m.turn == 1 ? INK : DIM);
    if (g.stake > 0) Txt(TextFormat("for %d", g.stake), r.x + 30, r.y + 72, 15, DIM);
    if (!m.last.empty()) DrawTextCentered((m.turn == 0 && g.opp != -1 ? g.oppName + " " : std::string("You ")) + m.last + ".", SCREEN_W / 2.0f, o.y + H * sc + 44, 16, DIM);
    LeaveButton(p, r, !g.over && g.opp != -1);
    if (g.over && g.replayT <= 0) { OverButtons(p, r); return; }
    if (m.turn != 0 || g.replayT > 0) { U.charging = false; if (m.turn == 1 && g.replayT <= 0) DrawTextCentered(g.oppName + " is lining up a shot.", SCREEN_W / 2.0f, o.y + H * sc + 70, 16, INK); return; }
    Vector2 ms = GetMousePosition(); Vector2 tm{(ms.x - o.x) / sc, (ms.y - o.y) / sc};
    if (m.ballInHand) {
        Vector2 q = {std::clamp(tm.x, BR, W - BR), std::clamp(tm.y, BR, H - BR)};
        DrawCircleV(P(q), BR * sc, Fade(WHITE, 0.6f));
        DrawTextCentered("Ball in hand: click to place the cue ball", SCREEN_W / 2.0f, o.y + H * sc + 70, 16, INK);
        if (Clicked()) { p.in.gameAim = q; p.in.gameAct = 2; }
        return;
    }
    // aim: the cue line wobbles with the meter; hold for power; W/S (or the wheel) sets the English
    Vector2 cue = m.t.b[0].p;
    float Wb = no::GD().poolSigma * n.PlayerAim(p) * 2.6f;
    float ang = atan2f(tm.y - cue.y, tm.x - cue.x) + Wb * (sinf(U.t * 2.3f) + 0.4f * sinf(U.t * 5.1f + 0.7f));
    Vector2 dir{cosf(ang), sinf(ang)};
    // the line to the first ball it meets, and the ghost ball there
    float best = 3; int hitB = -1;
    for (int i = 1; i < 16; i++) {
        if (m.t.b[i].in) continue;
        Vector2 d = Vector2Subtract(m.t.b[i].p, cue); float along = Vector2DotProduct(d, dir); if (along <= 0) continue;
        float perp2 = Vector2LengthSqr(d) - along * along; float R2 = 4 * BR * BR; if (perp2 > R2) continue;
        float tHit = along - sqrtf(R2 - perp2); if (tHit < best) { best = tHit; hitB = i; }
    }
    Vector2 end = Vector2Add(cue, Vector2Scale(dir, std::min(best, 2.6f)));
    DrawLineEx(P(cue), P(end), 1.5f, Fade(WHITE, 0.55f));
    if (hitB >= 0) { DrawCircleLines((int)P(end).x, (int)P(end).y, BR * sc, Fade(WHITE, 0.7f)); Vector2 nrm = Vector2Normalize(Vector2Subtract(m.t.b[hitB].p, end)); DrawLineEx(P(m.t.b[hitB].p), P(Vector2Add(m.t.b[hitB].p, Vector2Scale(nrm, 0.25f))), 1.5f, Fade(YELLOW, 0.5f)); }
    float wheel = GetMouseWheelMove();
    if (IsKeyDown(KEY_W)) U.english += GetFrameTime(); if (IsKeyDown(KEY_S)) U.english -= GetFrameTime();
    U.english = std::clamp(U.english + wheel * 0.15f, -1.0f, 1.0f);
    // the English dial
    Vector2 dc{r.x + r.width - 80, r.y + r.height - 90}; DrawCircleV(dc, 34, Color{236, 236, 226, 255}); DrawCircleLines((int)dc.x, (int)dc.y, 34, BRASS);
    DrawCircleV({dc.x, dc.y - U.english * 26}, 6, Color{200, 40, 36, 255});
    DrawTextCentered(U.english > 0.15f ? "follow" : U.english < -0.15f ? "draw" : "centre", dc.x, dc.y + 42, 13, DIM);
    bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);
    bool overTable = CheckCollisionPointRec(ms, {o.x - 40, o.y - 40, W * sc + 80, H * sc + 80});
    if (down && !U.charging && overTable) { U.charging = true; U.chargeT = 0; }
    float k = U.charging ? Tri(U.chargeT * 0.6f) : 0;
    // the cue, drawn back by the power
    Vector2 butt = Vector2Subtract(P(cue), Vector2Scale(dir, (30 + k * 60) + 380)), tip = Vector2Subtract(P(cue), Vector2Scale(dir, 14 + k * 60));
    DrawLineEx(butt, tip, 6, Color{150, 100, 56, 255}); DrawLineEx(Vector2Subtract(tip, Vector2Scale(dir, 4)), tip, 6, Color{230, 230, 220, 255});
    Rectangle bar{SCREEN_W / 2.0f - 160, o.y + H * sc + 70, 320, 16};
    DrawRectangleRounded(bar, 0.5f, 6, Color{40, 30, 22, 255}); DrawRectangleRounded({bar.x + 2, bar.y + 2, (bar.width - 4) * k, bar.height - 4}, 0.5f, 6, Color{230, 150, 70, 255});
    if (U.charging) {
        U.chargeT += GetFrameTime();
        if (!down) { U.charging = false; p.in.gameAim = {ang, 0}; p.in.gamePower = 0.4f + 6.0f * k; p.in.gameEnglish = U.english; p.in.gameAct = 1; }
    } else DrawTextCentered("Aim with the mouse; hold to draw the cue back, release to shoot", SCREEN_W / 2.0f, bar.y + 22, 14, DIM);
}

// ---------------------------------------------------------------- mini golf
void Golf(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; no::golf::Match& m = g.golf;
    const auto& C = no::golf::Course();
    int holeShown = g.replayT > 0 ? g.golfHole : m.hole;
    const no::golf::Hole& h = C[std::clamp(holeShown, 0, 8)];
    Rectangle r{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f}; GPanel(r);
    float sc = std::min(1000 / h.box.width, 420 / h.box.height);
    Vector2 o{SCREEN_W / 2.0f - h.box.width * sc / 2, r.y + 100 + (420 - h.box.height * sc) / 2};
    auto P = [&](Vector2 q) { return Vector2{o.x + q.x * sc, o.y + q.y * sc}; };
    TxtBold(TextFormat("Hole %d: %s (par %d)", holeShown + 1, h.name.c_str(), h.par), r.x + 30, r.y + 20, 22, BRASS);
    DrawRectangle((int)o.x, (int)o.y, (int)(h.box.width * sc), (int)(h.box.height * sc), Color{44, 120, 60, 255});
    if (h.loopV > 0) { DrawRectangleRec({o.x + h.loop.x * sc, o.y + h.loop.y * sc, h.loop.width * sc, h.loop.height * sc}, Color{70, 90, 140, 255}); DrawTextCentered("the loop", P({h.loop.x + h.loop.width / 2, 0}).x, P({0, h.loop.y + h.loop.height / 2}).y - 8, 14, INK); }
    if (h.drainR > 0) { DrawCircleV(P(h.drain), h.drainR * sc, Color{20, 20, 24, 255}); for (int k = -2; k <= 2; k++) DrawLineEx({P(h.drain).x + k * 5.0f, P(h.drain).y - h.drainR * sc * 0.8f}, {P(h.drain).x + k * 5.0f, P(h.drain).y + h.drainR * sc * 0.8f}, 2, Color{90, 90, 96, 255}); DrawCircleLines((int)P(h.drainOut).x, (int)P(h.drainOut).y, 8, Fade(INK, 0.4f)); }
    if (h.feature == "tank") { DrawRectangleRec({P(h.cup).x - 30, P(h.cup).y - 50, 60, 100}, Fade(Color{60, 140, 200, 255}, 0.4f)); DrawTextCentered("fish tank", P(h.cup).x, P(h.cup).y + 54, 13, INK); }
    for (const auto& w : h.walls) DrawLineEx(P(w.a), P(w.b), 7, Color{120, 76, 40, 255});
    if (h.millR > 0) {
        float th = (m.sim.t - g.replayT) * 2 * PI / 3;
        if (g.replayT > 0) th = (m.sim.t - g.replayT) * 2 * PI / 3;
        DrawCircleV(P(h.mill), 12, Color{200, 190, 170, 255});
        for (int k = 0; k < 2; k++) { float a = th + k * PI; DrawLineEx(P(h.mill), P(Vector2Add(h.mill, {cosf(a) * h.millR, sinf(a) * h.millR})), 8, Color{220, 210, 190, 255}); }
    }
    if (h.dogR > 0) {
        Vector2 dg = P(m.sim.dog.x == 0 && m.sim.dog.y == 0 ? h.dog : m.sim.dog);
        DrawEllipse((int)dg.x, (int)dg.y, h.dogR * sc, h.dogR * sc * 0.7f, Color{150, 100, 60, 255}); DrawCircleV({dg.x + h.dogR * sc * 0.8f, dg.y - 6}, h.dogR * sc * 0.38f, Color{160, 110, 70, 255});
        DrawText("z", (int)(dg.x + h.dogR * sc), (int)(dg.y - h.dogR * sc - 10 - 4 * sinf(U.t * 2)), 18, INK);
    }
    DrawCircleV(P(h.cup), no::golf::CUP * sc, Color{10, 10, 10, 255});
    DrawLineEx(P(h.cup), {P(h.cup).x, P(h.cup).y - 60}, 2, Color{230, 230, 220, 255}); DrawTri({P(h.cup).x, P(h.cup).y - 60}, {P(h.cup).x, P(h.cup).y - 44}, {P(h.cup).x + 22, P(h.cup).y - 52}, Color{220, 50, 40, 255});
    // the balls: the replay's, then whoever is still playing this hole
    for (int w = 0; w < 2; w++) {
        if (w == 1 && g.opp == -1) continue;
        Vector2 b = m.ball[w].p;
        if (g.replayT > 0 && w == g.golfWho && !g.golfPath.empty()) b = g.golfPath[std::clamp((int)(g.replayLen - g.replayT * 60), 0, (int)g.golfPath.size() - 1)];
        else if (g.replayT <= 0 && (m.done[w] || holeShown != m.hole)) continue;
        if (g.replayT > 0 && w != g.golfWho && (m.done[w] || holeShown != m.hole)) continue;
        DrawCircleV({P(b).x + 1, P(b).y + 2}, no::golf::BR * sc + 1, Fade(BLACK, 0.4f));
        DrawCircleV(P(b), no::golf::BR * sc + 1, w == 0 ? WHITE : Color{250, 220, 80, 255});
    }
    // the card
    float cx = r.x + 30, cy = r.y + r.height - 120;
    for (int k = 0; k < 9; k++) Txt(TextFormat("%d", k + 1), cx + 120 + k * 40, cy, 14, DIM);
    Txt("You", cx, cy + 22, 16, INK); for (int k = 0; k < 9; k++) if (m.strokes[0][k]) Txt(TextFormat("%d", m.strokes[0][k]), cx + 120 + k * 40, cy + 22, 16, INK);
    Txt(TextFormat("%d", m.Total(0)), cx + 120 + 9 * 40 + 10, cy + 22, 16, BRASS);
    if (g.opp != -1) { Txt(g.oppName.c_str(), cx, cy + 44, 16, INK); for (int k = 0; k < 9; k++) if (m.strokes[1][k]) Txt(TextFormat("%d", m.strokes[1][k]), cx + 120 + k * 40, cy + 44, 16, INK); Txt(TextFormat("%d", m.Total(1)), cx + 120 + 9 * 40 + 10, cy + 44, 16, BRASS); }
    if (g.stake > 0) Txt(TextFormat("for %d", g.stake), cx + 600, cy + 22, 15, DIM);
    LeaveButton(p, r, !g.over && g.opp != -1);
    if (g.over && g.replayT <= 0) { OverButtons(p, r); return; }
    if (m.turn != 0 || g.replayT > 0) { U.charging = false; if (m.turn == 1 && g.replayT <= 0) DrawTextCentered(g.oppName + " lines up a putt.", SCREEN_W / 2.0f, r.y + 70, 16, INK); return; }
    // the putting line sways with the meter; hold for power
    Vector2 ms = GetMousePosition(); Vector2 b = m.ball[0].p; Vector2 tm{(ms.x - o.x) / sc, (ms.y - o.y) / sc};
    float sway = no::GD().golfSigma * n.PlayerAim(p) * 2.2f;
    float ang = atan2f(tm.y - b.y, tm.x - b.x) + sway * (sinf(U.t * 1.9f) + 0.5f * sinf(U.t * 4.3f + 1.3f));
    DrawLineEx(P(b), P(Vector2Add(b, {cosf(ang) * 0.9f, sinf(ang) * 0.9f})), 2, Fade(WHITE, 0.6f));
    bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);
    if (down && !U.charging && CheckCollisionPointRec(ms, {o.x - 30, o.y - 30, h.box.width * sc + 60, h.box.height * sc + 60})) { U.charging = true; U.chargeT = 0; }
    float k = U.charging ? Tri(U.chargeT * 0.55f) : 0;
    Rectangle bar{SCREEN_W / 2.0f - 160, r.y + 66, 320, 16};
    DrawRectangleRounded(bar, 0.5f, 6, Color{40, 30, 22, 255}); DrawRectangleRounded({bar.x + 2, bar.y + 2, (bar.width - 4) * k, bar.height - 4}, 0.5f, 6, Color{230, 150, 70, 255});
    if (U.charging) {
        U.chargeT += GetFrameTime();
        if (!down) { U.charging = false; p.in.gameAim = {ang, 0}; p.in.gamePower = 0.3f + 4.6f * k; p.in.gameAct = 1; }
    }
}

// ---------------------------------------------------------------- the slots, scratch-offs, the fortune teller
void Symbol(int s, Vector2 c, float k) {
    switch (s) {
        case 0: DrawEllipse((int)c.x, (int)c.y, 26 * k, 14 * k, Color{240, 140, 50, 255}); DrawTri({c.x + 22 * k, c.y}, {c.x + 40 * k, c.y + 14 * k}, {c.x + 40 * k, c.y - 14 * k}, Color{240, 140, 50, 255}); DrawCircleV({c.x - 14 * k, c.y - 3 * k}, 3 * k, BLACK); break;   // a fish
        case 1: DrawLineEx({c.x, c.y - 28 * k}, {c.x, c.y + 24 * k}, 6 * k, Color{90, 100, 120, 255}); DrawLineEx({c.x - 16 * k, c.y - 16 * k}, {c.x + 16 * k, c.y - 16 * k}, 5 * k, Color{90, 100, 120, 255}); DrawRing(c, 18 * k, 24 * k, 20, 160, 10, Color{90, 100, 120, 255}); break;   // an anchor
        case 2: DrawLineEx({c.x - 28 * k, c.y - 6 * k}, {c.x, c.y + 8 * k}, 5 * k, Color{230, 230, 230, 255}); DrawLineEx({c.x, c.y + 8 * k}, {c.x + 28 * k, c.y - 6 * k}, 5 * k, Color{230, 230, 230, 255}); break;   // a gull
        case 3: DrawEllipse((int)c.x, (int)c.y, 22 * k, 28 * k, Color{150, 40, 50, 255}); DrawEllipse((int)(c.x + 12 * k), (int)c.y, 9 * k, 10 * k, PANEL); break;   // a kidney
        case 4: DrawRectangleV({c.x - 10 * k, c.y - 26 * k}, {18 * k, 40 * k}, Color{110, 70, 40, 255}); DrawRectangleV({c.x - 10 * k, c.y + 6 * k}, {34 * k, 14 * k}, Color{110, 70, 40, 255}); break;   // a boot
        default: DrawRectangleV({c.x - 11 * k, c.y - 12 * k}, {22 * k, 38 * k}, Color{50, 120, 70, 255}); DrawRectangleV({c.x - 4 * k, c.y - 28 * k}, {8 * k, 18 * k}, Color{50, 120, 70, 255}); break;   // a bottle
    }
}
void Slots(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game;
    Rectangle r{SCREEN_W / 2.0f - 300, 90, 600, 520}; GPanel(r);
    DrawTextCenteredBold(TextFormat("The %s wheel", g.machine == 0 ? "first" : g.machine == 1 ? "second" : "third"), r.x + r.width / 2, r.y + 14, 24, BRASS);
    for (int k = 0; k < 3; k++) {
        Rectangle w{r.x + 60 + k * 170.0f, r.y + 80, 140, 180};
        DrawRectangleRec(w, Color{236, 228, 206, 255}); DrawRectangleLinesEx(w, 4, Color{120, 90, 50, 255});
        bool spinning = g.spinT > 0 && g.spinT > 0.12f * (3 - k);
        int s = spinning ? (int)(U.t * 14 + k * 3) % 6 : g.pulls ? g.pull.reel[k] : (k * 2) % 6;
        Symbol(s, {w.x + w.width / 2, w.y + w.height / 2 + (spinning ? fmodf(U.t * 900, 60) - 30 : 0)}, 1.4f);
    }
    if (g.pulls && g.spinT <= 0) DrawTextCenteredBold(g.pull.pays ? TextFormat("Pays %d!%s", g.pull.pays, g.pull.kidney ? " And a kidney." : "") : "Nothing.", r.x + r.width / 2, r.y + 290, 22, g.pull.pays ? Color{255, 220, 120, 255} : DIM);
    Txt("Pair of kidneys 50   Three of a kind 200   Three kidneys 1,000 and a kidney", r.x + 30, r.y + 330, 14, DIM);
    Txt(TextFormat("Coins: %.0f    Pulls: %d", p.money, g.pulls), r.x + 30, r.y + 356, 16, INK);
    if (p.drunk >= 80) DrawTextCentered("You can't stop pulling.", r.x + r.width / 2, r.y + 390, 18, Color{240, 160, 140, 255});
    else if (Btn({r.x + r.width / 2 - 100, r.y + 400, 200, 44}, TextFormat("Pull (%d)", no::GD().slotCost), g.spinT <= 0 && p.money >= no::GD().slotCost, 18) || (IsKeyPressed(KEY_SPACE) && g.spinT <= 0)) p.in.gameAct = 1;
    if (Btn({r.x + r.width / 2 - 80, r.y + r.height - 52, 160, 36}, "Walk away") || IsKeyPressed(KEY_ESCAPE)) p.in.gameAct = 3;
}
void Scratch(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; const auto& d = no::GD();
    Rectangle r{SCREEN_W / 2.0f - 320, 90, 640, 520}; GPanel(r);
    bool pip = g.kind == no::GK_PIP;
    Rectangle tk{r.x + 60, r.y + 70, r.width - 120, 300};
    DrawRectangleRounded(tk, 0.06f, 6, pip ? Color{220, 196, 150, 255} : Color{236, 226, 196, 255});
    DrawTextCenteredBold(pip ? "PIP'S SPECIAL" : "LUCKY CATCH", tk.x + tk.width / 2, tk.y + 14, 26, Color{150, 40, 36, 255});
    DrawTextCentered("Three alike wins", tk.x + tk.width / 2, tk.y + 46, 14, Color{90, 70, 50, 255});
    if (!g.haveTicket) { DrawTextCentered("No ticket yet.", tk.x + tk.width / 2, tk.y + 140, 18, Color{90, 70, 50, 255}); }
    else {
        if (!U.masked) { for (auto& m : U.mask) for (float& x : m) x = 1; U.masked = true; }
        Vector2 ms = GetMousePosition(); bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        float jitter = p.drunk / 100 * 3;   // (a wrecked player scratches the table)
        float done = 1;
        for (int k = 0; k < 3; k++) {
            Rectangle pn{tk.x + 30 + k * 170.0f, tk.y + 90, 150, 150};
            DrawRectangleRec(pn, Color{250, 244, 230, 255});
            Vector2 cc{pn.x + pn.width / 2, pn.y + pn.height / 2 - 8};
            int s = g.ticket.sym[k];
            DrawCircleV(cc, 36, s == 0 ? Color{230, 170, 150, 255} : s == 1 ? Color{196, 202, 226, 255} : s == 2 ? Color{120, 130, 150, 255} : Color{230, 190, 60, 255});
            DrawTextCentered(d.scratchSymbols[std::clamp(s, 0, (int)d.scratchSymbols.size() - 1)], cc.x, pn.y + pn.height - 26, 15, Color{60, 50, 40, 255});
            float left = 0;
            for (int y = 0; y < 10; y++) for (int x = 0; x < 16; x++) {
                float& m = U.mask[k][y * 16 + x];
                Rectangle cell{pn.x + x * pn.width / 16, pn.y + y * pn.height / 10, pn.width / 16 + 1, pn.height / 10 + 1};
                if (down && m > 0) {
                    Vector2 bm{ms.x + sinf(U.t * 23) * jitter * 9, ms.y + cosf(U.t * 19) * jitter * 9};
                    if (CheckCollisionPointCircle({cell.x + cell.width / 2, cell.y + cell.height / 2}, bm, 16)) m = std::max(0.0f, m - GetFrameTime() * 8);
                }
                if (m > 0) DrawRectangleRec(cell, Fade(Color{170, 170, 176, 255}, m));
                left += m > 0.2f;
            }
            done = std::min(done, 1 - left / 160);
        }
        if (done > 0.7f && !g.paid) p.in.gameAct = 1;
        if (g.paid) DrawTextCenteredBold(g.ticket.prize ? TextFormat("Winner: %d!", g.ticket.prize) : "Nothing. Of course.", tk.x + tk.width / 2, tk.y + tk.height - 40, 22, g.ticket.prize ? Color{150, 40, 36, 255} : Color{90, 70, 50, 255});
        if (g.paid && g.ticket.map) DrawTextCentered("On the back, in pencil: a map to the safe.", tk.x + tk.width / 2, tk.y + tk.height + 8, 16, BRASS);
    }
    if (Btn({r.x + r.width / 2 - 100, r.y + 410, 200, 40}, TextFormat("Buy a ticket (%d)", d.scratchCost), (!g.haveTicket || g.paid) && p.money >= d.scratchCost)) { p.in.gameAct = 5; U.masked = false; }
    if (Btn({r.x + r.width / 2 - 80, r.y + r.height - 50, 160, 36}, "Done") || IsKeyPressed(KEY_ESCAPE)) p.in.gameAct = 3;
}
// ---------------------------------------------------------------- the card room: hold'em and bullshit
void DrawSuit(float cx, float cy, float k, int s) {
    Color c = (s == 1 || s == 2) ? Color{200, 40, 40, 255} : Color{24, 24, 28, 255};
    if (s == 1) { DrawCircleV({cx - 3.5f * k, cy - 2 * k}, 4 * k, c); DrawCircleV({cx + 3.5f * k, cy - 2 * k}, 4 * k, c); DrawTri({cx - 7.5f * k, cy - 0.5f * k}, {cx + 7.5f * k, cy - 0.5f * k}, {cx, cy + 8 * k}, c); }
    else if (s == 2) { DrawTri({cx, cy - 8 * k}, {cx + 6 * k, cy}, {cx - 6 * k, cy}, c); DrawTri({cx, cy + 8 * k}, {cx + 6 * k, cy}, {cx - 6 * k, cy}, c); }
    else if (s == 0) { DrawTri({cx, cy - 8 * k}, {cx + 7 * k, cy + 2 * k}, {cx - 7 * k, cy + 2 * k}, c); DrawCircleV({cx - 3.5f * k, cy + 2 * k}, 3.8f * k, c); DrawCircleV({cx + 3.5f * k, cy + 2 * k}, 3.8f * k, c); DrawRectangleV({cx - 1 * k, cy + 2 * k}, {2 * k, 7 * k}, c); }
    else { DrawCircleV({cx, cy - 4 * k}, 3.6f * k, c); DrawCircleV({cx - 4 * k, cy + 1.5f * k}, 3.6f * k, c); DrawCircleV({cx + 4 * k, cy + 1.5f * k}, 3.6f * k, c); DrawRectangleV({cx - 1 * k, cy + 1 * k}, {2 * k, 8 * k}, c); }
}
void DrawCard(float x, float y, float w, no::cards::Card c, bool up, bool lit = false) {
    float h = w * 1.4f;
    DrawRectangleRounded({x + 2, y + 3, w, h}, 0.15f, 6, Fade(BLACK, 0.4f));
    if (!up || c.r < 2) { DrawRectangleRounded({x, y, w, h}, 0.15f, 6, Color{120, 30, 36, 255}); DrawRectangleRoundedLinesEx({x + 4, y + 4, w - 8, h - 8}, 0.15f, 6, 1.5f, Color{220, 190, 120, 255}); return; }
    DrawRectangleRounded({x, y, w, h}, 0.15f, 6, Color{248, 244, 232, 255});
    if (lit) DrawRectangleRoundedLinesEx({x - 2, y - 2, w + 4, h + 4}, 0.15f, 6, 2, Color{200, 160, 255, 255});
    static const char* RN[15] = {"", "", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    Color ink = (c.s == 1 || c.s == 2) ? Color{200, 40, 40, 255} : Color{24, 24, 28, 255};
    TxtBold(RN[std::clamp((int)c.r, 0, 14)], x + 5, y + 3, (int)(w * 0.36f), ink);
    DrawSuit(x + w / 2, y + h * 0.6f, w / 32, c.s);
}
int gRaise = 0;
void Poker(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; no::cards::Poker& P = g.machine == 1 ? n.cartelHand : n.poker;
    Rectangle r{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f}; GPanel(r);
    Vector2 c{SCREEN_W / 2.0f, r.y + 270};
    DrawEllipse((int)c.x, (int)c.y, 430, 200, Color{90, 52, 30, 255}); DrawEllipse((int)c.x, (int)c.y, 410, 182, Color{26, 92, 60, 255});
    TxtBold(g.machine == 1 ? "The quiet man's hand: a kidney in the middle" : TextFormat("Hold'em   blinds %d/%d", P.sb, P.bb), r.x + 24, r.y + 16, 18, BRASS);
    int me = -1; for (int k = 0; k < (int)P.seats.size(); k++) if (P.seats[k].kind == 0 && P.seats[k].idx == p.id) me = k;
    int N = (int)P.seats.size();
    bool showdown = P.street == 5 && P.result.find(" with ") != std::string::npos;
    for (int k = 0; k < N; k++) {
        const no::cards::Seat& s = P.seats[k];
        int rel = me >= 0 ? (k - me + N) % N : k;
        float a = PI / 2 + rel * 2 * PI / std::max(1, N);
        Vector2 at{c.x + cosf(a) * 400, c.y + sinf(a) * 190};
        bool turn = P.turn == k && P.street <= 3;
        DrawRectangleRounded({at.x - 80, at.y - 30, 160, 60}, 0.3f, 6, turn ? Color{90, 70, 30, 240} : Color{30, 22, 16, 230});
        DrawTextCenteredBold(s.name, at.x, at.y - 26, 15, s.out ? DIM : INK);
        DrawTextCentered(s.out ? "out" : s.folded ? "folded" : TextFormat("%d chips%s", s.stack, s.allIn ? " (all in)" : ""), at.x, at.y - 8, 13, DIM);
        if (!s.last.empty() && P.street <= 3) DrawTextCentered(s.last, at.x, at.y + 8, 12, Color{240, 210, 150, 255});
        if (s.tellNow && p.drunk < 60 && k != me) DrawTextCentered(s.name + " " + s.tell, at.x, at.y - 48, 14, Color{200, 190, 255, 255});
        if (s.bet > 0) DrawTextCentered(TextFormat("%d", s.bet), at.x + (c.x - at.x) * 0.3f, at.y + (c.y - at.y) * 0.3f, 15, Color{255, 230, 140, 255});
        bool mine = k == me, peek = (s.peeked >> p.id) & 1, up = mine || peek || (showdown && !s.folded && !s.out);
        if (!s.out && !s.folded && (P.street <= 3 || up)) { float cx = mine ? c.x - 60 : at.x - 34, cy = mine ? r.y + r.height - 230 : at.y + 34; float cw = mine ? 56 : 32; DrawCard(cx, cy, cw, s.hole[0], up, peek); DrawCard(cx + cw + 6, cy, cw, s.hole[1], up, peek); }
    }
    for (int k = 0; k < P.boardN; k++) DrawCard(c.x - 150 + k * 62, c.y - 40, 54, P.board[k], true);
    DrawTextCentered(TextFormat("pot %d", P.Pot() + (P.street == 5 ? 0 : 0)), c.x, c.y + 50, 16, Color{255, 230, 140, 255});
    if (P.street == 5 && !P.result.empty()) DrawTextCenteredBold(P.result, c.x, c.y + 72, 18, Color{255, 220, 120, 255});
    LeaveButton(p, r, false);
    if (g.over) { DrawTextCenteredBold(g.caption, r.x + r.width / 2, r.y + r.height - 110, 18, INK); if (Btn({r.x + r.width / 2 - 80, r.y + r.height - 70, 160, 36}, "Leave the table")) p.in.gameAct = 3; return; }
    if (me < 0 || P.turn != me || P.street > 3) { gRaise = 0; return; }
    const no::cards::Seat& ms = P.seats[me];
    int owe = P.toCall - ms.bet, minTo = P.toCall + P.minRaise;
    if (gRaise < minTo) gRaise = minTo;
    gRaise = std::clamp(gRaise + (int)GetMouseWheelMove() * P.bb, minTo, ms.bet + ms.stack);
    float by = r.y + r.height - 70;
    if (Btn({r.x + 260, by, 130, 38}, "1 Fold") || IsKeyPressed(KEY_ONE)) p.in.gameAct = 21;
    if (Btn({r.x + 400, by, 150, 38}, owe > 0 ? TextFormat("2 Call %d", std::min(owe, ms.stack)) : "2 Check") || IsKeyPressed(KEY_TWO)) p.in.gameAct = 22;
    if (Btn({r.x + 560, by, 34, 38}, "-")) gRaise = std::max(minTo, gRaise - P.bb);
    if (Btn({r.x + 600, by, 170, 38}, gRaise >= ms.bet + ms.stack ? "3 All in" : TextFormat("3 Raise to %d", gRaise)) || IsKeyPressed(KEY_THREE)) { p.in.gameAct = 23; p.in.gamePower = (float)gRaise; }
    if (Btn({r.x + 776, by, 34, 38}, "+")) gRaise = std::min(ms.bet + ms.stack, gRaise + P.bb);
    if (Btn({r.x + 830, by, 190, 38}, "V: mark the deck", true, 14) || IsKeyPressed(KEY_V)) p.in.gameAct = 24;
}
void Bullshit(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; no::cards::Bullshit& B = n.bs;
    Rectangle r{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f}; GPanel(r);
    Vector2 c{SCREEN_W / 2.0f, r.y + 230};
    DrawEllipse((int)c.x, (int)c.y, 300, 150, Color{70, 40, 26, 255}); DrawEllipse((int)c.x, (int)c.y, 282, 134, Color{40, 70, 90, 255});
    TxtBold(TextFormat("Bullshit   pot %d", B.pot), r.x + 24, r.y + 16, 18, BRASS);
    int me = -1; for (int k = 0; k < (int)B.seats.size(); k++) if (B.seats[k].kind == 0 && B.seats[k].idx == p.id) me = k;
    LeaveButton(p, r, n.bsOn);
    if (!n.bsOn && !g.over) { DrawTextCenteredBold("Waiting for players...", c.x, c.y, 20, INK); return; }
    int N = (int)B.seats.size();
    for (int k = 0; k < N; k++) {
        if (k == me) continue;
        int rel = me >= 0 ? (k - me + N) % N : k; float a = PI / 2 + rel * 2 * PI / std::max(1, N);
        Vector2 at{c.x + cosf(a) * 300, c.y + sinf(a) * 150};
        DrawRectangleRounded({at.x - 80, at.y - 24, 160, 48}, 0.3f, 6, B.turn == k && B.window <= 0 ? Color{90, 70, 30, 240} : Color{30, 22, 16, 230});
        DrawTextCenteredBold(B.seats[k].name, at.x, at.y - 20, 15, INK);
        DrawTextCentered(TextFormat("%d cards", (int)B.seats[k].hand.size()), at.x, at.y, 13, DIM);
    }
    // (the playtest: players couldn't follow it) the rank ladder, the rules, and a short history of what's been said
    {
        static const char* RW[15] = {"", "", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
        static const int ORDER[13] = {14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        Rectangle lb{r.x + 20, r.y + 52, 230, 150};
        DrawRectangleRounded(lb, 0.06f, 6, Color{24, 18, 14, 230});
        TxtBold("The rank to claim", lb.x + 12, lb.y + 10, 15, BRASS);
        Txt("(they go round in order)", lb.x + 12, lb.y + 30, 12, DIM);
        for (int k = 0; k < 13; k++) {
            int rk = ORDER[k]; bool now = rk == B.rank, next = rk == B.NextRank();
            Rectangle q{lb.x + 12 + (k % 7) * 30, lb.y + 52 + (k / 7) * 34, 26, 28};
            DrawRectangleRounded(q, 0.25f, 6, now ? Color{240, 200, 110, 255} : next ? Color{90, 74, 40, 255} : Color{40, 30, 22, 255});
            DrawTextCenteredBold(RW[rk], q.x + 15, q.y + 6, 16, now ? Color{30, 20, 10, 255} : INK);
        }
        Txt(TextFormat("now: %s", no::cards::RankWord(B.rank, 2).c_str()), lb.x + 12, lb.y + 122, 14, Color{255, 220, 150, 255});
        Rectangle rb{r.x + 20, r.y + 212, 230, 210};
        DrawRectangleRounded(rb, 0.06f, 6, Color{24, 18, 14, 230});
        TxtBold("How it works", rb.x + 12, rb.y + 10, 15, BRASS);
        const char* L[] = {"1. On your turn, play 1-4 cards", "   face down and say they're", "   the rank that's called.", "2. You may lie.", "3. Anyone may shout BULLSHIT.", "   A liar takes the whole pile;", "   if it was true, the caller does.", "4. Empty your hand first: you", "   win the pot."};
        for (int k = 0; k < 9; k++) Txt(L[k], rb.x + 12, rb.y + 34 + k * 19, 13, INK);
        static std::vector<std::string> said; static std::string lastSaid;
        if (B.lastSeat >= 0 && !B.claim.empty()) { std::string s = B.seats[B.lastSeat].name + ": " + B.claim; if (s != lastSaid) { lastSaid = s; said.push_back(s); if (said.size() > 5) said.erase(said.begin()); } }
        Rectangle hb{r.x + r.width - 250, r.y + 52, 230, 150};
        DrawRectangleRounded(hb, 0.06f, 6, Color{24, 18, 14, 230});
        TxtBold("Said so far", hb.x + 12, hb.y + 10, 15, BRASS);
        for (int k = 0; k < (int)said.size(); k++) Txt(said[k], hb.x + 12, hb.y + 34 + k * 20, 13, k + 1 == (int)said.size() ? INK : DIM);
    }
    // the pile, the claim, the call
    for (int k = 0; k < std::min(6, (int)B.pile.size()); k++) DrawCard(c.x - 40 + k * 3, c.y - 50 - k * 2, 50, {}, false);
    DrawTextCentered(TextFormat("%d in the pile", (int)B.pile.size()), c.x, c.y + 26, 14, DIM);
    if (B.lastSeat >= 0 && !B.claim.empty()) DrawTextCenteredBold(TextFormat("%s: \"%s\"", B.seats[B.lastSeat].name.c_str(), B.claim.c_str()), c.x, c.y + 50, 18, INK);
    if (B.revealT > 0) { for (int k = 0; k < (int)B.lastCards.size(); k++) DrawCard(c.x + 120 + k * 46, c.y - 40, 42, B.lastCards[k], true); }
    if (!B.result.empty()) DrawTextCenteredBold(B.result, c.x, c.y + 76, B.revealT > 0 ? 22 : 15, B.revealT > 0 ? (B.lastWasLie ? Color{255, 120, 100, 255} : Color{140, 230, 150, 255}) : Color{255, 220, 150, 255});
    if (g.over) { DrawTextCenteredBold(g.caption, c.x, r.y + r.height - 120, 18, INK); if (Btn({c.x - 170, r.y + r.height - 70, 160, 36}, "Again (20)", p.money >= 20)) p.in.gameAct = 4; if (Btn({c.x + 10, r.y + r.height - 70, 160, 36}, "Leave")) p.in.gameAct = 3; return; }
    if (me < 0) return;
    // your hand: click to pick up to four
    auto& hand = B.seats[me].hand; int nh = (int)hand.size(); float cw = std::min(56.0f, 900.0f / std::max(1, nh)), x0 = c.x - nh * cw / 2;
    static int sel = 0;
    int honest = 0; for (const auto& h : hand) honest += h.r == B.rank;
    for (int k = 0; k < nh; k++) {
        bool on = (sel >> k) & 1; float x = x0 + k * cw, y = r.y + r.height - 200 - (on ? 18 : 0);
        if (hand[k].r == B.rank) DrawRectangleRounded({x - 3, y - 3, 60, 54 * 1.4f + 6}, 0.15f, 6, Color{240, 200, 110, 200});   // (the cards you can play honestly)
        DrawCard(x, y, 54, hand[k], true, on);
        if (Clicked() && CheckCollisionPointRec(GetMousePosition(), {x, y, cw, 76})) { int cnt = 0; for (int b = 0; b < 24; b++) cnt += (sel >> b) & 1; if (on) sel &= ~(1 << k); else if (cnt < 4) sel |= 1 << k; }
    }
    bool myTurn = B.turn == me && B.window <= 0 && B.winner < 0;
    int cnt = 0; for (int b = 0; b < 24; b++) cnt += (sel >> b) & 1;
    std::string claim = cnt > 0 ? no::cards::RankWord(B.rank, cnt) : no::cards::RankWord(B.rank, 2);
    // the turn, said plainly
    {
        std::string rks = no::cards::RankWord(B.rank, 2); rks = rks.substr(rks.find(' ') + 1);
        std::string line, sub;
        if (myTurn) { line = "YOUR TURN"; sub = honest > 0 ? TextFormat("play your %s (you have %d, outlined), or slip others in and lie", rks.c_str(), honest) : TextFormat("you have no %s: pick 1-4 cards and claim they are", rks.c_str()); }
        else if (B.window > 0 && B.lastSeat >= 0 && B.lastSeat != me) { line = TextFormat("%s says \"%s\". Believe it?", B.seats[B.lastSeat].name.c_str(), B.claim.c_str()); sub = TextFormat("BULLSHIT before the bar runs out: a liar takes the pile (%d); if it was true, you do", (int)B.pile.size()); }
        else if (B.window > 0 && B.lastSeat == me) { line = "You've played."; sub = "Wait and see if anyone calls you out..."; }
        else { line = TextFormat("%s's turn", B.seats[B.turn].name.c_str()); sub = TextFormat("they must claim %s", rks.c_str()); }
        DrawTextCenteredBold(line, c.x, r.y + r.height - 262, 18, myTurn ? Color{255, 220, 120, 255} : INK);
        DrawTextCentered(sub, c.x, r.y + r.height - 238, 14, DIM);
    }
    if (myTurn) { if (Btn({c.x - 150, r.y + r.height - 70, 300, 38}, cnt > 0 ? TextFormat("Play: \"%s\"", claim.c_str()) : TextFormat("Pick cards (it's %s)", claim.substr(claim.find(' ') + 1).c_str()), cnt > 0)) { p.in.gameAct = 25; p.in.gameStake = sel; sel = 0; } }
    else if (B.window > 0 && B.lastSeat != me) {
        DrawRectangle((int)(c.x - 150), (int)(r.y + r.height - 86), (int)(300 * B.window / 3), 6, Color{240, 140, 120, 255});
        if (Btn({c.x - 150, r.y + r.height - 76, 200, 40}, "BULLSHIT! (B)", true, 17) || IsKeyPressed(KEY_B)) p.in.gameAct = 26;
        if (Btn({c.x + 60, r.y + r.height - 76, 120, 40}, "V: peek", true, 14) || IsKeyPressed(KEY_V)) p.in.gameAct = 27;
    } else DrawTextCentered(TextFormat("%s to play %s", B.seats[B.turn].name.c_str(), no::cards::RankWord(B.rank, 2).c_str()), c.x, r.y + r.height - 64, 15, DIM);
}
struct DanceState { float t = -1; int hits = 0, beat = 0; bool judged[16] = {}; };
DanceState gDance;
void Dance(no::Night& n, no::Player& p) {
    // sixteen beats at the band's tempo; press Space as each one crosses the line (the drunker you are, the more the line sways)
    no::GameSeat& g = p.game;
    Rectangle r{SCREEN_W / 2.0f - 360, SCREEN_H - 250.0f, 720, 200}; GPanel(r);
    DrawTextCenteredBold("The dance floor", r.x + r.width / 2, r.y + 10, 20, BRASS);
    if (g.over) { DrawTextCentered(g.caption, r.x + r.width / 2, r.y + 90, 16, INK); if (Btn({r.x + r.width / 2 - 80, r.y + 140, 160, 36}, "Off the floor") || IsKeyPressed(KEY_ESCAPE)) { p.in.gameAct = 3; gDance = DanceState{}; } return; }
    if (gDance.t < 0) { gDance = DanceState{}; gDance.t = 0; }
    gDance.t += GetFrameTime();
    const float BEAT = 0.5f, LEAD = 2.0f; float sway = std::clamp(p.drunk / 100, 0.0f, 1.0f) * 40 * sinf(gDance.t * 2.1f);
    float lineX = r.x + 160 + sway;
    DrawLineEx({lineX, r.y + 50}, {lineX, r.y + 150}, 3, BRASS);
    for (int k = 0; k < 16; k++) {
        float due = LEAD + k * BEAT, x = lineX + (due - gDance.t) * 260;
        if (x < r.x + 20 || x > r.x + r.width - 20) continue;
        Color c = gDance.judged[k] ? Color{120, 220, 130, 255} : Color{240, 200, 140, 255};
        DrawCircleV({x, r.y + 100}, 14, c);
    }
    if (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int k = (int)lroundf((gDance.t - LEAD) / BEAT);
        if (k >= 0 && k < 16 && !gDance.judged[k] && fabsf(gDance.t - (LEAD + k * BEAT)) < 0.12f) { gDance.judged[k] = true; gDance.hits++; }
    }
    DrawTextCentered(TextFormat("%d beats", gDance.hits), r.x + r.width - 80, r.y + 16, 15, DIM);
    DrawTextCentered("Space on the beat", r.x + r.width / 2, r.y + 165, 14, DIM);
    if (gDance.t > LEAD + 16 * BEAT + 0.4f) { p.in.gameAct = 1; p.in.gamePower = gDance.hits / 16.0f; gDance.t = -1; }
}
// an emblem for each of the deck's 22 cards, in ink on the card's parchment
void TarotIcon(const std::string& nm, Vector2 c, float s, Color k) {
    auto L = [&](float x0, float y0, float x1, float y1, float w = 3) { DrawLineEx({c.x + x0 * s, c.y + y0 * s}, {c.x + x1 * s, c.y + y1 * s}, w, k); };
    auto C = [&](float x, float y, float r, bool fill = false) { if (fill) DrawCircleV({c.x + x * s, c.y + y * s}, r * s, k); else DrawRing({c.x + x * s, c.y + y * s}, r * s - 1.5f, r * s + 1.5f, 0, 360, 36, k); };
    auto R = [&](float x, float y, float w, float h) { DrawRectangleLinesEx({c.x + x * s, c.y + y * s, w * s, h * s}, 3, k); };
    if (nm == "The Diver") { C(0, 0, 0.8f); C(0, 0.05f, 0.35f); L(-0.8f, 0.9f, 0.8f, 0.9f); }
    else if (nm == "The Helm") { C(0, 0, 0.7f); C(0, 0, 0.15f, true); for (int i = 0; i < 8; i++) { float a = i * PI / 4; L(cosf(a) * 0.15f, sinf(a) * 0.15f, cosf(a) * 0.95f, sinf(a) * 0.95f); } }
    else if (nm == "The Periscope") { L(-0.2f, 0.9f, -0.2f, -0.6f, 6); L(-0.2f, -0.6f, 0.4f, -0.6f, 6); R(0.35f, -0.75f, 0.2f, 0.3f); L(-0.9f, 0.4f, 0.9f, 0.4f); }
    else if (nm == "The Lantern") { R(-0.35f, -0.4f, 0.7f, 0.9f); C(0, 0.05f, 0.18f, true); L(0, -0.4f, 0, -0.75f); C(0, -0.82f, 0.08f); }
    else if (nm == "The Kraken") { C(0, -0.35f, 0.4f); for (int i = 0; i < 5; i++) { float x = -0.6f + i * 0.3f; L(x * 0.5f, 0, x, 0.5f); L(x, 0.5f, x * 1.3f, 0.85f); } }
    else if (nm == "The Drowned") { C(0, -0.2f, 0.5f); C(-0.18f, -0.25f, 0.1f, true); C(0.18f, -0.25f, 0.1f, true); for (int i = 0; i < 3; i++) L(-0.8f, 0.5f + i * 0.18f, 0.8f, 0.5f + i * 0.18f, 2); }
    else if (nm == "The Siren") { C(0, -0.55f, 0.22f); L(0, -0.3f, 0.1f, 0.2f); L(0.1f, 0.2f, -0.2f, 0.6f); L(-0.2f, 0.6f, 0.2f, 0.85f); L(0.2f, 0.85f, -0.1f, 0.9f); }
    else if (nm == "The Island") { DrawRing({c.x, c.y + 0.6f * s}, 0.7f * s - 1.5f, 0.7f * s + 1.5f, 180, 360, 24, k); L(0.1f, -0.1f, 0.2f, -0.8f); L(0.2f, -0.8f, -0.3f, -0.6f); L(0.2f, -0.8f, 0.6f, -0.55f); L(-0.9f, 0.6f, 0.9f, 0.6f); }
    else if (nm == "The Cave") { DrawRing({c.x, c.y + 0.5f * s}, 0.75f * s - 1.5f, 0.75f * s + 1.5f, 180, 360, 24, k); DrawRing({c.x, c.y + 0.5f * s}, 0.35f * s - 1.5f, 0.35f * s + 1.5f, 180, 360, 24, k); L(-0.9f, 0.5f, 0.9f, 0.5f); }
    else if (nm == "The Weeds") { for (int i = 0; i < 4; i++) { float x = -0.6f + i * 0.4f; for (int j = 0; j < 6; j++) L(x + 0.12f * sinf(j * 1.3f), 0.8f - j * 0.27f, x + 0.12f * sinf((j + 1) * 1.3f), 0.8f - (j + 1) * 0.27f); } }
    else if (nm == "The Sunken City") { for (int i = 0; i < 4; i++) R(-0.75f + i * 0.4f, -0.4f + (i % 2) * 0.2f, 0.18f, 1.1f - (i % 2) * 0.2f); L(-0.9f, -0.5f, 0.6f, -0.6f); }
    else if (nm == "The Abyss") { for (int i = 1; i <= 4; i++) C(0, 0, 0.22f * i); C(0, 0, 0.12f, true); }
    else if (nm == "The Bottle") { R(-0.3f, -0.1f, 0.6f, 0.9f); R(-0.1f, -0.7f, 0.2f, 0.6f); L(-0.3f, 0.3f, 0.3f, 0.3f); }
    else if (nm == "The Coin") { C(0, 0, 0.7f); C(0, 0, 0.5f); L(0, -0.3f, 0, 0.3f); L(-0.15f, -0.15f, 0.15f, -0.15f); L(-0.15f, 0.15f, 0.15f, 0.15f); }
    else if (nm == "The Knife") { L(-0.6f, 0.6f, 0.5f, -0.5f, 6); L(0.5f, -0.5f, 0.75f, -0.8f, 3); L(-0.75f, 0.45f, -0.45f, 0.75f, 5); }
    else if (nm == "The Key") { C(-0.45f, -0.45f, 0.3f); L(-0.25f, -0.25f, 0.6f, 0.6f, 5); L(0.35f, 0.35f, 0.55f, 0.15f); L(0.5f, 0.5f, 0.7f, 0.3f); }
    else if (nm == "The Dog") { C(0, 0, 0.45f); L(-0.35f, -0.3f, -0.6f, 0.3f, 5); L(0.35f, -0.3f, 0.6f, 0.3f, 5); C(0, 0.2f, 0.1f, true); C(-0.15f, -0.08f, 0.06f, true); C(0.15f, -0.08f, 0.06f, true); }
    else if (nm == "The Goat") { C(0, 0.15f, 0.38f); L(-0.2f, -0.2f, -0.6f, -0.8f, 4); L(0.2f, -0.2f, 0.6f, -0.8f, 4); L(0, 0.5f, 0, 0.85f, 3); }
    else if (nm == "The Gull") { L(-0.8f, -0.1f, -0.35f, -0.35f); L(-0.35f, -0.35f, 0, 0); L(0, 0, 0.35f, -0.35f); L(0.35f, -0.35f, 0.8f, -0.1f); L(-0.9f, 0.6f, 0.9f, 0.6f, 2); }
    else if (nm == "The Stitch") { L(-0.8f, 0, 0.8f, 0, 3); for (int i = 0; i < 6; i++) { float x = -0.65f + i * 0.26f; L(x - 0.08f, -0.2f, x + 0.08f, 0.2f, 3); } }
    else if (nm == "The Bathtub") { L(-0.8f, -0.1f, 0.8f, -0.1f); DrawRing({c.x, c.y - 0.1f * s}, 0.8f * s - 1.5f, 0.8f * s + 1.5f, 0, 180, 24, k); L(-0.5f, 0.6f, -0.6f, 0.85f); L(0.5f, 0.6f, 0.6f, 0.85f); R(-0.25f, -0.45f, 0.3f, 0.3f); }
    else if (nm == "The Morning") { DrawRing({c.x, c.y + 0.3f * s}, 0.45f * s - 1.5f, 0.45f * s + 1.5f, 180, 360, 24, k); for (int i = 0; i < 7; i++) { float a = PI + i * PI / 6; L(cosf(a) * 0.6f, 0.3f + sinf(a) * 0.6f, cosf(a) * 0.85f, 0.3f + sinf(a) * 0.85f); } L(-0.9f, 0.3f, 0.9f, 0.3f); }
    else C(0, 0, 0.5f);
}
void Fortune(no::Night& n, no::Player& p) {
    no::GameSeat& g = p.game; const auto& d = no::GD();
    Rectangle r{SCREEN_W / 2.0f - 420, 70, 840, 560}; GPanel(r);
    DrawTextCenteredBold("The fortune teller", r.x + r.width / 2, r.y + 14, 24, BRASS);
    std::string key = g.haveReading ? g.reading.text[0] + g.reading.text[1] : "";
    if (U.revealT < -50) { U.readingKey = key; U.revealT = 10; }
    if (key != U.readingKey) { U.readingKey = key; U.revealT = 0; }
    U.revealT += GetFrameTime();
    for (int k = 0; k < 3; k++) {
        Rectangle c{r.x + 90 + k * 240.0f, r.y + 70, 180, 260};
        bool up = g.haveReading && U.revealT > 0.6f + k * 1.2f;
        if (!up) { DrawRectangleRounded(c, 0.08f, 6, Color{60, 30, 70, 255}); DrawRectangleRoundedLinesEx(c, 0.08f, 6, 3, BRASS); DrawCircleLines((int)(c.x + c.width / 2), (int)(c.y + c.height / 2), 40, BRASS); continue; }
        DrawRectangleRounded(c, 0.08f, 6, Color{236, 224, 196, 255}); DrawRectangleRoundedLinesEx(c, 0.08f, 6, 3, Color{120, 80, 40, 255});
        int ci = g.reading.card[k];
        if (p.wareT[no::W_ANGLER] > 0) ci = (ci + (int)(GetTime() * 1.7) + k * 5) % std::max(1, (int)d.deck.size());   // (Angler's Light: the cards change when you look at them)
        const std::string& nm = d.deck[std::clamp(ci, 0, (int)d.deck.size() - 1)];
        DrawTextCenteredBold(nm, c.x + c.width / 2, c.y + 16, 17, Color{80, 30, 40, 255});
        TarotIcon(nm, {c.x + c.width / 2, c.y + 135}, 62, Color{70, 40, 50, 255});
        static const char* POS[3] = {"who you'll meet", "what you'll do", "how it ends"};
        DrawTextCentered(POS[k], c.x + c.width / 2, c.y + c.height - 26, 13, Color{120, 80, 60, 255});
        DrawWrapped(g.reading.text[k], {c.x - 20, c.y + c.height + 12, c.width + 40, 80}, 14, INK);
    }
    if (g.haveReading && p.visionsT > 0) DrawTextCentered("(The absinthe lets you see what she sees: the marks over everyone's heads.)", r.x + r.width / 2, r.y + 440, 14, Color{200, 190, 255, 255});
    if (p.fortuneAsked && g.haveReading && p.st == no::State::Active) { if (Btn({r.x + r.width / 2 + 130, r.y + r.height - 100, 220, 40}, "Walk her home", true, 16)) p.in.fortuneYes = true; }
    if (Btn({r.x + r.width / 2 - 110, r.y + r.height - 100, 220, 40}, TextFormat("A reading (%d)", d.fortuneCost), p.money >= d.fortuneCost)) p.in.gameAct = 1;
    if (Btn({r.x + r.width / 2 - 80, r.y + r.height - 50, 160, 36}, "Thank her") || IsKeyPressed(KEY_ESCAPE)) p.in.gameAct = 3;
}

}  // namespace

void Reset() { U = UiState{}; }
void DebugPrepare(int kind) {
    if (kind == no::GK_FORTUNE) U.revealT = -100;   // (the key check resets it; see Fortune: a negative start is pushed past every flip)
    if (kind == no::GK_SCRATCH) { for (auto& m : U.mask) for (int i = 0; i < 160; i++) m[i] = (i % 16) < 9 ? 0.0f : 1.0f; U.masked = true; }
}
bool MenuOpen() { return U.menuKind >= 0; }
void OpenMenu(int kind, int machine) { U.menuKind = kind; U.menuMachine = machine; U.oppSel = 0; U.stakeSel = 0; }
void CloseMenu() { U.menuKind = -1; }
bool Blocking(const no::Player& p) { return U.menuKind >= 0 || p.game.kind >= 0; }
const char* Prompt(const no::Night& n, const no::Player& p, int kind) {
    switch (kind) {
        case no::GK_DARTS: return "E: play darts";
        case no::GK_POOL: return "E: play pool";
        case no::GK_GOLF: return n.Hour() >= no::GD().golfCloses ? "The course is closed" : "E: play nine holes";
        case no::GK_SLOTS: return "E: play the slot machine";
        case no::GK_SCRATCH: return "E: buy a scratch-off";
        case no::GK_FORTUNE: return "E: have your fortune read";
        case no::GK_PIP: return "E: buy a scratch-off from Pip";
        case no::GK_POKER: return "E: sit down at hold'em (buy-in 50)";
        case no::GK_BULLSHIT: return "E: play bullshit (20 a hand)";
    }
    (void)p; return nullptr;
}
void Frame(no::Night& n, no::Player& p, float dt) {
    U.t += dt;
    if (p.game.kind < 0) { if (U.menuKind >= 0) { DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.35f)); DrawMenu(n, p); } return; }
    U.menuKind = -1;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f));
    switch (p.game.kind) {
        case no::GK_DARTS: Darts(n, p); break;
        case no::GK_POOL: Pool(n, p); break;
        case no::GK_GOLF: Golf(n, p); break;
        case no::GK_SLOTS: Slots(n, p); break;
        case no::GK_SCRATCH: case no::GK_PIP: Scratch(n, p); break;
        case no::GK_FORTUNE: Fortune(n, p); break;
        case no::GK_DANCE: Dance(n, p); break;
        case no::GK_POKER: Poker(n, p); break;
        case no::GK_BULLSHIT: Bullshit(n, p); break;
    }
    Caption(p.game);
}

} // namespace nog
