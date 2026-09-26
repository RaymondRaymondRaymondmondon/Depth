# Depth: notes for Claude

Depth is a 2D game in C++17 with raylib 5.5, built with CMake (see README.md for build steps, controls and the file map). It's a brighter, underwater take on Darkest Dungeon: a submarine hub (the Nautilus), roguelike rank-based expeditions, and platformer levels.

The vertical slice was built in an earlier claude.ai chat, "Computer game development". All art is drawn in code; there are no image assets.

## Art direction (from the user)
- **The platform levels (Pipes, Hull, Pirate Ship) are retro pixel art.** They're drawn at half resolution into `PixelRT()` and scaled up with point filtering, with sub-pixel camera offsets for smooth scrolling.
- **Everything else looks like Darkest Dungeon: 2D with depth.**
  - **Characters must look three-dimensional.** Build them from `ShadeLimb` / `ShadeBall` / `ShadeQuad` (lit cylinders, spheres and panels), draw them between `BeginFigure` / `EndFigure` (or use `DrawCrewFigureInked`), and give them chunky, heroic proportions. The figure shader adds a thick ink outline, linework between parts, and light/shadow that wraps the silhouette.
  - **Backgrounds** are inked and painterly. Call `InkPass()` after the world and lighting, before the HUD; it adds edge ink, crosshatching in the darkest shadows, and canvas grain. Also use layered parallax, the lightmap (`LightsBegin` / `AddLight` / `AddCone` / `LightsEnd`), and generated textures.
- **The hub is a single screen: the Nautilus's grand salon** (like Nemo's salon in *Twenty Thousand Leagues Under the Sea*), drawn as a true 3D room (`salon.cpp`: `Proj()` projects room coordinates). **It must not scroll.** Stations sit around the room: side-wall art is painted flat with `PaintWall` and mapped onto the wall in perspective; floor furniture uses `Billboard`. **Only the Nautilus's own hands walk the salon, and only two at a time plus the cat** (`ACTIVE_NPCS`; the pool is helmsman, radio operator, engineer, professor, steward, orderly: `NPCS` in salon.cpp, uniforms via `Hero::outfit`), never the expedition crew. Each works at a sensible post (`WorkPose`) and they, and the cat, steer around the furniture (`OBSTACLES`). The user likes the cat and the moving crew. Keep the room logical.
- **Combat backgrounds are multi-layered parallax** (seven layers in `dungeon.cpp`), and they scroll as the party walks between rooms.
- **Combat actions are animated smoothly and differently per class** (`HeroAnimFx`: the Nurse's quick slash, the Diver's harpoon lunge, the Captain's overhead cut, the Mechanic's heavy swing; throws, shots, heals and buffs too). Effects land at the moment of impact (`PendingAction`).
- **Combat pacing:** after a room, the "advance / battery / retreat" panel waits about 1.4 s, then slides in, and its buttons only work once it has settled. A battery swap plays a torch flicker and locks the buttons for 1.4 s. Fighters breathe and shift their weight, attacks follow through, hits wobble, and rank changes slide (`ShownX`).
- **Platform camera:** locked exactly to the diver on both axes (no easing or lookahead), and only stops at the level edges. Movement is snappy (high acceleration). Tiles have 2.5D depth (`DrawDepth`: tops and sides recede up-right), and the diver casts a shadow on the wall behind.
- **The Pipes are claustrophobic:** enclosed ducts (the level `fill` is solid), lit only by the diver's helmet lamp (`DrawLampDarkness`), with pipes as platforms ('=' horizontal, '|' vertical, both solid). **Levels can climb and plunge**, and are now built by the kinematic generator (below), not from hand-made sections.
- **Platform levels have several parallax layers too** (`DrawBackground` in `platformer.cpp`). The Kraken is a colossal mythological beast (a body looming in the abyss, and tentacles that rise and slam); Blackbeard is tall (36 x 72).

## Platform level design (from the user)
- **Like Super Meat Boy: the hard part is the platforming itself.** All three levels prioritise hard jumps: precise landings, gears that force a low or a high arc, timed jets, and wall-jump chimneys.
- **Only levels 2 and 3 (Hull, Pirate Ship) have enemies.** The Pipes have none. Enemies are extra challenge, and touching one is fatal. **Only bosses can be stomped** (the Kraken optionally, Blackbeard to open the exit). Blackbeard is deadly from every side, including from above, except while he's dazed from charging into a wall.
- **Gold is meant to stay scarce** (the user's call): start with 60, upgrades cost 400/800/1400, relics 145-220, Ward 4-9 per HP, dungeon loot is modest. The intended income is the Periscope and the card table (Flats). Prices live in `data.cpp`; parkour payouts in `Lv()`; Flats payouts in `PAYOUT` (flats.cpp).
- **Each platform boss can be switched off** at the Periscope (`platHullBoss`, `platPirateBoss`, saved). Off means the arena has no boss (`HULL_ARENA_NOBOSS`, `CABIN_ARENA_NOBOSS`) and no relic: the Kraken gives a chance at one, Blackbeard guarantees one and often two (not with checkpoints on). `--verify` checks both arenas.
- **The Kraken is the giant itself**: `KRAKEN_SCALE` sizes the one body you stomp; there is no separate, smaller stand-in. **Mechanics:** steam vents ('v': an updraft column), fragile scaffolding ('f': gone 0.5 s after you land), barnacle walls ('b': 1.5x wall jumps), and friendly fire (musket balls and blasts kill other pirates and parakeets). The Pirate Ship is a **Ghost Ship** about 1 run in 8 (`layout[2]`): skeleton crew, fog, and everything 1.6x faster (`GHOST_SPEED`). **Blackbeard** only gets dazed by a charge that carried at least 5 tiles into a wall (`chargeStartX`).
- **The Hull runs in phases** (near hull plating, open water, then always the drop-off and trench mouth); the Pirate Ship runs deck, hatch, hold, companionway, cabin. Both are built by `GeneratePlatLayout` and checked by `PlatLayoutValid`.
- **Deaths restart the whole level** (it is rebuilt). The Periscope has two saved options: checkpoints (which forfeit the relic) and Normal/Hard. Normal turns 'g' into empty space and 't' into plain floor, so `--verify` checks Hard.
- **The Pirate Ship is structured**: deck, deck, hatch, hold, hold, companionway, cabin (`GeneratePlatLayout` and `PlatLayoutValid`). Its fill is solid below and open sky above (`fillAbove`). Sections carry an interior row and kind (hold or cabin) for the background drawn behind them. The pirates are 'P' (bursts out of a door to stab) and 'G' (shoots aimed musket balls from behind a 'k' barrel).
- Movement runs at a fixed 240 Hz. Jump height is about 3.8 tiles, and the max same-height gap is about 6.5 tiles. Two walls up to 4 tiles apart can be climbed.
- **Platform levels are generated, not stitched** (`levelgen.h/.cpp`). `CalculateValidJumpArc` derives the reachable arc from the movement constants in `namespace kin` (shared with platformer.cpp); pass 1 lays the critical path with set-pieces (steam boost, crumbling run, gear gauntlet, barnacle shaft, ship-to-ship gap, wall-jump shafts), pass 2 adds hazards, enemies, gunner perches and coins. A layout is `{seed, scale%[, ghost]}` (`Game::platLayouts`); `GeneratePlatLayout` validates every hop with the real movement code (weighted A*, `ValidateGenerated`) and redraws on failure (0.25-2 s). Only the boss arenas (`HULL_ARENA`, `CABIN_ARENA` and their no-boss twins) are hand-made. Shaft heights are proven as templates by `DEPTH_SHAFTS=1 depth.exe --verify`.
- **After changing the generator or the movement code, run `depth.exe --verify`.** It must report "All generated levels can be crossed".

## Building and checking on this PC
- Run `.\build.ps1` (add `-Run` to launch). The user has since installed a standalone CMake 4.4 (C:\Program Files\CMake); build.ps1 reconfigures the build folder automatically if the CMake that configured it changes. This machine has Visual Studio 2026 (not 2022) and no git on PATH; the script sets up the VS dev shell and uses VS's bundled git. Output: `build\Release\depth.exe`.
- The project is a git repo, but git isn't on PATH. Use `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.
- **See every sprite** with `depth.exe --sprites sprites.png` (gitignored). When adding a character or enemy, add it to a page (`Draw*SpritePage`).
- **Check visuals** with `depth.exe --shots shots`, which renders every screen to `shots\*.png` (gitignored), then look at the PNGs. Add new screens to the list in `TakeShots` in `main.cpp`. The game can't write to the temp/scratchpad folder, so keep `shots` inside the project.
- **Check balance** with `depth.exe --sim 2000 <level>`. Reference results (sensible auto-player, Cave): level 0 wins about 70% with about 1.5 deaths per run; level 2 about 97%; level 3 about 99%. A random player at level 0 wins only about 6%. (The design chat quoted 88% for its own auto-player, which isn't reproducible.)

## Expedition items and locations
- Expeditions carry a 5-slot pack (`DungeonState::inventory`, `INV_SLOTS`): batteries (burn for +40 light, separate from the stowed ones), bandages, keys (open locked chests in Treasure rooms) and loose relics. Found items must be taken or left; a full pack means clicking something in it to discard it. Relics carried home go to storage. Icons: `DrawItemIcon` in render.cpp (relic icons are picked by name). The pack is hidden during combat and walking, since the ability bar uses that space.
- **Four Shallows locations are open from the start** (`Location`: Cave, Island, Weeds, Atlantis), each with its own tier ladder (`tierCleared[]`, `tierSel[]`), mini-boss name (`LocationBossName`) and atmosphere (`DrawLocationTint`). They share the room and combat engine and the same four creatures.

## Flats (the card game, flats.cpp)
Two players, two decks, three lanes ("flats"); laid out like the first act of Inscryption (dealer across the table, glowing flats, a fan of cards, a brass bell that ends the turn). Rules are in the menu text. It is run-based like a roguelike: win a match, add a card, face a tougher dealer, cash out or risk the pot. Payouts 40/100/220 (+100 for clearing all three). `--flats-sim N [sensible]` plays it headlessly (a sensible greedy player wins the first match about half the time). The dealer sits at a table in the salon (`DrawCardTable`, `DrawCardDealer`).

## Bestiary, atmospheres and pixels (from the visual pass)
- See ROADMAP.md. Enemy skills use `fromRanks` (melee 1-2, long range 2-4); region debuffs live in `Status`; mini-boss chance is `MiniBossChance`; each location ends in its level boss (`LocationLevelBoss`).
- Each run rolls `visSeed` and `atmos` (StartDungeon). Platformer: world px = screen px, 2 px art grid, no fractional offsets or rotation on sprites.
- `--sim` now prints where wipes happen; Cave level 0 is about 51% wins.

## Relics (relics.h / relics.cpp)
- 20 relics in `RelicRegistry` (ids 0-11 are the originals and are saved by id). Two per hero, at most one hard weapon (`CanEquipRelic`); synergies via `CheckRelicSynergies`; numbers summed by `RelicBundle`; on-hit effects via `RunCombatRelicEffects` (a `CombatState` from `HeroAct`). The rank-1 hero's relics also shape platform runs (pickup radius, run speed, lamp).
- Icons are SVG strings in relics.cpp, rasterised by a small built-in SVG reader (raylib 5.5 here has no SVG support) into 128 px textures at startup (`RelicSpriteGenerator`); batteries, bandages and keys use it too. `--relic-test` checks the rules headlessly.
- The figure shader (FIG_FS) does the gritty finish: variable-width ink, cross-hatching, cloth folds, grit, salt, rust, cel bands, black block shadow.

## Rendering gotchas
- raylib culls triangles by winding order. Use `DrawTri()` (it draws both windings), not `DrawTriangle`.
- rlgl resets the bound texture when the batch switches to `RL_TRIANGLES`. Textured custom geometry must use `RL_QUADS` (see `DrawTexturedCircle`).
- Everything draws into an offscreen scene texture. To draw into another texture, use `BeginLayer(rt)` / `EndLayer()`, never raw `BeginTextureMode`.
- Menu screens set low bloom in `DrawCabinBackground`, because parchment is bright enough to bloom.
- **Tiny bright particles** (dust, marine snow, steam) must be drawn **after** `InkPass`. Otherwise the Sobel ink rings each one in black, which is where the "black dust" came from.
- The scene and the figure canvas are supersampled (`SS = 2`) through a pushed matrix (`PushScale`); leave targets with `EndTarget()`, never raw `EndTextureMode`.
- Distant scenery can be drawn between `BeginBackdrop()` and `EndBackdrop(blur)` for depth of field (the cave does this for its far layers).
- Combat poses go through a per-hero spring (`SpringPose`), so animations blend and overshoot. `Pose` has `stride`, `tremble` and `headDown` for steps, fear and flinching; idle posture reflects stress and Death's Door.

## Design calls made where the design doc was open (the user may change any of these)
- **Stress ("Nerves")**: at 100 a hero becomes Rattled: less accurate, easier to hit, sometimes freezes. The Sick Bay cures it, but the hero sits out the next expedition. Deliberately gentler than Darkest Dungeon's afflictions.
- **Cave levels (user's request)**: dungeon levels 0, 1, 3, 5 and 6 (`CAVE_TIER_LEVEL`). Beating one unlocks the next at the Helm, and earlier ones stay playable. Deeper levels scale enemies (`ScaleEnemyForTier`), rooms (3, or 4 from level 3), loot and XP. Simulated wins with a crew of matching level: 65% at level 0, 83% at 1, 69% at 3, 72% at 5, 43% at 6 (the hardest). Check with `depth.exe --sim 800 <crew level> sensible <tier 0-4>`.
- **Crew start at level 0 and level up** from XP (more XP at deeper cave levels). Level-ups are announced on the results panel.
- **Save/load:** the game autosaves to `depth_save.txt` next to the exe whenever you return to the salon, and on quit (`save.cpp`, plain text). "Start a new game" in the salon wipes the save.
- **Flashlight**: low light means more stress and enemies hit harder, but loot is up to 1.8x richer. It also really lights the combat scene.
- **Death's Door**: at 0 HP a hero keeps fighting; each further hit has a 35% chance to kill.
- **Relics**: simple stat bonuses. Two per hero, lost if the hero dies.
- **Bleed vs poison**: bleed refreshes; poison stacks (up to 3 doses) and halves healing received. This barely changed the win rate in simulation.
- **Abilities**: 8 per class, 4 slotted (`Hero::loadout`). Abilities 5-8 unlock at levels 1, 1, 2, 3. New mechanics: mark (+25% damage taken), dodge and armor buffs.
- **Workshop upgrades** (3 levels each; prices 120/240/400): Flashlight Reflector (light drain per room), Bunk Extension (roster size), Sonar Array (recruits per scan, scan cost), Infirmary Gear (Ward cost per HP).

## Locations
Built: the Cave (Shallows: 3 random rooms, then the Lobster mini-boss), and all three platform levels (Pipes, Hull with the Kraken, Pirate Ship with Blackbeard). Each platform level unlocks when the previous one is cleared.
Menu-only, "coming soon": Island, Weeds, Atlantis.

## Roadmap
1. ~~Expand to 8 abilities per class with a choose-4 loadout.~~ Done.
2. ~~Add the Workshop for upgrades, split off from the Radar.~~ Done.
3. ~~Build the Hull platformer level with its Kraken fight.~~ Done, along with the Pirate Ship and Blackbeard.
4. Add the other three expedition locations (Island, Weeds, Atlantis), plus the Deep and Abyssal tiers.
- Open issues: the new abilities make levelled crews noticeably safer (at level 3, deaths per run fall from 0.38 to 0.11); consider tuning once harder tiers exist.

## Enemies (enemyart.cpp)
- Rich enemies are drawn by `DrawRichEnemy` (a shared kit: two-segment limbs, claws, plates, barnacles, ragged hems, rivets, scars, hatching; lit parts inside the figure shader, facing left). Done so far: the Cave's crustaceans (Sea Louse, Pistol Shrimp, Dysformed Crustacean, Lobster, Crustacean Queen) and the Island's tribe (Spearman, War Dog, Shaman, Demigod). The rest still use the archetype drawers in render.cpp (`DrawBestiaryFigure`).
- **Bosses fill several ranks** (`Enemy::span`: level boss 3, mini-boss 2; still one enemy, hittable and able to act from any rank it fills; `CoversMask` / `SlotStart` in dungeon.cpp). A level boss leaves room for one retainer; bosses can't be shoved.
- `depth.exe --shots shots boss_` renders just the shots whose name contains the text (`foes_crab`, `boss_lobster`, `boss_queen`, `foes_tribal`, `boss_demigod`).
- The Weeds' merfolk are done too (Feral Merman, Siren, Neptune at 3 ranks). Next up: the rest of the Weeds (Octopus, Eel, Shark), Atlantis, the Island's queen and Sun God, and the remaining Cave creatures.
- **Bosses can strike twice in a round** (`Enemy::extraAct`, a percent tuned per boss in `MakeEnemy`; bonus turns can't chain; the chance shrinks with depth via `EXTRA_ACT_BY_TIER` in `ScaleEnemyForTier`). Tune with `depth.exe --boss <runs> <crewLevel> <tier> <EnemyType index>` (one boss, many fights; `DEPTH_EXTRA=0.5` scales every chance). At crew level 0: most bosses land at 70-88%; the Queen, Demigod, Armored Lost One and Alien Horror stay 95%+ (their stats, not the chance, are the limit) and Cthulhu is hard (about 50%). Whole expeditions: about 58/69/60/59/49% at cave levels 0/1/3/5/6.
- Platform layouts are now generated (and validated) the first time a level is started, not in `InitGame`, so simulations are fast.
- The Weeds are now fully redrawn (Octopus, Electric Eel, Great White too). Bosses were rebalanced: at crew level 0 every mini and level boss wins 66-85% (Cthulhu, the final boss, about 20%); the Demigod, Armored Lost One and Alien Horror had their damaging abilities strengthened (their `Support` abilities now carry some damage).
- Atlantis is redrawn (Lost One Infantry and Cultist, Armored Lost One at 2 ranks, Alien Horror at 2 ranks, Cthulhu at 3 ranks with wings, a tentacled face, a sigil-holding arm and a claw full of broken column). Still on the older archetype drawers: the Island's Coconut Queen and Sun God, and the Cave's Brine Worm, Ghost Worm and Lost Diver.
- Every enemy now has a rich drawing in enemyart.cpp (the older archetype drawers in render.cpp are unused except as a fallback). Next passes could be enemy animation (attack windups, hit recoil, death) and per-enemy idle detail.

## Platformer macro-structures and art (latest)
- `levelgen.cpp` now builds three real structures instead of floating platforms: the **Pipes** are a duct (solid all round) whose pipe runs ('=') stand on risers ('|') to the floor, join in continuous runs, and drop through pipe chambers with T/elbow joints (`PipeDrop`); the **Hull** is a trench of floor-to-ceiling coral columns with base tunnels, alternating with short plateau columns, chained as tunnel -> climb -> plateau -> drop -> tunnel (`BuildTrench`; overhang shelves with climbable weed 'w'); the **Pirate Ship** is a fleet (`BuildFleet`): ships with a raised stern castle and a stepped bow, hatches with spikes, mast-and-barricade shafts, dressing masts with tunnels at their feet, and, across gaps too wide to jump, a mast of shrinking yardarms up to a rigging rope ('r') to the next mast.
- `depth.exe --gen <level> <seed> [c0 c1]` prints a generated level as ASCII. `DEPTH_FULL=1 depth.exe --verify` also searches every shaft climb of the generated levels (slow); the normal run trusts the proven shaft templates.
- Art: tiles auto-join by an N/E/S/W bitmask (`SolidMask` / `TileMask`): pipe elbows, tees and crosses, coral crowns and hanging tendrils on open faces, ship keel and end trim, mast and yardarm lashings, sagging rope. Backgrounds are a `BackgroundSystem` with far, mid and fore `ParallaxLayer`s per zone (the fore layer is drawn in front of the diver). Seaweed slows a fall; hold up/down to climb it (Space still jumps off).
