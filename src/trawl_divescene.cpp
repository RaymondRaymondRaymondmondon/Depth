// The Trawl's dive scene (design doc v2, "Diving and salvage": "Diving switches that player to a side-view pixel scene
// built on the parkour side's movement code (walk, climb, squeeze, no jumping hazards), lit only by the helmet lamp").
// The rules stay in the headless dive (trawl_dive.cpp: rooms, air, salvage, residents); this is the diver's body and
// eyes. A wreck's rooms become a tile grid (a 4 x 3 m cell is 6 x 5 tiles of 32 px: a deck plank and four rows of
// room), doors cut through the walls at floor level, hatches are holes in the deck with a ladder, a squeeze is a
// one-tile tunnel, a breach is the hull torn open to the sea with the lifeline down it as a ladder. The body moves with
// the parkour code's StepPlayer in its underwater mode (a flooded level); walking into another room asks the host to
// move the diver there (HandInput HI_ORDER), and a refusal (a squeeze with salvage in your arms, a grip) puts the body
// back.
#include "trawl.h"
#include "trawl_wreck.h"
#include "trawl_art.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
constexpr int TT = 32, CW = 6, CH = 5, OX = 2, OY = 4;   // (TT must match platformer.cpp's T)
constexpr float BODY_W = 20, BODY_H = 26, STEP_HZ = 240;
struct Scene {
    bool built = false; int wreck = -1; uint32_t seed = 0; int you = -1;
    PlatformState p;
    int room = -1, lastAsk = -1; float askT = 0;
    Vector2 lastGood{};
};
Scene gD;

Rectangle Interior(const WreckRoom& r) { return {(float)(OX + r.x * CW + 1), (float)(OY + r.y * CH + 1), (float)(r.w * CW - 1), (float)(CH - 1)}; }   // (tiles)
void Carve(PlatformState& p, int tx, int ty, char c) { if (tx >= 0 && ty >= 0 && tx < p.w && ty < p.h) p.tiles[ty][tx] = c; }

void Build(const Wreck& w) {
    PlatformState& p = gD.p;
    p = PlatformState{};
    p.level = PL_CAVE;   // (a flooded level: the parkour code's underwater moves)
    p.verifying = true;  // (no creatures, particles or launch flora: this is a wreck, not a parkour level)
    p.w = OX * 2 + w.gw * CW + 1; p.h = OY + w.gh * CH + 3;
    p.tiles.assign(p.h, std::string(p.w, '#'));
    for (int y = 0; y < OY; y++) for (int x = 0; x < p.w; x++) p.tiles[y][x] = ' ';   // the sea over her
    for (const auto& r : w.rooms) { Rectangle I = Interior(r); for (int y = (int)I.y; y < I.y + I.height; y++) for (int x = (int)I.x; x < I.x + I.width; x++) Carve(p, x, y, ' '); }
    for (const auto& L : w.links) {
        const WreckRoom& a = w.rooms[L.a]; const WreckRoom& b = w.rooms[L.b];
        if (L.kind == 0) {   // a door: the wall between, the bottom three rows
            const WreckRoom& right = a.x < b.x ? b : a;
            int wx = OX + right.x * CW, by = OY + right.y * CH + CH - 1;
            for (int y = by - 2; y <= by; y++) Carve(p, wx, y, ' ');
        } else if (L.kind == 1) {   // a hatch: a hole in the deck plank and a ladder from the upper room's floor to the lower's
            const WreckRoom& up = a.y < b.y ? a : b; const WreckRoom& dn = a.y < b.y ? b : a;
            int x0 = std::max(up.x, dn.x), cx = OX + x0 * CW + 2;
            for (int y = OY + up.y * CH + 2; y <= OY + dn.y * CH + CH - 1; y++) { Carve(p, cx, y, 'l'); }
            Carve(p, cx + 1, OY + dn.y * CH, ' ');
        } else {   // a squeeze: a one-tile tunnel along the lower room's floor line, then up or down a ladder
            Rectangle A = Interior(a), B = Interior(b);
            int ax = (int)(A.x + A.width / 2), ay = (int)(A.y + A.height - 1), bx = (int)(B.x + B.width / 2), by = (int)(B.y + B.height - 1);
            for (int x = std::min(ax, bx); x <= std::max(ax, bx); x++) if (p.tiles[ay][x] == '#') Carve(p, x, ay, ' ');
            for (int y = std::min(ay, by); y <= std::max(ay, by); y++) Carve(p, bx, y, 'l');
        }
    }
    for (int e : w.entries) {   // a breach: torn open to the sea above, the lifeline down it
        const WreckRoom& r = w.rooms[e];
        int cx = OX + r.x * CW + std::max(2, r.w * CW / 2);
        for (int y = 0; y <= OY + r.y * CH + CH - 1; y++) { Carve(p, cx, y, 'l'); if (p.tiles[y][cx + 1] == '#') Carve(p, cx + 1, y, ' '); }
    }
    gD.built = true; gD.wreck = -1; gD.room = -1;
}
int RoomAt(const Wreck& w, Vector2 px) {
    float tx = px.x / TT, ty = px.y / TT;
    for (int i = 0; i < w.Rooms(); i++) { Rectangle I = Interior(w.rooms[i]); if (tx >= I.x && tx < I.x + I.width && ty >= I.y - 0.2f && ty < I.y + I.height) return i; }
    return -1;
}
Vector2 RoomFloor(const WreckRoom& r) { Rectangle I = Interior(r); return {(I.x + 1.5f) * TT, (I.y + I.height) * TT - BODY_H - 1}; }
}

// called each frame for the local diver: steps the body, and says which room the host should move them into (or -1)
int DiveSceneStep(const Gannet& G, int you, float dt, float dir, bool jumpHeld, bool up, bool down) {
    if (G.dive.diver < 0 || (you != G.dive.diver && you != G.dive.diver2) || !G.wrecks || G.dive.wreck < 0) { gD.built = false; return -1; }
    const Wreck& w = (*G.wrecks)[G.dive.wreck];
    if (!gD.built || gD.wreck != G.dive.wreck || gD.seed != w.seed || gD.you != you) {
        Build(w); gD.wreck = G.dive.wreck; gD.seed = w.seed; gD.you = you;
        int start = G.dive.room >= 0 ? G.dive.room : (w.entries.empty() ? 0 : w.entries[0]);
        gD.p.pos = RoomFloor(w.rooms[start]); gD.room = start; gD.lastGood = gD.p.pos;
        if (you == G.dive.diver2) gD.p.pos.x += 24;
    }
    if (G.dive.room < 0) return -1;   // (still on the line, or hauling up)
    // the host's room is the truth: a move it refused (or a move it made on its own) puts the body where it should be
    if (G.dive.room != gD.room) {
        if (gD.lastAsk == G.dive.room) gD.room = G.dive.room;
        else { gD.p.pos = RoomFloor(w.rooms[G.dive.room]); gD.p.vel = {0, 0}; gD.room = G.dive.room; }
    }
    gD.askT = std::max(0.0f, gD.askT - dt);
    if (gD.lastAsk >= 0 && gD.askT <= 0 && G.dive.room != gD.lastAsk) { gD.p.pos = gD.lastGood; gD.p.vel = {0, 0}; gD.lastAsk = -1; }   // (refused: back)
    // the body: held fast, it doesn't move; otherwise the parkour code's underwater moves at their own 240 Hz
    bool held = G.dive.holdT > 0;
    gD.p.upHeld = up && !held; gD.p.inDown = down && !held; gD.p.climbDir = up ? -1 : down ? 1 : 0;
    gD.p.accumulator += std::min(dt, 0.1f);
    while (gD.p.accumulator >= 1.0f / STEP_HZ) { gD.p.accumulator -= 1.0f / STEP_HZ; PlatStepPlayer(gD.p, held ? 0.0f : dir, jumpHeld && !held); }
    int r = RoomAt(w, {gD.p.pos.x + BODY_W / 2, gD.p.pos.y + BODY_H / 2});
    if (r >= 0 && r == gD.room) gD.lastGood = gD.p.pos;
    if (r >= 0 && r != gD.room && r != gD.lastAsk) { gD.lastAsk = r; gD.askT = 0.5f; return r; }
    return -1;
}

// the diver's eyes: the wreck in section, lit only by the helmet lamp (and the bell, and the air pockets' shimmer)
void DiveSceneDraw(const Gannet& G, int you) {
    if (!gD.built || !G.wrecks || G.dive.wreck < 0) return;
    const Wreck& w = (*G.wrecks)[G.dive.wreck];
    const PlatformState& p = gD.p;
    Vector2 me{p.pos.x + BODY_W / 2, p.pos.y + BODY_H / 2};
    if (G.dive.room < 0) me = {RoomFloor(w.rooms[w.entries.empty() ? 0 : w.entries[0]]).x, (OY * TT) * std::clamp(G.dive.depth / std::max(1.0f, w.depth), 0.0f, 1.0f)};
    Vector2 cam{me.x - SCREEN_W / 2.0f, me.y - SCREEN_H / 2.0f};
    float lamp = G.dive.lampOutT > 0 ? 1.4f * TT : 6.0f * TT;
    if (G.dive.siltT > 0) lamp *= 0.35f;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{2, 6, 10, 255});
    int tx0 = std::max(0, (int)(cam.x / TT) - 1), tx1 = std::min(p.w - 1, (int)((cam.x + SCREEN_W) / TT) + 1);
    int ty0 = std::max(0, (int)(cam.y / TT) - 1), ty1 = std::min(p.h - 1, (int)((cam.y + SCREEN_H) / TT) + 1);
    for (int ty = ty0; ty <= ty1; ty++) for (int tx = tx0; tx <= tx1; tx++) {
        Vector2 c{tx * (float)TT + TT / 2.0f, ty * (float)TT + TT / 2.0f};
        float d = Vector2Distance(c, me), lit = std::clamp(1.0f - d / lamp, 0.0f, 1.0f);
        char ch = p.tiles[ty][tx];
        if (ty < OY && ch != '#') lit = std::max(lit, 0.06f);   // (the faint grey of the sea above)
        if (lit <= 0.01f) continue;
        int sx = (int)(tx * TT - cam.x), sy = (int)(ty * TT - cam.y);
        if (ch == '#') {
            bool plank = ((ty - OY) % CH) == 0 && ty >= OY;
            Color wood = plank ? Color{92, 70, 48, 255} : Color{64, 50, 38, 255};
            DrawRectangle(sx, sy, TT, TT, Fade(wood, lit));
            for (int k = 1; k < 4; k++) DrawLine(sx, sy + k * 8, sx + TT, sy + k * 8, Fade(Color{40, 30, 22, 255}, lit));   // the strakes
            if (((tx * 7 + ty * 3) % 11) == 0) DrawCircle(sx + 16, sy + 16, 3, Fade(Color{70, 90, 60, 255}, lit));          // weed on the timbers
        } else {
            DrawRectangle(sx, sy, TT, TT, Fade(ty < OY ? Color{10, 26, 36, 255} : Color{14, 20, 22, 255}, lit));
            if (ch == 'l') { DrawLine(sx + 12, sy, sx + 12, sy + TT, Fade(Color{150, 140, 110, 255}, lit)); DrawLine(sx + 20, sy, sx + 20, sy + TT, Fade(Color{150, 140, 110, 255}, lit)); DrawLine(sx + 12, sy + 16, sx + 20, sy + 16, Fade(Color{150, 140, 110, 255}, lit)); }
            bool floorBelow = ty + 1 < p.h && p.tiles[ty + 1][tx] == '#' && ty >= OY;
            if (floorBelow) DrawRectangle(sx, sy + TT - 5, TT, 5, Fade(Color{70, 66, 54, 255}, lit));   // silt on every floor
        }
    }
    // salvage glinting on the floors, residents in the dark, air pockets shimmering at the ceilings
    for (int i = 0; i < w.Rooms(); i++) {
        const WreckRoom& r = w.rooms[i]; Rectangle I = Interior(r);
        int n = 0;
        for (const auto& s : w.salvage) if (s.room == i && !s.taken) {
            Vector2 q{(I.x + 1 + (n % 4) * 1.2f) * TT - cam.x, (I.y + I.height) * TT - 10 - cam.y}; n++;
            float lit = std::clamp(1.0f - Vector2Distance(Vector2Add(q, cam), me) / lamp, 0.0f, 1.0f);
            if (lit > 0.05f) { DrawRectangle((int)q.x, (int)q.y, s.twoDiver ? 16 : 9, s.twoDiver ? 10 : 7, Fade(s.relic ? Color{160, 220, 230, 255} : Color{200, 170, 80, 255}, lit)); if (fmodf((float)GetTime() * 2 + n, 3.0f) < 0.2f) DrawPixel((int)q.x + 2, (int)q.y + 1, WHITE); }
        }
        if (r.air) { Vector2 q{(I.x + I.width / 2) * TT - cam.x, I.y * TT + 6 - cam.y}; DrawEllipse((int)q.x, (int)q.y, (int)(I.width * TT / 3), 5, Fade(Color{170, 210, 230, 255}, 0.25f + 0.1f * sinf((float)GetTime() * 2))); }
        if (r.locked) { Vector2 q{I.x * TT - 4 - cam.x, (I.y + I.height - 2) * TT - cam.y}; DrawRectangle((int)q.x, (int)q.y, 8, 2 * TT, Fade(Color{120, 90, 50, 255}, 0.8f)); }
    }
    for (const auto& rs : w.residents) {
        if (rs.room < 0 || rs.room >= w.Rooms()) continue;
        Rectangle I = Interior(w.rooms[rs.room]);
        Vector2 q{(I.x + I.width - 1.5f) * TT - cam.x, (I.y + I.height - 1.2f) * TT - cam.y};
        float lit = std::clamp(1.0f - Vector2Distance(Vector2Add(q, cam), me) / lamp, 0.0f, 1.0f);
        if (lit < 0.05f) continue;
        Color rc = rs.what.find("Drowned") != std::string::npos ? Color{120, 140, 128, 255} : rs.what.find("Worm") != std::string::npos ? Color{214, 214, 204, 255}
                 : rs.what == "isopods" ? Color{200, 196, 176, 255} : rs.what.find("octopus") != std::string::npos ? Color{220, 196, 196, 255} : Color{96, 116, 84, 255};
        if (rs.what.find("Drowned") != std::string::npos) { DrawRectangle((int)q.x - 8, (int)q.y - 28, 16, 36, Fade(rc, lit)); DrawCircle((int)q.x, (int)q.y - 34, 7, Fade(rc, lit)); }
        else if (rs.what == "isopods") for (int k = 0; k < 6; k++) DrawRectangle((int)q.x - 24 + k * 8, (int)q.y + 18, 6, 3, Fade(rc, lit));
        else { DrawEllipse((int)q.x, (int)q.y + 8, 18, 8, Fade(rc, lit)); }
        if (rs.awake) DrawCircle((int)q.x - 10, (int)q.y + 6, 2, Fade(Color{240, 230, 160, 255}, lit));
    }
    // the bell at the first breach; the lifeline/hose up from the diver
    if (G.dive.bell && !w.entries.empty()) { Vector2 b{RoomFloor(w.rooms[w.entries[0]]).x + 16 - cam.x, (OY - 1) * TT - cam.y}; DrawCircleSector(b, 26, 180, 360, 16, Color{150, 120, 70, 255}); DrawLineEx({b.x, -10}, {b.x, b.y - 26}, 3, Color{90, 90, 86, 255}); }
    else { Vector2 a{me.x - cam.x, me.y - cam.y - 10}; DrawLineBezier(a, {a.x + 60, -20}, 2, Fade(Color{170, 160, 120, 255}, 0.7f)); }
    // the diver: a brass helmet, the lamp's beam, heavy boots
    Vector2 s{p.pos.x - cam.x, p.pos.y - cam.y};
    if (G.dive.room >= 0) {
        DrawRectangle((int)s.x + 3, (int)s.y + 10, 14, 14, Color{90, 96, 92, 255});
        DrawCircle((int)s.x + 10, (int)s.y + 7, 8, Color{180, 140, 70, 255}); DrawCircle((int)s.x + (p.facingRight ? 13 : 7), (int)s.y + 7, 3, Color{40, 60, 70, 255});
        DrawRectangle((int)s.x + 3, (int)s.y + 22, 6, 4, Color{50, 46, 42, 255}); DrawRectangle((int)s.x + 11, (int)s.y + 22, 6, 4, Color{50, 46, 42, 255});
        if (G.dive.lampOutT <= 0) DrawCircleV({s.x + 10, s.y + 10}, lamp * 0.5f, Fade(Color{255, 230, 170, 255}, 0.05f));
        if (G.dive.carrying) DrawRectangle((int)s.x + (p.facingRight ? 18 : -8), (int)s.y + 12, 10, 8, Color{200, 170, 80, 255});
        if (G.dive.diver2 >= 0) { DrawCircle((int)s.x - 14, (int)s.y + 7, 7, Color{170, 132, 66, 255}); DrawRectangle((int)s.x - 20, (int)s.y + 10, 12, 14, Color{84, 90, 86, 255}); }
        if (G.dive.siltT > 0) DrawCircleV({s.x + 10, s.y + 14}, TT * 2.5f, Fade(Color{70, 66, 54, 255}, 0.7f * std::min(1.0f, G.dive.siltT / 2)));
        if (G.dive.holdT > 0) DrawText("HELD", (int)s.x - 6, (int)s.y - 22, 16, Color{240, 140, 110, 255});
    }
    (void)you;
}

bool DiveSceneActive() { return gD.built; }

// ---------------------------------------------------------------- depth.exe --trawl-divescene-test
int RunTrawlDiveSceneTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: the dive scene (the parkour movement in a wreck)\n");
    // a whaler with a door to walk through: find a room with a door on its right
    for (uint32_t seed = 1; seed < 60; seed++) {
        std::vector<Wreck> ws{GenerateWreck(WreckType::Whaler, seed, "weeds")};
        const Wreck& w = ws[0];
        int from = -1, to = -1;
        for (const auto& L : w.links) if (L.kind == 0) { const WreckRoom& a = w.rooms[L.a]; const WreckRoom& b = w.rooms[L.b]; from = a.x < b.x ? L.a : L.b; to = a.x < b.x ? L.b : L.a; break; }
        if (from < 0) continue;
        Gannet g; g.Init(1, 3); g.wrecks = &ws; g.dive.diver = 0; g.dive.wreck = 0; g.dive.room = from; g.dive.depth = w.depth; g.crew[0].deck = DECK_DIVE;
        gD.built = false;
        int asked = -1;
        for (int i = 0; i < 60 * 8 && asked < 0; i++) asked = DiveSceneStep(g, 0, 1 / 60.0f, 1.0f, false, false, false);
        check(asked == to, TextFormat("walking right out of room %d through the door, the scene asks for room %d (asked %d)", from, to, asked));
        if (asked == to) { g.DiveMove(to); for (int i = 0; i < 10; i++) DiveSceneStep(g, 0, 1 / 60.0f, 0.0f, false, false, false); check(gD.room == to, "the host moved the diver: the scene agrees"); }
        // a refused move (held fast) puts the body back
        g.dive.holdT = 99; g.dive.room = to; gD.room = to;
        float x0 = gD.p.pos.x; for (int i = 0; i < 60 * 3; i++) DiveSceneStep(g, 0, 1 / 60.0f, -1.0f, false, false, false);
        check(fabsf(gD.p.pos.x - x0) < 1, "held fast by something in the wreck, the body can't walk");
        break;
    }
    printf(fails ? "trawl-divescene-test: %d FAILED\n" : "trawl-divescene-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw