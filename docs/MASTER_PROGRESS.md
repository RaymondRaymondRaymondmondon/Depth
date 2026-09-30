# Master Reference progress log

The plan is `Depth — Master Reference for Claude Code.pdf` (project root). One stage at a time; each ends at its Definition of Done.

| Stage | Work | Status |
|---|---|---|
| 1 | Shared rig, clips, chains, three-light rig, palettes, --silhouette | Built. User: right direction, a ways to go; flat-plane shading pass done; figures keep improving in stage 5 |
| 2 | Salon rebuilt in depth: stations as props, arcade cabinet, study hatch, roster edge, Embark | Done (hands/dealer move fully onto the rig in stage 5) |
| 3 | Sound architecture: buses, cue registry, reverb, salon music and ambience | Done (--audio-test passes for the salon) |
| 4 | Sonar chart expeditions | Done (--gen-chart 10,000/10,000; --sim within 10 points of the linear baseline at every tier) |
| 5 | Expedition visual overhaul on the rig | In progress: every hero on the rig, HUD, effects, camera; creature rig kit on the Cave; other locations' creatures, backgrounds-as-props and boss phase clips still to do |
| 6 | EnemyBrain | Done (--brain-test: level 0 -4.1 points, level 6 -19.0; boss targets re-tuned) |
| 7 | New expedition systems, the Trench and the Hadal | Done (every system saved, in --sim and --stage7-test; targets met within 6 points in the Cave and the Hadal, deviations logged; the other Shallows are much harder, flagged) |
| 8 | Expedition sound | |
| 9 | Flats visuals and card physics | |
| 10 | Flats long map and systems | |
| 11 | Deep Arcade, networking, Scuttle | |
| 12 | Flats Duel | |
| 13 | The Trawl | |
| 14 | Fathoms | |
| 15 | Arcade sound, final mix | |

## Stage 1 notes (2026-09-29)
- Definition of Done: `--figures` renders every hero and enemy in idle, walk, windup, strike, hit and death; `--silhouette` renders them in black. Every figure has at least one chain and a blink. The light rig and palettes are applied in every location and the salon.
- Found and fixed along the way: ten classes were drawn from broken generated placeholder PNGs in combat (only the Siren and Wisp have real painted art); they are procedural again.
- Only the Captain and the Cultist are fully on the rig; the rest move over in Stage 5 (full clip sets per class).

## Balance changelog
(none yet: Stage 1 changed no numbers)

### Stage 6, EnemyBrain (2026-09-29)
`depth.exe --brain-test -1 2000` (sensible player, crew level = cave level, the old random enemy AI vs EnemyBrain):

| Cave level | Old random AI | EnemyBrain | Change |
|---|---|---|---|
| 0 | 50.8% | 46.7% | -4.1 (gate: within 5) |
| 1 | 48.6% | 42.2% | -6.4 |
| 3 | 59.1% | 44.5% | -14.6 |
| 5 | 42.5% | 27.3% | -15.2 |
| 6 | 36.0% | 17.0% | -19.0 (gate: 15-25) |

Tuning: tier 6 temperature 0.25 -> 0.2 and 4 options looked ahead (at 0.25/3 it was -15.0, on the gate's edge).
Bosses with the brain, crew level 0, cave level 0 (`--boss`): Lobster 75.0, Ghost Worm 70.8, Lost Diver 80.0, Crustacean Queen 76.8, Tribal Demigod 75.2, Coconut Queen 80.2, Sun God 58.5 -> 71.7 (hp 66 -> 60, damage 5-8 -> 5-7), Great White 83.0, Electric Eel 88.2 -> 80.9 (extra action 50 -> 80%), Neptune 64.3 -> 69.7 (hp 70 -> 62), Armored Lost One 72.8, Alien Horror 70.0, Cthulhu 20.8 (target about 20).
Expedition win rates are now under Stage 7's targets (60/65/58/55/45); Stage 7's systems (provisioning, camp skills, the Drill Deck) and its rebalance are where they're met.

### Stage 7 part a: supplies, camp, resolve (2026-09-29)
New: the Quartermaster at the Helm (batteries 15 -> 25 gold; bandage 20, antivenom 20, grog 15, crowbar 30, salt 10; a suggested kit per location), supplies used on curios (a safe good outcome), crowbars on blocked passages and locked chests, bandages/antivenom on the chart screen, camp at rest rooms (5 points of two skills per class, grog, sleep heals 25%), and the resolve check at 100 nerves (25% Steeled: +10 accuracy, +8 dodge, steadies an ally each turn).
Rebalance toward the targets 60/65/58/55/45 (`--sim 1200 <lvl> sensible <tier>`, the auto-player takes the suggested kit):

| Cave level | Before (Stage 6) | With supplies + camp | After tuning | Target |
|---|---|---|---|---|
| 0 | 46.7% | 54.8% | 57.6% | 60 |
| 1 | 42.2% | 49.1% | 58.4% | 65 |
| 3 | 44.5% | 50.6% | 58.2% | 58 |
| 5 | 27.3% | 34.8% | 49.2% | 55 |
| 6 | 17.0% | 24.1% | 41.5% | 45 |

Tuning: tier scaling hp +11% -> +8% per level and damage +7% -> +5% per level; Crustacean Queen's Tidal Crush 1.5x -> 1.35x, stun 35 -> 25%; boss extra-action factor at cave level 1 0.6 -> 0.4 and level 6 0.15 -> 0.1; brain temperature level 1 1.2 -> 1.6, level 6 0.2 -> 0.3; camp points 4 -> 5; sleep heal 15 -> 25%; bandage 7 -> 9. All within 7 points of target; the Drill Deck (later in Stage 7) closes the rest.

### Stage 7 part b: habits, ailments, bonds (2026-09-29)
Crit reactions changed (hero crit -4 -> -3 nerves to the party; an enemy crit now adds 5 to everyone instead of 10 to its target). --sim 1200: 52.4 / 58.0 / 64.9 / 49.2 / 39.8 (targets 60/65/58/55/45; within noise of part a except level 0, -5). Spec deviations: camp points 5 (spec 4), Triage Tent heals 6 (spec 3), both for balance.

### Stage 7 part c: Drill Deck, elites, riposte, retreat (2026-09-29)
Elites 0/0/40/35/55% by cave level (+30% HP, an extra ability); riposte at 70% of a blow; Cowardly flee 35% below half HP. --sim now drills 0/2/1/3/2 slotted abilities once by cave level (SIM_DRILLS_BY_TIER), standing in for a player's spending. --sim 2000:

| Cave level | Result | Target |
|---|---|---|
| 0 | 55.2% | 60 (-4.8) |
| 1 | 63.4% | 65 |
| 3 | 61.9% | 58 |
| 5 | 54.6% | 55 |
| 6 | 44.2% | 45 |

### Stage 7 part e: eight new enemies (2026-09-29)
First pass made the Cave easier (the crab and angler diluted harder standards: 57.9/68.5/66.0/59.4/47.8). Barnacle Crab hp 20 -> 22, damage 3-5 -> 4-7, Crusher 1.1x -> 1.25x, stun 15 -> 20; Lantern Angler hp 13 -> 15, damage 2-4 -> 3-5, Lure Light 0.3x -> 0.5x. --sim 2000: 54.1 / 60.4 / 58.8 / 53.6 / 43.1 (targets 60/65/58/55/45: levels 0 and 1 run 5-6 low, the rest within 2).

### Stage 7 part f: the Trench and the Hadal (2026-09-29)
--sim 1000-1500 <lvl> sensible <tier> <loc> with crews of the matching level:

| Location | Level 5 | Level 6 | Level 7 | Target |
|---|---|---|---|---|
| The Trench | 44.7% | 37.5% | 35.5% | - |
| The Hadal | 33.5% | 25.1% | 31.6% | 35% at level 7 |

Tuning on the way: the Hadal's standards lightened (Star Spawn once, sea lice and Dysformed Crustaceans, supports Drowned Oracle and Lantern Angler, minis Alien Horror, Lost Diver, Ghost Worm; it started at 18%); Star Spawn hp 18 -> 16, damage 3-6 -> 3-5, Starlit Gaze 0.6x -> 0.55x, Unfold Space nerves 5 -> 3; the Abyssal Eye hp 90 -> 72, damage 6-9 -> 6-8, Void Stare 0.9x -> 0.75x; the Leviathan 70 -> 78 hp (86 overshot the Trench by 15 points), extra action 40%. Boss sims at crew level 7: the Eye 83.5% before its softening; the Leviathan 99.7% before its buff.

**Found while testing (not changed, a design question):** the Stage 7 targets were only ever measured in the Cave, and the other Shallows are far harder with the same crew. At level 0: the Island 23%, Atlantis 4% (most wipes at Cthulhu, tuned as the hard final boss). At level 6: the Island 14%, the Weeds 44%, Atlantis 3%.
