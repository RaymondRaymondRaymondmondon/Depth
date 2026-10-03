#pragma once
// The Flight's costumes (stage 8; doc pp. 28-30): Founder costumes from the token shop and the egg crate, and colony
// liveries (a colour and a hat for every bird in the colony). Headless: the catalogue (data/flight/flight_costumes.json),
// the wallet and wardrobe (flight_wardrobe.txt next to the exe), the crate's roll, and the tokens a match pays. The scene
// draws a costume's look on the bird (flight_game.cpp, DrawCostume); a look changes nothing but the silhouette.
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fl {

enum CostumeTier { CT_SHOP = 0, CT_COMMON, CT_RARE, CT_SUPER, CT_SPECIAL, CT_COUNT };
const char* CostumeTierName(int t);
Color CostumeTierColor(int t);

struct CostumeLook {
    std::string hat;                 // a hat kind the scene knows ("" none)
    Color hatC{255, 255, 255, 255};
    bool tinted = false; Color tint{255, 255, 255, 255};   // the whole bird
    bool beakTinted = false; Color beakC{255, 255, 255, 255}; float beak = 1;   // the beak's colour and size
    std::vector<std::string> extras; Color extraC{255, 255, 255, 255};
    float scale = 1;                 // the silhouette (the Roc: 3x, for show only)
    std::string fx, follow, label;   // embers / lightning / blur; gulls / chicks; "best" (the crown's score)
    bool glow = false, waddle = false;
};
struct CostumeDef { std::string id, name, note; int tier = CT_COMMON, price = 0; CostumeLook look; };
struct LiveryColour { std::string id, name; int price = 0; Color c{}; };
struct LiveryHat { std::string id, name, hat; int price = 0; Color c{}; };
struct CostumeData {
    std::vector<CostumeDef> costumes;
    std::vector<LiveryColour> colours;
    std::vector<LiveryHat> hats;
    int perMatch = 10, perScore = 50, first = 5, win = 20, cratePrice = 1;
    int odds[CT_COUNT] = {0, 60, 25, 12, 3};
};
const CostumeData& Costumes();
const CostumeDef* FindCostume(const std::string& id);
const LiveryColour* FindLiveryColour(const std::string& id);
const LiveryHat* FindLiveryHat(const std::string& id);

struct FlightWardrobe {
    int tokens = 0, feathers = 0, crates = 0, bestScore = 0;
    std::vector<std::string> owned;            // costume, livery colour and livery hat ids
    std::string costume, liveryColour, liveryHat;   // worn ("" none)
    uint32_t firsts = 0;                       // bit 0 first kraken kill, 1 first dangerous island held, 2 first tier 4
    bool Owns(const std::string& id) const;
};
FlightWardrobe& Wardrobe();
void LoadWardrobe();
void SaveWardrobe();
extern bool gWardrobeNoSave;                   // (tests)

bool BuyCostume(const std::string& id, std::string* why = nullptr);   // shop costumes and liveries
bool WearCostume(const std::string& id);       // "" takes it off; a livery id wears it as the colony's
bool BuyCrate(std::string* why = nullptr);     // a crate for its price in tokens
struct EggRoll { bool ok = false; int tier = -1; std::string id; bool duplicate = false; };
EggRoll OpenEgg(uint32_t seed);                // spends a crate: a costume, or a feather for a duplicate
// what a match pays the player: 10, 1 per 50 score, 5 for each first done in it (only the first time ever), 20 for a win
int MatchTokens(int score, bool won, uint32_t firstsThisMatch, uint32_t* newFirsts = nullptr);
int AwardMatch(int score, bool won, uint32_t firstsThisMatch);   // adds them to the wallet (and the best score); returns the tokens

int RunFlightCostumeTest();                    // depth.exe --flight-costume-test

}  // namespace fl
