// Mini-bosses, boss lures and charms (design doc v2, "Mini-bosses, boss lures, and harbour requests", "Charms").
// Every ground has two mini-bosses that only a boss lure calls up, over boss water (the ground's skiff-only marks);
// casting one adds 10 to the ground's Wake. A mini-boss is worth a flat value (times the Killscore and cooking, like
// any catch) and leaves a drop the harbour folk want: Mother Carey trades it for a legend lure, or it's worn as a
// charm. Each hand wears one charm on a cord, a small passive effect for the whole run, lost with a body lost at sea.
// The harbour requests themselves are the Session's (trawl_session.cpp).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

const std::vector<MiniBossDef>& MiniBosses() {
    // the Lagoon's (doc v2, page 47): both live in the Crest Pass's coral, so both come to a boss lure there
    static const std::vector<MiniBossDef> B = {
        {"Old Snapjaw", 30, 180, DB_BITER, Pattern::Cover, Pattern::Run, "a jaw full of old hooks", 60, "The Crest Pass"},       // a giant moray: retreats into the crest's holes on the line
        {"The Crest Grouper", 70, 240, DB_THRASHER, Pattern::Dive, Pattern::Cover, "a barnacled brass lure", 60, "The Crest Pass"},   // sounds into the caves
        // the Weeds' (doc v2, page 47-48; boss lures 120)
        {"The Kelp King", 250, 600, DB_THRASHER, Pattern::Dive, Pattern::Cover, "a kelp crown", 120, "The Inner Lanes"},     // a giant sea bass: dives into the canopy
        {"Gold Tail", 30, 400, DB_FLOPPER, Pattern::Run, Pattern::Jump, "a golden scale", 120, "The Seaward Rocks"},          // the yellowtail school's leader: jumps every 4 s
    };
    return B;
}
int MiniBossOf(const std::string& name) { const auto& B = MiniBosses(); for (int i = 0; i < (int)B.size(); i++) if (name.rfind(B[i].name, 0) == 0) return i; return -1; }

const char* CharmName(int c) {
    static const char* N[CH_COUNT] = {"(none)", "Lucky coin", "Shark tooth", "Tribal anklet", "Old hooks", "Brass lure", "Kelp crown", "Golden scale"};
    return N[std::clamp(c, 0, CH_COUNT - 1)];
}
const char* CharmEffect(int c) {
    static const char* E[CH_COUNT] = {"", "Glimmer variants twice as likely", "+0.1 Killscore on melee finishes", "+8 s before drowning",
                                      "The wearer's line never breaks on a fish's first run", "Boss lures cost the crew half",
                                      "The wearer is never entangled by kelp or Wraiths", "The wearer's Airborne bonus rises to 1.45"};
    return E[std::clamp(c, 0, CH_COUNT - 1)];
}
int CharmOfDrop(const std::string& d) {
    if (d == "a jaw full of old hooks") return CH_OLD_HOOKS;
    if (d == "a barnacled brass lure") return CH_BRASS_LURE;
    if (d == "a kelp crown") return CH_KELP_CROWN;
    if (d == "a golden scale") return CH_GOLDEN_SCALE;
    return CH_NONE;
}

bool Gannet::ArmBossLure(int ci) {
    if (bossArmed) { bossArmed = false; Say("The boss lure goes back in its box"); return true; }
    if (bossLures <= 0) { Say("No boss lure aboard (the Chandler sells them)"); return false; }
    if (skiffRod.state != RodState::Idle) { Say("Reel in first to change the lure"); return false; }
    bossArmed = true;
    // a boss needs a rod that can hold it: the heaviest the Gannet owns goes in the skiff with the lure
    for (Tackle t : {Tackle::Heavy, Tackle::Medium, Tackle::Light}) if (owned[(int)t]) { skiffRod.tackle = t; break; }
    skiffRod.fight.drag = 0.33f * TackleOf(skiffRod.tackle).strength;
    Say(TextFormat("A boss lure on the %s: cast it over boss water (the Crest Pass)", TackleOf(skiffRod.tackle).name));
    (void)ci;
    return true;
}

// called when the skiff's line is cast with a boss lure armed (StepSkiffRod)
void BossCast(Gannet& g) {
    Rod& r = g.skiffRod;
    int mk = g.eco ? g.eco->MarkAt({r.lure.x, r.lure.y}) : -1;
    bool boss = false; if (mk >= 0) for (const auto& b : MiniBosses()) if (g.eco->marks[mk].name == b.mark) boss = true;
    if (!boss) { g.Say("Nothing answers: a boss lure only works over boss water (a mark's slow rings on the sonar)"); return; }
    // the mark's mini-boss: whichever hasn't been taken this deadline (Old Snapjaw first)
    int idx = -1;
    for (int i = 0; i < (int)MiniBosses().size(); i++) if (!(g.bossCaught & (1 << i)) && g.eco->marks[mk].name == MiniBosses()[i].mark) { idx = i; break; }
    g.bossLures--; g.bossArmed = false;
    if (g.eco) g.eco->wake += 10;   // (a boss fight is also an invitation to the ground's apex)
    if (idx < 0) { g.Say("The boss lure works the water, but this mark's mini-boss is taken this deadline"); return; }
    g.bossBiteIdx = idx; g.bossBiteT = 6 + (float)((r.rng >> 8) % 600) / 100.0f;
    g.Say("The boss lure sinks into the coral. The water goes very still...");
}

void Gannet::StepBoss(float dt) {
    Rod& r = skiffRod;
    if (bossBiteT < 0) return;
    if (r.state != RodState::Out || !skiff.Up()) { bossBiteT = -1; bossBiteIdx = -1; Say("Whatever was coming to the boss lure turns back into the coral"); return; }
    bossBiteT -= dt;
    if (bossBiteT > 0 || r.bite.stage != BiteStage::None) return;
    const MiniBossDef& b = MiniBosses()[bossBiteIdx];
    FishSpec f{};
    f.name = b.name; f.kg = b.kg; f.a = b.a; f.b = b.b; f.wary = false; f.teeth = b.deck == DB_BITER;
    f.depth = std::max(1.0f, r.lure.z); f.floor = eco ? std::max(1.0f, eco->DepthAt({r.lure.x, r.lure.y})) : 3;
    f.price = b.value / b.kg; f.pullK = 1.25f; f.staminaK = 1.6f; f.softMouth = 0.7f;
    r.biteSpec = f; r.fishSp = -1;
    bool angler = false; for (const auto& c : crew) if (c.deck == DECK_SKIFF && c.skiffLine && c.role == Role::Angler) angler = true;
    r.bite.Start(&r.biteSpec, false, angler, r.rng++);
    Say(TextFormat("Something enormous takes the boss lure: %s!", b.name));
    bossBiteT = -1;
}

void Gannet::OnLanded(CatchRec& rec, int holder) {
    int bi = MiniBossOf(rec.name);
    if (bi >= 0) {
        const MiniBossDef& b = MiniBosses()[bi];
        rec.boss = bi; rec.price = b.value / std::max(1.0f, rec.kg); rec.deckKind = b.deck;
        bossCaught |= 1 << bi;
        drops.push_back(b.drop);
        Say(TextFormat("%s is taken! It leaves %s (harbour folk want it)", b.name, b.drop));
        return;
    }
    // a Glimmer variant: rare, shimmering, three times the value (the lucky coin doubles the chance)
    uint32_t h = (uint32_t)(time * 1000) * 2654435761u + (uint32_t)hold.size() * 40503u + (uint32_t)(rec.kg * 977);
    float roll = ((h >> 8) & 0xFFFF) / 65535.0f;
    if (roll < GLIMMER_CHANCE * (WearsCharm(holder, CH_LUCKY_COIN) ? 2 : 1)) { rec.glimmer = true; Say(TextFormat("A Glimmer %s! It shimmers like oil on water (worth three times as much)", rec.name.c_str())); }
}


// ---------------------------------------------------------------- depth.exe --trawl-quest-test
int RunTrawlQuestTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: mini-bosses, charms and harbour requests\n");
    const float dt = 1 / 60.0f;
    // a boss lure over the Crest Pass calls Old Snapjaw
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 41); s.plainNights = true;
        g.moored = false; s.money = 500; g.eco = &e; e.StartNight();
        check(s.Buy("bosslure") && g.bossLures == 1 && s.money == 440, "the Chandler sells a boss lure for 60");
        g.skiff.state = SkiffState::Afloat; g.skiff.integrity = D().skiffIntegrity;
        g.skiff.p = Vector2Add(e.marks[0].at, {-8, 0}); g.skiff.heading = 0;
        g.crew[0].deck = DECK_SKIFF; g.crew[0].p = {0.2f, 0}; g.crew[0].skiffLine = true;
        g.owned[(int)Tackle::Heavy] = true;
        check(g.ArmBossLure(0) && g.bossArmed && g.skiffRod.tackle == Tackle::Heavy, "R on the skiff's line: a boss lure on the heaviest rod aboard");
        float w0 = e.wake;
        Rod& r = g.skiffRod;
        Vector2 tip = g.SkiffRodTip(); r.aim = Vector2Normalize(Vector2Subtract(e.marks[0].at, tip));
        r.castHeld = true; for (int i = 0; i < 30; i++) g.Step(dt);
        r.castHeld = false; g.Step(dt);
        bool inWater = e.MarkAt({r.lure.x, r.lure.y}) == 0;
        check(inWater && g.bossLures == 0 && !g.bossArmed && e.wake >= w0 + 9.9f, TextFormat("cast into the Crest Pass: the lure is spent and the Wake rises by 10 (%.1f -> %.1f)", w0, e.wake));
        bool bit = false;
        for (int i = 0; i < 60 * 20 && !bit; i++) { g.Step(dt); bit = r.bite.stage != BiteStage::None && std::string(r.biteSpec.name) == "Old Snapjaw"; }
        check(bit, "Old Snapjaw comes to it out of the coral");
        r.fight = Fight{}; r.fight.tackle = r.tackle; r.fight.HookFish(r.biteSpec, r.lure, 5); r.state = RodState::Fighting; r.fight.end = FightEnd::Landed;
        g.Step(dt);
        bool towed = !g.towed.empty() && g.towed.back().boss == 0;
        check(towed && !g.drops.empty() && g.drops[0] == "a jaw full of old hooks" && (g.bossCaught & 1), "landed (30 kg: on the tow line), he leaves a jaw full of old hooks");
        if (towed) { CatchRec b = g.towed.back(); b.killScore = 1.5f; check(fabsf(s.Value(b) - 180 * 1.5f) < 0.5f, TextFormat("worth his flat 180 times the Killscore (%.0f at x1.5)", s.Value(b))); }
        // outside boss water nothing comes, and the lure isn't spent
        g.bossLures = 1; g.skiffRod.state = RodState::Idle; g.skiff.p = Vector2Add(g.boat.pos, {-60, 0}); g.ArmBossLure(0);
        r.aim = {1, 0}; r.castHeld = true; for (int i = 0; i < 30; i++) g.Step(dt); r.castHeld = false; g.Step(dt);
        check(g.bossLures == 1 && g.bossArmed && g.bossBiteT < 0, "cast outside boss water: nothing answers, the lure stays on");
    }
    // charms
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 2, 42); s.money = 500;
        g.crew[1].charm = CH_BRASS_LURE;
        float m0 = s.money; s.Buy("bosslure");
        check(fabsf(m0 - s.money - 30) < 0.01f, "the brass lure charm: boss lures cost the crew half (30)");
        check(s.Buy("coin", nullptr, 0) && g.crew[0].charm == CH_LUCKY_COIN, "the lucky coin goes round the buyer's neck");
        int with = 0, without = 0;
        for (int k = 0; k < 20000; k++) {
            g.time = k * 0.37f;
            CatchRec a; a.name = "snapper"; a.kg = 2 + (k % 7) * 0.3f; g.OnLanded(a, 0); if (a.glimmer) with++;
            CatchRec b = a; b.glimmer = false; g.OnLanded(b, 1); if (b.glimmer) without++;
        }
        check(without > 250 && without < 550 && with > without * 1.6f, TextFormat("Glimmer variants: %.1f%% of fish, %.1f%% with a lucky coin", without / 200.0f, with / 200.0f));
        CatchRec gl; gl.name = "snapper"; gl.kg = 2; gl.price = 3; gl.glimmer = true; CatchRec pl = gl; pl.glimmer = false;
        check(fabsf(s.Value(gl) - 3 * s.Value(pl)) < 0.01f, "a Glimmer sells at three times");
        // the shark tooth: +0.1 on a melee finish
        g.crew[0].charm = CH_SHARK_TOOTH;
        CatchRec f; f.name = "snapper"; f.kg = 3; f.price = 3; f.deckAt = {-2, 0.5f}; g.hold = {f};
        g.HitDeckFish(0, 999, 0, KH_MELEE, false, 0);
        float withTooth = g.hold[0].killScore;
        g.crew[0].charm = CH_NONE; g.hold = {f}; g.HitDeckFish(0, 999, 0, KH_MELEE, false, 0);
        check(fabsf(withTooth - g.hold[0].killScore - 0.1f) < 0.01f, TextFormat("the shark tooth: +0.1 Killscore on a melee finish (x%.2f against x%.2f)", withTooth, g.hold[0].killScore));
        // the anklet: +8 s before drowning; a body lost at sea takes its charm with it
        g.crew[1].charm = CH_ANKLET; g.crew[1].p = {-2, 2};
        g.GoOverboard(1, "test");
        check(fabsf(g.crew[1].drownT - 33) < 0.01f, TextFormat("the tribal anklet: %.0f s before drowning (25 + 8)", g.crew[1].drownT));
        g.Kill(1, "drowned", true);
        check(g.crew[1].charm == CH_NONE, "a body lost at sea takes its charm with it");
    }
    // the old hooks: the line never breaks on the first run
    {
        Fight f; f.tackle = Tackle::Light; f.HookFish(*FindDummyFish("yellowfin"), {20, 0, 10}, 3); f.drag = 1.1f * TackleOf(Tackle::Light).strength; f.noSnapUntil = 15;
        for (int i = 0; i < 60 * 14 && f.end == FightEnd::None; i++) { f.reeling = true; f.Step(dt); }
        check(f.end != FightEnd::Snapped, "the old hooks: a yellowfin on a screwed-down light rod doesn't snap it in the first 15 s");
    }
    // harbour requests
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 43);
        check(s.requests.size() == 5 && !s.requests[0].species.empty(), TextFormat("five requests chalked up (the cook wants a %s)", s.requests[0].species.c_str()));
        std::string why;
        check(!s.RequestReady(0, 0, &why), "the cook's isn't ready with nothing cooked");
        CatchRec c; c.name = s.requests[0].species; c.kg = 2; c.price = 3; c.cooked = true; c.cook = 1.5f; c.gutted = c.iced = true; g.hold = {c};
        float m0 = s.money, v = s.Value(c);
        check(s.FillRequest(0, 0) && fabsf(s.money - m0 - 3 * v) < 0.01f && g.spiceRub && g.hold.empty(), TextFormat("the cook takes it cooked to 1.5x: 3x its value (%.0f) and the spice rub", 3 * v));
        g.tagGun = true; g.tagged = 1;
        check(s.FillRequest(0, 1) && g.rareLures == 1, "the naturalist: a tagged turtle earns a rare-fish lure");
        CatchRec gl; gl.name = s.requests[2].species; gl.kg = 2; gl.glimmer = true; g.hold = {gl};
        int t0 = s.tokens; m0 = s.money;
        check(s.FillRequest(0, 2) && s.tokens == t0 + 3 && s.money == m0 + 100, "the collector: a Glimmer for three tokens and 100");
        g.highKills = 3;
        check(s.FillRequest(0, 3) && s.freeAttach == 1, "the gunsmith's apprentice: three 2.5x kills for a free attachment");
        g.drops = {"a jaw full of old hooks", "a barnacled brass lure"};
        check(s.FillRequest(0, 4) && g.legendLures == 1 && g.drops.size() == 1, "Mother Carey takes a mini-boss drop for a legend lure");
        check(!s.FillRequest(0, 4), "one each a deadline");
        check(s.WearDrop(0, 0) && g.crew[0].charm == CH_BRASS_LURE && g.drops.empty(), "the other drop worn on a cord (the brass lure)");
        s.BeginDeadline();
        check(!s.requests[0].done && !g.spiceRub, "a new deadline: fresh requests, the spice rub used up");
    }
    printf(fails ? "trawl-quest-test: %d FAILED\n" : "trawl-quest-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
