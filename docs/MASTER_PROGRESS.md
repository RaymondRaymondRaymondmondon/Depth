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
| 7 | New expedition systems, the Trench and the Hadal | |
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