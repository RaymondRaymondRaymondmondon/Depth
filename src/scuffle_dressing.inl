// Scuffle's stage dressing (the friends: "underdeveloped"): small props on the top faces and things hanging under the
// platforms, chosen per world by a hash of the tile, so every stage looks lived in. Drawn after the tiles and behind
// the sticks; never solid (purely the look). Included by scuffle_game.cpp.
static uint32_t DressHash(int x, int y, int w) { uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(w * 83492791); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return h; }
static void DressTop(int world, Vector2 base, float px, uint32_t h) {   // base: the middle of the tile's top face
    float s = px; Color ink = INK;
    switch (world) {
        case sf::WD_NAUTILUS: {
            int kind = (h >> 4) % 3;
            if (kind == 0) {   // a valve on a pipe stub
                DrawRectangleV({base.x - s * 0.08f, base.y - s * 0.45f}, {s * 0.16f, s * 0.45f}, Color{150, 140, 120, 255});
                DrawRectangleLinesEx({base.x - s * 0.08f, base.y - s * 0.45f, s * 0.16f, s * 0.45f}, 1.5f, ink);
                DrawRing({base.x, base.y - s * 0.5f}, s * 0.14f, s * 0.2f, 0, 360, 20, Color{180, 50, 40, 255});
                for (int q = 0; q < 4; q++) { float a = q * PI / 2; DrawLineEx({base.x, base.y - s * 0.5f}, {base.x + cosf(a) * s * 0.16f, base.y - s * 0.5f + sinf(a) * s * 0.16f}, 2, Color{180, 50, 40, 255}); }
            } else if (kind == 1) {   // a coil of rope
                for (int q = 0; q < 3; q++) DrawEllipseLines((int)base.x, (int)(base.y - s * (0.06f + q * 0.07f)), s * (0.3f - q * 0.04f), s * 0.07f, Color{150, 116, 70, 255});
            } else {   // a small stamped crate
                Rectangle r{base.x - s * 0.22f, base.y - s * 0.4f, s * 0.44f, s * 0.4f};
                DrawRectangleRec(r, Color{170, 130, 80, 255}); DrawRectangleLinesEx(r, 1.5f, ink); DrawLineEx({r.x, r.y}, {r.x + r.width, r.y + r.height}, 1.2f, ColorAlpha(ink, 0.6f));
            }
            break;
        }
        case sf::WD_CAVE: {
            int kind = (h >> 4) % 3;
            if (kind == 0) for (int q = 0; q < 3; q++) {   // glowing mushrooms
                float x = base.x + (q - 1) * s * 0.18f, hh = s * (0.18f + 0.08f * ((h >> (q * 3)) % 3));
                DrawLineEx({x, base.y}, {x, base.y - hh}, 2.5f, Color{210, 200, 170, 255});
                DrawCircleSector({x, base.y - hh}, s * 0.11f, 180, 360, 12, Color{120, 220, 190, 255});
                DrawCircleV({x, base.y - hh - s * 0.04f}, s * 0.14f, Color{120, 220, 190, 40});
            }
            else if (kind == 1) DrawTri({base.x - s * 0.16f, base.y}, {base.x + s * 0.16f, base.y}, {base.x, base.y - s * 0.45f}, Color{100, 84, 72, 255});   // a stalagmite
            else for (int q = 0; q < 4; q++) DrawEllipse((int)(base.x + (q - 1.5f) * s * 0.14f), (int)(base.y - s * 0.04f), s * 0.07f, s * 0.05f, Color{120, 106, 92, 255});   // pebbles
            break;
        }
        case sf::WD_REEF: {
            int kind = (h >> 4) % 3;
            if (kind == 0) for (int q = 0; q < 2; q++) {   // kelp swaying
                Vector2 p = {base.x + (q - 0.5f) * s * 0.2f, base.y}; float sw = sinf(S.t * 1.6f + (float)(h % 7) + q);
                for (int k = 0; k < 6; k++) { Vector2 n = {p.x + sw * s * 0.05f * k * 0.4f, p.y - s * 0.16f}; DrawLineEx(p, n, s * 0.07f, Color{60, 140, 70, 255}); p = n; }
            }
            else if (kind == 1) { for (int q = -2; q <= 2; q++) DrawLineEx(base, {base.x + q * s * 0.1f, base.y - s * (0.4f - 0.04f * q * q)}, 2.5f, Color{230, 110, 120, 255}); }   // a coral fan
            else { Vector2 c{base.x, base.y - s * 0.07f}; for (int q = 0; q < 5; q++) { float a = -PI / 2 + q * 2 * PI / 5; DrawLineEx(c, {c.x + cosf(a) * s * 0.16f, c.y + sinf(a) * s * 0.1f}, s * 0.07f, Color{240, 150, 60, 255}); } }   // a starfish
            break;
        }
        case sf::WD_ATLANTIS: {
            int kind = (h >> 4) % 2;
            if (kind == 0) {   // a broken column stub
                Rectangle r{base.x - s * 0.18f, base.y - s * 0.5f, s * 0.36f, s * 0.5f};
                DrawRectangleRec(r, Color{230, 224, 210, 255}); for (int q = 1; q < 4; q++) DrawLineEx({r.x + q * r.width / 4, r.y + 3}, {r.x + q * r.width / 4, r.y + r.height}, 1, Color{180, 172, 160, 255});
                DrawTri({r.x, r.y}, {r.x + r.width, r.y + s * 0.08f}, {r.x + r.width * 0.4f, r.y - s * 0.08f}, Color{230, 224, 210, 255}); DrawRectangleLinesEx(r, 1.5f, ink);
            } else {   // an amphora
                Vector2 c{base.x, base.y - s * 0.2f}; DrawEllipse((int)c.x, (int)c.y, s * 0.14f, s * 0.2f, Color{190, 100, 60, 255}); DrawRectangleV({c.x - s * 0.05f, c.y - s * 0.32f}, {s * 0.1f, s * 0.14f}, Color{190, 100, 60, 255});
                DrawLineEx({c.x - s * 0.12f, c.y}, {c.x + s * 0.12f, c.y}, 2, Color{40, 30, 26, 255});
            }
            break;
        }
        case sf::WD_VOID: {
            int kind = (h >> 4) % 2;
            if (kind == 0) for (int q = 0; q < 3; q++) {   // crystal shards, glowing
                float x = base.x + (q - 1) * s * 0.14f, hh = s * (0.25f + 0.12f * ((h >> (q * 2)) % 3)); Color c{170, 120, 255, 255};
                DrawTri({x - s * 0.06f, base.y}, {x + s * 0.06f, base.y}, {x + s * 0.02f, base.y - hh}, c);
                DrawCircleV({x, base.y - hh * 0.5f}, s * 0.16f, ColorAlpha(c, 0.08f + 0.05f * sinf(S.t * 2 + q)));
            }
            else { DrawLineEx({base.x - s * 0.2f, base.y - s * 0.05f}, {base.x + s * 0.2f, base.y - s * 0.05f}, s * 0.06f, Color{220, 214, 196, 255}); for (int q : {-1, 1}) { DrawCircleV({base.x + q * s * 0.2f, base.y - s * 0.09f}, s * 0.05f, Color{220, 214, 196, 255}); DrawCircleV({base.x + q * s * 0.2f, base.y - s * 0.02f}, s * 0.05f, Color{220, 214, 196, 255}); } }   // a bone
            break;
        }
        case sf::WD_SALON: {
            int kind = (h >> 4) % 3;
            if (kind == 0) { Rectangle r{base.x - s * 0.06f, base.y - s * 0.4f, s * 0.12f, s * 0.4f}; DrawRectangleRec(r, Color{40, 110, 60, 220}); DrawRectangleV({r.x + s * 0.03f, r.y - s * 0.12f}, {s * 0.06f, s * 0.12f}, Color{40, 110, 60, 220}); DrawRectangleLinesEx(r, 1, ink); }   // a bottle
            else if (kind == 1) { DrawRectangleV({base.x - s * 0.04f, base.y - s * 0.25f}, {s * 0.08f, s * 0.25f}, Color{236, 226, 200, 255}); float f = 0.8f + 0.2f * sinf(S.t * 12 + (float)(h % 9)); DrawEllipse((int)base.x, (int)(base.y - s * 0.32f), s * 0.035f, s * 0.07f * f, Color{255, 190, 80, 255}); DrawCircleV({base.x, base.y - s * 0.32f}, s * 0.2f, Color{255, 190, 80, 30}); }   // a candle
            else for (int q = 0; q < 3; q++) { Color bc = q == 0 ? Color{140, 40, 40, 255} : q == 1 ? Color{40, 70, 120, 255} : Color{120, 100, 40, 255}; Rectangle r{base.x - s * 0.24f + q * s * 0.02f, base.y - s * 0.09f * (q + 1), s * 0.44f, s * 0.09f}; DrawRectangleRec(r, bc); DrawRectangleLinesEx(r, 1, ink); }   // books
            break;
        }
    }
}
static void DressUnder(int world, Vector2 base, float px, uint32_t h) {   // base: the middle of the tile's underside
    float s = px; float sw = sinf(S.t * 1.3f + (float)(h % 11));
    switch (world) {
        case sf::WD_NAUTILUS: {   // a caged lamp on a cable
            Vector2 l{base.x, base.y + s * 0.5f}; DrawLineEx(base, l, 1.5f, INK);
            DrawCircleV(l, s * 0.45f, Color{255, 230, 160, 26}); DrawCircleV(l, s * 0.11f, Color{255, 236, 180, 255}); DrawCircleLines((int)l.x, (int)l.y, s * 0.13f, INK);
            break;
        }
        case sf::WD_CAVE: { Vector2 p = base; for (int k = 0; k < 4; k++) { Vector2 n{p.x + sw * s * 0.03f + ((k % 2) ? 1 : -1) * s * 0.04f, p.y + s * 0.13f}; DrawLineEx(p, n, 2, Color{90, 70, 50, 255}); p = n; } break; }   // roots
        case sf::WD_REEF: { Vector2 p = base; for (int k = 0; k < 5; k++) { Vector2 n{p.x + sw * s * 0.05f * k * 0.3f, p.y + s * 0.12f}; DrawLineEx(p, n, s * 0.05f, Color{70, 130, 80, 255}); p = n; } break; }   // weed
        case sf::WD_ATLANTIS: { Vector2 p = base; for (int k = 0; k < 4; k++) { Vector2 n{p.x + sw * s * 0.03f, p.y + s * 0.15f}; DrawLineEx(p, n, 2, Color{70, 120, 60, 255}); DrawCircleV(n, s * 0.05f, Color{90, 150, 70, 255}); p = n; } break; }   // a vine
        case sf::WD_VOID: { Vector2 p = base; for (int k = 0; k < 4; k++) { Vector2 n{p.x + sw * s * 0.06f, p.y + s * 0.14f}; DrawLineEx(p, n, s * 0.05f * (1 - k * 0.2f), Color{70, 50, 90, 255}); p = n; } DrawCircleV(p, s * 0.05f, Color{170, 120, 255, 180}); break; }   // a tendril, glowing at the tip
        case sf::WD_SALON: { Vector2 t{base.x + sw * s * 0.03f, base.y + s * 0.35f}; DrawLineEx(base, t, 1.5f, Color{170, 140, 60, 255}); DrawTri({t.x - s * 0.06f, t.y}, {t.x + s * 0.06f, t.y}, {t.x, t.y + s * 0.2f}, Color{170, 40, 40, 255}); break; }   // a tassel
    }
}
void DrawDressing(const sf::Stage& s) {
    float px = S.zoom * sf::TILE;
    auto plain = [&](int x, int y) { uint8_t k = s.At(x, y); return k == sf::T_STONE || k == sf::T_WOOD; };
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) {
        if (!plain(x, y)) continue;
        uint32_t h = DressHash(x, y, s.world);
        bool topOpen = y + 1 < s.h && s.At(x, y + 1) == sf::T_EMPTY && s.At(x, y + 2) == sf::T_EMPTY, underOpen = y > 0 && s.At(x, y - 1) == sf::T_EMPTY && s.At(x, y - 2) == sf::T_EMPTY;
        if (topOpen && h % 4 == 0) DressTop(s.world, W2S({(x + 0.5f) * sf::TILE, (y + 1) * sf::TILE}), px * 1.5f, h);
        if (underOpen && (h >> 8) % 6 == 0) DressUnder(s.world, W2S({(x + 0.5f) * sf::TILE, y * sf::TILE}), px * 1.4f, h);
    }
}
