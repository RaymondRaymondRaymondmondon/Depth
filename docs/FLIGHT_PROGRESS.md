# The Flight: build log

The design document is `Reference_For_Future_MP_Games/The Flight — Arcade Game 7 Design Document.pdf` (51 pages). Its
OCR is in `docs/flight_pdf_pages/pNNNN.txt`; the page images are gitignored.

The user's decisions (2026-10-02):
- Build the base game first (pages 1-34, the eight build stages), then the expansion (pages 35-51).
- Art is drawn in code first; a Blender pass comes later.
- The Flight is not in the upcoming online playtest (that is Red Tide and the Trawl).
- The Founder always respawns as a chick-leader.

My defaults where the doc was open (the user may change any of these):
- The kraken can be killed in every match.
- Feed moves between islands only along feed lines.
- Mates come from a finite pool of wild birds.

## Stage 1: flight and fishing (done, awaiting the user's ten-minute gate)

Gate (doc p33): "Fishing feels good alone for ten minutes."

**Core** (headless): `src/flight.h`, `src/flight.cpp`, namespace `fl`.
- **Founders:** the twelve from `data/flight/flight_founders.json`, with stats and look. `Founders()`, `FounderIndex`.
- **Island:** a generated tropical island (`Island::Generate`, 161x161 grid of 2 m):
  - a coast near 82 m and a hill to the north-west (about 34 m);
  - a lagoon bay to the south, 2-3 m deep;
  - the shelf falling away to about -12 m;
  - about 70 palms;
  - the Founder's nest in the crown of the palm nearest the hill.
- **Wind:** `Wind::At`. It shifts every 150-240 s over 20 s, with gusts. It changes ground speed by up to ±30% with it or against it (±15% for the Albatross into a headwind).
- **Flight** (`StepFounder`):
  - *Steering:* the bird turns toward the player's aim and banks into the turn.
  - *Flapping:* W holds cruise speed. Shift sprints, which costs 1 breath per second.
  - *Climbing:* costs 0.9 × sin(pitch) breath per second.
  - *Breath recovery:* a level flapping cruise recovers 0.15/s; a glide recovers 0.6/s (more on a thermal or a tailwind); perched, 1.2/s.
  - *Exhaustion:* half speed and no climbing (doc p23).
  - *Gliding:* sinks about 1.1 m/s.
  - *Thermals:* rise off the hill in the afternoon.
  - *Dives:* turn height into speed, up to 35 m/s (×1.4 for the Swift).
  - *Stalls:* drop the nose. S flares.
  - *Landing:* land on ground when slow; float on water when slow.
- **The strike:**
  - *Trigger:* a dive meeting open water (descending faster than 4 m/s at over 10 m/s, within 0.35 s of the surface).
  - *Slow motion:* the world runs at a quarter speed for 0.5 real seconds (1.0 s for the Taloned). The mouse or WASD steers the talons within 2.6 m.
  - *Hit chance:* `0.62 × talon × size factor × speed factor × clarity × closeness`, against the nearest catchable fish within 3 m. Reach depth grows with dive speed.
  - *A hit:* lifts the fish if its size is at most the Founder's carry. A heavier fish fights for 2 s, bleeding into the scent field, then escapes wounded.
  - *A miss:* puts the bird under for 0.6 s.
  - *Splash:* every dive makes a splash the web hears.
- **Carrying, the nest and hunger:**
  - A carried fish weighs the bird down (6% per size class) and drips scent over the water.
  - E at the nest drops the fish into the cache (20 slots; fish spoil after two game days).
  - F eats a carried fish, or the oldest fish in the cache; a meal is worth 0.25 of the hunger bar per size class.
  - Hunger drains over one game day (`DAY` = 240 s); at zero the Founder faints for 10 s.
- **Death and respawn:**
  - A reef shark or blacktip can take a bird that is low over the water. The bird is a Diver agent in Red Tide's web while under 10 m over the sea.
  - It comes back after 30 s as a chick-leader: half speed, half carry, and hunger draining twice as fast.
  - Three fish make it the Founder again. Three deaths cost 10%.
- **The day:** a 240 s cycle. Fish rise toward the surface at dawn and dusk, sink by day, and sink deeper at night (`Step`), so the rises are the best fishing.
- **The sea:** `rt::Ecosystem` running `data/flight/sea/tropical` (map key `flight_tropical`; `MapLoad` and the art sheet redirect `flight_` keys).
  - Seven zones: the Lagoon, the Reef Shallows, three shelves, the Drop-off and the Blue.
  - Fifteen species, reusing other maps' art rows, renamed and resized (`data/flight/art/species_flight_tropical.json`).

**Scene:** `src/flight_game.cpp`, `Scene::Flight`.
- *Rendering:* drawn with Red Tide's inked renderer in daylight.
  - The sun is the renderer's directional "moon" light; the filmic curve is off, because it blows the daylit sky out to white.
  - A thin ink line and haze to the horizon.
  - A sky dome that goes gold at dawn and dusk and dark at night.
- *The world:*
  - A sea grid of 120 × 24 m round the eye, with a slight swell, drawn as translucent water so the lagoon floor and the fish show through.
  - A seabed plane beyond the island's shelf, so the shelf has no edge.
  - The island as one coloured mesh, palms that sway, a twig nest with its cached fish, and the web's fish drawn with `rt::Creature`.
- *The bird:* built per founder from its look (body, head, beak, crest, a tail fan that spreads, two-panel wings with fingered primaries).
  - Flapping beats are heavier when tired. Gliding holds a slight dihedral. A dive tucks the wings, and the bird flares to land.
  - Perched, it folds its wings.
  - The carried fish hangs under the body.
- *The camera:* third person along the aim; it widens its field of view with speed, closes in on a perched bird, and centres on the aim during a strike.
- *The HUD:*
  - breath, hunger and health;
  - altitude band and speed;
  - a wind dial relative to the view;
  - the clock, and "the fish are rising";
  - the nest's mark and fish count;
  - "fish below";
  - the strike's ring, with fish in reach ringed green (liftable) or red (too heavy);
  - the death screen and the message log;
  - a help panel (toggled with H).
- *Arcade:* the Deep Arcade's sixth reel, `G_FLIGHT` "The Flight": pick a founder with `<` `>`, then "Fly (solo)". The game menu has "Leave the island".

**Checks:**
- `depth.exe --flight-test` (17 checks):
  - the data, the island and the web;
  - cruise, sprint, glide and dive speeds, and the wind;
  - a steered strike that catches;
  - a heavy fish that escapes bleeding;
  - the cache and eating;
  - fainting, a reef shark taking a floating bird, and the chick-leader;
  - ten minutes of an autopilot fishing with real dives (last run: 31 strikes, 15 caught).
- Shots: `flight_dawn`, `flight_strike`, `flight_nest`, `flight_high`, `arcade_flight`.

**Not done yet in stage 1:**
- Sound (the doc puts sound in stage 8).
- Spray and splash particles.
- The bird's rig as true two-bone chains with feather quads (it is matrix-posed panels for now).
- A controller.
- Other players' birds.

## Next: stage 2, the colony loop
Mates, eggs, chicks and fledging into roles; feeding the colony from the cache; the nest panel. See doc pp. 4-12 and 33.
