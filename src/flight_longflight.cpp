// The Flight's Long Flight (the two-hour expansion, "The Flight — The Long Flight"): two years in eight seasons, on
// top of the one-hour expansion. This file is its layers in the doc's build order; numbers are in
// data/flight/flight_longflight.json. Headless.
//   1. Generations and succession: the Founder ages (young, prime, old) and dies of age; a marked heir succeeds
//      with a choice (the Old Way, the New Broom, the Pilgrimage); dynasties; elders.
//   2. Evolution: twelve traits in ranks I-III that compound when both parents share them; mutations and rare
//      births; a species at 20 birds with one trait at III (a second power at 40).
//   3. The Far Sea: a ring of new islands beyond a fog that lifts at the end of year one (the Archipelago of Thorns,
//      the Drowned Fleet, the Roc's Peak, the Mirror Lagoon, the Ice Shelf, the Sunken City, two ports and the navy's
//      frigate), and the Storm Wall beyond it.
//   4. Grand Projects: nine wonders, one per map, consecrated by the Founder and raised by builders over a season;
//      each changes the map for everyone; raised to a second tier in year two.
//   5. Leagues (shared sight, truce, the score shared), the Council (a vote at dawn every six days from Summer of
//      year two: Peace, the Hunt, Embargo, Sanctuary, Tithe, the Great War), oaths and the Great War.
//   6. Trade empires: six wares (salt fish, lamp oil, spices, iron, cloth, feathers), the Bird Exchange (the Market
//      Hall's fee and embargo), chartered routes flown by Traders (convoys, pirates, escorts), market events.
//   7. Culture: the Chronicle (a chapter a season; exported at the end), titles worth score, and the Drummers' songs.
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

// ---------------------------------------------------------------- 2. evolution (doc pp. 5-6): traits that compound across generations
namespace {
struct EvoData { std::vector<GeneDef> genes; int speciesAt = 20, secondAt = 40; float mutate = 0.05f, rare = 0.01f, giantHunger = 3, giantMul = 2, crestedFervour = 5; std::vector<std::string> speciesWords; };
const EvoData& ED() {
    static EvoData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& e = j["evolution"];
    d.speciesAt = e["species_at"].I(d.speciesAt); d.secondAt = e["second_at"].I(d.secondAt); d.mutate = e["mutate"].F(d.mutate); d.rare = e["rare"].F(d.rare);
    d.giantHunger = e["giant_hunger"].F(d.giantHunger); d.giantMul = e["giant_mul"].F(d.giantMul); d.crestedFervour = e["crested_fervour"].F(d.crestedFervour);
    for (const Json& g : e["traits"].a) {
        GeneDef x; x.key = g["key"].Str0(); x.name = g["name"].Str0(x.key); x.effect = g["effect"].Str0(); x.look = g["look"].Str0(); x.power = g["power"].Str0(); x.species = g["species"].Str0(x.name);
        x.speed = g["speed"].F(0); x.attack = g["attack"].F(0); x.hpMul = g["hp_mul"].F(0); x.hpAdd = g["hp_add"].F(0); x.scout = g["scout"].F(0); x.stamina = g["stamina"].F(0);
        x.splash = g["splash"].F(0); x.dmgTaken = g["damage_taken"].F(0); x.deep = g["deep"].F(0); x.lucky = g["lucky"].F(0); x.clever = g["clever"].F(0); x.clutch = g["clutch"].F(0);
        d.genes.push_back(x);
    }
    while ((int)d.genes.size() < GT_COUNT) { GeneDef x; x.key = x.name = "trait"; d.genes.push_back(x); }
    return d;
}
}  // namespace
const std::vector<GeneDef>& Genes() { return ED().genes; }
int GeneCount(uint32_t g) { int n = 0; for (int t = 0; t < GT_COUNT; t++) n += GeneRank(g, t) > 0; return n; }
std::string GeneText(uint32_t g) {
    static const char* R[4] = {"", " I", " II", " III"};
    std::string s; for (int t = 0; t < GT_COUNT; t++) if (int r = GeneRank(g, t)) { if (!s.empty()) s += ", "; s += Genes()[t].name + R[r]; }
    return s.empty() ? std::string("no traits") : s;
}
uint32_t Inherit(uint32_t mother, uint32_t father, bool hybrid, float u1, float u2, float u3) {
    // where both parents share a trait the chick's rank is one higher (III at most; a hybrid line doesn't rank up for a
    // generation); where they differ it gets one from each, up to three traits; now and then a mutation, a new trait at I
    uint32_t c = 0;
    for (int t = 0; t < GT_COUNT; t++) { int a = GeneRank(mother, t), b = GeneRank(father, t); if (a && b) c = SetGene(c, t, hybrid ? std::max(a, b) : std::min(3, std::max(a, b) + 1)); }
    std::vector<int> fromM, fromF;
    for (int t = 0; t < GT_COUNT; t++) { if (GeneRank(mother, t) && !GeneRank(father, t)) fromM.push_back(t); if (GeneRank(father, t) && !GeneRank(mother, t)) fromF.push_back(t); }
    if (!fromM.empty() && GeneCount(c) < 3) { int t = fromM[(size_t)(u1 * fromM.size()) % fromM.size()]; c = SetGene(c, t, GeneRank(mother, t)); }
    if (!fromF.empty() && GeneCount(c) < 3) { int t = fromF[(size_t)(u2 * fromF.size()) % fromF.size()]; c = SetGene(c, t, GeneRank(father, t)); }
    if (u3 < ED().mutate && GeneCount(c) < 3) { int t = (int)(u3 / ED().mutate * GT_COUNT) % GT_COUNT; if (!GeneRank(c, t)) c = SetGene(c, t, 1); }
    return c;
}
GeneFx GeneEffects(const Bird& b, const Colony& C) {
    GeneFx f;
    if (b.genes == 0 && C.speciesTrait[0] < 0) {   // (the one-hour match: the mother's trait as it was)
        if (b.trait >= 0 && b.trait < MT_COUNT) { const MateTraitDef& T = MateTraits()[b.trait]; f.speed = T.speed; f.attack = T.attack; f.hpMul = T.hpMul; f.hpAdd = T.hpAdd; f.scout = T.scout; f.clutch = T.clutch; }
        return f;
    }
    for (int t = 0; t < GT_COUNT; t++) {
        int r = GeneRank(b.genes, t);
        for (int k = 0; k < 2; k++) if (C.speciesTrait[k] == t) r = 3;   // (a species: every bird has its trait at III)
        if (!r) continue;
        const GeneDef& G = Genes()[t]; float m = (float)r;   // (rank II is double, III triple)
        f.speed += G.speed * m; f.attack += G.attack * m; f.hpMul += G.hpMul * m; f.hpAdd += G.hpAdd * m; f.scout += G.scout * m; f.stamina += G.stamina * m;
        f.splash = std::max(0.0f, f.splash - G.splash * m); f.dmgTaken = std::max(0.3f, f.dmgTaken - G.dmgTaken * m); f.deep += G.deep * m; f.lucky += G.lucky * m; f.clever += G.clever * m; f.clutch += (int)lroundf(G.clutch * m);
    }
    if (b.rare & RARE_GIANT) { f.hpMul *= ED().giantMul; f.attack *= ED().giantMul; }
    return f;
}
float GiantHunger() { return ED().giantHunger; }
void World::StepEvolution(float dt) {
    if (!LongFlight() || fmodf(time, DAY * 0.25f) >= dt) return;
    const EvoData& D = ED();
    for (int s = 0; s <= (int)sides.size(); s++) {
        Colony& C = ColOf(s);
        if (!HumanOf(s) && C.wantTrait < 0 && GameDay() >= 7) C.wantTrait = (s * 7 + FounderOf(s).def) % MT_COUNT;   // (a bot breeds for one trait from its first week)
        // speciation: 20 birds with the same trait at III make a species (a second at 40 birds of another)
        int count[GT_COUNT] = {};
        for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Adult) for (int t = 0; t < GT_COUNT; t++) count[t] += GeneRank(b.genes, t) == 3;
        int slot = C.speciesTrait[0] < 0 ? 0 : C.speciesTrait[1] < 0 ? 1 : -1;
        if (slot < 0) continue;
        int need = slot == 0 ? D.speciesAt : D.secondAt, best = -1;
        for (int t = 0; t < GT_COUNT; t++) if (t != C.speciesTrait[0] && count[t] >= need && (best < 0 || count[t] > count[best])) best = t;
        if (best < 0) continue;
        C.speciesTrait[slot] = best;
        const GeneDef& G = Genes()[best];
        if (slot == 0) C.speciesName = G.species + " " + std::string(Founders()[FounderOf(s).def].name).substr(4);   // (e.g. "Swiftwing Taloned")
        std::string what = slot == 0 ? TextFormat("%s has become a species of its own: the %s (%s III in every bird; %s).", SideName(s).c_str(), C.speciesName.c_str(), G.name.c_str(), G.power.c_str())
                                     : TextFormat("The %s gains a second species power: %s III (%s).", C.speciesName.c_str(), G.name.c_str(), G.power.c_str());
        for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, what);
        Chronicle(s, CK_SPECIES, what);
    }
}
void World::Conceive(Bird& e, const Bird& mother, const Nest& n) {
    uint32_t mg = mother.genes ? mother.genes : mother.trait >= 0 && mother.trait < MT_COUNT ? SetGene(0, mother.trait, 1) : 0u;   // (a wild mate: its one trait at I)
    bool hybrid = mother.kin >= 0 && mother.kin != me.def;
    e.genes = Inherit(mg, n.line, hybrid, Rand(), Rand(), Rand());
    if (Rand() < ED().rare) e.rare = (uint8_t)(1u << ((int)(Rand() * 3) % 3));   // (a rare birth: albino, giant or crested)
}
int World::CleverAt(int side) const { const Colony& C = ColOf(side); float c = 0; for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Adult) c += GeneEffects(b, C).clever; return (int)lroundf(c * 100); }

// ---------------------------------------------------------------- 3. the Far Sea (doc pp. 9-11): the map doubles at the end of year one
namespace {
struct FarData {
    float ring = 700, wall = 450, openDusk = 0.47f, fleetSpeed = 1.5f, fleetEggR = 150, fleetEggEvery = 0.1f, fleetTwigs = 20; int fleetBombs = 2;
    float rocHunt0 = 0.42f, rocHunt1 = 0.6f, rocRange = 260, rocEvery = 2, rocFlock = 10, rocNestDays = 1, rocHp = 900;
    float mirrorCatch = 1.5f, mirrorMates = 10, mirrorCloud = 1;
    float thornTear = 8, thornConv = 4, iceKrill = 4, cityPearls = 2, cityFervour = 5, cityMob = 8, citySinkDays = 7;
    float frigateRange = 140, frigateEvery = 4, stormScore = 100;
};
const FarData& FD2() {
    static FarData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& f = j["far_sea"];
    auto F = [&](const char* k, float& v) { if (f[k].IsNum()) v = f[k].F(v); };
    F("ring_m", d.ring); F("storm_wall_m", d.wall); F("open_before_dawn_days", d.openDusk); F("fleet_speed", d.fleetSpeed); F("fleet_egg_m", d.fleetEggR); F("fleet_egg_every_days", d.fleetEggEvery); F("fleet_twigs", d.fleetTwigs);
    if (f["fleet_bombs"].IsNum()) d.fleetBombs = f["fleet_bombs"].I(d.fleetBombs);
    F("roc_hunt_from", d.rocHunt0); F("roc_hunt_to", d.rocHunt1); F("roc_range_m", d.rocRange); F("roc_every_s", d.rocEvery); F("roc_flock", d.rocFlock); F("roc_nest_days", d.rocNestDays); F("roc_hp", d.rocHp);
    F("mirror_catch", d.mirrorCatch); F("mirror_mates", d.mirrorMates); F("mirror_cloud_days", d.mirrorCloud);
    F("thorn_tear", d.thornTear); F("thorn_conv_per_priest", d.thornConv); F("ice_krill", d.iceKrill); F("city_pearls", d.cityPearls); F("city_fervour", d.cityFervour); F("city_mob", d.cityMob); F("city_sink_days", d.citySinkDays);
    F("frigate_range_m", d.frigateRange); F("frigate_every_s", d.frigateEvery); F("storm_score", d.stormScore);
    return d;
}
float FlatXZ(Vector3 a, Vector3 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
}  // namespace
float FarRing() { return FD2().ring; }
float StormWallBeyond() { return FD2().wall; }
float StormCrossScore() { return FD2().stormScore; }
bool World::FarOpen() const { return LongFlight() && opts.farSea && time >= (YearDays() - FD2().openDusk) * DAY; }
bool World::IsFar(int isle) const { return isle >= 0 && isle < (int)isles.size() && isles[isle].far; }
void World::InitFarSea() {
    far = FarState{};
    if (!LongFlight() || !opts.farSea || !wholeMap) return;
    // the old map's middle and reach (the fog lies between it and the Far Sea's ring)
    Vector3 c{}; int n = 0; float reach = 0;
    for (const auto& is : isles) if (!is.far) { c = Vector3Add(c, is.c); n++; }
    if (n) c = Vector3Scale(c, 1.0f / n);
    for (const auto& is : isles) if (!is.far) reach = std::max(reach, FlatXZ(is.c, c) + is.radius);
    far.c = c; far.fogR = reach + 120;
    float ringR = 0; for (const auto& is : isles) if (is.far) ringR = std::max(ringR, FlatXZ(is.c, c) + is.radius);
    far.wallR = std::max(far.fogR + 300, ringR) + FD2().wall;
    for (int i = 0; i < (int)isles.size(); i++) {
        switch (isles[i].type) {
        case IsleType::RocPeak: if (far.rocIsle < 0) { far.rocIsle = i; far.roc = Vector3Add(isles[i].hill, {0, 6, 0}); far.rocHp = FD2().rocHp; } break;
        case IsleType::DrownedFleet: if (far.fleet < 0) { far.fleet = i; far.fleetC = isles[i].c; } break;
        case IsleType::Town: if (isles[i].far && far.frigateTown < 0) { far.frigateTown = i; far.frigate = Vector3Add(isles[i].c, {isles[i].radius + 40, 0, 0}); } break;
        default: break;
        }
    }
    far.thornConv.assign(sides.size() + 1, 0); far.iceConv.assign(sides.size() + 1, 0);
}
void World::SetFleetPose() {
    if (far.fleet < 0 || far.fleet >= (int)isles.size()) return;
    Island& is = isles[far.fleet];
    Vector3 d = Vector3Subtract(far.fleetC, is.c); d.y = 0;
    if (fabsf(d.x) < 0.01f && fabsf(d.z) < 0.01f) return;
    is.c = Vector3Add(is.c, d); is.hill = Vector3Add(is.hill, d); is.nest = Vector3Add(is.nest, d); is.x0 += d.x; is.z0 += d.z;
    for (auto& s : is.sites) s = Vector3Add(s, d);
    for (auto& t : is.twigPts) t.first = Vector3Add(t.first, d);
    for (auto& p : is.props) p.c = Vector3Add(p.c, d);
    for (auto& o : is.outline) { o.x += d.x; o.y += d.z; }
}
float World::FarCatchMul(int zone) const {
    // the Mirror Lagoon: fish that never flee (unless a fight has clouded it)
    if (zone < 0 || !eco.map || zone >= (int)eco.map->zones.size() || !LongFlight()) return 1;
    Vector3 zc = eco.map->zones[zone].Center(); int is = IsleAt(zc.x, zc.z, 80);
    if (is < 0 || is >= (int)isles.size() || isles[is].type != IsleType::MirrorLagoon) return 1;
    return time < far.mirrorCloudT ? 0.0f : FD2().mirrorCatch;
}
void World::StepFarSea(float dt) {
    if (!LongFlight() || !opts.farSea || !wholeMap) return;
    const FarData& D = FD2();
    int N = (int)sides.size() + 1;
    bool open = FarOpen();
    if (open && !far.opened) {
        far.opened = true;
        for (int s = 0; s < N; s++) { SayTo(s, "THE FAR SEA OPENS: at dusk the fog beyond the map lifts. New islands, new dangers, new neighbours: the race is on."); Chronicle(s, CK_OTHER, "The fog beyond the map lifted: the Far Sea opened."); }
    }
    auto pushBack = [&](Vector3& p, Vector3& v, float r) {
        float d = FlatXZ(p, far.c); if (d <= r) return false;
        Vector3 u = Vector3Normalize({p.x - far.c.x, 0, p.z - far.c.z});
        p.x = far.c.x + u.x * r; p.z = far.c.z + u.z * r;
        float out = v.x * u.x + v.z * u.z; if (out > 0) { v.x -= 2 * out * u.x; v.z -= 2 * out * u.z; }
        return true;
    };
    for (int s = 0; s < N; s++) {
        Colony& C = ColOf(s); Founder& F = FounderOf(s);
        // before it opens the fog turns every bird back; after, the Storm Wall grounds all but the Albatross
        float limit = open ? far.wallR : far.fogR;
        bool albatross = Founders()[F.def].key == "albatross";
        if (!(open && albatross)) { if (pushBack(F.pos, F.vel, limit) && fmodf(time, 3.0f) < dt) SayTo(s, open ? "The Storm Wall: nothing flies through it (but an Albatross)." : "The fog beyond the map turns you back (it lifts at the end of the first year)."); }
        else if (FlatXZ(F.pos, far.c) > far.wallR + 150 && !C.stormCrossed) { C.stormCrossed = true; SayTo(s, "You crossed the Storm Wall: the map's end, and nothing but the score beyond it."); Chronicle(s, CK_TITLE, "The Founder crossed the Storm Wall."); for (int o = 0; o < N; o++) if (o != s) SayTo(o, SideName(s) + "'s Founder has crossed the Storm Wall."); }
        for (auto& b : C.birds) if (b.alive && b.stage == BStage::Adult) {
            if (!open) pushBack(b.pos, b.vel, far.fogR);
            else if (FlatXZ(b.pos, far.c) > far.wallR) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "lost in the Storm Wall"); }); }
        }
    }
    if (!open) return;
    float ph = DayPhase(); bool night = ph < 0.2f || ph > 0.85f, noon = ph > D.rocHunt0 && ph < D.rocHunt1;
    bool dayTick = fmodf(time, DAY) < dt;
    // ---- the Roc: sleeps on its peak, hunts at noon (the Far Sea, and the old map too from Summer of year two)
    if (far.rocIsle >= 0 && far.rocHp > 0 && opts.roc) {
        Vector3 home = Vector3Add(isles[far.rocIsle].hill, {0, 6, 0});
        bool wide = Year() == 2 && Season() >= SEASON_SUMMER;
        far.rocHunting = noon;
        if (!noon) { far.roc = Vector3Lerp(far.roc, home, std::min(1.0f, dt * 0.2f)); far.rocTarget = -1; }
        else {
            // the biggest flock of more than ten in the open within its range
            int ts = -1, tf = -1, best = (int)D.rocFlock; Vector3 at{};
            for (int s = 0; s < N; s++) for (const auto& f : ColOf(s).flocks) {
                int n = 0; Vector3 m{};
                for (int id : f.members) if (const Bird* b = FindBird(s, id)) { n++; m = Vector3Add(m, b->pos); }
                if (n <= best) continue;
                m = Vector3Scale(m, 1.0f / n);
                if (!wide && FlatXZ(m, far.c) < far.fogR) continue;
                if (FlatXZ(m, far.roc) > D.rocRange * 3) continue;
                best = n; ts = s; tf = f.id; at = m;
            }
            far.rocTarget = ts >= 0 ? ts * 1000 + tf : -1;
            Vector3 goal = ts >= 0 ? Vector3Add(at, {0, 12, 0}) : Vector3Add(home, {80 * cosf(time * 0.05f), 40, 80 * sinf(time * 0.05f)});
            Vector3 dv = Vector3Subtract(goal, far.roc); float dl = Vector3Length(dv);
            if (dl > 0.1f) far.roc = Vector3Add(far.roc, Vector3Scale(dv, std::min(1.0f, 28 * dt / dl)));
            far.rocT += dt;
            if (ts >= 0 && dl < 25) { far.rocHp -= dt * 3 * best; if (far.rocHp <= 0) { for (int s = 0; s < N; s++) SayTo(s, SideName(ts) + "'s flock has killed the Roc."); Chronicle(ts, CK_BEAST, "Our flock killed the Roc."); HuntKilled(1, ts); } }   // (a hunted flock fights back)
            if (ts >= 0 && dl < 25 && far.rocT >= D.rocEvery) {
                far.rocT = 0;
                if (Flock* f = FindFlock(ts, tf)) for (int id : f->members) if (Bird* b = FindBird(ts, id)) { Bird& bb = *b; WithSide(ts, [&] { BirdDies(bb, "taken by the Roc"); }); SayTo(ts, "The ROC strikes your flock (fly in flocks of nine, or at dawn)."); break; }
            }
            // once a hunt it carries off a nest whole from under its path
            if (time - far.rocNestT > D.rocNestDays * DAY) for (int s = 0; s < N && time - far.rocNestT > D.rocNestDays * DAY; s++) {
                Colony& C = ColOf(s);
                for (int ni = 0; ni < (int)C.nests.size(); ni++) {
                    Nest& n = C.nests[ni];
                    if (!n.built || FlatXZ(n.pos, far.roc) > 30 || (!wide && !IsFar(n.isle))) continue;
                    for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage != BStage::Adult) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "carried off with its nest by the Roc"); }); }
                    n.built = false; n.twigs = 0; n.mate = -1; n.bowl = 0; far.rocNestT = time;
                    SayTo(s, "The Roc carries off one of your nests whole!"); Chronicle(s, CK_BEAST, "The Roc carried off one of our nests.");
                    break;
                }
            }
        }
        // its eggs: a Founder who lands on the peak while it hunts takes one (it hatches a Giant)
        for (int s = 0; s < N; s++) {
            Founder& F = FounderOf(s); Colony& C = ColOf(s);
            if (!noon || C.rocEgg || F.st != FState::Perched || FlatXZ(F.pos, home) > 14 || C.caches.empty()) continue;
            C.rocEgg = true;
            Bird g; g.id = C.nextId++; g.stage = BStage::Adult; g.role = Role::Striker; g.rare = RARE_GIANT; g.hp = 120; g.hunger = 1; g.pos = Vector3Add(C.caches[0].pos, {0, 2, 0}); born.push_back(g);
            SayTo(s, "You take an egg from the Roc's nest: it hatches a Giant (a size-4 bird that fights like a Striker and eats for three).");
            Chronicle(s, CK_BEAST, "The Founder stole an egg from the Roc's nest; it hatched a Giant.");
        }
    }
    // ---- the Drowned Fleet: drifts toward the loudest colony; the Drowned take eggs at night; boarded by day for its stores
    if (far.fleet >= 0) {
        int loud = -1, most = -1; for (int s = 0; s < N; s++) { int a = 0; for (const auto& b : ColOf(s).birds) a += b.alive; if (a > most) { most = a; loud = s; } }
        if (loud >= 0 && !ColOf(loud).caches.empty()) {
            Vector3 to = ColOf(loud).caches[0].pos; Vector3 dv = Vector3Subtract(to, far.fleetC); dv.y = 0; float dl = Vector3Length(dv);
            if (dl > isles[far.fleet].radius + 160) far.fleetC = Vector3Add(far.fleetC, Vector3Scale(dv, D.fleetSpeed * dt / dl));
        }
        SetFleetPose();
        const Island& is = isles[far.fleet];
        if (night && fmodf(time, D.fleetEggEvery * DAY) < dt) for (int s = 0; s < N; s++) {
            Colony& C = ColOf(s); bool took = false;
            for (int ni = 0; ni < (int)C.nests.size() && !took; ni++) {
                if (!C.nests[ni].built || FlatXZ(C.nests[ni].pos, is.c) > D.fleetEggR) continue;
                for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage == BStage::Egg) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "taken by the Drowned Fleet's crew"); }); took = true; break; }
            }
            if (took) SayTo(s, "The Drowned come off the Fleet in the night and take an egg.");
        }
        if (!night) for (int s = 0; s < N; s++) {
            Founder& F = FounderOf(s); Colony& C = ColOf(s);
            if (C.fleetBoarded || F.st == FState::Dead || FlatXZ(F.pos, is.c) > is.radius || F.pos.y > 14) continue;
            C.fleetBoarded = true; C.twigs += (int)D.fleetTwigs; C.bombs += D.fleetBombs;
            std::string r;
            if (!far.admiralTaken && RelicCount(s) < RelicsMax()) for (int k = 0; k < RL_COUNT; k++) { bool held = false; for (int o = 0; o < N; o++) held |= (ColOf(o).relics >> k) & 1; if (!held) { C.relics |= 1u << k; far.admiralTaken = true; r = ", and the Admiral's relic: " + Relics()[k].name; break; } }
            SayTo(s, TextFormat("You board the Drowned Fleet by day: rigging twigs (+%d) and its magazine's powder (+%d bombs)%s.", (int)D.fleetTwigs, D.fleetBombs, r.c_str()));
        }
    }
    // ---- the wild colonies: the thorn-birds (Thorns) and the penguins (the Ice Shelf), converted by priests; the Sunken City's Lost Ones
    for (int i = 0; i < (int)isles.size(); i++) {
        const Island& is = isles[i];
        bool thorns = is.type == IsleType::Thorns, ice = is.type == IsleType::IceShelf, city = is.type == IsleType::SunkenCity, mirror = is.type == IsleType::MirrorLagoon;
        if (!thorns && !ice && !city && !mirror) continue;
        int holder = HolderOf(i);
        if ((thorns || city) && fmodf(time, 1.0f) < dt) for (int s = 0; s < N; s++) {   // (the brambles tear; the Lost Ones keep the spires)
            if (s == holder) continue;
            for (auto& b : ColOf(s).birds) {
                if (!b.alive || b.stage != BStage::Adult || FlatXZ(b.pos, is.c) > is.radius || b.pos.y > HeightAt(b.pos.x, b.pos.z) + 8) continue;
                if (thorns && b.role != Role::Skirmisher && b.role != Role::Tank) continue;
                b.hp -= thorns ? D.thornTear : D.cityMob;
                if (b.hp <= 0) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, thorns ? "torn by the brambles and the thorn-birds" : "killed by the Lost Ones in the spires"); }); }
            }
        }
        if ((thorns || ice) && dayTick) for (int s = 0; s < N; s++) {   // (priests convert the wild colony a flock at a time)
            if (s == holder) continue;
            int priests = 0; for (const auto& b : ColOf(s).birds) priests += b.alive && b.stage == BStage::Adult && b.role == Role::Priest && FlatXZ(b.pos, is.c) < 500;
            if (!priests) continue;
            float& conv = thorns ? far.thornConv[s] : far.iceConv[s];
            conv += priests * D.thornConv * (Founders()[FounderOf(s).def].key == "ibis" ? 3.0f : 1.0f);
            if (conv < 100) { SayTo(s, TextFormat("Your priests preach to the %s (%.0f%% converted).", thorns ? "thorn-birds" : "penguins", conv)); continue; }
            conv = 0;
            Colony& C = ColOf(s); int made = 0;
            for (const auto& p : is.sites) { if (made >= 5) break; Nest n; n.pos = p; n.isle = i; n.built = true; n.twigs = (float)NestTwigs(); Site st; st.pos = p; st.isle = i; st.nest = (int)C.nests.size(); C.sites.push_back(st); n.site = (int)C.sites.size() - 1; C.nests.push_back(n); made++; }
            for (int k = 0; k < 4; k++) { Bird w; w.id = C.nextId++; w.stage = BStage::Adult; w.role = thorns ? Role::Skirmisher : Role::Fisher; w.hp = 60; w.hunger = 1; w.pos = Vector3Add(is.c, {(float)k, 6, 0}); born.push_back(w); }
            for (int o = 0; o < N; o++) SayTo(o, SideName(s) + TextFormat(" has converted the %s: their island is theirs.", thorns ? "thorn-birds" : "penguins"));
            Chronicle(s, CK_FOUNDING, thorns ? "The thorn-birds of the Archipelago joined the colony." : "The penguins of the Ice Shelf joined the colony.");
        }
        if (holder < 0) continue;
        Colony& H = ColOf(holder);
        if (dayTick) {
            if (ice) for (int q = 0; q < (int)D.iceKrill && !H.caches.empty(); q++) H.caches[0].fish.push_back({0, 1, 0});   // (krill in walls)
            if (mirror) H.wildMates += (int)D.mirrorMates;   // (mates in dozens)
            if (city) {
                H.pearls += (int)D.cityPearls; H.fervour = std::min(100.0f, H.fervour + D.cityFervour);   // (the city's shrine: the Sky Temple's power at a quarter)
                if (!far.cityRelics) { far.cityRelics = true; int got = 0; for (int r = 0; r < RL_COUNT && got < 3 && RelicCount(holder) < RelicsMax(); r++) { bool held = false; for (int o = 0; o < N; o++) held |= (ColOf(o).relics >> r) & 1; if (!held) { H.relics |= 1u << r; got++; } } if (got) SayTo(holder, TextFormat("The Sunken City's vaults give up %d relics.", got)); }
            }
        }
        if (city && fmodf(time, D.citySinkDays * DAY) < dt && time > DAY) {   // (the city sinks a spire a week)
            for (int s = 0; s < N; s++) { Colony& C = ColOf(s); for (int ni = (int)C.nests.size() - 1; ni >= 0; ni--) if (C.nests[ni].built && C.nests[ni].isle == i) { C.nests[ni].built = false; C.nests[ni].twigs = 0; C.nests[ni].mate = -1; SayTo(s, "A spire of the Sunken City sinks, and a nest with it."); break; } }
        }
    }
    // ---- a fight over the Mirror Lagoon clouds it for a day
    for (size_t k = far.fxSeen; k < warFx.size(); k++) for (int i = 0; i < (int)isles.size(); i++) if (isles[i].type == IsleType::MirrorLagoon && FlatXZ(warFx[k].p, isles[i].c) < isles[i].radius + 30) far.mirrorCloudT = time + D.mirrorCloud * DAY;
    far.fxSeen = warFx.size();
    // ---- the navy's frigate: patrols its port, fires on the pirates and on the frigatebirds
    if (far.frigateTown >= 0) {
        const Island& t = isles[far.frigateTown];
        float a = time * 0.02f; far.frigate = {t.c.x + cosf(a) * (t.radius + 60), 0, t.c.z + sinf(a) * (t.radius + 60)};
        far.frigateT += dt;
        if (far.frigateT >= D.frigateEvery) {
            far.frigateT = 0;
            if (pirates.on && FlatXZ(pirates.pos, far.frigate) < D.frigateRange * 2) { pirates.hp -= 60; if (pirates.hp <= 0) { pirates.scatterUntil = time + DAY; pirates.hp = 400; for (int s = 0; s < N; s++) SayTo(s, "The navy's frigate scatters the pirates."); } }
            for (int s = 0; s < N; s++) {
                if (Founders()[FounderOf(s).def].key != "frigatebird") continue;
                for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && FlatXZ(b.pos, far.frigate) < D.frigateRange) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "shot by the navy's frigate"); }); SayTo(s, "The navy's frigate fires on your birds: it hunts pirates, frigatebirds included."); break; }
            }
        }
    }
}

// ---------------------------------------------------------------- 4. Grand Projects (doc pp. 7-9): nine wonders, one per map
namespace {
struct ProjData { std::vector<WonderDef> w; float minDays = 4, raiseMul = 0.5f, taxShare = 0.2f, gateYield = 2, rookeryMates = 1.3f, beamNight = 600, chainKills = 3, arkMates = 8, arkMove = 400; int worksBombs = 3; };
const ProjData& PD2() {
    static ProjData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& p = j["projects"];
    auto F = [&](const char* k, float& v) { if (p[k].IsNum()) v = p[k].F(v); };
    F("min_days", d.minDays); F("raise_cost", d.raiseMul); F("gate_tax", d.taxShare); F("gate_yield", d.gateYield); F("rookery_others_mate_time", d.rookeryMates); F("beam_night_m", d.beamNight);
    F("chain_kills", d.chainKills); F("ark_mates", d.arkMates); F("ark_move_m", d.arkMove);
    if (p["works_bombs"].IsNum()) d.worksBombs = p["works_bombs"].I(d.worksBombs);
    for (const Json& x : p["list"].a) {
        WonderDef w; w.key = x["key"].Str0(); w.name = x["name"].Str0(w.key); w.where = x["where"].Str0(); w.forBuilder = x["builder"].Str0(); w.forMap = x["map"].Str0(); w.raised = x["raised"].Str0();
        w.twigs = x["twigs"].I(0); w.shells = x["shells"].I(0); w.pearls = x["pearls"].I(0); w.sulfur = x["sulfur"].I(0); w.score = x["score"].I(300);
        d.w.push_back(w);
    }
    while ((int)d.w.size() < WD_COUNT) { WonderDef w; w.key = w.name = "wonder"; d.w.push_back(w); }
    return d;
}
}  // namespace
const std::vector<WonderDef>& Wonders() { return PD2().w; }
int StTwigs(const Structure& s) { return s.kind == ST_WONDER && s.wonder >= 0 ? (int)lroundf(Wonders()[s.wonder].twigs * (s.raising ? PD2().raiseMul : 1.0f)) : StructureTwigs(s.kind); }
int StShells(const Structure& s) { return s.kind == ST_WONDER && s.wonder >= 0 ? (int)lroundf(Wonders()[s.wonder].shells * (s.raising ? PD2().raiseMul : 1.0f)) : StructureShells(s.kind); }
bool World::WonderSiteOk(int wonder, int isle) const {
    if (wonder < 0 || wonder >= WD_COUNT || isle < 0 || isle >= (int)isles.size()) return false;
    IsleType t = isles[isle].type;
    switch (wonder) {
    case WD_ROOKERY: return isles[isle].sites.size() >= 20;
    case WD_LIGHTHOUSE: return t == IsleType::Stack || t == IsleType::CliffTown || t == IsleType::Lighthouse || t == IsleType::RocPeak || t == IsleType::KrakenCove;
    case WD_GATE: return t == IsleType::ReefGarden || t == IsleType::Atoll || t == IsleType::Shipwreck || t == IsleType::Islet || t == IsleType::Thorns;
    case WD_TEMPLE: { float top = -1e9f; int best = -1; for (int i = 0; i < (int)isles.size(); i++) if (!isles[i].far || FarOpen()) if (isles[i].hill.y > top) { top = isles[i].hill.y; best = i; } return isle == best; }
    case WD_WORKS: return t == IsleType::Volcano || t == IsleType::IronIsland;
    case WD_MARKET: return t == IsleType::Town || t == IsleType::CliffTown;
    case WD_CHAIN: return t == IsleType::KrakenCove;
    default: return true;   // (the Ark, the Monument of Feathers: anywhere)
    }
}
int World::WonderBy(int wonder) const { return wonder >= 0 && wonder < WD_COUNT ? wonderBy[wonder] : -1; }
bool World::HasWonder(int side, int wonder) const { return WonderBy(wonder) == side; }
bool World::Consecrate(int wonder) {
    if (!LongFlight() || wonder < 0 || wonder >= WD_COUNT) return false;
    if (wonderBy[wonder] >= 0) { Say(Wonders()[wonder].name + " is already built (by " + SideName(wonderBy[wonder]) + ")."); return false; }
    for (const auto& s : col.builds) if (s.kind == ST_WONDER && s.wonder == wonder) { Say("Your builders are already raising it."); return false; }
    int isle = IsleAt(me.pos.x, me.pos.z, 10);
    if (!WonderSiteOk(wonder, isle)) { Say(Wonders()[wonder].name + " must stand " + Wonders()[wonder].where + ": fly the Founder there and consecrate it."); return false; }
    Structure st; st.kind = ST_WONDER; st.wonder = wonder; st.isle = isle; st.pos = GroundAt(me.pos.x, me.pos.z); st.hp = 2000; st.startT = time;
    col.builds.push_back(st);
    Say("The site of " + Wonders()[wonder].name + " is consecrated: your builders will raise it (scaffolding everyone can scout).");
    for (int o = 0; o <= (int)sides.size(); o++) if (o != cur) SayTo(o, SideName(cur) + " has begun " + Wonders()[wonder].name + " on " + isles[isle].name + ".");
    Chronicle(cur, CK_WONDER, "The site of " + Wonders()[wonder].name + " was consecrated on " + isles[isle].name + ".");
    return true;
}
bool World::RaiseWonder(int wonder) {
    if (!LongFlight() || Year() < 2 || wonderBy[wonder] != cur || (wonderRaised >> wonder) & 1) return false;
    for (auto& s : col.builds) if (s.kind == ST_WONDER && s.wonder == wonder && s.built) { s.built = false; s.raising = true; s.twigs = 0; s.shells = 0; s.startT = time; Say(Wonders()[wonder].name + ": its second tier begins (half the cost again)."); return true; }
    return false;
}
bool World::WonderAct(int wonder, int arg, Vector3 at) {
    // the builder's hand: the Temple's blessing or curse, the Works' wind, the Chain's mark, the Ark's course
    if (!HasWonder(cur, wonder)) return false;
    if (wonder == WD_TEMPLE) {
        if (time - col.templeT < DAY) { Say("The Temple speaks once a day."); return false; }
        int t = arg >= 0 ? arg % 1000 : -1; bool curse = arg >= 1000 && arg < 2000; if (arg >= 1000) t = arg - (curse ? 1000 : 2000);
        if (t < 0 || t > (int)sides.size()) return false;
        col.templeT = time;
        ColOf(t).fervour = std::clamp(ColOf(t).fervour + (curse ? -10.0f : 10.0f), 0.0f, 100.0f);
        if (curse && (wonderRaised >> WD_TEMPLE) & 1) for (auto& f : ColOf(t).flocks) OrderFlock(t, f.id, Target::Home, -1, -1, -1, -1, {});   // (raised: the curse routs)
        for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, SideName(cur) + "'s Sky Temple " + (curse ? "curses " : "blesses ") + SideName(t) + ".");
        return true;
    }
    if (wonder == WD_WORKS) { float a = (float)arg * PI / 180; wind.dir = wind.nextDir = {cosf(a), sinf(a)}; col.windPick = arg; Say("The Great Works turn the wind."); return true; }
    if (wonder == WD_CHAIN) { if (arg < 0 || arg > (int)sides.size() || arg == cur) return false; col.chainMark = arg; Say("The Kraken's Chain: the kraken will rise against " + SideName(arg) + " at dawn."); return true; }
    if (wonder == WD_ARK) {
        Vector3 d = Vector3Subtract(at, arkPos); d.y = 0; float l = Vector3Length(d);
        if (l > PD2().arkMove) d = Vector3Scale(d, PD2().arkMove / l);
        if (time - col.arkT < DAY) { Say("The Ark moves a region a day."); return false; }
        col.arkT = time; arkPos = Vector3Add(arkPos, d); Say("The Ark drifts to its new water (the wild birds follow it)."); return true;
    }
    return false;
}
float World::MateTimeMul(int side) const {
    // the Great Rookery's island draws the map's wild mates: everyone else's courtship takes longer; the Ark the same
    float m = 1;
    for (int wd : {WD_ROOKERY, WD_ARK}) if (wonderBy[wd] >= 0 && wonderBy[wd] != side) m *= PD2().rookeryMates;
    return m;
}
float World::GateTaxFor(int zone, int side) const {
    if (wonderBy[WD_GATE] < 0 || wonderBy[WD_GATE] == side || zone != gateZone) return 0;
    return PD2().taxShare;
}
void World::StepWonders(float dt) {
    if (!LongFlight()) return;
    const ProjData& D = PD2();
    int N = (int)sides.size() + 1;
    bool dayTick = fmodf(time, DAY) < dt;
    float ph = DayPhase(); bool night = ph < 0.2f || ph > 0.85f;
    for (int s = 0; s < N; s++) {
        Colony& C = ColOf(s);
        for (auto& st : C.builds) {
            if (st.kind != ST_WONDER || st.built || st.wonder < 0) continue;
            const WonderDef& W = Wonders()[st.wonder];
            if (!st.raising && wonderBy[st.wonder] >= 0) continue;   // (someone else finished it first)
            float mul = st.raising ? D.raiseMul : 1.0f;
            int pearls = (int)lroundf(W.pearls * mul), sulfur = (int)lroundf(W.sulfur * mul);
            if (st.twigs < StTwigs(st) || st.shells < StShells(st) || time - st.startT < D.minDays * DAY) continue;
            if (C.pearls < pearls || C.sulfur < sulfur) { if (dayTick) SayTo(s, TextFormat("%s waits on the stores: %d pearls and %d sulfur.", W.name.c_str(), pearls, sulfur)); continue; }
            C.pearls -= pearls; C.sulfur -= (float)sulfur;
            st.built = true;
            if (st.raising) { st.raising = false; wonderRaised |= 1u << st.wonder; for (int o = 0; o < N; o++) SayTo(o, SideName(s) + " raises " + W.name + " to its second tier: " + W.raised + "."); Chronicle(s, CK_WONDER, W.name + " was raised to its second tier."); continue; }
            wonderBy[st.wonder] = s;
            if (st.wonder == WD_ARK) arkPos = st.pos;
            if (st.wonder == WD_GATE && eco.map) { int z = eco.ZoneAt({st.pos.x, -1, st.pos.z}); if (z < 0) { float bd = 1e9f; for (int k = 0; k < (int)eco.map->zones.size(); k++) { float dd = Vector3Distance(eco.map->zones[k].Center(), st.pos); if (dd < bd) { bd = dd; z = k; } } } gateZone = z; for (auto& sk : stocks) if (sk.zone == z) sk.K *= D.gateYield; }
            for (int o = 0; o < N; o++) { SayTo(o, SideName(s) + " has finished " + W.name + ". " + (o == s ? W.forBuilder : W.forMap) + "."); if (o != s) Chronicle(o, CK_WONDER, SideName(s) + " finished " + W.name + "."); }
            Chronicle(s, CK_WONDER, "We finished " + W.name + ": " + W.forBuilder + ".");
            // the others' progress on it turns to shells
            for (int o = 0; o < N; o++) if (o != s) { Colony& O = ColOf(o); for (int k = (int)O.builds.size() - 1; k >= 0; k--) if (O.builds[k].kind == ST_WONDER && O.builds[k].wonder == st.wonder && !O.builds[k].built) { O.shells += (int)(O.builds[k].twigs / 2) + O.builds[k].shells; O.builds.erase(O.builds.begin() + k); SayTo(o, "Your work on " + W.name + " is turned to shells: " + SideName(s) + " finished it first."); } }
        }
    }
    // ---- what the wonders do
    if (int b = wonderBy[WD_ROOKERY]; b >= 0 && dayTick) {   // (every chick on its island fledges together at dawn)
        Colony& C = ColOf(b); int isle = -1; for (const auto& st : C.builds) if (st.kind == ST_WONDER && st.wonder == WD_ROOKERY) isle = st.isle;
        bool raised = (wonderRaised >> WD_ROOKERY) & 1;
        WithSide(b, [&] { for (auto& c : col.birds) if (c.alive && c.nest >= 0 && c.nest < (int)col.nests.size() && col.nests[c.nest].isle == isle) { if (c.stage == BStage::Chick && c.age > Econ().chickDays * 0.5f) Fledge(c); else if (raised && c.stage == BStage::Egg) c.age = std::max(c.age, Econ().hatchDays); } });
    }
    if (int b = wonderBy[WD_LIGHTHOUSE]; b >= 0 && night && fmodf(time, 3.0f) < dt) {   // (the beam: a region of the builder's choice each night; every colony sees it)
        Vector3 lamp{}; for (const auto& st : ColOf(b).builds) if (st.kind == ST_WONDER && st.wonder == WD_LIGHTHOUSE) lamp = st.pos;
        int pick = (int)(time / DAY) % std::max(1, (int)isles.size());
        WithSide(b, [&] { Reveal(isles[pick].c, D.beamNight); });
        for (int o = 0; o < N; o++) WithSide(o, [&] { Reveal(lamp, 120); });
        if ((wonderRaised >> WD_LIGHTHOUSE) & 1) for (int o = 0; o < N; o++) if (o != b) for (auto& f : ColOf(o).flocks) if (Vector2Distance({f.pos.x, f.pos.z}, {lamp.x, lamp.z}) < 400 && f.target != Target::Home) { OrderFlock(o, f.id, Target::Home, -1, -1, -1, -1, {}); SayTo(o, "The Lighthouse of Birds' beam blinds your flock: it turns for home."); }
    }
    if (int b = wonderBy[WD_TEMPLE]; b >= 0) ColOf(b).fervour = 100;   // (fervour 100 for the builder)
    if (int b = wonderBy[WD_WORKS]; b >= 0 && dayTick) { ColOf(b).bombs += D.worksBombs; if ((wonderRaised >> WD_WORKS) & 1) ColOf(b).blockbusters++; }
    if (int b = wonderBy[WD_ARK]; b >= 0 && dayTick) ColOf(b).wildMates += (int)D.arkMates * (((wonderRaised >> WD_ARK) & 1) ? 2 : 1);
    if (int b = wonderBy[WD_CHAIN]; b >= 0) {
        kraken.mood = 0;   // (the kraken no longer roams: it answers to the Chain)
        Colony& B = ColOf(b);
        if (dayTick && B.chainMark >= 0 && B.chainMark <= (int)sides.size() && B.chainMark != b) {
            int t = B.chainMark; Colony& T = ColOf(t); int killed = 0;
            Vector3 home = T.caches.empty() ? isles[HomeOf(t)].c : T.caches[0].pos;
            int times = ((wonderRaised >> WD_CHAIN) & 1) ? 2 : 1;
            for (auto& x : T.birds) if (killed < (int)D.chainKills * times && x.alive && x.stage == BStage::Adult && Vector3Distance(x.pos, home) < 160) { Bird& bb = x; WithSide(t, [&] { BirdDies(bb, "seized by the chained kraken"); }); killed++; }
            SayTo(t, "The chained KRAKEN rises off your island at " + SideName(b) + "'s word!"); SayTo(b, TextFormat("The kraken rises against %s (%d of their birds taken).", SideName(t).c_str(), killed));
            B.chainMark = -1;
        }
    }
}
void World::BotWonders() {
    if (!LongFlight() || fmodf(time, DAY) >= 0.2f || HumanOf(cur) || GameDay() < 7) return;
    int alive = 0; for (const auto& b : col.birds) alive += b.alive && b.stage == BStage::Adult;
    if (alive < 25) return;
    for (const auto& s : col.builds) if (s.kind == ST_WONDER && !s.built) return;   // (one at a time)
    for (int wd = 0; wd < WD_COUNT; wd++) {
        if (wonderBy[wd] >= 0 || (wd == WD_MONUMENT && Year() < 2)) continue;
        bool mine = false; for (const auto& s : col.builds) mine |= s.kind == ST_WONDER && s.wonder == wd;
        if (mine || !WonderSiteOk(wd, home)) continue;
        Vector3 keep = me.pos; me.pos = isles[home].nest; bool ok = Consecrate(wd); me.pos = keep;
        if (ok) return;
    }
}
int World::WonderScore(int side) const {
    int s = 0;
    for (int w = 0; w < WD_COUNT; w++) if (wonderBy[w] == side) s += Wonders()[w].score * (((wonderRaised >> w) & 1) ? 2 : 1);
    return s;
}

// ---------------------------------------------------------------- 5. leagues, the Council and the Great War (doc pp. 11-12)
namespace {
struct CouncilData {
    int leaguePearls = 1; float leagueDays = 1, leaveFervour = 30, oathDays = 6, oathTrade = 0.8f;
    int firstDay = 31, every = 6; float voteDays = 0.3f, motionDays = 6; int peaceFish = 20; float huntScore = 300;
    float warDays = 6, warFervour = 5, warFishPerBird = 1, warShare = 2.0f / 3; float warScore = 200;
};
const CouncilData& CD() {
    static CouncilData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& c = j["council"];
    auto F = [&](const char* k, float& v) { if (c[k].IsNum()) v = c[k].F(v); };
    auto I = [&](const char* k, int& v) { if (c[k].IsNum()) v = c[k].I(v); };
    I("league_pearls", d.leaguePearls); F("league_days", d.leagueDays); F("leave_fervour", d.leaveFervour); F("oath_days", d.oathDays); F("oath_trade", d.oathTrade);
    I("first_day", d.firstDay); I("every_days", d.every); F("vote_days", d.voteDays); F("motion_days", d.motionDays); I("peace_fish", d.peaceFish); F("hunt_score", d.huntScore);
    F("war_days", d.warDays); F("war_fervour", d.warFervour); F("war_fish_per_bird", d.warFishPerBird); F("war_share", d.warShare); F("war_score", d.warScore);
    return d;
}
}  // namespace
const char* MotionName(int k) { static const char* N[MO_COUNT] = {"Peace of the Sea", "The Hunt", "Embargo", "Sanctuary", "Tithe", "The Great War"}; return N[std::clamp(k, 0, MO_COUNT - 1)]; }
const char* MotionWhat(int k) {
    static const char* W[MO_COUNT] = {"no raids for six days; every player pays 20 fish and the pirates are paid off", "whoever kills the named beast gets 300 and its relic",
                                      "the named player can't trade at any town for six days", "the named island is neutral for six days: no nests built or destroyed there",
                                      "every player pays a pearl to the Council's chest", "the proposer's league against everyone else, from the next dawn"};
    return W[std::clamp(k, 0, MO_COUNT - 1)];
}
bool World::Leagued(int a, int b) const { return a != b && a >= 0 && b >= 0 && a <= (int)sides.size() && b <= (int)sides.size() && ColOf(a).league >= 0 && ColOf(a).league == ColOf(b).league && time >= ColOf(a).leagueFrom && time >= ColOf(b).leagueFrom; }
bool World::Oathbroken(int side) const { return side >= 0 && side <= (int)sides.size() && time < ColOf(side).oathUntil; }
bool World::Embargoed(int side) const { return council.embargo == side && time < council.embargoUntil; }
bool World::PeaceNow() const { return time < council.peaceUntil; }
bool World::Sanctuary(int isle) const { return isle >= 0 && isle == council.sanctuary && time < council.sanctuaryUntil; }
int World::WarSide(int side) const { if (!council.war) return -1; return ((council.warA >> side) & 1) ? 0 : 1; }
int World::VoteWeight(int side) const { int w = 0; for (int i = 0; i < (int)isles.size(); i++) w += HolderOf(i) == side; return std::max(1, w + RelicCount(side)); }
void World::OathBreak(int side, const std::string& what) {
    Colony& C = ColOf(side);
    if (time < C.oathUntil) return;
    C.oathUntil = time + CD().oathDays * DAY; C.oathsBroken++;
    for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, SideName(side) + " is OATHBROKEN (" + what + "): worse rates at every town, no new leagues, its name in red for a season.");
    Chronicle(side, CK_OATH, "We broke our oath: " + what + ".");
}
int World::OfferLeague(int to) {
    if (!LongFlight() || to < 0 || to > (int)sides.size() || to == cur) return -1;
    if (Oathbroken(cur)) { Say("An oathbroken colony can't make a league."); return -1; }
    if (col.pearls < CD().leaguePearls) { Say("A league costs a pearl from each."); return -1; }
    Barter o; o.id = nextOffer++; o.from = cur; o.to = to; o.t = time; o.league = true; o.give[G_PEARLS] = 0;
    offers.push_back(o);
    SayTo(to, SideName(cur) + "'s Herald proposes a league: shared sight, feed lines, flocks together, the Reckoning's score shared (the trade page).");
    Say("Your Herald carries the proposal.");
    return o.id;
}
void World::JoinLeague(int a, int b) {
    Colony& A = ColOf(a); Colony& B = ColOf(b);
    A.pearls = std::max(0, A.pearls - CD().leaguePearls); B.pearls = std::max(0, B.pearls - CD().leaguePearls);
    int id = A.league >= 0 ? A.league : B.league >= 0 ? B.league : nextLeague++;
    int old = B.league; for (int s = 0; s <= (int)sides.size(); s++) if (old >= 0 && ColOf(s).league == old) ColOf(s).league = id;
    A.league = B.league = id; A.leagueFrom = std::max(A.leagueFrom, time + CD().leagueDays * DAY); B.leagueFrom = std::max(B.leagueFrom, time + CD().leagueDays * DAY);
    for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, SideName(a) + " and " + SideName(b) + " form a league (bound in a day).");
    Chronicle(a, CK_MARRIAGE, "We bound ourselves in a league with " + SideName(b) + "."); Chronicle(b, CK_MARRIAGE, "We bound ourselves in a league with " + SideName(a) + ".");
}
bool World::LeaveLeague() {
    if (col.league < 0) return false;
    col.league = -1; col.fervour = std::max(0.0f, col.fervour - CD().leaveFervour);
    OathBreak(cur, "it left its league");
    return true;
}
bool World::Propose(int kind, int target) {
    if (!LongFlight() || kind < 0 || kind >= MO_COUNT || council.meeting < 0) { Say("The Council isn't sitting (it meets every six days from Summer of year two)."); return false; }
    for (const auto& m : council.agenda) if (m.by == cur) { Say("One proposal a player a meeting."); return false; }
    Motion m; m.kind = kind; m.by = cur; m.target = target; m.votes.assign(sides.size() + 1, 0);
    council.agenda.push_back(m);
    for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, SideName(cur) + " proposes to the Council: " + MotionName(kind) + ".");
    return true;
}
bool World::Vote(int idx, bool yes) {
    if (council.meeting < 0 || idx < 0 || idx >= (int)council.agenda.size()) return false;
    council.agenda[idx].votes[cur] = yes ? 1 : -1;
    return true;
}
void World::BotCouncil(int s) {
    // a bot proposes what suits it, and votes its interest
    Colony& C = ColOf(s);
    float mine = Score(s).total, avg = 0; int N = (int)sides.size() + 1; for (int o = 0; o < N; o++) avg += Score(o).total; avg /= N;
    bool proposed = false; for (const auto& m : council.agenda) proposed |= m.by == s;
    if (!proposed && Rand() < 0.4f) {
        int leader = 0; for (int o = 1; o < N; o++) if (Score(o).total > Score(leader).total) leader = o;
        Motion m; m.by = s; m.votes.assign(N, 0);
        if (mine < avg * 0.85f) { m.kind = MO_PEACE; }
        else if (C.league >= 0 && opts.greatWar && !council.war && Year() == 2 && Season() == SEASON_SUMMER) { m.kind = MO_WAR; }
        else if (leader != s) { m.kind = MO_EMBARGO; m.target = leader; }
        else { m.kind = MO_HUNT; m.target = 0; }
        council.agenda.push_back(m);
    }
    for (auto& m : council.agenda) {
        if (m.votes[s]) continue;
        bool yes = false;
        switch (m.kind) {
        case MO_PEACE: yes = mine < avg; break;
        case MO_HUNT: yes = true; break;
        case MO_EMBARGO: yes = m.target != s && !Leagued(s, m.target); break;
        case MO_SANCTUARY: yes = HolderOf(m.target) != s; break;
        case MO_TITHE: yes = C.pearls > 6; break;
        case MO_WAR: yes = m.by == s || Leagued(s, m.by); break;
        }
        m.votes[s] = yes ? 1 : -1;
    }
}
void World::StepCouncil(float dt) {
    if (!LongFlight()) return;
    const CouncilData& D = CD();
    int N = (int)sides.size() + 1;
    int day = GameDay();
    bool dayTick = fmodf(time, DAY) < dt;
    // the leagues: shared sight (each member sees what any member has seen)
    if (fmodf(time, 5.0f) < dt) for (int a = 0; a < N; a++) for (int b = a + 1; b < N; b++) if (Leagued(a, b)) {
        Knowledge& KA = a == cur ? know : (a == 0 ? sides[cur - 1].know : sides[a - 1].know);
        Knowledge& KB = b == cur ? know : (b == 0 ? sides[cur - 1].know : sides[b - 1].know);
        for (size_t i = 0; i < KA.isle.size() && i < KB.isle.size(); i++) KA.isle[i] = KB.isle[i] = std::max(KA.isle[i], KB.isle[i]);
    }
    // bots seek a league in year two (a colony that hasn't raided them)
    if (dayTick && Year() == 2) for (int s = 0; s < N; s++) {
        if (HumanOf(s) || ColOf(s).league >= 0 || Oathbroken(s) || Rand() > 0.25f) continue;
        int to = -1; for (int o = 0; o < N; o++) if (o != s && ColOf(s).lastRaider != o && ColOf(o).lastRaider != s && (to < 0 || Rand() < 0.5f)) to = o;
        if (to >= 0) WithSide(s, [&] { OfferLeague(to); });
    }
    // the Council: from Summer of year two, a meeting at dawn every six days
    if (council.meeting < 0 && day >= D.firstDay && (day - D.firstDay) % D.every == 0 && council.lastMeeting != day && fmodf(time, DAY) < 0.1f * DAY) {
        council.meeting = day; council.lastMeeting = day; council.agenda.clear();
        for (int s = 0; s < N; s++) SayTo(s, "THE COUNCIL SITS: propose a motion and vote by midday (weight: islands held and relics).");
    }
    if (council.meeting >= 0) {
        for (int s = 0; s < N; s++) if (!HumanOf(s)) BotCouncil(s);
        if (fmodf(time, DAY) >= D.voteDays * DAY) {
            for (auto& m : council.agenda) {
                int yes = 0, no = 0; for (int s = 0; s < N; s++) { int wv = VoteWeight(s); if (m.votes[s] > 0) yes += wv; else if (m.votes[s] < 0) no += wv; }
                bool pass = yes > no;
                for (int s = 0; s < N; s++) SayTo(s, TextFormat("The Council: %s %s (%d to %d).", MotionName(m.kind), pass ? "PASSES" : "fails", yes, no));
                if (!pass) continue;
                float until = time + D.motionDays * DAY;
                switch (m.kind) {
                case MO_PEACE:
                    ColOf(m.by).peacemaker = true;
                    council.peaceUntil = until; pirates.scatterUntil = std::max(pirates.scatterUntil, until);
                    for (int s = 0; s < N; s++) { int n = D.peaceFish; for (auto& c : ColOf(s).caches) while (n > 0 && !c.fish.empty()) { c.fish.pop_back(); n--; } Chronicle(s, CK_OATH, "The Council made the Peace of the Sea."); }
                    for (int s = 0; s < N; s++) for (auto& f : ColOf(s).flocks) if (f.target == Target::Cache || f.target == Target::Nests) OrderFlock(s, f.id, Target::Home, -1, -1, -1, -1, {});
                    if (council.war) { council.war = false; for (int s = 0; s < N; s++) SayTo(s, "The Peace ends the Great War."); }
                    break;
                case MO_HUNT: council.hunt = m.target; council.huntUntil = until; break;
                case MO_EMBARGO: council.embargo = m.target; council.embargoUntil = until; break;
                case MO_SANCTUARY: council.sanctuary = m.target; council.sanctuaryUntil = until; break;
                case MO_TITHE: for (int s = 0; s < N; s++) if (ColOf(s).pearls > 0) { ColOf(s).pearls--; council.chest++; } break;
                case MO_WAR: {
                    if (council.war || !opts.greatWar) break;
                    uint32_t A = 1u << m.by; for (int s = 0; s < N; s++) if (Leagued(s, m.by)) A |= 1u << s;
                    council.warA = A; council.warFrom = (floorf(time / DAY) + 1) * DAY; council.warUntil = council.warFrom + D.warDays * DAY; council.war = true;
                    for (int s = 0; s < N; s++) { SayTo(s, std::string("THE GREAT WAR is declared: ") + (((A >> s) & 1) ? "your side is the proposer's league" : "you stand with everyone else") + ". It begins at the next dawn."); Chronicle(s, CK_WAR, "The Great War was declared."); }
                } break;
                }
            }
            council.meeting = -1; council.agenda.clear();
        }
    }
    // a raid during the Peace breaks the oath
    if (PeaceNow()) for (int s = 0; s < N; s++) for (const auto& f : ColOf(s).flocks) if ((f.target == Target::Cache || f.target == Target::Nests) && f.tSide >= 0 && f.tSide != s && !f.retreating) OathBreak(s, "it raided during the Peace of the Sea");
    // the Great War
    if (council.war && time >= council.warFrom) {
        if (dayTick) {
            for (int s = 0; s < N; s++) {   // (war weariness: armies eat, and every day costs fervour)
                Colony& C = ColOf(s); int birds = 0; for (const auto& b : C.birds) birds += b.alive;
                C.fervour = std::max(0.0f, C.fervour - D.warFervour);
                int n = (int)(birds * D.warFishPerBird); for (auto& c : C.caches) while (n > 0 && !c.fish.empty()) { c.fish.pop_back(); n--; }
                if (!HumanOf(s)) { int foe = -1; float bd = 1e9f; for (int o = 0; o < N; o++) if (WarSide(o) != WarSide(s) && !ColOf(o).caches.empty() && !C.caches.empty()) { float d = Vector3Distance(C.caches[0].pos, ColOf(o).caches[0].pos); if (d < bd) { bd = d; foe = o; } } C.warT = 0; if (foe >= 0) ColOf(foe).lastRaider = s; }
            }
        }
        // the fronts: every island either side holds; two-thirds to one side ends it
        int a = 0, b = 0; for (int i = 0; i < (int)isles.size(); i++) { int h = HolderOf(i); if (h < 0) continue; if (WarSide(h) == 0) a++; else b++; }
        int fronts = a + b; int won = -1;
        if (fronts >= 3 && a >= D.warShare * fronts) won = 0; else if (fronts >= 3 && b >= D.warShare * fronts) won = 1;
        if (won < 0 && time >= council.warUntil) won = a > b ? 0 : b > a ? 1 : 2;
        if (won >= 0) {
            council.war = false;
            for (int s = 0; s < N; s++) {
                bool win = won < 2 && WarSide(s) == won;
                if (win) { ColOf(s).warsWon++; Chronicle(s, CK_WAR, "We won the Great War."); } else Chronicle(s, CK_WAR, won == 2 ? "The Great War ended with no side the victor." : "We lost the Great War.");
                SayTo(s, won == 2 ? std::string("The Great War ends, undecided.") : win ? std::string("The GREAT WAR is won: your side holds the fronts (+200).") : std::string("The Great War is lost; you keep what you held."));
            }
        }
    }
}

void World::HuntKilled(int beast, int side) {
    if (!LongFlight() || council.hunt != beast || time > council.huntUntil || side < 0) return;
    Colony& C = ColOf(side); C.huntScore += (int)CD().huntScore; council.hunt = -1;
    if (RelicCount(side) < RelicsMax()) for (int r = 0; r < RL_COUNT; r++) { bool held = false; for (int o = 0; o <= (int)sides.size(); o++) held |= (ColOf(o).relics >> r) & 1; if (!held) { C.relics |= 1u << r; break; } }
    for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, SideName(side) + " wins the Council's Hunt (+300 and the beast's relic).");
    Chronicle(side, CK_BEAST, beast == 0 ? "We killed the kraken in the Council's Hunt." : "We killed the Roc in the Council's Hunt.");
}
void World::ShareLeagueScores() {
    if (!LongFlight()) return;
    int N = (int)scores.size();
    for (int a = 0; a < N; a++) { int lg = ColOf(a).league; if (lg < 0) continue; int sum = 0, n = 0; for (int b = 0; b < N; b++) if (ColOf(b).league == lg) { sum += scores[b].legacy >= 0 ? scores[b].total : 0; n++; } if (n > 1) scores[a].leagueShare = sum / n; }
    for (int a = 0; a < N; a++) if (scores[a].leagueShare > 0) { scores[a].total = scores[a].leagueShare; scores[a].leagueShare = 0; }
}

// ---------------------------------------------------------------- 6. trade empires (doc pp. 13-14): wares, the Bird Exchange, routes and convoys, market events
namespace {
struct WareDef { std::string key, name, from; float price = 4, farMul = 1, oldMul = 1; };
struct TradeData {
    std::vector<WareDef> wares; float feedPerPearl = 4, convoy = 1.2f, piracy = 0.25f, tripLoad = 2, traderSpeed = 10, routeRepDays = 6, routeRep = 20;
    int feeDefault = 20, feeMin = 10, feeMax = 40; float eventChance = 0.25f;
};
const TradeData& TRD() {
    static TradeData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_longflight.json");
    const Json& t = j["trade"];
    auto F = [&](const char* k, float& v) { if (t[k].IsNum()) v = t[k].F(v); };
    F("feed_per_pearl", d.feedPerPearl); F("convoy", d.convoy); F("piracy", d.piracy); F("trip_load", d.tripLoad); F("trader_speed", d.traderSpeed); F("route_rep_days", d.routeRepDays); F("route_rep", d.routeRep); F("event_chance", d.eventChance);
    if (t["fee"].IsNum()) d.feeDefault = t["fee"].I(d.feeDefault);
    for (const Json& x : t["wares"].a) { WareDef w; w.key = x["key"].Str0(); w.name = x["name"].Str0(w.key); w.from = x["from"].Str0(); w.price = x["price"].F(4); w.farMul = x["far_town"].F(1); w.oldMul = x["old_town"].F(1); d.wares.push_back(w); }
    while ((int)d.wares.size() < WR_COUNT) { WareDef w; w.key = w.name = "ware"; d.wares.push_back(w); }
    return d;
}
}  // namespace
const char* WareName(int w) { return w >= 0 && w < WR_COUNT ? TRD().wares[w].name.c_str() : "?"; }
const char* MarketEventName(int e) { static const char* N[ME_COUNT] = {"", "The fleet's bad season: fish prices triple", "A Far Sea town burned: spices vanish", "A town's festival wants feathers", "The cornering of the market"}; return N[std::clamp(e, 0, ME_COUNT - 1)]; }
float World::WarePrice(int town, int ware) const {
    // what a town pays for a ware, in feed: the Far Sea's goods pay triple at the old towns; the day's market event
    if (ware < 0 || ware >= WR_COUNT || town < 0 || town >= (int)towns.size()) return 0;
    const WareDef& W = TRD().wares[ware];
    bool farTown = IsFar(towns[town].isle);
    float p = W.price * (farTown ? W.farMul : W.oldMul);
    if (market.event == ME_SPICES && ware == WR_SPICE) p = 0;
    if (market.event == ME_FEATHERS && ware == WR_FEATHER) p *= 6;
    if (market.event == ME_CORNER && market.cornerWare == ware) p *= 2;
    return p;
}
float World::ExchangePrice(int ware, bool sell) const {
    // the Bird Exchange's board: a sale fetches the best price any town offers this dawn, a purchase pays the cheapest
    float best = sell ? 0 : 1e9f; for (int t = 0; t < (int)towns.size(); t++) if (!IsFar(towns[t].isle) || FarOpen()) { float p = WarePrice(t, ware); if (p <= 0) continue; best = sell ? std::max(best, p) : std::min(best, p); }
    return best >= 1e8f ? 0 : best;
}
int World::ExchangeFee() const { return market.fee < TRD().feeMin || market.fee > TRD().feeMax ? TRD().feeDefault : market.fee; }
bool World::Exchange(int ware, int qty, bool sell) {
    // sell (or buy) wares on the Exchange at the board's price; the Market Hall's builder takes its fee and can embargo
    if (!LongFlight() || ware < 0 || ware >= WR_COUNT || qty <= 0) return false;
    if (Embargoed(cur) || (market.embargo == cur && time < market.embargoUntil)) { Say("You are embargoed: your orders don't post."); return false; }
    float price = ExchangePrice(ware, sell); if (price <= 0) { Say(std::string("No market for ") + WareName(ware) + " today."); return false; }
    int hall = wonderBy[WD_MARKET];
    float feed = price * qty, fee = hall >= 0 && hall != cur ? feed / ExchangeFee() : 0;
    if (sell) {
        if (col.wares[ware] < qty) { Say("You don't have that much."); return false; }
        col.wares[ware] -= qty;
        int pearls = (int)((feed - fee) / TRD().feedPerPearl);
        col.pearls += pearls; col.tradeEarned += pearls;
        if (hall >= 0 && hall != cur) ColOf(hall).pearls += (int)(fee / TRD().feedPerPearl + 0.5f);
        Say(TextFormat("Sold %d %s on the Exchange: %d pearls%s.", qty, WareName(ware), pearls, fee > 0 ? " (less the Hall's fee)" : ""));
    } else {
        int cost = (int)ceilf((feed + fee) / TRD().feedPerPearl);
        if (col.pearls < cost) { Say(TextFormat("That costs %d pearls.", cost)); return false; }
        col.pearls -= cost; col.wares[ware] += qty;
        if (hall >= 0 && hall != cur) ColOf(hall).pearls += (int)(fee / TRD().feedPerPearl + 0.5f);
        Say(TextFormat("Bought %d %s on the Exchange for %d pearls.", qty, WareName(ware), cost));
    }
    return true;
}
bool World::Charter(int from, int to, int ware) {
    if (!LongFlight() || from < 0 || to < 0 || from >= (int)towns.size() || to >= (int)towns.size() || from == to || ware < 0 || ware >= WR_COUNT) return false;
    if ((IsFar(towns[from].isle) || IsFar(towns[to].isle)) && !FarOpen()) { Say("That town lies in the Far Sea (it opens at the end of the first year)."); return false; }
    for (const auto& r : col.routes) if (r.from == from && r.to == to && r.ware == ware) return false;
    if (col.routes.size() >= 4) { Say("Four routes at most."); return false; }
    TradeRoute r; r.from = from; r.to = to; r.ware = ware; r.since = time; col.routes.push_back(r);
    Say(std::string("A route is chartered: ") + WareName(ware) + " from " + isles[towns[from].isle].name + " to " + isles[towns[to].isle].name + " (your Traders fly it).");
    Chronicle(cur, CK_OTHER, std::string("We chartered a trade route in ") + WareName(ware) + ".");
    return true;
}
bool World::Uncharter(int k) { if (k < 0 || k >= (int)col.routes.size()) return false; col.routes.erase(col.routes.begin() + k); return true; }
void World::StepTrade(float dt) {
    if (!LongFlight()) return;
    const TradeData& D = TRD();
    int N = (int)sides.size() + 1;
    bool dayTick = fmodf(time, DAY) < dt;
    // the market's day: an event now and then (an Augur knows a day ahead)
    if (dayTick) {
        market.event = market.next; market.next = ME_NONE;
        if (market.event == ME_CORNER) market.cornerWare = (int)(Rand() * WR_COUNT) % WR_COUNT;
        if (market.event != ME_NONE) for (int s = 0; s < N; s++) SayTo(s, std::string("The market: ") + MarketEventName(market.event) + ".");
        if (Rand() < D.eventChance) {
            market.next = 1 + (int)(Rand() * (ME_COUNT - 1)) % (ME_COUNT - 1);
            for (int s = 0; s < N; s++) { bool augur = false; for (const auto& b : ColOf(s).birds) augur |= b.alive && b.stage == BStage::Adult && b.role == Role::Augur; if (augur) SayTo(s, std::string("Your Augur foresees tomorrow's market: ") + MarketEventName(market.next) + "."); }
        }
    }
    for (int s = 0; s < N; s++) {
        Colony& C = ColOf(s);
        // where the wares come from: salt fish from wrecks and the Fleet, lamp oil from the Lighthouse, feathers from every colony
        if (dayTick) {
            int birds = 0; for (const auto& b : C.birds) birds += b.alive;
            C.wares[WR_FEATHER] += birds / 10;
            int h = HomeOf(s); IsleType ht = h >= 0 && h < (int)isles.size() ? isles[h].type : IsleType::Islet;
            if (ht == IsleType::Shipwreck) C.wares[WR_SALTFISH] += 2;
            if (ht == IsleType::Lighthouse) C.wares[WR_OIL] += 2;
            for (int i = 0; i < (int)isles.size(); i++) if (HolderOf(i) == s) { IsleType t = isles[i].type; if (t == IsleType::IronIsland || t == IsleType::DrownedFleet || t == IsleType::Wreck) C.wares[WR_SALTFISH] += 2; }
        }
        if (dayTick && !HumanOf(s)) WithSide(s, [&] {
            for (int k = 0; k < WR_COUNT; k++) if (col.wares[k] >= 4 && ExchangePrice(k, true) > 0) Exchange(k, col.wares[k], true);
            int tr = 0; for (const auto& b : col.birds) tr += b.alive && b.stage == BStage::Adult && b.role == Role::Trader;
            if (tr > 0 && col.routes.empty() && towns.size() >= 2) {
                int bf = -1, bt = -1, bw = 0; float bs = 0;
                for (int a = 0; a < (int)towns.size(); a++) for (int b2 = 0; b2 < (int)towns.size(); b2++) if (a != b2) for (int k = 0; k < WR_COUNT; k++) {
                    if ((IsFar(towns[a].isle) || IsFar(towns[b2].isle)) && !FarOpen()) continue;
                    float sp = WarePrice(b2, k) - WarePrice(a, k) * 0.5f; if (sp > bs) { bs = sp; bf = a; bt = b2; bw = k; }
                }
                if (bf >= 0) Charter(bf, bt, bw);
            }
        });
        // the routes: the colony's Traders fly them; a round trip pays by the distance and the spread
        int traders = 0, escorts = 0; for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Adult) { traders += b.role == Role::Trader; escorts += b.role == Role::Tank || b.role == Role::Harrier; }
        int nr = (int)C.routes.size(); if (!nr) continue;
        for (auto& r : C.routes) {
            int tr = traders / nr + (&r - &C.routes[0] < traders % nr ? 1 : 0);
            r.traders = tr; if (tr <= 0) continue;
            Vector3 a = towns[r.from].dock, b = towns[r.to].dock;
            float dist = Vector3Distance(a, b), trip = std::max(30.0f, 2 * dist / D.traderSpeed);
            r.t += dt;
            if (r.t < trip) continue;
            r.t = 0; r.trips++;
            float buy = WarePrice(r.from, r.ware), sell = WarePrice(r.to, r.ware);
            float load = tr * D.tripLoad * (tr >= 3 ? D.convoy : 1.0f);   // (three or more: a convoy, 20% more)
            float gain = std::max(0.0f, (sell - buy * 0.5f)) * load * (1 + dist / 2000);
            bool robbed = tr >= 3 && pirates.on && time >= pirates.scatterUntil && escorts == 0 && Rand() < D.piracy;   // (pirates hunt convoys without an escort)
            if (robbed) { SayTo(s, "Pirates fall on your convoy: the load is lost (an escort, a Tank or a Harrier, would have held them off)."); continue; }
            int pearls = (int)(gain / D.feedPerPearl);
            C.pearls += pearls; C.tradeEarned += pearls;
            if (Embargoed(s)) C.pearls -= pearls;   // (an embargo: the towns won't take it)
            if (time - r.since > D.routeRepDays * DAY && !r.repDone) { r.repDone = true; for (int k : {r.from, r.to}) { Town& T = towns[k]; if ((int)T.rep.size() <= s) T.rep.resize(s + 1, 0); T.rep[s] = std::min(100.0f, T.rep[s] + D.routeRep); } SayTo(s, "Your route is kept a season: its towns' gulls escort it now."); }
        }
    }
}

bool World::SetHallFee(int fee) { if (!HasWonder(cur, WD_MARKET)) return false; market.fee = std::clamp(fee, TRD().feeMin, TRD().feeMax); Say(TextFormat("The Exchange's fee is 1 in %d.", market.fee)); return true; }
bool World::HallEmbargo(int side) { if (!HasWonder(cur, WD_MARKET) || side == cur || side < 0 || side > (int)sides.size() || time < market.hallEmbargoReady) return false; market.embargo = side; market.embargoUntil = time + 6 * DAY; market.hallEmbargoReady = time + 6 * DAY; for (int o = 0; o <= (int)sides.size(); o++) SayTo(o, SideName(cur) + "'s Market Hall embargoes " + SideName(side) + " on the Exchange for a season."); return true; }

// ---------------------------------------------------------------- 7. culture: the Chronicle, titles and songs (doc pp. 14-15)
namespace {
const char* SeasonWord(int s) { static const char* N[4] = {"Spring", "Summer", "Autumn", "Winter"}; return N[std::clamp(s, 0, 3)]; }
uint32_t Hash32(uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2)); h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return h; }
}  // namespace
int World::SeasonAbs() const { return seasons <= 0 ? 0 : (Year() - 1) * 4 + std::max(0, Season()); }
std::string World::ChronicleText(int side) const {
    // the book: a chapter a season, its lines in order; the dynasty's name at the top
    const Colony& C = ColOf(side);
    std::string out = "The Chronicle of " + DynastyOf(side) + " (" + SideName(side) + ", " + Founders()[FounderOf(side).def].name + ")\n";
    if (!C.speciesName.empty()) out += "a colony that became the " + C.speciesName + "\n";
    int chap = -1; int chapters = 0;
    for (const auto& l : C.chronicle) {
        int abs = (l.year - 1) * 4 + std::max(0, l.season);
        if (abs != chap) { chap = abs; chapters++; out += std::string("\n") + SeasonWord(l.season) + (l.year == 1 ? " of the first year\n" : " of the second year\n"); }
        out += TextFormat("  Day %d. ", l.day) + l.text + "\n";
    }
    for (const auto& s : C.songs) out += "\nA song of the colony: " + s.name;
    auto T = Titles(side);
    if (!T.empty()) { out += "\n\nTitles:"; for (const auto& t : T) out += " " + t.first + TextFormat(" (%+d)", t.second) + ";"; }
    out += TextFormat("\n\n%d chapters.\n", chapters);
    return out;
}
int World::ChronicleChapters(int side) const { int chap = -1, n = 0; for (const auto& l : ColOf(side).chronicle) { int abs = (l.year - 1) * 4 + std::max(0, l.season); if (abs != chap) { chap = abs; n++; } } return n; }
std::vector<std::pair<std::string, int>> World::Titles(int side) const {
    std::vector<std::pair<std::string, int>> v;
    if (!LongFlight() || side < 0 || side > (int)sides.size()) return v;
    const Colony& C = ColOf(side); int N = (int)sides.size() + 1;
    // the Fisher King: the most fish caught in a season, by anyone
    int best = 0, bestSide = -1; for (int s = 0; s < N; s++) for (int k = 0; k < 8; k++) if (ColOf(s).seasonCatch[k] > best) { best = ColOf(s).seasonCatch[k]; bestSide = s; }
    if (bestSide == side) v.push_back({"the Fisher King", 75});
    if (C.stormCrossed) v.push_back({"the Stormrider", 25});
    if (C.krakenKill) v.push_back({"the Kraken's Bane", 100});
    if (C.peacemaker) v.push_back({"the Peacemaker", 50});
    if (C.oathsBroken > 0) v.push_back({"the Oathbreaker", -50});
    int adults = 0; for (const auto& b : C.birds) adults += b.alive && b.stage == BStage::Adult;
    if (C.chicksStarved == 0 && adults >= 8) v.push_back({"the Shepherd", 50});
    bool wonder = false; for (int w = 0; w < WD_COUNT; w++) wonder |= wonderBy[w] == side;
    if (wonder) v.push_back({"the Wonder-builder", 50});
    if (C.legend == LG_DODO && C.legendAlive) v.push_back({"the Dodo's Keeper", 75});
    bool last = FounderOf(side).deaths == 0; for (int s = 0; s < N && last; s++) if (s != side && FounderOf(s).deaths == 0) last = false;
    if (last && N > 1) v.push_back({"the Last Founder", 100});
    return v;
}
int World::TitleScore(int side) const { int s = 0; for (const auto& t : Titles(side)) s += t.second; return s; }
void World::StepCulture(float dt) {
    if (!LongFlight()) return;
    int N = (int)sides.size() + 1;
    int abs = SeasonAbs();
    if (abs == lastSongSeason) return;
    int prev = lastSongSeason; lastSongSeason = abs;
    if (prev < 0) return;
    // a colony with a Drummer and fervour over 50 writes a song each season: its species' call, its best moment
    static const char* OF[CK_COUNT] = {"the Old Founder", "the Heir", "the Elders", "the New Shore", "the First Clutch", "the Raid", "the Wonder", "the War", "the League", "the Broken Oath", "the Beast", "the Storm Wall", "the New Kind", "the Season"};
    static const int RANK[CK_COUNT] = {7, 8, 3, 4, 2, 5, 10, 9, 6, 4, 8, 7, 9, 1};
    for (int s = 0; s < N; s++) {
        Colony& C = ColOf(s);
        bool drummer = false; for (const auto& b : C.birds) drummer |= b.alive && b.stage == BStage::Adult && b.role == Role::Drummer;
        if (!drummer || C.fervour <= 50) continue;
        int bestKind = CK_OTHER, br = 0;
        for (const auto& l : C.chronicle) if ((l.year - 1) * 4 + std::max(0, l.season) == prev && l.kind >= 0 && l.kind < CK_COUNT && RANK[l.kind] > br) { br = RANK[l.kind]; bestKind = l.kind; }
        Song g; g.seed = Hash32((uint32_t)(s * 131 + FounderOf(s).def), (uint32_t)(prev * 977 + C.speciesTrait[0] + 3));
        g.name = std::string("The Song of ") + OF[bestKind] + ", " + SeasonWord(prev % 4) + (prev < 4 ? " of the first year" : " of the second year");
        g.season = prev;
        C.songs.push_back(g);
        SayTo(s, "Your Drummers have made a song: " + g.name + " (it plays when your flocks fly in formation).");
        Chronicle(s, CK_OTHER, "The colony made a song: " + g.name + ".");
    }
    (void)dt;
}

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
    // ---- evolution
    {
        uint32_t I = SetGene(0, GT_QUICK, 1), II = SetGene(0, GT_QUICK, 2), III = SetGene(0, GT_QUICK, 3);
        bool up = GeneRank(Inherit(I, I, false, 0.5f, 0.5f, 0.9f), GT_QUICK) == 2 && GeneRank(Inherit(II, II, false, 0.5f, 0.5f, 0.9f), GT_QUICK) == 3 && GeneRank(Inherit(III, III, false, 0.5f, 0.5f, 0.9f), GT_QUICK) == 3;
        uint32_t mix = Inherit(SetGene(I, GT_HARDY, 1), SetGene(0, GT_FIERCE, 2), false, 0.0f, 0.0f, 0.9f);
        bool hyb = GeneRank(Inherit(I, I, true, 0.5f, 0.5f, 0.9f), GT_QUICK) == 1;
        bool mut = GeneCount(Inherit(0, 0, false, 0, 0, 0.01f)) == 1;
        check(up && GeneRank(mix, GT_FIERCE) == 2 && GeneCount(mix) <= 3 && hyb && mut,
              "traits compound: two Quick parents make Quick II, two II make III; different traits pass one from each; a hybrid line doesn't rank up; a 5% mutation adds a new trait");
        auto w = make(8);
        Bird plain; plain.genes = 0; plain.trait = -1;
        Bird q3; q3.genes = III;
        check(GeneEffects(q3, w->col).speed > 1.14f && GeneEffects(plain, w->col).speed == 1, "rank III is triple the bonus (Quick III: +15% speed)");
        for (int k = 0; k < 20; k++) { Bird& b = adult(*w, Role::Fisher, w->col.caches[0].pos); b.genes = III; }
        w->time = 30 * World::DAY; w->StepEvolution(0.1f);
        check(w->col.speciesTrait[0] == GT_QUICK && !w->col.speciesName.empty() && GeneEffects(plain, w->col).speed > 1.14f,
              "twenty birds with Quick III make a species (" + w->col.speciesName + "): every bird of the colony has the trait at III");
        // a bred colony: mothers and nest lines pass genes on through eggs
        Bird mom; mom.genes = II; mom.kin = w->me.def; Nest n; n.line = II; Bird egg; w->Conceive(egg, mom, n);
        check(GeneRank(egg.genes, GT_QUICK) == 3, "an egg of a Quick II mate in a Quick II nest line is Quick III");
    }    // ---- the Far Sea
    {
        auto w = make(8);
        int nf = 0; for (const auto& is : w->isles) nf += is.far;
        auto s4 = make(4); int n4 = 0; for (const auto& is : s4->isles) n4 += is.far;
        check(nf == 8 && n4 == 0 && w->far.fogR < w->far.wallR, TextFormat("the Long Flight's map has a Far Sea of 8 places beyond a fog (%.0f m out; the Storm Wall at %.0f m); a four-season match has none", w->far.fogR, w->far.wallR));
        Vector3 out = Vector3Add(w->far.c, {w->far.fogR + 200, 40, 0}); w->me.pos = out; w->me.st = FState::Fly; w->time = 5 * World::DAY; w->StepFarSea(0.1f);
        check(Vector2Distance({w->me.pos.x, w->me.pos.z}, {w->far.c.x, w->far.c.z}) <= w->far.fogR + 1 && !w->FarOpen(), "before it opens the fog turns a bird back");
        w->time = 23.6f * World::DAY; w->StepFarSea(0.1f);
        check(w->FarOpen() && w->far.opened, "at dusk of the last day of year one the Far Sea opens");
        Bird& lost = adult(*w, Role::Fisher, Vector3Add(w->far.c, {w->far.wallR + 50, 20, 0})); int lid = lost.id; w->StepFarSea(0.1f);
        bool dead = false; for (const auto& b : w->col.birds) if (b.id == lid) dead = !b.alive;
        check(dead, "past the Storm Wall a bird is lost");
        // the Mirror Lagoon's fish never flee
        int mz = -1; for (int z = 0; z < (int)w->eco.map->zones.size() && mz < 0; z++) { Vector3 zc = w->eco.map->zones[z].Center(); int is = w->IsleAt(zc.x, zc.z, 80); if (is >= 0 && w->isles[is].type == IsleType::MirrorLagoon) mz = z; }
        check(mz >= 0 && w->FarCatchMul(mz) > 1.4f, "the Mirror Lagoon: its fish never flee (+50% to the catch)");
        // the Roc hunts flocks of more than ten at noon
        std::vector<int> ids; Vector3 at = Vector3Add(w->far.roc, {20, -10, 0});
        for (int k = 0; k < 12; k++) ids.push_back(adult(*w, Role::Skirmisher, Vector3Add(at, {(float)k, 0, 0})).id);
        w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid);
        w->time = 30.28f * World::DAY;
        for (int q = 0; q < 200; q++) { w->StepFarSea(0.05f); for (int id : ids) if (Bird* b = w->FindBird(0, id)) b->pos = Vector3Add(at, {0, 0, 0}); w->time += 0.05f; }
        int left = 0; for (int id : ids) left += w->FindBird(0, id) != nullptr;
        check(left < 12 && w->far.rocHunting, TextFormat("the Roc hunts a flock of twelve at noon (%d left)", left));
        // its egg
        w->me.st = FState::Perched; w->me.pos = Vector3Add(w->isles[w->far.rocIsle].hill, {2, 0, 0}); w->StepFarSea(0.05f);
        bool giant = false; for (const auto& b : w->born) giant |= (b.rare & RARE_GIANT) != 0;
        check(w->col.rocEgg && giant, "a Founder on the peak while the Roc hunts takes an egg: it hatches a Giant");
        // the Drowned Fleet drifts toward the loudest colony
        Vector3 f0 = w->far.fleetC; for (int q = 0; q < 100; q++) w->StepFarSea(1.0f);
        check(Vector3Distance(f0, w->far.fleetC) > 50, "the Drowned Fleet drifts (toward the loudest colony)");
    }    // ---- Grand Projects
    {
        const auto& WD = Wonders();
        check(WD.size() == WD_COUNT && WD[WD_ROOKERY].twigs == 300 && WD[WD_MONUMENT].score == 500 && WD[WD_WORKS].sulfur == 20, "nine Grand Projects with the doc's costs");
        auto w = make(8);
        int cove = -1; for (int i = 0; i < (int)w->isles.size(); i++) if (w->isles[i].type == IsleType::KrakenCove) cove = i;
        check(cove >= 0 && w->WonderSiteOk(WD_CHAIN, cove) && !w->WonderSiteOk(WD_CHAIN, w->home) && w->WonderSiteOk(WD_MONUMENT, w->home), "each wonder has its place (the Kraken's Chain at the cove; the Monument anywhere)");
        w->me.pos = w->isles[w->home].nest; bool wrong = !w->Consecrate(WD_CHAIN);
        w->me.pos = w->isles[cove].hill; bool ok = w->Consecrate(WD_CHAIN);
        check(wrong && ok && !w->Consecrate(WD_CHAIN), "the Founder consecrates a wonder where it stands (and only at its place)");
        // a rival raises the same wonder; ours finishes first and theirs turns to shells
        w->WithSide(1, [&] { w->me.pos = w->isles[cove].hill; w->Consecrate(WD_CHAIN); if (!w->col.builds.empty()) w->col.builds.back().twigs = 40; });
        Structure* st = nullptr; for (auto& s : w->col.builds) if (s.kind == ST_WONDER) st = &s;
        st->twigs = (float)StTwigs(*st); st->shells = StShells(*st);
        w->Blast(st->pos, 1, 1);
        bool burnt = st->twigs < StTwigs(*st);
        st->twigs = (float)StTwigs(*st);
        w->col.pearls = 100; w->time = 6 * World::DAY + 1; int sh1 = w->ColOf(1).shells;
        w->StepWonders(0.1f);
        check(burnt && w->WonderBy(WD_CHAIN) == 0 && w->col.pearls == 50 && w->ColOf(1).shells > sh1, "an incendiary burns scaffolding; finished after a season (paid in pearls), it's ours: the rival's progress turns to shells");
        check(w->WonderScore(0) == 300 && w->LegacyScore(0) >= 300, "a Grand Project is 300 to the score");
        // the Chain: the kraken rises against the marked colony
        for (int k = 0; k < 4; k++) { Bird b; b.id = w->ColOf(1).nextId++; b.stage = BStage::Adult; b.role = Role::Fisher; b.hp = 60; b.hunger = 1; b.pos = w->ColOf(1).caches[0].pos; w->ColOf(1).birds.push_back(b); }
        int before = 0; for (const auto& b : w->ColOf(1).birds) before += b.alive && b.stage == BStage::Adult;
        check(w->WonderAct(WD_CHAIN, 1, {}), "the builder marks a colony for the kraken");
        w->time = 7 * World::DAY; w->StepWonders(0.1f);
        int after = 0; for (const auto& b : w->ColOf(1).birds) after += b.alive && b.stage == BStage::Adult;
        check(after < before, TextFormat("at dawn the chained kraken rises off the marked colony's island (%d of %d birds left)", after, before));
        // year two: the second tier
        w->time = 30 * World::DAY; bool raise = w->RaiseWonder(WD_CHAIN);
        for (auto& s : w->col.builds) if (s.kind == ST_WONDER) { s.twigs = (float)StTwigs(s); s.shells = StShells(s); s.startT = 0; }
        w->StepWonders(0.1f);
        check(raise && ((w->wonderRaised >> WD_CHAIN) & 1) && w->WonderScore(0) == 600, "in year two a wonder can be raised to its second tier (half the cost; its score doubles)");
    }    // ---- leagues, the Council and the Great War
    {
        auto w = std::make_unique<World>(); { MapOpts o; o.players = 3; o.seasons = 8; w->Init("taloned", 41, o); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f; }
        for (int s = 0; s < 3; s++) w->ColOf(s).pearls = 5;
        int id = w->OfferLeague(1);
        w->WithSide(1, [&] { w->AnswerOffer(1, id, true); });
        bool binding = w->ColOf(0).league >= 0 && !w->Leagued(0, 1);
        w->time += World::DAY * 1.1f;
        check(binding && w->Leagued(0, 1) && w->Truce(0, 1) && !w->Leagued(0, 2) && w->ColOf(0).pearls == 4, "a league by Herald: a pearl from each, bound in a day; leaguemates are at truce");
        // the league shares its score at the end
        w->scores.assign(3, ScoreCard{}); w->scores[0].total = 100; w->scores[1].total = 300; w->scores[2].total = 150; w->ShareLeagueScores();
        check(w->scores[0].total == 200 && w->scores[1].total == 200 && w->scores[2].total == 150, "the Reckoning's score is the league's total, shared evenly");
        // the Council
        w->time = 30 * World::DAY + 0.02f * World::DAY; w->StepCouncil(0.1f);
        bool sitting = w->council.meeting == 31;
        w->col.relics = (1u << RL_BELL) | (1u << RL_LENS);   // (weight: islands held and relics)
        bool prop = w->Propose(MO_EMBARGO, 2) && !w->Propose(MO_TITHE, -1);
        int ours = -1; for (int k = 0; k < (int)w->council.agenda.size(); k++) if (w->council.agenda[k].by == 0) ours = k;
        w->Vote(ours, true);
        w->time = 30 * World::DAY + 0.35f * World::DAY; w->StepCouncil(0.1f);
        check(sitting && prop && w->Embargoed(2) && w->council.meeting < 0, "the Council sits at dawn on day 31; one motion a player; an Embargo passes by weight (islands and relics)");
        bool traded = true; w->WithSide(2, [&] { traded = !w->towns.empty() && w->TradeAt(0, 4, G_TWIGS); });
        check(!traded, "an embargoed colony can't trade at any town");
        // leaving a league: oathbroken
        float fv = w->ColOf(0).fervour = 60; w->LeaveLeague();
        check(w->Oathbroken(0) && w->ColOf(0).fervour <= fv - 29 && w->OfferLeague(2) < 0, "leaving a league costs 30 fervour and a season oathbroken (no new leagues)");
        // the Great War
        w->council.meeting = 37; w->council.agenda.clear(); w->time = 36 * World::DAY + 0.02f * World::DAY;
        Motion m; m.kind = MO_WAR; m.by = 0; m.votes = {1, 1, 1}; w->council.agenda.push_back(m);
        w->time = 36 * World::DAY + 0.35f * World::DAY; w->StepCouncil(0.1f);
        bool declared = w->council.war;
        w->time = 37 * World::DAY + 0.5f * World::DAY; float f2 = w->ColOf(2).fervour; w->time = 38 * World::DAY; w->StepCouncil(0.1f);
        check(declared && !w->Truce(0, 2) && w->ColOf(2).fervour < f2, "the Great War: declared, it begins at dawn; no truce across the sides; war weariness costs fervour");
        w->time = w->council.warUntil + 1; w->StepCouncil(0.1f);
        check(!w->council.war, "the Great War ends (two-thirds of the fronts, a Peace, or six days)");
        // the Hunt
        w->council.hunt = 0; w->council.huntUntil = w->time + World::DAY; w->HuntKilled(0, 1);
        check(w->ColOf(1).huntScore == 300, "the Council's Hunt: the kraken's killer gets 300");
    }    // ---- trade empires
    {
        auto w = make(8);
        int oldT = -1, farT = -1; for (int t = 0; t < (int)w->towns.size(); t++) { if (w->IsFar(w->towns[t].isle)) farT = t; else oldT = t; }
        check(oldT >= 0 && farT >= 0 && w->WarePrice(oldT, WR_SPICE) > w->WarePrice(farT, WR_SPICE) * 2.5f, "the Far Sea's goods pay triple at the old towns");
        w->col.wares[WR_FEATHER] = 10; int p0 = w->col.pearls;
        bool sold = w->Exchange(WR_FEATHER, 10, true);
        check(sold && w->col.wares[WR_FEATHER] == 0 && w->col.pearls >= p0, "wares are sold on the Bird Exchange for pearls");
        w->market.event = ME_FEATHERS; float fest = w->ExchangePrice(WR_FEATHER, true); w->market.event = ME_NONE;
        check(fest > w->ExchangePrice(WR_FEATHER, true) * 5, "a festival makes feathers worth something");
        // a route flown by three Traders: a convoy pays more each trip
        for (int k = 0; k < 3; k++) adult(*w, Role::Trader, w->col.caches[0].pos);
        bool farClosed = !w->Charter(oldT, farT, WR_SALTFISH);
        w->time = 26 * World::DAY;
        bool ch = w->Charter(farT, oldT, WR_SPICE);
        int before = w->col.pearls; for (int q = 0; q < 4000; q++) w->StepTrade(0.25f);
        check(farClosed && ch && w->col.routes[0].trips > 0 && w->col.pearls > before, TextFormat("a chartered route (Far Sea spices to the old towns) pays its Traders' colony: %d trips, %d pearls", w->col.routes.empty() ? 0 : w->col.routes[0].trips, w->col.pearls - before));
        // the Market Hall's builder takes a fee
        w->wonderBy[WD_MARKET] = 1; w->col.wares[WR_OIL] = 20; int h0 = w->ColOf(1).pearls; w->Exchange(WR_OIL, 20, true);
        check(w->ColOf(1).pearls > h0, "the Market Hall's builder takes a fee on every trade");
    }    // ---- culture: the Chronicle, titles, songs
    {
        auto w = make(8);
        w->time = 2 * World::DAY; w->Chronicle(0, CK_FOUNDING, "We came to the island.");
        w->time = 8 * World::DAY; w->Chronicle(0, CK_RAID, "Rival 1 stole our eggs.");
        std::string book = w->ChronicleText(0);
        check(book.find("Spring of the first year") != std::string::npos && book.find("Summer of the first year") != std::string::npos && w->ChronicleChapters(0) == 2,
              "the Chronicle is written in chapters, a season each, with the dynasty's name at the top");
        w->col.seasonCatch[1] = 40; w->ColOf(1).seasonCatch[0] = 30; w->col.oathsBroken = 1;
        auto T = w->Titles(0); bool king = false, oath = false; for (const auto& t : T) { king |= t.first == "the Fisher King"; oath |= t.second < 0; }
        check(king && oath && w->TitleScore(0) == 75 - 50 + (w->Titles(0).size() > 2 ? w->TitleScore(0) - 25 : 0), "titles: the Fisher King (the most fish in a season), the Oathbreaker (-50)");
        // a song: a Drummer and fervour over 50, when the season turns
        adult(*w, Role::Drummer, w->col.caches[0].pos); w->col.fervour = 70;
        w->time = 3 * World::DAY; w->StepCulture(0.1f); w->time = 7 * World::DAY; w->StepCulture(0.1f);
        check(w->col.songs.size() == 1 && w->col.songs[0].name.find("The Song of") == 0, "a colony with a Drummer and fervour over 50 makes a song each season: " + (w->col.songs.empty() ? std::string("none") : w->col.songs[0].name));
        check(w->LegacyScore(0) >= w->TitleScore(0) + 10, "titles and the Chronicle's length (5 a chapter) are in the score");
    }    printf(fails ? "flight-longflight-test: %d check(s) failed\n" : "flight-longflight-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl