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

const int CRATE_PRICE = 75;             // tokens for a crate
const int RARITY_WEIGHT[RARITY_COUNT] = {0, 60, 27, 10, 3};   // percent: common, rare, super rare, legendary

// Costumes (the user, 2026-10-02, after A Night Off's examples): whole outfits worn over the figure that change how it
// looks (tools/artgen/costumes.py -> assets/shared/costumes/costume_<model>.glb, on the crew rig both games share).
// 20 per game in A Night Off's tiers: 8 common, 6 rare, 4 super rare, 2 special; bought on the Wardrobe's costume rack
// for tokens (kept apart from the skins and their crates). A costume is worn over whatever skin is worn.
struct Costume {
    const char* id; const char* model; const char* name; int tier;   // tier: COMMON, RARE, SUPER, LEGEND (shown "Special")
    int price; Color sleeve;            // sleeve: the first-person sleeves' colour while it's worn
    const char* note;
};
const std::vector<Costume>& Costumes(int game);
const Costume* FindCostume(int game, const std::string& id);
const char* CostumeTierName(int tier);
bool BuyCostume(int game, const std::string& id, std::string* why = nullptr);
bool WearCostume(int game, const std::string& id);   // "" takes it off
const Costume* WornCostume(int game);                // nullptr none
// the Wardrobe's costume preview: each game's figure drawn full screen (the page's panels go over it), set by the game
using PreviewFn = void (*)(int game, const char* model, float t);
extern PreviewFn gPreview[2];
void SetWardrobeTab(int game, int tab, const char* tryOn = nullptr);   // 0 skins, 1 costumes (--shots: tryOn a costume id)

struct Wardrobe {
    std::vector<std::string> owned;     // skin ids and costume ids
    std::string worn;                   // "" = as issued
    std::string costume;                // the costume worn, "" none
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
std::vector<Tint> ColoursOf(int game, const std::string& id);   // any skin's (a teammate's, from the network)

int RunSkinsTest();                     // depth.exe --skins-test
bool WardrobePage(int game);            // the page (skins_ui.cpp); true when Back is pressed
// the gallery shots (skins_gallery_*): each figure's label, collected by a studio as it draws, then printed over it
struct GalleryLabel { Vector2 at; const char* name; const char* rarity; Color c; int price; };
extern std::vector<GalleryLabel> gGallery;
void DrawGallery(int game, int page, bool costumes = false);   // the labels and a page title; clears them

} // namespace skins
