// ============================================================================
//  DEPTH - the roguelike expedition: rooms, the flashlight, and combat.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

static int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
static bool Chance(int pct) { return GetRandomValue(1, 100) <= pct; }

static void Log(Game& g, const std::string& s) {
    auto& L = g.dungeon.log;
    L.push_back(s);
    while (L.size() > 5) L.erase(L.begin());
}

// ---------------------------------------------------------------- light
static float StressMult(const Game& g) { float l = g.dungeon.light; return l >= 50 ? 1.0f : l > 0 ? 1.4f : 1.8f; }
static float LootMult(const Game& g) {
    float l = g.dungeon.light;
    return (l >= 50 ? 1.0f : l > 0 ? 1.4f : 1.8f) * (1.0f + 0.35f * CAVE_TIER_LEVEL[g.dungeon.tier]);
}
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

// ---------------------------------------------------------------- animation bookkeeping
static void StartAnim(Game& g, bool hero, int id, Anim kind, float dur) {
    auto& v = g.dungeon.anims;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const UnitAnim& a) { return a.hero == hero && a.id == id; }), v.end());
    v.push_back({hero, id, kind, 0, dur});
}

static const UnitAnim* FindAnim(const Game& g, bool hero, int id) {
    for (auto& a : g.dungeon.anims) if (a.hero == hero && a.id == id) return &a;
    return nullptr;
}

static void Sparkle(Game& g, Rectangle r, int n, Color c, float speed, float rise) {
    for (int i = 0; i < n; i++) {
        float a = GetRandomValue(0, 628) / 100.0f, v = speed * GetRandomValue(30, 100) / 100.0f;
        Vector2 p{r.x + r.width * GetRandomValue(15, 85) / 100.0f, r.y + r.height * GetRandomValue(20, 80) / 100.0f};
        float life = GetRandomValue(40, 90) / 100.0f;
        g.dungeon.sparks.push_back({p, {cosf(a) * v, sinf(a) * v - rise}, life, life, GetRandomValue(15, 35) / 10.0f, c});
    }
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
                if (!Chance(hit)) { Float(g, er, "Miss", Pal::Paper); StartAnim(g, false, uid, Anim::Dodge, 0.45f); continue; }
                bool crit = Chance(5);
                StartAnim(g, false, uid, Anim::Hurt, 0.5f);
                Sparkle(g, er, crit ? 16 : 8, crit ? Pal::Brass : Color{255, 210, 160, 255}, 220, 0);
                if (a.dmgMult > 0) {
                    float raw = Roll(s.dmgMin, s.dmgMax) * a.dmgMult * (1.0f + h->st.buffDmg / 100.0f);
                    if (crit) raw *= 1.5f;
                    if (e->st.marked > 0) raw *= 1.25f;
                    int dmg = std::max(1, (int)std::round(raw * (100 - e->prot) / 100.0f));
                    e->hp -= dmg;
                    Float(g, er, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Coral);
                    if (crit) {
                        d.shake = 0.35f;
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
        if (a.heal || a.stressHeal || a.cure) Sparkle(g, tr, 14, a.heal ? Color{130, 240, 150, 255} : Color{200, 170, 255, 255}, 50, 60);
        else Sparkle(g, tr, 14, Color{255, 214, 120, 255}, 60, 50);
        if (a.heal) { int amt = HealHero(*t, a.heal + Roll(0, 2)); Float(g, tr, "+" + std::to_string(amt), Pal::Good); }
        if (a.cure) { t->st.bleedTurns = 0; t->st.poisonTurns = 0; }
        if (a.stressHeal) AddStress(g, *t, -a.stressHeal);
        if (a.buffDmg) { t->st.buffDmg = a.buffDmg; t->st.buffTurns = 3; Float(g, tr, "Rallied", Pal::Brass); }
        if (a.buffDodge) { t->st.dodgeBuff = a.buffDodge; t->st.dodgeTurns = 3; Float(g, tr, "Dodge up", Pal::Teal); }
        if (a.buffProt) { t->st.protBuff = a.buffProt; t->st.protTurns = 3; Float(g, tr, "Armor up", Pal::Brass); }
        if (a.guardTurns) { t->st.guardTurns = a.guardTurns; Float(g, tr, "Guarding", Pal::Teal); }
    }
}

// Which ability an enemy will use this turn (-1 if it can't reach anyone).
static int EnemyPick(Game& g, int uid) {
    Enemy* e = FindEnemy(g, uid);
    int n = PartySize(g);
    if (!e || n == 0) return -1;
    std::vector<int> usable;
    for (int i = 0; i < (int)e->abilities.size(); i++)
        for (int p = 0; p < n; p++)
            if (e->abilities[i].hits & (1 << p)) { usable.push_back(i); break; }
    return usable.empty() ? -1 : usable[Roll(0, (int)usable.size() - 1)];
}

static void EnemyAct(Game& g, int uid, int ability) {
    Enemy* e = FindEnemy(g, uid);
    int n = PartySize(g);
    if (!e || n == 0) return;
    if (ability < 0) ability = EnemyPick(g, uid);
    if (ability < 0) { Log(g, e->name + " can't reach anyone and skitters about."); return; }
    const EnemyAbility& a = e->abilities[ability];

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
        if (!Chance(hit)) { Float(g, hr, "Dodge", Pal::Paper); StartAnim(g, true, h->id, Anim::Dodge, 0.45f); continue; }
        bool crit = Chance(6);
        if (crit) g.dungeon.shake = 0.35f;
        if (a.dmgMult > 0) {
            StartAnim(g, true, h->id, Anim::Hurt, 0.5f);
            Sparkle(g, hr, 8, Color{220, 60, 50, 255}, 180, 0);
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
        d.enemies.push_back(MakeEnemy(CAVE_TIER_LEVEL[d.tier] >= 3 ? EnemyType::CaveShrimp : EnemyType::SeaLouse, d.nextUid++));
        d.enemies.push_back(MakeEnemy(EnemyType::Lobster, d.nextUid++));
        d.enemies.push_back(MakeEnemy(EnemyType::BrineWorm, d.nextUid++));
        if (CAVE_TIER_LEVEL[d.tier] >= 6) d.enemies.push_back(MakeEnemy(EnemyType::BrineWorm, d.nextUid++));
        Log(g, "Something huge clacks in the dark...");
    } else {
        int count = Roll(3, 4);
        for (int i = 0; i < count; i++) d.enemies.push_back(MakeEnemy((EnemyType)Roll(0, 2), d.nextUid++));
        Log(g, "Something stirs in the dark...");
    }
    for (auto& e : d.enemies) ScaleEnemyForTier(e, d.tier);
    d.anims.clear();
    d.shots.clear();
    d.pending = PendingAction{};
    d.round = 0;
    BeginRound(g);
    d.phase = DPhase::Combat;
}

void StartDungeon(Game& g) {
    CompactParty(g);
    g.dungeon = DungeonState{};
    auto& d = g.dungeon;
    d.tier = std::clamp(g.caveTier, 0, std::min(CAVE_TIERS - 1, g.caveTierCleared + 1));
    int lvl = CAVE_TIER_LEVEL[d.tier], rooms = lvl >= 3 ? 4 : 3;
    bool anyFight = false;
    for (int i = 0; i < rooms; i++) {
        RoomType t = Chance(70) ? RoomType::Fight : RoomType::Treasure;
        anyFight |= t == RoomType::Fight;
        d.rooms.push_back(t);
    }
    if (!anyFight) d.rooms[Roll(0, rooms - 1)] = RoomType::Fight;
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
        if (win && d.tier > g.caveTierCleared) {
            g.caveTierCleared = d.tier;
            if (d.tier + 1 < CAVE_TIERS) g.caveTier = d.tier + 1;
        }
        int lvl = CAVE_TIER_LEVEL[d.tier];
        for (int id : g.party) {
            Hero* h = FindHero(g, id);
            if (!h) continue;
            int before = h->level;
            GiveXP(g, *h, win ? 5 + lvl * 2 : 2 + lvl);
            if (h->level > before) d.levelUps += (d.levelUps.empty() ? "" : ", ") + h->name + TextFormat(" reached level %d", h->level);
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
void SimulateExpeditions(int runs, int level, bool randomPlayer, int tier) {
    int wins = 0, losses = 0, deaths = 0, anyDeath = 0, rattled = 0;
    for (int r = 0; r < runs; r++) {
        Game g;
        InitGame(g);
        g.caveTier = tier;
        g.caveTierCleared = CAVE_TIERS;
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
            if (!te.hero) { EnemyAct(g, te.id, -1); EndTurn(g); continue; }
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
    printf("Simulated %d expeditions, crew level %d, cave level %d (%s player):\n", runs, level, CAVE_TIER_LEVEL[tier],
           randomPlayer ? "random" : "sensible");
    printf("  wins %.1f%%   wipes %.1f%%\n", 100.0 * wins / runs, 100.0 * losses / runs);
    printf("  runs with a death %.1f%%   avg deaths %.2f   runs with someone rattled %.1f%%\n",
           100.0 * anyDeath / runs, (double)deaths / runs, 100.0 * rattled / runs);
}

// ---------------------------------------------------------------- drawing: the cave, in layers
// The cave is painted in seven layers, from the far water to rocks right in front of the view. Each
// layer slides by a different amount as the party walks between rooms (and sways a little with the
// mouse), so the scenery has real depth. Deeper cave levels start further along the same cave.
static Vector2 gShake{0, 0};

static float LayerOffset(const Game& g, float depth) {
    float sway = sinf(g.time * 0.23f) * 14 + (GetMousePosition().x - SCREEN_W / 2.0f) * 0.025f;
    return -(g.dungeon.scroll + g.dungeon.tier * 1900.0f + sway) * depth + gShake.x * depth;
}

static float Hash1(float x) { float s = sinf(x * 12.9898f + 1.7f) * 43758.5453f; return s - floorf(s); }

// A rocky silhouette whose edge follows layered waves, with occasional spikes (stalactites/stalagmites).
static float RidgeY(float x, float base, float amp, int seed, bool fromTop, float spiky) {
    float h = sinf(x * 0.006f + seed) * 0.5f + sinf(x * 0.017f + seed * 2.3f) * 0.3f + sinf(x * 0.041f + seed * 0.7f) * 0.2f;
    float spike = powf(fabsf(sinf(x * 0.013f + seed * 1.7f)), 12.0f) * spiky;
    return base + h * amp + (fromTop ? spike : -spike);
}

static void DrawRidge(float off, float base, float amp, int seed, bool fromTop, Color col, float spiky) {
    const float STEP = 3;
    float edge = fromTop ? 0.0f : (float)SCREEN_H;
    for (float x = 0; x < SCREEN_W; x += STEP) {
        float y0 = RidgeY(x - off, base, amp, seed, fromTop, spiky), y1 = RidgeY(x + STEP - off, base, amp, seed, fromTop, spiky);
        DrawTri({x, y0}, {x + STEP, y1}, {x, edge}, col);
        DrawTri({x + STEP, y1}, {x + STEP, edge}, {x, edge}, col);
    }
}

// Calls fn(screenX, worldX) for every repeat of a pattern spaced `gap` apart on a layer at `off`.
template <typename F>
static void Repeat(float off, float gap, F fn) {
    float first = floorf(-off / gap) * gap;
    for (float wx = first - gap; wx < -off + SCREEN_W + gap; wx += gap) fn(wx + off, wx);
}

static std::vector<Vector2> CrystalSpots(const Game& g) {
    std::vector<Vector2> v;
    Repeat(LayerOffset(g, 0.7f), 330, [&](float sx, float wx) { v.push_back({sx + Hash1(wx) * 120, 446 + Hash1(wx + 3) * 8}); });
    return v;
}

static void DrawCaveLayers(Game& g) {
    float t = g.time;
    // 1. the far water, with bioluminescent haze drifting in it
    float deep = CAVE_TIER_LEVEL[g.dungeon.tier] / 6.0f; // deeper levels: darker water, more bones, more glowing things
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{(unsigned char)(30 - 16 * deep), (unsigned char)(78 - 40 * deep), (unsigned char)(94 - 40 * deep), 255},
                  Color{6, 20, 30, 255});
    Repeat(LayerOffset(g, 0.04f), 520, [&](float sx, float wx) {
        DrawCircleGradient((int)(sx + Hash1(wx) * 200), (int)(160 + Hash1(wx + 1) * 200), 160, Color{60, 140, 150, 50}, Color{60, 140, 150, 0});
    });
    BeginBlendMode(BLEND_ADDITIVE); // daylight through cracks far above
    Repeat(LayerOffset(g, 0.08f), 300, [&](float sx, float wx) {
        float x = sx + sinf(t * 0.2f + wx) * 20, w = 40 + Hash1(wx) * 40;
        DrawTri({x, 0}, {x + w, 0}, {x - 150, 520}, Color{60, 110, 120, 24});
        DrawTri({x + w, 0}, {x - 150 + w * 2, 520}, {x - 150, 520}, Color{60, 110, 120, 24});
    });
    EndBlendMode();
    // 2. the far cave walls
    DrawRidge(LayerOffset(g, 0.12f), 318, 70, 3, false, Color{16, 44, 56, 255}, 70);
    DrawRidge(LayerOffset(g, 0.2f), 372, 50, 11, false, Color{19, 48, 60, 255}, 40);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{30, 70, 84, 40}); // fog between layers
    Repeat(LayerOffset(g, 0.16f), 900, [&](float sx, float wx) { // schools of fish drifting past
        float dir = Hash1(wx) > 0.5f ? 1.0f : -1.0f, cx = sx + fmodf(t * 14 * dir + 9000, 900.0f) - 450, cy = 150 + Hash1(wx + 1) * 170;
        for (int f = 0; f < 16; f++) {
            float fx = cx + (Hash1(wx + f) - 0.5f) * 140 + sinf(t * 1.4f + f) * 4, fy = cy + (Hash1(wx + f + 30) - 0.5f) * 50 + cosf(t + f) * 3;
            DrawEllipse((int)fx, (int)fy, 6, 2, Color{70, 120, 130, 255});
            DrawTri({fx - dir * 5, fy}, {fx - dir * 10, fy - 3}, {fx - dir * 10, fy + 3}, Color{70, 120, 130, 255});
        }
    });
    // 3. distant rock columns rising from floor to ceiling
    Repeat(LayerOffset(g, 0.3f), 430, [&](float sx, float wx) {
        float x = sx + Hash1(wx) * 160, wTop = 60 + Hash1(wx + 2) * 40, wMid = 26 + Hash1(wx + 4) * 16, wBot = 80 + Hash1(wx + 5) * 40;
        Color c{24, 56, 68, 255};
        DrawTri({x - wTop, 40}, {x + wTop, 40}, {x + wMid, 260}, c);
        DrawTri({x - wTop, 40}, {x + wMid, 260}, {x - wMid, 260}, c);
        DrawTri({x - wMid, 260}, {x + wMid, 260}, {x + wBot, 470}, c);
        DrawTri({x - wMid, 260}, {x + wBot, 470}, {x - wBot, 470}, c);
        DrawTri({x + wMid * 0.2f, 60}, {x + wMid * 0.6f, 60}, {x + wMid * 0.5f, 440}, Color{40, 80, 92, 255}); // a lit seam
    });
    Repeat(LayerOffset(g, 0.34f), 2300, [&](float sx, float wx) { // a shipwreck settling into the silt
        float x = sx + Hash1(wx) * 600, base = 450;
        Color wreck{22, 48, 56, 255};
        for (int k = 0; k < 7; k++) { // the ribs of its hull
            float rx = x + k * 38, h = 150 - fabsf(k - 3.0f) * 18;
            DrawRing({rx, base}, h - 6, h, 200, 260 + k * 3.0f, 12, wreck);
        }
        DrawLineEx({x + 110, base - 20}, {x + 170, base - 260}, 8, wreck);            // the broken mast, leaning
        DrawLineEx({x + 150, base - 180}, {x + 230, base - 170}, 4, wreck);
        DrawRectangle((int)x - 20, (int)base - 30, 300, 30, wreck);
    });
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{30, 70, 84, 30});
    BeginBlendMode(BLEND_ADDITIVE);
    Repeat(LayerOffset(g, 0.38f), 520 - 180 * deep, [&](float sx, float wx) { // jellyfish, glowing faintly
        float x = sx + Hash1(wx) * 200, y = 140 + Hash1(wx + 2) * 180 + sinf(t * 0.6f + wx) * 18, pulse = 0.85f + 0.15f * sinf(t * 2.2f + wx);
        Color glow{120, 200, 230, 70};
        DrawCircleGradient((int)x, (int)y, 30, glow, Fade(glow, 0));
        DrawCircleSector({x, y}, 13 * pulse, 180, 360, 16, Color{150, 220, 240, 90});
        for (int k = 0; k < 5; k++) {
            Vector2 prev{x - 10 + k * 5.0f, y};
            for (int s2 = 1; s2 <= 6; s2++) {
                Vector2 q{prev.x + sinf(t * 2 + k + s2) * 1.5f, y + s2 * 7.0f};
                DrawLineEx(prev, q, 1.2f, Color{150, 220, 240, 70});
                prev = q;
            }
        }
    });
    EndBlendMode();
    // 4. the ceiling's stalactites, stalagmites and swaying kelp
    float off4 = LayerOffset(g, 0.45f);
    DrawRidge(off4, 70, 40, 5, true, Color{14, 30, 38, 255}, 130);
    DrawRidge(off4, 432, 22, 21, false, Color{20, 40, 48, 255}, 60);
    Repeat(LayerOffset(g, 0.55f), 170, [&](float sx, float wx) {
        float bx = sx + Hash1(wx) * 60, by = 462;
        int h = 8 + (int)(Hash1(wx + 9) * 5);
        Vector2 prev{bx, by};
        for (int s = 1; s <= h; s++) {
            Vector2 p{bx + sinf(t * 0.9f + wx + s * 0.45f) * s * 2.2f, by - s * 17.0f};
            DrawLineEx(prev, p, 6 - s * 0.35f, Color{34, 90, 60, 255});
            prev = p;
        }
    });
    // 5. the near wall behind the fighters, studded with glowing crystals
    float off5 = LayerOffset(g, 0.7f);
    DrawTiled(Tex::Rock, {0, 392, (float)SCREEN_W, 64}, 1.2f, Color{70, 86, 92, 255}, {-off5 / 1.2f, 0});
    for (int x = 0; x < SCREEN_W; x += 4) { // a ragged top edge
        float y = 392 - fabsf(sinf((x - off5) * 0.021f) * 16 + sinf((x - off5) * 0.07f) * 6);
        DrawRectangle(x, (int)y, 4, (int)(393 - y), Color{56, 70, 76, 255});
    }
    DrawVGradient({0, 392, (float)SCREEN_W, 64}, Fade(BLACK, 0.05f), Fade(BLACK, 0.45f));
    for (Vector2 c : CrystalSpots(g)) {
        for (int k = -2; k <= 2; k++) {
            float h = 26 - abs(k) * 6 + Hash1(c.x * 0.01f + k) * 8, lean = k * 7.0f;
            Vector2 base{c.x + k * 6.0f, c.y}, tip{c.x + k * 6.0f + lean, c.y - h};
            DrawTri({base.x - 4, base.y}, {base.x + 4, base.y}, tip, Color{80, 200, 210, 255});
            DrawTri({base.x - 1, base.y}, {base.x + 4, base.y}, tip, Color{150, 240, 240, 255});
        }
        DrawEllipse((int)c.x, (int)c.y + 2, 20, 5, Color{40, 60, 64, 255});
    }
    Repeat(LayerOffset(g, 0.6f), 1500, [&](float sx, float wx) { // an old anchor on a chain, hanging from above
        float x = sx + Hash1(wx + 5) * 400, sway = sinf(t * 0.5f + wx) * 6;
        for (int k = 0; k < 20; k++) DrawRing({x + sway * k / 20, 60 + k * 11.0f}, 3, 5, 0, 360, 8, Color{40, 46, 48, 255});
        Vector2 a{x + sway, 290};
        DrawLineEx(a, {a.x, a.y + 70}, 6, Color{44, 50, 52, 255});
        DrawRing({a.x, a.y + 50}, 26, 32, 20, 160, 16, Color{44, 50, 52, 255});
        DrawLineEx({a.x - 16, a.y + 12}, {a.x + 16, a.y + 12}, 5, Color{44, 50, 52, 255});
    });
    Repeat(LayerOffset(g, 0.7f), 700 - 300 * deep, [&](float sx, float wx) { // bones of things that came before
        float x = sx + Hash1(wx + 9) * 250, y = 450;
        Color bone{176, 172, 150, 255};
        for (int k = 0; k < 5; k++) DrawRing({x + k * 10.0f, y}, 12 - k * 1.2f, 14 - k * 1.2f, 180, 330, 8, bone); // a ribcage
        DrawCircleV({x - 16, y - 6}, 8, bone);                                       // a skull
        DrawCircleV({x - 19, y - 7}, 2.2f, Color{30, 30, 30, 255});
        DrawCircleV({x - 14, y - 7}, 2.2f, Color{30, 30, 30, 255});
    });
    // 6. the cave floor, with a wet lip and puddles
    float off6 = LayerOffset(g, 1.0f);
    DrawTiled(Tex::Rock, {0, 450, (float)SCREEN_W, 270}, 1.4f, Color{96, 110, 112, 255}, {-off6 / 1.4f, 0});
    DrawVGradient({0, 450, (float)SCREEN_W, 40}, Fade(BLACK, 0.55f), Fade(BLACK, 0));
    DrawRectangle(0, 450, SCREEN_W, 3, Color{120, 150, 150, 160});
    DrawVGradient({0, 600, (float)SCREEN_W, 120}, Fade(BLACK, 0), Fade(BLACK, 0.5f));
    Repeat(off6, 380, [&](float sx, float wx) {
        float px = sx + Hash1(wx) * 200, py = 505 + Hash1(wx + 1) * 80, w = 50 + Hash1(wx + 2) * 40;
        DrawEllipse((int)px, (int)py, w, 10, Color{40, 80, 90, 200});
        DrawEllipse((int)px - 10, (int)py - 2, w * 0.55f, 4, Color{110, 170, 180, 90});
    });
    Repeat(off6, 610, [&](float sx, float wx) { // vents in the floor, trickling bubbles
        float vx = sx + Hash1(wx + 4) * 300, vy = 470 + Hash1(wx + 6) * 30;
        DrawEllipse((int)vx, (int)vy, 10, 3, Color{30, 40, 44, 255});
        for (int k = 0; k < 6; k++) {
            float ph = fmodf(t * 0.5f + k / 6.0f + Hash1(wx), 1.0f);
            DrawCircleLines((int)(vx + sinf(ph * 9 + k) * 5), (int)(vy - ph * 420), 2 + (k % 2), Color{200, 235, 245, (unsigned char)(160 * (1 - ph))});
        }
    });
}

// 7. rocks and kelp right in front of the view: dark, and moving fastest of all
static void DrawCaveForeground(Game& g) {
    float t = g.time;
    Color fg{6, 12, 16, 255};
    Repeat(LayerOffset(g, 1.5f), 760, [&](float sx, float wx) {
        float x = sx + Hash1(wx) * 300;
        DrawCircle((int)x, 790, 170 + Hash1(wx + 1) * 90, fg);
        DrawCircle((int)(x + 180), 800, 110 + Hash1(wx + 2) * 50, fg);
    });
    Repeat(LayerOffset(g, 1.35f), 640, [&](float sx, float wx) {
        float bx = sx + Hash1(wx + 7) * 200;
        Vector2 prev{bx, 0};
        for (int s = 1; s <= 9; s++) {
            Vector2 p{bx + sinf(t * 0.7f + wx + s * 0.5f) * s * 3, s * 22.0f};
            DrawLineEx(prev, p, 16 - s, fg);
            prev = p;
        }
    });
}

// The creatures of the cave, built from lit forms like the crew, with jointed legs, plates and claws.
static void DrawEnemyFigure(const Enemy& e, Rectangle r, float t) {
    float cx = r.x + r.width / 2, by = r.y + r.height, bob = sinf(t * 2.5f + e.uid) * 3;
    auto legPair = [&](Vector2 hip, float reach, float kneeUp, float step, float w, Color col) {
        Vector2 knee{hip.x - reach * 0.45f, hip.y - kneeUp}, foot{hip.x - reach + step, by - 1};
        ShadeLimb(hip, knee, w, w * 0.8f, col);
        ShadeLimb(knee, foot, w * 0.8f, w * 0.5f, col);
    };
    switch (e.type) {
        case EnemyType::SeaLouse: { // a giant isopod: overlapping armoured plates on seven pairs of legs
            Color shell{150, 132, 170, 255}, seam{92, 78, 110, 255}, leg{104, 90, 124, 255};
            float cy = by - 30 + bob * 0.5f;
            for (int k = 0; k < 7; k++) legPair({cx - 26 + k * 9.0f, cy + 6}, 10, 4, sinf(t * 7 + k * 0.9f) * 3, 2.4f, leg);
            DrawTri({cx + 34, cy + 4}, {cx + 50, cy - 4}, {cx + 48, cy + 12}, seam);  // tail fan
            DrawTri({cx + 34, cy + 6}, {cx + 48, cy + 14}, {cx + 38, cy + 16}, seam);
            for (int k = 0; k < 8; k++) { // plates from tail to head, each overlapping the next
                float x = cx + 32 - k * 9.0f, rad = 13 + sinf((k + 0.5f) / 8 * PI) * 7;
                ShadeBall({x, cy + 2}, rad, shell);
                DrawRing({x + 1, cy + 2}, rad - 1.5f, rad, 200, 340, 10, seam);
            }
            ShadeBall({cx - 40, cy + 4}, 11, shell);                                    // head
            DrawEllipse((int)(cx - 44), (int)cy + 1, 5, 4, Color{30, 24, 36, 255});   // compound eye
            DrawCircleV({cx - 45.5f, cy - 0.5f}, 1.4f, Color{200, 210, 230, 255});
            ShadeLimb({cx - 46, cy - 2}, {cx - 64, cy - 26}, 2.0f, 1.3f, leg);           // antennae
            ShadeLimb({cx - 44, cy - 4}, {cx - 56, cy - 34}, 1.8f, 1.2f, leg);
        } break;
        case EnemyType::CaveShrimp: { // a pistol shrimp: curled abdomen, fan tail, and the snapping claw
            Color c{226, 120, 104, 255}, dk{170, 78, 70, 255}, lt{246, 178, 156, 255};
            float cy = by - 50 + bob;
            for (int k = 0; k < 3; k++) legPair({cx - 12 + k * 12.0f, cy + 12}, 12, 6, sinf(t * 6 + k) * 2, 2.6f, dk);
            Vector2 prev{cx - 2, cy};
            for (int k = 0; k < 5; k++) { // the abdomen curls down and back toward the tail
                float a = -0.4f + k * 0.42f;
                Vector2 q{cx + 6 + cosf(a) * 22 + k * 3, cy - 2 + sinf(a) * 16 + k * 3};
                ShadeBall(q, 13.0f - k * 1.6f, c);
                DrawRing(q, 12.0f - k * 1.6f, 13.0f - k * 1.6f, 230, 320, 8, dk);          // segment seams
                prev = q;
            }
            for (int k = -1; k <= 1; k++) DrawTri(prev, {prev.x + 14 + k * 3, prev.y + 12 + k * 7}, {prev.x + 6, prev.y + 16 + k * 5}, dk); // tail fan
            ShadeBall({cx - 12, cy - 4}, 17, c);                                          // carapace
            ShadeBall({cx - 24, cy - 2}, 12, c);
            DrawTri({cx - 30, cy - 8}, {cx - 44, cy - 6}, {cx - 30, cy - 2}, lt);         // rostrum
            for (int s = 0; s < 2; s++) {                                                 // stalked eyes
                Vector2 eye{cx - 30 + s * 6.0f, cy - 16 - s * 2.0f};
                ShadeLimb({cx - 26 + s * 6.0f, cy - 8}, eye, 2, 2, c);
                DrawCircleV(eye, 3, Color{24, 20, 20, 255});
                DrawCircleV({eye.x - 1, eye.y - 1}, 1, Color{230, 230, 220, 255});
            }
            ShadeLimb({cx - 32, cy - 10}, {cx - 64, cy - 40}, 1.8f, 1.1f, dk);          // antennae, swept back
            ShadeLimb({cx - 30, cy - 12}, {cx - 50, cy - 50}, 1.6f, 1.0f, dk);
            ShadeLimb({cx - 22, cy + 6}, {cx - 38, cy + 10}, 5, 4.5f, dk);               // the pistol claw
            ShadeBall({cx - 50, cy + 10}, 13, c);
            ShadeLimb({cx - 58, cy + 4}, {cx - 72, cy + 9}, 4, 2.4f, lt);
            ShadeBall({cx - 58, cy + 2}, 4, lt);
            ShadeLimb({cx - 20, cy + 10}, {cx - 34, cy + 20}, 2.4f, 2, dk);              // the small claw
            ShadeBall({cx - 36, cy + 21}, 4, c);
        } break;
        case EnemyType::BrineWorm: { // a bristle worm rearing up, jaws working
            Color c1{106, 170, 86, 255}, c2{84, 148, 70, 255}, bristle{200, 220, 150, 255};
            Vector2 head{cx, by};
            for (int k = 0; k <= 9; k++) {
                float u = k / 9.0f, rad = 15 - k * 0.6f;
                Vector2 q{cx + sinf(t * 1.6f + k * 0.7f) * 10 * u - k * 1.5f, by - 8 - k * 14.0f + bob * 0.5f * u};
                ShadeBall(q, rad, k % 2 ? c1 : c2);
                for (int sd = -1; sd <= 1; sd += 2) // parapodia with bristles
                    DrawLineEx({q.x + sd * rad * 0.8f, q.y}, {q.x + sd * (rad + 7), q.y - 3 + sinf(t * 5 + k) * 2}, 1.6f, bristle);
                head = q;
            }
            ShadeBall({head.x - 3, head.y - 4}, 12, c1);
            float jaw = 0.5f + 0.5f * sinf(t * 4 + e.uid);
            DrawTri({head.x - 10, head.y - 2}, {head.x - 22, head.y - 10 - jaw * 6}, {head.x - 14, head.y + 2}, Color{50, 40, 30, 255});
            DrawTri({head.x - 10, head.y + 2}, {head.x - 22, head.y + 8 + jaw * 6}, {head.x - 14, head.y - 2}, Color{50, 40, 30, 255});
            for (int k = 0; k < 4; k++) DrawCircleV({head.x - 6 + (k % 2) * 5.0f, head.y - 10 + (k / 2) * 4.0f}, 1.4f, Pal::Ink);
        } break;
        case EnemyType::Lobster: { // armoured, spiked, with a crusher claw held high
            Color c{182, 50, 42, 255}, dk{130, 34, 30, 255}, lt{222, 98, 78, 255};
            float cy = by - 72 + bob;
            for (int k = 0; k < 4; k++) legPair({cx - 8 + k * 14.0f, cy + 22}, 16, 10, sinf(t * 5 + k) * 3, 3.6f, dk);
            Vector2 prev{cx + 30, cy + 4};
            for (int k = 0; k < 6; k++) { // the tail, curling under
                float a = -0.2f + k * 0.38f;
                Vector2 q{cx + 34 + cosf(a) * 30 + k * 4, cy + 6 + sinf(a) * 22 + k * 4};
                ShadeBall(q, 17.0f - k * 1.7f, c);
                DrawRing(q, 16.0f - k * 1.7f, 17.0f - k * 1.7f, 220, 320, 8, dk);
                prev = q;
            }
            for (int k = -1; k <= 1; k++) DrawTri(prev, {prev.x + 20 + k * 6, prev.y + 16 + k * 10}, {prev.x + 8, prev.y + 24 + k * 6}, dk);
            ShadeBall({cx + 6, cy}, 34, c);                                               // carapace
            for (int k = 0; k < 6; k++) DrawTri({cx - 18 + k * 9.0f, cy - 28 + fabsf(k - 2.5f) * 2}, {cx - 12 + k * 9.0f, cy - 30 + fabsf(k - 2.5f) * 2},
                                                {cx - 15 + k * 9.0f, cy - 38 + fabsf(k - 2.5f) * 2}, lt); // spines
            ShadeBall({cx - 30, cy - 8}, 21, c);                                          // head
            DrawTri({cx - 44, cy - 16}, {cx - 66, cy - 18}, {cx - 46, cy - 8}, lt);       // rostrum
            for (int s = 0; s < 2; s++) {
                Vector2 eye{cx - 40 + s * 10.0f, cy - 30};
                ShadeLimb({cx - 36 + s * 10.0f, cy - 22}, eye, 2.4f, 2.4f, c);
                DrawCircleV(eye, 4.5f, Color{20, 16, 16, 255});
                DrawCircleV({eye.x - 1.5f, eye.y - 1.5f}, 1.4f, Color{230, 230, 220, 255});
            }
            ShadeLimb({cx - 42, cy - 26}, {cx - 92, cy - 170}, 2.6f, 1.1f, dk);          // whip antennae
            ShadeLimb({cx - 34, cy - 28}, {cx - 64, cy - 186}, 2.6f, 1.1f, dk);
            ShadeLimb({cx - 36, cy - 2}, {cx - 52, cy - 38}, 8, 7, c);                   // the crusher, raised
            ShadeLimb({cx - 52, cy - 38}, {cx - 66, cy - 66}, 7, 7, c);
            ShadeBall({cx - 68, cy - 84}, 23, c);
            ShadeLimb({cx - 78, cy - 100}, {cx - 96, cy - 118}, 7, 4, lt);               // its jaw, open
            for (int k = 0; k < 4; k++) DrawCircleV({cx - 82 - k * 4.0f, cy - 102 - k * 4.0f}, 2, Color{240, 220, 200, 255});
            ShadeLimb({cx - 30, cy + 10}, {cx - 50, cy + 18}, 7, 6, c);                  // the cutter, low
            ShadeBall({cx - 62, cy + 20}, 15, c);
            ShadeLimb({cx - 70, cy + 14}, {cx - 86, cy + 18}, 5, 3, lt);
        } break;
    }
}


// ---------------------------------------------------------------- drawing: animation
// Smooth 0 -> 1 -> 0 envelope: rises from a to its peak at b, falls back to zero at c.
static float Bell(float u, float a, float b, float c) {
    auto sm = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };
    if (u <= a || u >= c) return 0;
    return u < b ? sm((u - a) / (b - a)) : sm((c - u) / (c - b));
}

struct AnimFx { Pose pose; float dx = 0, dy = 0; Color tint = WHITE; };

// Each class has its own way of fighting: the Nurse's quick slash, the Diver's long harpoon lunge,
// the Captain's overhead cutlass cut, the Mechanic's heavy wrench swing.
static AnimFx HeroAnimFx(const Game& g, const Hero& h) {
    AnimFx fx;
    const UnitAnim* a = FindAnim(g, true, h.id);
    if (!a) return fx;
    float u = a->t;
    Pose& p = fx.pose;
    int c = (int)h.cls;
    switch (a->kind) {
        case Anim::Melee: {
            struct Style { float raise, windLean, windCrouch, lunge, tilt, reach, lean, crouch; };
            const Style S[4] = {{0.45f, -0.15f, 0.0f, 70, 35, 1.0f, 0.45f, 0.1f},    // Nurse
                                {0.0f, -0.3f, 0.35f, 95, 10, 1.0f, 0.6f, 0.25f},    // Diver
                                {1.0f, -0.25f, 0.0f, 60, 95, 0.7f, 0.5f, 0.05f},    // Captain
                                {1.0f, -0.4f, 0.2f, 45, 80, 0.5f, 0.65f, 0.45f}};   // Mechanic
            const Style& s = S[c];
            float wind = Bell(u, 0, 0.26f, 0.38f), strike = Bell(u, 0.28f, 0.38f, 0.88f);
            p.raise = wind * s.raise;
            p.lean = wind * s.windLean + strike * s.lean;
            p.crouch = wind * s.windCrouch + strike * s.crouch;
            p.reach = strike * s.reach;
            p.weaponTilt = strike * s.tilt;
            // follow-through: after the blow the body rocks back a little past neutral before settling
            float settle = Bell(u, 0.62f, 0.76f, 0.98f);
            p.lean -= settle * 0.12f;
            fx.dx = strike * s.lunge - wind * 8 - settle * 7;
        } break;
        case Anim::Ranged: {
            float aim = Bell(u, 0, 0.22f, 0.95f), recoil = Bell(u, 0.3f, 0.36f, 0.62f);
            if (h.cls == HeroClass::Nurse) { // wind up and throw
                p.raise = Bell(u, 0, 0.22f, 0.34f) * 0.9f;
                p.reach = Bell(u, 0.28f, 0.36f, 0.8f);
                p.weaponTilt = p.reach * 40;
                p.lean = p.reach * 0.35f - p.raise * 0.2f;
            } else if (h.cls == HeroClass::Mechanic) { // brace and vent
                p.crouch = aim * 0.4f;
                p.reach = aim * 0.6f;
                fx.dx = -recoil * 6;
            } else { // aim, fire, recoil
                p.reach = aim;
                p.crouch = aim * 0.2f;
                p.weaponTilt = h.cls == HeroClass::Captain ? aim * 60 : 0;
                p.lean = -recoil * 0.35f;
                fx.dx = -recoil * 12;
            }
        } break;
        case Anim::Heal: {
            float e = Bell(u, 0, 0.3f, 0.92f);
            switch (h.cls) {
                case HeroClass::Nurse: p.reach = e * 0.7f; p.raise = e * 0.25f; p.crouch = e * 0.2f; p.lean = e * 0.2f; break;
                case HeroClass::Captain: p.raise = e * 0.6f; p.backRaise = e * 0.5f; break;
                case HeroClass::Mechanic: p.crouch = e * 0.5f; p.reach = e * 0.4f; p.lean = e * 0.3f; break;
                default: p.crouch = e * 0.3f; break;
            }
        } break;
        case Anim::Buff: {
            float e = Bell(u, 0, 0.3f, 0.92f);
            switch (h.cls) {
                case HeroClass::Captain: p.raise = e; p.backRaise = e * 0.8f; p.lean = -e * 0.15f; break;  // sword aloft
                case HeroClass::Diver: p.crouch = e * 0.7f; p.lean = e * 0.3f; break;                      // into the ink
                case HeroClass::Mechanic: p.crouch = e * 0.5f; p.backRaise = e * 0.4f; p.lean = -e * 0.1f; break;
                default: p.raise = e * 0.5f; break;
            }
        } break;
        case Anim::Hurt: { // knocked back, then a damped wobble as they recover their footing
            float spring = expf(-7 * u) * cosf(u * 16) * std::min(1.0f, u / 0.04f), b = Bell(u, 0, 0.07f, 0.48f);
            fx.dx = -18 * spring;
            p.lean = -0.5f * spring;
            p.crouch = 0.25f * b;
            fx.tint = {255, (unsigned char)(255 - 120 * b), (unsigned char)(255 - 130 * b), 255};
        } break;
        case Anim::Dodge: {
            float b = Bell(u, 0, 0.15f, 0.45f);
            fx.dx = -26 * b;
            p.crouch = 0.35f * b;
            p.lean = -0.2f * b;
        } break;
        default: break;
    }
    return fx;
}

static AnimFx EnemyAnimFx(const Game& g, const Enemy& e) {
    AnimFx fx;
    const UnitAnim* a = FindAnim(g, false, e.uid);
    if (!a) return fx;
    float u = a->t;
    switch (a->kind) {
        case Anim::Melee: fx.dx = -85 * Bell(u, 0.2f, 0.36f, 0.8f) + 12 * Bell(u, 0, 0.16f, 0.28f); break;
        case Anim::Ranged: fx.dx = 10 * Bell(u, 0.05f, 0.25f, 0.45f); fx.dy = -6 * Bell(u, 0.25f, 0.32f, 0.5f); break;
        case Anim::Buff: fx.dy = -10 * Bell(u, 0.1f, 0.3f, 0.8f); fx.dx = 4 * sinf(u * 60) * Bell(u, 0.1f, 0.3f, 0.8f); break;
        case Anim::Hurt: {
            float spring = expf(-7 * u) * cosf(u * 16) * std::min(1.0f, u / 0.04f), b = Bell(u, 0, 0.07f, 0.48f);
            fx.dx = 20 * spring;
            fx.dy = -4 * b;
            fx.tint = {255, (unsigned char)(255 - 120 * b), (unsigned char)(255 - 130 * b), 255};
        } break;
        case Anim::Dodge: fx.dx = 28 * Bell(u, 0, 0.15f, 0.45f); break;
        default: break;
    }
    if (!e.alive) fx.tint.a = (unsigned char)(255 * std::max(0.0f, 1 - (a->kind == Anim::Hurt ? a->t / a->dur : 1)));
    return fx;
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

// Where each fighter is drawn, easing toward their rank's spot so swaps and shoves slide instead of jumping.
static float ShownX(int key, float target, float dt) {
    static std::unordered_map<int, float> shown;
    auto it = shown.find(key);
    if (it == shown.end() || fabsf(it->second - target) > 600) return shown[key] = target;
    it->second += (target - it->second) * std::min(1.0f, dt * 7);
    return it->second;
}

static void DrawUnitFigures(Game& g) {
    auto& d = g.dungeon;
    float t = g.time, dt = GetFrameTime();
    bool walking = d.phase == DPhase::Walking;
    for (int p = PARTY_SIZE - 1; p >= 0; p--) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        AnimFx fx = HeroAnimFx(g, *h);
        // alive even when standing still: breathing, and a slow shift of weight from foot to foot
        float ph = t + h->id * 2.3f;
        fx.pose.lean += 0.035f * sinf(ph * 1.1f);
        fx.pose.crouch += 0.04f * (0.5f + 0.5f * sinf(ph * 1.7f));
        fx.dx += sinf(ph * 0.6f) * 1.5f;
        Vector2 feet{ShownX(h->id, r.x + r.width / 2, dt) + fx.dx + gShake.x, r.y + r.height + fx.dy + gShake.y};
        DrawShadowBlob({feet.x, r.y + r.height}, 38);
        DrawCrewFigureInked(*h, feet, 1.08f, true, walking ? d.walkT * 9 + p * 1.3f : 0, t, fx.pose, fx.tint);
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        AnimFx fx = EnemyAnimFx(g, e);
        if (fx.tint.a == 0) continue;
        Rectangle r = EnemyRect(g, p);
        Vector2 feet{ShownX(1000000 + e.uid, r.x + r.width / 2, dt) + fx.dx + gShake.x, r.y + r.height + fx.dy + gShake.y}, ff = FigureFeet();
        DrawShadowBlob({feet.x, r.y + r.height}, e.boss ? 70 : 44);
        BeginFigure(); // draw on the figure canvas, lined up so its feet land on FigureFeet()
        DrawEnemyFigure(e, {ff.x - r.width / 2, ff.y - r.height, r.width, r.height}, t);
        EndFigure(feet, fx.tint);
    }
}

// Thrown vials, harpoon bolts, pistol shot, steam, spit and sonic pops.
static void DrawProjectiles(Game& g) {
    for (auto& s : g.dungeon.shots) {
        float u = std::clamp(s.t / s.dur, 0.0f, 1.0f);
        bool arc = s.kind == 0 || s.kind == 10;
        Vector2 p{s.from.x + (s.to.x - s.from.x) * u, s.from.y + (s.to.y - s.from.y) * u - (arc ? sinf(u * PI) * 70 : 0)};
        switch (s.kind) {
            case 0: // the Nurse's vial, tumbling
                DrawRectanglePro({p.x, p.y, 8, 14}, {4, 7}, u * 720, Color{120, 220, 130, 255});
                Glow(p, 16, Color{100, 255, 120, 90});
                break;
            case 1: // harpoon bolt
                DrawLineEx({p.x - 30, p.y}, p, 3, Color{200, 204, 210, 255});
                DrawTri({p.x + 8, p.y}, {p.x - 2, p.y - 5}, {p.x - 2, p.y + 5}, Color{200, 204, 210, 255});
                break;
            case 2: // shot
                DrawLineEx({p.x - 40, p.y}, p, 2, Color{255, 220, 150, 150});
                Glow(p, 14, Color{255, 220, 150, 200});
                break;
            case 3: // scalding steam
                for (int k = 0; k < 5; k++) DrawCircleV({p.x - k * 16.0f, p.y + sinf(k * 1.7f) * 8}, 12 + k * 3.0f, Color{230, 236, 236, (unsigned char)(160 - k * 28)});
                break;
            case 10: // spit
                DrawCircleV(p, 7, Color{120, 200, 80, 255});
                Glow(p, 14, Color{120, 255, 80, 80});
                break;
            default: // a sonic pop: rings spreading as they travel
                for (int k = 0; k < 3; k++) DrawRing(p, 10 + k * 8.0f, 12 + k * 8.0f, 110, 250, 16, Fade(Color{220, 230, 255, 255}, 0.7f - k * 0.2f));
                break;
        }
    }
}

static void DrawSparks(Game& g) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& s : g.dungeon.sparks) {
        float a = std::clamp(s.life / s.max, 0.0f, 1.0f);
        DrawCircleV(s.p, s.size * (0.5f + a * 0.5f), Fade(s.c, a));
    }
    EndBlendMode();
}

static void DrawUnitHud(Game& g, int actingHero, int actingEnemy) {
    auto& d = g.dungeon;
    float t = g.time;
    for (int p = 0; p < PARTY_SIZE; p++) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        Stats s = GetStats(*h);
        if (h->rattled) Glow({r.x + r.width / 2, r.y - 24}, 40 + sinf(t * 5) * 6, Fade(Pal::Stress, 0.6f));
        if (h->deathsDoor) Glow({r.x + r.width / 2, r.y + 60}, 80, Fade(Pal::Bad, 0.25f + 0.15f * sinf(t * 6)));
        DrawBar({r.x, r.y + r.height + 8, r.width, 8}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({r.x, r.y + r.height + 19, r.width, 5}, h->stress / 100.0f, Pal::Stress);
        float nw = (float)MeasureTxt(h->name, 16, true);
        TxtShadow(h->name, r.x + r.width / 2 - nw / 2, r.y + r.height + 28, 16, Pal::Paper, true);
        DrawTextCentered(TextFormat("Lv %d   %d/%d HP", h->level, h->hp, s.maxHp), r.x + r.width / 2, r.y + r.height + 47, 13, Color{220, 220, 200, 255});
        std::string tags = StatusTags(h->st);
        if (h->deathsDoor) tags += "DEATH'S DOOR ";
        if (h->rattled) tags += "RATTLED";
        float tw = (float)MeasureTxt(tags, 12, true);
        TxtShadow(tags, r.x + r.width / 2 - tw / 2, r.y - 46, 12, Pal::Coral, true);
        if (h->id == actingHero) {
            float y = r.y - 72 + sinf(t * 6) * 4;
            Glow({r.x + r.width / 2, y + 6}, 22, Fade(Pal::Brass, 0.7f));
            DrawTri({r.x + r.width / 2 - 12, y}, {r.x + r.width / 2, y + 14}, {r.x + r.width / 2 + 12, y}, Pal::Brass);
        }
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        if (!e.alive) continue;
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
    float t = g.time, L = d.lightShown / 100.0f;
    if (d.batteryT > 0.7f) L *= 0.35f + 0.65f * (sinf(t * 47) * sinf(t * 23) > 0 ? 1.0f : 0.3f); // the torch sputters as the battery goes in
    auto lerp = [](float a, float b, float k) { return (unsigned char)(a + (b - a) * k); };
    LightsBegin(Color{lerp(18, 84, L), lerp(22, 96, L), lerp(34, 108, L), 255});
    float flick = 0.95f + 0.05f * sinf(t * 17) * sinf(t * 5.3f);
    AddLight({470, 330}, 300 + 480 * L, Color{255, 214, 150, 255}, (0.45f + 0.5f * L) * flick); // the party's flashlight glow
    AddCone({560, 320}, 0.05f, 0.42f, 420 + 480 * L, Color{255, 226, 170, 255});
    Repeat(LayerOffset(g, 0.08f), 300, [&](float sx, float) { AddLight({sx - 60, 120}, 240, Color{70, 120, 130, 255}, 0.4f); });
    std::vector<Vector2> crystals = CrystalSpots(g);
    for (Vector2 c : crystals) AddLight({c.x, c.y - 14}, 130, Color{90, 220, 210, 255}, 0.65f);
    for (auto& s : d.shots) AddLight({s.from.x + (s.to.x - s.from.x) * std::clamp(s.t / s.dur, 0.0f, 1.0f), s.from.y}, 120, Color{255, 220, 170, 255}, 0.4f);
    LightsEnd();
    for (Vector2 c : crystals) Glow({c.x, c.y - 12}, 26, Color{90, 230, 220, 80});
    for (int k = 0; k < 40; k++) { // marine snow drifting through the beam
        float px = fmodf(k * 97.0f + t * (6 + k % 5) + LayerOffset(g, 0.9f) * -1 + 100000, (float)SCREEN_W);
        float py = fmodf(k * 53.0f + t * (10 + k % 7), 520.0f) + 40;
        DrawCircle((int)px, (int)py, 1.3f + (k % 3) * 0.5f, Color{220, 240, 240, (unsigned char)(50 + 60 * L)});
    }
}

static void DrawTopBar(Game& g) {
    auto& d = g.dungeon;
    DrawVGradient({0, 0, (float)SCREEN_W, 58}, Color{10, 18, 24, 240}, Color{16, 28, 36, 220});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    TxtShadow(TextFormat("THE CAVE  -  %s (Lv %d)", CAVE_TIER_NAME[d.tier], CAVE_TIER_LEVEL[d.tier]), 20, 15, 22, Pal::Brass, true);
    for (int i = 0; i < (int)d.rooms.size(); i++) {
        float x = 380 + i * 30.0f;
        Color c = i < d.roomIndex ? Pal::Good : i == d.roomIndex ? Pal::Brass : Color{90, 100, 104, 255};
        if (d.rooms[i] == RoomType::Boss) DrawPoly({x + 10, 28}, 4, 12, 45, c);
        else DrawCircle((int)x + 10, 28, 8, c);
    }
    Txt("Light", 590, 6, 16, Pal::Paper);
    DrawBar({590, 28, 180, 14}, d.lightShown / 100.0f, Color{250, 220, 120, 255});
    TxtShadow(LightName(d.light), 782, 22, 19, Color{250, 220, 120, 255});
    Txt(TextFormat("Loot: %d gold, %d relic%s", d.lootGold, (int)d.lootRelics.size(), d.lootRelics.size() == 1 ? "" : "s"), 930, 18, 19, Pal::Paper);
}

// Returns true if the button to leave was pressed.
static bool ResultPanel(const char* title, const std::string& body, const char* button, Color titleColor) {
    Rectangle p{340, 140, 600, 330};
    Panel(p);
    DrawTextCenteredBold(title, p.x + p.width / 2, p.y + 24, 34, titleColor);
    DrawWrapped(body, {p.x + 40, p.y + 80, p.width - 80, 180}, 18, Pal::Ink);
    return Button({p.x + 150, p.y + p.height - 66, 300, 48}, button);
}

// ---------------------------------------------------------------- actions in motion
static Anim KindFor(const Ability& a) {
    if (a.target == Target::Enemy) return a.ranged ? Anim::Ranged : Anim::Melee;
    return (a.heal || a.stressHeal || a.cure) ? Anim::Heal : Anim::Buff;
}

static void BeginHeroAction(Game& g, int heroId, int ability, int target) {
    Hero* h = FindHero(g, heroId);
    auto& p = g.dungeon.pending;
    p = PendingAction{};
    p.active = true;
    p.hero = true;
    p.id = heroId;
    p.ability = ability;
    p.target = target;
    p.kind = KindFor(ClassAbilities(h->cls)[ability]);
    switch (p.kind) {
        case Anim::Melee: p.impact = 0.36f; p.end = 0.95f; break;
        case Anim::Ranged: p.fire = 0.32f; p.impact = 0.6f; p.end = 1.05f; break;
        default: p.impact = 0.42f; p.end = 1.0f; break;
    }
    StartAnim(g, true, heroId, p.kind, p.end);
}

static void BeginEnemyAction(Game& g, int uid) {
    int ab = EnemyPick(g, uid);
    Enemy* e = FindEnemy(g, uid);
    auto& p = g.dungeon.pending;
    p = PendingAction{};
    p.active = true;
    p.id = uid;
    p.ability = ab;
    if (ab < 0) { p.kind = Anim::None; p.impact = 0; p.end = 0.3f; return; }
    const EnemyAbility& a = e->abilities[ab];
    p.kind = a.dmgMult <= 0 ? Anim::Buff : (a.hits & (RANK_3 | RANK_4)) ? Anim::Ranged : Anim::Melee;
    switch (p.kind) {
        case Anim::Melee: p.impact = 0.36f; p.end = 0.9f; break;
        case Anim::Ranged: p.fire = 0.25f; p.impact = 0.55f; p.end = 0.95f; break;
        default: p.impact = 0.4f; p.end = 0.9f; g.dungeon.shake = 0.3f; break;
    }
    StartAnim(g, false, uid, p.kind, p.end);
}

static void UpdatePending(Game& g, float dt) {
    auto& d = g.dungeon;
    auto& p = d.pending;
    if (!p.active) return;
    p.t += dt;
    if (p.kind == Anim::Ranged && !p.fired && p.t >= p.fire) {
        p.fired = true;
        Projectile s{};
        s.dur = p.impact - p.fire;
        s.hero = p.hero;
        if (p.hero) {
            Hero* h = FindHero(g, p.id);
            Rectangle hr = HeroRect(std::max(0, PartyPos(g, p.id)));
            int tp = std::clamp(p.target, 0, std::max(0, (int)d.enemies.size() - 1));
            Rectangle er = EnemyRect(g, tp);
            s.from = {hr.x + hr.width / 2 + 40, hr.y + 62};
            s.to = {er.x + er.width / 2, er.y + er.height * 0.5f};
            s.kind = h ? (int)h->cls : 1;
        } else {
            Enemy* e = FindEnemy(g, p.id);
            Rectangle er = EnemyRect(g, std::max(0, EnemyPos(g, p.id)));
            Rectangle hr = HeroRect(std::min(1, PartySize(g) - 1));
            s.from = {er.x + er.width / 2 - 30, er.y + er.height * 0.4f};
            s.to = {hr.x + hr.width / 2, hr.y + 70};
            s.kind = e && e->type == EnemyType::CaveShrimp ? 11 : 10;
        }
        d.shots.push_back(s);
    }
    if (!p.applied && p.t >= p.impact) {
        p.applied = true;
        if (p.hero) HeroAct(g, p.id, p.ability, p.target);
        else EnemyAct(g, p.id, p.ability);
    }
    if (p.t >= p.end) {
        p.active = false;
        EndTurn(g);
    }
}

static void UpdateEffects(Game& g, float dt) {
    auto& d = g.dungeon;
    for (auto& a : d.anims) a.t += dt;
    d.anims.erase(std::remove_if(d.anims.begin(), d.anims.end(), [](const UnitAnim& a) { return a.t >= a.dur; }), d.anims.end());
    for (auto& s : d.shots) s.t += dt;
    d.shots.erase(std::remove_if(d.shots.begin(), d.shots.end(), [](const Projectile& s) { return s.t >= s.dur; }), d.shots.end());
    for (auto& s : d.sparks) {
        s.p.x += s.v.x * dt;
        s.p.y += s.v.y * dt;
        s.v.x *= 1 - dt * 2;
        s.v.y = s.v.y * (1 - dt * 2) + 40 * dt;
        s.life -= dt;
    }
    d.sparks.erase(std::remove_if(d.sparks.begin(), d.sparks.end(), [](const Spark& s) { return s.life <= 0; }), d.sparks.end());
    d.shake = std::max(0.0f, d.shake - dt);
    if (d.phase == DPhase::Corridor) d.corridorT += dt;
    d.batteryT = std::max(0.0f, d.batteryT - dt);
    if (d.batteryT <= 0.7f) d.lightShown += std::clamp(d.light - d.lightShown, -40 * dt, 40 * dt); // light eases to its new level
    float k = d.shake * 22;
    gShake = {sinf(g.time * 70) * k, cosf(g.time * 57) * k * 0.6f};
}

// ---------------------------------------------------------------- the scene
void SceneDungeon(Game& g) {
    auto& d = g.dungeon;
    float dt = GetFrameTime();
    SetPost(0.5f, 0.035f, 0.45f);
    UpdateEffects(g, dt);

    // ---------------- walking between rooms: the whole cave slides past
    if (d.phase == DPhase::Walking) {
        d.walkT += dt;
        float speed = 240 * std::min(1.0f, d.walkT / 0.3f) * std::min(1.0f, std::max(0.0f, (2.0f - d.walkT) / 0.3f));
        d.scroll += speed * dt;
        if (d.walkT >= 2.0f) EnterNextRoom(g);
    }

    // ---------------- combat logic
    int actingHero = -1, actingEnemy = -1;
    if (d.phase == DPhase::Combat) {
        UpdatePending(g, dt);
        if (d.pending.active) {
            if (d.pending.hero) actingHero = d.pending.id; else actingEnemy = d.pending.id;
        } else if (d.phase == DPhase::Combat) {
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
                    if (d.actTimer > 0.35f) BeginEnemyAction(g, te.id);
                }
            }
        }
    }

    // ---------------- drawing
    DrawCaveLayers(g);
    DrawUnitFigures(g);
    DrawProjectiles(g);
    DrawCaveLighting(g);
    DrawCaveForeground(g);
    InkPass(1.0f, 1.0f);
    DrawSparks(g);
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
        case DPhase::Walking: break;
        case DPhase::Corridor: {
            // The party takes a breath before the choice comes up; the panel then eases down into place,
            // and its buttons only work once it has settled (and while no battery is being swapped).
            const float DELAY = 1.4f, SLIDE = 0.45f;
            if (d.corridorT < DELAY) {
                if (d.roomIndex >= 0) DrawTextCentered("The crew catch their breath...", SCREEN_W / 2.0f, 610, 20, Color{200, 210, 210, 200});
                break;
            }
            float u = std::min(1.0f, (d.corridorT - DELAY) / SLIDE), ease = 1 - (1 - u) * (1 - u) * (1 - u);
            bool ready = u >= 1 && d.batteryT <= 0;
            Rectangle p{400, 150 - (1 - ease) * 260, 480, 270};
            Panel(p);
            const char* head = d.roomIndex < 0 ? "At the cave mouth" : TextFormat("Room %d of %d cleared", d.roomIndex + 1, (int)d.rooms.size());
            DrawTextCenteredBold(head, p.x + p.width / 2, p.y + 20, 28, Pal::Ink);
            bool nextIsBoss = d.rooms[d.roomIndex + 1] == RoomType::Boss;
            DrawTextCentered(nextIsBoss ? "Heavy clacking echoes from the next chamber..." : "The passage winds deeper.",
                             p.x + p.width / 2, p.y + 60, 18, nextIsBoss ? Pal::Bad : Pal::BrassDk);
            int drain = LightDrainPerRoom(g);
            if (Button({p.x + 40, p.y + 96, 400, 46}, TextFormat(nextIsBoss ? "Face the Lobster  (-%d light)" : "Advance  (-%d light)", drain), ready)) {
                d.light = std::max(0.0f, d.light - drain);
                d.phase = DPhase::Walking;
                d.walkT = 0;
            }
            const char* swap = d.batteryT > 0 ? "Swapping the battery..." : TextFormat("Swap in a battery  (+40 light)   [%d left]", g.batteries);
            if (Button({p.x + 40, p.y + 150, 400, 42}, swap, ready && g.batteries > 0 && d.light < 100)) {
                g.batteries--;
                d.light = std::min(100.0f, d.light + 40);
                d.batteryT = 1.4f;
            }
            if (Button({p.x + 40, p.y + 200, 400, 42}, d.roomIndex < 0 ? "Turn back" : "Retreat with the loot (+10 stress)", ready)) {
                if (d.roomIndex < 0) { g.scene = Scene::Hub; return; }
                d.phase = DPhase::Retreat;
            }
        } break;

        case DPhase::Treasure: {
            std::string body = TextFormat("A barnacled chest! +%d gold.", d.roomGold);
            if (d.roomRelic >= 0) body += " Inside, wrapped in oilcloth: a " + Relics()[d.roomRelic].name + "!";
            if (d.light < 50) body += "\n\nThe darkness made the find richer.";
            if (ResultPanel("Treasure", body, "Continue", Pal::Brass)) { d.phase = DPhase::Corridor; d.corridorT = 0; }
        } break;

        case DPhase::RoomClear: {
            std::string body = TextFormat("The room is quiet again. You gather %d gold from the debris.", d.roomGold);
            if (ResultPanel("Room cleared", body, "Continue", Pal::Good)) { d.phase = DPhase::Corridor; d.corridorT = 0; }
        } break;

        case DPhase::Victory:
        case DPhase::Retreat:
        case DPhase::Defeat: {
            ApplyResults(g);
            std::string body;
            const char* title;
            Color tc;
            int lvl = CAVE_TIER_LEVEL[d.tier];
            if (d.phase == DPhase::Victory) {
                title = "Expedition complete!";
                tc = Pal::Good;
                body = TextFormat("The Lobster is beaten. You bring home %d gold", d.lootGold);
                for (int r : d.lootRelics) body += ", a " + Relics()[r].name;
                body += ", and as a reward for finishing: a " + Relics()[d.rewardRelic].name + ".";
                body += TextFormat("\n\nSurvivors earn %d XP.", 5 + lvl * 2);
                if (d.tier + 1 < CAVE_TIERS && g.caveTierCleared == d.tier)
                    body += TextFormat(" Cave level %d (%s) is now open at the Helm.", CAVE_TIER_LEVEL[d.tier + 1], CAVE_TIER_NAME[d.tier + 1]);
            } else if (d.phase == DPhase::Retreat) {
                title = "Retreat";
                tc = Pal::Brass;
                body = TextFormat("The crew scrambles back to the Nautilus with %d gold", d.lootGold);
                body += d.lootRelics.empty() ? "." : " and the relics they found.";
                body += TextFormat("\n\nNo completion reward. Survivors earn %d XP and +10 stress.", 2 + lvl);
            } else {
                title = "Lost to the depths";
                tc = Pal::Bad;
                body = "The whole party has fallen, along with their relics and everything they carried.";
                if (g.roster.size() == 1 && PartySize(g) == 1) body += "\n\nA stowaway creeps out of the cargo hold and volunteers.";
            }
            if (!d.levelUps.empty()) body += "\n\nLevel up! " + d.levelUps + ".";
            if (ResultPanel(title, body, "Return to the Nautilus", tc)) g.scene = Scene::Hub;
        } break;

        case DPhase::Combat: {
            Rectangle bar{20, 560, SCREEN_W - 40.0f, 148};
            DrawRectangleRounded({bar.x + 3, bar.y + 5, bar.width, bar.height}, 0.08f, 6, Fade(BLACK, 0.4f));
            DrawRectangleRounded(bar, 0.08f, 6, Color{14, 24, 32, 235});
            DrawRectangleRoundedLinesEx(bar, 0.08f, 6, 3, Pal::BrassDk);
            Hero* h = actingHero >= 0 ? FindHero(g, actingHero) : nullptr;
            if (!h || d.pendingSkip || !d.turnStarted || d.pending.active) {
                const char* who = h ? h->name.c_str() : actingEnemy >= 0 && FindEnemy(g, actingEnemy) ? FindEnemy(g, actingEnemy)->name.c_str() : "...";
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
                        BeginHeroAction(g, heroId, i, pos);
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
                        BeginHeroAction(g, heroId, d.selectedAbility, tp);
                        return;
                    }
                }
            }
        } break;
    }
}
