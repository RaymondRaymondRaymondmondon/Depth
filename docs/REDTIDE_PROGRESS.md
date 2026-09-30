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
| 5-8 | The Underwater Cave, the Coral Reef, Atlantis, Approaching the Void | Each map's Definition of Done | |
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

`depth.exe --redtide-match-test` (36 checks, all pass): data, stations, doors blocking and opening, racks and ammo,
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
| four careful bots | 3.0 | 25 +/- 3 |
| four careless bots | 2.7 | 12 +/- 3 |
| one careful bot | 2.7 | (none given for solo) |

Nearly every wipe is the Goliath: the only way back into the ship from the Keel runs through its engine room, and
the bots' way of crossing it (skirting it along the far wall while it dozes; it wakes to a touch before the power is
on) isn't good enough yet. Sharks, the crocodile and the Wreckers account for the rest; no run has reached a Hunt
with a living team, so the enemy share and the alarm-squad rate are still unmeasured.

The Sunken Ship's opening is outside: the start pocket and the foredeck have no spawns and the Salon door costs 750
against a 500 start, so the way to earn is out through the free hatch and down the one-way slide to the Keel, then
2000 for the breach back into the Stern and on through the Goliath's engine room (the blockout's training loop B,
read in the direction the one-way slide allows). That is the data as written; whether the doc means the Bridge to
hold a few beasts for the first 250 scrip is a question for the design.
