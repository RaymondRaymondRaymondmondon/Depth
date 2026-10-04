// A Night Off: the cartel's wares (doc pp. 36-37), stage 10. Headless. From the moment the cartel arrives, the quiet
// man's men sell a player anything on the list for money: eight substances, each funny, useful or disastrous (most are
// two of those), stacking with drink. One dose of each a night; nothing to a player above 80 drunk; a buyer seen by the
// police inspection (or Sergeant Mallory, at the Monkey) is searched and fined; a dose can be slipped into another
// player's drink (the Siren and the Cocktail: the game's dirtiest trick).
#include "nightoff.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace no {

struct WaresData { std::vector<WareDef> w; std::vector<std::string> cocktail, skipped; float maxDrunk = 80, fine = 50; };
static const WaresData& WD() {
    static WaresData d = [] {
        WaresData d; Json j = LoadJsonFile(rt::DataDir() + "/../nightoff/nightoff_wares.json");
        d.maxDrunk = j["max_drunk"].F(80); d.fine = j["search_fine"].F(50);
        for (const Json& e : j["wares"].a) { WareDef x; x.key = e["key"].Str0(); x.name = e["name"].Str0(); x.effect = e["effect"].Str0(); x.catchText = e["catch"].Str0(); x.price = e["price"].F(30); x.minutes = e["minutes"].F(5); d.w.push_back(x); }
        for (const Json& s : j["cocktail"].a) d.cocktail.push_back(s.Str0());
        for (const Json& s : j["skipped"].a) d.skipped.push_back(s.Str0());
        while ((int)d.w.size() < W_COUNT) d.w.push_back(WareDef{"?", "?", "", "", 999, 1});
        return d;
    }();
    return d;
}
const std::vector<WareDef>& Wares() { return WD().w; }
static float Sec(float gameMinutes) { return gameMinutes * SECONDS_PER_GAME_MINUTE; }

bool Night::WaresHere(const Player& p) const {
    if (p.st != State::Active) return false;
    if (CurBar() == BAR_MONKEY && Hour() >= 23 && Vector2Distance(p.pos, {36.5f, 25.0f}) < 3.0f) return true;   // (the Monkey: the cellar, any time after 11)
    if (!EventOn("cartel")) return false;
    for (const auto& c : patrons) if ((c.role == "a large man" || c.role == "the quiet man") && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 2.4f) return true;
    return false;
}
std::string Night::BuyWare(Player& p, int w, int slipTo) {
    if (w < 0 || w >= W_COUNT) return "Not on the list.";
    const WareDef& d = Wares()[w];
    Player* into = slipTo >= 0 && slipTo < (int)players.size() && slipTo != p.id ? &players[slipTo] : nullptr;
    if (slipTo >= 0 && !into) return "Nobody to slip it to.";
    Player& who = into ? *into : p;
    if (!WaresHere(p)) return "The quiet man's men aren't here.";
    if (p.money < d.price) return TextFormat("It's %.0f, and you don't have it.", d.price);
    if (!into && p.drunk > WD().maxDrunk) { Say("The quiet man, mildly: \"Not tonight. I have standards.\""); return "The quiet man has standards."; }
    if ((who.wares >> w) & 1) return who.name + (into ? " has" : " have") + " had that tonight.";
    if (into && (who.st != State::Active && who.st != State::Drinking)) return who.name + " isn't drinking.";
    if (into && Vector2Distance(who.pos, p.pos) > 2.0f) return "Get closer to their glass.";
    p.money -= d.price;
    // seen buying by the inspection (or an off-duty sergeant): searched, the dose taken, a fine
    bool seen = EventOn("police");
    for (const auto& c : patrons) if (c.name == "Sergeant Mallory" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 6) seen = true;
    if (seen) {
        float fine = std::min(p.money, WD().fine); p.money -= fine;
        Say("A constable's hand on " + p.name + "'s shoulder: \"Turn out your pockets.\"");
        Note(p, 9, TextFormat("Searched buying %s from the cartel: it was taken, and a %.0f fine.", d.name.c_str(), fine));
        return "";
    }
    if (into) {
        Note(p, 5, "Slipped " + d.name + " into " + who.name + "'s drink.");
        Flag("slipped", p.name);
    } else Note(p, 0, TextFormat("Bought %s from the quiet man's men (%.0f).", d.name.c_str(), d.price));
    DoseWare(who, w);
    return "";
}
void Night::DoseWare(Player& p, int w) {
    if (w < 0 || w >= W_COUNT || ((p.wares >> w) & 1)) return;
    const WareDef& d = Wares()[w];
    p.wares |= (uint16_t)(1u << w);
    p.wareT[w] = Sec(d.minutes);
    switch (w) {
        case W_KELP:
            for (auto& c : patrons) if (c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 5) c.mood = std::min(100.0f, c.mood + 15);
            for (auto& c : patrons) if (c.name == "The Reverend" && c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 10) { c.mood = std::max(0.0f, c.mood - 25); Say("The Reverend glares at " + p.name + "'s giggling."); }
            Say(p.name + " finds everything very, very funny.");
            break;
        case W_ANGLER: p.visionsT = std::max(p.visionsT, p.wareT[w]); p.hallucSeed = (uint32_t)(Rand() * 1e9f) | 1; break;
        case W_SIREN:
            for (auto& c : patrons) if (c.inside && !c.gone && c.type != T_STAFF) c.mood = std::max(c.mood, 85.0f);
            p.sirenT = 4;
            Say("Every head in the room turns toward " + p.name + ", and stays turned.");
            break;
        case W_COCKTAIL: {
            p.cocktail = 1 + (int)(Rand() * 6) % 6;
            const auto& L = WD().cocktail;
            Note(p, 5, "Drank the quiet man's Cocktail. " + (p.cocktail - 1 < (int)L.size() ? L[p.cocktail - 1] : std::string()));
            if (p.cocktail == 1) { p.money += 300; p.sureHome = true; Flag("cocktail_best", p.name); Say(p.name + " is having the best night of their life."); }
            if (p.cocktail == 2) Say(p.name + " announces that they can fly.");
            if (p.cocktail == 3) { p.barkeepT = p.wareT[w]; Say(p.name + " vaults the bar. The bartender, oddly, lets them."); }
            if (p.cocktail == 4) { goatOwner = p.id; goatOn = true; goatPos = Vector2Add(p.pos, {1, 0}); goatVel = {0, 0}; Say("A goat trots in from the street and sits at " + p.name + "'s feet. They nod at each other."); }
            if (p.cocktail == 5) { Flag("cocktail_gone", p.name); Leave(p, E_WALKED, "in your own bunk at noon, with a headline you don't remember earning"); }
            if (p.cocktail == 6) { p.kidneys = std::max(0, p.kidneys - 1); Flag("kidney", p.name); Flag("cocktail_kidney", p.name); Leave(p, E_PASSED_OUT, ""); p.wokeAt = "in a bathtub of ice; the quiet man's card is pinned to the note"; }
        } break;
        default: break;
    }
    AddPop(p.pos, 2.2f, d.name, {200, 170, 255, 255});
}
float Night::WareCharisma(const Player& p) const {
    float k = 1;
    if (p.wareT[W_OIL] > 0) k *= 1.3f;
    if (p.wareAfterT[W_OIL] > 0) k *= 0.7f;
    if (p.cocktail == 1 && p.wareT[W_COCKTAIL] > 0) k *= 2.0f;
    return k;
}
float Night::WareToughness(const Player& p) const {
    float k = 1;
    if (p.wareT[W_BARNACLE] > 0) k *= 1.5f;
    if (p.wareT[W_COCKTAIL] > 0) k *= p.cocktail == 1 ? 2.0f : p.cocktail == 2 ? 1.4f : 1.0f;
    return k;
}
float Night::PlayerAim(const Player& p) const {
    float k = AimMul(p.drunk);
    if (p.shakesT > 0) k *= 4;                         // (Sea Salt's catch, and the shakes from a drink)
    if (p.wareT[W_PRESSURE] > 0) k *= 0.4f;            // (Deep Pressure: time slows)
    if (p.steadyT > 0) k *= 0.5f;                      // (the Monkey's Marlin cocktail)
    if (p.cocktail == 1 && p.wareT[W_COCKTAIL] > 0) k *= 0.6f;
    return k;
}
void Night::StepWares(Player& p, float dt) {
    if (!p.wares) return;
    // each ware ends into its catch
    for (int w = 0; w < W_COUNT; w++) {
        p.wareAfterT[w] = std::max(0.0f, p.wareAfterT[w] - dt);
        if (p.wareT[w] <= 0) continue;
        if (w == W_SALT) p.drunk = std::max(0.0f, p.drunk - 30 * dt / Sec(Wares()[W_SALT].minutes));
        p.wareT[w] -= dt;
        if (p.wareT[w] > 0) continue;
        p.wareT[w] = 0;
        if (w == W_SALT) { p.shakesT = Sec(5); Say(p.name + "'s hands have started to shake."); }
        if (w == W_OIL) { p.wareAfterT[w] = Sec(5); p.honestT = std::max(p.honestT, Sec(5)); Say(p.name + " deflates, and starts telling everyone the truth."); }
        if (w == W_PRESSURE) {
            p.drunk = std::min(100.0f, p.drunk + 20); p.peakDrunk = std::max(p.peakDrunk, p.drunk);
            p.skipT = Sec(2); p.in = Input{}; EndTalk(p); EndFlirt(p); if (p.game.kind >= 0) EndGame(p);
            Say("The pressure lets go of " + p.name + ". The next two minutes happen without them.");
        }
        if (w == W_COCKTAIL) { p.barkeepT = 0; if (goatOwner == p.id) { goatOwner = -1; Say("The goat wanders off into the night, its business done."); if (!EventOn("goat")) goatOn = false; } }
        if (w == W_ANGLER) p.hallucSeed = 0;
    }
    // the skipped two minutes: then something happened
    if (p.skipT > 0) {
        p.skipT -= dt; p.vel = {0, 0}; p.in = Input{};
        if (p.skipT <= 0) {
            p.skipT = 0;
            const auto& L = WD().skipped; int k = (int)(Rand() * 5) % 5;
            std::string what = k < (int)L.size() ? L[k] : "Things happened.";
            if (k == 1) p.faceDrawn = true;
            if (k == 2) p.money = std::max(0.0f, p.money - 30);
            if (k == 3) p.money += 40;
            if (k == 4) for (auto& c : patrons) if (c.inside && !c.gone && c.reg >= 0) { c.mood = std::max(c.mood, 80.0f); break; }
            static const Vector2 SPOT[4] = {{6, 7}, {18, 5.5f}, {34, 4}, {16, 15}};
            p.pos = SPOT[(int)(Rand() * 4) % 4]; Collide(p.pos, 0.32f);
            Note(p, 5, what);
        }
    }
    // Barnacle: anyone who bumps you is a fight (and the police count it as armed)
    p.bumpT = std::max(0.0f, p.bumpT - dt);
    if (p.wareT[W_BARNACLE] > 0 && p.st == State::Active && p.fight.brawl < 0 && p.bumpT <= 0) {
        for (const auto& c : patrons) {
            if (!c.inside || c.gone || c.type == T_STAFF || c.fight.brawl >= 0 || c.fight.Down() || Vector2Distance(c.pos, p.pos) > 0.7f) continue;
            p.bumpT = 3;
            Say(c.name + " bumps " + p.name + ". " + p.name + " swings.");
            int b = StartBrawl(PlayerW(p.id), PatronW(c.id), PlayerW(p.id));
            if (b >= 0 && policeT < 0 && !EventOn("police") && !lockIn) { policeT = 3 * SECONDS_PER_GAME_MINUTE; Say("Someone runs for the police: \"He's armed! With... barnacles?\""); }
            break;
        }
    }
    // Kelp Smoke: the laughter spreads (+3 every 10 s to those near)
    if (p.wareT[W_KELP] > 0 && fmodf(p.wareT[W_KELP], 10) < dt) for (auto& c : patrons) if (c.inside && !c.gone && Vector2Distance(c.pos, p.pos) < 5 && c.name != "The Reverend") c.mood = std::min(100.0f, c.mood + 3);
    // the Siren: whoever is closest makes their move (the thieves too)
    if (p.wareT[W_SIREN] > 0 && p.st == State::Active && p.flirt.patron < 0 && p.talk.patron < 0 && p.game.kind < 0 && (p.sirenT -= dt) <= 0) {
        p.sirenT = 6;
        int best = -1; float bd = 6.0f;
        for (const auto& c : patrons) if (c.inside && !c.gone && c.type != T_STAFF && c.home != "none" && c.talkingTo < 0 && c.playing < 0 && c.fight.brawl < 0) { float dd = Vector2Distance(c.pos, p.pos); if (dd < bd) { bd = dd; best = c.id; } }
        if (best >= 0) { Patron& c = patrons[best]; c.pos = Vector2Add(p.pos, Vector2Scale(Vector2Normalize(Vector2Subtract(c.pos, p.pos)), std::min(bd, 1.6f))); StartFlirt(p, best); if (p.flirt.patron == best) Say(c.name + " crosses the room to " + p.name + "."); }
    }
    // the goat (the Cocktail's fourth): it follows, and bites whoever you're fighting
    if (goatOwner == p.id && goatOn) {
        goatPh += dt;
        Vector2 to = Vector2Subtract(p.pos, goatPos); float L = Vector2Length(to);
        Vector2 want = L > 1.2f ? Vector2Scale(to, 1.6f / L) : Vector2{0, 0};
        goatVel = Vector2Lerp(goatVel, want, std::min(1.0f, dt * 4)); goatPos = Vector2Add(goatPos, Vector2Scale(goatVel, dt)); Collide(goatPos, 0.3f);
        if (Vector2Length(goatVel) > 0.1f) goatYaw = atan2f(goatVel.y, goatVel.x);
        if (p.fight.brawl >= 0 && p.fight.foe.Valid() && Present(p.fight.foe) && fmodf(goatPh, 2.0f) < dt) {
            Combat* F = CombatOf(p.fight.foe); Vector2* fp = PosOf(p.fight.foe);
            if (F && fp && !F->Down() && Vector2Distance(*fp, goatPos) < 3) { F->hp -= 8; F->hitT = 0.3f; goatPos = Vector2Lerp(goatPos, *fp, 0.6f); AddPop(*fp, 2.0f, "The goat bites!", {230, 220, 180, 255}); if (F->hp <= 0) { F->hp = 1; F->stunT = 2; } }
        }
    }
}

// ---------------------------------------------------------------- --night-test's stage 10a checks
int NightWaresChecks() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    auto toMen = [&](Night& n, Player& p) { for (const auto& c : n.patrons) if (c.role == "a large man" && c.inside) { p.pos = Vector2Add(c.pos, {0.8f, 0}); break; } };
    auto setup = [&](Night& n, int players) {
        Opts o; o.seed = 77; o.events = false; o.players = players; n.Init(o);
        n.t = 5 * 60 * SECONDS_PER_GAME_MINUTE;
        int ci = n.ForceEvent("cartel", n.Hour()); for (int i = 0; i < 20 && !n.EventOn("cartel"); i++) n.Step(0.05f);
        (void)ci;
        for (auto& p : n.players) { p.money = 600; for (const auto& c : n.patrons) if (c.role == "a large man" && c.inside) { p.pos = Vector2Add(c.pos, {0.8f, 0}); break; } }
    };
    check(Wares().size() == W_COUNT && Wares()[W_COCKTAIL].price == 100, "the eight wares load from nightoff_wares.json");
    { Night n; setup(n, 1); Player& p = n.players[0];
      check(n.WaresHere(p), "the quiet man's men are selling");
      check(n.BuyWare(p, W_SALT).empty() && p.money == 580, "Sea Salt sold for 20");
      check(!n.BuyWare(p, W_SALT).empty(), "one dose of each a night");
      p.drunk = 60; for (int i = 0; i < (int)(Sec(2) / 0.05f) + 2; i++) n.StepWares(p, 0.05f);
      check(p.drunk < 32 && p.shakesT > 0, TextFormat("salt sobers 30 over two minutes (now %.0f), then the shakes", p.drunk));
      check(n.PlayerAim(p) > AimMul(p.drunk) * 3, "the shakes make every aim hopeless");
      p.drunk = 85; check(!n.BuyWare(p, W_OIL).empty() && p.money == 580, "nothing sold above 80 drunk (the quiet man has standards)"); p.drunk = 20;
      float c0 = n.Charisma(p); n.BuyWare(p, W_OIL); check(n.Charisma(p) > c0 * 1.25f, "Lamp Oil: +30% charisma");
      for (int i = 0; i < (int)(Sec(5) / 0.05f) + 2; i++) n.StepWares(p, 0.05f);
      check(n.Charisma(p) < c0 * 0.75f && p.honestT > 0, "then -30% and the truth");
      float t0 = n.Toughness(p); n.BuyWare(p, W_BARNACLE); check(n.Toughness(p) > t0 * 1.45f, "Barnacle: +50% toughness");
      int bumper = -1; for (const auto& c : n.patrons) if (c.inside && !c.gone && c.type != T_STAFF && c.role.empty()) { bumper = c.id; break; }
      if (bumper >= 0) { n.patrons[bumper].pos = Vector2Add(p.pos, {0.4f, 0}); n.StepWares(p, 0.05f); }
      check(bumper >= 0 && p.fight.brawl >= 0 && n.policeT > 0, "anyone who bumps a Barnacle is a fight, and the police are called (armed)");
    }
    { Night n; setup(n, 1); Player& p = n.players[0];
      int talker = -1; for (const auto& c : n.patrons) if (c.inside && !c.gone && c.type == T_TALKER) { talker = c.id; break; }
      n.BuyWare(p, W_KELP);
      bool allFail = true;
      if (talker >= 0) for (int k = 0; k < 12; k++) { Patron& c = n.patrons[talker]; c.mood = 90; c.talkingTo = -1; p.talk = Talk{}; p.pos = Vector2Add(c.pos, {0.8f, 0}); n.StartTalk(p, talker); p.drunk = 0; n.TalkChoose(p, 1); allFail &= p.talk.losses == 1; n.EndTalk(p); }
      check(talker >= 0 && allFail, "Kelp Smoke: you laugh, and every serious option fails");
      toMen(n, p); n.BuyWare(p, W_ANGLER); check(p.visionsT > 0 && p.hallucSeed != 0, "Angler's Light: visions (the traits and the thieves) and people who aren't there");
      n.BuyWare(p, W_PRESSURE); check(n.PlayerAim(p) < AimMul(p.drunk) * 0.5f, "Deep Pressure: aim is easy");
      float d0 = p.drunk; for (int i = 0; i < (int)(Sec(3) / 0.05f) + 2; i++) n.StepWares(p, 0.05f);
      check(p.skipT > 0 && p.drunk >= d0 + 19, "then the meter climbs 20 and the next two minutes are skipped");
      for (int i = 0; i < (int)(Sec(2) / 0.05f) + 2; i++) n.StepWares(p, 0.05f);
      bool happened = false; for (const auto& m : p.log) happened |= m.text.rfind("Things happened", 0) == 0;
      check(p.skipT <= 0 && happened, "and things happened");
    }
    { Night n; setup(n, 1); Player& p = n.players[0];
      n.BuyWare(p, W_SIREN);
      for (auto& c : n.patrons) if (c.inside && !c.gone && c.type != T_STAFF && c.home != "none" && c.role.empty()) { c.pos = Vector2Add(p.pos, {0, 1.5f}); c.talkingTo = -1; c.playing = -1; break; }
      int delighted = 0, in = 0; for (const auto& c : n.patrons) if (c.inside && !c.gone && c.type != T_STAFF) { in++; delighted += c.mood >= 80; }
      check(in > 0 && delighted == in, "the Siren: the whole room is Delighted with you");
      for (int i = 0; i < 200 && p.flirt.patron < 0; i++) n.StepWares(p, 0.05f);
      check(p.flirt.patron >= 0, "and whoever is closest makes their move");
    }
    { int seen[7] = {}; for (int s = 0; s < 60; s++) { Night n; setup(n, 1); n.rng = 1000 + s * 7919; Player& p = n.players[0]; if (n.BuyWare(p, W_COCKTAIL).empty()) seen[std::clamp(p.cocktail, 0, 6)]++; }
      bool all = true; for (int k = 1; k <= 6; k++) all &= seen[k] > 0;
      check(all, TextFormat("the Cocktail rolls all six (%d %d %d %d %d %d)", seen[1], seen[2], seen[3], seen[4], seen[5], seen[6])); }
    { Night n; setup(n, 1); Player& p = n.players[0]; p.cocktail = 0; n.rng = 1;
      for (int s = 0; s < 400 && p.cocktail != 6; s++) { Night m; setup(m, 1); m.rng = 50 + s * 104729; Player& q = m.players[0]; m.BuyWare(q, W_COCKTAIL); if (q.cocktail == 6) { check(q.kidneys == 1 && q.st == State::PassedOut, "a sixth: the morning, without a kidney"); std::string h = m.Headline(); check(h.find("KIDNEY") != std::string::npos || h.find("COCKTAIL") != std::string::npos, "the paper has it: " + h); p.cocktail = 6; } } }
    { Night n; setup(n, 2); Player& a = n.players[0]; Player& b = n.players[1]; b.pos = Vector2Add(a.pos, {0.8f, 0}); b.drunk = 30;
      check(n.BuyWare(a, W_SIREN, b.id).empty() && b.wareT[W_SIREN] > 0 && !((a.wares >> W_SIREN) & 1), "slip the Siren into a friend's drink");
      bool noted = false; for (const auto& m : a.log) noted |= m.text.find("Slipped") != std::string::npos; check(noted, "and the morning knows who did it"); }
    { Night n; setup(n, 1); Player& p = n.players[0]; int pi = n.ForceEvent("police", n.Hour()); for (int i = 0; i < 20 && !n.EventOn("police"); i++) n.Step(0.05f); (void)pi;
      for (const auto& c : n.patrons) if (c.role == "a large man" && c.inside) { p.pos = Vector2Add(c.pos, {0.8f, 0}); break; }
      float m0 = p.money; n.BuyWare(p, W_OIL);
      check(p.wareT[W_OIL] <= 0 && p.money <= m0 - 30 - 50 + 0.5f, "bought during an inspection: searched, taken and fined"); }
    return fails;
}

} // namespace no
