// ============================================================================
//  DEPTH - small immediate-mode UI helpers
// ============================================================================
#include "game.h"
#include <cmath>

void Txt(const std::string& s, float x, float y, int size, Color c) {
    DrawText(s.c_str(), (int)x, (int)y, size, c);
}

void DrawTextCentered(const std::string& s, float cx, float y, int size, Color c) {
    int w = MeasureText(s.c_str(), size);
    DrawText(s.c_str(), (int)(cx - w / 2.0f), (int)y, size, c);
}

bool Button(Rectangle r, const char* text, bool enabled, int fontSize) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color fill = !enabled ? Color{120, 110, 95, 255} : hover ? Pal::Brass : Pal::BrassDk;
    DrawRectangleRounded(r, 0.25f, 6, fill);
    DrawRectangleRoundedLinesEx(r, 0.25f, 6, 2.0f, Pal::Ink);
    int fs = fontSize;
    int tw = MeasureText(text, fs);
    while (tw > r.width - 12 && fs > 10) tw = MeasureText(text, --fs);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - fs) / 2), fs,
             enabled ? Pal::Paper : Color{200, 190, 170, 255});
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void Panel(Rectangle r, Color fill) {
    DrawRectangleRounded(r, 0.04f, 8, fill);
    DrawRectangleRoundedLinesEx(r, 0.04f, 8, 3.0f, Pal::BrassDk);
}

void DrawWrapped(const std::string& text, Rectangle r, int size, Color c) {
    float x = r.x, y = r.y, lineH = size * 1.3f;
    int space = MeasureText(" ", size) + 2;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return;
        int w = MeasureText(word.c_str(), size);
        if (x + w > r.x + r.width && x > r.x) { x = r.x; y += lineH; }
        DrawText(word.c_str(), (int)x, (int)y, size, c);
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
    DrawRectangleRec(r, Color{30, 24, 20, 220});
    DrawRectangleRec({r.x, r.y, r.width * frac, r.height}, fill);
}

void Toast(Game& g, const std::string& msg) {
    g.toast = msg;
    g.toastTimer = 2.8f;
}

void DrawToast(Game& g) {
    if (g.toastTimer <= 0) return;
    g.toastTimer -= GetFrameTime();
    int w = MeasureText(g.toast.c_str(), 22) + 40;
    Rectangle r{(SCREEN_W - w) / 2.0f, SCREEN_H - 120.0f, (float)w, 44};
    Panel(r, Color{250, 240, 214, 240});
    DrawTextCentered(g.toast, SCREEN_W / 2.0f, r.y + 11, 22, Pal::Ink);
}

bool BackButton(Game& g) {
    if (Button({20, 20, 170, 44}, "< The Nautilus")) {
        g.scene = Scene::Hub;
        g.dismissArmed = -1;
        return true;
    }
    return false;
}

void DrawSceneTitle(const char* title, const char* subtitle) {
    DrawTextCentered(title, SCREEN_W / 2.0f + 2, 18, 40, Pal::Ink);
    DrawTextCentered(title, SCREEN_W / 2.0f, 16, 40, Pal::Brass);
    DrawTextCentered(subtitle, SCREEN_W / 2.0f, 60, 18, Pal::Paper);
}

void DrawGoldBadge(const Game& g) {
    Rectangle r{1040, 18, 222, 48};
    Panel(r);
    DrawCircle((int)r.x + 22, (int)r.y + 24, 11, Pal::Brass);
    DrawCircle((int)r.x + 19, (int)r.y + 21, 4, Color{255, 240, 190, 255});
    Txt(TextFormat("%d", g.gold), r.x + 40, r.y + 13, 22, Pal::Ink);
    Txt(TextFormat("Batteries %d", g.batteries), r.x + 105, r.y + 16, 18, Pal::BrassDk);
}

void DrawCabinBackground() {
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Color{44, 92, 104, 255}, Color{20, 48, 62, 255});
    DrawRectangleLinesEx({6, 6, SCREEN_W - 12.0f, SCREEN_H - 12.0f}, 5, Pal::BrassDk);
    for (int x = 40; x < SCREEN_W; x += 80) {
        DrawCircle(x, 14, 3, Pal::Brass);
        DrawCircle(x, SCREEN_H - 14, 3, Pal::Brass);
    }
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
