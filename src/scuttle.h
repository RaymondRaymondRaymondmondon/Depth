#pragma once
// Scuttle, the Deep Arcade's crab-racing card game (Master Reference, "Arcade game 3: Scuttle"), as a headless engine:
// no raylib, no network. The host runs it; clients receive what their seat may see (Serialize with a viewer).
//   2-4 crabs race along an 8-space tide pool. Each turn: play one card, optionally place a bet token, draw back to 5.
//   A round ends when a crab reaches space 8; a match is first to 2 round wins (3 for a longer game).
#include "net_msg.h"
#include <cstdint>
#include <string>
#include <vector>

namespace scuttle {

enum CardType : uint8_t { C_SCUTTLE1, C_SCUTTLE2, C_SCUTTLE3, C_SIDESTEP, C_WAVE, C_PINCH, C_SHELL, C_CURRENT, C_GULL, C_ROCK, C_MOLT, C_TIDERUSH, C_COUNT };
struct CardDef { const char* name; const char* text; int copies; };
const CardDef& Card(int c);   // the deck list (scuttle.cpp): 60 cards

constexpr int TRACK = 8, HAND = 5, MAX_SEATS = 4, BETS_PER_ROUND = 3;
extern const float TURN_SECONDS, RESPONSE_SECONDS;   // the turn timer (20 s) and the Shell response window

enum Phase : uint8_t { PH_PLAY, PH_RESPONSE, PH_BET, PH_ROUND_OVER, PH_MATCH_OVER };

struct Seat {
    bool used = false;
    uint8_t pos = 0;            // 0..TRACK
    bool skipMove = false;      // Pinched, or stopped by a rock: the owner's next move card does nothing
    std::vector<uint8_t> hand;  // card types (only the viewer's own hand is ever sent to a client)
    int handCount = 0;          // what everyone may know about the hand
    uint8_t betsLeft = BETS_PER_ROUND;
    int points = 0, roundWins = 0;
};
struct Bet { uint8_t owner, crab; };

struct State {
    int nSeats = 0, winsNeeded = 2;
    Seat seats[MAX_SEATS];
    std::vector<uint8_t> deck, discard;
    int rock = -1;                        // the space a rock sits on, -1 for none
    int turn = 0, phase = PH_PLAY, round = 1, firstPlayer = 0;
    std::vector<Bet> bets;                // face down: a client sees its own and only a count of the others'
    int betsHidden[MAX_SEATS] = {};       // (client side) how many bets each other seat has down
    int pendCard = -1, pendPlayer = -1, pendTarget = -1;
    uint8_t respondMask = 0, shellMask = 0; // who may still answer with a Shell; who did
    int roundWinner = -1, roundSecond = -1, matchWinner = -1;
    int turnsPlayed = 0;
    int plays = 0;                        // cards resolved so far this match (a screen animates each new one)
    int lastCard = -1, lastPlayer = -1, lastTarget = -1;   // the last card resolved, who played it, at what
    uint8_t lastShelled = 0;              // who ducked into a Shell against it
    float timer = 0;                      // seconds left for the current actor (the host counts it down)
    uint32_t rng = 1;
    std::vector<std::string> log;         // the last few things that happened, for the table's ink
};

struct Action {
    enum Kind : uint8_t { PLAY, RESPOND, BET, SKIPBET, NEXTROUND } kind = PLAY;
    uint8_t seat = 0;
    uint8_t handIdx = 0;   // PLAY
    int8_t target = -1;    // PLAY: a seat (Sidestep, Pinch) or a space (Rock); BET: the crab (seat) to back
    bool shell = false;    // RESPOND: play a Shell to cancel
};

void NewMatch(State& s, int nSeats, uint32_t seed, int winsNeeded = 2);
int Actor(const State& s);                              // whose decision it is (-1: none; the host moves on)
bool Legal(const State& s, const Action& a, std::string* why = nullptr);
bool Apply(State& s, const Action& a);                  // the host's authority: false if illegal (ignored)
void Tick(State& s, float dt, std::vector<Action>& timeouts); // counts the timer down; a lapse makes the bot act
Action Bot(const State& s, int seat, uint32_t& rng);    // a simple bot (fills empty seats, stands in for drops)
bool NeedsTarget(int card);                             // Sidestep, Pinch, Rock
std::vector<int> Targets(const State& s, int seat, int card);

void Serialize(const State& s, int viewer, Writer& w);  // viewer -1: everything (the host's own copy, saves)
bool Deserialize(State& s, Reader& r);
void WriteAction(const Action& a, Writer& w);   // what a player sends (the host fills in the seat)

struct SimResult { int matches = 0, seatWins[MAX_SEATS] = {}; double avgTurns = 0, avgRounds = 0; };
SimResult Simulate(int matches, int nSeats, uint32_t seed);  // bots only: --scuttle-sim
}
