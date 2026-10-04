// Scuffle stage 5: how the other five worlds look (doc pp. 9-10, 20), included by scuffle_game.cpp (it draws with the
// scene's helpers). Depth's ink on each world's paper: the Cave's umber rock and glow-mould, the Reef's sunlit water and
// coral, Atlantis's marble and columns, the Void's black rock and the pale abyss, the Salon's wood panels and lamps. Every
// hazard shows its tell (a glow, a shadow, dust, a shiver) before it hurts; water is drawn over the sticks.

namespace {
bool DarkWorld(int w) { return w == sf::WD_VOID || w == sf::WD_CAVE; }
Color NameInk() { return DarkWorld(S.M.w.stage.world) ? Color{230, 222, 200, 255} : INK; }
Color SplashInk() { return DarkWorld(S.M.w.stage.world) ? Color{196, 186, 166, 255} : INK; }
Color WorldPaper(int w) {
    switch (w) {
        case sf::WD_CAVE: return {104, 88, 74, 255};
        case sf::WD_REEF: return {176, 216, 214, 255};
        case sf::WD_ATLANTIS: return {214, 206, 184, 255};
        case sf::WD_VOID: return {40, 42, 52, 255};
        case sf::WD_SALON: return {150, 104, 66, 255};
        default: return PAPER;
    }
}
void WorldBackdrop(const sf::Stage& s) {
    Color paper = WorldPaper(s.world);
    ClearBackground(paper);
    Color grain = ColorLerp(paper, BLACK, 0.12f);
    for (const auto& g : S.grain) DrawCircleV(g, 1.2f, grain);
    float W = s.Width(), H = s.Height(), px = S.zoom * sf::TILE;
    switch (s.world) {
    case sf::WD_CAVE: {
        // far stalactites as silhouettes; glow-mould specks that breathe; a pool's sheen low down
        Color far = ColorLerp(paper, BLACK, 0.25f);
        for (int k = 0; k < 14; k++) { float x = W * (k + 0.5f) / 14, len = H * (0.15f + 0.12f * sinf(k * 2.7f)); Vector2 a = W2S({x - 0.7f, H}), b = W2S({x + 0.7f, H}), c = W2S({x, H - len}); DrawTri(a, b, c, far); }
        for (int k = 0; k < 40; k++) { float x = fmodf(k * 7.31f, W), y = fmodf(k * 3.17f, H * 0.9f); float a = 0.4f + 0.4f * sinf(S.t * 1.3f + k); DrawCircleV(W2S({x, y}), 2 + (k % 3), ColorAlpha(Color{150, 230, 140, 255}, a)); }
        break;
    }
    case sf::WD_REEF: {
        // sun shafts down through the water, a sandy bed far off, fish drifting by
        for (int k = 0; k < 6; k++) { float x = W * (k + 0.3f) / 6 + sinf(S.t * 0.3f + k) * 0.8f; Vector2 a = W2S({x - 0.6f, H + 1}), b = W2S({x + 1.4f, H + 1}), c = W2S({x + 4, -1}), d = W2S({x + 2, -1}); DrawTri(a, b, c, ColorAlpha(WHITE, 0.10f)); DrawTri(a, c, d, ColorAlpha(WHITE, 0.10f)); }
        for (int k = 0; k < 9; k++) { float x = fmodf(k * 5.3f + S.t * (0.4f + 0.1f * (k % 3)), W + 4) - 2, y = H * (0.35f + 0.06f * (k % 5)); Vector2 c = W2S({x, y}); float r = px * 0.25f; DrawEllipse((int)c.x, (int)c.y, r * 1.6f, r * 0.7f, Color{110, 160, 170, 160}); DrawTri({c.x - r * 1.6f, c.y}, {c.x - r * 2.4f, c.y - r * 0.6f}, {c.x - r * 2.4f, c.y + r * 0.6f}, Color{110, 160, 170, 160}); }
        break;
    }
    case sf::WD_ATLANTIS: {
        // a colonnade far behind, the god-pool's light
        Color col = ColorLerp(paper, BLACK, 0.12f);
        for (int k = 0; k < 9; k++) { float x = W * (k + 0.5f) / 9; Vector2 a = W2S({x - 0.35f, H * 0.85f}), b = W2S({x + 0.35f, 0}); DrawRectangleV(a, {b.x - a.x, b.y - a.y}, col); Vector2 c = W2S({x - 0.6f, H * 0.88f}); DrawRectangleV(c, {px * 2, px * 0.4f}, col); }
        Vector2 l = W2S({0, H * 0.88f}); DrawRectangle(0, (int)l.y - (int)(px * 0.6f), SCREEN_W, (int)(px * 0.6f), col);
        break;
    }
    case sf::WD_VOID: {
        // black rock ridges, faint vents, the abyss a paler blue at the bottom of its side
        for (int k = 0; k < 30; k++) { float x = fmodf(k * 9.13f, W), y = fmodf(k * 4.71f, H); DrawCircleV(W2S({x, y}), 1.5f, ColorAlpha(Color{140, 170, 230, 255}, 0.25f + 0.2f * sinf(S.t + k))); }
        int side = S.M.w.wallSide;
        Vector2 a = W2S({side > 0 ? W * 0.7f : 0, H * 0.5f}), b = W2S({side > 0 ? W + 3 : W * 0.3f, -3});
        DrawRectangleGradientV((int)std::min(a.x, b.x), (int)a.y, (int)fabsf(b.x - a.x), (int)(b.y - a.y), ColorAlpha(Color{60, 80, 120, 255}, 0), ColorAlpha(Color{90, 120, 170, 255}, 0.35f));
        break;
    }
    case sf::WD_SALON: {
        // wood panels, the shelf of bottles, the lamps
        Color panel = ColorLerp(paper, BLACK, 0.15f);
        for (int k = 0; k <= 16; k++) { Vector2 a = W2S({W * k / 16, H}), b = W2S({W * k / 16, 0}); DrawLineEx(a, b, 2, panel); }
        Vector2 sh = W2S({0, H * 0.62f}); DrawRectangle(0, (int)sh.y, SCREEN_W, (int)(px * 0.25f), panel);
        for (int k = 0; k < 30; k++) { Vector2 c = W2S({W * (k + 0.5f) / 30, H * 0.62f}); Color g = k % 3 == 0 ? Color{70, 120, 70, 255} : k % 3 == 1 ? Color{140, 90, 40, 255} : Color{180, 170, 150, 255}; DrawRectangleV({c.x - px * 0.1f, c.y - px * 0.55f}, {px * 0.2f, px * 0.55f}, g); DrawRectangleV({c.x - px * 0.04f, c.y - px * 0.75f}, {px * 0.08f, px * 0.2f}, g); }
        for (int k = 0; k < 4; k++) { Vector2 c = W2S({W * (k + 0.5f) / 4, H * 0.9f}); DrawCircleGradient((int)c.x, (int)c.y, px * 3, ColorAlpha(Color{255, 220, 150, 255}, 0.35f), ColorAlpha(Color{255, 220, 150, 255}, 0)); DrawCircleV(c, px * 0.25f, Color{255, 230, 170, 255}); }
        break;
    }
    }
}
Color WorldStone(int w) {
    switch (w) { case sf::WD_CAVE: return {112, 96, 84, 255}; case sf::WD_REEF: return {214, 188, 146, 255}; case sf::WD_ATLANTIS: return {226, 220, 206, 255}; case sf::WD_VOID: return {68, 68, 78, 255}; case sf::WD_SALON: return {96, 62, 40, 255}; default: return STONE; }
}
void TileEdges(const sf::Stage& s, int x, int y, Vector2 a, float px, float th, Color ink) {
    auto solidish = [&](int xx, int yy) { uint8_t q = s.At(xx, yy); return q != sf::T_EMPTY && q != sf::T_ROPE && q != sf::T_GLASS && q != sf::T_WATER && q != sf::T_BRINE; };
    if (!solidish(x, y + 1)) DrawLineEx({a.x - 1, a.y}, {a.x + px + 1, a.y}, th, ink);
    if (!solidish(x, y - 1)) DrawLineEx({a.x - 1, a.y + px}, {a.x + px + 1, a.y + px}, th, ink);
    if (!solidish(x - 1, y)) DrawLineEx({a.x, a.y - 1}, {a.x, a.y + px + 1}, th, ink);
    if (!solidish(x + 1, y)) DrawLineEx({a.x + px, a.y - 1}, {a.x + px, a.y + px + 1}, th, ink);
}
// the other worlds' stone, and the new tiles
bool DrawWorldTile(const sf::Stage& s, int x, int y, uint8_t k, Vector2 a, float px, float th) {
    uint32_t h = (uint32_t)(x * 73856093u ^ y * 19349663u);
    switch (k) {
    case sf::T_STONE: {
        if (s.world == sf::WD_NAUTILUS) return false;
        Color c = WorldStone(s.world);
        DrawRectangleV(a, {px + 1, px + 1}, c);
        Color d = ColorLerp(c, BLACK, 0.18f);
        if (s.world == sf::WD_ATLANTIS) { DrawLineEx({a.x + px * ((h % 7) / 7.0f), a.y}, {a.x + px * ((h >> 3) % 7 / 7.0f), a.y + px}, 1, Color{180, 170, 160, 255}); }   // (marble's veins)
        else if (s.world == sf::WD_SALON) { for (int q = 1; q < 3; q++) DrawLineEx({a.x, a.y + q * px / 3}, {a.x + px, a.y + q * px / 3}, 1, d); }
        else { for (int q = 0; q < 3; q++) DrawCircleV({a.x + px * (0.2f + 0.3f * ((h >> (q * 4)) % 3)), a.y + px * (0.25f + 0.25f * q)}, std::max(1.0f, px * 0.05f), d); }
        if (s.world == sf::WD_VOID && h % 5 == 0) DrawCircleV({a.x + px * 0.5f, a.y + px * 0.5f}, std::max(1.0f, px * 0.05f), Color{120, 160, 230, 255});
        // the Reef's coral on top faces
        if (s.world == sf::WD_REEF && !s.Solid(x, y + 1) && !s.Liquid(x, y + 1)) { Color cc = h % 3 == 0 ? Color{230, 120, 110, 255} : h % 3 == 1 ? Color{240, 170, 80, 255} : Color{190, 120, 200, 255}; for (int q = 0; q < 3; q++) DrawCircleV({a.x + px * (0.2f + 0.3f * q), a.y - px * 0.06f}, px * (0.1f + 0.04f * ((h >> q) % 3)), cc); }
        if (s.world == sf::WD_CAVE && !s.Solid(x, y - 1) && y > 0 && h % 4 == 0) DrawTri({a.x + px * 0.3f, a.y + px}, {a.x + px * 0.7f, a.y + px}, {a.x + px * 0.5f, a.y + px * 1.4f}, c);   // (a little drip of rock under the ceiling)
        TileEdges(s, x, y, a, px, th, INK);
        return true;
    }
    case sf::T_CRUMBLE: {
        float tm = 0; int i = y * s.w + x; if (i >= 0 && i < (int)S.M.w.glassT.size()) tm = S.M.w.glassT[i];
        Vector2 j{tm > 0 ? sinf(S.t * 70 + x) * px * 0.06f : 0, 0};
        Vector2 b = Vector2Add(a, j);
        DrawRectangleV(b, {px + 1, px + 1}, ColorLerp(WorldStone(s.world), Color{170, 140, 110, 255}, 0.4f));
        DrawLineEx({b.x + px * 0.2f, b.y}, {b.x + px * 0.45f, b.y + px * 0.5f}, 1.5f, INK); DrawLineEx({b.x + px * 0.45f, b.y + px * 0.5f}, {b.x + px * 0.8f, b.y + px}, 1.5f, INK);
        if (tm > 0) DrawRectangleV(b, {px, px}, ColorAlpha(Color{230, 200, 150, 255}, std::min(0.5f, tm)));
        TileEdges(s, x, y, b, px, th, INK);
        return true;
    }
    case sf::T_CRYSTAL: {
        float g = 0.6f + 0.4f * sinf(S.t * 2 + x * 0.7f);
        DrawRectangleV(a, {px + 1, px + 1}, Color{120, 80, 170, 255});
        DrawTri({a.x, a.y + px}, {a.x + px * 0.5f, a.y}, {a.x + px, a.y + px}, ColorAlpha(Color{200, 160, 250, 255}, g));
        DrawLineEx({a.x + px * 0.5f, a.y}, {a.x + px * 0.5f, a.y + px}, 1, Color{240, 220, 255, 255});
        TileEdges(s, x, y, a, px, th, INK);
        return true;
    }
    case sf::T_URCHIN: {
        DrawRectangleV({a.x, a.y + px * 0.55f}, {px + 1, px * 0.45f + 1}, WorldStone(sf::WD_REEF));
        Vector2 c{a.x + px * 0.5f, a.y + px * 0.55f};
        for (int q = 0; q < 12; q++) { float an = q * PI / 11; DrawLineEx(c, {c.x + cosf(an) * px * 0.55f, c.y - sinf(an) * px * 0.55f}, 1.5f, INK); }
        DrawCircleV(c, px * 0.28f, Color{40, 30, 50, 255});
        return true;
    }
    case sf::T_BAR: {
        DrawRectangleV(a, {px + 1, px + 1}, Color{110, 50, 30, 255});
        if (!s.Solid(x, y + 1)) { DrawRectangleV(a, {px + 1, px * 0.2f}, Color{150, 80, 50, 255}); DrawLineEx({a.x, a.y + px * 0.35f}, {a.x + px + 1, a.y + px * 0.35f}, 2, Color{210, 170, 80, 255}); }   // (the polished top, the brass rail)
        TileEdges(s, x, y, a, px, th, INK);
        return true;
    }
    case sf::T_WATER: case sf::T_BRINE: return true;   // (drawn over the sticks: DrawLiquids)
    }
    return false;
}
float CycleOf(const sf::Piece& p) { return fmodf(std::max(0.0f, S.M.w.t - p.start) + p.phase * p.period, std::max(0.2f, p.period)); }
// the other worlds' pieces (r: the piece's rectangle on screen; the world rectangle is p.x..)
bool DrawWorldPiece(const sf::Piece& p, Rectangle r, float px) {
    const sf::World& w = S.M.w;
    bool awake = w.t >= p.start;
    float x0 = p.x * sf::TILE, y0 = p.y * sf::TILE, x1 = (p.x + p.w) * sf::TILE, y1 = (p.y + p.h) * sf::TILE;
    switch (p.kind) {
    case sf::PK_STALACTITE: {
        if (p.broken) return true;
        float shake = p.prog > 0 ? 0 : 0;
        Vector2 a{r.x + shake, r.y}, b{r.x + r.width, r.y}, c{r.x + r.width * 0.5f, r.y + r.height};
        DrawTri(Vector2Add(a, {-2, -1}), Vector2Add(b, {2, -1}), Vector2Add(c, {0, 3}), INK);
        DrawTri(a, b, c, WorldStone(sf::WD_CAVE));
        DrawLineEx({r.x + r.width * 0.35f, r.y + r.height * 0.2f}, {r.x + r.width * 0.5f, r.y + r.height * 0.7f}, 1, ColorLerp(WorldStone(sf::WD_CAVE), WHITE, 0.3f));
        return true;
    }
    case sf::PK_DRIP: {
        float u = CycleOf(p) / std::max(0.2f, p.period);
        Vector2 c = W2S({(x0 + x1) * 0.5f, y0});
        DrawLineEx({c.x - px * 0.3f, c.y}, {c.x + px * 0.3f, c.y}, 2, INK);
        DrawCircleV({c.x, c.y + px * 0.1f * u}, px * 0.12f * u + 1, Color{150, 220, 80, 255});
        return true;
    }
    case sf::PK_STREAM: {
        bool live = p.prog > 0 || p.period <= 0;
        float a = live ? 0.55f : 0.12f;
        Vector2 d = Vector2Normalize({(float)p.dx, (float)p.dy});
        for (int q = 0; q < p.w * p.h / 3 + 4; q++) {
            float fx = fmodf(q * 0.618f + S.t * (live ? p.power / std::max(1.0f, (float)p.w * sf::TILE) * (d.x >= 0 ? 1 : -1) : 0.05f), 1.0f); if (fx < 0) fx += 1;
            float fy = fmodf(q * 0.377f, 1.0f);
            Vector2 c = W2S({x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy});
            DrawLineEx(c, {c.x - d.x * px * 0.8f, c.y + d.y * px * 0.8f}, 1.5f, ColorAlpha(WHITE, a));
        }
        if (!live && p.period > 0 && CycleOf(p) > p.period - 1.0f) DrawRectangleLinesEx(r, 2, ColorAlpha(WHITE, 0.4f + 0.3f * sinf(S.t * 20)));   // (the surge's count: it's about to run)
        return true;
    }
    case sf::PK_TOAD: {
        float ox = p.dx > 0 ? r.x + r.width : r.x;
        Vector2 body{ox - p.dx * px * 0.6f, r.y - px * 0.1f};
        DrawEllipse((int)body.x, (int)body.y, px * 0.75f + 2, px * 0.45f + 2, INK); DrawEllipse((int)body.x, (int)body.y, px * 0.75f, px * 0.45f, Color{90, 130, 70, 255});
        for (int e : {-1, 1}) { Vector2 ec{body.x + e * px * 0.3f, body.y - px * 0.35f}; DrawCircleV(ec, px * 0.15f, Color{230, 210, 90, 255}); DrawCircleV(ec, px * 0.06f, INK); }
        if (p.prog > 0) {
            float reach = p.travel * sf::TILE * sinf(PI * p.prog / 0.6f);
            Vector2 o = W2S({p.dx > 0 ? x1 : x0, y1 + 0.35f}), t = W2S({(p.dx > 0 ? x1 : x0) + p.dx * reach, y1 + 0.35f});
            DrawLineEx(o, t, px * 0.18f + 2, INK); DrawLineEx(o, t, px * 0.18f, Color{220, 110, 120, 255}); DrawCircleV(t, px * 0.18f, Color{220, 110, 120, 255});
        }
        return true;
    }
    case sf::PK_CLAW: {
        if (!awake) return true;
        if (p.prog > 0 && p.prog < 1) {   // (the tell: the claw rises at its edge, red and shaking)
            Vector2 c = W2S({p.dx >= 0 ? x0 + 0.6f : x1 - 0.6f, (y0 + y1) * 0.5f}); c.x += sinf(S.t * 40) * 3;
            DrawCircleV(c, px * (0.6f + p.prog), ColorAlpha(Color{200, 60, 40, 255}, 0.3f + 0.4f * p.prog));
        }
        if (p.prog >= 1) {
            float s = p.prog - 1, cx = p.dx >= 0 ? x0 + (x1 - x0) * s : x1 - (x1 - x0) * s;
            Vector2 c = W2S({cx, (y0 + y1) * 0.5f}); float R = (y1 - y0) * 0.5f * S.zoom;
            DrawCircleSector(c, R + 3, 0, 360, 24, INK); DrawCircleSector(c, R, p.dx >= 0 ? 30.0f : 210.0f, p.dx >= 0 ? 330.0f : 510.0f, 24, Color{190, 60, 40, 255});
            DrawLineEx({c.x - p.dx * R * 1.6f, c.y}, c, R * 0.5f, Color{170, 50, 34, 255});
        }
        return true;
    }
    case sf::PK_REACHER: {
        bool holding = p.hold >= 0;
        for (int q = 0; q < 5; q++) {
            float an = -PI / 2 + (q - 2) * 0.35f + (holding ? (q < 2 ? 0.6f : q > 2 ? -0.6f : 0) : sinf(S.t * 1.5f + q) * 0.12f);
            Vector2 b{r.x + r.width * 0.5f, r.y + r.height * 0.3f}, t{b.x + cosf(an) * px * 0.9f, b.y + sinf(an) * px * 0.9f};
            DrawLineEx(b, t, px * 0.14f + 2, INK); DrawLineEx(b, t, px * 0.14f, Color{240, 140, 110, 255});
        }
        return true;
    }
    case sf::PK_EEL: {
        Vector2 m = W2S({p.dx >= 0 ? x1 : x0, (y0 + y1) * 0.5f});
        DrawEllipse((int)m.x, (int)m.y, px * 0.25f, px * 0.5f, Color{20, 16, 14, 255});
        float out = p.prog > 0 ? std::min(1.0f, p.prog / 0.35f) * 0.6f : (p.period - p.cool < 0.25f && p.cool > 0 ? 1.4f : 0);
        if (out > 0) { Vector2 hd{m.x + p.dx * px * out, m.y}; DrawLineEx(m, hd, px * 0.4f + 2, INK); DrawLineEx(m, hd, px * 0.4f, Color{110, 120, 70, 255}); DrawCircleV({hd.x, hd.y - px * 0.1f}, px * 0.06f + 1, Color{230, 220, 120, 255}); }
        return true;
    }
    case sf::PK_SHARK: {
        for (int q = 0; q < 2; q++) {   // (fins circling just under the surface)
            float u = fmodf(S.t * 0.12f + q * 0.5f, 1.0f), x = x0 + (x1 - x0) * (0.5f + 0.45f * sinf(u * 2 * PI));
            if (!w.stage.Liquid((int)floorf(x / sf::TILE), p.y + p.h - 1) || w.stage.Solid((int)floorf(x / sf::TILE), p.y + p.h)) continue;   // (only in open water)
            Vector2 c = W2S({x, y1}); float dir = cosf(u * 2 * PI) > 0 ? 1.0f : -1.0f;
            DrawTri({c.x - dir * px * 0.4f, c.y}, {c.x + dir * px * 0.3f, c.y}, {c.x - dir * px * 0.25f, c.y - px * 0.6f}, Color{90, 100, 110, 255});
        }
        return true;
    }
    case sf::PK_KRAKEN: {
        if (!awake || p.prog <= 0) return true;
        float cx = p.prevOff.x;
        if (p.prog < 1) { Vector2 c = W2S({cx, y1}); DrawEllipse((int)c.x, (int)c.y, px * (0.6f + 1.2f * p.prog), px * 0.25f, ColorAlpha(Color{40, 10, 20, 255}, 0.3f + 0.4f * p.prog)); return true; }
        float rise = std::min(1.0f, (p.prog - 1) * 3);
        Vector2 b = W2S({cx, -2}), t = W2S({cx + 0.6f * sinf(S.t * 6), -2 + (y1 + 3) * rise});
        DrawLineEx(b, t, px * 1.1f + 4, INK); DrawLineEx(b, t, px * 1.1f, Color{170, 50, 60, 255});
        for (int q = 1; q < 6; q++) { Vector2 sp = Vector2Lerp(b, t, q / 6.0f); DrawCircleV({sp.x + px * 0.35f, sp.y}, px * 0.12f, Color{230, 170, 170, 255}); }
        return true;
    }
    case sf::PK_GRATE: {
        for (int q = 0; q <= p.w * 3; q++) { float xx = r.x + q * r.width / (p.w * 3); DrawLineEx({xx, r.y}, {xx, r.y + r.height}, 2, Color{40, 40, 44, 255}); }
        if (!awake || p.prog <= 0) return true;
        if (p.prog < 1) { for (int q = 0; q < 6; q++) { float hgt = fmodf(S.t * 2 + q * 0.3f, 1.0f); DrawCircleV(W2S({x0 + (x1 - x0) * (q + 0.5f) / 6, y1 + hgt * 1.2f}), px * 0.15f * (1 - hgt), ColorAlpha(Color{200, 230, 210, 255}, 0.5f + 0.4f * p.prog)); } return true; }
        float rise = std::min(1.0f, (p.prog - 1) * 4);
        Vector2 b = W2S({(x0 + x1) * 0.5f, y1}), t = W2S({(x0 + x1) * 0.5f, y1 + p.travel * sf::TILE * rise});
        DrawLineEx(b, t, r.width * 0.9f + 4, INK); DrawLineEx(b, t, r.width * 0.9f, Color{200, 214, 200, 255});
        DrawCircleV(t, r.width * 0.55f, Color{200, 214, 200, 255}); DrawCircleV({t.x - r.width * 0.2f, t.y}, px * 0.1f, Color{200, 40, 40, 255}); DrawCircleV({t.x + r.width * 0.2f, t.y}, px * 0.1f, Color{200, 40, 40, 255});
        return true;
    }
    case sf::PK_TUNA: {
        if (p.prog <= 0) return true;
        float len = x1 - x0 + 4, hx = p.dx >= 0 ? x0 - 2 + len * p.prog : x1 + 2 - len * p.prog;
        Vector2 c = W2S({hx, (y0 + y1) * 0.5f}); float L = px * 2.4f, Hh = (y1 - y0) * S.zoom * 0.35f; int d = p.dx >= 0 ? 1 : -1;
        DrawEllipse((int)c.x, (int)c.y, L * 0.5f + 2, Hh + 2, INK); DrawEllipse((int)c.x, (int)c.y, L * 0.5f, Hh, Color{60, 80, 120, 255});
        DrawTri({c.x - d * L * 0.45f, c.y}, {c.x - d * L * 0.8f, c.y - Hh}, {c.x - d * L * 0.8f, c.y + Hh}, Color{60, 80, 120, 255});
        DrawEllipse((int)c.x, (int)(c.y + Hh * 0.35f), L * 0.4f, Hh * 0.4f, Color{200, 210, 220, 255});
        DrawCircleV({c.x + d * L * 0.35f, c.y - Hh * 0.25f}, px * 0.08f + 1, INK);
        return true;
    }
    case sf::PK_SLUICE: {   // (the gate over the terrace; its water is drawn with the liquids)
        DrawRectangleV({r.x, r.y - px * 0.3f}, {r.width, px * 0.3f}, Color{170, 130, 60, 255});
        for (int q = 0; q <= p.w; q++) DrawLineEx({r.x + q * px, r.y - px * 0.3f}, {r.x + q * px, r.y}, 1.5f, INK);
        if (p.prog <= 0 && CycleOf(p) > p.period - 1.0f) for (int q = 0; q < p.w; q++) DrawCircleV({r.x + (q + 0.5f) * px, r.y + fmodf(S.t * 3 + q * 0.3f, 1.0f) * px * 0.6f}, px * 0.06f + 1, Color{140, 190, 230, 255});   // (the tell: the gate leaks)
        return true;
    }
    case sf::PK_COLUMN: {
        if (p.broken) return true;
        if (p.prog <= 0) {   // (standing: the fluting, a capital and a base over its stone)
            for (int q = 1; q < 4; q++) DrawLineEx({r.x + r.width * q / 4, r.y}, {r.x + r.width * q / 4, r.y + r.height}, 1, Color{190, 184, 170, 255});
            DrawRectangleV({r.x - px * 0.2f, r.y - px * 0.2f}, {r.width + px * 0.4f, px * 0.3f}, Color{226, 220, 206, 255}); DrawRectangleLinesEx({r.x - px * 0.2f, r.y - px * 0.2f, r.width + px * 0.4f, px * 0.3f}, 1.5f, INK);
            return true;
        }
        float u = std::min(1.0f, p.prog), a = PI / 2 * (1 - u * u);
        Vector2 piv{p.dx >= 0 ? x1 : x0, y0}, dir{(p.dx >= 0 ? 1.0f : -1.0f) * cosf(a), sinf(a)}, nrm{-dir.y, dir.x};
        float L = y1 - y0, hw = (x1 - x0) * 0.5f;
        Vector2 base = Vector2Add(piv, Vector2Scale(nrm, p.dx >= 0 ? -hw : hw)), top = Vector2Add(base, Vector2Scale(dir, L));
        DrawLineEx(W2S(base), W2S(top), (x1 - x0) * S.zoom + 4, INK); DrawLineEx(W2S(base), W2S(top), (x1 - x0) * S.zoom, WorldStone(sf::WD_ATLANTIS));
        return true;
    }
    case sf::PK_LURE: {
        if (!awake) return true;
        Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
        DrawLineEx({c.x, 0}, c, 2, ColorAlpha(Color{120, 140, 170, 255}, 0.5f));
        float pulse = 0.7f + 0.3f * sinf(S.t * 3);
        DrawCircleGradient((int)c.x, (int)c.y, p.travel * px * 0.5f, ColorAlpha(Color{180, 230, 255, 255}, 0.25f * pulse), ColorAlpha(Color{180, 230, 255, 255}, 0));
        DrawCircleV(c, px * 0.35f * pulse, Color{220, 250, 255, 255});
        return true;
    }
    case sf::PK_LOWG: {
        DrawRectangleRec(r, ColorAlpha(Color{150, 170, 230, 255}, 0.08f));
        for (int q = 0; q < p.w * p.h / 4 + 3; q++) { float fy = fmodf(q * 0.37f + S.t * 0.15f, 1.0f), fx = fmodf(q * 0.618f, 1.0f); DrawCircleV(W2S({x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy}), 1.6f, ColorAlpha(Color{200, 220, 255, 255}, 0.5f)); }
        return true;
    }
    case sf::PK_WORM: {
        if (p.prog <= 0) return true;
        float cx = p.prevOff.x;
        if (p.prog < 1) { for (int q = 0; q < 8; q++) { float hgt = fmodf(S.t * 2.5f + q * 0.21f, 1.0f); DrawCircleV(W2S({cx + (q - 3.5f) * 0.15f, y0 + hgt * (0.4f + p.prog)}), px * 0.2f * (1 - hgt) + 1, ColorAlpha(Color{150, 140, 120, 255}, 0.7f)); } return true; }
        float rise = std::min(1.0f, (p.prog - 1) * 4);
        Vector2 b = W2S({cx, y0 - 0.3f}), t = W2S({cx, y0 + (y1 - y0 + 2 * sf::TILE) * rise});
        DrawLineEx(b, t, 2 * px + 4, INK); DrawLineEx(b, t, 2 * px, Color{150, 110, 80, 255});
        for (int q = 1; q < 5; q++) { Vector2 sp = Vector2Lerp(b, t, q / 5.0f); DrawLineEx({sp.x - px, sp.y}, {sp.x + px, sp.y}, 1.5f, INK); }
        DrawCircleV(t, px * 0.7f, Color{40, 20, 20, 255});
        return true;
    }
    case sf::PK_CROWD: {
        if (!awake) return true;
        for (int q = 0; q < p.w; q += 2) {
            Vector2 c = W2S({x0 + (q + 1) * sf::TILE, y0 + 0.3f + 0.1f * sinf(S.t * 3 + q)});
            DrawCircleV(c, px * 0.45f, Color{40, 30, 26, 220}); DrawCircleV({c.x, c.y - px * 0.6f}, px * 0.3f, Color{40, 30, 26, 220});
            if ((q * 7 + (int)(S.t * 2)) % 9 == 0) DrawLineEx(c, {c.x + px * 0.3f, c.y - px * 1.1f}, px * 0.15f, Color{40, 30, 26, 220});   // (an arm up)
        }
        return true;
    }
    case sf::PK_DART: {
        Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f}; float R = std::min(r.width, r.height) * 0.48f; c.x += p.prog * sinf(S.t * 60) * 2;
        DrawCircleV(c, R + 2, INK); DrawCircleV(c, R, Color{230, 220, 190, 255}); DrawCircleV(c, R * 0.66f, Color{170, 50, 40, 255}); DrawCircleV(c, R * 0.33f, Color{40, 110, 60, 255}); DrawCircleV(c, R * 0.1f, INK);
        return true;
    }
    case sf::PK_POOL: {
        DrawRectangleRec({r.x - px * 0.15f, r.y - px * 0.15f, r.width + px * 0.3f, r.height + px * 0.15f}, Color{90, 50, 26, 255});
        DrawRectangleRec({r.x, r.y - px * 0.05f, r.width, px * 0.35f}, Color{40, 120, 70, 255});
        for (float u : {0.0f, 0.5f, 1.0f}) DrawCircleV({r.x + r.width * u, r.y}, px * 0.12f, INK);
        DrawRectangleLinesEx({r.x - px * 0.15f, r.y - px * 0.15f, r.width + px * 0.3f, r.height + px * 0.15f}, 2, INK);
        return true;
    }
    case sf::PK_BOUNCER: {
        Vector2 f = W2S({(x0 + x1) * 0.5f, y0});
        float hgt = px * 3.6f, wdt = px * 1.1f;
        DrawRectangleV({f.x - wdt * 0.5f - 2, f.y - hgt * 0.75f - 2}, {wdt + 4, hgt * 0.5f + 4}, INK); DrawRectangleV({f.x - wdt * 0.5f, f.y - hgt * 0.75f}, {wdt, hgt * 0.5f}, Color{30, 30, 34, 255});
        DrawLineEx({f.x - wdt * 0.25f, f.y - hgt * 0.25f}, {f.x - wdt * 0.25f, f.y}, px * 0.25f, Color{30, 30, 34, 255}); DrawLineEx({f.x + wdt * 0.25f, f.y - hgt * 0.25f}, {f.x + wdt * 0.25f, f.y}, px * 0.25f, Color{30, 30, 34, 255});
        Vector2 hd{f.x, f.y - hgt * 0.85f}; DrawCircleV(hd, px * 0.35f + 2, INK); DrawCircleV(hd, px * 0.35f, Color{220, 180, 150, 255});
        DrawRectangleV({hd.x - px * 0.4f, hd.y - px * 0.45f}, {px * 0.8f, px * 0.15f}, INK); DrawRectangleV({hd.x - px * 0.25f, hd.y - px * 0.75f}, {px * 0.5f, px * 0.32f}, INK);   // (the bowler)
        if (p.hold >= 0) { DrawLineEx({f.x, f.y - hgt * 0.6f}, {f.x - px * 1.2f, f.y - hgt * 0.75f}, px * 0.22f, Color{30, 30, 34, 255}); DrawTextCentered("Oi!", hd.x, hd.y - px * 1.6f, (int)std::max(12.0f, px * 0.6f), Color{180, 40, 30, 255}); }
        else DrawLineEx({f.x - wdt * 0.45f, f.y - hgt * 0.55f}, {f.x + wdt * 0.45f, f.y - hgt * 0.55f}, px * 0.22f, Color{50, 50, 56, 255});   // (arms folded)
        return true;
    }
    case sf::PK_DOG: {
        Vector2 c = W2S({p.prevOff.x, y0 + 0.35f}); int d = p.dy >= 0 ? 1 : -1; float b = sinf(p.prog * 22) * px * 0.06f;
        DrawEllipse((int)c.x, (int)(c.y + b), px * 0.55f + 2, px * 0.25f + 2, INK); DrawEllipse((int)c.x, (int)(c.y + b), px * 0.55f, px * 0.25f, Color{170, 130, 80, 255});
        Vector2 hd{c.x + d * px * 0.55f, c.y - px * 0.2f + b}; DrawCircleV(hd, px * 0.22f + 2, INK); DrawCircleV(hd, px * 0.22f, Color{170, 130, 80, 255}); DrawCircleV({hd.x + d * px * 0.08f, hd.y - px * 0.05f}, 1.5f, INK);
        for (int q = 0; q < 4; q++) { float lx = c.x + (q - 1.5f) * px * 0.3f, sw = sinf(p.prog * 22 + q * 1.6f) * px * 0.12f; DrawLineEx({lx, c.y + px * 0.15f}, {lx + sw, c.y + px * 0.5f}, 2, INK); }
        DrawLineEx({c.x - d * px * 0.5f, c.y - px * 0.1f}, {c.x - d * px * 0.8f, c.y - px * 0.35f + sinf(S.t * 18) * px * 0.1f}, 2, INK);   // (the tail)
        return true;
    }
    }
    return false;
}
// water, brine, a live sluice and the rising tide: over the sticks, translucent, with a moving surface line
void DrawLiquids() {
    const sf::World& w = S.M.w; const sf::Stage& s = w.stage; float px = S.zoom * sf::TILE;
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) {
        uint8_t k = s.At(x, y); if (k != sf::T_WATER && k != sf::T_BRINE) continue;
        Vector2 a = W2S({x * sf::TILE, (y + 1) * sf::TILE}), b = W2S({(x + 1) * sf::TILE, y * sf::TILE});
        a = {roundf(a.x), roundf(a.y)}; b = {roundf(b.x), roundf(b.y)};   // (exact edges: no seams where the tiles meet)
        DrawRectangleV(a, {b.x - a.x, b.y - a.y}, k == sf::T_BRINE ? Color{30, 60, 50, 170} : Color{60, 120, 160, 120});
        if (!s.Liquid(x, y + 1)) { float o = sinf(S.t * 2 + x * 0.9f) * px * 0.06f; DrawLineEx({a.x, a.y + o}, {a.x + px, a.y - o}, 2, k == sf::T_BRINE ? Color{120, 200, 160, 220} : Color{230, 245, 255, 220}); }
    }
    for (const auto& p : s.pieces) if (p.kind == sf::PK_SLUICE && p.prog > 0) { Vector2 a = W2S({p.x * sf::TILE, (p.y + p.h) * sf::TILE}); DrawRectangleV(a, {p.w * px, p.h * px}, Color{70, 130, 170, 120}); DrawLineEx(a, {a.x + p.w * px, a.y}, 2, Color{230, 245, 255, 220}); }
    if ((s.world == sf::WD_REEF || s.world == sf::WD_ATLANTIS) && w.wallY > -2) {
        float y = W2S({0, w.wallY}).y;
        DrawRectangle(0, (int)y, SCREEN_W, SCREEN_H - (int)y, s.world == sf::WD_REEF ? Color{50, 110, 150, 140} : Color{60, 90, 110, 150});
        for (int x = 0; x < SCREEN_W; x += 6) DrawLineEx({(float)x, y + 4 * sinf(x * 0.03f + S.t * 3)}, {(float)x + 6, y + 4 * sinf((x + 6) * 0.03f + S.t * 3)}, 3, Color{230, 245, 255, 230});
    }
}
// the walls that aren't water: the Nautilus's flood, the Cave's ceiling, the Void's abyss, closing time in the Salon
void DrawWallFx() {
    const sf::World& w = S.M.w; const sf::Stage& s = w.stage;
    if (s.world == sf::WD_NAUTILUS && w.wallY > -2) {
        float y = W2S({0, w.wallY}).y;
        DrawRectangle(0, (int)y, SCREEN_W, SCREEN_H - (int)y, Color{60, 92, 120, 200});
        for (int x = 0; x < SCREEN_W; x += 6) DrawLineEx({(float)x, y + 4 * sinf(x * 0.03f + S.t * 3)}, {(float)x + 6, y + 4 * sinf((x + 6) * 0.03f + S.t * 3)}, 3, INK);
    }
    if (s.world == sf::WD_CAVE && w.ceilY < 1e8f) {
        float y = W2S({0, w.ceilY}).y;
        DrawRectangle(0, 0, SCREEN_W, (int)y, Color{70, 58, 50, 255});
        for (int x = 0; x < SCREEN_W; x += 24) { float d = 10 + 8 * sinf(x * 0.7f); DrawTri({(float)x, y}, {(float)x + 24, y}, {(float)x + 12, y + d}, Color{70, 58, 50, 255}); DrawLineEx({(float)x, y}, {(float)x + 12, y + d}, 2, INK); DrawLineEx({(float)x + 12, y + d}, {(float)x + 24, y}, 2, INK); }
    }
    if ((s.world == sf::WD_VOID || s.world == sf::WD_SALON) && w.sideX > -5) {
        float edge = w.wallSide > 0 ? s.Width() - w.sideX : w.sideX;
        float x = W2S({edge, 0}).x;
        Rectangle dark = w.wallSide > 0 ? Rectangle{x, 0, SCREEN_W - x, (float)SCREEN_H} : Rectangle{0, 0, x, (float)SCREEN_H};
        DrawRectangleRec(dark, s.world == sf::WD_VOID ? Color{8, 10, 18, 235} : Color{20, 12, 8, 220});
        if (s.world == sf::WD_SALON) {   // (the bouncer at the edge of the dark, sweeping the room)
            float px = S.zoom * sf::TILE; Vector2 f{x, W2S({0, 1.2f}).y};
            DrawRectangleV({f.x - px * 0.7f, f.y - px * 3.0f}, {px * 1.4f, px * 2.0f}, Color{30, 30, 34, 255}); DrawCircleV({f.x, f.y - px * 3.4f}, px * 0.4f, Color{220, 180, 150, 255});
            DrawLineEx({f.x, f.y - px * 2.6f}, {f.x - w.wallSide * px * 1.6f, f.y - px * 2.2f}, px * 0.3f, Color{30, 30, 34, 255});
            DrawTextCentered("Closing time!", f.x - w.wallSide * px * 2, f.y - px * 4.6f, (int)std::max(14.0f, px * 0.7f), Color{240, 220, 160, 255});
        } else for (int q = 0; q < 12; q++) { float yy = fmodf(q * 0.37f + S.t * 0.6f, 1.0f); DrawCircleV({x + w.wallSide * (q % 3) * 6.0f, SCREEN_H * yy}, 2, Color{140, 160, 200, 200}); }   // (rocks tumbling into the abyss)
    }
}
// a hazard's projectile: shrapnel, a bottle, a dart, a pool ball, a drop of acid
bool DrawHazardBullet(const sf::Bullet& b) {
    if (b.hazard == -1) return false;
    Vector2 p = W2S(b.p); float px = S.zoom * sf::TILE; Vector2 d = Vector2Normalize(b.v);
    switch (b.hazard) {
        case -2: DrawTri({p.x + d.x * px * 0.2f, p.y - d.y * px * 0.2f}, {p.x - d.y * px * 0.08f, p.y - d.x * px * 0.08f}, {p.x + d.y * px * 0.08f, p.y + d.x * px * 0.08f}, Color{210, 170, 250, 255}); break;
        case sf::PK_CROWD: { float an = S.t * 12; Vector2 e{cosf(an) * px * 0.3f, sinf(an) * px * 0.3f}; DrawLineEx(Vector2Subtract(p, e), Vector2Add(p, e), px * 0.18f + 2, INK); DrawLineEx(Vector2Subtract(p, e), Vector2Add(p, e), px * 0.18f, Color{70, 130, 70, 255}); break; }
        case sf::PK_DART: DrawLineEx({p.x - d.x * px * 0.5f, p.y + d.y * px * 0.5f}, p, 2.5f, INK); DrawTri({p.x - d.x * px * 0.5f, p.y + d.y * px * 0.5f}, {p.x - d.x * px * 0.65f, p.y - px * 0.1f}, {p.x - d.x * px * 0.65f, p.y + px * 0.1f}, Color{200, 50, 40, 255}); break;
        case sf::PK_POOL: { static const Color C[4] = {{230, 200, 40, 255}, {40, 70, 180, 255}, {180, 40, 40, 255}, {30, 30, 30, 255}}; DrawCircleV(p, px * 0.22f + 2, INK); DrawCircleV(p, px * 0.22f, C[((int)(b.p.x * 3 + b.age * 10)) & 3]); break; }
        case sf::PK_DRIP: DrawCircleV(p, px * 0.1f + 1, Color{150, 220, 80, 255}); break;
        default: DrawCircleV(p, 3, INK); break;
    }
    return true;
}
const char* WallLine(int world) {
    switch (world) { case sf::WD_CAVE: return "The ceiling is coming down!"; case sf::WD_REEF: return "The tide is coming in!"; case sf::WD_ATLANTIS: return "The city is sinking!"; case sf::WD_VOID: return "The abyss is widening!"; case sf::WD_SALON: return "Closing time!"; default: return "The bulkheads are flooding!"; }
}
}  // namespace
