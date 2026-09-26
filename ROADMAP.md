# Depth: visual roadmap (Darkest Dungeon direction)

## Rules for every sprite and screen
- Thick black ink outline (the figure shader adds it). Shadows are solid black blocks on the side away from the
  key light (lantern, sub headlight): never soft gradients. `BlockShadow` in render.cpp is the enemy version.
- Eyes hidden: under masks, hoods, helmets, goggles, brow bars. Rugged, top-heavy proportions; rust, torn cloth, salt.
- Palette: sea-green, dark rust red, tarnished brass, murky indigo, kelp brown, pitch black. Bright only where light lands.
- Every scene ends in `InkPass` (edge ink, hatching, grain) + a black vignette; UI in distressed iron, brass rivets, dark wood.

## Done in this pass
| Area | State |
|---|---|
| Procedural runs | `LevelThemeManager` equivalent in dungeon.cpp: per run a `visSeed` and an atmosphere 0-2 per location (12 variants, names in `AtmosphereName`). Seeded ink-black skyline silhouettes (`DrawSeededSilhouettes`), weather/light/grade (`DrawLocationTint`). Contact sheet: `shots/atm_*` |
| Bestiary | 21 creatures (Island, Cave, Weeds, Atlantis) with archetype art (`DrawBestiaryFigure`); sprite sheet pages 9-11 |
| Combat rules | Enemy skills obey rank rules (`fromRanks`): melee from ranks 1-2 onto 1-2, long range from 2-4 onto any. Commands: pull/shuffle/swap, self-move, heals, buffs, cleanse, drain, summon |
| Region debuffs | Totemic Burn, Silt Blindness, Drowning Entanglement, Eldritch Madness (`Status`, `ApplyRegion`) |
| Mini-bosses | `MiniBossChance`: level 0/1/3/5/6 -> 0/15/30/50/85 percent, in `EnterNextRoom` |
| Parkour pixels | World 1 px = 1 screen px (`ZOOM` 0.5, canvas 2x, integer only); diver on the 2 px art grid (`DiverPalette`); hazards built into recessed housings, pillar caps, foreground overlays |
| Card game | Crash after the first card fixed; dealer is lit as a dark, readable figure |

## Done in the second pass
| Area | State |
|---|---|
| Figures (heroes and enemies) | The figure shader now does a hand-inked finish: desaturated palette, 4 flat cel bands, a solid black block shadow along the edge away from the light. Heroes' eyes sit under a black brow bar with a pinprick of light |
| UI | Panel is a stained sheet in dark wood with an iron band, rivets and corner brackets; Button is a riveted iron plate |
| Region scenery | Island (basalt, dead jungle, bone totems), Weeds (kelp forest thick or thin by seed, leviathan ribs, chained anchors, spore pods), Atlantis (marble colonnades, void crystals, ruined altar); their own floors. The Cave keeps its cavern layers |
| Prop spawner | DrawPathProps: weighted pool per location, one prop or a bare patch per 230 px step, seeded per run (skulls, impaled skulls, cages, wrecks, totems, fungus, helmets, shell skeletons, coral, anchors, pods, crates, altars, void crystals, braziers, petrified Lost Ones) |

## Still to do (in order)
1. **Hero art** (12 classes in `DrawCrewFigure`): hide eyes (nurse cowl, diver cracked faceplate leaking light, captain hat brim, mechanic goggles, whaler oilskin, stowaway slouch with bottle, merman glow lure, queen shell crown, robot furnace grate, octopus mantle, siren bone jewelry, wisp plankton cloud). Swap gradient shading for cel blocks.
2. **Backgrounds**: replace `DrawCaveLayers` gradients with flat ink layers per location; make each location's own far layers (basalt/jungle, coral cave, kelp maze, Greco-Roman ruins) rather than tinting one cave.
3. **UI**: iron/brass/dark-wood panel frames in `Panel`, `Button`; distressed edges.
4. **Enemies**: per-creature bespoke animation (idle, attack lunge) beyond the shared bob; art pass to give each archetype more anatomy.
5. **Balance**: level-0 Cave sim is about 51% wins (was 67%); tune level bosses with `--sim`.
6. **Props**: weighted decor props along the path (skulls, anchors, crates, idols) placed from `visSeed` per step.


## Platformer generator (latest)
- Kinematic level generator replaces the chunk system; hops validated with the real movement code; steam vents, crumbling scaffolds, barnacle springboards, friendly fire, Ghost Ship variant.
- Not yet built from the design brief: swaying seaweed, falling stalactites, rotating solid gears, ropes/masts, bomb-jumping and cannons.
