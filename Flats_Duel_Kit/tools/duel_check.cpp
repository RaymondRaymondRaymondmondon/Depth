// The kit's own checker: builds the duel engine against copies of Depth's headless Flats engine (vendor/) and runs
//   sim N [mode]       bot vs bot: first-player win rate, game length, ties, sudden deaths   (--flats-duel-sim)
//   matrix N           Quick mode: every preset against every other
//   leak N             hidden information never changes a player's or a spectator's snapshot (the packet-log test)
//   views N            both players decode every snapshot, exactly, and their boards mirror
//   wire N             one seat plays through the GameHost's byte interface (WriteAction -> Act -> Snapshot -> ReadView)
//   sweep N            first-player win rate for a grid of second-player bonuses (for tuning)
//   (no arguments: everything, at modest sizes)
#include "flats_duel.h"
#include "arcade_game.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace arcade { namespace {
#include "flats_duel_host.inc"
} }

using namespace flats;
using namespace flats::duel;

static const char* MODE_NAME[MODE_COUNT] = {"draft", "constructed", "quick"};

static int RunSim(int n, int mode) {
    auto t0 = std::chrono::steady_clock::now();
    SimReport R = Simulate(n, mode, 12345);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int decided = R.games - R.ties;
    printf("%-11s %5d matches, %5d games: first player wins %.1f%% of decided games; ties %.2f%%; sudden death %.1f%%; "
           "past the round limit %.1f%%; %.1f rounds and %.0f steps a game  (%.1f s)\n",
           MODE_NAME[mode], R.matches, R.games, decided ? 100.0 * R.firstWins / decided : 0, R.games ? 100.0 * R.ties / R.games : 0,
           R.games ? 100.0 * R.suddenDeaths / R.games : 0, R.games ? 100.0 * R.roundLimitGames / R.games : 0, R.avgRounds, R.avgSteps, secs);
    double fw = decided ? 100.0 * R.firstWins / decided : 50;
    return (fw >= 45 && fw <= 55) ? 0 : 1;
}

static int RunMatrix(int n) {
    SimReport R = Simulate(n, MODE_QUICK, 999);
    printf("Quick mode, row deck's win rate against column deck (games):\n%-11s", "");
    for (int b = 0; b < 6; b++) printf("%11s", Presets()[b].name);
    printf("%11s\n", "overall");
    int worst = 0;
    for (int a = 0; a < 6; a++) {
        printf("%-11s", Presets()[a].name);
        int w = 0, g = 0;
        for (int b = 0; b < 6; b++) {
            if (a == b) { printf("%11s", "-"); continue; }
            int gg = R.presetGames[a][b];
            printf("%9.0f%% ", gg ? 100.0 * R.presetWins[a][b] / gg : 0.0);
            w += R.presetWins[a][b]; g += gg;
        }
        double ov = g ? 100.0 * w / g : 0;
        printf("%9.0f%%\n", ov);
        if (ov < 40 || ov > 60) worst = 1;   // every preset within 40-60% overall
    }
    return worst;
}

// Which cards win games: for every card, how often the side that played it won (Quick and Draft bot games).
static void RunCards(int n, int mode) {
    const int NC = (int)Catalog().size();
    std::vector<int> played(NC, 0), won(NC, 0);
    for (int m = 0; m < n; m++) {
        State s; NewMatch(s, 31337 + 17 * m); s.mode = mode;
        std::vector<bool> used[2] = {std::vector<bool>(NC, false), std::vector<bool>(NC, false)};
        int lastPhase = -1;
        for (int g = 0; g < 200000 && !Over(s); g++) {
            if (!HostTick(s, 1.0f, 3u)) continue;
            if (s.phase == PH_PLAY || s.phase == PH_GAME_OVER)
                for (const Event& e : s.ev)
                    if (e.type == Event::Play && e.r1 >= R_YOU_FRONT && s.bt.board.cell[e.r1][e.c1].used) {
                        int owner = s.evFrame, id = s.bt.board.cell[e.r1][e.c1].card.id;
                        if (s.active == s.evFrame) used[owner][id] = true;
                    }
            if (s.phase == PH_GAME_OVER && lastPhase == PH_PLAY) {
                for (int p = 0; p < 2; p++) for (int id = 0; id < NC; id++) if (used[p][id]) { played[id]++; if (s.gameWinner == p) won[id]++; }
                used[0].assign(NC, false); used[1].assign(NC, false);
            }
            lastPhase = s.phase;
        }
    }
    std::vector<int> order;
    for (int id = 0; id < NC; id++) if (played[id] >= 40) order.push_back(id);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return won[a] * (double)played[b] > won[b] * (double)played[a]; });
    printf("%s: win rate of the side that played each card (games it was played in)\n", MODE_NAME[mode]);
    for (int id : order) printf("  %-18s %5.1f%%  (%d)\n", Catalog()[id].name.c_str(), 100.0 * won[id] / played[id], played[id]);
}

static int RunPresetCheck() {
    int bad = 0;
    for (int i = 0; i < (int)Presets().size(); i++) {
        const Preset& p = Presets()[i];
        int n = 0;
        for (auto& pc : p.cards) {
            n += pc.count;
            if (CardIdByName(pc.name) == 0) { printf("preset %s: unknown card \"%s\"\n", p.name, pc.name); bad++; }
        }
        std::string why;
        std::vector<Card> d = PresetDeck(i);
        if (n != 20) { printf("preset %s has %d cards, not 20\n", p.name, n); bad++; }
        if (!ValidateDeck(d, {}, &why)) { printf("preset %s is not a legal deck: %s\n", p.name, why.c_str()); bad++; }
    }
    printf("presets: %s\n", bad ? "FAIL" : "PASS: six legal 20-card decks");
    return bad ? 1 : 0;
}

static int RunLeak(int n) {
    std::string why;
    bool ok = VerifyNoLeak(n, 4242, &why);
    printf("leak:  %s: %s\n", ok ? "PASS" : "FAIL", why.c_str());
    return ok ? 0 : 1;
}
static int RunViews(int n) {
    std::string why;
    bool ok = VerifyViews(n, 777, &why);
    printf("views: %s: %s\n", ok ? "PASS" : "FAIL", why.c_str());
    return ok ? 0 : 1;
}

// Player 0 is an AI seat on the host; player 1 is "remote": it only ever sees its snapshot bytes, decides from a
// state it rebuilds for the bot... except the bot needs a State, so it asks the host's copy (as a stand-in for a human),
// but every action goes over the byte interface and every snapshot is decoded as a client would.
static int RunWire(int n) {
    int bad = 0, acts = 0, snaps = 0, matches = 0;
    for (int m = 0; m < n; m++) {
        arcade::FlatsDuelHost h;
        h.Start(2, 900 + m);
        h.s.mode = m % MODE_COUNT;
        for (int guard = 0; guard < 200000 && !h.Over(); guard++) {
            h.Tick(1.0f, 1u);   // player 0 is AI; player 1 must act through bytes
            int actor = Actor(h.s);
            if (actor == 1 || actor == 2) {
                Action a = Bot(h.s, 1);
                if (a.kind != Action::COUNT && Legal(h.s, 1, a)) {
                    Writer w; WriteAction(a, w);
                    Reader r(w.b);
                    if (!h.Act(1, r)) bad++;
                    acts++;
                }
            }
            Writer w; h.Snapshot(1, w);
            Reader r(w.b); View v;
            if (!ReadView(r, v) || !r.Done() || v.me != 1) bad++;
            snaps++;
        }
        matches += h.Over();
    }
    printf("wire:  %s: %d matches finished of %d, %d actions and %d snapshots over bytes, %d failures\n",
           bad == 0 && matches == n ? "PASS" : "FAIL", matches, n, acts, snaps, bad);
    return bad == 0 && matches == n ? 0 : 1;
}

static void RunSweep(int n) {
    printf("first-player win rate by the second player's bonus (quick = presets, draft = bot drafts):\n");
    for (int skip = 0; skip <= 1; skip++)
        for (int hold = 0; hold <= 1; hold++)
            for (int bones = 0; bones <= 2; bones++)
                for (int minnows = 0; minnows <= 1; minnows++)
                    for (int tip = 0; tip <= 2; tip++) {
                        Tuning keep = Tune();
                        Tune().firstSkipsDraw = skip != 0; Tune().firstHoldsFire = hold != 0; Tune().secondBones = bones;
                        Tune().secondMinnows = minnows; for (int& k : Tune().secondScaleStart) k = tip;
                        SimReport R = Simulate(n, MODE_QUICK, 55);
                        SimReport D = Simulate(n / 2 + 1, MODE_DRAFT, 56);
                        int dq = R.games - R.ties, dd = D.games - D.ties;
                        double q = dq ? 100.0 * R.firstWins / dq : 0, d = dd ? 100.0 * D.firstWins / dd : 0;
                        printf("  skip draw %-3s hold fire %-3s | second +%d bones +%d minnow, scales %+d   quick %5.1f%%   draft %5.1f%%%s\n",
                               skip ? "yes" : "no", hold ? "yes" : "no", bones, minnows, -tip, q, d,
                               (q >= 48 && q <= 52 && d >= 48 && d <= 52) ? "   <- in the 48-52 band" : "");
                        Tune() = keep;
                    }
}

int main(int argc, char** argv) {
    std::string cmd = argc >= 2 ? argv[1] : "all";
    int n = argc >= 3 ? std::max(1, atoi(argv[2])) : 0;
    if (cmd == "sim") {
        if (argc >= 4) return RunSim(n ? n : 1000, atoi(argv[3]));
        int f = 0; for (int m = 0; m < MODE_COUNT; m++) f |= RunSim(n ? n : 1000, m); return f;
    }
    if (cmd == "matrix") return RunMatrix(n ? n : 3000);
    if (cmd == "leak") return RunLeak(n ? n : 12);
    if (cmd == "views") return RunViews(n ? n : 30);
    if (cmd == "wire") return RunWire(n ? n : 30);
    if (cmd == "sweep") { RunSweep(n ? n : 1500); return 0; }
    if (cmd == "tune" && argc >= 8) {   // tune N bones minnows skipDraw holdFire scaleNotches: first-player rate in every mode
        Tune().secondBones = atoi(argv[3]); Tune().secondMinnows = atoi(argv[4]); Tune().firstSkipsDraw = atoi(argv[5]) != 0;
        Tune().firstHoldsFire = atoi(argv[6]) != 0; for (int& k : Tune().secondScaleStart) k = atoi(argv[7]);
        printf("second +%d bones +%d minnows, first skips draw %d, holds fire %d, scales %+d:\n", Tune().secondBones, Tune().secondMinnows,
               (int)Tune().firstSkipsDraw, (int)Tune().firstHoldsFire, -Tune().secondScaleStart[0]);
        int f = 0; for (int m = 0; m < MODE_COUNT; m++) f |= RunSim(n ? n : 3000, m); return f;
    }
    if (cmd == "cards") { RunCards(n ? n : 2000, argc >= 4 ? atoi(argv[3]) : MODE_QUICK); return 0; }
    int f = RunPresetCheck();
    for (int m = 0; m < MODE_COUNT; m++) f |= RunSim(600, m);
    f |= RunMatrix(1500);
    f |= RunLeak(9);
    f |= RunViews(24);
    f |= RunWire(12);
    printf(f ? "SOME CHECKS FAILED\n" : "All duel checks pass.\n");
    return f;
}
