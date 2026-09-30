// Red Tide's arcade pages (stage 9): the dossier (a tabbed bestiary per map; pages earned in matches), the records,
// the how-to-play page (the Owners' standing orders), the Salt Charm pouch and the Locker of cosmetics. Opened from
// the Deep Arcade's Red Tide reel; drawn full screen with the match renderer so a page shows the beast itself.
#include "redtide.h"
#include "redtide_profile.h"
#include "redtide_render.h"
#include "game.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace rt {

static int gPage = 0;                          // 0 none; 1 dossier, 2 records, 3 how to play, 4 charms, 5 locker
static int gMap = 0, gSel = 0, gScroll = 0;
static const char* MAPS[] = {"ship", "cave", "reef", "atlantis", "void"};
static const char* TITLES[] = {"The Sunken Ship", "The Underwater Cave", "The Coral Reef", "Atlantis", "Approaching the Void"};
static const Color INK{36, 30, 24, 255}, CHALK{232, 226, 206, 255}, SLATE{24, 34, 36, 255};

struct Page { std::string name, cls, size, tier, diet, eatenBy, weak, note, how; bool flora = false, faction = false; };

static std::vector<Page> PagesFor(int mi) {
    static std::vector<Page> cache[5];
    static bool built[5] = {};
    if (built[mi]) return cache[mi];
    built[mi] = true;
    Json d = LoadJsonFile(DataDir() + "/engine/dossier_" + MAPS[mi] + ".json");
    const MapData& m = Map(MAPS[mi]);
    for (const Json& r : d.a) {
        Page p;
        p.name = r["beast"].Str0(); p.cls = r["class"].Str0(); p.size = r["size"].IsNum() ? std::to_string(r["size"].I()) : r["size"].Str0();
        p.tier = r["tier"].IsNum() ? std::to_string(r["tier"].I()) : r["tier"].Str0();
        p.diet = r["diet (from the diet matrix)"].Str0(); p.eatenBy = r["eaten by"].Str0(); p.weak = r["weak point"].Str0();
        p.note = r["field note"].Str0(); p.how = r["how to handle it"].Str0();
        p.flora = m.SpeciesIndex(p.name) < 0;
        cache[mi].push_back(p);
    }
    // the faction's page, from its sheet
    const Faction& f = m.faction;
    if (!f.units.empty()) {
        Page p; p.faction = true;
        p.name = f.name.empty() ? f.speciesName : f.name; p.cls = "Faction"; p.size = "squads"; p.tier = "enemy";
        for (const auto& u : f.units) { if (!p.diet.empty()) p.diet += "; "; p.diet += u.unit + " (" + u.weapon + ")"; }
        p.eatenBy = "The web's big hunters, if they bleed"; p.weak = "Flanks; their tells: ";
        for (const auto& u : f.units) if (!u.tell.empty()) { p.weak += u.tell + ". "; break; }
        p.note = "They come to noise: fire enough in one place and a squad enters at " + f.entryPoi + ".";
        p.how = "Fight where the reef fights them. Knives bring nobody.";
        cache[mi].push_back(p);
    }
    return cache[mi];
}

void OpenRedTidePage(Game& g, int page) {
    gPage = page; gSel = 0; gScroll = 0;
    EnableCursor();
    g.scene = Scene::RedTide;
}
bool RedTidePageOpen() { return gPage != 0; }

static bool Tab(Rectangle r, const char* t, bool on) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, on ? Color{60, 76, 70, 255} : hov ? Color{40, 54, 52, 255} : Color{28, 40, 40, 255});
    DrawRectangleLinesEx(r, 1, on ? Pal::Brass : Pal::BrassDk);
    DrawTextCentered(t, r.x + r.width / 2, r.y + 6, 15, on ? CHALK : Fade(CHALK, 0.7f));
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void BackRow(Game& g) {
    if (Button({30, 20, 130, 38}, "Back", true, 18) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE)) { gPage = 0; g.scene = Scene::Arcade; }
    const Profile& p = GetProfile();
    int next = p.NextRankAt();
    TxtBold(TextFormat("Rank %d", p.Rank()), SCREEN_W - 330, 24, 20, Pal::Brass);
    Txt(TextFormat("%d tokens%s", p.tokens, next > 0 ? TextFormat("   (next rank at %d earned)", next) : ""), SCREEN_W - 330, 50, 14, Fade(CHALK, 0.8f));
}

static void DrawBeast(const std::string& mapKey, const Page& pg, bool known, Rectangle view, float t) {
    // the beast itself, turning slowly in the dark (a black silhouette until the page is earned)
    Camera3D cam{}; cam.position = {0, 0.4f, -7}; cam.target = {0, 0, 0}; cam.up = {0, 1, 0}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE;
    SceneLight L; L.lampPos = {2, 4, -7}; L.lampDir = Vector3Normalize({-0.2f, -0.4f, 1}); L.lampRange = 40; L.lampCone = 0.3f;
    L.fogDensity = 0.01f; L.fog = {12, 22, 26, 255}; L.time = t;   // (a page not yet earned shows the beast as a black shape)
    // place the beast in the left window: offset the target so it sits in the view rectangle
    float cx = (view.x + view.width / 2) / SCREEN_W - 0.5f;
    Vector3 at{-cx * 5.6f, 0, 0};
    RenderBegin(cam, L);
    if (!pg.flora && !pg.faction) {
        const CreatureModel& cm = Creature(mapKey, pg.name);
        float sc = 2.2f / std::max(0.05f, cm.extent);
        DrawCreature(cm, at, t * 0.5f + 1.2f, 0, sc, t * cm.freq, 0.7f, known ? WHITE : Color{14, 14, 16, 255});
    } else if (pg.flora) {
        Color c = known ? Color{90, 170, 110, 255} : BLACK;
        for (int k = 0; k < 7; k++) DrawWorldCube(Vector3Add(at, {sinf(k * 1.7f) * 0.8f, -0.6f + (k % 3) * 0.4f, cosf(k * 1.7f) * 0.6f}), {0.2f, 0.9f + (k % 2) * 0.5f, 0.2f}, c);
    } else {
        Color c = known ? Color{120, 100, 80, 255} : BLACK;
        DrawWorldCube(Vector3Add(at, {0, 0, 0}), {0.6f, 1.6f, 0.4f}, c);
        DrawWorldCube(Vector3Add(at, {0, 1.1f, 0}), {0.5f, 0.5f, 0.5f}, c);
    }
    RenderEnd();
}

static void DossierPage(Game& g, float t) {
    const Profile& prof = GetProfile();
    std::vector<Page> pages = PagesFor(gMap);
    gSel = std::clamp(gSel, 0, (int)pages.size() - 1);
    const Page& pg = pages[gSel];
    std::string key = std::string(MAPS[gMap]) + "|" + pg.name;
    bool known = prof.dossier.count(key) > 0;
    Rectangle view{380, 110, 360, 340};
    DrawBeast(MAPS[gMap], pg, known, view, t);
    BackRow(g);
    DrawTextCenteredBold("THE DOSSIER", SCREEN_W / 2.0f, 22, 28, CHALK);
    for (int i = 0; i < 5; i++) if (Tab({220 + i * 170.0f, 64, 164, 30}, TITLES[i], i == gMap)) { gMap = i; gSel = 0; gScroll = 0; }
    // the list
    int have = 0; for (const auto& p : pages) if (prof.dossier.count(std::string(MAPS[gMap]) + "|" + p.name)) have++;
    Rectangle list{30, 110, 330, 580};
    DrawRectangleRec(list, Fade(SLATE, 0.92f));
    DrawRectangleLinesEx(list, 1, Pal::BrassDk);
    TxtBold(TextFormat("%d of %d pages", have, (int)pages.size()), list.x + 12, list.y + 8, 16, Pal::Brass);
    int rows = 26;
    float wheel = GetMouseWheelMove();
    if (CheckCollisionPointRec(GetMousePosition(), list)) gScroll = std::clamp(gScroll - (int)wheel * 3, 0, std::max(0, (int)pages.size() - rows));
    if (IsKeyPressed(KEY_DOWN)) gSel = std::min((int)pages.size() - 1, gSel + 1);
    if (IsKeyPressed(KEY_UP)) gSel = std::max(0, gSel - 1);
    if (gSel < gScroll) gScroll = gSel; if (gSel >= gScroll + rows) gScroll = gSel - rows + 1;
    for (int r = 0; r < rows && gScroll + r < (int)pages.size(); r++) {
        int i = gScroll + r;
        const Page& p = pages[i];
        bool k = prof.dossier.count(std::string(MAPS[gMap]) + "|" + p.name) > 0;
        Rectangle rr{list.x + 6, list.y + 34 + r * 20.5f, list.width - 12, 20};
        bool hov = CheckCollisionPointRec(GetMousePosition(), rr);
        if (i == gSel) DrawRectangleRec(rr, Color{60, 80, 74, 255}); else if (hov) DrawRectangleRec(rr, Color{40, 54, 52, 255});
        Txt(k ? p.name : std::string("? ") + (p.flora ? "(a plant)" : p.faction ? "(a faction)" : "(a beast)"), rr.x + 8, rr.y + 2, 14, k ? CHALK : Fade(CHALK, 0.45f));
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gSel = i;
    }
    // the page
    Rectangle pr{760, 110, 490, 580};
    DrawRectangleRec(pr, Color{226, 214, 184, 250});
    DrawRectangleLinesEx(pr, 2, Pal::BrassDk);
    float y = pr.y + 16;
    if (!known) {
        TxtBold("A page not yet written", pr.x + 20, y, 24, INK); y += 40;
        DrawWrapped(pg.flora ? "Swim beside it for half a minute and the page fills in." : "Kill one, or watch one for half a minute in all, and the page fills in: what it eats, what eats it, where it's soft, and what it's afraid of.", {pr.x + 20, y, pr.width - 40, 120}, 17, INK);
        return;
    }
    TxtBold(pg.name, pr.x + 20, y, 26, INK); y += 36;
    Txt(TextFormat("%s   -   size %s   -   %s", pg.cls.c_str(), pg.size.c_str(), pg.tier.c_str()), pr.x + 20, y, 15, Fade(INK, 0.8f)); y += 30;
    auto field = [&](const char* h, const std::string& v, float hgt) {
        if (v.empty()) return;
        TxtBold(h, pr.x + 20, y, 15, Color{120, 60, 40, 255}); y += 20;
        DrawWrapped(v, {pr.x + 20, y, pr.width - 40, hgt}, 15, INK); y += hgt + 8;
    };
    field(pg.faction ? "Units" : "Eats", pg.diet, 56);
    field("Eaten by", pg.eatenBy, 40);
    field("Weak point", pg.weak, 36);
    field("Field note", pg.note, 70);
    field("How to handle it", pg.how, 90);
}

static void RecordsPage(Game& g) {
    const Profile& p = GetProfile();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, SLATE);
    BackRow(g);
    DrawTextCenteredBold("RECORDS", SCREEN_W / 2.0f, 22, 28, CHALK);
    float y = 120;
    const char* hd[] = {"Map", "Highest tide", "Most scrip", "Longest survival", "Fastest Forge", "Dossier", "Firsts"};
    float cols[] = {60, 330, 480, 630, 800, 950, 1060};
    for (int k = 0; k < 7; k++) TxtBold(hd[k], cols[k], y, 16, Pal::Brass);
    y += 34;
    for (int i = 0; i < 5; i++) {
        std::string m = MAPS[i];
        auto get = [&](const std::map<std::string, int>& mm) { auto it = mm.find(m); return it == mm.end() ? -1 : it->second; };
        int tide = get(p.bestTide), scrip = get(p.bestScrip), tm = get(p.bestTimeS), fg = get(p.forgeTimeS);
        int have = 0; for (const auto& k : p.dossier) if (k.rfind(m + "|", 0) == 0) have++;
        std::string firsts;
        for (const char* f : {"tide20", "boss", "quest", "dossier"}) if (p.firsts.count(m + ":" + f)) firsts += std::string(firsts.empty() ? "" : ", ") + f;
        TxtBold(TITLES[i], cols[0], y, 17, CHALK);
        Txt(tide >= 0 ? std::to_string(tide) : "-", cols[1], y, 17, CHALK);
        Txt(scrip >= 0 ? std::to_string(scrip) : "-", cols[2], y, 17, CHALK);
        Txt(tm >= 0 ? TextFormat("%d:%02d", tm / 60, tm % 60) : "-", cols[3], y, 17, CHALK);
        Txt(fg >= 0 ? TextFormat("%d:%02d", fg / 60, fg % 60) : "-", cols[4], y, 17, CHALK);
        Txt(TextFormat("%d / %d", have, DossierTotal(m)), cols[5], y, 17, CHALK);
        Txt(firsts.empty() ? "-" : firsts, cols[6], y, 14, Fade(CHALK, 0.8f));
        y += 40;
    }
    y += 30;
    TxtBold("Bonus pages", 60, y, 18, Pal::Brass); y += 30;
    static const std::pair<const char*, const char*> BONUS[] = {
        {"owners", "The Owners: a salvage concern that pays scrip for cleared water and asks no questions."},
        {"expedition", "The expedition: the cave swallowed a party whose lanterns still burn in the dry chambers."},
        {"station", "The station: it bred what it found in the abyss, and the abyss bred back."},
        {"finallog", "The final log: 'We were breeding it back. The Relict was ours, once.'"},
    };
    for (const auto& b : BONUS) { bool got = p.bonus.count(b.first) > 0; Txt(got ? b.second : "? A page still to find.", 60, y, 16, got ? CHALK : Fade(CHALK, 0.4f)); y += 26; }
}

static void HowToPage(Game& g) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{30, 34, 30, 255});
    // the torn log page
    Rectangle pg{120, 70, SCREEN_W - 240.0f, SCREEN_H - 100.0f};
    DrawRectangleRec(pg, Color{214, 202, 170, 255});
    for (int k = 0; k < 40; k++) DrawTri({pg.x + k * pg.width / 40, pg.y}, {pg.x + (k + 0.5f) * pg.width / 40, pg.y - 6 - (k * 7 % 5)}, {pg.x + (k + 1) * pg.width / 40, pg.y}, Color{214, 202, 170, 255});
    BackRow(g);
    static const char* PARAS[] = {
        "1. The job. You and up to three others go down with a pistol, a knife, two charges, and five hundred scrip. You kill what lives there and the Owners pay by the head. Every tide sets a quota; meet it and the next tide is worth more and wants more of you. There is no end. There is only how deep you get.",
        "2. Scrip. Ten for a hit, the bounty for a kill, more for the eyes and the gills. Scrip opens doors, buys guns off the walls, pulls from Davy's Locker, pours tonics, and feeds the Pressure Forge. It's yours alone; the Fair Shares charm is the only way to split it. Spend between tides. Twenty seconds of calm is all you get.",
        "3. Blood. Everything you shoot bleeds, and everything you kill bleeds more. Blood drifts with the current and the water remembers it. Small things smell a little blood and come to eat. Big things need a lot of blood, but ten small kills make as much as one big one. Scavengers clean it up; kill them and nothing does. Watch the red at the edge of your mask, watch the vial, and when the pulse starts, leave.",
        "4. Noise. Guns are heard. Fire enough in one place and someone comes to see who's spending the reef: Wreckers, the Drowned, the tribe, the Lost Ones, what's left of the station. They hunt you, and the reef hunts them, and the smart diver stands where the second thing happens. Four divers firing in one room bring them four times faster than four rooms. Knives bring nobody.",
        "5. Going down. At zero you're floating with the pistol and forty-five seconds, and you're bleeding into the water the whole time. A friend can pull you up in four; fewer with Quick Brine. Dead is when everyone's down. Cold Coffin and Quick Brine buy second chances; nothing buys a third.",
        "6. The rest you learn by dying. Which coral grabs. Which light isn't a light. Which fish is on your side until you shoot it. Which door the shark can't fit through. The dossier fills in as you go: every beast you kill or watch for half a minute tells you what it eats, what eats it, where it's soft, and what it's afraid of. Read it between matches. It's the only map that matters.",
    };
    DrawTextCenteredBold("THE OWNERS' STANDING ORDERS FOR DIVERS", SCREEN_W / 2.0f, pg.y + 16, 24, INK);
    float y = pg.y + 56;
    for (int k = 0; k < 6; k++) { float w = pg.width * 0.66f; int lines = (int)ceilf(MeasureTxt(PARAS[k], 15) / (w - 20)) ; DrawWrapped(PARAS[k], {pg.x + 30, y, w, lines * 21.0f + 4}, 15, INK); y += lines * 20.0f + 10; }
    // the chalk diagram: a diver, a dead fish, three arrows
    float dx = pg.x + pg.width * 0.72f, dy = pg.y + 120;
    Color ch{60, 50, 40, 255};
    DrawCircleLines((int)dx + 40, (int)dy + 20, 14, ch); DrawLineEx({dx + 40, dy + 34}, {dx + 40, dy + 80}, 3, ch);
    DrawLineEx({dx + 40, dy + 50}, {dx + 70, dy + 60}, 3, ch); DrawLineEx({dx + 40, dy + 80}, {dx + 26, dy + 110}, 3, ch); DrawLineEx({dx + 40, dy + 80}, {dx + 54, dy + 110}, 3, ch);
    DrawEllipseLines((int)dx + 150, (int)dy + 70, 28, 12, ch); DrawLineEx({dx + 178, dy + 70}, {dx + 196, dy + 58}, 2, ch); DrawLineEx({dx + 178, dy + 70}, {dx + 196, dy + 82}, 2, ch);
    DrawText("x", (int)dx + 128, (int)dy + 62, 18, ch);
    for (int k = 0; k < 3; k++) { float ay = dy + 150 + k * 40; DrawLineEx({dx + 230, ay}, {dx + 170, ay - 40 + k * 20}, 3, Color{150, 40, 30, 255}); DrawCircle((int)dx + 170, (int)(ay - 40 + k * 20), 4, Color{150, 40, 30, 255}); }
    DrawWrapped("One kill: a little blood, the scavengers come. Ten kills in one place: the shark comes. A diver down: everything comes. Move, spread out, keep it quiet, and let the water forget you.", {dx, dy + 280, pg.width * 0.26f, 140}, 14, ch);
    DrawWrapped("Controls: WASD swim, Space/Ctrl up/down, Shift sprint, RMB aim, LMB fire, R reload, E use, V knife, G throw (Q picks), B build, X brush, Z bench stock, F drum, T charm, 1-3 guns.", {dx, dy + 380, pg.width * 0.26f, 100}, 13, ch);
    DrawTextCentered("The Owners are not responsible for anything that happens below the surface, including the surface.", SCREEN_W / 2.0f, pg.y + pg.height - 26, 13, Fade(INK, 0.7f));
}

void CharmsPage(Game& g);    // stage 9b
void LockerPage(Game& g);

bool RedTidePageFrame(Game& g, float t) {
    if (gPage == 0) return false;
    if (gPage == 1) DossierPage(g, t);
    else if (gPage == 2) RecordsPage(g);
    else if (gPage == 3) HowToPage(g);
    else if (gPage == 4) CharmsPage(g);
    else if (gPage == 5) LockerPage(g);
    return true;
}

void DebugRedTidePage(int page, int map, int sel) {
    // --shots: a page, with (in memory only) its selected dossier page earned when sel >= 0
    gPage = page; gMap = map; gSel = std::max(0, sel); gScroll = 0;
    Profile& p = GetProfile();
    if (sel >= 0 && page == 1) { std::vector<Page> pg = PagesFor(map); for (int i = 0; i <= sel && i < (int)pg.size(); i += 2) p.dossier.insert(std::string(MAPS[map]) + "|" + pg[i].name); }
    if (page == 4 && p.charms.empty()) { p.charms = {"brines", "circle", "fins"}; p.pouch = {"circle"}; }
}

} // namespace rt

namespace rt {

void CharmsPage(Game& g) {
    Profile& p = GetProfile();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, SLATE);
    BackRow(g);
    DrawTextCenteredBold("THE CHARM POUCH", SCREEN_W / 2.0f, 22, 28, CHALK);
    DrawTextCentered("Salt Charms go into a match in your pouch; T uses the next one. Each is spent once per match.", SCREEN_W / 2.0f, 74, 15, Fade(CHALK, 0.8f));
    // the pouch
    int slots = p.PouchSlots();
    for (int s = 0; s < 5; s++) {
        Rectangle r{SCREEN_W / 2.0f - 330 + s * 134, 96, 124, 70};
        bool open = s < slots;
        DrawRectangleRounded(r, 0.2f, 6, open ? Color{60, 50, 36, 255} : Color{30, 30, 30, 255});
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2, open ? Pal::Brass : Color{70, 70, 70, 255});
        if (!open) { DrawTextCentered("locked", r.x + r.width / 2, r.y + 26, 14, Fade(CHALK, 0.4f)); continue; }
        if (s < (int)p.pouch.size()) {
            std::string n; for (const auto& c : Charms()) if (c.id == p.pouch[s]) n = c.name;
            DrawWrapped(n, {r.x + 8, r.y + 12, r.width - 16, 50}, 15, CHALK);
            if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { p.pouch.erase(p.pouch.begin() + s); SaveProfile(); }
        } else DrawTextCentered("empty", r.x + r.width / 2, r.y + 26, 14, Fade(CHALK, 0.5f));
    }
    DrawTextCentered(TextFormat("%d of 5 slots open (more at ranks 7, 15, 25 and 40). Click a charm to pack it; click the pouch to take it out.", slots), SCREEN_W / 2.0f, 176, 14, Fade(CHALK, 0.7f));
    // the charms
    static const std::pair<const char*, const char*> HOW[] = {
        {"brines", "rank 2"}, {"circle", "rank 5"}, {"fins", "rank 9"}, {"luck", "rank 14"}, {"chum", "rank 20"}, {"shell", "rank 27"},
        {"clean", "rank 35"}, {"ghost", "rank 44"}, {"locker", "tide 15 on any map"}, {"shares", "tide 25 on any map"}};
    int i = 0;
    for (const auto& c : Charms()) {
        Rectangle r{120 + (i % 2) * 530.0f, 214 + (i / 2) * 92.0f, 510, 80};
        bool have = p.charms.count(c.id) > 0;
        bool packed = std::find(p.pouch.begin(), p.pouch.end(), c.id) != p.pouch.end();
        bool hov = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.12f, 6, have ? (hov ? Color{56, 70, 64, 255} : Color{40, 52, 50, 255}) : Color{28, 30, 30, 255});
        DrawRectangleRoundedLinesEx(r, 0.12f, 6, 1, packed ? Pal::Good : have ? Pal::Brass : Color{60, 60, 60, 255});
        TxtBold(c.name, r.x + 16, r.y + 10, 19, have ? CHALK : Fade(CHALK, 0.4f));
        Txt(c.effect, r.x + 16, r.y + 38, 15, have ? Fade(CHALK, 0.85f) : Fade(CHALK, 0.35f));
        const char* how = ""; for (const auto& h : HOW) if (c.id == h.first) how = h.second;
        Txt(have ? (packed ? "packed" : "") : TextFormat("unlocks at %s", how), r.x + r.width - 170, r.y + 10, 13, packed ? Pal::Good : Fade(CHALK, 0.5f));
        if (have && !packed && hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (int)p.pouch.size() < slots) { p.pouch.push_back(c.id); SaveProfile(); }
        i++;
    }
}

Color SuitColor(const std::string& id) {
    if (id == "verdigris") return {80, 150, 130, 255};
    if (id == "redtide") return {170, 40, 40, 255};
    if (id == "bone") return {220, 210, 190, 255};
    if (id == "pearl") return {210, 220, 230, 255};
    if (id == "atlantean") return {60, 90, 170, 255};
    return {150, 120, 70, 255};                          // the issue canvas
}
Color FinishColor(const std::string& id) {
    if (id == "barnacle") return {130, 124, 110, 255};
    if (id == "verdigrisf") return {90, 170, 150, 255};
    if (id == "bone_inlay") return {226, 214, 190, 255};
    if (id == "pearlf") return {230, 236, 240, 255};
    if (id == "redtidef") return {190, 40, 40, 255};
    return {0, 0, 0, 0};                                 // the brass as issued
}

void LockerPage(Game& g) {
    Profile& p = GetProfile();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, SLATE);
    BackRow(g);
    DrawTextCenteredBold("THE LOCKER ROOM", SCREEN_W / 2.0f, 22, 28, CHALK);
    DrawTextCentered("Finishes, suit colours and helmets, bought with arcade tokens. They never change a silhouette, or the fight.", SCREEN_W / 2.0f, 80, 15, Fade(CHALK, 0.8f));
    const char* kinds[] = {"suit", "helmet", "finish"};
    const char* heads[] = {"Suit colours", "Helmets", "Weapon finishes"};
    for (int k = 0; k < 3; k++) {
        float x = 70 + k * 390.0f, y = 110;
        TxtBold(heads[k], x, y, 20, Pal::Brass); y += 36;
        std::string& worn = k == 0 ? p.suit : k == 1 ? p.helmet : p.finish;
        // the issue kit
        {
            Rectangle r{x, y, 360, 44};
            bool on = worn.empty();
            DrawRectangleRounded(r, 0.2f, 6, on ? Color{60, 76, 60, 255} : Color{36, 46, 46, 255});
            Txt("As issued", r.x + 50, r.y + 12, 16, CHALK);
            if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { worn.clear(); SaveProfile(); }
            y += 52;
        }
        for (const auto& c : Cosmetics()) {
            if (c.kind != kinds[k]) continue;
            Rectangle r{x, y, 360, 44};
            bool own = p.owned.count(c.id) > 0, on = worn == c.id;
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            DrawRectangleRounded(r, 0.2f, 6, on ? Color{60, 76, 60, 255} : hov ? Color{46, 58, 56, 255} : Color{36, 46, 46, 255});
            Color sw = k == 2 ? FinishColor(c.id) : SuitColor(k == 1 ? c.id.substr(2) : c.id);
            DrawRectangleRounded({r.x + 10, r.y + 8, 28, 28}, 0.3f, 4, sw);
            Txt(c.name, r.x + 50, r.y + 12, 16, CHALK);
            Txt(on ? "worn" : own ? "wear" : TextFormat("%d tokens", c.cost), r.x + 270, r.y + 13, 14, on ? Pal::Good : own ? Fade(CHALK, 0.8f) : (p.tokens >= c.cost ? Pal::Brass : Fade(CHALK, 0.4f)));
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (own) worn = c.id;
                else if (p.tokens >= c.cost) { p.tokens -= c.cost; p.owned.insert(c.id); worn = c.id; }
                SaveProfile();
            }
            y += 52;
        }
    }
}

} // namespace rt

void OpenRedTidePage(Game& g, int page) { rt::OpenRedTidePage(g, page); }
