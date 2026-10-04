// A Night Off: the card room in the night (doc p. 18), stage 8: sitting down at hold'em (buy-in 50, blinds rising each
// hour, the regulars' tells) or bullshit (20 each, winner takes all), the cartel's hand with a kidney in the pot,
// cheating at every game (a charisma-plus-sobriety roll; caught by whoever's watching), and side bets on the other
// sailors' games. Headless; every move is the player's Input (gameAct 21-27, cheat held, evAct 30/31 for a side bet).
#include "nightoff.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

static const Rectangle POKER_T{33, 13, 3, 3}, BS_T{33, 17.5f, 3, 3};
static Vector2 SeatSpot(Rectangle r, int k, int n) { float a = -PI / 2 + k * 2 * PI / std::max(1, n); return {r.x + r.width / 2 + cosf(a) * 2.0f, r.y + r.height / 2 + sinf(a) * 2.0f}; }
float Night::CheatChance(const Player& p) const { return std::clamp(0.2f + 0.5f * (1 - p.drunk / 100) + (Charisma(p) - 1) * 0.6f, 0.05f, 0.85f); }
bool Night::TryCheat(Player& p, const char* what, int watcher) {
    if (Rand() < CheatChance(p)) { p.cheatsDone++; if (p.cheatsDone == 1) Note(p, 0, std::string("Cheated at ") + what + " and got away with it."); return true; }
    p.cheatsCaught++;
    Note(p, 5, std::string("Caught cheating at ") + what + ".");
    bar.mood = std::max(0.0f, bar.mood - 15);
    if (watcher >= 0 && watcher < (int)patrons.size()) {
        Patron& c = patrons[watcher]; c.mood = 0;
        Say(c.name + ": \"CHEAT!\"");
        if (c.Has(D().Trait("bad loser")) || c.Has(D().Trait("violent")) || c.type == T_HUSTLER || c.type == T_GAMBLER) StartBrawl(PatronW(watcher), PlayerW(p.id), PatronW(watcher));   // (a cheat they caught: they swing first)
    } else if (watcher == -2) Say("The bartender: \"Hands OFF the machine.\"");
    return false;
}
void Night::SettleSideBets(int on, bool won) {
    for (auto it = sideBets.begin(); it != sideBets.end();) {
        if (it->on != on) { ++it; continue; }
        if (it->bettor >= 0 && it->bettor < (int)players.size()) {
            Player& b = players[it->bettor]; bool good = it->forWin == won;
            if (good) { b.money += it->stake; Note(b, 3, TextFormat("Won a side bet of %d on %s.", it->stake, players[on].name.c_str())); }
            else { b.money -= it->stake; if (b.money < 0) { b.tab += -b.money; b.money = 0; } Note(b, 4, TextFormat("Lost a side bet of %d on %s.", it->stake, players[on].name.c_str())); }
        }
        it = sideBets.erase(it);
    }
}

// ---------------------------------------------------------------- sitting down
static void SeatPatrons(Night& n, cards::Poker* P, cards::Bullshit* B, int want) {
    static const char* PREFER[6] = {"Bartholomew Crane", "Finnegan", "Sly Pennick", "Harrow", "Captain Vane", "Red Haddock"};
    std::vector<int> pick;
    auto free = [&](const Patron& c) { return c.inside && !c.gone && !c.leaving && c.playing < 0 && c.talkingTo < 0 && c.fight.brawl < 0 && c.ev < 0 && c.type != T_STAFF; };
    for (const char* nm : PREFER) for (const auto& c : n.patrons) if ((int)pick.size() < want && c.name == nm && free(c)) pick.push_back(c.id);
    for (const auto& c : n.patrons) if ((int)pick.size() < want && free(c) && (c.type == T_GAMBLER || c.type == T_HUSTLER || c.type == T_SAILOR || c.type == T_REGULAR) && std::find(pick.begin(), pick.end(), c.id) == pick.end()) pick.push_back(c.id);
    for (int id : pick) {
        Patron& c = n.patrons[id]; c.playing = 99; c.nextGoalT = 1e9f;
        if (P) {
            cards::Seat s; s.kind = 1; s.idx = id; s.name = c.name; s.stack = 60 + (int)(n.Rand() * 100);
            s.skill = c.type == T_HUSTLER ? 0.9f : c.type == T_GAMBLER ? 1.0f : 1.3f; s.bluff = c.Has(D().Trait("liar")) ? 0.24f : 0.12f; s.drunk = c.drunk;
            if (c.reg >= 0 && !D().regulars[c.reg].tell.empty()) s.tell = D().regulars[c.reg].tell; else if (c.type == T_HUSTLER || c.type == T_GAMBLER) s.tell = n.Rand() < 0.5f ? "turns a ring on a finger" : "coughs";
            P->seats.push_back(s);
            Vector2 at = SeatSpot(POKER_T, (int)P->seats.size() - 1, 6); c.goal = at; c.path = n.NavPath(c.pos, at);
        }
        if (B) {
            cards::BsSeat s; s.kind = 1; s.idx = id; s.name = c.name; s.liar = c.Has(D().Trait("liar")); s.honest = c.Has(D().Trait("honest")); s.drunk = c.drunk;
            B->seats.push_back(s);
            Vector2 at = SeatSpot(BS_T, (int)B->seats.size() - 1, 6); c.goal = at; c.path = n.NavPath(c.pos, at);
        }
    }
}
bool Night::SitAtCards(Player& p, int kind, int machine, std::string* why) {
    auto no = [&](const char* s) { if (why) *why = s; return false; };
    if (kind == GK_POKER && machine == 1) {   // the cartel's hand: heads up with the quiet man, a kidney in the pot
        if (!EventOn("cartel")) return no("The quiet man isn't dealing.");
        int qm = -1; for (const auto& c : patrons) if (c.role == "the quiet man" && c.inside && !c.gone) qm = c.id;
        if (qm < 0) return no("The quiet man has gone.");
        cartelHand = cards::Poker{}; cartelHand.rng.s = rng ^ 0x5eedu; cartelHand.kidneyPot = true; cartelHand.sb = 10; cartelHand.bb = 20;
        cards::Seat me; me.kind = 0; me.idx = p.id; me.name = p.name; me.drunk = p.drunk;
        if (p.money >= 100) { me.stack = 100; p.money -= 100; } else { me.stack = 100; me.kidney = true; Note(p, 5, "Put a kidney in the cartel's pot."); }
        cards::Seat q; q.kind = 1; q.idx = qm; q.name = "the quiet man"; q.stack = 300; q.skill = 0.9f; q.bluff = 0.15f;
        cartelHand.seats = {me, q}; cartelHand.dealer = 1; cartelHand.StartHand();
        Say("The quiet man shuffles. \"One hand. The kidney's in the middle.\"");
        return true;
    }
    if (kind == GK_POKER) {
        if (p.money < 50) return no("The buy-in's fifty.");
        for (const auto& s : poker.seats) if (s.kind == 0 && s.idx == p.id && !s.out) return true;
        if (poker.seats.size() >= 6) return no("The table's full.");
        cards::Seat me; me.kind = 0; me.idx = p.id; me.name = p.name; me.stack = 50; me.drunk = p.drunk; p.money -= 50;
        bool fresh = true; for (const auto& s : poker.seats) fresh &= s.out;
        if (fresh) { poker = cards::Poker{}; poker.rng.s = rng ^ 0xc0ffeeu; poker.seats.push_back(me); SeatPatrons(*this, &poker, nullptr, 3 + (int)(Rand() * 2)); poker.nextT = 1.5f; }
        else poker.seats.push_back(me);
        Say(p.name + " buys in at the poker table.");
        return true;
    }
    if (kind == GK_BULLSHIT) {
        if (bsOn) { for (const auto& s : bs.seats) if (s.kind == 0 && s.idx == p.id) return true; return no("A hand's going: wait for the next."); }
        if (p.money < 20) return no("It's twenty to play.");
        bool there = false; for (const auto& s : bs.seats) there |= s.kind == 0 && s.idx == p.id;
        if (!there) { if (bs.seats.empty()) { bs = cards::Bullshit{}; bs.rng.s = rng ^ 0xb5b5u; } cards::BsSeat me; me.kind = 0; me.idx = p.id; me.name = p.name; me.drunk = p.drunk; bs.seats.push_back(me); p.money -= 20; }
        bsT = 2.5f;   // (the others join, then the deal)
        return true;
    }
    return false;
}
void Night::LeaveCards(Player& p) {
    for (auto& s : poker.seats) if (s.kind == 0 && s.idx == p.id && !s.out) { if (poker.street <= 3 && !s.folded) { s.folded = true; } p.money += s.stack; if (s.stack > 0) Note(p, 3, TextFormat("Cashed out of the poker table with %d.", s.stack)); s.stack = 0; s.out = true; }
    if (!bsOn) bs.seats.erase(std::remove_if(bs.seats.begin(), bs.seats.end(), [&](const cards::BsSeat& s) { return s.kind == 0 && s.idx == p.id; }), bs.seats.end());
}

// ---------------------------------------------------------------- a player's move at a card table
bool Night::CardAction(Player& p) {
    GameSeat& g = p.game; Input& in = p.in; int act = in.gameAct;
    if (g.kind != GK_POKER && g.kind != GK_BULLSHIT) return false;
    if (g.kind == GK_POKER) {
        cards::Poker& P = g.machine == 1 ? cartelHand : poker;
        int me = -1; for (int k = 0; k < (int)P.seats.size(); k++) if (P.seats[k].kind == 0 && P.seats[k].idx == p.id && !P.seats[k].out) me = k;
        if (act == 3) { if (g.machine == 0) LeaveCards(p); EndGame(p); return true; }
        if (me < 0) return true;
        if (act >= 21 && act <= 23 && P.turn == me) {
            int amt = (int)in.gamePower;
            // a bluff that makes the quiet man fold is one he respects (he pays double for it)
            if (P.kidneyPot && act == 23) { float e = P.Equity(me, 120); if (e < 0.45f) P.bluffWin = true; }
            P.Act(me, act - 20, amt);
        }
        if (act == 24 && P.street <= 3) {   // mark the deck: see one hand across the table (a cheat)
            int watcher = -1; for (const auto& s : P.seats) if (s.kind == 1 && !s.out) { watcher = s.idx; break; }
            if (TryCheat(p, "poker", watcher)) { for (auto& s : P.seats) if (s.kind == 1 && !s.out && !s.folded) { s.peeked |= 1u << p.id; break; } }
            else {   // the whole table turns on you: you're off the table, the stack with you stays in the pot
                Flag("poker_cheat", p.name);
                for (auto& s : P.seats) if (s.kind == 1 && s.idx >= 0 && s.idx < (int)patrons.size()) patrons[s.idx].mood = 0;
                P.seats[me].folded = true; P.seats[me].out = true; P.seats[me].stack = 0;
                g.over = true; g.result = 1; g.caption = "The whole table turns on you. You're off it."; g.captionT = 5;
            }
        }
        return true;
    }
    // bullshit
    int me = -1; for (int k = 0; k < (int)bs.seats.size(); k++) if (bs.seats[k].kind == 0 && bs.seats[k].idx == p.id) me = k;
    if (act == 3) { if (bsOn && me >= 0) { Note(p, 0, "Walked away from a bullshit hand (and the buy-in)."); bs.seats[me].hand.clear(); } LeaveCards(p); EndGame(p); return true; }
    if (act == 4 && !bsOn) { std::string why; SitAtCards(p, GK_BULLSHIT, 0, &why); g.over = false; return true; }
    if (me < 0 || !bsOn) return true;
    if (act == 25) { std::vector<int> pick; for (int k = 0; k < 24 && k < (int)bs.seats[me].hand.size(); k++) if ((in.gameStake >> k) & 1) pick.push_back(k); if (pick.size() > 4) pick.resize(4); if (bs.Play(me, pick)) bsT = 0; }
    if (act == 26) bs.Call(me);
    if (act == 27 && bs.window > 0) {   // peek at what was just played (a cheat)
        int watcher = bs.lastSeat >= 0 && bs.seats[bs.lastSeat].kind == 1 ? bs.seats[bs.lastSeat].idx : -1;
        if (TryCheat(p, "bullshit", watcher)) { g.caption = std::string("You peek: ") + (bs.lastWasLie ? "it's a lie." : "it's true."); g.captionT = 3; }
        else { Flag("poker_cheat", p.name); g.over = true; g.result = 1; g.caption = "Caught peeking. The table throws you out (and keeps your buy-in)."; g.captionT = 5; bs.seats[me].hand.clear(); }
    }
    return true;
}

// ---------------------------------------------------------------- the tables run themselves (bots think, the clock deals)
static void DriveHand(Night& n, cards::Poker& P, float dt, bool cartel) {
    if (P.street <= 3 && P.turn >= 0) {
        cards::Seat& s = P.seats[P.turn];
        P.actT += dt;
        if (s.kind == 1) { if (P.actT > 1.2f) { int amt = 0; float dr = s.idx >= 0 && s.idx < (int)n.patrons.size() ? n.patrons[s.idx].drunk : 0; int a = P.BotChoose(P.turn, amt, dr); P.Act(P.turn, a, amt); } }
        else if (P.actT > 40) P.Act(P.turn, P.toCall > P.seats[P.turn].bet ? 1 : 2, 0);   // (asleep at the table: check or fold)
    }
    (void)cartel;
}
void Night::StepCards(float dt) {
    // hold'em: a hand at a time while a sailor is sitting; the blinds rise every hour
    bool anyone = false; for (const auto& s : poker.seats) anyone |= s.kind == 0 && !s.out;
    if (!anyone && !poker.seats.empty()) {   // (the table breaks up)
        for (const auto& s : poker.seats) if (s.kind == 1 && s.idx >= 0 && s.idx < (int)patrons.size()) { patrons[s.idx].playing = -1; patrons[s.idx].nextGoalT = 0; }
        poker.seats.clear();
    }
    if (anyone) {
        int hr = (int)(Hour() - 19); poker.sb = 2 + 2 * hr; poker.bb = poker.sb * 2;
        for (auto& s : poker.seats) if (s.kind == 1 && s.idx >= 0 && s.idx < (int)patrons.size()) { const Patron& c = patrons[s.idx]; s.drunk = c.drunk; if (!c.inside || c.gone) s.out = true; }
        for (auto& s : poker.seats) if (s.kind == 0 && s.idx >= 0 && s.idx < (int)players.size()) s.drunk = players[s.idx].drunk;
        if (poker.street == 5) {
            if ((poker.nextT -= dt) <= 0) {
                // the busted leave; the house deals again (or a sailor alone at the table is dealt in with a new patron)
                for (auto& s : poker.seats) if (s.kind == 1 && s.stack <= 0 && !s.out) { s.out = true; if (s.idx < (int)patrons.size()) { patrons[s.idx].playing = -1; patrons[s.idx].nextGoalT = 0; patrons[s.idx].mood = std::max(0.0f, patrons[s.idx].mood - 10); } }
                for (auto& s : poker.seats) if (s.kind == 0 && s.stack <= 0 && !s.out) { s.out = true; Player& q = players[s.idx]; if (q.game.kind == GK_POKER) { q.game.over = true; q.game.result = 1; q.game.caption = "You're out of chips."; q.game.captionT = 5; } Note(q, 4, "Busted out at the poker table."); }
                if (poker.Live() < 2) SeatPatrons(*this, &poker, nullptr, 2);
                if (poker.Live() >= 2) poker.StartHand(); else poker.nextT = 5;
            }
        } else DriveHand(*this, poker, dt, false);
    }
    // the cartel's hand: one hand, then it's settled (the kidney comes back in a hand)
    if (!cartelHand.seats.empty()) {
        if (cartelHand.street <= 3) DriveHand(*this, cartelHand, dt, true);
        else if (cartelHand.street == 5 && cartelHand.hand >= 1 && !cartelHand.winners.empty()) {
            cards::Seat& me = cartelHand.seats[0];
            if (me.idx >= 0 && me.idx < (int)players.size()) {
                Player& p = players[me.idx];
                bool won = cartelHand.winners[0] == 0;
                if (won) {
                    int pay = cartelHand.seats[0].stack * (cartelHand.bluffWin && cartelHand.seats[1].folded ? 2 : 1);
                    p.money += pay;
                    if (p.kidneys < 2) { p.kidneys = 2; Note(p, 5, "Won a kidney back in the cartel's hand."); Flag("kidney_back", p.name); Say("The quiet man slides a cooler across the table. \"Yours, I believe.\""); }
                    else Note(p, 5, TextFormat("Beat the quiet man's hand for %d%s.", pay, cartelHand.bluffWin ? " (he paid double for the bluff)" : ""));
                } else {
                    if (me.kidney) { p.kidneys = std::max(0, p.kidneys - 1); Note(p, 5, "Lost a kidney in the cartel's hand."); Flag("kidney", p.name); }
                    else Note(p, 4, "Lost 100 to the quiet man.");
                }
                if (p.game.kind == GK_POKER && p.game.machine == 1) { p.game.over = true; p.game.result = won ? 0 : 1; p.game.caption = cartelHand.result; p.game.captionT = 6; }
            }
            cartelHand.seats.clear();
        }
    }
    // bullshit: the deal when three are seated; bots play and call; the winner takes the pot
    if (!bsOn && !bs.seats.empty() && (bsT -= dt) <= 0) {
        bool human = false; for (const auto& s : bs.seats) human |= s.kind == 0;
        if (!human) bs.seats.clear();
        else {
            if (bs.seats.size() < 3) SeatPatrons(*this, nullptr, &bs, 3 + (int)(Rand() * 2) - (int)bs.seats.size() + 1);
            if (bs.seats.size() >= 3) { bs.pot = 20 * (int)bs.seats.size(); bs.Deal(); bsOn = true; bsT = 1.5f; Say("A hand of bullshit is dealt in the card room."); for (auto& s : bs.seats) if (s.kind == 0) { Player& q = players[s.idx]; q.game.over = false; } }
            else bsT = 5;
        }
    }
    if (bsOn) {
        for (auto& s : bs.seats) { if (s.kind == 1 && s.idx >= 0 && s.idx < (int)patrons.size()) s.drunk = patrons[s.idx].drunk; if (s.kind == 0 && s.idx < (int)players.size()) s.drunk = players[s.idx].drunk; }
        bs.revealT = std::max(0.0f, bs.revealT - dt);
        if (bs.window > 0) {
            float before = bs.window; bs.window -= dt;
            // each bot decides once, early in the window, whether to call it
            if (before > 2.6f && bs.window <= 2.6f) {
                int first = bs.rng.I((int)bs.seats.size());
                for (int q = 0; q < (int)bs.seats.size(); q++) { int s = (first + q) % (int)bs.seats.size(); if (s == bs.lastSeat || bs.seats[s].kind != 1) continue; if (bs.rng.U() < bs.BotDoubt(s)) { bs.Call(s); break; } }
            }
            if (bs.window > 0 && bs.window <= 0.01f) bs.Resolve(false, -1);
            if (bs.window <= 0 && bs.callSeat < 0 && bs.winner < 0 && before > 0) bs.Resolve(false, -1);
        } else if (bs.winner < 0 && (bsT -= dt) <= 0) {
            cards::BsSeat& s = bs.seats[bs.turn];
            if (s.kind == 1 || bsT < -40) { std::vector<int> v; bs.BotPlay(bs.turn, v); bs.Play(bs.turn, v); bsT = 1.6f; }   // (a sailor asleep for 40 s is played for)
        }
        if (bs.winner >= 0) {
            cards::BsSeat& w = bs.seats[bs.winner];
            for (auto& s : bs.seats) if (s.kind == 0 && s.idx < (int)players.size()) { Player& q = players[s.idx]; bool me = &s == &w; if (me) { q.money += bs.pot; q.gamesWon++; Note(q, 3, TextFormat("Won a hand of bullshit (%d).", bs.pot)); } if (q.game.kind == GK_BULLSHIT) { q.game.over = true; q.game.result = me ? 0 : 1; q.game.caption = bs.result; q.game.captionT = 6; } }
            for (auto& s : bs.seats) if (s.kind == 1 && s.idx < (int)patrons.size()) { patrons[s.idx].playing = -1; patrons[s.idx].nextGoalT = 0; }
            Say(bs.result); bsOn = false;
            bs.seats.erase(std::remove_if(bs.seats.begin(), bs.seats.end(), [](const cards::BsSeat& s) { return s.kind == 1; }), bs.seats.end());
            for (auto& s : bs.seats) s.hand.clear();
            bs.seats.erase(std::remove_if(bs.seats.begin(), bs.seats.end(), [&](const cards::BsSeat& s) { return s.kind == 0 && (s.idx >= (int)players.size() || players[s.idx].game.kind != GK_BULLSHIT); }), bs.seats.end());
            bs.seats.clear();
        }
    }
}

// ---------------------------------------------------------------- --night-test's stage 8 checks (the gate: the kidney comes back in a hand)
int NightCardChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    // the gate: a sailor down a kidney sits in the cartel's hand and wins it back
    {
        Night n; Opts o; o.seed = 808; o.events = false; n.Init(o);
        n.t = 5 * 60 * SECONDS_PER_GAME_MINUTE; n.ForceEvent("cartel", 24.0f); n.Step(0.02f);
        Player& p = n.players[0]; p.st = State::Active; p.kidneys = 1; p.money = 40; p.pos = {37.0f, 14.5f};
        check(n.EventOn("cartel"), "the cartel is in");
        bool offered = false; for (const auto& op : n.EventOptions(p)) offered |= op.act == 40;
        check(offered, "the quiet man offers his hand (a kidney in the pot)");
        p.in.evAct = 40; n.Step(0.02f);
        check(p.game.kind == GK_POKER && p.game.machine == 1 && n.cartelHand.seats.size() == 2 && n.cartelHand.seats[0].kidney, "a kidney goes in the middle");
        // (the test's luck: the sailor holds aces, the quiet man seven-deuce)
        n.cartelHand.seats[0].hole[0] = {14, 0}; n.cartelHand.seats[0].hole[1] = {14, 1}; n.cartelHand.seats[1].hole[0] = {7, 2}; n.cartelHand.seats[1].hole[1] = {2, 3};
        for (auto it = n.cartelHand.deck.begin(); it != n.cartelHand.deck.end();) { if ((it->r == 14 && it->s <= 1) || (it->r == 7 && it->s == 2) || (it->r == 2 && it->s == 3)) it = n.cartelHand.deck.erase(it); else ++it; }
        for (int k = 0; k < 4000 && p.game.kind == GK_POKER && !p.game.over; k++) {
            if (n.cartelHand.turn == 0 && n.cartelHand.street <= 3) { p.in.gameAct = 22; }
            n.Step(0.05f);
        }
        check(p.kidneys == 2, TextFormat("the kidney comes back in a hand: %s", p.game.caption.c_str()));
        std::string h = n.Headline(); check(!h.empty(), "the morning: " + h);
    }
    // hold'em in the card room: buy in, play hands against the regulars, cash out
    {
        Night n; Opts o; o.seed = 909; o.events = false; n.Init(o);
        for (int i = 0; i < (int)(3 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        Player& p = n.players[0]; p.st = State::Active; p.pos = {34.5f, 11.8f}; p.money = 200;
        check(n.NearGame(p) == GK_POKER, "the poker table");
        p.in.startGame = GK_POKER; n.Step(0.02f);
        check(p.game.kind == GK_POKER && n.poker.seats.size() >= 3 && p.money == 150, TextFormat("buy in for 50; %d at the table", (int)n.poker.seats.size()));
        int hands0 = n.poker.hand;
        for (int k = 0; k < 6000 && n.poker.hand < hands0 + 4; k++) {
            int me = -1; for (int s = 0; s < (int)n.poker.seats.size(); s++) if (n.poker.seats[s].kind == 0) me = s;
            if (me >= 0 && n.poker.turn == me && n.poker.street <= 3) { p.in.gameAct = 22; }
            n.Step(0.05f);
        }
        check(n.poker.hand >= hands0 + 3, TextFormat("hands are dealt and played (%d); the blinds are %d/%d at %s", n.poker.hand, n.poker.sb, n.poker.bb, n.Clock().c_str()));
        float m0 = p.money; p.in.gameAct = 3; n.Step(0.02f);
        check(p.game.kind < 0 && p.money >= m0, TextFormat("cash out (%.0f)", p.money));
    }
    // bullshit: three or more, a winner takes the pot
    {
        Night n; Opts o; o.seed = 919; o.events = false; n.Init(o);
        for (int i = 0; i < (int)(3 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        Player& p = n.players[0]; p.st = State::Active; p.pos = {34.5f, 21.2f}; p.money = 100;
        check(n.NearGame(p) == GK_BULLSHIT, "the bullshit table");
        p.in.startGame = GK_BULLSHIT; n.Step(0.02f);
        for (int k = 0; k < 400 && !n.bsOn; k++) n.Step(0.05f);
        check(n.bsOn && n.bs.seats.size() >= 3, TextFormat("dealt to %d", (int)n.bs.seats.size()));
        int plays = 0;
        for (int k = 0; k < 40000 && n.bsOn; k++) {
            int me = -1; for (int s = 0; s < (int)n.bs.seats.size(); s++) if (n.bs.seats[s].kind == 0) me = s;
            if (me >= 0 && n.bs.turn == me && n.bs.window <= 0 && n.bs.winner < 0) { std::vector<int> v; n.bs.BotPlay(me, v); int mask = 0; for (int x : v) mask |= 1 << x; p.in.gameAct = 25; p.in.gameStake = mask; plays++; }
            n.Step(0.05f);
        }
        check(!n.bsOn && p.game.over, TextFormat("the hand ends (%d plays of yours): %s", plays, p.game.caption.c_str()));
    }
    // a side bet on another sailor's darts; a cheat at darts, caught
    {
        Night n; Opts o; o.seed = 929; o.players = 2; o.events = false; n.Init(o);
        for (int i = 0; i < (int)(3 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        Player& a = n.players[0]; Player& b = n.players[1]; a.st = b.st = State::Active; a.pos = {2.6f, 7.5f}; b.pos = {3.5f, 8.5f};
        auto ch = n.Challengers(a, GK_DARTS); a.in.startGame = GK_DARTS; a.in.gameOpp = ch.empty() ? -1 : ch[0]; a.in.gameStake = 10; n.Step(0.02f);
        bool offered = false; for (const auto& op : n.EventOptions(b)) offered |= op.act == 30;
        check(offered && !ch.empty(), "a side bet on the other sailor's match");
        b.in.evAct = 30; b.in.evArg = 0; n.Step(0.02f);
        float bm = b.money + b.tab * 0;
        GRng r; r.s = 9;
        for (int k = 0; k < 20000 && !a.game.over; k++) { if (a.game.darts.turn == 0 && a.game.botT <= 0) { Vector2 aim = a.game.darts.BotAim(); a.in.gameAim = {aim.x + r.N() * 20, aim.y + r.N() * 20}; a.in.gameAct = 1; } n.Step(0.05f); }
        check(a.game.over && fabsf(b.money - bm) == 10, TextFormat("the side bet settles with the match (%s: %+.0f)", a.game.result == 0 ? "won" : "lost", b.money - bm));
        // cheating: the roll is charisma and sobriety; a wrecked cheat is caught
        a.drunk = 95; int caught = 0; for (int k = 0; k < 50; k++) { int c0 = a.cheatsCaught; n.TryCheat(a, "darts", -1); caught += a.cheatsCaught > c0; }
        a.drunk = 0; int caughtSober = 0; for (int k = 0; k < 50; k++) { int c0 = a.cheatsCaught; n.TryCheat(a, "darts", -1); caughtSober += a.cheatsCaught > c0; }
        check(caught > caughtSober + 15, TextFormat("a wrecked cheat is caught far more than a sober one (%d vs %d of 50)", caught, caughtSober));
    }
    return fails;
}

}  // namespace no
