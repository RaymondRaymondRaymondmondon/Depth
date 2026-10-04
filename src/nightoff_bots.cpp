// A Night Off: the modes (doc p. 24), what players do to each other (p. 23), and the bot player (an AI seat, a dropped
// guest, --night-sim): it only ever writes its Input, exactly as a person would. Stage 6. Headless.
#include "nightoff.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace no {

const char* ModeName(int m) { static const char* N[MD_COUNT] = {"Night Off", "The Crew", "Last One Standing", "The Wager", "Rival Crews", "Sober Night", "Solo"}; return m >= 0 && m < MD_COUNT ? N[m] : "?"; }
const char* ModeRule(int m) {
    static const char* R[MD_COUNT] = {
        "As designed: everyone's night is their own, the headline is about the group.",
        "Co-op: one shared score and one shared tab; the night ends when anyone is arrested or hospitalized.",
        "The night ends when one player is left in the bar, by any means. The last one wins.",
        "Each of you bets at 7 p.m. on what your night will be; hit it for +300. The bets are revealed in the morning.",
        "3 against 3: two crews, shared crew scores, and the midnight brawl is on the schedule.",
        "No drinks: the bartender's on strike. Charisma only.",
        "One sailor, a quiet crowd, and the bartender narrating everything." };
    return m >= 0 && m < MD_COUNT ? R[m] : "";
}
const char* WagerName(int w) { static const char* N[WG_COUNT] = {"go home with someone", "walk out with 500 more", "win a fight sober", "survive the night with both kidneys"}; return w >= 0 && w < WG_COUNT ? N[w] : "?"; }

// ---------------------------------------------------------------- the modes
void Night::StepModes(float dt) {
    (void)dt;
    int present = 0, last = -1;
    for (const auto& p : players) if (p.st != State::Gone && p.st != State::PassedOut) { present++; last = p.id; }
    // The Crew: an arrest or a hospital ends it for everyone
    if (opts.mode == MD_CREW) for (const auto& p : players) if (p.ending == E_ARRESTED || p.ending == E_HOSPITAL) {
        for (auto& q : players) if (q.st != State::Gone && q.st != State::PassedOut) { Note(q, 8, p.name + "'s night ended the crew's."); Leave(q, E_CLOSING, "in the street with the rest of the crew"); }
        break;
    }
    // Last One Standing: the last one in the bar wins
    if (opts.mode == MD_LAST_STANDING && players.size() > 1 && present <= 1 && winner < 0) {
        winner = last >= 0 ? last : 0;
        if (last >= 0) { Note(players[last], 5, "Last one standing in the Gull."); Leave(players[last], E_CLOSING, "the last one standing, at the bar, alone and triumphant"); }
    }
    // Rival Crews: the midnight brawl is scheduled
    if (opts.mode == MD_RIVAL_CREWS && !midnightBrawl && Hour() >= 24) {
        midnightBrawl = true;
        int a = -1, b = -1;
        for (const auto& p : players) if (p.st == State::Active) { if (p.crew2 == 0 && a < 0) a = p.id; if (p.crew2 == 1 && b < 0) b = p.id; }
        if (a >= 0 && b >= 0) { Say("Midnight. The two crews put down their glasses."); StartBrawl(PlayerW(a), PlayerW(b), PlayerW(a));
            for (auto& p : players) if (p.st == State::Active && p.fight.brawl < 0) { Combat* c = CombatOf(PlayerW(p.id)); int br = players[a].fight.brawl; if (br >= 0) { c->brawl = br; c->side = p.crew2 == 0 ? players[a].fight.side : 1 - players[a].fight.side; brawls[br].size++; } } }
    }
}

// ---------------------------------------------------------------- players and players
void Night::PlayerTricks(Player& p) {
    Input& in = p.in;
    // a round for everyone within 5 m: their mood, your tab, the bartender's favour, a flirt's interest
    if (in.buyRound && NearServe(p) && opts.mode != MD_SOBER) {
        int n = 0; float price = PriceOf(DrinkIndex("pint"));
        for (auto& c : patrons) if (c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 5) { c.mood = std::min(100.0f, c.mood + 10); n++; }
        for (auto& q : players) if (q.id != p.id && (q.st == State::Active) && Vector2Distance(q.pos, p.pos) < 5) { q.drunk = std::min(100.0f, q.drunk + 20); q.drinks++; n++; }
        p.tab += price * (n + 1); bar.mood = std::min(100.0f, bar.mood + 3); p.roundT = t;
        for (const auto& c : patrons) if (c.role == "a rival sailor" && c.inside && Vector2Distance(c.pos, p.pos) < 6) partied = true;
        if (partied) for (auto& c : patrons) if (c.role == "a rival sailor") c.mood = 90;
        Say(TextFormat("%s buys a round (%d drinks).", p.name.c_str(), n + 1)); Note(p, 1, TextFormat("Bought a round of %d.", n + 1));
    }
    // a Gull in a friend's pint when they're not looking: +35 and a story
    if (in.spike >= 0 && in.spike < (int)players.size() && in.spike != p.id && NearServe(p)) {
        Player& q = players[in.spike];
        if (q.st == State::Active && Vector2Distance(q.pos, p.pos) < 4) {
            p.tab += 15; q.drunk = std::min(100.0f, q.drunk + 35); q.drinks++; q.peakDrunk = std::max(q.peakDrunk, q.drunk);
            Note(p, 5, "Spiked " + q.name + "'s pint with a Gull."); Note(q, 1, "A pint that tasted of the Gull.");
            if (q.drunk >= 100) Leave(q, E_PASSED_OUT, "");
        }
    }
    // carry a passed-out friend out (to the door: they wake in their own bunk, nothing lost), or put them down
    if (in.carry) {
        if (p.carrying >= 0) { players[p.carrying].carriedBy = -1; p.carrying = -1; }
        else for (auto& q : players) if (q.id != p.id && q.st == State::PassedOut && q.carriedBy < 0 && Vector2Distance(q.pos, p.pos) < 1.6f) { p.carrying = q.id; q.carriedBy = p.id; Say(p.name + " hauls " + q.name + " over a shoulder."); break; }
    }
    if (p.carrying >= 0) {
        Player& q = players[p.carrying];
        q.pos = Vector2Add(p.pos, {-cosf(p.yaw) * 0.3f, -sinf(p.yaw) * 0.3f});
        if (Vector2Distance(p.pos, D().bar.door) < 1.5f) {
            q.money += q.lostOnPass; q.lostOnPass = 0; q.wokeAt = "in your own bunk on the Nautilus: " + p.name + " carried you home";
            Note(p, 5, "Carried " + q.name + " out of the Gull."); Note(q, 8, p.name + " carried you home.");
            q.carriedBy = -1; p.carrying = -1;
        }
    }
    // draw on a passed-out friend's face (it lasts till the morning screen)
    if (in.drawFace) for (auto& q : players) if (q.id != p.id && q.st == State::PassedOut && !q.faceDrawn && Vector2Distance(q.pos, p.pos) < 1.6f) { q.faceDrawn = true; Note(p, 5, "Drew a moustache on " + q.name + "."); break; }
    // the Wager: a secret bet, made at 7 p.m.
    if (in.wager >= 0 && opts.mode == MD_WAGER && p.wager < 0 && Hour() < 19.5f) p.wager = std::clamp(in.wager, 0, WG_COUNT - 1);
    // an emote (doc p. 25): toast, point, laugh, shrug, fists up, fall over
    if (in.emote > 0 && in.emote <= 6) { p.emote = in.emote; p.emoteT = 2.5f; if (in.emote == 6 && p.st == State::Active) p.fight.fallT = 1.6f; }
    p.emoteT = std::max(0.0f, p.emoteT - 1 / 60.0f);
    in.buyRound = false; in.spike = -1; in.carry = false; in.drawFace = false; in.wager = -1; in.emote = 0;
}

// ---------------------------------------------------------------- the bot player
static Vector2 Station(int goal, const Night& n, int arg) {
    const BarData& B = D().bar;
    switch (goal) {
        case 0: return {13.5f + (arg % 6) * 1.2f, 8.4f};   // the bar
        case 1: return {2.6f, 7.5f};                         // the throw line
        case 2: return {3.3f + (arg % 2) * 0.1f, arg % 2 ? 11.9f : 4.9f};   // a pool table
        case 3: return B.golf;
        case 4: return {1.5f, 13.5f + (arg % 3) * 1.2f};    // a slot machine
        case 5: return B.scratch;
        case 6: return {B.fortune.x - 0.9f, B.fortune.y};
        case 7: case 8: { int c = std::clamp(arg, 0, (int)n.patrons.size() - 1); return n.patrons.empty() ? B.serve : n.patrons[c].pos; }   // a patron (talk, flirt)
        case 9: return B.door;
        case 10: return B.hatch;
        case 11: return {34.5f, 11.9f};    // the poker table
        case 12: return {34.5f, 21.2f};    // the bullshit table
    }
    return B.serve;
}
void Night::BotPlayer(Player& p, float dt) {
    Input& in = p.in; in.moveX = in.moveZ = 0; in.run = false;
    if (p.st != State::Active) return;
    p.botT -= dt;
    // decisions inside something: a conversation, a flirt, a game, a fight
    if (p.talk.patron >= 0) { if (!p.talk.over && p.botT <= 0) { const Patron& c = patrons[p.talk.patron]; int best = 0; float bd = 1e9f; for (int o = 0; o < 4; o++) { float d = Difficulty(c, o); if (d < bd) { bd = d; best = o; } } in.say = Rand() < 0.15f ? 5 : best; p.botT = 1.2f; } return; }
    if (p.flirt.patron >= 0) {
        if (p.flirt.offer && p.botT <= 0) { in.offer = Rand() < (p.botStyle ? 0.8f : 0.25f) ? 1 : 2; p.botT = 1; }
        else if (!p.flirt.over && p.botT <= 0) { const Patron& c = patrons[p.flirt.patron]; int best = 0; float bo = -1; for (int o = 0; o < 4; o++) { int opt = p.flirt.round == 0 ? o : (o == 2 ? 4 : o == 3 ? 5 : o); float v = FlirtOdds(p, c, opt); if (v > bo) { bo = v; best = o; } } in.flirtSay = best; p.botT = 1.4f; }
        return;
    }
    if (p.game.kind >= 0) {
        GameSeat& g = p.game;
        if (g.over) { if (p.botT <= 0) { in.gameAct = (g.result == 0 && p.botStyle && Rand() < 0.4f) ? 4 : 3; p.botT = 1.5f; } return; }
        float sk = PlayerAim(p);
        GRng r; r.s = (uint32_t)(t * 977) + p.id * 31 + 1;
        switch (g.kind) {
            case GK_DARTS: if (g.darts.turn == 0 && g.botT <= 0 && p.botT <= 0) { Vector2 a = g.darts.BotAim(); float s = GD().dartSigma * sk; in.gameAim = {a.x + r.N() * s, a.y + r.N() * s}; in.gameAct = 1; p.botT = 0.7f; } break;
            case GK_POOL: if (g.pool.turn == 0 && g.replayT <= 0 && p.botT <= 0) { if (g.pool.ballInHand) { in.gameAim = pool::BotPlace(g.pool, r); in.gameAct = 2; } else { pool::Shot s = pool::BotShot(g.pool, sk, r); in.gameAim = {s.ang, 0}; in.gamePower = s.power; in.gameEnglish = s.english; in.gameAct = 1; } p.botT = 1.0f; } break;
            case GK_GOLF: if (g.golf.turn == 0 && g.replayT <= 0 && p.botT <= 0) { float a, pw; golf::BotShot(g.golf, sk, r, a, pw); in.gameAim = {a, 0}; in.gamePower = pw; in.gameAct = 1; p.botT = 1.0f; } break;
            case GK_SLOTS: if (p.botT <= 0) { in.gameAct = (g.pulls < 6 + p.botStyle * 10 && p.money > 20) ? 1 : 3; p.botT = 1.2f; } break;
            case GK_SCRATCH: case GK_PIP: if (p.botT <= 0) { in.gameAct = !g.haveTicket ? 5 : !g.paid ? 1 : 3; p.botT = 1.5f; } break;
            case GK_POKER: {
                cards::Poker& P = g.machine == 1 ? cartelHand : poker; int me = -1; for (int k = 0; k < (int)P.seats.size(); k++) if (P.seats[k].kind == 0 && P.seats[k].idx == p.id && !P.seats[k].out) me = k;
                if (me < 0 || g.over) { in.gameAct = 3; break; }
                if (P.turn == me && P.street <= 3 && p.botT <= 0) { int amt = 0; int a = P.BotChoose(me, amt, p.drunk); in.gameAct = 20 + a; in.gamePower = (float)amt; p.botT = 1.0f; }
                if (P.street == 5 && P.hand >= 8 + p.botStyle * 10 && p.botT <= 0) { in.gameAct = 3; p.botT = 1; }
            } break;
            case GK_BULLSHIT: {
                int me = -1; for (int k = 0; k < (int)bs.seats.size(); k++) if (bs.seats[k].kind == 0 && bs.seats[k].idx == p.id) me = k;
                if (g.over || (!bsOn && p.botT <= 0 && g.captionT <= 0)) { in.gameAct = 3; p.botT = 1; break; }
                if (me < 0 || !bsOn) break;
                if (bs.turn == me && bs.window <= 0 && bs.winner < 0 && p.botT <= 0) { std::vector<int> v; bs.BotPlay(me, v); int mask = 0; for (int x : v) mask |= 1 << x; in.gameAct = 25; in.gameStake = mask; p.botT = 1.2f; }
                else if (bs.window > 0 && bs.window < 2.5f && bs.lastSeat != me && p.botT <= 0) { if (Rand() < bs.BotDoubt(me) * 0.5f) in.gameAct = 26; p.botT = 3; }
            } break;
            case GK_DANCE: if (p.botT <= 0) { if (g.captionT <= 0 && !g.over) { in.gameAct = 1; in.gamePower = 0.4f + Rand() * 0.55f - p.drunk / 300; } else in.gameAct = 3; p.botT = 8; } break;
            case GK_FORTUNE: if (p.botT <= 0) { in.gameAct = !g.haveReading ? 1 : 3; if (g.haveReading && p.fortuneAsked && p.botStyle && Rand() < 0.5f) in.fortuneYes = true; p.botT = 3.0f; } break;
        }
        return;
    }
    if (p.fight.brawl >= 0) {   // fight: the nearest of the other side (the careful one backs off to the door if it's going badly)
        if (p.botStyle == 0 && p.fight.hp < p.fight.hpMax * 0.4f) { p.botGoal = 9; }
        else {
            Who f{}; float bd = 12;
            for (int i = 0; i < (int)patrons.size(); i++) { const Patron& c = patrons[i]; if (c.inside && !c.gone && c.fight.brawl == p.fight.brawl && c.fight.side != p.fight.side && !c.fight.Down()) { float d = Vector2Distance(c.pos, p.pos); if (d < bd) { bd = d; f = PatronW(i); } } }
            for (const auto& q : players) if (q.id != p.id && q.fight.brawl == p.fight.brawl && q.fight.side != p.fight.side && !q.fight.Down() && Present(PlayerW(q.id))) { float d = Vector2Distance(q.pos, p.pos); if (d < bd) { bd = d; f = PlayerW(q.id); } }
            if (f.Valid()) {
                Vector2 to = Vector2Subtract(*PosOf(f), p.pos); p.yaw = atan2f(to.y, to.x);
                if (bd > 1.1f) { in.moveX = to.x / bd; in.moveZ = to.y / bd; }
                else if (p.fight.recT <= 0 && p.fight.windT <= 0 && p.botT <= 0) { float u = Rand(); in.attack = u < 0.6f ? MV_JAB : u < 0.85f ? MV_HAYMAKER : MV_SHOVE; in.block = false; p.botT = 0.3f + Rand() * 0.5f; }
                return;
            }
        }
    }
    // an event's offer: now and then the bot takes one (the careful never the piano or the kitty)
    if (p.botT <= 0 && Rand() < 0.04f) {
        auto opts = EventOptions(p);
        for (const auto& o : opts) { bool risky = o.act == 11 || o.act == 16 || o.act == 17 || o.act == 7 || o.act == 3; if (risky && p.botStyle == 0) continue; if (Rand() < 0.5f) { in.evAct = o.act; in.evArg = o.arg; p.botT = 2; return; } }
    }
    // the reckless pick fights when they're hammered
    if (p.botStyle == 1 && p.drunk >= 60 && (p.botFightT -= dt) <= 0) {
        p.botFightT = 25 + Rand() * 40;
        if (Rand() < 0.4f) { int c = NearestPatron(p, 1.6f); if (c >= 0 && patrons[c].type != T_STAFF) { p.yaw = atan2f(patrons[c].pos.y - p.pos.y, patrons[c].pos.x - p.pos.x); in.attack = MV_HAYMAKER; return; } }
    }
    // a goal: drink to the night's target, play, talk, flirt, eat, or go home
    if (p.botGoal < 0 || p.botT < -40) {
        p.botT = 0; p.botPath.clear();
        float h = Hour();
        int g;
        if (h >= p.botLeaveH || (p.botStyle == 0 && p.money - p.tab < 30)) g = 9;
        else if (p.drunk < p.botDrinkTo && opts.mode != MD_SOBER && Rand() < 0.55f) g = 0;
        else if (p.drunk > 70 && Rand() < 0.4f) g = 10;
        else { float u = Rand(); g = u < 0.16f ? 1 : u < 0.28f ? 2 : u < 0.33f && h < 26 ? 3 : u < 0.4f ? 4 : u < 0.43f ? 5 : u < 0.46f ? 6 : u < 0.52f ? 11 : u < 0.57f ? 12 : u < 0.8f ? 7 : 8; }
        p.botGoal = g; p.botArg = (int)(Rand() * 1000);
        if (g == 7 || g == 8) {   // someone to talk to (or flirt with): a patron in the bar who's free
            std::vector<int> c; for (const auto& q : patrons) if (q.inside && !q.gone && !q.leaving && q.talkingTo < 0 && q.playing < 0 && q.type != T_STAFF && q.fight.brawl < 0 && (g == 7 || q.home != "none")) c.push_back(q.id);
            if (c.empty()) p.botGoal = 0; else p.botArg = c[(int)(Rand() * c.size()) % c.size()];
        }
        p.botTarget = Station(p.botGoal, *this, p.botArg);
        p.botPath = NavPath(p.pos, p.botTarget);
        if (getenv("DEPTH_BOTTRACE")) printf("  [%s] %s: goal %d at (%.1f, %.1f) from (%.1f, %.1f), drunk %.0f, money %.0f tab %.0f | st %d game %d talk %d flirt %d busy %d grab %d,%d down %.0f path %d\n", Clock().c_str(), p.name.c_str(), p.botGoal, p.botTarget.x, p.botTarget.y, p.pos.x, p.pos.y, p.drunk, p.money, p.tab, (int)p.st, p.game.kind, p.talk.patron, p.flirt.patron, (int)p.fight.Busy(), p.fight.grabbedBy.kind, p.fight.grabbedBy.idx, p.fight.downT, (int)p.botPath.size());
    }
    if (p.botGoal == 7 || p.botGoal == 8) p.botTarget = Station(p.botGoal, *this, p.botArg);
    // walk there (the doorways first)
    Vector2 aim = p.botTarget;
    while (!p.botPath.empty() && Vector2Distance(p.pos, D().bar.nav[p.botPath.front()]) < 0.8f) p.botPath.erase(p.botPath.begin());
    if (!p.botPath.empty() && Vector2Distance(p.pos, p.botTarget) > 2.5f) aim = D().bar.nav[p.botPath.front()];
    Vector2 to = Vector2Subtract(aim, p.pos); float L = Vector2Length(to);
    float arrive = p.botGoal >= 7 && p.botGoal <= 8 ? 1.3f : 0.7f;
    if (Vector2Distance(p.pos, p.botTarget) > arrive) {
        if (L > 0.01f) { in.moveX = to.x / L; in.moveZ = to.y / L; }
        if (p.botT < -25) p.botGoal = -1;   // (stuck: think again)
        return;
    }
    // arrived
    if (p.botT > 0) return;
    int g = p.botGoal; p.botGoal = -1; p.botT = 1.0f + Rand() * 2;
    switch (g) {
        case 0: { static const char* CAREFUL[4] = {"pint", "small", "pint", "water"}, * RECKLESS[5] = {"rum", "gull", "whisky", "absinthe", "pint"};
                  const char* k = p.botStyle ? RECKLESS[(int)(Rand() * 5) % 5] : CAREFUL[(int)(Rand() * 4) % 4]; in.order = DrinkIndex(k);
                  if (p.botStyle && Rand() < 0.08f) in.buyRound = true; } break;
        case 1: case 2: case 3: {
            int kind = g == 1 ? GK_DARTS : g == 2 ? GK_POOL : GK_GOLF;
            int mach = 0; if (NearGame(p, &mach) != kind) break;
            auto ch = Challengers(p, kind); int opp = ch.empty() ? -1 : ch[(int)(Rand() * ch.size()) % ch.size()];
            in.startGame = kind; in.gameMachine = mach; in.gameOpp = opp; in.gameStake = opp >= 0 ? (p.botStyle ? 20 : 10) : 0;
            if (in.gameStake > p.money) in.gameStake = 0;
        } break;
        case 4: { int mach = 0; if (NearGame(p, &mach) == GK_SLOTS) { in.startGame = GK_SLOTS; in.gameMachine = mach; } } break;
        case 5: if (NearGame(p) == GK_SCRATCH) in.startGame = GK_SCRATCH; break;
        case 6: if (NearGame(p) == GK_FORTUNE) in.startGame = GK_FORTUNE; break;
        case 7: in.talkTo = p.botArg; break;
        case 8: in.flirtWith = p.botArg; break;
        case 9: in.leave = true; p.botGoal = 9; break;
        case 10: in.order = DrinkIndex(Rand() < 0.5f ? "chips" : "stew"); break;
        case 11: if (NearGame(p) == GK_POKER && p.money >= 50) in.startGame = GK_POKER; break;
        case 12: if (NearGame(p) == GK_BULLSHIT && p.money >= 20) in.startGame = GK_BULLSHIT; break;
    }
}

// ---------------------------------------------------------------- --night-sim <crowd> <players> [runs] [careful|reckless|mixed] [mode]
int RunNightSim(int crowd, int players, int runs, int style, int mode) {
    printf("A Night Off: %d bot night(s), crowd %d, %d player(s), %s, mode %s\n", runs, crowd, players, style == 0 ? "careful" : style == 1 ? "reckless" : "mixed", ModeName(mode));
    std::map<std::string, int> heads, ends, evs; double evN = 0;
    double drinks = 0, fights = 0, money = 0, homeGood = 0, homeAny = 0, kept300 = 0, kidneyLost = 0, arrested = 0, score = 0, games = 0, brawls = 0, wagers = 0, wagerHit = 0;
    int n = 0;
    for (int run = 0; run < runs; run++) {
        Night N; Opts o; o.players = players; o.crowd = crowd; o.seed = 1000 + run * 77; o.mode = mode; { const char* b = getenv("DEPTH_BAR"); o.bar = b ? std::clamp(atoi(b), 0, BAR_COUNT - 1) : 0; const char* se = getenv("DEPTH_SEASON"); o.season = se ? atoi(se) : 0; } N.Init(o);
        for (auto& p : N.players) { p.bot = true; p.botStyle = style == 2 ? (p.id % 2) : style; p.botDrinkTo = p.botStyle ? 70 + N.Rand() * 25 : 25 + N.Rand() * 20; p.botLeaveH = p.botStyle ? 26.5f : 24.0f + N.Rand() * 1.5f; if (mode == MD_WAGER) p.in.wager = p.botStyle ? WG_HOME : WG_SURVIVE; }
        while (!N.over) { for (auto& p : N.players) if (p.bot) N.BotPlayer(p, 0.1f); N.Step(0.1f); }
        for (const auto& p : N.players) {
            n++; drinks += p.drinks; money += p.money - 200; fights += p.fightsWon; games += p.gamesWon; score += N.Score(p);
            ends[EndingName(p.ending)]++;
            homeAny += p.ending == E_HOME_WITH; homeGood += p.ending == E_HOME_WITH && !p.homeBad;
            kept300 += p.money >= 300 && p.kidneys >= 2; kidneyLost += p.kidneys < 2; arrested += p.ending == E_ARRESTED;
            if (p.wager >= 0) { wagers++; for (const auto& l : N.ScoreBreakdown(p)) wagerHit += l.what.find("The wager") == 0; }
        }
        brawls += N.brawls.size();
        for (const auto& e : N.events) if (e.started) { evN++; std::string nm = "?"; for (int id : e.people) if (id < (int)N.patrons.size()) { nm = N.patrons[id].secret; break; } if (e.people.empty()) nm = N.lockIn ? "here for The lock-in" : "here for The goat"; evs[nm.size() > 9 ? nm.substr(9) : nm]++; }
        heads[N.Headline()]++;
    }
    printf("  per player: %.1f drinks, %.2f games won, %.2f fights won, money %+.0f, score %.0f\n", drinks / n, games / n, fights / n, money / n, score / n);
    printf("  brawls a night %.2f; went home with someone %.0f%% (well %.0f%%); kidney lost %.0f%%; arrested %.0f%%; kept 300+ with both kidneys %.0f%%\n", brawls / runs, 100 * homeAny / n, 100 * homeGood / n, 100 * kidneyLost / n, 100 * arrested / n, 100 * kept300 / n);
    if (wagers > 0) printf("  wagers hit %.0f%%\n", 100 * wagerHit / wagers);
    printf("  events a night %.2f:", evN / runs); for (const auto& e : evs) printf(" %s %d;", e.first.c_str(), e.second); printf("\n");
    printf("  endings:"); for (const auto& e : ends) printf(" %s %d;", e.first.c_str(), e.second); printf("\n");
    printf("  headlines:\n"); for (const auto& h : heads) printf("    %3d  %s\n", h.second, h.first.c_str());
    return 0;
}

} // namespace no
