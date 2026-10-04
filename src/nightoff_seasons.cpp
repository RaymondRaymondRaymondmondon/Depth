// A Night Off: seasonal nights (doc pp. 34-36), stage 10d. Headless. One season is always on (the real calendar picks
// it: SeasonToday; the host can force any) and it's held at one bar or either; a night at the other bar is an ordinary
// one. A season adds one layer and changes the odds: the Harbour Festival (Packed, double events, stalls in the yard,
// fireworks at midnight), the Storm (no ferry, the power fails, the yard is shut, a lock-in, the Marguerite's story),
// the Wedding (at the Monkey: the ceremony, a free bar, toasts, an objection), the Regatta (rival crews, the ladder
// pays triple, a refereed midnight brawl), New Year (to 4 a.m., the countdown, a kiss for everyone, the goat for the
// unlucky), the Wake of the Year (no fights, a confession round), the Masquerade (masks; three thieves; unmask one for
// their cooler) and the Cook's Birthday (free food; Tam fights anyone who won't eat; the frying pan given).
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>

namespace no {

struct SeasonDef { std::string key, name, where, what, signature, headline; std::vector<std::string> lines; };
struct SeasonsData { std::vector<SeasonDef> s; std::vector<std::string> byMonth; std::map<std::string, float> num; float N(const char* k, float d) const { auto it = num.find(k); return it == num.end() ? d : it->second; } };
static const SeasonsData& SSD() {
    static SeasonsData d = [] {
        SeasonsData d; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_seasons.json");
        for (const Json& e : j["seasons"].a) {
            SeasonDef x; x.key = e["key"].Str0(); x.name = e["name"].Str0(); x.where = e["where"].Str0("either"); x.what = e["what"].Str0(); x.signature = e["signature"].Str0(); x.headline = e["headline"].Str0();
            for (const Json& l : e["marguerite"].a) x.lines.push_back(l.Str0());
            for (const Json& l : e["toasts"].a) x.lines.push_back(l.Str0());
            d.s.push_back(x);
        }
        for (const Json& m : j["by_month"].a) d.byMonth.push_back(m.Str0());
        for (const char* sec : {"stalls", "wedding", "storm", "newyear", "wake", "cook"}) for (const auto& kv : j[sec].o) d.num[std::string(sec) + "." + kv.first] = kv.second.F(0);
        return d;
    }();
    return d;
}
int SeasonCount() { return (int)SSD().s.size(); }                        // (ids 1..count; 0 is an ordinary night)
const char* SeasonKey(int id) { return id >= 1 && id <= SeasonCount() ? SSD().s[id - 1].key.c_str() : ""; }
const char* SeasonName(int id) { return id >= 1 && id <= SeasonCount() ? SSD().s[id - 1].name.c_str() : "an ordinary night"; }
const char* SeasonWhat(int id) { return id >= 1 && id <= SeasonCount() ? SSD().s[id - 1].what.c_str() : ""; }
int SeasonWhere(int id) { if (id < 1 || id > SeasonCount()) return -1; const std::string& w = SSD().s[id - 1].where; return w == "gull" ? BAR_GULL : w == "monkey" ? BAR_MONKEY : 2; }
int SeasonId(const char* key) { for (int i = 0; i < SeasonCount(); i++) if (SSD().s[i].key == key) return i + 1; return 0; }
int SeasonForMonth(int month) { const auto& m = SSD().byMonth; return m.empty() ? 0 : SeasonId(m[std::clamp(month - 1, 0, (int)m.size() - 1)].c_str()); }
int SeasonToday() { time_t now = time(nullptr); tm* lt = localtime(&now); return SeasonForMonth(lt ? lt->tm_mon + 1 : 1); }
std::string SeasonWhereText(int id) { int w = SeasonWhere(id); return w == BAR_GULL ? "at the Gull" : w == BAR_MONKEY ? "at the Monkey" : w == 2 ? "at either bar" : ""; }

bool Night::SeasonOn() const { int w = SeasonWhere(opts.season); return w == 2 || w == opts.bar; }
bool Night::SeasonIs(const char* key) const { return SeasonOn() && opts.season == SeasonId(key); }
static float SN(const char* k, float d) { return SSD().N(k, d); }
static Patron* FindPatron(Night& n, const char* name) { for (auto& c : n.patrons) if (c.name == name && !c.gone) return &c; return nullptr; }
static float H(float hour) { return (hour - 19) * 60 * SECONDS_PER_GAME_MINUTE; }   // (an hour of the night as Night::t)

void Night::SeasonInit() {
    fireworksT = 0; fireworksDone = powerOut = countdownDone = confessed = ceremonyDone = weddingFree = cookAngry = false; stormLine = 0; stormSayT = 0; powerH = 99; groom = bride = -1;
    if (!SeasonOn()) return;
    if (SeasonIs("storm")) { rainH = 19.0f; powerH = SN("storm.power_from", 21) + Rand() * (SN("storm.power_to", 24) - SN("storm.power_from", 21)); if (EventIndex("lockin") < 0 || events[EventIndex("lockin")].startH > powerH + 1) ForceEvent("lockin", std::min(26.5f, powerH + 0.5f)); }
    if (SeasonIs("newyear")) endMinutes = (SN("newyear.end", 28) - 19) * 60;
    if (SeasonIs("regatta")) ForceEvent("rivals", 20.5f + Rand() * 1.5f);
    if (SeasonIs("wake")) { int w = ForceEvent("wake", 19.3f); (void)w; }
    if (SeasonIs("masquerade")) {   // three thieves instead of one: two more of the room carry coolers tonight
        int made = 0;
        for (auto& c : patrons) if (made < 2 && !c.thief && c.type != T_STAFF && c.reg >= 0 && (c.type == T_FLIRT || c.type == T_ODDBALL) && c.home != "married") { c.thief = true; c.home = "kidney"; made++; }
    }
    if (SeasonIs("wedding")) {   // the groom (Cutter Jones, up from the docks) and the bride (Lord Ashby's daughter), at the ballroom
        groom = AddPatron(-1); Patron& g = patrons[groom]; g.name = "Cutter Jones"; g.secret = "The groom, tonight"; g.type = T_SAILOR; g.traits = (1u << std::max(0, D().Trait("violent"))) | (1u << std::max(0, D().Trait("loves sailors"))); g.home = "none"; g.arriveH = 21.3f; g.leaveH = 27.5f; g.look.top = {230, 226, 220, 255}; g.look.build = 1.2f; g.role = "the groom";
        bride = AddPatron(-1); Patron& b = patrons[bride]; b.name = "Miss Ashby"; b.secret = "The bride: Lord Ashby's daughter, who chose a sailor"; b.type = T_TALKER; b.traits = 1u << std::max(0, D().Trait("romantic")); b.home = "none"; b.arriveH = 21.3f; b.leaveH = 27.5f; b.look.top = {250, 248, 244, 255}; b.look.model = 3; b.role = "the bride";
        patrons[groom].thief = patrons[bride].thief = false;
    }
}
void Night::StepSeason(float dt) {
    if (!SeasonOn()) return;
    float h = Hour();
    // the Harbour Festival: fireworks at midnight (60 s: flirts one step easier, fights the police never hear of)
    if (SeasonIs("festival")) {
        if (!fireworksDone && h >= 24) { fireworksDone = true; fireworksT = SN("stalls.fireworks_s", 60); Say("Midnight: the first rocket goes up over the harbour, and the whole yard looks up."); Flag("season_festival", ""); }
        if (fireworksT > 0) { fireworksT -= dt; policeT = -1; }
    }
    // the Storm: the power fails; candles; the Marguerite told in the dark
    if (SeasonIs("storm")) {
        if (!powerOut && h >= powerH) { powerOut = true; Say("A crack of thunder, and every lamp in the place goes out. Candles, then. The slot machines die mid-spin."); Flag("season_storm", ""); }
        if (powerOut && stormLine < 5 && (stormSayT -= dt) <= 0) {
            const auto& L = SSD().s[opts.season - 1].lines; stormSayT = 6 * SECONDS_PER_GAME_MINUTE;
            if (stormLine < (int)L.size()) Say("The bartender, by candlelight: " + L[stormLine]);
            stormLine++;
        }
    }
    // the Wedding: the ceremony, the free bar (+40 over the hour to everyone in the reception), toasts
    if (SeasonIs("wedding")) {
        float cer = SN("wedding.ceremony", 22);
        for (int id : {groom, bride}) if (id >= 0 && id < (int)patrons.size() && patrons[id].inside && h < cer + 0.3f) { Patron& c = patrons[id]; Vector2 at = id == groom ? Vector2{15.4f, 20.0f} : Vector2{16.6f, 20.0f}; if (Vector2Distance(c.goal, at) > 0.1f) { c.goal = at; c.path = NavPath(c.pos, at); c.seatKind = "stand"; c.seat = -1; } c.nextGoalT = 30; }
        if (!ceremonyDone && h >= cer) { ceremonyDone = true; Say("In the ballroom, Lord Ashby gives his daughter away to a sailor. \"If anyone here knows a reason...\""); }
        weddingFree = h >= SN("wedding.free_from", 22.5f) && h < SN("wedding.free_to", 23.5f);
        if (weddingFree) for (auto& p : players) if (p.st == State::Active && RoomAt(p.pos) == std::string("The card room")) { p.drunk = std::min(100.0f, p.drunk + SN("wedding.free_drunk", 40) * dt / (SN("wedding.free_drunk_minutes", 20) * SECONDS_PER_GAME_MINUTE));   /* (the free bar comes to you: +40 over twenty minutes) */ p.peakDrunk = std::max(p.peakDrunk, p.drunk); }
    }
    // New Year: the countdown, and everyone kisses someone (the goat, for those with no one)
    if (SeasonIs("newyear") && !countdownDone && h >= 24) {
        countdownDone = true;
        Say("TEN! NINE! EIGHT! ... THREE! TWO! ONE! HAPPY NEW YEAR!");
        for (auto& p : players) {
            if (p.st != State::Active) continue;
            int best = -1; float bd = 3.5f;
            for (const auto& c : patrons) if (c.inside && !c.gone && c.type != T_STAFF && c.home != "none") { float d = Vector2Distance(c.pos, p.pos); if (d < bd) { bd = d; best = c.id; } }
            if (best >= 0 && Rand() < FlirtOdds(p, patrons[best], 0)) { patrons[best].mood = std::min(100.0f, patrons[best].mood + 20); p.kissed = true; Note(p, 5, "Kissed " + patrons[best].name + " at midnight."); }
            else if (best >= 0) Note(p, 0, patrons[best].name + " turned a cheek at midnight.");
            else { goatOn = true; goatPos = Vector2Add(p.pos, {0.7f, 0}); p.kissed = true; Note(p, 5, "Had no one to kiss at midnight, so kissed the goat."); Flag("season_newyear", p.name); Say(p.name + " kisses the goat. The goat allows it."); }
        }
        const auto& deck = GD().deck; if (!deck.empty()) Say("The fortune teller turns one card for the year: " + deck[(int)(Rand() * deck.size()) % deck.size()] + ". She doesn't say what it means.");
    }
    // the Wake of the Year: the confession round at the bar (every patron tells one secret; the thieves are named)
    if (SeasonIs("wake") && !confessed && h >= SN("wake.confession", 23)) {
        confessed = true; Flag("season_wake", "");
        Say("At the bar, glass by glass, the whole dock confesses. Deacon Pale would have liked it.");
        for (auto& p : players) {
            if (p.st != State::Active || RoomAt(p.pos) != std::string(D().bar.rooms.size() > 1 ? D().bar.rooms[1].name : "The main bar")) continue;
            int n = 0; for (const auto& c : patrons) if (c.inside && !c.gone && c.reg >= 0 && std::find(p.known.begin(), p.known.end(), c.name) == p.known.end()) { p.known.push_back(c.name); n++; }
            for (const auto& c : patrons) if (c.thief && !c.gone && (c.inside || c.arriveH < h + 2)) Note(p, 6, "At the confession, someone names " + c.name + " as a kidney thief.");
            Note(p, 5, TextFormat("Listened to the confession round: %d secrets.", n));
        }
    }
    // the Cook's Birthday: by half eleven, Tam takes it personally if you haven't eaten
    if (SeasonIs("cook") && !cookAngry && h >= SN("cook.angry_at", 23.5f)) {
        cookAngry = true;
        Patron* tam = FindPatron(*this, "Tam the Cook");
        for (auto& p : players) if (p.st == State::Active && !p.ate && tam && tam->inside) { Say("Tam the Cook, with a ladle: \"You haven't EATEN. On my BIRTHDAY.\""); StartBrawl(PatronW(tam->id), PlayerW(p.id), PatronW(tam->id)); break; }
    }
}
// the season's own things to do (EventOptions): the stalls, the objection, a toast, the midnight brawl, an unmasking
void Night::SeasonOptions(const Player& p, std::vector<EvOption>& o) const {
    if (!SeasonOn() || p.st != State::Active) return;
    float h = Hour();
    if (SeasonIs("festival") && p.money >= SN("stalls.cost", 5)) {
        static const Vector2 STALL[3] = {{8, 34}, {20, 33}, {30, 36}};
        static const char* LABEL[3] = {"The ring toss (5)", "Ring the strongman bell (5)", "Guess the fish in the jar (5)"};
        for (int k = 0; k < 3; k++) if (Vector2Distance(p.pos, STALL[k]) < 2.0f) o.push_back({80 + k, -1, LABEL[k]});
    }
    if (SeasonIs("wedding")) {
        if (ceremonyDone && h < SN("wedding.ceremony", 22) + 0.3f && !p.objected && RoomAt(p.pos) == std::string("The ballroom")) o.push_back({83, -1, "\"I object!\""});
        if (ceremonyDone && h < 25 && !p.toasted && RoomAt(p.pos) == std::string("The card room")) o.push_back({84, -1, "Raise a toast to the couple"});
    }
    if (SeasonIs("masquerade") && h < 24) { int c = NearestPatron(p, 1.6f); if (c >= 0 && patrons[c].type != T_STAFF && !patrons[c].unmasked) o.push_back({85, c, "Unmask them"}); }
    if (SeasonIs("regatta") && h >= 24 && h < 24.6f && p.fight.brawl < 0) for (const auto& c : patrons) if (c.role == "a rival sailor" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 3) { o.push_back({86, c.id, "Enter the midnight brawl (Sister Ash referees)"}); break; }
}
void Night::SeasonAction(Player& p, int act, int arg) {
    float cost = SN("stalls.cost", 5);
    switch (act) {
        case 80: p.money -= cost; if (Rand() < std::clamp(0.55f / PlayerAim(p), 0.05f, 0.9f)) { p.money += SN("stalls.ring_prize", 30); Note(p, 3, "Won at the ring toss."); Say(p.name + " rings the bottle. The stallholder sighs."); } else Say("The ring bounces off. The stallholder smiles."); break;
        case 81: p.money -= cost; if (Rand() * Toughness(p) > 0.85f) { p.money += SN("stalls.bell_prize", 20); p.charBuff = std::max(p.charBuff, 0.1f); p.charBuffT = std::max(p.charBuffT, 10 * SECONDS_PER_GAME_MINUTE); Note(p, 5, "Rang the strongman bell."); Say("DING! " + p.name + " rings the strongman bell, and the yard cheers."); } else Say("The puck rises halfway and thinks better of it."); break;
        case 82: p.money -= cost; if (Rand() < 1 / 6.0f) { p.money += SN("stalls.jar_prize", 60); Note(p, 5, "Guessed the fish in the jar exactly."); Flag("festival_jar", p.name); Say(p.name + " guesses the fish in the jar to the fish."); } else Say("Wrong by a dozen fish. The jar looks smug."); break;
        case 83: p.objected = true; Flag("season_wedding", p.name); Note(p, 5, "Objected at the wedding."); Say(p.name + ": \"I OBJECT!\" The room turns. Cutter Jones rolls up his sleeves."); if (groom >= 0) StartBrawl(PatronW(groom), PlayerW(p.id), PatronW(groom)); break;
        case 84: {
            p.toasted = true;
            const auto& L = SSD().s[opts.season - 1].lines; std::string toast = L.empty() ? "To the happy couple!" : L[(int)(Rand() * L.size()) % L.size()];
            bool good = Rand() < std::clamp(0.3f + 0.5f * Charisma(p), 0.1f, 0.95f);
            Say(p.name + ", glass raised: \"" + toast + "\"");
            for (auto& c : patrons) if (c.inside && !c.gone && RoomAt(c.pos) == std::string("The card room")) c.mood = std::clamp(c.mood + (good ? 10.0f : -5.0f), 0.0f, 100.0f);
            Say(good ? "The reception roars and drinks." : "A cough. A clink. Someone laughs at the wrong moment.");
            if (good) Note(p, 5, "Gave a toast at the society wedding, and they loved it."); else Note(p, 0, "Gave a toast at the wedding. It could have gone better.");
        } break;
        case 85: if (arg >= 0 && arg < (int)patrons.size()) {
            Patron& c = patrons[arg]; c.unmasked = true;
            if (c.thief) { p.items.push_back("a kidney cooler"); c.mood = 0; c.thief = false; c.home = "sincere"; c.leaveH = Hour(); Note(p, 5, "Unmasked " + c.name + ", a kidney thief, and took their cooler."); Flag("season_masquerade", p.name); Say("The mask comes off: it's " + c.name + ", and the cooler under their arm. They flee."); }
            else { c.mood = std::max(0.0f, c.mood - 30); Note(p, 0, "Pulled the mask off " + c.name + ", who was not a thief, and is not pleased."); Say(c.name + ", unmasked: \"How DARE you.\""); }
        } break;
        case 86: if (arg >= 0 && arg < (int)patrons.size()) {
            Say("Sister Ash rings a bell: \"The midnight brawl. Queensberry rules. I referee.\"");
            int b = StartBrawl(PlayerW(p.id), PatronW(arg), PlayerW(p.id)); if (b >= 0) { brawls[b].refereed = true; policeT = -1; }
            Note(p, 0, "Entered the regatta's midnight brawl.");
        } break;
    }
}
// a win at darts or pool during the Regatta: the ladder (three wins sober: the headline, kept on the profile)
void Night::SeasonGameWon(Player& p, int kind) {
    if (!SeasonIs("regatta") || (kind != GK_DARTS && kind != GK_POOL)) return;
    p.ladderWins++;
    if (p.ladderWins == 3) { Note(p, 5, p.drunk < 20 ? "Took the regatta ladder, stone-cold sober." : "Took the regatta ladder."); if (p.drunk < 20) Flag("season_regatta", p.name); Say(p.name + " takes the regatta ladder."); }
}

// ---------------------------------------------------------------- --night-test's stage 10d checks (seasonal nights)
int NightSeasonChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    check(SeasonCount() == 8, "eight seasonal nights load from nightoff_seasons.json");
    { bool every = true; for (int m = 1; m <= 12; m++) every &= SeasonForMonth(m) >= 1; check(every && SeasonToday() >= 1, TextFormat("one is always on (today: %s %s)", SeasonName(SeasonToday()), SeasonWhereText(SeasonToday()).c_str())); }
    auto night = [&](const char* key, int bar, float start, int players = 1) { Night* n = new Night; Opts o; o.seed = 41; o.bar = bar; o.season = SeasonId(key); o.startMinutes = start; o.players = players; n->Init(o); return n; };
    { Night* n = night("festival", BAR_MONKEY, 0); check(!n->SeasonOn(), "a Gull season at the Monkey is an ordinary night"); delete n; }
    { Night* n = night("festival", BAR_GULL, 0); check(n->opts.crowd == 2, "the Festival: a packed crowd"); delete n; }
    { Night* n = night("festival", BAR_GULL, 290); Player& p = n->players[0]; p.money = 100; p.pos = {20, 33.5f};
      bool stall = false; for (const auto& o : n->EventOptions(p)) stall |= o.act == 81; check(stall, "and the stalls in the yard");
      for (int i = 0; i < 1600 && n->fireworksT <= 0; i++) n->Step(0.05f);
      check(n->fireworksT > 0, "midnight: fireworks");
      int c = -1; for (auto& x : n->patrons) if (x.inside && !x.gone && x.type == T_FLIRT) { c = x.id; x.mood = 70; x.talkingTo = -1; x.playing = -1; x.pos = Vector2Add(p.pos, {0.8f, 0}); break; }
      if (c >= 0) { n->StartFlirt(p, c); }
      check(c >= 0 && p.flirt.patron == c && p.flirt.need <= 2, "during the fireworks a flirt is one step easier");
      n->policeT = 30; n->Step(0.05f); check(n->policeT < 0, "and the police never hear of a fight"); delete n; }
    { Night* n = night("storm", BAR_GULL, 0); n->Step(0.05f); check(n->raining && n->EventIndex("lockin") >= 0 && !n->FerryRunning(), "the Storm: rain from the start, a lock-in coming, no ferry");
      n->t = H(n->powerH + 0.01f); n->Step(0.05f); Player& p = n->players[0]; p.pos = D().bar.dartboard; p.pos.x += 2; p.money = 100;
      p.pos = {1.6f, 14.3f}; p.in.startGame = GK_SLOTS; n->Step(0.05f);
      check(n->powerOut && p.game.kind != GK_SLOTS, "the power fails: candles, and the slot machines are dead");
      p.pos = {19.5f, 31.5f}; Vector2 q = p.pos; n->Collide(q, 0.3f); check(q.y < 30, "and the yard is shut"); delete n; }
    { Night* n = night("wedding", BAR_MONKEY, 175); Player& p = n->players[0];
      for (int i = 0; i < 600 && !n->ceremonyDone; i++) n->Step(0.05f);
      check(n->ceremonyDone && n->groom >= 0 && n->patrons[n->groom].name == "Cutter Jones", "the Wedding at the Monkey: Cutter Jones marries Lord Ashby's daughter");
      { const Patron& g = n->patrons[n->groom]; check(g.inside && !g.gone && Vector2Distance(g.pos, {15.4f, 20.0f}) < 2.5f, TextFormat("the couple stand under the arch (the groom at %.1f, %.1f; inside %d gone %d)", g.pos.x, g.pos.y, g.inside, g.gone)); }
      p.pos = {16, 15}; bool obj = false; for (const auto& o : n->EventOptions(p)) obj |= o.act == 83; check(obj, "and anyone in the ballroom may object");
      n->t = H(22.6f); p.pos = {34.5f, 16.5f}; float d0 = p.drunk; for (int i = 0; i < 200; i++) n->Step(0.05f);
      check(n->weddingFree && p.drunk > d0 + 1 && n->PriceOf(DrinkIndex("pint")) == 0, "the free bar: free drinks, and the reception goes to your head");
      p.in.evAct = 84; n->Step(0.05f); check(p.toasted, "a toast to the couple"); delete n; }
    { Night* n = night("newyear", BAR_GULL, 290); Player& p = n->players[0];
      check(n->endMinutes >= 8.9f * 60, "New Year: the clock runs to 4");
      p.pos = {39, 46};   // (alone in the yard's corner: nobody to kiss)
      for (int i = 0; i < 1600 && !n->countdownDone; i++) { p.pos = {39, 46}; n->Step(0.05f); }
      check(n->countdownDone && p.kissed && n->goatOn, "midnight: the countdown, and a sailor with no one to kiss kisses the goat"); delete n; }
    { Night* n = night("wake", BAR_GULL, 230); Player& p = n->players[0];
      int c = -1; for (auto& x : n->patrons) if (x.inside && !x.gone && x.type != T_STAFF) { c = x.id; x.pos = Vector2Add(p.pos, {0.6f, 0}); break; }
      check(c >= 0 && n->StartBrawl(PlayerW(0), PatronW(c), PlayerW(0)) < 0, "the Wake of the Year: no fights are possible");
      p.pos = {17, 6};
      for (int i = 0; i < 1200 && !n->confessed; i++) { p.pos = {17, 6}; n->Step(0.05f); }
      check(n->confessed && p.known.size() >= 5, TextFormat("the confession round: %d secrets learned at the bar", (int)p.known.size())); delete n; }
    { Night* n = night("masquerade", BAR_MONKEY, 120); int thieves = 0; for (const auto& c : n->patrons) thieves += c.thief; check(thieves >= 3, TextFormat("the Masquerade: three thieves (%d)", thieves));
      Player& p = n->players[0]; int t = -1; for (auto& c : n->patrons) if (c.thief) { c.inside = true; c.gone = false; c.pos = Vector2Add(p.pos, {0.8f, 0}); c.vel = {0, 0}; t = c.id; break; }
      if (t >= 0) n->SeasonAction(p, 85, t);
      bool cooler = std::find(p.items.begin(), p.items.end(), "a kidney cooler") != p.items.end();
      check(t >= 0 && cooler, "unmasking a thief before midnight earns their cooler");
      int f = -1; for (auto& c : n->patrons) if (c.thief && c.id != t) { c.inside = true; c.gone = false; c.mood = 90; c.talkingTo = -1; c.playing = -1; c.pos = Vector2Add(p.pos, {0.8f, 0}); f = c.id; break; }
      if (f >= 0) { n->StartFlirt(p, f); p.flirt.offer = true; n->FlirtOffer(p, true); }
      check(f >= 0 && p.kidneys == 2 && p.st == State::Active, "and the cooler stops the next theft"); delete n; }
    { Night* n = night("cook", BAR_GULL, 0); int stew = DrinkIndex("stew"); check(n->PriceOf(stew) == 0, "the Cook's Birthday: the food is free");
      Player& p = n->players[0]; p.pos = D().bar.hatch; p.money = 50; n->Order(p, stew); for (int i = 0; i < 800 && p.st == State::Eating; i++) n->Step(0.05f);
      check(p.ate && std::find(p.items.begin(), p.items.end(), "the cook's frying pan") != p.items.end(), "and the frying pan is given, not sold"); delete n; }
    { Night* n = night("regatta", BAR_GULL, 0); Player& p = n->players[0]; p.drunk = 5;
      for (int k = 0; k < 3; k++) n->SeasonGameWon(p, GK_DARTS);
      bool flag = false; for (const auto& f : n->flags) flag |= f.first == "season_regatta"; check(flag && n->EventIndex("rivals") >= 0, "the Regatta: rival crews in, and the ladder taken sober is a headline");
      check(n->Headline().find("LADDER") != std::string::npos, "the paper: " + n->Headline()); delete n; }
    SetBar(BAR_GULL);
    return fails;
}

} // namespace no
