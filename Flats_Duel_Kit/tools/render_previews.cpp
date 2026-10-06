// Renders the kit's art assets to PNG (a hidden raylib window, off-screen targets): every dealer skin, their
// expressions, emotes and tells, the duel's icons, and layout mockups of the duel's screens (the table, the draft,
// the setup plates, the sideboard). Usage: render_previews <out dir>
#include "flats_duel_art.h"
#include "flats_duel.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace flats;
using namespace flats::duel;

static std::string gOut = "previews";
static const Color BG{14, 16, 22, 255}, PAPER{226, 214, 186, 255}, BRASS{196, 160, 84, 255}, BRASS_DK{110, 82, 44, 255}, DIM{150, 150, 140, 255};

static void Save(RenderTexture2D rt, const char* name) {
    Image im = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&im);
    ExportImage(im, (gOut + "/" + name).c_str());
    UnloadImage(im);
    TraceLog(LOG_INFO, "wrote %s", name);
}
static void Label(const char* s, float cx, float y, int size, Color col) { int w = MeasureText(s, size); DrawText(s, (int)(cx - w / 2.0f), (int)y, size, col); }
static void Backdrop(Rectangle r, bool table) {   // a dim salon wall, a candle's warmth, and the table's near edge
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, Color{22, 26, 36, 255}, Color{10, 12, 16, 255});
    DrawCircleGradient((int)(r.x + r.width * 0.82f), (int)(r.y + r.height * 0.35f), r.width * 0.35f, Color{120, 80, 40, 60}, Color{0, 0, 0, 0});
    DrawCircleGradient((int)(r.x + r.width * 0.12f), (int)(r.y + r.height * 0.3f), r.width * 0.3f, Color{40, 90, 130, 50}, Color{0, 0, 0, 0});
    if (table) DrawRectangleGradientV((int)r.x, (int)(r.y + r.height * 0.8f), (int)r.width, (int)(r.height * 0.2f), Color{70, 48, 34, 255}, Color{36, 24, 18, 255});
}

// a stand-in card face (the real game draws DrawCardFace from flats.cpp)
static void MockCard(Rectangle r, const Card& c, bool faceUp) {
    DrawRectangleRounded(r, 0.08f, 6, Color{8, 6, 6, 255});
    Rectangle in{r.x + 3, r.y + 3, r.width - 6, r.height - 6};
    if (!faceUp) {
        DrawRectangleRounded(in, 0.08f, 6, Color{60, 44, 32, 255});
        DrawRing({in.x + in.width / 2, in.y + in.height / 2}, in.width * 0.18f, in.width * 0.24f, 0, 360, 24, BRASS_DK);
        return;
    }
    DrawRectangleRounded(in, 0.08f, 6, PAPER);
    int fs = std::max(8, (int)(r.width / 9));
    std::string n = c.name;
    while (MeasureText(n.c_str(), fs) > in.width - 6 && n.size() > 3) n.pop_back();
    DrawText(n.c_str(), (int)(in.x + 4), (int)(in.y + 4), fs, Color{30, 20, 14, 255});
    DrawRectangle((int)(in.x + 5), (int)(in.y + fs + 8), (int)(in.width - 10), (int)(in.height * 0.5f), Color{190, 176, 146, 255});
    DrawText(TextFormat("%d", c.strength), (int)(in.x + 5), (int)(in.y + in.height - fs - 5), fs + 2, Color{170, 60, 40, 255});
    DrawText(TextFormat("%d", c.hp), (int)(in.x + in.width - fs - 3), (int)(in.y + in.height - fs - 5), fs + 2, Color{40, 120, 70, 255});
    std::string cost = c.CostText();
    DrawText(cost.c_str(), (int)(in.x + in.width / 2 - MeasureText(cost.c_str(), fs - 2) / 2.0f), (int)(in.y + in.height - fs - 3), fs - 2, Color{90, 60, 40, 255});
}
// flats.cpp's board geometry (CellRect), so the mockups line up with the real table
static const float COLX[4] = {430, 570, 710, 850};
static Rectangle CellR(int r, int c) {
    float w, h, cy;
    switch (r) { case 0: w = 58; h = 82; cy = 318; break; case 1: w = 74; h = 104; cy = 394; break; case 2: w = 92; h = 128; cy = 518; break; default: w = 66; h = 92; cy = 608; break; }
    return {COLX[c] - w / 2, cy - h / 2, w, h};
}
static void Slot(Rectangle r, bool hot) {
    DrawRectangleRounded(r, 0.1f, 6, Color{8, 26, 40, 200});
    DrawRectangleRoundedLinesEx(r, 0.1f, 6, hot ? 2.5f : 1.2f, hot ? Color{90, 220, 250, 255} : Color{40, 110, 140, 200});
}
static void Plate(Rectangle r, const char* title) {
    DrawRectangleRounded(r, 0.12f, 6, Color{8, 12, 16, 220});
    DrawRectangleRoundedLinesEx(r, 0.12f, 6, 1.5f, BRASS_DK);
    if (title) DrawText(title, (int)r.x + 10, (int)r.y + 8, 14, BRASS);
}
static void Note(const char* s, float x, float y) { DrawText(s, (int)x, (int)y, 12, Color{240, 120, 90, 255}); }

// ---------------------------------------------------------------- the figure as the table frames it
// Each figure is drawn into its own small target (its hatching uses the scissor, so a page-wide clip can't hold) with
// the table's far edge across it, as in the game: flats.cpp draws DrawTable() over the dealer, so only the head,
// shoulders and the hands resting on the felt ever show. `s` is the figure's scale; the head sits at `headAt` of the cell.
// (raylib's targets don't nest, so a page first renders its cells, then composes them)
struct CellJob { float x, y; int w, h, skin; DealerPose p; float s, t, headAt; };
static RenderTexture2D FigureCell(const CellJob& j) {
    RenderTexture2D c = LoadRenderTexture(j.w, j.h);
    BeginTextureMode(c);
    ClearBackground(BG);
    Backdrop({0, 0, (float)j.w, (float)j.h}, false);
    float anchorY = j.h * j.headAt + 158 * 3.1f * j.s;      // DrawDealer's head is 158 units above its base line
    DrawDuelDealer(SkinOf(j.skin), j.p, {j.w / 2.0f, anchorY}, j.s, j.t, {j.w / 2.0f, (float)j.h}, DL_BODY);
    float tableY = anchorY - (602 - 300) * j.s;              // the table's far edge sits 302 px above the base line at scale 1
    DrawRectangleGradientV(0, (int)tableY, j.w, j.h - (int)tableY, Color{70, 48, 34, 255}, Color{30, 20, 14, 255});
    DrawLine(0, (int)tableY, j.w, (int)tableY, Color{20, 14, 10, 255});
    DrawDuelDealer(SkinOf(j.skin), j.p, {j.w / 2.0f, anchorY}, j.s, j.t, {j.w / 2.0f, (float)j.h}, DL_HANDS);   // hands on the felt
    EndTextureMode();
    return c;
}

static void Free(std::vector<RenderTexture2D>& cells);
// The opponent lays a card in each of its four front lanes (ReachPose), at the moment of the slap.
static void PageReach(float t) {
    const int CW = 640, CH = 330, W = CW * 2, H = CH * 2 + 40;
    const float s = 0.5f;
    std::vector<RenderTexture2D> cells;
    for (int lane = 0; lane < 4; lane++) {
        RenderTexture2D c = LoadRenderTexture(CW, CH);
        BeginTextureMode(c);
        ClearBackground(BG);
        Backdrop({0, 0, (float)CW, (float)CH}, false);
        Vector2 anchor{320, 301};                            // the game's {640, 602} at half scale
        const float colx[4] = {430, 570, 710, 850};
        Vector2 target{colx[lane] * s, 394 * s};             // CellCenter(R_FOE_FRONT, lane), halved
        DealerPose p = Add(IdlePose(t, 2), ReachPose(target, 0.44f, anchor, s));
        DrawDuelDealer(SkinOf(2), p, anchor, s, t, target, DL_BODY);
        float tableY = anchor.y - 302 * s;
        DrawRectangleGradientV(0, (int)tableY, CW, CH - (int)tableY, Color{70, 48, 34, 255}, Color{30, 20, 14, 255});
        for (int k = 0; k < 4; k++) {
            Rectangle q{colx[k] * s - 18.5f, 394 * s - 26, 37, 52};
            DrawRectangleRoundedLinesEx(q, 0.1f, 6, k == lane ? 2.0f : 1.0f, k == lane ? Color{90, 220, 250, 255} : Color{40, 110, 140, 200});
        }
        DrawDuelDealer(SkinOf(2), p, anchor, s, t, target, DL_HANDS);
        DrawText(TextFormat("lane %d", lane), 10, 10, 14, PAPER);
        EndTextureMode();
        cells.push_back(c);
    }
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("ReachPose: the nearer hand lays a card in the lane (drawn DL_BODY, then the table, then DL_HANDS)", W / 2.0f, 10, 18, PAPER);
    for (int i = 0; i < 4; i++) DrawTextureRec(cells[i].texture, {0, 0, (float)CW, -(float)CH}, {(float)(i % 2) * CW, 40.0f + (i / 2) * CH}, WHITE);
    EndTextureMode();
    Save(rt, "dealer_reach.png");
    UnloadRenderTexture(rt); Free(cells);
}
static void Compose(const std::vector<CellJob>& jobs, std::vector<RenderTexture2D>& out) { out.clear(); for (auto& j : jobs) out.push_back(FigureCell(j)); }
static void Blit(const std::vector<CellJob>& jobs, std::vector<RenderTexture2D>& cells) {
    for (size_t i = 0; i < jobs.size(); i++) {
        DrawTextureRec(cells[i].texture, {0, 0, (float)jobs[i].w, -(float)jobs[i].h}, {jobs[i].x, jobs[i].y}, WHITE);
        DrawRectangleLinesEx({jobs[i].x, jobs[i].y, (float)jobs[i].w, (float)jobs[i].h}, 1, Color{40, 44, 52, 255});
    }
}
static void Free(std::vector<RenderTexture2D>& cells) { for (auto& c : cells) UnloadRenderTexture(c); cells.clear(); }

// ---------------------------------------------------------------- pages
static void PageSkins(float t) {
    const int W = 1600, H = 780, CW = 320, CH = 320;
    std::vector<CellJob> jobs;
    for (int i = 0; i < SkinCount(); i++) jobs.push_back({(float)(i % 5) * CW, 44.0f + (i / 5) * (CH + 44), CW - 4, CH, i, IdlePose(t + i, i), 0.62f, t + i, 0.50f});
    std::vector<RenderTexture2D> cells; Compose(jobs, cells);
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("FLATS DUEL - opponent skins as seen across the table (DrawDuelDealer; five free, five for arcade tokens)", W / 2.0f, 12, 20, PAPER);
    Blit(jobs, cells);
    for (int i = 0; i < SkinCount(); i++) {
        float cx = (i % 5) * CW + CW / 2.0f, y = 44.0f + (i / 5) * (CH + 44) + CH + 4;
        Label(SkinOf(i).name, cx, y, 16, PAPER);
        Label(SkinOf(i).unlock, cx, y + 20, 12, DIM);
    }
    EndTextureMode();
    Save(rt, "dealer_skins.png");
    UnloadRenderTexture(rt); Free(cells);
}

static void PageExpressions(float t) {
    const int CW = 200, CH = 190, W = CW * EX_COUNT + 150, H = (CH + 6) * 5 + 56;
    std::vector<CellJob> jobs;
    for (int s = 0; s < 5; s++)
        for (int e = 0; e < EX_COUNT; e++) {
            DealerPose p = Mix(IdlePose(t, s), ExpressionPose(e), 1.0f);
            p.eyeOpen = ExpressionPose(e).eyeOpen;
            jobs.push_back({150.0f + e * CW, 50.0f + s * (CH + 6), CW - 4, CH, s, p, 0.62f, t, 0.52f});
        }
    std::vector<RenderTexture2D> cells; Compose(jobs, cells);
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("Expressions (ExpressionPose): the opponent reacts to the scales, big hits and results", W / 2.0f, 8, 18, PAPER);
    for (int e = 0; e < EX_COUNT; e++) Label(ExpressionName(e), 150 + e * CW + CW / 2.0f, 32, 14, BRASS);
    for (int s = 0; s < 5; s++) DrawText(SkinOf(s).name, 8, 50 + s * (CH + 6) + CH / 2, 13, PAPER);
    Blit(jobs, cells);
    EndTextureMode();
    Save(rt, "dealer_expressions.png");
    UnloadRenderTexture(rt); Free(cells);
}

static void PageEmotes(float t) {
    const int FR = 7, CW = 190, CH = 190, W = CW * FR + 150, H = (CH + 6) * EM_COUNT + 50;
    const int skinFor[EM_COUNT] = {0, 3, 1, 2, 3, 0};   // each on a skin it suits (the tricorn tips well)
    std::vector<CellJob> jobs;
    for (int e = 0; e < EM_COUNT; e++)
        for (int f = 0; f < FR; f++) {
            float u = (f + 0.5f) / FR;
            jobs.push_back({150.0f + f * CW, 44.0f + e * (CH + 6), CW - 4, CH, skinFor[e], Add(IdlePose(0, skinFor[e]), EmotePose(e, u)), 0.40f, t + u * EmoteOf(e).seconds, 0.30f});
        }
    std::vector<RenderTexture2D> cells; Compose(jobs, cells);
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("Emotes (EmotePose, u = 0..1 over EmoteOf(e).seconds), played by the sender's figure", W / 2.0f, 10, 18, PAPER);
    Blit(jobs, cells);
    for (int e = 0; e < EM_COUNT; e++) {
        float y = 44.0f + e * (CH + 6) + CH / 2 - 24;
        DrawText(EmoteOf(e).name, 10, (int)y, 18, BRASS);
        DrawText(TextFormat("%.1f s", EmoteOf(e).seconds), 10, (int)y + 24, 12, DIM);
        DrawText(SkinOf(skinFor[e]).name, 10, (int)y + 40, 10, DIM);
        for (int f = 0; f < FR; f++) DrawText(TextFormat("u %.2f", (f + 0.5f) / FR), 156 + f * CW, 50 + e * (CH + 6), 10, DIM);
    }
    EndTextureMode();
    Save(rt, "dealer_emotes.png");
    UnloadRenderTexture(rt); Free(cells);
}

static void PageTells(float t) {
    const int FR = 4, CW = 180, CH = 180, W = CW * FR * 2 + 30, H = (CH + 6) * 5 + 50;
    std::vector<CellJob> jobs;
    for (int s = 0; s < SkinCount(); s++)
        for (int f = 0; f < FR; f++) {
            float u = (f + 0.5f) / FR;
            jobs.push_back({10.0f + (s / 5) * (CW * FR + 10) + f * CW, 44.0f + (s % 5) * (CH + 6), CW - 4, CH, s, Add(IdlePose(t, s), TellPose(s, u)), 0.38f, t + u * 1.6f, 0.30f});
        }
    std::vector<RenderTexture2D> cells; Compose(jobs, cells);
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("Tells (TellPose): what each skin does while it thinks. Visual only - a duel has no hidden dealer rules.", W / 2.0f, 10, 18, PAPER);
    Blit(jobs, cells);
    for (int s = 0; s < SkinCount(); s++) DrawText(SkinOf(s).name, 16 + (s / 5) * (CW * FR + 10), 50 + (s % 5) * (CH + 6), 12, PAPER);
    EndTextureMode();
    Save(rt, "dealer_tells.png");
    UnloadRenderTexture(rt); Free(cells);
}


static void PageIcons(float t) {
    const int CW = 150, CH = 150, N = IC_COUNT, COLS = 7, W = CW * COLS, H = CH * ((N + COLS - 1) / COLS) + 40;
    static const char* NAMES[IC_COUNT] = {"Nod", "Sneer", "Shrug", "Tip hat", "Slow clap", "Gulp", "Draft", "Constructed", "Quick", "Reel", "Pearl (a game won)", "Sideboard swap", "Turn clock", "Draft pack"};
    RenderTexture2D rt = LoadRenderTexture(W, H);
    BeginTextureMode(rt);
    ClearBackground(BG);
    Label("Icons (DrawDuelIcon): emote wheel, mode plates, the drum's reel, pips, the clock, the pack", W / 2.0f, 10, 18, PAPER);
    for (int i = 0; i < N; i++) {
        Rectangle cell{(float)(i % COLS) * CW, 40.0f + (i / COLS) * CH, (float)CW, (float)CH};
        Plate({cell.x + 10, cell.y + 6, cell.width - 20, cell.height - 30}, nullptr);
        DrawDuelIcon(i, {cell.x + CW / 2.0f, cell.y + 62}, 88, BRASS, t);
        Label(NAMES[i], cell.x + CW / 2.0f, cell.y + CH - 20, 12, PAPER);
    }
    EndTextureMode();
    Save(rt, "duel_icons.png");
    UnloadRenderTexture(rt);
}

static void PageReel(float t) {
    RenderTexture2D rt = LoadRenderTexture(256, 256);
    BeginTextureMode(rt);
    ClearBackground({10, 54, 60, 255});   // the arcade porthole's screen colour (arcade.cpp SCREEN_BG)
    DrawDuelIcon(IC_REEL, {128, 116}, 190, Color{180, 255, 240, 255}, t);
    Label("FLATS DUEL", 128, 222, 22, Color{180, 255, 240, 255});
    EndTextureMode();
    Save(rt, "reel_flats_duel.png");
    UnloadRenderTexture(rt);
}

// The duel table as the player sees it: their side in rows 2-3, the opponent (in a skin) across.
static void PageTable(float t) {
    RenderTexture2D rt = LoadRenderTexture(1280, 720);
    BeginTextureMode(rt);
    Backdrop({0, 0, 1280, 720}, false);
    const Skin& sk = SkinOf(3);
    DealerPose p = Mix(IdlePose(t, 3), ExpressionPose(EX_AHEAD), 0.6f);
    p = Add(p, ReachPose({710, 394}, 0.44f, {640, 602}, 1.0f));   // laying a card in lane 2
    DrawDuelDealer(sk, p, {640, 602}, 1.0f, t, {640, 560}, DL_BODY);
    // the table
    for (int i = 0; i < 24; i++) {
        float v0 = i / 24.0f, v1 = (i + 1) / 24.0f;
        auto TP = [](float u, float v) { float hw = 390 + 270 * v; return Vector2{640 + u * hw, 300 + 310 * v}; };
        float sh = 0.42f + 0.5f * v0;
        Color col{(unsigned char)(70 * sh), (unsigned char)(48 * sh), (unsigned char)(34 * sh), 255};
        Vector2 a = TP(-1, v0), b = TP(1, v0), c = TP(1, v1), d = TP(-1, v1);
        DrawTriangle(a, c, b, col); DrawTriangle(a, d, c, col);
    }
    DrawLineEx({250, 452}, {1030, 452}, 1.5f, Color{60, 180, 220, 60});
    // the board: the opponent's reserve (row 0) and front (row 1); yours (rows 2, 3)
    const char* names[4][4] = {{"", "Barracuda", "", ""}, {"Hammerhead", "", "Crab Sentinel", "Sea Turtle"}, {"Sea Anemone", "Great White", "", "Ghost Crab"}, {"", "", "Fry", ""}};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            Slot(CellR(r, c), r == 2 && c == 2);
            if (names[r][c][0]) { Card k = MakeCard(CardIdByName(names[r][c])); MockCard(CellR(r, c), k, true); }
        }
    DrawDuelDealer(sk, p, {640, 602}, 1.0f, t, {640, 560}, DL_HANDS);   // after the table and the board: the hands rest on the felt and lay cards over the slots
    Note("row 0 = THEIR RESERVE (the single-player dealer's queue row)", 880, 300);
    Note("row 3 = YOUR RESERVE", 890, 620);
    // their hand: backs, and a count (never the cards)
    for (int i = 0; i < 5; i++) MockCard({930.0f + i * 14, 292.0f - (i % 2) * 3, 34, 48}, Card{}, false);
    DrawText("5 in hand", 934, 346, 11, DIM);
    // your hand
    const char* hand[6] = {"Minnow", "Ship's Cat", "Sea Serpent", "Hermit Crab", "Clownfish", "Barracuda"};
    for (int i = 0; i < 6; i++) {
        float off = i - 2.5f;
        Card k = MakeCard(CardIdByName(hand[i]));
        MockCard({640 + off * 100 - 46, 716 - fabsf(off) * 5 - 66 - (i == 2 ? 34 : 0), 92, 128}, k, true);
    }
    // the scales (left), the bell (right), bones, items, the clock, the pips, the emote wheel
    DrawRectangle(118, 250, 12, 236, BRASS_DK);
    DrawLineEx({30, 262}, {218, 238}, 8, BRASS);
    DrawText("THE HOUSE", 36, 220, 12, PAPER); DrawText("YOU", 196, 220, 12, PAPER);
    DrawText("+3 / 8", 98, 500, 14, Color{140, 230, 160, 255});
    DrawCircleV({1090, 534}, 44, BRASS); DrawText("BELL", 1074, 590, 12, PAPER);
    Plate({104, 570, 130, 64}, nullptr); DrawText("bones x 4", 120, 592, 16, PAPER);
    for (int i = 0; i < 2; i++) Plate({1184, 472.0f + i * 62, 52, 56}, nullptr);
    DrawText("items (2)", 1178, 600, 11, DIM);
    DrawTurnClock({1200, 90}, 34, 0.35f, 0.25f, t); DrawText("your turn: 21 s", 1150, 132, 12, PAPER);
    for (int k = 0; k < 2; k++) { DrawDuelIcon(IC_PEARL, {1150.0f + k * 30, 180}, 40, BRASS); DrawDuelIcon(IC_PEARL, {1150.0f + k * 30, 214}, 40, Fade(BRASS, k == 0 ? 1.0f : 0.25f)); }
    DrawText("you 1 - 1 them", 1134, 236, 11, DIM);
    for (int e = 0; e < EM_COUNT; e++) { float a = -2.4f + e * 0.3f; DrawDuelIcon(e, {70 + cosf(a) * 0 + e * 44.0f, 690}, 38, BRASS, t); }
    DrawText("emotes (6 s cooldown)", 20, 660, 11, DIM);
    Plate({500, 20, 280, 30}, nullptr); Label("THE HOUSE  -  game 2 of 3  -  round 4", 640, 28, 14, PAPER);
    DrawRectangleRounded({280, 58, 720, 26}, 0.5f, 6, Color{8, 12, 16, 190});
    Label("Pick a card, then a lane. Ring the bell to fight.", 640, 63, 16, Color{214, 222, 226, 255});
    Note("MOCKUP - layout only. Cards, bell, scales and bones are flats.cpp's own drawers in the game.", 20, 6);
    EndTextureMode();
    Save(rt, "mockup_table.png");
    UnloadRenderTexture(rt);
}

static void PageDraft(float t) {
    RenderTexture2D rt = LoadRenderTexture(1280, 720);
    BeginTextureMode(rt);
    Backdrop({0, 0, 1280, 720}, true);
    DealerPose p = Add(IdlePose(t, 1), TellPose(1, 0.5f));
    DrawDuelDealer(SkinOf(1), p, {640, 330}, 0.5f, t, {640, 500});
    Plate({340, 20, 600, 34}, nullptr); Label("OPEN DRAFT  -  pack 6 of 13  -  your pick (2 of 4)", 640, 29, 16, PAPER);
    DrawDuelIcon(IC_PACK, {300, 420}, 90, BRASS, t);
    const char* pack[3] = {"Manta Ray", "Sea Serpent", "Skeleton Sailor"};
    for (int i = 0; i < 3; i++) {
        Card k = MakeCard(CardIdByName(pack[i]));
        Rectangle r{420.0f + i * 150, 350, 130, 182};
        if (i == 1) DrawRectangleRoundedLinesEx({r.x - 6, r.y - 6, r.width + 12, r.height + 12}, 0.1f, 6, 3, Color{90, 220, 250, 255});
        MockCard(r, k, true);
    }
    DrawText("Hover: the inspector (DrawInspector). Click: PICK. Both players watch every pick.", 420, 548, 12, DIM);
    DrawTurnClock({990, 440}, 40, 0.6f, 0.25f, t); DrawText("12 s a pick", 960, 490, 12, PAPER);
    Plate({20, 90, 230, 600}, "YOUR PICKS (11)");
    const char* mine[11] = {"Great White", "Ship's Cat", "Stingray", "Ghost Crab", "Fry", "Clownfish", "Hermit Crab", "Moray Eel", "Salvage Diver", "Barracuda", "Anglerfish"};
    for (int i = 0; i < 11; i++) DrawText(mine[i], 34, 120 + i * 22, 14, PAPER);
    DrawText("blood 8  bones 2  free 1", 34, 640, 12, DIM);
    Plate({1030, 90, 230, 600}, "THEIR PICKS (11)");
    const char* theirs[11] = {"Sperm Whale", "Crab Sentinel", "Sea Turtle", "Coral Queen", "Nautilus", "Pufferfish", "Sea Urchin", "Mudskipper", "Hammerhead", "Sailfish", "Kraken Spawn"};
    for (int i = 0; i < 11; i++) DrawText(theirs[i], 1044, 120 + i * 22, 14, PAPER);
    DrawText("(public: an open draft)", 1044, 640, 12, DIM);
    Note("MOCKUP - layout only.", 20, 6);
    EndTextureMode();
    Save(rt, "mockup_draft.png");
    UnloadRenderTexture(rt);
}

static void PageSetup(float t) {
    RenderTexture2D rt = LoadRenderTexture(1280, 720);
    BeginTextureMode(rt);
    Backdrop({0, 0, 1280, 720}, true);
    Plate({340, 18, 600, 34}, nullptr); Label("TAKE YOUR SEATS  -  table setup (PH_SETUP)", 640, 27, 16, PAPER);
    // the host's mode plates
    for (int m = 0; m < MODE_COUNT; m++) {
        Rectangle r{120.0f + m * 360, 70, 320, 150};
        Plate(r, nullptr);
        if (m == 0) DrawRectangleRoundedLinesEx(r, 0.12f, 6, 3, BRASS);
        DrawDuelIcon(IC_DRAFT + m, {r.x + 56, r.y + 70}, 80, BRASS, t);
        DrawText(RulesOf(m).name, (int)r.x + 110, (int)r.y + 20, 22, PAPER);
        int y = (int)r.y + 52;
        const char* wraps[3][3] = {{"Open draft: 13 packs of 4.", "Keep 20, sideboard 6.", "Best of three."},
                                   {"Bring a 20-30 card deck", "and a 6-card sideboard.", "Best of three."},
                                   {"Six preset decks.", "One game,", "30-second turns."}};
        for (int k = 0; k < 3; k++) DrawText(wraps[m][k], (int)r.x + 110, y + k * 20, 14, DIM);
    }
    DrawText("only the host picks the mode (SET_MODE); changing it un-readies both players", 120, 226, 12, DIM);
    // skins: a carousel of the two players' chairs
    for (int side = 0; side < 2; side++) {
        Rectangle r{120.0f + side * 540, 260, 500, 330};
        Plate(r, side == 0 ? "YOU  -  Old Ironsides" : "THEM  -  The Tidewife");
        int s = side == 0 ? 7 : 1;
        Backdrop({r.x + 10, r.y + 34, 240, 250}, true);
        DrawDuelDealer(SkinOf(s), IdlePose(t, s), {r.x + 130, r.y + 276}, 0.36f, t, {r.x + 130, r.y + 300});
        DrawText("<  skin  >", (int)r.x + 90, (int)r.y + 292, 14, BRASS);
        DrawText("Items (pick two):", (int)r.x + 270, (int)r.y + 44, 14, PAPER);
        const char* items[6] = {"Boulder in a Bottle", "Black Goat Bottle", "Fishhook", "Squid Ink", "Barnacle Bandage", "Harpoon"};
        for (int k = 0; k < 6; k++) {
            bool on = side == 0 ? (k == 1 || k == 5) : false;
            DrawRectangleRoundedLinesEx({r.x + 270, r.y + 68.0f + k * 30, 210, 24}, 0.3f, 4, on ? 2.5f : 1, on ? BRASS : BRASS_DK);
            DrawText(side == 0 ? items[k] : "(hidden)", (int)r.x + 280, (int)r.y + 73 + k * 30, 13, on ? PAPER : DIM);
        }
        DrawText(side == 0 ? "ready lever: DOWN" : "ready lever: UP (ready)", (int)r.x + 270, (int)r.y + 262, 14, side == 0 ? DIM : Color{140, 230, 160, 255});
    }
    DrawText("Their items and deck stay hidden (Serialize sends only skin, ready and submitted).", 120, 604, 13, DIM);
    Note("MOCKUP - layout only.", 20, 6);
    EndTextureMode();
    Save(rt, "mockup_setup.png");
    UnloadRenderTexture(rt);
}

static void PageSideboard(float t) {
    RenderTexture2D rt = LoadRenderTexture(1280, 720);
    BeginTextureMode(rt);
    Backdrop({0, 0, 1280, 720}, true);
    Plate({340, 18, 600, 34}, nullptr); Label("BETWEEN GAMES  -  you lead 1-0  -  3 swaps left  -  45 s", 640, 27, 16, PAPER);
    std::vector<Card> deck = WithStaples(PresetDeck(1)), side = PresetDeck(4);
    side.resize(6);
    Plate({30, 70, 900, 610}, "YOUR DECK (23)");
    for (int i = 0; i < (int)deck.size(); i++) MockCard({50.0f + (i % 8) * 108, 100.0f + (i / 8) * 150, 96, 134}, deck[i], true);
    Plate({950, 70, 300, 610}, "SIDEBOARD (6)");
    for (int i = 0; i < 6; i++) MockCard({970.0f + (i % 2) * 140, 100.0f + (i / 2) * 150, 120, 134}, side[i], true);
    DrawDuelIcon(IC_SIDEBOARD, {940, 380}, 60, BRASS, t);
    DrawText("click a deck card, then a sideboard card: SWAP (a, b)", 40, 686, 13, DIM);
    DrawText("READY when done", 1050, 686, 13, BRASS);
    Note("MOCKUP - layout only.", 20, 6);
    EndTextureMode();
    Save(rt, "mockup_sideboard.png");
    UnloadRenderTexture(rt);
}

int main(int argc, char** argv) {
    if (argc >= 2) gOut = argv[1];
    SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(320, 200, "duel previews");
    const float t = 1.3f;
    PageSkins(t); PageExpressions(t); PageEmotes(t); PageTells(t); PageIcons(t); PageReel(t);
    PageReach(t); PageTable(t); PageDraft(t); PageSetup(t); PageSideboard(t);
    CloseWindow();
    return 0;
}
