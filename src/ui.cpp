// ============================================================================
//  DEPTH - small immediate-mode UI helpers
// ============================================================================
#include "game.h"
#include <cmath>

static const Font& F(bool bold) { return bold ? BoldFont() : BodyFont(); }
static float Spacing(int size) { return size * 0.02f; }

int MeasureTxt(const std::string& s, int size, bool bold) {
    return (int)MeasureTextEx(F(bold), s.c_str(), (float)size, Spacing(size)).x;
}

void Txt(const std::string& s, float x, float y, int size, Color c) {
    DrawTextEx(F(false), s.c_str(), {roundf(x), roundf(y)}, (float)size, Spacing(size), c);
}

void TxtBold(const std::string& s, float x, float y, int size, Color c) {
    DrawTextEx(F(true), s.c_str(), {roundf(x), roundf(y)}, (float)size, Spacing(size), c);
}

void TxtShadow(const std::string& s, float x, float y, int size, Color c, bool bold) {
    Color sh{0, 0, 0, (unsigned char)(c.a * 0.7f)};
    if (bold) { TxtBold(s, x + 1, y + 2, size, sh); TxtBold(s, x, y, size, c); }
    else { Txt(s, x + 1, y + 2, size, sh); Txt(s, x, y, size, c); }
}

void DrawTextCentered(const std::string& s, float cx, float y, int size, Color c) {
    Txt(s, cx - MeasureTxt(s, size) / 2.0f, y, size, c);
}

void DrawTextCenteredBold(const std::string& s, float cx, float y, int size, Color c) {
    TxtBold(s, cx - MeasureTxt(s, size, true) / 2.0f, y, size, c);
}

// Buttons are plates of dark iron with brass rivets; hover warms the edge, pressing sinks the plate.
bool Button(Rectangle r, const char* text, bool enabled, int fontSize) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color iron = !enabled ? Color{46, 46, 44, 255} : hover ? Color{86, 70, 52, 255} : Color{58, 56, 54, 255};
    Color edge = !enabled ? Color{70, 66, 58, 255} : hover ? Color{214, 164, 78, 255} : Color{140, 104, 52, 255};
    DrawRectangleRounded({r.x + 1, r.y + 4, r.width, r.height}, 0.12f, 4, Fade(BLACK, 0.6f));
    Rectangle b = down ? Rectangle{r.x, r.y + 1, r.width, r.height} : r;
    DrawRectangleRounded(b, 0.12f, 4, Color{8, 8, 10, 255});                                     // the black ink outline
    DrawRectangleRounded({b.x + 2, b.y + 2, b.width - 4, b.height - 4}, 0.12f, 4, edge);          // a worn brass rim
    DrawRectangleRounded({b.x + 4, b.y + 4, b.width - 8, b.height - 8}, 0.1f, 4, iron);           // the iron plate
    DrawRectangle((int)b.x + 6, (int)(b.y + b.height - 9), (int)b.width - 12, 4, Fade(BLACK, 0.55f)); // a hard shadow band along the bottom
    for (int k = 0; k < 2; k++) { // rivets
        float rx = k ? b.x + b.width - 9 : b.x + 9;
        DrawCircleV({rx, b.y + b.height / 2}, 2.6f, Color{10, 10, 12, 255});
        DrawCircleV({rx - 0.5f, b.y + b.height / 2 - 0.5f}, 1.6f, enabled ? Color{206, 168, 96, 255} : Color{110, 104, 92, 255});
    }
    int fs = fontSize;
    int tw = MeasureTxt(text, fs, true);
    while (tw > r.width - 30 && fs > 10) tw = MeasureTxt(text, --fs, true);
    float tx = b.x + (b.width - tw) / 2, ty = b.y + (b.height - fs) / 2 - 2;
    Color tc = !enabled ? Color{120, 116, 104, 255} : hover ? Color{255, 240, 200, 255} : Color{226, 214, 186, 255};
    TxtBold(text, tx, ty + 2, fs, Color{0, 0, 0, 230});
    TxtBold(text, tx, ty, fs, tc);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// A stained parchment sheet in a frame of dark wood and riveted iron, its corners bracketed and its edges
// darkened with age and salt.
void Panel(Rectangle r, Color fill) {
    DrawRectangleRounded({r.x + 5, r.y + 8, r.width, r.height}, 0.02f, 4, Fade(BLACK, 0.55f));
    DrawRectangleRounded({r.x - 8, r.y - 8, r.width + 16, r.height + 16}, 0.02f, 4, Color{6, 6, 8, 255});           // ink outline
    DrawTiled(Tex::Wood, {r.x - 6, r.y - 6, r.width + 12, r.height + 12}, 1.0f, Color{62, 40, 28, 255}, {r.x * 0.3f, r.y * 0.3f}); // dark stained wood
    DrawRectangleLinesEx({r.x - 6, r.y - 6, r.width + 12, r.height + 12}, 1.5f, Color{20, 12, 8, 255});
    DrawRectangleLinesEx({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 3, Color{50, 50, 52, 255});                    // an iron band
    DrawRectangleLinesEx({r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 1, Color{104, 100, 94, 255});
    Color aged = {(unsigned char)(fill.r * 0.86f), (unsigned char)(fill.g * 0.82f), (unsigned char)(fill.b * 0.72f), fill.a};
    DrawTiled(Tex::Paper, r, 1.0f, aged, {r.x * 0.7f, r.y * 0.7f});
    float e = std::fmin(26.0f, std::fmin(r.width, r.height) * 0.24f);
    Color edge = Fade(Color{50, 32, 18, 255}, fill.a / 255.0f * 0.5f), none = Fade(edge, 0);
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)e, edge, none);
    DrawRectangleGradientV((int)r.x, (int)(r.y + r.height - e), (int)r.width, (int)e, none, edge);
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)e, (int)r.height, edge, none);
    DrawRectangleGradientH((int)(r.x + r.width - e), (int)r.y, (int)e, (int)r.height, none, edge);
    for (int k = 0; k < 5; k++) { // grime and salt blotches, fixed by position so they don't crawl
        float bx = r.x + fmodf(r.x * 7.3f + k * 131.7f + r.y * 3.1f, r.width), by = r.y + fmodf(r.y * 5.9f + k * 97.3f + r.x * 1.7f, r.height);
        DrawEllipse((int)bx, (int)by, 14 + k * 4, 6 + k * 2, Fade(Color{60, 40, 24, 255}, 0.08f));
    }
    if (r.width > 120 && r.height > 60) {
        for (float x = r.x + 26; x < r.x + r.width - 20; x += 46) { // rivets along the iron band, top and bottom
            for (float y : {r.y - 4.0f, r.y + r.height + 4.0f}) {
                DrawCircleV({x, y}, 3.2f, Color{10, 10, 12, 255});
                DrawCircleV({x - 0.6f, y - 0.6f}, 2.0f, Color{170, 130, 66, 255});
            }
        }
        for (Vector2 c : {Vector2{r.x - 4, r.y - 4}, Vector2{r.x + r.width + 4, r.y - 4}, Vector2{r.x - 4, r.y + r.height + 4}, Vector2{r.x + r.width + 4, r.y + r.height + 4}}) {
            DrawCircleV(c, 7.5f, Color{10, 10, 12, 255});      // heavy corner brackets
            DrawCircleV(c, 6, Color{70, 66, 62, 255});
            DrawCircleV({c.x - 1, c.y - 1}, 3, Color{190, 146, 76, 255});
        }
    }
}
void DrawWrapped(const std::string& text, Rectangle r, int size, Color c) {
    float x = r.x, y = r.y, lineH = size * 1.3f;
    int space = MeasureTxt(" ", size) + 2;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return;
        int w = MeasureTxt(word, size);
        if (x + w > r.x + r.width && x > r.x) { x = r.x; y += lineH; }
        Txt(word, x, y, size, c);
        x += w + space;
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flush();
        else if (ch == '\n') { flush(); x = r.x; y += lineH; }
        else word += ch;
    }
    flush();
}

void DrawBar(Rectangle r, float frac, Color fill) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    DrawRectangleRec({r.x - 1, r.y - 1, r.width + 2, r.height + 2}, Color{20, 16, 12, 230});
    DrawRectangleRec({r.x, r.y, r.width * frac, r.height}, fill);
    DrawRectangleRec({r.x, r.y, r.width * frac, std::fmax(1.0f, r.height * 0.35f)}, Fade(WHITE, 0.25f));
}

void Toast(Game& g, const std::string& msg) {
    g.toast = msg;
    g.toastTimer = 2.8f;
}

void DrawToast(Game& g) {
    if (g.toastTimer <= 0) return;
    g.toastTimer -= GetFrameTime();
    int w = MeasureTxt(g.toast, 21) + 48;
    Rectangle r{(SCREEN_W - w) / 2.0f, SCREEN_H - 128.0f, (float)w, 44};
    Panel(r, Color{255, 248, 230, 245});
    DrawTextCentered(g.toast, SCREEN_W / 2.0f, r.y + 11, 21, Pal::Ink);
}

bool BackButton(Game& g) {
    if (Button({20, 20, 190, 44}, "< The Nautilus")) {
        g.scene = Scene::Hub;
        g.dismissArmed = -1;
        return true;
    }
    return false;
}

void DrawSceneTitle(const char* title, const char* subtitle) {
    int w = MeasureTxt(title, 40, true);
    TxtBold(title, SCREEN_W / 2.0f - w / 2.0f + 2, 16, 40, Fade(BLACK, 0.7f));
    TxtBold(title, SCREEN_W / 2.0f - w / 2.0f, 13, 40, Pal::Brass);
    int sw = MeasureTxt(subtitle, 18);
    TxtShadow(subtitle, SCREEN_W / 2.0f - sw / 2.0f, 60, 18, Pal::Paper);
}

void DrawGoldBadge(const Game& g) {
    Rectangle r{1030, 18, 232, 48};
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.3f, 6, Fade(BLACK, 0.45f));
    DrawRectangleRounded(r, 0.3f, 6, Color{28, 36, 40, 235});
    DrawRectangleRoundedLinesEx(r, 0.3f, 6, 2, Pal::BrassDk);
    DrawCircle((int)r.x + 24, (int)r.y + 24, 11, Pal::Brass);
    DrawCircle((int)r.x + 21, (int)r.y + 21, 4, Color{255, 240, 190, 255});
    TxtBold(TextFormat("%d", g.gold), r.x + 42, r.y + 12, 22, Pal::Brass);
    Txt(TextFormat("Batteries %d", g.batteries), r.x + 118, r.y + 15, 17, Pal::Paper);
}

// Station screens sit in a dim, lamp-lit corner of the hull.
void DrawCabinBackground() {
    SetPost(0.28f, 0.02f, 0.06f); // parchment is bright: keep bloom low so text stays crisp
    float t = (float)GetTime();
    DrawTiled(Tex::Metal, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, 1.0f, Color{104, 120, 118, 255});
    for (int i = 0; i < 6; i++) { // hull ribs
        float x = 110 + i * 212.0f;
        DrawRectangleGradientH((int)x - 14, 0, 14, SCREEN_H, Color{40, 50, 50, 255}, Color{92, 108, 104, 255});
        DrawRectangleGradientH((int)x, 0, 14, SCREEN_H, Color{92, 108, 104, 255}, Color{34, 42, 42, 255});
    }
    DrawPipeH(0, SCREEN_W, 84, 9, Pal::Copper);
    DrawPipeH(0, SCREEN_W, 104, 6, Color{120, 124, 118, 255});
    for (int i = 0; i < 5; i++) DrawFlange({150 + i * 260.0f, 84}, 9, false, Pal::BrassDk);
    DrawVGradient({0, SCREEN_H - 90.0f, (float)SCREEN_W, 90}, Fade(BLACK, 0), Fade(BLACK, 0.5f));

    LightsBegin(Color{58, 66, 72, 255});
    for (int i = 0; i < 4; i++) {
        float flick = 0.92f + 0.08f * sinf(t * 13 + i * 2.1f) * sinf(t * 3.7f + i);
        AddLight({160 + i * 320.0f, 120}, 520, Color{255, 206, 140, 255}, 0.75f * flick);
    }
    AddLight({SCREEN_W / 2.0f, SCREEN_H / 2.0f}, 900, Color{90, 110, 120, 255}, 0.6f);
    LightsEnd();
    for (int i = 0; i < 4; i++) Glow({160 + i * 320.0f, 118}, 34, Color{255, 200, 120, 120});
    InkPass(0.9f, 1.0f);
}

std::string RankString(int mask) {
    // Turns a rank mask into "1-2", "2-4" or "1, 3"
    std::vector<int> r;
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) r.push_back(i + 1);
    if (r.empty()) return "-";
    if (r.size() == 1) return std::to_string(r[0]);
    if (r.back() - r.front() == (int)r.size() - 1) return std::to_string(r.front()) + "-" + std::to_string(r.back());
    std::string s;
    for (size_t i = 0; i < r.size(); i++) s += (i ? ", " : "") + std::to_string(r[i]);
    return s;
}
