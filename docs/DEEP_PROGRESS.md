# The Deep: build log

Design: `Reference_For_Future_MP_Games/The Deep Design Document.pdf` (137 pages; page images and OCR in
`docs/deep_pdf_pages/`, all the text in `docs/deep_pdf_all.txt`). A 1-4 player online co-op survival game: a raft at
night, the derelict Nautilus as a mobile base, nine biomes stacked by depth plus a surface island, a simulated food
web, sound (the Wake) and scent as systems, hull upgrades as the depth gate.

## The user's decisions (2026-10-06)
- **Engine: Unity** (6000.6.4f1; the doc says Unreal 5, overridden). Project `TheDeep/` in the repo (its
  Library, Temp, Logs, UserSettings and builds are gitignored). It sits on Depth's arcade **Action** reel and is
  launched as its own program.
- **First build: phases 1-2 deep** (the raft, the Nautilus's rooms, the Sunlit Shallows and the Kelp Labyrinth with
  their full rosters, the Wake, scent and AI states, crafting, the first two hull upgrades, 1-4 players), then one
  biome per stage.
- **Multiplayer: Depth's lobby hands off.** Host/Join in Depth's arcade (ZeroTier works); when the crew is in, Depth
  launches The Deep on every PC with the host's address. Unity Netcode for GameObjects, a direct connection.
- **Look:** realistic, Subnautica-like, made with Blender and Unity, and optimised to run on this PC (Intel UHD).
- **Campaign:** a long saved campaign per crew (the host saves). A death respawns you aboard the Nautilus and leaves
  what you carried where you died.

## How it's built and checked (headless; no Editor window, no Visual Studio needed)
- Unity: `C:\Program Files\Unity\Hub\Editor\6000.6.4f1\Editor\Unity.exe`, run with `-batchmode -quit -projectPath
  TheDeep -executeMethod <Class.Method>`.
- Packages: URP 17.6, Netcode for GameObjects 2.13, Transport 6.6, Test Framework 1.8 (`DeepSetup.AddPackages`).

## Stages
1. **Foundation:** URP set up by script, one boot scene (everything else is built at runtime from code and data),
   the diver (swim, walk, sprint, oxygen), the procedural seabed for the Shallows and the Kelp Labyrinth, the ocean
   surface, the underwater look (depth fog, caustics, light shafts, marine snow), day and night, the build script, a
   screenshot harness and headless tests, and the Depth reel entry that launches it.
2. **The Nautilus:** the sub (Blender), piloting from the Bridge, walkable interior rooms while it moves, power
   states and the telegraph, active and passive sonar, hull breaches, flooding and pumps, the airlock and dive room.
3. **The living sea:** the species tables for biomes 1-2 as data, the biomass simulation, creatures with the six AI
   states, the Wake field, scent drifting on currents, daily migration, schools, and the two leviathans.
4. **Survival and crafting:** oxygen, hunger, thirst, health and cold; gathering by tier; the inventory; the
   Fabrication Bay and stations; the doc's recipes; the knife, Starter Drill, Spear Gun and Heated Blade; the first two
   hull upgrades; the Kite-Sub.
5. **The opening:** the night raft, the storm, boarding the Nautilus and restoring power.
6. **Multiplayer:** host-authoritative Netcode, 1-4 players, Depth's lobby handing off.
7. **Campaign and polish:** saves, death rules, sound, the Artisan Bench, performance on this PC.
