// A Night Off's headless core (see nightoff.h), stage 1: the bar, the crew's movement, the drunk meter, the bartender
// and the menu, the clock, passing out, walking home, and the morning screen's first lines.
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace no {

// ---------------------------------------------------------------- data
static Rectangle R4(const Json& j) { return {j[0].F(0), j[1].F(0), j[2].F(1), j[3].F(1)}; }
static Vector2 V2(const Json& j) { return {j[0].F(0), j[1].F(0)}; }
static Color C3(const Json& j, Color d) { return j.IsArr() ? Color{(unsigned char)j[0].I(d.r), (unsigned char)j[1].I(d.g), (unsigned char)j[2].I(d.b), 255} : d; }
static void LoadDrink(const Json& r, std::vector<DrinkDef>& out) {
    DrinkDef x; x.key = r["key"].Str0(); x.name = r["name"].Str0(x.key); x.where = r["where"].Str0("bar"); x.effect = r["effect"].Str0(); x.line = r["line"].Str0();
    x.drunk = r["drunk"].F(0); x.price = r["price"].F(0); x.amount = r["amount"].F(0); x.minutes = r["minutes"].F(0); x.eatS = r["eat_s"].F(0);
    out.push_back(x);
}
static void LoadRegular(const Data& d, const Json& r, std::vector<PatronDef>& out) {
    PatronDef p; p.name = r["name"].Str0(); p.secret = r["secret"].Str0(); p.tell = r["tell"].Str0(); p.staff = r["staff"].Str0(); p.home = r["home"].Str0("sincere");
    std::string ty = r["type"].Str0(); for (int t = 0; t < T_COUNT; t++) if (ty == TypeName(t)) p.type = t;
    for (const Json& tr : r["traits"].a) { int k = d.Trait(tr.Str0()); if (k >= 0) p.traits.push_back(k); }
    p.thief = r["thief"].Bool0(false); p.rich = r["rich"].Bool0(false); p.travels = r["travels"].Bool0(false); p.stool = r["stool"].I(-1); p.arrive = r["arrive"].F(20); p.leave = r["leave"].F(25);
    const Json& lk = r["look"]; p.look.model = lk["model"].I(1); p.look.build = lk["build"].F(1); p.look.height = lk["height"].F(1); p.look.beard = lk["beard"].Str0();
    if (lk["top"].IsArr()) p.look.top = {(unsigned char)lk["top"][0].I(), (unsigned char)lk["top"][1].I(), (unsigned char)lk["top"][2].I(), 255};
    if (lk["hat"].IsArr()) p.look.hat = {(unsigned char)lk["hat"][0].I(), (unsigned char)lk["hat"][1].I(), (unsigned char)lk["hat"][2].I(), 255};
    out.push_back(p);
}
static Data Load(int bar) {
    Data d;
    std::string dir = rt::DataDir() + "/../nightoff/";
    Json mj; if (bar == BAR_MONKEY) mj = LoadJsonFile(dir + "nightoff_patrons_monkey.json");
    Json k = LoadJsonFile(dir + "nightoff_drinks.json");
    for (const Json& b : k["meter"].a) { Band x; x.from = b["from"].F(0); x.state = b["state"].Str0(); x.charisma = b["charisma"].F(1); x.toughness = b["toughness"].F(1); d.bands.push_back(x); }
    if (d.bands.empty()) d.bands.push_back(Band{0, "Sober", 1, 1});
    d.soberPerMin = k["sober_per_game_minute"].F(1); d.rise10 = k["price_rise_10pm"].F(1.2f); d.lastCallHour = k["last_call_hour"].F(25.5f); d.lastCallMult = k["last_call_mult"].F(2);
    d.vomitChance = k["vomit_chance_per_minute_wrecked"].F(0.08f); d.vomitDrop = k["vomit_drop"].F(15); d.vomitStun = k["vomit_stun_s"].F(5);
    for (const Json& r : k["drinks"].a) {
        bool gullOnly = false; for (const Json& g : mj["gull_only_drinks"].a) gullOnly |= g.Str0() == r["key"].Str0();
        if (!gullOnly) LoadDrink(r, d.drinks);
    }
    for (const Json& r : mj["drinks"].a) LoadDrink(r, d.drinks);   // (Celeste's cocktails)
    Json b = LoadJsonFile(dir + (bar == BAR_MONKEY ? "nightoff_bar_monkey.json" : "nightoff_bar.json"));
    BarData& B = d.bar;
    B.name = b["name"].Str0("The Sodden Gull"); B.wallH = b["wall_height"].F(3.4f);
    B.key = b["key"].Str0("gull"); B.barkeep = b["barkeep"].Str0("the bartender"); B.fortuneName = b["fortune_name"].Str0("the fortune teller");
    B.priceMul = b["price_mul"].F(1); B.roof = b["roof"].Bool0(false); B.golfHoles = b["golf_holes"].I(9);
    const Json& pal = b["palette"];
    B.wainscot = C3(pal["wainscot"], B.wainscot); B.plaster = C3(pal["plaster"], B.plaster); B.outer = C3(pal["outer"], B.outer); B.rail = C3(pal["rail"], B.rail); B.ceiling = C3(pal["ceiling"], B.ceiling); B.beam = C3(pal["beam"], B.beam);
    for (const auto& kv : b["floors"].o) B.floors.push_back({kv.first, C3(kv.second, Color{116, 80, 50, 255})});
    for (const Json& r : b["rooms"].a) B.rooms.push_back({r["key"].Str0(), r["name"].Str0(), R4(r["rect"])});
    for (const Json& w : b["walls"].a) B.walls.push_back({{w[0].F(0), w[1].F(0)}, {w[2].F(0), w[3].F(0)}});
    for (const Json& x : b["boxes"].a) B.boxes.push_back({x["kind"].Str0(), R4(x["rect"]), x["h"].F(1)});
    for (const Json& s : b["stools"].a) B.stools.push_back(V2(s));
    for (const Json& l : b["lamps"].a) B.lamps.push_back({l[0].F(0), l[1].F(2.8f), l[2].F(0)});
    const Json& s = b["spots"];
    B.bartender = V2(s["bartender"]); B.serve = V2(s["serve"]); B.hatch = V2(s["kitchen_hatch"]); B.door = V2(s["front_door"]); B.spawn = V2(s["spawn"]);
    B.dartboard = V2(s["dartboard"]); B.fortune = V2(s["fortune"]); B.mirror = V2(s["mirror"]); B.jukebox = V2(s["jukebox"]);
    B.scratch = s["scratch"].a.size() == 2 ? V2(s["scratch"]) : Vector2{0.5f, 11.8f}; B.golf = s["golf"].a.size() == 2 ? V2(s["golf"]) : Vector2{12, 38};
    for (const Json& n : b["nav"]["nodes"].a) B.nav.push_back(V2(n));
    B.navLinks.assign(B.nav.size(), {});
    for (const Json& l : b["nav"]["links"].a) { int a = l[0].I(-1), c = l[1].I(-1); if (a >= 0 && c >= 0 && a < (int)B.nav.size() && c < (int)B.nav.size()) { B.navLinks[a].push_back(c); B.navLinks[c].push_back(a); } }
    for (const Json& n : b["nav"]["names"].a) B.navNames.push_back(n.Str0());
    for (const auto& kv : b["seats"].o) { std::vector<Vector2> v; for (const Json& p : kv.second.a) v.push_back(V2(p)); B.seats.push_back({kv.first, v}); }
    // the patrons and what they say
    Json pj = LoadJsonFile(dir + "nightoff_patrons.json");
    for (int t = 0; t < T_COUNT; t++) {
        const Json& x = pj["types"][TypeName(t)];
        d.types[t].want = x["want"].Str0(); d.types[t].approach = x["approach"].Str0(); d.types[t].danger = x["danger"].Str0(); d.types[t].haunt = x["haunt"].Str0("bar");
        d.types[t].tolerance = x["tolerance"].I(4); d.types[t].hp = x["hp"].F(80);
    }
    for (const Json& x : pj["traits"].a) d.traitNames.push_back(x.Str0());
    for (const Json& x : pj["generic_secrets"].a) d.genericSecrets.push_back(x.Str0());
    for (const Json& x : pj["first_names"].a) d.firstNames.push_back(x.Str0());
    for (const Json& x : pj["last_names"].a) d.lastNames.push_back(x.Str0());
    // the regulars: the Gull's, or the Monkey's cast; the five who travel are on both books (one bar a night)
    if (bar == BAR_MONKEY) { for (const Json& r : mj["regulars"].a) LoadRegular(d, r, d.regulars); for (const Json& r : pj["regulars"].a) if (r["travels"].Bool0(false)) LoadRegular(d, r, d.regulars); }
    else for (const Json& r : pj["regulars"].a) LoadRegular(d, r, d.regulars);
    for (const Json& h : pj["crowd"]["hours"].a) d.crowdHours.push_back({h[0].F(19), h[1].F(4), h[2].F(8)});
    d.crowdMul[0] = pj["crowd"]["dead"].F(0.5f); d.crowdMul[1] = pj["crowd"]["normal"].F(1); d.crowdMul[2] = pj["crowd"]["packed"].F(1.5f);
    Json dj = LoadJsonFile(dir + "nightoff_dialogue.json");
    auto lines = [](const Json& a, Lines& out) { for (const Json& x : a.a) out.v.push_back(x.Str0()); if (out.v.empty()) out.v.push_back("..."); };
    static const char* OPT[4] = {"ask", "agree", "joke", "challenge"};
    for (int k = 0; k < 4; k++) {
        lines(dj["you"][OPT[k]], d.talk.you[k]); lines(dj["you"][std::string("drunk_") + OPT[k]], d.talk.youDrunk[k]);
        lines(dj["reply"][std::string(OPT[k]) + "_ok"], d.talk.ok[k]); lines(dj["reply"][std::string(OPT[k]) + "_fail"], d.talk.fail[k]);
    }
    lines(dj["you"]["listen"], d.talk.listen); lines(dj["you"]["buy"], d.talk.buy);
    for (int t = 0; t < T_COUNT; t++) lines(dj["reply"]["greet"][TypeName(t)], d.talk.greet[t]);
    lines(dj["reply"]["listen"], d.talk.listenReply); lines(dj["reply"]["drink"], d.talk.drink); lines(dj["reply"]["end_good"], d.talk.endGood);
    lines(dj["reply"]["end_bad"], d.talk.endBad); lines(dj["reply"]["hostile"], d.talk.hostile);
    for (const Json& x : dj["items"].a) d.talk.items.push_back(x.Str0());
    return d;
}
static int gBar = BAR_GULL;
bool gTravelRolls = true;
void SetBar(int bar) { gBar = std::clamp(bar, 0, BAR_COUNT - 1); }
int CurBar() { return gBar; }
const Data& DataOf(int bar) {
    static Data d0 = Load(BAR_GULL);
    if (bar == BAR_MONKEY) { static Data d1 = Load(BAR_MONKEY); return d1; }
    return d0;
}
const Data& D() { return DataOf(gBar); }
const char* BarName(int bar) { return bar == BAR_MONKEY ? "The Brass Monkey" : "The Sodden Gull"; }
bool TravellerHere(const std::string& name, int bar, uint32_t seed) {
    // the five travellers: one bar a night, from the seed (Dottie goes where the coats are better: the Monkey, two nights in three)
    uint32_t h = seed * 2654435761u; for (char c : name) h = (h ^ (uint8_t)c) * 16777619u;
    float u = ((h >> 8) & 0xffff) / 65536.0f;
    int at = u < (name == "Dottie Finch" ? 0.66f : 0.5f) ? BAR_MONKEY : BAR_GULL;
    return at == bar;
}
const char* TypeName(int t) { static const char* N[T_COUNT] = {"Talker", "Flirt", "Brooder", "Hustler", "Regular", "Gambler", "Sailor", "Oddball", "Staff"}; return N[std::clamp(t, 0, T_COUNT - 1)]; }
int Data::Trait(const std::string& n) const { for (int i = 0; i < (int)traitNames.size(); i++) if (traitNames[i] == n) return i; return -1; }
const std::string& Lines::Pick(uint32_t k) const { static const std::string none = "..."; return v.empty() ? none : v[k % v.size()]; }
const std::vector<Vector2>* BarData::Seats(const std::string& kind) const { for (const auto& s : seats) if (s.first == kind) return &s.second; return nullptr; }
int DrinkIndex(const std::string& key) { const auto& v = D().drinks; for (int i = 0; i < (int)v.size(); i++) if (v[i].key == key) return i; return -1; }
const char* CrewName(int c) { static const char* N[6] = {"Diver", "Whaler", "Stowaway", "Mechanic", "Captain", "Nurse"}; return N[std::clamp(c, 0, 5)]; }
const char* RoomAt(Vector2 p) {
    for (const auto& r : D().bar.rooms) if (r.key != "street" && CheckCollisionPointRec(p, r.r)) return r.name.c_str();
    return "the street";
}
const char* EndingName(int e) {
    static const char* N[] = {"", "Walked home", "Went home with someone", "Passed out", "Knocked out", "Arrested", "Hospitalized", "Robbed and dumped", "Thrown out", "Still here at closing"};
    return N[std::clamp(e, 0, 9)];
}

// ---------------------------------------------------------------- the night
float Night::Rand() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffffff) / 16777216.0f; }
static std::string MonkeyWords(std::string s) {   // (at the Monkey the bartender is Celeste, and the bar is the Monkey)
    auto rep = [&](const char* a, const char* b) { for (size_t i = s.find(a); i != std::string::npos; i = s.find(a, i + strlen(b))) s.replace(i, strlen(a), b); };
    rep("The bartender", "Celeste"); rep("the bartender", "Celeste"); rep("The Sodden Gull", "The Brass Monkey"); rep("the Gull", "the Monkey");
    return s;
}
void Night::Say(const std::string& s0) { std::string s = CurBar() == BAR_MONKEY ? MonkeyWords(s0) : s0; say.push_back(s); if (say.size() > 40) say.erase(say.begin()); }
void Night::Note(Player& p, int kind, const std::string& text) {
    p.log.push_back({t, kind, text});
    // Solo: the bartender narrates everything (doc p. 24)
    if (opts.mode == MD_SOLO) {
        static const char* SAYS[12] = {"", "\"Steady, sailor.\"", "\"I'll get the mop. Again.\"", "\"Nicely done. Don't let it go to your head.\"", "\"Happens to the best of us. Mostly to you.\"",
                                       "\"Now THAT's a story for the regulars.\"", "\"Ask her what the cards say about me.\"", "\"Made a friend? In here?\"", "\"Mind the step.\"", "\"Lights out.\"", "\"That's going on the tab.\"", "\"Ouch.\""};
        if (kind > 0 && kind < 12 && Rand() < 0.7f) Say(std::string("The bartender: ") + SAYS[kind]);
    }
}
std::string Night::Clock() const {
    float h = Hour(); int hh = (int)h, mm = (int)((h - hh) * 60);
    int h12 = hh % 12 == 0 ? 12 : hh % 12;
    return TextFormat("%d:%02d %s", h12, mm, hh % 24 >= 12 ? "p.m." : "a.m.");
}
void Night::Init(const Opts& o) {
    SetBar(o.bar);
    opts = o; rng = o.seed ? o.seed : 1; t = 0; seanceDone = false; goatOwner = -1; over = false; say.clear(); players.clear(); flags.clear(); winner = -1; crewTab = 0; midnightBrawl = false; bartenderDarts = false;
    bar = Bartender{}; bar.pos = D().bar.bartender; patrons.clear();
    static const char* NAMES[6] = {"You", "Player 2", "Player 3", "Player 4", "Player 5", "Player 6"};
    for (int i = 0; i < std::clamp(o.players, 1, 6); i++) {
        Player p; p.id = i; p.name = NAMES[i]; p.crew = i % 6; p.crew2 = i % 2;
        p.pos = Vector2Add(D().bar.spawn, {(i - 2.5f) * 0.9f, 0}); p.yaw = PI * 0.5f;   // (walking in from the street, facing the bar)
        p.swayPh = Rand() * 6.28f;
        players.push_back(p);
    }
    InitPatrons();
    InitProps();
    ScheduleEvents();
    Say(o.mode == MD_SOLO ? "The bartender looks up. \"Just you tonight? I'll keep you company.\"" : "The Sodden Gull, 7 p.m. The bartender looks up.");
    // a night can start late (the tests' short nights): the regulars due by then are already in
    if (o.startMinutes > 0) { t = o.startMinutes * SECONDS_PER_GAME_MINUTE; for (int k = 0; k < 40; k++) StepPatrons(0.5f); }
}
const Band& Night::BandOf(const Player& p) const { const auto& b = D().bands; int k = 0; for (int i = 0; i < (int)b.size(); i++) if (p.drunk >= b[i].from) k = i; return b[k]; }
float Night::Charisma(const Player& p) const { return BandOf(p).charisma * (1 + (p.charBuffT > 0 ? p.charBuff : 0)) * WareCharisma(p); }
float Night::Toughness(const Player& p) const { return BandOf(p).toughness * (1 + (p.toughBuffT > 0 ? p.toughBuff : 0)) * (p.jacket ? 1.2f : 1.0f) * WareToughness(p); }   // (a biker's jacket: +20%)
float Night::PriceOf(int i) const {
    if (i < 0 || i >= (int)D().drinks.size()) return 0;
    if (freeDrinks) return 0;   // (the shotgun fired at the cartel: the bar is yours)
    float p = D().drinks[i].price * D().bar.priceMul, h = Hour();   // (the Monkey's price board: everything 50% more)
    if (lockIn && D().drinks[i].drunk > 0) p *= 2;   // (the lock-in: double prices)
    if (h >= 22) p *= D().rise10;                                   // (prices rise 20% at 10 p.m.)
    if (h >= D().lastCallHour) p *= D().lastCallMult;               // (and double at last call)
    if (bar.mood >= 80) p *= 0.9f; else if (bar.mood < 30) p *= 1.2f;   // (the bartender's mood: Delighted cheaper, Annoyed dearer)
    return roundf(p);
}
bool Night::NearServe(const Player& p) const { return Vector2Distance(p.pos, D().bar.serve) < 2.4f || (p.pos.y < 9 && p.pos.y > 7.2f && p.pos.x > 12 && p.pos.x < 21.5f); }
bool Night::NearHatch(const Player& p) const { return Vector2Distance(p.pos, D().bar.hatch) < 2.0f; }
bool Night::NearDoor(const Player& p) const { return Vector2Distance(p.pos, D().bar.door) < 1.8f; }
bool Night::Order(Player& p, int i, std::string* why) {
    if (p.st != State::Active || i < 0 || i >= (int)D().drinks.size()) return false;
    const DrinkDef& d = D().drinks[i];
    bool kitchen = d.where == "kitchen";
    if (kitchen ? !NearHatch(p) : !NearServe(p)) { if (why) *why = kitchen ? "Order food at the kitchen hatch." : "Order at the bar."; return false; }
    // the bartender cuts you off when he's Annoyed and you're past 60, and everyone at last call who's Wrecked
    if (!kitchen && d.drunk > 0 && ((bar.mood < 30 && p.drunk >= 60) || (Hour() >= D().lastCallHour && p.drunk >= 80))) { if (why) *why = "\"You've had enough, sailor.\""; Say("The bartender: \"You've had enough.\""); return false; }
    if (!kitchen && p.barred) { if (why) *why = "\"You're barred. Water, and then the door.\""; return false; }
    if (!kitchen && opts.mode == MD_SOBER && d.drunk > 0) { if (why) *why = "\"I'm on strike tonight. Water or coffee.\""; return false; }
    if (p.fight.brawl >= 0) { if (why) *why = "Not in the middle of a fight."; return false; }
    float price = p.barkeepT > 0 ? 0 : PriceOf(i) * p.priceMul;   // (the Cocktail's third: you're the bartender, and you drink free)
    if (!kitchen && price > 0) for (auto& q : players) if (q.barkeepT > 0 && q.id != p.id) q.money += roundf(price * 0.25f);   // (and every drink you pour tips you)   // (the bartender's memory: a regular's price, or his window's)
    if (p.money - p.tab < price && !kitchen) { if (why) *why = "Your tab's bigger than your wages."; return false; }   // (drinks go on the tab: doc p. 19)
    if (kitchen) { if (p.money < price) { if (why) *why = "Not enough money."; return false; } p.money -= price; }
    else p.tab += price;
    p.spent += price;
    p.st = kitchen ? State::Eating : State::Drinking;
    p.actT = kitchen ? std::max(2.0f, d.eatS) : 2.5f;
    p.acting = i;
    if (!kitchen) { bar.busyT = 1.5f; bar.servingFor = p.id; if (d.key == "water") bar.mood = std::max(0.0f, bar.mood - 1); }
    return true;
}
static void Finish(Night& n, Player& p, int i) {
    const Data& dd = D();
    const DrinkDef& d = dd.drinks[i];
    float amount = d.drunk;
    if (d.effect == "spit" && n.Rand() < 0.1f) { amount = 5; n.Say("The cook watches you eat with an unsettling smile."); }
    if (amount > 0 && p.kidneys < 2) amount *= 2;   // (the kidney: every drink counts double)
    p.drunk = std::clamp(p.drunk + amount, 0.0f, 100.0f);
    if (amount > 0) p.drinks++;
    p.peakDrunk = std::max(p.peakDrunk, p.drunk);
    float sec = d.minutes * SECONDS_PER_GAME_MINUTE;   // (effects last game minutes)
    if (d.effect == "charisma") { p.charBuff = d.amount; p.charBuffT = sec; }
    else if (d.effect == "toughness") { p.toughBuff = d.amount; p.toughBuffT = sec; }
    else if (d.effect == "honest") p.honestT = sec;
    else if (d.effect == "visions") p.visionsT = sec;
    else if (d.effect == "shakes") p.shakesT = sec;
    else if (d.effect == "steady") p.steadyT = sec;                                     // (the Marlin: the cue's wobble halved)
    else if (d.effect == "widow") { p.honestT = std::max(p.honestT, sec); p.visionsT = std::max(p.visionsT, sec); }   // (the Widow's Kiss)
    else if (d.effect == "round") { for (auto& c : n.patrons) if (c.inside && !c.gone) c.mood = std::min(100.0f, c.mood + 10); p.roundT = n.t; n.Say(p.name + " pops a bottle of champagne and pours for the room."); n.Note(p, 5, "Bought the room champagne."); }
    else if (d.effect == "crew") { p.charBuff = d.amount; p.charBuffT = 20 * SECONDS_PER_GAME_MINUTE; }
    else if (d.effect == "random") {   // the Gull: one of the others doubled, or a hiccup
        int r = (int)(n.Rand() * 5);
        if (r == 0) { p.charBuff = 0.10f; p.charBuffT = 4 * SECONDS_PER_GAME_MINUTE; }
        else if (r == 1) { p.toughBuff = 0.20f; p.toughBuffT = 4 * SECONDS_PER_GAME_MINUTE; }
        else if (r == 2) p.honestT = 4 * SECONDS_PER_GAME_MINUTE;
        else if (r == 3) p.visionsT = 2 * SECONDS_PER_GAME_MINUTE;
        else p.hiccup = true;
    }
    if (d.key == "water") n.Say("The bartender pours a water. He judges you.");
    n.Note(p, 1, d.name);
    if (p.drunk >= 100) { n.Leave(p, E_PASSED_OUT, ""); }
}
void Night::Leave(Player& p, int ending, const std::string& where) {
    if (p.st == State::Gone || p.st == State::PassedOut) return;
    p.ending = ending;
    if (ending == E_PASSED_OUT) {
        // passed out: you wake somewhere, minus some money (doc p. 3); the bartender takes the tab first
        p.st = State::PassedOut;
        static const char* WAKE[5] = {"on the floor of the snug", "in the alley, the dog asleep on your feet", "on a stranger's porch three streets away", "in the bathtub upstairs (no ice, thankfully)", "under the pool table"};
        p.wokeAt = WAKE[(int)(Rand() * 5) % 5];
        float lost = std::min(p.money, roundf(p.money * Rand(0.1f, 0.35f)));
        float paid = std::min(p.money - lost, p.tab); p.money -= lost + paid; p.tab -= paid; p.lostOnPass = lost;
        Note(p, 9, TextFormat("Blacked out at %s; woke %s, %.0f lighter.", Clock().c_str(), p.wokeAt.c_str(), lost));
        Say(p.name + (p.name == "You" ? " have" : " has") + " passed out.");
    } else {
        p.st = State::Gone;
        if (ending == E_ARRESTED) { p.money -= 200; Note(p, 9, "Fined 200 by the harbour magistrate."); }   // (night over, a 200 fine, a mugshot)
        if (ending == E_HOSPITAL) { p.money -= 100; }                                                    // (the hospital's bill)
        p.wokeAt = where.empty() ? (ending == E_WALKED ? "in your own bunk on the Nautilus" : "somewhere") : where;
        // the tab is settled at the door (doc p. 19)
        if (!p.ferried) { float paid = std::min(p.money, p.tab); p.money -= paid; p.tab -= paid; }   // (the ferry: the tab follows you across)
        if (ending == E_WALKED && !p.ferried) Note(p, 8, TextFormat("Walked home at %s%s.", Clock().c_str(), p.drunk < 20 ? ", sober" : ""));
        Say(p.name + (p.name == "You" ? " head" : " heads") + " for the door.");
    }
}
void Night::Collide(Vector2& pos, float r) const {
    const BarData& B = D().bar;
    for (int it = 0; it < 2; it++) {
        for (const auto& w : B.walls) {
            Vector2 ab = Vector2Subtract(w.b, w.a); float L2 = Vector2LengthSqr(ab);
            float k = L2 > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(pos, w.a), ab) / L2, 0.0f, 1.0f) : 0;
            Vector2 c = Vector2Add(w.a, Vector2Scale(ab, k)), d = Vector2Subtract(pos, c);
            float L = Vector2Length(d), rr = r + 0.15f;
            if (L < rr) { if (L < 1e-4f) { d = {0, 1}; L = 1; } pos = Vector2Add(c, Vector2Scale(d, rr / L)); }
        }
        for (const auto& b : B.boxes) {
            Vector2 c{std::clamp(pos.x, b.r.x, b.r.x + b.r.width), std::clamp(pos.y, b.r.y, b.r.y + b.r.height)};
            Vector2 d = Vector2Subtract(pos, c); float L = Vector2Length(d);
            if (L < r) {
                if (L < 1e-4f) {   // (inside the box: out the nearest side)
                    float l = pos.x - b.r.x, rt = b.r.x + b.r.width - pos.x, tp = pos.y - b.r.y, bt = b.r.y + b.r.height - pos.y, m = std::min({l, rt, tp, bt});
                    if (m == l) pos.x = b.r.x - r; else if (m == rt) pos.x = b.r.x + b.r.width + r; else if (m == tp) pos.y = b.r.y - r; else pos.y = b.r.y + b.r.height + r;
                } else pos = Vector2Add(c, Vector2Scale(d, r / L));
            }
        }
    }
    // the night's edges: the street's far kerb, the yard's fence, the alley's wall
    pos.x = std::clamp(pos.x, -8.6f, 48.0f); pos.y = std::clamp(pos.y, -9.0f, 49.5f);
}
void Night::StepPlayer(Player& p, float dt) {
    auto dec = [&](float& x) { x = std::max(0.0f, x - dt); };
    dec(p.charBuffT); dec(p.toughBuffT); dec(p.honestT); dec(p.visionsT); dec(p.shakesT); dec(p.stumbleT); dec(p.toastT); dec(p.barkeepT); dec(p.steadyT);
    if (p.st == State::Gone || p.st == State::PassedOut) return;
    StepWares(p, dt); StepFate(p);
    if (p.skipT > 0 || p.st == State::Gone || p.st == State::PassedOut) return;   // (Deep Pressure's lost minutes; the Cocktail's mornings)
    if (p.st == State::Down) { p.vel = {0, 0}; return; }   // (knocked out: StepBrawls counts the 30 s)
    // time sobers you: 1 a game minute (about 15 a real minute)
    p.drunk = std::max(0.0f, p.drunk - D().soberPerMin * dt / SECONDS_PER_GAME_MINUTE);
    if (p.st == State::Drinking || p.st == State::Eating) {
        p.actT -= dt; p.vel = Vector2Scale(p.vel, 0.8f);
        if (p.actT <= 0) { int i = p.acting; p.st = State::Active; p.acting = -1; Finish(*this, p, i); }
        return;
    }
    if (p.st == State::Vomiting) {
        p.vomitT -= dt; p.vel = {0, 0};
        if (p.vomitT <= 0) p.st = State::Active;
        return;
    }
    // Wrecked: now and then you throw up (a 5 s stun that lowers the meter by 15)
    if (p.drunk >= 80 && Rand() < D().vomitChance * dt / SECONDS_PER_GAME_MINUTE) {
        p.st = State::Vomiting; p.vomitT = D().vomitStun; p.drunk = std::max(0.0f, p.drunk - D().vomitDrop);
        Note(p, 2, TextFormat("Threw up in %s at %s.", RoomAt(p.pos), Clock().c_str()));
        Say(p.name + (p.name == "You" ? " are" : " is") + " sick.");
        return;
    }
    // walking: the drunk curve (a weave that grows with the meter), stumbles above 60, a lurch when turning hard
    Input& in = p.in;
    Vector2 wish{in.moveX, in.moveZ};
    float wl = Vector2Length(wish); if (wl > 1) wish = Vector2Scale(wish, 1 / wl);
    float k = std::clamp(p.drunk / 100, 0.0f, 1.0f);
    p.swayPh += dt * (1.3f + 0.8f * k);
    float speed = (in.run && p.drunk < 60 ? 5.0f : 3.0f) * (1 - 0.25f * k);
    if (p.fight.Busy()) speed = 0;                                   // (stunned, fallen over, held, smashing a bottle)
    if (p.fight.grabbing.Valid()) speed *= 0.4f;
    if (p.fight.windT > 0) speed *= 0.35f;
    if (wl > 0.05f) {
        float weave = (p.drunk >= 40 ? 0.35f + 0.9f * (k - 0.4f) : 0) * sinf(p.swayPh * 1.7f);
        float c = cosf(weave), s = sinf(weave);
        wish = {wish.x * c - wish.y * s, wish.x * s + wish.y * c};
        if (p.drunk >= 60 && p.stumbleT <= 0 && Rand() < dt * 0.12f * (k * 2 - 1)) { p.stumbleT = 0.9f; p.stumbleDir = Rand(-1, 1); }
    }
    Vector2 want = Vector2Scale(wish, speed);
    if (p.stumbleT > 0) { Vector2 side{-wish.y, wish.x}; want = Vector2Add(Vector2Scale(want, 0.4f), Vector2Scale(side, p.stumbleDir * 2.2f)); }
    float acc = 10 * (1 - 0.6f * k);
    p.vel = Vector2Lerp(p.vel, want, std::min(1.0f, dt * acc));
    if (Vector2Length(p.vel) > 0.2f) { float ty = atan2f(p.vel.y, p.vel.x), dyaw = atan2f(sinf(ty - p.yaw), cosf(ty - p.yaw)); p.yaw += dyaw * std::min(1.0f, dt * (8 - 4 * k)); p.lurch = std::clamp(dyaw, -1.0f, 1.0f) * k; }
    p.pos = Vector2Add(p.pos, Vector2Scale(p.vel, dt));
    Collide(p.pos, 0.32f);
    PlayerFightInput(p, dt);
    PlayerTricks(p);
    // a conversation: start one, choose in one
    if (in.talkTo >= 0) { StartTalk(p, in.talkTo); in.talkTo = -1; }
    if (in.say >= 0) { TalkChoose(p, in.say); in.say = -1; }
    if (p.talk.patron >= 0) {
        Patron& c = patrons[p.talk.patron];
        p.vel = Vector2Scale(p.vel, 0.85f);
        if (Vector2Distance(c.pos, p.pos) > 3.5f || c.gone || !c.inside) EndTalk(p);
        else if (p.talk.over && (p.talk.overT -= dt) <= 0) EndTalk(p);
    }
    // flirting (nightoff_flirt.cpp): start one (from a conversation too), choose a line, take or decline the offer
    if (in.flirtWith >= 0) { if (p.talk.patron == in.flirtWith) EndTalk(p); StartFlirt(p, in.flirtWith); in.flirtWith = -1; }
    if (in.flirtSay >= 0) { FlirtChoose(p, in.flirtSay); in.flirtSay = -1; }
    if (in.offer) { FlirtOffer(p, in.offer == 1); in.offer = 0; }
    if (p.flirt.patron >= 0) {
        Patron& c = patrons[p.flirt.patron];
        p.vel = Vector2Scale(p.vel, 0.85f);
        if (Vector2Distance(c.pos, p.pos) > 3.5f || c.gone || !c.inside) EndFlirt(p);
        else if (p.flirt.over && (p.flirt.overT -= dt) <= 0) EndFlirt(p);
    }
    if (in.askTrouble) { AskTrouble(p); in.askTrouble = false; }
    if (in.fortuneYes) { in.fortuneYes = false; if (p.game.kind == GK_FORTUNE && p.fortuneAsked) { EndGame(p); GoHome(p, -1, "fortune"); return; } }
    // a bad night's 30 s at the door: a teammate who gets there first stops it (doc p. 14)
    if (p.leavingT > 0) {
        p.vel = {0, 0}; p.pos = Vector2Lerp(p.pos, D().bar.door, std::min(1.0f, dt * 2));
        bool saved = false; for (const auto& q : players) if (q.id != p.id && (q.st == State::Active || q.st == State::Drinking) && Vector2Distance(q.pos, D().bar.door) < 3.5f) saved = true;
        if (saved) {
            Patron& c = patrons[std::clamp(p.leavingWith, 0, (int)patrons.size() - 1)];
            Say("A short scuffle at the door: " + c.name + " runs for it."); c.gone = true; c.inside = false; c.playing = -1;
            for (auto& q : players) if (q.id != p.id && Vector2Distance(q.pos, D().bar.door) < 3.5f) Note(q, 5, "Stopped " + c.name + " at the door before they could take " + p.name + " home.");
            Note(p, 5, "Was rescued from " + c.name + " at the door."); p.leavingT = 0; p.leavingWith = -1;
        } else if ((p.leavingT -= dt) <= 0) { p.leavingT = 0; int w = p.leavingWith; p.leavingWith = -1; GoHome(p, w, patrons[std::clamp(w, 0, (int)patrons.size() - 1)].home); return; }
        return;
    }
    // the bar games
    if (in.startGame >= 0) { std::string why; if (!StartGame(p, in.startGame, in.gameMachine, in.gameOpp, in.gameStake, &why) && !why.empty() && p.id == 0) Say(why); in.startGame = -1; }
    if (in.gameAct) { GameAction(p); in.gameAct = 0; }
    // the menu and the door
    if (in.order >= 0) { std::string why; if (!Order(p, in.order, &why) && !why.empty() && p.id == 0) Say(why); in.order = -1; }
    if (in.leave && NearDoor(p)) {
        if (lockIn) Say("The door's bolted till four. (There's always the toilet window.)");
        else if (p.adopted && EventOn("bachelor")) Say("The groom hauls you back in: \"ONE MORE! YOU'RE MY BEST FRIEND!\"");
        else Leave(p, E_WALKED, "");
    }
    if (in.evAct) { EventAction(p, in.evAct, in.evArg); in.evAct = 0; in.evArg = -1; }
    in.leave = false;
}
void Night::Step(float dt) {
    SetBar(opts.bar);   // (the data is the bar's: two nights in one process at different bars each set their own)
    if (over) return;
    t += dt;
    // the bartender: polishes, serves, walks to whoever ordered; his mood drifts with the hour (tired after 1 a.m.)
    bar.busyT = std::max(0.0f, bar.busyT - dt); bar.polishPh += dt;
    Vector2 goal = D().bar.bartender;
    if (bar.servingFor >= 0 && bar.servingFor < (int)players.size() && bar.busyT > 0) goal = {std::clamp(players[bar.servingFor].pos.x, 13.0f, 20.5f), 11.0f};
    bar.pos = Vector2Lerp(bar.pos, goal, std::min(1.0f, dt * 2));
    if (Hour() >= 25) bar.mood = std::max(0.0f, bar.mood - dt * 0.02f);
    StepPatrons(dt);
    for (auto& p : players) StepPlayer(p, dt);
    StepGames(dt);
    StepBrawls(dt);
    StepModes(dt);
    StepEvents(dt);
    StepCards(dt);
    StepMonkey(dt);
    // 3 a.m., or every night over
    bool anyone = false; for (const auto& p : players) anyone |= p.st != State::Gone && p.st != State::PassedOut;
    if (Minutes() >= endMinutes) { for (auto& p : players) if (p.st != State::Gone && p.st != State::PassedOut) Leave(p, p.st == State::Down ? E_KNOCKED_OUT : E_CLOSING, p.st == State::Down ? (CurBar() == BAR_MONKEY ? "on the Monkey's marble floor with a black eye" : "on the floor of the Gull with a black eye") : std::string("on the pavement outside ") + (CurBar() == BAR_MONKEY ? "the Monkey" : "the Gull") + " at 3 a.m., swept out with the glass"); anyone = false; }
    if (!anyone) over = true;
}
// ---------------------------------------------------------------- the morning (doc pp. 3-4)
std::string Night::MorningLine(const Player& p) const { auto s = MorningStory(p); return s.empty() ? std::string() : s[0]; }

// ---------------------------------------------------------------- the check (stage 1's gate: walk in, drink, stumble, pass out)
int NightGamesChecks();
int NightBrawlChecks();
int NightFlirtChecks();
int NightEventChecks();
int NightCardChecks();
int NightProfileChecks();
int NightWaresChecks();
int NightMonkeyChecks();
int RunNightTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("A Night Off: stage 1\n");
    gTravelRolls = false;   // (the earlier stages' checks were written before the Monkey: every traveller drinks at the Gull)
    const Data& d = D();
    check(d.drinks.size() == 12 && d.bands.size() == 6, TextFormat("the menu (%d) and the meter (%d bands)", (int)d.drinks.size(), (int)d.bands.size()));
    check(d.bar.walls.size() > 15 && d.bar.boxes.size() > 15 && d.bar.rooms.size() >= 10, "the Sodden Gull's rooms, walls and furniture");
    Night n; Opts o; o.players = 1; o.seed = 3; n.Init(o);
    Player& p = n.players[0];
    check(std::string(RoomAt(p.pos)) == "The main bar" || std::string(RoomAt(p.pos)) == "The front door", std::string("a player walks in at ") + RoomAt(p.pos));
    check(n.Clock() == "7:00 p.m.", "the clock starts at 7 p.m.");
    // walk to the bar: the counter stops you
    for (int i = 0; i < 400; i++) { Vector2 to = Vector2Subtract(d.bar.serve, p.pos); float L = Vector2Length(to); p.in.moveX = L > 0.3f ? to.x / L : 0; p.in.moveZ = L > 0.3f ? to.y / L : 0; n.Step(0.02f); }
    check(n.NearServe(p) && p.pos.y < 9.0f, TextFormat("walks to the bar and stops at the counter (%.1f, %.1f)", p.pos.x, p.pos.y));
    p.in = Input{};
    // drink: the meter, the tab, the bands
    int pint = DrinkIndex("pint"), rum = DrinkIndex("rum"), water = DrinkIndex("water");
    check(n.Order(p, pint) && p.st == State::Drinking && p.tab == 5, "a pint goes on the tab");
    for (int i = 0; i < 200; i++) n.Step(0.02f);
    check(p.st == State::Active && fabsf(p.drunk - 20) < 1, TextFormat("a pint: drunk %.1f, Merry", p.drunk));
    p.drunk = 25;
    check(n.BandOf(p).state == "Merry" && fabsf(n.Charisma(p) - 1.1f) < 0.01f && fabsf(n.Toughness(p) - 1.1f) < 0.01f, "Merry: charisma and toughness 110%");
    n.Order(p, rum); for (int i = 0; i < 200; i++) n.Step(0.02f);
    check(p.charBuffT > 0 && n.Charisma(p) < 1.0f, "rum: sea-legs confidence, but Drunk already costs charisma");
    // time sobers: 15 a real minute
    float before = p.drunk; for (int i = 0; i < 3000; i++) n.Step(0.02f);
    check(fabsf((before - p.drunk) - 15) < 1.5f, TextFormat("a real minute sobers 15 (%.1f)", before - p.drunk));
    // prices rise at 10 p.m. and double at last call
    n.t = (22 - 19) * 60 * SECONDS_PER_GAME_MINUTE + 1; check(n.PriceOf(pint) == 6, "at 10 p.m. a pint is 6");
    n.t = (25.5f - 19) * 60 * SECONDS_PER_GAME_MINUTE + 1; check(n.PriceOf(pint) == 12, "at last call a pint is 12");
    n.t = 60 * SECONDS_PER_GAME_MINUTE;
    // stumble: walking hammered weaves and stumbles
    p.drunk = 75; float wander = 0; Vector2 start = p.pos;
    for (int i = 0; i < 600; i++) { p.in.moveX = -1; p.in.moveZ = 0; n.Step(0.02f); wander = std::max(wander, fabsf(p.pos.y - start.y)); if (p.pos.x < 11) { p.pos = start; } }
    check(wander > 0.4f, TextFormat("hammered, a straight walk weaves (%.2f m off the line)", wander));
    // pass out: drink to 100
    p.in = Input{}; p.pos = d.bar.serve; p.st = State::Active; p.drunk = 70;
    int abs = DrinkIndex("absinthe");
    for (int k = 0; k < 4 && p.st != State::PassedOut; k++) { n.Order(p, abs); for (int i = 0; i < 200; i++) n.Step(0.02f); }
    check(p.st == State::PassedOut && p.ending == E_PASSED_OUT && !p.wokeAt.empty(), "drink to 100 and black out: the night ends, you wake " + p.wokeAt);
    check(n.over, "the night is over when the last player's is");
    printf("  the morning: \"%s\"; %s\n", n.Headline().c_str(), n.MorningLine(p).c_str());
    // walking home settles the tab
    Night m; m.Init(o); Player& q = m.players[0];
    q.pos = d.bar.serve; m.Order(q, pint); for (int i = 0; i < 200; i++) m.Step(0.02f);
    q.pos = d.bar.door; q.in.leave = true; m.Step(0.02f);
    check(q.st == State::Gone && q.ending == E_WALKED && q.tab == 0 && q.money == 195, "walking home settles the tab at the door");
    m.Init(o); Player& w = m.players[0]; w.pos = d.bar.serve; w.drunk = 30;
    check(m.Order(w, water) && w.tab == 0, "water is free (and the bartender judges you)");
    // ---- stage 2: a dead night with the patrons (its gate: a dead night is playable alone)
    {
        Night e; Opts eo; eo.players = 1; eo.crowd = 0; eo.seed = 21; eo.events = false; e.Init(eo);
        Player& y = e.players[0];
        int max8 = 0, max23 = 0, sat = 0;
        for (int i = 0; i < (int)(5 * 60 * SECONDS_PER_GAME_MINUTE / 0.05f); i++) {   // (7 p.m. to midnight)
            e.Step(0.05f);
            int in = 0; for (const auto& c : e.patrons) in += c.inside && !c.gone;
            if (e.Hour() < 20) max8 = std::max(max8, in);
            if (e.Hour() > 22.5f && e.Hour() < 23.5f) max23 = std::max(max23, in);
            if (e.Hour() > 21) for (const auto& c : e.patrons) sat += c.sitting;
        }
        check(max8 >= 1 && max8 <= 8, TextFormat("a dead night: %d in by 8 p.m.", max8));
        check(max23 > max8 && max23 <= 22, TextFormat("the room fills toward the peak (%d at 11 p.m.)", max23));
        check(sat > 0, "patrons walk to their seats and sit");
        bool stuck = false; for (const auto& c : e.patrons) if (c.inside && !c.gone) { std::string room = RoomAt(c.pos); if (room == "the street" && !c.leaving) stuck = true; }
        check(!stuck, "nobody is stuck in the street");
        // talk to someone: the mini-game runs and ends
        int who = -1; for (const auto& c : e.patrons) if (c.inside && !c.gone && !c.leaving && c.mood >= 20 && c.type != T_STAFF) { who = c.id; break; }
        check(who >= 0, "someone to talk to");
        if (who >= 0) {
            Patron& c = e.patrons[who]; y.pos = Vector2Add(c.pos, {0.8f, 0}); y.st = State::Active; y.drunk = 25;
            e.StartTalk(y, who);
            check(y.talk.patron == who && c.talkingTo == 0 && !y.talk.theirLine.empty(), "a conversation opens: \"" + y.talk.theirLine + "\"");
            float m0 = c.mood; e.TalkChoose(y, 5);
            check(c.mood > m0 && y.tab > 0, "buying them a drink: their mood rises, your tab too");
            for (int k = 0; k < 6 && !y.talk.over; k++) e.TalkChoose(y, k % 2);
            check(y.talk.over && !y.talk.result.empty(), "it ends: " + y.talk.result);
            e.EndTalk(y);
            check(c.talkingTo < 0, "and they go back to their night");
            // drunk captions: at 80+, half the time the most insulting thing comes out
            int subs = 0; for (int k = 0; k < 200; k++) { y.drunk = 85; y.talk = Talk{}; c.talkingTo = -1; c.mood = 60; c.bothers = 0; y.pos = Vector2Add(c.pos, {0.8f, 0}); e.StartTalk(y, who); e.TalkChoose(y, 1); subs += y.talk.substituted; e.EndTalk(y); }
            check(subs > 60 && subs < 140, TextFormat("wrecked, the wrong words come out about half the time (%d of 200)", subs));
        }
    }
    // ---- stage 3: the bar games in the night
    fails += NightGamesChecks();
    // ---- stage 4: fights, weapons, the mess and the bill
    fails += NightBrawlChecks();
    // ---- stage 5: flirting, going home, the morning
    fails += NightFlirtChecks();
    // ---- stage 7: the events
    fails += NightEventChecks();
    // ---- stage 8: the card room, cheating, side bets
    fails += NightCardChecks();
    // ---- stage 9: the profile across nights
    fails += NightProfileChecks();
    // ---- stage 10a: the cartel's wares
    fails += NightWaresChecks();
    // ---- stage 10c: the Brass Monkey
    gTravelRolls = true;
    fails += NightMonkeyChecks();
    printf(fails ? "A Night Off: %d check(s) FAILED\n" : "A Night Off: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace no
