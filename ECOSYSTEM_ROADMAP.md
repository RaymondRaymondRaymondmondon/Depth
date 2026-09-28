# Depth: the ecosystem/parkour expansion (Rain World-inspired)

This tracks a large addition the user asked for, kept separate from ROADMAP.md (the visual-art
roadmap) because it's a different initiative: new parkour content and systems, explicitly **not**
touching the main game (the Nautilus hub, expeditions, Flats) at all.

## What was asked for

The user's brief (developed with Gemini, then refined with Claude through several rounds of
clarifying questions) asks for Depth's parkour section to grow toward Rain World's depth of
movement and living-world simulation, layered on top of the existing Super Meat Boy-style hard
platforming. Concretely:

1. **Aquatic traversal mechanics** for water biomes: a **Water Dash** (impulse movement, overrides
   drag briefly, displaces small nearby creatures, triggers predator "investigate" states, costs
   stamina, short cooldown) and **Hydro-Glide** (streamlines the body to cut drag, locks onto ambient
   current vectors like slipstreams/vents for free high-speed travel, and moves near-silently past
   predators).
2. **Multi-species ecosystem interaction arrays**: every creature in every biome runs perception
   checks against the others - not just "attack player." The brief lays out full 10-species food
   chains with cause-and-effect diagrams for seven biomes: **The Pipes**, **The Hull**, **The Pirate
   Ship** (the three existing platform levels - to be retrofitted, not replaced), and **The Island**,
   **The Cave**, **The Weeds**, **Atlantis** (new platform versions of the four Shallows expedition
   locations - new parkour content only, the turn-based expedition versions are untouched).
3. **A personality system**: every spawned creature rolls a `PersonalityProfile` (aggression,
   bravery, energy, curiosity) from the level's seed, so the same species plays differently run to
   run (an aggressive shark this run, a timid one next time), driving a shared
   `EntityBrain::EvaluateBehavior`-style perception/state loop (Flee / Hunt / Investigate / Feed /
   Defend).
4. **A fifth, brand-new biome**: **The Open Abyss** - an endless vertical descent trench, added after
   Claude asked what the "fifth" level was (four of the five levels are new parkour versions of the
   existing Shallows locations; this one is genuinely new). Its own detailed brief covers:
   - Vertical-physics-specific ecosystem chain (Abyssal Leviathans, Giant Isopods, Gulper Eels,
     Siphonophore Colonies, Vampire Squid, Hadal Trench-Worms, Bioluminescent Plankton, Deep-Sea
     Hatchetfish, Glass Sponges, Heavy Brine-Slugs) driven by falling/acoustic chain reactions.
   - Descent-specific traversal: a **hydro-parachute** (aim up while gliding, arrests fall speed to
     land on fragile Glass Sponges without shattering them), **slipstream diving** (aim down inside a
     Leviathan's down-draft to outpace falling hazards), and **acoustic dashing** (dash to trigger
     plankton light in the pitch black - which also draws curious predators).
   - A visual identity brief: near-black void canvas, neon bioluminescent accents (acid green
     Siphonophores, cyan Glass Sponges, magenta Vampire Squid), suggested-not-rendered Leviathan
     silhouettes in the background, a pressure-distortion screen-space shader, marine-snow fog that
     streaks to telegraph down-drafts before they hit.
   - A procedural topology brief: frictionless jagged trench walls (no clinging/wall-jumping out),
     Glass Sponge ledges as the only safe footholds, brine-pool horizontal arenas with inverted glide
     physics, Siphonophore "laser mazes" you must parachute through, thermal-vent updrafts, and
     Isopod "bowling lane" funnel slopes.

## Decisions made (via clarifying questions, before writing any code)

- **True 3D physics**, not an adaptation of the existing 2D side-view engine - a real Camera3D/3D
  collision system for the aquatic/vertical biomes, separate from the 2D platformer's fixed-timestep
  movement code that Pipes/Hull/Pirate Ship and `--verify`'s jump-arc validator depend on.
- **Creatures and the player render as full 3D models**, not billboarded 2D sprites or simple
  primitive rigs.
- Since there's no way to run 3D modeling/rigging software or import external asset files in this
  environment, those "full 3D models" are **procedural 3D geometry built directly in C++** (raylib
  `Mesh`/`Model` data and primitives assembled in code), not hand-sculpted/imported assets - the 3D
  equivalent of how the game already builds its 2D figures in code (`ShadeLimb`/`ShadeBall`).
- The **existing 3 platform levels get retrofitted** onto the same shared personality/ecosystem
  framework eventually, not just the 4 new ones.
- **Build order: one vertical slice first.** Build the full framework (personality struct, dive
  physics, a real render pipeline) against one biome with a handful of its species, prove it plays,
  then replicate the pattern - rather than stubbing all 7+ biomes at once.

## Status: done so far

**The Open Abyss - first vertical slice** (`src/abyss.cpp`, commit `1659e0c`). A genuinely 3D scene
(`Scene::Abyss`), fully additive - nothing about the main game, the existing platform levels, or
`--verify`/`--flats-ui-test` changed or regressed.

Working:
- Dive physics: ambient fluid drag, a dash impulse (20% stamina cost, short cooldown, displaces
  nearby small life and can dislodge clinging Isopods), hydro-glide with both the hydro-parachute
  (aim up) and slipstream (aim down inside a down-draft) variants.
- A procedurally jagged vertical trench as a real GPU mesh (custom vertices/normals/vertex colours,
  layered hash noise per depth/angle), frictionless walls that softly clamp the player inward.
- A first slice of the food chain: Glass Sponges (only safe footholds; shatter under a hard landing
  or a falling Isopod), Giant Isopods (cling until a down-draft or nearby dash dislodges them into
  falling hazards), a Gulper Eel (lunges toward acoustic disturbances), Bioluminescent Plankton
  (flash-lit by those disturbances). Each rolls a shared `PersonalityProfile` from the level seed.
- A first visual-identity pass: near-black void background, dim procedural wall shading, the
  player's own weak bioluminescent glow, flash-lit plankton puffs.
- Debug/test hooks: `depth.exe --verify-abyss` (headless smoke test, mirrors `--verify`: a scripted
  diver drifts to the shaft centre and holds the hydro-parachute when falling fast, over 6000 fixed
  steps), `depth.exe --shots shots abyss` / `abyss_deep`.

Two real bugs were found and fixed while building this (see the commit message and code comments in
`abyss.cpp`/`render.cpp` for the technical detail): a matrix-stack overflow from nesting 3D rendering
inside the existing 2D scene canvas's own pushed transform (fixed with a dedicated depth-buffered
render target, `Mode3DRT()`, composited in like `PixelRT`/`OceanRT`), and a degenerate camera (a
velocity-derived look direction goes parallel to the up vector during a near-straight-down fall,
silently producing a blank render) - fixed with a fixed chase-camera offset instead.

## Not yet done

- The other biomes: new platform versions of Island/Cave/Weeds/Atlantis, and retrofitting Pipes/Hull/
  Pirate Ship onto the shared personality/ecosystem framework.
- The Open Abyss's remaining species (Leviathans, Siphonophore Colonies, Vampire Squid, Hadal
  Trench-Worms, Deep-Sea Hatchetfish, Heavy Brine-Slugs) and remaining set-pieces (Siphonophore
  mazes, thermal-vent updrafts, brine-pool shelves with inverted glide physics, Isopod bowling
  lanes).
- The pressure-distortion screen-space shader, marine-snow velocity telegraphing, and background
  Leviathan silhouettes.
- Streaming/infinite generation past this slice's fixed ~900-unit trench.
- A real way to reach the Abyss in-game (a Periscope/Helm entry point) - it's debug-launch-only for
  now (`--shots ... abyss`, or setting `g.scene = Scene::Abyss` directly).
- Whether biome transitions (e.g. falling from the Hull into the Abyss) are seamless or gated by a
  loading/transition room - raised in the brief, not yet decided or built.
