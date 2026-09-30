// Red Tide's arcade profile (stage 9): tokens and the 50 ranks, what they unlock (Salt Charms, skins, charm pouch
// slots, bonus dossier pages), per-map records and firsts, the dossier pages a diver has earned, the cosmetics owned
// and worn, and the charm pouch. Nothing here changes in-match power except which charms are brought.
// Saved to redtide_profile.txt next to the executable, apart from the Nautilus's save.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rt {

struct CharmDef { std::string id, name, effect; };
const std::vector<CharmDef>& Charms();                  // the ten Salt Charms
struct Cosmetic { std::string id, kind, name; int cost = 150; };   // kind: finish, suit, helmet
const std::vector<Cosmetic>& Cosmetics();

struct Profile {
    int tokens = 0;                 // spendable
    int earned = 0;                 // lifetime (sets the rank)
    std::set<std::string> charms;   // unlocked Salt Charm ids
    std::set<std::string> owned;    // cosmetic ids bought or unlocked
    std::set<std::string> firsts;   // "map:tide20", "map:boss", "map:quest", "map:dossier"
    std::set<std::string> dossier;  // "map|name" pages earned (kill or 30 s of watching)
    std::set<std::string> bonus;    // bonus pages: "owners", "expedition", "station", "finallog", ...
    std::map<std::string, int> bestTide, bestScrip, bestTimeS, forgeTimeS;
    std::string suit = "", helmet = "", finish = "";      // worn cosmetics ("" = the issue kit)
    std::vector<std::string> pouch;                       // charms brought into a match (up to PouchSlots())
    int Rank() const;               // 1..50
    int NextRankAt() const;         // lifetime tokens to the next rank (-1 at 50)
    int PouchSlots() const;         // 1, +1 at ranks 7, 15, 25 and 40 (max 5)
    void ApplyRankUnlocks(std::vector<std::string>* news = nullptr);
};
Profile& GetProfile();
void LoadProfile();
void SaveProfile();
std::string ProfilePath();

// The match's end: tokens by the progression sheet (a match 10, tide 10+ 25, 2 per tide past 10, firsts), records,
// milestone charms (Lucky Locker at tide 15, Fair Shares at 25), and the dossier pages earned. Returns the lines
// for the results panel.
struct MatchSummary {
    std::string map; int tide = 1, scrip = 0; float timeS = 0; float forgeAtS = -1;
    bool bossKilled = false, questDone = false;
    std::vector<std::string> pages;            // dossier page names earned this match
    std::vector<std::string> bonusPages;
};
std::vector<std::string> AwardMatch(const MatchSummary& s, int* tokensOut = nullptr);
int DossierTotal(const std::string& map);     // pages on a map (the workbook's dossier sheet + the faction)

} // namespace rt
