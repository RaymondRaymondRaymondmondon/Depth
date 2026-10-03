// The Flight's long match (the expansion, design doc pp. 35-51): the seasons, their events and their pull on the sea,
// the wind and the work. Everything a season does is a number in data/flight/flight_long.json.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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


// ---------------------------------------------------------------- --flight-long-test (the expansion's long match)
int RunFlightLongTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, the long match: seasons and their events (doc pp. 35-36)\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    const auto& S = Seasons();
    check(S.size() == 4 && S[0].name == "Spring" && S[3].name == "Winter" && S[1].firstDay == 4 && S[2].firstDay == 8 && S[3].firstDay == 12,
          "four seasons: Spring 1-3, Summer 4-7, Autumn 8-11, Winter 12-16");
    check(SeasonDays(2) == 7 && SeasonDays(3) == 11 && SeasonDays(4) == 16, "a match by seasons: two 7 days, three 11, four 16");
    auto make = [](int seasons, uint32_t seed = 41) { auto w = std::make_unique<World>(); MapOpts o; o.players = 2; o.seasons = seasons; w->Init("taloned", seed, o); return w; };
    {
        auto w = make(4);
        bool ok = w->matchLen == 16 * World::DAY;
        int got[5] = {}; for (int d : {1, 5, 9, 13}) { w->time = (d - 0.5f) * World::DAY; got[d / 4] = w->Season(); }
        check(ok && got[0] == SEASON_SPRING && got[1] == SEASON_SUMMER && got[2] == SEASON_AUTUMN && got[3] == SEASON_WINTER, "a four-season match runs 16 days; days 1, 5, 9 and 13 fall in spring, summer, autumn and winter");
        auto s = make(0);
        check(s->Season() == -1 && s->matchLen == 0 && s->RegrowMul(0) == 1, "a standard match has no seasons and no limit");
        // the season's pull on the sea, the wind and the work
        int nearZ = -1, farZ = -1; for (int z = 0; z < (int)w->zoneNear.size(); z++) { if (w->zoneNear[z] && nearZ < 0) nearZ = z; if (!w->zoneNear[z] && farZ < 0) farZ = z; }
        w->time = 5.5f * World::DAY; w->seasonEvent = -1;
        check(nearZ >= 0 && farZ >= 0 && w->RegrowMul(nearZ) < 1 && w->RegrowMul(farZ) > 1, TextFormat("summer thins the grounds near the shores (x%.1f) and fattens the blue (x%.1f)", w->RegrowMul(nearZ), w->RegrowMul(farZ)));
        w->time = 9.5f * World::DAY; float autumnWind = w->SeasonNow().windK; w->time = 1.5f * World::DAY; float springWind = w->SeasonNow().windK;
        w->time = 13.5f * World::DAY;
        check(autumnWind > 1.4f && springWind < 0.8f && w->SeasonNow().stamina > 1.2f && w->SeasonNow().fishDepth > 1, "spring's wind is light and autumn's a gale; winter costs breath and sends the fish deep");
    }
    {   // the events, each once on its day
        auto w = make(4, 7);
        w->ape.isle = -1; w->kraken.isle = -1; w->weather.next = 1e9f;
        int fired[EV_SEASON_COUNT] = {}; bool darkLongNight = true, sharksDeep = true, tunaMoved = false; int tunaBorn = 0, matesBefore = w->ColOf(0).wildMates, matesAfter = 0; float tunaX0 = 0;
        int last = -1;
        for (float t = 0; t < World::DAY * 16 && !w->over; t += 0.25f) {
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
        w->time = 5 * World::DAY; ScoreCard a = w->Score(0);
        w->time = 14 * World::DAY; ScoreCard b = w->Score(0);
        check(a.nests > 0 && b.nests == (int)lroundf(a.nests * WinterHoldings()), TextFormat("Winter's nests count double (%d in summer, %d in winter)", a.nests, b.nests));
    }
    {   // two bot colonies through a two-season match: it ends on its last day
        auto w = make(2, 21);
        w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0});
        float t0 = 0;
        for (; t0 < World::DAY * 8 && !w->over; t0 += 0.1f) { w->BotGovern(0.1f); w->BotWar(0, 0.1f); w->Step(0.1f, FounderInput{}); }
        check(w->over && fabsf(w->time - 7 * World::DAY) < 2, TextFormat("a two-season bot match ends on day 7 (%s; %d and %d birds)", w->overReason.c_str(), w->Alive(), (int)w->ColOf(1).birds.size()));
    }
    printf(fails ? "flight-long-test: %d check(s) failed\n" : "flight-long-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
}  // namespace fl
