# DEPTH: the main game (Expeditions)

*This document covers the rank-based roguelike at the heart of Depth. See [1_Depth_Overall.md](1_Depth_Overall.md) for the hub and economy it sits in.*

---

## 1. What it is

An expedition takes a party of **four crew** from the Nautilus into one of four Shallows locations. The party walks a corridor of rooms, fights rank-based battles, loots chests, manages light and nerves, and faces a boss at the end. It plays like *Darkest Dungeon*, but brighter and underwater: marine wonder rather than gothic despair, with Rattled nerves rather than full afflictions.

**Flow:** Helm (choose a location and tier) → corridor → rooms (Fight / Treasure) → after each room, a panel offers *advance / battery / retreat* → the Boss room → Victory (XP, loot, level-ups) or Retreat or Defeat.

Phases (`DPhase`): Corridor, Walking, Combat, RoomClear, Treasure, Victory, Retreat, Defeat.

---

## 2. Art and feel

- **Darkest Dungeon style: 2D with depth.** Every fighter is built from lit 3D primitives (`ShadeLimb`, `ShadeBall`, `ShadeQuad`) inside the figure shader. The shader adds variable-width ink, cross-hatching, cloth folds and an **ink-illustration** finish (black linework, Bayer stipple, hatching over dusky paper tones) that matches the Flats cards. Proportions are chunky and heroic, with upright torsos and arms hanging from the shoulders.
- **Backgrounds:** seven layers of parallax (`dungeon.cpp`) that scroll as the party walks between rooms. Each region's furthest layer is painted by `DrawRegionFar`:
  - Cave: its own water and ridges.
  - Island: an eclipse sunset and palm isles.
  - Weeds: a kelp forest.
  - Atlantis: a drowned city under a pale eye.
- Each run also rolls `visSeed` and `atmos` (location tint, `DrawLocationTint`). `InkPass()` inks the whole scene. Far layers can be blurred for depth of field.
- **Light:** the flashlight really lights the combat scene through the lightmap.
- **Animation:**
  - Every class attacks differently (`HeroAnimFx`): the Nurse's quick slash, the Diver's harpoon lunge, the Captain's overhead cut and the Mechanic's heavy swing, plus throws, shots, heals and buffs.
  - Effects land at the moment of impact (`PendingAction`).
  - Fighters breathe and shift their weight, attacks follow through, hits wobble, and rank changes slide (`ShownX`).
  - Poses go through a per-hero spring (`SpringPose`) that blends and overshoots, with `stride`, `tremble` and `headDown`. Idle posture shows stress and Death's Door.
- **Pacing:**
  - After a room, the advance/battery/retreat panel waits about 1.4 s, slides in, and only accepts clicks once it has settled.
  - A battery swap plays a torch flicker and locks the buttons for 1.4 s.

---

## 3. The crew

### Classes (12)
Base stats: HP / damage min-max / speed / accuracy / dodge / protection / resist.

| Class | HP | Dmg | Spd | Acc | Dodge | Prot | Resist | Identity |
|---|---|---|---|---|---|---|---|---|
| **Nurse** | 22 | 3-6 | 5 | 85 | 10 | 0 | 0 | Healer with a scalpel and toxins |
| **Diver** | 20 | 4-8 | 8 | 90 | 15 | 0 | 0 | Fast harpoon striker, marks prey |
| **Captain** | 24 | 4-7 | 5 | 85 | 8 | 10 | 10 | Leader: cutlass, rallies and buffs |
| **Mechanic** | 30 | 4-7 | 2 | 80 | 5 | 20 | 0 | Tank: wrench, plating, repairs |
| **Whaler** | 22 | 4-8 | 4 | 85 | 8 | 5 | 5 | Back-rank harpooner |
| **Stowaway** | 20 | 3-7 | 6 | 72 | 12 | 0 | 25 | Reckless drunken brawler |
| **Merman** | 26 | 5-10 | 6 | 82 | 10 | 5 | 0 | Savage trident bruiser |
| **Dethroned Island Queen** | 20 | 3-6 | 5 | 85 | 10 | 0 | 15 | Curses and healing balms |
| **Robot** | 32 | 4-7 | 3 | 80 | 3 | 25 | 30 | Steam-powered bulwark |
| **Octopus** | 20 | 2-5 | 9 | 88 | 18 | 0 | 5 | Fastest; ink, snatches, flurries |
| **Siren** | 20 | 2-5 | 6 | 82 | 10 | 0 | 10 | Songs: mesmerise, rally, reposition |
| **Wisp of the Sea** | 18 | 2-5 | 7 | 85 | 20 | 0 | 20 | Light magic: bolts, wards, empower |

### Abilities (8 per class; 4 slotted in `Hero::loadout`)
Abilities 5-8 unlock at levels 1, 1, 2 and 3. Every skill has usable ranks (`fromRanks`) and target ranks. Mechanics include damage, heal, stun, bleed, poison, mark (+25% damage taken), dodge and armor buffs, shoves and pulls, and stress relief.

| Class | Abilities |
|---|---|
| Nurse | Scalpel Slash, Sedative Jab, Field Dressing, Smelling Salts, Triage, Toxin Vial, Adrenaline Shot, Bone Saw |
| Diver | Harpoon Thrust, Twin Strike, Undertow, Riptide Shove, Speargun, Ink Cloud, Mark the Prey, Depth Charge |
| Captain | Cutlass, Rally the Crew, All Hands!, Steady Now, Flintlock, Hold the Line, Grog Ration, Grapeshot |
| Mechanic | Wrench Bash, Rivet Spray, Brace for Impact, Patch the Hull, Blowtorch, Iron Plating, Jury-Rig, Steam Valve |
| Whaler | Harpoon Gun, Boat Hook, Warning Shot, Steady Aim, Chain Harpoon, Barbed Shot, Broadside, Harpoon Volley |
| Stowaway | Wild Swing, Broken Bottle, Foul Breath, Liquid Courage, Poisoned Flask, Stumble, Reckless Brawl, Last Call |
| Merman | Trident Strike, Crushing Grip, Tail Sweep, Tidal Roar, Savage Bite, Riptide Slam, Frenzy, Deep Fury |
| Island Queen | Royal Scepter, Curse of the Fallen Throne, Tend the Faithful, Regal Composure, Binding Curse, Healing Balm, Wrath of the Deposed, Last Decree |
| Robot | Piston Fist, Gear Grind, Reposition Protocol, Overclock, Steam Hammer, Bulwark Mode, Reinforce, Full Steam |
| Octopus | Tentacle Lash, Ink Spray, Quick Snatch, Camouflage, Eight-Armed Flurry, Jet Propulsion, Venomous Bite, Deep Sea Barrage |
| Siren | Enthralling Song, Mesmerize, Rally Cry, Reposition, Siren's Wail, Harmonize, Discordant Screech, Anthem of the Deep |
| Wisp | Glowing Bolt, Paralyzing Light, Guiding Glow, Soothing Aura, Will-o'-Wisp Flare, Empower, Blinding Flash, Radiant Ward |

Crew Quarters show each ability's computed numbers inline (damage range, heal, stun %, accuracy).

### Recruiting and variance
- Recruits come from the **Radar Room** (Sonar scans). Crew names are unique within the roster.
- Each recruit rolls **vigor, might, quickness and fortitude** (-2 to +2). These feed their stats, and `HeroBuildTag()` sums them up in a short read ("Vigorous, Weak-armed").
- Roster size depends on the Bunk Extension: 8 / 9 / 10 / 12.

### Levels and XP
- Crew start at level 0; the maximum is 6. The XP table is 0, 6, 17, 34, 59, 94, 142 (steep).
- XP per win scales hard with depth (`3 + level*3`), so diving deeper is the fast way to level. Level-ups are announced on the results panel.
- Each level adds HP and small stat gains.

---

## 4. Combat rules

- **Ranks:** four hero ranks facing four enemy ranks. Skills can only be used from, and can only reach, particular ranks: melee is usually ranks 1-2, long range ranks 2-4. Shoves and pulls reorder ranks, and rank changes slide smoothly.
- **Turn order** follows speed. An enemy boss may **act twice in a round** (`extraAct`, see below).
- **Stress ("Nerves"):** comes from enemy abilities, crits, low light and more. At 100 a hero becomes **Rattled**: less accurate, easier to hit, and sometimes frozen. The Sick Bay cures it, but the hero sits out the next expedition. Rattled is deliberately gentler than Darkest Dungeon's afflictions.
- **Death's Door:** at 0 HP a hero keeps fighting. Each further hit has a **35% chance to kill**. A dead hero loses their relics.
- **Bleed** refreshes. **Poison** stacks (up to 3 doses) and halves healing received.
- **Region debuffs** (`Status`), one per location:
  - Silt Blindness (Cave): blinds.
  - Totemic Burn (Island): burns.
  - Drowning Entanglement (Weeds): entangles.
  - Eldritch Madness (Atlantis): drives mad.
- **Mark:** the target takes +25% damage. **Dodge** and **armor** buffs also exist.
- **Light (the flashlight):** batteries burn for +40 light. Each room drains light (20 / 16 / 12 / 9 with the Reflector upgrade). Low light means more stress and harder enemy hits, but loot is up to **1.8x** richer. The light really lights the scene.

---

## 5. Rooms, items and loot

- **Rooms** (`RoomType`): **Fight**, **Treasure** (sometimes a locked chest that needs a Key), and **Boss**. There are 3 rooms before the boss, or 4 from cave level 3.
- **The pack** (`DungeonState::inventory`): 5 slots (+2 with a Backpack relic, +0 to 3 with Cargo Netting).
  - **Battery:** burn it for +40 light. These are separate from the batteries stowed on the ship.
  - **Bandage:** patch up a hero on the spot.
  - **Key:** opens a locked chest in a Treasure room.
  - **Relic:** a loose relic. Carried home, it goes to storage.
- Found items must be taken or left. With a full pack, click something in it to discard it.
- The pack is hidden during combat and walking, because the ability bar uses that space. Icons come from `DrawItemIcon` and SVG textures.
- **Loot** is modest (gold is meant to be scarce) and scales with depth and darkness.

---

## 6. Locations and tiers

Four Shallows locations are open from the start. Each has its own ladder of **5 tiers**, played at dungeon levels 0, 1, 3, 5 and 6 (`CAVE_TIER_LEVEL`). Clearing a tier unlocks the next, and earlier tiers stay playable. Deeper tiers scale enemies (`ScaleEnemyForTier`), rooms, loot and XP. The mini-boss chance by tier is 0 / 15 / 30 / 50 / 85%.

| Location | Look | Region debuff | Mini-bosses | Level boss |
|---|---|---|---|---|
| **The Cave** | Flooded grotto, glowing mould and coral | Silt Blindness | The Lobster, Ghost Worm, The Lost Diver | **The Crustacean Queen** |
| **The Island** | Basalt, bone totems, eclipse sunset | Totemic Burn | Tribal Demigod, Coconut Queen | **The Sun God** |
| **The Weeds** | Kelp forest | Drowning Entanglement | Electric Eel, Great White Shark | **Neptune** |
| **Atlantis** | Sunken Greco-Roman city under a pale eye | Eldritch Madness | Armored Lost One, Alien Horror | **Cthulhu** (the final boss) |

---

## 7. Enemies (bestiary)

Every enemy has a rich drawing in `enemyart.cpp` (`DrawRichEnemy`: a shared kit of two-segment limbs, claws, plates, barnacles, ragged hems, rivets, scars and hatching; lit parts inside the figure shader; all enemies face left).

### The Cave (crustaceans and the drowned)
| Enemy | Abilities |
|---|---|
| Sea Louse | Nibble (melee, +stress), Skitter Bite (any rank, bleed) |
| Pistol Shrimp | Snap Claw (melee, 20% stun), Sonic Pop (any rank, heavy stress) |
| Brine Worm | Toxic Spit (poison 3), Coil (melee) |
| Dysformed Crustacean | Pincer Snap (defense down), Crushing Clamp (bleed, 30% stun) |
| *Mini:* **The Lobster** | Crushing Claw (30% stun), Tail Sweep (AoE), Clack Clack (AoE stress). Acts twice 30% of the time |
| *Mini:* **Ghost Worm** | Phasmic Toxins (poison, attack down), Terror Screech (AoE pull, stress). 50% |
| *Mini:* **The Lost Diver** | Anchor Swing (bleed 4, 40% stun), Pressure Vent (speed down). 45% |
| *Boss:* **The Crustacean Queen** | Tidal Crush (bleed, 35% stun), Spawning Surge (heals and summons a Pistol Shrimp), Abyssal Roar (AoE, attack down). 40% |

### The Island (the tribe)
| Enemy | Abilities |
|---|---|
| Tribal Spearman | Jagged Thrust (bleed 3), Shield Bash (40% stun) |
| War Dog | Rabid Savage (bleed, speed down) |
| Tribal Shaman | Toxic Dart (poison, Totemic Burn), Ancestor Chant (heal and buff allies), Grasping Vines (pulls ranks 3-4) |
| *Mini:* **Tribal Demigod** | Idol Slam (50% stun), Roar of the Ancestors (AoE pull, defense down). 45% |
| *Mini:* **Coconut Queen** | Curse of the Reef (2 targets), Royal Decree (heal allies, defense buff). 5% |
| *Boss:* **The Sun God** | Solar Cleave (AoE burn), Wrath of the Sun (40% stun), Aegis of Gold (defense buff, cleanse). 5% |

### The Weeds (merfolk and predators)
| Enemy | Abilities |
|---|---|
| Feral Merman | Gutting Claw (bleed), Thrasher Strike (speed down, entangle) |
| Siren | Alluring Song (pulls and stuns the back ranks), Dissonant Wail (attack down), Soothing Tide (heal) |
| Giant Octopus | Tentacle Constrain (pull 3), Ink Jet (AoE accuracy down, stress) |
| *Mini:* **Electric Eel** | Voltaic Burst (40% stun, slow), Coil Whip (bleed). 50% |
| *Mini:* **Great White Shark** | Feeding Frenzy (bleed 4, self attack buff), Thrash (35% stun). 20% |
| *Boss:* **Neptune** | Trident Impale (bleed, defense down), Maelstrom Call (AoE pull, stress), Ocean's Blessing (heal and buff allies). 13% |

### Atlantis (the Lost Ones and what they worship)
| Enemy | Abilities |
|---|---|
| Lost One Infantry | Rusted Gladius (bleed), Phalanx Slam (35% stun, defense buff) |
| Lost One Cultist | Void Chant (madness, poison), Siphon Offering (drain) |
| *Mini:* **Armored Lost One** (2 ranks) | Titan Shield Crush (2x damage, 40% stun), Immovable Wall. 50% |
| *Mini:* **Alien Horror** (2 ranks) | Spatial Distortion (AoE pull, stress), Mind Rend. 45% |
| *Boss:* **Cthulhu** (3 ranks: wings, a tentacled face, a sigil-holding arm, a claw full of broken column) | Gaze of the Abyss (AoE madness, stress 10), Cosmic Crush (bleed, 40% stun), Siphon Reality (heal, attack buff). 10% |

### Boss rules
- **Bosses fill several ranks** (`Enemy::span`: level boss 3, mini-boss 2). A boss is still one enemy, hittable and able to act from any rank it fills. A level boss leaves room for one retainer, and bosses can't be shoved.
- **Extra actions:** the percentages above are the chance of a second action in a round. Bonus turns can't chain, and the chance shrinks at deeper tiers (`EXTRA_ACT_BY_TIER`).
- **Balance targets (crew level 0):** every mini-boss and level boss wins 66-85%. Cthulhu is about 20%.

---

## 8. Relics

Each hero carries **two relics**, and at most **one HARD WEAPON**. Relics are bought at the Radar Room (145-320 gold) or found. They're lost if the hero dies. The rank-1 hero's relics also shape parkour runs (pickup radius, run speed, lamp).

| Relic | Effect | Price |
|---|---|---|
| Wrench | +6 Protection, +5% crit | 170 |
| Pipe Wrench | +1 Damage, -1 Speed; 25% stun vs armoured foes | 165 |
| Monkey Wrench | +4 Accuracy, +3 Dodge; +30% vs constructs | 170 |
| Rivet Gun (HW) | +2 Damage, -4 Accuracy, ignores 20% armour | 210 |
| Sword (HW) | +1 Damage, +3 Accuracy, +1 Speed | 195 |
| Butcher Knife (HW) | +1 Damage, +2 Speed, 35% bleed | 200 |
| Flintlock (HW) | +1 Damage, +8 Accuracy, +6% crit | 200 |
| Six-Shooter (HW) | +1 Damage, +1 Speed, 20% second shot | 220 |
| MedKit | +5 Max HP, heals +2 | 170 |
| Syringe | +2 Speed, heals +1 | 195 |
| Pliers | 10% stress resist; hits can draw out bleed or poison | 180 |
| Backpack | +2 pack slots, +3 Max HP, 5% stress resist | 205 |
| Power Sword (HW) | +3 Damage, +1 Speed, +4% crit, -5 Dodge | 320 |
| Pickaxe (HW) | +2 Damage, -2 Accuracy, +30% vs armoured foes | 220 |
| Awakened Lantern | +3 Accuracy, -15% stress; crits steady the party | 240 |
| Tesla Gun (HW) | +1 Damage; 30% of hits arc to the next enemy | 280 |
| Eldritch Staff (HW) | +1 Damage, ignores 30% armour; each blow costs 3 nerves | 260 |
| Dynamite | 12% of blows blast both front enemies (15% go off in hand) | 180 |
| Tesla Crank | +1 Speed, +2 Accuracy; boosts the Tesla Gun | 190 |
| Chemistry Kit | Heals +1 and splash onto the party, 5% stress resist | 230 |

**Synergies** (`CheckRelicSynergies`):
- Rivet Gun + Pliers = *Armour Stripper*.
- Tesla Gun + Crank = *Overcharged Coil*.
- Syringe + Chemistry Kit = *Elixir Line*.
- Eldritch Staff + Awakened Lantern = *Lit Ritual*.

Icons are SVG strings rasterised by a built-in SVG reader. `--relic-test` checks the rules.

---

## 9. Ship upgrades that affect expeditions (Workshop: 400 / 800 / 1400)

| Upgrade | Effect by level (0 / 1 / 2 / 3) |
|---|---|
| Flashlight Reflector | Light drain per room 20 / 16 / 12 / 9 |
| Bunk Extension | Roster 8 / 9 / 10 / 12 |
| Sonar Array | Recruit refreshes 0-3, scan cost 70 / 55 / 40 / 25 |
| Infirmary Gear | Ward cost per HP 9 / 7 / 5 / 4 |
| Cargo Netting | +0-3 pack slots |

---

## 10. Balance references

- `depth.exe --sim N <crewLevel> sensible <tier>` runs the sensible auto-player. Whole expeditions win about 58 / 69 / 60 / 59 / 49% at cave levels 0 / 1 / 3 / 5 / 6. A random player at level 0 wins about 6%.
- `depth.exe --boss <runs> <crewLevel> <tier> <EnemyType>` fights one boss many times.
- Known issue: new abilities make levelled crews noticeably safer. Tune when harder tiers exist.

---

## 11. Roadmap

- The Deep and Abyssal tiers, beyond the Shallows.
- Enemy animation: attack windups, hit recoil, death.
- More per-enemy idle detail.
