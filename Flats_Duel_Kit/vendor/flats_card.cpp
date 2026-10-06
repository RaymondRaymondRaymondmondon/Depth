#include "flats_card.h"
#include <algorithm>

namespace flats {

static const SigilInfo SIGILS[(int)Sigil::COUNT] = {
    {"", ""},
    {"Airborne", "Flies over the creature in front and strikes the scales directly. A Mighty Leap creature blocks it."},
    {"Twin Tide", "Strikes the two neighbouring lanes instead of the one ahead."},
    {"Tidecaller", "Creatures beside it gain +1 strength."},
    {"Sharp Quills", "Whatever strikes it takes 1 damage."},
    {"Brine", "The creature across from it loses 1 strength."},
    {"Venom", "Anything it damages dies."},
    {"Sentinel", "Steps in front of an enemy that arrives across an empty lane."},
    {"Burrower", "When an empty lane is struck, slides in to take the blow."},
    {"Undying", "When it dies, a copy returns to your hand."},
    {"Ballast", "Counts as three blood when sacrificed."},
    {"Many Lives", "Survives being sacrificed."},
    {"Spawn", "When played, a copy of it is added to your hand."},
    {"Heavy Current", "Each turn it shoves sideways, pushing lighter creatures ahead of it."},
    {"Swimmer", "Drifts one lane each turn, turning about at a wall or a blocker."},
    {"Bone King", "Leaves four bones when it dies."},
    {"Scavenger", "When played, you find an item if your pack has room."},
    {"Fledgling", "Grows into something bigger after a turn on the board."},
    {"Repulsive", "Creatures will not strike it."},
    {"Waterborne", "Submerges on the enemy's turn: their blows pass over it and land on the scales."},
    {"Phalanx", "If an adjacent Phalanx creature is struck, this one takes the blow instead."},
    {"Foresight", "Steps out of the way of attacks. Each turn's end it heals every Phalanx creature by 1."},
    {"Mighty Leap", "Blocks Airborne creatures: they cannot fly over its line."},
    {"Massive", "Fills all four lanes and cannot be moved. Every attack in any lane strikes it."},
    {"Tidal Pull", "At the end of its turn, drags every enemy creature one lane sideways. The edge is fatal."},
};
const SigilInfo& InfoOf(Sigil s) { return SIGILS[(int)s]; }
const char* SuitName(int suit) { static const char* n[SUITS] = {"Coin", "Cup", "Blade", "Shell"}; return n[suit]; }
const char* EditionName(int ed) { static const char* n[ED_COUNT] = {"", "Foil", "Gilt", "Hex"}; return n[ed]; }
const char* EditionText(int ed) {
    static const char* t[ED_COUNT] = {"", "+1 strength.", "Slaying a card pays 4 gold into the pot.", "+2 strength, but 1 less defense."};
    return t[ed];
}

bool Card::Has(Sigil s) const { return std::find(sigils.begin(), sigils.end(), s) != sigils.end(); }
bool Card::AddSigil(Sigil s) {
    if (s == Sigil::NONE || Has(s) || (int)sigils.size() >= MAX_SIGILS) return false;
    sigils.push_back(s);
    return true;
}
int Card::Rating() const {
    int r = strength * 2 + defense + weight / 2;
    for (Sigil s : sigils) {
        switch (s) {
            case Sigil::VENOM: case Sigil::SKIMMER: case Sigil::TWIN_TIDE: case Sigil::TIDECALLER: r += 3; break;
            case Sigil::UNDYING: case Sigil::SPINES: case Sigil::SENTINEL: case Sigil::BONE_KING: r += 2; break;
            default: r += 1; break;
        }
    }
    if (edition == ED_FOIL) r += 2;
    if (edition == ED_HEX) r += 1;
    return r;
}
std::string Card::CostText() const {
    if (cost == CostType::FREE || costAmount <= 0) return "Free";
    return std::to_string(costAmount) + (cost == CostType::BLOOD ? " blood" : " bones");
}
void Card::ResetForBattle() {
    hp = std::max(1, defense - (edition == ED_HEX ? 1 : 0));
    age = 0;
    dir = 1;
}

// ---------------------------------------------------------------- the catalogue
namespace {
struct Def {
    const char* name; int suit, str, def, wt; CostType cost; int amt; std::vector<Sigil> sig; int tier; const char* evolve;
};
const CostType F = CostType::FREE, BL = CostType::BLOOD, BN = CostType::BONES;
using S = Sigil;

std::vector<Card> Build() {
    // name, suit, strength, defense, weight, cost, amount, sigils, tier, evolves into
    const std::vector<Def> defs = {
        {"Minnow", COIN, 0, 1, 1, F, 0, {}, 0, nullptr},
        // ---- tier 1: cheap and simple, the bones and blood you start with
        {"Hermit Crab", SHELL, 1, 3, 3, BN, 2, {S::BURROWER}, 1, nullptr},
        {"Flying Fish", CUP, 1, 2, 1, BL, 1, {S::SKIMMER}, 1, nullptr},
        {"Anglerfish", CUP, 2, 2, 2, BL, 1, {S::WATERBORNE}, 1, nullptr},
        {"Pufferfish", CUP, 1, 2, 1, BN, 2, {S::SPINES}, 1, nullptr},
        {"Sea Urchin", BLADE, 0, 3, 3, BN, 2, {S::SPINES}, 1, nullptr},
        {"Clownfish", COIN, 1, 1, 1, BL, 1, {S::SPAWN}, 1, nullptr},
        {"Fry", COIN, 1, 2, 1, BL, 1, {S::FRY}, 1, "Barracuda"},
        {"Ship's Cat", SHELL, 1, 2, 1, BL, 1, {S::NINE_LIVES}, 1, nullptr},
        {"Ballast Cask", SHELL, 0, 2, 4, BN, 1, {S::BALLAST}, 1, nullptr},
        {"Salvage Diver", COIN, 2, 2, 2, BL, 1, {S::SCAVENGER}, 1, nullptr},
        {"Mudskipper", BLADE, 1, 2, 1, BN, 2, {S::BURROWER}, 1, nullptr},
        {"Sailfish", BLADE, 2, 2, 2, BL, 1, {S::SKIMMER}, 1, nullptr},
        {"Skeleton Sailor", BLADE, 1, 2, 2, BL, 1, {S::BONE_KING}, 1, nullptr},
        // ---- tier 2
        {"Stingray", BLADE, 1, 2, 2, BL, 2, {S::VENOM, S::WATERBORNE}, 2, nullptr},
        {"Manta Ray", CUP, 2, 3, 3, BL, 2, {S::TWIN_TIDE}, 2, nullptr},
        {"Sea Turtle", SHELL, 1, 5, 4, BL, 2, {S::SPINES}, 2, nullptr},
        {"Swordfish", BLADE, 2, 2, 2, BL, 2, {S::SKIMMER, S::SPINES}, 2, nullptr},
        {"Squid", CUP, 1, 2, 2, BL, 1, {S::WATERBORNE, S::BRINE}, 2, nullptr},
        {"Coral Queen", SHELL, 1, 3, 2, BL, 2, {S::TIDECALLER}, 2, nullptr},
        {"Crab Sentinel", SHELL, 2, 3, 3, BL, 2, {S::SENTINEL}, 2, nullptr},
        {"Ghost Crab", SHELL, 2, 1, 2, BN, 4, {S::UNDYING}, 2, nullptr},
        {"Sea Anemone", SHELL, 0, 4, 3, BN, 3, {S::REPULSIVE}, 2, nullptr},
        {"Hammerhead", BLADE, 3, 3, 4, BL, 2, {}, 2, nullptr},
        {"Barracuda", BLADE, 3, 3, 3, BL, 2, {}, 2, nullptr},
        {"Moray Eel", BLADE, 3, 2, 1, BL, 2, {S::SKIMMER}, 2, nullptr},
        {"Nautilus", SHELL, 1, 2, 2, BL, 1, {S::FRY}, 2, "Chambered Titan"},
        // ---- tier 3: heavy hitters
        {"Great White", BLADE, 4, 4, 5, BL, 3, {}, 3, nullptr},
        {"Sperm Whale", CUP, 3, 6, 5, BL, 3, {S::HEAVY_CURRENT}, 3, nullptr},
        {"Kraken Spawn", CUP, 1, 3, 3, BL, 2, {S::FRY, S::SPINES}, 3, "Kraken"},
        {"Sea Serpent", BLADE, 4, 3, 3, BL, 2, {S::TWIN_TIDE, S::BRINE}, 3, nullptr},
        {"Drowned King", COIN, 3, 4, 4, BL, 3, {S::BONE_KING, S::UNDYING}, 3, nullptr},
        {"Chambered Titan", SHELL, 3, 5, 4, BL, 3, {S::SPINES}, 3, nullptr},
        // ---- the dealers' own cards (tier 0: never offered as rewards)
        {"Bilge Rat", COIN, 1, 1, 1, F, 0, {}, 0, nullptr},
        {"Deckhand", BLADE, 1, 2, 2, F, 0, {}, 0, nullptr},
        {"Rusted Anchor", SHELL, 0, 4, 5, F, 0, {}, 0, nullptr},
        {"Barnacle Husk", SHELL, 1, 3, 3, F, 0, {S::SPINES}, 0, nullptr},
        {"The Croupier", CUP, 4, 8, 5, F, 0, {S::TWIN_TIDE, S::SPINES}, 0, nullptr},
        {"Boulder", SHELL, 0, 5, 5, F, 0, {}, 0, nullptr},
        {"Black Goat", BLADE, 0, 1, 1, F, 0, {S::BALLAST}, 0, nullptr},
        // ---- what a Kraken Spawn grows into, and the drowned court of Atlantis (cards you can never win)
        {"Kraken", CUP, 5, 8, 6, F, 0, {S::SPINES, S::TIDECALLER}, 0, nullptr},
        {"Atlantean Hoplite", SHELL, 1, 3, 3, F, 0, {S::PHALANX}, 0, nullptr},
        {"Sunken Oracle", CUP, 0, 2, 1, F, 0, {S::FORESIGHT}, 0, nullptr},
        {"Coral Golem", SHELL, 2, 4, 5, F, 0, {S::MIGHTY_LEAP}, 0, nullptr},
        {"Selenis, the Moon God", CUP, 4, 40, 9, F, 0, {S::MASSIVE, S::TIDAL_PULL}, 0, nullptr},
    };
    std::vector<Card> out;
    for (size_t i = 0; i < defs.size(); i++) {
        const Def& d = defs[i];
        Card c;
        c.id = (int)i; c.name = d.name; c.suit = d.suit; c.strength = d.str; c.defense = d.def; c.weight = d.wt;
        c.cost = d.cost; c.costAmount = d.amt; c.sigils = d.sig; c.tier = d.tier;
        c.ResetForBattle();
        out.push_back(c);
    }
    for (size_t i = 0; i < defs.size(); i++) // resolve the evolutions by name, now that every id exists
        if (defs[i].evolve) for (auto& c : out) if (c.name == defs[i].evolve) out[i].evolveId = c.id;
    return out;
}
}  // namespace

const std::vector<Card>& Catalog() { static const std::vector<Card> c = Build(); return c; }
Card MakeCard(int id, int edition) {
    Card c = Catalog()[std::clamp(id, 0, (int)Catalog().size() - 1)];
    c.edition = edition;
    c.ResetForBattle();
    return c;
}
int CardIdByName(const std::string& name) {
    for (auto& c : Catalog()) if (c.name == name) return c.id;
    return 0;
}
Card Minnow() { return MakeCard(0); }
Card RandomPlayerCard(int tier, Rng& rng) {
    // deeper runs favour stronger cards: tier 1 offers mostly tier 1, tier 3 mostly tier 2 and 3
    std::vector<int> pool;
    for (auto& c : Catalog()) {
        if (c.tier < 1 || c.tier > 3) continue;
        int w = c.tier == tier ? 4 : (std::abs(c.tier - tier) == 1 ? 2 : 0);
        for (int k = 0; k < w; k++) pool.push_back(c.id);
    }
    Card c = MakeCard(pool[rng.I(0, (int)pool.size() - 1)]);
    if (rng.C(0.18f + 0.06f * tier)) c.edition = rng.I(ED_FOIL, ED_COUNT - 1);
    c.ResetForBattle();
    return c;
}

}  // namespace flats
