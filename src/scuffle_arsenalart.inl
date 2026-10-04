// Scuffle stage 6: how the full arsenal looks (doc pp. 5-8, 14, 20), included by scuffle_game.cpp after the world art.
// Every weapon has its own silhouette in hand and on the floor; the things they leave (bees, snakes, fish, the black
// hole, portals, traps, turrets, mines, peels, decoys, springs, charges, beams, the alley dog, the hot potato); what they
// do to a stick (flames, a block of ice, a bubble, a net, the gravity gun's glow, a bear trap's jaws); gear in use (the
// hook's line, the shield, the jetpack's flame, the parachute); and the screen's ink, flash, dark and event banners.

namespace {
Color Party(int i) { static const Color C[6] = {{230, 70, 80, 255}, {250, 200, 60, 255}, {80, 190, 110, 255}, {70, 140, 230, 255}, {200, 90, 220, 255}, {250, 140, 50, 255}}; return C[((i % 6) + 6) % 6]; }
// a melee weapon, a thrown thing, or one of the strange guns: true if drawn here (else the stage-2 drawing)
bool DrawWeaponSpecial(const sf::WeaponDef& d, Vector2 a, Vector2 b, Vector2 dir, Vector2 n, float px, float alpha, int count) {
    Color ink = ColorAlpha(INK, alpha);
    auto line = [&](Vector2 p, Vector2 q, float w, Color c) { DrawLineEx(p, q, w + 2.5f, ink); DrawLineEx(p, q, w, ColorAlpha(c, alpha)); };
    auto dot = [&](Vector2 c, float r, Color col) { DrawCircleV(c, r + 1.5f, ink); DrawCircleV(c, r, ColorAlpha(col, alpha)); };
    Color wood{140, 96, 54, 255}, steel{150, 156, 160, 255}, brass{200, 160, 70, 255};
    float L = Vector2Distance(a, b);
    Vector2 at = [&](float u) { return Vector2Add(a, Vector2Scale(dir, L * u)); }(0);
    auto P = [&](float u) { return Vector2Add(a, Vector2Scale(dir, L * u)); };
    (void)at;
    const std::string& k = d.key;
    if (d.kind == "melee") {
        if (k == "axe") { line(a, P(1.0f), px * 1.0f, wood); DrawTri(P(0.75f), Vector2Add(P(1.0f), Vector2Scale(n, px * 4)), Vector2Add(P(0.7f), Vector2Scale(n, px * 4)), ColorAlpha(steel, alpha)); DrawLineEx(Vector2Add(P(1.0f), Vector2Scale(n, px * 4)), Vector2Add(P(0.7f), Vector2Scale(n, px * 4)), 2, ink); return true; }
        if (k == "trident") { Vector2 t = P(1.6f); line(Vector2Subtract(a, Vector2Scale(dir, px * 6)), t, px * 0.8f, brass); for (float s : {-1.0f, 0.0f, 1.0f}) line(Vector2Add(t, Vector2Scale(n, s * px * 2)), Vector2Add(Vector2Add(t, Vector2Scale(n, s * px * 2)), Vector2Scale(dir, px * 4)), px * 0.6f, steel); line(Vector2Add(t, Vector2Scale(n, -px * 2)), Vector2Add(t, Vector2Scale(n, px * 2)), px * 0.6f, steel); return true; }
        if (k == "poolcue") { float len = count > 0 ? 0.8f : 1.55f; line(Vector2Subtract(a, Vector2Scale(dir, px * 4)), P(len), px * 0.7f, Color{220, 190, 130, 255}); dot(P(len), px * 0.5f, Color{60, 100, 170, 255}); return true; }
        if (k == "bat") { line(a, P(0.4f), px * 0.8f, wood); line(P(0.35f), P(1.15f), px * 1.6f, Color{170, 120, 70, 255}); return true; }
        if (k == "sledge") { line(Vector2Subtract(a, Vector2Scale(dir, px * 3)), P(1.15f), px * 0.9f, wood); Vector2 h = P(1.15f); DrawRectanglePro({h.x, h.y, px * 3.6f, px * 6}, {px * 1.8f, px * 3}, atan2f(dir.y, dir.x) * RAD2DEG, ColorAlpha(Color{90, 90, 96, 255}, alpha)); return true; }
        if (k == "whip") { Vector2 p0 = a; for (int s = 1; s <= 10; s++) { float u = s / 10.0f; Vector2 p1 = Vector2Add(P(u * 2.2f), Vector2Scale(n, sinf(u * 6 + S.t * 8) * px * 2 * u)); DrawLineEx(p0, p1, std::max(1.5f, px * (1.2f - u)), ColorAlpha(Color{110, 70, 40, 255}, alpha)); p0 = p1; } return true; }
        if (k == "fish") { Vector2 c = P(0.75f); DrawEllipse((int)c.x, (int)c.y, px * 5 + 2, px * 2.2f + 2, ink); DrawEllipse((int)c.x, (int)c.y, px * 5, px * 2.2f, ColorAlpha(Color{120, 160, 180, 255}, alpha)); DrawTri(P(0.25f), Vector2Add(P(0.1f), Vector2Scale(n, px * 2.5f)), Vector2Subtract(P(0.1f), Vector2Scale(n, px * 2.5f)), ColorAlpha(Color{120, 160, 180, 255}, alpha)); DrawCircleV(P(1.1f), px * 0.6f, ink); return true; }
        if (k == "oar") { line(Vector2Subtract(a, Vector2Scale(dir, px * 4)), P(1.2f), px * 0.8f, wood); Vector2 c = P(1.25f); DrawEllipse((int)c.x, (int)c.y, px * 3, px * 1.6f, ColorAlpha(wood, alpha)); return true; }
        if (k == "teslagaff") { line(a, P(1.0f), px * 0.9f, steel); Vector2 h = P(1.0f); DrawRing(Vector2Add(h, Vector2Scale(n, px * 1.5f)), px * 1.2f, px * 1.9f, 0, 270, 10, ColorAlpha(steel, alpha)); if (fmodf(S.t * 7, 1.0f) < 0.5f) DrawLineEx(h, Vector2Add(h, {px * 3 * sinf(S.t * 40), px * 3 * cosf(S.t * 33)}), 1.5f, Color{170, 220, 255, 255}); return true; }
        return false;
    }
    if (d.kind == "thrown") {
        Vector2 c = P(0.4f); float r = px * 2.2f;
        if (k == "limpet" || k == "mine") { dot(c, r, k == "mine" ? Color{70, 70, 74, 255} : Color{110, 120, 90, 255}); DrawCircleV(c, r * 0.3f, ColorAlpha(Color{220, 60, 40, 255}, alpha)); }
        else if (k == "sticky") { dot(c, r, Color{90, 160, 70, 255}); }
        else if (k == "inkbomb") { dot(c, r, Color{30, 30, 36, 255}); DrawLineEx(Vector2Add(c, {0, -r}), Vector2Add(c, {r * 0.5f, -r * 1.6f}), 2, ink); }
        else if (k == "flashbang") { DrawRectanglePro({c.x, c.y, r * 1.2f, r * 2}, {r * 0.6f, r}, atan2f(dir.y, dir.x) * RAD2DEG, ColorAlpha(Color{180, 180, 170, 255}, alpha)); }
        else if (k == "beartrap") { DrawRing(c, r * 0.7f, r, 180, 360, 12, ColorAlpha(steel, alpha)); for (int i = 0; i < 5; i++) DrawLineEx(Vector2Add(c, {(i - 2) * r * 0.35f, -r * 0.2f}), Vector2Add(c, {(i - 2) * r * 0.35f, -r * 0.7f}), 1.5f, ink); }
        else if (k == "turret") { DrawRectangleV({c.x - r, c.y - r}, {2 * r, 2 * r}, ColorAlpha(Color{120, 124, 110, 255}, alpha)); DrawRectangleLinesEx({c.x - r, c.y - r, 2 * r, 2 * r}, 2, ink); }
        else if (k == "banana") { DrawRing(c, r * 0.6f, r, 200, 340, 12, ColorAlpha(Color{240, 210, 70, 255}, alpha)); }
        else if (k == "beehive") { dot(c, r * 1.2f, Color{210, 170, 70, 255}); for (int i = -1; i <= 1; i++) DrawLineEx({c.x - r, c.y + i * r * 0.4f}, {c.x + r, c.y + i * r * 0.4f}, 1.5f, ink); }
        else if (k == "snakejar") { DrawRectangleV({c.x - r * 0.8f, c.y - r}, {r * 1.6f, r * 2}, ColorAlpha(Color{170, 200, 190, 160}, alpha)); DrawRectangleLinesEx({c.x - r * 0.8f, c.y - r, r * 1.6f, r * 2}, 1.5f, ink); DrawRing(c, r * 0.3f, r * 0.5f, 0, 300, 10, ColorAlpha(Color{90, 160, 70, 255}, alpha)); }
        else if (k == "bottle") { line(P(0.1f), P(0.55f), px * 2, Color{70, 130, 70, 255}); line(P(0.5f), P(0.75f), px * 0.9f, Color{70, 130, 70, 255}); }
        else dot(c, r, Color{150, 140, 120, 255});
        if (count > 1) DrawTextCentered(TextFormat("x%d", count), c.x, c.y - r - 14, 11, ink);
        return true;
    }
    // the strange guns: a body in their colour and a muzzle that says what they do
    if (d.special.empty() && k != "confetti" && k != "speargun") return false;
    Color body = k == "bees" ? Color{220, 180, 50, 255} : k == "flamethrower" ? Color{170, 60, 40, 255} : k == "icegun" ? Color{140, 200, 230, 255} : k == "tesla" ? Color{90, 120, 160, 255}
               : k == "snakegun" ? Color{80, 140, 70, 255} : k == "blackhole" ? Color{60, 40, 90, 255} : k == "bubble" ? Color{230, 150, 200, 255} : k == "gravitygun" ? Color{80, 180, 190, 255}
               : k == "laser" ? Color{200, 60, 60, 255} : k == "chum" ? Color{140, 110, 80, 255} : k == "netgun" ? Color{150, 140, 100, 255} : k == "boomerang" ? wood
               : k == "confetti" ? Party((int)(S.t * 4)) : k == "portal" ? Color{240, 140, 50, 255} : k == "flare" ? Color{230, 110, 40, 255} : steel;
    line(Vector2Subtract(a, Vector2Scale(dir, px * 3)), P(0.85f), px * 1.8f, body);
    line(P(0.4f), Vector2Add(P(0.4f), Vector2Scale(n, -px * 3.5f)), px * 1.1f, wood);
    Vector2 m = P(0.9f);
    if (k == "bees") for (float u : {0.2f, 0.45f, 0.7f}) DrawLineEx(Vector2Add(P(u), Vector2Scale(n, px * 1.2f)), Vector2Subtract(P(u), Vector2Scale(n, px * 1.2f)), 2, ink);
    if (k == "flamethrower") { dot(Vector2Add(P(0.25f), Vector2Scale(n, px * 2.2f)), px * 1.6f, Color{200, 80, 50, 255}); DrawCircleV(m, px * 0.8f, ColorAlpha(Color{255, 170, 60, 255}, 0.6f + 0.4f * sinf(S.t * 30))); }
    if (k == "tesla") for (int i = 0; i < 4; i++) DrawRing(P(0.3f + i * 0.12f), px * 1.0f, px * 1.5f, 0, 360, 10, ColorAlpha(brass, alpha));
    if (k == "blackhole") { DrawCircleV(m, px * 1.6f + 2, ink); DrawCircleV(m, px * 1.6f, BLACK); DrawRing(m, px * 1.6f, px * 2.0f, 0, 360, 16, ColorAlpha(Color{170, 120, 230, 255}, 0.7f)); }
    if (k == "bubble") DrawCircleLines((int)m.x, (int)m.y, px * 1.4f, ColorAlpha(Color{250, 230, 250, 255}, alpha));
    if (k == "gravitygun") for (float s : {-1.0f, 1.0f}) line(m, Vector2Add(Vector2Add(m, Vector2Scale(dir, px * 2)), Vector2Scale(n, s * px * 1.6f)), px * 0.5f, body);
    if (k == "laser") DrawCircleV(m, px * 0.7f, ColorAlpha(Color{255, 80, 80, 255}, 0.7f + 0.3f * sinf(S.t * 20)));
    if (k == "netgun") DrawCircleLines((int)m.x, (int)m.y, px * 1.6f, ink);
    if (k == "boomerang") DrawRing(P(1.0f), px * 1.2f, px * 2.0f, 0, 140, 10, ColorAlpha(wood, alpha));
    if (k == "portal") { DrawCircleV(m, px * 0.9f, ColorAlpha(Color{90, 160, 250, 255}, alpha)); }
    if (k == "speargun") line(P(0.85f), P(1.25f), px * 0.5f, steel);
    return true;
}
const sf::Thing* PortalPair(const sf::Thing& p) { for (const auto& o : S.M.w.things) if (&o != &p && o.alive && o.kind == sf::TH_PORTAL && o.owner == p.owner) return &o; return nullptr; }
void DrawThings() {
    const sf::World& w = S.M.w; float px = S.zoom * sf::TILE;
    for (const auto& th : w.things) {
        if (!th.alive) continue;
        Vector2 c = W2S(th.p);
        switch (th.kind) {
        case sf::TH_SWARM: for (int i = 0; i < 14; i++) { float a = S.t * (5 + i % 4) + i * 1.7f; Vector2 b{c.x + cosf(a) * px * (0.5f + 0.3f * sinf(i * 3.1f)), c.y + sinf(a * 1.3f) * px * 0.45f}; DrawEllipse((int)b.x, (int)b.y, 3, 2, Color{230, 190, 40, 255}); DrawLineEx({b.x - 2, b.y}, {b.x + 2, b.y}, 1, INK); } break;
        case sf::TH_SNAKE: { int d = th.v.x >= 0 ? 1 : -1; Vector2 p0 = {c.x - d * px * 0.9f, c.y}; for (int i = 1; i <= 8; i++) { float u = i / 8.0f; Vector2 p1{c.x - d * px * 0.9f * (1 - u), c.y - px * 0.1f - sinf(u * 9 + S.t * 10) * px * 0.08f}; DrawLineEx(p0, p1, px * 0.18f + 2, INK); DrawLineEx(p0, p1, px * 0.18f, Color{90, 150, 60, 255}); p0 = p1; } DrawCircleV(p0, px * 0.14f, Color{90, 150, 60, 255}); break; }
        case sf::TH_FISH: { int d = th.v.x >= 0 ? 1 : -1; DrawEllipse((int)c.x, (int)c.y, px * 0.45f + 2, px * 0.2f + 2, INK); DrawEllipse((int)c.x, (int)c.y, px * 0.45f, px * 0.2f, Color{110, 150, 170, 255}); DrawTri({c.x - d * px * 0.4f, c.y}, {c.x - d * px * 0.7f, c.y - px * 0.2f}, {c.x - d * px * 0.7f, c.y + px * 0.2f}, Color{110, 150, 170, 255}); break; }
        case sf::TH_HOLE: { float r = px * 0.9f; for (int i = 0; i < 3; i++) DrawRing(c, r * (1.2f + i * 0.6f + fmodf(S.t, 0.6f)), r * (1.3f + i * 0.6f + fmodf(S.t, 0.6f)), 0, 360, 24, ColorAlpha(Color{170, 120, 230, 255}, 0.35f - i * 0.1f)); DrawCircleV(c, r, BLACK); DrawRing(c, r, r * 1.15f, 0, 360, 24, Color{200, 150, 255, 255}); break; }
        case sf::TH_CHUM: for (int i = 0; i < 10; i++) DrawCircleV({c.x + sinf(i * 2.3f + S.t) * px * 0.8f, c.y + cosf(i * 1.7f + S.t * 0.7f) * px * 0.5f}, px * 0.12f, ColorAlpha(Color{150, 50, 40, 255}, 0.6f)); break;
        case sf::TH_PORTAL: { Color col = th.a < 0.5f ? Color{240, 140, 50, 255} : Color{70, 150, 250, 255}; float ang = atan2f(-th.q.y, th.q.x); Vector2 sideV{-sinf(ang), -cosf(ang)}; (void)sideV;
            DrawEllipse((int)c.x, (int)c.y, fabsf(th.q.x) > 0.5f ? px * 0.25f : px * 0.9f, fabsf(th.q.x) > 0.5f ? px * 0.9f : px * 0.25f, ColorAlpha(col, 0.85f));
            if (!PortalPair(th)) DrawTextCentered("?", c.x, c.y - 8, 14, col); break; }
        case sf::TH_TRAP: { DrawRing({c.x, c.y}, px * 0.25f, px * 0.4f, 180, 360, 12, Color{150, 156, 160, 255}); for (int i = 0; i < 5; i++) DrawLineEx({c.x + (i - 2) * px * 0.15f, c.y}, {c.x + (i - 2) * px * 0.15f, c.y - px * 0.2f}, 1.5f, INK); break; }
        case sf::TH_TURRET: { DrawRectangleV({c.x - px * 0.35f, c.y - px * 0.5f}, {px * 0.7f, px * 0.5f}, Color{120, 124, 110, 255}); DrawRectangleLinesEx({c.x - px * 0.35f, c.y - px * 0.5f, px * 0.7f, px * 0.5f}, 2, INK); Vector2 e{c.x, c.y - px * 0.6f}; DrawLineEx(e, {e.x + cosf(th.a) * px * 0.7f, e.y - sinf(th.a) * px * 0.7f}, px * 0.15f + 2, INK); DrawCircleV(e, px * 0.2f, Color{90, 94, 84, 255}); if (th.life < 2 && fmodf(S.t * 6, 1.0f) < 0.5f) DrawCircleV(e, 3, RED); break; }
        case sf::TH_MINE: { DrawEllipse((int)c.x, (int)c.y, px * 0.35f, px * 0.12f, Color{70, 70, 74, 255}); DrawCircleV({c.x, c.y - px * 0.1f}, 2.5f, th.cool <= 0 && fmodf(S.t * 3, 1.0f) < 0.5f ? Color{255, 60, 40, 255} : Color{90, 30, 20, 255}); break; }
        case sf::TH_PEEL: DrawRing({c.x, c.y}, px * 0.15f, px * 0.3f, 180, 360, 10, Color{240, 210, 70, 255}); break;
        case sf::TH_DECOY: { float h = px * 1.6f; DrawLineEx({c.x, c.y}, {c.x, c.y - h * 0.6f}, 4, Color{170, 150, 120, 255}); DrawCircleV({c.x, c.y - h * 0.75f}, px * 0.25f, Color{230, 220, 200, 255}); DrawCircleLines((int)c.x, (int)(c.y - h * 0.75f), px * 0.25f, INK); DrawLineEx({c.x - px * 0.4f, c.y - h * 0.5f}, {c.x + px * 0.4f, c.y - h * 0.5f}, 3, Color{170, 150, 120, 255}); break; }
        case sf::TH_SPRING: { for (int i = 0; i < 4; i++) DrawLineEx({c.x - px * 0.4f, c.y - i * px * 0.08f}, {c.x + px * 0.4f, c.y - (i + 0.5f) * px * 0.08f}, 2, Color{150, 156, 160, 255}); DrawRectangleV({c.x - px * 0.5f, c.y - px * 0.38f}, {px, px * 0.08f}, Color{200, 60, 50, 255}); break; }
        case sf::TH_STUCK: { DrawCircleV(c, px * 0.15f + 1.5f, INK); DrawCircleV(c, px * 0.15f, Color{110, 120, 90, 255}); if (fmodf(S.t * (th.life < 0.6f ? 12 : 4), 1.0f) < 0.5f) DrawCircleV(c, px * 0.06f + 1, RED); break; }
        case sf::TH_BEAM: { Vector2 e = W2S(th.q); Color col = th.a < 1.5f ? Color{170, 220, 255, 255} : th.a < 2.5f ? Color{120, 230, 230, 160} : Color{255, 70, 60, 255};
            if (th.a < 1.5f) { Vector2 p0 = c; for (int i = 1; i <= 8; i++) { Vector2 p1 = Vector2Lerp(c, e, i / 8.0f); if (i < 8) { p1.x += (sinf(S.t * 90 + i * 7) * 6); p1.y += cosf(S.t * 80 + i * 5) * 6; } DrawLineEx(p0, p1, 2.5f, col); p0 = p1; } }
            else DrawLineEx(c, e, th.a < 2.5f ? 6.0f : 3.0f, col); break; }
        case sf::TH_DOG: { Vector2 d = W2S({th.p.x, th.p.y + 0.35f}); float b = sinf(th.a * 25) * px * 0.06f; DrawEllipse((int)d.x, (int)(d.y + b), px * 0.55f + 2, px * 0.25f + 2, INK); DrawEllipse((int)d.x, (int)(d.y + b), px * 0.55f, px * 0.25f, Color{120, 100, 80, 255}); DrawCircleV({d.x + px * 0.55f, d.y - px * 0.2f + b}, px * 0.22f, Color{120, 100, 80, 255}); break; }
        case sf::TH_POTATO: { float hot = th.life < 3 ? 1 : 0.4f; DrawCircleV(c, px * 0.32f + 2, INK); DrawCircleV(c, px * 0.32f, ColorLerp(Color{150, 110, 60, 255}, Color{240, 60, 30, 255}, hot * (0.5f + 0.5f * sinf(S.t * (th.life < 3 ? 25 : 8))))); DrawTextCentered(TextFormat("%.0f", ceilf(th.life)), c.x, c.y - px * 0.9f, 14, Color{240, 60, 30, 255}); break; }
        default: break;
        }
    }
}
// burning wood
void DrawFires() {
    const sf::World& w = S.M.w; float px = S.zoom * sf::TILE;
    for (int i = 0; i < (int)w.fireT.size(); i++) {
        if (w.fireT[i] <= 0) continue;
        Vector2 a = W2S({(i % w.stage.w) * sf::TILE, (i / w.stage.w + 1) * sf::TILE});
        for (int q = 0; q < 4; q++) { float h = fmodf(S.t * 3 + q * 0.27f + i * 0.13f, 1.0f); DrawCircleV({a.x + px * (0.2f + 0.2f * q), a.y - h * px * 0.9f}, px * 0.2f * (1 - h) + 1, ColorAlpha(q % 2 ? Color{255, 170, 50, 255} : Color{240, 80, 30, 255}, 0.8f)); }
    }
}
// what's happening to a stick (drawn over it)
void DrawStickStatus(const sf::Stick& k) {
    if (!k.present) return;
    float px = S.zoom * sf::TILE;
    Vector2 pel = W2S(k.pt[sf::J_PELVIS].p), head = W2S(k.pt[sf::J_HEAD].p), feet = W2S(k.pos);
    if (k.burnT > 0) for (int q = 0; q < 6; q++) { float h = fmodf(S.t * 4 + q * 0.17f, 1.0f); Vector2 c{pel.x + sinf(q * 2.1f) * px * 0.3f, pel.y - h * px * 1.6f + px * 0.5f}; DrawCircleV(c, px * 0.22f * (1 - h) + 1, ColorAlpha(q % 2 ? Color{255, 170, 50, 255} : Color{240, 80, 30, 255}, 0.8f)); }
    if (k.frozenT > 0) { Rectangle r{std::min(head.x, feet.x) - px * 0.6f, head.y - px * 0.5f, px * 1.2f + fabsf(head.x - feet.x), feet.y - head.y + px * 0.5f}; DrawRectangleRec(r, Color{180, 220, 240, 130}); DrawRectangleLinesEx(r, 2, Color{90, 140, 170, 255}); DrawLineEx({r.x + 4, r.y + 6}, {r.x + r.width * 0.4f, r.y + 2}, 2, WHITE); }
    if (k.bubbleT > 0) { DrawCircleLines((int)pel.x, (int)pel.y, px * 1.6f, Color{250, 230, 250, 220}); DrawCircleV({pel.x - px * 0.6f, pel.y - px * 0.8f}, px * 0.2f, ColorAlpha(WHITE, 0.6f)); }
    if (k.netT > 0) for (int i = -2; i <= 2; i++) { DrawLineEx({pel.x + i * px * 0.35f - px * 0.6f, head.y - px * 0.3f}, {pel.x + i * px * 0.35f + px * 0.6f, feet.y}, 1.5f, Color{120, 100, 60, 255}); DrawLineEx({pel.x + i * px * 0.35f + px * 0.6f, head.y - px * 0.3f}, {pel.x + i * px * 0.35f - px * 0.6f, feet.y}, 1.5f, Color{120, 100, 60, 255}); }
    if (k.gravT > 0) DrawCircleGradient((int)pel.x, (int)pel.y, px * 1.8f, ColorAlpha(Color{120, 230, 230, 255}, 0.35f), ColorAlpha(Color{120, 230, 230, 255}, 0));
    if (k.trapT > 0) { DrawRing({feet.x, feet.y}, px * 0.25f, px * 0.42f, 180, 360, 12, Color{150, 156, 160, 255}); }
    // gear in use
    if (k.alive && k.gear == sf::GR_SHIELD) { Vector2 h = W2S(k.pt[sf::J_HAND_L].p); DrawCircleV(h, px * 0.42f + 2, INK); DrawCircleV(h, px * 0.42f, k.in.gear ? Color{200, 160, 70, 255} : Color{150, 120, 60, 255}); }
    if (k.alive && k.gear == sf::GR_JETPACK) { Vector2 n = W2S(k.pt[sf::J_NECK].p); DrawRectangleV({n.x - k.face * px * 0.45f - px * 0.15f, n.y}, {px * 0.3f, px * 0.6f}, Color{140, 140, 150, 255}); if (k.in.gear && k.gearFuel > 0) for (int q = 0; q < 4; q++) DrawCircleV({n.x - k.face * px * 0.45f, n.y + px * (0.7f + q * 0.25f)}, px * (0.2f - q * 0.04f), ColorAlpha(q % 2 ? Color{255, 200, 80, 255} : Color{240, 90, 30, 255}, 0.9f)); }
    if (k.alive && k.gear == sf::GR_PARACHUTE && k.in.gear && !k.grounded && k.vel.y <= -2.5f) { Vector2 top{head.x, head.y - px * 1.6f}; float r = px * 1.3f; DrawCircleSector(top, r + 2, 180, 360, 20, INK); DrawCircleSector(top, r, 180, 360, 20, Color{230, 120, 90, 255}); for (float u : {-1.0f, 0.0f, 1.0f}) DrawLineEx({top.x + u * r, top.y}, head, 1.2f, INK); }
    if (k.alive && k.hookOn) { DrawLineEx(W2S(k.pt[sf::J_HAND_R].p), W2S(k.hook), 2, Color{120, 100, 70, 255}); DrawCircleV(W2S(k.hook), 3, INK); }
}
// the screen: ink, a flash, the dark (Lights Out, Blackout), and an event's banner
float gEventBanner = 0; int gEventKind = -1;
void DrawScreenFx(float dt) {
    const sf::World& w = S.M.w;
    if (w.lightsT > 0 || w.Mut(sf::MU_BLACKOUT)) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{4, 4, 8, 236});
        // what glows: muzzle flashes and shots, fire, beams, hazards' tells, and a Loud Mouth
        for (const auto& b : w.bullets) DrawCircleGradient((int)W2S(b.p).x, (int)W2S(b.p).y, 26, ColorAlpha(Color{255, 220, 150, 255}, 0.6f), ColorAlpha(Color{255, 220, 150, 255}, 0));
        for (const auto& k : w.sticks) if (k.alive && k.present && (k.burnT > 0 || k.trinket == sf::TK_LOUD_MOUTH || k.fireCool > 0.05f)) { Vector2 c = W2S(k.pt[sf::J_NECK].p); DrawCircleGradient((int)c.x, (int)c.y, 70, ColorAlpha(Color{255, 210, 140, 255}, 0.45f), ColorAlpha(Color{255, 210, 140, 255}, 0)); DrawCircleV(c, 3, StickColor(k.id)); }
        for (const auto& p : w.stage.pieces) if (p.prog > 0) { Vector2 c = W2S({(p.x + p.w * 0.5f) * sf::TILE, (p.y + p.h * 0.5f) * sf::TILE}); DrawCircleGradient((int)c.x, (int)c.y, 60, ColorAlpha(Color{255, 120, 80, 255}, 0.4f), ColorAlpha(Color{255, 120, 80, 255}, 0)); }
        for (const auto& th : w.things) if (th.kind == sf::TH_BEAM || th.kind == sf::TH_HOLE || th.kind == sf::TH_POTATO) { Vector2 c = W2S(th.p); DrawCircleGradient((int)c.x, (int)c.y, 50, ColorAlpha(Color{200, 200, 255, 255}, 0.4f), ColorAlpha(Color{200, 200, 255, 255}, 0)); }
    }
    if (w.inkT > 0) { float a = std::min(1.0f, w.inkT); for (int i = 0; i < 9; i++) { float x = SCREEN_W * (0.1f + 0.1f * i), y = SCREEN_H * (0.3f + 0.35f * sinf(i * 2.1f)); DrawCircleV({x, y}, 140 + 40 * sinf(i * 1.3f), ColorAlpha(Color{12, 10, 16, 255}, 0.92f * a)); } }
    if (w.flashT > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, ColorAlpha(WHITE, std::min(1.0f, w.flashT / 1.0f)));
    if (w.reachX > -5 && w.reachRow >= 0) { Vector2 c = W2S({w.reachX, (w.reachRow + 1.0f) * sf::TILE}); float px = S.zoom * sf::TILE; DrawLineEx({c.x, (float)SCREEN_H + 40}, c, px * 1.6f + 4, INK); DrawLineEx({c.x, (float)SCREEN_H + 40}, c, px * 1.6f, Color{170, 50, 60, 255}); DrawCircleV(c, px * 1.0f, Color{170, 50, 60, 255}); }
    if (w.floodY > -5) { float y = W2S({0, w.floodY}).y; DrawRectangle(0, (int)y, SCREEN_W, SCREEN_H - (int)y, Color{60, 120, 160, 120}); DrawLineEx({0, y}, {(float)SCREEN_W, y}, 2, Color{230, 245, 255, 220}); }
    gEventBanner = std::max(0.0f, gEventBanner - dt);
    if (gEventBanner > 0 && gEventKind >= 0) { float a = std::min(1.0f, gEventBanner); DrawRectangle(0, 120, SCREEN_W, 54, ColorAlpha(Color{20, 14, 10, 255}, 0.7f * a)); DrawTextCenteredBold(gEventKind >= 100 ? "Hot potato!" : sf::EventName(gEventKind), SCREEN_W / 2.0f, 130, 30, ColorAlpha(Color{250, 220, 150, 255}, a)); }
}
}  // namespace
