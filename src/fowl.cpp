// Fowl Play's headless core (see fowl.h): the data, the match clock, the birds and their flight, shooting and kill
// credit, the fun guns' projectiles, the dog. The room (shop, gambling, Slop) is fowl_shop.cpp; bots and tests fowl_bots.cpp.
#include "fowl.h"
#include "json.h"
#include "redtide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fp {

// ---------------------------------------------------------------- the data
static Color Col(const Json& a, Color def) { if (a.type != Json::Arr || a.a.size() < 3) return def; return {(unsigned char)a[0].I(), (unsigned char)a[1].I(), (unsigned char)a[2].I(), 255}; }
static Data Load() {
    Data d; std::string dir = rt::DataDir() + "/../fowl/";
    Json g = LoadJsonFile(dir + "fowlplay_guns.json");
    for (const auto& e : g["guns"].a) {
        GunDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.kind = e["kind"].Str0("real"); x.type = e["type"].Str0(""); x.note = e["note"].Str0(""); x.special = e["special"].Str0("");
        x.price = e["price"].I(0); x.damage = e["damage"].I(1); x.pellets = e["pellets"].I(1); x.mag = e["mag"].I(6); x.reserve = e["reserve"].I(0); x.ammoPrice = e["ammoPrice"].I(0);
        x.interval = e["interval"].F(0.2f); x.reload = e["reload"].F(1); x.spread = e["spread"].F(0.5f); x.range = e["range"].F(60); x.kick = e["kick"].F(1); x.speed = e["speed"].F(40); x.autoFire = e["auto"].Bool0(false);
        d.guns.push_back(x);
    }
    Json a = LoadJsonFile(dir + "fowlplay_attachments.json");
    static const char* SLOTS[SL_COUNT] = {"sight", "magazine", "barrel", "trigger", "under"};
    for (const auto& e : a["attachments"].a) {
        AttDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.slot = e["slot"].Str0(""); x.note = e["note"].Str0(""); x.price = e["price"].I(0);
        for (int s = 0; s < SL_COUNT; s++) if (x.slot == SLOTS[s]) x.slotIdx = s;
        x.spread = e["spread"].F(1); x.zoom = e["zoom"].F(1); x.mag = e["mag"].F(1); x.reload = e["reload"].F(1); x.shotSpread = e["shotSpread"].F(1); x.range = e["range"].F(1); x.seek = e["seek"].F(0); x.money = e["money"].F(1); x.rate = e["rate"].F(1); x.steady = e["steady"].F(1);
        d.atts.push_back(x);
    }
    Json b = LoadJsonFile(dir + "fowlplay_birds.json");
    for (const auto& e : b["birds"].a) {
        BirdDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.pattern = e["pattern"].Str0("zigzag");
        x.from = e["from"].I(1); x.hp = e["hp"].I(1); x.counts = e["counts"].I(1); x.pays = e["pays"].I(10); x.flock = e["flock"].I(0); x.convoy = e["convoy"].I(0); x.split = e["split"].I(0); x.perRound = e["perRound"].I(0);
        x.speed = e["speed"].F(7); x.r = e["r"].F(0.4f); x.life = e["life"].F(8); x.rare = e["rare"].F(1);
        x.armor = e["armor"].Bool0(false); x.metal = e["metal"].Bool0(false); x.decoy = e["decoy"].Bool0(false); x.angry = e["angry"].Bool0(false); x.boo = e["boo"].Bool0(false); x.clay = e["clay"].Bool0(false);
        x.body = Col(e["body"], GRAY); x.wing = Col(e["wing"], DARKGRAY); x.head = Col(e["head"], GRAY);
        d.birds.push_back(x);
    }
    Json r = LoadJsonFile(dir + "fowlplay_rounds.json");
    d.hunt = r["hunt"].F(60); d.tally = r["tally"].F(5); d.intermission = r["intermission"].F(45); d.bell = r["bell"].F(10); d.rounds = r["rounds"].I(15); d.startMoney = r["startMoney"].I(60);
    d.patience = r["patience"].F(0.05f); d.perfectBonus = r["perfectWaveBonus"].I(30); d.bonusEvery = r["bonusEvery"].I(3); d.bonusLength = r["bonusLength"].F(10); d.bonusClays = r["bonusClays"].I(10); d.goldenHour = r["goldenHour"].I(15);
    for (const auto& e : r["brackets"].a) {
        Bracket x; x.from = e["from"].I(1); x.perWave = e["perWave"].I(2); x.waves = e["waves"].I(6); x.speed = e["speed"].F(1); x.erratic = e["erratic"].F(0);
        for (const auto& s : e["birds"].a) { int k = -1; for (int i = 0; i < (int)d.birds.size(); i++) if (d.birds[i].id == s.Str0("")) k = i; if (k >= 0) x.birds.push_back(k); }
        d.brackets.push_back(x);
    }
    Json m = LoadJsonFile(dir + "fowlplay_gambling.json");
    for (const auto& v : m["slots"]["bets"].a) d.slotBets.push_back(v.I(10));
    for (const auto& v : m["slots"]["weights"].a) d.slotWeights.push_back(v.I(10));
    d.slotPull = m["slots"]["pull"].F(2); d.threeBell = m["slots"]["threeBell"].I(12); d.threeZappa = m["slots"]["threeZappa"].I(30);
    for (const auto& e : m["scratch"].a) {
        Scratch s; s.id = e["id"].Str0(""); s.name = e["name"].Str0(s.id); s.price = e["price"].I(5); s.odds = e["odds"].I(3);
        for (const auto& pz : e["prizes"].a) { std::string k = pz[0].type == Json::Str ? pz[0].Str0("") : std::to_string(pz[0].I(0)); s.prizes.push_back({k, pz[1].F(0)}); }
        d.scratch.push_back(s);
    }
    d.scratchTime = m["scratchTime"].F(3); d.mysteryPrice = m["mystery"]["price"].I(100);
    Json s = LoadJsonFile(dir + "fowlplay_slop.json");
    for (const auto& e : s["cosmetics"].a) { SlopItem x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.kind = e["kind"].Str0(""); x.price = e["price"].I(20); d.cosmetics.push_back(x); }
    for (const auto& e : s["sabotage"].a) { SlopItem x; x.sabotage = true; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.price = e["price"].I(50); x.effect = e["effect"].Str0(""); x.counter = e["counter"].Str0(""); x.counterItem = e["counterItem"].Str0(""); x.counterPrice = e["counterPrice"].I(0); d.sabotage.push_back(x); }
    d.perTarget = s["perTarget"].I(2); d.leaderMarkup = s["leaderMarkup"].F(0.5f);
    Json md = LoadJsonFile(dir + "fowlplay_modes.json");
    for (const auto& e : md["modes"].a) {
        ModeDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.rule = e["rule"].Str0(""); x.rounds = e["rounds"].I(15); x.intermission = e["intermission"].F(45); x.startMoney = e["startMoney"].I(-1);
        x.zapperOnly = e["zapperOnly"].Bool0(false); x.moneyScore = e["moneyScore"].Bool0(false); x.teams = e["teams"].Bool0(false); x.night = e["night"].Bool0(false); x.mystery = e["mystery"].Bool0(false); x.practice = e["practice"].Bool0(false);
        d.modes.push_back(x);
    }
    if (d.modes.empty()) d.modes.push_back(ModeDef{"open", "Open Season", "", 15, 45});
    const Json& tk = md["tokens"]; d.tokMatch = tk["match"].I(10); d.tokPerBirds = tk["perBirds"].I(5); d.tokWin = tk["win"].I(25); d.tokUfo = tk["ufo"].I(50); d.tokPerfect = tk["perfect"].I(20);
    if (d.guns.empty()) { GunDef z; z.id = "zapper"; z.name = "The Zapper"; d.guns.push_back(z); }
    if (d.slotWeights.size() < 5) d.slotWeights = {30, 26, 22, 14, 8};
    if (d.slotBets.empty()) d.slotBets = {10, 25, 50};
    return d;
}
const Data& D() { static Data d = Load(); return d; }
int GunIndex(const std::string& id) { for (int i = 0; i < (int)D().guns.size(); i++) if (D().guns[i].id == id) return i; return -1; }
int AttIndex(const std::string& id) { for (int i = 0; i < (int)D().atts.size(); i++) if (D().atts[i].id == id) return i; return -1; }
int BirdIndex(const std::string& id) { for (int i = 0; i < (int)D().birds.size(); i++) if (D().birds[i].id == id) return i; return -1; }
int SabIndex(const std::string& id) { for (int i = 0; i < (int)D().sabotage.size(); i++) if (D().sabotage[i].id == id) return i; return -1; }
int ModeIndex(const std::string& id) { for (int i = 0; i < (int)D().modes.size(); i++) if (D().modes[i].id == id) return i; return 0; }

// ---------------------------------------------------------------- a gun with its attachments
static float AttMul(const Gun& g, float AttDef::*f) { float m = 1; for (int s = 0; s < SL_COUNT; s++) if (g.att[s] >= 0) m *= D().atts[g.att[s]].*f; return m; }
int Gun::MagSize() const { if (def < 0) return 0; return std::max(1, (int)roundf(D().guns[def].mag * AttMul(*this, &AttDef::mag))); }
float Gun::Spread() const { if (def < 0) return 1; const GunDef& G = D().guns[def]; float s = G.spread * AttMul(*this, &AttDef::spread); if (G.pellets > 1) s *= AttMul(*this, &AttDef::shotSpread); return s; }
float Gun::Range() const { return def < 0 ? 50 : D().guns[def].range * AttMul(*this, &AttDef::range); }
float Gun::Interval() const { return def < 0 ? 0.3f : D().guns[def].interval / AttMul(*this, &AttDef::rate); }
float Gun::ReloadTime() const { return def < 0 ? 1 : D().guns[def].reload * AttMul(*this, &AttDef::reload); }
float Gun::MoneyMul() const { return AttMul(*this, &AttDef::money); }
float Gun::Seek() const { float s = 0; for (int k = 0; k < SL_COUNT; k++) if (att[k] >= 0) s += D().atts[att[k]].seek; return s; }
Gun MakeGun(int def) { Gun g; g.def = def; if (def >= 0) { g.ammo = g.MagSize(); g.reserve = D().guns[def].reserve; } return g; }

Vector3 Bird::PosAgo(int steps) const { if (steps <= 0 || histN == 0) return p; int k = std::min(steps, histN) - 1; return hist[k]; }

// ---------------------------------------------------------------- the world
float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
const ModeDef& World::M() const { return D().modes[std::clamp(mode, 0, (int)D().modes.size() - 1)]; }
const Bracket& World::Br() const { const auto& B = D().brackets; int k = 0; for (int i = 0; i < (int)B.size(); i++) if (round >= B[i].from) k = i; static Bracket none; return B.empty() ? none : B[k]; }
int World::Score(const Player& p) const { return M().moneyScore ? p.money : p.birds; }
int World::Leader() const { int best = -1, bs = -1; for (const auto& p : players) if (p.present && Score(p) > bs) { bs = Score(p); best = p.id; } return best; }

Vector3 World::Station(int k) {
    static const Vector3 S[7] = {{-10.5f, 0, -6}, {-5, 0, -14.5f}, {2, 0, -15.5f}, {8.5f, 0, -14.5f}, {12.5f, 0, -7}, {-12.5f, 0, -14}, {5.5f, 0, -4.5f}};
    return S[std::clamp(k, 0, 6)];
}
int World::NearStation(const Player& p) const { for (int k = 0; k < 7; k++) { Vector3 s = Station(k); float r = k == 2 ? 4.5f : 2.6f; if (Vector2Distance({p.pos.x, p.pos.z}, {s.x, s.z}) < r) return k; } return -1; }

void World::Init(int humans, int bots, int modeIdx, uint32_t seed) {
    rng = seed ? seed * 2654435761u + 11 : 7; for (int i = 0; i < 3; i++) Rand();
    mode = std::clamp(modeIdx, 0, (int)D().modes.size() - 1); roundsTotal = M().rounds; interLen = M().intermission;
    players.clear(); birds.clear(); projs.clear(); floor.clear(); events.clear(); evCount = 0;
    int n = std::clamp(humans + bots, 1, MAX_PLAYERS);
    for (int i = 0; i < n; i++) {
        Player p; p.id = i; p.stall = i; p.team = M().teams ? (i < 3 ? 0 : 1) : i; p.bot = i >= humans; p.pos = StallPos(i);
        p.guns[0] = MakeGun(std::max(0, GunIndex("zapper"))); p.money = M().startMoney >= 0 ? M().startMoney : D().startMoney;
        players.push_back(p);
    }
    round = 0; champion = -1; ufoDone = goldenDone = false; dog = Dog{};
    phase = PH_TALLY; phaseT = D().tally;   // (a short wait, then round 1)
    BeginInter(); phaseT = interLen - 4;    // (the first intermission is a 4 s walk-in)
}
void World::BeginHunt() {
    round++; phase = PH_HUNT; phaseT = 0; wave = 0; wavesThisRound = Br().waves; nextWaveT = 1.0f; goldenDone = round < 4;
    waveBirds.assign(wavesThisRound + 2, 0); waveKills.assign(wavesThisRound + 2, 0); waveCredit.assign(wavesThisRound + 2, -2);
    birds.clear(); projs.clear(); flareT = 0;
    for (auto& p : players) {
        p.room = 0; p.pos = StallPos(p.stall); p.roundBirds = p.roundMoney = 0; p.roundPay = 0; p.hatOff = false;
        for (auto& g : p.guns) if (g.Has()) { g.ammo = g.MagSize(); g.reloadT = 0; g.cool = 0; g.spin = 0; if (D().guns[g.def].reserve > 0) g.reserve = std::max(g.reserve, 0); }
        ApplySabotage(p);
    }
    Emit(EV_ROUND, {0, 0, 0}, -1, -1, (float)round);
}
void World::BeginBonus() {
    for (auto& p : players) { p.saved[0] = p.guns[0]; p.saved[1] = p.guns[1]; p.savedHand = p.hand; p.guns[0] = MakeGun(std::max(0, GunIndex("zapper"))); p.guns[1] = Gun{}; p.hand = 0; p.inBonus = true; }
    phase = PH_BONUS; phaseT = 0; nextWaveT = 0.5f; wave = wavesThisRound;   // (clays: their own "wave")
    Emit(EV_BONUS, {0, 0, 0});
}
void World::BeginTally() {
    for (auto& p : players) if (p.inBonus) { p.guns[0] = p.saved[0]; p.guns[1] = p.saved[1]; p.hand = p.savedHand; p.inBonus = false; }
    phase = PH_TALLY; phaseT = 0; dog.state = 4; dog.t = 0;
    bool golden = round >= D().goldenHour;
    for (auto& p : players) {
        float pay = p.roundPay * (golden ? 2.0f : 1.0f);
        if (p.grudge) { p.grudgeMoney += (int)(pay * 0.5f); }   // (halved at the end of the match)
        if (p.taxBy >= 0 && p.taxBy < (int)players.size()) { int tax = (int)(pay * 0.2f); pay -= tax; players[p.taxBy].money += tax; }
        p.roundMoney = (int)roundf(pay); p.money += p.roundMoney; p.money = std::max(0, p.money);
        p.best.push_back(p.roundBirds);
    }
    Emit(EV_TALLY, {0, 0, 0});
}
void World::BeginInter() {
    phase = PH_INTER; phaseT = 0; dog.state = 0;
    for (auto& p : players) {
        p.boughtThisInter = false; p.mysteryFree = M().mystery; p.room = 1; p.botPlanned = false; if (p.dogFirst == 2) p.dogFirst = 0;
        // tonight's deal from Mr. Zappa: one gun or attachment at 30% off, shown only to you
        p.dealIsAtt = Rand() < 0.4f;
        if (p.dealIsAtt) p.deal = RandI((int)D().atts.size()); else { int g; do g = RandI((int)D().guns.size()); while (D().guns[g].price <= 0); p.deal = g; }
        // the hunt's sabotage wears off
        p.active.clear(); p.beesT = p.smudgeT = p.reverseT = p.pepperT = p.repelT = p.cardboardT = p.gunDropT = p.jamT = 0; p.bagpipe = false; p.grudge = false; p.taxBy = -1; p.rubberLeft = 0; p.dropsLeft = 0;
        if (p.swapWith >= 0) {   // (the Swap Meet: the guns go back)
            Player& o = players[p.swapWith]; std::swap(p.guns[0], o.guns[0]); p.swapWith = -1;
        }
    }
}
void World::EndMatch() {
    phase = PH_OVER; phaseT = 0;
    for (auto& p : players) { p.money -= std::min(p.money, p.grudgeMoney); }
    // the winner: most birds (accuracy breaks ties), or money in High Roller; Teams: the team total
    int best = -1; float bs = -1;
    for (const auto& p : players) { float s = Score(p) + (p.shots ? (float)p.hits / p.shots : 0) * 0.5f; if (s > bs) { bs = s; best = p.id; } }
    champion = best;
    if (M().teams) { int t[2] = {0, 0}; for (const auto& p : players) t[p.team & 1] += Score(p); int wt = t[0] >= t[1] ? 0 : 1; for (const auto& p : players) if ((p.team & 1) == wt) { champion = p.id; break; } }
}

// ---------------------------------------------------------------- birds
Bird& World::SpawnBird(int def, Vector3 at, int wv) {
    Bird b; b.def = def; b.id = nextBirdId++; b.wave = wv; b.p = at; b.seed = Rand() * 1000; const BirdDef& B = D().birds[def];
    b.hp = (float)B.hp; b.life = B.life * (0.85f + 0.3f * Rand()); b.golden = B.id == "golden";
    float s = B.speed * (B.clay ? 1 : Br().speed);
    // the first heading: up and away, off to one side
    float side = Rand() < 0.5f ? -1.0f : 1.0f;
    Vector3 to{at.x + side * (8 + 20 * Rand()), (B.pattern == "high" ? 28.0f : B.pattern == "skim" ? 1.6f : B.pattern == "ground" ? 0.4f : 10 + 12 * Rand()), at.z + 50 + 60 * Rand()};
    b.v = Vector3Scale(Vector3Normalize(Vector3Subtract(to, at)), s);
    if (B.pattern == "skim") b.v = {side * s, 0.3f, s * 0.35f};
    if (B.pattern == "ground") b.v = {side * s, 0, 0.6f};
    if (B.pattern == "ufo") { b.p = {at.x, 26, 60}; b.v = {side * s, 0, 0}; }
    if (!B.decoy && !B.clay && wv >= 0 && wv < (int)waveBirds.size()) waveBirds[wv] += 1;
    birds.push_back(b);
    return birds.back();
}
void World::SpawnWave() {
    const Bracket& br = Br(); int wv = wave; std::vector<int> pool = br.birds; if (pool.empty()) pool.push_back(0);
    Emit(EV_WAVE, {0, 0, 12}, -1, -1, (float)wv);
    dog.state = 1; dog.t = 0;
    int slots = br.perWave;
    // a wave's specials: the golden duck once a round (from 4), the phoenix now and then (from 10), the UFO once (round 15)
    if (!goldenDone && (wv == wavesThisRound / 2 || (Rand() < 0.2f && wv > 0))) { int g = BirdIndex("golden"); if (g >= 0) { SpawnBird(g, {(Rand() - 0.5f) * 100, 0.3f, 12 + 6 * Rand()}, wv); slots--; goldenDone = true; } }
    for (auto& p : players) if (p.dogFirst == 1 && wv == 1) { int g = BirdIndex("golden"); if (g >= 0) SpawnBird(g, {StallPos(p.stall).x * 10, 0.3f, 12}, wv); p.dogFirst = 2; }   // (the Dog's Bone: it sniffs one out on your side)
    if (round >= roundsTotal && !ufoDone && wv == 2) { int u = BirdIndex("ufo"); if (u >= 0) { SpawnBird(u, {(Rand() - 0.5f) * 60, 26, 60}, wv); ufoDone = true; Emit(EV_UFO, {0, 26, 60}); } }
    for (int k = 0; k < std::max(1, slots); k++) {
        int def = pool[RandI((int)pool.size())]; const BirdDef& B = D().birds[def];
        if (B.rare < 1 && Rand() > B.rare * 0.5f) def = pool[0];
        if (B.id == "decoy" && Rand() < 0.5f) def = pool[RandI(2)];   // (decoys come in a flock now and then, not every slot)
        const BirdDef& Bd = D().birds[def];
        Vector3 at{(Rand() - 0.5f) * 120, 0.3f, 10 + 8 * Rand()};
        if (Bd.flock > 0 || Bd.id == "decoy") {   // a flock (pigeons, or a decoy flock)
            int n = Bd.flock > 0 ? Bd.flock : 4; int lead = -1;
            for (int i = 0; i < n; i++) { Bird& b = SpawnBird(def, Vector3Add(at, {(Rand() - 0.5f) * 3, 0, (Rand() - 0.5f) * 2}), wv); if (lead < 0) lead = b.id; b.flock = lead; }
            continue;
        }
        if (Bd.convoy > 0) {   // a mother and her ducklings in a line
            Bird& m = SpawnBird(def, at, wv); int lead = m.id; float vx = m.v.x; m.v = {vx * 0.6f, 1.5f, 3}; int prev = lead;
            int dk = BirdIndex("duckling"); for (int i = 0; i < Bd.convoy && dk >= 0; i++) { Bird& c = SpawnBird(dk, Vector3Add(at, {-(i + 1) * 1.1f * (vx > 0 ? 1 : -1), 0, 0}), wv); c.leader = prev; prev = c.id; }
            continue;
        }
        SpawnBird(def, at, wv);
    }
    // a Decoy Rain sabotage: extra decoys over the target's half of the sky
    for (auto& p : players) for (auto& s : p.active) if (s.item >= 0 && D().sabotage[s.item].id == "decoyrain" && !s.countered) {
        int dc = BirdIndex("decoy"); if (dc < 0) break;
        for (int i = 0; i < 2; i++) { Bird& b = SpawnBird(dc, {StallPos(p.stall).x * 10 + (Rand() - 0.5f) * 20, 0.3f, 12}, wv); b.decoyRain = true; b.sideOf = p.id; }
    }
}
static Bird* Find(std::vector<Bird>& v, int id) { for (auto& b : v) if (b.id == id) return &b; return nullptr; }
void World::StepBird(Bird& b) {
    const BirdDef& B = D().birds[b.def]; float dt = STEP;
    for (int k = HIST - 1; k > 0; k--) b.hist[k] = b.hist[k - 1]; b.hist[0] = b.p; b.histN = std::min(HIST, b.histN + 1);
    b.t += dt;
    if (b.st == BI_HIT) { b.freezeT -= dt; if (b.freezeT <= 0) { b.st = BI_FALL; b.v = {0, 2, 0}; } return; }
    if (b.st == BI_FALL) {
        b.v.y -= 14 * dt; b.p = Vector3Add(b.p, Vector3Scale(b.v, dt));
        if (b.p.y <= 0.1f) { b.p.y = 0.1f; b.st = BI_GONE; b.v = {}; if (!b.retrieved) dog.queue.push_back(b.id); }
        return;
    }
    if (b.st != BI_FLY) return;
    float s = B.speed * (B.clay ? 1 : Br().speed);
    // a crab clamped on: dragged down, then the kill (the crab's owner)
    if (b.clampT > 0) { b.clampT -= dt; b.v = {b.v.x * 0.9f, -4, b.v.z * 0.9f}; b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); if (b.clampT <= 0 || b.p.y < 0.5f) KillBird(b, b.clampBy, GunIndex("crab"), false); return; }
    if (b.bubbleT > 0) { b.bubbleT -= dt; b.v = {0, 1.5f, 0}; b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); if (b.bubbleT <= 0) { b.escaped = true; b.st = BI_GONE; Emit(EV_ESCAPE, b.p); } return; }
    if (b.abductT > 0) { b.abductT -= dt; b.v = {0, 6, 0}; b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); if (b.abductT <= 0) { b.st = BI_GONE; b.escaped = true; } return; }
    Vector3 want = b.v; float wl = Vector3Length(want); if (wl < 0.01f) want = {0, 0, 1};
    // escaping: time's up, off over the far edge
    bool leaving = b.t > b.life;
    if (leaving) want = Vector3Scale(Vector3Normalize({b.v.x * 0.3f, 0.5f, 1}), s * 1.6f);
    else if (B.pattern == "zigzag") { float flip = (b.t > 1.4f) + (b.t > 2.8f); float side = (fmodf(b.seed, 2) < 1 ? 1.0f : -1.0f) * (((int)flip % 2) ? -1.0f : 1.0f); want = {side * s * 0.7f, b.p.y < 14 ? s * 0.5f : 0.2f, s * 0.5f}; }
    else if (B.pattern == "skim") { want = {b.v.x, (1.6f - b.p.y) * 2, s * 0.35f}; }
    else if (B.pattern == "high") { want = {b.v.x, (28 - b.p.y) * 0.8f, s * 0.6f}; }
    else if (B.pattern == "erratic") { if (fmodf(b.t + b.seed, 0.35f) < dt) { float a = Rand() * 2 * PI; b.seed += 0.13f; want = {cosf(a) * s, (b.p.y < 6 ? 0.8f : (Rand() - 0.3f)) * s * 0.6f, (0.3f + 0.7f * Rand()) * s}; } }
    else if (B.pattern == "flock") { Bird* L = b.flock >= 0 && b.flock != b.id ? Find(birds, b.flock) : nullptr; if (L && L->st == BI_FLY && b.flock != -2) { Vector3 off{sinf(b.seed) * 1.5f, cosf(b.seed * 1.3f) * 0.8f, cosf(b.seed) * 1.5f}; want = Vector3Add(L->v, Vector3Scale(Vector3Subtract(Vector3Add(L->p, off), b.p), 3)); } else if (b.p.y < 12) want.y = s * 0.5f; }
    else if (B.pattern == "dive") {   // a crow: up, then down at someone on the porch (it can knock a hat off), then away
        if (b.t < 1.5f) want = {b.v.x * 0.5f, s * 0.7f, s * 0.3f};
        else if (b.t < 4.0f) { int tgt = (int)b.seed % std::max(1, (int)players.size()); Vector3 head = players[tgt].Eye(); want = Vector3Scale(Vector3Normalize(Vector3Subtract(head, b.p)), s * 1.3f); if (Vector3Distance(b.p, head) < 1.4f) { if (!players[tgt].hatOff && players[tgt].hat >= 0) { players[tgt].hatOff = true; Emit(EV_HATOFF, head, tgt); } b.t = 4.0f; } }
        else want = {b.v.x, s * 0.6f, s * 0.8f};
    }
    else if (B.pattern == "glide") {
        if (b.angryT > 0 && b.angryAt >= 0) { b.angryT -= dt; Vector3 to = players[b.angryAt].Eye(); want = Vector3Scale(Vector3Normalize(Vector3Subtract(to, b.p)), s * 2.2f); if (Vector3Distance(to, b.p) < 1.6f) { players[b.angryAt].jamT = 3; Emit(EV_JAM, to, b.angryAt); b.angryT = 0; b.life = b.t + 2; } }
        else want = {b.v.x, (16 - b.p.y) * 0.5f, s * 0.5f};
    }
    else if (B.pattern == "convoy") { want = {b.v.x, (6 - b.p.y) * 0.8f, s * 0.35f}; }
    else if (B.pattern == "follow") { Bird* L = b.leader >= 0 ? Find(birds, b.leader) : nullptr; if (L && L->st == BI_FLY) { Vector3 back = Vector3Normalize(L->v); want = Vector3Scale(Vector3Subtract(Vector3Subtract(L->p, Vector3Scale(back, 1.1f)), b.p), 5); } else { b.leader = -1; if (fmodf(b.t + b.seed, 0.4f) < dt) { float a = Rand() * 2 * PI; want = {cosf(a) * s * 1.4f, s * 0.5f, s}; } } }
    else if (B.pattern == "loop") { float a = b.t * 1.6f + b.seed; want = {cosf(a) * s, sinf(a) * s * 0.6f + (12 - b.p.y) * 0.3f, s * 0.45f}; }
    else if (B.pattern == "arc") { b.v.y -= 6 * dt; b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); if (b.p.y < 0 || b.t > b.life) { b.st = BI_GONE; b.escaped = true; } return; }
    else if (B.pattern == "ground") { want = {b.v.x, 0, 0.4f}; b.p.y = 0.4f; if (fabsf(b.p.x) > 70) b.v.x = -b.v.x; }
    else if (B.pattern == "ufo") {   // hovers and abducts: the nearest ducks rise into it every few seconds
        want = {b.v.x, (26 - b.p.y), (60 - b.p.z)}; if (fabsf(b.p.x) > 60) b.v.x = -b.v.x;
        if (fmodf(b.t, 5.0f) < dt && b.t > 2) for (auto& o : birds) if (o.st == BI_FLY && &o != &b && o.abductT <= 0 && !D().birds[o.def].metal && Vector3Distance(o.p, b.p) < 30) { o.abductT = 2.5f; o.flock = -2; o.angryAt = b.id; }
    }
    // the bracket's jitter, and the effects that steer a bird
    float er = Br().erratic; if (er > 0 && B.pattern != "ufo") { want.x += sinf(b.t * 5.3f + b.seed) * s * er * 0.5f; want.y += cosf(b.t * 4.1f + b.seed * 2) * s * er * 0.3f; }
    if (b.breadT > 0 && b.breadBy >= 0) { b.breadT -= dt; Vector3 to = Vector3Add(players[b.breadBy].Eye(), {0, 4, 8}); want = Vector3Scale(Vector3Normalize(Vector3Subtract(to, b.p)), 3.0f); }
    if (b.honkT > 0 && b.honkTo >= 0) { b.honkT -= dt; Vector3 to = Vector3Add(players[b.honkTo].Eye(), {0, B.id == "swan" ? 0.0f : 6.0f, B.id == "swan" ? 0.0f : 10.0f}); want = Vector3Scale(Vector3Normalize(Vector3Subtract(to, b.p)), B.id == "swan" ? s * 2 : 4.0f); if (B.id == "swan" && Vector3Distance(to, b.p) < 1.6f) { players[b.honkTo].jamT = 3; Emit(EV_JAM, to, b.honkTo); b.honkT = 0; } }
    if (b.plungers > 0) { want = Vector3Scale(want, 0.5f); want.x += sinf(b.t * 9 + b.seed) * 3; want.y += cosf(b.t * 7) * 2; }
    if (b.blowT > 0) { b.blowT -= dt; want.z -= 8; b.life += dt; }
    for (const auto& p : players) if (p.repelT > 0) { float sx = StallPos(p.stall).x * 10; if (fabsf(b.p.x - sx) < 16) want.x += (b.p.x > sx ? 1 : -1) * s; }
    // steer (a smooth turn), fly, keep inside the field
    b.v = Vector3Lerp(b.v, want, std::min(1.0f, dt * 3.0f));
    b.p = Vector3Add(b.p, Vector3Scale(b.v, dt));
    if (b.p.y < 0.4f && B.pattern != "ground") { b.p.y = 0.4f; b.v.y = fabsf(b.v.y); }
    if (fabsf(b.p.x) > 100) { b.v.x = -b.v.x; b.p.x = std::clamp(b.p.x, -100.0f, 100.0f); }
    if (b.p.z < 4 && !(b.honkT > 0 || b.angryT > 0 || B.pattern == "dive")) { b.p.z = 4; b.v.z = fabsf(b.v.z); }
    if (b.p.z > 160 || b.p.y > 70 || b.t > b.life + 8) {
        b.st = BI_GONE; b.escaped = true; Emit(EV_ESCAPE, b.p, -1, -1, (float)b.def);
    }
}
void World::KillBird(Bird& b, int by, int gun, bool freeze) {
    if (b.st != BI_FLY) return;
    const BirdDef& B = D().birds[b.def];
    b.st = freeze ? BI_HIT : BI_FALL; b.freezeT = 0.35f; b.killer = by; b.gunOf = gun; if (!freeze) b.v = {0, -2, 0};
    if (by >= 0 && by < (int)players.size()) {
        Player& p = players[by]; float mul = 1;
        if (gun >= 0) for (const auto& g : p.guns) if (g.def == gun) mul = g.MoneyMul();
        p.birds += B.counts; p.roundBirds += B.counts; p.roundPay += B.pays * mul;
        if (B.decoy) Emit(EV_DECOY, b.p, by);
        if (B.boo) Emit(EV_BOO, b.p, by);
        if (B.id == "ufo") { p.ufoKills++; for (auto& o : birds) if (o.abductT > 0 && o.st == BI_FLY) { o.abductT = 0; KillBird(o, by, gun); Emit(EV_FREE, o.p, by); } }
        if (gun >= 0 && gun == p.mostExpensive) p.mostExpensiveBirds += B.counts;
        if (!B.decoy && !B.clay && b.wave >= 0 && b.wave < (int)waveKills.size()) { waveKills[b.wave]++; int& c = waveCredit[b.wave]; c = c == -2 ? by : (c == by ? by : -1); }
    }
    Emit(EV_KILL, b.p, by, gun, (float)b.def);
    // the flock scatters; the convoy breaks up; the phoenix bursts into fire birds
    for (auto& o : birds) if (o.st == BI_FLY && ((b.flock >= 0 && o.flock == b.flock) || o.leader == b.id)) { o.flock = -1; o.leader = -1; o.v = Vector3Add(o.v, {(Rand() - 0.5f) * 10, 4, (Rand() - 0.2f) * 6}); }
    if (B.split > 0) { int fb = BirdIndex("firebird"); for (int i = 0; i < B.split && fb >= 0; i++) { Bird& f = SpawnBird(fb, b.p, b.wave); f.v = {cosf(i * 2.1f) * 10, 6, sinf(i * 2.1f) * 6 + 4}; } }
}
void World::DamageBird(Bird& b, int dmg, int by, int gun, Vector3 at) {
    if (b.st != BI_FLY) return;
    const BirdDef& B = D().birds[b.def];
    if (b.bubbleT > 0) { b.bubbleT = 0; KillBird(b, by, gun); return; }   // (a bubble pops for anyone)
    if (B.armor && at.y > b.p.y - B.r * 0.3f) { Emit(EV_ARMOR, at, by); return; }   // (pings off the helmet: the belly is the weak point)
    if (B.angry && b.hp - dmg > 0 && by >= 0) { b.angryT = 4; b.angryAt = by; }
    b.hp -= dmg; Emit(EV_HIT, at, by, gun, (float)b.def);
    if (b.hp <= 0) KillBird(b, by, gun);
}
void World::StepBirds() {
    for (size_t i = 0; i < birds.size(); i++) StepBird(birds[i]);
    // a wave resolved: everyone killed by one player is a perfect wave; everyone escaped and the dog laughs
    for (int wv = 0; wv < (int)waveBirds.size(); wv++) {
        if (waveBirds[wv] <= 0 || waveCredit[wv] == -3) continue;
        int open = 0, escaped = 0; for (const auto& b : birds) if (b.wave == wv && !D().birds[b.def].decoy && !D().birds[b.def].clay) { if (b.st == BI_FLY) open++; else if (b.escaped && b.killer < 0) escaped++; }
        if (open) continue;
        if (waveKills[wv] >= waveBirds[wv] && waveCredit[wv] >= 0) { Player& p = players[waveCredit[wv]]; p.roundPay += D().perfectBonus; p.perfect++; Emit(EV_PERFECT, {0, 10, 30}, p.id); }
        else if (waveKills[wv] == 0 && escaped > 0) { dog.state = 3; dog.t = 0; dog.laughs++; Emit(EV_DOG_LAUGH, dog.p); }
        waveCredit[wv] = -3;
    }
}

// ---------------------------------------------------------------- the dog
void World::StepDog() {
    Dog& d = dog; d.t += STEP;
    if (d.state == 1) { float k = std::min(1.0f, d.t / 1.2f); d.p = {sinf(round * 1.7f + wave) * 25 * k, k < 0.8f ? 0 : sinf((k - 0.8f) * 5 * PI) * 1.2f, 6 + 6 * k}; if (d.t > 1.6f) { d.state = d.queue.empty() ? 0 : 2; d.t = 0; } }
    else if (d.state == 2) {   // retrieving: swim out to the next bird in the order (the Bone's owner first, the dog-shooter last)
        if (d.holding < 0) {
            if (d.queue.empty()) { d.state = 0; return; }
            int pick = 0, bestRank = 99;
            for (int i = 0; i < (int)d.queue.size(); i++) { Bird* b = Find(birds, d.queue[i]); if (!b) continue; int k = b->killer; int rank = 5; if (k >= 0 && k < (int)players.size()) { if (players[k].dogFirst > 0) rank = 0; if (players[k].dogShot) rank = 9; if (players[k].grudge && !players[k].dogSausage) rank = 99; } if (rank < bestRank) { bestRank = rank; pick = i; } }
            if (bestRank >= 99) { d.queue.clear(); d.state = 0; return; }   // (the Dog's Grudge: it won't fetch theirs)
            d.holding = d.queue[pick]; d.queue.erase(d.queue.begin() + pick);
        }
        Bird* b = Find(birds, d.holding); if (!b) { d.holding = -1; return; }
        Vector3 to = b->retrieved ? Vector3{d.p.x, 0, 4} : b->p; to.y = 0;
        Vector3 dd = Vector3Subtract(to, d.p); float L = Vector2Length({dd.x, dd.z});
        if (L > 0.4f) { d.p.x += dd.x / L * 9 * STEP; d.p.z += dd.z / L * 9 * STEP; }
        else if (!b->retrieved) b->retrieved = true;
        else { d.holding = -1; b->st = BI_GONE; b->p.y = -10; }
        if (b->retrieved) { b->p = Vector3Add(d.p, {0, 0.6f, 0}); }
    }
    else if (d.state == 3) { d.p = {d.p.x * 0.98f, sinf(std::min(1.0f, d.t / 0.5f) * PI * 0.5f) * 1.0f, 9}; if (d.t > 2.2f) { d.state = d.queue.empty() ? 0 : 2; d.t = 0; } }
    else if (d.state == 0) { if (!d.queue.empty() && phase == PH_HUNT) { d.state = 2; d.t = 0; } }
}

// ---------------------------------------------------------------- shooting
bool World::RayBird(Vector3 o, Vector3 d, float maxT, int rewind, int* bi, float* tHit, Vector3* at) const {
    float best = maxT; int k = -1;
    for (int i = 0; i < (int)birds.size(); i++) {
        const Bird& b = birds[i]; if (b.st != BI_FLY) continue;
        Vector3 c = b.PosAgo(rewind); float r = D().birds[b.def].r * (b.bubbleT > 0 ? 1.8f : 1.0f);
        Vector3 oc = Vector3Subtract(o, c); float bb = Vector3DotProduct(oc, d), cc = Vector3DotProduct(oc, oc) - r * r, disc = bb * bb - cc;
        if (disc < 0) continue; float t = -bb - sqrtf(disc); if (t < 0 || t >= best) continue;
        best = t; k = i;
    }
    if (k < 0) return false;
    if (bi) *bi = k; if (tHit) *tHit = best;
    if (at) { const Bird& b = birds[k]; Vector3 hit = Vector3Add(o, Vector3Scale(d, best)); *at = Vector3Add(Vector3Subtract(hit, birds[k].PosAgo(rewind)), b.p); }
    return true;
}
static Vector3 Spread(World& w, Vector3 d, float deg) {
    if (deg <= 0) return d;
    Vector3 up = fabsf(d.y) > 0.95f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    Vector3 r = Vector3Normalize(Vector3CrossProduct(d, up)), u = Vector3CrossProduct(r, d);
    float a = w.Rand() * 2 * PI, m = sqrtf(w.Rand()) * tanf(deg * DEG2RAD);
    return Vector3Normalize(Vector3Add(d, Vector3Add(Vector3Scale(r, cosf(a) * m), Vector3Scale(u, sinf(a) * m))));
}
// one bullet (or pellet) from p: the cardboard moose, the dog, other players' hats, then the birds
static void Bullet(World& w, Player& p, Vector3 dir, int dmg, bool rubber) {
    const Gun& g = p.G(); Vector3 o = p.Eye();
    // the bird-seeker sticker: bend toward the nearest bird within a few degrees
    float seek = g.Seek();
    if (seek > 0) { float bestA = seek * 3 * DEG2RAD; Vector3 to{}; for (const auto& b : w.birds) if (b.st == BI_FLY) { Vector3 v = Vector3Normalize(Vector3Subtract(b.p, o)); float a = acosf(std::clamp(Vector3DotProduct(v, dir), -1.0f, 1.0f)); if (a < bestA) { bestA = a; to = v; } } if (Vector3Length(to) > 0.5f) dir = Vector3Normalize(Vector3Lerp(dir, to, std::min(1.0f, seek * DEG2RAD / std::max(1e-4f, bestA)))); }
    // the moose in front of your stall
    if (p.cardboardT > 0) { float t = (6 - o.z) / std::max(1e-4f, dir.z); Vector3 h = Vector3Add(o, Vector3Scale(dir, t)); float sx = w.StallPos(p.stall).x; if (t > 0 && fabsf(h.x - sx) < 1.1f && h.y < 2.6f) { if (--p.cardboardHP <= 0) { p.cardboardT = 0; w.Emit(EV_COUNTER, h, p.id, -1, (float)SabIndex("cardboard")); } return; } }
    float tb = 1e9f; int bi = -1; Vector3 at{};
    bool hitBird = w.RayBird(o, dir, 250, std::clamp(p.in.lagSteps, 0, HIST), &bi, &tb, &at);
    // the dog (it squeaks, and remembers)
    if (w.dog.state != 0) { Vector3 dc = Vector3Add(w.dog.p, {0, 0.5f, 0}); Vector3 oc = Vector3Subtract(o, dc); float b2 = Vector3DotProduct(oc, dir), c2 = Vector3DotProduct(oc, oc) - 0.36f, disc = b2 * b2 - c2; if (disc > 0) { float t = -b2 - sqrtf(disc); if (t > 0 && t < tb) { p.birds = std::max(0, p.birds - 1); p.dogShot = true; w.Emit(EV_DOG_SHOT, dc, p.id); return; } } }
    // another player's hat
    for (auto& q : w.players) if (q.id != p.id && q.hat >= 0 && !q.hatOff) { Vector3 hc = Vector3Add(q.Eye(), {0, 0.22f, 0}); Vector3 oc = Vector3Subtract(o, hc); float b2 = Vector3DotProduct(oc, dir), c2 = Vector3DotProduct(oc, oc) - 0.04f, disc = b2 * b2 - c2; if (disc > 0) { float t = -b2 - sqrtf(disc); if (t > 0 && t < tb) { q.hatOff = true; w.Emit(EV_HATOFF, hc, q.id, p.id); return; } } }
    if (!hitBird) { w.Emit(EV_SHOT, Vector3Add(o, Vector3Scale(dir, std::min(tb, 120.0f))), p.id, -1, 0); return; }
    Bird& b = w.birds[bi];
    // range: past the gun's range the odds fall off; at night, past 30 m without a flare, half again
    float range = g.Range(), dist = tb, chance = dist <= range ? 1.0f : range / dist;
    if (w.M().night && w.flareT <= 0 && dist > 30) chance *= 0.5f;
    if (w.Rand() > chance) { w.Emit(EV_SHOT, at, p.id, -1, 1); return; }
    p.hits++;
    if (rubber) { w.Emit(EV_HIT, at, p.id, -2, (float)b.def); return; }   // (squeak: a rubber duck does nothing)
    if (b.plungers > 0 && D().guns[g.def].special != "plunger") {}
    w.DamageBird(b, dmg, p.id, g.def, at);
}
static Vector3 Rot(Vector3 d, float yawOff, float pitchOff) { float yaw = atan2f(d.x, d.z) + yawOff, pitch = asinf(std::clamp(d.y, -1.0f, 1.0f)) + pitchOff; return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }
void World::Fire(Player& p, bool both) {
    Gun& g = p.G(); const GunDef& G = D().guns[g.def];
    p.shots++; g.shotN++;
    Vector3 dir = p.Look();
    // the sabotage on your aim: bees sway it, the ghost pepper wobbles it
    if (p.beesT > 0) dir = Rot(dir, sinf(t * 7.3f + p.id) * 3.5f * DEG2RAD, cosf(t * 5.9f) * 2.5f * DEG2RAD);
    if (p.pepperT > 0) dir = Rot(dir, sinf(t * 3.1f) * 1.5f * DEG2RAD, cosf(t * 2.7f) * 1.5f * DEG2RAD);
    if (G.special == "akimbo") dir = Rot(dir, (g.shotN % 2 ? 0.6f : -0.6f) * DEG2RAD, 0);
    bool rubber = p.rubberLeft > 0; if (rubber) p.rubberLeft--;
    float spread = g.Spread(); int pellets = G.pellets * (both && G.special == "both" ? 2 : 1);
    Emit(EV_FLASH, p.Eye(), p.id, g.def, (float)pellets);
    const std::string& sp = G.special;
    auto proj = [&](uint8_t kind, float speed) { Proj q; q.kind = kind; q.owner = p.id; q.p = Vector3Add(p.Eye(), Vector3Scale(dir, 0.8f)); q.v = Vector3Scale(Spread(*this, dir, spread), speed); projs.push_back(q); };
    if (rubber && (sp == "crab" || sp == "beam" || sp == "net" || sp == "firework" || sp == "banana" || sp == "cone" || sp == "chain")) { Emit(EV_SHOT, p.Eye(), p.id, -1, 2); return; }
    if (sp == "crab") { proj(PJ_CRAB, G.speed); return; }
    if (sp == "beam") { proj(PJ_BEAM, G.speed); return; }
    if (sp == "net") { proj(PJ_NET, G.speed); return; }
    if (sp == "bread") { proj(PJ_BREAD, G.speed); return; }
    if (sp == "boomerang") { proj(PJ_BOOMERANG, G.speed); projs.back().state = 0; return; }
    if (sp == "bubble") { proj(PJ_BUBBLE, G.speed); return; }
    if (sp == "firework") { proj(PJ_FIREWORK, G.speed); return; }
    if (sp == "plunger") { proj(PJ_PLUNGER, G.speed); return; }
    if (sp == "banana") { proj(PJ_BANANA, G.speed); return; }
    if (sp == "flare") { proj(PJ_FLARE, 45); Bullet(*this, p, dir, G.damage, rubber); return; }
    if (sp == "honker") { for (auto& b : birds) if (b.st == BI_FLY) { const std::string& id = D().birds[b.def].id; if (id == "goose" || id == "swan") { b.honkT = 3; b.honkTo = p.id; } } Emit(EV_HONK, p.Eye(), p.id); return; }
    if (sp == "magnet" || sp == "blower") {   // a continuous cone: metal yanked to the rail; birds blown back toward the porch (and hats off)
        for (auto& b : birds) if (b.st == BI_FLY) {
            Vector3 v = Vector3Subtract(b.p, p.Eye()); float L = Vector3Length(v); if (L > G.range) continue;
            if (Vector3DotProduct(Vector3Scale(v, 1 / L), dir) < cosf((sp == "magnet" ? 6.0f : 14.0f) * DEG2RAD)) continue;
            if (sp == "magnet" && D().birds[b.def].metal) { b.v = Vector3Scale(Vector3Normalize(Vector3Subtract(p.Eye(), b.p)), 14); b.p = Vector3Add(b.p, Vector3Scale(b.v, STEP)); b.life += STEP; if (L < 6.5f && D().birds[b.def].id != "ufo") KillBird(b, p.id, g.def); }
            if (sp == "blower") b.blowT = 0.3f;
        }
        if (sp == "blower") for (auto& q : players) if (q.id != p.id && !q.hatOff && q.hat >= 0) { Vector3 v = Vector3Subtract(q.Eye(), p.Eye()); float L = Vector3Length(v); if (L < 12 && Vector3DotProduct(Vector3Scale(v, 1 / L), dir) > 0.9f) { q.hatOff = true; Emit(EV_HATOFF, q.Eye(), q.id, p.id); } }
        p.shots--;   // (a held stream isn't a shot for accuracy)
        return;
    }
    if (sp == "cone") {   // the Mega Zapper: everything in a wide cone; everyone's screen flashes
        for (auto& b : birds) if (b.st == BI_FLY) { Vector3 v = Vector3Subtract(b.p, p.Eye()); float L = Vector3Length(v); if (L < G.range && Vector3DotProduct(Vector3Scale(v, 1 / L), dir) > cosf(25 * DEG2RAD)) { if (D().birds[b.def].id == "ufo") DamageBird(b, 6, p.id, g.def, b.p); else KillBird(b, p.id, g.def); p.hits++; } }
        Emit(EV_FLASH, p.Eye(), -1, g.def, 99);
        return;
    }
    if (sp == "chain") {   // a bolt, then on to the nearest other bird, up to five times
        int bi; float tb; Vector3 at; Vector3 o = p.Eye();
        if (RayBird(o, Spread(*this, dir, spread), 250, std::clamp(p.in.lagSteps, 0, HIST), &bi, &tb, &at)) {
            std::vector<int> hit; int cur = bi; p.hits++;
            for (int k = 0; k < 6 && cur >= 0; k++) {
                Bird& b = birds[cur]; hit.push_back(b.id); Vector3 from = b.p;
                Emit(EV_CHAIN, from, p.id, k, 0);
                DamageBird(b, G.damage, p.id, g.def, from);
                int nx = -1; float nd = 12; for (int i = 0; i < (int)birds.size(); i++) if (birds[i].st == BI_FLY && std::find(hit.begin(), hit.end(), birds[i].id) == hit.end()) { float d = Vector3Distance(birds[i].p, from); if (d < nd) { nd = d; nx = i; } }
                cur = nx;
            }
        } else Emit(EV_SHOT, Vector3Add(o, Vector3Scale(dir, 80)), p.id, -1, 0);
        return;
    }
    int dmg = rubber ? 0 : G.damage;
    for (int k = 0; k < pellets; k++) Bullet(*this, p, Spread(*this, dir, spread), dmg, rubber);
    if (sp == "akimbo") {}
}

// ---------------------------------------------------------------- projectiles (the fun guns)
void World::StepProjs() {
    float dt = STEP;
    for (size_t i = 0; i < projs.size(); i++) {
        Proj& q = projs[i]; if (!q.alive) continue; q.t += dt;
        Player* ow = q.owner >= 0 && q.owner < (int)players.size() ? &players[q.owner] : nullptr;
        int gun = -1; if (ow) gun = ow->G().def;
        auto nearBird = [&](float r, bool small) { for (int k = 0; k < (int)birds.size(); k++) { Bird& b = birds[k]; if (b.st != BI_FLY) continue; if (small && D().birds[b.def].r > 0.5f) continue; if (std::find(q.hit.begin(), q.hit.end(), b.id) != q.hit.end()) continue; if (Vector3Distance(b.p, q.p) < r + D().birds[b.def].r) return k; } return -1; };
        float grav = q.kind == PJ_CRAB || q.kind == PJ_NET || q.kind == PJ_BREAD || q.kind == PJ_BANANA || q.kind == PJ_BANANA_BIT || q.kind == PJ_FLARE ? 3.0f : 0.0f;
        if (q.kind == PJ_BOOMERANG) {   // out, then back to the stall, hitting everything on the way both ways
            if (q.state == 0 && q.t > 0.85f) q.state = 1;
            if (q.state == 1 && ow) { Vector3 to = ow->Eye(); Vector3 v = Vector3Subtract(to, q.p); float L = Vector3Length(v); q.v = Vector3Lerp(q.v, Vector3Scale(v, 32 / std::max(0.1f, L)), std::min(1.0f, dt * 4)); if (L < 1.4f) { q.alive = false; Gun& g = ow->G(); bool facing = Vector3DotProduct(ow->Look(), Vector3Normalize(Vector3Scale(v, -1))) > 0.3f; if (g.Has() && D().guns[g.def].special == "boomerang") { if (facing) { g.ammo = 1; g.reloadT = 0; } else g.reloadT = 4; } } }
            if (q.t > 4) q.alive = false;
        }
        q.v.y -= grav * dt;
        q.p = Vector3Add(q.p, Vector3Scale(q.v, dt));
        if (q.p.y < 0 || q.p.z > 170 || fabsf(q.p.x) > 120 || q.t > 6) {
            if (q.kind == PJ_CRAB && q.p.y < 0) Emit(EV_SHOT, q.p, q.owner, -1, 3);   // (it lands in the marsh and walks back)
            q.alive = false; continue;
        }
        switch (q.kind) {
            case PJ_CRAB: { int k = nearBird(0.3f, false); if (k >= 0) { Bird& b = birds[k]; if (D().birds[b.def].id == "ufo") { DamageBird(b, 4, q.owner, gun, b.p); } else { b.clampT = 1; b.clampBy = q.owner; } q.alive = false; if (ow) ow->hits++; } break; }
            case PJ_BEAM: { for (int k = 0; k < (int)birds.size(); k++) { Bird& b = birds[k]; if (b.st == BI_FLY && Vector3Distance(b.p, q.p) < 1.4f + D().birds[b.def].r) { if (D().birds[b.def].id == "ufo") DamageBird(b, 1, q.owner, gun, b.p); else { KillBird(b, q.owner, gun); if (ow) ow->hits++; } } } if (q.t > 4) q.alive = false; break; }
            case PJ_NET: { int k = nearBird(2.0f, false); if (k >= 0) { int caught = 0; for (auto& b : birds) if (b.st == BI_FLY && caught < 3 && D().birds[b.def].id != "ufo" && Vector3Distance(b.p, q.p) < 3.5f) { KillBird(b, q.owner, gun, false); caught++; } if (ow && caught) ow->hits++; q.alive = false; } break; }
            case PJ_BREAD: { int k = nearBird(1.2f, false); if (k >= 0) { birds[k].breadT = 3; birds[k].breadBy = q.owner; q.alive = false; } break; }
            case PJ_BOOMERANG: { int k = nearBird(0.8f, false); if (k >= 0) { q.hit.push_back(birds[k].id); DamageBird(birds[k], 3, q.owner, gun, birds[k].p); if (ow) ow->hits++; } if (q.state == 1 && !q.hit.empty() && q.t > 0.9f && q.hit.size() > 6) q.hit.erase(q.hit.begin()); break; }
            case PJ_BUBBLE: { int k = nearBird(0.6f, false); if (k >= 0) { Bird& b = birds[k]; if (D().birds[b.def].id != "ufo") { b.bubbleT = 4; b.bubbleBy = q.owner; } q.alive = false; } break; }
            case PJ_FIREWORK: { if (q.t > 1.2f || q.p.y > 30) { int kills = 0; std::vector<int> cand; for (int k = 0; k < (int)birds.size(); k++) if (birds[k].st == BI_FLY && D().birds[birds[k].def].id != "ufo" && Vector3Distance(birds[k].p, q.p) < 40) cand.push_back(k); for (int n = 0; n < 3 && !cand.empty(); n++) { int c = RandI((int)cand.size()); if (Rand() < 0.6f) { KillBird(birds[cand[c]], q.owner, gun); kills++; } cand.erase(cand.begin() + c); } if (ow && kills) ow->hits++; Emit(EV_FLARE, q.p, q.owner, -1, 1); q.alive = false; } break; }
            case PJ_PLUNGER: { int k = nearBird(0.4f, false); if (k >= 0) { Bird& b = birds[k]; b.plungers++; if (b.plungers >= 2) KillBird(b, q.owner, gun); else b.plungeBy = q.owner; if (ow) ow->hits++; q.alive = false; } break; }
            case PJ_BANANA: { if (q.t > 0.6f) { for (int n = 0; n < 5; n++) { Proj s; s.kind = PJ_BANANA_BIT; s.owner = q.owner; s.p = q.p; float a = (n - 2) * 0.12f; s.v = Vector3Add(Rot(Vector3Normalize(q.v), a, (n % 2) * 0.05f), {0, 0, 0}); s.v = Vector3Scale(s.v, Vector3Length(q.v)); projs.push_back(s); } q.alive = false; } break; }
            case PJ_BANANA_BIT: { int k = nearBird(0.9f, false); if (k >= 0) { Bird& b = birds[k]; if (D().birds[b.def].r <= 0.5f) KillBird(b, q.owner, gun); else DamageBird(b, 2, q.owner, gun, b.p); if (ow) ow->hits++; q.alive = false; } break; }
            case PJ_FLARE: { if (q.t > 0.9f) { flareT = 9; Emit(EV_FLARE, q.p, q.owner); q.alive = false; } break; }
        }
    }
    projs.erase(std::remove_if(projs.begin(), projs.end(), [](const Proj& q) { return !q.alive; }), projs.end());
}

// ---------------------------------------------------------------- the players
void World::StepGun(Player& p) {
    Gun& g = p.G(); if (!g.Has()) return; const GunDef& G = D().guns[g.def]; const Input& in = p.in;
    g.cool = std::max(0.0f, g.cool - STEP);
    if (p.jamT > 0 || p.gunDropT > 0) { g.spin = 0; return; }
    if (g.reloadT > 0) { g.reloadT -= STEP; if (g.reloadT <= 0) { int need = g.MagSize() - g.ammo; if (G.reserve > 0 || G.ammoPrice > 0) { int take = std::min(need, g.reserve); g.ammo += take; g.reserve -= take; } else g.ammo = g.MagSize(); } return; }
    bool firePhase = phase == PH_HUNT || phase == PH_BONUS;
    if (in.reload && g.ammo < g.MagSize()) { g.reloadT = g.ReloadTime(); p.rubberLeft = 0; Emit(EV_RELOAD, p.Eye(), p.id, g.def); return; }   // (reloading early also throws out the rubber ducks)
    if (!firePhase) return;
    // the gatling spins up for a second before it fires
    if (G.special == "spinup") { g.spin = in.fire ? std::min(1.0f, g.spin + STEP) : std::max(0.0f, g.spin - STEP * 2); if (g.spin < 1) return; }
    bool want = G.autoFire ? in.fire : (in.fire && !p.lastFire);
    if (!want || g.cool > 0) return;
    if (g.ammo <= 0) { if (G.reserve > 0 || G.ammoPrice > 0) { if (g.reserve > 0) { g.reloadT = g.ReloadTime(); Emit(EV_RELOAD, p.Eye(), p.id, g.def); } } else { g.reloadT = g.ReloadTime(); Emit(EV_RELOAD, p.Eye(), p.id, g.def); } return; }
    bool both = in.alt && G.special == "both" && g.ammo >= 2;
    Fire(p, both);
    g.ammo -= both ? 2 : 1; g.cool = g.Interval();
    if (g.ammo <= 0 && G.reserve == 0 && G.ammoPrice == 0) { g.reloadT = g.ReloadTime(); Emit(EV_RELOAD, p.Eye(), p.id, g.def); }
}
void World::StepPlayer(Player& p) {
    const Input& in = p.in; float dt = STEP;
    p.yaw = in.yaw; p.pitch = std::clamp(in.pitch, -1.2f, 1.45f);
    p.crouchK += ((in.crouch ? 1.0f : 0.0f) - p.crouchK) * std::min(1.0f, dt * 10);
    p.lean += (std::clamp(in.lean, -1.0f, 1.0f) - p.lean) * std::min(1.0f, dt * 10);
    if (p.hatOff) p.hatOffT += dt;
    Gun& g = p.G();
    bool pinned = g.Has() && in.fire && (D().guns[g.def].special == "pinned" || D().guns[g.def].special == "bipod");
    if (g.Has() && D().guns[g.def].special == "bipod" && p.room == 0) p.yaw = std::clamp(p.yaw, -PI / 4, PI / 4);   // (the belt-fed sits on the rail: 90 degrees of sky)
    // moving: in a hunt only inside your stall; in the intermission the room and the porch, through the stall gates
    float sp = p.room ? 4.5f : 1.6f; if (pinned) sp = 0;
    Vector3 np{p.pos.x + std::clamp(in.moveX, -1.0f, 1.0f) * sp * dt, 0, p.pos.z + std::clamp(in.moveZ, -1.0f, 1.0f) * sp * dt};
    if (!p.room || phase == PH_HUNT || phase == PH_BONUS || phase == PH_TALLY) { float sx = StallPos(p.stall).x; np.x = std::clamp(np.x, sx - 1.2f, sx + 1.2f); np.z = std::clamp(np.z, -0.6f, 0.6f); }
    else {
        np.x = std::clamp(np.x, -14.5f, 14.5f); np.z = std::clamp(np.z, -16.6f, 0.6f);
        // the wall between the porch and the room (z = -2), open at each stall's gate
        bool crossing = (p.pos.z > -1.7f) != (np.z > -1.7f) || (np.z < -1.7f && np.z > -2.3f);
        if (crossing) { bool gate = false; for (int s = 0; s < MAX_PLAYERS; s++) if (fabsf(np.x - StallPos(s).x) < 0.7f) gate = true; if (!gate) np.z = p.pos.z > -2 ? std::max(np.z, -1.7f) : std::min(np.z, -2.3f); }
        // the furniture: keep off the stations' counters
        for (int k = 0; k < 7; k++) { Vector3 s = Station(k); float r = k == 2 ? 0 : 0.9f; if (r <= 0) continue; Vector2 d{np.x - s.x, np.z - (s.z - 0.0f)}; float L = Vector2Length(d); if (L < r) { np.x = s.x + d.x / std::max(0.01f, L) * r; np.z = s.z + d.y / std::max(0.01f, L) * r; } }
    }
    p.pos = np;
    // swap guns, the hook
    if (in.swap && p.guns[1 - p.hand].Has()) { p.hand = 1 - p.hand; p.guns[p.hand].reloadT = 0; }
    StepGun(p); p.lastFire = in.fire;
    StepSabotage(p);
    for (auto& c : p.cmds) Command(p, c);
    p.cmds.clear();
}

// ---------------------------------------------------------------- the clock
void World::Step() {
    t += STEP; phaseT += STEP; flareT = std::max(0.0f, flareT - STEP);
    for (auto& p : players) if (p.present) StepPlayer(p);
    switch (phase) {
        case PH_HUNT: {
            if (wave < wavesThisRound && phaseT >= nextWaveT) { SpawnWave(); wave++; nextWaveT += (D().hunt - 4) / std::max(1, wavesThisRound); }
            StepBirds(); StepProjs(); StepDog();
            if (phaseT >= D().hunt) {
                for (auto& b : birds) if (b.st == BI_FLY) { b.st = BI_GONE; b.escaped = true; }
                StepBirds();
                if (D().bonusEvery > 0 && round % D().bonusEvery == 0) BeginBonus(); else BeginTally();
            }
            break;
        }
        case PH_BONUS: {   // clay pigeons from the traps at the porch's ends; everyone has the grey pistol
            int clay = BirdIndex("clay");
            if (clay >= 0 && phaseT >= nextWaveT && phaseT < D().bonusLength - 2) {
                float side = Rand() < 0.5f ? -1.0f : 1.0f; Bird& b = SpawnBird(clay, {side * 10, 1, 2}, wavesThisRound);
                b.v = {-side * (6 + 4 * Rand()), 9 + 3 * Rand(), 9 + 5 * Rand()}; nextWaveT += (D().bonusLength - 2) / std::max(1, D().bonusClays);
            }
            StepBirds(); StepProjs();
            if (phaseT >= D().bonusLength) BeginTally();
            break;
        }
        case PH_TALLY: StepDog(); if (phaseT >= D().tally) { if (round >= roundsTotal) EndMatch(); else BeginInter(); } break;
        case PH_INTER: {
            if (phaseT >= interLen - D().bell && phaseT - STEP < interLen - D().bell) Emit(EV_BELL, {0, 2, -8});
            if (phaseT >= interLen) {
                for (auto& p : players) { if (!p.boughtThisInter && !M().practice) p.money += (int)(p.money * D().patience); p.room = 0; }
                BeginHunt();
            }
            break;
        }
        default: break;
    }
}

}  // namespace fp
