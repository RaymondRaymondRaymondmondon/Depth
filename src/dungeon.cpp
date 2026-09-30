// ============================================================================
//  DEPTH - the roguelike expedition: rooms, the flashlight, and combat.
// ============================================================================
#include "game.h"
#include "rig.h"
#include "sound.h"
#include "combatfx.h"
#include "sprite_renderer.h"
#include "rlgl.h"
#include "relics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

static int Roll(int lo, int hi) { return GetRandomValue(lo, hi); }
static bool Chance(int pct) { return GetRandomValue(1, 100) <= pct; }

// ---------------------------------------------------------------- carried items
static InvItem RollFoundItem() {
    int r = Roll(1, 100);
    if (r <= 35) return {ItemKind::Battery};
    if (r <= 70) return {ItemKind::Bandage};
    return {ItemKind::Key};
}

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

static bool BossFightNow(const DungeonState& d);
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
// Heroes stand on the left with rank 1 closest to the middle; enemies mirror them on the right. A big enemy fills
// several ranks (span) but is still one enemy: it can be hit, and can act, from any rank it fills.
static Rectangle HeroRect(int pos) { float cx = 520 - pos * 125.0f; return {cx - 50, 250, 100, 210}; } // Darkest Dungeon scale: the fighters fill the stage
static int EnemySpan(const Enemy& e) { return std::max(1, e.span); }
static int SlotStart(Game& g, int idx) { int s = 0; for (int i = 0; i < idx && i < (int)g.dungeon.enemies.size(); i++) s += EnemySpan(g.dungeon.enemies[i]); return s; }
static int SlotsUsed(Game& g) { return SlotStart(g, (int)g.dungeon.enemies.size()); }
static bool CoversMask(Game& g, int idx, int mask) { // does the enemy at list index idx fill any rank in mask?
    if (idx < 0 || idx >= (int)g.dungeon.enemies.size()) return false;
    int st = SlotStart(g, idx);
    for (int k = 0; k < EnemySpan(g.dungeon.enemies[idx]); k++) if (mask & (1 << (st + k))) return true;
    return false;
}
static Rectangle EnemyRect(Game& g, int pos) {
    int slot = pos, span = 1;
    bool boss = false;
    if (pos >= 0 && pos < (int)g.dungeon.enemies.size()) { slot = SlotStart(g, pos); span = EnemySpan(g.dungeon.enemies[pos]); boss = g.dungeon.enemies[pos].boss; }
    float cx = 760 + (slot + (span - 1) * 0.5f) * 125.0f, w = 90 + (span - 1) * 125.0f + (span > 1 ? 34 : 0);
    if (span >= 3) return {cx - w / 2, 120, w, 340};
    if (span == 2) return {cx - w / 2, 185, w, 275};
    if (boss) return {cx - 64, 220, 128, 240};
    return {cx - 56, 290, 112, 170};  // Darkest Dungeon scale: the fighters fill the stage
}

static Vector2 BodyOf(Rectangle r) { return {r.x + r.width / 2, r.y + r.height * 0.42f}; }
static void Float(Game& g, Rectangle r, const std::string& t, Color c) {
    (void)g;
    Vector2 at = BodyOf(r);
    if (!t.empty() && t[0] == '+') cfx::Heal(at, atoi(t.c_str() + 1));
    else if (t == "Miss" || t == "Dodge") cfx::Miss(at, t.c_str());
    else cfx::Word(at, t.c_str(), c);
}
// a blow lands: everything that hangs off the figure whips (its chains, rags, hems)
static void KickFigure(int key, bool crit) {
    for (int k : {key, key + 2000000}) {
        rig::Instance& in = rig::Get(k);
        for (auto& ch : in.chains) ch.Kick({crit ? -420.0f : -240.0f, -140});
    }
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
        amount = (int)std::round(amount * StressMult(g) * (100 - GetStats(h).stressResist) / 100.0f * (100 - RelicBundle(h).stressGainPct) / 100.0f);
        if (amount <= 0) return;
    }
    int before = h.stress;
    h.stress = std::clamp(h.stress + amount, 0, 100);
    if (h.stress != before) { int pos = PartyPos(g, h.id); if (pos >= 0) cfx::Nerves(BodyOf(HeroRect(pos)), h.stress - before); }
    int diff = h.stress - before;
    int pos = PartyPos(g, h.id);
    if (diff != 0 && pos >= 0) Float(g, HeroRect(pos), TextFormat("%+d nerves", diff), Pal::Stress);
    if (diff > 0 && pos >= 0) {
        const UnitAnim* cur = FindAnim(g, true, h.id);
        if (!cur || cur->kind == Anim::Stress) StartAnim(g, true, h.id, Anim::Stress, 1.1f);
    }
    if (h.stress >= 100 && !h.rattled) {
        h.rattled = true;
        Log(g, h.name + " is RATTLED! (less accurate, may freeze up)");
    }
}

static void DamageHero(Game& g, Hero& h, int dmg) {
    if (dmg <= 0) return;
    if (h.st.madTurns > 0) dmg = (int)std::ceil(dmg * 1.15f); // Eldritch Madness: 15% more damage from all sources
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
    g.mourning = true;   // the salon mourns them when the party comes home
    for (auto& h : g.roster)
        if (h.dead) for (auto& id : g.party) if (id == h.id) id = -1;
    g.roster.erase(std::remove_if(g.roster.begin(), g.roster.end(), [](const Hero& h) { return h.dead; }), g.roster.end());
    CompactParty(g);
}

static void MoveEnemy(Game& g, int uid, int delta) {
    auto& en = g.dungeon.enemies;
    int i = EnemyPos(g, uid);
    if (i < 0) return;
    if (en[i].span > 1) { Log(g, en[i].name + " is far too big to move."); return; }
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
        for (int i = 0; i < (int)g.dungeon.enemies.size(); i++)
            if (CoversMask(g, i, a.hits)) v.push_back(i);
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
            for (int i = 0; i < (int)d.enemies.size(); i++)
                if (CoversMask(g, i, a.hits)) uids.push_back(d.enemies[i].uid);
        } else if (targetPos >= 0 && targetPos < (int)d.enemies.size()) {
            uids.push_back(d.enemies[targetPos].uid);
        }
        for (int uid : uids) {
            for (int k = 0; k < a.hitsCount; k++) {
                Enemy* e = FindEnemy(g, uid);
                if (!e || !e->alive) break;
                Rectangle er = EnemyRect(g, EnemyPos(g, uid));
                int edodge = e->dodge + (e->st.dodgeTurns > 0 ? e->st.dodgeBuff : 0);
                int regionAcc = (h->st.burnTurns > 0 ? 15 : 0) + (h->st.siltTurns > 0 ? 25 : 0); // Totemic Burn -15%, Silt Blindness -25%
                int hit = std::clamp(s.acc + a.accBonus + (h->st.accTurns > 0 ? h->st.accBuff : 0) + HeroAccBonus(g) - edodge - regionAcc, 5, 95);
                if (!Chance(hit)) { Float(g, er, "Miss", Pal::Paper); StartAnim(g, false, uid, Anim::Dodge, 0.45f); continue; }
                RelicFx rb = RelicBundle(*h);
                bool crit = Chance(std::max(0, 5 + rb.critPct - (h->st.siltTurns > 0 ? 10 : 0)));
                StartAnim(g, false, uid, Anim::Hurt, 0.5f);
                Sparkle(g, er, crit ? 16 : 8, crit ? Pal::Brass : Color{255, 210, 160, 255}, 220, 0);
                if (a.dmgMult > 0) {
                    float raw = Roll(s.dmgMin, s.dmgMax) * a.dmgMult * (1.0f + h->st.buffDmg / 100.0f);
                    if (crit) raw *= 1.5f;
                    if (e->st.marked > 0) raw *= 1.25f;
                    if (e->prot >= 10 && rb.vsArmored) raw *= 1.0f + rb.vsArmored / 100.0f;                          // pickaxes and picks vs shells and plate
                    if ((e->type == EnemyType::LostDiver || e->type == EnemyType::SunGod || e->type == EnemyType::ArmorLostOne) && rb.vsConstruct) raw *= 1.0f + rb.vsConstruct / 100.0f;
                    int eprot = std::max(0, e->prot + (e->st.protTurns > 0 ? e->st.protBuff : 0));
                    eprot = eprot * (100 - rb.armorPen) / 100;                                                        // armour-piercing tools
                    int dmg = std::max(1, (int)std::round(raw * (100 - eprot) / 100.0f));
                    e->hp -= dmg;
                    cfx::Hit(BodyOf(er), dmg, crit, false);
                    KickFigure(1000000 + uid, crit);
                    PlayCue(crit ? "mus.drum" : "ui.drop", crit ? 0.9f : 0.6f);
                    if (crit) {
                        d.shake = 0.35f;
                        Log(g, "Critical hit! The crew cheers.");
                        for (int p = 0; p < PARTY_SIZE; p++) if (Hero* o = PartyAt(g, p)) AddStress(g, *o, -4);
                    }
                    // relic effects that fire when a blow lands: stuns, bleeds, arcs, dynamite, occult costs
                    CombatState cs;
                    cs.game = &g; cs.hero = h; cs.target = e; cs.damage = dmg; cs.crit = crit;
                    RunCombatRelicEffects(cs);
                    if (cs.stunTarget && e->hp > 0) { e->st.stunned = 1; Float(g, er, "Stunned", Pal::Teal); }
                    if (cs.bleedTarget && e->hp > 0) { e->st.bleedDmg = std::max(e->st.bleedDmg, cs.bleedTarget); e->st.bleedTurns = 3; Float(g, er, "Bleed", Pal::Bad); }
                    if (cs.arcDamage > 0) { // a Tesla arc leaps to the next enemy in line
                        int ei = EnemyPos(g, uid);
                        if (ei >= 0 && ei + 1 < (int)d.enemies.size() && d.enemies[ei + 1].alive) {
                            Enemy& nx = d.enemies[ei + 1];
                            nx.hp -= cs.arcDamage;
                            Float(g, EnemyRect(g, ei + 1), "Arc " + std::to_string(cs.arcDamage), Pal::Teal);
                            Sparkle(g, EnemyRect(g, ei + 1), 10, Color{110, 200, 230, 255}, 200, 0);
                            if (nx.hp <= 0) { nx.hp = 0; nx.alive = false; Log(g, nx.name + " is defeated."); }
                        }
                    }
                    if (cs.splashFront > 0) { // dynamite: both front enemies
                        for (int fi = 0; fi < (int)d.enemies.size(); fi++) {
                            if (SlotStart(g, fi) >= 2) break;
                            Enemy& fe = d.enemies[fi];
                            if (!fe.alive) continue;
                            fe.hp -= cs.splashFront;
                            Float(g, EnemyRect(g, fi), "Boom " + std::to_string(cs.splashFront), Pal::Coral);
                            Sparkle(g, EnemyRect(g, fi), 16, Color{255, 170, 60, 255}, 260, 0);
                            if (fe.hp <= 0) { fe.hp = 0; fe.alive = false; Log(g, fe.name + " is defeated."); }
                        }
                        d.shake = 0.3f;
                    }
                    if (cs.recoilDamage > 0) { Float(g, HeroRect(std::max(0, PartyPos(g, h->id))), "Recoil " + std::to_string(cs.recoilDamage), Pal::Bad); DamageHero(g, *h, cs.recoilDamage); }
                    if (cs.selfStress > 0) AddStress(g, *h, cs.selfStress);
                    if (cs.stressRelief > 0) for (int p = 0; p < PARTY_SIZE; p++) if (Hero* o = PartyAt(g, p)) AddStress(g, *o, -cs.stressRelief);
                    if (e->hp <= 0) { e->hp = 0; e->alive = false; Log(g, e->name + " is defeated."); break; }
                }
                if (a.bleed) { e->st.bleedDmg = std::max(e->st.bleedDmg, a.bleed); e->st.bleedTurns = 3; Float(g, er, "Bleed", Pal::Bad); }
                if (a.poison) { ApplyPoison(e->st, a.poison); Float(g, er, TextFormat("Poison %d", e->st.poisonDmg), Pal::Good); }
                if (a.mark) { e->st.marked = 3; Float(g, er, "Marked", Pal::Brass); }
                if (a.stunChance && Chance(a.stunChance)) { e->st.stunned = 1; Float(g, er, "Stunned", Pal::Teal); }
                if (a.moveTarget) MoveEnemy(g, uid, a.moveTarget);
                // a curse or a song can weaken an enemy the same way a buff strengthens an ally -- same
                // fields, just applied to the other side with a negative value
                if (a.buffDmg) { e->st.buffDmg = a.buffDmg; e->st.buffTurns = 3; Float(g, er, "Weakened", Pal::Bad); }
                if (a.buffProt) { e->st.protBuff = a.buffProt; e->st.protTurns = 3; Float(g, er, "Exposed", Pal::Bad); }
                if (a.buffDodge) { e->st.dodgeBuff = a.buffDodge; e->st.dodgeTurns = 3; Float(g, er, "Off Balance", Pal::Bad); }
                if (a.buffAcc) { e->st.accBuff = a.buffAcc; e->st.accTurns = 3; Float(g, er, "Blinded", Pal::Bad); }
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
        for (int idA : {g.party[myPos], g.party[targetPos]}) // Drowning Entanglement: damage whenever forced to change ranks
            if (Hero* mv = FindHero(g, idA); mv && mv->st.drownTurns > 0) { DamageHero(g, *mv, 3); Float(g, HeroRect(PartyPos(g, idA)), "Drowning 3", Pal::Bad); }
        Log(g, "The Captain reorders the line.");
        targets = {myPos}; // the ally now stands where the Captain was
    }
    for (int p : targets) {
        Hero* t = PartyAt(g, p);
        if (!t) continue;
        Rectangle tr = HeroRect(p);
        if (a.heal || a.stressHeal || a.cure) Sparkle(g, tr, 14, a.heal ? Color{130, 240, 150, 255} : Color{200, 170, 255, 255}, 50, 60);
        else Sparkle(g, tr, 14, Color{255, 214, 120, 255}, 60, 50);
        if (a.heal) {
            RelicFx hb = RelicBundle(*h);
            int amt = HealHero(*t, a.heal + Roll(0, 2) + hb.healBonus);
            Float(g, tr, "+" + std::to_string(amt), Pal::Good);
            if (hb.healParty > 0 && a.target == Target::Ally) // a chemistry kit splashes the mixture around
                for (int q = 0; q < PartySize(g); q++) if (q != p) if (Hero* o = PartyAt(g, q)) { int x = HealHero(*o, hb.healParty); Float(g, HeroRect(q), "+" + std::to_string(x), Pal::Good); }
        }
        if (a.cure) { t->st.bleedTurns = 0; t->st.poisonTurns = 0; }
        if (a.stressHeal) AddStress(g, *t, -a.stressHeal);
        if (a.buffDmg) { t->st.buffDmg = a.buffDmg; t->st.buffTurns = 3; Float(g, tr, "Rallied", Pal::Brass); }
        if (a.buffDodge) { t->st.dodgeBuff = a.buffDodge; t->st.dodgeTurns = 3; Float(g, tr, "Dodge up", Pal::Teal); }
        if (a.buffProt) { t->st.protBuff = a.buffProt; t->st.protTurns = 3; Float(g, tr, "Armor up", Pal::Brass); }
        if (a.buffAcc) { t->st.accBuff = a.buffAcc; t->st.accTurns = 3; Float(g, tr, "Eagle Eye", Pal::Brass); }
        if (a.guardTurns) { t->st.guardTurns = a.guardTurns; Float(g, tr, "Guarding", Pal::Teal); }
    }
}

// Which ability an enemy will use this turn (-1 if it can't do anything). Rank rules: a skill can only be
// used from the ranks it allows (melee: the front two; long range: anywhere but the front), and a melee
// skill only reaches the heroes in the front two ranks. Supports (heals, buffs, commands) pick their own
// sense: no heal when everyone is well, no drag when there's no one at the back to drag.
static bool EnemyCanUse(Game& g, int uid, const EnemyAbility& a) {
    int rank = EnemyPos(g, uid), n = PartySize(g);
    if (rank < 0 || n == 0 || !CoversMask(g, rank, a.fromRanks)) return false;
    bool heals = a.healSelf || a.healAllies || a.healLowest;
    if (heals) {
        bool hurt = false;
        for (auto& o : g.dungeon.enemies) if (o.alive && o.hp < o.maxHp * 0.8f) hurt = true;
        if (!hurt && a.dmgMult <= 0 && !a.buffAllyAtk && !a.buffAllyDef) return false;
    }
    if (a.summon >= 0 && (int)g.dungeon.enemies.size() >= 2) return false; // she only calls for help once her court is dead
    if (a.drain) return g.dungeon.enemies.size() >= 2;
    if (a.pull == 1) return n >= 3;
    if (a.pull >= 2) return n >= 2;
    if (a.dmgMult <= 0 && a.hits == ANY_RANK && !a.aoe && !a.region && !a.weakAtk && !a.weakAcc && !a.weakDef && !a.weakSpd) return true; // pure self/ally effect
    for (int p = 0; p < n; p++) if (a.hits & (1 << p)) return true;
    return false;
}

static int EnemyPick(Game& g, int uid) {
    Enemy* e = FindEnemy(g, uid);
    if (!e) return -1;
    std::vector<int> usable;
    for (int i = 0; i < (int)e->abilities.size(); i++) if (EnemyCanUse(g, uid, e->abilities[i])) usable.push_back(i);
    return usable.empty() ? -1 : usable[Roll(0, (int)usable.size() - 1)];
}

static void ApplyRegion(Game& g, Hero& h, int region) {
    Rectangle hr = HeroRect(std::max(0, PartyPos(g, h.id)));
    switch (region) {
        case 1: h.st.burnTurns = 3; Float(g, hr, "Totemic Burn", Pal::Coral); break;
        case 2: h.st.siltTurns = 3; Float(g, hr, "Silt Blindness", Pal::Paper); break;
        case 3: h.st.drownTurns = 3; Float(g, hr, "Entangled", Pal::Teal); break;
        case 4: h.st.madTurns = 3; Float(g, hr, "Madness", Pal::Stress); break;
        default: break;
    }
}

// Entangled heroes take damage whenever they are forced to change rank.
static void AfterPartyMoved(Game& g, const std::array<int, PARTY_SIZE>& before) {
    for (int p = 0; p < PARTY_SIZE; p++) {
        int id = g.party[p];
        if (id < 0 || id == before[p]) continue;
        Hero* h = FindHero(g, id);
        if (h && h->st.drownTurns > 0) { DamageHero(g, *h, 3); Float(g, HeroRect(p), "Drowning 3", Pal::Bad); }
    }
}

static void EnemyAct(Game& g, int uid, int ability) {
    Enemy* e = FindEnemy(g, uid);
    int n = PartySize(g);
    if (!e || n == 0) return;
    if (ability < 0) ability = EnemyPick(g, uid);
    if (ability < 0) { Log(g, e->name + " can't reach anyone and skitters about."); return; }
    const EnemyAbility a = e->abilities[ability]; // a copy: summoning may move the enemy list under us
    Rectangle er = EnemyRect(g, std::max(0, EnemyPos(g, uid)));

    // ---- who it goes for
    std::vector<int> targets;
    bool hasHeroEffect = a.dmgMult > 0 || a.aoe || a.region || a.weakAtk || a.weakAcc || a.weakDef || a.weakSpd || a.stress || a.bleed || a.poison || a.stunChance || a.pull == 1;
    if (a.aoe) {
        for (int p = 0; p < n; p++) if (a.hits & (1 << p)) targets.push_back(p);
    } else if (a.pull == 1) {
        int pick = -1; // an alluring song or a vine reaches for someone at the back
        for (int p = n - 1; p >= 2; p--) if (a.hits & (1 << p)) { pick = p; break; }
        if (pick >= 0) targets.push_back(pick);
    } else if (hasHeroEffect) {
        int guard = -1;
        for (int p = 0; p < n; p++) if (Hero* h = PartyAt(g, p); h && h->st.guardTurns > 0) guard = p;
        std::vector<int> opts;
        for (int p = 0; p < n; p++) if (a.hits & (1 << p)) opts.push_back(p);
        if (guard >= 0 && a.dmgMult > 0 && a.targetsN == 0) targets.push_back(guard);
        else if (!opts.empty()) {
            int count = std::min(std::max(1, a.targetsN), (int)opts.size());
            for (int k = 0; k < count; k++) {
                int i = Roll(0, (int)opts.size() - 1);
                targets.push_back(opts[i]);
                opts.erase(opts.begin() + i);
            }
        }
    }
    Log(g, e->name + ": " + a.name);
    std::array<int, PARTY_SIZE> before = g.party;

    // ---- what lands on the heroes
    for (int p : targets) {
        Hero* h = PartyAt(g, p);
        if (!h || h->dead) continue;
        Stats s = GetStats(*h);
        Rectangle hr = HeroRect(p);
        int dodge = s.dodge + (h->st.dodgeTurns > 0 ? h->st.dodgeBuff : 0);
        int eacc = e->acc + (e->st.accTurns > 0 ? e->st.accBuff : 0);
        int hit = std::clamp(eacc + EnemyAccBonus(g) - dodge, 5, 95);
        if (a.dmgMult > 0 && !Chance(hit)) { Float(g, hr, "Dodge", Pal::Paper); StartAnim(g, true, h->id, Anim::Dodge, 0.45f); continue; }
        bool crit = a.dmgMult > 0 && Chance(6);
        if (crit) g.dungeon.shake = 0.35f;
        if (a.dmgMult > 0) {
            StartAnim(g, true, h->id, Anim::Hurt, 0.5f);
            Sparkle(g, hr, 8, Color{220, 60, 50, 255}, 180, 0);
            int prot = std::min(80, s.prot + (h->st.guardTurns > 0 ? 25 : 0) + (h->st.protTurns > 0 ? h->st.protBuff : 0));
            float raw = Roll(e->dmgMin, e->dmgMax) * a.dmgMult * (1.0f + (e->st.buffTurns > 0 ? e->st.buffDmg / 100.0f : 0.0f)) * (crit ? 1.5f : 1.0f);
            int dmg = std::max(1, (int)std::round(raw * (100 - prot) / 100.0f));
            cfx::Hit(BodyOf(hr), dmg, crit, true);
            KickFigure(h->id, crit);
            PlayCue(crit ? "mus.drum" : "ui.drop", crit ? 0.9f : 0.6f);
            DamageHero(g, *h, dmg);
            if (h->dead) continue;
        } else if (hasHeroEffect) {
            StartAnim(g, true, h->id, Anim::Stress, 0.7f);
        }
        int st = a.stress + (crit ? 10 : 0);
        if (st) AddStress(g, *h, st);
        if (a.bleed) { h->st.bleedDmg = std::max(h->st.bleedDmg, a.bleed); h->st.bleedTurns = 3; }
        if (a.poison) ApplyPoison(h->st, a.poison);
        if (a.stunChance && Chance(a.stunChance)) { h->st.stunned = 1; Float(g, hr, "Stunned", Pal::Teal); }
        if (a.weakAtk) { h->st.buffDmg = -a.weakAtk; h->st.buffTurns = 3; Float(g, hr, "Weakened", Pal::Bad); }
        if (a.weakAcc) { h->st.accBuff = -a.weakAcc; h->st.accTurns = 3; Float(g, hr, "Blinded", Pal::Bad); }
        if (a.weakDef) { h->st.protBuff = -a.weakDef; h->st.protTurns = 3; Float(g, hr, "Exposed", Pal::Bad); }
        if (a.weakSpd) { h->st.spdBuff = -a.weakSpd; h->st.spdTurns = 3; Float(g, hr, "Slowed", Pal::Teal); }
        if (a.region) ApplyRegion(g, *h, a.region);
    }

    // ---- commands: who stands where
    if (a.pull == 1 && !targets.empty()) {
        int k = targets[0];
        if (k >= 1) std::rotate(g.party.begin(), g.party.begin() + k, g.party.begin() + k + 1); // dragged to the front
        Log(g, "A hero is dragged to the front!");
    } else if (a.pull == 2) {
        std::vector<int> ids;
        for (int id : g.party) if (id >= 0) ids.push_back(id);
        for (int i = (int)ids.size() - 1; i > 0; i--) std::swap(ids[i], ids[Roll(0, i)]);
        for (int p = 0, k = 0; p < PARTY_SIZE; p++) if (g.party[p] >= 0) g.party[p] = ids[k++];
        Log(g, "The party is thrown into disarray!");
    } else if (a.pull == 3 && n >= 2) {
        int i = Roll(0, n - 1), j = Roll(0, n - 2);
        if (j >= i) j++;
        std::swap(g.party[i], g.party[j]);
        Log(g, "Two of the crew are hauled out of place!");
        if (Hero* h = PartyAt(g, i); h && a.region) ApplyRegion(g, *h, a.region);
        if (Hero* h = PartyAt(g, j); h && a.region) ApplyRegion(g, *h, a.region);
    }
    if (a.pull >= 1) AfterPartyMoved(g, before);

    // ---- what it does for its own side
    auto lowestAlly = [&]() -> Enemy* {
        Enemy* best = nullptr;
        for (auto& o : g.dungeon.enemies) if (o.alive && (!best || o.hp * best->maxHp < best->hp * o.maxHp)) best = &o;
        return best;
    };
    auto healEnemy = [&](Enemy& t, int amt) {
        t.hp = std::min(t.maxHp, t.hp + amt);
        Float(g, EnemyRect(g, std::max(0, EnemyPos(g, t.uid))), TextFormat("+%d", amt), Pal::Good);
    };
    auto buffEnemy = [&](Enemy& t, int atk, int def) {
        if (atk) { t.st.buffDmg = atk; t.st.buffTurns = 3; }
        if (def) { t.st.protBuff = def; t.st.protTurns = 3; }
    };
    if (a.healSelf) { healEnemy(*e, a.healSelf); e = FindEnemy(g, uid); }
    if (a.healLowest) if (Enemy* t = lowestAlly()) { healEnemy(*t, a.healLowest); buffEnemy(*t, a.buffAllyAtk, a.buffAllyDef); }
    if (a.healAllies) for (auto& o : g.dungeon.enemies) if (o.alive) { healEnemy(o, a.healAllies); buffEnemy(o, a.buffAllyAtk, a.buffAllyDef); }
    if (!a.healLowest && !a.healAllies && (a.buffAllyAtk || a.buffAllyDef)) for (auto& o : g.dungeon.enemies) if (o.alive) buffEnemy(o, a.buffAllyAtk, a.buffAllyDef);
    if ((e = FindEnemy(g, uid))) {
        if (a.cleanse) { Status keep; keep.protBuff = e->st.protBuff; keep.protTurns = e->st.protTurns; keep.buffDmg = std::max(0, e->st.buffDmg); keep.buffTurns = e->st.buffDmg > 0 ? e->st.buffTurns : 0; e->st = keep; Float(g, er, "Cleansed", Pal::Good); }
        if (a.buffSelfDef) { e->st.protBuff = a.buffSelfDef; e->st.protTurns = 3; Float(g, er, "Armor up", Pal::Brass); }
        if (a.buffSelfAtk) { e->st.buffDmg = a.buffSelfAtk; e->st.buffTurns = 3; Float(g, er, "Enraged", Pal::Bad); }
        if (a.drain) { // Siphon Offering: bleed a minion to feed itself (or the boss)
            Enemy* victim = nullptr;
            for (auto& o : g.dungeon.enemies) if (o.alive && o.uid != uid && !o.boss) victim = &o;
            if (!victim) for (auto& o : g.dungeon.enemies) if (o.alive && o.uid != uid) victim = &o;
            Enemy* feed = e;
            for (auto& o : g.dungeon.enemies) if (o.alive && o.boss && o.uid != (victim ? victim->uid : -1)) feed = &o;
            if (victim) {
                int take = std::min(5, victim->hp - 1);
                if (take > 0) { victim->hp -= take; Float(g, EnemyRect(g, std::max(0, EnemyPos(g, victim->uid))), TextFormat("-%d", take), Pal::Bad); healEnemy(*feed, take + 2); }
            }
        }
        if (a.selfMove) MoveEnemy(g, uid, a.selfMove == -99 ? -EnemyPos(g, uid) : a.selfMove);
        if (a.summon >= 0 && SlotsUsed(g) < 4) {
            Enemy add = MakeEnemy((EnemyType)a.summon, g.dungeon.nextUid++);
            ScaleEnemyForTier(add, g.dungeon.tier);
            g.dungeon.enemies.push_back(add);
            Log(g, add.name + " scuttles in from the dark.");
        }
    }
}
// ---------------------------------------------------------------- turn flow
static void BeginRound(Game& g) {
    auto& d = g.dungeon;
    d.order.clear();
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) {
            int spd = GetStats(*h).speed + (h->st.spdTurns > 0 ? h->st.spdBuff : 0);
            if (h->st.drownTurns > 0) spd /= 2; // Drowning Entanglement: -50% speed
            d.order.push_back({true, id, spd + Roll(0, 8)});
        }
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
    bool boss = BossFightNow(d);
    d.fightsWon++;
    if (!d.inHall && !d.chart.rooms.empty()) d.chart.rooms[d.curRoom].cleared = true;
    d.roomGold = (int)(Roll(boss ? 30 : d.inHall ? 5 : 10, boss ? 50 : d.inHall ? 12 : 22) * LootMult(g)); // kept modest: gold should stay scarce
    d.roomRelic = -1;
    if (boss && Chance(50)) { d.roomRelic = Roll(0, (int)Relics().size() - 1); d.lootRelics.push_back(d.roomRelic); }
    if (d.miniFight) d.roomGold = d.roomGold * 8 / 5; // a mini-boss guards better loot
    d.lootGold += d.roomGold;
    d.pendingItem = !boss && Chance(d.miniFight ? 85 : d.inHall ? 20 : 40); // something dropped among the wreckage, worth a look
    if (d.pendingItem) d.pendingItemVal = RollFoundItem();
    if (d.miniFight && Chance(35)) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; }
    d.phase = boss ? DPhase::Victory : DPhase::RoomClear;
}

static const int BONUS_TURN = -999; // the initiative marker of an extra boss action, so it can't chain into another
static void EndTurn(Game& g) {
    Cleanup(g);
    auto& d = g.dungeon;
    d.selectedAbility = -1;
    d.pendingSkip = false;
    d.turnStarted = false;
    if (PartySize(g) == 0) { d.phase = DPhase::Defeat; return; }
    if (d.enemies.empty()) { RoomCleared(g); return; }
    // a boss that just acted may get a second action in the same round (its own chance, once per turn)
    TurnEntry done = d.order[d.turnIdx];
    if (!done.hero && done.init != BONUS_TURN)
        if (Enemy* e = FindEnemy(g, done.id))
            if (e->alive && e->extraAct > 0 && Chance(e->extraAct)) {
                d.order.insert(d.order.begin() + d.turnIdx + 1, {false, done.id, BONUS_TURN});
                Float(g, EnemyRect(g, std::max(0, EnemyPos(g, done.id))), "Again!", Pal::Bad);
                Log(g, e->name + " strikes again!");
            }
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
        if (st.accTurns > 0 && --st.accTurns == 0) st.accBuff = 0;
        if (st.spdTurns > 0 && --st.spdTurns == 0) st.spdBuff = 0;
        if (st.guardTurns > 0) st.guardTurns--;
        if (st.burnTurns > 0) { st.burnTurns--; Float(g, r, "Burn 2", Pal::Coral); DamageHero(g, *h, 2); }
        if (st.siltTurns > 0) st.siltTurns--;
        if (st.drownTurns > 0) st.drownTurns--;
        if (!h->dead && st.madTurns > 0) {
            st.madTurns--;
            if (Chance(20)) { // Eldritch Madness: the mind slips
                std::vector<int> others;
                for (int p = 0; p < PartySize(g); p++) if (PartyAt(g, p) && PartyAt(g, p)->id != h->id) others.push_back(p);
                if (!others.empty() && Chance(50)) {
                    Hero* victim = PartyAt(g, others[Roll(0, (int)others.size() - 1)]);
                    Float(g, r, "Madness!", Pal::Stress);
                    DamageHero(g, *victim, Roll(3, 5));
                    skip(h->name + " lashes out at " + victim->name + " in a fit of madness.");
                } else skip(h->name + " stares into nothing, lost to madness.");
                return;
            }
        }
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
        if (st.buffTurns > 0 && --st.buffTurns == 0) st.buffDmg = 0;
        if (st.dodgeTurns > 0 && --st.dodgeTurns == 0) st.dodgeBuff = 0;
        if (st.protTurns > 0 && --st.protTurns == 0) st.protBuff = 0;
        if (st.accTurns > 0 && --st.accTurns == 0) st.accBuff = 0;
        if (e->hp <= 0) { e->hp = 0; e->alive = false; skip(e->name + " succumbs."); return; }
        if (st.stunned > 0) { st.stunned--; skip(e->name + " is stunned."); return; }
    }
}

// ---------------------------------------------------------------- rooms and the chart
static int gForceEnemy = -1; // the boss simulator sets this to fight one particular enemy
static RoomType CurRoomType(const DungeonState& d) { return d.chart.rooms.empty() ? RoomType::Fight : d.chart.rooms[d.curRoom].type; }
static bool BossFightNow(const DungeonState& d) { return !d.inHall && CurRoomType(d) == RoomType::Boss; }
static void BeginEvent(Game& g, EventKind k);
static bool ObjectiveMet(Game& g);
// a corridor drains what a room used to, spread over its stretches (half on a passage walked before)
static float StretchDrain(Game& g) {
    const ChartParams& P = ChartParamsFor(g.dungeon.tier);
    if (getenv("DEPTH_LINEAR")) return (float)LightDrainPerRoom(g);
    return LightDrainPerRoom(g) * CHART_STRETCH_DRAIN * 2.0f / (P.segMin + P.segMax) * (g.dungeon.walkRevisit ? 0.5f : 1.0f);
}


// a fight: in a room (a mini-boss more likely the deeper you go), in a hallway (a weaker group), or the boss
static void StartFight(Game& g, bool hall) {
    auto& d = g.dungeon;
    d.inHall = hall;
    d.enemies.clear();
    d.log.clear();
    d.floats.clear();
    int level = CAVE_TIER_LEVEL[d.tier];
    auto pickFrom = [&](const std::vector<EnemyType>& pool) { return pool[Roll(0, (int)pool.size() - 1)]; };
    auto standards = LocationStandards(d.loc), supports = LocationSupports(d.loc), minis = LocationMinis(d.loc);
    d.miniFight = false;
    if (gForceEnemy >= 0) { // the boss simulator: this enemy and the retainers it would normally have
        Enemy b = MakeEnemy((EnemyType)gForceEnemy, d.nextUid++);
        d.enemies.push_back(b);
        d.miniFight = b.tier == 1;
        int adds = b.span >= 3 ? 1 : Roll(1, 2);
        for (int i = 0; i < adds; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
    } else if (hall) { // caught in the passage: two or three of the ordinary kind
        int count = Roll(0, 99) < 70 ? 2 : 3;   // a passage is narrow: mostly a pair
        for (int i = 0; i < count; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
        Log(g, "Something moves in the passage!");
    } else if (CurRoomType(d) == RoomType::Boss) { // the location's level boss stands in front, with its own to back it up
        d.enemies.push_back(MakeEnemy(LocationLevelBoss(d.loc), d.nextUid++));
        d.enemies.push_back(MakeEnemy(Chance(50) ? pickFrom(supports) : pickFrom(standards), d.nextUid++)); // the boss fills three ranks: one retainer fits
        Log(g, std::string(LocationBossName(d.loc)) + " rises to meet you...");
    } else if (d.minisMet < MAX_MINIS_PER_RUN && Chance(MiniBossChance(level))) {
        d.miniFight = true;
        d.minisMet++;
        d.enemies.push_back(MakeEnemy(pickFrom(minis), d.nextUid++));
        int adds = Roll(1, 2);
        for (int i = 0; i < adds; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
        Log(g, d.enemies[0].name + " blocks the way!");
    } else {
        int count = Roll(3, 4);
        for (int i = 0; i < count; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
        if (Chance(50)) d.enemies.back() = MakeEnemy(pickFrom(supports), d.nextUid - 1); // a support hangs back at the rear
        Log(g, "Something stirs in the dark...");
    }
    for (auto& e : d.enemies) ScaleEnemyForTier(e, d.tier);
    if (d.blessFights > 0) { // a shrine's blessing
        d.blessFights--;
        for (int id : g.party) if (Hero* h = FindHero(g, id)) { h->st.buffDmg = 25; h->st.buffTurns = 4; }
        Log(g, "The shrine's blessing steadies the crew's hands.");
    }
    d.anims.clear();
    d.shots.clear();
    d.pending = PendingAction{};
    d.round = 0;
    BeginRound(g);
    d.phase = DPhase::Combat;
}

// what the crew know: rooms near where they stand show on the scope (further with a better Sonar Array)
static void Reveal(Game& g, int extra = 0) {
    auto& d = g.dungeon;
    auto& c = d.chart;
    int sonar = g.upgrades[UP_SONAR], steps = (sonar >= 2 ? 2 : 1) + extra;
    for (int id : g.party) if (Hero* h = FindHero(g, id))   // the Awakened Lantern lights the way further
        for (int rid : h->relics) if (rid >= 0 && rid < (int)Relics().size() && Relics()[rid].name == "Awakened Lantern") { steps++; break; }
    if (sonar >= 3) for (auto& r : c.rooms) r.known = true;   // the whole layout, without what's in it
    std::vector<int> dist(c.rooms.size(), -1), q{d.curRoom};
    dist[d.curRoom] = 0;
    for (size_t i = 0; i < q.size(); i++) {
        int r = q[i];
        c.rooms[r].known = c.rooms[r].scouted = true;
        if (dist[r] >= steps) continue;
        for (int o : c.Neighbours(r)) if (dist[o] < 0) { dist[o] = dist[r] + 1; q.push_back(o); }
    }
}

// the Helm's objective: has it been met?
static bool ObjectiveMet(Game& g) {
    auto& d = g.dungeon;
    auto& c = d.chart;
    int visited = 0, treasures = 0, opened = 0, fights = 0, won = 0;
    for (auto& r : c.rooms) {
        visited += r.visited;
        if (r.type == RoomType::Treasure) { treasures++; opened += r.cleared; }
        if (r.type == RoomType::Fight || r.type == RoomType::Boss) { fights++; won += r.cleared; }
    }
    switch (d.objective) {
        case Objective::Chart: return visited * 10 >= (int)c.rooms.size() * 9;
        case Objective::Salvage: return treasures > 0 && opened == treasures;
        case Objective::Cleanse: return fights > 0 && won == fights;
        default: return c.rooms[c.boss].cleared;
    }
}

static void OpenEvent(Game& g, EventKind k, const std::string& title, const std::string& body) {
    auto& d = g.dungeon;
    d.event = k; d.eventTitle = title; d.eventBody = body; d.eventStage = 0;
    d.phase = DPhase::Event;
}

// the party has reached the room at the end of the corridor
static void ArriveRoom(Game& g) {
    auto& d = g.dungeon;
    auto& c = d.chart;
    d.curRoom = d.walkDest;
    d.walkEdge = -1; d.walkDest = -1;
    ChartRoom& r = c.rooms[d.curRoom];
    r.visited = true;
    Reveal(g);
    d.roomIndex++;
    d.pendingItem = false;
    if (r.cleared) { d.phase = DPhase::Corridor; d.corridorT = 0; return; }
    switch (r.type) {
        case RoomType::Treasure:
            d.roomIsChest = Chance(35); // sometimes it's locked, and only a carried key opens it
            d.chestOpened = false;
            d.roomGold = d.roomIsChest ? 0 : (int)(Roll(18, 36) * LootMult(g));
            d.roomRelic = -1;
            if (!d.roomIsChest) {
                d.lootGold += d.roomGold;
                r.cleared = true;
                if (Chance(30)) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; }
            }
            d.phase = DPhase::Treasure;
            return;
        case RoomType::Curio: BeginEvent(g, EventKind::Curio); return;
        case RoomType::Rest: OpenEvent(g, EventKind::Rest, "A place to rest", "A dry ledge above the water, out of the current. The crew could make camp here: bind wounds, eat, sleep in turns. Something may come in the night."); return;
        case RoomType::Shrine: BeginEvent(g, EventKind::Shrine); return;
        case RoomType::Entrance: case RoomType::Empty: r.cleared = true; d.phase = DPhase::Corridor; d.corridorT = 0; return;
        default: StartFight(g, false); return;
    }
}

// what waits on one stretch of the corridor, if anything
static void ResolveSegment(Game& g) {
    auto& d = g.dungeon;
    ChartEdge& e = d.chart.edges[d.walkEdge];
    int si = d.walkForward ? d.walkSeg : (int)e.segs.size() - 1 - d.walkSeg;
    CorridorEvent ev = e.segs[si];
    if (d.walkRevisit && d.walkSeg == d.ambushSeg) { d.ambushSeg = -1; Log(g, "An ambush in a passage you thought was safe!"); StartFight(g, true); return; }
    e.segs[si] = ev == CorridorEvent::Blocked ? ev : CorridorEvent::None; // resolved (a blocked passage stays blocked until cleared)
    switch (ev) {
        case CorridorEvent::HallFight: StartFight(g, true); return;
        case CorridorEvent::Trap: BeginEvent(g, EventKind::Trap); return;
        case CorridorEvent::Loot: BeginEvent(g, EventKind::Loot); return;
        case CorridorEvent::Curio: BeginEvent(g, EventKind::Curio); return;
        case CorridorEvent::Blocked: OpenEvent(g, EventKind::Blocked, "A blocked passage", "Rockfall and wreckage choke the passage. A battery's charge through the old blasting cap would clear it; otherwise, turn back and go round."); return;
        default: break;
    }
    // nothing here: on to the next stretch, or into the room
    d.walkSeg++;
    if (d.walkSeg >= d.walkSegs) ArriveRoom(g);
    else { d.walkT = 0; d.light = std::max(0.0f, d.light - StretchDrain(g)); }
}

// after an event or a hallway fight in the corridor: carry on walking, or arrive
static void ResumeWalk(Game& g) {
    auto& d = g.dungeon;
    d.inHall = false;
    if (d.walkEdge < 0) { d.phase = DPhase::Corridor; d.corridorT = 0; return; }
    d.walkSeg++;
    if (d.walkSeg >= d.walkSegs) { ArriveRoom(g); return; }
    d.phase = DPhase::Walking;
    d.walkT = 0;
    d.light = std::max(0.0f, d.light - StretchDrain(g));
}

// set off down the corridor to a neighbouring room
static void BeginWalk(Game& g, int dest) {
    auto& d = g.dungeon;
    int e = d.chart.EdgeBetween(d.curRoom, dest);
    if (e < 0) return;
    ChartEdge& ed = d.chart.edges[e];
    d.walkEdge = e; d.walkDest = dest; d.walkSeg = 0; d.walkSegs = (int)ed.segs.size();
    d.walkForward = ed.a == d.curRoom;
    d.walkRevisit = ed.walked > 0;
    d.ambushSeg = d.walkRevisit && Chance(ChartRevisitAmbush()) ? Roll(0, d.walkSegs - 1) : -1; // a revisited corridor: no scripted fights, but maybe an ambush
    ed.walked++;
    d.light = std::max(0.0f, d.light - StretchDrain(g));
    d.phase = DPhase::Walking;
    d.walkT = 0;
}

// (kept for the older callers: the boss simulator and the debug shots) - into a fight at the next room on the way
static void EnterNextRoom(Game& g) {
    auto& d = g.dungeon;
    d.roomIndex++;
    StartFight(g, false);
}

void StartDungeon(Game& g, Location loc) {
    g.mourning = false;
    CompactParty(g);
    g.dungeon = DungeonState{};
    auto& d = g.dungeon;
    d.loc = loc;
    d.visSeed = (unsigned)GetRandomValue(1, 2000000000); // this run's look: which skyline, which atmosphere
    d.atmos = GetRandomValue(0, 2);
    int li = (int)loc;
    d.tier = std::clamp(g.tierSel[li], 0, std::min(CAVE_TIERS - 1, g.tierCleared[li] + 1));
    d.chart = GenerateChart(d.tier, (unsigned)GetRandomValue(1, 2000000000));
    cfx::Reset();
    if (getenv("DEPTH_LINEAR")) { // balance baseline: the old linear run (3 or 4 rooms, 70% fights, then the boss) as a straight chart
        Chart c;
        int lvl = CAVE_TIER_LEVEL[d.tier], n = lvl >= 3 ? 4 : 3;
        auto add = [&](RoomType t) { ChartRoom r; r.type = t; r.gx = (int)c.rooms.size(); r.gy = 0; c.rooms.push_back(r); };
        add(RoomType::Entrance);
        bool anyFight = false;
        for (int i = 0; i < n; i++) { bool f = Chance(70); anyFight |= f; add(f ? RoomType::Fight : RoomType::Treasure); }
        if (!anyFight) c.rooms[1].type = RoomType::Fight;
        add(RoomType::Boss);
        for (int i = 1; i < (int)c.rooms.size(); i++) c.edges.push_back({i - 1, i, {CorridorEvent::None}, 0, false});
        c.entrance = 0; c.boss = (int)c.rooms.size() - 1;
        c.rooms[0].known = c.rooms[0].scouted = c.rooms[0].visited = c.rooms[0].cleared = true;
        d.chart = c;
    }
    d.curRoom = d.chart.entrance;
    d.objective = g.objectiveSel;
    Reveal(g);
    d.rooms.assign(1, RoomType::Fight);
    for (int id : g.party)
        if (Hero* h = FindHero(g, id)) { h->st = Status{}; h->deathsDoor = false; h->hp = std::max(1, h->hp); }
    g.scene = Scene::Dungeon;
}
// ---------------------------------------------------------------- the chart's events
// Curios: about five per location and six found anywhere (Stage 7 adds the supplies that force a good outcome).
struct CurioDef { const char* name; const char* look; bool cursed; };
static const CurioDef CURIOS[] = {
    // the Cave
    {"A barnacled sea chest", "Crusted shut with barnacles, half sunk in the silt.", false},
    {"A drowned sailor's locker", "Stencilled with a ship's name nobody remembers. Something knocks inside.", false},
    {"A glowing anemone bed", "It pulses with a soft light, and the water around it is warm.", false},
    {"A diver's skeleton, still suited", "The helmet is cracked. One gloved hand still grips a satchel.", false},
    {"A crystal-studded shelf", "Crystals grow from the rock like teeth, humming faintly.", false},
    // the Island
    {"A tribal idol", "A squat idol with shell eyes. Offerings lie rotting at its feet.", false},
    {"A bone totem", "Skulls of fish and men, lashed together with sinew.", false},
    {"A smoking offering bowl", "The embers are still warm. The smoke smells sweet and wrong.", false},
    {"A cache of palm-leaf scrolls", "Bundled in waxed cloth, marked with the tide's glyph.", false},
    {"A shipwrecked sea-chest", "Washed up and wedged between two roots.", false},
    // the Weeds
    {"A kelp-wrapped cage", "Something small and bright is caught inside, pressed against the bars.", false},
    {"A giant clam", "Big enough to swallow a man. It is very slightly open.", false},
    {"A merfolk trinket hoard", "Buttons, coins, a compass, a doll's head: all very carefully arranged.", false},
    {"A sunken rowboat", "Upside down on the kelp, its oars still lashed in.", false},
    {"A pale egg cluster", "Soft, translucent, and something inside is moving.", false},
    // Atlantis
    {"A cult altar", "Black stone, a groove for blood, a sigil that hurts to look at.", true},
    {"A broken statue with a hollow chest", "A drowned king, and something glinting where his heart should be.", false},
    {"A glyph-carved tablet", "The glyphs rearrange themselves while you watch.", false},
    {"An amphora sealed with wax", "Still sealed after all these centuries.", false},
    {"A mirror of black bronze", "Your reflection is a moment late.", false},
    // anywhere
    {"A floating bottle with a note", "Corked tight. The paper inside is still dry.", false},
    {"A rusted diving bell", "Its porthole is fogged from the inside.", false},
    {"A school of glowing fish", "They circle you, curious, and don't flee.", false},
    {"An old anchor chain", "It runs down into the dark further than the lamp can follow.", false},
    {"A strongbox", "Iron-bound, with a keyhole shaped like a starfish.", false},
    {"A tangle of fishing net", "Floats, hooks, a lost lure, and the shape of something caught.", false},
};
constexpr int CURIO_COUNT = sizeof(CURIOS) / sizeof(CURIOS[0]);
static int PickCurio(const DungeonState& d) { return Chance(70) ? (int)d.loc * 5 + Roll(0, 4) : 20 + Roll(0, 5); }

static Hero* RandomPartyHero(Game& g) {
    std::vector<Hero*> hs;
    for (int id : g.party) if (Hero* h = FindHero(g, id)) hs.push_back(h);
    return hs.empty() ? nullptr : hs[Roll(0, (int)hs.size() - 1)];
}
static void Hurt(Game& g, Hero& h, int dmg) { h.hp = std::max(1, h.hp - dmg); (void)g; }
static void Nerve(Hero& h, int n) { h.stress = std::clamp(h.stress + n, 0, 100); if (h.stress >= 100) h.rattled = true; }

// opens an event, working out anything that happens the moment it springs
static void BeginEvent(Game& g, EventKind k) {
    auto& d = g.dungeon;
    d.eventAmbush = false;
    switch (k) {
    case EventKind::Trap: {
        bool spotter = false;
        for (int id : g.party) if (Hero* h = FindHero(g, id)) spotter |= h->cls == HeroClass::Diver || h->cls == HeroClass::Whaler;
        if (spotter && Chance(ChartTrapSpotChance())) {
            int gold = (int)(Roll(4, 9) * LootMult(g));
            d.lootGold += gold;
            OpenEvent(g, k, "A trap, disarmed", TextFormat("A trip-line in the silt, strung to a rusted spring-harpoon. Sharp eyes caught it; the crew take it apart and salvage %d gold of brass.", gold));
        } else {
            Hero* h = RandomPartyHero(g);
            if (!h) { OpenEvent(g, k, "A trap", "It springs on nothing."); break; }
            int kind = Roll(0, 2);
            if (kind == 0) { Hurt(g, *h, Roll(2, 4)); OpenEvent(g, k, "A trap!", h->name + " steps on a nest of rusted barbs and bleeds."); }
            else if (kind == 1) { Hurt(g, *h, Roll(2, 4)); h->st.poisonDmg = 2; h->st.poisonTurns = 2; OpenEvent(g, k, "A trap!", h->name + " is stung by a spined thing hidden in the weed. The poison lingers."); }
            else { Nerve(*h, 9); OpenEvent(g, k, "A trap!", "A dead-man's rattle of bones on a line: the whole passage clatters. " + h->name + "'s nerves are shot."); }
        }
        d.eventStage = 1;
    } break;
    case EventKind::Loot: {
        if (Chance(55)) { int gold = (int)(Roll(6, 14) * LootMult(g)); d.lootGold += gold; OpenEvent(g, k, "Loose loot", TextFormat("Something catches the lamp: %d gold in a split purse.", gold)); }
        else { d.pendingItem = true; d.pendingItemVal = RollFoundItem(); OpenEvent(g, k, "Loose loot", "Something catches the lamp, half-buried in the silt."); }
        d.eventStage = 1;
    } break;
    case EventKind::Curio: {
        d.eventArg = PickCurio(d);
        const CurioDef& c = CURIOS[d.eventArg];
        OpenEvent(g, k, c.name, c.look);
    } break;
    case EventKind::Shrine: {
        const char* NAMES[LOCATION_COUNT] = {"A drowned altar to the tide", "A basalt shrine to the Sun", "A kelp-grown shrine of the merfolk", "A shrine to the Sleeper"};
        OpenEvent(g, k, NAMES[(int)d.loc], "Old offerings lie on it. The crew could pray here, and hope something listens kindly.");
    } break;
    default: break;
    }
}

// the choice made on an event (0 the first option: inspect, camp, pray, clear the passage; 1 leave, press on, turn back)
static void ChooseEvent(Game& g, int choice) {
    auto& d = g.dungeon;
    if (d.eventStage == 1) { // the outcome has been read: carry on
        d.event = EventKind::None;
        bool inCorridor = d.walkEdge >= 0;
        if (!inCorridor && !d.chart.rooms.empty()) d.chart.rooms[d.curRoom].cleared = true;
        if (d.eventAmbush) { d.eventAmbush = false; StartFight(g, true); return; }
        if (inCorridor) ResumeWalk(g); else { d.phase = DPhase::Corridor; d.corridorT = 0; }
        return;
    }
    switch (d.event) {
    case EventKind::Curio: {
        if (choice != 0) { d.eventBody = "You leave it be."; break; }
        const CurioDef& c = CURIOS[d.eventArg];
        int roll = Roll(0, 99);
        Hero* h = RandomPartyHero(g);
        if (c.cursed) roll = 40 + roll * 60 / 100;   // no safe way to touch it
        if (roll < 28) { int gold = (int)(Roll(10, 24) * LootMult(g)); d.lootGold += gold; d.eventBody = TextFormat("Inside: %d gold.", gold); }
        else if (roll < 36) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; d.eventBody = "Inside, wrapped in oilcloth: a relic."; }
        else if (roll < 48) { d.blessFights++; d.eventBody = "A warmth spreads through the crew. They'll fight the better for it."; }
        else if (roll < 54) { for (auto& r : d.chart.rooms) r.known = true; Reveal(g, 1); d.eventBody = "Rolled up inside: a scrap of sea-chart, and the passages ahead are drawn on it."; }
        else if (roll < 60) { for (int id : g.party) if (Hero* x = FindHero(g, id)) { x->hp = std::min(GetStats(*x).maxHp, x->hp + 4); Nerve(*x, -8); } d.eventBody = "It soothes: wounds close a little, nerves settle."; }
        else if (roll < 78) { if (h) Nerve(*h, 14); for (int id : g.party) if (Hero* x = FindHero(g, id)) Nerve(*x, 4); d.eventBody = h ? h->name + " recoils from what's inside. Everyone is a little shaken." : "Nothing, and somehow that is worse."; }
        else if (roll < 90) { if (h) Hurt(g, *h, Roll(3, 5)); d.eventBody = h ? h->name + " is cut by something sharp inside." : "It bites."; }
        else { d.eventAmbush = true; d.eventBody = "It was bait. Something comes out of the dark!"; }
    } break;
    case EventKind::Rest: {
        if (choice != 0) { d.eventBody = "The crew press on without resting."; break; }
        for (int id : g.party) if (Hero* x = FindHero(g, id)) { x->hp = std::min(GetStats(*x).maxHp, x->hp + GetStats(*x).maxHp * 2 / 5); Nerve(*x, -20); }
        d.eventAmbush = Chance(ChartNightAmbush());
        d.eventBody = d.eventAmbush ? "They bind their wounds and sleep in turns... and in the night, something finds the camp!"
                                    : "They bind their wounds, eat, and sleep in turns. Nothing comes. Morning, of a kind.";
    } break;
    case EventKind::Shrine: {
        if (choice != 0) { d.eventBody = "You leave the shrine to its own."; break; }
        if (Chance(60)) { d.blessFights += 2; d.eventBody = "The water stills. The crew feel watched over: their next two fights will go better."; }
        else { for (int id : g.party) if (Hero* x = FindHero(g, id)) Nerve(*x, 12); d.eventBody = "Something answers, and it isn't kind. Every nerve jangles."; }
    } break;
    case EventKind::Blocked: {
        ChartEdge& e = d.chart.edges[d.walkEdge];
        int si = d.walkForward ? d.walkSeg : (int)e.segs.size() - 1 - d.walkSeg;
        if (choice == 0 && g.batteries > 0) {
            g.batteries--;
            e.segs[si] = CorridorEvent::None;
            d.event = EventKind::None;
            d.eventBody = "";
            ResumeWalk(g);
            return;
        }
        // turn back: the party returns to the room they came from
        d.walkEdge = -1; d.walkDest = -1;
        d.event = EventKind::None;
        d.phase = DPhase::Corridor; d.corridorT = 0;
        return;
    }
    default: break;
    }
    d.eventStage = 1;
}

// the auto-player's route: the fewest fights to the boss, with detours for loot and rest when it can afford them
static void SimChooseRoute(Game& g, bool randomPlayer) {
    auto& d = g.dungeon;
    auto& c = d.chart;
    std::vector<int> nb = c.Neighbours(d.curRoom);
    if (nb.empty()) { d.phase = DPhase::Retreat; return; }
    int dest = -1;
    if (!randomPlayer && d.light < 35 && g.batteries > 0) { g.batteries--; d.light = std::min(100.0f, d.light + 40); }
    if (randomPlayer) dest = nb[Roll(0, (int)nb.size() - 1)];
    else {
        float hp = 0; int n = 0;
        for (int id : g.party) if (Hero* h = FindHero(g, id)) { hp += (float)h->hp / GetStats(*h).maxHp; n++; }
        hp = n ? hp / n : 0;
        for (int o : nb) {
            const ChartRoom& r = c.rooms[o];
            if (r.cleared || !r.scouted) continue;
            if (r.type == RoomType::Rest && hp < 0.6f) { dest = o; break; }
            if ((r.type == RoomType::Treasure || r.type == RoomType::Curio) && d.light > 40 && hp > 0.5f) { dest = o; break; }
        }
        if (dest < 0 && hp < 0.55f) // hurt: make for a rest room if one is close
            for (int r = 0; r < (int)c.rooms.size() && dest < 0; r++)
                if (c.rooms[r].type == RoomType::Rest && !c.rooms[r].cleared && c.rooms[r].known) {
                    std::vector<int> p = ChartPath(c, d.curRoom, r, g.batteries == 0);
                    if (p.size() > 1 && p.size() <= 3) dest = p[1];
                }
        if (dest < 0) {
            std::vector<int> path = ChartPath(c, d.curRoom, c.boss, g.batteries == 0);
            dest = path.size() > 1 ? path[1] : nb[0];
        }
    }
    BeginWalk(g, dest);
}
static void SimResolveEvent(Game& g) {
    auto& d = g.dungeon;
    if (d.event == EventKind::Blocked) { ChooseEvent(g, g.batteries > 0 ? 0 : 1); return; }
    ChooseEvent(g, 0);
}
void DebugSetEnemies(Game& g, Location loc, const std::vector<EnemyType>& types) {
    DebugEnterCombat(g, loc);
    auto& d = g.dungeon;
    d.enemies.clear();
    for (EnemyType ty : types) { Enemy e = MakeEnemy(ty, d.nextUid++); ScaleEnemyForTier(e, d.tier); d.enemies.push_back(e); }
}

void DebugEnterCombat(Game& g, Location loc) {
    StartDungeon(g, loc);
    auto& d = g.dungeon;
    std::vector<int> route = ChartPath(d.chart, d.chart.entrance, d.chart.boss);
    d.curRoom = route.size() > 1 ? route[1] : d.chart.entrance;
    d.chart.rooms[d.curRoom].type = RoomType::Fight;
    d.chart.rooms[d.curRoom].visited = true;
    Reveal(g);
    d.light = 60;
    StartFight(g, false);
}

static void ApplyResults(Game& g) {
    auto& d = g.dungeon;
    if (d.resultsApplied) return;
    d.resultsApplied = true;
    bool win = d.phase == DPhase::Victory;
    if (win && !d.chart.rooms.empty()) d.objectiveDone = d.objectiveDone || ObjectiveMet(g); // a retreat forfeits the objective's bonus
    float xpMult = 1;
    if (win && d.objectiveDone) switch (d.objective) {
        case Objective::Slay: xpMult = 1.25f; break;
        case Objective::Cleanse: xpMult = 1.4f; break;
        case Objective::Chart: d.lootGold = d.lootGold * 3 / 2; d.lootRelics.push_back(Roll(0, (int)Relics().size() - 1)); break;
        case Objective::Salvage: d.lootRelics.push_back(Roll(0, (int)Relics().size() - 1)); break;
        default: break;
    }
    if (d.phase != DPhase::Defeat) {
        g.gold += d.lootGold;
        for (int r : d.lootRelics) g.relicStorage.push_back(r);
        for (auto& it : d.inventory) if (it.kind == ItemKind::Relic) g.relicStorage.push_back(it.relicId); // carried home safely
        if (win) { d.rewardRelic = Roll(0, (int)Relics().size() - 1); g.relicStorage.push_back(d.rewardRelic); }
        if (win && d.tier > g.tierCleared[(int)d.loc]) {
            g.tierCleared[(int)d.loc] = d.tier;
            if (d.tier + 1 < CAVE_TIERS) g.tierSel[(int)d.loc] = d.tier + 1;
        }
        int lvl = CAVE_TIER_LEVEL[d.tier];
        for (int id : g.party) {
            Hero* h = FindHero(g, id);
            if (!h) continue;
            int before = h->level;
            GiveXP(g, *h, (int)((win ? 3 + lvl * 3 : 1 + lvl) * xpMult + 0.5f));   // deeper tiers pay off far faster than grinding the Shallows
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
static void SimCombatStep(Game& g, bool randomPlayer) { // one unit's turn, played by the simulator
    auto& d = g.dungeon;
    do {
        if (d.turnIdx >= (int)d.order.size()) BeginRound(g);
        TurnEntry te = d.order[d.turnIdx];
        bool valid = te.hero ? (FindHero(g, te.id) && PartyPos(g, te.id) >= 0) : FindEnemy(g, te.id) != nullptr;
        if (!valid) { d.turnIdx++; d.turnStarted = false; break; }
        if (!d.turnStarted) StartTurn(g);
        if (d.pendingSkip) { EndTurn(g); break; }
        if (!te.hero) { EnemyAct(g, te.id, -1); EndTurn(g); break; }
        Hero* h = FindHero(g, te.id);
        int pos = PartyPos(g, te.id);
        const auto& abs = ClassAbilities(h->cls);
        std::vector<int> usable;
        for (int ab : h->loadout)
            if (ab >= 0 && HeroCanUse(g, pos, abs[ab])) usable.push_back(ab);
        if (usable.empty()) { EndTurn(g); break; }
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
    } while (false);
}

// Run with:  depth.exe --sim 400 [level] [random]
// The default player heals anyone below 40% HP and otherwise uses its hardest-hitting attack on the
// weakest enemy it can reach; "random" picks any usable ability and target instead.
void SimulateExpeditions(int runs, int level, bool randomPlayer, int tier) {
    int wins = 0, losses = 0, deaths = 0, anyDeath = 0, rattled = 0, wipeRoom[8] = {0}, retreats = 0;
    std::unordered_map<std::string, int> killers; // what was standing when the crew went down
    for (int r = 0; r < runs; r++) {
        Game g;
        InitGame(g);
        g.tierSel[(int)Location::Cave] = tier;
        g.tierCleared[(int)Location::Cave] = CAVE_TIERS;
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
        StartDungeon(g, Location::Cave);
        auto& d = g.dungeon;
        int steps = 0;
        while (steps++ < 40000) {
            if (d.phase == DPhase::Corridor) { SimChooseRoute(g, randomPlayer); continue; }
            if (d.phase == DPhase::Walking) { ResolveSegment(g); continue; }
            if (d.phase == DPhase::Event) { SimResolveEvent(g); continue; }
            if (d.phase == DPhase::Treasure || d.phase == DPhase::RoomClear) { d.pendingItem = false; if (d.walkEdge >= 0) ResumeWalk(g); else { d.phase = DPhase::Corridor; } continue; }
            if (d.phase != DPhase::Combat) break;
            SimCombatStep(g, randomPlayer);
        }
        int lost = 4 - (int)g.roster.size();
        deaths += lost;
        anyDeath += lost > 0;
        for (auto& h : g.roster) if (h.rattled) { rattled++; break; }
        if (d.phase == DPhase::Victory) wins++; else { losses++; wipeRoom[std::clamp(d.roomIndex, 0, 7)]++;
            if (d.phase == DPhase::Retreat) retreats++; for (auto& e : d.enemies) if (e.alive) killers[e.name]++; }
    }
    printf("Simulated %d expeditions, crew level %d, cave level %d (%s player):\n", runs, level, CAVE_TIER_LEVEL[tier],
           randomPlayer ? "random" : "sensible");
    printf("  wins %.1f%%   wipes %.1f%%   (of which retreats %.1f%%)\n", 100.0 * wins / runs, 100.0 * (losses - retreats) / runs, 100.0 * retreats / runs);
    printf("  runs with a death %.1f%%   avg deaths %.2f   runs with someone rattled %.1f%%\n",
           100.0 * anyDeath / runs, (double)deaths / runs, 100.0 * rattled / runs);
    printf("  wipes by room:");
    for (int i = 0; i < 6; i++) printf(" %d:%d", i + 1, wipeRoom[i]);
    printf("\n  standing at the end:");
    for (auto& kv : killers) printf(" %s x%d;", kv.first.c_str(), kv.second);
    printf("\n");
}

// Fights one particular boss or mini-boss (with the retainers it would normally bring) again and again, against a
// fresh crew of the given level, and prints how often the crew wins and how long the fight lasts.
// Run with:  depth.exe --boss <runs> <crewLevel> <tier 0-4> <enemy type index> [random]
void SimulateBossFight(int runs, int level, int tier, int enemyType, bool randomPlayer) {
    int wins = 0, deaths = 0, rounds = 0;
    Location loc = Location::Cave;
    if (enemyType >= (int)EnemyType::TribalSpearman && enemyType <= (int)EnemyType::SunGod) loc = Location::Island;
    else if (enemyType >= (int)EnemyType::FeralMerman && enemyType <= (int)EnemyType::Neptune) loc = Location::Weeds;
    else if (enemyType >= (int)EnemyType::LostInfantry) loc = Location::Atlantis;
    for (int r = 0; r < runs; r++) {
        Game g;
        InitGame(g);
        g.tierSel[(int)loc] = tier;
        g.tierCleared[(int)loc] = CAVE_TIERS;
        for (auto& h : g.roster) {
            h.level = level;
            h.hp = GetStats(h).maxHp;
            std::vector<int> pool;
            const auto& abs = ClassAbilities(h.cls);
            for (int i = 0; i < (int)abs.size(); i++) if (abs[i].unlockLevel <= level) pool.push_back(i);
            for (int i = (int)pool.size() - 1; i > 0; i--) std::swap(pool[i], pool[Roll(0, i)]);
            for (int k = 0; k < LOADOUT_SIZE; k++) h.loadout[k] = k < (int)pool.size() ? pool[k] : -1;
        }
        StartDungeon(g, loc);
        auto& d = g.dungeon;
        d.rooms = {RoomType::Fight};
        d.roomIndex = -1;
        gForceEnemy = enemyType;
        int steps = 0;
        while (steps++ < 20000) {
            if (d.phase == DPhase::Corridor) { EnterNextRoom(g); continue; }
            if (d.phase != DPhase::Combat) break;
            SimCombatStep(g, randomPlayer);
        }
        gForceEnemy = -1;
        deaths += 4 - (int)g.roster.size();
        rounds += d.round;
        wins += d.phase == DPhase::RoomClear || d.phase == DPhase::Victory;
    }
    Enemy e = MakeEnemy((EnemyType)enemyType, 0);
    printf("%-22s (span %d, extra action %2d%%) vs crew level %d, cave level %d: wins %.1f%%   avg deaths %.2f   avg rounds %.1f\n",
           e.name.c_str(), e.span, e.extraAct, level, CAVE_TIER_LEVEL[tier], 100.0 * wins / runs, (double)deaths / runs, (double)rounds / runs);
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

// ---------------------------------------------------------------- the regions' own scenery
// Each location has its own mid and near layers instead of a re-tinted cave: the Island a drowned jungle of
// basalt, dead trees and bone totems; the Weeds a kelp forest, thick or thin by the run's seed, with leviathan
// ribs and chained anchors; Atlantis broken marble colonnades, hanging void crystals and a ruined altar. All
// flat, muted ink-dark masses with one hard-edged lit face; the vignette and ink pass finish them.
static void DrawRegionMidground(Game& g) {
    auto& d = g.dungeon;
    float t = g.time, sd = (float)(d.visSeed % 9973) * 1.37f;
    if (d.loc == Location::Cave) { // rock pillars, hanging root curtains and glowing moss: a cavern to walk through
        Repeat(LayerOffset(g, 0.42f), 420, [&](float sx, float wx) {
            if (Hash1(wx * 0.4f + sd) > 0.8f) return;
            float x = sx + Hash1(wx + sd) * 160, w = 44 + Hash1(wx * 1.6f) * 40;
            Color rock{14, 22, 28, 255}, lit{34, 52, 60, 255};
            for (int seg = 0; seg < 9; seg++) { // a pillar of stacked, uneven blocks from the ceiling to the floor
                float y0 = 60 + seg * 46.0f, jut = (Hash1(wx + seg * 3.1f) - 0.5f) * 14;
                DrawRectangle((int)(x + jut), (int)y0, (int)w, 48, rock);
                DrawRectangle((int)(x + jut), (int)y0, 5, 48, lit);
            }
            DrawTri({x - 20, 472}, {x + w + 20, 472}, {x + w / 2, 440}, rock);                                                  // rubble at its foot
            if (Hash1(wx * 2.2f + sd) > 0.4f) { Vector2 mo{x + w * 0.5f, 330 + Hash1(wx) * 90}; DrawEllipse((int)mo.x, (int)mo.y, 16, 7, Color{40, 150, 130, 255}); Glow(mo, 60, Color{60, 220, 190, 60}); } // glowing moss
        });
        Repeat(LayerOffset(g, 0.62f), 350, [&](float sx, float wx) { // root curtains
            float x = sx + Hash1(wx + sd) * 200;
            for (int k = 0; k < 5; k++) DrawLineEx({x + k * 9.0f, 56}, {x + k * 9.0f + sinf(t * 0.5f + wx + k) * 7, 130 + Hash1(wx + k) * 130}, 3, Color{16, 22, 20, 255});
        });
        return;
    }
    float dense = 0.35f + Hash1(sd + 3.0f) * 0.65f;
    if (d.loc == Location::Island) {
        Repeat(LayerOffset(g, 0.3f), 300, [&](float sx, float wx) { // jagged basalt columns
            if (Hash1(wx * 0.3f + sd) > 0.85f) return;
            float x = sx + Hash1(wx + sd) * 120, h = 130 + Hash1(wx * 1.7f + sd) * 220, w = 26 + Hash1(wx + 4) * 20;
            for (int k = 0; k < 3; k++) {
                float cx = x + k * w * 0.9f, ch = h * (1 - k * 0.22f);
                DrawRectangle((int)cx, (int)(470 - ch), (int)w, (int)ch, Color{22, 28, 32, 255});
                DrawTri({cx, 470 - ch}, {cx + w, 470 - ch}, {cx + w * 0.4f, 470 - ch - 18}, Color{28, 36, 40, 255});
                DrawRectangle((int)cx, (int)(470 - ch), 4, (int)ch, Color{40, 52, 56, 255});     // the lit face, one hard stripe
            }
        });
        Repeat(LayerOffset(g, 0.5f), 260, [&](float sx, float wx) { // drowned jungle: bare trunks, drooping fronds, vines
            if (Hash1(wx * 0.9f + sd) > 0.75f) return;
            float x = sx + Hash1(wx + sd * 2) * 100, h = 200 + Hash1(wx) * 130;
            DrawRectangle((int)x, (int)(470 - h), 12, (int)h, Color{20, 22, 18, 255});
            for (int k = 0; k < 4; k++) {
                float a = -2.6f + k * 0.6f, len = 60 + (k & 1) * 26, sw = sinf(t * 0.8f + wx + k) * 6;
                DrawTri({x + 6, 470 - h + 8}, {x + 6 + cosf(a) * len + sw, 470 - h + sinf(a) * len * 0.4f + 26}, {x + 6 + cosf(a) * len * 0.5f, 470 - h + 4}, Color{24, 38, 28, 255});
            }
            for (int k = 0; k < 3; k++) DrawLineEx({x + k * 8.0f, 60}, {x + k * 8.0f + sinf(t + wx + k) * 6, 140 + Hash1(wx + k) * 90}, 3, Color{20, 36, 26, 255});
        });
        Repeat(LayerOffset(g, 0.66f), 820, [&](float sx, float wx) { // bone totems
            float x = sx + Hash1(wx + sd) * 300;
            DrawRectangle((int)x, 330, 14, 140, Color{40, 30, 22, 255});
            for (int k = 0; k < 4; k++) {
                DrawCircleV({x + 7, 340 + k * 30.0f}, 11, Color{190, 182, 158, 255});
                DrawRectangle((int)x + 1, (int)(336 + k * 30), 5, 7, Color{6, 6, 8, 255}); DrawRectangle((int)x + 8, (int)(336 + k * 30), 5, 7, Color{6, 6, 8, 255});
            }
            DrawTri({x - 14, 340}, {x + 28, 340}, {x + 7, 300}, Color{110, 60, 44, 255});
        });
    } else if (d.loc == Location::Weeds) {
        for (int layer = 0; layer < 3; layer++) { // kelp: sparse and open, or a choking maze
            float par = 0.3f + layer * 0.16f, gap = 210 - dense * 120 - layer * 20;
            Repeat(LayerOffset(g, par), gap, [&](float sx, float wx) {
                float x = sx + Hash1(wx + sd + layer) * gap * 0.8f, h = 260 + Hash1(wx * 1.3f + layer) * 200;
                Vector2 prev{x, 480};
                for (int sgm = 1; sgm <= 12; sgm++) {
                    float u = sgm / 12.0f;
                    Vector2 q{x + sinf(t * 0.7f + wx + sgm * 0.5f) * sgm * (3 + layer), 480 - u * h};
                    DrawLineEx(prev, q, 12 - layer * 3 - sgm * 0.6f, layer == 0 ? Color{14, 26, 22, 255} : layer == 1 ? Color{18, 36, 28, 255} : Color{24, 46, 32, 255});
                    if (sgm % 3 == 0) DrawCircleV({q.x + 6, q.y}, 3, Color{60, 90, 52, 255});
                    prev = q;
                }
            });
        }
        Repeat(LayerOffset(g, 0.36f), 1200, [&](float sx, float wx) { // a leviathan's ribcage, half sunk
            if (Hash1(wx + sd) > 0.6f) return;
            float x = sx + Hash1(wx * 2 + sd) * 300;
            for (int k = 0; k < 7; k++) DrawRing({x + k * 34.0f, 500}, 120 - fabsf(k - 3.0f) * 14, 130 - fabsf(k - 3.0f) * 14, 200, 340, 18, Color{150, 146, 128, 255});
            DrawRectangle((int)x - 10, 496, 260, 12, Color{120, 116, 100, 255});
        });
        Repeat(LayerOffset(g, 0.6f), 640, [&](float sx, float wx) { // entangled anchor chains, hanging from the dark
            float x = sx + Hash1(wx + sd) * 260;
            for (int k = 0; k < 16; k++) DrawRing({x + sinf(t * 0.4f + wx + k * 0.3f) * 5, 50 + k * 13.0f}, 3, 5.5f, 0, 360, 8, Color{50, 50, 46, 255});
            DrawRing({x, 260}, 22, 28, 30, 330, 12, Color{50, 50, 46, 255});
            DrawCircleV({x, 300}, 10, Color{120, 220, 110, 255}); DrawCircleV({x, 300}, 7, Color{50, 110, 50, 255}); // a glowing spore pod
        });
    } else { // Atlantis
        for (int layer = 0; layer < 2; layer++)
            Repeat(LayerOffset(g, 0.3f + layer * 0.22f), layer ? 250 : 340, [&](float sx, float wx) { // broken marble colonnades
                if (Hash1(wx + sd * 1.5f + layer) > 0.8f) return;
                float x = sx + Hash1(wx + sd) * 100, h = 150 + Hash1(wx * 1.9f + layer) * 200 * (layer ? 0.7f : 1);
                Color c = layer ? Color{34, 34, 42, 255} : Color{26, 26, 34, 255};
                DrawRectangle((int)x, (int)(470 - h), 30, (int)h, c);
                DrawRectangle((int)x - 6, (int)(470 - h - 12), 42, 12, c);
                DrawRectangle((int)x - 8, 458, 46, 12, c);
                DrawRectangle((int)x + 4, (int)(470 - h), 3, (int)h, Color{74, 74, 86, 255});                                 // a fluted, lit edge
                if (Hash1(wx * 3 + sd) > 0.5f) DrawTri({x - 6, 470 - h - 12}, {x + 36, 470 - h - 12}, {x + 30, 470 - h - 30}, Color{8, 8, 12, 255}); // a jagged break
            });
        Repeat(LayerOffset(g, 0.5f), 520, [&](float sx, float wx) { // void crystals hanging from the ceiling
            float x = sx + Hash1(wx + sd) * 220, len = 60 + Hash1(wx * 1.1f) * 110;
            Color v = d.atmos == 0 ? Color{190, 60, 210, 255} : d.atmos == 2 ? Color{210, 60, 60, 255} : Color{170, 170, 190, 255};
            DrawTri({x - 12, 56}, {x + 12, 56}, {x, 56 + len}, Color{16, 12, 26, 255});
            DrawTri({x, 56}, {x + 12, 56}, {x, 56 + len}, Fade(v, 0.55f));
        });
        Repeat(LayerOffset(g, 0.68f), 900, [&](float sx, float wx) { // a fractured altar with a corrupted brazier
            float x = sx + Hash1(wx + sd) * 300;
            DrawRectangle((int)x, 420, 90, 50, Color{40, 40, 50, 255});
            DrawRectangle((int)x - 8, 412, 106, 10, Color{58, 58, 70, 255});
            DrawTri({x + 60, 412}, {x + 98, 412}, {x + 80, 396}, Color{8, 8, 12, 255});
            DrawCircleV({x + 20, 400}, 8, Color{30, 26, 34, 255});
            Glow({x + 20, 392}, 34, Color{200, 80, 230, 90});
        });
    }
}

// The ground the party walks on, for every region: a lit worn path over darker ground that falls away to black, with
// its own material (cave rock slabs, a rotted boardwalk, black mud and roots, cracked marble with a glowing rune line),
// wet sheen where the party's light lands, ragged edges, and a hard ink lip along the horizon.
static void DrawRegionFloor(Game& g) {
    auto& d = g.dungeon;
    float t = g.time, off = LayerOffset(g, 1.0f), sd = (float)(d.visSeed % 4099) * 0.37f;
    const Color ink{6, 7, 10, 255};
    Color base = d.loc == Location::Cave ? Color{34, 46, 54, 255} : d.loc == Location::Island ? Color{104, 88, 62, 255} : d.loc == Location::Weeds ? Color{36, 50, 40, 255} : Color{62, 62, 76, 255};
    DrawVGradient({0, 452, (float)SCREEN_W, 268}, Fade(Tone(base, 0.05f), 0.95f), Fade(Tone(base, -0.7f), 0.98f));      // ground falling away to black
    DrawRectangle(0, 448, SCREEN_W, 6, ink);                                                                                // the horizon lip
    DrawRectangle(0, 454, SCREEN_W, 2, Fade(Tone(base, 0.4f), 0.8f));
    DrawRectangle(0, 456, SCREEN_W, 60, Fade(Tone(base, 0.16f), 0.6f));                                                     // the worn path
    Repeat(off, 44, [&](float sx, float wx) {                                                                              // its ragged lower edge
        float h = 3 + Hash1(wx + sd) * 9;
        DrawTri({sx, 516}, {sx + 44, 516}, {sx + 22 + (Hash1(wx * 2 + sd) - 0.5f) * 16, 516 + h}, Fade(Tone(base, 0.16f), 0.6f));
    });
    for (int k = 1; k < 6; k++) DrawRectangle(0, 452 + k * k * 9, SCREEN_W, 1 + k / 3, Fade(ink, 0.35f));                    // receding bands
    if (d.loc == Location::Cave) { // uneven rock slabs with cracks and silt
        Repeat(off, 130, [&](float sx, float wx) {
            float w = 90 + Hash1(wx + sd) * 60, y = 462 + Hash1(wx * 1.3f + sd) * 46, x = sx + Hash1(wx * 2 + sd) * 40;
            DrawEllipse((int)(x + w / 2), (int)y, w / 2 + 2, 11, ink); DrawEllipse((int)(x + w / 2), (int)y - 1, w / 2 - 1, 9, Tone(base, 0.14f));
            DrawEllipse((int)(x + w / 2 - w * 0.12f), (int)y - 4, w * 0.32f, 3, Tone(base, 0.34f));                          // the lit top
            DrawLineEx({x + w * 0.2f, y - 3}, {x + w * 0.35f, y + 6}, 1.6f, ink); DrawLineEx({x + w * 0.35f, y + 6}, {x + w * 0.3f, y + 10}, 1.6f, ink);
        });
        Repeat(off, 210, [&](float sx, float wx) { for (int k = 0; k < 4; k++) DrawRectangle((int)(sx + k * 17 + Hash1(wx) * 40), 522 + k * 14 + (int)(Hash1(wx + k) * 6), 20 + k * 6, 2, Fade(Tone(base, 0.25f), 0.45f)); });   // strata of silt
    } else if (d.loc == Location::Atlantis) {
        for (float x = fmodf(off, 130) - 130; x < SCREEN_W + 130; x += 130) DrawLineEx({x, 454}, {x - 90, 720}, 3, ink);       // marble flag seams
        Repeat(off, 260, [&](float sx, float wx) { // cracks, chips, and an inlaid rune line that pulses
            float y = 470 + Hash1(wx + sd) * 40;
            DrawLineEx({sx + 20, y}, {sx + 44, y + 7}, 2, ink); DrawLineEx({sx + 44, y + 7}, {sx + 38, y + 16}, 2, ink);
        });
        Color rune = d.atmos == 2 ? Color{230, 70, 60, 255} : d.atmos == 1 ? Color{220, 190, 110, 255} : Color{200, 90, 240, 255};
        Repeat(off, 90, [&](float sx, float wx) { float pulse = 0.5f + 0.5f * sinf(t * 1.6f + wx * 0.05f); DrawRectangle((int)sx, 490, 46, 3, Fade(rune, 0.25f + 0.45f * pulse)); DrawRectangle((int)sx + 54, 490, 8, 3, Fade(rune, 0.2f + 0.3f * pulse)); });
    } else if (d.loc == Location::Island) { // a rotted boardwalk over the sand
        Repeat(off, 72, [&](float sx, float wx) {
            if (Hash1(wx * 0.7f + sd) < 0.1f) { DrawRectangle((int)sx, 462, 70, 52, Color{14, 12, 10, 255}); return; }              // a missing plank
            Color pc = Tone(Color{116, 82, 50, 255}, (Hash1(wx + sd) - 0.5f) * 0.4f);
            DrawRectangle((int)sx, 460, 70, 56, ink); DrawRectangle((int)sx + 2, 462, 66, 52, pc);
            DrawRectangle((int)sx + 2, 462, 66, 4, Tone(pc, 0.3f));                                                            // its lit edge
            for (int k = 0; k < 3; k++) DrawLineEx({sx + 8, 472 + k * 14.0f}, {sx + 62, 470 + k * 14.0f + Hash1(wx + k) * 4}, 1, Tone(pc, -0.3f)); // grain
            DrawCircleV({sx + 8, 468}, 2, Color{40, 36, 34, 255}); DrawCircleV({sx + 62, 468}, 2, Color{40, 36, 34, 255});          // nails
        });
        Repeat(off, 60, [&](float sx, float wx) { DrawEllipse((int)sx, 540 + (int)(Hash1(wx + sd) * 90), 18 + Hash1(wx) * 14, 3, Fade(Tone(base, -0.4f), 0.7f)); }); // sand ripples
    } else { // Weeds: black mud, roots and standing water
        Repeat(off, 150, [&](float sx, float wx) {
            float y = 470 + Hash1(wx + sd) * 40, x = sx + Hash1(wx * 2 + sd) * 60;
            Vector2 prev{x, y};
            for (int k = 1; k <= 6; k++) { Vector2 q{x + k * 12.0f, y + sinf(k * 0.9f + wx) * 5}; DrawLineEx(prev, q, 5.0f - k * 0.4f, ink); DrawLineEx(prev, q, 3.0f - k * 0.3f, Color{54, 44, 34, 255}); prev = q; } // a root
        });
        Repeat(off, 240, [&](float sx, float wx) { // puddles, catching the light
            float y = 500 + Hash1(wx + sd) * 40, w = 40 + Hash1(wx * 3) * 40;
            DrawEllipse((int)sx + 40, (int)y, w, 8, ink); DrawEllipse((int)sx + 40, (int)y, w - 2, 6, Color{40, 70, 74, 255}); DrawEllipse((int)sx + 30, (int)y - 1, w * 0.4f, 2, Color{120, 180, 180, 255});
        });
    }
    // sheen where the party's lantern and the props' lights fall on the wet ground
    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < 4; k++) DrawEllipse(430 + k * 20, 476 + k * 12, 300 - k * 55, 20 - k * 3, Fade(Tone(base, 0.4f), 0.05f));
    EndBlendMode();
}// ---------------------------------------------------------------- the prop spawner
// Modular decor along the path, chosen per step from a weighted pool for the location and the run's seed:
// each 230-pixel step of the walk may hold one prop (or none), so the path is assembled differently every run.
// Flat shapes, black ink under every mass, one hard lit edge.
enum Prop { P_SKULL, P_IMPALED, P_CAGE, P_WRECK, P_TOTEM, P_FUNGUS, P_HELMET, P_SHELLBONES, P_CORAL, P_ANCHOR, P_POD, P_CRATE, P_ALTAR, P_VOIDCRYSTAL, P_BRAZIER, P_LOSTONE, P_STALAGMITE, P_TORCH, P_STAKES, P_KELPCLUMP, P_BARREL, P_COLUMN, P_RIBS, P_COUNT };
struct PropWeight { Prop p; int w; };

static void DrawProp(Prop p, float x, float y, float t, int seed) {
    const Color ink{6, 7, 10, 255};
    switch (p) {
        case P_SKULL:
            DrawEllipse((int)x, (int)y - 8, 12, 10, ink); DrawEllipse((int)x, (int)y - 8, 10, 8, Color{176, 168, 146, 255});
            DrawRectangle((int)x - 6, (int)y - 10, 4, 5, ink); DrawRectangle((int)x + 2, (int)y - 10, 4, 5, ink);
            break;
        case P_IMPALED:
            DrawLineEx({x, y}, {x + 2, y - 70}, 5, Color{60, 44, 30, 255});
            DrawEllipse((int)x + 2, (int)y - 74, 11, 9, ink); DrawEllipse((int)x + 2, (int)y - 74, 9, 7, Color{184, 176, 152, 255});
            DrawRectangle((int)x - 3, (int)y - 77, 3, 4, ink); DrawRectangle((int)x + 3, (int)y - 77, 3, 4, ink);
            break;
        case P_CAGE:
            DrawRectangle((int)x - 22, (int)y - 46, 44, 46, ink);
            for (int k = -2; k <= 2; k++) DrawRectangle((int)x + k * 9 - 1, (int)y - 44, 3, 42, Color{110, 70, 46, 255});
            DrawRectangle((int)x - 22, (int)y - 46, 44, 4, Color{130, 84, 54, 255}); DrawRectangle((int)x - 22, (int)y - 6, 44, 6, Color{90, 58, 38, 255});
            break;
        case P_WRECK:
            DrawTri({x - 50, y}, {x + 46, y}, {x + 30, y - 34}, ink); DrawTri({x - 44, y - 2}, {x + 40, y - 2}, {x + 26, y - 28}, Color{70, 48, 32, 255});
            DrawLineEx({x - 10, y - 20}, {x - 4, y - 70}, 4, Color{60, 42, 28, 255});
            break;
        case P_TOTEM:
            DrawRectangle((int)x - 8, (int)y - 80, 16, 80, ink);
            for (int k = 0; k < 3; k++) { DrawRectangle((int)x - 6, (int)y - 76 + k * 26, 12, 22, Color{90, 66, 44, 255}); DrawRectangle((int)x - 4, (int)y - 68 + k * 26, 3, 4, ink); DrawRectangle((int)x + 1, (int)y - 68 + k * 26, 3, 4, ink); }
            break;
        case P_FUNGUS:
            for (int k = 0; k < 4; k++) { float h = 14 + (seed + k * 7) % 22; DrawEllipse((int)x + k * 9 - 14, (int)(y - h), 9, 7, ink); DrawLineEx({x + k * 9 - 14.0f, y}, {x + k * 9 - 14.0f, y - h}, 3, ink); Glow({x + k * 9 - 14.0f, y - h}, 24, Color{90, 230, 220, 70}); DrawEllipse((int)x + k * 9 - 14, (int)(y - h), 7, 5, Color{60, 190, 190, 255}); }
            break;
        case P_HELMET:
            DrawCircleSector({x, y}, 18, 180, 360, 14, ink); DrawCircleSector({x, y}, 15, 180, 360, 14, Color{120, 92, 52, 255});
            DrawRing({x + 5, y - 8}, 3, 6, 0, 360, 10, ink); DrawLineEx({x + 1, y - 12}, {x + 9, y - 4}, 1.5f, ink);
            break;
        case P_SHELLBONES:
            DrawCircleSector({x, y}, 28, 180, 360, 16, ink); DrawCircleSector({x, y}, 25, 180, 360, 16, Color{150, 136, 118, 255});
            for (int k = 0; k < 4; k++) DrawLineEx({x - 12 + k * 8.0f, y - 6}, {x - 14 + k * 9.0f, y - 22}, 2.5f, Color{206, 200, 180, 255});
            break;
        case P_CORAL:
            for (int k = 0; k < 4; k++) { float h = 20 + (seed + k * 11) % 34; DrawTri({x + k * 9 - 20.0f, y}, {x + k * 9 - 12.0f, y}, {x + k * 9 - 16.0f + (k & 1 ? 4 : -4), y - h}, ink); DrawTri({x + k * 9 - 18.0f, y}, {x + k * 9 - 13.0f, y}, {x + k * 9 - 15.0f, y - h + 4}, Color{172, 80, 70, 255}); }
            break;
        case P_ANCHOR:
            DrawLineEx({x, y}, {x, y - 60}, 7, ink); DrawLineEx({x, y}, {x, y - 58}, 4, Color{70, 70, 66, 255});
            DrawRing({x, y - 66}, 5, 9, 0, 360, 10, Color{70, 70, 66, 255}); DrawRing({x, y - 10}, 24, 30, 20, 160, 14, Color{104, 60, 38, 255});
            DrawLineEx({x - 16, y - 46}, {x + 16, y - 46}, 5, Color{70, 70, 66, 255});
            break;
        case P_POD:
            DrawCircleV({x, y - 12}, 14, ink); DrawCircleV({x, y - 12}, 11, Color{50, 110, 56, 255}); Glow({x, y - 12}, 28, Color{150, 255, 110, (unsigned char)(80 + 50 * sinf(t * 2 + seed))});
            DrawCircleV({x - 3, y - 15}, 4, Color{190, 255, 150, 255});
            break;
        case P_CRATE:
            DrawRectangle((int)x - 22, (int)y - 34, 44, 34, ink); DrawRectangle((int)x - 20, (int)y - 32, 40, 30, Color{86, 62, 40, 255});
            DrawLineEx({x - 20, y - 32}, {x + 20, y - 2}, 3, Color{60, 42, 28, 255}); DrawRectangle((int)x - 20, (int)y - 32, 40, 4, Color{130, 98, 64, 255});
            break;
        case P_ALTAR:
            DrawRectangle((int)x - 34, (int)y - 30, 68, 30, ink); DrawRectangle((int)x - 31, (int)y - 27, 62, 27, Color{88, 88, 100, 255});
            DrawTri({x + 12, y - 30}, {x + 36, y - 30}, {x + 26, y - 42}, ink); DrawLineEx({x - 20, y - 27}, {x - 8, y - 4}, 2, ink);
            break;
        case P_VOIDCRYSTAL:
            for (int k = 0; k < 3; k++) { float h = 28 + (seed + k * 13) % 40; DrawTri({x + k * 12 - 14.0f, y}, {x + k * 12 - 4.0f, y}, {x + k * 12 - 9.0f, y - h}, ink); DrawTri({x + k * 12 - 9.0f, y}, {x + k * 12 - 4.0f, y}, {x + k * 12 - 9.0f, y - h}, Color{170, 70, 220, 255}); }
            Glow({x, y - 20}, 40, Color{190, 70, 230, 60});
            break;
        case P_BRAZIER:
            DrawRectangle((int)x - 3, (int)y - 30, 6, 30, ink); DrawTri({x - 16, y - 30}, {x + 16, y - 30}, {x, y - 18}, ink);
            DrawEllipse((int)x, (int)(y - 40 + sinf(t * 9 + seed) * 2), 9, 14, Color{150, 60, 220, 255}); Glow({x, y - 40}, 50, Color{170, 70, 230, 90});
            break;
        case P_LOSTONE: // a petrified, coral-crusted figure, kneeling in worship
            DrawEllipse((int)x, (int)y - 22, 14, 22, ink); DrawEllipse((int)x, (int)y - 22, 11, 19, Color{96, 100, 96, 255});
            DrawCircleV({x + 2, y - 46}, 9, ink); DrawCircleV({x + 2, y - 46}, 7, Color{104, 108, 102, 255}); DrawRectangle((int)x - 5, (int)y - 48, 14, 4, Color{6, 6, 8, 255});
            DrawTri({x - 12, y - 30}, {x - 6, y - 30}, {x - 9, y - 50}, Color{178, 84, 72, 255});
            break;
        case P_STALAGMITE:
            for (int k = 0; k < 3; k++) {
                float h = 30 + (seed + k * 17) % 46, bx = x + k * 15 - 15;
                DrawTri({bx - 10, y}, {bx + 10, y}, {bx + (k - 1) * 3, y - h}, ink);
                DrawTri({bx - 7, y}, {bx + 3, y}, {bx + (k - 1) * 3, y - h + 5}, Color{84, 96, 108, 255});
                DrawTri({bx + 1, y}, {bx + 7, y}, {bx + (k - 1) * 3, y - h + 5}, Color{46, 56, 66, 255});
            }
            break;
        case P_TORCH: { // a driftwood torch: it really lights what is around it (see DrawCaveLighting)
            DrawLineEx({x, y}, {x + 1, y - 64}, 6, ink); DrawLineEx({x, y}, {x + 1, y - 64}, 3.5f, Color{96, 66, 40, 255});
            DrawTri({x - 10, y - 62}, {x + 10, y - 62}, {x, y - 48}, ink);
            float fl = sinf(t * 11 + seed) * 2;
            DrawEllipse((int)x, (int)(y - 76), 9, 15 + fl, Color{200, 70, 20, 255});
            DrawEllipse((int)x, (int)(y - 74), 6, 11 + fl, Color{255, 160, 50, 255});
            DrawEllipse((int)x, (int)(y - 72), 3, 6, Color{255, 236, 170, 255});
            Glow({x, y - 74}, 70, Color{255, 150, 60, 90});
        } break;
        case P_STAKES: // sharpened tribal stakes lashed with rope
            for (int k = 0; k < 3; k++) {
                float bx = x + k * 18 - 18, h = 54 + (seed + k * 9) % 28, lean = (k - 1) * 6;
                DrawLineEx({bx, y}, {bx + lean, y - h}, 7, ink); DrawLineEx({bx, y}, {bx + lean, y - h}, 4, Color{112, 78, 46, 255});
                DrawTri({bx + lean - 4, y - h + 2}, {bx + lean + 4, y - h + 2}, {bx + lean, y - h - 14}, ink);
            }
            DrawLineEx({x - 18, y - 30}, {x + 18, y - 26}, 3, Color{170, 140, 90, 255});
            break;
        case P_KELPCLUMP:
            for (int k = 0; k < 5; k++) {
                float bx = x + k * 8 - 16, h = 60 + (seed + k * 13) % 60;
                Vector2 prev{bx, y};
                for (int s2 = 1; s2 <= 6; s2++) {
                    Vector2 q{bx + sinf(t * 0.9f + k + s2 * 0.6f) * s2 * 2.5f, y - h * s2 / 6};
                    DrawLineEx(prev, q, 8.0f - s2 * 0.9f, ink); DrawLineEx(prev, q, 5.0f - s2 * 0.6f, Color{44, 112, 66, 255});
                    prev = q;
                }
            }
            break;
        case P_BARREL:
            DrawEllipse((int)x, (int)y - 16, 20, 22, ink); DrawEllipse((int)x, (int)y - 16, 17, 19, Color{96, 66, 40, 255});
            DrawRectangle((int)x - 17, (int)y - 26, 34, 3, Color{60, 58, 62, 255}); DrawRectangle((int)x - 17, (int)y - 8, 34, 3, Color{60, 58, 62, 255});
            DrawEllipse((int)x - 6, (int)y - 22, 5, 8, Color{140, 100, 62, 255});
            break;
        case P_COLUMN:
            DrawRectangle((int)x - 15, (int)y - 74, 30, 74, ink); DrawRectangle((int)x - 12, (int)y - 72, 24, 72, Color{132, 132, 146, 255});
            for (int k = 0; k < 4; k++) DrawLineEx({x - 9 + k * 6.0f, y - 70}, {x - 9 + k * 6.0f, y - 2}, 1.5f, Color{88, 88, 102, 255});
            DrawTri({x - 15, y - 74}, {x + 15, y - 74}, {x + 4, y - 88}, ink); DrawTri({x - 12, y - 72}, {x + 12, y - 72}, {x + 3, y - 84}, Color{132, 132, 146, 255});
            DrawEllipse((int)x + 32, (int)y - 8, 22, 8, ink); DrawEllipse((int)x + 32, (int)y - 8, 19, 6, Color{112, 112, 126, 255}); // a fallen drum
            break;
        case P_RIBS: // the ribcage of something huge
            for (int k = 0; k < 5; k++) {
                float bx = x + k * 11 - 22, r = k == 2 ? 27.0f : 24.0f;
                DrawRing({bx, y}, r - 5, r, 180, 360, 14, ink); DrawRing({bx, y}, r - 4, r - 1, 185, 355, 14, Color{196, 188, 164, 255});
            }
            DrawLineEx({x - 22, y}, {x + 22, y}, 5, ink); DrawLineEx({x - 22, y}, {x + 22, y}, 3, Color{176, 168, 146, 255});
            break;        default: break;
    }
}

// Every prop on the path this frame, from the run's seed: a dense, region-specific midground the party walks past.
struct PlacedProp { Prop p; float x, y; int seed; };
static std::vector<PlacedProp> CollectProps(Game& g) {
    auto& d = g.dungeon;
    static const std::vector<PropWeight> ISLAND = {{P_SKULL, 2}, {P_IMPALED, 3}, {P_CAGE, 2}, {P_WRECK, 3}, {P_TOTEM, 2}, {P_TORCH, 4}, {P_STAKES, 3}, {P_RIBS, 2}};
    static const std::vector<PropWeight> CAVE = {{P_FUNGUS, 4}, {P_HELMET, 2}, {P_SHELLBONES, 2}, {P_CORAL, 3}, {P_SKULL, 1}, {P_STALAGMITE, 4}, {P_RIBS, 1}};
    static const std::vector<PropWeight> WEEDS = {{P_ANCHOR, 2}, {P_POD, 4}, {P_CRATE, 2}, {P_SHELLBONES, 2}, {P_SKULL, 1}, {P_KELPCLUMP, 5}, {P_BARREL, 3}, {P_FUNGUS, 2}};
    static const std::vector<PropWeight> ATLANTIS = {{P_ALTAR, 2}, {P_VOIDCRYSTAL, 3}, {P_BRAZIER, 2}, {P_LOSTONE, 3}, {P_COLUMN, 4}};
    const auto& pool = d.loc == Location::Island ? ISLAND : d.loc == Location::Weeds ? WEEDS : d.loc == Location::Atlantis ? ATLANTIS : CAVE;
    int total = 0;
    for (auto& w : pool) total += w.w;
    float sd = (float)(d.visSeed % 7919) * 0.91f;
    std::vector<PlacedProp> out;
    Repeat(LayerOffset(g, 1.0f), 150, [&](float sx, float wx) {
        if (Hash1(wx * 0.77f + sd) < 0.2f) return;                         // an odd bare step
        float pick = Hash1(wx * 1.31f + sd * 2.0f) * total;
        Prop p = pool[0].p;
        for (auto& w : pool) { if (pick < w.w) { p = w.p; break; } pick -= w.w; }
        out.push_back({p, sx + Hash1(wx + sd) * 100, 466 + Hash1(wx * 2.1f) * 46, (int)(wx * 13)});
    });
    std::sort(out.begin(), out.end(), [](const PlacedProp& a, const PlacedProp& b) { return a.y < b.y; });
    return out;
}
static void DrawPathProps(Game& g) {
    for (const PlacedProp& pp : CollectProps(g)) { // nearer props (lower on the ground plane) are drawn larger
        float k = 1.25f + (pp.y - 466) / 46.0f * 0.5f;
        rlPushMatrix(); rlTranslatef(pp.x, pp.y, 0); rlScalef(k, k, 1); rlTranslatef(-pp.x, -pp.y, 0);
        DrawProp(pp.p, pp.x, pp.y, g.time, pp.seed);
        rlPopMatrix();
    }
}

// Small clutter thickly strewn over the ground plane so the floor is never bare: pebbles, tufts, bones and cracks.
static void DrawGroundClutter(Game& g) {
    auto& d = g.dungeon;
    const Color ink{6, 7, 10, 255};
    Color rock = d.loc == Location::Island ? Color{112, 94, 68, 255} : d.loc == Location::Weeds ? Color{58, 72, 60, 255} : d.loc == Location::Atlantis ? Color{108, 108, 122, 255} : Color{82, 96, 104, 255};
    Color plant = d.loc == Location::Island ? Color{112, 118, 56, 255} : d.loc == Location::Weeds ? Color{56, 130, 78, 255} : d.loc == Location::Atlantis ? Color{120, 90, 150, 255} : Color{62, 140, 128, 255};
    float sd = (float)(d.visSeed % 4099) * 0.37f, t = g.time;
    Repeat(LayerOffset(g, 1.0f), 46, [&](float sx, float wx) {
        float h = Hash1(wx * 0.53f + sd);
        if (h < 0.12f) return;
        float x = sx + Hash1(wx * 3.1f + sd) * 40, y = 458 + Hash1(wx * 1.7f + sd) * 84;
        switch ((int)(Hash1(wx * 2.3f + sd) * 5)) {
            case 0: // a cluster of pebbles
                for (int k = 0; k < 3; k++) { float r = 4 + (k * 3 + (int)wx) % 4; DrawEllipse((int)x + k * 7 - 7, (int)y - (int)(r * 0.5f), r + 1.5f, r * 0.7f + 1.5f, ink); DrawEllipse((int)x + k * 7 - 7, (int)y - (int)(r * 0.5f), r, r * 0.7f, k == 1 ? Tone(rock, 0.15f) : rock); }
                break;
            case 1: // a tuft of moss, weed or crystal grass, swaying a little
                for (int k = 0; k < 4; k++) { float sw = sinf(t * 1.3f + wx + k) * 2.5f; DrawLineEx({x + k * 3 - 5, y}, {x + k * 4 - 6 + sw, y - 12 - k % 3 * 5}, 3.4f, ink); DrawLineEx({x + k * 3 - 5, y}, {x + k * 4 - 6 + sw, y - 12 - k % 3 * 5}, 1.8f, plant); }
                break;
            case 2: // a bone
                DrawLineEx({x - 9, y}, {x + 9, y - 3}, 5, ink); DrawLineEx({x - 9, y}, {x + 9, y - 3}, 3, Color{196, 188, 164, 255});
                DrawCircleV({x - 10, y - 1}, 3.4f, Color{196, 188, 164, 255}); DrawCircleV({x + 10, y - 4}, 3.4f, Color{196, 188, 164, 255});
                break;
            case 3: // a shell
                DrawCircleSector({x, y}, 8, 180, 360, 10, ink); DrawCircleSector({x, y}, 6.5f, 180, 360, 10, Color{214, 178, 160, 255});
                for (int k = -1; k <= 1; k++) DrawLineEx({x, y}, {x + k * 4.0f, y - 6}, 1, Color{150, 110, 96, 255});
                break;
            default: // a crack in the ground
                DrawLineEx({x - 12, y}, {x - 2, y - 3}, 2.2f, ink); DrawLineEx({x - 2, y - 3}, {x + 4, y + 1}, 2.2f, ink); DrawLineEx({x + 4, y + 1}, {x + 14, y - 2}, 2.2f, ink);
                break;
        }
    });
}

// The very front of the frame: heavy black ink silhouettes at the camera lens, so the scene is seen through them.
static art::ParallaxBackgroundManager& PaintedBackground(const Game& g, bool& active);
static void DrawRegionForeground(Game& g) {
    {
        bool painted = false;
        art::ParallaxBackgroundManager& pm = PaintedBackground(g, painted);
        if (painted) { pm.Draw(art::BackgroundLayer::Foreground, -LayerOffset(g, 1.0f), 0, SCREEN_W, SCREEN_H); return; }
    }
    auto& d = g.dungeon;
    float t = g.time;
    const Color fg{4, 7, 9, 255};
    switch (d.loc) {
        case Location::Cave: // stalactite teeth along the ceiling, and rocks at the corners
            Repeat(LayerOffset(g, 1.4f), 210, [&](float sx, float wx) {
                float x = sx + Hash1(wx) * 120, h = 40 + Hash1(wx + 2) * 120, w = 20 + Hash1(wx + 3) * 30;
                DrawTri({x - w, -4}, {x + w, -4}, {x + (Hash1(wx + 5) - 0.5f) * 16, h}, fg);
            });
            DrawCircle(-30, 760, 190, fg); DrawCircle(1320, 770, 200, fg);
            break;
        case Location::Island: // hanging jungle vines with broad leaves, and palm fronds from the top corners
            Repeat(LayerOffset(g, 1.4f), 280, [&](float sx, float wx) {
                float x = sx + Hash1(wx) * 160, len = 90 + Hash1(wx + 1) * 130;
                Vector2 prev{x, -4};
                for (int s2 = 1; s2 <= 8; s2++) {
                    Vector2 q{x + sinf(t * 0.8f + wx + s2 * 0.5f) * s2 * 2.2f, -4 + len * s2 / 8};
                    DrawLineEx(prev, q, 7.0f - s2 * 0.6f, fg);
                    if (s2 % 2 == 0) DrawTri(q, {q.x + (s2 % 4 == 0 ? 26.0f : -26.0f), q.y + 9}, {q.x + 3, q.y + 20}, fg);
                    prev = q;
                }
            });
            for (int side = -1; side <= 1; side += 2) // palm fronds arching in from the corners
                for (int k = 0; k < 5; k++) {
                    float a = (side < 0 ? 0.25f : PI - 0.25f) + side * k * 0.16f, cx0 = side < 0 ? -10.0f : 1290.0f;
                    DrawTri({cx0, 0}, {cx0 + cosf(a) * 260 + sinf(t * 0.9f + k) * 6, sinf(a) * 220 + 20}, {cx0 + cosf(a + 0.17f * -side) * 190, sinf(a + 0.17f * -side) * 160 + 40}, fg);
                }
            break;
        case Location::Weeds: // tall kelp blades rising at the left and right edges, hanging weed above
            for (int side = 0; side < 2; side++)
                for (int k = 0; k < 4; k++) {
                    float bx = side ? 1290.0f - k * 34 : -10.0f + k * 34, h = 210 + (k * 53 + side * 31) % 150;
                    Vector2 prev{bx, 726};
                    for (int s2 = 1; s2 <= 9; s2++) {
                        Vector2 q{bx + sinf(t * 0.7f + k + side * 2 + s2 * 0.5f) * s2 * 3.2f, 726 - h * s2 / 9};
                        DrawLineEx(prev, q, 16.0f - s2 * 1.4f, fg);
                        prev = q;
                    }
                }
            Repeat(LayerOffset(g, 1.4f), 240, [&](float sx, float wx) {
                float x = sx + Hash1(wx) * 120, len = 50 + Hash1(wx + 1) * 90;
                Vector2 prev{x, -4};
                for (int s2 = 1; s2 <= 6; s2++) { Vector2 q{x + sinf(t + wx + s2) * s2 * 2.5f, -4 + len * s2 / 6}; DrawLineEx(prev, q, 9.0f - s2, fg); prev = q; }
            });
            break;
        default: // Atlantis: broken columns and a drowned arch at the edges of the frame
            for (int side = 0; side < 2; side++) {
                float bx = side ? 1240.0f : 40.0f, h = side ? 330.0f : 400.0f;
                DrawRectangle((int)bx - 34, (int)(726 - h), 68, (int)h + 10, fg);
                DrawTri({bx - 34, 726 - h}, {bx + 34, 726 - h}, {bx + 6 - side * 12, 726 - h - 42}, fg);
                DrawRectangle((int)bx - 46, (int)(726 - h) + 30, 92, 14, fg);
            }
            DrawRing({110, -40}, 110, 165, 20, 175, 24, fg); DrawRing({1170, -40}, 110, 165, 5, 160, 24, fg); // broken arches hanging in the corners
            break;
    }
}
// The furthest layer of each region, painted as part of its own look (the Cave keeps its dark water and light shafts):
// the Island a drowned eldritch sunset behind distant palm isles and stilt huts, the Weeds a green kelp forest lit from far
// above, Atlantis a vast sunken city of colonnades, domes and spires under a pale, watching moon.
static void DrawRegionFar(Game& g) {
    float t = g.time;
    Rectangle full{0, 0, (float)SCREEN_W, (float)SCREEN_H};
    if (g.dungeon.loc == Location::Island) {
        DrawVGradient(full, Color{96, 36, 74, 255}, Color{16, 14, 36, 255});
        Vector2 sun{860 - LayerOffset(g, 0.02f) * 0.5f, 250};
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)sun.x, (int)sun.y, 330, Color{255, 130, 60, 110}, Color{255, 130, 60, 0});
        for (int k = 0; k < 14; k++) { // rays fanning from the sun
            float a = k * 0.45f + t * 0.03f;
            DrawTri(sun, {sun.x + cosf(a) * 900, sun.y + sinf(a) * 900}, {sun.x + cosf(a + 0.08f) * 900, sun.y + sinf(a + 0.08f) * 900}, Color{255, 150, 80, 12});
        }
        EndBlendMode();
        DrawCircleV(sun, 66, Color{255, 196, 120, 255});
        DrawRing(sun, 72, 79, 0, 360, 48, Color{140, 44, 84, 255}); // a dark ring about it: an eclipse, or an eye
        DrawCircleV({sun.x + 14, sun.y - 6}, 26, Color{40, 14, 40, 230});
        Repeat(LayerOffset(g, 0.05f), 470, [&](float sx, float wx) { // dusk cloud, banded
            float x = sx + Hash1(wx) * 200, y = 90 + Hash1(wx + 3) * 170;
            for (int k = 0; k < 3; k++) DrawEllipse((int)(x + k * 40), (int)(y + k * 6), 150, 9, Color{170, 64, 96, 90});
        });
        for (int layer = 0; layer < 2; layer++) { // distant palm isles and stilt huts on the horizon
            Color land = layer ? Color{34, 20, 44, 255} : Color{52, 26, 58, 255};
            Repeat(LayerOffset(g, layer ? 0.11f : 0.07f), layer ? 520 : 700, [&](float sx, float wx) {
                float x = sx + Hash1(wx + layer) * 240, hh = 40 + Hash1(wx + 2) * 40, by = layer ? 452 : 436;
                DrawEllipse((int)x, (int)by, 200, hh, land);
                for (int p = 0; p < 3; p++) { // palms
                    float px = x - 90 + p * 80 + Hash1(wx + p * 7) * 30, ph = 70 + Hash1(wx + p) * 50 - layer * 16, lean = (p - 1) * 10;
                    DrawLineEx({px, by - hh * 0.5f}, {px + lean, by - hh * 0.5f - ph}, layer ? 5.0f : 6.0f, land);
                    for (int fr = -2; fr <= 2; fr++) DrawTri({px + lean, by - hh * 0.5f - ph}, {px + lean + fr * 22.0f, by - hh * 0.5f - ph + 10 + abs(fr) * 5}, {px + lean + fr * 22.0f + 6, by - hh * 0.5f - ph + 20}, land);
                }
                if (Hash1(wx + 11) > 0.4f) { // a hut on stilts, one window lit
                    float hx = x + 70;
                    DrawRectangle((int)hx - 14, (int)(by - hh * 0.4f) - 22, 28, 20, land);
                    DrawTri({hx - 20, by - hh * 0.4f - 22}, {hx + 20, by - hh * 0.4f - 22}, {hx, by - hh * 0.4f - 42}, land);
                    for (int s = -1; s <= 1; s += 2) DrawLineEx({hx + s * 10.0f, by - hh * 0.4f - 2}, {hx + s * 12.0f, by - hh * 0.4f + 22}, 3, land);
                    DrawRectangle((int)hx - 3, (int)(by - hh * 0.4f) - 16, 6, 6, Color{255, 180, 90, 255});
                }
            });
        }
    } else if (g.dungeon.loc == Location::Weeds) {
        DrawVGradient(full, Color{34, 112, 86, 255}, Color{4, 26, 26, 255});
        BeginBlendMode(BLEND_ADDITIVE); // the sun through the canopy of the surface
        Repeat(LayerOffset(g, 0.05f), 260, [&](float sx, float wx) {
            float x = sx + sinf(t * 0.25f + wx) * 22, w = 34 + Hash1(wx) * 46;
            DrawTri({x, 0}, {x + w, 0}, {x - 120, 620}, Color{120, 220, 150, 20});
            DrawTri({x + w, 0}, {x - 120 + w * 2, 620}, {x - 120, 620}, Color{120, 220, 150, 20});
        });
        EndBlendMode();
        for (int layer = 0; layer < 3; layer++) { // a kelp forest in three depths, the furthest palest
            float f = 0.05f + layer * 0.06f;
            Color kc = layer == 0 ? Color{28, 96, 78, 255} : layer == 1 ? Color{18, 74, 62, 255} : Color{10, 52, 46, 255};
            Repeat(LayerOffset(g, f), 64 - layer * 8, [&](float sx, float wx) {
                float x = sx + Hash1(wx + layer) * 40, h = 200 + Hash1(wx + 5 + layer) * 220;
                Vector2 prev{x, 470};
                for (int s2 = 1; s2 <= 9; s2++) {
                    Vector2 q{x + sinf(t * 0.6f + wx * 0.7f + s2 * 0.5f) * s2 * 2.4f, 470 - h * s2 / 9};
                    DrawLineEx(prev, q, 13.0f - s2 * 1.1f - layer, kc);
                    if (s2 % 2 == 0) DrawEllipse((int)q.x + (s2 % 4 ? 12 : -12), (int)q.y, 12, 4, kc); // a broad blade
                    prev = q;
                }
            });
        }
    } else { // Atlantis
        DrawVGradient(full, Color{44, 36, 96, 255}, Color{6, 6, 24, 255});
        Vector2 moon{640 - LayerOffset(g, 0.02f) * 0.4f, 170};
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((int)moon.x, (int)moon.y, 260, Color{140, 120, 220, 90}, Color{140, 120, 220, 0});
        EndBlendMode();
        DrawCircleV(moon, 78, Color{176, 168, 226, 255});
        DrawCircleV({moon.x + 22, moon.y}, 26, Color{22, 18, 52, 255}); // a slitted pupil in a pale, watching eye
        DrawEllipse((int)moon.x + 22, (int)moon.y, 9, 34, Color{160, 40, 60, 255});
        for (int layer = 0; layer < 2; layer++) { // the drowned city: colonnades, domes and spires
            Color st = layer ? Color{18, 16, 52, 255} : Color{32, 28, 80, 255};
            Repeat(LayerOffset(g, layer ? 0.12f : 0.07f), layer ? 620 : 800, [&](float sx, float wx) {
                float x = sx + Hash1(wx + layer) * 260, by = layer ? 456 : 440;
                int kind = (int)(Hash1(wx + 4 + layer) * 3);
                if (kind == 0) { // a colonnade under an architrave and pediment
                    for (int k = 0; k < 6; k++) DrawRectangle((int)(x + k * 34), (int)(by - 150), 16, 150, st);
                    DrawRectangle((int)x - 10, (int)(by - 166), 220, 16, st);
                    DrawTri({x - 10, by - 166}, {x + 210, by - 166}, {x + 100, by - 210}, st);
                } else if (kind == 1) { // a great dome on a drum
                    DrawRectangle((int)x, (int)(by - 90), 150, 90, st);
                    DrawCircleSector({x + 75, by - 90}, 84, 270, 450, 24, st);
                    DrawRectangle((int)x + 70, (int)(by - 200), 10, 40, st);
                } else { // spires and a broken arch
                    for (int k = 0; k < 4; k++) { float hh = 120 + Hash1(wx + k * 3) * 130; DrawTri({x + k * 44.0f, by}, {x + k * 44.0f + 30, by}, {x + k * 44.0f + 15, by - hh}, st); }
                    DrawRing({x + 230, by}, 60, 80, 180, 320, 20, st);
                }
                BeginBlendMode(BLEND_ADDITIVE);
                for (int w = 0; w < 4; w++) DrawRectangle((int)(x + 20 + Hash1(wx + w * 5) * 160), (int)(by - 30 - Hash1(wx + w) * 90), 4, 6, Color{110, 190, 230, (unsigned char)(90 + 50 * sinf(t + wx + w))}); // lit windows
                EndBlendMode();
            });
        }
    }
}
// ART HOOK: painted parallax backgrounds. If assets/backgrounds/<region>/layers.txt exists (region: cave, island, weeds, atlantis) the
// ParallaxBackgroundManager draws those bands (distant and mid planes here, the foreground plane in front of the fighters) at their own
// scroll speeds, in place of the procedural layers below. See sprite_renderer.h for the file format.
static art::ParallaxBackgroundManager& PaintedBackground(const Game& g, bool& active) {
    static art::ParallaxBackgroundManager mgr;
    static int loaded = -1;
    static bool have = false;
    int loc = (int)g.dungeon.loc;
    if (loaded != loc) {
        static const char* names[4] = {"cave", "island", "weeds", "atlantis"};
        loaded = loc;
        have = art::ParallaxBackgroundManager::HasAssets(names[std::clamp(loc, 0, 3)]) && mgr.LoadRegion(names[std::clamp(loc, 0, 3)]);
    }
    active = have;
    return mgr;
}
static void DrawCaveLayers(Game& g) {
    {
        bool painted = false;
        art::ParallaxBackgroundManager& pm = PaintedBackground(g, painted);
        if (painted) {
            float camX = -LayerOffset(g, 1.0f);
            pm.Draw(art::BackgroundLayer::Distant, camX, 0, SCREEN_W, SCREEN_H);
            pm.Draw(art::BackgroundLayer::Mid, camX, 0, SCREEN_W, SCREEN_H);
            return;
        }
    }
    float t = g.time;
    // 1. the far water, with bioluminescent haze drifting in it
    float deep = CAVE_TIER_LEVEL[g.dungeon.tier] / 6.0f; // deeper levels: darker water, more bones, more glowing things
    BeginBackdrop(); // layers 1-3 are far away: drawn out of focus, like a camera focused on the fighters
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
    if (g.dungeon.loc != Location::Cave) DrawRegionFar(g); // each region paints its own furthest layer instead of the cave water
    // 2. the far cave walls
    if (g.dungeon.loc == Location::Cave) DrawRidge(LayerOffset(g, 0.12f), 318, 70, 3, false, Color{16, 44, 56, 255}, 70);
    if (g.dungeon.loc == Location::Cave) DrawRidge(LayerOffset(g, 0.2f), 372, 50, 11, false, Color{19, 48, 60, 255}, 40);
    FogVeil(0.16f);   // fog between layers, in the scene's own fog colour
    Repeat(LayerOffset(g, 0.16f), 900, [&](float sx, float wx) { // schools of fish drifting past
        float dir = Hash1(wx) > 0.5f ? 1.0f : -1.0f, cx = sx + fmodf(t * 14 * dir + 9000, 900.0f) - 450, cy = 150 + Hash1(wx + 1) * 170;
        for (int f = 0; f < 16; f++) {
            float fx = cx + (Hash1(wx + f) - 0.5f) * 140 + sinf(t * 1.4f + f) * 4, fy = cy + (Hash1(wx + f + 30) - 0.5f) * 50 + cosf(t + f) * 3;
            DrawEllipse((int)fx, (int)fy, 6, 2, Color{70, 120, 130, 255});
            DrawTri({fx - dir * 5, fy}, {fx - dir * 10, fy - 3}, {fx - dir * 10, fy + 3}, Color{70, 120, 130, 255});
        }
    });
    // 3. distant rock columns rising from floor to ceiling (the Cave's own; the other regions have their own, below)
    if (g.dungeon.loc == Location::Cave) Repeat(LayerOffset(g, 0.3f), 430, [&](float sx, float wx) {
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
    FogVeil(0.12f);
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
    EndBackdrop(1.7f);
    // 4. the ceiling's stalactites, stalagmites and swaying kelp
    float off4 = LayerOffset(g, 0.45f);
    if (g.dungeon.loc == Location::Cave) {
        DrawRidge(off4, 70, 40, 5, true, Color{14, 30, 38, 255}, 130);
        DrawRidge(off4, 432, 22, 21, false, Color{20, 40, 48, 255}, 60);
    }
    DrawRegionMidground(g);
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
    FogVeil(0.06f);   // a thinner veil: the near layers are closer
    // 5. the near wall behind the fighters, studded with glowing crystals
    float off5 = LayerOffset(g, 0.7f);
    DrawTiled(Tex::Rock, {0, 392, (float)SCREEN_W, 64}, 1.2f, Color{70, 86, 92, 255}, {-off5 / 1.2f, 0});
    for (int x = 0; x < SCREEN_W; x += 4) { // a ragged top edge
        float y = 392 - fabsf(sinf((x - off5) * 0.021f) * 16 + sinf((x - off5) * 0.07f) * 6);
        DrawRectangle(x, (int)y, 4, (int)(393 - y), Color{56, 70, 76, 255});
    }
    DrawVGradient({0, 392, (float)SCREEN_W, 64}, Fade(BLACK, 0.05f), Fade(BLACK, 0.45f));
    if (g.dungeon.loc == Location::Cave) for (Vector2 c : CrystalSpots(g)) {
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
    BeginBlendMode(BLEND_ADDITIVE); // caustics: light from far above, rippling across the wet floor
    for (int k = 0; k < 14; k++) {
        float y0 = 462 + k * k * 1.4f, sq = 0.35f + k * 0.05f; // bands crowd together toward the far edge
        Vector2 prev{-20, y0};
        for (float x = -20; x <= SCREEN_W + 20; x += 24) {
            float u = x + off6 * 0.6f;
            Vector2 q{x, y0 + (sinf(u * 0.021f + t * 0.9f + k * 1.7f) * 7 + sinf(u * 0.047f - t * 1.3f + k) * 4) * sq};
            DrawLineEx(prev, q, 1.5f + k * 0.12f, Color{120, 200, 210, (unsigned char)(10 + k)});
            prev = q;
        }
    }
    EndBlendMode();
    Repeat(off6, 610, [&](float sx, float wx) { // vents in the floor, trickling bubbles
        float vx = sx + Hash1(wx + 4) * 300, vy = 470 + Hash1(wx + 6) * 30;
        DrawEllipse((int)vx, (int)vy, 10, 3, Color{30, 40, 44, 255});
    });
}

// 7. rocks and kelp right in front of the view: dark, and moving fastest of all
static void DrawCaveForeground(Game& g) {
    float t = g.time;
    DrawRegionForeground(g);
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
    if (DrawRichEnemy(e, r, t)) return;
    if (e.type >= EnemyType::DysCrustacean && e.type != EnemyType::COUNT) { DrawBestiaryFigure(e, r, t); return; } // the region bestiaries
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
            Color c{150, 92, 80, 255}, dk{104, 62, 56, 255}, lt{176, 130, 112, 255};
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

struct AnimFx { Pose pose; float dx = 0, dy = 0; Color tint = WHITE; float sx = 1, sy = 1; }; // sx/sy: squash and stretch, about the feet

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
            const Style S[(int)HeroClass::COUNT] = {
                {0.45f, -0.15f, 0.0f, 70, 35, 1.0f, 0.45f, 0.1f},    // Nurse: a quick slash
                {0.0f, -0.3f, 0.35f, 95, 10, 1.0f, 0.6f, 0.25f},    // Diver: a harpoon lunge
                {1.0f, -0.25f, 0.0f, 60, 95, 0.7f, 0.5f, 0.05f},    // Captain: an overhead cut
                {1.0f, -0.4f, 0.2f, 45, 80, 0.5f, 0.65f, 0.45f},    // Mechanic: a heavy swing
                {0.3f, -0.2f, 0.1f, 55, 50, 0.7f, 0.4f, 0.15f},     // Whaler: a hooked jab
                {0.6f, -0.35f, 0.25f, 75, 65, 0.6f, 0.7f, 0.3f},    // Stowaway: a wild, staggering swing
                {0.7f, -0.2f, 0.05f, 90, 70, 0.9f, 0.55f, 0.2f},    // Merman: a crushing tail-driven blow
                {0.4f, -0.15f, 0.0f, 50, 40, 0.6f, 0.35f, 0.1f},    // Queen: a regal, precise strike
                {0.85f, -0.1f, 0.35f, 40, 90, 0.4f, 0.3f, 0.4f},    // Robot: a slow, mechanical piston punch
                {0.15f, -0.45f, 0.3f, 100, 20, 1.1f, 0.75f, 0.35f}, // Octopus: a fast whipping tentacle
                {0.35f, -0.25f, 0.1f, 55, 45, 0.6f, 0.5f, 0.15f},   // Siren: a graceful, sudden strike
                {0.5f, -0.3f, 0.15f, 60, 55, 0.7f, 0.6f, 0.2f},     // Wisp: a drifting, ethereal lash
            };
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
            p.stride = strike * 0.9f - wind * 0.2f;
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
            p.headDown = -0.9f * b;        // the head snaps back...
            p.backRaise = 0.55f * b;       // ...an arm flies up...
            p.stride = -0.6f * b;          // ...and they stagger a step back
            p.tremble = 0.6f * Bell(u, 0.1f, 0.25f, 0.6f);
            fx.tint = {255, (unsigned char)(255 - 120 * b), (unsigned char)(255 - 130 * b), 255};
            fx.sx = 1 + 0.09f * b; fx.sy = 1 - 0.08f * b;                                       // flinches: squashed by the blow
        } break;
        case Anim::Dodge: {
            float b = Bell(u, 0, 0.15f, 0.45f);
            fx.dx = -26 * b;
            p.crouch = 0.35f * b;
            p.lean = -0.2f * b;
            p.stride = -0.5f * b;
        } break;
        case Anim::Stress: { // dread: they flinch, hunch, bring a hand up and shake
            float b = Bell(u, 0, 0.18f, 1.05f);
            p.crouch = 0.3f * b;
            p.lean = -0.18f * Bell(u, 0, 0.1f, 0.4f) + 0.12f * Bell(u, 0.3f, 0.6f, 1.05f);
            p.headDown = 0.7f * b;
            p.backRaise = 0.35f * b;
            p.tremble = 1.0f * b;
            fx.dx = -6 * Bell(u, 0, 0.1f, 0.5f);
        } break;
        default: break;
    }
    return fx;
}

// Every pose change goes through a spring, so motions blend into each other, overshoot a touch and
// settle, rather than snapping from one position to the next.
static Pose SpringPose(int id, const Pose& target, float& dx, float dt) {
    struct Spring { float v[9] = {}, vel[9] = {}; bool init = false; };
    static std::unordered_map<int, Spring> springs;
    Spring& sp = springs[id];
    float tgt[9] = {target.lean, target.crouch, target.reach, target.raise, target.backRaise, target.weaponTilt / 100, target.stride, target.headDown, dx};
    if (!sp.init) { for (int i = 0; i < 9; i++) sp.v[i] = tgt[i]; sp.init = true; }
    const float w = 24, z = 0.62f;
    float step = std::min(dt, 1 / 30.0f);
    for (int i = 0; i < 9; i++) {
        float acc = w * w * (tgt[i] - sp.v[i]) - 2 * z * w * sp.vel[i];
        sp.vel[i] += acc * step;
        sp.v[i] += sp.vel[i] * step;
    }
    Pose p = target;
    p.lean = sp.v[0]; p.crouch = sp.v[1]; p.reach = sp.v[2]; p.raise = sp.v[3]; p.backRaise = sp.v[4];
    p.weaponTilt = sp.v[5] * 100; p.stride = sp.v[6]; p.headDown = sp.v[7];
    dx = sp.v[8];
    return p;
}

static AnimFx EnemyAnimFx(const Game& g, const Enemy& e) {
    AnimFx fx;
    const UnitAnim* a = FindAnim(g, false, e.uid);
    if (!a) return fx;
    float u = a->t;
    switch (a->kind) {
        case Anim::Melee: {
            fx.dx = -85 * Bell(u, 0.2f, 0.36f, 0.8f) + 12 * Bell(u, 0, 0.16f, 0.28f);
            float wind = Bell(u, 0, 0.13f, 0.24f), strike = Bell(u, 0.2f, 0.3f, 0.5f);      // gather, then stretch into the blow
            fx.sx = 1 + 0.07f * wind - 0.06f * strike; fx.sy = 1 - 0.06f * wind + 0.07f * strike;
        } break;
        case Anim::Ranged: {
            fx.dx = 10 * Bell(u, 0.05f, 0.25f, 0.45f); fx.dy = -6 * Bell(u, 0.25f, 0.32f, 0.5f);
            float draw = Bell(u, 0, 0.2f, 0.35f);
            fx.sx = 1 - 0.04f * draw; fx.sy = 1 + 0.05f * draw;                                 // rears up to cast or spit
        } break;
        case Anim::Buff: fx.dy = -10 * Bell(u, 0.1f, 0.3f, 0.8f); fx.dx = 4 * sinf(u * 60) * Bell(u, 0.1f, 0.3f, 0.8f); break;
        case Anim::Hurt: {
            float spring = expf(-7 * u) * cosf(u * 16) * std::min(1.0f, u / 0.04f), b = Bell(u, 0, 0.07f, 0.48f);
            fx.dx = 20 * spring;
            fx.dy = -4 * b;
            fx.tint = {255, (unsigned char)(255 - 120 * b), (unsigned char)(255 - 130 * b), 255};
            fx.sx = 1 + 0.09f * b; fx.sy = 1 - 0.08f * b;                                       // flinches: squashed by the blow
        } break;
        case Anim::Dodge: fx.dx = 28 * Bell(u, 0, 0.15f, 0.45f); break;
        default: break;
    }
    if (!e.alive) { // dying: it buckles and sinks as it fades
        float prog = std::clamp(a->kind == Anim::Hurt ? a->t / a->dur : 1.0f, 0.0f, 1.0f);
        fx.tint.a = (unsigned char)(255 * (1 - prog));
        fx.sy *= 1 - 0.7f * prog * prog; fx.sx *= 1 + 0.3f * prog; fx.dx += 8 * prog;
    }
    return fx;
}

static std::string StatusTags(const Status& st) {
    std::string s;
    if (st.bleedTurns > 0) s += "BLEED ";
    if (st.poisonTurns > 0) s += TextFormat("POISON %d ", st.poisonDmg);
    if (st.stunned > 0) s += "STUN ";
    if (st.marked > 0) s += "MARKED ";
    if (st.buffTurns > 0) s += st.buffDmg > 0 ? "RALLY " : "WEAKENED ";
    if (st.dodgeTurns > 0) s += st.dodgeBuff > 0 ? "DODGE+ " : "OFF-BALANCE ";
    if (st.protTurns > 0) s += st.protBuff > 0 ? "ARMOR+ " : "EXPOSED ";
    if (st.accTurns > 0) s += st.accBuff > 0 ? "ACC+ " : "BLINDED ";
    if (st.spdTurns > 0) s += "SLOWED ";
    if (st.burnTurns > 0) s += "BURN ";
    if (st.siltTurns > 0) s += "SILT ";
    if (st.drownTurns > 0) s += "DROWNING ";
    if (st.madTurns > 0) s += "MADNESS ";
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
    // whoever acts steps forward half a rank onto a lit ring on the ground
    int actHero = -1, actEnemy = -1;
    if (d.phase == DPhase::Combat && d.turnIdx < (int)d.order.size()) { const TurnEntry& te = d.order[d.turnIdx]; (te.hero ? actHero : actEnemy) = te.id; }
    static std::unordered_map<int, float> step;
    auto stepOf = [&](int key, bool on) { float& v = step[key]; v += ((on ? 1.0f : 0.0f) - v) * std::min(1.0f, dt * 6); return v; };
    auto ring = [&](Vector2 at, float w, float k) {
        if (k < 0.02f) return;
        DrawEllipse((int)at.x, (int)at.y, w, 12, Fade(Color{255, 210, 140, 255}, 0.22f * k));
        DrawEllipseLines((int)at.x, (int)at.y, w, 12, Fade(Color{255, 220, 160, 255}, 0.7f * k));
    };
    for (int p = PARTY_SIZE - 1; p >= 0; p--) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        AnimFx fx = HeroAnimFx(g, *h);
        // alive even when standing still: breathing, and a slow shift of weight from foot to foot...
        float ph = t + h->id * 2.3f;
        fx.pose.lean += 0.035f * sinf(ph * 1.1f);
        fx.pose.crouch += 0.04f * (0.5f + 0.5f * sinf(ph * 1.7f));
        fx.dx += sinf(ph * 0.6f) * 1.5f;
        // ...and wearing the strain: nerves hunch the shoulders and shake the hands; at Death's Door they
        // sag to one knee, heaving for breath
        float nerves = h->rattled ? 1.0f : h->stress / 100.0f;
        fx.pose.crouch += nerves * 0.14f;
        fx.pose.headDown += nerves * 0.35f;
        fx.pose.tremble = std::max(fx.pose.tremble, nerves * nerves * 0.7f);
        if (h->deathsDoor) { // on one knee, breathing slow and heavy
            fx.pose.crouch += 0.5f + 0.08f * sinf(ph * 1.6f);
            fx.pose.headDown += 0.6f;
            fx.pose.lean += 0.18f;
        }
        fx.pose = SpringPose(h->id, fx.pose, fx.dx, dt);
        float st = stepOf(h->id, h->id == actHero);
        Vector2 feet{ShownX(h->id, r.x + r.width / 2, dt) + fx.dx + st * 45 + gShake.x, r.y + r.height + fx.dy + gShake.y};
        ring({feet.x, r.y + r.height}, 52, st);
        DrawShadowBlob({feet.x, r.y + r.height}, 38);
        if (h->deathsDoor) SetFigureMood(0.8f, 1.0f);   // Death's Door: grey, with a red rim light
        DrawCrewFigureInked(*h, feet, 1.42f, true, walking ? d.walkT * 9 + p * 1.3f : 0, t, fx.pose, fx.tint);
        SetFigureMood(0, 0);
        cfx::DrawStatus(feet, 170 * 1.42f, h->st, h->st.marked > 0, t);
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        AnimFx fx = EnemyAnimFx(g, e);
        if (fx.tint.a == 0) continue;
        Rectangle r = EnemyRect(g, p);
        float est = stepOf(1000000 + e.uid, e.uid == actEnemy && e.span <= 1);
        Vector2 feet{ShownX(1000000 + e.uid, r.x + r.width / 2, dt) + fx.dx - est * 45 + gShake.x, r.y + r.height + fx.dy + gShake.y}, ff = FigureFeet();
        ring({feet.x, r.y + r.height}, e.boss ? 90 : 52, e.uid == actEnemy ? std::max(est, 0.8f) : est);
        DrawShadowBlob({feet.x, r.y + r.height}, e.boss ? 70 : 44);
        // ART HOOK: a painted creature. If assets/enemies/<name>/layers.txt exists (name lower-case, spaces as underscores: ghost_worm,
        // crustacean_queen, cthulhu ...) it is drawn from its layered high-resolution sprites as painted: no procedural body, no figure
        // shader, no rust or noise. The collision hull comes from UpdateEnemyCollisionMesh (used by the hit tests when they need it).
        {
            std::string key = e.name;
            for (auto& ch : key) ch = ch == ' ' ? '_' : (char)tolower(ch);
            static std::unordered_map<std::string, art::EnemyRenderer> painted;
            if (art::EnemyRenderer::HasAssets(key)) {
                art::EnemyRenderer& er = painted[key];
                if (!er.Ready()) er.Load(key);
                if (er.Ready()) { er.Draw(feet, std::max(0.2f, r.height / 200.0f) * fx.sx, t, fx.tint); continue; }
            }
        }
        rig::SetWorldOffset({feet.x - ff.x, feet.y - ff.y});
        {   // rig figures play a clip for what they are doing: a cast, a strike, a flinch
            const UnitAnim* a = FindAnim(g, false, e.uid);
            int clip = -1;
            if (a) switch (a->kind) {
                case Anim::Ranged: case Anim::Buff: case Anim::Heal: clip = rig::CL_CAST; break;
                case Anim::Melee: clip = rig::CL_SLASH; break;
                case Anim::Hurt: clip = e.alive ? rig::CL_HIT : rig::CL_DEATH; break;
                case Anim::Dodge: clip = rig::CL_DODGE; break;
                default: break;
            }
            RigSetActing(clip, a && a->dur > 0 ? a->t / a->dur * rig::GetClip(clip < 0 ? 0 : clip).dur : 0);
        }
        SetFigureFacing(-1);
        BeginFigure(); // draw on the figure canvas, lined up so its feet land on FigureFeet()
        DrawEnemyFigure(e, {ff.x - r.width / 2, ff.y - r.height, r.width, r.height}, t);
        RigSetActing(-1, 0);
        float breathe = 1 + 0.012f * sinf(t * 1.7f + e.uid * 1.3f);                               // it breathes, slowly
        EndFigure(feet, fx.tint, fx.sx / breathe, fx.sy * breathe);
        SetFigureFacing(0);
        if (e.alive) cfx::DrawStatus(feet, r.height, e.st, e.st.marked > 0, t);
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
    // Under each fighter only a thin health bar and a nerves bar, as in Darkest Dungeon; names show on hover,
    // everything else lives in the HUD below the stage.
    auto& d = g.dungeon;
    float t = g.time;
    Vector2 m = GetMousePosition();
    for (int p = 0; p < PARTY_SIZE; p++) {
        Hero* h = PartyAt(g, p);
        if (!h) continue;
        Rectangle r = HeroRect(p);
        Stats s = GetStats(*h);
        float bx = r.x + 6, bw = r.width - 12, by = r.y + r.height + 6;
        DrawRectangle((int)bx - 1, (int)by - 1, (int)bw + 2, 11, Color{8, 6, 6, 230});
        DrawRectangle((int)bx, (int)by, (int)(bw * std::clamp((float)h->hp / s.maxHp, 0.0f, 1.0f)), 5, h->deathsDoor ? Color{120, 20, 20, 255} : Color{190, 40, 36, 255});
        DrawRectangle((int)bx, (int)by + 6, (int)(bw * h->stress / 100.0f), 3, Color{200, 200, 220, 255});
        if (h->rattled) Glow({r.x + r.width / 2, r.y - 30}, 34 + sinf(t * 5) * 6, Fade(Pal::Stress, 0.55f));
        if (CheckCollisionPointRec(m, r)) { float nw = (float)MeasureTxt(h->name, 15, true); TxtShadow(h->name, r.x + r.width / 2 - nw / 2, by + 13, 15, Pal::Paper, true); }
        (void)actingHero;
    }
    for (int p = 0; p < (int)d.enemies.size(); p++) {
        const Enemy& e = d.enemies[p];
        if (!e.alive) continue;
        Rectangle r = EnemyRect(g, p);
        float bx = r.x + 6, bw = r.width - 12, by = r.y + r.height + 6;
        DrawRectangle((int)bx - 1, (int)by - 1, (int)bw + 2, 7, Color{8, 6, 6, 230});
        DrawRectangle((int)bx, (int)by, (int)(bw * std::clamp((float)e.hp / e.maxHp, 0.0f, 1.0f)), 5, Color{190, 40, 36, 255});
        if (CheckCollisionPointRec(m, r)) { float nw = (float)MeasureTxt(e.name, 15, true); TxtShadow(e.name, r.x + r.width / 2 - nw / 2, by + 9, 15, Pal::Paper, true); }
        (void)actingEnemy;
    }
}

// the round counter, a brass medallion at the top of the stage
static void DrawRoundMedallion(Game& g) {
    Vector2 c{640, 122};
    DrawCircleV({c.x + 2, c.y + 3}, 26, Fade(BLACK, 0.5f));
    DrawCircleV(c, 26, Pal::BrassDk);
    DrawRing(c, 20, 25, 0, 360, 36, Pal::Brass);
    for (int k = 0; k < 8; k++) { float a = k * PI / 4; DrawCircleV({c.x + cosf(a) * 22.5f, c.y + sinf(a) * 22.5f}, 1.6f, Pal::BrassDk); }
    DrawCircleV(c, 19, Color{24, 18, 14, 255});
    std::string rs = std::to_string(std::max(1, g.dungeon.round));
    DrawTextCenteredBold(rs, c.x, c.y - 12, 24, Pal::Brass);
    DrawLineEx({c.x - 110, c.y}, {c.x - 30, c.y}, 2, Fade(Pal::BrassDk, 0.8f));
    DrawLineEx({c.x + 30, c.y}, {c.x + 110, c.y}, 2, Fade(Pal::BrassDk, 0.8f));
}

// who acts next, as a strip of small portraits just above the HUD
static void DrawTurnStrip(Game& g) {
    auto& d = g.dungeon;
    std::vector<TurnEntry> next;
    for (int i = d.turnIdx; i < (int)d.order.size() && next.size() < 8; i++) {
        const TurnEntry& te = d.order[i];
        if (te.hero ? (FindHero(g, te.id) && PartyPos(g, te.id) >= 0) : FindEnemy(g, te.id) != nullptr) next.push_back(te);
    }
    float x = 640 - next.size() * 22.0f;
    for (size_t i = 0; i < next.size(); i++) {
        float sz = i == 0 ? 40.0f : 32.0f;
        Rectangle r{x, 548 - sz, sz, sz};
        DrawRectangleRec({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, i == 0 ? Pal::Brass : next[i].hero ? Pal::BrassDk : Color{120, 30, 26, 255});
        DrawRectangleRec(r, Color{20, 18, 16, 255});
        if (next[i].hero) { if (Hero* h = FindHero(g, next[i].id)) DrawPortrait(*h, r, g.time); }
        else if (Enemy* e = FindEnemy(g, next[i].id)) {
            DrawCircleV({r.x + sz / 2, r.y + sz / 2}, sz * 0.36f, Color{110, 24, 20, 255});
            std::string ini = e->name.substr(0, 1);
            DrawTextCenteredBold(ini, r.x + sz / 2, r.y + sz / 2 - 9, 18, Pal::Paper);
        }
        x += sz + 8;
    }
}

// the HUD plate below the stage
static void DrawHudPlate() {
    Rectangle bar{0, 556, (float)SCREEN_W, (float)SCREEN_H - 556};
    DrawRectangleRec(bar, Color{12, 10, 9, 250});
    DrawTiled(Tex::Metal, bar, 0.7f, Color{40, 36, 32, 255});
    DrawRectangleRec(bar, Fade(Color{8, 6, 5, 255}, 0.6f));
    DrawRectangle(0, 556, SCREEN_W, 3, Pal::BrassDk);
    for (int k = 0; k < 2; k++) { // carved end-pieces, like Darkest Dungeon's
        float x = k ? SCREEN_W - 28.0f : 0.0f;
        DrawRectangle((int)x, 560, 28, SCREEN_H - 560, Color{26, 22, 18, 255});
        for (int j = 0; j < 5; j++) DrawCircleV({x + 14, 580 + j * 30.0f}, 5, Pal::BrassDk);
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
    for (const PlacedProp& pp : CollectProps(g)) { // props that glow light the props and figures around them
        switch (pp.p) {
            case P_TORCH: AddLight({pp.x, pp.y - 74}, 280, Color{255, 170, 80, 255}, 0.95f * flick); break;
            case P_FUNGUS: AddLight({pp.x, pp.y - 20}, 140, Color{90, 220, 210, 255}, 0.7f); break;
            case P_POD: AddLight({pp.x, pp.y - 12}, 130, Color{150, 255, 110, 255}, 0.7f); break;
            case P_BRAZIER: AddLight({pp.x, pp.y - 40}, 220, Color{170, 80, 230, 255}, 0.8f); break;
            case P_VOIDCRYSTAL: AddLight({pp.x, pp.y - 24}, 160, Color{190, 80, 240, 255}, 0.7f); break;
            default: break;
        }
    }
    for (auto& s : d.shots) AddLight({s.from.x + (s.to.x - s.from.x) * std::clamp(s.t / s.dur, 0.0f, 1.0f), s.from.y}, 120, Color{255, 220, 170, 255}, 0.4f);
    LightsEnd();
    for (Vector2 c : crystals) Glow({c.x, c.y - 12}, 26, Color{90, 230, 220, 80});
}

// Tiny drifting specks are drawn after the ink pass: inked, each would get a dark ring (the "black dust").
static void DrawDriftingSpecks(Game& g) {
    float t = g.time, L = g.dungeon.lightShown / 100.0f;
    for (int k = 0; k < 40; k++) { // marine snow drifting through the beam
        float px = fmodf(k * 97.0f + t * (6 + k % 5) + LayerOffset(g, 0.9f) * -1 + 100000, (float)SCREEN_W);
        float py = fmodf(k * 53.0f + t * (10 + k % 7), 520.0f) + 40;
        DrawCircleV({px, py}, 1.3f + (k % 3) * 0.5f, Color{220, 240, 240, (unsigned char)(50 + 60 * L)});
    }
    Repeat(LayerOffset(g, 1.0f), 610, [&](float sx, float wx) { // bubbles trickling from vents in the floor
        float vx = sx + Hash1(wx + 4) * 300, vy = 470 + Hash1(wx + 6) * 30;
        for (int k = 0; k < 6; k++) {
            float ph = fmodf(t * 0.5f + k / 6.0f + Hash1(wx), 1.0f);
            DrawRing({vx + sinf(ph * 9 + k) * 5, vy - ph * 420}, 1.5f + (k % 2), 2.5f + (k % 2), 0, 360, 12, Color{200, 235, 245, (unsigned char)(160 * (1 - ph))});
        }
    });
}

// ---------------------------------------------------------------- the level theme manager
// Every run rolls a visual seed and one of three atmospheric states for its location. The seed decides
// which silhouettes stand on the far horizon and how thick they are; the state decides the colour grade,
// the weather and the light. Both are fixed for the run, so a level looks like itself from room to room,
// and different from the last time. All silhouettes are flat ink-black masses: no gradients, hard edges.
static const char* ATMOS_NAME[LOCATION_COUNT][3] = {
    {"Pitch Black Trench", "Bioluminescent Bloom", "Submerged Silt Storm"},
    {"Torrential Downpour", "Toxic Sea Fog", "Eldritch Sunset"},
    {"Abyssal Current", "Fungal Rot", "Sanguine Tide"},
    {"Cosmic Void", "Drowned Eclipse", "Blood Moon Abyss"},
};
const char* AtmosphereName(Location loc, int variant) { return ATMOS_NAME[(int)loc][std::clamp(variant, 0, 2)]; }

// Distant silhouettes: an ink-black frieze of local landmarks, laid out from the run's seed on two parallax
// depths, with variable spacing so no two runs share a skyline.
// Shafts of light slanting down through the region's air, behind the figures: pale cyan in the cave, a sickly glow over the
// island, green in the weeds, violet in Atlantis. Additive, soft, and slowly drifting.
static void DrawLightShafts(Game& g) {
    auto& d = g.dungeon;
    float t = g.time, sd = (float)(d.visSeed % 1013) * 0.7f;
    Color c = d.loc == Location::Cave ? Color{90, 190, 210, 255} : d.loc == Location::Island ? Color{220, 210, 130, 255} : d.loc == Location::Weeds ? Color{120, 230, 140, 255} : Color{170, 120, 240, 255};
    if (d.atmos == 0 && d.loc == Location::Cave) return; // the pitch-black trench has no light to spare
    BeginBlendMode(BLEND_ADDITIVE);
    Repeat(LayerOffset(g, 0.2f), 260, [&](float sx, float wx) {
        if (Hash1(wx * 0.9f + sd) < 0.35f) return;
        float x = sx + Hash1(wx + sd) * 120, w = 26 + Hash1(wx * 1.7f) * 40, sway = sinf(t * 0.25f + wx) * 14, a = 0.05f + Hash1(wx * 2.3f) * 0.05f;
        Vector2 a0{x, 56}, a1{x + w, 56}, b0{x - 150 + sway, 470}, b1{x + w + 100 + sway, 470};
        DrawTri(a0, b0, a1, Fade(c, a)); DrawTri(a1, b0, b1, Fade(c, a * 0.8f));
    });
    EndBlendMode();
}

static void DrawSeededSilhouettes(Game& g) {
    auto& d = g.dungeon;
    const Color ink{4, 5, 8, 255};
    float sd = (float)(d.visSeed % 9973) * 1.37f, t = g.time;
    for (int layer = 0; layer < 2; layer++) {
        float par = layer ? 0.42f : 0.24f, gap = layer ? 470.0f : 690.0f, base = 452.0f - layer * 10.0f;
        float density = 0.45f + Hash1(sd + layer * 7.0f) * 0.5f;            // how crowded this run's horizon is
        Repeat(LayerOffset(g, par), gap, [&](float sx, float wx) {
            float h = Hash1(wx * 0.37f + sd + layer * 31.0f);
            if (Hash1(wx * 0.11f + sd * 2.1f) > density) return;           // gaps in the skyline
            float x = sx + Hash1(wx + sd) * 180.0f;
            float sc = 0.8f + Hash1(wx * 1.3f + sd) * 0.7f;
            int kind = (int)(h * 4.0f);
            switch (d.loc) {
                case Location::Island:
                    if (kind == 0) { // a stilt hut
                        DrawRectangle((int)x - 30 * sc, (int)(base - 90 * sc), 60 * sc, 34 * sc, ink);
                        DrawTri({x - 40 * sc, base - 90 * sc}, {x + 40 * sc, base - 90 * sc}, {x, base - 130 * sc}, ink);
                        for (int k = 0; k < 4; k++) DrawRectangle((int)(x - 28 * sc + k * 18 * sc), (int)(base - 56 * sc), 4, (int)(56 * sc), ink);
                    } else if (kind == 1) { // a watchtower
                        DrawRectangle((int)x - 6, (int)(base - 190 * sc), 12, (int)(190 * sc), ink);
                        DrawRectangle((int)x - 26, (int)(base - 200 * sc), 52, 22, ink);
                        DrawTri({x - 32, base - 200 * sc}, {x + 32, base - 200 * sc}, {x, base - 232 * sc}, ink);
                    } else if (kind == 2) { // a smoking pyre
                        DrawTri({x - 26, base}, {x + 26, base}, {x, base - 44 * sc}, ink);
                        Glow({x, base - 54 * sc}, 40, Color{255, 110, 40, 80});
                        for (int k = 0; k < 5; k++) DrawCircleV({x + sinf(t + k) * 10 + k * 3, base - 60 * sc - k * 26 - fmodf(t * 20 + k * 9, 30)}, 10 + k * 3, Fade(Color{20, 18, 20, 255}, 0.5f));
                    } else { // a stone idol
                        DrawRectangle((int)x - 16, (int)(base - 120 * sc), 32, (int)(120 * sc), ink);
                        DrawRectangle((int)x - 24, (int)(base - 160 * sc), 48, 44, ink);
                        DrawTri({x - 24, base - 160 * sc}, {x + 24, base - 160 * sc}, {x, base - 186 * sc}, ink);
                    }
                    break;
                case Location::Cave:
                    if (kind < 2) { // a stalactite cluster hanging from the ceiling
                        for (int k = -2; k <= 2; k++) DrawTri({x + k * 22 - 14, 60}, {x + k * 22 + 14, 60}, {x + k * 22 + k * 3, 60 + (90 + Hash1(wx + k) * 110) * sc}, ink);
                    } else if (kind == 2) { // a gargantuan molt in the wall: an empty carapace
                        DrawRing({x, base - 110 * sc}, 70 * sc, 92 * sc, 180, 360, 22, ink);
                        for (int k = 0; k < 5; k++) DrawLineEx({x - 60 * sc + k * 30 * sc, base - 110 * sc}, {x - 70 * sc + k * 34 * sc, base - 50 * sc}, 5, ink);
                    } else { // a chasm opening in the floor
                        DrawTri({x - 90 * sc, base}, {x + 90 * sc, base}, {x, base + 60}, ink);
                    }
                    break;
                case Location::Weeds:
                    if (kind < 3) { // kelp: sparse or choking, by the run's density
                        int n = 2 + (int)(density * 5);
                        for (int k = 0; k < n; k++) {
                            Vector2 prev{x + k * 14.0f, base};
                            for (int sgm = 1; sgm <= 9; sgm++) {
                                Vector2 q{x + k * 14.0f + sinf(t * 0.6f + wx + sgm * 0.5f + k) * sgm * 2.4f, base - sgm * 22.0f * sc};
                                DrawLineEx(prev, q, 7 - sgm * 0.5f, ink);
                                prev = q;
                            }
                        }
                    } else { // a sunken naval hull wrapped in vines
                        DrawTri({x - 150 * sc, base - 30}, {x + 150 * sc, base - 90 * sc}, {x + 150 * sc, base}, ink);
                        DrawRectangle((int)(x + 40 * sc), (int)(base - 220 * sc), 8, (int)(140 * sc), ink);
                        DrawLineEx({x + 44 * sc, base - 210 * sc}, {x - 60 * sc, base - 60}, 3, ink);
                    }
                    break;
                default: // Atlantis
                    if (kind == 0) { // a drowned temple
                        DrawRectangle((int)(x - 110 * sc), (int)(base - 20), (int)(220 * sc), 20, ink);
                        for (int k = 0; k < 5; k++) DrawRectangle((int)(x - 96 * sc + k * 48 * sc), (int)(base - 150 * sc), 18, (int)(130 * sc), ink);
                        DrawTri({x - 118 * sc, base - 150 * sc}, {x + 118 * sc, base - 150 * sc}, {x, base - 210 * sc}, ink);
                    } else if (kind == 1) { // a broken aqueduct
                        for (int k = 0; k < 3; k++) {
                            DrawRectangle((int)(x + k * 90 * sc), (int)(base - 170 * sc), 26, (int)(170 * sc), ink);
                            DrawRing({x + k * 90 * sc + 58 * sc, base - 130 * sc}, 26, 40, 180, 360, 14, ink);
                        }
                    } else if (kind == 2) { // a colossal headless statue
                        DrawTri({x - 60 * sc, base}, {x + 60 * sc, base}, {x, base - 200 * sc}, ink);
                        DrawRectangle((int)(x - 74 * sc), (int)(base - 210 * sc), (int)(148 * sc), 24, ink);
                    } else { // an alien monolith, its runes pulsing
                        DrawTri({x - 22 * sc, base}, {x + 22 * sc, base}, {x, base - 260 * sc}, ink);
                        Color rune = d.atmos == 0 ? Color{230, 60, 220, 255} : d.atmos == 2 ? Color{255, 70, 50, 255} : Color{200, 200, 210, 255};
                        float pulse = 0.5f + 0.5f * sinf(t * 1.6f + wx);
                        for (int k = 0; k < 4; k++) DrawRectangle((int)x - 2, (int)(base - 60 * sc - k * 44 * sc), 4, 10, Fade(rune, 0.3f + 0.6f * pulse));
                    }
                    break;
            }
        });
    }
}

// The run's atmosphere in two passes. The GRADE (fx=false) is drawn onto the backdrop, before the figures, so the
// party keeps its true colours; the FX (fx=true) are weather, spores and localised glows drawn after the ink pass.
static void DrawLocationTint(Game& g, bool fx) {
    auto& d = g.dungeon;
    float t = g.time;
    float seed = (float)(d.visSeed % 997);
    int v = d.atmos;
    auto tint = [&](Color c) { if (!fx) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, c); };
    auto lightWash = [&](Color c) { if (!fx) { BeginBlendMode(BLEND_ADDITIVE); DrawRectangle(0, 0, SCREEN_W, SCREEN_H, c); EndBlendMode(); } };
    auto glows = [&](auto fn) { if (fx) { BeginBlendMode(BLEND_ADDITIVE); fn(); EndBlendMode(); } };
    switch (d.loc) {
        case Location::Cave:
            if (v == 0) { // pitch black: a tight searchlight round the party, ink beyond it
                if (fx) {
                    // centered toward the enemy side, not the party - the back rank (pos 3, drawn out at
                    // x=145) is ~545px from this center, which used to fall inside the very first
                    // graduated ring (old base 300), so even the lightest darkening already dimmed her
                    // and the figure's own already-dark ink shading finished the job. Pushed the whole
                    // falloff out by 280 so every rank - and the enemy formation on the other side - sits
                    // inside the fully-lit radius, and only the far background/corners still go dark.
                    for (int i = 0; i < 8; i++) DrawRing({690, 390}, 580 + i * 30, 620 + i * 30, 0, 360, 48, Fade(Color{0, 0, 0, 255}, 0.14f + i * 0.06f));
                    DrawRing({690, 390}, 820, 1600, 0, 360, 64, BLACK);
                }
            } else if (v == 1) { // bloom: cyan and violet bleeding off the walls
                lightWash(Color{20, 90, 110, 34});
                glows([&] { for (int k = 0; k < 9; k++) Glow({fmodf(k * 173.0f + seed * 31, 1280.0f), 120 + fmodf(k * 89.0f, 300.0f)}, 150, k % 2 ? Color{160, 60, 230, 40} : Color{40, 210, 230, 44}); });
            } else { // silt storm: a grey-brown haze full of drifting dirt
                tint(Color{92, 84, 70, 70});
                if (fx) for (int k = 0; k < 90; k++) {
                    float px = fmodf(k * 67.0f + t * (30 + k % 9 * 5), 1300.0f) - 10, py = fmodf(k * 41.0f + sinf(t + k) * 14, 520.0f) + 50;
                    DrawRectangle((int)px, (int)py, 3, 2, Color{132, 116, 92, 150});
                }
            }
            break;
        case Location::Island:
            if (v == 0) { // downpour: charcoal grade, slanted rain, and lightning
                tint(Color{20, 24, 30, 60});
                if (fx) {
                    for (int k = 0; k < 140; k++) {
                        float px = fmodf(k * 47.0f + t * 180, 1400.0f) - 60, py = fmodf(k * 31.0f + t * 700 + k * 13, 760.0f) - 20;
                        DrawLineEx({px, py}, {px - 8, py + 22}, 1.5f, Color{170, 190, 210, 90});
                    }
                    float c = fmodf(t + seed * 0.013f, 9.0f);
                    if (c < 0.22f) { BeginBlendMode(BLEND_ADDITIVE); DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{190, 210, 255, (unsigned char)(120 * (1 - c / 0.22f))}); EndBlendMode(); }
                }
            } else if (v == 1) { // toxic fog: yellow-green rolling over the lower half
                tint(Color{70, 80, 30, 36});
                if (fx) for (int k = 0; k < 7; k++) {
                    float x = fmodf(k * 230.0f + t * 12, 1500.0f) - 150;
                    DrawEllipse((int)x, 470 + (k % 3) * 30, 260, 70, Color{150, 160, 60, 34});
                }
            } else { // eldritch sunset: a blood-crimson sky and amber rim light
                if (!fx) { DrawVGradient({0, 0, (float)SCREEN_W, 380}, Fade(Color{150, 20, 20, 255}, 0.36f), Fade(Color{60, 10, 20, 255}, 0.0f)); }
                lightWash(Color{120, 60, 10, 26});
            }
            break;
        case Location::Weeds:
            if (v == 0) { // abyssal current: indigo water, marine rot streaming sideways
                tint(Color{18, 24, 74, 70});
                if (fx) for (int k = 0; k < 70; k++) {
                    float px = fmodf(k * 89.0f + t * (120 + k % 5 * 30), 1400.0f) - 60, py = 70 + fmodf(k * 53.0f, 520.0f) + sinf(t * 2 + k) * 6;
                    DrawRectangle((int)px, (int)py, 8 + k % 4 * 3, 2, Color{120, 110, 130, 110});
                }
            } else if (v == 1) { // fungal rot: lime murk with pulsing spores
                tint(Color{40, 74, 20, 60});
                if (fx) for (int k = 0; k < 30; k++) {
                    float px = fmodf(k * 121.0f + sinf(t * 0.5f + k) * 30, 1280.0f), py = 60 + fmodf(k * 67.0f + t * 6, 520.0f);
                    float pulse = 0.5f + 0.5f * sinf(t * 2.2f + k);
                    Glow({px, py}, 16 + pulse * 10, Color{170, 255, 90, (unsigned char)(40 + 60 * pulse)});
                }
            } else { // sanguine tide: burgundy water and crimson rim light
                tint(Color{92, 14, 26, 70});
                lightWash(Color{110, 20, 20, 24});
            }
            break;
        default: // Atlantis
            if (v == 0) { // cosmic void: magenta and violet energy over cold blue light
                tint(Color{30, 14, 66, 66});
                lightWash(Color{40, 60, 140, 20});
                if (fx) for (int k = 0; k < 12; k++) {
                    float px = fmodf(k * 151.0f + t * 6, 1280.0f), py = fmodf(k * 83.0f + t * 3, 480.0f) + 40, a = 40 + 30 * sinf(t * 1.3f + k);
                    DrawRing({px, py}, 5, 6.5f, 0, 360, 6, Color{230, 90, 230, (unsigned char)std::max(0.0f, a)});
                }
            } else if (v == 1) { // drowned eclipse: drained to slate, gold only where light lands
                tint(Color{110, 114, 122, 100});
                glows([&] { Glow({640, 60}, 300, Color{255, 200, 90, 60}); });
            } else { // blood moon: red light bleeding up from vents below
                tint(Color{60, 6, 10, 56});
                glows([&] {
                    for (int k = 0; k < 6; k++) DrawTri({150.0f + k * 210, 560}, {230.0f + k * 210, 560}, {190.0f + k * 210 + sinf(t + k) * 20, 140}, Color{140, 20, 20, 16});
                    Glow({640, 600}, 700, Color{200, 30, 30, 50});
                });
            }
            break;
    }
    if (!fx) return;
    // a soft vignette closes the scene in
    for (int i = 0; i < 6; i++) DrawRing({640, 360}, 540 + i * 60, 580 + i * 60, 0, 360, 64, Fade(BLACK, 0.05f + i * 0.035f));
    if (d.atmos >= 0) {
        const char* nm = AtmosphereName(d.loc, v);
        Txt(nm, 20, 62, 14, Fade(Pal::Paper, 0.55f));
    }
}static void DrawTopBar(Game& g) {
    auto& d = g.dungeon;
    DrawVGradient({0, 0, (float)SCREEN_W, 58}, Color{10, 18, 24, 240}, Color{16, 28, 36, 220});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    TxtShadow(TextFormat("%s  -  %s (Lv %d)", LocationName(d.loc), CAVE_TIER_NAME[d.tier], CAVE_TIER_LEVEL[d.tier]), 20, 15, 22, Pal::Brass, true);
    if (!d.chart.rooms.empty()) {
        int visited = 0; for (auto& r : d.chart.rooms) visited += r.visited;
        Txt(TextFormat("%s  %d/%d  (Tab)", ObjectiveName(d.objective), visited, (int)d.chart.rooms.size()), 350, 22, 13, ObjectiveMet(g) ? Pal::Good : Color{190, 200, 196, 255});
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

// What a found item is called, for the "Found: ..." label.
static std::string ItemName(const InvItem& it) {
    switch (it.kind) {
        case ItemKind::Battery: return "A battery";
        case ItemKind::Bandage: return "A bandage";
        case ItemKind::Key: return "A key";
        default: return (it.relicId >= 0 && it.relicId < (int)Relics().size()) ? Relics()[it.relicId].name : "A relic";
    }
}

// The locked-chest prompt, or what was found lying among the wreckage: take it, leave it, or -- if the
// pack is already full -- clear a slot first by discarding something (click it) before taking the new one.
static void DrawFoundItemPanel(Game& g, Rectangle main) {
    auto& d = g.dungeon;
    Rectangle p{main.x, main.y + main.height + 14, main.width, 158};
    if (d.roomIsChest && !d.chestOpened) {
        Panel(p, Color{224, 214, 190, 255});
        bool hasKey = false;
        for (auto& it : d.inventory) if (it.kind == ItemKind::Key) hasKey = true;
        DrawTextCenteredBold("A locked chest, bound in iron", p.x + p.width / 2, p.y + 14, 22, Pal::BrassDk);
        DrawTextCentered(hasKey ? "A key from your pack fits the lock." : "You have no key. It stays shut.",
                         p.x + p.width / 2, p.y + 46, 16, Pal::Ink);
        if (Button({p.x + p.width / 2 - 140, p.y + 90, 280, 44}, "Open it", hasKey)) {
            for (auto it = d.inventory.begin(); it != d.inventory.end(); ++it)
                if (it->kind == ItemKind::Key) { d.inventory.erase(it); break; }
            d.chestOpened = true;
            d.roomGold = (int)(Roll(40, 70) * LootMult(g));
            d.lootGold += d.roomGold;
            if (Chance(60)) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; }
            Toast(g, TextFormat("The chest creaks open: +%d gold.", d.roomGold));
        }
        return;
    }
    if (!d.pendingItem) return;
    Panel(p, Color{224, 214, 190, 255});
    bool full = (int)d.inventory.size() >= InvCapacity(g);
    Vector2 ic{p.x + 50, p.y + 50};
    DrawItemIcon(d.pendingItemVal.kind, d.pendingItemVal.relicId, ic, 44);
    TxtBold(("Found: " + ItemName(d.pendingItemVal)).c_str(), p.x + 92, p.y + 16, 19, Pal::Ink);
    if (!full) {
        if (Button({p.x + 92, p.y + 52, 150, 42}, "Take it")) { d.inventory.push_back(d.pendingItemVal); d.pendingItem = false; }
        if (Button({p.x + 254, p.y + 52, 150, 42}, "Leave it")) d.pendingItem = false;
    } else {
        Txt(TextFormat("Your pack is full (%d/%d). Click something below to leave it behind, or leave the new find.", InvCapacity(g), InvCapacity(g)), p.x + 92, p.y + 50, 15, Pal::Bad);
        if (Button({p.x + 92, p.y + 78, 150, 36}, "Leave the find")) d.pendingItem = false;
        for (int i = 0; i < (int)d.inventory.size(); i++) {
            Vector2 sc{p.x + 300.0f + i * 52, p.y + 96};
            Rectangle sr{sc.x - 24, sc.y - 24, 48, 48};
            bool hov = CheckCollisionPointRec(GetMousePosition(), sr);
            if (hov) DrawRectangleRounded(sr, 0.3f, 6, Fade(Pal::Bad, 0.25f));
            DrawItemIcon(d.inventory[i].kind, d.inventory[i].relicId, sc, 40);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                d.inventory.erase(d.inventory.begin() + i);
                d.inventory.push_back(d.pendingItemVal);
                d.pendingItem = false;
                Toast(g, "Left it behind to make room.");
                break;
            }
        }
    }
}

// The pack: a row of carried items, always visible once the expedition starts. Click one to use it --
// a bandage or a battery asks which hero (or to burn it on the spot), a relic asks which hero to fit it
// to. Click the same slot again, or elsewhere, to cancel.
static void DrawInventoryBar(Game& g) {
    auto& d = g.dungeon;
    const float SZ = 46, GAP = 8, x0 = 20, y0 = SCREEN_H - 66.0f;
    for (int i = 0; i < InvCapacity(g); i++) {
        Rectangle r{x0 + i * (SZ + GAP), y0, SZ, SZ};
        bool has = i < (int)d.inventory.size();
        DrawRectangleRounded(r, 0.25f, 6, has ? Color{40, 46, 44, 235} : Color{20, 24, 24, 160});
        DrawRectangleRoundedLinesEx(r, 0.25f, 6, d.invSelected == i ? 2.5f : 1.5f, d.invSelected == i ? Pal::Brass : Color{80, 80, 70, 200});
        if (has) {
            Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
            DrawItemIcon(d.inventory[i].kind, d.inventory[i].relicId, c, 36);
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) d.invSelected = d.invSelected == i ? -1 : i;
            if (hov) {
                std::string name = ItemName(d.inventory[i]);
                float w = (float)MeasureTxt(name, 15, true);
                Rectangle lr{r.x + r.width / 2 - w / 2 - 8, r.y - 30, w + 16, 24};
                DrawRectangleRounded(lr, 0.4f, 6, Color{12, 18, 22, 230});
                TxtBold(name, lr.x + 8, lr.y + 4, 15, Pal::Paper);
            }
        }
    }
    if (d.invSelected < 0 || d.invSelected >= (int)d.inventory.size()) return;
    // a compact menu of who to use it on (or, for a battery, to burn it on the spot)
    const InvItem& it = d.inventory[d.invSelected];
    Rectangle m{x0, y0 - 190, 280, 176};
    Panel(m, Color{230, 222, 200, 255});
    TxtBold(("Use: " + ItemName(it)).c_str(), m.x + 14, m.y + 10, 17, Pal::Ink);
    if (it.kind == ItemKind::Battery) {
        if (Button({m.x + 14, m.y + 44, 252, 40}, TextFormat("Burn it now (+40 light, at %.0f)", d.light), d.light < 100)) {
            d.light = std::min(100.0f, d.light + 40);
            d.inventory.erase(d.inventory.begin() + d.invSelected);
            d.invSelected = -1;
            Toast(g, "The lamp flares brighter.");
        }
        Txt("A carried battery, separate from the ones stowed at home.", m.x + 14, m.y + 92, 13, Pal::BrassDk);
    } else if (it.kind == ItemKind::Key) {
        Txt("Used automatically on a locked chest, if you're carrying one when you reach it.", m.x + 14, m.y + 44, 15, Pal::Ink);
    } else {
        int row = 0;
        for (int p = 0; p < PARTY_SIZE; p++) {
            Hero* h = PartyAt(g, p);
            if (!h) continue;
            Rectangle br{m.x + 14, m.y + 40.0f + row * 34, 252, 30};
            bool canBandage = it.kind == ItemKind::Bandage && h->hp < GetStats(*h).maxHp;
            std::string whyNot;
            bool canRelic = it.kind == ItemKind::Relic && CanEquipRelic(*h, it.relicId, &whyNot);
            const char* label = it.kind == ItemKind::Bandage ? TextFormat("%s  (%d/%d HP)", h->name.c_str(), h->hp, GetStats(*h).maxHp)
                                                              : TextFormat("%s  (%s)", h->name.c_str(), canRelic ? "can carry it" : whyNot.c_str());
            if (Button(br, label, canBandage || canRelic, 14)) {
                if (it.kind == ItemKind::Bandage) {
                    int heal = std::max(1, GetStats(*h).maxHp * 30 / 100);
                    h->hp = std::min(GetStats(*h).maxHp, h->hp + heal);
                    Float(g, HeroRect(p), TextFormat("+%d", heal), Pal::Good);
                    Toast(g, TextFormat("%s is patched up.", h->name.c_str()));
                } else {
                    int slot = h->relics[0] < 0 ? 0 : 1;
                    h->relics[slot] = it.relicId;
                    Toast(g, TextFormat("%s fits the %s.", h->name.c_str(), Relics()[it.relicId].name.c_str()));
                }
                d.inventory.erase(d.inventory.begin() + d.invSelected);
                d.invSelected = -1;
                break; // `it` (a reference into the vector) is no longer valid after the erase
            }
            row++;
        }
    }
    if (CheckCollisionPointRec(GetMousePosition(), m) == false && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !CheckCollisionPointRec(GetMousePosition(), {x0 + d.invSelected * (SZ + GAP), y0, SZ, SZ}))
        d.invSelected = -1;
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
    cfx::Update(dt);
    if (d.phase == DPhase::Corridor) d.corridorT += dt;
    d.batteryT = std::max(0.0f, d.batteryT - dt);
    if (d.batteryT <= 0.7f) d.lightShown += std::clamp(d.light - d.lightShown, -40 * dt, 40 * dt); // light eases to its new level
    float k = d.shake * 22;
    gShake = {sinf(g.time * 70) * k, cosf(g.time * 57) * k * 0.6f};
}

// ---------------------------------------------------------------- the sonar scope
// The chart, read off the Nautilus's sonar: a round phosphor scope in a brass bezel. Rooms are ink glyphs, corridors
// faint dotted echoes, the party a bright blip; rooms not yet scouted are a soft "?" echo. A sweep passes every two
// seconds and brightens what it crosses. The phosphor follows the flashlight: as the light fails, the scope dims and
// static crackles across it. Returns the room clicked (only rooms next to the party, and only if interactive).
static int DrawSonarScope(Game& g, Vector2 c, float R, bool interactive) {
    auto& d = g.dungeon;
    auto& ch = d.chart;
    float t = g.time, L = std::clamp(d.lightShown / 100.0f, 0.0f, 1.0f), phos = 0.35f + 0.65f * L;
    int W = 1, H = 1;
    for (auto& r : ch.rooms) { W = std::max(W, r.gx + 1); H = std::max(H, r.gy + 1); }
    float span = R * 1.45f, cell = std::min(span / std::max(1, W - 1), span * 0.75f / std::max(1, H - 1));
    auto pos = [&](int r) { return Vector2{c.x + (ch.rooms[r].gx - (W - 1) * 0.5f) * cell, c.y + (ch.rooms[r].gy - (H - 1) * 0.5f) * cell}; };
    // the bezel and the glass
    DrawCircleV({c.x + 5, c.y + 7}, R + 26, Fade(BLACK, 0.5f));
    DrawCircleV(c, R + 26, Pal::BrassDk);
    DrawRing(c, R + 6, R + 22, 0, 360, 72, Pal::Brass);
    for (int k = 0; k < 16; k++) { float a = k * PI / 8; DrawCircleV({c.x + cosf(a) * (R + 14), c.y + sinf(a) * (R + 14)}, 3, Pal::BrassDk); }
    DrawCircleV(c, R + 4, Color{4, 18, 10, 255});
    Color ph{(unsigned char)(70 + 90 * phos), (unsigned char)(170 + 85 * phos), (unsigned char)(100 + 60 * phos), 255};
    for (int k = 1; k <= 3; k++) DrawRing(c, R * k / 3.0f - 0.8f, R * k / 3.0f + 0.8f, 0, 360, 64, Fade(ph, 0.12f * phos));
    DrawLineEx({c.x - R, c.y}, {c.x + R, c.y}, 1, Fade(ph, 0.08f));
    DrawLineEx({c.x, c.y - R}, {c.x, c.y + R}, 1, Fade(ph, 0.08f));
    float sweep = fmodf(t / 2.0f, 1.0f) * 2 * PI;   // a sweep every two seconds
    for (int k = 0; k < 14; k++) DrawCircleSector(c, R, (sweep * RAD2DEG) - (k + 1) * 4, (sweep * RAD2DEG) - k * 4, 3, Fade(ph, 0.1f * phos * (1 - k / 14.0f)));
    DrawLineEx(c, {c.x + cosf(sweep) * R, c.y + sinf(sweep) * R}, 2, Fade(ph, 0.8f * phos));
    auto lit = [&](Vector2 p) { // how freshly the sweep has passed over a point
        float a = atan2f(p.y - c.y, p.x - c.x);
        if (a < 0) a += 2 * PI;
        float since = fmodf(sweep - a + 4 * PI, 2 * PI);
        return 1.0f + 0.9f * std::max(0.0f, 1 - since / 1.2f);
    };
    // corridors: faint dotted echoes, with a bigger dot for each stretch
    for (int e = 0; e < (int)ch.edges.size(); e++) {
        const ChartEdge& ed = ch.edges[e];
        if (!ch.rooms[ed.a].known || !ch.rooms[ed.b].known) continue;
        Vector2 a = pos(ed.a), b = pos(ed.b);
        bool walked = ed.walked > 0;
        for (int k = 0; k <= 12; k++) { Vector2 p{a.x + (b.x - a.x) * k / 12, a.y + (b.y - a.y) * k / 12}; DrawCircleV(p, 1.2f, Fade(ph, (walked ? 0.45f : 0.2f) * phos * lit(p))); }
        for (int s = 0; s < (int)ed.segs.size(); s++) {
            float u = (s + 0.5f) / ed.segs.size();
            Vector2 p{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
            DrawCircleV(p, 2.6f, Fade(ph, (walked ? 0.6f : 0.35f) * phos * lit(p)));
        }
    }
    // rooms: ink glyphs
    Vector2 m = GetMousePosition();
    int hovered = -1;
    std::vector<int> nb = ch.Neighbours(d.curRoom);
    for (int r = 0; r < (int)ch.rooms.size(); r++) {
        const ChartRoom& room = ch.rooms[r];
        if (!room.known) continue;
        Vector2 p = pos(r);
        float a = phos * lit(p) * (room.cleared ? 0.55f : 1.0f);
        float rr = cell * 0.2f;
        bool adj = std::find(nb.begin(), nb.end(), r) != nb.end() && d.phase == DPhase::Corridor;
        if (interactive && adj && CheckCollisionPointCircle(m, p, rr + 10)) hovered = r;
        DrawRectangleRounded({p.x - rr, p.y - rr, rr * 2, rr * 2}, 0.3f, 6, Fade(Color{6, 30, 16, 255}, 0.95f));
        DrawRectangleRoundedLinesEx({p.x - rr, p.y - rr, rr * 2, rr * 2}, 0.3f, 6, hovered == r ? 3.0f : 1.5f,
                                    Fade(hovered == r ? Color{220, 255, 220, 255} : ph, adj ? std::min(1.0f, a * 1.3f) : a * 0.8f));
        Color gc = Fade(ph, std::min(1.0f, a));
        float s = rr * 0.6f;
        if (!room.scouted) DrawTextCenteredBold("?", p.x, p.y - s, (int)(s * 2), Fade(ph, a * 0.7f));
        else switch (room.type) {
            case RoomType::Fight: DrawLineEx({p.x - s, p.y - s}, {p.x + s, p.y + s}, 2.5f, gc); DrawLineEx({p.x + s, p.y - s}, {p.x - s, p.y + s}, 2.5f, gc); break;   // crossed blades
            case RoomType::Treasure: DrawRectangleLinesEx({p.x - s, p.y - s * 0.5f, s * 2, s * 1.3f}, 2, gc); DrawLineEx({p.x - s, p.y}, {p.x + s, p.y}, 2, gc); break; // a chest
            case RoomType::Boss: DrawRing(p, s * 0.7f, s * 1.05f, 0, 360, 20, gc); DrawCircleV({p.x - s * 0.35f, p.y - s * 0.1f}, s * 0.18f, gc); DrawCircleV({p.x + s * 0.35f, p.y - s * 0.1f}, s * 0.18f, gc); break; // a skull
            case RoomType::Entrance: DrawTri({p.x - s * 0.6f, p.y - s}, {p.x - s * 0.6f, p.y + s}, {p.x + s, p.y}, gc); break;   // the way in
            case RoomType::Curio: DrawPoly(p, 4, s, 45, gc); DrawCircleV(p, s * 0.3f, Color{6, 30, 16, 255}); break;                // a diamond
            case RoomType::Rest: DrawTri({p.x - s, p.y + s * 0.7f}, {p.x + s, p.y + s * 0.7f}, {p.x, p.y - s}, gc); DrawLineEx({p.x, p.y - s}, {p.x, p.y + s * 0.7f}, 2, Color{6, 30, 16, 255}); break; // a tent
            case RoomType::Empty: DrawRing(p, s * 0.25f, s * 0.45f, 0, 360, 12, gc); break;                                           // a quiet junction
            default: DrawRectangleLinesEx({p.x - s * 0.3f, p.y - s, s * 0.6f, s * 2}, 2, gc); DrawLineEx({p.x - s * 0.8f, p.y - s}, {p.x + s * 0.8f, p.y - s}, 2, gc); break; // Shrine: a pillar
        }
        if (room.cleared && room.type != RoomType::Entrance) DrawLineEx({p.x + rr * 0.3f, p.y + rr * 0.95f}, {p.x + rr, p.y + rr * 0.35f}, 2, Fade(ph, 0.8f));
    }
    // the party: a bright blip, where it is (or how far along the corridor)
    Vector2 blip = pos(d.curRoom);
    if (d.walkEdge >= 0 && d.walkDest >= 0) {
        float u = (d.walkSeg + std::min(1.0f, d.walkT / 1.2f)) / std::max(1, d.walkSegs);
        Vector2 b = pos(d.walkDest);
        blip = {blip.x + (b.x - blip.x) * u, blip.y + (b.y - blip.y) * u};
    }
    float pulse = 0.6f + 0.4f * sinf(t * 6);
    DrawCircleV(blip, 6 + 3 * pulse, Fade(Color{230, 255, 230, 255}, 0.25f));
    DrawCircleV(blip, 5, Color{230, 255, 230, 255});
    // static as the light fails
    int crackle = (int)((1 - L) * 60);
    for (int k = 0; k < crackle; k++) {
        float a = GetRandomValue(0, 628) / 100.0f, rr = R * sqrtf(GetRandomValue(0, 1000) / 1000.0f);
        DrawCircleV({c.x + cosf(a) * rr, c.y + sinf(a) * rr}, 1, Fade(ph, 0.5f));
    }
    if (L < 0.4f && fmodf(t * 7, 1.0f) < 0.15f) {
        float y = c.y + (GetRandomValue(-100, 100) / 100.0f) * R * 0.8f, hw = sqrtf(std::max(0.0f, R * R - (y - c.y) * (y - c.y)));
        DrawLineEx({c.x - hw, y}, {c.x + hw, y}, 2, Fade(ph, 0.35f));
    }
    DrawCircleSector(c, R, 200, 240, 12, Fade(WHITE, 0.04f)); // a glint on the glass
    if (hovered >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return hovered;
    return -1;
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
        float speed = 240 * std::min(1.0f, d.walkT / 0.25f) * std::min(1.0f, std::max(0.0f, (1.2f - d.walkT) / 0.25f));
        d.scroll += speed * dt;
        if (d.walkT >= 1.2f) ResolveSegment(g);
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
    {   // the light rig: the flashlight is the key; the location's palette, its current, and ink flecks fixed per run
        SetSceneLight(LocationLight(d.loc, d.lightShown / 100.0f));
        SetInkLook(&LocationPalette(d.loc), 0.32f, d.visSeed);
        const Vector2 CURRENT[LOCATION_COUNT] = {{-40, 0}, {-90, -10}, {-70, -20}, {-30, 10}};
        rig::SetCurrent({CURRENT[(int)d.loc].x * (1 + 0.5f * sinf(g.time * 0.4f)), CURRENT[(int)d.loc].y});
    }
    DrawCaveLayers(g);
    DrawRegionFloor(g);
    DrawSeededSilhouettes(g);
    DrawGroundClutter(g);
    DrawPathProps(g);
    DrawCaveLighting(g);
    DrawLightShafts(g);
    DrawLocationTint(g, false); // the colour grade is baked into the backdrop, under the figures
    DrawUnitFigures(g);   // after the lightmap: characters keep their own colours instead of being multiplied toward black
    DrawProjectiles(g);
    cfx::DrawWorld();     // ink splashes and ripples, inked with the stage
    DrawCaveForeground(g);
    InkPass(1.0f, 1.0f);
    DrawDriftingSpecks(g);
    DrawLocationTint(g, true);
    DrawSparks(g);
    {   // the camera: it leans in on an attacker's windup, snaps back when the blow lands, shakes with it, drifts otherwise
        auto& pa = d.pending;
        if (pa.active && pa.t < pa.impact) {
            Rectangle ar = pa.hero ? HeroRect(std::max(0, PartyPos(g, pa.id))) : EnemyRect(g, std::max(0, EnemyPos(g, pa.id)));
            cfx::Focus(BodyOf(ar), true);
        } else cfx::Focus({640, 360}, false);
        SceneCamera(cfx::FocusPoint(), cfx::Zoom(), cfx::Offset());
    }
    cfx::DrawOverlay();
    if (d.phase == DPhase::Combat) DrawRoundMedallion(g);
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
    if (d.phase != DPhase::Combat && d.phase != DPhase::Walking) DrawInventoryBar(g);
    if (IsKeyPressed(KEY_TAB)) d.scopeOpen = !d.scopeOpen;
    if (d.phase == DPhase::Walking || d.phase == DPhase::Combat) {
        if (d.scopeOpen) { DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.45f)); DrawSonarScope(g, {640, 340}, 240, false); DrawTextCentered("Tab to close the scope", 640, 640, 16, Pal::Paper); }
        else if (d.phase == DPhase::Walking) DrawSonarScope(g, {1170, 590}, 78, false);
    }

    if (!d.log.empty()) {
        DrawRectangleRounded({380, 66, 520, 20.0f * d.log.size() + 14}, 0.1f, 6, Color{8, 16, 22, 180});
        for (size_t i = 0; i < d.log.size(); i++)
            DrawTextCentered(d.log[i], 640, 73 + i * 20.0f, 16, i + 1 == d.log.size() ? Pal::Paper : Color{176, 190, 190, 255});
    }

    // ---------------- phase-specific UI
    switch (d.phase) {
        case DPhase::Walking: break;
        case DPhase::Corridor: {
            // The party takes a breath before the chart comes up; the scope then rises into place, and it only
            // answers clicks once it has settled (and while no battery is being swapped).
            const float DELAY = 1.4f, SLIDE = 0.45f;
            if (d.corridorT < DELAY && d.roomIndex >= 0) {
                DrawTextCentered("The crew catch their breath...", SCREEN_W / 2.0f, 610, 20, Color{200, 210, 210, 200});
                break;
            }
            float u = std::min(1.0f, std::max(0.0f, d.corridorT - (d.roomIndex >= 0 ? DELAY : 0)) / SLIDE), ease = 1 - (1 - u) * (1 - u) * (1 - u);
            bool ready = u >= 1 && d.batteryT <= 0;
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.5f * ease));
            int pick = DrawSonarScope(g, {470, 350 + (1 - ease) * 520}, 250, ready);
            Rectangle p{800, 90 + (1 - ease) * 560, 450, 470};
            Panel(p);
            DrawTextCenteredBold("The sonar chart", p.x + p.width / 2, p.y + 18, 28, Pal::Ink);
            TxtBold(TextFormat("Objective: %s", ObjectiveName(d.objective)), p.x + 26, p.y + 62, 18, Pal::BrassDk);
            DrawWrapped(ObjectiveText(d.objective), {p.x + 26, p.y + 86, p.width - 52, 40}, 15, Pal::Ink);
            bool met = ObjectiveMet(g);
            int visited = 0; for (auto& r : d.chart.rooms) visited += r.visited;
            Txt(TextFormat("Rooms visited %d / %d   %s", visited, (int)d.chart.rooms.size(), met ? "- objective complete!" : ""), p.x + 26, p.y + 126, 15, met ? Pal::Good : Pal::BrassDk);
            DrawWrapped("Click a room next to the party to go there. Every stretch of corridor drains light (half as much on a passage you've walked). "
                        "The short way to the boss runs through fights; treasure, curios and rest lie off it.",
                        {p.x + 26, p.y + 156, p.width - 52, 100}, 15, Pal::Ink);
            int drain = LightDrainPerRoom(g);
            Txt(TextFormat("Light: %d%%   (about -%d a corridor)", (int)d.light, (int)(StretchDrain(g) * (ChartParamsFor(d.tier).segMin + ChartParamsFor(d.tier).segMax) / 2 + 0.5f)), p.x + 26, p.y + 262, 16, Pal::BrassDk);
            (void)drain;
            const char* swap = d.batteryT > 0 ? "Swapping the battery..." : TextFormat("Swap in a battery  (+40 light)   [%d left]", g.batteries);
            if (Button({p.x + 25, p.y + 292, 400, 42}, swap, ready && g.batteries > 0 && d.light < 100)) {
                g.batteries--;
                d.light = std::min(100.0f, d.light + 40);
                d.batteryT = 1.4f;
            }
            {   // a Diver can scout ahead once per room (Mark the Prey, out of combat): what lies two rooms on shows up
                bool diver = false;
                for (int id : g.party) if (Hero* h = FindHero(g, id)) diver |= h->cls == HeroClass::Diver;
                static int scoutedAt = -1;
                if (diver && scoutedAt != d.curRoom * 1000 + d.roomIndex && Button({p.x + 25, p.y + 344, 195, 42}, "Scout ahead (Diver)", ready)) { Reveal(g, 1); scoutedAt = d.curRoom * 1000 + d.roomIndex; PlayCue("hub.sonar", 0.6f); }
            }
            if (met && d.objective != Objective::Slay) {
                if (Button({p.x + 230, p.y + 344, 195, 42}, "Return triumphant", ready)) { d.objectiveDone = true; d.phase = DPhase::Victory; PlayCue("ui.confirm"); }
            }
            if (Button({p.x + 25, p.y + 396, 400, 42}, d.roomIndex < 0 ? "Turn back to the Nautilus" : "Retreat with the loot (+10 nerves)", ready)) {
                if (d.roomIndex < 0 && d.lootGold == 0) { g.scene = Scene::Hub; return; }
                d.phase = DPhase::Retreat;
            }
            if (pick >= 0) { BeginWalk(g, pick); PlayCue("ui.confirm", 0.7f); }
        } break;

        case DPhase::Event: {
            Rectangle main{340, 90, 600, 300};
            Panel(main);
            DrawTextCenteredBold(d.eventTitle.c_str(), main.x + main.width / 2, main.y + 20, 28, d.event == EventKind::Trap ? Pal::Bad : Pal::Ink);
            DrawWrapped(d.eventBody, {main.x + 36, main.y + 66, main.width - 72, 150}, 17, Pal::Ink);
            if (d.eventStage == 1) {
                if (d.pendingItem) DrawFoundItemPanel(g, main);
                if (Button({main.x + main.width / 2 - 140, main.y + main.height - 50, 280, 42}, d.eventAmbush ? "To arms!" : d.pendingItem ? "Move on (leave it)" : "Continue")) {
                    d.pendingItem = false;
                    ChooseEvent(g, 0);
                }
                break;
            }
            const char* A = "", *B = "";
            bool aOk = true;
            switch (d.event) {
                case EventKind::Curio: A = "Inspect it"; B = "Leave it"; break;
                case EventKind::Rest: A = "Make camp"; B = "Press on"; break;
                case EventKind::Shrine: A = "Pray"; B = "Leave it"; break;
                case EventKind::Blocked: A = g.batteries > 0 ? TextFormat("Clear it  (1 battery, %d left)", g.batteries) : "Clear it  (no batteries)"; B = "Turn back"; aOk = g.batteries > 0; break;
                default: A = "Continue"; B = nullptr; break;
            }
            if (Button({main.x + 40, main.y + main.height - 52, 250, 42}, A, aOk)) ChooseEvent(g, 0);
            if (B && Button({main.x + 310, main.y + main.height - 52, 250, 42}, B)) ChooseEvent(g, 1);
        } break;

        case DPhase::Treasure: {
            Rectangle main{340, 90, 600, 260};
            Panel(main);
            std::string body = d.roomIsChest ? "A chest, bound shut, sits half-buried in the silt."
                                             : TextFormat("A barnacled cache! +%d gold.%s", d.roomGold, d.light < 50 ? " The darkness made the find richer." : "");
            DrawTextCenteredBold("Treasure", main.x + main.width / 2, main.y + 20, 30, Pal::Brass);
            DrawWrapped(body, {main.x + 36, main.y + 66, main.width - 72, 130}, 17, Pal::Ink);
            DrawFoundItemPanel(g, main);
            bool blocked = d.pendingItem || (d.roomIsChest && !d.chestOpened);
            if (Button({main.x + main.width / 2 - 130, main.y + main.height - 46, 260, 42}, blocked ? "Move on (leave anything unclaimed)" : "Continue")) {
                d.pendingItem = false;
                if (!d.chart.rooms.empty()) d.chart.rooms[d.curRoom].cleared = !d.roomIsChest || d.chestOpened; // a locked chest waits for a key
                d.phase = DPhase::Corridor; d.corridorT = 0;
            }
        } break;

        case DPhase::RoomClear: {
            Rectangle main{340, 90, 600, 260};
            Panel(main);
            DrawTextCenteredBold("Room cleared", main.x + main.width / 2, main.y + 20, 30, Pal::Good);
            DrawWrapped(TextFormat("The room is quiet again. You gather %d gold from the debris.", d.roomGold),
                        {main.x + 36, main.y + 66, main.width - 72, 130}, 17, Pal::Ink);
            DrawFoundItemPanel(g, main);
            if (Button({main.x + main.width / 2 - 130, main.y + main.height - 46, 260, 42}, d.pendingItem ? "Move on (leave it)" : "Continue")) {
                d.pendingItem = false;
                if (d.walkEdge >= 0) ResumeWalk(g); // a hallway fight: on down the corridor
                else { d.phase = DPhase::Corridor; d.corridorT = 0; }
            }
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
                bool bossDown = d.chart.rooms.empty() || d.chart.rooms[d.chart.boss].cleared;
                body = bossDown ? TextFormat("%s is beaten. You bring home %d gold", LocationBossName(d.loc), d.lootGold)
                                : TextFormat("Objective complete: %s. You bring home %d gold", ObjectiveName(d.objective), d.lootGold);
                for (int r : d.lootRelics) body += ", a " + Relics()[r].name;
                body += ", and as a reward for finishing: a " + Relics()[d.rewardRelic].name + ".";
                if (d.objectiveDone) body += TextFormat("\nObjective bonus (%s): %s", ObjectiveName(d.objective), ObjectiveText(d.objective));
                body += TextFormat("\n\nSurvivors earn XP (%d base).", 3 + lvl * 3);
                if (d.tier + 1 < CAVE_TIERS && g.tierCleared[(int)d.loc] == d.tier)
                    body += TextFormat(" %s level %d (%s) is now open at the Helm.", LocationName(d.loc), CAVE_TIER_LEVEL[d.tier + 1], CAVE_TIER_NAME[d.tier + 1]);
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
            DrawHudPlate();
            DrawTurnStrip(g);
            Hero* h = actingHero >= 0 ? FindHero(g, actingHero) : nullptr;
            // the right-hand panel: the enemy under the mouse (or being targeted), else the sonar
            Rectangle right{900, 566, 350, 146};
            DrawRectangleRounded(right, 0.08f, 6, Color{20, 18, 16, 235});
            DrawRectangleRoundedLinesEx(right, 0.08f, 6, 2, Pal::BrassDk);
            int hovE = -1;
            for (int p = 0; p < (int)d.enemies.size(); p++) if (d.enemies[p].alive && CheckCollisionPointRec(GetMousePosition(), EnemyRect(g, p))) hovE = p;
            if (hovE < 0 && actingEnemy >= 0) hovE = EnemyPos(g, actingEnemy);
            if (hovE >= 0) {
                const Enemy& e = d.enemies[hovE];
                TxtBold(e.name, right.x + 14, right.y + 8, 18, Color{230, 120, 100, 255});
                Txt(TextFormat("HP %d / %d", e.hp, e.maxHp), right.x + 14, right.y + 32, 14, Pal::Paper);
                DrawRectangle((int)right.x + 110, (int)right.y + 36, 120, 6, Color{40, 10, 10, 255});
                DrawRectangle((int)right.x + 110, (int)right.y + 36, (int)(120.0f * e.hp / e.maxHp), 6, Color{190, 40, 36, 255});
                Txt(TextFormat("PROT %d%%   DODGE %d   SPD %d   ACC %d", e.prot, e.dodge, e.speed, e.acc), right.x + 14, right.y + 52, 13, Color{200, 190, 170, 255});
                std::string ab;
                for (auto& a : e.abilities) ab += (ab.empty() ? "" : ",  ") + a.name;
                DrawWrapped(ab, {right.x + 14, right.y + 72, right.width - 28, 40}, 13, Color{170, 160, 140, 255});
                std::string tags = StatusTags(e.st);
                if (!tags.empty()) TxtBold(tags, right.x + 14, right.y + 120, 12, Pal::Coral);
            } else {
                DrawSonarScope(g, {right.x + 66, right.y + 73}, 40, false);
                Txt(ObjectiveName(d.objective), right.x + 150, right.y + 22, 15, Pal::Brass);
                Txt(TextFormat("Light %d%%", (int)d.light), right.x + 150, right.y + 44, 14, Pal::Paper);
                Txt("Tab: the full scope", right.x + 150, right.y + 66, 13, Color{170, 160, 140, 255});
            }
            if (!h || d.pendingSkip || !d.turnStarted || d.pending.active) {
                const char* who = h ? h->name.c_str() : actingEnemy >= 0 && FindEnemy(g, actingEnemy) ? FindEnemy(g, actingEnemy)->name.c_str() : "...";
                DrawTextCentered(TextFormat("%s is acting", who), 460, 620, 24, Pal::Paper);
                break;
            }
            int pos = PartyPos(g, h->id);
            int heroId = h->id;
            Stats s = GetStats(*h);
            // the hero: portrait, name, stats
            Rectangle por{40, 570, 80, 96};
            DrawRectangleRec({por.x - 3, por.y - 3, por.width + 6, por.height + 6}, Pal::BrassDk);
            DrawRectangleRec(por, Color{30, 34, 36, 255});
            DrawPortrait(*h, por, g.time);
            TxtBold(h->name, 132, 568, 20, Pal::Brass);
            Txt(TextFormat("%s   Lv %d   rank %d", ClassName(h->cls), h->level, pos + 1), 132, 592, 13, Color{190, 176, 140, 255});
            DrawRectangle(132, 612, 150, 7, Color{40, 10, 10, 255}); DrawRectangle(132, 612, (int)(150.0f * h->hp / s.maxHp), 7, Color{190, 40, 36, 255});
            Txt(TextFormat("%d/%d", h->hp, s.maxHp), 288, 608, 13, Pal::Paper);
            DrawRectangle(132, 624, 150, 5, Color{30, 30, 40, 255}); DrawRectangle(132, 624, (int)(150.0f * h->stress / 100), 5, Color{200, 200, 230, 255});
            Txt(TextFormat("%d nerves", h->stress), 288, 620, 12, Color{200, 200, 230, 255});
            Txt(TextFormat("ACC %d   CRIT %d%%   DMG %d-%d", s.acc, 5 + RelicBundle(*h).critPct, s.dmgMin, s.dmgMax), 132, 640, 13, Color{200, 190, 170, 255});
            Txt(TextFormat("DODGE %d   PROT %d%%   SPD %d", s.dodge, s.prot, s.speed), 132, 658, 13, Color{200, 190, 170, 255});
            std::string tags = StatusTags(h->st);
            if (h->deathsDoor) tags += "DEATH'S DOOR ";
            if (h->rattled) tags += "RATTLED";
            if (!tags.empty()) TxtBold(tags, 40, 684, 12, Pal::Coral);
            // the skill bar
            const auto& abs = ClassAbilities(h->cls);
            int hoverAb = -1;
            for (int slot = 0; slot < LOADOUT_SIZE; slot++) {
                int i = h->loadout[slot];
                Rectangle b{400 + (slot % 2) * 240.0f, 572 + (slot / 2) * 48.0f, 232, 42};
                if (i < 0) {
                    DrawRectangleRoundedLinesEx(b, 0.25f, 6, 1, Color{90, 100, 100, 255});
                    DrawTextCentered("(empty slot)", b.x + b.width / 2, b.y + 13, 15, Color{90, 100, 100, 255});
                    continue;
                }
                bool usable = HeroCanUse(g, pos, abs[i]);
                if (i == d.selectedAbility) DrawRectangleRounded({b.x - 4, b.y - 4, b.width + 8, b.height + 8}, 0.3f, 6, Pal::Teal);
                if (CheckCollisionPointRec(GetMousePosition(), b)) hoverAb = i;
                if (Button(b, abs[i].name.c_str(), usable, 16)) {
                    if (abs[i].target == Target::Self || abs[i].target == Target::AllAllies) {
                        BeginHeroAction(g, heroId, i, pos);
                        return;
                    }
                    d.selectedAbility = i;
                }
            }
            if (Button({400, 672, 472, 34}, "Pass the turn", true, 14)) { Log(g, h->name + " holds position."); EndTurn(g); return; }
            int showAb = hoverAb >= 0 ? hoverAb : d.selectedAbility;
            if (showAb >= 0) { // what it does, on a plate just above the HUD
                const Ability& a = abs[showAb];
                std::string info = a.desc + "   [from ranks " + RankString(a.usableFrom);
                if (a.target == Target::Enemy) info += ", hits enemy ranks " + RankString(a.hits);
                info += "]";
                if (!(a.usableFrom & (1 << pos))) info += "  - can't be used from this rank";
                float w = (float)MeasureTxt(info, 15) + 24;
                DrawRectangleRounded({640 - w / 2, 496, w, 26}, 0.4f, 6, Color{10, 10, 12, 220});
                Txt(info, 640 - w / 2 + 12, 500, 15, Pal::Paper);
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) d.selectedAbility = -1;
            if (d.selectedAbility >= 0) {
                const Ability& a = abs[d.selectedAbility];
                for (int tp : ValidTargets(g, pos, a)) {
                    Rectangle tr = a.target == Target::Enemy ? EnemyRect(g, tp) : HeroRect(tp);
                    bool hov = CheckCollisionPointRec(GetMousePosition(), tr);
                    // a target ring on the ground, and a bracket around it
                    Vector2 foot{tr.x + tr.width / 2, tr.y + tr.height};
                    Color rc = a.target == Target::Enemy ? Color{230, 80, 60, 255} : Color{120, 230, 140, 255};
                    DrawEllipseLines((int)foot.x, (int)foot.y, tr.width * 0.55f, 12, Fade(rc, hov ? 1.0f : 0.6f));
                    DrawRectangleRoundedLinesEx({tr.x - 6, tr.y - 6, tr.width + 12, tr.height + 12}, 0.1f, 6, hov ? 3.0f : 1.5f, Fade(rc, hov ? 1.0f : 0.5f));
                    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        BeginHeroAction(g, heroId, d.selectedAbility, tp);
                        return;
                    }
                }
            }
        } break;
    }
}

// ============================================================ sprite sheet pages (developer tool)
// The expedition crew in each of the poses combat uses, then the creatures of the cave.
void DrawCrewSpritePage(float t) {
    TxtBold("Expedition crew: the poses their animations blend between", 30, 16, 24, Pal::Brass);
    struct Named { const char* name; Pose pose; float walk; };
    Pose melee, raise, hurt, stress, door, dodge;
    melee.reach = 1; melee.lean = 0.3f; melee.stride = 0.9f;
    raise.raise = 1; raise.lean = -0.1f;
    hurt.lean = -0.5f; hurt.crouch = 0.25f; hurt.headDown = -0.9f; hurt.backRaise = 0.55f; hurt.stride = -0.6f;
    stress.crouch = 0.3f; stress.headDown = 0.7f; stress.backRaise = 0.35f; stress.tremble = 1; stress.lean = -0.1f;
    door.crouch = 0.62f; door.headDown = 0.95f; door.lean = 0.18f; door.tremble = 0.4f;
    dodge.crouch = 0.35f; dodge.lean = -0.2f; dodge.stride = -0.5f;
    const Named poses[8] = {{"Ready", Pose{}, 0}, {"Walk", Pose{}, 1.2f}, {"Walk", Pose{}, 2.8f}, {"Strike", melee, 0},
                            {"Raise / cast", raise, 0}, {"Hit!", hurt, 0}, {"Dread", stress, 0}, {"Death's Door", door, 0}};
    // Twelve classes at the original scale would run off the bottom of the page, so this shrinks to fit.
    int n = (int)HeroClass::COUNT;
    float scale = n <= 4 ? 0.82f : 0.82f * 4 / n, rowH = (720.0f - 90) / n;
    for (int c = 0; c < n; c++) {
        Hero h;
        h.id = 3 + c * 5;
        h.cls = (HeroClass)c;
        float y = 90 + (c + 1) * rowH - rowH * 0.5f;
        TxtBold(ClassName(h.cls), 12, y - rowH * 0.42f, 15, ClassColor(h.cls));
        for (int k = 0; k < 8; k++) {
            float x = 90 + k * 158.0f;
            if (c == 0) TxtBold(poses[k].name, x - MeasureTxt(poses[k].name, 16, true) / 2.0f, 52, 16, Pal::Paper);
            DrawShadowBlob({x, y}, 26 * scale / 0.82f);
            DrawCrewFigureInked(h, {x, y}, scale, true, poses[k].walk, t, poses[k].pose);
        }
    }
    (void)dodge;
}

// One class filling the whole page: several of the painted rigs are still badly out of scale (a known,
// unfixed calibration bug - see chargen.cpp), so a shared tile with hard clipping either hid them entirely
// (BeginScissorMode doesn't reliably clip this draw path - a separate bug worth its own look) or cut them off.
// A full page and a small, generous scale is the reliable way to actually see one character at a time.
void DrawCrewGalleryPage(HeroClass cls, float t) {
    ClearBackground(Color{20, 22, 28, 255});
    TxtBold(ClassName(cls), 30, 20, 30, ClassColor(cls));
    Hero h; h.id = 3 + (int)cls * 5; h.cls = cls;
    Vector2 feet{SCREEN_W * 0.5f, SCREEN_H - 90.0f};
    DrawShadowBlob(feet, 40);
    DrawCrewFigureInked(h, feet, 0.55f, true, 0, t, Pose{});
}

void DrawCaveSpritePage(float t) {
    TxtBold("Creatures of the Cave", 30, 16, 24, Pal::Brass);
    const EnemyType types[4] = {EnemyType::SeaLouse, EnemyType::CaveShrimp, EnemyType::BrineWorm, EnemyType::Lobster};
    for (int i = 0; i < 4; i++) {
        Enemy e = MakeEnemy(types[i], 50 + i);
        float x = 170 + i * 310.0f, y = 520;
        Rectangle r = e.boss ? Rectangle{0, 0, 116, 210} : Rectangle{0, 0, 90, 130};
        Vector2 ff = FigureFeet();
        TxtBold(e.name.c_str(), x - MeasureTxt(e.name, 19, true) / 2.0f, 580, 19, Pal::Paper);
        DrawShadowBlob({x, y}, e.boss ? 90 : 60);
        BeginFigure();
        DrawEnemyFigure(e, {ff.x - r.width / 2, ff.y - r.height, r.width, r.height}, t);
        EndFigure({x, y});
    }
}

// Every creature of the four regions, on two sprite sheet pages.
void DrawBestiarySpritePage(int page, float t) {
    DrawVGradient({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Color{16, 18, 24, 255}, Color{6, 7, 10, 255});
    const int FIRST = (int)EnemyType::DysCrustacean, LAST = (int)EnemyType::COUNT;
    int total = LAST - FIRST, perPage = 8, from = FIRST + page * perPage;
    TxtBold(TextFormat("The bestiary (%d of 3)", page + 1), 30, 14, 24, Pal::Brass);
    for (int k = 0; k < perPage && from + k < LAST; k++) {
        Enemy e = MakeEnemy((EnemyType)(from + k), 90 + k);
        float x = 170 + (k % 4) * 310.0f, y = 330 + (k / 4) * 300.0f;
        Rectangle r = e.boss ? Rectangle{0, 0, 116, 210} : Rectangle{0, 0, 90, 130};
        Vector2 ff = FigureFeet();
        DrawShadowBlob({x, y}, e.boss ? 80 : 50);
        BeginFigure();
        DrawEnemyFigure(e, {ff.x - r.width / 2, ff.y - r.height, r.width, r.height}, t);
        EndFigure({x, y});
        const char* tier = e.tier == 2 ? "LEVEL BOSS" : e.tier == 1 ? "MINI-BOSS" : "";
        TxtBold(e.name, x - MeasureTxt(e.name, 17, true) / 2.0f, y + 12, 17, Pal::Paper);
        if (e.tier) Txt(tier, x - MeasureTxt(tier, 13) / 2.0f, y + 32, 13, Pal::Bad);
        std::string sk;
        for (auto& a : e.abilities) sk += (sk.empty() ? "" : ", ") + a.name;
        Txt(sk, x - MeasureTxt(sk, 11) / 2.0f, y + 48, 11, Color{170, 176, 170, 255});
    }
    (void)total;
}

// ---------------------------------------------------------------- figure sheets (depth.exe --figures / --silhouette)
// One figure in every state: idle, walking, the windup and the strike of its attack, a hit, and death. Drawn under
// its location's light rig on a plain ground, so the figure itself is what's judged.
void DrawFigureSheet(bool heroSheet, int index, float t) {
    static Game sg;
    Location loc = Location::Cave;
    if (!heroSheet) loc = index <= 7 ? Location::Cave : index <= 13 ? Location::Island : index <= 19 ? Location::Weeds : Location::Atlantis;
    SetSceneLight(LocationLight(loc, 0.85f));
    SetInkLook(&LocationPalette(loc), 0.3f, 1234);
    rig::SetCurrent({-40, 0});
    const Palette& pal = LocationPalette(loc);
    if (gSilhouette) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{226, 218, 198, 255});
    else {
        DrawVGradient({0, 0, (float)SCREEN_W, 560}, pal.base[2], pal.base[1]);
        DrawVGradient({0, 560, (float)SCREEN_W, 160}, pal.base[1], pal.base[0]);
        FogVeil(0.2f);
    }
    const char* LABEL[6] = {"idle", "walk", "windup", "strike", "hit", "death"};
    bool big = getenv("DEPTH_SHEET_BIG") != nullptr;   // a close-up: idle and strike, large, to judge the drawing itself
    float bigK = big ? (heroSheet ? 2.3f : 1.6f) : 1.0f;
    for (int i = 0; i < 6; i++) {
        if (big && i != 0 && i != 3) continue;
        float cx = big ? (i == 0 ? 330.0f : 900.0f) : 110 + i * 212.0f, fy = big ? 690.0f : 560.0f;
        if (!gSilhouette) DrawEllipse((int)cx, (int)fy, 70, 12, Fade(BLACK, 0.35f));
        sg.dungeon.anims.clear();
        if (heroSheet) {
            Hero h{};
            h.cls = (HeroClass)index; h.id = 50000 + index * 10 + i; h.outfit = -1;
            Anim kind = i == 2 || i == 3 ? Anim::Melee : i == 4 ? Anim::Hurt : Anim::None;
            float dur = kind == Anim::Melee ? 0.9f : 0.6f, u = i == 2 ? 0.24f : i == 3 ? 0.4f : 0.1f;
            if (kind != Anim::None) sg.dungeon.anims.push_back({true, h.id, kind, u * dur, dur});
            AnimFx fx = HeroAnimFx(sg, h);
            if (i == 5) { fx.pose.crouch = 1.0f; fx.pose.headDown = 1.0f; fx.pose.lean = 0.5f;
                          rig::Instance& in = rig::Get(h.id); if (in.reaction != rig::CL_DEATH) { in.reaction = rig::CL_DEATH; in.reactT = 0; } }
            DrawCrewFigureInked(h, {cx + fx.dx * 0.5f, fy}, 1.35f * bigK * 0.8f, true, i == 1 ? t * 9 : 0, t, fx.pose, fx.tint);
        } else {
            Enemy e = MakeEnemy((EnemyType)index, 60000 + index * 10 + i);
            Anim kind = i == 2 || i == 3 ? Anim::Melee : i == 4 || i == 5 ? Anim::Hurt : Anim::None;
            float dur = 0.9f, u = i == 2 ? 0.14f : i == 3 ? 0.32f : i == 4 ? 0.1f : 0.5f;
            if (i == 5) e.alive = false;
            if (kind != Anim::None) sg.dungeon.anims.push_back({false, e.uid, kind, u * dur, dur});
            AnimFx fx = EnemyAnimFx(sg, e);
            if (i == 5) fx.tint.a = 255;   // held at the moment of death, not faded out
            int span = std::max(1, e.span);
            float w = span >= 3 ? 374.0f : span == 2 ? 249.0f : e.boss ? 116.0f : 90.0f, hgt = span >= 3 ? 310.0f : span == 2 ? 245.0f : e.boss ? 210.0f : 130.0f;
            float k = span > 1 || e.boss ? std::min({1.0f, 120.0f / w, 300.0f / hgt}) : std::min({1.4f, 200.0f / w, 380.0f / hgt}); // big ones draw well past their rects
            k *= bigK; w *= k; hgt *= k;
            Vector2 feet{cx + fx.dx * 0.5f * k, fy + fx.dy}, ff = FigureFeet();
            rig::SetWorldOffset({feet.x - ff.x, feet.y - ff.y});
            int clip = i == 1 ? -1 : i == 2 || i == 3 ? rig::CL_SLASH : i == 4 ? rig::CL_HIT : i == 5 ? rig::CL_DEATH : -1;
            RigSetActing(clip, clip < 0 ? 0 : u * rig::GetClip(clip).dur * (i == 5 ? 3.0f : 1.0f));
            BeginFigure();
            DrawEnemyFigure(e, {ff.x - w / 2, ff.y - hgt, w, hgt}, t + (i == 1 ? 0.7f : 0));
            EndFigure(feet, fx.tint, fx.sx, fx.sy);
            RigSetActing(-1, 0);
        }
    }
    InkPass(1.0f, 1.0f);
    std::string title = (heroSheet ? std::string(ClassName((HeroClass)index)) : MakeEnemy((EnemyType)index, 1).name) + (gSilhouette ? "  (silhouette)" : "");
    DrawTextCenteredBold(title, SCREEN_W / 2.0f, 40, 30, gSilhouette ? Color{20, 16, 14, 255} : Pal::Paper);
    if (!big) for (int i = 0; i < 6; i++) DrawTextCentered(LABEL[i], 110 + i * 212.0f, 600, 20, gSilhouette ? Color{60, 50, 40, 255} : Color{200, 206, 200, 255});
}

// --shots: the chart's screens
void DebugChartWalk(Game& g, int dest) { BeginWalk(g, dest); g.dungeon.walkT = 0.3f; }
void DebugChartEvent(Game& g, int kind) { if (kind == 1) BeginEvent(g, EventKind::Curio); else OpenEvent(g, EventKind::Rest, "A place to rest", "A dry ledge above the water, out of the current. The crew could make camp here: bind wounds, eat, sleep in turns. Something may come in the night."); }
