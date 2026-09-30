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
| 2 | 3D core: inked low-poly renderer, CreatureBuilder with the body plans, first-person diver, the Cormorant | A diver swims a box room among generated fish | |
| 3 | The Sunken Ship: layout, 40 beasts, 12 flora, racks, tonics, the Locker, the Forge, drops, tides, the Wreckers, the Goliath | Sunken Ship Definition of Done | |
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
