// Mouthful's cosmetics (design doc pp. 16-18): skins (a palette and a pattern, plus a hat) worn per path, the token shop,
// the crate (a clam that opens), the tokens a round pays, and the arcade profile (rounds, crowns, best tier per path, the
// Blobfish King). Headless; saved to mouthful_wardrobe.txt next to the exe (gitignored).
#include "mouthful.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mf {

bool gWardrobeNoSave = false;
static Color JCol(const Json& j, bool* has) { if (!j.IsArr() || j.Size() < 3) { *has = false; return WHITE; } *has = true; return {(unsigned char)j[0].I(), (unsigned char)j[1].I(), (unsigned char)j[2].I(), 255}; }
struct SkinData { std::vector<SkinDef> skins; int cratePrice = 1; float odds[5] = {0, 4, 2.5f, 1.2f, 0.6f}; int tPerRound = 10, tPer100 = 1, tWon = 20, tFirstCrown = 30, tBlobKing = 500; };
static const SkinData& SD() {
    static SkinData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(rt::DataDir() + "/../mouthful/mouthful_skins.json");
    d.cratePrice = j["crate_price"].I(1);
    d.odds[1] = j["odds"]["common"].F(4); d.odds[2] = j["odds"]["rare"].F(2.5f); d.odds[3] = j["odds"]["super"].F(1.2f); d.odds[4] = j["odds"]["special"].F(0.6f);
    d.tPerRound = j["tokens"]["per_round"].I(10); d.tPer100 = j["tokens"]["per_100_score"].I(1); d.tWon = j["tokens"]["round_won"].I(20); d.tFirstCrown = j["tokens"]["first_crown_on_path"].I(30); d.tBlobKing = j["tokens"]["blobfish_king"].I(500);
    for (const Json& r : j["skins"].a) {
        SkinDef s;
        s.id = r["id"].Str0(); s.name = r["name"].Str0(s.id); s.note = r["note"].Str0(); s.hat = r["hat"].Str0(); s.pattern = r["pattern"].Str0(); s.special = r["special"].Str0();
        std::string t = r["tier"].Str0("common"); s.tier = t == "store" ? 0 : t == "rare" ? 2 : t == "super" ? 3 : t == "special" ? 4 : 1;
        s.price = r["price"].I(0); s.earned = r["earned"].Bool0(false);
        s.tint = JCol(r["tint"], &s.hasTint); s.glow = JCol(r["glow"], &s.hasGlow);
        d.skins.push_back(s);
    }
    return d;
}
const std::vector<SkinDef>& Skins() { return SD().skins; }
const SkinDef* FindSkin(const std::string& id) { for (const auto& s : SD().skins) if (s.id == id) return &s; return nullptr; }
const char* SkinTierName(int t) { static const char* N[5] = {"Shop", "Common", "Rare", "Super Rare", "Special"}; return N[std::clamp(t, 0, 4)]; }
int CratePrice() { return SD().cratePrice; }

bool Wardrobe::Owns(const std::string& id) const { return std::find(owned.begin(), owned.end(), id) != owned.end(); }
static Wardrobe gW; static bool gWLoaded = false;
Wardrobe& MyWardrobe() { if (!gWLoaded) LoadWardrobe(); return gW; }
void LoadWardrobe() {
    gWLoaded = true; gW = Wardrobe{};
    std::ifstream f("mouthful_wardrobe.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k;
        if (k == "tokens") s >> gW.tokens; else if (k == "crates") s >> gW.crates; else if (k == "rounds") s >> gW.rounds; else if (k == "crowns") s >> gW.crowns;
        else if (k == "blobking") { int b = 0; s >> b; gW.blobKing = b != 0; } else if (k == "crownpaths") s >> gW.crownPaths;
        else if (k == "own") { std::string id; while (s >> id) gW.owned.push_back(id); }
        else if (k == "worn") { int p = -1; std::string id; s >> p >> id; if (p >= 0 && p < P_COUNT && id != "-") gW.worn[p] = id; }
        else if (k == "best") { for (int p = 0; p < P_COUNT; p++) s >> gW.bestTier[p]; }
    }
}
void SaveWardrobe() {
    if (gWardrobeNoSave) return;
    std::ofstream f("mouthful_wardrobe.txt");
    f << "tokens " << gW.tokens << "\ncrates " << gW.crates << "\nrounds " << gW.rounds << "\ncrowns " << gW.crowns << "\nblobking " << (gW.blobKing ? 1 : 0) << "\ncrownpaths " << gW.crownPaths << "\nown";
    for (const auto& id : gW.owned) f << " " << id;
    f << "\n";
    for (int p = 0; p < P_COUNT; p++) f << "worn " << p << " " << (gW.worn[p].empty() ? "-" : gW.worn[p]) << "\n";
    f << "best"; for (int p = 0; p < P_COUNT; p++) f << " " << gW.bestTier[p]; f << "\n";
}
bool BuySkin(const std::string& id, std::string* why) {
    Wardrobe& w = MyWardrobe();
    const SkinDef* s = FindSkin(id);
    if (!s || s->tier != 0) { if (why) *why = "That isn't in the shop."; return false; }
    if (w.Owns(id)) { if (why) *why = "You have it."; return false; }
    if (w.tokens < s->price) { if (why) *why = TextFormat("It costs %d tokens.", s->price); return false; }
    w.tokens -= s->price; w.owned.push_back(id); SaveWardrobe(); return true;
}
bool BuyCrate(std::string* why) {
    Wardrobe& w = MyWardrobe();
    if (w.tokens < CratePrice()) { if (why) *why = TextFormat("A crate costs %d token%s.", CratePrice(), CratePrice() == 1 ? "" : "s"); return false; }
    w.tokens -= CratePrice(); w.crates++; SaveWardrobe(); return true;
}
CrateRoll OpenCrate(uint32_t seed) {
    // one crate: a clam that opens. Each crate skin has its tier's chance (the specials earned by deed are never in it);
    // what's left over is an empty clam. A duplicate is a pearl of no value.
    CrateRoll r;
    Wardrobe& w = MyWardrobe();
    if (w.crates <= 0) return r;
    w.crates--; r.ok = true;
    uint32_t h = seed * 2654435761u + 12345u; h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    float x = (h % 100000) / 1000.0f;   // 0..100
    for (const auto& s : SD().skins) {
        if (s.tier == 0 || s.earned) continue;
        x -= SD().odds[s.tier];
        if (x < 0) { r.id = s.id; r.tier = s.tier; break; }
    }
    if (r.id.empty()) { const auto& v = SD().skins; for (const auto& s : v) if (s.tier == 1) { r.id = s.id; r.tier = 1; break; } }   // (the odds sum short of 100: the rest is the commonest)
    if (w.Owns(r.id)) r.duplicate = true; else w.owned.push_back(r.id);
    SaveWardrobe();
    return r;
}
bool WearSkin(int path, const std::string& id) {
    Wardrobe& w = MyWardrobe();
    if (path < 0 || path >= P_COUNT) return false;
    if (!id.empty() && !w.Owns(id)) return false;
    w.worn[path] = id; SaveWardrobe(); return true;
}
std::string LookString() { const Wardrobe& w = MyWardrobe(); std::string s; for (int p = 0; p < P_COUNT; p++) { if (p) s += ";"; s += w.worn[p]; } return s; }
const SkinDef* WornSkin(const std::string& look, int path) {
    if (path < 0 || look.empty()) return nullptr;
    int k = 0; size_t a = 0;
    for (size_t i = 0; i <= look.size(); i++) if (i == look.size() || look[i] == ';') { if (k == path) return FindSkin(look.substr(a, i - a)); k++; a = i + 1; }
    return nullptr;
}
int RoundTokens(const World& w, int me, bool award) {
    // 10 a round, 1 per 100 score, 20 for a round won, 30 for a first crown on a path, 500 for the Blobfish King (doc p. 16)
    const SkinData& d = SD();
    if (me < 0 || me >= (int)w.mouths.size()) return 0;
    const Mouth& m = w.mouths[me];
    Wardrobe& wd = MyWardrobe();
    int t = d.tPerRound + (int)(ScoreOf(w, m) / 100) * d.tPer100 + (w.winner == me ? d.tWon : 0);
    int crownPath = m.path >= 0 ? m.path : m.lastPath;
    bool firstCrown = m.crownT > 0 && crownPath >= 0 && !((wd.crownPaths >> crownPath) & 1);
    if (firstCrown) t += d.tFirstCrown;
    if (m.blobKing && !wd.blobKing) t += d.tBlobKing;
    if (!award) return t;
    wd.tokens += t; wd.rounds++;
    if (m.crownT > 0) wd.crowns++;
    if (firstCrown) wd.crownPaths |= 1u << crownPath;
    if (m.blobKing) { wd.blobKing = true; if (!wd.Owns("blobfish_king")) wd.owned.push_back("blobfish_king"); }
    if (m.levTooth && !wd.Owns("leviathan_tooth")) wd.owned.push_back("leviathan_tooth");
    if (crownPath >= 0) wd.bestTier[crownPath] = std::max(wd.bestTier[crownPath], m.bestTier);
    SaveWardrobe();
    return t;
}

int RunMouthfulSkinsTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; };
    printf("Mouthful: skins and the crate\n");
    gWardrobeNoSave = true;
    int store = 0, crate[5] = {}; for (const auto& s : Skins()) { if (s.tier == 0) store++; else crate[s.tier]++; }
    check(store == 10 && crate[1] == 15 && crate[2] == 10 && crate[3] == 10 && crate[4] == 5, TextFormat("the shop's 10 and the crate's 40 (%d; %d/%d/%d/%d)", store, crate[1], crate[2], crate[3], crate[4]));
    const SkinDef* camo = FindSkin("reef_camo"); const SkinDef* pearl = FindSkin("pearl"); int camoP = camo ? camo->price : 0, pearlP = pearl ? pearl->price : 0;   // (prices from the catalogue: they were halved, 2026-10-05)
    gW = Wardrobe{}; gWLoaded = true; gW.tokens = camoP + 10;
    std::string why;
    check(camo && BuySkin("reef_camo", &why) && gW.tokens == 10 && gW.Owns("reef_camo"), TextFormat("Reef Camo for its %d tokens", camoP));
    check(pearlP > 10 && !BuySkin("pearl", &why), TextFormat("Pearl at %d is out of reach with 10", pearlP));
    check(WearSkin(P_SHARK, "reef_camo") && WornSkin(LookString(), P_SHARK) && WornSkin(LookString(), P_SHARK)->id == "reef_camo" && !WornSkin(LookString(), P_EEL), "worn on the shark path, and only there");
    // the crate's odds over many clams: commons about 60%, specials about 1.8% (three crateable at 0.6%)
    int n = 20000, tierN[5] = {}, earned = 0;
    gW.tokens = n * CratePrice();
    for (int i = 0; i < n; i++) { BuyCrate(&why); CrateRoll r = OpenCrate((uint32_t)i * 7919u + 1); if (r.ok) { tierN[r.tier]++; const SkinDef* s = FindSkin(r.id); if (s && s->earned) earned++; } }
    check(earned == 0, "the Blobfish King and the Leviathan's Tooth never come out of a clam");
    check(fabsf(100.0f * tierN[1] / n - 61) < 4 && fabsf(100.0f * tierN[2] / n - 25) < 2.5f && fabsf(100.0f * tierN[3] / n - 12) < 2 && tierN[4] > 0, TextFormat("the odds: common %.1f%%, rare %.1f%%, super %.1f%%, special %.1f%%", 100.0f * tierN[1] / n, 100.0f * tierN[2] / n, 100.0f * tierN[3] / n, 100.0f * tierN[4] / n));
    // a round's pay
    World w; Opts o; o.humans = 1; o.bots = 0; o.seed = 9; w.Init(o);
    Mouth& m = w.mouths[0]; m.massEaten = 2000; m.score = 300; m.crownT = 30; m.path = P_EEL; m.blobKing = false;
    w.over = true; w.winner = 0;
    gW = Wardrobe{}; gWLoaded = true;
    int t = RoundTokens(w, 0, true);
    check(t == 10 + (int)(ScoreOf(w, m) / 100) + 20 + 30 && gW.rounds == 1 && gW.crowns == 1 && ((gW.crownPaths >> P_EEL) & 1), TextFormat("a won round with a first crown on the eel path pays %d", t));
    int t2 = RoundTokens(w, 0, false);
    check(t2 == t - 30, "a second crown on the same path pays no first-crown bonus");
    m.blobKing = true; int t3 = RoundTokens(w, 0, true);
    check(t3 >= 500 && gW.Owns("blobfish_king") && gW.blobKing, "the Blobfish King: 500 tokens and the skin nobody else can get");
    gWardrobeNoSave = false; gWLoaded = false;
    printf(fails ? "Mouthful skins: %d FAILED\n" : "Mouthful skins: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace mf
