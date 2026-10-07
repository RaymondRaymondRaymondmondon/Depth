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

## Stage 2 so far (2026-10-07)
- **The model:** `tools/artgen/deep_nautilus.py` (+ `deep_tiles.py`: fouled riveted iron, walnut, teak, interior
  iron) builds Verne's Nautilus in Blender: `nautilus_hull.glb` (the 76 m cigar with a 12 cm plate thickness, the
  salon windows, the airlock, the moonpool, the deck, the lantern, floods, fins and screw, and the pilot house built
  from four cut panels), `nautilus_interior.glb` (seven rooms bow to stern plus the pilot house up its shaft) and
  `nautilus_layout.json` (rooms, doors, stations, lamps, hatches, ladders, windows, moonpool, lights) into
  `TheDeep/Assets/Deep/Resources/Models`. Run: `blender -b --factory-startup -P tools/artgen/deep_nautilus.py --
  --out TheDeep/Assets/Deep/Resources/Models [--part hull|interior]`. Lessons: a boolean through a closed solid
  leaves a pocket with a floor (give skins a thickness); bevelled thin panels can refuse a cut; two coaxial
  cylinders drop faces (build sleeves directly).
- **In Unity:** glTFast 6.20 imports the models; `Nautilus.cs` swaps their materials for `Deep/Lit` (the baked
  textures, a derivative-frame normal map, vertex-colour AO, lit by DeepWater.hlsl; `_Interior` for no sun inside,
  `_Emit` for lamp globes, `_Cull` one-sided outside) and `Deep/Glass` (windows, the moonpool's water).
  `DeepWater.hlsl` now has `DeepLamps` (up to 24 lamps near the camera plus the helmet lamp, absorbed through water),
  used by every Deep shader. The generator frame maps to the ship's frame by `Nautilus.G` (x bow -> z).
- **Walking aboard while she moves:** the Body (visible, with hull colliders for swimmers) and the Proxy (invisible
  room colliders built from the layout, fixed at y 5000); the diver walks in the Proxy and the camera is mapped onto
  the Body. Ladders, the airlock, the deck hatch and the moonpool board and leave her.
- **Sailing:** `Nautilus.Sail` (telegraph, rudder, heading hold, depth order, grounding, deck-awash surface, pitch
  and heel), worked from the pilot house's helm (A/D rudder, W/S telegraph, Space/C depth, H hold, X centre) or the
  Bridge telegraph; the switchboard in the engine room switches her power (lamps come up unevenly; emergency red
  lamps without power).
- Shots: `nautilus`, `nautilus_side`, `nautilus_stern`, `salon`, `bridge`, `pilothouse`, `engine`, `moonpool`,
  `dark`, `underway` (50 s of sailing), `helm`. `tools\deep.ps1 shots "<names>"`.
- **Still to do in stage 2:** sonar (active and passive), hull breaches, flooding and pumps, power as a resource
  (the boiler), the station fittings turning (wheel, telegraph handle, gauges), sound aboard, furniture collisions.
