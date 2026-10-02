#pragma once
// Skins for the Trawl's crew and Red Tide's divers (the user's call, 2026-10-02): characters only, the silhouette never
// changes (a skin is a recolour of the figure's own materials: top, trousers, hat or helmet, trim). Per game: 15 bought
// outright with tokens, and 65 from crates (25 common, 20 rare, 15 super rare, 5 legendary). Crates are earned at play
// milestones and bought with tokens. A crate that rolls a skin you already own is a failed roll: nothing, the crate
// spent. Red Tide's existing Locker (suits, helmets, finishes) and Salt Charms stay as they are beside these.
// Headless; saved to skins_save.txt next to the exe. Red Tide pays from its arcade profile's tokens; the Trawl has
// its own wallet here (its run tokens, credited at each met deadline).
#include "raylib.h"
#include <string>
#include <vector>

namespace skins {

enum Game { TRAWL = 0, REDTIDE = 1, GAME_COUNT };
enum Rarity { STORE = 0, COMMON, RARE, SUPER, LEGEND, RARITY_COUNT };
const char* RarityName(int r);
Color RarityColor(int r);

struct Skin {
    const char* id; const char* name; int rarity;
    Color top, trousers, hat, trim;     // the suit or oilskins, the legs, the helmet or hat, the trim (Red Tide's corselet, a Trawl apron)
    int price;                          // store skins only
    const char* note;
};
const std::vector<Skin>& Catalogue(int game);
const Skin* Find(int game, const std::string& id);

const int CRATE_PRICE = 150;            // tokens for a crate
const int RARITY_WEIGHT[RARITY_COUNT] = {0, 60, 27, 10, 3};   // percent: common, rare, super rare, legendary

struct Wardrobe {
    std::vector<std::string> owned;     // skin ids
    std::string worn;                   // "" = as issued
    int crates = 0;                     // unopened
    int tokens = 0;                     // the Trawl's wallet (Red Tide uses its profile's)
    bool Owns(const std::string& id) const;
};
Wardrobe& Get(int game);
void Load();
void Save();
extern bool gNoSave;                    // (tests: nothing touches the player's file)

int Tokens(int game);
bool Spend(int game, int n);
void AddTokens(int game, int n);        // the Trawl's wallet (Red Tide's go to its profile)
void AwardCrates(int game, int n);      // a milestone
bool BuyCrate(int game, std::string* why = nullptr);
bool BuySkin(int game, const std::string& id, std::string* why = nullptr);
bool Wear(int game, const std::string& id);   // "" takes it off
struct Roll { bool ok = false; int rarity = -1; std::string id; bool duplicate = false; };
Roll OpenCrate(int game, uint32_t seed);      // spends a crate; a duplicate gives nothing
// the colours a worn skin puts on the figure's materials (name, colour); empty as issued
struct Tint { const char* material; Color c; };
std::vector<Tint> WornColours(int game);

int RunSkinsTest();                     // depth.exe --skins-test
bool WardrobePage(int game);            // the page (skins_ui.cpp); true when Back is pressed

} // namespace skins
