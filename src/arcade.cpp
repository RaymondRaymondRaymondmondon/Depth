// ============================================================================
//  The Deep Arcade: the cabinet's screen (the reels, Host / Join / Browse), the lobby and the Scuttle table.
//  The session and the rules live in arcade_session.* and scuttle.* (no raylib); this file only draws and clicks.
// ============================================================================
#include "game.h"
#include "arcade_session.h"
#include "net.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
using namespace arcade;

Session gSess;
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
float gShown[scuttle::MAX_SEATS] = {};
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
    struct Reel { const char* name; const char* players; const char* length; const char* line; bool ready; };
    const Reel reels[4] = {
        {"Flats Duel", "2 players", "8-12 min", "Flats against a person: a best of three at the table.", false},
        {"The Trawl", "1-4 co-op", "15-25 min", "Fish the deep by night, fill the quota, don't wake what's below.", false},
        {"Scuttle", "2-4 players", "5-10 min", "A fast crab-racing card game anyone can learn in one hand.", true},
        {"Fathoms", "2-6 players", "20-30 min", "The island strategy game: six factions of the deep.", false},
    };
    gDrum += (gSel - gDrum) * std::min(1.0f, GetFrameTime() * 8);
    float wheel = GetMouseWheelMove();
    if (wheel < 0 || IsKeyPressed(KEY_DOWN)) gSel = std::min(3, gSel + 1);
    if (wheel > 0 || IsKeyPressed(KEY_UP)) gSel = std::max(0, gSel - 1);
    for (int i = 0; i < 4; i++) {
        float off = (i - gDrum) * 92;
        if (fabsf(off) > 120) continue;
        float sc = 1 - fabsf(off) / 400;
        Rectangle r{c.x - 230 * sc, c.y - 40 + off - 34 * sc, 460 * sc, 68 * sc};
        bool on = i == gSel;
        DrawRectangleRounded(r, 0.25f, 8, on ? Color{30, 120, 118, 255} : Color{16, 60, 64, 255});
        DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, on ? Pal::Brass : Pal::BrassDk);
        DrawTextCenteredBold(reels[i].name, c.x, r.y + 8 * sc, (int)(26 * sc), on ? Color{220, 255, 244, 255} : SCREEN_DIM);
        if (on) DrawTextCentered(TextFormat("%s   -   %s%s", reels[i].players, reels[i].length, reels[i].ready ? "" : "   -   coming aboard later"), c.x, r.y + 40, 15, Color{180, 230, 220, 255});
        if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gSel = i;
    }
    DrawWrapped(reels[gSel].line, {c.x - 200, c.y + 100, 400, 50}, 17, Color{200, 240, 232, 255});

    // the valve wheels: Host, Join, Browse (and a practice table against the arcade's own crabs)
    bool ready = reels[gSel].ready;
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
                if (gSess.Host(gProfile, G_SCUTTLE, &err)) gMode = MODE_ROOM;
                else gError = "Couldn't host: " + err;
            } else if (k == 1) { gMode = MODE_JOIN; gJoinFocus = true; }
            else gMode = MODE_BROWSE;
        }
    }
    if (ready && Button({c.x - 110, c.y + 236, 220, 36}, "Practice with AI crabs", true, 15)) {
        std::string err;
        if (gSess.Host(gProfile, G_SCUTTLE, &err, 47790, net::MakeMemoryTransport(), false)) { gSess.AddAI(); gSess.AddAI(); gSess.AddAI(); gMode = MODE_ROOM; }
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
    DrawTextCenteredBold(TextFormat("%s  -  %s's table", GameName(gSess.game), gSess.hostName.c_str()), p.x + p.width / 2, p.y + 16, 24, SCREEN_INK);
    TxtBold("CODE", p.x + 30, p.y + 58, 16, SCREEN_DIM);
    TxtBold(gSess.code, p.x + 84, p.y + 50, 30, Pal::Brass);
    if (host) {
        static std::string ips; static double ipsAt = -10;
        if (GetTime() - ipsAt > 5) { ipsAt = GetTime(); ips.clear(); for (auto& ip : net::LocalIPv4()) ips += (ips.empty() ? "" : ",  ") + ip; }
        Txt("Your address: " + (ips.empty() ? std::string("?") : ips), p.x + 250, p.y + 52, 15, SCREEN_DIM);
        DrawWrapped("On this network friends Browse or type the code. Elsewhere they need your public address (forward UDP 47778).", {p.x + 250, p.y + 70, p.width - 280, 34}, 13, Fade(SCREEN_DIM, 0.8f));
    }
    float y = p.y + 110;
    int maxP = GameMaxPlayers(gSess.game);
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
void DrawTable(Game& g) {
    const scuttle::State& s = gSess.view;
    float t = g.time, dt = GetFrameTime();
    int me = gSess.MyCrab();
    if (gLastVersion != gSess.stateVersion) { gLastVersion = gSess.stateVersion; gPickCard = -1; }
    // the felt and its tide pool
    Rectangle felt{40, 90, 1200, 390};
    DrawRectangleRounded({felt.x + 6, felt.y + 10, felt.width, felt.height}, 0.06f, 8, Fade(BLACK, 0.5f));
    DrawRectangleRounded({felt.x - 12, felt.y - 12, felt.width + 24, felt.height + 24}, 0.07f, 8, Color{70, 44, 28, 255});
    DrawRectangleRounded(felt, 0.06f, 8, Color{22, 84, 70, 255});
    const float x0 = 250, cell = 100;
    int n = std::max(2, s.nSeats);
    float laneH = std::min(86.0f, (felt.height - 40) / n);
    float laneY0 = felt.y + 20 + (felt.height - 40 - laneH * n) / 2;
    for (int sp = 0; sp <= scuttle::TRACK; sp++) {
        float x = x0 + sp * cell;
        Color col = sp == scuttle::TRACK ? Color{210, 190, 130, 255} : sp == 0 ? Color{40, 60, 56, 255} : Color{32, 104, 92, 255};
        DrawRectangleRounded({x - cell / 2 + 4, laneY0 - 8, cell - 8, laneH * n + 16}, 0.1f, 6, Fade(col, sp == scuttle::TRACK ? 0.5f : 0.6f));
        DrawTextCentered(sp == 0 ? "start" : sp == scuttle::TRACK ? "tide line" : std::to_string(sp), x, laneY0 - 30, 15, Pal::Paper);
        if (sp == s.rock) { for (int k = 0; k < n; k++) DrawEllipse((int)x, (int)(laneY0 + laneH * k + laneH * 0.72f), 16, 8, Color{110, 104, 96, 255}); }
    }
    // the foam line along the finish
    for (int k = 0; k < 30; k++) { float yy = laneY0 - 8 + k * (laneH * n + 16) / 30; DrawCircleV({x0 + scuttle::TRACK * cell + cell / 2 - 4 + sinf(t * 2 + k) * 3, yy}, 4, Fade(WHITE, 0.4f)); }
    // pick a target: a crab lane or a space
    int pickCard = gPickCard >= 0 && me >= 0 && gPickCard < (int)s.seats[me].hand.size() ? s.seats[me].hand[gPickCard] : -1;
    std::vector<int> targets = pickCard >= 0 ? scuttle::Targets(s, me, pickCard) : std::vector<int>{};
    Vector2 mouse = GetMousePosition();
    for (int k = 0; k < s.nSeats; k++) {
        const scuttle::Seat& p = s.seats[k];
        float ly = laneY0 + laneH * k;
        float target = (float)p.pos;
        float before = gShown[k];
        gShown[k] += (target - gShown[k]) * std::min(1.0f, dt * 5);
        float moving = fabsf(target - before) > 0.002f ? 1.0f : 0.0f;
        bool acting = scuttle::Actor(s) == k;
        if (acting) DrawRectangleRounded({felt.x + 10, ly + 2, felt.width - 20, laneH - 4}, 0.2f, 6, Fade(WHITE, 0.07f));
        int seat = gSess.SeatOfCrab(k);
        std::string owner = seat >= 0 ? gSess.seats[seat].name : "?";
        TxtBold(owner, felt.x + 24, ly + laneH / 2 - 20, 18, k == me ? Pal::Brass : Pal::Paper);
        Txt(TextFormat("%s crab  -  %d pts  -  %d card%s", CRAB_NAME[k], p.points, p.handCount, p.handCount == 1 ? "" : "s"), felt.x + 24, ly + laneH / 2 + 2, 13, Fade(Pal::Paper, 0.8f));
        for (int w = 0; w < s.winsNeeded; w++) DrawCircleV({felt.x + 30 + w * 14.0f, ly + laneH / 2 + 26}, 5, w < p.roundWins ? Pal::Brass : Fade(Pal::Paper, 0.25f));
        for (int b = 0; b < s.betsHidden[k]; b++) DrawCircleV({felt.x + 150 + b * 12.0f, ly + laneH / 2 + 26}, 5, Color{230, 226, 240, 255});   // pearls face down
        DrawCrab({x0 + gShown[k] * cell, ly + laneH * 0.55f}, CRAB[k], t, moving, p.skipMove, std::min(1.0f, laneH / 80));
        bool isTarget = pickCard == scuttle::C_SIDESTEP || pickCard == scuttle::C_PINCH ? std::find(targets.begin(), targets.end(), k) != targets.end() : false;
        if (isTarget) {
            Rectangle lr{x0 - cell / 2, ly, cell * (scuttle::TRACK + 1), laneH};
            bool hov = CheckCollisionPointRec(mouse, lr);
            DrawRectangleRoundedLinesEx(lr, 0.2f, 6, hov ? 4.0f : 2.0f, Fade(Pal::Coral, 0.6f + 0.4f * sinf(t * 6)));
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { scuttle::Action a; a.kind = scuttle::Action::PLAY; a.handIdx = (uint8_t)gPickCard; a.target = (int8_t)k; gSess.Act(a); gPickCard = -1; }
        }
    }
    // revealed bets at the round's end
    for (const scuttle::Bet& b : s.bets) {
        float ly = laneY0 + laneH * b.crab;
        DrawCircleV({x0 + scuttle::TRACK * cell + 40 + b.owner * 12.0f, ly + laneH / 2}, 5, CRAB[b.owner]);
    }
    if (pickCard == scuttle::C_ROCK) for (int sp : targets) {
        Rectangle cr{x0 + sp * cell - cell / 2 + 4, laneY0 - 8, cell - 8, laneH * n + 16};
        bool hov = CheckCollisionPointRec(mouse, cr);
        DrawRectangleRoundedLinesEx(cr, 0.1f, 6, hov ? 4.0f : 2.0f, Fade(Pal::Coral, 0.5f + 0.4f * sinf(t * 6)));
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { scuttle::Action a; a.kind = scuttle::Action::PLAY; a.handIdx = (uint8_t)gPickCard; a.target = (int8_t)sp; gSess.Act(a); gPickCard = -1; }
    }

    // who's up, and the timer
    int actor = scuttle::Actor(s);
    std::string banner;
    if (gSess.paused) {
        std::string who; for (auto& st : gSess.seats) if (st.used && st.lost && !st.ai) who = st.name;
        banner = TextFormat("Waiting for %s to reconnect  -  the AI takes their crab in %d:%02d", who.c_str(), (int)gSess.pauseLeft / 60, (int)gSess.pauseLeft % 60);
    } else if (s.phase == scuttle::PH_MATCH_OVER) {
        int seat = gSess.SeatOfCrab(s.matchWinner);
        banner = TextFormat("%s's %s crab wins the match!", seat >= 0 ? gSess.seats[seat].name.c_str() : "?", CRAB_NAME[std::max(0, s.matchWinner)]);
    } else if (s.phase == scuttle::PH_ROUND_OVER) banner = TextFormat("%s wins round %d. The next heat is lining up...", CRAB_NAME[std::max(0, s.roundWinner)], s.round);
    else if (actor == me) banner = s.phase == scuttle::PH_BET ? "Lay a pearl on a crab, or pass" : s.phase == scuttle::PH_RESPONSE ? "A card is aimed at you!" : "Your turn: play a card";
    else if (actor >= 0) { int seat = gSess.SeatOfCrab(actor); banner = TextFormat("%s is %s...", seat >= 0 ? gSess.seats[seat].name.c_str() : "?", s.phase == scuttle::PH_BET ? "betting" : s.phase == scuttle::PH_RESPONSE ? "deciding whether to duck" : "choosing a card"); }
    DrawTextCenteredBold(banner, SCREEN_W / 2.0f, 30, 24, gSess.paused ? Pal::Coral : Pal::Brass);
    if (actor >= 0 && !gSess.paused && s.timer < 1e8f) {
        float full = s.phase == scuttle::PH_RESPONSE ? scuttle::RESPONSE_SECONDS : scuttle::TURN_SECONDS;
        DrawBar({SCREEN_W / 2.0f - 150, 62, 300, 6}, std::max(0.0f, s.timer) / full, s.timer < 5 ? Pal::Coral : Pal::Teal);
    }
    // the log
    float ly = 494;
    for (int i = std::max(0, (int)s.log.size() - 4); i < (int)s.log.size(); i++, ly += 18) Txt(s.log[i], 900, ly, 14, Fade(Pal::Paper, 0.85f));

    // my hand
    if (me >= 0) {
        const scuttle::Seat& p = s.seats[me];
        bool myPlay = actor == me && s.phase == scuttle::PH_PLAY && !gSess.paused;
        float cw = 126, ch = 176, gap = 12;
        float hx = 60;
        for (int i = 0; i < (int)p.hand.size(); i++) {
            Rectangle r{hx + i * (cw + gap), 516, cw, ch};
            bool hov = CheckCollisionPointRec(mouse, r);
            DrawCard(r, p.hand[i], hov, gPickCard == i, myPlay);
            if (hov && myPlay && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int c = p.hand[i];
                if (scuttle::NeedsTarget(c) && !scuttle::Targets(s, me, c).empty()) gPickCard = gPickCard == i ? -1 : i;
                else { scuttle::Action a; a.kind = scuttle::Action::PLAY; a.handIdx = (uint8_t)i; a.target = -1; gSess.Act(a); gPickCard = -1; }
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) gPickCard = -1;
        if (pickCard >= 0) DrawTextCentered(pickCard == scuttle::C_ROCK ? "Click a space for the rock (right-click to cancel)" : "Click a crab's lane (right-click to cancel)", 380, 494, 16, Pal::Coral);
        // answering, betting
        if (actor == me && !gSess.paused && s.phase == scuttle::PH_RESPONSE) {
            Rectangle q{760, 560, 460, 120};
            Panel(q);
            TxtBold(TextFormat("%s is aimed at your crab.", s.pendCard >= 0 ? scuttle::Card(s.pendCard).name : "A card"), q.x + 16, q.y + 12, 18, Pal::Ink);
            bool has = std::find(p.hand.begin(), p.hand.end(), (uint8_t)scuttle::C_SHELL) != p.hand.end();
            if (Button({q.x + 16, q.y + 56, 200, 46}, "Duck into your Shell", has, 16)) { scuttle::Action a; a.kind = scuttle::Action::RESPOND; a.shell = true; gSess.Act(a); }
            if (Button({q.x + 236, q.y + 56, 200, 46}, "Let it happen", true, 16)) { scuttle::Action a; a.kind = scuttle::Action::RESPOND; a.shell = false; gSess.Act(a); }
        } else if (actor == me && !gSess.paused && s.phase == scuttle::PH_BET) {
            Rectangle q{760, 560, 460, 120};
            Panel(q);
            TxtBold(TextFormat("Lay a pearl? (%d left this round)  +3 if it wins, +1 second, -1 else", p.betsLeft), q.x + 14, q.y + 10, 14, Pal::Ink);
            for (int k = 0; k < s.nSeats; k++) if (Button({q.x + 14 + k * 88.0f, q.y + 44, 82, 40}, CRAB_NAME[k], true, 15)) { scuttle::Action a; a.kind = scuttle::Action::BET; a.target = (int8_t)k; gSess.Act(a); }
            if (Button({q.x + 14 + s.nSeats * 88.0f, q.y + 44, 82, 40}, "Pass", true, 15)) { scuttle::Action a; a.kind = scuttle::Action::SKIPBET; gSess.Act(a); }
        }
    }
    if (s.phase == scuttle::PH_MATCH_OVER) {
        if (gSess.role == R_HOST) { if (Button({SCREEN_W / 2.0f - 120, 430, 240, 46}, "Back to the lobby", true, 18)) gSess.BackToLobby(); }
        else DrawTextCentered("Waiting for the host...", SCREEN_W / 2.0f, 444, 17, Pal::Paper);
    }
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
        case S_PLAYING: DrawTable(g); break;
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
    gSess.Update(GetTime(), GetFrameTime());
    DrawCabinBackground();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, gMode == MODE_ROOM ? 0.55f : 0.0f));
    bool inGame = gMode == MODE_ROOM && gSess.stage != S_IDLE && gSess.stage != S_ENDED;
    if (!inGame && gMode == MODE_MENU) { if (BackButton(g)) { StopBrowsing(); return; } }
    else if (Button({20, 20, 190, 44}, inGame ? "Leave the table" : "< The reels", true, 17)) {
        PlayCue("ui.cancel");
        if (inGame || gSess.stage == S_ENDED) gSess.Leave();
        StopBrowsing();
        gMode = MODE_MENU; gError.clear();
        return;
    }
    switch (gMode) {
        case MODE_MENU: DrawReels(g); break;
        case MODE_JOIN: DrawJoin(); break;
        case MODE_BROWSE: DrawBrowse(); break;
        case MODE_ROOM: DrawRoom(g); break;
    }
    if (!gError.empty() && gMode != MODE_ROOM) {
        int w = MeasureTxt(gError, 16) + 40;
        Rectangle r{(SCREEN_W - w) / 2.0f, 650, (float)w, 36};
        DrawRectangleRounded(r, 0.3f, 6, Color{80, 20, 20, 230});
        DrawTextCentered(gError, SCREEN_W / 2.0f, r.y + 9, 16, Pal::Paper);
    }
}

// the shots: the lobby and the table with AI crabs (no network needed)
void DebugArcadeShot(int which) {
    if (!gProfileLoaded) LoadProfile();
    std::string err;
    gSess.Host(gProfile, G_SCUTTLE, &err, 47791, net::MakeMemoryTransport(), false);
    gSess.AddAI(); gSess.AddAI();
    gMode = MODE_ROOM;
    if (which == 1) {
        std::string w; gSess.Launch(&w);
        for (int i = 0; i < 600; i++) gSess.Update(i / 60.0, 1 / 60.0f);   // let the AI crabs run a few turns
        for (int k = 0; k < scuttle::MAX_SEATS; k++) gShown[k] = gSess.view.seats[k].pos;
    }
}
