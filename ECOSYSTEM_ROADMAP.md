# Depth: the ecosystem/parkour expansion (Rain World-inspired)

This tracks a large addition the user asked for, kept separate from ROADMAP.md (the visual-art
roadmap) because it's a different initiative: new parkour content and systems, explicitly **not**
touching the main game (the Nautilus hub, expeditions, Flats) at all.

## ⚠ CURRENT PRIORITY (supersedes "Weeds next" below): a real creature-AI rework, handed to Opus

After commit 64b4901 the user gave this feedback verbatim:

> "The beasts in every level still don't feel like they are part of an environment. Some feel like they
> just sit there and do nothing (have no reaction to the player) and the one's that are meant to follow
> and try and out smart the player either don't exist, or sit only on two block ledges and follow a
> goomba path from mario. These beasts should have the developed intelligence of animals. They should be
> interacting with each other and the environment. There should be parts of the environment generated
> that are unique to them (if ever far off in the level that they will seek safety in). Feel free to ask
> questions now. These creatures should have genuinely developed AI's that make them living instead of
> stagnant. If this is a project for an opus model tell me"

Clarifying questions were asked; the user's answers (binding):
- **Depth: full grid pathfinding for wandering and fleeing** - not just richer state machines. Creatures
  must plan real routes across the level's tile grid to a chosen destination (a shelter, a pursued
  target, a roam point) instead of today's `EcoWander` / `PipeWander` "walk until a wall or ledge, then
  turn around" - which is literally the "Mario goomba path" the user is describing. The player-side
  `Crossable()` A* in platformer.cpp (used by `--verify`) is the obvious reference for how to search this
  grid with the real movement constraints, but a creature navigator doesn't need the player's full jump
  physics - most species walk/crawl/fly/swim, not wall-jump - so it wants its own, cheaper graph (walkable
  floor spans + which spans connect by a drop/step/short hop, per species' movement type), precomputed
  once per `BuildLevel` rather than searched from scratch every frame.
- **Shelters: first-class, in all five generators, this pass** - each structural generator (BuildTrench,
  BuildFleet, BuildIsland, BuildCave, and the Pipes' pass-1/pass-2 core) gets real creature shelters
  (dens, burrows, crevices, nests - biome-appropriate) that threatened prey navigate to and are safe
  inside, "if ever far off in the level." These are for creatures, never the player's critical path, so
  they must not break `--verify` crossability. **Open conflict to resolve first:** CLAUDE.md and the
  user's own earlier feedback say the Pipes are "fine as-is, leave alone" - check with the user whether
  that still holds now that they've explicitly asked for shelters in all five generators, before
  touching the Pipes' generator core.
- **Rollout: prove it on ONE biome first, then propagate** to the rest. Not yet chosen which biome -
  recommendation: **the Hull**, since it already has the clearest real predator/prey pair (crabs, eels)
  plus a full 7-species overlay, so the gap between "what exists" and "genuinely alive" is easiest to see
  and test there. Confirm with the user or pick and say so.
- **Model: the user asked for this to be done on Opus**, not in the Sonnet session that built the
  biomes so far. This section exists so an Opus session can pick it up cold. (The user has since
  switched; this work is now running on Opus 5.5.)
- **Decisions taken on the open points above (Opus, same session):** the Hull is the proving biome.
  The Pipes DO get creature shelters - the user explicitly chose "all five generators" after the "leave
  the Pipes alone" note, so that answer supersedes it - but only as creature-side structure: no change
  to the Pipes' platforming, hazards, set-pieces or `--verify` crossability.

What "genuinely alive" needs to cover, synthesized from this message and the two earlier AI notes below
(the swim/eel/school/death note and the exploratory-personality note - they all belong to this same pass):
1. **Perception that actually reacts to the player** - several species today never read `p.pos` at all
   except by accident (the Pipes' chain deliberately; others just never got a check). Every species needs
   a sight/hearing model appropriate to it (with line-of-sight against solid tiles, not just a radius)
   and a short memory of where it last noticed the player/a threat.
2. **Predators that genuinely hunt** - pursue along the navigation graph (the "outsmart the player"
   complaint), including predicting where prey is heading, not just chasing its current tile; give up
   based on personality (aggression/energy), not a fixed timer.
3. **Prey that genuinely flee** - to the nearest reachable shelter via the navigation graph, not just
   "away" along one axis; schooling species keep formation with personality-driven stragglers a predator
   can target (earlier note).
4. **Creatures reacting to each other**, not only to the player - alarm propagation (one prey spotting a
   predator warns nearby prey), predator/prey pairs across species within a biome, and a real terminal
   "eaten/dead" state with removal + slow repopulation (earlier note: "the beasts can perish").
5. **Real movement modes per species** - swimmers actually swim through open water in the aquatic
   biomes instead of crawling along the floor (earlier note), fliers fly, crawlers crawl; the navigation
   graph must respect each mode.
6. **Exploratory personality** (earlier note) - a trait that makes some individuals roam far from home
   and explore the map, choosing their own path, rather than every creature being leashed to its spawn.

Everything below this section still stands, but is lower priority than this until the user says otherwise.
Weeds and Atlantis (the remaining two new biomes) are on hold behind this.

### Guiding spec: ParkourReference1.2.pdf (in the repo root)
The user then supplied `ParkourReference1.2.pdf` as the direction for the whole parkour section. Order
taken: its AI sections first (perception, memory, utility, personalities, food web, dens), then player
physics (slide, impact roll, poles, hydro-glide, water dash, parachute brake), FABRIK IK animation, and
shaders/auto-tiling. It is written for SFML; everything is implemented in raylib here.

### Status: the beast engine is built and proven on the Hull
- `src/beasts.h/.cpp`: the four-tier pipeline (senses with sight cone + LOS raycast, hearing I/(1+kd^2),
  a diffusing Eulerian scent grid for blood and the diver's trail; a 12-slot decaying memory with
  trauma bits; hunger/fear/fatigue drives; sigmoid utility with hysteresis; A* motor for walkers and
  swimmers with boids schooling), the six personality axes plus the 12 abnormal profiles (~15%), a food
  web, corpses that sink/bleed/get scavenged, dens (tile 'D', Poisson-disc placed by `PlaceDens` in all
  five generators, solid like '#', so `--verify` is unaffected) that prey hide in and that repopulate.
- The Hull runs ten species (sprat schools with stragglers, cleaner shrimp grooming eels, octopus with
  camo/ink, pufferfish, leeches riding hosts, anemones, hermit crabs mobbing, brittle-stars that break
  under a fast diver, barnacle crabs, moray eels that ambush from breaches and patrol). The old Hull
  chain (`PopulateEcoLife` etc.) was removed.
- `depth.exe --verify-beasts` (with `DEPTH_BEASTLOG=1` for per-state histograms of a minute of real Hull
  life) checks routing, LOS, hunt/eat/scavenge, hiding, straggler selection (the confusion effect: a
  strike into a tight school usually misses and the hunter re-targets the loneliest fish), roaming,
  and the abnormal rates.
- **Propagated to all five levels** (beasts_biomes.cpp): Pirate Ship (rats, ship's cats, powder monkeys with
  lit kegs, guard dogs and their fleas, barn owls, gulls, an albatross), Island (boar, tree snakes, monitor
  lizards, fruit bats, orb spiders, coconut crabs and their coconuts, dart frogs, gulls, hunting dogs), Cave
  (bats, glow jellies, blind salamanders, fungal beetles, ceiling leeches, tube worms, moths), Pipes (moths,
  water-spiders, centipedes, blind pipe-rats, rust-mites, pillbugs, mice, cockroaches, glow-beetles, crickets;
  none of it notices the diver). The old scripted chains and their structs are removed; the old verifier flags
  now run each biome's new tests.
- Engine additions on the way: prey hears predators and hunters hear prey (the dark levels run on sound),
  hungry predators forage, loud crashes startle the skittish, the level's own enemies are threats, stray shots
  and blasts kill (friendly fire), walkers have ledge sense and judge every leap's landing (`SafeArc`), bodies on
  spikes are left alone, scavengers give up on meals they can't reach.
- IK (PDF "Procedural Animation"): `ik.h` FABRIK + two-bone knee; planted-foot diagonal gaits for walkers.
- Next: more IK (octopus arms and tentacles reaching with FABRIK, death throes and being-eaten animation),
  art polish at play scale (creatures read small), then the PDF's player-physics section, then Weeds and Atlantis.

## Status as of commit cd1fe26: Island and Cave both built; Weeds and Atlantis next

The Island (commits 8ee3412, 44ae583, cd1fe26) and the Cave (commit 42ebeff) are both done - see
their own commit messages for the technical detail. Playtest feedback, addressed along the way:
- The Island first read as a Hull reskin (shared tile vocabulary, recolored rather than redesigned
  creatures) - fixed (44ae583): every tile/creature the Island touches now has its own real art
  (vines not seaweed, thorned bark not barnacles, a mud wallow not a steam jet, a real four-legged
  lizard not a recolored crab, a zigzag-bodied snake not a recolored eel).
- Adding a fourth platform level pushed the Periscope's Abyss card off the visible screen with no
  way to reach it - fixed in the same commit (card layout is now sized to fit PL_COUNT + 1 cards on
  screen, not hand-tuned for exactly three).
- **"Do not mirror the levels. They should be distinct levels that have distinct layouts."** - given
  while the Cave was being built as another floor-level set-piece carousel just like the Island's.
  Redesigned around a genuinely different traversal shape instead: the Cave's dominant motif is a
  winding chain of wall-jump shafts that actually change the level's standing height as you climb and
  drop, not a fixed floor row with set-pieces breaking it up. **Apply this same distinctness test to
  Weeds and Atlantis too** - each of the four new biomes should have its own dominant traversal shape
  (Island: floor-level crossing of set-pieces; Cave: vertical shaft-chain), not a reskin of a shape
  already used. Decide Weeds' and Atlantis' own shapes before writing their generators, the way the
  Cave's "climb dominant, not floor dominant" idea was decided before BuildCave was written.
- **"The island level looks like a forest not a tropical island inhabited by untouched locals
  worshipping strange gods."** - fixed (cd1fe26): rebalanced the feature carousel so the two
  worship-site set-pieces (idol climbs, village stands) dominate instead of generic jungle floor: the
  skyline and midground are now dominated by carved idol silhouettes rather than trees; the ground
  reads as worked stone with buried glyphs, not packed earth; totem poles (carved rings, a lashed
  skull, a trailing feather) are planted procedurally throughout, not just at village stands. Keep
  this "worship site over wilderness" bar in mind for the Island's future AI/art passes too - it's
  easy to regress back toward "generic jungle" by leaning on tree/vine art for new content there.

## Status as of commit 9a0159c (checkpoint before starting the four new biomes)

**The entire original feedback list (below) is now done**, including the Pirate Ship retrofit:
dash double-tap, real predator physics/AI (Pursue/FleeFrom, ambient perception, predictive lead,
personality-scaled persistence), Hull creature size/silhouettes, Abyss creature silhouettes (Isopod/
Eel/Squid/TrenchWorm/Hatchetfish/BrineSlug/GlassSponge all redone), free-look+clamped Abyss camera, a
wider trench, three horizontal-cavern crossings with blocking Rock Ledges, more general resting
ledges, and the Pirate Ship's personality-rolled crew plus its other 7 species and their real chain
(Cat/Rat, scare/fuse/explode/scatter/infest/berserk - see commit 9a0159c for detail). All of
`--verify`, `--verify-abyss`, `--verify-hull-ecosystem`, `--verify-pipe-ecosystem`,
`--verify-pirate-ecosystem`, `--verify-hull-life`, `--verify-critters`, `--flats-ui-test` pass.

Investigated (not fixed, because it turned out not to be broken): the Pirate Ship's suspected
eel-style dead-code bug for parakeets. Empirically counted 102 literal parakeet tiles across 40
generated layouts - they were already being placed. The real gap was just the missing personality
roll, which is now fixed.

Not done: the Pipes were left alone per the user's own note that it's fine as is. The Abyss trench
wall's own texture/readability at its new larger scale is still fairly soft/hazy at a distance -
noted but not chased further, since it's a minor polish item behind the much larger work below.

**Next action: the four new parkour-only biomes, one at a time, in this order: Island, Cave, Weeds,
Atlantis.** Each needs the same production bar as Pipes/Hull/Pirate Ship: a new `PlatLevel` entry (bump
`PL_COUNT` and every array sized by it), a `LevelDef` in `Lv()`, a structural generator function in
levelgen.cpp (the equivalent of `BuildTrench`/`BuildFleet`) validated by the existing jump-arc/
`--verify` machinery, a full enemy/hazard set (not just an ecosystem overlay - Tribal Warriors/Feral
Boars/etc. for the Island are real hazards, per ECOSYSTEM_BESTIARY.md), its own art pass, and a
Periscope entry. Each must scale harder than the Pirate Ship, and per the user's explicit instruction
must NEVER touch the existing turn-based expedition locations of the same name (Location::Island
etc. in dungeon.cpp/data.cpp are a completely separate system - do not modify them). The user also
suggested the Island specifically could randomize between a temple, a village, or a coast-to-coast
traverse over hills/palm trees/a small mountainside - a good model for the other three biomes having
their own internal variety too, not just one fixed layout theme apiece.

This is a substantially larger undertaking than the retrofit work above (each new biome is
comparable in scope to building the Hull or Pirate Ship originally). Budget context/session usage
accordingly - checkpoint and commit after each biome, not just at the end of all four.

## Original work order (the user's own words, playtest feedback after the first PR merge)

The user tried the Pipes/Hull/Abyss, liked the mechanics, and gave this feedback verbatim - treat
this whole section as the standing task list until every item is checked off or explicitly punted:

- **Dash input**: they don't know how to dash; expected double-tap of a movement key (like many
  games), not just a dedicated button.
- **"There are no physics applied to the beast first of all."** Creature movement needs real
  physics (acceleration/momentum), not snapped-to-target velocity.
- **The Pipes**: current ambient-life-only approach is fine, leave as is for now.
- **The Hull**: most creatures are too small to identify what they are.
- **The Abyss creatures**: need real visual definition - right now they read as plain geometric
  primitives, not animals.
- **The Abyss camera**: can't look around; gets too close to the trench wall and shows outside the
  level. Needs free look and to stay clamped inside the trench.
- **The Abyss play area**: should be larger (wider trench).
- **The Abyss needs small ledges** to stop a descent and let the player wait out danger below -
  more/different from the existing Glass Sponges.
- **The Abyss is too easy**: creatures don't do anything, don't seek out the player. Needs real
  predator behavior.
- **The Abyss needs horizontal sections** between vertical free-falls, not just a straight drop.
- **Every creature (all biomes) should behave like a real animal**: physics-driven, hunting like a
  hunting animal, fleeing like scared prey. "Every beast should also be smart (until it has the
  chance to get the dumb personality, make sure all of the personalities are created)" - i.e.
  competent/intelligent pursuit should be the norm, with the full personality range actually
  represented, not creatures that are trivially easy to outrun or juke.

**Work order** (explicit, do not reorder): finish all of the above first. Then the Pirate Ship
retrofit (7 more species from ECOSYSTEM_BESTIARY.md, its own eel/parakeet dead-code bug, matching
the Hull's approach). Then the four new parkour-only biomes **one at a time**, each with the same
production bar as Pipes/Hull/Pirate Ship (procedural generation, jump-arc validation, `--verify`,
art pass) but a uniquely-designed layout per biome (e.g. the Island could randomize between an old
temple, a village, or a coast-to-coast traverse over hills/palm trees/a small mountainside) -
**difficulty scaling above the Pirate Ship**, in this order: Pipes < Hull < Pirate Ship < Island <
Cave < Weeds < Atlantis (the Abyss is its own separate difficulty track). These four are parkour-
section-only additions and must never touch the existing turn-based expedition locations of the
same names. If time/budget remains after that: keep improving creature AI, keep improving creature
art/design, then build the level-randomization system for the new content on the same standards as
the game's existing procedural generation (the kinematic platform generator, `visSeed`/atmosphere
system, etc.).

The user said they'd be unavailable for a long time and explicitly asked for autonomous, continuous
work with no further check-ins - proceed through this list without waiting for confirmation. They
also asked: if a usage/rate limit is approached, pause and schedule a resume for when it resets
(`CronCreate`, not `ScheduleWakeup` - this isn't a `/loop` session) rather than stopping silently;
and to commit/push work before the session gets too full, so nothing already done is ever at risk
of being lost.

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

**Superseded, not replaced, by the Pipes' real chain below.** This pass predates the full brief and is one
stand-in species reacting to the diver; the real Pipes chain (ten species that *ignore* the diver entirely)
now runs alongside it. Both stay - see the next section.

## The Pipes' real ecosystem chain: the ten-species systemic-chaos version

Per `ECOSYSTEM_BESTIARY.md`, the Pipes are the one biome where "entities ignore the player; all hazards stem
from systemic chaos and collateral physics" - a fundamentally different shape from every other biome's chain
(which all react to the diver somewhere). So this is the one retrofit that adds *zero* diver-reactivity: it
runs identically whether or not the diver is anywhere nearby, matching CLAUDE.md's "the Pipes have no
enemies" as literally as possible - not just "nothing here can hurt you" but "nothing here has ever heard of
you." The existing `PlatCritter` pass above is kept running alongside it unmodified.

The chain: Dust Moths flutter toward the duct's surviving light leaks (the same sparse `'o'` tiles kept as
"faint pools of light" - see `PopulateEcoLife`'s Hull comment for the identical mechanism); Water-Spiders sit
at a fixed web and catch a Moth that flies through it; Centipedes come eat a caught Moth, freeing it back to
flutter off; Blind Pipe-Rats hunt a feeding (Hunting-state) Centipede by proximity and, if aggressive enough,
bite into the pipe on contact; Rust-Mites swarm out at a nearby bite to feed on the flakes, and a swarming
Mite curls up any Pillbug it brushes; a curled Pillbug starts rolling like the Hull's rolling hazards, and a
rolling one startles off a hunting Scavenger Mouse; a Cockroach that's aggressive enough and near a Mouse
picks a fight; a fight that happens near a Glow-Beetle makes it flash; a flash near a Cave Cricket panics it
into an erratic, leash-ignoring stampede.

- `game.h`: `PipeKind`/`PipeState`/`PlatPipeLife` - one struct for all ten, same shape as `PlatEcoLife`, states
  meaning different things per kind. `PlatformState` gains `pipeLife` and `lightSpots` (the latter: the
  surviving `'o'` tile positions, scanned once in `PopulatePipeLife` so Moths don't need to rescan the grid
  every frame).
- `PopulatePipeLife` (platformer.cpp): same seed-stable floor scan as `PopulateCritters`/`PopulateEcoLife`,
  weighted-picking a kind per spot (Moths commonest, denser overall than the Hull - a whole ten-species chain
  needs more bodies - capped at 55).
- `UpdatePipeLife`: the chain above; every lookup is entity-to-entity (nearest caught Moth, nearest Hunting
  Centipede, nearest Biting Rat, etc.) - `p.pos` (the diver) is never read anywhere in it.
- `DrawPipeLife`: small flat unrotated silhouettes, same style as `DrawCritter`/`DrawEcoLife`, one shape per
  species (a Water-Spider's web ring, a Glow-Beetle's flash bloom, a Pillbug curling into a ball, and so on).
- `depth.exe --verify-pipe-ecosystem`: a real generated layout spawns several distinct species, then the
  mechanism is proven on synthetic setups (a Moth flying into a web then freed by a Centipede; an aggressive
  Rat biting near a Hunting Centipede, waking a Mite swarm, curling then rolling a Pillbug, and scaring off a
  Mouse; a fight flashing a Beetle and panicking a Cricket) - and, uniquely to this biome, an explicit check
  that sweeping the diver across an entire real level's worth of frames never makes any entity's position
  blow up or NaN, since nothing in this code path should ever even look at where the diver is.

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

## The full brief now lives in ECOSYSTEM_BESTIARY.md

The user's complete, seven-biome ecosystem brief (verbatim: `WaterTraversalState`/Water Dash/Hydro-Glide,
the numbered food chains and cause-effect diagrams for all seven biomes, and the `PersonalityProfile`/
`EntityBrain` pseudocode) is now persisted in `ECOSYSTEM_BESTIARY.md` so it never has to be re-pasted. It's
the source of truth for what each biome's real chain is; this file tracks what's actually built against it.
Correction from earlier in this file: the Pipes' ambient duct life above is one species (a stand-in for the
brief's Pipes chain of ten), not a match to the brief - it's flagged there as a gap, not finished.

## The Hull's real ecosystem chain: the other 7 species

The brief's Hull chain (`ECOSYSTEM_BESTIARY.md`, "The Hull") is: Cleaner Shrimp draw Camouflage Octopuses
into ambush; Barnacle Crabs (the existing `'c'` enemy) pinch an Octopus that lands on their bed; either
pinch, or the player bumping an Octopus, sprays an ink cloud; Pufferfish caught in it panic and puff up into
a real hazard; a puffed Pufferfish knocks a Hull-Leech loose to drift; a Stinging Anemone catches a drifting
Leech; Hermit Crabs scavenge an Anemone's scraps; Brittle-Star mats break underfoot.

- `PlatEcoLife` (game.h): one struct for all seven, `EcoKind` picks the species and `EcoState` the state -
  the same states mean different things per kind (`Hidden`/`Ambush` only apply to the Octopus, `Puffed` only
  to the Pufferfish, `Fed` only to the Anemone, etc.), the same way `CritterState`'s three states already
  cover the Pipes' whole vocabulary with one enum.
- `PopulateEcoLife` (platformer.cpp): same seed-stable floor scan as `PopulateCritters`, picking a kind per
  spawn spot by a weighted roll (Shrimp commonest, Brittle-Star rarest) and capped at 45 - sparser than the
  Pipes' critters, since this is a whole food chain, not a swarm.
- `UpdateEcoLife`: the chain above, implemented as read of each entity's own neighbours every frame (arrays
  are small, so an O(n^2) scan per frame is cheap) rather than pointers between them, so entities can be
  freely recycled (a captured Leech respawns at its own home; a spent ink cloud just expires).
- The mat/hazard end of the chain is deliberately conservative: Brittle-Star mats never sit over an already-
  deadly tile (they're scenery over safe floor), so breaking one gives the player a small downward velocity
  dip rather than an actual drop onto a hazard underneath - the brief's "dropping onto underlying hazards"
  read literally would mean touching Depth's tile-solidity code (`Solid()`), which every jump-arc validator
  and the whole movement model depend on; not worth that risk for one set-piece. Only the player triggers a
  mat break, not walking Barnacle Crabs (also in the brief) - out of scope for this pass.
- Only a puffed Pufferfish is an actual hazard (touching it kills, same check as `p.enemies`/`p.shots`);
  everything else in the chain is scenery, same rule as the Pipes' critters.
- `depth.exe --verify-hull-ecosystem`: a real generated layout spawns several distinct species (not just
  crabs/eels), then the mechanism is proven on synthetic setups the same way `--verify-hull-life` proves the
  crab/eel mechanism - bumping an Octopus sprays ink, a Pufferfish in it panics and puffs, a puffed Pufferfish
  detaches a Leech, and (a separate synthetic, since stacking an Anemone at the same point as the other three
  would recapture the Leech in the very same frame it detaches) a Leech already adrift near an Anemone gets
  captured. A Brittle-Star mat also confirmed to survive a first touch and break only after a moment.

## Not yet done

- The Pirate Ship's own real 10-species chain, per `ECOSYSTEM_BESTIARY.md` - hasn't been touched (its own
  retrofit, and very likely the same dead-code eel/parakeet-placement bug fix, scoped to `BuildFleet` this
  time). The Pipes and the Hull are both now built.
- Four new parkour-only biomes (Island, Cave, Weeds, Atlantis) per the brief - additions to the parkour
  section only, never touching the turn-based expedition locations of the same names.
- Streaming/infinite generation past the Abyss's fixed ~900 m trench - deliberately deferred, see above.
- Whether biome transitions (e.g. falling from the Hull into the Abyss) are ever made seamless rather
  than a standalone menu entry - raised in the brief; for this slice, decided as standalone (simplest,
  matches how Pipes/Hull/Pirate Ship are already picked), revisit if a future pass wants otherwise.

## Next phase, AFTER the four biomes: deeper beast AI/physics/art (user's verbatim feedback, given mid-session)

The user gave this while Island work was starting. Explicit instruction: finish Island/Cave/Weeds/Atlantis
**first** ("after all of that I want you to continue developing..."), then come back to this list. Recorded
here in full so it survives a context reset - do not lose it, do not start it early unless all four biomes
are done or the user says otherwise.

Verbatim: "Then after all of that I want you to continue developing the individual beast AI/intelligence and
their physics. In levels where there is water the fish/sea creatures that swim only lay on the ground and
move at a crawl. They should be moving about, swimming. Part of the personality is if they are generated in
a school of fish some may not understand to stay with the pack of fish when chased by a predator which a
predator can pick up on. There are no predators but the crabs in the hull level. The eels are missing and
they should be swimming around. The abyss level still doesn't have a lot of art development both in the
environment and in the character and in the beasts so they are still geometric blobs. I don't really know
how the beasts interact. There should be animation in all levels as the beasts interact with each other they
devour their prey the prey gets scared and runs. The beasts can perish."

Breaking that down into concrete gaps against what's actually built:
- **The Hull's `PlatEcoLife` (Shrimp/Octopus/Puffer/Leech/Anemone/Hermit/BrittleStar) all move via `EcoWander`
  - a ground-hugging walk-and-turn-at-a-ledge patrol, the same helper the Pipes' land critters use.** The Hull
  is underwater; these are meant to be fish/sea life, not vermin, and should actually swim through open space
  (a real 2D drift/patrol volume, not pinned to a floor row) - this is the literal "lay on the ground and
  crawl" complaint and needs its own swim-capable movement helper, separate from `EcoWander`.
- **The Hull's eels are still not reading as present/swimming.** They were fixed from being genuinely dead
  code (see the "genuine pre-existing bug" section above - they do now spawn, confirmed via `--gen`), but
  their movement is a scripted leap-from-a-hole-on-a-timer, which apparently doesn't read as "an eel swimming
  around" to the user even when it's technically on screen. Likely needs a real swim/patrol phase between
  leaps, not just dormant-then-leap-then-dormant, plus a visual pass so it's unmistakably an eel and not
  another blob.
- **Only the Hull's crabs currently function as real predators anywhere.** The user wants more biomes (and
  more species per biome) to have a real predator role, not just decorative chain reactions - most of the
  current ecosystem work (Pipes ten-species chain, Hull's Octopus/Pufferfish/etc., Pirate Ship's chain) is
  scenery/cause-effect, not predation.
- **School-of-fish behaviour with personality-driven stragglers**: when a school (Hatchetfish in the Abyss is
  the closest existing example) is chased by a predator, some individuals (by rolled personality - low
  bravery/energy, maybe low curiosity too) should fail to keep formation and lag behind - and predators should
  be able to target that straggler specifically, not just the school's centroid. Needs an actual flocking/
  school-cohesion behavior (separate from `FleeFrom`) before this can exist anywhere.
- **The Abyss's visual pass is still not enough** - despite the redraw work already done (Isopod/Eel/Squid/
  TrenchWorm/Hatchetfish/BrineSlug/GlassSponge as multi-part composites with a `face` direction vector), the
  user still sees "geometric blobs," for both creatures AND the environment (the trench wall's "fairly soft/
  hazy" texture noted earlier is part of the same complaint). This needs a real further pass, not just the
  one already done - treat the earlier redraw as a first draft that didn't land, not as finished.
- **Real predator-prey interaction/animation across every biome**: devouring (a kill should have a visible
  animation/effect, not just a state flip), fleeing (already partly done via `FleeFrom` in the Abyss, needs
  parity elsewhere), and death (creatures should be able to actually die/despawn as part of the chain, not
  just change state forever) - "the beasts can perish" is a new requirement: currently nothing in any
  ecosystem chain permanently removes an entity (see `UpdateEcoLife`/`UpdatePipeLife`/`UpdatePirateLife`/
  `StepAbyss` - every state is cyclic, nothing has a terminal "eaten" or "dead" outcome that removes it from
  the vector). This means each biome's life vector needs to support erasing entries and needs a Died/Eaten
  terminal state with a real animation, likely with slow respawn/repopulation so a level doesn't empty out
  over a long play session.

This is a big, cross-cutting pass (touches every biome's ecosystem code, not just one), likely comparable in
scope to the whole Pirate Ship retrofit again. Suggested shape for whoever picks this up: (1) a real swim
helper for aquatic biomes (Hull first, since it's the specific complaint), (2) give eels a swim-patrol phase,
(3) a school/flocking helper with personality-driven straggling, (4) a genuine "can die" terminal state with
removal + respawn, usable by any chain, (5) then the Abyss's second art pass (environment first, since that's
called out as behind the creatures), (6) expand real predation beyond the Hull's crabs where the brief
supports it (e.g. the Hull's eels, once they swim, are a natural second predator).

**Follow-up, same session, before any of the above was started** - another verbatim note, add to the same
pass rather than treating as separate: "Also, and possible personality is that a beast can be exploratory.
This adds to the fact that all beasts are not subject to certain walk/swim/fly patterns. They should be able
to move in procedurally generated intelligent ways that feel alive. Like they are choosing their path. The
exploratory personality has they continuing to move well beyond where they started and exploring the map."

Concretely: right now essentially every creature's movement (`EcoWander`, `PipeWander`, the Island's own reuse
of `EcoWander`, even the Abyss's `Pursue`/`FleeFrom`) is leashed to a `home` position - it patrols, hunts or
flees, but always snaps back toward where it spawned, and the leash distance is a fixed constant per call site,
never personality-driven. The user wants a new personality axis (an "exploratory" trait, alongside aggression/
bravery/energy/curiosity - decide whether it's a 5th `PersonalityProfile` field or derived from existing ones,
e.g. high curiosity + low bravery-need) that lets a creature roll a real, wandering, long-range path across the
level rather than sitting in its home radius forever - "choosing their path" procedurally (not a fixed patrol
loop) is the key ask, so this likely wants a genuine steering/pathing behavior (a random-walk waypoint picker
over the level's actual solid tiles, or a noise-driven wander target that relocates itself periodically) rather
than the current leash-and-return model, at least for creatures that roll high on this trait. Roll it into the
same pass as the swim/flock/death work above, since it touches the same movement helpers.

### Checkpoint (paused at the 5-hour usage limit) - where to resume
Done this round: pirates sometimes stay out on deck; cave spiders hang from the real ceiling; foreground
vines/kelp/rock fade instead of popping; beasts re-read broken fragile tiles; predators carry kills (T_CARRY: owls
in talons, eels/cats/dogs/octopus/bats in jaws) - tested by --verify-pirate-ecosystem; per-species draw sizes
(BiomeDef::sizes, BeastSize) scale up the food chain; **the Weeds and Atlantis are built** (PL_WEEDS, PL_ATLANTIS:
BuildWeeds/BuildAtlantis in levelgen.cpp, tile art, backgrounds, 460/560 gold, both valid on the first draw) and
the Periscope is now a chart (list left, detail right, all 8 dives incl. the Abyss).
Still to do, in order:
1. Beast biomes for the Weeds (the user's web: phytoplankton/spores -> kelp seahorses -> mermen + tiger sharks;
   barracuda -> mermen; mermen <-> tiger sharks; barracuda/mermen/sharks -> electric rays; a ray's death or shock
   -> electrified corpse -> scavenger crabs + parasitic fungi. Sizes vs the diver: plankton, seahorses, crabs, fungi
   smaller (different from each other); electric ray about the diver's size; barracuda a little larger; tiger shark
   and merman 3-4x) and for Atlantis (its own web, distinct). Add WeedsBiome()/AtlantisBiome() in beasts_biomes.cpp,
   Biome() cases in beasts.cpp, VerifyBeastBiome cases, a --verify flag each, draw functions + dens in platformer.cpp.
2. Sizes for the other biomes may need a second look at play scale (shots: `--shots shots fauna`).
3. Remove the old "Still uncharted: the Island, the Weeds, and Atlantis." help line in hub.cpp.
4. Update CLAUDE.md (new levels, periscope chart, T_CARRY, sizes).
### Resumed: done
Weeds and Atlantis creatures built (WeedsBiome/AtlantisBiome, tests via --verify-weeds-ecosystem / --verify-atlantis-ecosystem, art, dens). Shots: fauna_weeds_lineup (size check), fauna_weeds_shark, fauna_atlantis_*. Next: the PDF's player-physics section (slide, impact roll, poles, hydro-glide, water dash, parachute brake).

## ParkourReference1.3 (the user's newest spec) - the plan
1.3 gives every biome (Hull, Pirate Ship, Island, Cave, Seaweed, Atlantis, Abyss; not the Pipes) four large beasts (passive giant,
clever hunter, unique predator, apex), two fodder beasts and five flora, plus universal rules: proximity herding, body-impact flora
triggers, persistent injuries (lower speed, limps, bleeding into the scent grid; the wounded become targets). Many flora are the
counterplay that lets a smart player survive the dangerous beasts. Its text is extracted to the session scratchpad (the PDF's glyphs
decode as Arial glyph ids: char = gid + 29).
User decisions: **add 1.3 to the existing webs, merging overlaps** (e.g. Hull moray -> Hull-Crusher eel, Hull sprat -> pilot-fish,
Weeds tiger shark -> Leviathan Tiger Shark, Weeds scavenger crab -> Bristle-Crab); **Pirate Ship, Island and Cave stay above water**
(adapt the 1.3 beasts to deck/shore/dry-tunnel versions); **the Hull first**.
Apex beasts: **not scripted boss fights** - they can turn up anywhere, and the level designs itself so a smart player can learn to
survive them. Answer: an AI for the level design, in two halves:
1. **Ecology planner** (generation time, in BeastsBuild): habitats for each apex (open stretches, plating runs, exhaust tubes), counter-
   flora placed with purpose (rust-algae before open water, hydroids near octopus tubes, pressure-anemones at the start of long runs,
   hull-kelp anchors spaced so no stretch an apex can reach is longer than a refuge interval), destructible tiles ('d') only where the
   proven route never depends on them.
2. **Runtime director** (a Left-4-Dead-style AI director): tension and pacing; it lets an apex roam in from out of view when the player
   has been calm and is entering a stretch with options; the beast's own AI does the rest (hunts what it senses, leaves when fed or
   bored).
Hull roster: Hull-Grazer Whale (passive giant: a moving solid surface to run on and hide under), Siphon Octopus (clever: anchors in
exhaust tubes, suction toward its lair, pilot-fish bait), Electric Hull-Crusher Eel (unique: slams the plating, a shockwave that
disrupts momentum and shatters rust-algae), Steel-Biter Megalodon (apex: shadow eclipses light, bites superstructure, its turbulence
tears you off the hull unless anchored); fodder Barnacle-Mites (dash through: slick, less drag) and Pilot-Fish (scatter when sprinted
through: a cue for current shifts); flora Rust-Algae, Bio-Electric Hydroids, Pressure-Anemone, Metal-Eater Moss, Hull-Kelp.
## >>> RESUME HERE (written before a context compaction, 2026-09-29) <<<
Standing orders from the user: work autonomously, commit and push often, ask questions as you go (AskUserQuestion) on real design
decisions; if 5-hour usage reaches 93% pause (checkpoint + say so). Check usage with mcp__ccd_session_mgmt__get_usage.
The PDFs' text is in docs/ParkourReference1.2.txt and docs/ParkourReference1.3.txt (decoded; '?' marks symbols that didn't decode).
Done and pushed up to 2e916eb: the beast engine in all 7 platform levels (incl. the Weeds and Atlantis, built this session), carry-off,
per-species sizes, the Periscope chart, and the diver's extra moves (--verify-moves). All verifiers pass.
Progress 2026-09-29 (after compaction): item 1 DONE (5a8569a + follow-up): the whole Hull 1.3 roster, flora, movers, slime,
the runtime director for the megalodon and the ecology planner with its cover check (see CLAUDE.md "ParkourReference1.3: the Hull
first"). Item 2 DONE for the engine (injuries, herding, stun; lame-leg drawing). Item 3 partly DONE: planner + director + verifier
for cover; Design call: the megalodon's bites tear visible scars and shards out of the plating it lunges into (BeastWorld::scars)
but never change tiles, so every proven route stays proven (real 'd' tiles would need a second path-search validation per
level). STILL TO DO there: siphon lairs at 'v' ballast vents when a Hull has no suitable wall.
To do, in order:
1. (DONE) **Hull 1.3 roster** (plan above): merge HS_EEL -> Electric Hull-Crusher Eel (bigger, a slam shockwave along the deck that knocks
   the diver's momentum and shatters rust-algae; keep its den ambush), HS_SPRAT -> Pilot-Fish (ride the hull's boundary layer, band 1;
   scatter when sprinted through); add Hull-Grazer Whale (a moving solid surface: add "movers" the diver collides with and rides - the
   path search never sees them), Siphon Octopus (in 'T' tubes / 'v' vents: suction toward its lair, pilot-fish corpses as bait,
   stunned by hydroids), Steel-Biter Megalodon (apex, brought in by the director; shadow over the screen; strike turbulence throws the
   diver off the hull unless anchored - anchored = holding hull-kelp, on a pole, or under a low ceiling; bites 'd' destructible tiles),
   Barnacle-Mites (sessile carpets: dashing through crushes them and gives a short slick boost). Flora as sessile species with a
   T_FLORA trait (not prey, not threats): Rust-Algae (grazed at speed or by the eel's shockwave -> a rust cloud: blinds, and hurts
   predators passing through), Bio-Electric Hydroids (struck -> a stun shockwave: needs a Beast::stunT status), Pressure-Anemone (vault
   off it at speed: a bounce forward, and a jet pushing trailing predators back), Metal-Eater Moss (predators crossing it are hurt -
   lure them along your line), Hull-Kelp (a grab-and-swing that turns horizontal speed around a 90-degree corner; also an anchor).
2. **Universal 1.3 rules**: persistent injuries (health < 1 lowers top speed and acceleration, bleeds into the scent grid, limps in the
   gait/IK drawing, marks the wounded as easier prey), proximity herding (the diver sprinting or dashing near small fauna frightens them
   along his velocity), body-impact flora triggers.
3. **The level-design AI**: (a) the ecology planner in BeastsBuild (habitats, purposeful counter-flora, anchor spacing checked stretch
   by stretch, 'd' destructible tiles only off the proven route); (b) the runtime director (tension/pacing; lets an apex roam in from
   out of view; not scripted). Needs a verifier: every apex habitat stretch has refuges within reach; 'd' removal never breaks --verify.
4. Then propagate 1.3 biome by biome: Pirate Ship, Island, Cave (all kept above water, beasts adapted), Seaweed (merge into the user's
   Weeds web: tiger shark -> Leviathan Tiger Shark apex tracking blood across the forest; scavenger crab -> Bristle-Crab; add Goliath
   Manatee, Mimic Octopus-Stalker, Harpoon Mantis, Silver-Fin Sardines and the five flora - Blood-Kelp, Luminescent Anemone, Air-Weed,
   Tangle-Vine, Spore-Pod), Atlantis, and the Abyss (a separate mode, abyss.cpp).
5. Movement pass 2: set-pieces that REQUIRE the new moves (low tunnels to slide through, pole chains, ledges), with the path search
   taught each move (the user chose "optional first", then this).
6. Older queue (still wanted): art polish at play scale, FABRIK for more limbs, slimy walls (1.2), PDF shader/auto-tiling phase.
Known small leftovers: an empty stray file `beasts.h` in the repo root (untracked; deleting it was blocked - the user can remove it).
### Progress 2026-09-29, later: the Pirate Ship's Grand Kraken
User decision (AskUserQuestion): the Grand Kraken makes **real snaps anywhere** - so the generator proposes snap points and the
validator proves each (see CLAUDE.md "the Pirate Ship's Grand Kraken"). Done: GenSnap, the proof, the layout mask, PlatSnapShip,
the director, slams and snaps, drawing, VerifyPirate13, shots. NEXT for the Pirate Ship (in beasts_pirate.cpp; slots exist):
Timber-Shell Tortoise (passive giant walker: crushes the below-deck 'i' interiors it lumbers through, opening corridors - adding
routes, never removing the deck), Rigging-Mimic Cuttlefish (hangs as a fraying rope from spars/yards; grabs a diver who leaps from
it mid-air unless they paused on it first - it twitches when touched; the lantern weed reveals it), Cannoneer Mantis Shrimp (in 'N'
gun ports: fires a bullet-fast cavitation strike along a line at anything moving fast across it, shattering crates), wood-borer
worms (on 'f' planking: dashing across makes it collapse behind you, dropping pursuers), copper-scale moths (the minnows, adapted to
air: nest in open chests, herded they flash a curtain that distracts the cuttlefish), flora: cannon-moss (silent landings),
ship-rot fungus (spore cloud + rot scent for scavengers), mast-kelp (dash through: a beam swings into pursuers), barnacle-cluster
(fatal if dashed into, shreds big pursuers), Siren's lantern weed (light that reveals mimics). Then the planner for the fleet.
**User correction (2026-09-29): the Cave is an UNDERWATER cave.** Only the Pirate Ship and the Island stay above water. The Cave's
1.3 pass (next after the Pirate Ship) must: add PL_CAVE to WaterLevel() (water dash/glide), water visuals (tint, bubbles,
caustics, drifting silt), CaveBiome water=true, and an aquatic roster from the 1.3 Cave section (bats/moths/beetles replaced).
Progress: the whole Pirate Ship 1.3 roster is DONE (see CLAUDE.md). Next biome: the Cave, now an underwater cave (the user's correction), then the Island (above water), the Seaweed/Weeds, Atlantis and the Abyss.
Progress: the Cave is now a flooded cave with its whole 1.3 roster (see CLAUDE.md). Remaining biomes for 1.3: the Island (above water), the Seaweed/Weeds (merge into the user's web), Atlantis, the Abyss (abyss.cpp). Then movement pass 2.
