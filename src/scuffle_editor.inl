// Scuffle's level editor (doc p. 17), included at the end of scuffle_game.cpp (it draws with the scene's helpers). Paint
// tiles, drag out pieces, place spawns and crate zones, tune a piece's numbers; open the built-ins to see how they work;
// mirror; the check (every spawn must reach every other before a stage saves or shares); share by code (copy and paste:
// a stage or a pack); a library of your own stages (scuffle_library.txt next to the exe); P plays it with bots (P again,
// or the button at the match's end, comes back with the edits).

namespace {
enum Brush { B_TILE0 = 1, B_PIECE0 = 100, B_SPAWN = 200, B_CRATE, B_ERASE, B_SELECT };
struct Editor {
    bool on = false, playing = false; sf::Stage st; int brush = B_TILE0 + sf::T_STONE - 1; int sel = -1;
    bool dragging = false; int dx0 = 0, dy0 = 0; std::string msg; float msgT = 0;
    std::vector<sf::Stage> builtins; int builtIdx = -1; std::vector<std::pair<std::string, std::string>> lib; int libIdx = -1;
    bool naming = false; std::string nameBuf;
};
Editor E;
const char* LIBRARY = "scuffle_library.txt";
void Say(const std::string& m) { E.msg = m; E.msgT = 4; }
sf::Stage Blank() {
    sf::Stage s; s.w = 32; s.h = 18; s.t.assign(32 * 18, sf::T_EMPTY);
    for (int x = 0; x < 32; x++) s.Set(x, 0, sf::T_STONE);
    s.name = "Untitled"; s.author = "You"; s.spawns = {{2.5f * sf::TILE, sf::TILE}, {29.5f * sf::TILE, sf::TILE}};
    return s;
}
void LoadLibrary() {
    E.lib.clear(); FILE* f = fopen(LIBRARY, "r"); if (!f) return;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t a = l.find('\t'), b = l.rfind('\t'); if (a == std::string::npos || b == a) continue;
        E.lib.push_back({l.substr(0, a), l.substr(b + 1)});
    }
    fclose(f);
}
void SaveToLibrary(const sf::Stage& s, const std::string& code) {
    FILE* f = fopen(LIBRARY, "a"); if (!f) { Say("Couldn't write the library."); return; }
    fprintf(f, "%s\t%s\t%s\n", s.name.c_str(), s.author.c_str(), code.c_str()); fclose(f); LoadLibrary();
}
void FitEditorCamera() {   // (the whole stage, left of the palette)
    float sw = E.st.Width(), sh = E.st.Height();
    S.zoom = std::min((SCREEN_W - 300.0f) / sw, (SCREEN_H - 170.0f) / sh);
    S.cam = {sw / 2 + 140 / S.zoom, sh / 2 - 15 / S.zoom};
}
bool InStage(int x, int y) { return x >= 0 && y >= 0 && x < E.st.w && y < E.st.h; }
int PieceAt(int x, int y) { for (int i = (int)E.st.pieces.size() - 1; i >= 0; i--) { const sf::Piece& p = E.st.pieces[i]; if (x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h) return i; } return -1; }
void ClearCell(int x, int y) {
    E.st.Set(x, y, sf::T_EMPTY);
    int pi = PieceAt(x, y); if (pi >= 0) { E.st.pieces.erase(E.st.pieces.begin() + pi); if (E.sel == pi) E.sel = -1; else if (E.sel > pi) E.sel--; }
    E.st.spawns.erase(std::remove_if(E.st.spawns.begin(), E.st.spawns.end(), [&](const Vector2& s) { return (int)floorf(s.x / sf::TILE) == x && (int)floorf(s.y / sf::TILE + 0.01f) == y; }), E.st.spawns.end());
}
sf::Stage Mirrored(const sf::Stage& in) {
    sf::Stage m = in;
    for (int y = 0; y < m.h; y++) for (int x = 0; x < m.w; x++) { uint8_t k = in.At(m.w - 1 - x, y); if (k == sf::T_CONV_L) k = sf::T_CONV_R; else if (k == sf::T_CONV_R) k = sf::T_CONV_L; m.t[y * m.w + x] = k; }
    for (auto& s : m.spawns) s.x = m.Width() - s.x;
    for (auto& p : m.pieces) { p.x = m.w - p.x - p.w; p.dx = -p.dx; }
    for (auto& c : m.crateCols) c = m.w - 1 - c;
    return m;
}
}  // namespace

static bool EditorPlaying() { return E.on && E.playing; }
static void EditorBackFromPlay() { E.playing = false; S.active = false; FitEditorCamera(); }
void StartScuffleEditor(Game& g) {
    E = Editor{}; E.on = true; E.st = Blank(); E.builtins = sf::LoadWorldPack(sf::WD_NAUTILUS); LoadLibrary();
    S = ScuffleScene{}; S.active = false; S.M.w.Init(E.st, 1, 1);
    uint32_t h = 99; for (int i = 0; i < 700; i++) { h = h * 1664525u + 1013904223u; float x = (float)((h >> 8) % SCREEN_W); h = h * 1664525u + 1013904223u; S.grain.push_back({x, (float)((h >> 8) % SCREEN_H)}); }
    FitEditorCamera();
    g.scene = Scene::Scuffle;
    Say("The editor: paint with the left button, clear with the right. P plays it with bots.");
}
static bool EditorFrame(Game& g) {
    if (!E.on || E.playing) return false;
    float dt = std::min(GetFrameTime(), 1 / 20.0f); S.t += dt; E.msgT -= dt;
    S.M.w.stage = E.st; S.M.w.t = 0; S.M.w.sticks.clear(); S.M.w.items.clear(); S.M.w.bullets.clear();   // (nothing moves while you edit)
    FitEditorCamera();
    DrawStage();
    // the grid, the crate zones, the spawns, the selection
    float px = S.zoom * sf::TILE;
    Vector2 o = W2S({0, E.st.Height()});
    for (int x = 0; x <= E.st.w; x++) DrawLineEx({o.x + x * px, o.y}, {o.x + x * px, o.y + E.st.h * px}, 1, ColorAlpha(INK, 0.12f));
    for (int y = 0; y <= E.st.h; y++) DrawLineEx({o.x, o.y + y * px}, {o.x + E.st.w * px, o.y + y * px}, 1, ColorAlpha(INK, 0.12f));
    DrawRectangleLinesEx({o.x, o.y, E.st.w * px, E.st.h * px}, 2, INK);
    for (int c : E.st.crateCols) { DrawRectangle((int)(o.x + c * px), (int)o.y, (int)px, 6, Color{170, 120, 64, 255}); DrawTextCentered("C", o.x + (c + 0.5f) * px, o.y - 16, 12, INK); }
    for (size_t i = 0; i < E.st.spawns.size(); i++) { Vector2 s = W2S(E.st.spawns[i]); DrawCircleV({s.x, s.y - px * 0.5f}, px * 0.35f, StickColor((int)i)); DrawTextCentered(TextFormat("%d", (int)i + 1), s.x, s.y - px * 0.5f - 6, 12, WHITE); }
    if (E.sel >= 0 && E.sel < (int)E.st.pieces.size()) { const sf::Piece& p = E.st.pieces[E.sel]; Vector2 a = W2S({p.x * sf::TILE, (p.y + p.h) * sf::TILE}); DrawRectangleLinesEx({a.x - 3, a.y - 3, p.w * px + 6, p.h * px + 6}, 3, Color{230, 80, 60, 255}); }
    // the mouse on the grid
    Vector2 mp = GetMousePosition(), mw = S2W(mp); int tx = (int)floorf(mw.x / sf::TILE), ty = (int)floorf(mw.y / sf::TILE);
    bool overGrid = InStage(tx, ty) && mp.x < SCREEN_W - 280 && mp.y > 70 && mp.y < SCREEN_H - 90;
    if (overGrid) DrawRectangleLinesEx({o.x + tx * px, o.y + (E.st.h - 1 - ty) * px, px, px}, 2, Color{230, 80, 60, 255});
    if (overGrid && !E.naming) {
        bool leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT), rightDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT), click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (rightDown) ClearCell(tx, ty);
        else if (E.brush >= B_TILE0 && E.brush < B_PIECE0 && leftDown) E.st.Set(tx, ty, (uint8_t)(E.brush - B_TILE0 + 1));
        else if (E.brush == B_ERASE && leftDown) ClearCell(tx, ty);
        else if (E.brush == B_SELECT && click) E.sel = PieceAt(tx, ty);
        else if (E.brush == B_SPAWN && click) {
            bool had = false;
            for (size_t i = 0; i < E.st.spawns.size(); i++) if ((int)floorf(E.st.spawns[i].x / sf::TILE) == tx && (int)floorf(E.st.spawns[i].y / sf::TILE + 0.01f) == ty) { E.st.spawns.erase(E.st.spawns.begin() + i); had = true; break; }
            if (!had) { if (E.st.spawns.size() < 8) E.st.spawns.push_back({(tx + 0.5f) * sf::TILE, ty * sf::TILE}); else Say("Eight spawns at most."); }
        }
        else if (E.brush == B_CRATE && click) { auto it = std::find(E.st.crateCols.begin(), E.st.crateCols.end(), tx); if (it != E.st.crateCols.end()) E.st.crateCols.erase(it); else E.st.crateCols.push_back(tx); }
        else if (E.brush >= B_PIECE0 && E.brush < B_SPAWN) {
            int hit = PieceAt(tx, ty);
            if (click && hit >= 0) E.sel = hit;
            else if (click) { E.dragging = true; E.dx0 = tx; E.dy0 = ty; }
        }
    }
    if (E.dragging) {
        int x0 = std::clamp(std::min(E.dx0, tx), 0, E.st.w - 1), x1 = std::clamp(std::max(E.dx0, tx), 0, E.st.w - 1), y0 = std::clamp(std::min(E.dy0, ty), 0, E.st.h - 1), y1 = std::clamp(std::max(E.dy0, ty), 0, E.st.h - 1);
        DrawRectangleLinesEx({o.x + x0 * px, o.y + (E.st.h - 1 - y1) * px, (x1 - x0 + 1) * px, (y1 - y0 + 1) * px}, 2, Color{230, 80, 60, 255});
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            E.dragging = false;
            sf::Piece p; p.kind = (uint8_t)(E.brush - B_PIECE0); sf::PieceDefaults(p); p.x = x0; p.y = y0; p.w = x1 - x0 + 1; p.h = y1 - y0 + 1;
            if (p.kind == sf::PK_VENT || p.kind == sf::PK_TUBE) { p.h = 1; if (p.kind == sf::PK_TUBE) p.w = 1; }
            if (p.kind == sf::PK_VENT) for (int x = p.x; x < p.x + p.w; x++) E.st.Set(x, p.y, sf::T_STONE);
            if (p.kind == sf::PK_WINDOW) for (int y = p.y; y < p.y + p.h; y++) for (int x = p.x; x < p.x + p.w; x++) E.st.Set(x, y, sf::T_GLASS);
            if (p.kind == sf::PK_TUBE) p.dx = E.st.Solid(p.x - 1, p.y) ? 1 : -1;
            E.st.pieces.push_back(p); E.sel = (int)E.st.pieces.size() - 1;
        }
    }
    if (E.sel >= 0 && IsKeyPressed(KEY_DELETE)) { E.st.pieces.erase(E.st.pieces.begin() + E.sel); E.sel = -1; }
    // the palette (right)
    Rectangle pal{SCREEN_W - 270.0f, 70, 256, SCREEN_H - 170.0f};
    DrawRectangleRounded(pal, 0.04f, 6, ColorAlpha(Color{24, 18, 14, 255}, 0.88f));
    TxtBold("Pieces", pal.x + 12, pal.y + 8, 15, Color{230, 200, 140, 255});
    struct PalItem { int brush; const char* name; };
    static const PalItem P[] = {{B_TILE0 + sf::T_STONE - 1, "Stone"}, {B_TILE0 + sf::T_WOOD - 1, "Wood"}, {B_TILE0 + sf::T_ICE - 1, "Ice"}, {B_TILE0 + sf::T_GLASS - 1, "Glass"}, {B_TILE0 + sf::T_ROPE - 1, "Rope"},
                                {B_TILE0 + sf::T_RAIL - 1, "Rail"}, {B_TILE0 + sf::T_CONV_L - 1, "Conveyor <"}, {B_TILE0 + sf::T_CONV_R - 1, "Conveyor >"},
                                {B_PIECE0 + sf::PK_PISTON, "Piston"}, {B_PIECE0 + sf::PK_ELEVATOR, "Elevator"}, {B_PIECE0 + sf::PK_VENT, "Steam vent"}, {B_PIECE0 + sf::PK_PROPELLER, "Propeller"},
                                {B_PIECE0 + sf::PK_TUBE, "Torpedo tube"}, {B_PIECE0 + sf::PK_WINDOW, "Window"}, {B_SPAWN, "Spawn"}, {B_CRATE, "Crate zone"}, {B_SELECT, "Select"}, {B_ERASE, "Eraser"}};
    const int NP = (int)(sizeof P / sizeof P[0]);
    for (int i = 0; i < NP; i++) {
        Rectangle r{pal.x + 10 + (i % 2) * 120.0f, pal.y + 32 + (i / 2) * 26.0f, 114, 22};
        bool on = E.brush == P[i].brush;
        DrawRectangleRounded(r, 0.3f, 6, on ? Color{150, 110, 50, 255} : Color{60, 48, 38, 255});
        DrawTextCentered(P[i].name, r.x + r.width / 2, r.y + 4, 12, Color{240, 230, 210, 255});
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mp, r)) { E.brush = P[i].brush; E.dragging = false; }
    }
    // the selected piece's numbers
    float py = pal.y + 32 + ((NP + 1) / 2) * 26.0f + 10;
    if (E.sel >= 0 && E.sel < (int)E.st.pieces.size()) {
        sf::Piece& p = E.st.pieces[E.sel];
        TxtBold(TextFormat("%s %d (%dx%d)", sf::PieceName(p.kind), E.sel + 1, p.w, p.h), pal.x + 12, py, 14, Color{230, 200, 140, 255}); py += 22;
        struct Num { const char* label; float* v; float step; int* iv; };
        Num N[] = {{"dx", nullptr, 1, &p.dx}, {"dy", nullptr, 1, &p.dy}, {"travel", &p.travel, 0.5f, nullptr}, {"period", &p.period, 0.25f, nullptr}, {"phase", &p.phase, p.kind == sf::PK_WINDOW ? 1.0f : 0.05f, nullptr}, {"on", &p.on, 0.1f, nullptr}, {"power", &p.power, 0.25f, nullptr}};
        for (auto& n : N) {
            Txt(n.label, pal.x + 14, py + 2, 13, Color{220, 210, 190, 255});
            Txt(n.iv ? TextFormat("%d", *n.iv) : TextFormat("%.2f", *n.v), pal.x + 90, py + 2, 13, WHITE);
            if (Button({pal.x + 160, py, 34, 20}, "-", true, 13)) { if (n.iv) *n.iv = std::max(-1, *n.iv - 1); else *n.v = std::max(0.0f, *n.v - n.step); }
            if (Button({pal.x + 200, py, 34, 20}, "+", true, 13)) { if (n.iv) *n.iv = std::min(1, *n.iv + 1); else *n.v += n.step; }
            py += 23;
        }
        if (Button({pal.x + 12, py + 2, 110, 24}, "Delete it", true, 13)) { E.st.pieces.erase(E.st.pieces.begin() + E.sel); E.sel = -1; }
    }
    // the top bar: the name, the world, the wrap
    DrawRectangle(0, 0, SCREEN_W, 62, ColorAlpha(Color{24, 18, 14, 255}, 0.9f));
    if (E.naming) {
        NoteTyping(); int ch; while ((ch = GetCharPressed()) > 0) if (ch >= 32 && ch < 127 && E.nameBuf.size() < 40) E.nameBuf += (char)ch;
        if (IsKeyPressed(KEY_BACKSPACE) && !E.nameBuf.empty()) E.nameBuf.pop_back();
        if (IsKeyPressed(KEY_ENTER)) { if (!E.nameBuf.empty()) E.st.name = E.nameBuf; E.naming = false; }
        TxtBold(("Name: " + E.nameBuf + "_").c_str(), 16, 10, 20, Color{255, 230, 170, 255});
    } else {
        TxtBold(E.st.name.c_str(), 16, 10, 20, Color{240, 230, 210, 255});
        if (Button({16, 36, 80, 22}, "Rename", true, 12)) { E.naming = true; E.nameBuf = E.st.name; while (GetCharPressed() > 0) {} }
    }
    if (Button({110, 36, 150, 22}, sf::WorldName(E.st.world), true, 12)) E.st.world = (E.st.world + 1) % sf::WD_COUNT;
    if (Button({270, 36, 120, 22}, E.st.wrap ? "Wraps: on" : "Wraps: off", true, 12)) E.st.wrap = !E.st.wrap;
    Txt(TextFormat("%d spawns   %d pieces   %d crate zones   by %s", (int)E.st.spawns.size(), (int)E.st.pieces.size(), (int)E.st.crateCols.size(), E.st.author.c_str()), 410, 40, 13, Color{210, 200, 180, 255});
    // the bottom bar: new, the built-ins, mirror, the check, codes, the library, play
    float bx = 16, by = SCREEN_H - 80.0f;
    auto bt = [&](const char* label, float w = 112) { bool r = Button({bx, by, w, 30}, label, true, 13); bx += w + 8; return r; };
    if (bt("New", 70)) { E.st = Blank(); E.sel = -1; Say("A blank stage."); }
    if (bt("< Built-in", 100) && !E.builtins.empty()) { E.builtIdx = (E.builtIdx - 1 + (int)E.builtins.size()) % (int)E.builtins.size(); E.st = E.builtins[E.builtIdx]; E.sel = -1; Say("Opened " + E.st.name + " (a built-in: see how it works)."); }
    if (bt("Built-in >", 100) && !E.builtins.empty()) { E.builtIdx = (E.builtIdx + 1) % (int)E.builtins.size(); E.st = E.builtins[E.builtIdx]; E.sel = -1; Say("Opened " + E.st.name + " (a built-in: see how it works)."); }
    if (bt("Mirror", 76)) { E.st = Mirrored(E.st); E.sel = -1; }
    if (bt("Check", 76)) { sf::Reach r = sf::CheckReachable(E.st); Say(r.ok ? std::string(TextFormat("Every spawn reaches every other (%d places found).", r.nodes)) : "Not yet: " + r.why + ". It won't save until it does."); }
    if (bt("Copy code", 100)) { sf::Reach r = sf::CheckReachable(E.st); if (!r.ok) Say("It won't share until every spawn can reach every other: " + r.why); else { std::string c = sf::StageToCode(E.st); SetClipboardText(c.c_str()); Say(TextFormat("Copied: %d characters. Paste it in the lobby's chat.", (int)c.size())); } }
    if (bt("Paste code", 100)) {
        const char* c = GetClipboardText(); sf::Stage s; std::vector<sf::Stage> pack; std::string err;
        if (c && sf::StageFromCode(c, s, &err)) { E.st = s; E.sel = -1; Say("Pasted: " + s.name + " by " + s.author + "."); }
        else if (c && sf::PackFromCode(c, pack, &err) && !pack.empty()) { E.builtins = pack; E.builtIdx = 0; E.st = pack[0]; Say(TextFormat("Pasted a pack of %d: the built-in arrows look through it.", (int)pack.size())); }
        else Say("That isn't a stage code (" + err + ").");
    }
    if (bt("Save", 70)) { sf::Reach r = sf::CheckReachable(E.st); if (!r.ok) Say("It won't save until every spawn can reach every other: " + r.why); else { SaveToLibrary(E.st, sf::StageToCode(E.st)); Say("Saved to your library."); } }
    if (bt("Library >", 96) && !E.lib.empty()) { E.libIdx = (E.libIdx + 1) % (int)E.lib.size(); sf::Stage s; if (sf::StageFromCode(E.lib[E.libIdx].second, s)) { E.st = s; E.sel = -1; Say("From your library: " + s.name + "."); } }
    if (bt("Play (P)", 96) || (IsKeyPressed(KEY_P) && !E.naming)) {
        if (E.st.spawns.size() < 2) Say("It needs two spawns at least.");
        else { E.playing = true; sf::Stage st = E.st; StartScuffle(g, std::max(1, std::min(7, (int)st.spawns.size() - 1)), 2, 3); S.M.custom = {st}; S.M.Start(S.players, 3, (uint32_t)GetRandomValue(1, 1 << 30), sf::AR_CLASSIC); S.lastRound = S.M.round; }
    }
    if (bt("Leave", 70)) { E.on = false; S.active = false; g.scene = Scene::Arcade; }
    if (E.msgT > 0) DrawTextCenteredBold(E.msg.c_str(), (SCREEN_W - 280) / 2.0f, SCREEN_H - 42.0f, 15, Color{70, 40, 30, 255});
    DrawTextCentered("left: paint (drag for a piece)   right: clear   Delete: the selected piece   P: play it with bots (P again: back here)", (SCREEN_W - 280) / 2.0f, SCREEN_H - 18.0f, 11, ColorAlpha(INK, 0.7f));
    return true;
}
void DebugScuffleEditorShot(Game& g, int which) {
    StartScuffleEditor(g);
    if (which == 1 && !E.builtins.empty()) { E.builtIdx = 0; E.st = E.builtins[0]; E.sel = 0; E.brush = B_PIECE0 + sf::PK_PISTON; Say("Opened The Engine Room (a built-in: see how it works)."); }
}
