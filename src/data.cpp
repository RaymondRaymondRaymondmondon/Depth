// ============================================================================
//  DEPTH - game data: crew classes, abilities, relics, enemies, leveling.
//  Most balancing happens in this file.
// ============================================================================
#include "game.h"
#include <algorithm>

// ---------------------------------------------------------------- abilities
static Ability Ab(const char* name, const char* desc, int from, int hits, Target t) {
    Ability a;
    a.name = name; a.desc = desc; a.usableFrom = from; a.hits = hits; a.target = t;
    return a;
}

static std::vector<Ability> BuildNurse() {
    std::vector<Ability> v;
    Ability a = Ab("Scalpel Slash", "A precise cut that leaves the target bleeding.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.8f; a.bleed = 2; v.push_back(a);
    a = Ab("Sedative Jab", "Light damage with a strong chance to stun.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.4f; a.stunChance = 65; v.push_back(a);
    a = Ab("Field Dressing", "Heal an ally and cure bleed and poison.", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 6; a.cure = true; v.push_back(a);
    a = Ab("Smelling Salts", "Steady an ally's nerves (-15 stress).", ANY_RANK, ANY_RANK, Target::Ally);
    a.stressHeal = 15; a.heal = 1; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildDiver() {
    std::vector<Ability> v;
    Ability a = Ab("Harpoon Thrust", "A fast, accurate stab.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.0f; a.accBonus = 5; v.push_back(a);
    a = Ab("Twin Strike", "Two quick hits on the same target.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.55f; a.hitsCount = 2; v.push_back(a);
    a = Ab("Undertow", "Hook a back-line enemy and drag it forward 2 ranks.", MELEE_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.5f; a.moveTarget = -2; v.push_back(a);
    a = Ab("Riptide Shove", "Shove the front enemy back 2 ranks. May stun.", MELEE_FROM, RANK_1, Target::Enemy);
    a.dmgMult = 0.6f; a.moveTarget = 2; a.stunChance = 20; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildCaptain() {
    std::vector<Ability> v;
    Ability a = Ab("Cutlass", "A solid swing of the captain's blade.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.9f; v.push_back(a);
    a = Ab("Rally the Crew", "Whole party deals +25% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDmg = 25; a.stressHeal = 3; v.push_back(a);
    a = Ab("All Hands!", "Swap places with an ally and calm them a little.", ANY_RANK, ANY_RANK, Target::Ally);
    a.swapWithTarget = true; a.stressHeal = 4; v.push_back(a);
    a = Ab("Steady Now", "A calm word to everyone (-8 stress to the party).", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.stressHeal = 8; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildMechanic() {
    std::vector<Ability> v;
    Ability a = Ab("Wrench Bash", "A heavy blow that may stun.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.9f; a.stunChance = 35; v.push_back(a);
    a = Ab("Rivet Spray", "Pepper both front enemies with hot rivets.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.5f; a.aoe = true; v.push_back(a);
    a = Ab("Brace for Impact", "Guard: enemies must attack you, +25 protection.", ANY_RANK, ANY_RANK, Target::Self);
    a.guardTurns = 2; v.push_back(a);
    a = Ab("Patch the Hull", "Weld over your own wounds (heal self).", ANY_RANK, ANY_RANK, Target::Self);
    a.heal = 5; v.push_back(a);
    return v;
}

const std::vector<Ability>& ClassAbilities(HeroClass c) {
    static const std::vector<Ability> nurse = BuildNurse(), diver = BuildDiver(),
                                      captain = BuildCaptain(), mechanic = BuildMechanic();
    switch (c) {
        case HeroClass::Nurse: return nurse;
        case HeroClass::Diver: return diver;
        case HeroClass::Captain: return captain;
        default: return mechanic;
    }
}

const char* ClassName(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return "Nurse";
        case HeroClass::Diver: return "Diver";
        case HeroClass::Captain: return "Captain";
        default: return "Mechanic";
    }
}

const char* ClassBlurb(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return "A steady-handed medic. Fights up close, but the real job is keeping the crew breathing.";
        case HeroClass::Diver: return "Quick and daring. Strikes fast from the front and hauls enemies out of position.";
        case HeroClass::Captain: return "Leads from anywhere on deck. Rallies the crew and reorders the line.";
        default: return "Built like a bulkhead. Soaks up hits so the rest of the crew doesn't have to.";
    }
}

Color ClassColor(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return {236, 120, 150, 255};
        case HeroClass::Diver: return {64, 196, 190, 255};
        case HeroClass::Captain: return {70, 90, 170, 255};
        default: return {232, 140, 50, 255};
    }
}

// ---------------------------------------------------------------- stats
static Stats BaseStats(HeroClass c) {
    //                         hp  min max spd acc dodge prot resist
    switch (c) {
        case HeroClass::Nurse:   return {22, 3, 6, 5, 85, 10, 0, 0};
        case HeroClass::Diver:   return {20, 4, 8, 8, 90, 15, 0, 0};
        case HeroClass::Captain: return {24, 4, 7, 5, 85, 8, 10, 10};
        default:                 return {30, 4, 7, 2, 80, 5, 20, 0};
    }
}

Stats GetStats(const Hero& h) {
    Stats s = BaseStats(h.cls);
    s.maxHp += h.level * 2;
    s.dmgMin += h.level / 2;
    s.dmgMax += h.level;
    s.acc += h.level * 2;
    s.dodge += h.level * 2;
    for (int r : h.relics) {
        if (r < 0) continue;
        const RelicDef& d = Relics()[r];
        s.maxHp += d.hp; s.dmgMin += d.dmg; s.dmgMax += d.dmg; s.speed += d.speed;
        s.acc += d.acc; s.dodge += d.dodge; s.prot += d.prot; s.stressResist += d.stressResist;
    }
    if (h.rattled) { s.acc -= 10; s.dodge -= 5; }
    s.dmgMin = std::max(1, s.dmgMin);
    s.dmgMax = std::max(s.dmgMin, s.dmgMax);
    s.prot = std::clamp(s.prot, 0, 60);
    s.dodge = std::max(0, s.dodge);
    s.speed = std::max(0, s.speed);
    s.stressResist = std::clamp(s.stressResist, 0, 60);
    return s;
}

// ---------------------------------------------------------------- relics
static RelicDef R(const char* n, const char* d, int hp, int dmg, int spd, int acc, int dodge, int prot, int sr, int price) {
    RelicDef r;
    r.name = n; r.desc = d; r.hp = hp; r.dmg = dmg; r.speed = spd; r.acc = acc;
    r.dodge = dodge; r.prot = prot; r.stressResist = sr; r.price = price;
    return r;
}

const std::vector<RelicDef>& Relics() {
    static const std::vector<RelicDef> list = {
        R("Wrench", "+10 Protection", 0, 0, 0, 0, 0, 10, 0, 70),                // 0
        R("Pipe Wrench", "+1 Damage, -1 Speed", 0, 1, -1, 0, 0, 0, 0, 60),      // 1
        R("Monkey Wrench", "+5 Accuracy, +3 Dodge", 0, 0, 0, 5, 3, 0, 0, 70),   // 2
        R("Rivet Gun", "+2 Damage, -5 Accuracy", 0, 2, 0, -5, 0, 0, 0, 80),     // 3
        R("Sword", "+1 Damage, +3 Accuracy", 0, 1, 0, 3, 0, 0, 0, 80),          // 4
        R("Butcher Knife", "+2 Damage, -5 Protection", 0, 2, 0, 0, 0, -5, 0, 75),
        R("Flintlock", "+8 Accuracy", 0, 0, 0, 8, 0, 0, 0, 70),
        R("Six-Shooter", "+1 Damage, +1 Speed", 0, 1, 1, 0, 0, 0, 0, 90),
        R("MedKit", "+5 Max HP", 5, 0, 0, 0, 0, 0, 0, 70),                      // 8
        R("Syringe", "+2 Speed", 0, 0, 2, 0, 0, 0, 0, 80),
        R("Pliers", "Resist 15% of stress", 0, 0, 0, 0, 0, 0, 15, 75),
        R("Backpack", "+3 Max HP, +5 Protection", 3, 0, 0, 0, 0, 5, 0, 85),
    };
    return list;
}

// ---------------------------------------------------------------- heroes
static const char* NAMES[] = {"Abernathy", "Beatrix", "Cormac",   "Delphine", "Ezra",   "Fitz",
                              "Greta",     "Hollis",  "Isolde",   "Jasper",   "Kit",    "Lorelei",
                              "Mabel",     "Nils",    "Ottoline", "Percival", "Quincy", "Rosalind",
                              "Silas",     "Tamsin",  "Ulric",    "Violet",   "Wendell", "Yara"};

Hero MakeHero(Game& g, HeroClass c) {
    Hero h;
    h.id = g.nextHeroId++;
    h.cls = c;
    h.name = NAMES[GetRandomValue(0, (int)(sizeof(NAMES) / sizeof(NAMES[0])) - 1)];
    h.hp = GetStats(h).maxHp;
    return h;
}

Hero MakeRandomHero(Game& g) {
    return MakeHero(g, (HeroClass)GetRandomValue(0, (int)HeroClass::COUNT - 1));
}

// Level 0-6. Values are total XP needed to reach each level.
static const int XP_TABLE[7] = {0, 4, 10, 18, 28, 40, 55};

void GiveXP(Game& g, Hero& h, int amount) {
    int oldMax = GetStats(h).maxHp;
    h.xp += amount;
    while (h.level < 6 && h.xp >= XP_TABLE[h.level + 1]) {
        h.level++;
        Toast(g, h.name + " reached level " + std::to_string(h.level) + "!");
    }
    h.hp += GetStats(h).maxHp - oldMax;
}

int XpForNextLevel(const Hero& h) { return h.level >= 6 ? -1 : XP_TABLE[h.level + 1]; }

Hero* FindHero(Game& g, int id) {
    if (id < 0) return nullptr;
    for (auto& h : g.roster)
        if (h.id == id) return &h;
    return nullptr;
}

bool InParty(const Game& g, int id) {
    for (int p : g.party)
        if (p == id && id >= 0) return true;
    return false;
}

// Removes missing/dead/on-leave heroes from the party and closes the gaps.
void CompactParty(Game& g) {
    std::array<int, PARTY_SIZE> np{{-1, -1, -1, -1}};
    int k = 0;
    for (int id : g.party) {
        Hero* h = FindHero(g, id);
        if (h && !h->dead && h->onLeave == 0) np[k++] = id;
    }
    g.party = np;
}

void RefreshRadar(Game& g) {
    g.recruits.clear();
    for (int i = 0; i < 3; i++) g.recruits.push_back(MakeRandomHero(g));
    g.shopRelics.clear();
    while (g.shopRelics.size() < 3) {
        int r = GetRandomValue(0, (int)Relics().size() - 1);
        if (std::find(g.shopRelics.begin(), g.shopRelics.end(), r) == g.shopRelics.end()) g.shopRelics.push_back(r);
    }
}

// ---------------------------------------------------------------- enemies
static EnemyAbility EA(const char* n, int hits, float mult) {
    EnemyAbility a;
    a.name = n; a.hits = hits; a.dmgMult = mult;
    return a;
}

Enemy MakeEnemy(EnemyType t, int uid) {
    Enemy e;
    e.uid = uid;
    e.type = t;
    switch (t) {
        case EnemyType::SeaLouse: {
            e.name = "Sea Louse";
            e.maxHp = 12; e.dmgMin = 3; e.dmgMax = 5; e.speed = 7; e.acc = 80; e.dodge = 15; e.prot = 0;
            EnemyAbility a = EA("Nibble", MELEE_HITS, 1.0f); a.stress = 6; e.abilities.push_back(a);
            a = EA("Skitter Bite", ANY_RANK, 0.7f); a.bleed = 2; e.abilities.push_back(a);
        } break;
        case EnemyType::CaveShrimp: {
            e.name = "Pistol Shrimp";
            e.maxHp = 15; e.dmgMin = 4; e.dmgMax = 6; e.speed = 6; e.acc = 85; e.dodge = 10; e.prot = 10;
            EnemyAbility a = EA("Snap Claw", MELEE_HITS, 1.0f); a.stunChance = 20; e.abilities.push_back(a);
            a = EA("Sonic Pop", ANY_RANK, 0.5f); a.stress = 14; e.abilities.push_back(a);
        } break;
        case EnemyType::BrineWorm: {
            e.name = "Brine Worm";
            e.maxHp = 17; e.dmgMin = 3; e.dmgMax = 5; e.speed = 3; e.acc = 80; e.dodge = 5; e.prot = 0;
            EnemyAbility a = EA("Toxic Spit", ANY_RANK, 0.6f); a.poison = 3; a.stress = 4; e.abilities.push_back(a);
            a = EA("Coil", MELEE_HITS, 1.1f); a.stress = 5; e.abilities.push_back(a);
        } break;
        case EnemyType::Lobster: {
            e.name = "The Lobster";
            e.boss = true;
            e.maxHp = 60; e.dmgMin = 7; e.dmgMax = 11; e.speed = 4; e.acc = 85; e.dodge = 5; e.prot = 25;
            EnemyAbility a = EA("Crushing Claw", MELEE_HITS, 1.2f); a.stunChance = 30; e.abilities.push_back(a);
            a = EA("Tail Sweep", MELEE_HITS, 0.7f); a.aoe = true; e.abilities.push_back(a);
            a = EA("Clack Clack", ANY_RANK, 0.0f); a.aoe = true; a.stress = 13; e.abilities.push_back(a);
        } break;
    }
    e.hp = e.maxHp;
    return e;
}

// ---------------------------------------------------------------- new game
void InitGame(Game& g) {
    for (int c = 0; c < (int)HeroClass::COUNT; c++) g.roster.push_back(MakeHero(g, (HeroClass)c));
    // Starting marching order: Mechanic, Diver, Captain, Nurse
    g.party = {{g.roster[3].id, g.roster[1].id, g.roster[2].id, g.roster[0].id}};
    g.relicStorage = {0, 8}; // Wrench, MedKit
    RefreshRadar(g);
    GeneratePipesLayout(g);
}
