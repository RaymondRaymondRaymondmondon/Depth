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

## Stage 3: islands, scouting and the map (done; gate met)

Gate (doc p34): "A Scout report updates the map correctly from each altitude."

**Code:** `src/flight_map.cpp` (headless), plus the scene's island drawing and the chart in `flight_game.cpp`.

**Islands** (`Island::Generate(type, seed, centre)`, doc pp. 13-16):
- **Starting types:**
  - *The tropical island:* stage 1's, with 40 sites: 32 palm crowns and 8 on the beach.
  - *The sea stack:* a terraced column 120 m tall, with 20 sites on its ledges, driftwood and shells on the lowest ledge, and deep water all round.
  - *The fishing town:* a low island with a harbour, houses with roofs, a church tower, docks, boats and a woodpile. The props stand in the heightmap, so a bird lands on roofs and quays. 25 sites: roofs, the tower and its ledges, boats and docks.
  - *The atoll:* a sand ring round a lagoon, with a channel to the east, a few palms and 15 sites on the ring.
- **Neutral islands:** islets.
- **Dangerous islands:** their shapes only. Their dangers and prizes are stage 7.
  - the kraken cove: a crescent of cliffs round a deep cove;
  - skull island: a big jungle island round a skull-faced mountain, with the tallest trees;
  - the volcano: a cone with a crater lake;
  - the reef garden: a coral flat with one sand cay;
  - the wreck: a hulk with masts.
- **Outlines:** each island carries 72 points of coastline for the chart.

**Arrangements** (`LayoutMap`, doc p14): Archipelago, Safe Distance, Ring and Chain, for 2-6 starting islands.
- Slot 0 (yours) is at the origin; the rivals' types are random.
- The kraken cove sits in the middle and a ring of dangerous islands between the starts. Neutral fishing towns and islets lie between, and the wreck sits off-centre.
- `depth.exe --flight-fair [seed]` checks 100 maps (5 seeds × 4 arrangements × 2-6 players). Every start must be the same distance from its nearest dangerous island, town and neighbour (within 2%), and no two coasts may come within 40 m (10 m for the drifting wreck). All 100 pass.
- **Deviation:** archipelago neighbours sit 560-1120 m apart, not the doc's 300-500, because a ring of dangerous islands the size of skull island has to fit between them.

**The sea over the whole map:**
- `World::Init(founder, seed, MapOpts)` builds an `rt::MapData` in code: Red Tide's species and food web, each island's water zones from a template, a 500 m open-sea grid round everything, and links between every pair of zones that touch. `rt::RebuildMapGeometry` finishes it.
- **Dormant zones:** zones more than 300 m from every bird hold their fish as numbers (`Stock::pop`) and regrow logistically. A zone wakes with fish when a bird comes near and goes back to sleep 20 s after the last one leaves.
- **Slot reuse:** fish go into dead slots (`SpawnFishSlot`), because the web only ever appends.
- **Cost:** a 4-player archipelago has 96 zones and 369 links; about 650 fish are live, and a step costs about 4-6 ms.

**What the colony knows** (`Knowledge`, doc pp. 18-19):
- **The fog:** a 25 m grid of when each cell was last seen.
  - The Founder sees 45-300 m round it, more the higher it flies.
  - Colony birds see 45 m.
  - A scout sees by its height: 250 m high, 120 m mid, 60 m low.
- **Islands:** unknown, flown over (a silhouette), or landed on (charted in full).
- **Grounds:** yield and sharks.
- **Sightings:** nests, caches and birds, with their time, height, exactness and how many scouts saw them.
- **Reports:** a log.
- **Rivals:** the other starting islands each hold a rival colony that sits still until stage 4: 3-7 nests, 1-2 caches and some birds.

**Scouts** (a new role; `SendScout`, `ScoutStep`, `ScoutReport`):
- A scout flies to an island or a ground at high (120 m), mid (40 m) or low (12 m). It circles and looks for 6 s, then flies home.
- It reports within 60 m (measured flat) of a nest, a cache or the Founder.
- High counts nests and birds to within 30%. Mid and low count exactly (doc: mid is seen by Watchers and low is a target; both are stage 4).
- Two scouts reporting on the same island within half a day cross-check, and the counts become exact.
- A scout out on an order flies on through the night.

**The chart (M):**
- *Fog and age:* cloud where nobody has looked; sea where your birds have, washing to sepia as the sighting ages.
- *Islands:* landed islands in their colours with nest sites; silhouettes in grey, named only by type.
- *Overlays:*
  - worked grounds with their yield word and a shark fin;
  - sightings ("~4 nests, 2 caches, ~12 birds; seen high, 1.0 days ago, cross-checked");
  - wind arrows over seen water;
  - your birds by role, scouts with a line to their target, and the Founder.
- *Navigation:* wheel to zoom, right-drag to pan.
- *Orders:* send a scout to an island or a ground at the chosen height, or send the fishers to a ground ("the best ground" resets it).
- *Reports:* the log, newest first; click one to centre the chart on it.

**The arcade:** a plate left of the drum picks your island type, the arrangement and the number of starting islands. The Founder's playstyle shows there too.

**Checks:**
- `depth.exe --flight-scout-test`:
  - the map's makeup;
  - every island type generates with the doc's site counts;
  - the sea sleeps where there are no birds;
  - the fog at the start;
  - a scout at each height reports correctly (high about 6 of 7 nests, mid and low exactly 7, all in about 0.4 days);
  - higher scouts clear more fog;
  - two scouts cross-check to an exact count;
  - a ground report;
  - a whole map costs under 8 ms a step.
- `--flight-fair`; the stage 1 and stage 2 checks still pass.
- Shots: `flight_stack`, `flight_town`, `flight_atoll`, `flight_chart`, `arcade_flight`.

**Carried over:**
- *The colony on other home types:* the colony runs on any home island using that island's own sites and twig spots, but its numbers were tuned on the tropical island.
- *The careful sim:* it varies from seed to seed: 40 birds on days 10-19.
## Stage 4: war (done; gate met)

Gate (doc p34): "Two bot colonies fight and the higher, downwind flock wins."

**Code:**
- `src/flight_war.cpp`: the war, flocks, morale, raids, defences, the bots' governor and war, and `--flight-war`.
- Numbers: `data/flight/flight_war.json`.
- Colony and scene changes alongside.

**Warriors** (doc pp. 11-12): Skirmisher, Tank, Striker, Watcher, Screamer and Flockmaster, with the doc's health, speed and attack.
- Counters: ×1.6 against what a role is strong against, ×0.6 against what it's weak against.
- They are fledged by the plan or retrained (a day).
- At home they roost and heal over a day. Watchers perch by the nests, or on a tower.
- The Bomber waits for the Works (stage 7).

**Flocks** (doc p13):
- 2-12 warriors with a leader: the Founder, who takes the lead with G within 70 m (+20 morale, +10% speed), or a Flockmaster (+10, +5%).
- A formation (Chevron, Wall, Spiral, Scatter, Hammer, Cover), a height (low 12, mid 40, high 120 m), a stance (raid, hold, escort, retreat at half), and a target: home, a cache raid, a chick snatch, a ground to harass, an enemy flock, or a mark.
- They fly at the slowest member's pace, with the wind: ±30% along it.
- A flying army eats 1.6× (the campaign).

**The five rules** (doc pp. 22-23):
1. **Altitude:** the first strike diving from above does +50%. A bird zooms back up after a dive (its speed turns into height), and later re-dives do +25%. Strikes made upward do ×0.6, and climbing is slow (2.5 m/s). Strikers climb above their target before they dive.
2. **Wind:** downwind is faster, and flapping upwind costs 0.6 breath a second more.
3. **Stamina:** 25 s of flapping fight. Climbing and upwind cost breath; gliding and diving give it back. An exhausted bird flies at half speed and can't climb.
4. **Morale:** base 50, plus a leader, plus Screamers (up to +15), plus wins (+10 each, up to 2); fed +5 / hungry -15; enemy Screamers nearby -4 each; -40 × the share of the flock lost; a killed leader -30. At 30 a flock retreats home; at 0 it scatters.
5. **Blood:** a bird killed over water bleeds into Red Tide's scent field, and a fight over water for more than a minute keeps bleeding. Sharks come.

**Formations:**
- Chevron: +10% speed; hits from the side do +25%.
- Wall: its Tanks stand in front; Strikers diving onto it do +25%.
- Spiral: climbs 2× (3× on a thermal); +30% damage when hit from above.
- Scatter: 20% dodge; organised flocks do +20% against it.
- Hammer: Strikers fly 30 m above.
- Cover: Tanks take 60% of the blows aimed at the escorted.

**Raids** (doc p24):
- *Cache raids:* Skirmishers take fish home. A hedge keeps them out, and a Watcher nets one (held 3 s).
- *Chick snatches:* Strikers take chicks, but not while a Watcher guards the nest. Watchers are weak against Strikers, which kill them first.
- *Fisher harassment:* Skirmishers make enemy fishers drop their catch (it bleeds) and flee their ground.

**Defences:**
- Hedges (10 twigs) round the home cache.
- Towers (20 twigs, 5 shells): a Watcher on one gets ×1.5 sight and net range.
- Builders raise them after nests. The bots raise a hedge at 12 birds and a tower at 20.
- Idle warriors rise as a "Home guard" against an enemy flock over their island (every colony, yours too).

**Rival colonies are bots:**
- Each rival is a `Side` (its colony, island and Founder) swapped into the World to step, so all colonies run the same code. A bot Founder, a careful governor (`BotGovern`: nests, the fledging plan with warriors from 10 birds, retraining, defences) and `BotWar` (a raid every 20 s when it has 4+ idle warriors and food, waiting for a fair wind) drive it.
- Rival fishers in sleeping waters catch from the stock numbers.
- Grounds are chosen counting sleeping waters too. Fishers wait at home when nothing is worth the trip, and the bot Founder roosts at night.

**The scene:**
- *Birds:* rival birds in their livery colours, warriors shaped by role (Tanks wear shell armour), and the rival Founders.
- *Combat:* feather bursts on hits, birds tumbling into the sea with a splash and a blood stain, nets, and health bars over the fighting.
- *The Flocks page (Tab twice):*
  - idle warriors and "form a flock";
  - each flock's members, leader, morale bar, formation, height and stance, target, and buttons (pick, home, disband);
  - defences (raise a hedge or a tower).
- *The chart:* a Flock order mode (an island to raid, choosing caches or chicks; a ground to harass; an enemy flock to meet; your island to guard), with flocks drawn (an enemy's only where your birds see it now).
- *The HUD:* a raid alarm, and the led flock's morale.

**Checks:** `depth.exe --flight-war [scenario|all] [runs]`, over 20 runs each.

| Scenario | Last run |
|---|---|
| altitude | 20/20 |
| wind | 15-19/20 |
| stamina | 19-20/20 |
| morale | fed against hungry 20/20; a leader and a Screamer against an extra Skirmisher 10-13/20; killing the leader 65 → 25; retreat under 30 |
| blood | a hungry shark drawn from 40 m to within 4 m |
| formation | Hammer over Scatter, 16-17/20 |
| raids | cache, net, hedge, chick snatch, harassment |
| **gate** | **the higher, downwind flock wins 20/20** |
| bots | 14 days of two bot colonies, which raise warriors and raid |

`homes` and `rival` are balance and trace probes. Shots: `flight_battle`, `flight_flocks`, `flight_chartwar`.

**Balance notes (for stage 8):**
- *Home islands:* a sea stack colony grows to about 13 birds by day 8, where tropical, town and atoll colonies reach 24-27. The doc wants about 75%; this is about 54%, because the stack is short of twigs and has few fish within reach.
- *Changes made:* the stack's waters got 1.6× the fish, upwelling (shoals at 2.6 m by day) and more driftwood.
- *Morale:* morale is now decisive, and the Flockmaster is a liability if it dies early.
## Stage 5: multiplayer (done 2026-10-03)
Doc p32 ("Simulation and networking") and p27 (victory and score). The Flight on the Deep Arcade's shared session, host-authoritative like the Trawl and Red Tide. Code: `flight_net.h/.cpp`.

**How a networked match works:**
- *One world on the host.* The host runs the one real `World` (`FlightHost`, the GameHost for `G_FLIGHT`): the ecosystem, every colony and the war. Each person is a side; side 0 is the host. Lobby AI seats are bot colonies.
- *Per-side state.* Every side now has its own input, fog, knowledge, news log and diver body in the web (`Side::know`, `log`, `human`; `SwapSide` swaps them). `WithSide(s, fn)` runs code with any side swapped in; `SayTo(s, ...)` sends a side its news. A shark's strike finds whose Founder it hit.
- *Input and orders.* A person's Founder flies by `FA_INPUT` (12 bytes a frame). Everything else is an order (`ApplyOrder`): the colony panel, the flocks page, the chart, G. **Solo play goes through the same orders**, so the two can't drift apart.
- *The snapshot.* Each player gets their own 20 times a second (`WriteWorld`/`PackWorld`, deflated): their colony in the World's fields, their knowledge and news, every Founder and colony, another colony's birds only within 330 m of them, the sea's fish only within 160 m. One templated `Visit` writes and reads, so the two can never disagree.
- *The guest's mirror.* It is rebuilt from the seed and options (the islands, the sea and the stocks are already there), lifts its own fog locally, and flies its own Founder ahead of the host between snapshots (`PredictFounder`, eased toward the host's). Everything else dead-reckons between snapshots.
- *No slow motion.* A strike can't slow the world for one player, so in a networked match the strike runs in real time.

**Dropping out:**
- A dropped player's colony runs on its last orders. The table does not pause (`GameInfo::pauseOnLost` false for the Flight); the HUD shows the countdown.
- After the session's two minutes a cautious AI flies and runs the colony: it fishes, defends and never raids (`cautiousMask`).
- They get it back if they rejoin.

**Winning:**
- The match ends at the time limit (20, 30 or 45 minutes, chosen in the lobby) or when one colony holds two-thirds of the nests in use (from day 3, with at least 4 nests a player on the map).
- The score is the doc's p27 table so far: birds 2 (chicks 1), nests 5, home island 30, fish in caches 1 per 5, kills 1, the Founder never died 60. Stage 6 adds pearls, research tiers, fervour and stolen eggs. All of it is data in `data/flight/flight_scoring.json`.
- The HUD shows the time left and the standings; the results panel shows the end.

**The arcade:** the Flight reel hosts. The lobby picks the home island type, the arrangement and the length; each player's founder (from the reel) arrives in their hello. "Take wing" launches it.

**Checks:**
- `depth.exe --flight-net-test`: inputs, orders (including refused ones), the snapshot rewritten byte-identical, interest management, bandwidth, prediction (within 0.6 m), a drop and a rejoin, the end on every screen.
- `depth.exe --net-loop flight mem`, **the gate**: a host and five guests over the in-memory transport play a full 30-minute match. Guest 0 plays by hand (input and orders); the host and guests 1-4 use a test-only autopilot (`FA_AUTOPILOT`, accepted only by a host configured with `:test`); guest 4 leaves halfway.
- Over real sockets the loop plays a 3-minute match (`DEPTH_FLIGHT_MINUTES` overrides).

**The gate run:**
- Six colonies, 124 birds at the end, 81,133 snapshots (3.3 KB average, 6.4 KB at most), none refused, nobody else lost.
- Every remaining guest agreed on the winner and the scores.
- It took about 48 minutes of real time over the in-memory transport.

Shots: `flight_guest` (a guest's screen), `flight_results`.

**Playtest fixes (2026-10-03):**
- The Founder couldn't take off: it re-perched on its nest at once. It now can't settle on a site for its first second in the air.
- Founder hunger lasts `founder_hunger_s` (360 s), not one 2-minute day.

## Stage 6: research, faith, trade, the twelve founders (done 2026-10-03)
Doc pp. 6-10, 20-22, 25-27. Code: `flight_society.cpp`. Data: `flight_research.json`, `flight_bends.json`, `flight_towns.json`.

**Research (nine trees, four tiers each):**
- It runs at the **Roost** (a structure: 16 twigs; the bots and the Founder raise it first), one line at a time.
- Costs, from the doc: tier 1 is 2 pearls, 10 shells and half a day; up to tier 4 at 20 pearls, 80 shells, 3 days and a colony of 25.
- Bombing and Chemistry open on day 5 (the Alchemist's on day 4, at 1.5x speed). Their effects come in stage 7.
- Wired effects:
  - *Nesting:* shell lining (eggs hatch faster), big nests (6 eggs), cliff nests (+50% sites).
  - *Fishing:* schooling eye (fishers see 40% farther), deep dive (pearls), night fishing, cooperative fishing (x1.5 catches, triple splash).
  - *Flight:* thermal riding, long wings (stamina +50%), wind reading (headwinds halved), formation flight (the chevron +30%).
  - *Caches and craft:* smokehouse (6 days), big caches (50), shell armour (Tanks +60 HP), nets (hold 5 s).
  - *War:* formations (War 1), Skirmishers and Strikers (War 2), towers and hedges (War 3), Flockmasters (War 4).
  - *Faith:* the shrine and priests, offerings, conversion, Zeal.
  - *Trade:* Traders (Trade 1), barter (Trade 2), egg theft for everyone (Trade 3), brood parasitism (Trade 4).
- Roles and builds are gated (`RoleUnlocked`, `BuildUnlocked`): fledging falls back to a fisher, and retraining is refused.

**New roles:** the Trader, the Priest, the Chemist and the Pathfinder (stage 7 gives the last two their work), and the Bomber and the frigatebird Pirate (stage 7).

**Faith (fervour 0-100):**
- *Raised by:* priests at the shrine (7 a day each, up to a ceiling), offerings (Faith 2: a fish laid on the shrine, 10 for a big one), and the Founder's prayer (+15 for an hour, once a day).
- *Lowered by:* drift back toward 20, hunger (-25 a day while starving), a routed flock (-8), and the Founder's death (-10, or -30 at Zeal).
- *What it does (the doc's bands):* how much of the night the colony sleeps; morale +10 or +20; low fervour breaks flocks at 40.
- *At 100 with Faith 4 (Zeal):* no rest, no rout, priests convert enemy birds near the shrine, and the colony eats 20% more.
- *Conversion (Faith 3):* priests turn wild birds into fishers, at one per priest a day.

**Trade:**
- Every fishing town on the map has a dock market priced in feed.
  - Fish pays 0.8 in the morning and 1.3 at evening, less in a glut and double in a storm.
  - A pearl costs 4 feed, a shell 1 and a twig 0.5, dearer when the town is short.
  - Each colony keeps credit at each town and has a reputation there: trade raises it, and at -50 the town shoos you away.
- Traders carry the surplus beyond a day's food to the nearest dock and bring back the colony's choice (pearls by default). The Founder sells a carried fish in person (E at a dock).
- The old pelican sells one true thing about an island for a pearl.
- Barter (Trade 2) offers goods each way plus a truce. A truce stops raids and fights between the two; bots accept fair offers.

**The twelve founders** (`flight_bends.json`) bend their whole colony:
- Every boost and weakness in the doc is a number there.
- Every unique is in:
  - the Taloned's Talon Lock;
  - the Swift's Skim;
  - the Beaked's Tear (hits bleed);
  - the Lyrebird's Serenade (E at a nest);
  - the Shadow's Dusk Raid and fervour cap of 80;
  - the Sigma's better rates and Market Cornering;
  - the Ibis's fervour and conversion;
  - the Cuckoo's egg theft (Skirmishers carry eggs home, where they hatch yours) and Brood Parasite (E at an enemy nest: a daily exact report from that island);
  - the Tycoon's Boom (+60% for 2 days, then -40% for 1) and Golden Nest (a pearl a day);
  - the Alchemist's early trees and double guano;
  - the Albatross's Long Reach and halved wind.
- The Pilgrimage scores in stage 7, when shrines can stand on other islands.

**Design calls (logged; the user may change them):**
- Each colony starts with a founder's dowry of 2 pearls and 12 shells (data), so the first research comes by day 2 as the doc's pacing expects.
- The Founder brings up a pearl with 10% of its catches (the doc lists the Founder among the pearl gatherers).
- A third of the builders keep a stock of shells for the research.

**The scene:**
- The third Tab page holds research (the Roost, nine trees with tier dots, costs, the hover text), faith (fervour bar, band, shrine and priests), trade (the nearest town's prices and your standing, what the Traders buy, the pelican at a dock), the founder's own button (boom, corner) and barter (draft an offer; accept or refuse one).
- The colony page lists only the roles the colony can fledge. Formations cycle only through the unlocked ones.
- The HUD shows pearls, fervour and research, and the boom.

**Checks:**
- `depth.exe --flight-society-test`: the bends, research and gating, towns and trade, the pelican, priests and conversion, prayer, the Shadow's cap, the boom and the golden nest, the serenade, barter and the truce, the Cuckoo's egg theft, and a bot colony's society over eight days.
- `depth.exe --flight-sim founders [days] [seeds]`, **the gate**: every founder's window is shown. Twelve bot colonies (one per species) run on the same maps in 12 threads, about 6 minutes. Each prints its strength against the field at days 3, 6 and 10, its window, and the doc's window.

**Balance (stage 8):**
- The first run had only 3 of 12 windows where the doc puts them.
- The Sigma was far ahead (its corner paid 3x; fixed to 1x).
- The bots research slowly: shells and pearls are the bottleneck, as the doc intends, but too tight.
- The ±3% target is stage 8's.

Shot: `flight_society`.

## Stage 7: dangerous islands, sieges, bombing and chemistry (done 2026-10-03)
Doc pp. 15-18, 21, 24-25, 34. Code: `flight_danger.cpp`. Data: `flight_danger.json` (every number below is there).

**Holding islands.** `HolderOf(isle)` is the side with the most built nests on an island (a tie holds nothing). An **outpost** is a nest and a cache on another island: the Founder lands on a free site and presses E, or a Pathfinder (Trade 4) flies out when ordered on the chart's new Outpost mode. Outposts grow to three nests, and get a home guard when raided. The score counts islands held: 30 each, 80 for a dangerous one, and 150 for killing the kraken.

**The dangerous islands:**
- *Kraken Cove.* The kraken sleeps, wakes on blood and noise, and surfaces. Low fliers in the cove are snatched: 1% a second while it sleeps, 5% awake, 12% surfaced. It hoards 20 pearls. Offerings of fish calm it for a day; neglect for three wakes it. It has 4000 HP, and bombs do 300.
- *Skull Island.* A great ape throws rocks (170 m, 45 damage) at birds below 25 m. Feeding it a big fish puts it to sleep for a day. Lizards raid nests. The summit shrine gives its holder +20% fervour.
- *The volcano.* The first eruption comes about day 4.5, then every 6 days, each after half a day of tremors. Ash grounds birds within 420 m for a day. Its thermals lift fliers. It yields sulfur (2 a day) and crater fish.
- *The wreck.* It drifts and sinks over about 3.5 days. Its hold has 30 fish, there are rats, and a ghost crew comes at night. The bell, carried home, gives +10 morale.
- *The reef.* Eels, and cleaner fish that calm flocks.

**Weather:**
- A storm every 3-5 days: two hours of 2.5x wind, and every flock makes for home.
- Fog at 30% of dawns: two hours in which Watchers see half as far.

**Sieges:**
- *Blockade* (War 4): a hostile flock within 120 m holds a ground, and the owner's fishers stay off it.
- *Wall* (War 4): a hostile flock within 150 m of an island; the Traders and Scouts stay home.
- *Assault:* Strikers tear down a nest with no chick, egg or Watcher in it (3 a strike), and the island's holding goes with it.
- *Desertion:* a colony with under 0.15 days of food loses its warriors to fishing.
- *Bot sieges:* a bot sieges a rival who holds a dangerous island, with an assault, a blockade, and a stimulant for the assault.

**Bombing and chemistry (the Works: 20 twigs, 15 shells):**
- *Bombs* (Bombing 1): 5 guano and 1 sulfur, one a day, up to 4. Bombers (Bombing 2) carry one high and drop it from over 50 m: 60 damage in 8 m, structures damaged, and -15 morale. There are also incendiaries and a blockbuster every 2 days.
- *Stimulants* (Chemistry): Haste x1.3 speed; Fury x1.4 attack with more bleeding; Clot halves bleeding; the Draught gives x1.5 everything and immunity to morale, then a day asleep. Each lasts a day and is followed by a day's crash. Chemists brew one every half day from guano and pearls.

**The scene:**
- The kraken's arms (four awake, eight surfaced), its head when surfaced, and an arm out of the water where a bird was taken.
- The ape on the summit (lying down asleep) and its rock in flight.
- The volcano's plume, with an ash column, a glowing crater and lava pools in an eruption.
- The wreck's ghost lanterns.
- Bomb blasts: a flash, a ball of smoke, and fire for an incendiary.
- Models for the Roost, the shrine and the Works. Bombs, the bell, sulfur and stolen eggs are drawn in birds' talons.
- *Weather:* a storm darkens and closes the sky, roughens the sea and rains on the screen. Fog and ash thicken the haze.
- *HUD banners* for a storm, fog, ash, tremors, the kraken and the ape.
- *The Flocks page* has a dose button per flock, and the Works' stocks with a brew picker. The chart has an Outpost mode and a holder's flag by every island.

**Fixes found on the way:**
- The Flockmaster now leads from the rear, screened by its Tanks (an attacker must get past them first). The fight with a leader and a Screamer against an extra Skirmisher had slipped to 5 of 20 and is back to 10 of 20.
- The war tests give both sides the same founder (the bends shape fights).
- A starving bot Founder kept still until it died; now it goes for food. This is what wiped a rival colony out in the bots test.
- The first builder now also keeps the shell stock; with one builder and the "every third bird" rule, no shells were stocked.
- Shells are stocked for the next tier of the colony's furthest tree.
- War before Trade in the bots' research order (doc p22: War 1 and 2 by day 4).
- *Pearls:* any catch has a 5% chance of an oyster with a pearl (`pearl_per_catch`).

**Checks:**
- `depth.exe --flight-danger-test`: each monster, the volcano, the wreck, the weather, outposts and holding, blockade, wall, assault, desertion, bombs and stimulants.
- `depth.exe --flight-siege [runs]`, **the gate**: a bot takes the cove and holds it while a rival lays siege. It passed 3 of 3 (some runs take it with a single nest).
- Shots: `flight_kraken`, `flight_ape`, `flight_volcano`, `flight_storm`, `flight_outpost`.

**Open for stage 8 (balance):**
- *The bots test* (`--flight-war bots`): the bots now raise warriors and reach War 2 by about day 10, but don't raid in 14 days. Their colonies stall near 20 birds, short of food.
- *The colony gate* (`--flight-sim tropical 16 careful`) peaks at 35 birds, against 40 (the doc wants 40 by day 6).

## Stage 8: costumes, sound, internet play, the balance pass (done 2026-10-03; targets met or logged)
**Costumes** (doc pp. 28-30; `flight_costumes.*`, `data/flight/flight_costumes.json`):
- 52 Founder costumes: 10 in the token shop (50-250 tokens) and 42 in the egg crate (15 common, 11 rare, 10 super rare, 6 special at 60/25/12/3%).
- A crate costs one token. Tap an egg to hatch it; a duplicate hatches a feather.
- Colony liveries: six colours at 60 tokens and six hats at 80; every colony bird wears them.
- Tokens per match: 10, plus 1 per 50 score, 5 for each first-ever (kraken kill, dangerous island held, tier 4), and 20 for a win. Kept in `flight_wardrobe.txt`.
- Drawing: each look is parts on the bird: a hat, extras, a tint, the beak, a scale (the Roc is drawn 3x) and effects (embers, lightning on a dive, a blur, gulls or chicks following, a glow at night). The Founder's Crown shows its best score on the name tag.
- The Roost wardrobe (arcade reel, "Roost wardrobe"): shop, eggs, liveries and owned items, with the Founder turning on a perch in the chosen look.
- Over the network: the look goes in the hello and the snapshot (`World::lookOf`).
- Checks: `--flight-costume-test`. Shots: `flight_wardrobe*`, `flight_costumes_1-4` (the gallery).

**Sound** (doc p32; `sound_flight.inl`, `FlAudio`/`FlightCue`, `FlightAudioFrame` in the scene):
- Beds: sea swell, wind by height and speed, surf by shore type, the cove breathing while the kraken sleeps, rigging at towns, rain in a storm.
- Life: the colony chorus by size and nearness, hungry chicks peeping, gulls, the dawn chorus.
- Music: a 3/4 brass-and-strings theme that layers with colony size (strings always, a pipe for a small colony, the horn theme from 12 birds, a second horn from 40, a flute from 70); a war motif while flocks fight near you; a kraken theme (a tritone heave); a hush at dusk and at night; a horn call or a bell at the end of a match.
- 28 effects, each triggered by a change in the world. Everything is muffled under water.
- `--audio-test` renders 10 Flight states and every effect.

**Internet play:**
- `--net-loop flight 100` passes over GameNetworkingSockets with 100 ms of simulated lag.
- Fixed: a guest who left on purpose had their goodbye destroyed unsent (GNS was torn down at once). The host treated it as a drop and the AI waited two minutes. Connections are now flushed before closing.
- Interest management is by distance (others' birds within 330 m).

**The balance pass** (doc p34 targets):
- *Growth.* Chicks fledge in 1.2 days, clutches are 3-4 eggs, fishers carry 3, and the careful bot courts when its catch keeps up with its mouths.
  - Result: 40 birds by day 10 and 83 by day 16, after which the lagoon is fished to a third.
  - **The user's call:** the doc's days may stretch to fit the progression. The targets are now 40 by day 10 and 100 by day 16 (logged: 83).
- *Bot fixes:*
  - A bot retrained every builder at once (it counted those already retraining).
  - A starving bot Founder sat still.
  - The first builder stocked shells while nests waited.
  - Outposts now get their nests first and their Watchers first.
  - Birds climb over an awake ape, and bots feed it.
  - Shells are stocked for the next tier.
  - War comes before Trade in the bots' research.
  - A 5% pearl chance per catch.
- *War:* upwind fights cost 0.9 stamina a second; the wind rule holds at 77% with equal founders. The Flockmaster leads from the rear, screened by its Tanks.
- *Founders (±3% target), logged:* four-seed runs (`--flight-sim founders 10 4`, 25 minutes). After three rounds of bend tuning the spread was still -9% to +21%. The Sigma's trade-first research wins day 3; the Lyrebird's mates compound. See the latest round in this file's history; 3 of 12 windows match the doc.
- *Logged, not measured:* fisher deaths per fishing-day (1 per 15 safe, 1 per 4 on the cove); siege starvation pacing; the kraken killed about 1 match in 10.
- *Gates:* `--flight-siege 3` passes (3 of 3 taken, 2 held); `--flight-war all` passes; the colony gate reaches 40 by day 10.

## The expansion: the long match (doc pp. 35-51), done 2026-10-03
Code: `flight_long.cpp`. Data: `data/flight/flight_long.json` (every number). Checks: `--flight-long-test`. A long match is picked by seasons (2, 3 or 4) in the lobby or on the solo plate.

**The calendar:** stretched by about 1.5 to fit the colony's growth (the user's call). Spring is days 1-5, Summer 6-11, Autumn 12-17, Winter 18-24; matches of 11, 17 or 24 days.

**Seasons** (doc pp. 35-36):
- Regrowth near shores and in the open sea, mate arrival, wind, storm frequency, kraken waking, stamina and fish depth.
- Thermals all day in summer; bombs cheaper in summer; autumn gales from one quarter for a day at a time.
- Each season's event, once, on a day within it:
  - *The Spawning:* grounds regrow 3x and the sharks sleep.
  - *The Tuna Run:* 26 Tuna cross the map in a day.
  - *The Migration:* +6 wild mates, and mates come free.
  - *The Long Night:* a day of darkness.
- Winter's islands and nests count double in a four-season match.
- HUD: the season, the day and an event banner.

**Decrees** (doc pp. 36-38):
- 24 in the data, each with its effect and trade-off as numbers (and "tomorrow" numbers for after-effects).
- Three dealt at dawn, no repeats in a match. Bots choose at once; people pick from cards, or get a Day of Rest by mid-morning.
- The hooks cover the catch, splash, growth, spoilage, hatching, clutches, building, mates, guano, conversion, trade and the Tithe, reputation, morale, raids (Egg Watch), flock speed, formations, Grudge, Call to Arms, scouting (Scouts Aloft, Fog Bank), Lighthouse, Thermal Day, Offerings, Salvage, Hospitality and healing.
- Snapshot and order: `FA_DECREE`.

**Founder perks** (doc p38): 12 perks, offered at days 5, 11 and 17 (three each). Bots pick by a build order. The hooks cover stamina, attack and led flocks, carry, Loud Voice, Mother's Instinct, Pearl Diver, Weather Sense, Nine Lives, Sharp Eyes and Thief's Eye, Diplomat and Old Salt. Order: `FA_PERK`.

**Veterans and mates' traits** (doc pp. 42-43):
- A warrior that survives three fights becomes a veteran: a name from a list of 40 and +15%.
- Veteran traits: Fearless (+15 morale to its flock), Lucky (survives a killing blow once), Keen (hears silent raids), Greedy (a fish per kill), Loyal (+20% beside the Founder).
- Mates arrive with one of six traits that their chicks inherit. A bowl filled with the wanted trait's favourite fish calls that trait (`FA_WANT_TRAIT`, on the colony panel).
- Veterans wear a feather and a name tag.

**Six more founders** (doc pp. 39-40): the Gannet (Plunge Strike from a dive), the Pelican (the Pouch: the Founder brings two fish, feeders carry bigger), the Frigatebird (pirate raids), the Penguin, the Owl (night fishing, deaf to the Sirens) and the Phoenix (a starving chick fledges small instead). There are 18 founders now; their bends are in `flight_bends.json`.
- Balance, round 6 (`--flight-sim founders`, 18 founders x 3 seeds, 10 days): every founder lands between -7% and +8% of the field. Round 5 was -10% to +11%. 6 of 18 windows land where the doc puts them.

**New roles** (doc p41): eight warriors (Plunger, Swallow, Mimic, Nurse, Ferrier, Lancer, Harrier, Drummer) and six workers (Diver, Gardener, Keeper, Teacher, Herald, Augur), each opened in a long match by the tree it grows from.

**Relics, legendary birds and great events** (doc pp. 44-46):
- 8 relics lie on dangerous islands; they're taken by landing on them and can be stolen in a raid at the shrine.
- 5 legendary birds come as the Visitor and join for a fish.
- 8 great events, one per match: Red Tide, the Kraken Walks, the Great Storm, the Eclipse, the Treasure Ship, the Plague, the Calm, the Visitor.

**Neutral factions** (doc pp. 46-47):
- The pirates: hired with fish to raid a rival.
- The fishing fleet: its boats work the grounds, and a bird that steals from them pays in reputation.
- The Grey Wings: they hunt chicks and lone fishers until paid tribute.

**Diplomacy** (doc pp. 47-48):
- Feed pacts: twice a day the better-fed colony sends fish down the line, and a raid on either partner is war on both.
- Flock loans: a flock lent for a day, for fish, follows the borrower's lead flock.
- Bounties: fish posted on a rival's Founder, collected by its killer.
- Breaking a truce costs 20 fervour and reputation.
- Orders: `FA_PACT`, `FA_LOAN`, `FA_BOUNTY` and `FA_BREAK`. The section is on the long-match page (Tab, page 4).

**Fishing mastery** (doc p48):
- Six techniques: Plunge (big fish; sharks see the splash; a miss is 2 s underwater; the Gannet), Skim (small fish; the Swift), Hover-strike (precision and shallows; slower; frigatebirds steal), Drive (needs three fishers on the ground; a triple splash the sharks come to), Deep dive (Divers, Penguins, Deep Dive research; reaches 2.5x deeper; a predator is a fight, not a death) and Night fishing (dusk, dawn and night; two fish a trip; safe only for the Owl and the Shadow).
- The colony panel picks one (or auto, which reads the log and now and then tries an untested one). It shows catch and loss per dive for every technique on the chosen ground.
- Mastery grows with each catch (+0.006, to +25% hit at full). Order: `FA_TECH`.

**Nest styles** (doc p49), chosen for new nests on the colony panel (`FA_NEST_STYLE`). The default is the Cup.

| Style | Cost | Site | Effect |
|---|---|---|---|
| Platform | 20 twigs | any | holds 6 eggs; egg thieves find it from farther |
| Burrow | 5 twigs | low ground | hidden from scouts; a storm floods it and drowns its young |
| Hanging | 15 twigs | a palm | no theft, raid or ground creature reaches it; no Watcher can guard it |
| Mud | 10 twigs | by water | fireproof; two days of rain dissolve it |
| Cliff ledge | 8 twigs and 10 shells | a high site | nothing climbs to it |
| Floating | 25 twigs | by water | out on the water, immune to everything on land; sharks take a chick about one day in four |

Each style is drawn differently.

**Structures** (doc p49):
- Perch: the next Watcher's post, +25% sight.
- Smokehouse (Caches 1): the caches keep 1.5x as long.
- Lookout: scouts report from twice as far.
- Rookery: with three adults about it, its eggs stay warm and its chicks fledge together.
- Beacon: lit from the long-match page, it calls every flock home; relit in a quarter day.
- Monument (from autumn, 60 shells): score and nothing else; more than one may be raised.

Costs are in `flight_research.json`, health in `flight_danger.json`. Bots lay these out too, and each is drawn.

**Score additions** (doc p50, the results' "legacy" column):

| What | Points |
|---|---|
| A veteran alive | 10 each |
| A relic held | 60 each |
| A legendary bird alive | 100 (the Dodo 200) |
| A Monument | 30 each |
| Decrees used | 5 per distinct one, at most 60 |
| A truce kept to the end | 20 each (none if the colony ever broke one) |

Winter's holdings still count double.

**The twelve new islands** (doc pp. 43-45; `flight_isles.cpp`). Long-match maps draw from all ten starting and eleven dangerous islands, and a Ghost Ship sails on each. The lobby offers all ten homes. A guest's mirror now knows a long match: the snapshot header carries its seasons.

Starting islands:

| Island | Shape and sites | Rules |
|---|---|---|
| The Iceberg | 15 ledges and a cave | Ice nests cost 6 shells, not twigs. Raiders can't land. A ledge melts each Summer day. |
| Lighthouse Rock | 18 sites on the lamp gallery and rocks | The keeper trades 5 fish for a pearl daily. The beam reveals 400 m at night, blinds night raiders, and lets everyone see the island. |
| Shipwreck Island | 22 sites in the rigging, deck, stern cabin and hold | Starts with a hold of 20 salted fish and the ship's bell. Rats take an egg now and then. The hold floods in Autumn. |
| The Mangrove | 35 sites in the roots | Endless twigs and crabs below. Nothing raids its nests. No thermals. Crocodiles take diving fishers. |
| The Kelp Raft | 20 sites on a floating mat | Fish shelter under it. Kelp is twig-grade. |
| The Cliff Town | 30 roofs and ledges | Two markets, the town's at 1.15x. Wild gulls raid the caches daily unless a Watcher is alive. |

Dangerous islands:

| Island | What happens there |
|---|---|
| Iron Island | Cannons fire on any flock of more than six. Marines shoot into a colony with more than three nests inside. The holder gets the stores (30 fish) and a pearl a day for the flag. |
| The Whale | Krill feeds the holder. It dives once a season: everything on it is lost. An Augur warns a day ahead, others half a day. |
| Siren Rocks | Birds within 160 m fly to the rocks and sit for a day, unless the colony has a Drummer or an Owl Founder. The holder gets +30 fervour. |
| The Maelstrom | Anything below 30 m within 110 m is pulled in. The rocks can be held only on a calm day, and raiders can't touch nests there. The holder gets a pearl and fish daily and one relic. |
| The Ghost Ship | Drifts (as a function of time) and can't be held. The Drowned take eggs within 150 m at night. A Founder who boards it by day gets the cabin's relic and fires its cannon at the nearest rival, once a season. |
| Bird Island | Mobs anything that lands. Priests convert it (an Ibis 10x faster), and the converted colony is the holder's. The holder's wild mates and guano are refilled. |

Numbers are in `flight_long.json` "isles". Shots: `flight_isle_*`.

**The mastery map** (doc p50): eight hints, in the doc's order, each shown once when its moment first comes. A returning player isn't told twice: they're kept in `flight_wardrobe.txt` ("hints").

**Not done, or done more simply than the doc:**
- The Iceberg and the Kelp Raft don't drift; the Whale surfaces where it dived.
- The Siren's "a mate of any trait for a pearl" isn't built.
- The Ghost Ship doesn't ram islands.
- The Mangrove isn't burnt faster by Bombers.
- Nests on the Whale aren't moved automatically before a dive.
- Bots don't choose nest styles or techniques (they use the Cup and auto).

## The Long Flight (the two-hour expansion), built 2026-10-03
Design: `Reference_For_Future_MP_Games/The Flight — The Long Flight (two-hour expansion).pdf`; its OCR is in `docs/longflight_pdf_pages/`.
Code: `src/flight_longflight.cpp`, with the Far Sea's shapes in `flight_isles.cpp`. Data: `data/flight/flight_longflight.json`.
Checks: `depth.exe --flight-longflight-test` and `depth.exe --flight-long <days> [players] [seed] [runs]` (a full bot match and its report).

**The match.** Picked in the lobby or on the solo plate as "The Long Flight" (48 days) or "Long Flight, short" (36 days).
- Internally `MapOpts::seasons` is 8 or 6, and `World::LongFlight()` means `seasons >= 6`. Everything here is gated on it.
- A year is the four-season match's 24 days (the user's stretch of the doc's days), and the seasons come round again in year two.
- Each season's event happens once a year. Year two's Long Night lasts two days.
- Perks are offered again to the heir in year two.

The ten layers, in the doc's build order. Each one has its own tests in `--flight-longflight-test`.

1. **Generations.**
   - The Founder ages: young for days 0-12, in its prime for 12-18 (+10% speed and attack), then old (6.7% slower and shorter of breath each day, no size-4 fish, and the flock it leads gets a doubled bonus).
   - It dies of age when its year is up. The heir is marked automatically as the first chick (re-mark it on the Dynasty page) and succeeds with a choice:
     - **the Old Way:** nothing changes.
     - **the New Broom:** the colony takes the heir's mother's founder species, if she was wild-born of another species (30% of the Long Flight's mates are).
     - **the Pilgrimage:** a held relic's effect is kept for good.
   - The heir keeps one perk (your choice) and its mother's trait as a permanent bonus.
   - No heir means a regent: the oldest veteran rules without the founder's bonus until a chick grows up.
   - Every succession brings a day of grief (fervour -20, the flocks come home), and everyone is told it's the day to raid.
   - The dynasty is named at the first succession.
   - Veterans that survive a year become elders: they don't fight and they teach (a Keen elder makes the scouts exact).
   - Violent deaths still respawn the Founder, as you decided for the base game. Only age kills it in the Long Flight.
2. **Evolution.**
   - Twelve traits, two bits each in `Bird::genes`. The first six are the mates' traits.
   - Ranks I to III: rank II doubles a trait's bonus and III triples it.
   - Inheritance comes from the mother (the mate) and the nest's line (`Nest::line`, its last fledgling's genes).
     - Where both share a trait, the chick's rank is one higher. Otherwise it gets one trait from each parent, up to three.
     - A hybrid line (the mother of another species) doesn't rank up.
   - 5% of chicks mutate. 1% are rare births: Albino, a Giant (twice the HP and attack, eats for three) or Crested (+5 fervour).
   - 20 birds with one trait at III make the colony a species: it's named, and every bird has that trait at III. A second power comes at 40 birds.
   - The look: Hardy III birds are broad, Quick III slim, Fierce III red-beaked, Giants big, Albinos white.
   - Bots breed for one trait from day 7.
3. **The Far Sea.** At dusk on day 24 the fog lifts.
   - Eight places in a ring well beyond the old map:
     - the Ice Shelf (penguins to convert, krill)
     - the Archipelago of Thorns (twelve islets; thorn-birds to convert; brambles tear Skirmishers)
     - the Drowned Fleet (drifts toward the loudest colony; the Drowned take eggs at night; boarded by day for twigs, powder and the Admiral's relic)
     - the Roc's Peak (the Roc hunts flocks of more than ten at noon and carries off nests; it can be fought; a Founder who lands on the peak while it hunts takes an egg that hatches a Giant)
     - the Mirror Lagoon (+50% to the catch; mates for the holder; a fight clouds it for a day)
     - the Sunken City (relics, pearls and fervour; the Lost Ones keep it; it sinks a spire a week)
     - two Far Sea ports, one with the navy's frigate (it hunts pirates, Frigatebird colonies and the embargoed)
   - Beyond it the Storm Wall kills birds; only an Albatross crosses it (100 score).
   - The islands exist from the start, behind a fog that turns birds back, so the sea's simulation is unchanged.
   - The snapshot's position range was widened to ±6000 m.
4. **Grand Projects.**
   - Nine wonders, one per map, with the doc's costs and places. The Founder consecrates a site where it stands; builders bring the twigs and shells over at least four days; the pearls and sulfur are paid at the end.
   - Whoever finishes first gets it, and the others' progress turns to shells.
   - Each has its builder and map effects:
     - **The Great Rookery:** chicks fledge together; others' courtship is slower.
     - **The Lighthouse of Birds:** a nightly beam, seen by everyone.
     - **The Fish Gate:** doubles a ground, with a toll of a fish in five.
     - **The Sky Temple:** fervour 100; bless or curse.
     - **The Great Works:** three bombs a day; turns the wind.
     - **The Market Hall:** runs the Exchange (see 6).
     - **The Kraken's Chain:** the kraken rises against a marked colony.
     - **The Ark:** mates; moves a region a day.
     - **The Monument of Feathers:** 500 score.
   - Second tiers can be raised in year two at half the cost (the score doubles).
   - An incendiary burns scaffolding. Bots consecrate at home once they have 25 adults.
5. **Leagues, the Council and the Great War.**
   - Leagues are made by Herald: a pearl from each, bound in a day. Members share sight, are at truce, and split the final score evenly. Leaving costs 30 fervour and makes you oathbroken for a season (towns -20%, no new leagues).
   - The Council sits on day 31 and every six days after:
     - each player proposes one motion; votes are weighted by islands held plus relics.
     - motions: Peace of the Sea (raiding during it breaks your oath), the Hunt (300 and a relic for the kraken or the Roc), Embargo, Sanctuary, Tithe, and the Great War (the proposer's league against everyone).
   - The Great War: no truce across the sides; every day costs 5 fervour and a fish a bird; it ends at two-thirds of the fronts, a Peace, or six days. Each winner gets 200.
   - Bots propose, vote their interest, and seek leagues in year two.
6. **Trade empires.**
   - Six wares: salt fish, lamp oil, spices, iron, cloth and feathers. Their sources are wrecks and the Fleet, Lighthouse Rock, the Far Sea towns, and every colony.
   - The Bird Exchange's board is the best price to sell and the cheapest to buy. The Market Hall's builder takes a fee (1 in 10 to 1 in 40) and can embargo one colony a season.
   - Chartered routes (four at most) are flown by the colony's Traders. Three or more on a route is a convoy: +20%, and hunted by pirates unless a Tank or Harrier escorts it. A route kept for a season earns the towns' reputation.
   - Market events: fish prices triple, spices vanish, a feather festival, a cornering. An Augur warns a day ahead.
   - Bots sell their wares and charter routes.
7. **Culture.**
   - **The Chronicle:**
     - **Moments it records:** foundings, the first clutch, raids given and suffered, the deaths of Founders, veterans and heirs, successions, elders, wonders, leagues, oaths, beasts, the Storm Wall, species, songs, plagues.
     - **Chapters:** one per season.
     - **The end of the match:** it is written to `flight_chronicle.txt` in score order.
   - **Titles, worth score:** the Fisher King, the Stormrider, the Kraken's Bane, the Peacemaker, the Oathbreaker (-50), the Shepherd, the Wonder-builder, the Dodo's Keeper, the Last Founder.
   - **Songs:** a Drummer and fervour over 50 write one a season, named for its best moment. It plays as a synthesized pentatonic phrase near the colony's island and over its flocks (`FlightSong`).
8. **Tools and taming.**
   - Three Clever III birds learn a tool a season: the Hook, the Shell Hammer, the Fire Carry, the Mirror and the Rope.
   - Faith 3, a Priest and six days of five-fish offerings tame one beast:
     - **Dolphins:** half the shark risk.
     - **A sea turtle:** mates.
     - **The Grey Wings' eagle:** a Striker that never eats.
     - **A crocodile:** guards the home shore.
     - **The great ape:** stones enemies near home; eats a big fish a day.
9. **The Reckoning and year two.**
   - **Catch-up winds:** the bottom third gets faster mates.
   - **The grounds remember:** year two's capacity is the year-one average, between 0.5x and 1.2x.
   - **Plagues:** in colonies over 80 birds in year two; Nurses or the Chemistry tree cut the losses.
   - **The countdown:** on the HUD from day 37.
   - **Day 43, the Reading:** every Chronicle is shown in score order, 15 s each; the screen can be skipped.
   - **Days 44-45, the Great Storm of the Year:** an unhedged wonder loses half its score; the Fleet runs aground; the Storm Wall closes in.
   - **Day 46, the Kraken's Reckoning:** the kraken, the Roc, the Grey Wings and the Drowned each come for their debtor. A prepared colony (three Watchers, a hedge or a tower) loses 5% of its nests, an unprepared one 15%.
   - **Day 48:** Reckoning Day.
   - **The last winter:** holdings count triple.
   - **Score lines:** a species 200 (+200 for a second power), an unbroken dynasty 150, elders 20 each, coming through the Reckoning without losing a nest 150.
   - **Tokens:** double, +50 for a first speciation, wonder and Great War win; the Dynast crown, worn at once.
10. **Save and resume.**
    - Solo matches save the whole world at each season's break (`flight_longflight_save.bin`).
    - "Resume the Long Flight" appears on the arcade's Flight plate.
    - The save uses the network snapshot in a save-everything mode (`gSaveAll`: every colony in full, every fish), plus the sleeping stocks.

The long-match page (Tab, page 4) now has sub-pages: The year, Powers (the neutral powers, diplomacy, leagues, the Council), Building (structures, Grand Projects), Dynasty (generations, evolution), Trade, and Culture (the Chronicle, songs, tools, taming).

**Not built, or simpler than the doc:**
- Dynastic marriages, and the Matchmaker title.
- Hybrids carrying two founder boosts at half strength.
- The Ark carrying a project.
- The Market Hall setting prices.
- The Gate's wall.
- Beasts taking sides in the Great War (only the Chain fights).
- Route passage through others' territory.
- Convoy gulls.
- War decrees (the war deck).
- The Ice Shelf advancing.
- A networked save and resume (it is solo only).
- The arcade marquee for the winning dynasty.

**The doc's open questions** (defaults taken; the user may change them):
- No abdication.
- Speciated colonies aren't saved to the profile.
- The Great War stays a Council decision.
- The Reading is skippable.

## Winter famine (2026-10-04)
- The long match's population crashed every winter (173 to 84, and 155 to 60 at the end). The trace (`DEPTH_LFTRACE=1`)
  showed catch running under the mouths all match and falling to almost nothing on the Long Night (event 3: a whole
  day dark between two nights). Changes: winter regrows at 1.0 (was 0.8) and sends the fish 0.5 m deeper (was 1.0);
  a colony asleep through the Long Night eats a quarter (was half; a storm still half).
- `--flight-long 48 6 5 1` after: 112 birds after the first winter (was 100), 89 at the end (was 34), a wonder (the
  Great Rookery, day 46), one colony speciated (the Broadback Alchemist), the Reckoning took 12% (the doc: 10-20%).
  Starvation is still the main killer (525 in a match): the next lever is the bots' growth (they court on today's
  catch, not the grounds' yield).
## The towns and the islands dressed (2026-10-05, the user's 3D polish list)
- tools/artgen/flight_town.py -> assets/flight/town (18 models): two styles of house (plaster, tiled gable roof, shuttered windows, a door with a step, a chimney; one with a porch and window boxes), the church tower (clock, belfry with its bell, spire), a pier on posts with bollards and a lamp, two old rounded cars, a lamp post, a bench, a market stall with an awning, a picket fence, two broadleaf trees and a pine, a bush, a flower patch, rocks, a grass tuft, a woodpile. The town's boxes are drawn as these (`baked` in `DrawWorld`; the boats are the Trawl's skiff); beyond 900 m the boxes stand in.
- `DressIsland` (flight_game.cpp, from a hash of the island so every client builds the same): a town gets a main street from the harbour up to the church square (kerbs, a dashed centre line, lamps every 13 m), a cobbled lane from every door, a paved square with the stall and benches, a parking lot by the harbour with white bays and cars, a tree behind each house, flowers by the doors, fences; every green island gets bushes, grass, flowers, rocks and the odd tree among its palms; rocky islands rocks and grass. Draw distances by size (grass 90 m .. trees 320 m).
- **Townsfolk** walk from their doors to the square or the harbour and back on the shared crew figure in everyday colours (`DrawTownsfolk`; the nearest dozen within 150 m).
- The core stamps roofs into the heightmap (so a bird can land on them); the terrain is now drawn at the surrounding ground level under the buildings, so they no longer sit in blocks of turf.

