// Fowl Play: the room behind the porch (design doc pp. 10-13). Every purchase is a Command, applied by the host in
// World::Command; the gun counter, the mystery-gun machine, the slots, the scratch-offs, the Slop Shop with its
// sabotage and counters. Sabotage is queued on the target and switched on at the start of their next hunt.
#include "fowl.h"
#include <algorithm>
#include <cmath>

namespace fp {

bool World::CanBuy(const Player& p, int station) const {
    if (phase != PH_INTER) return false;
    if (station < 0) return true;
    return NearStation(p) == station || p.bot && NearStation(p) == station;
}
int World::SabPrice(int item, int target) const {
    int base = D().sabotage[item].price;
    return target == Leader() ? (int)roundf(base * (1 + D().leaderMarkup)) : base;
}
// a gun into your hands: the free hand first; else the one you're holding goes to the hook (the Zapper never leaves)
void World::GiveGun(Player& p, const Gun& g) {
    int zap = GunIndex("zapper");
    if (!p.guns[1].Has()) { p.guns[1] = g; p.hand = 1; return; }
    int slot = p.guns[p.hand].def == zap ? 1 - p.hand : p.hand;
    if (p.guns[slot].def == zap) slot = 1 - slot;
    Gun old = p.guns[slot]; p.guns[slot] = g; p.hand = slot;
    if (!p.hook.Has()) p.hook = old;
    else { FloorGun f; f.g = old; f.p = {p.pos.x + (Rand() - 0.5f), 0, p.pos.z - 0.6f}; floor.push_back(f); }
}

// ---------------------------------------------------------------- gambling
static float R01(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFFFF) / 16777216.0f; }
void SlotSpin(uint32_t& rng, int out[3]) {
    const auto& W = D().slotWeights; int tot = 0; for (int w : W) tot += w;
    for (int k = 0; k < 3; k++) { float x = R01(rng) * tot; int s = 0; for (; s < (int)W.size() - 1; s++) { x -= W[s]; if (x < 0) break; } out[k] = s; }
}
// two of a kind returns the bet; three ducks, dogs or guns pay a gun (a $10 pull: cash instead); three bells and three
// Zappa logos pay big (and the logo's fanfare is heard on the porch)
int SlotPayout(const int r[3], int bet, int* gunOut, uint32_t& rng) {
    if (gunOut) *gunOut = -1;
    if (r[0] == r[1] && r[1] == r[2]) {
        if (r[0] == 3) return bet * D().threeBell;
        if (r[0] == 4) return bet * D().threeZappa;
        if (bet < 25) return (int)(bet * 6.5f);
        std::vector<int> pool; for (int i = 0; i < (int)D().guns.size(); i++) if (D().guns[i].price > 0 && D().guns[i].price <= bet * (bet >= 50 ? 10 : 8)) pool.push_back(i);
        if (pool.empty()) return bet * 6;
        if (gunOut) *gunOut = pool[std::min((int)pool.size() - 1, (int)(R01(rng) * pool.size()))];
        return 0;
    }
    if (r[0] == r[1] || r[1] == r[2] || r[0] == r[2]) return bet;
    return 0;
}
float SlotEV(int bet, int pulls, uint32_t seed) {
    uint32_t rng = seed; double back = 0;
    for (int i = 0; i < pulls; i++) { int r[3]; SlotSpin(rng, r); int gun; int cash = SlotPayout(r, bet, &gun, rng); back += cash + (gun >= 0 ? D().guns[gun].price : 0); }
    return (float)(back / ((double)bet * pulls));
}
static void ScratchResolve(World& w, Player& p, int type) {
    const Scratch& S = D().scratch[type];
    if (w.Rand() * S.odds >= 1) { p.gambleLost += S.price; w.Emit(EV_SCRATCH, p.pos, p.id, -1, 0); return; }
    float x = w.Rand(); std::string prize = S.prizes.empty() ? "0" : S.prizes.back().first;
    for (const auto& pz : S.prizes) { x -= pz.second; if (x < 0) { prize = pz.first; break; } }
    int cash = 0;
    if (prize == "gun" || prize == "gun+200") { int g; do g = w.RandI((int)D().guns.size()); while (D().guns[g].price <= 0); w.GiveGun(p, MakeGun(g)); if (prize == "gun+200") cash = 200; }
    else if (prize == "dog") { p.dogFirst = 1; }
    else if (prize == "slop") { if (!D().cosmetics.empty()) { int c = w.RandI((int)D().cosmetics.size()); const SlopItem& it = D().cosmetics[c]; if (it.kind == "hat") p.hat = c; else if (it.kind == "paint") p.paint = c; else if (it.kind == "sound") p.killSound = c; else if (it.kind == "dog") p.dogCoat = c; else if (it.kind == "flag") p.flag = c; else if (it.kind == "dance") p.dance = c; else p.tracer = c; } }
    else cash = atoi(prize.c_str());
    p.money += cash; if (cash > S.price) p.gambleWon += cash - S.price; else p.gambleLost += S.price - cash;
    w.Emit(EV_SCRATCH, p.pos, p.id, type, (float)std::max(1, cash));
}

// ---------------------------------------------------------------- sabotage
void World::ApplySabotage(Player& p) {
    p.active.clear();
    for (auto s : p.incoming) {
        if (s.item < 0 || s.item >= (int)D().sabotage.size()) continue;
        const std::string& id = D().sabotage[s.item].id;
        if ((p.counters >> s.item) & 1) { s.countered = true; p.counters &= ~(1u << s.item); Emit(EV_COUNTER, p.Eye(), p.id, s.by, (float)s.item); }
        if (id == "tax" && s.countered && s.by >= 0) { players[s.by].taxBy = p.id; }   // (the lawyer: returned to sender)
        if (!s.countered) {
            if (id == "rubber") p.rubberLeft = p.G().MagSize();
            else if (id == "butter") { p.dropsLeft = 2; p.nextDropT = 8 + Rand() * 18; }
            else if (id == "cardboard") { p.cardboardT = 15; p.cardboardHP = 3; }
            else if (id == "bees") p.beesT = 10;
            else if (id == "smudge") { p.smudgeT = 20; p.wipeT = 0; }
            else if (id == "reverse") p.reverseT = 15;
            else if (id == "bagpipe") p.bagpipe = true;
            else if (id == "grudge") p.grudge = !p.dogSausage;
            else if (id == "tax") p.taxBy = s.by;
            else if (id == "swap" && s.by >= 0 && !p.lockbox) { std::swap(p.guns[0], players[s.by].guns[0]); p.swapWith = s.by; }
            else if (id == "pepper") p.pepperT = 10;
            else if (id == "repel") p.repelT = 20;
            else if (id == "glitter") p.glitter = true;
        }
        p.active.push_back(s);
        Emit(EV_SABOTAGE, p.Eye(), p.id, s.by, (float)s.item);
    }
    p.incoming.clear(); p.dogSausage = false;
}
void World::StepSabotage(Player& p) {
    float dt = STEP;
    for (float* f : {&p.beesT, &p.smudgeT, &p.reverseT, &p.pepperT, &p.repelT, &p.cardboardT, &p.gunDropT, &p.jamT}) *f = std::max(0.0f, *f - dt);
    if (phase == PH_HUNT && p.dropsLeft > 0) { p.nextDropT -= dt; if (p.nextDropT <= 0) { p.gunDropT = 2; p.dropsLeft--; p.nextDropT = 12 + Rand() * 15; Emit(EV_DROP, p.Eye(), p.id); } }
    if (p.smudgeT > 0) { if (p.in.wipe) { p.wipeT += dt; if (p.wipeT >= 2) { p.smudgeT = 0; Emit(EV_COUNTER, p.Eye(), p.id, -1, (float)SabIndex("smudge")); } } else p.wipeT = std::max(0.0f, p.wipeT - dt); }
    // the slot pull and a ticket being scratched run on their own clocks (paid when they finish, even past the bell)
    if (p.slotT > 0) { p.slotT -= dt; if (p.slotT <= 0) { Emit(EV_SLOT, p.pos, p.id, p.slotWin, (float)p.slotBet); } }
    if (p.scratchOpen >= 0) { p.scratchT -= dt; if (p.scratchT <= 0) { int t2 = p.scratchOpen; p.scratchOpen = -1; ScratchResolve(*this, p, t2); } }
}

// ---------------------------------------------------------------- commands (the host applies them)
void World::Command(Player& p, const fp::Command& c) {
    const ModeDef& md = M(); bool free = md.practice;
    auto pay = [&](int price) { if (free) return true; if (p.money < price) return false; p.money -= price; p.roundSpent += price; p.boughtThisInter = true; return true; };
    switch (c.kind) {
        case CMD_NAME: if (!c.s.empty()) p.name = c.s.substr(0, 16); break;
        case CMD_HOOK_SWAP: { if (!p.hook.Has()) break; std::swap(p.hook, p.guns[p.hand]); if (!p.guns[p.hand].Has()) { p.guns[p.hand] = p.hook; p.hook = Gun{}; } break; }
        case CMD_BUY_GUN: case CMD_DEAL: {
            int gi = c.kind == CMD_DEAL ? (p.dealIsAtt ? -1 : p.deal) : c.a;
            if (c.kind == CMD_DEAL && p.dealIsAtt) { // (the deal is an attachment)
                fp::Command a; a.kind = CMD_BUY_ATT; a.a = p.deal; a.b = c.b; a.s = "deal"; Command(p, a); break;
            }
            if (gi < 0 || gi >= (int)D().guns.size() || D().guns[gi].price <= 0 || !CanBuy(p, 0) || md.zapperOnly || md.mystery) break;
            int price = D().guns[gi].price; if (c.kind == CMD_DEAL || (gi == p.deal && !p.dealIsAtt)) price = (int)(price * 0.7f);
            if (!pay(price)) break;
            GiveGun(p, MakeGun(gi)); if (c.kind == CMD_DEAL) p.deal = -1;
            if (p.mostExpensive < 0 || D().guns[gi].price > D().guns[p.mostExpensive].price) { p.mostExpensive = gi; p.mostExpensiveBirds = 0; }
            Emit(EV_BUY, p.pos, p.id, gi, 0);
            break;
        }
        case CMD_BUY_ATT: {
            int ai = c.a; if (ai < 0 || ai >= (int)D().atts.size() || !CanBuy(p, 0)) break;
            const AttDef& A = D().atts[ai]; int price = A.price; if ((c.s == "deal" || ai == p.deal) && p.dealIsAtt) price = (int)(price * 0.7f);
            if (A.slot == "counter") {   // grip tape: Butter Fingers' counter, bought at the counter
                if (!pay(price)) break; int sb = SabIndex("butter"); if (sb >= 0) { p.counters |= 1u << sb; for (auto& s : p.incoming) if (s.item == sb) s.countered = true; } Emit(EV_BUY, p.pos, p.id, -1, (float)ai); break;
            }
            if (A.slot == "cosmetic") { if (!pay(price)) break; Emit(EV_BUY, p.pos, p.id, -1, (float)ai); break; }
            int hand = std::clamp(c.b, 0, 1); Gun& g = p.guns[hand]; if (!g.Has() || A.slotIdx < 0) break;
            if (!pay(price)) break;
            g.att[A.slotIdx] = ai; if (A.slotIdx == SL_MAG) g.ammo = g.MagSize();
            if (c.s == "deal" || ai == p.deal) p.deal = -1;
            Emit(EV_BUY, p.pos, p.id, -1, (float)ai);
            break;
        }
        case CMD_BUY_AMMO: { Gun& g = p.G(); if (!g.Has() || D().guns[g.def].ammoPrice <= 0 || !CanBuy(p, 0)) break; int n = std::max(1, c.a); if (!pay(D().guns[g.def].ammoPrice * n)) break; g.reserve += n; break; }
        case CMD_MYSTERY: {
            if (!CanBuy(p, 1) || md.zapperOnly) break;
            int price = p.mysteryFree ? 0 : D().mysteryPrice;
            if (!pay(price)) break; p.mysteryFree = false;
            int zap = GunIndex("zapper"), g; do g = RandI((int)D().guns.size()); while (g == zap);
            GiveGun(p, MakeGun(g)); if (interLen - phaseT < 10) p.pendingCapsule = g;
            Emit(EV_MYSTERY, Station(1), p.id, g);
            break;
        }
        case CMD_SLOT: {
            int bet = c.a; bool ok = false; for (int b : D().slotBets) ok |= b == bet;
            if (!ok || !CanBuy(p, 2) || p.slotT > 0 || p.money < bet) break;
            p.money -= bet; p.boughtThisInter = true; p.slotBet = bet; p.slotT = D().slotPull;
            SlotSpin(rng, p.reels); int gun; int cash = SlotPayout(p.reels, bet, &gun, rng);
            p.money += cash; p.slotWin = gun >= 0 ? 1000 + gun : cash;
            if (gun >= 0) GiveGun(p, MakeGun(gun));
            int value = cash + (gun >= 0 ? D().guns[gun].price : 0);
            if (value > bet) p.gambleWon += value - bet; else p.gambleLost += bet - value;
            if (p.reels[0] == 4 && p.reels[1] == 4 && p.reels[2] == 4) Emit(EV_JACKPOT, Station(2), p.id, -1, (float)cash);
            break;
        }
        case CMD_SCRATCH: { int k = c.a; if (k < 0 || k >= (int)D().scratch.size() || k >= 8 || !CanBuy(p, 3)) break; if (!pay(D().scratch[k].price)) break; p.scratchPocket[k]++; break; }
        case CMD_SCRATCH_OPEN: { int k = c.a; if (k < 0 || k >= 8 || p.scratchPocket[k] <= 0 || p.scratchOpen >= 0) break; p.scratchPocket[k]--; p.scratchOpen = k; p.scratchT = D().scratchTime; break; }
        case CMD_COSMETIC: {
            int k = c.a; if (k < 0 || k >= (int)D().cosmetics.size() || !CanBuy(p, 4)) break; const SlopItem& it = D().cosmetics[k];
            if (!pay(it.price)) break;
            if (it.kind == "hat") { p.hat = k; p.hatOff = false; } else if (it.kind == "paint") p.paint = k; else if (it.kind == "dance") p.dance = k; else if (it.kind == "flag") p.flag = k; else if (it.kind == "dog") p.dogCoat = k; else if (it.kind == "sound") p.killSound = k; else p.tracer = k;
            Emit(EV_BUY, p.pos, p.id, -2, (float)k);
            break;
        }
        case CMD_SABOTAGE: {
            int item = c.a, tg = c.b; if (item < 0 || item >= (int)D().sabotage.size() || tg < 0 || tg >= (int)players.size() || tg == p.id || !CanBuy(p, 4) || md.practice) break;
            Player& T = players[tg]; if (md.teams && T.team == p.team) break;
            if ((int)T.incoming.size() >= D().perTarget) break;   // ("sold out")
            if (!pay(SabPrice(item, tg))) break;
            Sabotage s; s.item = item; s.by = p.id; if ((T.counters >> item) & 1) s.countered = true;
            T.incoming.push_back(s); p.sabGiven++; T.sabTaken++;
            p.sabotaged |= 1u << tg;
            bool all = true; for (const auto& o : players) if (o.id != p.id && !((p.sabotaged >> o.id) & 1)) all = false;
            if (all && players.size() > 2) p.menace = true;
            Emit(EV_SABOTAGE, T.Eye(), tg, p.id, (float)item);
            break;
        }
        case CMD_COUNTER: {
            int item = c.a; if (item < 0 || item >= (int)D().sabotage.size() || !CanBuy(p, 4)) break; const SlopItem& it = D().sabotage[item];
            if (it.counterItem.empty() || it.counterPrice <= 0) break;
            if (!pay(it.counterPrice)) break;
            p.counters |= 1u << item; if (it.counterItem == "sausage") p.dogSausage = true; if (it.counterItem == "lockbox") p.lockbox = true;
            for (auto& s : p.incoming) if (s.item == item) s.countered = true;
            Emit(EV_COUNTER, p.Eye(), p.id, -1, (float)item);
            break;
        }
        case CMD_TAKE_FLOOR: {
            int k = c.a; if (k < 0 || k >= (int)floor.size() || phase != PH_INTER) break;
            if (Vector2Distance({floor[k].p.x, floor[k].p.z}, {p.pos.x, p.pos.z}) > 1.8f) break;
            Gun g = floor[k].g; floor.erase(floor.begin() + k); GiveGun(p, g);
            break;
        }
        default: break;
    }
}

}  // namespace fp
