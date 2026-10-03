// The Flight, stage 7: the dangerous islands, holding islands, sieges and assaults, bombing and chemistry (design doc
// pp. 15-18, 24-25, 21). Headless; every number is data (data/flight/flight_danger.json).
//
// An island is held by whoever has the most nests on it; the Founder (or a Pathfinder) founds an outpost on any island
// with a free site. The dangerous islands give their prize to whoever holds them and their danger to everyone: the
// kraken grabs low birds over its cove unless their colony offers it a fish a day, wakes to blood, bombs and storms, and
// can be killed; skull island's ape throws rocks at low flyers and sleeps when fed, its lizards and plants take chicks;
// the volcano erupts every six days after tremors (ash kills chicks and grounds birds, lava takes nests); the wreck
// drifts, sinks, has a ghost crew at night, a hold of salted fish and the ship's bell; the reef's eels take chicks.
// Storms ground birds and fog hides raids. A siege blockades grounds, walls the air over an island and starves it;
// an assault's Strikers tear nests down. The Works make bombs (guano and sulfur) for Bombers and stimulants (guano
// and pearls) for Chemists to dose flocks with, each with its crash.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fl {

namespace {
struct DangerData {
    float kHp = 4000, kBelow = 20, kRadius = 62, kAwake = 0.05f, kSurfaced = 0.12f, kAsleep = 0.01f, kWakeBlood = 6, kOfferDays = 1, kNeglect = 3, kCalm = 0.33f, kArmKill = 4, kBomb = 300, kHoard = 20, kRaidS = 30, kLoud = 10;
    float apeRange = 170, apeBelow = 25, apeThrow = 3, apeHit = 0.35f, apeDmg = 45, apeSleep = 1; int apeFeed = 4;
    float lizardDays = 0.5f, plantDays = 1, carcass = 4, skullShrine = 0.2f;
    float vEvery = 6, vFirst = 4.5f, vTremor = 0.5f, vAsh = 1, vAshR = 420, vLava = 0.5f, vSulfur = 2, vCrater = 2;
    float wDrift = 0.2f, wSink = 0.35f, wGone = 3.5f, wGhost = 0.2f, wRat = 0.5f; int wHold = 30; float wBell = 10;
    float eelDays = 1;
    float stormMin = 3, stormMax = 5, stormHours = 2, fogChance = 0.3f, fogHours = 2, fogSight = 0.5f;
    float blockadeM = 120, wallM = 150, desert = 0.15f, tear = 3;
    float bGuano = 5, bSulfur = 1, bMax = 4, bDmg = 60, bR = 8, bIncR = 12, blockEvery = 2, bMorale = 15, bNoise = 10;
    float hp[ST_COUNT] = {60, 120, 150, 100, 120};
    float stGuano[STIM_COUNT] = {0, 3, 4, 4, 6}, stPearls[STIM_COUNT] = {0, 0, 1, 1, 2};
    float haste = 1.3f, fury = 1.4f, furyBleed = 1.5f, clotBleed = 0.5f, draught = 1.5f, brewDays = 0.5f, effectDays = 1, crashDays = 1;
};
const DangerData& DD() {
    static DangerData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_danger.json");
    if (!j.IsObj()) return d;
    auto F = [&](const Json& o, const char* k, float& v) { if (o[k].IsNum()) v = o[k].F(v); };
    const Json& k = j["kraken"]; F(k, "hp", d.kHp); F(k, "grab_below_m", d.kBelow); F(k, "cove_radius_m", d.kRadius); F(k, "grab_per_s_awake", d.kAwake); F(k, "grab_per_s_surfaced", d.kSurfaced);
    F(k, "grab_per_s_asleep_low", d.kAsleep); F(k, "wake_blood", d.kWakeBlood); F(k, "offering_days", d.kOfferDays); F(k, "neglect_days", d.kNeglect); F(k, "calm_days", d.kCalm);
    F(k, "arm_kill_s", d.kArmKill); F(k, "bomb_damage", d.kBomb); F(k, "hoard_pearls", d.kHoard); F(k, "nest_raid_s", d.kRaidS); F(k, "loud_birds", d.kLoud);
    const Json& a = j["ape"]; F(a, "throw_range_m", d.apeRange); F(a, "throw_below_m", d.apeBelow); F(a, "throw_s", d.apeThrow); F(a, "hit", d.apeHit); F(a, "rock_damage", d.apeDmg); F(a, "sleep_days_per_feed", d.apeSleep);
    if (a["feed_size"].IsNum()) d.apeFeed = a["feed_size"].I(d.apeFeed);
    const Json& s = j["skull"]; F(s, "lizard_days", d.lizardDays); F(s, "plant_days", d.plantDays); F(s, "carcass_feed_per_day", d.carcass); F(s, "shrine_fervour", d.skullShrine);
    const Json& v = j["volcano"]; F(v, "every_days", d.vEvery); F(v, "first_days", d.vFirst); F(v, "tremor_days", d.vTremor); F(v, "ash_days", d.vAsh); F(v, "ash_radius_m", d.vAshR); F(v, "lava_share", d.vLava); F(v, "sulfur_per_day", d.vSulfur); F(v, "crater_fish_per_day", d.vCrater);
    const Json& w = j["wreck"]; F(w, "drift_mps", d.wDrift); F(w, "sink_per_day", d.wSink); F(w, "gone_at", d.wGone); F(w, "ghost_per_s", d.wGhost); F(w, "rat_days", d.wRat); F(w, "bell_morale", d.wBell);
    if (w["hold_fish"].IsNum()) d.wHold = w["hold_fish"].I(d.wHold);
    F(j["reef"], "eel_days", d.eelDays);
    const Json& we = j["weather"]; if (we["storm_every_days"].IsArr() && we["storm_every_days"].a.size() >= 2) { d.stormMin = we["storm_every_days"][0].F(3); d.stormMax = we["storm_every_days"][1].F(5); }
    F(we, "storm_hours", d.stormHours); F(we, "fog_chance_per_dawn", d.fogChance); F(we, "fog_hours", d.fogHours); F(we, "fog_sight", d.fogSight);
    F(j["siege"], "blockade_m", d.blockadeM); F(j["siege"], "wall_m", d.wallM); F(j["siege"], "desert_days_of_food", d.desert); F(j["assault"], "tear_per_strike", d.tear);
    const Json& b = j["bombs"]; F(b, "guano", d.bGuano); F(b, "sulfur", d.bSulfur); F(b, "max", d.bMax); F(b, "damage", d.bDmg); F(b, "radius_m", d.bR); F(b, "incendiary_radius_m", d.bIncR);
    F(b, "blockbuster_every_days", d.blockEvery); F(b, "morale_hit", d.bMorale); F(b, "noise", d.bNoise);
    static const char* SH[ST_COUNT] = {"hedge", "tower", "roost", "shrine", "works"};
    for (int i = 0; i < ST_COUNT; i++) F(j["structures_hp"], SH[i], d.hp[i]);
    const Json& st = j["stims"];
    static const char* SK[STIM_COUNT] = {"", "haste", "fury", "clot", "draught"};
    for (int i = 1; i < STIM_COUNT; i++) { F(st[SK[i]], "guano", d.stGuano[i]); F(st[SK[i]], "pearls", d.stPearls[i]); }
    F(st["haste"], "speed", d.haste); F(st["fury"], "attack", d.fury); F(st["fury"], "bleed", d.furyBleed); F(st["clot"], "bleed", d.clotBleed); F(st["draught"], "all", d.draught);
    F(st, "brew_days", d.brewDays); F(st, "effect_days", d.effectDays); F(st, "crash_days", d.crashDays);
    return d;
}
float Flat2(Vector3 a, Vector3 b) { return Vector2Distance({a.x, a.z}, {b.x, b.z}); }
float Hash01(int a, int b) { uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0f; }
}  // namespace

const char* StimName(int s) { static const char* N[STIM_COUNT] = {"none", "Haste", "Fury", "Clot", "the Draught"}; return N[std::clamp(s, 0, STIM_COUNT - 1)]; }
float StructureHp(int kind) { return DD().hp[std::clamp(kind, 0, ST_COUNT - 1)]; }
float StimSpeed(int stim) { return stim == STIM_HASTE ? DD().haste : stim == STIM_DRAUGHT ? DD().draught : 1.0f; }
float StimAttack(int stim) { return stim == STIM_FURY ? DD().fury : stim == STIM_DRAUGHT ? DD().draught : 1.0f; }
float StimBleed(int stim) { return stim == STIM_FURY ? DD().furyBleed : stim == STIM_CLOT ? DD().clotBleed : 1.0f; }
float BellMorale() { return DD().wBell; }
float FogSight() { return DD().fogSight; }
float TearPerStrike() { return DD().tear; }
float SkullShrineFervour() { return DD().skullShrine; }
float DesertDays() { return DD().desert; }

// ---------------------------------------------------------------- holding islands
int World::NestsOn(int side, int isle) const { int n = 0; for (const auto& ns : ColOf(side).nests) n += ns.built && ns.isle == isle; return n; }
int World::HolderOf(int isle) const {
    int best = -1, bn = 0; bool tie = false;
    for (int s = 0; s <= (int)sides.size(); s++) { int n = NestsOn(s, isle); if (n > bn) { bn = n; best = s; tie = false; } else if (n == bn && n > 0) tie = true; }
    return tie ? -1 : best;
}
bool World::FoundOutpost(int isle, Vector3 near) {
    if (isle < 0 || isle >= (int)isles.size() || isle == home) return false;
    const Island& is = isles[isle];
    if (is.type == IsleType::Wreck) { Say("The wreck can't be held: it's raided."); return false; }
    for (const auto& n : col.nests) if (n.isle == isle) return false;   // (already an outpost there: its builders raise more nests)
    bool already = false; for (const auto& s : col.sites) already |= s.isle == isle;
    // the island's free sites (none another colony nests on) join the colony's; the nearest gets a nest at once
    auto taken = [&](Vector3 p) { for (int s = 0; s <= (int)sides.size(); s++) for (const auto& n : ColOf(s).nests) if (Vector3Distance(n.pos, p) < 1.5f) return true; return false; };
    int first = -1; float bd = 1e9f;
    for (const auto& p : is.sites) {
        if (taken(p)) continue;
        int idx = -1;
        for (int k = 0; k < (int)col.sites.size(); k++) if (Vector3Distance(col.sites[k].pos, p) < 0.5f) idx = k;
        if (idx < 0) { Site s; s.pos = p; s.isle = isle; col.sites.push_back(s); idx = (int)col.sites.size() - 1; }
        float d = Vector3Distance(p, near);
        if (col.sites[idx].nest < 0 && d < bd) { bd = d; first = idx; }
    }
    if (first < 0) { Say(is.name + " has no free nest site."); return false; }
    Nest n; n.site = first; n.pos = col.sites[first].pos; n.isle = isle; n.bowlNeed = Econ().courtFish + Econ().courtStep * (int)col.nests.size();
    col.sites[first].nest = (int)col.nests.size(); col.nests.push_back(n);
    if (!already) {
        // a cache on its flattest ground near the first nest; its twigs and shells for the builders
        Vector3 cp = GroundAt(n.pos.x + 3, n.pos.z + 2);
        for (int k = 0; k < 80; k++) {
            float a = k * 0.7f, r = 3 + (k % 10) * 1.6f;
            Vector3 q = GroundAt(n.pos.x + cosf(a) * r, n.pos.z + sinf(a) * r);
            if (LandAt(q.x, q.z) && NormalAt(q.x, q.z).y > 0.8f) { cp = q; break; }
        }
        Cache c; c.pos = {cp.x, cp.y + 0.1f, cp.z}; c.built = false; c.isle = isle; col.caches.push_back(c);
        for (const auto& tp : is.twigPts) { TwigSource t; t.pos = tp.first; t.cap = tp.second < 0 ? (float)Econ().palmTwigs : tp.second; t.twigs = t.cap; col.twigSrc.push_back(t); }
        for (const auto& sp : is.shellPts) { TwigSource t; t.pos = Vector3Add(sp, {0, 0.15f, 0}); t.shells = true; t.cap = 4; t.twigs = t.cap; col.twigSrc.push_back(t); }
        Reveal(n.pos, 120, isle);
    }
    Say(TextFormat("An outpost on %s: a nest laid out (bring %d twigs) and a cache; whoever has the most nests there holds it.", is.name.c_str(), NestTwigs()));
    for (int s = 0; s <= (int)sides.size(); s++) if (s != cur && IsDangerous(is.type)) SayTo(s, SideName(cur) + " lands on " + is.name + ".");
    return true;
}

int World::OutpostSite() const {
    // a free site on an outpost island that has fewer than three nests (built or under way)
    for (const auto& n0 : col.nests) {
        if (n0.isle < 0 || n0.isle == home) continue;
        int count = 0; for (const auto& n : col.nests) count += n.isle == n0.isle;
        if (count >= 3) continue;
        int best = -1; float bd = 1e9f;
        for (int k = 0; k < (int)col.sites.size(); k++) {
            const Site& s = col.sites[k];
            if (s.isle != n0.isle || s.nest >= 0) continue;
            bool taken = false; for (int sd = 0; sd <= (int)sides.size(); sd++) for (const auto& n : ColOf(sd).nests) if (Vector3Distance(n.pos, s.pos) < 1.5f) taken = true;
            if (taken) continue;
            float d = Vector3Distance(s.pos, n0.pos); if (d < bd) { bd = d; best = k; }
        }
        if (best >= 0) return best;
    }
    return -1;
}

// ---------------------------------------------------------------- setting up
void World::InitDanger() {
    const DangerData& D = DD();
    kraken = Kraken{}; ape = Ape{}; volcano = Volcano{}; wreck = WreckState{}; weather = Weather{};
    for (int i = 0; i < (int)isles.size(); i++) {
        switch (isles[i].type) {
        case IsleType::KrakenCove: if (kraken.isle < 0) { kraken.isle = i; kraken.hp = kraken.hpMax = D.kHp; } break;
        case IsleType::Skull: if (ape.isle < 0) { ape.isle = i; ape.pos = isles[i].hill; } break;
        case IsleType::Volcano: if (volcano.isle < 0) { volcano.isle = i; volcano.next = D.vFirst * DAY; } break;
        case IsleType::Wreck: if (wreck.isle < 0) { wreck.isle = i; wreck.c0 = isles[i].c; float a = (float)(opts.seed % 628) / 100.0f; wreck.vel = {cosf(a) * D.wDrift, sinf(a) * D.wDrift}; wreck.hold = D.wHold; } break;
        default: break;
        }
    }
    weather.next = (D.stormMin + (D.stormMax - D.stormMin) * 0.5f) * DAY;
}
// the wreck where it has drifted to by now (a pure function of the time: a guest's mirror puts it in the same place)
void World::SetWreckPose() {
    if (wreck.isle < 0) return;
    const DangerData& D = DD();
    Island& is = isles[wreck.isle];
    float days = time / DAY;
    Vector3 want{wreck.c0.x + wreck.vel.x * time, 0, wreck.c0.z + wreck.vel.y * time};
    Vector3 d = Vector3Subtract(want, is.c);
    if (fabsf(d.x) > 0.01f || fabsf(d.z) > 0.01f) {
        is.c = Vector3Add(is.c, d); is.hill = Vector3Add(is.hill, d); is.nest = Vector3Add(is.nest, d);
        is.x0 += d.x; is.z0 += d.z;
        for (auto& s : is.sites) s = Vector3Add(s, d);
        for (auto& t : is.twigPts) t.first = Vector3Add(t.first, d);
        for (auto& p : is.props) p.c = Vector3Add(p.c, d);
        for (auto& o : is.outline) { o.x += d.x; o.y += d.z; }
    }
    float sink = std::min(D.wGone + 1, days * D.wSink);
    float ds = sink - is.drop;
    if (fabsf(ds) > 0.001f) { is.drop = sink; for (auto& p : is.props) p.c.y -= ds; is.hill.y -= ds; }
    wreck.sunk = sink;
    wreck.gone = sink >= D.wGone;
}

// ---------------------------------------------------------------- the weather, siege tests
bool World::Grounded(Vector3 p) const {
    if (StormNow()) return true;
    if (volcano.isle >= 0 && volcano.ashT > 0 && Flat2(p, isles[volcano.isle].c) < DD().vAshR) return true;
    return false;
}
bool World::Blockaded(int zone, int side) const {
    if (zone < 0 || zone >= (int)eco.map->zones.size()) return false;
    Vector3 zc = eco.map->zones[zone].Center();
    for (int s = 0; s <= (int)sides.size(); s++) {
        if (s == side || Truce(s, side) || !ColOf(s).HasTier(Tree::War, 4)) continue;
        for (const auto& f : ColOf(s).flocks) if (!f.retreating && !f.members.empty() && f.stance == Stance::Hold && f.target == Target::Ground && f.tZone == zone && Flat2(f.pos, zc) < DD().blockadeM) return true;
    }
    return false;
}
bool World::Walled(int isle, int side) const {
    if (isle < 0 || isle >= (int)isles.size()) return false;
    for (int s = 0; s <= (int)sides.size(); s++) {
        if (s == side || Truce(s, side)) continue;
        for (const auto& f : ColOf(s).flocks) if (!f.retreating && f.members.size() >= 3 && f.form == Formation::Wall && Flat2(f.pos, isles[isle].c) < DD().wallM) return true;
    }
    return false;
}

// ---------------------------------------------------------------- the Founder's hands on the dangers
bool World::OfferKraken() {
    if (kraken.isle < 0 || kraken.dead || me.carrySp < 0) return false;
    if (Flat2(me.pos, isles[kraken.isle].c) > DD().kRadius + 20) return false;
    col.offeredKraken = time; me.carrySp = -1; me.carrySize = 0;
    Say("You drop a fish into the cove: the kraken lets your birds be for a day.");
    return true;
}
bool World::FeedApe() {
    if (ape.isle < 0 || me.carrySp < 0 || me.carrySize < DD().apeFeed || Flat2(me.pos, ape.pos) > 25) return false;
    ape.sleepT = DD().apeSleep * DAY; col.apeFedT = time; me.carrySp = -1; me.carrySize = 0;
    Say("The great ape takes the fish, and sleeps for a day.");
    return true;
}

// ---------------------------------------------------------------- bombs and stimulants
void World::Blast(Vector3 at, int side, int kind) {
    const DangerData& D = DD();
    float r = kind == 1 ? D.bIncR : D.bR;
    if (warFx.size() > 400) { warFxBase += 200; warFx.erase(warFx.begin(), warFx.begin() + 200); }
    warFx.push_back({at, 3, side, Role::Bomber, (float)kind});
    eco.AddNoise(at, D.bNoise, true);
    std::string by = SideName(side) + "'s bomb";
    for (int s = 0; s <= (int)sides.size(); s++) {
        Colony& C = ColOf(s);
        // birds in the blast (friend or foe); a flock in it is shaken
        for (auto& b : C.birds) {
            if (!b.alive || b.stage == BStage::Egg || Vector3Distance(b.pos, at) > r) continue;
            b.hp -= D.bDmg;
            if (b.hp <= 0 || b.stage == BStage::Chick) { WithSide(s, [&] { BirdDies(b, "blasted by " + by); }); C.losses++; if (s != side) ColOf(side).kills++; }
        }
        for (auto& f : C.flocks) if (Flat2(f.pos, at) < r * 2.5f) { f.lost += 1; f.wins = std::max(0.0f, f.wins - 1); }
        Founder& F = FounderOf(s);
        if (F.st != FState::Dead && Vector3Distance(F.pos, at) < r) { F.hp -= D.bDmg; if (F.hp <= 0) { if (HumanOf(s)) WithSide(s, [&] { Kill("blasted by " + by); }); else { F.st = FState::Dead; F.respawnT = 30; F.deaths++; } } }
        // nests: torn apart (an incendiary burns them), their eggs and chicks with them
        for (int ni = 0; ni < (int)C.nests.size(); ni++) {
            Nest& n = C.nests[ni];
            if (!n.built || Vector3Distance(n.pos, at) > r) continue;
            n.built = false; n.twigs = 0; n.tear = 0;
            for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick || b.stage == BStage::Mate)) WithSide(s, [&] { BirdDies(b, "blasted by " + by); });
            n.mate = -1; n.bowl = 0;
            if (s != side) ColOf(side).nestsDestroyed++;
            SayTo(s, TextFormat("%s's bomb has destroyed one of your nests!", SideName(side).c_str()));
        }
        // structures: hedges and towers break; a blockbuster fells a Roost or a tower outright; an incendiary burns hedges
        for (int k = (int)C.builds.size() - 1; k >= 0; k--) {
            Structure& st = C.builds[k];
            float reach = kind == 2 ? 20.0f : r;
            if (Vector3Distance(st.pos, at) > reach) continue;
            if (kind == 2 && (st.kind == ST_ROOST || st.kind == ST_TOWER)) st.hp = 0;
            else if (kind == 1 && st.kind == ST_HEDGE) st.hp = 0;
            else if (st.kind != ST_ROOST || kind == 2) st.hp -= D.bDmg;
            if (st.hp <= 0) {
                SayTo(s, TextFormat("%s's bomb has destroyed your %s!", SideName(side).c_str(), StructureName(st.kind)));
                if (st.kind == ST_ROOST && C.resTree >= 0) { C.resTree = -1; }   // (the research line stops)
                C.builds.erase(C.builds.begin() + k);
            }
        }
    }
    // the cove: a bomb wakes the kraken and wounds it; skull island's plants burn
    if (kraken.isle >= 0 && !kraken.dead && Flat2(at, isles[kraken.isle].c) < D.kRadius) { kraken.hp -= D.kBomb; kraken.mood = 2; kraken.calmT = 0; kraken.moodT = 0; }
    if (kind == 1 && ape.isle >= 0 && Flat2(at, isles[ape.isle].c) < isles[ape.isle].radius) ape.plantsBurnt = time + 3 * DAY;
}
bool World::Dose(int flock, int stim) {
    if (stim <= STIM_NONE || stim >= STIM_COUNT || col.stims[stim] <= 0) return false;
    Flock* f = FindFlock(cur, flock);
    if (!f || f->stim != STIM_NONE) return false;
    f->stim = stim; f->stimT = DD().effectDays * DAY; f->crashT = 0;
    col.stims[stim]--;
    Say(TextFormat("%s is dosed with %s.", f->name.c_str(), StimName(stim)));
    return true;
}
void World::StepWorks(float dt) {
    const DangerData& D = DD();
    Colony& C = col;
    // the flocks' stimulants run out, then crash (Haste: a day of half breath; the Draught: a day asleep where they are)
    for (auto& f : C.flocks) {
        if (f.stimT > 0) {
            f.stimT -= dt;
            if (f.stimT <= 0 && (f.stim == STIM_HASTE || f.stim == STIM_DRAUGHT)) {
                f.crashT = D.crashDays * DAY;
                if (f.stim == STIM_DRAUGHT) for (int id : f.members) if (Bird* b = FindBird(cur, id)) b->netT = f.crashT;   // (they sleep wherever they are)
                Say(f.name + (f.stim == STIM_DRAUGHT ? "'s Draught wears off: the whole flock falls asleep where it is." : "'s Haste wears off: a day of half breath."));
            } else if (f.stimT <= 0) f.stim = STIM_NONE;
        } else if (f.crashT > 0) { f.crashT -= dt; if (f.crashT <= 0) f.stim = STIM_NONE; }
    }
    if (!Built(ST_WORKS)) return;
    // bombs: one a day from guano and sulfur (Bombing 1); a blockbuster every two days (Bombing 4)
    if (C.HasTier(Tree::Bombing, 1)) {
        C.bombT += dt;
        float every = DAY * (C.boomState == 1 ? 0.6f : 1.0f);
        if (C.bombT >= every) { C.bombT = 0; float bg = D.bGuano * SeasonNow().bombCost; if (C.bombs < D.bMax && C.guano >= bg && C.sulfur >= D.bSulfur) { C.bombs++; C.guano -= bg; C.sulfur -= D.bSulfur; Say(TextFormat("The Works have made a bomb (%d ready).", C.bombs)); } }
        if (C.HasTier(Tree::Bombing, 4)) { C.blockT += dt; if (C.blockT >= D.blockEvery * DAY) { C.blockT = 0; if (C.blockbusters < 2 && C.guano >= 2 * D.bGuano && C.sulfur >= 2 * D.bSulfur) { C.blockbusters++; C.guano -= 2 * D.bGuano; C.sulfur -= 2 * D.bSulfur; Say("The Works have made a blockbuster."); } } }
    }
    // stimulants: each Chemist brews a dose every half day of the colony's choice, if it's researched and afforded
    int chemists = 0; for (const auto& b : C.birds) chemists += b.alive && b.stage == BStage::Adult && b.role == Role::Chemist && b.retrainT <= 0;
    if (chemists > 0) {
        C.brewT += dt * chemists;
        if (C.brewT >= D.brewDays * DAY) {
            C.brewT = 0;
            int k = C.brewFor;
            int need = k == STIM_HASTE ? 2 : k == STIM_DRAUGHT ? 4 : 3;
            if (k > STIM_NONE && k < STIM_COUNT && C.HasTier(Tree::Chemistry, need) && C.guano >= D.stGuano[k] && C.pearls >= D.stPearls[k] && C.stims[k] < 6) {
                C.guano -= D.stGuano[k]; C.pearls -= (int)D.stPearls[k]; C.stims[k]++;
                Say(TextFormat("The Chemists have brewed a dose of %s.", StimName(k)));
            }
        }
    }
}

// ---------------------------------------------------------------- the dangers' step (once a world step)
void World::StepDanger(float dt) {
    const DangerData& D = DD();
    int N = (int)sides.size() + 1;
    float ph = DayPhase();
    bool night = ph < 0.2f || ph > 0.85f;
    // ---- weather: a storm every three to five days for two hours; fog some dawns
    if (weather.kind != 0) { weather.t -= dt; if (weather.t <= 0) { weather.kind = 0; for (int s = 0; s < N; s++) SayTo(s, "The weather clears."); } }
    if (weather.kind == 0 && time >= weather.next) {
        weather.kind = 1; weather.t = D.stormHours * DAY / 24; weather.next = time + (D.stormMin + (D.stormMax - D.stormMin) * Rand()) * DAY / std::max(0.2f, SeasonNow().stormK);   // (autumn: storms twice as often)
        for (auto& t : towns) t.storm = weather.t + DAY * 0.25f;
        for (int s = 0; s < N; s++) SayTo(s, "A storm crosses the map: every bird is grounded until it passes (the towns pay double for fish).");
        // the atoll floods: eggs on its ring are lost
        for (int s = 0; s < N; s++) { Colony& C = ColOf(s); for (auto& b : C.birds) if (b.alive && b.stage == BStage::Egg && b.nest >= 0 && b.nest < (int)C.nests.size()) { int is = C.nests[b.nest].isle; if (is >= 0 && is < (int)isles.size() && isles[is].type == IsleType::Atoll && Rand() < 0.5f) WithSide(s, [&] { BirdDies(b, "washed away by the storm"); }); } }
    }
    if (weather.kind == 0 && ph > 0.2f && ph < 0.2f + dt / DAY * 1.01f) {
        if (Rand() < D.fogChance) { weather.kind = 2; weather.t = D.fogHours * DAY / 24; for (int s = 0; s < N; s++) SayTo(s, "Fog at dawn: raids go unseen, and the Watchers see half as far."); }
    }
    // ---- the kraken
    if (kraken.isle >= 0 && !kraken.dead) {
        Vector3 cove = isles[kraken.isle].c;
        float blood = eco.BloodNear({cove.x, -0.5f, cove.z}, D.kRadius);
        bool offered = false; for (int s = 0; s < N; s++) offered |= time - ColOf(s).offeredKraken < D.kNeglect * DAY;
        float wake = D.kWakeBlood * SeasonNow().krakenWake;   // (autumn: it wakes on half the blood)
        int want = StormNow() || blood > 2 * wake || (!offered && time > D.kNeglect * DAY) ? 2 : blood > wake ? 1 : 0;
        if (want > kraken.mood) { kraken.mood = want; kraken.calmT = 0; if (want == 2) for (int s = 0; s < N; s++) if (NestsOn(s, kraken.isle) > 0 || Flat2(FounderOf(s).pos, cove) < 400) SayTo(s, "THE KRAKEN SURFACES in its cove."); }
        else if (want < kraken.mood) { kraken.calmT += dt; if (kraken.calmT > D.kCalm * DAY) { kraken.mood = want; kraken.calmT = 0; } }
        // grabs: any bird low over the cove whose colony hasn't offered today
        kraken.grabT += dt;
        if (kraken.armT > 0) kraken.armT -= dt;
        if (kraken.grabT >= 1) {
            kraken.grabT = 0;
            for (int s = 0; s < N; s++) {
                Colony& C = ColOf(s);
                bool safe = time - C.offeredKraken < D.kOfferDays * DAY || DecreeOf(s).dangersIgnore;   // (Offerings: it ignores you today)
                if (safe) continue;
                auto chance = [&](float y) { return kraken.mood == 2 ? D.kSurfaced : kraken.mood == 1 ? D.kAwake : (y < 5 ? D.kAsleep : 0.0f); };
                for (auto& b : C.birds) {
                    if (!b.alive || b.stage != BStage::Adult || b.pos.y > D.kBelow || Flat2(b.pos, cove) > D.kRadius) continue;
                    if (Rand() < chance(b.pos.y)) { kraken.arm = b.pos; kraken.armT = 1.2f; WithSide(s, [&] { BirdDies(b, "taken by the kraken"); }); eco.AddBlood({b.pos.x, -0.5f, b.pos.z}, 3); }
                }
                Founder& F = FounderOf(s);
                if (F.st != FState::Dead && F.pos.y < D.kBelow && Flat2(F.pos, cove) < D.kRadius && Rand() < chance(F.pos.y)) {
                    kraken.arm = F.pos; kraken.armT = 1.2f;
                    if (HumanOf(s)) WithSide(s, [&] { Kill("taken by the kraken"); }); else { F.st = FState::Dead; F.respawnT = 30; F.deaths++; }
                }
            }
        }
        // surfaced, it raids the nests on the cliffs of a loud colony
        if (kraken.mood == 2) {
            kraken.moodT += dt;
            if (kraken.moodT >= D.kRaidS) {
                kraken.moodT = 0;
                for (int s = 0; s < N; s++) {
                    Colony& C = ColOf(s);
                    int there = 0; for (const auto& b : C.birds) there += b.alive && b.stage == BStage::Adult && Flat2(b.pos, cove) < isles[kraken.isle].radius + 20;
                    if (there < D.kLoud) continue;
                    for (auto& b : C.birds) if (b.alive && (b.stage == BStage::Egg || b.stage == BStage::Chick) && b.nest >= 0 && b.nest < (int)C.nests.size() && C.nests[b.nest].isle == kraken.isle) { WithSide(s, [&] { BirdDies(b, "taken from the cliffs by the kraken"); }); break; }
                }
            }
            // a flock that fights it over the cove: its strikes wound it; an arm takes one of them every few seconds
            float dmg = 0; int bySide = -1; float most = 0;
            for (int s = 0; s < N; s++) for (auto& f : ColOf(s).flocks) {
                if (f.retreating || Flat2(f.pos, cove) > D.kRadius + 10) continue;
                float sideDmg = 0;
                for (int id : f.members) if (Bird* b = FindBird(s, id); b && b->pos.y < D.kBelow + 10) sideDmg += RoleOf(b->role).attack / std::max(0.3f, RoleOf(b->role).cooldown) * dt * BendOfSide(s).attack * StimAttack(f.stim);
                dmg += sideDmg; if (sideDmg > most) { most = sideDmg; bySide = s; }
            }
            if (dmg > 0) {
                kraken.hp -= dmg;
                kraken.armKill += dt;
                if (kraken.armKill >= D.kArmKill) {
                    kraken.armKill = 0;
                    for (int s = 0; s < N; s++) for (auto& f : ColOf(s).flocks) if (!f.members.empty() && Flat2(f.pos, cove) < D.kRadius + 10) { if (Bird* b = FindBird(s, f.members[(int)(Rand() * f.members.size()) % f.members.size()])) { kraken.arm = b->pos; kraken.armT = 1.2f; WithSide(s, [&] { BirdDies(*b, "taken by the kraken"); }); } s = N; break; }
                }
                if (kraken.hp <= 0 && bySide >= 0) {
                    kraken.dead = true; kraken.killedBy = bySide;
                    Colony& K = ColOf(bySide); K.krakenKill = 1; K.pearls += (int)D.kHoard;
                    for (auto& st : stocks) { int zi = st.zone; if (zi >= 0 && eco.ZoneAt(eco.map->zones[zi].Center()) == zi && Flat2(eco.map->zones[zi].Center(), cove) < 220) st.K *= 0.5f; }   // (nothing keeps the sharks out now)
                    for (int s = 0; s < N; s++) SayTo(s, s == bySide ? "THE KRAKEN IS DEAD: your flock has killed it (its hoard of pearls is yours; the cove's yield halves for everyone)." : SideName(bySide) + " has killed the kraken.");
                }
            }
        }
    }
    // ---- skull island: the ape's rocks, the lizards, the plants; the holder's carcasses
    if (ape.isle >= 0) {
        if (ape.sleepT > 0) ape.sleepT -= dt;
        if (ape.rockT > 0) ape.rockT -= dt;
        ape.throwT -= dt;
        if (ape.sleepT <= 0 && ape.throwT <= 0) {
            ape.throwT = D.apeThrow;
            // the nearest low flyer in range, whoever's
            int ts = -1; Bird* tb = nullptr; Founder* tf = nullptr; float bd = D.apeRange;
            for (int s = 0; s < N; s++) {
                if (DecreeOf(s).dangersIgnore) continue;   // (Offerings)
                for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && b.pos.y < D.apeBelow + ape.pos.y * 0.3f && Vector3Length(b.vel) > 0.5f) { float d = Flat2(b.pos, ape.pos); if (d < bd) { bd = d; ts = s; tb = &b; tf = nullptr; } }
                Founder& F = FounderOf(s);
                if (F.st == FState::Fly && F.pos.y < D.apeBelow + ape.pos.y * 0.3f) { float d = Flat2(F.pos, ape.pos); if (d < bd) { bd = d; ts = s; tf = &F; tb = nullptr; } }
            }
            if (ts >= 0) {
                Vector3 to = tb ? tb->pos : tf->pos;
                ape.rockFrom = Vector3Add(ape.pos, {0, 4, 0}); ape.rockTo = to; ape.rockT = 1.0f;
                if (Rand() < D.apeHit) {
                    if (tb) { tb->hp -= D.apeDmg; if (tb->hp <= 0 || !IsWarrior(tb->role)) { Bird& b = *tb; WithSide(ts, [&] { BirdDies(b, "struck by the great ape's rock"); }); } }
                    else { tf->hp -= D.apeDmg; if (tf->hp <= 0) { if (HumanOf(ts)) WithSide(ts, [&] { Kill("struck by the great ape's rock"); }); else { tf->st = FState::Dead; tf->respawnT = 30; tf->deaths++; } } else SayTo(ts, "A rock from the great ape hits you!"); }
                }
            }
        }
        ape.lizardT += dt; ape.plantT += dt;
        bool lizards = ape.lizardT >= D.lizardDays * DAY, plants = ape.plantT >= D.plantDays * DAY && ape.plantsBurnt < time;
        if (lizards) ape.lizardT = 0;
        if (plants || ape.plantT >= D.plantDays * DAY) ape.plantT = 0;
        if (lizards || plants) for (int s = 0; s < N; s++) {
            Colony& C = ColOf(s);
            for (auto& b : C.birds) {
                if (!b.alive || (b.stage != BStage::Egg && b.stage != BStage::Chick) || b.nest < 0 || b.nest >= (int)C.nests.size() || C.nests[b.nest].isle != ape.isle) continue;
                bool watched = false; for (const auto& w : C.birds) watched |= w.alive && w.role == Role::Watcher && w.stage == BStage::Adult && Vector3Distance(w.pos, b.pos) < 40;
                if (lizards && !watched) { WithSide(s, [&] { BirdDies(b, "taken by the giant lizards"); }); lizards = false; continue; }
                if (plants) { WithSide(s, [&] { BirdDies(b, "eaten by the carnivorous plants"); }); plants = false; }
            }
        }
    }
    // ---- the volcano: tremors, then the eruption (ash and lava); the holder's sulfur and the crater lake
    if (volcano.isle >= 0) {
        Vector3 vc = isles[volcano.isle].c;
        if (volcano.ashT > 0) volcano.ashT -= dt;
        if (volcano.tremorT <= 0 && time >= volcano.next - D.vTremor * DAY) {
            volcano.tremorT = 1;
            for (int s = 0; s < N; s++) {
                bool near = false; for (const auto& n : ColOf(s).nests) near |= n.built && Flat2(n.pos, vc) < D.vAshR;
                if (near || Flat2(FounderOf(s).pos, vc) < D.vAshR * 1.5f) SayTo(s, "TREMORS on the volcano: it will erupt within half a day (move the eggs, keep the birds away).");
            }
        }
        if (time >= volcano.next) {
            volcano.next += D.vEvery * DAY; volcano.tremorT = 0; volcano.ashT = D.vAsh * DAY; volcano.eruptions++;
            for (int s = 0; s < N; s++) {
                Colony& C = ColOf(s);
                for (int ni = 0; ni < (int)C.nests.size(); ni++) {
                    Nest& n = C.nests[ni];
                    if (!n.built) continue;
                    bool onIt = n.isle == volcano.isle;
                    float a = atan2f(n.pos.z - vc.z, n.pos.x - vc.x);
                    bool lava = onIt && Hash01((int)(a * 100), volcano.eruptions) < D.vLava && !C.HasTier(Tree::Nesting, 4);
                    if (lava) {
                        n.built = false; n.twigs = 0;
                        for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage != BStage::Adult) WithSide(s, [&] { BirdDies(b, "taken by the lava"); });
                        n.mate = -1; n.bowl = 0;
                        SayTo(s, "Lava takes one of your nests on the volcano.");
                    } else if (Flat2(n.pos, vc) < D.vAshR && !C.HasTier(Tree::Nesting, 4)) {
                        for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage == BStage::Chick) WithSide(s, [&] { BirdDies(b, "choked by the volcano's ash"); });
                    }
                }
                SayTo(s, "The volcano ERUPTS: ash grounds every bird near it for a day.");
            }
        }
        int holder = HolderOf(volcano.isle);
        if (holder >= 0 && fmodf(time, DAY) < dt) {
            Colony& H = ColOf(holder);
            H.sulfur += D.vSulfur;
            int ci = -1; for (int k = 0; k < (int)H.caches.size(); k++) if (H.caches[k].built && H.caches[k].isle == volcano.isle) ci = k;
            if (ci < 0 && !H.caches.empty()) ci = 0;
            if (ci >= 0) for (int k = 0; k < (int)D.vCrater; k++) H.caches[ci].fish.push_back({eco.map->SpeciesIndex("Tuna"), 4, 0});   // (the crater lake's strange fish: size 4, and nothing there eats birds)
            SayTo(holder, TextFormat("The volcano gives its holder %.0f sulfur and %.0f of the crater lake's big fish.", D.vSulfur, D.vCrater));
        }
    }
    // ---- skull island's holder: carcasses from the beasts' kills
    if (ape.isle >= 0) {
        int holder = HolderOf(ape.isle);
        if (holder >= 0 && fmodf(time, DAY) < dt) {
            Colony& H = ColOf(holder);
            int ci = -1; for (int k = 0; k < (int)H.caches.size(); k++) if (H.caches[k].built && H.caches[k].isle == ape.isle) ci = k;
            if (ci < 0 && !H.caches.empty()) ci = 0;
            if (ci >= 0) for (int k = 0; k < (int)(D.carcass / 2); k++) H.caches[ci].fish.push_back({eco.map->SpeciesIndex("Mullet"), 2, 0});
        }
    }
    // ---- the wreck: it drifts and sinks; its ghost crew at night; rats in its hold
    if (wreck.isle >= 0) {
        SetWreckPose();
        Vector3 wc = isles[wreck.isle].c;
        if (!wreck.gone && night) {
            wreck.ghostT += dt;
            if (wreck.ghostT >= 1) {
                wreck.ghostT = 0;
                for (int s = 0; s < N; s++) for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && Flat2(b.pos, wc) < 25 && b.pos.y < isles[wreck.isle].hill.y + 8 && Rand() < D.wGhost) WithSide(s, [&] { BirdDies(b, "taken by the ghost crew"); });
            }
        }
        wreck.ratT += dt;
        if (wreck.ratT >= D.wRat * DAY) { wreck.ratT = 0; if (wreck.hold > 0) wreck.hold--; }
    }
    // ---- the reef garden's eels take chicks from nests on its cay
    for (int i = 0; i < (int)isles.size(); i++) {
        if (isles[i].type != IsleType::ReefGarden || fmodf(time, D.eelDays * DAY) >= dt) continue;
        for (int s = 0; s < N; s++) { Colony& C = ColOf(s); for (auto& b : C.birds) if (b.alive && (b.stage == BStage::Chick || b.stage == BStage::Egg) && b.nest >= 0 && b.nest < (int)C.nests.size() && C.nests[b.nest].isle == i) { WithSide(s, [&] { BirdDies(b, "taken by the reef's eels"); }); break; } }
    }
}

// ---------------------------------------------------------------- a bot's dangers: the Works, expansion, offerings, sieges
void World::BotDanger(float dt) {
    Colony& C = col;
    int alive = Alive();
    bool works = false; for (const auto& s : C.builds) works |= s.kind == ST_WORKS;
    if (!works && (C.HasTier(Tree::Bombing, 1) || C.HasTier(Tree::Chemistry, 1)) && alive >= 12) { Structure s; s.kind = ST_WORKS; s.pos = GroundAt(C.caches[0].pos.x - 6, C.caches[0].pos.z - 4); s.isle = home; C.builds.push_back(s); }
    if (RoleUnlocked(Role::Bomber) && Built(ST_WORKS)) C.plan[(int)Role::Bomber] = 0.05f;
    if (RoleUnlocked(Role::Chemist) && Built(ST_WORKS)) { C.plan[(int)Role::Chemist] = 0.03f; C.brewFor = C.HasTier(Tree::Chemistry, 3) ? STIM_FURY : STIM_HASTE; }
    // a cove outpost keeps the kraken fed: the priests drop a fish each day (or the Founder carries one: BotFounderStep)
    bool atCove = false; for (const auto& n : C.nests) atCove |= n.isle == kraken.isle;
    if (kraken.isle >= 0 && !kraken.dead && atCove && time - C.offeredKraken > DAY * 0.8f) {
        if (Count(BStage::Adult, Role::Priest) > 0) { int ci = NearestCache(isles[kraken.isle].c, true, false); if (ci >= 0) { C.caches[ci].fish.pop_back(); C.offeredKraken = time; } }
    }
    // expansion: a strong colony takes another island (the cove when it's strong enough to hold it)
    int outposts = 0; for (const auto& n : C.nests) outposts += n.isle >= 0 && n.isle != home;
    if (C.expandTo < 0 && alive >= 16 && C.HasTier(Tree::War, 2) && outposts == 0 && time > DAY * 2) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < (int)isles.size(); i++) {
            const Island& is = isles[i];
            if (i == home || is.start >= 0 || is.type == IsleType::Wreck || is.type == IsleType::Town || is.sites.empty()) continue;
            if (HolderOf(i) >= 0) continue;
            float d = Flat2(is.c, isles[home].c) * (is.type == IsleType::KrakenCove && alive >= 22 ? 0.4f : 1.0f);
            if (d < bd) { bd = d; best = i; }
        }
        C.expandTo = best;
    }
    (void)dt;
}

// ---------------------------------------------------------------- tests
namespace {
Bird& Adult(World& w, int side, Role r, Vector3 p) {
    Colony& C = w.ColOf(side);
    Bird b; b.id = C.nextId++; b.stage = BStage::Adult; b.role = r; b.hp = RoleOf(r).hp; b.fight = 25; b.hunger = 1; b.pos = p; C.birds.push_back(b);
    return C.birds.back();
}
int IsleOf(const World& w, IsleType t) { for (int i = 0; i < (int)w.isles.size(); i++) if (w.isles[i].type == t) return i; return -1; }
void Feed(World& w, int side, int n) { for (int k = 0; k < n; k++) w.ColOf(side).caches[0].fish.push_back({w.eco.map->SpeciesIndex("Mullet"), 2, 0}); }
// an outpost with built nests on an island for a side (as if its builders had raised them)
void Outpost(World& w, int side, int isle, int nests) {
    w.WithSide(side, [&] {
        w.FoundOutpost(isle, w.isles[isle].c);
        int made = 0;
        for (auto& n : w.col.nests) if (n.isle == isle) { n.built = true; n.twigs = (float)w.NestTwigs(); made++; }
        for (int k = 0; k < (int)w.col.sites.size() && made < nests; k++) {
            Site& s = w.col.sites[k];
            if (s.isle != isle || s.nest >= 0) continue;
            Nest n; n.site = k; n.pos = s.pos; n.isle = isle; n.built = true; n.twigs = (float)w.NestTwigs(); s.nest = (int)w.col.nests.size(); w.col.nests.push_back(n); made++;
        }
        for (auto& c : w.col.caches) if (c.isle == isle) c.built = true;
    });
}
}
int RunFlightDangerTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 7: the dangerous islands, holding islands, sieges, bombs and stimulants\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    auto fresh = [](uint32_t seed = 61) { auto w = std::make_unique<World>(); MapOpts o; o.players = 3; w->Init("taloned", seed, o); return w; };
    auto run = [](World& w, float secs, float dt = 0.1f) { for (float t = 0; t < secs; t += dt) w.Step(dt, FounderInput{w.me.yaw}); };
    // holding islands: an outpost, its nests, the holder, the score
    {
        auto w = fresh();
        int islet = IsleOf(*w, IsleType::Islet);
        int before = w->Score(0).isles;
        check(islet >= 0 && w->FoundOutpost(islet, w->isles[islet].c), "the Founder founds an outpost on an islet (a nest and a cache)");
        bool tagged = false; for (const auto& n : w->col.nests) tagged |= n.isle == islet;
        check(tagged && w->HolderOf(islet) < 0, "its nest is laid out on the islet; nobody holds it until a nest is built");
        Outpost(*w, 0, islet, 1);
        check(w->HolderOf(islet) == 0 && w->Score(0).isles == before + 30, TextFormat("built, the islet is held: islands %d -> %d points", before, w->Score(0).isles));
        Outpost(*w, 1, islet, 3);
        check(w->HolderOf(islet) == 1, TextFormat("a rival with more nests there takes it (%d to %d)", w->NestsOn(1, islet), w->NestsOn(0, islet)));
    }
    // the kraken
    {
        auto w = fresh();
        int cove = w->kraken.isle;
        check(cove >= 0 && w->kraken.mood == 0, "the Kraken Cove has its kraken, asleep");
        Vector3 c = w->isles[cove].c;
        w->kraken.mood = 2;
        std::vector<int> ids; for (int k = 0; k < 6; k++) ids.push_back(Adult(*w, 0, Role::Fisher, {c.x + k * 3.0f, 6, c.z}).id);
        int lost = 0; for (float t = 0; t < 30; t += 0.1f) { w->kraken.mood = 2; w->Step(0.1f, FounderInput{w->me.yaw}); for (int id : ids) if (!w->FindBird(0, id)) { lost++; ids.erase(std::find(ids.begin(), ids.end(), id)); break; } }
        check(lost >= 2, TextFormat("surfaced, it takes %d of six birds low over the cove in 30 s", lost));
        auto u = fresh(); u->kraken.mood = 2; u->col.offeredKraken = 0;
        std::vector<int> safe; for (int k = 0; k < 6; k++) safe.push_back(Adult(*u, 0, Role::Fisher, {c.x + k * 3.0f, 6, c.z}).id);
        for (float t = 0; t < 30; t += 0.1f) { u->kraken.mood = 2; u->Step(0.1f, FounderInput{u->me.yaw}); }
        int kept = 0; for (int id : safe) kept += u->FindBird(0, id) != nullptr;
        check(kept == 6, "a colony that offered it a fish today keeps all six");
        auto v = fresh();
        v->eco.AddBlood({c.x, -0.5f, c.z}, 40);
        run(*v, 2);
        check(v->kraken.mood >= 1, TextFormat("blood in the cove wakes it (mood %d)", v->kraken.mood));
        // a fight: a flock over the cove while it's surfaced wounds it; enough of them kill it
        auto k = fresh(); k->kraken.mood = 2; k->kraken.hp = 400; k->col.offeredKraken = 0; Feed(*k, 0, 30);
        std::vector<int> war; for (int i = 0; i < 10; i++) war.push_back(Adult(*k, 0, i < 5 ? Role::Striker : Role::Tank, {c.x, 12, c.z + i}).id);
        int f = k->MakeFlock(0, war, Formation::Chevron, Alt::Low, Stance::Raid);
        k->OrderFlock(0, f, Target::Point, -1, -1, -1, -1, c);
        for (float t = 0; t < 120 && !k->kraken.dead; t += 0.1f) { k->kraken.mood = 2; k->Step(0.1f, FounderInput{k->me.yaw}); }
        check(k->kraken.dead && k->kraken.killedBy == 0 && k->col.krakenKill && k->Score(0).kraken == 150, "a flock kills the wounded kraken: 150 to the score and its hoard of pearls");
    }
    // the ape on skull island
    {
        auto w = fresh();
        int sk = w->ape.isle;
        check(sk >= 0, "skull island has its great ape");
        if (sk >= 0) {
            Vector3 a = w->ape.pos;
            std::vector<int> ids; for (int k = 0; k < 4; k++) { Bird& b = Adult(*w, 0, Role::Fisher, {a.x + 60, 12, a.z + k * 4.0f}); b.vel = {1, 0, 0}; ids.push_back(b.id); }
            int hit = 0; for (float t = 0; t < 60; t += 0.1f) { for (int id : ids) if (Bird* b = w->FindBird(0, id)) { b->pos = {a.x + 60, 12, a.z}; b->vel = {1, 0, 0}; b->task = Task::Fly; } w->Step(0.1f, FounderInput{w->me.yaw}); }
            for (int id : ids) hit += !w->FindBird(0, id);
            check(hit >= 1, TextFormat("it throws rocks at low flyers: %d of 4 struck down in a minute", hit));
            w->me.st = FState::Perched; w->me.pos = a; w->me.carrySp = w->eco.map->SpeciesIndex("Tuna"); w->me.carrySize = 4;
            check(w->FeedApe() && w->ape.sleepT > 0, "fed a tuna, it sleeps for a day");
        }
    }
    // the volcano
    {
        auto w = fresh();
        int vi = w->volcano.isle;
        check(vi >= 0, "the volcano is on the map");
        if (vi >= 0) {
            Outpost(*w, 0, vi, 4);
            int chicks = 0;
            for (int ni = 0; ni < (int)w->col.nests.size(); ni++) if (w->col.nests[ni].isle == vi) { Bird c; c.id = w->col.nextId++; c.stage = BStage::Chick; c.nest = ni; c.pos = w->col.nests[ni].pos; c.hunger = 1; w->col.birds.push_back(c); chicks++; }
            w->time = w->volcano.next - 1; w->weather.next = 1e9f;   // (no storm in the way)
            int nests0 = w->NestsOn(0, vi);
            run(*w, 2);
            int alive = 0; for (const auto& b : w->col.birds) alive += b.alive && b.stage == BStage::Chick;
            check(w->volcano.eruptions == 1 && alive < chicks, TextFormat("it erupts: %d of %d chicks on it die in the ash", chicks - alive, chicks));
            check(w->NestsOn(0, vi) < nests0, TextFormat("lava takes nests on its slopes (%d of %d left)", w->NestsOn(0, vi), nests0));
            Vector3 far = Vector3Add(w->isles[vi].c, {1200, 0, 0});
            check(w->Grounded(w->isles[vi].c) && !w->Grounded(far), "its ash grounds the birds near it for a day");
        }
    }
    // the wreck and the weather
    {
        auto w = fresh();
        int wi = w->wreck.isle;
        Vector3 c0 = w->isles[wi].c;
        w->time = World::DAY * 5; w->SetWreckPose();
        check(Vector3Distance(w->isles[wi].c, c0) > 50 && w->isles[wi].drop > 1, TextFormat("the wreck drifts (%.0f m in five days) and sinks (%.1f m)", Vector3Distance(w->isles[wi].c, c0), w->isles[wi].drop));
        w->time = World::DAY * 11; w->SetWreckPose();
        check(w->wreck.gone, "by the late game it's gone");
        auto s = fresh();
        s->weather.next = s->time + 0.5f;
        run(*s, 1);
        check(s->StormNow() && s->Grounded(s->me.pos), "a storm grounds every bird");
    }
    // bombs and the Works
    {
        auto w = fresh();
        w->col.tier[(int)Tree::Bombing] = 2;
        Structure wk; wk.kind = ST_WORKS; wk.built = true; wk.pos = w->GroundAt(w->col.caches[0].pos.x - 6, w->col.caches[0].pos.z); w->col.builds.push_back(wk);
        w->col.guano = 30; w->col.sulfur = 3;
        run(*w, World::DAY * 1.05f);
        check(w->col.bombs >= 1, TextFormat("the Works make a bomb a day from guano and sulfur (%d)", w->col.bombs));
        Colony& R = w->ColOf(1);
        Structure h; h.kind = ST_HEDGE; h.built = true; h.hp = StructureHp(ST_HEDGE); h.pos = R.caches[0].pos; R.builds.push_back(h);
        Vector3 at = R.nests[0].pos;
        Adult(*w, 1, Role::Fisher, at);
        int built0 = 0; for (const auto& n : R.nests) built0 += n.built;
        w->Blast(at, 0, 1);
        int built1 = 0; for (const auto& n : R.nests) built1 += n.built;
        bool hedge = false; for (const auto& s2 : R.builds) hedge |= s2.kind == ST_HEDGE && Vector3Distance(s2.pos, at) < 12;
        check(built1 < built0, "an incendiary on their nest destroys it");
        check(!hedge || Vector3Distance(R.caches[0].pos, at) > 12, "and burns a hedge in its reach");
        Structure ro; ro.kind = ST_ROOST; ro.built = true; ro.hp = StructureHp(ST_ROOST); ro.pos = R.caches[0].pos; R.builds.push_back(ro);
        w->Blast(R.caches[0].pos, 0, 0);
        bool roost = false; for (const auto& s2 : R.builds) roost |= s2.kind == ST_ROOST;
        w->Blast(R.caches[0].pos, 0, 2);
        bool roost2 = false; for (const auto& s2 : R.builds) roost2 |= s2.kind == ST_ROOST;
        check(roost && !roost2, "a plain bomb doesn't fell a Roost; a blockbuster does");
    }
    // stimulants
    {
        auto w = fresh();
        w->col.tier[(int)Tree::Chemistry] = 4; w->col.stims[STIM_HASTE] = 1; w->col.stims[STIM_DRAUGHT] = 1;
        Feed(*w, 0, 20);
        auto make = [&](float x) { std::vector<int> ids; for (int k = 0; k < 4; k++) ids.push_back(Adult(*w, 0, Role::Skirmisher, {x, 40, -300.0f + k * 3}).id); return w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid); };
        int a = make(0), b = make(40);
        check(w->Dose(a, STIM_HASTE) && !w->Dose(a, STIM_DRAUGHT), "a flock dosed with Haste (one stimulant at a time)");
        w->OrderFlock(0, a, Target::Point, -1, -1, -1, -1, {0, 0, 900}); w->OrderFlock(0, b, Target::Point, -1, -1, -1, -1, {40, 0, 900});
        Vector3 pa = w->FindFlock(0, a)->pos, pb = w->FindFlock(0, b)->pos;
        run(*w, 8, 0.05f);
        float da = Vector3Distance(w->FindFlock(0, a)->pos, pa), db = Vector3Distance(w->FindFlock(0, b)->pos, pb);
        check(da > db * 1.2f, TextFormat("Haste: %.0f m against %.0f m undosed", da, db));
        check(w->Dose(b, STIM_DRAUGHT) && w->Morale(0, *w->FindFlock(0, b)) == 100, "the Draught: immune to morale");
        w->FindFlock(0, b)->stimT = 0.01f;
        run(*w, 0.5f, 0.05f);
        int asleep = 0; if (Flock* f = w->FindFlock(0, b)) for (int id : f->members) if (Bird* bd = w->FindBird(0, id)) asleep += bd->netT > World::DAY * 0.5f;
        check(asleep == 4, "and when it wears off, the whole flock sleeps where it is");
    }
    // sieges and assaults
    {
        auto w = fresh();
        Colony& B = w->ColOf(1);
        B.tier[(int)Tree::War] = 4;
        int zone = w->eco.ZoneAt({w->isles[w->home].c.x, -2, w->isles[w->home].c.z + 70});
        if (zone < 0) zone = w->inshoreZone;
        std::vector<int> ids; for (int k = 0; k < 3; k++) ids.push_back(Adult(*w, 1, Role::Tank, Vector3Add(w->eco.map->zones[zone].Center(), {(float)k, 15, 0})).id);
        int f = w->MakeFlock(1, ids, Formation::Wall, Alt::Mid, Stance::Hold);
        w->OrderFlock(1, f, Target::Ground, -1, -1, zone, -1, w->eco.map->zones[zone].Center());
        w->FindFlock(1, f)->pos = w->eco.map->zones[zone].Center();
        check(w->Blockaded(zone, 0) && !w->Blockaded(zone, 1), "a rival's Hold flock on your ground (War 4) blockades it: your fishers won't work it");
        w->col.ground = -1;
        bool chosen = false; for (int k = 0; k < 20; k++) { Bird& b = Adult(*w, 0, Role::Fisher, w->col.caches[0].pos); (void)b; }
        run(*w, 3);
        for (const auto& b : w->col.birds) if (b.alive && b.role == Role::Fisher && w->eco.ZoneAt({b.goal.x, -1, b.goal.z}) == zone && b.task == Task::Search) chosen = true;
        check(!chosen, "(none of twenty fishers works the blockaded ground)");
        // an assault: Strikers tear down an unguarded empty nest on an outpost, and the island changes hands
        int islet = IsleOf(*w, IsleType::Islet);
        Outpost(*w, 1, islet, 1); Outpost(*w, 0, islet, 1);
        Vector3 nestAt{}; for (const auto& n : w->ColOf(1).nests) if (n.isle == islet && n.built) nestAt = n.pos;
        std::vector<int> st; for (int k = 0; k < 3; k++) st.push_back(Adult(*w, 0, Role::Striker, Vector3Add(nestAt, {-20, 10, 0})).id);
        Feed(*w, 0, 20);
        int fs = w->MakeFlock(0, st, Formation::Chevron, Alt::Low, Stance::Raid);
        w->OrderFlock(0, fs, Target::Nests, 1, islet, -1, -1, nestAt);
        for (float t = 0; t < 60 && w->NestsOn(1, islet) > 0; t += 0.05f) w->Step(0.05f, FounderInput{w->me.yaw});
        check(w->NestsOn(1, islet) == 0 && w->HolderOf(islet) == 0 && w->col.nestsDestroyed >= 1, "an assault's Strikers tear down the rival's nest on the islet: it's yours");
        // starving warriors desert
        auto d = fresh();
        d->col.caches[0].fish.clear();
        for (int k = 0; k < 3; k++) { Bird& b = Adult(*d, 0, Role::Tank, d->col.caches[0].pos); b.hunger = 0.1f; }
        for (int k = 0; k < 3; k++) Adult(*d, 0, Role::Fisher, d->col.caches[0].pos).hunger = 0.1f;
        run(*d, 2);
        int tanks = d->Count(BStage::Adult, Role::Tank);
        check(tanks == 0, TextFormat("a starving colony's warriors desert to fish (%d Tanks left of 3)", tanks));
    }
    printf(fails ? "flight-danger-test: %d check(s) failed\n" : "flight-danger-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --flight-siege: the stage-7 gate
// A bot takes the Kraken Cove and holds it through a siege (doc p34). A strong bot colony (side 1) is sent to the cove:
// its Founder founds the outpost, its builders raise the nests, its priests keep the kraken fed. Then another bot (side
// 2, War 4, warriors, bombs) besieges it: assaults on the cove's nests and a blockade of the cove's water. The gate:
// after three days of siege, side 1 still holds the cove.
int RunFlightSiege(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int runs = argc > 2 ? std::max(1, atoi(argv[2])) : 3;
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    printf("The Flight, stage 7 gate: a bot takes the cove and holds it through a siege (%d runs)\n", runs);
    int held = 0, taken = 0;
    for (int r = 0; r < runs; r++) {
        auto w = std::make_unique<World>();
        MapOpts o; o.players = 3; o.arr = Arrangement::Archipelago;
        w->Init("taloned", 701 + r * 37, o);
        w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0}); w->me.hunger = 1;
        int cove = w->kraken.isle;
        // side 1, the holder: a grown colony (day 5 as a careful bot would have it): fishers, builders, feeders, Watchers,
        // warriors, two priests and a shrine, its research, its stores
        auto grow = [&](int side, int fishers, int builders, int watchers, int strikers, int skirm, int tanks, int priests) {
            w->WithSide(side, [&] {
                Vector3 h = w->col.caches[0].pos;
                auto add = [&](Role role, int n) { for (int k = 0; k < n; k++) { Bird b; b.id = w->col.nextId++; b.stage = BStage::Adult; b.role = role; b.hp = w->MaxHp(role); b.fight = 25; b.hunger = 1; b.pos = Vector3Add(h, {(float)(k % 5), 3, (float)(k / 5)}); w->col.birds.push_back(b); } };
                add(Role::Fisher, fishers); add(Role::Builder, builders); add(Role::Feeder, 3); add(Role::Watcher, watchers);
                add(Role::Striker, strikers); add(Role::Skirmisher, skirm); add(Role::Tank, tanks); add(Role::Priest, priests);
                for (int k = 0; k < 60; k++) w->col.caches[0].fish.push_back({w->eco.map->SpeciesIndex("Mullet"), 2, 0});
                w->col.twigs = 80; w->col.shells = 60; w->col.pearls = 10;
                w->col.tier[(int)Tree::War] = 4; w->col.tier[(int)Tree::Faith] = 1; w->col.tier[(int)Tree::Caches] = 2; w->col.tier[(int)Tree::Nesting] = 2;
                Structure sh; sh.kind = ST_SHRINE; sh.built = true; sh.pos = w->GroundAt(h.x + 4, h.z - 5); sh.isle = w->home; w->col.builds.push_back(sh);
                Structure ro; ro.kind = ST_ROOST; ro.built = true; ro.pos = w->GroundAt(h.x - 5, h.z + 3); ro.isle = w->home; w->col.builds.push_back(ro);
                w->time = std::max(w->time, World::DAY * 5);
            });
        };
        grow(1, 16, 5, 4, 2, 2, 2, 2);
        // ---- phase 1: it takes the cove
        w->ColOf(1).expandTo = cove;
        float t1 = 0;
        for (; t1 < World::DAY * 4 && !(w->HolderOf(cove) == 1 && w->NestsOn(1, cove) >= 2); t1 += 0.1f) {
            w->Step(0.1f, FounderInput{});
            if (getenv("DEPTH_SIEGETRACE") && fmodf(t1, World::DAY * 0.25f) < 0.1f) {
                const Colony& C = w->ColOf(1); int laid = 0, built = 0, caches = 0, cachesB = 0; for (const auto& n : C.nests) if (n.isle == cove) { laid++; built += n.built; } for (const auto& c : C.caches) { caches++; cachesB += c.built; }
                const Founder& F = w->FounderOf(1);
                printf("    [%.2f d] expandTo %d; cove nests %d laid %d built; caches %d/%d; twigs %d; builders %d; founder %.0f m from the cove; nests wanted %d of %d\n", t1 / World::DAY, C.expandTo, laid, built, cachesB, caches, C.twigs,
                       (int)std::count_if(C.birds.begin(), C.birds.end(), [](const Bird& b) { return b.alive && b.role == Role::Builder; }), Vector2Distance({F.pos.x, F.pos.z}, {w->isles[cove].c.x, w->isles[cove].c.z}), C.nestsWanted, (int)C.nests.size());
            }
        }
        if (getenv("DEPTH_SIEGETRACE")) { std::string d; for (const auto& x : w->ColOf(1).deaths) d += TextFormat(" %s %d;", x.first.c_str(), x.second); printf("    side 1 deaths:%s\n", d.c_str()); }
        bool tookIt = w->HolderOf(cove) == 1;
        taken += tookIt;
        printf("  run %d: side 1 %s the cove in %.1f days (%d nests there)\n", r + 1, tookIt ? "takes" : "fails to take", t1 / World::DAY, w->NestsOn(1, cove));
        if (!tookIt) continue;
        // ---- phase 2: side 2 besieges it
        grow(2, 14, 3, 1, 5, 4, 4, 0);
        { Colony& S = w->ColOf(2); S.tier[(int)Tree::Bombing] = 2; S.tier[(int)Tree::Chemistry] = 3; S.stims[STIM_FURY] = 2; S.stims[STIM_HASTE] = 2; S.bombs = 2;
          w->WithSide(2, [&] { Vector3 h = w->col.caches[0].pos; Structure wk; wk.kind = ST_WORKS; wk.built = true; wk.pos = w->GroundAt(h.x - 6, h.z - 4); wk.isle = w->home; w->col.builds.push_back(wk);
                               for (int k = 0; k < 2; k++) { Bird b; b.id = w->col.nextId++; b.stage = BStage::Adult; b.role = Role::Bomber; b.hp = w->MaxHp(Role::Bomber); b.fight = 25; b.hunger = 1; b.pos = h; w->col.birds.push_back(b); } }); }
        int sieges = 0, lostNests = 0, minNests = 99; float t2 = 0; std::vector<int> assaultIds; int losses0 = w->ColOf(1).losses, kills0 = w->ColOf(1).kills;
        int destroyed0 = w->ColOf(2).nestsDestroyed;
        for (; t2 < World::DAY * 3; t2 += 0.1f) {
            w->Step(0.1f, FounderInput{});
            for (const auto& f : w->ColOf(2).flocks) if (f.tIsle == cove || (f.target == Target::Ground && f.stance == Stance::Hold)) { if (std::find(assaultIds.begin(), assaultIds.end(), f.id) == assaultIds.end()) assaultIds.push_back(f.id); if (f.engagedT > 0 && fmodf(t2, 20.0f) < 0.1f) sieges++; }
            minNests = std::min(minNests, w->NestsOn(1, cove));
        }
        lostNests = w->ColOf(2).nestsDestroyed - destroyed0;
        bool holds = w->HolderOf(cove) == 1;
        held += holds;
        int assaults = 0; for (const auto& line : w->warLog) assaults += line.find("Assault") != std::string::npos;
        printf("        under siege 3 days: side 2 flew %d assault and blockade flocks (fighting in %d of the 20 s checks); side 1 lost %d birds and killed %d; %d nests torn down (side 1 at %d at worst)\n",
               (int)assaultIds.size(), sieges, w->ColOf(1).losses - losses0, w->ColOf(1).kills - kills0, lostNests, minNests);
        printf("        side 1 %s the cove with %d nests (side 2: %d); kraken %s\n", holds ? "still holds" : "has LOST", w->NestsOn(1, cove), w->NestsOn(2, cove), w->kraken.dead ? "dead" : w->kraken.mood == 2 ? "surfaced" : w->kraken.mood == 1 ? "awake" : "asleep");
        (void)sieges; (void)assaults;
    }
    bool ok = taken == runs && held * 3 >= runs * 2;
    printf("  %s  GATE: a bot takes the cove in %d of %d runs and holds it through the siege in %d\n", ok ? "ok  " : "FAIL", taken, runs, held);
    return ok ? 0 : 1;
}

}  // namespace fl
