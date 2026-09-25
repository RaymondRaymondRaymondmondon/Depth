# Depth: notes for Claude

Depth is a 2D game in C++17 with raylib 5.5, built with CMake (see README.md for build steps, controls and the file map). It's a brighter, underwater take on Darkest Dungeon: a submarine hub (the Nautilus), roguelike rank-based expeditions, and platformer levels.

The vertical slice was built in an earlier claude.ai chat, "Computer game development". All art is drawn in code; there are no image assets.

## Art direction (from the user)
- **The platform levels (Pipes, Hull, Pirate Ship) are retro pixel art.** They're drawn at half resolution into `PixelRT()` and scaled up with point filtering, with sub-pixel camera offsets for smooth scrolling.
- **Everything else looks like Darkest Dungeon: 2D with depth.**
  - **Characters must look three-dimensional.** Build them from `ShadeLimb` / `ShadeBall` / `ShadeQuad` (lit cylinders, spheres and panels), draw them between `BeginFigure` / `EndFigure` (or use `DrawCrewFigureInked`), and give them chunky, heroic proportions. The figure shader adds a thick ink outline, linework between parts, and light/shadow that wraps the silhouette.
  - **Backgrounds** are inked and painterly. Call `InkPass()` after the world and lighting, before the HUD; it adds edge ink, crosshatching in the darkest shadows, and canvas grain. Also use layered parallax, the lightmap (`LightsBegin` / `AddLight` / `AddCone` / `LightsEnd`), and generated textures.
- **The hub is a single screen: the Nautilus's grand salon** (like Nemo's salon in *Twenty Thousand Leagues Under the Sea*), drawn as a true 3D room (`salon.cpp`: `Proj()` projects room coordinates). **It must not scroll.** Stations sit around the room: side-wall art is painted flat with `PaintWall` and mapped onto the wall in perspective; floor furniture uses `Billboard`. Crew, the ship's hands and the cat wander the floor, and the user likes that.
- **Combat backgrounds are multi-layered parallax** (seven layers in `dungeon.cpp`), and they scroll as the party walks between rooms.
- **Combat actions are animated smoothly and differently per class** (`HeroAnimFx`: the Nurse's quick slash, the Diver's harpoon lunge, the Captain's overhead cut, the Mechanic's heavy swing; throws, shots, heals and buffs too). Effects land at the moment of impact (`PendingAction`).
- **Platform levels have several parallax layers too** (`DrawBackground` in `platformer.cpp`). The Kraken is a colossal mythological beast (a body looming in the abyss, and tentacles that rise and slam); Blackbeard is tall (36 x 72).

## Platform level design (from the user)
- **Like Super Meat Boy: the hard part is the platforming itself.** All three levels prioritise hard jumps: precise landings, gears that force a low or a high arc, timed jets, and wall-jump chimneys.
- **Only levels 2 and 3 (Hull, Pirate Ship) have enemies.** The Pipes have none. Enemies are extra challenge, and touching one is fatal. **Only bosses can be stomped** (the Kraken optionally, Blackbeard to open the exit).
- Movement runs at a fixed 240 Hz. Jump height is about 3.8 tiles, and the max same-height gap is about 6.5 tiles. Two walls up to 4 tiles apart can be climbed.
- **After adding or changing a section, run `depth.exe --verify`.** It must report "All sections can be crossed".

## Building and checking on this PC
- Run `.\build.ps1` (add `-Run` to launch). The user has since installed a standalone CMake 4.4 (C:\Program Files\CMake); build.ps1 reconfigures the build folder automatically if the CMake that configured it changes. This machine has Visual Studio 2026 (not 2022) and no git on PATH; the script sets up the VS dev shell and uses VS's bundled git. Output: `build\Release\depth.exe`.
- The project is a git repo, but git isn't on PATH. Use `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.
- **Check visuals** with `depth.exe --shots shots`, which renders every screen to `shots\*.png` (gitignored), then look at the PNGs. Add new screens to the list in `TakeShots` in `main.cpp`. The game can't write to the temp/scratchpad folder, so keep `shots` inside the project.
- **Check balance** with `depth.exe --sim 2000 <level>`. Reference results (sensible auto-player, Cave): level 0 wins about 70% with about 1.5 deaths per run; level 2 about 97%; level 3 about 99%. A random player at level 0 wins only about 6%. (The design chat quoted 88% for its own auto-player, which isn't reproducible.)

## Rendering gotchas
- raylib culls triangles by winding order. Use `DrawTri()` (it draws both windings), not `DrawTriangle`.
- rlgl resets the bound texture when the batch switches to `RL_TRIANGLES`. Textured custom geometry must use `RL_QUADS` (see `DrawTexturedCircle`).
- Everything draws into an offscreen scene texture. To draw into another texture, use `BeginLayer(rt)` / `EndLayer()`, never raw `BeginTextureMode`.
- Menu screens set low bloom in `DrawCabinBackground`, because parchment is bright enough to bloom.

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
