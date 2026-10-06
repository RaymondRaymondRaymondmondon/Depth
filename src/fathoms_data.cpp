// Fathoms' balance: data/fathoms/fathoms_balance.json loaded once (the doc's playtest plan: "put every number in this
// document in one data file that the game loads at startup, so tuning never requires recompiling").
#include "fathoms.h"
#include "json.h"
#include "redtide.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace fa {

static Cost ReadCost(const Json& j) { Cost c{}; for (int i = 0; i < R_COUNT && i < (int)j.Size(); i++) c[i] = j[i].F(0); return c; }
static uint32_t TagOf(const std::string& s) {
    static const std::pair<const char*, uint32_t> T[] = {{"infantry", TG_INFANTRY}, {"ranged", TG_RANGED}, {"large", TG_LARGE}, {"mounted", TG_MOUNTED}, {"siege", TG_SIEGE}, {"ship", TG_SHIP}, {"light", TG_LIGHT},
        {"heavy", TG_HEAVY}, {"air", TG_AIR}, {"worker", TG_WORKER}, {"hero", TG_HERO}, {"ultimate", TG_ULTIMATE}, {"torpedo", TG_TORPEDO}, {"support", TG_SUPPORT}, {"building", TG_BUILDING}};
    for (auto& t : T) if (s == t.first) return t.second;
    return 0;
}
static DmgType TypeOf(const std::string& s) { return s == "melee" ? D_MELEE : s == "pierce" ? D_PIERCE : s == "blast" ? D_BLAST : s == "heal" ? D_HEAL : D_NONE; }
static UnitDef ReadUnit(const std::string& key, const Json& u, int faction) {
    UnitDef d; d.key = key; d.name = u["name"].Str0(key); d.from = u["from"].Str0(""); d.era = u["era"].I(0); d.faction = faction;
    d.cost = ReadCost(u["cost"]); d.pop = u["pop"].I(1); d.hp = u["hp"].F(50); d.attack = u["attack"].F(0); d.type = TypeOf(u["type"].Str0("none"));
    d.reload = u["reload"].F(2); d.range = u["range"].F(0.5f); d.minRange = u["min_range"].F(0); d.speed = u["speed"].F(1); d.waterSpeed = u["water_speed"].F(0);
    d.armor[0] = u["armor"][0].F(0); d.armor[1] = u["armor"][1].F(0);
    for (size_t i = 0; i < u["tags"].Size(); i++) d.tags |= TagOf(u["tags"][i].Str0());
    for (const auto& kv : u["bonus"].o) d.bonus.push_back({TagOf(kv.first == "building" ? "building" : kv.first), kv.second.F(0)});
    d.train = u["train"].F(20); d.vision = u["vision"].F(6); d.upkeep = u["upkeep"].F(0); d.heal = u["heal"].F(0); d.carry = u["carry"].I(0); d.carryWorkers = u["carry_workers"].I(0);
    d.shore = u["shore"].F(0); d.life = u["life"].F(0); d.aura = u["aura"].F(0); d.special = u["special"].Str0("");
    d.move = u["air"].Bool0(false) ? MV_AIR : u["naval"].Bool0(false) ? MV_SEA : (d.special == "amphibious" ? MV_AMPHIB : MV_LAND);
    return d;
}

static Balance LoadBalance() {
    Balance b; Json j = LoadJsonFile(rt::DataDir() + "/../fathoms/fathoms_balance.json");
    const Json& r = j["resources"];
    for (size_t i = 0; i < r["names"].Size(); i++) b.resNames.push_back(r["names"][i].Str0());
    for (int i = 0; i < R_COUNT; i++) b.weights[i] = r["weights"][i].F(b.weights[i]);
    static const char* NK[N_COUNT] = {"fish", "grove", "kelp", "brass", "wreck", "coal", "vent", "pearl", "farm"};
    static const char* GK[N_COUNT] = {"fish", "grove", "kelp", "seam", "wreck", "coal", "vent", "pearl", "farm"};
    for (int k = 0; k < N_COUNT; k++) {
        const Json& n = r["nodes"][NK[k]]; NodeDef& d = b.nodes[k];
        d.amount = n["amount"].F(k == N_FARM ? 1e9f : 400); d.regrowEvery = n["regrow_every"].F(0); d.water = n["water"].Bool0(false); d.rate = r["gather"][GK[k]].F(0.4f);
        std::string kind = n["kind"].Str0(k == N_FARM ? "food" : "food"); d.res = kind == "brass" ? R_BRASS : kind == "coal" ? R_COAL : kind == "ichor" ? R_ICHOR : kind == "doubloons" ? R_DOUB : R_FOOD;
    }
    b.nodes[N_FARM].res = R_FOOD; b.nodes[N_FARM].amount = 1e9f;
    b.homeCoal = r["nodes"]["coal"]["home_amount"].F(800); b.volcanoVent = r["nodes"]["vent"]["volcano_amount"].F(3000); b.volcanoRate = r["nodes"]["vent"]["volcano_rate_mult"].F(1.6f); b.wreckDoub = r["nodes"]["wreck"]["doubloons"].F(30);
    b.carry[R_FOOD] = r["carry"]["food"].F(10); b.carry[R_BRASS] = r["carry"]["brass"].F(10); b.carry[R_COAL] = r["carry"]["coal"].F(8); b.carry[R_ICHOR] = r["carry"]["ichor"].F(5); b.carry[R_DOUB] = r["carry"]["doubloons"].F(5);
    b.start[0] = ReadCost(r["start"]["standard"]); b.start[1] = ReadCost(r["start"]["high"]); b.start[2] = ReadCost(r["start"]["very_high"]);
    b.startWorkers = r["start_units"]["workers"].I(4); b.startBoats = r["start_units"]["fishing_boats"].I(1); b.startSkiffs = r["start_units"]["scout_skiffs"].I(1);
    const Json& p = j["population"]; b.popHarbor = p["harbor"].I(10); b.popCottage = p["cottage"].I(5); b.popColony = p["colony_hall"].I(8); b.cap2 = p["cap_2p"].I(100); b.cap4 = p["cap_4p"].I(80); b.cap6 = p["cap_6p"].I(60);
    b.emptySpeed = j["upkeep"]["empty_speed"].F(0.6f); b.emptyDamage = j["upkeep"]["empty_damage"].F(0.75f);
    const Json& ex = j["exchange"]; b.exBaseOut = ex["base_out"].F(70); b.exPer = ex["per_trade"].F(100); b.exStep = ex["step"].F(0.03f); b.exDrift = ex["drift"].F(0.01f); b.exDriftEvery = ex["drift_every"].F(20); b.exSell = ex["sell_doubloons"].F(15);
    b.buildMinFrac = j["build"]["min_time_frac"].F(0.3333f);
    for (int e = 0; e < 3 && e < (int)j["eras"].Size(); e++) { const Json& er = j["eras"][e]; b.eraCost[e] = ReadCost(er["cost"]); b.eraTime[e] = er["time"].F(90); for (size_t i = 0; i < er["needs_two_of"].Size(); i++) b.eraNeeds[e].push_back(er["needs_two_of"][i].Str0()); }
    for (const auto& kv : j["buildings"].o) {
        const Json& bd = kv.second; BuildingDef d; d.key = kv.first; d.era = bd["era"].I(0); d.cost = ReadCost(bd["cost"]); d.hp = bd["hp"].F(1000); d.time = bd["time"].F(30); d.size = bd["size"].I(2);
        d.pop = bd["pop"].I(0); d.garrison = bd["garrison"].I(0); d.max = bd["max"].I(0); d.drop = bd["drop"].Str0(""); d.vision = bd["vision"].F(5); d.attack = bd["attack"].F(0); d.range = bd["range"].F(0);
        d.claim = bd["claim"].F(0); d.coalRange = bd["coal_range"].F(0); d.bonusShips = bd["bonus_ships"].F(0); d.score = bd["score"].F(0); d.coast = bd["coast"].Bool0(false); d.wall = bd["wall"].Bool0(false);
        d.relics = bd["relics"].Bool0(false); d.antiAir = bd["anti_air"].Bool0(false); d.foodPerSec = bd["food_per_sec"].F(0); d.auraR = bd["aura_radius"].F(0);
        { std::string fk = bd["faction"].Str0(""); for (size_t fi = 0; fi < j["factions"].Size(); fi++) if (j["factions"][fi]["key"].Str0() == fk) d.faction = (int)fi; }
        for (size_t i = 0; i < bd["trains"].Size(); i++) d.trains.push_back(bd["trains"][i].Str0());
        b.buildings.push_back(d);
    }
    for (const auto& kv : j["units"].o) if (kv.first[0] != '_') b.units.push_back(ReadUnit(kv.first, kv.second, -1));
    for (const auto& kv : j["techs"].o) {
        const Json& t = kv.second; TechDef d; d.key = kv.first; d.name = kv.first; d.at = t["at"].Str0("harbor"); d.era = t["era"].I(0); d.cost = ReadCost(t["cost"]); d.time = t["time"].F(30);
        for (const auto& f : t["fx"].o) d.fx.push_back({f.first, f.second.F(0)});
        b.techs.push_back(d);
    }
    { const Json& fg = j["forge"]; int line = 0;
      for (const auto& kv : fg["lines"].o) {
          for (int lv = 0; lv < 3 && lv < (int)fg["levels"].Size(); lv++) {
              TechDef d; d.key = kv.first + "_" + std::to_string(lv + 1); d.name = kv.first + " " + std::to_string(lv + 1); d.at = kv.second["at"].Str0("workshop");
              d.era = fg["levels"][lv]["era"].I(1); d.cost = ReadCost(fg["levels"][lv]["cost"]); d.time = 40 + 10 * lv; d.forgeLine = line; d.forgeLevel = lv + 1;
              for (const auto& f : kv.second["fx"].o) { if (f.first.find("_l3") != std::string::npos && lv < 2) continue; d.fx.push_back({f.first, f.second.F(0)}); }
              b.techs.push_back(d);
          }
          line++;
      } }
    for (size_t fi = 0; fi < j["factions"].Size(); fi++) {
        const Json& f = j["factions"][fi]; FactionDef d; d.key = f["key"].Str0(); d.name = f["name"].Str0(); d.style = f["style"].Str0(); d.mechanic = f["mechanic"].Str0(); d.weakness = f["weakness"].Str0();
        d.ultimate = f["ultimate"].Str0(); d.wonder = f["wonder"].Str0(); d.costMult = f["cost_mult"].F(1); d.landSpeedMult = f["land_speed_mult"].F(1); d.foodToCoal = f["food_to_coal"].F(0);
        for (const auto& g : f["gather_mult"].o) { static const char* GK2[N_COUNT] = {"fish", "grove", "kelp", "brass", "wreck", "coal", "vent", "pearl", "farm"}; for (int k = 0; k < N_COUNT; k++) if (g.first == GK2[k]) d.gatherMult[k] = g.second.F(1); }
        for (int e = 0; e < 3; e++) d.eraGather[e] = f["era_gather_mult"][e].F(1);
        const Json& h = f["hero"]; d.heroName = h["name"].Str0("Hero"); d.heroAura = h["aura"].Str0(""); d.heroAuraAmount = h["aura_amount"].F(0); d.heroActive = h["active"].Str0(""); d.heroCooldown = h["cooldown"].F(60); d.heroHp = h["hp"].F(500);
        for (size_t u = 0; u < f["uniques"].Size(); u++) { const Json& uu = f["uniques"][u]; b.units.push_back(ReadUnit(uu["key"].Str0(), uu, (int)fi)); d.uniques.push_back((int)b.units.size() - 1); }
        for (size_t t = 0; t < f["techs"].Size(); t++) { const Json& tt = f["techs"][t]; TechDef td; td.key = tt["key"].Str0(); td.name = tt["name"].Str0(); td.at = td.era >= 2 ? "harbor" : "harbor"; td.era = tt["era"].I(1); td.cost = ReadCost(tt["cost"]); td.time = 45; td.faction = (int)fi; td.fx.push_back({td.key, 1}); b.techs.push_back(td); d.techs.push_back((int)b.techs.size() - 1); }
        for (size_t c = 0; c < f["cards"].Size(); c++) d.cards.push_back(f["cards"][c].Str0());
        b.factions.push_back(d);
    }
    const Json& c = j["combat"]; b.splashR = c["splash_radius"].F(1.5f); b.splashFrac = c["splash_frac"].F(0.5f); b.landingWindow = c["landing_window"].F(10); b.landingMult = c["landing_mult"].F(1.25f);
    b.wadingMult = c["wading_mult"].F(1.2f); b.shallowLand = c["shallow_land_speed"].F(0.5f); b.shallowShip = c["shallow_ship_speed"].F(0.7f); b.jungleRanged = c["jungle_ranged_mult"].F(0.75f); b.jungleHidden = c["jungle_hidden"].F(4);
    b.highDeal = c["high_ground_deal"].F(1.25f); b.highTake = c["high_ground_take"].F(0.85f); b.hillTowerRange = c["hill_tower_range"].F(2); b.lavaDps = c["lava_dps"].F(40); b.currentShip = c["current_ship_speed"].F(1.5f);
    const Json& m = j["morale"]; b.moraleHome = m["home_gain"].F(5); b.moraleAway = m["away_loss"].F(2); b.moraleEnemy = m["enemy_mult"].F(2); b.moraleEvery = m["every"].F(10); b.low1 = m["low1"].F(50);
    b.low1Dmg = m["low1_damage"].F(0.9f); b.low2 = m["low2"].F(25); b.low2Dmg = m["low2_damage"].F(0.75f); b.low2Speed = m["low2_speed"].F(0.9f); b.coalRange = m["coal_range"].F(30); b.noCoalSpeed = m["no_coal_speed"].F(0.75f);
    const Json& mp = j["map"];
    for (size_t i = 0; i < mp["sizes"].Size(); i++) { const Json& s = mp["sizes"][i]; Balance::Size z; z.name = s["name"].Str0(); z.players = s["players"].I(2); z.grid = s["grid"].I(128); z.islands = s["islands"].I(12); z.volcanoes = s["volcanoes"].I(1); z.coves = s["coves"].I(1); z.tribes = s["tribes"].I(2); z.ruins = s["ruins"].I(3); z.coal = s["coal"].I(4); b.sizes.push_back(z); }
    b.weatherEvery = mp["weather_every"].F(180); b.weatherP[0] = mp["weather"]["calm"].F(0.6f); b.weatherP[1] = mp["weather"]["fog"].F(0.2f); b.weatherP[2] = mp["weather"]["storm"].F(0.15f); b.weatherP[3] = mp["weather"]["whirlpool"].F(0.05f);
    b.stormDps = mp["storm_dps"].F(2); b.stormSpeed = mp["storm_speed"].F(0.6f); b.whirlTime = mp["whirlpool_time"].F(45);
    const Json& n = j["neutral"];
    for (int i = 0; i < 3; i++) b.neutralStrength[i] = n["strength"][i].F(b.neutralStrength[i]);
    const Json& cv = n["cove"]; b.coveHp = cv["hp"].F(4000); b.covePirates = cv["pirates"].I(8); b.coveCannons = cv["cannons"].I(4); b.coveCannon = cv["cannon_attack"].F(14); b.coveRange = cv["cannon_range"].F(8);
    b.contractEvery = cv["contract_every"].F(90); b.raidWarning = cv["warning"].F(20); b.pirateRaidEvery = cv["raid_every"].F(240); b.blackbeardHp = cv["blackbeard_hp"].F(1500);
    for (size_t i = 0; i < cv["deals"].Size(); i++) { const Json& d = cv["deals"][i]; Balance::Deal dl; dl.key = d["key"].Str0(); dl.era = d["era"].I(0); dl.price = d["price"].F(150); dl.time = d["time"].F(120); dl.sloops = d["sloops"].I(0); dl.gunboats = d["gunboats"].I(0); dl.ironclads = d["ironclads"].I(0); dl.pirates = d["pirates"].I(0); b.deals.push_back(dl); }
    b.bmSell = cv["black_market"]["sell_per_100"].F(20); b.bmBuy = cv["black_market"]["buy_per_100"].F(60); b.bmIchor = cv["black_market"]["ichor_per_40"].F(100);
    const Json& tr = n["tribe"]; b.tribeHp = tr["hp"].F(3000); b.tribeWarriors = tr["warriors"].I(8); b.tribeThrowers = tr["throwers"].I(4); b.tribeWake = tr["wake"].F(360); b.tribeRaidEvery = tr["raid_every"].F(300);
    for (int i = 0; i < 3; i++) b.raidSize[i] = tr["raid_size"][i].I(b.raidSize[i]);
    b.tribeGrowth = tr["growth_every"].F(60); b.tribeCap = tr["cap"].I(16); b.tribute = Cost{tr["tribute"][0].F(200), tr["tribute"][1].F(100), 0, 0, 0}; b.tribeTributeEvery = tr["tribute_every"].F(300);
    b.tribeLoot = ReadCost(tr["loot"]); b.relicChance = tr["relic_chance"].F(0.25f); b.kinship = ReadCost(tr["kinship_cost"]); b.ritualTime = tr["ritual_time"].F(45); b.allyEvery = tr["ally_warrior_every"].F(60); b.allyCap = tr["ally_cap"].I(6);
    const Json& v = n["volcano"]; b.volcanoWake = v["wake"].F(600); b.eruptEvery = v["erupt_every"].F(240); b.eruptWarning = v["warning"].F(30); b.lavaTime = v["lava_time"].F(40); b.ash = v["ash"].F(60);
    b.sunGodHp = v["sun_god_hp"].F(4000); b.sunGodBeam = v["sun_god_beam"].F(60); b.imps = v["imps"].I(6); b.sunGodReform = v["reform"].F(480); b.altarHold = v["altar_hold"].F(45); b.altarIchor = v["altar_ichor"].F(1);
    b.krakenWake = n["kraken"]["wake"].F(1200); b.krakenHp = n["kraken"]["hp"].F(6000); b.krakenDps = n["kraken"]["dps"].F(30); b.krakenReward = ReadCost(n["kraken"]["reward"]);
    b.ghostWake = n["ghost_ship"]["wake"].F(900); b.ghostHp = n["ghost_ship"]["hp"].F(1200); b.ghostReward = n["ghost_ship"]["reward_doubloons"].F(400); b.wreckTrap = n["wreck_trap"].F(0.2f); b.ruinSentinels = n["ruin_sentinels"].I(4);
    const Json& dp = j["diplomacy"]; b.ceasefire = dp["ceasefire"].F(180); b.tributeFee = dp["tribute_fee"].F(0.1f); b.betrayMorale = dp["betray_morale"].F(15); b.betrayPause = dp["betray_trade_pause"].F(60); b.allianceAge = dp["alliance_min_age"].F(300); b.tradePerTile = dp["trade_per_tile"].F(0.15f);
    b.conquestGrace = j["victory"]["conquest_grace"].F(90); b.relicCountdown = j["victory"]["relic_countdown"].F(180); b.volcanoCountdown = j["victory"]["volcano_countdown"].F(240);
    const Json& sc = j["score"]; b.scIsland = sc["island"].F(20); b.scRelic = sc["relic"].F(40); b.scKill = sc["kill_per"].F(10); b.scRaze = sc["raze_per"].F(20); b.scGather = sc["gather_per"].F(100);
    for (int e = 0; e < 3; e++) b.scEra[e] = sc["era"][e].F(b.scEra[e]);
    b.scTech = sc["tech"].F(5); b.scWonder = sc["wonder"].F(150); b.scBoss[0] = sc["blackbeard"].F(50); b.scBoss[1] = sc["sun_god"].F(60); b.scBoss[2] = sc["kraken"].F(80); b.scBoss[3] = sc["tribe"].F(25);
    for (size_t i = 0; i < j["ai"].Size(); i++) { const Json& a = j["ai"][i]; Balance::Ai ai; ai.name = a["name"].Str0(); ai.reaction = a["reaction"].F(1); ai.steamAt = a["steam_at"].F(600); ai.attackAfter = a["attack_after"].F(0); ai.gatherMult = a["gather_mult"].F(1); b.ai.push_back(ai); }
    if (b.sizes.empty()) b.sizes.push_back({});
    if (b.ai.empty()) b.ai.push_back({});
    return b;
}
static Balance& Store() { static Balance b = LoadBalance(); return b; }
const Balance& B() { return Store(); }
Balance& BMut() { return Store(); }
int Balance::Unit(const std::string& k) const { for (size_t i = 0; i < units.size(); i++) if (units[i].key == k) return (int)i; return -1; }
int Balance::Building(const std::string& k) const { for (size_t i = 0; i < buildings.size(); i++) if (buildings[i].key == k) return (int)i; return -1; }
int Balance::Tech(const std::string& k) const { for (size_t i = 0; i < techs.size(); i++) if (techs[i].key == k) return (int)i; return -1; }
const char* ResName(int r) { static const char* N[R_COUNT] = {"Food", "Brass", "Coal", "Ichor", "Doubloons"}; return N[std::clamp(r, 0, R_COUNT - 1)]; }
const char* FactionName(int f) { return f >= 0 && f < (int)B().factions.size() ? B().factions[f].name.c_str() : "?"; }
uint32_t BalanceHash() {
    std::ifstream f(rt::DataDir() + "/../fathoms/fathoms_balance.json", std::ios::binary); uint32_t h = 2166136261u; char ch;
    while (f.get(ch)) if (ch != 13) { h ^= (uint8_t)ch; h *= 16777619u; }
    return h;
}

}  // namespace fa
