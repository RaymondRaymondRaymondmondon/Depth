// ============================================================================
//  DEPTH - Flats Duel (Deep Arcade game 1): every number, list and name the duel uses.
//
//  The rules engine (flats_duel.h) reads only this file for its numbers, so balance work never touches the rules.
//  Everything here also goes into the arcade's DataHash (HashDuelData), so two copies that disagree on a number
//  refuse each other at the handshake.
//  No raylib here.
// ============================================================================
#pragma once
#include "flats_board.h"
#include "net_msg.h"
#include <cstdint>
#include <vector>

namespace flats {
namespace duel {

// ---------------------------------------------------------------- modes
enum Mode : uint8_t { MODE_DRAFT, MODE_CONSTRUCTED, MODE_QUICK, MODE_COUNT };
struct ModeRules {
    const char* name;
    const char* pitch;          // one line for the setup screen
    float turnSeconds;          // the whole turn: upkeep, plays and the bell
    int gamesToWin;             // 2 = best of three, 1 = a single game
    bool sideboard;             // swaps between games
};
const ModeRules& RulesOf(int mode);

// ---------------------------------------------------------------- tuning
// The defaults are the shipped values. --flats-duel-sim can override them to sweep (see Tune()).
struct Tuning {
    // opening and second-player balance. The Master Reference asks for "one extra bone and a Minnow in hand" and a
    // first-player win rate of 48-52%. Measured by the kit's duel_check (3000 bot matches a line): with no bonus the
    // first player wins 55-57%; the spec's bone + Minnow overshoots to 43-44% (the Minnow alone is worth ~11 points).
    // Shipped: +1 bone, and the scales start tipped toward the second player: 1 notch in Draft, 2 with the stronger
    // preset/constructed decks (which give the first player more tempo). Result: Draft 50.6%, Constructed 50.2%, Quick 50.9%.
    int openingDraw = 3;            // cards from the deck in the opening hand (plus one Minnow, as in single-player)
    int secondBones = 1;            // the player going second starts with this many bones
    int secondMinnows = 0;          // ... and this many extra Minnows in hand (the spec's 1 overshoots: see above)
    bool firstSkipsDraw = false;    // the first player's first upkeep draws nothing (costs the first player ~10 points)
    bool firstHoldsFire = false;    // the first player's first turn has no combat (costs ~7 points)
    int secondScaleStart[MODE_COUNT] = {1, 2, 2};   // the scales start this many notches toward the second player (~3-4 points each), per mode
    // how a game ends
    int roundLimit = 20;            // after this many rounds the scales decide; level scales: sudden death
    int hardLimit = 30;             // sudden death still level after this many rounds: the game is tied and replayed
    int maxGames = 5;               // a match with this many games (ties included) and no winner is drawn
    // rule changes for two humans
    int undyingCap = 2;             // Undying copies that may return to one player's hand per game
    int itemsPerGame = 2;           // pack items each player brings to a game (chosen at setup). A Scavenger find is extra,
                                    // up to the pack's room (MAX_ITEMS = 3); there is no cap on using what you hold.
    // draft (an open "Rochester" draft: both players see every pack and every pick)
    int draftPacks = 13;            // 13 packs of 4, two picks each per pack: 26 cards each
    int packSize = 4;
    int deckFromDraft = 20;         // keep 20 in the deck; the other 6 are the sideboard
    int draftFoilPct = 8, draftHexPct = 4;   // editions in draft packs (never Gilt: a duel has no pot)
    // decks
    int deckMin = 20, deckMax = 30;
    int sideboardMax = 6;
    int swapsBetweenGames = 3;
    int maxCopies = 3;              // copies of one card in a constructed deck (Minnows free)
    int maxTier3 = 2;               // copies of any one tier-3 card
    // pacing (host clock; the sims ignore it)
    float pickSeconds = 12;
    float buildSeconds = 75;
    float sideboardSeconds = 45;
    float stepSeconds = 0.42f;      // one micro-step of combat per this long, so both screens can show every strike
    float aiThink = 0.9f;           // an AI seat waits this long before each decision
    float gameOverPause = 4.0f;     // the "game won" panel stays this long before the sideboard or the next game
    float emoteCooldown = 6.0f;
};
Tuning& Tune();                     // mutable only by the sims; never changed during a networked match

// ---------------------------------------------------------------- the card pool
bool Banned(int cardId);            // Massive, Tidal Pull, and every tier-0 card except the item-made ones
bool Draftable(int cardId);         // tiers 1-3, not banned
int DraftWeight(int tier);          // how often each tier turns up in a draft pack
bool ValidateDeck(const std::vector<Card>& deck, const std::vector<Card>& side, std::string* why);

// Six preset decks for Quick mode (and the AI's constructed deck).
struct PresetCard { const char* name; int count; };
struct Preset {
    const char* name;
    const char* pitch;
    std::vector<PresetCard> cards;  // 20 cards; the engine adds 2 Minnows and a Ballast Cask as for every duel deck
    int items[2];                   // PackItem
    int skin;                       // the dealer skin the AI wears with it
};
const std::vector<Preset>& Presets();
std::vector<Card> PresetDeck(int preset);
std::vector<Card> WithStaples(std::vector<Card> deck);   // + 2 Minnows + a Ballast Cask

// ---------------------------------------------------------------- dealer skins (the opponent across the table)
enum Hat : uint8_t { HAT_HOOD, HAT_SHAWL, HAT_TRICORN, HAT_TOPHAT, HAT_CROWN, HAT_CAP, HAT_HELMET, HAT_EARS, HAT_COUNT };
enum Hands : uint8_t { HANDS_FUMBLE, HANDS_WET, HANDS_RINGED, HANDS_MECHANICAL, HANDS_BONE, HANDS_GLOVED, HANDS_PAWS, HANDS_COUNT };
enum Face : uint8_t { FACE_WAX, FACE_WEATHERED, FACE_SKULL, FACE_BRASS, FACE_CAT, FACE_COUNT };
struct Rgb { uint8_t r, g, b; };
struct Skin {
    const char* name;
    const char* unlock;             // how it is had ("free", "clear the Sovereign", or a token price)
    int tokens;                     // arcade tokens to buy it (0: free)
    Hat hat; Hands hands; Face face;
    Rgb cloak, cloakLit, trim, skin, eyes, gem;
    float voicePitch;               // formant mumble for its lines (Hz of the voice's root)
    float voiceRate;                // syllables a second
    const char* tell;               // what it does while it thinks (a visual-only fidget: there are no hidden rules in a duel)
    const char* greet;              // the start of each game
    const char* ahead;              // the scales favour it by 4+
    const char* behind;             // the scales are against it by 4+
    const char* bigHit;             // one of its creatures took 4+ in a blow
    const char* wins;               // it took the game
    const char* loses;              // it lost the game
};
int SkinCount();
const Skin& SkinOf(int i);

// ---------------------------------------------------------------- emotes (played by the sender's figure)
enum Emote : uint8_t { EM_NOD, EM_SNEER, EM_SHRUG, EM_TIP_HAT, EM_SLOW_CLAP, EM_GULP, EM_COUNT };
struct EmoteInfo { const char* name; const char* cue; float seconds; };
const EmoteInfo& EmoteOf(int e);

// ---------------------------------------------------------------- arcade tokens (Master Reference: cosmetics only)
constexpr int TOKENS_PER_MATCH = 10, TOKENS_PER_WIN = 25;

// Every rule above, for the arcade's DataHash.
void HashDuelData(Writer& w);

}  // namespace duel
}  // namespace flats
