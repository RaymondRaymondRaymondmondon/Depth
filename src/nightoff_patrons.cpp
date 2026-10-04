// A Night Off's patrons (design doc pp. 8-13, 40-42), stage 2: the crowd curve, the regulars' schedules and the
// generator's extras, haunts and goals, walking the Gull's rooms by its doorways, moods and the hour, a memory of each
// player, and the conversation mini-game (four options rolled against charisma and the patron's traits, drunk captions).
#include "nightoff.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <queue>

namespace no {

static const char* const HAUNT_SEATS[] = {"tables", "snug", "corner", "games", "", "cards", "tables", "", ""};   // (by type; Regular: a stool; Oddball: anywhere; Staff: the kitchen)

int Night::CrowdTarget() const {
    // the crowd curve (doc p. 2): the hour's range, the night's setting (Random changes every hour, and nobody says)
    const auto& H = D().crowdHours;
    float h = Hour(); float lo = 4, hi = 8;
    for (const auto& r : H) if (h >= r[0]) { lo = r[1]; hi = r[2]; }
    int c = opts.crowd;
    if (c == 3) { uint32_t k = (uint32_t)h * 2654435761u ^ opts.seed; c = (int)(k % 3); }
    float mul = D().crowdMul[std::clamp(c, 0, 2)];
    return (int)lroundf((lo + hi) * 0.5f * mul);
}
const char* Night::MoodName(float m) const { return m < 20 ? "Hostile" : m < 40 ? "Annoyed" : m < 60 ? "Neutral" : m < 80 ? "Friendly" : "Delighted"; }
float Night::Noise() const {
    int inside = 0; for (const auto& c : patrons) inside += c.inside && !c.gone;
    return std::clamp(inside / 28.0f, 0.0f, 1.0f);
}
static int NearestNode(const BarData& B, Vector2 p) { int best = 0; float bd = 1e9f; for (int i = 0; i < (int)B.nav.size(); i++) { float d = Vector2Distance(B.nav[i], p); if (d < bd) { bd = d; best = i; } } return best; }
std::vector<int> Night::NavPath(Vector2 from, Vector2 to) const {
    // the doorways as a graph: a breadth-first path between the nodes nearest each end
    const BarData& B = D().bar;
    if (B.nav.empty()) return {};
    int a = NearestNode(B, from), b = NearestNode(B, to);
    std::vector<int> prev(B.nav.size(), -2); std::queue<int> q; q.push(a); prev[a] = -1;
    while (!q.empty()) { int n = q.front(); q.pop(); if (n == b) break; for (int m : B.navLinks[n]) if (prev[m] == -2) { prev[m] = n; q.push(m); } }
    std::vector<int> path; if (prev[b] == -2) return path;
    for (int n = b; n >= 0; n = prev[n]) path.push_back(n);
    std::reverse(path.begin(), path.end());
    return path;
}
int Night::AddPatron(int reg) {
    const Data& d = D();
    Patron c; c.id = (int)patrons.size(); c.reg = reg;
    if (reg >= 0) {
        const PatronDef& r = d.regulars[reg];
        c.name = r.name; c.secret = r.secret; c.tell = r.tell; c.type = r.type; c.thief = r.thief; c.rich = r.rich; c.home = r.home;
        for (int t : r.traits) c.traits |= 1u << t;
        c.arriveH = r.arrive + Rand(-0.15f, 0.25f); c.leaveH = r.leave + Rand(-0.3f, 0.3f); c.look = r.look;
    } else {
        // the generator's extras (doc p. 13): a type, three random traits, one of eight generic secrets (a kidney thief at 3%)
        c.type = (int)(Rand() * (T_COUNT - 1)) % (T_COUNT - 1);   // (no Staff)
        for (int k = 0; k < 3; k++) c.traits |= 1u << ((int)(Rand() * d.traitNames.size()) % d.traitNames.size());
        c.name = d.firstNames[(int)(Rand() * d.firstNames.size()) % d.firstNames.size()] + " " + d.lastNames[(int)(Rand() * d.lastNames.size()) % d.lastNames.size()];
        float r = Rand(); c.thief = r < 0.03f;
        c.secret = c.thief ? "kidney thief" : d.genericSecrets[1 + (int)(Rand() * (d.genericSecrets.size() - 1)) % (d.genericSecrets.size() - 1)];
        // where going home with them ends up: the regulars' odds, the kidney at 3% (a thief) (doc p. 14)
        { float u = Rand() * 95; c.home = c.thief ? "kidney" : u < 42 ? "sincere" : u < 58 ? "sweet" : u < 63 ? "rich" : u < 74 ? "married" : "robbery"; if (c.home == "rich") c.rich = true; }
        c.arriveH = Hour(); c.leaveH = std::min(26.9f, Hour() + Rand(1.0f, 3.5f));
        c.look.model = (int)(Rand() * 5) % 5; c.look.build = Rand(0.8f, 1.25f); c.look.height = Rand(0.85f, 1.08f);
        c.look.top = {(unsigned char)Rand(40, 220), (unsigned char)Rand(40, 200), (unsigned char)Rand(40, 200), 255};
        c.look.hat = {(unsigned char)(c.look.top.r * 0.6f), (unsigned char)(c.look.top.g * 0.6f), (unsigned char)(c.look.top.b * 0.6f), 255};
    }
    c.mood = 45 + Rand() * 20;
    c.mem.assign(players.size(), Memory{});
    c.pos = d.bar.nav.empty() ? Vector2{19.5f, -3} : d.bar.nav[0];
    patrons.push_back(c);
    return c.id;
}
void Night::InitPatrons() {
    patrons.clear();
    const Data& d = D();
    // every regular is on the books for the night; the crowd curve decides how many of the extras come
    for (int i = 0; i < (int)d.regulars.size(); i++) {
        // a dead night thins the regulars too (the staff always come)
        if (opts.crowd == 0 && d.regulars[i].staff.empty() && Rand() < 0.45f) continue;
        if (d.regulars[i].travels && gTravelRolls && !TravellerHere(d.regulars[i].name, opts.bar, opts.seed)) continue;   // (at the other bar tonight)
        AddPatron(i);
    }
    seatTaken.assign(64, -1);
}
static int SeatKey(const std::string& kind, int idx) {
    static const char* K[] = {"stool", "snug", "cards", "tables", "games", "dance", "corner", "yard"};
    for (int k = 0; k < 8; k++) if (kind == K[k]) return k * 8 + idx;
    return -1;
}
static Vector2 SeatPos(const std::string& kind, int idx) {
    const BarData& B = D().bar;
    if (kind == "stool") return idx >= 0 && idx < (int)B.stools.size() ? B.stools[idx] : B.serve;
    const auto* v = B.Seats(kind);
    return v && idx >= 0 && idx < (int)v->size() ? (*v)[idx] : B.serve;
}
static void ChooseGoal(Night& n, Patron& c) {
    // what a patron does next, by type (doc p. 41): a Regular to their stool, a Hustler to the games room, a Gambler to the
    // card room, a Brooder to the emptiest corner, a Flirt toward the snug (or the player with the highest charisma), the
    // rest to a table or the bar; now and then anyone goes for a drink, the toilets or the yard
    const Data& d = D();
    if (c.seat >= 0) { int key = SeatKey(c.seatKind, c.seat); if (key >= 0 && key < (int)n.seatTaken.size() && n.seatTaken[key] == c.id) n.seatTaken[key] = -1; }
    c.seat = -1; c.sitting = false;
    if (c.reg >= 0 && !d.regulars[c.reg].staff.empty()) {   // the staff keep their posts: the cook his kitchen, the Monkey's doorman the door, the croupier the cards
        const std::string& st = d.regulars[c.reg].staff;
        c.seatKind = st; c.goal = st == "door" ? Vector2{17.2f, 1.6f} : st == "cards" ? Vector2{34.5f, 16.75f} : Vector2{26.5f, 15.5f};
        c.path = n.NavPath(c.pos, c.goal); c.nextGoalT = 999; return;
    }
    std::string kind;
    float r = n.Rand();
    if (r < 0.18f) { c.seatKind = "drink"; c.goal = {std::clamp(n.Rand(13, 20), 13.0f, 20.5f), 8.2f}; c.path = n.NavPath(c.pos, c.goal); c.nextGoalT = n.Rand(8, 14); return; }
    if (r < 0.25f) { c.seatKind = "toilets"; c.goal = {n.Rand(5, 8), n.Rand(21, 24)}; c.path = n.NavPath(c.pos, c.goal); c.nextGoalT = n.Rand(10, 18); return; }
    if (c.type == T_REGULAR && c.reg >= 0 && d.regulars[c.reg].stool >= 0) kind = "stool";
    else if (c.type == T_ODDBALL || c.type == T_FLIRT) { static const char* K[4] = {"snug", "dance", "tables", "yard"}; kind = K[(int)(n.Rand() * 4) % 4]; }
    else kind = HAUNT_SEATS[std::clamp(c.type, 0, T_COUNT - 1)];
    if (kind.empty()) kind = "tables";
    int want = kind == "stool" && c.reg >= 0 ? d.regulars[c.reg].stool : -1;
    int count = kind == "stool" ? (int)d.bar.stools.size() : (d.bar.Seats(kind) ? (int)d.bar.Seats(kind)->size() : 0);
    int pick = -1;
    if (want >= 0 && want < count) pick = want;
    else for (int k = 0, s0 = (int)(n.Rand() * std::max(1, count)); k < count; k++) { int s = (s0 + k) % count, key = SeatKey(kind, s); if (key >= 0 && key < (int)n.seatTaken.size() && n.seatTaken[key] < 0) { pick = s; break; } }
    if (pick < 0) { kind = "stool"; for (int s = 0; s < (int)d.bar.stools.size(); s++) if (n.seatTaken[SeatKey("stool", s)] < 0) { pick = s; break; } }
    if (pick < 0) { c.seatKind = "stand"; c.goal = {n.Rand(13, 26), n.Rand(2, 7)}; }
    else { c.seatKind = kind; c.seat = pick; int key = SeatKey(kind, pick); if (key >= 0 && key < (int)n.seatTaken.size()) n.seatTaken[key] = c.id; c.goal = SeatPos(kind, pick); }
    c.path = n.NavPath(c.pos, c.goal);
    c.nextGoalT = n.Rand(25, 70);   // (real seconds: six to seventeen game minutes)
}
void Night::StepPatrons(float dt) {
    const Data& d = D();
    float h = Hour();
    // the generator tops the room up to the crowd curve (and the extras drift home as it falls)
    int inside = 0; for (const auto& c : patrons) inside += (c.inside || (!c.gone && c.arriveH <= h && c.reg < 0)) && !c.gone && !c.leaving && c.ev < 0;   // (an event's people are on top of the curve)
    int want = CrowdTarget();
    if (inside < want && Rand() < dt * 0.4f && patrons.size() < 90) { int id = AddPatron(-1); (void)id; }
    if (inside > want + 2 && Rand() < dt * 0.15f) for (auto& c : patrons) if (c.inside && c.reg < 0 && c.ev < 0 && !c.leaving && c.talkingTo < 0) { c.leaveH = h; break; }
    for (auto& c : patrons) {
        if (c.gone) {   // (sent home by a fight or the police: back in twenty minutes)
            if (c.backAt > 0 && h >= c.backAt && !c.outForNight) { c.gone = false; c.inside = false; c.leaving = false; c.arriveH = h; c.backAt = 0; c.leaveH = std::max(c.leaveH, h + 1); }
            else continue;
        }
        if (!c.inside) {
            if (h < c.arriveH) continue;
            c.inside = true; c.pos = d.bar.nav.empty() ? Vector2{19.5f, -3} : d.bar.nav[0]; ChooseGoal(*this, c);
        }
        if (c.fight.brawl >= 0 || c.fight.Down() || c.fight.Busy()) continue;   // (in a fight: StepBrawls moves them)
        if (c.ev < 0 && EventOn("robbery") && c.inside) { c.vel = {0, 0}; continue; }        // (the room is held: everyone freezes)
        // the hour (doc p. 10): everyone loosens after 10 p.m. and sours after 1 a.m.
        if (h >= 22 && h < 25) c.mood = std::min(100.0f, c.mood + dt * 0.03f);
        if (h >= 25) c.mood = std::max(0.0f, c.mood - dt * 0.03f);
        // leaving at their hour (unless talking to someone)
        if (!c.leaving && h >= c.leaveH && c.talkingTo < 0 && c.playing < 0) {
            c.leaving = true;
            if (c.seat >= 0) { int key = SeatKey(c.seatKind, c.seat); if (key >= 0 && key < (int)seatTaken.size()) seatTaken[key] = -1; c.seat = -1; }
            c.sitting = false; c.seatKind = "leave"; c.goal = d.bar.nav[0]; c.path = NavPath(c.pos, c.goal);
        }
        // a patron being talked to stands still and faces you
        if (c.talkingTo >= 0) {
            const Player& p = players[c.talkingTo];
            float ty = atan2f(p.pos.y - c.pos.y, p.pos.x - c.pos.x); c.yaw += atan2f(sinf(ty - c.yaw), cosf(ty - c.yaw)) * std::min(1.0f, dt * 4);
            c.vel = {0, 0};
            continue;
        }
        // walking: along the path's doorways, then to the goal
        Vector2 aim = c.goal;
        while (!c.path.empty() && Vector2Distance(c.pos, d.bar.nav[c.path.front()]) < 0.7f) c.path.erase(c.path.begin());
        if (!c.path.empty() && Vector2Distance(c.pos, c.goal) > 2.5f) aim = d.bar.nav[c.path.front()];
        Vector2 to = Vector2Subtract(aim, c.pos); float L = Vector2Length(to);
        if (L > 0.35f) {
            float sp = 1.35f * (1 - 0.3f * std::clamp(c.drunk / 100, 0.0f, 1.0f));
            Vector2 v = Vector2Scale(to, sp / L);
            // a little room between people
            for (const auto& o : patrons) { if (&o == &c || !o.inside || o.gone) continue; Vector2 dd = Vector2Subtract(c.pos, o.pos); float l = Vector2Length(dd); if (l < 0.6f && l > 1e-3f) v = Vector2Add(v, Vector2Scale(dd, (0.6f - l) * 3 / l)); }
            for (const auto& p : players) { Vector2 dd = Vector2Subtract(c.pos, p.pos); float l = Vector2Length(dd); if (l < 0.6f && l > 1e-3f) v = Vector2Add(v, Vector2Scale(dd, (0.6f - l) * 3 / l)); }
            c.vel = Vector2Lerp(c.vel, v, std::min(1.0f, dt * 5));
            c.sitting = false;
        } else {
            c.vel = Vector2Scale(c.vel, 0.7f);
            if (!c.sitting && c.seatKind != "stand" && c.seatKind != "drink" && c.seatKind != "toilets") c.sitting = true;
            if (c.leaving && Vector2Distance(c.pos, d.bar.nav[0]) < 1.0f) { c.gone = true; c.inside = false; continue; }
            // at the bar: a drink (the bartender serves; teetotallers have water)
            if (c.seatKind == "drink" && c.drinkT <= 0) { c.drinkT = 8; bar.busyT = std::max(bar.busyT, 1.2f); if (!c.Has(d.Trait("teetotal"))) c.drunk += c.Has(d.Trait("drunkard")) ? 25.0f : 15.0f; }
        }
        if (Vector2Length(c.vel) > 0.1f) { float ty = atan2f(c.vel.y, c.vel.x); c.yaw += atan2f(sinf(ty - c.yaw), cosf(ty - c.yaw)) * std::min(1.0f, dt * 6); }
        c.pos = Vector2Add(c.pos, Vector2Scale(c.vel, dt));
        Collide(c.pos, 0.28f);
        c.walkPh += Vector2Length(c.vel) * dt * 1.6f;
        c.drinkT = std::max(0.0f, c.drinkT - dt);
        c.drunk = std::max(0.0f, c.drunk - dt * 0.25f);
        if (!c.leaving && (c.nextGoalT -= dt) <= 0) ChooseGoal(*this, c);
    }
    // a seated regular whose stool a player has taken is put out (doc p. 9: taking the stool starts a feud)
    for (auto& c : patrons) {
        if (!c.inside || c.gone || c.seatKind != "stool" || c.reg < 0 || D().regulars[c.reg].stool < 0) continue;
        Vector2 st = SeatPos("stool", c.seat);
        for (auto& p : players) if (Vector2Distance(p.pos, st) < 0.45f && Vector2Distance(c.pos, st) < 3 && c.talkingTo < 0 && Rand() < dt * 0.5f) {
            c.mood = std::max(0.0f, c.mood - 15); if (p.id < (int)c.mem.size()) c.mem[p.id].insults += 1;
            Say(c.name + ": \"That's MY stool.\"");
        }
    }
}
int Night::NearestPatron(const Player& p, float range) const {
    int best = -1; float bd = range;
    for (const auto& c : patrons) { if (!c.inside || c.gone || c.leaving) continue; float dd = Vector2Distance(c.pos, p.pos); if (dd < bd) { bd = dd; best = c.id; } }
    return best;
}
// ---------------------------------------------------------------- the conversation (doc p. 10 and p. 40)
float Night::Difficulty(const Patron& c, int o) const {
    // base difficulty (ask 40, agree 30, joke 50, challenge 60) adjusted by the patron's traits
    static const float BASE[4] = {40, 30, 50, 60};
    float x = BASE[std::clamp(o, 0, 3)];
    const Data& d = D();
    auto has = [&](const char* t) { return c.Has(d.Trait(t)); };
    if (has("friendly")) x -= 10;
    if (has("suspicious") && o == 0) x += 15;
    if (has("honest") && (o == 1 || o == 3)) x -= 10;
    if (has("loud") && o == 2) x -= 15;
    if (has("grieving") && o == 2) x += 30;
    if (has("liar") && o == 0) x += 10;
    return x;
}
void Night::StartTalk(Player& p, int idx) {
    if (idx < 0 || idx >= (int)patrons.size() || p.talk.patron >= 0 || p.st != State::Active) return;
    Patron& c = patrons[idx];
    if (!c.inside || c.gone || c.talkingTo >= 0 || Vector2Distance(c.pos, p.pos) > 2.4f) return;
    p.talk = Talk{}; p.talk.patron = idx; p.talk.target = 3 + (int)(Rand() * 3);   // (three to five exchanges)
    c.talkingTo = p.id;
    const Data& d = D();
    // a Hostile patron won't talk; a Brooder bothered past their tolerance turns Hostile
    if (c.mood < 20) { p.talk.theirLine = d.talk.hostile.Pick(rng); p.talk.over = true; p.talk.overT = 1.8f; p.talk.result = TextFormat("%s is Hostile.", c.name.c_str()); return; }
    c.bothers++;
    if (c.bothers > d.types[c.type].tolerance) { c.mood = std::max(0.0f, c.mood - 15); }
    p.talk.theirLine = d.talk.greet[c.type].Pick(rng >> 3);
    if (p.id < (int)c.mem.size()) c.mem[p.id].talks += 1;
}
void Night::TalkChoose(Player& p, int o) {
    Talk& T = p.talk;
    if (T.patron < 0 || T.over) { if (o == 6) EndTalk(p); return; }
    Patron& c = patrons[T.patron];
    const Data& d = D();
    Memory* M = p.id < (int)c.mem.size() ? &c.mem[p.id] : nullptr;
    if (o == 6) { EndTalk(p); return; }
    if (o == 5) {   // buy them a drink: on your tab (a pint's price), +15 mood (the Twins: buying one buys both)
        float price = PriceOf(DrinkIndex("pint")) * (c.name == "The Twins" ? 2 : 1);
        if (p.money - p.tab < price) { T.theirLine = "(You pat your pockets. Empty.)"; return; }
        p.tab += price; p.spent += price;
        c.mood = std::min(100.0f, c.mood + 15); if (M) M->drinks += 1;
        T.myCaption = d.talk.buy.Pick(rng); T.theirLine = d.talk.drink.Pick(rng >> 5);
        bar.busyT = std::max(bar.busyT, 1.2f);
        Note(p, 3, "Bought " + c.name + " a drink.");
        QuinceTells(p, c);   // (the Monkey's surgeon: a drink buys a name)
        return;
    }
    if (o == 4) {   // listening (Talkers): always works, costs time; every 30 s of it is +2 drunk (you keep sipping) and +1 mood
        T.myCaption = d.talk.listen.Pick((uint32_t)(t * 3)); T.theirLine = d.talk.listenReply.Pick((uint32_t)(t * 7));
        p.drunk = std::min(100.0f, p.drunk + 2.0f * (5.0f / 30)); c.mood = std::min(100.0f, c.mood + 1); T.listenT += 5;
        if (M) M->listened += 5;
        // Mr. Lemmon pays your tab if you listen for ten minutes (a game minute is four seconds)
        if (c.name == "Mr. Lemmon" && M && M->listened >= 10 * SECONDS_PER_GAME_MINUTE && p.tab > 0) { Say("Mr. Lemmon pays " + p.name + "'s tab, misty-eyed."); Note(p, 5, TextFormat("Mr. Lemmon paid a tab of %.0f.", p.tab)); p.tab = 0; M->listened = -1e9f; }
        return;
    }
    // a roll: d100 + charisma (charisma% - 100) + mood (Delighted +20, Annoyed -15, Hostile -40) + noise (-10 at peak unless close)
    int option = std::clamp(o, 0, 3);
    T.substituted = false;
    if (p.drunk >= 80 && Rand() < 0.5f) { option = 3; T.substituted = true; }   // (at 80+, half the time the most insulting thing comes out)
    else if (p.drunk >= 60 && Rand() < 0.25f) { option = (int)(Rand() * 4) % 4; T.substituted = option != o; }
    float roll = Rand(1, 100) + (Charisma(p) * 100 - 100) + AfterFightCharisma(p, c) * 100;
    roll += c.mood >= 80 ? 20 : c.mood < 20 ? -40 : c.mood < 40 ? -15 : 0;
    if (Noise() > 0.8f && Vector2Distance(c.pos, p.pos) > 1.0f) roll -= 10;
    bool win = roll >= Difficulty(c, option);
    if (p.wareT[W_KELP] > 0 && option != 2) win = false;   // (Kelp Smoke: you laugh through anything serious)
    T.lastOption = option; T.lastWin = win; T.exchanges++;
    T.myCaption = (T.substituted ? d.talk.youDrunk[option] : d.talk.you[option]).Pick((uint32_t)(t * 13) + option);
    T.theirLine = (win ? d.talk.ok[option] : d.talk.fail[option]).Pick((uint32_t)(t * 17));
    if (win) { T.wins++; c.mood = std::min(100.0f, c.mood + 4); } else { T.losses++; c.mood = std::max(0.0f, c.mood - 6); if (option == 3 && M) M->insults += 1; }
    // three successes end it well; two failures end it badly (doc p. 40)
    if (T.wins >= 3 || (T.exchanges >= T.target && T.wins > T.losses)) {
        T.over = true; T.overT = 3.5f; c.mood = std::min(100.0f, c.mood + 15);
        T.theirLine = d.talk.endGood.Pick((uint32_t)(t * 3));
        // the reward: their secret, a laugh (+5 charisma for 5 minutes) or an item
        float r = Rand();
        if (r < 0.5f && !c.secret.empty()) { T.result = c.name + "'s secret: " + c.secret + "."; if (std::find(p.known.begin(), p.known.end(), c.name) == p.known.end()) p.known.push_back(c.name); Note(p, 6, "Learned " + c.name + "'s secret."); }
        else if (r < 0.8f) { T.result = "A laugh shared: +5% charisma for 5 minutes."; p.charBuff = std::max(p.charBuff, 0.05f); p.charBuffT = std::max(p.charBuffT, 5 * SECONDS_PER_GAME_MINUTE); }
        else { std::string it = d.talk.items[(int)(Rand() * d.talk.items.size()) % d.talk.items.size()]; p.items.push_back(it); T.result = c.name + " gives you " + it + "."; Note(p, 7, "Was given " + it + " by " + c.name + "."); }
    } else if (T.losses >= 2 || T.exchanges >= T.target) {
        T.over = true; T.overT = 3.0f; c.mood = std::max(0.0f, c.mood - 15);
        T.theirLine = d.talk.endBad.Pick((uint32_t)(t * 5));
        bool violent = c.Has(d.Trait("violent"));
        T.result = violent && T.lastOption == 3 ? c.name + " swings at you!" : c.name + " has had enough of you.";
        if (violent && T.lastOption == 3) { c.mood = std::max(0.0f, c.mood - 20); Note(p, 4, c.name + " took a swing at you."); T.overT = 0.4f; StartBrawl(PatronW(c.id), PlayerW(p.id), PatronW(c.id)); c.fight.foe = PlayerW(p.id); }
    }
}
void Night::EndTalk(Player& p) {
    if (p.talk.patron >= 0 && p.talk.patron < (int)patrons.size()) patrons[p.talk.patron].talkingTo = -1;
    p.talk = Talk{};
}

// --patron-check (doc p. 30): every regular has a type, three traits, a secret and a face; the lines exist
int RunPatronCheck() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    for (int bi = 0; bi < BAR_COUNT; bi++) {
    const Data& d = DataOf(bi);
    printf("A Night Off: the patrons of %s\n", BarName(bi));
    if (bi == BAR_GULL) check(d.regulars.size() >= 29, TextFormat("%d regulars (and the bartender: thirty)", (int)d.regulars.size()));
    else check(d.regulars.size() >= 19, TextFormat("%d regulars (with Celeste and the stuffed marlin: the doc's twenty, and Dottie)", (int)d.regulars.size()));
    for (const auto& r : d.regulars) {
        bool ok = r.traits.size() == 3 && !r.secret.empty() && r.type >= 0 && r.type < T_COUNT && r.arrive < r.leave;
        if (!ok) check(false, r.name + " is missing a type, three traits, a secret or a schedule");
    }
    check(d.traitNames.size() == 30 && d.genericSecrets.size() == 8, "thirty traits and eight generic secrets");
    for (int t = 0; t < T_COUNT; t++) if (d.talk.greet[t].v.empty() || d.talk.greet[t].v[0] == "...") check(t == T_BROODER, std::string("greetings for ") + TypeName(t));
    // the walking graph: no link runs through the furniture or a wall (a patron or a bot would walk into it and stick)
    {
        const BarData& B = d.bar; std::string bad;
        auto cross = [](Vector2 a, Vector2 b, Vector2 c, Vector2 e) { auto cr = [](Vector2 o, Vector2 p, Vector2 q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); }; return ((cr(c, e, a) > 0) != (cr(c, e, b) > 0)) && ((cr(a, b, c) > 0) != (cr(a, b, e) > 0)); };
        for (int i = 0; i < (int)B.navLinks.size(); i++) for (int j : B.navLinks[i]) {
            if (j < i) continue;
            Vector2 a = B.nav[i], b = B.nav[j]; bool hit = false;
            for (int k = 0; k <= 40 && !hit; k++) { Vector2 q = Vector2Lerp(a, b, k / 40.0f); for (const auto& x : B.boxes) if (q.x > x.r.x - 0.25f && q.x < x.r.x + x.r.width + 0.25f && q.y > x.r.y - 0.25f && q.y < x.r.y + x.r.height + 0.25f) hit = true; }
            for (const auto& w : B.walls) if (cross(a, b, w.a, w.b)) hit = true;
            if (hit) bad += " " + B.navNames[i] + "-" + B.navNames[j];
        }
        check(bad.empty(), "the walking graph is clear of furniture and walls" + (bad.empty() ? std::string() : ":" + bad));
    }
    // every seat and spot is out of the furniture (a patron sent there would stick)
    {
        const BarData& B = d.bar; std::string bad;
        auto inBox = [&](Vector2 q) { for (const auto& x : B.boxes) if (x.kind != "table" && q.x > x.r.x && q.x < x.r.x + x.r.width && q.y > x.r.y && q.y < x.r.y + x.r.height) return true; return false; };
        for (const auto& s : B.seats) for (const auto& q : s.second) if (inBox(q)) bad += " " + s.first;
        for (const auto& q : B.stools) if (inBox(q)) bad += " stool";
        check(bad.empty(), "every seat is clear of the furniture" + (bad.empty() ? std::string() : ":" + bad));
    }
    }
    printf(fails ? "patron-check: %d FAILED\n" : "patron-check: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace no
