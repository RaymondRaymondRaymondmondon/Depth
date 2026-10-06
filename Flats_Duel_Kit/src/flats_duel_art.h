// ============================================================================
//  DEPTH - Flats Duel: the opponent across the table, in any dealer skin, and the duel's icons.
//
//  Drawn the same way as flats.cpp's DrawDealer (layered inked ellipses, a cold rim light down the left edge, black
//  block shadows, clipped hatching, slivers of light for eyes), so a skinned opponent sits at the same table without
//  looking like a different game. Everything is parameterised by a Skin (flats_duel_data.h: colours, hat, hands, face)
//  and a DealerPose (the posture, the face, the hands), so expressions, emotes and tells are just poses:
//      DealerPose p = IdlePose(t, skin);                      // breathing, blinking, a drifting hand
//      p = Mix(p, ExpressionPose(EX_AHEAD), w);               // the game state: leaning in when ahead, ...
//      p = Add(p, EmotePose(EM_TIP_HAT, u));                  // an emote in flight (u = 0..1 over EmoteOf(e).seconds)
//      DrawDuelDealer(SkinOf(k), p, {640, 602}, 1.0f, t, lookAt);   // {640, 602} and 1.0 match DrawDealer exactly
//  raylib only; no game headers, so the kit's preview tool can render it headless.
// ============================================================================
#pragma once
#include "flats_duel_data.h"
#include "raylib.h"

namespace flats {
namespace duel {

struct DealerPose {
    float bob = 0;                   // breathing, in figure units (y)
    float lean = 0;                  // +1 leans in toward the player (bigger and lower), -1 sits back
    float headX = 0, headY = 0;      // head offset, figure units
    float headTilt = 0;              // radians, + to the viewer's right
    float shoulders = 0;             // +1 a full shrug
    Vector2 handL{0, 0}, handR{0, 0};// offsets from the resting hands, figure units (L = viewer's left)
    float palmsUp = 0;               // 0..1 hands turn up (shrug)
    float hatLift = 0, hatTilt = 0;  // tip of the hat
    float brow = 0;                  // + raised (worry, surprise), - lowered (menace, cunning)
    float browSkew = 0;              // + the viewer's-right brow up (a sneer)
    float mouth = 0;                 // + smile, - frown
    float mouthOpen = 0;             // 0..1
    float mouthSkew = 0;             // + the right corner up
    float eyeOpen = 1;               // 0 closed
    float eyeWide = 0;               // 0..1 surprise
    float gulp = 0;                  // 0..1 the throat bob
    float sweat = 0;                 // 0..1 a bead of sweat on the brow
    float jitter = 0;                // the Novice's fumbling tremble (0..1)
    int holdCard = -1;               // a card back held in the left (0) or right (1) hand
};
DealerPose Add(const DealerPose& a, const DealerPose& b);             // b's offsets on top of a (eyeOpen multiplies)
DealerPose Mix(const DealerPose& a, const DealerPose& b, float w);    // blend toward b by w

enum Expression : uint8_t { EX_NEUTRAL, EX_AHEAD, EX_BEHIND, EX_FLINCH, EX_WIN, EX_LOSE, EX_COUNT };
DealerPose IdlePose(float t, int skin);           // breathing, a blink every few seconds, hands resting
DealerPose ExpressionPose(int ex);                // a held expression (blend it in over ~0.3 s)
DealerPose EmotePose(int emote, float u);         // an emote's offset at u = 0..1 (0 and 1 are rest)
DealerPose TellPose(int skin, float u);           // the skin's thinking fidget (1.6 s: u = 0..1)
// A hand reaches out and lays a card at 	arget (a screen point: CellCenter of the lane it plays to), u = 0..1 over
// about 0.7 s: out, a little slap at u ~0.45 (fire the card's own flight there), back. The nearer hand reaches.
DealerPose ReachPose(Vector2 target, float u, Vector2 anchor, float scale);
const char* ExpressionName(int ex);

// anchor: DrawDealer's centre and base line ({640, 602} at scale 1); look: a screen point the eyes follow (the cursor).
// layer: flats.cpp draws DrawTable() over the dealer, which hides his hands; draw DL_BODY before the table and DL_HANDS
// after it, so the hands rest on (and reach across) the felt. DL_ALL draws both (the salon, previews).
enum Layer : uint8_t { DL_ALL, DL_BODY, DL_HANDS };
void DrawDuelDealer(const Skin& skin, const DealerPose& pose, Vector2 anchor, float scale, float t, Vector2 look, int layer = DL_ALL);

// ---------------------------------------------------------------- icons for the duel's screens
enum Icon : uint8_t {
    IC_NOD, IC_SNEER, IC_SHRUG, IC_TIP_HAT, IC_SLOW_CLAP, IC_GULP,     // the emote wheel (same order as Emote)
    IC_DRAFT, IC_CONSTRUCTED, IC_QUICK,                                // the mode plates (same order as Mode)
    IC_REEL,                                                           // the arcade drum's Flats Duel reel
    IC_PEARL,                                                          // a game won (best-of-three pips)
    IC_SIDEBOARD,                                                      // swap a card
    IC_TIMER,                                                          // the turn clock's face
    IC_PACK,                                                           // a draft pack, tied with twine
    IC_COUNT
};
void DrawDuelIcon(int icon, Vector2 centre, float size, Color ink, float t = 0);
// The turn clock: a brass ring that empties as `frac` (1 -> 0) runs out, red in the last `warn` fraction.
void DrawTurnClock(Vector2 centre, float radius, float frac, float warn, float t);

}  // namespace duel
}  // namespace flats
