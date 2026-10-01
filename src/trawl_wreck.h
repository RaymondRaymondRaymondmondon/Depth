#pragma once
// The Trawl's wrecks (design doc v2, "Diving and salvage", pages 55-57): generated per ground at the start of each
// deadline and kept for its three nights. Headless: the dive scene draws and walks them.
//
// A wreck is a hull cut into decks, and the decks into rooms on a grid of 4 x 3 m cells, joined by doors (along a deck),
// hatches (between decks) and breaches (the hull torn open: the ways in). 30% lie on their side (ladders become floors).
// Rules (the doc's layout rules, page 56): every room within the hardhat's 40 m of hose from an entry (more breaches are
// torn until that holds); an air pocket per five rooms; the salvage budget spread heavier and richer deeper in, 40% of
// its value behind locked doors (a crowbar, or the Diver's Wreck Rat); a squeeze passage in about 30% (one diver, no
// carried salvage through it) shortcutting to the richest room; silt on every floor; residents from the ground's roster.
#include <cstdint>
#include <string>
#include <vector>

namespace tw {

enum class WreckType { Sloop, Whaler, Smuggler, Galleon, Terrace, COUNT };
const char* WreckTypeName(WreckType t);

struct WreckRoom {
    int x = 0, y = 0, w = 1;            // grid cells: x along the hull (4 m each), y the deck from the top (3 m each), w cells wide
    bool locked = false, air = false;   // a locked cabin; an air pocket
    std::string kind;                   // "hold", "cabin", "galley", "magazine", "captain's cabin", "vault", "hall", ...
};
struct WreckLink { int a = -1, b = -1; int kind = 0; };   // 0 door, 1 hatch, 2 squeeze
struct SalvageItem { std::string name; float value = 0, kg = 0; int room = -1; bool relic = false, cursed = false, twoDiver = false, taken = false; };
struct Resident { std::string what; int room = -1; bool awake = false; };

struct Wreck {
    WreckType type = WreckType::Sloop;
    uint32_t seed = 0;
    float depth = 0;                    // m to the wreck's top deck
    bool onSide = false, bell = false;  // lying on her side; reached by the diving bell (deeper than a hardhat goes)
    int gw = 0, gh = 0;                 // the grid
    std::vector<WreckRoom> rooms;
    std::vector<WreckLink> links;
    std::vector<int> entries;           // rooms with a breach to the sea
    std::vector<SalvageItem> salvage;
    std::vector<Resident> residents;
    float x = 0, y = 0;                 // where on the ground's chart (world m)
    float Budget() const;               // the salvage's total value
    float LockedShare() const;          // of it, the share behind locked doors
    int Rooms() const { return (int)rooms.size(); }
};

Wreck GenerateWreck(WreckType t, uint32_t seed, const std::string& ground);
// the doc's checks: every room within 40 m of hose of an entry (a bell wreck: of the bell's entry), an air pocket per
// five rooms, the type's room count, locked cabins and salvage budget, 40% of the value locked (+-10%)
bool CheckWreck(const Wreck& w, std::string* why);
std::vector<float> HoseDistances(const Wreck& w);   // per room: the shortest hose run (m) from any entry
std::string WreckAscii(const Wreck& w);
// a ground's wrecks for a deadline (doc: Lagoon 1-2, Weeds 2, Grotto 3-4, Atlantis 2-3 plus one terrace)
std::vector<Wreck> GroundWrecks(const std::string& ground, uint32_t seed);

int RunTrawlWreck(int argc, char** argv);   // depth.exe --trawl-wreck <seed> [type 0-4|all] [count]

} // namespace tw
