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
#include "nightoff_games.h"
#include "nightoff_brawl.h"

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
    Vector2 bartender{}, serve{}, hatch{}, door{}, spawn{}, dartboard{}, fortune{}, mirror{}, jukebox{}, scratch{}, golf{};
};
// the patrons (doc pp. 8-13): types, traits, the regulars, the generator's parts; the dialogue (p. 26)
enum PatronType { T_TALKER, T_FLIRT, T_BROODER, T_HUSTLER, T_REGULAR, T_GAMBLER, T_SAILOR, T_ODDBALL, T_STAFF, T_COUNT };
const char* TypeName(int t);
struct TypeDef { std::string want, approach, danger, haunt; int tolerance = 4; float hp = 80; };
struct Look { int model = 1; Color top{120, 100, 80, 255}, hat{80, 70, 60, 255}; float build = 1, height = 1; std::string beard; };
struct PatronDef {
    std::string name, secret, tell, staff, home = "sincere"; int type = T_TALKER; std::vector<int> traits;   // home: where going home with them ends up (doc pp. 13-14)
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
enum class State : uint8_t { Active, Drinking, Eating, Vomiting, PassedOut, Gone, Down };   // Down: knocked out (30 s)
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
    int flirtWith = -1, flirtSay = -1;                // flirt with a patron; a line (0 compliment, 1 joke, 2 a drink, 3 a dance, 4 ask about them, 5 lean in; 6 walk away)
    int offer = 0;                                    // the offer: 1 take it, 2 decline
    bool askTrouble = false;                          // ask the bartender who's trouble tonight (a drink on your tab)
    bool fortuneYes = false;                          // the fortune teller asks you home
    // the bar games (nightoff_games.cpp): start one at a station, then act in it
    int startGame = -1, gameMachine = 0, gameOpp = -1, gameStake = 0;   // GK_*; the table or machine; a patron id (-1 alone, -2 the bartender); the stake
    int gameAct = 0;                                  // 1 throw / shoot / pull / reveal / read, 2 place the cue ball, 3 leave, 4 again, 5 buy another
    Vector2 gameAim{}; float gamePower = 0, gameEnglish = 0;
    // fighting (nightoff_brawl.cpp): a move this frame, held guards, the things in reach
    int attack = 0;                                   // Move: 1 jab, 2 haymaker (winds up), 3 grab (again: throw), 4 shove, 5 throw what you hold
    bool block = false, dodge = false, pickUp = false, smash = false, feedDog = false, grabGun = false;   // darts: where it landed (mm); pool: the cue ball (m) or the shot's angle in .x; golf: angle in .x
};
struct Talk {                                         // the conversation mini-game (doc p. 10, p. 40)
    int patron = -1; int exchanges = 0, wins = 0, losses = 0, target = 4;
    std::string theirLine, myCaption, result; bool over = false; float overT = 0, listenT = 0;
    int lastOption = -1; bool lastWin = false, substituted = false;
};struct Moment { float t; int kind; std::string text; };   // the night's log (the morning screen's story; kind 5 is a story worth points)
struct Flirt {                                        // the flirt mini-game (doc p. 13): open, build, the offer
    int patron = -1, round = 0, wins = 0, losses = 0, need = 3, lastOption = -1;
    std::string theirLine, myCaption, tell, result; bool over = false, offer = false, substituted = false, lastWin = false; float overT = 0;
};
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
    Talk talk; GameSeat game; Combat fight; Flirt flirt; bool barred = false; std::vector<std::string> items, known;
    int gamesWon = 0, fightsWon = 0, fightsWonSober = 0, eventsSurvived = 0;   // (the morning's scoreboard)
    std::string homeWith, homeKind, card; bool homeBad = false; float leavingT = 0; int leavingWith = -1;   // going home: with whom, how it went; a bad night's 30 s at the door
    bool fortuneAsked = false;   // items given; secrets learned (patron names whose secret you know)
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
    float drunk = 0, drinkT = 0; int talkingTo = -1, playing = -1;   // playing: a game with that player
    Combat fight; bool outForNight = false;           // (thrown through a window: out for the night)
    std::string home = "sincere"; uint8_t friendOf = 0;   // where going home ends up; players they'll back in a fight (a declined offer)
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
    // the bar games (nightoff_games.cpp)
    bool bartenderDarts = false;                      // (he plays one game a night)
    int NearGame(const Player& p, int* machine = nullptr) const;   // the game station within reach (GK_*), or -1
    std::vector<int> Challengers(const Player& p, int kind) const; // patrons who'd play you: the named ones first
    bool StartGame(Player& p, int kind, int machine, int opponent, int stake, std::string* why = nullptr);
    void GameAction(Player& p);                       // applies p.in.gameAct
    void StepGames(float dt);
    void EndGame(Player& p);
    Vector2 GameSpot(int kind, int machine) const;    // where the opponent stands
    // fights, weapons, mess and damage (nightoff_brawl.cpp)
    std::vector<Prop> props; std::vector<Brawl> brawls; std::vector<Pop> pops; Dog dog;
    float policeT = -1, policeInT = 0, damage = 0;    // police due in (s; -1 none); police in the bar (s); the night's damage
    void InitProps();
    void StepBrawls(float dt);
    Combat* CombatOf(Who w);
    Vector2* PosOf(Who w);
    float* YawOf(Who w);
    std::string NameOf(Who w) const;
    float DrunkOf(Who w) const;
    float ToughOf(Who w) const;
    bool Present(Who w) const;                        // in the bar, standing or down
    int StartBrawl(Who a, Who b, Who starter);        // a fight between a and b (joins one already going)
    void Attack(Who w, int move);
    void Strike(Who att, Who def, float dmg, int weapon, bool haymaker);
    void BreakProp(int prop, Who by, int brawl, const char* how = nullptr);
    int NearestProp(Vector2 at, float range, bool weaponsOnly) const;
    void PlayerFightInput(Player& p, float dt);
    void EndBrawl(Brawl& b);
    void AddPop(Vector2 at, float y, const std::string& text, Color c);
    float AfterFightCharisma(const Player& p, const Patron& c) const;   // the after-fight swing with this patron
    // flirting, going home, the morning (nightoff_flirt.cpp)
    std::vector<std::pair<std::string, std::string>> flags;   // the night's headline moments: (key, who)
    void Flag(const std::string& key, const std::string& who = "");
    void StartFlirt(Player& p, int patron);
    void FlirtChoose(Player& p, int option);
    void FlirtOffer(Player& p, bool take);
    void EndFlirt(Player& p);
    void GoHome(Player& p, int patron, const std::string& kind);
    void AskTrouble(Player& p);
    float FlirtOdds(const Player& p, const Patron& c, int option) const;   // the chance a line lands (0..1)
    struct ScoreLine { std::string what; int points; };
    std::vector<ScoreLine> ScoreBreakdown(const Player& p) const;
    std::vector<std::string> MorningStory(const Player& p) const;
    std::string Headline() const;
    std::string MorningLine(const Player& p) const;
};

int RunNightTest();   // --night-test
int RunPatronCheck(); // --patron-check (doc p. 30)

} // namespace no
