// The Flight's Long Flight (the two-hour expansion, "The Flight — The Long Flight"): two years in eight seasons, on
// top of the one-hour expansion. This file is its layers in the doc's build order; numbers are in
// data/flight/flight_longflight.json. Headless.
//   1. Generations and succession: the Founder ages (young, prime, old) and dies of age; a marked heir succeeds
//      with a choice (the Old Way, the New Broom, the Pilgrimage); dynasties; elders.
// The calendar (the user's call: the doc's days stretched to fit the colony's growth): a year is the four-season
// match's 24 days, so the Long Flight is 48 days (or 36 for the shorter one); the Founder's life is a year.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fl {

namespace {
struct GenData {
    float primeFrom = 12, oldFrom = 18, life = 24, primeMul = 1.1f, oldPerDay = 0.067f, oldMin = 0.6f, grief = 20, elderDays = 24, mateKin = 0.3f;
    int oldCarry = 3;
    std::vector<std::string> houses;
};
const GenData& GD() {
    static GenData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& g = j["generations"];
    auto F = [&](const char* k, float& v) { if (g[k].IsNum()) v = g[k].F(v); };
    F("prime_from_day", d.primeFrom); F("old_from_day", d.oldFrom); F("life_days", d.life); F("prime_mul", d.primeMul); F("old_per_day", d.oldPerDay); F("old_min", d.oldMin);
    F("grief_fervour", d.grief); F("elder_days", d.elderDays); F("mate_other_kin", d.mateKin);
    if (g["old_carry"].IsNum()) d.oldCarry = g["old_carry"].I(d.oldCarry);
    for (const Json& n : g["houses"].a) d.houses.push_back(n.Str0());
    if (d.houses.empty()) d.houses = {"Longbeak", "Saltwing", "Greycrest", "Stormfeather", "Tidecaller", "Ironclaw"};
    return d;
}
}  // namespace

int YearDays() { return SeasonDays(4); }
const std::vector<std::string>& DynastyNames() { return GD().houses; }
const char* SuccessionName(int c) { static const char* N[3] = {"The Old Way", "The New Broom", "The Pilgrimage"}; return N[std::clamp(c, 0, 2)]; }
const char* SuccessionWhat(int c) {
    static const char* W[3] = {"keep every colony bonus as it is", "take the heir's mother's species (if she was wild-born of another)", "the heir claims a held relic's effect for the colony for good"};
    return W[std::clamp(c, 0, 2)];
}
const Bend& NoBend() { static Bend b; return b; }

int World::Year() const { return LongFlight() && GameDay() > YearDays() ? 2 : 1; }
float World::FounderAge(int side) const { return side < 0 || side > (int)sides.size() ? 0 : (time - ColOf(side).genStart) / DAY; }
int World::AgeStage(int side) const {
    if (!LongFlight()) return 0;
    float a = FounderAge(side);
    return a < GD().primeFrom ? 0 : a < GD().oldFrom ? 1 : 2;
}
void World::Chronicle(int side, int kind, const std::string& text) {
    if (side < 0 || side > (int)sides.size() || seasons <= 0) return;
    Colony& C = ColOf(side);
    ChronLine l; l.day = GameDay(); l.season = Season(); l.year = Year(); l.kind = kind; l.text = text;
    C.chronicle.push_back(l);
    if (C.chronicle.size() > 400) C.chronicle.erase(C.chronicle.begin());
}
std::string World::DynastyOf(int side) const {
    const Colony& C = ColOf(side);
    if (!C.dynasty.empty()) return C.dynasty;
    const auto& H = DynastyNames();
    return "the House of " + H[(size_t)(C.dynastyPick >= 0 ? C.dynastyPick : 0) % H.size()];
}
bool World::MarkHeir(int id) {
    if (!LongFlight()) return false;
    Bird* b = FindBird(cur, id);
    if (!b || (b->stage != BStage::Chick && b->stage != BStage::Adult) || b->elder) { Say("Only a chick or a young adult can be the heir."); return false; }
    col.heirId = id; Say("The heir is marked: a crest on its head.");
    return true;
}
bool World::MarkNextHeir() {
    // cycle through the colony's chicks (then its youngest adults) after the one marked now
    std::vector<int> c;
    for (const auto& b : col.birds) if (b.alive && b.stage == BStage::Chick) c.push_back(b.id);
    for (const auto& b : col.birds) if (b.alive && b.stage == BStage::Adult && b.vet < 0 && !b.elder && b.flock < 0 && b.age < GD().primeFrom * DAY) c.push_back(b.id);
    if (c.empty()) { Say("No chick to mark as the heir yet."); return false; }
    size_t k = 0; for (size_t i = 0; i < c.size(); i++) if (c[i] == col.heirId) k = i + 1;
    return MarkHeir(c[k % c.size()]);
}
void World::Succeed(int s) {
    const GenData& D = GD();
    Colony& C = ColOf(s);
    Founder& F = FounderOf(s);
    std::string old = Founders()[F.def].name, house = DynastyOf(s);
    Chronicle(s, CK_DEATH, TextFormat("%s, Founder of %s, died of age in its %s year, after %d deaths and returns, %d fights won by its flocks and %d birds raised.",
                                      old.c_str(), house.c_str(), C.gen == 0 ? "first" : "second", F.deaths, C.kills, C.caughtToday + (int)C.birds.size()));
    Bird* h = C.heirId >= 0 ? FindBird(s, C.heirId) : nullptr;
    if (h && h->stage != BStage::Chick && h->stage != BStage::Adult) h = nullptr;
    int keep = C.keepPerk >= 0 && ((F.perks >> C.keepPerk) & 1) ? C.keepPerk : -1;
    if (keep < 0) for (int i = 0; i < 32; i++) if ((F.perks >> i) & 1) { keep = i; break; }
    C.gen++; C.genStart = time; C.successionT = time;
    F.perks = keep >= 0 ? 1u << keep : 0; F.perkOffer[0] = F.perkOffer[1] = F.perkOffer[2] = -1;
    F.hunger = 1; F.hp = Founders()[F.def].hp; F.stamina = Founders()[F.def].stamina; F.respawnT = 0; F.ageSpeed = F.ageAttack = 1; F.old = false;
    if (F.st == FState::Dead || F.st == FState::Under || F.st == FState::Fainted) F.st = FState::Perched;
    std::string heirWhat;
    if (h) {
        F.pos = Vector3Add(h->pos, {0, 0.4f, 0}); F.chick = h->stage == BStage::Chick; F.chickFish = 0;
        C.heirTrait = h->trait;
        if (C.succChoice == 1 && h->kin >= 0 && h->kin != F.def) { int was = F.def; F.def = h->kin; heirWhat = std::string(" The New Broom: the colony is half ") + Founders()[F.def].name + " now, not " + Founders()[was].name + "."; }
        h->alive = false; h->cause = "became the Founder";
        C.regent = false;
    } else {
        // no heir: a regent (the oldest veteran, else the oldest adult) holds the colony, with no founder bonus, until a chick is grown
        Bird* r = nullptr;
        for (auto& b : C.birds) if (b.alive && b.stage == BStage::Adult && (!r || (b.vet >= 0) > (r->vet >= 0) || ((b.vet >= 0) == (r->vet >= 0) && b.age > r->age))) r = &b;
        if (r) { F.pos = Vector3Add(r->pos, {0, 0.4f, 0}); F.chick = false; r->alive = false; r->cause = "became the regent"; C.regent = true; heirWhat = " No heir: a regent holds the colony until a chick is grown."; }
        else { F.chick = true; C.regent = false; heirWhat = " No heir and no regent: a foundling chick leads what is left."; }
        C.heirTrait = -1;
    }
    if (C.succChoice == 2) {   // (the Pilgrimage: a relic's effect claimed for good)
        for (int r = 0; r < RL_COUNT; r++) if (((C.relics >> r) & 1) && !((C.relicsKept >> r) & 1)) { C.relicsKept |= 1u << r; heirWhat += " The Pilgrimage: " + Relics()[r].name + " is the colony's for good."; break; }
    }
    C.heirId = -1;
    // a succession day: grief, the warriors come home, and everyone knows
    C.fervour = std::max(0.0f, C.fervour - D.grief);
    for (auto& fl : C.flocks) if (fl.loanTo < 0) OrderFlock(s, fl.id, Target::Home, -1, -1, -1, -1, {});
    if (C.dynasty.empty()) C.dynasty = "the House of " + DynastyNames()[(size_t)std::max(0, C.dynastyPick) % DynastyNames().size()];
    Chronicle(s, CK_SUCCESSION, TextFormat("The heir succeeded: %s's %s generation begins (%s).%s", C.dynasty.c_str(), C.gen == 1 ? "second" : "third", SuccessionName(C.succChoice), heirWhat.c_str()));
    for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, o == s ? "Your Founder has died of age. The heir takes the colony: a day of grief (fervour -20, the warriors come home)." + heirWhat
                                                                 : SideName(s) + "'s Founder has died: it is their succession day (the day to raid them).");
}
void World::StepGenerations(float dt) {
    if (!LongFlight()) return;
    const GenData& D = GD();
    int N = (int)sides.size() + 1;
    for (int s = 0; s < N; s++) {
        Colony& C = ColOf(s); Founder& F = FounderOf(s);
        float age = (time - C.genStart) / DAY;
        // aging: young, then prime (+10% to everything), then old (slower and shorter of breath a day at a time; it can't lift the biggest fish)
        const MateTraitDef* T = C.heirTrait >= 0 && C.heirTrait < MT_COUNT ? &MateTraits()[C.heirTrait] : nullptr;
        float sp = age < D.primeFrom ? 1.0f : age < D.oldFrom ? D.primeMul : std::max(D.oldMin, 1 - D.oldPerDay * (age - D.oldFrom + 1));
        F.ageSpeed = sp * (T ? T->speed : 1.0f);
        F.ageAttack = (age >= D.primeFrom && age < D.oldFrom ? D.primeMul : 1.0f) * (T ? T->attack : 1.0f);
        F.old = age >= D.oldFrom;
        F.oldCarry = F.old ? D.oldCarry : 0;
        // the heir: the first chick of the generation is marked (the player can mark another)
        Bird* h = C.heirId >= 0 ? FindBird(s, C.heirId) : nullptr;
        if (!h || (h->stage != BStage::Chick && h->stage != BStage::Adult)) {
            C.heirId = -1;
            for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Chick) { C.heirId = b.id; break; }
            if (C.heirId >= 0 && h == nullptr && C.heirAnnounced != C.gen * 1000 + C.heirId) { C.heirAnnounced = C.gen * 1000 + C.heirId; SayTo(s, "An heir is marked: the first chick of the generation wears a crest (re-mark it on the long-match page)."); }
        }
        // a regent's colony takes the first chick that grows up as its Founder
        if (C.regent && h && h->stage == BStage::Adult) { C.regent = false; F.pos = Vector3Add(h->pos, {0, 0.4f, 0}); F.chick = false; C.heirTrait = h->trait; h->alive = false; h->cause = "became the Founder"; C.heirId = -1; C.genStart = time; SayTo(s, "The heir is grown: the regency ends."); Chronicle(s, CK_SUCCESSION, "The regency ended: the heir came of age."); }
        // elders: veterans that survive a year stop fighting and start teaching
        for (auto& b : C.birds) if (b.alive && b.stage == BStage::Adult && b.vet >= 0 && !b.elder && b.vetT > -1e8f && time - b.vetT >= D.elderDays * DAY) {
            b.elder = true; b.flock = -1;
            std::string nm = b.vetName >= 0 && b.vetName < (int)Veterans().names.size() ? Veterans().names[b.vetName] : std::string("a veteran");
            SayTo(s, nm + " has become an elder: it fights no more, and teaches.");
            Chronicle(s, CK_ELDER, nm + " became an elder of the colony.");
        }
        // death of age (at the dawn after its last day), then the heir
        if (age >= D.life && !over && time > DAY) Succeed(s);
    }
    (void)dt;
}
int World::Elders(int side, int trait) const { int n = 0; for (const auto& b : ColOf(side).birds) n += b.alive && b.elder && (trait < 0 || b.vet == trait); return n; }

// ---------------------------------------------------------------- --flight-longflight-test
int RunFlightLongFlightTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, the Long Flight (the two-hour expansion)\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    auto make = [](int seasons, uint32_t seed = 41) { auto w = std::make_unique<World>(); MapOpts o; o.players = 2; o.seasons = seasons; w->Init("taloned", seed, o); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f; return w; };
    auto adult = [](World& w, Role r, Vector3 at) -> Bird& { Bird b; b.id = w.col.nextId++; b.stage = BStage::Adult; b.role = r; b.hp = 60; b.hunger = 1; b.pos = at; w.col.birds.push_back(b); return w.col.birds.back(); };
    // ---- the calendar: two years of four seasons
    {
        auto w = make(8);
        bool len = w->LongFlight() && w->matchLen == 48 * World::DAY && SeasonDays(6) == 36;
        int got[4]; int k = 0; for (int d : {3, 27, 33, 46}) { w->time = (d - 0.5f) * World::DAY; got[k++] = w->Season(); }
        w->time = 30 * World::DAY; int y2 = w->Year(); w->time = 10 * World::DAY; int y1 = w->Year();
        check(len && got[0] == SEASON_SPRING && got[1] == SEASON_SPRING && got[2] == SEASON_SUMMER && got[3] == SEASON_WINTER && y1 == 1 && y2 == 2,
              "the Long Flight is two years of 24 days (48 in all; 36 for the short one), the seasons coming round again in year two");
        bool twice = true; for (int e = 0; e < EV_SEASON_COUNT; e++) twice &= w->eventDay[e] < 0 || (w->eventDay2[e] > 24 && w->eventDay2[e] <= 48);
        check(twice, "each season's event comes again in year two");
        auto s = make(4); check(!s->LongFlight(), "a four-season match isn't the Long Flight");
    }
    // ---- aging
    {
        auto w = make(8);
        auto at = [&](float d) { w->time = d * World::DAY; w->StepGenerations(0.1f); };
        at(5); float young = w->me.ageSpeed;
        at(14); float prime = w->me.ageSpeed, primeAtk = w->me.ageAttack;
        at(22); float old = w->me.ageSpeed; int carry = w->me.Carry(w->Def());
        check(young == 1 && prime > 1.05f && primeAtk > 1.05f && old < 0.9f && w->me.old && carry <= 3,
              TextFormat("the Founder is young, then in its prime (x%.2f), then old (x%.2f, carrying at most %d)", prime, old, carry));
    }
    // ---- the heir and the succession (the Pilgrimage keeps a relic for good)
    {
        auto w = make(8);
        Bird ch; ch.id = w->col.nextId++; ch.stage = BStage::Chick; ch.nest = 0; ch.pos = w->col.nests[0].pos; ch.trait = MT_QUICK; ch.hunger = 1; w->col.birds.push_back(ch); int hid = ch.id;
        w->time = 3 * World::DAY; w->StepGenerations(0.1f);
        check(w->col.heirId == hid, "the first chick of the generation is marked as the heir");
        w->col.relics = 1u << RL_BELL; w->col.succChoice = 2; w->col.fervour = 50; w->me.perks = 0b110; w->col.keepPerk = 2;
        w->time = 24.05f * World::DAY; w->StepGenerations(0.1f);
        bool heirGone = true; for (const auto& b : w->col.birds) if (b.id == hid) heirGone = !b.alive;
        check(w->col.gen == 1 && heirGone && w->me.chick && w->col.heirTrait == MT_QUICK && w->me.perks == 0b100 && w->col.fervour <= 30.1f,
              "at the end of its year the Founder dies of age: the heir (a chick yet) takes the colony, keeps one perk, inherits its mother's trait; a day of grief");
        check((w->col.relicsKept >> RL_BELL) & 1 && (w->col.relics = 0, w->HasRelic(0, RL_BELL)), "the Pilgrimage: the relic's effect is the colony's for good, even when the relic is gone");
        bool obit = false; for (const auto& l : w->col.chronicle) obit |= l.kind == CK_DEATH;
        check(obit && !w->col.dynasty.empty(), "the Chronicle writes the obituary; the dynasty is named (" + w->col.dynasty + ")");
        w->time = 25 * World::DAY; w->StepGenerations(0.1f);
        check(w->AgeStage(0) == 0 && w->me.ageSpeed > 1.0f, "the heir starts young (and its Quick mother's trait is its for good: faster)");
    }
    // ---- no heir: a regent, with no founder bonus
    {
        auto w = make(8);
        for (auto& b : w->col.birds) if (b.stage == BStage::Chick || b.stage == BStage::Egg) b.alive = false;
        Bird& v = adult(*w, Role::Striker, w->col.caches[0].pos); v.vet = VT_LUCKY; int vid = v.id;
        w->time = 24.05f * World::DAY; w->StepGenerations(0.1f);
        bool gone = true; for (const auto& b : w->col.birds) if (b.id == vid) gone = !b.alive;
        check(w->col.regent && gone && &w->BendNow() == &NoBend(), "no heir: the oldest veteran rules as a regent, without the founder's bonus");
    }
    // ---- the New Broom: the heir's mother was of another species
    {
        auto w = make(8);
        int other = (w->me.def + 3) % (int)Founders().size();
        Bird ch; ch.id = w->col.nextId++; ch.stage = BStage::Chick; ch.nest = 0; ch.pos = w->col.nests[0].pos; ch.kin = other; ch.hunger = 1; w->col.birds.push_back(ch);
        w->col.succChoice = 1; w->time = 3 * World::DAY; w->StepGenerations(0.1f);
        w->time = 24.05f * World::DAY; w->StepGenerations(0.1f);
        check(w->me.def == other, std::string("the New Broom: the colony takes the heir's mother's species (") + Founders()[other].name + ")");
    }
    // ---- elders
    {
        auto w = make(8);
        Bird& v = adult(*w, Role::Striker, w->col.caches[0].pos); v.vet = VT_KEEN; v.vetT = 0; int vid = v.id;
        w->time = 24.5f * World::DAY;
        for (auto& b : w->col.birds) if (b.stage == BStage::Chick) b.alive = false;
        w->col.genStart = 10 * World::DAY;   // (keep the Founder alive through this)
        w->StepGenerations(0.1f);
        bool el = false; for (const auto& b : w->col.birds) if (b.id == vid) el = b.elder;
        check(el && w->Elders(0, VT_KEEN) == 1, "a veteran that survives a year becomes an elder (a Keen elder makes the scouts exact)");
    }
    printf(fails ? "flight-longflight-test: %d check(s) failed\n" : "flight-longflight-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl