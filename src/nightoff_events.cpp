// A Night Off: the events (doc pp. 20-23), rain, and the crowd's moods (stage 7). Headless. Events walk in the front
// door: two or three a night, rolled against the hour and the crowd, each changing what the bar is for an hour. Their
// people are ordinary patrons with an event and a role (so they walk, talk, play and fight like everyone); what's
// special about each event is here, and what a player can do about it is EventOptions/EventAction (through Input).
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace no {

struct EventDef {
    std::string key, name, arrive, book; float from = 20, to = 24, weight[3] = {1, 1, 1};
    std::vector<std::string> roles, lines, dares, bookLines; Color top{120, 100, 80, 255};
    std::map<std::string, float> num;
    float N(const char* k, float d) const { auto it = num.find(k); return it == num.end() ? d : it->second; }
};
struct EventsData { std::vector<EventDef> ev; int perNight[3][2] = {{1, 2}, {2, 3}, {3, 4}}; float minutes = 60, sendHome = 0.33f, homeMinutes = 20, rainChance = 0.35f; };
static const EventsData& ED() {
    static EventsData d = [] {
        EventsData d; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_events.json");
        const char* PN[3] = {"dead", "normal", "packed"};
        for (int k = 0; k < 3; k++) { d.perNight[k][0] = j["per_night"][PN[k]][0].I(2); d.perNight[k][1] = j["per_night"][PN[k]][1].I(3); }
        d.minutes = j["event_minutes"].F(60); d.sendHome = j["fight_sends_home"].F(0.33f); d.homeMinutes = j["home_minutes"].F(20); d.rainChance = j["rain_chance"].F(0.35f);
        for (const Json& e : j["events"].a) {
            EventDef x; x.key = e["key"].Str0(); x.name = e["name"].Str0(); x.arrive = e["arrive"].Str0(); x.from = e["from"].F(20); x.to = e["to"].F(24); x.book = e["book"].Str0();
            for (int k = 0; k < 3; k++) x.weight[k] = e["weight"][k].F(1);
            for (const Json& s : e["roles"].a) x.roles.push_back(s.Str0());
            for (const Json& s : e["lines"].a) x.lines.push_back(s.Str0());
            for (const Json& s : e["dares"].a) x.dares.push_back(s.Str0());
            for (const Json& s : e["book_lines"].a) x.bookLines.push_back(s.Str0());
            x.top = {(unsigned char)e["top"][0].I(120), (unsigned char)e["top"][1].I(100), (unsigned char)e["top"][2].I(80), 255};
            for (const auto& kv : e.o) if (kv.second.type == Json::Num) x.num[kv.first] = kv.second.F(0);
            d.ev.push_back(x);
        }
        return d;
    }();
    return d;
}
static int DefIndex(const char* key) { const auto& e = ED().ev; for (int i = 0; i < (int)e.size(); i++) if (e[i].key == key) return i; return -1; }
static const EventDef& Def(const Night::EventRun& r) { return ED().ev[std::clamp(r.def, 0, (int)ED().ev.size() - 1)]; }
int Night::EventIndex(const char* key) const { int d = DefIndex(key); for (int i = 0; i < (int)events.size(); i++) if (events[i].def == d) return i; return -1; }
std::string Night::EventKey(int def) const { return def >= 0 && def < (int)ED().ev.size() ? ED().ev[def].key : std::string(); }
std::string Night::EventName(int def) const { return def >= 0 && def < (int)ED().ev.size() ? ED().ev[def].name : std::string(); }
bool Night::EventOn(const char* key) const { int i = EventIndex(key); return i >= 0 && events[i].started && !events[i].done; }

// ---------------------------------------------------------------- the schedule
void Night::ScheduleEvents() {
    events.clear(); raining = false; lockIn = false; freeDrinks = false; scratchEaten = false; safeOpened = false; goatOn = false; partied = false; endMinutes = NIGHT_MINUTES;
    const EventsData& D_ = ED();
    if (!opts.events) { rainH = 99; return; }
    int c = std::clamp(opts.crowd == 3 ? (int)(Rand() * 3) : opts.crowd, 0, 2);
    int n = D_.perNight[c][0] + (int)(Rand() * (D_.perNight[c][1] - D_.perNight[c][0] + 1));
    if (opts.mode == MD_SOLO) n = std::min(n, 2);
    std::vector<int> pool; for (int i = 0; i < (int)D_.ev.size(); i++) if (D_.ev[i].key != "goat") pool.push_back(i);
    for (int k = 0; k < n && !pool.empty(); k++) {
        float tot = 0; for (int i : pool) tot += D_.ev[i].weight[c];
        float u = Rand() * tot; int pick = pool.back();
        for (int i : pool) { u -= D_.ev[i].weight[c]; if (u <= 0) { pick = i; break; } }
        pool.erase(std::find(pool.begin(), pool.end(), pick));
        EventRun r; r.def = pick; const EventDef& e = D_.ev[pick];
        r.startH = e.from + Rand() * std::max(0.0f, e.to - e.from);
        events.push_back(r);
    }
    if (Rand() < 0.05f) { EventRun r; r.def = DefIndex("goat"); r.startH = 19.5f + Rand() * 6; events.push_back(r); }   // (any hour, 5%)
    std::sort(events.begin(), events.end(), [](const EventRun& a, const EventRun& b) { return a.startH < b.startH; });
    if (Rand() < D_.rainChance) rainH = 20 + Rand() * 5; else rainH = 99;
}
int Night::ForceEvent(const char* key, float hour) {
    int d = DefIndex(key); if (d < 0) return -1;
    int i = EventIndex(key);
    if (i < 0) { EventRun r; r.def = d; events.push_back(r); i = (int)events.size() - 1; }
    events[i].startH = hour; events[i].started = false; events[i].done = false;
    return i;
}

// ---------------------------------------------------------------- the people
int Night::AddEventPatron(int run, const std::string& role, Vector2 at, Color top) {
    int id = AddPatron(-1);
    Patron& c = patrons[id];
    c.ev = run; c.role = role; c.name = role; c.secret = "here for " + Def(events[run]).name;
    c.inside = true; c.arriveH = Hour(); c.leaveH = 27.5f; c.pos = Vector2Add(at, {(Rand() - 0.5f) * 1.5f, (Rand() - 0.5f) * 1.0f});
    c.look.top = top; c.look.hat = {(unsigned char)(top.r / 2), (unsigned char)(top.g / 2), (unsigned char)(top.b / 2), 255};
    c.mood = 65; c.home = "sincere"; c.thief = false; c.traits = 0;
    const std::string& k = Def(events[run]).key;
    if (k == "bikers" || k == "rivals") { c.type = k == "rivals" ? T_SAILOR : T_REGULAR; c.traits |= 1u << std::max(0, D().Trait("violent")); c.look.build = 1.2f; }
    if (k == "bachelor") c.type = T_SAILOR;
    if (k == "bachelorette") c.type = T_FLIRT;
    if (k == "police" || k == "cartel" || k == "robbery") c.type = T_STAFF;
    if (k == "wake") { c.type = T_BROODER; c.mood = 45; }
    if (k == "band") c.type = T_STAFF;
    c.nextGoalT = 1e9f; c.goal = c.pos;
    events[run].people.push_back(id);
    return id;
}
static Vector2 Spot(const char* where, int k) {
    const BarData& B = D().bar;
    std::string w = where;
    if (w == "door") return B.spawn;
    if (w == "kitchen") return {26, 16};
    if (w == "bar") return {13.6f + (k % 7) * 1.0f, 7.8f - (k / 7) * 0.8f};
    if (w == "games") return {3 + (k % 4) * 1.6f, 5.5f + (k / 4) * 3.2f};
    if (w == "dance") return {14 + (k % 4) * 1.6f, 14 + (k / 4) * 1.6f};
    if (w == "snug") return {33.5f + (k % 3) * 1.2f, 3 + (k / 3) * 1.4f};
    if (w == "cards") return {37.5f, 13 + k * 1.2f};
    if (w == "stage") return {14.5f + k * 2.0f, 20.5f};
    return B.serve;
}
static void Goal(Night& n, Patron& c, Vector2 g) { c.goal = g; c.path = n.NavPath(c.pos, g); c.nextGoalT = 1e9f; c.seatKind = "stand"; c.sitting = false; }

void Night::StartEvent(int i) {
    EventRun& r = events[i]; const EventDef& e = Def(r);
    r.started = true; r.endH = Hour() + e.N("minutes", ED().minutes) / 60.0f;
    Say(e.arrive);
    for (auto& p : players) Note(p, 0, e.name + " at " + Clock() + ".");
    const std::string& k = e.key;
    int n = (int)e.roles.size();
    if (k == "rivals") n = 5 + (int)(Rand() * 3);
    for (int j = 0; j < n && j < (int)e.roles.size(); j++) {
        Vector2 at = k == "robbery" ? Spot("kitchen", j) : Spot("door", j);
        int id = AddEventPatron(i, e.roles[j], at, e.top);
        Patron& c = patrons[id];
        if (k == "bachelor") Goal(*this, c, j < 2 ? Spot("bar", j) : Spot("games", j));
        if (k == "bachelorette") Goal(*this, c, j < 4 ? Spot("dance", j) : Spot("snug", j - 4));
        if (k == "bikers") Goal(*this, c, j < 8 ? Spot("games", j) : Spot("dance", j - 8));
        if (k == "police") Goal(*this, c, Spot("bar", j));
        if (k == "robbery") { Goal(*this, c, j == 0 ? Vector2{17, 8.0f} : Spot("bar", 3 + j)); c.fight.hpMax = c.fight.hp = 150; int kn = (int)props.size(); props.push_back(Prop{}); Prop& kp = props.back(); kp.kind = "knife"; kp.weapon = FD().Weapon("knife"); kp.state = PS_HELD; kp.holder = PatronW(id); c.fight.held = kn; }
        if (k == "cartel") Goal(*this, c, Spot("cards", j));
        if (k == "rivals") Goal(*this, c, Spot("bar", 7 + j));
        if (k == "wake") Goal(*this, c, Spot("bar", 2 + j));
        if (k == "band") { c.pos = Spot("stage", j); Goal(*this, c, Spot("stage", j)); }
    }
    if (k == "bachelor") { Prop kitty; kitty.kind = "kitty"; kitty.pos = {14.6f, 0.82f, 3.6f}; props.push_back(kitty); for (auto& p : players) if (p.st == State::Active) { p.drunk = std::min(99.0f, p.drunk + 20); p.drinks++; Note(p, 1, "A free round from the bachelor party."); } }
    if (k == "bikers") for (int b = 0; b < 6; b++) { Prop bike; bike.kind = "bike"; bike.pos = {24.0f + b * 1.6f, 0, 44.5f}; bike.yaw = 0.3f; props.push_back(bike); }
    if (k == "wake") { Prop coffin; coffin.kind = "coffin"; coffin.pos = {17, 1.15f, 9.5f}; props.push_back(coffin); for (auto& c : patrons) if (c.inside && !c.gone && c.ev < 0) c.mood = std::min(c.mood, 45.0f); }
    if (k == "police") {
        policeT = -1; policeInT = e.N("minutes", 25) * SECONDS_PER_GAME_MINUTE; for (auto& p : players) p.checked = false; SendHome(ED().sendHome);
        // they come for the brawl: anyone who was fighting in the last ten minutes (unless hiding in the toilets, or bribed)
        for (auto& p : players) if ((p.st == State::Active || p.st == State::Down || p.st == State::Drinking) && t - p.lastFightT < 10 * SECONDS_PER_GAME_MINUTE && RoomAt(p.pos) != std::string("The toilets") && !p.bribed) {
            Note(p, 9, "The police came for the brawl, and the bartender pointed."); Leave(p, E_ARRESTED, "a cell at the harbour station"); }
    }
    if (k == "robbery") { r.a = 0; r.stage = 0; }
    if (k == "lockin") { lockIn = true; endMinutes = (e.N("extend_to", 28) - 19) * 60; for (auto& p : players) if (p.st == State::Active) p.honestT = (endMinutes - Minutes()) * SECONDS_PER_GAME_MINUTE; for (auto& x : events) if (!x.started && &x != &r) x.done = true; }
    if (k == "goat") { goatOn = true; goatPos = D().bar.spawn; goatVel = {0, 0}; }
    if (k == "cartel") {   // who owes: Harrow, Bartholomew, anyone who borrowed, anyone the informant named
        for (auto& p : players) if (p.debt > 0 || (p.id < (int)patrons.size() && Rand() < 0.15f)) p.cartelDue = (int)std::max(e.N("collect", 150), p.debt * 1.5f);
    }
}
void Night::EndEvent(int i, const std::string& outcome) {
    EventRun& r = events[i]; if (r.done) return;
    r.done = true; r.outcome = outcome;
    const EventDef& e = Def(r);
    for (int id : r.people) { if (id < 0 || id >= (int)patrons.size()) continue; Patron& c = patrons[id]; if (c.gone || !c.inside) continue; c.leaving = true; c.fight.brawl = -1; Goal(*this, c, D().bar.nav.empty() ? D().bar.door : D().bar.nav[0]); c.seatKind = "leave"; }
    if (!outcome.empty()) Say(outcome);
    // surviving an event with your money and your organs (doc p. 4)
    if (e.key == "police" || e.key == "robbery" || e.key == "cartel")
        for (auto& p : players) if (p.st != State::Gone && p.st != State::PassedOut && p.money > 0 && p.kidneys >= 2) { p.eventsSurvived++; Note(p, 0, "Came through " + e.name + " with money and organs."); }
    if (e.key == "police") { policeInT = 0; for (auto& p : players) p.bribed = false; }
    if (e.key == "bachelor") {   // the best man's reward for keeping the groom out of trouble
        bool trouble = false; for (int id : r.people) if (id >= 0 && id < (int)patrons.size() && (patrons[id].fight.knockouts > 0 || patrons[id].gone) && patrons[id].role == "the groom") trouble = true;
        for (auto& p : players) if (p.promised && !trouble && p.st != State::Gone) { p.money += e.N("best_man_reward", 60); Note(p, 5, "Kept the groom out of trouble; the best man paid up."); }
        for (auto& p : players) p.adopted = false;
        for (auto& pr : props) if (pr.kind == "kitty" && pr.state == PS_OK) pr.state = PS_GONE;
    }
    if (e.key == "bikers") for (auto& pr : props) if (pr.kind == "bike") pr.state = PS_GONE;
    if (e.key == "wake") for (auto& pr : props) if (pr.kind == "coffin") pr.state = PS_GONE;
    if (e.key == "goat") goatOn = false;
}
void Night::SendHome(float share) {
    std::vector<int> c; for (const auto& q : patrons) if (q.inside && !q.gone && !q.leaving && q.ev < 0 && q.type != T_STAFF && q.talkingTo < 0 && q.playing < 0 && q.fight.brawl < 0) c.push_back(q.id);
    int n = (int)(c.size() * share);
    for (int k = 0; k < n && !c.empty(); k++) {
        int j = (int)(Rand() * c.size()) % (int)c.size(); Patron& q = patrons[c[j]]; c.erase(c.begin() + j);
        q.leaving = true; q.backAt = Hour() + ED().homeMinutes / 60.0f; q.seatKind = "leave"; q.goal = D().bar.nav[0]; q.path = NavPath(q.pos, q.goal); q.nextGoalT = 1e9f;
        if (q.seat >= 0) q.seat = -1; q.sitting = false;
    }
}

// ---------------------------------------------------------------- the step
void Night::StepEvents(float dt) {
    float h = Hour();
    // rain: the yard clears, the smokers come in, the golf balls float
    if (!raining && h >= rainH) { raining = true; Say("Rain on the windows. The yard empties; the smokers crowd in."); for (auto& c : patrons) if (c.inside && !c.gone && c.seatKind == "yard") c.nextGoalT = 0; }
    for (auto& p : players) if (raining && p.game.kind == GK_GOLF) p.game.golf.rain = true;
    // last call empties the games room first (doc p. 23)
    if (h >= D().lastCallHour && h < D().lastCallHour + 0.02f) for (auto& c : patrons) if (c.inside && !c.gone && c.ev < 0 && RoomAt(c.pos) == std::string("The games room")) c.leaveH = std::min(c.leaveH, h + 0.1f + Rand() * 0.2f);
    // the schedule (the lock-in stops the rest; nothing new after the night's end)
    for (int i = 0; i < (int)events.size(); i++) {
        EventRun& r = events[i];
        if (!r.started && !r.done && h >= r.startH && (!lockIn || Def(r).key == "lockin")) StartEvent(i);
        if (r.started && !r.done && h >= r.endH && Def(r).key != "lockin") EndEvent(i, Def(r).name + " leaves.");
    }
    // each event's own business
    for (int i = 0; i < (int)events.size(); i++) {
        EventRun& r = events[i]; if (!r.started || r.done) continue;
        const EventDef& e = Def(r); const std::string& k = e.key;
        r.a += dt;
        auto person = [&](const char* role) -> Patron* { for (int id : r.people) if (id >= 0 && id < (int)patrons.size() && patrons[id].role == role && patrons[id].inside && !patrons[id].gone) return &patrons[id]; return nullptr; };
        if (!e.lines.empty() && Rand() < dt * 0.02f) Say(e.lines[(int)(Rand() * e.lines.size()) % e.lines.size()]);
        if (k == "bachelor") {
            // a round every game hour; the groom flirts with Little Ruth's wife, and Little Ruth notices
            if (r.a > 60 * SECONDS_PER_GAME_MINUTE * (r.stage + 1)) { r.stage++; for (auto& p : players) if (p.st == State::Active) { p.drunk = std::min(99.0f, p.drunk + 20); p.drinks++; } Say("The best man: \"ANOTHER ROUND!\""); }
            if (r.b == 0 && r.a > 30 * SECONDS_PER_GAME_MINUTE) {
                r.b = 1; Patron* g = person("the groom"); int lr = -1; for (auto& c : patrons) if (c.name == "Little Ruth" && c.inside && !c.gone) lr = c.id;
                if (g && lr >= 0 && Rand() < 0.5f) { Say("The groom is flirting with Big Ruth. Little Ruth puts down her drink."); StartBrawl(PatronW(lr), PatronW(g->id), PatronW(lr)); }
            }
        }
        if (k == "police") {
            // the inspector walks the room; the bartender points at whoever he dislikes; the wrecked, the armed and anyone under the stairs go in the van
            Patron* ins = person("the inspector");
            if (ins) {
                static const char* ROOMS[6] = {"bar", "games", "dance", "snug", "cards", "bar"};
                int leg = (int)(r.a / (4 * SECONDS_PER_GAME_MINUTE)) % 6;
                if ((int)r.b != leg + 1) { r.b = (float)(leg + 1); Goal(*this, *ins, Spot(ROOMS[leg], 2)); }
                bool ash = false; for (const auto& c : patrons) if (c.name == "Sister Ash" && c.inside && !c.gone) ash = true;
                for (auto& p : players) {
                    if (p.checked || (p.st != State::Active && p.st != State::Drinking) || Vector2Distance(p.pos, ins->pos) > 3.5f) continue;
                    p.checked = true;
                    std::string room = RoomAt(p.pos);
                    if (room == "The toilets" || p.bribed) { Note(p, 0, p.bribed ? "The constable looked the other way (50 well spent)." : "Hid in the toilets while the inspector went by."); continue; }
                    bool armed = p.fight.held >= 0 && p.fight.held < (int)props.size() && props[p.fight.held].weapon >= 0 && FD().weapons[props[p.fight.held].weapon].armed;
                    bool wrecked = p.drunk >= 80, stairs = Vector2Distance(p.pos, {36.5f, 25.5f}) < 2.5f;
                    float worst = -1; int pointed = -1; for (const auto& q : players) if (q.st != State::Gone && q.damageCaused + q.tab * 0.1f > worst) { worst = q.damageCaused + q.tab * 0.1f; pointed = q.id; }
                    bool pointedAt = bar.mood < 30 && pointed == p.id;
                    bool good = p.fight.knockouts == 0 && p.damageCaused <= 0 && p.fightsWon == 0;
                    if (armed || ((wrecked || stairs || pointedAt) && !(ash && good))) {
                        Note(p, 9, armed && props[p.fight.held].kind == "shotgun" ? "Caught by the inspector holding the bartender's shotgun." : wrecked ? "The inspector found you wrecked." : pointedAt ? "The bartender pointed you out to the inspector." : "Found under the stairs by the inspector.");
                        if (armed && props[p.fight.held].kind == "shotgun") Flag("shotgun_caught", p.name);
                        Leave(p, E_ARRESTED, "a cell at the harbour station");
                    } else if (ash && good && (wrecked || stairs || pointedAt)) Note(p, 5, "Sister Ash vouched for you to the inspector.");
                }
            }
            if (policeInT <= 0) EndEvent(i, "The police leave. The room breathes out.");
        }
        if (k == "robbery") {
            // the room is held; robbers go round the players for 50 each; the bartender reaches for his shotgun
            bool gunUnder = false; int gunHolder = -1;
            for (const auto& pr : props) if (pr.kind == "shotgun") { if (pr.state == PS_OK) gunUnder = true; if (pr.state == PS_HELD && pr.holder.kind == 0) gunHolder = pr.holder.idx; }
            if (gunHolder >= 0) {   // a player with the shotgun ends it with a shout
                Player& p = players[gunHolder];
                bool near = false; for (int id : r.people) if (id < (int)patrons.size() && patrons[id].inside && !patrons[id].gone && Vector2Distance(patrons[id].pos, p.pos) < 7) near = true;
                if (near) { Note(p, 5, "Ended the robbery with the bartender's shotgun and one shout."); Flag("robbery_foiled", p.name); EndEvent(i, p.name + " levels the shotgun: \"OUT.\" The robbers run for the kitchen."); continue; }
            }
            if (gunUnder && r.a > 3 * SECONDS_PER_GAME_MINUTE && r.stage == 0) { r.stage = 1; Say("The bartender comes up from under the till with the shotgun. The robbers decide against it."); EndEvent(i, "The robbers bolt through the kitchen, empty-handed."); continue; }
            for (int j = 0; j < (int)r.people.size(); j++) {
                int id = r.people[j]; if (id >= (int)patrons.size()) continue; Patron& c = patrons[id]; if (!c.inside || c.gone || c.fight.brawl >= 0) continue;
                // each robber picks a player who hasn't paid and walks up to them
                Player* tgt = nullptr; float bd = 1e9f;
                for (auto& p : players) if (p.st == State::Active && !p.gaveRobbers && !p.helpingRobbers && !p.watchingSafe) { float d = Vector2Distance(p.pos, c.pos); if (d < bd) { bd = d; tgt = &p; } }
                if (!tgt) continue;
                if (bd > 1.4f) { if (Vector2Distance(c.goal, tgt->pos) > 1) Goal(*this, c, tgt->pos); }
                else if ((c.fight.aiT -= dt) <= 0) { c.fight.aiT = 20; if (r.target != tgt->id) { r.target = tgt->id; Say(std::string("A robber, to ") + tgt->name + ": \"Fifty. Now.\""); } else { Strike(PatronW(id), PlayerW(tgt->id), 20, c.fight.held >= 0 ? props[c.fight.held].weapon : -1, false); float took = std::min(tgt->money, e.N("give", 50)); tgt->money -= took; tgt->gaveRobbers = true; Note(*tgt, 4, TextFormat("The robbers took %.0f the hard way.", took)); } }
            }
            // the safe goes at the end (unless someone's opened it first)
            if (r.a > e.N("minutes", 12) * SECONDS_PER_GAME_MINUTE * 0.8f && r.stage == 0) {
                r.stage = 2;
                float cut = safeOpened ? 0 : e.N("safe", 500) * e.N("cut", 0.33f);
                safeOpened = true;
                for (auto& p : players) if (p.helpingRobbers && p.st != State::Gone) { p.money += cut; p.barred = true; bar.mood = 0; Note(p, 5, TextFormat("Helped the robbers for a cut of %.0f. The bar won't forget.", cut)); }
                EndEvent(i, "The robbers go out through the kitchen with the till and the safe.");
            }
        }
        if (k == "cartel") {
            // the large men collect: from Harrow and Bartholomew (they pay), and from any player who owes
            for (auto& c : patrons) if (c.inside && !c.gone && (c.name == "Harrow" || c.name == "Bartholomew Crane") && r.b < 2 && r.a > (r.b + 1) * 2 * SECONDS_PER_GAME_MINUTE) { r.b += 1; Say("The large men have a word with " + c.name + ", who pays."); c.mood = 10; }
            for (int j = 1; j < (int)r.people.size(); j++) {
                int id = r.people[j]; if (id >= (int)patrons.size()) continue; Patron& c = patrons[id]; if (!c.inside || c.gone) continue;
                Player* due = nullptr; for (auto& p : players) if (p.cartelDue > 0 && p.st == State::Active) { due = &p; break; }
                if (!due) { if (Vector2Distance(c.goal, Spot("cards", j)) > 1) Goal(*this, c, Spot("cards", j)); continue; }
                if (Vector2Distance(c.pos, due->pos) > 1.4f) { if (Vector2Distance(c.goal, due->pos) > 1) Goal(*this, c, due->pos); }
                else if ((c.fight.aiT -= dt) <= 0) {
                    if (c.fight.aiT > -100 && r.target != due->id) { r.target = due->id; c.fight.aiT = 20; Say(TextFormat("A large man, to %s: \"The quiet man says you owe %d.\"", due->name.c_str(), due->cartelDue)); }
                    else {   // not paying is the alley and a missing kidney (a dog at your side, and they think again)
                        c.fight.aiT = 1e9f;
                        if (dog.owner == due->id) { Say("The dog growls. The large men decide it can wait."); due->cartelDue = 0; }
                        else { due->kidneys = std::max(0, due->kidneys - 1); due->pos = {-4, 22}; due->cartelDue = 0; due->debt = 0; Note(*due, 5, "Didn't pay the cartel: woke in the alley minus a kidney."); Flag("kidney", due->name); Say(due->name + " is walked out to the alley by two large men."); }
                    }
                }
            }
        }
        if (k == "rivals") {
            // Cutter Jones starts the midnight brawl (unless a round made it a party)
            if (!partied && h >= 24 && r.stage == 0) {
                r.stage = 1;
                Player* tgt = nullptr; for (auto& p : players) if (p.st == State::Active) { tgt = &p; break; }
                int cj = -1; for (auto& c : patrons) if (c.name == "Cutter Jones" && c.inside && !c.gone) cj = c.id;
                int lead = cj >= 0 ? cj : (r.people.empty() ? -1 : r.people[0]);
                if (tgt && lead >= 0 && opts.mode != MD_SOBER) {
                    Say("Midnight. " + patrons[lead].name + " stands on a chair: \"NAUTILUS? NAUTILUS CAN'T HOLD ITS GROG!\"");
                    int b = StartBrawl(PatronW(lead), PlayerW(tgt->id), PatronW(lead));
                    if (b >= 0) for (int id : r.people) if (id < (int)patrons.size() && patrons[id].inside && !patrons[id].gone && patrons[id].fight.brawl < 0) { Patron& c = patrons[id]; c.fight.brawl = b; c.fight.side = patrons[lead].fight.side; c.fight.foe = PlayerW(tgt->id); brawls[b].size++; }
                }
            }
        }
        if (k == "wake") {
            // rounds are expected: an hour in and you haven't bought one, the bartender notices
            if (r.stage == 0 && r.a > 30 * SECONDS_PER_GAME_MINUTE) { r.stage = 1; for (auto& p : players) if (p.st == State::Active && p.roundT < t - r.a) { bar.mood = std::max(0.0f, bar.mood - 10); Note(p, 0, "Didn't buy a round at the wake. The bartender noticed."); } }
            for (auto& c : patrons) if (c.inside && !c.gone && c.ev < 0) c.mood = std::min(c.mood, 50.0f);
        }
        if (k == "band") { int j = 0; for (int id : r.people) if (id < (int)patrons.size() && patrons[id].inside && !patrons[id].gone && !patrons[id].leaving) { Patron& c = patrons[id]; c.pos = Spot("stage", j++); c.vel = {0, 0}; c.yaw = -PI / 2; } }   // (on the stage, facing the floor)
        if (k == "bikers") {
            // the leader sings (nobody may laugh); otherwise they're a book club with the pool tables
            if (r.stage == 0 && r.a > 20 * SECONDS_PER_GAME_MINUTE) { r.stage = 1; r.b = t; Say("The biker leader climbs onto the stage and sings. Nobody laughs. Nobody."); }
        }
        if (k == "goat") {   // it wanders; it eats the scratch-offs
            goatPh += dt;
            if (Rand() < dt * 0.3f || Vector2Length(goatVel) < 0.05f) { float a = Rand() * 2 * PI; goatVel = {cosf(a) * 0.8f, sinf(a) * 0.8f}; }
            Vector2 tgt = scratchEaten ? D().bar.serve : D().bar.scratch;
            goatVel = Vector2Lerp(goatVel, Vector2Scale(Vector2Normalize(Vector2Subtract(tgt, goatPos)), 0.8f), dt * 0.3f);
            goatPos = Vector2Add(goatPos, Vector2Scale(goatVel, dt)); Collide(goatPos, 0.3f); goatYaw = atan2f(goatVel.y, goatVel.x);
            if (!scratchEaten && Vector2Distance(goatPos, D().bar.scratch) < 1.2f) { scratchEaten = true; Say("The goat eats every scratch-off in the dispenser."); Flag("goat", ""); }
        }
    }
    // the bachelorette's dares: done when you've done them
    if (EventOn("bachelorette")) {
        const auto& dares = ED().ev[DefIndex("bachelorette")].dares;
        for (auto& p : players) {
            if (p.dare < 0 || p.dare >= (int)dares.size() || p.st != State::Active) continue;
            const std::string& d = dares[p.dare]; bool done = false;
            if (d.find("hat") != std::string::npos) done = p.pos.y > 9.8f && Vector2Distance(p.pos, bar.pos) < 1.3f;
            if (d.find("Reverend") != std::string::npos) done = p.flirt.patron >= 0 && p.flirt.lastWin && patrons[p.flirt.patron].name == "The Reverend";
            if (d.find("scratch") != std::string::npos) done = (p.game.kind == GK_SCRATCH || p.game.kind == GK_PIP) && p.game.paid && p.game.ticket.prize > 0;
            if (d.find("shotgun") != std::string::npos) done = p.fight.held >= 0 && p.fight.held < (int)props.size() && props[p.fight.held].kind == "shotgun";
            if (done) {
                if (d.find("hat") != std::string::npos) bar.mood = std::max(0.0f, bar.mood - 10);
                p.money += ED().ev[DefIndex("bachelorette")].N("tip", 50); p.charBuff = std::max(p.charBuff, 0.1f); p.charBuffT = std::max(p.charBuffT, 5 * SECONDS_PER_GAME_MINUTE);
                p.daresDone++; Note(p, 5, "Did the bride's dare: " + d + "."); Say("The bachelorette party SCREAMS. The bride tips " + p.name + " fifty."); p.dare = -1;
            }
        }
    }
}

// ---------------------------------------------------------------- what a player can do about it
std::vector<Night::EvOption> Night::EventOptions(const Player& p) const {
    std::vector<EvOption> o;
    if (p.st != State::Active) return o;
    auto near = [&](const char* role, float r) -> int { for (const auto& c : patrons) if (c.ev >= 0 && c.inside && !c.gone && c.role == role && Vector2Distance(c.pos, p.pos) < r) return c.id; return -1; };
    if (EventOn("bachelor")) {
        if (near("the best man", 1.8f) >= 0 && !p.promised) o.push_back({9, -1, "Promise the best man you'll keep the groom out of trouble"});
        for (const auto& pr : props) if (pr.kind == "kitty" && pr.state == PS_OK && Vector2Distance({pr.pos.x, pr.pos.z}, p.pos) < 1.4f) o.push_back({16, -1, "Pocket the bachelor party's kitty (300)"});
    }
    if (EventOn("bachelorette") && near("the bride", 1.8f) >= 0 && p.dare < 0) o.push_back({1, -1, "Ask the bride for a dare"});
    if (EventOn("bikers") && near("the leader", 1.8f) >= 0) o.push_back({2, -1, "Ask about the book"});
    if (EventOn("police") && (near("a constable", 1.8f) >= 0 || near("the inspector", 1.8f) >= 0) && !p.bribed && p.money >= 50) o.push_back({8, -1, "Bribe the constable (50)"});
    if (EventOn("robbery")) {
        bool r = false; for (const auto& c : patrons) if (c.role == "a robber" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 1.8f) r = true;
        if (r && !p.gaveRobbers) o.push_back({6, -1, "Hand over 50 and keep the night"});
        if (r && !p.helpingRobbers) o.push_back({7, -1, "Help them for a cut (a feud with the bar)"});
    }
    if (EventOn("cartel") && p.cartelDue > 0) {
        bool m = false; for (const auto& c : patrons) if (c.role == "a large man" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 2) m = true;
        if (m && p.money >= p.cartelDue) o.push_back({4, -1, TextFormat("Pay the cartel (%d)", p.cartelDue)});
        if (m) for (const auto& q : players) if (q.id != p.id && q.st == State::Active) { o.push_back({5, q.id, "Point them at " + q.name}); break; }
        if (m) for (const auto& c : patrons) if (c.name == "Harrow" && c.inside && !c.gone) { o.push_back({5, 100 + c.id, "Point them at Harrow"}); break; }
    }
    for (const auto& c : patrons) if (c.name == "Harrow" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 1.8f && p.money < 20 && p.debt <= 0) o.push_back({3, -1, "Borrow 100 from Harrow"});
    // the cartel's wares: the list (the scene opens it; the purchases are acts 61-78)
    if (WaresHere(p)) o.insert(o.begin(), {60, -1, "See what the quiet man's men are selling"});
    // the ferry across the harbour (doc p. 34): on the hour, 10, and the tab comes with you
    if (FerryRunning() && Vector2Distance(p.pos, D().bar.door) < 3.0f && p.money >= 10 && p.fight.brawl < 0 && Hour() < 26.5f) o.push_back({52, -1, CurBar() == BAR_MONKEY ? "Take the ferry back to the Gull (10)" : "Take the ferry to the Brass Monkey (10)"});
    // the Monkey's rope: a bribe for Horace
    if (RopeStops(p) && p.pos.y < 0.8f && p.pos.x > 15 && p.pos.x < 24 && p.money >= 30) o.insert(o.begin(), {51, -1, "Slip Horace 30 to be let in"});
    // the bouncer: last night's unpaid tab, paid double
    if (p.owedAtDoor > 0 && p.money >= p.owedAtDoor && Vector2Distance(p.pos, D().bar.spawn) < 3.5f) o.push_back({50, -1, TextFormat("Pay the bouncer double for last night (%.0f)", p.owedAtDoor)});
    // the cartel's hand at the poker table; side bets on the other sailors' matches
    if (EventOn("cartel") && p.game.kind < 0 && cartelHand.seats.empty() && Vector2Distance(p.pos, {34.5f, 14.5f}) < 3.2f) o.push_back({40, -1, p.money >= 100 ? "Sit in the quiet man's hand (100, a kidney in the pot)" : "Sit in the quiet man's hand (stake a kidney)"});
    for (const auto& q : players) {
        if (q.id == p.id || q.game.kind < 0 || q.game.over || q.game.opp < 0 || (q.game.kind != GK_DARTS && q.game.kind != GK_POOL && q.game.kind != GK_GOLF) || Vector2Distance(q.pos, p.pos) > 5) continue;
        bool already = false; for (const auto& b : sideBets) already |= b.bettor == p.id && b.on == q.id;
        if (!already) { o.push_back({30, q.id, "Side bet 10 on " + q.name}); o.push_back({31, q.id, "Side bet 10 against " + q.name}); }
        break;
    }
    // the piano, the bikes, the band, the safe, the toilet window
    for (const auto& b : D().bar.boxes) if (b.kind == "piano" && Vector2Distance(p.pos, {b.r.x + b.r.width / 2, b.r.y + b.r.height / 2}) < 2.9f) o.push_back({11, -1, "Play the piano"});   // (it stands on the stage: reach up to it)
    if (EventOn("bikers")) for (const auto& pr : props) if (pr.kind == "bike" && pr.state == PS_OK && Vector2Distance({pr.pos.x, pr.pos.z}, p.pos) < 1.4f) { o.push_back({17, -1, "Sit on a biker's bike"}); break; }
    if (EventOn("band") && RoomAt(p.pos) == std::string("The dance floor")) { o.push_back({12, -1, "Dance (hit the beats)"}); if (p.money >= 10) o.push_back({10, -1, "Request a song (10)"}); }
    bool atStairs = Vector2Distance(p.pos, {36.5f, 25.5f}) < 2.5f;
    bool knowsSafe = std::find(p.items.begin(), p.items.end(), "a map to the safe") != p.items.end() || std::find(p.known.begin(), p.known.end(), "Old Marlow") != p.known.end();
    if (atStairs && knowsSafe && !safeOpened) o.push_back({13, -1, "Go up and open the safe"});
    if (atStairs && EventOn("robbery") && !p.watchingSafe) o.push_back({18, -1, "Hide upstairs and watch the safe"});
    if (lockIn && RoomAt(p.pos) == std::string("The toilets")) o.push_back({14, -1, "Out through the toilet window"});
    return o;
}
void Night::EventAction(Player& p, int act, int arg) {
    if (p.st != State::Active) return;
    if (act > 60 && act < 80) {   // the cartel's wares (nightoff_wares.cpp checks everything itself): 61-68 buy, 71-78 slip one into arg's drink
        std::string why = BuyWare(p, (act - 61) % 10, act >= 71 ? arg : -1);
        if (!why.empty()) { p.toast = why; p.toastT = 4; }
        return;
    }
    auto valid = [&]() { for (const auto& o : EventOptions(p)) if (o.act == act && (o.arg == arg || arg < 0)) return true; return false; };
    if (!valid()) return;
    int bi = EventIndex("bachelorette"), ki = EventIndex("bikers");
    switch (act) {
        case 1: { const auto& d = Def(events[bi]).dares; p.dare = d.empty() ? -1 : (int)(Rand() * d.size()) % (int)d.size(); Say(std::string("The bride, to ") + p.name + ": \"Your dare: " + (p.dare >= 0 ? d[p.dare] : std::string("dance")) + "!\""); } break;
        case 2: { const auto& L = Def(events[ki]).bookLines; Say("The biker leader: " + (L.empty() ? std::string("\"Chapter nine.\"") : L[(int)(Rand() * L.size()) % L.size()])); p.charBuff = std::max(p.charBuff, 0.1f); p.charBuffT = std::max(p.charBuffT, 10 * SECONDS_PER_GAME_MINUTE); p.drunk = std::min(99.0f, p.drunk + 12); p.drinks++; Note(p, 5, "Discussed " + Def(events[ki]).book + " with a biker gang. They bought the drink."); } break;
        case 3: p.money += 100; p.debt += 100; Say("Harrow counts out 100. \"From a friend of mine. He'll want it back.\""); Note(p, 0, "Borrowed 100 from Harrow (from the cartel)."); break;
        case 4: p.money -= p.cartelDue; Note(p, 0, TextFormat("Paid the cartel %d.", p.cartelDue)); p.cartelDue = 0; p.debt = 0; break;
        case 5: {
            int owed = p.cartelDue; p.cartelDue = 0;
            if (arg >= 100) { int c = arg - 100; if (c < (int)patrons.size()) { Say("The large men turn to " + patrons[c].name + ". " + patrons[c].name + " pays, white as a sheet."); patrons[c].mood = 0; } Note(p, 5, "Pointed the cartel at Harrow."); }
            else if (arg >= 0 && arg < (int)players.size()) { players[arg].cartelDue = owed; Note(p, 5, "Pointed the cartel at " + players[arg].name + "."); Say(p.name + " points at " + players[arg].name + ". The large men turn."); }
        } break;
        case 6: { float took = std::min(p.money, 50.0f); p.money -= took; p.gaveRobbers = true; Note(p, 0, TextFormat("Handed the robbers %.0f.", took)); } break;
        case 7: p.helpingRobbers = true; Say(p.name + " holds the kitchen door for the robbers."); break;
        case 8: p.money -= 50; p.bribed = true; Say("A constable pockets something and studies the ceiling."); break;
        case 9: p.promised = true; Say("The best man, to " + p.name + ": \"Keep him out of trouble and there's sixty in it.\""); { int bc = EventIndex("bachelor"); if (bc >= 0 && Rand() < 0.4f) { p.adopted = true; Say("The groom throws an arm round " + p.name + ": \"MY NEW BEST FRIEND! You're not going ANYWHERE.\""); } (void)bc; } break;
        case 10: p.money -= 10; for (auto& c : patrons) if (c.inside && !c.gone && RoomAt(c.pos) == std::string("The dance floor")) c.mood = std::min(100.0f, c.mood + 5); Say("The band plays " + p.name + "'s request. The dance floor swings."); break;
        case 11: {
            if (EventOn("bikers")) {   // touching the piano is a twelve-on-six brawl
                Say("A biker, very quietly: \"That's our piano.\"");
                int ldr = -1; for (int id : events[ki].people) if (id < (int)patrons.size() && patrons[id].inside && !patrons[id].gone) { ldr = id; break; }
                if (ldr >= 0) { int b = StartBrawl(PatronW(ldr), PlayerW(p.id), PatronW(ldr)); if (b >= 0) for (int id : events[ki].people) if (id < (int)patrons.size() && patrons[id].inside && !patrons[id].gone && patrons[id].fight.brawl < 0) { Patron& c = patrons[id]; c.fight.brawl = b; c.fight.side = patrons[ldr].fight.side; c.fight.foe = PlayerW(p.id); brawls[b].size++; }
                    for (auto& q : players) if (q.st == State::Active && q.fight.brawl < 0 && b >= 0) { q.fight.brawl = b; q.fight.side = p.fight.side; brawls[b].size++; }
                    if (policeT < 0 && !EventOn("police") && !lockIn) { policeT = 2 * SECONDS_PER_GAME_MINUTE * 4; Say("Someone runs for the police."); } }
                Flag("piano", p.name);
            } else { for (auto& c : patrons) if (c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 6) c.mood = std::min(100.0f, c.mood + 3); Say(p.name + " plays a sea shanty on the piano, mostly the right notes."); }
        } break;
        case 12: { std::string why; StartGame(p, GK_DANCE, 0, -1, 0, &why); } break;
        case 13: { safeOpened = true; int n = 0; for (const auto& q : players) n += q.st == State::Active && Vector2Distance(q.pos, p.pos) < 4; float share = 500.0f / std::max(1, n); for (auto& q : players) if (q.st == State::Active && Vector2Distance(q.pos, p.pos) < 4) { q.money += share; Note(q, 5, TextFormat("Opened the upstairs safe: %.0f.", share)); } Flag("safe", p.name); Say("Upstairs, a safe swings open."); } break;
        case 14: Note(p, 5, "Went out through the toilet window at the lock-in."); Leave(p, E_WALKED, "home, by way of the toilet window"); break;
        case 16: { for (auto& pr : props) if (pr.kind == "kitty" && pr.state == PS_OK) pr.state = PS_GONE; p.money += 300; Note(p, 5, "Pocketed the bachelor party's kitty."); Flag("kitty", p.name);
                   int bc = EventIndex("bachelor"); bool seen = false; if (bc >= 0) for (int id : events[bc].people) if (id < (int)patrons.size() && patrons[id].inside && Vector2Distance(patrons[id].pos, p.pos) < 6) seen = true;
                   if (seen && bc >= 0) { Say("\"OI! THE KITTY!\""); int bm = events[bc].people.size() > 1 ? events[bc].people[1] : events[bc].people[0]; int b = StartBrawl(PatronW(bm), PlayerW(p.id), PatronW(bm)); if (b >= 0) for (int id : events[bc].people) if (id < (int)patrons.size() && patrons[id].inside && patrons[id].fight.brawl < 0) { patrons[id].fight.brawl = b; patrons[id].fight.side = patrons[bm].fight.side; patrons[id].fight.foe = PlayerW(p.id); brawls[b].size++; } } } break;
        case 17: EventAction(p, 11, -1); break;   // (the bikes are as sacred as the piano)
        case 30: case 31: { SideBet b; b.bettor = p.id; b.on = arg; b.stake = 10; b.forWin = act == 30; sideBets.push_back(b); Note(p, 0, std::string("Side bet of 10 ") + (act == 30 ? "on " : "against ") + players[std::clamp(arg, 0, (int)players.size() - 1)].name + "."); } break;
        case 40: { std::string why; StartGame(p, GK_POKER, 1, -1, 0, &why); if (!why.empty()) Say(why); } break;
        case 52: p.money -= 10; p.ferried = true; Note(p, 0, std::string("Took the ferry to ") + BarName(1 - CurBar()) + " at " + Clock() + "."); Leave(p, E_WALKED, std::string("across the harbour, at ") + BarName(1 - CurBar())); break;
        case 51: p.money -= 30; p.letIn = true; Say("Horace pockets it without looking down, and unhooks the rope."); Note(p, 0, "Bribed the Monkey's doorman."); break;
        case 50: p.money -= p.owedAtDoor; Note(p, 0, TextFormat("Paid the bouncer %.0f for last night.", p.owedAtDoor)); p.owedAtDoor = 0; p.barred = false; Say("The bouncer counts it twice and stands aside."); break;
        case 18: p.watchingSafe = true; Note(p, 0, "Hid upstairs during the robbery and watched the safe."); break;
    }
}

// ---------------------------------------------------------------- --night-test's stage 7 checks (the gate: a packed night with the bikers and the police)
int NightEventChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    // the schedule: two or three a night (more when packed)
    { double tot = 0; int nights = 200; for (int s = 0; s < nights; s++) { Night n; Opts o; o.seed = 500 + s; o.crowd = 1; n.Init(o); tot += n.events.size(); }
      check(tot / nights > 2.0f && tot / nights < 3.2f, TextFormat("a normal night schedules %.1f events (the doc: 2.4 a night)", tot / nights)); }
    // the gate: a packed night, the bikers in, a sailor touches the piano, twelve on six, and the police come for the brawl
    Night n; Opts o; o.seed = 4242; o.crowd = 2; o.players = 2; n.Init(o);
    n.events.clear(); n.rainH = 99;
    int bk = n.ForceEvent("bikers", 22.0f);
    for (int i = 0; i < (int)(4 * 60 * SECONDS_PER_GAME_MINUTE / 0.1f) && !n.EventOn("bikers"); i++) n.Step(0.1f);
    check(n.EventOn("bikers") && n.events[bk].people.size() == 12, TextFormat("the biker gang arrives at %s: %d of them", n.Clock().c_str(), (int)n.events[bk].people.size()));
    int bikes = 0; for (const auto& p : n.props) bikes += p.kind == "bike" && p.state == PS_OK;
    check(bikes == 6, "their bikes park in the yard");
    for (int i = 0; i < 300; i++) n.Step(0.1f);
    int inGames = 0; for (int id : n.events[bk].people) inGames += RoomAt(n.patrons[id].pos) == std::string("The games room");
    check(inGames >= 5, TextFormat("they take the games room (%d of them at the tables)", inGames));
    int inside0 = 0; for (const auto& c : n.patrons) inside0 += c.inside && !c.gone;
    check(inside0 >= 30, TextFormat("a packed night: %d in the bar", inside0));
    Player& p = n.players[0]; Player& q = n.players[1]; p.st = q.st = State::Active;
    // asking about the book first: a free drink and a better line
    { int ldr = n.events[bk].people[0]; p.pos = Vector2Add(n.patrons[ldr].pos, {0.6f, 0}); float d0 = p.drunk; p.in.evAct = 2; n.Step(0.02f); check(p.drunk > d0 && p.charBuffT > 0, "ask the leader about the book: a free drink and charisma"); }
    // then the piano
    for (const auto& b : D().bar.boxes) if (b.kind == "piano") { p.pos = {b.r.x + b.r.width / 2, b.r.y - 0.6f}; q.pos = {b.r.x + b.r.width / 2 + 1.0f, b.r.y - 0.8f}; }
    p.in.evAct = 11; n.Step(0.02f);
    int br = p.fight.brawl;
    int bikersIn = 0; if (br >= 0) for (int id : n.events[bk].people) bikersIn += n.patrons[id].fight.brawl == br;
    check(br >= 0 && bikersIn >= 10 && q.fight.brawl == br, TextFormat("touching the piano is a brawl: %d bikers against the crew", bikersIn));
    check(n.policeT > 0, "someone runs for the police");
    bool policeCame = false, arrested = false; int kos = 0;
    for (int i = 0; i < 5000 && !(policeCame && n.policeInT <= 0); i++) {
        for (auto& pl : n.players) if (pl.st == State::Active && pl.fight.brawl >= 0 && !pl.fight.Busy() && pl.fight.recT <= 0) { pl.in.attack = MV_JAB; }
        n.Step(0.05f);
        policeCame |= n.EventOn("police");
        for (const auto& pl : n.players) arrested |= pl.ending == E_ARRESTED;
    }
    for (const auto& b : n.brawls) kos += b.kos;
    check(policeCame, "the police come for the brawl");
    check(arrested, TextFormat("and whoever's still fighting goes in the van (%d knockouts first)", kos));
    check(n.Headline().size() > 10, "the morning has a headline: " + n.Headline());
    // the other events resolve: run each alone on a normal night with two bot sailors
    static const char* ALL[11] = {"bachelor", "bachelorette", "bikers", "police", "robbery", "cartel", "rivals", "lockin", "wake", "band", "goat"};
    for (const char* key : ALL) {
        Night m; Opts mo; mo.seed = 77; mo.players = 2; m.Init(mo); m.events.clear();
        int ei = m.ForceEvent(key, 23.0f);
        for (auto& pl : m.players) { pl.bot = true; pl.botStyle = pl.id; pl.botDrinkTo = 40; pl.botLeaveH = 27; }
        m.t = 4 * 60 * SECONDS_PER_GAME_MINUTE - 20;
        for (int i = 0; i < 40000 && !m.over && !(m.events[ei].started && m.events[ei].done); i++) { for (auto& pl : m.players) if (pl.bot) m.BotPlayer(pl, 0.1f); m.Step(0.1f); }
        bool ok = m.events[ei].started && (m.events[ei].done || std::string(key) == "lockin" || m.over);
        check(ok, std::string(key) + " starts and resolves" + (m.events[ei].outcome.empty() ? std::string() : ": " + m.events[ei].outcome));
        if (std::string(key) == "lockin") check(m.endMinutes > NIGHT_MINUTES && m.lockIn, "the lock-in runs the night to four");
        if (std::string(key) == "goat") check(m.scratchEaten || m.goatOn || m.events[ei].done, "the goat");
    }
    // the robbery and the shotgun; the cartel and the alley; the wake stops fights
    { Night m; Opts mo; mo.seed = 3; m.Init(mo); m.events.clear(); m.t = 4 * 60 * SECONDS_PER_GAME_MINUTE; int ri = m.ForceEvent("robbery", 23.0f); m.Step(0.02f);
      Player& u = m.players[0]; u.st = State::Active;
      int g = -1; for (int i = 0; i < (int)m.props.size(); i++) if (m.props[i].kind == "shotgun") g = i;
      u.fight.held = g; m.props[g].state = PS_HELD; m.props[g].holder = PlayerW(0); u.pos = {24, 14};
      for (int i = 0; i < 200 && !m.events[ri].done; i++) m.Step(0.05f);
      check(m.events[ri].done && !m.events[ri].outcome.empty(), "a sailor with the shotgun ends the robbery with a shout: " + m.events[ri].outcome); }
    { Night m; Opts mo; mo.seed = 4; m.Init(mo); m.events.clear(); m.t = 5 * 60 * SECONDS_PER_GAME_MINUTE; Player& u = m.players[0]; u.st = State::Active; u.debt = 100; u.money = 0; u.pos = {36, 15};
      int ci = m.ForceEvent("cartel", 24.0f);
      for (int i = 0; i < 4000 && u.cartelDue >= 0 && u.kidneys == 2; i++) m.Step(0.05f);
      check(m.events[ci].started && u.kidneys == 1, "owe the cartel and don't pay: the alley and a missing kidney"); }
    { Night m; Opts mo; mo.seed = 5; m.Init(mo); m.events.clear(); m.ForceEvent("wake", 19.0f); for (int i = 0; i < 40; i++) m.Step(0.25f);
      Player& u = m.players[0]; u.st = State::Active; int c = -1; for (auto& x : m.patrons) if (x.inside && !x.gone && x.ev < 0) { c = x.id; break; }
      if (c >= 0) { m.patrons[c].pos = Vector2Add(u.pos, {0.8f, 0}); u.yaw = 0; u.in.attack = MV_JAB; m.Step(0.02f); for (int i = 0; i < 30; i++) m.Step(0.02f); }
      check(c >= 0 && m.brawls.empty(), "fights are impossible at a wake"); }
    return fails;
}

} // namespace no
