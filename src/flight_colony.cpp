// The Flight, stage 2: the colony loop (design doc pp. 3-7, 11, 33). Fish -> feed yourself -> a courtship -> a mate ->
// eggs -> chicks (fed) -> adults (fed) -> more fish, more nests, more mates. Headless; the numbers are data
// (data/flight/flight_economy.json, flight_roles.json).
//
// Colony birds are kinematic flyers (no stamina model): they fly to a job, and the jobs read the real sea. Fishers
// take real fish out of Red Tide's web, and the grounds regrow logistically (RegrowFish), so a ground fished past its
// yield collapses and recovers only slowly: the forced expansion of the design.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace fl {

// ---------------------------------------------------------------- data
const Economy& Econ() {
    static Economy e; static bool loaded = false;
    if (loaded) return e;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_economy.json");
    if (!j.IsObj()) return e;
    auto F = [&](const char* k, float& v) { if (j.Has(k)) v = j[k].F(v); };
    auto I = [&](const char* k, int& v) { if (j.Has(k)) v = j[k].I(v); };
    F("day_seconds", e.daySeconds); F("work_pace", e.workPace);
    if (e.workPace <= 0) e.workPace = 240 / std::max(30.0f, e.daySeconds);   // (0: the pace the doc's 4-minute day was tuned at)
    F("feed_adult", e.feedAdult); F("feed_chick", e.feedChick); F("feed_founder", e.feedFounder); F("chick_drain", e.chickDrain);
    F("starve_days", e.starveDays); I("cache_capacity", e.cacheCap); F("spoil_days", e.spoilDays);
    I("courtship_fish", e.courtFish); I("courtship_step", e.courtStep); I("courtship_min_size", e.courtMinSize);
    F("mate_arrive_min_days", e.mateMin); F("mate_arrive_max_days", e.mateMax); F("tropical_mate_mult", e.tropicalMate); I("wild_mates", e.wildMates);
    I("clutch_min", e.clutchMin); I("clutch_max", e.clutchMax); F("clutch_every_days", e.clutchDays); I("clutches_per_mate", e.clutches); I("nest_max_eggs", e.nestEggs);
    F("hatch_days", e.hatchDays); F("egg_chill_days", e.chillDays); F("chick_days", e.chickDays); F("overfeed_growth", e.overfeed);
    F("lining_hatch", e.liningHatch); I("lining_shells", e.liningShells);
    I("nest_twigs", e.nestTwigs); I("cache_twigs", e.cacheTwigs); F("retrain_days", e.retrainDays);
    if (j["regrow_per_day_by_size"].IsArr()) for (int k = 0; k < 7 && k < (int)j["regrow_per_day_by_size"].a.size(); k++) e.regrow[k] = j["regrow_per_day_by_size"][k].F(e.regrow[k]);
    F("immigration_per_day", e.immigration); I("sites", e.sites); I("palm_twigs", e.palmTwigs); F("twig_regrow_days", e.twigRegrowDays);
    F("self_fetch_s", e.selfFetchS); F("fed_s", e.fedS); I("feeder_cover", e.feederCover);
    return e;
}
const RoleDef& RoleOf(Role r) {
    static std::vector<RoleDef> v;
    if (v.empty()) {
        v.resize((int)Role::COUNT);
        v[0] = {"none", "Fledgling", "", 60, 12, 1};
        v[1] = {"fisher", "Fisher", "Fishes a ground and brings the catch to the nearest cache.", 60, 12, 2};
        v[2] = {"feeder", "Feeder", "Carries fish from the caches to the nests.", 60, 14, 2};
        v[3] = {"builder", "Builder", "Gathers twigs and shells; builds nests and caches.", 70, 11, 3};
        v[4] = {"scout", "Scout", "Flies to a target at the height you set, looks, and comes home to report.", 50, 18, 0};
        v[5] = {"skirmisher", "Skirmisher", "Quick attacks: hit, climb, hit again; raids caches and nests; harasses fishers.", 50, 20, 1};
        v[6] = {"tank", "Tank", "Takes hits: a wall in the air; escorts.", 180, 10, 0};
        v[7] = {"striker", "Striker", "Strong, slow attacks from above: the killer; takes chicks.", 90, 14, 0};
        v[8] = {"watcher", "Watcher", "Island defence: guards the nests and caches, throws nets that hold a bird.", 80, 9, 0};
        v[9] = {"screamer", "Screamer", "Morale: raises its flock's and lowers the enemy's.", 60, 15, 0};
        v[10] = {"flockmaster", "Flockmaster", "Leads a flock without the Founder: holds formation, +5% speed.", 90, 13, 0};
        Json j = LoadJsonFile(FlightDataDir() + "/flight_roles.json");
        for (const Json& r : j.a) {
            std::string k = r["key"].Str0();
            for (auto& d : v) if (d.key == k) { d.name = r["name"].Str0(d.name.c_str()); d.what = r["what"].Str0(d.what.c_str()); d.hp = r["hp"].F(d.hp); d.speed = r["speed"].F(d.speed); d.carry = r["carry"].I(d.carry); }
        }
        // war: attack, cooldown, the height it likes, what it beats and what beats it (flight_war.json)
        Json war = LoadJsonFile(FlightDataDir() + "/flight_war.json");
        auto bit = [&](const std::string& key) { for (int i = 0; i < (int)v.size(); i++) if (v[i].key == key) return 1u << i; return 0u; };
        for (auto& d : v) {
            const Json& r = war["roles"][d.key];
            if (!r.IsObj()) continue;
            d.hp = r["hp"].F(d.hp); d.speed = r["speed"].F(d.speed); d.attack = r["attack"].F(d.attack); d.cooldown = r["cooldown"].F(d.cooldown);
            std::string a = r["alt"].Str0("mid"); d.pref = a == "high" ? Alt::High : a == "low" ? Alt::Low : Alt::Mid; d.perched = a == "perched";
            for (const Json& s : r["strong"].a) d.strong |= bit(s.Str0());
            for (const Json& s : r["weak"].a) d.weak |= bit(s.Str0());
        }
    }
    return v[std::clamp((int)r, 0, (int)Role::COUNT - 1)];
}
const char* RoleName(Role r) { return RoleOf(r).name.c_str(); }

// ---------------------------------------------------------------- helpers
namespace {
bool Catchable(const rt::Species& s) { return !s.isDiver && !s.isEnemy && s.tier < 4 && s.size <= 4 && !s.Has("protected"); }
bool EatsBirds(const rt::Ecosystem& e, int sp) {
    if (e.diverSpecies < 0 || sp < 0 || sp >= (int)e.map->diet.size()) return false;
    for (const auto& pr : e.map->diet[sp].prey) if (pr.first == e.diverSpecies) return true;
    return false;
}
float Dist2(Vector3 a, Vector3 b) { return Vector2Distance({a.x, a.z}, {b.x, b.z}); }
constexpr float FISHER_REACH = 2.7f;   // how deep a colony bird's plunge reaches (the lagoon's floor is 3 m down)
}  // namespace

float World::StockOf(int zone) const {
    float n = 0, k = 0;
    bool asleep = wholeMap && zone >= 0 && zone < (int)liveZone.size() && !liveZone[zone];
    for (const auto& s : stocks) if (s.zone == zone && Catchable(eco.map->species[s.sp])) {
        k += s.K;
        if (asleep) { n += s.pop; continue; }
        for (const auto& a : eco.agents) if (a.alive && a.diver < 0 && a.sp == s.sp && a.homeZone == s.zone) n++;
    }
    return k > 0 ? n / k : 0;
}
int World::Count(BStage st, Role r) const {
    int n = 0;
    for (const auto& b : col.birds) if (b.alive && b.stage == st && (r == Role::None || b.role == r)) n++;
    return n;
}
int World::Adults() const { return Count(BStage::Adult) + Count(BStage::Mate) + (me.st != FState::Dead ? 1 : 0); }
int World::Alive() const { return Count(BStage::Adult) + Count(BStage::Mate) + Count(BStage::Chick) + 1; }
float World::MouthsPerDay() const {
    const Economy& E = Econ();
    float m = E.feedFounder;
    for (const auto& b : col.birds) if (b.alive) m += b.stage == BStage::Chick ? E.feedChick : b.stage == BStage::Egg ? 0 : E.feedAdult;
    return m;
}
float World::CacheFeed() const {
    float f = 0;
    for (const auto& c : col.caches) for (const auto& x : c.fish) f += x.size;
    for (const auto& n : col.nests) f += n.larder;
    return f;
}
float World::FeedPerDayEstimate() const {
    float frac = dayAcc / DAY;
    if (dayNum == 0) return frac > 0.1f ? col.feedToday / frac : 0;
    return frac > 0.5f ? col.feedToday / frac : col.feedYesterday * (1 - frac) + col.feedToday;
}
float World::DaysOfFood() const { float m = MouthsPerDay(); return m > 0 ? CacheFeed() / m : 0; }
int World::NearestCache(Vector3 p, bool withFish, bool withRoom) const {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)col.caches.size(); i++) {
        const Cache& c = col.caches[i];
        if (!c.built) continue;
        if (withFish && c.fish.empty()) continue;
        if (withRoom && (int)c.fish.size() >= Econ().cacheCap) continue;
        float d = Vector3Distance(p, c.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int World::NearestNest(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)col.nests.size(); i++) { float d = Vector3Distance(p, col.nests[i].pos); if (d < bd) { bd = d; best = i; } }
    return best;
}
int World::NearestSite(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)col.sites.size(); i++) { if (col.sites[i].nest >= 0) continue; float d = Vector3Distance(p, col.sites[i].pos); if (d < bd) { bd = d; best = i; } }
    return best;
}
int World::NearestTwigs(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)col.twigSrc.size(); i++) { const auto& s = col.twigSrc[i]; if (s.shells || s.twigs < 1) continue; float d = Vector3Distance(p, s.pos); if (d < bd) { bd = d; best = i; } }
    return best;
}

// ---------------------------------------------------------------- setting up
void World::InitColony() {
    const Economy& E = Econ();
    col = Colony{};
    col.wildMates = E.wildMates;
    // nest sites: the island's own (palm crowns and beach, ledges, roofs and the tower, the ring), the nearest to home first
    std::vector<Vector3> sites = island.sites;
    std::sort(sites.begin(), sites.end(), [&](const Vector3& a, const Vector3& b) { return Vector3Distance(a, island.nest) < Vector3Distance(b, island.nest); });
    for (int k = 0; k < (int)sites.size() && (int)col.sites.size() < std::max(E.sites, (int)sites.size()); k++) {
        Site s; s.pos = sites[k];
        for (int i = 0; i < (int)island.palms.size(); i++) if (Vector2Distance({island.palms[i].x, island.palms[i].z}, {s.pos.x, s.pos.z}) < 0.2f) s.palm = i;
        col.sites.push_back(s);
    }
    // the Founder's own nest, built; its courtship bowl waits for the first mate
    Nest home; home.pos = island.nest; home.built = true; home.founders = true; home.twigs = (float)E.nestTwigs; home.bowlNeed = E.courtFish;
    { int best = -1; float bd = 3; for (int i = 0; i < (int)col.sites.size(); i++) { float d = Vector3Distance(col.sites[i].pos, island.nest); if (d < bd) { bd = d; best = i; } } if (best >= 0) { home.site = best; col.sites[best].nest = 0; } }
    col.nests.push_back(home);
    // the first cache: on flat ground by home (the foot of the home palm; the same ledge on the stack; a roof in town)
    Vector3 foot = island.nestPalm >= 0 ? island.palms[island.nestPalm] : island.nest;
    Vector3 cp = island.Ground(foot.x + 2.5f, foot.z + 1.5f);
    bool found = island.nestPalm >= 0 && island.Land(cp.x, cp.z);
    for (int k = 0; k < 64 && !found; k++) {
        float a = k * 0.7f, r = 2 + (k % 8) * 0.8f;
        Vector3 q = island.Ground(island.nest.x + cosf(a) * r, island.nest.z + sinf(a) * r);
        if (island.Land(q.x, q.z) && fabsf(q.y - island.nest.y) < 1.5f && island.Normal(q.x, q.z).y > 0.8f) { cp = q; found = true; }
    }
    if (!found) cp = island.Ground(island.nest.x + 2, island.nest.z + 2);
    Cache c0; c0.pos = {cp.x, cp.y + 0.1f, cp.z};
    col.caches.push_back(c0);
    // twigs: palms drop fronds and sticks; driftwood on the beaches and ledges; the town's woodpile; shells
    for (const auto& tp : island.twigPts) { TwigSource t; t.pos = tp.first; t.cap = tp.second < 0 ? (float)E.palmTwigs : tp.second; t.twigs = t.cap; col.twigSrc.push_back(t); }
    for (const auto& sp : island.shellPts) { TwigSource t; t.pos = Vector3Add(sp, {0, 0.15f, 0}); t.shells = true; t.cap = 4; t.twigs = t.cap; col.twigSrc.push_back(t); }    // the grounds: the sea's spawn rows, regrown logistically from here on
    if (!stocks.empty()) { dayAcc = 0; dayNum = 0; return; }   // (a rival's colony: the grounds are the world's, already counted)
    for (int ri = 0; ri < (int)eco.map->spawns.size(); ri++) {
        const auto& r = eco.map->spawns[ri];
        Stock s; s.row = ri; s.sp = eco.map->SpeciesIndex(r.species); s.zone = eco.map->ZoneIndex(r.zone); s.K = (float)r.count;
        if (s.sp >= 0 && s.zone >= 0) stocks.push_back(s);
    }
    lagoonZone = eco.map->ZoneIndex("The Lagoon");
    dayAcc = 0; dayNum = 0;
}

// ---------------------------------------------------------------- the grounds regrow (stock-dependent: overfishing collapses a ground)
void World::RegrowFish(float dt) {
    static float acc = 0; acc += dt;
    if (acc < 2) return;
    float step = acc; acc = 0;
    const Economy& E = Econ();
    for (auto& s : stocks) {
        const rt::Species& sp = eco.map->species[s.sp];
        if (wholeMap && s.zone < (int)liveZone.size() && !liveZone[s.zone]) {   // (asleep: the count regrows as a number)
            float r = E.regrow[std::clamp(sp.size, 0, 6)];
            s.pop = std::min(s.K, s.pop + step / DAY * (r * s.pop * (1 - s.pop / std::max(1.0f, s.K)) + E.immigration * s.K));
            continue;
        }
        int n = 0; int any = -1;
        for (int i = 0; i < (int)eco.agents.size(); i++) { const auto& a = eco.agents[i]; if (a.alive && a.diver < 0 && a.sp == s.sp && a.homeZone == s.zone) { n++; if (any < 0 || Rand() < 0.2f) any = i; } }
        float r = E.regrow[std::clamp(sp.size, 0, 6)];
        float N = (float)n, K = std::max(1.0f, s.K);
        s.births += step / DAY * (r * N * (1 - N / K) + E.immigration * K);
        while (s.births >= 1 && n < (int)ceilf(K)) {
            s.births -= 1;
            Vector3 at = any >= 0 ? eco.agents[any].pos : eco.map->zones[s.zone].Center();
            int ai = SpawnFishSlot(eco, s.sp, eco.map->zones[s.zone].Clamp(Vector3Add(at, {Rand() * 4 - 2, Rand() * 2 - 1, Rand() * 4 - 2})), s.zone);
            if (ai >= 0) { eco.agents[ai].homeZone = s.zone; if (any >= 0) eco.agents[ai].group = eco.agents[any].group; }
            n++;
        }
        if (n >= (int)ceilf(K)) s.births = 0;
    }
}

// ---------------------------------------------------------------- flying
bool World::MoveTo(Bird& b, Vector3 goal, float speed, float dt, float arrive) {
    speed *= Econ().workPace;
    Vector3 d = Vector3Subtract(goal, b.pos);
    float flat = sqrtf(d.x * d.x + d.z * d.z);
    if (Vector3Length(d) < arrive) { b.vel = Vector3Scale(b.vel, 0.5f); return true; }
    // travel at a cruising height, coming down over the last stretch
    float cruise = std::max(goal.y, std::max(0.0f, HeightAt(b.pos.x, b.pos.z)) + 10);
    Vector3 aim = flat > 14 ? Vector3{goal.x, std::max(goal.y, cruise), goal.z} : goal;
    Vector3 want = Vector3Scale(Vector3Normalize(Vector3Subtract(aim, b.pos)), speed);
    b.vel = Vector3Lerp(b.vel, want, std::min(1.0f, dt * 3 * Econ().workPace));
    b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
    float g = std::max(0.0f, HeightAt(b.pos.x, b.pos.z));
    if (b.pos.y < g + 0.3f && flat > 3) b.pos.y = g + 0.3f;
    if (Vector2Length({b.vel.x, b.vel.z}) > 0.3f) b.yaw = atan2f(b.vel.z, b.vel.x);
    b.flapPh += dt * 2 * PI * (b.vel.y > 0.5f ? 3.4f : 2.2f);
    return false;
}
// a meal at a cache: a feeder hands it over in a moment, or the bird fetches its own (a third of its day lost, over a day)
bool World::BirdEatsAtCache(Bird& b, float dt) {
    int ci = NearestCache(b.pos, true, false);
    if (ci < 0) return false;
    Cache& c = col.caches[ci];
    if (!MoveTo(b, Vector3Add(c.pos, {0, 0.6f, 0}), RoleOf(b.role).speed, dt)) { b.task = Task::Eat; return true; }
    b.task = Task::Eat; b.taskT += dt;
    int feeders = Count(BStage::Adult, Role::Feeder), workers = Count(BStage::Adult) - feeders;
    bool served = feeders > 0 && feeders * Econ().feederCover >= workers;
    if (b.taskT < (served ? Econ().fedS : Econ().selfFetchS) / Econ().workPace) return true;
    size_t k = 0; for (size_t i = 1; i < c.fish.size(); i++) if (c.fish[i].age > c.fish[k].age) k = i;
    b.hunger = std::min(1.0f, b.hunger + c.fish[k].size / Econ().feedAdult);
    c.fish.erase(c.fish.begin() + k);
    b.task = Task::Idle; b.taskT = 0;
    return true;
}
int World::ChooseGround(Vector3 from) const {
    if (col.ground >= 0) return col.ground;
    // the best ground: fish near the surface that a fisher can lift, over the distance to get there
    std::vector<float> score(eco.map->zones.size(), 0);
    int carry = RoleOf(Role::Fisher).carry + (Def().key == "strongbird" ? 2 : Def().key == "swift" ? -1 : 0);
    for (const auto& a : eco.agents) {
        if (!a.alive || a.diver >= 0 || a.pos.y < -FISHER_REACH || a.zone < 0) continue;
        const rt::Species& s = eco.map->species[a.sp];
        if (!Catchable(s) || s.size > carry) continue;
        score[a.zone] += expf(-Dist2(from, a.pos) / 100);   // (a fisher wants the nearest good patch: far fish are worth little)
    }
    // waters asleep (no fish drawn there now): their count, at the share the time of day brings within reach
    if (wholeMap) {
        float ph = DayPhase();
        bool rise = (ph > 0.19f && ph < 0.34f) || (ph > 0.68f && ph < 0.82f), night = ph < 0.19f || ph > 0.87f;
        for (const auto& s : stocks) {
            if (s.zone >= (int)liveZone.size() || liveZone[s.zone]) continue;
            const rt::Species& sp = eco.map->species[s.sp];
            if (!Catchable(sp) || sp.size > carry) continue;
            const rt::Zone& Z = eco.map->zones[s.zone];
            float reach = Z.y0 > -FISHER_REACH - 0.5f ? 1.0f : rise ? 1.0f : night ? 0.0f : 0.2f;
            float dz = std::max(0.0f, Dist2(from, Z.Center()) - std::min(Z.plan.width, Z.plan.height) * 0.4f);
            score[s.zone] += s.pop * reach * expf(-dz / 100);
        }
    }
    int best = wholeMap ? -1 : lagoonZone; float bs = -1;
    for (int z = 0; z < (int)score.size(); z++) {
        const rt::Zone& Z = eco.map->zones[z];
        Vector3 c = Z.Center();
        float s = score[z]; (void)c;
        if (wholeMap && s < 0.3f) continue;   // (not worth the trip)
        if (col.restBelow > 0 && score[z] > 0 && StockOf(z) < col.restBelow) s *= 0.02f;   // (resting: only if nothing else is left)
        if (s > bs) { bs = s; best = z; }
    }
    return best;
}

// ---------------------------------------------------------------- the jobs
void World::FisherStep(Bird& b, float dt) {
    const RoleDef& R = RoleOf(b.role == Role::None ? Role::Fisher : b.role);
    int carry = R.carry + (Def().key == "strongbird" ? 2 : Def().key == "swift" ? -1 : 0);
    if (&b == &fb) carry = me.Carry(Def());
    float speed = R.speed * (Def().key == "swift" ? 1.25f : Def().key == "taloned" ? 0.9f : 1.0f);
    if (b.carrySp >= 0) {
        // home with it: a courtship fish to its bowl, otherwise the nearest cache with room (hungry, it eats it)
        if (b.hunger < 0.25f && NearestCache(b.pos, true, false) < 0) { b.hunger = std::min(1.0f, b.hunger + b.carrySize / Econ().feedAdult); b.carrySp = -1; b.carrySize = 0; return; }
        if (b.target >= 1000 && b.target - 1000 < (int)col.nests.size()) {
            Nest& n = col.nests[b.target - 1000];
            if (MoveTo(b, Vector3Add(n.pos, {0, 0.4f, 0}), speed, dt)) {
                if (n.mate < 0 && n.mateT < 0) n.bowl++;
                n.larder += b.carrySize;
                b.carrySp = -1; b.carrySize = 0; b.target = -1; b.task = Task::Idle;
            }
            return;
        }
        int ci = NearestCache(b.pos, false, true);
        if (ci < 0) { b.carrySp = -1; b.carrySize = 0; return; }   // (nowhere to put it: dropped)
        b.task = Task::Deliver;
        if (MoveTo(b, Vector3Add(col.caches[ci].pos, {0, 0.6f, 0}), speed, dt)) {
            col.caches[ci].fish.push_back({b.carrySp, b.carrySize, 0});
            col.feedToday += b.carrySize; col.caughtToday++; b.caught++;
            b.carrySp = -1; b.carrySize = 0; b.task = Task::Idle;
        }
        return;
    }
    if (b.fleeT > 0) { b.fleeT -= dt; b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {0.5f, 0.4f, 0.5f}), speed, dt, 0.5f); return; }   // (chased off its ground)
    if (b.hunger < 0.35f && BirdEatsAtCache(b, dt)) return;
    int minSize = b.target >= 1000 ? Econ().courtMinSize : 1;
    // in waters that sleep (no bird of yours near: a rival's fisher far away) the catch comes from the ground's count
    if (wholeMap && b.task == Task::Search) {
        int z = eco.ZoneAt({b.goal.x, -1, b.goal.z});
        if (z >= 0 && z < (int)liveZone.size() && !liveZone[z]) {
            b.taskT += dt * Econ().workPace;
            if (b.taskT > 6) {
                b.taskT = 0;
                float stock = StockOf(z), total = 0;
                for (const auto& s : stocks) if (s.zone == z) { const rt::Species& sp = eco.map->species[s.sp]; if (Catchable(sp) && sp.size <= carry && sp.size >= minSize) total += s.pop; }
                if (total >= 1 && Rand() < 0.55f * std::clamp(stock * 1.5f, 0.1f, 1.0f)) {
                    float pick = Rand() * total;
                    for (auto& s : stocks) if (s.zone == z) { const rt::Species& sp = eco.map->species[s.sp]; if (!Catchable(sp) || sp.size > carry || sp.size < minSize) continue; pick -= s.pop; if (pick <= 0) { s.pop -= 1; b.carrySp = s.sp; b.carrySize = sp.size; b.task = Task::Idle; break; } }
                }
                if (b.carrySp < 0 && Rand() < 0.3f) b.task = Task::Idle;
            }
            return;
        }
    }
    switch (b.task) {
    default:
    case Task::Idle: {
        // the ground, then where its fish show from the air (schools are dark patches; a lone fish a glint)
        int z = ChooseGround(b.pos);
        if (z < 0) { b.task = Task::Sit; b.taskT = 0; MoveTo(b, Vector3Add(col.caches[0].pos, {0.6f, 0.4f, -0.6f}), speed, dt, 0.5f); return; }   // (nothing worth the trip: wait at home)
        const rt::Zone& Z = eco.map->zones[z];
        b.goal = {Z.plan.x + Z.plan.width * (0.2f + 0.6f * Rand()), 12, Z.plan.y + Z.plan.height * (0.2f + 0.6f * Rand())};
        int seen = 0;
        for (const auto& ag : eco.agents) {
            if (!ag.alive || ag.diver >= 0 || ag.zone != z || ag.pos.y < -FISHER_REACH) continue;
            const rt::Species& s = eco.map->species[ag.sp];
            if (!Catchable(s) || s.size > carry || s.size < minSize) continue;
            float d = Dist2(ag.pos, b.pos) * (0.6f + 0.8f * Rand());   // (a near one, give or take: the flock spreads over the ground)
            if (seen++ == 0 || d < b.taskT) { b.goal = {ag.pos.x, 12, ag.pos.z}; b.taskT = d; }
        }
        b.task = Task::Fly; b.taskT = 0;
        (void)seen;
    } break;
    case Task::Fly:
        if (MoveTo(b, b.goal, speed, dt, 3)) { b.task = Task::Search; b.taskT = 0; }
        break;
    case Task::Search: {
        // circle over the water, reading it for a fish near the surface it can lift
        b.taskT += dt * Econ().workPace;
        float a = b.taskT * 0.5f + b.id;
        MoveTo(b, {b.goal.x + cosf(a) * 8, 12, b.goal.z + sinf(a) * 8}, speed * 0.7f, dt, 0.5f);
        if (fmodf(b.taskT, 0.5f) < dt) {
            int best = -1; float bd = 25;
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                const auto& ag = eco.agents[i];
                if (!ag.alive || ag.diver >= 0 || ag.pos.y < -FISHER_REACH) continue;
                const rt::Species& s = eco.map->species[ag.sp];
                if (!Catchable(s) || s.size > carry || s.size < minSize) continue;
                float d = Dist2(ag.pos, b.pos);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) { b.task = Task::Dive; b.fish = best; b.taskT = 0; }
        }
        if (b.taskT > 12) b.task = Task::Idle;   // (nothing here: look again)
    } break;
    case Task::Dive: {
        int fi = b.fish;
        b.taskT += dt * Econ().workPace;
        bool gone = fi < 0 || fi >= (int)eco.agents.size() || !eco.agents[fi].alive;
        Vector3 fp = gone ? Vector3{b.pos.x, 0, b.pos.z} : eco.agents[fi].pos;
        MoveTo(b, {fp.x, 0.2f, fp.z}, 18, dt, 0.2f);
        if (b.taskT < 1.2f && b.pos.y > 0.6f) break;
        // the strike: hit chance by size, the founder's boost, the water; a hungry shark below may take the bird
        eco.AddNoise({b.pos.x, 0, b.pos.z}, 1.5f);
        for (const auto& ag : eco.agents) {
            if (!ag.alive || ag.diver >= 0 || !EatsBirds(eco, ag.sp)) continue;
            if (Vector3Distance(ag.pos, {b.pos.x, 0, b.pos.z}) < 6 && ag.hunger > 0.5f && Rand() < 0.35f) { BirdDies(b, "taken by a " + eco.map->species[ag.sp].name); return; }
        }
        if (!gone && Dist2(fp, b.pos) < 3 && fp.y > -FISHER_REACH - 0.3f) {
            const rt::Species& s = eco.map->species[eco.agents[fi].sp];
            float boost = Def().key == "taloned" ? 1.3f : Def().key == "beaked" ? 0.85f : Def().key == "shadow" || Def().key == "sigma" ? 0.9f : 1.0f;
            float chance = 0.55f * boost * (1.15f - 0.1f * s.size);
            if (Rand() < chance) {
                b.carrySp = eco.agents[fi].sp; b.carrySize = s.size;
                if (eco.agents[fi].homeZone >= 0 && eco.agents[fi].homeZone < 16) caughtIn[eco.agents[fi].homeZone]++;
                eco.agents[fi].alive = false;
                if (b.carrySp < (int)eco.deathsBySpecies.size()) eco.deathsBySpecies[b.carrySp]++;
                b.task = Task::Idle; b.pos.y = 0.8f;
                if (&b == &fb) fishCaught++;
                return;
            }
        }
        b.task = Task::Search; b.taskT = 3; b.pos.y = 0.8f;   // (missed: up and look again)
    } break;
    }
}

void World::FeederStep(Bird& b, float dt) {
    const RoleDef& R = RoleOf(Role::Feeder);
    if (b.hunger < 0.35f && BirdEatsAtCache(b, dt)) return;
    if (b.carrySp >= 0) {
        if (b.target < 0 || b.target >= (int)col.nests.size()) { b.carrySp = -1; return; }
        Nest& n = col.nests[b.target];
        if (MoveTo(b, Vector3Add(n.pos, {0, 0.4f, 0}), R.speed, dt)) { n.larder += b.carrySize; b.carrySp = -1; b.carrySize = 0; b.target = -1; b.task = Task::Idle; }
        return;
    }
    // the hungriest nest: what its chicks and mate need, less what's laid in it
    int bestN = -1; float need = 0.6f;
    for (int i = 0; i < (int)col.nests.size(); i++) {
        float nd = -col.nests[i].larder;
        for (const auto& o : col.birds) if (o.alive && o.nest == i && (o.stage == BStage::Chick || o.stage == BStage::Mate)) nd += (1.2f - o.hunger) * (o.stage == BStage::Chick ? Econ().feedChick : Econ().feedAdult);
        if (nd > need) { need = nd; bestN = i; }
    }
    int ci = bestN >= 0 ? NearestCache(b.pos, true, false) : -1;
    if (ci < 0) { b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {1.2f, 0.4f, 0.8f}), R.speed, dt, 0.4f); return; }
    Cache& c = col.caches[ci];
    b.task = Task::Fetch;
    if (MoveTo(b, Vector3Add(c.pos, {0, 0.6f, 0}), R.speed, dt)) {
        int k = -1;
        for (int i = 0; i < (int)c.fish.size(); i++) if (c.fish[i].size <= R.carry && (k < 0 || c.fish[i].age > c.fish[k].age)) k = i;
        if (k < 0) { b.task = Task::Sit; return; }
        b.carrySp = c.fish[k].sp; b.carrySize = c.fish[k].size; c.fish.erase(c.fish.begin() + k);
        b.target = bestN; b.task = Task::Feed;
    }
}

void World::BuilderStep(Bird& b, float dt) {
    const Economy& E = Econ();
    const RoleDef& R = RoleOf(Role::Builder);
    if (b.hunger < 0.35f && BirdEatsAtCache(b, dt)) return;
    // what needs raising: a nest under way, a cache under way; else lay one out (nests up to the wanted count, a cache
    // when the piles are filling)
    // defences after nests: a hedge (twigs) or a tower (twigs and shells) the colony has laid out
    bool nestJob = false; for (const auto& n : col.nests) nestJob |= !n.built;
    if (!nestJob) for (auto& st : col.builds) {
        if (st.built) continue;
        int needT = StructureTwigs(st.kind) - (int)st.twigs, needS = StructureShells(st.kind) - st.shells;
        if (b.carryTwigs > 0 || b.carryShells > 0) {
            b.task = Task::Build;
            if (MoveTo(b, Vector3Add(st.pos, {0, 0.6f, 0}), R.speed, dt)) {
                st.twigs += b.carryTwigs; st.shells += b.carryShells; b.carryTwigs = 0; b.carryShells = 0; b.task = Task::Idle;
                if (st.twigs >= StructureTwigs(st.kind) && st.shells >= StructureShells(st.kind)) { st.built = true; Say(st.kind == 0 ? "A hedge of thorn now rings the home nest: Skirmishers can't get through." : "A tower stands over the colony: a Watcher on it sees farther and nets farther."); }
            }
            return;
        }
        if (needS > 0 && col.shells > 0) { b.task = Task::Fetch; if (MoveTo(b, Vector3Add(col.caches[0].pos, {0, 0.5f, 0}), R.speed, dt)) { int s = std::min(col.shells, std::min(needS, R.carry)); col.shells -= s; b.carryShells = s; } return; }
        if (needT > 0 && col.twigs > 0) { b.task = Task::Fetch; if (MoveTo(b, Vector3Add(col.caches[0].pos, {0, 0.5f, 0}), R.speed, dt)) { int s = std::min(col.twigs, std::min(needT, R.carry)); col.twigs -= s; b.carryTwigs = s; } return; }
        int src = -1; float bd = 1e9f;
        for (int i = 0; i < (int)col.twigSrc.size(); i++) { const auto& s = col.twigSrc[i]; if (s.twigs < 1 || s.shells != (needT <= 0)) continue; float d = Vector3Distance(b.pos, s.pos); if (d < bd) { bd = d; src = i; } }
        if (src < 0) break;
        b.task = Task::Gather;
        if (MoveTo(b, Vector3Add(col.twigSrc[src].pos, {0, 0.3f, 0}), R.speed, dt)) { int k = std::min((int)col.twigSrc[src].twigs, R.carry); col.twigSrc[src].twigs -= k; if (col.twigSrc[src].shells) b.carryShells = k; else b.carryTwigs = k; }
        return;
    }
    int job = -1; bool jobCache = false;
    for (int i = 0; i < (int)col.nests.size() && job < 0; i++) if (!col.nests[i].built) job = i;
    if (job < 0) for (int i = 0; i < (int)col.caches.size() && job < 0; i++) if (!col.caches[i].built) { job = i; jobCache = true; }
    if (job < 0 && !col.leaderless) {
        int nests = (int)col.nests.size();
        int fish = 0, cap = 0; for (const auto& c : col.caches) if (c.built) { fish += (int)c.fish.size(); cap += E.cacheCap; }
        int fishers = Count(BStage::Adult, Role::Fisher);
        if ((fish > cap * 0.7f || (int)col.caches.size() < 1 + fishers / 4) && (int)col.caches.size() < 2 + nests) {
            // a new cache on the shore, on the side with the fewest caches (short trips home from every ground)
            float side = 0; { float best = -1; for (int k = 0; k < 8; k++) { float a = k * PI / 4, near = 1e9f; for (const auto& c : col.caches) near = std::min(near, Vector2Distance({c.pos.x, c.pos.z}, {cosf(a) * 80, sinf(a) * 80})); if (near > best) { best = near; side = a; } } }
            for (int tries = 0; tries < 80; tries++) {
                float a = side + (Rand() - 0.5f) * 0.6f, r = 55 + Rand() * 35;
                Vector3 p = island.Ground(cosf(a) * r, sinf(a) * r);
                float h = island.Height(p.x, p.z);
                if (h < 0.6f || h > 3 || island.Normal(p.x, p.z).y < 0.85f) continue;
                Cache c; c.pos = {p.x, p.y + 0.1f, p.z}; c.built = false; col.caches.push_back(c);
                job = (int)col.caches.size() - 1; jobCache = true; break;
            }
        } else if (nests < col.nestsWanted) {
            int s = NearestSite(col.nests[0].pos, 1e9f);
            if (s >= 0) { Nest n; n.site = s; n.pos = col.sites[s].pos; n.bowlNeed = E.courtFish + E.courtStep * (int)col.nests.size(); col.sites[s].nest = (int)col.nests.size(); col.nests.push_back(n); job = (int)col.nests.size() - 1; }
        }
    }
    float need = job < 0 ? 0 : jobCache ? E.cacheTwigs - col.caches[job].twigs : (Def().key == "albatross" ? 2 : 1) * E.nestTwigs - col.nests[job].twigs;
    Vector3 jobPos = job < 0 ? Vector3{} : jobCache ? col.caches[job].pos : col.nests[job].pos;
    if (b.carryTwigs > 0) {
        Vector3 to = job >= 0 ? jobPos : col.caches[0].pos;
        b.task = Task::Build;
        if (MoveTo(b, Vector3Add(to, {0, 0.5f, 0}), R.speed, dt)) {
            if (job >= 0) {
                if (jobCache) { col.caches[job].twigs += b.carryTwigs; if (col.caches[job].twigs >= E.cacheTwigs) { col.caches[job].built = true; Say("A new cache is built."); } }
                else { col.nests[job].twigs += b.carryTwigs; if (col.nests[job].twigs >= (Def().key == "albatross" ? 2 : 1) * E.nestTwigs) { col.nests[job].built = true; Say(TextFormat("A new nest is built: its courtship bowl wants %d fish.", col.nests[job].bowlNeed)); } }
            } else col.twigs += b.carryTwigs;
            b.carryTwigs = 0; b.task = Task::Idle;
        }
        return;
    }
    if (b.carryShells > 0) {
        int ln = -1; for (int i = 0; i < (int)col.nests.size(); i++) if (col.nests[i].built && col.nests[i].shells < E.liningShells) { ln = i; break; }
        Vector3 to = ln >= 0 ? col.nests[ln].pos : col.caches[0].pos;
        if (MoveTo(b, Vector3Add(to, {0, 0.5f, 0}), R.speed, dt)) { if (ln >= 0) col.nests[ln].shells += b.carryShells; else col.shells += b.carryShells; b.carryShells = 0; }
        return;
    }
    if (job >= 0 && need > 0) {
        // twigs from the stock pile if there are any, else from the palms and the driftwood
        if (col.twigs > 0) {
            b.task = Task::Fetch;
            if (MoveTo(b, Vector3Add(col.caches[0].pos, {0, 0.5f, 0}), R.speed, dt)) { int t = std::min(col.twigs, R.carry); col.twigs -= t; b.carryTwigs = t; }
            return;
        }
        int src = NearestTwigs(jobPos, 1e9f);
        if (src < 0) { b.task = Task::Sit; return; }
        b.task = Task::Gather;
        if (MoveTo(b, Vector3Add(col.twigSrc[src].pos, {0, 0.3f, 0}), R.speed, dt)) { int t = std::min((int)col.twigSrc[src].twigs, R.carry); col.twigSrc[src].twigs -= t; b.carryTwigs = t; }
        return;
    }
    // nothing to raise: shells for the nests' linings, and a stock of twigs for the next nest
    bool lining = false; for (const auto& n : col.nests) if (n.built && n.shells < E.liningShells) lining = true;
    int want = -1; float bd = 1e9f;
    for (int i = 0; i < (int)col.twigSrc.size(); i++) {
        const auto& s = col.twigSrc[i];
        if (s.twigs < 1 || (s.shells ? !lining : col.twigs >= 2 * E.nestTwigs)) continue;
        float d = Vector3Distance(b.pos, s.pos); if (d < bd) { bd = d; want = i; }
    }
    if (want < 0) { b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {-1.0f, 0.4f, 1.0f}), R.speed, dt, 0.4f); return; }
    b.task = Task::Gather;
    if (MoveTo(b, Vector3Add(col.twigSrc[want].pos, {0, 0.3f, 0}), R.speed, dt)) {
        int t = std::min((int)col.twigSrc[want].twigs, R.carry); col.twigSrc[want].twigs -= t;
        if (col.twigSrc[want].shells) b.carryShells = t; else b.carryTwigs = t;
    }
}

void World::MateStep(Bird& b, float dt) {
    const Economy& E = Econ();
    if (b.nest < 0 || b.nest >= (int)col.nests.size()) return;
    Nest& n = col.nests[b.nest];
    if (b.task == Task::Fly) { if (MoveTo(b, Vector3Add(n.pos, {0, 0.3f, 0}), 13, dt, 0.5f)) { b.task = Task::Sit; Say("A mate has come to a nest."); } return; }
    // no larder and no feeders: it goes for food itself and for its chicks, leaving any eggs to chill
    bool feeders = Count(BStage::Adult, Role::Feeder) > 0;
    float chicksNeed = 0;
    for (const auto& o : col.birds) if (o.alive && o.nest == b.nest && o.stage == BStage::Chick) chicksNeed += (1 - o.hunger) * E.feedChick;
    bool needFood = n.larder < 0.5f && (b.hunger < 0.3f || chicksNeed > 0.6f);
    if (b.task == Task::Fetch || (needFood && !feeders && NearestCache(b.pos, true, false) >= 0)) {
        if (b.carrySp >= 0) { if (MoveTo(b, Vector3Add(n.pos, {0, 0.3f, 0}), 13, dt, 0.5f)) { n.larder += b.carrySize; b.carrySp = -1; b.carrySize = 0; b.task = Task::Sit; } return; }
        int ci = NearestCache(b.pos, true, false);
        if (ci < 0) { b.task = Task::Sit; return; }
        b.task = Task::Fetch;
        if (MoveTo(b, Vector3Add(col.caches[ci].pos, {0, 0.6f, 0}), 13, dt)) { auto& f = col.caches[ci].fish; b.carrySp = f.back().sp; b.carrySize = f.back().size; f.pop_back(); }
        return;
    }
    b.task = Task::Sit; b.pos = Vector3Add(n.pos, {0, 0.25f, 0}); b.vel = {0, 0, 0};
    // a clutch every two days while fed, three clutches a mate
    if (b.hunger > 0.3f && b.clutches < E.clutches) {
        b.clutchT += dt;
        if (b.clutchT >= E.clutchDays * DAY) {
            int inNest = 0; for (const auto& o : col.birds) if (o.alive && o.nest == b.nest && (o.stage == BStage::Egg || o.stage == BStage::Chick)) inNest++;
            int eggs = E.clutchMin + (int)(Rand() * (E.clutchMax - E.clutchMin + 1));
            if (Def().key == "lyrebird") eggs++;
            if (Def().key == "cuckoo") eggs--;
            eggs = std::min(eggs, E.nestEggs - inNest);
            if (eggs > 0) {
                for (int k = 0; k < eggs; k++) { Bird e; e.id = col.nextId++; e.stage = BStage::Egg; e.nest = b.nest; e.pos = n.pos; e.hunger = 1; born.push_back(e); }   // (appended after the loop: b is a reference into col.birds)
                b.clutches++; b.clutchT = 0;
                Say(TextFormat("A clutch of %d eggs.", eggs));
            }
        }
    }
}

bool World::Retrain(Role to, Role from) {
    if (from == Role::None) { int most = 0; for (int o = 1; o < (int)Role::COUNT; o++) if ((Role)o != to && Count(BStage::Adult, (Role)o) > most) { most = Count(BStage::Adult, (Role)o); from = (Role)o; } }
    for (auto& b : col.birds) if (b.alive && b.stage == BStage::Adult && b.role == from && b.retrainT <= 0 && b.carrySp < 0 && b.carryTwigs == 0 && b.flock < 0) {
        b.retrainTo = to; b.retrainT = Econ().retrainDays * DAY; b.task = Task::Idle;
        Say(std::string("A ") + RoleName(from) + " retrains as a " + RoleName(to) + " (a day).");
        return true;
    }
    return false;
}
void World::Fledge(Bird& b) {
    Role r = b.retrainTo;
    if (r == Role::None) {
        // the plan: the role furthest below its share
        float total = 1; for (const auto& o : col.birds) if (o.alive && o.stage == BStage::Adult) total += 1;
        float worst = -1e9f;
        for (int k = 1; k < (int)Role::COUNT; k++) {
            float gap = col.plan[k] * total - Count(BStage::Adult, (Role)k);
            if (col.plan[k] > 0 && gap > worst) { worst = gap; r = (Role)k; }
        }
        if (r == Role::None) r = Role::Fisher;
        if (col.leaderless) r = Role::Fisher;
    }
    b.stage = BStage::Adult; b.role = r; b.retrainTo = Role::None; b.task = Task::Idle; b.age = 0; b.hp = RoleOf(r).hp; b.fight = 25;
    b.pos.y += 0.5f;
    Say(std::string("A chick fledges: a ") + RoleName(r) + ".");
}
void World::BirdDies(Bird& b, const std::string& cause) {
    if (!b.alive) return;
    b.alive = false; b.cause = cause;
    for (auto& d : col.deaths) if (d.first == cause) { d.second++; goto counted; }
    col.deaths.push_back({cause, 1});
counted:
    col.deathsToday++;
    if (b.stage == BStage::Mate && b.nest >= 0 && b.nest < (int)col.nests.size()) { Nest& n = col.nests[b.nest]; n.mate = -1; n.bowl = 0; n.bowlNeed += Econ().courtStep; }
    if (b.stage != BStage::Egg) Say(std::string(b.stage == BStage::Chick ? "A chick" : b.stage == BStage::Mate ? "A mate" : RoleName(b.role)) + " has died: " + cause + ".");
}

void World::StepBird(Bird& b, float dt) {
    const Economy& E = Econ();
    // hunger: a day to empty (chicks twice as fast); chicks and mates eat what's laid in the nest
    if (b.stage != BStage::Egg) {
        b.hunger -= dt / DAY * (b.stage == BStage::Chick ? E.chickDrain : 1.0f);
        if ((b.stage == BStage::Chick || b.stage == BStage::Mate) && b.nest >= 0 && b.hunger < 0.85f) {
            Nest& n = col.nests[b.nest];
            float cap = b.stage == BStage::Chick ? E.feedChick : E.feedAdult;
            float take = std::min(n.larder, (1 - b.hunger) * cap);
            n.larder -= take; b.hunger += take / cap;
        }
        if (b.hunger <= 0) { b.hunger = 0; b.starveT += dt; if (b.starveT > E.starveDays * DAY) { BirdDies(b, "starved"); return; } }
        else b.starveT = 0;
    }
    switch (b.stage) {
    case BStage::Egg: {
        Nest& n = col.nests[b.nest];
        bool warm = false;
        if (n.mate >= 0) for (const auto& o : col.birds) if (o.id == n.mate && o.alive && o.task == Task::Sit) warm = true;
        if (n.founders && me.st == FState::Perched && Vector3Distance(me.pos, n.pos) < 2) warm = true;
        if (warm) { b.age += dt / DAY * (n.shells >= E.liningShells ? 1 + E.liningHatch : 1.0f); b.chillT = std::max(0.0f, b.chillT - dt * 0.5f); }
        else { b.chillT += dt; if (b.chillT > E.chillDays * DAY) { BirdDies(b, "chilled"); return; } }
        if (b.age >= E.hatchDays) { b.stage = BStage::Chick; b.age = 0; b.hunger = 0.8f; Say("An egg hatches."); }
    } break;
    case BStage::Chick: {
        Nest& n = col.nests[b.nest];
        b.pos = Vector3Add(n.pos, {0.15f * cosf(b.id * 1.7f), 0.2f, 0.15f * sinf(b.id * 1.7f)});
        b.age += dt / DAY * (b.hunger > 0.9f ? 1 + E.overfeed : 1.0f);
        if (b.age >= E.chickDays + (Def().key == "swift" ? 1 : 0)) Fledge(b);
    } break;
    case BStage::Mate: MateStep(b, dt); break;
    case BStage::Adult: {
        // night: the colony roosts (fervour, a later stage, will keep it working); a bird with a fish brings it home first
        float ph = DayPhase();
        bool night = ph < 0.19f || ph > 0.87f;
        if (night && b.carrySp < 0 && b.carryTwigs == 0 && b.carryShells == 0 && !(b.role == Role::Scout && b.hasOrder) && b.flock < 0 && b.role != Role::Watcher) {   // (a scout out, a flock and a Watcher stay up)   // (a scout out on an order flies on)
            if (b.hunger < 0.3f && BirdEatsAtCache(b, dt)) break;
            const Site& s = col.sites[(b.id * 7) % col.sites.size()];
            b.task = MoveTo(b, Vector3Add(s.pos, {0.3f * cosf(b.id * 1.1f), 0.2f, 0.3f * sinf(b.id * 1.1f)}), 12, dt, 0.4f) ? Task::Sit : Task::Fly;
            if (b.task == Task::Sit) b.vel = {0, 0, 0};
            break;
        }
        if (b.retrainT > 0) { b.retrainT -= dt; b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {0, 0.4f, -1.2f}), 10, dt, 0.4f); if (b.retrainT <= 0) { b.role = b.retrainTo; b.retrainTo = Role::None; b.task = Task::Idle; b.hp = RoleOf(b.role).hp; b.fight = 25; } break; }
        if (IsWarrior(b.role)) {
            if (b.flock >= 0) break;   // (StepWar flies it)
            if (b.hunger < 0.4f && BirdEatsAtCache(b, dt)) break;
            b.fight = std::min(25.0f, b.fight + 0.5f * dt);
            if (b.hp < RoleOf(b.role).hp) b.hp = std::min(RoleOf(b.role).hp, b.hp + RoleOf(b.role).hp * dt / DAY);   // (it heals over a day at home)
            // a Watcher perches by the nests (on a tower if there is one); the rest roost round home
            Vector3 post;
            if (b.role == Role::Watcher) {
                int k = 0, mine = 0; for (const auto& o : col.birds) { if (&o == &b) mine = k; if (o.alive && o.role == Role::Watcher && o.stage == BStage::Adult) k++; }
                const Structure* tw = nullptr; for (const auto& s : col.builds) if (s.kind == 1 && s.built) tw = &s;
                if (tw && mine == 0) post = Vector3Add(tw->pos, {0, 16, 0});
                else { const Nest& n = col.nests[mine % col.nests.size()]; post = Vector3Add(n.pos, {1.2f, 0.3f, 0.8f}); }
                b.post = post;
            } else { const Site& s = col.sites[(b.id * 5) % col.sites.size()]; post = Vector3Add(s.pos, {0.4f * cosf(b.id * 1.3f), 0.25f, 0.4f * sinf(b.id * 1.3f)}); }
            b.task = MoveTo(b, post, RoleOf(b.role).speed, dt, 0.4f) ? Task::Sit : Task::Fly;
            if (b.task == Task::Sit) b.vel = {0, 0, 0};
            break;
        }
        if (b.role == Role::Fisher || b.role == Role::None) FisherStep(b, dt);
        else if (b.role == Role::Feeder) FeederStep(b, dt);
        else if (b.role == Role::Builder) BuilderStep(b, dt);
        else if (b.role == Role::Scout) ScoutStep(b, dt);
    } break;
    }
}

void World::DayTick() {
    DayStats d; d.day = ++dayNum;
    d.birds = Alive(); d.eggs = Count(BStage::Egg); d.chicks = Count(BStage::Chick); d.mates = Count(BStage::Mate);
    for (const auto& n : col.nests) d.nests += n.built;
    d.caught = col.caughtToday; d.feedCaught = col.feedToday; d.mouths = MouthsPerDay(); d.cacheFeed = CacheFeed(); d.deaths = col.deathsToday;
    d.lagoon = lagoonZone >= 0 ? StockOf(lagoonZone) : 0;
    col.days.push_back(d);
    col.feedYesterday = col.feedToday; col.feedToday = 0; col.caughtToday = 0; col.deathsToday = 0;
}

void World::StepColony(float dt) {
    const Economy& E = Econ();
    if (cur == 0) RegrowFish(dt);   // (once a step, not once a colony)
    born.clear();
    for (size_t i = 0; i < col.birds.size(); i++) if (col.birds[i].alive) StepBird(col.birds[i], dt);
    col.birds.insert(col.birds.end(), born.begin(), born.end());
    // the dead are cleared now and then (ids, not indices, tie birds together)
    if (col.birds.size() > 64) { size_t dead = 0; for (const auto& b : col.birds) dead += !b.alive; if (dead > col.birds.size() / 3) col.birds.erase(std::remove_if(col.birds.begin(), col.birds.end(), [](const Bird& b) { return !b.alive; }), col.birds.end()); }
    // caches spoil; twigs regrow; a filled courtship bowl brings a mate within a day (from a finite wild flock)
    for (auto& c : col.caches) {
        for (auto& f : c.fish) f.age += dt;
        size_t before = c.fish.size();
        c.fish.erase(std::remove_if(c.fish.begin(), c.fish.end(), [&](const CachedFish& f) { return f.age > E.spoilDays * DAY; }), c.fish.end());
        if (c.fish.size() < before && &c == &col.caches[0]) Say("Fish in the cache have spoiled.");
    }
    for (auto& s : col.twigSrc) s.twigs = std::min(s.cap, s.twigs + s.cap / (E.twigRegrowDays * DAY) * dt);
    for (int i = 0; i < (int)col.nests.size(); i++) {
        Nest& n = col.nests[i];
        if (!n.built || n.mate >= 0) continue;
        if (n.mateT < 0 && n.bowl >= n.bowlNeed && col.wildMates > 0) {
            float mul = E.tropicalMate * (Def().key == "lyrebird" ? 0.5f : 1.0f);
            n.mateT = (E.mateMin + (E.mateMax - E.mateMin) * Rand()) * DAY * mul;
            Say("The courtship bowl is full: a mate will come within the day.");
        }
        if (n.mateT >= 0) {
            n.mateT -= dt;
            if (n.mateT <= 0 && col.wildMates > 0) {
                Bird m; m.id = col.nextId++; m.stage = BStage::Mate; m.nest = i; m.hunger = 1; m.task = Task::Fly;
                float a = Rand() * 2 * PI; m.pos = {n.pos.x + cosf(a) * 120, 20, n.pos.z + sinf(a) * 120};
                m.clutchT = (E.clutchDays - 0.3f) * DAY;   // (the first clutch comes soon after it settles)
                col.birds.push_back(m);
                n.mate = m.id; n.mateT = -1; n.bowl = 0; col.wildMates--;
            }
        }
    }
    // the colony without its leader: after a minute the old orders run down (no new nests, fledglings fish)
    col.downT = me.st == FState::Dead || me.chick ? col.downT + dt : 0;
    col.leaderless = col.downT > 60;
    dayAcc += dt;
    if (dayAcc >= DAY) { dayAcc -= DAY; DayTick(); }
}

// ---------------------------------------------------------------- the Founder's hands: E and F
namespace {
enum class Act { None, Bowl, Larder, Store, PickFish, AddTwigs, StockTwigs, PickTwigs, StartNest };
}
static Act FounderAct(const World& w, int* idx) {
    const Founder& f = w.me;
    if (f.st == FState::Dead || f.st == FState::Strike || f.st == FState::Fainted || f.st == FState::Struggle) return Act::None;
    if (f.st == FState::Fly && Vector3Length(f.vel) > 9) return Act::None;   // (too fast to land on anything)
    const Economy& E = Econ();
    int n = w.NearestNest(f.pos, 3.5f);
    int c = -1; { float bd = 3.5f; for (int i = 0; i < (int)w.col.caches.size(); i++) { float d = Vector3Distance(f.pos, w.col.caches[i].pos); if (d < bd) { bd = d; c = i; } } }
    if (f.carrySp >= 0) {
        if (n >= 0 && w.col.nests[n].built && w.col.nests[n].mate < 0 && w.col.nests[n].mateT < 0 && w.col.wildMates > 0) { *idx = n; return Act::Bowl; }
        if (n >= 0 && w.col.nests[n].built) { *idx = n; return Act::Larder; }
        if (c >= 0 && w.col.caches[c].built && (int)w.col.caches[c].fish.size() < E.cacheCap) { *idx = c; return Act::Store; }
        return Act::None;
    }
    if (f.carryTwigs > 0) {
        if (n >= 0 && !w.col.nests[n].built) { *idx = n; return Act::AddTwigs; }
        if (c >= 0) { *idx = c; return w.col.caches[c].built ? Act::StockTwigs : Act::AddTwigs; }
        return Act::None;
    }
    if (c >= 0 && !w.col.caches[c].fish.empty()) { *idx = c; return Act::PickFish; }
    int t = w.NearestTwigs(f.pos, 3.5f);
    if (t >= 0) { *idx = t; return Act::PickTwigs; }
    if (c >= 0 && w.col.twigs > 0) { *idx = -1; return Act::PickTwigs; }
    int s = w.NearestSite(f.pos, 3.5f);
    if (s >= 0) { *idx = s; return Act::StartNest; }
    return Act::None;
}
std::string World::InteractHint() const {
    int i = -1;
    switch (FounderAct(*this, &i)) {
    case Act::Bowl: { const Nest& n = col.nests[i]; return me.carrySize >= Econ().courtMinSize ? TextFormat("E: into the courtship bowl (%d of %d)", n.bowl + 1, n.bowlNeed) : TextFormat("E: lay it in the nest (courtship wants size %d+)", Econ().courtMinSize); }
    case Act::Larder: return "E: lay it in the nest for the chicks and the mate";
    case Act::Store: return "E: into the cache";
    case Act::PickFish: return "E: take a fish from the cache (for a courtship bowl)";
    case Act::AddTwigs: return "E: add the twigs to the build";
    case Act::StockTwigs: return "E: add the twigs to the colony's stock";
    case Act::PickTwigs: return "E: pick up twigs";
    case Act::StartNest: return TextFormat("E: lay out a nest here (%d twigs)", (Def().key == "albatross" ? 2 : 1) * Econ().nestTwigs);
    default: return "";
    }
}
void World::Interact() {
    const Economy& E = Econ();
    int i = -1;
    Act a = FounderAct(*this, &i);
    Founder& f = me;
    switch (a) {
    case Act::Bowl: {
        Nest& n = col.nests[i];
        if (f.carrySize >= E.courtMinSize) { n.bowl++; Say(TextFormat("Into the courtship bowl: %d of %d.", n.bowl, n.bowlNeed)); }
        else Say("Too small for courtship: laid in the nest instead.");
        n.larder += f.carrySize; f.carrySp = -1; f.carrySize = 0;
    } break;
    case Act::Larder: col.nests[i].larder += f.carrySize; Say("Laid in the nest."); f.carrySp = -1; f.carrySize = 0; break;
    case Act::Store: col.caches[i].fish.push_back({f.carrySp, f.carrySize, 0}); Say("Into the cache: " + eco.map->species[f.carrySp].name + "."); f.carrySp = -1; f.carrySize = 0; break;
    case Act::PickFish: {
        auto& fish = col.caches[i].fish; int k = -1;
        for (int j = 0; j < (int)fish.size(); j++) if (fish[j].size <= f.Carry(Def()) && (k < 0 || fish[j].size > fish[k].size)) k = j;
        if (k < 0) { Say("Everything here is too heavy to lift."); break; }
        f.carrySp = fish[k].sp; f.carrySize = fish[k].size; fish.erase(fish.begin() + k);
        Say("Took a " + eco.map->species[f.carrySp].name + " from the cache.");
    } break;
    case Act::AddTwigs:
        if (i >= 0 && i < (int)col.nests.size() && !col.nests[i].built && Vector3Distance(f.pos, col.nests[i].pos) < 3.5f) {
            Nest& n = col.nests[i]; n.twigs += f.carryTwigs; f.carryTwigs = 0;
            float need = (float)((Def().key == "albatross" ? 2 : 1) * E.nestTwigs);
            if (n.twigs >= need) { n.built = true; n.bowlNeed = E.courtFish + E.courtStep * (i); Say(TextFormat("The nest is built. Its courtship bowl wants %d fish of size %d+.", n.bowlNeed, E.courtMinSize)); }
            else Say(TextFormat("The nest: %.0f of %.0f twigs.", n.twigs, need));
        } else { Cache& c = col.caches[i]; c.twigs += f.carryTwigs; f.carryTwigs = 0; if (c.twigs >= E.cacheTwigs) { c.built = true; Say("The cache is built."); } }
        break;
    case Act::StockTwigs: col.twigs += f.carryTwigs; f.carryTwigs = 0; Say(TextFormat("Twigs in stock: %d.", col.twigs)); break;
    case Act::PickTwigs:
        if (i >= 0) { int t = std::min(2, (int)col.twigSrc[i].twigs); col.twigSrc[i].twigs -= t; f.carryTwigs = t; }
        else { int t = std::min(2, col.twigs); col.twigs -= t; f.carryTwigs = t; }
        Say(TextFormat("Carrying %d twigs.", f.carryTwigs));
        break;
    case Act::StartNest: {
        Nest n; n.site = i; n.pos = col.sites[i].pos; n.bowlNeed = E.courtFish + E.courtStep * (int)col.nests.size();
        col.sites[i].nest = (int)col.nests.size(); col.nests.push_back(n);
        Say(TextFormat("A nest is laid out: bring %d twigs (palms and driftwood have them).", (Def().key == "albatross" ? 2 : 1) * E.nestTwigs));
    } break;
    default: break;
    }
}
bool World::Eat() {
    Founder& f = me;
    if (f.st != FState::Perched && f.st != FState::Floating) return false;
    int sz = 0;
    if (f.carrySp >= 0) { sz = f.carrySize; f.carrySp = -1; f.carrySize = 0; }
    else {
        int c = -1; float bd = 3.5f;
        for (int i = 0; i < (int)col.caches.size(); i++) { float d = Vector3Distance(f.pos, col.caches[i].pos); if (d < bd && !col.caches[i].fish.empty()) { bd = d; c = i; } }
        if (c < 0) { int n = NearestNest(f.pos, 3); if (n >= 0 && col.nests[n].larder >= 1) { col.nests[n].larder -= 1; sz = 1; } }
        else { auto& fish = col.caches[c].fish; size_t k = 0; for (size_t i = 1; i < fish.size(); i++) if (fish[i].age > fish[k].age) k = i; sz = fish[k].size; fish.erase(fish.begin() + k); }
    }
    if (sz <= 0) return false;
    f.hunger = std::min(1.0f, f.hunger + sz / Econ().feedFounder); fishEaten++;
    if (f.chick) f.chickFish++;
    Say(TextFormat("Ate a size-%d fish.%s", sz, f.chick ? TextFormat(" (%d of 3)", f.chickFish) : ""));
    return true;
}

// ---------------------------------------------------------------- the bot Founder (--flight-sim): the colony's own logic flies it
int World::BotFounderStep(float dt) {
    Founder& f = me;
    const Economy& E = Econ();
    if (f.st == FState::Dead) { f.respawnT -= dt; if (f.respawnT <= 0) Respawn(); return 0; }
    f.hunger -= dt / DAY * (f.chick ? 2.0f : 1.0f);
    if (f.hunger <= 0) { f.hunger = 0; static float st = 0; st += dt; if (st > E.starveDays * DAY) { st = 0; Kill("starved"); } return 0; }
    if (f.chick && f.chickFish >= 3) f.chick = false;
    fb.pos = f.pos; fb.hunger = 1;   // (its own hunger is the Founder's)
    // night: home to the nest (it eats from the cache if it can)
    float ph = DayPhase();
    if ((ph < 0.19f || ph > 0.87f) && fb.carrySp < 0) {
        int ci = NearestCache(f.pos, true, false);
        if (f.hunger < 0.5f && ci >= 0) { if (MoveTo(fb, Vector3Add(col.caches[ci].pos, {0, 0.5f, 0}), 11, dt)) { auto& fish = col.caches[ci].fish; f.hunger = std::min(1.0f, f.hunger + fish.back().size / E.feedFounder); fish.pop_back(); fishEaten++; } }
        else MoveTo(fb, Vector3Add(col.nests[0].pos, {0, 0.3f, 0}), 11, dt, 0.5f);
        f.pos = fb.pos; f.yaw = fb.yaw; return 0;
    }
    // 1. hungry: eat from a cache, or the catch
    if (f.hunger < 0.4f) {
        if (fb.carrySp >= 0) { f.hunger = std::min(1.0f, f.hunger + fb.carrySize / E.feedFounder); if (f.chick) f.chickFish++; fb.carrySp = -1; fishEaten++; fb.task = Task::Idle; }
        else {
            int ci = NearestCache(f.pos, true, false);
            if (ci >= 0) {
                if (MoveTo(fb, Vector3Add(col.caches[ci].pos, {0, 0.5f, 0}), 11, dt)) {
                    auto& fish = col.caches[ci].fish; f.hunger = std::min(1.0f, f.hunger + fish.back().size / E.feedFounder); fish.pop_back(); fishEaten++; if (f.chick) f.chickFish++;
                }
                f.pos = fb.pos; f.yaw = fb.yaw; return 1;
            }
            fb.target = -1; FisherStep(fb, dt); f.pos = fb.pos; f.yaw = fb.yaw; return 1;
        }
    }
    // 2. a nest that wants courtship fish: take one from a cache, or fish one up
    // (a careful player courts the first mate at once, later ones only while the colony is fed with room to spare)
    int court = -1;
    float mouths = MouthsPerDay(), feed = FeedPerDayEstimate();
    bool affordable = Count(BStage::Mate) == 0 || (DaysOfFood() > 1.2f && feed >= mouths + Econ().feedAdult + 2 * Econ().feedChick);
    if (affordable || (fb.target >= 1000 && fb.carrySp >= 0))
        for (int i = 0; i < (int)col.nests.size(); i++) { const Nest& n = col.nests[i]; if (n.built && n.mate < 0 && n.mateT < 0 && col.wildMates > 0) { court = i; break; } }
    if (court >= 0) {
        if (fb.carrySp < 0) {
            int ci = -1; float bd = 1e9f;
            for (int i = 0; i < (int)col.caches.size(); i++) for (const auto& x : col.caches[i].fish) if (x.size >= E.courtMinSize && x.size <= f.Carry(Def())) { float d = Vector3Distance(f.pos, col.caches[i].pos); if (d < bd) { bd = d; ci = i; } }
            if (ci >= 0) {
                if (MoveTo(fb, Vector3Add(col.caches[ci].pos, {0, 0.5f, 0}), 11, dt)) {
                    auto& fish = col.caches[ci].fish;
                    for (size_t k = 0; k < fish.size(); k++) if (fish[k].size >= E.courtMinSize && fish[k].size <= f.Carry(Def())) { fb.carrySp = fish[k].sp; fb.carrySize = fish[k].size; fish.erase(fish.begin() + k); break; }
                    fb.target = 1000 + court;
                }
                f.pos = fb.pos; f.yaw = fb.yaw; return 2;
            }
        }
        fb.target = 1000 + court;
        FisherStep(fb, dt);
        f.pos = fb.pos; f.yaw = fb.yaw; f.carrySp = fb.carrySp; f.carrySize = fb.carrySize;
        return 2;
    }
    // 3. with no builders yet: build the next nest alone
    if (Count(BStage::Adult, Role::Builder) == 0 && (int)col.nests.size() < col.nestsWanted) {
        int s = NearestSite(col.nests[0].pos, 1e9f);
        if (s >= 0) { Nest n; n.site = s; n.pos = col.sites[s].pos; n.bowlNeed = E.courtFish + E.courtStep * (int)col.nests.size(); col.sites[s].nest = (int)col.nests.size(); col.nests.push_back(n); }
    }
    int job = -1; for (int i = 0; i < (int)col.nests.size(); i++) if (!col.nests[i].built) { job = i; break; }
    if (job >= 0 && Count(BStage::Adult, Role::Builder) == 0) {
        Nest& n = col.nests[job];
        if (fb.carryTwigs > 0) {
            if (MoveTo(fb, Vector3Add(n.pos, {0, 0.4f, 0}), 11, dt)) { n.twigs += fb.carryTwigs; fb.carryTwigs = 0; if (n.twigs >= (Def().key == "albatross" ? 2 : 1) * E.nestTwigs) { n.built = true; Say("The Founder has built a nest."); } }
        } else {
            int t = NearestTwigs(n.pos, 1e9f);
            if (t >= 0 && MoveTo(fb, Vector3Add(col.twigSrc[t].pos, {0, 0.3f, 0}), 11, dt)) { int k = std::min(2, (int)col.twigSrc[t].twigs); col.twigSrc[t].twigs -= k; fb.carryTwigs = k; }
        }
        f.pos = fb.pos; f.yaw = fb.yaw; return 3;
    }
    // 4. otherwise it fishes for the caches like any fisher
    if (fb.target >= 1000) fb.target = -1;
    FisherStep(fb, dt);
    f.pos = fb.pos; f.yaw = fb.yaw; f.carrySp = fb.carrySp; f.carrySize = fb.carrySize;
    return 4;
}

// ---------------------------------------------------------------- --flight-colony-test
int RunFlightColonyTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 2: the colony loop\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    const Economy& E = Econ();
    check(E.courtFish == 3 && E.nestEggs == 4 && E.hatchDays == 1 && E.chickDays == 2, "the economy loads (courtship 3 fish, 4 eggs a nest, hatch in a day, fledge in two)");
    const float D = World::DAY;
    auto run = [&](World& w, float secs, float dt = 0.1f) { FounderInput in; dt = std::min(dt, 0.1f); for (float t = 0; t < secs; t += dt) { w.Step(dt, in); } };   // (Step takes at most 0.1 s)
    // ---- the Founder's courtship: three fish of size 2+ in the bowl call a mate within a day
    {
        World w; w.Init("taloned", 5);
        check(w.col.nests.size() == 1 && w.col.nests[0].built && w.col.caches.size() == 1 && (int)w.col.sites.size() == (int)w.island.sites.size(), TextFormat("the colony starts with the Founder's nest, a cache and %d nest sites", (int)w.col.sites.size()));
        w.me.st = FState::Perched; w.me.pos = w.island.nest;
        int mullet = w.eco.map->SpeciesIndex("Mullet"), sardine = w.eco.map->SpeciesIndex("Sardine");
        w.me.carrySp = sardine; w.me.carrySize = 1; bool small = w.InteractHint().find("courtship wants") != std::string::npos; w.Interact();
        for (int k = 0; k < 3; k++) { w.me.carrySp = mullet; w.me.carrySize = 2; w.Interact(); }
        run(w, 0.1f);
        check(small && w.col.nests[0].bowl == 3 && w.col.nests[0].mateT > 0, "a sardine is too small for courtship; three mullet fill the bowl and a mate is on its way");
        w.me.st = FState::Fly; w.me.pos = {0, 60, 0};   // (the Founder away)
        float t0 = w.time;
        while (w.col.nests[0].mate < 0 && w.time - t0 < D * 1.2f) run(w, 1);
        check(w.col.nests[0].mate >= 0, TextFormat("the mate arrived after %.2f days", (w.time - t0) / D));
        // fed (the larder), it lays within the day; warmed, the eggs hatch in a day; fed chicks fledge into the plan's roles
        w.col.nests[0].larder = 100;
        float t1 = w.time;
        while (w.Count(BStage::Egg) == 0 && w.time - t1 < D) run(w, 1);
        int eggs = w.Count(BStage::Egg);
        check(eggs >= E.clutchMin && eggs <= E.clutchMax, TextFormat("a clutch of %d eggs", eggs));
        run(w, D * 1.1f, 0.25f);
        int chicks = w.Count(BStage::Chick);
        check(chicks == eggs, TextFormat("warmed by the mate, all %d hatched a day later", chicks));
        w.col.nests[0].larder = 100;
        run(w, D * 2.1f, 0.25f);
        int adults = w.Count(BStage::Adult);
        check(adults == eggs && w.Count(BStage::Adult, Role::Fisher) >= 1, TextFormat("two days on they fledged: %d fishers, %d feeders, %d builders", w.Count(BStage::Adult, Role::Fisher), w.Count(BStage::Adult, Role::Feeder), w.Count(BStage::Adult, Role::Builder)));
    }
    // ---- unwarmed eggs chill; unfed chicks starve before the adults
    {
        World w; w.Init("taloned", 6);
        for (int k = 0; k < 2; k++) { Bird e; e.id = w.col.nextId++; e.stage = BStage::Egg; e.nest = 0; e.pos = w.island.nest; w.col.birds.push_back(e); }
        w.me.st = FState::Fly; w.me.pos = {0, 60, 0};
        run(w, D * 2.1f, 0.25f);
        check(w.Count(BStage::Egg) == 0 && w.col.deaths.size() == 1 && w.col.deaths[0].first == "chilled", "eggs with no mate on them chill and fail after two days");
        World s; s.Init("taloned", 7);
        Bird c; c.stage = BStage::Chick; c.nest = 0; c.hunger = 0.5f; c.id = s.col.nextId++; s.col.birds.push_back(c);
        Bird a; a.stage = BStage::Adult; a.role = Role::Builder; a.hunger = 0.5f; a.id = s.col.nextId++; a.pos = s.col.caches[0].pos; s.col.birds.push_back(a);
        s.Cache0().clear();
        s.me.st = FState::Fly; s.me.pos = {0, 80, 0};
        float tc = -1, ta = -1;
        for (float t = 0; t < D * 1.5f; t += 0.1f) { s.Step(0.1f, FounderInput{}); if (tc < 0 && !s.col.birds[0].alive) tc = t; if (ta < 0 && !s.col.birds[1].alive) ta = t; }
        check(tc > 0 && (ta < 0 || ta > tc), TextFormat("with no food the chick starves first (%.2f days; the adult %s)", tc / D, ta < 0 ? "still alive" : TextFormat("at %.2f", ta / D)));
    }
    // ---- a fisher fishes the real sea into the cache; a feeder carries it to a hungry chick; a builder raises a nest
    {
        World w; w.Init("taloned", 8);
        w.me.st = FState::Fly; w.me.pos = {0, 80, 0};
        Bird f; f.stage = BStage::Adult; f.role = Role::Fisher; f.id = w.col.nextId++; f.pos = w.col.caches[0].pos; w.col.birds.push_back(f);
        Bird fd; fd.stage = BStage::Adult; fd.role = Role::Feeder; fd.id = w.col.nextId++; fd.pos = w.col.caches[0].pos; w.col.birds.push_back(fd);
        Bird b; b.stage = BStage::Adult; b.role = Role::Builder; b.id = w.col.nextId++; b.pos = w.col.caches[0].pos; w.col.birds.push_back(b);
        Bird c; c.stage = BStage::Chick; c.nest = 0; c.hunger = 0.4f; c.id = w.col.nextId++; w.col.birds.push_back(c);
        w.col.nestsWanted = 2;
        run(w, D * 0.5f, 0.1f);
        int caught = w.col.birds[0].caught;
        check(caught >= 1, TextFormat("in half a day a fisher brought %d fish to the cache (feed %.0f)", caught, w.col.feedToday));
        check(w.col.birds[3].alive && w.col.birds[3].hunger > 0.4f, TextFormat("the feeder kept the chick fed (hunger %.2f)", w.col.birds[3].hunger));
        check(w.col.nests.size() >= 2 && w.col.nests[1].built, TextFormat("the builder raised a second nest (%.0f twigs)", w.col.nests.size() > 1 ? w.col.nests[1].twigs : 0.0f));
    }
    // ---- the grounds regrow with the stock: a fished-out lagoon recovers slowly, a lightly fished one fast
    {
        World w; w.Init("taloned", 9);
        int lz = w.lagoonZone;
        for (auto& a : w.eco.agents) if (a.alive && a.diver < 0 && a.homeZone == lz && Catchable(w.eco.map->species[a.sp])) a.alive = w.Rand() < 0.08f;
        float low0 = w.StockOf(lz);
        w.me.st = FState::Fly; w.me.pos = {0, 80, 0};
        run(w, D, 0.25f);
        float low1 = w.StockOf(lz);
        World v; v.Init("taloned", 9);
        for (auto& a : v.eco.agents) if (a.alive && a.diver < 0 && a.homeZone == lz && Catchable(v.eco.map->species[a.sp])) a.alive = v.Rand() < 0.5f;
        float mid0 = v.StockOf(lz);
        v.me.st = FState::Fly; v.me.pos = {0, 80, 0};
        run(v, D, 0.25f);
        float mid1 = v.StockOf(lz);
        check(low1 - low0 < mid1 - mid0 && low1 < 0.5f, TextFormat("the lagoon fished to %.0f%% regrows to %.0f%% in a day; fished to %.0f%% it regrows to %.0f%%", low0 * 100, low1 * 100, mid0 * 100, mid1 * 100));
    }
    printf(fails ? "flight-colony-test: %d check(s) failed\n" : "flight-colony-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --flight-sim <island> <days> [careful|lagoon] [founder] [seed]
int RunFlightSim(int argc, char** argv) {
    std::string island = argc > 2 ? argv[2] : "tropical";
    int days = argc > 3 ? atoi(argv[3]) : 10;
    std::string mode = argc > 4 ? argv[4] : "careful";
    std::string founder = argc > 5 ? argv[5] : "taloned";
    uint32_t seed = argc > 6 ? (uint32_t)atoi(argv[6]) : 1;
    std::string why;
    if (!rt::DataOk(&why)) { printf("no data: %s\n", why.c_str()); return 1; }
    if (island != "tropical") printf("(only the tropical island exists until stage 3: using it)\n");
    World w; w.Init(founder, seed);
    w.founderBot = true; w.me.st = FState::Fly; w.me.pos = Vector3Add(w.island.nest, {0, 2, 0}); w.me.hunger = 1;
    bool lagoon = mode == "lagoon";
    if (lagoon) w.col.ground = w.lagoonZone;
    else w.col.restBelow = 0.45f;   // (a careful colony rests a ground that's running down)
    printf("The Flight --flight-sim: %s island, %d days, %s colony, %s founder, seed %u\n", island.c_str(), days, mode.c_str(), founder.c_str(), seed);
    printf("  day  birds  eggs chicks mates nests  fishers feeders builders  caught  feed/day mouths/day  food(days)  lagoon  deaths\n");
    int peak = 0, peakDay = 0;
    const float dt = 0.1f;
    float taskT[(int)Role::COUNT][16] = {}; float carryT = 0;
    int lastDay = 0;
    for (float t = 0; t < days * World::DAY; t += dt) {
        // the governor (a careful player's colony panel): nests ahead of the mates, the plan by the feed
        float food = w.DaysOfFood(), fpd = w.FeedPerDayEstimate(), mouths = w.MouthsPerDay();
        int mates = w.Count(BStage::Mate);
        int built = 0; for (const auto& n : w.col.nests) built += n.built;
        w.col.nestsWanted = std::min((int)w.col.sites.size(), std::max(2, mates + 1));
        if (!lagoon && food < 1 && w.dayNum > 0) w.col.nestsWanted = (int)w.col.nests.size();
        // fishers first; a feeder per eight workers; a builder or two while nests are wanted
        int under = 0; for (const auto& n : w.col.nests) under += !n.built;
        float fishShare = fpd < mouths * 1.15f ? 0.8f : 0.65f;
        w.col.plan[(int)Role::Fisher] = fishShare; w.col.plan[(int)Role::Feeder] = 0.13f; w.col.plan[(int)Role::Builder] = std::max(0.07f, 1 - fishShare - 0.13f);
        // idle hands retrain as fishers (twice a day at most): builders beyond the work in hand, feeders beyond one per eight
        if (fmodf(t, World::DAY * 0.5f) < dt) {
            int bld = w.Count(BStage::Adult, Role::Builder), fdr = w.Count(BStage::Adult, Role::Feeder), wk = w.Count(BStage::Adult) - fdr;
            if (bld > 1 + 2 * under) w.Retrain(Role::Fisher, Role::Builder);
            else if (fdr > 1 + wk / Econ().feederCover) w.Retrain(Role::Fisher, Role::Feeder);
        }
        FounderInput in;
        w.Step(dt, in);
        if (getenv("DEPTH_TRACE") && w.dayNum == 5 && fmodf(t, 12.0f) < dt) {
            std::vector<int> reach(w.eco.map->zones.size(), 0), all(w.eco.map->zones.size(), 0);
            for (const auto& a : w.eco.agents) if (a.alive && a.diver < 0 && a.zone >= 0 && Catchable(w.eco.map->species[a.sp]) && w.eco.map->species[a.sp].size <= 2) { all[a.zone]++; if (a.pos.y > -2.7f) reach[a.zone]++; }

            printf("    phase %.2f:", w.DayPhase()); for (size_t z = 0; z < reach.size(); z++) printf("  %d/%d", reach[z], all[z]);
            std::vector<int> at(w.eco.map->zones.size() + 1, 0); for (const auto& b : w.col.birds) if (b.alive && b.role == Role::Fisher && b.stage == BStage::Adult) { int z = w.eco.ZoneAt({b.goal.x, -1, b.goal.z}); at[z < 0 ? w.eco.map->zones.size() : z]++; }
            printf("   fishers' goals:"); for (int v : at) printf(" %d", v); printf("\n");
        }
        for (const auto& b : w.col.birds) if (b.alive && b.stage == BStage::Adult) { int r = (int)b.role; taskT[r][(int)b.task] += dt; if (b.carrySp >= 0 && b.role == Role::Fisher) carryT += dt; }
        if (w.dayNum != lastDay) {
            lastDay = w.dayNum;
            const DayStats& d = w.col.days.back();
            printf("  %3d  %5d  %4d %6d %5d %5d  %7d %7d %8d  %6d  %8.1f %10.1f  %10.2f  %5.0f%%  %6d\n", d.day, d.birds, d.eggs, d.chicks, d.mates, d.nests,
                   w.Count(BStage::Adult, Role::Fisher), w.Count(BStage::Adult, Role::Feeder), w.Count(BStage::Adult, Role::Builder), d.caught, d.feedCaught, d.mouths, d.mouths > 0 ? d.cacheFeed / d.mouths : 0, d.lagoon * 100, d.deaths);
            if (d.birds > peak) { peak = d.birds; peakDay = d.day; }
            (void)built;
        }
    }
    printf("  peak %d birds on day %d; wild mates left %d\n", peak, peakDay, w.col.wildMates);
    int starved = 0; for (const auto& d : w.col.deaths) if (d.first == "starved") starved += d.second;
    int day40 = 0; for (const auto& d : w.col.days) if (!day40 && d.birds >= 40) day40 = d.day;
    if (lagoon) printf("  GATE (outfishing the lagoon starves the colony): %s - the lagoon at %.0f%%, %d starved, %d birds left of %d\n", starved > peak / 2 && w.StockOf(w.lagoonZone) < 0.15f ? "yes" : "no", w.StockOf(w.lagoonZone) * 100, starved, w.Alive(), peak);
    else printf("  GATE (a careful colony grows to 40): %s%s (design target: 40 by day 6, 100 by day 10)\n", day40 ? "yes, on day " : "no", day40 ? std::to_string(day40).c_str() : "");
    static const char* TN[] = {"idle", "fly", "search", "dive", "deliver", "eat", "gather", "build", "sit", "fetch", "feed"};
    for (int r = 1; r < (int)Role::COUNT; r++) {
        float tot = 0; for (int k = 0; k < 11; k++) tot += taskT[r][k];
        if (tot <= 0) continue;
        printf("  %-8s time:", RoleName((Role)r)); for (int k = 0; k < 11; k++) if (taskT[r][k] > tot * 0.01f) printf("  %s %.0f%%", TN[k], 100 * taskT[r][k] / tot); printf("   (%.1f bird-days)\n", tot / World::DAY);
    }
    printf("  caught by ground:"); for (int z = 0; z < (int)w.eco.map->zones.size(); z++) printf("  %s %d", w.eco.map->zones[z].name.c_str(), w.caughtIn[z]); printf("\n");
    { std::vector<int> eaten(w.eco.map->zones.size(), 0); (void)eaten; }
    printf("  grounds at the end:"); for (int z = 0; z < (int)w.eco.map->zones.size(); z++) printf("  %s %.0f%%", w.eco.map->zones[z].name.c_str(), w.StockOf(z) * 100); printf("\n");
    printf("  deaths:"); if (w.col.deaths.empty()) printf(" none"); for (const auto& d : w.col.deaths) printf("  %s %d", d.first.c_str(), d.second); printf("\n");
    return 0;
}

}  // namespace fl
