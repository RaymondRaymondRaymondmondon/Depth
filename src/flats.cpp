// ============================================================================
//  DEPTH - Flats, the card game played at the Nautilus's card table.
//
//  A two-player roguelike duel: you against the ship's dealer, each with your own deck. Three "flats"
//  (lanes) lie across the table. Each round you and the dealer are dealt five cards and take turns laying
//  them, two at a time, then ringing the bell. The higher total in a flat wins it; take two flats to take
//  the round, and two rounds to take the match. Winning a match adds a card of your choice to your deck
//  and puts the next, tougher dealer in front of you. Cash your winnings out after any match, or press
//  on and risk the lot: lose, and the pot goes to the house.
//
//  The scene is laid out like the first act of Inscryption: the dealer sits across from you in the dark,
//  the flats glow on the table between you, your hand fans out along the bottom of the screen, and a brass
//  bell on the table ends your turn.
// ============================================================================
#include "game.h"
#include "rlgl.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
// ---------------------------------------------------------------- the cards
enum Suit { COIN, CUP, BLADE, SHELL, SUITS };
enum Special { SP_NONE, SP_TIDE, SP_SNARE, SP_LANTERN, SP_WAVE, SP_COUNT };
struct Card { int suit = COIN; int value = 1; int special = SP_NONE; };
const char* SUIT_NAME[SUITS] = {"Coin", "Cup", "Blade", "Shell"};
const char* SPECIAL_NAME[SP_COUNT] = {"", "Tide", "Snare", "Lantern", "Wave"};
const char* SPECIAL_TEXT[SP_COUNT] = {"", "Copies the highest card beside it.", "Cuts the opposing flat by 3.",
                                      "Counts as any suit for pairs.", "+2 for each other card in its flat."};
const Color SUIT_COL[SUITS] = {{200, 148, 42, 255}, {60, 100, 168, 255}, {178, 46, 42, 255}, {64, 152, 142, 255}};

constexpr int LANES = 3, LANE_CAP = 3, HAND_SIZE = 5, MATCHES = 3;
const int PAYOUT[MATCHES] = {40, 100, 220};
const char* DEALER_LINES[MATCHES] = {"\"Sit. Cards don't bite. Much.\"", "\"You learn quickly. Shame.\"", "\"Now we play for real.\""};

struct Placed { Card c; float age = 0; };
struct Side {
    std::vector<Card> deck, hand;
    std::vector<Placed> lane[LANES];
};

int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
void Shuffle(std::vector<Card>& v) { for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[Roll(0, i)]); }

// The total of one flat: card values, plus bonuses for sharing a suit and for runs, plus the specials,
// minus 3 for each Snare on the other side of the table in the same flat.
int LaneTotal(const std::vector<Placed>& lane, int snaresAgainst) {
    int n = (int)lane.size();
    if (n == 0) return 0;
    int maxv = 1;
    for (auto& p : lane) if (p.c.special != SP_TIDE) maxv = std::max(maxv, p.c.value);
    int total = 0;
    std::vector<int> vals;
    for (auto& p : lane) {
        int v = p.c.special == SP_TIDE ? maxv : p.c.value;
        vals.push_back(v);
        total += v;
        if (p.c.special == SP_WAVE) total += 2 * (n - 1);
    }
    int cnt[SUITS] = {0}, lanterns = 0;
    for (auto& p : lane) { if (p.c.special == SP_LANTERN) lanterns++; else cnt[p.c.suit]++; }
    int best = 0;
    for (int s = 0; s < SUITS; s++) best = std::max(best, cnt[s]);
    bool lanternsUsed = false;
    for (int s = 0; s < SUITS; s++) {
        int k = cnt[s];
        if (!lanternsUsed && k == best) { k += lanterns; lanternsUsed = true; }
        if (k >= 2) total += 2 * (k - 1); // each extra card of a suit in the flat: +2
    }
    std::sort(vals.begin(), vals.end());
    int steps = 0;
    for (size_t i = 1; i < vals.size(); i++) if (vals[i] == vals[i - 1] + 1) steps++;
    total += (n == 3 && steps == 2) ? 4 : steps; // a straight of three: +4; each consecutive pair: +1
    total -= 3 * snaresAgainst;
    return std::max(0, total);
}

int Snares(const std::vector<Placed>& lane) {
    int n = 0;
    for (auto& p : lane) if (p.c.special == SP_SNARE) n++;
    return n;
}

// ---------------------------------------------------------------- state
enum class Phase { Menu, Playing, Resolving, MatchOver, Reward, RunOver };
struct State {
    bool inited = false;
    Phase phase = Phase::Menu;
    Side you, foe;
    int match = 0, round = 0, roundsYou = 0, roundsFoe = 0;
    bool yourTurn = true;
    int actions = 2, selected = -1;
    float foeT = 0;
    int pot = 0;
    std::vector<Card> rewards;
    std::string banner, sub;
    float resolveT = 0, bellT = 0, beam = 0;
    int laneResult[LANES] = {0, 0, 0}; // after a round: +1 you took it, -1 the dealer did
    bool lost = false, cashed = false;
    int payout = 0;
    bool showRules = false; // the "how to play" folder, open at any time
};
State S;

const char* RULES_TEXT =
    "Three flats lie across the table. Each round you and the dealer are dealt five cards from your own decks and "
    "take turns laying them, two at a time; ring the bell to end your turn. The higher total in a flat wins it. "
    "Take two flats to take the round, and two rounds to take the match.\n\n"
    "A pair of a suit in one flat: +2 for each extra card. Consecutive values: +1 for each step, +4 for a straight of three. "
    "Specials: Tide copies its best neighbour, Snare cuts the opposite flat by 3, Lantern counts as any suit, Wave adds +2 for every other card beside it.\n\n"
    "Win a match to add a card to your deck and face a harder dealer. Payouts: 40, 100 and 220 gold. "
    "Cash out after any match, or press on; lose a match and the pot is gone.";

// A folder tab at the screen's edge, open at any time during play, so the rules are never more than a click away.
Rectangle RulesTabRect() { return {(float)SCREEN_W - 34, 300, 34, 130}; }
void DrawRulesFolder(Vector2 m) {
    Rectangle tab = RulesTabRect();
    bool hot = CheckCollisionPointRec(m, tab) || S.showRules;
    Color col = hot ? Pal::Brass : ColorBrightness(Pal::Brass, -0.3f);
    DrawRectangleRounded({tab.x - 2, tab.y, tab.width + 2, tab.height}, 0.3f, 6, Color{8, 12, 16, 220});
    DrawRectangleRoundedLinesEx({tab.x - 2, tab.y, tab.width + 2, tab.height}, 0.3f, 6, 1.5f, col);
    rlPushMatrix();
    rlTranslatef(tab.x + tab.width / 2 + 5, tab.y + tab.height - 10, 0);
    rlRotatef(-90, 0, 0, 1);
    TxtBold("HOW TO PLAY", -55, -8, 15, col);
    rlPopMatrix();
    if (S.showRules) {
        Rectangle p{SCREEN_W / 2.0f - 340, SCREEN_H / 2.0f - 230, 680, 460};
        Panel(p);
        DrawTextCenteredBold("How Flats is played", p.x + p.width / 2, p.y + 18, 30, Pal::Ink);
        DrawWrapped(RULES_TEXT, {p.x + 34, p.y + 64, p.width - 68, p.height - 130}, 15, Pal::Ink);
        if (Button({p.x + p.width / 2 - 90, p.y + p.height - 56, 180, 42}, "Close")) S.showRules = false;
    }
}

Card RandomCard(int lo, int hi) {
    Card c;
    c.suit = Roll(0, SUITS - 1);
    c.value = Roll(lo, hi);
    return c;
}

void BuildDecks() {
    S.you.deck.clear();
    for (int s = 0; s < SUITS; s++) for (int v : {2, 4, 6}) S.you.deck.push_back({s, v, SP_NONE});
}

void BuildFoeDeck(int match) {
    S.foe.deck.clear();
    int size = 12 + match * 2;
    for (int i = 0; i < size; i++) S.foe.deck.push_back(RandomCard(1, 6 + match));
    for (int i = 0; i < match + 1 && match > 0; i++) {
        Card c = RandomCard(1, 5);
        c.special = Roll(SP_TIDE, SP_COUNT - 1);
        c.value = c.special == SP_SNARE ? 1 : c.special == SP_WAVE ? 2 : c.special == SP_LANTERN ? 4 : 2;
        S.foe.deck[Roll(0, (int)S.foe.deck.size() - 1)] = c;
    }
}

void ClearLanes() {
    for (int l = 0; l < LANES; l++) { S.you.lane[l].clear(); S.foe.lane[l].clear(); }
}

void DealHands() {
    for (Side* s : {&S.you, &S.foe}) {
        std::vector<Card> pile = s->deck;
        Shuffle(pile);
        int n = std::min((int)pile.size(), HAND_SIZE + (s == &S.foe && S.match == 2 ? 1 : 0));
        s->hand.assign(pile.begin(), pile.begin() + n);
    }
}

void BeginTurn(bool you);

void StartRound() {
    ClearLanes();
    DealHands();
    S.selected = -1;
    S.phase = Phase::Playing;
    BeginTurn(true);
}

void BeginTurn(bool you) {
    S.yourTurn = you;
    Side& side = you ? S.you : S.foe;
    S.actions = std::min(2, (int)side.hand.size());
    S.foeT = 0.7f;
    S.selected = -1;
}

void ResetRun() {
    S = State{};
    S.inited = true;
    BuildDecks();
}

void StartMatch() {
    S.roundsYou = S.roundsFoe = 0;
    S.round = 0;
    BuildFoeDeck(S.match);
    StartRound();
}

// A single card into a flat.
bool PlayCard(Side& side, int handIdx, int lane) {
    if (handIdx < 0 || handIdx >= (int)side.hand.size() || (int)side.lane[lane].size() >= LANE_CAP) return false;
    Placed p;
    p.c = side.hand[handIdx];
    side.hand.erase(side.hand.begin() + handIdx);
    side.lane[lane].push_back(p);
    return true;
}

bool HandsEmpty() { return S.you.hand.empty() && S.foe.hand.empty(); }
bool CanPlaceAnywhere(const Side& s) {
    for (int l = 0; l < LANES; l++) if ((int)s.lane[l].size() < LANE_CAP) return true;
    return false;
}

// ---------------------------------------------------------------- the dealer's play
float Sigmoid(float m) { return 1.0f / (1.0f + expf(-m / 3.0f)); }

// The play both the dealer and the simulated "sensible" player use: for each card and flat, how much does it
// improve the balance there? Small cards go first and the big ones are held back; `noise` makes it err.
bool GreedyPlayOne(Side& me, Side& them, float noise) {
    if (me.hand.empty() || !CanPlaceAnywhere(me)) return false;
    float bestU = -1e9f;
    int bc = -1, bl = -1;
    for (int i = 0; i < (int)me.hand.size(); i++)
        for (int l = 0; l < LANES; l++) {
            if ((int)me.lane[l].size() >= LANE_CAP) continue;
            int themTot = LaneTotal(them.lane[l], Snares(me.lane[l]));
            int before = LaneTotal(me.lane[l], Snares(them.lane[l]));
            std::vector<Placed> trial = me.lane[l];
            Placed p; p.c = me.hand[i];
            trial.push_back(p);
            int after = LaneTotal(trial, Snares(them.lane[l]));
            int themAfter = LaneTotal(them.lane[l], Snares(trial)); // a Snare also cuts what the other side has there
            float u = Sigmoid((float)(after - themAfter)) - Sigmoid((float)(before - themTot));
            if (me.hand[i].special == SP_SNARE) u += 0.06f * (float)(themTot > 0);
            u -= 0.012f * me.hand[i].value;
            u += (Roll(0, 1000) / 1000.0f - 0.5f) * noise;
            if (u > bestU) { bestU = u; bc = i; bl = l; }
        }
    if (bc < 0) return false;
    PlayCard(me, bc, bl);
    return true;
}

bool FoePlayOne() {
    float noise = S.match == 0 ? 0.22f : S.match == 1 ? 0.10f : 0.03f; // an early dealer errs more often
    return GreedyPlayOne(S.foe, S.you, noise);
}

// ---------------------------------------------------------------- audio: a bell and a card on wood
Sound MakeSound(float dur, const std::function<float(float)>& gen) {
    const int rate = 22050;
    int n = (int)(rate * dur);
    short* data = (short*)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++) data[i] = (short)(std::clamp(gen((float)i / rate), -1.0f, 1.0f) * 0.7f * 32767);
    Wave w{(unsigned)n, rate, 16, 1, data};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

void PlayBell() {
    static Sound s{};
    static bool loaded = false;
    if (!IsAudioDeviceReady()) return;
    if (!loaded) {
        s = MakeSound(1.6f, [](float t) {
            float e = expf(-t * 3.2f);
            return e * (0.5f * sinf(2 * PI * 880 * t) + 0.32f * sinf(2 * PI * 1318 * t) + 0.22f * sinf(2 * PI * 2093 * t) * expf(-t * 4)) *
                   std::min(1.0f, t * 400);
        });
        loaded = true;
    }
    PlaySound(s);
}

void PlaySlap() {
    static Sound s{};
    static bool loaded = false;
    if (!IsAudioDeviceReady()) return;
    if (!loaded) {
        unsigned seed = 4242;
        float lp = 0;
        s = MakeSound(0.11f, [&seed, &lp](float t) {
            seed = seed * 1664525u + 1013904223u;
            float n = ((seed >> 9) & 0xffff) / 32768.0f - 1;
            lp += (n - lp) * 0.25f;
            return lp * expf(-t * 38) * 1.6f + 0.35f * sinf(2 * PI * 140 * t) * expf(-t * 50);
        });
        loaded = true;
    }
    PlaySound(s);
}

// ---------------------------------------------------------------- drawing: cards
void DrawSuitIcon(int suit, Vector2 c, float s, Color col) {
    Color dk = ColorBrightness(col, -0.5f), lt = ColorBrightness(col, 0.35f);
    switch (suit) {
        case COIN:
            DrawCircleV(c, 0.5f * s, dk);
            DrawCircleV(c, 0.44f * s, col);
            DrawRing(c, 0.26f * s, 0.32f * s, 0, 360, 20, dk);
            DrawRectangleRec({c.x - 0.04f * s, c.y - 0.18f * s, 0.08f * s, 0.36f * s}, dk);
            DrawCircleSector(c, 0.44f * s, 200, 250, 8, Fade(lt, 0.6f));
            break;
        case CUP:
            DrawTri({c.x - 0.46f * s, c.y - 0.46f * s}, {c.x + 0.46f * s, c.y - 0.46f * s}, {c.x + 0.16f * s, c.y + 0.06f * s}, col);
            DrawTri({c.x - 0.46f * s, c.y - 0.46f * s}, {c.x + 0.16f * s, c.y + 0.06f * s}, {c.x - 0.16f * s, c.y + 0.06f * s}, col);
            DrawRectangleRec({c.x - 0.05f * s, c.y + 0.04f * s, 0.1f * s, 0.34f * s}, dk);
            DrawEllipse((int)c.x, (int)(c.y + 0.42f * s), 0.3f * s, 0.07f * s, dk);
            DrawRectangleRec({c.x - 0.46f * s, c.y - 0.5f * s, 0.92f * s, 0.06f * s}, lt);
            break;
        case BLADE:
            DrawTri({c.x, c.y - 0.56f * s}, {c.x - 0.11f * s, c.y + 0.18f * s}, {c.x + 0.11f * s, c.y + 0.18f * s}, col);
            DrawTri({c.x, c.y - 0.56f * s}, {c.x, c.y + 0.18f * s}, {c.x + 0.11f * s, c.y + 0.18f * s}, lt);
            DrawRectangleRec({c.x - 0.3f * s, c.y + 0.16f * s, 0.6f * s, 0.08f * s}, dk);
            DrawRectangleRec({c.x - 0.045f * s, c.y + 0.22f * s, 0.09f * s, 0.26f * s}, dk);
            DrawCircleV({c.x, c.y + 0.52f * s}, 0.06f * s, dk);
            break;
        default: // shell
            DrawCircleSector({c.x, c.y + 0.16f * s}, 0.56f * s, 198, 342, 16, col);
            for (int k = -3; k <= 3; k++) {
                float a = (270 + k * 19) * DEG2RAD;
                DrawLineEx({c.x, c.y + 0.16f * s}, {c.x + cosf(a) * 0.54f * s, c.y + 0.16f * s + sinf(a) * 0.54f * s}, 0.03f * s + 0.5f, dk);
            }
            DrawRectangleRec({c.x - 0.14f * s, c.y + 0.14f * s, 0.28f * s, 0.12f * s}, dk);
            break;
    }
}

// A card face (or its back) filling `r`. Everything scales from the card's height, so the same routine
// draws a big card in your hand and a small one on the far side of the table.
void DrawCardFace(Rectangle r, const Card& c, bool faceUp) {
    float u = r.height / 150.0f;
    DrawRectangleRounded({r.x + 2 * u, r.y + 3 * u, r.width, r.height}, 0.08f, 6, Fade(BLACK, 0.5f));
    if (!faceUp) {
        DrawRectangleRounded(r, 0.08f, 6, Color{40, 46, 66, 255});
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, 1.5f * u + 0.5f, Color{110, 120, 150, 255});
        Vector2 m{r.x + r.width / 2, r.y + r.height / 2};
        DrawRing(m, 0.16f * r.width, 0.2f * r.width, 0, 360, 20, Color{110, 120, 150, 255});
        DrawCircleV(m, 0.07f * r.width, Color{110, 120, 150, 255});
        return;
    }
    Color paper{234, 200, 148, 255}, ink{38, 26, 20, 255};
    DrawRectangleRounded(r, 0.08f, 6, ColorBrightness(paper, -0.25f));
    DrawRectangleRounded({r.x + 1.5f * u, r.y + 1.5f * u, r.width - 3 * u, r.height - 3 * u}, 0.08f, 6, paper);
    DrawRectangleRoundedLinesEx({r.x + 4 * u, r.y + 4 * u, r.width - 8 * u, r.height - 8 * u}, 0.06f, 6, std::max(1.0f, 1.2f * u), Fade(ink, 0.7f));
    Color col = SUIT_COL[c.suit];
    if (c.special != SP_NONE) { // a coloured header band for the specials
        Rectangle band{r.x + 6 * u, r.y + 6 * u, r.width - 12 * u, 20 * u};
        DrawRectangleRec(band, ColorBrightness(col, -0.15f));
        std::string nm = SPECIAL_NAME[c.special];
        for (auto& ch : nm) ch = (char)toupper(ch);
        int fs = std::max(8, (int)(15 * u));
        TxtBold(nm, band.x + band.width / 2 - MeasureTxt(nm, fs, true) / 2.0f, band.y + 2 * u, fs, Color{250, 240, 220, 255});
    }
    DrawSuitIcon(c.suit, {r.x + r.width / 2, r.y + r.height * 0.54f}, r.width * 0.62f, col);
    int fs = std::max(9, (int)(30 * u));
    TxtBold(TextFormat("%d", c.value), r.x + 9 * u, r.y + (c.special != SP_NONE ? 28 : 8) * u, fs, ink);
    TxtBold(TextFormat("%d", c.value), r.x + r.width - 9 * u - MeasureTxt(TextFormat("%d", c.value), fs, true), r.y + r.height - 8 * u - fs, fs, ink);
    if (c.special == SP_NONE) {
        std::string nm = SUIT_NAME[c.suit];
        int nf = std::max(7, (int)(12 * u));
        Txt(nm, r.x + r.width / 2 - MeasureTxt(nm, nf) / 2.0f, r.y + r.height - 22 * u, nf, Fade(ink, 0.6f));
    }
}

// ---------------------------------------------------------------- drawing: the room
constexpr float FOE_ROW_Y = 392, YOU_ROW_Y = 512, LANE_X[LANES] = {440, 640, 840};
constexpr Vector2 FOE_CARD = {48, 68}, YOU_CARD = {62, 88};
constexpr float FOE_STEP = 54, YOU_STEP = 66;

Rectangle SlotRect(bool you, int lane, int idx) {
    float step = you ? YOU_STEP : FOE_STEP;
    Vector2 sz = you ? YOU_CARD : FOE_CARD;
    float cx = LANE_X[lane] + (idx - 1) * step, cy = you ? YOU_ROW_Y : FOE_ROW_Y;
    return {cx - sz.x / 2, cy - sz.y / 2, sz.x, sz.y};
}

Vector2 TP(float u, float v) { float hw = 390 + 270 * v; return {640 + u * hw, 300 + 310 * v}; }

void DrawTable() {
    // the tabletop: dark planks running away from you, converging toward the far edge
    for (int i = 0; i < 24; i++) {
        float v0 = i / 24.0f, v1 = (i + 1) / 24.0f;
        Vector2 a = TP(-1, v0), b = TP(1, v0), c = TP(1, v1), d = TP(-1, v1);
        float shade = 0.42f + 0.5f * v0 + 0.05f * sinf(i * 2.3f);
        Color col{(unsigned char)(70 * shade), (unsigned char)(48 * shade), (unsigned char)(34 * shade), 255};
        DrawTri(a, b, c, col);
        DrawTri(a, c, d, col);
    }
    for (int k = -9; k <= 9; k += 3) DrawLineEx(TP(k / 10.0f, 0), TP(k / 10.0f, 1), 1.5f, Color{26, 16, 12, 200});
    DrawLineEx(TP(-1, 0), TP(1, 0), 4, Color{30, 20, 14, 255});
    DrawLineEx(TP(-1, 0), TP(-1, 1), 3, Color{20, 14, 10, 255});
    DrawLineEx(TP(1, 0), TP(1, 1), 3, Color{20, 14, 10, 255});
    // the glowing dividing line where the flats meet
    DrawLineEx({250, 452}, {1030, 452}, 1.5f, Color{60, 180, 220, 60});
    Txt("you : dealer", 258, 458, 12, Color{120, 190, 220, 160});
}

// The same lit-figure technique the salon uses for its own cast (ShadeLimb/ShadeBall between BeginFigure/
// EndFigure), so the dealer you actually sit across from looks as three-dimensional as the one at his table
// in the salon, just bigger: he's the whole scene's focal point here.
// The same proportions and lit-figure technique (ShadeLimb/ShadeBall between BeginFigure/EndFigure) as the
// dealer standing at his table in the salon, just scaled up: he's the whole scene's focal point here, so the
// two should read as the same character instead of two differently-drawn men.
void DrawDealer(float t) {
    float cx = 640, breathe = sinf(t * 1.2f) * 2.4f;
    Vector2 feet{cx, 470}, o = FigureFeet();
    float k = 2.0f;
    auto P = [&](float dx, float dy) { return Vector2{o.x + dx * k, o.y + dy * k}; };
    Color cloak{26, 30, 48, 255}, glove{34, 34, 40, 255}, wax{132, 134, 132, 255};
    BeginFigure();
    ShadeLimb(P(-30, -128 + breathe), P(30, -128 + breathe), 24, 24, cloak); // shoulders
    ShadeLimb(P(0, -60), P(0, -130 + breathe), 30, 27, cloak);               // the body under the cloak
    DrawTri(P(-46, -118), P(46, -118), P(0, -180 + breathe), Tone(cloak, -0.3f)); // the hood's peak
    ShadeBall(P(0, -160 + breathe), 25, Tone(cloak, -0.2f));                 // hood
    ShadeBall(P(1, -158 + breathe), 15.5f, wax);                            // face
    DrawLineEx(P(-12, -164 + breathe), P(12, -164 + breathe), 6, Color{50, 52, 56, 255});   // heavy brow
    DrawLineEx(P(-8, -142 + breathe), P(8, -142 + breathe), 4.4f, Color{36, 32, 34, 255});  // a flat mouth
    for (int e = -1; e <= 1; e += 2) DrawEllipse((int)P(e * 6.5f, -159 + breathe).x, (int)P(e * 6.5f, -159 + breathe).y, 3.6f * k, 2.4f * k, Color{214, 226, 255, 255});
    ShadeLimb(P(-30, -128 + breathe), P(-38, -108), 10, 9, cloak); // arms reaching to the felt
    ShadeLimb(P(30, -128 + breathe), P(38, -108), 10, 9, cloak);
    ShadeBall(P(-38, -106), 7, glove);
    ShadeBall(P(38, -106), 7, glove);
    EndFigure(feet);
    // his eyes catch the light and follow you
    Vector2 m = GetMousePosition();
    float lookx = std::clamp((m.x - cx) * 0.012f, -4.0f, 4.0f), looky = std::clamp((m.y - 200) * 0.006f, -2.0f, 3.0f);
    for (int s = -1; s <= 1; s += 2) {
        Vector2 e{feet.x + (P(s * 6.5f, -159 + breathe).x - o.x), feet.y + (P(s * 6.5f, -159 + breathe).y - o.y)};
        Glow(e, 26, Color{150, 170, 255, 90});
        DrawCircleV({e.x + lookx, e.y + looky}, 3.6f, Color{20, 24, 40, 255});
    }
    // a dagger held up beside his shoulder, catching the cold light
    Vector2 hand{feet.x + (P(38, -108).x - o.x), feet.y + (P(38, -108).y - o.y)}, tip{cx + 60, feet.y - 340};
    DrawLineEx(hand, tip, 7, Color{150, 176, 196, 255});
    DrawLineEx({hand.x - 3, hand.y}, {tip.x - 3, tip.y + 10}, 2, Color{210, 232, 244, 255});
    DrawLineEx({hand.x - 22, hand.y - 4}, {hand.x + 22, hand.y - 4}, 6, Color{90, 78, 56, 255});
    DrawCircleV({hand.x, hand.y + 14}, 15, Color{28, 30, 36, 255}); // his gloved fist around the grip
    Vector2 rest{feet.x + (P(-38, -106).x - o.x), feet.y + (P(-38, -106).y - o.y) + 156};
    DrawCircleV(rest, 15, Color{28, 30, 36, 255}); // and the other hand, resting on the table
}

void DrawSkull(float t, float x, float y) {
    DrawEllipse((int)x, (int)(y - 8), 26, 24, Color{210, 200, 172, 255});
    DrawEllipse((int)x, (int)(y + 12), 18, 12, Color{192, 182, 154, 255});
    DrawEllipse((int)x - 9, (int)(y - 6), 7, 8, Color{20, 16, 14, 255});
    DrawEllipse((int)x + 9, (int)(y - 6), 7, 8, Color{20, 16, 14, 255});
    DrawTri({x - 3, y + 2}, {x + 3, y + 2}, {x, y + 9}, Color{30, 24, 20, 255});
    for (int k = -2; k <= 2; k++) DrawRectangle((int)(x + k * 6 - 2), (int)(y + 16), 4, 7, Color{226, 218, 190, 255});
    for (int c = -1; c <= 1; c += 2) {
        float cx = x + c * 12;
        DrawRectangle((int)cx - 4, (int)(y - 44), 8, 26, Color{236, 228, 200, 255});
        float f = sinf(t * 9 + c * 2) * 1.5f + sinf(t * 23 + c) * 0.8f;
        DrawEllipse((int)cx, (int)(y - 52), 4.5f + f * 0.3f, 9 + f, Color{255, 208, 120, 255});
        Glow({cx, y - 52}, 44, Color{255, 190, 100, 90});
    }
}

// The scales: their beam tilts toward whoever is ahead in the flats right now.
void DrawScale(float tilt) {
    Vector2 top{124, 250};
    Color brass = Pal::Brass, brassDk = Pal::BrassDk;
    DrawEllipse(124, 486, 56, 12, brassDk);
    DrawRectangle(118, 250, 12, 236, brassDk);
    DrawRectangle(120, 250, 5, 236, brass);
    DrawCircleV(top, 13, brass);
    float a = tilt;
    Vector2 l{top.x - cosf(a) * 94, top.y - sinf(a) * 94}, r{top.x + cosf(a) * 94, top.y + sinf(a) * 94};
    DrawLineEx(l, r, 8, brassDk);
    DrawLineEx({l.x, l.y - 1}, {r.x, r.y - 1}, 4, brass);
    for (Vector2 e : {l, r}) {
        DrawCircleV(e, 8, brass);
        Vector2 dish{e.x, e.y + 96};
        DrawLineEx(e, {dish.x - 42, dish.y - 8}, 1.5f, brassDk);
        DrawLineEx(e, {dish.x + 42, dish.y - 8}, 1.5f, brassDk);
        DrawCircleSector({dish.x, dish.y - 12}, 46, 0, 180, 20, brassDk);
        DrawCircleSector({dish.x, dish.y - 14}, 42, 0, 180, 20, brass);
        DrawEllipse((int)dish.x, (int)dish.y - 14, 46, 6, ColorBrightness(brass, 0.2f));
    }
    // bone tally-beads heaped in the dishes, one per flat currently held
    int youLeads = 0, foeLeads = 0;
    for (int i = 0; i < LANES; i++) {
        int y = LaneTotal(S.you.lane[i], Snares(S.foe.lane[i])), f = LaneTotal(S.foe.lane[i], Snares(S.you.lane[i]));
        if (y > f) youLeads++; else if (f > y) foeLeads++;
    }
    Vector2 dishL{l.x, l.y + 96}, dishR{r.x, r.y + 96};
    for (int k = 0; k < foeLeads; k++) DrawCircleV({dishL.x - 14 + k * 14.0f, dishL.y - 32 - (k % 2) * 4}, 6, Color{224, 218, 196, 255});
    for (int k = 0; k < youLeads; k++) DrawCircleV({dishR.x - 14 + k * 14.0f, dishR.y - 32 - (k % 2) * 4}, 6, Color{224, 218, 196, 255});
    // the two sides, labelled so the balance reads at a glance
    Txt("DEALER", l.x - MeasureTxt("DEALER", 12) / 2.0f, l.y - 30, 12, Fade(Pal::Paper, 0.7f));
    Txt("YOU", r.x - MeasureTxt("YOU", 12) / 2.0f, r.y - 30, 12, Fade(Pal::Paper, 0.7f));
}

// The bell: click it to end your turn.
void DrawBell(Vector2 c, bool live, float ring) {
    float s = sinf(ring * 40) * ring * 4;
    Color brass = live ? Pal::Brass : ColorBrightness(Pal::Brass, -0.35f), dk = ColorBrightness(brass, -0.45f), hi = ColorBrightness(brass, 0.45f);
    DrawEllipse((int)c.x, (int)(c.y + 24), 56, 10, Color{20, 14, 10, 255});
    DrawEllipse((int)c.x, (int)(c.y + 22), 52, 8, dk);
    DrawEllipse((int)(c.x + s), (int)(c.y + 12), 12, 10, dk); // the clapper
    DrawCircleSector({c.x, c.y + 6}, 46, 180, 360, 24, dk);
    DrawCircleSector({c.x - 2, c.y + 6}, 42, 180, 360, 24, brass);
    DrawEllipse((int)c.x, (int)(c.y + 6), 47, 9, dk);
    DrawEllipse((int)c.x, (int)(c.y + 5), 43, 6, hi);
    DrawCircleV({c.x, c.y - 42}, 7, brass);
    DrawCircleSector({c.x - 8, c.y + 4}, 36, 200, 250, 8, Fade(WHITE, 0.35f));
    if (ring > 0)
        for (int k = 1; k <= 3; k++) DrawRing({c.x, c.y - 10}, 60 + k * 26 * (1 - ring) + 4, 63 + k * 26 * (1 - ring) + 4, 200, 340, 12, Fade(hi, ring * 0.5f));
}

void DrawSlot(Rectangle r, bool hot, float t) {
    Color cy{70, 200, 240, 255};
    Glow({r.x + r.width / 2, r.y + r.height / 2}, r.width * 0.9f, Fade(cy, hot ? 0.28f : 0.10f));
    DrawRectangleRounded(r, 0.1f, 6, Color{8, 26, 40, 200});
    DrawRectangleRoundedLinesEx(r, 0.1f, 6, hot ? 2.5f : 1.5f, Fade(cy, hot ? 0.95f : 0.55f + 0.1f * sinf(t * 2)));
    Vector2 m{r.x + r.width / 2, r.y + r.height / 2};
    DrawCircleV(m, r.width * 0.12f, Fade(cy, 0.35f)); // the little emblem in an empty slot
    DrawRing(m, r.width * 0.2f, r.width * 0.24f, 0, 360, 16, Fade(cy, 0.3f));
}

// ---------------------------------------------------------------- the scene
Rectangle BellRect() { return {1040, 420, 100, 90}; }

bool RunOver() { return S.phase == Phase::RunOver; }

void Resolve() {
    int you = 0, foe = 0, yourSum = 0, foeSum = 0;
    for (int l = 0; l < LANES; l++) {
        int y = LaneTotal(S.you.lane[l], Snares(S.foe.lane[l])), f = LaneTotal(S.foe.lane[l], Snares(S.you.lane[l]));
        S.laneResult[l] = y > f ? 1 : f > y ? -1 : 0;
        you += y > f; foe += f > y;
        yourSum += y; foeSum += f;
    }
    int winner = you > foe ? 1 : foe > you ? -1 : yourSum > foeSum ? 1 : foeSum > yourSum ? -1 : 0;
    if (winner > 0) { S.roundsYou++; S.banner = "You take the round"; }
    else if (winner < 0) { S.roundsFoe++; S.banner = "The dealer takes the round"; }
    else S.banner = "A dead heat: the round is dealt again";
    S.sub = TextFormat("Flats %d - %d   (totals %d - %d)", you, foe, yourSum, foeSum);
    if (winner == 0) S.round--; // replay
    S.round++;
    S.phase = Phase::Resolving;
    S.resolveT = 2.6f;
}

void MakeRewards() {
    S.rewards.clear();
    for (int k = 0; k < 3; k++) {
        Card c = RandomCard(5, 9);
        if (Roll(1, 100) <= 40) {
            c.special = Roll(SP_TIDE, SP_COUNT - 1);
            c.value = c.special == SP_SNARE ? 1 : c.special == SP_WAVE ? 2 : c.special == SP_LANTERN ? 4 : 2;
        }
        S.rewards.push_back(c);
    }
    S.phase = Phase::Reward;
}

void TakeReward(int k) {
    S.you.deck.push_back(S.rewards[k]);
    S.match++;
    StartMatch();
}

void DrawPot(Game& g) {
    DrawRectangleRounded({SCREEN_W - 250.0f, 12, 238, 40}, 0.3f, 6, Color{8, 12, 16, 210});
    DrawRectangleRoundedLinesEx({SCREEN_W - 250.0f, 12, 238, 40}, 0.3f, 6, 1.5f, Pal::BrassDk);
    DrawCircle(SCREEN_W - 228, 32, 9, Pal::Brass);
    TxtBold(TextFormat("%d", g.gold), SCREEN_W - 210, 20, 20, Pal::Brass);
    Txt(TextFormat("Pot %d", S.pot), SCREEN_W - 120, 23, 17, Pal::Paper);
}

void LeaveTable(Game& g) {
    S.inited = false;
    g.scene = Scene::Hub;
}

void Tooltip(const std::string& text, Vector2 at) {
    float w = (float)MeasureTxt(text, 15, true);
    Rectangle r{std::clamp(at.x - w / 2 - 10, 8.0f, SCREEN_W - w - 28), at.y, w + 20, 28};
    DrawRectangleRounded(r, 0.4f, 6, Color{8, 12, 16, 240});
    DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Pal::BrassDk);
    TxtBold(text, r.x + 10, r.y + 5, 15, Pal::Paper);
}

std::string CardDescription(const Card& c) {
    if (c.special != SP_NONE) return TextFormat("%s (%d): %s", SPECIAL_NAME[c.special], c.value, SPECIAL_TEXT[c.special]);
    return TextFormat("%s %d", SUIT_NAME[c.suit], c.value);
}
// One frame of the rules: the dealer's plays, resolving a round, and easing the balance. With utoYou
// set (the headless simulation) your own turn is played at random too.
void UpdateFlats(float dt, bool autoYou, bool sensible) {
    S.bellT = std::max(0.0f, S.bellT - dt * 1.1f);
    for (int l = 0; l < LANES; l++)
        for (Side* s : {&S.you, &S.foe}) for (auto& p : s->lane[l]) p.age = std::min(1.0f, p.age + dt * 6);
    if (S.phase == Phase::Playing) {
        // the dealer takes his plays one at a time, with a pause between, then it's your turn
        if (!S.yourTurn) {
            S.foeT -= dt;
            if (S.foeT <= 0) {
                if (S.actions > 0 && FoePlayOne()) { S.actions--; S.foeT = 0.85f; PlaySlap(); }
                else {
                    if (HandsEmpty() || (!CanPlaceAnywhere(S.foe) && !CanPlaceAnywhere(S.you))) Resolve();
                    else BeginTurn(true);
                }
            }
        } else if (S.you.hand.empty()) {
            // out of cards: the dealer finishes his hand alone
            if (S.foe.hand.empty()) Resolve(); else BeginTurn(false);
        } else if (!CanPlaceAnywhere(S.you) && !S.you.hand.empty()) {
            S.you.hand.clear(); // nowhere left to put anything
        }
    } else if (S.phase == Phase::Resolving) {
        S.resolveT -= dt;
        if (S.resolveT <= 0) {
            if (S.roundsYou >= 2) {
                S.phase = Phase::MatchOver;
                S.pot += PAYOUT[S.match];
            } else if (S.roundsFoe >= 2) {
                S.phase = Phase::RunOver;
                S.lost = true;
                S.payout = 0;
            } else StartRound();
        }
    }
    // the scales ease toward whoever leads
    int lead = 0;
    for (int l = 0; l < LANES; l++) {
        int y = LaneTotal(S.you.lane[l], Snares(S.foe.lane[l])), f = LaneTotal(S.foe.lane[l], Snares(S.you.lane[l]));
        lead += y > f ? 1 : f > y ? -1 : 0;
    }
    float target = -lead * 0.13f;
    S.beam += (target - S.beam) * std::min(1.0f, dt * 3);


    if (autoYou && S.phase == Phase::Playing && S.yourTurn && !S.you.hand.empty()) {
        if (S.actions > 0) {
            if (sensible) {
                if (GreedyPlayOne(S.you, S.foe, 0.03f)) S.actions--; else S.you.hand.clear();
            } else {
                std::vector<int> open;
                for (int l = 0; l < LANES; l++) if ((int)S.you.lane[l].size() < LANE_CAP) open.push_back(l);
                if (!open.empty()) { PlayCard(S.you, Roll(0, (int)S.you.hand.size() - 1), open[Roll(0, (int)open.size() - 1)]); S.actions--; }
                else S.you.hand.clear();
            }
        } else {
            if (HandsEmpty()) Resolve(); else BeginTurn(false);
        }
    }
}
}  // namespace

// ============================================================================
void SceneCards(Game& g) {
    if (!S.inited) ResetRun();
    float dt = GetFrameTime(), t = g.time;
    SetPost(0.75f, 0.03f, 0.5f);
    Vector2 m = GetMousePosition();

    UpdateFlats(dt, false, false);

    // ---------------- draw the room
    ClearBackground(Color{4, 5, 8, 255});
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    // hanging chains and dim shapes in the dark around the dealer, as in the reference
    for (int k = 0; k < 6; k++) {
        float x = 80 + k * 220 + sinf(t * 0.3f + k) * 4;
        DrawLineEx({x, 0}, {x + 20, 200.0f + (k % 3) * 40}, 3, Color{18, 22, 30, 255});
    }
    DrawDealer(t);
    DrawTable();
    DrawSkull(t, 300, 372);
    DrawScale(S.beam);
    // bottles on the right, as on the reference's table
    for (int k = 0; k < 2; k++) {
        float x = 1130 + k * 44, y = 352 + k * 16;
        DrawRectangleRounded({x - 14, y, 28, 56}, 0.3f, 6, Color{58, 78, 52, 255});
        DrawRectangle((int)x - 5, (int)y - 14, 10, 16, Color{50, 66, 46, 255});
        DrawRectangle((int)x - 6, (int)y - 18, 12, 6, Color{150, 108, 60, 255});
        DrawRectangle((int)x - 10, (int)y + 16, 20, 22, Color{220, 190, 130, 255});
    }
    // where the flats are: your row and the dealer's, each in threes
    int hoverLane = -1;
    for (int l = 0; l < LANES; l++) {
        Rectangle whole{SlotRect(true, l, 0).x, SlotRect(true, l, 0).y, SlotRect(true, l, 2).x + YOU_CARD.x - SlotRect(true, l, 0).x, YOU_CARD.y};
        if (CheckCollisionPointRec(m, whole)) hoverLane = l;
    }
    bool aiming = S.phase == Phase::Playing && S.yourTurn && S.selected >= 0 && S.actions > 0;
    for (int l = 0; l < LANES; l++) {
        for (int i = 0; i < LANE_CAP; i++) {
            DrawSlot(SlotRect(false, l, i), false, t);
            bool free = i == (int)S.you.lane[l].size();
            DrawSlot(SlotRect(true, l, i), aiming && hoverLane == l && free, t);
        }
    }

    // ---------------- lights: the candles at the skull, and a cold glow over the flats
    LightsBegin(Color{48, 50, 66, 255});
    AddLight({300, 340}, 380, Color{255, 190, 110, 255}, 0.95f);
    AddLight({640, 470}, 620, Color{80, 150, 200, 255}, 0.6f);
    AddLight({640, 150}, 230, Color{110, 120, 150, 255}, 0.5f);
    AddLight({124, 350}, 260, Color{255, 200, 120, 255}, 0.4f);
    AddLight({1090, 460}, 200, Color{255, 190, 110, 255}, 0.45f);
    LightsEnd();
    InkPass(0.55f, 0.7f);

    // ---------------- cards on the table (drawn crisp, after the ink pass)
    for (int l = 0; l < LANES; l++)
        for (bool you : {false, true}) {
            auto& lane = (you ? S.you : S.foe).lane[l];
            for (int i = 0; i < (int)lane.size(); i++) {
                Rectangle r = SlotRect(you, l, i);
                float sc = 0.55f + 0.45f * lane[i].age, lift = (1 - lane[i].age) * 40 * (you ? 1 : -1);
                Rectangle rr{r.x + r.width * (1 - sc) / 2, r.y + r.height * (1 - sc) / 2 + lift, r.width * sc, r.height * sc};
                DrawCardFace(rr, lane[i].c, true);
                if (CheckCollisionPointRec(m, r) && lane[i].age >= 1) Tooltip(CardDescription(lane[i].c), {m.x, m.y + 22});
            }
        }
    // each flat's running total, in the band between the two rows
    for (int l = 0; l < LANES; l++) {
        int y = LaneTotal(S.you.lane[l], Snares(S.foe.lane[l])), f = LaneTotal(S.foe.lane[l], Snares(S.you.lane[l]));
        Vector2 c{LANE_X[l], 452};
        bool done = S.phase == Phase::Resolving || S.phase == Phase::MatchOver || S.phase == Phase::RunOver;
        Color col = y > f ? Color{120, 230, 150, 255} : f > y ? Color{240, 110, 100, 255} : Color{210, 210, 200, 255};
        if (done && S.laneResult[l] != 0) DrawRectangleRounded({c.x - 62, c.y - 16, 124, 32}, 0.5f, 6, Fade(col, 0.28f));
        DrawRectangleRounded({c.x - 48, c.y - 13, 96, 26}, 0.5f, 6, Color{8, 12, 16, 200});
        std::string txt = TextFormat("%d : %d", y, f); // you : dealer
        TxtBold(txt, c.x - MeasureTxt(txt, 18, true) / 2.0f, c.y - 10, 18, col);
    }
    DrawBell({1090, 470}, S.phase == Phase::Playing && S.yourTurn && S.actions >= 0, S.bellT);

    // the dealer's hand, as a small fan of backs up by his shoulder
    for (int i = 0; i < (int)S.foe.hand.size(); i++)
        DrawCardFace({930.0f + i * 14, 292.0f - (i % 2) * 3, 34, 48}, Card{}, false);

    // ---------------- HUD
    if (S.phase != Phase::Menu) {
        DrawRectangleRounded({12, 12, 300, 100}, 0.12f, 6, Color{8, 12, 16, 200});
        DrawRectangleRoundedLinesEx({12, 12, 300, 100}, 0.12f, 6, 1.5f, Pal::BrassDk);
        TxtBold("FLATS", 26, 20, 26, Pal::Brass);
        Txt(TextFormat("Match %d of %d  -  round %d", S.match + 1, MATCHES, std::min(3, S.round + 1)), 26, 54, 16, Pal::Paper);
        Txt(TextFormat("Rounds: you %d, dealer %d", S.roundsYou, S.roundsFoe), 26, 76, 16, Pal::Paper);
    }
    DrawPot(g);

    // ---------------- your hand, fanned along the bottom
    int hoverCard = -1;
    int n = (int)S.you.hand.size();
    if (S.phase == Phase::Playing || S.phase == Phase::Resolving) {
        for (int i = 0; i < n; i++) { // hit-test from the top card down
            float off = i - (n - 1) / 2.0f, x = 640 + off * 116, y = 664 - fabsf(off) * 8 - (S.selected == i ? 46 : 0);
            Rectangle hit{x - 56, y - 76, 112, 152};
            if (CheckCollisionPointRec(m, hit)) hoverCard = i;
        }
        for (int i = 0; i < n; i++) {
            float off = i - (n - 1) / 2.0f, x = 640 + off * 116, y = 664 - fabsf(off) * 8 - (S.selected == i ? 46 : (hoverCard == i ? 22 : 0));
            rlPushMatrix();
            rlTranslatef(x, y, 0);
            rlRotatef(off * 4.5f, 0, 0, 1);
            DrawCardFace({-54, -75, 108, 150}, S.you.hand[i], true);
            rlPopMatrix();
        }
        if (hoverCard >= 0) Tooltip(CardDescription(S.you.hand[hoverCard]), {m.x, m.y - 40});
    }

    // ---------------- turn hint
    if (S.phase == Phase::Playing) {
        const char* hint = !S.yourTurn ? "The dealer is playing..."
                           : S.actions > 0 ? TextFormat("Your turn: %d play%s left. Pick a card, then a flat. Ring the bell to end your turn.", S.actions, S.actions == 1 ? "" : "s")
                                            : "Ring the bell.";
        DrawRectangleRounded({SCREEN_W / 2.0f - 340, 14, 680, 26}, 0.5f, 6, Color{8, 12, 16, 190});
        DrawTextCentered(hint, SCREEN_W / 2.0f, 18, 16, Color{214, 222, 226, 255});
    }

    // ---------------- input while playing
    if (S.phase == Phase::Playing && S.yourTurn && !S.showRules) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) S.selected = -1;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (hoverCard >= 0 && S.actions > 0) S.selected = S.selected == hoverCard ? -1 : hoverCard;
            else if (aiming && hoverLane >= 0 && (int)S.you.lane[hoverLane].size() < LANE_CAP) {
                if (PlayCard(S.you, S.selected, hoverLane)) { S.actions--; S.selected = -1; PlaySlap(); }
            } else if (CheckCollisionPointRec(m, BellRect())) {
                S.bellT = 1;
                PlayBell();
                if (HandsEmpty()) Resolve();
                else BeginTurn(false);
                if (S.phase == Phase::Playing) S.actions = std::min(2, (int)S.foe.hand.size());
            }
        }
        if (aiming) DrawCardFace({m.x - 24, m.y - 34, 48, 68}, S.you.hand[S.selected], true); // the card held in hand
    }
    if (S.phase == Phase::Playing && S.yourTurn && CheckCollisionPointRec(m, BellRect())) Tooltip("Ring the bell to end your turn", {m.x, m.y + 22});

    // ---------------- overlays
    Rectangle centre{300, 110, 680, 400};
    if (!S.showRules)
    switch (S.phase) {
        case Phase::Menu: {
            Panel(centre);
            DrawTextCenteredBold("FLATS", centre.x + centre.width / 2, centre.y + 18, 44, Pal::Ink);
            DrawTextCentered(DEALER_LINES[0], centre.x + centre.width / 2, centre.y + 72, 18, Pal::BrassDk);
            DrawWrapped(RULES_TEXT, {centre.x + 34, centre.y + 104, centre.width - 68, 230}, 15, Pal::Ink);
            if (Button({centre.x + centre.width / 2 - 140, centre.y + centre.height - 62, 280, 48}, "Take a seat")) { S.phase = Phase::Playing; StartMatch(); }
            if (BackButton(g)) S.inited = false;
        } break;

        case Phase::Resolving: {
            Rectangle b{340, 250, 600, 96};
            DrawRectangleRounded(b, 0.15f, 8, Color{6, 8, 12, 232});
            DrawRectangleRoundedLinesEx(b, 0.15f, 8, 2, Pal::BrassDk);
            DrawTextCenteredBold(S.banner, b.x + b.width / 2, b.y + 14, 30, Pal::Brass);
            DrawTextCentered(S.sub, b.x + b.width / 2, b.y + 58, 18, Pal::Paper);
        } break;

        case Phase::MatchOver: {
            Panel(centre);
            DrawTextCenteredBold("You win the match!", centre.x + centre.width / 2, centre.y + 28, 38, Pal::Good);
            DrawTextCentered(TextFormat("The pot stands at %d gold.", S.pot), centre.x + centre.width / 2, centre.y + 90, 22, Pal::Ink);
            if (S.match + 1 >= MATCHES) {
                DrawTextCentered("The dealer slides the last of the house's gold across the table. You've cleaned him out.",
                                 centre.x + centre.width / 2, centre.y + 140, 17, Pal::BrassDk);
                if (Button({centre.x + centre.width / 2 - 150, centre.y + 250, 300, 52}, TextFormat("Collect %d gold", S.pot + 100))) {
                    g.gold += S.pot + 100;
                    S.payout = S.pot + 100;
                    S.cashed = true;
                    S.phase = Phase::RunOver;
                }
            } else {
                DrawWrapped("Cash out and walk away with it, or press on: a card for your deck, a sharper dealer, and a bigger pot. "
                            "Lose the next match and all of it stays on the table.",
                            {centre.x + 50, centre.y + 130, centre.width - 100, 100}, 17, Pal::Ink);
                if (Button({centre.x + 50, centre.y + 250, 280, 52}, TextFormat("Cash out %d gold", S.pot))) {
                    g.gold += S.pot;
                    S.payout = S.pot;
                    S.cashed = true;
                    S.phase = Phase::RunOver;
                }
                if (Button({centre.x + 350, centre.y + 250, 280, 52}, "Press on")) MakeRewards();
            }
        } break;

        case Phase::Reward: {
            Panel(centre);
            DrawTextCenteredBold("Choose a card for your deck", centre.x + centre.width / 2, centre.y + 24, 32, Pal::Ink);
            DrawTextCentered(DEALER_LINES[std::min(MATCHES - 1, S.match + 1)], centre.x + centre.width / 2, centre.y + 72, 17, Pal::BrassDk);
            for (int k = 0; k < (int)S.rewards.size(); k++) {
                Rectangle r{centre.x + 60 + k * 205.0f, centre.y + 120, 140, 196};
                bool hov = CheckCollisionPointRec(m, r);
                if (hov) r.y -= 10;
                DrawCardFace(r, S.rewards[k], true);
                if (hov) Tooltip(CardDescription(S.rewards[k]), {m.x, m.y + 22});
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { TakeReward(k); break; }
            }
            Txt(TextFormat("Your deck: %d cards", (int)S.you.deck.size()), centre.x + 60, centre.y + 350, 16, Pal::BrassDk);
        } break;

        case Phase::RunOver: {
            Panel(centre);
            if (S.cashed) {
                DrawTextCenteredBold("You leave the table", centre.x + centre.width / 2, centre.y + 40, 40, Pal::Good);
                DrawTextCentered(TextFormat("%d gold richer.", S.payout), centre.x + centre.width / 2, centre.y + 110, 26, Pal::Ink);
            } else {
                DrawTextCenteredBold("The dealer sweeps the pot", centre.x + centre.width / 2, centre.y + 40, 38, Pal::Bad);
                DrawTextCentered(S.pot > 0 ? TextFormat("%d gold, gone.", S.pot) : "You hadn't won anything yet.", centre.x + centre.width / 2, centre.y + 110, 24, Pal::Ink);
            }
            DrawTextCentered("\"Come back when you've more to lose.\"", centre.x + centre.width / 2, centre.y + 170, 18, Pal::BrassDk);
            if (Button({centre.x + centre.width / 2 - 260, centre.y + 260, 240, 50}, "Deal again")) { ResetRun(); }
            if (Button({centre.x + centre.width / 2 + 20, centre.y + 260, 240, 50}, "Leave the table")) LeaveTable(g);
        } break;

        default: break;
    }

    // leaving mid-run forfeits whatever is unbanked
    if (!S.showRules && (S.phase == Phase::Playing || S.phase == Phase::Resolving || S.phase == Phase::MatchOver || S.phase == Phase::Reward))
        if (Button({20, 122, 150, 34}, S.pot > 0 ? "Fold (lose pot)" : "Fold and leave", true, 14)) LeaveTable(g);

    // ---------------- the "how to play" folder: open at any time, over everything else
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, RulesTabRect())) S.showRules = !S.showRules;
    DrawRulesFolder(m);
}

// ---------------------------------------------------------------- the sprite sheet page
void FlatsSpritePage(float t) {
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    TxtBold("Flats: the cards, the dealer, and the bell", 30, 16, 24, Pal::Brass);
    int x = 30;
    for (int s = 0; s < SUITS; s++)
        for (int v : {2, 5, 9}) {
            Card c{s, v, SP_NONE};
            DrawCardFace({(float)x + (v == 2 ? 0 : v == 5 ? 100 : 200), 60.0f + s * 130, 84, 118}, c, true);
        }
    Card specials[SP_COUNT - 1] = {{COIN, 2, SP_TIDE}, {BLADE, 1, SP_SNARE}, {CUP, 4, SP_LANTERN}, {SHELL, 2, SP_WAVE}};
    for (int i = 0; i < SP_COUNT - 1; i++) DrawCardFace({360.0f + i * 100, 60, 84, 118}, specials[i], true);
    Txt("Specials: Tide, Snare, Lantern, Wave", 360, 184, 15, Pal::Paper);
    DrawCardFace({360, 226, 84, 118}, Card{}, false);
    Txt("Card back", 360, 350, 15, Pal::Paper);
    DrawBell({560, 300}, true, 0);
    Txt("The bell (ends your turn)", 490, 350, 15, Pal::Paper);
    S.beam = 0.1f;
    rlPushMatrix();
    rlTranslatef(600, 120, 0);
    rlScalef(0.75f, 0.75f, 1);
    DrawScale(0.1f);
    rlPopMatrix();
    DrawSkull(t, 1000, 330);
    Txt("The balance, and the candle skull", 700, 350, 15, Pal::Paper);
    rlPushMatrix();
    rlTranslatef(180, 460, 0);
    rlScalef(0.6f, 0.6f, 1);
    DrawDealer(t);
    rlPopMatrix();
    Txt("The dealer", 30, 690, 15, Pal::Paper);
}

// A mid-round position for screenshots: a few cards already on the table and a full hand.
void DebugFlatsDeal() {
    ResetRun();
    S.phase = Phase::Playing;
    StartMatch();
    S.you.lane[0].push_back({{COIN, 6, SP_NONE}, 1});
    S.you.lane[0].push_back({{COIN, 4, SP_NONE}, 1});
    S.you.lane[1].push_back({{BLADE, 5, SP_NONE}, 1});
    S.foe.lane[0].push_back({{CUP, 5, SP_NONE}, 1});
    S.foe.lane[1].push_back({{SHELL, 3, SP_NONE}, 1});
    S.foe.lane[1].push_back({{SHELL, 2, SP_WAVE}, 1});
    S.foe.lane[2].push_back({{BLADE, 7, SP_NONE}, 1});
    S.you.hand.resize(4);
    S.you.hand[0] = {SHELL, 8, SP_NONE};
    S.you.hand[1] = {CUP, 4, SP_LANTERN};
    S.you.hand[2] = {BLADE, 1, SP_SNARE};
    S.you.hand[3] = {COIN, 9, SP_NONE};
}

// The carried items and every relic icon, for the sprite sheet.
void DrawItemSpritePage(float t) {
    (void)t;
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{46, 50, 58, 255}, Color{24, 26, 32, 255});
    TxtBold("Carried items and relics", 30, 16, 24, Pal::Brass);
    const char* names[3] = {"Battery", "Bandage", "Key"};
    for (int i = 0; i < 3; i++) {
        Vector2 c{120.0f + i * 150, 130};
        DrawRectangleRounded({c.x - 40, c.y - 40, 80, 80}, 0.25f, 6, Color{40, 46, 44, 235});
        DrawItemIcon((ItemKind)i, -1, c, 64);
        Txt(names[i], c.x - MeasureTxt(names[i], 16) / 2.0f, c.y + 52, 16, Pal::Paper);
    }
    const auto& relics = Relics();
    for (int i = 0; i < (int)relics.size(); i++) {
        Vector2 c{120.0f + (i % 6) * 190, 300.0f + (i / 6) * 160};
        DrawRectangleRounded({c.x - 40, c.y - 40, 80, 80}, 0.25f, 6, Color{40, 46, 44, 235});
        DrawItemIcon(ItemKind::Relic, i, c, 64);
        std::string n = relics[i].name;
        Txt(n, c.x - MeasureTxt(n, 15) / 2.0f, c.y + 48, 15, Pal::Paper);
        Txt(relics[i].desc, c.x - MeasureTxt(relics[i].desc, 11) / 2.0f, c.y + 68, 11, Color{190, 190, 176, 255});
    }
}

// Plays whole runs headlessly (you at random, the dealer by his own rules) to check the turn logic never
// stalls and to see how often a random player gets how far. Run with:  depth.exe --flats-sim 2000
void FlatsSim(int runs, bool sensible) {
    int reached[MATCHES + 1] = {0, 0, 0, 0}, stalls = 0;
    long steps = 0;
    for (int r = 0; r < runs; r++) {
        ResetRun();
        S.phase = Phase::Playing;
        StartMatch();
        int guard = 0;
        while (S.phase != Phase::RunOver && ++guard < 20000) {
            steps++;
            if (S.phase == Phase::MatchOver) {
                if (S.match + 1 >= MATCHES) { S.cashed = true; S.phase = Phase::RunOver; }
                else MakeRewards();
            } else if (S.phase == Phase::Reward) TakeReward(Roll(0, 2));
            else UpdateFlats(1.0f, true, sensible);
        }
        if (guard >= 20000) stalls++;
        reached[std::min(MATCHES, S.match + (S.cashed ? 1 : 0))]++;
    }
    printf("Flats: %d runs, a %s player against the dealer\n", runs, sensible ? "sensible" : "random");
    for (int m = 0; m <= MATCHES; m++) printf("  matches won: %d   %5.1f%%\n", m, 100.0 * reached[m] / runs);
    printf("  stalled runs: %d, average %.0f steps per run\n", stalls, (double)steps / runs);
}
