#include "flats_board.h"
#include <algorithm>
#include <cmath>

namespace flats {

// ---------------------------------------------------------------- names
const char* CharmName(int c) {
    static const char* n[CH_COUNT] = {"Pearl Necklace", "Sailor's Knot", "Brass Compass", "Lucky Tooth", "Iron Anchor", "Tip Jar", "Barbed Hook"};
    return n[c];
}
const char* CharmText(int c) {
    static const char* t[CH_COUNT] = {"Your Shell creatures enter with +1 defense.", "Your first creature each turn costs 1 less blood.",
                                      "Each upkeep you draw a second card.", "Start each battle with an extra card and 2 bones.",
                                      "Your creatures weigh 1 more.", "+10 gold in the pot for each battle you win.",
                                      "Slaying a creature earns you an extra bone."};
    return t[c];
}
const char* ItemName(int k) {
    static const char* n[(int)PackItem::COUNT] = {"Boulder in a Bottle", "Black Goat Bottle", "Fishhook", "Squid Ink", "Barnacle Bandage", "Harpoon"};
    return n[k];
}
const char* ItemText(int k) {
    static const char* t[(int)PackItem::COUNT] = {"A 0/5 boulder appears in your hand.", "A black goat (worth three blood) appears in your hand.",
                                                  "Pull one of the dealer's queued cards into your hand.", "The dealer's creatures strike 1 weaker for two turns.",
                                                  "Give one of your creatures +3 defense.", "Deal 3 damage to one of the dealer's creatures."};
    return t[k];
}
bool ItemNeedsTarget(int k) { return k == (int)PackItem::FISHHOOK || k == (int)PackItem::BANDAGE || k == (int)PackItem::HARPOON; }

const DealerInfo& Dealer(int i) {
    static const DealerInfo d[DEALERS] = {
        {"The Novice", "\"Sit. Cards don't bite. Much.\"", "No tricks. Yet.", 1, 1},
        {"The Tidewife", "\"You learn quickly. Shame.\"", "Her Shell creatures strike for +1.", 1, 1},
        {"The Wreck-Broker", "\"Now we play for real.\"", "A rusted anchor already waits across from you.", 2, 1},
        {"The House", "\"The house always wins. Prove me wrong.\"", "Foil in the deck, two cards drawn a turn, and the Croupier at the table.", 2, 2},
    };
    return d[std::clamp(i, 0, DEALERS - 1)];
}

std::vector<Card> BuildDealerDeck(int dealer, bool elite, Rng& rng) {
    std::vector<Card> deck;
    auto add = [&](const char* n, int k) { for (int i = 0; i < k; i++) deck.push_back(MakeCard(CardIdByName(n))); };
    switch (dealer) {
        case 0: add("Bilge Rat", 3); add("Deckhand", 3); add("Hermit Crab", 2); add("Pufferfish", 2); add("Flying Fish", 2); add("Sea Urchin", 1); add("Anglerfish", 1); break;
        case 1: add("Coral Queen", 1); add("Manta Ray", 1); add("Nautilus", 1); add("Anglerfish", 1); add("Sailfish", 1);
                add("Barnacle Husk", 2); add("Deckhand", 3); add("Fry", 2); add("Bilge Rat", 2); break;
        case 2: add("Rusted Anchor", 1); add("Stingray", 3); add("Ghost Crab", 2); add("Skeleton Sailor", 1); add("Sea Urchin", 2); add("Hammerhead", 2);
                add("Mudskipper", 1); add("Deckhand", 2); break;
        default: add("Great White", 2); add("Kraken Spawn", 1); add("Manta Ray", 2); add("Hammerhead", 2); add("Sperm Whale", 1); add("The Croupier", 1);
                 add("Crab Sentinel", 2); add("Stingray", 1); add("Deckhand", 2); break;
    }
    int foils = dealer >= 3 ? 4 : elite ? 3 : 0;
    for (int i = 0; i < foils; i++) deck[rng.I(0, (int)deck.size() - 1)].edition = ED_FOIL;
    for (auto& c : deck) c.ResetForBattle();
    return deck;
}

// ---------------------------------------------------------------- the board
static bool In(int c) { return c >= 0 && c < COLS; }

int Board::EffWeight(int r, int c) const { return cell[r][c].card.weight + rules[Idx(Owner(r))].weightAll; }

int Board::EffStrength(int r, int c) const {
    const Card& k = cell[r][c].card;
    Side s = Owner(r);
    const SideRules& R = rules[Idx(s)];
    int st = k.strength;
    if (k.edition == ED_FOIL) st += 1;
    if (k.edition == ED_HEX) st += 2;
    for (int dc : {-1, 1}) { int nc = c + dc; if (In(nc) && cell[r][nc].used && cell[r][nc].card.Has(Sigil::TIDECALLER)) st += 1; }
    st += R.strAll;
    if (R.boonSuit >= 0 && k.suit == R.boonSuit) st += 1;
    if (R.tempTurns > 0) st += R.tempStr;
    if (r == FrontRow(s)) { const Cell& o = cell[FrontRow(Other(s))][c]; if (o.used && o.card.Has(Sigil::BRINE)) st -= 1; }
    return std::max(0, st);
}

void Board::Put(int r, int c, const Card& k, Events& ev) {
    cell[r][c].used = true;
    cell[r][c].card = k;
    if (rules[Idx(Owner(r))].pearl && k.suit == SHELL) cell[r][c].card.hp += 1;
    Event e; e.type = Event::Play; e.r1 = r; e.c1 = c; ev.push_back(e);
}

void Board::Move(int r0, int c0, int r1, int c1, Events& ev) {
    cell[r1][c1] = cell[r0][c0];
    cell[r0][c0].used = false;
    Event e; e.type = Event::Move; e.r0 = r0; e.c0 = c0; e.r1 = r1; e.c1 = c1; ev.push_back(e);
}

// A creature dies (or is sacrificed). Bones for its owner; Undying leaves a copy for the hand; a sacrificed Nine Lives cat lives on.
bool Board::Kill(int r, int c, Side killer, bool sacrificed, int killerEdition, Events& ev) {
    Cell& x = cell[r][c];
    if (!x.used) return false;
    Side owner = Owner(r);
    Card dead = x.card;
    if (sacrificed && dead.Has(Sigil::NINE_LIVES)) {
        Event e; e.type = Event::SigilFired; e.r1 = r; e.c1 = c; e.text = "Nine Lives"; ev.push_back(e);
        return false;
    }
    x.used = false;
    int b = dead.Has(Sigil::BONE_KING) ? 4 : 1;
    bones[Idx(owner)] += b;
    Event d; d.type = Event::Death; d.r1 = r; d.c1 = c; d.amount = b; d.text = dead.name; ev.push_back(d);
    if (dead.Has(Sigil::UNDYING)) {
        Card cp = dead; cp.ResetForBattle(); returned[Idx(owner)].push_back(cp);
        Event e; e.type = Event::SigilFired; e.r1 = r; e.c1 = c; e.text = "Undying"; ev.push_back(e);
    }
    if (!sacrificed && killer != owner) {
        if (killer == Side::YOU && killerEdition == ED_GILT) { gold += 4; Event g; g.type = Event::Gold; g.amount = 4; g.r1 = r; g.c1 = c; ev.push_back(g); }
        bones[Idx(killer)] += rules[Idx(killer)].boneOnKill;
    }
    return true;
}

void Board::HitScale(Side s, int amount, int r, int c, Events& ev) {
    scale += s == Side::YOU ? amount : -amount;
    Event e; e.type = Event::ScaleHit; e.r0 = r; e.c0 = c; e.amount = amount; e.r1 = -1; e.c1 = s == Side::YOU ? 1 : -1; ev.push_back(e);
}

// The defender's burrower slides across to take a blow that would fall on an empty lane.
void Board::TryBurrow(Side defender, int col, Events& ev) {
    int df = FrontRow(defender), best = -1;
    for (int k = 0; k < COLS; k++)
        if (k != col && cell[df][k].used && cell[df][k].card.Has(Sigil::BURROWER) && (best < 0 || std::abs(k - col) < std::abs(best - col))) best = k;
    if (best < 0) return;
    Move(df, best, df, col, ev);
    Event e; e.type = Event::SigilFired; e.r1 = df; e.c1 = col; e.text = "Burrower"; ev.push_back(e);
}

// One attacker strikes. Returns true if it struck at all (so the scene knows whether to spend time on this column).
bool Board::StrikeColumn(Side s, int c, Events& ev) {
    int fr = FrontRow(s), of = FrontRow(Other(s));
    if (!cell[fr][c].used) return false;
    int str = EffStrength(fr, c);
    if (str <= 0) return false;
    std::vector<int> targets;
    if (cell[fr][c].card.Has(Sigil::TWIN_TIDE)) { if (In(c - 1)) targets.push_back(c - 1); if (In(c + 1)) targets.push_back(c + 1); }
    else targets.push_back(c);
    bool skim = cell[fr][c].card.Has(Sigil::SKIMMER);
    for (int tc : targets) {
        if (!cell[fr][c].used) break; // it fell to spines on an earlier blow
        Event st; st.type = Event::Strike; st.r0 = fr; st.c0 = c; st.r1 = of; st.c1 = tc; st.amount = str; ev.push_back(st);
        if (!skim && !cell[of][tc].used) TryBurrow(Other(s), tc, ev);
        if (skim || !cell[of][tc].used) { HitScale(s, str, fr, c, ev); continue; }
        if (cell[of][tc].card.Has(Sigil::REPULSIVE)) { Event e; e.type = Event::SigilFired; e.r1 = of; e.c1 = tc; e.text = "Repulsive"; ev.push_back(e); continue; }
        // --- the blow lands on a card: strength against defense, then weight decides the shove
        Card& a = cell[fr][c].card;
        Card& t = cell[of][tc].card;
        int aw = EffWeight(fr, c), tw = EffWeight(of, tc), aed = a.edition;
        bool venom = a.Has(Sigil::VENOM);
        t.hp -= str;
        Event dm; dm.type = Event::Damage; dm.r1 = of; dm.c1 = tc; dm.amount = str; ev.push_back(dm);
        if (venom && t.hp > 0) { t.hp = 0; Event e; e.type = Event::SigilFired; e.r1 = of; e.c1 = tc; e.text = "Venom"; ev.push_back(e); }
        if (t.hp <= 0) { Kill(of, tc, s, false, aed, ev); continue; }
        bool attackerAlive = true;
        if (t.Has(Sigil::SPINES)) {
            a.hp -= 1;
            Event e; e.type = Event::Damage; e.r1 = fr; e.c1 = c; e.amount = 1; e.text = "Spines"; ev.push_back(e);
            if (a.hp <= 0) { Kill(fr, c, Other(s), false, ED_NONE, ev); attackerAlive = false; }
        }
        if (attackerAlive && aw > tw) { // a heavier striker knocks a survivor back a row, if there is room behind it
            int back = BackRow(Other(s));
            if (!cell[back][tc].used) {
                Move(of, tc, back, tc, ev);
                ev.back().type = Event::Knock;
            }
        }
    }
    return true;
}

// A sideways forced move. A heavier mover pushes the card in its way further along; a mover that is not heavier is crushed.
bool Board::Shove(int r, int c, int dir, bool crushIfLight, Events& ev) {
    int nc = c + dir;
    if (!In(nc)) return false;
    if (!cell[r][nc].used) { Move(r, c, r, nc, ev); return true; }
    if (EffWeight(r, c) > EffWeight(r, nc)) {
        if (Shove(r, nc, dir, false, ev)) { Move(r, c, r, nc, ev); ev.back().type = Event::Push; return true; }
        return false;
    }
    if (crushIfLight) { Event e; e.type = Event::Crush; e.r1 = r; e.c1 = c; ev.push_back(e); Kill(r, c, Other(Owner(r)), false, ED_NONE, ev); }
    return false;
}

void Board::SentinelPass(Side def, Events& ev) {
    int df = FrontRow(def), af = FrontRow(Other(def));
    for (int c = 0; c < COLS; c++) {
        if (!cell[af][c].used || cell[df][c].used) continue;
        int best = -1;
        for (int k = 0; k < COLS; k++)
            if (k != c && cell[df][k].used && cell[df][k].card.Has(Sigil::SENTINEL) && (best < 0 || std::abs(k - c) < std::abs(best - c))) best = k;
        if (best < 0) continue;
        Move(df, best, df, c, ev);
        Event e; e.type = Event::SigilFired; e.r1 = df; e.c1 = c; e.text = "Sentinel"; ev.push_back(e);
    }
}

// The dealer's queue steps forward. A queued card takes an empty front space; if a friend stands there, it only shoves in
// when it is strictly heavier (the friend is pushed back into the queue), otherwise it waits.
void Board::AdvanceQueue(Events& ev) {
    for (int c = 0; c < COLS; c++) {
        if (!cell[R_FOE_QUEUE][c].used) continue;
        if (!cell[R_FOE_FRONT][c].used) Move(R_FOE_QUEUE, c, R_FOE_FRONT, c, ev);
        else if (EffWeight(R_FOE_QUEUE, c) > EffWeight(R_FOE_FRONT, c)) {
            Cell a = cell[R_FOE_QUEUE][c], b = cell[R_FOE_FRONT][c];
            cell[R_FOE_FRONT][c] = a; cell[R_FOE_QUEUE][c] = b;
            Event e; e.type = Event::Push; e.r0 = R_FOE_QUEUE; e.c0 = c; e.r1 = R_FOE_FRONT; e.c1 = c; ev.push_back(e);
        }
    }
    SentinelPass(Side::YOU, ev);
}

// The end of a side's turn: fry grow up, swimmers drift, heavy currents shove, the reserve steps forward, everything ages.
void Board::EndPhase(Side s, Events& ev) {
    int fr = FrontRow(s), br = BackRow(s);
    for (int r : {fr, br})
        for (int c = 0; c < COLS; c++) {
            Cell& x = cell[r][c];
            if (x.used && x.card.Has(Sigil::FRY) && x.card.age >= 1 && x.card.evolveId >= 0) {
                Card n = MakeCard(x.card.evolveId, x.card.edition);
                n.age = 1;
                x.card = n;
                Event e; e.type = Event::Evolve; e.r1 = r; e.c1 = c; e.text = n.name; ev.push_back(e);
            }
        }
    bool moved[COLS] = {false, false, false, false};
    for (int c = 0; c < COLS; c++) { // swimmers drift one lane, turning about at a wall or a blocker (never crushed: they choose to move)
        Cell& x = cell[fr][c];
        if (!x.used || !x.card.Has(Sigil::SWIMMER) || moved[c]) continue;
        int nc = c + x.card.dir;
        if (!In(nc) || cell[fr][nc].used) { x.card.dir = -x.card.dir; nc = c + x.card.dir; }
        if (In(nc) && !cell[fr][nc].used) { Move(fr, c, fr, nc, ev); moved[nc] = true; }
    }
    for (int c = 0; c < COLS; c++) { // heavy currents force their way sideways
        Cell& x = cell[fr][c];
        if (!x.used || !x.card.Has(Sigil::HEAVY_CURRENT)) continue;
        if (!In(c + x.card.dir)) x.card.dir = -x.card.dir;
        int d = x.card.dir;
        Event e; e.type = Event::SigilFired; e.r1 = fr; e.c1 = c; e.text = "Heavy Current"; ev.push_back(e);
        Shove(fr, c, d, true, ev);
    }
    if (s == Side::YOU) // your reserve steps into empty lanes (the dealer's queue moves at the start of his turn instead)
        for (int c = 0; c < COLS; c++) if (cell[br][c].used && !cell[fr][c].used) Move(br, c, fr, c, ev);
    for (int r : {fr, br}) for (int c = 0; c < COLS; c++) if (cell[r][c].used) cell[r][c].card.age++;
    SentinelPass(Other(s), ev);
    SideRules& R = rules[Idx(s)];
    if (R.tempTurns > 0) R.tempTurns--;
}

// ---------------------------------------------------------------- one battle
void Battle::Start(const BattleSetup& s, Rng& rng) {
    *this = Battle();
    dealer = s.dealer; elite = s.elite; charms = s.charms; items = s.items;
    board.rules[0].pearl = (charms >> CH_PEARL) & 1u;
    board.rules[0].weightAll = ((charms >> CH_ANCHOR) & 1u) ? 1 : 0;
    board.rules[0].boneOnKill = ((charms >> CH_BARB) & 1u) ? 1 : 0;
    deck = s.deck;
    for (auto& c : deck) c.ResetForBattle();
    rng.Shuffle(deck);
    int draw = 3 + (((charms >> CH_TOOTH) & 1u) ? 1 : 0) + (s.boon == Boon::EXTRA_DRAW ? 2 : 0);
    for (int i = 0; i < draw && !deck.empty(); i++) { hand.push_back(deck.back()); deck.pop_back(); }
    hand.push_back(Minnow());
    board.bones[0] = s.startBones + (((charms >> CH_TOOTH) & 1u) ? 2 : 0) + (s.boon == Boon::BONES ? 3 : 0);
    if (s.boon == Boon::HEAD_START) board.scale = 1;
    foeDeck = BuildDealerDeck(dealer, elite, rng);
    rng.Shuffle(foeDeck);
    foeMaxPlays = Dealer(dealer).plays + (elite ? 1 : 0);
    if (dealer == 1) board.rules[1].boonSuit = SHELL;
    if (elite) board.rules[1].boonSuit = rng.I(0, SUITS - 1);
    Events dummy;
    if (dealer == 2) board.Put(R_FOE_FRONT, rng.I(0, COLS - 1), MakeCard(CardIdByName("Rusted Anchor")), dummy);
    FoeDrawCards(3, rng);
    foePlays = 0;
    FoeChoose(dummy, rng); // one card is already in the queue when you sit down, so you can see what is coming
    foePlays = 0;
    turn = Turn::YOU_DRAW;
    turnNo = 1;
    firstPlayThisTurn = true;
}

void Battle::FoeDrawCards(int n, Rng& rng) {
    (void)rng;
    for (int i = 0; i < n && !foeDeck.empty() && foeHand.size() < 6; i++) { foeHand.push_back(foeDeck.back()); foeDeck.pop_back(); }
}

void Battle::Drain(Events& ev) {
    for (int i = 0; i < 2; i++) {
        std::vector<Card>& h = i == 0 ? hand : foeHand;
        for (auto& c : board.returned[i]) if ((int)h.size() < MAX_HAND) h.push_back(c);
        board.returned[i].clear();
    }
    (void)ev;
}

void Battle::CheckOver() {
    if (turn == Turn::OVER) return;
    if (board.OverCheck()) { turn = Turn::OVER; winner = board.scale > 0 ? 1 : -1; return; }
    if (turnNo > 40) { turn = Turn::OVER; winner = board.scale > 0 ? 1 : -1; }
}

bool Battle::Draw(bool fromDeck, Events& ev, Rng& rng) {
    (void)rng;
    if (turn != Turn::YOU_DRAW || (int)hand.size() >= MAX_HAND) { if (turn == Turn::YOU_DRAW) turn = Turn::YOU_MAIN; return false; }
    auto take = [&](bool deckPile) {
        if (deckPile && !deck.empty()) { hand.push_back(deck.back()); deck.pop_back(); }
        else hand.push_back(Minnow());
        Event e; e.type = Event::Drew; e.amount = hand.back().id; ev.push_back(e);
    };
    (void)fromDeck;
    int minnows = 0;
    for (auto& h : hand) if (h.name == "Minnow") minnows++;
    if (!deck.empty()) take(true);                                                                       // a card from the deck...
    if (minnows < 3 && (int)hand.size() < MAX_HAND) take(false);                                          // ...and a free minnow to spend as blood
    if (((charms >> CH_COMPASS) & 1u) && !deck.empty() && (int)hand.size() < MAX_HAND) take(true);         // the compass: a second card
    turn = Turn::YOU_MAIN;
    firstPlayThisTurn = true;
    return true;
}

int Battle::BloodNeeded(const Card& c) const {
    if (c.cost != CostType::BLOOD) return 0;
    int need = c.costAmount;
    if (((charms >> CH_KNOT) & 1u) && firstPlayThisTurn) need = std::max(1, need - 1);
    return need;
}

bool Battle::CanPlay(int hi, int col, const std::vector<std::pair<int, int>>& sacs, std::string* why) const {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (turn != Turn::YOU_MAIN) return no("Not your turn.");
    if (hi < 0 || hi >= (int)hand.size() || col < 0 || col >= COLS) return no("Pick a card and a lane.");
    const Card& c = hand[hi];
    int blood = 0;
    for (size_t i = 0; i < sacs.size(); i++) {
        auto [r, cc] = sacs[i];
        if (r < R_YOU_FRONT || r > R_YOU_BACK || cc < 0 || cc >= COLS || !board.cell[r][cc].used) return no("Sacrifice one of your own creatures.");
        for (size_t j = 0; j < i; j++) if (sacs[j] == sacs[i]) return no("Each sacrifice must be a different creature.");
        blood += board.cell[r][cc].card.BloodValue();
    }
    if (c.cost == CostType::BLOOD && blood < BloodNeeded(c)) return no("Not enough blood: sacrifice more creatures.");
    if (c.cost != CostType::BLOOD && !sacs.empty()) return no("This card is not paid in blood.");
    if (c.cost == CostType::BONES && board.bones[0] < c.costAmount) return no("Not enough bones.");
    auto remains = [&](int r, int cc) {
        if (!board.cell[r][cc].used) return false;
        for (auto& s : sacs) if (s.first == r && s.second == cc) return board.cell[r][cc].card.Has(Sigil::NINE_LIVES);
        return true;
    };
    if (remains(R_YOU_FRONT, col)) { // shoving onto an occupied lane: only a strictly heavier card may, and the occupant needs room behind
        int mine = c.weight + board.rules[0].weightAll;
        if (mine <= board.EffWeight(R_YOU_FRONT, col)) return no("That lane is held by a card at least as heavy.");
        if (remains(R_YOU_BACK, col)) return no("There is no room behind to push it into.");
    }
    return true;
}

bool Battle::Play(int hi, int col, const std::vector<std::pair<int, int>>& sacs, Events& ev, Rng& rng) {
    (void)rng;
    if (!CanPlay(hi, col, sacs, nullptr)) return false;
    Card c = hand[hi];
    for (auto [r, cc] : sacs) board.Kill(r, cc, Side::YOU, true, ED_NONE, ev);
    if (c.cost == CostType::BONES) board.bones[0] -= c.costAmount;
    hand.erase(hand.begin() + hi);
    c.ResetForBattle();
    if (board.cell[R_YOU_FRONT][col].used) { // the heavier card shoves the occupant back a row
        board.Move(R_YOU_FRONT, col, R_YOU_BACK, col, ev);
        ev.back().type = Event::Push;
    }
    board.Put(R_YOU_FRONT, col, c, ev);
    if (c.Has(Sigil::SPAWN) && (int)hand.size() < MAX_HAND) {
        Card cp = c; cp.sigils.erase(std::remove(cp.sigils.begin(), cp.sigils.end(), Sigil::SPAWN), cp.sigils.end());
        cp.ResetForBattle();
        hand.push_back(cp);
        Event e; e.type = Event::SigilFired; e.r1 = R_YOU_FRONT; e.c1 = col; e.text = "Spawn"; ev.push_back(e);
    }
    if (c.Has(Sigil::SCAVENGER)) board.itemsFound[0]++;
    firstPlayThisTurn = false;
    board.SentinelPass(Side::FOE, ev);
    Drain(ev);
    CheckOver();
    return true;
}

bool Battle::UseItem(int slot, int r, int c, Events& ev) {
    if (turn != Turn::YOU_MAIN || slot < 0 || slot >= (int)items.size()) return false;
    PackItem k = (PackItem)items[slot];
    bool ok = false;
    switch (k) {
        case PackItem::BOULDER: if ((int)hand.size() < MAX_HAND) { hand.push_back(MakeCard(CardIdByName("Boulder"))); ok = true; } break;
        case PackItem::GOAT: if ((int)hand.size() < MAX_HAND) { hand.push_back(MakeCard(CardIdByName("Black Goat"))); ok = true; } break;
        case PackItem::FISHHOOK:
            if (r == R_FOE_QUEUE && In(c) && board.cell[r][c].used && (int)hand.size() < MAX_HAND) {
                Card k2 = board.cell[r][c].card; k2.cost = CostType::FREE; k2.costAmount = 0; k2.ResetForBattle();
                board.cell[r][c].used = false;
                hand.push_back(k2);
                ok = true;
            }
            break;
        case PackItem::INK: board.rules[1].tempStr = -1; board.rules[1].tempTurns = 2; ok = true; break;
        case PackItem::BANDAGE:
            if ((r == R_YOU_FRONT || r == R_YOU_BACK) && In(c) && board.cell[r][c].used) { board.cell[r][c].card.hp += 3; ok = true; }
            break;
        case PackItem::HARPOON:
            if ((r == R_FOE_QUEUE || r == R_FOE_FRONT) && In(c) && board.cell[r][c].used) {
                board.cell[r][c].card.hp -= 3;
                Event d; d.type = Event::Damage; d.r1 = r; d.c1 = c; d.amount = 3; ev.push_back(d);
                if (board.cell[r][c].card.hp <= 0) board.Kill(r, c, Side::YOU, false, ED_NONE, ev);
                ok = true;
            }
            break;
        default: break;
    }
    if (!ok) return false;
    items.erase(items.begin() + slot);
    Drain(ev);
    return true;
}

void Battle::EndTurn() {
    if (turn != Turn::YOU_MAIN) return;
    turn = Turn::YOU_COMBAT;
    col = 0;
}

// The dealer lays one card into his queue: the best (card, lane) pair by a simple reading of the lane across from it.
int Battle::BestColumn(const Card& c, Side me, Rng& rng) const {
    // For the dealer: queue lanes; for the sensible player: front lanes. Score each empty lane.
    int mineFront = FrontRow(me), theirFront = FrontRow(Other(me)), place = me == Side::FOE ? R_FOE_QUEUE : R_YOU_FRONT;
    int best = -1; float bestS = -1e9f;
    for (int col2 = 0; col2 < COLS; col2++) {
        if (board.cell[place][col2].used) continue;
        if (me == Side::FOE && board.cell[R_FOE_FRONT][col2].used && board.cell[R_FOE_FRONT][col2].card.weight >= c.weight) { /* it would wait behind a friend */ }
        float s = 0;
        const Cell& t = board.cell[theirFront][col2];
        if (t.used) {
            int tstr = board.EffStrength(theirFront, col2);
            if (c.strength >= t.card.hp) s += 3;
            if (c.defense > tstr) s += 2;
            s += 0.25f * c.weight;
            if (t.card.Has(Sigil::SPINES) && c.defense <= 2) s -= 1.5f;
        } else s += 2.0f * (c.strength + (c.Has(Sigil::SKIMMER) ? 1 : 0)); // open lane: the blow goes straight to the scales
        if (board.cell[mineFront][col2].used && me == Side::FOE) s -= 1.5f; // a friend already fights here
        s += rng.F() * 0.6f;
        if (s > bestS) { bestS = s; best = col2; }
    }
    return best;
}

void Battle::FoeChoose(Events& ev, Rng& rng) {
    int allowed = foeMaxPlays + (board.scale >= 3 ? 1 : 0);
    int bi = -1, bc = -1; float bs = -1e9f;
    if (foePlays < allowed)
        for (int i = 0; i < (int)foeHand.size(); i++) {
            int col2 = BestColumn(foeHand[i], Side::FOE, rng);
            if (col2 < 0) break;
            float s = foeHand[i].Rating() * 0.5f + rng.F();
            if (s > bs) { bs = s; bi = i; bc = col2; }
        }
    if (bi < 0) { turn = Turn::FOE_COMBAT; col = 0; return; }
    Card c = foeHand[bi];
    foeHand.erase(foeHand.begin() + bi);
    c.ResetForBattle();
    board.Put(R_FOE_QUEUE, bc, c, ev);
    foePlays++;
}

void Battle::Advance(Events& ev, Rng& rng) {
    switch (turn) {
        case Turn::YOU_COMBAT:
            while (col < COLS) { bool hit = board.StrikeColumn(Side::YOU, col, ev); col++; if (hit) break; }
            Drain(ev);
            CheckOver();
            if (turn != Turn::OVER && col >= COLS) turn = Turn::YOU_END;
            break;
        case Turn::YOU_END:
            board.EndPhase(Side::YOU, ev);
            Drain(ev);
            CheckOver();
            if (turn != Turn::OVER) turn = Turn::FOE_START;
            break;
        case Turn::FOE_START:
            FoeDrawCards(Dealer(dealer).draws, rng);
            board.AdvanceQueue(ev);
            foePlays = 0;
            turn = Turn::FOE_MAIN;
            break;
        case Turn::FOE_MAIN:
            FoeChoose(ev, rng);
            break;
        case Turn::FOE_COMBAT:
            while (col < COLS) { bool hit = board.StrikeColumn(Side::FOE, col, ev); col++; if (hit) break; }
            Drain(ev);
            CheckOver();
            if (turn != Turn::OVER && col >= COLS) turn = Turn::FOE_END;
            break;
        case Turn::FOE_END:
            board.EndPhase(Side::FOE, ev);
            Drain(ev);
            turnNo++;
            CheckOver();
            if (turn != Turn::OVER) turn = Turn::YOU_DRAW;
            break;
        default: break;
    }
}

// A sensible player: draws, then repeatedly plays the card with the best value for its cost, sacrificing the cheapest
// creatures that pay for it, and rings the bell when nothing is worth playing.
void Battle::AutoYourTurn(Events& ev, Rng& rng) {
    if (turn == Turn::YOU_DRAW) {
        int fodder = 0;
        for (auto& c : hand) if (c.cost == CostType::FREE) fodder++;
        bool fromDeck = !deck.empty() && (hand.size() < 3 || fodder >= 1 || rng.C(0.6f));
        if (deck.empty()) fromDeck = false;
        if (!Draw(fromDeck, ev, rng)) turn = Turn::YOU_MAIN;
    }
    for (int plays = 0; plays < 5 && turn == Turn::YOU_MAIN; plays++) {
        float bestScore = 0.4f;
        int bh = -1, bcol = -1;
        std::vector<std::pair<int, int>> bsac;
        for (int hi = 0; hi < (int)hand.size(); hi++) {
            const Card& c = hand[hi];
            std::vector<std::pair<int, int>> sac;
            float sacCost = 0;
            if (c.cost == CostType::BLOOD) {
                std::vector<std::pair<float, std::pair<int, int>>> cand;
                for (int r : {R_YOU_FRONT, R_YOU_BACK})
                    for (int cc = 0; cc < COLS; cc++)
                        if (board.cell[r][cc].used) {
                            const Card& k = board.cell[r][cc].card;
                            float v = (float)k.Rating() - (k.Has(Sigil::NINE_LIVES) ? 4.0f : 0) - (k.Has(Sigil::BALLAST) ? 3.0f : 0) - (k.hp < k.defense ? 1.0f : 0);
                            cand.push_back({v, {r, cc}});
                        }
                std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first < b.first; });
                int have = 0;
                for (auto& k : cand) { if (have >= BloodNeeded(c)) break; sac.push_back(k.second); have += board.cell[k.second.first][k.second.second].card.BloodValue(); sacCost += std::max(0.0f, k.first) * 0.9f; }
                if (have < BloodNeeded(c)) continue;
            } else if (c.cost == CostType::BONES && board.bones[0] < c.costAmount) continue;
            float boneCost = c.cost == CostType::BONES ? 0.5f * c.costAmount : 0;
            int col2 = BestColumn(c, Side::YOU, rng);
            if (col2 < 0) {
                // no empty lane: a heavier card may shove
                for (int k = 0; k < COLS; k++) if (CanPlay(hi, k, sac, nullptr)) { col2 = k; break; }
            }
            if (col2 < 0 || !CanPlay(hi, col2, sac, nullptr)) continue;
            float value = (float)c.Rating() * 1.1f - sacCost - boneCost + (c.cost == CostType::FREE ? 0.3f : 0);
            if (c.name == "Minnow") value = (board.cell[R_YOU_FRONT][col2].used ? -1.0f : 0.8f);   // a minnow is fodder, played only to be spent or to soak a lane
            if (value > bestScore) { bestScore = value; bh = hi; bcol = col2; bsac = sac; }
        }
        if (bh < 0) break;
        Play(bh, bcol, bsac, ev, rng);
    }
    if (turn == Turn::YOU_MAIN) EndTurn();
}

}  // namespace flats
