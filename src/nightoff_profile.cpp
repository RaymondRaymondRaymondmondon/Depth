// A Night Off: the profile and the bartender's memory (doc pp. 19, 20, 26, 30), stage 9. Headless.
//  The profile file (nightoff_profile.txt next to the exe, gitignored) keeps nights played, headlines earned, kidneys
//  lost and won, the bartender's memory of you (tabs paid, the one you didn't, his window), feuds and friends among the
//  regulars (three nights; a round bought clears a feud), and tomorrow's carry-overs: the kidney (one night down one;
//  the Uber note delivers it the morning after), a black eye, the hangover, and a debt (the cartel visits).
#include "nightoff.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace no {

NightProfile LoadNightProfile(const std::string& path) {
    NightProfile pr; std::ifstream f(path); std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k;
        if (k == "name") { std::getline(s, pr.name); if (!pr.name.empty() && pr.name[0] == ' ') pr.name.erase(0, 1); }
        else if (k == "nights") s >> pr.nights; else if (k == "best") s >> pr.best; else if (k == "total") s >> pr.total;
        else if (k == "kidneys") s >> pr.kidneysLost >> pr.kidneysWon >> pr.kidneyNights;
        else if (k == "bar" || k == "bar1") { int b = k == "bar1" ? 1 : 0, w = 0; s >> pr.tabsPaid[b] >> pr.owed[b] >> w; pr.shotWindow[b] = w != 0; }
        else if (k == "carry") { int e = 0; s >> e >> pr.hangover >> pr.debt; pr.blackEye = e != 0; }
        else if (k == "tokens") s >> pr.tokens >> pr.spins;
        else if (k == "skins") { unsigned long long m = 0; s >> std::hex >> m >> std::dec >> pr.skin; pr.skins = m; }
        else if (k == "seasons") s >> pr.seasonsDone;
        else if (k == "best_headline") { std::getline(s, pr.bestHeadline); if (!pr.bestHeadline.empty() && pr.bestHeadline[0] == ' ') pr.bestHeadline.erase(0, 1); }
        else if (k == "headline") { std::string h; std::getline(s, h); if (!h.empty() && h[0] == ' ') h.erase(0, 1); pr.headlines.push_back(h); }
        else if (k == "feud" || k == "friend") { int n = 0; s >> n; std::string who; std::getline(s, who); if (!who.empty() && who[0] == ' ') who.erase(0, 1); (k == "feud" ? pr.feuds : pr.friends).push_back({who, n}); }
    }
    return pr;
}
void SaveNightProfile(const NightProfile& pr, const std::string& path) {
    std::ofstream f(path);
    f << "name " << pr.name << "\nnights " << pr.nights << "\nbest " << pr.best << "\ntotal " << pr.total << "\n";
    f << "kidneys " << pr.kidneysLost << " " << pr.kidneysWon << " " << pr.kidneyNights << "\n";
    for (int b = 0; b < BAR_COUNT; b++) f << (b ? "bar1 " : "bar ") << pr.tabsPaid[b] << " " << pr.owed[b] << " " << (pr.shotWindow[b] ? 1 : 0) << "\n";
    f << "carry " << (pr.blackEye ? 1 : 0) << " " << pr.hangover << " " << pr.debt << "\n";
    f << "tokens " << pr.tokens << " " << pr.spins << "\n" << "skins " << std::hex << (unsigned long long)pr.skins << std::dec << " " << pr.skin << "\n" << "seasons " << pr.seasonsDone << "\n";
    if (!pr.bestHeadline.empty()) f << "best_headline " << pr.bestHeadline << "\n";
    for (const auto& h : pr.headlines) f << "headline " << h << "\n";
    for (const auto& x : pr.feuds) f << "feud " << x.second << " " << x.first << "\n";
    for (const auto& x : pr.friends) f << "friend " << x.second << " " << x.first << "\n";
}
// a compact one-line form for the host (what the night needs, nothing else)
std::string ProfileSummary(const NightProfile& pr) {
    std::ostringstream s;
    s << pr.nights << "|" << pr.kidneyNights << "|" << pr.tabsPaid[0] << "|" << pr.owed[0] << "|" << (pr.shotWindow[0] ? 1 : 0) << "|" << (pr.blackEye ? 1 : 0) << "|" << pr.hangover << "|" << pr.debt << "|";
    for (const auto& x : pr.feuds) s << x.first << ";"; s << "|";
    for (const auto& x : pr.friends) s << x.first << ";";
    s << "|" << pr.tabsPaid[1] << "|" << pr.owed[1] << "|" << (pr.shotWindow[1] ? 1 : 0);   // (Celeste's memory)
    std::string bh = pr.bestHeadline; for (char& c : bh) if (c == '|') c = '/';
    s << "|" << (OwnsSkin(pr, pr.skin) ? pr.skin : -1) << "|" << bh.substr(0, 120);   // (the skin everyone sees; the Headline costume's front page)
    return s.str();
}
bool ParseProfileSummary(const std::string& str, NightProfile& pr) {
    std::vector<std::string> f; std::string cur; for (char c : str) { if (c == '|') { f.push_back(cur); cur.clear(); } else cur += c; } f.push_back(cur);
    if (f.size() < 10) return false;
    try {
        pr.nights = std::stoi(f[0]); pr.kidneyNights = std::stoi(f[1]); pr.tabsPaid[0] = std::stoi(f[2]); pr.owed[0] = std::stof(f[3]); pr.shotWindow[0] = f[4] == "1"; pr.blackEye = f[5] == "1"; pr.hangover = std::stof(f[6]); pr.debt = std::stof(f[7]);
        if (f.size() >= 13) { pr.tabsPaid[1] = std::stoi(f[10]); pr.owed[1] = std::stof(f[11]); pr.shotWindow[1] = f[12] == "1"; }
        if (f.size() >= 15) { pr.skin = std::stoi(f[13]); if (pr.skin < -1 || pr.skin >= (int)Skins().size()) pr.skin = -1; if (pr.skin >= 0) pr.skins |= 1ull << pr.skin; pr.bestHeadline = f[14]; }
    } catch (...) { return false; }
    auto names = [](const std::string& s, std::vector<std::pair<std::string, int>>& out) { out.clear(); std::string c; for (char ch : s) { if (ch == ';') { if (!c.empty()) out.push_back({c, 3}); c.clear(); } else c += ch; } };
    names(f[8], pr.feuds); names(f[9], pr.friends);
    for (float& o : pr.owed) o = std::clamp(o, 0.0f, 5000.0f); pr.debt = std::clamp(pr.debt, 0.0f, 5000.0f); pr.hangover = std::clamp(pr.hangover, 0.0f, 0.5f);
    return true;
}

// ---------------------------------------------------------------- tonight, from the profile
// this bar's bartender's memory of you (the Gull's, or Celeste's): the unpaid tab at the door, the window, the regular's price
void Night::ApplyBarMemory(Player& p, const NightProfile& pr) {
    const int b = CurBar();
    p.priceMul = 1; p.owedAtDoor = 0;
    // the bartender's memory: a cold eye for an unpaid tab (pay double at the door, or you're barred) and for his window; a warm one for a regular who pays
    if (pr.owed[b] > 0) { p.owedAtDoor = pr.owed[b] * 2; p.barred = true; Say(TextFormat("The bouncer, to %s: \"You owe the Gull %.0f. Double, or you drink water.\"", p.name.c_str(), pr.owed[b])); }
    if (b == BAR_MONKEY && p.barred) { p.pos = {19.5f, -1.6f}; p.ropeIn = true; Say("Horace steps in front of " + p.name + " at the velvet rope."); }   // (the Monkey: barred means the street)
    if (pr.shotWindow[b]) { p.priceMul = 1.2f; Say("The bartender looks at " + p.name + ", then at his window. Prices are up for you tonight."); }
    else if (pr.tabsPaid[b] >= 3) { p.priceMul = 0.9f; Say("The bartender nods at " + p.name + ". \"The usual.\" (A regular's price.)"); }
}
void Night::ApplyProfile(Player& p, const NightProfile& pr) {
    const int b = CurBar();   // (this bar's bartender remembers; the other's memory waits for you there)
    // the kidney: one night down a kidney; the night after, it's back (the Uber note)
    if (pr.kidneyNights >= 2) { p.kidneys = 1; Note(p, 0, "You're a kidney short tonight. Every drink counts double."); }
    p.kidneysAtStart = p.kidneys;
    if (pr.blackEye) { p.blackEye = true; Note(p, 0, "A black eye from last night."); }
    if (pr.hangover > 0) { p.charBuff = -pr.hangover; p.charBuffT = 60 * SECONDS_PER_GAME_MINUTE; Note(p, 0, "Last night's hangover: charisma down for the first hour."); }
    ApplyBarMemory(p, pr);
    // the skin everyone sees (and its jokes and nods)
    if (OwnsSkin(pr, pr.skin)) {
        p.skin = pr.skin; p.skinText = pr.bestHeadline;
        const std::string& k = Skins()[p.skin].key;
        if (k == "dockhand") for (auto& c : patrons) if (c.name == "Tam the Cook") c.mood = std::max(c.mood, 75.0f);
        if (k == "croupier") for (auto& c : patrons) if (c.name == "Anselm") c.mood = std::max(c.mood, 80.0f);
        if (k == "captain") Say("Captain Vane, across the room, to " + p.name + ": \"Nice coat. Darts. Now.\"");
        if (k == "quietman") Say("The room goes quiet as " + p.name + " walks in. Someone puts down a glass very carefully.");
        if (k == "gull") Say("The bartender looks at " + p.name + "'s apron, and says nothing for a long time.");
    }
    // the regulars remember (three nights)
    for (auto& c : patrons) {
        for (const auto& x : pr.feuds) if (c.name == x.first) c.mood = std::min(c.mood, 30.0f);   // (cold tonight; tonight's own insults decide whether it carries on)
        for (const auto& x : pr.friends) if (c.name == x.first) { c.friendOf |= (uint8_t)(1u << std::clamp(p.id, 0, 7)); c.mood = std::max(c.mood, 75.0f); }
    }
    // a debt: the cartel will come for it
    if (pr.debt > 0) { p.debt = pr.debt; if (EventIndex("cartel") < 0 && !lockIn) ForceEvent("cartel", 21.0f + Rand() * 2); Note(p, 0, TextFormat("You owe the cartel %.0f.", pr.debt)); }
}
// ---------------------------------------------------------------- tomorrow, from tonight
std::vector<std::string> Night::ProfileAfter(const Player& p, NightProfile& pr) const {
    const int b = CurBar();
    std::vector<std::string> L;
    int score = Score(p);
    NightTokens(*this, p, pr, L);   // (tokens: before tonight's headline is remembered, so a first is a first)
    if (score > pr.best || pr.bestHeadline.empty()) pr.bestHeadline = Headline();
    pr.nights++; pr.total += score; pr.best = std::max(pr.best, score);
    if (p.lostGold) { pr.skins &= ~(1ull << SkinIndex("goldkidney")); if (pr.skin == SkinIndex("goldkidney")) pr.skin = -1; L.push_back("The Golden Kidney costume is gone. You still have the real ones."); }
    std::string h = Headline(); if (!h.empty()) { pr.headlines.push_back(h); if (pr.headlines.size() > 20) pr.headlines.erase(pr.headlines.begin()); }
    // the kidney
    if (pr.kidneyNights >= 2) { pr.kidneyNights = 1; }
    else if (pr.kidneyNights == 1) { pr.kidneyNights = 0; L.push_back("An Uber note on the door this morning: your kidney, on ice. It's back where it belongs."); }
    if (p.kidneys < p.kidneysAtStart) { pr.kidneysLost++; pr.kidneyNights = 2; L.push_back("You'll be a kidney short tomorrow night. The note says it'll come back the morning after."); }
    else if (p.kidneys > p.kidneysAtStart) { pr.kidneysWon++; pr.kidneyNights = 0; L.push_back("Won a kidney back. The note can stay in its envelope."); }
    // the bartender's memory
    if (p.owedAtDoor > 0 && p.barred) { L.push_back("You never paid the bouncer. The Gull remembers."); }
    else if (p.owedAtDoor <= 0 && pr.owed[b] > 0) pr.owed[b] = 0;
    if (p.tab > 0) { pr.owed[b] += p.tab; L.push_back(TextFormat("The bartender will remember the %.0f you didn't pay.", p.tab)); }
    else if (p.drinks > 0) { pr.tabsPaid[b]++; if (pr.tabsPaid[b] == 3) L.push_back("Three tabs paid: the bartender knows your usual now."); }
    bool window = false; for (const auto& m : p.log) window |= m.text.find("window") != std::string::npos && (m.text.find("Broke") != std::string::npos || m.text.find("shotgun") != std::string::npos);
    for (const auto& m : p.log) window |= m.text.find("Fired the bartender's shotgun") != std::string::npos;
    if (window) { pr.shotWindow[b] = true; L.push_back("The bartender will remember his window."); }
    // tomorrow's face and head
    pr.blackEye = p.fight.knockouts > 0 || p.ending == E_KNOCKED_OUT;
    if (pr.blackEye) L.push_back("A black eye for tomorrow night.");
    pr.hangover = p.peakDrunk >= 80 ? 0.15f : p.peakDrunk >= 60 ? 0.08f : 0.0f;
    if (pr.hangover > 0) L.push_back("A hangover for the first hour tomorrow.");
    pr.debt = p.debt;
    if (pr.debt > 0) L.push_back(TextFormat("You still owe the cartel %.0f. They'll be in.", pr.debt));
    // the regulars: feuds and friends last three nights (a round clears a feud)
    for (auto& x : pr.feuds) x.second--; for (auto& x : pr.friends) x.second--;
    pr.feuds.erase(std::remove_if(pr.feuds.begin(), pr.feuds.end(), [](const std::pair<std::string, int>& x) { return x.second <= 0; }), pr.feuds.end());
    pr.friends.erase(std::remove_if(pr.friends.begin(), pr.friends.end(), [](const std::pair<std::string, int>& x) { return x.second <= 0; }), pr.friends.end());
    bool round = p.roundT > 0;
    if (round && !pr.feuds.empty()) { pr.feuds.clear(); L.push_back("A round bought: the old feuds are forgotten."); }
    for (const auto& c : patrons) {
        if (c.reg < 0 || p.id >= (int)c.mem.size()) continue;
        const Memory& m = c.mem[p.id];
        auto add = [&](std::vector<std::pair<std::string, int>>& v, const char* what) { for (auto& x : v) if (x.first == c.name) { x.second = 3; return; } v.push_back({c.name, 3}); L.push_back(c.name + " " + what); };
        if (m.fights > 0 || m.insults >= 2) add(pr.feuds, "will remember you (a feud, three nights).");
        else if ((c.friendOf >> std::clamp(p.id, 0, 7)) & 1) add(pr.friends, "counts you a friend now.");
    }
    return L;
}

// ---------------------------------------------------------------- --night-test's stage 9 checks (the profile across three nights)
int NightProfileChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    NightProfile pr; pr.name = "Tester";
    // night one: a kidney lost, a tab left, a fight lost, a feud
    {
        Night n; Opts o; o.seed = 1; o.events = false; n.Init(o); Player& p = n.players[0]; n.ApplyProfile(p, pr);
        p.kidneys = 1; p.tab = 40; p.money = 0; p.fight.knockouts = 1; p.peakDrunk = 85; p.drinks = 6;
        for (auto& c : n.patrons) if (c.name == "Boxer Mags") c.mem[0].fights = 1;
        n.Leave(p, E_CLOSING, ""); n.over = true;
        auto lines = n.ProfileAfter(p, pr);
        check(pr.kidneyNights == 2 && pr.owed[0] > 0 && pr.blackEye && pr.hangover > 0 && !pr.feuds.empty(), TextFormat("night one remembered: %d lines (a kidney, a tab of %.0f, a black eye, a hangover, a feud)", (int)lines.size(), pr.owed[0]));
    }
    // the file round-trips
    { std::string path = "nightoff_profile_test.txt"; SaveNightProfile(pr, path); NightProfile q = LoadNightProfile(path); std::remove(path.c_str());
      check(q.kidneyNights == pr.kidneyNights && q.owed[0] == pr.owed[0] && q.blackEye == pr.blackEye && q.feuds.size() == pr.feuds.size() && q.nights == 1, "the profile file round-trips");
      NightProfile s; check(ParseProfileSummary(ProfileSummary(pr), s) && s.owed[0] == pr.owed[0] && s.kidneyNights == 2 && s.feuds.size() == pr.feuds.size(), "and so does the summary a guest sends"); }
    // night two: a kidney short, barred till you pay double, Boxer Mags remembers; pay at the door
    {
        Night n; Opts o; o.seed = 2; o.events = false; n.Init(o); Player& p = n.players[0]; p.money = 200; n.ApplyProfile(p, pr);
        int mags = -1; for (auto& c : n.patrons) if (c.name == "Boxer Mags") mags = c.id;
        check(p.kidneys == 1 && p.barred && p.owedAtDoor > 0 && p.blackEye && p.charBuffT > 0, "night two: a kidney short, a black eye, a hangover, and the bouncer wants double");
        check(mags >= 0 && n.patrons[mags].mood <= 30, "and Boxer Mags remembers");
        // pay the bouncer (an event option at the door)
        p.pos = D().bar.spawn; bool offered = false; for (const auto& op : n.EventOptions(p)) offered |= op.act == 50;
        p.in.evAct = 50; n.Step(0.02f);
        check(offered && !p.barred && p.owedAtDoor == 0, TextFormat("paying double at the door lifts the bar (%.0f left)", p.money));
        p.roundT = n.t; p.drinks = 2; p.tab = 0; n.Leave(p, E_WALKED, ""); n.over = true;
        auto lines = n.ProfileAfter(p, pr);
        check(pr.kidneyNights == 1 && pr.owed[0] == 0 && pr.feuds.empty(), "a round clears the feud; the tab's square");
    }
    // night three: the kidney's back
    {
        Night n; Opts o; o.seed = 3; o.events = false; n.Init(o); Player& p = n.players[0]; n.ApplyProfile(p, pr);
        p.drinks = 1; n.Leave(p, E_WALKED, ""); n.over = true;
        auto lines = n.ProfileAfter(p, pr);
        bool note = false; for (const auto& l : lines) note |= l.find("Uber note") != std::string::npos;
        check(p.kidneys == 2 && note && pr.kidneyNights == 0 && pr.nights == 3, "night three: the Uber note, and the kidney's back");
    }
    return fails;
}

}  // namespace no
