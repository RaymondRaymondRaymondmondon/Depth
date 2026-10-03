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

## Stage 2: the colony loop (done; gate met)

Gate (doc p33): "A bot colony grows to 40 and starves if it outfishes its lagoon."

**Code:** `src/flight_colony.cpp`, and the colony types in `flight.h` (`Colony`, `Bird`, `Nest`, `Cache`, `Site`, `Stock`, `Economy`, `RoleDef`). Every number is data:
- `data/flight/flight_economy.json`: hunger, feed, courtship, mates, clutches, eggs, chicks, twigs, regrowth. Keys marked `_open` are numbers the doc leaves open.
- `data/flight/flight_roles.json`: the fisher, feeder and builder roles (doc p11).

**The loop (doc pp. 3-7):**
- **Feed:** a fish's feed is its size class. A full hunger bar holds one day's eating: 2.5 for an adult, 1 for a chick ("a sardine feeds one chick for a day"), 3 for the Founder. Chicks empty twice as fast. At zero for half a day, a bird dies, so chicks starve first.
- **Courtship:** a nest's courtship bowl wants 3 fish of size 2+, and one more for each mate after. Only the Founder fills it, carrying fish there and pressing E (doc p4: the Founder picks the mate).
- **Mates:** a mate arrives within 0.3-0.9 of a day (×0.8 on a tropical island, ×0.5 for the Lyrebird), from a finite wild flock of 30. It sits on the nest, eats from the nest's larder, and lays 2-4 eggs (Lyrebird +1, Cuckoo -1) every 2 days while fed, 3 clutches in all, up to 4 eggs and chicks in a nest. With no feeders, the mate fetches food for itself and its chicks, leaving the eggs to chill.
- **Eggs:** they hatch after a day while warmed by the mate (+25% faster in a nest lined with 3 shells), and fail after 2 days unwarmed.
- **Chicks:** they eat from the larder and fledge after 2 days (a day later for the Swift; +25% faster while fully fed). Each fledges into the role furthest below the colony's fledging plan.
- **Roles** (kinematic flyers that read the real sea):
  - *Fishers* go to the best ground: the nearest reachable fish, weighted `exp(-d/100)`, with resting grounds avoided. They dive at a fish within 2.7 m of the surface. A hungry shark within 6 m can take them. They carry the catch to the nearest cache with room.
  - *Feeders* carry fish from the caches to the nests that need them most.
  - *Builders* raise nests on free sites, up to the number wanted. They raise caches on the shore as the fishers multiply, gathering twigs from palms and driftwood, and line nests with beach shells.
- **Meals:** a working bird eats at a cache: 2 s when the feeders cover it (one feeder per 8 workers), 20 s when it fetches its own.
- **Night:** the colony roosts (fervour, a later stage, will keep it working). Retraining takes a day.
- **The stores:** caches hold 20 fish, and fish spoil after 2 days. The first cache is at the foot of the home palm. There are 24 nest sites, the palm crowns nearest home.
- **The grounds:** they regrow logistically: `r × N × (1 - N/K) + 4% of K` a day, with r by size class (3.0, 1.6, 0.9, 0.5). Red Tide's flat respawn is switched off for the Flight's rows (`respawn_s` 0). A fished-out ground recovers only slowly.
- **The day's depths:** fish rise to about 1.4 m at dawn and dusk (all in reach), sit about 3.8 m down by day give or take 2 m (a few in reach), and go deep at night. Each fish keeps its own depth from a hash of its home point. Red Tide's steering returns a fish to its home and goal, so their depths move too. The 3 m lagoon is in reach all day, which is why it gets overfished.
- **The Founder's E** (`InteractHint` / `Interact`): fill a courtship bowl, lay food in a nest, store a fish in a cache, take a fish from a cache, pick up twigs (palms, driftwood, the stock pile), add twigs to a build, or lay out a nest on a free site.
- **The Founder's F:** eat a carried fish, or the oldest in a nearby cache.
- **Leaderless:** a minute after the Founder dies, or while it's a chick, the old orders run down: no new nests, and fledglings fish.

**The scene:**
- *In the world:* colony birds (the founder's species at 0.75 scale, posed from their motion, carrying fish, twigs or shells), nests (growing as twigs come in), shell linings, courtship bowls (a cup lit per fish), eggs (blue-grey as they chill), chicks (down, a gaping beak, growing, pinker when hungry), cache piles with their fish (yellowing as they go off), the twig stock, and driftwood and shells on the beaches.
- *Labels:* nests within 45 m are labelled with twigs, their bowl, "a mate is coming", or the mate, eggs and chicks.
- *The HUD:* the E hint, the colony's size and days of food, and a flashing alarm when the colony is running out.
- *The colony panel (Tab):* the mouse is freed while it's open.
  - birds, feed per day against mouths per day, and the stores (days of food);
  - twigs, shells and the wild mates left;
  - each role with a retrain button;
  - the fledging plan (−/+);
  - nests built, under way, free sites, and how many builders raise (−/+);
  - the fishers' ground (`<` `>`: the best, or a zone) with its stock;
  - deaths by cause.

**Checks:**
- `depth.exe --flight-colony-test` (13 checks):
  - the data and the start;
  - the courtship bowl (a sardine is too small; three mullet), the mate's arrival, a clutch, hatching, fledging into the plan's roles;
  - chilled eggs, and chicks starving before adults;
  - a fisher, a feeder and a builder at work;
  - logistic regrowth (fished to 4%: back to 37% in a day; fished to 50%: back to 93%).
- `depth.exe --flight-sim tropical <days> [careful|lagoon] [founder] [seed]`:
  - Runs a bot colony. The Founder is flown by the colony's logic: it eats, courts when the colony can afford another mate, builds alone until there are builders, and otherwise fishes.
  - A governor plays the panel the careful way: nests just ahead of the mates, the plan by the feed, idle builders and feeders retrained as fishers, and grounds rested under 45%.
  - It prints a row a day, each role's time by task, the catch by ground, the grounds' stock at the end, deaths, and a GATE line.
- **The gate, last run:**
  - *Careful:* 40 birds on day 16 (seed 1) and day 14 (seed 2), no deaths, the lagoon held near 45%, fishers about 5 fish a day each (the doc's 3-6).
  - *Lagoon-only:* 23 birds by day 7, then the lagoon is empty, 25 birds starve, and 2 are left.
- Shots: `flight_colony`, `flight_panel` (a colony grown for six days by the bot).

**A target not met (logged, a question for the user):** the doc's balance target is 40 birds by day 6 and 100 by day 10 (p34). Its own timings don't allow that: hatch 1 day, fledge 2, a clutch every 2 days, a mate a day after its bowl is full, and every courtship fish carried by the Founder alone. With those numbers a careful bot reaches 40 in 14-16 days.

**Tuning notes from this stage:**
- The open sea's stocks were doubled for this stage: the shelf and open-sea rows count ×2, leaving out sharks, barracuda, tuna and mahi.
- Fishers' reach is 2.7 m (the lagoon's floor is 3 m down).

**Day length (the user, 2026-10-03): keep the doc's timings in days, shorter days.** A game day is `day_seconds` 120 (was the doc's 240), so a 30-minute match runs to day 15. Colony birds work at `work_pace` 240/day_seconds (2x: they fly and fetch faster) and fish rise and sink at the same share of the shorter day, so every per-day number (3-6 fish a fisher a day, hatch 1, fledge 2...) holds. Last sim: careful reaches 40 birds on day 12 (24 minutes), no deaths; lagoon-only starves (24 of 24).

## Next: stage 3, islands, scouting and the map
All four starting island types, the generator, the arrangements; scouts, fog, the map, reports (doc p34).
