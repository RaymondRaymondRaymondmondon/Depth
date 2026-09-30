// Scuttle's rules (see scuttle.h). The host is the only authority: clients send Actions, the host applies them and sends
// each seat back what it may see.
#include "scuttle.h"
#include <algorithm>
#include <cstdio>

namespace scuttle {

const float TURN_SECONDS = 20.0f, RESPONSE_SECONDS = 6.0f;

static const CardDef DECK[C_COUNT] = {
    {"Scuttle 1", "Your crab moves forward 1.", 10},
    {"Scuttle 2", "Your crab moves forward 2.", 8},
    {"Scuttle 3", "Your crab moves forward 3.", 4},
    {"Sidestep", "Swap your crab with a crab one space ahead.", 6},
    {"Wave", "Every crab except yours moves back 1.", 5},
    {"Pinch", "Target crab skips its owner's next move card.", 5},
    {"Shell", "Play when a card is aimed at you: cancel it.", 5},
    {"Current", "Every crab moves forward 2; the crab in last place moves 3.", 4},
    {"Gull", "The crab in the lead moves back 2.", 4},
    {"Rock", "Place a rock on a space: the next crab to land there stops for a turn.", 3},
    {"Molt", "Discard your hand and draw 5.", 3},
    {"Tide Rush", "Your crab moves 1 for each crab ahead of it.", 3},
};
const CardDef& Card(int c) { return DECK[std::clamp(c, 0, C_COUNT - 1)]; }

static uint32_t Rnd(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
static void Log(State& s, const std::string& l) { s.log.push_back(l); if (s.log.size() > 8) s.log.erase(s.log.begin()); }
static std::string Crab(int seat) { static const char* N[MAX_SEATS] = {"Red", "Blue", "Gold", "Green"}; return seat >= 0 && seat < MAX_SEATS ? N[seat] : "?"; }

static void Shuffle(State& s, std::vector<uint8_t>& v) { for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[Rnd(s.rng) % (i + 1)]); }
static int Draw(State& s) {
    if (s.deck.empty()) { s.deck.swap(s.discard); Shuffle(s, s.deck); }
    if (s.deck.empty()) return -1;
    int c = s.deck.back(); s.deck.pop_back(); return c;
}
static void Refill(State& s, int seat) {
    Seat& p = s.seats[seat];
    while ((int)p.hand.size() < HAND) { int c = Draw(s); if (c < 0) break; p.hand.push_back((uint8_t)c); }
    p.handCount = (int)p.hand.size();
}
static void StartRound(State& s) {
    s.deck.clear(); s.discard.clear();
    for (int c = 0; c < C_COUNT; c++) for (int k = 0; k < DECK[c].copies; k++) s.deck.push_back((uint8_t)c);
    Shuffle(s, s.deck);
    s.rock = -1; s.bets.clear();
    for (int i = 0; i < s.nSeats; i++) { Seat& p = s.seats[i]; p.pos = 0; p.skipMove = false; p.hand.clear(); p.betsLeft = BETS_PER_ROUND; Refill(s, i); }
    s.turn = s.firstPlayer; s.phase = PH_PLAY; s.timer = TURN_SECONDS;
    s.pendCard = s.pendPlayer = s.pendTarget = -1; s.respondMask = s.shellMask = 0;
    s.roundWinner = s.roundSecond = -1;
    Log(s, "Round " + std::to_string(s.round) + ": the crabs line up at the tide line's far end.");
}
void NewMatch(State& s, int nSeats, uint32_t seed, int winsNeeded) {
    s = State{};
    s.nSeats = std::clamp(nSeats, 2, MAX_SEATS);
    s.winsNeeded = winsNeeded;
    s.rng = seed ? seed : 1;
    for (int i = 0; i < s.nSeats; i++) s.seats[i].used = true;
    s.firstPlayer = (int)(Rnd(s.rng) % s.nSeats);
    StartRound(s);
}

int Actor(const State& s) {
    switch (s.phase) {
        case PH_PLAY: case PH_BET: return s.turn;
        case PH_RESPONSE: for (int i = 0; i < s.nSeats; i++) if (s.respondMask & (1 << i)) return i; return -1;
        default: return -1;
    }
}
bool NeedsTarget(int card) { return card == C_SIDESTEP || card == C_PINCH || card == C_ROCK; }
std::vector<int> Targets(const State& s, int seat, int card) {
    std::vector<int> t;
    if (card == C_SIDESTEP) { for (int i = 0; i < s.nSeats; i++) if (i != seat && s.seats[i].pos == s.seats[seat].pos + 1) t.push_back(i); }
    else if (card == C_PINCH) { for (int i = 0; i < s.nSeats; i++) if (i != seat) t.push_back(i); }
    else if (card == C_ROCK) { for (int sp = 1; sp < TRACK; sp++) t.push_back(sp); }
    return t;
}

bool Legal(const State& s, const Action& a, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (a.seat >= s.nSeats) return no("no such seat");
    if (a.kind == Action::NEXTROUND) return s.phase == PH_ROUND_OVER ? true : no("the round isn't over");
    if (Actor(s) != a.seat) return no("not your move");
    const Seat& p = s.seats[a.seat];
    switch (a.kind) {
        case Action::PLAY: {
            if (s.phase != PH_PLAY) return no("not the time to play a card");
            if (a.handIdx >= p.hand.size()) return no("no such card");
            int c = p.hand[a.handIdx];
            if (NeedsTarget(c)) { auto t = Targets(s, a.seat, c); if (t.empty() ? a.target != -1 : std::find(t.begin(), t.end(), (int)a.target) == t.end()) return no("pick a target"); } // (a Sidestep with no crab just ahead plays with no effect)
            return true;
        }
        case Action::RESPOND:
            if (s.phase != PH_RESPONSE) return no("nothing to answer");
            if (a.shell && std::find(p.hand.begin(), p.hand.end(), (uint8_t)C_SHELL) == p.hand.end()) return no("no Shell in hand");
            return true;
        case Action::BET:
            if (s.phase != PH_BET) return no("not the time to bet");
            if (p.betsLeft == 0) return no("no bet tokens left");
            if (a.target < 0 || a.target >= s.nSeats) return no("bet on a crab");
            return true;
        case Action::SKIPBET: return s.phase == PH_BET ? true : no("not the time to bet");
        default: return no("unknown action");
    }
}

// moving a crab: a move card on a Pinched crab does nothing; landing on a rock stops the crab for a turn
static void Move(State& s, int seat, int delta, bool moveCard) {
    Seat& p = s.seats[seat];
    if (moveCard && p.skipMove) { p.skipMove = false; Log(s, Crab(seat) + " is held fast and doesn't move."); return; }
    int np = std::clamp((int)p.pos + delta, 0, TRACK);
    p.pos = (uint8_t)np;
    if (delta > 0 && np == s.rock && np < TRACK) { s.rock = -1; p.skipMove = true; Log(s, Crab(seat) + " lands on the rock and is stuck for a turn."); }
}
// which seats a card is aimed at (a Shell can answer): other crabs it pushes back or holds
static uint8_t AimedMask(const State& s, int seat, int card, int target) {
    uint8_t m = 0;
    switch (card) {
        case C_SIDESTEP: case C_PINCH: if (target >= 0 && target != seat) m |= 1 << target; break;
        case C_WAVE: for (int i = 0; i < s.nSeats; i++) if (i != seat) m |= 1 << i; break;
        case C_GULL: { int lead = 0; for (int i = 0; i < s.nSeats; i++) lead = std::max(lead, (int)s.seats[i].pos); for (int i = 0; i < s.nSeats; i++) if (i != seat && s.seats[i].pos == lead) m |= 1 << i; break; }
        default: break;
    }
    return m;
}
static void CheckRoundEnd(State& s, int actor) {
    std::vector<int> atEnd;
    for (int k = 0; k < s.nSeats; k++) { int i = (actor + k) % s.nSeats; if (s.seats[i].pos >= TRACK) atEnd.push_back(i); }
    if (atEnd.empty()) return;
    int win = atEnd[0], second = -1, best = -1;
    for (int k = 0; k < s.nSeats; k++) { int i = (actor + k) % s.nSeats; if (i != win && (int)s.seats[i].pos > best) { best = s.seats[i].pos; second = i; } }
    s.roundWinner = win; s.roundSecond = second;
    s.seats[win].roundWins++; s.seats[win].points += 2;
    for (const Bet& b : s.bets) s.seats[b.owner].points += b.crab == win ? 3 : b.crab == second ? 1 : -1;
    Log(s, Crab(win) + " reaches the tide line and wins round " + std::to_string(s.round) + "!");
    s.phase = s.seats[win].roundWins >= s.winsNeeded ? PH_MATCH_OVER : PH_ROUND_OVER;
    if (s.phase == PH_MATCH_OVER) { s.matchWinner = win; Log(s, Crab(win) + " wins the match."); }
    s.timer = 0;
}
static void Resolve(State& s) {
    int seat = s.pendPlayer, card = s.pendCard, target = s.pendTarget;
    auto exempt = [&](int i) { return (s.shellMask >> i) & 1; };
    switch (card) {
        case C_SCUTTLE1: case C_SCUTTLE2: case C_SCUTTLE3: Move(s, seat, card - C_SCUTTLE1 + 1, true); break;
        case C_SIDESTEP: if (target >= 0 && !exempt(target)) { std::swap(s.seats[seat].pos, s.seats[target].pos); Log(s, Crab(seat) + " sidesteps past " + Crab(target) + "."); } break;
        case C_WAVE: for (int i = 0; i < s.nSeats; i++) if (i != seat && !exempt(i)) Move(s, i, -1, false); break;
        case C_PINCH: if (target >= 0 && !exempt(target)) { s.seats[target].skipMove = true; Log(s, Crab(target) + " is pinched!"); } break;
        case C_CURRENT: {
            int low = TRACK; for (int i = 0; i < s.nSeats; i++) low = std::min(low, (int)s.seats[i].pos);
            std::vector<int> last; for (int i = 0; i < s.nSeats; i++) if (s.seats[i].pos == low) last.push_back(i);
            for (int i = 0; i < s.nSeats; i++) Move(s, i, std::find(last.begin(), last.end(), i) != last.end() ? 3 : 2, false);
        } break;
        case C_GULL: { int lead = 0; for (int i = 0; i < s.nSeats; i++) lead = std::max(lead, (int)s.seats[i].pos);
            for (int i = 0; i < s.nSeats; i++) if (s.seats[i].pos == lead && !exempt(i)) Move(s, i, -2, false); Log(s, "A gull swoops on the leader."); } break;
        case C_ROCK: s.rock = target; Log(s, Crab(seat) + " drops a rock on space " + std::to_string(target) + "."); break;
        case C_MOLT: { Seat& p = s.seats[seat]; for (uint8_t c : p.hand) s.discard.push_back(c); p.hand.clear(); Refill(s, seat); Log(s, Crab(seat) + " molts: a fresh hand."); } break;
        case C_TIDERUSH: { int ahead = 0; for (int i = 0; i < s.nSeats; i++) if (s.seats[i].pos > s.seats[seat].pos) ahead++; Move(s, seat, ahead, true); } break;
        default: break;
    }
    for (int i = 0; i < s.nSeats; i++) if (exempt(i)) Log(s, Crab(i) + " ducks into its shell.");
    s.plays++; s.lastCard = card; s.lastPlayer = seat; s.lastTarget = target; s.lastShelled = s.shellMask;
    s.pendCard = s.pendPlayer = s.pendTarget = -1; s.respondMask = s.shellMask = 0;
    CheckRoundEnd(s, seat);
    if (s.phase == PH_ROUND_OVER || s.phase == PH_MATCH_OVER) return;
    s.phase = s.seats[seat].betsLeft > 0 ? PH_BET : PH_PLAY;
    if (s.phase == PH_PLAY) { Refill(s, seat); s.turn = (seat + 1) % s.nSeats; s.turnsPlayed++; }
    s.timer = TURN_SECONDS;
}
bool Apply(State& s, const Action& a) {
    if (!Legal(s, a)) return false;
    Seat& p = s.seats[a.seat];
    switch (a.kind) {
        case Action::PLAY: {
            int c = p.hand[a.handIdx];
            p.hand.erase(p.hand.begin() + a.handIdx); p.handCount = (int)p.hand.size();
            s.discard.push_back((uint8_t)c);
            s.pendCard = c; s.pendPlayer = a.seat; s.pendTarget = a.target;
            Log(s, Crab(a.seat) + " plays " + DECK[c].name + ".");
            uint8_t aimed = AimedMask(s, a.seat, c, a.target), can = 0;
            for (int i = 0; i < s.nSeats; i++) if ((aimed >> i) & 1) for (uint8_t h : s.seats[i].hand) if (h == C_SHELL) { can |= 1 << i; break; }
            s.shellMask = 0;
            if (can) { s.respondMask = can; s.phase = PH_RESPONSE; s.timer = RESPONSE_SECONDS; }
            else Resolve(s);
        } break;
        case Action::RESPOND:
            if (a.shell) { auto it = std::find(p.hand.begin(), p.hand.end(), (uint8_t)C_SHELL); s.discard.push_back(*it); p.hand.erase(it); p.handCount = (int)p.hand.size(); s.shellMask |= 1 << a.seat; }
            s.respondMask &= ~(1 << a.seat);
            if (s.respondMask) s.timer = RESPONSE_SECONDS; else Resolve(s);
            break;
        case Action::BET: p.betsLeft--; s.bets.push_back({a.seat, (uint8_t)a.target}); Log(s, Crab(a.seat) + " lays a pearl face down."); [[fallthrough]];
        case Action::SKIPBET: Refill(s, a.seat); s.turn = (a.seat + 1) % s.nSeats; s.turnsPlayed++; s.phase = PH_PLAY; s.timer = TURN_SECONDS; break;
        case Action::NEXTROUND: s.round++; s.firstPlayer = (s.firstPlayer + 1) % s.nSeats; StartRound(s); break;
    }
    return true;
}
void Tick(State& s, float dt, std::vector<Action>& timeouts) {
    int actor = Actor(s);
    if (actor < 0) return;
    s.timer -= dt;
    if (s.timer > 0) return;
    uint32_t r = s.rng ^ 0x9E3779B9u;
    if (s.phase == PH_RESPONSE) { Action a; a.kind = Action::RESPOND; a.seat = (uint8_t)actor; a.shell = false; timeouts.push_back(a); }   // silence is a no
    else timeouts.push_back(Bot(s, actor, r));
    s.timer = 1e9f;   // (until the host applies it)
}

// ---- the bot: tries every card and target on a copy and keeps the best-looking result
static float Score(const State& s, int seat) {
    float me = s.seats[seat].pos, best = 0;
    for (int i = 0; i < s.nSeats; i++) if (i != seat) best = std::max(best, (float)s.seats[i].pos);
    float v = me * 3 - best * 2 - (s.seats[seat].skipMove ? 1.5f : 0);
    for (int i = 0; i < s.nSeats; i++) if (i != seat && s.seats[i].skipMove) v += 0.8f;
    if (s.roundWinner == seat) v += 100; else if (s.roundWinner >= 0) v -= 60;
    return v;
}
Action Bot(const State& s, int seat, uint32_t& rng) {
    Action best; best.seat = (uint8_t)seat;
    if (s.phase == PH_RESPONSE) { best.kind = Action::RESPOND; best.shell = true; return best; }   // always duck
    if (s.phase == PH_BET) { // back whoever leads by 2 or more, or its own crab when it leads
        int lead = seat, lp = -1, second = -1;
        for (int i = 0; i < s.nSeats; i++) if (s.seats[i].pos > lp) { second = lp; lp = s.seats[i].pos; lead = i; } else second = std::max(second, (int)s.seats[i].pos);
        if (lp >= 4 && (lead == seat || lp - second >= 2) && Rnd(rng) % 3 != 0) { best.kind = Action::BET; best.target = (int8_t)lead; return best; }
        best.kind = Action::SKIPBET; return best;
    }
    best.kind = Action::PLAY;
    float bestV = -1e9f;
    const Seat& p = s.seats[seat];
    for (int h = 0; h < (int)p.hand.size(); h++) {
        int c = p.hand[h];
        std::vector<int> ts = NeedsTarget(c) ? Targets(s, seat, c) : std::vector<int>{-1};
        if (ts.empty()) ts.push_back(-1);
        for (int t : ts) {
            State c2 = s; c2.log.clear();
            Action a; a.kind = Action::PLAY; a.seat = (uint8_t)seat; a.handIdx = (uint8_t)h; a.target = (int8_t)t;
            if (!Apply(c2, a)) continue;
            while (c2.phase == PH_RESPONSE) { Action r; r.kind = Action::RESPOND; r.seat = (uint8_t)Actor(c2); r.shell = false; Apply(c2, r); } // (it can't know their hands)
            float v = Score(c2, seat) + (c == C_SHELL ? -4 : 0) + (Rnd(rng) % 100) * 0.004f;
            if (c == C_ROCK) { int ahead = 99; for (int i = 0; i < s.nSeats; i++) if (i != seat && s.seats[i].pos >= s.seats[seat].pos) ahead = std::min(ahead, (int)s.seats[i].pos); v += (t > ahead && t <= ahead + 3) ? 1.2f : -1; }
            if (v > bestV) { bestV = v; best.handIdx = (uint8_t)h; best.target = (int8_t)t; }
        }
    }
    return best;
}

// ---- what a seat sees
void Serialize(const State& s, int viewer, Writer& w) {
    w.U8(s.nSeats); w.U8(s.winsNeeded); w.I32(s.rock); w.U8(s.turn); w.U8(s.phase); w.U8(s.round); w.U8(s.firstPlayer);
    w.I32(s.pendCard); w.I32(s.pendPlayer); w.I32(s.pendTarget); w.U8(s.respondMask);
    w.I32(s.roundWinner); w.I32(s.roundSecond); w.I32(s.matchWinner); w.U32(s.turnsPlayed); w.F32(s.timer);
    w.U32(s.plays); w.I32(s.lastCard); w.I32(s.lastPlayer); w.I32(s.lastTarget); w.U8(s.lastShelled);
    w.VarU((uint32_t)s.deck.size()); w.I32(s.discard.empty() ? -1 : s.discard.back());
    for (int i = 0; i < s.nSeats; i++) {
        const Seat& p = s.seats[i];
        w.U8(p.pos); w.U8(p.skipMove); w.U8(p.betsLeft); w.I32(p.points); w.U8(p.roundWins); w.U8((uint8_t)p.hand.size());
        bool mine = viewer < 0 || viewer == i;        // hidden information: only your own hand
        w.U8(mine);
        if (mine) for (uint8_t c : p.hand) w.U8(c);
    }
    std::vector<Bet> shown; int hidden[MAX_SEATS] = {};
    for (const Bet& b : s.bets) { if (viewer < 0 || b.owner == viewer || s.phase >= PH_ROUND_OVER) shown.push_back(b); else hidden[b.owner]++; } // face down until the round ends
    w.VarU((uint32_t)shown.size()); for (const Bet& b : shown) { w.U8(b.owner); w.U8(b.crab); }
    for (int i = 0; i < s.nSeats; i++) w.U8(hidden[i]);
    w.VarU((uint32_t)s.log.size()); for (auto& l : s.log) w.Str(l);
    if (viewer < 0) { w.U32(s.rng); w.VarU((uint32_t)s.deck.size()); w.Bytes(s.deck.data(), s.deck.size()); w.VarU((uint32_t)s.discard.size()); w.Bytes(s.discard.data(), s.discard.size()); }
}
bool Deserialize(State& s, Reader& r) {
    State n;
    n.nSeats = r.U8(); n.winsNeeded = r.U8(); n.rock = r.I32(); n.turn = r.U8(); n.phase = r.U8(); n.round = r.U8(); n.firstPlayer = r.U8();
    n.pendCard = r.I32(); n.pendPlayer = r.I32(); n.pendTarget = r.I32(); n.respondMask = (uint8_t)r.U8();
    n.roundWinner = r.I32(); n.roundSecond = r.I32(); n.matchWinner = r.I32(); n.turnsPlayed = r.U32(); n.timer = r.F32();
    n.plays = r.U32(); n.lastCard = r.I32(); n.lastPlayer = r.I32(); n.lastTarget = r.I32(); n.lastShelled = (uint8_t)r.U8();
    uint32_t deckN = r.VarU(); int top = r.I32(); (void)top;
    if (n.nSeats < 2 || n.nSeats > MAX_SEATS) return false;
    for (int i = 0; i < n.nSeats; i++) {
        Seat& p = n.seats[i]; p.used = true;
        p.pos = (uint8_t)r.U8(); p.skipMove = r.U8() != 0; p.betsLeft = (uint8_t)r.U8(); p.points = r.I32(); p.roundWins = r.U8(); p.handCount = r.U8();
        if (r.U8()) for (int k = 0; k < p.handCount && !r.bad; k++) p.hand.push_back((uint8_t)r.U8());
    }
    uint32_t nb = r.VarU(); for (uint32_t k = 0; k < nb && !r.bad; k++) { Bet b; b.owner = (uint8_t)r.U8(); b.crab = (uint8_t)r.U8(); n.bets.push_back(b); }
    for (int i = 0; i < n.nSeats; i++) n.betsHidden[i] = r.U8();
    uint32_t nl = r.VarU(); for (uint32_t k = 0; k < nl && !r.bad; k++) n.log.push_back(r.Str());
    if (!r.Done()) { n.rng = r.U32(); uint32_t d = r.VarU(); for (uint32_t k = 0; k < d && !r.bad; k++) n.deck.push_back((uint8_t)r.U8()); uint32_t dc = r.VarU(); for (uint32_t k = 0; k < dc && !r.bad; k++) n.discard.push_back((uint8_t)r.U8()); }
    else n.deck.assign(std::min<uint32_t>(deckN, 200), 0);   // (a client only knows how many are left)
    if (r.bad) return false;
    s = n;
    return true;
}

void WriteAction(const Action& a, Writer& w) { w.U8(a.kind); w.U8(a.handIdx); w.U8((uint8_t)a.target); w.U8(a.shell); }

SimResult Simulate(int matches, int nSeats, uint32_t seed) {
    SimResult res; long turns = 0, rounds = 0;
    uint32_t rng = seed ? seed : 7;
    for (int m = 0; m < matches; m++) {
        State s; NewMatch(s, nSeats, Rnd(rng));
        for (int guard = 0; guard < 5000 && s.phase != PH_MATCH_OVER; guard++) {
            if (s.phase == PH_ROUND_OVER) { Action a; a.kind = Action::NEXTROUND; Apply(s, a); continue; }
            int who = Actor(s);
            Action a = Bot(s, who, rng);
            if (!Apply(s, a)) { Action k; k.kind = Action::SKIPBET; k.seat = (uint8_t)who; if (!Apply(s, k)) break; }
        }
        if (s.matchWinner >= 0) res.seatWins[s.matchWinner]++;
        res.matches++; turns += s.turnsPlayed; rounds += s.round;
    }
    res.avgTurns = (double)turns / std::max(1, res.matches); res.avgRounds = (double)rounds / std::max(1, res.matches);
    return res;
}
}
