// ============================================================================
//  DEPTH - Flats, the card game: cards, sigils and the catalogue.
//
//  This header is the data half of the engine (no drawing, no raylib): what a card is. A Card has three
//  numbers that fight each other in combat (strength hits, defense absorbs, weight decides who gets shoved
//  aside), a cost paid in blood (sacrificed creatures) or bones (earned from dead ones), and a list of sigils.
//  The board (flats_board.h) plays them; the run (flats_run.h) strings battles together on a map.
// ============================================================================
#pragma once
#include <string>
#include <vector>

namespace flats {

// A tiny deterministic generator, so a simulation is repeatable and the engine needs nothing from the game.
struct Rng {
    unsigned s = 12345u;
    void Seed(unsigned v) { s = v ? v : 1u; }
    unsigned Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int I(int lo, int hi) { return lo + (int)(Next() % (unsigned)(hi - lo + 1)); }   // inclusive
    float F() { return (Next() & 0xffffff) / 16777216.0f; }
    bool C(float p) { return F() < p; }
    template <typename T> void Shuffle(std::vector<T>& v) { for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[I(0, i)]); }
};

enum Suit { COIN, CUP, BLADE, SHELL, SUITS };
enum class CostType { FREE, BLOOD, BONES };   // blood: sacrifice creatures on your side; bones: spend tokens earned from the dead
enum Edition { ED_NONE, ED_FOIL, ED_GILT, ED_HEX, ED_COUNT };

// Persistent abilities. Each one hooks into exactly one moment of the turn (see flats_board.cpp).
enum class Sigil {
    NONE,
    SKIMMER,       // strikes the scales directly, over whatever stands in front of it
    TWIN_TIDE,     // strikes the two neighbouring lanes instead of the one ahead
    TIDECALLER,    // creatures beside it (same row) gain +1 strength
    SPINES,        // whatever strikes it takes 1 damage
    BRINE,         // the creature across from it loses 1 strength
    VENOM,         // anything it damages dies
    SENTINEL,      // steps in front of an enemy that arrives across an empty lane
    BURROWER,      // when an empty lane is struck it slides in to take the blow
    UNDYING,       // when it dies a copy returns to your hand
    BALLAST,       // counts as three blood when sacrificed
    NINE_LIVES,    // survives being sacrificed
    SPAWN,         // when played, a copy of it (without this sigil) is added to your hand
    HEAVY_CURRENT, // each turn it shoves sideways, pushing lighter creatures ahead of it
    SWIMMER,       // drifts one lane each turn, turning about at a wall or a blocker
    BONE_KING,     // leaves four bones when it dies instead of one
    SCAVENGER,     // when played, you find an item (if your pack has room)
    FRY,           // becomes something bigger after a turn on the board
    REPULSIVE,     // creatures will not strike it at all
    COUNT
};
constexpr int MAX_SIGILS = 3;
struct SigilInfo { const char* name; const char* text; };
const SigilInfo& InfoOf(Sigil s);
const char* SuitName(int suit);
const char* EditionName(int ed);
const char* EditionText(int ed);

class Card {
public:
    int id = -1;                          // catalogue index (-1: none)
    std::string name = "?";
    int suit = COIN;
    int strength = 0;                     // damage dealt to the card directly across (or to the scales if the lane is empty)
    int defense = 1;                      // durability: damage it can take before it dies
    int weight = 1;                       // who shoves whom: a heavier mover pushes a lighter card, a lighter mover is crushed
    CostType cost = CostType::FREE;
    int costAmount = 0;
    std::vector<Sigil> sigils;
    int edition = ED_NONE;                // Foil +1 strength, Gilt pays gold when it slays, Hex +2 strength but 1 less defense
    int evolveId = -1;                    // what a FRY grows into
    int tier = 1;                         // 0 dealer-only, 1..3 how strong (rewards scale with depth)
    // --- battle state (reset when the card is put on the board)
    int hp = 1;
    int age = 0;                          // end phases survived on the board
    int dir = 1;                          // drift direction for SWIMMER / HEAVY_CURRENT

    bool Has(Sigil s) const;
    bool AddSigil(Sigil s);               // false if it already has it or is full
    int BloodValue() const { return Has(Sigil::BALLAST) ? 3 : 1; }
    int Rating() const;                   // a rough power score: used by the dealer's deck-building and the reward tiers
    std::string CostText() const;
    void ResetForBattle();                // full health, no age
};

const std::vector<Card>& Catalog();
Card MakeCard(int id, int edition = ED_NONE);
int CardIdByName(const std::string& name);
Card Minnow();                            // the free 0/1 you can always draw instead of a card
Card RandomPlayerCard(int tier, Rng& rng);  // a card you might be offered at that depth (tier 1..3)

}  // namespace flats
