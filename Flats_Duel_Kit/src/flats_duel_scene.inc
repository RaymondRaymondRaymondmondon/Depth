// ============================================================================
//  Flats Duel: the client's table screen. REFERENCE CODE: written against flats.cpp's helpers as they stand on
//  2026-09-30 (U, CellFx, ApplyEvents, UpdateFx, DrawBoardCard, DrawCardFace, DrawSlot, DrawTable, DrawScale,
//  DrawBell, DrawBottle, HandPos, CellRect, CellCenter, COL_X, BellRect, Button, Txt, DrawTextCentered, Hover,
//  HoverInHand, DrawInspector, DrawParticles, Toast, PlayBell, BloodAvailable, Affordable). It has NOT been compiled
//  inside Depth (the kit never touches the Depth folder): expect small fixes. Names used from flats.cpp are the real ones.
//
//  PASTE INTO: flats.cpp, inside its anonymous namespace, after DrawBattle. Then export one function (bottom of this
//  file) and declare it in game.h. arcade.cpp calls it while gSess.stage == S_PLAYING && gSess.game == G_FLATS_DUEL.
//
//  THE MIRROR BATTLE. Everything flats.cpp draws reads U.bat (DrawBoardCard, Affordable, the hand fan, the sacrifice
//  marks, the item targets, the bell). So the duel keeps U.bat as a MIRROR of the latest snapshot: the board, my hand,
//  my items, placeholder cards for the opponent's hand and both decks (counts only). Nothing ever calls
//  U.bat.Play/Draw/UseItem/EndTurn/Advance here: every one of those becomes an Action sent to the host, and the next
//  snapshot brings the result. U.bat.CanPlay is still used, locally, as a PREDICTION to grey out illegal moves before
//  sending (the host re-checks everything).
//  Animation: when a snapshot's `step` moves on, ApplyEvents(view.ev, previousBoard) plays it exactly as single-player
//  does (the events are already in my frame; the opponent's draws arrive with amount -1).
// ============================================================================
#include "flats_duel.h"
#include "flats_duel_art.h"

struct DuelUi {
    flats::duel::View v;
    bool have = false;
    uint32_t step = 0;
    Board prevBoard;
    // the opponent's figure
    float exprW = 0; int expr = flats::duel::EX_NEUTRAL; float exprHold = 0;
    int emoteSeen[2] = {0, 0}; int emote[2] = {-1, -1}; float emoteT[2] = {99, 99};
    float reachT = 99; Vector2 reachTo{0, 0};           // their hand laying a card (ReachPose)
    float tellT = 99, tellWait = 3;                     // their thinking fidget
    float lastClockSec = 99;
    // my own emote wheel
    bool wheel = false;
};
DuelUi D;

// ---- the mirror
void DuelMirror(const flats::duel::View& v) {
    Battle& b = U.bat;
    b.board = v.board;
    b.hand = v.hand;
    b.items = v.items;
    b.foeHand.assign(v.handCount[1], Card{});             // backs only
    b.deck.assign(v.deckCount[0], Card{});                 // counts only (the Draw button reads deck.size())
    b.foeDeck.assign(v.deckCount[1], Card{});
    b.charms = 0; b.dealer = 2; b.boss = false;
    b.turn = v.phase == flats::duel::PH_PLAY ? (v.myTurn ? (Turn)v.turn : Turn::FOE_MAIN) : Turn::OVER;
    b.firstPlayThisTurn = true;   // (no Sailor's Knot in a duel, so BloodNeeded doesn't depend on it)
}

// ---- a new snapshot from the session
void DuelTakeSnapshot(const std::vector<uint8_t>& bytes) {
    flats::duel::View v;
    Reader r(bytes);
    if (!flats::duel::ReadView(r, v)) return;
    bool fresh = !D.have || v.gameNo != D.v.gameNo || v.phase != D.v.phase;
    Board prev = U.bat.board;
    if (fresh) for (int rr = 0; rr < ROWS; rr++) for (int c = 0; c < COLS; c++) U.fx[rr][c] = CellFx();
    DuelMirror(v);
    if (D.have && v.step != D.step && v.step == D.step + 1 && !fresh) {   // the next step: animate it (a jump, as after a rejoin, just snaps)
        ApplyEvents(v.ev, prev);
        for (const Event& e : v.ev) {
            if (e.type == Event::Play && v.evActor == 1) {    // their hand lays it: start the reach; the card's own flight comes from the hand
                D.reachT = 0; D.reachTo = CellCenter(e.r1, e.c1);
                PlayCue("duel.reach");
            }
            if (e.type == Event::ItemFound && e.text.rfind("Used: ", 0) == 0) Toast(e.text);
        }
        if (v.bigHit == 0) { D.expr = flats::duel::EX_AHEAD; D.exprHold = 1.4f; }        // my creature took a big hit: they gloat
        if (v.bigHit == 1) { D.expr = flats::duel::EX_FLINCH; D.exprHold = 0.9f; }       // theirs did: they flinch
    }
    if (D.have && v.myTurn && !D.v.myTurn && v.phase == flats::duel::PH_PLAY) PlayCue("duel.turn");
    for (int k = 0; k < 2; k++) if (D.have && v.emoteSeq[k] != D.emoteSeen[k]) { D.emote[k] = v.emote[k]; D.emoteT[k] = 0; PlayCue(flats::duel::EmoteOf(v.emote[k]).cue); }
    for (int k = 0; k < 2; k++) D.emoteSeen[k] = v.emoteSeq[k];
    D.v = v; D.step = v.step; D.have = true;
}

// ---- sending
using SendFn = void (*)(const flats::duel::Action&);   // arcade.cpp passes one that does WriteAction + gSess.Act
void DuelTryPlay(int col, SendFn send) {
    if (U.selHand < 0 || U.selHand >= (int)U.bat.hand.size()) return;
    const Card& c = U.bat.hand[U.selHand];
    if (c.cost == CostType::BLOOD && !U.sacMode) {
        if (BloodAvailable() < U.bat.BloodNeeded(c)) { Toast("You need more creatures to sacrifice for that card."); return; }
        U.sacMode = true; U.sacCol = col; U.sacs.clear();
        return;
    }
    std::string why;
    if (!U.bat.CanPlay(U.selHand, col, U.sacs, &why)) { Toast(why); U.sacMode = false; U.sacs.clear(); return; }   // predicted illegal: don't send
    U.handFrom = HandPos(U.selHand, (int)U.bat.hand.size());
    flats::duel::Action a; a.kind = flats::duel::Action::PLAY; a.a = U.selHand; a.b = col; a.sacs = U.sacs;
    send(a);
    U.selHand = -1; U.sacs.clear(); U.sacMode = false;
}

// ---- the opponent, drawn in their skin: body before the table, hands after the board
flats::duel::DealerPose DuelOpponentPose(float t, float dt) {
    using namespace flats::duel;
    const View& v = D.v;
    int skin = v.skin[1];
    DealerPose p = IdlePose(t, skin);
    int want = EX_NEUTRAL;
    if (v.phase == PH_GAME_OVER || v.phase == PH_MATCH_OVER) want = v.gameWinner == 1 ? EX_WIN : v.gameWinner == 0 ? EX_LOSE : EX_NEUTRAL;
    else if (v.board.scale <= -4) want = EX_AHEAD;       // (my frame: negative is their way)
    else if (v.board.scale >= 4) want = EX_BEHIND;
    if (D.exprHold > 0) { D.exprHold -= dt; want = D.expr; }
    if (want != D.expr && D.exprHold <= 0) { D.expr = want; D.exprW = 0; }
    D.exprW = std::min(1.0f, D.exprW + dt / 0.3f);
    p = Mix(p, ExpressionPose(D.expr), D.exprW);
    if (!v.myTurn && v.phase == PH_PLAY && v.actor == 1) {   // they're thinking: a tell now and then
        if ((D.tellWait -= dt) <= 0) { D.tellT = 0; D.tellWait = 5 + GetRandomValue(0, 40) / 10.0f; }
    }
    if (D.tellT < 1.6f) { p = Add(p, TellPose(skin, D.tellT / 1.6f)); D.tellT += dt; }
    if (D.emoteT[1] < EmoteOf(D.emote[1]).seconds) { p = Add(p, EmotePose(D.emote[1], D.emoteT[1] / EmoteOf(D.emote[1]).seconds)); D.emoteT[1] += dt; }
    if (D.reachT < 0.7f) { p = Add(p, ReachPose(D.reachTo, D.reachT / 0.7f, {640, 602}, 1.0f)); D.reachT += dt; }
    return p;
}

void DrawDuelRoom(float t, const flats::duel::DealerPose& pose, Vector2 m) {
    ClearBackground(Color{4, 5, 8, 255});
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{12, 14, 22, 255}, Color{4, 4, 7, 255});
    flats::duel::DrawDuelDealer(flats::duel::SkinOf(D.v.skin[1]), pose, {640, 602}, 1.0f, t, m, flats::duel::DL_BODY);
    DrawTable();
    DrawScale(U.beam, D.v.board.scale);   // relabel "DEALER" with the opponent's name: DrawScale prints "DEALER"/"YOU" (give it a name parameter)
}

// ---- the screen. `send` sends an action; `names` are the two players' display names (me, them) from the session.
void DrawDuel(float dt, float t, Vector2 m, SendFn send, const char* myName, const char* theirName) {
    using namespace flats::duel;
    if (!D.have) return;
    const View& v = D.v;
    UpdateFx(dt);
    flats::duel::DealerPose pose = DuelOpponentPose(t, dt);
    DrawDuelRoom(t, pose, m);

    if (v.phase == PH_SETUP)      { /* the setup plates: see previews/mockup_setup.png; SET_MODE (host only), SET_SKIN, SET_ITEMS, SET_PRESET, SUBMIT_DECK, READY */ }
    else if (v.phase == PH_DRAFT) { /* the open pack: see previews/mockup_draft.png; PICK when v.picker == 0 */ }
    else if (v.phase == PH_BUILD) { /* DeckGrid over v.pool: toggle 20+ into the deck, then BUILD with their indices */ }
    else if (v.phase == PH_SIDEBOARD) { /* two DeckGrids (v.deckList, v.sideboard): SWAP(a, b) up to v.swapsLeft, then READY */ }

    if (v.phase == PH_PLAY || v.phase == PH_GAME_OVER) {
        bool myMain = v.myTurn && v.turn == (int)Turn::YOU_MAIN;
        // slots and cards, exactly as DrawBattle draws them (row 0 is their reserve now, not a queue: no telegraph arrows)
        for (int c = 0; c < COLS; c++) {
            DrawSlot(CellRect(R_FOE_QUEUE, c), false, t);
            DrawSlot(CellRect(R_FOE_FRONT, c), false, t);
            DrawSlot(CellRect(R_YOU_FRONT, c), myMain && U.selHand >= 0 && !U.sacMode, t);
        }
        for (int r : {R_YOU_BACK, R_FOE_QUEUE, R_FOE_FRONT, R_YOU_FRONT}) for (int c = 0; c < COLS; c++) DrawBoardCard(r, c, t);
        for (int i = 0; i < v.handCount[1]; i++) DrawCardFace({930.0f + i * 14, 292.0f - (i % 2) * 3, 34, 48}, Card{}, false);
        // their hands go on the felt AFTER the board, so a reaching hand lays its card over the slot
        flats::duel::DrawDuelDealer(flats::duel::SkinOf(v.skin[1]), pose, {640, 602}, 1.0f, t, m, flats::duel::DL_HANDS);

        // ... then the rest of DrawBattle's body, unchanged except for these substitutions:
        //   "Draw a card" button     -> send DRAW(1)        "Draw a minnow" -> send DRAW(0)
        //   the bell                 -> PlayBell(); send BELL (only when myMain)
        //   TryPlay(col)             -> DuelTryPlay(col, send)
        //   UseItemAt(slot, r, c)    -> send ITEM(slot, r, c)   (the target highlight: Fishhook now targets R_FOE_QUEUE = their reserve)
        //   momentum pips            -> drop them (no momentum in a duel); show v.gamesWon[] as IC_PEARL pips instead
        //   the dealer's twist line  -> "<theirName>  -  game N of 3  -  round R"  (+ "SUDDEN DEATH" when v.suddenDeath)
        //   the turn hint            -> "<theirName> is thinking..." while !v.myTurn
        (void)myName; (void)theirName;

        // the turn clock (top right), with the last 10 seconds ticking
        float full = RulesOf(v.mode).turnSeconds;
        DrawTurnClock({1200, 90}, 34, v.timer / full, 10.0f / full, t);
        if (v.myTurn && v.timer <= 10 && (int)v.timer != (int)D.lastClockSec) PlayCue("duel.clock");
        D.lastClockSec = v.timer;
        // games won
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < RulesOf(v.mode).gamesToWin; i++)
                DrawDuelIcon(IC_PEARL, {1150.0f + i * 30, 180.0f + k * 34}, 40, Fade(Pal::Brass, i < v.gamesWon[k] ? 1.0f : 0.25f));
    }

    // the emote wheel (bottom left): six icons, a 6 s cooldown shown as a dimmed ring
    for (int e = 0; e < EM_COUNT; e++) {
        Vector2 c{70 + e * 44.0f, 690};
        bool hot = CheckCollisionPointCircle(m, c, 19);
        DrawDuelIcon(e, c, 38, hot ? Pal::Paper : Pal::Brass, t);
        if (hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { flats::duel::Action a; a.kind = flats::duel::Action::EMOTE; a.a = e; send(a); }
    }
    // my own emote plays on nobody (I'm the camera); show it as a small floating icon over my side instead
    if (D.emoteT[0] < 1.2f) { DrawDuelIcon(D.emote[0], {640, 470 - D.emoteT[0] * 30}, 48, Fade(Pal::Brass, 1 - D.emoteT[0] / 1.2f), t); D.emoteT[0] += dt; }
    DrawParticles();
}

// ---- export (outside the anonymous namespace), and declare in game.h:
//   void FlatsDuelScene(const std::vector<uint8_t>& snapshot, int stateVersion, void (*send)(const flats::duel::Action&), const char* me, const char* them);
// void FlatsDuelScene(const std::vector<uint8_t>& snap, int version, void (*send)(const flats::duel::Action&), const char* me, const char* them) {
//     static int seen = -1;
//     if (version != seen) { seen = version; DuelTakeSnapshot(snap); }
//     DrawDuel(GetFrameTime(), (float)GetTime(), MouseInGame(), send, me, them);   // MouseInGame: whatever flats.cpp uses for the letterboxed mouse
// }
