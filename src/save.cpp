#include "sound.h"
// ============================================================================
//  DEPTH - saving and loading. The game autosaves whenever you return to the
//  Nautilus and when you quit, to depth_save.txt next to the executable.
//  The format is plain text, one "key values..." line per item, so it's easy
//  to read and survives new fields being added (unknown keys are skipped).
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

static const int SAVE_VERSION = 1;

std::string SavePath() { return std::string(GetApplicationDirectory()) + "depth_save.txt"; }

bool SaveGame(const Game& g) {
    std::string tmp = SavePath() + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return false;
        f << "depth_save " << SAVE_VERSION << "\n";
        f << "gold " << g.gold << "\n";
        f << "batteries " << g.batteries << "\n";
        f << "provision"; for (int i = 0; i < SUP_COUNT; i++) f << " " << g.provision[i]; f << "\n";
        f << "nextHeroId " << g.nextHeroId << "\n";
        f << "party";
        for (int id : g.party) f << " " << id;
        f << "\nupgrades";
        for (int u : g.upgrades) f << " " << u;
        f << "\nrelics";
        for (int r : g.relicStorage) f << " " << r;
        f << "\nlocations";
        for (int l = 0; l < LOCATION_COUNT; l++) f << " " << g.tierCleared[l] << " " << g.tierSel[l];
        f << "\n";
        f << "platopts " << (g.platHard ? 1 : 0) << " " << (g.platCheckpoints ? 1 : 0) << " " << (g.platHullBoss ? 1 : 0) << " " << (g.platPirateBoss ? 1 : 0) << "\n";
        f << "abyss " << (g.abyssCleared ? 1 : 0) << " " << g.abyssBest << "\n";
        f << "audio " << Volumes().master << " " << Volumes().music << " " << Volumes().sfx << " " << Volumes().ambience << "\n";
        f << "seen"; for (int l = 0; l <= PL_COUNT; l++) f << " " << g.platSeen[l]; f << "\n";
        for (int l = 0; l < PL_COUNT; l++) {
            f << "plat " << l << " " << (g.platCleared[l] ? 1 : 0) << " " << g.platBest[l];
            for (int c : g.platLayouts[l]) f << " " << c;
            f << "\n";
        }
        for (const Hero& h : g.roster) {
            f << "hero " << h.id << " " << (int)h.cls << " " << h.level << " " << h.xp << " " << h.hp << " " << h.stress << " "
              << (h.rattled ? 1 : 0) << " " << h.onLeave << " " << h.relics[0] << " " << h.relics[1];
            for (int a : h.loadout) f << " " << a;
            f << " " << h.name << "\n";
        }
        for (const Hero& h : g.roster) { f << "herox " << h.id << " " << h.habits << " " << h.habitLocked << " " << h.ailments << " " << h.vigor << " " << h.might << " " << h.quickness << " " << h.fortitude; for (int d : h.drill) f << " " << d; f << "\n"; }
        for (auto& [k, v] : g.bonds) if (v > 0) f << "bond " << k.first << " " << k.second << " " << v << "\n";
    }
    std::remove(SavePath().c_str());
    return std::rename(tmp.c_str(), SavePath().c_str()) == 0; // replace the old save only once the new one is complete
}

bool LoadGame(Game& g) {
    std::ifstream f(SavePath());
    if (!f) return false;
    std::string line, key;
    int version = 0;
    if (!std::getline(f, line) || sscanf(line.c_str(), "depth_save %d", &version) != 1) return false;
    Game fresh;
    fresh.roster.clear();
    fresh.relicStorage.clear();
    while (std::getline(f, line)) {
        std::istringstream in(line);
        in >> key;
        if (key == "gold") in >> fresh.gold;
        else if (key == "batteries") in >> fresh.batteries;
        else if (key == "provision") { for (int i = 0; i < SUP_COUNT; i++) in >> fresh.provision[i]; }
        else if (key == "nextHeroId") in >> fresh.nextHeroId;
        else if (key == "party") for (int& id : fresh.party) in >> id;
        else if (key == "upgrades") for (int& u : fresh.upgrades) in >> u;
        else if (key == "relics") { int r; while (in >> r) if (r >= 0 && r < (int)Relics().size()) fresh.relicStorage.push_back(r); }
        else if (key == "cave") { in >> fresh.tierCleared[(int)Location::Cave] >> fresh.tierSel[(int)Location::Cave]; } // an older save: Cave only
        else if (key == "locations") for (int l = 0; l < LOCATION_COUNT; l++) in >> fresh.tierCleared[l] >> fresh.tierSel[l];
        else if (key == "platopts") {
            int h = 0, c = 0, hb = 1, pb = 1;
            in >> h >> c >> hb >> pb; // hb/pb default to 1 (on) for saves from before this option existed
            fresh.platHard = h != 0; fresh.platCheckpoints = c != 0; fresh.platHullBoss = hb != 0; fresh.platPirateBoss = pb != 0;
        }
        else if (key == "audio") { AudioVolumes& V = Volumes(); in >> V.master >> V.music >> V.sfx >> V.ambience; }
        else if (key == "seen") { for (int l = 0; l <= PL_COUNT; l++) in >> fresh.platSeen[l]; }
        else if (key == "abyss") {
            int cleared = 0; float best = 0;
            in >> cleared >> best;
            fresh.abyssCleared = cleared != 0; fresh.abyssBest = best;
        }
        else if (key == "plat") {
            int l, cleared, c;
            float best;
            if (!(in >> l >> cleared >> best) || l < 0 || l >= PL_COUNT) continue;
            fresh.platCleared[l] = cleared != 0;
            fresh.platBest[l] = best;
            fresh.platLayouts[l].clear();
            while (in >> c) fresh.platLayouts[l].push_back(c);
        } else if (key == "hero") {
            Hero h;
            int cls, rattled;
            in >> h.id >> cls >> h.level >> h.xp >> h.hp >> h.stress >> rattled >> h.onLeave >> h.relics[0] >> h.relics[1];
            for (int& a : h.loadout) in >> a;
            in >> h.name;
            if (!in || cls < 0 || cls >= (int)HeroClass::COUNT) continue;
            h.cls = (HeroClass)cls;
            h.rattled = rattled != 0;
            for (int& r : h.relics) if (r >= (int)Relics().size()) r = -1;
            fresh.roster.push_back(h);
        }
        else if (key == "herox") { // Stage 7: habits, ailments, and the recruit's own build
            int id; unsigned hb = 0, lk = 0, ai = 0; int v = 0, m = 0, q = 0, fo = 0;
            if (!(in >> id >> hb >> lk >> ai)) continue;
            in >> v >> m >> q >> fo;
            int dr[8] = {}; bool haveDrill = true; for (int& d : dr) if (!(in >> d)) haveDrill = false;
            for (auto& h : fresh.roster) if (h.id == id) {
                h.habits = hb; h.habitLocked = lk; h.ailments = ai; h.vigor = v; h.might = m; h.quickness = q; h.fortitude = fo;
                if (haveDrill) for (int k = 0; k < 8; k++) h.drill[k] = std::clamp(dr[k], 0, DRILL_MAX);
            }
        }
        else if (key == "bond") { int a, b, v; if (in >> a >> b >> v) fresh.bonds[{std::min(a, b), std::max(a, b)}] = std::clamp(v, 0, BOND_MAX); }
    }
    if (fresh.roster.empty()) return false;
    for (int& u : fresh.upgrades) u = std::clamp(u, 0, UPGRADE_MAX);
    for (int l = 0; l < LOCATION_COUNT; l++) fresh.tierSel[l] = std::clamp(fresh.tierSel[l], 0, std::min(CAVE_TIERS - 1, fresh.tierCleared[l] + 1));
    g = fresh;
    CompactParty(g);
    RefreshRadar(g);
    for (int l = 0; l < PL_COUNT; l++) if (!PlatLayoutValid(g, l)) GeneratePlatLayout(g, l); // e.g. from an older version
    return true;
}

void DeleteSave() { std::remove(SavePath().c_str()); }
