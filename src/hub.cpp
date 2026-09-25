// ============================================================================
//  DEPTH - the Nautilus: the hub screen and every station you can click.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

// ============================================================ hub drawing
static void DrawPorthole(float cx, float cy, float t) {
    DrawCircle((int)cx, (int)cy, 62, Pal::BrassDk);
    DrawCircle((int)cx, (int)cy, 54, Color{64, 150, 176, 255});
    DrawCircle((int)cx, (int)(cy + 20), 40, Color{52, 128, 156, 255});
    // a little fish drifting back and forth
    float fx = cx + sinf(t * 0.6f) * 28, fy = cy - 8 + sinf(t * 1.3f) * 6;
    bool right = cosf(t * 0.6f) > 0;
    DrawEllipse((int)fx, (int)fy, 12, 6, Color{250, 170, 80, 255});
    float tail = right ? fx - 12 : fx + 12, tip = right ? fx - 20 : fx + 20;
    DrawTriangle({tail, fy}, {tip, fy + (right ? 7.f : -7.f)}, {tip, fy + (right ? -7.f : 7.f)}, Color{240, 140, 60, 255});
    for (int k = 0; k < 4; k++) {
        float by = cy + 40 - fmodf(t * 25 + k * 22, 85);
        DrawCircleLines((int)(cx - 25 + k * 16), (int)by, 3, Color{220, 245, 255, 200});
    }
    DrawRing({cx, cy}, 54, 62, 0, 360, 48, Pal::Brass);
    for (int k = 0; k < 8; k++) {
        float a = k * PI / 4;
        DrawCircle((int)(cx + cosf(a) * 58), (int)(cy + sinf(a) * 58), 3, Pal::BrassDk);
    }
}

static void DrawSign(Rectangle area, const char* text) {
    Rectangle s{area.x - 6, area.y - 46, area.width + 12, 32};
    DrawRectangleRec(s, Pal::Paper);
    DrawRectangleLinesEx(s, 3, Pal::BrassDk);
    DrawTextCentered(text, s.x + s.width / 2, s.y + 6, 20, Pal::Ink);
}

static void DrawDoor(Rectangle r, const char* sign) {
    DrawRectangleRec({r.x - 8, r.y - 8, r.width + 16, r.height + 8}, Pal::BrassDk);
    DrawRectangleRec(r, Color{104, 66, 42, 255});
    DrawRectangleLinesEx({r.x + 14, r.y + 100, r.width - 28, r.height - 120}, 3, Color{76, 46, 30, 255});
    float cx = r.x + r.width / 2;
    DrawCircle((int)cx, (int)r.y + 55, 30, Pal::Brass);
    DrawCircle((int)cx, (int)r.y + 55, 23, Color{130, 205, 215, 255});
    DrawCircle((int)cx - 8, (int)r.y + 47, 6, Color{210, 245, 250, 255});
    DrawCircle((int)(r.x + r.width - 24), (int)(r.y + r.height / 2 + 20), 7, Pal::Brass);
    DrawSign(r, sign);
}

static void DrawBookshelf(Rectangle r) {
    DrawRectangleRec(r, Color{90, 56, 36, 255});
    DrawRectangleLinesEx(r, 5, Color{66, 40, 26, 255});
    const Color bookColors[] = {{170, 60, 60, 255}, {60, 110, 160, 255}, {70, 140, 90, 255},
                                {200, 160, 70, 255}, {130, 80, 150, 255}, {220, 210, 190, 255}};
    for (int s = 0; s < 4; s++) {
        float sy = r.y + 12 + s * 58.0f;
        DrawRectangle((int)r.x + 5, (int)sy + 48, (int)r.width - 10, 6, Color{66, 40, 26, 255});
        float x = r.x + 10;
        for (int b = 0; x < r.x + r.width - 22; b++) {
            int w = 10 + (s * 7 + b * 13) % 7;
            int h = 34 + (s * 5 + b * 11) % 14;
            DrawRectangle((int)x, (int)(sy + 48 - h), w, h, bookColors[(s * 3 + b) % 6]);
            x += w + 2;
        }
    }
    DrawSign(r, "LIBRARY");
}

static void DrawRadarConsole(Rectangle r, float t) {
    DrawRectangleRounded(r, 0.08f, 6, Color{96, 104, 98, 255});
    DrawRectangleRoundedLinesEx(r, 0.08f, 6, 4, Pal::BrassDk);
    Vector2 c{r.x + r.width / 2, r.y + 82};
    DrawCircleV(c, 64, Pal::BrassDk);
    DrawCircleV(c, 56, Color{14, 54, 34, 255});
    for (int k = 1; k <= 3; k++) DrawCircleLines((int)c.x, (int)c.y, 18.0f * k, Color{60, 170, 90, 160});
    float a = t * 2.0f;
    DrawLineEx(c, {c.x + cosf(a) * 56, c.y + sinf(a) * 56}, 3, Color{110, 255, 150, 255});
    const Vector2 blips[3] = {{22, -18}, {-30, 10}, {8, 34}};
    for (auto b : blips) {
        float ba = atan2f(b.y, b.x);
        float since = fmodf(a - ba + 20 * PI, 2 * PI);
        unsigned char alpha = (unsigned char)(255 * std::max(0.0f, 1.0f - since / 3.0f));
        DrawCircle((int)(c.x + b.x), (int)(c.y + b.y), 4, Color{150, 255, 170, alpha});
    }
    for (int k = 0; k < 4; k++) DrawCircle((int)(r.x + 32 + k * 35), (int)(r.y + 170), 8,
                                          k % 2 ? Pal::Coral : Pal::Teal);
    DrawSign(r, "RADAR");
}

static void DrawHelm(Rectangle r, float t, bool hover) {
    float cx = r.x + r.width / 2, cy = r.y + 90;
    DrawRectangle((int)cx - 14, (int)cy, 28, (int)(r.y + r.height - cy), Color{96, 60, 38, 255});
    DrawRectangle((int)cx - 40, (int)(r.y + r.height - 16), 80, 16, Color{76, 46, 30, 255});
    static float rot = 0;
    rot += GetFrameTime() * (hover ? 1.2f : 0.15f);
    for (int k = 0; k < 8; k++) {
        float a = rot + k * PI / 4;
        Vector2 in{cx + cosf(a) * 12, cy + sinf(a) * 12}, out{cx + cosf(a) * 88, cy + sinf(a) * 88};
        DrawLineEx(in, out, 7, Color{120, 76, 44, 255});
        DrawCircleV(out, 8, Color{140, 90, 52, 255});
    }
    DrawRing({cx, cy}, 60, 72, 0, 360, 48, Color{140, 90, 52, 255});
    DrawRing({cx, cy}, 60, 63, 0, 360, 48, Pal::Brass);
    DrawCircle((int)cx, (int)cy, 16, Pal::Brass);
    DrawCircle((int)cx, (int)cy, 7, Pal::BrassDk);
    (void)t;
}

static void DrawPeriscope(Rectangle r) {
    float cx = r.x + r.width / 2;
    float bottom = r.y + r.height;
    DrawRectangle((int)cx - 14, 70, 28, (int)(bottom - 150 - 70), Pal::Copper);
    DrawRectangle((int)cx - 14, 70, 6, (int)(bottom - 150 - 70), Color{220, 140, 100, 255});
    for (float y = 110; y < bottom - 160; y += 70) DrawRectangle((int)cx - 18, (int)y, 36, 8, Pal::BrassDk);
    Rectangle box{cx - 36, bottom - 160, 72, 60};
    DrawRectangleRounded(box, 0.2f, 6, Pal::Brass);
    DrawRectangle((int)box.x - 26, (int)box.y + 22, 26, 12, Pal::BrassDk);
    DrawRectangle((int)(box.x + box.width), (int)box.y + 22, 26, 12, Pal::BrassDk);
    DrawCircle((int)cx, (int)box.y + 30, 14, Color{30, 40, 46, 255});
    DrawCircle((int)cx - 4, (int)box.y + 26, 4, Color{140, 220, 230, 255});
    DrawTextCentered("PERISCOPE", cx, bottom - 80, 16, Pal::Paper);
}

static void DrawSickLeave(Rectangle r) {
    float top = r.y + 70;
    DrawRectangle((int)r.x, (int)top, (int)r.width, 14, Color{120, 78, 46, 255});
    DrawRectangle((int)r.x + 12, (int)top + 14, 12, (int)(r.y + r.height - top - 14), Color{96, 60, 38, 255});
    DrawRectangle((int)(r.x + r.width - 24), (int)top + 14, 12, (int)(r.y + r.height - top - 14), Color{96, 60, 38, 255});
    float cx = r.x + r.width / 2 - 20;
    DrawRectangleRounded({cx - 32, top - 34, 64, 34}, 0.3f, 6, Pal::Coral);
    DrawRectangleRounded({cx - 40, top - 46, 80, 14}, 0.6f, 6, Color{200, 80, 66, 255});
    DrawCircle((int)cx, (int)top - 16, 9, Pal::Paper);
    DrawRectangle((int)(r.x + r.width - 62), (int)top - 30, 40, 30, Pal::Paper); // a note pad
    DrawLine((int)(r.x + r.width - 56), (int)top - 20, (int)(r.x + r.width - 28), (int)top - 20, Pal::BrassDk);
    DrawLine((int)(r.x + r.width - 56), (int)top - 12, (int)(r.x + r.width - 30), (int)top - 12, Pal::BrassDk);
    DrawSign(r, "SICK LEAVE");
}

struct Station { Rectangle area; Scene target; const char* hint; };

void SceneHub(Game& g) {
    float t = g.time;
    // walls, ribs, floor
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Color{58, 116, 124, 255}, Color{30, 72, 84, 255});
    for (int i = 0; i < 10; i++) DrawRectangle(20 + i * 140, 70, 12, 480, Color{40, 88, 98, 255});
    DrawRectangle(0, 540, SCREEN_W, 180, Color{122, 82, 50, 255});
    for (int y = 560; y < SCREEN_H; y += 34) DrawLine(0, y, SCREEN_W, y, Color{96, 62, 38, 255});
    DrawRectangle(0, 540, SCREEN_W, 8, Pal::BrassDk);
    DrawRectangle(0, 58, SCREEN_W, 18, Pal::Copper);
    DrawRectangle(0, 58, SCREEN_W, 5, Color{224, 150, 108, 255});
    DrawPorthole(470, 150, t);
    DrawPorthole(960, 150, t + 3.0f);

    DrawTextCentered("THE NAUTILUS", SCREEN_W / 2.0f + 3, 13, 38, Pal::Ink);
    DrawTextCentered("THE NAUTILUS", SCREEN_W / 2.0f, 10, 38, Pal::Brass);

    const Station stations[] = {
        {{50, 250, 140, 290}, Scene::Crew, "Crew Quarters: choose your party, fit relics, review or dismiss crew."},
        {{215, 300, 145, 240}, Scene::Bookshelf, "Library: learn about the crew, conditions, and creatures of the deep."},
        {{382, 330, 160, 210}, Scene::Radar, "Radar: pick up new recruits and buy relics from passing salvagers."},
        {{552, 330, 176, 210}, Scene::Helm, "The Helm: chart a course and send your party on an expedition."},
        {{762, 70, 90, 470}, Scene::Periscope, "Periscope: take on a platforming run for gold and relics."},
        {{880, 380, 165, 160}, Scene::SickLeave, "Sick Leave: rattled crew can rest and steady their nerves."},
        {{1070, 250, 140, 290}, Scene::Ward, "The Ward: patch up injured crew."},
    };
    DrawDoor(stations[0].area, "CREW");
    DrawBookshelf(stations[1].area);
    DrawRadarConsole(stations[2].area, t);
    const Station* hovered = nullptr;
    for (const auto& s : stations)
        if (CheckCollisionPointRec(GetMousePosition(), s.area)) hovered = &s;
    DrawHelm(stations[3].area, t, hovered == &stations[3]);
    DrawPeriscope(stations[4].area);
    DrawSickLeave(stations[5].area);
    DrawDoor(stations[6].area, "WARD");

    if (hovered) {
        float pulse = 0.55f + 0.45f * sinf(t * 6);
        Rectangle a = hovered->area;
        DrawRectangleRoundedLinesEx({a.x - 12, a.y - 56, a.width + 24, a.height + 66}, 0.08f, 6, 4, Fade(Pal::Teal, pulse));
        DrawTextCentered(hovered->hint, SCREEN_W / 2.0f, 566, 22, Pal::Paper);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g.scene = hovered->target;
            if (g.scene == Scene::Crew && !FindHero(g, g.selectedHero) && !g.roster.empty())
                g.selectedHero = g.roster[0].id;
        }
    } else {
        DrawTextCentered("Click a station aboard the Nautilus", SCREEN_W / 2.0f, 566, 22, Color{240, 228, 200, 160});
    }

    // status strip
    Panel({20, 610, SCREEN_W - 40.0f, 90});
    Txt(TextFormat("Gold: %d", g.gold), 44, 626, 24, Pal::Ink);
    Txt(TextFormat("Batteries: %d", g.batteries), 44, 660, 20, Pal::BrassDk);
    Txt(TextFormat("Relics in storage: %d", (int)g.relicStorage.size()), 210, 660, 20, Pal::BrassDk);
    Txt("Party:", 460, 626, 22, Pal::Ink);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        float x = 540 + k * 175.0f;
        if (!h) { Txt(TextFormat("%d. ---", k + 1), x, 628, 20, Pal::BrassDk); continue; }
        Txt(TextFormat("%d. %s", k + 1, h->name.c_str()), x, 628, 20, Pal::Ink);
        Txt(ClassName(h->cls), x + 22, 650, 16, Pal::BrassDk);
        Stats s = GetStats(*h);
        DrawBar({x + 22, 672, 120, 7}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({x + 22, 682, 120, 5}, h->stress / 100.0f, Pal::Stress);
    }
}

// ============================================================ helm
void SceneHelm(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Helm", "Choose a destination for the expedition");
    DrawGoldBadge(g);

    struct Loc { const char* name; const char* desc; bool open; Color col; };
    const Loc locs[4] = {
        {"The Cave", "An undersea cave crawling with oversized crustaceans, sea bugs, and worms. Mini boss: the Lobster.", true, Color{60, 120, 140, 255}},
        {"The Island", "Sun-baked shores ruled by the Sun God and the Coconut Queen.", false, Color{200, 160, 80, 255}},
        {"The Weeds", "A kelp forest of merfolk, octopi, and barracuda. Neptune waits within.", false, Color{70, 140, 80, 255}},
        {"Atlantis", "A sunken city of lost ones who still worship something ancient.", false, Color{110, 90, 150, 255}},
    };
    int partyCount = 0;
    for (int id : g.party) if (id >= 0) partyCount++;

    for (int i = 0; i < 4; i++) {
        Rectangle c{50 + i * 300.0f, 100, 280, 330};
        Panel(c, locs[i].open ? Pal::Paper : Color{190, 180, 160, 255});
        DrawRectangleRec({c.x + 12, c.y + 12, c.width - 24, 90}, locs[i].col);
        DrawTextCentered(locs[i].name, c.x + c.width / 2, c.y + 40, 30, Pal::Paper);
        DrawWrapped(locs[i].desc, {c.x + 16, c.y + 116, c.width - 32, 120}, 18, Pal::Ink);
        if (locs[i].open) {
            Txt("Depth: Shallows (levels 0-2)", c.x + 16, c.y + 228, 17, Pal::BrassDk);
            Txt("3 rooms + mini boss", c.x + 16, c.y + 250, 17, Pal::BrassDk);
            if (Button({c.x + 20, c.y + 276, c.width - 40, 42}, "Embark", partyCount > 0)) {
                StartDungeon(g);
                return;
            }
        } else {
            DrawTextCentered("Not yet charted", c.x + c.width / 2, c.y + 280, 22, Pal::BrassDk);
        }
    }

    Panel({50, 450, 880, 250});
    Txt("Expedition party", 70, 464, 24, Pal::Ink);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        float x = 70 + k * 215.0f;
        Txt(TextFormat("Rank %d", k + 1), x, 502, 16, Pal::BrassDk);
        if (!h) { Txt("- empty -", x, 526, 20, Pal::BrassDk); continue; }
        Stats s = GetStats(*h);
        Txt(h->name, x, 524, 22, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h->cls), h->level), x, 550, 17, Pal::BrassDk);
        Txt(TextFormat("HP %d/%d", h->hp, s.maxHp), x, 576, 16, Pal::Ink);
        DrawBar({x, 596, 170, 8}, (float)h->hp / s.maxHp, Pal::Good);
        Txt(TextFormat("Nerves %d/100%s", h->stress, h->rattled ? "  RATTLED" : ""), x, 610, 16, Pal::Ink);
        DrawBar({x, 630, 170, 6}, h->stress / 100.0f, Pal::Stress);
    }
    if (partyCount < 4)
        Txt("Tip: a full party of four is strongly recommended. Visit Crew Quarters.", 70, 660, 18, Pal::Coral);
    else
        Txt("Change the marching order in Crew Quarters. Rank 1 is the front line.", 70, 660, 18, Pal::BrassDk);

    Panel({950, 450, 280, 250});
    Txt("Provisions", 970, 464, 24, Pal::Ink);
    DrawWrapped("Each room ahead drains the flashlight. Batteries recharge it by 40.", {970, 500, 240, 80}, 17, Pal::Ink);
    Txt(TextFormat("Batteries: %d", g.batteries), 970, 580, 22, Pal::Ink);
    if (Button({970, 620, 240, 44}, "Buy battery (15g)", g.gold >= 15)) {
        g.gold -= 15;
        g.batteries++;
    }
}

// ============================================================ crew quarters
void SceneCrew(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Crew Quarters", "Choose your party, set the marching order, and fit relics");
    DrawGoldBadge(g);
    if (!FindHero(g, g.selectedHero)) g.selectedHero = g.roster.empty() ? -1 : g.roster[0].id;
    Vector2 m = GetMousePosition();

    // --- roster
    Panel({20, 90, 300, 612});
    Txt(TextFormat("Roster  %d/%d", (int)g.roster.size(), MAX_ROSTER), 36, 102, 22, Pal::Ink);
    for (size_t i = 0; i < g.roster.size(); i++) {
        Hero& h = g.roster[i];
        Rectangle r{32, 134 + i * 70.0f, 276, 64};
        bool sel = h.id == g.selectedHero, hover = CheckCollisionPointRec(m, r);
        DrawRectangleRounded(r, 0.15f, 6, sel ? Color{252, 242, 214, 255} : hover ? Color{236, 222, 190, 255} : Color{222, 206, 170, 255});
        if (sel) DrawRectangleRoundedLinesEx(r, 0.15f, 6, 2, Pal::BrassDk);
        DrawRectangle((int)r.x, (int)r.y + 6, 6, (int)r.height - 12, ClassColor(h.cls));
        Txt(h.name, r.x + 14, r.y + 8, 20, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h.cls), h.level), r.x + 14, r.y + 34, 16, Pal::BrassDk);
        Stats s = GetStats(h);
        DrawBar({r.x + 150, r.y + 36, 116, 8}, (float)h.hp / s.maxHp, Pal::Good);
        DrawBar({r.x + 150, r.y + 48, 116, 6}, h.stress / 100.0f, Pal::Stress);
        const char* tag = InParty(g, h.id) ? "IN PARTY" : h.onLeave ? "ON LEAVE" : h.rattled ? "RATTLED" : "";
        Txt(tag, r.x + r.width - 8 - MeasureText(tag, 14), r.y + 10, 14,
            h.onLeave ? Pal::Stress : h.rattled ? Pal::Bad : Pal::Copper);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g.selectedHero = h.id; g.dismissArmed = -1; }
    }

    // --- party slots
    Panel({340, 90, 600, 145});
    Txt("Expedition party   (Rank 1 = front line)", 356, 100, 20, Pal::Ink);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Rectangle s{352 + k * 146.0f, 128, 136, 98};
        DrawRectangleRounded(s, 0.12f, 6, Color{214, 196, 158, 255});
        Txt(TextFormat("Rank %d", k + 1), s.x + 8, s.y + 6, 14, Pal::BrassDk);
        Hero* h = FindHero(g, g.party[k]);
        if (!h) { Txt("- empty -", s.x + 8, s.y + 30, 18, Pal::BrassDk); continue; }
        Txt(h->name, s.x + 8, s.y + 22, 18, Pal::Ink);
        Txt(ClassName(h->cls), s.x + 8, s.y + 42, 16, Pal::BrassDk);
        if (Button({s.x + 4, s.y + 64, 40, 28}, "<", k > 0)) std::swap(g.party[k], g.party[k - 1]);
        if (Button({s.x + 48, s.y + 64, 40, 28}, "x")) { g.party[k] = -1; CompactParty(g); }
        if (Button({s.x + 92, s.y + 64, 40, 28}, ">", k < 3 && g.party[k + 1] >= 0)) std::swap(g.party[k], g.party[k + 1]);
    }

    // --- details of the selected hero
    Panel({340, 245, 600, 457});
    Hero* sel = FindHero(g, g.selectedHero);
    if (sel) {
        Hero& h = *sel;
        Stats s = GetStats(h);
        Txt(h.name, 356, 256, 28, Pal::Ink);
        Txt(TextFormat("%s  -  Level %d", ClassName(h.cls), h.level), 356 + MeasureText(h.name.c_str(), 28) + 16, 264, 20, Pal::BrassDk);
        DrawWrapped(ClassBlurb(h.cls), {356, 292, 570, 40}, 16, Pal::Ink);
        int next = XpForNextLevel(h);
        const std::string lines[8] = {
            TextFormat("HP  %d / %d%s", h.hp, s.maxHp, h.deathsDoor ? "  (Death's Door)" : ""),
            TextFormat("Nerves  %d / 100%s", h.stress, h.rattled ? "  RATTLED" : ""),
            TextFormat("Damage  %d - %d", s.dmgMin, s.dmgMax),
            next < 0 ? std::string("XP  max level") : std::string(TextFormat("XP  %d / %d", h.xp, next)),
            TextFormat("Accuracy  %d", s.acc),
            TextFormat("Dodge  %d", s.dodge),
            TextFormat("Protection  %d%%", s.prot),
            TextFormat("Speed  %d", s.speed),
        };
        for (int i = 0; i < 8; i++) Txt(lines[i], 356 + (i / 4) * 290.0f, 338 + (i % 4) * 22.0f, 18, Pal::Ink);

        const auto& abs = ClassAbilities(h.cls);
        for (size_t i = 0; i < abs.size(); i++) {
            float y = 432 + i * 44.0f;
            const Ability& a = abs[i];
            std::string where = "From ranks " + RankString(a.usableFrom);
            if (a.target == Target::Enemy) where += " > hits " + RankString(a.hits);
            Txt(a.name, 356, y, 18, Pal::Copper);
            Txt(where, 356 + MeasureText(a.name.c_str(), 18) + 12, y + 3, 14, Pal::BrassDk);
            Txt(a.desc, 356, y + 20, 15, Pal::Ink);
        }

        for (int k = 0; k < 2; k++) {
            Rectangle rs{352 + k * 204.0f, 612, 196, 40};
            DrawRectangleRounded(rs, 0.2f, 6, Color{214, 196, 158, 255});
            if (h.relics[k] >= 0) {
                const RelicDef& rd = Relics()[h.relics[k]];
                Txt(rd.name, rs.x + 8, rs.y + 3, 16, Pal::Ink);
                Txt(rd.desc, rs.x + 8, rs.y + 22, 13, Pal::BrassDk);
                if (Button({rs.x + rs.width - 34, rs.y + 6, 28, 28}, "x")) {
                    g.relicStorage.push_back(h.relics[k]);
                    h.hp = std::max(1, h.hp - rd.hp);
                    h.relics[k] = -1;
                    h.hp = std::min(h.hp, GetStats(h).maxHp);
                }
            } else {
                Txt("Empty relic slot", rs.x + 10, rs.y + 12, 16, Pal::BrassDk);
            }
        }

        bool inParty = InParty(g, h.id);
        int partyCount = 0;
        for (int id : g.party) if (id >= 0) partyCount++;
        if (inParty) {
            if (Button({352, 660, 190, 34}, "Remove from party")) {
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
            }
        } else if (Button({352, 660, 190, 34}, h.onLeave ? "On sick leave" : "Add to party", h.onLeave == 0 && partyCount < PARTY_SIZE)) {
            CompactParty(g);
            for (auto& id : g.party) if (id < 0) { id = h.id; break; }
        }

        bool armed = g.dismissArmed == h.id;
        if (Button({760, 660, 170, 34}, armed ? "Click to confirm" : "Dismiss", g.roster.size() > 1)) {
            if (!armed) {
                g.dismissArmed = h.id;
            } else {
                for (int r : h.relics) if (r >= 0) g.relicStorage.push_back(r);
                for (auto& id : g.party) if (id == h.id) id = -1;
                std::string name = h.name;
                int id = h.id;
                g.roster.erase(std::remove_if(g.roster.begin(), g.roster.end(), [id](const Hero& x) { return x.id == id; }), g.roster.end());
                CompactParty(g);
                g.selectedHero = -1;
                g.dismissArmed = -1;
                Toast(g, name + " has left the Nautilus. Their relics went to storage.");
                return;
            }
        }
    }

    // --- relic storage
    Panel({960, 90, 300, 612});
    Txt("Relic storage", 976, 102, 22, Pal::Ink);
    Txt("Two relics per crew member", 976, 128, 14, Pal::BrassDk);
    int n = (int)g.relicStorage.size();
    if (CheckCollisionPointRec(m, {960, 90, 300, 612})) g.relicScroll -= (int)GetMouseWheelMove();
    g.relicScroll = std::clamp(g.relicScroll, 0, std::max(0, n - 10));
    bool canEquip = sel && (sel->relics[0] < 0 || sel->relics[1] < 0);
    if (n == 0) Txt("Empty. Finish expeditions to find relics.", 976, 160, 15, Pal::BrassDk);
    for (int i = g.relicScroll; i < n && i < g.relicScroll + 10; i++) {
        int rid = g.relicStorage[i];
        const RelicDef& rd = Relics()[rid];
        Rectangle r{972, 150 + (i - g.relicScroll) * 54.0f, 276, 48};
        DrawRectangleRounded(r, 0.2f, 6, Color{222, 206, 170, 255});
        Txt(rd.name, r.x + 8, r.y + 5, 17, Pal::Ink);
        Txt(rd.desc, r.x + 8, r.y + 27, 13, Pal::BrassDk);
        if (Button({r.x + r.width - 70, r.y + 9, 62, 30}, "Equip", canEquip)) {
            int slot = sel->relics[0] < 0 ? 0 : 1;
            sel->relics[slot] = rid;
            sel->hp += rd.hp;
            g.relicStorage.erase(g.relicStorage.begin() + i);
            break;
        }
    }
    if (n > 10) Txt("Scroll for more", 976, 690, 14, Pal::BrassDk);
}

// ============================================================ radar
void SceneRadar(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Radar Station", "Recruits signal from passing ships; salvagers sell relics");
    DrawGoldBadge(g);

    Panel({30, 90, 620, 612});
    Txt("Incoming signals: recruits (100g each)", 50, 104, 22, Pal::Ink);
    for (size_t i = 0; i < g.recruits.size(); i++) {
        Hero& h = g.recruits[i];
        Rectangle c{48, 140 + i * 150.0f, 584, 138};
        DrawRectangleRounded(c, 0.08f, 6, Color{222, 206, 170, 255});
        DrawRectangle((int)c.x, (int)c.y + 10, 8, (int)c.height - 20, ClassColor(h.cls));
        Txt(h.name, c.x + 20, c.y + 10, 24, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 30 + MeasureText(h.name.c_str(), 24), c.y + 16, 18, Pal::BrassDk);
        DrawWrapped(ClassBlurb(h.cls), {c.x + 20, c.y + 44, 400, 60}, 16, Pal::Ink);
        Stats s = GetStats(h);
        Txt(TextFormat("HP %d   Dmg %d-%d   Spd %d   Dodge %d   Prot %d", s.maxHp, s.dmgMin, s.dmgMax, s.speed, s.dodge, s.prot),
            c.x + 20, c.y + 108, 15, Pal::BrassDk);
        bool ok = g.gold >= 100 && (int)g.roster.size() < MAX_ROSTER;
        if (Button({c.x + c.width - 140, c.y + 44, 124, 44}, "Hire", ok)) {
            g.gold -= 100;
            g.roster.push_back(h);
            Toast(g, h.name + " the " + ClassName(h.cls) + " joins the crew!");
            g.recruits.erase(g.recruits.begin() + i);
            break;
        }
    }
    if ((int)g.roster.size() >= MAX_ROSTER) Txt("Crew quarters are full (8). Dismiss someone to hire.", 50, 600, 17, Pal::Coral);
    if (Button({48, 640, 300, 44}, "Scan for new signals (20g)", g.gold >= 20)) {
        g.gold -= 20;
        g.recruits.clear();
        for (int i = 0; i < 3; i++) g.recruits.push_back(MakeRandomHero(g));
    }

    Panel({670, 90, 580, 612});
    Txt("Salvage market: relics", 690, 104, 22, Pal::Ink);
    for (size_t i = 0; i < g.shopRelics.size(); i++) {
        const RelicDef& rd = Relics()[g.shopRelics[i]];
        Rectangle c{688, 140 + i * 100.0f, 544, 88};
        DrawRectangleRounded(c, 0.1f, 6, Color{222, 206, 170, 255});
        Txt(rd.name, c.x + 16, c.y + 12, 24, Pal::Ink);
        Txt(rd.desc, c.x + 16, c.y + 46, 18, Pal::BrassDk);
        if (Button({c.x + c.width - 150, c.y + 22, 134, 44}, TextFormat("Buy %dg", rd.price), g.gold >= rd.price)) {
            g.gold -= rd.price;
            g.relicStorage.push_back(g.shopRelics[i]);
            Toast(g, "Bought the " + rd.name + ". It's waiting in Crew Quarters.");
            g.shopRelics.erase(g.shopRelics.begin() + i);
            break;
        }
    }
    Rectangle b{688, 450, 544, 88};
    DrawRectangleRounded(b, 0.1f, 6, Color{222, 206, 170, 255});
    Txt("Flashlight battery", b.x + 16, b.y + 12, 24, Pal::Ink);
    Txt(TextFormat("+40 light on an expedition. You have %d.", g.batteries), b.x + 16, b.y + 46, 18, Pal::BrassDk);
    if (Button({b.x + b.width - 150, b.y + 22, 134, 44}, "Buy 15g", g.gold >= 15)) { g.gold -= 15; g.batteries++; }
    DrawWrapped("Crew training, hull upgrades and discounts will live here in a later build. Stock refreshes after every expedition.",
                {690, 560, 540, 80}, 16, Pal::BrassDk);
}

// ============================================================ ward
void SceneWard(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Ward", "Injured crew can be patched up here (3 gold per HP)");
    DrawGoldBadge(g);
    int shown = 0, totalCost = 0;
    for (auto& h : g.roster) {
        Stats s = GetStats(h);
        int missing = s.maxHp - h.hp;
        if (missing <= 0) continue;
        int cost = missing * 3;
        totalCost += cost;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        Txt(h.name, c.x + 18, c.y + 14, 24, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureText(h.name.c_str(), 24), c.y + 20, 18, Pal::BrassDk);
        Txt(TextFormat("HP %d / %d", h.hp, s.maxHp), c.x + 18, c.y + 50, 20, Pal::Ink);
        DrawBar({c.x + 18, c.y + 76, 240, 10}, (float)h.hp / s.maxHp, Pal::Good);
        if (Button({c.x + c.width - 170, c.y + 30, 150, 44}, TextFormat("Treat %dg", cost), g.gold >= cost)) {
            g.gold -= cost;
            h.hp = s.maxHp;
        }
        shown++;
    }
    if (shown == 0) DrawTextCentered("Everyone is shipshape. No patients today.", SCREEN_W / 2.0f, 320, 28, Pal::Paper);
    else if (shown > 1 && Button({SCREEN_W / 2.0f - 150, 640, 300, 48}, TextFormat("Treat everyone (%dg)", totalCost), g.gold >= totalCost)) {
        g.gold -= totalCost;
        for (auto& h : g.roster) h.hp = GetStats(h).maxHp;
    }
}

// ============================================================ sick leave
void SceneSickLeave(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Sick Leave", "Rest clears all stress and cures Rattled, but they sit out the next expedition");
    DrawGoldBadge(g);
    int shown = 0;
    for (auto& h : g.roster) {
        if (h.stress <= 0 && !h.rattled && h.onLeave == 0) continue;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        Txt(h.name, c.x + 18, c.y + 14, 24, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureText(h.name.c_str(), 24), c.y + 20, 18, Pal::BrassDk);
        if (h.onLeave) {
            Txt("Resting. Back after the next expedition.", c.x + 18, c.y + 56, 18, Pal::Stress);
        } else {
            Txt(TextFormat("Nerves %d / 100%s", h.stress, h.rattled ? "   RATTLED" : ""), c.x + 18, c.y + 50, 20, h.rattled ? Pal::Bad : Pal::Ink);
            DrawBar({c.x + 18, c.y + 76, 240, 10}, h.stress / 100.0f, Pal::Stress);
            int cost = std::max(10, h.stress + (h.rattled ? 40 : 0));
            if (Button({c.x + c.width - 190, c.y + 30, 170, 44}, TextFormat("Grant leave %dg", cost), g.gold >= cost)) {
                g.gold -= cost;
                h.stress = 0;
                h.rattled = false;
                h.onLeave = 1;
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
                Toast(g, h.name + " is on sick leave and will miss the next expedition.");
            }
        }
        shown++;
    }
    if (shown == 0) DrawTextCentered("Nerves of steel all round. Nobody needs leave.", SCREEN_W / 2.0f, 320, 28, Pal::Paper);
}

// ============================================================ library
void SceneBookshelf(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Library", "Everything the crew has learned about the deep");
    const char* tabs[] = {"Crew", "Conditions", "Light & Nerves", "Bestiary", "Platforming"};
    for (int i = 0; i < 5; i++) {
        Rectangle r{140 + i * 205.0f, 92, 195, 42};
        if (i == g.bookTab) DrawRectangleRounded({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 0.3f, 6, Pal::Teal);
        if (Button(r, tabs[i])) g.bookTab = i;
    }
    Rectangle page{80, 150, SCREEN_W - 160.0f, 550};
    Panel(page);
    Rectangle body{page.x + 30, page.y + 24, page.width - 60, page.height - 40};

    if (g.bookTab == 0) {
        for (int c = 0; c < (int)HeroClass::COUNT; c++) {
            float y = body.y + c * 128.0f;
            HeroClass hc = (HeroClass)c;
            DrawRectangle((int)body.x, (int)y + 4, 8, 110, ClassColor(hc));
            Txt(ClassName(hc), body.x + 20, y, 26, Pal::Ink);
            DrawWrapped(ClassBlurb(hc), {body.x + 20, y + 32, body.width - 20, 40}, 17, Pal::Ink);
            std::string ab;
            for (auto& a : ClassAbilities(hc)) ab += a.name + "  |  ";
            Txt(ab.substr(0, ab.size() - 5), body.x + 20, y + 80, 16, Pal::Copper);
        }
    } else if (g.bookTab == 1) {
        DrawWrapped(
            "BLEED: loses health at the start of each turn for 3 turns.\n\n"
            "POISON: loses health at the start of each turn for 3 turns. (Planned: poison will stack and weaken healing so it plays differently from bleed.)\n\n"
            "STUN: loses their next turn.\n\n"
            "RALLIED: deals extra damage for a few turns.\n\n"
            "GUARDING: enemies must attack this crew member, who takes reduced damage.\n\n"
            "DEATH'S DOOR: a crew member at 0 HP is still standing, but every further hit has a chance to finish them. Any healing pulls them back.\n\n"
            "RATTLED: a crew member whose Nerves reach 100 is Rattled: less accurate, easier to hit, and may freeze up in combat. Only Sick Leave cures it.",
            body, 19, Pal::Ink);
    } else if (g.bookTab == 2) {
        DrawWrapped(
            "THE FLASHLIGHT\nEvery room you advance into drains the flashlight. Swap in a battery to recharge it.\n\n"
            "Bright (75+): your crew aim better.\n"
            "Dim (50-74): normal conditions.\n"
            "Murky (1-49): enemies hit more often and your crew's nerves fray faster, but the loot is 40% richer.\n"
            "Pitch Black (0): enemies are deadly accurate and stress piles up quickly, but loot nearly doubles.\n\n"
            "NERVES\nScary attacks, critical hits and falling to Death's Door all add stress. The Nurse's Smelling Salts and the Captain's orders bring it down, and a critical hit by your own crew lifts everyone's spirits.\n"
            "Retreating from an expedition adds stress to the whole party.",
            body, 19, Pal::Ink);
    } else if (g.bookTab == 3) {
        DrawWrapped(
            "THE CAVE (Shallows)\n\n"
            "SEA LOUSE: fast, fragile nibblers. Their bites can cause bleeding.\n\n"
            "PISTOL SHRIMP: tough little shells. Their claw can stun, and their sonic pop rattles nerves from any range.\n\n"
            "BRINE WORM: slow, but spits poison at any rank.\n\n"
            "THE LOBSTER (mini boss): heavily armored. Crushing Claw can stun, Tail Sweep hits your front two ranks, and its clacking frays everyone's nerves. "
            "Bleed and poison ignore its armor.\n\n"
            "Still uncharted: the Island, the Weeds, and Atlantis.",
            body, 19, Pal::Ink);
    } else {
        DrawWrapped(
            "THE PIPES (easy): a run through the Nautilus's steam pipes. Coins are worth 2 gold each, plus 25 gold for reaching the valve. "
            "Falling or touching steam just sends you back to the last checkpoint. Nobody dies for real here.\n\n"
            "Each Pipes layout is stitched together from hand-built sections. Beat it and a new layout is shuffled in. Stuck on a layout? Pay 10 gold at the periscope to reshuffle it.\n\n"
            "Controls: A/D or arrow keys to move, Space/W/Up to jump (hold for higher), Esc to give up the run.\n\n"
            "THE HULL (medium, coming soon): outside the hull among the growths, with an optional Kraken fight for a relic.\n\n"
            "THE PIRATE SHIP (hard, coming soon): precise jumps, pirates, parakeets, and Blackbeard. Sometimes it's a ghost ship: faster foes, double rewards.",
            body, 19, Pal::Ink);
    }
}

// ============================================================ periscope
void ScenePeriscope(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Periscope", "Platforming runs: nobody dies here, and there's gold to be had");
    DrawGoldBadge(g);
    struct Lvl { const char* name; const char* diff; const char* desc; bool open; };
    const Lvl lv[3] = {
        {"The Pipes", "EASY", "Hop through the Nautilus's steam pipes. Dodge vents, stomp rats, grab coins, and turn the valve at the end.", true},
        {"The Hull", "MEDIUM", "Outside the hull among the growths and fishing lines. Optional Kraken boss for a chance at a relic.", false},
        {"The Pirate Ship", "HARD", "Precision jumps past pirates and parakeets up to Blackbeard. Sometimes it's a ghost ship.", false},
    };
    for (int i = 0; i < 3; i++) {
        Rectangle c{60 + i * 400.0f, 110, 370, 470};
        Panel(c, lv[i].open ? Pal::Paper : Color{190, 180, 160, 255});
        DrawRectangleRec({c.x + 14, c.y + 14, c.width - 28, 120},
                         i == 0 ? Pal::Copper : i == 1 ? Color{50, 110, 130, 255} : Color{90, 70, 60, 255});
        DrawTextCentered(lv[i].name, c.x + c.width / 2, c.y + 50, 32, Pal::Paper);
        DrawTextCentered(lv[i].diff, c.x + c.width / 2, c.y + 92, 20, Pal::Paper);
        DrawWrapped(lv[i].desc, {c.x + 20, c.y + 150, c.width - 40, 120}, 18, Pal::Ink);
        if (lv[i].open) {
            Txt("Reward: 2 gold per coin", c.x + 20, c.y + 270, 18, Pal::BrassDk);
            Txt("+25 gold for reaching the valve", c.x + 20, c.y + 294, 18, Pal::BrassDk);
            Txt(TextFormat("Current layout: %s", PipesLayoutCode(g).c_str()), c.x + 20, c.y + 330, 20, Pal::Ink);
            if (Button({c.x + 20, c.y + 364, c.width - 40, 44}, "Dive in")) { StartPipes(g); return; }
            if (Button({c.x + 20, c.y + 414, c.width - 40, 40}, "Reshuffle layout (10g)", g.gold >= 10)) {
                g.gold -= 10;
                GeneratePipesLayout(g);
                Toast(g, "The pipes rattle and rearrange themselves...");
            }
        } else {
            DrawTextCentered("Coming in a later build", c.x + c.width / 2, c.y + 400, 22, Pal::BrassDk);
        }
    }
    DrawTextCentered("A/D or arrows to move   |   Space to jump (hold for height)   |   Esc to give up",
                     SCREEN_W / 2.0f, 610, 20, Pal::Paper);
}
