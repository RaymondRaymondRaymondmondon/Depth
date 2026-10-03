// The Trawl's proximity voice (design doc v2, "Talking"; "Sound": "Proximity voice is processed in the world: muffled
// behind walls, thinned by wind and rain, lost under the engine at full, crackling on walkies, and bubbling for a
// crewman in the water. Ghosts hear each other with a cold reverb and hear the living faintly"). How one hand hears
// another, as voice::Hearing, worked out from the world every frame (headless: --trawl-voice-test checks it).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "voice.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace tw {

namespace {
const float VOICE_RANGE = 12;            // "voices carry about 12 m on deck"
const float VOICE_CLEAR = 2;             // full strength this close
// the speaking tube's three mouths (design doc: "from the wheelhouse to the engine room and to the bow, always clear")
bool AtTube(const Crew& c, int* end) {
    if (c.dead || c.overboard) return false;
    const auto& st = Stations();
    if (c.deck == 0 && Vector2Distance(c.p, st[(int)StationKind::Helm].at) < 2.0f) { *end = 0; return true; }
    if (c.deck == 1 && Vector2Distance(c.p, st[(int)StationKind::Boiler].at) < 2.5f) { *end = 1; return true; }
    if (c.deck == 0 && c.p.x > 8.5f) { *end = 2; return true; }
    return false;
}
// a walkie with tonight's battery: held in hand to talk; anywhere in the slots to hear
bool LiveWalkie(const Crew& c, bool inHand) {
    if (c.dead) return false;
    if (inHand) { const Slot& s = c.slots[c.sel]; return s.it == Item::Walkie && s.spare > 0; }
    for (const auto& s : c.slots) if (s.it == Item::Walkie && s.spare > 0) return true;
    return false;
}
}  // namespace

bool OnWalkie(const Crew& c) { return LiveWalkie(c, true); }

voice::Hearing HearOnBoard(const Gannet& g, int you, int them) {
    voice::Hearing none; none.gain = 0;
    if (you < 0 || them < 0 || you >= (int)g.crew.size() || them >= (int)g.crew.size() || you == them) return none;
    const Crew& L = g.crew[you];
    const Crew& S = g.crew[them];
    // the dead: ghosts hear each other clearly, in a cold hollow; the living never hear a ghost
    if (S.dead) { if (!L.dead) return none; voice::Hearing h; h.ghost = 1; return h; }
    // the walkie: channel-wide, crackling, wherever the two of them are
    voice::Hearing w; w.gain = 0;
    if (!L.dead && LiveWalkie(S, true) && LiveWalkie(L, false)) { w.gain = 0.85f; w.radio = 1; }
    // in the air between them: 12 m, halved by a squall or a storm (a fifth off for rain) and by the engine at full
    float range = VOICE_RANGE;
    Weather wx = g.sea.weather;
    bool wild = wx == Weather::Squall || wx == Weather::Storm;
    if (wild) range *= 0.5f; else if (wx == Weather::Rain) range *= 0.8f;
    if (g.boat.telegraph == 3 && !g.moored) range *= 0.5f;
    if (S.overboard) range = std::min(range, 6.0f);     // (a mouth at the waterline)
    voice::Hearing p; p.gain = 0;
    bool aboardBoth = L.deck <= 1 && S.deck <= 1 && !L.overboard && !S.overboard;
    if (aboardBoth && (L.deck == 1) != (S.deck == 1)) {
        // between the engine room and the deck: muffled to nothing, except mouth to mouth through the speaking tube
        int ea = -1, eb = -1;
        if (AtTube(S, &ea) && AtTube(L, &eb) && ea != eb) { p.gain = 0.9f; p.radio = 0.25f; p.muffle = 0.2f; }
    } else {
        Vector2 a = g.HandWorld(you), b = g.HandWorld(them);
        float d = Vector2Distance(a, b);
        float prox = std::clamp(1 - (d - VOICE_CLEAR) / std::max(1.0f, range - VOICE_CLEAR), 0.0f, 1.0f);
        p.gain = prox;
        p.muffle = 0.15f * (1 - prox) + (wild ? 0.15f : 0.0f);   // (the far end of a shout, and the wind taking it)
        if (S.overboard) p.bubble = 1;
        if (L.overboard) p.muffle = std::max(p.muffle, 0.45f);
        // which side: in the Gannet's frame (x to the bow, y to starboard), against the way the listener faces
        Vector2 dd = Vector2Subtract(g.boat.ToDeck(b), g.boat.ToDeck(a));
        if (Vector2Length(dd) > 0.5f) { Vector2 right{-L.facing.y, L.facing.x}; p.pan = std::clamp(Vector2DotProduct(Vector2Normalize(dd), right), -1.0f, 1.0f) * 0.8f; }
    }
    // a ghost listening to the living: faint, through the cold
    if (L.dead) { p.gain *= 0.4f; p.ghost = 0.4f; return p; }
    return w.gain > p.gain ? w : p;
}

// ---------------------------------------------------------------- --trawl-voice-test
int RunTrawlVoiceTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; };
    printf("The Trawl's proximity voice\n");
    Gannet g; g.Init(3, 7, Weather::Calm); g.moored = false;
    for (auto& c : g.crew) { c.station = -1; c.deck = 0; c.dead = c.overboard = false; }
    g.crew[0].p = {-2, 0}; g.crew[0].facing = {1, 0};
    g.crew[1].p = {0, 0};
    voice::Hearing near = HearOnBoard(g, 0, 1);
    g.crew[1].p = {6, 0};
    voice::Hearing mid = HearOnBoard(g, 0, 1);
    g.crew[1].p = {-2, 0}; g.crew[0].p = {9.6f, 0};   // (bow to stern: over 11 m... and further below)
    check(near.gain > 0.95f && mid.gain > 0.2f && mid.gain < near.gain && mid.muffle > near.muffle, "a hand 2 m off is heard full; 8 m off, quieter and a little muffled");
    g.crew[0].p = {-2, 0}; g.crew[1].p = {-2, 2.5f};
    voice::Hearing side = HearOnBoard(g, 0, 1);
    check(side.pan > 0.5f, "a hand on your right (starboard, facing the bow) is heard on the right");
    g.crew[1].p = {-2 + 9, 0};
    float calm9 = HearOnBoard(g, 0, 1).gain;
    g.sea.weather = Weather::Squall; float squall9 = HearOnBoard(g, 0, 1).gain; g.sea.weather = Weather::Calm;
    g.boat.telegraph = 3; float full9 = HearOnBoard(g, 0, 1).gain; g.boat.telegraph = 0;
    check(calm9 > 0.2f && squall9 == 0 && full9 == 0, "9 m carries in a calm, but not in a squall, nor over the engine at full");
    // below decks and the speaking tube
    const auto& st = Stations();
    g.crew[1].deck = 1; g.crew[1].p = st[(int)StationKind::Boiler].at;
    g.crew[0].p = Vector2Add(st[(int)StationKind::Helm].at, {1.5f, 1.5f});
    float through = HearOnBoard(g, 0, 1).gain;
    g.crew[0].p = st[(int)StationKind::Helm].at;
    voice::Hearing tube = HearOnBoard(g, 0, 1);
    check(through == 0 && tube.gain > 0.8f && tube.radio > 0, "the engine room is muffled to nothing from the deck, except through the speaking tube at the wheel");
    // the water
    g.crew[1].deck = 0; g.crew[1].overboard = true; g.crew[1].swim = g.boat.ToWorld({-2, 6});
    voice::Hearing wet = HearOnBoard(g, 0, 1);
    g.crew[0].p = {-2, 2.4f};
    voice::Hearing wetNear = HearOnBoard(g, 0, 1);
    check(wet.gain == 0 && wetNear.gain > 0 && wetNear.bubble > 0.9f, "a hand in the water bubbles, and carries only 6 m");
    g.crew[1].overboard = false; g.crew[1].p = {-2, 0};
    // walkies
    g.crew[1].p = {9.5f, 0}; g.crew[0].p = {-11, 0};
    g.crew[1].slots[2] = {Item::Walkie, 0}; g.crew[1].slots[2].spare = 1; g.crew[1].sel = 2;
    g.crew[0].slots[3] = {Item::Walkie, 0}; g.crew[0].slots[3].spare = 1;
    voice::Hearing radio = HearOnBoard(g, 0, 1);
    g.crew[1].sel = 0; float notHeld = HearOnBoard(g, 0, 1).gain; g.crew[1].sel = 2;
    g.crew[0].slots[3].spare = 0; float flat = HearOnBoard(g, 0, 1).gain; g.crew[0].slots[3].spare = 1;
    check(radio.gain > 0.8f && radio.radio > 0.9f && notHeld == 0 && flat == 0, "a walkie held to the mouth reaches a hand with a live walkie the length of the boat, crackling; not when it's put away, nor with a flat battery");
    // the dead
    g.crew[1].sel = 0; g.crew[1].p = {-1, 0}; g.crew[0].p = {-2, 0};
    g.crew[1].dead = true; float ghostToLiving = HearOnBoard(g, 0, 1).gain;
    g.crew[0].dead = true; voice::Hearing ghosts = HearOnBoard(g, 0, 1);
    g.crew[1].dead = false; voice::Hearing livingToGhost = HearOnBoard(g, 0, 1);
    check(ghostToLiving == 0 && ghosts.gain > 0.9f && ghosts.ghost > 0.9f && livingToGhost.gain > 0 && livingToGhost.gain < 0.5f && livingToGhost.ghost > 0,
          "the living never hear a ghost; ghosts hear each other in a cold hollow, and the living faintly");
    // a battery a night
    {
        Gannet gs; Eco es; Session ss; ss.Begin(gs, es, 1, 45); ss.money = 100;
        gs.crew[0].slots[2] = Slot{}; gs.crew[0].slots[3] = Slot{};
        bool b1 = ss.Buy("walkie", nullptr, 0) && ss.Buy("battery", nullptr, 0) && ss.money == 55;
        int ws = -1; for (int k = 0; k < 4; k++) if (gs.crew[0].slots[k].it == Item::Walkie) ws = k;
        check(b1 && ws >= 0 && gs.crew[0].slots[ws].ammo == 2, "the Chandler sells a walkie (40, with a battery) and batteries (5)");
    }
    printf(fails ? "trawl-voice-test: %d check(s) failed\n" : "trawl-voice-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace tw
