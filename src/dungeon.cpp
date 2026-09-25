// ============================================================================
//  DEPTH - the roguelike expedition: rooms, the flashlight, and combat.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

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

static void HealHero(Hero& h, int amt) {
    h.hp = std::min(GetStats(h).maxHp, h.hp + amt);
    if (h.hp > 0) h.deathsDoor = false;
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
                if (a.poison) { e->st.poisonDmg = std::max(e->st.poisonDmg, a.poison); e->st.poisonTurns = 3; Float(g, er, "Poison", Pal::Good); }
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
        if (a.heal) { int amt = a.heal + Roll(0, 2); HealHero(*t, amt); Float(g, tr, "+" + std::to_string(amt), Pal::Good); }
        if (a.cure) { t->st.bleedTurns = 0; t->st.poisonTurns = 0; }
        if (a.stressHeal) AddStress(g, *t, -a.stressHeal);
        if (a.buffDmg) { t->st.buffDmg = a.buffDmg; t->st.buffTurns = 3; Float(g, tr, "Rallied", Pal::Brass); }
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
        int hit = std::clamp(e->acc + EnemyAccBonus(g) - s.dodge, 5, 95);
        if (!Chance(hit)) { Float(g, hr, "Dodge", Pal::Paper); continue; }
        bool crit = Chance(6);
        if (a.dmgMult > 0) {
            int prot = std::min(80, s.prot + (h->st.guardTurns > 0 ? 25 : 0));
            float raw = Roll(e->dmgMin, e->dmgMax) * a.dmgMult * (crit ? 1.5f : 1.0f);
            int dmg = std::max(1, (int)std::round(raw * (100 - prot) / 100.0f));
            Float(g, hr, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Bad);
            DamageHero(g, *h, dmg);
            if (h->dead) continue;
        }
        int st = a.stress + (crit ? 10 : 0);
        if (st) AddStress(g, *h, st);
        if (a.bleed) { h->st.bleedDmg = std::max(h->st.bleedDmg, a.bleed); h->st.bleedTurns = 3; }
        if (a.poison) { h->st.poisonDmg = std::max(h->st.poisonDmg, a.poison); h->st.poisonTurns = 3; }
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

// ---------------------------------------------------------------- drawing
static void DrawCave(Game& g) {
    float t = g.time;
    DrawRectangleGradientV(0, 0, SCREEN_W, SCREEN_H, Color{34, 104, 120, 255}, Color{12, 40, 56, 255});
    for (int i = 0; i < 7; i++) DrawCircle(80 + i * 200, 470, 170 + (i % 3) * 30.0f, Color{20, 62, 78, 255});
    for (int i = 0; i < 6; i++) {
        float x = 60 + i * 230.0f;
        DrawTriangle({x, 56}, {x + 50, 56}, {x + 25, 140 + (i % 3) * 30.0f}, Color{18, 54, 68, 255});
    }
    DrawRectangle(0, 460, SCREEN_W, 260, Color{46, 78, 82, 255});
    DrawRectangle(0, 460, SCREEN_W, 6, Color{70, 110, 110, 255});
    for (int k = 0; k < 14; k++) {
        float y = 460 - fmodf(t * (18 + k % 4 * 6) + k * 57, 400);
        DrawCircleLines(40 + k * 92, (int)y, 3 + k % 3, Color{200, 240, 250, 90});
    }
}

static void DrawHeroFigure(const Hero& h, Rectangle r, float t) {
    Color c = ClassColor(h.cls);
    float cx = r.x + r.width / 2, bob = sinf(t * 2 + h.id) * 2, top = r.y + bob;
    Color skin{236, 196, 160, 255};
    DrawRectangle((int)cx - 18, (int)(r.y + 128), 14, 32, Color{60, 50, 44, 255});
    DrawRectangle((int)cx + 4, (int)(r.y + 128), 14, 32, Color{60, 50, 44, 255});
    DrawRectangleRounded({cx - 26, top + 56, 52, 76}, 0.3f, 6, c);
    switch (h.cls) {
        case HeroClass::Nurse:
            DrawRectangleRounded({cx - 18, top + 64, 36, 62}, 0.3f, 6, Pal::Paper);
            DrawCircle((int)cx, (int)(top + 36), 20, skin);
            DrawRectangle((int)cx - 20, (int)(top + 10), 40, 14, Pal::Paper);
            DrawRectangle((int)cx - 2, (int)(top + 12), 4, 10, Pal::Bad);
            DrawRectangle((int)cx - 5, (int)(top + 15), 10, 4, Pal::Bad);
            break;
        case HeroClass::Diver:
            DrawCircle((int)cx, (int)(top + 34), 28, Pal::Brass);
            DrawCircle((int)cx + 6, (int)(top + 34), 15, Color{40, 90, 110, 255});
            DrawCircle((int)cx + 2, (int)(top + 29), 5, Color{180, 230, 240, 255});
            DrawLineEx({cx + 26, top + 90}, {cx + 60, top + 60}, 4, Color{160, 160, 170, 255});
            break;
        case HeroClass::Captain:
            DrawCircle((int)cx, (int)(top + 36), 20, skin);
            DrawRectangle((int)cx - 28, (int)(top + 14), 56, 8, Color{30, 30, 40, 255});
            DrawRectangle((int)cx - 16, (int)(top - 2), 32, 18, Color{30, 30, 40, 255});
            DrawRectangle((int)cx - 30, (int)(top + 56), 14, 6, Pal::Brass);
            DrawRectangle((int)cx + 16, (int)(top + 56), 14, 6, Pal::Brass);
            break;
        default:
            DrawCircle((int)cx, (int)(top + 36), 20, skin);
            DrawCircle((int)cx - 8, (int)(top + 24), 7, Pal::Brass);
            DrawCircle((int)cx + 8, (int)(top + 24), 7, Pal::Brass);
            DrawCircle((int)cx - 8, (int)(top + 24), 4, Color{140, 220, 230, 255});
            DrawCircle((int)cx + 8, (int)(top + 24), 4, Color{140, 220, 230, 255});
            DrawLineEx({cx + 24, top + 100}, {cx + 50, top + 70}, 6, Color{150, 150, 160, 255});
            break;
    }
    if (h.cls != HeroClass::Diver) DrawCircle((int)cx + 8, (int)(top + 38), 3, Pal::Ink);
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
            for (int k = 1; k < 4; k++) DrawLineEx({cx - 40 + k * 20.0f, cy - 19}, {cx - 40 + k * 20.0f, cy + 19}, 2, Color{140, 120, 160, 255});
            DrawLineEx({cx - 34, cy - 12}, {cx - 58, cy - 38}, 2, c);
            DrawCircle((int)(cx - 30), (int)(cy - 5), 4, Pal::Ink);
        } break;
        case EnemyType::CaveShrimp: {
            Color c{248, 146, 132, 255};
            float cy = by - 52 + bob;
            const float seg[5][3] = {{-24, -12, 17}, {-8, -16, 16}, {8, -12, 14}, {20, -2, 12}, {28, 10, 10}};
            DrawTriangle({cx + 30, cy + 16}, {cx + 44, cy + 34}, {cx + 20, cy + 34}, Color{230, 120, 110, 255});
            for (auto& s : seg) DrawCircle((int)(cx + s[0]), (int)(cy + s[1]), s[2], c);
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
            DrawTriangle({cx + 78, cy + 20}, {cx + 100, cy + 44}, {cx + 70, cy + 44}, dk);
            DrawEllipse((int)(cx + 8), (int)cy, 48, 32, c);
            DrawCircle((int)(cx - 34), (int)(cy - 10), 24, c);
            DrawLineEx({cx - 40, cy - 30}, {cx - 90, cy - 180}, 2, dk);
            DrawLineEx({cx - 30, cy - 32}, {cx - 60, cy - 190}, 2, dk);
            DrawCircle((int)(cx - 44), (int)(cy - 28), 5, Pal::Ink);
            DrawCircle((int)(cx - 30), (int)(cy - 30), 5, Pal::Ink);
            DrawLineEx({cx - 40, cy - 4}, {cx - 64, cy - 70}, 8, c);
            DrawCircle((int)(cx - 66), (int)(cy - 84), 22, c);
            DrawTriangle({cx - 90, cy - 104}, {cx - 64, cy - 88}, {cx - 76, cy - 112}, Color{30, 60, 70, 255});
            DrawLineEx({cx - 30, cy + 10}, {cx - 56, cy + 20}, 8, c);
            DrawCircle((int)(cx - 62), (int)(cy + 20), 16, c);
        } break;
    }
}

static std::string StatusTags(const Status& st) {
    std::string s;
    if (st.bleedTurns > 0) s += "BLEED ";
    if (st.poisonTurns > 0) s += "POISON ";
    if (st.stunned > 0) s += "STUN ";
    if (st.buffTurns > 0) s += "RALLY ";
    if (st.guardTurns > 0) s += "GUARD ";
    return s;
}

static void DrawUnits(Game& g, int actingHero, int actingEnemy) {
    auto& d = g.dungeon;
    float t = g.time;
    for (int p = 0; p < PARTY_SIZE; p++) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        DrawEllipse((int)(r.x + r.width / 2), (int)(r.y + r.height), 40, 8, Color{0, 0, 0, 60});
        DrawHeroFigure(*h, r, t);
        Stats s = GetStats(*h);
        DrawBar({r.x, r.y + r.height + 8, r.width, 8}, (float)h->hp / s.maxHp, Pal::Good);
        DrawBar({r.x, r.y + r.height + 18, r.width, 6}, h->stress / 100.0f, Pal::Stress);
        DrawTextCentered(h->name, r.x + r.width / 2, r.y + r.height + 28, 16, Pal::Paper);
        DrawTextCentered(TextFormat("%d/%d HP", h->hp, s.maxHp), r.x + r.width / 2, r.y + r.height + 46, 14, Color{220, 220, 200, 255});
        std::string tags = StatusTags(h->st);
        if (h->deathsDoor) tags += "DEATH'S DOOR ";
        if (h->rattled) tags += "RATTLED";
        DrawTextCentered(tags, r.x + r.width / 2, r.y - 22, 12, Pal::Coral);
        if (h->id == actingHero) {
            float y = r.y - 44 + sinf(t * 6) * 4;
            DrawTriangle({r.x + r.width / 2 - 12, y}, {r.x + r.width / 2, y + 14}, {r.x + r.width / 2 + 12, y}, Pal::Brass);
        }
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        Rectangle r = EnemyRect(g, p);
        DrawEllipse((int)(r.x + r.width / 2), (int)(r.y + r.height), e.boss ? 60 : 40, 8, Color{0, 0, 0, 60});
        DrawEnemyFigure(e, r, t);
        DrawBar({r.x, r.y + r.height + 8, r.width, 8}, (float)e.hp / e.maxHp, Pal::Bad);
        DrawTextCentered(e.name, r.x + r.width / 2, r.y + r.height + 22, 16, Pal::Paper);
        DrawTextCentered(TextFormat("%d/%d HP", e.hp, e.maxHp), r.x + r.width / 2, r.y + r.height + 40, 14, Color{220, 220, 200, 255});
        DrawTextCentered(StatusTags(e.st), r.x + r.width / 2, r.y - 22, 12, Pal::Coral);
        if (e.uid == actingEnemy) {
            float y = r.y - 44 + sinf(t * 6) * 4;
            DrawTriangle({r.x + r.width / 2 - 12, y}, {r.x + r.width / 2, y + 14}, {r.x + r.width / 2 + 12, y}, Pal::Bad);
        }
    }
}

static void DrawTopBar(Game& g) {
    auto& d = g.dungeon;
    DrawRectangle(0, 0, SCREEN_W, 56, Color{16, 30, 40, 230});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    Txt("THE CAVE  -  Shallows", 20, 16, 24, Pal::Brass);
    for (int i = 0; i < (int)d.rooms.size(); i++) {
        float x = 330 + i * 34.0f;
        Color c = i < d.roomIndex ? Pal::Good : i == d.roomIndex ? Pal::Brass : Color{90, 100, 104, 255};
        if (d.rooms[i] == RoomType::Boss) DrawPoly({x + 10, 28}, 4, 13, 45, c);
        else DrawCircle((int)x + 10, 28, 9, c);
    }
    Txt("Light", 500, 8, 16, Pal::Paper);
    DrawBar({500, 28, 200, 14}, d.light / 100.0f, Color{250, 220, 120, 255});
    Txt(LightName(d.light), 712, 24, 20, Color{250, 220, 120, 255});
    Txt(TextFormat("Loot: %d gold, %d relic%s", d.lootGold, (int)d.lootRelics.size(), d.lootRelics.size() == 1 ? "" : "s"), 880, 18, 20, Pal::Paper);
}

// Returns true if the button to leave was pressed.
static bool ResultPanel(const char* title, const std::string& body, const char* button, Color titleColor) {
    Rectangle p{340, 150, 600, 300};
    Panel(p);
    DrawTextCentered(title, p.x + p.width / 2, p.y + 24, 36, titleColor);
    DrawWrapped(body, {p.x + 40, p.y + 84, p.width - 80, 150}, 20, Pal::Ink);
    return Button({p.x + 150, p.y + p.height - 70, 300, 48}, button);
}

void SceneDungeon(Game& g) {
    auto& d = g.dungeon;
    float dt = GetFrameTime();

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
    DrawUnits(g, actingHero, actingEnemy);
    unsigned char dark = (unsigned char)std::clamp((100 - d.light) * 1.5f, 0.0f, 150.0f);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 8, 18, dark});
    for (auto& f : d.floats) {
        f.life -= dt;
        f.pos.y -= 32 * dt;
        unsigned char a = (unsigned char)(255 * std::clamp(f.life / 0.4f, 0.0f, 1.0f));
        DrawTextCentered(f.text, f.pos.x + 2, f.pos.y + 2, 22, Color{0, 0, 0, a});
        DrawTextCentered(f.text, f.pos.x, f.pos.y, 22, Color{f.color.r, f.color.g, f.color.b, a});
    }
    d.floats.erase(std::remove_if(d.floats.begin(), d.floats.end(), [](const FloatText& f) { return f.life <= 0; }), d.floats.end());
    DrawTopBar(g);

    if (!d.log.empty()) {
        DrawRectangleRounded({380, 66, 520, 20.0f * d.log.size() + 14}, 0.1f, 6, Color{10, 20, 28, 170});
        for (size_t i = 0; i < d.log.size(); i++)
            DrawTextCentered(d.log[i], 640, 73 + i * 20.0f, 17, i + 1 == d.log.size() ? Pal::Paper : Color{190, 200, 200, 255});
    }

    // ---------------- phase-specific UI
    switch (d.phase) {
        case DPhase::Corridor: {
            Rectangle p{400, 150, 480, 270};
            Panel(p);
            const char* head = d.roomIndex < 0 ? "At the cave mouth" : TextFormat("Room %d of %d cleared", d.roomIndex + 1, (int)d.rooms.size());
            DrawTextCentered(head, p.x + p.width / 2, p.y + 20, 28, Pal::Ink);
            bool nextIsBoss = d.rooms[d.roomIndex + 1] == RoomType::Boss;
            DrawTextCentered(nextIsBoss ? "Heavy clacking echoes from the next chamber..." : "The passage winds deeper.",
                             p.x + p.width / 2, p.y + 60, 18, nextIsBoss ? Pal::Bad : Pal::BrassDk);
            if (Button({p.x + 40, p.y + 96, 400, 46}, nextIsBoss ? "Face the Lobster  (-20 light)" : "Advance  (-20 light)")) {
                d.light = std::max(0.0f, d.light - 20);
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
            DrawRectangleRounded(bar, 0.08f, 6, Color{16, 30, 40, 230});
            DrawRectangleRoundedLinesEx(bar, 0.08f, 6, 3, Pal::BrassDk);
            Hero* h = actingHero >= 0 ? FindHero(g, actingHero) : nullptr;
            if (!h || d.pendingSkip || !d.turnStarted) {
                const char* who = actingEnemy >= 0 && FindEnemy(g, actingEnemy) ? FindEnemy(g, actingEnemy)->name.c_str() : "...";
                DrawTextCentered(TextFormat("%s is acting", who), SCREEN_W / 2.0f, 620, 24, Pal::Paper);
                break;
            }
            int pos = PartyPos(g, h->id);
            int heroId = h->id;
            Txt(TextFormat("%s's turn  (%s, rank %d)", h->name.c_str(), ClassName(h->cls), pos + 1), 40, 570, 20, Pal::Brass);
            Txt("Choose an ability, then click a highlighted target. Right-click to cancel.", 520, 572, 16, Color{190, 200, 200, 255});
            const auto& abs = ClassAbilities(h->cls);
            int hoverAb = -1;
            for (int i = 0; i < (int)abs.size(); i++) {
                Rectangle b{40 + i * 240.0f, 600, 228, 50};
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
                Txt(info, 40, 664, 17, Pal::Paper);
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
