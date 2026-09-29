// ============================================================================
//  DEPTH - game data: crew classes, abilities, relics, enemies, leveling.
//  Most balancing happens in this file.
// ============================================================================
#include "game.h"
#include <cstdlib>
#include "relics.h"
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
        case Location::Island: return "the Sun God";
        case Location::Weeds: return "Neptune";
        case Location::Atlantis: return "Cthulhu";
        default: return "the Crustacean Queen";
    }
}
const char* LocationDesc(Location loc) {
    switch (loc) {
        case Location::Island: return "A sunken jungle of basalt and bone totems. Tribes with spears and dogs, a Coconut Queen, a Demigod, and the Sun God. Burns.";
        case Location::Weeds: return "A suffocating kelp forest of mermen, sirens and octopi. An Eel, a Great White, and Neptune. Entangles.";
        case Location::Atlantis: return "A sunken Greco-Roman city of Lost Ones, alien monoliths and something ancient. Cthulhu waits. Drives mad.";
        default: return "A cavern of glowing mould and sharp coral. Crustaceans, worms and shrimp; the Lobster, a Ghost Worm, a Lost Diver, and the Queen. Blinds.";
    }
}

// ---------------------------------------------------------------- palettes and light rigs (Master Reference)
// Five muted base tones, dark to light (umbers, slates, sea-greens), and two saturated accents for blood, glow,
// gold and eyes. InkPass pulls low-saturation colour toward these hues.
const Palette& LocationPalette(Location loc) {
    static const Palette P[LOCATION_COUNT] = {
        // the Cave: slate-teal water, wet umber rock, bone; accents: mould glow and coral blood
        {{{14, 24, 28, 255}, {34, 56, 60, 255}, {70, 88, 84, 255}, {120, 128, 112, 255}, {196, 190, 164, 255}}, {{96, 214, 206, 255}, {196, 64, 52, 255}}},
        // the Island: basalt, ochre, dried palm, sun-bleached bone; accents: eclipse fire and totem red
        {{{24, 18, 20, 255}, {64, 44, 36, 255}, {118, 88, 60, 255}, {170, 140, 96, 255}, {226, 204, 160, 255}}, {{250, 150, 50, 255}, {176, 40, 36, 255}}},
        // the Weeds: deep kelp green, olive, sand; accents: siren pink and ray glow
        {{{12, 24, 18, 255}, {30, 58, 40, 255}, {68, 96, 62, 255}, {128, 138, 96, 255}, {200, 196, 150, 255}}, {{236, 120, 140, 255}, {120, 230, 200, 255}}},
        // Atlantis: drowned marble, verdigris, void; accents: void violet and the eye's pale gold
        {{{14, 14, 24, 255}, {38, 42, 58, 255}, {80, 96, 100, 255}, {140, 150, 140, 255}, {210, 206, 190, 255}}, {{190, 120, 255, 255}, {236, 214, 140, 255}}},
    };
    return P[std::clamp((int)loc, 0, LOCATION_COUNT - 1)];
}
const Palette& SalonPalette() {
    // mahogany, brass-dark, sea-blue shadow, lamp-lit paper; accents: brass and the window's blue
    static const Palette P{{{20, 16, 18, 255}, {58, 36, 28, 255}, {110, 76, 50, 255}, {176, 136, 90, 255}, {232, 212, 170, 255}}, {{226, 176, 80, 255}, {90, 170, 210, 255}}};
    return P;
}
SceneLight LocationLight(Location loc, float light01) {
    SceneLight l;
    float L = std::clamp(light01, 0.0f, 1.0f);
    const Palette& p = LocationPalette(loc);
    auto mix = [](Color a, Color b, float k) { return Color{(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; };
    l.keyDir = {-0.62f, -0.78f};                 // the flashlight, carried by the party on the left, raised
    l.key = mix(Color{255, 214, 160, 255}, Color{255, 238, 206, 255}, L);
    l.fill = mix(p.base[1], p.base[2], 0.5f);
    l.rim = mix(p.accent[0], Color{200, 230, 236, 255}, 0.45f);
    l.fog = p.base[1];
    l.keyAmt = 0.6f + 0.4f * L;
    l.fillAmt = 0.35f + 0.65f * L;              // at low light the fill drops...
    l.rimAmt = 1.0f + 0.9f * (1 - L);           // ...the rim rises...
    l.fog.a = (unsigned char)(26 + 40 * (1 - L)); // ...and the fog closes in
    return l;
}
SceneLight SalonLight() {
    SceneLight l;
    l.keyDir = {-0.45f, -0.89f};                 // oil lamps overhead, a little to the left
    l.key = {255, 214, 150, 255};
    l.fill = {90, 150, 196, 255};                // the sea's blue through the great window
    l.rim = {255, 190, 110, 255};                // candlelight from the card table
    l.fog = {40, 52, 64, 40};
    l.keyAmt = 1.0f; l.fillAmt = 0.9f; l.rimAmt = 0.8f;
    return l;
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
    // this individual's own build: vigor (HP), might (damage), quickness (speed & dodge), fortitude (protection & nerve)
    s.maxHp += h.vigor * 3;
    s.dmgMin += h.might; s.dmgMax += h.might * 2;
    s.speed += h.quickness; s.dodge += h.quickness * 3;
    s.prot += h.fortitude * 4; s.stressResist += h.fortitude * 5;
    for (int r : h.relics) {
        if (r < 0) continue;
        const RelicDef& d = Relics()[r];
        s.maxHp += d.hp; s.dmgMin += d.dmg; s.dmgMax += d.dmg; s.speed += d.speed;
        s.acc += d.acc; s.dodge += d.dodge; s.prot += d.prot; s.stressResist += d.stressResist;
    }
    { RelicFx b = RelicBundle(h); s.dmgMin += b.dmg; s.dmgMax += b.dmg; } // synergy damage (Tesla Gun + Crank)
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
// The relics live in relics.cpp (the registry, the equip rules, the synergies and the icons).
const std::vector<RelicDef>& Relics() { return RelicRegistry::All(); }

// ---------------------------------------------------------------- heroes
static const char* NAMES[] = {"Abernathy", "Beatrix", "Cormac",   "Delphine", "Ezra",   "Fitz",
                              "Greta",     "Hollis",  "Isolde",   "Jasper",   "Kit",    "Lorelei",
                              "Mabel",     "Nils",    "Ottoline", "Percival", "Quincy", "Rosalind",
                              "Silas",     "Tamsin",  "Ulric",    "Violet",   "Wendell", "Yara"};

Hero MakeHero(Game& g, HeroClass c) {
    Hero h;
    h.id = g.nextHeroId++;
    h.cls = c;
    for (int tries = 0; tries < 60; tries++) { // a crew of distinct names
        h.name = NAMES[GetRandomValue(0, (int)(sizeof(NAMES) / sizeof(NAMES[0])) - 1)];
        bool taken = false;
        for (const Hero& o : g.roster) taken |= o.name == h.name;
        if (!taken) break;
    }
    h.vigor = GetRandomValue(-2, 2); h.might = GetRandomValue(-2, 2); h.quickness = GetRandomValue(-2, 2); h.fortitude = GetRandomValue(-2, 2);
    h.hp = GetStats(h).maxHp;
    return h;
}

// A one- or two-word read on a recruit's build, for the roster and radar screens: which axes stand out, and by how much.
std::string HeroBuildTag(const Hero& h) {
    struct Axis { int v; const char* hi; const char* lo; };
    Axis axes[4] = {{h.vigor, "Vigorous", "Frail"}, {h.might, "Mighty", "Weak-armed"}, {h.quickness, "Quick", "Sluggish"}, {h.fortitude, "Steady", "Nervy"}};
    int best = 0;
    for (int i = 1; i < 4; i++) if (std::abs(axes[i].v) > std::abs(axes[best].v)) best = i;
    if (axes[best].v == 0) return "Balanced";
    std::string tag = axes[best].v > 0 ? axes[best].hi : axes[best].lo;
    for (int i = 0; i < 4; i++) if (i != best && std::abs(axes[i].v) >= 2) tag += std::string(", ") + (axes[i].v > 0 ? axes[i].hi : axes[i].lo);
    return tag;
}

Hero MakeRandomHero(Game& g) {
    return MakeHero(g, (HeroClass)GetRandomValue(0, (int)HeroClass::COUNT - 1));
}

// Level 0-6. Values are total XP needed to reach each level.
// Each level costs more than the last (deltas 6, 11, 17, 25, 35, 48), so a crew that keeps farming the shallowest
// water stalls out: XP per win scales with how deep the cave tier actually is (see ApplyResults), so the fastest way
// to keep levelling is to take the crew somewhere harder, not to grind the same easy room.
static const int XP_TABLE[7] = {0, 6, 17, 34, 59, 94, 142};

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
static const int CARGO_SLOTS[4]      = {0,  1,  2,  3};

int MaxRoster(const Game& g) { return ROSTER_SIZE[g.upgrades[UP_BUNKS]]; }
int SonarRefreshCount(const Game& g) { return RADAR_REFRESHES[g.upgrades[UP_SONAR]]; }
int ScanCost(const Game& g) { return SCAN_COST[g.upgrades[UP_SONAR]]; }
int WardCostPerHp(const Game& g) { return WARD_COST[g.upgrades[UP_INFIRMARY]]; }
int LightDrainPerRoom(const Game& g) { return LIGHT_DRAIN[g.upgrades[UP_REFLECTOR]]; }
int CargoBonusSlots(const Game& g) { return CARGO_SLOTS[g.upgrades[UP_CARGO]]; }
int UpgradePrice(int level) { return level == 1 ? 400 : level == 2 ? 800 : 1400; } // gold should stay scarce: parkour and Flats are meant to fill the gap

const char* UpgradeName(int u) {
    switch (u) {
        case UP_REFLECTOR: return "Flashlight Reflector";
        case UP_BUNKS: return "Bunk Extension";
        case UP_SONAR: return "Sonar Array";
        case UP_CARGO: return "Cargo Netting";
        default: return "Infirmary Gear";
    }
}

const char* UpgradeDesc(int u, int lv) {
    switch (u) {
        case UP_REFLECTOR: return TextFormat("Each room drains %d light", LIGHT_DRAIN[lv]);
        case UP_BUNKS: return TextFormat("Room for %d crew aboard", ROSTER_SIZE[lv]);
        case UP_SONAR: return TextFormat("%d re-scan%s per mission, scans cost %dg", RADAR_REFRESHES[lv], RADAR_REFRESHES[lv] == 1 ? "" : "s", SCAN_COST[lv]);
        case UP_CARGO: return TextFormat("+%d pack slot%s on expeditions", CARGO_SLOTS[lv], CARGO_SLOTS[lv] == 1 ? "" : "s");
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
    if (hits == MELEE_HITS) a.fromRanks = MELEE_FROM;   // melee: from ranks 1-2 only, onto ranks 1-2 only
    return a;
}
static EnemyAbility Melee(const char* n, float mult) { return EA(n, MELEE_HITS, mult); }
static EnemyAbility Long(const char* n, float mult) { // long range: from any rank except the front, onto any rank
    EnemyAbility a = EA(n, ANY_RANK, mult);
    a.fromRanks = RANGED_FROM;
    return a;
}
static EnemyAbility Support(const char* n) { // a heal, buff or command: no damage, usable from anywhere
    EnemyAbility a = EA(n, ANY_RANK, 0.0f);
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
        // ------------------------------------------------ the Island
        case EnemyType::TribalSpearman: {
            e.name = "Tribal Spearman";
            e.maxHp = 15; e.dmgMin = 4; e.dmgMax = 6; e.speed = 6; e.acc = 80; e.dodge = 10;
            EnemyAbility a = Melee("Jagged Thrust", 1.0f); a.bleed = 3; e.abilities.push_back(a);
            a = Melee("Shield Bash", 0.7f); a.stunChance = 40; e.abilities.push_back(a);
        } break;
        case EnemyType::WarDog: {
            e.name = "War Dog";
            e.maxHp = 12; e.dmgMin = 3; e.dmgMax = 5; e.speed = 9; e.acc = 85; e.dodge = 20;
            EnemyAbility a = Melee("Rabid Savage", 1.0f); a.bleed = 2; a.weakSpd = 30; e.abilities.push_back(a);
        } break;
        case EnemyType::TribalShaman: {
            e.name = "Tribal Shaman";
            e.maxHp = 12; e.dmgMin = 2; e.dmgMax = 4; e.speed = 5; e.acc = 80; e.dodge = 10;
            EnemyAbility a = Long("Toxic Dart", 0.6f); a.poison = 2; a.region = 1; e.abilities.push_back(a);
            a = Support("Ancestor Chant"); a.healLowest = 6; a.buffAllyAtk = 25; e.abilities.push_back(a);
            a = Support("Grasping Vines"); a.pull = 1; a.hits = RANK_3 | RANK_4; e.abilities.push_back(a);
        } break;
        case EnemyType::TribalDemigod: {
            e.name = "Tribal Demigod"; e.boss = true; e.tier = 1;
            e.maxHp = 76; e.dmgMin = 8; e.dmgMax = 12; e.speed = 3; e.acc = 85; e.dodge = 5; e.prot = 15;
            EnemyAbility a = Melee("Idol Slam", 1.6f); a.stunChance = 50; e.abilities.push_back(a);
            a = Support("Roar of the Ancestors"); a.pull = 2; a.aoe = true; a.dmgMult = 0.2f; a.weakDef = 20; a.stress = 8; e.abilities.push_back(a);
        } break;
        case EnemyType::CoconutQueen: {
            e.name = "Coconut Queen"; e.boss = true; e.tier = 1;
            e.maxHp = 42; e.dmgMin = 4; e.dmgMax = 6; e.speed = 5; e.acc = 85; e.dodge = 10; e.prot = 5;
            EnemyAbility a = Long("Curse of the Reef", 0.5f); a.region = 1; a.targetsN = 2; e.abilities.push_back(a);
            a = Support("Royal Decree"); a.healAllies = 8; a.buffAllyDef = 25; e.abilities.push_back(a);
        } break;
        case EnemyType::SunGod: {
            e.name = "The Sun God"; e.boss = true; e.tier = 2;
            e.maxHp = 66; e.dmgMin = 5; e.dmgMax = 8; e.speed = 4; e.acc = 85; e.dodge = 0; e.prot = 15;
            EnemyAbility a = Long("Solar Cleave", 0.8f); a.aoe = true; a.region = 1; e.abilities.push_back(a);
            a = Melee("Wrath of the Sun", 1.4f); a.stunChance = 40; e.abilities.push_back(a);
            a = Support("Aegis of Gold"); a.buffSelfDef = 30; a.cleanse = true; e.abilities.push_back(a);
        } break;
        // ------------------------------------------------ the Cave
        case EnemyType::DysCrustacean: {
            e.name = "Dysformed Crustacean";
            e.maxHp = 18; e.dmgMin = 4; e.dmgMax = 6; e.speed = 4; e.acc = 80; e.dodge = 5; e.prot = 10;
            EnemyAbility a = Melee("Pincer Snap", 1.0f); a.weakDef = 20; e.abilities.push_back(a);
            a = Melee("Crushing Clamp", 1.1f); a.bleed = 3; a.stunChance = 30; e.abilities.push_back(a);
        } break;
        case EnemyType::GhostWorm: {
            e.name = "Ghost Worm"; e.boss = true; e.tier = 1;
            e.maxHp = 50; e.dmgMin = 5; e.dmgMax = 7; e.speed = 6; e.acc = 85; e.dodge = 15; e.prot = 0;
            EnemyAbility a = Long("Phasmic Toxins", 0.7f); a.poison = 3; a.weakAtk = 20; e.abilities.push_back(a);
            a = Support("Terror Screech"); a.pull = 2; a.aoe = true; a.region = 2; a.stress = 6; e.abilities.push_back(a);
        } break;
        case EnemyType::LostDiver: {
            e.name = "The Lost Diver"; e.boss = true; e.tier = 1;
            e.maxHp = 52; e.dmgMin = 5; e.dmgMax = 8; e.speed = 3; e.acc = 80; e.dodge = 0; e.prot = 20;
            EnemyAbility a = Melee("Anchor Swing", 1.3f); a.bleed = 4; a.stunChance = 40; e.abilities.push_back(a);
            a = Long("Pressure Vent", 0.5f); a.region = 2; a.weakSpd = 30; e.abilities.push_back(a);
        } break;
        case EnemyType::CrustaceanQueen: {
            e.name = "The Crustacean Queen"; e.boss = true; e.tier = 2;
            e.maxHp = 62; e.dmgMin = 5; e.dmgMax = 7; e.speed = 3; e.acc = 80; e.dodge = 0; e.prot = 10;
            EnemyAbility a = Melee("Tidal Crush", 1.5f); a.bleed = 3; a.stunChance = 35; e.abilities.push_back(a);
            a = Support("Spawning Surge"); a.healSelf = 4; a.summon = (int)EnemyType::CaveShrimp; e.abilities.push_back(a);
            a = Long("Abyssal Roar", 0.0f); a.aoe = true; a.region = 2; a.weakAtk = 20; a.stress = 4; e.abilities.push_back(a);
        } break;
        // ------------------------------------------------ the Weeds
        case EnemyType::FeralMerman: {
            e.name = "Feral Merman";
            e.maxHp = 15; e.dmgMin = 4; e.dmgMax = 6; e.speed = 8; e.acc = 85; e.dodge = 15;
            EnemyAbility a = Melee("Gutting Claw", 1.0f); a.bleed = 3; e.abilities.push_back(a);
            a = Melee("Thrasher Strike", 0.8f); a.weakSpd = 30; a.region = 3; e.abilities.push_back(a);
        } break;
        case EnemyType::Siren: {
            e.name = "Siren";
            e.maxHp = 12; e.dmgMin = 2; e.dmgMax = 4; e.speed = 7; e.acc = 85; e.dodge = 20;
            EnemyAbility a = Support("Alluring Song"); a.pull = 1; a.hits = RANK_3 | RANK_4; a.stunChance = 100; e.abilities.push_back(a);
            a = Long("Dissonant Wail", 0.5f); a.weakAtk = 20; a.region = 3; e.abilities.push_back(a);
            a = Support("Soothing Tide"); a.healLowest = 6; e.abilities.push_back(a);
        } break;
        case EnemyType::GiantOctopus: {
            e.name = "Giant Octopus";
            e.maxHp = 20; e.dmgMin = 3; e.dmgMax = 5; e.speed = 4; e.acc = 80; e.dodge = 5; e.prot = 5;
            EnemyAbility a = Support("Tentacle Constrain"); a.pull = 3; a.region = 3; a.aoe = false; e.abilities.push_back(a);
            a = Long("Ink Jet", 0.0f); a.aoe = true; a.weakAcc = 30; a.stress = 5; e.abilities.push_back(a);
        } break;
        case EnemyType::ElectricEel: {
            e.name = "Electric Eel"; e.boss = true; e.tier = 1;
            e.maxHp = 52; e.dmgMin = 5; e.dmgMax = 8; e.speed = 8; e.acc = 85; e.dodge = 15;
            EnemyAbility a = Long("Voltaic Burst", 0.8f); a.stunChance = 40; a.weakSpd = 25; e.abilities.push_back(a);
            a = Melee("Coil Whip", 1.2f); a.bleed = 3; e.abilities.push_back(a);
        } break;
        case EnemyType::GreatWhite: {
            e.name = "Great White Shark"; e.boss = true; e.tier = 1;
            e.maxHp = 55; e.dmgMin = 6; e.dmgMax = 9; e.speed = 7; e.acc = 85; e.dodge = 5; e.prot = 10;
            EnemyAbility a = Melee("Feeding Frenzy", 1.4f); a.bleed = 4; a.buffSelfAtk = 20; e.abilities.push_back(a);
            a = Melee("Thrash", 1.0f); a.stunChance = 35; e.abilities.push_back(a);
        } break;
        case EnemyType::Neptune: {
            e.name = "Neptune"; e.boss = true; e.tier = 2;
            e.maxHp = 70; e.dmgMin = 5; e.dmgMax = 8; e.speed = 4; e.acc = 85; e.dodge = 0; e.prot = 15;
            EnemyAbility a = Melee("Trident Impale", 1.4f); a.bleed = 3; a.weakDef = 20; e.abilities.push_back(a);
            a = Support("Maelstrom Call"); a.pull = 2; a.aoe = true; a.region = 3; a.stress = 6; e.abilities.push_back(a);
            a = Support("Ocean''s Blessing"); a.healAllies = 8; a.buffAllyAtk = 25; e.abilities.push_back(a);
        } break;
        // ------------------------------------------------ Atlantis
        case EnemyType::LostInfantry: {
            e.name = "Lost One Infantry";
            e.maxHp = 18; e.dmgMin = 4; e.dmgMax = 6; e.speed = 5; e.acc = 80; e.dodge = 5; e.prot = 15;
            EnemyAbility a = Melee("Rusted Gladius", 1.0f); a.bleed = 3; e.abilities.push_back(a);
            a = Melee("Phalanx Slam", 0.8f); a.stunChance = 35; a.buffSelfDef = 20; e.abilities.push_back(a);
        } break;
        case EnemyType::LostCultist: {
            e.name = "Lost One Cultist";
            e.maxHp = 12; e.dmgMin = 2; e.dmgMax = 4; e.speed = 6; e.acc = 85; e.dodge = 15;
            EnemyAbility a = Long("Void Chant", 0.6f); a.region = 4; a.poison = 2; e.abilities.push_back(a);
            a = Support("Siphon Offering"); a.drain = true; e.abilities.push_back(a);
        } break;
        case EnemyType::ArmorLostOne: {
            e.name = "Armored Lost One"; e.boss = true; e.tier = 1;
            e.maxHp = 84; e.dmgMin = 9; e.dmgMax = 12; e.speed = 3; e.acc = 80; e.dodge = 0; e.prot = 30;
            EnemyAbility a = Melee("Titan Shield Crush", 2.0f); a.stunChance = 40; a.weakAtk = 20; e.abilities.push_back(a);
            a = Support("Immovable Wall"); a.selfMove = -99; a.dmgMult = 0.8f; a.hits = MELEE_HITS; a.buffSelfDef = 30; e.abilities.push_back(a);
        } break;
        case EnemyType::AlienHorror: {
            e.name = "Alien Horror"; e.boss = true; e.tier = 1;
            e.maxHp = 64; e.dmgMin = 6; e.dmgMax = 9; e.speed = 7; e.acc = 90; e.dodge = 20;
            EnemyAbility a = Support("Spatial Distortion"); a.pull = 2; a.aoe = true; a.dmgMult = 0.2f; a.region = 4; a.stress = 8; e.abilities.push_back(a);
            a = Long("Mind Rend", 0.95f); a.region = 4; a.weakAcc = 20; e.abilities.push_back(a);
        } break;
        case EnemyType::Cthulhu: {
            e.name = "Cthulhu"; e.boss = true; e.tier = 2;
            e.maxHp = 96; e.dmgMin = 5; e.dmgMax = 9; e.speed = 5; e.acc = 90; e.dodge = 0; e.prot = 15;
            EnemyAbility a = Long("Gaze of the Abyss", 0.6f); a.aoe = true; a.region = 4; a.stress = 10; e.abilities.push_back(a);
            a = Melee("Cosmic Crush", 1.6f); a.bleed = 3; a.stunChance = 40; e.abilities.push_back(a);
            a = Support("Siphon Reality"); a.healSelf = 10; a.buffSelfAtk = 20; e.abilities.push_back(a);
        } break;
        default: break;    }
    e.hp = e.maxHp;
    switch (t) { // bosses can strike twice in a round: the chance (percent) is tuned per boss
        case EnemyType::Lobster: e.extraAct = 30; break;
        case EnemyType::GhostWorm: e.extraAct = 50; break;
        case EnemyType::LostDiver: e.extraAct = 45; break;
        case EnemyType::CrustaceanQueen: e.extraAct = 40; break;
        case EnemyType::TribalDemigod: e.extraAct = 45; break;
        case EnemyType::CoconutQueen: e.extraAct = 5; break;
        case EnemyType::SunGod: e.extraAct = 5; break;
        case EnemyType::ElectricEel: e.extraAct = 50; break;
        case EnemyType::GreatWhite: e.extraAct = 20; break;
        case EnemyType::Neptune: e.extraAct = 13; break;
        case EnemyType::ArmorLostOne: e.extraAct = 50; break;
        case EnemyType::AlienHorror: e.extraAct = 45; break;
        case EnemyType::Cthulhu: e.extraAct = 10; break;
        default: break;
    }
    if (const char* sc = getenv("DEPTH_EXTRA")) e.extraAct = (int)(e.extraAct * atof(sc)); // developer knob for tuning runs
    e.span = e.tier == 2 ? 3 : e.boss ? 2 : 1; // bosses are big: a level boss fills three ranks, a mini-boss two
    return e;
}

// ---------------------------------------------------------------- region bestiaries
std::vector<EnemyType> LocationStandards(Location loc) {
    switch (loc) {
        case Location::Island: return {EnemyType::TribalSpearman, EnemyType::WarDog, EnemyType::TribalSpearman};
        case Location::Weeds: return {EnemyType::FeralMerman, EnemyType::FeralMerman, EnemyType::GiantOctopus};
        case Location::Atlantis: return {EnemyType::LostInfantry, EnemyType::LostInfantry};
        default: return {EnemyType::DysCrustacean, EnemyType::CaveShrimp, EnemyType::BrineWorm, EnemyType::SeaLouse};
    }
}
std::vector<EnemyType> LocationSupports(Location loc) {
    switch (loc) {
        case Location::Island: return {EnemyType::TribalShaman};
        case Location::Weeds: return {EnemyType::Siren, EnemyType::GiantOctopus};
        case Location::Atlantis: return {EnemyType::LostCultist};
        default: return {EnemyType::CaveShrimp};
    }
}
std::vector<EnemyType> LocationMinis(Location loc) {
    switch (loc) {
        case Location::Island: return {EnemyType::TribalDemigod, EnemyType::CoconutQueen};
        case Location::Weeds: return {EnemyType::ElectricEel, EnemyType::GreatWhite};
        case Location::Atlantis: return {EnemyType::ArmorLostOne, EnemyType::AlienHorror};
        default: return {EnemyType::Lobster, EnemyType::GhostWorm, EnemyType::LostDiver};
    }
}
EnemyType LocationLevelBoss(Location loc) {
    switch (loc) {
        case Location::Island: return EnemyType::SunGod;
        case Location::Weeds: return EnemyType::Neptune;
        case Location::Atlantis: return EnemyType::Cthulhu;
        default: return EnemyType::CrustaceanQueen;
    }
}
// Mini-boss encounters at a depth level: 0, 1, 3, 5, 6 -> 0%, 15%, 30%, 50%, 85%.
int MiniBossChance(int levelValue) {
    switch (levelValue) {
        case 0: return 0;
        case 1: return 15;
        case 3: return 30;
        case 5: return 50;
        default: return 85;
    }
}
const char* RegionDebuffName(Location loc) {
    switch (loc) {
        case Location::Island: return "Totemic Burn";
        case Location::Weeds: return "Drowning Entanglement";
        case Location::Atlantis: return "Eldritch Madness";
        default: return "Silt Blindness";
    }
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
    static const float EXTRA_ACT_BY_TIER[CAVE_TIERS] = {1.0f, 0.6f, 0.35f, 0.25f, 0.15f}; // deeper bosses already hit harder and last longer
    e.extraAct = (int)(e.extraAct * EXTRA_ACT_BY_TIER[std::clamp(tier, 0, CAVE_TIERS - 1)] + 0.5f);
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
    // platform layouts are generated (and validated, which takes a moment) the first time each level is started
}

// ---------------------------------------------------------------- the cue registry (Master Reference, "Sound design")
// Every sound outside the parkour section is a named cue: bus, recipe, base frequency, length, gain, pitch
// variation, filter/length variants, priority (0-3, low dropped first), max at once, and whether it ducks the music.
#include "sound.h"
const CueDef* CueTable(int& count) {
    static const CueDef C[] = {
        // --- UI
        {"ui.hover", CB_UI, CR_TICK, 2400, 0.03f, 0.08f, 0.06f, 4, 0, 2, false},
        {"ui.click", CB_UI, CR_CLICK, 1500, 0.06f, 0.16f, 0.05f, 3, 1, 2, false},
        {"ui.confirm", CB_UI, CR_CONFIRM, 660, 0.35f, 0.14f, 0.03f, 3, 2, 2, false},
        {"ui.cancel", CB_UI, CR_CANCEL, 440, 0.25f, 0.12f, 0.03f, 3, 1, 2, false},
        {"ui.error", CB_UI, CR_ERROR, 110, 0.25f, 0.3f, 0.04f, 3, 2, 1, false},
        {"ui.levelup", CB_UI, CR_FANFARE, 262, 1.4f, 0.16f, 0.0f, 1, 3, 1, true},
        {"ui.chalk", CB_UI, CR_CHALK, 3000, 0.25f, 0.12f, 0.1f, 5, 0, 3, false},
        {"ui.drag", CB_UI, CR_TICK, 900, 0.05f, 0.1f, 0.06f, 3, 0, 2, false},
        {"ui.drop", CB_UI, CR_THUD, 160, 0.16f, 0.3f, 0.05f, 3, 1, 2, false},
        // --- the salon's stations: hover, open, embark
        {"hub.plaque", CB_UI, CR_PLAQUE, 1800, 0.18f, 0.1f, 0.05f, 4, 0, 1, false},
        {"hub.panel", CB_UI, CR_WHOOSH, 500, 0.35f, 0.16f, 0.06f, 3, 2, 1, false},
        {"hub.latch", CB_UI, CR_LATCH, 1200, 0.3f, 0.2f, 0.05f, 4, 2, 1, false},
        {"hub.wheel", CB_SFX, CR_CREAK, 220, 0.7f, 0.14f, 0.08f, 5, 1, 1, false},
        {"hub.sonar", CB_SFX, CR_SONAR, 1100, 1.6f, 0.12f, 0.02f, 3, 1, 1, false},
        {"hub.organ", CB_SFX, CR_ORGAN2, 146.8f, 1.6f, 0.14f, 0.0f, 3, 1, 1, false},
        {"hub.candle", CB_SFX, CR_FLARE, 600, 0.5f, 0.14f, 0.08f, 4, 1, 1, false},
        {"hub.jelly", CB_SFX, CR_JELLYHUM, 180, 1.2f, 0.1f, 0.04f, 3, 1, 1, false},
        {"hub.gauge", CB_SFX, CR_GAUGE, 3000, 0.2f, 0.12f, 0.1f, 4, 0, 2, false},
        {"hub.hatch", CB_SFX, CR_WHEEL, 700, 0.8f, 0.16f, 0.06f, 3, 1, 1, false},
        {"hub.rungs", CB_SFX, CR_RUNGS, 380, 0.9f, 0.16f, 0.06f, 3, 1, 1, false},
        {"hub.door", CB_SFX, CR_DOOR, 180, 0.8f, 0.14f, 0.06f, 3, 1, 1, false},
        {"hub.ladder", CB_SFX, CR_LADDER, 300, 0.6f, 0.12f, 0.06f, 3, 1, 1, false},
        {"hub.gear", CB_SFX, CR_GEAR, 900, 0.7f, 0.1f, 0.06f, 3, 1, 1, false},
        {"hub.periscope", CB_SFX, CR_CREAK, 330, 0.6f, 0.12f, 0.06f, 3, 1, 1, false},
        {"hub.token", CB_SFX, CR_TOKEN, 1400, 0.9f, 0.2f, 0.04f, 3, 2, 1, false},
        {"hub.embark", CB_SFX, CR_SPOOL, 60, 2.6f, 0.26f, 0.02f, 2, 3, 1, true},
        {"hub.tilt", CB_SFX, CR_TILT, 90, 2.0f, 0.2f, 0.05f, 3, 2, 1, false},
        // --- the salon's ambience events
        {"amb.clock", CB_AMB, CR_CLOCK, 2200, 0.05f, 0.05f, 0.02f, 2, 0, 2, false},
        {"amb.creak", CB_AMB, CR_CREAK, 110, 1.4f, 0.07f, 0.12f, 6, 0, 2, false},
        {"amb.step", CB_AMB, CR_STEP, 120, 0.12f, 0.08f, 0.1f, 5, 0, 4, false},
        {"amb.organbreath", CB_AMB, CR_WHOOSH, 180, 2.6f, 0.05f, 0.05f, 3, 0, 1, false},
        // --- instruments the scores use as one-shots (checked by --audio-test like every other cue)
        {"mus.pluck", CB_MUSIC, CR_PLUCK, 440, 1.2f, 0.1f, 0.0f, 3, 1, 8, false},
        {"mus.bell", CB_MUSIC, CR_BELL, 880, 1.4f, 0.08f, 0.0f, 3, 1, 8, false},
        {"mus.drum", CB_MUSIC, CR_DRUM, 70, 0.4f, 0.2f, 0.03f, 3, 1, 4, false},
    };
    count = (int)(sizeof(C) / sizeof(C[0]));
    return C;
}

// ---------------------------------------------------------------- the sonar chart's numbers, per tier (Master Reference)
//   rooms, corridor segments (min, max), fights on the shortest route, loops, curios (rooms and corridors), rest rooms
const ChartParams& ChartParamsFor(int tier) {
    static const ChartParams P[CAVE_TIERS] = {
        {6, 2, 2, 2, 1, 2, 1},    // cave level 0
        {8, 2, 3, 3, 1, 3, 1},    // level 1
        {10, 3, 3, 3, 2, 4, 1},   // level 3
        {12, 3, 4, 4, 2, 5, 2},   // level 5
        {14, 4, 4, 4, 3, 6, 2},   // level 6
    };
    return P[std::clamp(tier, 0, CAVE_TIERS - 1)];
}
const char* ObjectiveName(Objective o) {
    switch (o) {
        case Objective::Chart: return "Chart the depths";
        case Objective::Salvage: return "Salvage";
        case Objective::Cleanse: return "Cleanse";
        default: return "Slay the boss";
    }
}
const char* ObjectiveText(Objective o) {
    switch (o) {
        case Objective::Chart: return "Visit 90% of the rooms. +50% gold and a relic.";
        case Objective::Salvage: return "Open every treasure room. +1 relic.";
        case Objective::Cleanse: return "Win every fight on the map. +40% XP.";
        default: return "Kill the boss. +25% XP.";
    }
}
// the numbers the chart's events use
extern const float CHART_HALLFIGHT = 0.0f, CHART_TRAP = 0.09f, CHART_LOOT = 0.12f; // hallway fights come only from the generator's fewest-fights top-up
extern const int MAX_MINIS_PER_RUN = 2;         // a chart has more fight rooms than the old run: at most two of them hold a mini-boss
extern const float CHART_STRETCH_DRAIN = 0.85f; // a corridor drains what a room did, spread over its stretches (the doc says each stretch drains a room's worth: too harsh on the long deep charts, see docs/MASTER_PROGRESS.md)
const int TRAP_SPOT_CHANCE = 50;       // a Diver or Whaler spots a trap and disarms it
const int REVISIT_AMBUSH = 15;         // a corridor walked again: the chance of an ambush
const int NIGHT_AMBUSH = 20;           // camping at a rest room: the chance of a night ambush
int ChartTrapSpotChance() { return TRAP_SPOT_CHANCE; }
int ChartRevisitAmbush() { return REVISIT_AMBUSH; }
int ChartNightAmbush() { return NIGHT_AMBUSH; }