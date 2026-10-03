// The Flight's long match (the expansion, design doc pp. 35-51): the seasons, their events and their pull on the sea,
// the wind and the work. Everything a season does is a number in data/flight/flight_long.json.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace fl {

namespace {
struct LongData {
    std::vector<SeasonFx> seasons;
    int days[5] = {0, 0, 7, 11, 16};
    float spawningRegrow = 3, winterHoldings = 2;
    int migrationMates = 6, tunaCount = 26, tunaSize = 4;
};
const LongData& LD() {
    static LongData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& sd = j["season_days"];
    for (int k = 2; k <= 4; k++) d.days[k] = sd[std::to_string(k)].I(d.days[k]);
    for (const Json& s : j["seasons"].a) {
        SeasonFx f;
        f.name = s["name"].Str0(); f.sea = s["sea"].Str0(); f.wind = s["wind"].Str0(); f.rewards = s["rewards"].Str0(); f.event = s["event"].Str0(); f.eventWhat = s["event_what"].Str0();
        f.firstDay = s["first"].I(1); f.lastDay = s["last"].I(3);
        f.regrowNear = s["regrow_near"].F(1); f.regrowFar = s["regrow_far"].F(1); f.mateTime = s["mate_time"].F(1); f.windK = s["wind_k"].F(1); f.stormK = s["storm_k"].F(1);
        f.krakenWake = s["kraken_wake"].F(1); f.stamina = s["stamina"].F(1); f.fishDepth = s["fish_depth"].F(0); f.bombCost = s["bomb_cost"].F(1);
        f.thermalsAllDay = s["thermals_all_day"].Bool0(false); f.galeOneQuarter = s["gale_one_quarter"].Bool0(false);
        d.seasons.push_back(f);
    }
    while (d.seasons.size() < SEASON_COUNT) d.seasons.push_back(SeasonFx{});
    d.spawningRegrow = j["spawning_regrow"].F(d.spawningRegrow); d.winterHoldings = j["winter_holdings"].F(d.winterHoldings);
    d.migrationMates = j["migration_mates"].I(d.migrationMates);
    d.tunaCount = j["tuna"]["count"].I(d.tunaCount); d.tunaSize = j["tuna"]["size_min"].I(d.tunaSize);
    return d;
}
bool IsShark(const rt::Species& s) { return s.Has("apex") || s.name.find("Shark") != std::string::npos || s.name.find("tip") != std::string::npos; }
}  // namespace

const std::vector<SeasonFx>& Seasons() { return LD().seasons; }
int SeasonDays(int seasons) { return seasons >= 2 && seasons <= 4 ? LD().days[seasons] : 0; }
const char* SeasonEventName(int e) { static const char* N[EV_SEASON_COUNT] = {"The Spawning", "The Tuna Run", "The Migration", "The Long Night"}; return e >= 0 && e < EV_SEASON_COUNT ? N[e] : ""; }
float WinterHoldings() { return LD().winterHoldings; }

int World::Season() const {
    if (seasons <= 0) return -1;
    int d = GameDay();
    const auto& S = LD().seasons;
    for (int s = SEASON_COUNT - 1; s >= 0; s--) if (d >= S[s].firstDay) return s;
    return SEASON_SPRING;
}
const SeasonFx& World::SeasonNow() const {
    static SeasonFx none;
    int s = Season();
    return s < 0 ? none : LD().seasons[s];
}
float World::RegrowMul(int zone) const {
    const SeasonFx& f = SeasonNow();
    bool near = zone >= 0 && zone < (int)zoneNear.size() && zoneNear[zone];
    float m = near ? f.regrowNear : f.regrowFar;
    if (EventNow(EV_SPAWNING)) m *= LD().spawningRegrow;
    return m;
}

void World::InitSeasons() {
    seasonEvent = -1; eventUntil = 0; eventsDone = 0; tuna.clear();
    if (seasons <= 0 || !eco.map) return;
    // which grounds lie along a shore (summer thins them, the blue grows rich)
    zoneNear.assign(eco.map->zones.size(), 0);
    for (int z = 0; z < (int)eco.map->zones.size(); z++) {
        Vector3 c = eco.map->zones[z].Center();
        for (const auto& is : isles) if (Vector2Distance({c.x, c.z}, {is.c.x, is.c.z}) < is.radius + 160) { zoneNear[z] = 1; break; }
        if (!wholeMap && Vector2Distance({c.x, c.z}, {island.c.x, island.c.z}) < island.radius + 160) zoneNear[z] = 1;
    }
    // each season's event falls on a day within it (never its first: the season announces itself first)
    for (int e = 0; e < EV_SEASON_COUNT; e++) {
        const SeasonFx& f = LD().seasons[e];
        int last = std::min(f.lastDay, SeasonDays(seasons));
        if (f.firstDay > SeasonDays(seasons)) { eventDay[e] = -1; continue; }
        int lo = std::min(last, f.firstDay + 1);
        eventDay[e] = (float)(lo + (int)(Rand() * (last - lo + 1)));
    }
    matchLen = SeasonDays(seasons) * DAY;
}

void World::StepSeasons(float dt) {
    if (seasons <= 0) return;
    const LongData& D = LD();
    int day = GameDay();
    // an event begins at the dawn of its day and lasts the day
    for (int e = 0; e < EV_SEASON_COUNT; e++) {
        if ((eventsDone >> e) & 1 || eventDay[e] < 0 || day < (int)eventDay[e]) continue;
        eventsDone |= 1u << e; seasonEvent = e; eventUntil = time + DAY;
        std::string what = D.seasons[e].event + ": " + D.seasons[e].eventWhat + ".";
        for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, what);
        if (e == EV_MIGRATION) for (int s = 0; s <= (int)sides.size(); s++) ColOf(s).wildMates += D.migrationMates;
        if (e == EV_TUNA_RUN && eco.map && !eco.map->zones.empty()) {
            // a school of big fish from one edge of the map to the other
            int sp = -1; for (int i = 0; i < (int)eco.map->species.size(); i++) { const auto& s = eco.map->species[i]; if (s.size >= D.tunaSize && !IsShark(s) && !s.Has("protected") && !s.isEnemy && !s.isDiver) { sp = i; if (s.name == "Tuna") break; } }
            int zw = 0, ze = 0;
            for (int z = 0; z < (int)eco.map->zones.size(); z++) { if (eco.map->zones[z].Center().x < eco.map->zones[zw].Center().x) zw = z; if (eco.map->zones[z].Center().x > eco.map->zones[ze].Center().x) ze = z; }
            tunaFrom = eco.map->zones[zw].Center(); tunaTo = eco.map->zones[ze].Center();
            tunaFrom.y = tunaTo.y = -2.5f;
            tuna.clear();
            if (sp >= 0) for (int k = 0; k < D.tunaCount; k++) {
                Vector3 at = Vector3Add(tunaFrom, {Rand() * 16 - 8, Rand() * 2 - 1, Rand() * 16 - 8});
                int ai = SpawnFishSlot(eco, sp, eco.map->zones[zw].Clamp(at), zw);
                if (ai >= 0) { eco.agents[ai].group = 9000; tuna.push_back(ai); }
            }
        }
    }
    if (seasonEvent >= 0 && time >= eventUntil) {
        if (seasonEvent == EV_TUNA_RUN) { for (int ai : tuna) if (ai < (int)eco.agents.size()) eco.agents[ai].alive = false; tuna.clear(); }   // (the school passes on)
        seasonEvent = -1;
    }
    // the Tuna Run: the school swims across the map through the day (birds that follow it eat)
    if (EventNow(EV_TUNA_RUN)) {
        float k = std::clamp(1 - (eventUntil - time) / DAY, 0.0f, 1.0f);
        Vector3 at = Vector3Lerp(tunaFrom, tunaTo, std::min(1.0f, k * 1.15f));
        for (int ai : tuna) {
            if (ai >= (int)eco.agents.size()) continue;
            rt::Agent& a = eco.agents[ai];
            if (!a.alive) continue;
            a.home = a.goal = Vector3Add(at, {sinf(ai * 1.7f) * 10, 0, cosf(ai * 2.3f) * 10});
            int z = eco.ZoneAt(a.pos); if (z >= 0) { a.homeZone = z; }
        }
    }
    // the Spawning: the sharks sleep (down in the deep, still)
    if (EventNow(EV_SPAWNING)) for (auto& a : eco.agents) {
        if (!a.alive || a.diver >= 0) continue;
        if (IsShark(eco.map->species[a.sp])) { int z = a.zone; float floorY = z >= 0 && z < (int)eco.map->zones.size() ? eco.map->zones[z].y0 + 0.5f : -15.0f; a.pos.y = floorY; a.vel = {0, 0, 0}; a.target = -1; }
    }
    // autumn's gales: from one quarter for a day at a time
    if (SeasonNow().galeOneQuarter) {
        int d = GameDay(); float h = sinf(d * 12.9898f + 3.1f) * 43758.5453f; float a = (h - floorf(h)) * 2 * PI;
        wind.dir = wind.nextDir = {cosf(a), sinf(a)};
    }
    (void)dt;
}


// ---------------------------------------------------------------- decrees (doc pp. 36-38)
namespace {
std::vector<DecreeDef> LoadDecrees() {
    std::vector<DecreeDef> v;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    auto fill = [](const Json& d, DecreeFx& f, const char* pre) {
        auto K = [&](const char* k) { return std::string(pre) + k; };
        f.catchK = d[K("catch")].F(f.catchK); f.splash = d[K("splash")].F(f.splash); f.grow = d[K("grow")].F(f.grow); f.spoil = d[K("spoil")].F(f.spoil);
        f.build = d[K("build")].F(f.build); f.trade = d[K("trade")].F(f.trade); f.flockSpeed = d[K("flock_speed")].F(f.flockSpeed); f.chill = d[K("chill")].F(f.chill);
        f.guano = d[K("guano")].F(f.guano); f.mateTime = d[K("mate_time")].F(f.mateTime); f.morale = d[K("morale")].F(f.morale); f.grudge = d[K("grudge")].F(f.grudge);
        f.heal = d[K("heal")].F(f.heal); f.cacheCost = d[K("cache_cost")].F(f.cacheCost);
        f.fervour = d[K("fervour")].I(f.fervour); f.tithe = d[K("tithe")].I(f.tithe); f.reputation = d[K("reputation")].I(f.reputation); f.wildJoin = d[K("wild_join")].I(f.wildJoin);
        f.hatchAll = d[K("hatch_all")].Bool0(f.hatchAll); f.noClutch = d[K("no_clutch")].Bool0(f.noClutch); f.callToArms = d[K("call_to_arms")].Bool0(f.callToArms);
        f.pearlDive = d[K("pearl_dive")].Bool0(f.pearlDive); f.noConvert = d[K("no_convert")].Bool0(f.noConvert); f.scoutsExact = d[K("scouts_exact")].Bool0(f.scoutsExact);
        f.scoutsLate = d[K("scouts_late")].Bool0(f.scoutsLate); f.noRaids = d[K("no_raids")].Bool0(f.noRaids); f.noFormation = d[K("no_formation")].Bool0(f.noFormation);
        f.dangersIgnore = d[K("dangers_ignore")].Bool0(f.dangersIgnore); f.extraEgg = d[K("extra_egg")].Bool0(f.extraEgg); f.noMates = d[K("no_mates")].Bool0(f.noMates);
        f.visible = d[K("visible")].Bool0(f.visible); f.hidden = d[K("hidden")].Bool0(f.hidden); f.scoutsBlind = d[K("scouts_blind")].Bool0(f.scoutsBlind);
        f.noDesert = d[K("no_desert")].Bool0(f.noDesert); f.thermalHome = d[K("thermal_home")].Bool0(f.thermalHome); f.salvage = d[K("salvage")].Bool0(f.salvage);
        f.silentRaids = d[K("silent_raids")].Bool0(f.silentRaids); f.tradersKnown = d[K("traders_known")].Bool0(f.tradersKnown); f.rest = d[K("rest")].Bool0(f.rest);
    };
    for (const Json& d : j["decrees"].a) {
        DecreeDef x; x.key = d["key"].Str0(); x.name = d["name"].Str0(x.key); x.effect = d["effect"].Str0(); x.tradeoff = d["tradeoff"].Str0();
        fill(d, x.fx, ""); fill(d, x.tomorrow, "tomorrow_");
        v.push_back(x);
    }
    return v;
}
}  // namespace
const std::vector<DecreeDef>& Decrees() { static std::vector<DecreeDef> v = LoadDecrees(); return v; }
int DecreeIndex(const std::string& key) { const auto& v = Decrees(); for (int i = 0; i < (int)v.size(); i++) if (v[i].key == key) return i; return -1; }

const DecreeFx& World::DecreeOf(int side) const {
    static thread_local DecreeFx slot[8];
    static const DecreeFx none;
    if (seasons <= 0 || side < 0 || side > 7) return none;
    const Colony& C = ColOf(side);
    const auto& D = Decrees();
    DecreeFx f = C.decree >= 0 && C.decree < (int)D.size() ? D[C.decree].fx : DecreeFx{};
    if (C.yesterday >= 0 && C.yesterday < (int)D.size()) {   // (yesterday's after-effects: a tired colony, no clutches)
        const DecreeFx& y = D[C.yesterday].tomorrow;
        f.catchK *= y.catchK; f.noClutch |= y.noClutch;
    }
    slot[side] = f;
    return slot[side];
}

int World::BotDecree() const {
    const Colony& C = col;
    const auto& D = Decrees();
    float food = DaysOfFood();
    int chicks = 0, eggs = 0, mates = 0; for (const auto& b : C.birds) if (b.alive) { chicks += b.stage == BStage::Chick; eggs += b.stage == BStage::Egg; mates += b.stage == BStage::Mate; }
    bool threat = false; for (int s = 0; s <= (int)sides.size(); s++) if (s != cur) for (const auto& f : ColOf(s).flocks) if (!f.retreating && Vector3Distance(f.pos, C.caches.empty() ? me.pos : C.caches[0].pos) < 300) threat = true;
    bool building = false; for (const auto& n : C.nests) building |= !n.built; for (const auto& s : C.builds) building |= !s.built;
    int best = -1; float bs = -1e9f;
    for (int k = 0; k < 3; k++) {
        int i = C.offer[k]; if (i < 0 || i >= (int)D.size()) continue;
        const std::string& key = D[i].key; float v = 0;
        if (key == "full_nets") v = food < 0.8f ? 3 : 1.2f;
        else if (key == "salvage") v = food < 0.6f ? 2 : 0.6f;
        else if (key == "feast_day") v = chicks >= 4 && food > 1 ? 2 : -1;
        else if (key == "hatching_moon") v = eggs >= 3 ? 1.6f : -0.5f;
        else if (key == "brood_day") v = mates >= 2 && food > 0.8f ? 1.8f : 0.2f;
        else if (key == "egg_watch") v = C.lastRaider >= 0 ? 2.2f : 0.3f;
        else if (key == "grudge") v = C.lastRaider >= 0 ? 1.5f : -0.5f;
        else if (key == "sermon") v = C.fervour < 30 ? 2 : 0.4f;
        else if (key == "builders_day") v = building ? 1.6f : 0.2f;
        else if (key == "deep_dive" || key == "tithe") v = C.pearls < 6 ? 1.3f : 0.4f;
        else if (key == "market_day") v = Count(BStage::Adult, Role::Trader) > 0 ? 1.4f : 0.1f;
        else if (key == "open_skies") v = C.flocks.empty() ? 0 : 1.1f;
        else if (key == "call_to_arms") v = threat ? 2.6f : -2;
        else if (key == "mutiny_watch") v = food < 0.3f && Count(BStage::Adult) > 6 ? 1.2f : -0.5f;
        else if (key == "offerings") v = 0.5f;
        else if (key == "day_of_rest") v = 0.8f;
        else v = 0.3f;
        if (v > bs) { bs = v; best = k; }
    }
    return best;
}

bool World::PickDecree(int k) {
    Colony& C = col;
    if (seasons <= 0 || k < 0 || k > 2 || C.decree >= 0 || C.offer[k] < 0) return false;
    const auto& D = Decrees();
    int i = C.offer[k];
    C.decree = i; C.decreesUsed |= 1u << i;
    const DecreeFx& f = D[i].fx;
    C.fervour = std::clamp(C.fervour + (float)f.fervour, 0.0f, 100.0f);
    if (f.reputation) for (auto& t : towns) if (cur < (int)t.rep.size()) t.rep[cur] = std::clamp(t.rep[cur] + (float)f.reputation, -100.0f, 100.0f);
    if (f.cacheCost > 0) for (auto& c : C.caches) { int lose = (int)ceilf(c.fish.size() * f.cacheCost); for (int q = 0; q < lose && !c.fish.empty(); q++) c.fish.pop_back(); }
    if (f.wildJoin > 0 && !C.caches.empty()) for (int q = 0; q < f.wildJoin; q++) {   // (wild birds land and stay)
        Bird b; b.id = C.nextId++; b.stage = BStage::Adult; b.role = Role::Fisher; b.hp = RoleOf(Role::Fisher).hp; b.fight = 25; b.hunger = 0.8f; b.pos = Vector3Add(C.caches[0].pos, {(float)q, 2, 1}); born.push_back(b);
    }
    if (f.visible) for (int s = 0; s <= (int)sides.size(); s++) if (s != cur) { int h = home; WithSide(s, [&] { if (h >= 0 && h < (int)know.isle.size()) know.isle[h] = std::max<uint8_t>(know.isle[h], 2); }); }
    Say(TextFormat("Today's decree: %s. %s%s%s", D[i].name.c_str(), D[i].effect.c_str(), D[i].tradeoff.empty() ? "" : "; ", D[i].tradeoff.c_str()));
    return true;
}

void World::StepDecrees(float dt) {
    if (seasons <= 0) return;
    const auto& D = Decrees();
    int n = (int)D.size(), day = GameDay();
    int rest = DecreeIndex("day_of_rest");
    for (int s = 0; s <= (int)sides.size(); s++) {
        WithSide(s, [&] {
            Colony& C = col;
            if (C.dealtDay != day) {   // dawn: yesterday's decree passes; three new ones from the deck (no repeats in a match)
                C.yesterday = C.decree; C.decree = -1; C.dealtDay = day;
                std::vector<int> pool; for (int i = 0; i < n && i < 32; i++) if (!((C.decreesUsed >> i) & 1) && i != rest) pool.push_back(i);
                for (int k = 0; k < 3; k++) {
                    if (pool.empty()) { C.offer[k] = k == 0 && rest >= 0 ? rest : -1; continue; }
                    int q = (int)(Rand() * pool.size()) % (int)pool.size(); C.offer[k] = pool[q]; pool.erase(pool.begin() + q);
                }
                if (rest >= 0 && C.offer[2] >= 0 && C.offer[0] != rest && C.offer[1] != rest && C.offer[2] != rest && pool.size() < 2) C.offer[2] = rest;
                if (BotFlown(s) || !HumanOf(s)) { int k = BotDecree(); if (k >= 0) PickDecree(k); }
                else Say("Dawn: pick today's decree (one of three; the colony panel, or D).");
            }
            // a person who hasn't picked by mid-morning rests the day
            if (C.decree < 0 && time - (day - 1) * DAY > DAY * 0.3f) {
                int k = -1; for (int q = 0; q < 3; q++) if (C.offer[q] == rest) k = q;
                if (k >= 0) PickDecree(k); else { C.decree = rest; }
            }
            // Salvage: the day's corpses on your grounds come ashore as feed
            if (C.decree >= 0 && C.decree < n && D[C.decree].fx.salvage && !C.caches.empty()) {
                C.salvageT += dt;
                if (C.salvageT >= DAY * 0.05f) { C.salvageT = 0; if ((int)C.caches[0].fish.size() < CacheCap()) C.caches[0].fish.push_back({eco.map && !eco.map->species.empty() ? 0 : -1, 1, 0}); }
            }
        });
    }
}
// ---------------------------------------------------------------- Founder perks (doc p38)
namespace {
struct PerkData { std::vector<PerkDef> perks; std::vector<int> days{3, 7, 11}; };
const PerkData& PD() {
    static PerkData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    if (j["perk_days"].IsArr()) { d.days.clear(); for (const Json& x : j["perk_days"].a) d.days.push_back(x.I()); }
    for (const Json& p : j["perks"].a) {
        PerkDef x; x.key = p["key"].Str0(); x.name = p["name"].Str0(x.key); x.effect = p["effect"].Str0();
        x.stamina = p["stamina"].F(1); x.attack = p["attack"].F(1); x.ledAttack = p["led_attack"].F(1); x.chickGrow = p["chick_grow"].F(1); x.pearl = p["pearl"].F(0);
        x.barter = p["barter"].F(1); x.sightM = p["sight_m"].F(0); x.carry = p["carry"].I(0);
        x.sharpEyes = p["sharp_eyes"].Bool0(false); x.noRout = p["no_rout"].Bool0(false); x.weatherSense = p["weather_sense"].Bool0(false);
        x.nineLives = p["nine_lives"].Bool0(false); x.thiefsEye = p["thiefs_eye"].Bool0(false); x.oldSalt = p["old_salt"].Bool0(false);
        d.perks.push_back(x);
    }
    return d;
}
}  // namespace
const std::vector<PerkDef>& Perks() { return PD().perks; }
const std::vector<int>& PerkDays() { return PD().days; }
int PerkIndex(const std::string& key) { const auto& v = Perks(); for (int i = 0; i < (int)v.size(); i++) if (v[i].key == key) return i; return -1; }
PerkDef PerkSum(uint32_t bits) {
    PerkDef s;
    if (!bits) return s;
    const auto& v = Perks();
    for (int i = 0; i < (int)v.size() && i < 32; i++) if ((bits >> i) & 1) {
        const PerkDef& p = v[i];
        s.stamina *= p.stamina; s.attack *= p.attack; s.ledAttack *= p.ledAttack; s.chickGrow *= p.chickGrow; s.barter *= p.barter;
        s.pearl = std::max(s.pearl, p.pearl); s.carry += p.carry;
        if (p.chickGrow > 1) s.sightM = std::max(s.sightM, p.sightM);
        s.sharpEyes |= p.sharpEyes; s.noRout |= p.noRout; s.weatherSense |= p.weatherSense; s.nineLives |= p.nineLives; s.thiefsEye |= p.thiefsEye; s.oldSalt |= p.oldSalt;
    }
    return s;
}

int World::BotPerk() const {
    // a bot's build: fighters and breath first, then the colony's growth, then the rest
    static const char* PREF[] = {"hooked_beak", "broad_wings", "mothers_instinct", "loud_voice", "iron_talons", "pearl_diver", "nine_lives", "sharp_eyes", "weather_sense", "diplomat", "thiefs_eye", "old_salt"};
    int best = -1, br = 99;
    for (int k = 0; k < 3; k++) {
        int i = me.perkOffer[k]; if (i < 0) continue;
        for (int r = 0; r < 12; r++) if (Perks()[i].key == PREF[r] && r < br) { br = r; best = k; }
    }
    return best >= 0 ? best : (me.perkOffer[0] >= 0 ? 0 : -1);
}
bool World::PickPerk(int k) {
    Founder& F = me;
    if (k < 0 || k > 2 || F.perkOffer[k] < 0) return false;
    int i = F.perkOffer[k];
    F.perks |= 1u << i; F.perkLevel++;
    for (int& o : F.perkOffer) o = -1;
    if (Perks()[i].stamina > 1) F.stamina = Def().stamina * PerkSum(F.perks).stamina;
    Say(TextFormat("The Founder grows: %s (%s).", Perks()[i].name.c_str(), Perks()[i].effect.c_str()));
    return true;
}

void World::StepPerks(float dt) {
    if (seasons <= 0) return;
    const auto& days = PerkDays();
    int day = GameDay();
    int due = 0; for (int d : days) due += day >= d;
    for (int s = 0; s <= (int)sides.size(); s++) {
        WithSide(s, [&] {
            Founder& F = me;
            if (F.perkLevel < due && F.perkOffer[0] < 0) {   // (a level: three perks it doesn't have)
                std::vector<int> pool; for (int i = 0; i < (int)Perks().size() && i < 32; i++) if (!((F.perks >> i) & 1)) pool.push_back(i);
                for (int k = 0; k < 3 && !pool.empty(); k++) { int q = (int)(Rand() * pool.size()) % (int)pool.size(); F.perkOffer[k] = pool[q]; pool.erase(pool.begin() + q); }
                if (BotFlown(s) || !HumanOf(s)) { int k = BotPerk(); if (k >= 0) PickPerk(k); }
                else Say("The Founder has grown: pick a perk (one of three).");
            }
            if (F.perkOffer[0] >= 0 && day > days[std::min(F.perkLevel, (int)days.size() - 1)]) PickPerk(0);   // (not picked within its day: the first)
            PerkDef P = PerkSum(F.perks);
            // Weather Sense: storms warned a day ahead
            if (P.weatherSense && weather.next - time < DAY && weather.next > time && F.stormWarned != weather.next) { F.stormWarned = weather.next; Say("Weather sense: a storm comes within the day."); }
            // Old Salt: at dawn, where tomorrow's fish will be
            if (P.oldSalt && F.saltDay != day) {
                F.saltDay = day;
                int best = -1; float bv = -1;
                for (int z = 0; z < (int)eco.map->zones.size(); z++) { float v = StockOf(z) * RegrowMul(z); if (v > bv) { bv = v; best = z; } }
                if (best >= 0) Say("Old salt: the fish will run thickest at " + eco.map->zones[best].name + " tomorrow.");
            }
            // Sharp Eyes and Thief's Eye: what the Founder flies near is known exactly (a Scout's eye at any height; caches through walls)
            if ((P.sharpEyes || P.thiefsEye) && wholeMap && !mirror) {
                float r = P.thiefsEye ? std::max(P.sightM, 150.0f) : 150.0f;
                for (int i = 0; i < (int)isles.size(); i++) if (i != home && Vector2Distance({me.pos.x, me.pos.z}, {isles[i].c.x, isles[i].c.z}) < isles[i].radius + r && fmodf(time, 1.0f) < dt) {
                    Sighting x = TrueSighting(i); know.sight[i] = x; if (know.isle[i] < 1) know.isle[i] = 1;
                }
            }
        });
    }
}
// ---------------------------------------------------------------- --flight-long-test (the expansion's long match)
int RunFlightLongTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, the long match: seasons and their events (doc pp. 35-36)\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    const auto& S = Seasons();
    check(S.size() == 4 && S[0].name == "Spring" && S[3].name == "Winter" && S[1].firstDay == 6 && S[2].firstDay == 12 && S[3].firstDay == 18,
          "four seasons: Spring 1-5, Summer 6-11, Autumn 12-17, Winter 18-24 (the doc's days stretched to fit the colony's growth)");
    check(SeasonDays(2) == 11 && SeasonDays(3) == 17 && SeasonDays(4) == 24, "a match by seasons: two 11 days, three 17, four 24");
    auto make = [](int seasons, uint32_t seed = 41) { auto w = std::make_unique<World>(); MapOpts o; o.players = 2; o.seasons = seasons; w->Init("taloned", seed, o); return w; };
    {
        auto w = make(4);
        bool ok = w->matchLen == 24 * World::DAY;
        int got[5] = {}; int di = 0; for (int d : {1, 8, 14, 20}) { w->time = (d - 0.5f) * World::DAY; got[di++] = w->Season(); }
        check(ok && got[0] == SEASON_SPRING && got[1] == SEASON_SUMMER && got[2] == SEASON_AUTUMN && got[3] == SEASON_WINTER, "a four-season match runs 24 days; days 1, 8, 14 and 20 fall in spring, summer, autumn and winter");
        auto s = make(0);
        check(s->Season() == -1 && s->matchLen == 0 && s->RegrowMul(0) == 1, "a standard match has no seasons and no limit");
        // the season's pull on the sea, the wind and the work
        int nearZ = -1, farZ = -1; for (int z = 0; z < (int)w->zoneNear.size(); z++) { if (w->zoneNear[z] && nearZ < 0) nearZ = z; if (!w->zoneNear[z] && farZ < 0) farZ = z; }
        w->time = 8.5f * World::DAY; w->seasonEvent = -1;
        check(nearZ >= 0 && farZ >= 0 && w->RegrowMul(nearZ) < 1 && w->RegrowMul(farZ) > 1, TextFormat("summer thins the grounds near the shores (x%.1f) and fattens the blue (x%.1f)", w->RegrowMul(nearZ), w->RegrowMul(farZ)));
        w->time = 14.5f * World::DAY; float autumnWind = w->SeasonNow().windK; w->time = 1.5f * World::DAY; float springWind = w->SeasonNow().windK;
        w->time = 20.5f * World::DAY;
        check(autumnWind > 1.4f && springWind < 0.8f && w->SeasonNow().stamina > 1.2f && w->SeasonNow().fishDepth > 1, "spring's wind is light and autumn's a gale; winter costs breath and sends the fish deep");
    }
    {   // the events, each once on its day
        auto w = make(4, 7);
        w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        int fired[EV_SEASON_COUNT] = {}; bool darkLongNight = true, sharksDeep = true, tunaMoved = false; int tunaBorn = 0, matesBefore = w->ColOf(0).wildMates, matesAfter = 0; float tunaX0 = 0;
        int last = -1;
        for (float t = 0; t < World::DAY * 24 && !w->over; t += 0.25f) {
            w->time += 0.25f; w->StepSeasons(0.25f);
            if (w->seasonEvent != last && w->seasonEvent >= 0) { fired[w->seasonEvent]++; if (w->seasonEvent == EV_MIGRATION) matesAfter = w->ColOf(0).wildMates;
                if (w->seasonEvent == EV_TUNA_RUN) { tunaBorn = (int)w->tuna.size(); if (!w->tuna.empty()) tunaX0 = w->eco.agents[w->tuna[0]].home.x; } }
            last = w->seasonEvent;
            if (w->EventNow(EV_LONG_NIGHT)) { float ph = w->DayPhase(); darkLongNight &= ph < 0.18f || ph > 0.86f; }
            if (w->EventNow(EV_SPAWNING)) for (const auto& a : w->eco.agents) if (a.alive && a.diver < 0 && IsShark(w->eco.map->species[a.sp]) && a.zone >= 0 && a.pos.y > w->eco.map->zones[a.zone].y0 + 1.5f) sharksDeep = false;
            if (w->EventNow(EV_TUNA_RUN) && !w->tuna.empty() && w->eco.agents[w->tuna[0]].home.x > tunaX0 + 100) tunaMoved = true;
        }
        check(fired[0] == 1 && fired[1] == 1 && fired[2] == 1 && fired[3] == 1, TextFormat("each season's event comes once, on a day within it (days %.0f, %.0f, %.0f, %.0f)", w->eventDay[0], w->eventDay[1], w->eventDay[2], w->eventDay[3]));
        check(sharksDeep, "the Spawning: the sharks sleep in the deep for the day");
        check(tunaBorn >= 10 && tunaMoved && w->tuna.empty(), TextFormat("the Tuna Run: a school of %d big fish crosses the map in a day, then moves on", tunaBorn));
        check(matesAfter > matesBefore, TextFormat("the Migration: wild mates flood in (%d to %d) and come free for the day", matesBefore, matesAfter));
        check(darkLongNight, "the Long Night: a whole game day of darkness");
    }
    {   // the Migration: a built nest with an empty bowl calls a mate
        auto w = make(4, 9);
        Colony& C = w->col; Nest& n = C.nests[0]; n.built = true; n.mate = -1; n.mateT = -1; n.bowl = 0;
        for (auto& b : C.birds) if (b.stage == BStage::Mate) b.alive = false;
        w->seasonEvent = EV_MIGRATION; w->eventUntil = w->time + World::DAY;
        w->StepColony(0.1f);
        check(n.mateT >= 0, "during the Migration a nest's empty bowl still calls a mate");
    }
    {   // winter's holdings count double at the end of a four-season match
        auto w = make(4, 13);
        w->time = 8 * World::DAY; ScoreCard a = w->Score(0);
        w->time = 20 * World::DAY; ScoreCard b = w->Score(0);
        check(a.nests > 0 && b.nests == (int)lroundf(a.nests * WinterHoldings()), TextFormat("Winter's nests count double (%d in summer, %d in winter)", a.nests, b.nests));
    }
    {   // two bot colonies through a two-season match: it ends on its last day
        auto w = make(2, 21);
        w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0});
        float t0 = 0;
        for (; t0 < World::DAY * 12 && !w->over; t0 += 0.1f) { w->BotGovern(0.1f); w->BotWar(0, 0.1f); w->Step(0.1f, FounderInput{}); }
        check(w->over && fabsf(w->time - 11 * World::DAY) < 2, TextFormat("a two-season bot match ends on day 11 (%s; %d and %d birds)", w->overReason.c_str(), w->Alive(), (int)w->ColOf(1).birds.size()));
    }
    // ---- decrees (doc pp. 36-38)
    {
        const auto& D = Decrees();
        std::map<std::string, int> keys; bool uniq = true; for (const auto& d : D) uniq &= ++keys[d.key] == 1;
        check(D.size() == 24 && uniq && DecreeIndex("day_of_rest") >= 0, TextFormat("the deck holds 24 decrees (%d), each its own", (int)D.size()));
        auto w = make(4, 17);
        w->ape.isle = -1; w->kraken.isle = -1;
        w->time = 0.05f * World::DAY; w->StepDecrees(0.1f);
        Colony& C = w->col;
        bool dealt = C.offer[0] >= 0 && C.offer[1] >= 0 && C.offer[2] >= 0 && C.offer[0] != C.offer[1] && C.offer[1] != C.offer[2] && C.offer[0] != C.offer[2] && C.decree < 0;
        bool botsPicked = true; for (int s = 1; s <= (int)w->sides.size(); s++) botsPicked &= w->ColOf(s).decree >= 0;
        check(dealt && botsPicked, "at dawn every colony is dealt three different decrees; the bots choose at once, you choose");
        w->time = 0.4f * World::DAY; w->StepDecrees(0.1f);
        check(C.decree == DecreeIndex("day_of_rest"), "no choice by mid-morning: a Day of Rest");
        // a match of picks: never the same decree twice (the Day of Rest aside)
        std::map<int, int> seen; bool repeat = false;
        for (int day = 2; day <= 24; day++) { w->time = (day - 1 + 0.05f) * World::DAY; w->StepDecrees(0.1f); if (w->PickDecree(0) && C.decree != DecreeIndex("day_of_rest") && ++seen[C.decree] > 1) repeat = true; }
        check(!repeat && seen.size() >= 12, TextFormat("24 days of decrees, %d different, none repeated", (int)seen.size()));
        // their effects: catch, after-effects, the one-off costs and gifts, visibility
        auto force = [&](World& v, const char* key) { Colony& K = v.col; K.yesterday = K.decree; K.decree = -1; K.offer[0] = DecreeIndex(key); K.offer[1] = K.offer[2] = -1; return v.PickDecree(0); };
        auto v = make(4, 23); v->ape.isle = -1; v->kraken.isle = -1;
        force(*v, "full_nets"); float fn = v->DecreeNow().catchK, splash = v->DecreeNow().splash;
        force(*v, "call_to_arms"); bool arms = v->DecreeNow().callToArms && v->DecreeNow().catchK == 0;
        force(*v, "day_of_rest"); float tired = v->DecreeNow().catchK;
        check(fn > 1.25f && splash >= 2 && arms && fabsf(tired - 0.8f) < 0.01f, "Full Nets: +30% catch and twice the splash; Call to Arms: nobody fishes, and tomorrow they're tired (-20%)");
        force(*v, "hatching_moon"); force(*v, "day_of_rest");
        check(v->DecreeNow().noClutch, "the day after a Hatching Moon: no new clutches");
        for (int k = 0; k < 20; k++) v->col.caches[0].fish.push_back({0, 2, 0});
        size_t before = v->col.caches[0].fish.size(); force(*v, "offerings");
        check(v->col.caches[0].fish.size() == before - (size_t)ceilf(before * 0.1f) && v->DecreeNow().dangersIgnore, TextFormat("Offerings: 10%% of the caches (%d to %d), and the dangers ignore you", (int)before, (int)v->col.caches[0].fish.size()));
        int birds = (int)v->born.size(); force(*v, "hospitality");
        check((int)v->born.size() == birds + 2, "Hospitality: wild birds land and join");
        force(*v, "lighthouse"); bool lit = true; for (int s = 1; s <= (int)v->sides.size(); s++) { int h = v->home; v->WithSide(s, [&] { lit &= v->know.isle[h] == 2; }); }
        check(lit, "Lighthouse: every other colony knows your island");
        force(*v, "fog_bank"); Sighting fog = v->TrueSighting(v->home);
        force(*v, "day_of_rest"); Sighting clear = v->TrueSighting(v->home);
        check(fog.nests == 0 && fog.birds == 0 && clear.nests > 0, TextFormat("Fog Bank: your island shows nothing to scouts (%d nests seen; %d on a clear day)", fog.nests, clear.nests));
        force(*v, "feast_day"); float spoilFeast = v->SpoilDays(); force(*v, "day_of_rest");
        check(spoilFeast < v->SpoilDays() * 0.6f, "Feast Day: the caches spoil twice as fast");
    }
    {   // a two-season match: every colony has a decree every day
        auto w = make(2, 31);
        w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0});
        int days = 0, decreed = 0, lastDay = 0;
        for (float t = 0; t < World::DAY * 11 && !w->over; t += 0.1f) {
            w->BotGovern(0.1f); w->BotWar(0, 0.1f); w->Step(0.1f, FounderInput{});
            if (w->GameDay() != lastDay && fmodf(w->time, World::DAY) > World::DAY * 0.5f) { lastDay = w->GameDay(); days++; for (int s = 0; s <= (int)w->sides.size(); s++) decreed += w->ColOf(s).decree >= 0; }
        }
        check(decreed == days * ((int)w->sides.size() + 1) && days >= 10, TextFormat("a two-season bot match: a decree for every colony every day (%d of %d)", decreed, days * ((int)w->sides.size() + 1)));
    }    // ---- Founder perks (doc p38)
    {
        check(Perks().size() == 12 && PerkDays().size() == 3 && PerkDays()[0] == 5 && PerkDays()[2] == 17, "twelve Founder perks, offered at days 5, 11 and 17");
        auto w = make(4, 51); w->ape.isle = -1; w->kraken.isle = -1;
        w->time = 3.5f * World::DAY; w->StepPerks(0.1f);
        bool none = w->me.perkOffer[0] < 0;
        w->time = 4.2f * World::DAY; w->StepPerks(0.1f);
        bool offered = w->me.perkOffer[0] >= 0 && w->me.perkOffer[1] >= 0 && w->me.perkOffer[2] >= 0;
        bool botsGrew = true; for (int s = 1; s <= (int)w->sides.size(); s++) botsGrew &= w->FounderOf(s).perkLevel == 1 && w->FounderOf(s).perks != 0;
        check(none && offered && botsGrew, "on day 5 the Founder is offered three perks (none before); the bots pick at once");
        auto give = [&](const char* key) { w->me.perkOffer[0] = PerkIndex(key); w->me.perkOffer[1] = w->me.perkOffer[2] = -1; return w->PickPerk(0); };
        int carry0 = w->me.Carry(w->Def()); give("iron_talons");
        check(w->me.Carry(w->Def()) == carry0 + 1, TextFormat("Iron Talons: carry %d to %d", carry0, w->me.Carry(w->Def())));
        give("broad_wings"); give("hooked_beak");
        check(fabsf(w->PerksOf(0).stamina - 1.5f) < 0.01f && fabsf(w->PerksOf(0).attack - 1.3f) < 0.01f && fabsf(w->PerksOf(0).ledAttack - 1.1f) < 0.01f, "Broad Wings +50% stamina, Hooked Beak +30% attack and +10% for the flock it leads; perks stack");
        give("nine_lives");
        w->me.st = FState::Dead; w->RespawnNow();
        bool whole = !w->me.chick && w->me.nineUsed;
        w->me.st = FState::Dead; w->RespawnNow();
        check(whole && w->me.chick, "Nine Lives: one respawn whole; the next as a chick-leader");
        give("loud_voice");
        check(w->PerksOf(0).noRout, "Loud Voice: the Founder's flocks never rout");
    }    printf(fails ? "flight-long-test: %d check(s) failed\n" : "flight-long-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
}  // namespace fl
