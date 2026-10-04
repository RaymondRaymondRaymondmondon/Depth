#pragma once
// ============================================================================
//  A NIGHT OFF - the bar games (doc pp. 17-18), stage 3: darts, pool, mini golf, the slot machines, scratch-offs and
//  the fortune teller. Headless: each game is rules + physics + a bot that plays it at a skill (an error multiplier: a
//  sober player is 1.0, lower is better) bent by the drunk meter (AimMul). The scene draws them (nightoff_gamesui.cpp);
//  --game-check plays a bot against each hustler at 0, 40 and 80 drunk (the targets: 55%, 35%, 10%).
// ============================================================================
#include "raylib.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace no {

struct GRng {
    uint32_t s = 1;
    float U() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) / 16777216.0f; }
    float N();                                                   // a standard normal
    int I(int n) { return n <= 0 ? 0 : (int)(U() * n) % n; }
};

// ---------------------------------------------------------------- the data (data/nightoff/nightoff_games.json)
struct Opponent { std::string name, tell; float skill = 1; bool hustler = false, sandbag = false, once = false, teaches = false; int stake = 0; };
struct GamesData {
    std::vector<std::array<float, 2>> aimCurve;
    float dartSigma = 26; std::vector<int> dartStakes; std::vector<Opponent> darts;
    float poolSigma = 0.012f, poolPower = 0.07f, poolMissFrom = 60; std::vector<int> poolStakes; std::vector<Opponent> pool;
    float golfSigma = 0.035f, golfPower = 0.08f, golfCloses = 26; std::vector<int> golfStakes; std::vector<Opponent> golf;
    int slotCost = 2; std::vector<std::string> slotSymbols; std::vector<std::vector<float>> slotWeights; int slotHonest = 1;
    int payPair = 50, payTriple = 200, payLine = 1000;
    int scratchCost = 5; std::vector<int> prizes; std::vector<float> dispenser, pip; float pipMap = 0.02f; std::vector<std::string> scratchSymbols;
    int fortuneCost = 10; std::vector<std::string> deck;
};
const GamesData& GD();
float AimMul(float drunk);                                       // the meter's effect on a thrower's error

// ---------------------------------------------------------------- darts (501 straight out, or Around the Clock)
namespace darts {
constexpr float R_BULL = 6.35f, R_OUTER = 15.9f, R_T_IN = 99, R_T_OUT = 107, R_D_IN = 162, R_D_OUT = 170;   // mm
extern const int ORDER[20];
struct Hit { int value = 0, mult = 0; int Points() const { return value * mult; } };   // mult 0: off the board
Hit Score(Vector2 mm);
Vector2 Bed(int value, int mult);                                // the middle of a bed (the fat single for mult 1; 25 is the bull)
struct Match {
    bool clock = false;
    int left[2] = {501, 501}, next[2] = {1, 1};                  // 501's remaining; Around the Clock's next number (21 = the bull)
    int turn = 0, dart = 0, turnStart = 501, winner = -1, turns = 0;
    int turnPts = 0; bool bust = false; int big[2] = {0, 0};     // the last turn's points; a 180 each
    std::vector<Vector2> marks;                                  // this turn's darts on the board
    void Start(bool clockMode, int first);
    Vector2 BotAim() const;                                      // where a sensible thrower aims
    void Throw(Vector2 landed);                                  // a dart lands: scores, busts, the turn passes after three
};
}

// ---------------------------------------------------------------- pool (8-ball on a 2.24 x 1.12 m bed)
namespace pool {
constexpr float W = 2.24f, H = 1.12f, BR = 0.0286f;
struct Ball { Vector2 p{}, v{}; bool in = false; };
struct Shot { float ang = 0, power = 3, english = 0; };       // power: m/s off the cue; english: -1 draw .. 1 follow
struct Result { std::vector<int> potted; int firstHit = -1; bool scratch = false, missedCue = false; float time = 0; };
struct Table {
    Ball b[16];                                                  // 0 the cue ball, 1-7 solids, 8 the eight, 9-15 stripes
    Result Simulate(const Shot& s, bool record = false);         // until everything stops (record: the frames for the replay)
    bool Moving() const;
    void Step(float dt, Result& r, bool& hitAny, int& first, float englishLeft, Vector2 shotDir);
    std::vector<std::array<Vector2, 16>> frames;                 // the last recorded shot (every 1/60 s)
};
extern const Vector2 POCKETS[6];
struct Match {
    Table t; int turn = 0, winner = -1, group[2] = {0, 0};       // group 0 open, 1 solids, 2 stripes
    bool ballInHand = false, broken = false, replay = true; int shots = 0;
    std::string last;                                            // what happened, for the caption
    void Rack(GRng& r, int first);
    bool Legal(int ball, int who) const;                         // may 'who' hit 'ball' first
    int Left(int who) const;                                     // balls of their group still on the table
    void Play(const Shot& s, bool missedCue = false);            // simulate and apply the rules
};
Shot BotShot(Match& m, float skill, GRng& r, bool exact = false);   // choose (and, unless exact, fumble) a shot
Vector2 BotPlace(Match& m, GRng& r);                              // ball in hand: where to put the cue ball
}

// ---------------------------------------------------------------- mini golf (the yard: nine holes)
namespace golf {
constexpr float BR = 0.021f, CUP = 0.054f;
struct Seg { Vector2 a, b; };
struct Hole {
    std::string name, feature; Vector2 tee{}, cup{}; std::vector<Seg> walls; Rectangle box{};
    Vector2 mill{}; float millR = 0;                             // the windmill: blades turning about mill
    Vector2 drain{}; float drainR = 0; Vector2 drainOut{};       // the drain: a ball in it comes out at drainOut
    Vector2 dog{}; float dogR = 0;                               // the sleeping dog (a soft round obstacle that shifts when hit)
    Rectangle loop{}; float loopV = 0;                           // the loop: a ball slower than loopV rolls back out
    int par = 2;
};
const std::vector<Hole>& Course();
struct Ball { Vector2 p{}, v{}; bool holed = false; };
struct Sim { float t = 0; Vector2 dog{}; };                      // the clock (the windmill) and the dog
bool Roll(const Hole& h, Ball& b, Sim& s, float maxT = 12, std::vector<Vector2>* path = nullptr);   // true if holed
struct Match {
    int hole = 0, strokes[2][9] = {}, turn = 0, winner = -1; Ball ball[2]; bool done[2] = {false, false}; Sim sim;
    int maxStrokes = 6; bool rain = false, solo = false;
    bool lastHoled = false; int lastWho = 0, lastHole = 0, lastStrokes = 0;   // the last putt (a hole in one is a story)
    void Start(int first);
    void Shoot(float ang, float power, std::vector<Vector2>* path = nullptr);
    int Total(int who) const;
};
void BotShot(const Match& m, float skill, GRng& r, float& ang, float& power, bool exact = false);
}

// ---------------------------------------------------------------- the slot machines, scratch-offs, the fortune teller
namespace slots {
struct Pull { int reel[3]{}; int pays = 0; bool kidney = false; };
Pull Spin(int machine, GRng& r);
float ReturnRate(int machine);                                   // the machine's expected return per coin (exact)
}
namespace scratch {
struct Ticket { int prize = 0; bool map = false; int sym[3]{}; };
Ticket Buy(bool pip, GRng& r);
}
struct Night;
struct Player;
namespace fortune {
struct Reading { int card[3]{}; std::string text[3]; };
Reading Read(const Night& n, const Player& p, GRng& r);           // three cards and what they mean tonight (true things)
}

// ---------------------------------------------------------------- a player's seat at a game (Player::game)
enum GameKind { GK_DARTS, GK_POOL, GK_GOLF, GK_SLOTS, GK_SCRATCH, GK_FORTUNE, GK_PIP, GK_DANCE, GK_COUNT };   // (GK_DANCE: the band's rhythm game)   // (GK_PIP: a scratch-off from Pip's coat)
const char* GameName(int kind);
struct GameSeat {
    int kind = -1, machine = 0, opp = -1, stake = 0;             // opp: a patron id, -1 alone, -2 the bartender
    float oppSkill = 1.5f; std::string oppName, tell; bool hustler = false, sandbag = false;
    bool over = false, tellShown = false; int result = -1;       // 0 you won, 1 they did, 2 a draw
    float botT = 0, replayT = 0; int replayLen = 0;              // the opponent's pause; the replay running (pool frames, the golf path)
    darts::Match darts; pool::Match pool; golf::Match golf; std::vector<Vector2> golfPath; int golfWho = 0, golfHole = 0;
    // the last shot, so a guest can replay it itself (the frames and the path aren't sent): the table or the ball before it
    int shotSerial = 0; pool::Table shotTable; pool::Shot shot; bool shotMiss = false; golf::Ball shotBall; golf::Sim shotSim; bool shotRain = false;
    slots::Pull pull; int pulls = 0; float spinT = 0, autoT = 0;
    scratch::Ticket ticket; bool haveTicket = false, paid = false;
    fortune::Reading reading; bool haveReading = false;
    std::string caption; float captionT = 0;
    GRng rng;
};

int RunGameCheck(const std::string& game, int games);            // --game-check <darts|pool|golf|all> [games]

} // namespace no
