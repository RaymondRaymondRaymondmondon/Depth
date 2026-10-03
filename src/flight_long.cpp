// The Flight's long match (the expansion, design doc pp. 35-51): the seasons, their events and their pull on the sea,
// the wind and the work. Everything a season does is a number in data/flight/flight_long.json.
#include "flight.h"
#include "flight_net.h"
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
    int days[9] = {0, 0, 7, 11, 16, 0, 36, 0, 48};   // (6 and 8: the Long Flight, a year and a half or two)
    float spawningRegrow = 3, winterHoldings = 2;
    int migrationMates = 6, tunaCount = 26, tunaSize = 4;
};
const LongData& LD() {
    static LongData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& sd = j["season_days"];
    for (int k : {2, 3, 4, 6, 8}) d.days[k] = sd[std::to_string(k)].I(d.days[k]);
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
int SeasonDays(int seasons) { return seasons >= 2 && seasons <= 8 ? LD().days[seasons] : 0; }
const char* SeasonEventName(int e) { static const char* N[EV_SEASON_COUNT] = {"The Spawning", "The Tuna Run", "The Migration", "The Long Night"}; return e >= 0 && e < EV_SEASON_COUNT ? N[e] : ""; }
float WinterHoldings() { return LD().winterHoldings; }

int World::Season() const {
    if (seasons <= 0) return -1;
    int d = GameDay();
    if (seasons >= 6) d = (d - 1) % LD().days[4] + 1;   // (the Long Flight: the seasons come round again in year two)
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
        eventDay2[e] = seasons >= 6 && eventDay[e] + LD().days[4] <= SeasonDays(seasons) ? eventDay[e] + LD().days[4] : -1;   // (the Long Flight: again in year two)
    }
    matchLen = SeasonDays(seasons) * DAY;
}

void World::StepSeasons(float dt) {
    if (seasons <= 0) return;
    const LongData& D = LD();
    int day = GameDay();
    // an event begins at the dawn of its day and lasts the day
    for (int k = 0; k < EV_SEASON_COUNT * 2; k++) {
        int e = k % EV_SEASON_COUNT; float on = k < EV_SEASON_COUNT ? eventDay[e] : eventDay2[e];
        if ((eventsDone >> k) & 1 || on < 0 || day < (int)on) continue;
        eventsDone |= 1u << k; seasonEvent = e; eventUntil = time + DAY * (k >= EV_SEASON_COUNT && e == EV_LONG_NIGHT ? 2 : 1);   // (year two's Long Night is two days)
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
    std::vector<int> days = PerkDays();
    if (LongFlight()) { size_t n = days.size(); for (size_t i = 0; i < n; i++) days.push_back(days[i] + YearDays()); }   // (the heir is offered perks in year two)
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
            if (Count(BStage::Adult, Role::Augur) > 0) { P.weatherSense = true; P.oldSalt = true; }   // (an Augur reads the sea for the whole colony)
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
// ---------------------------------------------------------------- veterans (doc p42) and mates' traits (p43)
const VetData& Veterans() {
    static VetData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& v = j["veterans"];
    d.fights = v["fights"].I(d.fights); d.bonus = v["bonus"].F(d.bonus); d.fearlessMorale = v["fearless_morale"].F(d.fearlessMorale); d.loyalAttack = v["loyal_attack"].F(d.loyalAttack);
    for (const Json& n : v["names"].a) d.names.push_back(n.Str0());
    for (const Json& t : v["traits"].a) { d.traitNames.push_back(t["name"].Str0()); d.traitWhat.push_back(t["effect"].Str0()); }
    if (d.names.empty()) d.names.push_back("Old Gray");
    while (d.traitNames.size() < VT_COUNT) { d.traitNames.push_back("?"); d.traitWhat.push_back(""); }
    return d;
}
const std::vector<MateTraitDef>& MateTraits() {
    static std::vector<MateTraitDef> v; static bool loaded = false;
    if (loaded) return v;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    for (const Json& t : j["mate_traits"].a) {
        MateTraitDef m; m.key = t["key"].Str0(); m.name = t["name"].Str0(m.key); m.effect = t["effect"].Str0(); m.favorite = t["favorite"].Str0();
        m.hpAdd = t["hp_add"].F(0); m.hpMul = t["hp_mul"].F(1); m.speed = t["speed"].F(1); m.scout = t["scout"].F(1); m.attack = t["attack"].F(1); m.clutch = t["clutch"].I(0);
        v.push_back(m);
    }
    while (v.size() < MT_COUNT) v.push_back(MateTraitDef{});
    return v;
}
std::string World::VetLabel(const Bird& b) const {
    if (b.vet < 0) return "";
    const VetData& V = Veterans();
    return V.names[std::max(0, b.vetName) % V.names.size()] + ", " + V.traitNames[std::clamp(b.vet, 0, VT_COUNT - 1)];
}
void World::StepVeterans(float dt) {
    // a fight is an engagement it struck or was struck in: counted once the air has been quiet round it for ten seconds
    const VetData& V = Veterans();
    for (auto& b : col.birds) {
        if (!b.alive || b.stage != BStage::Adult || !IsWarrior(b.role)) continue;
        if (b.foughtT > b.countedT && time - b.foughtT > 10) {
            b.countedT = time; b.fights++;
            if (b.vet < 0 && b.fights >= V.fights) {
                b.vet = (int)(Rand() * VT_COUNT) % VT_COUNT; b.vetName = col.nextVetName++ % (int)V.names.size(); b.vetT = time;
                b.hp = std::min(MaxHp(b.role) * (1 + V.bonus), b.hp * (1 + V.bonus));
                Say(TextFormat("%s the %s has survived %d fights: a veteran (%s: %s).", V.names[b.vetName].c_str(), RoleName(b.role), b.fights, V.traitNames[b.vet].c_str(), V.traitWhat[b.vet].c_str()));
            }
        }
    }
    (void)dt;
}
// ---------------------------------------------------------------- relics, legendary birds and great events (doc pp. 46-47)
namespace {
struct ExtraData {
    std::vector<RelicDef> relics; std::vector<LegendDef> legends; std::vector<GreatDef> events;
    int relicsMax = 3; float relicSteal = 0.25f;
};
const ExtraData& XD() {
    static ExtraData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    d.relicsMax = j["relics_max"].I(d.relicsMax); d.relicSteal = j["relic_steal"].F(d.relicSteal);
    for (const Json& r : j["relics"].a) { RelicDef x; x.key = r["key"].Str0(); x.name = r["name"].Str0(); x.effect = r["effect"].Str0(); x.morale = r["morale"].F(0); x.scout = r["scout"].F(1); x.trade = r["trade"].F(1); x.striker = r["striker"].F(1); x.mateTime = r["mate_time"].F(1); d.relics.push_back(x); }
    for (const Json& r : j["legends"].a) { LegendDef x; x.key = r["key"].Str0(); x.name = r["name"].Str0(); x.effect = r["effect"].Str0(); x.score = r["score"].I(100); d.legends.push_back(x); }
    for (const Json& r : j["great_events"].a) { GreatDef x; x.key = r["key"].Str0(); x.name = r["name"].Str0(); x.what = r["what"].Str0(); x.stock = r["stock"].F(1); x.poison = r["poison"].F(0); x.days = r["days"].F(1); x.pearls = r["pearls"].I(0); x.take = r["take"].I(0); x.crowd = r["crowd"].I(60); x.loss = r["loss"].F(0); x.catchK = r["catch"].F(1); d.events.push_back(x); }
    while (d.relics.size() < RL_COUNT) d.relics.push_back(RelicDef{});
    while (d.legends.size() < LG_COUNT) d.legends.push_back(LegendDef{});
    while (d.events.size() < GE_COUNT) d.events.push_back(GreatDef{});
    return d;
}
}  // namespace
const std::vector<RelicDef>& Relics() { return XD().relics; }
const std::vector<LegendDef>& Legends() { return XD().legends; }
const std::vector<GreatDef>& GreatEvents() { return XD().events; }
int RelicsMax() { return XD().relicsMax; }
float RelicStealChance() { return XD().relicSteal; }

bool World::HasRelic(int side, int relic) const { return seasons > 0 && side >= 0 && side <= (int)sides.size() && (((ColOf(side).relics | ColOf(side).relicsKept) >> relic) & 1); }   // (a Pilgrimage's relic is kept for good)
bool World::HasLegend(int side, int legend) const { return seasons > 0 && side >= 0 && side <= (int)sides.size() && ColOf(side).legend == legend && ColOf(side).legendAlive; }
int World::RelicCount(int side) const { int n = 0; uint32_t r = ColOf(side).relics; while (r) { n += r & 1; r >>= 1; } return n; }

void World::InitRelics() {
    relicSpots.clear(); greatEvent = -1; greatDay = -1; greatUntil = 0; greatAnnounced = greatDone = false; treasure = 0; legendFree = -1;
    if (seasons <= 0 || !wholeMap) return;
    // the relics lie on the dangerous islands: the kraken's cove, skull island's summit, the volcano's rim, the wreck
    std::vector<int> pool; for (int r = 0; r < RL_COUNT; r++) pool.push_back(r);
    auto place = [&](int isle, Vector3 at) {
        if (isle < 0 || pool.empty()) return;
        int q = (int)(Rand() * pool.size()) % (int)pool.size();
        RelicSpot s; s.isle = isle; s.pos = at; s.relic = pool[q]; pool.erase(pool.begin() + q); relicSpots.push_back(s);
    };
    if (kraken.isle >= 0) place(kraken.isle, Vector3Add(isles[kraken.isle].c, {0, 2, 0}));
    if (ape.isle >= 0) place(ape.isle, Vector3Add(ape.pos, {6, 1, 4}));
    if (volcano.isle >= 0) place(volcano.isle, Vector3Add(isles[volcano.isle].hill, {8, -2, 0}));
    if (wreck.isle >= 0) place(wreck.isle, Vector3Add(isles[wreck.isle].c, {0, 2, 0}));
    // the match's one great event, on a day in its middle
    int last = SeasonDays(seasons);
    greatEvent = (int)(Rand() * GE_COUNT) % GE_COUNT;
    int lo = std::min(last, 8), hi = std::max(lo, std::min(last - 2, 20));
    greatDay = (float)(lo + (int)(Rand() * (hi - lo + 1)));
}

bool World::TakeRelic(int spot) {   // (the colony in the fields)
    if (spot < 0 || spot >= (int)relicSpots.size() || relicSpots[spot].taken || RelicCount(cur) >= XD().relicsMax) return false;
    RelicSpot& s = relicSpots[spot];
    s.taken = true; col.relics |= 1u << s.relic;
    const RelicDef& r = XD().relics[s.relic];
    Say(r.name + ": " + r.effect + ". It goes to the shrine.");
    for (int o = 0; o <= (int)sides.size(); o++) if (o != cur) SayTo(o, SideName(cur) + " has found " + r.name + ".");
    return true;
}
bool World::RecruitLegend() {   // (the Founder at the Visitor with a fish of size 3 or more)
    if (legendFree < 0 || me.carrySp < 0 || me.carrySize < 3 || Vector3Distance(me.pos, legendPos) > 10 || col.legend >= 0) return false;
    me.carrySp = -1; me.carrySize = 0;
    GiveLegend(cur, legendFree);
    return true;
}
void World::GiveLegend(int side, int legend) {
    WithSide(side, [&] {
        col.legend = legend; col.legendAlive = true;
        Say(XD().legends[legend].name + " joins the colony: " + XD().legends[legend].effect + ".");
        if (legend == LG_PHOENIX_CHICK && !col.caches.empty()) {   // (it fledges into the founder's best warrior)
            Bird b; b.id = col.nextId++; b.stage = BStage::Adult; b.role = Role::Striker; b.hp = MaxHp(b.role) * 1.15f; b.hunger = 1; b.fight = 25;
            b.vet = VT_FEARLESS; b.vetName = col.nextVetName++ % (int)Veterans().names.size(); b.fights = 3; b.pos = Vector3Add(col.caches[0].pos, {0, 3, 0}); born.push_back(b);
        }
    });
    for (int o = 0; o <= (int)sides.size(); o++) if (o != side) SayTo(o, SideName(side) + " has recruited " + XD().legends[legend].name + ".");
    legendFree = -1;
}
bool World::GreatNow(int e) const { return seasons > 0 && greatEvent == e && time < greatUntil && greatUntil > 0; }

void World::StepGreat(float dt) {
    if (seasons <= 0 || !wholeMap) return;
    const ExtraData& X = XD();
    int day = GameDay();
    int N = (int)sides.size() + 1;
    // a day ahead, an Augur reads it
    if (!greatAnnounced && greatEvent >= 0 && day >= (int)greatDay - 1) {
        greatAnnounced = true;
        for (int s = 0; s < N; s++) { bool augur = false; for (const auto& b : ColOf(s).birds) augur |= b.alive && b.stage == BStage::Adult && b.role == Role::Augur; if (augur) SayTo(s, "Your Augur reads the sea: " + X.events[greatEvent].name + " tomorrow. " + X.events[greatEvent].what + "."); }
    }
    if (!greatDone && greatEvent >= 0 && day >= (int)greatDay) {
        greatDone = true; greatUntil = time + DAY * X.events[greatEvent].days;
        const GreatDef& g = X.events[greatEvent];
        for (int s = 0; s < N; s++) SayTo(s, g.name + ": " + g.what + ".");
        switch (greatEvent) {
            case GE_RED_TIDE: {   // a ground's fish die; caches without a Keeper are poisoned
                int best = -1; float bv = -1; for (int z = 0; z < (int)stocks.size() && z < (int)eco.map->zones.size(); z++) { float v = StockOf(z); if (v > bv) { bv = v; best = z; } }
                greatZone = best;
                for (auto& st : stocks) if (st.zone == best) st.pop *= g.stock;
                for (auto& a : eco.agents) if (a.alive && a.diver < 0 && a.homeZone == best && Rand() > g.stock) a.alive = false;
                for (int s = 0; s < N; s++) {
                    Colony& C = ColOf(s); bool keeper = false; for (const auto& b : C.birds) keeper |= b.alive && b.role == Role::Keeper && b.stage == BStage::Adult;
                    if (!keeper) for (auto& c : C.caches) { int lose = (int)(c.fish.size() * g.poison); for (int q = 0; q < lose && !c.fish.empty(); q++) c.fish.pop_back(); }
                }
            } break;
            case GE_KRAKEN_WALK:
                if (kraken.isle >= 0 && !kraken.dead) { walkFrom = isles[kraken.isle].c; int t = -1; for (int k = 0; k < 20 && (t < 0 || t == kraken.isle); k++) t = (int)(Rand() * isles.size()) % (int)isles.size(); walkTo = isles[std::max(0, t)].c; }
                break;
            case GE_GREAT_STORM: weather.kind = 1; weather.t = DAY * g.days; if (wreck.isle >= 0 && !wreck.gone) wreck.sunk += 1; break;
            case GE_TREASURE: {   // a ship sinks near a colony: its hold of pearls
                int s = (int)(Rand() * N) % N; Vector3 h = isles[HomeOf(s)].c; float a = Rand() * 2 * PI;
                greatPos = {h.x + cosf(a) * (isles[HomeOf(s)].radius + 140), 0, h.z + sinf(a) * (isles[HomeOf(s)].radius + 140)}; treasure = g.pearls;
            } break;
            case GE_PLAGUE:
                for (int s = 0; s < N; s++) {
                    Colony& C = ColOf(s); int alive = 0, nurses = 0; for (const auto& b : C.birds) if (b.alive && b.stage != BStage::Egg) { alive++; nurses += b.role == Role::Nurse && b.stage == BStage::Adult; }
                    if (alive <= g.crowd || C.HasTier(Tree::Chemistry, 1) || nurses * 20 >= alive) continue;
                    int lose = (int)(alive * g.loss);
                    WithSide(s, [&] { for (auto& b : col.birds) { if (lose <= 0) break; if (b.alive && b.stage == BStage::Adult && Rand() < 0.5f) { BirdDies(b, "taken by the plague"); lose--; } } });
                }
                break;
            case GE_VISITOR: {
                legendFree = (int)(Rand() * LG_COUNT) % LG_COUNT;
                int t = -1; for (int k = 0; k < 30 && (t < 0 || IsStartType(isles[t].type)); k++) t = (int)(Rand() * isles.size()) % (int)isles.size();
                legendPos = Vector3Add(isles[std::max(0, t)].hill, {0, 3, 0});
            } break;
            default: break;
        }
    }
    // the event's day: the Kraken's Walk crosses the map; the treasure is taken; bots court the Visitor
    if (GreatNow(GE_KRAKEN_WALK)) {
        float k = 1 - (greatUntil - time) / DAY;
        Vector3 at = Vector3Lerp(walkFrom, walkTo, std::clamp(k, 0.0f, 1.0f));
        kraken.arm = {at.x, 6, at.z}; kraken.armT = 0.5f;
        for (int s = 0; s < N; s++) {
            if (HasRelic(s, RL_BEAK)) continue;
            Colony& C = ColOf(s);
            for (auto& b : C.birds) if (b.alive && b.stage == BStage::Adult && b.pos.y < 20 && Vector2Distance({b.pos.x, b.pos.z}, {at.x, at.z}) < 60 && Rand() < 0.08f * dt) WithSide(s, [&] { BirdDies(b, "taken by the walking kraken"); });
            for (int ni = 0; ni < (int)C.nests.size(); ni++) {
                Nest& n = C.nests[ni];
                if (n.pos.y >= 20 || Vector2Distance({n.pos.x, n.pos.z}, {at.x, at.z}) > 70) continue;
                for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick) && Rand() < 0.2f * dt) WithSide(s, [&] { BirdDies(b, "eaten by the walking kraken"); });
            }
        }
    }
    if (treasure > 0) {
        for (int s = 0; s < N; s++) if (BotFlown(s) || !HumanOf(s)) {   // (a bot colony near the wreck sends its divers)
            if (Vector2Distance({isles[HomeOf(s)].c.x, isles[HomeOf(s)].c.z}, {greatPos.x, greatPos.z}) < 700 && Rand() < 0.05f * dt) { int k = std::min(treasure, X.events[GE_TREASURE].take); treasure -= k; ColOf(s).pearls += k; SayTo(s, TextFormat("Your birds dive the treasure ship: %d pearls.", k)); }
        }
    }
    if (legendFree >= 0) for (int s = 0; s < N; s++) if ((BotFlown(s) || !HumanOf(s)) && ColOf(s).legend < 0 && Rand() < 0.02f * dt && Vector2Distance({isles[HomeOf(s)].c.x, isles[HomeOf(s)].c.z}, {legendPos.x, legendPos.z}) < 900) { GiveLegend(s, legendFree); break; }
    // the Keeper's Logbook: every ground's yield known
    for (int s = 0; s < N; s++) if (HasRelic(s, RL_LOGBOOK) && fmodf(time, 2.0f) < dt) WithSide(s, [&] { for (int z = 0; z < (int)know.ground.size() && z < (int)eco.map->zones.size(); z++) { know.ground[z].t = time; know.ground[z].stock = StockOf(z); } });
}

// ---------------------------------------------------------------- the neutral factions (doc p46)
namespace {
struct FactionData {
    int band = 8, cacheTake = 2, hireFish = 8, boats = 3, tributeFish = 6;
    float pHp = 400, pSpeed = 16, stealEvery = 20, frigShare = 0.5f, scatterDays = 4;
    float boatSpeed = 4, fleetFish = 0.08f, netR = 15, netBelow = 5, netS = 5, chum = 4;
    float gHp = 300, huntEvery = 45, killChance = 0.5f, gRange = 700, peaceDays = 1;
};
const FactionData& FD() {
    static FactionData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& p = j["factions"]["pirates"]; const Json& f = j["factions"]["fleet"]; const Json& g = j["factions"]["grey_wings"];
    d.band = p["band"].I(d.band); d.pHp = p["hp"].F(d.pHp); d.pSpeed = p["speed"].F(d.pSpeed); d.stealEvery = p["steal_every_s"].F(d.stealEvery); d.cacheTake = p["cache_take"].I(d.cacheTake);
    d.hireFish = p["hire_fish"].I(d.hireFish); d.frigShare = p["frigatebird_share"].F(d.frigShare); d.scatterDays = p["scatter_days"].F(d.scatterDays);
    d.boats = f["boats"].I(d.boats); d.boatSpeed = f["speed"].F(d.boatSpeed); d.fleetFish = f["fish_per_day"].F(d.fleetFish); d.netR = f["net_radius_m"].F(d.netR); d.netBelow = f["net_below_m"].F(d.netBelow); d.netS = f["net_s"].F(d.netS); d.chum = f["chum_blood"].F(d.chum);
    d.gHp = g["hp"].F(d.gHp); d.huntEvery = g["hunt_every_s"].F(d.huntEvery); d.killChance = g["kill_chance"].F(d.killChance); d.gRange = g["range_m"].F(d.gRange); d.tributeFish = g["tribute_fish"].I(d.tributeFish); d.peaceDays = g["peace_days"].F(d.peaceDays);
    return d;
}
// fish from a colony's caches (the payment for a hire or a tribute); false if it hasn't enough
bool PayFish(Colony& C, int n) {
    int have = 0; for (const auto& c : C.caches) have += (int)c.fish.size();
    if (have < n) return false;
    for (auto& c : C.caches) while (n > 0 && !c.fish.empty()) { c.fish.pop_back(); n--; }
    return true;
}
}  // namespace
int PirateHireFish() { return FD().hireFish; }
int TributeFish() { return FD().tributeFish; }

void World::InitFactions() {
    pirates = Pirates{}; fleet.clear(); grey = GreyWings{};
    if (seasons <= 0 || !wholeMap || isles.empty()) return;
    const FactionData& D = FD();
    // the pirates start at sea between the islands; the fleet off the towns; the Grey Wings on the highest crag that isn't anyone's home
    Vector3 mid{}; for (const auto& is : isles) mid = Vector3Add(mid, is.c); mid = Vector3Scale(mid, 1.0f / isles.size());
    pirates.pos = {mid.x + 80, 30, mid.z - 60}; pirates.hp = D.pHp; pirates.on = true;
    for (int k = 0; k < D.boats; k++) {
        Boat b; Vector3 base = towns.empty() ? mid : towns[k % towns.size()].dock;
        b.pos = {base.x + 60 * cosf(k * 2.1f), 0, base.z + 60 * sinf(k * 2.1f)}; b.goal = b.pos; fleet.push_back(b);
    }
    int best = -1; float hy = -1;
    for (int i = 0; i < (int)isles.size(); i++) { if (IsStartType(isles[i].type) && isles[i].start >= 0) continue; if (isles[i].hill.y > hy) { hy = isles[i].hill.y; best = i; } }
    grey.isle = best; if (best >= 0) grey.crag = Vector3Add(isles[best].hill, {0, 6, 0});
    grey.hp = D.gHp; grey.peaceUntil.assign(sides.size() + 1, 0);
}

bool World::HirePirates(int target) {   // (the colony in the fields)
    const FactionData& D = FD();
    if (!pirates.on || time < pirates.scatterUntil || target < 0 || target > (int)sides.size() || target == cur) return false;
    int cost = (int)ceilf(D.hireFish * (BendNow().steal ? D.frigShare : 1.0f));   // (the Frigatebird is their cousin: half)
    if (!PayFish(col, cost)) { Say(TextFormat("The pirates want %d fish.", cost)); return false; }
    pirates.target = target; pirates.hiredBy = cur; pirates.hireUntil = time + DAY;
    Say(TextFormat("The Frigate Pirates take %d fish: they'll raid %s for a day.", cost, SideName(target).c_str()));
    return true;
}
bool World::PayTribute() {
    const FactionData& D = FD();
    if (grey.isle < 0 || grey.dead) return false;
    if (!PayFish(col, D.tributeFish)) { Say(TextFormat("The Grey Wings want %d fish.", D.tributeFish)); return false; }
    col.tributePaid++;
    if ((int)grey.peaceUntil.size() <= cur) grey.peaceUntil.resize(cur + 1, 0);
    grey.peaceUntil[cur] = time + D.peaceDays * DAY;
    Say("The Grey Wings take the tribute: a day's peace.");
    return true;
}

void World::StepFactions(float dt) {
    if (seasons <= 0 || !wholeMap || mirror) return;
    const FactionData& D = FD();
    int N = (int)sides.size() + 1;
    // ---- the Frigate Pirates: hunt a carrying fisher (or a full cache), steal, and fight back
    if (pirates.on && time >= pirates.scatterUntil) {
        if (time > pirates.hireUntil) pirates.target = -1;
        Bird* prey = nullptr; int preySide = -1; float bd = 1e9f;
        for (int s = 0; s < N; s++) {
            if (pirates.target >= 0 && s != pirates.target) continue;
            if (BendOfSide(s).steal) continue;   // (they don't rob their cousins)
            for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && b.carrySp >= 0 && b.flock < 0) { float d = Vector3Distance(b.pos, pirates.pos); if (d < bd) { bd = d; prey = &b; preySide = s; } }
        }
        Vector3 goal = prey ? prey->pos : Vector3{pirates.pos.x + 30 * cosf(time * 0.05f), 30, pirates.pos.z + 30 * sinf(time * 0.05f)};
        pirates.stealT += dt;
        if (!prey && pirates.stealT > D.stealEvery * 3) {   // (nothing in the air: a cache)
            for (int s = 0; s < N; s++) {
                if ((pirates.target >= 0 && s != pirates.target) || BendOfSide(s).steal) continue;
                Colony& C = ColOf(s);
                if (!C.caches.empty() && C.caches[0].fish.size() > 4) { goal = C.caches[0].pos; if (Vector3Distance(pirates.pos, goal) < 8) { for (int q = 0; q < D.cacheTake && !C.caches[0].fish.empty(); q++) C.caches[0].fish.pop_back(); pirates.loot += D.cacheTake; pirates.stealT = 0; SayTo(s, "The Frigate Pirates raid your cache."); } break; }
            }
        }
        Vector3 d = Vector3Subtract(goal, pirates.pos); float l = Vector3Length(d);
        if (l > 0.5f) pirates.pos = Vector3Add(pirates.pos, Vector3Scale(d, std::min(1.0f, D.pSpeed * dt / l)));
        if (prey && bd < 8) { prey->carrySp = -1; prey->carrySize = 0; pirates.loot++; pirates.stealT = 0; if (preySide >= 0 && Rand() < 0.3f) SayTo(preySide, "The Frigate Pirates steal a fisher's catch."); }
        // a flock that closes with them fights: the captain falls at 0 HP and the band scatters
        for (int s = 0; s < N; s++) for (auto& fl : ColOf(s).flocks) {
            if (fl.members.size() < 3 || fl.retreating || Vector3Distance(fl.pos, pirates.pos) > 30) continue;
            pirates.hp -= fl.members.size() * 4 * dt;
            if (Rand() < 0.1f * dt) { Bird* v = FindBird(s, fl.members[(int)(Rand() * fl.members.size()) % fl.members.size()]); if (v) WithSide(s, [&] { BirdDies(*v, "killed by the Frigate Pirates"); }); }
            if (pirates.hp <= 0) {
                pirates.hp = D.pHp; pirates.scatterUntil = time + D.scatterDays * DAY; pirates.target = -1;
                for (int o = 0; o < N; o++) SayTo(o, SideName(s) + " kills the pirates' captain: the band scatters.");
                break;
            }
        }
    }
    // ---- the Fishing Fleet: boats work the grounds, drop chum, and net low fliers
    for (auto& b : fleet) {
        Vector3 d = Vector3Subtract(b.goal, b.pos); float l = Vector2Length({d.x, d.z});
        if (l < 5 && eco.map && !eco.map->zones.empty()) { int z = (int)(Rand() * eco.map->zones.size()) % (int)eco.map->zones.size(); b.goal = eco.map->zones[z].Center(); b.goal.y = 0; }
        else if (l > 0.1f) { b.pos.x += d.x / l * D.boatSpeed * dt; b.pos.z += d.z / l * D.boatSpeed * dt; }
        int z = eco.ZoneAt({b.pos.x, -1, b.pos.z});
        if (z >= 0) for (auto& st : stocks) if (st.zone == z) st.pop = std::max(0.0f, st.pop - st.K * D.fleetFish * dt / DAY);   // (competition)
        b.chumT += dt; if (b.chumT > 10) { b.chumT = 0; eco.AddBlood({b.pos.x, -0.5f, b.pos.z}, D.chum); }   // (chum: feed in the water, and the sharks come)
        for (int s = 0; s < N; s++) for (auto& bird : ColOf(s).birds)
            if (bird.alive && bird.stage == BStage::Adult && bird.netT <= 0 && bird.pos.y < D.netBelow && Vector2Distance({bird.pos.x, bird.pos.z}, {b.pos.x, b.pos.z}) < D.netR && Rand() < 0.2f * dt) bird.netT = D.netS;
    }
    // ---- the Grey Wings: hunt chicks and lone fishers in their range, unless paid; their eagle can be killed
    if (grey.isle >= 0 && !grey.dead) {
        grey.huntT += dt; grey.hunterT = std::max(0.0f, grey.hunterT - dt);
        if (grey.huntT >= D.huntEvery) {
            grey.huntT = 0;
            for (int tries = 0; tries < 6; tries++) {
                int s = (int)(Rand() * N) % N;
                if (s < (int)grey.peaceUntil.size() && time < grey.peaceUntil[s]) continue;
                Colony& C = ColOf(s);
                Bird* t = nullptr; for (auto& b : C.birds) if (b.alive && Vector3Distance(b.pos, grey.crag) < D.gRange && (b.stage == BStage::Chick || (b.stage == BStage::Adult && b.role == Role::Fisher && b.flock < 0))) { t = &b; if (Rand() < 0.3f) break; }
                if (!t) continue;
                grey.hunter = t->pos; grey.hunterT = 2;
                if (Rand() < D.killChance) { Bird& v = *t; WithSide(s, [&] { BirdDies(v, "taken by the Grey Wings"); }); C.greyHit = time; }
                break;
            }
        }
        for (int s = 0; s < N && !grey.dead; s++) for (auto& fl : ColOf(s).flocks) {
            if (fl.members.size() < 3 || fl.retreating || Vector3Distance(fl.pos, grey.crag) > 40) continue;
            grey.hp -= fl.members.size() * 3 * dt;
            if (grey.hp <= 0) {
                grey.dead = true;
                if (RelicCount(s) < RelicsMax()) ColOf(s).relics |= 1u << RL_FEATHER;
                for (int o = 0; o < N; o++) SayTo(o, SideName(s) + " kills the Grey Wings' eagle: the Eagle's Feather is theirs.");
                break;
            }
        }
    }
}

void World::BotFactions() {   // (the colony in the fields, a bot: tribute when the Grey Wings are taking its young; a hire against the leader now and then)
    if (seasons <= 0 || !wholeMap) return;
    int have = 0; for (const auto& c : col.caches) have += (int)c.fish.size();
    if (grey.isle >= 0 && !grey.dead && time - col.greyHit < DAY * 0.5f && (cur >= (int)grey.peaceUntil.size() || time > grey.peaceUntil[cur]) && have >= FD().tributeFish + 6) PayTribute();
    if (pirates.on && time > pirates.hireUntil && time > pirates.scatterUntil && have >= FD().hireFish + 12 && Rand() < 0.002f) {
        int leader = -1, best = Score(cur).total; for (int s = 0; s <= (int)sides.size(); s++) if (s != cur && !Truce(cur, s) && Score(s).total > best * 1.15f) { best = Score(s).total; leader = s; }
        if (leader >= 0) HirePirates(leader);
    }
}

// ---------------------------------------------------------------- diplomacy, lightly (doc pp. 47-48): truces, feed pacts, bounties, flock loans
namespace {
int CacheFish(const Colony& C) { int n = 0; for (const auto& c : C.caches) n += (int)c.fish.size(); return n; }
}
int World::OfferPact(int to) {   // (the colony in the fields proposes a feed line)
    if (seasons <= 0 || to < 0 || to > (int)sides.size() || to == cur || col.pact >= 0) return -1;
    int none[G_COUNT] = {};
    Barter o; o.id = nextOffer++; o.from = cur; o.to = to; o.t = time; o.pact = true;
    for (int g = 0; g < G_COUNT; g++) { o.give[g] = none[g]; o.get[g] = none[g]; }
    offers.push_back(o);
    SayTo(to, SideName(cur) + " proposes a feed pact: a feed line between the two colonies (the trade page).");
    Say("Your pact is proposed.");
    return o.id;
}
int World::OfferLoan(int to, int flock, int fish) {   // (lend a flock for a day, for fish)
    Flock* f = FindFlock(cur, flock);
    if (seasons <= 0 || !f || to < 0 || to > (int)sides.size() || to == cur || f->loanTo >= 0) return -1;
    Barter o; o.id = nextOffer++; o.from = cur; o.to = to; o.t = time; o.loanFlock = flock; o.get[G_FISH] = std::clamp(fish, 0, 60);
    offers.push_back(o);
    SayTo(to, TextFormat("%s offers you its flock %s for a day, for %d fish (the trade page).", SideName(cur).c_str(), f->name.c_str(), o.get[G_FISH]));
    Say("Your flock is offered.");
    return o.id;
}
bool World::PostBounty(int target, int fish) {
    if (seasons <= 0 || target < 0 || target > (int)sides.size() || target == cur || fish <= 0) return false;
    if (CacheFish(col) < fish) { Say("Not enough fish in the caches for that bounty."); return false; }
    int n = fish; for (auto& c : col.caches) while (n > 0 && !c.fish.empty()) { c.fish.pop_back(); n--; }
    ColOf(target).bounty += fish; ColOf(target).bountyBy = cur;
    for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, TextFormat("%s posts a bounty of %d fish on %s's Founder: whoever kills it collects.", SideName(cur).c_str(), fish, SideName(target).c_str()));
    return true;
}
bool World::BreakTruce(int with) {
    if (!Truce(cur, with)) return false;
    size_t N = sides.size() + 1;
    truceUntil[cur * N + with] = truceUntil[with * N + cur] = -1;
    col.fervour = std::max(0.0f, col.fervour - 20);
    for (auto& t : towns) if (cur < (int)t.rep.size()) t.rep[cur] = std::max(-100.0f, t.rep[cur] - 10);
    for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, SideName(cur) + " breaks its truce with " + SideName(with) + ": the broken flag flies over its island.");
    col.truceBroken = time;
    return true;
}
void World::CollectBounty(int victim, int killer) {
    Colony& V = ColOf(victim);
    if (V.bounty <= 0 || killer < 0 || killer == victim || killer > (int)sides.size()) return;
    Colony& K = ColOf(killer);
    int n = V.bounty; V.bounty = 0;
    for (int q = 0; q < n && !K.caches.empty(); q++) K.caches[q % K.caches.size()].fish.push_back({0, 2, 0});
    for (int s = 0; s <= (int)sides.size(); s++) SayTo(s, TextFormat("%s kills %s's Founder and collects the bounty: %d fish.", SideName(killer).c_str(), SideName(victim).c_str(), n));
}
void World::StepDiplomacy(float dt) {
    if (seasons <= 0) return;
    int N = (int)sides.size() + 1;
    // feed pacts: twice a day the better-fed colony sends fish down the line
    for (int a = 0; a < N; a++) {
        Colony& A = ColOf(a); int b = A.pact;
        if (b < 0 || b <= a || b >= N) continue;
        Colony& B = ColOf(b);
        A.pactT += dt;
        if (A.pactT < DAY * 0.5f) continue;
        A.pactT = 0;
        int fa = CacheFish(A), fb = CacheFish(B);
        Colony& rich = fa > fb ? A : B; Colony& poor = fa > fb ? B : A;
        int give = std::min(4, std::abs(fa - fb) / 3);
        for (int q = 0; q < give; q++) for (auto& c : rich.caches) if (!c.fish.empty()) { CachedFish f = c.fish.back(); c.fish.pop_back(); if (!poor.caches.empty()) poor.caches[0].fish.push_back(f); break; }
    }
    // flock loans: a lent flock fights beside the borrower's (or guards its island), then comes home
    for (int s = 0; s < N; s++) for (auto& fl : ColOf(s).flocks) {
        if (fl.loanTo < 0) continue;
        if (time > fl.loanUntil) { fl.loanTo = -1; OrderFlock(s, fl.id, Target::Home, -1, -1, -1, -1, {}); SayTo(s, fl.name + " comes home from its loan."); continue; }
        const Colony& B = ColOf(fl.loanTo);
        const Flock* lead = nullptr; for (const auto& o : B.flocks) if (o.name != "Home guard" && !o.retreating) { lead = &o; break; }
        if (lead && (fl.target != lead->target || fl.tSide != lead->tSide)) OrderFlock(s, fl.id, lead->target, lead->tSide, lead->tIsle, lead->tZone, lead->tFlock, lead->tAt);
        else if (!lead && fl.target != Target::Point) OrderFlock(s, fl.id, Target::Point, -1, -1, -1, -1, B.caches.empty() ? isles[HomeOf(fl.loanTo)].c : B.caches[0].pos);
    }
    // a raid on one end of a feed line is an act of war against both
    for (int s = 0; s < N; s++) for (auto& fl : ColOf(s).flocks) {
        if (fl.tSide < 0 || fl.tSide >= N || fl.pactWarned || (fl.target != Target::Cache && fl.target != Target::Nests)) continue;
        int p = ColOf(fl.tSide).pact;
        if (p < 0 || p == s) continue;
        fl.pactWarned = true; ColOf(p).lastRaider = s;
        SayTo(p, SideName(s) + " raids " + SideName(fl.tSide) + ", your feed-pact partner: an act of war against you both.");
    }
}

// ---------------------------------------------------------------- fishing mastery (doc p48): techniques, each a different dive and a different risk
namespace {
struct TechData { std::vector<TechDef> v; float masteryPer = 0.025f, masteryK = 0.25f, explore = 0.15f; int trustTries = 12; };
const TechData& TD() {
    static TechData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& t = j["techniques"];
    d.masteryPer = t["mastery_per_catch"].F(d.masteryPer); d.masteryK = t["mastery_catch"].F(d.masteryK); d.explore = t["explore"].F(d.explore); d.trustTries = t["trust_tries"].I(d.trustTries);
    for (const Json& p : t["list"].a) {
        TechDef x; x.key = p["key"].Str0(); x.name = p["name"].Str0(x.key); x.how = p["how"].Str0(); x.best = p["best"].Str0(); x.risk = p["risk"].Str0();
        x.catchK = p["catch"].F(1); x.bigK = p["big"].F(0); x.smallK = p["small"].F(1); x.shoreK = p["shore"].F(1); x.riskK = p["risk_k"].F(1); x.splash = p["splash"].F(1);
        x.pace = p["pace"].F(1); x.steal = p["steal"].F(0); x.fightK = p["fight"].F(0); x.recover = p["recover"].F(0); x.yield = p["yield"].I(1); x.minFishers = p["min_fishers"].I(0);
        x.reach = p["reach"].F(1); x.founder = p["founder"].Str0(); x.founderK = p["founder_k"].F(1); x.safeFor = p["safe_for"].Str0(); x.safeK = p["safe_k"].F(1);
        d.v.push_back(x);
    }
    while ((int)d.v.size() < TK_COUNT) { TechDef x; x.key = x.name = "technique"; d.v.push_back(x); }
    return d;
}
}  // namespace
const std::vector<TechDef>& Techniques() { return TD().v; }
static int TechLogIx(int zone, int tk, int what) { return (zone * TK_COUNT + tk) * 3 + what; }
void World::TechLog(int zone, int tk, int what) {
    if (seasons <= 0 || zone < 0 || tk < 0 || tk >= TK_COUNT || !eco.map) return;
    size_t need = eco.map->zones.size() * TK_COUNT * 3;
    if (col.techLog.size() < need) col.techLog.resize(need, 0);
    col.techLog[TechLogIx(zone, tk, what)]++;
    if (what == TL_CATCH) col.techMastery[tk] = std::min(1.0f, col.techMastery[tk] + TD().masteryPer);
}
float World::TechRate(int zone, int tk, int what, int* tries) const {
    int ix = TechLogIx(zone, tk, TL_TRY);
    int n = zone >= 0 && ix + 2 < (int)col.techLog.size() ? col.techLog[ix] : 0;
    if (tries) *tries = n;
    return n > 0 ? col.techLog[ix + what] / (float)n : 0;
}
bool World::TechUsable(int tk, int zone, const Bird* b) const {
    if (seasons <= 0 || tk < 0 || tk >= TK_COUNT) return false;
    const std::string& fk = Founders()[me.def].key;
    if (tk == TK_DEEP) return (b && b->role == Role::Diver) || fk == "penguin" || col.HasTier(Tree::Fishing, 2) || col.speciesTrait[0] == GT_DEEP || col.speciesTrait[1] == GT_DEEP;   // (a Deep species)
    if (tk == TK_NIGHT) { float ph = DayPhase(); return ph < 0.3f || ph > 0.7f; }   // (the dusk and dawn rises, and the night)
    if (tk == TK_DRIVE) {
        int n = 0;
        for (const auto& o : col.birds) if (o.alive && o.stage == BStage::Adult && (o.role == Role::Fisher || o.role == Role::Diver) && eco.ZoneAt({o.goal.x, -1, o.goal.z}) == zone) n++;
        return n >= Techniques()[TK_DRIVE].minFishers;
    }
    return true;
}
int World::BestTech(int zone, const Bird* b) const {
    // what the log says, once a technique has been tried enough on this ground; otherwise what the water looks like
    int best = -1; float bs = -1e9f;
    for (int k = 0; k < TK_COUNT; k++) {
        if (!TechUsable(k, zone, b)) continue;
        int n = 0; float c = TechRate(zone, k, TL_CATCH, &n), l = TechRate(zone, k, TL_LOSS);
        if (n < TD().trustTries) continue;
        float s = c * Techniques()[k].yield - l * 4;
        if (s > bs) { bs = s; best = k; }
    }
    if (best >= 0) return best;
    const std::string& fk = Founders()[me.def].key;
    if (TechUsable(TK_NIGHT, zone, b) && Techniques()[TK_NIGHT].safeFor.find(fk) != std::string::npos) return TK_NIGHT;
    if (zone >= 0 && zone < (int)zoneNear.size() && zoneNear[zone]) return TK_HOVER;   // (crabs and shallows)
    if (zone >= 0 && eco.map && eco.map->zones[zone].y0 < -3.5f && TechUsable(TK_DEEP, zone, b)) return TK_DEEP;
    float small = 0, big = 0;
    for (const auto& s : stocks) if (s.zone == zone && eco.map) { int sz = eco.map->species[s.sp].size; (sz <= 1 ? small : big) += s.pop; }
    if (fk == "gannet") return TK_PLUNGE;
    return small > big * 1.5f ? TK_SKIM : TK_PLUNGE;
}
int World::PickTech(int zone, const Bird* b) {
    if (seasons <= 0) return -1;
    int t = col.tech;
    if (t >= 0 && TechUsable(t, zone, b)) return t;
    if (t < 0 && Rand() < TD().explore) {   // (auto: now and then a technique this ground hasn't seen enough of, so the log fills)
        std::vector<int> fresh;
        for (int k = 0; k < TK_COUNT; k++) { int n = 0; TechRate(zone, k, TL_CATCH, &n); if (n < TD().trustTries && TechUsable(k, zone, b)) fresh.push_back(k); }
        if (!fresh.empty()) return fresh[(size_t)(Rand() * fresh.size()) % fresh.size()];
    }
    return BestTech(zone, b);
}
TechMod World::TechMods(int tk, int zone, int size) const {
    TechMod m;
    if (seasons <= 0 || tk < 0 || tk >= TK_COUNT) return m;
    const TechDef& T = Techniques()[tk];
    const std::string& fk = Founders()[me.def].key;
    m.hit = T.catchK * (size <= 1 ? T.smallK : 1.0f + T.bigK * (size - 1)) * (1 + TD().masteryK * col.techMastery[tk]);
    if (zone >= 0 && zone < (int)zoneNear.size() && zoneNear[zone]) m.hit *= T.shoreK;
    if (!T.founder.empty() && T.founder.find(fk) != std::string::npos) m.hit *= T.founderK;   // (the Gannet's plunge, the Swift's skim)
    if (tk == TK_DRIVE && col.HasTier(Tree::Fishing, 4)) m.hit *= 1.25f;   // (yield with Cooperative Fishing)
    m.risk = T.riskK * ((!T.safeFor.empty() && T.safeFor.find(fk) != std::string::npos) || (tk == TK_NIGHT && (col.speciesTrait[0] == GT_KEEN || col.speciesTrait[1] == GT_KEEN)) ? T.safeK : 1.0f);   // (a Keen species sees at night)
    m.splash = T.splash; m.yield = T.yield; m.fight = T.fightK; m.recover = T.recover; m.pace = T.pace; m.reach = T.reach;
    m.steal = T.steal;
    if (m.steal > 0) for (int s = 0; s <= (int)sides.size(); s++) if (s != cur && Founders()[FounderOf(s).def].key == "frigatebird") { m.steal *= 3; break; }   // (frigatebirds steal from a hovering bird)
    return m;
}

// ---------------------------------------------------------------- nest styles (doc p49): chosen per nest, each a small bet
namespace {
struct NestStyleData { std::vector<NestStyleDef> v; float floodDays = 0.2f, mudDays = 2, sharkPerDay = 0.25f, waterM = 30, cliffM = 4, sandM = 3; };
const NestStyleData& NSD() {
    static NestStyleData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& t = j["nest_styles"];
    d.floodDays = t["burrow_flood_days"].F(d.floodDays); d.mudDays = t["mud_rain_days"].F(d.mudDays); d.sharkPerDay = t["floating_shark_per_day"].F(d.sharkPerDay);
    d.waterM = t["water_near_m"].F(d.waterM); d.cliffM = t["cliff_height_m"].F(d.cliffM); d.sandM = t["sand_below_m"].F(d.sandM);
    for (const Json& p : t["list"].a) {
        NestStyleDef x; x.key = p["key"].Str0(); x.name = p["name"].Str0(x.key); x.cost = p["cost"].Str0(); x.effect = p["effect"].Str0();
        x.twigs = p["twigs"].I(10); x.shells = p["shells"].I(0); x.eggs = p["eggs"].I(0);
        d.v.push_back(x);
    }
    while ((int)d.v.size() < NS_COUNT) { NestStyleDef x; x.key = x.name = "cup"; d.v.push_back(x); }
    return d;
}
}  // namespace
const std::vector<NestStyleDef>& NestStyles() { return NSD().v; }
bool World::WaterNear(Vector3 p, float r, Vector3* at) const {
    for (float rr = 6; rr <= r; rr += 6) for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        float x = p.x + cosf(a) * rr, z = p.z + sinf(a) * rr;
        if (HeightAt(x, z) < -0.3f) { if (at) *at = {x, 0.05f, z}; return true; }
    }
    return false;
}
bool World::NestStyleFits(int style, int site) const {
    if (style <= NS_CUP || style >= NS_COUNT) return true;
    if (site < 0 || site >= (int)col.sites.size()) return false;
    const Site& s = col.sites[site];
    const NestStyleData& D = NSD();
    switch (style) {
    case NS_BURROW: return s.pos.y < D.sandM;                    // (sand or soil: the low ground)
    case NS_HANGING: return s.palm >= 0;                         // (a branch or an overhang)
    case NS_MUD: case NS_FLOATING: return WaterNear(s.pos, D.waterM);
    case NS_CLIFF: return s.pos.y >= D.cliffM && col.shells >= NestStyles()[NS_CLIFF].shells;   // (the stack: a high site, and the shells)
    default: return true;
    }
}
void World::StyleNest(Nest& n, bool tell) {
    n.style = NS_CUP;
    if (seasons <= 0) return;
    int st = col.nestStyle;
    if (st <= NS_CUP || st >= NS_COUNT) return;
    if (!NestStyleFits(st, n.site)) { if (tell) Say("This site won't take a " + NestStyles()[st].name + " nest (" + NestStyles()[st].cost + "): a cup it is."); return; }
    col.shells -= NestStyles()[st].shells;
    n.style = st;
    if (st == NS_FLOATING) { Vector3 at; if (WaterNear(n.pos, NSD().waterM, &at)) n.pos = at; }   // (out on the water)
    if (tell) Say("A " + NestStyles()[st].name + " nest: " + NestStyles()[st].effect + ".");
}
int World::NestCostOf(int style) const { return (int)lroundf(NestTwigs() * NestStyles()[std::clamp(style, 0, NS_COUNT - 1)].twigs / 10.0f); }
int World::NestEggsOf(const Nest& n) const { int e = NestStyles()[std::clamp(n.style, 0, NS_COUNT - 1)].eggs; return e > 0 ? e : NestEggs(); }
bool NestOpen(const Nest& n, int threat) {
    switch (n.style) {
    case NS_HANGING: return threat != NT_THEFT && threat != NT_TEAR && threat != NT_LAND;   // (out of reach of the ground and the raid; it sways, so no Watcher perches on it)
    case NS_MUD: return threat != NT_LAVA;                                                 // (fireproof)
    case NS_CLIFF: return threat != NT_THEFT && threat != NT_TEAR && threat != NT_LAND;   // (nothing climbs to it)
    case NS_FLOATING: return threat != NT_LAND && threat != NT_LAVA && threat != NT_ASH && threat != NT_THEFT && threat != NT_TEAR;   // (immune to everything on land)
    default: return true;
    }
}
void World::StepNestStyles(float dt) {
    if (seasons <= 0) return;
    const NestStyleData& D = NSD();
    bool storm = weather.kind == 1;
    for (int s = 0; s <= (int)sides.size(); s++) {
        Colony& C = ColOf(s);
        for (int ni = 0; ni < (int)C.nests.size(); ni++) {
            Nest& n = C.nests[ni];
            if (!n.built) continue;
            auto lose = [&](const char* cause, bool all) {
                for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick)) { WithSide(s, [&] { BirdDies(b, cause); }); if (!all) break; }
            };
            if (n.style == NS_BURROW) {   // (floods in storms: the eggs and chicks in it drown)
                if (!storm) n.floodT = 0;
                else if (n.floodT >= 0 && (n.floodT += dt) >= D.floodDays * DAY) { lose("drowned in a flooded burrow", true); n.floodT = -1e9f; SayTo(s, "The storm floods a burrow nest: its eggs and chicks drown."); }
            }
            if (n.style == NS_MUD && storm && (n.rainT += dt) >= D.mudDays * DAY) {   // (dissolves in two days of rain)
                lose("its mud nest dissolved in the rain", true);
                n.built = false; n.twigs = 0; n.rainT = 0; n.bowl = 0;
                for (auto& m : C.birds) if (m.alive && m.nest == ni && m.stage == BStage::Mate) { m.alive = false; m.cause = "its mud nest dissolved"; }
                n.mate = -1;
                SayTo(s, "Two days of rain: a mud nest dissolves.");
            }
            if (n.style == NS_FLOATING && (n.floodT += dt) >= DAY) {   // (sharks: once a day, a chance one of its young is taken from below)
                n.floodT = 0;
                if (Rand() < D.sharkPerDay) { bool any = false; for (const auto& b : C.birds) any |= b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick); if (any) { lose("taken by a shark under the floating nest", false); SayTo(s, "A shark takes one of the young from a floating nest."); } }
            }
        }
    }
}

// ---------------------------------------------------------------- structures beyond nests (doc p49): perch, smokehouse, Lookout, Rookery, Beacon, Monument
namespace {
struct StructData { float perchSight = 1.25f, smoke = 1.5f, lookoutReport = 2, rookeryR = 40, beaconCd = 0.25f; int rookeryAdults = 3; };
const StructData& STD() {
    static StructData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& t = j["structures"];
    d.perchSight = t["perch_sight"].F(d.perchSight); d.smoke = t["smokehouse_spoil"].F(d.smoke); d.lookoutReport = t["lookout_report"].F(d.lookoutReport);
    d.rookeryR = t["rookery_m"].F(d.rookeryR); d.rookeryAdults = t["rookery_adults"].I(d.rookeryAdults); d.beaconCd = t["beacon_days"].F(d.beaconCd);
    return d;
}
}  // namespace
const Structure* BuiltOf(const Colony& C, int kind) { for (const auto& s : C.builds) if (s.kind == kind && s.built && s.hp > 0) return &s; return nullptr; }
int MonumentsOf(const Colony& C) { int n = 0; for (const auto& s : C.builds) n += s.kind == ST_MONUMENT && s.built && s.hp > 0; return n; }
float PerchSight() { return STD().perchSight; }
float SmokehouseSpoil() { return STD().smoke; }
float LookoutReport() { return STD().lookoutReport; }
static float FlatD(Vector3 a, Vector3 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
bool World::InRookery(const Nest& n) const { const Structure* r = BuiltOf(col, ST_ROOKERY); return r && FlatD(r->pos, n.pos) < STD().rookeryR; }
bool World::LightBeacon() {
    const Structure* b = BuiltOf(col, ST_BEACON);
    if (!b) { Say("There's no Beacon to light."); return false; }
    if (time - col.beaconT < STD().beaconCd * DAY) { Say("The Beacon is still being relit."); return false; }
    col.beaconT = time;
    int n = 0; for (auto& f : col.flocks) if (f.loanTo < 0) { OrderFlock(cur, f.id, Target::Home, -1, -1, -1, -1, {}); n++; }
    Say(TextFormat("The Beacon is lit: every flock (%d) turns for home.", n));
    return true;
}
void World::StepStructures(float dt) {
    if (seasons <= 0) return;
    const StructData& D = STD();
    for (int s = 0; s <= (int)sides.size(); s++) {
        Colony& C = ColOf(s);
        // the Rookery: chicks there are warmed by any adult (enough adults about it) and fledge together
        const Structure* r = BuiltOf(C, ST_ROOKERY);
        C.rookeryWarm = false;
        if (r) { int a = 0; for (const auto& b : C.birds) a += b.alive && b.stage == BStage::Adult && FlatD(b.pos, r->pos) < D.rookeryR; C.rookeryWarm = a >= D.rookeryAdults; }
        // the bots lay out what they want once a day: a perch at 10 birds, a smokehouse with the research, a Rookery at 15, a Lookout at 20, a Beacon at war, a Monument in winter
        if (HumanOf(s) || fmodf(time, DAY) >= dt) continue;
        WithSide(s, [&] {
            int alive = 0; for (const auto& b : col.birds) alive += b.alive && b.stage == BStage::Adult;
            auto laid = [&](int k) { for (const auto& st : col.builds) if (st.kind == k && (k != ST_MONUMENT || !st.built)) return true; return false; };
            auto want = [&](int k, bool when) { if (when && !laid(k) && BuildUnlocked(k)) LayStructure(k); };
            want(ST_PERCH, alive >= 10); want(ST_SMOKEHOUSE, alive >= 8); want(ST_ROOKERY, alive >= 15); want(ST_LOOKOUT, alive >= 20);
            want(ST_BEACON, !col.flocks.empty() && alive >= 18); want(ST_MONUMENT, col.shells >= StructureShells(ST_MONUMENT) + 20);
        });
    }
}
bool World::LayStructure(int kind) {
    if (kind < 0 || kind >= ST_COUNT || col.caches.empty()) return false;
    int same = 0; for (const auto& s : col.builds) if (s.kind == kind) { if (kind != ST_MONUMENT || !s.built) return false; same++; }
    if (!BuildUnlocked(kind)) return false;
    static const Vector3 OFF[ST_COUNT] = {{0, 0, 0}, {6, 0, 4}, {-5, 0, 3}, {4, 0, -5}, {-6, 0, -4}, {3, 0, 7}, {-3, 0, -8}, {9, 0, -2}, {-9, 0, 1}, {2, 0, 10}, {11, 0, 7}, {0, 0, 0}};
    Structure n; n.kind = kind; n.isle = home; n.hp = StructureHp(kind);
    Vector3 o = OFF[kind]; if (kind == ST_MONUMENT) { o.x += 4 * same; o.z -= 3 * same; }
    n.pos = kind == ST_HEDGE ? col.caches[0].pos : GroundAt(col.caches[0].pos.x + o.x, col.caches[0].pos.z + o.z);
    col.builds.push_back(n);
    return true;
}

// ---------------------------------------------------------------- the long match's additions to the score (doc p50)
namespace {
struct LongScore { float vet = 10, relic = 60, monument = 30, decree = 5, decreeMax = 60, truce = 20; };
const LongScore& LS() {
    static LongScore d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& t = j["score"];
    d.vet = t["veteran_alive"].F(d.vet); d.relic = t["relic_held"].F(d.relic); d.monument = t["monument"].F(d.monument);
    d.decree = t["decree_distinct"].F(d.decree); d.decreeMax = t["decree_max"].F(d.decreeMax); d.truce = t["truce_kept"].F(d.truce);
    return d;
}
int Bits(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
}  // namespace
int World::LegacyScore(int side, int* part) const {
    // veterans alive 10 each; relics held 60 each; a legendary bird alive 100 (the Dodo 200); monuments 30 each;
    // decrees used, 5 per distinct one (at most 60); truces kept to the end, 20 each (none for a colony that broke one)
    int p[6] = {};
    if (seasons > 0 && side >= 0 && side <= (int)sides.size()) {
        const LongScore& K = LS();
        const Colony& C = ColOf(side);
        int vets = 0; for (const auto& b : C.birds) vets += b.alive && b.stage == BStage::Adult && b.vet >= 0;
        p[0] = (int)lroundf(vets * K.vet);
        p[1] = (int)lroundf(Bits(C.relics) * K.relic);
        p[2] = C.legendAlive && C.legend >= 0 && C.legend < (int)Legends().size() ? Legends()[C.legend].score : 0;
        p[3] = (int)lroundf(MonumentsOf(C) * K.monument);
        p[4] = (int)std::min(K.decreeMax, Bits(C.decreesUsed) * K.decree);
        size_t N = sides.size() + 1;
        if (C.truceBroken < -999 && truceUntil.size() >= N * N) for (size_t t = 0; t < N; t++) if ((int)t != side && truceUntil[side * N + t] >= time) p[5] += (int)K.truce;
    }
    if (part) for (int k = 0; k < 6; k++) part[k] = p[k];
    int lf = LongFlight() && side >= 0 && side <= (int)sides.size() && ColOf(side).stormCrossed ? (int)StormCrossScore() : 0;   // (the Long Flight: the Storm Wall crossed)
    if (LongFlight() && side >= 0 && side <= (int)sides.size()) { const Colony& C = ColOf(side); lf += WonderScore(side) + C.warsWon * 200 + C.huntScore + TitleScore(side) + std::min(100, 5 * ChronicleChapters(side)) + ReckoningScore(side); }   // (titles; the Chronicle's length)   // (Grand Projects; the Great War won; the Hunt)
    return p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + lf;
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
    const bool quick = getenv("DEPTH_LONG_QUICK") != nullptr;   // (skips the two bot matches: about ten minutes)
    if (!quick) {   // two bot colonies through a two-season match: it ends on its last day
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
    if (!quick) {   // a two-season match: every colony has a decree every day
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
    }    // ---- veterans (doc p42) and mates' traits (p43)
    {
        check(Veterans().fights == 3 && Veterans().names.size() >= 30 && MateTraits().size() == 6, "veterans after three fights (a name list, five traits); six mates' traits");
        auto w = make(4, 61); w->ape.isle = -1; w->kraken.isle = -1;
        Colony& C = w->col;
        Bird k; k.id = C.nextId++; k.stage = BStage::Adult; k.role = Role::Skirmisher; k.hp = w->MaxHp(Role::Skirmisher); k.pos = C.caches[0].pos; C.birds.push_back(k);
        Bird& v = C.birds.back();
        for (int f = 0; f < 3; f++) { w->time += 30; v.foughtT = w->time; w->time += 11; w->StepVeterans(0.1f); }
        check(v.fights == 3 && v.vet >= 0 && v.vetName >= 0 && !w->VetLabel(v).empty(), "a Skirmisher that survives three fights is named a veteran: " + w->VetLabel(v) + TextFormat(" (fights %d, vet %d, alive %d, stage %d)", v.fights, v.vet, (int)v.alive, (int)v.stage));
        v.vet = VT_FEARLESS;
        int fl = w->MakeFlock(0, {v.id}, Formation::Chevron, Alt::Mid, Stance::Hold);
        float withVet = fl >= 0 ? w->Morale(0, *w->FindFlock(0, fl)) : 0; v.vet = -1; float without = fl >= 0 ? w->Morale(0, *w->FindFlock(0, fl)) : 0;
        check(withVet - without >= 14, TextFormat("a Fearless veteran steadies its flock (morale %.0f with it, %.0f without)", withVet, without));
        // mates: a picky bowl calls the trait it asks for, and the chicks inherit it
        C.wantTrait = MT_FERTILE;
        Nest& n = C.nests[0]; n.built = true; n.mate = -1; n.mateT = 0.01f; n.bowl = n.bowlNeed; n.favFish = n.bowlNeed;
        for (auto& b : C.birds) if (b.stage == BStage::Mate) b.alive = false;
        w->StepColony(0.1f);
        int mt = -2; for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Mate) mt = b.trait;
        check(mt == MT_FERTILE, TextFormat("a bowl filled with %s calls a Fertile mate", MateTraits()[MT_FERTILE].favorite.c_str()));
        Bird* mate = nullptr; for (auto& b : C.birds) if (b.alive && b.stage == BStage::Mate) mate = &b;
        if (mate) { mate->pos = Vector3Add(n.pos, {0, 0.25f, 0}); mate->task = Task::Sit; mate->hunger = 1; mate->clutchT = 1e9f; for (int q = 0; q < 3; q++) w->StepColony(0.1f); }
        for (const auto& b : w->born) C.birds.push_back(b); w->born.clear();
        int eggs = 0, inherit = 0; for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Egg) { eggs++; inherit += b.trait == MT_FERTILE; }
        check(eggs > 0 && inherit == eggs, TextFormat("its eggs inherit the trait (%d of %d)", inherit, eggs));
        Bird ch; ch.stage = BStage::Chick; ch.trait = MT_HARDY; ch.retrainTo = Role::Fisher; C.birds.push_back(ch); w->FledgeNow(C.birds.back());
        check(C.birds.back().hp > w->MaxHp(Role::Fisher) * 1.05f, "a Hardy mother's chick fledges with more health");
    }    // ---- the expansion's roles (doc pp. 40-42)
    {
        check(IsWarrior(Role::Plunger) && IsWarrior(Role::Drummer) && !IsWarrior(Role::Diver) && !IsWarrior(Role::Augur) && RoleOf(Role::Augur).key == "augur" && RoleOf(Role::Plunger).attack == 40,
              "eight new warriors and six new workers, with their stats (a Plunger strikes for 40)");
        auto s = make(0, 71); s->col.tier[(int)Tree::War] = 3; s->col.tier[(int)Tree::Caches] = 1;
        auto w = make(4, 71); w->col.tier[(int)Tree::War] = 3; w->col.tier[(int)Tree::Caches] = 1;
        check(!s->RoleUnlocked(Role::Plunger) && !s->RoleUnlocked(Role::Keeper) && w->RoleUnlocked(Role::Plunger) && w->RoleUnlocked(Role::Keeper) && !w->RoleUnlocked(Role::Augur),
              "they open in a long match only, by the trees they grow from (War 3: the Plunger; Caches 1: the Keeper)");
        float spoil = w->SpoilDays();
        Colony& C = w->col;
        auto add = [&](Role r) { Bird b; b.id = C.nextId++; b.stage = BStage::Adult; b.role = r; b.hp = w->MaxHp(r); b.hunger = 1; b.pos = C.caches[0].pos; C.birds.push_back(b); return b.id; };
        add(Role::Keeper);
        check(w->SpoilDays() > spoil * 1.9f, TextFormat("a Keeper halves the spoilage (%.1f to %.1f days)", spoil, w->SpoilDays()));
        // a Teacher: a chick near it fledges early
        add(Role::Teacher);
        Bird ch; ch.id = C.nextId++; ch.stage = BStage::Chick; ch.nest = 0; ch.pos = C.caches[0].pos; ch.hunger = 1; ch.age = Econ().chickDays - 0.35f; C.birds.push_back(ch);
        int chid = ch.id; w->StepColony(0.1f);
        bool fledged = false; for (const auto& b : C.birds) if (b.id == chid) fledged = b.stage == BStage::Adult;
        check(fledged, "a Teacher's chick fledges early");
        // a Drummer at home: the island's defenders +15
        int k1 = add(Role::Skirmisher), k2 = add(Role::Skirmisher);
        int fl = w->MakeFlock(0, {k1, k2}, Formation::Chevron, Alt::Mid, Stance::Hold);
        float before = fl >= 0 ? w->Morale(0, *w->FindFlock(0, fl)) : 0;
        add(Role::Drummer);
        float after = fl >= 0 ? w->Morale(0, *w->FindFlock(0, fl)) : 0;
        check(after - before >= 14, TextFormat("a Drummer at home steadies the defenders (morale %.0f to %.0f)", before, after));
    }    // ---- relics, legendary birds and great events (doc pp. 46-47)
    {
        check(Relics().size() == 8 && Legends().size() == 5 && GreatEvents().size() == 8 && RelicsMax() == 3, "8 relics (three at most to a colony), 5 legendary birds, 8 great events");
        auto w = make(4, 81);
        int spots = (int)w->relicSpots.size(); bool onDanger = true; for (const auto& r : w->relicSpots) onDanger &= IsDangerous(w->isles[r.isle].type);
        check(spots >= 3 && onDanger && w->greatEvent >= 0 && w->greatDay >= 8 && w->greatDay <= 20, TextFormat("relics lie on %d dangerous islands; the great event (%s) comes on day %.0f", spots, GreatEvents()[std::max(0, w->greatEvent)].name.c_str(), w->greatDay));
        // take them: three at most
        int took = 0; for (int i = 0; i < spots; i++) took += w->TakeRelic(i);
        check(took == std::min(3, spots) && w->RelicCount(0) == took, TextFormat("the Founder takes relics home (%d taken, %d held)", took, w->RelicCount(0)));
        w->col.relics = 0; w->relicSpots[0].taken = false; w->relicSpots[0].relic = RL_FLAG; w->TakeRelic(0);
        check(w->HasRelic(0, RL_FLAG), "the Fort's Flag held: every town's rates +20%");
        // the Visitor: a fish of size 3 recruits it
        w->legendFree = LG_DODO; w->legendPos = w->me.pos; w->me.carrySp = 0; w->me.carrySize = 3; w->col.legend = -1;
        check(w->RecruitLegend() && w->col.legend == LG_DODO && w->HasLegend(0, LG_DODO) && w->legendFree < 0, "the Visitor takes the gift and joins: the Dodo");
        // every event runs on its day
        for (int e = 0; e < GE_COUNT; e++) {
            auto v = make(4, 90 + e);
            v->greatEvent = e; v->greatDay = 9; v->greatDone = v->greatAnnounced = false;
            for (auto& c : v->col.caches) for (int q = 0; q < 10; q++) c.fish.push_back({0, 2, 0});
            v->time = 8.6f * World::DAY; v->StepGreat(0.1f);
            bool on = v->GreatNow(e) || (e == GE_TREASURE && v->treasure > 0) || (e == GE_VISITOR && v->legendFree >= 0) || e == GE_PLAGUE;
            bool effect = true;
            if (e == GE_ECLIPSE) effect = v->DayPhase() < 0.1f;
            if (e == GE_CALM) effect = Vector2Length(v->WindAt()) < 0.01f && v->Thermal(v->isles[0].hill) == 0;
            if (e == GE_GREAT_STORM) effect = v->weather.kind == 1;
            if (e == GE_RED_TIDE) effect = v->col.caches[0].fish.size() < 10;
            check(on && effect, std::string("the great event: ") + GreatEvents()[e].name);
        }
    }    // ---- the neutral factions (doc p46)
    {
        auto w = make(4, 101); w->ape.isle = -1; w->kraken.isle = -1;
        check(w->pirates.on && w->fleet.size() == 3 && w->grey.isle >= 0 && !IsStartType(w->isles[w->grey.isle].type), "a long match has the Frigate Pirates, a fishing fleet of three boats, and the Grey Wings on a crag");
        // the pirates steal a carried fish
        Colony& C1 = w->ColOf(1);
        Bird f; f.id = C1.nextId++; f.stage = BStage::Adult; f.role = Role::Fisher; f.hp = 60; f.hunger = 1; f.carrySp = 0; f.carrySize = 2; f.pos = Vector3Add(w->pirates.pos, {3, 0, 0}); C1.birds.push_back(f);
        int fid = f.id; w->StepFactions(0.1f);
        bool stolen = false; for (const auto& b : C1.birds) if (b.id == fid) stolen = b.carrySp < 0;
        check(stolen && w->pirates.loot >= 1, "the pirates steal a fisher's catch in the air");
        // a hire: fish paid, a target for a day
        for (int q = 0; q < 20; q++) w->col.caches[0].fish.push_back({0, 2, 0});
        size_t before = w->col.caches[0].fish.size();
        bool hired = w->HirePirates(1);
        check(hired && w->pirates.target == 1 && w->col.caches[0].fish.size() == before - PirateHireFish(), TextFormat("the pirates hired against %s for %d fish (hired %d, target %d, %d to %d fish)", w->SideName(1).c_str(), PirateHireFish(), (int)hired, w->pirates.target, (int)before, (int)w->col.caches[0].fish.size()));
        // a flock that fights them kills the captain: the band scatters
        std::vector<int> ids; for (int q = 0; q < 8; q++) { Bird k; k.id = w->col.nextId++; k.stage = BStage::Adult; k.role = Role::Tank; k.hp = 180; k.hunger = 1; k.pos = w->pirates.pos; w->col.birds.push_back(k); ids.push_back(k.id); }
        int fl = w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Hold);
        if (Flock* F = w->FindFlock(0, fl)) F->pos = w->pirates.pos;
        for (int q = 0; q < 200 && w->time >= w->pirates.scatterUntil; q++) { if (Flock* F = w->FindFlock(0, fl)) F->pos = w->pirates.pos; w->StepFactions(0.1f); w->time += 0.1f; }
        check(w->time < w->pirates.scatterUntil, "a flock kills the pirates' captain: the band scatters");
        // the Grey Wings: tribute buys a day's peace
        size_t b2 = w->col.caches[0].fish.size();
        check(w->PayTribute() && w->time < w->grey.peaceUntil[0] && w->col.caches[0].fish.size() == b2 - TributeFish(), "tribute to the Grey Wings: a day's peace");
        // the fleet nets a bird flying low beside a boat
        Bird lo; lo.id = w->col.nextId++; lo.stage = BStage::Adult; lo.role = Role::Fisher; lo.hp = 60; lo.hunger = 1; lo.pos = {w->fleet[0].pos.x + 2, 2, w->fleet[0].pos.z}; w->col.birds.push_back(lo);
        int lid = lo.id; bool netted = false;
        for (int q = 0; q < 200 && !netted; q++) { for (auto& b : w->col.birds) if (b.id == lid) { b.pos = {w->fleet[0].pos.x + 2, 2, w->fleet[0].pos.z}; netted = b.netT > 0; } w->StepFactions(0.1f); }
        check(netted, "the Fishing Fleet nets a bird flying low beside its boat");
    }    // ---- diplomacy, lightly (doc pp. 47-48)
    {
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1;
        for (int s = 0; s <= 1; s++) w->WithSide(s, [&] { for (int q = 0; q < 30; q++) w->col.caches[0].fish.push_back({0, 2, 0}); });
        int id = w->OfferPact(1);
        w->WithSide(1, [&] { w->AnswerOffer(1, id, true); });
        check(w->ColOf(0).pact == 1 && w->ColOf(1).pact == 0, "a feed pact proposed and accepted: a feed line between the two");
        for (int q = 0; q < 25; q++) w->ColOf(1).caches[0].fish.pop_back();
        int poor0 = (int)w->ColOf(1).caches[0].fish.size();
        w->ColOf(0).pactT = World::DAY; w->StepDiplomacy(0.1f);
        check((int)w->ColOf(1).caches[0].fish.size() > poor0, TextFormat("fish go down the line to the hungrier colony (%d to %d)", poor0, (int)w->ColOf(1).caches[0].fish.size()));
        check(w->PostBounty(1, 10) && w->ColOf(1).bounty == 10, "a bounty of 10 fish posted on a rival's Founder");
        size_t mine = w->ColOf(0).caches[0].fish.size();
        w->CollectBounty(1, 0);
        check(w->ColOf(1).bounty == 0 && w->ColOf(0).caches[0].fish.size() == mine + 10, "the Founder killed: the killer collects the bounty");
        // a truce, then broken: fervour and reputation pay for it
        size_t N = w->sides.size() + 1; w->truceUntil.assign(N * N, -1); w->truceUntil[0 * N + 1] = w->truceUntil[1 * N + 0] = w->time + World::DAY;
        float f0 = w->col.fervour = 50;
        check(w->BreakTruce(1) && !w->Truce(0, 1) && w->col.fervour <= f0 - 19.9f, "a truce broken: the breaker loses 20 fervour and reputation, and everyone sees the broken flag");
        // a flock lent for a day follows the borrower's lead
        std::vector<int> ids; for (int q = 0; q < 3; q++) { Bird k; k.id = w->col.nextId++; k.stage = BStage::Adult; k.role = Role::Skirmisher; k.hp = 50; k.hunger = 1; k.pos = w->col.caches[0].pos; w->col.birds.push_back(k); ids.push_back(k.id); }
        int fl = w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid);
        int lid = w->OfferLoan(1, fl, 8);
        w->WithSide(1, [&] { w->AnswerOffer(1, lid, true); });
        Flock* F = w->FindFlock(0, fl);
        check(F && F->loanTo == 1 && w->ColOf(1).caches[0].fish.size() < 5 + 30, "a flock lent for a day, for fish");
        w->time += World::DAY * 1.1f; w->StepDiplomacy(0.1f);
        check(F && F->loanTo < 0, "the loan ends and the flock comes home");
    }    // ---- fishing mastery (doc p48)
    {
        const auto& TK = Techniques();
        check(TK.size() == TK_COUNT && TK[TK_PLUNGE].name == "Plunge" && TK[TK_NIGHT].yield == 2 && TK[TK_DRIVE].minFishers == 3, "six techniques: Plunge, Skim, Hover-strike, Drive, Deep dive, Night fishing");
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        int z = w->lagoonZone >= 0 ? w->lagoonZone : 0;
        check(w->TechMods(TK_SKIM, z, 1).hit > w->TechMods(TK_PLUNGE, z, 1).hit && w->TechMods(TK_PLUNGE, z, 3).hit > w->TechMods(TK_SKIM, z, 3).hit,
              "a skim takes small fish better, a plunge big ones");
        check(w->TechMods(TK_DRIVE, z, 1).splash >= 3 && w->TechMods(TK_DRIVE, z, 1).risk > 1.5f && w->TechMods(TK_NIGHT, z, 1).yield == 2 && w->TechMods(TK_DEEP, z, 1).fight > 0,
              "a drive is a triple splash the sharks come to; night fishing brings two fish; a deep dive turns a predator into a fight");
        check(!w->TechUsable(TK_DEEP, z), "a deep dive wants a Diver, a Penguin or the Deep Dive research");
        w->col.tier[(int)Tree::Fishing] = 2;
        check(w->TechUsable(TK_DEEP, z), "with the research, the colony's fishers can dive deep");
        w->time = 0.28f * World::DAY; bool noon = w->TechUsable(TK_NIGHT, z); w->time = 0.88f * World::DAY; bool dawn = w->TechUsable(TK_NIGHT, z);
        check(!noon && dawn, "night fishing only at the dusk and dawn rises (and the night)");
        check(!w->TechUsable(TK_DRIVE, z), "a drive needs three fishers on the ground");
        auto o = make(4, 111); o->ape.isle = -1; o->kraken.isle = -1;
        bool owlSafe = false; for (int d = 0; d < (int)Founders().size(); d++) if (Founders()[d].key == "owl") { o->me.def = d; owlSafe = o->TechMods(TK_NIGHT, z, 1).risk < 1; }
        check(owlSafe, "night fishing is safe for an Owl");
        // the colony fishes a day by plunging, then a day by skimming: the log fills and the technique is learned
        w->col.tier[(int)Tree::Fishing] = 0; w->time = 0;
        for (int q = 0; q < 8; q++) { Bird b; b.id = w->col.nextId++; b.stage = BStage::Adult; b.role = Role::Fisher; b.hp = 60; b.hunger = 1; b.pos = w->col.caches[0].pos; w->col.birds.push_back(b); }
        w->col.ground = z;
        auto w2 = make(4, 111); w2->ape.isle = -1; w2->kraken.isle = -1; w2->weather.next = 1e9f;   // (a second colony, so the plunge day doesn't empty the skim day's lagoon)
        for (int q = 0; q < 8; q++) { Bird b; b.id = w2->col.nextId++; b.stage = BStage::Adult; b.role = Role::Fisher; b.hp = 60; b.hunger = 1; b.pos = w2->col.caches[0].pos; w2->col.birds.push_back(b); }
        w2->col.ground = z;
        auto day = [&](World& W, int tk) { W.col.tech = tk; for (float t = 0; t < World::DAY; t += 0.1f) { W.Step(0.1f, FounderInput{}); for (auto& b : W.col.birds) if (b.alive) b.hunger = 1; } };
        day(*w, TK_PLUNGE); day(*w2, TK_SKIM);
        int np = 0, ns = 0; float cp = w->TechRate(z, TK_PLUNGE, TL_CATCH, &np), cs = w2->TechRate(z, TK_SKIM, TL_CATCH, &ns);
        check(np >= 10 && ns >= 10 && cp > 0 && cs > 0, TextFormat("a day of each on the lagoon: plunge %d dives, %.0f%% caught; skim %d dives, %.0f%% caught", np, cp * 100, ns, cs * 100));
        check(w->col.techMastery[TK_PLUNGE] > 0 && w2->col.techMastery[TK_SKIM] > 0 && w->TechMods(TK_PLUNGE, z, 2).hit > Techniques()[TK_PLUNGE].catchK * (1 + Techniques()[TK_PLUNGE].bigK),
              TextFormat("learned by using them: plunge mastery %.2f, skim %.2f, and a mastered technique strikes truer", w->col.techMastery[TK_PLUNGE], w2->col.techMastery[TK_SKIM]));
        w->col.tech = -1; int best = w->BestTech(z);
        check(best == TK_PLUNGE, "auto picks from what the log says once each has been tried enough: " + TK[best].name);
        auto s = make(0, 111);
        check(s->PickTech(0) == -1 && s->TechMods(TK_DRIVE, 0, 1).hit == 1, "a standard match has no techniques");
    }    // ---- nest styles (doc p49)
    {
        const auto& NS = NestStyles();
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        check(NS.size() == NS_COUNT && NS[NS_PLATFORM].eggs == 6 && w->NestCostOf(NS_PLATFORM) == 2 * w->NestCostOf(NS_CUP) && w->NestCostOf(NS_BURROW) < w->NestCostOf(NS_CUP),
              TextFormat("seven nest styles; a Platform costs %d twigs to a Cup's %d, a Burrow %d", w->NestCostOf(NS_PLATFORM), w->NestCostOf(NS_CUP), w->NestCostOf(NS_BURROW)));
        int palm = -1, bare = -1, wet = -1, low = -1;
        for (int s = 0; s < (int)w->col.sites.size(); s++) {
            const Site& S = w->col.sites[s]; if (S.nest >= 0) continue;
            if (S.palm >= 0 && palm < 0) palm = s; if (S.palm < 0 && bare < 0) bare = s;
            if (wet < 0 && w->WaterNear(S.pos, 30)) wet = s; if (low < 0 && S.pos.y < 3) low = s;
        }
        auto lay = [&](int style, int site) { w->col.nestStyle = style; Nest n; n.site = site; n.pos = w->col.sites[site].pos; n.isle = w->col.sites[site].isle; w->StyleNest(n); return n; };
        Nest pl = lay(NS_PLATFORM, bare >= 0 ? bare : 0);
        check(pl.style == NS_PLATFORM && w->NestEggsOf(pl) == 6 && w->NestEggsOf(Nest{}) < 6, "a Platform holds six eggs");
        if (palm >= 0) { Nest h = lay(NS_HANGING, palm); check(h.style == NS_HANGING && !NestOpen(h, NT_THEFT) && !NestOpen(h, NT_LAND) && NestOpen(h, NT_LAVA), "a Hanging nest on a palm: no theft, no raid, nothing from the ground reaches it"); }
        else check(false, "a palm site for a Hanging nest");
        if (bare >= 0) { Nest h = lay(NS_HANGING, bare); check(h.style == NS_CUP, "a Hanging nest needs a branch: on a bare site it's a cup"); }
        if (wet >= 0) {
            Nest f = lay(NS_FLOATING, wet);
            check(f.style == NS_FLOATING && w->HeightAt(f.pos.x, f.pos.z) < 0 && !NestOpen(f, NT_LAND) && !NestOpen(f, NT_LAVA), "a Floating nest goes out on the water: immune to everything on land");
            Nest m = lay(NS_MUD, wet); check(m.style == NS_MUD && !NestOpen(m, NT_LAVA) && NestOpen(m, NT_THEFT), "a Mud nest by the water: fireproof");
        } else check(false, "a site by the water");
        w->col.shells = 0; w->col.nestStyle = NS_CLIFF;
        bool noCliff = true; for (int s = 0; s < (int)w->col.sites.size(); s++) noCliff &= !w->NestStyleFits(NS_CLIFF, s);
        check(noCliff, "a Cliff ledge wants ten shells");
        Nest c; c.style = NS_CLIFF; check(!NestOpen(c, NT_LAND) && !NestOpen(c, NT_THEFT), "nothing climbs to a Cliff ledge");
        // a burrow: hidden from scouts, and a storm floods it
        int ni = -1; for (int k = 0; k < (int)w->col.nests.size(); k++) if (w->col.nests[k].built) { ni = k; break; }
        if (ni >= 0) {
            int seen0 = w->TrueSighting(w->home).nests;
            w->col.nests[ni].style = NS_BURROW;
            check(w->TrueSighting(w->home).nests == seen0 - 1, TextFormat("a burrow is hidden from scouts (%d nests seen, was %d)", w->TrueSighting(w->home).nests, seen0));
            Bird e; e.id = w->col.nextId++; e.stage = BStage::Egg; e.nest = ni; e.pos = w->col.nests[ni].pos; w->col.birds.push_back(e); int eid = e.id;
            w->weather.kind = 1;
            for (float t = 0; t < World::DAY * 0.3f; t += 0.5f) w->StepNestStyles(0.5f);
            bool drowned = false; for (const auto& b : w->col.birds) if (b.id == eid) drowned = !b.alive;
            check(drowned, "a storm floods the burrow: its egg drowns");
            w->col.nests[ni].style = NS_MUD; w->col.nests[ni].rainT = 0;
            for (float t = 0; t < World::DAY * 2.1f; t += 0.5f) w->StepNestStyles(0.5f);
            check(!w->col.nests[ni].built, "two days of rain dissolve a mud nest");
            w->weather.kind = 0;
        }
        auto s = make(0, 111); s->col.nestStyle = NS_PLATFORM; Nest n; n.site = 0; s->StyleNest(n);
        check(n.style == NS_CUP, "a standard match lays cups");
    }    // ---- structures beyond nests (doc p49)
    {
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        auto s0 = make(0, 111);
        check(StructureTwigs(ST_PERCH) == 6 && StructureShells(ST_MONUMENT) == 60 && std::string(StructureName(ST_ROOKERY)) == "Rookery", "six new structures with their costs (a Monument: 60 shells)");
        check(!s0->BuildUnlocked(ST_PERCH) && w->BuildUnlocked(ST_PERCH) && !w->BuildUnlocked(ST_SMOKEHOUSE) && !w->BuildUnlocked(ST_MONUMENT), "they're the long match's; a smokehouse wants Caches 1, a Monument the autumn");
        w->time = 13.5f * World::DAY; bool lateOk = w->BuildUnlocked(ST_MONUMENT); w->time = 0;
        check(lateOk, "from autumn, a Monument can be raised");
        auto built = [&](int k) { w->LayStructure(k); auto& s = w->col.builds.back(); s.built = true; s.twigs = (float)StructureTwigs(k); return &s; };
        // a perch: the Watcher's post, and its sight
        built(ST_PERCH);
        Bird wt; wt.id = w->col.nextId++; wt.stage = BStage::Adult; wt.role = Role::Watcher; wt.hp = 60; wt.hunger = 1; wt.pos = w->col.caches[0].pos; w->col.birds.push_back(wt);
        for (int q = 0; q < 20; q++) w->Step(0.1f, FounderInput{});
        const Bird* wb = nullptr; for (const auto& b : w->col.birds) if (b.id == wt.id) wb = &b;
        check(wb && w->OnPerch(0, wb->post) && PerchSight() > 1, "a Watcher takes the perch as its post, and sees farther from it");
        // a smokehouse
        w->col.tier[(int)Tree::Caches] = 1; float sp0 = w->SpoilDays(); built(ST_SMOKEHOUSE);
        check(w->SpoilDays() > sp0 * 1.4f, TextFormat("a smokehouse keeps the caches longer (%.1f days to %.1f)", sp0, w->SpoilDays()));
        // the Rookery: eggs kept warm by any adult about it; chicks fledge together
        auto* ro = built(ST_ROOKERY); ro->pos = w->col.nests[0].pos;
        for (int q = 0; q < 3; q++) { Bird a; a.id = w->col.nextId++; a.stage = BStage::Adult; a.role = Role::Builder; a.hp = 60; a.hunger = 1; a.pos = ro->pos; a.task = Task::Sit; w->col.birds.push_back(a); }
        w->StepStructures(0.1f);
        check(w->col.rookeryWarm && w->InRookery(w->col.nests[0]), "three adults about the Rookery: its nests are warm");
        // the Beacon: every flock home at once
        built(ST_BEACON);
        std::vector<int> ids; for (int q = 0; q < 3; q++) { Bird k; k.id = w->col.nextId++; k.stage = BStage::Adult; k.role = Role::Skirmisher; k.hp = 50; k.hunger = 1; k.pos = w->col.caches[0].pos; w->col.birds.push_back(k); ids.push_back(k.id); }
        int fl = w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid);
        w->OrderFlock(0, fl, Target::Point, -1, -1, -1, -1, Vector3Add(w->col.caches[0].pos, {300, 20, 0}));
        w->time = World::DAY;
        bool lit = w->LightBeacon(); Flock* F = w->FindFlock(0, fl);
        check(lit && F && F->target == Target::Home && !w->LightBeacon(), "the Beacon calls every flock home at once (and is relit in a quarter day)");
        // Monuments: more than one
        w->time = 14 * World::DAY; w->col.shells = 200;
        built(ST_MONUMENT); bool second = w->LayStructure(ST_MONUMENT);
        check(second && MonumentsOf(w->col) == 1, "a second Monument can be laid once the first is raised");
    }    // ---- the long match's additions to the score (doc p50)
    {
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1;
        int base = w->LegacyScore(0);
        w->col.relics = (1u << RL_BELL) | (1u << RL_LENS);
        w->col.decreesUsed = 0xFFFFFu;   // (20 distinct decrees: capped at 60)
        Bird v; v.id = w->col.nextId++; v.stage = BStage::Adult; v.role = Role::Striker; v.hp = 60; v.vet = VT_LUCKY; w->col.birds.push_back(v);
        w->col.legend = LG_DODO; w->col.legendAlive = true;
        Structure m; m.kind = ST_MONUMENT; m.built = true; w->col.builds.push_back(m);
        size_t N = w->sides.size() + 1; w->truceUntil.assign(N * N, -1); w->truceUntil[0 * N + 1] = w->truceUntil[1 * N + 0] = w->time + World::DAY;
        int part[6] = {}; int got = w->LegacyScore(0, part);
        check(base == 0 && part[0] == 10 && part[1] == 120 && part[2] == 200 && part[3] == 30 && part[4] == 60 && part[5] == 20 && got == 440 && w->Score(0).legacy == got,
              TextFormat("a veteran 10, two relics 120, the Dodo 200, a Monument 30, decrees 60 (capped), a truce kept 20: %d", got));
        w->col.truceBroken = w->time;
        check(w->LegacyScore(0) == 420, "a colony that broke a truce gets nothing for the ones it kept");
        auto s = make(0, 111); s->col.relics = 3;
        check(s->LegacyScore(0) == 0, "a standard match has no legacy score");
    }    // ---- the expansion's islands (doc pp. 43-45)
    {
        struct Want { IsleType t; int sites; bool start; };
        const Want W[] = {{IsleType::Iceberg, 15, true}, {IsleType::Lighthouse, 18, true}, {IsleType::Shipwreck, 22, true}, {IsleType::Mangrove, 35, true}, {IsleType::KelpRaft, 20, true}, {IsleType::CliffTown, 30, true},
                          {IsleType::IronIsland, 10, false}, {IsleType::Whale, 10, false}, {IsleType::SirenRocks, 6, false}, {IsleType::Maelstrom, 4, false}, {IsleType::GhostShip, 3, false}, {IsleType::BirdIsland, 10, false}};
        bool shapes = true; std::string bad;
        for (const auto& q : W) {
            Island is; is.Generate(q.t, 77, {0, 0, 0});
            bool ok = (int)is.sites.size() >= q.sites && (q.start ? IsStartType(q.t) && !IsDangerous(q.t) : IsDangerous(q.t) && !IsStartType(q.t)) && !is.outline.empty();
            if (q.t != IsleType::GhostShip && q.t != IsleType::Mangrove) ok = ok && is.Height(is.hill.x, is.hill.z) > 0;   // (the Mangrove is roots over shallow water)
            if (!ok) { shapes = false; bad += std::string(" ") + IsleTypeName(q.t) + TextFormat("(%d sites)", (int)is.sites.size()); }
        }
        check(shapes, "twelve new islands, each with its sites (the Iceberg 15, the Lighthouse 18, the Shipwreck 22, the Mangrove 35, the Kelp Raft 20, the Cliff Town 30)" + bad);
        int seenNew = 0, ghosts = 0;
        for (uint32_t sd = 1; sd <= 6; sd++) { auto m = std::make_unique<World>(); MapOpts o; o.players = 6; o.seasons = 4; m->Init("taloned", sd, o);
            for (const auto& is : m->isles) { seenNew += is.type >= IsleType::Iceberg && is.type != IsleType::GhostShip; ghosts += is.type == IsleType::GhostShip; } }
        auto s0 = make(0, 5); int newInStd = 0; for (const auto& is : s0->isles) newInStd += is.type >= IsleType::Iceberg;
        check(seenNew > 6 && ghosts == 6 && newInStd == 0, TextFormat("long-match maps draw from all of them (%d new islands over six maps, a Ghost Ship on each); a standard match has none", seenNew));
        // the rules, island by island (an island of the map turned into the one under test)
        auto turn = [&](World& v, int k, IsleType t) { v.isles[k].Generate(t, 9, v.isles[k].c); v.InitIsles(); };
        auto w = make(4, 111); w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        int far = -1; for (int k = 0; k < (int)w->isles.size(); k++) if (k != w->home && w->isles[k].start < 0 && w->isles[k].type != IsleType::KrakenCove && w->isles[k].type != IsleType::Wreck) { far = k; break; }
        turn(*w, far, IsleType::Iceberg);
        check(w->IsleShields(far, NT_TEAR) && w->IsleShields(far, NT_THEFT) && !w->IsleShields(far, NT_LAVA), "the Iceberg: raiders can't land on sheer ice");
        turn(*w, far, IsleType::Lighthouse);
        w->time = 0.28f * World::DAY; bool day = w->IsleShields(far, NT_THEFT); w->time = 0.9f * World::DAY; bool night = w->IsleShields(far, NT_THEFT);
        check(!day && night, "Lighthouse Rock: the beam blinds night raiders (and only at night)");
        turn(*w, far, IsleType::GhostShip); w->time = 0; w->SetGhostPose(); Vector3 g0 = w->isles[far].c; w->time = 2 * World::DAY; w->SetGhostPose();
        check(Vector3Distance(g0, w->isles[far].c) > 20 && !w->FoundOutpost(far, w->isles[far].c), "the Ghost Ship drifts, and can't be held");
        turn(*w, far, IsleType::BirdIsland);
        check(!w->FoundOutpost(far, w->isles[far].c), "Bird Island can't be landed on and held");
        {   // priests convert it
            Bird pr; pr.id = w->col.nextId++; pr.stage = BStage::Adult; pr.role = Role::Priest; pr.hp = 60; pr.hunger = 1; pr.pos = w->isles[far].c; pr.pos.y = 80; w->col.birds.push_back(pr);
            w->isx.birdConv[0] = 99; w->time = 5 * World::DAY; w->StepIsles(0.1f);
            check(w->HolderOf(far) == 0, "Bird Island: priests preaching at its edge convert it, and its colony is yours");
        }
        turn(*w, far, IsleType::Maelstrom);
        {
            w->wind.speed = 9; w->wind.nextSpeed = 9;
            bool refused = !w->FoundOutpost(far, w->isles[far].c);
            Bird lo; lo.id = w->col.nextId++; lo.stage = BStage::Adult; lo.role = Role::Fisher; lo.hp = 60; lo.hunger = 1; lo.pos = Vector3Add(w->isles[far].c, {40, 5, 0}); w->col.birds.push_back(lo); int lid = lo.id;
            for (int q = 0; q < 60; q++) { w->time = 10 * World::DAY + q; for (auto& b : w->col.birds) if (b.id == lid) b.pos = Vector3Add(w->isles[far].c, {40, 5, 0}); w->StepIsles(0.05f); }
            bool dead = false; for (const auto& b : w->col.birds) if (b.id == lid) dead = !b.alive;
            check(refused && dead, "the Maelstrom: its rocks can't be reached on a windy day, and a bird low over it is pulled in");
        }
        turn(*w, far, IsleType::SirenRocks);
        {
            Bird sb; sb.id = w->col.nextId++; sb.stage = BStage::Adult; sb.role = Role::Fisher; sb.hp = 60; sb.hunger = 1; sb.pos = Vector3Add(w->isles[far].c, {60, 20, 0}); w->col.birds.push_back(sb); int sid = sb.id;
            for (int q = 0; q < 200; q++) { w->time = 12 * World::DAY + q * 0.05f * World::DAY; w->StepIsles(0.1f); }
            float song = 0; for (const auto& b : w->col.birds) if (b.id == sid) song = b.songT;
            check(song > 0, "the Siren Rocks: a bird within the song flies to the rocks and sits");
        }
        turn(*w, far, IsleType::IronIsland);
        {
            std::vector<int> ids; for (int q = 0; q < 8; q++) { Bird k; k.id = w->col.nextId++; k.stage = BStage::Adult; k.role = Role::Skirmisher; k.hp = 50; k.hunger = 1; k.pos = Vector3Add(w->isles[far].c, {q * 2.0f, 30, 0}); w->col.birds.push_back(k); ids.push_back(k.id); }
            w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid);
            for (int q = 0; q < 40; q++) w->StepIsles(0.1f);
            int alive = 0; for (int id : ids) alive += w->FindBird(0, id) != nullptr;
            check(alive < 8, TextFormat("Iron Island: the fort's cannons fire on a flock of more than six (%d of 8 left)", alive));
        }
        turn(*w, far, IsleType::Whale);
        {
            Nest n; n.isle = far; n.pos = w->isles[far].sites[0]; n.built = true; w->col.nests.push_back(n); int ni = (int)w->col.nests.size() - 1;
            w->time = w->isx.whaleNext; w->StepIsles(0.1f);
            check(!w->col.nests[ni].built && w->isx.whaleUnderT > 0, "the Whale dives once a season: everything on its back is in the sea");
        }
        // the starting islands' gifts
        auto st = [&](IsleType t) { auto v = std::make_unique<World>(); MapOpts o; o.players = 2; o.seasons = 4; o.home = t; v->Init("taloned", 21, o); v->ape.isle = -1; v->kraken.isle = -1; return v; };
        { auto v = st(IsleType::Shipwreck); int f = 0; for (const auto& c : v->col.caches) f += (int)c.fish.size(); check(v->col.bell && f >= 20, TextFormat("the Shipwreck Island: a hold of salted fish (%d) and the ship's bell", f)); }
        { auto v = st(IsleType::CliffTown); int towns = 0; for (const auto& t : v->towns) towns += t.isle == v->home; check(towns == 2, TextFormat("the Cliff Town: two markets (%d; home is %s, %d towns)", towns, IsleTypeName(v->isles[v->home].type), (int)v->towns.size())); }
        { auto v = st(IsleType::Iceberg); Nest n; n.isle = v->home; n.pos = v->isles[v->home].sites[3]; v->col.nests.push_back(n); int ni = (int)v->col.nests.size() - 1; int sh = v->col.shells; v->StepIsles(0.1f);
          check(v->col.nests[ni].built && v->col.shells == sh - (int)IcebergShells(), "the Iceberg: no twigs; an ice nest is cut with shells"); }
        { auto v = st(IsleType::Lighthouse); for (int q = 0; q < 20; q++) v->col.caches[0].fish.push_back({0, 2, 0}); int p0 = v->col.pearls; v->time = World::DAY; v->StepIsles(0.1f);
          check(v->col.pearls == p0 + 1, "Lighthouse Rock: the keeper trades lamp oil for fish"); }
    }    // ---- a guest's mirror of a long match: the same map, the expansion's islands and all
    {
        auto h = std::make_unique<World>(); MapOpts o; o.players = 4; o.seasons = 4; o.home = IsleType::Mangrove; o.multi = true; o.humanMask = 3; h->Init("taloned", 33, o);
        Writer wrt; WriteWorld(*h, 1, wrt, true);
        auto m = std::make_unique<World>(); Reader rdr(wrt.b.data(), wrt.b.size());
        bool ok = ReadWorld(rdr, *m) && m->isles.size() == h->isles.size() && m->seasons == 4;
        for (size_t i = 0; ok && i < h->isles.size(); i++) ok = m->isles[i].type == h->isles[i].type && Vector3Distance(m->isles[i].c, h->isles[i].c) < 0.5f;
        check(ok && m->isles[0].type == IsleType::Mangrove, "a guest's mirror of a long match has the same map (the expansion's islands, the Ghost Ship where it has drifted)");
    }    printf(fails ? "flight-long-test: %d check(s) failed\n" : "flight-long-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
}  // namespace fl
