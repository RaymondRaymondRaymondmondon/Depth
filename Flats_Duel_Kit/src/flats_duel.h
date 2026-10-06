// ============================================================================
//  DEPTH - Flats Duel (Deep Arcade game 1, Master Reference "Arcade game 1: Flats Duel"): the headless rules engine.
//
//  Two people at the Flats table, best of three, on a symmetric board. No raylib, no network: the host runs it inside
//  its GameHost (arcade_games.cpp), clients only ever receive Serialize(state, viewer) and decode it with ReadView.
//
//  THE PERSPECTIVE FLIP. The single-player engine (flats_board.*) always plays "you" (rows 2-3) against "the dealer"
//  (rows 0-1). A duel reuses it unchanged: the Battle is always kept in the ACTIVE player's frame, so whoever's turn it
//  is plays as Side::YOU with the real Draw / Play / UseItem / EndTurn / Advance. When a turn ends the board is
//  flipped (rows 0<->3 and 1<->2, the scales negated, bones/rules/returns swapped) and the hands and decks change
//  places. Row 0, the dealer's queue in single-player, is simply the opponent's reserve here. Serialize flips again
//  as needed so every viewer always sees themselves in rows 2-3, exactly as flats.cpp draws single-player.
//
//  Flow of a match:
//      PH_SETUP      both pick a dealer skin and two items; player 0 (the host) picks the mode; constructed decks are
//                    submitted and checked; Quick picks a preset. Both ready -> PH_DRAFT (Draft) or PH_PLAY.
//      PH_DRAFT      13 packs of 4, open (Rochester): the opener picks, then the other, then the opener, then the other.
//      PH_BUILD      each player keeps 20 of their 26 picks; the rest is their sideboard.
//      PH_PLAY       one game. Turns alternate: upkeep (a card or a Minnow) -> plays and items -> the bell -> combat,
//                    one lane per micro-step (paced by the host so both screens see each strike) -> the end phase.
//      PH_GAME_OVER  a pause on the result; then PH_SIDEBOARD (best of three) or PH_MATCH_OVER.
//      PH_SIDEBOARD  up to 3 swaps between deck and sideboard, then both ready -> the next game (its loser goes first).
// ============================================================================
#pragma once
#include "flats_duel_data.h"
#include <string>

namespace flats {
namespace duel {

enum Phase : uint8_t { PH_SETUP, PH_DRAFT, PH_BUILD, PH_PLAY, PH_GAME_OVER, PH_SIDEBOARD, PH_MATCH_OVER };

// What a player sends: [u8 kind][payload] (WriteAction/ApplyAction). All rows and lanes are in the SENDER's frame
// (their front is row 2, their reserve row 3, the opponent's front row 1 and reserve row 0).
struct Action {
    enum Kind : uint8_t {
        SET_MODE,       // a: mode (player 0 only, during setup)
        SET_SKIN,       // a: skin
        SET_ITEMS,      // a, b: two PackItems
        SET_PRESET,     // a: preset (Quick, and the fallback deck for Constructed)
        SUBMIT_DECK,    // cards (id, edition): the deck, then side: the sideboard (Constructed)
        READY,          // a: 1 ready, 0 not (setup and sideboard)
        PICK,           // a: index in the open pack (draft)
        BUILD,          // idx: indices into your pool that make the deck (build)
        DRAW,           // a: 1 from the deck, 0 a Minnow (upkeep)
        PLAY,           // a: hand index, b: lane, sacs: (row, lane) of your creatures to sacrifice
        ITEM,           // a: item slot, b: row, c: lane (-1, -1 for an untargeted item)
        BELL,           // end your turn
        SWAP,           // a: deck index, b: sideboard index (sideboard)
        EMOTE,          // a: Emote
        CONCEDE,        // give up the current game
        COUNT
    } kind = BELL;
    int a = -1, b = -1, c = -1;
    std::vector<std::pair<int, int>> sacs;
    std::vector<std::pair<int, int>> cards, side;   // (id, edition)
    std::vector<int> idx;
};
void WriteAction(const Action& a, Writer& w);
bool ReadAction(Reader& r, Action& a);

struct Zone {                       // one player's everything
    int skin = 0;
    int loadout[2] = {(int)PackItem::BANDAGE, (int)PackItem::HARPOON};
    int preset = 0;
    bool ready = false, submitted = false;
    std::vector<Card> pool;         // draft picks (Draft), or the submitted list (Constructed)
    std::vector<Card> deckList, sideboard;
    int swapsLeft = 0;
    // one game
    std::vector<Card> deck, hand;
    std::vector<int> items;
    int itemsUsed = 0, undyingUsed = 0;
    int gamesWon = 0;
    float emoteCd = 0;
    int emote = -1, emoteSeq = 0;   // the last emote and a counter (a screen plays it when the counter moves)
};

struct State {
    int mode = MODE_DRAFT;
    Phase phase = PH_SETUP;
    Zone z[2];
    Battle bt;                      // in the ACTIVE player's frame (see the top of this file)
    int active = 0, firstPlayer = 0, gameNo = 0, round = 1;
    int thisGameFirst = 0;          // who went first in the current (or just-finished) game; firstPlayer is the next game's
    bool suddenDeath = false;
    int gameWinner = -1;            // 0/1, 2 for a tie, -1 while playing
    int matchWinner = -1;           // 0/1, 2 for a drawn match
    float timer = 0, wait = 0;      // the actor's clock; the host's pacing clock
    // draft
    std::vector<Card> pack;
    int packNo = 0, picksInPack = 0, opener = 0;
    std::vector<std::pair<int, int>> picks;   // (player, card id), in order: public in an open draft
    // what just happened, for the screens (in the frame of `evFrame`)
    uint32_t step = 0;
    Events ev;
    int evFrame = 0, evActor = -1;
    std::vector<std::string> log;
    Rng rng;                        // host only: never serialized to a client (it would predict the draws)
    int bigHitSide = -1;            // last step: whose creature took 4+ in one blow (the opponent's figure reacts)
};

// ---- the host
void NewMatch(State& s, uint32_t seed);
int Actor(const State& s);                                      // who the game waits on (-1: the host moves it on; 2: both)
bool Legal(const State& s, int player, const Action& a, std::string* why = nullptr);
bool Apply(State& s, int player, const Action& a);             // false: illegal and ignored
bool HostTick(State& s, float dt, uint32_t aiMask);            // timers, pacing and the AI seats; true if anything changed
bool Over(const State& s);
Action Bot(const State& s, int player);                        // one decision for an AI seat (or a timed-out player)

// ---- what one viewer may see. viewer 0/1: a player; -1: a spectator (both hands hidden); FULL: everything (saves, tests).
constexpr int FULL = 99;
void Serialize(const State& s, int viewer, Writer& w);

struct View {                                   // a client's decoded snapshot, always in its own frame
    int me = -1;                                // -1 spectator
    int mode = 0, phase = 0, gameNo = 0, round = 1;
    bool myTurn = false, suddenDeath = false;
    int actor = -1, turn = 0;                   // flats::Turn of the active player's battle
    int gameWinner = -1, matchWinner = -1, firstPlayer = 0;
    float timer = 0;
    Board board;                                // viewer's rows 2-3
    std::vector<Card> hand;                     // mine (empty for a spectator)
    std::vector<int> items;                     // mine
    int deckCount[2] = {}, handCount[2] = {}, itemCount[2] = {}, itemsUsed[2] = {}, undyingUsed[2] = {};
    int gamesWon[2] = {}, skin[2] = {}, emote[2] = {-1, -1}, emoteSeq[2] = {}, bones[2] = {};
    bool ready[2] = {};
    // index 0 = me, 1 = the opponent, everywhere above (a spectator: 0 = player 0)
    std::vector<Card> pool, deckList, sideboard;   // mine (draft, build, sideboard)
    int swapsLeft = 0;
    std::vector<Card> pack;                        // the open pack (draft)
    int packNo = 0, picker = -1;                   // who picks now (0 me, 1 them)
    std::vector<std::pair<int, int>> picks;        // (0 me / 1 them, card id)
    uint32_t step = 0;
    Events ev;                                     // the last step's events, in my frame; the opponent's draws are hidden (amount -1)
    int evActor = -1;                              // 0 me, 1 them
    int bigHit = -1;                               // 0 mine, 1 theirs
    std::vector<std::string> log;
};
bool ReadView(Reader& r, View& v);

// ---- tools
void FlipBoard(Board& b);
Event FlipEvent(Event e);
struct SimReport {
    int games = 0, matches = 0, firstWins = 0, ties = 0, suddenDeaths = 0, roundLimitGames = 0;
    double avgRounds = 0, avgSteps = 0;
    int presetWins[6][6] = {}, presetGames[6][6] = {};
};
SimReport Simulate(int matches, int mode, uint32_t seed);      // bots only: --flats-duel-sim
bool VerifyNoLeak(int matches, uint32_t seed, std::string* why);   // hidden information never changes a player's snapshot
bool VerifyViews(int matches, uint32_t seed, std::string* why);    // both clients decode every snapshot and agree on the board

}  // namespace duel
}  // namespace flats
