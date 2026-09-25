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
- **The Pipes are claustrophobic:** enclosed ducts (the level `fill` is solid), lit only by the diver's helmet lamp (`DrawLampDarkness`), with pipes as platforms ('=' horizontal, '|' vertical, both solid). **Levels can climb and plunge:** sections may be any height; '<' in column 0 and '>' in the last column mark the entry and exit rows, and `BuildFromParts` offsets each section vertically to join them.
- **Platform levels have several parallax layers too** (`DrawBackground` in `platformer.cpp`). The Kraken is a colossal mythological beast (a body looming in the abyss, and tentacles that rise and slam); Blackbeard is tall (36 x 72).

## Platform level design (from the user)
- **Like Super Meat Boy: the hard part is the platforming itself.** All three levels prioritise hard jumps: precise landings, gears that force a low or a high arc, timed jets, and wall-jump chimneys.
- **Only levels 2 and 3 (Hull, Pirate Ship) have enemies.** The Pipes have none. Enemies are extra challenge, and touching one is fatal. **Only bosses can be stomped** (the Kraken optionally, Blackbeard to open the exit). Blackbeard is deadly from every side, including from above, except while he's dazed from charging into a wall.
- **Gold is meant to stay scarce** (the user's call): start with 60, upgrades cost 400/800/1400, relics 145-220, Ward 4-9 per HP, dungeon loot is modest. The intended income is the Periscope and the card table (Flats). Prices live in `data.cpp`; parkour payouts in `Lv()`; Flats payouts in `PAYOUT` (flats.cpp).
- **Each platform boss can be switched off** at the Periscope (`platHullBoss`, `platPirateBoss`, saved). Off means the arena has no boss (`HULL_ARENA_NOBOSS`, `CABIN_ARENA_NOBOSS`) and no relic: the Kraken gives a chance at one, Blackbeard guarantees one and often two (not with checkpoints on). `--verify` checks both arenas.
- **The Kraken is the giant itself**: `KRAKEN_SCALE` sizes the one body you stomp; there is no separate, smaller stand-in. **Blackbeard** only gets dazed by a charge that carried at least 5 tiles into a wall (`chargeStartX`).
- **The Hull runs in phases** (near hull plating, open water, then always the drop-off and trench mouth); the Pirate Ship runs deck, hatch, hold, companionway, cabin. Both are built by `GeneratePlatLayout` and checked by `PlatLayoutValid`.
- **Deaths restart the whole level** (it is rebuilt). The Periscope has two saved options: checkpoints (which forfeit the relic) and Normal/Hard. Normal turns 'g' into empty space and 't' into plain floor, so `--verify` checks Hard.
- **The Pirate Ship is structured**: deck, deck, hatch, hold, hold, companionway, cabin (`GeneratePlatLayout` and `PlatLayoutValid`). Its fill is solid below and open sky above (`fillAbove`). Sections carry an interior row and kind (hold or cabin) for the background drawn behind them. The pirates are 'P' (bursts out of a door to stab) and 'G' (shoots aimed musket balls from behind a 'k' barrel).
- Movement runs at a fixed 240 Hz. Jump height is about 3.8 tiles, and the max same-height gap is about 6.5 tiles. Two walls up to 4 tiles apart can be climbed.
- **After adding or changing a section, run `depth.exe --verify`.** It must report "All sections can be crossed".

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
