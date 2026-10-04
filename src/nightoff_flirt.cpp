// A Night Off: flirting and going home, the bartender's warning, and the morning after (doc pp. 13-14, 3-4, 20),
// stage 5. Headless: the flirt mini-game (an opening line that fits the patron's traits, two exchanges, the offer),
// where going home ends up (each regular carries it; the kidney thieves are the best flirts), the dog and a teammate
// at the door, the scoreboard's points, the headline generator and each player's story.
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace no {

// ---------------------------------------------------------------- data (nightoff_dialogue.json's "flirt", nightoff_scoring.json)
struct FlirtLines {
    std::vector<std::string> options; std::map<std::string, std::vector<std::string>> you, ok, fail;
    std::vector<std::string> drunk, thiefOk, offer, decline, endBad; std::map<std::string, std::string> tells;
};
struct HomeDef { std::string key, morning; float odds = 0; bool good = true; };
struct Headline { std::string when, text; };
struct ScoringData {
    float moneyPer = 10, gameWon = 20, fightWon = 30, fightSober = 60, homeGood = 100, homeBad = 50, eventSurvived = 50, story = 25, bothKidneys = 50, walkedSober = 40, debtPer10 = -5;
    std::vector<HomeDef> home; std::vector<Headline> headlines; std::vector<std::string> things;
    std::map<std::string, std::vector<std::string>> woke;
};
static const char* KEYS[6] = {"compliment", "joke", "drink", "dance", "ask", "lean"};
static std::vector<std::string> Strs(const Json& a) { std::vector<std::string> v; for (const Json& s : a.a) v.push_back(s.Str0()); return v; }
static const FlirtLines& FL() {
    static FlirtLines f = [] {
        FlirtLines f; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_dialogue.json"); const Json& x = j["flirt"];
        f.options = Strs(x["options"]);
        for (const char* k : KEYS) { f.you[k] = Strs(x["you"][k]); f.ok[k] = Strs(x["ok"][k]); f.fail[k] = Strs(x["fail"][k]); }
        f.drunk = Strs(x["drunk"]); f.thiefOk = Strs(x["thief_ok"]); f.offer = Strs(x["offer"]); f.decline = Strs(x["decline"]); f.endBad = Strs(x["end_bad"]);
        for (const auto& kv : x["tells"].o) f.tells[kv.first] = kv.second.Str0();
        return f;
    }();
    return f;
}
static const ScoringData& SD() {
    static ScoringData s = [] {
        ScoringData s; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_scoring.json"); const Json& p = j["points"];
        s.moneyPer = p["money_per"].F(10); s.gameWon = p["game_won"].F(20); s.fightWon = p["fight_won"].F(30); s.fightSober = p["fight_won_sober"].F(60);
        s.homeGood = p["home_good"].F(100); s.homeBad = p["home_bad"].F(50); s.eventSurvived = p["event_survived"].F(50); s.story = p["story"].F(25);
        s.bothKidneys = p["both_kidneys"].F(50); s.walkedSober = p["walked_sober"].F(40); s.debtPer10 = p["debt_per_10"].F(-5);
        for (const Json& h : j["home"].a) s.home.push_back({h["key"].Str0(), h["morning"].Str0(), h["odds"].F(0), h["good"].Bool0()});
        for (const Json& h : j["headlines"].a) s.headlines.push_back({h["when"].Str0(), h["text"].Str0()});
        s.things = Strs(j["headline_things"]);
        for (const auto& kv : j["woke"].o) s.woke[kv.first] = Strs(kv.second);
        return s;
    }();
    return s;
}
static const std::string& Pick(const std::vector<std::string>& v, uint32_t k) { static const std::string none = "..."; return v.empty() ? none : v[k % v.size()]; }
static const HomeDef* HomeOf(const std::string& key) { for (const auto& h : SD().home) if (h.key == key) return &h; return nullptr; }
static std::string Upper(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }
static std::string Replace(std::string s, const std::string& a, const std::string& b) { for (size_t i = s.find(a); i != std::string::npos; i = s.find(a, i + b.size())) s.replace(i, a.size(), b); return s; }
void Night::Flag(const std::string& key, const std::string& who) { flags.push_back({key, who}); }

// ---------------------------------------------------------------- the flirt mini-game
float Night::FlirtOdds(const Player& p, const Patron& c, int o) const {
    const Data& d = D(); auto has = [&](const char* t) { return c.Has(d.Trait(t)); };
    static const float BASE[6] = {0, -5, 5, 0, 5, -10};
    float x = 50 + (Charisma(p) * 100 - 100) + BASE[std::clamp(o, 0, 5)];
    // the line's fit: a romantic patron wants a compliment, a flirty one a joke, a suspicious one a drink first, a loud one a dance
    if (has("romantic") && o == 0) x += 25;
    if (has("flirty") && o == 1) x += 25;
    if (has("suspicious")) x += (o == 2 && p.flirt.round == 0) ? 25 : (p.flirt.round == 0 ? -20 : 0);
    if (has("loud") && o == 3) x += 25;
    if (has("honest") && o == 5) x -= 10;
    if (c.home == "kidney") x += 30;                                   // (the kidney thieves are the best flirts in the bar)
    if (c.home == "married") x -= 10;
    x += c.mood >= 80 ? 15 : c.mood < 40 ? -15 : 0;
    x += AfterFightCharisma(p, c) * 100;
    if (p.wareT[W_SIREN] > 0) x += 30;                                // (the Siren: every threshold -30)
    return std::clamp(x / 100, 0.05f, 0.95f);
}
void Night::StartFlirt(Player& p, int idx) {
    if (idx < 0 || idx >= (int)patrons.size() || p.flirt.patron >= 0 || p.talk.patron >= 0 || p.st != State::Active) return;
    Patron& c = patrons[idx];
    if (!c.inside || c.gone || c.talkingTo >= 0 || c.playing >= 0 || c.fight.brawl >= 0 || c.type == T_STAFF || c.home == "none" || Vector2Distance(c.pos, p.pos) > 2.4f) return;
    Flirt F; F.patron = idx;
    // a packed bar after 10 p.m. lowers the bar by one; the kidney thieves make their offer early
    F.need = (opts.crowd == 2 && Hour() >= 22) || c.home == "kidney" ? 2 : 3;
    if (p.wareT[W_SIREN] > 0) F.need = std::max(1, F.need - 1);
    if (fireworksT > 0) F.need = std::max(1, F.need - 1);   // (the Festival's fireworks: every flirt one step easier)   // (the Siren: the offer comes early)
    c.talkingTo = p.id;
    if (EventOn("wake")) {   // flirting at a wake is -30 with everyone, and the Reverend notices
        for (auto& o : patrons) if (o.inside && !o.gone && Vector2Distance(o.pos, p.pos) < 12) o.mood = std::max(0.0f, o.mood - 30);
        for (auto& o : patrons) if (o.name == "The Reverend" && o.inside) o.mood = 0;
        Say("Flirting. At a wake. The Reverend has noticed.");
    }
    if (c.mood < 20) { F.theirLine = "Not tonight, sailor. Not ever."; F.over = true; F.overT = 1.8f; F.result = c.name + " isn't interested."; }
    else F.theirLine = c.type == T_FLIRT ? "Well, hello, sailor." : "Oh? Go on, then.";
    p.flirt = F;
    if (p.id < (int)c.mem.size()) c.mem[p.id].flirts += 1;
}
void Night::FlirtChoose(Player& p, int o) {
    Flirt& F = p.flirt;
    if (F.patron < 0 || F.over || F.offer) return;
    Patron& c = patrons[F.patron];
    if (o == 6) { F.over = true; F.overT = 0.6f; F.result = "You leave it there."; return; }
    // the open has four lines (a compliment, a joke, a drink, a dance); the build, four more (no drink, no dance: ask, lean)
    int opt = F.round == 0 ? std::clamp(o, 0, 3) : (o == 2 ? 4 : o == 3 ? 5 : std::clamp(o, 0, 5));
    if (opt == 2) { float pr = 6; p.tab += pr; c.mood = std::min(100.0f, c.mood + 6); bar.busyT = std::max(bar.busyT, 1.0f); }   // (a drink costs a drink)
    float odds = FlirtOdds(p, c, opt);
    F.substituted = false;
    const FlirtLines& L = FL();
    uint32_t k = (uint32_t)(t * 31) + opt * 7 + F.round;
    // drunk captions: at 60+, what you say is a surprise
    if (p.drunk >= 60 && Rand() < 0.5f) { F.substituted = true; F.myCaption = Pick(L.drunk, k); odds = std::max(0.05f, odds - 0.1f + (Rand() - 0.5f) * 0.4f); }
    else F.myCaption = Pick(L.you.at(KEYS[opt]), k);
    bool win = Rand() < odds;
    F.lastOption = opt; F.lastWin = win; F.round++;
    if (win) { F.wins++; c.mood = std::min(100.0f, c.mood + 5); F.theirLine = c.home == "kidney" ? Pick(L.thiefOk, k + 3) : Pick(L.ok.at(KEYS[opt]), k + 1); }
    else { F.losses++; c.mood = std::max(0.0f, c.mood - 5); F.theirLine = Pick(L.fail.at(KEYS[opt]), k + 2); }
    // a signal a sober player can read: the thief's glance at the toilets' window, a married patron's ring, a sincere laugh
    F.tell.clear();
    if (p.drunk < 40 && Rand() < 0.65f) { auto it = L.tells.find(c.home); if (it != L.tells.end()) F.tell = c.name + " " + it->second + "."; }
    if (F.wins >= F.need) { F.offer = true; F.theirLine = Pick(L.offer, k + 5); }
    else if (F.losses >= 2) { F.over = true; F.overT = 3; F.theirLine = Pick(L.endBad, k + 6); F.result = c.name + " has gone off you."; c.mood = std::max(0.0f, c.mood - 10); }
}
void Night::FlirtOffer(Player& p, bool take) {
    Flirt& F = p.flirt;
    if (F.patron < 0 || !F.offer) return;
    Patron& c = patrons[F.patron];
    if (!take) {   // decline and you keep a friend: they'll back you in a fight
        c.mood = std::min(100.0f, c.mood + 25); c.friendOf |= (uint8_t)(1u << std::clamp(p.id, 0, 7));
        F.offer = false; F.over = true; F.overT = 2.5f; F.theirLine = Pick(FL().decline, (uint32_t)(t * 13)); F.result = c.name + " will have your back tonight.";
        Note(p, 7, "Turned down " + c.name + ", and made a friend.");
        return;
    }
    int idx = F.patron;
    auto cool = std::find(p.items.begin(), p.items.end(), "a kidney cooler");
    if (c.home == "kidney" && cool != p.items.end()) {   // (the Masquerade's prize: you show them a cooler of their own)
        p.items.erase(cool); c.mood = 0; c.leaveH = Hour();
        F.offer = false; F.over = true; F.overT = 3; F.result = c.name + " sees the cooler in your hand and remembers an appointment.";
        Note(p, 5, "Held up an unmasked thief's cooler to " + c.name + ", who fled."); return;
    }
    EndFlirt(p);
    const HomeDef* h = HomeOf(c.home);
    bool bad = h && !h->good;
    // a bad night can be stopped at the door: another player who follows within 30 s (doc p. 14)
    int others = 0; for (const auto& q : players) others += q.id != p.id && (q.st == State::Active || q.st == State::Drinking);
    if (bad && others > 0 && p.wareT[W_SIREN] <= 0) {   /* (under the Siren, nobody talks you out of it) */ p.leavingT = 30; p.leavingWith = idx; c.playing = p.id; c.goal = D().bar.door; c.path = NavPath(c.pos, c.goal); c.nextGoalT = 1e9f; Say(p.name + " leaves with " + c.name + "."); return; }
    GoHome(p, idx, c.home);
}
void Night::EndFlirt(Player& p) {
    if (p.flirt.patron >= 0 && p.flirt.patron < (int)patrons.size()) patrons[p.flirt.patron].talkingTo = -1;
    p.flirt = Flirt{};
}
void Night::GoHome(Player& p, int idx, const std::string& kindIn) {
    std::string kind = kindIn;
    std::string who = idx >= 0 ? patrons[idx].name : "the fortune teller";
    // nobody steals a kidney in front of a dog
    if (p.sureHome && idx >= 0) { kind = "sincere"; p.sureHome = false; }   // (the Cocktail's best night: a sincere go-home, guaranteed)
    if (kind == "kidney" && dog.owner == p.id) { Say("Halfway to the door, " + who + " sees the dog and remembers an appointment."); kind = "dog"; }
    const HomeDef* h = HomeOf(kind);
    p.homeWith = who; p.homeKind = kind; p.homeBad = kind == "dog" || (h && !h->good);
    if (kind == "robbery") { p.items.clear(); p.items.push_back("no coat"); Note(p, 4, "Robbed by " + who + ": the money, the coat, the hat."); }
    if (kind == "kidney") { p.kidneys = 1; Note(p, 5, "Lost a kidney to " + who + "."); Flag("kidney", p.name); }
    if (kind == "married") { p.items.push_back("one shoe"); Note(p, 7, "Went home with " + who + "; their spouse came home early."); Flag("married", p.name); }
    if (kind == "rich") { Flag("rich", p.name); }
    if (kind == "sweet") Flag("home_sweet", p.name);
    if (kind == "fortune") { const auto& deck = GD().deck; p.card = deck.empty() ? "The Diver" : deck[(int)(Rand() * deck.size()) % deck.size()]; p.items.push_back("a tarot card: " + p.card); }
    if (idx >= 0) { Patron& c = patrons[idx]; c.gone = true; c.inside = false; c.talkingTo = -1; c.playing = -1; }   // (a patron who went home with a player is gone for the night)
    Note(p, 7, "Went home with " + who + " at " + Clock() + ".");
    float money = p.money;
    Leave(p, E_HOME_WITH, kind == "robbery" ? "in the alley without your money or your coat" : kind == "kidney" ? "in a bathtub of ice with a note and a stitch" : "at " + who + "'s place");
    if (kind == "robbery") p.money = std::min(p.money, money * 0.0f);
}
// the bartender, for a drink: who's trouble tonight (one true name, one false)
void Night::AskTrouble(Player& p) {
    if (!NearServe(p) || p.st != State::Active) return;
    p.tab += PriceOf(0) > 0 ? PriceOf(0) : 5; bar.mood = std::min(100.0f, bar.mood + 2);
    std::vector<std::string> bad, fine;
    for (const auto& c : patrons) if (!c.gone && (c.inside || c.arriveH < Hour() + 2)) { if (c.home == "kidney" || c.home == "robbery") bad.push_back(c.name); else if (c.type != T_STAFF && c.reg >= 0) fine.push_back(c.name); }
    if (bad.empty()) { Say("The bartender, low: \"Quiet lot tonight. Even me.\""); return; }
    std::string a = bad[(int)(Rand() * bad.size()) % bad.size()], b = fine.empty() ? a : fine[(int)(Rand() * fine.size()) % fine.size()];
    if (Rand() < 0.5f) std::swap(a, b);
    Say("The bartender, low, polishing a glass: \"Keep an eye on " + a + ". And " + b + ".\"");
    Note(p, 6, "The bartender warned about " + a + " and " + b + " (one of them is true).");
}

// ---------------------------------------------------------------- the morning (doc pp. 3-4)
std::vector<Night::ScoreLine> Night::ScoreBreakdown(const Player& p) const {
    if (mirror && p.id >= 0 && p.id < (int)scoreCache.size()) return scoreCache[p.id];   // (a guest draws the host's)
    const ScoringData& s = SD(); std::vector<ScoreLine> L;
    float kept = p.money - 200;
    if (kept > 0) L.push_back({TextFormat("Money kept over your wages (%.0f)", kept), (int)(kept / s.moneyPer)});
    if (p.gamesWon) L.push_back({TextFormat("Games won (%d)", p.gamesWon), (int)(p.gamesWon * s.gameWon)});
    if (p.fightsWon - p.fightsWonSober > 0) L.push_back({TextFormat("Fights won (%d)", p.fightsWon - p.fightsWonSober), (int)((p.fightsWon - p.fightsWonSober) * s.fightWon)});
    if (p.fightsWonSober) L.push_back({TextFormat("Fights won sober (%d)", p.fightsWonSober), (int)(p.fightsWonSober * s.fightSober)});
    if (p.ending == E_HOME_WITH) L.push_back({"Went home with " + p.homeWith + (p.homeBad ? " (it went badly)" : ""), (int)(p.homeBad ? s.homeBad : s.homeGood)});
    if (p.eventsSurvived) L.push_back({TextFormat("Events survived (%d)", p.eventsSurvived), (int)(p.eventsSurvived * s.eventSurvived)});
    int stories = 0; for (const auto& m : p.log) stories += m.kind == 5 && m.text.find("Lost a kidney") == std::string::npos;
    if (stories) L.push_back({TextFormat("Stories (%d)", stories), (int)(stories * s.story)});
    if (p.kidneys >= 2) L.push_back({"Still have both kidneys", (int)s.bothKidneys});
    if (p.ending == E_WALKED && p.drunk < 20) L.push_back({"Walked home sober", (int)s.walkedSober});
    if (p.tab > 0) L.push_back({TextFormat("Unpaid tab (%.0f)", p.tab), (int)(floorf(p.tab / 10) * s.debtPer10)});
    if (p.wager >= 0) {
        bool hit = p.wager == WG_HOME ? p.ending == E_HOME_WITH : p.wager == WG_RICH ? p.money >= 700 : p.wager == WG_SOBER_FIGHT ? p.fightsWonSober > 0 : (p.kidneys >= 2 && p.ending != E_ARRESTED && p.ending != E_HOSPITAL && p.ending != E_KNOCKED_OUT);
        L.push_back({std::string("The wager: ") + WagerName(p.wager) + (hit ? "" : " (lost)"), hit ? 300 : 0});
    }
    if (opts.mode == MD_LAST_STANDING && winner == p.id) L.push_back({"Last one standing", 200});
    return L;
}
int Night::Score(const Player& p) const {
    // The Crew: one shared score; Rival Crews: a score per crew; otherwise your own
    int s = 0;
    for (const auto& q : players) {
        bool counts = q.id == p.id || opts.mode == MD_CREW || (opts.mode == MD_RIVAL_CREWS && q.crew2 == p.crew2);
        if (counts) for (const auto& l : ScoreBreakdown(q)) s += l.points;
    }
    return s;
}
static const char* Words(int n) { static const char* W[] = {"NO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX"}; return n >= 0 && n <= 6 ? W[n] : "MANY"; }
std::string Night::Headline() const {
    if (mirror && !headlineCache.empty()) return headlineCache;
    const ScoringData& s = SD();
    int n = (int)players.size(), passed = 0, sick = 0, walked = 0, drinks = 0, arrested = 0, hosp = 0, soberWins = 0;
    for (const auto& p : players) { passed += p.ending == E_PASSED_OUT; walked += p.ending == E_WALKED; drinks += p.drinks; arrested += p.ending == E_ARRESTED; hosp += p.ending == E_HOSPITAL; soberWins += p.fightsWonSober; for (const auto& m : p.log) sick += m.kind == 2; }
    float bigBill = 0; for (const auto& b : brawls) bigBill = std::max(bigBill, b.bill);
    std::string windowWho; for (const auto& c : patrons) if (c.outForNight) windowWho = c.name;
    auto flagged = [&](const char* k, std::string* who = nullptr) { for (const auto& f : flags) if (f.first == k) { if (who) *who = f.second; return true; } return false; };
    for (const auto& h : s.headlines) {
        std::string who, thing; bool hit = false;
        const std::string& w = h.when;
        if (w == "shotgun") hit = flagged("shotgun", &who);
        else if (w == "kidney") { hit = flagged("kidney", &who); thing = Pick(s.things, (uint32_t)(rng >> 5)); }
        else if (w == "window_body") { hit = !windowWho.empty(); who = windowWho; }
        else if (w == "arrested") hit = arrested > 0;
        else if (w == "rich") hit = flagged("rich", &who);
        else if (w == "hospital") hit = hosp > 0;
        else if (w == "bartender") hit = flagged("bartender", &who);
        else if (w == "big_bill") { hit = bigBill >= 100; thing = TextFormat("%.0f IN DAMAGES", bigBill); }
        else if (w == "one_eighty") hit = flagged("one_eighty", &who);
        else if (w == "hole_in_one") hit = flagged("hole_in_one", &thing);
        else if (w == "slots_kidney") hit = flagged("slots_kidney", &who);
        else if (w == "married") hit = flagged("married", &who);
        else if (w == "sober_win") hit = soberWins > 0;
        else if (w == "all_passed") hit = passed == n;
        else if (w == "sick") hit = sick >= 2;
        else if (w == "fight") hit = !brawls.empty();
        else if (w == "home_sweet") hit = flagged("home_sweet", &who);
        else if (w == "no_drinks") hit = drinks == 0;
        else if (w == "all_walked") hit = walked == n && passed == 0;
        else if (w == "any") hit = true;
        else if (w.rfind("ev_", 0) == 0) { int ei = EventIndex(w.substr(3).c_str()); hit = ei >= 0 && events[ei].started; }
        else hit = flagged(w.c_str(), &who);
        if (!hit) continue;
        std::string out = h.text;
        out = Replace(out, "{N}", Words(n)); out = Replace(out, "{S}", n == 1 ? "" : "S"); out = Replace(out, "{n}", TextFormat("%d", w == "any" ? drinks : n));
        out = Replace(out, "{who}", Upper(who.empty() ? std::string("A SAILOR") : who == "You" ? std::string("A SAILOR") : who)); out = Replace(out, "{thing}", Upper(thing));
        if (CurBar() == BAR_MONKEY) { out = Replace(out, "THE SODDEN GULL", "THE BRASS MONKEY"); out = Replace(out, "SODDEN GULL", "BRASS MONKEY"); out = Replace(out, "THE GULL'S", "THE MONKEY'S"); out = Replace(out, "THE GULL", "THE MONKEY"); out = Replace(out, "THE BARTENDER", "CELESTE"); out = Replace(out, "BARTENDER", "CELESTE"); }
        return out;
    }
    return CurBar() == BAR_MONKEY ? "ANOTHER NIGHT AT THE BRASS MONKEY" : "ANOTHER NIGHT AT THE SODDEN GULL";
}
std::vector<std::string> Night::MorningStory(const Player& p) const {
    if (mirror && p.id >= 0 && p.id < (int)storyCache.size()) return storyCache[p.id];
    const ScoringData& s = SD(); std::vector<std::string> L;
    uint32_t k = (uint32_t)(p.id * 7 + p.drinks * 3 + (int)p.money);
    switch (p.ending) {
        case E_HOME_WITH: {
            const HomeDef* h = HomeOf(p.homeKind);
            std::string m = p.homeKind == "dog" ? "Halfway up the stairs {who} saw the dog and remembered an appointment. You walked home with the dog instead." : h ? h->morning : "You went home with {who}.";
            L.push_back(Replace(Replace(m, "{who}", p.homeWith), "{card}", p.card));
        } break;
        case E_PASSED_OUT: L.push_back("You woke " + p.wokeAt + ". Your head is a bell someone keeps ringing."); break;
        case E_WALKED: L.push_back(p.drunk < 20 ? "You walked home sober, which nobody at breakfast believes." : "You walked home, mostly in a straight line, singing."); break;
        case E_CLOSING: L.push_back(p.st == State::Down ? "You woke " + Pick(s.woke.count("knocked_out") ? s.woke.at("knocked_out") : std::vector<std::string>{}, k) + "." : "The bartender swept you out with the glass at three."); break;
        case E_KNOCKED_OUT: L.push_back("You woke " + (s.woke.count("knocked_out") ? Pick(s.woke.at("knocked_out"), k) : p.wokeAt) + ", a black eye coming up nicely."); break;
        case E_ARRESTED: L.push_back("You woke " + (s.woke.count("arrested") ? Pick(s.woke.at("arrested"), k) : p.wokeAt) + "."); break;
        case E_HOSPITAL: L.push_back("You woke " + (s.woke.count("hospital") ? Pick(s.woke.at("hospital"), k) : p.wokeAt) + ", and a scar you'll lie about."); break;
        case E_THROWN_OUT: L.push_back("You woke " + (s.woke.count("thrown_out") ? Pick(s.woke.at("thrown_out"), k) : p.wokeAt) + "."); break;
        default: L.push_back("You woke " + p.wokeAt + "."); break;
    }
    if (p.faceDrawn) L.push_back("Someone drew a moustache on your face. It's on the morning screen now, forever.");
    // the night's best moment and what it cost
    std::string best; for (const auto& m : p.log) if (m.kind == 5) best = m.text;
    if (!best.empty()) L.push_back("Best moment: " + best);
    std::string lost;
    if (p.kidneys < 2) lost += "a kidney, ";
    for (const auto& it : p.items) if (it == "one shoe" || it == "no coat") lost += (it == "one shoe" ? std::string("a shoe, ") : std::string("your coat, "));
    float spent = 200 - p.money; if (spent > 0) lost += TextFormat("%.0f in wages, ", spent);
    if (!lost.empty()) { lost.resize(lost.size() - 2); L.push_back("Lost: " + lost + "."); }
    return L;
}

// ---------------------------------------------------------------- --night-test's stage 5 checks (the gate: someone loses a kidney and the headline is funny)
int NightFlirtChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    // where the regulars' nights end up, against the doc's odds
    { std::map<std::string, int> c; int n = 0; for (const auto& r : D().regulars) if (r.home != "none") { c[r.home]++; n++; }
      bool ok = true; std::string s;
      for (const auto& h : SD().home) { if (h.key == "fortune") continue; float got = 100.0f * c[h.key] / std::max(1, n + 1); ok &= fabsf(got - h.odds) <= 6; s += TextFormat(" %s %.0f%% (%.0f)", h.key.c_str(), got, h.odds); }
      check(ok, "the regulars' going-home odds:" + s); }
    auto find = [](Night& n, const char* name) { for (auto& c : n.patrons) if (c.name == name) return c.id; return -1; };
    auto setup = [&](Night& n, int who, Player& p) { Patron& c = n.patrons[who]; c.inside = true; c.gone = false; c.leaving = false; c.talkingTo = -1; c.mood = 70; c.pos = {18, 5}; c.goal = c.pos; c.nextGoalT = 1e9f; p.st = State::Active; p.pos = {18.8f, 5}; p.drunk = 25; };
    // the fit: a romantic patron likes a compliment far more than a dance
    { Night n; Opts o; o.events = false; o.seed = 9; n.Init(o); Player& p = n.players[0]; int w = find(n, "Nellie Bright"); setup(n, w, p);
      float a = n.FlirtOdds(p, n.patrons[w], 0), b = n.FlirtOdds(p, n.patrons[w], 3);
      check(a > b + 0.2f, TextFormat("Nellie Bright (romantic) likes a compliment (%.0f%%) more than a dance (%.0f%%)", a * 100, b * 100));
      int d = find(n, "Dottie Finch"); setup(n, d, p);
      check(n.FlirtOdds(p, n.patrons[d], 1) > 0.85f, "and the kidney thieves are the best flirts in the bar"); }
    // the whole flirt: open, build, the offer, the kidney; the headline is the funny one
    { Night n; Opts o; o.events = false; o.seed = 11; n.Init(o); Player& p = n.players[0]; p.name = "You"; int d = find(n, "Dottie Finch"); setup(n, d, p);
      p.in.flirtWith = d; n.Step(0.02f);
      check(p.flirt.patron == d && !p.flirt.theirLine.empty(), "a flirt opens: \"" + p.flirt.theirLine + "\"");
      for (int k = 0; k < 8 && !p.flirt.offer && !p.flirt.over; k++) { p.in.flirtSay = 1; n.Step(0.02f); }
      check(p.flirt.offer, "three good lines (two, for a thief) and the offer: \"" + p.flirt.theirLine + "\"");
      p.in.offer = 1; n.Step(0.02f);
      check(p.st == State::Gone && p.ending == E_HOME_WITH && p.kidneys == 1, "you take it, and wake in a bathtub of ice with one kidney");
      std::string h = n.Headline();
      check(h.find("ONE KIDNEY") != std::string::npos && h.find("TRIUMPHANT") != std::string::npos, "the headline: " + h);
      auto story = n.MorningStory(p); bool bath = !story.empty() && story[0].find("bathtub") != std::string::npos;
      check(bath, "the morning: " + (story.empty() ? std::string() : story[0]));
      int sc = n.Score(p), sum = 0; for (const auto& l : n.ScoreBreakdown(p)) sum += l.points;
      check(sc == sum && sc >= 50, TextFormat("the score adds up (%d)", sc)); }
    // a dog at your side and the thieves think again
    { Night n; Opts o; o.events = false; o.seed = 12; n.Init(o); Player& p = n.players[0]; int d = find(n, "Jasper Coil"); setup(n, d, p); n.dog.owner = 0;
      n.GoHome(p, d, "kidney");
      check(p.kidneys == 2 && p.homeKind == "dog", "nobody steals a kidney in front of a dog"); }
    // a teammate at the door stops a bad night
    { Night n; Opts o; o.events = false; o.seed = 13; o.players = 2; n.Init(o); Player& p = n.players[0]; Player& q = n.players[1]; int d = find(n, "Harrow"); setup(n, d, p);
      q.st = State::Active; q.pos = {40, 40};
      p.flirt.patron = d; p.flirt.offer = true; n.patrons[d].talkingTo = 0; p.in.offer = 1; n.Step(0.02f);
      check(p.leavingT > 0 && p.st == State::Active, "a bad night waits 30 s at the door when there are friends in the bar");
      q.pos = D().bar.door; for (int k = 0; k < 50; k++) n.Step(0.02f);
      check(p.st == State::Active && p.leavingT <= 0 && p.money > 0, "a teammate at the door stops it: the thief runs"); }
    // a sincere night: +100
    { Night n; Opts o; o.events = false; o.seed = 14; n.Init(o); Player& p = n.players[0]; int d = find(n, "Old Marlow"); setup(n, d, p); n.GoHome(p, d, "sincere");
      int pts = 0; for (const auto& l : n.ScoreBreakdown(p)) if (l.what.find("Went home") == 0) pts = l.points;
      check(pts == 100 && !p.homeBad, "a sincere night: breakfast and 100"); }
    // decline: a friend who backs you in a fight
    { Night n; Opts o; o.events = false; o.seed = 15; n.Init(o); Player& p = n.players[0]; int d = find(n, "Nellie Bright"); setup(n, d, p);
      p.flirt.patron = d; p.flirt.offer = true; n.patrons[d].talkingTo = 0; p.in.offer = 2; n.Step(0.02f);
      check((n.patrons[d].friendOf & 1) && n.patrons[d].mood > 80, "decline the offer and keep a friend"); }
    // the bartender's warning: one true name and one false
    { Night n; Opts o; o.events = false; o.seed = 16; n.Init(o); Player& p = n.players[0]; p.st = State::Active; p.pos = D().bar.serve; for (auto& c : n.patrons) if (c.home == "kidney") c.inside = true;
      p.in.askTrouble = true; n.Step(0.02f); bool told = false; for (const auto& s : n.say) told |= s.find("Keep an eye on") != std::string::npos;
      check(told && p.tab > 0, "ask the bartender who's trouble: " + (n.say.empty() ? std::string() : n.say.back())); }
    return fails;
}

} // namespace no
