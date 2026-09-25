// ============================================================================
//  DEPTH - the roguelike expedition: rooms, the flashlight, and combat.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
static bool Chance(int pct) { return GetRandomValue(1, 100) <= pct; }

static void Log(Game& g, const std::string& s) {
    auto& L = g.dungeon.log;
    L.push_back(s);
    while (L.size() > 5) L.erase(L.begin());
}

// ---------------------------------------------------------------- light
static float StressMult(const Game& g) { float l = g.dungeon.light; return l >= 50 ? 1.0f : l > 0 ? 1.4f : 1.8f; }
static float LootMult(const Game& g) { float l = g.dungeon.light; return l >= 50 ? 1.0f : l > 0 ? 1.4f : 1.8f; }
static int EnemyAccBonus(const Game& g) { float l = g.dungeon.light; return l >= 50 ? 0 : l > 0 ? 6 : 12; }
static int HeroAccBonus(const Game& g) { return g.dungeon.light >= 75 ? 5 : 0; }
static const char* LightName(float l) { return l >= 75 ? "Bright" : l >= 50 ? "Dim" : l > 0 ? "Murky" : "Pitch Black"; }

// ---------------------------------------------------------------- lookups
static int PartySize(Game& g) { int n = 0; for (int id : g.party) if (id >= 0) n++; return n; }
static Hero* PartyAt(Game& g, int pos) { return (pos < 0 || pos >= PARTY_SIZE) ? nullptr : FindHero(g, g.party[pos]); }
static int PartyPos(Game& g, int id) { for (int i = 0; i < PARTY_SIZE; i++) if (g.party[i] == id) return i; return -1; }
static Enemy* FindEnemy(Game& g, int uid) { for (auto& e : g.dungeon.enemies) if (e.uid == uid) return &e; return nullptr; }
static int EnemyPos(Game& g, int uid) {
    for (size_t i = 0; i < g.dungeon.enemies.size(); i++) if (g.dungeon.enemies[i].uid == uid) return (int)i;
    return -1;
}

// ---------------------------------------------------------------- layout
// Heroes stand on the left with rank 1 closest to the middle; enemies mirror them on the right.
static Rectangle HeroRect(int pos) { float cx = 520 - pos * 125.0f; return {cx - 45, 300, 90, 160}; }
static Rectangle EnemyRect(Game& g, int pos) {
    float cx = 760 + pos * 125.0f;
    if (pos >= 0 && pos < (int)g.dungeon.enemies.size() && g.dungeon.enemies[pos].boss) return {cx - 58, 250, 116, 210};
    return {cx - 45, 330, 90, 130};
}

static void Float(Game& g, Rectangle r, const std::string& t, Color c) {
    float x = r.x + r.width / 2;
    int stack = 0;
    for (auto& f : g.dungeon.floats) if (fabsf(f.pos.x - x) < 2 && f.life > 0.75f) stack++;
    g.dungeon.floats.push_back({{x, r.y - 16 - stack * 22.0f}, t, c, 1.2f});
}

// ---------------------------------------------------------------- hero effects
static void AddStress(Game& g, Hero& h, int amount) {
    if (amount > 0) {
        amount = (int)std::round(amount * StressMult(g) * (100 - GetStats(h).stressResist) / 100.0f);
        if (amount <= 0) return;
    }
    int before = h.stress;
    h.stress = std::clamp(h.stress + amount, 0, 100);
    int diff = h.stress - before;
    int pos = PartyPos(g, h.id);
    if (diff != 0 && pos >= 0) Float(g, HeroRect(pos), TextFormat("%+d nerves", diff), Pal::Stress);
    if (h.stress >= 100 && !h.rattled) {
        h.rattled = true;
        Log(g, h.name + " is RATTLED! (less accurate, may freeze up)");
    }
}

static void DamageHero(Game& g, Hero& h, int dmg) {
    if (dmg <= 0) return;
    if (h.deathsDoor) {
        if (Chance(35)) { h.dead = true; Log(g, h.name + " has been lost to the depths."); }
        else Log(g, h.name + " clings on at Death's Door!");
        return;
    }
    h.hp -= dmg;
    if (h.hp <= 0) {
        h.hp = 0;
        h.deathsDoor = true;
        Log(g, h.name + " is at DEATH'S DOOR!");
        AddStress(g, h, 10);
    }
}

// Poison halves healing, which is what makes it different from bleed.
static int HealHero(Hero& h, int amt) {
    if (h.st.poisonTurns > 0) amt = std::max(1, amt / 2);
    h.hp = std::min(GetStats(h).maxHp, h.hp + amt);
    if (h.hp > 0) h.deathsDoor = false;
    return amt;
}

// Poison stacks up to three applications; bleed just refreshes.
static void ApplyPoison(Status& st, int dmg) {
    st.poisonDmg = st.poisonTurns > 0 ? std::min(st.poisonDmg + dmg, dmg * 3) : dmg;
    st.poisonTurns = 3;
}

// Remove defeated enemies and fallen crew. The fallen take their relics with them.
static void Cleanup(Game& g) {
    auto& en = g.dungeon.enemies;
    en.erase(std::remove_if(en.begin(), en.end(), [](const Enemy& e) { return !e.alive; }), en.end());
    bool anyDead = false;
    for (auto& h : g.roster) if (h.dead) anyDead = true;
    if (!anyDead) return;
    for (auto& h : g.roster)
        if (h.dead) for (auto& id : g.party) if (id == h.id) id = -1;
    g.roster.erase(std::remove_if(g.roster.begin(), g.roster.end(), [](const Hero& h) { return h.dead; }), g.roster.end());
    CompactParty(g);
}

static void MoveEnemy(Game& g, int uid, int delta) {
    auto& en = g.dungeon.enemies;
    int i = EnemyPos(g, uid);
    if (i < 0) return;
    int j = std::clamp(i + delta, 0, (int)en.size() - 1);
    if (i == j) return;
    Enemy e = en[i];
    en.erase(en.begin() + i);
    en.insert(en.begin() + j, e);
    Log(g, e.name + (delta > 0 ? " is shoved back." : " is dragged forward."));
}

// ---------------------------------------------------------------- targeting
static std::vector<int> ValidTargets(Game& g, int heroPos, const Ability& a) {
    std::vector<int> v;
    if (a.target == Target::Enemy) {
        for (int i = 0; i < (int)g.dungeon.enemies.size() && i < 4; i++)
            if (a.hits & (1 << i)) v.push_back(i);
    } else if (a.target == Target::Ally) {
        for (int i = 0; i < PartySize(g); i++) {
            if (a.swapWithTarget && i == heroPos) continue;
            v.push_back(i);
        }
    } else {
        v.push_back(heroPos);
    }
    return v;
}

static bool HeroCanUse(Game& g, int heroPos, const Ability& a) {
    return (a.usableFrom & (1 << heroPos)) && !ValidTargets(g, heroPos, a).empty();
}

// ---------------------------------------------------------------- actions
static void HeroAct(Game& g, int heroId, int abilityIdx, int targetPos) {
    Hero* h = FindHero(g, heroId);
    if (!h) return;
    const Ability& a = ClassAbilities(h->cls)[abilityIdx];
    Stats s = GetStats(*h);
    int myPos = PartyPos(g, heroId);
    auto& d = g.dungeon;
    Log(g, h->name + ": " + a.name);

    if (a.target == Target::Enemy) {
        std::vector<int> uids;
        if (a.aoe) {
            for (int i = 0; i < (int)d.enemies.size() && i < 4; i++)
                if (a.hits & (1 << i)) uids.push_back(d.enemies[i].uid);
        } else if (targetPos >= 0 && targetPos < (int)d.enemies.size()) {
            uids.push_back(d.enemies[targetPos].uid);
        }
        for (int uid : uids) {
            for (int k = 0; k < a.hitsCount; k++) {
                Enemy* e = FindEnemy(g, uid);
                if (!e || !e->alive) break;
                Rectangle er = EnemyRect(g, EnemyPos(g, uid));
                int hit = std::clamp(s.acc + a.accBonus + HeroAccBonus(g) - e->dodge, 5, 95);
                if (!Chance(hit)) { Float(g, er, "Miss", Pal::Paper); continue; }
                bool crit = Chance(5);
                if (a.dmgMult > 0) {
                    float raw = Roll(s.dmgMin, s.dmgMax) * a.dmgMult * (1.0f + h->st.buffDmg / 100.0f);
                    if (crit) raw *= 1.5f;
                    if (e->st.marked > 0) raw *= 1.25f;
                    int dmg = std::max(1, (int)std::round(raw * (100 - e->prot) / 100.0f));
                    e->hp -= dmg;
                    Float(g, er, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Coral);
                    if (crit) {
                        Log(g, "Critical hit! The crew cheers.");
                        for (int p = 0; p < PARTY_SIZE; p++) if (Hero* o = PartyAt(g, p)) AddStress(g, *o, -4);
                    }
                    if (e->hp <= 0) { e->hp = 0; e->alive = false; Log(g, e->name + " is defeated."); break; }
                }
                if (a.bleed) { e->st.bleedDmg = std::max(e->st.bleedDmg, a.bleed); e->st.bleedTurns = 3; Float(g, er, "Bleed", Pal::Bad); }
                if (a.poison) { ApplyPoison(e->st, a.poison); Float(g, er, TextFormat("Poison %d", e->st.poisonDmg), Pal::Good); }
                if (a.mark) { e->st.marked = 3; Float(g, er, "Marked", Pal::Brass); }
                if (a.stunChance && Chance(a.stunChance)) { e->st.stunned = 1; Float(g, er, "Stunned", Pal::Teal); }
                if (a.moveTarget) MoveEnemy(g, uid, a.moveTarget);
            }
        }
        return;
    }

    std::vector<int> targets;
    if (a.target == Target::Ally) targets.push_back(targetPos);
    else if (a.target == Target::Self) targets.push_back(myPos);
    else for (int i = 0; i < PartySize(g); i++) targets.push_back(i);

    if (a.swapWithTarget && myPos >= 0 && targetPos >= 0) {
        std::swap(g.party[myPos], g.party[targetPos]);
        Log(g, "The Captain reorders the line.");
        targets = {myPos}; // the ally now stands where the Captain was
    }
    for (int p : targets) {
        Hero* t = PartyAt(g, p);
        if (!t) continue;
        Rectangle tr = HeroRect(p);
        if (a.heal) { int amt = HealHero(*t, a.heal + Roll(0, 2)); Float(g, tr, "+" + std::to_string(amt), Pal::Good); }
        if (a.cure) { t->st.bleedTurns = 0; t->st.poisonTurns = 0; }
        if (a.stressHeal) AddStress(g, *t, -a.stressHeal);
        if (a.buffDmg) { t->st.buffDmg = a.buffDmg; t->st.buffTurns = 3; Float(g, tr, "Rallied", Pal::Brass); }
        if (a.buffDodge) { t->st.dodgeBuff = a.buffDodge; t->st.dodgeTurns = 3; Float(g, tr, "Dodge up", Pal::Teal); }
        if (a.buffProt) { t->st.protBuff = a.buffProt; t->st.protTurns = 3; Float(g, tr, "Armor up", Pal::Brass); }
        if (a.guardTurns) { t->st.guardTurns = a.guardTurns; Float(g, tr, "Guarding", Pal::Teal); }
    }
}

static void EnemyAct(Game& g, int uid) {
    Enemy* e = FindEnemy(g, uid);
    int n = PartySize(g);
    if (!e || n == 0) return;

    std::vector<int> usable;
    for (int i = 0; i < (int)e->abilities.size(); i++)
        for (int p = 0; p < n; p++)
            if (e->abilities[i].hits & (1 << p)) { usable.push_back(i); break; }
    if (usable.empty()) { Log(g, e->name + " can't reach anyone and skitters about."); return; }
    const EnemyAbility& a = e->abilities[usable[Roll(0, (int)usable.size() - 1)]];

    std::vector<int> targets;
    if (a.aoe) {
        for (int p = 0; p < n; p++) if (a.hits & (1 << p)) targets.push_back(p);
    } else {
        int guard = -1;
        for (int p = 0; p < n; p++) if (Hero* h = PartyAt(g, p); h && h->st.guardTurns > 0) guard = p;
        if (guard >= 0 && a.dmgMult > 0) {
            targets.push_back(guard);
        } else {
            std::vector<int> opts;
            for (int p = 0; p < n; p++) if (a.hits & (1 << p)) opts.push_back(p);
            targets.push_back(opts[Roll(0, (int)opts.size() - 1)]);
        }
    }
    Log(g, e->name + ": " + a.name);

    for (int p : targets) {
        Hero* h = PartyAt(g, p);
        if (!h || h->dead) continue;
        Stats s = GetStats(*h);
        Rectangle hr = HeroRect(p);
        int dodge = s.dodge + (h->st.dodgeTurns > 0 ? h->st.dodgeBuff : 0);
        int hit = std::clamp(e->acc + EnemyAccBonus(g) - dodge, 5, 95);
        if (!Chance(hit)) { Float(g, hr, "Dodge", Pal::Paper); continue; }
        bool crit = Chance(6);
        if (a.dmgMult > 0) {
            int prot = std::min(80, s.prot + (h->st.guardTurns > 0 ? 25 : 0) + (h->st.protTurns > 0 ? h->st.protBuff : 0));
            float raw = Roll(e->dmgMin, e->dmgMax) * a.dmgMult * (crit ? 1.5f : 1.0f);
            int dmg = std::max(1, (int)std::round(raw * (100 - prot) / 100.0f));
            Float(g, hr, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Bad);
            DamageHero(g, *h, dmg);
            if (h->dead) continue;
        }
        int st = a.stress + (crit ? 10 : 0);
        if (st) AddStress(g, *h, st);
        if (a.bleed) { h->st.bleedDmg = std::max(h->st.bleedDmg, a.bleed); h->st.bleedTurns = 3; }
        if (a.poison) ApplyPoison(h->st, a.poison);
        if (a.stunChance && Chance(a.stunChance)) { h->st.stunned = 1; Float(g, hr, "Stunned", Pal::Teal); }
    }
}

// ---------------------------------------------------------------- turn flow
static void BeginRound(Game& g) {
    auto& d = g.dungeon;
    d.order.clear();
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) d.order.push_back({true, id, GetStats(*h).speed + Roll(0, 8)});
    for (auto& e : d.enemies) d.order.push_back({false, e.uid, e.speed + Roll(0, 8)});
    std::stable_sort(d.order.begin(), d.order.end(), [](const TurnEntry& a, const TurnEntry& b) { return a.init > b.init; });
    d.turnIdx = 0;
    d.round++;
    d.turnStarted = false;
}

static void RoomCleared(Game& g) {
    auto& d = g.dungeon;
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) { h->st = Status{}; }
    bool boss = d.rooms[d.roomIndex] == RoomType::Boss;
    d.roomGold = (int)(Roll(boss ? 60 : 20, boss ? 90 : 40) * LootMult(g));
    d.roomRelic = -1;
    if (boss && Chance(50)) { d.roomRelic = Roll(0, (int)Relics().size() - 1); d.lootRelics.push_back(d.roomRelic); }
    d.lootGold += d.roomGold;
    d.phase = boss ? DPhase::Victory : DPhase::RoomClear;
}

static void EndTurn(Game& g) {
    Cleanup(g);
    auto& d = g.dungeon;
    d.selectedAbility = -1;
    d.pendingSkip = false;
    d.turnStarted = false;
    if (PartySize(g) == 0) { d.phase = DPhase::Defeat; return; }
    if (d.enemies.empty()) { RoomCleared(g); return; }
    d.turnIdx++;
}

// Damage-over-time ticks, stuns and hesitation at the start of a unit's turn.
static void StartTurn(Game& g) {
    auto& d = g.dungeon;
    TurnEntry te = d.order[d.turnIdx];
    d.turnStarted = true;
    d.actTimer = 0;
    d.pendingSkip = false;
    d.selectedAbility = -1;
    auto skip = [&](const std::string& why) { if (!why.empty()) Log(g, why); d.pendingSkip = true; d.actTimer = 0.8f; };

    if (te.hero) {
        Hero* h = FindHero(g, te.id);
        Rectangle r = HeroRect(PartyPos(g, te.id));
        Status& st = h->st;
        if (st.bleedTurns > 0) { st.bleedTurns--; Float(g, r, "Bleed " + std::to_string(st.bleedDmg), Pal::Bad); DamageHero(g, *h, st.bleedDmg); }
        if (!h->dead && st.poisonTurns > 0) { st.poisonTurns--; Float(g, r, "Poison " + std::to_string(st.poisonDmg), Pal::Good); DamageHero(g, *h, st.poisonDmg); }
        if (st.buffTurns > 0 && --st.buffTurns == 0) st.buffDmg = 0;
        if (st.dodgeTurns > 0 && --st.dodgeTurns == 0) st.dodgeBuff = 0;
        if (st.protTurns > 0 && --st.protTurns == 0) st.protBuff = 0;
        if (st.guardTurns > 0) st.guardTurns--;
        if (h->dead) { skip(""); return; }
        if (st.stunned > 0) { st.stunned--; skip(h->name + " is stunned and loses the turn."); return; }
        if (h->rattled && Chance(20)) { skip(h->name + " freezes up, too rattled to act!"); return; }
    } else {
        Enemy* e = FindEnemy(g, te.id);
        Rectangle r = EnemyRect(g, EnemyPos(g, te.id));
        Status& st = e->st;
        if (st.bleedTurns > 0) { st.bleedTurns--; e->hp -= st.bleedDmg; Float(g, r, "Bleed " + std::to_string(st.bleedDmg), Pal::Bad); }
        if (st.poisonTurns > 0) { st.poisonTurns--; e->hp -= st.poisonDmg; Float(g, r, "Poison " + std::to_string(st.poisonDmg), Pal::Good); }
        if (st.marked > 0) st.marked--;
        if (e->hp <= 0) { e->hp = 0; e->alive = false; skip(e->name + " succumbs."); return; }
        if (st.stunned > 0) { st.stunned--; skip(e->name + " is stunned."); return; }
    }
}

// ---------------------------------------------------------------- rooms
static void EnterNextRoom(Game& g) {
    auto& d = g.dungeon;
    d.roomIndex++;
    d.enemies.clear();
    d.log.clear();
    d.floats.clear();
    RoomType rt = d.rooms[d.roomIndex];
    if (rt == RoomType::Treasure) {
        d.roomGold = (int)(Roll(35, 70) * LootMult(g));
        d.roomRelic = Chance(30) ? Roll(0, (int)Relics().size() - 1) : -1;
        d.lootGold += d.roomGold;
        if (d.roomRelic >= 0) d.lootRelics.push_back(d.roomRelic);
        d.phase = DPhase::Treasure;
        return;
    }
    if (rt == RoomType::Boss) {
        d.enemies.push_back(MakeEnemy(EnemyType::SeaLouse, d.nextUid++));
        d.enemies.push_back(MakeEnemy(EnemyType::Lobster, d.nextUid++));
        d.enemies.push_back(MakeEnemy(EnemyType::BrineWorm, d.nextUid++));
        Log(g, "Something huge clacks in the dark...");
    } else {
        int count = Roll(3, 4);
        for (int i = 0; i < count; i++) d.enemies.push_back(MakeEnemy((EnemyType)Roll(0, 2), d.nextUid++));
        Log(g, "Something stirs in the dark...");
    }
    d.round = 0;
    BeginRound(g);
    d.phase = DPhase::Combat;
}

void StartDungeon(Game& g) {
    CompactParty(g);
    g.dungeon = DungeonState{};
    auto& d = g.dungeon;
    bool anyFight = false;
    for (int i = 0; i < 3; i++) {
        RoomType t = Chance(70) ? RoomType::Fight : RoomType::Treasure;
        anyFight |= t == RoomType::Fight;
        d.rooms.push_back(t);
    }
    if (!anyFight) d.rooms[Roll(0, 2)] = RoomType::Fight;
    d.rooms.push_back(RoomType::Boss);
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) { h->st = Status{}; h->deathsDoor = false; h->hp = std::max(1, h->hp); }
    g.scene = Scene::Dungeon;
}

void DebugEnterCombat(Game& g) {
    StartDungeon(g);
    for (auto& r : g.dungeon.rooms) if (r != RoomType::Boss) r = RoomType::Fight;
    g.dungeon.light = 60;
    EnterNextRoom(g);
}

static void ApplyResults(Game& g) {
    auto& d = g.dungeon;
    if (d.resultsApplied) return;
    d.resultsApplied = true;
    bool win = d.phase == DPhase::Victory;
    if (d.phase != DPhase::Defeat) {
        g.gold += d.lootGold;
        for (int r : d.lootRelics) g.relicStorage.push_back(r);
        if (win) { d.rewardRelic = Roll(0, (int)Relics().size() - 1); g.relicStorage.push_back(d.rewardRelic); }
        for (int id : g.party) {
            Hero* h = FindHero(g, id);
            if (!h) continue;
            GiveXP(g, *h, win ? 5 : 2);
            if (!win) {
                h->stress = std::min(100, h->stress + 10);
                if (h->stress >= 100) h->rattled = true;
            }
        }
    }
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) { h->st = Status{}; if (h->deathsDoor) { h->deathsDoor = false; h->hp = std::max(1, h->hp); } }
    for (auto& h : g.roster)
        if (!InParty(g, h.id) && h.onLeave > 0) h.onLeave--;
    RefreshRadar(g);
    if (g.roster.empty()) {
        Hero h = MakeRandomHero(g);
        g.roster.push_back(h);
        g.party = {{h.id, -1, -1, -1}};
    }
}

// ---------------------------------------------------------------- auto-play (balance testing)
// Plays expeditions with a simple auto-player that never swaps batteries and never retreats.
// Run with:  depth.exe --sim 400 [level] [random]
// The default player heals anyone below 40% HP and otherwise uses its hardest-hitting attack on the
// weakest enemy it can reach; "random" picks any usable ability and target instead.
void SimulateExpeditions(int runs, int level, bool randomPlayer) {
    int wins = 0, losses = 0, deaths = 0, anyDeath = 0, rattled = 0;
    for (int r = 0; r < runs; r++) {
        Game g;
        InitGame(g);
        for (auto& h : g.roster) {
            h.level = level;
            h.hp = GetStats(h).maxHp;
            // random loadout among the unlocked abilities
            std::vector<int> pool;
            const auto& abs = ClassAbilities(h.cls);
            for (int i = 0; i < (int)abs.size(); i++) if (abs[i].unlockLevel <= level) pool.push_back(i);
            for (int i = (int)pool.size() - 1; i > 0; i--) std::swap(pool[i], pool[Roll(0, i)]);
            for (int k = 0; k < LOADOUT_SIZE; k++) h.loadout[k] = k < (int)pool.size() ? pool[k] : -1;
        }
        StartDungeon(g);
        auto& d = g.dungeon;
        int steps = 0;
        while (steps++ < 20000) {
            if (d.phase == DPhase::Corridor) { d.light = std::max(0.0f, d.light - LightDrainPerRoom(g)); EnterNextRoom(g); continue; }
            if (d.phase == DPhase::Treasure || d.phase == DPhase::RoomClear) { d.phase = DPhase::Corridor; continue; }
            if (d.phase != DPhase::Combat) break;
            if (d.turnIdx >= (int)d.order.size()) BeginRound(g);
            TurnEntry te = d.order[d.turnIdx];
            bool valid = te.hero ? (FindHero(g, te.id) && PartyPos(g, te.id) >= 0) : FindEnemy(g, te.id) != nullptr;
            if (!valid) { d.turnIdx++; d.turnStarted = false; continue; }
            if (!d.turnStarted) StartTurn(g);
            if (d.pendingSkip) { EndTurn(g); continue; }
            if (!te.hero) { EnemyAct(g, te.id); EndTurn(g); continue; }
            Hero* h = FindHero(g, te.id);
            int pos = PartyPos(g, te.id);
            const auto& abs = ClassAbilities(h->cls);
            std::vector<int> usable;
            for (int ab : h->loadout)
                if (ab >= 0 && HeroCanUse(g, pos, abs[ab])) usable.push_back(ab);
            if (usable.empty()) { EndTurn(g); continue; }
            int ab = usable[Roll(0, (int)usable.size() - 1)], target = -1;
            if (!randomPlayer) {
                // Simple but sensible: heal whoever is badly hurt, else hit the weakest enemy as hard as possible.
                int hurt = -1;
                float worst = 0.4f;
                for (int p = 0; p < PartySize(g); p++) {
                    Hero* o = PartyAt(g, p);
                    float f = (float)o->hp / GetStats(*o).maxHp;
                    if (f < worst) { worst = f; hurt = p; }
                }
                int healAb = -1, bestAb = -1;
                float best = 0;
                for (int a : usable) {
                    if (abs[a].heal > 0 && abs[a].target == Target::Ally) healAb = a;
                    float v = abs[a].dmgMult * abs[a].hitsCount * (abs[a].aoe ? 2.0f : 1.0f) + (abs[a].bleed + abs[a].poison) * 0.15f;
                    if (abs[a].target == Target::Enemy && v > best) { best = v; bestAb = a; }
                }
                if (hurt >= 0 && healAb >= 0) { ab = healAb; target = hurt; }
                else if (bestAb >= 0) {
                    ab = bestAb;
                    int lowHp = 1 << 30;
                    for (int tp : ValidTargets(g, pos, abs[ab]))
                        if (d.enemies[tp].hp < lowHp) { lowHp = d.enemies[tp].hp; target = tp; }
                }
            }
            auto targets = ValidTargets(g, pos, abs[ab]);
            if (target < 0) target = targets[Roll(0, (int)targets.size() - 1)];
            HeroAct(g, h->id, ab, target);
            EndTurn(g);
        }
        int lost = 4 - (int)g.roster.size();
        deaths += lost;
        anyDeath += lost > 0;
        for (auto& h : g.roster) if (h.rattled) { rattled++; break; }
        if (d.phase == DPhase::Victory) wins++; else losses++;
    }
    printf("Simulated %d expeditions at level %d (%s player):\n", runs, level, randomPlayer ? "random" : "sensible");
    printf("  wins %.1f%%   wipes %.1f%%\n", 100.0 * wins / runs, 100.0 * losses / runs);
    printf("  runs with a death %.1f%%   avg deaths %.2f   runs with someone rattled %.1f%%\n",
           100.0 * anyDeath / runs, (double)deaths / runs, 100.0 * rattled / runs);
}

// ---------------------------------------------------------------- drawing
static const float ANEMONE_X[] = {40, 640, 1240};
static const int ANEMONES = 3;
// A rocky silhouette whose edge follows layered waves, with occasional spikes (stalactites/stalagmites).
static float RidgeY(float x, float base, float amp, int seed, bool fromTop, float spiky) {
    float h = sinf(x * 0.006f + seed) * 0.5f + sinf(x * 0.017f + seed * 2.3f) * 0.3f + sinf(x * 0.041f + seed * 0.7f) * 0.2f;
    float spike = powf(fabsf(sinf(x * 0.013f + seed * 1.7f)), 12.0f) * spiky;
    return base + h * amp + (fromTop ? spike : -spike);
}

static void DrawRidge(float base, float amp, int seed, bool fromTop, Color col, float spiky) {
    const float STEP = 3;
    float edge = fromTop ? 0.0f : (float)SCREEN_H;
    for (float x = 0; x < SCREEN_W; x += STEP) {
        float y0 = RidgeY(x, base, amp, seed, fromTop, spiky), y1 = RidgeY(x + STEP, base, amp, seed, fromTop, spiky);
        DrawTri({x, y0}, {x + STEP, y1}, {x, edge}, col);
        DrawTri({x + STEP, y1}, {x + STEP, edge}, {x, edge}, col);
    }
}

static void DrawCave(Game& g) {
    float t = g.time;
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{28, 74, 90, 255}, Color{6, 20, 30, 255});
    // shafts of daylight through cracks far above
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 4; k++) {
        float x = 200 + k * 290 + sinf(t * 0.2f + k) * 20;
        DrawTri({x, 0}, {x - 60, 0}, {x - 170, 520}, Color{60, 110, 120, 30});
        DrawTri({x - 60, 0}, {x - 240, 520}, {x - 170, 520}, Color{60, 110, 120, 30});
    }
    EndBlendMode();
    DrawRidge(330, 60, 3, false, Color{18, 46, 58, 255}, 60);   // far rock
    DrawRidge(390, 40, 9, false, Color{22, 40, 48, 255}, 30);   // nearer rock
    DrawRidge(70, 40, 5, true, Color{14, 30, 38, 255}, 130);    // ceiling and stalactites
    // kelp swaying against the back wall
    for (int k = 0; k < 9; k++) {
        float bx = 40 + k * 150 + (k % 2) * 40, by = 460;
        Vector2 prev{bx, by};
        for (int s = 1; s <= 10; s++) {
            Vector2 p{bx + sinf(t * 0.9f + k + s * 0.45f) * s * 2.2f, by - s * (16 + k % 3 * 3)};
            DrawLineEx(prev, p, 6 - s * 0.4f, Color{34, 90, 60, 255});
            prev = p;
        }
    }
    // the cave floor, with a wet lip where it meets the back wall
    DrawTiled(Tex::Rock, {0, 450, (float)SCREEN_W, 270}, 1.4f, Color{96, 110, 112, 255});
    DrawVGradient({0, 450, (float)SCREEN_W, 40}, Fade(BLACK, 0.55f), Fade(BLACK, 0));
    DrawRectangle(0, 450, SCREEN_W, 3, Color{120, 150, 150, 160});
    DrawVGradient({0, 600, (float)SCREEN_W, 120}, Fade(BLACK, 0), Fade(BLACK, 0.5f));
    for (int k = 0; k < 5; k++) { // puddles catch the light
        float px = 90 + k * 270.0f + (k % 2) * 60, py = 505 + (k % 3) * 40.0f;
        DrawEllipse((int)px, (int)py, 70 - k * 4, 10, Color{40, 80, 90, 200});
        DrawEllipse((int)px - 10, (int)py - 2, 40 - k * 3, 4, Color{110, 170, 180, 90});
    }
    // glowing anemones, in the gaps between where the units stand
    for (int k = 0; k < ANEMONES; k++) {
        float ax = ANEMONE_X[k], ay = 452;
        for (int f = -3; f <= 3; f++)
            DrawLineEx({ax, ay}, {ax + f * 5 + sinf(t * 2 + k + f) * 3, ay - 16 - (3 - abs(f)) * 3}, 2.5f, Color{120, 230, 220, 255});
        DrawCircle((int)ax, (int)ay, 6, Color{60, 150, 150, 255});
    }
}

static void DrawCaveForeground(Game& g) {
    float t = g.time;
    Color fg{6, 12, 16, 255};
    // rocks framing the bottom corners and kelp hanging in front of the view
    DrawCircle(-40, 760, 230, fg);
    DrawCircle(160, 790, 150, fg);
    DrawCircle(1320, 770, 240, fg);
    DrawCircle(1110, 800, 140, fg);
    for (int k = 0; k < 3; k++) {
        float bx = k == 0 ? 30 : k == 1 ? 1230 : 1180;
        Vector2 prev{bx, 0};
        for (int s = 1; s <= 9; s++) {
            Vector2 p{bx + sinf(t * 0.7f + k * 2 + s * 0.5f) * s * 3, s * 22.0f};
            DrawLineEx(prev, p, 16 - s, fg);
            prev = p;
        }
    }
}

static void DrawEnemyFigure(const Enemy& e, Rectangle r, float t) {
    float cx = r.x + r.width / 2, by = r.y + r.height, bob = sinf(t * 2.5f + e.uid) * 3;
    switch (e.type) {
        case EnemyType::SeaLouse: {
            Color c{176, 156, 200, 255};
            float cy = by - 32 + bob;
            for (int k = 0; k < 3; k++) {
                float lx = cx - 20 + k * 20.0f;
                DrawLineEx({lx, cy}, {lx - 10, by}, 3, Color{110, 95, 130, 255});
            }
            DrawEllipse((int)cx, (int)cy, 40, 22, c);
            DrawEllipse((int)cx - 6, (int)cy - 8, 26, 9, Fade(WHITE, 0.2f));
            for (int k = 1; k < 4; k++) DrawLineEx({cx - 40 + k * 20.0f, cy - 19}, {cx - 40 + k * 20.0f, cy + 19}, 2, Color{140, 120, 160, 255});
            DrawLineEx({cx - 34, cy - 12}, {cx - 58, cy - 38}, 2, c);
            DrawCircle((int)(cx - 30), (int)(cy - 5), 4, Pal::Ink);
        } break;
        case EnemyType::CaveShrimp: {
            Color c{248, 146, 132, 255};
            float cy = by - 52 + bob;
            const float seg[5][3] = {{-24, -12, 17}, {-8, -16, 16}, {8, -12, 14}, {20, -2, 12}, {28, 10, 10}};
            DrawTri({cx + 30, cy + 16}, {cx + 44, cy + 34}, {cx + 20, cy + 34}, Color{230, 120, 110, 255});
            for (auto& s : seg) DrawCircle((int)(cx + s[0]), (int)(cy + s[1]), s[2], c);
            for (auto& s : seg) DrawCircle((int)(cx + s[0] - 3), (int)(cy + s[1] - 5), s[2] * 0.4f, Fade(WHITE, 0.18f));
            for (int k = 0; k < 4; k++) DrawLineEx({cx - 16 + k * 10.0f, cy}, {cx - 20 + k * 10.0f, by}, 2, Color{210, 110, 100, 255});
            DrawCircle((int)(cx - 44), (int)(cy + 6), 14, Color{220, 100, 90, 255});
            DrawCircle((int)(cx - 54), (int)(cy + 2), 7, Color{240, 170, 150, 255});
            DrawCircle((int)(cx - 30), (int)(cy - 22), 4, Pal::Ink);
            DrawLineEx({cx - 32, cy - 24}, {cx - 64, cy - 56}, 2, c);
        } break;
        case EnemyType::BrineWorm: {
            for (int k = 5; k >= 0; k--) {
                float x = cx + sinf(t * 2 + k * 0.8f) * 10 - k * 2;
                float y = by - 14 - k * 18.0f + bob * 0.5f;
                DrawCircle((int)x, (int)y, 18 - k * 1.5f, k % 2 ? Color{110, 180, 90, 255} : Color{90, 160, 76, 255});
                DrawCircle((int)x - 4, (int)y - 5, (18 - k * 1.5f) * 0.4f, Fade(WHITE, 0.15f));
                if (k == 5) {
                    DrawCircle((int)x - 8, (int)y + 2, 6, Color{40, 60, 30, 255});
                    DrawCircle((int)x - 2, (int)y - 6, 3, Pal::Ink);
                }
            }
        } break;
        case EnemyType::Lobster: {
            Color c{210, 60, 50, 255}, dk{160, 40, 36, 255};
            float cy = by - 62 + bob;
            for (int k = 0; k < 4; k++) DrawLineEx({cx - 10 + k * 14.0f, cy + 20}, {cx - 20 + k * 14.0f, by}, 4, dk);
            DrawCircle((int)(cx + 48), (int)(cy + 8), 20, c);
            DrawCircle((int)(cx + 70), (int)(cy + 16), 15, c);
            DrawTri({cx + 78, cy + 20}, {cx + 100, cy + 44}, {cx + 70, cy + 44}, dk);
            DrawEllipse((int)(cx + 8), (int)cy, 48, 32, c);
            DrawEllipse((int)(cx - 2), (int)cy - 12, 30, 12, Fade(WHITE, 0.18f));
            DrawCircle((int)(cx - 34), (int)(cy - 10), 24, c);
            DrawLineEx({cx - 40, cy - 30}, {cx - 90, cy - 180}, 2, dk);
            DrawLineEx({cx - 30, cy - 32}, {cx - 60, cy - 190}, 2, dk);
            DrawCircle((int)(cx - 44), (int)(cy - 28), 5, Pal::Ink);
            DrawCircle((int)(cx - 30), (int)(cy - 30), 5, Pal::Ink);
            DrawLineEx({cx - 40, cy - 4}, {cx - 64, cy - 70}, 8, c);
            DrawCircle((int)(cx - 66), (int)(cy - 84), 22, c);
            DrawTri({cx - 90, cy - 104}, {cx - 64, cy - 88}, {cx - 76, cy - 112}, Color{30, 60, 70, 255});
            DrawLineEx({cx - 30, cy + 10}, {cx - 56, cy + 20}, 8, c);
            DrawCircle((int)(cx - 62), (int)(cy + 20), 16, c);
        } break;
    }
}

static std::string StatusTags(const Status& st) {
    std::string s;
    if (st.bleedTurns > 0) s += "BLEED ";
    if (st.poisonTurns > 0) s += TextFormat("POISON %d ", st.poisonDmg);
    if (st.stunned > 0) s += "STUN ";
    if (st.marked > 0) s += "MARKED ";
    if (st.buffTurns > 0) s += "RALLY ";
    if (st.dodgeTurns > 0) s += "DODGE+ ";
    if (st.protTurns > 0) s += "ARMOR+ ";
    if (st.guardTurns > 0) s += "GUARD ";
    return s;
}

static void DrawUnitFigures(Game& g) {
    auto& d = g.dungeon;
    float t = g.time;
    for (int p = 0; p < PARTY_SIZE; p++) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        Vector2 feet{r.x + r.width / 2, r.y + r.height};
        DrawShadowBlob(feet, 38);
        DrawCrewFigure(*h, feet, 1.05f, true, 0, t);
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        Rectangle r = EnemyRect(g, p);
        DrawShadowBlob({r.x + r.width / 2, r.y + r.height}, e.boss ? 70 : 44);
        DrawEnemyFigure(e, r, t);
    }
}

static void DrawUnitHud(Game& g, int actingHero, int actingEnemy) {
    auto& d = g.dungeon;
    float t = g.time;
    for (int p = 0; p < PARTY_SIZE; p++) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        Stats s = GetStats(*h);
        if (h->rattled) Glow({r.x + r.width / 2, r.y - 4}, 40 + sinf(t * 5) * 6, Fade(Pal::Stress, 0.6f));
        if (h->deathsDoor) Glow({r.x + r.width / 2, r.y + 60}, 80, Fade(Pal::Bad, 0.25f + 0.15f * sinf(t * 6)));
        DrawBar({r.x, r.y + r.height + 8, r.width, 8}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({r.x, r.y + r.height + 19, r.width, 5}, h->stress / 100.0f, Pal::Stress);
        float nw = (float)MeasureTxt(h->name, 16, true);
        TxtShadow(h->name, r.x + r.width / 2 - nw / 2, r.y + r.height + 28, 16, Pal::Paper, true);
        DrawTextCentered(TextFormat("%d/%d HP", h->hp, s.maxHp), r.x + r.width / 2, r.y + r.height + 47, 13, Color{220, 220, 200, 255});
        std::string tags = StatusTags(h->st);
        if (h->deathsDoor) tags += "DEATH'S DOOR ";
        if (h->rattled) tags += "RATTLED";
        float tw = (float)MeasureTxt(tags, 12, true);
        TxtShadow(tags, r.x + r.width / 2 - tw / 2, r.y - 26, 12, Pal::Coral, true);
        if (h->id == actingHero) {
            float y = r.y - 50 + sinf(t * 6) * 4;
            Glow({r.x + r.width / 2, y + 6}, 22, Fade(Pal::Brass, 0.7f));
            DrawTri({r.x + r.width / 2 - 12, y}, {r.x + r.width / 2, y + 14}, {r.x + r.width / 2 + 12, y}, Pal::Brass);
        }
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        Rectangle r = EnemyRect(g, p);
        DrawBar({r.x, r.y + r.height + 8, r.width, 8}, (float)e.hp / e.maxHp, Pal::Bad);
        float nw = (float)MeasureTxt(e.name, 16, true);
        TxtShadow(e.name, r.x + r.width / 2 - nw / 2, r.y + r.height + 22, 16, Pal::Paper, true);
        DrawTextCentered(TextFormat("%d/%d HP", e.hp, e.maxHp), r.x + r.width / 2, r.y + r.height + 41, 13, Color{220, 220, 200, 255});
        std::string tags = StatusTags(e.st);
        float tw = (float)MeasureTxt(tags, 12, true);
        TxtShadow(tags, r.x + r.width / 2 - tw / 2, r.y - 26, 12, Pal::Coral, true);
        if (e.uid == actingEnemy) {
            float y = r.y - 50 + sinf(t * 6) * 4;
            Glow({r.x + r.width / 2, y + 6}, 22, Fade(Pal::Bad, 0.7f));
            DrawTri({r.x + r.width / 2 - 12, y}, {r.x + r.width / 2, y + 14}, {r.x + r.width / 2 + 12, y}, Pal::Bad);
        }
    }
}

static void DrawCaveLighting(Game& g) {
    auto& d = g.dungeon;
    float L = d.light / 100.0f, t = g.time;
    auto lerp = [](float a, float b, float k) { return (unsigned char)(a + (b - a) * k); };
    LightsBegin(Color{lerp(18, 84, L), lerp(22, 96, L), lerp(34, 108, L), 255});
    float flick = 0.95f + 0.05f * sinf(t * 17) * sinf(t * 5.3f);
    Color warm{255, 214, 150, 255};
    AddLight({470, 330}, 300 + 480 * L, warm, (0.45f + 0.5f * L) * flick); // the party's flashlight glow
    AddCone({560, 320}, 0.05f, 0.42f, 420 + 480 * L, Color{255, 226, 170, 255});
    for (int k = 0; k < 4; k++) AddLight({200 + k * 290.0f - 110, 120}, 260, Color{70, 120, 130, 255}, 0.45f); // shafts
    for (int k = 0; k < ANEMONES; k++) AddLight({ANEMONE_X[k], 446}, 110, Color{90, 220, 210, 255}, 0.6f);
    LightsEnd();
    for (int k = 0; k < ANEMONES; k++) Glow({ANEMONE_X[k], 440}, 22, Color{90, 220, 210, 80});
    // marine snow drifting through the beam
    for (int k = 0; k < 40; k++) {
        float px = fmodf(k * 97.0f + t * (6 + k % 5), (float)SCREEN_W);
        float py = fmodf(k * 53.0f + t * (10 + k % 7), 520.0f) + 40;
        DrawCircle((int)px, (int)py, 1.3f + (k % 3) * 0.5f, Color{220, 240, 240, (unsigned char)(50 + 60 * L)});
    }
}

static void DrawTopBar(Game& g) {
    auto& d = g.dungeon;
    DrawVGradient({0, 0, (float)SCREEN_W, 58}, Color{10, 18, 24, 240}, Color{16, 28, 36, 220});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    TxtShadow("THE CAVE  -  Shallows", 20, 15, 24, Pal::Brass, true);
    for (int i = 0; i < (int)d.rooms.size(); i++) {
        float x = 330 + i * 34.0f;
        Color c = i < d.roomIndex ? Pal::Good : i == d.roomIndex ? Pal::Brass : Color{90, 100, 104, 255};
        if (d.rooms[i] == RoomType::Boss) DrawPoly({x + 10, 28}, 4, 13, 45, c);
        else DrawCircle((int)x + 10, 28, 9, c);
    }
    Txt("Light", 500, 6, 16, Pal::Paper);
    DrawBar({500, 28, 200, 14}, d.light / 100.0f, Color{250, 220, 120, 255});
    TxtShadow(LightName(d.light), 712, 22, 20, Color{250, 220, 120, 255});
    Txt(TextFormat("Loot: %d gold, %d relic%s", d.lootGold, (int)d.lootRelics.size(), d.lootRelics.size() == 1 ? "" : "s"), 880, 18, 20, Pal::Paper);
}

// Returns true if the button to leave was pressed.
static bool ResultPanel(const char* title, const std::string& body, const char* button, Color titleColor) {
    Rectangle p{340, 150, 600, 300};
    Panel(p);
    DrawTextCenteredBold(title, p.x + p.width / 2, p.y + 24, 34, titleColor);
    DrawWrapped(body, {p.x + 40, p.y + 84, p.width - 80, 150}, 19, Pal::Ink);
    return Button({p.x + 150, p.y + p.height - 70, 300, 48}, button);
}

void SceneDungeon(Game& g) {
    auto& d = g.dungeon;
    float dt = GetFrameTime();
    SetPost(0.5f, 0.035f, 0.45f);

    // ---------------- combat logic
    int actingHero = -1, actingEnemy = -1;
    if (d.phase == DPhase::Combat) {
        if (d.turnIdx >= (int)d.order.size()) BeginRound(g);
        TurnEntry te = d.order[d.turnIdx];
        bool valid = te.hero ? (FindHero(g, te.id) && PartyPos(g, te.id) >= 0) : FindEnemy(g, te.id) != nullptr;
        if (!valid) {
            d.turnIdx++;
            d.turnStarted = false;
        } else {
            if (!d.turnStarted) StartTurn(g);
            if (te.hero) actingHero = te.id; else actingEnemy = te.id;
            if (d.pendingSkip) {
                d.actTimer -= dt;
                if (d.actTimer <= 0) EndTurn(g);
            } else if (!te.hero) {
                d.actTimer += dt;
                if (d.actTimer > 0.75f) { EnemyAct(g, te.id); EndTurn(g); }
            }
        }
    }

    // ---------------- drawing
    DrawCave(g);
    DrawUnitFigures(g);
    DrawCaveLighting(g);
    DrawCaveForeground(g);
    DrawUnitHud(g, actingHero, actingEnemy);
    for (auto& f : d.floats) {
        f.life -= dt;
        f.pos.y -= 32 * dt;
        unsigned char a = (unsigned char)(255 * std::clamp(f.life / 0.4f, 0.0f, 1.0f));
        float w = (float)MeasureTxt(f.text, 22, true);
        TxtBold(f.text, f.pos.x - w / 2 + 2, f.pos.y + 2, 22, Color{0, 0, 0, a});
        TxtBold(f.text, f.pos.x - w / 2, f.pos.y, 22, Color{f.color.r, f.color.g, f.color.b, a});
    }
    d.floats.erase(std::remove_if(d.floats.begin(), d.floats.end(), [](const FloatText& f) { return f.life <= 0; }), d.floats.end());
    DrawTopBar(g);

    if (!d.log.empty()) {
        DrawRectangleRounded({380, 66, 520, 20.0f * d.log.size() + 14}, 0.1f, 6, Color{8, 16, 22, 180});
        for (size_t i = 0; i < d.log.size(); i++)
            DrawTextCentered(d.log[i], 640, 73 + i * 20.0f, 16, i + 1 == d.log.size() ? Pal::Paper : Color{176, 190, 190, 255});
    }

    // ---------------- phase-specific UI
    switch (d.phase) {
        case DPhase::Corridor: {
            Rectangle p{400, 150, 480, 270};
            Panel(p);
            const char* head = d.roomIndex < 0 ? "At the cave mouth" : TextFormat("Room %d of %d cleared", d.roomIndex + 1, (int)d.rooms.size());
            DrawTextCenteredBold(head, p.x + p.width / 2, p.y + 20, 28, Pal::Ink);
            bool nextIsBoss = d.rooms[d.roomIndex + 1] == RoomType::Boss;
            DrawTextCentered(nextIsBoss ? "Heavy clacking echoes from the next chamber..." : "The passage winds deeper.",
                             p.x + p.width / 2, p.y + 60, 18, nextIsBoss ? Pal::Bad : Pal::BrassDk);
            int drain = LightDrainPerRoom(g);
            if (Button({p.x + 40, p.y + 96, 400, 46}, TextFormat(nextIsBoss ? "Face the Lobster  (-%d light)" : "Advance  (-%d light)", drain))) {
                d.light = std::max(0.0f, d.light - drain);
                EnterNextRoom(g);
            }
            if (Button({p.x + 40, p.y + 150, 400, 42}, TextFormat("Swap in a battery  (+40 light)   [%d left]", g.batteries),
                       g.batteries > 0 && d.light < 100)) {
                g.batteries--;
                d.light = std::min(100.0f, d.light + 40);
            }
            if (Button({p.x + 40, p.y + 200, 400, 42}, d.roomIndex < 0 ? "Turn back" : "Retreat with the loot (+10 stress)")) {
                if (d.roomIndex < 0) { g.scene = Scene::Hub; return; }
                d.phase = DPhase::Retreat;
            }
        } break;

        case DPhase::Treasure: {
            std::string body = TextFormat("A barnacled chest! +%d gold.", d.roomGold);
            if (d.roomRelic >= 0) body += " Inside, wrapped in oilcloth: a " + Relics()[d.roomRelic].name + "!";
            if (d.light < 50) body += "\n\nThe darkness made the find richer.";
            if (ResultPanel("Treasure", body, "Continue", Pal::Brass)) d.phase = DPhase::Corridor;
        } break;

        case DPhase::RoomClear: {
            std::string body = TextFormat("The room is quiet again. You gather %d gold from the debris.", d.roomGold);
            if (ResultPanel("Room cleared", body, "Continue", Pal::Good)) d.phase = DPhase::Corridor;
        } break;

        case DPhase::Victory:
        case DPhase::Retreat:
        case DPhase::Defeat: {
            ApplyResults(g);
            std::string body;
            const char* title;
            Color tc;
            if (d.phase == DPhase::Victory) {
                title = "Expedition complete!";
                tc = Pal::Good;
                body = TextFormat("The Lobster is beaten. You bring home %d gold", d.lootGold);
                for (int r : d.lootRelics) body += ", a " + Relics()[r].name;
                body += ", and as a reward for finishing: a " + Relics()[d.rewardRelic].name + ".\n\nSurvivors earn 5 XP.";
            } else if (d.phase == DPhase::Retreat) {
                title = "Retreat";
                tc = Pal::Brass;
                body = TextFormat("The crew scrambles back to the Nautilus with %d gold", d.lootGold);
                body += d.lootRelics.empty() ? "." : " and the relics they found.";
                body += "\n\nNo completion reward. Survivors earn 2 XP and +10 stress.";
            } else {
                title = "Lost to the depths";
                tc = Pal::Bad;
                body = "The whole party has fallen, along with their relics and everything they carried.";
                if (g.roster.size() == 1 && PartySize(g) == 1) body += "\n\nA stowaway creeps out of the cargo hold and volunteers.";
            }
            if (ResultPanel(title, body, "Return to the Nautilus", tc)) g.scene = Scene::Hub;
        } break;

        case DPhase::Combat: {
            Rectangle bar{20, 560, SCREEN_W - 40.0f, 148};
            DrawRectangleRounded({bar.x + 3, bar.y + 5, bar.width, bar.height}, 0.08f, 6, Fade(BLACK, 0.4f));
            DrawRectangleRounded(bar, 0.08f, 6, Color{14, 24, 32, 235});
            DrawRectangleRoundedLinesEx(bar, 0.08f, 6, 3, Pal::BrassDk);
            Hero* h = actingHero >= 0 ? FindHero(g, actingHero) : nullptr;
            if (!h || d.pendingSkip || !d.turnStarted) {
                const char* who = actingEnemy >= 0 && FindEnemy(g, actingEnemy) ? FindEnemy(g, actingEnemy)->name.c_str() : "...";
                DrawTextCentered(TextFormat("%s is acting", who), SCREEN_W / 2.0f, 620, 24, Pal::Paper);
                break;
            }
            int pos = PartyPos(g, h->id);
            int heroId = h->id;
            TxtBold(TextFormat("%s's turn  (%s, rank %d)", h->name.c_str(), ClassName(h->cls), pos + 1), 40, 570, 20, Pal::Brass);
            Txt("Choose an ability, then click a highlighted target. Right-click to cancel.", 560, 573, 15, Color{176, 190, 190, 255});
            const auto& abs = ClassAbilities(h->cls);
            int hoverAb = -1;
            for (int slot = 0; slot < LOADOUT_SIZE; slot++) {
                int i = h->loadout[slot];
                Rectangle b{40 + slot * 240.0f, 600, 228, 50};
                if (i < 0) {
                    DrawRectangleRoundedLinesEx(b, 0.25f, 6, 1, Color{90, 100, 100, 255});
                    DrawTextCentered("(empty slot)", b.x + b.width / 2, b.y + 16, 16, Color{90, 100, 100, 255});
                    continue;
                }
                bool usable = HeroCanUse(g, pos, abs[i]);
                if (i == d.selectedAbility) DrawRectangleRounded({b.x - 4, b.y - 4, b.width + 8, b.height + 8}, 0.3f, 6, Pal::Teal);
                if (CheckCollisionPointRec(GetMousePosition(), b)) hoverAb = i;
                if (Button(b, abs[i].name.c_str(), usable)) {
                    if (abs[i].target == Target::Self || abs[i].target == Target::AllAllies) {
                        HeroAct(g, heroId, i, pos);
                        EndTurn(g);
                        return;
                    }
                    d.selectedAbility = i;
                }
            }
            if (Button({1010, 600, 120, 50}, "Pass")) { Log(g, h->name + " holds position."); EndTurn(g); return; }
            int showAb = hoverAb >= 0 ? hoverAb : d.selectedAbility;
            if (showAb >= 0) {
                const Ability& a = abs[showAb];
                std::string info = a.desc + "   [from ranks " + RankString(a.usableFrom);
                if (a.target == Target::Enemy) info += ", hits enemy ranks " + RankString(a.hits);
                info += "]";
                if (!(a.usableFrom & (1 << pos))) info += "  - can't be used from this rank";
                Txt(info, 40, 664, 16, Pal::Paper);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) d.selectedAbility = -1;
            if (d.selectedAbility >= 0) {
                const Ability& a = abs[d.selectedAbility];
                for (int tp : ValidTargets(g, pos, a)) {
                    Rectangle tr = a.target == Target::Enemy ? EnemyRect(g, tp) : HeroRect(tp);
                    bool hov = CheckCollisionPointRec(GetMousePosition(), tr);
                    DrawRectangleRoundedLinesEx({tr.x - 6, tr.y - 6, tr.width + 12, tr.height + 12}, 0.1f, 6, hov ? 4.0f : 2.0f,
                                                a.target == Target::Enemy ? Pal::Coral : Pal::Good);
                    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        HeroAct(g, heroId, d.selectedAbility, tp);
                        EndTurn(g);
                        return;
                    }
                }
            }
        } break;
    }
}
