// ============================================================================
//  DEPTH - Flats: the run. A branching map of nodes strung between battles, and what carries over.
//
//  GameState is everything that persists: your deck (cards keep their edits: stronger, spliced, scarred), your
//  trinkets and items, the pot, and Momentum: win a battle fast and you bank a token to spend on a boon at the
//  start of the next one. The RunManager builds the map and applies each node to the GameState. A battle node hands
//  a BattleSetup to the Battle (flats_board.h) and takes the result back through OnBattleFinished.
// ============================================================================
#pragma once
#include "flats_board.h"

namespace flats {

enum class NodeType {
    BATTLE,     // a dealer at the table
    ELITE,      // a harder dealer with a totem effect: pays more and gives a rare card
    CARD_PICK,  // choose one of three cards
    CAMPFIRE,   // pick a card: +1 strength or +1 defense
    SPLICE,     // pick two cards: one is destroyed, the other inherits its sigils
    SACRIFICE,  // remove a card from the deck, and start every battle with an extra bone
    TRIAL,      // a card gives up 2 defense for 3 strength, for good
    STALL,      // the dealer's stall: spend the pot
    CACHE,      // a free item (or, with a full pack, a pack rat: a spare minnow-grade card)
    BOSS,       // the Atlantean Sovereign
    VENTS,      // the Boiling Vents: warm a creature for a bonus, at a rising risk of losing it
    SCRIMSHAW,  // carve a totem: a tribe head and a sigil base, a passive for that tribe
    SPLICERS,   // the Abyssal Splicers: merge two copies of the same card
    COUNT
};
const char* NodeName(NodeType t);
const char* NodeText(NodeType t);

struct MapNode {
    NodeType type = NodeType::BATTLE;
    int dealer = 0;                  // for battles: which dealer
    std::vector<int> next;           // indices into the next layer
    bool visited = false;
};

constexpr int MAP_LAYERS = 9;        // eight of choices, then the boss
constexpr int MOMENTUM_TURNS = 6;    // win a battle in fewer turns than this and you gain momentum
constexpr int MAX_MOMENTUM = 3;

class GameState {
public:
    std::vector<Card> deck;
    unsigned charms = 0;             // bits of Charm
    std::vector<int> items;          // PackItem values, at most MAX_ITEMS
    int momentumTracker = 0;         // banked momentum, spent at the start of a battle
    int startBones = 0;              // grows with every sacrifice node
    int pot = 0;                     // gold on the table: lost if you lose a battle, banked if you cash out
    int battlesWon = 0;
    bool insured = false;
    std::vector<std::pair<int, int>> totems;   // scrimshaw totems (tribe, sigil), at most three
    int Charms() const { int n = 0; for (int i = 0; i < CH_COUNT; i++) n += (charms >> i) & 1u; return n; }
    bool HasCharm(int c) const { return (charms >> c) & 1u; }
    bool AddItem(int kind) { if ((int)items.size() >= MAX_ITEMS) return false; items.push_back(kind); return true; }
};

class RunManager {
public:
    Rng rng;
    GameState gs;
    std::vector<std::vector<MapNode>> map;   // map[layer][slot]
    int layer = -1, slot = -1;               // where you stand (layer -1: not yet on the map)

    void NewRun(unsigned seed);
    std::vector<int> Reachable() const;      // slots you may enter next
    const MapNode& Current() const { return map[layer][slot]; }
    bool Enter(int nextSlot);                // step onto a node of the next layer
    bool Finished() const { return layer >= MAP_LAYERS - 1 && map[layer][slot].visited; }

    // battles
    BattleSetup MakeBattle(Boon boon) const;
    int PayoutFor(const MapNode& n) const;
    // Records the result: pot, momentum, item finds. `turns` is how long it lasted. Returns gold banked by the win.
    int OnBattleFinished(bool won, int turns, int goldFromBoard, int itemsFound);
    Boon SuggestBoon() const { return Boon::EXTRA_DRAW; }
    bool SpendMomentum();                    // -1 momentum, true if there was any

    // the deck-altering nodes
    std::vector<Card> OfferCards(int count, bool rare);
    void TakeCard(const Card& c) { gs.deck.push_back(c); }
    bool Campfire(int deckIdx, bool strength);
    bool Splice(int keepIdx, int consumeIdx);      // destroy one card, the keeper gains its sigils
    bool SacrificeCard(int deckIdx);               // thin the deck, +1 starting bone
    bool Trial(int deckIdx);                       // -2 defense, +3 strength
    bool Vent(int deckIdx, bool strength, int step);   // warm a card: safe at first, then a 25% chance per step of losing it. Returns false if it was destroyed
    bool Merge(int a, int b);                      // two copies of one card fuse into a single, terrifying monstrosity
    bool Carve(int suit, int sigil);               // add a scrimshaw totem
    int  RandomItem();
    int  Tier() const { return layer < 3 ? 1 : layer < 6 ? 2 : 3; }
    static std::vector<Card> StartingDeck();
};

}  // namespace flats
