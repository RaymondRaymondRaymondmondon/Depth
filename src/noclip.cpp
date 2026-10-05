// NOCLIP's headless core (see noclip.h): the data, the campaign and the day, the crew (meters, injuries, carrying,
// tools), loot, Threshold Labs (restart, the crate, the portal, the network, jumps, breaches), moving between levels,
// and the Surface (the Bureau, the Fence, the quota, contracts). Entities are noclip_ents.cpp; bots and tests
// noclip_bots.cpp; the generators noclip_gen.cpp.
#include "noclip.h"
#include "json.h"
#include "redtide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>

namespace nc {

// ---------------------------------------------------------------- data
static Color Col(const Json& a, Color def) { if (a.type != Json::Arr || a.a.size() < 3) return def; return {(unsigned char)a[0].I(), (unsigned char)a[1].I(), (unsigned char)a[2].I(), 255}; }
static Data Load() {
    Data d; std::string dir = rt::DataDir() + "/../noclip/";
    Json lv = LoadJsonFile(dir + "noclip_levels.json");
    for (const auto& e : lv["levels"].a) {
        LevelDef L; L.id = e["id"].I(0); L.name = e["name"].Str0(""); L.kit = e["kit"].Str0("maze"); L.mood = e["mood"].Str0(""); L.w = e["w"].I(60); L.h = e["h"].I(60); L.ceil = e["ceil"].F(2.6f);
        L.wall = Col(e["wall"], GRAY); L.floor = Col(e["floor"], DARKGRAY); L.top = Col(e["top"], LIGHTGRAY); L.light = Col(e["light"], WHITE); L.lit = e["lit"].F(0.9f); L.flicker = e["flicker"].F(0.05f);
        for (int k = 0; k < 3; k++) L.tex[k] = e["tex"][k].Str0("concrete");
        for (const auto& x : e["entities"].a) L.entities.push_back({x[0].Str0(""), x[1].F(1)});
        L.budget = e["budget"].F(2); L.lootBand = e["loot"].I(L.id); L.startOnline = e["startOnline"].Bool0(false); L.compass = e["compass"].Bool0(false);
        for (const auto& x : e["labs"].a) L.labs.push_back(x.Str0("Lab"));
        for (const auto& x : e["exits"].a) { ExitDef ex; ex.to = x["to"].I(0); ex.kind = x["kind"].Str0("door"); ex.label = x["label"].Str0(""); ex.chance = x["chance"].F(1); L.exits.push_back(ex); }
        d.levels.push_back(L);
    }
    Json lo = LoadJsonFile(dir + "noclip_loot.json");
    for (const auto& b : lo["bands"].a) { int lvl = b["level"].I(0); for (const auto& it : b["items"].a) { LootDef x; x.name = it[0].Str0(""); x.size = it[1].Str0("s"); x.min = it[2].I(5); x.max = it[3].I(40); x.props = it[4].Str0(""); x.note = it[5].Str0(""); x.level = lvl; d.loot.push_back(x); } }
    for (const auto& it : lo["anomalous"].a) { LootDef x; x.name = it["name"].Str0(""); x.size = it["size"].Str0("s"); x.min = it["min"].I(200); x.max = it["max"].I(400); x.anomalous = true; x.effect = it["effect"].Str0(""); x.props = "anomalous"; x.level = -1; d.loot.push_back(x); }
    d.anomalousChance = lo["anomalousChance"].F(0.02f);
    Json it = LoadJsonFile(dir + "noclip_items.json");
    for (const auto& e : it["items"].a) { ItemDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(x.id); x.use = e["use"].Str0(""); x.note = e["note"].Str0(""); x.price = e["price"].I(10); x.charges = e["charges"].I(1); d.items.push_back(x); }
    for (const auto& e : it["suits"].a) { SuitDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(""); x.note = e["note"].Str0(""); x.price = e["price"].I(100); d.suits.push_back(x); }
    for (const auto& e : it["labUpgrades"].a) { SuitDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(""); x.note = e["note"].Str0(""); x.price = e["price"].I(100); d.labUps.push_back(x); }
    for (const auto& v : it["labPriceMul"].a) d.labPriceMul.push_back(v.F(1));
    Json en = LoadJsonFile(dir + "noclip_entities.json");
    for (const auto& e : en["entities"].a) { EntityDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(""); x.hunts = e["hunts"].Str0(""); x.behaviour = e["behaviour"].Str0("wander"); x.tell = e["tell"].Str0(""); x.counter = e["counter"].Str0(""); x.walk = e["speed"][0].F(1); x.chase = e["speed"][1].F(4); x.damage = e["damage"].I(20); x.pack = e["pack"].I(1); x.bounty = e["bounty"].I(50); d.entities.push_back(x); }
    Json co = LoadJsonFile(dir + "noclip_contracts.json");
    for (const auto& e : co["contracts"].a) { ContractDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(""); x.task = e["task"].Str0(""); x.creditMin = e["credit"][0].I(0); x.creditMax = e["credit"][1].I(0); x.cash = e["cash"].I(0); d.contracts.push_back(x); }
    const Json& q = co["quota"]; d.week1 = q["week1"].I(600); d.growth = q["growth"].F(0.4f); d.growthLate = q["growthLate"].F(0.35f); d.lateFrom = q["lateFrom"].I(5); d.daysPerWeek = q["daysPerWeek"].I(3); d.bureauCash = q["bureauCashShare"].F(0.2f); d.deathFee = q["deathFee"].F(0.15f); d.startCash = q["startCash"].I(120);
    const Json& dy = co["day"]; d.dayStart = dy["start"].F(360); d.dayEnd = dy["end"].F(1440); d.dayReal = dy["realSeconds"].F(900); d.evening = dy["evening"].F(1080);
    for (const auto& r : co["ranks"].a) d.ranks.push_back({r[0].Str0(""), r[1].I(0)});
    d.tokDay = co["tokens"]["day"].I(10); d.tokQuota = co["tokens"]["quota"].I(50); d.tokEntity = co["tokens"]["entity"].I(25);
    Json cs = LoadJsonFile(dir + "noclip_cosmetics.json");
    for (const auto& e : cs["cosmetics"].a) { CosDef x; x.id = e["id"].Str0(""); x.name = e["name"].Str0(""); x.kind = e["kind"].Str0(""); x.tier = e["tier"].I(0); x.glow = e["glow"].Bool0(false); d.cosmetics.push_back(x); }
    for (int k = 0; k < 4; k++) { d.cosPrice[k] = cs["prices"][k].I(d.cosPrice[k]); d.crateW[k] = cs["crateWeights"][k].I(d.crateW[k]); } d.cratePrice = cs["crate"].I(1);
    if (d.levels.empty()) { LevelDef L; L.name = "The Lobby"; L.labs = {"Alpha"}; L.startOnline = true; d.levels.push_back(L); }
    while (d.labPriceMul.size() < d.levels.size()) d.labPriceMul.push_back(1);
    return d;
}
const Data& D() { static Data d = Load(); return d; }
int ItemIndex(const std::string& id) { for (int i = 0; i < (int)D().items.size(); i++) if (D().items[i].id == id) return i; return -1; }
int EntityIndex(const std::string& id) { for (int i = 0; i < (int)D().entities.size(); i++) if (D().entities[i].id == id) return i; return -1; }
int ContractIndex(const std::string& id) { for (int i = 0; i < (int)D().contracts.size(); i++) if (D().contracts[i].id == id) return i; return -1; }
static Size SizeOf(const LootDef& l) { return l.size == "m" ? SZ_M : l.size == "l" ? SZ_L : l.size == "h" ? SZ_H : SZ_S; }
static bool Has(const LootDef& l, const char* prop) { return l.props.find(prop) != std::string::npos; }

float Player::SpeedMul() const {
    float m = 1;
    if (hands.def >= 0) { Size s = SizeOf(D().loot[hands.def]); m *= s == SZ_M ? 0.85f : s == SZ_L ? 0.7f : s == SZ_H ? 0.6f : 1.0f; }
    if (injuries & IN_BREAK) m *= 0.5f;
    return m;
}

// ---------------------------------------------------------------- the world
float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
int World::QuotaFor(int wk) const { float q = (float)D().week1; for (int k = 2; k <= wk; k++) q *= 1 + (k >= D().lateFrom ? D().growthLate : D().growth); return (int)roundf(q / 10) * 10; }
std::string World::ClockText() const { int m = (int)clock % 1440; char s[16]; snprintf(s, sizeof s, "%02d:%02d", m / 60, m % 60); return s; }
void World::Init(int humans, int bots, int md, uint32_t seed) {
    rng = seed ? seed * 2654435761u + 5 : 9; for (int i = 0; i < 3; i++) Rand();
    mode = md; crew.clear(); levels.clear(); labs.clear(); items.clear(); ents.clear(); events.clear(); evCount = 0;
    int n = std::clamp(humans + bots, 1, MAX_CREW);
    for (int i = 0; i < n; i++) { Player p; p.id = i; p.bot = i >= humans; p.name = "Salvager " + std::to_string(i + 1); crew.push_back(p); }
    // every level's Labs, dormant except Lab Alpha
    for (const auto& L : D().levels) for (int k = 0; k < (int)L.labs.size(); k++) { LabState s; s.level = L.id; s.idx = k; s.online = L.startOnline && k == 0; s.doorOpen = s.online; s.fuel = s.online ? 99 : 0; labs.push_back(s); }
    week = 1; day = 1; quota = QuotaFor(1); credit = 0; cash = D().startCash; failed = false; weeksSurvived = 0;
    seen.assign(LEVELS, 0); dossier = 0; learned.clear(); sold.clear(); bay.clear(); inDay = false; insertion = 0;
    // the starting kit: a battery each, and the crew's first chalk and almond water
    int bat = ItemIndex("battery"), alm = ItemIndex("almond"), ch = ItemIndex("chalk");
    for (auto& p : crew) { if (bat >= 0) p.tools[0] = {bat, 1, 0}; if (alm >= 0) p.tools[1] = {alm, 1, 0}; if (ch >= 0 && p.id == 0) p.tools[2] = {ch, D().items[ch].charges, 0}; }
    daySeed = rng; EndDay(false); day = 1; memo = "Welcome to the Liminal Recovery Bureau. Your first quota is " + std::to_string(quota) + ". The portal is through the door.";
    won = false; score = 0; marks.clear();
    // the modes (design doc p. 35)
    if (mode == 2) { quota = (int)(quota * 0.6f); memo = "Noclip Roulette: every exit is a noclip somewhere. The quota is low. Good luck."; }
    if (mode == 3) memo = "Lights Out: every level plays by Level 6's rule today.";
    if (mode == 4) { memo = "Expedition: one long day (60 minutes), no shop in the field, scored on what you bring back."; }
    if (mode == 5) memo = "Lonely: it's just you. Fewer entities. More of the other thing.";
    if (mode == 6 && humans >= 3) { int k = RandI(humans); crew[k].impostor = true; memo = "One of you isn't one of you. Extract and leave it behind."; }
    if (mode == 1) {   // Lost: no Surface, nothing in the pockets, Lab Alpha dormant; find a Lab and restart it to escape. One life.
        for (auto& l : labs) { l.online = false; l.doorOpen = false; l.fuel = 0; }
        for (auto& p : crew) for (auto& t : p.tools) t = Tool{};
        memo = "Lost: you're in the Lobby with nothing. Find a Lab, restart it, and get out. One life each.";
        BeginDay(); int wl = 1 + RandI(4); Level& lv = L(wl); int k = 0;
        // wake on a deeper level, as far from every Lab as the Lobby allows (a reachable cell: a path to Lab Alpha's door)
        Vector3 wake = lv.start; float bestD = -1; for (int t = 0; t < 400; t++) { int x = RandI(lv.w), z = RandI(lv.h); if (!lv.Walkable(x, z) || lv.At(x, z) == T_PIT || lv.At(x, z) == T_DEEP || lv.At(x, z) == T_LABFLOOR) continue; Vector3 c = lv.Center(x, z); float md = 1e9f; for (const auto& lp : lv.labs) md = std::min(md, Vector2Distance({c.x, c.z}, {lv.Center(lp.doorX, lp.doorZ).x, lv.Center(lp.doorX, lp.doorZ).z}));
            if (md > bestD && !lv.labs.empty() && !Path(wl, c, lv.Center(lv.labs[0].doorX, lv.labs[0].doorZ), 20000).empty()) { bestD = md; wake = c; } }
        for (auto& p : crew) { p.level = wl; p.p = Vector3Add(wake, {(float)(k % 3) * 0.6f, 0, (float)(k / 3) * 0.6f}); if (lv.Solid(lv.CellX(p.p.x), lv.CellZ(p.p.z))) p.p = wake; k++; }
        int fuel = ItemIndex("fuel"); if (fuel >= 0) crew[0].tools[0] = {fuel, 1, 0};   // (one canister between you: the Bureau's last kindness)
    }
}
Level& World::L(int level) {
    auto it = levels.find(level);
    if (it != levels.end()) return it->second;
    Level& lv = levels[level] = Generate(level, daySeed);
    if (mirror) return lv;
    // the day's loot: rolled from the level's band (and now and then an anomalous object)
    int band = D().levels[level].lootBand; std::vector<int> pool; for (int i = 0; i < (int)D().loot.size(); i++) if (D().loot[i].level == band) pool.push_back(i);
    std::vector<int> anom; for (int i = 0; i < (int)D().loot.size(); i++) if (D().loot[i].anomalous) anom.push_back(i);
    for (const auto& sp : lv.loot) {
        if (pool.empty()) break;
        int def = Rand() < D().anomalousChance && !anom.empty() ? anom[RandI((int)anom.size())] : pool[RandI((int)pool.size())];
        // (huge things are rarer)
        if (SizeOf(D().loot[def]) == SZ_H && Rand() < 0.6f) def = pool[RandI((int)pool.size())];
        WorldItem wi; wi.level = level; wi.p = sp.at; wi.loot.def = def; const LootDef& ld = D().loot[def]; wi.loot.value = ld.min + RandI(ld.max - ld.min + 1); wi.loot.foundOn = level; wi.loot.uid = nextUid++;
        items.push_back(wi);
    }
    // a fuel canister and a few supplies lie about most levels (the restart's fuel)
    int fuel = ItemIndex("fuel"); (void)fuel;
    return lv;
}
LabState* World::Lab(int level, int idx) { for (auto& l : labs) if (l.level == level && l.idx == idx) return &l; return nullptr; }
LabState* World::LabAt(int level, Vector3 p) {
    Level& lv = L(level); int x = lv.CellX(p.x), z = lv.CellZ(p.z);
    for (int k = 0; k < (int)lv.labs.size(); k++) { const LabPlan& lp = lv.labs[k]; if (x >= lp.x0 && x <= lp.x1 && z >= lp.z0 && z <= lp.z1) return Lab(level, k); }
    return nullptr;
}
bool World::InPortalHall(const Player& pl, const LabState& lab) {
    if (pl.level != lab.level || !pl.Alive()) return false;
    LabState* at = LabAt(pl.level, pl.p); return at == &lab;
}
int World::CrateTotal() const { int t = 0; for (const auto& l : labs) if (l.online && l.fuel > 0) for (const auto& c : l.crate) t += c.value; return t; }
int World::Nearest(int level, Vector3 at, float range, int except) const {
    int best = -1; float bd = range;
    for (const auto& p : crew) if (p.id != except && p.level == level && p.st == PS_ALIVE) { float d = Vector2Distance({p.p.x, p.p.z}, {at.x, at.z}); if (d < bd) { bd = d; best = p.id; } }
    return best;
}
bool World::LineOfSight(int level, Vector3 a, Vector3 b) const {
    auto it = levels.find(level); if (it == levels.end()) return false; const Level& lv = it->second;
    float d = Vector2Distance({a.x, a.z}, {b.x, b.z}); int n = std::max(1, (int)(d / (CELL * 0.4f)));
    int wheat = 0;
    for (int k = 1; k < n; k++) { float u = (float)k / n; float x = a.x + (b.x - a.x) * u, z = a.z + (b.z - a.z) * u; int cx = (int)floorf(x / CELL), cz = (int)floorf(z / CELL); if (!lv.SeeThrough(cx, cz)) return false; if (lv.At(cx, cz) == T_WHEAT && ++wheat > 2) return false; }
    return true;
}
float World::LightAt(int level, Vector3 at) const {
    auto it = levels.find(level); if (it == levels.end()) return 0; const Level& lv = it->second;
    int cx = lv.CellX(at.x), cz = lv.CellZ(at.z); float best = 0;
    bool out = overtime || (level == 1 && lightsOutT > 0) || mode == 3;
    if (!out) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= lv.w || z >= lv.h) continue; uint8_t l = lv.light[z * lv.w + x]; if (l) best = std::max(best, (l >= 128 && l < 255 ? 0.6f : 1.0f) * (dx || dz ? 0.6f : 1.0f)); }
    if (level == 10 || level == 11) best = std::max(best, overtime ? 0.1f : 0.8f);   // (the open sky)
    // a Lab with its generator running is lit
    for (int k = 0; k < (int)lv.labs.size(); k++) { const LabPlan& lp = lv.labs[k]; if (cx >= lp.x0 && cx <= lp.x1 && cz >= lp.z0 && cz <= lp.z1) { for (const auto& l : labs) if (l.level == level && l.idx == k && l.online && l.fuel > 0) best = 1; } }
    // the crew's lamps, and lights they carry or dropped
    for (const auto& p : crew) if (p.level == level && p.Alive()) { float d = Vector2Distance({p.p.x, p.p.z}, {at.x, at.z}); if (p.lamp && p.battery > 0 && d < 7) best = std::max(best, 0.7f * (1 - d / 7)); }
    return std::clamp(best, 0.0f, 1.0f);
}
std::vector<int> World::Path(int level, Vector3 from, Vector3 to, int maxNodes) const {
    auto it = levels.find(level); if (it == levels.end()) return {}; const Level& lv = it->second;
    int sx = lv.CellX(from.x), sz = lv.CellZ(from.z), tx = lv.CellX(to.x), tz = lv.CellZ(to.z);
    if (sx < 0 || sz < 0 || sx >= lv.w || sz >= lv.h || tx < 0 || tz < 0 || tx >= lv.w || tz >= lv.h) return {};
    std::vector<int> prev(lv.w * lv.h, -2); std::queue<int> q; int s = sz * lv.w + sx, t = tz * lv.w + tx; prev[s] = -1; q.push(s); int n = 0;
    while (!q.empty() && n++ < maxNodes) { int c = q.front(); q.pop(); if (c == t) break; int x = c % lv.w, z = c / lv.w; const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& d : D4) { int nx = x + d[0], nz = z + d[1]; if (nx < 0 || nz < 0 || nx >= lv.w || nz >= lv.h) continue; int ni = nz * lv.w + nx; if (prev[ni] != -2 || !lv.Walkable(nx, nz) || lv.At(nx, nz) == T_PIT) continue; prev[ni] = c; q.push(ni); } }
    if (prev[t] == -2) return {};
    std::vector<int> path; for (int c = t; c != -1; c = prev[c]) path.push_back(c); std::reverse(path.begin(), path.end()); return path;
}

// ---------------------------------------------------------------- the day
void World::BeginDay() {
    inDay = true; clock = D().dayStart; overtime = false; hourT = 0; levels.clear(); items.clear(); ents.clear(); seenCells.clear(); lightsOutT = 0; lockdownT = 0; lockdownNext = 120;
    marks.clear(); trapRooms.clear();
    LabState* ins = insertion >= 0 && insertion < (int)labs.size() ? &labs[insertion] : nullptr;
    if (!ins || (!ins->online && mode != 1)) { ins = &labs[0]; insertion = 0; }
    Level& lv = L(ins->level); const LabPlan& lp = lv.labs[ins->idx];
    int k = 0;
    for (auto& p : crew) {
        if (!p.present) continue;
        p.st = PS_ALIVE; p.level = ins->level; Vector3 ring = lp.spots[0].at; p.p = {ring.x + (k % 3 - 1) * 1.2f, 0, ring.z + (k / 3 - 0.5f) * 1.2f}; k++;
        p.health = 100; p.stamina = 100; p.sanity = std::max(p.sanity, 100.0f); p.injuries = 0; p.bleedT = p.downT = p.stunT = 0; p.lostT = 0; p.blackoutT = 0; p.jumpCool = 0; p.hands = Loot{}; p.pocket[0] = p.pocket[1] = Loot{}; p.carryWith = -1;
        p.battery = std::max(p.battery, 300.0f); p.lamp = true; p.sleptDay = -1; p.toolSlots = (p.suits >> 2) & 1 ? 5 : 4;
    }
    // the Labs' generators burn a day of fuel
    for (auto& l : labs) if (l.online) { l.fuel -= (l.upgrades >> 4) & 1 ? 0.5f : 1.0f; if (l.fuel < 0) { l.fuel = 0; } l.cooldown = 0; l.charge = 0; l.charging = false; l.openT = 0; }
    ins->fuel = std::max(ins->fuel, 0.5f);   // (the Bureau keeps the insertion Lab's portal fed)
    Emit(E_WAKE, -1, -1, ins->level);
}
void World::EndDay(bool extracted) {
    (void)extracted;
    // today's contract: paid if it was done (some are checked against the bay and the map now)
    if (contract >= 0 && inDay) {
        const ContractDef& c = D().contracts[contract];
        if (c.id == "retrieval") for (const auto& l : bay) if (l.def == contractTarget) contractDone = true;
        if (c.id == "survey" && SeenFraction(contractLevel) >= 0.8f) contractDone = true;
        if (c.id == "specimen") for (const auto& l : bay) if (l.def >= 0 && D().loot[l.def].props.find("living") != std::string::npos) contractDone = true;
        if (contractDone) { int pay = (c.creditMin + c.creditMax) / 2; credit += pay; cash += c.cash; Emit(E_SALE, -1, 2, -1, {}, (float)pay, "contract: " + c.name); }
    }
    // the Bureau's memo, written from the day's log
    if (inDay) {
        std::string m; int deaths = 0, noclips = 0; std::string worst;
        for (const auto& p : crew) { deaths += p.st == PS_DEAD || p.st == PS_TAKEN; if (p.noclipCount >= 2) worst = p.name + " noclipped " + std::to_string(p.noclipCount) + " times; please stop. "; noclips += p.noclipCount; }
        int got = 0; for (const auto& l : bay) got += l.value;
        if (deaths) m += std::to_string(deaths) + (deaths == 1 ? " employee" : " employees") + " did not clock out. ";
        m += worst;
        m += got ? "The loading bay received " + std::to_string((int)bay.size()) + " items (est. $" + std::to_string(got) + "). " : "The loading bay received nothing. The Bureau notes this. ";
        if (contractDone && contract >= 0) m += "Contract '" + D().contracts[contract].name + "' completed. ";
        memo = m;
        for (auto& p : crew) p.noclipCount = 0;
    }
    if (mode == 4 && inDay) { score = credit; for (const auto& l : bay) score += l.value; failed = true; won = true; }   // (Expedition: one day, scored)
    inDay = false;
    for (auto& p : crew) { if (p.st == PS_DEAD || p.st == PS_TAKEN) p.st = PS_SURFACE; p.st = PS_SURFACE; p.hands = Loot{}; p.pocket[0] = p.pocket[1] = Loot{}; }
    day++;
    // tomorrow: a forecast (which levels are active), the Fence's rates, a contract
    daySeed = rng ^ (uint32_t)(day * 2654435761u); forecast.clear(); for (int k = 0; k < 3; k++) forecast.push_back(RandI(LEVELS));
    fenceRate.assign(8, 100); for (int& r : fenceRate) r = 60 + RandI(101);
    std::vector<int> offer; for (int i = 0; i < (int)D().contracts.size(); i++) if (D().contracts[i].id != "party" && D().contracts[i].id != "rescue") offer.push_back(i);
    contract = offer.empty() ? -1 : offer[RandI((int)offer.size())]; contractDone = false; contractLevel = std::min(LEVELS - 1, RandI(4) + (week - 1) * 2); contractTarget = 0;
    { int band = D().levels[contractLevel].lootBand; std::vector<int> pool; for (int i = 0; i < (int)D().loot.size(); i++) if (D().loot[i].level == band) pool.push_back(i); if (!pool.empty()) contractTarget = pool[RandI((int)pool.size())]; }
    if (contract >= 0) { const std::string& cid = D().contracts[contract].id; if (cid == "retrieval") memo += " Today: bring back a " + D().loot[contractTarget].name + " from Level " + std::to_string(contractLevel) + "."; if (cid == "survey") memo += " Today: map 80% of Level " + std::to_string(contractLevel) + "."; }
    Emit(E_DAY_END);
}
void World::NextWeek() {
    weeksSurvived++; week++; day = 1; quota = QuotaFor(week); credit = 0;
    Emit(E_QUOTA, -1, -1, -1, {}, (float)quota);
}

// ---------------------------------------------------------------- the crew
void World::Hurt(Player& p, float dmg, const std::string& cause, int injury) {
    if (p.impostor) return;   // (the social-deduction mode's Skin-Stealer can't die)
    if (p.st != PS_ALIVE) { if (p.st == PS_DOWNED && dmg >= 30) Kill(p, cause); return; }
    p.health -= dmg; p.injuries |= injury; p.lastCause = cause; Emit(E_HURT, p.id, -1, p.level, p.p, dmg, cause);
    if (injury & IN_BLEED) p.bleedT = 0;
    if (p.health <= 0) Down(p, cause);
}
void World::Down(Player& p, const std::string& cause) { p.st = PS_DOWNED; p.health = 0; p.downT = 40; p.lastCause = cause; Drop(p, false); Emit(E_DOWNED, p.id, -1, p.level, p.p, 0, cause); }
void World::Kill(Player& p, const std::string& cause) {
    // everything they carried drops; they become a Wanderer
    if (p.hands.def >= 0 || p.pocket[0].def >= 0 || p.pocket[1].def >= 0) { for (Loot* l : {&p.hands, &p.pocket[0], &p.pocket[1]}) if (l->def >= 0) { WorldItem wi; wi.loot = *l; wi.level = p.level; wi.p = Vector3Add(p.p, {(Rand() - 0.5f), 0, (Rand() - 0.5f)}); items.push_back(wi); *l = Loot{}; } }
    p.st = PS_DEAD; p.health = 0; p.deaths++; p.lastCause = cause; Emit(E_DIED, p.id, -1, p.level, p.p, 0, cause);
    for (auto& o : crew) if (o.id != p.id && o.st == PS_ALIVE) o.sanity -= o.level == p.level ? 15 : 5;
}
bool World::Give(Player& p, const Loot& l) {
    if (l.def < 0) return false; Size s = SizeOf(D().loot[l.def]);
    if (s == SZ_S) { for (auto& pk : p.pocket) if (pk.def < 0) { pk = l; return true; } }
    if (p.hands.def < 0) { p.hands = l; return true; }
    return false;
}
void World::Drop(Player& p, bool throwIt) {
    Loot* l = p.hands.def >= 0 ? &p.hands : p.pocket[1].def >= 0 ? &p.pocket[1] : p.pocket[0].def >= 0 ? &p.pocket[0] : nullptr;
    if (!l) return;
    WorldItem wi; wi.loot = *l; wi.level = p.level; Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)};
    wi.p = Vector3Add(p.p, Vector3Scale(f, throwIt ? 3.0f : 0.7f));
    Level& lv = L(p.level); if (lv.Solid(lv.CellX(wi.p.x), lv.CellZ(wi.p.z))) wi.p = p.p;
    if (Has(D().loot[l->def], "fragile") && throwIt) wi.loot.damaged = true;
    wi.noiseT = throwIt ? 1.5f : 0.6f;
    items.push_back(wi); *l = Loot{}; p.carryWith = -1;
    Emit(E_DROP, p.id, -1, p.level, wi.p, throwIt ? 1.0f : 0.0f);
}
void World::Transit(Player& p, int to, bool noclip, const std::string& how) {
    int from = p.level; to = std::clamp(to, 0, LEVELS - 1);
    if (mode == 2 && how != "shot" && how != "test") { to = RandI(10); noclip = true; }   // (Noclip Roulette: every exit is a noclip to a random level)
    Level& lv = L(to);
    // arrive at the way back if there is one (a door from `from`), else somewhere in the level
    Vector3 at = lv.start; bool placed = false;
    if (!noclip) for (const auto& e : lv.exits) if (e.to == from && !e.noclip) { at = lv.Center(e.cx, e.cz); placed = true; break; }
    if (!placed) { for (int tries = 0; tries < 200; tries++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.Walkable(x, z) && lv.At(x, z) != T_PIT && lv.At(x, z) != T_DEEP && lv.At(x, z) != T_LABFLOOR) { at = lv.Center(x, z); break; } } }
    p.level = to; p.p = at; p.vel = {};
    if (noclip) { p.sanity -= 10; p.blackoutT = 2; p.noclipCount++; }
    std::string key = std::to_string(from) + ":" + how; if (std::find(learned.begin(), learned.end(), key) == learned.end()) learned.push_back(key);
    Emit(noclip ? E_NOCLIP : E_EXIT, p.id, from, to, at, 0, how);
    // whoever carried the other end of a huge thing came along only if they went too
    if (p.carryWith >= 0) { Player& o = crew[p.carryWith]; if (o.level != to) { o.carryWith = -1; p.carryWith = -1; } }
}
void World::Move(Player& p, Vector3 wish, float speed) {
    Level& lv = L(p.level); float r = 0.3f;
    Vector3 np = Vector3Add(p.p, Vector3Scale(wish, speed * STEP));
    bool keycard = false; if (p.level == 5) for (const Loot& l : {p.pocket[0], p.pocket[1]}) if (l.def >= 0 && D().loot[l.def].name == "Key card") keycard = true;
    auto blocked = [&](float x, float z) { int cx = lv.CellX(x), cz = lv.CellZ(z); uint8_t t = lv.At(cx, cz); if (lv.Solid(cx, cz)) return true;
        if (t == T_DOOR && p.level == 5 && !keycard && (cx + cz) % 3 != 0) return true;   // (the hotel's rooms: most doors want a key card)
        if (t == T_BLAST) { LabState* lab = nullptr; for (int k = 0; k < (int)lv.labs.size(); k++) if (lv.labs[k].doorX == cx && lv.labs[k].doorZ == cz) lab = Lab(p.level, k); return lab && (!lab->doorOpen || (lab->locked && false)); } return false; };
    // axis by axis against the cells (a circle of radius r)
    float ox = np.x; for (float s : {-r, r}) if (blocked(np.x + s, p.p.z - r * 0.7f) || blocked(np.x + s, p.p.z + r * 0.7f)) { np.x = p.p.x; break; }
    for (float s : {-r, r}) if (blocked(np.x - r * 0.7f, np.z + s) || blocked(np.x + r * 0.7f, np.z + s)) { np.z = p.p.z; break; }
    (void)ox; p.p = np;
}
void World::StepMeters(Player& p) {
    float dt = STEP; Level& lv = L(p.level); int cx = lv.CellX(p.p.x), cz = lv.CellZ(p.p.z); uint16_t fl = lv.Flags(cx, cz); uint8_t tile = lv.At(cx, cz);
    const std::string& mood = D().levels[p.level].mood;
    bool inLab = LabAt(p.level, p.p) != nullptr; LabState* lab = LabAt(p.level, p.p); bool labLit = lab && lab->online && lab->fuel > 0;
    // sanity: time in the Backrooms, darkness, being alone, the hum; restored by company, Lab lights, music boxes
    float drain = 1.0f / 60;
    if (LightAt(p.level, p.p) < 0.2f) drain += 1.0f / 30;
    int near = Nearest(p.level, p.p, 15, p.id); if (near < 0 && crew.size() > 1) drain += 1.0f / 20; else if (near >= 0) drain -= 1.0f / 30;
    if (mood == "hum" && !(fl & CF_DEADLIGHT)) drain += 1.0f / 90;
    if (mood == "living") drain += 1.0f / 25;
    if (mood == "edge") { bool edge = false; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (lv.At(cx + dx, cz + dz) == T_PIT) edge = true; if (edge) { p.edgeT += dt; if (p.edgeT > 5) drain += 1.5f; } else p.edgeT = 0; }
    if (mood == "comfort") drain -= 1.0f / 30 + 1.0f / 60;
    if (mood == "haven") { /* the windows */ if (fl & CF_WINDOW) { p.windowT += dt; if (p.windowT > 3) { p.sanity -= 10; p.windowT = 0; } } }
    if (overtime) drain *= 2;
    if (labLit) { drain -= 5.0f / 60; p.health = std::min(100.0f, p.health + dt * 0.1f); }
    for (const auto& w : items) if (w.level == p.level && w.loot.def < 0 && w.noiseT > 0 && D().items[-1 - w.loot.def].use == "musicbox" && Vector3Distance(w.p, p.p) < 5) drain -= 0.2f;   // (a music box: +2 every 10 s)
    p.sanity = std::clamp(p.sanity - drain * dt, 0.0f, 100.0f);
    // Level 13: stare at a mirror too long and your reflection steps out
    if (p.level == 13) { Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}; int fx = lv.CellX(p.p.x + f.x * CELL), fz = lv.CellZ(p.p.z + f.z * CELL); if (lv.Flags(fx, fz) & CF_MIRROR) { p.mirrorT += dt; if (p.mirrorT > 5) { p.mirrorT = -20; Entity e; e.def = EntityIndex("mirrorthing"); e.level = 13; e.uid = nextUid++; e.p = lv.Center(fx - (int)roundf(f.x), fz - (int)roundf(f.z)); e.st = ES_CHASE; if (e.def >= 0) ents.push_back(e); Emit(E_TELL, p.id, e.def, 13, e.p, 0, "your reflection is late"); } } else p.mirrorT = std::max(0.0f, p.mirrorT - dt); }
    // Level 18: it's hard to leave. After ten minutes, the Stay prompt every minute (Y would take it)
    if (p.level == 18) { p.stayT += dt; if (p.stayT > 600) { p.stayPromptT -= dt; if (p.stayPromptT <= -50) { p.stayPromptT = 10; Emit(E_STAY, p.id, -1, 18, p.p); } } } else { p.stayT = 0; p.stayPromptT = 0; }
    if (p.sanity <= 0 && p.lostT <= 0 && p.st == PS_ALIVE) { p.lostT = 10; p.sanity = 5; Emit(E_LOST, p.id, -1, p.level, p.p); }
    // health: bleeding
    if (p.injuries & IN_BLEED) { p.bleedT += dt; if (p.bleedT >= 3) { p.bleedT = 0; Hurt(p, 1, "bled out"); } }
    // stamina: sprinting, heat and cold, heavy loads; recovered walking or standing
    float sp = Vector2Length({p.vel.x, p.vel.z});
    float heat = (mood == "heat" || (fl & CF_HOT)) ? ((p.suits & 1) ? 0.5f : 1.0f) * (p.flaskT > 0 ? 0.5f : 1.0f) : 0;
    float cold = (mood == "water" && (tile == T_WATER || tile == T_DEEP)) ? ((p.suits & 2) ? 0.5f : 1.0f) * (p.flaskT > 0 ? 0.5f : 1.0f) : 0;
    if (p.in.sprint && sp > 3.2f) p.stamina -= (14 + heat * 8) * dt;
    else p.stamina += (sp < 0.2f ? 14 : 8) * (1 - heat * 0.5f) * dt;
    if (p.hands.def >= 0 && SizeOf(D().loot[p.hands.def]) >= SZ_L) p.stamina -= 2 * dt;
    if (tile == T_DEEP || tile == T_WATER) p.stamina -= (tile == T_DEEP ? 4 : 1.5f) * (1 + cold) * dt;
    p.stamina = std::clamp(p.stamina, 0.0f, 100.0f);
    p.flaskT = std::max(0.0f, p.flaskT - dt);
    // water: breath under the deep water (a tank gives three minutes); the Leviathan counts the swim (noclip_ents.cpp)
    if (tile == T_DEEP) { p.swimT += dt; if (p.swimT > 30 && p.tankAir <= 0) Hurt(p, 6 * dt, "drowned"); if (p.tankAir > 0) p.tankAir -= dt; } else p.swimT = std::max(0.0f, p.swimT - dt * 2);
    // electrified floors, steam, the abyss
    if ((fl & CF_LIVE) && fmodf(clock, 20) < 6 && p.stunT <= 0) { Hurt(p, 12, "electrocuted"); p.stunT = 2; Drop(p, false); }   // (live floors: electrified six minutes in twenty)
    if ((fl & CF_STEAM) && fmodf(clock * 0.37f + cx * 7 + cz * 3, 30) < 0.4f) Hurt(p, 20, "a steam burst");
    if (tile == T_PIT) {
        // a pit: an exit if it is one, otherwise a fall
        bool exitPit = false; for (const auto& e : lv.exits) if (e.cx == cx && e.cz == cz) exitPit = true;
        if (!exitPit) { if (p.level == 19) Kill(p, "fell into the abyss"); else { Hurt(p, 40, "a fall", IN_BREAK); Transit(p, std::min(LEVELS - 1, p.level + (p.level == 6 ? 2 : 1)), true, "a fall"); } }
    }
    if (labLit && inLab) p.labLightT += dt;
}
void World::StepPlayer(Player& p) {
    float dt = STEP; Input& in = p.in;
    p.yaw = in.yaw; p.pitch = std::clamp(in.pitch, -1.4f, 1.4f);
    p.jumpCool = std::max(0.0f, p.jumpCool - dt * (D().dayEnd - D().dayStart) / dayLen());   // (in in-game minutes)
    p.noise = 0;
    if (p.st == PS_SURFACE) return;
    if (p.st == PS_DEAD) {   // a Wanderer: drifts through walls at walking speed
        Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}, r{-sinf(p.yaw), 0, cosf(p.yaw)};
        Vector3 wish = Vector3Add(Vector3Scale(f, in.moveX), Vector3Scale(r, in.moveZ)); if (Vector3Length(wish) > 1) wish = Vector3Normalize(wish);
        p.p = Vector3Add(p.p, Vector3Scale(wish, 3.0f * dt)); Level& lv = L(p.level); p.p.x = std::clamp(p.p.x, 0.0f, lv.w * CELL); p.p.z = std::clamp(p.p.z, 0.0f, lv.h * CELL);
        return;
    }
    if (p.st == PS_DOWNED) {
        p.downT -= dt; if (p.downT <= 0) { Kill(p, p.lastCause.empty() ? "bled out" : p.lastCause); return; }
        Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}; Move(p, Vector3Scale(f, std::clamp(in.moveX, 0.0f, 1.0f)), 0.6f);
        return;
    }
    if (p.st != PS_ALIVE) return;
    p.stunT = std::max(0.0f, p.stunT - dt); p.blackoutT = std::max(0.0f, p.blackoutT - dt);
    StepMeters(p); if (p.st != PS_ALIVE) return;
    // the headlamp: a battery's ten minutes (faster on Level 6)
    if (in.lamp) p.lamp = !p.lamp;
    if (p.lamp && p.battery > 0) { p.battery -= dt * (p.level == 6 ? 3.0f : 1.0f); if (p.battery <= 0) { p.battery = 0; p.lamp = false; } }
    // Lost: the autopilot walks them toward the nearest noclip spot unless a teammate grabs them
    Vector3 wish{}; float speed = 3.0f;
    if (p.lostT > 0) {
        p.lostT -= dt; Level& lv = L(p.level); float bd = 1e9f; Vector3 tgt = p.p;
        for (const auto& e : lv.exits) if (e.noclip) { Vector3 c = lv.Center(e.cx, e.cz); float d = Vector3Distance(c, p.p); if (d < bd) { bd = d; tgt = c; } }
        std::vector<int> path = Path(p.level, p.p, tgt, 3000);
        Vector3 nx = tgt; if (path.size() > 1) { int c = path[1]; nx = lv.Center(c % lv.w, c / lv.w); } wish = Vector3Subtract(nx, p.p); wish.y = 0; if (Vector3Length(wish) > 0.05f) wish = Vector3Normalize(wish);
        speed = 2.4f;
        if (bd < 1.6f) { for (const auto& e : lv.exits) if (e.noclip && Vector3Distance(lv.Center(e.cx, e.cz), p.p) < 1.6f) { p.lostT = 0; Transit(p, RandI(10), true, "wandered while Lost"); return; } }
    } else if (p.stunT <= 0 && p.blackoutT <= 0) {
        Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}, r{-sinf(p.yaw), 0, cosf(p.yaw)};
        wish = Vector3Add(Vector3Scale(f, in.moveX), Vector3Scale(r, in.moveZ)); float wl = Vector3Length(wish); if (wl > 1) wish = Vector3Scale(wish, 1 / wl);
        bool canSprint = in.sprint && p.stamina > 5 && !(p.injuries & IN_SPRAIN) && !in.crouch;
        p.crouched = in.crouch;
        speed = p.crouched ? 1.5f : canSprint ? 5.2f : 3.0f;
        Level& lv = L(p.level); uint8_t tile = lv.At(lv.CellX(p.p.x), lv.CellZ(p.p.z));
        if (tile == T_WATER) speed *= 0.6f; else if (tile == T_DEEP) speed = 1.8f; else if (tile == T_WHEAT) speed *= 0.8f;
        speed *= p.SpeedMul();
        if (p.carryWith >= 0) speed *= 0.6f / std::max(0.6f, p.SpeedMul());   // (a huge thing between two)
        // noise: footsteps (carpet hushes them), noisy carry
        if (wl > 0.1f) { const std::string& tex = D().levels[p.level].tex[1]; float base = canSprint ? 1.0f : p.crouched ? 0.12f : 0.45f; if (tex == "carpet") base *= 0.6f; if ((p.suits >> 3) & 1) base *= 0.5f; p.noise = std::max(p.noise, base); }
        if (p.hands.def >= 0 && Has(D().loot[p.hands.def], "noisy")) p.noise = std::max(p.noise, 0.5f);
    }
    Vector3 before = p.p; Move(p, wish, speed); p.vel = Vector3Scale(Vector3Subtract(p.p, before), 1 / dt);
    // walking into a noclip spot at a run drops you through; stepping onto a door exit takes it (E for doors, below)
    Level& lv = L(p.level); int cx = lv.CellX(p.p.x), cz = lv.CellZ(p.p.z);
    for (const auto& e : lv.exits) if (e.cx == cx && e.cz == cz && e.noclip && (Vector2Length({p.vel.x, p.vel.z}) > 4.0f || e.kind == "pit" || e.kind == "abyss")) {
        int to = e.to; if (e.kind == "abyss") { if (Rand() < 0.15f) to = 0; else { Kill(p, "the abyss"); return; } }
        else if (Rand() < 0.3f) to = std::clamp(e.to + (Rand() < 0.5f ? 1 : -1), 0, 9);   // (a noclip lands in one of two levels, weighted)
        Transit(p, to, true, e.label); return;
    }
    SeeMap(p);
    if (in.use) Interact(p);
    if (in.drop || in.throwIt) Drop(p, in.throwIt);
    if (in.slot >= 0 && in.slot < p.toolSlots) p.sel = in.slot;
    if (in.primary) UseTool(p);
    for (auto& c : p.cmds) Command(p, c); p.cmds.clear();
}
void World::Interact(Player& p) {
    Level& lv = L(p.level); Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)};
    Vector3 front = Vector3Add(p.p, Vector3Scale(f, 0.9f));
    // a downed or Lost teammate first: revive or grab
    for (auto& o : crew) if (o.id != p.id && o.level == p.level && Vector3Distance(o.p, p.p) < 1.8f) {
        if (o.st == PS_DOWNED) { bool kit = false; for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item == ItemIndex("medkit")) { kit = true; p.tools[k] = Tool{}; break; } o.st = PS_ALIVE; o.health = kit ? 50 : 20; o.downT = 0; Emit(E_REVIVED, o.id, p.id, o.level, o.p, kit ? 1.0f : 0.0f); return; }
        if (o.lostT > 0) { o.lostT = 0; o.sanity = std::max(o.sanity, 15.0f); return; }
        if (o.hands.def >= 0 && SizeOf(D().loot[o.hands.def]) == SZ_H && o.carryWith < 0 && p.hands.def < 0) { o.carryWith = p.id; p.carryWith = o.id; return; }   // (taking the other end)
    }
    // loot on the floor: the nearest in front
    int best = -1; float bd = 1.7f;
    for (int i = 0; i < (int)items.size(); i++) if (items[i].level == p.level) { float d = Vector3Distance(items[i].p, front); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) {
        const LootDef& ld = D().loot[items[best].loot.def];
        if (SizeOf(ld) == SZ_H) { if (p.hands.def < 0) { p.hands = items[best].loot; items.erase(items.begin() + best); Emit(E_PICKUP, p.id, -1, p.level, p.p); } return; }
        if (Give(p, items[best].loot)) { items.erase(items.begin() + best); Emit(E_PICKUP, p.id, -1, p.level, p.p); }
        return;
    }
    // a Lab's fittings
    for (int k = 0; k < (int)lv.labs.size(); k++) {
        const LabPlan& lp = lv.labs[k]; LabState* lab = Lab(p.level, k); if (!lab) continue;
        // the blast door: power from the breaker, or a crowbar and two (one alone, slowly: a long pry)
        Vector3 door = lv.Center(lp.doorX, lp.doorZ);
        if (!lab->doorOpen && Vector3Distance(door, p.p) < 2.2f) {
            bool crowbar = false; for (int t = 0; t < p.toolSlots; t++) if (p.tools[t].item == ItemIndex("crowbar")) crowbar = true;
            int helpers = 0; for (const auto& o : crew) if (o.id != p.id && o.level == p.level && o.st == PS_ALIVE && Vector3Distance(o.p, door) < 3) helpers++;
            if (crowbar && (helpers >= 1 || Rand() < 0.08f)) { lab->doorOpen = true; Emit(E_TELL, p.id, -1, p.level, door, 2, "the blast door shrieks open"); }
            return;
        }
        if (Vector3Distance(lv.Center(lp.breakerX, lp.breakerZ), p.p) < 1.8f && !lab->doorOpen) { lab->doorOpen = true; Emit(E_TELL, p.id, -1, p.level, p.p, 1, "a breaker clunks over; far off, a blast door opens"); return; }
        for (const auto& sp : lp.spots) {
            if (Vector2Distance({sp.at.x, sp.at.z}, {p.p.x, p.p.z}) > 1.7f) continue;
            switch (sp.part) {
                case LP_CRATE: {   // the drop-off: everything carried goes in
                    int moved = 0; for (Loot* l : {&p.hands, &p.pocket[0], &p.pocket[1]}) if (l->def >= 0) { lab->crate.push_back(*l); p.broughtValue += l->value; *l = Loot{}; moved++; }
                    if (p.carryWith >= 0) { crew[p.carryWith].carryWith = -1; p.carryWith = -1; }
                    if (moved) Emit(E_CRATE, p.id, -1, p.level, sp.at, (float)moved);
                    return;
                }
                case LP_GEN: {   // the generator: a fuel canister brings the Lab online
                    int fuel = ItemIndex("fuel"); for (int t = 0; t < p.toolSlots; t++) if (p.tools[t].item == fuel && fuel >= 0) { p.tools[t] = Tool{}; bool was = lab->online; lab->online = true; lab->doorOpen = true; lab->fuel += (lab->upgrades >> 4) & 1 ? 2 : 1; if (!was) { Emit(E_LAB_ONLINE, p.id, -1, p.level, sp.at, (float)k, lp.name); if (contract >= 0 && D().contracts[contract].id == "restart") contractDone = true; } return; }
                    return;
                }
                case LP_BUNK: { if (p.sleptDay != day && lab->online) { p.sleptDay = (float)day; p.sanity = std::min(100.0f, p.sanity + 20); p.stunT = 3; } return; }
                case LP_ARCHIVE: { for (const auto& e : lv.exits) { std::string key = std::to_string(p.level) + ":" + e.label; if (std::find(learned.begin(), learned.end(), key) == learned.end()) learned.push_back(key); } Emit(E_TELL, p.id, -1, p.level, sp.at, 3, "a map fragment: this level's exits"); return; }
                default: return;   // (the desk, the shop and the monitors are panels the scene opens)
            }
        }
    }
    // a door exit: E to go through
    int cx = lv.CellX(front.x), cz = lv.CellZ(front.z), pcx = lv.CellX(p.p.x), pcz = lv.CellZ(p.p.z);
    for (const auto& e : lv.exits) if (!e.noclip && ((e.cx == cx && e.cz == cz) || (e.cx == pcx && e.cz == pcz))) { Transit(p, e.to, false, e.label); return; }
    // Level 14's beds heal (ten seconds on a gurney); Level 15's terminals: a hack stops the Sentries for two minutes
    if (p.level == 14 && (lv.Flags(pcx, pcz) & CF_ROOM) && p.health < 100) { p.stunT = 10; p.health = 100; p.injuries = 0; Emit(E_TELL, p.id, -1, 14, p.p, 3, "you lie down on a gurney: fully healed"); return; }
    if (p.level == 15 && lv.At(cx, cz) == T_LOW) { p.stunT = 5; for (auto& e : ents) if (e.level == 15 && D().entities[e.def].id == "sentry") e.stunT = 120; Emit(E_TELL, p.id, -1, 15, p.p, 3, "the terminal's pattern matches: the Sentries power down"); return; }
}
void World::UseTool(Player& p) {
    Tool& t = p.tools[std::clamp(p.sel, 0, 4)]; if (t.item < 0) return; const ItemDef& it = D().items[t.item]; const std::string& u = it.use;
    auto consume = [&]() { if (--t.charges <= 0) t = Tool{}; };
    if (u == "battery") { p.battery = std::min(1200.0f, p.battery + 600); p.lamp = true; consume(); }
    else if (u == "almond") { p.sanity = std::min(100.0f, p.sanity + 25); p.health = std::min(100.0f, p.health + 10); p.stamina = 100; consume(); }
    else if (u == "medkit") { p.health = std::min(100.0f, p.health + 50); consume(); }
    else if (u == "bandage") { p.injuries &= ~(IN_BLEED | IN_SPRAIN); consume(); }
    else if (u == "splint") { p.injuries &= ~IN_BREAK; consume(); }
    else if (u == "flask") { p.flaskT = 600; consume(); }
    else if (u == "tank") { p.tankAir = 180; consume(); }
    else if (u == "flare" || u == "glowstick" || u == "noisemaker" || u == "musicbox") {   // dropped lights and toys (the entities and the meters read them as items with a timer)
        WorldItem wi; wi.level = p.level; wi.p = Vector3Add(p.p, {cosf(p.yaw) * 1.0f, 0, sinf(p.yaw) * 1.0f}); wi.loot.def = -1 - t.item; wi.noiseT = u == "flare" ? 30 : u == "glowstick" ? (p.level == 6 ? 30 : 120) : u == "noisemaker" ? 20 : 60; items.push_back(wi); consume();
        Emit(E_TELL, p.id, -1, p.level, wi.p, 4, u);
    }
    else if (u == "crowbar" || u == "shotgun" || u == "camera") { Emit(E_SHOT, p.id, t.item, p.level, p.Eye(), 0, u); ToolOnEntities(p, u); }
    else if (u == "chalk") {   // a chalk arrow on the wall in front (it lasts the day)
        Level& lv = L(p.level); Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)}; Vector3 at = p.p; for (float d = 0.4f; d < 3; d += 0.2f) { Vector3 q = Vector3Add(p.p, Vector3Scale(f, d)); if (lv.Solid(lv.CellX(q.x), lv.CellZ(q.z))) break; at = q; }
        marks.push_back({p.level, at, p.yaw}); consume();
    }
    else if (u == "grapple" || u == "rope") {   // across a pit (the grapple: 15 m to the far edge) / down one safely (the rope: the pit's exit without the fall)
        Level& lv = L(p.level); Vector3 f{cosf(p.yaw), 0, sinf(p.yaw)};
        if (u == "rope") { int cx = lv.CellX(p.p.x + f.x * CELL), cz = lv.CellZ(p.p.z + f.z * CELL); if (lv.At(cx, cz) == T_PIT) { for (const auto& e : lv.exits) if (e.cx == cx && e.cz == cz) { Transit(p, e.to, false, "down a rope"); return; } Transit(p, std::min(LEVELS - 1, p.level + 1), false, "down a rope"); } return; }
        for (float d = 2; d <= 15; d += 0.5f) { Vector3 q = Vector3Add(p.p, Vector3Scale(f, d)); int cx = lv.CellX(q.x), cz = lv.CellZ(q.z); if (lv.Solid(cx, cz)) break; if (d > 3 && lv.Walkable(cx, cz) && lv.At(cx, cz) != T_PIT && lv.At(cx, cz) != T_DEEP) { bool crossed = false; for (float e2 = 1; e2 < d; e2 += 0.5f) { Vector3 m = Vector3Add(p.p, Vector3Scale(f, e2)); if (lv.At(lv.CellX(m.x), lv.CellZ(m.z)) == T_PIT) crossed = true; } if (crossed) { p.p = lv.Center(cx, cz); Emit(E_TELL, p.id, -1, p.level, p.p, 3, "the hook bites; you swing across"); return; } } }
    }
    if (u == "shotgun") consume();
}
float World::Noise(int level, Vector3 at) const {
    float n = 0;
    for (const auto& p : crew) if (p.level == level && p.Alive()) { float d = Vector3Distance(p.p, at); n = std::max(n, p.noise * 30 / std::max(1.0f, d)); }
    for (const auto& w : items) if (w.level == level && w.noiseT > 0) { float d = Vector3Distance(w.p, at); n = std::max(n, 0.8f * 30 / std::max(1.0f, d)); }
    for (const auto& l : labs) if (l.level == level && l.charging) { n = std::max(n, 1.5f * ((l.upgrades >> 3) & 1 ? 2 : 1)); }
    return n;
}

// ---------------------------------------------------------------- the field map (what the crew has seen: shared)
void World::SeeMap(const Player& p) {
    if (seenCells.size() < (size_t)LEVELS) seenCells.resize(LEVELS);
    Level& lv = L(p.level); auto& s = seenCells[p.level]; if (s.size() != (size_t)(lv.w * lv.h)) s.assign(lv.w * lv.h, 0);
    int cx = lv.CellX(p.p.x), cz = lv.CellZ(p.p.z);
    for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) { int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= lv.w || z >= lv.h) continue; if (dx * dx + dz * dz > 18) continue; if (s[z * lv.w + x]) continue; if (abs(dx) + abs(dz) <= 1 || LineOfSight(p.level, Vector3Add(p.p, {0, 1, 0}), Vector3Add(lv.Center(x, z), {0, 1, 0}))) s[z * lv.w + x] = 1; }
}
float World::SeenFraction(int level) const {
    if ((size_t)level >= seenCells.size()) return 0; auto it = levels.find(level); if (it == levels.end()) return 0; const Level& lv = it->second;
    int walk = 0, seen = 0; for (int i = 0; i < lv.w * lv.h; i++) if (lv.Walkable(i % lv.w, i / lv.w)) { walk++; if (i < (int)seenCells[level].size() && seenCells[level][i]) seen++; }
    return walk ? (float)seen / walk : 0;
}

// ---------------------------------------------------------------- the Labs
void World::Extract(LabState& lab) {
    // everyone in the Portal Hall goes up, with every networked crate (generators running), and what they hold
    for (auto& l : labs) if (&l == &lab || (l.online && l.fuel > 0 && !overtime && l.level != 12)) { for (auto& c : l.crate) bay.push_back(c); l.crate.clear(); }
    for (auto& p : crew) if (InPortalHall(p, lab)) { for (Loot* l : {&p.hands, &p.pocket[0], &p.pocket[1]}) if (l->def >= 0) { bay.push_back(*l); *l = Loot{}; } p.st = PS_SURFACE; if (p.carryWith >= 0) p.carryWith = -1; }
    Emit(E_EXTRACT, -1, -1, lab.level);
    if (mode == 1) { won = true; failed = true; }   // (Lost: you escaped)
    if (mode == 6) { bool impostorLeft = false, crewUp = false; for (const auto& p : crew) { if (p.impostor && p.st != PS_SURFACE) impostorLeft = true; if (!p.impostor && p.st == PS_SURFACE) crewUp = true; } if (impostorLeft && crewUp) { won = true; failed = true; memo = "The crew got out and left the thing wearing a friend behind."; } }
    bool anyoneLeft = false; for (const auto& p : crew) if (p.st == PS_ALIVE || p.st == PS_DOWNED) anyoneLeft = true;
    if (!anyoneLeft) EndDay(true);
}
void World::StepLabs() {
    float dt = STEP;
    for (auto& l : labs) {
        l.cooldown = std::max(0.0f, l.cooldown - dt); l.sirenT = std::max(0.0f, l.sirenT - dt); l.cargoCool = std::max(0.0f, l.cargoCool - dt);
        if (l.charging) {
            l.charge += dt / ((l.upgrades >> 3) & 1 ? 20.0f : 30.0f);
            if (l.charge >= 1) { l.charging = false; l.charge = 0; l.openT = 20; Emit(E_PORTAL_OPEN, -1, -1, l.level); }
        }
        if (l.openT > 0) { l.openT -= dt; bool any = false; for (const auto& p : crew) if (InPortalHall(p, l)) any = true; if (any) { l.openT = 0; Extract(l); l.cooldown = 50; } else if (l.openT <= 0) l.cooldown = 50; }
        if (l.jumpFor >= 0) { l.jumpT -= dt; if (l.jumpT <= 0) { Player& p = crew[l.jumpFor]; LabState& to = labs[std::clamp(l.jumpTo, 0, (int)labs.size() - 1)]; if (p.st == PS_ALIVE && InPortalHall(p, l)) { Level& lv = L(to.level); p.level = to.level; p.p = lv.labs[to.idx].spots[0].at; p.jumpCool = 60; Emit(E_JUMP, p.id, -1, to.level, p.p); } l.jumpFor = -1; } }
    }
}

// ---------------------------------------------------------------- commands (the Surface's, and the Lab desk's)
void World::Command(Player& p, const nc::Command& c) {
    auto price = [&](int base, int level) { return (int)roundf(base * (level >= 0 ? D().labPriceMul[std::clamp(level, 0, LEVELS - 1)] * 1.25f : 1.0f)); };
    auto giveTool = [&](int item) { for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item < 0) { p.tools[k] = {item, D().items[item].charges, 0}; return true; } return false; };
    switch (c.kind) {
        case C_NAME: if (!c.s.empty()) p.name = c.s.substr(0, 16); break;
        case C_BUY: if (!inDay && c.a >= 0 && c.a < (int)D().items.size() && cash >= D().items[c.a].price) { if (D().items[c.a].id == "shotgun") { bool owned = false; for (auto& o : crew) for (auto& t : o.tools) if (t.item == c.a) owned = true; if (owned) break; } if (giveTool(c.a)) cash -= D().items[c.a].price; } break;
        case C_LAB_BUY: { LabState* lab = LabAt(p.level, p.p); if (!inDay || mode == 4 || !lab || !lab->online || c.a < 0 || c.a >= (int)D().items.size()) break; int pr = price(D().items[c.a].price, p.level); if (cash >= pr && giveTool(c.a)) cash -= pr; break; }
        case C_BUY_SUIT: if (!inDay && c.a >= 0 && c.a < (int)D().suits.size() && !((p.suits >> c.a) & 1) && cash >= D().suits[c.a].price) { cash -= D().suits[c.a].price; p.suits |= 1u << c.a; if (D().suits[c.a].id == "bigpack") p.toolSlots = 5; } break;
        case C_BUY_UPGRADE: if (!inDay && c.a >= 0 && c.a < (int)D().labUps.size() && c.b >= 0 && c.b < (int)labs.size() && labs[c.b].online && !((labs[c.b].upgrades >> c.a) & 1) && cash >= D().labUps[c.a].price) { cash -= D().labUps[c.a].price; labs[c.b].upgrades |= 1u << c.a; } break;
        case C_SELL_BUREAU: case C_SELL_FENCE: case C_SELL_ALL: {
            if (inDay) break;
            auto sell = [&](int i, bool fence) {
                Loot l = bay[i]; const LootDef& ld = D().loot[l.def]; int v = l.damaged ? l.value / 2 : l.value;
                if (fence) { int cat = Has(ld, "cursed") ? 4 : (Has(ld, "glowing") || ld.anomalous) ? 3 : (int)(std::hash<std::string>{}(ld.name) % 8); float mul = fenceRate.empty() ? 1.0f : fenceRate[cat] / 100.0f; if (ld.anomalous || Has(ld, "cursed")) mul *= 1.3f; int got = (int)(v * mul); cash += got; sold.push_back({ld.name, got, true, l.foundOn}); }
                else { credit += v; cash += (int)(v * D().bureauCash); sold.push_back({ld.name, v, false, l.foundOn}); }
                Emit(E_SALE, p.id, fence ? 1 : 0, l.foundOn, {}, (float)v, ld.name);
            };
            if (c.kind == C_SELL_ALL) { for (int i = (int)bay.size() - 1; i >= 0; i--) sell(i, c.a == 1); bay.clear(); }
            else if (c.a >= 0 && c.a < (int)bay.size()) { sell(c.a, c.kind == C_SELL_FENCE); bay.erase(bay.begin() + c.a); }
            break;
        }
        case C_CONTRACT: break;
        case C_INSERT: if (!inDay && c.a >= 0 && c.a < (int)labs.size() && labs[c.a].online && labs[c.a].level != 12) insertion = c.a; break;
        case C_START_DAY: {
            if (inDay || failed) break;
            if (day > D().daysPerWeek) {   // the week's end: the quota
                if (credit >= quota) { NextWeek(); }
                else { failed = true; Emit(E_SHUTDOWN, -1, -1, -1, {}, (float)credit); break; }
            }
            BeginDay(); break;
        }
        case C_PORTAL: { LabState* lab = LabAt(p.level, p.p); if (!inDay || !lab || !lab->online || lab->fuel <= 0 || lab->cooldown > 0 || overtime) break; if (!lab->charging) { lab->charging = true; Emit(E_PORTAL_START, p.id, -1, p.level, p.p); } break; }
        case C_PORTAL_PAUSE: { LabState* lab = LabAt(p.level, p.p); if (lab) lab->charging = !lab->charging && lab->charge > 0; break; }
        case C_LOCK: { LabState* lab = LabAt(p.level, p.p); if (lab && lab->online && lab->fuel > 0) lab->locked = !lab->locked; break; }
        case C_JUMP: { LabState* lab = LabAt(p.level, p.p); if (!inDay || overtime || !lab || !lab->online || lab->fuel <= 0 || p.jumpCool > 0 || c.a < 0 || c.a >= (int)labs.size() || !labs[c.a].online || labs[c.a].fuel <= 0 || &labs[c.a] == lab || lab->level == 12 || labs[c.a].level == 12) break; lab->jumpFor = p.id; lab->jumpTo = c.a; lab->jumpT = 10; break; }
        case C_SLEEP: break;
        case C_TRADE: {   // a Faceling trader: your cheapest pocket thing for almond water; the Innkeeper: pay for drinks ($20) and he calms
            for (auto& e : ents) {
                if (e.level != p.level || Vector3Distance(e.p, p.p) > 2.8f) continue; const std::string& id = D().entities[e.def].id;
                if (id == "faceling" && e.mimicOf == -2) { int k = p.pocket[0].def >= 0 && (p.pocket[1].def < 0 || p.pocket[0].value <= p.pocket[1].value) ? 0 : 1; if (p.pocket[k].def < 0) break; int alm = ItemIndex("almond"); bool given = false; for (int t = 0; t < p.toolSlots && alm >= 0; t++) if (p.tools[t].item < 0) { p.tools[t] = {alm, 1, 0}; given = true; break; } if (given) { p.pocket[k] = Loot{}; Emit(E_TRADE, p.id, e.def, p.level, e.p); } break; }
                if (id == "innkeeper" && cash >= 20) { cash -= 20; e.st = ES_IDLE; Emit(E_TRADE, p.id, e.def, p.level, e.p, 1); break; }
            }
            break;
        }
        case C_COSMETIC: { int k = c.b; if (k < -1 || k >= (int)D().cosmetics.size()) break; switch (c.a) { case 0: p.hat = k; break; case 1: p.vest = k; break; case 2: p.lamp_c = k; break; case 3: p.suitCos = k; break; default: p.costume = k; break; } break; }
        case C_SIREN: { LabState* lab = LabAt(p.level, p.p); if (lab && lab->online && ((lab->upgrades >> 6) & 1)) { lab->sirenT = 40; Emit(E_TELL, p.id, -1, p.level, p.p, 6, "the Siren wails across the level"); } break; }
        case C_CARGO: { LabState* lab = LabAt(p.level, p.p); if (!lab || !lab->online || !((lab->upgrades >> 5) & 1) || lab->cargoCool > 0 || c.a < 0 || c.a >= (int)labs.size() || !labs[c.a].online || &labs[c.a] == lab) break; for (auto& l : lab->crate) labs[c.a].crate.push_back(l); lab->crate.clear(); lab->cargoCool = 50; Emit(E_TELL, p.id, -1, p.level, p.p, 3, "the cargo link hums: the crate is somewhere else now"); break; }
        case C_GRAB: {   // the social-deduction mode's Skin-Stealer takes a teammate (one per level)
            if (!p.impostor || p.takenOnLevel == p.level || !inDay) break;
            for (auto& o : crew) if (o.id != p.id && o.st == PS_ALIVE && o.level == p.level && Vector3Distance(o.p, p.p) < 1.8f) { o.st = PS_TAKEN; o.deaths++; o.lastCause = "taken by someone wearing a friend"; p.takenOnLevel = p.level; Emit(E_TAKEN, o.id, -1, o.level, o.p, 0, "a Skin-Stealer"); break; }
            break;
        }
        case C_ACCEPT: {   // a Partygoer's invitation: off to the Level 5 ballroom, pockets emptied
            if (p.stayPromptT > 0 && p.level == 18) { p.st = PS_SURFACE; p.hands = Loot{}; p.pocket[0] = p.pocket[1] = Loot{}; Emit(E_TAKEN, p.id, -1, 18, p.p, 4, "they had a lovely time"); break; }
            for (auto& e : ents) if (e.level == p.level && D().entities[e.def].id == "partygoer" && Vector3Distance(e.p, p.p) < 4) { p.pocket[0] = p.pocket[1] = Loot{}; p.hands = Loot{}; Transit(p, 5, true, "the party"); Level& lv = L(5); p.p = lv.Center(lv.w / 2, lv.h / 2); break; }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- the clock
void World::Step() {
    if (!inDay) { for (auto& p : crew) { for (auto& c : p.cmds) Command(p, c); p.cmds.clear(); } return; }
    float gameRate = (D().dayEnd - D().dayStart) / dayLen();   // in-game minutes per real second
    clock += STEP * gameRate; hourT += STEP * gameRate;
    if (!overtime && clock >= D().dayEnd) { overtime = true; Emit(E_OVERTIME); for (auto& l : labs) { l.charging = false; l.charge = 0; } }
    // Level 1's lights-out periods; Level 16's lockdowns
    if (lightsOutT > 0) lightsOutT -= STEP; else if (Rand() < STEP / 240) lightsOutT = 120 + Rand() * 180;
    lockdownNext -= STEP; if (lockdownNext <= 0) { lockdownT = 60; lockdownNext = 240 + Rand() * 120; Emit(E_LOCKDOWN, -1, -1, 16); } lockdownT = std::max(0.0f, lockdownT - STEP);
    // Level 1's supply crates: they appear and vanish when nobody's looking
    if (levels.count(1) && Rand() < STEP / 45) {
        Level& lv = levels[1]; int band = D().levels[1].lootBand; std::vector<int> pool; for (int i = 0; i < (int)D().loot.size(); i++) if (D().loot[i].level == band) pool.push_back(i);
        for (int tries = 0; tries < 30 && !pool.empty(); tries++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.At(x, z) != T_FLOOR) continue; Vector3 at = lv.Center(x, z); bool seenNow = false; for (const auto& p : crew) if (p.level == 1 && p.Alive() && (Vector3Distance(p.p, at) < 20 || LineOfSight(1, p.Eye(), Vector3Add(at, {0, 0.5f, 0})))) seenNow = true; if (seenNow) continue; WorldItem wi; wi.level = 1; wi.p = at; wi.loot.def = pool[RandI((int)pool.size())]; wi.loot.value = D().loot[wi.loot.def].min + RandI(D().loot[wi.loot.def].max - D().loot[wi.loot.def].min + 1); wi.loot.foundOn = 1; wi.loot.uid = nextUid++; items.push_back(wi); break; }
    }
    for (auto& p : crew) if (p.present) StepPlayer(p);
    // dropped lights and toys burn down
    for (auto& w : items) w.noiseT = std::max(0.0f, w.noiseT - STEP);
    items.erase(std::remove_if(items.begin(), items.end(), [](const WorldItem& w) { return w.loot.def < 0 && w.noiseT <= 0; }), items.end());
    StepEntities(); StepLabs();
    // the map: what the crew has seen
    // (a coarse share: each living crew member reveals the cells round them)
    // the morning after Overtime: those in a lit Lab are pulled out with its crate (it counts for the next day)
    if (overtime && clock >= D().dayEnd + D().dayStart) {
        for (auto& l : labs) if (l.online && l.fuel > 0) { bool someone = false; for (const auto& p : crew) if (InPortalHall(p, l)) someone = true; if (someone) { if (contract >= 0 && D().contracts[contract].id == "nightshift") { int v = 0; for (auto& c : l.crate) v += c.value; if (v >= 300) contractDone = true; } Extract(l); } }
        for (auto& p : crew) if (p.Alive()) Kill(p, "the night");
        if (inDay) EndDay(false);
        return;
    }
    // everyone gone (dead, taken or up): the day ends; if nobody made it, the death fee
    bool anyAlive = false, anyUp = false; for (const auto& p : crew) { if (p.st == PS_ALIVE || p.st == PS_DOWNED) anyAlive = true; if (p.st == PS_SURFACE) anyUp = true; }
    if (!anyAlive) { if (mode == 1 && !won) { failed = true; memo = "Lost: nobody found the way out."; } if (!anyUp) cash -= (int)(cash * D().deathFee); EndDay(anyUp); }
}


}  // namespace nc
