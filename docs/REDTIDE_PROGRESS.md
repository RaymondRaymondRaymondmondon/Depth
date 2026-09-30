# Red Tide: build log

Red Tide is the Deep Arcade's four-diver survival shooter. The design is `Red_Tide_Reference/Red Tide — Arcade Game 5
Design Document.pdf`; every number lives in the reference workbooks (`Red_Tide_Reference/*.xlsx`), exported to
`data/redtide/` by `tools/export_redtide.py` (re-run it after editing a workbook). The only hand-written data is
`data/redtide/maps/<map>/extra.json`, for things the workbooks state only in prose (current direction, alarm
regions, the Wreckers' units from the doc), each entry citing its source.

Build order (the design doc's own):

| Stage | Work | Gate | Status |
|---|---|---|---|
| 1 | Ecosystem engine, headless: species records, scent grid, sound layer, state machine, population | `--eco-sim` shows the sprat chain end to end | **Done** (see below) |
| 2 | 3D core: inked low-poly renderer, CreatureBuilder with the body plans, first-person diver, the Cormorant | A diver swims a box room among generated fish | **Done** (see below) |
| 3 | The Sunken Ship: layout, 40 beasts, 12 flora, racks, tonics, the Locker, the Forge, drops, tides, the Wreckers, the Goliath | Sunken Ship Definition of Done | **Playable solo; rules complete** (see below). The DoD's four-player LAN match waits on stage 4; the balance targets are open |
| 4 | Multiplayer through the shared arcade layer (prediction, lag compensation, snapshots, downs and revives) | Four players finish a Sunken Ship match on LAN | Needs the shared arcade networking layer (Master Reference stage 11) |
| 5 | The Underwater Cave (slipstreams, dry chambers, rockfall, the Drowned, the Lobster, the Resonator) | Its Definition of Done | **Playable solo; mechanics complete** (see below); the four-player parts wait on stage 4 |
| 6-8 | The Coral Reef, Atlantis, Approaching the Void | Each map's Definition of Done | |
| 9 | Charms, skins, dossiers, hidden quests, sound pass | `--audio-test` and the dossier | |
| 10 | Internet play; balance pass with `--redtide-sim` on every map | All balance targets met or logged | |

## Stage 1: the ecosystem engine (headless)

Code: `src/redtide.h` (data model and Ecosystem API), `src/redtide_data.cpp` (loaders), `src/redtide_eco.cpp` (the
simulation), `src/redtide_tools.cpp` (the tools), `src/json.h/.cpp` (a small JSON reader).

- **Data-driven:** species, attacks, the diet matrix, flora, spawn pools, the tide curve, the blockout (zones,
  links, points of interest), the boss sheet, the faction sheets, the 78 engine constants, movement, drops, body
  plans and palettes all load from `data/redtide/`. Every map's geometry schema is read (the Ship's rectangles,
  Atlantis's radial districts, the Void's plan coordinates).
- **The seven layers of the web:** trophic (hunger by size, the diet matrix, hunting with an escape chance by size,
  feeding, fed-and-calm), blood and scent (a 3D grid at 2 m, diffusion, current advection routed toward each room's
  drain passage, exchange through the passages' mouths, decay by tide era; beasts smell only their own zone and
  the zones one passage away, so blood has to travel), sound (a fast-decaying field; fearful flee, curious approach,
  aggressive attack), territory (defend radius with a warning display, intruders driven off rather than fought to
  the death), symbiosis (cleaners calm hosts; killing every cleaner enrages hosts map-wide for 300 s; parasites
  attach and bleed their host), flora (patches that grazers eat and that regrow; grazer capacity follows flora), and
  the enemy layer (the faction is a species; the regional alarm spawns squads by the tide curve's chance).
- **Tools:** `depth.exe --eco-sim <map> <minutes> [pattern]` (patterns none, sprat, quiet, loud, camping, spread),
  `depth.exe --web-check <map|all>`, `depth.exe --eco-test <map>` (the design doc's ecosystem test plan).

### Results (`--eco-test ship`)

| Check (design doc, Ecosystem test plan) | Result |
|---|---|
| 15 sprats killed in the salon call the barracuda within 30 s | 1.0 s |
| ... and the bull shark within 90 s | 14 s (from the Stern; it reaches the plume at 22.7 s) |
| Blood made in the salon reads at the stern within 40 s | 17 s |
| A size-3 corpse cues a threshold-30 predator at 20 m within 60 s; a threshold-120 apex only on four corpses | Pass |
| The loud pattern at tide 8 brings an alarm squad within 90 s; the spread pattern brings none | 2 squads / 0 squads |
| Killing every cleaner raises host aggression within 5 s; it decays after 300 s | Pass |
| Population stable for 10 minutes with no divers (no species to 0 or above 3x) | **7 of 10 seeds** (see below) |

**Open: population stability.** On three seeds in ten, one singleton mid-predator (the one barracuda, a grouper, the
octopus) is eaten by a shark or the Goliath and its 240-600 s respawn doesn't bring it back inside the ten minutes.
The Goliath eating whatever enters the engine room is the design ("everything that flees runs to the engine room's
shadows and gets eaten there"). Left for the balance pass (stage 10) rather than tuned blind now.

**Test definition notes.** The Ship has two alarm regions (Inside, Outside), so the "spread" pattern puts two divers
in each, in different rooms, firing Gannets every 4 s; "loud" puts all four in one room with powder guns every 2 s.
The sprat check starts the barracuda and the bull shark at home and calm, so it measures their response to this
blood rather than whatever the reef was already doing.

### Data findings (from `--web-check all`)

- **The Sunken Ship:** clean after one reconciliation. The design doc's beast table says the Sergeant Major is
  eaten by the grouper and the moray; the workbook's Diet matrix had neither, so `extra.json` patches both rows
  (renormalised to 1) and cites the doc.
- **The other four maps** have gaps to resolve when each is built (stages 5-8): species whose home zones aren't
  blockout zones (Atlantis's "Cisterns" and "Courtyards", the Void's "Overlooks" and "The Station: Labs"), diet
  entries that aren't species or flora ("Bones", "Tube worm garden", "Lab algae fish"), and a few species with no
  predator listed. They need an `extra.json` like the Ship's (zone aliases, sub-zones) or a workbook fix.

## Stage 2: the 3D core

Code: `src/redtide_render.h/.cpp` (mesh building, the CreatureBuilder, the inked renderer), `src/redtide_game.cpp`
(the scene: the diver, the Cormorant, the test tank, the HUD). Entry: the Deep Arcade's new **Red Tide** reel ->
"Dive (solo)"; Esc opens the game menu with "Leave the match".

- **Inked low-poly renderer:** two geometry passes (colour, then view-space normal and linear depth), composited by
  an ink shader: Sobel edges on depth and normals with line weight by distance, Bayer stipple in the deepest
  shadow, paper grain, a vignette, and the red at the mask's edge that grows with the local scent. Flat shading comes
  from screen-space derivatives, so bodies animated in the vertex shader stay faceted. The key light is the helmet
  lamp (a spotlight), with the sea's cool fill from above, a rim, fog by distance and caustics near the surface.
  Blood plumes, bubbles and marine snow are drawn after the ink pass.
- **CreatureBuilder:** every body plan in the art workbook (fusiform, compressiform, anguilliform, depressiform,
  shark, turtle, crocodilian, snake, crab, shrimp, cephalopod, jelly, amphibian, pinniped, cetacean, insect,
  echinoderm, burrower, bird, colony, leviathan) built from the per-species rows (length, girth, head ratio, tail,
  fins, eyes, palette tones). Swimming runs in the vertex shader (a traveling sine along the spine, vertical for
  cetaceans, wing waves for rays, full-body undulation for eels, bell pulses for jellies, leg cycles for walkers,
  trailing arms for cephalopods). Tone names are mapped to RGB in `data/redtide/art/tones.json`.
- **The diver and the Cormorant:** the Movement sheet's numbers (2.0 swim, 3.4 sprint for 6 s, 8 s to refill, 1.4
  vertical, 0.4 s to full speed, 0.6x while aiming) and the Racks table's Cormorant (30 damage, 300 rpm, 8/32 darts,
  noise 2, 1.4 s reload, thumbed in dart by dart). Darts fly down the crosshair's ray, are swept against each body's
  capsule every frame, lose damage past 15 m (half at 25 m), double on weak points, feed the alarm with their noise,
  and pay scrip (10 a hit, the bounty at the tide's multiplier on a kill, +50% on a weak point).
- **The test tank:** a 30 x 12 x 30 m room with a population drawn from the Sunken Ship's species, run by the real
  Ecosystem (schools, hunts, blood, scavengers).
- **Checks:** `depth.exe --redtide-test` (swim speed, darts hit, a kill, scrip, blood, the dart-by-dart reload: all
  pass); `--shots shots redtide` renders the tank, its silhouettes (the master reference's `--silhouette` check:
  every creature identifiable as a black shape), and both pages of the Ship's species lineup.

### Found and fixed along the way
- Darts tunnelled through small fish (a dart moves about 0.5 m a frame; a sprat is 3 cm thick): hits are now swept.
- Darts left the muzzle 15 cm below the eye and flew parallel to the view, passing under whatever the crosshair was on.
- The art workbook gives the Green Moray the ray plan with a ray's girth, though its own BodyPlans sheet lists the moray
  as an anguilliform example: corrected in `data/redtide/art/art_patch.json`, citing that sheet.

## Stage 3: the Sunken Ship

Code: `src/redtide_match.h/.cpp` (the rules: level, weapons, tides, stations, beasts and the Wreckers against divers,
the Goliath, drops, bots, `--redtide-sim`, `--redtide-match-test`), `src/redtide_game.cpp` (now a Match: the ship's
geometry, props, flora, the HUD), `data/redtide/engine/weapons.json` (transcribed from the design doc's weapon,
Locker, Forge and tonic tables: no workbook holds them). Entry: the arcade's Red Tide reel -> "Dive (solo)".

- **The level** is built from the blockout: each zone a room at its deck's height (extra.json), each link a passage
  between the two mouths; doors carry the link's cost and block divers *and* beasts until bought (the Ecosystem's
  `linkClosed`), so "safe from sharks until the breach opens" holds. The one-way links (the slide down the hull, the
  dumbwaiter) run with a current no diver can swim up. Stations come from the points of interest: 6 racks, 5 tonic
  machines (Juggernaut and Quick Brine live, the rest on power), 2 Locker spots, the Forge, the power switch, the
  cargo crane trap, the captain's safe, the cleaning station.
- **Tides** follow the TideCurve sheet: quota x 0.4 / 0.6 / 0.8 / 1.0 by players, a 20 s calm, the tide bonus split
  among the living, bounties x the tide's multiplier; tides 5, 10, 15... are Hunts (the Wreckers "10 plus a Foreman",
  scaled; from tide 10 a coin flip may make it a Predator Hunt), rewarded with a Resupply and a Locker weapon each.
  Tides 1-3 are "ecosystem calm": nothing hunts a diver unprovoked, and a provoked apex gives one warning strike.
- **Divers**: the spawn-in kit, 100 HP regenerating after 4 s at 20/s, bleed and poison, holds (a hold with
  "teammates have N s" lands its damage unless the holder takes 15% of its HP, per the Movement sheet, or the diver
  struggles free with twice the 6 taps), downs (45 s bleeding into the water, the Cormorant only), revives (4 s, 2 s
  with Quick Brine, +100 scrip, tonics lost), solo self-revives per Quick Brine (500 solo, three a match), the dead
  back at the next tide.
- **Weapons**: all 22 from the doc's tables with their handling class (spread hip/aim, swim penalty, headshot
  multiplier, falloff, dart speed); pellets, bursts, spin-up, thumb-loaded reloads, chum, nets, explosives, the Galvanic
  Rod's chain arc, limpets (thrown, sticky, 3 s fuse), the knife and melee weapons. The Forge (5000 after power:
  x2.5 damage, x1.5 ammo, a rolled alternate ammunition, re-roll 2500) and the forged twists that change numbers.
  Davy's Locker (950; the moray moves it after 8-12 pulls; the wonder weapon at 2% and guaranteed by 12 team pulls).
- **Beasts against divers**: a strike goes to the match (`Ecosystem::onDiverHit`), which picks one of the beast's
  attacks from the Attacks sheet and applies its effect text (bleed, poison, holds, knockback, stun, aim sway, the
  remora's slow, the octopus's theft). Contact attackers (stonefish, lionfish, rabbitfish, the jelly bloom) and
  hazard flora (fire coral, hydroids, anemones) hurt on touch.
- **The Wreckers** spawn from the alarm and the Hunts through the hull breach, hold their roles' distances
  (Cutters close in, the Speargunner covers from 12 m with a red laser tell, the Netman wraps, the Chummer chums the
  diver, the Foreman winds the Harpoon Cannon), bark from the faction sheet, patrol the doc's path, flee big beasts,
  and drop their guns.
- **The Goliath** (boss sheet): dozes until the power is on (then it wakes at 5 m or when a diver lingers; a shot
  always wakes it; fed, it lets you pass); phase 1 stays within 12 m of home, phase 2 roams the room, calls two
  Groupers and Tail Slams loose cargo onto marked squares, phase 3 enrages and breaks the aft bulkhead. Inhale pulls a
  diver from 4 m: 150 to the gills in 5 s spits them out, else they're swallowed and downed. Armour halves frontal
  damage; the gills count only in the windows after Inhale and Tail Slam; cycling the power stuns it once. The kill
  pays 1500 x tide each, a Locker weapon each and a safe key (the Foreman and the octopus hold the other two).
- **Drops** by the Drops sheet (2.5%, 4% after 3 dry minutes, the last two excluded, minimum tides, Fire Sale spacing):
  Resupply, Blood Frenzy (every hit kills, blood x5), Double Scrip, Purge (40 m, no blood), Fire Sale (10 scrip, at
  every Locker spot), Harpoon Hour.
- **The scene**: the ship as inked rooms with door openings, debris over unbought doors, the salon's pillars and air
  pocket, the cabins' partitions, the engine room's boiler, the mast and the breach; station props, flora, drops, falling
  crates; the Wreckers' laser tells, the Goliath's inhale current; the HUD (tide bell and quota, scrip, pressure gauge,
  tonic bottles, weapon slots, prompts with prices, captions, damage direction, the boss bar, down/held states, the
  results panel). `--shots shots redtide_ship` renders six views.

### Checks

`depth.exe --redtide-match-test` (48 checks, all pass: the original 36 plus the portholes, the confinement, the breach, the Supper Call): data, stations, doors blocking and opening, racks and ammo,
the quota and calm, a Bull Shark strike through the Attacks sheet, the Death Roll's hold (broken by damage, landing
if not), a solo down ending the match, the Quick Brine self-revive, power, the Forge, the Locker's moray, Resupply,
Purge, a tide-5 Hunt with its Foreman shooting the diver, the Goliath's Inhale and the gill escape, bots playing.
`--redtide-test` (stage 2's checks, now on the Match) and `--eco-test ship` still pass.

### Found and fixed along the way
- Divers joined the Ecosystem with species index -1 (no record); every lookup of a diver's species read out of
  bounds. They now have a Diver record (size 3), which is also what lets parasites attach to them.
- The first bots shot through a school into a shark, knifed the shark's flank instead of the fish, walked down the
  one-way slide with no way back, ran dry of darts, and woke the Goliath through the hull: each fixed in the rules
  (melee takes the body nearest the crosshair; the boss needs line of sight) or in the bot.
- The art workbook's lengths are far larger than the rooms (the Goliath 23.4 m against a 14 m engine room, the
  crocodile 11 m, the tiger shark 9.9 m), so the Goliath's hit capsule reached through the hull into the Keel. Bodies
  are now capped at half their home room's smaller floor side, for hits and drawing alike, and darts can't hit through
  walls. The workbook's lengths may want a look (the doc calls the Goliath "the size of a boiler").
- The Goliath's workbook row gives it a 12 m defend radius, which made it rise at every diver near its home; a
  dozing boss now ignores its territory and wakes only by the boss rules above.

### Open: balance (`--redtide-sim`)
The bots are a first pass and the numbers are far from the targets; logged for the stage 10 balance pass rather
than tuned against a weak bot now.

| `--redtide-sim ship`, 3 runs each | Tide reached | Target |
|---|---|---|
| four careful bots | 5.0 (downs 68% beasts / 25% enemies / 6% hazards; 0.73 alarm squads a tide) | 25 +/- 3 (60/25/15; 0.5) |
| four careless bots | 5.0 | 12 +/- 3 |
| one careful bot | 4.0 | (none given for solo) |

Teams now reach the first Hunt at tide 5 and die there, mostly to the Goliath: the Wreckers come in at the breach
beyond its engine room, and the bots have no boss tactics. The downs' split for careful play is already close to
the doc's shape.

### The ship confined (the user's call, 2026-09-30)
"The player is confined to the ship entirely... claustrophobic/tight... shooting out of the windows and holes of the
yacht at fish." In `maps/ship/extra.json`: the Keel and the Foredeck are `outside_zones` (in the web, never swum by
divers); `link_rules` make the Bridge hatch a window (never open to beasts), the slide beast-only, and the hull breach
a beast-and-Wrecker hole that tears open at tide 4 or when the Wreckers first come through it; `windows` generates
portholes (12 on the Ship) wherever a room faces open water within 6 m, which darts pass and divers don't; the Kick
Brine machine moved into the Galley; `spawn_add` puts small fish in the Bridge and schools outside its portholes, so
the first 250 scrip toward the Salon door is earned inside. The Ecosystem starts with these openings shut to beasts.

### Supper Call (an easter egg for the Goliath)
The user asked for a short quest leading into the boss fight. Read the chief engineer's log in the Cabin Deck ("three
blasts on the whistle and she comes up for her supper"), find his brass whistle behind the Galley's tins, and pull the
boiler's whistle cord three times with the power off: the Goliath wakes hungry into its fight. Killing it then pays
double and pressure-forges every diver's weapon in hand. Steps are `poi_add` entries and the text is `quest` in
extra.json.

### Tides 1-3 (the user: "create this how you see necessary, keep the game balanced")
Kept: nothing hunts a diver unprovoked; a provoked apex strikes once at a third and leaves; territory still bites.

## Stage 5: the Underwater Cave

Data: `data/redtide/maps/cave/extra.json` (every entry cites the design doc). Code: the map mechanics below are general
(each later map switches them on in its own extra.json); `--redtide-map-test cave` (29 checks, all pass).

- **Geometry**: `zone_y` gives each hall its depth from the blockout (the Chimney a 34 m shaft, the Squeeze a 3 m crawl,
  the Cathedral 24 m high). `dressing` places what the zone notes describe: the Gallery's columns and roots,
  stalactites everywhere, the Cathedral's crystal clusters, the dry chambers' pools, the Chimney's ledges, the Sump's
  silt, the Dynamo's generator, the Mouth's light shaft. `palette` colours the rock and sets the fog.
- **Slipstreams A-F** (`slipstreams`): one-way links that are rides, not passages. A diver who swims into a mouth is
  carried to the far hall at 8 m/s (no firing inside; 2 s of drift on arrival; the screen streams past); beasts path
  through them; blood rides them (blood at the Gallery's mouth reads in the Chimney within seconds); the Drowned never
  use them (`faction_patch.slipstreams: false`). Mouths come from the blockout's "Slipstream X mouth" points.
- **The dry chambers** (`air_zones`): on foot, at the Movement sheet's 1.6 m/s, pinned to the floor. **The Squeeze**
  (`crawl_zones`, `no_fire_zones`): a slow crawl with no weapons drawn.
- **Rockfall** (`rockfall`): a noise-6+ shot in the Chimney or the Cathedral has a 5% chance to bring a stalactite down
  near its target (200 to whatever is under it, credited to the shooter). The Chimney's **trap** (`trap.kind:
  rockfall`, 1000) drops seven.
- **Flora tools** (flora of type Tool): a dart into Bloodvine smells of 40 blood and keeps smelling for 30 s; an ink cap
  bursts into a cloud that blinds what's in it (the Lobster 5 s); crystal coral rings (6 noise) and calls the Lobster
  to Rear. Contact flora hurt by their sheet (snottite 5/s, drip fungus slows 30% for 5 s, white anemones sting 8).
- **The Drowned** (`faction_patch`): bloodless (no scent from wounds or deaths), ignore beasts, never ride slipstreams;
  Deckhands close in a line, the Lantern Bearer's light draws curious and luminous beasts onto the nearest diver, the
  Rope Hand's grapple holds and hauls a diver, the Bell Ringer sets every beast in 30 m on the diver it points at, the
  Bosun leads Hunts from the Roost. Barks from the faction sheet. Only the Ghost Worm and the caiman eat them.
- **The Lobster** (boss sheet): wakes to a diver in the Cathedral, a shot, or rung coral; Crush (a 2 s hold), Sweep
  (knockback 6 m), Clack (stun in 10 m; a stalactite on a marked square in phase 2, three in phase 3), Rear (the
  underside exposed 3 s, forced by rung coral or 800 damage in 5 s); phase 2 on the walls and ceiling with four
  Troglobite Crabs from the crystal; phase 3 rides slipstream E backward into the Dynamo Sump and cuts the power. Its
  kill leaves the expedition's key, which opens the Lantern Cache's crate: a free Resonator (`boss_key`).
- **The Resonator** (the Cave's wonder weapon, `wonder`): a 12 m, 30-degree sonic cone that kills size 1-2 outright,
  knocks bigger beasts back, and brings stalactites down where it points in the rockfall halls.
- Attack effects the Cave added to the parser: "N s hold", pins, "slow N% for T s", "slowed N% until brushed".
- Data reconciled with the doc's own tables (`diet_patch`, `species_patch`): the sleeper shark eats the Current Runner,
  the caiman the epaulette shark, crayfish the sea spider; the swiftlet ("Nothing here") and the hagfish ("Nothing
  (slime)") need no predator; the Current Runner lives where slipstreams A and B meet.
- Ecosystem fix: a territorial blow never takes a non-prey intruder below 10% (the dry chambers' toad was beating the
  epaulette sharks to death in its small pool).
- The arcade's Red Tide reel has a map chooser (< >). `--shots shots redtide_cave` renders six views.

| `--redtide-sim cave`, 3 runs each | Tide reached | Downs (beasts / enemies / hazards) |
|---|---|---|
| four careful bots | 5.0 | 70 / 30 / 0 |
| four careless bots | 6.0 | 76 / 23 / 1 |

Open: `--eco-test cave` population stability fails on the caiman eating the lone snapping turtle and giant
salamander (both in its diet; their 500-600 s respawns fall outside the ten minutes). The same open item as the Ship.

## Stage 6: the Coral Reef

Data: `data/redtide/maps/reef/extra.json`. Code: `UpdateReef`, `UpdateBossMatriarch`, `BeatDrum`, `EnemyBlast` in
redtide_match.cpp; `--redtide-map-test reef` (29 checks, all pass).

- **Open water**: every zone is open to the sky (`open_zones`, the shallows' tops are the surface); zone depths from
  the blockout (the Lagoon 2-4 m down to the Wall at 80 m); a sand-and-turquoise `palette`; staghorn, table and brain
  coral, seagrass, mangrove roots and the Bommie's mound as `dressing`.
- **The tide mill** (`tide_flow_period`: 240 s): the set runs Lagoon -> Forest -> Flats -> Wall on the ebb and reverses on
  the flood (`Ecosystem::flowSign` flips advection, drains and link exchange), so blood drifts the other way after
  each turn. **The Surge Channel** (`zone_currents`): a 5 m/s current toward the Wall's mouth (the 4 s ride); its trap
  flips it.
- **Reacher coral** (`flora_rules`): grabs a diver who brushes it (20 HP/s; the diver can still shoot); freed by
  shooting the coral to 0 (400), a teammate holding E for ~1.5 s, or 16 taps alone. It feeds on small beasts that brush
  it (held, dead in 3 s). **Sea grape** heals 10; **Halimeda** shatters loudly (4 noise).
- **Narrow zones** (`narrow_zones`): the Staghorn Forest and the Brain Coral Maze keep out anything above size 4 (the
  big sharks path round). Crown-of-thorns and parrotfish graze the staghorn; below 70% the Forest's corridors open.
- **The dolphins** (`allies`): follow a diver within 25 m and guard within 10; one diver shot turns the pod hostile for
  the match. The Raiders shoot them too (`shoots_allies`), and that turns them on everyone.
- **The Raiders** (`faction_patch`): turtle riders who hunt beasts as well as divers; the Coconut Slinger lobs bombs
  (friendly fire hurts Raiders); the Shaman's drum rages the reef, a third beat calls sharks, a fifth wakes the
  Matriarch. The Shaman drops the drum: a diver can beat it (F, 5 beats: beasts within 40 m turn on the nearest Raider)
  or lay it on the Turtle Beach altar (`quest_altar`: 2500 to every diver).
- **The Matriarch** (bossKind 2): comes up the Wall with a pod of three (`species_add` "Pod Orca"); Ram (straight-line
  charge), Bite (3 s hold, carries), Tail Slap (knockback), Herd (the pod pushes divers off cover); surfaces every 90 s
  (the blowhole open 4 s); phase 2 at 60% or two pod dead; at 30% she rams the Bommie (the cleaners die, every host
  angry for 300 s) and breaches into the Lagoon.
- **The Anemone Gun** (`wonder`): darts root stinging polyps (30 s) that sting and hold what swims into them.
- `--shots shots redtide_reef` renders six views (Lagoon, Forest, Bommie, Wall, Blue Hole, the Matriarch).

| `--redtide-sim reef` | Tide reached | Downs (beasts / enemies / hazards) |
|---|---|---|
| four careful bots, 3 runs | 6.7 | 81 / 18 / 0 |

The Blacktip Reef Shark does most of the downing; balance stays with Stage 10.

## Stage 7: Atlantis, the Forgotten City

Data: `data/redtide/maps/atlantis/extra.json`. Code: `UpdateAtlantis`, `UpdateBossWyrm`, `QuestAdvance`,
`WyrmGrates` in redtide_match.cpp, the city generator in `BuildLevel`; `--redtide-map-test atlantis` (40 checks, all
pass). `--web-check atlantis` is clean.

- **The city on its hill** (`plan_override`, `poi_scale`, `zone_y`): the engine's rooms are boxes, so the radial
  blockout (districts by radius and angle, 500 m across) is laid out as a box city at 0.4 scale in the same order: the
  Harbor Gate at the foot, the Farms and the Lower Town above it, the Bathhouses east and the Barracks west on the
  slopes, the Library above them, the Forum and the Grand Chapel on the summit; floors climb from 90 m deep to 30 m.
  The rampart walk is the west wall; the open sea beyond it (`zone_add` "Beyond the Wall") is shot into over the
  crenellations (`windows`).
- **The city generator** (`dressing.buildings`): 60+ marble houses on each district's street grid (4 m streets, the
  avenue up the hill kept clear, none overlapping, none on a doorway or station), farm terraces, forum sea fans,
  amphorae in the streets, columns in the nave, the god-pool.
- **Cisterns and grates** (`zone_add` "The Cisterns", `link_add` with `at`): a network under every district, opening
  through twelve grates and the god-pool (beasts only; divers can't go down). Congers, lampreys and the Wyrm live
  there.
- **The aqueduct** (`link_add` flows A1-A4, zone `flow`): blood made on the plaza reaches the market in ~73 s and the
  outfall at the gate in ~81 s (the doc: 75 and 120 s).
- **Doors** (`link_patch`): the parade stair opens with the garrison gate (`Link::opensWith`); the wall fort's postern
  costs 1,500 (our call, so the rampart isn't a free way round three doors).
- **The Lost Ones** (`faction_patch`, roles `phalanx`, `slinger`, `chant`, `wall`, `priest`): Legionnaires lock
  shields three abreast (400 shield HP soaks frontal darts; a blast or the Tide Staff tears it away; from behind the
  dart lands); Slingers pick out reloading divers; Cultists' chant slows divers 30% within 10 m; in a Hunt the Armored
  Lost One can't be shot through from the front, and the Priest stands in the plaza: 60 s of chanting calls the Alien
  Horror (`species_add`, 2,500 HP, throws a diver 20 m every 8 s, gone after 90 s; kill the Priest first and it never
  comes). They're bloodless; their **ichor** (60 s) keeps sharks and the Wyrm out of 20 m and draws the scavengers.
- **The Cistern Wyrm** (bossKind 3): below, nothing reaches it; it follows under the streets and comes up through a
  grate near the diver it wants (phase 1: only the god-pool and the chapel's two grates, only for divers on the
  chapel floor; phase 2 at 60%: the diver's district and the one below, and congers pour from other grates; never
  beside fresh ichor). The grate rattles for 1 s, the Surface strike lands at 0.6 s, the gills are bare from 2 s; a
  Drag below gives the team 6 s to put 200 into the gills. The god-pool trap (1,000) strands it for 6 s if it's home.
  Phase 3 at 30%: the Flood (the district slows, blood doubles, the great white comes over the wall) and it hunts the
  streets. Its kill leaves the lighthouse key; it reforms in the god-pool after 6 minutes at half health.
- **Traps**: the canal sluice (drowns a phalanx in 14 m, stuns beasts), the god-pool, the bathhouse hypocaust (scalds
  everything in the baths).
- **Quests** (`quests`, generic chains of `carry`/`spark`/`hold`/`ichor`/`key` steps): the Tide Staff (the treasury
  crystal's spark in a jar, three chapel braziers, hold the plaza 45 s, a Lost One's ichor offered at the god-pool
  while the Wyrm is below); the Lighthouse (four wall-fires with the spark, then the lighthouse with the Wyrm's key:
  the whole city on the sonar, the vault's 5,000 split, the Sovereign's Staff for the lighter).
- **The Tide Staff** (`wonder`, `wave`): a 16 m, 40-degree wave that throws what it hits 8 m along it (into a trap,
  off a diver) and strips shields; forged, it drowns Lost Ones (+150) and carries blood.
- Fixed along the way: the Ecosystem's agent array could move under a held reference when something spawned mid-step
  (a crash first seen here); it now reserves room. `HasW` in tests needs lower-case words.
- `--shots shots redtide_atlantis` renders six views (the gate, the town, the forum, the chapel, the wall, the Wyrm).

| `--redtide-sim atlantis`, four careful bots, 3 runs | Tide reached | Downs (beasts / enemies / hazards) |
|---|---|---|
| before the white was held back to tide 4 | 4.7 | 66 / 33 / 0 |

The Farms' amberjack pack and the great white did most of the downing (a careful bot now leaves packs alone while its
gun is weak); the great white comes over the wall from tide 4, after the calm. Balance stays with Stage 10.
