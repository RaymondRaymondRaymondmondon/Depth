// Scuffle stage 9: skins and hats (doc pp. 18-20). A stick is a colour and a hat: colours are free (by seat), skins
// change the line style and the body, hats sit on the head and are their own body, so a headshot sends one flying and
// anyone bare-headed can pick it up and wear it. Tokens buy them from the shop (10 skins, 10 hats); the crate (one
// token) hatches the other 40 (15 common hats 60%, 10 rare skins 25%, 10 super rare hats 12%, 5 special skins 3%);
// a duplicate gives a banana. Nothing changes a stat. The catalogue is data (scuffle_cosmetics.json); the locker is
// scuffle_profile.txt next to the exe. The drawing is scuffle_cosart.inl.
#include "scuffle.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace sf {

namespace {
struct CosData { std::vector<Cosmetic> skins, hats; int cratePrice = 1; float odds[5] = {0, 60, 25, 12, 3}; int tMatch = 3, tRound = 1, tWon = 5, tBoss = 2, tStage = 1; };
const CosData& CD() {
    static CosData d = [] {
        CosData d; Json j = LoadJsonFile(rt::DataDir() + "/../scuffle/scuffle_cosmetics.json");
        auto read = [&](const Json& arr, bool hat, std::vector<Cosmetic>& out) { for (const Json& e : arr.a) { Cosmetic c; c.id = e["id"].Str0(); c.name = e["name"].Str0(c.id); c.look = e["look"].Str0(); c.tier = e["tier"].I(0); c.cost = e["cost"].I(0); c.hat = hat; out.push_back(c); } };
        read(j["skins"], false, d.skins); read(j["hats"], true, d.hats);
        d.cratePrice = j["crate_price"].I(1);
        const Json& o = j["odds"]; d.odds[1] = o["common"].F(60); d.odds[2] = o["rare"].F(25); d.odds[3] = o["super"].F(12); d.odds[4] = o["special"].F(3);
        const Json& t = j["tokens"]; d.tMatch = t["match"].I(3); d.tRound = t["round_won"].I(1); d.tWon = t["match_won"].I(5); d.tBoss = t["boss"].I(2); d.tStage = t["gauntlet_stage"].I(1);
        return d;
    }();
    return d;
}
const Cosmetic* Find(const std::string& id) { for (const auto& c : CD().skins) if (c.id == id) return &c; for (const auto& c : CD().hats) if (c.id == id) return &c; return nullptr; }
Locker gL; bool gLoaded = false;
const char* PATH = "scuffle_profile.txt";
void Load() {
    gLoaded = true; gL = Locker{};
    std::ifstream f(PATH); std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k;
        if (k == "tokens") s >> gL.tokens; else if (k == "crates") s >> gL.crates; else if (k == "bananas") s >> gL.bananas;
        else if (k == "matches") s >> gL.matches; else if (k == "wins") s >> gL.wins;
        else if (k == "skin") s >> gL.skin; else if (k == "hat") s >> gL.hat;
        else if (k == "own") { std::string id; while (s >> id) if (Find(id) && !gL.Owns(id)) gL.owned.push_back(id); }
    }
    if (!gL.skin.empty() && !gL.Owns(gL.skin)) gL.skin.clear();
    if (!gL.hat.empty() && !gL.Owns(gL.hat)) gL.hat.clear();
}
}
bool gLockerNoSave = false;
const std::vector<Cosmetic>& Skins() { return CD().skins; }
const std::vector<Cosmetic>& Hats() { return CD().hats; }
int SkinIndex(const std::string& id) { for (int i = 0; i < (int)Skins().size(); i++) if (Skins()[i].id == id) return i; return -1; }
int HatIndex(const std::string& id) { for (int i = 0; i < (int)Hats().size(); i++) if (Hats()[i].id == id) return i; return -1; }
const char* CosTierName(int t) { static const char* N[5] = {"Shop", "Common", "Rare", "Super Rare", "Special"}; return N[std::clamp(t, 0, 4)]; }
bool Locker::Owns(const std::string& id) const { return std::find(owned.begin(), owned.end(), id) != owned.end(); }
Locker& MyLocker() { if (!gLoaded) Load(); return gL; }
void SaveLocker() {
    if (gLockerNoSave) return;
    std::ofstream f(PATH);
    f << "tokens " << gL.tokens << "\ncrates " << gL.crates << "\nbananas " << gL.bananas << "\nmatches " << gL.matches << "\nwins " << gL.wins << "\n";
    if (!gL.skin.empty()) f << "skin " << gL.skin << "\n";
    if (!gL.hat.empty()) f << "hat " << gL.hat << "\n";
    f << "own"; for (const auto& o : gL.owned) f << " " << o; f << "\n";
}
int CratePrice() { return CD().cratePrice; }
bool BuyCosmetic(const std::string& id, std::string* why) {
    Locker& L = MyLocker(); const Cosmetic* c = Find(id);
    if (!c || c->tier != 0) { if (why) *why = "That isn't in the shop."; return false; }
    if (L.Owns(id)) { if (why) *why = "You have it already."; return false; }
    if (L.tokens < c->cost) { if (why) *why = TextFormat("It costs %d tokens; you have %d.", c->cost, L.tokens); return false; }
    L.tokens -= c->cost; L.owned.push_back(id); SaveLocker(); return true;
}
bool BuyCrate(std::string* why) {
    Locker& L = MyLocker();
    if (L.tokens < CratePrice()) { if (why) *why = TextFormat("A crate costs %d token%s.", CratePrice(), CratePrice() == 1 ? "" : "s"); return false; }
    L.tokens -= CratePrice(); L.crates++; SaveLocker(); return true;
}
CrateRoll OpenCrate(uint32_t seed) {
    Locker& L = MyLocker(); CrateRoll r;
    if (L.crates <= 0) return r;
    L.crates--; r.ok = true;
    uint32_t h = seed * 2654435761u + 0x9E3779B9u; auto R = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xFFFFFF) / 16777216.0f; };
    const CosData& d = CD(); float tot = d.odds[1] + d.odds[2] + d.odds[3] + d.odds[4], u = R() * tot; int tier = 4;
    for (int t = 1; t <= 4; t++) { if (u < d.odds[t]) { tier = t; break; } u -= d.odds[t]; }
    std::vector<const Cosmetic*> pool; for (const auto& c : d.skins) if (c.tier == tier) pool.push_back(&c); for (const auto& c : d.hats) if (c.tier == tier) pool.push_back(&c);
    if (pool.empty()) { r.banana = true; L.bananas++; SaveLocker(); return r; }
    const Cosmetic* c = pool[(size_t)(R() * pool.size()) % pool.size()];
    r.id = c->id; r.tier = tier;
    if (L.Owns(c->id)) { r.banana = true; L.bananas++; } else L.owned.push_back(c->id);
    SaveLocker(); return r;
}
bool WearCosmetic(const std::string& id) {
    Locker& L = MyLocker();
    if (id == "skin:") { L.skin.clear(); SaveLocker(); return true; }
    if (id == "hat:") { L.hat.clear(); SaveLocker(); return true; }
    const Cosmetic* c = Find(id); if (!c || !L.Owns(id)) return false;
    (c->hat ? L.hat : L.skin) = id; SaveLocker(); return true;
}
int MatchTokens(const Match& m, int me) {
    // a match played, each round won, the match won; a boss beaten, a Gauntlet stage cleared
    const CosData& d = CD(); int t = d.tMatch;
    if (me >= 0 && me < (int)m.wins.size()) t += std::min(m.wins[me], 10) * d.tRound;
    if (m.champion == me && m.mode != MD_GAUNTLET && m.mode != MD_BOSS) t += d.tWon;
    if (m.mode == MD_BOSS) t += m.gStage * d.tBoss + (m.gStage >= m.toWin ? d.tWon : 0);
    if (m.mode == MD_GAUNTLET) t += std::min(m.gStage, 20) * d.tStage;
    return t;
}

// ---------------------------------------------------------------- the hats in the world
void World::KnockHat(Stick& k, Vector2 v) {
    if (k.hat < 0) return;
    Vector2 d = Vector2Length(v) > 0.01f ? Vector2Normalize(v) : Vector2{(float)-k.face, 0};
    int i = AddThing(TH_HAT, Vector2Add(k.pt[J_HEAD].p, {0, 0.2f}), {d.x * 4.5f, 5.5f + fabsf(d.y) * 2}, 1e9f, k.id);
    things[i].weapon = k.hat; things[i].a = 0;
    k.hat = -1;
}
void World::StepHats() {
    const float dt = STEP;
    for (auto& th : things) {
        if (!th.alive || th.kind != TH_HAT) continue;
        th.t += dt;
        // it falls, spins while it flies, bounces off what it hits, and settles
        th.v.y -= gravity * dt;
        Vector2 np = Vector2Add(th.p, Vector2Scale(th.v, dt));
        if (stage.Solid((int)floorf(np.x / TILE), (int)floorf((np.y - 0.08f) / TILE))) { np.y = (floorf((np.y - 0.08f) / TILE) + 1) * TILE + 0.08f; th.v.y = fabsf(th.v.y) > 2 ? -th.v.y * 0.3f : 0; th.v.x *= 0.6f; }
        if (stage.Solid((int)floorf((np.x + (th.v.x > 0 ? 0.12f : -0.12f)) / TILE), (int)floorf(np.y / TILE))) { th.v.x = -th.v.x * 0.4f; np.x = th.p.x; }
        th.p = np; if (fabsf(th.v.x) + fabsf(th.v.y) > 0.5f) th.a += th.v.x * dt * 3;
        else th.a *= 0.9f;
        if (th.p.y < -6) { th.alive = false; continue; }
        // anyone bare-headed whose head comes near it wears it (not the one who just lost it, for a moment)
        if (th.t < 0.5f) continue;
        for (auto& k : sticks) {
            if (!k.alive || !k.present || k.hat >= 0 || (k.id == th.owner && th.t < 1.5f)) continue;
            bool head = Vector2Distance(k.pt[J_HEAD].p, th.p) < k.pt[J_HEAD].r + 0.28f;
            bool walk = fabsf(k.pt[J_PELVIS].p.x - th.p.x) < 0.35f && th.p.y > k.pos.y - 0.25f && th.p.y < k.pt[J_NECK].p.y;   // (walked over where it lies)
            if (head || walk) { k.hat = th.weapon; th.alive = false; Emit(EV_PICKUP, th.p, k.id); break; }
        }
    }
}

// ---------------------------------------------------------------- the stage-9 cosmetic checks (in --scuffle-test)
static int CF = 0;
static void CC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) CF++; }
int ScuffleCosmeticChecks() {
    CF = 0;
    printf("Scuffle: stage 9a (skins, hats, the crate)\n");
    int shopS = 0, shopH = 0, tier[5] = {};
    for (const auto& c : Skins()) { if (c.tier == 0) shopS++; else tier[c.tier]++; }
    for (const auto& c : Hats()) { if (c.tier == 0) shopH++; else tier[c.tier]++; }
    CC(shopS == 10 && shopH == 10 && tier[1] == 15 && tier[2] == 10 && tier[3] == 10 && tier[4] == 5, TextFormat("the shop's 10 skins and 10 hats, the crate's 40 (%d/%d/%d/%d)", tier[1], tier[2], tier[3], tier[4]));
    // the locker without touching the player's file
    bool was = gLockerNoSave; gLockerNoSave = true; Locker saved = MyLocker(); Locker& L = MyLocker();
    L = Locker{}; L.tokens = 60; std::string why;
    bool bought = BuyCosmetic("brass", &why), again = BuyCosmetic("brass", &why), dear = BuyCosmetic("golden", &why);
    CC(bought && !again && !dear && L.tokens == 20 && L.Owns("brass") && WearCosmetic("brass") && L.skin == "brass", "the shop: bought once, worn; too dear is refused");
    // the crate's odds over many: commons about 60%, specials about 3%; every duplicate a banana
    int n = 20000, got[5] = {}, bananas = 0; L.tokens = n * CratePrice(); L.owned.clear();
    for (int i = 0; i < n; i++) { BuyCrate(&why); CrateRoll r = OpenCrate((uint32_t)i * 7919u + 3); if (r.ok && r.tier > 0) got[r.tier]++; bananas += r.banana; }
    CC(fabsf(got[1] * 100.0f / n - 60) < 2 && fabsf(got[2] * 100.0f / n - 25) < 2 && fabsf(got[3] * 100.0f / n - 12) < 1.5f && fabsf(got[4] * 100.0f / n - 3) < 0.8f && (int)L.owned.size() == 40 && bananas == n - 40,
       TextFormat("the crate's odds: %.1f / %.1f / %.1f / %.1f%%, all 40 found, the rest bananas", got[1] * 100.0f / n, got[2] * 100.0f / n, got[3] * 100.0f / n, got[4] * 100.0f / n));
    // a match's payout
    { Match m; m.Start(4, 3, 1); m.wins = {3, 1, 0, 0}; m.champion = 0; CC(MatchTokens(m, 0) == 3 + 3 + 5 && MatchTokens(m, 2) == 3, TextFormat("a match pays %d to its winner, %d to a stick with no rounds", MatchTokens(m, 0), MatchTokens(m, 2))); }
    MyLocker() = saved; gLockerNoSave = was;
    // a headshot sends a hat flying; a bare head picks it up
    {
        World w; w.Init(StoneStage(), 2, 7); w.sticks[0].hat = HatIndex("tophat"); w.sticks[1].hat = -1;
        for (int i = 0; i < 30; i++) w.Step();
        Bullet b; b.p = Vector2Add(w.sticks[0].pt[J_HEAD].p, {-1.2f, 0}); b.v = {40, 0}; b.owner = 1; b.weapon = WeaponIndex("gaspistol"); b.dmg = 5; b.life = 1; w.bullets.push_back(b);
        for (int i = 0; i < 20; i++) w.Step();
        int loose = -1; for (int i = 0; i < (int)w.things.size(); i++) if (w.things[i].alive && w.things[i].kind == TH_HAT) loose = i;
        CC(w.sticks[0].hat < 0 && loose >= 0, "a headshot knocks the hat off");
        if (loose >= 0) { w.sticks[0].present = false; for (int i = 0; i < 360; i++) w.Step(); Vector2 at = w.things[loose].p; w.SpawnStick(w.sticks[1], {at.x - 1.5f, at.y + 0.1f}, 1); for (int i = 0; i < 240 && w.sticks[1].hat < 0; i++) { w.sticks[1].in = Input{}; w.sticks[1].in.moveX = 1; w.Step(); } }
        CC(w.sticks[1].hat == HatIndex("tophat"), TextFormat("and the other stick, walking over it, wears it (hat at %.1f,%.1f; stick at %.1f)", loose >= 0 ? w.things[loose].p.x : 0, loose >= 0 ? w.things[loose].p.y : 0, w.sticks[1].pos.x));
    }
    printf("  stage 9a: %s\n", CF ? TextFormat("%d FAILED", CF) : "all checks passed");
    return CF;
}

} // namespace sf
