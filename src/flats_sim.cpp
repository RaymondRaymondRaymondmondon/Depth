// Headless play of the Flats engine: whole runs with a sensible auto-player, to check the turn logic never stalls
// and to see how the dealers balance.  Run with:  depth.exe --flats-sim 1000
#include "flats_run.h"
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace flats;

namespace {
struct BattleResult { bool won; int turns; int gold; int found; };

BattleResult PlayBattle(RunManager& rm, Rng& rng, long* steps) {
    Boon boon = Boon::NONE;
    if (rm.gs.momentumTracker > 0 && rm.SpendMomentum()) boon = Boon::EXTRA_DRAW;
    Battle b;
    b.Start(rm.MakeBattle(boon), rng);
    Events ev;
    int guard = 0;
    bool trace = getenv("DEPTH_TRACE") != nullptr;
    int lastTurn = 0;
    while (b.turn != Turn::OVER && guard++ < 4000) {
        ev.clear();
        (*steps)++;
        if (trace && b.turn == Turn::YOU_DRAW && b.turnNo != lastTurn) {
            lastTurn = b.turnNo;
            printf("--- turn %d  scale %d  bones %d  hand:", b.turnNo, b.board.scale, b.board.bones[0]);
            for (auto& h : b.hand) printf(" %s(%d/%d w%d %s)", h.name.c_str(), h.strength, h.defense, h.weight, h.CostText().c_str());
            printf("\n");
            for (int r = 0; r < ROWS; r++) { printf("  row %d:", r); for (int k = 0; k < COLS; k++) { auto& x = b.board.cell[r][k]; if (x.used) printf(" [%s %d/%d]", x.card.name.c_str(), x.card.strength, x.card.hp); else printf(" [        ]"); } printf("\n"); }
        }
        if (b.Waiting()) b.AutoYourTurn(ev, rng);
        else b.Advance(ev, rng);
    }
    return {b.winner > 0, b.turnNo, b.board.gold, b.board.itemsFound[0]};
}

int Score(NodeType t) { // the auto-player's taste in nodes
    switch (t) {
        case NodeType::CARD_PICK: return 9; case NodeType::CAMPFIRE: return 8; case NodeType::SPLICE: return 6; case NodeType::CACHE: return 5;
        case NodeType::BATTLE: return 4; case NodeType::STALL: return 3; case NodeType::TRIAL: return 3; case NodeType::SACRIFICE: return 2;
        case NodeType::ELITE: return 1; default: return 0;
    }
}
}  // namespace

void FlatsSim(int runs, bool sensible) {
    (void)sensible;
    int battles[DEALERS + 1] = {0}, wins[DEALERS + 1] = {0}, turnsSum[DEALERS + 1] = {0}, layerReached[MAP_LAYERS + 1] = {0}, cleared = 0, stalls = 0;
    long steps = 0;
    int boons = 0;
    for (int r = 0; r < runs; r++) {
        RunManager rm;
        rm.NewRun((unsigned)(r * 7919 + 1));
        Rng rng;
        rng.Seed((unsigned)(r * 104729 + 7));
        bool dead = false;
        while (!dead && !rm.Finished()) {
            auto opts = rm.Reachable();
            if (opts.empty()) break;
            int best = opts[0], bs = -1;
            int nextLayer = rm.layer + 1;
            for (int o : opts) { int s = Score(rm.map[nextLayer][o].type) * 10 + rng.I(0, 5); if (s > bs) { bs = s; best = o; } }
            rm.Enter(best);
            MapNode& n = rm.map[rm.layer][rm.slot];
            switch (n.type) {
                case NodeType::BATTLE: case NodeType::ELITE: case NodeType::BOSS: {
                    int before = rm.gs.momentumTracker;
                    BattleResult res = PlayBattle(rm, rng, &steps);
                    if (before > 0 && rm.gs.momentumTracker < before) boons++;
                    int d = n.type == NodeType::BOSS ? DEALERS - 1 : n.dealer;
                    battles[d]++; turnsSum[d] += res.turns;
                    if (res.won) {
                        wins[d]++;
                        rm.OnBattleFinished(true, res.turns, res.gold, res.found);
                        if (n.type == NodeType::BOSS) cleared++;
                    } else { rm.OnBattleFinished(false, res.turns, 0, res.found); dead = true; }
                    break;
                }
                case NodeType::CARD_PICK: {
                    auto o = rm.OfferCards(3, false);
                    rm.TakeCard(*std::max_element(o.begin(), o.end(), [](const Card& a, const Card& b) { return a.Rating() < b.Rating(); }));
                    break;
                }
                case NodeType::CAMPFIRE: {
                    int bi = 0;
                    for (int i = 0; i < (int)rm.gs.deck.size(); i++) if (rm.gs.deck[i].Rating() > rm.gs.deck[bi].Rating()) bi = i;
                    rm.Campfire(bi, rm.rng.C(0.5f));
                    break;
                }
                case NodeType::SPLICE: {
                    int keep = 0, eat = -1;
                    for (int i = 0; i < (int)rm.gs.deck.size(); i++) if (rm.gs.deck[i].Rating() > rm.gs.deck[keep].Rating()) keep = i;
                    for (int i = 0; i < (int)rm.gs.deck.size(); i++) if (i != keep && !rm.gs.deck[i].sigils.empty() && (eat < 0 || rm.gs.deck[i].Rating() < rm.gs.deck[eat].Rating())) eat = i;
                    if (eat >= 0) rm.Splice(keep, eat);
                    break;
                }
                case NodeType::SACRIFICE: {
                    int wi = 0;
                    for (int i = 0; i < (int)rm.gs.deck.size(); i++) if (rm.gs.deck[i].Rating() < rm.gs.deck[wi].Rating()) wi = i;
                    if (rm.gs.deck.size() > 10) rm.SacrificeCard(wi);
                    break;
                }
                case NodeType::TRIAL: {
                    int bi = -1;
                    for (int i = 0; i < (int)rm.gs.deck.size(); i++) if (rm.gs.deck[i].defense >= 4 && (bi < 0 || rm.gs.deck[i].strength > rm.gs.deck[bi].strength)) bi = i;
                    if (bi >= 0) rm.Trial(bi);
                    break;
                }
                case NodeType::CACHE: rm.gs.AddItem(rm.RandomItem()); break;
                default: break;
            }
        }
        if (!dead && !rm.Finished()) stalls++;
        layerReached[std::max(0, rm.layer)]++;
    }
    printf("Flats engine: %d runs, a sensible auto-player\n", runs);
    for (int d = 0; d < DEALERS; d++)
        if (battles[d]) printf("  %-17s %6d battles, %5.1f%% won, %.1f turns on average\n", Dealer(d).name, battles[d], 100.0 * wins[d] / battles[d], (double)turnsSum[d] / battles[d]);
    printf("  runs that cleared the House: %.1f%%   (momentum boons spent: %d)\n", 100.0 * cleared / runs, boons);
    printf("  layer where runs ended:");
    for (int l = 0; l < MAP_LAYERS; l++) printf(" %d:%d", l, layerReached[l]);
    printf("\n  stalled runs: %d, average %.0f steps per run\n", stalls, (double)steps / runs);
}
