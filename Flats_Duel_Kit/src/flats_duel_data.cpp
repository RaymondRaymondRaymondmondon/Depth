#include "flats_duel_data.h"
#include <algorithm>
#include <map>
#include <string>

namespace flats {
namespace duel {

const ModeRules& RulesOf(int m) {
    static const ModeRules R[MODE_COUNT] = {
        {"Draft", "Open draft: 13 packs of 4, both of you see every pick. Keep 20, sideboard 6. Best of three.", 60, 2, true},
        {"Constructed", "Bring a 20-30 card deck of your own, with a 6-card sideboard. Best of three.", 60, 2, true},
        {"Quick", "Pick one of six preset decks. One game, 30-second turns.", 30, 1, false},
    };
    return R[std::clamp(m, 0, MODE_COUNT - 1)];
}

Tuning& Tune() { static Tuning t; return t; }

// ---------------------------------------------------------------- the pool
bool Banned(int id) {
    const auto& cat = Catalog();
    if (id < 0 || id >= (int)cat.size()) return true;
    const Card& c = cat[id];
    if (c.Has(Sigil::MASSIVE) || c.Has(Sigil::TIDAL_PULL)) return true;
    if (c.tier == 0) return !(c.name == "Minnow" || c.name == "Boulder" || c.name == "Black Goat" || c.name == "Kraken");   // (a Kraken only ever grows from a Kraken Spawn)
    return false;
}
bool Draftable(int id) {
    const auto& cat = Catalog();
    return id >= 0 && id < (int)cat.size() && cat[id].tier >= 1 && cat[id].tier <= 3 && !Banned(id);
}
int DraftWeight(int tier) { static const int W[4] = {0, 6, 4, 2}; return tier >= 1 && tier <= 3 ? W[tier] : 0; }

bool ValidateDeck(const std::vector<Card>& deck, const std::vector<Card>& side, std::string* why) {
    auto no = [&](const std::string& m) { if (why) *why = m; return false; };
    const Tuning& T = Tune();
    if ((int)deck.size() < T.deckMin || (int)deck.size() > T.deckMax) return no("A deck holds " + std::to_string(T.deckMin) + "-" + std::to_string(T.deckMax) + " cards.");
    if ((int)side.size() > T.sideboardMax) return no("A sideboard holds at most " + std::to_string(T.sideboardMax) + " cards.");
    std::map<int, int> n;
    for (const auto* v : {&deck, &side})
        for (const Card& c : *v) {
            if (c.id < 0 || c.id >= (int)Catalog().size()) return no("An unknown card.");
            if (c.name == "Minnow") continue;
            if (!Draftable(c.id)) return no(c.name + " isn't allowed in a duel.");
            if (c.edition == ED_GILT) return no("Gilt cards pay a pot, and a duel has none.");
            n[c.id]++;
        }
    for (auto& [id, k] : n) {
        const Card& c = Catalog()[id];
        int cap = c.tier == 3 ? T.maxTier3 : T.maxCopies;
        if (k > cap) return no("At most " + std::to_string(cap) + " copies of " + c.name + ".");
    }
    return true;
}

// ---------------------------------------------------------------- presets
const std::vector<Preset>& Presets() {
    using P = PackItem;
    static const std::vector<Preset> p = {
        // Every preset carries about two tier-3 finishers: in bot games the side that plays one wins 70-80% of the time,
        // so a deck without them can't keep up (duel_check cards 3000 2 lists every card's number).
        {"Bone", "Cheap bone-cost walls; Skeleton Sailors pay for the Drowned King, the Titan and the Serpent.",
         {{"Skeleton Sailor", 3}, {"Hermit Crab", 2}, {"Pufferfish", 1}, {"Sea Serpent", 1}, {"Sea Urchin", 1}, {"Mudskipper", 2}, {"Ghost Crab", 2},
          {"Sea Anemone", 1}, {"Drowned King", 1}, {"Chambered Titan", 1}, {"Clownfish", 2}, {"Ship's Cat", 1}, {"Salvage Diver", 2}},
         {(int)P::BANDAGE, (int)P::HARPOON}, 0},
        {"Blood", "Spawn fodder, Many Lives and Ballast, then the Great White.",
         {{"Clownfish", 2}, {"Ship's Cat", 2}, {"Ballast Cask", 1}, {"Fry", 2}, {"Barracuda", 1}, {"Hammerhead", 1}, {"Great White", 1},
          {"Moray Eel", 1}, {"Salvage Diver", 2}, {"Stingray", 2}, {"Anglerfish", 1}, {"Flying Fish", 1}, {"Skeleton Sailor", 2}, {"Mudskipper", 1}},
         {(int)P::GOAT, (int)P::HARPOON}, 3},
        {"Airborne", "Sailfish and morays fly over the wall and hit the scales.",
         {{"Sailfish", 3}, {"Moray Eel", 3}, {"Swordfish", 1}, {"Flying Fish", 1}, {"Clownfish", 2}, {"Ship's Cat", 2},
          {"Manta Ray", 2}, {"Sea Serpent", 1}, {"Great White", 1}, {"Stingray", 2}, {"Anglerfish", 1}, {"Salvage Diver", 1}},
         {(int)P::INK, (int)P::GOAT}, 1},
        {"Big Fish", "Grow Fry, a Nautilus and a Kraken Spawn; sacrifice into the Great White, the Whale and the Serpent.",
         {{"Fry", 2}, {"Nautilus", 1}, {"Kraken Spawn", 1}, {"Hammerhead", 2}, {"Great White", 1}, {"Sperm Whale", 1}, {"Barracuda", 1},
          {"Ship's Cat", 2}, {"Clownfish", 2}, {"Salvage Diver", 2}, {"Mudskipper", 1}, {"Sea Serpent", 1}, {"Hermit Crab", 2}, {"Pufferfish", 1}},
         {(int)P::BOULDER, (int)P::BANDAGE}, 2},
        {"Shell Wall", "Sentinels, turtles and ghost crabs hold every lane; the Titan and the Whale close it out.",
         {{"Hermit Crab", 2}, {"Sea Turtle", 2}, {"Crab Sentinel", 2}, {"Coral Queen", 1}, {"Sea Anemone", 1}, {"Ghost Crab", 2},
          {"Chambered Titan", 1}, {"Sperm Whale", 1}, {"Nautilus", 1}, {"Ship's Cat", 2}, {"Clownfish", 2}, {"Hammerhead", 1},
          {"Barracuda", 1}, {"Salvage Diver", 1}},
         {(int)P::GOAT, (int)P::BANDAGE}, 1},
        {"Swarm", "Many small bodies at once; Venom makes them count, the Serpent and the King finish.",
         {{"Clownfish", 3}, {"Fry", 2}, {"Salvage Diver", 2}, {"Skeleton Sailor", 2}, {"Coral Queen", 1}, {"Stingray", 3},
          {"Barracuda", 2}, {"Moray Eel", 1}, {"Hammerhead", 1}, {"Sea Serpent", 1}, {"Drowned King", 1}, {"Ship's Cat", 1}},
         {(int)P::GOAT, (int)P::HARPOON}, 0},
    };
    return p;
}
std::vector<Card> WithStaples(std::vector<Card> deck) {
    deck.push_back(Minnow()); deck.push_back(Minnow());
    deck.push_back(MakeCard(CardIdByName("Ballast Cask")));
    return deck;
}
std::vector<Card> PresetDeck(int i) {
    const Preset& p = Presets()[std::clamp(i, 0, (int)Presets().size() - 1)];
    std::vector<Card> d;
    for (const PresetCard& pc : p.cards) for (int k = 0; k < pc.count; k++) d.push_back(MakeCard(CardIdByName(pc.name)));
    return d;
}

// ---------------------------------------------------------------- skins
int SkinCount() { return 10; }
const Skin& SkinOf(int i) {
    // name, unlock, tokens, hat, hands, face, cloak, cloakLit, trim, skin, eyes, gem, voice pitch, rate, tell, lines...
    static const Skin S[10] = {
        {"The Novice", "free", 0, HAT_CAP, HANDS_FUMBLE, FACE_WEATHERED,
         {58, 70, 62}, {86, 104, 90}, {150, 128, 84}, {196, 160, 128}, {220, 232, 200}, {120, 200, 140}, 170, 5.5f,
         "drops a card, snatches it back up, glances at you",
         "\"Oh! We're starting? Right. Right.\"", "\"Is... is that good? That's good!\"", "\"I had a plan. I did have a plan.\"",
         "\"Ow. Sorry. Not ow. Sorry.\"", "\"I won? Can I keep the bell?\"", "\"Fair. Very fair. Again?\""},
        {"The Tidewife", "free", 0, HAT_SHAWL, HANDS_WET, FACE_WAX,
         {40, 74, 82}, {60, 110, 118}, {190, 196, 204}, {150, 168, 160}, {170, 240, 230}, {200, 220, 236}, 210, 4.0f,
         "wrings the water from her hair, one strand at a time",
         "\"Sit. The tide is patient. I am not.\"", "\"Feel that? The water's rising on your side.\"", "\"Mm. The current turns. It always turns.\"",
         "\"Salt in the wound. How rude.\"", "\"Back to the sea with you.\"", "\"The tide goes out. It comes back.\""},
        {"The Wreck-Broker", "free", 0, HAT_TRICORN, HANDS_RINGED, FACE_WEATHERED,
         {70, 48, 36}, {104, 72, 52}, {196, 156, 72}, {176, 132, 100}, {240, 210, 140}, {220, 80, 60}, 120, 3.2f,
         "taps a ringed finger on the anchor-shaped weight by his cards",
         "\"Everything's salvage, friend. Even you.\"", "\"I'll give you a fair price for what's left.\"", "\"Hm. That'll come off the bill.\"",
         "\"That was an expensive fish.\"", "\"Sold. To the man with the rings.\"", "\"Well. Keep the change.\""},
        {"The House", "free", 0, HAT_TOPHAT, HANDS_MECHANICAL, FACE_WAX,
         {30, 30, 44}, {50, 50, 72}, {210, 180, 100}, {200, 196, 186}, {255, 220, 120}, {90, 200, 220}, 150, 3.6f,
         "the brass hand clicks twice, then settles flat on the felt",
         "\"The house always wins. Prove me wrong.\"", "\"Odds, darling. Always the odds.\"", "\"An anomaly. Noted.\"",
         "\"The house will remember that.\"", "\"As expected.\"", "\"Recalculating.\""},
        {"The Atlantean Sovereign", "clear the Atlantean Sovereign in Flats", 0, HAT_CROWN, HANDS_BONE, FACE_SKULL,
         {40, 60, 70}, {70, 100, 110}, {214, 190, 110}, {150, 170, 150}, {140, 230, 255}, {150, 240, 255}, 90, 2.4f,
         "his court's shadows lean in over his shoulders",
         "\"Kneel. The tide remembers every crown.\"", "\"You drown slowly. It is kinder.\"", "\"Even Atlantis fell once.\"",
         "\"You chip at a statue.\"", "\"Another name for the deep.\"", "\"The moon will rise again.\""},
        {"The Harbourmaster", "60 arcade tokens", 60, HAT_CAP, HANDS_GLOVED, FACE_WEATHERED,
         {36, 52, 96}, {56, 78, 136}, {226, 196, 90}, {200, 150, 120}, {230, 236, 255}, {240, 200, 80}, 135, 4.2f,
         "checks a pocket watch, then the tide chart",
         "\"Papers in order? Then play.\"", "\"You're overdue, captain.\"", "\"Choppy water. Hold steady.\"",
         "\"That'll need a report.\"", "\"Cleared for departure.\"", "\"Docked fair and square.\""},
        {"The Lamplighter", "80 arcade tokens", 80, HAT_HOOD, HANDS_GLOVED, FACE_WAX,
         {64, 40, 30}, {96, 62, 44}, {236, 170, 70}, {210, 176, 140}, {255, 200, 110}, {255, 170, 60}, 180, 3.0f,
         "trims the candle's wick with two licked fingers",
         "\"Mind the flame. It minds you.\"", "\"Your light's guttering.\"", "\"Dark in here, isn't it.\"",
         "\"Burned. Hm.\"", "\"Lights out.\"", "\"You kept the flame. Well done.\""},
        {"Old Ironsides", "120 arcade tokens", 120, HAT_HELMET, HANDS_GLOVED, FACE_BRASS,
         {70, 66, 60}, {104, 98, 88}, {200, 150, 80}, {200, 150, 80}, {120, 230, 220}, {120, 230, 220}, 100, 2.8f,
         "a slow stream of bubbles rises from the helmet valve",
         "\"*hiss* Deep enough for you?\"", "\"*clank* Pressure's on you, topside.\"", "\"*glub* Taking on water.\"",
         "\"*clang* Dented.\"", "\"*hiss* Surface.\"", "\"*glub glub*\""},
        {"The Siren", "150 arcade tokens", 150, HAT_SHAWL, HANDS_WET, FACE_WAX,
         {30, 80, 90}, {50, 130, 140}, {220, 200, 230}, {176, 206, 200}, {255, 170, 230}, {255, 140, 220}, 260, 4.6f,
         "hums a phrase and watches your hand, not your face",
         "\"Come closer. The cards can't hear us.\"", "\"Such a lovely sinking sound.\"", "\"Oh, you're cruel. I like that.\"",
         "\"You'll pay for that. Sweetly.\"", "\"Down you go.\"", "\"I'll sing about you. Briefly.\""},
        {"The Ship's Cat", "200 arcade tokens", 200, HAT_EARS, HANDS_PAWS, FACE_CAT,
         {48, 44, 40}, {80, 72, 64}, {200, 170, 90}, {90, 80, 72}, {200, 240, 120}, {200, 240, 120}, 300, 6.0f,
         "bats one card slightly out of line, then pretends it didn't",
         "\"Mrrp.\"", "\"*purrs loudly*\"", "\"*tail lashes*\"", "\"HSSS.\"", "\"*knocks the bell off the table*\"", "\"*washes a paw, unbothered*\""},
    };
    return S[std::clamp(i, 0, SkinCount() - 1)];
}

const EmoteInfo& EmoteOf(int e) {
    static const EmoteInfo E[EM_COUNT] = {
        {"Nod", "duel.em.nod", 0.9f}, {"Sneer", "duel.em.sneer", 1.1f}, {"Shrug", "duel.em.shrug", 1.2f},
        {"Tip hat", "duel.em.tiphat", 1.3f}, {"Slow clap", "duel.em.clap", 2.2f}, {"Gulp", "duel.em.gulp", 1.0f},
    };
    return E[std::clamp(e, 0, EM_COUNT - 1)];
}

void HashDuelData(Writer& w) {
    const Tuning T;   // the shipped numbers, never a sim's override
    w.U8(T.openingDraw); w.U8(T.secondBones); w.U8(T.secondMinnows); w.U8(T.firstSkipsDraw); w.U8(T.firstHoldsFire); for (int m = 0; m < MODE_COUNT; m++) w.U8(T.secondScaleStart[m]);
    w.U8(T.roundLimit); w.U8(T.hardLimit); w.U8(T.maxGames); w.U8(T.undyingCap); w.U8(T.itemsPerGame);
    w.U8(T.draftPacks); w.U8(T.packSize); w.U8(T.deckFromDraft); w.U8(T.draftFoilPct); w.U8(T.draftHexPct); w.U8(T.deckMin); w.U8(T.deckMax); w.U8(T.sideboardMax);
    w.U8(T.swapsBetweenGames); w.U8(T.maxCopies); w.U8(T.maxTier3);
    w.F32(T.pickSeconds); w.F32(T.buildSeconds); w.F32(T.sideboardSeconds); w.F32(T.stepSeconds);
    for (int m = 0; m < MODE_COUNT; m++) { w.F32(RulesOf(m).turnSeconds); w.U8(RulesOf(m).gamesToWin); w.U8(RulesOf(m).sideboard); }
    for (const Card& c : Catalog()) {   // the catalogue itself: a changed card is a different game
        w.Str(c.name); w.U8(c.strength); w.U8(c.defense); w.U8(c.weight); w.U8((int)c.cost); w.U8(c.costAmount); w.U8(c.tier);
        for (Sigil s : c.sigils) w.U8((int)s);
        w.U8(Banned(c.id));
    }
    for (const Preset& p : Presets()) { w.Str(p.name); for (auto& pc : p.cards) { w.Str(pc.name); w.U8(pc.count); } w.U8(p.items[0]); w.U8(p.items[1]); }
    for (int i = 0; i < SkinCount(); i++) { w.Str(SkinOf(i).name); w.U16(SkinOf(i).tokens); }
}

}  // namespace duel
}  // namespace flats
