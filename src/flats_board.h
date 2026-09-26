// ============================================================================
//  DEPTH - Flats: the board and a single battle.
//
//  The board is a 4x4 grid. Rows 0-1 are the dealer's, rows 2-3 are yours:
//      row 0  dealer's QUEUE   - cards the dealer has laid down; they step forward at the start of his turn
//      row 1  dealer's FRONT   - fights
//      row 2  your FRONT       - fights
//      row 3  your RESERVE     - where a card lands when it is knocked back or shoved aside; it steps forward when the lane is empty
//  Every creature in a front row strikes the space straight ahead each combat. If that space is empty the
//  blow lands on the scales instead. Three numbers interact:
//      strength  hurts the card across from it        defense  is how much hurt it can take
//      weight    decides the shoving: a blow from a heavier card knocks a survivor back a row; a heavier mover pushes a
//                lighter card aside, and a mover that is not heavier than the card in its way is crushed.
//  The data flows one way: the Battle drives phases, the Board resolves them, and both append Events that
//  the drawing code plays back (nothing here draws anything).
// ============================================================================
#pragma once
#include "flats_card.h"
#include <utility>

namespace flats {

constexpr int COLS = 4, ROWS = 4;
constexpr int R_FOE_QUEUE = 0, R_FOE_FRONT = 1, R_YOU_FRONT = 2, R_YOU_BACK = 3;
constexpr int SCALE_LIMIT = 8;      // the scales tip this far and the battle is over
constexpr int MAX_HAND = 8;
constexpr int MAX_ITEMS = 3;

enum class Side { YOU, FOE };
inline Side Owner(int row) { return row <= 1 ? Side::FOE : Side::YOU; }
inline Side Other(Side s) { return s == Side::YOU ? Side::FOE : Side::YOU; }
inline int Idx(Side s) { return s == Side::YOU ? 0 : 1; }
inline int FrontRow(Side s) { return s == Side::YOU ? R_YOU_FRONT : R_FOE_FRONT; }
inline int BackRow(Side s) { return s == Side::YOU ? R_YOU_BACK : R_FOE_QUEUE; }

// Trinkets carried between battles (bits in a mask).
enum Charm { CH_PEARL, CH_KNOT, CH_COMPASS, CH_TOOTH, CH_ANCHOR, CH_JAR, CH_BARB, CH_COUNT };
const char* CharmName(int c);
const char* CharmText(int c);

struct Cell { bool used = false; Card card; };

// What happened, in order, so the scene can animate it.
struct Event {
    enum Type { Play, Strike, Damage, ScaleHit, Death, Move, Knock, Push, Crush, SigilFired, Evolve, Bones, Gold, Drew, ItemFound, PhaseChange, Tide } type = Play;
    int r0 = 0, c0 = 0, r1 = 0, c1 = 0, amount = 0;
    std::string text;
};
using Events = std::vector<Event>;

struct SideRules {
    int boonSuit = -1;      // creatures of this suit gain +1 strength (a dealer's totem)
    int strAll = 0;         // flat strength bonus to every creature
    int weightAll = 0;      // flat weight bonus
    int tempStr = 0, tempTurns = 0;   // an ink cloud: a penalty that wears off
    int boneOnKill = 0;     // extra bones for each creature slain
    bool pearl = false;     // Shell creatures enter with +1 defense
};

class Board {
public:
    Cell cell[ROWS][COLS];
    int scale = 0;                    // positive: you are ahead. Reaching +/-SCALE_LIMIT ends the battle
    int bones[2] = {0, 0};
    SideRules rules[2];
    std::vector<Card> returned[2];    // Undying copies waiting to go back into a hand
    int itemsFound[2] = {0, 0};       // Scavenger finds waiting to be picked up
    int gold = 0;                     // Gilt kills: gold for the pot
    bool massive = false;             // Selenis is on the board: he is anchored at the dealer's front lane 0 but fills all four
    int tideDir = 1;                  // which way the next Tidal Pull drags

    bool Empty(int r, int c) const { return c >= 0 && c < COLS && !cell[r][c].used; }
    int EffStrength(int r, int c) const;
    int EffWeight(int r, int c) const;

    void Put(int r, int c, const Card& k, Events& ev);
    bool Kill(int r, int c, Side killer, bool sacrificed, int killerEdition, Events& ev);
    void Move(int r0, int c0, int r1, int c1, Events& ev);

    // combat: one column at a time so the scene can show each strike
    bool StrikeColumn(Side s, int col, Events& ev);
    void EndPhase(Side s, Events& ev);            // evolutions, drifting, shoving, stepping forward, ageing
    void AdvanceQueue(Events& ev);                // the dealer's queue steps into his front row
    void SentinelPass(Side defender, Events& ev); // sentinels step in front of enemies that arrived across an empty lane
    bool Shove(int r, int c, int dir, bool crushIfLight, Events& ev);
    void TidalPull(Side victim, Events& ev);      // Selenis drags every creature of `victim` one lane sideways; the edge is fatal (Waterborne creatures submerge and stay)
    // The cell a blow at (r, c) really lands on: with Selenis on the board every dealer front lane is Selenis.
    bool Occupied(int r, int c) const { return cell[r][c].used || (massive && r == R_FOE_FRONT && cell[R_FOE_FRONT][0].used); }
    int RealCol(int r, int c) const { return (massive && r == R_FOE_FRONT && !cell[r][c].used) ? 0 : c; }

    bool OverCheck() const { return scale >= SCALE_LIMIT || scale <= -SCALE_LIMIT; }

private:
    void HitScale(Side s, int amount, int r, int c, Events& ev);
    void TryBurrow(Side defender, int col, Events& ev);
};

// ---------------------------------------------------------------- one battle
enum class PackItem { BOULDER, GOAT, FISHHOOK, INK, BANDAGE, HARPOON, COUNT };
const char* ItemName(int k);
const char* ItemText(int k);
bool ItemNeedsTarget(int k);

enum class Turn { YOU_DRAW, YOU_MAIN, YOU_COMBAT, YOU_END, FOE_START, FOE_MAIN, FOE_COMBAT, FOE_END, OVER };
enum class Boon { NONE, EXTRA_DRAW, BONES, HEAD_START };   // what spent momentum buys at the start of a battle

struct DealerInfo { const char* name; const char* line; const char* twist; int plays; int draws; };
const DealerInfo& Dealer(int i);
constexpr int DEALERS = 5;          // 0-3 the tables on the way, 4 the Atlantean Sovereign (the boss, in two phases)

struct BattleSetup {
    std::vector<Card> deck;
    unsigned charms = 0;
    std::vector<int> items;
    int startBones = 0;
    int dealer = 0;
    bool elite = false;
    Boon boon = Boon::NONE;
    std::vector<std::pair<int, int>> totems;   // scrimshaw totems: (tribe, sigil): a creature of that tribe gains the sigil when played
};

class Battle {
public:
    Board board;
    std::vector<Card> deck, hand, foeDeck, foeHand;
    std::vector<int> items;
    Turn turn = Turn::YOU_DRAW;
    int turnNo = 1, col = 0, foePlays = 0, dealer = 0;
    bool elite = false, firstPlayThisTurn = true;
    int winner = 0;                   // +1 you, -1 the dealer, once the turn is OVER
    unsigned charms = 0;
    int foeMaxPlays = 1;
    bool boss = false;                // the Sovereign: when the scales tip your way he sacrifices his court and Selenis rises
    int phase = 1;                    // 1 the Drowned Phalanx, 2 the Lunar Tide
    std::vector<std::pair<int, int>> totems;

    void Start(const BattleSetup& s, Rng& rng);
    bool Waiting() const { return turn == Turn::YOU_DRAW || turn == Turn::YOU_MAIN || turn == Turn::OVER; }

    // your turn ----------------------------------------------------------
    bool Draw(bool fromDeck, Events& ev, Rng& rng);           // upkeep: a card from the deck, or a free minnow
    int BloodNeeded(const Card& c) const;                     // after the Sailor's Knot
    bool CanPlay(int handIdx, int col, const std::vector<std::pair<int, int>>& sacs, std::string* why) const;
    bool Play(int handIdx, int col, const std::vector<std::pair<int, int>>& sacs, Events& ev, Rng& rng);
    bool UseItem(int slot, int r, int c, Events& ev);
    void EndTurn();                                           // ring the bell: main phase -> combat
    void RiseSelenis(Events& ev);                             // phase two of the Sovereign: he sacrifices his court and the Moon God rises
    // the automatic phases: one micro-step per call (one column of combat, one card the dealer lays, ...)
    void Advance(Events& ev, Rng& rng);
    void AutoYourTurn(Events& ev, Rng& rng);                  // a sensible player: used by the simulator and the hints

    int HandCount() const { return (int)hand.size(); }
    int TotalTurns() const { return turnNo; }

private:
    void FoeChoose(Events& ev, Rng& rng);
    void Drain(Events& ev);                                   // move Undying copies and finds into hands
    void CheckOver(Events& ev);
    void FoeDrawCards(int n, Rng& rng);
    int BestColumn(const Card& c, Side me, Rng& rng) const;
};

std::vector<Card> BuildDealerDeck(int dealer, bool elite, Rng& rng);

}  // namespace flats
