#include "flats_duel.h"
#include <algorithm>
#include <cmath>

namespace flats {
namespace duel {

namespace {
int Opp(int p) { return 1 - p; }
bool In(int c) { return c >= 0 && c < COLS; }
bool RowEvent(Event::Type t) {
    switch (t) {
        case Event::Play: case Event::Strike: case Event::Damage: case Event::ScaleHit: case Event::Death: case Event::Move:
        case Event::Knock: case Event::Push: case Event::Crush: case Event::SigilFired: case Event::Evolve:
        case Event::ItemFound: return true;   // (the duel's own: an item used at a cell, or a find on one side)
        default: return false;
    }
}
// the live hands/decks/items: the Battle holds both hands while a game is on
std::vector<Card>& HandOf(State& s, int p) { return p == s.active ? s.bt.hand : s.bt.foeHand; }
const std::vector<Card>& HandOf(const State& s, int p) { return p == s.active ? s.bt.hand : s.bt.foeHand; }
std::vector<Card>& DeckOf(State& s, int p) { return p == s.active ? s.bt.deck : s.bt.foeDeck; }
const std::vector<Card>& DeckOf(const State& s, int p) { return p == s.active ? s.bt.deck : s.bt.foeDeck; }
std::vector<int>& ItemsOf(State& s, int p) { return p == s.active ? s.bt.items : s.z[p].items; }
const std::vector<int>& ItemsOf(const State& s, int p) { return p == s.active ? s.bt.items : s.z[p].items; }
bool InGame(const State& s) { return s.phase == PH_PLAY; }

void Log(State& s, const std::string& line) { s.log.push_back(line); if (s.log.size() > 8) s.log.erase(s.log.begin()); }
float TurnSeconds(const State& s) { return RulesOf(s.mode).turnSeconds; }

// ---------------------------------------------------------------- a step: one engine call, its events, the duel's own rules
void BeginStep(State& s) {
    s.ev.clear();
    s.evFrame = s.active; s.evActor = s.active; s.bigHitSide = -1;
    // Undying has returned twice for a player: their creatures on the board lose it
    for (int q = 0; q < 2; q++) {
        if (s.z[q].undyingUsed < Tune().undyingCap) continue;
        const int mine[2] = {R_YOU_FRONT, R_YOU_BACK}, theirs[2] = {R_FOE_QUEUE, R_FOE_FRONT};
        for (int r : (q == s.active ? mine : theirs))
            for (int c = 0; c < COLS; c++) {
                Card& k = s.bt.board.cell[r][c].card;
                if (s.bt.board.cell[r][c].used && k.Has(Sigil::UNDYING)) k.sigils.erase(std::remove(k.sigils.begin(), k.sigils.end(), Sigil::UNDYING), k.sigils.end());
            }
    }
}
void EndStep(State& s, const Events& ev) {
    std::string lastDead[ROWS][COLS];
    for (const Event& e : ev) {
        Event out = e;
        if (e.type == Event::Death && e.r1 >= 0 && e.r1 < ROWS && In(e.c1)) lastDead[e.r1][e.c1] = e.text;
        if (e.type == Event::SigilFired && e.text == "Undying" && e.r1 >= 0 && e.r1 < ROWS && In(e.c1)) {
            int q = e.r1 >= R_YOU_FRONT ? s.active : Opp(s.active);
            if (++s.z[q].undyingUsed > Tune().undyingCap) {   // two died in one step: the extra copy never reaches the hand
                s.z[q].undyingUsed = Tune().undyingCap;
                auto& h = HandOf(s, q);
                for (int i = (int)h.size() - 1; i >= 0; i--) if (h[i].name == lastDead[e.r1][e.c1]) { h.erase(h.begin() + i); break; }
                out.text = "Undying (spent)";
            }
        }
        if (e.type == Event::Damage && e.amount >= 4 && e.r1 >= 0) s.bigHitSide = e.r1 >= R_YOU_FRONT ? s.active : Opp(s.active);
        s.ev.push_back(out);
    }
    // Scavenger finds: a random pack item, if the pack has room
    for (int side = 0; side < 2; side++) {
        int q = side == 0 ? s.active : Opp(s.active);
        while (s.bt.board.itemsFound[side] > 0) {
            s.bt.board.itemsFound[side]--;
            auto& it = ItemsOf(s, q);
            if ((int)it.size() >= MAX_ITEMS) continue;
            int kind = s.rng.I(0, (int)PackItem::COUNT - 1);
            it.push_back(kind);
            Event f; f.type = Event::ItemFound; f.amount = kind; f.r1 = side == 0 ? R_YOU_FRONT : R_FOE_FRONT; f.text = "Found"; s.ev.push_back(f);
        }
    }
    s.step++;
}

void Shuffle(State& s, std::vector<Card>& v) { s.rng.Shuffle(v); }

void StartGame(State& s) {
    const Tuning& T = Tune();
    s.phase = PH_PLAY;
    s.gameNo++;
    s.round = 1;
    s.suddenDeath = false;
    s.gameWinner = -1;
    s.active = s.thisGameFirst = s.firstPlayer;
    s.bt = Battle();
    s.bt.dealer = 2;                     // (the Novice's and the Tidewife's free minnows are single-player training wheels)
    s.bt.turnNo = 1;
    std::vector<Card> hands[2], decks[2];
    for (int p = 0; p < 2; p++) {
        Zone& z = s.z[p];
        decks[p] = z.deckList;
        for (auto& c : decks[p]) c.ResetForBattle();
        Shuffle(s, decks[p]);
        for (int i = 0; i < T.openingDraw && !decks[p].empty(); i++) { hands[p].push_back(decks[p].back()); decks[p].pop_back(); }
        hands[p].push_back(Minnow());
        if (p != s.firstPlayer) for (int i = 0; i < T.secondMinnows; i++) hands[p].push_back(Minnow());
        z.items.assign(z.loadout, z.loadout + std::clamp(T.itemsPerGame, 0, 2));
        z.itemsUsed = z.undyingUsed = 0;
    }
    int a = s.active, b = Opp(a);
    s.bt.hand = hands[a]; s.bt.foeHand = hands[b];
    s.bt.deck = decks[a]; s.bt.foeDeck = decks[b];
    s.bt.items = s.z[a].items;
    s.bt.board.bones[1] = T.secondBones;  // the second player, across from the first
    s.bt.board.scale = -T.secondScaleStart[std::clamp(s.mode, 0, MODE_COUNT - 1)];
    s.bt.turn = T.firstSkipsDraw ? Turn::YOU_MAIN : Turn::YOU_DRAW;
    s.bt.firstPlayThisTurn = true;
    s.timer = TurnSeconds(s);
    s.wait = 0;
    s.ev.clear(); s.step++;
    Log(s, "Game " + std::to_string(s.gameNo) + ": player " + std::to_string(s.firstPlayer + 1) + " goes first.");
}

void EndGame(State& s, int winner) {
    s.gameWinner = winner;
    if (winner == 0 || winner == 1) s.z[winner].gamesWon++;
    s.phase = PH_GAME_OVER;
    s.wait = 0;
    s.bt.turn = Turn::OVER;
    Log(s, winner == 2 ? "The game is tied." : "Player " + std::to_string(winner + 1) + " takes game " + std::to_string(s.gameNo) + ".");
    // the loser of a game goes first in the next; after a tie, whoever went second
    s.firstPlayer = winner == 2 ? Opp(s.firstPlayer) : Opp(winner);
}

// A turn has ended (the Battle reached FOE_START): count the round, check its limits, then hand the table over.
void SwapTurn(State& s) {
    const Tuning& T = Tune();
    s.z[s.active].items = s.bt.items;
    if (s.active != s.firstPlayer) {
        s.round++;
        int sc = s.bt.board.scale;       // in the active player's frame
        if (s.round > T.roundLimit && !s.suddenDeath) {
            if (sc != 0) { EndGame(s, sc > 0 ? s.active : Opp(s.active)); return; }
            s.suddenDeath = true;
            Log(s, "Level scales: sudden death. The next tip wins.");
        }
        if (s.suddenDeath && s.round > T.hardLimit) { EndGame(s, 2); return; }
    }
    FlipBoard(s.bt.board);
    std::swap(s.bt.hand, s.bt.foeHand);
    std::swap(s.bt.deck, s.bt.foeDeck);
    s.active = Opp(s.active);
    s.bt.items = s.z[s.active].items;
    s.bt.turn = Turn::YOU_DRAW;
    s.bt.turnNo = s.round;
    s.bt.firstPlayThisTurn = true;
    s.timer = TurnSeconds(s);
    s.wait = 0;
}

// after every step of a game: a tipped scale, sudden death, or the end of the turn
void AfterPlayStep(State& s) {
    if (s.phase != PH_PLAY) return;
    if (s.bt.turn == Turn::OVER) { EndGame(s, s.bt.winner > 0 ? s.active : Opp(s.active)); return; }
    if (s.suddenDeath && s.bt.board.scale != 0) { EndGame(s, s.bt.board.scale > 0 ? s.active : Opp(s.active)); return; }
    if (s.bt.turn == Turn::FOE_START) SwapTurn(s);
}

// ---------------------------------------------------------------- the draft
void NewPack(State& s) {
    const Tuning& T = Tune();
    std::vector<int> pool;
    for (const Card& c : Catalog()) if (Draftable(c.id)) for (int k = 0; k < DraftWeight(c.tier); k++) pool.push_back(c.id);
    s.pack.clear();
    for (int tries = 0; (int)s.pack.size() < T.packSize && tries < 200; tries++) {
        int id = pool[s.rng.I(0, (int)pool.size() - 1)];
        if (std::any_of(s.pack.begin(), s.pack.end(), [&](const Card& c) { return c.id == id; })) continue;
        int roll = s.rng.I(0, 99);
        s.pack.push_back(MakeCard(id, roll < T.draftHexPct ? ED_HEX : roll < T.draftHexPct + T.draftFoilPct ? ED_FOIL : ED_NONE));
    }
    s.picksInPack = 0;
    s.timer = T.pickSeconds;
}
int Picker(const State& s) { return (s.picksInPack % 2 == 0) ? s.opener : Opp(s.opener); }

void BeginMatch(State& s) {
    for (int p = 0; p < 2; p++) { s.z[p].ready = false; s.z[p].gamesWon = 0; }
    s.firstPlayer = s.rng.I(0, 1);
    if (s.mode == MODE_DRAFT) {
        s.phase = PH_DRAFT;
        s.packNo = 0; s.opener = s.rng.I(0, 1);
        for (auto& z : s.z) { z.pool.clear(); z.deckList.clear(); z.sideboard.clear(); z.submitted = false; }
        s.picks.clear();
        NewPack(s);
        Log(s, "The draft begins.");
        return;
    }
    for (auto& z : s.z) {
        if (s.mode == MODE_QUICK) { z.deckList = WithStaples(PresetDeck(z.preset)); z.sideboard.clear(); }
        z.swapsLeft = 0;
    }
    StartGame(s);
}

void StartBuild(State& s) {
    s.phase = PH_BUILD;
    for (auto& z : s.z) z.submitted = false;
    s.timer = Tune().buildSeconds;
    Log(s, "Build your deck: keep " + std::to_string(Tune().deckFromDraft) + ".");
}

void AfterBuild(State& s) {
    if (s.z[0].submitted && s.z[1].submitted) StartGame(s);
}

// ---------------------------------------------------------------- the bot's reading of the board (from Battle::AutoYourTurn)
int LaneFor(const Battle& bt, const Card& c) {
    int best = -1; float bestS = -1e9f;
    for (int col = 0; col < COLS; col++) {
        if (bt.board.cell[R_YOU_FRONT][col].used) continue;
        float sc = 0;
        const Cell& t = bt.board.cell[R_FOE_FRONT][col];
        if (t.used) {
            int tstr = bt.board.EffStrength(R_FOE_FRONT, col);
            if (c.strength >= t.card.hp) sc += 3;
            if (c.defense > tstr) sc += 2;
            sc += 0.25f * c.weight;
            if (t.card.Has(Sigil::SPINES) && c.defense <= 2) sc -= 1.5f;
            if (t.card.Has(Sigil::REPULSIVE)) sc -= 1.0f;
        } else sc += 2.0f * (c.strength + (c.Has(Sigil::SKIMMER) ? 1 : 0));
        sc += 0.05f * col;   // deterministic tie-break (the bot never touches the host's rng)
        if (sc > bestS) { bestS = sc; best = col; }
    }
    return best;
}

bool BestPlay(const Battle& bt, int& handIdx, int& lane, std::vector<std::pair<int, int>>& sacs) {
    float bestScore = 0.4f;
    handIdx = -1;
    for (int hi = 0; hi < (int)bt.hand.size(); hi++) {
        const Card& c = bt.hand[hi];
        std::vector<std::pair<int, int>> sac;
        float sacCost = 0;
        if (c.cost == CostType::BLOOD) {
            std::vector<std::pair<float, std::pair<int, int>>> cand;
            for (int r : {R_YOU_FRONT, R_YOU_BACK})
                for (int cc = 0; cc < COLS; cc++)
                    if (bt.board.cell[r][cc].used) {
                        const Card& k = bt.board.cell[r][cc].card;
                        float v = (float)k.Rating() - (k.Has(Sigil::NINE_LIVES) ? 4.0f : 0) - (k.Has(Sigil::BALLAST) ? 3.0f : 0) - (k.hp < k.defense ? 1.0f : 0);
                        cand.push_back({v, {r, cc}});
                    }
            std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first < b.first; });
            int have = 0;
            for (auto& k : cand) {
                if (have >= bt.BloodNeeded(c)) break;
                sac.push_back(k.second);
                have += bt.board.cell[k.second.first][k.second.second].card.BloodValue();
                sacCost += std::max(0.0f, k.first) * 0.9f;
            }
            if (have < bt.BloodNeeded(c)) continue;
        } else if (c.cost == CostType::BONES && bt.board.bones[0] < c.costAmount) continue;
        float boneCost = c.cost == CostType::BONES ? 0.5f * c.costAmount : 0;
        int col = LaneFor(bt, c);
        if (col < 0) for (int k = 0; k < COLS; k++) if (bt.CanPlay(hi, k, sac, nullptr)) { col = k; break; }
        if (col < 0 || !bt.CanPlay(hi, col, sac, nullptr)) continue;
        float value = (float)c.Rating() * 1.1f - sacCost - boneCost + (c.cost == CostType::FREE ? 0.3f : 0);
        if (c.name == "Minnow") value = bt.board.cell[R_YOU_FRONT][col].used ? -1.0f : 0.8f;
        if (value > bestScore) { bestScore = value; handIdx = hi; lane = col; sacs = sac; }
    }
    return handIdx >= 0;
}

// one pack item worth using now, in the active frame: (slot, row, lane)
bool BestItem(const Battle& bt, int& slot, int& r, int& c) {
    const Board& b = bt.board;
    for (int i = 0; i < (int)bt.items.size(); i++) {
        PackItem k = (PackItem)bt.items[i];
        slot = i; r = -1; c = -1;
        switch (k) {
            case PackItem::HARPOON: {
                int bestS = -1;
                for (int rr : {R_FOE_FRONT, R_FOE_QUEUE}) for (int cc = 0; cc < COLS; cc++)
                    if (b.cell[rr][cc].used && b.cell[rr][cc].card.hp <= 3) {
                        int sv = b.EffStrength(rr, cc) * 2 + b.cell[rr][cc].card.Rating() / 3 + (rr == R_FOE_FRONT ? 2 : 0);
                        if (sv > bestS) { bestS = sv; r = rr; c = cc; }
                    }
                if (bestS >= 5) return true;
            } break;
            case PackItem::BANDAGE:
                for (int cc = 0; cc < COLS; cc++)
                    if (b.cell[R_YOU_FRONT][cc].used && b.cell[R_FOE_FRONT][cc].used && b.EffStrength(R_FOE_FRONT, cc) >= b.cell[R_YOU_FRONT][cc].card.hp &&
                        b.cell[R_YOU_FRONT][cc].card.Rating() >= 6) { r = R_YOU_FRONT; c = cc; return true; }
                break;
            case PackItem::BOULDER:
                if ((int)bt.hand.size() < MAX_HAND)
                    for (int cc = 0; cc < COLS; cc++) if (!b.cell[R_YOU_FRONT][cc].used && b.cell[R_FOE_FRONT][cc].used && b.EffStrength(R_FOE_FRONT, cc) >= 2) return true;
                break;
            case PackItem::GOAT:
                if ((int)bt.hand.size() < MAX_HAND) {
                    int blood = 0;
                    for (int rr : {R_YOU_FRONT, R_YOU_BACK}) for (int cc = 0; cc < COLS; cc++) if (b.cell[rr][cc].used) blood += b.cell[rr][cc].card.BloodValue();
                    for (const Card& h : bt.hand) if (h.cost == CostType::BLOOD && h.costAmount >= 2 && bt.BloodNeeded(h) > blood) return true;
                }
                break;
            case PackItem::INK: {
                int str = 0;
                for (int cc = 0; cc < COLS; cc++) if (b.cell[R_FOE_FRONT][cc].used) str += b.EffStrength(R_FOE_FRONT, cc);
                if (str >= 5 && b.rules[1].tempTurns == 0) return true;
            } break;
            case PackItem::FISHHOOK:
                if ((int)bt.hand.size() < MAX_HAND)
                    for (int cc = 0; cc < COLS; cc++) if (b.cell[R_FOE_QUEUE][cc].used && b.cell[R_FOE_QUEUE][cc].card.Rating() >= 6) { r = R_FOE_QUEUE; c = cc; return true; }
                break;
            default: break;
        }
    }
    return false;
}

float DraftScore(const Zone& z, const Card& c) {
    int blood = 0, bones = 0;
    for (const Card& k : z.pool) { blood += k.cost == CostType::BLOOD; bones += k.cost == CostType::BONES; }
    float s = (float)c.Rating();
    if (c.cost == CostType::BLOOD && blood > bones + 4) s += 0.5f;   // lean into what you have
    if (c.cost == CostType::BONES && bones > blood) s += 0.5f;
    if (c.cost == CostType::BLOOD && c.costAmount >= 3 && blood < 6) s -= 1.5f;   // no fodder yet for the big ones
    return s;
}
std::vector<int> BotBuild(const Zone& z) {
    std::vector<int> idx((int)z.pool.size());
    for (int i = 0; i < (int)idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return z.pool[a].Rating() > z.pool[b].Rating(); });
    idx.resize(std::min((int)idx.size(), Tune().deckFromDraft));
    return idx;
}
}  // namespace

// ---------------------------------------------------------------- flipping
void FlipBoard(Board& b) {
    for (int c = 0; c < COLS; c++) { std::swap(b.cell[0][c], b.cell[3][c]); std::swap(b.cell[1][c], b.cell[2][c]); }
    b.scale = -b.scale;
    std::swap(b.bones[0], b.bones[1]);
    std::swap(b.rules[0], b.rules[1]);
    std::swap(b.returned[0], b.returned[1]);
    std::swap(b.itemsFound[0], b.itemsFound[1]);
    b.gold = 0; b.massive = false;
}
Event FlipEvent(Event e) {
    if (!RowEvent(e.type)) return e;
    if (e.r0 >= 0 && e.r0 < ROWS) e.r0 = ROWS - 1 - e.r0;
    if (e.r1 >= 0 && e.r1 < ROWS) e.r1 = ROWS - 1 - e.r1;
    if (e.type == Event::ScaleHit) e.c1 = -e.c1;
    return e;
}

// ---------------------------------------------------------------- the host
void NewMatch(State& s, uint32_t seed) {
    s = State();
    s.rng.Seed(seed ? seed : 1);
    for (int p = 0; p < 2; p++) { s.z[p].skin = p == 0 ? 0 : 1; s.z[p].preset = p; }
    Log(s, "Take your seats.");
}

int Actor(const State& s) {
    switch (s.phase) {
        case PH_SETUP: case PH_BUILD: case PH_SIDEBOARD: return 2;
        case PH_DRAFT: return Picker(s);
        case PH_PLAY: return (s.bt.turn == Turn::YOU_DRAW || s.bt.turn == Turn::YOU_MAIN) ? s.active : -1;
        default: return -1;
    }
}
bool Over(const State& s) { return s.phase == PH_MATCH_OVER; }

bool Legal(const State& s, int p, const Action& a, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (p < 0 || p > 1) return no("Not a player.");
    const Zone& z = s.z[p];
    const Tuning& T = Tune();
    switch (a.kind) {
        case Action::EMOTE: return (a.a >= 0 && a.a < EM_COUNT && z.emoteCd <= 0 && s.phase != PH_MATCH_OVER) || no("Not yet.");
        case Action::CONCEDE: return s.phase == PH_PLAY || no("There is no game to concede.");
        case Action::SET_MODE: return (s.phase == PH_SETUP && p == 0 && a.a >= 0 && a.a < MODE_COUNT) || no("Only the host picks the mode.");
        case Action::SET_SKIN: return (s.phase == PH_SETUP && a.a >= 0 && a.a < SkinCount()) || no("No such skin.");
        case Action::SET_ITEMS: return (s.phase == PH_SETUP && a.a >= 0 && a.a < (int)PackItem::COUNT && a.b >= 0 && a.b < (int)PackItem::COUNT) || no("No such item.");
        case Action::SET_PRESET: return (s.phase == PH_SETUP && a.a >= 0 && a.a < (int)Presets().size()) || no("No such deck.");
        case Action::SUBMIT_DECK: {
            if (s.phase != PH_SETUP || s.mode != MODE_CONSTRUCTED) return no("Decks are brought only to a constructed match.");
            if ((int)a.cards.size() > T.deckMax || (int)a.side.size() > T.sideboardMax) return no("Too many cards.");
            std::vector<Card> d, sd;
            for (auto& [id, ed] : a.cards) { if (id < 0 || id >= (int)Catalog().size() || ed < 0 || ed >= ED_COUNT) return no("An unknown card."); d.push_back(MakeCard(id, ed)); }
            for (auto& [id, ed] : a.side) { if (id < 0 || id >= (int)Catalog().size() || ed < 0 || ed >= ED_COUNT) return no("An unknown card."); sd.push_back(MakeCard(id, ed)); }
            return ValidateDeck(d, sd, why);
        }
        case Action::READY:
            if (s.phase == PH_SETUP) return (s.mode != MODE_CONSTRUCTED || z.submitted || a.a == 0) || no("Submit a deck first.");
            return s.phase == PH_SIDEBOARD || no("Nothing to be ready for.");
        case Action::PICK: return (s.phase == PH_DRAFT && Picker(s) == p && a.a >= 0 && a.a < (int)s.pack.size()) || no("Not your pick.");
        case Action::BUILD: {
            if (s.phase != PH_BUILD || z.submitted) return no("Not building.");
            if ((int)a.idx.size() < T.deckFromDraft || (int)a.idx.size() > std::min((int)z.pool.size(), T.deckMax)) return no("Keep at least 20.");
            std::vector<bool> seen(z.pool.size(), false);
            for (int i : a.idx) { if (i < 0 || i >= (int)z.pool.size() || seen[i]) return no("Each pick once."); seen[i] = true; }
            return true;
        }
        case Action::SWAP:
            if (s.phase != PH_SIDEBOARD || z.ready || z.swapsLeft <= 0) return no("No swaps left.");
            return (a.a >= 0 && a.a < (int)z.deckList.size() && a.b >= 0 && a.b < (int)z.sideboard.size()) || no("Pick a card on each side.");
        case Action::DRAW: return (s.phase == PH_PLAY && s.active == p && s.bt.turn == Turn::YOU_DRAW) || no("Not your upkeep.");
        case Action::BELL: return (s.phase == PH_PLAY && s.active == p && (s.bt.turn == Turn::YOU_DRAW || s.bt.turn == Turn::YOU_MAIN)) || no("Not your turn.");
        case Action::PLAY:
            if (s.phase != PH_PLAY || s.active != p) return no("Not your turn.");
            for (auto& sc : a.sacs) if (sc.first < R_YOU_FRONT || sc.first > R_YOU_BACK) return no("Sacrifice your own creatures.");
            return s.bt.CanPlay(a.a, a.b, a.sacs, why);
        case Action::ITEM: {
            if (s.phase != PH_PLAY || s.active != p || s.bt.turn != Turn::YOU_MAIN) return no("Not your turn.");
            if (a.a < 0 || a.a >= (int)s.bt.items.size()) return no("No such item.");
            Battle dry = s.bt; Events e;
            return dry.UseItem(a.a, a.b, a.c, e) || no("That item can't go there.");
        }
        default: return no("Unknown action.");
    }
}

bool Apply(State& s, int p, const Action& a) {
    if (!Legal(s, p, a)) return false;
    Zone& z = s.z[p];
    const Tuning& T = Tune();
    switch (a.kind) {
        case Action::EMOTE: z.emote = a.a; z.emoteSeq++; z.emoteCd = T.emoteCooldown; return true;
        case Action::CONCEDE: Log(s, "Player " + std::to_string(p + 1) + " concedes."); EndGame(s, Opp(p)); return true;
        case Action::SET_MODE:
            if (s.mode != a.a) { s.mode = a.a; for (auto& zz : s.z) { zz.ready = false; zz.submitted = false; } }
            return true;
        case Action::SET_SKIN: z.skin = a.a; return true;
        case Action::SET_ITEMS: z.loadout[0] = a.a; z.loadout[1] = a.b; return true;
        case Action::SET_PRESET: z.preset = a.a; return true;
        case Action::SUBMIT_DECK:
            z.deckList.clear(); z.sideboard.clear();
            for (auto& [id, ed] : a.cards) z.deckList.push_back(MakeCard(id, ed));
            for (auto& [id, ed] : a.side) z.sideboard.push_back(MakeCard(id, ed));
            z.submitted = true; z.ready = false;
            return true;
        case Action::READY:
            z.ready = a.a != 0;
            if (s.z[0].ready && s.z[1].ready) {
                if (s.phase == PH_SETUP) BeginMatch(s);
                else if (s.phase == PH_SIDEBOARD) { for (auto& zz : s.z) zz.ready = false; StartGame(s); }
            }
            return true;
        case Action::PICK: {
            Card c = s.pack[a.a];
            s.pack.erase(s.pack.begin() + a.a);
            z.pool.push_back(c);
            s.picks.push_back({p, c.id});
            s.picksInPack++;
            s.timer = T.pickSeconds; s.wait = 0;
            if (s.picksInPack >= T.packSize || s.pack.empty()) {
                s.packNo++;
                s.opener = Opp(s.opener);
                if (s.packNo >= T.draftPacks) StartBuild(s); else NewPack(s);
            }
            s.step++;
            return true;
        }
        case Action::BUILD: {
            std::vector<bool> in(z.pool.size(), false);
            for (int i : a.idx) in[i] = true;
            std::vector<Card> deck;
            z.sideboard.clear();
            for (int i = 0; i < (int)z.pool.size(); i++) {
                if (in[i]) deck.push_back(z.pool[i]);
                else if ((int)z.sideboard.size() < T.sideboardMax) z.sideboard.push_back(z.pool[i]);
            }
            z.deckList = WithStaples(deck);
            z.submitted = true;
            AfterBuild(s);
            return true;
        }
        case Action::SWAP:
            std::swap(z.deckList[a.a], z.sideboard[a.b]);
            z.swapsLeft--;
            return true;
        case Action::DRAW: {
            BeginStep(s); Events ev;
            s.bt.Draw(a.a != 0, ev, s.rng);
            EndStep(s, ev); AfterPlayStep(s);
            return true;
        }
        case Action::PLAY: {
            BeginStep(s); Events ev;
            s.bt.Play(a.a, a.b, a.sacs, ev, s.rng);
            EndStep(s, ev); AfterPlayStep(s);
            return true;
        }
        case Action::ITEM: {
            BeginStep(s); Events ev;
            int kind = s.bt.items[a.a];
            Event u; u.type = Event::ItemFound; u.amount = kind; u.r1 = a.b; u.c1 = a.c; u.text = std::string("Used: ") + ItemName(kind);
            ev.push_back(u);
            s.bt.UseItem(a.a, a.b, a.c, ev);
            z.itemsUsed++;
            EndStep(s, ev); AfterPlayStep(s);
            return true;
        }
        case Action::BELL:
            if (s.bt.turn == Turn::YOU_DRAW) s.bt.turn = Turn::YOU_MAIN;
            s.bt.EndTurn();
            if (T.firstHoldsFire && s.round == 1 && s.active == s.firstPlayer) s.bt.turn = Turn::YOU_END;
            s.wait = 0;
            s.ev.clear(); s.step++;
            return true;
        default: return false;
    }
}

Action Bot(const State& s, int p) {
    Action a;
    const Zone& z = s.z[p];
    switch (s.phase) {
        case PH_SETUP:
            if (s.mode == MODE_CONSTRUCTED && !z.submitted) {
                a.kind = Action::SUBMIT_DECK;
                for (const Card& c : PresetDeck(z.preset)) a.cards.push_back({c.id, c.edition});
                return a;
            }
            a.kind = Action::READY; a.a = 1; return a;
        case PH_DRAFT: {
            a.kind = Action::PICK; a.a = 0;
            float best = -1e9f;
            for (int i = 0; i < (int)s.pack.size(); i++) { float sc = DraftScore(z, s.pack[i]) - 0.01f * i; if (sc > best) { best = sc; a.a = i; } }
            return a;
        }
        case PH_BUILD: a.kind = Action::BUILD; a.idx = BotBuild(z); return a;
        case PH_SIDEBOARD: a.kind = Action::READY; a.a = 1; return a;
        case PH_PLAY: {
            const Battle& bt = s.bt;
            if (bt.turn == Turn::YOU_DRAW) {
                int fodder = 0;
                for (auto& c : bt.hand) if (c.cost == CostType::FREE) fodder++;
                bool needBlood = false;
                for (auto& c : bt.hand) if (c.cost == CostType::BLOOD && c.costAmount >= 2) needBlood = true;
                a.kind = Action::DRAW;
                a.a = (!bt.deck.empty() && (bt.hand.size() < 3 || fodder >= 1 || !needBlood)) ? 1 : 0;
                return a;
            }
            int slot, r, c;
            if (BestItem(bt, slot, r, c)) { a.kind = Action::ITEM; a.a = slot; a.b = r; a.c = c; return a; }
            int hi, lane; std::vector<std::pair<int, int>> sacs;
            if (BestPlay(bt, hi, lane, sacs)) { a.kind = Action::PLAY; a.a = hi; a.b = lane; a.sacs = sacs; return a; }
            a.kind = Action::BELL; return a;
        }
        default: a.kind = Action::COUNT; return a;
    }
}

bool HostTick(State& s, float dt, uint32_t ai) {
    const Tuning& T = Tune();
    bool changed = false;
    for (auto& z : s.z) z.emoteCd = std::max(0.0f, z.emoteCd - dt);
    auto isAi = [&](int p) { return ((ai >> p) & 1u) != 0; };
    switch (s.phase) {
        case PH_SETUP:
            for (int p = 0; p < 2; p++) {
                if (!isAi(p) || s.z[p].ready) continue;
                Zone& z = s.z[p];
                if (!z.submitted && !z.ready) {   // an AI seat dresses once: a random deck, its skin and its items
                    z.preset = s.rng.I(0, (int)Presets().size() - 1);
                    z.skin = Presets()[z.preset].skin;
                    if (s.z[Opp(p)].skin == z.skin) z.skin = (z.skin + 5) % SkinCount();
                    z.loadout[0] = Presets()[z.preset].items[0]; z.loadout[1] = Presets()[z.preset].items[1];
                }
                Action a = Bot(s, p);
                changed |= Apply(s, p, a);
                if (s.phase != PH_SETUP) break;
            }
            break;
        case PH_DRAFT: {
            int p = Picker(s);
            s.timer -= dt;
            if (isAi(p)) { if ((s.wait += dt) >= T.aiThink) { s.wait = 0; changed |= Apply(s, p, Bot(s, p)); } }
            else if (s.timer <= 0) changed |= Apply(s, p, Bot(s, p));
        } break;
        case PH_BUILD:
            s.timer -= dt;
            for (int p = 0; p < 2 && s.phase == PH_BUILD; p++)
                if (!s.z[p].submitted && (isAi(p) || s.timer <= 0)) changed |= Apply(s, p, Bot(s, p));
            break;
        case PH_SIDEBOARD:
            s.timer -= dt;
            for (int p = 0; p < 2 && s.phase == PH_SIDEBOARD; p++)
                if (!s.z[p].ready && (isAi(p) || s.timer <= 0)) { Action a; a.kind = Action::READY; a.a = 1; changed |= Apply(s, p, a); }
            break;
        case PH_PLAY: {
            Turn t = s.bt.turn;
            if (t == Turn::YOU_COMBAT || t == Turn::YOU_END) {   // the host paces combat: one lane per step
                if ((s.wait += dt) >= T.stepSeconds) {
                    s.wait = 0;
                    BeginStep(s); Events ev;
                    s.bt.Advance(ev, s.rng);
                    EndStep(s, ev); AfterPlayStep(s);
                    changed = true;
                }
                break;
            }
            int p = s.active;
            s.timer -= dt;
            if (isAi(p)) {
                if ((s.wait += dt) >= T.aiThink) { s.wait = 0; changed |= Apply(s, p, Bot(s, p)); }
            } else if (s.timer <= 0) {   // the clock ran out: the upkeep draws from the deck, then the bell rings
                if (s.bt.turn == Turn::YOU_DRAW) { Action d; d.kind = Action::DRAW; d.a = 1; Apply(s, p, d); }
                if (s.phase == PH_PLAY && s.active == p) { Action b; b.kind = Action::BELL; Apply(s, p, b); Log(s, "Time: the bell rings itself."); }
                changed = true;
            }
        } break;
        case PH_GAME_OVER:
            if ((s.wait += dt) >= T.gameOverPause) {
                s.wait = 0;
                int need = RulesOf(s.mode).gamesToWin;
                if (s.z[0].gamesWon >= need || s.z[1].gamesWon >= need) { s.matchWinner = s.z[0].gamesWon > s.z[1].gamesWon ? 0 : 1; s.phase = PH_MATCH_OVER; }
                else if (s.gameNo >= T.maxGames) { s.matchWinner = s.z[0].gamesWon == s.z[1].gamesWon ? 2 : (s.z[0].gamesWon > s.z[1].gamesWon ? 0 : 1); s.phase = PH_MATCH_OVER; }
                else if (RulesOf(s.mode).sideboard && (!s.z[0].sideboard.empty() || !s.z[1].sideboard.empty())) {
                    s.phase = PH_SIDEBOARD; s.timer = T.sideboardSeconds;
                    for (auto& z : s.z) { z.ready = false; z.swapsLeft = T.swapsBetweenGames; }
                } else StartGame(s);
                if (s.phase == PH_MATCH_OVER) Log(s, s.matchWinner == 2 ? "The match is drawn." : "Player " + std::to_string(s.matchWinner + 1) + " wins the match.");
                changed = true;
            }
            break;
        default: break;
    }
    return changed;
}

// ---------------------------------------------------------------- snapshots
namespace {
void WCard(Writer& w, const Card& c) {
    w.U8((uint8_t)c.id); w.U8((uint8_t)c.edition);
    w.U8((uint8_t)(int8_t)c.strength); w.U8((uint8_t)(int8_t)c.defense); w.U8((uint8_t)(int8_t)c.weight);
    w.U8((uint8_t)c.cost); w.U8((uint8_t)c.costAmount);
    w.U8((uint8_t)c.sigils.size()); for (Sigil g : c.sigils) w.U8((uint8_t)g);
    w.U8((uint8_t)(int8_t)std::clamp(c.hp, -127, 127)); w.U8((uint8_t)std::min(c.age, 255)); w.U8((uint8_t)(int8_t)c.dir);
}
bool RCard(Reader& r, Card& c) {
    int id = r.U8();
    if (id >= (int)Catalog().size()) { r.bad = true; return false; }
    c = MakeCard(id, (int)r.U8());
    c.strength = (int8_t)r.U8(); c.defense = (int8_t)r.U8(); c.weight = (int8_t)r.U8();
    c.cost = (CostType)r.U8(); c.costAmount = r.U8();
    int n = r.U8(); c.sigils.clear();
    for (int i = 0; i < n && i < 8; i++) c.sigils.push_back((Sigil)std::min<uint32_t>(r.U8(), (uint32_t)Sigil::COUNT - 1));
    c.hp = (int8_t)r.U8(); c.age = r.U8(); c.dir = (int8_t)r.U8();
    return !r.bad;
}
void WCards(Writer& w, const std::vector<Card>& v) { w.VarU((uint32_t)v.size()); for (const Card& c : v) WCard(w, c); }
bool RCards(Reader& r, std::vector<Card>& v) {
    uint32_t n = r.VarU(); if (n > 64) { r.bad = true; return false; }
    v.resize(n); for (auto& c : v) if (!RCard(r, c)) return false;
    return true;
}
uint8_t Rel(int who, int f) { return who < 0 ? 255 : who == 2 ? 2 : (uint8_t)(who == f ? 0 : 1); }
}  // namespace

void Serialize(const State& s, int viewer, Writer& w) {
    bool player = viewer == 0 || viewer == 1, full = viewer == FULL;
    int f = player ? viewer : 0;          // whose frame (a spectator sits behind player 0)
    int o = Opp(f);
    bool showMine = player || full;
    w.U8(1);                              // format
    w.U8(player ? (uint8_t)viewer : full ? 254 : 255);
    w.U8((uint8_t)s.mode); w.U8((uint8_t)s.phase); w.U8((uint8_t)s.gameNo); w.U8((uint8_t)s.round);
    w.U8((s.active == f ? 1 : 0) | (s.suddenDeath ? 2 : 0));
    w.U8(Rel(Actor(s), f)); w.U8((uint8_t)s.bt.turn);
    w.U8(Rel(s.gameWinner, f)); w.U8(Rel(s.matchWinner, f)); w.U8(Rel(s.firstPlayer, f));
    w.U16((uint32_t)std::clamp((int)std::ceil(s.timer * 10), 0, 65535));
    // the board, in the viewer's frame
    Board b = s.bt.board;
    if (s.phase == PH_PLAY || s.phase == PH_GAME_OVER) { if (s.active != f) FlipBoard(b); }
    else b = Board();
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { w.U8(b.cell[r][c].used); if (b.cell[r][c].used) WCard(w, b.cell[r][c].card); }
    w.U8((uint8_t)(int8_t)b.scale); w.U8((uint8_t)b.bones[0]); w.U8((uint8_t)b.bones[1]);
    for (int k = 0; k < 2; k++) { w.U8((uint8_t)(int8_t)b.rules[k].tempStr); w.U8((uint8_t)b.rules[k].tempTurns); }
    // both players: only what is public
    for (int who : {f, o}) {
        const Zone& z = s.z[who];
        bool live = s.phase == PH_PLAY || s.phase == PH_GAME_OVER;
        w.U8(live ? (uint8_t)DeckOf(s, who).size() : (uint8_t)z.deckList.size());
        w.U8(live ? (uint8_t)HandOf(s, who).size() : 0);
        w.U8(live ? (uint8_t)ItemsOf(s, who).size() : 0);
        w.U8((uint8_t)z.itemsUsed); w.U8((uint8_t)z.undyingUsed); w.U8((uint8_t)z.gamesWon); w.U8((uint8_t)z.skin);
        w.U8((z.ready ? 1 : 0) | (z.submitted ? 2 : 0)); w.U8((uint8_t)(z.emote + 1)); w.U16((uint32_t)z.emoteSeq);
    }
    // mine
    const Zone& me = s.z[f];
    bool live = s.phase == PH_PLAY || s.phase == PH_GAME_OVER;
    WCards(w, showMine && live ? HandOf(s, f) : std::vector<Card>{});
    const std::vector<int>& myItems = live ? ItemsOf(s, f) : std::vector<int>{};
    w.U8(showMine ? (uint8_t)myItems.size() : 0); if (showMine) for (int k : myItems) w.U8((uint8_t)k);
    WCards(w, showMine ? me.pool : std::vector<Card>{});
    WCards(w, showMine ? me.deckList : std::vector<Card>{});
    WCards(w, showMine ? me.sideboard : std::vector<Card>{});
    w.U8(showMine ? (uint8_t)me.swapsLeft : 0);
    // the draft is open: the pack and every pick are public
    WCards(w, s.phase == PH_DRAFT ? s.pack : std::vector<Card>{});
    w.U8((uint8_t)s.packNo); w.U8(s.phase == PH_DRAFT ? Rel(Picker(s), f) : 255);
    w.VarU((uint32_t)s.picks.size()); for (auto& [who, id] : s.picks) { w.U8(Rel(who, f)); w.U8((uint8_t)id); }
    // the last step, in the viewer's frame; someone else's draws stay face down
    w.U32(s.step); w.U8(Rel(s.evActor, f)); w.U8(Rel(s.bigHitSide, f));
    w.VarU((uint32_t)s.ev.size());
    for (const Event& e0 : s.ev) {
        Event e = (s.evFrame == f) ? e0 : FlipEvent(e0);
        bool hidden = (e.type == Event::Drew && (s.evActor != f || !showMine)) ||
                      (e.type == Event::ItemFound && e.text == "Found" && (!showMine || !(e.r1 >= R_YOU_FRONT)));
        w.U8((uint8_t)e.type); w.U8((uint8_t)(int8_t)e.r0); w.U8((uint8_t)(int8_t)e.c0); w.U8((uint8_t)(int8_t)e.r1); w.U8((uint8_t)(int8_t)e.c1);
        w.U16((uint16_t)(int16_t)(hidden ? -1 : e.amount)); w.Str(e.text);
    }
    w.U8((uint8_t)s.log.size()); for (auto& l : s.log) w.Str(l);
    // (FULL only: the opponent's hand, for a debugger or a replay; never sent over the network)
    if (full) WCards(w, live ? HandOf(s, o) : std::vector<Card>{});
}

bool ReadView(Reader& r, View& v) {
    v = View();
    if (r.U8() != 1) return false;
    int me = r.U8(); v.me = me < 2 ? me : -1;
    v.mode = r.U8(); v.phase = r.U8(); v.gameNo = r.U8(); v.round = r.U8();
    int fl = r.U8(); v.myTurn = fl & 1; v.suddenDeath = (fl & 2) != 0;
    auto rel = [](int x) { return x == 255 ? -1 : x; };
    v.actor = rel(r.U8()); v.turn = r.U8();
    v.gameWinner = rel(r.U8()); v.matchWinner = rel(r.U8()); v.firstPlayer = rel(r.U8());
    v.timer = r.U16() / 10.0f;
    for (int rr = 0; rr < ROWS; rr++) for (int c = 0; c < COLS; c++) { v.board.cell[rr][c].used = r.U8() != 0; if (v.board.cell[rr][c].used && !RCard(r, v.board.cell[rr][c].card)) return false; }
    v.board.scale = (int8_t)r.U8(); v.board.bones[0] = r.U8(); v.board.bones[1] = r.U8();
    for (int k = 0; k < 2; k++) { v.board.rules[k].tempStr = (int8_t)r.U8(); v.board.rules[k].tempTurns = r.U8(); }
    v.bones[0] = v.board.bones[0]; v.bones[1] = v.board.bones[1];
    for (int k = 0; k < 2; k++) {
        v.deckCount[k] = r.U8(); v.handCount[k] = r.U8(); v.itemCount[k] = r.U8();
        v.itemsUsed[k] = r.U8(); v.undyingUsed[k] = r.U8(); v.gamesWon[k] = r.U8(); v.skin[k] = r.U8();
        v.ready[k] = (r.U8() & 1) != 0; v.emote[k] = (int)r.U8() - 1; v.emoteSeq[k] = r.U16();
    }
    if (!RCards(r, v.hand)) return false;
    int ni = r.U8(); for (int i = 0; i < ni; i++) v.items.push_back(r.U8());
    if (!RCards(r, v.pool) || !RCards(r, v.deckList) || !RCards(r, v.sideboard)) return false;
    v.swapsLeft = r.U8();
    if (!RCards(r, v.pack)) return false;
    v.packNo = r.U8(); v.picker = rel(r.U8());
    uint32_t np = r.VarU(); if (np > 256) return false;
    for (uint32_t i = 0; i < np; i++) { int who = r.U8(); int id = r.U8(); v.picks.push_back({who, id}); }
    v.step = r.U32(); v.evActor = rel(r.U8()); v.bigHit = rel(r.U8());
    uint32_t ne = r.VarU(); if (ne > 512) return false;
    for (uint32_t i = 0; i < ne; i++) {
        Event e; e.type = (Event::Type)r.U8(); e.r0 = (int8_t)r.U8(); e.c0 = (int8_t)r.U8(); e.r1 = (int8_t)r.U8(); e.c1 = (int8_t)r.U8();
        e.amount = (int16_t)r.U16(); e.text = r.Str(); v.ev.push_back(e);
    }
    int nl = r.U8(); for (int i = 0; i < nl; i++) v.log.push_back(r.Str());
    return !r.bad;
}

// ---------------------------------------------------------------- actions on the wire
void WriteAction(const Action& a, Writer& w) {
    w.U8(a.kind);
    switch (a.kind) {
        case Action::SET_MODE: case Action::SET_SKIN: case Action::SET_PRESET: case Action::READY: case Action::PICK:
        case Action::DRAW: case Action::EMOTE: w.U8((uint8_t)a.a); break;
        case Action::SET_ITEMS: case Action::SWAP: w.U8((uint8_t)a.a); w.U8((uint8_t)a.b); break;
        case Action::PLAY: w.U8((uint8_t)a.a); w.U8((uint8_t)a.b); w.U8((uint8_t)a.sacs.size()); for (auto& sc : a.sacs) { w.U8((uint8_t)sc.first); w.U8((uint8_t)sc.second); } break;
        case Action::ITEM: w.U8((uint8_t)a.a); w.U8((uint8_t)(int8_t)a.b); w.U8((uint8_t)(int8_t)a.c); break;
        case Action::SUBMIT_DECK:
            w.U8((uint8_t)a.cards.size()); for (auto& [id, ed] : a.cards) { w.U8((uint8_t)id); w.U8((uint8_t)ed); }
            w.U8((uint8_t)a.side.size()); for (auto& [id, ed] : a.side) { w.U8((uint8_t)id); w.U8((uint8_t)ed); }
            break;
        case Action::BUILD: w.U8((uint8_t)a.idx.size()); for (int i : a.idx) w.U8((uint8_t)i); break;
        default: break;
    }
}
bool ReadAction(Reader& r, Action& a) {
    a = Action();
    uint32_t k = r.U8();
    if (r.bad || k >= Action::COUNT) return false;
    a.kind = (Action::Kind)k;
    switch (a.kind) {
        case Action::SET_MODE: case Action::SET_SKIN: case Action::SET_PRESET: case Action::READY: case Action::PICK:
        case Action::DRAW: case Action::EMOTE: a.a = r.U8(); break;
        case Action::SET_ITEMS: case Action::SWAP: a.a = r.U8(); a.b = r.U8(); break;
        case Action::PLAY: { a.a = r.U8(); a.b = r.U8(); int n = r.U8(); if (n > 8) return false; for (int i = 0; i < n; i++) { int rr = r.U8(); int cc = r.U8(); a.sacs.push_back({rr, cc}); } } break;
        case Action::ITEM: a.a = r.U8(); a.b = (int8_t)r.U8(); a.c = (int8_t)r.U8(); break;
        case Action::SUBMIT_DECK: {
            int n = r.U8(); if (n > 40) return false; for (int i = 0; i < n; i++) { int id = r.U8(); int ed = r.U8(); a.cards.push_back({id, ed}); }
            int m = r.U8(); if (m > 12) return false; for (int i = 0; i < m; i++) { int id = r.U8(); int ed = r.U8(); a.side.push_back({id, ed}); }
        } break;
        case Action::BUILD: { int n = r.U8(); if (n > 40) return false; for (int i = 0; i < n; i++) a.idx.push_back(r.U8()); } break;
        default: break;
    }
    return !r.bad;
}

// ---------------------------------------------------------------- self-tests and the simulator
namespace {
// Drive a whole match with two AI seats on the host's real clock, calling `each` after every change.
template <typename F> void DriveMatch(State& s, uint32_t seed, int mode, F each) {
    NewMatch(s, seed);
    s.mode = mode;
    for (int guard = 0; guard < 200000 && !Over(s); guard++)
        if (HostTick(s, 1.0f, 3u)) each(s);
}
}

SimReport Simulate(int matches, int mode, uint32_t seed) {
    SimReport R;
    long long rounds = 0, steps = 0;
    for (int m = 0; m < matches; m++) {
        State s;
        int lastPhase = -1, games = 0;
        uint32_t stepAtStart = 0;
        DriveMatch(s, seed + 7919u * m, mode, [&](State& st) {
            if (st.phase == PH_PLAY && lastPhase != PH_PLAY) stepAtStart = st.step;
            if (st.phase == PH_GAME_OVER && lastPhase == PH_PLAY) {
                games++;
                R.games++;
                rounds += st.round;
                steps += st.step - stepAtStart;
                if (st.gameWinner == 2) R.ties++;
                else if (st.gameWinner == st.thisGameFirst) R.firstWins++;
                if (st.suddenDeath) R.suddenDeaths++;
                if (st.round > Tune().roundLimit) R.roundLimitGames++;
                if (mode == MODE_QUICK && st.gameWinner < 2) {
                    int a = st.z[0].preset, b = st.z[1].preset, w = st.gameWinner;
                    R.presetGames[a][b]++; R.presetGames[b][a]++;
                    if (w == 0) R.presetWins[a][b]++; else R.presetWins[b][a]++;
                }
            }
            lastPhase = st.phase;
        });
        R.matches++;
    }
    if (R.games) { R.avgRounds = (double)rounds / R.games; R.avgSteps = (double)steps / R.games; }
    return R;
}

bool VerifyNoLeak(int matches, uint32_t seed, std::string* why) {
    bool ok = true;
    int checks = 0;
    for (int m = 0; m < matches && ok; m++) {
        State s;
        int mode = m % MODE_COUNT;
        DriveMatch(s, seed + 101u * m, mode, [&](State& st) {
            if (!ok) return;
            for (int viewer : {0, 1, -1}) {
                Writer a; Serialize(st, viewer, a);
                State t = st;
                Rng mess; mess.Seed(st.step * 2654435761u + viewer + 7);
                // change everything this viewer must not see: the other hand(s), deck order, items, the deck/sideboard split, the host's rng
                for (int q = 0; q < 2; q++) {
                    if (q == viewer) continue;
                    auto& h = HandOf(t, q);
                    for (auto& c : h) c = MakeCard(mess.I(1, 32), mess.I(0, 2));
                    mess.Shuffle(DeckOf(t, q));
                    for (auto& c : DeckOf(t, q)) if (mess.C(0.3f)) c = MakeCard(mess.I(1, 32));
                    for (auto& k : ItemsOf(t, q)) k = mess.I(0, (int)PackItem::COUNT - 1);
                    t.z[q].loadout[0] = mess.I(0, 5); t.z[q].loadout[1] = mess.I(0, 5); t.z[q].preset = mess.I(0, 5);
                    if (!t.z[q].deckList.empty() && !t.z[q].sideboard.empty()) std::swap(t.z[q].deckList[0], t.z[q].sideboard[0]);
                    mess.Shuffle(t.z[q].deckList);
                }
                t.rng.Seed(mess.Next());
                Writer b; Serialize(t, viewer, b);
                checks++;
                if (a.b != b.b) {
                    ok = false;
                    if (why) *why = "viewer " + std::to_string(viewer) + " saw hidden information at step " + std::to_string(st.step) + " (phase " + std::to_string(st.phase) + ")";
                    return;
                }
            }
        });
    }
    if (ok && why) *why = std::to_string(checks) + " snapshots unchanged by hidden information";
    return ok;
}

bool VerifyViews(int matches, uint32_t seed, std::string* why) {
    bool ok = true;
    int checks = 0;
    auto fail = [&](const std::string& m) { if (ok && why) *why = m; ok = false; };
    for (int m = 0; m < matches && ok; m++) {
        State s;
        DriveMatch(s, seed + 31u * m, m % MODE_COUNT, [&](State& st) {
            if (!ok) return;
            View v[2];
            for (int p = 0; p < 2; p++) {
                Writer w; Serialize(st, p, w);
                Reader r(w.b);
                if (!ReadView(r, v[p]) || !r.Done()) return fail("a snapshot did not decode exactly");
            }
            checks++;
            if (v[0].phase != v[1].phase || v[0].round != v[1].round || v[0].step != v[1].step) return fail("the two players disagree on the phase");
            if (v[0].board.scale != -v[1].board.scale) return fail("the scales do not mirror");
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
                const Cell &a = v[0].board.cell[r][c], &b = v[1].board.cell[ROWS - 1 - r][c];
                if (a.used != b.used || (a.used && (a.card.id != b.card.id || a.card.hp != b.card.hp))) return fail("the boards do not mirror");
            }
            for (int p = 0; p < 2; p++) {
                if (v[p].handCount[0] != (int)v[p].hand.size() && (v[p].phase == PH_PLAY)) return fail("my hand count is wrong");
                if (v[p].handCount[1] != v[1 - p].handCount[0]) return fail("the opponent's hand count is wrong");
                if (v[p].myTurn == v[1 - p].myTurn && v[p].phase == PH_PLAY) return fail("both think it is their turn");
            }
            if (st.phase == PH_PLAY)
                for (int p = 0; p < 2; p++)
                    for (const Event& e : v[p].ev) if (e.type == Event::Drew && v[p].evActor == 1 && e.amount != -1) return fail("an opponent's draw was shown");
        });
    }
    if (ok && why) *why = std::to_string(checks) + " snapshots decoded and mirrored";
    return ok;
}

}  // namespace duel
}  // namespace flats
