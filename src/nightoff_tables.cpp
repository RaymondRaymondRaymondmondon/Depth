// A Night Off: the bar games in the night (stage 3): the stations, who'll play you, starting a game, your moves (all
// through Input, so a guest's are the same as the host's), the opponents' turns, and settling up. Headless.
#include "nightoff.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

const char* GameName(int k) {
    static const char* N[GK_COUNT] = {"darts", "pool", "mini golf", "the slot machine", "a scratch-off", "the fortune teller", "Pip's scratch-offs", "the dance floor", "poker", "bullshit"};
    return k >= 0 && k < GK_COUNT ? N[k] : "?";
}
static float RectDist(Vector2 p, Rectangle r) { float dx = std::max({r.x - p.x, 0.0f, p.x - (r.x + r.width)}), dz = std::max({r.y - p.y, 0.0f, p.y - (r.y + r.height)}); return sqrtf(dx * dx + dz * dz); }
static const Vector2 THROW_LINE{2.6f, 7.5f};

int Night::NearGame(const Player& p, int* machine) const {
    const BarData& B = D().bar; int m = 0;
    if (Vector2Distance(p.pos, THROW_LINE) < 1.2f) { if (machine) *machine = 0; return GK_DARTS; }
    for (const auto& b : B.boxes) {
        if (b.kind == "pool") { if (RectDist(p.pos, b.r) < 0.9f) { if (machine) *machine = m; return GK_POOL; } m++; }
    }
    m = 0;
    for (const auto& b : B.boxes) {
        if (b.kind == "slot") { if (RectDist(p.pos, b.r) < 0.8f) { if (machine) *machine = m; return GK_SLOTS; } m++; }
    }
    if (Vector2Distance(p.pos, B.scratch) < 1.0f) return GK_SCRATCH;
    if (Vector2Distance(p.pos, B.fortune) < 1.8f) return GK_FORTUNE;
    if (Vector2Distance(p.pos, B.golf) < 1.8f) return GK_GOLF;
    if (RectDist(p.pos, {33, 13, 3, 3}) < 1.4f) return GK_POKER;
    if (RectDist(p.pos, {33, 17.5f, 3, 3}) < 1.4f) return GK_BULLSHIT;
    for (const auto& c : patrons) if (c.inside && !c.gone && c.name == "Pip" && c.playing < 0 && c.talkingTo < 0 && Vector2Distance(c.pos, p.pos) < 1.6f) return GK_PIP;
    return -1;
}
Vector2 Night::GameSpot(int kind, int machine) const {
    const BarData& B = D().bar;
    if (kind == GK_DARTS) return {THROW_LINE.x, THROW_LINE.y + 0.9f};
    if (kind == GK_POOL) { int m = 0; for (const auto& b : B.boxes) if (b.kind == "pool") { if (m++ == machine) return {b.r.x + b.r.width / 2, b.r.y + b.r.height + 0.6f}; } }
    if (kind == GK_GOLF) return {B.golf.x + 1.2f, B.golf.y};
    return B.golf;
}
static const std::vector<Opponent>& OppsOf(int kind) {
    static const std::vector<Opponent> none;
    return kind == GK_DARTS ? GD().darts : kind == GK_POOL ? GD().pool : kind == GK_GOLF ? GD().golf : none;
}
std::vector<int> Night::Challengers(const Player& p, int kind) const {
    std::vector<int> out;
    if (kind != GK_DARTS && kind != GK_POOL && kind != GK_GOLF) return out;
    auto free = [&](const Patron& c) { return c.inside && !c.gone && !c.leaving && c.talkingTo < 0 && c.playing < 0 && c.mood >= 20 && c.type != T_STAFF; };
    for (const auto& o : OppsOf(kind)) for (const auto& c : patrons) if (free(c) && c.name == o.name) out.push_back(c.id);
    std::string here = RoomAt(p.pos);
    for (const auto& c : patrons) {
        if ((int)out.size() >= 4) break;
        if (!free(c) || std::find(out.begin(), out.end(), c.id) != out.end()) continue;
        if (c.type != T_HUSTLER && c.type != T_GAMBLER && c.type != T_SAILOR && c.type != T_REGULAR) continue;
        if (Vector2Distance(c.pos, p.pos) < 9 || here == RoomAt(c.pos)) out.push_back(c.id);
    }
    return out;
}
static float TypeSkill(int type) { return type == T_HUSTLER ? 1.05f : type == T_GAMBLER ? 1.25f : type == T_SAILOR ? 1.35f : type == T_REGULAR ? 1.5f : 1.8f; }

bool Night::StartGame(Player& p, int kind, int machine, int opp, int stake, std::string* why) {
    auto no = [&](const char* s) { if (why) *why = s; return false; };
    if (p.st != State::Active || p.game.kind >= 0 || p.talk.patron >= 0) return false;
    if (kind < 0 || kind >= GK_COUNT) return false;
    bool match = kind == GK_DARTS || kind == GK_POOL || kind == GK_GOLF;
    if (kind == GK_GOLF && Hour() >= GD().golfCloses) return no("The yard's closed: the course shuts at 2 a.m.");
    if (kind == GK_POKER || kind == GK_BULLSHIT) {   // the card room (nightoff_cardroom.cpp)
        if (!SitAtCards(p, kind, machine, why)) return false;
        GameSeat c; c.kind = kind; c.machine = machine; c.caption = kind == GK_BULLSHIT ? "Twenty in the pot. Shed your cards, claim the rank, call the liars." : machine == 1 ? "One hand with the quiet man." : "Fifty in chips. The blinds go up every hour."; c.captionT = 4;
        p.game = c; return true;
    }
    GameSeat g; g.kind = kind; g.machine = machine; g.rng.s = rng ^ (0x9e37u * (p.id + 1)) ^ (uint32_t)(t * 1000); if (!g.rng.s) g.rng.s = 7;
    if (match) {
        if (opp == -2) {
            if (kind != GK_DARTS || bartenderDarts) return no("The bartender's had his game tonight.");
            if (p.tab <= 0) return no("\"You don't owe me anything to win back.\"");
            g.opp = -2; g.oppName = "the bartender"; g.stake = 0; bartenderDarts = true;
            for (const auto& o : GD().darts) if (o.once) { g.oppSkill = o.skill; g.tell = o.tell; }
        } else if (opp >= 0) {
            if (opp >= (int)patrons.size()) return false;
            Patron& c = patrons[opp];
            if (!c.inside || c.gone || c.playing >= 0 || c.talkingTo >= 0) return no("They're busy.");
            if (stake > p.money) return no("You haven't the money for that stake.");
            g.opp = opp; g.oppName = c.name; g.stake = std::max(0, stake); g.oppSkill = TypeSkill(c.type);
            for (const auto& o : OppsOf(kind)) if (o.name == c.name) { g.oppSkill = o.skill; g.tell = o.tell; g.hustler = o.hustler; if (o.stake > 0) g.stake = std::max(g.stake, std::min(o.stake, (int)p.money));
                // a hustler who sandbags misses the first game against you on purpose
                g.sandbag = o.sandbag && (p.id >= (int)c.mem.size() || c.mem[p.id].games == 0); }
            if (c.role == "a rival sailor") g.stake = std::max(g.stake, std::min(40, (int)p.money));   // (the rival crew plays for their wages)
            if (c.role == "the leader" && kind == GK_POOL) g.tell = "The biker leader racks up. \"Beat me and the jacket's yours.\"";
            c.playing = p.id; c.goal = GameSpot(kind, machine); c.path = NavPath(c.pos, c.goal); c.seatKind = "stand"; c.sitting = false; c.nextGoalT = 1e9f;
        } else g.opp = -1;
        if (kind == GK_DARTS) g.darts.Start(false, 0);
        if (kind == GK_POOL) g.pool.Rack(g.rng, 0);
        if (kind == GK_GOLF) { g.golf.solo = g.opp == -1; g.golf.Start(0); }
        g.caption = g.opp == -1 ? std::string("Practice: ") + GameName(kind) + ", alone." : g.opp == -2 ? "The bartender takes three darts from under the bar. \"For your tab.\"" : TextFormat("%s plays you for %d.", g.oppName.c_str(), g.stake);
        g.captionT = 4;
    }
    if (kind == GK_SLOTS) g.caption = "Two a pull. Three kidneys pays a kidney.";
    if (kind == GK_SCRATCH || kind == GK_PIP) g.caption = kind == GK_PIP ? "Pip opens his coat: \"Five. Luckier than the machine's.\"" : "The dispenser hums. Five a ticket.";
    if (kind == GK_FORTUNE) g.caption = "\"Ten, and sit. The cards don't lie, love; people do.\"";
    if (kind == GK_SCRATCH && scratchEaten) return no("The dispenser's empty: the goat ate them.");
    if (kind == GK_DANCE) { if (!EventOn("band")) return no("There's no band."); g.caption = "The band counts you in: hit the beats."; }
    if (!g.caption.empty() && g.captionT <= 0) g.captionT = 5;
    p.game = g;
    return true;
}
static void Settle(Night& n, Player& p, int result) {
    GameSeat& g = p.game; if (g.over) return;
    g.over = true; g.result = result;
    const char* what = GameName(g.kind);
    if (g.opp == -2) {
        if (result == 0) { p.gamesWon++; n.Flag("bartender", p.name); n.Note(p, 5, TextFormat("Beat the bartender at darts and won back a tab of %.0f.", p.tab)); p.tab = 0; g.caption = "The bartender tears up your tab. \"Don't tell anyone.\""; n.bar.mood = std::min(100.0f, n.bar.mood + 5); }
        else { p.tab += 10; g.caption = "\"Ten more on the tab, for the lesson.\""; }
    } else if (g.opp >= 0) {
        Patron& c = n.patrons[g.opp];
        if (g.opp < (int)n.patrons.size() && p.id < (int)c.mem.size()) c.mem[p.id].games += 1;
        n.SettleSideBets(p.id, result == 0);
        if (result == 0) {
            p.gamesWon++;
            p.money += g.stake;
            if (c.role == "the leader" && g.kind == GK_POOL && !p.jacket) { p.jacket = true; p.items.push_back("a biker's jacket"); n.Note(p, 5, "Beat the biker leader at pool and won his jacket."); }
            if (c.role == "a mourner" && g.kind == GK_DARTS) { p.money += 100; n.Note(p, 5, "Won the dead man's darts tournament, in his honour."); } g.caption = TextFormat("You win %d off %s.", g.stake, c.name.c_str());
            if (c.Has(D().Trait("bad loser"))) { c.mood = std::max(0.0f, c.mood - 20); g.caption += " They don't take it well."; }
            else if (c.Has(D().Trait("good loser"))) { c.mood = std::min(100.0f, c.mood + 5); g.caption += " They shake your hand."; }
            if (g.stake > 0) n.Note(p, 3, TextFormat("Won %d at %s off %s.", g.stake, what, c.name.c_str()));
            if (g.sandbag) g.caption += TextFormat(" %s grins: \"Beginner's luck. Double or nothing?\"", c.name.c_str());
        } else if (result == 1) {
            p.money -= g.stake; g.caption = TextFormat("%s takes your %d.", c.name.c_str(), g.stake);
            c.mood = std::min(100.0f, c.mood + 5);
            if (g.stake > 0) n.Note(p, 4, TextFormat("Lost %d at %s to %s%s.", g.stake, what, c.name.c_str(), g.hustler ? " (a hustler)" : ""));
        } else g.caption = "A draw: nobody pays.";
    } else g.caption = result == 0 ? "Done. Nobody saw." : "Done.";
    g.captionT = 6;
}
void Night::EndGame(Player& p) {
    GameSeat& g = p.game;
    if (g.kind < 0) return;
    if (g.opp >= 0 && g.opp < (int)patrons.size()) { Patron& c = patrons[g.opp]; if (c.playing == p.id) { c.playing = -1; c.nextGoalT = 0; } }
    p.game = GameSeat{};
}
void Night::GameAction(Player& p) {
    GameSeat& g = p.game; Input& in = p.in;
    if (g.kind < 0) return;
    if (CardAction(p)) return;   // (poker and bullshit)
    int act = in.gameAct;
    if (act == 3) {   // leave: walking out of a match you're losing is losing it
        bool live = (g.kind == GK_DARTS || g.kind == GK_POOL || g.kind == GK_GOLF) && !g.over && g.opp != -1;
        if (live) { Settle(*this, p, 1); if (g.opp >= 0 && g.opp < (int)patrons.size()) patrons[g.opp].mood = std::max(0.0f, patrons[g.opp].mood - 10); }
        EndGame(p); return;
    }
    if (act == 4) {   // again: the same opponent and stake (doubled, if the hustler asked)
        if (!g.over) return;
        int kind = g.kind, machine = g.machine, opp = g.opp, stake = g.sandbag && g.result == 0 ? g.stake * 2 : g.stake;
        if (opp == -2) return;
        EndGame(p);
        std::string why; if (!StartGame(p, kind, machine, opp, std::min(stake, (int)std::max(0.0f, p.money)), &why) && p.id == 0 && !why.empty()) Say(why);
        return;
    }
    const GamesData& d = GD();
    switch (g.kind) {
        case GK_DARTS:
            if (act == 1 && !g.over && g.darts.turn == 0 && g.botT <= 0) {
                int b0 = g.darts.big[0];
                if (in.cheat) { if (TryCheat(p, "darts", g.opp)) in.gameAim = Vector2Lerp(in.gameAim, g.darts.BotAim(), 0.7f); else { g.darts.turn = 1; g.darts.dart = 0; g.botT = 1.2f; break; } }   // (a weighted dart)
                g.darts.Throw(in.gameAim);
                if (g.darts.big[0] > b0) { Note(p, 5, "Threw a 180."); Flag("one_eighty", p.name); }
                if (g.darts.winner >= 0) Settle(*this, p, g.darts.winner == 0 ? 0 : 1);
                else if (g.darts.turn == 1) g.botT = 1.4f;
            }
            break;
        case GK_POOL:
            if (g.over || g.pool.turn != 0 || g.replayT > 0) break;
            if (act == 2 && g.pool.ballInHand) {
                Vector2 q = {std::clamp(in.gameAim.x, pool::BR, pool::W - pool::BR), std::clamp(in.gameAim.y, pool::BR, pool::H - pool::BR)};
                bool free = true; for (int i = 1; i < 16; i++) if (!g.pool.t.b[i].in && Vector2Distance(q, g.pool.t.b[i].p) < 2 * pool::BR) free = false;
                if (free) { g.pool.t.b[0].p = q; g.pool.t.b[0].in = false; g.pool.ballInHand = false; }
            }
            if (act == 1 && !g.pool.ballInHand) {
                bool miss = p.drunk > d.poolMissFrom && g.rng.U() < (p.drunk - d.poolMissFrom) / 150;
                if (in.cheat) { if (TryCheat(p, "pool", g.opp)) g.pool.t.b[0].p = pool::BotPlace(g.pool, g.rng); else { g.pool.turn = 1; g.pool.ballInHand = true; g.pool.last = "was caught moving the cue ball"; g.botT = 1.5f; break; } }   // (moving your ball)
                pool::Shot s; s.ang = in.gameAim.x; s.power = in.gamePower; s.english = in.gameEnglish;
                g.shotTable = g.pool.t; g.shotTable.frames.clear(); g.shot = s; g.shotMiss = miss; g.shotSerial++;
                g.pool.Play(s, miss);
                g.replayLen = (int)g.pool.t.frames.size(); g.replayT = g.replayLen / 60.0f;
                if (g.pool.turn == 1) g.botT = g.replayT + 1.2f;
            }
            break;
        case GK_GOLF:
            if (act == 1 && !g.over && g.golf.turn == 0 && g.replayT <= 0) {
                if (in.cheat) { const golf::Hole& hh = golf::Course()[g.golf.hole]; if (TryCheat(p, "mini golf", g.opp)) { Vector2 to = Vector2Subtract(hh.cup, g.golf.ball[0].p); float L = Vector2Length(to); if (L > 0.7f) g.golf.ball[0].p = Vector2Add(g.golf.ball[0].p, Vector2Scale(to, 0.6f / L)); } else { g.golf.strokes[0][g.golf.hole] += 2; } }   // (a nudge with the foot; caught: two strokes)
                g.golfPath.clear(); g.golfWho = 0; g.golfHole = g.golf.hole;
                g.golfPath.push_back(g.golf.ball[0].p);
                g.shotBall = g.golf.ball[0]; g.shotSim = g.golf.sim; g.shot.ang = in.gameAim.x; g.shot.power = in.gamePower; g.shotRain = g.golf.rain; g.shotSerial++;
                g.golf.Shoot(in.gameAim.x, in.gamePower, &g.golfPath);
                g.replayLen = (int)g.golfPath.size(); g.replayT = g.replayLen / 60.0f;
                if (g.golf.lastHoled && g.golf.lastStrokes == 1) { Note(p, 5, TextFormat("A hole in one on %s.", golf::Course()[g.golf.lastHole].name.c_str())); Flag("hole_in_one", golf::Course()[g.golf.lastHole].name); }
                if (g.golf.turn == 1) g.botT = g.replayT + 1.0f;
            }
            break;
        case GK_SLOTS:
            if (act == 1 && g.spinT <= 0) {
                if (p.money < d.slotCost) { g.caption = "You're out of coins."; g.captionT = 3; break; }
                p.money -= d.slotCost; g.pull = slots::Spin(g.machine, g.rng); g.pulls++;
                if (in.cheat && g.pull.pays == 0) { bool watched = Vector2Distance(p.pos, bar.pos) < 10; if (TryCheat(p, "the slot machine", watched ? -2 : -3)) { slots::Pull again = slots::Spin(g.machine, g.rng); if (again.pays > g.pull.pays) g.pull = again; } }   // (a nudge) g.spinT = std::max(0.35f, 1.1f - p.drunk / 140);   // (pulls get faster)
                p.money += g.pull.pays;
                if (g.pull.kidney) { p.kidneys = std::min(2, p.kidneys + 1); Note(p, 5, "Hit the kidney line on the slot machine."); Flag("slots_kidney", p.name); }
                if (g.pull.pays >= 200) Note(p, 3, TextFormat("Won %d on the slots.", g.pull.pays));
            }
            break;
        case GK_SCRATCH: case GK_PIP: {
            int cost = d.scratchCost;
            if (act == 5 && (!g.haveTicket || g.paid)) {
                if (p.money < cost) { g.caption = "You haven't five."; g.captionT = 3; break; }
                p.money -= cost; g.ticket = scratch::Buy(g.kind == GK_PIP, g.rng); g.haveTicket = true; g.paid = false;
            }
            if (act == 1 && g.haveTicket && !g.paid) {
                g.paid = true; p.money += g.ticket.prize;
                if (g.ticket.prize >= 500) Note(p, 5, "Scratched a 500.");
                if (g.ticket.map) { p.items.push_back("a map to the safe"); Note(p, 5, "Found a map to the safe on one of Pip's tickets."); }
            }
            break;
        }
        case GK_DANCE:
            if (act == 1 && !g.over) {   // the song's done: how many beats you hit (a guest's screen judges its own timing)
                float frac = std::clamp(in.gamePower, 0.0f, 1.0f); g.over = true; g.result = frac >= 0.6f ? 0 : 1;
                if (frac >= 0.6f) { p.charBuff = std::max(p.charBuff, 0.15f); p.charBuffT = std::max(p.charBuffT, 5 * SECONDS_PER_GAME_MINUTE); g.caption = TextFormat("%.0f%% of the beats: the floor loves you (+15%% charisma).", frac * 100); }
                else g.caption = TextFormat("%.0f%% of the beats.", frac * 100);
                if (p.drunk >= 60) { p.charBuff = std::max(p.charBuff, 0.05f); p.charBuffT = std::max(p.charBuffT, 5 * SECONDS_PER_GAME_MINUTE); g.caption += " You dance terribly, and they love you for it."; }
                g.captionT = 5; Note(p, 0, "Danced to the band.");
            }
            break;
        case GK_FORTUNE:
            if (act == 1) {
                if (p.money < d.fortuneCost) { g.caption = "\"Ten, love. The cards don't read for free.\""; g.captionT = 3; break; }
                p.money -= d.fortuneCost; g.reading = fortune::Read(*this, p, g.rng); g.haveReading = true;
                if (!p.fortuneAsked && Hour() >= 22 && g.rng.U() < 0.25f) { p.fortuneAsked = true; g.caption = "She gathers the cards and doesn't look up: \"Walk me home, sailor?\""; g.captionT = 8; }
                Note(p, 6, "Had a fortune read in the snug.");
            }
            break;
    }
}
void Night::StepGames(float dt) {
    for (auto& p : players) {
        GameSeat& g = p.game;
        if (g.kind < 0) continue;
        if (p.st != State::Active && p.st != State::Drinking) { EndGame(p); continue; }
        g.captionT = std::max(0.0f, g.captionT - dt); g.botT = std::max(0.0f, g.botT - dt); g.replayT = std::max(0.0f, g.replayT - dt); g.spinT = std::max(0.0f, g.spinT - dt);
        // a wrecked player pulls until broke (doc p. 17)
        if (g.kind == GK_SLOTS && p.drunk >= 80 && p.money >= GD().slotCost && g.spinT <= 0 && (g.autoT -= dt) <= 0) { p.in.gameAct = 1; GameAction(p); p.in.gameAct = 0; g.autoT = 0.25f; }
        Patron* c = g.opp >= 0 && g.opp < (int)patrons.size() ? &patrons[g.opp] : nullptr;
        if (c && (c->gone || !c->inside)) { if (!g.over) { g.caption = c->name + " has gone home. The game's off."; g.captionT = 5; g.over = true; g.result = 2; } c->playing = -1; continue; }
        if (g.over) continue;
        float skill = g.oppSkill * AimMul(c ? c->drunk : 0) * (g.sandbag ? 2.6f : 1);
        if (g.kind == GK_DARTS) {
            if (g.opp == -1 && g.darts.turn == 1) { g.darts.turn = 0; g.darts.dart = 0; }
            if (g.darts.turn == 1 && g.botT <= 0) {
                if (g.darts.dart == 0 && g.hustler && !g.tellShown && g.rng.U() < 0.4f) { g.caption = g.oppName + ": " + g.tell; g.captionT = 4; g.tellShown = true; }
                Vector2 a = g.darts.BotAim(); float sig = GD().dartSigma * skill;
                g.darts.Throw({a.x + g.rng.N() * sig, a.y + g.rng.N() * sig});
                g.botT = g.darts.turn == 1 ? 0.9f : 0.6f;
                if (g.darts.winner >= 0) Settle(*this, p, g.darts.winner == 0 ? 0 : 1);
            }
        }
        if (g.kind == GK_POOL && g.replayT <= 0) {
            if (g.pool.winner >= 0) { Settle(*this, p, g.pool.winner == 0 ? 0 : 1); continue; }
            if (g.opp == -1 && g.pool.turn == 1) g.pool.turn = 0;
            if (g.pool.turn == 1 && g.botT <= 0) {
                if (g.pool.ballInHand) { g.pool.t.b[0].p = pool::BotPlace(g.pool, g.rng); g.pool.t.b[0].in = false; g.pool.ballInHand = false; }
                if (g.hustler && !g.tellShown && g.rng.U() < 0.3f) { g.caption = g.oppName + ": " + g.tell; g.captionT = 4; g.tellShown = true; }
                pool::Shot s = pool::BotShot(g.pool, skill, g.rng);
                g.shotTable = g.pool.t; g.shotTable.frames.clear(); g.shot = s; g.shotMiss = false; g.shotSerial++;
                g.pool.Play(s);
                g.replayLen = (int)g.pool.t.frames.size(); g.replayT = g.replayLen / 60.0f;
                g.botT = g.replayT + 1.0f;
            }
        }
        if (g.kind == GK_GOLF && g.replayT <= 0) {
            g.golf.sim.t += dt;   // (the windmill turns while you line up)
            if (g.golf.winner >= 0) { Settle(*this, p, g.golf.winner == 0 ? 0 : g.golf.winner == 1 ? 1 : 2); continue; }
            if (g.golf.turn == 1 && g.botT <= 0) {
                float a, pw; golf::BotShot(g.golf, skill, g.rng, a, pw);
                g.golfPath.clear(); g.golfWho = 1; g.golfHole = g.golf.hole; g.golfPath.push_back(g.golf.ball[1].p);
                g.shotBall = g.golf.ball[1]; g.shotSim = g.golf.sim; g.shot.ang = a; g.shot.power = pw; g.shotRain = g.golf.rain; g.shotSerial++;
                g.golf.Shoot(a, pw, &g.golfPath);
                g.replayLen = (int)g.golfPath.size(); g.replayT = g.replayLen / 60.0f;
                g.botT = g.replayT + 1.0f;
            }
        }
    }
}

// ---------------------------------------------------------------- stage 3's checks (part of --night-test)
int NightGamesChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    Night n; Opts o; o.players = 1; o.seed = 33; o.crowd = 1; o.events = false; n.Init(o);
    for (int i = 0; i < (int)(3 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);   // (10 p.m.)
    Player& p = n.players[0]; p.st = State::Active; p.drunk = 0; p.money = 200;
    // darts against whoever's in the games room: play it out through Input, as the screen would
    p.pos = {2.6f, 7.5f};
    check(n.NearGame(p) == GK_DARTS, "the throw line is a darts station");
    auto ch = n.Challengers(p, GK_DARTS);
    check(!ch.empty(), TextFormat("someone will play darts (%d)", (int)ch.size()));
    int opp = ch.empty() ? -1 : ch[0];
    float m0 = p.money;
    p.in.startGame = GK_DARTS; p.in.gameOpp = opp; p.in.gameStake = 20; n.Step(0.05f);
    check(p.game.kind == GK_DARTS && (opp < 0 || n.patrons[opp].playing == 0), "a darts match starts and the opponent comes over");
    GRng r; r.s = 99;
    for (int k = 0; k < 20000 && !p.game.over; k++) {
        if (p.game.darts.turn == 0 && p.game.botT <= 0) { Vector2 a = p.game.darts.BotAim(); p.in.gameAim = {a.x + r.N() * 26, a.y + r.N() * 26}; p.in.gameAct = 1; }
        n.Step(0.05f);
    }
    check(p.game.over && (p.game.result == 0 || p.game.result == 1), TextFormat("the match ends (%s)", p.game.result == 0 ? "you won" : "you lost"));
    check(opp < 0 || fabsf(p.money - m0) == 20, TextFormat("the stake changes hands (%.0f -> %.0f)", m0, p.money));
    p.in.gameAct = 3; n.Step(0.05f);
    check(p.game.kind < 0 && (opp < 0 || n.patrons[opp].playing < 0), "leaving the table frees the opponent");
    // pool: practice, a few shots through Input
    p.pos = {3.3f, 5.0f}; int mach = -1;
    check(n.NearGame(p, &mach) == GK_POOL, "beside a pool table");
    p.in.startGame = GK_POOL; p.in.gameMachine = mach; p.in.gameOpp = -1; n.Step(0.05f);
    int shots = 0;
    for (int k = 0; k < 4000 && shots < 6 && !p.game.over; k++) {
        GameSeat& g = p.game;
        if (g.replayT <= 0 && g.pool.turn == 0) {
            if (g.pool.ballInHand) { p.in.gameAim = {0.5f, 0.56f}; p.in.gameAct = 2; }
            else { pool::Shot s = pool::BotShot(g.pool, 1, r); p.in.gameAim = {s.ang, 0}; p.in.gamePower = s.power; p.in.gameEnglish = 0; p.in.gameAct = 1; shots++; }
        }
        n.Step(0.05f);
    }
    int down = 0; for (int i = 1; i < 16; i++) down += p.game.pool.t.b[i].in;
    check(shots >= 6 || p.game.over, TextFormat("six practice shots (%d balls down)", down));
    p.in.gameAct = 3; n.Step(0.05f);
    // golf, alone, all nine holes
    p.pos = D().bar.golf;
    check(n.NearGame(p) == GK_GOLF, "the yard's first tee is a golf station");
    p.in.startGame = GK_GOLF; p.in.gameOpp = -1; n.Step(0.05f);
    for (int k = 0; k < 60000 && !p.game.over; k++) {
        GameSeat& g = p.game;
        if (g.replayT <= 0 && g.golf.turn == 0 && g.golf.winner < 0) { float a, pw; golf::BotShot(g.golf, 1, r, a, pw); p.in.gameAim = {a, 0}; p.in.gamePower = pw; p.in.gameAct = 1; }
        n.Step(0.05f);
    }
    check(p.game.over, TextFormat("nine holes played alone in %d strokes", p.game.golf.Total(0)));
    p.in.gameAct = 3; n.Step(0.05f);
    // the slots: coins go in; the honest wheel pays; three kidneys pays a kidney
    p.pos = {1.5f, 15.9f};
    check(n.NearGame(p, &mach) == GK_SLOTS, "in front of a slot machine");
    p.in.startGame = GK_SLOTS; p.in.gameMachine = mach; n.Step(0.05f);
    float before = p.money; p.in.gameAct = 1; n.Step(0.05f);
    check(p.game.pulls == 1 && p.money == before - GD().slotCost + p.game.pull.pays, "a pull costs two and pays what it shows");
    { GRng q; q.s = 5; int lines = 0; for (int k = 0; k < 300000; k++) { auto pl = slots::Spin(0, q); lines += pl.kidney; } check(lines > 0, TextFormat("the kidney line comes up (%d in 300,000 pulls)", lines)); }
    p.in.gameAct = 3; n.Step(0.05f);
    // a wrecked player pulls until broke
    p.money = 20; int pulled = 0;
    for (int k = 0; k < 400; k++) {
        p.drunk = 85; if (p.st != State::Active) { p.st = State::Active; p.vomitT = 0; }
        if (p.game.kind < 0) { p.in.startGame = GK_SLOTS; p.in.gameMachine = mach; }
        int before = p.game.pulls; n.Step(0.05f); if (p.game.kind == GK_SLOTS) pulled += std::max(0, p.game.pulls - before);
    }
    check(p.money < GD().slotCost || pulled >= 10, TextFormat("wrecked at the slots, you pull until broke (%d pulls, %.0f left)", pulled, p.money));
    p.in.gameAct = 3; n.Step(0.05f); p.drunk = 0; p.money = 200;
    // a scratch-off: bought, scratched, paid
    p.pos = D().bar.scratch;
    check(n.NearGame(p) == GK_SCRATCH, "the scratch-off dispenser");
    p.in.startGame = GK_SCRATCH; n.Step(0.05f); p.in.gameAct = 5; n.Step(0.05f);
    before = p.money; p.in.gameAct = 1; n.Step(0.05f);
    check(p.game.haveTicket && p.game.paid && p.money == before + p.game.ticket.prize, TextFormat("a ticket pays what it shows (%d)", p.game.ticket.prize));
    p.in.gameAct = 3; n.Step(0.05f);
    // the fortune teller tells the truth: name a thief who's really in the room
    for (auto& c : n.patrons) if (c.thief && !c.gone) { c.inside = true; c.pos = {30, 6}; }
    p.pos = D().bar.fortune;
    check(n.NearGame(p) == GK_FORTUNE, "the fortune teller's table");
    bool named = false, allTrue = true;
    for (int k = 0; k < 12; k++) {
        p.money = 200; p.in.startGame = GK_FORTUNE; n.Step(0.05f); p.in.gameAct = 1; n.Step(0.05f);
        for (int c = 0; c < 3; c++) {
            const std::string& s = p.game.reading.text[c];
            if (s.find("cooler sits in this room") != std::string::npos) {
                bool ok = false; for (const auto& q : n.patrons) if (q.thief && q.inside && s.find(q.name) != std::string::npos) ok = true;
                named |= ok; allTrue &= ok;
            }
        }
        p.in.gameAct = 3; n.Step(0.05f);
    }
    check(named && allTrue, "a smile with a cooler names a real kidney thief in the room");
    // the bartender plays one game a night, for your tab
    p.pos = {2.6f, 7.5f}; p.tab = 40; n.bartenderDarts = false;
    p.in.startGame = GK_DARTS; p.in.gameOpp = -2; n.Step(0.05f);
    check(p.game.kind == GK_DARTS && p.game.opp == -2, "the bartender takes you on for your tab");
    p.in.gameAct = 3; n.Step(0.05f);
    p.in.startGame = GK_DARTS; p.in.gameOpp = -2; n.Step(0.05f);
    check(p.game.kind < 0, "and only once a night");
    return fails;
}

} // namespace no
