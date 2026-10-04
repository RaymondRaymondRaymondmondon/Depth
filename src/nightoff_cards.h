#pragma once
// A Night Off: the card room (doc p. 18), stage 8. Texas hold'em (blinds rising each hour, patrons with tells, the pot
// that can hold a kidney) and bullshit (shed cards face down by rank, claim the rank, call it). Headless: the tables'
// rules and the bots; the seats are players or patrons, and the Night drives them (nightoff_cardroom.cpp).
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

namespace no {
namespace cards {

struct Card { int8_t r = 0, s = 0; bool operator==(const Card& o) const { return r == o.r && s == o.s; } };   // r 2..14 (14 the ace), s 0..3
std::string Name(Card c);                        // "K♠" style, as text: "K s"
int Eval(const Card* c, int n);                  // the best five of n (5..7): a comparable score
std::string HandName(int score);                 // "two pair", "a flush"...
struct Rng { uint32_t s = 1; float U() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) / 16777216.0f; } int I(int n) { return n <= 0 ? 0 : (int)(U() * n) % n; } };
void Shuffle(std::vector<Card>& d, Rng& r);
std::vector<Card> Deck();

// ---------------------------------------------------------------- hold'em
struct Seat {
    int kind = -1, idx = -1;                     // 0 a player, 1 a patron (who sits here)
    std::string name;
    int stack = 0, bet = 0, total = 0;           // chips; this street's bet; this hand's
    bool folded = false, allIn = false, acted = false, out = false;
    Card hole[2];
    float skill = 1, bluff = 0.1f, drunk = 0;    // a bot's error, its bluffing, its courage
    std::string tell; bool tellNow = false;      // a patron's tell, and whether it's showing (a bluff)
    std::string last;                            // its last action ("raises 20")
    bool kidney = false;                         // staked a kidney (the cartel's hand)
    uint32_t peeked = 0;                         // players who've seen these cards (a marked deck)
};
struct Poker {
    std::vector<Seat> seats;
    Card board[5]; int boardN = 0;
    std::vector<Card> deck;
    int dealer = 0, turn = -1, street = 5;       // 0 pre-flop, 1 the flop, 2 the turn, 3 the river, 4 showdown, 5 between hands
    int toCall = 0, minRaise = 0, sb = 2, bb = 4, hand = 0;
    float actT = 0, nextT = 0;
    std::vector<int> winners; std::string result; int potShown = 0;
    bool kidneyPot = false, bluffWin = false;    // the cartel's hand: a kidney in the pot; the quiet man pays double for a bluff
    Rng rng;
    int Pot() const { int p = 0; for (const auto& s : seats) p += s.total; return p; }
    int Active() const;                          // seats still in the hand
    int Live() const;                            // seats with chips (in the game)
    void StartHand();
    bool Act(int seat, int action, int amount);  // 1 fold, 2 check or call, 3 raise to `amount` (this street): false if not allowed now
    void NextStreet();
    void Showdown();
    int BotChoose(int seat, int& amount, float drunk);   // a bot's action (Monte Carlo equity against the live hands)
    float Equity(int seat, int sims);
};

// ---------------------------------------------------------------- bullshit
struct BsSeat { int kind = -1, idx = -1; std::string name; std::vector<Card> hand; bool liar = false, honest = false; float drunk = 0; };
struct Bullshit {
    std::vector<BsSeat> seats;
    std::vector<Card> pile;
    int turn = 0, rank = 14, claimRank = 14;     // the rank to claim (aces first, then 2, 3 ... kings); the rank the last play claimed
    int lastSeat = -1, lastCount = 0; std::vector<Card> lastCards; std::string claim;   // the last play and what was said
    float window = 0;                            // the time left to call it
    int winner = -1, pot = 0, buyIn = 20; std::string result;
    int callSeat = -1; bool lastWasLie = false; float revealT = 0;   // the last call and how it went
    Rng rng;
    void Deal();
    bool Play(int seat, const std::vector<int>& cards);   // indices into that seat's hand, 1-4 of them
    bool Call(int seat);                                  // "bullshit!"
    void Resolve(bool called, int caller);
    int NextRank() const { return rank == 14 ? 2 : rank == 13 ? 14 : rank + 1; }
    void BotPlay(int seat, std::vector<int>& out);
    float BotDoubt(int seat) const;                       // how likely a bot thinks the last claim was a lie
};
std::string RankWord(int r, int n);              // "two kings", "an ace"

}  // namespace cards
int RunCardCheck(const std::string& game, int games);    // --game-check poker|bullshit
}  // namespace no
