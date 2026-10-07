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
- **Ship systems** (`ShipSystems.cs`, the doc's table): power states at the switchboard (Engine: full speed, battery
  recharging, 85-110 dB, burns the boiler's fuel; Silent running: a quarter speed on the battery, 15-25 dB; Dead in
  the water: no screw, lamps, life support or sonar, 0 dB), `NoiseDb` for the Wake (stage 3); breaches (a hard
  grounding opens one; F9 holes your room for testing) let the sea in by the pressure at her depth, water runs room
  to room through the doors, electric pumps run while she has power and the hand pumps when someone mans them; the
  water's weight sinks her and trims her; hold E at a breach for 3 s to patch it; wading slows you and deeper water
  is swum (the tank drains with your head under). The Bridge's sonar: Space pings (a 115 dB crack, 0.5% battery)
  and paints a 260 m depth chart, ground standing over her keel in orange; passive listening waits for stage 3's
  creatures. Station boxes keep you out of the fittings.
- Shots added: `flooding` (90 s after a breach), `sonar`.
- **Moving fittings** (`ShipFittings.cs`, built in code on pivots): the helm's wheel turns with the rudder, the Bridge
  telegraph's handle stands at the ordered rung, depth gauge needles (the pilot house's with a red order needle) and
  the Bridge clock's hands. Shots `telegraph`, `gauges`; `helm` shows the wheel put over.
- Left for later stages: the passive sonar's contacts (stage 3's creatures), stoking the boiler (stage 4's items).
  **All sound waits for one full sound stage** (the user, 2026-10-07).

## Playtest notes (2026-10-07)
- **Ladders:** walk into one and hold W to climb, S to go down, Space to let go. At the top of the pilot house's shaft
  you step off onto its floor (before, you rose past the top and fell back down the shaft); at the top of the Dive
  Room's you open the deck hatch with E. Standing at the top of the shaft, S takes hold to climb down
  (`Diver.LadderStep`). Shot `ladder_top` climbs for 6 s from the Bridge and logs where you end up.
- **Four crew cabins:** the hull was lengthened by an 8 m section let in between Hydroponics and the Fabrication
  Bay (`S` in deep_nautilus.py; everything forward of x = 6 moved 8 m to the bow; she's now 78 m plus the spur).
  The Crew Quarters there: a passage down the middle, two cabins each side, each with a bunk, desk and chair,
  locker, shelf, its own lamp and a porthole in the hull. The layout lists each cabin (`cabins`, for decorating
  later) and the partitions (`walls`, the game's colliders). Shots `crew`, `cabin`.
- The interior draws first (render queue 1950), so the seabed and life beyond the walls cost nothing when you're
  inside (the crew passage went from 33 to 60 fps).

## Stage 3, the living sea (first part, 2026-10-07)
- **Data:** `Resources/Data/species_shallows.json` and `species_kelp.json`, the doc's rosters read from the page
  images: 20 flora and 60 fauna per biome (plankton, grazers, filter feeders, mesopredators, scavengers, apex), with
  diets, predators, habitat, depth band, activity, hearing bands, light, aggression, lethality and tactics; every
  estimated field is listed in the entry's `estimated`. `Sea/Species.cs` derives speeds, senses, body plan and
  colours.
- **Sound and the Wake** (`Sea/Acoustics.cs`): P(d) = P0 - 20 log10 d - a d per band (low, mid, high, ultrasonic),
  events and continuous sources (the Nautilus's engine, the diver's flippers), Wake heat over a 32 m grid with the
  doc's tiers, blood raising a zone's Wake for good. The sonar ping is a 115 dB ultrasonic event.
- **Currents and scent** (`Sea/Scent.cs`): tidal currents with eddies (stronger in the Kelp), the scent field
  advected downstream, spread and fading; 0.1 ppm noticed, 1.5 ppm frenzy.
- **The food web** (`Sea/Ecology.cs`): B0-B4 pools per biome at 12% efficiency, calibrated so the start is the
  balance; over-harvesting starves the hunters (STARVING: senses x3.5, no fear, they ram the hull's larder),
  ecosystem memory, starving apexes invade shallower water.
- **The animals** (`Sea/Life.cs`, `CreatureMeshes.cs`, `Creature.shader`): individuals near the diver drawn from the
  pools, the doc's six states with its thresholds, schools, daily migration (the Kelp's hunters rise at night),
  predation with blood and noise, bites (diver health; at 0 you come to aboard), rams that breach the hull. The two
  resident leviathans: the Reef-Crusher (35 m) and the Tangle-Serpent (45 m), each holding a territory, roused by an
  engine in it. Procedural bodies for 15 body plans, swimming in the vertex shader; the passive sonar lists the big
  ones by bearing. Shots `life_reef`, `life_school`, `life_kelp`, `life_night`, `leviathan`, `life_blood` (a bleeding
  fish: hunters come, two frenzy), `life_starve` (75% of the grazers netted: hunters hunt and ram the hull).
- Tests (`SeaTests`): the sound law, Wake tiers and cooling, hearing by band, scent drifting downstream and
  fading, the web holding its balance, over-harvesting starving the hunters.
- **Blender bodies** (`tools/artgen/deep_creatures.py` -> `Resources/Creatures/creatures_<biome>.glb`, 3.4 MB): every
  species of both rosters built from its body plan and its name - lofted bodies with real profiles, membrane fins
  with rays, eyes, mouths and gill slits, jointed legs and claws, arms and tentacles, spiral shells, valves, stars,
  tube worms with crowns; palettes and patterns (countershading, bars, lateral stripes, spots, mottling, the orca's
  markings, scutes) in vertex colours (no textures). The leviathans: the Reef-Crusher's battering crest and four eyes,
  the Coral-Crusher's grinding plates and coral crusts, the Tangle-Serpent's barbels and glowing spots, the Shroud
  Serpent's veil; jaws, teeth, scute rows, fin pairs and flukes. UV0 carries the swim. Contact sheets: render with the
  gallery script (see the build log in the session) into `shots/deep/gallery_*.png`.
- **Flora** (`tools/artgen/deep_flora.py` -> `Resources/Flora/flora_<biome>.glb`): all 40 plants of the two flora
  tables; Iron-Kelp stays the code-built stalk. `Flora.cs` plants them by habitat: sea-grass meadows with reeds at
  their sandy edges, thick reefs (corals, sponges, anemones, ferns, kelplets, blooms, algae), filaments and moss on
  steep rock, and in the Labyrinth the kelp stalks with glow bulbs at their feet, holdfasts, shelves, bladders, pods,
  tubeworms, spongeweed and vine curtains up them, canopy vines on the tall ones, and moss, sponges, tubeworms, pods
  and tendrils on the clearings' floor (about 180,000 plants). Each kind has a group (reef, meadow, rock, kelp, floor):
  reef fish spawn over the reef and kelp species among the stalks, never in plain sight.
- **The seabed's heightmap fixed:** 769 samples is not 2^n + 1, so Unity rounded it and the drawn terrain no longer
  matched `Seabed.H` (up to 70 m off: the stage 1 kelp stood in mid-water). Now 1025.
- **The doc's named behaviours:** light as a channel (a dive lamp ~1,000 lux at 1 m in its beam, the floods: light-drawn
  species come to it, light-shy ones flee; the plankton - pool biomass, not individuals - shows as motes gathering in
  the beam at night); the full moon's plankton bloom (bait fish and open-water hunters crowd the top 25 m); bait balls
  pulling tight and rising under attack; cleaning stations (well-fed hunters visit the cleaner shrimp and wrasse and stay
  calm while cleaned); snapper packs driving their prey toward the morays; ambushers striking only what comes within a
  body length or two; carrion-crabs clicking to call the scavengers; fin-sharks, dogfish and stalker-hounds smelling
  blood fifty times fainter (300 m); groupers booming; the Wake's tiers drawing scavengers, then mesopredators, then
  the apex; noises approached only to a standoff (grazers keep 30 m off); the Reef-Crusher ignoring silent divers but
  ramming her over 70 dB within 400 m; the Tangle-Serpent wrapping anything that passes within 15 m.
- Shots added: `life_coral` (the densest reef), `life_light` (night, the lamp), `life_engine` (two minutes at full
  ahead: Wake tier 3, the Reef-Crusher comes and rams), `life_moon` (the full-moon bloom). `LogLife` prints the
  states and the reasons ("DEEP WHY").
- **Left for later:** the Stalker-Hounds stealing tools and the wreck interactions (with stage 4's items and the
  wrecks), the Tangle-Serpent's 95 dB constriction sound and all other sound (the sound stage).

## The helm's cupola (playtest, 2026-10-07)
- The user's reference (a bridge walled in tall glass): the pilot house is now a glazed observation cupola, 5 x 3.4 m,
  a three-faceted bay at the front, glass between slim iron mullions from a waist-high sill to the roof on every side
  but the back; the helm at the front of the bay. The fog was thinned (visibility 90 m in the Shallows, 60 in the
  Kelp; density 1.3/vis; far clip 2.8 x vis) so the helm can see where she's going.

## Stage 4, survival and crafting (2026-10-07)
- **Data:** `Resources/Data/items.json` (transcribed from the doc's pages 3-4, 12, 36, 120-135): 152 resources (each
  with its source and the tools that take it), 98 items with recipes and stations, 10 hull upgrades, 12 stations; the
  doc gives no rates for the survival meters (only what food and drink restore).
- **Code** (`Runtime/Craft/`): `Items.cs` (ItemDB, Inventory: stacks, crafting from the pack and her stores together,
  the doc's station names mapped onto her rooms), `Hands.cs` (the hotbar 1-5/0, the held tool drawn in front of the
  eye, gathering by the resource table's tools, the knife / Heated Blade (cauterises) / spear gun on animals, kills
  loot the table's parts, the drill's 70 dB in the Wake), `Survival.cs` (hunger 2.2/min, thirst 3.0/min, warmth: the
  sea's temperature by depth and night, her cabins warm with life support and cool when she's dead; raw fish poisons),
  `CraftUI.cs` (Tab the pack; E at a station its recipes; her lockers and larder; the desalinator's 2 minutes a cup),
  `Deposits.cs` (titanium nodes on the Shallows' rocky slopes and in the kelp clearings for the drill, loose chunks by
  hand; the wrecks), `KiteSub.cs`.
- **Design calls (the doc's loops):** loose titanium chunks are picked up by hand so the first Starter Drill can be made
  (the doc puts Titanium Ore behind the drill, which needs titanium); "restore primary power" is repairing the Steam
  engine at the boiler (1 Titanium, 1 Synthetic Fuel Canister) - until then she has only her batteries; Synthetic
  Fuel Canisters stoke the boiler; the stations are all there, derelict but working (the Abyssal Forge and the Thermal
  Desalinator's costs were circular in the doc); her crush depth is 30 m until the Bio-Polymer Hull Patches (50 m),
  then the Titanium Hull Plates (150 m); past it the hull groans and plates give way.
- **Blender:** `deep_wrecks.py` (the Sunken Pirate Galleon broken in two in the Shallows, the tangled dreadnought in the
  Kelp, with JSON salvage spots and collider boxes), `deep_minerals.py` (ore nodes, quartz, calcite, salvage piles),
  `deep_vehicles.py` (the Kite-Sub). Deep/Lit has `_VCAlbedo` for vertex-coloured props.
- **The Kite-Sub:** made at the moonpool (4 Titanium, 2 Reinforced Glass, 2 Glow-Bulb Cells, 1 Kelplet Float Bladder),
  docks in the well and charges there; mouse steers, W/S, A/D, Space/Ctrl, Shift; its own battery; crush depth 200 m.
- The sonar's ping marks deposits (white) and wrecks (yellow).
- Tests (`CraftTests`): stacks, crafting from pack and stores, the tables loading, and every phase 1-2 recipe's raw
  ingredients gatherable somewhere in the world. Shots: `craft`, `pack`, `deposit`, `wreck_galleon`,
  `wreck_dreadnought`, `kitesub`, `kitesub_fly`.
- **Left for later:** the equipment the doc gives no recipe for (flares, decoys, traps), the Artisan Bench's furniture
  placed in the cabins (with decorating), a proper brass-and-paper UI, the Blender pass on the hand tools.
