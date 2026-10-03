// The Flight's costumes, liveries, wallet and egg crate (stage 8; doc pp. 28-30). See flight_costumes.h.
#include "flight_costumes.h"
#include "flight.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

namespace fl {

const char* CostumeTierName(int t) {
    static const char* N[CT_COUNT] = {"Shop", "Common", "Rare", "Super Rare", "Special"};
    return N[std::clamp(t, 0, CT_COUNT - 1)];
}
Color CostumeTierColor(int t) {
    static const Color C[CT_COUNT] = {{220, 200, 150, 255}, {200, 204, 210, 255}, {110, 170, 250, 255}, {200, 120, 250, 255}, {250, 190, 60, 255}};
    return C[std::clamp(t, 0, CT_COUNT - 1)];
}

namespace {
Color Col(const Json& j, Color def) {
    if (!j.IsArr() || j.a.size() < 3) return def;
    return {(unsigned char)j.a[0].I(), (unsigned char)j.a[1].I(), (unsigned char)j.a[2].I(), 255};
}
int TierOf(const std::string& s) { return s == "shop" ? CT_SHOP : s == "rare" ? CT_RARE : s == "super" ? CT_SUPER : s == "special" ? CT_SPECIAL : CT_COMMON; }
}  // namespace

const CostumeData& Costumes() {
    static CostumeData d;
    static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_costumes.json");
    const Json& tk = j["tokens"];
    d.perMatch = tk["per_match"].I(d.perMatch); d.perScore = std::max(1, tk["per_score"].I(d.perScore)); d.first = tk["first"].I(d.first); d.win = tk["win"].I(d.win);
    d.cratePrice = j["crate_price"].I(d.cratePrice);
    const Json& od = j["odds"];
    d.odds[CT_COMMON] = od["common"].I(d.odds[CT_COMMON]); d.odds[CT_RARE] = od["rare"].I(d.odds[CT_RARE]);
    d.odds[CT_SUPER] = od["super"].I(d.odds[CT_SUPER]); d.odds[CT_SPECIAL] = od["special"].I(d.odds[CT_SPECIAL]);
    for (const Json& c : j["costumes"].a) {
        CostumeDef cd;
        cd.id = c["id"].Str0(); cd.name = c["name"].Str0(cd.id); cd.note = c["note"].Str0(); cd.tier = TierOf(c["tier"].Str0()); cd.price = c["price"].I(0);
        const Json& l = c["look"];
        CostumeLook& L = cd.look;
        L.hat = l["hat"].Str0(); L.hatC = Col(l["hat_c"], L.hatC);
        if (l["tint"].IsArr()) { L.tinted = true; L.tint = Col(l["tint"], L.tint); }
        if (l["beak_c"].IsArr()) { L.beakTinted = true; L.beakC = Col(l["beak_c"], L.beakC); }
        L.beak = l["beak"].F(1);
        for (const Json& e : l["extras"].a) L.extras.push_back(e.Str0());
        L.extraC = Col(l["extra_c"], L.extraC);
        L.scale = l["scale"].F(1); L.fx = l["fx"].Str0(); L.follow = l["follow"].Str0(); L.label = l["label"].Str0();
        L.glow = l["glow"].Bool0(false); L.waddle = l["waddle"].Bool0(false);
        if (!cd.id.empty()) d.costumes.push_back(cd);
    }
    const Json& lv = j["liveries"];
    for (const Json& c : lv["colours"].a) { LiveryColour x; x.id = c["id"].Str0(); x.name = c["name"].Str0(x.id); x.price = c["price"].I(60); x.c = Col(c["c"], WHITE); d.colours.push_back(x); }
    for (const Json& c : lv["hats"].a) { LiveryHat x; x.id = c["id"].Str0(); x.name = c["name"].Str0(x.id); x.hat = c["hat"].Str0(); x.price = c["price"].I(80); x.c = Col(c["c"], WHITE); d.hats.push_back(x); }
    return d;
}
const CostumeDef* FindCostume(const std::string& id) {
    for (const auto& c : Costumes().costumes) if (c.id == id) return &c;
    if (id == "dynast") {   // (the costume only the Long Flight gives: a crown of the dynasty's own feathers)
        static CostumeDef d; static bool made = false;
        if (!made) { made = true; d.id = "dynast"; d.name = "The Dynast"; d.note = "A crown of your dynasty's own feathers, with its name (only the Long Flight gives it)"; d.tier = CT_SPECIAL; d.look.hat = "crown"; d.look.hatC = {200, 120, 220, 255}; d.look.label = "dynasty"; }
        return &d;
    }
    return nullptr;
}
int AwardLongFlight(int baseTokens, uint32_t lfFirsts, const std::string& dynasty) {
    FlightWardrobe& w = Wardrobe();
    int got = baseTokens;   // (double: the match's own tokens again)
    for (int b = 3; b <= 5; b++) if (((lfFirsts >> b) & 1) && !((w.firsts >> b) & 1)) { w.firsts |= 1u << b; got += 50; }
    w.tokens += got;
    if (!w.Owns("dynast")) { w.owned.push_back("dynast"); if (w.costume.empty()) w.costume = "dynast"; }
    w.dynasty = dynasty;
    SaveWardrobe();
    return got;
}
const LiveryColour* FindLiveryColour(const std::string& id) { for (const auto& c : Costumes().colours) if (c.id == id) return &c; return nullptr; }
const LiveryHat* FindLiveryHat(const std::string& id) { for (const auto& c : Costumes().hats) if (c.id == id) return &c; return nullptr; }

// ---------------------------------------------------------------- the wardrobe (flight_wardrobe.txt next to the exe)
bool gWardrobeNoSave = false;
bool FlightWardrobe::Owns(const std::string& id) const { return std::find(owned.begin(), owned.end(), id) != owned.end(); }
namespace {
FlightWardrobe gW; bool gWLoaded = false;
std::string WardrobePath() { return std::string(GetApplicationDirectory()) + "flight_wardrobe.txt"; }
}
FlightWardrobe& Wardrobe() { if (!gWLoaded) LoadWardrobe(); return gW; }
void LoadWardrobe() {
    gWLoaded = true;
    gW = FlightWardrobe{};
    if (gWardrobeNoSave) return;
    std::ifstream f(WardrobePath());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k;
        if (k == "tokens") s >> gW.tokens;
        else if (k == "feathers") s >> gW.feathers;
        else if (k == "crates") s >> gW.crates;
        else if (k == "best") s >> gW.bestScore;
        else if (k == "firsts") s >> gW.firsts;
        else if (k == "hints") s >> gW.hints;
        else if (k == "dynasty") { std::getline(s, gW.dynasty); if (!gW.dynasty.empty() && gW.dynasty[0] == ' ') gW.dynasty.erase(0, 1); }
        else if (k == "own") { std::string id; while (s >> id) gW.owned.push_back(id); }
        else if (k == "costume") s >> gW.costume;
        else if (k == "colour") s >> gW.liveryColour;
        else if (k == "hat") s >> gW.liveryHat;
    }
}
void SaveWardrobe() {
    if (gWardrobeNoSave) return;
    std::ofstream f(WardrobePath());
    f << "tokens " << gW.tokens << "\nfeathers " << gW.feathers << "\ncrates " << gW.crates << "\nbest " << gW.bestScore << "\nfirsts " << gW.firsts << "\nhints " << gW.hints << "\nown";
    for (const auto& id : gW.owned) f << " " << id;
    f << "\n";
    if (!gW.costume.empty()) f << "costume " << gW.costume << "\n";
    if (!gW.liveryColour.empty()) f << "colour " << gW.liveryColour << "\n";
    if (!gW.liveryHat.empty()) f << "hat " << gW.liveryHat << "\n";
    if (!gW.dynasty.empty()) f << "dynasty " << gW.dynasty << "\n";
}

bool BuyCostume(const std::string& id, std::string* why) {
    FlightWardrobe& w = Wardrobe();
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    int price = -1;
    if (const CostumeDef* c = FindCostume(id)) { if (c->tier != CT_SHOP) return no("that one only hatches from an egg"); price = c->price; }
    else if (const LiveryColour* c = FindLiveryColour(id)) price = c->price;
    else if (const LiveryHat* h = FindLiveryHat(id)) price = h->price;
    if (price < 0) return no("no such costume");
    if (w.Owns(id)) return no("already yours");
    if (w.tokens < price) return no("not enough tokens");
    w.tokens -= price; w.owned.push_back(id);
    SaveWardrobe();
    return true;
}
bool WearCostume(const std::string& id) {
    FlightWardrobe& w = Wardrobe();
    if (id.empty()) { w.costume.clear(); SaveWardrobe(); return true; }
    if (!w.Owns(id)) return false;
    if (FindCostume(id)) w.costume = id;
    else if (FindLiveryColour(id)) w.liveryColour = w.liveryColour == id ? "" : id;
    else if (FindLiveryHat(id)) w.liveryHat = w.liveryHat == id ? "" : id;
    else return false;
    SaveWardrobe();
    return true;
}
bool BuyCrate(std::string* why) {
    FlightWardrobe& w = Wardrobe();
    if (w.tokens < Costumes().cratePrice) { if (why) *why = "not enough tokens"; return false; }
    w.tokens -= Costumes().cratePrice; w.crates++;
    SaveWardrobe();
    return true;
}
EggRoll OpenEgg(uint32_t seed) {
    EggRoll r;
    FlightWardrobe& w = Wardrobe();
    if (w.crates <= 0) return r;
    w.crates--;
    std::mt19937 rng(seed);
    const CostumeData& D = Costumes();
    int total = 0; for (int t = CT_COMMON; t < CT_COUNT; t++) total += D.odds[t];
    int pick = (int)(rng() % (uint32_t)std::max(1, total));
    int tier = CT_COMMON; for (int t = CT_COMMON; t < CT_COUNT; t++) { if (pick < D.odds[t]) { tier = t; break; } pick -= D.odds[t]; }
    std::vector<const CostumeDef*> pool; for (const auto& c : D.costumes) if (c.tier == tier) pool.push_back(&c);
    r.ok = true; r.tier = tier;
    if (pool.empty()) { r.duplicate = true; w.feathers++; SaveWardrobe(); return r; }
    const CostumeDef* c = pool[rng() % pool.size()];
    r.id = c->id;
    if (w.Owns(c->id)) { r.duplicate = true; w.feathers++; }   // (a duplicate hatches a feather and nothing else)
    else w.owned.push_back(c->id);
    SaveWardrobe();
    return r;
}
int MatchTokens(int score, bool won, uint32_t firstsThisMatch, uint32_t* newFirsts) {
    const CostumeData& D = Costumes();
    uint32_t fresh = firstsThisMatch & ~Wardrobe().firsts;
    int n = D.perMatch + std::max(0, score) / D.perScore + (won ? D.win : 0);
    for (int b = 0; b < 3; b++) if (fresh & (1u << b)) n += D.first;
    if (newFirsts) *newFirsts = fresh;
    return n;
}
int AwardMatch(int score, bool won, uint32_t firstsThisMatch) {
    uint32_t fresh = 0;
    int n = MatchTokens(score, won, firstsThisMatch, &fresh);
    FlightWardrobe& w = Wardrobe();
    w.tokens += n; w.firsts |= fresh; w.bestScore = std::max(w.bestScore, score);
    SaveWardrobe();
    return n;
}

// ---------------------------------------------------------------- --flight-costume-test
int RunFlightCostumeTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 8: costumes, the egg crate, liveries and tokens (doc pp. 28-30)\n");
    gWardrobeNoSave = true; LoadWardrobe();
    const CostumeData& D = Costumes();
    int n[CT_COUNT] = {}; for (const auto& c : D.costumes) n[c.tier]++;
    check(n[CT_SHOP] == 10, TextFormat("the token shop has 10 Founder costumes (%d)", n[CT_SHOP]));
    check(n[CT_COMMON] == 15 && n[CT_RARE] == 11 && n[CT_SUPER] == 10 && n[CT_SPECIAL] == 6, TextFormat("the crate holds 15 common, 11 rare, 10 super rare and 6 special (%d, %d, %d, %d)", n[CT_COMMON], n[CT_RARE], n[CT_SUPER], n[CT_SPECIAL]));
    std::map<std::string, int> ids; bool unique = true; for (const auto& c : D.costumes) unique &= ++ids[c.id] == 1;
    check(unique, "every costume has its own id");
    bool prices = true; for (const auto& c : D.costumes) if (c.tier == CT_SHOP) prices &= c.price >= 50 && c.price <= 250;
    check(prices && FindCostume("captain_nemo") && FindCostume("captain_nemo")->price == 250 && FindCostume("sea_legs")->price == 50, "shop prices run 50 (Sea Legs) to 250 (Captain Nemo)");
    check(D.colours.size() == 6 && D.hats.size() == 6 && D.colours[0].price == 60 && D.hats[0].price == 80, TextFormat("six livery colours at 60 and six colony hats at 80 (%d, %d)", (int)D.colours.size(), (int)D.hats.size()));
    check(D.odds[CT_COMMON] + D.odds[CT_RARE] + D.odds[CT_SUPER] + D.odds[CT_SPECIAL] == 100, "the crate's odds sum to 100%");
    // the odds over many eggs (the wardrobe reset each time, so nothing is a duplicate)
    {
        int got[CT_COUNT] = {}; const int N = 20000;
        for (int i = 0; i < N; i++) { gW = FlightWardrobe{}; gW.crates = 1; EggRoll r = OpenEgg(1000u + i * 7919u); if (r.ok) got[r.tier]++; }
        bool ok = true; std::string s;
        for (int t = CT_COMMON; t < CT_COUNT; t++) { float pct = 100.0f * got[t] / N; ok &= fabsf(pct - D.odds[t]) < 1.5f; s += TextFormat(" %s %.1f%%", CostumeTierName(t), pct); }
        check(ok, "20,000 eggs hatch at the odds:" + s);
    }
    {
        gW = FlightWardrobe{}; gW.tokens = 3;
        check(BuyCrate() && gW.tokens == 3 - D.cratePrice && gW.crates == 1, "a crate costs a token");
        EggRoll a = OpenEgg(5); gW.crates = 1; EggRoll b = OpenEgg(5);
        check(a.ok && !a.duplicate && b.ok && b.duplicate && gW.feathers == 1 && gW.owned.size() == 1, TextFormat("the same egg twice: %s, then a feather (%d feather)", a.id.c_str(), gW.feathers));
        check(!OpenEgg(6).ok, "no crate, no egg");
    }
    {
        gW = FlightWardrobe{}; gW.tokens = 130;
        std::string why;
        bool duck = BuyCostume("rubber_duck", &why);
        check(!duck && why.find("egg") != std::string::npos, "crate costumes can't be bought: " + why);
        check(BuyCostume("admiral") && gW.tokens == 30 && gW.Owns("admiral"), "the Admiral for 100 tokens");
        bool twice = BuyCostume("admiral", &why);
        check(!twice, "not twice: " + why);
        bool nemo = BuyCostume("captain_nemo", &why);
        check(!nemo && gW.tokens == 30, "not without the tokens: " + why);
        check(WearCostume("admiral") && gW.costume == "admiral" && !WearCostume("pirate"), "worn only when owned");
        gW.tokens = 200; BuyCostume("lagoon"); BuyCostume("top_hats");
        check(WearCostume("lagoon") && WearCostume("top_hats") && gW.liveryColour == "lagoon" && gW.liveryHat == "top_hats" && gW.costume == "admiral", "a colony livery (lagoon, tiny top hats) worn beside the costume");
    }
    {
        gW = FlightWardrobe{};
        int t1 = MatchTokens(260, false, 0), t2 = MatchTokens(260, true, 0b011);
        check(t1 == 10 + 5 && t2 == 10 + 5 + 20 + 10, TextFormat("a match pays 10, 1 per 50 score and 20 for a win, 5 for each first (%d, %d)", t1, t2));
        AwardMatch(260, true, 0b001);
        check(gW.tokens == 10 + 5 + 20 + 5 && gW.firsts == 1 && gW.bestScore == 260 && MatchTokens(0, false, 0b001) == 10, "a first pays only the first time ever; the best score is kept (for the crown)");
    }
    {   // every look names parts the scene knows
        static const char* HATS[] = {"", "sailor_cap", "beanie", "bicorne", "hook_hat", "straw_hat", "bandana", "cowl", "periscope_cap", "comb", "crest_feathers", "heron_hat", "bell_helmet", "brass_helmet", "ape_hat", "crown", "swoop_hair", "top_hat", "shell"};
        static const char* EXTRAS[] = {"neckerchief", "lantern", "epaulettes", "goggles", "shell_necklace", "straw", "eyepatch", "wooden_leg", "tentacle_scarf", "coat", "beard", "white_front", "beak_stripes", "long_leg", "iridescent_neck", "fan_tail", "wattle", "glasses", "long_neck", "pouch", "peacock_tail", "chip", "shiny", "cape", "kiwi_suit", "bundle", "ruff", "clock", "scroll", "armband", "lion_rear", "arms8", "worm_puppet", "fish_suit", "sub_hull", "goat_horns", "goat_beard", "egg_body", "sun_mask", "orca_patches", "dorsal_fin", "red_tie"};
        auto known = [](const char* const* list, size_t n, const std::string& s) { for (size_t i = 0; i < n; i++) if (s == list[i]) return true; return false; };
        std::string bad;
        for (const auto& c : D.costumes) {
            if (!known(HATS, sizeof(HATS) / sizeof(*HATS), c.look.hat)) bad += " " + c.id + ":" + c.look.hat;
            for (const auto& e : c.look.extras) if (!known(EXTRAS, sizeof(EXTRAS) / sizeof(*EXTRAS), e)) bad += " " + c.id + ":" + e;
        }
        for (const auto& h : D.hats) if (!known(HATS, sizeof(HATS) / sizeof(*HATS), h.hat)) bad += " " + h.id + ":" + h.hat;
        check(bad.empty(), "every look's hat and extras are drawn by the scene" + (bad.empty() ? std::string() : " (unknown:" + bad + ")"));
    }
    gWardrobeNoSave = false; gWLoaded = false;
    printf(fails ? "flight-costume-test: %d check(s) failed\n" : "flight-costume-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl
