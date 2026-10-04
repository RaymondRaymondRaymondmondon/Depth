// A Night Off: the Brass Monkey's house rules (doc pp. 30-34), stage 10. Headless. The Monkey is the Gull's footprint
// with its own rooms, cast, cocktails and prices (data: nightoff_bar_monkey.json, nightoff_patrons_monkey.json); what
// it does differently is here: Horace at the velvet rope (Wrecked or barred stays out, unless a bribe of 30), the
// library (a quiet room: a fight there is an instant bar), Celeste (remembers everything and forgives nothing),
// Anselm the croupier (cheating is harder, and he touches his bow tie), Sergeant Mallory (a fight near him is an
// inspection), Dr. Quince (a drink buys the name of the flirt with a cooler), Juniper Vale (knows when the police are
// crossing the harbour), the cartel's business in the cellar after 11, and Madame Ostrova's seance at midnight, where
// the stuffed marlin answers.
#include "nightoff.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

static Patron* Named(Night& n, const char* name, bool insideOnly = true) {
    for (auto& c : n.patrons) if (c.name == name && !c.gone && (!insideOnly || c.inside)) return &c;
    return nullptr;
}
// the velvet rope: anyone coming in off the street who is Wrecked, or barred, stays out (a bribe lets them in)
bool Night::AtRope(const Player& p) const { return CurBar() == BAR_MONKEY && p.pos.y < 0.6f && p.pos.x > 16 && p.pos.x < 23 && p.st == State::Active; }
bool Night::RopeStops(const Player& p) const { return CurBar() == BAR_MONKEY && !p.letIn && (p.barred || p.drunk >= 80); }
void Night::StepMonkey(float dt) {
    if (CurBar() != BAR_MONKEY) return;
    // Horace at the rope
    for (auto& p : players) {
        if (p.st != State::Active || !RopeStops(p)) continue;
        if (p.pos.y > -0.6f && p.pos.y < 3.0f && p.pos.x > 15.5f && p.pos.x < 23.5f && p.ropeIn) {   // (they came in from the street: back out)
            p.pos.y = -0.8f; p.vel = {0, 0};
            if (p.toastT <= 0) { p.toast = p.barred ? "Horace, the doorman: \"Not tonight, sir. Celeste's orders.\" (A bribe of 30 might change his mind.)" : "Horace looks you up and down: \"Not in that state, sir.\" (A bribe of 30 might help.)"; p.toastT = 4; }
        }
        p.ropeIn = p.pos.y < -0.3f;   // (outside: the next step in is checked)
    }
    // Celeste forgives nothing: her mood never climbs more than 10 above the lowest it's been tonight
    bar.low = std::min(bar.low, bar.mood); bar.mood = std::min(bar.mood, bar.low + 10);
    // Sergeant Mallory: a fight near him is an inspection
    if (Patron* m = Named(*this, "Sergeant Mallory")) {
        for (const auto& b : brawls) {
            if (b.over || EventOn("police") || policeInT > 0) continue;
            bool near = false; for (const auto& p : players) near |= p.fight.brawl >= 0 && Vector2Distance(p.pos, m->pos) < 9;
            for (const auto& c : patrons) near |= c.fight.brawl >= 0 && c.inside && Vector2Distance(c.pos, m->pos) < 9;
            if (near) { Say("Sergeant Mallory stands up and shows his warrant card. \"Police. Nobody move.\""); ForceEvent("police", Hour()); break; }
        }
    }
    // Juniper Vale and the telescope: the police are crossing the harbour (a warning, once, to whoever talks to her)
    if (Patron* j = Named(*this, "Juniper Vale")) {
        int pi = EventIndex("police");
        if (pi >= 0 && !events[pi].started && events[pi].startH - Hour() < 1.0f)
            for (auto& p : players) if (p.talk.patron == j->id && !p.juniperTold) { p.juniperTold = true; Note(p, 6, "Juniper Vale, at the telescope: \"The police boat is crossing the harbour. Twenty minutes.\""); p.toast = "Juniper Vale: \"The police are crossing the harbour.\""; p.toastT = 5; }
    }
    // Madame Ostrova's seance at midnight, in the library: the stuffed marlin answers (a true reading for everyone there)
    if (!seanceDone && Hour() >= 24.0f) {
        seanceDone = true;
        if (Patron* o = Named(*this, "Madame Ostrova")) {
            o->pos = {35.6f, 5.6f}; o->goal = o->pos; o->nextGoalT = 30;
            Say("Madame Ostrova dims the library lamps. \"Silence. He is speaking.\" The stuffed marlin's glass eye catches the fire.");
            for (auto& p : players) {
                if (p.st != State::Active || RoomAt(p.pos) != std::string("The library")) continue;
                GRng r; r.s = opts.seed * 31 + p.id * 7 + 1;
                int reads = p.fortuneReads; p.fortuneReads = 0;
                fortune::Reading R = fortune::Read(*this, p, r);
                p.fortuneReads = reads;
                if (R.fated >= 0 && p.fatedThief < 0) p.fatedThief = R.fated;
                Note(p, 5, "The marlin spoke at the seance: " + R.text[0] + " " + R.text[1] + " " + R.text[2]);
                p.toast = "The marlin: \"" + R.text[0] + "\""; p.toastT = 7;
            }
            Flag("seance", "");
        }
    }
}
// a fight in the library is an instant bar (Celeste's rule): the one who swung is out
void Night::LibraryRule(Who att) {
    if (CurBar() != BAR_MONKEY || att.kind != 0 || att.idx < 0 || att.idx >= (int)players.size()) return;
    Player& p = players[att.idx];
    if (RoomAt(p.pos) != std::string("The library") || p.st == State::Gone) return;
    Say("Celeste, from the doorway: \"Not in the library.\" Horace takes " + p.name + " by the collar.");
    Note(p, 9, "Started a fight in the library. Celeste's rule: barred, on the spot.");
    Flag("library", p.name);
    p.barred = true;
    Leave(p, E_THROWN_OUT, "on the Monkey's front steps, barred: no fights in the library");
}
// Dr. Quince, for a drink: which flirt carries a cooler
void Night::QuinceTells(Player& p, Patron& c) {
    if (CurBar() != BAR_MONKEY || c.name != "Dr. Quince") return;
    std::string who;
    for (const auto& x : patrons) if (x.thief && !x.gone && (x.inside || x.arriveH < Hour() + 2)) { who = x.name; break; }
    if (who.empty()) { p.talk.theirLine = "\"No coolers tonight. I'd know; I can smell the ice.\""; return; }
    p.talk.theirLine = "\"Since you ask. " + who + " carries a cooler. I recognise a stitch when I see one.\"";
    if (std::find(p.known.begin(), p.known.end(), who) == p.known.end()) p.known.push_back(who);
    Note(p, 6, "Dr. Quince named the Monkey's kidney thief: " + who + ".");
}

bool Night::FerryRunning() const { float m = fmodf(Minutes(), 60); return m < 10 && Hour() >= 20; }
void Night::FerryFrom(const Night& from, int pid) {
    const Player& old = from.players[std::clamp(pid, 0, (int)from.players.size() - 1)];
    Opts o = from.opts; o.players = 1; o.bar = 1 - from.opts.bar; o.seed = from.opts.seed * 2654435761u + 77; o.startMinutes = std::min(470.0f, from.Minutes() + 15);
    Init(o);
    Player p = old;   // (everything comes with you: the money, the tab, the meter, the kidneys, the log, the wares)
    p.id = 0; p.st = State::Active; p.ending = E_NONE; p.wokeAt.clear(); p.ferried = false;
    p.pos = D().bar.spawn; p.vel = {0, 0}; p.yaw = PI * 0.5f;
    p.talk = Talk{}; p.flirt = Flirt{}; p.game = GameSeat{}; p.fight = Combat{}; p.in = Input{}; p.leavingT = 0; p.leavingWith = -1; p.carrying = p.carriedBy = -1;
    p.drunk = std::max(0.0f, p.drunk - 15 * D().soberPerMin);   // (fifteen minutes of sea air)
    p.letIn = false; p.ropeIn = false; p.fatedThief = -1; p.fateMet = false; p.cartelDue = 0;
    p.fight.hp = 100 * Toughness(p);
    players[0] = p;
    for (auto& c : patrons) c.mem.assign(1, Memory{});
    Note(players[0], 0, std::string("Stepped off the ferry at ") + D().bar.name + " at " + Clock() + ".");
    Say(players[0].name + (players[0].name == "You" ? " step" : " steps") + " off the ferry.");
    // a Wrecked or barred arrival meets the Monkey's rope from the street side
    if (CurBar() == BAR_MONKEY && RopeStops(players[0])) { players[0].pos = {19.5f, -1.6f}; players[0].ropeIn = true; }
}

// ---------------------------------------------------------------- --night-test's stage 10c checks (the Brass Monkey)
int NightMonkeyChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const Data& g = DataOf(BAR_GULL); const Data& m = DataOf(BAR_MONKEY);
    check(m.bar.name == "The Brass Monkey" && m.bar.priceMul == 1.5f && m.bar.roof, "the Brass Monkey's data: its name, its price board, its roof");
    int monkeyCast = 0, travellers = 0; for (const auto& r : m.regulars) { monkeyCast += !r.travels; travellers += r.travels; }
    check(monkeyCast >= 14 && travellers == 5, TextFormat("the Monkey's cast (%d) and the five travellers", monkeyCast));
    bool cocktails = DrinkIndex("pint") >= 0; for (const char* k : {"harbor", "sidecar", "marlin", "widow", "champagne"}) { bool f = false; for (const auto& d : m.drinks) f |= d.key == k; cocktails &= f; }
    bool noGull = true; for (const auto& d : m.drinks) noGull &= d.key != "gull";
    check(cocktails && noGull, "Celeste's cocktails, beer, and no Gull house special");
    // each traveller is at one bar a night
    { int both = 0, none = 0; for (uint32_t s = 1; s < 60; s++) for (const auto& r : g.regulars) if (r.travels) { bool a = TravellerHere(r.name, BAR_GULL, s), b = TravellerHere(r.name, BAR_MONKEY, s); both += a && b; none += !a && !b; }
      check(both == 0 && none == 0, "a traveller is at exactly one bar each night"); }
    Night n; Opts o; o.seed = 21; o.bar = BAR_MONKEY; o.events = false; o.players = 2; n.Init(o);
    check(CurBar() == BAR_MONKEY && std::string(RoomAt({35, 5})) == "The library", "a Monkey night: the library where the Gull's snug is");
    int pint = DrinkIndex("pint");
    check(n.PriceOf(pint) == roundf(5 * 1.5f), TextFormat("everything 50%% more (a pint %.0f)", n.PriceOf(pint)));
    bool horace = false, anselm = false; for (const auto& c : n.patrons) { horace |= c.name == "Horace"; anselm |= c.name == "Anselm"; }
    check(horace && anselm, "the doorman and the croupier are on the books");
    // the rope: barred stays out, a bribe gets in
    Player& p = n.players[0]; p.barred = true; p.pos = {19.5f, -2.0f}; p.ropeIn = true;
    for (int i = 0; i < 80; i++) { p.in.moveZ = 1; n.Step(0.05f); }
    check(p.pos.y < 0.2f, TextFormat("barred: Horace keeps you on the street (z %.1f)", p.pos.y));
    bool offered = false; for (const auto& op : n.EventOptions(p)) offered |= op.act == 51;
    check(offered, "and he can be bribed");
    p.money = 100; p.in.evAct = 51; n.Step(0.05f);
    for (int i = 0; i < 80; i++) { p.in.moveZ = 1; n.Step(0.05f); }
    check(p.letIn && p.pos.y > 1.0f && p.money == 70, "30 to Horace, and in you go");
    // the library: a fight there is an instant bar
    Player& q = n.players[1]; q.pos = {35, 6}; q.st = State::Active;
    int target = -1; for (auto& c : n.patrons) if (c.inside && !c.gone && c.type != T_STAFF) { target = c.id; c.pos = {35.6f, 6}; c.vel = {0, 0}; break; }
    if (target >= 0) { q.yaw = 0; q.in.attack = MV_JAB; for (int i = 0; i < 40 && q.st != State::Gone; i++) { n.patrons[target].pos = {35.6f, 6}; n.Step(0.05f); } }
    check(target >= 0 && q.st == State::Gone && q.ending == E_THROWN_OUT, "a punch in the library: thrown out and barred (Celeste's rule)");
    // Celeste forgives nothing
    { Night c2; Opts o2 = o; o2.players = 1; c2.Init(o2); c2.bar.mood = 30; c2.Step(0.05f); c2.bar.mood = 90; c2.Step(0.05f); check(c2.bar.mood <= 40.5f, TextFormat("Celeste forgives nothing (mood held to %.0f)", c2.bar.mood)); }
    // the croupier: cheating is harder here
    { Night gn; Opts og; og.seed = 5; og.events = false; gn.Init(og); Player& a = gn.players[0]; a.drunk = 10; float cg = gn.CheatChance(a);
      Night mn; Opts om = og; om.bar = BAR_MONKEY; mn.Init(om); Player& b = mn.players[0]; b.drunk = 10; float cm = mn.CheatChance(b);
      check(cm < cg * 0.7f, TextFormat("the croupier watches: cheating %.0f%% at the Monkey, %.0f%% at the Gull", cm * 100, cg * 100)); }
    // Dr. Quince, for a drink, names the thief
    { Night qn; Opts oq = o; oq.players = 1; oq.startMinutes = 150; qn.Init(oq); Player& a = qn.players[0]; a.money = 200;
      Patron* dq = nullptr; for (auto& c : qn.patrons) if (c.name == "Dr. Quince") dq = &c;
      bool thiefIn = false; for (auto& c : qn.patrons) if (c.thief) { c.inside = true; c.gone = false; thiefIn = true; }
      if (dq) { dq->inside = true; dq->gone = false; dq->mood = 70; dq->talkingTo = -1; a.pos = Vector2Add(dq->pos, {0.8f, 0}); qn.StartTalk(a, dq->id); qn.TalkChoose(a, 5); }
      bool named = false; for (const auto& c : qn.patrons) if (c.thief && std::find(a.known.begin(), a.known.end(), c.name) != a.known.end()) named = true;
      check(dq && thiefIn && named, "a drink for Dr. Quince buys the name of the flirt with a cooler"); }
    // the seance at midnight: the marlin answers those in the library
    { Night sn; Opts os = o; os.players = 1; os.startMinutes = 290; sn.Init(os); Player& a = sn.players[0]; a.pos = {35, 6};
      for (auto& c : sn.patrons) if (c.name == "Madame Ostrova") { c.inside = true; c.gone = false; c.arriveH = 19; }
      for (int i = 0; i < (int)(12 * SECONDS_PER_GAME_MINUTE / 0.05f) && !sn.seanceDone; i++) { a.pos = {35, 6}; sn.Step(0.05f); }
      bool spoke = false; for (const auto& x : a.log) spoke |= x.text.find("The marlin spoke") != std::string::npos;
      check(sn.seanceDone && spoke, "midnight: Madame Ostrova's seance, and the stuffed marlin answers"); }
    // the cellar after 11: the cartel's business
    { Night cn; Opts oc = o; oc.players = 1; oc.startMinutes = 250; cn.Init(oc); Player& a = cn.players[0]; a.pos = {36.5f, 24.5f};
      check(cn.WaresHere(a), "after 11 the cartel does its business in the cellar (no event needed)"); }
    // golf on the roof: three holes
    { Night rn; Opts orr = o; orr.players = 1; rn.Init(orr); Player& a = rn.players[0]; a.pos = DataOf(BAR_MONKEY).bar.golf; a.money = 100;
      a.in.startGame = GK_GOLF; a.in.gameOpp = -1; rn.Step(0.05f);
      check(a.game.kind == GK_GOLF && a.game.golf.holes == 3, "the roof's putting green has three holes"); }
    // the ferry: on the hour, 10, and the night carries on at the other bar with your tab and your meter
    { Night a; Opts oa; oa.seed = 13; oa.events = false; a.Init(oa); Player& p = a.players[0];
      a.t = (21 - 19) * 60 * SECONDS_PER_GAME_MINUTE + 4 * SECONDS_PER_GAME_MINUTE; p.pos = DataOf(BAR_GULL).bar.door; p.pos.y += 1.0f; p.money = 120; p.tab = 22; p.drunk = 50; p.drinks = 3;
      bool offered = false; for (const auto& op : a.EventOptions(p)) offered |= op.act == 52;
      check(offered && a.FerryRunning(), "the ferry runs on the hour, from the door");
      p.in.evAct = 52; a.Step(0.05f);
      check(p.st == State::Gone && p.ferried && p.money == 110, "10 to the ferryman");
      Night b; b.FerryFrom(a, 0); Player& q = b.players[0];
      check(CurBar() == BAR_MONKEY && b.Minutes() >= a.Minutes() + 14.5f && q.st == State::Active && q.tab == 22 && q.drinks == 3 && fabsf(q.drunk - (p.drunk - 15)) < 0.5f, TextFormat("and the night goes on at the Monkey, fifteen minutes on, the tab following (%.0f), the meter a little lower (%.0f)", q.tab, q.drunk));
      SetBar(BAR_GULL); }
    // each bartender remembers on their own: a tab left at the Monkey bars you there, not at the Gull
    { NightProfile pr; pr.owed[BAR_MONKEY] = 40;
      Night a; Opts oa; oa.seed = 9; oa.events = false; a.Init(oa); a.ApplyProfile(a.players[0], pr);
      Night b; Opts ob = oa; ob.bar = BAR_MONKEY; b.Init(ob); b.ApplyProfile(b.players[0], pr);
      check(!a.players[0].barred && b.players[0].barred && b.players[0].pos.y < 0, "Celeste remembers a tab the Gull never heard of: barred at the Monkey's rope, welcome at the Gull");
      NightProfile s; check(ParseProfileSummary(ProfileSummary(pr), s) && s.owed[BAR_MONKEY] == 40 && s.owed[BAR_GULL] == 0, "both bartenders' memories travel in the summary"); }
    SetBar(BAR_GULL);
    return fails;
}

} // namespace no
