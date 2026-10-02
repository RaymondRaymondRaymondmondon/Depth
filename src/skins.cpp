// Skins for both arcade games (see skins.h): the wallet, the crates, the store, what's worn, and the save.
#include "skins.h"
#include "redtide_profile.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace skins {

bool gNoSave = false;
static Wardrobe gW[GAME_COUNT];
static bool gLoaded = false;

const char* RarityName(int r) { static const char* N[RARITY_COUNT] = {"Store", "Common", "Rare", "Super rare", "Legendary"}; return r >= 0 && r < RARITY_COUNT ? N[r] : "?"; }
Color RarityColor(int r) {
    static const Color C[RARITY_COUNT] = {{200, 190, 160, 255}, {170, 180, 176, 255}, {90, 150, 230, 255}, {190, 110, 230, 255}, {240, 180, 60, 255}};
    return r >= 0 && r < RARITY_COUNT ? C[r] : WHITE;
}
const Skin* Find(int game, const std::string& id) {
    for (const auto& s : Catalogue(game)) if (id == s.id) return &s;
    return nullptr;
}
bool Wardrobe::Owns(const std::string& id) const { return std::find(owned.begin(), owned.end(), id) != owned.end(); }

static std::string Path() {
    return std::string(GetApplicationDirectory()) + "skins_save.txt";
}
void Load() {
    gLoaded = true;
    for (auto& w : gW) w = Wardrobe{};
    if (gNoSave) return;
    std::ifstream f(Path());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string key; int g = -1;
        in >> key >> g;
        if (g < 0 || g >= GAME_COUNT) continue;
        Wardrobe& w = gW[g];
        if (key == "own") { std::string id; while (in >> id) if (Find(g, id) && !w.Owns(id)) w.owned.push_back(id); }
        else if (key == "wear") { std::string id; in >> id; w.worn = id == "-" ? "" : id; }
        else if (key == "crates") in >> w.crates;
        else if (key == "tokens") in >> w.tokens;
    }
}
void Save() {
    if (gNoSave) return;
    std::ofstream f(Path());
    for (int g = 0; g < GAME_COUNT; g++) {
        const Wardrobe& w = gW[g];
        f << "own " << g; for (const auto& id : w.owned) f << " " << id; f << "\n";
        f << "wear " << g << " " << (w.worn.empty() ? "-" : w.worn) << "\n";
        f << "crates " << g << " " << w.crates << "\n";
        f << "tokens " << g << " " << w.tokens << "\n";
    }
}
Wardrobe& Get(int game) { if (!gLoaded) Load(); return gW[std::clamp(game, 0, GAME_COUNT - 1)]; }

int Tokens(int game) { return game == REDTIDE ? rt::GetProfile().tokens : Get(TRAWL).tokens; }
bool Spend(int game, int n) {
    if (Tokens(game) < n) return false;
    if (game == REDTIDE) { rt::GetProfile().tokens -= n; if (!gNoSave) rt::SaveProfile(); }
    else Get(TRAWL).tokens -= n;
    Save();
    return true;
}
void AddTokens(int game, int n) {
    if (game == REDTIDE) { rt::GetProfile().tokens += n; if (!gNoSave) rt::SaveProfile(); }
    else { Get(TRAWL).tokens += n; Save(); }
}
void AwardCrates(int game, int n) { Get(game).crates += n; Save(); }
bool BuyCrate(int game, std::string* why) {
    if (!Spend(game, CRATE_PRICE)) { if (why) *why = "not enough tokens"; return false; }
    Get(game).crates++; Save();
    return true;
}
bool BuySkin(int game, const std::string& id, std::string* why) {
    const Skin* s = Find(game, id);
    Wardrobe& w = Get(game);
    if (!s || s->rarity != STORE) { if (why) *why = "not for sale: crates only"; return false; }
    if (w.Owns(id)) { if (why) *why = "already yours"; return false; }
    if (!Spend(game, s->price)) { if (why) *why = "not enough tokens"; return false; }
    w.owned.push_back(id); w.worn = id; Save();
    return true;
}
bool Wear(int game, const std::string& id) {
    Wardrobe& w = Get(game);
    if (!id.empty() && !w.Owns(id)) return false;
    w.worn = id; Save();
    return true;
}
Roll OpenCrate(int game, uint32_t seed) {
    Roll r;
    Wardrobe& w = Get(game);
    if (w.crates <= 0) return r;
    w.crates--;
    uint32_t h = seed * 2654435761u + 0x9E3779B9u;
    auto R = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
    int pick = (int)(R() % 100), acc = 0;
    r.rarity = COMMON;
    for (int k = COMMON; k < RARITY_COUNT; k++) { acc += RARITY_WEIGHT[k]; if (pick < acc) { r.rarity = k; break; } }
    std::vector<const Skin*> pool;
    for (const auto& s : Catalogue(game)) if (s.rarity == r.rarity) pool.push_back(&s);
    if (pool.empty()) { Save(); return r; }
    const Skin* s = pool[R() % pool.size()];
    r.id = s->id; r.ok = true;
    if (w.Owns(r.id)) r.duplicate = true;           // (a failed roll: nothing, the crate spent)
    else w.owned.push_back(r.id);
    Save();
    return r;
}
std::vector<Tint> WornColours(int game) {
    const Wardrobe& w = Get(game);
    const Skin* s = w.worn.empty() ? nullptr : Find(game, w.worn);
    if (!s) return {};
    return {{"top", s->top}, {"trousers", s->trousers}, {"hat", s->hat}, {"accent", s->trim}};
}

// ---------------------------------------------------------------- depth.exe --skins-test
int RunSkinsTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Skins: the catalogue, the store, the crates\n");
    gNoSave = true; Load();
    for (int g = 0; g < GAME_COUNT; g++) {
        int n[RARITY_COUNT] = {};
        std::vector<std::string> ids;
        for (const auto& s : Catalogue(g)) { n[s.rarity]++; ids.push_back(s.id); }
        std::sort(ids.begin(), ids.end());
        bool unique = std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        check(n[STORE] == 15 && n[COMMON] == 25 && n[RARE] == 20 && n[SUPER] == 15 && n[LEGEND] == 5 && unique,
              TextFormat("%s: 15 store + 25 common + 20 rare + 15 super rare + 5 legendary, every id unique (%d %d %d %d %d)", g ? "Red Tide" : "the Trawl", n[0], n[1], n[2], n[3], n[4]));
        bool priced = true; for (const auto& s : Catalogue(g)) if ((s.rarity == STORE) != (s.price > 0)) priced = false;
        check(priced, "store skins have a price, crate skins none");
    }
    // the Trawl's wallet: buy a store skin, a crate; a duplicate is a failed roll
    Wardrobe& w = Get(TRAWL);
    w = Wardrobe{};
    w.tokens = 1000;
    std::string why;
    check(BuySkin(TRAWL, "t_harbour_blue", &why) && w.Owns("t_harbour_blue") && w.tokens == 800 && w.worn == "t_harbour_blue", "a store skin bought for 200 tokens and worn");
    check(!BuySkin(TRAWL, "t_harbour_blue", &why) && why == "already yours", "it can't be bought twice");
    check(!BuySkin(TRAWL, "t_glimmer", &why), "crate skins aren't for sale");
    check(BuyCrate(TRAWL, &why) && w.crates == 1 && w.tokens == 800 - CRATE_PRICE, TextFormat("a crate bought for %d tokens", CRATE_PRICE));
    Roll r = OpenCrate(TRAWL, 7);
    check(r.ok && !r.duplicate && w.Owns(r.id) && w.crates == 0 && Find(TRAWL, r.id)->rarity == r.rarity, TextFormat("opened: %s %s", RarityName(r.rarity), r.id.c_str()));
    // force a duplicate: own everything of the rolled rarity, then roll until that rarity comes up
    int dupSeen = 0, before = 0;
    for (const auto& s : Catalogue(TRAWL)) if (s.rarity != STORE && !w.Owns(s.id)) w.owned.push_back(s.id);
    before = (int)w.owned.size();
    for (uint32_t seed = 1; seed < 40; seed++) { w.crates = 1; Roll d = OpenCrate(TRAWL, seed); if (d.duplicate) dupSeen++; }
    check(dupSeen == 39 && (int)w.owned.size() == before && w.crates == 0, "with everything owned every roll is a duplicate: nothing given, the crate spent");
    // the odds: 10,000 rolls into an empty wardrobe land near 60 / 27 / 10 / 3
    int hist[RARITY_COUNT] = {};
    for (uint32_t seed = 1; seed <= 10000; seed++) { w.owned.clear(); w.crates = 1; Roll d = OpenCrate(TRAWL, seed); if (d.ok) hist[d.rarity]++; }
    check(abs(hist[COMMON] - 6000) < 300 && abs(hist[RARE] - 2700) < 250 && abs(hist[SUPER] - 1000) < 150 && abs(hist[LEGEND] - 300) < 80,
          TextFormat("crate odds over 10,000 rolls: common %d, rare %d, super rare %d, legendary %d", hist[COMMON], hist[RARE], hist[SUPER], hist[LEGEND]));
    w.owned = {"t_harbour_blue"};
    check(!Wear(TRAWL, "t_glimmer") && Wear(TRAWL, "t_harbour_blue") && WornColours(TRAWL).size() == 4 && Wear(TRAWL, "") && WornColours(TRAWL).empty(), "only owned skins can be worn; as issued has no colours");
    printf(fails ? "skins-test: %d check(s) failed\n" : "skins-test: all checks passed\n", fails);
    gNoSave = false; gLoaded = false;
    return fails ? 1 : 0;
}

} // namespace skins
