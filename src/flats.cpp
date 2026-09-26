// ============================================================================
//  DEPTH - Flats, the card game played at the Nautilus's card table.
//
//  A roguelike duel of creatures. You and the dealer fight across a 4x4 board (see flats_board.h): your creatures
//  strike the space ahead each combat, or the scales if it is empty, and the scales tipping 8 either way ends the
//  battle. Cards have strength, defense and weight, a cost in blood (sacrifice your own creatures) or bones (earned
//  from the dead), and sigils. Battles are strung together on a branching map (flats_run.h): more battles, cards to
//  pick, campfires to strengthen a card, splices, trials, a stall to spend the pot in, and the House at the end.
//  Win a battle fast and you bank Momentum for a head start in the next. Cash out between battles or press on.
//
//  This file is the scene: it draws the table, plays back the engine's events as animation and handles the clicks.
//  The rules themselves live in the engine (flats_card / flats_board / flats_run); nothing here decides an outcome.
// ============================================================================
#include "game.h"
#include "flats_run.h"
#include "relics.h"
bool DrawCreaturePixels(const std::string& name, Rectangle box, float dim, int seed = 0);
#include "rlgl.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
using namespace flats;

const Color SUIT_COL[SUITS] = {{200, 148, 42, 255}, {60, 100, 168, 255}, {178, 46, 42, 255}, {64, 152, 142, 255}};
const Color STR_COL{214, 84, 64, 255}, HP_COL{74, 168, 104, 255}, WT_COL{128, 138, 158, 255}, BONE_COL{226, 220, 196, 255}, BLOOD_COL{196, 36, 48, 255};

int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
float Dist(Vector2 a, Vector2 b) { return hypotf(a.x - b.x, a.y - b.y); }

struct Particle { Vector2 p, v; float life, max, size; Color col; int kind; }; // kind 0 spark, 1 coin, 2 dust, 3 wisp
enum ShopKind { SK_CARD, SK_CHARM, SK_ITEM, SK_TRIM, SK_EDITION, SK_INSURE };
struct ShopItem { int kind = SK_CARD, price = 0, charm = -1, item = -1; Card card; bool sold = false; };

enum class Ph { Menu, Map, Boon, Battle, Won, Node, RunOver, Showcase };
enum class NodeUi { None, CardPick, Campfire, Splice, Sacrifice, Trial, Stall, Cache, Vents, Scrimshaw, Splicers };

// A card on the board can be mid-flight, mid-lunge, shaking or flashing: per-cell effects, decaying to nothing.
struct CellFx {
    Vector2 off{0, 0};       // where it is drawn relative to its cell (a slide or a flight)
    float offT = 0, offDur = 0.3f, arc = 0;
    float lunge = 0;         // 1 -> 0: a strike
    Vector2 lungeDir{0, 0};
    float shake = 0, flash = 0;
    float appear = 1;        // scale-in for a card that has just landed
};
struct Ghost { Card c; Vector2 pos; Vector2 size; float t = 0, dur = 0.6f; bool dies = true; };
struct FloatText { std::string text; Vector2 pos; float t = 0; Color col; float size = 20; };

struct Ui {
    bool inited = false;
    Ph ph = Ph::Menu;
    NodeUi nu = NodeUi::None;
    RunManager rm;
    Battle bat;
    Rng rng;
    Events ev;
    float stepT = 0, endT = 0;
    // interaction
    int selHand = -1;
    std::vector<std::pair<int, int>> sacs;
    bool sacMode = false;
    int sacCol = -1;
    int selItem = -1;             // an item waiting for a target
    Vector2 handFrom{640, 700};   // where the last card played left the hand from (for its flight)
    // playback state
    CellFx fx[ROWS][COLS];
    std::vector<Ghost> ghosts;
    std::vector<FloatText> floats;
    std::vector<Particle> parts;
    float shake = 0, beam = 0, bellT = 0, mood = 0, moodT = 0;
    // panels
    std::vector<Card> offers;
    int offerCharm = -1;
    bool rareOffer = false;
    int pick1 = -1, pick2 = -1;
    std::vector<ShopItem> shop;
    int rerolls = 0, shopPick = -1;
    int foundItem = -1;
    std::string toast; float toastT = 0;
    int gainedGold = 0; bool gainedMomentum = false, isElite = false, isBoss = false;
    bool cashed = false, lost = false; int payout = 0; bool insurePaid = false;
    bool showRules = false, showDeck = false;
    int lastTurns = 0;
    int showPage = 0;                              // the showcase page (developer tool): 0-8 sheets, 100+ one card each
    int ventStep = 0;                             // warmings so far at the Boiling Vents (the risk climbs with each)
    std::vector<int> scrimSuits, scrimSigils;      // the carved bones on offer at the Scrimshaw Artist
    int pickSuit = -1, pickSigil = -1;
};
Ui U;

void Toast(const std::string& s) { U.toast = s; U.toastT = 2.4f; }

void Burst(Vector2 at, int n, int kind, Color col, float speed) {
    for (int i = 0; i < n; i++) {
        float a = Roll(0, 6283) / 1000.0f, sp = speed * (0.3f + Roll(0, 100) / 100.0f);
        float life = 0.6f + Roll(0, 60) / 100.0f;
        U.parts.push_back({at, {cosf(a) * sp, sinf(a) * sp - (kind == 1 ? speed * 0.6f : 0)}, life, life, 2.0f + Roll(0, 30) / 10.0f, col, kind});
    }
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
    std::string nm = CharmName(ch);
    Txt(nm, r.x + r.width / 2 - MeasureTxt(nm, 14) / 2.0f, r.y + r.height * 0.68f, 14, Pal::Paper);
    DrawWrapped(CharmText(ch), {r.x + 12, r.y + r.height * 0.76f, r.width - 24, r.height * 0.22f}, 11, Fade(Pal::Paper, 0.75f));
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
    { Vector2 c = V(0, -76); Ell(c, 66 * sx + 6, 70 * sy + 6, ink); Ell(c, 66 * sx, 70 * sy, cloakDk); }
    // 2. the cloak, with a lit left edge and long folds
    Ell(V(0, -116), 60 * sx + 6, 30 * sy + 6, ink); Ell(V(0, -116), 60 * sx, 30 * sy, cloak);       // rounded, sloping shoulders
    { Vector2 b = V(0, -70); Ell(b, 58 * sx + 6, 56 * sy + 6, ink); Ell(b, 58 * sx, 56 * sy, cloak); Ell(V(-20, -68), 30 * sx, 48 * sy, cloakLt); Ell(V(8, -40), 44 * sx, 22 * sy, cloakDk); } // one round, heavy body
    
    
    for (int i = -2; i <= 2; i++) DrawLineEx(V(i * 9.0f, -112), V(i * 10.0f + (i > 0 ? 3.0f : -3.0f), -30), 3, cloakDk);
    DrawRing(V(0, -70), 58 * sx - 3, 58 * sx + 2, 150, 210, 30, Fade(rim, 0.8f));                              // the cold rim light down the left edge
    // 3. the arms and gloved hands, resting on the felt
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        Vector2 sh = V(s2 * 42.0f, -122), hd = V(s2 * 52.0f, -102);
        DrawLineEx(sh, hd, 84, ink); DrawLineEx(sh, hd, 76, cloak);
        if (s2 < 0) DrawLineEx({sh.x - 20, sh.y}, {hd.x - 20, hd.y}, 10, cloakLt);
        Vector2 cf = V(s2 * 51.5f, -105);
        DrawLineEx({cf.x - 30, cf.y}, {cf.x + 30, cf.y}, 16, ink); DrawLineEx({cf.x - 28, cf.y}, {cf.x + 28, cf.y}, 11, brass);   // a brass cuff
        Vector2 g = V(s2 * 52.5f, -96);
        Ell(g, 42, 32, ink); Ell(g, 38, 28, glove);
        for (int i = -1; i <= 1; i++) { DrawLineEx({g.x + i * 13.0f, g.y + 10}, {g.x + i * 13.0f + s2 * 3.0f, g.y + 26}, 11, ink); DrawLineEx({g.x + i * 13.0f, g.y + 10}, {g.x + i * 13.0f + s2 * 3.0f, g.y + 26}, 7, glove); }
        DrawRing({g.x + 13.0f * s2, g.y + 20}, 5, 9, 0, 360, 10, brass);                    // a ring on a finger
        DrawLineEx({g.x - 24, g.y - 12}, {g.x - 8, g.y - 16}, 2, Fade(rim, 0.7f));         // a thin edge of light on the knuckles
    }
    // 4. the mantle over the shoulders, trimmed in brass, with tassels
    Ell(V(0, -117), 54 * sx + 5, 17 * sy + 5, ink); Ell(V(0, -117), 54 * sx, 17 * sy, cloakLt);
    Ell(V(-6, -120), 30 * sx, 8 * sy, Color{48, 58, 88, 255});
    DrawLineEx(V(-50, -108), V(50, -108), 6, brass);
    for (int i = -2; i <= 2; i++) { DrawLineEx(V(i * 15.0f, -109), V(i * 15.0f, -99), 3, brass); Ell(V(i * 15.0f, -98), 5, 5, brass); }
    // 5. the high collar and the hood: peak, lit half, lining, rim light
    DrawTri(V(-34, -126), V(34, -126), V(0, -152), ink);
    DrawTri(V(-30, -126), V(30, -126), V(0, -148), cloakDk);
    DrawTri(V(-27, -140), V(27, -140), V(1, -172), ink);                                   // a soft peak to the hood, not a cone
    DrawTri(V(-25, -141), V(25, -141), V(1, -168), cloak);
    DrawTri(V(-25, -141), V(0, -141), V(-1, -168), cloakLt);
    DrawTri(V(-38, -122), V(-20, -150), V(-14, -122), cloak); DrawTri(V(38, -122), V(20, -150), V(14, -122), cloak); // the hood falls in folds to the shoulders
    DrawLineEx(V(-38, -122), V(-24, -156), 3, Fade(rim, 0.9f)); DrawLineEx(V(-25, -141), V(1, -178), 3, Fade(rim, 0.9f));
    { Vector2 c = V(0, -160); Ell(c, 30 * sx + 5, 28 * sy + 5, ink); Ell(c, 30 * sx, 28 * sy, cloak); Ell(V(0, -159), 25 * sx, 23 * sy, cloakDk); } // the hood and its darker lining
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

void Tooltip(const std::string& text, Vector2 at) {
    float w = (float)MeasureTxt(text, 15, true);
    Rectangle r{std::clamp(at.x - w / 2 - 10, 8.0f, SCREEN_W - w - 28), at.y, w + 20, 28};
    DrawRectangleRounded(r, 0.4f, 6, Color{8, 12, 16, 240});
    DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Pal::BrassDk);
    TxtBold(text, r.x + 10, r.y + 5, 15, Pal::Paper);
}

void DrawParticles() {
    for (auto& p : U.parts) {
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


// ---------------------------------------------------------------- drawing: glyphs, costs and the card itself
void DrawDrop(Vector2 c, float s, Color col) {
    DrawCircleV({c.x, c.y + 0.16f * s}, 0.32f * s, col);
    DrawTri({c.x - 0.3f * s, c.y + 0.06f * s}, {c.x + 0.3f * s, c.y + 0.06f * s}, {c.x, c.y - 0.5f * s}, col);
}
void DrawBone(Vector2 c, float s, Color col) {
    Color dk = ColorBrightness(col, -0.5f);
    DrawLineEx({c.x - 0.38f * s, c.y + 0.16f * s}, {c.x + 0.38f * s, c.y - 0.16f * s}, 0.24f * s + 2, dk);
    DrawLineEx({c.x - 0.38f * s, c.y + 0.16f * s}, {c.x + 0.38f * s, c.y - 0.16f * s}, 0.16f * s, col);
    for (int e = -1; e <= 1; e += 2)
        for (int k = -1; k <= 1; k += 2) DrawCircleV({c.x + e * 0.4f * s + k * 0.03f * s, c.y - e * 0.16f * s + k * 0.1f * s}, 0.11f * s, col);
}

void DrawSigilGlyph(Sigil sg, Vector2 c, float s, Color col) {
    float w = std::max(1.0f, 0.1f * s);
    Color dk = ColorBrightness(col, -0.5f);
    switch (sg) {
        case Sigil::SKIMMER:
            DrawTri({c.x - 0.5f * s, c.y + 0.25f * s}, {c.x + 0.5f * s, c.y - 0.35f * s}, {c.x + 0.05f * s, c.y + 0.4f * s}, col);
            DrawLineEx({c.x - 0.5f * s, c.y + 0.25f * s}, {c.x + 0.5f * s, c.y - 0.35f * s}, w * 0.7f, dk);
            break;
        case Sigil::TWIN_TIDE:
            for (int e = -1; e <= 1; e += 2) {
                DrawLineEx({c.x, c.y + 0.35f * s}, {c.x + e * 0.4f * s, c.y - 0.25f * s}, w, col);
                DrawTri({c.x + e * 0.4f * s - 0.18f * s, c.y - 0.15f * s}, {c.x + e * 0.4f * s + 0.18f * s, c.y - 0.15f * s}, {c.x + e * 0.4f * s, c.y - 0.45f * s}, col);
            }
            break;
        case Sigil::TIDECALLER:
            for (int k = -1; k <= 1; k++) DrawTri({c.x + k * 0.3f * s - 0.14f * s, c.y}, {c.x + k * 0.3f * s + 0.14f * s, c.y}, {c.x + k * 0.3f * s, c.y - 0.45f * s + (k == 0 ? -0.1f * s : 0)}, col);
            DrawRectangleRec({c.x - 0.44f * s, c.y, 0.88f * s, 0.24f * s}, col);
            break;
        case Sigil::SPINES:
            DrawCircleV(c, 0.22f * s, col);
            for (int k = 0; k < 8; k++) { float a = k * PI / 4; DrawLineEx({c.x + cosf(a) * 0.22f * s, c.y + sinf(a) * 0.22f * s}, {c.x + cosf(a) * 0.5f * s, c.y + sinf(a) * 0.5f * s}, w * 0.8f, col); }
            break;
        case Sigil::BRINE: DrawDrop(c, s, col); DrawDrop({c.x + 0.28f * s, c.y + 0.18f * s}, 0.55f * s, col); break;
        case Sigil::VENOM:
            DrawDrop({c.x, c.y - 0.05f * s}, s * 0.9f, col);
            DrawCircleV({c.x - 0.1f * s, c.y + 0.1f * s}, 0.05f * s, dk); DrawCircleV({c.x + 0.1f * s, c.y + 0.1f * s}, 0.05f * s, dk);
            break;
        case Sigil::SENTINEL:
            DrawRectangleRec({c.x - 0.36f * s, c.y - 0.42f * s, 0.72f * s, 0.5f * s}, col);
            DrawTri({c.x - 0.36f * s, c.y + 0.08f * s}, {c.x + 0.36f * s, c.y + 0.08f * s}, {c.x, c.y + 0.5f * s}, col);
            DrawLineEx({c.x, c.y - 0.34f * s}, {c.x, c.y + 0.28f * s}, w * 0.7f, dk);
            break;
        case Sigil::BURROWER:
            DrawCircleSector({c.x, c.y + 0.25f * s}, 0.5f * s, 270 - 90, 270 + 90, 12, col);
            DrawEllipse((int)c.x, (int)(c.y + 0.05f * s), 0.2f * s, 0.08f * s, dk);
            break;
        case Sigil::UNDYING:
            DrawRing(c, 0.26f * s, 0.36f * s, 30, 320, 16, col);
            DrawTri({c.x + 0.3f * s, c.y - 0.34f * s}, {c.x + 0.52f * s, c.y - 0.05f * s}, {c.x + 0.05f * s, c.y - 0.1f * s}, col);
            break;
        case Sigil::BALLAST:
            DrawTri({c.x - 0.5f * s, c.y + 0.4f * s}, {c.x + 0.5f * s, c.y + 0.4f * s}, {c.x + 0.28f * s, c.y - 0.15f * s}, col);
            DrawTri({c.x - 0.5f * s, c.y + 0.4f * s}, {c.x + 0.28f * s, c.y - 0.15f * s}, {c.x - 0.28f * s, c.y - 0.15f * s}, col);
            DrawRing({c.x, c.y - 0.26f * s}, 0.1f * s, 0.18f * s, 0, 360, 10, col);
            break;
        case Sigil::NINE_LIVES: TxtBold("9", c.x - MeasureTxt("9", (int)(s * 0.95f), true) / 2.0f, c.y - s * 0.55f, (int)(s * 0.95f), col); break;
        case Sigil::SPAWN:
            DrawCircleV({c.x - 0.18f * s, c.y}, 0.28f * s, col);
            DrawCircleV({c.x + 0.2f * s, c.y}, 0.28f * s, Fade(col, 0.75f));
            break;
        case Sigil::HEAVY_CURRENT:
            for (int k = -1; k <= 1; k++)
                for (int i = 0; i < 5; i++) DrawLineEx({c.x - 0.5f * s + i * 0.2f * s, c.y + k * 0.28f * s + sinf(i * 1.3f) * 0.1f * s}, {c.x - 0.3f * s + i * 0.2f * s, c.y + k * 0.28f * s + sinf((i + 1) * 1.3f) * 0.1f * s}, w * 0.8f, col);
            break;
        case Sigil::SWIMMER:
            DrawEllipse((int)(c.x - 0.08f * s), (int)c.y, 0.38f * s, 0.22f * s, col);
            DrawTri({c.x + 0.24f * s, c.y}, {c.x + 0.52f * s, c.y - 0.24f * s}, {c.x + 0.52f * s, c.y + 0.24f * s}, col);
            DrawCircleV({c.x - 0.28f * s, c.y - 0.05f * s}, 0.04f * s, dk);
            break;
        case Sigil::BONE_KING:
            DrawBone({c.x, c.y + 0.16f * s}, s, col);
            DrawTri({c.x - 0.3f * s, c.y - 0.05f * s}, {c.x - 0.1f * s, c.y - 0.05f * s}, {c.x - 0.2f * s, c.y - 0.42f * s}, col);
            DrawTri({c.x + 0.1f * s, c.y - 0.05f * s}, {c.x + 0.3f * s, c.y - 0.05f * s}, {c.x + 0.2f * s, c.y - 0.42f * s}, col);
            break;
        case Sigil::SCAVENGER:
            DrawRing({c.x, c.y + 0.1f * s}, 0.22f * s, 0.32f * s, 90, 360, 12, col);
            DrawLineEx({c.x, c.y - 0.42f * s}, {c.x, c.y + 0.1f * s}, w, col);
            break;
        case Sigil::FRY:
            DrawCircleV({c.x - 0.28f * s, c.y + 0.12f * s}, 0.12f * s, col);
            DrawCircleV({c.x + 0.16f * s, c.y - 0.05f * s}, 0.3f * s, col);
            break;
        case Sigil::REPULSIVE:
            DrawRing(c, 0.32f * s, 0.42f * s, 0, 360, 16, col);
            DrawLineEx({c.x - 0.28f * s, c.y + 0.28f * s}, {c.x + 0.28f * s, c.y - 0.28f * s}, w, col);
            break;
        case Sigil::WATERBORNE:
            DrawCircleV({c.x, c.y - 0.25f * s}, 0.2f * s, col);
            for (int k = 0; k < 2; k++) for (int i = 0; i < 4; i++) DrawLineEx({c.x - 0.5f * s + i * 0.25f * s, c.y + (0.05f + 0.25f * k) * s + (i % 2 ? -0.06f : 0.06f) * s}, {c.x - 0.25f * s + i * 0.25f * s, c.y + (0.05f + 0.25f * k) * s + (i % 2 ? 0.06f : -0.06f) * s}, w * 0.9f, col);
            break;
        case Sigil::PHALANX:
            for (int k = -1; k <= 1; k += 2) { DrawRectangleRec({c.x + k * 0.22f * s - 0.2f * s, c.y - 0.4f * s, 0.4f * s, 0.5f * s}, k > 0 ? col : Fade(col, 0.75f)); DrawTri({c.x + k * 0.22f * s - 0.2f * s, c.y + 0.1f * s}, {c.x + k * 0.22f * s + 0.2f * s, c.y + 0.1f * s}, {c.x + k * 0.22f * s, c.y + 0.42f * s}, k > 0 ? col : Fade(col, 0.75f)); }
            break;
        case Sigil::FORESIGHT:
            DrawEllipse((int)c.x, (int)c.y, 0.5f * s, 0.28f * s, col);
            DrawCircleV(c, 0.17f * s, dk); DrawCircleV({c.x - 0.05f * s, c.y - 0.05f * s}, 0.05f * s, col);
            break;
        case Sigil::MIGHTY_LEAP:
            for (int k = 0; k < 2; k++) { float y0 = c.y + 0.2f * s - k * 0.32f * s; DrawLineEx({c.x - 0.4f * s, y0}, {c.x, y0 - 0.3f * s}, w * 1.1f, col); DrawLineEx({c.x + 0.4f * s, y0}, {c.x, y0 - 0.3f * s}, w * 1.1f, col); }
            DrawRectangleRec({c.x - 0.45f * s, c.y + 0.36f * s, 0.9f * s, 0.12f * s}, col);
            break;
        case Sigil::MASSIVE:
            DrawRectangleLinesEx({c.x - 0.5f * s, c.y - 0.32f * s, s, 0.64f * s}, w * 1.2f, col);
            for (int k = -1; k <= 1; k++) DrawLineEx({c.x + k * 0.25f * s, c.y - 0.32f * s}, {c.x + k * 0.25f * s, c.y + 0.32f * s}, w * 0.6f, Fade(col, 0.7f));
            break;
        case Sigil::TIDAL_PULL:
            DrawCircleV(c, 0.4f * s, col); DrawCircleV({c.x + 0.2f * s, c.y - 0.06f * s}, 0.36f * s, dk);
            DrawLineEx({c.x - 0.5f * s, c.y + 0.42f * s}, {c.x + 0.4f * s, c.y + 0.42f * s}, w, col);
            break;
        default: break;
    }
}

Card gHoverCard;               // whichever card the mouse is over this frame (a copy: the original may be played away): shown large in the inspector
bool gHasHover = false;
int gHoverHp = -1, gHoverStr = -1;
void Hover(const Card& c, int hp = -1, int str = -1) { gHoverCard = c; gHasHover = true; gHoverHp = hp; gHoverStr = str; }

// ---- a tiny bitmap font of carved, tally-style numerals (5 x 7 pixels per digit), drawn as square pixels with a rim and a shaded lower half
void DrawPxNum(int v, Vector2 centre, float px, Color fill, Color rim) {
    static const char* G[10][7] = {
        {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}, {"00100", "01100", "00100", "00100", "00100", "00100", "01110"},
        {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}, {"11110", "00001", "00001", "01110", "00001", "00001", "11110"},
        {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}, {"11111", "10000", "11110", "00001", "00001", "10001", "01110"},
        {"00110", "01000", "10000", "11110", "10001", "10001", "01110"}, {"11111", "00001", "00010", "00100", "01000", "01000", "01000"},
        {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}, {"01110", "10001", "10001", "01111", "00001", "00010", "01100"}};
    std::string s = std::to_string(std::clamp(v, 0, 99));
    float w = (s.size() * 6 - 1) * px, x0 = centre.x - w / 2, y0 = centre.y - 3.5f * px;
    for (int pass = 0; pass < 2; pass++)
        for (size_t d = 0; d < s.size(); d++)
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++) {
                    if (G[s[d] - '0'][row][col] != '1') continue;
                    float x = x0 + (d * 6 + col) * px, y = y0 + row * px;
                    if (pass == 0) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) DrawRectangleRec({x + dx * px * 0.75f, y + dy * px * 0.75f, px, px}, rim); }
                    else { float sh = row < 4 ? 1.0f : 0.82f; DrawRectangleRec({x, y, px, px}, Color{(unsigned char)(fill.r * sh), (unsigned char)(fill.g * sh), (unsigned char)(fill.b * sh), 255}); }
                }
}

// Blood splatters for the cost: fixed pixel clusters, one per point of blood (never circles).
void DrawSplat(int variant, Vector2 c, float px) {
    static const char* S[3][7] = {{"..r.r..", ".rrRrr.", "rrRrrRr", ".rrrrr.", "rRrrrRr", ".r.rr..", "...r..."}, {"r..rr..", "rrrrRr.", ".rRrrrr", "rrrrRr.", ".rrRrr.", "..rr.r.", ".r....."}, {"..rr...", ".rRrrr.", "rrrrRrr", "rRrrrr.", ".rrrRrr", "..rrr.r", ".....r."}};
    for (int y = 0; y < 7; y++) for (int x = 0; x < 7; x++) {
        char ch = S[variant % 3][y][x];
        if (ch == '.') continue;
        DrawRectangleRec({c.x + (x - 3.5f) * px + px * 0.4f, c.y + (y - 3.5f) * px + px * 0.4f, px, px}, Color{20, 4, 6, 255});
        DrawRectangleRec({c.x + (x - 3.5f) * px, c.y + (y - 3.5f) * px, px, px}, ch == 'R' ? Color{112, 10, 16, 255} : Color{176, 22, 28, 255});
    }
}

// A tarnished, engraved iron plate that stretches to fit its lettering (fixed end caps, a stretched brushed middle). It is worn into the
// card: rust and stain spill past its edges, parchment shows through where the metal has flaked, and the ends fade back into the paper.
void DrawMetalPlate(Rectangle p, float u, unsigned seed) {
    Color iron{122, 114, 98, 255}, dk{50, 38, 28, 255}, paper{194, 168, 118, 255};
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xffff) / 65535.0f; };
    for (int k = 0; k < 6; k++) DrawCircleV({p.x + p.width * rnd(), p.y + p.height * (rnd() < 0.5f ? -0.05f : 1.05f)}, (2 + 4 * rnd()) * u, Fade(Color{130, 84, 44, 255}, 0.22f));   // rust weeping onto the paper
    DrawRectangleRounded({p.x + 0.5f * u, p.y + 0.9f * u, p.width, p.height}, 0.18f, 4, Fade(BLACK, 0.2f));
    DrawRectangleRounded(p, 0.18f, 4, Fade(iron, 0.93f));
    float cap = std::min(6 * u, p.width * 0.3f);
    DrawRectangleGradientV((int)p.x, (int)p.y, (int)p.width, (int)(p.height * 0.5f), Fade(WHITE, 0.14f), Fade(WHITE, 0.0f));
    DrawRectangleGradientV((int)p.x, (int)(p.y + p.height * 0.5f), (int)p.width, (int)(p.height * 0.5f), Fade(BLACK, 0.0f), Fade(BLACK, 0.24f));
    for (int k = 0; k < 9; k++) { float y = p.y + p.height * (0.12f + 0.76f * rnd()); DrawLineEx({p.x + cap, y}, {p.x + p.width - cap, y}, std::max(1.0f, 0.5f * u), Fade(rnd() < 0.5f ? WHITE : BLACK, 0.13f)); }
    for (int k = 0; k < 8; k++) DrawCircleV({p.x + p.width * rnd(), p.y + p.height * rnd()}, (1.2f + 3.0f * rnd()) * u, Fade(Color{156, 88, 44, 255}, 0.28f));   // rust bloom across the face
    DrawRectangleGradientH((int)p.x, (int)p.y, (int)(cap * 1.4f), (int)p.height, Fade(paper, 0.6f), Fade(paper, 0.0f));   // the ends fade back into the paper
    DrawRectangleGradientH((int)(p.x + p.width - cap * 1.4f), (int)p.y, (int)(cap * 1.4f), (int)p.height, Fade(paper, 0.0f), Fade(paper, 0.6f));
    DrawRectangleRoundedLinesEx(p, 0.18f, 4, std::max(1.0f, 1.0f * u), Fade(dk, 0.5f));
    for (int k = 0; k < 9; k++) { // flaked away: paper showing through along the edges
        float ex = p.x + p.width * rnd(); DrawCircleV({ex, rnd() < 0.5f ? p.y : p.y + p.height}, (0.8f + 1.8f * rnd()) * u, Fade(paper, 0.7f));
    }
    for (int e = 0; e < 2; e++) { Vector2 b{e ? p.x + p.width - cap * 0.55f : p.x + cap * 0.55f, p.y + p.height / 2}; DrawCircleV(b, 1.3f * u, Fade(dk, 0.75f)); DrawCircleV({b.x - 0.3f * u, b.y - 0.3f * u}, 0.6f * u, Fade(WHITE, 0.3f)); }
}
// A card face (or its back) filling `r`. Everything scales from the card's height, so the same routine draws a big card in
// the inspector and a small one in the queue. `hp` and `str` show a creature's current numbers (damaged, buffed) on the board.
void DrawCardFace(Rectangle r, const Card& c, bool faceUp, int hp = -1, int str = -1) {
    float u = r.height / 150.0f;
    DrawRectangleRounded({r.x + 2 * u, r.y + 3 * u, r.width, r.height}, 0.08f, 6, Fade(BLACK, 0.5f));
    if (!faceUp) { // the back: the same driftwood and stained cloth, stamped with a faded ship's wheel
        Color wood{54, 40, 30, 255}, woodLt{86, 66, 48, 255}, ink{30, 22, 16, 255}, cloth{104, 98, 82, 255};
        unsigned hh = (unsigned)(r.x * 3 + 17);
        auto rn = [&]() { hh = hh * 1664525u + 1013904223u; return ((hh >> 8) & 0xffff) / 65535.0f; };
        DrawRectangleRounded(r, 0.035f, 4, wood);
        for (int k = 0; k < 12; k++) { float y = r.y + rn() * r.height; DrawLineEx({r.x + 1, y}, {r.x + r.width - 1, y + (rn() - 0.5f) * 3 * u}, std::max(1.0f, 0.8f * u), Fade(rn() < 0.5f ? BLACK : woodLt, 0.35f)); }
        Rectangle in{r.x + 5 * u, r.y + 5 * u, r.width - 10 * u, r.height - 10 * u};
        DrawRectangleRec(in, cloth);
        DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)in.height, Fade(Color{40, 60, 60, 255}, 0.30f), Fade(Color{20, 30, 30, 255}, 0.42f));
        for (int k = -6; k <= 6; k++) { // a knotted net, worn thin
            DrawLineEx({in.x + in.width * 0.5f + k * 0.16f * in.width - 0.4f * in.width, in.y}, {in.x + in.width * 0.5f + k * 0.16f * in.width + 0.4f * in.width, in.y + in.height}, std::max(1.0f, 0.8f * u), Fade(ink, 0.13f));
            DrawLineEx({in.x + in.width * 0.5f + k * 0.16f * in.width + 0.4f * in.width, in.y}, {in.x + in.width * 0.5f + k * 0.16f * in.width - 0.4f * in.width, in.y + in.height}, std::max(1.0f, 0.8f * u), Fade(ink, 0.13f));
        }
        for (int k = 0; k < 4; k++) { Vector2 sc{in.x + in.width * rn(), in.y + in.height * rn()}; float rad = (10 + 18 * rn()) * u; DrawCircleV(sc, rad, Fade(Color{120, 100, 60, 255}, 0.10f)); DrawRing(sc, rad - 1.2f * u, rad, 0, 360, 20, Fade(Color{70, 54, 30, 255}, 0.24f)); }
        for (int k = 0; k < 5; k++) DrawCircleV({in.x + in.width * rn(), in.y + in.height * rn()}, (3 + 5 * rn()) * u, Fade(Color{80, 112, 92, 255}, 0.16f));   // mildew
        for (int side = 0; side < 4; side++) { // frayed rim
            bool horiz = side < 2; float len = horiz ? in.width : in.height;
            for (float s = 0; s < len; s += 2.2f * u) {
                if (rn() > 0.4f) continue;
                float w2 = (1.0f + 2.0f * rn()) * u, d = (0.7f + 1.8f * rn()) * u;
                DrawRectangleRec(horiz ? Rectangle{in.x + s, side == 0 ? in.y : in.y + in.height - d, w2, d} : Rectangle{side == 2 ? in.x : in.x + in.width - d, in.y + s, d, w2}, wood);
            }
        }
        Vector2 m{r.x + r.width / 2, r.y + r.height / 2};
        float R0 = 0.27f * r.width;
        DrawRing(m, R0 * 0.62f, R0 * 0.72f, 0, 360, 28, Fade(ink, 0.55f));         // a ship's wheel, stamped in faded ink
        DrawRing(m, R0 * 1.02f, R0 * 1.12f, 0, 360, 28, Fade(ink, 0.5f));
        for (int k = 0; k < 8; k++) { float a = k * PI / 4; DrawLineEx({m.x + cosf(a) * R0 * 0.2f, m.y + sinf(a) * R0 * 0.2f}, {m.x + cosf(a) * R0 * 1.4f, m.y + sinf(a) * R0 * 1.4f}, std::max(1.0f, 1.6f * u), Fade(ink, 0.5f)); DrawCircleV({m.x + cosf(a) * R0 * 1.4f, m.y + sinf(a) * R0 * 1.4f}, 1.7f * u, Fade(ink, 0.55f)); }
        DrawCircleV(m, R0 * 0.2f, Fade(ink, 0.6f));
        for (int k = 0; k < 30; k++) { float e = rn(); Vector2 sp = k % 2 ? Vector2{in.x + in.width * rn(), e < 0.5f ? in.y + 2 * u * rn() : in.y + in.height - 2 * u * rn()} : Vector2{e < 0.5f ? in.x + 2 * u * rn() : in.x + in.width - 2 * u * rn(), in.y + in.height * rn()}; DrawCircleV(sp, (0.5f + 0.9f * rn()) * u, Fade(Color{240, 236, 220, 255}, 0.5f)); }   // salt
        for (int k = 0; k < 4; k++) DrawCircleV({k % 2 ? in.x + in.width - 4 * u : in.x + 4 * u, k < 2 ? in.y + 4 * u : in.y + in.height - 4 * u}, 1.5f * u, Fade(ink, 0.7f));   // rivets
        return;
    }    // ---- a physical relic salvaged from the sea: a driftwood frame round stained, salt-crusted parchment
    Color ink{30, 22, 16, 255}, wood{54, 40, 30, 255}, woodLt{86, 66, 48, 255};
    Color paper{194, 168, 118, 255};
    if (c.edition == ED_HEX) paper = Color{176, 150, 140, 255};
    unsigned h = (unsigned)(c.id * 131 + c.strength * 31 + 5);
    auto rnd = [&]() { h = h * 1664525u + 1013904223u; return ((h >> 8) & 0xffff) / 65535.0f; };
    DrawRectangleRounded(r, 0.035f, 4, wood);
    for (int k = 0; k < 14; k++) { float y = r.y + rnd() * r.height; DrawLineEx({r.x + 1, y}, {r.x + r.width - 1, y + (rnd() - 0.5f) * 3 * u}, std::max(1.0f, 0.8f * u), Fade(rnd() < 0.5f ? BLACK : woodLt, 0.35f)); }   // driftwood grain
    Rectangle in{r.x + 5 * u, r.y + 5 * u, r.width - 10 * u, r.height - 10 * u};
    DrawRectangleRec(in, paper);
    DrawRectangleGradientV((int)in.x, (int)in.y, (int)in.width, (int)(in.height * 0.5f), Fade(Color{120, 84, 40, 255}, 0.28f), Fade(Color{120, 84, 40, 255}, 0.0f));   // aged, uneven: dark at the top and foot
    DrawRectangleGradientV((int)in.x, (int)(in.y + in.height * 0.55f), (int)in.width, (int)(in.height * 0.45f), Fade(Color{100, 70, 36, 255}, 0.0f), Fade(Color{80, 54, 26, 255}, 0.42f));
    for (int k = 0; k < 4; k++) { // water stains: soft blotches with a darker tide-line ring
        Vector2 sc{in.x + in.width * (0.1f + 0.8f * rnd()), in.y + in.height * (0.1f + 0.8f * rnd())};
        float rad = (10 + 20 * rnd()) * u;
        DrawCircleV(sc, rad, Fade(Color{120, 84, 40, 255}, 0.10f));
        DrawRing(sc, rad - 1.2f * u, rad, 0, 360, 24, Fade(Color{96, 64, 30, 255}, 0.22f));
    }
    for (int k = 0; k < 5; k++) { Vector2 mc{in.x + in.width * rnd(), in.y + in.height * rnd()}; DrawCircleV(mc, (3 + 6 * rnd()) * u, Fade(Color{86, 104, 84, 255}, 0.13f)); }   // mildew
    for (int k = 0; k < 16; k++) DrawCircleV({in.x + in.width * rnd(), in.y + in.height * rnd()}, (0.6f + 1.2f * rnd()) * u, Fade(Color{86, 60, 30, 255}, 0.3f));       // foxing
    // frayed edges: the driftwood bites back into the parchment
    for (int side = 0; side < 4; side++) {
        bool horiz = side < 2;
        float len = horiz ? in.width : in.height;
        for (float s = 0; s < len; s += 2.2f * u) {
            if (rnd() > 0.42f) continue;
            float w2 = (1.0f + 2.2f * rnd()) * u, d = (0.7f + 1.9f * rnd()) * u;
            Rectangle q = horiz ? Rectangle{in.x + s, side == 0 ? in.y : in.y + in.height - d, w2, d} : Rectangle{side == 2 ? in.x : in.x + in.width - d, in.y + s, d, w2};
            DrawRectangleRec(q, wood);
        }
    }
    for (int k = 0; k < 34; k++) { // salt crust along the edges
        float e = rnd(); Vector2 sp = k % 2 ? Vector2{in.x + in.width * rnd(), e < 0.5f ? in.y + 2 * u * rnd() : in.y + in.height - 2 * u * rnd()} : Vector2{e < 0.5f ? in.x + 2 * u * rnd() : in.x + in.width - 2 * u * rnd(), in.y + in.height * rnd()};
        DrawCircleV(sp, (0.5f + 1.0f * rnd()) * u, Fade(Color{240, 236, 220, 255}, 0.55f));
    }
    DrawRectangleLinesEx({in.x + 3 * u, in.y + 3 * u, in.width - 6 * u, in.height - 6 * u}, std::max(1.0f, 0.7f * u), Fade(ink, 0.22f));

    int shownHp = hp >= 0 ? hp : c.defense, shownStr = str >= 0 ? str : c.strength + (c.edition == ED_FOIL ? 1 : c.edition == ED_HEX ? 2 : 0);
    if (hp < 0 && c.edition == ED_HEX) shownHp = std::max(1, c.defense - 1);
    int base = c.strength + (c.edition == ED_FOIL ? 1 : c.edition == ED_HEX ? 2 : 0);
    int n = (int)c.sigils.size();
    // ---- layer 1: things printed straight onto the card: the tribe emblem (bottom left) and the unframed sigils (bottom bar)
    DrawSuitIcon(c.suit, {in.x + 11 * u, in.y + in.height - 35 * u}, 13 * u, Fade(ink, 0.75f));
    const float kf = r.height < 135 ? 1.3f : 1.0f;   // small board cards: everything printed larger, so it stays readable
    float bs = std::min((n <= 2 ? 24.0f : 19.0f) * u * kf, 26 * u), gap = bs * 1.02f;
    for (int i = 0; i < n; i++) {
        bool top = n == 3 && i == 2;   // a third seal rides above the other two
        int rowN = std::min(n, 2);
        Vector2 p = top ? Vector2{r.x + r.width / 2, in.y + in.height - 15 * u - bs * 0.95f} : Vector2{r.x + r.width / 2 + ((float)i - (rowN - 1) / 2.0f) * gap, in.y + in.height - 15 * u};
        DrawSigilGlyph(c.sigils[i], {p.x + 0.7f * u, p.y + 0.8f * u}, bs, Fade(Color{250, 240, 210, 255}, 0.55f));
        DrawSigilGlyph(c.sigils[i], p, bs, Color{28, 20, 14, 255});
    }
    // ---- layer 2: the creature, with no box round it: it may spill over the header and the stat line
    Rectangle art{r.x + 2 * u, r.y + r.height * 0.13f, r.width - 4 * u, r.height * 0.66f};
    DrawEllipse((int)(r.x + r.width / 2), (int)(r.y + r.height * 0.5f), r.width * 0.42f, r.height * 0.22f, Fade(Color{60, 42, 24, 255}, 0.16f));
    if (!DrawCreaturePixels(c.name, art, 1.0f, c.id)) DrawSuitIcon(c.suit, {art.x + art.width / 2, art.y + art.height / 2}, art.width * 0.6f, SUIT_COL[c.suit]);
    // ---- layer 3: hardware. The engraved iron header plate stretches to fit the name; an iron weight hangs top left
    int fs = std::max(8, (int)(12 * u * kf));
    while (fs > 8 && MeasureTxt(c.name, fs, true) > in.width - 12 * u) fs--;
    float tw = MeasureTxt(c.name, fs, true), pw = std::min(in.width - 2 * u, std::max(46 * u, tw + 14 * u)), ph = std::max(15 * u, fs + 6.0f);
    Rectangle plate{r.x + r.width / 2 - pw / 2, in.y + 1.5f * u, pw, ph};
    DrawMetalPlate(plate, u, (unsigned)(c.id * 977 + 3));
    float tx = plate.x + pw / 2 - tw / 2, ty = plate.y + (ph - fs) / 2 - 1;
    TxtBold(c.name, tx + 0.8f * u, ty + 0.8f * u, fs, Fade(Color{190, 194, 190, 255}, 0.55f));
    TxtBold(c.name, tx - 0.4f * u, ty - 0.4f * u, fs, Fade(BLACK, 0.7f));
    TxtBold(c.name, tx, ty, fs, Color{42, 30, 22, 255});
    {
        Vector2 wp{in.x + 12 * u, in.y + 30 * u};
        DrawTri({wp.x - 8 * u, wp.y + 8 * u}, {wp.x + 8 * u, wp.y + 8 * u}, {wp.x + 5 * u, wp.y - 4 * u}, Color{20, 22, 24, 255});
        DrawTri({wp.x - 8 * u, wp.y + 8 * u}, {wp.x + 5 * u, wp.y - 4 * u}, {wp.x - 5 * u, wp.y - 4 * u}, Color{20, 22, 24, 255});
        DrawTri({wp.x - 7 * u, wp.y + 7.2f * u}, {wp.x + 7 * u, wp.y + 7.2f * u}, {wp.x + 4.4f * u, wp.y - 3.2f * u}, Color{92, 96, 100, 255});
        DrawTri({wp.x - 7 * u, wp.y + 7.2f * u}, {wp.x + 4.4f * u, wp.y - 3.2f * u}, {wp.x - 4.4f * u, wp.y - 3.2f * u}, Color{112, 116, 120, 255});
        DrawRing({wp.x, wp.y - 6 * u}, 1.6f * u, 3 * u, 0, 360, 12, Color{20, 22, 24, 255});
        DrawPxNum(c.weight, {wp.x, wp.y + 2.4f * u}, 1.5f * u, Color{236, 232, 216, 255}, Color{10, 10, 12, 255});
    }
    // ---- layer 4: metrics. Blood splatters (or bones) for the cost, and the carved numerals
    if (c.cost != CostType::FREE && c.costAmount > 0) {
        int cn = c.costAmount;
        for (int i = 0; i < cn; i++) {
            Vector2 sp{in.x + in.width - (cn > 2 ? 10 + (i % 2) * 11 : 12) * u, in.y + (25 + (cn > 2 ? (i / 2) * 11 : i * 12)) * u};
            if (c.cost == CostType::BLOOD) DrawSplat(i + c.id, sp, 1.35f * u);
            else DrawBone(sp, 10 * u, ColorBrightness(BONE_COL, -0.05f));
        }
    }
    Color strC = shownStr > base ? Color{150, 240, 150, 255} : shownStr < c.strength ? Color{255, 130, 110, 255} : Color{238, 226, 194, 255};
    Color hpC = hp >= 0 && hp < c.defense ? Color{255, 130, 110, 255} : hp > c.defense ? Color{150, 240, 150, 255} : Color{238, 226, 194, 255};
    float px = 3.0f * u * std::min(kf, 1.15f);
    DrawPxNum(shownStr, {in.x + 11 * u, in.y + in.height - 12 * u}, px, strC, Color{18, 12, 8, 255});
    DrawPxNum(shownHp, {in.x + in.width - 11 * u, in.y + in.height - 12 * u}, px, hpC, Color{18, 12, 8, 255});    if (c.edition != ED_NONE) DrawEdition(r, c.edition, u);
}
// The inspector: the hovered card large, with every number and sigil spelled out.
void DrawInspector(const Card& c, int hp, int str) {
    Rectangle p{1006, 70, 262, 396};
    DrawRectangleRounded(p, 0.04f, 8, Color{8, 12, 16, 236});
    DrawRectangleRoundedLinesEx(p, 0.04f, 8, 2, Pal::BrassDk);
    DrawCardFace({p.x + 56, p.y + 10, 150, 210}, c, true, hp, str);
    float y = p.y + 228;
    TxtBold(c.name, p.x + 14, y, 18, Pal::Brass); y += 24;
    Txt(TextFormat("Strength %d   Defense %d   Weight %d", str >= 0 ? str : c.strength, hp >= 0 ? hp : c.defense, c.weight), p.x + 14, y, 14, Pal::Paper); y += 20;
    Txt(TextFormat("Cost: %s   Tribe: %s", c.CostText().c_str(), SuitName(c.suit)), p.x + 14, y, 14, Pal::Paper); y += 22;
    for (Sigil s : c.sigils) {
        TxtBold(InfoOf(s).name, p.x + 14, y, 14, Color{236, 214, 160, 255}); y += 17;
        DrawWrapped(InfoOf(s).text, {p.x + 14, y, p.width - 28, 34}, 12, Fade(Pal::Paper, 0.8f)); y += 30;
    }
    if (c.edition != ED_NONE) { TxtBold(EditionName(c.edition), p.x + 14, y, 14, Pal::Brass); y += 17; DrawWrapped(EditionText(c.edition), {p.x + 14, y, p.width - 28, 30}, 12, Fade(Pal::Paper, 0.8f)); }
}

// A bottle of something in a pack slot.
void DrawBottle(int kind, Vector2 c, float s) {
    Color glass{150, 190, 196, 150}, ink{8, 8, 12, 255};
    DrawEllipse((int)c.x, (int)(c.y + 0.46f * s), 0.34f * s, 0.07f * s, Fade(BLACK, 0.5f));
    DrawRectangleRounded({c.x - 0.28f * s, c.y - 0.2f * s, 0.56f * s, 0.64f * s}, 0.4f, 6, ink);
    DrawRectangleRounded({c.x - 0.25f * s, c.y - 0.17f * s, 0.5f * s, 0.58f * s}, 0.4f, 6, glass);
    DrawRectangle((int)(c.x - 0.09f * s), (int)(c.y - 0.42f * s), (int)(0.18f * s), (int)(0.24f * s), ink);
    DrawRectangle((int)(c.x - 0.07f * s), (int)(c.y - 0.4f * s), (int)(0.14f * s), (int)(0.2f * s), glass);
    DrawRectangle((int)(c.x - 0.1f * s), (int)(c.y - 0.5f * s), (int)(0.2f * s), (int)(0.1f * s), Color{150, 108, 60, 255}); // the cork
    Vector2 m{c.x, c.y + 0.14f * s};
    switch ((PackItem)kind) {
        case PackItem::BOULDER: DrawCircleV(m, 0.17f * s, Color{120, 122, 128, 255}); DrawCircleV({m.x - 0.05f * s, m.y - 0.05f * s}, 0.05f * s, Color{170, 172, 176, 255}); break;
        case PackItem::GOAT: DrawTri({m.x - 0.14f * s, m.y + 0.14f * s}, {m.x + 0.14f * s, m.y + 0.14f * s}, {m.x, m.y - 0.12f * s}, ink); DrawLineEx({m.x - 0.12f * s, m.y - 0.02f * s}, {m.x - 0.2f * s, m.y - 0.2f * s}, 0.04f * s + 1, ink); DrawLineEx({m.x + 0.12f * s, m.y - 0.02f * s}, {m.x + 0.2f * s, m.y - 0.2f * s}, 0.04f * s + 1, ink); break;
        case PackItem::FISHHOOK: DrawRing({m.x, m.y + 0.03f * s}, 0.1f * s, 0.16f * s, 60, 330, 10, Color{200, 205, 210, 255}); DrawLineEx({m.x + 0.1f * s, m.y - 0.2f * s}, {m.x + 0.1f * s, m.y}, 0.05f * s + 1, Color{200, 205, 210, 255}); break;
        case PackItem::INK: DrawDrop(m, 0.5f * s, ink); break;
        case PackItem::BANDAGE: DrawRectangleRec({m.x - 0.15f * s, m.y - 0.05f * s, 0.3f * s, 0.1f * s}, Color{236, 226, 200, 255}); DrawRectangleRec({m.x - 0.05f * s, m.y - 0.15f * s, 0.1f * s, 0.3f * s}, Color{236, 226, 200, 255}); break;
        default: DrawLineEx({m.x - 0.14f * s, m.y + 0.16f * s}, {m.x + 0.14f * s, m.y - 0.2f * s}, 0.04f * s + 1, Color{200, 205, 210, 255}); DrawTri({m.x + 0.08f * s, m.y - 0.14f * s}, {m.x + 0.2f * s, m.y - 0.14f * s}, {m.x + 0.16f * s, m.y - 0.3f * s}, Color{220, 224, 228, 255}); break;
    }
}

// A node on the map.
void DrawNodeIcon(NodeType t, Vector2 c, float r, Color col) {
    Color ink{20, 14, 10, 255};
    switch (t) {
        case NodeType::BATTLE:
            DrawLineEx({c.x - 0.6f * r, c.y + 0.6f * r}, {c.x + 0.6f * r, c.y - 0.6f * r}, 0.22f * r, col);
            DrawLineEx({c.x + 0.6f * r, c.y + 0.6f * r}, {c.x - 0.6f * r, c.y - 0.6f * r}, 0.22f * r, col);
            DrawCircleV({c.x, c.y}, 0.16f * r, ink);
            break;
        case NodeType::ELITE: case NodeType::BOSS:
            DrawCircleV({c.x, c.y - 0.05f * r}, 0.5f * r, col);
            DrawRectangleRec({c.x - 0.28f * r, c.y + 0.3f * r, 0.56f * r, 0.32f * r}, col);
            DrawCircleV({c.x - 0.2f * r, c.y - 0.1f * r}, 0.12f * r, ink); DrawCircleV({c.x + 0.2f * r, c.y - 0.1f * r}, 0.12f * r, ink);
            DrawTri({c.x - 0.5f * r, c.y - 0.25f * r}, {c.x - 0.2f * r, c.y - 0.45f * r}, {c.x - 0.6f * r, c.y - 0.75f * r}, col);
            DrawTri({c.x + 0.5f * r, c.y - 0.25f * r}, {c.x + 0.2f * r, c.y - 0.45f * r}, {c.x + 0.6f * r, c.y - 0.75f * r}, col);
            break;
        case NodeType::CARD_PICK:
            DrawRectangleRounded({c.x - 0.42f * r, c.y - 0.6f * r, 0.84f * r, 1.2f * r}, 0.15f, 4, col);
            TxtBold("?", c.x - MeasureTxt("?", (int)(r * 1.1f), true) / 2.0f, c.y - r * 0.62f, (int)(r * 1.1f), ink);
            break;
        case NodeType::CAMPFIRE:
            DrawTri({c.x - 0.5f * r, c.y + 0.5f * r}, {c.x + 0.5f * r, c.y + 0.5f * r}, {c.x, c.y - 0.6f * r}, col);
            DrawTri({c.x - 0.22f * r, c.y + 0.5f * r}, {c.x + 0.22f * r, c.y + 0.5f * r}, {c.x, c.y - 0.05f * r}, ink);
            break;
        case NodeType::SPLICE:
            DrawCircleV({c.x - 0.3f * r, c.y}, 0.42f * r, col); DrawCircleV({c.x + 0.3f * r, c.y}, 0.42f * r, Fade(col, 0.8f));
            DrawCircleV({c.x, c.y}, 0.14f * r, ink);
            break;
        case NodeType::SACRIFICE:
            DrawRectangleLinesEx({c.x - 0.5f * r, c.y - 0.5f * r, r, r}, 0.16f * r, col);
            DrawLineEx({c.x - 0.5f * r, c.y - 0.5f * r}, {c.x + 0.5f * r, c.y + 0.5f * r}, 0.16f * r, col);
            DrawLineEx({c.x + 0.5f * r, c.y - 0.5f * r}, {c.x - 0.5f * r, c.y + 0.5f * r}, 0.16f * r, col);
            break;
        case NodeType::TRIAL:
            DrawTri({c.x - 0.6f * r, c.y + 0.5f * r}, {c.x + 0.6f * r, c.y + 0.5f * r}, {c.x, c.y - 0.6f * r}, col);
            DrawEllipse((int)c.x, (int)(c.y + 0.12f * r), 0.26f * r, 0.16f * r, ink); DrawCircleV({c.x, c.y + 0.12f * r}, 0.08f * r, col);
            break;
        case NodeType::STALL:
            for (int k = 0; k < 3; k++) { DrawEllipse((int)c.x, (int)(c.y + 0.4f * r - k * 0.28f * r), 0.5f * r, 0.18f * r, ink); DrawEllipse((int)c.x, (int)(c.y + 0.36f * r - k * 0.28f * r), 0.46f * r, 0.16f * r, col); }
            break;
        case NodeType::VENTS:
            for (int k = -1; k <= 1; k++) for (int i = 0; i < 4; i++) DrawLineEx({c.x + k * 0.45f * r + sinf(i * 1.4f + k) * 0.1f * r, c.y + 0.6f * r - i * 0.4f * r}, {c.x + k * 0.45f * r + sinf((i + 1) * 1.4f + k) * 0.1f * r, c.y + 0.6f * r - (i + 1) * 0.4f * r}, 0.16f * r, col);
            DrawRectangleRec({c.x - 0.7f * r, c.y + 0.6f * r, 1.4f * r, 0.18f * r}, ink);
            break;
        case NodeType::SCRIMSHAW:
            DrawBone({c.x, c.y}, 1.7f * r, col);
            DrawLineEx({c.x - 0.3f * r, c.y - 0.08f * r}, {c.x + 0.3f * r, c.y - 0.08f * r}, 0.08f * r, ink);
            DrawLineEx({c.x - 0.2f * r, c.y + 0.1f * r}, {c.x + 0.2f * r, c.y + 0.1f * r}, 0.08f * r, ink);
            break;
        case NodeType::SPLICERS:
            DrawRing({c.x - 0.3f * r, c.y}, 0.28f * r, 0.5f * r, 0, 360, 14, col); DrawRing({c.x + 0.3f * r, c.y}, 0.28f * r, 0.5f * r, 0, 360, 14, col);
            DrawCircleV({c.x, c.y}, 0.2f * r, ink); DrawCircleV({c.x, c.y}, 0.1f * r, col);
            break;
        default: // cache: a satchel
            DrawRectangleRounded({c.x - 0.5f * r, c.y - 0.25f * r, r, 0.8f * r}, 0.3f, 4, col);
            DrawRing({c.x, c.y - 0.25f * r}, 0.2f * r, 0.32f * r, 180, 360, 10, col);
            DrawRectangleRec({c.x - 0.1f * r, c.y, 0.2f * r, 0.16f * r}, ink);
            break;
    }
}

// The scales: their beam tilts toward whoever is ahead; bone beads heaped in the leading dish, one per point.
void DrawScale(float tilt, int scaleVal) {
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
    Vector2 dishL{l.x, l.y + 96}, dishR{r.x, r.y + 96};
    int foeBeads = scaleVal < 0 ? -scaleVal : 0, youBeads = scaleVal > 0 ? scaleVal : 0;
    for (int k = 0; k < foeBeads; k++) DrawCircleV({dishL.x - 24 + (k % 5) * 12.0f, dishL.y - 30 - (k / 5) * 9.0f}, 5.5f, Color{224, 218, 196, 255});
    for (int k = 0; k < youBeads; k++) DrawCircleV({dishR.x - 24 + (k % 5) * 12.0f, dishR.y - 30 - (k / 5) * 9.0f}, 5.5f, Color{224, 218, 196, 255});
    Txt("DEALER", l.x - MeasureTxt("DEALER", 12) / 2.0f, l.y - 30, 12, Fade(Pal::Paper, 0.7f));
    Txt("YOU", r.x - MeasureTxt("YOU", 12) / 2.0f, r.y - 30, 12, Fade(Pal::Paper, 0.7f));
    Txt(TextFormat("%+d / %d", scaleVal, SCALE_LIMIT), 124 - 26, 500, 14, scaleVal > 0 ? Color{140, 230, 160, 255} : scaleVal < 0 ? Color{240, 120, 110, 255} : Pal::Paper);
}

// ---------------------------------------------------------------- the board's geometry
constexpr float COL_X[COLS] = {430, 570, 710, 850};
Rectangle CellRect(int r, int c) {
    float w, h, cy;
    switch (r) {
        case R_FOE_QUEUE: w = 58; h = 82; cy = 318; break;
        case R_FOE_FRONT: w = 74; h = 104; cy = 394; break;
        case R_YOU_FRONT: w = 92; h = 128; cy = 518; break;
        default: w = 66; h = 92; cy = 608; break;
    }
    return {COL_X[c] - w / 2, cy - h / 2, w, h};
}
Vector2 CellCenter(int r, int c) { Rectangle q = CellRect(r, c); return {q.x + q.width / 2, q.y + q.height / 2}; }
Vector2 HandPos(int i, int n) { float off = i - (n - 1) / 2.0f; return {640 + off * 100, 716 - fabsf(off) * 5}; }
Rectangle BellRect() { return {1040, 484, 100, 90}; }

// ---------------------------------------------------------------- rules text and the folder tab
const char* RULES_TEXT =
    "You and the dealer fight across a board of four lanes. Each turn you draw a card and a free minnow, lay cards in your front "
    "row, then ring the bell: every creature strikes the space straight ahead. If it is empty the blow lands on the scales; when "
    "the scales tip 8 points either way the battle is over.\n\n"
    "Every card has STRENGTH (damage dealt), DEFENSE (damage it can take) and WEIGHT. A blow from a heavier creature knocks a "
    "survivor back a row. A heavier card may be laid onto an occupied lane of yours and shoves the occupant behind it; a mover "
    "that is not heavier than what is in its way is crushed.\n\n"
    "Costs: BLOOD means sacrificing your own creatures (click them). BONES are earned whenever any of your creatures dies. "
    "Sigils are abilities: hover a card to read them. Cards come in Foil, Gilt and Hex.\n\n"
    "Between battles you walk a map: card picks, campfires (+1 strength or defense), splices (merge sigils), sacrifices (thin the deck "
    "and start with more bones), trials (+3 strength, -2 defense), a stall to spend the pot, caches of items, and tougher dealers "
    "with tricks of their own. Win a battle in under six turns and you bank Momentum, spent for a head start next time. "
    "Cash out after any battle, or press on and risk the pot.";

Rectangle RulesTabRect() { return {(float)SCREEN_W - 34, 300, 34, 130}; }
void DrawRulesFolder(Vector2 m) {
    Rectangle tab = RulesTabRect();
    bool hot = CheckCollisionPointRec(m, tab) || U.showRules;
    Color col = hot ? Pal::Brass : ColorBrightness(Pal::Brass, -0.3f);
    DrawRectangleRounded({tab.x - 2, tab.y, tab.width + 2, tab.height}, 0.3f, 6, Color{8, 12, 16, 220});
    DrawRectangleRoundedLinesEx({tab.x - 2, tab.y, tab.width + 2, tab.height}, 0.3f, 6, 1.5f, col);
    rlPushMatrix();
    rlTranslatef(tab.x + tab.width / 2 + 5, tab.y + tab.height - 10, 0);
    rlRotatef(-90, 0, 0, 1);
    TxtBold("HOW TO PLAY", -55, -8, 15, col);
    rlPopMatrix();
    if (U.showRules) {
        Rectangle p{SCREEN_W / 2.0f - 380, SCREEN_H / 2.0f - 300, 760, 600};
        Panel(p);
        DrawTextCenteredBold("How Flats is played", p.x + p.width / 2, p.y + 16, 30, Pal::Ink);
        DrawWrapped(RULES_TEXT, {p.x + 34, p.y + 60, p.width - 68, p.height - 120}, 14, Pal::Ink);
        if (Button({p.x + p.width / 2 - 90, p.y + p.height - 54, 180, 40}, "Close")) U.showRules = false;
    }
}

Game* G = nullptr;
int PotPct(int pct, int lo) { return std::max(lo, U.rm.gs.pot * pct / 100); }

void ResetUi() {
    U = Ui();
    U.inited = true;
    U.rng.Seed((unsigned)GetRandomValue(1, 1 << 30));
}

void LeaveTable(Game& g) {
    U.inited = false;
    g.scene = Scene::Hub;
}

// ---------------------------------------------------------------- playing the engine's events back as animation
void ApplyEvents(const Events& evs, const Board& prev) {
    for (const Event& e : evs) {
        switch (e.type) {
            case Event::Play: {
                Vector2 to = CellCenter(e.r1, e.c1);
                CellFx& f = U.fx[e.r1][e.c1];
                Vector2 from = Owner(e.r1) == Side::YOU ? U.handFrom : Vector2{930, 300};
                f.off = {from.x - to.x, from.y - to.y};
                f.offT = 0; f.offDur = 0.42f; f.arc = 70; f.appear = 0.55f;
                PlaySlap();
                Burst({to.x, to.y + 30}, 5, 2, Color{150, 130, 110, 255}, 40);
            } break;
            case Event::Move: case Event::Knock: case Event::Push: {
                Vector2 a = CellCenter(e.r0, e.c0), b = CellCenter(e.r1, e.c1);
                CellFx& f = U.fx[e.r1][e.c1];
                f.off = {a.x - b.x, a.y - b.y}; f.offT = 0; f.offDur = 0.32f; f.arc = 0;
                if (e.type == Event::Push) { CellFx& g2 = U.fx[e.r0][e.c0]; g2.off = {b.x - a.x, b.y - a.y}; g2.offT = 0; g2.offDur = 0.32f; g2.arc = 0; }
                if (e.type == Event::Knock) { f.shake = 0.5f; f.flash = 0.6f; U.floats.push_back({"knocked back", {b.x, b.y - 30}, 0, Color{230, 200, 120, 255}, 15}); }
            } break;
            case Event::Strike: {
                int sc0 = (prev.massive && e.r0 == R_FOE_FRONT) ? 0 : e.c0;   // Selenis strikes from his one wide card
                CellFx& f = U.fx[e.r0][sc0];
                Vector2 a = CellCenter(e.r0, e.c0), b = CellCenter(e.r1, e.c1);
                float d = std::max(1.0f, Dist(a, b));
                f.lunge = 1; f.lungeDir = {(b.x - a.x) / d * 46, (b.y - a.y) / d * 46};
                PlaySlap();
            } break;
            case Event::Damage: {
                Vector2 c = CellCenter(e.r1, e.c1);
                CellFx& f = U.fx[e.r1][e.c1];
                f.shake = 0.5f; f.flash = 1;
                U.floats.push_back({e.text == "Spines" ? "spines -1" : TextFormat("-%d", e.amount), {c.x, c.y - 20}, 0, Color{255, 110, 90, 255}, 24});
                Burst(c, 8, 0, Color{255, 150, 90, 255}, 150);
                U.shake = std::max(U.shake, 0.12f + 0.03f * e.amount);
            } break;
            case Event::ScaleHit: {
                U.floats.push_back({TextFormat("%+d", e.c1 > 0 ? e.amount : -e.amount), {130, 340}, 0, e.c1 > 0 ? Color{140, 240, 160, 255} : Color{255, 120, 110, 255}, 30});
                Burst({124, 340}, 8 + e.amount * 2, e.c1 > 0 ? 1 : 0, Color{255, 220, 120, 255}, 140);
                U.shake = std::max(U.shake, 0.2f + 0.05f * e.amount);
                U.mood = e.c1 > 0 ? -1.0f : 1.0f; U.moodT = 1.4f;
            } break;
            case Event::Death: {
                Rectangle q = CellRect(e.r1, e.c1);
                U.ghosts.push_back({prev.cell[e.r1][e.c1].card, {q.x, q.y}, {q.width, q.height}, 0, 0.7f, true});
                Vector2 c = CellCenter(e.r1, e.c1);
                Burst(c, 12, 2, Color{170, 160, 150, 255}, 90);
                Burst(c, 5, 3, Color{190, 180, 220, 255}, 50);
                U.floats.push_back({TextFormat("+%d bone%s", e.amount, e.amount == 1 ? "" : "s"), {c.x, c.y}, 0, BONE_COL, 17});
            } break;
            case Event::SigilFired: {
                Vector2 c = CellCenter(e.r1, e.c1);
                U.floats.push_back({e.text, {c.x, c.y - 40}, 0, Color{236, 214, 160, 255}, 16});
                Burst(c, 6, 0, Color{236, 214, 160, 255}, 90);
            } break;
            case Event::Evolve: {
                Vector2 c = CellCenter(e.r1, e.c1);
                U.fx[e.r1][e.c1].flash = 1; U.fx[e.r1][e.c1].appear = 0.6f;
                U.floats.push_back({"grows into " + e.text, {c.x, c.y - 44}, 0, Color{160, 240, 200, 255}, 16});
                Burst(c, 14, 0, Color{160, 240, 200, 255}, 130);
            } break;
            case Event::PhaseChange: {
                U.shake = 1.0f;
                Toast("The Sovereign sacrifices his court. Selenis, the Moon God, rises!");
                U.floats.push_back({"PHASE TWO: THE LUNAR TIDE", {640, 330}, 0, Color{190, 215, 255, 255}, 34});
                Burst({640, 394}, 40, 3, Color{170, 200, 255, 255}, 220);
            } break;
            case Event::Tide: {
                Toast(e.amount > 0 ? "Tidal Pull drags your creatures to the right!" : "Tidal Pull drags your creatures to the left!");
                U.floats.push_back({e.amount > 0 ? "tide >>>" : "<<< tide", {640, 470}, 0, Color{150, 200, 255, 255}, 26});
                for (int i = 0; i < 14; i++) Burst({430.0f + i * 32, 520.0f}, 1, 3, Color{150, 200, 255, 255}, 60);
            } break;
            case Event::Crush: {
                Vector2 c = CellCenter(e.r1, e.c1);
                U.floats.push_back({"swept away", {c.x, c.y - 20}, 0, Color{150, 200, 255, 255}, 18});
            } break;
            case Event::Gold: {
                Vector2 c = CellCenter(e.r1, e.c1);
                U.floats.push_back({TextFormat("+%d gold", e.amount), {c.x, c.y - 30}, 0, Pal::Brass, 20});
                Burst(c, 8, 1, Color{240, 200, 80, 255}, 200);
            } break;
            default: break;
        }
    }
}

void UpdateFx(float dt) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            CellFx& f = U.fx[r][c];
            f.offT = std::min(f.offDur, f.offT + dt);
            f.lunge = std::max(0.0f, f.lunge - dt * 2.2f);
            f.shake = std::max(0.0f, f.shake - dt * 2.4f);
            f.flash = std::max(0.0f, f.flash - dt * 3.0f);
            f.appear = std::min(1.0f, f.appear + dt * 4.0f);
        }
    for (auto& g : U.ghosts) g.t += dt;
    U.ghosts.erase(std::remove_if(U.ghosts.begin(), U.ghosts.end(), [](const Ghost& g) { return g.t >= g.dur; }), U.ghosts.end());
    for (auto& f : U.floats) { f.t += dt; f.pos.y -= dt * 26; }
    U.floats.erase(std::remove_if(U.floats.begin(), U.floats.end(), [](const FloatText& f) { return f.t > 1.3f; }), U.floats.end());
    for (auto& p : U.parts) {
        p.life -= dt;
        p.p.x += p.v.x * dt; p.p.y += p.v.y * dt;
        if (p.kind == 1) p.v.y += 900 * dt;
        else if (p.kind == 2) { p.v.x *= 0.92f; p.v.y *= 0.92f; }
        else if (p.kind == 3) { p.v.y -= 40 * dt; p.v.x *= 0.98f; }
    }
    U.parts.erase(std::remove_if(U.parts.begin(), U.parts.end(), [](const Particle& p) { return p.life <= 0; }), U.parts.end());
    U.shake = std::max(0.0f, U.shake - dt * 1.4f);
    U.moodT = std::max(0.0f, U.moodT - dt);
    U.bellT = std::max(0.0f, U.bellT - dt * 1.1f);
    U.toastT = std::max(0.0f, U.toastT - dt);
    float target = -(float)U.bat.board.scale / SCALE_LIMIT * 0.34f;
    U.beam += (target - U.beam) * std::min(1.0f, dt * 3);
}

// ---------------------------------------------------------------- the map and its nodes
void StartBattle(Boon boon) {
    const MapNode& n = U.rm.Current();
    U.bat.Start(U.rm.MakeBattle(boon), U.rng);
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) U.fx[r][c] = CellFx();
    U.ghosts.clear(); U.floats.clear();
    U.selHand = -1; U.sacs.clear(); U.sacMode = false; U.selItem = -1;
    U.stepT = 0.5f; U.endT = 0;
    U.isElite = n.type == NodeType::ELITE; U.isBoss = n.type == NodeType::BOSS;
    for (int c = 0; c < COLS; c++) if (U.bat.board.cell[R_FOE_QUEUE][c].used) { U.fx[R_FOE_QUEUE][c].appear = 0.3f; U.fx[R_FOE_QUEUE][c].off = {480 - CellCenter(0, c).x, -60}; U.fx[R_FOE_QUEUE][c].offDur = 0.6f; U.fx[R_FOE_QUEUE][c].offT = 0; }
    U.ph = Ph::Battle;
}

void FinishNode() {
    U.rm.map[U.rm.layer][U.rm.slot].visited = true;
    U.nu = NodeUi::None;
    U.ph = Ph::Map;
    U.pick1 = U.pick2 = -1;
}

void StockShop() {
    U.shop.clear();
    ShopItem a; a.kind = SK_CARD; a.price = PotPct(30, 10); a.card = RandomPlayerCard(std::min(3, U.rm.Tier() + 1), U.rng); U.shop.push_back(a);
    if (U.rm.gs.Charms() < 3) {
        std::vector<int> free;
        for (int i = 0; i < CH_COUNT; i++) if (!U.rm.gs.HasCharm(i)) free.push_back(i);
        if (!free.empty()) { ShopItem c; c.kind = SK_CHARM; c.price = PotPct(50, 20); c.charm = free[U.rng.I(0, (int)free.size() - 1)]; U.shop.push_back(c); }
    }
    ShopItem it; it.kind = SK_ITEM; it.price = PotPct(15, 8); it.item = U.rng.I(0, (int)PackItem::COUNT - 1); U.shop.push_back(it);
    ShopItem t; t.kind = SK_TRIM; t.price = PotPct(20, 8); U.shop.push_back(t);
    ShopItem e; e.kind = SK_EDITION; e.price = PotPct(35, 15); e.card.edition = U.rng.I(ED_FOIL, ED_COUNT - 1); U.shop.push_back(e);
    if (!U.rm.gs.insured) { ShopItem i; i.kind = SK_INSURE; i.price = PotPct(25, 10); U.shop.push_back(i); }
}
int RerollPrice() { return PotPct(5 + 5 * U.rerolls, 5); }

void EnterNode() {
    const MapNode& n = U.rm.Current();
    U.pick1 = U.pick2 = -1;
    switch (n.type) {
        case NodeType::BATTLE: case NodeType::ELITE: case NodeType::BOSS:
            if (U.rm.gs.momentumTracker > 0) U.ph = Ph::Boon; else StartBattle(Boon::NONE);
            break;
        case NodeType::CARD_PICK: U.offers = U.rm.OfferCards(3, false); U.offerCharm = -1; U.rareOffer = false; U.nu = NodeUi::CardPick; U.ph = Ph::Node; break;
        case NodeType::CAMPFIRE: U.nu = NodeUi::Campfire; U.ph = Ph::Node; break;
        case NodeType::SPLICE: U.nu = NodeUi::Splice; U.ph = Ph::Node; break;
        case NodeType::SACRIFICE: U.nu = NodeUi::Sacrifice; U.ph = Ph::Node; break;
        case NodeType::TRIAL: U.nu = NodeUi::Trial; U.ph = Ph::Node; break;
        case NodeType::STALL: U.rerolls = 0; U.shopPick = -1; StockShop(); U.nu = NodeUi::Stall; U.ph = Ph::Node; break;
        case NodeType::CACHE: U.foundItem = U.rm.RandomItem(); U.nu = NodeUi::Cache; U.ph = Ph::Node; break;
        case NodeType::VENTS: U.ventStep = 0; U.nu = NodeUi::Vents; U.ph = Ph::Node; break;
        case NodeType::SPLICERS: U.nu = NodeUi::Splicers; U.ph = Ph::Node; break;
        case NodeType::SCRIMSHAW: {
            U.scrimSuits.clear(); U.scrimSigils.clear(); U.pickSuit = U.pickSigil = -1;
            std::vector<int> suits = {COIN, CUP, BLADE, SHELL};
            U.rng.Shuffle(suits);
            U.scrimSuits.assign(suits.begin(), suits.begin() + 3);
            std::vector<int> sigs = {(int)Sigil::SPINES, (int)Sigil::SKIMMER, (int)Sigil::BURROWER, (int)Sigil::WATERBORNE, (int)Sigil::TIDECALLER, (int)Sigil::BRINE, (int)Sigil::SENTINEL, (int)Sigil::UNDYING};
            U.rng.Shuffle(sigs);
            U.scrimSigils.assign(sigs.begin(), sigs.begin() + 3);
            U.nu = NodeUi::Scrimshaw; U.ph = Ph::Node;
        } break;
    }
}

void CashOut() {
    G->gold += U.rm.gs.pot;
    U.payout = U.rm.gs.pot;
    U.cashed = true; U.lost = false;
    U.ph = Ph::RunOver;
}

void HandleBattleEnd() {
    bool won = U.bat.winner > 0;
    U.lastTurns = U.bat.turnNo;
    int before = U.rm.gs.momentumTracker;
    U.gainedGold = U.rm.OnBattleFinished(won, U.bat.turnNo, U.bat.board.gold, U.bat.board.itemsFound[0]);
    U.gainedMomentum = won && U.rm.gs.momentumTracker > before;
    if (won) {
        U.ph = Ph::Won;
        Burst({640, 300}, 30, 1, Color{240, 200, 80, 255}, 260);
    } else {
        U.lost = true; U.cashed = false;
        U.payout = U.rm.gs.insured ? U.rm.gs.pot / 2 : 0;
        U.insurePaid = false;
        U.ph = Ph::RunOver;
    }
}

// ---------------------------------------------------------------- a battle: picking cards, sacrifices, items, the bell
int BloodAvailable() {
    int have = 0;
    for (int r : {R_YOU_FRONT, R_YOU_BACK}) for (int c = 0; c < COLS; c++) if (U.bat.board.cell[r][c].used) have += U.bat.board.cell[r][c].card.BloodValue();
    return have;
}
bool Affordable(const Card& c) {
    if (c.cost == CostType::BLOOD) return BloodAvailable() >= U.bat.BloodNeeded(c);
    if (c.cost == CostType::BONES) return U.bat.board.bones[0] >= c.costAmount;
    return true;
}

void TryPlay(int col) {
    if (U.selHand < 0 || U.selHand >= (int)U.bat.hand.size()) return;
    const Card& c = U.bat.hand[U.selHand];
    if (c.cost == CostType::BLOOD && !U.sacMode) {
        if (BloodAvailable() < U.bat.BloodNeeded(c)) { Toast("You need more creatures to sacrifice for that card."); return; }
        U.sacMode = true; U.sacCol = col; U.sacs.clear();
        return;
    }
    std::string why;
    if (!U.bat.CanPlay(U.selHand, col, U.sacs, &why)) { Toast(why); if (U.sacMode) { U.sacMode = false; U.sacs.clear(); } return; }
    Board prev = U.bat.board;
    U.handFrom = HandPos(U.selHand, (int)U.bat.hand.size());
    U.ev.clear();
    U.bat.Play(U.selHand, col, U.sacs, U.ev, U.rng);
    ApplyEvents(U.ev, prev);
    U.selHand = -1; U.sacs.clear(); U.sacMode = false;
}

void UseItemAt(int slot, int r, int c) {
    Board prev = U.bat.board;
    U.ev.clear();
    if (U.bat.UseItem(slot, r, c, U.ev)) { ApplyEvents(U.ev, prev); U.selItem = -1; }
    else Toast("It can't be used there.");
}

// ---------------------------------------------------------------- deck grids (used by the campfire, splice, trial, stall and the deck viewer)
int DeckGrid(Rectangle area, const std::vector<Card>& d, const std::vector<int>& sel, Vector2 m, const std::function<bool(int)>& enabled = nullptr) {
    int n = (int)d.size(), cols = 8, clicked = -1;
    int rows = std::max(1, (n + cols - 1) / cols);
    float ch = std::min(112.0f, area.height / rows - 8), cw = ch * 0.72f, gapX = area.width / cols;
    for (int i = 0; i < n; i++) {
        Rectangle r{area.x + (i % cols) * gapX + (gapX - cw) / 2, area.y + (i / cols) * (ch + 8), cw, ch};
        bool ok = !enabled || enabled(i), hov = CheckCollisionPointRec(m, r);
        if (hov && ok) r.y -= 6;
        bool isSel = std::find(sel.begin(), sel.end(), i) != sel.end();
        if (isSel) { DrawRectangleRounded({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 0.1f, 6, Fade(Pal::Brass, 0.9f)); }
        DrawCardFace(r, d[i], true);
        if (!ok) DrawRectangleRounded(r, 0.08f, 6, Fade(BLACK, 0.55f));
        if (hov) Hover(d[i]);
        if (hov && ok && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clicked = i;
    }
    return clicked;
}

// ---------------------------------------------------------------- drawing the battle
// Selenis, the Moon God: one enormous card across all four of the dealer's lanes.
void DrawSelenis(Rectangle w, const Card& c, float t, float flash) {
    DrawRectangleRounded({w.x + 3, w.y + 4, w.width, w.height}, 0.12f, 8, Fade(BLACK, 0.5f));
    DrawRectangleRounded(w, 0.12f, 8, Color{16, 26, 52, 255});
    DrawRectangleRounded({w.x + 4, w.y + 4, w.width - 8, w.height - 8}, 0.12f, 8, Color{22, 36, 72, 255});
    DrawRectangleRoundedLinesEx(w, 0.12f, 8, 3, Color{150, 180, 230, 255});
    for (int k = 1; k < COLS; k++) { float x = (COL_X[k - 1] + COL_X[k]) / 2; DrawLineEx({x, w.y + 10}, {x, w.y + w.height - 10}, 1, Fade(Color{150, 180, 230, 255}, 0.18f)); }
    Vector2 mc{w.x + w.width / 2, w.y + w.height / 2};
    Glow(mc, 100, Fade(Color{150, 190, 255, 255}, 0.22f + 0.05f * sinf(t * 1.6f)));
    DrawCreaturePixels(c.name, {mc.x - 50, w.y + 8, 100, w.height - 16}, 1.0f);
    TxtBold("SELENIS, THE MOON GOD", w.x + 16, w.y + 8, 15, Color{220, 232, 255, 255});
    Txt("Massive  -  Tidal Pull", w.x + 16, w.y + 28, 12, Fade(Color{220, 232, 255, 255}, 0.8f));
    // his health, as a long bar
    Rectangle bar{w.x + 16, w.y + w.height - 26, w.width - 32, 12};
    DrawRectangleRounded(bar, 0.5f, 6, Color{8, 10, 20, 255});
    DrawRectangleRounded({bar.x, bar.y, bar.width * std::clamp((float)c.hp / c.defense, 0.0f, 1.0f), bar.height}, 0.5f, 6, Color{110, 170, 240, 255});
    TxtBold(TextFormat("%d / %d", c.hp, c.defense), bar.x + bar.width / 2 - 24, bar.y - 1, 12, WHITE);
    DrawCircleV({w.x + 30, w.y + w.height - 50}, 15, Color{200, 60, 50, 255}); TxtBold("1", w.x + 26, w.y + w.height - 60, 18, WHITE);
    if (flash > 0) DrawRectangleRounded(w, 0.12f, 8, Fade(WHITE, 0.5f * flash));
}

void DrawBoardCard(int r, int c, float t) {
    const Cell& x = U.bat.board.cell[r][c];
    if (!x.used) return;
    CellFx& f = U.fx[r][c];
    Rectangle q = CellRect(r, c);
    if (U.bat.board.massive && r == R_FOE_FRONT) {
        float ox = (f.shake > 0 ? sinf(t * 90) * f.shake * 8 : 0) + (f.lunge > 0 ? f.lungeDir.x * sinf((1 - f.lunge) * PI) * 0.4f : 0);
        float oy = f.lunge > 0 ? f.lungeDir.y * sinf((1 - f.lunge) * PI) * 0.6f : 0;
        DrawSelenis({COL_X[0] - 42 + ox, q.y - 6 + oy, COL_X[3] - COL_X[0] + 84, q.height + 12}, x.card, t, f.flash);
        return;
    }
    float k = f.offDur > 0 ? std::clamp(f.offT / f.offDur, 0.0f, 1.0f) : 1.0f, ease = 1 - powf(1 - k, 3);
    Vector2 o{f.off.x * (1 - ease), f.off.y * (1 - ease) - sinf(k * PI) * f.arc};
    if (f.lunge > 0) { float a = sinf((1 - f.lunge) * PI); o.x += f.lungeDir.x * a; o.y += f.lungeDir.y * a; }
    if (f.shake > 0) o.x += sinf(t * 90) * f.shake * 8;
    float sc = f.appear;
    Rectangle rr{q.x + o.x + q.width * (1 - sc) / 2, q.y + o.y + q.height * (1 - sc) / 2, q.width * sc, q.height * sc};
    int str = U.bat.board.EffStrength(r, c);
    DrawCardFace(rr, x.card, true, x.card.hp, str);
    if (r == R_FOE_QUEUE) DrawRectangleRounded(rr, 0.08f, 6, Fade(BLACK, 0.3f)); // waiting in the queue
    if (f.flash > 0) DrawRectangleRounded(rr, 0.08f, 6, Fade(WHITE, 0.55f * f.flash));
}

void DrawBattle(Game& g, float dt, float t, Vector2 m, bool modal) {
    (void)g;
    Battle& bat = U.bat;
    // ---- the automatic phases, one micro-step at a time
    if (bat.turn == Turn::YOU_DRAW) {
        U.stepT -= dt;
        if (U.stepT <= 0) { U.ev.clear(); bat.Draw(true, U.ev, U.rng); U.stepT = 0.15f; U.selHand = -1; U.sacs.clear(); U.sacMode = false; U.selItem = -1; }
    } else if (bat.turn == Turn::OVER) {
        U.endT += dt;
        if (U.endT > 1.1f && U.ph == Ph::Battle) HandleBattleEnd();
    } else if (!bat.Waiting()) {
        U.stepT -= dt;
        if (U.stepT <= 0) {
            Board prev = bat.board;
            U.ev.clear();
            bat.Advance(U.ev, U.rng);
            ApplyEvents(U.ev, prev);
            bool strike = false, play = false;
            for (auto& e : U.ev) { strike |= e.type == Event::Strike; play |= e.type == Event::Play; }
            U.stepT = strike ? 0.62f : play ? 0.6f : 0.28f;
        }
    }
    bool myMain = bat.turn == Turn::YOU_MAIN && !modal && U.ph == Ph::Battle;

    // ---- the slots
    for (int c = 0; c < COLS; c++) {
        DrawSlot(CellRect(R_FOE_QUEUE, c), false, t);
        DrawSlot(CellRect(R_FOE_FRONT, c), false, t);
        bool hotCol = false;
        Rectangle lane{COL_X[c] - 50, 440, 100, 150};
        if (myMain && U.selHand >= 0 && CheckCollisionPointRec(m, lane)) hotCol = true;
        DrawSlot(CellRect(R_YOU_FRONT, c), hotCol || (myMain && U.selHand >= 0 && !U.sacMode), t);
    }
    // the dealer's telegraph: a small brass arrow under each queued card, where it will step
    for (int c = 0; c < COLS; c++) if (bat.board.cell[R_FOE_QUEUE][c].used) DrawTri({COL_X[c] - 7, 362}, {COL_X[c] + 7, 362}, {COL_X[c], 372}, Fade(Pal::Brass, 0.7f));

    // ---- the cards: reserve and queue first (they sit behind), then the fronts
    for (int r : {R_YOU_BACK, R_FOE_QUEUE, R_FOE_FRONT, R_YOU_FRONT})
        for (int c = 0; c < COLS; c++) DrawBoardCard(r, c, t);
    for (auto& gh : U.ghosts) { // the dead fade and sink
        float a = 1 - gh.t / gh.dur;
        Rectangle q{gh.pos.x + gh.size.x * (1 - a) * 0.15f, gh.pos.y + (1 - a) * 24, gh.size.x * (0.85f + 0.15f * a), gh.size.y * (0.85f + 0.15f * a)};
        DrawCardFace(q, gh.c, true, 0);
        DrawRectangleRounded(q, 0.08f, 6, Fade(BLACK, 0.6f * (1 - a)));
        DrawRectangleRounded(q, 0.08f, 6, Fade(WHITE, 0.0f));
    }
    // the dealer's hand, as a small fan of backs
    for (int i = 0; i < (int)bat.foeHand.size(); i++) DrawCardFace({930.0f + i * 14, 292.0f - (i % 2) * 3, 34, 48}, Card{}, false);

    // ---- hover and selection on the board
    int hoverR = -1, hoverC = -1;
    if (bat.board.massive && CheckCollisionPointRec(m, {COL_X[0] - 42, 340, COL_X[3] - COL_X[0] + 84, 116})) { hoverR = R_FOE_FRONT; hoverC = 0; }
    for (int r : {R_YOU_FRONT, R_FOE_FRONT, R_YOU_BACK, R_FOE_QUEUE}) {
        for (int c = 0; c < COLS && hoverR < 0; c++) if (bat.board.cell[r][c].used && CheckCollisionPointRec(m, CellRect(r, c))) { hoverR = r; hoverC = c; }
        if (hoverR >= 0) break;
    }
    if (hoverR >= 0 && !modal) Hover(bat.board.cell[hoverR][hoverC].card, bat.board.cell[hoverR][hoverC].card.hp, bat.board.EffStrength(hoverR, hoverC));
    // sacrifice marks and targeting highlights
    if (U.sacMode)
        for (int r : {R_YOU_FRONT, R_YOU_BACK})
            for (int c = 0; c < COLS; c++)
                if (bat.board.cell[r][c].used) {
                    Rectangle q = CellRect(r, c);
                    bool marked = std::find(U.sacs.begin(), U.sacs.end(), std::make_pair(r, c)) != U.sacs.end();
                    DrawRectangleRoundedLinesEx({q.x - 3, q.y - 3, q.width + 6, q.height + 6}, 0.1f, 6, marked ? 4.0f : 2.0f, marked ? BLOOD_COL : Fade(BLOOD_COL, 0.5f + 0.3f * sinf(t * 6)));
                    if (marked) DrawDrop({q.x + q.width / 2, q.y + q.height / 2}, 46, Fade(BLOOD_COL, 0.85f));
                }
    if (U.selItem >= 0 && U.selItem < (int)bat.items.size()) {
        PackItem k = (PackItem)bat.items[U.selItem];
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                bool ok = bat.board.cell[r][c].used && (k == PackItem::BANDAGE ? (r >= 2) : k == PackItem::HARPOON ? (r <= 1) : (r == R_FOE_QUEUE));
                if (!ok) continue;
                Rectangle q = CellRect(r, c);
                DrawRectangleRoundedLinesEx({q.x - 3, q.y - 3, q.width + 6, q.height + 6}, 0.1f, 6, 3, Fade(Pal::Brass, 0.6f + 0.3f * sinf(t * 6)));
            }
    }

    // ---- your hand, fanned along the bottom
    int n = (int)bat.hand.size(), hoverHand = -1;
    for (int i = n - 1; i >= 0 && hoverHand < 0; i--) {
        Vector2 p = HandPos(i, n);
        float lift = U.selHand == i ? 62 : 0;
        Rectangle hit{p.x - 48, p.y - 66 - lift, 96, 150};
        if (CheckCollisionPointRec(m, hit) && !modal) hoverHand = i;
    }
    for (int i = 0; i < n; i++) {
        Vector2 p = HandPos(i, n);
        float off = i - (n - 1) / 2.0f, lift = U.selHand == i ? 62 : (hoverHand == i ? 34 : 0);
        rlPushMatrix();
        rlTranslatef(p.x, p.y - lift, 0);
        rlRotatef(off * 3.5f, 0, 0, 1);
        Rectangle cr{-46, -66, 92, 128};
        DrawCardFace(cr, bat.hand[i], true);
        if (!Affordable(bat.hand[i]) && bat.turn == Turn::YOU_MAIN) DrawRectangleRounded(cr, 0.08f, 6, Fade(BLACK, 0.5f));
        rlPopMatrix();
    }
    if (hoverHand >= 0) Hover(bat.hand[hoverHand]);

    // ---- bones, momentum, items on the table's near edge
    {
        Vector2 b{150, 600};
        DrawRectangleRounded({b.x - 46, b.y - 30, 130, 64}, 0.2f, 6, Color{8, 12, 16, 200});
        DrawBone({b.x - 20, b.y}, 34, BONE_COL);
        TxtBold(TextFormat("x %d", bat.board.bones[0]), b.x + 4, b.y - 14, 26, BONE_COL);
        Txt("bones", b.x + 6, b.y + 14, 12, Fade(Pal::Paper, 0.7f));
        for (int i = 0; i < MAX_MOMENTUM; i++) DrawCircleV({b.x - 26 + i * 24.0f, b.y + 54}, 8, i < U.rm.gs.momentumTracker ? Color{170, 220, 240, 255} : Color{40, 50, 60, 255});
        Txt("momentum", b.x - 42, b.y + 66, 11, Fade(Pal::Paper, 0.6f));
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        Vector2 p{1150, 520.0f + i * 62};
        DrawRectangleRounded({p.x - 26, p.y - 28, 52, 56}, 0.2f, 6, Color{8, 12, 16, 190});
        DrawRectangleRoundedLinesEx({p.x - 26, p.y - 28, 52, 56}, 0.2f, 6, U.selItem == i ? 3.0f : 1.5f, U.selItem == i ? Pal::Brass : Pal::BrassDk);
        if (i < (int)bat.items.size()) {
            DrawBottle(bat.items[i], p, 46);
            if (CheckCollisionPointRec(m, {p.x - 26, p.y - 28, 52, 56}) && !modal) {
                Rectangle tip{p.x - 250, p.y - 18, 216, 44};
                DrawRectangleRounded(tip, 0.2f, 6, Color{8, 12, 16, 240});
                TxtBold(ItemName(bat.items[i]), tip.x + 8, tip.y + 4, 14, Pal::Brass);
                DrawWrapped(ItemText(bat.items[i]), {tip.x + 8, tip.y + 20, tip.width - 16, 24}, 11, Pal::Paper);
                if (myMain && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (ItemNeedsTarget(bat.items[i])) { U.selItem = U.selItem == i ? -1 : i; U.selHand = -1; U.sacMode = false; }
                    else UseItemAt(i, 0, 0);
                }
            }
        }
    }
    DrawBell({1090, 534}, myMain, U.bellT);

    // ---- input
    if (myMain) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { U.selHand = -1; U.sacs.clear(); U.sacMode = false; U.selItem = -1; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool hoverItemSlot = false;
            for (int i = 0; i < MAX_ITEMS; i++) hoverItemSlot |= CheckCollisionPointRec(m, {1124, 492.0f + i * 62, 52, 56});
            if (CheckCollisionPointRec(m, BellRect())) {
                U.bellT = 1; U.shake = std::max(U.shake, 0.1f); PlayBell();
                U.selHand = -1; U.sacs.clear(); U.sacMode = false; U.selItem = -1;
                bat.EndTurn();
                U.stepT = 0.5f;
            } else if (hoverItemSlot) {
                // handled above
            } else if (U.selItem >= 0) {
                if (hoverR >= 0) UseItemAt(U.selItem, hoverR, hoverC);
            } else if (U.sacMode) {
                if (hoverR == R_YOU_FRONT || hoverR == R_YOU_BACK) {
                    auto key = std::make_pair(hoverR, hoverC);
                    auto it = std::find(U.sacs.begin(), U.sacs.end(), key);
                    if (it != U.sacs.end()) U.sacs.erase(it); else U.sacs.push_back(key);
                    int blood = 0;
                    for (auto& s : U.sacs) blood += bat.board.cell[s.first][s.second].card.BloodValue();
                    if (U.selHand >= 0 && blood >= bat.BloodNeeded(bat.hand[U.selHand])) TryPlay(U.sacCol);
                }
            } else if (hoverHand >= 0) {
                U.selHand = U.selHand == hoverHand ? -1 : hoverHand;
                U.sacs.clear(); U.sacMode = false;
            } else if (U.selHand >= 0) {
                for (int c = 0; c < COLS; c++) if (CheckCollisionPointRec(m, {COL_X[c] - 50, 440, 100, 150})) { TryPlay(c); break; }
            }
        }
    }

    // ---- the turn hint
    const char* hint = "";
    switch (bat.turn) {
        case Turn::YOU_DRAW: hint = "You draw a card and a minnow..."; break;
        case Turn::YOU_MAIN:
            hint = U.sacMode ? TextFormat("Click your creatures to sacrifice: %d blood needed. Right-click cancels.", U.selHand >= 0 ? bat.BloodNeeded(bat.hand[U.selHand]) : 0)
                   : U.selItem >= 0 ? "Click a target for the item." : U.selHand >= 0 ? "Click a lane to play the card there. Right-click puts it back." : "Pick a card, then a lane. Ring the bell to fight."; break;
        case Turn::YOU_COMBAT: case Turn::YOU_END: hint = "Your creatures strike..."; break;
        case Turn::OVER: hint = bat.winner > 0 ? "The scales tip your way!" : "The scales tip against you..."; break;
        default: hint = "The dealer's turn..."; break;
    }
    DrawRectangleRounded({SCREEN_W / 2.0f - 360, 14, 720, 26}, 0.5f, 6, Color{8, 12, 16, 190});
    DrawTextCentered(hint, SCREEN_W / 2.0f, 18, 16, Color{214, 222, 226, 255});
    const DealerInfo& di = Dealer(bat.dealer);
    if (bat.boss) DrawTextCentered(TextFormat("%s   -   Phase %d: %s", di.name, bat.phase, bat.phase == 1 ? "The Drowned Phalanx" : "The Lunar Tide"), SCREEN_W / 2.0f, 46, 15, Fade(Color{190, 215, 255, 255}, 0.95f));
    else DrawTextCentered(TextFormat("%s%s: %s", U.isElite ? "Elite " : "", di.name, di.twist), SCREEN_W / 2.0f, 46, 14, Fade(Pal::Brass, 0.85f));
}

// ---------------------------------------------------------------- the map
Color NodeCol(NodeType t) {
    switch (t) {
        case NodeType::BATTLE: return {190, 96, 74, 255}; case NodeType::ELITE: return {170, 100, 200, 255}; case NodeType::CARD_PICK: return {90, 150, 210, 255};
        case NodeType::CAMPFIRE: return {230, 150, 70, 255}; case NodeType::SPLICE: return {110, 190, 130, 255}; case NodeType::SACRIFICE: return {190, 60, 70, 255};
        case NodeType::TRIAL: return {226, 200, 90, 255}; case NodeType::STALL: return {200, 160, 70, 255}; case NodeType::CACHE: return {190, 160, 120, 255};
        case NodeType::VENTS: return {232, 110, 60, 255}; case NodeType::SCRIMSHAW: return {225, 214, 186, 255}; case NodeType::SPLICERS: return {150, 120, 214, 255};
        default: return {120, 70, 170, 255};
    }
}

void DrawStatusBar(Vector2 m, float y, bool modal) {
    // the deck, charms, pack and pot along the bottom of a panel
    if (!modal && Button({80, y, 130, 34}, TextFormat("Deck (%d)", (int)U.rm.gs.deck.size()), true, 15)) U.showDeck = true;
    int shown = 0;
    for (int c = 0; c < CH_COUNT; c++) {
        if (!U.rm.gs.HasCharm(c)) continue;
        Vector2 cc{250.0f + shown * 44, y + 17};
        DrawCharmIcon(c, cc, 36, 0);
        if (!modal && Dist(m, cc) < 19) Tooltip(std::string(CharmName(c)) + ": " + CharmText(c), {m.x, m.y + 24});
        shown++;
    }
    for (int i = 0; i < (int)U.rm.gs.items.size(); i++) {
        Vector2 p{520.0f + i * 50, y + 15};
        DrawBottle(U.rm.gs.items[i], p, 40);
        if (!modal && Dist(m, p) < 22) Tooltip(std::string(ItemName(U.rm.gs.items[i])) + ": " + ItemText(U.rm.gs.items[i]), {m.x, m.y + 24});
    }
    for (int i = 0; i < MAX_MOMENTUM; i++) DrawCircleV({710.0f + i * 22, y + 17}, 8, i < U.rm.gs.momentumTracker ? Color{170, 220, 240, 255} : Color{70, 60, 50, 255});
    Txt("momentum", 700, y + 30, 11, Fade(Pal::Ink, 0.6f));
    Txt(TextFormat("Starting bones: %d", U.rm.gs.startBones), 800, y + 8, 15, Pal::Ink);
    for (size_t i = 0; i < U.rm.gs.totems.size(); i++) {   // scrimshaw totems
        Vector2 tp{980.0f + i * 34, y + 17};
        DrawCircleV(tp, 15, Color{226, 214, 184, 255}); DrawCircleV(tp, 12, Color{62, 46, 34, 255});
        DrawSuitIcon(U.rm.gs.totems[i].first, tp, 16, SUIT_COL[U.rm.gs.totems[i].first]);
        if (!modal && Dist(m, tp) < 16) Tooltip(std::string(SuitName(U.rm.gs.totems[i].first)) + " creatures gain " + InfoOf((Sigil)U.rm.gs.totems[i].second).name + " when played.", {m.x, m.y + 24});
    }
}

void DrawMap(Game& g, float t, Vector2 m, bool modal) {
    (void)g;
    Rectangle mp{50, 84, 1180, 560};
    Panel(mp);
    DrawTextCenteredBold("The Deep Table", mp.x + mp.width / 2, mp.y + 12, 30, Pal::Ink);
    DrawTextCentered(U.rm.layer < 0 ? "Choose where to begin." : "Choose the next table.", mp.x + mp.width / 2, mp.y + 48, 16, Pal::BrassDk);
    float x0 = mp.x + 90, x1 = mp.x + mp.width - 90;
    auto pos = [&](int l, int s) { int w = (int)U.rm.map[l].size(); return Vector2{x0 + (x1 - x0) * l / (MAP_LAYERS - 1), mp.y + 270 + (s - (w - 1) / 2.0f) * 128}; };
    auto reach = U.rm.Reachable();
    int nextLayer = U.rm.layer + 1;
    for (int l = 0; l + 1 < MAP_LAYERS; l++)
        for (int s = 0; s < (int)U.rm.map[l].size(); s++)
            for (int nx : U.rm.map[l][s].next) {
                bool live = l == U.rm.layer && s == U.rm.slot;
                Color ec = live ? Color{120, 40, 30, 255} : Color{120, 90, 60, 120};
                Vector2 a = pos(l, s), b = pos(l + 1, nx);
                for (int k = 0; k < 14; k += 2) DrawLineEx({a.x + (b.x - a.x) * k / 14.0f, a.y + (b.y - a.y) * k / 14.0f}, {a.x + (b.x - a.x) * (k + 1) / 14.0f, a.y + (b.y - a.y) * (k + 1) / 14.0f}, live ? 3.0f : 2.0f, ec);
            }
    int hovL = -1, hovS = -1;
    for (int l = 0; l < MAP_LAYERS; l++)
        for (int s = 0; s < (int)U.rm.map[l].size(); s++) {
            const MapNode& n = U.rm.map[l][s];
            Vector2 p = pos(l, s);
            float rad = n.type == NodeType::BOSS ? 40 : 28;
            bool canGo = l == nextLayer && std::find(reach.begin(), reach.end(), s) != reach.end();
            bool here = l == U.rm.layer && s == U.rm.slot;
            bool hov = Dist(m, p) < rad + 4;
            Color col = NodeCol(n.type);
            bool past = l <= U.rm.layer && !here;
            if (canGo) Glow(p, rad * 2.0f, Fade(col, 0.22f + 0.1f * sinf(t * 4)));
            DrawCircleV({p.x + 2, p.y + 3}, rad, Fade(BLACK, 0.3f));
            DrawCircleV(p, rad, Color{46, 32, 22, 255});
            DrawCircleV(p, rad - 3, past ? Color{90, 80, 70, 255} : Fade(ColorBrightness(col, -0.45f), 1.0f));
            DrawNodeIcon(n.type, p, rad * 0.62f, past ? Color{150, 140, 130, 255} : ColorBrightness(col, 0.25f));
            DrawRing(p, rad - 3, rad, 0, 360, 28, canGo ? Fade(Pal::Brass, hov ? 1.0f : 0.8f) : here ? Color{240, 240, 240, 255} : Color{20, 14, 10, 255});
            if (here) DrawTri({p.x - 8, p.y - rad - 16}, {p.x + 8, p.y - rad - 16}, {p.x, p.y - rad - 4}, Color{230, 230, 240, 255});
            if (hov) { hovL = l; hovS = s; }
            if (canGo && hov && !modal && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { U.rm.Enter(s); EnterNode(); return; }
        }
    if (hovL >= 0 && !modal) {
        const MapNode& n = U.rm.map[hovL][hovS];
        std::string txt = std::string(NodeName(n.type)) + (n.type == NodeType::BATTLE || n.type == NodeType::ELITE ? std::string(" vs ") + Dealer(n.dealer).name : "") + ": " + NodeText(n.type);
        Tooltip(txt, {m.x, m.y + 30});
    }
    DrawStatusBar(m, mp.y + mp.height - 64, modal);
    if (!modal && Button({mp.x + mp.width - 250, mp.y + mp.height - 64, 220, 36}, TextFormat("Cash out %d gold", U.rm.gs.pot), true, 15)) CashOut();
    Txt(TextFormat("Pot: %d gold%s", U.rm.gs.pot, U.rm.gs.insured ? "  (insured)" : ""), mp.x + mp.width - 250, mp.y + mp.height - 88, 16, Pal::Ink);
}

// ---------------------------------------------------------------- node screens
void DrawNodePanel(Game& g, float t, Vector2 m, bool modal) {
    (void)g; (void)t;
    Rectangle p{110, 84, 1060, 560};
    GameState& gs = U.rm.gs;
    switch (U.nu) {
        case NodeUi::CardPick: {
            Panel(p);
            DrawTextCenteredBold(U.rareOffer ? "A rare find: choose one" : "Choose a card for your deck", p.x + p.width / 2, p.y + 20, 32, Pal::Ink);
            DrawTextCentered("It joins your deck for the rest of the run.", p.x + p.width / 2, p.y + 66, 16, Pal::BrassDk);
            int slots = (int)U.offers.size() + (U.offerCharm >= 0 ? 1 : 0);
            for (int k = 0; k < slots; k++) {
                Rectangle r{p.x + p.width / 2 - slots * 110.0f + k * 220.0f + 20, p.y + 130, 170, 238};
                bool hov = CheckCollisionPointRec(m, r) && !modal;
                if (hov) r.y -= 10;
                bool charm = k >= (int)U.offers.size();
                if (charm) DrawCharmCard(r, U.offerCharm); else DrawCardFace(r, U.offers[k], true);
                if (hov && !charm) Hover(U.offers[k]);
                if (hov && charm) Tooltip(std::string(CharmName(U.offerCharm)) + ": " + CharmText(U.offerCharm), {m.x, m.y + 24});
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (charm) gs.charms |= 1u << U.offerCharm; else U.rm.TakeCard(U.offers[k]);
                    FinishNode();
                    return;
                }
            }
            if (!modal && Button({p.x + p.width / 2 - 90, p.y + p.height - 60, 180, 40}, "Take nothing", true, 15)) FinishNode();
        } break;
        case NodeUi::Campfire: case NodeUi::Trial: case NodeUi::Sacrifice: {
            bool camp = U.nu == NodeUi::Campfire, trial = U.nu == NodeUi::Trial;
            Panel(p);
            DrawTextCenteredBold(camp ? "The Campfire" : trial ? "The Trial" : "The Maelstrom", p.x + p.width / 2, p.y + 16, 32, Pal::Ink);
            DrawTextCentered(camp ? "Pick a card, then choose what the fire gives it." : trial ? "Pick a card: -2 defense, +3 strength, for good." : "A vortex takes one card from your deck for good. In return, every battle starts with an extra bone.",
                             p.x + p.width / 2, p.y + 60, 16, Pal::BrassDk);
            std::vector<int> sel; if (U.pick1 >= 0) sel.push_back(U.pick1);
            int clicked = DeckGrid({p.x + 30, p.y + 100, p.width - 60, p.height - 210}, gs.deck, sel, m,
                                   [&](int i) { return trial ? gs.deck[i].defense > 2 : (!(U.nu == NodeUi::Sacrifice) || gs.deck.size() > 8); });
            if (clicked >= 0 && !modal) U.pick1 = clicked;
            if (U.pick1 >= 0 && U.pick1 < (int)gs.deck.size()) {
                const Card& c = gs.deck[U.pick1];
                if (camp) {
                    if (Button({p.x + p.width / 2 - 250, p.y + p.height - 90, 230, 44}, TextFormat("+1 strength (%d -> %d)", c.strength, c.strength + 1), true, 15)) { U.rm.Campfire(U.pick1, true); FinishNode(); return; }
                    if (Button({p.x + p.width / 2 + 20, p.y + p.height - 90, 230, 44}, TextFormat("+1 defense (%d -> %d)", c.defense, c.defense + 1), true, 15)) { U.rm.Campfire(U.pick1, false); FinishNode(); return; }
                } else if (trial) {
                    if (Button({p.x + p.width / 2 - 150, p.y + p.height - 90, 300, 44}, TextFormat("Face the trial: %d/%d -> %d/%d", c.strength, c.defense, c.strength + 3, c.defense - 2), true, 15)) { U.rm.Trial(U.pick1); FinishNode(); return; }
                } else {
                    if (Button({p.x + p.width / 2 - 150, p.y + p.height - 90, 300, 44}, TextFormat("Give up %s", c.name.c_str()), gs.deck.size() > 8, 15)) { U.rm.SacrificeCard(U.pick1); FinishNode(); return; }
                }
            } else DrawTextCentered("Click a card.", p.x + p.width / 2, p.y + p.height - 78, 16, Fade(Pal::Ink, 0.6f));
            if (!modal && Button({p.x + p.width - 210, p.y + p.height - 90, 170, 40}, "Walk on", true, 15)) FinishNode();
        } break;
        case NodeUi::Splice: {
            Panel(p);
            DrawTextCenteredBold("The Barnacle Cluster", p.x + p.width / 2, p.y + 16, 32, Pal::Ink);
            DrawTextCentered(U.pick1 < 0 ? "First choose the card to SACRIFICE: its sigils will be encrusted onto another." : U.pick2 < 0 ? "Now choose the host that keeps them (up to three sigils)." : "Confirm the encrusting.",
                             p.x + p.width / 2, p.y + 60, 16, Pal::BrassDk);
            std::vector<int> sel; if (U.pick1 >= 0) sel.push_back(U.pick1); if (U.pick2 >= 0) sel.push_back(U.pick2);
            int clicked = DeckGrid({p.x + 30, p.y + 100, p.width - 60, p.height - 210}, gs.deck, sel, m, [&](int i) { return U.pick1 < 0 ? !gs.deck[i].sigils.empty() : i != U.pick1; });
            if (clicked >= 0 && !modal) { if (U.pick1 < 0) U.pick1 = clicked; else U.pick2 = clicked; }
            if (U.pick1 >= 0 && U.pick2 >= 0) {
                Card k = gs.deck[U.pick2]; for (Sigil s : gs.deck[U.pick1].sigils) k.AddSigil(s);
                if (Button({p.x + p.width / 2 - 200, p.y + p.height - 90, 400, 44}, TextFormat("Splice: %s absorbs %s", k.name.c_str(), gs.deck[U.pick1].name.c_str()), true, 15)) { U.rm.Splice(U.pick2, U.pick1); FinishNode(); return; }
            }
            if (!modal && Button({p.x + 40, p.y + p.height - 90, 150, 40}, "Start over", true, 15)) U.pick1 = U.pick2 = -1;
            if (!modal && Button({p.x + p.width - 210, p.y + p.height - 90, 170, 40}, "Walk on", true, 15)) FinishNode();
        } break;
        case NodeUi::Vents: {
            Panel(p);
            DrawTextCenteredBold("The Boiling Vents", p.x + p.width / 2, p.y + 16, 32, Pal::Ink);
            DrawTextCentered("Hold a creature in the heat for +1 strength or +1 defense. Each warming after the first risks it boiling away.", p.x + p.width / 2, p.y + 60, 16, Pal::BrassDk);
            std::vector<int> sel; if (U.pick1 >= 0) sel.push_back(U.pick1);
            int clicked = DeckGrid({p.x + 30, p.y + 100, p.width - 60, p.height - 210}, gs.deck, sel, m);
            if (clicked >= 0 && !modal) { if (clicked != U.pick1) U.ventStep = 0; U.pick1 = clicked; }
            if (U.pick1 >= 0 && U.pick1 < (int)gs.deck.size()) {
                const Card& c = gs.deck[U.pick1];
                int risk = 25 * U.ventStep;
                Txt(U.ventStep == 0 ? "The first warming is safe." : TextFormat("Risk of boiling away: %d%%", std::min(100, risk)), p.x + p.width / 2 - 120, p.y + p.height - 128, 17, U.ventStep == 0 ? Pal::Good : Pal::Bad);
                for (int k = 0; k < 2; k++) {
                    bool str = k == 0;
                    if (Button({p.x + p.width / 2 - 250 + k * 270.0f, p.y + p.height - 90, 230, 44}, str ? TextFormat("Warm: +1 strength (%d)", c.strength + 1) : TextFormat("Warm: +1 defense (%d)", c.defense + 1), true, 15)) {
                        std::string nm = c.name;
                        if (!U.rm.Vent(U.pick1, str, U.ventStep)) { Toast(nm + " boiled away in the vents."); FinishNode(); return; }
                        U.ventStep++;
                        Toast(nm + " grows warmer.");
                    }
                }
            } else DrawTextCentered("Click a creature to hold over the vents.", p.x + p.width / 2, p.y + p.height - 78, 16, Fade(Pal::Ink, 0.6f));
            if (!modal && Button({p.x + p.width - 210, p.y + p.height - 90, 170, 40}, "Step away", true, 15)) FinishNode();
        } break;
        case NodeUi::Splicers: {
            Panel(p);
            DrawTextCenteredBold("The Abyssal Splicers", p.x + p.width / 2, p.y + 16, 32, Pal::Ink);
            DrawTextCentered(U.pick1 < 0 ? "Twin mutants fuse two copies of the same card. Choose the first." : U.pick2 < 0 ? "Now choose its twin." : "Confirm the fusion.", p.x + p.width / 2, p.y + 60, 16, Pal::BrassDk);
            auto twin = [&](int i) { for (int j = 0; j < (int)gs.deck.size(); j++) if (j != i && gs.deck[j].name == gs.deck[i].name) return true; return false; };
            bool any = false; for (int i = 0; i < (int)gs.deck.size(); i++) any |= twin(i);
            std::vector<int> sel; if (U.pick1 >= 0) sel.push_back(U.pick1); if (U.pick2 >= 0) sel.push_back(U.pick2);
            int clicked = DeckGrid({p.x + 30, p.y + 100, p.width - 60, p.height - 210}, gs.deck, sel, m, [&](int i) { return U.pick1 < 0 ? twin(i) : (i != U.pick1 && gs.deck[i].name == gs.deck[U.pick1].name); });
            if (clicked >= 0 && !modal) { if (U.pick1 < 0) U.pick1 = clicked; else U.pick2 = clicked; }
            if (!any) DrawTextCentered("You carry no matching pair. The twins shrug.", p.x + p.width / 2, p.y + p.height - 78, 17, Pal::Bad);
            if (U.pick1 >= 0 && U.pick2 >= 0) {
                const Card &a = gs.deck[U.pick1], &b = gs.deck[U.pick2];
                if (Button({p.x + p.width / 2 - 220, p.y + p.height - 90, 440, 44}, TextFormat("Fuse: %d/%d -> %d/%d, weight %d", a.strength, a.defense, a.strength + b.strength, a.defense + b.defense, std::max(a.weight, b.weight) + 1), true, 15)) { U.rm.Merge(U.pick1, U.pick2); FinishNode(); return; }
            }
            if (!modal && Button({p.x + 40, p.y + p.height - 90, 150, 40}, "Start over", true, 15)) U.pick1 = U.pick2 = -1;
            if (!modal && Button({p.x + p.width - 210, p.y + p.height - 90, 170, 40}, "Walk on", true, 15)) FinishNode();
        } break;
        case NodeUi::Scrimshaw: {
            Panel(p);
            DrawTextCenteredBold("The Scrimshaw Artist", p.x + p.width / 2, p.y + 16, 32, Pal::Ink);
            DrawTextCentered("Carve a totem from whale bone: a tribe head on a sigil base. That tribe gains the sigil whenever it is played.", p.x + p.width / 2, p.y + 60, 16, Pal::BrassDk);
            Txt("Choose a tribe head", p.x + 130, p.y + 110, 18, Pal::Ink);
            Txt("Choose a sigil base", p.x + 620, p.y + 110, 18, Pal::Ink);
            for (int k = 0; k < (int)U.scrimSuits.size(); k++) {
                Vector2 cc{p.x + 190 + k * 150.0f, p.y + 210};
                bool hov = Dist(m, cc) < 56 && !modal, sel = U.pickSuit == U.scrimSuits[k];
                DrawCircleV({cc.x + 3, cc.y + 4}, 54, Fade(BLACK, 0.3f));
                DrawCircleV(cc, 54, sel ? Pal::Brass : Color{226, 214, 184, 255});
                DrawCircleV(cc, 46, Color{62, 46, 34, 255});
                DrawSuitIcon(U.scrimSuits[k], cc, 60, SUIT_COL[U.scrimSuits[k]]);
                std::string nm = std::string(SuitName(U.scrimSuits[k])) + " tribe";
                Txt(nm, cc.x - MeasureTxt(nm, 15) / 2.0f, cc.y + 64, 15, Pal::Ink);
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) U.pickSuit = U.scrimSuits[k];
            }
            for (int k = 0; k < (int)U.scrimSigils.size(); k++) {
                Vector2 cc{p.x + 680 + k * 140.0f, p.y + 210};
                bool hov = Dist(m, cc) < 56 && !modal, sel = U.pickSigil == U.scrimSigils[k];
                DrawRectangleRounded({cc.x - 50, cc.y - 50, 100, 100}, 0.3f, 8, sel ? Pal::Brass : Color{226, 214, 184, 255});
                DrawRectangleRounded({cc.x - 44, cc.y - 44, 88, 88}, 0.3f, 8, Color{62, 46, 34, 255});
                DrawSigilGlyph((Sigil)U.scrimSigils[k], cc, 56, Color{236, 214, 160, 255});
                std::string nm = InfoOf((Sigil)U.scrimSigils[k]).name;
                Txt(nm, cc.x - MeasureTxt(nm, 14) / 2.0f, cc.y + 58, 14, Pal::Ink);
                if (hov) Tooltip(InfoOf((Sigil)U.scrimSigils[k]).text, {m.x, m.y + 26});
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) U.pickSigil = U.scrimSigils[k];
            }
            if (!gs.totems.empty()) {
                Txt("Your totems:", p.x + 60, p.y + 372, 16, Pal::Ink);
                for (size_t i = 0; i < gs.totems.size(); i++) {
                    Txt(TextFormat("%s tribe: %s", SuitName(gs.totems[i].first), InfoOf((Sigil)gs.totems[i].second).name), p.x + 60, p.y + 396 + i * 22.0f, 15, Pal::BrassDk);
                }
            }
            if (U.pickSuit >= 0 && U.pickSigil >= 0) {
                if (Button({p.x + p.width / 2 - 240, p.y + p.height - 90, 480, 44}, TextFormat("Carve: %s creatures gain %s", SuitName(U.pickSuit), InfoOf((Sigil)U.pickSigil).name), true, 15)) {
                    if (gs.totems.size() >= 3) gs.totems.erase(gs.totems.begin());
                    U.rm.Carve(U.pickSuit, U.pickSigil);
                    FinishNode();
                    return;
                }
            }
            if (!modal && Button({p.x + p.width - 210, p.y + p.height - 90, 170, 40}, "Walk on", true, 15)) FinishNode();
        } break;
        case NodeUi::Cache: {
            Panel({p.x + 250, p.y + 70, 560, 400});
            DrawTextCenteredBold("A cache in the wreck", p.x + p.width / 2, p.y + 96, 32, Pal::Ink);
            DrawBottle(U.foundItem, {p.x + p.width / 2, p.y + 220}, 130);
            DrawTextCenteredBold(ItemName(U.foundItem), p.x + p.width / 2, p.y + 290, 24, Pal::Ink);
            DrawTextCentered(ItemText(U.foundItem), p.x + p.width / 2, p.y + 326, 16, Pal::BrassDk);
            bool room = (int)gs.items.size() < MAX_ITEMS;
            if (room) { if (Button({p.x + p.width / 2 - 110, p.y + 390, 220, 44}, "Take it")) { gs.AddItem(U.foundItem); FinishNode(); return; } }
            else {
                DrawTextCentered("Your pack is full. Click an item to swap it out:", p.x + p.width / 2, p.y + 366, 15, Pal::Bad);
                for (int i = 0; i < (int)gs.items.size(); i++) {
                    Vector2 c{p.x + p.width / 2 - 70 + i * 70.0f, p.y + 410};
                    DrawBottle(gs.items[i], c, 50);
                    if (Dist(m, c) < 28 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { gs.items[i] = U.foundItem; FinishNode(); return; }
                }
                if (Button({p.x + p.width / 2 - 80, p.y + 436, 160, 32}, "Leave it", true, 14)) FinishNode();
            }
        } break;
        case NodeUi::Stall: {
            Rectangle sp{110, 80, 1060, 560};
            DrawRectangleRounded(sp, 0.04f, 8, Color{22, 16, 14, 245});
            DrawRectangleRoundedLinesEx(sp, 0.04f, 8, 3, Pal::BrassDk);
            DrawRectangleRoundedLinesEx({sp.x + 8, sp.y + 8, sp.width - 16, sp.height - 16}, 0.04f, 8, 1, Fade(Pal::Brass, 0.4f));
            DrawTextCenteredBold("The Dealer's Stall", sp.x + sp.width / 2, sp.y + 14, 32, Pal::Brass);
            DrawTextCentered("\"Everything has a price. Yours is the pot.\"", sp.x + sp.width / 2, sp.y + 56, 16, Fade(Pal::Paper, 0.8f));
            {
                float bob = sinf(t * 1.7f) * 4;
                DrawTri({sp.x + 40, sp.y + 96}, {sp.x + 130, sp.y + 96}, {sp.x + 100 + bob, sp.y + 174}, Color{14, 18, 26, 255});
                DrawTri({sp.x + 40, sp.y + 96}, {sp.x + 100 + bob, sp.y + 174}, {sp.x + 60 + bob, sp.y + 186}, Color{20, 26, 36, 255});
                DrawCircleV({sp.x + 106 + bob, sp.y + 182}, 15, Color{28, 30, 36, 255});
                for (int k = 0; k < 4; k++) DrawLineEx({sp.x + 112 + bob, sp.y + 176 + k * 5}, {sp.x + 134 + bob, sp.y + 182 + k * 5}, 4, Color{28, 30, 36, 255});
            }
            const char* KIND_NAME[6] = {"Card", "Charm", "Item", "Trim a card", "Add an edition", "Insurance"};
            const char* KIND_TEXT[6] = {"", "", "", "Discard a card from your deck (min 8).", "Choose a card: it gains the edition.", "If you lose a battle, keep half the pot."};
            int n = (int)U.shop.size();
            float x0 = sp.x + 190, gap = (sp.width - 220) / 6.0f;
            for (int k = 0; k < n; k++) {
                ShopItem& it = U.shop[k];
                Rectangle r{x0 + k * gap, sp.y + 104, 130, 182};
                bool afford = !it.sold && it.price <= gs.pot;
                bool hov = CheckCollisionPointRec(m, r) && U.shopPick < 0 && !modal;
                if (hov && afford) r.y -= 8;
                if (it.kind == SK_CARD) DrawCardFace(r, it.card, true);
                else if (it.kind == SK_CHARM) DrawCharmCard(r, it.charm);
                else {
                    DrawRectangleRounded(r, 0.08f, 6, Color{30, 38, 46, 255});
                    DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, Pal::BrassDk);
                    if (it.kind == SK_ITEM) { DrawBottle(it.item, {r.x + 65, r.y + 84}, 96); }
                    else if (it.kind == SK_EDITION) { Card demo = MakeCard(CardIdByName("Hammerhead"), it.card.edition); DrawCardFace({r.x + 25, r.y + 20, 80, 112}, demo, true); }
                    else if (it.kind == SK_TRIM) { DrawCardFace({r.x + 25, r.y + 20, 80, 112}, MakeCard(CardIdByName("Bilge Rat")), true); DrawLineEx({r.x + 20, r.y + 20}, {r.x + 110, r.y + 132}, 5, Fade(Color{220, 60, 50, 255}, 0.9f)); DrawLineEx({r.x + 110, r.y + 20}, {r.x + 20, r.y + 132}, 5, Fade(Color{220, 60, 50, 255}, 0.9f)); }
                    else { Glow({r.x + 65, r.y + 74}, 60, Fade(Pal::Brass, 0.25f)); DrawTri({r.x + 65, r.y + 26}, {r.x + 22, r.y + 60}, {r.x + 108, r.y + 60}, Pal::Brass); DrawRectangle((int)r.x + 26, (int)r.y + 60, 78, 60, Pal::Brass); DrawRectangle((int)r.x + 58, (int)r.y + 84, 14, 36, Pal::BrassDk); }
                    std::string nm = KIND_NAME[it.kind];
                    Txt(nm, r.x + 65 - MeasureTxt(nm, 14) / 2.0f, r.y + 150, 14, Pal::Paper);
                }
                if (it.sold) {
                    DrawRectangleRounded(r, 0.08f, 6, Fade(BLACK, 0.7f));
                    rlPushMatrix(); rlTranslatef(r.x + 65, r.y + 90, 0); rlRotatef(-18, 0, 0, 1);
                    TxtBold("SOLD", -MeasureTxt("SOLD", 28, true) / 2.0f, -16, 28, Fade(Color{220, 80, 70, 255}, 0.95f));
                    rlPopMatrix();
                } else if (!afford) DrawRectangleRounded(r, 0.08f, 6, Fade(BLACK, 0.55f));
                Rectangle tag{r.x + 25, r.y + r.height + 6, 80, 28};
                DrawLineEx({r.x + 65, r.y + r.height - 2}, {r.x + 65, tag.y + 2}, 1.5f, Fade(Pal::Paper, 0.6f));
                DrawRectangleRounded(tag, 0.35f, 6, it.sold ? Color{60, 56, 50, 255} : afford ? Pal::Brass : Color{120, 90, 60, 255});
                DrawCircleV({tag.x + 9, tag.y + 14}, 3, Color{22, 16, 14, 255});
                TxtBold(TextFormat("%d", it.price), tag.x + 20, tag.y + 4, 18, Color{28, 20, 14, 255});
                if (hov && !it.sold) {
                    if (it.kind == SK_CARD) Hover(it.card);
                    else {
                        std::string d = it.kind == SK_CHARM ? std::string(CharmName(it.charm)) + ": " + CharmText(it.charm)
                                        : it.kind == SK_ITEM ? std::string(ItemName(it.item)) + ": " + ItemText(it.item)
                                        : std::string(KIND_TEXT[it.kind]) + (it.kind == SK_EDITION ? std::string(" ") + EditionName(it.card.edition) + ": " + EditionText(it.card.edition) : "");
                        Tooltip(d, {m.x, m.y + 22});
                    }
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && afford) {
                        if (it.kind == SK_TRIM || it.kind == SK_EDITION) U.shopPick = k;
                        else {
                            gs.pot -= it.price; it.sold = true;
                            if (it.kind == SK_CARD) gs.deck.push_back(it.card);
                            else if (it.kind == SK_CHARM) gs.charms |= 1u << it.charm;
                            else if (it.kind == SK_ITEM) { if (!gs.AddItem(it.item)) { gs.pot += it.price; it.sold = false; Toast("Your pack is full."); } }
                            else if (it.kind == SK_INSURE) gs.insured = true;
                        }
                    }
                }
            }
            Txt(TextFormat("Pot: %d gold%s", gs.pot, gs.insured ? "   (insured)" : ""), sp.x + 34, sp.y + 214, 18, Pal::Brass);
            if (!modal && U.shopPick < 0 && Button({sp.x + 190, sp.y + sp.height - 130, 200, 40}, TextFormat("Reroll goods (%d)", RerollPrice()), RerollPrice() <= gs.pot, 15)) {
                gs.pot -= RerollPrice(); U.rerolls++;
                std::vector<ShopItem> keep;
                for (auto& it : U.shop) if (it.sold && it.kind != SK_CARD && it.kind != SK_CHARM && it.kind != SK_ITEM) keep.push_back(it);
                StockShop();
                for (auto& it : U.shop) for (auto& k : keep) if (k.kind == it.kind) it.sold = true;
            }
            if (!modal && U.shopPick < 0 && Button({sp.x + sp.width - 300, sp.y + sp.height - 66, 260, 48}, "Leave the stall")) FinishNode();
            if (U.shopPick >= 0) {
                Rectangle pp{150, 70, 980, 580};
                DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.5f));
                Panel(pp);
                ShopItem& it = U.shop[U.shopPick];
                DrawTextCenteredBold(it.kind == SK_TRIM ? "Choose a card to discard" : "Choose a card to dress", pp.x + pp.width / 2, pp.y + 14, 30, Pal::Ink);
                int clicked = DeckGrid({pp.x + 30, pp.y + 64, pp.width - 60, pp.height - 130}, gs.deck, {}, m,
                                       [&](int i) { return it.kind == SK_TRIM ? gs.deck.size() > 8 : gs.deck[i].edition == ED_NONE; });
                if (clicked >= 0) {
                    if (it.kind == SK_TRIM) gs.deck.erase(gs.deck.begin() + clicked); else gs.deck[clicked].edition = it.card.edition;
                    gs.pot -= it.price; it.sold = true; U.shopPick = -1;
                }
                if (Button({pp.x + pp.width / 2 - 90, pp.y + pp.height - 54, 180, 40}, "Cancel")) U.shopPick = -1;
            }
        } break;
        default: break;
    }
}

// ---------------------------------------------------------------- the room, the panels and the scene
void DrawRoom(float t) {
    ClearBackground(Color{4, 5, 8, 255});
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    for (int k = 0; k < 6; k++) { // hanging chains in the dark around the dealer
        float x = 80 + k * 220 + sinf(t * 0.3f + k) * 4;
        DrawLineEx({x, 0}, {x + 20, 200.0f + (k % 3) * 40}, 3, Color{18, 22, 30, 255});
        for (int j = 0; j < 4; j++) DrawCircleLines((int)(x + 20 * (j / 4.0f)), 50 + j * 40, 5, Color{24, 30, 40, 255});
    }
    float bob = 0, lean = 0;
    if (U.moodT > 0) {
        float f = std::min(1.0f, U.moodT);
        if (U.mood > 0) bob = -6 * fabsf(sinf(t * 9)) * f; else lean = sinf(t * 42) * 3 * f;
    } else if (U.ph == Ph::Battle && U.bat.turn >= Turn::FOE_START && U.bat.turn <= Turn::FOE_END) bob = sinf(t * 3) * 1.5f;
    rlPushMatrix();
    rlTranslatef(lean, bob, 0);
    DrawDealer(t);
    rlPopMatrix();
    DrawTable();
    DrawSkull(t, 300, 372);
    DrawScale(U.beam, U.ph == Ph::Battle || U.ph == Ph::Won ? U.bat.board.scale : 0);
    DrawChips({150, 560}, U.rm.gs.pot, t);
    for (int k = 0; k < 2; k++) { // bottles on the right
        float x = 1130 + k * 44, y = 352 + k * 16;
        DrawRectangleRounded({x - 14, y, 28, 56}, 0.3f, 6, Color{58, 78, 52, 255});
        DrawRectangle((int)x - 5, (int)y - 14, 10, 16, Color{50, 66, 46, 255});
        DrawRectangle((int)x - 6, (int)y - 18, 12, 6, Color{150, 108, 60, 255});
        DrawRectangle((int)x - 10, (int)y + 16, 20, 22, Color{220, 190, 130, 255});
    }
}

void DrawHud(Game& g, Vector2 m, bool modal) {
    (void)m; (void)modal;
    if (U.ph != Ph::Menu) {
        DrawRectangleRounded({12, 12, 300, 92}, 0.12f, 6, Color{8, 12, 16, 200});
        DrawRectangleRoundedLinesEx({12, 12, 300, 92}, 0.12f, 6, 1.5f, Pal::BrassDk);
        TxtBold("FLATS", 26, 18, 24, Pal::Brass);
        if (U.ph == Ph::Battle) {
            Txt(Dealer(U.bat.dealer).name, 116, 24, 16, Pal::Paper);
            Txt(TextFormat("Turn %d   Deck %d   Hand %d", U.bat.turnNo, (int)U.bat.deck.size(), (int)U.bat.hand.size()), 26, 54, 15, Pal::Paper);
            Txt(TextFormat("Battles won %d", U.rm.gs.battlesWon), 26, 76, 14, Fade(Pal::Paper, 0.75f));
        } else {
            Txt(TextFormat("Table %d of %d", std::max(0, U.rm.layer + 1), MAP_LAYERS), 116, 24, 16, Pal::Paper);
            Txt(TextFormat("Battles won %d", U.rm.gs.battlesWon), 26, 54, 15, Pal::Paper);
            Txt(TextFormat("Deck of %d", (int)U.rm.gs.deck.size()), 26, 76, 14, Fade(Pal::Paper, 0.75f));
        }
    }
    DrawRectangleRounded({SCREEN_W - 250.0f, 12, 238, 40}, 0.3f, 6, Color{8, 12, 16, 210});
    DrawRectangleRoundedLinesEx({SCREEN_W - 250.0f, 12, 238, 40}, 0.3f, 6, 1.5f, Pal::BrassDk);
    DrawCircle(SCREEN_W - 228, 32, 9, Pal::Brass);
    TxtBold(TextFormat("%d", g.gold), SCREEN_W - 210, 20, 20, Pal::Brass);
    Txt(TextFormat("Pot %d", U.rm.gs.pot), SCREEN_W - 120, 23, 17, Pal::Paper);
}

// ---------------------------------------------------------------- the showcase: every card and component, one page each (for the shots folder)
void DrawShowcase(float t) {
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{20, 40, 42, 255}, Color{8, 16, 20, 255});
    for (int k = 0; k < 40; k++) DrawCircleV({fmodf(k * 173.0f, 1280.0f), fmodf(k * 97.0f, 720.0f)}, 60 + (k % 5) * 20, Fade(Color{30, 70, 66, 255}, 0.05f));
    const auto& cat = Catalog();
    int pg = U.showPage;
    auto title = [&](const char* s, const char* sub) {
        TxtBold(s, 40, 18, 32, Pal::Brass);
        Txt(sub, 42, 58, 15, Fade(Pal::Paper, 0.8f));
        DrawLineEx({40, 82}, {1240, 82}, 2, Pal::BrassDk);
    };
    if (pg >= 100) { // one card, large, with everything spelled out
        const Card& c = cat[std::clamp(pg - 100, 0, (int)cat.size() - 1)];
        DrawCardFace({90, 60, 340, 476}, c, true);
        float x = 480, y = 70;
        TxtBold(c.name, x, y, 40, Pal::Brass); y += 56;
        Txt(TextFormat("Strength %d      Defense %d      Weight %d", c.strength, c.defense, c.weight), x, y, 22, Pal::Paper); y += 34;
        Txt(TextFormat("Cost: %s      Tribe: %s      %s", c.CostText().c_str(), SuitName(c.suit), c.tier == 0 ? "Never offered as a reward" : c.tier == 1 ? "Common" : c.tier == 2 ? "Uncommon" : "Rare"), x, y, 18, Pal::Paper); y += 40;
        for (Sigil s : c.sigils) {
            DrawCircleV({x + 20, y + 20}, 22, Fade(Color{38, 26, 20, 255}, 0.9f));
            DrawSigilGlyph(s, {x + 20, y + 20}, 30, Color{236, 214, 160, 255});
            TxtBold(InfoOf(s).name, x + 58, y, 20, Pal::Brass);
            DrawWrapped(InfoOf(s).text, {x + 58, y + 26, 640, 44}, 16, Pal::Paper);
            y += 62;
        }
        if (c.evolveId >= 0) { Txt(TextFormat("Grows into: %s", cat[c.evolveId].name.c_str()), x, y + 6, 18, Color{160, 240, 200, 255}); DrawCardFace({x + 300, y - 10, 90, 126}, cat[c.evolveId], true); }
        return;
    }
    if (pg >= 0 && pg <= 4) {
        struct Sheet { const char* title; const char* sub; };
        static const Sheet S[5] = {{"Flats: common creatures", "Tier 1: the cheap cards you start with and find early."}, {"Flats: uncommon creatures", "Tier 2: found deeper in the run."},
                                   {"Flats: rare creatures", "Tier 3: the heavy hitters and engines."}, {"Flats: the dealers' cards and the Kraken", "Cards you can meet but never take: the dealers' decks and what a Kraken Spawn becomes."},
                                   {"Flats: the drowned court of Atlantis", "The Atlantean Sovereign's cards: Phase 1, the Drowned Phalanx, and Phase 2, Selenis the Moon God."}};
        title(S[pg].title, S[pg].sub);
        std::vector<int> ids;
        for (const Card& c : cat) {
            bool boss = c.name == "Atlantean Hoplite" || c.name == "Sunken Oracle" || c.name == "Coral Golem" || c.name.rfind("Selenis", 0) == 0;
            bool sel = pg == 0 ? (c.tier == 1 && c.name != "Minnow") : pg == 1 ? c.tier == 2 : pg == 2 ? c.tier == 3 : pg == 3 ? ((c.tier == 0 && !boss) || c.name == "Kraken") : boss;
            if (pg == 3 && c.name == "Kraken") sel = true;
            if (pg == 3 && c.name == "Minnow") sel = true;
            if (sel) ids.push_back(c.id);
        }
        if (pg == 4) {
            for (int i = 0; i < (int)ids.size() - 1; i++) { float x = 60.0f + i * 220; DrawCardFace({x, 130, 200, 280}, cat[ids[i]], true); }
            Rectangle w{60, 440, 1160, 180};
            DrawSelenis(w, cat[ids.back()], t, 0);
            Txt("Phase 1: Hoplites cover each other, the Oracle heals them and slips your blows, the Golem holds off Airborne creatures.   Phase 2: Selenis fills every lane, and each turn's end his tide drags your creatures sideways.", 60, 640, 14, Fade(Pal::Paper, 0.85f));
            return;
        }
        int cw = 138, ch = 194, per = 8;
        for (int i = 0; i < (int)ids.size(); i++) DrawCardFace({44.0f + (i % per) * (cw + 12), 100.0f + (i / per) * (ch + 22), (float)cw, (float)ch}, cat[ids[i]], true);
        return;
    }
    if (pg == 5) { // the sigils
        title("Flats: sigils", "Persistent abilities. Each one hooks into a single moment of the turn.");
        for (int s = 1; s < (int)Sigil::COUNT; s++) {
            int col = (s - 1) / 13, row = (s - 1) % 13;
            float x = 40 + col * 610, y = 100 + row * 46;
            DrawCircleV({x + 20, y + 20}, 20, Fade(Color{38, 26, 20, 255}, 0.95f));
            DrawSigilGlyph((Sigil)s, {x + 20, y + 20}, 28, Color{236, 214, 160, 255});
            TxtBold(InfoOf((Sigil)s).name, x + 52, y, 17, Pal::Brass);
            DrawWrapped(InfoOf((Sigil)s).text, {x + 190, y, 400, 44}, 13, Pal::Paper);
        }
        return;
    }
    if (pg == 6) { // charms, bottles, editions, tribes
        title("Flats: charms, bottles, editions and tribes", "Everything you carry, and how cards are dressed.");
        TxtBold("Charms (up to three)", 40, 100, 20, Pal::Brass);
        for (int c = 0; c < CH_COUNT; c++) { DrawCharmIcon(c, {70.0f, 150.0f + c * 46}, 40, t); TxtBold(CharmName(c), 110, 136 + c * 46, 16, Pal::Paper); Txt(CharmText(c), 300, 138 + c * 46, 14, Fade(Pal::Paper, 0.85f)); }
        TxtBold("Bottles (pack items, up to three)", 40, 476, 20, Pal::Brass);
        for (int k = 0; k < (int)PackItem::COUNT; k++) { DrawBottle(k, {70.0f + (k % 2) * 400, 528.0f + (k / 2) * 62}, 46); TxtBold(ItemName(k), 104 + (k % 2) * 400, 510 + (k / 2) * 62, 15, Pal::Paper); DrawWrapped(ItemText(k), {104.0f + (k % 2) * 400, 530.0f + (k / 2) * 62, 290, 40}, 12, Fade(Pal::Paper, 0.85f)); }
        TxtBold("Editions", 900, 100, 20, Pal::Brass);
        for (int e = 1; e < ED_COUNT; e++) { Card d = MakeCard(CardIdByName("Hammerhead"), e); DrawCardFace({900.0f + (e - 1) * 116, 136, 104, 146}, d, true); TxtBold(EditionName(e), 900.0f + (e - 1) * 116, 288, 15, Pal::Paper); DrawWrapped(EditionText(e), {900.0f + (e - 1) * 116, 308, 108, 60}, 11, Fade(Pal::Paper, 0.85f)); }
        TxtBold("Tribes (suits)", 900, 380, 20, Pal::Brass);
        for (int s = 0; s < SUITS; s++) { Vector2 p{930.0f + s * 84, 448}; DrawCircleV(p, 30, Fade(SUIT_COL[s], 0.3f)); DrawSuitIcon(s, p, 40, SUIT_COL[s]); Txt(SuitName(s), p.x - MeasureTxt(SuitName(s), 14) / 2.0f, p.y + 36, 14, Pal::Paper); }
        TxtBold("Costs", 900, 520, 20, Pal::Brass);
        DrawDrop({930, 570}, 40, BLOOD_COL); Txt("Blood: sacrifice your creatures", 956, 560, 14, Pal::Paper);
        DrawBone({930, 606}, 36, BONE_COL); Txt("Bones: earned from the dead", 956, 596, 14, Pal::Paper);
        return;
    }
    if (pg == 7) { // map nodes
        title("Flats: the sunken map", "Eight layers of choices, then the Atlantean Sovereign. Every node is an event.");
        for (int k = 0; k < (int)NodeType::COUNT; k++) {
            int col = k % 2, row = k / 2;
            Vector2 p{80.0f + col * 610, 140.0f + row * 78};
            DrawCircleV({p.x + 2, p.y + 3}, 30, Fade(BLACK, 0.3f)); DrawCircleV(p, 30, Color{46, 32, 22, 255});
            DrawCircleV(p, 27, ColorBrightness(NodeCol((NodeType)k), -0.45f));
            DrawNodeIcon((NodeType)k, p, 18, ColorBrightness(NodeCol((NodeType)k), 0.25f));
            TxtBold(NodeName((NodeType)k), p.x + 44, p.y - 26, 18, Pal::Brass);
            DrawWrapped(NodeText((NodeType)k), {p.x + 44, p.y - 4, 520, 44}, 13, Fade(Pal::Paper, 0.9f));
        }
        return;
    }
    if (pg == 8) { // the dealers
        title("Flats: the dealers", "A dealer for each stretch of the map; each has a trick.");
        for (int d = 0; d < DEALERS; d++) {
            const DealerInfo& di = Dealer(d);
            float y = 110 + d * 108;
            TxtBold(di.name, 60, y, 24, Pal::Brass);
            Txt(di.line, 60, y + 32, 16, Fade(Pal::Paper, 0.85f));
            Txt(TextFormat("Trick: %s   (plays %d, draws %d a turn)", di.twist, di.plays, di.draws), 60, y + 58, 15, Pal::Paper);
        }
        return;
    }
}

}  // namespace

// ============================================================================
void SceneCards(Game& g) {
    G = &g;
    if (!U.inited) ResetUi();
    float dt = GetFrameTime(), t = g.time;
    SetPost(0.75f, 0.03f, 0.5f);
    Vector2 m = GetMousePosition();
    gHasHover = false;
    UpdateFx(dt);
    if (U.ph == Ph::Showcase) { SetPost(0.15f, 0.0f, 0.1f); DrawShowcase(t); return; }   // a clean page, without the table or the HUD
    bool modal = U.showRules || U.showDeck;
    Vector2 sh{sinf(t * 91) * U.shake * 9, cosf(t * 77) * U.shake * 9};

    rlPushMatrix();
    rlTranslatef(sh.x, sh.y, 0);
    DrawRoom(t);
    rlPopMatrix();

    float fl = 1 + 0.06f * sinf(t * 13) + 0.04f * sinf(t * 29 + 1);
    LightsBegin(Color{22, 24, 32, 255}); // the room is in shadow; the dealer is lit on purpose below
    AddLight({300, 340}, 380 * fl, Color{255, 190, 110, 255}, 0.95f);
    AddLight({640, 470}, 620, Color{80, 150, 200, 255}, 0.6f);
    AddLight({640, 170}, 330, Color{150, 165, 205, 255}, 1.0f);
    AddLight({640, 330}, 260, Color{70, 150, 190, 255}, 0.8f);
    AddLight({124, 350}, 260 * fl, Color{255, 200, 120, 255}, 0.4f);
    AddLight({1090, 460}, 200, Color{255, 190, 110, 255}, 0.45f);
    if (U.rm.gs.pot > 0) AddLight({170, 550}, 150, Color{255, 200, 100, 255}, 0.3f);
    LightsEnd();
    InkPass(0.55f, 0.7f);

    rlPushMatrix();
    rlTranslatef(sh.x * 0.6f, sh.y * 0.6f, 0);
    Rectangle centre{300, 110, 680, 400};
    switch (U.ph) {
        case Ph::Menu: {
            Panel(centre);
            DrawTextCenteredBold("FLATS", centre.x + centre.width / 2, centre.y + 16, 44, Pal::Ink);
            DrawTextCentered("\"Sit. Cards don't bite. Much.\"", centre.x + centre.width / 2, centre.y + 70, 18, Pal::BrassDk);
            DrawWrapped("A duel of creatures on a four-lane board. Every card has strength, defense and weight; pay in blood (sacrifice your own creatures) "
                        "or bones (earned from the dead); and read the sigils. Tip the scales 8 points your way to win a battle.\n\n"
                        "Between battles you walk a map: card picks, campfires, splices, trials, a stall to spend your winnings, and four dealers, each with a trick of "
                        "their own. Win fast and you bank Momentum. Cash out after any battle, or press on and risk the pot.\n\nThe HOW TO PLAY tab on the right has the full rules.",
                        {centre.x + 34, centre.y + 104, centre.width - 68, 230}, 15, Pal::Ink);
            if (Button({centre.x + centre.width / 2 - 140, centre.y + centre.height - 62, 280, 48}, "Take a seat")) { U.rm.NewRun((unsigned)GetRandomValue(1, 1 << 30)); U.ph = Ph::Map; }
            if (BackButton(g)) U.inited = false;
        } break;

        case Ph::Map: DrawMap(g, t, m, modal); break;

        case Ph::Node: DrawNodePanel(g, t, m, modal); break;

        case Ph::Boon: {
            Panel(centre);
            const MapNode& n = U.rm.Current();
            DrawTextCenteredBold(TextFormat("%s%s", n.type == NodeType::ELITE ? "Elite: " : "", Dealer(n.dealer).name), centre.x + centre.width / 2, centre.y + 20, 36, Pal::Ink);
            DrawTextCentered(Dealer(n.dealer).line, centre.x + centre.width / 2, centre.y + 70, 18, Pal::BrassDk);
            DrawTextCentered(Dealer(n.dealer).twist, centre.x + centre.width / 2, centre.y + 100, 16, Pal::Ink);
            DrawTextCenteredBold(TextFormat("You hold %d momentum. Spend one for a head start?", U.rm.gs.momentumTracker), centre.x + centre.width / 2, centre.y + 150, 20, Pal::Ink);
            struct B { const char* label; Boon b; } bs[3] = {{"Two extra cards", Boon::EXTRA_DRAW}, {"Three bones", Boon::BONES}, {"The scales start +1", Boon::HEAD_START}};
            for (int i = 0; i < 3; i++)
                if (Button({centre.x + 34 + i * 214.0f, centre.y + 200, 200, 52}, bs[i].label, true, 15)) { U.rm.SpendMomentum(); StartBattle(bs[i].b); }
            if (Button({centre.x + centre.width / 2 - 120, centre.y + 300, 240, 46}, "Keep it for later")) StartBattle(Boon::NONE);
        } break;

        case Ph::Battle: DrawBattle(g, dt, t, m, modal); break;

        case Ph::Showcase: DrawShowcase(t); break;

        case Ph::Won: {
            DrawBattle(g, 0, t, m, true);
            Panel(centre);
            DrawTextCenteredBold(TextFormat("You beat %s!", Dealer(U.bat.dealer).name), centre.x + centre.width / 2, centre.y + 20, 36, Pal::Good);
            DrawTextCentered(TextFormat("In %d turn%s. +%d gold: the pot stands at %d.", U.lastTurns, U.lastTurns == 1 ? "" : "s", U.gainedGold, U.rm.gs.pot), centre.x + centre.width / 2, centre.y + 80, 20, Pal::Ink);
            if (U.gainedMomentum) DrawTextCenteredBold("Fast work: +1 momentum.", centre.x + centre.width / 2, centre.y + 112, 20, Color{60, 120, 170, 255});
            if (U.isBoss) {
                DrawTextCentered("The Sovereign's court is drowned, and the Moon God is dark. You've cleaned out the deep table.", centre.x + centre.width / 2, centre.y + 160, 17, Pal::BrassDk);
                if (Button({centre.x + centre.width / 2 - 150, centre.y + 250, 300, 52}, TextFormat("Collect %d gold", U.rm.gs.pot + 100))) {
                    U.rm.gs.pot += 100; CashOut();
                }
            } else {
                DrawWrapped("Cash out and walk away with it, or press on: another card, a sharper dealer, a bigger pot. Lose the next battle and all of it stays on the table.",
                            {centre.x + 50, centre.y + 150, centre.width - 100, 80}, 17, Pal::Ink);
                if (Button({centre.x + 50, centre.y + 250, 280, 52}, TextFormat("Cash out %d gold", U.rm.gs.pot))) CashOut();
                if (Button({centre.x + 350, centre.y + 250, 280, 52}, U.isElite ? "Press on (a rare find)" : "Press on")) {
                    if (U.isElite) {
                        U.offers = U.rm.OfferCards(3, true); U.offerCharm = -1; U.rareOffer = true;
                        if (U.rm.gs.Charms() < 3) { std::vector<int> free; for (int i = 0; i < CH_COUNT; i++) if (!U.rm.gs.HasCharm(i)) free.push_back(i); if (!free.empty()) U.offerCharm = free[U.rng.I(0, (int)free.size() - 1)]; }
                        U.nu = NodeUi::CardPick; U.ph = Ph::Node;
                    } else FinishNode();
                }
            }
        } break;

        case Ph::RunOver: {
            Panel(centre);
            if (U.cashed) {
                DrawTextCenteredBold(U.isBoss ? "You cleared the deep table!" : "You leave the table", centre.x + centre.width / 2, centre.y + 40, 40, Pal::Good);
                DrawTextCentered(TextFormat("%d gold richer.", U.payout), centre.x + centre.width / 2, centre.y + 110, 26, Pal::Ink);
            } else {
                if (U.payout > 0 && !U.insurePaid) { g.gold += U.payout; U.insurePaid = true; }
                DrawTextCenteredBold("The dealer sweeps the pot", centre.x + centre.width / 2, centre.y + 40, 38, Pal::Bad);
                DrawTextCentered(U.payout > 0 ? TextFormat("Your insurance pays %d of the %d gold.", U.payout, U.rm.gs.pot)
                                 : U.rm.gs.pot > 0 ? TextFormat("%d gold, gone.", U.rm.gs.pot) : "You hadn't won anything yet.", centre.x + centre.width / 2, centre.y + 110, 24, Pal::Ink);
            }
            DrawTextCentered("\"Come back when you've more to lose.\"", centre.x + centre.width / 2, centre.y + 170, 18, Pal::BrassDk);
            if (Button({centre.x + centre.width / 2 - 260, centre.y + 260, 240, 50}, "Deal again")) ResetUi();
            if (Button({centre.x + centre.width / 2 + 20, centre.y + 260, 240, 50}, "Leave the table")) LeaveTable(g);
        } break;
    }
    // effects over the whole table
    for (auto& f : U.floats) {
        float a = 1 - f.t / 1.3f;
        int fs = (int)f.size;
        TxtBold(f.text, f.pos.x - MeasureTxt(f.text, fs, true) / 2.0f + 1, f.pos.y + 1, fs, Fade(BLACK, a * 0.8f));
        TxtBold(f.text, f.pos.x - MeasureTxt(f.text, fs, true) / 2.0f, f.pos.y, fs, Fade(f.col, std::min(1.0f, a * 1.6f)));
    }
    DrawParticles();
    rlPopMatrix();

    // leaving mid-run forfeits whatever is unbanked
    bool inRun = U.ph == Ph::Map || U.ph == Ph::Node || U.ph == Ph::Boon || U.ph == Ph::Battle;
    if (!modal && inRun && Button({20, 122, 150, 34}, U.rm.gs.pot > 0 ? "Fold (lose pot)" : "Fold and leave", true, 14)) LeaveTable(g);
    if (!modal && (U.ph == Ph::Battle || U.ph == Ph::Boon) && Button({20, 164, 150, 30}, TextFormat("Deck (%d)", (int)U.rm.gs.deck.size()), true, 14)) U.showDeck = true;   // read every card before the fight, too

    DrawHud(g, m, modal);
    if (gHasHover && !modal) DrawInspector(gHoverCard, gHoverHp, gHoverStr);
    if (U.toastT > 0) {
        float w = (float)MeasureTxt(U.toast, 17, true);
        Rectangle r{SCREEN_W / 2.0f - w / 2 - 16, 74, w + 32, 34};
        DrawRectangleRounded(r, 0.4f, 6, Fade(Color{40, 12, 12, 240}, std::min(1.0f, U.toastT * 3)));
        DrawRectangleRoundedLinesEx(r, 0.4f, 6, 1.5f, Fade(Pal::Bad, std::min(1.0f, U.toastT * 3)));
        TxtBold(U.toast, r.x + 16, r.y + 7, 17, Fade(Pal::Paper, std::min(1.0f, U.toastT * 3)));
    }

    // ---- the deck viewer
    if (U.showDeck) {
        Rectangle p{170, 70, 940, 580};
        Panel(p);
        const std::vector<Card>& d = U.ph == Ph::Battle ? U.rm.gs.deck : U.rm.gs.deck;
        DrawTextCenteredBold(TextFormat("Your deck: %d cards", (int)d.size()), p.x + p.width / 2, p.y + 14, 30, Pal::Ink);
        DeckGrid({p.x + 30, p.y + 64, p.width - 60, p.height - 130}, d, {}, m);
        if (gHasHover) DrawInspector(gHoverCard, gHoverHp, gHoverStr);
        if (Button({p.x + p.width / 2 - 90, p.y + p.height - 54, 180, 40}, "Close")) U.showDeck = false;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, RulesTabRect())) U.showRules = !U.showRules;
    DrawRulesFolder(m);
}

// ---------------------------------------------------------------- the sprite sheet page
void FlatsSpritePage(float t) {
    gHasHover = false;
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    TxtBold("Flats: creatures, sigils, charms, bottles, map nodes", 30, 12, 22, Pal::Brass);
    const auto& cat = Catalog();
    for (int i = 0; i < (int)cat.size(); i++) {
        Card c = cat[i];
        if (i % 7 == 3) c.edition = ED_FOIL; else if (i % 7 == 5) c.edition = ED_GILT; else if (i % 11 == 4) c.edition = ED_HEX;
        DrawCardFace({20.0f + (i % 13) * 96, 44.0f + (i / 13) * 132, 88, 122}, c, true);
    }
    for (int k = 0; k < (int)PackItem::COUNT; k++) DrawBottle(k, {60.0f + k * 70, 480}, 54);
    for (int c = 0; c < CH_COUNT; c++) DrawCharmIcon(c, {540.0f + c * 60, 480}, 46, t);
    Txt("Bottles (items) and charms", 40, 516, 14, Pal::Paper);
    for (int k = 0; k < (int)NodeType::COUNT; k++) {
        Vector2 p{60.0f + k * 80, 580};
        DrawCircleV(p, 28, Color{46, 32, 22, 255});
        DrawNodeIcon((NodeType)k, p, 17, NodeCol((NodeType)k));
    }
    Txt("Map nodes: battle, elite, card, campfire, splice, sacrifice, trial, stall, cache, boss", 40, 618, 14, Pal::Paper);
    for (int s = 1; s < (int)Sigil::COUNT; s++) {
        Vector2 p{50.0f + (s - 1) * 66, 672};
        DrawCircleV(p, 22, Fade(Color{38, 26, 20, 255}, 0.9f));
        DrawSigilGlyph((Sigil)s, p, 30, Color{236, 214, 160, 255});
        std::string nm = InfoOf((Sigil)s).name;
        Txt(nm, p.x - MeasureTxt(nm, 9) / 2.0f, p.y + 24, 9, Fade(Pal::Paper, 0.85f));
    }
    DrawBell({1180, 560}, true, 0);
    DrawSkull(t, 1180, 650);
}

// ---------------------------------------------------------------- screenshots and tests
static void DebugRun(unsigned seed) {
    ResetUi();
    U.rm.NewRun(seed);
    U.rm.gs.pot = 140;
    U.rm.gs.charms = (1u << CH_PEARL) | (1u << CH_ANCHOR);
    U.rm.gs.items = {(int)PackItem::HARPOON, (int)PackItem::BANDAGE};
    U.rm.gs.momentumTracker = 2;
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Manta Ray"), ED_FOIL));
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Kraken Spawn"), ED_GILT));
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Stingray"), ED_HEX));
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Sperm Whale")));
}
void DebugFlatsDeal() {
    DebugRun(7);
    U.rm.layer = 4; U.rm.slot = 0;
    U.rm.map[4][0].type = NodeType::ELITE; U.rm.map[4][0].dealer = 2;
    StartBattle(Boon::NONE);
    Battle& b = U.bat;
    Events ev;
    b.Draw(true, ev, U.rng);
    auto put = [&](int r, int c, const char* n, int hpLoss = 0, int ed = ED_NONE) { Card k = MakeCard(CardIdByName(n), ed); k.hp -= hpLoss; b.board.cell[r][c].used = true; b.board.cell[r][c].card = k; b.board.cell[r][c].card.age = 2; };
    put(R_YOU_FRONT, 0, "Manta Ray", 0, ED_FOIL); put(R_YOU_FRONT, 2, "Crab Sentinel", 1); put(R_YOU_FRONT, 3, "Minnow"); put(R_YOU_BACK, 1, "Ship's Cat");
    put(R_FOE_FRONT, 0, "Stingray"); put(R_FOE_FRONT, 1, "Rusted Anchor"); put(R_FOE_FRONT, 3, "Hammerhead", 1);
    put(R_FOE_QUEUE, 2, "Ghost Crab"); put(R_FOE_QUEUE, 3, "Deckhand");
    b.board.bones[0] = 4; b.board.scale = 2;
    b.hand.clear();
    for (const char* n : {"Hammerhead", "Coral Queen", "Ballast Cask", "Moray Eel", "Minnow"}) b.hand.push_back(MakeCard(CardIdByName(n)));
    b.hand[1].edition = ED_GILT;
    b.turn = Turn::YOU_MAIN;
    U.selHand = 0;
    U.beam = -0.34f * 2.0f / SCALE_LIMIT;
    U.fx[R_YOU_FRONT][0].flash = 0;
}
void DebugFlatsCombat() {
    DebugFlatsDeal();
    U.selHand = -1;
    U.bat.turn = Turn::YOU_COMBAT; U.bat.col = 0; U.stepT = 0;
}
void DebugFlatsWon() {
    DebugFlatsDeal();
    U.bat.board.scale = SCALE_LIMIT; U.bat.turn = Turn::OVER; U.bat.winner = 1; U.bat.turnNo = 4;
    HandleBattleEnd();
}
void DebugFlatsBoon() {
    DebugRun(23);
    U.rm.layer = 1; U.rm.slot = 0;
    U.rm.map[1][0].type = NodeType::BATTLE; U.rm.map[1][0].dealer = 1;
    U.ph = Ph::Boon;
}
void DebugFlatsMap() {
    DebugRun(11);
    U.rm.layer = 2; U.rm.slot = 0;
    for (int l = 0; l <= 2; l++) for (auto& n : U.rm.map[l]) n.visited = true;
    U.rm.map[2][0].visited = true;
    U.rm.gs.battlesWon = 2;
    U.ph = Ph::Map;
}
void DebugFlatsShop() {
    DebugRun(13);
    U.rm.layer = 3; U.rm.slot = 0;
    StockShop();
    U.shop[1].sold = true;
    U.nu = NodeUi::Stall; U.ph = Ph::Node;
}
void DebugFlatsDeck() { DebugFlatsMap(); U.showDeck = true; }
void DebugFlatsReward() {
    DebugRun(17);
    U.rm.layer = 4; U.rm.slot = 0;
    U.offers = U.rm.OfferCards(3, true);
    U.offerCharm = CH_COMPASS; U.rareOffer = true;
    U.nu = NodeUi::CardPick; U.ph = Ph::Node;
}
void DebugFlatsCampfire() {
    DebugRun(19);
    U.rm.layer = 3; U.rm.slot = 0;
    U.nu = NodeUi::Campfire; U.ph = Ph::Node; U.pick1 = 3;
}
static void DebugNode(unsigned seed, NodeType t) {
    DebugRun(seed);
    U.rm.layer = 3; U.rm.slot = 0;
    U.rm.map[3][0].type = t;
    EnterNode();
}
void DebugFlatsVents() { DebugNode(31, NodeType::VENTS); U.pick1 = 3; U.ventStep = 2; }
void DebugFlatsSplicers() {
    DebugRun(37);
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Hermit Crab")));
    U.rm.gs.deck.push_back(MakeCard(CardIdByName("Pufferfish")));
    U.rm.layer = 3; U.rm.slot = 0; U.rm.map[3][0].type = NodeType::SPLICERS;
    EnterNode();
    U.pick1 = 0; U.pick2 = (int)U.rm.gs.deck.size() - 2;
}
void DebugFlatsScrimshaw() {
    DebugNode(41, NodeType::SCRIMSHAW);
    U.rm.gs.totems.push_back({BLADE, (int)Sigil::SKIMMER});
    U.pickSuit = U.scrimSuits[0]; U.pickSigil = U.scrimSigils[1];
}
void DebugFlatsBarnacle() { DebugNode(43, NodeType::SPLICE); U.pick1 = 4; U.pick2 = 0; }
void DebugFlatsMaelstrom() { DebugNode(47, NodeType::SACRIFICE); U.pick1 = 5; }
void DebugFlatsBoss(int phase) {
    DebugRun(29);
    U.rm.layer = 8; U.rm.slot = 0;
    U.rm.map[8][0].type = NodeType::BOSS; U.rm.map[8][0].dealer = 4;
    StartBattle(Boon::NONE);
    Battle& b = U.bat;
    Events ev;
    auto put = [&](int r, int c, const char* n, int hpLoss = 0) { Card k = MakeCard(CardIdByName(n)); k.hp -= hpLoss; b.board.cell[r][c].used = true; b.board.cell[r][c].card = k; b.board.cell[r][c].card.age = 2; };
    put(R_YOU_FRONT, 0, "Flying Fish"); put(R_YOU_FRONT, 1, "Pufferfish"); put(R_YOU_FRONT, 2, "Anglerfish"); put(R_YOU_BACK, 1, "Ship's Cat"); put(R_YOU_FRONT, 3, "Hermit Crab", 1);
    b.hand.clear();
    for (const char* n : {"Kraken Spawn", "Sperm Whale", "Sailfish", "Sea Urchin", "Minnow"}) b.hand.push_back(MakeCard(CardIdByName(n)));
    b.board.bones[0] = 5;
    if (phase == 1) {
        b.board.cell[R_FOE_FRONT][0] = Cell(); b.board.cell[R_FOE_QUEUE][1] = Cell();
        put(R_FOE_FRONT, 0, "Atlantean Hoplite"); put(R_FOE_FRONT, 1, "Atlantean Hoplite", 1); put(R_FOE_FRONT, 2, "Sunken Oracle"); put(R_FOE_FRONT, 3, "Atlantean Hoplite");
        put(R_FOE_QUEUE, 1, "Coral Golem"); put(R_FOE_QUEUE, 3, "Atlantean Hoplite");
        b.board.scale = 3;
        U.beam = -0.34f * 3.0f / SCALE_LIMIT;
    } else {
        for (int r : {R_FOE_QUEUE, R_FOE_FRONT}) for (int c = 0; c < COLS; c++) b.board.cell[r][c] = Cell();
        b.RiseSelenis(ev);
        b.board.cell[R_FOE_FRONT][0].card.hp = 31;
        b.board.scale = 2;
        b.phase = 2;
        U.beam = -0.34f * 2.0f / SCALE_LIMIT;
    }
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) U.fx[r][c] = CellFx();
    b.turn = Turn::YOU_MAIN;
}
void DebugFlatsShowcase(int page) { DebugRun(3); U.ph = Ph::Showcase; U.showPage = page; }
int FlatsCatalogSize() { return (int)Catalog().size(); }
const char* FlatsCardName(int i) { return Catalog()[std::clamp(i, 0, (int)Catalog().size() - 1)].name.c_str(); }

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
