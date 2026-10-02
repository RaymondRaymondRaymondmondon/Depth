// Red Tide's arcade profile: see redtide_profile.h.
#include "redtide_profile.h"
#include "skins.h"
#include "json.h"
#include "redtide.h"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace rt {

const std::vector<CharmDef>& Charms() {
    // the design doc's Salt Charms (gobblegums)
    static const std::vector<CharmDef> c = {
        {"brines", "Keep Your Brines", "Tonics survive a down (once)"},
        {"circle", "Salt Circle", "20 s: beasts won't cross where you stand"},
        {"locker", "Lucky Locker", "Your next Locker pull comes out of the Forge"},
        {"shares", "Fair Shares", "Every living diver's scrip is pooled and split evenly"},
        {"fins", "Slick Fins", "30 s of full sprint"},
        {"clean", "Clean Water", "Clears the blood within 30 m"},
        {"luck", "Fisher's Luck", "Your next 10 kills pay double"},
        {"chum", "Chum Bucket", "A big chum cloud 30 m away: the reef looks over there"},
        {"shell", "Hard Shell", "60 s of half damage"},
        {"ghost", "Ghost Fin", "15 s: beasts and enemies lose your scent and sound"},
    };
    return c;
}

const std::vector<Cosmetic>& Cosmetics() {
    // the progression sheet: finishes 150, suit colour sets 100, helmets 200; some come free with ranks
    static const std::vector<Cosmetic> c = {
        {"barnacle", "finish", "Barnacle", 150}, {"verdigrisf", "finish", "Verdigris", 150}, {"bone_inlay", "finish", "Bone inlay", 150},
        {"pearlf", "finish", "Pearl", 150}, {"redtidef", "finish", "Red Tide", 150},
        {"verdigris", "suit", "Verdigris suit", 100}, {"redtide", "suit", "Red Tide suit", 100}, {"bone", "suit", "Bone suit", 100},
        {"pearl", "suit", "Pearl suit", 100}, {"atlantean", "suit", "Atlantean suit", 100},
        {"h_verdigris", "helmet", "Verdigris helmet", 200}, {"h_redtide", "helmet", "Red Tide helmet", 200}, {"h_bone", "helmet", "Bone helmet", 200},
        {"h_pearl", "helmet", "Pearl helmet", 200}, {"h_atlantean", "helmet", "Atlantean helmet", 200},
    };
    return c;
}

static std::vector<std::pair<int, std::string>>& RankTable() {
    // [rank, lifetime tokens to reach, unlock] from engine/progression.json
    static std::vector<std::pair<int, std::string>> t;
    if (!t.empty()) return t;
    Json p = LoadJsonFile(DataDir() + "/engine/progression.json");
    for (const Json& r : p.a) {
        if (!r.IsArr() || r.Size() < 2 || !r[0].IsNum()) continue;
        t.push_back({r[1].I(), r.Size() > 2 ? r[2].Str0() : ""});
    }
    if (t.empty()) for (int k = 1; k <= 50; k++) t.push_back({(int)(50 * powf((float)k, 1.6f)), ""});
    return t;
}

int Profile::Rank() const {
    int r = 0;
    for (const auto& e : RankTable()) if (earned >= e.first) r++;
    return std::clamp(r, 1, 50);
}
int Profile::NextRankAt() const {
    for (const auto& e : RankTable()) if (earned < e.first) return e.first;
    return -1;
}
int Profile::PouchSlots() const {
    int n = 1, r = Rank();
    for (int at : {7, 15, 25, 40}) if (r >= at) n++;
    return std::min(5, n);
}

// The rank sheet's unlock text, applied: "Salt Charm: X", "Skin: X suit/helmet", "Dossier bonus page: X"
void Profile::ApplyRankUnlocks(std::vector<std::string>* news) {
    const auto& t = RankTable();
    int r = Rank();
    for (int i = 0; i < (int)t.size() && i < r; i++) {
        const std::string& u = t[i].second;
        if (u.rfind("Salt Charm: ", 0) == 0) {
            std::string n = u.substr(12);
            for (const auto& c : Charms()) if (c.name == n && !charms.count(c.id)) { charms.insert(c.id); if (news) news->push_back("New Salt Charm: " + n); }
        } else if (u.rfind("Skin: ", 0) == 0) {
            std::string n = u.substr(6);
            for (const auto& c : Cosmetics()) if (c.name == n && !owned.count(c.id)) { owned.insert(c.id); if (news) news->push_back("New skin: " + n); }
        } else if (u.rfind("Dossier bonus page: ", 0) == 0) {
            std::string n = u.substr(20);
            std::string id = n.find("Owners") != std::string::npos ? "owners" : n.find("expedition") != std::string::npos ? "expedition" : n.find("station") != std::string::npos ? "station" : n;
            if (!bonus.count(id)) { bonus.insert(id); if (news) news->push_back("New dossier page: " + n); }
        }
    }
    if ((int)pouch.size() > PouchSlots()) pouch.resize(PouchSlots());
}

static Profile gProfile;
static bool gLoaded = false;
Profile& GetProfile() { if (!gLoaded) LoadProfile(); return gProfile; }
std::string ProfilePath() { return std::string(GetApplicationDirectory()) + "redtide_profile.txt"; }

void SaveProfile() {
    const Profile& p = gProfile;
    std::ofstream f(ProfilePath());
    if (!f) return;
    f << "tokens " << p.tokens << "\nearned " << p.earned << "\n";
    auto set = [&](const char* k, const std::set<std::string>& s) { for (const auto& v : s) f << k << " " << v << "\n"; };
    set("charm", p.charms); set("owned", p.owned); set("first", p.firsts); set("page", p.dossier); set("bonus", p.bonus);
    auto map = [&](const char* k, const std::map<std::string, int>& m) { for (const auto& kv : m) f << k << " " << kv.first << " " << kv.second << "\n"; };
    map("tide", p.bestTide); map("scrip", p.bestScrip); map("time", p.bestTimeS); map("forge", p.forgeTimeS);
    f << "wear " << (p.suit.empty() ? "-" : p.suit) << " " << (p.helmet.empty() ? "-" : p.helmet) << " " << (p.finish.empty() ? "-" : p.finish) << "\n";
    for (const auto& c : p.pouch) f << "pouch " << c << "\n";
}

void LoadProfile() {
    gLoaded = true;
    gProfile = Profile{};
    std::ifstream f(ProfilePath());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string k; s >> k;
        std::string rest; std::getline(s, rest); if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        Profile& p = gProfile;
        if (k == "tokens") p.tokens = atoi(rest.c_str());
        else if (k == "earned") p.earned = atoi(rest.c_str());
        else if (k == "charm") p.charms.insert(rest);
        else if (k == "owned") p.owned.insert(rest);
        else if (k == "first") p.firsts.insert(rest);
        else if (k == "page") p.dossier.insert(rest);
        else if (k == "bonus") p.bonus.insert(rest);
        else if (k == "pouch") p.pouch.push_back(rest);
        else if (k == "tide" || k == "scrip" || k == "time" || k == "forge") {
            size_t sp = rest.rfind(' ');
            if (sp == std::string::npos) continue;
            std::string key = rest.substr(0, sp); int v = atoi(rest.c_str() + sp + 1);
            (k == "tide" ? p.bestTide : k == "scrip" ? p.bestScrip : k == "time" ? p.bestTimeS : p.forgeTimeS)[key] = v;
        } else if (k == "wear") {
            std::istringstream w(rest); std::string a, b, c; w >> a >> b >> c;
            p.suit = a == "-" ? "" : a; p.helmet = b == "-" ? "" : b; p.finish = c == "-" ? "" : c;
        }
    }
    gProfile.ApplyRankUnlocks();
}

int DossierTotal(const std::string& map) {
    Json d = LoadJsonFile(DataDir() + "/engine/dossier_" + map + ".json");
    return (int)d.Size() + 1;   // + the faction's page
}

std::vector<std::string> AwardMatch(const MatchSummary& s, int* tokensOut) {
    Profile& p = GetProfile();
    std::vector<std::string> lines;
    int tok = 10;
    lines.push_back("Match played: 10");
    if (s.tide >= 10) { tok += 25; lines.push_back("Tide 10 and past: 25"); }
    if (s.tide > 10) { tok += 2 * (s.tide - 10); lines.push_back(TextFormat("%d tides past 10: %d", s.tide - 10, 2 * (s.tide - 10))); }
    auto first = [&](const std::string& key, int n, const std::string& what) { if (!p.firsts.count(key)) { p.firsts.insert(key); tok += n; lines.push_back(what + TextFormat(": %d", n)); } };
    if (s.tide >= 20) first(s.map + ":tide20", 50, "First time to tide 20 here");
    if (s.bossKilled) first(s.map + ":boss", 30, "First boss kill here");
    if (s.questDone) first(s.map + ":quest", 100, "First hidden quest here");
    int newPages = 0;
    for (const auto& n : s.pages) if (p.dossier.insert(s.map + "|" + n).second) newPages++;
    for (const auto& b : s.bonusPages) if (p.bonus.insert(b).second) lines.push_back("New dossier page: " + b);
    if (newPages) lines.push_back(TextFormat("New dossier pages: %d", newPages));
    int have = 0; for (const auto& k : p.dossier) if (k.rfind(s.map + "|", 0) == 0) have++;
    if (have >= DossierTotal(s.map)) first(s.map + ":dossier", 75, "Dossier complete");
    // milestone charms
    if (s.tide >= 15 && !p.charms.count("locker")) { p.charms.insert("locker"); lines.push_back("New Salt Charm: Lucky Locker (tide 15)"); }
    if (s.tide >= 25 && !p.charms.count("shares")) { p.charms.insert("shares"); lines.push_back("New Salt Charm: Fair Shares (tide 25)"); }
    // records
    auto best = [&](std::map<std::string, int>& m, int v, bool lower) { auto it = m.find(s.map); if (it == m.end() || (lower ? v < it->second : v > it->second)) { m[s.map] = v; return true; } return false; };
    if (best(p.bestTide, s.tide, false)) lines.push_back(TextFormat("New record: tide %d", s.tide));
    best(p.bestScrip, s.scrip, false);
    best(p.bestTimeS, (int)s.timeS, false);
    if (s.forgeAtS >= 0) best(p.forgeTimeS, (int)s.forgeAtS, true);
    // skin crates (skins.h) at the match's milestones: tide 10, 20 and 30, and the boss
    {
        int crates = (s.tide >= 10) + (s.tide >= 20) + (s.tide >= 30) + (s.bossKilled ? 1 : 0);
        if (crates) { skins::AwardCrates(skins::REDTIDE, crates); lines.push_back(TextFormat("Skin crates: %d (the Wardrobe)", crates)); }
    }
    int r0 = p.Rank();
    p.tokens += tok; p.earned += tok;
    if (p.Rank() > r0) lines.push_back(TextFormat("Rank %d", p.Rank()));
    p.ApplyRankUnlocks(&lines);
    SaveProfile();
    if (tokensOut) *tokensOut = tok;
    return lines;
}

} // namespace rt
