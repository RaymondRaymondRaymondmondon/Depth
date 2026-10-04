// A Night Off: the card room's rules and bots (see nightoff_cards.h). Headless.
#include "nightoff_cards.h"
#include "nightoff_games.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {
namespace cards {

static const char* RN[15] = {"", "", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
static const char* SN[4] = {"s", "h", "d", "c"};
std::string Name(Card c) { return std::string(RN[std::clamp((int)c.r, 0, 14)]) + SN[c.s & 3]; }
std::vector<Card> Deck() { std::vector<Card> d; for (int s = 0; s < 4; s++) for (int r = 2; r <= 14; r++) d.push_back({(int8_t)r, (int8_t)s}); return d; }
void Shuffle(std::vector<Card>& d, Rng& r) { for (int i = (int)d.size() - 1; i > 0; i--) std::swap(d[i], d[r.I(i + 1)]); }
static std::string NV(const std::string& n, const char* s3, const char* s1) { return n + " " + (n == "You" ? s1 : s3); }   // ("You win", "Harrow wins")
static int S(int cat, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0) { return cat * 759375 + a * 50625 + b * 3375 + c * 225 + d * 15 + e; }
static int StraightHigh(const bool* has) {
    for (int hi = 14; hi >= 5; hi--) { bool ok = true; for (int k = 0; k < 5 && ok; k++) { int r = hi - k; if (r == 1) r = 14; ok = has[r]; } if (ok) return hi; }
    return 0;
}
int Eval(const Card* c, int n) {
    int cnt[15] = {}, sc[4] = {};
    for (int i = 0; i < n; i++) { cnt[c[i].r]++; sc[c[i].s & 3]++; }
    int fs = -1; for (int s = 0; s < 4; s++) if (sc[s] >= 5) fs = s;
    if (fs >= 0) { bool has[15] = {}; for (int i = 0; i < n; i++) if (c[i].s == fs) has[c[i].r] = true; int sh = StraightHigh(has); if (sh) return S(8, sh); }
    int quad = 0, trips[2] = {0, 0}, nt = 0, pairs[3] = {0, 0, 0}, np = 0;
    for (int r = 14; r >= 2; r--) { if (cnt[r] == 4) quad = r; else if (cnt[r] == 3 && nt < 2) trips[nt++] = r; else if (cnt[r] == 2 && np < 3) pairs[np++] = r; }
    auto kick = [&](int skip1, int skip2, int k, int* out) { int m = 0; for (int r = 14; r >= 2 && m < k; r--) if (r != skip1 && r != skip2 && cnt[r] > 0) out[m++] = r; while (m < k) out[m++] = 0; };
    if (quad) { int k[1]; kick(quad, 0, 1, k); return S(7, quad, k[0]); }
    if (nt >= 1 && (np >= 1 || nt >= 2)) return S(6, trips[0], nt >= 2 ? std::max(trips[1], pairs[0]) : pairs[0]);
    if (fs >= 0) { int f[5], m = 0; for (int r = 14; r >= 2 && m < 5; r--) for (int i = 0; i < n; i++) if (c[i].s == fs && c[i].r == r && m < 5) { f[m++] = r; break; } return S(5, f[0], f[1], f[2], f[3], f[4]); }
    { bool has[15] = {}; for (int r = 2; r <= 14; r++) has[r] = cnt[r] > 0; int sh = StraightHigh(has); if (sh) return S(4, sh); }
    if (nt >= 1) { int k[2]; kick(trips[0], 0, 2, k); return S(3, trips[0], k[0], k[1]); }
    if (np >= 2) { int k[1]; kick(pairs[0], pairs[1], 1, k); return S(2, pairs[0], pairs[1], k[0]); }
    if (np == 1) { int k[3]; kick(pairs[0], 0, 3, k); return S(1, pairs[0], k[0], k[1], k[2]); }
    int k[5]; kick(0, 0, 5, k); return S(0, k[0], k[1], k[2], k[3], k[4]);
}
std::string HandName(int score) {
    static const char* N[9] = {"high card", "a pair", "two pair", "three of a kind", "a straight", "a flush", "a full house", "four of a kind", "a straight flush"};
    return N[std::clamp(score / 759375, 0, 8)];
}
std::string RankWord(int r, int n) {
    static const char* PL[15] = {"", "", "twos", "threes", "fours", "fives", "sixes", "sevens", "eights", "nines", "tens", "jacks", "queens", "kings", "aces"};
    static const char* SG[15] = {"", "", "a two", "a three", "a four", "a five", "a six", "a seven", "an eight", "a nine", "a ten", "a jack", "a queen", "a king", "an ace"};
    static const char* NUM[5] = {"no", "one", "two", "three", "four"};
    r = std::clamp(r, 2, 14);
    return n == 1 ? SG[r] : std::string(NUM[std::clamp(n, 0, 4)]) + " " + PL[r];
}

// ================================================================ hold'em
int Poker::Active() const { int n = 0; for (const auto& s : seats) n += !s.out && !s.folded; return n; }
int Poker::Live() const { int n = 0; for (const auto& s : seats) n += !s.out && s.stack > 0; return n; }
static int NextSeat(const Poker& p, int from, bool needChips) {
    int n = (int)p.seats.size();
    for (int k = 1; k <= n; k++) { int i = (from + k) % n; const Seat& s = p.seats[i]; if (s.out) continue; if (needChips && s.stack <= 0) continue; return i; }
    return -1;
}
void Poker::StartHand() {
    for (auto& s : seats) if (s.stack <= 0) s.out = true;
    if (Live() < 2) { street = 5; turn = -1; return; }
    hand++;
    for (auto& s : seats) { s.bet = s.total = 0; s.folded = s.out; s.allIn = false; s.acted = false; s.last.clear(); s.tellNow = false; s.peeked = 0; s.kidney = s.kidney && kidneyPot; }
    deck = Deck(); Shuffle(deck, rng); boardN = 0; winners.clear(); result.clear(); bluffWin = false;
    dealer = NextSeat(*this, dealer, true);
    for (auto& s : seats) if (!s.out) { s.hole[0] = deck.back(); deck.pop_back(); }
    for (auto& s : seats) if (!s.out) { s.hole[1] = deck.back(); deck.pop_back(); }
    bool heads = Live() == 2;
    int sbS = heads ? dealer : NextSeat(*this, dealer, true), bbS = NextSeat(*this, sbS, true);
    auto post = [&](int i, int amt) { Seat& s = seats[i]; int pay = std::min(amt, s.stack); s.stack -= pay; s.bet += pay; s.total += pay; if (s.stack == 0) s.allIn = true; };
    post(sbS, sb); post(bbS, bb);
    toCall = bb; minRaise = bb; street = 0;
    turn = NextSeat(*this, bbS, true);
    while (turn >= 0 && (seats[turn].folded || seats[turn].allIn)) turn = NextSeat(*this, turn, true);
    actT = 0;
}
static bool NeedsAction(const Poker& p, const Seat& s) { return !s.out && !s.folded && !s.allIn && (!s.acted || s.bet < p.toCall); }
bool Poker::Act(int i, int action, int amount) {
    if (street > 3 || i != turn || i < 0 || i >= (int)seats.size()) return false;
    Seat& s = seats[i]; if (s.folded || s.allIn || s.out) return false;
    s.tellNow = false;
    auto pay = [&](int amt) { amt = std::clamp(amt, 0, s.stack); s.stack -= amt; s.bet += amt; s.total += amt; if (s.stack == 0) s.allIn = true; };
    if (action == 1) { s.folded = true; s.last = "folds"; }
    else if (action == 2) { int d = toCall - s.bet; s.last = d <= 0 ? "checks" : "calls"; pay(d); }
    else {
        int target = std::max(amount, toCall + minRaise);
        if (target - s.bet >= s.stack) target = s.bet + s.stack;   // (all in)
        if (target <= toCall) { int d = toCall - s.bet; s.last = d <= 0 ? "checks" : "calls"; pay(d); }
        else {
            int raise = target - toCall; pay(target - s.bet);
            if (raise >= minRaise) { minRaise = raise; for (auto& o : seats) if (&o != &s && !o.folded && !o.allIn) o.acted = false; }
            toCall = std::max(toCall, s.bet); s.last = s.allIn ? "goes all in" : "raises to " + std::to_string(target);
        }
    }
    s.acted = true;
    if (Active() == 1) {   // everyone else folded: the pot is theirs
        for (int k = 0; k < (int)seats.size(); k++) if (!seats[k].out && !seats[k].folded) { winners = {k}; potShown = Pot(); seats[k].stack += potShown; result = NV(seats[k].name, "takes", "take") + " " + std::to_string(potShown) + " uncontested"; }
        for (auto& o : seats) o.total = 0;
        street = 5; turn = -1; nextT = 3; return true;
    }
    int nx = turn;
    for (int k = 0; k < (int)seats.size(); k++) { nx = NextSeat(*this, nx, false); if (nx >= 0 && NeedsAction(*this, seats[nx])) { turn = nx; actT = 0; return true; } }
    NextStreet();
    return true;
}
void Poker::NextStreet() {
    for (auto& s : seats) { s.bet = 0; s.acted = false; }
    toCall = 0; minRaise = bb;
    street++;
    if (street == 1) { for (int k = 0; k < 3; k++) { board[boardN++] = deck.back(); deck.pop_back(); } }
    else if (street == 2 || street == 3) { board[boardN++] = deck.back(); deck.pop_back(); }
    if (street >= 4) { Showdown(); return; }
    int canAct = 0; for (const auto& s : seats) canAct += !s.out && !s.folded && !s.allIn;
    if (canAct <= 1) { while (street < 4) NextStreet(); return; }   // (everyone's in: run the board out)
    turn = dealer; do { turn = NextSeat(*this, turn, false); } while (turn >= 0 && (seats[turn].folded || seats[turn].allIn));
    actT = 0;
}
void Poker::Showdown() {
    street = 4; turn = -1; winners.clear();
    std::vector<int> score(seats.size(), -1);
    for (int i = 0; i < (int)seats.size(); i++) if (!seats[i].out && !seats[i].folded) { Card c[7] = {seats[i].hole[0], seats[i].hole[1]}; for (int k = 0; k < boardN; k++) c[2 + k] = board[k]; score[i] = Eval(c, 2 + boardN); }
    // side pots: each level of contribution is a pot for those who put that much in
    std::vector<int> levels; for (const auto& s : seats) if (s.total > 0) levels.push_back(s.total);
    std::sort(levels.begin(), levels.end()); levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    int prev = 0; potShown = Pot();
    for (int L : levels) {
        int part = 0; for (const auto& s : seats) part += std::max(0, std::min(s.total, L) - prev);
        int best = -1; std::vector<int> w;
        for (int i = 0; i < (int)seats.size(); i++) if (score[i] >= 0 && seats[i].total >= L) { if (score[i] > best) { best = score[i]; w = {i}; } else if (score[i] == best) w.push_back(i); }
        if (w.empty()) { for (int i = 0; i < (int)seats.size(); i++) if (seats[i].total >= L && !seats[i].out) { w = {i}; break; } }
        for (size_t k = 0; k < w.size(); k++) seats[w[k]].stack += part / (int)w.size() + (k == 0 ? part % (int)w.size() : 0);
        for (int x : w) if (std::find(winners.begin(), winners.end(), x) == winners.end()) winners.push_back(x);
        prev = L;
    }
    for (auto& s : seats) s.total = 0;
    if (!winners.empty()) result = NV(seats[winners[0]].name, "wins", "win") + " " + std::to_string(potShown) + " with " + HandName(score[winners[0]]);
    street = 5; nextT = 5;
}
float Poker::Equity(int i, int sims) {
    const Seat& me = seats[i];
    std::vector<Card> unknown = Deck();
    auto drop = [&](Card c) { unknown.erase(std::remove(unknown.begin(), unknown.end(), c), unknown.end()); };
    drop(me.hole[0]); drop(me.hole[1]); for (int k = 0; k < boardN; k++) drop(board[k]);
    int opp = 0; for (int k = 0; k < (int)seats.size(); k++) opp += k != i && !seats[k].out && !seats[k].folded;
    if (opp == 0) return 1;
    float win = 0;
    for (int n = 0; n < sims; n++) {
        // a partial shuffle: just the cards we need
        int need = opp * 2 + (5 - boardN);
        for (int k = 0; k < need && k < (int)unknown.size(); k++) std::swap(unknown[k], unknown[k + rng.I((int)unknown.size() - k)]);
        Card b[5]; for (int k = 0; k < boardN; k++) b[k] = board[k];
        int u = opp * 2; for (int k = boardN; k < 5; k++) b[k] = unknown[u++];
        Card mine[7] = {me.hole[0], me.hole[1], b[0], b[1], b[2], b[3], b[4]};
        int ms = Eval(mine, 7), best = 0, ties = 0;
        for (int o = 0; o < opp; o++) { Card th[7] = {unknown[o * 2], unknown[o * 2 + 1], b[0], b[1], b[2], b[3], b[4]}; int s = Eval(th, 7); if (s > best) best = s; }
        if (ms > best) win += 1; else if (ms == best) { ties++; win += 0.5f; }
    }
    return win / sims;
}
int Poker::BotChoose(int i, int& amount, float drunk) {
    Seat& s = seats[i];
    float e = Equity(i, 160);
    float noise = 0.06f * s.skill + drunk / 260;
    e = std::clamp(e + ((rng.U() + rng.U() + rng.U()) / 1.5f - 1) * noise, 0.0f, 1.0f);
    int pot = Pot(), owe = toCall - s.bet;
    float courage = drunk / 150;   // (betting gets braver: the courage bonus is real, and expensive)
    float odds = owe > 0 ? (float)owe / (pot + owe) : 0;
    int opp = Active() - 1; float fair = 1.0f / (opp + 1);   // (an even share of the pot)
    // a sober player at a drunk table is a wolf: against someone past 60, bluff less and call them down lighter
    float drunkest = 0; for (int k = 0; k < (int)seats.size(); k++) if (k != i && !seats[k].folded && !seats[k].out) drunkest = std::max(drunkest, seats[k].drunk);
    bool wolf = drunk < 40 && drunkest >= 60; float bluff = wolf ? s.bluff * 0.3f : s.bluff;
    auto raiseTo = [&](float k) { amount = toCall + std::max(minRaise, (int)(pot * k)); };
    if (drunk >= 60 && rng.U() < (drunk - 50) / 160) { float u = rng.U(); if (u < 0.35f && owe > 0) return 1; if (u < 0.7f) return 2; raiseTo(0.4f + rng.U()); return 3; }   // (the wrecked misread their cards)
    if (owe <= 0) {
        if (e > fair + 0.22f - courage * 0.3f) { raiseTo(0.6f); return 3; }
        if (rng.U() < bluff) { raiseTo(0.5f); if (e < fair && !s.tell.empty()) s.tellNow = true; return 3; }
        return 2;
    }
    if (e > fair + 0.35f - courage * 0.3f) { raiseTo(0.75f); return 3; }
    if (e > odds + 0.04f - courage - (wolf ? 0.15f : 0)) return 2;
    if (rng.U() < bluff * 0.4f) { raiseTo(0.8f); if (!s.tell.empty()) s.tellNow = true; return 3; }
    return 1;
}

// ================================================================ bullshit
void Bullshit::Deal() {
    auto d = Deck(); Shuffle(d, rng);
    for (auto& s : seats) s.hand.clear();
    for (size_t k = 0; k < d.size(); k++) seats[k % seats.size()].hand.push_back(d[k]);
    for (auto& s : seats) std::sort(s.hand.begin(), s.hand.end(), [](Card a, Card b) { return a.r < b.r; });
    pile.clear(); turn = 0; rank = 14; lastSeat = -1; lastCount = 0; lastCards.clear(); claim.clear(); window = 0; winner = -1; result.clear(); callSeat = -1; revealT = 0;
}
bool Bullshit::Play(int i, const std::vector<int>& idx) {
    if (winner >= 0 || i != turn || window > 0 || i < 0 || i >= (int)seats.size() || idx.empty() || idx.size() > 4) return false;
    BsSeat& s = seats[i];
    std::vector<int> v = idx; std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end());
    for (int k : v) if (k < 0 || k >= (int)s.hand.size()) return false;
    lastCards.clear(); for (int k = (int)v.size() - 1; k >= 0; k--) { lastCards.push_back(s.hand[v[k]]); s.hand.erase(s.hand.begin() + v[k]); }
    for (Card c : lastCards) pile.push_back(c);
    lastSeat = i; lastCount = (int)lastCards.size();
    int real = 0; for (Card c : lastCards) real += c.r == rank;
    lastWasLie = real < lastCount;
    claim = RankWord(rank, lastCount);
    // the very drunk say it honestly (doc p. 18); an honest patron can't lie and everyone knows
    if (lastWasLie && (s.drunk >= 80 || s.honest)) claim += real == 0 ? "... no. None. Not one." : "... no, " + RankWord(rank, real) + ".";
    window = 3.0f; callSeat = -1; revealT = 0;
    turn = (turn + 1) % (int)seats.size(); claimRank = rank; rank = NextRank();
    return true;
}
bool Bullshit::Call(int i) {
    if (window <= 0 || i == lastSeat || i < 0 || i >= (int)seats.size() || winner >= 0) return false;
    Resolve(true, i);
    return true;
}
void Bullshit::Resolve(bool called, int caller) {
    window = 0;
    if (called) {
        callSeat = caller; revealT = 2.5f;
        int taker = lastWasLie ? lastSeat : caller;
        for (Card c : pile) seats[taker].hand.push_back(c);
        std::sort(seats[taker].hand.begin(), seats[taker].hand.end(), [](Card a, Card b) { return a.r < b.r; });
        result = NV(seats[caller].name, "calls", "call") + " it: " + (lastWasLie ? NV(seats[lastSeat].name, "was", "were") + " lying and " + (seats[lastSeat].name == "You" ? "take" : "takes") + " the pile (" : "it was true; " + NV(seats[caller].name, "takes", "take") + " the pile (") + std::to_string(pile.size()) + ")";
        pile.clear();
    }
    if (lastSeat >= 0 && seats[lastSeat].hand.empty() && !(called && lastWasLie)) { winner = lastSeat; result = NV(seats[lastSeat].name, "is", "are") + " out of cards and " + (seats[lastSeat].name == "You" ? "take" : "takes") + " the pot"; }
}
void Bullshit::BotPlay(int i, std::vector<int>& out) {
    out.clear(); const BsSeat& s = seats[i];
    for (int k = 0; k < (int)s.hand.size() && out.size() < 4; k++) if (s.hand[k].r == rank) out.push_back(k);
    if (!out.empty()) { if (!s.honest && s.hand.size() > out.size() + 2 && rng.U() < 0.25f && out.size() < 4) { int k = rng.I((int)s.hand.size()); if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k); } return; }
    out.push_back(rng.I((int)s.hand.size()));
    if (!s.honest && s.hand.size() > 4 && rng.U() < 0.3f) { int k = rng.I((int)s.hand.size()); if (k != out[0]) out.push_back(k); }
}
float Bullshit::BotDoubt(int i) const {
    if (lastSeat < 0 || i == lastSeat) return 0;
    const BsSeat& me = seats[i]; const BsSeat& them = seats[lastSeat];
    int have = 0; for (Card c : me.hand) have += c.r == claimRank;
    if (have + lastCount > 4) return 1;                       // (impossible)
    if (lastWasLie && (them.drunk >= 80 || them.honest)) return 1;   // (they said so)
    float d = 0.02f + 0.1f * (lastCount - 1) + 0.05f * have + (them.hand.size() <= 2 ? 0.3f : 0);
    if (them.liar) d -= 0.08f;                                 // (a straight face)
    return std::clamp(d, 0.0f, 0.95f);
}

}  // namespace cards

// ================================================================ --game-check poker|bullshit
int RunCardCheck(const std::string& game, int games) {
    using namespace cards;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    // the evaluator against known hands
    if (game == "poker" || game == "all") {
        auto H = [](std::vector<Card> v) { return Eval(v.data(), (int)v.size()); };
        Card As{14, 0}, Ks{13, 0}, Qs{12, 0}, Js{11, 0}, Ts{10, 0}, Ah{14, 1}, Ad{14, 2}, Kh{13, 1}, Kd{13, 2}, two{2, 3}, three{3, 1}, four{4, 2}, five{5, 3};
        check(H({As, Ks, Qs, Js, Ts, two, three}) / 759375 == 8, "a royal flush is a straight flush");
        check(H({Ah, two, three, four, five, Kd, Qs}) / 759375 == 4, "the wheel (A-2-3-4-5) is a straight");
        check(H({As, Ah, Ad, Ks, Kh, two, three}) / 759375 == 6 && H({As, Ah, Ad, Ks, Kh, two, three}) > H({Ks, Kh, Kd, As, Ah, two, three}), "aces full beats kings full");
        check(H({As, Ah, Ks, Kh, two, three, five}) > H({As, Ah, Ks, Kh, two, three, four}) || H({As, Ah, Ks, Kh, two, three, five}) == H({As, Ah, Ks, Kh, two, three, four}), "two pair's kicker counts");
    }
    const float LV[3] = {0, 40, 80}, TARGET[3] = {0.55f, 0.35f, 0.10f};
    if (game == "poker" || game == "all") {
        // heads up against the hustler, 300 chips each, to the end: the player reads a tell when sober enough to see it
        for (int l = 0; l < 3; l++) {
            int won = 0;
            for (int g = 0; g < games; g++) {
                Poker P; P.rng.s = 1000 + g * 31 + l * 7;
                Seat a; a.name = "you"; a.stack = 300; a.skill = AimMul(LV[l]); a.bluff = 0.08f + LV[l] / 400; a.drunk = LV[l];
                Seat b; b.name = "the hustler"; b.stack = 300; b.skill = 0.95f; b.bluff = 0.24f; b.tell = "scratches his ear";
                P.seats = {a, b}; P.dealer = g & 1;
                for (int h = 0; h < 200 && P.Live() >= 2; h++) {
                    P.StartHand();
                    for (int k = 0; k < 60 && P.street <= 3 && P.turn >= 0; k++) {
                        int t = P.turn, amt = 0, act = P.BotChoose(t, amt, P.seats[t].drunk);
                        // a tell you can see (sober enough): the hustler's bet is a bluff, so call it
                        if (t == 0 && P.seats[1].tellNow && LV[l] < 60) { act = 3; amt = P.toCall + P.minRaise * 2; }   // (re-raise the bluff)
                        P.Act(t, act, amt);
                    }
                }
                won += P.seats[0].stack > P.seats[1].stack;
            }
            float r = (float)won / games;
            check(fabsf(r - TARGET[l]) <= 0.15f, TextFormat("poker against the hustler at %.0f drunk: %.0f%% (target %.0f%%)", LV[l], r * 100, TARGET[l] * 100));
        }
    }
    if (game == "bullshit" || game == "all") {
        for (int l = 0; l < 3; l++) {
            int won = 0;
            for (int g = 0; g < games; g++) {
                Bullshit B; B.rng.s = 500 + g * 17 + l;
                BsSeat a; a.name = "you"; a.drunk = LV[l];
                BsSeat b; b.name = "the hustler"; b.liar = true;
                BsSeat c; c.name = "a regular";
                B.seats = {a, b, c}; B.Deal();
                for (int step = 0; step < 4000 && B.winner < 0; step++) {
                    std::vector<int> v; B.BotPlay(B.turn, v); B.Play(B.turn, v);
                    // the others decide whether to call (the sober player doubts better: a drunk one's judgement blurs)
                    int caller = -1;
                    int first = B.rng.I((int)B.seats.size());
                    for (int q = 0; q < (int)B.seats.size() && caller < 0; q++) { int s = (first + q) % (int)B.seats.size(); if (s == B.lastSeat) continue; float d = B.BotDoubt(s); if (s == 0 && LV[l] >= 40) d = LV[l] >= 80 ? d * 0.3f + B.rng.U() * 0.6f : d * 0.7f + B.rng.U() * 0.25f; if (B.rng.U() < d) caller = s; }
                    if (caller >= 0) B.Call(caller); else B.Resolve(false, -1);
                }
                won += B.winner == 0;
            }
            float r = (float)won / games;
            printf("  bullshit (you, the hustler, a regular) at %.0f drunk: you win %.0f%%\n", LV[l], r * 100);
            if (l == 0) check(r > 0.3f, "a sober player is a wolf at the table");
            if (l == 2) check(r < 0.25f, "a wrecked one says what they've got");
        }
    }
    printf(fails ? "card-check: %d FAILED\n" : "card-check: all checks passed\n", fails);
    return fails;
}

}  // namespace no
