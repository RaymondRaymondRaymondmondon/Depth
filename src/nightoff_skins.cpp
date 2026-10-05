// A Night Off: skins and the token spin (doc pp. 37-40), stage 10e. Headless. Tokens buy skins and nothing else: a
// night pays 10 for playing, 1 per 50 score, 5 per headline earned for the first time and 20 for a first on each
// seasonal night. 50 skins: 10 in the cloakroom's store, 40 only from the brass wheel (one token a spin; duplicates
// aren't refunded; odds Common 60%, Rare 25%, Super Rare 12%, Special 3%). Skins never change the numbers, with two
// jokes: the Constable confuses the police for 10 seconds, and the Golden Kidney is still stealable (they take the
// costume). The list, the odds and the token values are data (nightoff_skins.json).
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>

namespace no {

struct SkinsData { std::vector<SkinDef> s; float odds[4] = {0.6f, 0.25f, 0.12f, 0.03f}; int perNight = 10, per50 = 1, firstHeadline = 5, firstSeason = 20, spinCost = 1; };
static Color C3(const Json& j) { return {(unsigned char)j[0].I(128), (unsigned char)j[1].I(128), (unsigned char)j[2].I(128), 255}; }
static const SkinsData& KD() {
    static SkinsData d = [] {
        SkinsData d; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_skins.json");
        d.perNight = j["tokens"]["per_night"].I(10); d.per50 = j["tokens"]["per_50_score"].I(1); d.firstHeadline = j["tokens"]["first_headline"].I(5); d.firstSeason = j["tokens"]["first_season"].I(20);
        d.spinCost = j["spin_cost"].I(1);
        static const char* T[4] = {"common", "rare", "super", "special"};
        for (int k = 0; k < 4; k++) d.odds[k] = j["odds"][T[k]].F(d.odds[k]);
        for (const Json& e : j["skins"].a) {
            SkinDef x; x.key = e["key"].Str0(); x.name = e["name"].Str0(); x.tier = e["tier"].Str0("common"); x.shape = e["shape"].Str0(); x.look = e["look"].Str0(); x.cost = e["cost"].I(0);
            x.top = C3(e["top"]); x.trousers = C3(e["trousers"]); x.hat = C3(e["hat"]);
            d.s.push_back(x);
        }
        return d;
    }();
    return d;
}
const std::vector<SkinDef>& Skins() { return KD().s; }
int SkinIndex(const std::string& key) { const auto& s = Skins(); for (int i = 0; i < (int)s.size(); i++) if (s[i].key == key) return i; return -1; }
int SpinCost() { return KD().spinCost; }
int SpinSkin(uint32_t& rng) {
    auto U = [&]() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffffff) / 16777216.0f; };
    static const char* T[4] = {"common", "rare", "super", "special"};
    float u = U(), acc = 0; int tier = 0;
    for (int k = 0; k < 4; k++) { acc += KD().odds[k]; if (u < acc) { tier = k; break; } tier = k; }
    std::vector<int> pool; for (int i = 0; i < (int)Skins().size(); i++) if (Skins()[i].tier == T[tier]) pool.push_back(i);
    if (pool.empty()) return -1;
    return pool[(int)(U() * pool.size()) % pool.size()];
}
bool OwnsSkin(const NightProfile& pr, int i) { return i >= 0 && i < 64 && ((pr.skins >> i) & 1); }
bool BuySkin(NightProfile& pr, int i, std::string* why) {
    if (i < 0 || i >= (int)Skins().size() || Skins()[i].tier != "store") { if (why) *why = "That one only comes from the wheel."; return false; }
    if (OwnsSkin(pr, i)) { if (why) *why = "You already own it."; return false; }
    if (pr.tokens < Skins()[i].cost) { if (why) *why = TextFormat("It's %d tokens; you have %d.", Skins()[i].cost, pr.tokens); return false; }
    pr.tokens -= Skins()[i].cost; pr.skins |= 1ull << i; return true;
}
int SpinWheel(NightProfile& pr, uint32_t seed, bool* duplicate) {
    if (pr.tokens < SpinCost()) return -1;
    pr.tokens -= SpinCost();
    uint32_t r = seed ? seed : 1; int i = SpinSkin(r);
    if (i < 0) return -1;
    if (duplicate) *duplicate = OwnsSkin(pr, i);   // (the wheel laughs; nothing is refunded)
    pr.skins |= 1ull << i; pr.spins++;
    return i;
}
// the night's tokens, into the profile (called by ProfileAfter before tonight's headline is added to the list)
int NightTokens(const Night& n, const Player& p, NightProfile& pr, std::vector<std::string>& lines) {
    const SkinsData& d = KD();
    int score = n.Score(p), tok = d.perNight + std::max(0, score) / 50 * d.per50;
    std::string h = n.Headline();
    bool first = !h.empty() && std::find(pr.headlines.begin(), pr.headlines.end(), h) == pr.headlines.end();
    if (first) tok += d.firstHeadline;
    if (n.SeasonOn() && n.opts.season > 0 && n.opts.season < 32 && !((pr.seasonsDone >> n.opts.season) & 1)) { pr.seasonsDone |= 1u << n.opts.season; tok += d.firstSeason; lines.push_back(TextFormat("Your first %s: +%d tokens.", SeasonName(n.opts.season), d.firstSeason)); }
    pr.tokens += tok;
    lines.push_back(TextFormat("%d arcade tokens tonight (%d in the purse).", tok, pr.tokens));
    return tok;
}

// ---------------------------------------------------------------- --night-test's stage 10e checks
int NightSkinChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    int store = 0, tiers[4] = {}; static const char* T[4] = {"common", "rare", "super", "special"};
    for (const auto& s : Skins()) { store += s.tier == "store"; for (int k = 0; k < 4; k++) tiers[k] += s.tier == T[k]; }
    check(Skins().size() == 50 && store == 10 && tiers[0] == 15 && tiers[1] == 10 && tiers[2] == 10 && tiers[3] == 5, "50 skins: 10 to save for, and the wheel's 15/10/10/5");
    // the wheel's odds (100,000 spins)
    { int got[4] = {}; uint32_t r = 7; for (int i = 0; i < 100000; i++) { int s = SpinSkin(r); for (int k = 0; k < 4; k++) got[k] += Skins()[s].tier == T[k]; }
      check(std::abs(got[0] - 60000) < 1500 && std::abs(got[1] - 25000) < 1200 && std::abs(got[2] - 12000) < 900 && std::abs(got[3] - 3000) < 400, TextFormat("the wheel: %.1f%% common, %.1f%% rare, %.1f%% super rare, %.1f%% special", got[0] / 1000.0f, got[1] / 1000.0f, got[2] / 1000.0f, got[3] / 1000.0f)); }
    // the doc's estimate: about 290 spins to own all 40
    { double tot = 0; for (int run = 0; run < 200; run++) { uint64_t own = 0; int n = 0; uint32_t r = 1000 + run * 7919; int left = 40; while (left > 0 && n < 5000) { int s = SpinSkin(r); n++; if (!((own >> s) & 1)) { own |= 1ull << s; left--; } } tot += n; }
      check(tot / 200 > 250 && tot / 200 < 500, TextFormat("about %.0f spins to own all forty (the doc's odds; its own estimate of 290 is low)", tot / 200)); }
    NightProfile pr; pr.tokens = 60; std::string why;
    int shore = SkinIndex("shoreleave"), cap = SkinIndex("captain");
    check(BuySkin(pr, shore, &why) && pr.tokens == 35 && OwnsSkin(pr, shore), "Shore Leave bought for 25");
    check(!BuySkin(pr, cap, &why), "the Captain is 125: not yet");
    check(!BuySkin(pr, SkinIndex("goat"), &why), "the Goat only comes from the wheel");
    { int before = pr.tokens; bool dup = false; int s = SpinWheel(pr, 99, &dup); check(s >= 0 && Skins()[s].tier != "store" && pr.tokens == before - 1 && OwnsSkin(pr, s), "a spin: one token, one skin from the wheel");
      bool dup2 = false; NightProfile q = pr; q.tokens = 1; int s2 = SpinWheel(q, 99, &dup2); check(s2 == s && dup2 && q.tokens == 0, "a duplicate is a duplicate (nothing refunded)"); }
    // tokens from a night: 10 for playing, 1 per 50, 5 for a first headline, 20 for a first seasonal night
    { Night n; Opts o; o.seed = 5; o.events = false; o.season = SeasonId("regatta"); n.Init(o); Player& p = n.players[0]; p.money = 700; n.Leave(p, E_WALKED, ""); n.over = true;
      NightProfile a; std::vector<std::string> L; int got = NightTokens(n, p, a, L); int score = n.Score(p);
      check(got == 10 + score / 50 + 5 + 20, TextFormat("a night's tokens: %d (score %d, a first headline, a first Regatta)", got, score));
      a.headlines.push_back(n.Headline()); std::vector<std::string> L2; int again = NightTokens(n, p, a, L2);
      check(again == 10 + score / 50, "the same headline and season again: no firsts"); }
    // the jokes: the Constable confuses the police; the Golden Kidney is stealable (they take the costume)
    { Night n; Opts o; o.seed = 8; o.events = false; n.Init(o); Player& p = n.players[0]; p.skin = SkinIndex("constable"); n.policeConfusedT = 10;
      n.Leave(p, E_ARRESTED, ""); check(p.st == State::Active, "the Constable: the police salute you for ten confused seconds"); }
    { Night n; Opts o; o.seed = 8; o.events = false; n.Init(o); Player& p = n.players[0]; p.skin = SkinIndex("goldkidney");
      int thief = -1; for (auto& c : n.patrons) if (c.home == "kidney") { thief = c.id; break; }
      if (thief >= 0) n.GoHome(p, thief, "kidney");
      NightProfile g; g.skins = 1ull << SkinIndex("goldkidney"); g.skin = SkinIndex("goldkidney");
      n.over = true; n.ProfileAfter(p, g);
      check(thief >= 0 && p.kidneys == 2 && p.lostGold && !OwnsSkin(g, SkinIndex("goldkidney")) && g.skin < 0, "the Golden Kidney: they take the costume, and you keep your kidney"); }
    { NightProfile s; s.skins = 3; s.skin = 1; s.tokens = 33; s.bestHeadline = "SAILOR WINS"; NightProfile t; check(ParseProfileSummary(ProfileSummary(s), t) && t.skin == 1 && t.bestHeadline == "SAILOR WINS", "the skin goes to the host in the hello");
      std::string path = "nightoff_skins_test.txt"; SaveNightProfile(s, path); NightProfile u = LoadNightProfile(path); std::remove(path.c_str());
      check(u.skins == 3 && u.skin == 1 && u.tokens == 33, "tokens and skins are saved in the profile"); }
    // the dog (doc p. 42): once it follows for five minutes, twice all night, three times it's yours; named by its first feeder; stolen with more chips, a feud
    { Night n; Opts o; o.seed = 4; o.events = false; o.players = 2; n.Init(o); Player& a = n.players[0]; Player& b = n.players[1];
      auto feed = [&](Player& p) { p.pos = Vector2Add(n.dog.pos, {0.6f, 0}); p.money = 100; p.in.feedDog = true; n.Step(0.02f); };
      feed(a); check(n.dog.follow == 0 && n.dog.followT > 0 && n.dog.followT < 1e8f, "fed once: the dog follows for five minutes");
      a.in.nameDog = "Biscuit"; n.Step(0.02f); check(n.dog.name == "Biscuit", "and the first to feed it names it");
      feed(a); check(n.dog.followT > 1e8f, "fed twice: all night");
      feed(a); check(n.dog.owner == 0, "three times: it's yours");
      for (int k = 0; k < 4; k++) feed(b);
      bool feud = false; for (const auto& m : a.log) feud |= m.text.find("stole") != std::string::npos;
      check(n.dog.owner == 1 && feud, "and a friend who feeds it more steals it: a feud"); }
    return fails;
}

} // namespace no
