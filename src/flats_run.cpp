#include "flats_run.h"
#include <algorithm>

namespace flats {

const char* NodeName(NodeType t) {
    static const char* n[(int)NodeType::COUNT] = {"Battle", "Elite battle", "Choose a card", "Campfire", "Splice", "Sacrifice", "Trial", "The stall", "Cache", "The House"};
    return n[(int)t];
}
const char* NodeText(NodeType t) {
    static const char* n[(int)NodeType::COUNT] = {
        "A dealer at the table. Win to bank the pot.", "A harder dealer with a totem. Pays half again, and gives a rare card.",
        "Choose one card from three.", "Pick a card: +1 strength or +1 defense.", "Pick two cards: one is destroyed, the other inherits its sigils.",
        "Remove a card from your deck. Every battle starts with an extra bone.", "A card gives up 2 defense for 3 strength, for good.",
        "Spend the pot on cards, charms, items and insurance.", "A free item for the pack.", "The last table. Beat the House to clear the run."};
    return n[(int)t];
}

std::vector<Card> RunManager::StartingDeck() {
    // twelve cards: cheap and simple, enough blood and bones to learn the rules on
    const char* names[] = {"Hermit Crab", "Flying Fish", "Anglerfish", "Pufferfish", "Sea Urchin", "Clownfish", "Fry", "Ship's Cat", "Ballast Cask", "Salvage Diver", "Sailfish", "Skeleton Sailor"};
    std::vector<Card> d;
    for (const char* n : names) d.push_back(MakeCard(CardIdByName(n)));
    return d;
}

void RunManager::NewRun(unsigned seed) {
    rng.Seed(seed);
    gs = GameState();
    gs.deck = StartingDeck();
    layer = slot = -1;
    map.assign(MAP_LAYERS, {});
    // layer widths: two or three choices, then a single boss
    for (int l = 0; l < MAP_LAYERS; l++) {
        int w = l == MAP_LAYERS - 1 ? 1 : (l == 0 ? 2 : rng.I(2, 3));
        map[l].assign(w, MapNode());
    }
    for (int l = 0; l < MAP_LAYERS; l++) {
        int dealer = l < 2 ? 0 : l < 4 ? 1 : l < 7 ? 2 : 3; // a dealer per stretch of the map; the last layers before the House are Broker territory
        for (int s = 0; s < (int)map[l].size(); s++) {
            MapNode& n = map[l][s];
            n.dealer = dealer;
            if (l == MAP_LAYERS - 1) { n.type = NodeType::BOSS; n.dealer = 3; continue; }
            if (l == 0) { n.type = s == 0 ? NodeType::BATTLE : (rng.C(0.5f) ? NodeType::CARD_PICK : NodeType::CACHE); continue; }
            if (l == MAP_LAYERS - 2) { n.type = s == 0 ? NodeType::CAMPFIRE : (rng.C(0.6f) ? NodeType::STALL : NodeType::TRIAL); continue; } // a breath before the House
            // weighted draw of a node type
            struct W { NodeType t; int w; };
            std::vector<W> ws = {{NodeType::BATTLE, 36}, {NodeType::CARD_PICK, 12}, {NodeType::CAMPFIRE, 10}, {NodeType::SPLICE, 7}, {NodeType::SACRIFICE, 7},
                                 {NodeType::TRIAL, 7}, {NodeType::STALL, 9}, {NodeType::CACHE, 7}};
            if (l >= 3) ws.push_back({NodeType::ELITE, 12});
            int total = 0;
            for (auto& x : ws) total += x.w;
            int roll = rng.I(1, total);
            for (auto& x : ws) { if (roll <= x.w) { n.type = x.t; break; } roll -= x.w; }
        }
        // every layer offers at least one battle-free path and (except the ones above) at least one battle
        bool anyBattle = false;
        for (auto& n : map[l]) anyBattle |= n.type == NodeType::BATTLE || n.type == NodeType::ELITE || n.type == NodeType::BOSS;
        if (!anyBattle && l > 0 && l < MAP_LAYERS - 2) map[l][rng.I(0, (int)map[l].size() - 1)].type = NodeType::BATTLE;
    }
    // edges: each node joins the proportionally nearest node(s) of the next layer; every node of the next layer gets at least one way in
    for (int l = 0; l + 1 < MAP_LAYERS; l++) {
        int w0 = (int)map[l].size(), w1 = (int)map[l + 1].size();
        std::vector<bool> reached(w1, false);
        for (int s = 0; s < w0; s++) {
            int centre = w0 == 1 ? 0 : (int)((float)s * (w1 - 1) / (w0 - 1) + 0.5f);
            map[l][s].next.push_back(centre);
            reached[centre] = true;
            if (w1 > 1 && rng.C(0.5f)) { int o = std::clamp(centre + (rng.C(0.5f) ? 1 : -1), 0, w1 - 1); if (o != centre) { map[l][s].next.push_back(o); reached[o] = true; } }
        }
        for (int t = 0; t < w1; t++)
            if (!reached[t]) { int s = std::min(w0 - 1, (int)((float)t * (w0 - 1) / std::max(1, w1 - 1) + 0.5f)); map[l][s].next.push_back(t); }
    }
}

std::vector<int> RunManager::Reachable() const {
    if (layer < 0) { std::vector<int> all; for (int i = 0; i < (int)map[0].size(); i++) all.push_back(i); return all; }
    if (layer >= MAP_LAYERS - 1) return {};
    return map[layer][slot].next;
}

bool RunManager::Enter(int nextSlot) {
    auto r = Reachable();
    if (std::find(r.begin(), r.end(), nextSlot) == r.end()) return false;
    layer++;
    slot = nextSlot;
    return true;
}

BattleSetup RunManager::MakeBattle(Boon boon) const {
    const MapNode& n = map[layer][slot];
    BattleSetup s;
    s.deck = gs.deck; s.charms = gs.charms; s.items = gs.items; s.startBones = gs.startBones;
    s.dealer = n.dealer; s.elite = n.type == NodeType::ELITE; s.boon = boon;
    return s;
}

int RunManager::PayoutFor(const MapNode& n) const {
    static const int base[DEALERS] = {20, 40, 70, 250};
    int p = base[std::clamp(n.dealer, 0, DEALERS - 1)];
    if (n.type == NodeType::ELITE) p = p * 3 / 2;
    return p;
}

int RunManager::OnBattleFinished(bool won, int turns, int goldFromBoard, int itemsFound) {
    int gained = 0;
    if (won) {
        gained = PayoutFor(map[layer][slot]) + goldFromBoard + (gs.HasCharm(CH_JAR) ? 10 : 0);
        gs.pot += gained;
        gs.battlesWon++;
        if (turns < MOMENTUM_TURNS) gs.momentumTracker = std::min(MAX_MOMENTUM, gs.momentumTracker + 1);
        map[layer][slot].visited = true;
    }
    for (int i = 0; i < itemsFound; i++) gs.AddItem(RandomItem());
    return gained;
}

bool RunManager::SpendMomentum() {
    if (gs.momentumTracker <= 0) return false;
    gs.momentumTracker--;
    return true;
}

std::vector<Card> RunManager::OfferCards(int count, bool rare) {
    std::vector<Card> out;
    for (int i = 0; i < count; i++) {
        Card c = RandomPlayerCard(std::min(3, Tier() + (rare ? 1 : 0)), rng);
        if (rare && c.edition == ED_NONE && rng.C(0.5f)) c.edition = rng.I(ED_FOIL, ED_COUNT - 1);
        out.push_back(c);
    }
    return out;
}

bool RunManager::Campfire(int i, bool strength) {
    if (i < 0 || i >= (int)gs.deck.size()) return false;
    if (strength) gs.deck[i].strength++; else gs.deck[i].defense++;
    gs.deck[i].ResetForBattle();
    return true;
}
bool RunManager::Splice(int keep, int consume) {
    if (keep == consume || keep < 0 || consume < 0 || keep >= (int)gs.deck.size() || consume >= (int)gs.deck.size()) return false;
    Card k = gs.deck[keep];
    for (Sigil s : gs.deck[consume].sigils) k.AddSigil(s);
    gs.deck[keep] = k;
    gs.deck.erase(gs.deck.begin() + consume);
    return true;
}
bool RunManager::SacrificeCard(int i) {
    if (i < 0 || i >= (int)gs.deck.size() || gs.deck.size() <= 8) return false;
    gs.deck.erase(gs.deck.begin() + i);
    gs.startBones++;
    return true;
}
bool RunManager::Trial(int i) {
    if (i < 0 || i >= (int)gs.deck.size() || gs.deck[i].defense <= 2) return false; // it must be able to survive the trial
    gs.deck[i].defense -= 2;
    gs.deck[i].strength += 3;
    gs.deck[i].ResetForBattle();
    return true;
}
int RunManager::RandomItem() { return rng.I(0, (int)PackItem::COUNT - 1); }

}  // namespace flats
