#pragma once
// ============================================================================
//  A NIGHT OFF - fights, weapons, the room's mess and damage (doc pp. 15-17, 5-6, 19-20), stage 4. The data, a
//  fighter's state (Combat, on players, patrons and the alley dog), the props that can be picked up, thrown, flipped and
//  broken, and a brawl's ledger (who started it, what broke, the bill). The rules are nightoff_brawl.cpp (headless).
// ============================================================================
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

namespace no {

struct WeaponDef { std::string key, name, smashTo, breakTo; float damage = 10, thrown = 0, price = 0; int breaks = 0; bool bleed = false, armed = false, stun = false, shotgun = false; };
struct FightData {
    float hp = 100, reach = 1.35f;
    float jab = 10, jabWind = 0.12f, jabRec = 0.35f, hay = 25, hayWind = 0.8f, hayRec = 0.6f;
    float grabHold = 1.5f, throwDmg = 15, throwSpeed = 6.5f, shoveDmg = 4, shovePush = 3.5f, shoveRec = 0.5f;
    float blockT = 1.2f, blockK = 0.5f, dodgeT = 0.45f, dodgeFall = 60;
    std::vector<WeaponDef> weapons; std::vector<std::pair<std::string, float>> prices;
    float koS = 30; int koBarred = 3; float crowdR = 3, policeArmed = 180, policeShotgun = 60;
    float afterGood = 0.1f, afterBad = -0.2f, afterMin = 5, barPerFight = -10, barPerBreak = -3, barBehind = -15, panPrice = 20;
    int dogFeeds = 3; float dogBite = 15, dogHold = 1.2f;
    int Weapon(const std::string& key) const;
    float Price(const std::string& kind) const;
};
const FightData& FD();

struct Who {                                                    // a fighter: kind 0 a player, 1 a patron, 2 the dog
    int kind = -1, idx = -1;
    bool operator==(const Who& o) const { return kind == o.kind && idx == o.idx; }
    bool operator!=(const Who& o) const { return !(*this == o); }
    bool Valid() const { return kind >= 0; }
};
inline Who PlayerW(int i) { return {0, i}; }
inline Who PatronW(int i) { return {1, i}; }
inline Who DogW() { return {2, 0}; }

enum Move { MV_NONE, MV_JAB, MV_HAYMAKER, MV_GRAB, MV_SHOVE, MV_THROW };
struct Combat {
    int brawl = -1, side = 0; float hp = 100, hpMax = 100;
    int move = MV_NONE; float windT = 0, recT = 0, swingT = 0;    // the move winding up; recovering; the swing's animation (1 -> 0)
    Vector2 swingDir{};
    float blockT = 0, dodgeT = 0, stunT = 0, downT = 0, bleedT = 0, hitT = 0, fallT = 0, smashT = 0;
    Who grabbing{}, grabbedBy{}; float grabT = 0;
    int held = -1;                                             // a prop in hand
    int knockouts = 0; Who foe{}; float aiT = 0;
    Vector2 push{};                                            // a knock (a shove, a throw) dying away
    bool flying = false;                                       // thrown across the room (breaks what it lands in)
    float afterT = 0, afterK = 0;                              // after a fight: charisma with violent/loud patrons vs everyone else
    bool Down() const { return downT > 0; }
    bool Busy() const { return downT > 0 || stunT > 0 || fallT > 0 || grabbedBy.Valid() || smashT > 0; }
};
enum PropState : uint8_t { PS_OK, PS_OVER, PS_BROKEN, PS_HELD, PS_FLYING, PS_GONE };
struct Prop {
    std::string kind;          // chair, stool, table, window, piano, glass, bottle, cue, dart, club, pan, knife, shotgun, mirror
    Vector3 pos{}, vel{}; float yaw = 0, tilt = 0, spin = 0;
    uint8_t state = PS_OK; Who holder{}, thrower{}; int hits = 0, weapon = -1, brawl = -1;
    Vector2 size{0.4f, 0.4f};  // (tables: their top)
    bool Breakable() const;
    bool Pickup() const { return weapon >= 0 && (state == PS_OK || state == PS_OVER); }
};
struct Brawl {
    int id = 0; Who starter{}; float t = 0, quietT = 0; bool over = false, armed = false;
    float bill = 0; std::vector<std::string> broke; std::string room; Vector2 at{}; int kos = 0, size = 0;
};
struct Pop { Vector3 pos{}; std::string text; float t = 0; Color col{255, 255, 255, 255}; };   // a floating "Whack!" over a hit
struct Dog { Vector2 pos{}, vel{}; float yaw = 0, walkPh = 0; int owner = -1, fed[6] = {0, 0, 0, 0, 0, 0}; Combat fight; Who biting{}; float biteT = 0; bool sleeping = true; };

} // namespace no
