#pragma once
// ============================================================================
//  A NIGHT OFF - arcade game 6 of the Deep Arcade (Reference_For_Future_MP_Games/A Night Off — Arcade Game 6 Design
//  Document.pdf; its OCR is in docs/nightoff_pdf_pages/). A 1-6 player third-person comedy sandbox: the Nautilus's crew
//  have one night ashore at a dockside bar, the Sodden Gull, from 7 p.m. to 3 a.m. Drinking is the dial (every drink
//  trades charisma for toughness); the morning-after screen is the real scoreboard.
//
//  This header is headless (no drawing): the data (data/nightoff/*.json), the bar's shape, and the Night that steps a
//  night. nightoff_game.cpp is the scene. World units are metres: x west to east, z the street (south) to the yard
//  (north), y up.
// ============================================================================
#include "raylib.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace no {

// ---------------------------------------------------------------- the data
struct Band { float from = 0; std::string state; float charisma = 1, toughness = 1; };
struct DrinkDef {
    std::string key, name, where, effect, line;
    float drunk = 0, price = 0, amount = 0, minutes = 0, eatS = 0;
};
struct Box { std::string kind; Rectangle r{}; float h = 1; };        // furniture (x, z, width, depth) and its height
struct Room { std::string key, name; Rectangle r{}; };
struct Wall { Vector2 a{}, b{}; };
struct BarData {
    std::string name; float wallH = 3.4f;
    std::vector<Vector2> nav; std::vector<std::vector<int>> navLinks; std::vector<std::string> navNames;
    std::vector<std::pair<std::string, std::vector<Vector2>>> seats;   // by kind: snug, cards, tables, games, dance, corner, yard
    const std::vector<Vector2>* Seats(const std::string& kind) const;
    std::vector<Room> rooms; std::vector<Wall> walls; std::vector<Box> boxes;
    std::vector<Vector2> stools; std::vector<Vector3> lamps;
    Vector2 bartender{}, serve{}, hatch{}, door{}, spawn{}, dartboard{}, fortune{}, mirror{}, jukebox{};
};
// the patrons (doc pp. 8-13): types, traits, the regulars, the generator's parts; the dialogue (p. 26)
enum PatronType { T_TALKER, T_FLIRT, T_BROODER, T_HUSTLER, T_REGULAR, T_GAMBLER, T_SAILOR, T_ODDBALL, T_STAFF, T_COUNT };
const char* TypeName(int t);
struct TypeDef { std::string want, approach, danger, haunt; int tolerance = 4; float hp = 80; };
struct Look { int model = 1; Color top{120, 100, 80, 255}, hat{80, 70, 60, 255}; float build = 1, height = 1; std::string beard; };
struct PatronDef {
    std::string name, secret, tell, staff; int type = T_TALKER; std::vector<int> traits;
    bool thief = false, rich = false; int stool = -1; float arrive = 19, leave = 26; Look look;
};
struct Lines { std::vector<std::string> v; const std::string& Pick(uint32_t k) const; };
struct Dialogue {
    Lines you[4], youDrunk[4], listen, buy;                     // ask, agree, joke, challenge
    Lines greet[T_COUNT], ok[4], fail[4], listenReply, drink, endGood, endBad, hostile;
    std::vector<std::string> items;
};
struct Data {
    std::vector<Band> bands; std::vector<DrinkDef> drinks; BarData bar;
    TypeDef types[T_COUNT]; std::vector<std::string> traitNames, genericSecrets, firstNames, lastNames; std::vector<PatronDef> regulars;
    std::vector<std::array<float, 3>> crowdHours; float crowdMul[3] = {0.5f, 1, 1.5f};
    Dialogue talk;
    int Trait(const std::string& name) const;
    float soberPerMin = 1, rise10 = 1.2f, lastCallHour = 25.5f, lastCallMult = 2, vomitChance = 0.08f, vomitDrop = 15, vomitStun = 5;
};
const Data& D();
int DrinkIndex(const std::string& key);
const char* CrewName(int crew);   // 0 Diver, 1 Whaler, 2 Stowaway, 3 Mechanic, 4 Captain, 5 Nurse
const char* RoomAt(Vector2 p);    // the room's name, or "the street"

// ---------------------------------------------------------------- the night
constexpr float SECONDS_PER_GAME_MINUTE = 4;      // about four real minutes per game hour (doc p. 2)
constexpr float NIGHT_MINUTES = 8 * 60;           // 7 p.m. to 3 a.m.
enum class State : uint8_t { Active, Drinking, Eating, Vomiting, PassedOut, Gone };
enum Ending : uint8_t { E_NONE, E_WALKED, E_HOME_WITH, E_PASSED_OUT, E_KNOCKED_OUT, E_ARRESTED, E_HOSPITAL, E_ROBBED, E_THROWN_OUT, E_CLOSING };
const char* EndingName(int e);
struct Input {
    float moveX = 0, moveZ = 0;                       // the wished direction in the world (-1..1), from the camera
    float faceYaw = 0;
    bool run = false, use = false;                    // use: E (order at the bar, eat at the hatch, leave at the door)
    int order = -1;                                   // a drink or dish picked from the menu (an index into D().drinks)
    int talkTo = -1;                                  // start a conversation with a patron
    int say = -1;                                     // a conversation's option: 0 ask, 1 agree, 2 joke, 3 challenge, 4 listen, 5 buy them a drink, 6 walk away
    bool leave = false;                               // confirm walking home
};
struct Talk {                                         // the conversation mini-game (doc p. 10, p. 40)
    int patron = -1; int exchanges = 0, wins = 0, losses = 0, target = 4;
    std::string theirLine, myCaption, result; bool over = false; float overT = 0, listenT = 0;
    int lastOption = -1; bool lastWin = false, substituted = false;
};struct Moment { float t; int kind; std::string text; };   // the night's log (the morning screen's story)
struct Player {
    int id = 0; std::string name; int crew = 0; bool bot = false;
    State st = State::Active; int ending = E_NONE; std::string wokeAt;
    Vector2 pos{}, vel{}; float yaw = 0;
    float drunk = 0, money = 200, tab = 0; int kidneys = 2; float hp = 100;
    float actT = 0; int acting = -1;                  // drinking or eating: seconds left, and what
    float charBuffT = 0, charBuff = 0, toughBuffT = 0, toughBuff = 0, honestT = 0, visionsT = 0, shakesT = 0;
    bool hiccup = false;
    float swayPh = 0, stumbleT = 0, stumbleDir = 0, vomitT = 0, lurch = 0;   // the drunk walk: a curve, stumbles, a lurch
    int drinks = 0; float peakDrunk = 0, spent = 0;
    Talk talk; std::vector<std::string> items, known;   // items given; secrets learned (patron names whose secret you know)
    Input in;
    std::vector<Moment> log;
};
struct Bartender {
    float mood = 55;                                  // 0 Hostile .. 100 Delighted (doc p. 20)
    Vector2 pos{}; float busyT = 0; int servingFor = -1; float polishPh = 0;
};
struct Memory { float drinks = 0, insults = 0, talks = 0, games = 0, fights = 0, flirts = 0, listened = 0; };
struct Patron {
    int id = 0, reg = -1;                             // reg: which regular (-1: one of the generator's extras)
    std::string name, secret, tell; int type = T_TALKER; uint32_t traits = 0; bool thief = false, rich = false;
    float mood = 50; int bothers = 0;                 // mood 0 Hostile .. 100 Delighted; bothers against the type's tolerance
    float arriveH = 20, leaveH = 25;
    bool inside = false, gone = false, leaving = false;
    Vector2 pos{}, vel{}; float yaw = 0, walkPh = 0;
    std::vector<int> path; Vector2 goal{}; std::string seatKind; int seat = -1; float nextGoalT = 0; bool sitting = false;
    float drunk = 0, drinkT = 0; int talkingTo = -1;
    Look look; std::vector<Memory> mem;               // a memory per player
    bool Has(int trait) const { return trait >= 0 && ((traits >> trait) & 1); }
};

struct Opts { int players = 1; uint32_t seed = 1; int crowd = 1; };   // crowd: 0 Dead, 1 Normal, 2 Packed, 3 Random

struct Night {
    Opts opts; uint32_t rng = 1;
    float t = 0;                                      // real seconds since 7 p.m.
    std::vector<Player> players;
    std::vector<Patron> patrons;
    Bartender bar;
    std::vector<int> seatTaken;                       // (index: a seat key; value: who's on it: patron id, or -1)
    bool over = false;
    std::vector<std::string> say;                     // the toasts and the bartender's lines (newest last)

    void Init(const Opts& o);
    void Step(float dt);
    float Minutes() const { return t / SECONDS_PER_GAME_MINUTE; }   // game minutes since 7 p.m.
    float Hour() const { return 19 + Minutes() / 60; }              // 19.0 .. 27.0 (3 a.m.)
    std::string Clock() const;                                      // "9:42 p.m."
    float Rand();
    float Rand(float a, float b) { return a + (b - a) * Rand(); }
    // the drunk meter (doc p. 6)
    const Band& BandOf(const Player& p) const;
    float Charisma(const Player& p) const;
    float Toughness(const Player& p) const;
    float PriceOf(int drink) const;
    bool NearServe(const Player& p) const;
    bool NearHatch(const Player& p) const;
    bool NearDoor(const Player& p) const;
    bool Order(Player& p, int drink, std::string* why = nullptr);
    void Leave(Player& p, int ending, const std::string& where);
    void Note(Player& p, int kind, const std::string& text);
    void Say(const std::string& s);
    void StepPlayer(Player& p, float dt);
    void Collide(Vector2& pos, float r) const;
    int Score(const Player& p) const;
    // the patrons (nightoff_patrons.cpp)
    void InitPatrons();
    void StepPatrons(float dt);
    int CrowdTarget() const;                          // how many should be in the bar this hour (the crowd curve)
    int AddPatron(int reg);                           // a regular (reg >= 0) or a generated extra (-1)
    std::vector<int> NavPath(Vector2 from, Vector2 to) const;
    int NearestPatron(const Player& p, float range) const;
    const char* MoodName(float mood) const;
    void StartTalk(Player& p, int patron);
    void TalkChoose(Player& p, int option);
    void EndTalk(Player& p);
    float Difficulty(const Patron& c, int option) const;
    float Noise() const;                              // 0..1 the room's noise
    std::string Headline() const;
    std::string MorningLine(const Player& p) const;
};

int RunNightTest();   // --night-test
int RunPatronCheck(); // --patron-check (doc p. 30)

} // namespace no
