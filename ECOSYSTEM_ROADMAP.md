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

## Status: the Open Abyss is now a complete, fixed-length slice

Decided with the user (see "Decisions made" above for the earlier round; this is the follow-up
round, before this pass started): the Abyss is "finished" as a **complete slice at its existing
fixed ~900 m length** - the full species/set-piece roster below, a real in-game entry point, a
standalone Helm/Periscope menu entry (no physical connection to the Hull's 2D level) - with
infinite/streaming generation explicitly deferred as its own future project, not part of "finished."

Added on top of the first vertical slice above:
- **The remaining species**: Vampire Squid (a faster, more aggressive hunter than the Gulper Eel,
  only in deeper water), Trench Worms (coiled in the wall, lunging at anything that lingers close),
  Hatchetfish (harmless ambient schools that flee a disturbance and drift back), Brine Slugs (slow,
  heavy, crushing - the hazard the bowling lane and brine pool both lean on), and Leviathans (a
  colossal, mostly-background presence patrolling slow depth bands; rarely, gated by its own rolled
  aggression, it deviates into a fast, dangerous close pass).
- **The remaining set-pieces**, generated once per seed as depth-banded zones layered on as physics/
  behaviour effects rather than changes to the trench's own (proven, verified) wall geometry:
  a thermal-vent updraft column, an Isopod bowling lane (isopods let go on their own clock, not just
  on request), a Siphonophore maze (a band of static stinging tendrils meant to be threaded slowly),
  and a brine-pool arena (gravity cut well down, a distinctly different body of water to swim
  through).
- **A real fail state**: stamina is air, warmth, and health at once - every hazard drains it hard
  (gated by a brief immunity window so one hit can't chain into ten) rather than killing outright,
  and reaching zero is the one thing that ends a dive. This was a real gap in the first slice: the
  Gulper Eel could already "catch" the player but nothing happened when it did.
- **Visual polish**: a pressure-distortion screen-space shader (a subtle depth-scaled UV wobble on
  the composited 3D render, skipped in the headless verifier where no GL context exists), marine
  snow that streaks harder in the seconds before a down-draft hits (so the water telegraphs the
  hazard), and Leviathans drawn as huge, near-black, low-alpha masses with two faint eyes - meant to
  read as "something is down here," not as a fully modelled monster.
- **A real entry point**: a fourth Periscope card ("The Open Abyss," gated behind clearing the
  Pirate Ship), styled distinctly from the three platform-level cards since it isn't another
  chunk-layout level. Reaching the bottom pays out gold and a relic (the deepest dive, so it pays
  the most); dying or surfacing both show an overlay with the outcome before returning to the Helm.
  `abyssCleared`/`abyssBest` persist in the save file.
- The scripted diver in `--verify-abyss` now also steers away from whatever hazard is nearest, not
  just toward the shaft's centre - it isn't smart, but the descent has to be provably survivable
  with the mechanics as tuned, not just in theory. Passed 65/65 runs across random seeds while this
  was being tuned.

## The Pipes: retrofitted onto the ecosystem framework (life, not enemies)

Checked with the user first: "retrofit the existing Pipes" turned out to mean two different things the
roadmap could have meant (retrofit it in place, or start a brand-new platform biome), and the answer was
retrofit-in-place. The Pipes are deliberately the one platform level with no enemies (see CLAUDE.md and
ROADMAP.md's own file-map comment in platformer.cpp) - touching one anywhere else is fatal - so this
retrofit adds *life*, never a *hazard*: no collision or death check anywhere touches it.

- `PersonalityProfile` (previously Abyss-only) moved earlier in game.h so both biomes can share it, and a
  new `PlatCritter`/`CritterState` pair sits right beside `PlatEnemy` - little vermin skittering along the
  duct floor, each with its own rolled aggression/bravery/energy/curiosity.
- `PopulateCritters` (Pipes-only, skipped headlessly) scans the generated tile grid for real floor spots
  (open tile over solid ground) and seed-jitters a sparse population from them - about 25-30 across a
  typical ~240-wide level, capped at 40.
- `UpdateCritters` gives each one the same Flee/Investigate/Idle shape as the Abyss's ecosystem: bolt away
  from a close pass (braver ones tolerate it longer), creep toward a diver standing still if curious enough,
  otherwise a short skitter near home and a long pause. Movement reuses the crab enemy's own walk-and-turn-
  at-a-wall-or-ledge logic, so nothing ever runs off a platform.
- `DrawCritter` is a small, flat, unrotated silhouette (the Pipes' 2px art grid has no sprite rotation) with
  a wobbling pair of legs - just enough to read as alive under the helmet lamp.
- `depth.exe --verify-critters`: a headless smoke test (population happens, standing on top of one makes it
  flee, no position ever blows up) - the same spirit as `--verify-abyss`, since nothing here needs a window.

## The Hull: crabs and eels gain personality, and a real bug got fixed along the way

As predicted above: unlike the Pipes, the Hull already has real enemies (crabs and eels), so its retrofit
gives *them* the personality/perception system rather than adding new ambient life.

- `PersonalityProfile` moved earlier again (now above `PlatEnemy`, which needs it as a real member, not just
  in a vector) so crabs and eels each roll one (`RollEnemyTraits`, hashed from their own spawn tile - stable
  across a checkpoint respawn's rebuild without threading the level seed down to `ScanTiles`).
- Crabs: energy scales patrol speed (55-95 instead of a fixed 70); an aggressive one (>0.6) that notices the
  diver at its own height turns to charge instead of patrolling past - but only if that direction doesn't
  walk it straight off its own platform's edge (checked the same way the existing patrol turn is, so a smart
  aggro override can't be undone a line later by that same check, and a crab never suicides off a ledge just
  to give chase).
- Eels: aggression shortens the leap cycle (1.7-2.6s instead of a fixed 2.6s); a curious one (>0.6) lingering
  near a diver who's lingering over its hole leaps early instead of finishing out a long dormant phase - the
  same "notices a presence" shape as the Abyss's `Disturb()`, just proximity-triggered instead of acoustic
  (the 2D platformer has nothing analogous to a dash to react to).
- `depth.exe --verify-hull-life`: proves personalities aren't degenerate in a real generated layout, then
  proves the *mechanism* itself (charge-on-aggression, leap-early-on-curiosity) on a synthetic crab/eel with
  an unambiguous long floor either side - a real generated crab's platform is sometimes too narrow to charge
  either way, which is a correct refusal, not a mechanism failure, so it's the wrong thing to assert on.

**A genuine pre-existing bug, found and fixed**: eels (and parakeets) had been complete dead code since the
Hull/Pirate Ship generator was rewritten to `BuildTrench`/`BuildFleet` (see ROADMAP.md's "Platformer
macro-structures" pass). The only place that ever set `'e'`/`'p'` tiles was gated by `level >= 1`, but that
code sat entirely inside the generator's `else` branch that only runs `if (level == 0)` (Pipes) - so the
condition could never be true where the code could run, and vice versa. Confirmed via `--gen 1 <seed>`
across 40 seeds (zero eels, zero parakeets) before touching anything. The game's own how-to-play text has
been telling players "crabs, leaping eels, urchins and mines" this whole time for a Hull that could never
actually contain an eel. Fixed by adding real eel spawns directly into `BuildTrench`'s torpedo-gap and
rock-reef features (occasionally replacing a plain urchin tile), confirmed via `--gen` and
`--verify-hull-life` that eels now genuinely appear. Left the Pirate Ship's parakeets alone for this pass -
out of scope ("the Hull section") - but the same dead-code gap almost certainly affects them too.

## Not yet done

- The other biomes: new platform versions of Island/Cave/Weeds/Atlantis.
- The Pirate Ship's own retrofit (parakeets and gunners gaining personality) - and very likely the same
  dead-code eel/parakeet-placement bug fix, scoped to `BuildFleet` this time.
- Streaming/infinite generation past the Abyss's fixed ~900 m trench - deliberately deferred, see above.
- Whether biome transitions (e.g. falling from the Hull into the Abyss) are ever made seamless rather
  than a standalone menu entry - raised in the brief; for this slice, decided as standalone (simplest,
  matches how Pipes/Hull/Pirate Ship are already picked), revisit if a future pass wants otherwise.
