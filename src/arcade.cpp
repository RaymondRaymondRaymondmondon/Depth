// ============================================================================
//  The Deep Arcade: the cabinet's screen (the reels, Host / Join / Browse), the lobby and the Scuttle table.
//  The session and the rules live in arcade_session.* and scuttle.* (no raylib); this file only draws and clicks.
// ============================================================================
#include "game.h"
#include "arcade_session.h"
#include "scuttle.h"
#include "net.h"
#include "skins.h"
static int gWardrobe = -1;   // the skins page over the arcade (skins::TRAWL), -1 none
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
using namespace arcade;

Session gSess;
bool gTrawlFp = false;    // the Trawl's view for a networked match (the reel remembers the last one chosen)
int twCrew = 4;           // the Trawl's hands sailing solo (the rest are bots)
net::LanBrowser gBrowse;
bool gBrowsing = false;
enum Mode { MODE_MENU, MODE_JOIN, MODE_BROWSE, MODE_ROOM };
Mode gMode = MODE_MENU;
std::string gJoinText, gChatText, gError;
bool gNameFocus = false, gJoinFocus = false, gChatFocus = false;
int gSel = 2;           // Scuttle, the reel that is ready
float gDrum = 2;
Profile gProfile;
bool gProfileLoaded = false;
int gPickCard = -1;          // a card waiting for its target

int gLastVersion = -1;

const Color CRAB[scuttle::MAX_SEATS] = {{214, 70, 58, 255}, {70, 128, 214, 255}, {226, 182, 64, 255}, {86, 176, 96, 255}};
const char* CRAB_NAME[scuttle::MAX_SEATS] = {"Red", "Blue", "Gold", "Green"};
const Color SCREEN_INK{180, 255, 240, 255}, SCREEN_DIM{120, 170, 166, 255}, SCREEN_BG{10, 54, 60, 255};

// ---- the diver's arcade profile: a name the table shows, a random id, tokens (arcade_profile.txt next to the exe)
std::string ProfilePath() { return std::string(GetApplicationDirectory()) + "arcade_profile.txt"; }
void SaveProfile() {
    std::ofstream f(ProfilePath());
    f << "name " << gProfile.name << "\nid " << gProfile.id << "\n";
}
void LoadProfile() {
    gProfileLoaded = true;
    std::ifstream f(ProfilePath());
    std::string k;
    while (f >> k) {
        if (k == "name") { std::getline(f, gProfile.name); if (!gProfile.name.empty() && gProfile.name[0] == ' ') gProfile.name.erase(0, 1); }
        else if (k == "id") f >> gProfile.id;
    }
    if (gProfile.name.empty()) gProfile.name = "Diver";
    if (!gProfile.id) { gProfile.id = ((uint64_t)GetRandomValue(1, 0x7FFFFFFF) << 31) ^ (uint64_t)GetRandomValue(1, 0x7FFFFFFF); SaveProfile(); }
}

// a one-line text field: click to type, Enter returns true
bool TextField(Rectangle r, std::string& s, bool& focus, size_t maxLen, const char* hint, bool upper = false) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) focus = hover;
    DrawRectangleRounded(r, 0.2f, 6, Color{6, 30, 34, 255});
    DrawRectangleRoundedLinesEx(r, 0.2f, 6, 2, focus ? Pal::Brass : hover ? Pal::BrassDk : Color{40, 90, 90, 255});
    bool enter = false;
    if (focus) {
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= 32 && c < 127 && s.size() < maxLen) s += upper ? (char)toupper(c) : (char)c;
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) { if (!s.empty()) s.pop_back(); }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) enter = true;
    }
    int fs = (int)std::min(22.0f, r.height - 12);
    if (s.empty() && !focus) Txt(hint, r.x + 12, r.y + (r.height - fs) / 2, fs, Fade(SCREEN_DIM, 0.6f));
    else Txt(s + (focus && fmodf((float)GetTime(), 1.0f) < 0.5f ? "_" : ""), r.x + 12, r.y + (r.height - fs) / 2, fs, SCREEN_INK);
    return enter;
}

void DrawScreenPanel(Rectangle r) {
    DrawRectangleRounded({r.x + 6, r.y + 8, r.width, r.height}, 0.04f, 8, Fade(BLACK, 0.5f));
    DrawRectangleRounded({r.x - 10, r.y - 10, r.width + 20, r.height + 20}, 0.05f, 8, Pal::BrassDk);
    DrawRectangleRoundedLinesEx({r.x - 10, r.y - 10, r.width + 20, r.height + 20}, 0.05f, 8, 3, Pal::Brass);
    DrawRectangleRounded(r, 0.04f, 8, SCREEN_BG);
    for (float y = r.y + 3; y < r.y + r.height; y += 4) DrawLine((int)r.x + 4, (int)y, (int)(r.x + r.width - 4), (int)y, Fade(BLACK, 0.12f)); // scanlines
}

// ---- a crab, drawn in code: a round shell, two claws, legs that patter when it moves
void DrawCrab(Vector2 p, Color c, float t, float moving, bool held, float scale = 1) {
    float s = scale;
    DrawEllipse((int)p.x, (int)(p.y + 14 * s), (int)(26 * s), (int)(6 * s), Fade(BLACK, 0.35f));
    for (int side = -1; side <= 1; side += 2) for (int k = 0; k < 3; k++) {
        float ph = t * 18 * moving + k * 1.7f + (side > 0 ? 1.1f : 0);
        Vector2 a{p.x + side * (8 + k * 5) * s, p.y + 2 * s}, b{p.x + side * (18 + k * 5) * s, p.y + (12 + sinf(ph) * 3 * moving) * s};
        DrawLineEx(a, b, 3 * s, ColorBrightness(c, -0.45f));
    }
    for (int side = -1; side <= 1; side += 2) { // the claws, raised
        float wave = sinf(t * 3 + side) * 3;
        Vector2 arm{p.x + side * 20 * s, p.y - (12 + wave) * s};
        DrawLineEx({p.x + side * 12 * s, p.y - 4 * s}, arm, 4 * s, ColorBrightness(c, -0.3f));
        DrawCircleV(arm, 7 * s, ColorBrightness(c, -0.15f));
        DrawCircleV({arm.x + side * 3 * s, arm.y - 4 * s}, 4 * s, SCREEN_BG);   // the notch of the pincer
        DrawCircleLinesV(arm, 7 * s, Fade(BLACK, 0.6f));
    }
    DrawEllipse((int)p.x, (int)p.y, (int)(20 * s), (int)(13 * s), c);
    DrawEllipse((int)(p.x - 4 * s), (int)(p.y - 4 * s), (int)(10 * s), (int)(5 * s), Fade(WHITE, 0.25f));
    DrawEllipseLines((int)p.x, (int)p.y, (int)(20 * s), (int)(13 * s), Fade(BLACK, 0.7f));
    for (int side = -1; side <= 1; side += 2) { // eyes on stalks
        Vector2 e{p.x + side * 6 * s, p.y - 16 * s};
        DrawLineEx({p.x + side * 5 * s, p.y - 9 * s}, e, 2 * s, ColorBrightness(c, -0.4f));
        DrawCircleV(e, 3.2f * s, WHITE); DrawCircleV({e.x + 0.8f * s, e.y}, 1.6f * s, BLACK);
    }
    if (held) { DrawRing({p.x, p.y}, 22 * s, 25 * s, 0, 360, 24, Fade(Pal::Stress, 0.8f)); }
}

void DrawCard(Rectangle r, int card, bool hover, bool picked, bool enabled) {
    float lift = (hover || picked) && enabled ? -14 : 0;
    Rectangle b{r.x, r.y + lift, r.width, r.height};
    DrawRectangleRounded({b.x + 3, b.y + 5, b.width, b.height}, 0.08f, 6, Fade(BLACK, 0.5f));
    DrawRectangleRounded(b, 0.08f, 6, enabled ? Color{236, 224, 194, 255} : Color{160, 154, 138, 255});
    DrawRectangleRoundedLinesEx(b, 0.08f, 6, picked ? 4.0f : 2.0f, picked ? Pal::Coral : Pal::Ink);
    const scuttle::CardDef& d = scuttle::Card(card);
    int fs = 17; while (MeasureTxt(d.name, fs, true) > b.width - 12 && fs > 11) fs--;
    TxtBold(d.name, b.x + (b.width - MeasureTxt(d.name, fs, true)) / 2, b.y + 8, fs, Pal::Ink);
    // a small emblem: arrows for moves, a wave, a claw, a shell, a gull, a rock
    Vector2 m{b.x + b.width / 2, b.y + 56};
    Color ink{70, 50, 34, 255};
    switch (card) {
        case scuttle::C_SCUTTLE1: case scuttle::C_SCUTTLE2: case scuttle::C_SCUTTLE3: {
            int n = card - scuttle::C_SCUTTLE1 + 1;
            for (int k = 0; k < n; k++) { float x = m.x + (k - (n - 1) / 2.0f) * 16; DrawTri({x - 6, m.y - 9}, {x - 6, m.y + 9}, {x + 7, m.y}, ink); }
        } break;
        case scuttle::C_WAVE: case scuttle::C_CURRENT: case scuttle::C_TIDERUSH:
            for (int k = 0; k < 3; k++) for (int i = 0; i < 12; i++) { float x0 = m.x - 26 + i * 4.5f; DrawLineEx({x0, m.y - 8 + k * 8 + sinf(i * 0.9f) * 3}, {x0 + 4.5f, m.y - 8 + k * 8 + sinf((i + 1) * 0.9f) * 3}, 2, card == scuttle::C_CURRENT ? Color{40, 110, 130, 255} : ink); }
            break;
        case scuttle::C_PINCH: DrawCircleV(m, 12, ink); DrawCircleV({m.x + 6, m.y - 6}, 7, Color{236, 224, 194, 255}); break;
        case scuttle::C_SHELL: DrawCircleSector(m, 16, 180, 360, 12, ink); for (int k = -2; k <= 2; k++) DrawLineEx(m, {m.x + k * 7.0f, m.y - 15}, 1.5f, Color{236, 224, 194, 255}); break;
        case scuttle::C_GULL: DrawLineEx({m.x - 20, m.y}, {m.x, m.y + 6}, 3, ink); DrawLineEx({m.x, m.y + 6}, {m.x + 20, m.y}, 3, ink); break;
        case scuttle::C_ROCK: DrawEllipse((int)m.x, (int)m.y, 18, 11, Color{110, 104, 96, 255}); DrawEllipseLines((int)m.x, (int)m.y, 18, 11, ink); break;
        case scuttle::C_SIDESTEP: DrawTri({m.x - 20, m.y}, {m.x - 8, m.y - 8}, {m.x - 8, m.y + 8}, ink); DrawTri({m.x + 20, m.y}, {m.x + 8, m.y + 8}, {m.x + 8, m.y - 8}, ink); break;
        case scuttle::C_MOLT: DrawRing(m, 10, 14, 30, 330, 16, ink); break;
    }
    DrawWrapped(d.text, {b.x + 9, b.y + 80, b.width - 16, b.height - 84}, 13, Pal::Ink);
}

// ---------------------------------------------------------------- the reels (the cabinet's front page)
void DrawReels(Game& g) {
    float t = g.time;
    Vector2 c{SCREEN_W / 2.0f, 380};
    DrawCircleV({c.x + 8, c.y + 10}, 300, Fade(BLACK, 0.5f));
    DrawCircleV(c, 300, Pal::BrassDk);
    DrawRing(c, 270, 296, 0, 360, 90, Pal::Brass);
    for (int k = 0; k < 20; k++) { float a = k * PI / 10; DrawCircleV({c.x + cosf(a) * 283, c.y + sinf(a) * 283}, 6, Pal::BrassDk); }
    DrawCircleV(c, 268, SCREEN_BG);
    Glow(c, 360, Color{60, 220, 210, 50});
    TxtBold("THE DEEP ARCADE", c.x - MeasureTxt("THE DEEP ARCADE", 30, true) / 2.0f, c.y - 250, 30, SCREEN_INK);
    struct Reel { int game; const char* players; const char* length; const char* line; };
    const int NREELS = 5;
    const Reel reels[NREELS] = {
        {G_FLATS_DUEL, "2 players", "8-12 min", "Flats against a person: a best of three at the table."},
        {G_TRAWL, "1-6 co-op", "30-35 min", "Work a steam trawler by night: catch it, kill it, cook it, sell it, and meet the Owners' quota."},
        {G_SCUTTLE, "2-4 players", "5-10 min", "A fast crab-racing card game anyone can learn in one hand."},
        {G_FATHOMS, "2-6 players", "20-30 min", "The island strategy game: six factions of the deep."},
        {G_RED_TIDE, "1-4 co-op", "20-60 min", "Divers in living ecosystems: kill for scrip, and the blood in the water brings what eats everything."},
    };
    gDrum += (gSel - gDrum) * std::min(1.0f, GetFrameTime() * 8);
    float wheel = GetMouseWheelMove();
    if (wheel < 0 || IsKeyPressed(KEY_DOWN)) gSel = std::min(NREELS - 1, gSel + 1);
    if (wheel > 0 || IsKeyPressed(KEY_UP)) gSel = std::max(0, gSel - 1);
    for (int i = 0; i < NREELS; i++) {
        float off = (i - gDrum) * 92;
        if (fabsf(off) > 120) continue;
        float sc = 1 - fabsf(off) / 400;
        Rectangle r{c.x - 230 * sc, c.y - 40 + off - 34 * sc, 460 * sc, 68 * sc};
        bool on = i == gSel;
        DrawRectangleRounded(r, 0.25f, 8, on ? Color{30, 120, 118, 255} : Color{16, 60, 64, 255});
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, on ? Pal::Brass : Pal::BrassDk);
        DrawTextCenteredBold(Info(reels[i].game).name, c.x, r.y + 8 * sc, (int)(26 * sc), on ? Color{220, 255, 244, 255} : SCREEN_DIM);
        if (on) DrawTextCentered(TextFormat("%s   -   %s%s", reels[i].players, reels[i].length, Info(reels[i].game).built || reels[i].game == G_TRAWL || reels[i].game == G_RED_TIDE ? "" : "   -   coming aboard later"), c.x, r.y + 40, 15, Color{180, 230, 220, 255});
        if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gSel = i;
    }
    DrawWrapped(reels[gSel].line, {c.x - 200, c.y + 100, 400, 50}, 17, Color{200, 240, 232, 255});

    // the valve wheels: Host, Join, Browse (and a practice table against the arcade's own crabs)
    bool ready = Info(reels[gSel].game).built;
    int selGame = reels[gSel].game;
    const char* valves[3] = {"Host", "Join", "Browse"};
    for (int k = 0; k < 3; k++) {
        Vector2 v{c.x - 120 + k * 120.0f, c.y + 180};
        bool hover = CheckCollisionPointCircle(GetMousePosition(), v, 30);
        bool live = ready || k > 0;
        Color wheelC = !live ? Color{80, 40, 36, 255} : hover ? Color{220, 80, 60, 255} : Color{170, 50, 42, 255};
        DrawRing(v, 22, 28, 0, 360, 24, wheelC);
        float spin = t * (hover ? 2.0f : 0.3f);
        for (int s = 0; s < 3; s++) DrawLineEx(v, {v.x + cosf(spin + s * 2.09f) * 24, v.y + sinf(spin + s * 2.09f) * 24}, 3, wheelC);
        DrawTextCentered(valves[k], v.x, v.y + 34, 15, live ? SCREEN_INK : SCREEN_DIM);
        if (hover && live && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            PlayCue("ui.click");
            gError.clear();
            if (k == 0) {
                std::string err;
                if (gSess.Host(gProfile, selGame, &err)) gMode = MODE_ROOM;
                else gError = "Couldn't host: " + err;
            } else if (k == 1) { gMode = MODE_JOIN; gJoinFocus = true; }
            else gMode = MODE_BROWSE;
        }
    }
    // The Trawl and Red Tide play solo until their networking stages: a button launches them straight from the reel
    if (selGame == G_TRAWL) {   // two versions of the same game: from above, and through the hand's eyes
        // the crew: you and up to five bot hands, and how good they are (design doc "Bot crew")
        static int twSkill = 1;
        static const char* SKILLS[] = {"Green hands", "Able hands", "Old Hands"};
        auto picker = [&](float x, const char* text, int& v, int lo, int hi) {
            Rectangle l{x - 110, c.y + 52, 26, 26}, r{x + 84, c.y + 52, 26, 26};
            DrawTextCenteredBold(text, x, c.y + 54, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 13, l.y, 22, v > lo ? Pal::Brass : Pal::BrassDk);
            DrawTextCenteredBold(">", r.x + 13, r.y, 22, v < hi ? Pal::Brass : Pal::BrassDk);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l) && v > lo) { v--; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r) && v < hi) { v++; PlayCue("ui.click"); }
        };
        picker(c.x - 120, twCrew == 1 ? "Alone" : TextFormat("%d hands", twCrew), twCrew, 1, 6);
        if (twCrew > 1) picker(c.x + 120, SKILLS[twSkill], twSkill, 0, 2);
        if (Button({c.x - 226, c.y + 236, 220, 36}, "Sail: top-down", true, 15)) { gTrawlFp = false; StartTrawl(g, false, twCrew, twSkill); return; }
        if (Button({c.x + 6, c.y + 236, 220, 36}, "Sail: first person", true, 15)) { gTrawlFp = true; StartTrawl(g, true, twCrew, twSkill); return; }
        if (Button({c.x - 110, c.y + 278, 220, 30}, "Shakedown night (with Kess)", true, 14)) { StartTrawlShakedown(g, gTrawlFp); return; }
        if (Button({c.x + 120, c.y + 278, 150, 30}, TextFormat("Wardrobe (%d)", skins::Get(skins::TRAWL).crates), true, 14)) { gWardrobe = skins::TRAWL; return; }
        DrawTextCentered(TextFormat("Host or Join to sail with friends (view: %s, V switches aboard)", gTrawlFp ? "first person" : "top-down"), c.x, c.y + 280, 13, SCREEN_DIM);
        if (CheckCollisionPointRec(GetMousePosition(), {c.x - 200, c.y + 274, 400, 20}) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gTrawlFp = !gTrawlFp;
    }
    if (selGame == G_RED_TIDE) {
        static const char* RT_MAPS[] = {"ship", "cave", "reef", "atlantis", "void"};
        static const char* RT_TITLES[] = {"The Sunken Ship", "The Underwater Cave", "The Coral Reef", "Atlantis", "Approaching the Void"};
        static int rtMap = 0;
        const int RT_N = (int)(sizeof(RT_MAPS) / sizeof(RT_MAPS[0]));
        Rectangle lt{c.x - 190, c.y + 52, 30, 26}, rtR{c.x + 160, c.y + 52, 30, 26};
        DrawTextCenteredBold(RT_TITLES[rtMap], c.x, c.y + 54, 20, Color{230, 200, 150, 255});
        DrawTextCenteredBold("<", lt.x + 15, lt.y, 22, Pal::Brass);
        DrawTextCenteredBold(">", rtR.x + 15, rtR.y, 22, Pal::Brass);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), lt)) rtMap = (rtMap + RT_N - 1) % RT_N;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), rtR)) rtMap = (rtMap + 1) % RT_N;
        if (IsKeyPressed(KEY_LEFT)) rtMap = (rtMap + RT_N - 1) % RT_N;
        if (IsKeyPressed(KEY_RIGHT)) rtMap = (rtMap + 1) % RT_N;
        static const char* PAGES[] = {"Dossier", "Records", "How to play", "Charm pouch", "Locker room"};
        for (int k = 0; k < 5; k++) if (Button({40, 250 + k * 58.0f, 200, 44}, PAGES[k], true, 18)) { OpenRedTidePage(g, k + 1); return; }
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Dive (solo)", true, 15)) { StartRedTide(g, RT_MAPS[rtMap]); return; }
    }
    if (ready && Button({c.x - 110, c.y + 236, 220, 36}, "Practice with AI crabs", true, 15)) {
        std::string err;
        if (gSess.Host(gProfile, selGame, &err, 47790, net::MakeMemoryTransport(), false)) { gSess.AddAI(); gSess.AddAI(); gSess.AddAI(); gMode = MODE_ROOM; }
        else gError = err;
    }
    // the name plate
    Rectangle np{40, 640, 300, 40};
    TxtShadow("Your name at the table", np.x, np.y - 24, 16, Pal::Paper);
    std::string before = gProfile.name;
    TextField(np, gProfile.name, gNameFocus, 20, "Diver");
    if (!gNameFocus && gProfile.name != before) SaveProfile();
    static bool wasFocus = false;
    if (wasFocus && !gNameFocus) { if (gProfile.name.empty()) gProfile.name = "Diver"; SaveProfile(); }
    wasFocus = gNameFocus;
    if (!net::Available()) DrawTextCentered("This build has no network library (tools/build_gns.ps1): only practice tables work.", c.x, 700, 15, Pal::Coral);
}

// ---------------------------------------------------------------- Join / Browse
void EnsureBrowsing() { if (!gBrowsing) gBrowsing = gBrowse.Start(); if (gBrowsing) gBrowse.Poll(GetTime()); }
void StopBrowsing() { if (gBrowsing) { gBrowse.Stop(); gBrowsing = false; gBrowse.games.clear(); } }

void TryJoin(const std::string& addr) {
    std::string err;
    StopBrowsing();
    if (gSess.Join(gProfile, addr, &err)) gMode = MODE_ROOM;
    else gError = "Couldn't join: " + err;
}

void DrawJoin() {
    EnsureBrowsing();
    Rectangle p{340, 170, 600, 330};
    DrawScreenPanel(p);
    DrawTextCenteredBold("JOIN A TABLE", p.x + p.width / 2, p.y + 22, 28, SCREEN_INK);
    DrawWrapped("Type the host's 6-letter code (same network), or their address for a friend elsewhere (for example 203.0.113.7). Over the internet the host must forward UDP port 47778 until Depth is on Steam.",
                {p.x + 40, p.y + 70, p.width - 80, 80}, 16, SCREEN_DIM);
    bool enter = TextField({p.x + 60, p.y + 170, p.width - 120, 50}, gJoinText, gJoinFocus, 40, "code or address");
    bool go = Button({p.x + p.width / 2 - 90, p.y + 245, 180, 46}, "Join", !gJoinText.empty(), 20) || (enter && !gJoinText.empty());
    if (go) {
        std::string text = gJoinText;
        std::string upper = text; for (char& ch : upper) ch = (char)toupper(ch);
        bool isCode = upper.size() == 6 && upper.find('.') == std::string::npos && upper.find(':') == std::string::npos;
        if (isCode) {
            auto it = std::find_if(gBrowse.games.begin(), gBrowse.games.end(), [&](const net::LanGame& lg) { return lg.code == upper; });
            if (it != gBrowse.games.end()) TryJoin(it->addr + ":" + std::to_string(it->port));
            else gError = "No table with code " + upper + " can be heard on this network. For a friend elsewhere, type their address.";
        } else TryJoin(text);
    }
}

void DrawBrowse() {
    EnsureBrowsing();
    Rectangle p{240, 130, 800, 480};
    DrawScreenPanel(p);
    DrawTextCenteredBold("TABLES ON THIS NETWORK", p.x + p.width / 2, p.y + 20, 28, SCREEN_INK);
    if (!gBrowsing) DrawTextCentered("Couldn't listen for tables (UDP 47777 is blocked?).", p.x + p.width / 2, p.y + 80, 17, Pal::Coral);
    else if (gBrowse.games.empty()) {
        DrawTextCentered("Listening for tables...", p.x + p.width / 2, p.y + 100, 18, SCREEN_DIM);
        for (int k = 0; k < 3; k++) { float r = fmodf((float)GetTime() * 60 + k * 60, 180); DrawRing({p.x + p.width / 2, p.y + 250}, r, r + 2, 0, 360, 48, Fade(SCREEN_INK, 1 - r / 180)); }
    }
    float y = p.y + 70;
    for (const net::LanGame& lg : gBrowse.games) {
        Rectangle row{p.x + 30, y, p.width - 60, 58};
        DrawRectangleRounded(row, 0.2f, 6, Color{16, 76, 80, 255});
        bool same = lg.build == BuildId();
        TxtBold(lg.name + "'s table", row.x + 16, row.y + 7, 20, SCREEN_INK);
        Txt(TextFormat("%s  -  %d/%d  -  code %s%s%s", lg.game.c_str(), lg.players, lg.maxPlayers, lg.code.c_str(), lg.inProgress ? "  -  playing" : "", same ? "" : "  -  a different version of Depth"),
            row.x + 16, row.y + 33, 15, same ? SCREEN_DIM : Pal::Coral);
        bool can = same && !lg.inProgress && lg.players < lg.maxPlayers;
        if (Button({row.x + row.width - 130, row.y + 9, 116, 40}, "Join", can, 18)) TryJoin(lg.addr + ":" + std::to_string(lg.port));
        y += 66;
        if (y > p.y + p.height - 70) break;
    }
    if (Button({p.x + p.width / 2 - 110, p.y + p.height - 56, 220, 40}, "Join by code or address", true, 16)) { gMode = MODE_JOIN; gJoinFocus = true; }
}

// ---------------------------------------------------------------- the lobby
void DrawChat(Rectangle r) {
    DrawScreenPanel(r);
    TxtBold("TABLE TALK", r.x + 16, r.y + 12, 18, SCREEN_INK);
    float y = r.y + r.height - 76;
    for (int i = (int)gSess.chat.size() - 1; i >= 0 && y > r.y + 40; i--, y -= 22) Txt(gSess.chat[i], r.x + 16, y, 15, SCREEN_DIM);
    if (TextField({r.x + 12, r.y + r.height - 48, r.width - 24, 36}, gChatText, gChatFocus, 120, "say something (Enter)")) { gSess.Chat(gChatText); gChatText.clear(); }
}

void DrawLobby() {
    Rectangle p{60, 110, 700, 520};
    DrawScreenPanel(p);
    bool host = gSess.role == R_HOST;
    DrawTextCenteredBold(TextFormat("%s  -  %s's table", Info(gSess.game).name, gSess.hostName.c_str()), p.x + p.width / 2, p.y + 16, 24, SCREEN_INK);
    TxtBold("CODE", p.x + 30, p.y + 58, 16, SCREEN_DIM);
    TxtBold(gSess.code, p.x + 84, p.y + 50, 30, Pal::Brass);
    if (host) {
        static std::string ips; static double ipsAt = -10;
        if (GetTime() - ipsAt > 5) { ipsAt = GetTime(); ips.clear(); for (auto& ip : net::LocalIPv4()) ips += (ips.empty() ? "" : ",  ") + ip; }
        Txt("Your address: " + (ips.empty() ? std::string("?") : ips), p.x + 250, p.y + 52, 15, SCREEN_DIM);
        DrawWrapped("On this network friends Browse or type the code. Elsewhere they need your public address (forward UDP 47778).", {p.x + 250, p.y + 70, p.width - 280, 34}, 13, Fade(SCREEN_DIM, 0.8f));
    }
    float y = p.y + 110;
    int maxP = Info(gSess.game).maxPlayers;
    for (int i = 0, shown = 0; i < MAX_PLAYERS && shown < maxP; i++) {
        const SeatInfo& s = gSess.seats[i];
        if (!s.used) continue;
        shown++;
        Rectangle row{p.x + 30, y, p.width - 60, 52};
        DrawRectangleRounded(row, 0.2f, 6, i == gSess.mySeat ? Color{24, 96, 96, 255} : Color{16, 70, 74, 255});
        DrawCrab({row.x + 32, row.y + 30}, CRAB[std::min(shown - 1, 3)], (float)GetTime(), 0, false, 0.8f);
        TxtBold(s.name + (s.host ? "  (host)" : s.ai ? "  (AI)" : ""), row.x + 66, row.y + 8, 20, SCREEN_INK);
        Txt(s.lost ? "lost connection" : s.ready ? "ready" : "not ready", row.x + 66, row.y + 30, 15, s.lost ? Pal::Coral : s.ready ? Pal::Good : SCREEN_DIM);
        if (!s.ai && !s.host && s.ping) Txt(TextFormat("%d ms", s.ping), row.x + row.width - 220, row.y + 17, 15, SCREEN_DIM);
        if (host && i != 0 && Button({row.x + row.width - 120, row.y + 8, 108, 36}, s.ai ? "Remove" : "Give away", true, 15)) gSess.RemoveSeat(i);
        y += 60;
    }
    int used = 0; for (auto& s : gSess.seats) used += s.used;
    for (int k = used; k < maxP; k++) {
        Rectangle row{p.x + 30, y, p.width - 60, 52};
        DrawRectangleRoundedLinesEx(row, 0.2f, 6, 1.5f, Fade(SCREEN_DIM, 0.4f));
        Txt("an empty seat", row.x + 66, row.y + 17, 16, Fade(SCREEN_DIM, 0.6f));
        if (host && k == used && Button({row.x + row.width - 120, row.y + 8, 108, 36}, "Add AI", true, 15)) gSess.AddAI();
        y += 60;
    }
    if (host) {
        std::string why;
        bool can = gSess.CanLaunch(&why);
        if (Button({p.x + p.width - 250, p.y + p.height - 66, 220, 50}, "Start the race", can, 20)) { std::string w2; gSess.Launch(&w2); }
        if (!can) Txt(why, p.x + 30, p.y + p.height - 50, 15, SCREEN_DIM);
    } else if (gSess.mySeat >= 0) {
        bool ready = gSess.seats[gSess.mySeat].ready;
        if (Button({p.x + p.width - 250, p.y + p.height - 66, 220, 50}, ready ? "Not ready" : "Ready", true, 20)) gSess.SetReady(!ready);
        Txt("The host starts once everyone is ready.", p.x + 30, p.y + p.height - 50, 15, SCREEN_DIM);
    }
    DrawChat({800, 110, 420, 520});
}

// ---------------------------------------------------------------- the Scuttle table
scuttle::State gView;          // my view of the match, decoded from the session's snapshot
int gViewVer = -1;
bool gShowRules = false;
struct Hop { float from = 0, to = 0, t = 1, dur = 0; int steps = 0, stepped = 0; };
Hop gHop[scuttle::MAX_SEATS];
struct PlayFx { int card = -1, player = -1, target = -1; uint8_t shelled = 0, hit = 0; float t = 99; };
PlayFx gPlay;                  // the last card resolved, animating
struct Floater { std::string text; Vector2 p; float t; Color c; };
std::vector<Floater> gFloat;
float gSplashT = 99;           // a heat or the match just ended
int gLastTimerSec = 99;

// the table's geometry
const Rectangle FELT{40, 90, 1200, 390};
const float X0 = 262, CELL = 94;
const Vector2 SLOT{1150, 215};  // where the last card played lies
float LaneH(int n) { return std::min(86.0f, (FELT.height - 60) / std::max(2, n)); }
float LaneY(int k, int n) { return FELT.y + 34 + (FELT.height - 60 - LaneH(n) * n) / 2 + LaneH(n) * k; }

void Send(scuttle::Action a) { Writer w; scuttle::WriteAction(a, w); gSess.Act(w); }
std::string OwnerName(int crab) {
    int s = gSess.SeatOfPlayer(crab);
    return s >= 0 && gSess.seats[s].used ? gSess.seats[s].name : std::string(crab >= 0 && crab < scuttle::MAX_SEATS ? CRAB_NAME[crab] : "?");
}
float HopFrac(const Hop& h) { return h.dur > 0 ? std::min(1.0f, h.t / h.dur) : 1.0f; }
float CrabSpace(int k) { const Hop& h = gHop[k]; return h.from + (h.to - h.from) * HopFrac(h); }
float CrabLift(int k) {   // one little arc per space crossed
    const Hop& h = gHop[k];
    if (HopFrac(h) >= 1) return 0;
    return fabsf(sinf(HopFrac(h) * std::max(1, h.steps) * PI)) * 12;
}
Vector2 CrabAt(int k) { int n = gView.nSeats; return {X0 + CrabSpace(k) * CELL, LaneY(k, n) + LaneH(n) * 0.55f - CrabLift(k)}; }
void Float(const std::string& text, Vector2 p, Color c) { gFloat.push_back({text, p, 0, c}); }

// a new snapshot: work out what happened since the last one and set the table moving
void TakeView() {
    if (gViewVer == gSess.stateVersion) return;
    gViewVer = gSess.stateVersion;
    if (gSess.Snapshot().empty()) return;
    scuttle::State n;
    Reader r(gSess.Snapshot());
    if (!scuttle::Deserialize(n, r)) return;
    const scuttle::State o = gView;
    bool fresh = o.nSeats != n.nSeats || n.plays < o.plays || n.round < o.round;   // first sight or a new match: no animation
    int me = gSess.MyPlayer();
    gView = n;
    gPickCard = -1;
    gLastTimerSec = 99;
    if (fresh) {
        for (int k = 0; k < scuttle::MAX_SEATS; k++) gHop[k] = {(float)n.seats[k].pos, (float)n.seats[k].pos, 1, 0, 0, 0};
        gPlay = PlayFx{}; gSplashT = 99; gFloat.clear();
        return;
    }
    uint8_t hit = 0;
    for (int k = 0; k < n.nSeats; k++) if (n.seats[k].pos != o.seats[k].pos) {
        int d = abs((int)n.seats[k].pos - (int)o.seats[k].pos);
        if (n.seats[k].pos < o.seats[k].pos && n.round == o.round) hit |= 1 << k;
        gHop[k] = {CrabSpace(k), (float)n.seats[k].pos, 0, 0.2f * std::max(1, d) + 0.05f, std::max(1, d), 0};
    }
    if (n.plays > o.plays && n.lastCard >= 0) {
        gPlay = {n.lastCard, n.lastPlayer, n.lastTarget, n.lastShelled, hit, 0};
        PlayCue("arc.card");
        static const char* CUE[scuttle::C_COUNT] = {nullptr, nullptr, nullptr, nullptr, "arc.wave", "arc.pinch", nullptr, "arc.current", "arc.gull", "arc.rock", "arc.molt", nullptr};
        if (CUE[n.lastCard]) PlayCue(CUE[n.lastCard]);
        for (int k = 0; k < n.nSeats; k++) {
            Vector2 p = {X0 + n.seats[k].pos * CELL, LaneY(k, n.nSeats) + 4};
            if ((n.lastShelled >> k) & 1) { Float("Ducked!", p, Pal::Teal); PlayCue("arc.shell"); }
            else if ((hit >> k) & 1) Float(TextFormat("-%d", (int)o.seats[k].pos - (int)n.seats[k].pos), p, Pal::Coral);
        }
        if (n.lastCard == scuttle::C_PINCH && n.lastTarget >= 0 && !((n.lastShelled >> n.lastTarget) & 1)) Float("Pinched!", {X0 + n.seats[n.lastTarget].pos * CELL, LaneY(n.lastTarget, n.nSeats) + 4}, Pal::Stress);
    } else if (n.phase == scuttle::PH_RESPONSE && o.phase != scuttle::PH_RESPONSE) PlayCue("arc.card");   // a card waits on a Shell
    for (int k = 0; k < n.nSeats; k++) if (n.seats[k].skipMove && !o.seats[k].skipMove && n.rock != o.rock && n.lastCard != scuttle::C_PINCH)
        Float("Stuck on the rock!", {X0 + n.seats[k].pos * CELL, LaneY(k, n.nSeats) + 4}, Pal::Paper);
    int ob = (int)o.bets.size(), nb = (int)n.bets.size();
    for (int k = 0; k < n.nSeats; k++) { ob += o.betsHidden[k]; nb += n.betsHidden[k]; }
    if (nb > ob && n.phase < scuttle::PH_ROUND_OVER) PlayCue("arc.pearl");
    if (me >= 0 && n.seats[me].hand.size() > o.seats[me].hand.size() && n.round == o.round) PlayCue("arc.deal");
    if (n.phase == scuttle::PH_ROUND_OVER && o.phase != scuttle::PH_ROUND_OVER) { gSplashT = 0; PlayCue(n.roundWinner == me ? "arc.heat" : "arc.lose"); }
    if (n.phase == scuttle::PH_MATCH_OVER && o.phase != scuttle::PH_MATCH_OVER) { gSplashT = 0; PlayCue(n.matchWinner == me ? "arc.match" : "arc.lose"); }
    if (n.round > o.round) gSplashT = 99;
    if (me >= 0 && scuttle::Actor(n) == me && scuttle::Actor(o) != me && !gSess.paused) PlayCue("arc.turn");
}

// the per-frame clock: hops, the card's flight, floaters, the timer's last seconds
void TickTable(float dt) {
    for (int k = 0; k < gView.nSeats; k++) {
        Hop& h = gHop[k];
        if (HopFrac(h) >= 1) continue;
        h.t += dt;
        int now = (int)(HopFrac(h) * h.steps + 0.5f);
        if (now > h.stepped) { h.stepped = now; PlayCue("arc.scuttle", 0.8f, (CrabSpace(k) - 4) / 8.0f); }
    }
    gPlay.t += dt; gSplashT += dt;
    for (auto& f : gFloat) f.t += dt;
    gFloat.erase(std::remove_if(gFloat.begin(), gFloat.end(), [](const Floater& f) { return f.t > 1.6f; }), gFloat.end());
    int actor = scuttle::Actor(gView);
    if (actor >= 0 && !gSess.paused && gView.timer < 1e8f) {
        gView.timer = std::max(0.0f, gView.timer - dt);   // (a display countdown; the host's clock rules)
        int sec = (int)ceilf(gView.timer);
        if (actor == gSess.MyPlayer() && sec <= 5 && sec != gLastTimerSec && sec > 0) PlayCue("arc.tick");
        gLastTimerSec = sec;
    }
}

void DrawCardBack(Rectangle r) {
    DrawRectangleRounded(r, 0.15f, 4, Color{40, 70, 76, 255});
    DrawRectangleRoundedLinesEx(r, 0.15f, 4, 1.2f, Pal::BrassDk);
    DrawCircleLinesV({r.x + r.width / 2, r.y + r.height / 2}, std::min(r.width, r.height) * 0.28f, Fade(Pal::Brass, 0.7f));
}

// the card's effect on the felt, while it plays out
void DrawPlayFx(float t) {
    const scuttle::State& s = gView;
    int n = s.nSeats;
    const PlayFx& p = gPlay;
    if (p.t > 2.0f || p.card < 0) return;
    float u = p.t;
    switch (p.card) {
        case scuttle::C_WAVE: {   // a wall of water sweeping the pool, sparing the player's lane
            float x = FELT.x + (u / 0.9f) * FELT.width;
            if (u < 0.9f) for (int k = 0; k < n; k++) if (k != p.player) {
                float y = LaneY(k, n);
                DrawRectangleGradientH((int)(x - 120), (int)y, 120, (int)LaneH(n), Fade(Pal::Teal, 0), Fade(Color{190, 240, 240, 255}, 0.45f));
                for (int i = 0; i < 6; i++) DrawCircleV({x - 6, y + i * LaneH(n) / 6 + sinf(t * 9 + i) * 3}, 5, Fade(WHITE, 0.6f));
            }
        } break;
        case scuttle::C_CURRENT:   // streaks running toward the tide line
            if (u < 1.2f) for (int k = 0; k < n; k++) for (int i = 0; i < 5; i++) {
                float x = X0 + fmodf(u * 700 + i * 190 + k * 60, 8 * CELL), y = LaneY(k, n) + 10 + i * (LaneH(n) - 20) / 5;
                DrawLineEx({x, y}, {x + 50, y}, 2, Fade(Color{150, 220, 255, 255}, 0.55f * (1 - u / 1.2f)));
            }
            break;
        case scuttle::C_GULL: {   // a gull stoops from the sky onto the leader's lane, then climbs away
            if (u > 1.3f) break;
            int lane = -1; for (int k = 0; k < n; k++) if ((p.hit >> k) & 1) { lane = k; break; }
            float tx = lane >= 0 ? X0 + s.seats[lane].pos * CELL + 2 * CELL : X0 + 6 * CELL, ty = lane >= 0 ? LaneY(lane, n) + 10 : FELT.y + 40;
            float f = u / 1.3f;
            Vector2 g{FELT.x + FELT.width + 40 - (FELT.x + FELT.width + 40 - (tx - 200)) * f, f < 0.5f ? FELT.y - 60 + (ty - FELT.y + 60) * (f / 0.5f) : ty - (ty - FELT.y + 80) * ((f - 0.5f) / 0.5f)};
            float flap = sinf(t * 22) * 10;
            DrawEllipse((int)g.x, (int)g.y, 14, 6, Color{236, 236, 230, 255});
            DrawLineEx(g, {g.x - 22, g.y - 10 + flap}, 5, Color{210, 210, 204, 255});
            DrawLineEx(g, {g.x + 22, g.y - 10 + flap}, 5, Color{210, 210, 204, 255});
            DrawTri({g.x - 16, g.y - 2}, {g.x - 24, g.y}, {g.x - 16, g.y + 2}, Color{240, 170, 40, 255});
        } break;
        case scuttle::C_ROCK:     // the rock drops onto its space
            if (u < 0.5f && p.target > 0) {
                float y = FELT.y - 40 + (LaneY(0, n) + LaneH(n) * 0.7f - FELT.y + 40) * (u / 0.5f);
                DrawEllipse((int)(X0 + p.target * CELL), (int)y, 16, 9, Color{110, 104, 96, 255});
            } else if (u < 0.8f && p.target > 0) for (int k = 0; k < n; k++) DrawRing({X0 + p.target * CELL, LaneY(k, n) + LaneH(n) * 0.72f}, (u - 0.5f) * 90, (u - 0.5f) * 90 + 2, 0, 360, 24, Fade(Pal::Paper, 0.7f - (u - 0.5f) * 2));
            break;
        case scuttle::C_PINCH:    // a great claw snaps at the target
            if (u < 0.7f && p.target >= 0 && !((p.shelled >> p.target) & 1)) {
                Vector2 c = CrabAt(p.target); c.y -= 6;
                float open = u < 0.35f ? 40 * (1 - u / 0.35f) + 6 : 6;
                DrawCircleSector({c.x, c.y - open * 0.5f}, 26, 200, 340, 12, Fade(Color{200, 70, 60, 255}, 0.8f));
                DrawCircleSector({c.x, c.y + open * 0.5f}, 26, 20, 160, 12, Fade(Color{200, 70, 60, 255}, 0.8f));
            }
            break;
        case scuttle::C_SIDESTEP:
            if (u < 0.8f && p.player >= 0 && p.target >= 0) for (int k : {p.player, p.target}) { Vector2 c = CrabAt(k); DrawTextCenteredBold(k == p.player ? ">>" : "<<", c.x, c.y - 44, 18, Fade(Pal::Brass, 1 - u / 0.8f)); }
            break;
        case scuttle::C_MOLT:
            if (u < 0.9f && p.player >= 0) for (int i = 0; i < 10; i++) { float a = i * 0.63f + u * 3; DrawCircleV({FELT.x + 120 + cosf(a) * u * 90, LaneY(p.player, n) + LaneH(n) / 2 + sinf(a) * u * 40}, 3, Fade(Pal::Brass, 1 - u / 0.9f)); }
            break;
        default: break;
    }
    // Shells: a shell dome over each crab that ducked
    for (int k = 0; k < n; k++) if (((p.shelled >> k) & 1) && u < 1.4f) {
        Vector2 c = CrabAt(k);
        float a = std::min(1.0f, (1.4f - u) * 2);
        DrawCircleSector({c.x, c.y + 8}, 30, 180, 360, 16, Fade(Color{236, 214, 170, 255}, 0.85f * a));
        for (int i = -2; i <= 2; i++) DrawLineEx({c.x, c.y + 8}, {c.x + i * 12.0f, c.y - 18}, 2, Fade(Color{150, 110, 70, 255}, a));
    }
}

void DrawRules() {
    Rectangle p{190, 44, 900, 640};
    Panel(p);
    DrawTextCenteredBold("SCUTTLE: HOW TO PLAY", p.x + p.width / 2, p.y + 18, 26, Pal::Ink);
    DrawWrapped("Race your crab from the start to the tide line (space 8). The first crab there wins the heat; win two heats to take the match.\n\n"
                "On your turn play one card from your hand of five. Then you may lay a pearl face down on any crab (three a heat): +3 if that crab wins the heat, +1 if it comes second, -1 otherwise. A heat win is worth +2. Pearls are your score.\n\n"
                "Some cards are aimed at other crabs (Sidestep, Pinch, Wave, Gull). If one is aimed at you and you hold a Shell, you have a few seconds to duck into it and cancel it for your crab.\n\n"
                "A Pinched crab's next move card does nothing. A crab that lands on the rock is stuck for a turn. Every turn has a 20-second timer; when it runs out the tide moves on for you.",
                {p.x + 30, p.y + 60, p.width - 60, 290}, 16, Pal::Ink);
    TxtBold("THE DECK (60 cards)", p.x + 30, p.y + 346, 16, Pal::Ink);
    float y = p.y + 372;
    for (int c = 0; c < scuttle::C_COUNT; c++) {
        float x = p.x + 30 + (c % 2) * 425; if (c % 2 == 0 && c) y += 36;
        TxtBold(TextFormat("%s x%d", scuttle::Card(c).name, scuttle::Card(c).copies), x, y, 14, Pal::Ink);
        DrawWrapped(scuttle::Card(c).text, {x + 118, y, 290, 34}, 13, Color{70, 50, 34, 255});
    }
    if (Button({p.x + p.width / 2 - 80, p.y + p.height - 20, 160, 40}, "Got it", true, 17) || IsKeyPressed(KEY_ESCAPE)) gShowRules = false;
}

void DrawTableTalk(Rectangle r) {
    DrawRectangleRounded(r, 0.06f, 6, Fade(Color{6, 26, 30, 255}, 0.92f));
    DrawRectangleRoundedLinesEx(r, 0.06f, 6, 2, Pal::BrassDk);
    TxtBold("TABLE TALK", r.x + 12, r.y + 9, 15, SCREEN_INK);
    const char* EMOTES[4] = {"Ahoy!", "Nice move", "Blast!", "Good game"};
    for (int i = 0; i < 4; i++) if (Button({r.x + 120 + i * 84.0f, r.y + 5, 80, 26}, EMOTES[i], true, 12)) gSess.Chat(EMOTES[i]);
    float y = r.y + r.height - 70;
    for (int i = (int)gSess.chat.size() - 1; i >= 0 && y > r.y + 36; i--, y -= 20) {
        std::string line = gSess.chat[i];
        while (line.size() > 4 && MeasureTxt(line, 14) > r.width - 24) line = line.substr(0, line.size() - 4) + "...";
        Txt(line, r.x + 12, y, 14, SCREEN_DIM);
    }
    if (TextField({r.x + 10, r.y + r.height - 42, r.width - 20, 34}, gChatText, gChatFocus, 120, "say something (Enter)")) { gSess.Chat(gChatText); gChatText.clear(); }
}

void DrawTable(Game& g) {
    TakeView();
    const scuttle::State& s = gView;
    float t = g.time;
    TickTable(GetFrameTime());
    int me = gSess.MyPlayer();
    int n = std::max(2, s.nSeats);
    int actor = scuttle::Actor(s);
    bool modal = gShowRules;
    Vector2 mouse = modal ? Vector2{-1000, -1000} : GetMousePosition();
    bool click = !modal && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // the felt and its tide pool: wet sand darkening toward the tide line, which foams
    DrawRectangleRounded({FELT.x + 6, FELT.y + 10, FELT.width, FELT.height}, 0.06f, 8, Fade(BLACK, 0.5f));
    DrawRectangleRounded({FELT.x - 14, FELT.y - 14, FELT.width + 28, FELT.height + 28}, 0.07f, 8, Color{62, 38, 24, 255});
    DrawRectangleRoundedLinesEx({FELT.x - 14, FELT.y - 14, FELT.width + 28, FELT.height + 28}, 0.07f, 8, 3, Pal::BrassDk);
    DrawRectangleRounded(FELT, 0.06f, 8, Color{22, 84, 70, 255});
    for (int k = 0; k < 40; k++) DrawCircleV({FELT.x + fmodf(k * 211.7f, FELT.width), FELT.y + fmodf(k * 97.3f, FELT.height)}, 1.5f, Fade(BLACK, 0.12f));
    float laneTop = LaneY(0, n) - 8, laneBot = LaneY(n - 1, n) + LaneH(n) + 8;
    for (int sp = 0; sp <= scuttle::TRACK; sp++) {
        float x = X0 + sp * CELL;
        float wet = sp / (float)scuttle::TRACK;
        Color sand = sp == scuttle::TRACK ? Color{80, 150, 150, 255} : sp == 0 ? Color{120, 104, 76, 255} : ColorLerp(Color{176, 150, 104, 255}, Color{110, 110, 92, 255}, wet);
        DrawRectangleRounded({x - CELL / 2 + 4, laneTop, CELL - 8, laneBot - laneTop}, 0.12f, 6, Fade(sand, 0.55f));
        DrawTextCentered(sp == 0 ? "start" : sp == scuttle::TRACK ? "tide line" : std::to_string(sp), x, FELT.y + 10, 15, Pal::Paper);
    }
    for (int k = 0; k < 34; k++) { float yy = laneTop + k * (laneBot - laneTop) / 33; DrawCircleV({X0 + scuttle::TRACK * CELL - CELL / 2 + 4 + sinf(t * 2 + k) * 4, yy}, 4 + sinf(t * 3 + k * 1.7f), Fade(WHITE, 0.45f)); }
    if (s.rock > 0) for (int k = 0; k < n && !(gPlay.card == scuttle::C_ROCK && gPlay.t < 0.5f); k++) {
        Vector2 rp{X0 + s.rock * CELL, LaneY(k, n) + LaneH(n) * 0.74f};
        DrawEllipse((int)rp.x, (int)rp.y + 3, 17, 7, Fade(BLACK, 0.3f)); DrawEllipse((int)rp.x, (int)rp.y, 16, 9, Color{110, 104, 96, 255}); DrawEllipse((int)rp.x - 4, (int)rp.y - 3, 6, 3, Fade(WHITE, 0.2f));
    }

    // a card waiting for its target: highlight the crabs or spaces it can go to
    int pickCard = gPickCard >= 0 && me >= 0 && gPickCard < (int)s.seats[me].hand.size() ? s.seats[me].hand[gPickCard] : -1;
    std::vector<int> targets = pickCard >= 0 ? scuttle::Targets(s, me, pickCard) : std::vector<int>{};
    auto playAt = [&](int target) { scuttle::Action a; a.kind = scuttle::Action::PLAY; a.handIdx = (uint8_t)gPickCard; a.target = (int8_t)target; Send(a); gPickCard = -1; };

    for (int k = 0; k < s.nSeats; k++) {
        const scuttle::Seat& p = s.seats[k];
        float ly = LaneY(k, n), lh = LaneH(n);
        if (actor == k) DrawRectangleRounded({FELT.x + 10, ly + 2, FELT.width - 20, lh - 4}, 0.2f, 6, Fade(WHITE, 0.06f + 0.03f * sinf(t * 4)));
        bool mine = k == me;
        TxtBold(OwnerName(k), FELT.x + 22, ly + lh / 2 - 26, 18, mine ? Pal::Brass : Pal::Paper);
        Txt(TextFormat("%s crab  -  %d pearl%s", CRAB_NAME[k], p.points, abs(p.points) == 1 ? "" : "s"), FELT.x + 22, ly + lh / 2 - 5, 13, Fade(Pal::Paper, 0.8f));
        for (int w = 0; w < s.winsNeeded; w++) DrawCircleV({FELT.x + 28 + w * 14.0f, ly + lh / 2 + 20}, 5, w < p.roundWins ? Pal::Brass : Fade(Pal::Paper, 0.25f));
        for (int b = 0; b < s.betsHidden[k]; b++) DrawCircleV({FELT.x + 70 + b * 12.0f, ly + lh / 2 + 20}, 5, Color{230, 226, 240, 255});   // pearls face down
        if (!mine) for (int c = 0; c < p.handCount; c++) DrawCardBack({FELT.x + 104 + c * 8.0f, ly + lh / 2 + 12, 12, 17});
        Vector2 cp = CrabAt(k);
        bool moving = HopFrac(gHop[k]) < 1;
        DrawCrab(cp, CRAB[k], t, moving ? 1.0f : 0.0f, p.skipMove, std::min(1.0f, lh / 80));
        if (p.skipMove && CheckCollisionPointCircle(mouse, cp, 26)) DrawTextCentered("held: its next move card does nothing", cp.x, cp.y - 44, 13, Pal::Paper);
        bool isTarget = (pickCard == scuttle::C_SIDESTEP || pickCard == scuttle::C_PINCH) && std::find(targets.begin(), targets.end(), k) != targets.end();
        if (isTarget) {
            Rectangle lr{X0 - CELL / 2, ly, CELL * (scuttle::TRACK + 1), lh};
            bool hov = CheckCollisionPointRec(mouse, lr);
            DrawRectangleRoundedLinesEx(lr, 0.2f, 6, hov ? 4.0f : 2.0f, Fade(Pal::Coral, 0.6f + 0.4f * sinf(t * 6)));
            if (hov && click) playAt(k);
        }
    }
    for (const scuttle::Bet& b : s.bets) if (b.crab < n) {   // the pearls on each crab (yours, or all of them once the heat is over)
        int idx = 0; for (const scuttle::Bet& o : s.bets) { if (&o == &b) break; if (o.crab == b.crab) idx++; }
        DrawCircleV({X0 + scuttle::TRACK * CELL + 30 + idx * 13.0f, LaneY(b.crab, n) + LaneH(n) / 2}, 6, CRAB[b.owner]);
        DrawCircleLinesV({X0 + scuttle::TRACK * CELL + 30 + idx * 13.0f, LaneY(b.crab, n) + LaneH(n) / 2}, 6, Fade(WHITE, 0.7f));
    }
    if (pickCard == scuttle::C_ROCK) for (int sp : targets) {
        Rectangle cr{X0 + sp * CELL - CELL / 2 + 4, laneTop, CELL - 8, laneBot - laneTop};
        bool hov = CheckCollisionPointRec(mouse, cr);
        DrawRectangleRoundedLinesEx(cr, 0.1f, 6, hov ? 4.0f : 2.0f, Fade(Pal::Coral, 0.5f + 0.4f * sinf(t * 6)));
        if (hov && click) playAt(sp);
    }
    DrawPlayFx(t);
    for (const Floater& f : gFloat) DrawTextCenteredBold(f.text, f.p.x, f.p.y - f.t * 30, 18, Fade(f.c, std::min(1.0f, (1.6f - f.t) * 2)));

    // the last card played, and the card aimed at someone right now, lie by the tide line
    int shownCard = s.phase == scuttle::PH_RESPONSE ? s.pendCard : gPlay.card;
    int shownBy = s.phase == scuttle::PH_RESPONSE ? s.pendPlayer : gPlay.player;
    if (shownCard >= 0) {
        float f = s.phase == scuttle::PH_RESPONSE ? 1.0f : std::min(1.0f, gPlay.t / 0.35f);
        float e = 1 - (1 - f) * (1 - f);
        Vector2 from{FELT.x + 150, shownBy >= 0 ? LaneY(shownBy, n) + LaneH(n) / 2 : SLOT.y}, to{SLOT.x, SLOT.y};
        Vector2 c{from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e - sinf(e * PI) * 60};
        float sc = 0.4f + 0.6f * e;
        DrawCard({c.x - 58 * sc, c.y - 80 * sc, 116 * sc, 160 * sc}, shownCard, false, false, true);
        if (e >= 1 && shownBy >= 0) DrawTextCentered(TextFormat("%s crab", CRAB_NAME[shownBy]), SLOT.x, SLOT.y + 86, 13, Fade(Pal::Paper, 0.8f));
    } else DrawTextCentered("cards played land here", SLOT.x, SLOT.y - 8, 13, Fade(Pal::Paper, 0.35f));
    for (int i = std::max(0, (int)s.log.size() - 2), row = 0; i < (int)s.log.size(); i++, row++)
        Txt(s.log[i], FELT.x + 22, FELT.y + FELT.height - 42 + row * 17, 13, Fade(Pal::Paper, row ? 0.85f : 0.55f));

    // who's up, and the timer
    std::string banner;
    if (gSess.paused) {
        std::string who; for (auto& st : gSess.seats) if (st.used && st.lost && !st.ai) who = st.name;
        banner = TextFormat("Waiting for %s to reconnect  -  the AI takes over in %d:%02d", who.c_str(), (int)gSess.pauseLeft / 60, (int)gSess.pauseLeft % 60);
    } else if (s.phase == scuttle::PH_MATCH_OVER) banner = OwnerName(s.matchWinner) + "'s " + CRAB_NAME[std::max(0, s.matchWinner)] + " crab wins the match!";
    else if (s.phase == scuttle::PH_ROUND_OVER) banner = TextFormat("Heat %d to %s. The next heat is lining up...", s.round, CRAB_NAME[std::max(0, s.roundWinner)]);
    else if (actor == me) banner = s.phase == scuttle::PH_BET ? "Lay a pearl on a crab, or pass" : s.phase == scuttle::PH_RESPONSE ? "A card is aimed at your crab!" : "Your turn: play a card";
    else if (actor >= 0) banner = OwnerName(actor) + (s.phase == scuttle::PH_BET ? " is weighing a pearl..." : s.phase == scuttle::PH_RESPONSE ? " is deciding whether to duck..." : " is choosing a card...");
    DrawTextCenteredBold(banner, SCREEN_W / 2.0f, 26, 24, gSess.paused ? Pal::Coral : actor == me && actor >= 0 ? Color{255, 220, 140, 255} : Pal::Brass);
    TxtShadow(TextFormat("Heat %d  -  first to %d", s.round, s.winsNeeded), 240, 60, 14, Fade(Pal::Paper, 0.8f));
    if (actor >= 0 && !gSess.paused && s.timer < 1e8f) {
        float full = s.phase == scuttle::PH_RESPONSE ? scuttle::RESPONSE_SECONDS : scuttle::TURN_SECONDS;
        DrawBar({SCREEN_W / 2.0f - 150, 58, 300, 6}, std::max(0.0f, s.timer) / full, s.timer < 5 ? Pal::Coral : Pal::Teal);
    }
    if (Button({1070, 20, 190, 44}, "How to play", !modal, 17)) gShowRules = true;

    // my hand
    if (me >= 0) {
        const scuttle::Seat& p = s.seats[me];
        bool myPlay = actor == me && s.phase == scuttle::PH_PLAY && !gSess.paused;
        const float cw = 126, ch = 176, gap = 12, hx = 60;
        if (myPlay) DrawRectangleRounded({hx - 12, 504, 5 * (cw + gap) + 12, 206}, 0.08f, 6, Fade(Pal::Brass, 0.08f + 0.05f * sinf(t * 4)));
        for (int i = 0; i < (int)p.hand.size(); i++) {
            Rectangle r{hx + i * (cw + gap), 518, cw, ch};
            bool hov = CheckCollisionPointRec(mouse, r);
            DrawCard(r, p.hand[i], hov, gPickCard == i, myPlay);
            if (hov && myPlay && click) {
                int c = p.hand[i];
                if (scuttle::NeedsTarget(c) && !scuttle::Targets(s, me, c).empty()) gPickCard = gPickCard == i ? -1 : i;
                else { scuttle::Action a; a.kind = scuttle::Action::PLAY; a.handIdx = (uint8_t)i; a.target = -1; Send(a); gPickCard = -1; }
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) gPickCard = -1;
        if (pickCard >= 0) DrawTextCentered(pickCard == scuttle::C_ROCK ? "Click a space for the rock (right-click to cancel)" : "Click a crab's lane (right-click to cancel)", 400, 496, 16, Pal::Coral);
        else if (myPlay) DrawTextCentered(TextFormat("Pick a card  -  %d s", (int)ceilf(s.timer)), 400, 496, 15, Fade(Pal::Paper, 0.8f));
        // answering, betting: a prompt over the foot of the pool
        Rectangle q{790, 504, 440, 192};   // (in the table talk's place while it waits on you)
        if (actor == me && !gSess.paused && s.phase == scuttle::PH_RESPONSE) {
            Panel(q);
            DrawWrapped(TextFormat("%s's %s is aimed at your crab. Duck into a Shell to cancel it for you?", OwnerName(s.pendPlayer).c_str(), s.pendCard >= 0 ? scuttle::Card(s.pendCard).name : "card"), {q.x + 16, q.y + 14, q.width - 32, 60}, 17, Pal::Ink);
            if (s.pendCard >= 0) Txt(scuttle::Card(s.pendCard).text, q.x + 16, q.y + 78, 14, Color{90, 60, 40, 255});
            bool has = std::find(p.hand.begin(), p.hand.end(), (uint8_t)scuttle::C_SHELL) != p.hand.end();
            if (Button({q.x + 16, q.y + 124, 200, 50}, "Duck into your Shell", has && !modal, 16)) { scuttle::Action a; a.kind = scuttle::Action::RESPOND; a.shell = true; Send(a); }
            if (Button({q.x + q.width - 216, q.y + 124, 200, 50}, "Let it happen", !modal, 16)) { scuttle::Action a; a.kind = scuttle::Action::RESPOND; a.shell = false; Send(a); }
        } else if (actor == me && !gSess.paused && s.phase == scuttle::PH_BET) {
            Panel(q);
            TxtBold(TextFormat("Lay a pearl face down?  (%d left this heat)", p.betsLeft), q.x + 16, q.y + 14, 18, Pal::Ink);
            Txt("On the winner +3, on the second +1, otherwise -1.", q.x + 16, q.y + 42, 15, Color{90, 60, 40, 255});
            float bw = (q.width - 32 - (s.nSeats - 1) * 8) / s.nSeats;
            for (int k = 0; k < s.nSeats; k++) {
                Rectangle br{q.x + 16 + k * (bw + 8), q.y + 74, bw, 46};
                if (Button(br, CRAB_NAME[k], !modal, 16)) { scuttle::Action a; a.kind = scuttle::Action::BET; a.target = (int8_t)k; Send(a); }
                DrawCircleV({br.x + 14, br.y + 12}, 5, CRAB[k]);
            }
            if (Button({q.x + q.width / 2 - 90, q.y + 132, 180, 44}, "Pass", !modal, 16)) { scuttle::Action a; a.kind = scuttle::Action::SKIPBET; Send(a); }
        }
    }
    bool prompting = me >= 0 && actor == me && !gSess.paused && (s.phase == scuttle::PH_RESPONSE || s.phase == scuttle::PH_BET);
    if (!prompting) DrawTableTalk({780, 494, 460, 212});

    // a heat just ended: who won, and what the pearls paid
    if (s.phase == scuttle::PH_ROUND_OVER && gSplashT < 99) {
        float a = std::min(1.0f, gSplashT * 3);
        Rectangle q{400, 150, 480, 60 + 22.0f * std::max(1, (int)s.bets.size())};
        DrawRectangleRounded(q, 0.08f, 6, Fade(Color{8, 30, 34, 255}, 0.92f * a));
        DrawRectangleRoundedLinesEx(q, 0.08f, 6, 2, Fade(Pal::Brass, a));
        DrawTextCenteredBold(TextFormat("%s's %s crab takes heat %d!  +2", OwnerName(s.roundWinner).c_str(), CRAB_NAME[std::max(0, s.roundWinner)], s.round), q.x + q.width / 2, q.y + 12, 20, Fade(Pal::Brass, a));
        float y = q.y + 44;
        if (s.bets.empty()) DrawTextCentered("No pearls were laid.", q.x + q.width / 2, y, 15, Fade(Pal::Paper, a));
        for (const scuttle::Bet& b : s.bets) {
            int pay = b.crab == s.roundWinner ? 3 : b.crab == s.roundSecond ? 1 : -1;
            DrawTextCentered(TextFormat("%s's pearl on %s:  %+d", OwnerName(b.owner).c_str(), CRAB_NAME[b.crab], pay), q.x + q.width / 2, y, 15, Fade(pay > 0 ? Pal::Good : Pal::Coral, a));
            y += 22;
        }
    }
    // the match is over: the standings, a rematch
    if (s.phase == scuttle::PH_MATCH_OVER) {
        Rectangle q{380, 120, 520, 120 + 44.0f * s.nSeats};
        Panel(q);
        DrawTextCenteredBold(s.matchWinner == me ? "You win the match!" : OwnerName(s.matchWinner) + " wins the match", q.x + q.width / 2, q.y + 14, 26, Pal::Ink);
        std::vector<int> order; for (int k = 0; k < s.nSeats; k++) order.push_back(k);
        std::sort(order.begin(), order.end(), [&](int x, int y) { return s.seats[x].roundWins != s.seats[y].roundWins ? s.seats[x].roundWins > s.seats[y].roundWins : s.seats[x].points > s.seats[y].points; });
        float y = q.y + 58;
        for (int i = 0; i < (int)order.size(); i++, y += 44) {
            int k = order[i];
            DrawCrab({q.x + 50, y + 18}, CRAB[k], t, 0, false, 0.7f);
            TxtBold(OwnerName(k), q.x + 86, y + 6, 19, k == me ? Color{150, 90, 20, 255} : Pal::Ink);
            Txt(TextFormat("%d heat%s  -  %d pearls", s.seats[k].roundWins, s.seats[k].roundWins == 1 ? "" : "s", s.seats[k].points), q.x + 300, y + 8, 16, Pal::Ink);
        }
        if (gSess.role == R_HOST) {
            std::string why;
            if (Button({q.x + 30, q.y + q.height - 56, 220, 44}, "Rematch", !modal, 18)) gSess.Rematch(&why);
            if (Button({q.x + q.width - 250, q.y + q.height - 56, 220, 44}, "Back to the lobby", !modal, 17)) gSess.BackToLobby();
        } else DrawTextCentered("Waiting for the host: a rematch, or back to the lobby.", q.x + q.width / 2, q.y + q.height - 40, 16, Pal::Ink);
    }
    if (gShowRules) DrawRules();
}
void DrawRoom(Game& g) {
    switch (gSess.stage) {
        case S_CONNECTING: {
            Rectangle p{390, 250, 500, 180};
            DrawScreenPanel(p);
            DrawTextCenteredBold("CONNECTING", p.x + p.width / 2, p.y + 30, 26, SCREEN_INK);
            DrawTextCentered(gSess.status, p.x + p.width / 2, p.y + 80, 17, SCREEN_DIM);
            if (Button({p.x + p.width / 2 - 80, p.y + 116, 160, 42}, "Cancel", true, 17)) { gSess.Leave(); gMode = MODE_MENU; }
        } break;
        case S_LOBBY: DrawLobby(); break;
        case S_PLAYING:
            if (gSess.game == G_TRAWL) { StartTrawlNet(g, &gSess, gTrawlFp); return; }   // aboard the Gannet (host or guest)
            DrawTable(g);
            break;
        case S_ENDED: {
            Rectangle p{340, 230, 600, 220};
            DrawScreenPanel(p);
            DrawTextCenteredBold("THE TABLE IS CLOSED", p.x + p.width / 2, p.y + 26, 26, SCREEN_INK);
            DrawWrapped(gSess.endReason, {p.x + 40, p.y + 76, p.width - 80, 60}, 17, SCREEN_DIM);
            bool canRejoin = gSess.rejoinToken && !gSess.hostAddr.empty() && gSess.rejects == 0;
            if (canRejoin && Button({p.x + 60, p.y + 150, 220, 46}, "Rejoin", true, 18)) {
                std::string err, addr = gSess.hostAddr; uint32_t tok = gSess.rejoinToken;
                if (!gSess.Join(gProfile, addr, &err, tok)) gError = err;
            }
            if (Button({p.x + p.width - 280, p.y + 150, 220, 46}, "Back to the reels", true, 18)) { gSess.Leave(); gMode = MODE_MENU; }
        } break;
        default: gMode = MODE_MENU; break;
    }
}
}  // namespace

// ============================================================ the Deep Arcade
void SceneArcade(Game& g) {
    if (!gProfileLoaded) LoadProfile();
    if (gWardrobe >= 0) { if (skins::WardrobePage(gWardrobe)) gWardrobe = -1; return; }
    // the lobby's small noises: someone sits down, someone speaks
    static int lastSeats = 0; static size_t lastChat = 0;
    gSess.Update(GetTime(), GetFrameTime());
    int seated = 0; for (auto& s : gSess.seats) seated += s.used;
    if (gSess.stage == S_LOBBY && seated > lastSeats && lastSeats > 0) PlayCue("arc.join");
    if (gSess.chat.size() != lastChat && gSess.stage >= S_LOBBY && !gSess.chat.empty() && gSess.chat.back().find(": ") != std::string::npos) PlayCue("arc.chat");
    lastSeats = seated; lastChat = gSess.chat.size();

    DrawCabinBackground();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, gMode == MODE_ROOM ? 0.55f : 0.0f));
    bool inGame = gMode == MODE_ROOM && gSess.stage != S_IDLE && gSess.stage != S_ENDED;
    bool midMatch = inGame && gSess.stage == S_PLAYING && gView.phase != scuttle::PH_MATCH_OVER;
    static double leaveArmed = -10;   // leaving mid-match hands your crab to the AI: ask twice
    bool armed = GetTime() - leaveArmed < 3;
    if (!inGame && gMode == MODE_MENU) { if (BackButton(g)) { StopBrowsing(); return; } }
    else if (Button({20, 20, 190, 44}, inGame ? (armed ? "Really leave?" : "Leave the table") : "< The reels", true, 17)) {
        if (midMatch && !armed) leaveArmed = GetTime();
        else {
            PlayCue("ui.cancel");
            if (inGame || gSess.stage == S_ENDED) gSess.Leave();
            StopBrowsing();
            gMode = MODE_MENU; gError.clear(); gShowRules = false; leaveArmed = -10;
            return;
        }
    }
    switch (gMode) {
        case MODE_MENU: DrawReels(g); break;
        case MODE_JOIN: DrawJoin(); break;
        case MODE_BROWSE: DrawBrowse(); break;
        case MODE_ROOM: DrawRoom(g); break;
    }
    if (gMode == MODE_ROOM && gSess.stage == S_LOBBY && gSess.game == G_SCUTTLE) {
        if (Button({1070, 20, 190, 44}, "How to play", !gShowRules, 17)) gShowRules = true;
        if (gShowRules) DrawRules();
    }
    if (!gError.empty() && gMode != MODE_ROOM) {
        int w = MeasureTxt(gError, 16) + 40;
        Rectangle r{(SCREEN_W - w) / 2.0f, 650, (float)w, 36};
        DrawRectangleRounded(r, 0.3f, 6, Color{80, 20, 20, 230});
        DrawTextCentered(gError, SCREEN_W / 2.0f, r.y + 9, 16, Pal::Paper);
    }
}

// --shots: turn the drum to a reel (0 Flats Duel ... 4 Red Tide) on the front page
void DebugArcadeReel(int reel) { gMode = MODE_MENU; gSel = reel; gDrum = (float)reel; }

// the shots (no network needed): 0 the lobby, 1 the table mid-heat, 2 the match over, 3 the rules
void DebugArcadeShot(int which) {
    if (!gProfileLoaded) LoadProfile();
    std::string err;
    SetAudioSuppressed(true);
    gSess.Host(gProfile, G_SCUTTLE, &err, 47791, net::MakeMemoryTransport(), false);
    gSess.AddAI(); gSess.AddAI();
    gSess.Chat("Ahoy!");
    gMode = MODE_ROOM;
    gViewVer = -1; gView = scuttle::State{};
    double clock = 0;
    uint32_t rng = 5;
    auto run = [&](int frames, bool playMe) {
        for (int i = 0; i < frames; i++) {
            clock += 1 / 60.0; gSess.Update(clock, 1 / 60.0f);
            if (!playMe || gSess.Snapshot().empty()) continue;
            scuttle::State v; Reader r(gSess.Snapshot()); if (!scuttle::Deserialize(v, r)) continue;
            if (scuttle::Actor(v) == gSess.MyPlayer() && i % 30 == 0) { Writer w; scuttle::WriteAction(scuttle::Bot(v, gSess.MyPlayer(), rng), w); gSess.Act(w); }
        }
    };
    if (which == 3) gShowRules = true;
    if (which == 1 || which == 2) {
        std::string w; gSess.Launch(&w);
        run(which == 1 ? 300 : 60 * 400, true);
        if (which == 1) {   // catch the next card mid-flight
            TakeView();
            int plays = gView.plays;
            for (int i = 0; i < 60 * 30 && gView.plays == plays; i++) { run(1, true); TakeView(); }
            gPlay.t = 0.6f;
            for (auto& h : gHop) h.t = h.dur;
        } else TakeView();
    }
    SetAudioSuppressed(false);
}
// --shots: the Wardrobe page with a few skins owned (in memory: shots never save)
void DebugWardrobe(Game& g, int game) {
    int tab = game >= 2 ? 1 : 0;   // (2, 3: the costume rack, a costume tried on)
    game %= 2;
    skins::SetWardrobeTab(game, tab, tab ? (game == skins::REDTIDE ? "rc_shark" : "tc_gull") : nullptr);
    if (tab) {
        skins::Wardrobe& w = skins::Get(game);
        w.tokens = 900; w.owned.clear();
        for (int k = 0; k < (int)skins::Costumes(game).size(); k += 4) w.owned.push_back(skins::Costumes(game)[k].id);
        w.costume = w.owned.empty() ? "" : w.owned[0];
        g.scene = Scene::Arcade; gWardrobe = game;
        return;
    }
    g.scene = Scene::Arcade; gWardrobe = game;
    skins::Wardrobe& w = skins::Get(game);
    w.crates = 3; w.tokens = 420; w.owned.clear();
    int k = 0;
    for (const auto& s : skins::Catalogue(game)) { if (k % 3 == 0) w.owned.push_back(s.id); k++; }
    w.worn = w.owned.empty() ? "" : w.owned[1];
}
