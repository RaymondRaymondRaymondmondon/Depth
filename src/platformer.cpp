// ============================================================================
//  DEPTH - the Pipes: a platformer built from shuffled, hand-designed chunks.
//
//  Chunk legend:  # pipe wall   = pipe platform   x steam vent (hazard)
//                 o coin        r rat             S start      E exit valve
//  Every chunk is 20 tiles wide and 16 tall, with solid floor at its left
//  and right edges so any chunk can follow any other.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

static const int T = 32;           // tile size in pixels
static const int CH_W = 20, CH_H = 16;
static const float PW = 22, PH = 28; // player size
static const float RW = 24, RH = 16; // rat size
static const float GRAVITY = 1900, JUMP_V = 640, RUN = 270, RAT_SPEED = 60;

#define EMPTY "...................."
static const char* CHUNK_START[CH_H] = {
    "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
    ".........o.o.o......",
    "........=====.......",
    ".S..................",
    "####################",
    "####################",
};
static const char* CHUNK_END[CH_H] = {
    "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
    "...............E....",
    "####################",
    "####################",
};
static const char* CHUNKS[5][CH_H] = {
    {   // A: pits
        "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        "......o.......o.....",
        EMPTY,
        "....===......===....",
        EMPTY,
        "###....######....###",
        "###....######....###",
    },
    {   // B: steam vents and rats
        "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        "........ooo.........",
        EMPTY,
        ".......=====........",
        "...r..........r.....",
        "#####xx####xx#######",
        "####################",
    },
    {   // C: staircase over a long pit
        "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        "..........o.........",
        ".........===........",
        EMPTY,
        "......===......o....",
        "..............===...",
        "...===..............",
        EMPTY,
        "###............#####",
        "###............#####",
    },
    {   // D: low tunnel with a rat (or take the roof)
        "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        "....o...o...o.......",
        "...##############...",
        EMPTY,
        "......r...o.o.o.....",
        "####################",
        "####################",
    },
    {   // E: vent gauntlet
        "####################", EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
        "...o....o....o......",
        "..===..===..===.....",
        EMPTY,
        "##xxxxxxxxxxxxxx####",
        "####################",
    },
};
#undef EMPTY

void GeneratePipesLayout(Game& g) {
    std::vector<int> idx = {0, 1, 2, 3, 4};
    for (int i = (int)idx.size() - 1; i > 0; i--) std::swap(idx[i], idx[GetRandomValue(0, i)]);
    idx.resize(4);
    g.plat.pipesLayout = idx;
}

std::string PipesLayoutCode(const Game& g) {
    std::string s;
    for (int c : g.plat.pipesLayout) { if (!s.empty()) s += "-"; s += (char)('A' + c); }
    return s.empty() ? "(new)" : s;
}

static bool Solid(const PlatformState& p, int tx, int ty) {
    if (tx < 0 || tx >= p.w) return true; // level edges act as walls
    if (ty < 0 || ty >= p.h) return false;
    char c = p.tiles[ty][tx];
    return c == '#' || c == '=';
}

// Moves a box one axis at a time and pushes it out of solid tiles.
static void MoveAndCollide(const PlatformState& p, Vector2& pos, Vector2& vel, float w, float h, float dt, bool& grounded, bool& hitWall) {
    grounded = false;
    hitWall = false;
    pos.x += vel.x * dt;
    int y0 = (int)floorf(pos.y / T), y1 = (int)floorf((pos.y + h - 0.01f) / T);
    if (vel.x > 0) {
        int tx = (int)floorf((pos.x + w - 0.01f) / T);
        for (int ty = y0; ty <= y1; ty++) if (Solid(p, tx, ty)) { pos.x = tx * (float)T - w; vel.x = 0; hitWall = true; break; }
    } else if (vel.x < 0) {
        int tx = (int)floorf(pos.x / T);
        for (int ty = y0; ty <= y1; ty++) if (Solid(p, tx, ty)) { pos.x = (tx + 1) * (float)T; vel.x = 0; hitWall = true; break; }
    }
    pos.y += vel.y * dt;
    int x0 = (int)floorf(pos.x / T), x1 = (int)floorf((pos.x + w - 0.01f) / T);
    if (vel.y > 0) {
        int ty = (int)floorf((pos.y + h - 0.01f) / T);
        for (int tx = x0; tx <= x1; tx++) if (Solid(p, tx, ty)) { pos.y = ty * (float)T - h; vel.y = 0; grounded = true; break; }
    } else if (vel.y < 0) {
        int ty = (int)floorf(pos.y / T);
        for (int tx = x0; tx <= x1; tx++) if (Solid(p, tx, ty)) { pos.y = (ty + 1) * (float)T; vel.y = 0; break; }
    }
}

static Vector2 SpawnPoint(const PlatformState& p) {
    if (p.checkpointChunk == 0) return {1.0f * T + 4, 14.0f * T - PH};
    return {p.checkpointChunk * CH_W * (float)T + 6, 14.0f * T - PH};
}

static void BuildLevel(PlatformState& p) {
    std::vector<const char**> parts;
    parts.push_back(CHUNK_START);
    for (int c : p.pipesLayout) parts.push_back(CHUNKS[c]);
    parts.push_back(CHUNK_END);
    p.tiles.assign(CH_H, "");
    for (auto part : parts)
        for (int r = 0; r < CH_H; r++) p.tiles[r] += part[r];
    p.w = (int)p.tiles[0].size();
    p.h = CH_H;
    p.rats.clear();
    for (int r = 0; r < p.h; r++)
        for (int c = 0; c < p.w; c++) {
            char& ch = p.tiles[r][c];
            if (ch == 'S') { p.pos = {c * (float)T + 4, (r + 1) * (float)T - PH}; ch = '.'; }
            else if (ch == 'r') { p.rats.push_back({{c * (float)T + 4, (r + 1) * (float)T - RH}, -1, 0, true}); ch = '.'; }
        }
}

void StartPipes(Game& g) {
    if (g.plat.pipesLayout.empty()) GeneratePipesLayout(g);
    std::vector<int> layout = g.plat.pipesLayout;
    g.plat = PlatformState{};
    g.plat.pipesLayout = layout;
    g.plat.layoutCode = PipesLayoutCode(g);
    BuildLevel(g.plat);
    g.scene = Scene::Platformer;
}

static void Die(PlatformState& p) {
    p.deaths++;
    p.pos = SpawnPoint(p);
    p.vel = {0, 0};
    p.deathFlash = 0.45f;
}

// ---------------------------------------------------------------- drawing
static void DrawTile(char c, int x, int y, float t) {
    float px = x * (float)T, py = y * (float)T;
    switch (c) {
        case '#':
            DrawRectangle((int)px, (int)py, T, T, Color{150, 106, 52, 255});
            DrawRectangleLines((int)px, (int)py, T, T, Color{104, 72, 36, 255});
            DrawCircle((int)px + 6, (int)py + 6, 2, Color{226, 186, 116, 255});
            DrawCircle((int)px + T - 6, (int)py + 6, 2, Color{226, 186, 116, 255});
            break;
        case '=':
            DrawRectangle((int)px, (int)py, T, T, Pal::Copper);
            DrawRectangle((int)px, (int)py + 4, T, 5, Color{232, 156, 114, 255});
            DrawRectangle((int)px, (int)py + T - 6, T, 6, Color{120, 60, 36, 255});
            if (x % 3 == 0) DrawRectangle((int)px, (int)py - 2, 6, T + 4, Color{110, 76, 42, 255});
            break;
        case 'x':
            DrawRectangle((int)px, (int)py + T - 12, T, 12, Color{64, 64, 70, 255});
            for (int k = 0; k < 4; k++) DrawRectangle((int)px + 3 + k * 8, (int)py + T - 10, 4, 8, Color{30, 30, 34, 255});
            for (int k = 0; k < 3; k++) {
                float ph = fmodf(t * 1.6f + k * 0.33f + x * 0.17f, 1.0f);
                DrawCircle((int)(px + T / 2 + sinf(ph * 6 + k) * 6), (int)(py + T - 8 - ph * 46), 6 + ph * 9,
                           Fade(Color{240, 245, 250, 255}, 0.7f * (1 - ph)));
            }
            break;
        case 'o': {
            float bob = sinf(t * 4 + x) * 3;
            DrawCircle((int)px + T / 2, (int)(py + T / 2 + bob), 9, Color{250, 210, 70, 255});
            DrawCircle((int)px + T / 2 - 3, (int)(py + T / 2 - 3 + bob), 3, Color{255, 246, 196, 255});
        } break;
        case 'E': {
            Vector2 c{px + T / 2, py - 10};
            DrawRectangle((int)px + 10, (int)py - 10, 12, T + 10, Pal::BrassDk);
            float a = t * 1.5f;
            for (int k = 0; k < 4; k++) {
                float ang = a + k * PI / 2;
                DrawLineEx(c, {c.x + cosf(ang) * 26, c.y + sinf(ang) * 26}, 4, Pal::Coral);
            }
            DrawRing(c, 22, 28, 0, 360, 36, Pal::Bad);
            DrawCircleV(c, 7, Pal::Brass);
            DrawRectangle((int)px + 30, (int)py + 8, 34, 24, Color{120, 76, 40, 255});
            DrawRectangle((int)px + 30, (int)py + 8, 34, 8, Pal::Brass);
        } break;
        default: break;
    }
}

static void DrawPlayer(const PlatformState& p, float t) {
    float x = p.pos.x, y = p.pos.y;
    bool moving = fabsf(p.vel.x) > 20 && p.onGround;
    float step = moving ? sinf(t * 18) * 3 : 0;
    DrawRectangle((int)x + 3, (int)(y + PH - 8), 7, 8 + (int)step, Color{50, 60, 70, 255});
    DrawRectangle((int)x + 12, (int)(y + PH - 8), 7, 8 - (int)step, Color{50, 60, 70, 255});
    DrawRectangleRounded({x, y + 10, PW, PH - 16}, 0.3f, 4, Pal::Teal);
    DrawCircle((int)(x + PW / 2), (int)y + 8, 11, Pal::Brass);
    float wx = x + PW / 2 + (p.facingRight ? 3.f : -3.f);
    DrawCircle((int)wx, (int)y + 8, 6, Color{40, 90, 110, 255});
    DrawCircle((int)wx - 1, (int)y + 6, 2, Color{190, 235, 245, 255});
}

void ScenePlatformer(Game& g) {
    auto& p = g.plat;
    float dt = std::min(GetFrameTime(), 1.0f / 30.0f);

    // ---------------- update
    if (!p.finished) {
        p.time += dt;
        if (IsKeyPressed(KEY_ESCAPE)) {
            g.scene = Scene::Periscope;
            Toast(g, "Run abandoned. The pipes keep their coins.");
            return;
        }
        float dir = 0;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dir += 1;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) dir -= 1;
        if (dir != 0) p.facingRight = dir > 0;
        float accel = p.onGround ? 2600.f : 1700.f, target = dir * RUN;
        if (p.vel.x < target) p.vel.x = std::min(target, p.vel.x + accel * dt);
        else if (p.vel.x > target) p.vel.x = std::max(target, p.vel.x - accel * dt);

        bool jumpPressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP);
        bool jumpReleased = IsKeyReleased(KEY_SPACE) || IsKeyReleased(KEY_W) || IsKeyReleased(KEY_UP);
        p.coyote = p.onGround ? 0.1f : p.coyote - dt;               // a moment of grace after leaving a ledge
        p.jumpBuffer = jumpPressed ? 0.12f : p.jumpBuffer - dt;     // jump pressed just before landing still counts
        if (p.jumpBuffer > 0 && p.coyote > 0) { p.vel.y = -JUMP_V; p.jumpBuffer = 0; p.coyote = 0; }
        if (jumpReleased && p.vel.y < 0) p.vel.y *= 0.45f;          // short hop when the button is released early
        p.vel.y = std::min(p.vel.y + GRAVITY * dt, 900.0f);

        float prevBottom = p.pos.y + PH;
        bool wall;
        MoveAndCollide(p, p.pos, p.vel, PW, PH, dt, p.onGround, wall);

        int chunk = (int)((p.pos.x + PW / 2) / (CH_W * T));
        if (p.onGround && chunk > p.checkpointChunk && chunk <= (int)p.pipesLayout.size()) p.checkpointChunk = chunk;

        bool dead = p.pos.y > p.h * T + 60;
        int x0 = (int)floorf((p.pos.x + 3) / T), x1 = (int)floorf((p.pos.x + PW - 3) / T);
        int y0 = (int)floorf((p.pos.y + 3) / T), y1 = (int)floorf((p.pos.y + PH - 3) / T);
        for (int ty = std::max(0, y0); ty <= std::min(p.h - 1, y1); ty++)
            for (int tx = std::max(0, x0); tx <= std::min(p.w - 1, x1); tx++) {
                char& c = p.tiles[ty][tx];
                if (c == 'o') { c = '.'; p.coins++; }
                else if (c == 'x') dead = true;
                else if (c == 'E' && !p.finished) {
                    p.finished = true;
                    p.reward = p.coins * 2 + 25;
                    g.gold += p.reward;
                    GeneratePipesLayout(g); // beaten: a fresh layout next time
                }
            }

        for (auto& r : p.rats) {
            if (!r.alive) continue;
            Vector2 vel{r.dir * RAT_SPEED, std::min(r.vy + GRAVITY * dt, 900.0f)};
            bool grounded, hitWall;
            MoveAndCollide(p, r.pos, vel, RW, RH, dt, grounded, hitWall);
            r.vy = vel.y;
            if (hitWall) r.dir = -r.dir;
            else if (grounded) {
                float fx = r.dir > 0 ? r.pos.x + RW + 2 : r.pos.x - 2;
                if (!Solid(p, (int)floorf(fx / T), (int)floorf((r.pos.y + RH + 2) / T))) r.dir = -r.dir;
            }
            if (r.pos.y > p.h * T + 60) r.alive = false;
            Rectangle pr{p.pos.x, p.pos.y, PW, PH}, rr{r.pos.x, r.pos.y, RW, RH};
            if (!dead && CheckCollisionRecs(pr, rr)) {
                if (p.vel.y > 0 && prevBottom <= r.pos.y + 8) { r.alive = false; p.vel.y = -420; }
                else dead = true;
            }
        }
        if (dead && !p.finished) Die(p);
    }

    // ---------------- draw
    // The Pipes are deliberately retro: the world is drawn at half resolution into a small
    // canvas, then scaled up without smoothing so every shape turns into crisp pixels.
    float t = g.time;
    SetPost(0.2f, 0.0f, 0.15f);
    const float PX = (float)SCREEN_W / PIXEL_W; // screen pixels per canvas pixel
    const float ZOOM = 1.25f / PX;
    float levelW = (float)p.w * T;
    float halfView = PIXEL_W / 2.0f / ZOOM;
    float camX = std::clamp(p.pos.x + PW / 2, halfView, levelW - halfView);
    camX = roundf(camX * ZOOM) / ZOOM; // snap to whole canvas pixels so tiles don't shimmer
    BeginLayer(PixelRT());
    DrawRectangleGradientV(0, 0, PIXEL_W, PIXEL_H, Color{70, 56, 46, 255}, Color{26, 32, 38, 255});
    for (int i = 0; i < 12; i++) {
        float x = (fmodf(i * 260.0f - camX * 0.3f + 4000, 3120) - 200) / PX;
        DrawRectangle((int)x, 30, 23, PIXEL_H, Color{90, 70, 54, 255});
        DrawRectangle((int)x, 30, 3, PIXEL_H, Color{112, 88, 66, 255});
        DrawRectangle((int)x - 3, 100 + (i % 3) * 60, 29, 7, Color{110, 84, 60, 255});
    }
    Camera2D cam{};
    cam.zoom = ZOOM;
    cam.offset = {PIXEL_W / 2.0f, (60 + (SCREEN_H - 60) / 2.0f) / PX};
    cam.target = {camX, p.h * T / 2.0f};
    BeginMode2D(cam);
    int c0 = std::max(0, (int)((camX - halfView) / T) - 1), c1 = std::min(p.w - 1, (int)((camX + halfView) / T) + 1);
    for (int y = 0; y < p.h; y++)
        for (int x = c0; x <= c1; x++) DrawTile(p.tiles[y][x], x, y, t);
    for (auto& r : p.rats) {
        if (!r.alive) continue;
        float cx = r.pos.x + RW / 2, cy = r.pos.y + RH / 2 + 2;
        DrawLineEx({cx - r.dir * 10, cy + 2}, {cx - r.dir * 24, cy - 4 + sinf(t * 8) * 3}, 2, Color{200, 150, 150, 255});
        DrawEllipse((int)cx, (int)cy, RW / 2, RH / 2, Color{120, 116, 124, 255});
        DrawCircle((int)(cx + r.dir * 8), (int)cy - 4, 2, Pal::Bad);
    }
    DrawPlayer(p, t);
    EndMode2D();
    EndLayer();
    DrawTexturePro(PixelRT().texture, {0, 0, (float)PIXEL_W, -(float)PIXEL_H}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);

    if (p.deathFlash > 0) {
        p.deathFlash -= dt;
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Pal::Bad, p.deathFlash));
    }

    DrawRectangle(0, 0, SCREEN_W, 56, Color{16, 30, 40, 235});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    Txt(TextFormat("THE PIPES   layout %s", p.layoutCode.c_str()), 20, 16, 24, Pal::Brass);
    DrawCircle(470, 28, 10, Color{250, 210, 70, 255});
    Txt(TextFormat("x %d", p.coins), 488, 17, 24, Pal::Paper);
    Txt(TextFormat("Checkpoint %d/%d", p.checkpointChunk + 1, (int)p.pipesLayout.size() + 1), 580, 18, 20, Pal::Paper);
    Txt(TextFormat("Deaths %d", p.deaths), 790, 18, 20, Pal::Paper);
    Txt(TextFormat("%.1fs", p.time), 920, 18, 20, Pal::Paper);
    Txt("Esc: give up", 1130, 20, 18, Color{190, 200, 200, 255});

    if (p.finished) {
        Rectangle panel{390, 200, 500, 280};
        Panel(panel);
        DrawTextCentered("Valve reached!", panel.x + panel.width / 2, panel.y + 26, 36, Pal::Good);
        DrawTextCentered(TextFormat("%d coins x 2  +  25 bonus  =  %d gold", p.coins, p.reward), panel.x + panel.width / 2, panel.y + 92, 22, Pal::Ink);
        DrawTextCentered(TextFormat("Time %.1fs    Deaths %d", p.time, p.deaths), panel.x + panel.width / 2, panel.y + 130, 20, Pal::BrassDk);
        DrawTextCentered("The pipes will be rearranged for your next run.", panel.x + panel.width / 2, panel.y + 166, 18, Pal::BrassDk);
        if (Button({panel.x + 100, panel.y + 206, 300, 48}, "Back to the periscope")) g.scene = Scene::Periscope;
    }
}
