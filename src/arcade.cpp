// ============================================================================
//  The Deep Arcade: the cabinet's screen (the reels, Host / Join / Browse), the lobby and the Scuttle table.
//  The session and the rules live in arcade_session.* and scuttle.* (no raylib); this file only draws and clicks.
// ============================================================================
#include "mouthful.h"
#include "scuffle.h"
#include "game.h"
#include "nightoff.h"
#include "arcade_session.h"
#include "deep_launch.h"
#include "scuttle.h"
#include "flight_net.h"
#include "warp_net.h"
#include "ballpit_net.h"
#include "fathoms_net.h"
#include "fowl_net.h"
#include "noclip_net.h"
#include "net.h"
#include "skins.h"
#include "voice.h"
#include "input.h"
static int gWardrobe = -1;
static bool gMfWardrobe = false; static int gMfMode = 0, gMfPath = 2;   // Mouthful's wardrobe page; the mode (and One Path's path) picked on its reel
static bool gSfLocker = false;   // (Scuffle's locker page over the arcade)
static bool gFpLocker = false;   // (Fowl Play's locker)
static bool gNcLocker = false;   // (NOCLIP's)
static int gNcMode = 0;
static bool gFlWardrobe = false; static int gFlGallery = -1;   // the Flight's Roost wardrobe; a costume gallery page (--shots)   // the skins page over the arcade (skins::TRAWL), -1 none
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <fstream>

int BetWallet(int game);                 // (the pre-match bets, at the end of the file)
void DrawBetControls(Rectangle row);
void ArcadeBetFrame();
static void DrawBetResult();

fa::Settings gFaSettings;   // (the arcade's picks for a Fathoms game: solo or hosted)
bool gFaResume = false;      // (hosting reloads fathoms_autosave.bin)
namespace {
int gNoMode = 0, gNoCrowd = 1, gNoCrew = 0, gNoBar = 0, gNoSeason = -1; bool gNoPvp = true, gNoCloak = false;   // (A Night Off: the host's mode, crowd and fights; who you go ashore as)

using namespace arcade;

Session gSess;
const char* RT_MAP_KEYS[5] = {"ship", "cave", "reef", "atlantis", "void"};
const char* RT_TITLES[5] = {"The Sunken Ship", "The Underwater Cave", "The Coral Reef", "Atlantis", "Approaching the Void"};
int gRtMapSel = 0;        // Red Tide's map on the reel (solo, and what a host's table dives)
int gRtModeSel = 0;       // and its mode (design doc "Modes")
int gRtSeasonPick = 0;    // and the species season (0 none)
static bool gRtCustomOpen = false;   // Custom mode's rules panel, over the arcade
// the Flight's choices: your founder, your island, the arrangement, the starting islands (solo), the match's length
int gFlSel = 0, gFlIsle = 0, gFlArr = 0, gFlPlayers = 4, gFlMinutes = 1, gFlSoloSeasons = 0;
// the match's length: the standard lengths, then the expansion's long matches by seasons (2, 3 or 4)
int FlLengthCount() { return std::max(1, (int)fl::MatchLengths().size()) + 5; }
int FlSeasonsOf(int i) { static const int S[5] = {2, 3, 4, 6, 8}; int n = std::max(1, (int)fl::MatchLengths().size()); return i >= n ? S[std::min(4, i - n)] : 0; }   // (6, 8: the Long Flight)
std::string FlLengthName(int i) {
    const auto& L = fl::MatchLengths(); int s = FlSeasonsOf(i);
    if (s) { static const char* N[9] = {"", "", "Two seasons", "Three seasons", "Four seasons", "", "The Long Flight, short", "", "The Long Flight"}; return TextFormat("%s (%d days)", N[s], fl::SeasonDays(s)); }
    return TextFormat("%d minutes", L.empty() ? 30 : L[std::clamp(i, 0, (int)L.size() - 1)]);
}
std::string FlOpts() {
    const auto& L = fl::MatchLengths(); int s = FlSeasonsOf(gFlMinutes);
    int minutes = s ? (int)(fl::SeasonDays(s) * fl::World::DAY / 60) : L.empty() ? 30 : L[std::clamp(gFlMinutes, 0, (int)L.size() - 1)];
    return fl::FlightHostOpts(gFlArr, gFlIsle, minutes) + (s ? TextFormat(":seasons=%d", s) : "");
}
std::string RtOpts() { return std::string(RT_MAP_KEYS[gRtMapSel]) + ":" + RedTideModeKey(gRtModeSel) + ":" + std::to_string(gRtSeasonPick) + (std::string(RedTideModeKey(gRtModeSel)) == "custom" ? ":" + RedTideCustomRules() : std::string()); }
bool gTrawlFp = false;   // the Trawl's view for a networked match (the reel remembers the last one chosen)
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
        NoteTyping();
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
    const int NREELS = 14;
    const Reel reels[NREELS] = {
        {G_FLATS_DUEL, "2 players", "8-12 min", "Flats against a person: a best of three at the table."},
        {G_TRAWL, "1-6 co-op", "30-35 min", "Work a steam trawler by night: catch it, kill it, cook it, sell it, and meet the Owners' quota."},
        {G_SCUTTLE, "2-4 players", "5-10 min", "A fast crab-racing card game anyone can learn in one hand."},
        {G_FATHOMS, "2-6 players", "20-30 min", "The island strategy game: six factions of the deep."},
        {G_RED_TIDE, "1-4 co-op", "20-60 min", "Divers in living ecosystems: kill for scrip, and the blood in the water brings what eats everything."},
        {G_FLIGHT, "2-6 players, or solo with bots", "20-45 min", "Be the bird: fly your Founder in person, fish the living sea, and grow a colony."},
        {G_MOUTHFUL, "up to 12 mouths (solo with bots)", "10-20 min", "Start as a fry, eat your way up the food chain, pick a path at each fork, and wear the crown."},
        {G_NIGHT_OFF, "1-6 players", "25-40 min", "One night ashore at the Sodden Gull: drink, play, flirt, fight, and make it to the morning with both kidneys."},
        {G_SCUFFLE, "2-8 players (solo with bots)", "10-20 min", "Stick figures, ragdolls, and whatever falls from the sky: the last stick standing wins the round."},
        {G_WARP, "2-12 players (solo with bots)", "10-15 min", "Team dodgeball where everyone carries a portal gun: throw through a wall and out of the ceiling."},
        {G_FOWL, "1-6 players (bots fill the stalls)", "25 min", "A light-gun duck shoot with money: thirty toy guns, slots and scratchers, and sabotage for your friends."},
        {G_NOCLIP, "1-6 co-op (solo with bot salvagers)", "45-90 min", "Scavenge the Backrooms for the Bureau: carry it to a Threshold Lab, signal the portal, and make the week's quota."},
        {G_BALLPIT, "2-12 players (solo with bots)", "10-15 min", "Foam guns in a four-storey play centre: climb the nets, ride the slides, hide in the ball pits - and any live ball is a knockout."},
        {G_DEEP, "1-4 co-op", "a long saved campaign", "Adrift on a raft at night: board the derelict Nautilus, repair her room by room, and take her down through a living ocean."},
    };
    // the games in five groups (the playtesters' call): a tab row, and the drum shows one group's reels
    static const char* CATS[5] = {"Action", "Strategy", "Fighting", "Traditional", "Slop"};
    static const int CAT_N[5] = {4, 2, 3, 2, 3};
    static const int CAT_LIST[5][4] = {{1, 4, 11, 13}, {5, 3, -1, -1}, {6, 8, 12, -1}, {0, 2, -1, -1}, {7, 9, 10, -1}};   // (reel indices: the Trawl, Red Tide, NOCLIP, The Deep | the Flight, Fathoms | Mouthful, Scuffle, Ball Pit | Flats Duel, Scuttle | A Night Off, Warp Dodgeball, Fowl Play)
    auto catOf = [&](int reel) { for (int k = 0; k < 5; k++) for (int j = 0; j < CAT_N[k]; j++) if (CAT_LIST[k][j] == reel) return k; return 0; };
    auto posIn = [&](int reel) { int k = catOf(reel); for (int j = 0; j < CAT_N[k]; j++) if (CAT_LIST[k][j] == reel) return j; return 0; };
    static int lastCat = -1;
    int cat = catOf(gSel);
    for (int k = 0; k < 5; k++) {   // the tabs
        Rectangle tr{c.x - 235 + k * 94.0f, c.y - 216, 88, 26};
        bool on = k == cat, hov = CheckCollisionPointRec(GetMousePosition(), tr);
        DrawRectangleRounded(tr, 0.4f, 6, on ? Color{30, 120, 118, 255} : hov ? Color{22, 84, 86, 255} : Color{12, 44, 48, 255});
        DrawRectangleRoundedLinesEx(tr, 0.4f, 6, 1.5f, on ? Pal::Brass : Pal::BrassDk);
        DrawTextCenteredBold(CATS[k], tr.x + tr.width / 2, tr.y + 5, 14, on ? Color{220, 255, 244, 255} : SCREEN_DIM);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !on) { gSel = CAT_LIST[k][0]; PlayCue("ui.click"); }
    }
    cat = catOf(gSel);
    if (cat != lastCat) { gDrum = (float)posIn(gSel); lastCat = cat; }
    // the wheel and the arrows roll the drum; past a group's last reel the next group comes round
    float wheel = GetMouseWheelMove();
    int pos = posIn(gSel);
    if (wheel < 0 || IsKeyPressed(KEY_DOWN)) { if (pos + 1 < CAT_N[cat]) gSel = CAT_LIST[cat][pos + 1]; else { int k2 = (cat + 1) % 5; gSel = CAT_LIST[k2][0]; } }
    if (wheel > 0 || IsKeyPressed(KEY_UP)) { if (pos > 0) gSel = CAT_LIST[cat][pos - 1]; else { int k2 = (cat + 4) % 5; gSel = CAT_LIST[k2][CAT_N[k2] - 1]; } }
    cat = catOf(gSel); if (cat != lastCat) { gDrum = (float)posIn(gSel) + (wheel < 0 || IsKeyPressed(KEY_DOWN) ? -1.0f : 1.0f); lastCat = cat; }
    pos = posIn(gSel);
    gDrum += (pos - gDrum) * std::min(1.0f, GetFrameTime() * 8);
    // the drum: a cylinder lying on its side. Each reel sits on its face at an angle; turning it brings the next round
    // from behind (squashed, dim) to the front (full height, lit). Ribs turn with it, so the roll reads even between reels
    {
        const float R = 62, cy = c.y - 96, STEP = 0.85f;
        Rectangle drum{c.x - 250, cy - R - 6, 500, 2 * R + 12};
        DrawRectangleRounded(drum, 0.12f, 8, Color{8, 30, 34, 255});
        for (int b = 0; b < 12; b++) { float u = b / 11.0f, a = (u - 0.5f) * PI; float sh = cosf(a); DrawRectangle((int)drum.x + 8, (int)(cy + sinf(a) * R - 6), (int)drum.width - 16, 12, Fade(Color{20, 90, 92, 255}, 0.25f * sh * sh)); }   // (the curve: lighter across the middle)
        float frac = gDrum - floorf(gDrum);
        for (int j = -6; j <= 6; j++) { float a = (j - frac * 2) * (STEP / 2); if (fabsf(a) > 1.45f) continue; float y = cy + sinf(a) * R, w = 236 * (0.9f + 0.1f * cosf(a)); DrawLineEx({c.x - w, y}, {c.x + w, y}, 1, Fade(Pal::BrassDk, 0.35f * cosf(a))); }
        DrawRectangleRoundedLinesEx(drum, 0.12f, 8, 2, Pal::BrassDk);
        struct Face { int reel; float a; };
        std::vector<Face> faces;
        for (int j = 0; j < CAT_N[cat]; j++) { float a = (j - gDrum) * STEP; if (fabsf(a) < 1.4f) faces.push_back({CAT_LIST[cat][j], a}); }
        std::sort(faces.begin(), faces.end(), [](const Face& p, const Face& q) { return cosf(p.a) < cosf(q.a); });   // (back to front)
        for (const Face& f : faces) {
            int i = f.reel; float ca = cosf(f.a), y = cy + sinf(f.a) * R;
            float h = 72 * ca, w = 460 * (0.88f + 0.12f * ca);
            Rectangle r{c.x - w / 2, y - h / 2, w, h};
            bool on = i == gSel;
            Color face = on ? Color{30, 120, 118, 255} : Color{16, 60, 64, 255};
            DrawRectangleRounded(r, 0.25f, 8, ColorLerp(Color{6, 20, 24, 255}, face, 0.35f + 0.65f * ca));
            DrawRectangleRoundedLinesEx(r, 0.25f, 8, 2, ColorLerp(Color{6, 20, 24, 255}, on ? Pal::Brass : Pal::BrassDk, ca));
            if (ca > 0.45f) {
                DrawTextCenteredBold(Info(reels[i].game).name, c.x, r.y + 8 * ca, (int)(26 * ca), ColorLerp(Color{40, 70, 72, 255}, on ? Color{220, 255, 244, 255} : SCREEN_DIM, ca));
                if (on && ca > 0.8f) DrawTextCentered(TextFormat("%s   -   %s%s", reels[i].players, reels[i].length, Info(reels[i].game).built || reels[i].game == G_TRAWL || reels[i].game == G_RED_TIDE || reels[i].game == G_FLIGHT || reels[i].game == G_MOUTHFUL || reels[i].game == G_NIGHT_OFF || reels[i].game == G_SCUFFLE || reels[i].game == G_WARP || reels[i].game == G_FOWL || reels[i].game == G_NOCLIP || reels[i].game == G_DEEP ? "": "   -   coming aboard later"), c.x, r.y + 40 * ca, 15, Color{180, 230, 220, 255});
            }
            if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gSel = i;
        }
        // the cabinet's lip over the drum's top and bottom edges (it turns inside the machine)
        DrawRectangleGradientV((int)drum.x, (int)drum.y, (int)drum.width, 26, Color{8, 30, 34, 255}, Fade(Color{8, 30, 34, 255}, 0));
        DrawRectangleGradientV((int)drum.x, (int)(drum.y + drum.height - 26), (int)drum.width, 26, Fade(Color{8, 30, 34, 255}, 0), Color{8, 30, 34, 255});
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
                if (gSess.Host(gProfile, selGame, &err)) { gMode = MODE_ROOM; gSess.gameOpts = selGame == G_RED_TIDE ? RtOpts() : selGame == G_FLIGHT ? FlOpts() : selGame == G_MOUTHFUL ? MouthfulOpts(15, 0, 12) : selGame == G_NIGHT_OFF ? NightOffOpts(gNoMode, gNoCrowd, gNoPvp, gNoBar, gNoSeason) : selGame == G_SCUFFLE ? ScuffleOpts(5, 0, 2) : selGame == G_WARP ? wd::WarpOpts(gWarpArena, 1, gWarpFill) : selGame == G_FOWL ? fp::FowlOpts(gFowlMode, 1, 6) : selGame == G_NOCLIP ? nc::NoclipOpts(0, gNcMode) : selGame == G_BALLPIT ? bp::BallPitOpts(gBallPitMode, 1, gBallPitFill) : selGame == G_FATHOMS ? fa::FathomsOpts(gFaSettings) + (gFaResume ? " resume" : "") : ""; }
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
    if (selGame == G_FLIGHT) {   // stage 1: the Founder alone over the tropical island
        int& flSel = gFlSel;
        int nf = std::max(1, FlightFounderCount());
        Rectangle l{c.x - 190, c.y + 52, 30, 26}, r{c.x + 160, c.y + 52, 30, 26};
        DrawTextCenteredBold(FlightFounderName(flSel), c.x, c.y + 54, 20, Color{230, 200, 150, 255});
        DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass);
        DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
        if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) || IsKeyPressed(KEY_LEFT)) { flSel = (flSel + nf - 1) % nf; PlayCue("ui.click"); }
        if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) || IsKeyPressed(KEY_RIGHT)) { flSel = (flSel + 1) % nf; PlayCue("ui.click"); }

        // the map: your island type, the arrangement, how many starting islands (the rivals sit still until stage 4)
        int& flIsle = gFlIsle; int& flArr = gFlArr; int& flPlayers = gFlPlayers;
        // (on a plate to the left of the drum, like Red Tide's pages)
        DrawRectangleRounded({28, 236, 268, 296}, 0.08f, 6, Fade(Color{8, 30, 34, 255}, 0.85f));
        DrawRectangleRoundedLinesEx({28, 236, 268, 296}, 0.08f, 6, 2, Pal::BrassDk);
        DrawTextCenteredBold("The map", 162, 246, 18, Color{230, 200, 150, 255});
        auto cyc = [&](float y, const char* label, int& v, int n, const char* text) {
            Rectangle l{40, y + 16, 24, 22}, r{260, y + 16, 24, 22};
            DrawTextCentered(label, 162, y, 13, SCREEN_DIM);
            DrawTextCenteredBold(text, 162, y + 17, 16, Color{180, 230, 220, 255});
            DrawTextCenteredBold("<", l.x + 12, l.y, 18, Pal::Brass); DrawTextCenteredBold(">", r.x + 12, r.y, 18, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; PlayCue("ui.click"); }
        };
        cyc(276, "your island", flIsle, 10, FlightIsleTypeName(flIsle));   // (the four, then the expansion's six)
        cyc(326, "the arrangement", flArr, 4, FlightArrangementName(flArr));
        { int pv = flPlayers - 2; cyc(376, "starting islands (solo)", pv, 5, TextFormat("%d: you and %d bot colonies", flPlayers, flPlayers - 1)); flPlayers = pv + 2; }
        { static const char* M[6] = {"Standard (no limit)", "Two seasons (11 days)", "Three seasons (17 days)", "Four seasons (24 days)", "Long Flight, short (36 days)", "The Long Flight (48 days)"}; static const int SV[6] = {0, 2, 3, 4, 6, 8};
          int mv = 0; for (int k = 0; k < 6; k++) if (SV[k] == gFlSoloSeasons) mv = k; cyc(426, "the match", mv, 6, M[mv]); gFlSoloSeasons = SV[mv]; }
        DrawWrapped(FlightFounderLine(flSel), {40, 478, 244, 48}, 13, SCREEN_DIM);
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Fly (solo)", true, 15)) { StartFlight(g, FlightFounderKey(flSel), flIsle, flArr, flPlayers, gFlSoloSeasons); return; }
        if (FlightResumable() && Button({c.x - 110, c.y + 280, 220, 30}, "Resume the Long Flight", true, 13)) { if (ResumeFlight(g)) return; }
        if (Button({c.x + 120, c.y + 236, 170, 36}, "Roost wardrobe", true, 14)) { gFlWardrobe = true; return; }
        DrawTextCentered("Host or Join to fly with friends (2-6; the host picks the map and the length in the lobby)", c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_SCUFFLE) {   // solo: you and the bots (Host or Join above for friends: up to eight sticks)
        static int sfBots = 3, sfSkill = 2, sfToWin = 1, sfWorld = -1;
        static const char* SKILL[3] = {"Stumble bots", "Scrap bots", "Sharp bots"};
        static const int TOWIN[3] = {5, 10, 20};
        auto row = [&](float y, const char* text, int& v, int n, int lo) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y - 10, TextFormat("%d bot%s", sfBots, sfBots == 1 ? "" : "s"), sfBots, 7, 1);
        row(c.y + 16, SKILL[sfSkill], sfSkill, 3, 0);
        row(c.y + 42, TextFormat("first to %d", TOWIN[sfToWin]), sfToWin, 3, 0);
        {   // the match, on a plate to the left of the drum: the world, your trinket, the rules
            DrawRectangleRounded({28, 236, 268, 330}, 0.08f, 6, Fade(Color{8, 30, 34, 255}, 0.85f));
            DrawRectangleRoundedLinesEx({28, 236, 268, 330}, 0.08f, 6, 2, Pal::BrassDk);
            DrawTextCenteredBold("The match", 162, 246, 18, Color{230, 200, 150, 255});
            auto cyc = [&](float y, const char* label, int& v, int n, int lo, const char* text) {
                Rectangle l{40, y + 16, 24, 22}, r{260, y + 16, 24, 22};
                DrawTextCentered(label, 162, y, 13, SCREEN_DIM);
                DrawTextCenteredBold(text, 162, y + 17, 15, Color{180, 230, 220, 255});
                DrawTextCenteredBold("<", l.x + 12, l.y, 18, Pal::Brass); DrawTextCenteredBold(">", r.x + 12, r.y, 18, Pal::Brass);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
            };
            cyc(272, "the mode", gScuffleMode, sf::MD_COUNT, 0, sf::ModeName(gScuffleMode));
            cyc(316, gScuffleMode == sf::MD_BOSS ? "the boss" : "the stages", sfWorld, 8, -1, gScuffleMode == sf::MD_BOSS ? (sfWorld < 0 || sfWorld >= sf::WD_COUNT ? "all six in turn" : sf::BossName(sf::BossOfWorld(sfWorld))) : ScuffleWorldChoice(sfWorld));
            cyc(360, "your trinket (others see it)", gScuffleTrinket, sf::TK_COUNT + 1, -1, gScuffleTrinket < 0 ? "the game picks" : sf::TrinketName(gScuffleTrinket));
            cyc(404, "the rules", gScuffleRules, sf::MU_COUNT + 2, 0, ScuffleRulesChoice(gScuffleRules));
            const char* note = gScuffleMode != sf::MD_CLASSIC ? sf::ModeRule(gScuffleMode) : gScuffleTrinket >= 0 ? sf::TrinketText(gScuffleTrinket) : gScuffleRules >= 2 ? sf::MutatorText(gScuffleRules - 2) : "Gear is the E key (or the right mouse button).";
            DrawWrapped(note, {40, 452, 244, 110}, 13, SCREEN_DIM);
        }
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Fight (solo)", true, 15)) { StartScuffle(g, sfBots, sfSkill, TOWIN[sfToWin], sfWorld); return; }
        if (Button({c.x - 110, c.y + 278, 220, 30}, "The editor", true, 13)) { StartScuffleEditor(g); return; }
        if (Button({40, 538, 244, 30}, "Training (learn the moves)", true, 13)) { StartScuffleTraining(g); return; }
        if (Button({40, 574, 244, 30}, TextFormat("The locker (%d tokens)", sf::MyLocker().tokens), true, 13)) { gSfLocker = true; DebugScuffleLocker(0); return; }
        DrawTextCentered("Host or Join to fight friends (2-8; the host picks the rounds and the arsenal in the lobby)", c.x, c.y + 316, 13, SCREEN_DIM);
    }
    if (selGame == G_WARP) {   // solo: your team and theirs filled with bots
        static int wdSkill = 1; int wdSize = gWarpFill - 1;
        static const char* SKILL[3] = {"Rookie bots", "League bots", "Pro bots"};
        static const char* ARENA[2] = {"the Classic court", "the Extreme arena"};
        auto row = [&](float y, const char* text, int& v, int n, int lo) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y - 10, TextFormat("%d v %d", wdSize + 1, wdSize + 1), wdSize, 6, 0);
        row(c.y + 16, SKILL[wdSkill], wdSkill, 3, 0);
        row(c.y + 42, ARENA[gWarpArena], gWarpArena, 2, 0); gWarpFill = wdSize + 1;
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Play (solo)", true, 15)) { StartWarp(g, wdSize + 1, wdSkill, gWarpArena); return; }
        DrawTextCentered("Host or Join for friends (the sides above fill up with bots)", c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_FATHOMS) {   // solo: you against AI captains (Host or Join for friends: up to six, AI in the empty seats)
        static int faPlayers = 2, faFac = 0, faAi = 1, faMap = 0, faWin = 0, faCap = 1, faStart = 0, faTeams = 0;
        static const char* TEAMS[4] = {"free for all", "teams of two", "teams of three", "three teams of two"};
        static const char* FAC[7] = {"Nautilus Crew", "Islanders", "Crustacean Brood", "Merfolk of the Weeds", "Atlantean Lost Ones", "Clockwork Foundry", "a random faction"};
        static const char* AIL[4] = {"Deckhand rivals", "Mate rivals", "Captain rivals", "Admiral rivals"};
        static const char* MAP[4] = {"an archipelago", "twin continents", "a ring of isles", "scattered isles"};
        static const char* WIN[3] = {"every victory", "conquest only", "score only"};
        static const int CAP[4] = {20, 30, 45, 0};
        static const char* START[3] = {"standard stores", "high stores", "very high stores"};
        auto row = [&](float y, const char* text, int& v, int n, int lo) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 18, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y - 14, TextFormat("%d players", faPlayers), faPlayers, 5, 2);
        row(c.y + 12, TextFormat("You: %s", FAC[faFac]), faFac, 7, 0);
        row(c.y + 38, AIL[faAi], faAi, 4, 0);
        row(c.y + 64, MAP[faMap], faMap, 4, 0);
        row(c.y + 90, WIN[faWin], faWin, 3, 0);
        row(c.y + 116, CAP[faCap] ? TextFormat("%d-minute cap", CAP[faCap]) : "no time cap", faCap, 4, 0);
        row(c.y + 142, START[faStart], faStart, 3, 0);
        row(c.y + 168, TEAMS[faTeams], faTeams, 4, 0);
        if (faTeams == 1 && faPlayers % 2) faPlayers++; if (faTeams == 2) faPlayers = 6; if (faTeams == 3) faPlayers = 6; faPlayers = std::min(faPlayers, 6);
        fa::Settings& st = gFaSettings; st.players = faPlayers; st.mapType = faMap; st.victory = faWin == 0 ? 7 : faWin == 1 ? 1 : 0; st.timeCap = CAP[faCap] * 60; st.start = faStart;
        for (int k = 0; k < fa::MAX_PLAYERS; k++) { st.faction[k] = k == 0 ? (faFac == 6 ? -1 : faFac) : -1; st.aiLevelOf[k] = faAi; st.ai[k] = k > 0; st.team[k] = faTeams == 0 ? k : faTeams == 3 ? k % 3 : k % 2; }
        st.teams = faTeams;
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Set sail (solo)", true, 15)) { fa::Settings s = st; s.seed = (uint32_t)GetRandomValue(1, 1 << 30); uint32_t h = s.seed; for (int k = 0; k < fa::MAX_PLAYERS; k++) if (s.faction[k] < 0) { h = h * 1103515245u + 12345u; s.faction[k] = (int)((h >> 16) % 6); } StartFathoms(g, s); return; }
        { static int known = -1; if (known < 0) { FILE* f = std::fopen("fathoms_autosave.bin", "rb"); known = f ? 1 : 0; if (f) std::fclose(f); }
          if (known == 1 && Button({c.x + 120, c.y + 236, 150, 36}, "Resume save", true, 15)) { if (ResumeFathoms(g)) return; known = 0; }
          if (known == 1 && Button({c.x - 270, c.y + 236, 150, 36}, gFaResume ? "Host: the save" : "Host: a new map", true, 13)) gFaResume = !gFaResume; }
        DrawTextCentered("Host or Join for friends (AI captains take the empty seats)", c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_DEEP) {   // The Deep is its own program (Unity): Depth starts it and waits, minimised, until it closes
        static std::string deepMsg;
        static bool wasRunning = false;
        bool running = DeepRunning();
        if (wasRunning && !running) RestoreWindow();   // (back from The Deep)
        wasRunning = running;
        if (Button({c.x - 110, c.y + 236, 220, 36}, running ? "The Deep is running" : "Dive (solo)", !running, 15)) {
            std::string err;
            if (LaunchDeep("solo", "", gProfile.name, &err, 0, DEEP_PORT, gDeepNewGame)) { deepMsg = ""; MinimizeWindow(); wasRunning = true; gDeepNewGame = false; } else deepMsg = err;
        }
        // the campaign: continue the saved one (the host's, for a crew), or begin again
        if (!running && DeepSaveExists()) {
            if (Button({c.x + 120, c.y + 236, 190, 36}, gDeepNewGame ? "New campaign (click: continue)" : "Continue the campaign", true, 13)) gDeepNewGame = !gDeepNewGame;
        }
        DrawTextCentered(deepMsg.empty() ? "Host or Join above for a crew of up to four: The Deep opens on every PC" : deepMsg.c_str(), c.x, c.y + 280, 13, deepMsg.empty() ? SCREEN_DIM : Color{255, 170, 140, 255});
    }
    if (selGame == G_BALLPIT) {   // solo: you and the bots (Host or Join above for friends: up to twelve)
        static int bpSkill = 1;
        static const char* SKILL[3] = {"Toddler bots", "Kid bots", "Teen bots"};
        auto row = [&](float y, const char* text, int& v, int n, int lo) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y - 10, bp::ModeName(gBallPitMode), gBallPitMode, bp::MD_COUNT, 0);
        int maxP = gBallPitMode == bp::MD_BOMB ? 10 : 12; gBallPitFill = std::clamp(gBallPitFill, 2, maxP);
        row(c.y + 16, TextFormat("%d players", gBallPitFill), gBallPitFill, maxP - 1, 2);
        row(c.y + 42, SKILL[bpSkill], bpSkill, 3, 0);
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Play (solo)", true, 15)) { StartBallPit(g, gBallPitMode, gBallPitFill, bpSkill); return; }
        DrawTextCentered("Host or Join for friends (bots fill the rest of the players above)", c.x, c.y + 280, 13, SCREEN_DIM);
    }    if (selGame == G_NOCLIP) {   // solo: you and bot salvagers; Host for friends (proximity voice)
        static int ncBots = 2;
        Rectangle l{c.x - 190, c.y - 10, 30, 26}, r{c.x + 160, c.y - 10, 30, 26};
        DrawTextCenteredBold(TextFormat("%d bot salvager%s", ncBots, ncBots == 1 ? "" : "s"), c.x, c.y - 8, 19, Color{230, 200, 150, 255});
        DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { ncBots = (ncBots + 5) % 6; PlayCue("ui.click"); }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { ncBots = (ncBots + 1) % 6; PlayCue("ui.click"); }
        static const char* MODES[7] = {"The Bureau (campaign)", "Lost", "Noclip Roulette", "Lights Out", "Expedition", "Lonely", "Skin-Stealer (3+ players)"};
        { Rectangle l2{c.x - 190, c.y + 16, 30, 26}, r2{c.x + 160, c.y + 16, 30, 26}; DrawTextCenteredBold(MODES[gNcMode], c.x, c.y + 18, 19, Color{230, 200, 150, 255}); DrawTextCenteredBold("<", l2.x + 15, l2.y, 22, Pal::Brass); DrawTextCenteredBold(">", r2.x + 15, r2.y, 22, Pal::Brass);
          if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l2)) { gNcMode = (gNcMode + 6) % 7; PlayCue("ui.click"); } if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r2)) { gNcMode = (gNcMode + 1) % 7; PlayCue("ui.click"); } }
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Clock in (solo)", gNcMode != 6, 15)) { StartNoclip(g, gNcMode == 5 ? 0 : ncBots, gNcMode); return; }
        if (Button({40, 574, 244, 30}, TextFormat("The locker (%d tokens)", NoclipTokens()), true, 13)) { gNcLocker = true; return; }
        DrawTextCentered(TextFormat("Week one's quota: 600. Arcade tokens from the Bureau: %d", NoclipTokens()), c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_FOWL) {   // solo: your stall and up to five bots; Host for friends
        static int fpBots = 5, fpSkill = 1;
        static const char* SKILL[3] = {"Weekend shooters", "Club regulars", "Crack shots"};
        auto row = [&](float y, const char* text, int& v, int n, int lo) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = lo + (v - lo + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = lo + (v - lo + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y - 10, fp::D().modes[std::clamp(gFowlMode, 0, (int)fp::D().modes.size() - 1)].name.c_str(), gFowlMode, (int)fp::D().modes.size(), 0);
        row(c.y + 16, TextFormat("%d bot%s", fpBots, fpBots == 1 ? "" : "s"), fpBots, 6, 0);
        row(c.y + 42, SKILL[fpSkill], fpSkill, 3, 0);
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Take a stall (solo)", true, 15)) { StartFowl(g, gFowlMode, fpBots, fpSkill); return; }
        DrawTextCentered(fp::D().modes[std::clamp(gFowlMode, 0, (int)fp::D().modes.size() - 1)].rule.c_str(), c.x, c.y + 280, 13, SCREEN_DIM);
        if (Button({40, 574, 244, 30}, TextFormat("The locker (%d tokens)", FowlTokens()), true, 13)) { gFpLocker = true; return; }
    }
    if (selGame == G_NIGHT_OFF) {   // solo: one sailor, the bar, the night (the modes that make sense alone; Host above for friends)
        static const char* CREW[6] = {"the Diver", "the Whaler", "the Stowaway", "the Mechanic", "the Captain", "the Nurse"};
        static const int SOLO_MODES[4] = {no::MD_NIGHT_OFF, no::MD_SOLO, no::MD_SOBER, no::MD_WAGER};
        static const char* CROWD[3] = {"a dead night", "a normal night", "a packed night"};
        static int soloMode = 0, soloCrowd = 1;
        auto row = [&](float y, const char* text, int& v, int n) {
            Rectangle l{c.x - 190, y, 30, 26}, r{c.x + 160, y, 30, 26};
            DrawTextCenteredBold(text, c.x, y + 2, 19, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 15, l.y, 22, Pal::Brass); DrawTextCenteredBold(">", r.x + 15, r.y, 22, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; PlayCue("ui.click"); }
        };
        row(c.y + 44, TextFormat("ashore as %s", CREW[gNoCrew]), gNoCrew, 6);
        row(c.y + 70, TextFormat("solo: %s", no::ModeName(SOLO_MODES[soloMode])), soloMode, 4);
        row(c.y - 10, no::BarName(gNoBar), gNoBar, no::BAR_COUNT);
        if (gNoSeason < 0) gNoSeason = no::SeasonToday();
        row(c.y + 16, gNoSeason ? TextFormat("%s %s", no::SeasonName(gNoSeason), no::SeasonWhereText(gNoSeason).c_str()) : "an ordinary night", gNoSeason, no::SeasonCount() + 1);
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Go ashore (solo)", true, 15)) { StartNightOff(g, gNoCrew, SOLO_MODES[soloMode], soloCrowd, gNoBar, gNoSeason); return; }
        if (Button({c.x - 300, c.y + 236, 170, 36}, "The cloakroom", true, 14)) { gNoCloak = true; return; }   // (skins and the token spin)
        if (Button({c.x + 120, c.y + 241, 120, 26}, CROWD[soloCrowd], true, 12)) soloCrowd = (soloCrowd + 1) % 3;
        DrawTextCentered(no::ModeRule(SOLO_MODES[soloMode]), c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_MOUTHFUL) {   // solo: you and the bots on the reef (stage 3 brings friends)
        static int mfBots = 11, mfLen = 1, mfLevel = 0;
        static const float LENS[3] = {10, 15, 20};
        static const char* LEVELS[4] = {"a mix of bots", "Minnow bots", "Hunter bots", "Shark bots"};
        auto picker = [&](float x, float y, const char* text, int& v, int lo, int hi) {
            Rectangle l{x - 120, y, 26, 26}, r{x + 94, y, 26, 26};
            DrawTextCenteredBold(text, x, y + 2, 18, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 13, l.y, 22, v > lo ? Pal::Brass : Pal::BrassDk);
            DrawTextCenteredBold(">", r.x + 13, r.y, 22, v < hi ? Pal::Brass : Pal::BrassDk);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l) && v > lo) { v--; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r) && v < hi) { v++; PlayCue("ui.click"); }
        };
        picker(c.x - 130, c.y + 52, TextFormat("%d bots", mfBots), mfBots, 0, 11);
        picker(c.x + 130, c.y + 52, TextFormat("%.0f minutes", LENS[mfLen]), mfLen, 0, 2);
        // the mode (and One Path's path), on a plate to the left of the drum
        DrawRectangleRounded({28, 236, 268, 200}, 0.08f, 6, Fade(Color{8, 30, 34, 255}, 0.85f));
        DrawRectangleRoundedLinesEx({28, 236, 268, 200}, 0.08f, 6, 2, Pal::BrassDk);
        DrawTextCenteredBold("The mode", 162, 246, 18, Color{230, 200, 150, 255});
        auto cyc = [&](float y, int& v, int n, const char* text) {
            Rectangle l{40, y, 24, 22}, r{260, y, 24, 22};
            DrawTextCenteredBold(text, 162, y + 1, 16, Color{180, 230, 220, 255});
            DrawTextCenteredBold("<", l.x + 12, l.y, 18, Pal::Brass); DrawTextCenteredBold(">", r.x + 12, r.y, 18, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; PlayCue("ui.click"); }
        };
        cyc(276, gMfMode, mf::M_COUNT, mf::ModeName(gMfMode));
        if (gMfMode == mf::M_ONE_PATH) { int pv = gMfPath; cyc(306, pv, mf::P_COUNT, mf::PathName(pv)); gMfPath = pv; }
        DrawWrapped(mf::ModeRule(gMfMode), {40, 336, 244, 60}, 13, SCREEN_DIM);
        {   // (the bots' level on the mode plate, below the mode)
            Rectangle l{40, 400, 24, 22}, r{260, 400, 24, 22};
            DrawTextCenteredBold(LEVELS[mfLevel], 162, 401, 15, Color{230, 200, 150, 255});
            DrawTextCenteredBold("<", l.x + 12, l.y, 18, Pal::Brass); DrawTextCenteredBold(">", r.x + 12, r.y, 18, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { mfLevel = (mfLevel + 3) % 4; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { mfLevel = (mfLevel + 1) % 4; PlayCue("ui.click"); }
        }
        if (Button({c.x - 110, c.y + 236, 220, 36}, "Swim (solo)", true, 15)) { StartMouthfulMode(g, mfBots, LENS[mfLen], mfLevel, gMfMode, gMfPath); return; }
        if (Button({c.x + 120, c.y + 236, 170, 36}, TextFormat("Wardrobe (%d)", mf::MyWardrobe().tokens), true, 14)) { gMfWardrobe = true; return; }
        DrawTextCentered("Mouse steers, W swims, Shift boosts, left click bites, right click is your form's ability", c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (selGame == G_RED_TIDE) {
        const char* const* RT_MAPS = RT_MAP_KEYS;
        int& rtMap = gRtMapSel;
        const int RT_N = 5;
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
        {   // the mode under the map
            Rectangle ml{c.x - 150, c.y + 80, 24, 22}, mr{c.x + 126, c.y + 80, 24, 22};
            int nm = RedTideModeCount();
            DrawTextCenteredBold(RedTideModeName(gRtModeSel), c.x, c.y + 81, 16, Color{180, 230, 220, 255});
            DrawTextCenteredBold("<", ml.x + 12, ml.y, 18, Pal::Brass);
            DrawTextCenteredBold(">", mr.x + 12, mr.y, 18, Pal::Brass);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), ml)) { gRtModeSel = (gRtModeSel + nm - 1) % nm; PlayCue("ui.click"); }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), mr)) { gRtModeSel = (gRtModeSel + 1) % nm; PlayCue("ui.click"); }
            if (CheckCollisionPointRec(GetMousePosition(), {c.x - 150, c.y + 78, 300, 26})) DrawWrapped(RedTideModeRules(gRtModeSel), {c.x - 200, c.y + 128, 400, 60}, 14, SCREEN_INK);
        }
        if (int ns = RedTideSeasonCount(); ns > 0) {   // the species season (a data drop of new species)
            Rectangle sl{c.x - 150, c.y + 104, 24, 22}, sr{c.x + 126, c.y + 104, 24, 22};
            DrawTextCentered(gRtSeasonPick ? ("Season " + std::to_string(gRtSeasonPick) + ": " + RedTideSeasonName(gRtSeasonPick)).c_str() : "No season", c.x, c.y + 106, 14, Color{170, 210, 200, 255});
            DrawTextCenteredBold("<", sl.x + 12, sl.y, 16, Pal::BrassDk); DrawTextCenteredBold(">", sr.x + 12, sr.y, 16, Pal::BrassDk);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), sl)) gRtSeasonPick = (gRtSeasonPick + ns) % (ns + 1);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), sr)) gRtSeasonPick = (gRtSeasonPick + 1) % (ns + 1);
        }
        int lnTide = 0; float lnTime = 0;
        bool longNight = std::string(RedTideModeKey(gRtModeSel)) == "longnight" && RedTideLongNightSaved(RT_MAPS[rtMap], &lnTide, &lnTime);
        if (longNight) {   // (a Long Night kept on this machine: go on with it, or begin a new one)
            if (Button({c.x - 110, c.y + 236, 220, 36}, TextFormat("Resume: tide %d, %d:%02d in", lnTide, (int)lnTime / 3600, ((int)lnTime / 60) % 60), true, 15)) { SetRedTideMode(gRtModeSel); SetRedTideSeason(gRtSeasonPick); SetRedTideResume(true); StartRedTide(g, RT_MAPS[rtMap]); return; }
            if (Button({c.x - 90, c.y + 196, 180, 30}, "Begin a new night", true, 13)) { RedTideClearLongNight(RT_MAPS[rtMap]); SetRedTideMode(gRtModeSel); SetRedTideSeason(gRtSeasonPick); StartRedTide(g, RT_MAPS[rtMap]); return; }
        } else if (Button({c.x - 110, c.y + 236, 220, 36}, "Dive (solo)", true, 15)) { SetRedTideMode(gRtModeSel); SetRedTideSeason(gRtSeasonPick); StartRedTide(g, RT_MAPS[rtMap]); return; }
        if (std::string(RedTideModeKey(gRtModeSel)) == "custom" && Button({c.x - 90, c.y + 196, 180, 30}, "Custom rules...", true, 13)) gRtCustomOpen = true;
        DrawTextCentered("Host or Join to dive with up to three friends (the host's map)", c.x, c.y + 280, 13, SCREEN_DIM);
    }
    if (ready && selGame == G_SCUTTLE && Button({c.x - 110, c.y + 236, 220, 36}, "Practice with AI crabs", true, 15)) {
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
    DrawWrapped("Type the host's 6-letter code (same network), or their address. Over ZeroTier, type the host's ZeroTier address (10.x.x.x): no port forwarding needed. Over the open internet the host must forward UDP port 47778.",
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
        DrawWrapped("On this network (or the same ZeroTier network) friends Browse, type the code or type your address. Over the open internet: your public address, with UDP 47778 forwarded.", {p.x + 250, p.y + 70, p.width - 280, 34}, 13, Fade(SCREEN_DIM, 0.8f));
    }
    float y = p.y + 110;
    int maxP = Info(gSess.game).maxPlayers;
    const float rowH = maxP > 6 ? 34 : 52, rowStep = maxP > 6 ? 38 : 60;   // (eight seats: slimmer rows)
    for (int i = 0, shown = 0; i < MAX_PLAYERS && shown < maxP; i++) {
        const SeatInfo& s = gSess.seats[i];
        if (!s.used) continue;
        shown++;
        Rectangle row{p.x + 30, y, p.width - 60, rowH};
        DrawRectangleRounded(row, 0.2f, 6, i == gSess.mySeat ? Color{24, 96, 96, 255} : Color{16, 70, 74, 255});
        bool slim = rowH < 40;
        DrawCrab({row.x + 32, row.y + rowH * 0.58f}, CRAB[std::min(shown - 1, 3)], (float)GetTime(), 0, false, slim ? 0.55f : 0.8f);
        TxtBold(s.name + (s.host ? "  (host)" : s.ai ? "  (AI)" : ""), row.x + 66, row.y + (slim ? 7 : 8), slim ? 18 : 20, SCREEN_INK);
        Txt(s.lost ? "lost connection" : s.ready ? "ready" : "not ready", slim ? row.x + 300 : row.x + 66, row.y + (slim ? 9 : 30), 15, s.lost ? Pal::Coral : s.ready ? Pal::Good : SCREEN_DIM);
        if (!s.ai && !s.host && s.ping) Txt(TextFormat("%d ms", s.ping), row.x + row.width - 220, row.y + rowH / 2 - 9, 15, SCREEN_DIM);
        if (i == gSess.mySeat && BetWallet(gSess.game) >= 0 && gSess.stage == S_LOBBY) DrawBetControls(row);
        else if (s.bet > 0 && s.betOn >= 0 && gSess.seats[s.betOn].used) Txt(TextFormat("bets %d on %s", s.bet, gSess.seats[s.betOn].name.c_str()), row.x + 270, row.y + 30, 14, Pal::Brass);
        if (host && i != 0 && Button({row.x + row.width - 120, row.y + (rowH - 36) / 2, 108, 36}, s.ai ? "Remove" : "Give away", true, 15)) gSess.RemoveSeat(i);
        y += rowStep;
    }
    int used = 0; for (auto& s : gSess.seats) used += s.used;
    for (int k = used; k < maxP; k++) {
        Rectangle row{p.x + 30, y, p.width - 60, rowH};
        DrawRectangleRoundedLinesEx(row, 0.2f, 6, 1.5f, Fade(SCREEN_DIM, 0.4f));
        Txt("an empty seat", row.x + 66, row.y + rowH / 2 - 9, 16, Fade(SCREEN_DIM, 0.6f));
        if (host && k == used && Button({row.x + row.width - 120, row.y + (rowH - 36) / 2, 108, 36}, "Add AI", true, 15)) gSess.AddAI();
        y += rowStep;
    }
    if (gSess.game == G_RED_TIDE && host) {
        // the host picks the water (the guests hear it in the chat)
        int before = gRtMapSel;
        Rectangle l{p.x + 30, p.y + p.height - 110, 30, 30}, r{p.x + 330, p.y + p.height - 110, 30, 30};
        DrawTextCenteredBold("<", l.x + 15, l.y + 2, 22, Pal::Brass);
        DrawTextCenteredBold(">", r.x + 15, r.y + 2, 22, Pal::Brass);
        DrawTextCenteredBold(RT_TITLES[gRtMapSel], p.x + 195, p.y + p.height - 106, 19, Color{230, 200, 150, 255});
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) gRtMapSel = (gRtMapSel + 4) % 5;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) gRtMapSel = (gRtMapSel + 1) % 5;
        // and the mode
        int beforeMode = gRtModeSel, nm = RedTideModeCount();
        Rectangle ml{p.x + 380, p.y + p.height - 110, 30, 30}, mr{p.x + 600, p.y + p.height - 110, 30, 30};
        DrawTextCenteredBold("<", ml.x + 15, ml.y + 2, 22, Pal::Brass);
        DrawTextCenteredBold(">", mr.x + 15, mr.y + 2, 22, Pal::Brass);
        DrawTextCenteredBold(RedTideModeName(gRtModeSel), p.x + 505, p.y + p.height - 106, 19, Color{180, 230, 220, 255});
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), ml)) gRtModeSel = (gRtModeSel + nm - 1) % nm;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), mr)) gRtModeSel = (gRtModeSel + 1) % nm;
        gSess.gameOpts = RtOpts();
        int beforeSeason = gRtSeasonPick, ns = RedTideSeasonCount();
        if (ns > 0) {
            Rectangle sl{p.x + 380, p.y + p.height - 140, 26, 26}, sr{p.x + 600, p.y + p.height - 140, 26, 26};
            DrawTextCenteredBold("<", sl.x + 13, sl.y + 2, 18, Pal::BrassDk); DrawTextCenteredBold(">", sr.x + 13, sr.y + 2, 18, Pal::BrassDk);
            DrawTextCentered(gRtSeasonPick ? ("Season: " + RedTideSeasonName(gRtSeasonPick)).c_str() : "No season", p.x + 505, p.y + p.height - 136, 15, Color{170, 210, 200, 255});
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), sl)) gRtSeasonPick = (gRtSeasonPick + ns) % (ns + 1);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), sr)) gRtSeasonPick = (gRtSeasonPick + 1) % (ns + 1);
            gSess.gameOpts = RtOpts();
        }
        if (beforeSeason != gRtSeasonPick && gRtSeasonPick) gSess.Chat("Season " + std::to_string(gRtSeasonPick) + ": " + RedTideSeasonName(gRtSeasonPick));
        if (std::string(RedTideModeKey(gRtModeSel)) == "custom" && Button({p.x + 650, p.y + p.height - 112, 150, 26}, "Custom rules...", true, 13)) gRtCustomOpen = true;
        int lnTide = 0; float lnTime = 0;
        if (std::string(RedTideModeKey(gRtModeSel)) == "longnight" && RedTideLongNightSaved(RT_MAP_KEYS[gRtMapSel], &lnTide, &lnTime)) {
            // the host's Long Night on this map goes on when the dive starts, unless they begin a new one
            DrawTextCentered(TextFormat("The Long Night goes on: tide %d, %d:%02d in", lnTide, (int)lnTime / 3600, ((int)lnTime / 60) % 60), p.x + 505, p.y + p.height - 166, 15, Color{150, 250, 210, 255});
            if (Button({p.x + 650, p.y + p.height - 172, 150, 26}, "Begin a new night", true, 13)) { RedTideClearLongNight(RT_MAP_KEYS[gRtMapSel]); gSess.Chat("A new Long Night"); }
        }
        if (before != gRtMapSel || beforeMode != gRtModeSel)
            gSess.Chat(std::string("We dive ") + RT_TITLES[gRtMapSel] + (gRtModeSel ? std::string(": ") + RedTideModeName(gRtModeSel) + " - " + RedTideModeRules(gRtModeSel) : std::string()));
    }
    if (gSess.game == G_FLIGHT && host) {
        // the host picks the map and the match's length (design doc p27: 20, 30 or 45 minutes)
        auto pick = [&](float x, float y, const char* text, int& v, int n) {
            Rectangle l{x - 150, y, 26, 26}, r{x + 124, y, 26, 26};
            DrawTextCenteredBold("<", l.x + 13, l.y + 2, 20, Pal::Brass); DrawTextCenteredBold(">", r.x + 13, r.y + 2, 20, Pal::Brass);
            DrawTextCenteredBold(text, x, y + 3, 17, Color{230, 200, 150, 255});
            bool ch = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; ch = true; }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; ch = true; }
            return ch;
        };
        const auto& L = fl::MatchLengths();
        bool ch = pick(p.x + 195, p.y + p.height - 140, FlightIsleTypeName(gFlIsle), gFlIsle, 10);
        ch |= pick(p.x + 195, p.y + p.height - 108, FlightArrangementName(gFlArr), gFlArr, 4);
        ch |= pick(p.x + 505, p.y + p.height - 140, FlLengthName(gFlMinutes).c_str(), gFlMinutes, FlLengthCount());
        DrawTextCentered(TextFormat("your founder: %s", FlightFounderName(gFlSel)), p.x + 505, p.y + p.height - 104, 15, Color{180, 230, 220, 255});
        gSess.gameOpts = FlOpts();
        if (ch) gSess.Chat(std::string("The map: ") + FlightIsleTypeName(gFlIsle) + " homes, " + FlightArrangementName(gFlArr) + ", " + FlLengthName(gFlMinutes));
        (void)L;
    } else if (gSess.game == G_FLIGHT) DrawTextCentered(TextFormat("your founder: %s (pick it on the reel)", FlightFounderName(gFlSel)), p.x + 350, p.y + p.height - 104, 15, Color{180, 230, 220, 255});
    if (gSess.game == G_MOUTHFUL && host) {
        // the host picks the round's length (10, 15 or 20 minutes) and the bots that fill the water to twelve (doc p. 2)
        static int mfLen = 1, mfLevel = 0;
        static const int LENS[3] = {10, 15, 20};
        static const char* LEVELS[4] = {"a mix of bots", "Minnow bots", "Hunter bots", "Shark bots"};
        auto pick = [&](float x, float y, const char* text, int& v, int n) {
            Rectangle l{x - 150, y, 26, 26}, r{x + 124, y, 26, 26};
            DrawTextCenteredBold("<", l.x + 13, l.y + 2, 20, Pal::Brass); DrawTextCenteredBold(">", r.x + 13, r.y + 2, 20, Pal::Brass);
            DrawTextCenteredBold(text, x, y + 3, 17, Color{230, 200, 150, 255});
            bool ch = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; ch = true; }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; ch = true; }
            return ch;
        };
        bool ch = pick(p.x + 195, p.y + p.height - 140, TextFormat("%d-minute round", LENS[mfLen]), mfLen, 3);
        ch |= pick(p.x + 505, p.y + p.height - 140, LEVELS[mfLevel], mfLevel, 4);
        ch |= pick(p.x + 195, p.y + p.height - 108, mf::ModeName(gMfMode), gMfMode, mf::M_COUNT);
        if (gMfMode == mf::M_ONE_PATH) ch |= pick(p.x + 505, p.y + p.height - 108, mf::PathName(gMfPath), gMfPath, mf::P_COUNT);
        gSess.gameOpts = MouthfulOpts(LENS[mfLen], mfLevel, 12, gMfMode, gMfPath);
        if (ch) gSess.Chat(TextFormat("The round: %s, %d minutes, %s filling the water to twelve", mf::ModeName(gMfMode), LENS[mfLen], LEVELS[mfLevel]));
    }
    if (gSess.game == G_TRAWL) {   // each hand picks their own view (the playtest: guests only ever got top-down)
        float yy = p.y + p.height - 98;
        DrawTextCentered("Your view aboard (V switches at sea)", p.x + 250, yy - 2, 14, SCREEN_DIM);
        if (Button({p.x + 40, yy + 18, 200, 36}, gTrawlFp ? "Top-down" : "> Top-down <", gTrawlFp, 15)) { gTrawlFp = false; PlayCue("ui.click"); }
        if (Button({p.x + 260, yy + 18, 200, 36}, gTrawlFp ? "> First person <" : "First person", !gTrawlFp, 15)) { gTrawlFp = true; PlayCue("ui.click"); }
    }
    if (gSess.game == G_SCUFFLE && host) {
        // the host picks the rounds to win, the arsenal (doc p. 6) and how sharp the AI seats fight
        static int sfWin = 1, sfArs = 0, sfSkill = 2, sfWorld = 0;   // (sfWorld: 0 all six, 1-6 one world, 7 endless)
        static const int WINS[3] = {3, 5, 10};
        static const char* SKILLS[3] = {"Stumble AI", "Scrap AI", "Sharp AI"};
        auto pick = [&](float x, float y, const char* text, int& v, int n) {
            Rectangle l{x - 108, y, 24, 24}, r{x + 84, y, 24, 24};   // (two narrow columns: the Fight! button has the right)
            DrawTextCenteredBold("<", l.x + 12, l.y + 2, 18, Pal::Brass); DrawTextCenteredBold(">", r.x + 12, r.y + 2, 18, Pal::Brass);
            DrawTextCenteredBold(text, x, y + 4, 14, Color{230, 200, 150, 255});
            bool ch = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; ch = true; }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; ch = true; }
            return ch;
        };
        float yy = p.y + p.height - 98;
        bool ch = pick(p.x + 140, yy, TextFormat("first to %d", WINS[sfWin]), sfWin, 3);
        ch |= pick(p.x + 140, yy + 30, TextFormat("%s arsenal", sf::ArsenalName(sfArs)), sfArs, sf::AR_COUNT);
        ch |= pick(p.x + 140, yy + 60, ScuffleRulesChoice(gScuffleRules), gScuffleRules, sf::MU_COUNT + 2);
        ch |= pick(p.x + 362, yy, SKILLS[sfSkill], sfSkill, 3);
        ch |= pick(p.x + 362, yy + 30, gScuffleMode == sf::MD_BOSS ? (sfWorld < 1 || sfWorld > sf::WD_COUNT ? "all six bosses" : sf::BossName(sf::BossOfWorld(sfWorld - 1))) : ScuffleWorldChoice(sfWorld - 1), sfWorld, 8);
        ch |= pick(p.x + 362, yy + 60, sf::ModeName(gScuffleMode), gScuffleMode, sf::MD_COUNT);
        gSess.gameOpts = ScuffleOpts(WINS[sfWin], sfArs, sfSkill, sfWorld - 1, ScuffleRulesMask(gScuffleRules), gScuffleRules == 1);
        if (ch) gSess.Chat(TextFormat("The fight: %s, first to %d, the %s arsenal, %s", sf::ModeName(gScuffleMode), WINS[sfWin], sf::ArsenalName(sfArs), ScuffleWorldChoice(sfWorld - 1)));
    }
    if (gSess.game == G_NIGHT_OFF) {
        // the host picks the mode (doc p. 24), the crowd (Dead, Normal, Packed, Random) and whether sailors may fight each other;
        // everyone picks who they go ashore as
        static const char* CROWD[4] = {"a dead night", "a normal night", "a packed night", "a random crowd"};
        static const char* CREWN[6] = {"the Diver", "the Whaler", "the Stowaway", "the Mechanic", "the Captain", "the Nurse"};
        auto pick = [&](float x, float y, const char* text, int& v, int n, bool on) {
            Rectangle l{x - 150, y, 26, 26}, r{x + 124, y, 26, 26};
            if (on) { DrawTextCenteredBold("<", l.x + 13, l.y + 2, 20, Pal::Brass); DrawTextCenteredBold(">", r.x + 13, r.y + 2, 20, Pal::Brass); }
            DrawTextCenteredBold(text, x, y + 3, 17, Color{230, 200, 150, 255});
            bool ch = false;
            if (on && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), l)) { v = (v + n - 1) % n; ch = true; }
            if (on && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r)) { v = (v + 1) % n; ch = true; }
            return ch;
        };
        bool ch = false;
        if (host) {
            ch |= pick(p.x + 195, p.y + p.height - 140, no::ModeName(gNoMode), gNoMode, no::MD_SOLO, true);
            ch |= pick(p.x + 505, p.y + p.height - 140, CROWD[gNoCrowd], gNoCrowd, 4, true);
            int pv = gNoPvp ? 1 : 0; ch |= pick(p.x + 195, p.y + p.height - 108, pv ? "sailors may fight sailors" : "no fights between sailors", pv, 2, true); gNoPvp = pv != 0;
            ch |= pick(p.x + 195, p.y + p.height - 172, no::BarName(gNoBar), gNoBar, no::BAR_COUNT, true);   // (the two bars, doc p. 30)
            if (gNoSeason < 0) gNoSeason = no::SeasonToday();   // (the calendar's season; the host may force another, or none)
            ch |= pick(p.x + 505, p.y + p.height - 172, gNoSeason ? TextFormat("%s %s", no::SeasonName(gNoSeason), no::SeasonWhereText(gNoSeason).c_str()) : "an ordinary night", gNoSeason, no::SeasonCount() + 1, true);
            gSess.gameOpts = NightOffOpts(gNoMode, gNoCrowd, gNoPvp, gNoBar, gNoSeason);
            if (ch) gSess.Chat(TextFormat("Tonight: %s at %s, %s%s", no::ModeName(gNoMode), no::BarName(gNoBar), CROWD[gNoCrowd], gNoPvp ? "" : ", no fights between sailors"));
        }
        pick(p.x + 505, p.y + p.height - 108, TextFormat("ashore as %s", CREWN[gNoCrew]), gNoCrew, 6, true);
        DrawTextCentered(no::ModeRule(gNoMode), p.x + p.width / 2, p.y + p.height - 76, 13, Color{200, 190, 170, 255});
    }
    if (host) {
        std::string why;
        bool can = gSess.CanLaunch(&why);
        const char* go = gSess.game == G_RED_TIDE ? "Dive" : gSess.game == G_TRAWL ? "Cast off" : gSess.game == G_FLIGHT ? "Take wing" : gSess.game == G_MOUTHFUL ? "Into the water" : gSess.game == G_NIGHT_OFF ? "Go ashore" : gSess.game == G_SCUFFLE ? "Fight!" : gSess.game == G_WARP ? "Play ball" : gSess.game == G_FOWL ? "Open the gates" : gSess.game == G_NOCLIP ? "Through the portal" : gSess.game == G_BALLPIT ? "Into the pit" : gSess.game == G_FATHOMS ? "Set sail" : gSess.game == G_DEEP ? "Dive together" : "Start the race";
        if (Button({p.x + p.width - 250, p.y + p.height - 66, 220, 50}, go, can, 20)) { std::string w2; gSess.Launch(&w2); }
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
// The Deep: once the host starts, every PC opens the Unity game (the host's as host, the others joining the host's
// address on The Deep's own port) and waits, minimised, until it closes; then back to the reels.
void DrawDeepHandoff() {
    static int launchedVer = -1; static bool running = false; static std::string msg;
    Rectangle p{340, 230, 600, 240};
    DrawScreenPanel(p);
    DrawTextCenteredBold("THE DEEP", p.x + p.width / 2, p.y + 26, 26, SCREEN_INK);
    if (launchedVer != gSess.stateVersion && !gSess.Snapshot().empty() && !running) {
        Reader r(gSess.Snapshot());
        r.U8(); r.U32(); int seat = (int)r.U8(); int crew = (int)r.U8();
        bool host = gSess.role == R_HOST;
        std::string addr;
        if (!host) { addr = gSess.hostAddr; size_t colon = addr.rfind(':'); if (colon != std::string::npos) addr = addr.substr(0, colon); }
        std::string err;
        if (LaunchDeep(host ? "host" : "join", addr, gProfile.name, &err, seat, DEEP_PORT, host && gDeepNewGame)) { running = true; msg = TextFormat("Diving with a crew of %d (seat %d)", crew, seat + 1); MinimizeWindow(); }
        else msg = err;
        launchedVer = gSess.stateVersion;
    }
    if (running && !DeepRunning()) { running = false; RestoreWindow(); msg = "Back from the deep."; }
    DrawTextCentered(msg.c_str(), p.x + p.width / 2, p.y + 86, 17, SCREEN_DIM);
    DrawTextCentered(running ? "The Deep is open on every PC in the crew." : "", p.x + p.width / 2, p.y + 116, 15, SCREEN_DIM);
    if (!running && Button({p.x + p.width / 2 - 120, p.y + 160, 240, 46}, "Back to the reels", true, 18)) { gSess.Leave(); gMode = MODE_MENU; launchedVer = -1; }
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
            if (gSess.game == G_RED_TIDE) { StartRedTideNet(g, &gSess); return; }       // into the water (host or guest)
            if (gSess.game == G_FLIGHT) { StartFlightNet(g, &gSess, FlightFounderKey(gFlSel), gProfile.name.c_str()); return; }   // into the air (host or guest)
            if (gSess.game == G_MOUTHFUL) { StartMouthfulNet(g, &gSess, gProfile.name.c_str()); return; }                            // into the water (host or guest)
            if (gSess.game == G_NIGHT_OFF) { StartNightOffNet(g, &gSess, gProfile.name.c_str(), gNoCrew); return; }                  // ashore (host or guest)
            if (gSess.game == G_SCUFFLE) { StartScuffleNet(g, &gSess, gProfile.name.c_str()); return; }
            if (gSess.game == G_WARP) { StartWarpNet(g, &gSess, gProfile.name.c_str()); return; }
            if (gSess.game == G_FOWL) { StartFowlNet(g, &gSess, gProfile.name.c_str()); return; }
            if (gSess.game == G_BALLPIT) { StartBallPitNet(g, &gSess, gProfile.name.c_str()); return; }
            if (gSess.game == G_FATHOMS) { StartFathomsNet(g, &gSess, gProfile.name.c_str()); return; }
            if (gSess.game == G_NOCLIP) { StartNoclipNet(g, &gSess, gProfile.name.c_str()); return; }                              // into the ring (host or guest)
            if (gSess.game == G_DEEP) { DrawDeepHandoff(); return; }                                                                // The Deep opens on every PC
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
    if (gFlGallery >= 0) { DrawFlightCostumeGallery(gFlGallery); return; }
    if (gFlWardrobe) { if (FlightWardrobePage(gFlSel)) gFlWardrobe = false; return; }
    if (gSfLocker) { if (ScuffleLockerPage(g)) gSfLocker = false; return; }
    if (gFpLocker) { if (FowlLockerPage(g)) gFpLocker = false; return; }
    if (gNcLocker) { if (NoclipLockerPage(g)) gNcLocker = false; return; }
    if (gMfWardrobe) { if (MouthfulWardrobePage()) gMfWardrobe = false; return; }
    if (gNoCloakShot) { gNoCloak = true; gNoCloakShot = false; }
    if (gNoCloak) { if (NightCloakroomPage()) gNoCloak = false; return; }
    // the lobby's small noises: someone sits down, someone speaks
    static int lastSeats = 0; static size_t lastChat = 0;
    gSess.Update(GetTime(), GetFrameTime());
    int seated = 0; for (auto& s : gSess.seats) seated += s.used;
    if (gSess.stage == S_LOBBY && seated > lastSeats && lastSeats > 0) PlayCue("arc.join");
    if (gSess.chat.size() != lastChat && gSess.stage >= S_LOBBY && !gSess.chat.empty() && gSess.chat.back().find(": ") != std::string::npos) PlayCue("arc.chat");
    lastSeats = seated; lastChat = gSess.chat.size();
    if (gRtCustomOpen) {   // Custom mode's rules (the session keeps running underneath)
        DrawCabinBackground();
        if (RedTideCustomPanel()) { gRtCustomOpen = false; if (gSess.stage == S_LOBBY) gSess.Chat("Custom rules: " + RedTideCustomRules()); }
        return;
    }

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
void DebugArcadeReel(int reel) { gMfWardrobe = reel == 206; if (reel == 206) reel = 6; gMode = MODE_MENU; gSel = reel; gDrum = (float)reel; gRtCustomOpen = reel == 104; if (reel == 104) { gSel = 4; gDrum = 4; SetRedTideCustom("q1.50h1.25c30f0l1o1"); } }   // (104: the Custom rules panel)

// the shots (no network needed): 0 the lobby, 1 the table mid-heat, 2 the match over, 3 the rules
void DebugArcadeShot(int which) {
    if (!gProfileLoaded) LoadProfile();
    std::string err;
    SetAudioSuppressed(true);
    gSess.Host(gProfile, which == 10 ? G_RED_TIDE : which == 11 ? G_SCUFFLE : G_SCUTTLE, &err, 47791, net::MakeMemoryTransport(), false);
    gSess.AddAI(); gSess.AddAI();
    if (which == 11) { for (int k = 0; k < 4; k++) gSess.AddAI(); gSess.Chat("Eight sticks: who's first in?"); gMode = MODE_ROOM; SetAudioSuppressed(false); return; }   // (11: a Scuffle lobby, six AI seats and one empty)
    gSess.Chat("Ahoy!");
    gMode = MODE_ROOM;
    if (which == 10) { gSess.seats[1].betOn = 2; gSess.seats[1].bet = 15; gSess.PlaceBet(1, 20); return; }   // (10: a Red Tide lobby with bets: mine on the first AI, one shown for the second)
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
int ArcadeTableGame() { return gMode == MODE_ROOM ? (int)gSess.game : -1; }
void DebugArcadeNoclipLocker(Game& g) { g.scene = Scene::Arcade; gNcLocker = true; }
void DebugArcadeFowlLocker(Game& g) { g.scene = Scene::Arcade; gFpLocker = true; }
void DebugArcadeScuffleLocker(Game& g, int tab) { g.scene = Scene::Arcade; gSfLocker = true; DebugScuffleLocker(tab); }
void DebugArcadeFlightWardrobe(Game& g, int tab, const char* pick, int galleryPage) {
    g.scene = Scene::Arcade; gFlGallery = galleryPage; gFlWardrobe = galleryPage < 0;
    if (galleryPage < 0) DebugFlightWardrobe(tab, pick);
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

// ---------------------------------------------------------------- voice chat's glue (voice.h): every frame, in every scene
// The microphone is open only while voice is on and there's a table to talk to (or the settings page's mic test);
// push-to-talk is the Talk action, or an open mic behind the sensitivity gate. What arrives is handed to the speakers
// by lobby seat; a scene that knows where everyone is (the Trawl, Red Tide) shapes how each is heard.
static bool gMicTest = false, gTalking = false;
static float gMicLevel = 0;
static std::string gMicError;
bool& VoiceMicTest() { return gMicTest; }
float VoiceMicLevel() { return gMicLevel; }
bool VoiceTalking() { return gTalking; }
const std::string& VoiceMicError() { return gMicError; }
int VoiceMySeat() { return gSess.mySeat; }
bool VoiceSpeaking(int seat) { return seat == gSess.mySeat ? gTalking : voice::Speaking(seat); }
const char* VoiceSeatName(int seat) { return seat >= 0 && seat < MAX_PLAYERS && gSess.seats[seat].used ? gSess.seats[seat].name.c_str() : ""; }
void ArcadeVoiceFrame(float dt) {
    static uint16_t seq = 0;
    static voice::Gate gate;
    static bool wasIn = false;
    Settings& S = GameSettings();
    bool inSession = gSess.stage == S_LOBBY || gSess.stage == S_PLAYING;
    bool want = S.voiceOn && (inSession || gMicTest);
    if (want && !voice::MicIsOpen() && gMicError.empty()) { if (!voice::MicOpen(&gMicError)) gMicTest = false; }
    if (!want) { if (voice::MicIsOpen()) voice::MicClose(); gMicError.clear(); }
    ArcadeBetFrame();   // (the pre-match bets: the stake at the start, the payout at the end)
    for (auto& v : gSess.voiceIn) voice::Receive(v.seat, v.seq, v.data.data(), v.data.size());
    gSess.voiceIn.clear();
    if (!inSession && wasIn) voice::Reset();
    wasIn = inSession;
    std::vector<int16_t> pcm;
    int n = voice::MicFrames(pcm);
    bool ptt = ActDown(A_TALK);
    for (int k = 0; k < n; k++) {
        const int16_t* f = &pcm[(size_t)k * voice::FRAME];
        gMicLevel = gMicLevel * 0.5f + voice::FrameLevel(f) * 0.5f;
        gTalking = gate.Step(f, ptt, S.voiceOpenMic, S.voiceSensitivity);
        if (!gTalking) continue;
        uint8_t enc[voice::ENC_BYTES];
        voice::Encode(f, enc);
        if (gMicTest) voice::Receive(voice::SELF, seq, enc, sizeof enc);     // (the mic test: hear yourself as others will)
        if (inSession) { gSess.SendVoice(seq, enc, sizeof enc); voice::GetStats().framesSent++; }
        seq++;
    }
    if (!voice::MicIsOpen()) { gTalking = false; gMicLevel *= 0.8f; }
    voice::Tick(dt);
}
// who's talking: a strip at the top of the screen in any scene while at a table (the game's own HUD may show more)
void DrawVoiceHud() {
    DrawBetResult();
    if (gSess.stage != S_LOBBY && gSess.stage != S_PLAYING) return;
    float x = SCREEN_W / 2.0f - 200, y = 8;
    for (int s = 0; s < MAX_PLAYERS; s++) {
        if (!gSess.seats[s].used || gSess.seats[s].ai || !VoiceSpeaking(s)) continue;
        std::string name = gSess.seats[s].name;
        float w = (float)MeasureTxt(name, 15, true) + 34;
        DrawRectangleRounded({x, y, w, 24}, 0.5f, 6, Fade(Color{12, 20, 22, 255}, 0.75f));
        float lv = s == gSess.mySeat ? gMicLevel : voice::Level(s);
        for (int b = 0; b < 3; b++) DrawRectangle((int)x + 8 + b * 5, (int)(y + 17 - (4 + b * 3) * (0.4f + lv)), 3, (int)((4 + b * 3) * (0.4f + lv)), Color{120, 240, 200, 230});
        TxtBold(name, x + 26, y + 4, 15, Color{220, 236, 226, 255});
        x += w + 8;
    }
}
// ---------------------------------------------------------------- the pre-match bets (the user's request)
// In the lobby each player may stake up to BET_CAP tokens from the game's own wallet (Red Tide's profile, the Trawl's)
// on who will win: Red Tide's richest diver (the richer pair in Poachers), the Trawl's top earner over the run. The
// stake is taken when the match starts; a right pick pays the stake times the number of players at the table, shared
// if several win together; a wrong one loses it; a match left before its end gives it back. Everyone's bet shows.
int BetWallet(int game) { return game == G_RED_TIDE ? skins::REDTIDE : game == G_TRAWL ? skins::TRAWL : -1; }
static struct { bool live = false; int wallet = -1, game = -1, on = -1, amount = 0, players = 0; } gBet;
static std::string gBetMsg; static double gBetMsgAt = -100;
static void BetSay(const std::string& m) { gBetMsg = m; gBetMsgAt = GetTime(); gSess.chat.push_back(m); }
void DrawBetControls(Rectangle row) {
    const SeatInfo& me = gSess.seats[gSess.mySeat];
    int wallet = BetWallet(gSess.game), have = skins::Tokens(wallet);
    std::vector<int> backable; for (int k = 0; k < MAX_PLAYERS; k++) if (gSess.seats[k].used) backable.push_back(k);
    int on = me.betOn, amt = me.bet;
    float x = row.x + 270, y = row.y + 26;
    // (brass glyphs, like the map and mode pickers: the little riveted buttons are too small to read)
    auto hot = [](Rectangle r, const char* s, bool enabled) {
        bool h = enabled && CheckCollisionPointRec(GetMousePosition(), r);
        DrawTextCenteredBold(s, r.x + r.width / 2, r.y + 1, 18, !enabled ? Fade(Pal::BrassDk, 0.4f) : h ? Pal::Brass : Pal::BrassDk);
        if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { PlayCue("ui.click"); return true; }
        return false;
    };
    Txt("Bet", x, y + 3, 14, SCREEN_DIM);
    // who: < name >
    auto idx = std::find(backable.begin(), backable.end(), on);
    int cur = idx == backable.end() ? -1 : (int)(idx - backable.begin());
    if (hot({x + 30, y, 22, 22}, "<", true)) { cur = cur <= 0 ? (int)backable.size() - 1 : cur - 1; on = backable[cur]; if (amt <= 0) amt = std::min(10, std::min(BET_CAP, have)); }
    DrawTextCentered(on >= 0 && gSess.seats[on].used ? gSess.seats[on].name : "nobody", x + 105, y + 3, 14, on >= 0 ? Pal::Brass : SCREEN_DIM);
    if (hot({x + 158, y, 22, 22}, ">", true)) { cur = cur < 0 || cur + 1 >= (int)backable.size() ? 0 : cur + 1; on = backable[cur]; if (amt <= 0) amt = std::min(10, std::min(BET_CAP, have)); }
    // how much: - n +
    if (hot({x + 190, y, 22, 22}, "-", on >= 0)) amt = std::max(0, amt - 5);
    DrawTextCenteredBold(TextFormat("%d", amt), x + 229, y + 2, 16, SCREEN_INK);
    if (hot({x + 246, y, 22, 22}, "+", on >= 0)) amt = std::min({amt + 5, BET_CAP, have});
    Txt(TextFormat("of %d tokens", have), x + 274, y + 3, 12, SCREEN_DIM);
    if (amt > have) amt = have;
    if (on != me.betOn || amt != me.bet) gSess.PlaceBet(amt > 0 ? on : -1, amt);
}
void ArcadeBetFrame() {
    static int lastStage = S_IDLE;
    int me = gSess.mySeat;
    if (lastStage != S_PLAYING && gSess.stage == S_PLAYING && me >= 0 && !gBet.live) {
        // the match starts: the stake leaves the wallet (if it's still there)
        const SeatInfo& s = gSess.seats[me];
        int wallet = BetWallet(gSess.game), players = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) players += gSess.SeatOfPlayer(p) >= 0;
        if (players == 0) return;   // (the launch arrived before the lobby that says who plays: next frame)
        bool inGame = false; for (int p = 0; p < MAX_PLAYERS; p++) if (gSess.SeatOfPlayer(p) == s.betOn) inGame = true;
        if (wallet >= 0 && s.bet > 0 && s.betOn >= 0 && inGame && players >= 2 && skins::Spend(wallet, s.bet)) {
            gBet.live = true; gBet.wallet = wallet; gBet.game = gSess.game; gBet.on = s.betOn; gBet.amount = s.bet; gBet.players = players;
            BetSay(TextFormat("Your bet: %d tokens on %s", s.bet, gSess.seats[s.betOn].name.c_str()));
        }
    }
    if (gBet.live) {
        std::vector<int> won;
        bool over = gBet.game == G_RED_TIDE ? RedTideBetWinners(won) : gBet.game == G_TRAWL ? TrawlBetWinners(won) : false;
        if (over) {
            bool right = std::find(won.begin(), won.end(), gBet.on) != won.end();
            int pay = BetPayout(gBet.amount, gBet.players, (int)won.size(), right);
            if (pay > 0) skins::AddTokens(gBet.wallet, pay);
            std::string who = gBet.on >= 0 && gBet.on < MAX_PLAYERS ? gSess.seats[gBet.on].name : "?";
            BetSay(right ? TextFormat("Your bet on %s came in: %d tokens back (%d staked)", who.c_str(), pay, gBet.amount) : TextFormat("Your bet on %s lost: %d tokens gone", who.c_str(), gBet.amount));
            gBet.live = false;
        } else if (gSess.stage != S_PLAYING) {
            skins::AddTokens(gBet.wallet, gBet.amount);   // (the match ended before a winner: the stake comes back)
            BetSay(TextFormat("The match ended early: your %d tokens come back", gBet.amount));
            gBet.live = false;
        }
    }
    lastStage = gSess.stage;
}
static void DrawBetResult() {
    double age = GetTime() - gBetMsgAt;
    if (age > 8 || gBetMsg.empty()) return;
    float a = (float)std::min(1.0, std::min(age * 4, (8 - age) * 2));
    float w = (float)MeasureTxt(gBetMsg, 17, true) + 40;
    Rectangle r{SCREEN_W / 2.0f - w / 2, 40, w, 32};
    DrawRectangleRounded(r, 0.4f, 6, Fade(Color{12, 20, 22, 255}, 0.8f * a));
    DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Fade(Pal::Brass, a));
    DrawTextCenteredBold(gBetMsg, r.x + w / 2, r.y + 7, 17, Fade(Pal::Brass, a));
}