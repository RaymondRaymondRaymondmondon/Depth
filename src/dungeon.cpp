// ============================================================================
//  DEPTH - the roguelike expedition: rooms, the flashlight, and combat.
// ============================================================================
#include "game.h"
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
        amount = (int)std::round(amount * StressMult(g) * (100 - GetStats(h).stressResist) / 100.0f * (100 - RelicBundle(h).stressGainPct) / 100.0f);
        if (amount <= 0) return;
    }
    int before = h.stress;
    h.stress = std::clamp(h.stress + amount, 0, 100);
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
                    Float(g, er, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Coral);
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
                        for (int fi = 0; fi < 2 && fi < (int)d.enemies.size(); fi++) {
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
    if (rank < 0 || n == 0 || !(a.fromRanks & (1 << rank))) return false;
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
            Float(g, hr, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Pal::Brass : Pal::Bad);
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
        if (a.summon >= 0 && (int)g.dungeon.enemies.size() < 4) {
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
    bool boss = d.rooms[d.roomIndex] == RoomType::Boss;
    d.roomGold = (int)(Roll(boss ? 30 : 10, boss ? 50 : 22) * LootMult(g)); // kept modest: gold should stay scarce
    d.roomRelic = -1;
    if (boss && Chance(50)) { d.roomRelic = Roll(0, (int)Relics().size() - 1); d.lootRelics.push_back(d.roomRelic); }
    if (d.miniFight) d.roomGold = d.roomGold * 8 / 5; // a mini-boss guards better loot
    d.lootGold += d.roomGold;
    d.pendingItem = !boss && Chance(d.miniFight ? 85 : 40); // something dropped among the wreckage, worth a look
    if (d.pendingItem) d.pendingItemVal = RollFoundItem();
    if (d.miniFight && Chance(35)) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; }
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

// ---------------------------------------------------------------- rooms
static void EnterNextRoom(Game& g) {
    auto& d = g.dungeon;
    d.roomIndex++;
    d.enemies.clear();
    d.log.clear();
    d.floats.clear();
    RoomType rt = d.rooms[d.roomIndex];
    if (rt == RoomType::Treasure) {
        d.roomIsChest = Chance(35); // sometimes it's locked, and only a carried key opens it
        d.chestOpened = false;
        d.roomGold = d.roomIsChest ? 0 : (int)(Roll(18, 36) * LootMult(g));
        d.roomRelic = -1;
        d.pendingItem = false;
        if (!d.roomIsChest) {
            d.lootGold += d.roomGold;
            // the relic here, if any, is loose -- carry it home in the inventory rather than an automatic find
            if (Chance(30)) { d.pendingItem = true; d.pendingItemVal = {ItemKind::Relic, Roll(0, (int)Relics().size() - 1)}; }
        }
        d.phase = DPhase::Treasure;
        return;
    }
    int level = CAVE_TIER_LEVEL[d.tier];
    auto pickFrom = [&](const std::vector<EnemyType>& pool) { return pool[Roll(0, (int)pool.size() - 1)]; };
    auto standards = LocationStandards(d.loc), supports = LocationSupports(d.loc), minis = LocationMinis(d.loc);
    d.miniFight = false;
    if (rt == RoomType::Boss) { // the location's level boss stands in front, with its own to back it up
        d.enemies.push_back(MakeEnemy(LocationLevelBoss(d.loc), d.nextUid++));
        int adds = level >= 5 ? 2 : 1;
        for (int i = 0; i < adds; i++) d.enemies.push_back(MakeEnemy(i == adds - 1 && Chance(50) ? pickFrom(supports) : pickFrom(standards), d.nextUid++));
        Log(g, std::string(LocationBossName(d.loc)) + " rises to meet you...");
    } else if (Chance(MiniBossChance(level))) { // a mini-boss: more likely the deeper you go
        d.miniFight = true;
        d.enemies.push_back(MakeEnemy(pickFrom(minis), d.nextUid++));
        int adds = Roll(1, 2);
        for (int i = 0; i < adds; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
        Log(g, d.enemies[0].name + " blocks the way!");
    } else {
        int count = Roll(3, 4);
        for (int i = 0; i < count; i++) d.enemies.push_back(MakeEnemy(pickFrom(standards), d.nextUid++));
        if (Chance(50)) d.enemies.back() = MakeEnemy(pickFrom(supports), d.nextUid - 1); // a support hangs back at the rear
        Log(g, "Something stirs in the dark...");
    }    for (auto& e : d.enemies) ScaleEnemyForTier(e, d.tier);
    d.anims.clear();
    d.shots.clear();
    d.pending = PendingAction{};
    d.round = 0;
    BeginRound(g);
    d.phase = DPhase::Combat;
}

void StartDungeon(Game& g, Location loc) {
    CompactParty(g);
    g.dungeon = DungeonState{};
    auto& d = g.dungeon;
    d.loc = loc;
    d.visSeed = (unsigned)GetRandomValue(1, 2000000000); // this run's look: which skyline, which atmosphere
    d.atmos = GetRandomValue(0, 2);
    int li = (int)loc;
    d.tier = std::clamp(g.tierSel[li], 0, std::min(CAVE_TIERS - 1, g.tierCleared[li] + 1));
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

void DebugEnterCombat(Game& g, Location loc) {
    StartDungeon(g, loc);
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
    int wins = 0, losses = 0, deaths = 0, anyDeath = 0, rattled = 0, wipeRoom[8] = {0};
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
        if (d.phase == DPhase::Victory) wins++; else { losses++; wipeRoom[std::clamp(d.roomIndex, 0, 7)]++; for (auto& e : d.enemies) if (e.alive) killers[e.name]++; }
    }
    printf("Simulated %d expeditions, crew level %d, cave level %d (%s player):\n", runs, level, CAVE_TIER_LEVEL[tier],
           randomPlayer ? "random" : "sensible");
    printf("  wins %.1f%%   wipes %.1f%%\n", 100.0 * wins / runs, 100.0 * losses / runs);
    printf("  runs with a death %.1f%%   avg deaths %.2f   runs with someone rattled %.1f%%\n",
           100.0 * anyDeath / runs, (double)deaths / runs, 100.0 * rattled / runs);
    printf("  wipes by room:");
    for (int i = 0; i < 6; i++) printf(" %d:%d", i + 1, wipeRoom[i]);
    printf("\n  standing at the end:");
    for (auto& kv : killers) printf(" %s x%d;", kv.first.c_str(), kv.second);
    printf("\n");
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
    if (d.loc == Location::Cave) return;
    float t = g.time, sd = (float)(d.visSeed % 9973) * 1.37f;
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

// Ground colour and texture for each region: sun-bleached sand, black silt, cracked marble flags.
static void DrawRegionFloor(Game& g) {
    auto& d = g.dungeon;
    if (d.loc == Location::Cave) return;
    float off = LayerOffset(g, 1.0f);
    Color base = d.loc == Location::Island ? Color{96, 82, 58, 255} : d.loc == Location::Weeds ? Color{34, 44, 36, 255} : Color{58, 58, 68, 255};
    DrawRectangle(0, 452, SCREEN_W, 268, Fade(base, 0.78f));
    DrawRectangle(0, 450, SCREEN_W, 4, Color{6, 6, 8, 255});
    if (d.loc == Location::Atlantis) {
        for (float x = fmodf(off, 130) - 130; x < SCREEN_W + 130; x += 130) DrawLineEx({x, 452}, {x - 90, 720}, 3, Color{8, 8, 12, 255}); // marble flag seams
        for (int k = 1; k < 5; k++) DrawRectangle(0, 452 + k * k * 12, SCREEN_W, 2, Color{8, 8, 12, 255});
    } else if (d.loc == Location::Island) {
        for (int k = 0; k < 24; k++) DrawEllipse((int)fmodf(k * 133.0f + off, 1400.0f) - 60, 500 + (k % 5) * 40, 16 + k % 4 * 6, 3, Color{60, 50, 34, 255});
    } else {
        for (int k = 0; k < 18; k++) DrawEllipse((int)fmodf(k * 151.0f + off, 1400.0f) - 60, 490 + (k % 4) * 50, 26, 5, Color{18, 26, 20, 255});
    }
}
// ---------------------------------------------------------------- the prop spawner
// Modular decor along the path, chosen per step from a weighted pool for the location and the run's seed:
// each 230-pixel step of the walk may hold one prop (or none), so the path is assembled differently every run.
// Flat shapes, black ink under every mass, one hard lit edge.
enum Prop { P_SKULL, P_IMPALED, P_CAGE, P_WRECK, P_TOTEM, P_FUNGUS, P_HELMET, P_SHELLBONES, P_CORAL, P_ANCHOR, P_POD, P_CRATE, P_ALTAR, P_VOIDCRYSTAL, P_BRAZIER, P_LOSTONE, P_COUNT };
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
        default: break;
    }
}

static void DrawPathProps(Game& g) {
    auto& d = g.dungeon;
    static const PropWeight ISLAND[] = {{P_SKULL, 3}, {P_IMPALED, 3}, {P_CAGE, 2}, {P_WRECK, 3}, {P_TOTEM, 2}};
    static const PropWeight CAVE[] = {{P_FUNGUS, 4}, {P_HELMET, 2}, {P_SHELLBONES, 2}, {P_CORAL, 3}, {P_SKULL, 1}};
    static const PropWeight WEEDS[] = {{P_ANCHOR, 2}, {P_POD, 4}, {P_CRATE, 3}, {P_SHELLBONES, 2}, {P_SKULL, 1}};
    static const PropWeight ATLANTIS[] = {{P_ALTAR, 2}, {P_VOIDCRYSTAL, 3}, {P_BRAZIER, 2}, {P_LOSTONE, 3}};
    const PropWeight* pool = d.loc == Location::Island ? ISLAND : d.loc == Location::Weeds ? WEEDS : d.loc == Location::Atlantis ? ATLANTIS : CAVE;
    int n = d.loc == Location::Island ? 5 : d.loc == Location::Weeds ? 5 : d.loc == Location::Atlantis ? 4 : 5, total = 0;
    for (int i = 0; i < n; i++) total += pool[i].w;
    float sd = (float)(d.visSeed % 7919) * 0.91f;
    Repeat(LayerOffset(g, 1.0f), 230, [&](float sx, float wx) {
        float roll = Hash1(wx * 0.77f + sd);
        if (roll < 0.38f) return;                                       // some steps are bare
        float pick = Hash1(wx * 1.31f + sd * 2.0f) * total;
        Prop p = pool[0].p;
        for (int i = 0; i < n; i++) { if (pick < pool[i].w) { p = pool[i].p; break; } pick -= pool[i].w; }
        float x = sx + Hash1(wx + sd) * 150, y = 470 + Hash1(wx * 2.1f) * 30;
        DrawProp(p, x, y, g.time, (int)(wx * 13));
    });
}
static void DrawCaveLayers(Game& g) {
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
        if (h->deathsDoor) {
            fx.pose.crouch += 0.5f + 0.08f * sinf(ph * 4);
            fx.pose.headDown += 0.6f;
            fx.pose.lean += 0.18f;
        }
        fx.pose = SpringPose(h->id, fx.pose, fx.dx, dt);
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
        int nfs = 16;
        while (nfs > 10 && MeasureTxt(e.name, nfs, true) > 120) nfs--; // long names (Dysformed Crustacean, Tribal Spearman) shrink to fit their rank
        float nw = (float)MeasureTxt(e.name, nfs, true);
        TxtShadow(e.name, r.x + r.width / 2 - nw / 2, r.y + r.height + 22, nfs, Pal::Paper, true);
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

// The run's atmospheric state: colour grade, weather and light, laid over the finished, inked scene.
static void DrawLocationTint(Game& g) {
    auto& d = g.dungeon;
    float t = g.time;
    float seed = (float)(d.visSeed % 997);
    int v = d.atmos;
    auto tint = [&](Color c) { DrawRectangle(0, 0, SCREEN_W, SCREEN_H, c); };                    // a colour grade
    auto lightWash = [&](Color c) { BeginBlendMode(BLEND_ADDITIVE); tint(c); EndBlendMode(); };  // light spilling in
    switch (d.loc) {
        case Location::Cave:
            if (v == 0) { // pitch black: a tight searchlight round the party, ink beyond it
                for (int i = 0; i < 8; i++) DrawRing({690, 390}, 300 + i * 30, 340 + i * 30, 0, 360, 48, Fade(Color{0, 0, 0, 255}, 0.14f + i * 0.06f)); // the lamp reaches the whole line, crew and foes
                DrawRing({690, 390}, 540, 1600, 0, 360, 64, BLACK);
            } else if (v == 1) { // bloom: cyan and violet bleeding off the walls
                lightWash(Color{20, 90, 110, 34});
                BeginBlendMode(BLEND_ADDITIVE);
                for (int k = 0; k < 9; k++) Glow({fmodf(k * 173.0f + seed * 31, 1280.0f), 120 + fmodf(k * 89.0f, 300.0f)}, 150, k % 2 ? Color{160, 60, 230, 40} : Color{40, 210, 230, 44});
                EndBlendMode();
            } else { // silt storm: a grey-brown haze full of drifting dirt
                tint(Color{92, 84, 70, 92});
                for (int k = 0; k < 90; k++) {
                    float px = fmodf(k * 67.0f + t * (30 + k % 9 * 5), 1300.0f) - 10, py = fmodf(k * 41.0f + sinf(t + k) * 14, 520.0f) + 50;
                    DrawRectangle((int)px, (int)py, 3, 2, Color{132, 116, 92, 150});
                }
            }
            break;
        case Location::Island:
            if (v == 0) { // downpour: charcoal grade, slanted rain, and lightning that lights the black ink for an instant
                tint(Color{20, 24, 30, 84});
                for (int k = 0; k < 140; k++) {
                    float px = fmodf(k * 47.0f + t * 180, 1400.0f) - 60, py = fmodf(k * 31.0f + t * 700 + k * 13, 760.0f) - 20;
                    DrawLineEx({px, py}, {px - 8, py + 22}, 1.5f, Color{170, 190, 210, 90});
                }
                float c = fmodf(t + seed * 0.013f, 9.0f);
                if (c < 0.22f) lightWash(Color{190, 210, 255, (unsigned char)(120 * (1 - c / 0.22f))});
            } else if (v == 1) { // toxic fog: yellow-green rolling over the lower half
                tint(Color{70, 80, 30, 46});
                for (int k = 0; k < 7; k++) {
                    float x = fmodf(k * 230.0f + t * 12, 1500.0f) - 150;
                    DrawEllipse((int)x, 470 + (k % 3) * 30, 260, 70, Color{150, 160, 60, 50});
                }
                DrawVGradient({0, 360, (float)SCREEN_W, 360}, Fade(Color{120, 130, 40, 255}, 0.0f), Fade(Color{110, 120, 40, 255}, 0.28f));
            } else { // eldritch sunset: a blood-crimson sky and amber rim light
                DrawVGradient({0, 0, (float)SCREEN_W, 380}, Fade(Color{150, 20, 20, 255}, 0.42f), Fade(Color{60, 10, 20, 255}, 0.0f));
                lightWash(Color{120, 60, 10, 30});
            }
            break;
        case Location::Weeds:
            if (v == 0) { // abyssal current: indigo water, marine rot streaming sideways
                tint(Color{18, 24, 74, 92});
                for (int k = 0; k < 70; k++) {
                    float px = fmodf(k * 89.0f + t * (120 + k % 5 * 30), 1400.0f) - 60, py = 70 + fmodf(k * 53.0f, 520.0f) + sinf(t * 2 + k) * 6;
                    DrawRectangle((int)px, (int)py, 8 + k % 4 * 3, 2, Color{120, 110, 130, 110});
                }
            } else if (v == 1) { // fungal rot: lime murk with pulsing spores
                tint(Color{40, 74, 20, 82});
                for (int k = 0; k < 30; k++) {
                    float px = fmodf(k * 121.0f + sinf(t * 0.5f + k) * 30, 1280.0f), py = 60 + fmodf(k * 67.0f + t * 6, 520.0f);
                    float pulse = 0.5f + 0.5f * sinf(t * 2.2f + k);
                    Glow({px, py}, 16 + pulse * 10, Color{170, 255, 90, (unsigned char)(40 + 60 * pulse)});
                }
            } else { // sanguine tide: burgundy water and crimson rim light
                tint(Color{92, 14, 26, 96});
                lightWash(Color{110, 20, 20, 30});
            }
            break;
        default: // Atlantis
            if (v == 0) { // cosmic void: magenta and violet energy over cold blue light
                tint(Color{30, 14, 66, 88});
                lightWash(Color{40, 60, 140, 24});
                for (int k = 0; k < 12; k++) {
                    float px = fmodf(k * 151.0f + t * 6, 1280.0f), py = fmodf(k * 83.0f + t * 3, 480.0f) + 40, a = 40 + 30 * sinf(t * 1.3f + k);
                    DrawRing({px, py}, 5, 6.5f, 0, 360, 6, Color{230, 90, 230, (unsigned char)std::max(0.0f, a)});
                }
            } else if (v == 1) { // drowned eclipse: drained to slate, gold only where light lands
                tint(Color{110, 114, 122, 150});
                BeginBlendMode(BLEND_ADDITIVE);
                Glow({640, 60}, 300, Color{255, 200, 90, 60});
                EndBlendMode();
            } else { // blood moon: red light bleeding up from vents below, shadows pointing skyward
                tint(Color{60, 6, 10, 70});
                BeginBlendMode(BLEND_ADDITIVE);
                for (int k = 0; k < 6; k++) DrawTri({150.0f + k * 210, 560}, {230.0f + k * 210, 560}, {190.0f + k * 210 + sinf(t + k) * 20, 140}, Color{140, 20, 20, 16});
                Glow({640, 600}, 700, Color{200, 30, 30, 50});
                EndBlendMode();
            }
            break;
    }
    // the vignette that closes every scene in: black at the edges, hard
    for (int i = 0; i < 6; i++) DrawRing({640, 360}, 520 + i * 60, 560 + i * 60, 0, 360, 64, Fade(BLACK, 0.06f + i * 0.045f));
    if (d.atmos >= 0) {
        const char* nm = AtmosphereName(d.loc, v);
        Txt(nm, 20, 62, 14, Fade(Pal::Paper, 0.55f));
    }
}
static void DrawTopBar(Game& g) {
    auto& d = g.dungeon;
    DrawVGradient({0, 0, (float)SCREEN_W, 58}, Color{10, 18, 24, 240}, Color{16, 28, 36, 220});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    TxtShadow(TextFormat("%s  -  %s (Lv %d)", LocationName(d.loc), CAVE_TIER_NAME[d.tier], CAVE_TIER_LEVEL[d.tier]), 20, 15, 22, Pal::Brass, true);
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
    DrawRegionFloor(g);
    DrawSeededSilhouettes(g);
    DrawPathProps(g);
    DrawUnitFigures(g);
    DrawProjectiles(g);
    DrawCaveLighting(g);
    DrawCaveForeground(g);
    InkPass(1.0f, 1.0f);
    DrawDriftingSpecks(g);
    DrawLocationTint(g);
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
    if (d.phase != DPhase::Combat && d.phase != DPhase::Walking) DrawInventoryBar(g);

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
            Rectangle main{340, 90, 600, 260};
            Panel(main);
            std::string body = d.roomIsChest ? "A chest, bound shut, sits half-buried in the silt."
                                             : TextFormat("A barnacled cache! +%d gold.%s", d.roomGold, d.light < 50 ? " The darkness made the find richer." : "");
            DrawTextCenteredBold("Treasure", main.x + main.width / 2, main.y + 20, 30, Pal::Brass);
            DrawWrapped(body, {main.x + 36, main.y + 66, main.width - 72, 130}, 17, Pal::Ink);
            DrawFoundItemPanel(g, main);
            bool blocked = d.pendingItem || (d.roomIsChest && !d.chestOpened);
            if (Button({main.x + main.width / 2 - 130, main.y + main.height - 46, 260, 42}, blocked ? "Move on (leave anything unclaimed)" : "Continue")) {
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
                d.phase = DPhase::Corridor; d.corridorT = 0;
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
                body = TextFormat("%s is beaten. You bring home %d gold", LocationBossName(d.loc), d.lootGold);
                for (int r : d.lootRelics) body += ", a " + Relics()[r].name;
                body += ", and as a reward for finishing: a " + Relics()[d.rewardRelic].name + ".";
                body += TextFormat("\n\nSurvivors earn %d XP.", 5 + lvl * 2);
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
