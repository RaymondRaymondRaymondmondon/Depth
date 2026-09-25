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
    // unlocked by leveling up
    a = Ab("Triage", "Quick care for the whole party (heal 3 each).", RANGED_FROM, ANY_RANK, Target::AllAllies);
    a.heal = 3; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Toxin Vial", "Lob a vial at the back line. Poison stacks.", RANGED_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.3f; a.poison = 3; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Adrenaline Shot", "An ally heals 2, calms a little and hits 20% harder.", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 2; a.buffDmg = 20; a.stressHeal = 4; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Bone Saw", "A brutal, messy cut. Heavy bleeding.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.15f; a.bleed = 3; a.accBonus = -5; a.unlockLevel = 3; v.push_back(a);
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
    a = Ab("Speargun", "A barbed bolt from the second line or further back.", RANGED_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.85f; a.accBonus = 5; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Ink Cloud", "Vanish into the murk: +25 dodge for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDodge = 25; a.stressHeal = 3; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Mark the Prey", "Tag a target: it takes 25% more damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.2f; a.mark = true; a.accBonus = 10; a.ranged = true; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Depth Charge", "Blast the back two ranks. May stun.", RANK_1 | RANK_2 | RANK_3, RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.5f; a.aoe = true; a.stunChance = 15; a.ranged = true; a.unlockLevel = 3; v.push_back(a);
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
    a = Ab("Flintlock", "A steady shot at any enemy rank.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.75f; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Hold the Line", "Whole party gains +15 protection for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffProt = 15; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Grog Ration", "A tot of grog: an ally heals 4 and loses 10 stress.", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 4; a.stressHeal = 10; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Grapeshot", "A scattering blast across the first three enemy ranks.", RANGED_FROM, RANK_1 | RANK_2 | RANK_3, Target::Enemy);
    a.dmgMult = 0.4f; a.aoe = true; a.ranged = true; a.unlockLevel = 3; v.push_back(a);
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
    a = Ab("Blowtorch", "Scorch both front enemies. They keep burning.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.35f; a.aoe = true; a.bleed = 2; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Iron Plating", "Bolt on extra plates: +30 protection for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffProt = 30; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Jury-Rig", "Patch up an ally with whatever's to hand (heal 5).", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 5; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Steam Valve", "Vent scalding steam over every enemy. May stun.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.25f; a.aoe = true; a.stunChance = 20; a.ranged = true; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildWhaler() {
    std::vector<Ability> v;
    Ability a = Ab("Harpoon Gun", "A heavy bolt from the gun, fired from any rank.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.85f; a.accBonus = 5; a.ranged = true; v.push_back(a);
    a = Ab("Boat Hook", "Hook a back-line enemy and haul it in 2 ranks.", RANGED_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.3f; a.moveTarget = -2; a.ranged = true; v.push_back(a);
    a = Ab("Warning Shot", "Blast the front enemy back 2 ranks.", RANGED_FROM, RANK_1, Target::Enemy);
    a.dmgMult = 0.4f; a.moveTarget = 2; a.ranged = true; v.push_back(a);
    a = Ab("Steady Aim", "Brace and take careful aim: +20% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDmg = 20; v.push_back(a);
    a = Ab("Chain Harpoon", "A tethered shot that marks its catch.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.6f; a.mark = true; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Barbed Shot", "A barbed head that tears on the way out.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.5f; a.bleed = 3; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Broadside", "A spread of bolts across the front three ranks.", RANGED_FROM, RANK_1 | RANK_2 | RANK_3, Target::Enemy);
    a.dmgMult = 0.35f; a.aoe = true; a.ranged = true; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Harpoon Volley", "Two aimed shots at the same target.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.5f; a.hitsCount = 2; a.accBonus = 5; a.ranged = true; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildStowaway() {
    std::vector<Ability> v;
    Ability a = Ab("Wild Swing", "A reckless haymaker: hard to aim, harder to stop.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.0f; a.accBonus = -10; v.push_back(a);
    a = Ab("Broken Bottle", "A jagged edge, up close.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.6f; a.bleed = 3; a.accBonus = -5; v.push_back(a);
    a = Ab("Foul Breath", "A blast of rotgut fumes: blinds the target for 3 turns.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.2f; a.buffAcc = -15; a.accBonus = -5; v.push_back(a);
    a = Ab("Liquid Courage", "A stiff drink: steadies the nerves and the swing (+15% damage).", ANY_RANK, ANY_RANK, Target::Self);
    a.stressHeal = 10; a.buffDmg = 15; v.push_back(a);
    a = Ab("Poisoned Flask", "Lob a flask of something you shouldn't drink.", RANGED_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.3f; a.poison = 3; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Stumble", "Trip into the target, off balance but heavy.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.5f; a.stunChance = 30; a.accBonus = -10; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Reckless Brawl", "Swing wildly at both enemies up front.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.4f; a.aoe = true; a.bleed = 2; a.accBonus = -10; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Last Call", "A slurred curse that leaves the target seeing double.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.3f; a.buffAcc = -25; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildMerman() {
    std::vector<Ability> v;
    Ability a = Ab("Trident Strike", "A powerful thrust with the trident.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.1f; v.push_back(a);
    a = Ab("Crushing Grip", "A bone-crushing grapple. May stun.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.7f; a.stunChance = 25; v.push_back(a);
    a = Ab("Tail Sweep", "A sweeping blow across both front enemies.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.5f; a.aoe = true; v.push_back(a);
    a = Ab("Tidal Roar", "A bellow that hardens resolve: +20% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDmg = 20; v.push_back(a);
    a = Ab("Savage Bite", "Fangs meant for something bigger than you.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.3f; a.accBonus = -5; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Riptide Slam", "Slam the front enemy back with the tide.", MELEE_FROM, RANK_1, Target::Enemy);
    a.dmgMult = 0.6f; a.moveTarget = 2; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Frenzy", "A killing frenzy: +30% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDmg = 30; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Deep Fury", "Everything, all at once.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.5f; a.accBonus = -10; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildQueen() {
    std::vector<Ability> v;
    Ability a = Ab("Royal Scepter", "A blow struck with a scepter that once meant something.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.6f; a.accBonus = 5; a.ranged = true; v.push_back(a);
    a = Ab("Curse of the Fallen Throne", "An old curse: weakens the target's attacks for 3 turns.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.buffDmg = -15; a.ranged = true; v.push_back(a);
    a = Ab("Tend the Faithful", "A small mercy, from someone who has little left to give.", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 4; v.push_back(a);
    a = Ab("Regal Composure", "Her bearing alone steadies the crew (-6 stress).", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.stressHeal = 6; v.push_back(a);
    a = Ab("Binding Curse", "Strips away a foe's defenses for 3 turns.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.buffProt = -15; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Healing Balm", "A remedy from the old palace gardens.", ANY_RANK, ANY_RANK, Target::Ally);
    a.heal = 5; a.cure = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Wrath of the Deposed", "Everything she has left, hurled at once.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.5f; a.mark = true; a.ranged = true; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Last Decree", "One final command to the crew: +15% damage, -5 stress.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDmg = 15; a.stressHeal = 5; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildRobot() {
    std::vector<Ability> v;
    Ability a = Ab("Piston Fist", "A hydraulic punch. May stun.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.95f; a.stunChance = 15; v.push_back(a);
    a = Ab("Gear Grind", "Grinding gears tear at both front enemies.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.45f; a.aoe = true; v.push_back(a);
    a = Ab("Reposition Protocol", "Trade places with an ally, calculated to the inch.", ANY_RANK, ANY_RANK, Target::Ally);
    a.swapWithTarget = true; v.push_back(a);
    a = Ab("Overclock", "Push the boiler past redline: +20% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDmg = 20; v.push_back(a);
    a = Ab("Steam Hammer", "A heavy overhead strike, vented with steam.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 1.2f; a.accBonus = -5; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Bulwark Mode", "Lock into place: enemies must attack you, +25 protection.", ANY_RANK, ANY_RANK, Target::Self);
    a.guardTurns = 2; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Reinforce", "Extend armor plating to the whole party: +15 protection for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffProt = 15; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Full Steam", "Every system at once: +30% damage, +15 protection for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDmg = 30; a.buffProt = 15; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildOctopus() {
    std::vector<Ability> v;
    Ability a = Ab("Tentacle Lash", "Two quick lashes, weak but fast.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.5f; a.hitsCount = 2; v.push_back(a);
    a = Ab("Ink Spray", "A cloud of ink, blinding the target for 3 turns.", ANY_RANK, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.2f; a.buffAcc = -10; v.push_back(a);
    a = Ab("Quick Snatch", "A grasping tentacle pulls a back-line enemy closer.", RANGED_FROM, RANK_2 | RANK_3 | RANK_4, Target::Enemy);
    a.dmgMult = 0.4f; a.moveTarget = -1; a.ranged = true; v.push_back(a);
    a = Ab("Camouflage", "Blend into the rocks: +20 dodge for 3 turns.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDodge = 20; v.push_back(a);
    a = Ab("Eight-Armed Flurry", "Every arm at once, weak but relentless.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.3f; a.hitsCount = 3; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Jet Propulsion", "A burst of water jets it out of harm's way.", ANY_RANK, ANY_RANK, Target::Self);
    a.buffDodge = 15; a.buffProt = 10; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Venomous Bite", "A small, weak, poisoned bite.", MELEE_FROM, MELEE_HITS, Target::Enemy);
    a.dmgMult = 0.5f; a.poison = 2; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Deep Sea Barrage", "A spray of ink and grit across the front ranks.", RANGED_FROM, RANK_1 | RANK_2 | RANK_3, Target::Enemy);
    a.dmgMult = 0.3f; a.aoe = true; a.ranged = true; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildSiren() {
    std::vector<Ability> v;
    Ability a = Ab("Enthralling Song", "A song that saps the will to fight: -15% damage for 3 turns.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.buffDmg = -15; a.ranged = true; v.push_back(a);
    a = Ab("Mesmerize", "A hypnotic verse. Often stuns.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.2f; a.stunChance = 40; a.ranged = true; v.push_back(a);
    a = Ab("Rally Cry", "A rousing chorus: +15% damage for the whole party.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDmg = 15; v.push_back(a);
    a = Ab("Reposition", "Trade places with an ally, mid-verse.", ANY_RANK, ANY_RANK, Target::Ally);
    a.swapWithTarget = true; v.push_back(a);
    a = Ab("Siren's Wail", "A piercing wail that shatters a foe's guard for 3 turns.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.buffProt = -15; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Harmonize", "A soothing melody sharpens the party's footing: +10 dodge for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDodge = 10; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Discordant Screech", "An ugly note that rattles and blinds the front line.", RANGED_FROM, RANK_1 | RANK_2 | RANK_3, Target::Enemy);
    a.dmgMult = 0.3f; a.aoe = true; a.buffAcc = -10; a.ranged = true; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Anthem of the Deep", "Her finest song: +20% damage, +10 protection for the party.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDmg = 20; a.buffProt = 10; a.unlockLevel = 3; v.push_back(a);
    return v;
}

static std::vector<Ability> BuildWisp() {
    std::vector<Ability> v;
    Ability a = Ab("Glowing Bolt", "A bolt of cold light from any rank.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.6f; a.ranged = true; v.push_back(a);
    a = Ab("Paralyzing Light", "A flare that often locks a target in place.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.2f; a.stunChance = 45; a.ranged = true; v.push_back(a);
    a = Ab("Guiding Glow", "A light that helps the whole crew move quicker: +15 dodge for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffDodge = 15; v.push_back(a);
    a = Ab("Soothing Aura", "A calm, cold light settles over the party (-8 stress).", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.stressHeal = 8; v.push_back(a);
    a = Ab("Will-o'-Wisp Flare", "A blinding flash that may stun.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.dmgMult = 0.5f; a.stunChance = 20; a.ranged = true; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Empower", "Lend an ally some of its own light: +20% damage for 3 turns.", ANY_RANK, ANY_RANK, Target::Ally);
    a.buffDmg = 20; a.unlockLevel = 1; v.push_back(a);
    a = Ab("Blinding Flash", "A flash that leaves a target seeing spots for 3 turns.", RANGED_FROM, ANY_RANK, Target::Enemy);
    a.buffAcc = -15; a.ranged = true; a.unlockLevel = 2; v.push_back(a);
    a = Ab("Radiant Ward", "Wraps the party in cold light: +15 protection, +10 dodge for 3 turns.", ANY_RANK, ANY_RANK, Target::AllAllies);
    a.buffProt = 15; a.buffDodge = 10; a.unlockLevel = 3; v.push_back(a);
    return v;
}

const std::vector<Ability>& ClassAbilities(HeroClass c) {
    static const std::vector<Ability> nurse = BuildNurse(), diver = BuildDiver(),
                                      captain = BuildCaptain(), mechanic = BuildMechanic(),
                                      whaler = BuildWhaler(), stowaway = BuildStowaway(),
                                      merman = BuildMerman(), queen = BuildQueen(),
                                      robot = BuildRobot(), octopus = BuildOctopus(),
                                      siren = BuildSiren(), wisp = BuildWisp();
    switch (c) {
        case HeroClass::Nurse: return nurse;
        case HeroClass::Diver: return diver;
        case HeroClass::Captain: return captain;
        case HeroClass::Mechanic: return mechanic;
        case HeroClass::Whaler: return whaler;
        case HeroClass::Stowaway: return stowaway;
        case HeroClass::Merman: return merman;
        case HeroClass::Queen: return queen;
        case HeroClass::Robot: return robot;
        case HeroClass::Octopus: return octopus;
        case HeroClass::Siren: return siren;
        default: return wisp;
    }
}

const char* LocationName(Location loc) {
    switch (loc) {
        case Location::Island: return "The Island";
        case Location::Weeds: return "The Weeds";
        case Location::Atlantis: return "Atlantis";
        default: return "The Cave";
    }
}
const char* LocationBossName(Location loc) {
    switch (loc) {
        case Location::Island: return "the Coconut Queen";
        case Location::Weeds: return "Neptune's Herald";
        case Location::Atlantis: return "the Drowned King";
        default: return "the Lobster";
    }
}
const char* LocationDesc(Location loc) {
    switch (loc) {
        case Location::Island: return "Sun-baked shallows and a wrecked longboat, ruled by the Sun God and the Coconut Queen.";
        case Location::Weeds: return "A kelp forest thick with merfolk, octopi, and barracuda. Neptune's Herald waits within.";
        case Location::Atlantis: return "A sunken city of lost ones who still worship something ancient. The Drowned King still keeps its gate.";
        default: return "An undersea cave crawling with oversized crustaceans, sea bugs, and worms. Mini boss: the Lobster.";
    }
}

const char* ClassName(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return "Nurse";
        case HeroClass::Diver: return "Diver";
        case HeroClass::Captain: return "Captain";
        case HeroClass::Mechanic: return "Mechanic";
        case HeroClass::Whaler: return "Whaler";
        case HeroClass::Stowaway: return "Stowaway";
        case HeroClass::Merman: return "Merman";
        case HeroClass::Queen: return "Dethroned Island Queen";
        case HeroClass::Robot: return "Robot";
        case HeroClass::Octopus: return "Octopus";
        case HeroClass::Siren: return "Siren";
        default: return "Wisp of the Sea";
    }
}

const char* ClassBlurb(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return "A steady-handed medic. Fights up close, but the real job is keeping the crew breathing.";
        case HeroClass::Diver: return "Quick and daring. Strikes fast from the front and hauls enemies out of position.";
        case HeroClass::Captain: return "Leads from anywhere on deck. Rallies the crew and reorders the line.";
        case HeroClass::Mechanic: return "Built like a bulkhead. Soaks up hits so the rest of the crew doesn't have to.";
        case HeroClass::Whaler: return "A harpoon gun and a steady hand. Fights from range and hauls enemies wherever it likes.";
        case HeroClass::Stowaway: return "Found sleeping in the hold, three sheets to the wind. Fights dirty, sees double, doesn't much care.";
        case HeroClass::Merman: return "Hauled aboard fighting. Ferociously strong up close, and hard to calm down.";
        case HeroClass::Queen: return "Once ruled an island that no longer exists. A weaker healer, but her curses still carry weight.";
        case HeroClass::Robot: return "A steampunk contraption of gears and steam. Slow, armored, and endlessly reconfigurable.";
        case HeroClass::Octopus: return "Eight arms, none of them strong, all of them fast. Just as at home up close as at range.";
        case HeroClass::Siren: return "Her song saps the will to fight and steels her own crew's nerve in the same breath.";
        default: return "A drifting light that was once something else. Stuns, wards, and lights the way.";
    }
}

Color ClassColor(HeroClass c) {
    switch (c) {
        case HeroClass::Nurse: return {236, 120, 150, 255};
        case HeroClass::Diver: return {64, 196, 190, 255};
        case HeroClass::Captain: return {70, 90, 170, 255};
        case HeroClass::Mechanic: return {232, 140, 50, 255};
        case HeroClass::Whaler: return {90, 110, 130, 255};
        case HeroClass::Stowaway: return {150, 110, 60, 255};
        case HeroClass::Merman: return {40, 150, 130, 255};
        case HeroClass::Queen: return {180, 130, 200, 255};
        case HeroClass::Robot: return {170, 150, 90, 255};
        case HeroClass::Octopus: return {160, 70, 140, 255};
        case HeroClass::Siren: return {210, 90, 130, 255};
        default: return {150, 220, 220, 255};
    }
}

// ---------------------------------------------------------------- stats
static Stats BaseStats(HeroClass c) {
    //                              hp  min max spd acc dodge prot resist
    switch (c) {
        case HeroClass::Nurse:      return {22, 3, 6, 5, 85, 10, 0, 0};
        case HeroClass::Diver:      return {20, 4, 8, 8, 90, 15, 0, 0};
        case HeroClass::Captain:    return {24, 4, 7, 5, 85, 8, 10, 10};
        case HeroClass::Mechanic:   return {30, 4, 7, 2, 80, 5, 20, 0};
        case HeroClass::Whaler:     return {22, 4, 8, 4, 85, 8, 5, 5};
        case HeroClass::Stowaway:   return {20, 3, 7, 6, 72, 12, 0, 25};
        case HeroClass::Merman:     return {26, 5, 10, 6, 82, 10, 5, 0};
        case HeroClass::Queen:      return {20, 3, 6, 5, 85, 10, 0, 15};
        case HeroClass::Robot:      return {32, 4, 7, 3, 80, 3, 25, 30};
        case HeroClass::Octopus:    return {20, 2, 5, 9, 88, 18, 0, 5};
        case HeroClass::Siren:      return {20, 2, 5, 6, 82, 10, 0, 10};
        default:                    return {18, 2, 5, 7, 85, 20, 0, 20}; // Wisp
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
        R("Wrench", "+10 Protection", 0, 0, 0, 0, 0, 10, 0, 170),                // 0
        R("Pipe Wrench", "+1 Damage, -1 Speed", 0, 1, -1, 0, 0, 0, 0, 145),      // 1
        R("Monkey Wrench", "+5 Accuracy, +3 Dodge", 0, 0, 0, 5, 3, 0, 0, 170),   // 2
        R("Rivet Gun", "+2 Damage, -5 Accuracy", 0, 2, 0, -5, 0, 0, 0, 195),     // 3
        R("Sword", "+1 Damage, +3 Accuracy", 0, 1, 0, 3, 0, 0, 0, 195),          // 4
        R("Butcher Knife", "+2 Damage, -5 Protection", 0, 2, 0, 0, 0, -5, 0, 180),
        R("Flintlock", "+8 Accuracy", 0, 0, 0, 8, 0, 0, 0, 170),
        R("Six-Shooter", "+1 Damage, +1 Speed", 0, 1, 1, 0, 0, 0, 0, 220),
        R("MedKit", "+5 Max HP", 5, 0, 0, 0, 0, 0, 0, 170),                      // 8
        R("Syringe", "+2 Speed", 0, 0, 2, 0, 0, 0, 0, 195),
        R("Pliers", "Resist 15% of stress", 0, 0, 0, 0, 0, 0, 15, 180),
        R("Backpack", "+3 Max HP, +5 Protection", 3, 0, 0, 0, 0, 5, 0, 205),
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

int LoadoutCount(const Hero& h) {
    int n = 0;
    for (int a : h.loadout) if (a >= 0) n++;
    return n;
}

// ---------------------------------------------------------------- workshop upgrades
//                                    level:  0   1   2   3
static const int ROSTER_SIZE[4]      = {8,  9, 10, 12};
static const int RADAR_REFRESHES[4]  = {0,  1,  2,  3};
static const int SCAN_COST[4]        = {70, 55, 40, 25};
static const int WARD_COST[4]        = {9,  7,  5,  4};
static const int LIGHT_DRAIN[4]      = {20, 16, 12, 9};

int MaxRoster(const Game& g) { return ROSTER_SIZE[g.upgrades[UP_BUNKS]]; }
int SonarRefreshCount(const Game& g) { return RADAR_REFRESHES[g.upgrades[UP_SONAR]]; }
int ScanCost(const Game& g) { return SCAN_COST[g.upgrades[UP_SONAR]]; }
int WardCostPerHp(const Game& g) { return WARD_COST[g.upgrades[UP_INFIRMARY]]; }
int LightDrainPerRoom(const Game& g) { return LIGHT_DRAIN[g.upgrades[UP_REFLECTOR]]; }
int UpgradePrice(int level) { return level == 1 ? 400 : level == 2 ? 800 : 1400; } // gold should stay scarce: parkour and Flats are meant to fill the gap

const char* UpgradeName(int u) {
    switch (u) {
        case UP_REFLECTOR: return "Flashlight Reflector";
        case UP_BUNKS: return "Bunk Extension";
        case UP_SONAR: return "Sonar Array";
        default: return "Infirmary Gear";
    }
}

const char* UpgradeDesc(int u, int lv) {
    switch (u) {
        case UP_REFLECTOR: return TextFormat("Each room drains %d light", LIGHT_DRAIN[lv]);
        case UP_BUNKS: return TextFormat("Room for %d crew aboard", ROSTER_SIZE[lv]);
        case UP_SONAR: return TextFormat("%d re-scan%s per mission, scans cost %dg", RADAR_REFRESHES[lv], RADAR_REFRESHES[lv] == 1 ? "" : "s", SCAN_COST[lv]);
        default: return TextFormat("The Ward charges %dg per HP", WARD_COST[lv]);
    }
}

void RefreshRadar(Game& g) {
    g.recruits.clear();
    for (int i = 0; i < RECRUIT_BATCH; i++) g.recruits.push_back(MakeRandomHero(g));
    g.radarRefreshes = SonarRefreshCount(g);
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

// Deeper cave levels field tougher versions of the same creatures.
void ScaleEnemyForTier(Enemy& e, int tier) {
    int L = CAVE_TIER_LEVEL[tier];
    e.maxHp = (int)(e.maxHp * (1 + 0.11f * L));
    e.dmgMin = (int)(e.dmgMin * (1 + 0.07f * L) + 0.5f);
    e.dmgMax = (int)(e.dmgMax * (1 + 0.07f * L) + 0.5f);
    e.acc += (3 * L) / 2;
    e.dodge += L / 2;
    e.prot = std::min(50, e.prot + L);
    e.speed += L / 2;
    e.hp = e.maxHp;
}

// ---------------------------------------------------------------- new game
void InitGame(Game& g) {
    // Only the four original hands start aboard; the other eight classes turn up at the Radar Room
    // like any other recruit (MakeRandomHero already draws from every class).
    for (HeroClass c : {HeroClass::Mechanic, HeroClass::Diver, HeroClass::Captain, HeroClass::Nurse})
        g.roster.push_back(MakeHero(g, c));
    // Starting marching order: Mechanic, Diver, Captain, Nurse
    g.party = {{g.roster[0].id, g.roster[1].id, g.roster[2].id, g.roster[3].id}};
    g.relicStorage = {0, 8}; // Wrench, MedKit
    RefreshRadar(g);
    for (int l = 0; l < PL_COUNT; l++) GeneratePlatLayout(g, l);
}
