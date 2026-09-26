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
#include "relics.h"
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
enum Edition { ED_NONE, ED_FOIL, ED_GILT, ED_HEX, ED_COUNT };
enum LaneMod { LM_NONE, LM_TREASURE, LM_REEF, LM_TRENCH, LM_WHIRL, LM_COUNT };
enum Charm { CH_PEARL, CH_KNOT, CH_COMPASS, CH_TOOTH, CH_ANCHOR, CH_JAR, CH_BARB, CH_COUNT };
struct Card { int suit = COIN; int value = 1; int special = SP_NONE; int ed = ED_NONE; };
const char* SUIT_NAME[SUITS] = {"Coin", "Cup", "Blade", "Shell"};
const char* SPECIAL_NAME[SP_COUNT] = {"", "Tide", "Snare", "Lantern", "Wave"};
const char* SPECIAL_TEXT[SP_COUNT] = {"", "Copies the highest card beside it.", "Cuts the opposing flat by 3.",
                                      "Counts as any suit for pairs.", "+2 for each other card in its flat."};
const char* ED_NAME[ED_COUNT] = {"", "Foil", "Gilt", "Hex"};
const char* ED_TEXT[ED_COUNT] = {"", "+2 in its flat.", "Win its flat: +6 gold in the pot.", "+5 in its flat, but lose 8 gold if the flat is lost."};
const char* MOD_NAME[LM_COUNT] = {"", "Sunken Chest", "Coral Reef", "Trench Current", "Whirlpool"};
const char* MOD_TEXT[LM_COUNT] = {"", "Whoever wins this flat takes 10 gold for the pot.", "Suit and pair bonuses are doubled in this flat.",
                                  "Every card in this flat counts +1.", "Only two cards fit in this flat."};
const char* CHARM_NAME[CH_COUNT] = {"Pearl Necklace", "Sailor's Knot", "Brass Compass", "Lucky Tooth", "Iron Anchor", "Tip Jar", "Barbed Hook"};
const char* CHARM_TEXT[CH_COUNT] = {"Your Shell cards count +1.", "Pairs of a suit are worth +1 more each.", "You may play three cards a turn.",
                                    "You are dealt a sixth card.", "Ties in a flat go to you.", "+10 gold in the pot for each round you win.",
                                    "Your Snares cut 5 instead of 3."};
const Color SUIT_COL[SUITS] = {{200, 148, 42, 255}, {60, 100, 168, 255}, {178, 46, 42, 255}, {64, 152, 142, 255}};

constexpr int LANES = 3, LANE_CAP = 3, HAND_SIZE = 5, MATCHES = 4, MAX_CHARMS = 3;
const int PAYOUT[MATCHES] = {40, 100, 200, 340};
const char* DEALER_NAME[MATCHES] = {"The Novice", "The Tidewife", "The Wreck-Broker", "The House"};
const char* DEALER_LINES[MATCHES] = {"\"Sit. Cards don't bite. Much.\"", "\"You learn quickly. Shame.\"", "\"Now we play for real.\"",
                                     "\"The house always wins. Prove me wrong.\""};

struct Placed { Card c; float age = 0; Vector2 from{0, 0}; float rot = 0; };
struct Side {
    std::vector<Card> deck, hand;
    std::vector<Placed> lane[LANES];
    unsigned charms = 0;
    int boonSuit = -1;
};

int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
void Shuffle(std::vector<Card>& v) { for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[Roll(0, i)]); }
int LaneCap(int mod) { return mod == LM_WHIRL ? 2 : LANE_CAP; }
bool Has(const Side& s, int charm) { return (s.charms >> charm) & 1u; }

// The total of one flat: card values (plus foil, hex, the flat's modifier and the owner's boons), bonuses for sharing
// a suit and for runs, the specials, minus whatever the Snares on the other side of the table cut.
int LaneTotal(const std::vector<Placed>& lane, int cut, int mod, const Side& own) {
    int n = (int)lane.size();
    if (n == 0) return 0;
    int maxv = 1;
    for (auto& p : lane) if (p.c.special != SP_TIDE) maxv = std::max(maxv, p.c.value);
    int total = 0;
    std::vector<int> vals;
    for (auto& p : lane) {
        int v = p.c.special == SP_TIDE ? maxv : p.c.value;
        vals.push_back(v);
        if (p.c.ed == ED_FOIL) v += 2;
        if (p.c.ed == ED_HEX) v += 5;
        if (mod == LM_TRENCH) v += 1;
        if (own.boonSuit == p.c.suit && p.c.special != SP_LANTERN) v += 1;
        if (Has(own, CH_PEARL) && p.c.suit == SHELL) v += 1;
        total += v;
        if (p.c.special == SP_WAVE) total += 2 * (n - 1);
    }
    int cnt[SUITS] = {0}, lanterns = 0;
    for (auto& p : lane) { if (p.c.special == SP_LANTERN) lanterns++; else cnt[p.c.suit]++; }
    int best = 0;
    for (int s = 0; s < SUITS; s++) best = std::max(best, cnt[s]);
    bool lanternsUsed = false;
    int per = (2 + (Has(own, CH_KNOT) ? 1 : 0)) * (mod == LM_REEF ? 2 : 1);
    for (int s = 0; s < SUITS; s++) {
        int k = cnt[s];
        if (!lanternsUsed && k == best) { k += lanterns; lanternsUsed = true; }
        if (k >= 2) total += per * (k - 1);
    }
    std::sort(vals.begin(), vals.end());
    int steps = 0;
    for (size_t i = 1; i < vals.size(); i++) if (vals[i] == vals[i - 1] + 1) steps++;
    total += (n == 3 && steps == 2) ? 4 : steps; // a straight of three: +4; each consecutive pair: +1
    total -= cut;
    return std::max(0, total);
}

// How much the Snares in `lane` (played by `owner`) cut from the flat opposite.
int Snares(const std::vector<Placed>& lane, const Side& owner) {
    int n = 0;
    for (auto& p : lane) if (p.c.special == SP_SNARE) n++;
    return n * (Has(owner, CH_BARB) ? 5 : 3);
}

// ---------------------------------------------------------------- state
enum class Phase { Menu, Playing, Resolving, MatchOver, Reward, RunOver };
struct Particle { Vector2 p, v; float life, max, size; Color col; int kind; }; // kind 0 spark, 1 coin, 2 dust, 3 wisp
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
    int rewardCharm = -1; // when >= 0 the third reward slot is this charm rather than a card
    std::string banner, sub, twistText;
    float resolveT = 0, bellT = 0, beam = 0;
    int laneResult[LANES] = {0, 0, 0}; // after a round: +1 you took it, -1 the dealer did
    int mod[LANES] = {0, 0, 0};
    bool lost = false, cashed = false;
    int payout = 0;
    bool showRules = false, showDeck = false;
    bool foeOpenSnare = false, foeExtraCard = false, foeExtraAct = false;
    float shake = 0, mood = 0, moodT = 0; // mood: +1 the dealer gloats, -1 he flinches
    std::vector<Particle> parts;
};
State S;

const char* RULES_TEXT =
    "Three flats lie across the table. Each round you and the dealer are dealt five cards from your own decks and "
    "take turns laying them, two at a time; ring the bell to end your turn. The higher total in a flat wins it. "
    "Take two flats to take the round, and two rounds to take the match.\n\n"
    "A pair of a suit in one flat: +2 for each extra card. Consecutive values: +1 for each step, +4 for a straight of three. "
    "Specials: Tide copies its best neighbour, Snare cuts the opposite flat by 3, Lantern counts as any suit, Wave adds +2 for every other card beside it.\n\n"
    "Editions: Foil is +2, Gilt pays 6 gold into the pot when its flat wins, Hex is +5 but costs 8 gold if its flat is lost. "
    "Each round one or two flats carry a modifier (Sunken Chest, Coral Reef, Trench Current, Whirlpool); hover the emblem to read it.\n\n"
    "Win a match and choose a reward: a card for your deck, or a charm (you can carry three) that bends the rules your way. "
    "Four dealers, each with a trick of their own; payouts 40, 100, 200 and 340 gold. Cash out after any match, or press on: lose and the pot is gone.";

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
        Rectangle p{SCREEN_W / 2.0f - 360, SCREEN_H / 2.0f - 270, 720, 540};
        Panel(p);
        DrawTextCenteredBold("How Flats is played", p.x + p.width / 2, p.y + 18, 30, Pal::Ink);
        DrawWrapped(RULES_TEXT, {p.x + 34, p.y + 64, p.width - 68, p.height - 130}, 14, Pal::Ink);
        if (Button({p.x + p.width / 2 - 90, p.y + p.height - 56, 180, 42}, "Close")) S.showRules = false;
    }
}

int Tot(const Side& mine, const Side& other, int l) { return LaneTotal(mine.lane[l], Snares(other.lane[l], other), S.mod[l], mine); }

void Burst(Vector2 at, int n, int kind, Color col, float speed) {
    for (int i = 0; i < n; i++) {
        float a = Roll(0, 6283) / 1000.0f, sp = speed * (0.3f + Roll(0, 100) / 100.0f);
        float life = 0.6f + Roll(0, 60) / 100.0f;
        S.parts.push_back({at, {cosf(a) * sp, sinf(a) * sp - (kind == 1 ? speed * 0.6f : 0)}, life, life, 2.0f + Roll(0, 30) / 10.0f, col, kind});
    }
}

Card RandomCard(int lo, int hi) {
    Card c;
    c.suit = Roll(0, SUITS - 1);
    c.value = Roll(lo, hi);
    return c;
}
Card RandomSpecial() {
    Card c = RandomCard(1, 5);
    c.special = Roll(SP_TIDE, SP_COUNT - 1);
    c.value = c.special == SP_SNARE ? 1 : c.special == SP_WAVE ? 2 : c.special == SP_LANTERN ? 4 : 2;
    return c;
}

void BuildDecks() {
    S.you.deck.clear();
    for (int s = 0; s < SUITS; s++) for (int v : {2, 4, 6}) S.you.deck.push_back({s, v, SP_NONE});
}

// Each dealer is a little different: what he does is rolled when you sit down.
void BuildFoeDeck(int match) {
    S.foe.deck.clear();
    int size = 12 + match * 2;
    for (int i = 0; i < size; i++) S.foe.deck.push_back(RandomCard(1, 6 + match));
    for (int i = 0; i < match + 1 && match > 0; i++) S.foe.deck[Roll(0, (int)S.foe.deck.size() - 1)] = RandomSpecial();
    S.foe.boonSuit = -1;
    S.foeOpenSnare = S.foeExtraCard = S.foeExtraAct = false;
    S.twistText = "No tricks. Yet.";
    bool foil = false, pick = Roll(0, 1) == 1;
    if (match == 1) {
        if (pick) { S.foe.boonSuit = Roll(0, SUITS - 1); S.twistText = TextFormat("Her %s cards all count +1.", SUIT_NAME[S.foe.boonSuit]); }
        else { S.foeOpenSnare = true; S.twistText = "She lays a Snare before you have sat down."; }
    } else if (match == 2) {
        if (pick) { foil = true; S.twistText = "His deck is shot through with Foil."; }
        else { S.foeExtraCard = true; S.twistText = "He is dealt a sixth card each round."; }
    } else if (match >= 3) {
        S.foe.boonSuit = Roll(0, SUITS - 1);
        foil = S.foeExtraCard = true;
        S.twistText = TextFormat("Foil, a sixth card, and %s cards count +1.", SUIT_NAME[S.foe.boonSuit]);
    }
    if (foil) for (auto& c : S.foe.deck) if (Roll(1, 100) <= 28) c.ed = ED_FOIL;
}

void ClearLanes() {
    for (int l = 0; l < LANES; l++) { S.you.lane[l].clear(); S.foe.lane[l].clear(); }
}

void RollMods() {
    for (int l = 0; l < LANES; l++) S.mod[l] = LM_NONE;
    int n = S.match == 0 ? (Roll(1, 100) <= 60 ? 1 : 0) : S.match >= 3 ? 2 : 1;
    for (int k = 0; k < n; k++) {
        int l = Roll(0, LANES - 1);
        if (S.mod[l] != LM_NONE) { k--; continue; }
        S.mod[l] = Roll(LM_TREASURE, LM_COUNT - 1);
    }
}

void DealHands() {
    for (Side* s : {&S.you, &S.foe}) {
        std::vector<Card> pile = s->deck;
        Shuffle(pile);
        int want = HAND_SIZE + (s == &S.foe ? (S.foeExtraCard ? 1 : 0) : (Has(S.you, CH_TOOTH) ? 1 : 0));
        int n = std::min((int)pile.size(), want);
        s->hand.assign(pile.begin(), pile.begin() + n);
    }
}

void BeginTurn(bool you);

void StartRound() {
    ClearLanes();
    RollMods();
    DealHands();
    if (S.foeOpenSnare) {
        Placed p;
        p.c = {BLADE, 1, SP_SNARE};
        p.from = {930, 300};
        S.foe.lane[Roll(0, LANES - 1)].push_back(p);
    }
    S.selected = -1;
    S.phase = Phase::Playing;
    BeginTurn(true);
}

void BeginTurn(bool you) {
    S.yourTurn = you;
    Side& side = you ? S.you : S.foe;
    int per = 2 + (you ? (Has(S.you, CH_COMPASS) ? 1 : 0) : (S.foeExtraAct ? 1 : 0));
    S.actions = std::min(per, (int)side.hand.size());
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

// A single card into a flat; it remembers where it came from so it can fly to its place.
bool PlayCard(Side& side, int handIdx, int lane) {
    if (handIdx < 0 || handIdx >= (int)side.hand.size() || (int)side.lane[lane].size() >= LaneCap(S.mod[lane])) return false;
    Placed p;
    p.c = side.hand[handIdx];
    bool mine = &side == &S.you;
    if (mine) {
        float off = handIdx - ((int)side.hand.size() - 1) / 2.0f;
        p.from = {640 + off * 116, 664 - fabsf(off) * 8};
        p.rot = off * 4.5f;
    } else {
        p.from = {930 + handIdx * 14.0f, 300};
        p.rot = -8;
    }
    side.hand.erase(side.hand.begin() + handIdx);
    side.lane[lane].push_back(p);
    return true;
}

bool HandsEmpty() { return S.you.hand.empty() && S.foe.hand.empty(); }
bool CanPlaceAnywhere(const Side& s) {
    for (int l = 0; l < LANES; l++) if ((int)s.lane[l].size() < LaneCap(S.mod[l])) return true;
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
            if ((int)me.lane[l].size() >= LaneCap(S.mod[l])) continue;
            int themTot = LaneTotal(them.lane[l], Snares(me.lane[l], me), S.mod[l], them);
            int before = LaneTotal(me.lane[l], Snares(them.lane[l], them), S.mod[l], me);
            std::vector<Placed> trial = me.lane[l];
            Placed p; p.c = me.hand[i];
            trial.push_back(p);
            int after = LaneTotal(trial, Snares(them.lane[l], them), S.mod[l], me);
            int themAfter = LaneTotal(them.lane[l], Snares(trial, me), S.mod[l], them); // a Snare also cuts what the other side has there
            float u = Sigmoid((float)(after - themAfter)) - Sigmoid((float)(before - themTot));
            if (me.hand[i].special == SP_SNARE) u += 0.06f * (float)(themTot > 0);
            u -= 0.012f * me.hand[i].value;
            if (S.mod[l] == LM_TREASURE) u += 0.05f;
            u += (Roll(0, 1000) / 1000.0f - 0.5f) * noise;
            if (u > bestU) { bestU = u; bc = i; bl = l; }
        }
    if (bc < 0) return false;
    PlayCard(me, bc, bl);
    return true;
}

bool FoePlayOne() {
    static const float NOISE[MATCHES] = {0.22f, 0.10f, 0.05f, 0.02f}; // an early dealer errs more often
    return GreedyPlayOne(S.foe, S.you, NOISE[std::min(S.match, MATCHES - 1)]);
}
float Dist(Vector2 a, Vector2 b) { return hypotf(a.x - b.x, a.y - b.y); }

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

// A small glyph for each special, drawn in the strip under the medallion.
void DrawSpecialGlyph(int sp, Vector2 c, float s, Color col) {
    float w = std::max(1.0f, 0.06f * s);
    switch (sp) {
        case SP_TIDE:
            for (int k = 0; k < 3; k++)
                for (int i = 0; i < 8; i++) {
                    float x0 = c.x - 0.5f * s + i * 0.125f * s, x1 = x0 + 0.125f * s;
                    DrawLineEx({x0, c.y + (k - 1) * 0.2f * s + sinf(i * 0.9f) * 0.07f * s}, {x1, c.y + (k - 1) * 0.2f * s + sinf((i + 1) * 0.9f) * 0.07f * s}, w, col);
                }
            break;
        case SP_SNARE:
            for (int k = -2; k <= 2; k++) {
                DrawLineEx({c.x + k * 0.2f * s, c.y - 0.4f * s}, {c.x + k * 0.2f * s + 0.2f * s, c.y + 0.4f * s}, w, col);
                DrawLineEx({c.x + k * 0.2f * s, c.y + 0.4f * s}, {c.x + k * 0.2f * s + 0.2f * s, c.y - 0.4f * s}, w, col);
            }
            break;
        case SP_LANTERN:
            DrawRing(c, 0.18f * s, 0.22f * s, 0, 360, 14, col);
            for (int k = 0; k < 8; k++) {
                float a = k * PI / 4;
                DrawLineEx({c.x + cosf(a) * 0.3f * s, c.y + sinf(a) * 0.3f * s}, {c.x + cosf(a) * 0.46f * s, c.y + sinf(a) * 0.46f * s}, w, col);
            }
            DrawCircleV(c, 0.11f * s, col);
            break;
        default: // wave
            DrawRing({c.x - 0.15f * s, c.y + 0.2f * s}, 0.24f * s, 0.29f * s, 180, 360, 12, col);
            DrawRing({c.x + 0.25f * s, c.y + 0.2f * s}, 0.24f * s, 0.29f * s, 180, 360, 12, col);
            DrawRing({c.x - 0.15f * s, c.y - 0.1f * s}, 0.16f * s, 0.2f * s, 180, 300, 12, col);
            break;
    }
}

// The edition dressing: a sliding rainbow sheen for Foil, a beaten-gold border with a glint for Gilt, and violet smoke for Hex.
void DrawEdition(Rectangle r, int ed, float u) {
    float t = (float)GetTime();
    if (ed == ED_FOIL) {
        int slices = 18;
        for (int k = 0; k < slices; k++) {
            float a = 0.5f + 0.5f * sinf(k * 0.55f - t * 3.0f);
            a = a * a * a;
            Color c = ColorFromHSV(fmodf(k * 22.0f + t * 90.0f, 360.0f), 0.55f, 1.0f);
            DrawRectangle((int)(r.x + 3 * u), (int)(r.y + 3 * u + k * (r.height - 6 * u) / slices), (int)(r.width - 6 * u), (int)ceilf((r.height - 6 * u) / slices), Fade(c, 0.34f * a));
        }
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, std::max(1.5f, 3 * u), ColorFromHSV(fmodf(t * 80.0f, 360.0f), 0.6f, 1.0f));
    } else if (ed == ED_GILT) {
        Color g{236, 190, 70, 255};
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, std::max(1.5f, 4 * u), g);
        DrawRectangleRoundedLinesEx({r.x + 4 * u, r.y + 4 * u, r.width - 8 * u, r.height - 8 * u}, 0.06f, 6, std::max(1.0f, 1.5f * u), ColorBrightness(g, -0.3f));
        float ph = fmodf(t * 0.7f + r.x * 0.01f, 1.6f);
        if (ph < 1.0f) {
            Vector2 c{r.x + r.width * ph, r.y + r.height * (0.15f + 0.7f * ph)};
            float s = 9 * u * sinf(ph * PI);
            DrawLineEx({c.x - s, c.y}, {c.x + s, c.y}, std::max(1.0f, 1.5f * u), Fade(WHITE, 0.9f));
            DrawLineEx({c.x, c.y - s}, {c.x, c.y + s}, std::max(1.0f, 1.5f * u), Fade(WHITE, 0.9f));
        }
    } else if (ed == ED_HEX) {
        Color v{140, 70, 200, 255};
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, std::max(1.5f, 3 * u), Fade(v, 0.6f + 0.3f * sinf(t * 4)));
        for (int k = 0; k < 5; k++) {
            float ph = fmodf(t * 0.5f + k * 0.2f, 1.0f);
            float x = r.x + r.width * (0.15f + 0.7f * (k / 4.0f)) + sinf(t * 2 + k * 2) * 5 * u;
            DrawCircleV({x, r.y + r.height * (1.0f - ph)}, (3.5f - 2.5f * ph) * u * 1.6f, Fade(v, 0.5f * (1 - ph)));
        }
    }
}

// A card face (or its back) filling `r`. Everything scales from the card's height, so the same routine
// draws a big card in your hand and a small one on the far side of the table.
void DrawCardFace(Rectangle r, const Card& c, bool faceUp) {
    float u = r.height / 150.0f;
    DrawRectangleRounded({r.x + 2 * u, r.y + 3 * u, r.width, r.height}, 0.08f, 6, Fade(BLACK, 0.5f));
    if (!faceUp) {
        Color edge{110, 120, 150, 255};
        DrawRectangleRounded(r, 0.08f, 6, Color{34, 40, 60, 255});
        DrawRectangleRounded({r.x + 3 * u, r.y + 3 * u, r.width - 6 * u, r.height - 6 * u}, 0.06f, 6, Color{44, 52, 76, 255});
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, 1.5f * u + 0.5f, edge);
        DrawRectangleRoundedLinesEx({r.x + 6 * u, r.y + 6 * u, r.width - 12 * u, r.height - 12 * u}, 0.05f, 6, std::max(1.0f, u), Fade(edge, 0.55f));
        Vector2 m{r.x + r.width / 2, r.y + r.height / 2};
        for (int k = -3; k <= 3; k++) { // a lattice of fine diagonals
            DrawLineEx({m.x + k * 0.13f * r.width - 0.3f * r.width, m.y - 0.38f * r.height}, {m.x + k * 0.13f * r.width + 0.3f * r.width, m.y + 0.38f * r.height}, 1, Fade(edge, 0.12f));
            DrawLineEx({m.x + k * 0.13f * r.width + 0.3f * r.width, m.y - 0.38f * r.height}, {m.x + k * 0.13f * r.width - 0.3f * r.width, m.y + 0.38f * r.height}, 1, Fade(edge, 0.12f));
        }
        DrawRing(m, 0.16f * r.width, 0.2f * r.width, 0, 360, 20, edge);
        DrawRing(m, 0.26f * r.width, 0.28f * r.width, 0, 360, 24, Fade(edge, 0.6f));
        DrawCircleV(m, 0.07f * r.width, edge);
        return;
    }
    Color paper{234, 200, 148, 255}, ink{38, 26, 20, 255};
    if (c.ed == ED_HEX) paper = Color{206, 178, 170, 255};
    DrawRectangleRounded(r, 0.08f, 6, ColorBrightness(paper, -0.25f));
    DrawRectangleRounded({r.x + 1.5f * u, r.y + 1.5f * u, r.width - 3 * u, r.height - 3 * u}, 0.08f, 6, paper);
    // foxing and grain, fixed per card so it doesn't crawl
    unsigned h = (unsigned)(c.suit * 131 + c.value * 31 + c.special * 7 + 5);
    for (int k = 0; k < 9; k++) {
        h = h * 1664525u + 1013904223u;
        float fx = ((h >> 8) & 255) / 255.0f, fy = ((h >> 16) & 255) / 255.0f;
        DrawCircleV({r.x + r.width * (0.12f + 0.76f * fx), r.y + r.height * (0.1f + 0.8f * fy)}, (1.5f + (k % 3)) * u, Fade(Color{150, 100, 50, 255}, 0.13f));
    }
    DrawRectangleRoundedLinesEx({r.x + 4 * u, r.y + 4 * u, r.width - 8 * u, r.height - 8 * u}, 0.06f, 6, std::max(1.0f, 1.2f * u), Fade(ink, 0.7f));
    Color col = SUIT_COL[c.suit];
    Vector2 mid{r.x + r.width / 2, r.y + r.height * 0.54f};
    float mr = r.width * 0.42f;
    if (c.special != SP_NONE) { // a coloured header band for the specials
        Rectangle band{r.x + 6 * u, r.y + 6 * u, r.width - 12 * u, 20 * u};
        DrawRectangleRec(band, ColorBrightness(col, -0.15f));
        DrawRectangleRec({band.x, band.y + band.height - 2 * u, band.width, 2 * u}, Fade(BLACK, 0.3f));
        std::string nm = SPECIAL_NAME[c.special];
        for (auto& ch : nm) ch = (char)toupper(ch);
        int fs = std::max(8, (int)(15 * u));
        TxtBold(nm, band.x + band.width / 2 - MeasureTxt(nm, fs, true) / 2.0f, band.y + 2 * u, fs, Color{250, 240, 220, 255});
    }
    // the medallion the suit sits in
    DrawCircleV(mid, mr, Fade(col, 0.16f));
    DrawRing(mid, mr - std::max(1.0f, 1.5f * u), mr, 0, 360, 28, Fade(col, 0.7f));
    DrawRing(mid, mr * 0.86f, mr * 0.86f + std::max(1.0f, u), 0, 360, 28, Fade(ink, 0.25f));
    DrawSuitIcon(c.suit, mid, r.width * 0.56f, col);
    int fs = std::max(9, (int)(30 * u));
    float top = (c.special != SP_NONE ? 28 : 8) * u;
    TxtBold(TextFormat("%d", c.value), r.x + 9 * u, r.y + top, fs, ink);
    DrawSuitIcon(c.suit, {r.x + 9 * u + fs * 0.32f, r.y + top + fs * 1.35f}, fs * 0.5f, col); // a pip under each corner number
    int bw = MeasureTxt(TextFormat("%d", c.value), fs, true);
    TxtBold(TextFormat("%d", c.value), r.x + r.width - 9 * u - bw, r.y + r.height - 8 * u - fs, fs, ink);
    DrawSuitIcon(c.suit, {r.x + r.width - 9 * u - bw + fs * 0.32f, r.y + r.height - 8 * u - fs - fs * 0.5f}, fs * 0.5f, col);
    if (c.special == SP_NONE) {
        std::string nm = SUIT_NAME[c.suit];
        int nf = std::max(7, (int)(12 * u));
        Txt(nm, r.x + r.width / 2 - MeasureTxt(nm, nf) / 2.0f, r.y + r.height - 22 * u, nf, Fade(ink, 0.6f));
    } else {
        DrawSpecialGlyph(c.special, {r.x + r.width / 2, r.y + r.height - 20 * u}, 22 * u, Fade(ink, 0.65f));
    }
    if (c.ed != ED_NONE) DrawEdition(r, c.ed, u);
}

// The charms: brass-set trinkets carried in the pocket, one glyph each.
void DrawCharmIcon(int ch, Vector2 c, float s, float t) {
    Color br = Pal::Brass, dk = Pal::BrassDk;
    DrawCircleV({c.x, c.y + 0.04f * s}, 0.5f * s, Fade(BLACK, 0.4f));
    DrawCircleV(c, 0.5f * s, dk);
    DrawCircleV(c, 0.44f * s, Color{22, 30, 38, 255});
    DrawRing(c, 0.44f * s, 0.5f * s, 0, 360, 24, br);
    float w = std::max(1.0f, 0.06f * s);
    switch (ch) {
        case CH_PEARL:
            DrawCircleV(c, 0.28f * s, Color{232, 228, 218, 255});
            DrawCircleV({c.x - 0.09f * s, c.y - 0.09f * s}, 0.09f * s, WHITE);
            DrawCircleSector(c, 0.28f * s, 20, 130, 10, Fade(Color{120, 130, 170, 255}, 0.4f));
            break;
        case CH_KNOT:
            DrawRing({c.x - 0.11f * s, c.y}, 0.13f * s, 0.19f * s, 0, 360, 16, Color{206, 174, 110, 255});
            DrawRing({c.x + 0.11f * s, c.y}, 0.13f * s, 0.19f * s, 0, 360, 16, Color{186, 154, 90, 255});
            DrawLineEx({c.x - 0.3f * s, c.y + 0.28f * s}, {c.x + 0.3f * s, c.y - 0.28f * s}, w, Color{206, 174, 110, 255});
            break;
        case CH_COMPASS:
            DrawRing(c, 0.26f * s, 0.29f * s, 0, 360, 20, br);
            DrawTri({c.x, c.y - 0.3f * s}, {c.x - 0.07f * s, c.y}, {c.x + 0.07f * s, c.y}, Color{200, 60, 50, 255});
            DrawTri({c.x, c.y + 0.3f * s}, {c.x + 0.07f * s, c.y}, {c.x - 0.07f * s, c.y}, Color{220, 220, 210, 255});
            break;
        case CH_TOOTH:
            DrawTri({c.x - 0.2f * s, c.y - 0.26f * s}, {c.x + 0.2f * s, c.y - 0.26f * s}, {c.x, c.y + 0.3f * s}, Color{236, 230, 210, 255});
            DrawTri({c.x - 0.2f * s, c.y - 0.26f * s}, {c.x, c.y - 0.26f * s}, {c.x, c.y + 0.3f * s}, Color{200, 190, 164, 255});
            break;
        case CH_ANCHOR:
            DrawLineEx({c.x, c.y - 0.3f * s}, {c.x, c.y + 0.3f * s}, w * 1.4f, br);
            DrawLineEx({c.x - 0.14f * s, c.y - 0.16f * s}, {c.x + 0.14f * s, c.y - 0.16f * s}, w, br);
            DrawRing({c.x, c.y - 0.34f * s}, 0.05f * s, 0.09f * s, 0, 360, 10, br);
            DrawRing({c.x, c.y + 0.06f * s}, 0.26f * s, 0.3f * s, 20, 160, 12, br);
            break;
        case CH_JAR:
            DrawRectangleRounded({c.x - 0.18f * s, c.y - 0.16f * s, 0.36f * s, 0.42f * s}, 0.3f, 6, Color{150, 190, 190, 200});
            DrawRectangle((int)(c.x - 0.13f * s), (int)(c.y - 0.26f * s), (int)(0.26f * s), (int)(0.1f * s), dk);
            DrawCircleV({c.x, c.y + 0.16f * s}, 0.07f * s, br);
            DrawCircleV({c.x - 0.07f * s, c.y + 0.1f * s}, 0.06f * s, br);
            break;
        default: // barbed hook
            DrawRing({c.x, c.y + 0.06f * s}, 0.22f * s, 0.27f * s, 0, 200, 14, Color{200, 205, 210, 255});
            DrawLineEx({c.x + 0.22f * s, c.y + 0.06f * s}, {c.x + 0.22f * s, c.y - 0.3f * s}, w, Color{200, 205, 210, 255});
            DrawTri({c.x - 0.22f * s, c.y + 0.06f * s}, {c.x - 0.12f * s, c.y - 0.04f * s}, {c.x - 0.3f * s, c.y - 0.04f * s}, Color{200, 205, 210, 255});
            break;
    }
    (void)t;
}

void DrawCharmCard(Rectangle r, int ch) {
    float u = r.height / 196.0f;
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.08f, 6, Fade(BLACK, 0.5f));
    DrawRectangleRounded(r, 0.08f, 6, Color{28, 36, 44, 255});
    DrawRectangleRoundedLinesEx(r, 0.08f, 6, 3, Pal::Brass);
    DrawRectangleRoundedLinesEx({r.x + 7, r.y + 7, r.width - 14, r.height - 14}, 0.06f, 6, 1, Fade(Pal::Brass, 0.5f));
    TxtBold("CHARM", r.x + r.width / 2 - MeasureTxt("CHARM", 13, true) / 2.0f, r.y + 12 * u + 2, 13, Pal::Brass);
    Glow({r.x + r.width / 2, r.y + r.height * 0.4f}, r.width * 0.5f, Fade(Pal::Brass, 0.3f));
    DrawCharmIcon(ch, {r.x + r.width / 2, r.y + r.height * 0.4f}, r.width * 0.62f, 0);
    std::string nm = CHARM_NAME[ch];
    Txt(nm, r.x + r.width / 2 - MeasureTxt(nm, 14) / 2.0f, r.y + r.height * 0.68f, 14, Pal::Paper);
    DrawWrapped(CHARM_TEXT[ch], {r.x + 12, r.y + r.height * 0.76f, r.width - 24, r.height * 0.22f}, 11, Fade(Pal::Paper, 0.75f));
}

// The emblem on a flat that carries a modifier.
void DrawModEmblem(int mod, Vector2 c, float t) {
    Color cols[LM_COUNT] = {WHITE, {236, 190, 70, 255}, {230, 110, 130, 255}, {90, 200, 230, 255}, {150, 110, 220, 255}};
    Color col = cols[mod];
    Glow(c, 34, Fade(col, 0.22f + 0.08f * sinf(t * 3)));
    DrawCircleV(c, 15, Color{10, 14, 20, 235});
    DrawRing(c, 13, 15.5f, 0, 360, 20, col);
    switch (mod) {
        case LM_TREASURE:
            DrawRectangleRounded({c.x - 8, c.y - 3, 16, 10}, 0.3f, 4, col);
            DrawCircleSector({c.x, c.y - 3}, 8, 270, 450, 10, ColorBrightness(col, 0.2f));
            DrawRectangle((int)c.x - 1, (int)c.y - 1, 2, 4, Color{20, 20, 20, 255});
            break;
        case LM_REEF:
            for (int k = -1; k <= 1; k++) { DrawLineEx({c.x + k * 5, c.y + 8}, {c.x + k * 5, c.y - 4 + abs(k) * 3}, 2, col); DrawCircleV({c.x + k * 5, c.y - 5 + abs(k) * 3}, 2.4f, col); }
            break;
        case LM_TRENCH:
            for (int k = -1; k <= 1; k++) DrawRing({c.x - 4, c.y + k * 5}, 4, 5.4f, 200, 340, 6, col);
            break;
        default:
            DrawRing(c, 3, 5, 0, 300, 10, col);
            DrawRing(c, 6, 8, 60, 360, 12, col);
            DrawRing(c, 9, 11, 120, 420, 14, col);
            break;
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
// The dealer sits in shadow, but he is never lost in it: the room around him is black, and he is picked out
// in solid ink-black masses with a thin cold rim of light along his hood and shoulders, a brow that hides
// his eyes, and two pinpricks of light beneath it. Hard block shadows, no gradients.
void DrawDealer(float t) {
    // The same character as the one at the table in the salon, drawn large: a hooded cloak, a face like old candle wax,
    // eyes that catch the cold light, gloved hands, a brass-trimmed mantle, a glowing brooch. Built in layers, back to front.
    const float cx = 640, sx = 4.0f, sy = 3.1f, base = 44 + 180 * sy, bob = sinf(t * 1.1f) * 2.0f;
    auto V = [&](float dx, float dy) { return Vector2{cx + dx * sx, base + dy * sy + bob}; };
    const Color ink{6, 8, 14, 255}, cloak{26, 30, 48, 255}, cloakLt{36, 42, 64, 255}, cloakDk{14, 16, 28, 255}, glove{34, 34, 40, 255};
    const Color wax{132, 134, 132, 255}, waxDk{74, 76, 80, 255}, rim{92, 150, 190, 255}, brass{176, 140, 70, 255}, gem{90, 200, 220, 255};
    auto Ell = [&](Vector2 c, float rx, float ry, Color col) { DrawEllipse((int)c.x, (int)c.y, rx, ry, col); };
    auto Quad = [&](Vector2 a, Vector2 b2, Vector2 c2, Vector2 d, Color col) { DrawTri(a, b2, c2, col); DrawTri(a, c2, d, col); };
    // 1. the back mantle, the widest and darkest layer
    { Vector2 c = V(0, -76); Ell(c, 52 * sx + 6, 64 * sy + 6, ink); Ell(c, 52 * sx, 64 * sy, cloakDk); }
    // 2. the cloak, with a lit left edge and long folds
    Ell(V(0, -118), 47 * sx + 6, 24 * sy + 6, ink); Ell(V(0, -118), 47 * sx, 24 * sy, cloak);       // rounded, sloping shoulders
    Quad(V(-38, -120), V(38, -120), V(44, -24), V(-44, -24), ink);
    Quad(V(-35, -120), V(35, -120), V(41, -26), V(-41, -26), cloak);
    Quad(V(-35, -120), V(-18, -120), V(-22, -26), V(-41, -26), cloakLt);
    for (int i = -2; i <= 2; i++) DrawLineEx(V(i * 9.0f, -112), V(i * 10.0f + (i > 0 ? 3.0f : -3.0f), -30), 3, cloakDk);
    DrawLineEx(V(-46, -112), V(-44, -24), 3, Fade(rim, 0.8f));                              // the cold rim light down the left edge
    // 3. the arms and gloved hands, resting on the felt
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        Vector2 sh = V(s2 * 30.0f, -124), hd = V(s2 * 40.0f, -104);
        DrawLineEx(sh, hd, 66, ink); DrawLineEx(sh, hd, 58, cloak);
        if (s2 < 0) DrawLineEx({sh.x - 20, sh.y}, {hd.x - 20, hd.y}, 10, cloakLt);
        Vector2 cf = V(s2 * 39.5f, -107);
        DrawLineEx({cf.x - 30, cf.y}, {cf.x + 30, cf.y}, 16, ink); DrawLineEx({cf.x - 28, cf.y}, {cf.x + 28, cf.y}, 11, brass);   // a brass cuff
        Vector2 g = V(s2 * 40.5f, -98);
        Ell(g, 34, 26, ink); Ell(g, 30, 22, glove);
        for (int i = -1; i <= 1; i++) { DrawLineEx({g.x + i * 13.0f, g.y + 10}, {g.x + i * 13.0f + s2 * 3.0f, g.y + 26}, 11, ink); DrawLineEx({g.x + i * 13.0f, g.y + 10}, {g.x + i * 13.0f + s2 * 3.0f, g.y + 26}, 7, glove); }
        DrawRing({g.x + 13.0f * s2, g.y + 20}, 5, 9, 0, 360, 10, brass);                    // a ring on a finger
        DrawLineEx({g.x - 24, g.y - 12}, {g.x - 8, g.y - 16}, 2, Fade(rim, 0.7f));         // a thin edge of light on the knuckles
    }
    // 4. the mantle over the shoulders, trimmed in brass, with tassels
    Ell(V(0, -118), 41 * sx + 5, 13 * sy + 5, ink); Ell(V(0, -118), 41 * sx, 13 * sy, cloakLt);
    Ell(V(-6, -120), 30 * sx, 8 * sy, Color{48, 58, 88, 255});
    DrawLineEx(V(-38, -110), V(38, -110), 5, brass);
    for (int i = -2; i <= 2; i++) { DrawLineEx(V(i * 15.0f, -109), V(i * 15.0f, -99), 3, brass); Ell(V(i * 15.0f, -98), 5, 5, brass); }
    // 5. the high collar and the hood: peak, lit half, lining, rim light
    DrawTri(V(-34, -126), V(34, -126), V(0, -152), ink);
    DrawTri(V(-30, -126), V(30, -126), V(0, -148), cloakDk);
    DrawTri(V(-27, -140), V(27, -140), V(1, -178), ink);                                   // a soft peak to the hood, not a cone
    DrawTri(V(-25, -141), V(25, -141), V(1, -174), cloak);
    DrawTri(V(-25, -141), V(0, -141), V(-1, -174), cloakLt);
    DrawTri(V(-38, -122), V(-20, -150), V(-14, -122), cloak); DrawTri(V(38, -122), V(20, -150), V(14, -122), cloak); // the hood falls in folds to the shoulders
    DrawLineEx(V(-38, -122), V(-24, -156), 3, Fade(rim, 0.9f)); DrawLineEx(V(-25, -141), V(1, -178), 3, Fade(rim, 0.9f));
    { Vector2 c = V(0, -160); Ell(c, 25 * sx + 5, 25 * sy + 5, ink); Ell(c, 25 * sx, 25 * sy, cloak); Ell(V(0, -159), 21 * sx, 21 * sy, cloakDk); } // the hood and its darker lining
    for (int i = -6; i <= 6; i++) { Vector2 p = V(i * 3.4f, -176 + fabsf(i) * 3.6f); DrawRectangle((int)p.x - 2, (int)p.y, 4, 4, Fade(brass, 0.6f)); } // stitched trim
    // 6. the face: mostly shadow, one lit cheek, a brow that hides the eyes
    Ell(V(1, -158), 15.5f * sx, 15.5f * sy, waxDk);
    Ell(V(-3.5f, -156), 10 * sx, 13 * sy, wax);
    for (int s2 = -1; s2 <= 1; s2 += 2) Ell(V(s2 * 8.0f, -149), 3.2f * sx, 3.6f * sy, Fade(BLACK, 0.3f));   // sunken cheeks
    DrawLineEx(V(1, -161), V(2.5f, -150), 5, waxDk); Ell(V(3, -149), 9, 4, Fade(BLACK, 0.4f));               // the nose
    { Vector2 b0 = V(-14.5f, -169); DrawRectangle((int)b0.x, (int)b0.y, (int)(29 * sx), (int)(11 * sy), ink); } // the brow: a black bar
    DrawLineEx(V(-8, -142), V(8, -142), 4, Color{4, 4, 8, 255});                                             // a flat mouth
    Vector2 m = GetMousePosition();
    float lookx = std::clamp((m.x - cx) * 0.012f, -4.0f, 4.0f);
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        Vector2 e = V(s2 * 6.5f, -160);
        e.x += lookx;
        Glow(e, 28, Color{150, 170, 255, 70});
        DrawRectangle((int)e.x - 12, (int)e.y - 3, 24, 6, Color{220, 232, 255, 255});                        // just a sliver of light in the dark
    }
    // 7. the brooch at the throat, its gem the colour of the table's glow, and a watch chain
    { Vector2 c = V(0, -124); Ell(c, 16, 15, ink); Ell(c, 13, 12, brass); Ell(c, 8, 7, gem); Glow(c, 40, Color{90, 200, 220, (unsigned char)(80 + 30 * sinf(t * 2))}); }
    DrawLineEx(V(0, -121), V(-16, -101), 3, ink); DrawLineEx(V(0, -121), V(-16, -101), 1.6f, brass); Ell(V(-16, -99), 9, 9, ink); Ell(V(-16, -99), 6.5f, 6.5f, brass);
}void DrawSkull(float t, float x, float y) {
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
        int y = Tot(S.you, S.foe, i), f = Tot(S.foe, S.you, i);
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
    int you = 0, foe = 0, yourSum = 0, foeSum = 0, gain = 0;
    for (int l = 0; l < LANES; l++) {
        int y = Tot(S.you, S.foe, l), f = Tot(S.foe, S.you, l);
        int res = y > f ? 1 : f > y ? -1 : (Has(S.you, CH_ANCHOR) && y > 0 ? 1 : 0);
        S.laneResult[l] = res;
        you += res > 0; foe += res < 0;
        yourSum += y; foeSum += f;
        Vector2 c{LANE_X[l], 452};
        if (res > 0) {
            for (auto& p : S.you.lane[l]) if (p.c.ed == ED_GILT) gain += 6;
            if (S.mod[l] == LM_TREASURE) gain += 10;
            Burst(c, 14, 1, Color{240, 200, 80, 255}, 220);
        } else if (res < 0) {
            for (auto& p : S.you.lane[l]) if (p.c.ed == ED_HEX) gain -= 8;
            Burst(c, 8, 3, Color{150, 80, 210, 255}, 60);
        }
    }
    int winner = you > foe ? 1 : foe > you ? -1 : yourSum > foeSum ? 1 : foeSum > yourSum ? -1 : 0;
    if (winner > 0) { S.roundsYou++; S.banner = "You take the round"; if (Has(S.you, CH_JAR)) gain += 10; }
    else if (winner < 0) { S.roundsFoe++; S.banner = "The dealer takes the round"; }
    else S.banner = "A dead heat: the round is dealt again";
    S.pot = std::max(0, S.pot + gain);
    S.sub = TextFormat("Flats %d - %d   (totals %d - %d)%s", you, foe, yourSum, foeSum, gain != 0 ? TextFormat("   pot %+d", gain) : "");
    if (winner == 0) S.round--; // replay
    S.round++;
    S.phase = Phase::Resolving;
    S.resolveT = 2.6f;
    S.shake = 0.5f;
    S.mood = (float)-winner; // he gloats when he wins, flinches when he loses
    S.moodT = 1.6f;
}

void MakeRewards() {
    S.rewards.clear();
    S.rewardCharm = -1;
    for (int k = 0; k < 3; k++) {
        Card c = RandomCard(5, 9);
        if (Roll(1, 100) <= 40) c = RandomSpecial();
        if (Roll(1, 100) <= 38) c.ed = Roll(ED_FOIL, ED_COUNT - 1);
        S.rewards.push_back(c);
    }
    int owned = 0;
    std::vector<int> free;
    for (int i = 0; i < CH_COUNT; i++) { if (Has(S.you, i)) owned++; else free.push_back(i); }
    if (owned < MAX_CHARMS && !free.empty()) S.rewardCharm = free[Roll(0, (int)free.size() - 1)];
    S.phase = Phase::Reward;
}

void TakeReward(int k) {
    if (k == 2 && S.rewardCharm >= 0) S.you.charms |= 1u << S.rewardCharm;
    else S.you.deck.push_back(S.rewards[k]);
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

// The pot, as stacks of coins on the table.
void DrawChips(Vector2 base, int pot, float t) {
    int n = std::min(25, pot / 12);
    for (int k = 0; k < n; k++) {
        int col = k / 5, row = k % 5;
        Vector2 c{base.x + col * 30.0f + (row % 2) * 1.5f, base.y - row * 6.0f + (col % 2) * 6};
        DrawEllipse((int)c.x, (int)c.y + 3, 16, 6, Fade(BLACK, 0.5f));
        DrawEllipse((int)c.x, (int)c.y + 2, 15, 6, Pal::BrassDk);
        DrawEllipse((int)c.x, (int)c.y, 15, 6, Pal::Brass);
        DrawEllipse((int)c.x, (int)c.y - 1, 9, 3, ColorBrightness(Pal::Brass, 0.3f));
    }
    if (n > 0) Glow({base.x + 30, base.y - 10}, 50 + 4 * sinf(t * 3), Fade(Pal::Brass, 0.12f));
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
    std::string s = c.special != SP_NONE ? TextFormat("%s (%d): %s", SPECIAL_NAME[c.special], c.value, SPECIAL_TEXT[c.special])
                                         : TextFormat("%s %d", SUIT_NAME[c.suit], c.value);
    if (c.ed != ED_NONE) s += TextFormat("  |  %s: %s", ED_NAME[c.ed], ED_TEXT[c.ed]);
    return s;
}

// One frame of the rules: the dealer's plays, resolving a round, and easing the balance. With autoYou
// set (the headless simulation) your own turn is played at random too.
void UpdateFlats(float dt, bool autoYou, bool sensible) {
    bool live = dt < 0.5f;
    S.bellT = std::max(0.0f, S.bellT - dt * 1.1f);
    S.shake = std::max(0.0f, S.shake - dt * 1.4f);
    S.moodT = std::max(0.0f, S.moodT - dt);
    for (int l = 0; l < LANES; l++)
        for (bool you : {true, false}) {
            auto& lane = (you ? S.you : S.foe).lane[l];
            for (int i = 0; i < (int)lane.size(); i++) {
                float before = lane[i].age;
                lane[i].age = std::min(1.0f, before + dt * 3.2f);
                if (live && before < 1 && lane[i].age >= 1) {
                    Rectangle r = SlotRect(you, l, i);
                    Burst({r.x + r.width / 2, r.y + r.height - 4}, 5, 2, Color{150, 130, 110, 255}, 40);
                    if (lane[i].c.ed == ED_FOIL) Burst({r.x + r.width / 2, r.y + r.height / 2}, 6, 0, Color{200, 240, 255, 255}, 90);
                    if (lane[i].c.ed == ED_GILT) Burst({r.x + r.width / 2, r.y + r.height / 2}, 6, 0, Color{255, 220, 110, 255}, 90);
                    if (lane[i].c.ed == ED_HEX) Burst({r.x + r.width / 2, r.y + r.height / 2}, 5, 3, Color{150, 80, 210, 255}, 40);
                }
            }
        }
    if (live) {
        for (auto& p : S.parts) {
            p.life -= dt;
            p.p.x += p.v.x * dt; p.p.y += p.v.y * dt;
            if (p.kind == 1) p.v.y += 900 * dt;
            else if (p.kind == 2) { p.v.x *= 0.92f; p.v.y *= 0.92f; }
            else if (p.kind == 3) { p.v.y -= 40 * dt; p.v.x *= 0.98f; }
        }
        S.parts.erase(std::remove_if(S.parts.begin(), S.parts.end(), [](const Particle& p) { return p.life <= 0; }), S.parts.end());
    } else S.parts.clear();

    if (S.phase == Phase::Playing) {
        // the dealer takes his plays one at a time, with a pause between, then it's your turn
        if (!S.yourTurn) {
            S.foeT -= dt;
            if (S.foeT <= 0) {
                if (S.actions > 0 && FoePlayOne()) { S.actions--; S.foeT = 0.85f; if (live) PlaySlap(); }
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
        int y = Tot(S.you, S.foe, l), f = Tot(S.foe, S.you, l);
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
                for (int l = 0; l < LANES; l++) if ((int)S.you.lane[l].size() < LaneCap(S.mod[l])) open.push_back(l);
                if (!open.empty()) { PlayCard(S.you, Roll(0, (int)S.you.hand.size() - 1), open[Roll(0, (int)open.size() - 1)]); S.actions--; }
                else S.you.hand.clear();
            }
        } else {
            if (HandsEmpty()) Resolve(); else BeginTurn(false);
        }
    }
}

// A card on the table: it flies in from where it was played on an arc, and the dealer's cards turn over as they land.
void DrawPlaced(bool you, int l, int i, float t) {
    Placed& p = (you ? S.you : S.foe).lane[l][i];
    Rectangle r = SlotRect(you, l, i);
    float e = p.age, ease = 1 - powf(1 - e, 3);
    Vector2 tc{r.x + r.width / 2, r.y + r.height / 2};
    Vector2 pos{p.from.x + (tc.x - p.from.x) * ease, p.from.y + (tc.y - p.from.y) * ease - sinf(e * PI) * 70};
    Vector2 fsz = you ? Vector2{108, 150} : Vector2{34, 48};
    float w = fsz.x + (r.width - fsz.x) * ease, h = fsz.y + (r.height - fsz.y) * ease;
    if (e >= 1) { pos = tc; w = r.width; h = r.height; }
    // the winning flats bob and glow once a round is settled, the losing ones dim
    bool settled = S.phase == Phase::Resolving || S.phase == Phase::MatchOver || S.phase == Phase::RunOver;
    if (settled && S.laneResult[l] != 0) {
        bool won = (S.laneResult[l] > 0) == you;
        Color gc = won ? Color{120, 240, 150, 255} : Color{240, 100, 90, 255};
        if (won) pos.y -= 4 + 3 * sinf(t * 6 + i);
        Glow(pos, w * (won ? 1.1f : 0.6f), Fade(gc, won ? 0.30f : 0.10f));
    } else if (e < 1) {
        DrawEllipse((int)tc.x, (int)(tc.y + h / 2), (int)(w * 0.5f * e), (int)(4 * e), Fade(BLACK, 0.4f * e));
    }
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, 0);
    rlRotatef(p.rot * (1 - ease), 0, 0, 1);
    bool faceUp = you || e >= 0.45f;
    float flip = (you || e >= 0.7f) ? 1.0f : std::max(0.05f, (e - 0.45f) / 0.25f);
    if (!you && e >= 0.45f && e < 0.7f) faceUp = true;
    DrawCardFace({-w * flip / 2, -h / 2, w * flip, h}, p.c, faceUp);
    rlPopMatrix();
}

void DrawParticles() {
    for (auto& p : S.parts) {
        float a = std::clamp(p.life / p.max, 0.0f, 1.0f);
        switch (p.kind) {
            case 0: Glow(p.p, p.size * 4, Fade(p.col, 0.5f * a)); DrawCircleV(p.p, p.size * 0.7f, Fade(WHITE, a)); break;
            case 1:
                DrawEllipse((int)p.p.x, (int)p.p.y, p.size * 2.2f, p.size * 1.6f, Fade(Pal::BrassDk, a));
                DrawEllipse((int)p.p.x, (int)p.p.y - 1, p.size * 1.8f, p.size * 1.2f, Fade(p.col, a));
                break;
            case 2: DrawCircleV(p.p, p.size * (2 - a), Fade(p.col, 0.4f * a)); break;
            default: DrawCircleV(p.p, p.size * (0.6f + 0.6f * a), Fade(p.col, 0.5f * a)); break;
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
    bool modal = S.showRules || S.showDeck;

    UpdateFlats(dt, false, false);
    Vector2 sh{sinf(t * 91) * S.shake * 9, cosf(t * 77) * S.shake * 9};

    // ---------------- draw the room
    ClearBackground(Color{4, 5, 8, 255});
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    rlPushMatrix();
    rlTranslatef(sh.x, sh.y, 0);
    // hanging chains and dim shapes in the dark around the dealer, as in the reference
    for (int k = 0; k < 6; k++) {
        float x = 80 + k * 220 + sinf(t * 0.3f + k) * 4;
        DrawLineEx({x, 0}, {x + 20, 200.0f + (k % 3) * 40}, 3, Color{18, 22, 30, 255});
        for (int j = 0; j < 4; j++) DrawCircleLines((int)(x + 20 * (j / 4.0f)), 50 + j * 40, 5, Color{24, 30, 40, 255});
    }
    {
        float bob = 0, lean = 0;
        if (S.moodT > 0) {
            float f = std::min(1.0f, S.moodT);
            if (S.mood > 0) bob = -6 * fabsf(sinf(t * 9)) * f; else lean = sinf(t * 42) * 3 * f;
        } else if (S.phase == Phase::Playing && !S.yourTurn) bob = sinf(t * 3) * 1.5f;
        rlPushMatrix();
        rlTranslatef(lean, bob, 0);
        DrawDealer(t);
        rlPopMatrix();
    }
    DrawTable();
    DrawSkull(t, 300, 372);
    DrawScale(S.beam);
    DrawChips({150, 560}, S.pot, t);
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
    bool aiming = S.phase == Phase::Playing && S.yourTurn && S.selected >= 0 && S.actions > 0 && !modal;
    for (int l = 0; l < LANES; l++) {
        for (int i = 0; i < LANE_CAP; i++) {
            if (i >= LaneCap(S.mod[l])) { // the whirlpool has swallowed this slot
                for (bool you : {false, true}) {
                    Rectangle r = SlotRect(you, l, i);
                    DrawRectangleRounded(r, 0.1f, 6, Color{14, 8, 26, 220});
                    DrawLineEx({r.x + 8, r.y + 8}, {r.x + r.width - 8, r.y + r.height - 8}, 2, Fade(Color{150, 110, 220, 255}, 0.5f));
                    DrawLineEx({r.x + r.width - 8, r.y + 8}, {r.x + 8, r.y + r.height - 8}, 2, Fade(Color{150, 110, 220, 255}, 0.5f));
                }
                continue;
            }
            DrawSlot(SlotRect(false, l, i), false, t);
            bool free = i == (int)S.you.lane[l].size();
            DrawSlot(SlotRect(true, l, i), aiming && hoverLane == l && free, t);
        }
    }
    rlPopMatrix();

    // ---------------- lights: the candles at the skull, and a cold glow over the flats
    float fl = 1 + 0.06f * sinf(t * 13) + 0.04f * sinf(t * 29 + 1);
    LightsBegin(Color{22, 24, 32, 255}); // the room is in shadow; the dealer is lit on purpose below
    AddLight({300, 340}, 380 * fl, Color{255, 190, 110, 255}, 0.95f);
    AddLight({640, 470}, 620, Color{80, 150, 200, 255}, 0.6f);
    AddLight({640, 170}, 330, Color{150, 165, 205, 255}, 1.0f);   // the dealer: dark, but always readable
    AddLight({640, 330}, 260, Color{70, 150, 190, 255}, 0.8f);    // and lit from below by the flats
    AddLight({124, 350}, 260 * fl, Color{255, 200, 120, 255}, 0.4f);
    AddLight({1090, 460}, 200, Color{255, 190, 110, 255}, 0.45f);
    if (S.pot > 0) AddLight({170, 550}, 150, Color{255, 200, 100, 255}, 0.3f);
    for (int l = 0; l < LANES; l++) {
        if (S.mod[l] == LM_TREASURE) AddLight({LANE_X[l], 452}, 150, Color{255, 200, 90, 255}, 0.35f);
        if (S.mod[l] == LM_WHIRL) AddLight({LANE_X[l], 452}, 150, Color{140, 90, 220, 255}, 0.35f);
    }
    LightsEnd();
    InkPass(0.55f, 0.7f);

    // ---------------- cards on the table (drawn crisp, after the ink pass)
    rlPushMatrix();
    rlTranslatef(sh.x * 0.6f, sh.y * 0.6f, 0);
    bool settled = S.phase == Phase::Resolving || S.phase == Phase::MatchOver || S.phase == Phase::RunOver;
    for (int l = 0; l < LANES; l++)
        for (bool you : {false, true}) {
            auto& lane = (you ? S.you : S.foe).lane[l];
            for (int i = 0; i < (int)lane.size(); i++) {
                DrawPlaced(you, l, i, t);
                Rectangle r = SlotRect(you, l, i);
                if (!modal && CheckCollisionPointRec(m, r) && lane[i].age >= 1) Tooltip(CardDescription(lane[i].c), {m.x, m.y + 22});
            }
        }
    // each flat's running total, in the band between the two rows
    for (int l = 0; l < LANES; l++) {
        int y = Tot(S.you, S.foe, l), f = Tot(S.foe, S.you, l);
        Vector2 c{LANE_X[l], 452};
        Color col = y > f ? Color{120, 230, 150, 255} : f > y ? Color{240, 110, 100, 255} : Color{210, 210, 200, 255};
        if (settled && S.laneResult[l] != 0) DrawRectangleRounded({c.x - 62, c.y - 16, 124, 32}, 0.5f, 6, Fade(col, 0.28f));
        DrawRectangleRounded({c.x - 48, c.y - 13, 96, 26}, 0.5f, 6, Color{8, 12, 16, 200});
        std::string txt = TextFormat("%d : %d", y, f); // you : dealer
        TxtBold(txt, c.x - MeasureTxt(txt, 18, true) / 2.0f, c.y - 10, 18, col);
        if (S.mod[l] != LM_NONE) {
            Vector2 e{c.x - 84, c.y};
            DrawModEmblem(S.mod[l], e, t);
            if (!modal && Dist(m, e) < 18) Tooltip(std::string(MOD_NAME[S.mod[l]]) + ": " + MOD_TEXT[S.mod[l]], {m.x, m.y + 24});
        }
    }
    DrawBell({1090, 470}, S.phase == Phase::Playing && S.yourTurn && S.actions >= 0, S.bellT);

    // the dealer's hand, as a small fan of backs up by his shoulder
    for (int i = 0; i < (int)S.foe.hand.size(); i++)
        DrawCardFace({930.0f + i * 14, 292.0f - (i % 2) * 3, 34, 48}, Card{}, false);
    DrawParticles();
    rlPopMatrix();

    // ---------------- HUD
    if (S.phase != Phase::Menu) {
        DrawRectangleRounded({12, 12, 300, 100}, 0.12f, 6, Color{8, 12, 16, 200});
        DrawRectangleRoundedLinesEx({12, 12, 300, 100}, 0.12f, 6, 1.5f, Pal::BrassDk);
        TxtBold("FLATS", 26, 18, 24, Pal::Brass);
        Txt(DEALER_NAME[std::min(S.match, MATCHES - 1)], 116, 24, 16, Pal::Paper);
        Txt(TextFormat("Match %d of %d  -  round %d", S.match + 1, MATCHES, std::min(3, S.round + 1)), 26, 54, 16, Pal::Paper);
        Txt(TextFormat("Rounds: you %d, dealer %d", S.roundsYou, S.roundsFoe), 26, 76, 16, Pal::Paper);
        // the charms in your pocket
        int shown = 0;
        for (int c = 0; c < CH_COUNT; c++) {
            if (!Has(S.you, c)) continue;
            Vector2 cc{200.0f + shown * 42, 148};
            DrawCharmIcon(c, cc, 34, t);
            if (!modal && Dist(m, cc) < 18) Tooltip(std::string(CHARM_NAME[c]) + ": " + CHARM_TEXT[c], {m.x, m.y + 24});
            shown++;
        }
        if (!modal && S.phase != Phase::RunOver && Button({20, 166, 150, 30}, TextFormat("Deck (%d)", (int)S.you.deck.size()), true, 14)) S.showDeck = true;
    }
    DrawPot(g);

    // ---------------- your hand, fanned along the bottom
    int hoverCard = -1;
    int n = (int)S.you.hand.size();
    if (S.phase == Phase::Playing || S.phase == Phase::Resolving) {
        for (int i = 0; i < n; i++) { // hit-test from the top card down
            float off = i - (n - 1) / 2.0f, x = 640 + off * 116, y = 664 - fabsf(off) * 8 - (S.selected == i ? 46 : 0);
            Rectangle hit{x - 56, y - 76, 112, 152};
            if (CheckCollisionPointRec(m, hit) && !modal) hoverCard = i;
        }
        for (int i = 0; i < n; i++) {
            float off = i - (n - 1) / 2.0f, x = 640 + off * 116, y = 664 - fabsf(off) * 8 - (S.selected == i ? 46 : (hoverCard == i ? 22 : 0));
            y += sinf(t * 1.6f + i) * 1.5f;
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
        DrawTextCentered(TextFormat("%s: %s", DEALER_NAME[std::min(S.match, MATCHES - 1)], S.twistText.c_str()), SCREEN_W / 2.0f, 46, 14, Fade(Pal::Brass, 0.85f));
    }

    // ---------------- input while playing
    if (S.phase == Phase::Playing && S.yourTurn && !modal) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) S.selected = -1;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (hoverCard >= 0 && S.actions > 0) S.selected = S.selected == hoverCard ? -1 : hoverCard;
            else if (aiming && hoverLane >= 0 && (int)S.you.lane[hoverLane].size() < LaneCap(S.mod[hoverLane])) {
                if (PlayCard(S.you, S.selected, hoverLane)) { S.actions--; S.selected = -1; PlaySlap(); }
            } else if (CheckCollisionPointRec(m, BellRect())) {
                S.bellT = 1;
                S.shake = std::max(S.shake, 0.12f);
                PlayBell();
                if (HandsEmpty()) Resolve();
                else BeginTurn(false);
            }
        }
        if (aiming && S.selected >= 0 && S.selected < (int)S.you.hand.size()) DrawCardFace({m.x - 24, m.y - 34, 48, 68}, S.you.hand[S.selected], true); // the card held in hand
    }
    if (S.phase == Phase::Playing && S.yourTurn && !modal && CheckCollisionPointRec(m, BellRect())) Tooltip("Ring the bell to end your turn", {m.x, m.y + 22});

    // ---------------- overlays
    Rectangle centre{300, 110, 680, 400};
    if (!modal)
    switch (S.phase) {
        case Phase::Menu: {
            Panel(centre);
            DrawTextCenteredBold("FLATS", centre.x + centre.width / 2, centre.y + 18, 44, Pal::Ink);
            DrawTextCentered(DEALER_LINES[0], centre.x + centre.width / 2, centre.y + 72, 18, Pal::BrassDk);
            DrawWrapped("Three flats, two dealers' decks, one bell. Win two flats for a round and two rounds for a match. Win a match to add a card to your deck "
                        "or take a charm that bends the rules; but each dealer, from the Novice to the House, has a trick of his own. Every round some flats "
                        "carry a hazard or a prize, and cards can come Foil, Gilt or Hex.\n\nOpen the HOW TO PLAY tab on the right for the full rules. "
                        "Cash out after any match, or press on and risk the pot.",
                        {centre.x + 34, centre.y + 104, centre.width - 68, 230}, 15, Pal::Ink);
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
            DrawTextCenteredBold(TextFormat("You beat %s!", DEALER_NAME[std::min(S.match, MATCHES - 1)]), centre.x + centre.width / 2, centre.y + 28, 36, Pal::Good);
            DrawTextCentered(TextFormat("The pot stands at %d gold.", S.pot), centre.x + centre.width / 2, centre.y + 90, 22, Pal::Ink);
            if (S.match + 1 >= MATCHES) {
                DrawTextCentered("The House slides the last of its gold across the table. You've cleaned it out.",
                                 centre.x + centre.width / 2, centre.y + 140, 17, Pal::BrassDk);
                if (Button({centre.x + centre.width / 2 - 150, centre.y + 250, 300, 52}, TextFormat("Collect %d gold", S.pot + 100))) {
                    g.gold += S.pot + 100;
                    S.payout = S.pot + 100;
                    S.cashed = true;
                    S.phase = Phase::RunOver;
                }
            } else {
                DrawWrapped(TextFormat("Cash out and walk away with it, or press on to face %s: a reward for your hand, a sharper dealer, and a bigger pot. "
                            "Lose the next match and all of it stays on the table.", DEALER_NAME[S.match + 1]),
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
            DrawTextCenteredBold(S.rewardCharm >= 0 ? "Choose a card, or a charm" : "Choose a card for your deck", centre.x + centre.width / 2, centre.y + 24, 32, Pal::Ink);
            DrawTextCentered(DEALER_LINES[std::min(MATCHES - 1, S.match + 1)], centre.x + centre.width / 2, centre.y + 72, 17, Pal::BrassDk);
            for (int k = 0; k < (int)S.rewards.size(); k++) {
                Rectangle r{centre.x + 60 + k * 205.0f, centre.y + 120, 140, 196};
                bool hov = CheckCollisionPointRec(m, r);
                if (hov) r.y -= 10;
                bool charm = k == 2 && S.rewardCharm >= 0;
                if (charm) DrawCharmCard(r, S.rewardCharm); else DrawCardFace(r, S.rewards[k], true);
                if (hov && !charm) Tooltip(CardDescription(S.rewards[k]), {m.x, m.y + 22});
                if (hov && charm) Tooltip(std::string(CHARM_NAME[S.rewardCharm]) + ": " + CHARM_TEXT[S.rewardCharm], {m.x, m.y + 22});
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
    if (!modal && (S.phase == Phase::Playing || S.phase == Phase::Resolving || S.phase == Phase::MatchOver || S.phase == Phase::Reward))
        if (Button({20, 122, 150, 34}, S.pot > 0 ? "Fold (lose pot)" : "Fold and leave", true, 14)) LeaveTable(g);

    // ---------------- the deck viewer
    if (S.showDeck) {
        Rectangle p{170, 70, 940, 580};
        Panel(p);
        DrawTextCenteredBold(TextFormat("Your deck: %d cards", (int)S.you.deck.size()), p.x + p.width / 2, p.y + 14, 30, Pal::Ink);
        std::vector<Card> sorted = S.you.deck;
        std::stable_sort(sorted.begin(), sorted.end(), [](const Card& a, const Card& b) { return a.suit != b.suit ? a.suit < b.suit : a.value < b.value; });
        for (int i = 0; i < (int)sorted.size(); i++) {
            Rectangle r{p.x + 30 + (i % 8) * 108.0f, p.y + 64 + (i / 8) * 122.0f, 82, 114};
            DrawCardFace(r, sorted[i], true);
            if (CheckCollisionPointRec(m, r)) Tooltip(CardDescription(sorted[i]), {m.x, m.y + 22});
        }
        if (Button({p.x + p.width / 2 - 90, p.y + p.height - 50, 180, 40}, "Close")) S.showDeck = false;
    }

    // ---------------- the "how to play" folder: open at any time, over everything else
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, RulesTabRect())) S.showRules = !S.showRules;
    DrawRulesFolder(m);
}

// ---------------------------------------------------------------- the sprite sheet page
void FlatsSpritePage(float t) {
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    TxtBold("Flats: the cards, editions, charms, modifiers, the dealer, and the bell", 30, 16, 24, Pal::Brass);
    int x = 30;
    for (int s = 0; s < SUITS; s++)
        for (int v : {2, 5, 9}) {
            Card c{s, v, SP_NONE};
            DrawCardFace({(float)x + (v == 2 ? 0 : v == 5 ? 100 : 200), 60.0f + s * 130, 84, 118}, c, true);
        }
    Card specials[SP_COUNT - 1] = {{COIN, 2, SP_TIDE}, {BLADE, 1, SP_SNARE}, {CUP, 4, SP_LANTERN}, {SHELL, 2, SP_WAVE}};
    for (int i = 0; i < SP_COUNT - 1; i++) DrawCardFace({360.0f + i * 100, 60, 84, 118}, specials[i], true);
    Txt("Specials: Tide, Snare, Lantern, Wave", 360, 184, 15, Pal::Paper);
    for (int e = ED_FOIL; e < ED_COUNT; e++) DrawCardFace({360.0f + (e - 1) * 100, 214, 84, 118}, Card{e == 2 ? CUP : e == 1 ? COIN : BLADE, 7, SP_NONE, e}, true);
    DrawCardFace({660, 214, 84, 118}, Card{}, false);
    Txt("Editions: Foil, Gilt, Hex; and the card back", 360, 338, 15, Pal::Paper);
    for (int c = 0; c < CH_COUNT; c++) DrawCharmIcon(c, {410.0f + c * 76, 400}, 60, t);
    Txt("Charms", 360, 438, 15, Pal::Paper);
    for (int k = 1; k < LM_COUNT; k++) DrawModEmblem(k, {410.0f + k * 76, 480}, t);
    Txt("Flat modifiers", 360, 508, 15, Pal::Paper);
    DrawCharmCard({800, 210, 140, 196}, CH_ANCHOR);
    DrawBell({560, 600}, true, 0);
    Txt("The bell (ends your turn)", 490, 650, 15, Pal::Paper);
    S.beam = 0.1f;
    rlPushMatrix();
    rlTranslatef(820, 80, 0);
    rlScalef(0.6f, 0.6f, 1);
    DrawScale(0.1f);
    rlPopMatrix();
    DrawSkull(t, 1000, 640);
    DrawChips({960, 560}, 240, t);
    Txt("The balance, the pot, and the candle skull", 800, 690, 15, Pal::Paper);
    rlPushMatrix();
    rlTranslatef(180, 460, 0);
    rlScalef(0.6f, 0.6f, 1);
    DrawDealer(t);
    rlPopMatrix();
    Txt("The dealer", 30, 690, 15, Pal::Paper);
}

// A mid-round position for screenshots: a few cards already on the table and a full hand, with editions,
// a modifier and a couple of charms in play. Variant 1 is the reward screen; variant 2 the deck viewer.
void DebugFlatsDeal(int variant) {
    ResetRun();
    S.phase = Phase::Playing;
    S.match = 1;
    StartMatch();
    S.mod[0] = LM_TREASURE; S.mod[1] = LM_REEF; S.mod[2] = LM_NONE;
    ClearLanes();
    S.you.charms = (1u << CH_PEARL) | (1u << CH_ANCHOR);
    S.you.lane[0].push_back({{COIN, 6, SP_NONE, ED_FOIL}, 1});
    S.you.lane[0].push_back({{COIN, 4, SP_NONE, ED_GILT}, 1});
    S.you.lane[1].push_back({{BLADE, 5, SP_NONE}, 1});
    S.you.lane[1].push_back({{SHELL, 3, SP_NONE, ED_HEX}, 0.5f, {640, 664}, 4});
    S.foe.lane[0].push_back({{CUP, 5, SP_NONE}, 1});
    S.foe.lane[1].push_back({{SHELL, 3, SP_NONE}, 1});
    S.foe.lane[1].push_back({{SHELL, 2, SP_WAVE}, 1});
    S.foe.lane[2].push_back({{BLADE, 7, SP_NONE, ED_FOIL}, 1});
    S.you.hand.resize(4);
    S.you.hand[0] = {SHELL, 8, SP_NONE};
    S.you.hand[1] = {CUP, 4, SP_LANTERN};
    S.you.hand[2] = {BLADE, 1, SP_SNARE, ED_GILT};
    S.you.hand[3] = {COIN, 9, SP_NONE, ED_HEX};
    S.pot = 140;
    S.beam = 0.1f;
    Burst({640, 452}, 8, 1, Color{240, 200, 80, 255}, 60);
    if (variant == 1) { S.match = 1; MakeRewards(); S.rewardCharm = CH_COMPASS; }
    if (variant == 2) S.showDeck = true;
}
void DebugFlatsDeal() { DebugFlatsDeal(0); }
void DebugFlatsReward() { DebugFlatsDeal(1); }
void DebugFlatsDeck() { DebugFlatsDeal(2); }
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
        Vector2 c{120.0f + (i % 7) * 170, 290.0f + (i / 7) * 170};
        DrawRectangleRounded({c.x - 40, c.y - 40, 80, 80}, 0.25f, 6, Color{40, 46, 44, 235});
        DrawItemIcon(ItemKind::Relic, i, c, 64);
        std::string n = relics[i].name;
        Txt(n, c.x - MeasureTxt(n, 15) / 2.0f, c.y + 48, 15, Pal::Paper);
        std::string tag = relics[i].isHardWeapon ? "hard weapon" : RelicCategoryName(relics[i].category);
        Txt(tag, c.x - MeasureTxt(tag, 11) / 2.0f, c.y + 66, 11, Color{190, 190, 176, 255});
    }
}

// Plays whole runs headlessly (you at random, the dealer by his own rules) to check the turn logic never
// stalls and to see how often a random player gets how far. Run with:  depth.exe --flats-sim 2000
void FlatsSim(int runs, bool sensible) {
    int reached[MATCHES + 1] = {0}, stalls = 0;
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
