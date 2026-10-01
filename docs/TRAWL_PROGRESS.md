# The Trawl: build log

Design: `The_Trawl_Reference/The Trawl — Arcade Game 2 Design Document (Draft).pdf` (50 pages). Numbers live in
`src/trawl_data.cpp` (and, from stage 3, species records in `data/trawl/trawl_species.json`).

Build order (the design doc's own):

| Stage | Work | Gate | Status |
|---|---|---|---|
| 1 | The boat alone: hull physics, engine, stations, top-down deck, movement, one player | The boat rolls, lists under weight, and sinks when flooded | **Done** (`--trawl-boat-test`) |
| 2 | Lines and fishing: Verlet lines, cast, bite, fight, landing against a dummy fish | `--trawl-fight` numbers in range | **Done** (`--trawl-fight all`) |
| 3 | Ecosystem port: the water column, light layer, vibration, Wake, the Lagoon's species | `--trawl-eco lagoon` curves stable with no crew | **Done** (`--trawl-eco lagoon 27`, `--trawl-eco-test`) |
| 4 | The session loop: dock, Chandler, sail, clock, sell, quota | A solo night on the Lagoon is playable end to end | **Done** (`--trawl-session-test`) |
| 5 | Networking: six players, voice, prediction for reeling | `--net-loop trawl` and a six-player LAN night | Needs the shared arcade networking layer |
| 6-10 | Shooting, the net, set gear, death and ghosts; the Weeds and the Grotto; diving and wrecks; Atlantis Waters, the Kraken, the Ghost Ship; tuning and sound | | |

## Stage 1: the boat alone

Code: `src/trawl.h` (the headless core: `Sea`, `Boat`, `Crew`, `Gannet`), `src/trawl_boat.cpp` (physics),
`src/trawl_data.cpp` (numbers, stations), `src/trawl_art.cpp` (pixel art), `src/trawl.cpp` (the scene).

- **The sea**: four directional waves (a Gerstner-style sum) whose amplitude is the weather's swell (Calm 0.2 m,
  Fog 0.3, Rain 0.6, Squall 1.5, Storm 3, the Glass flat); a wind and a current push the boat.
- **The hull**: a rigid body on the surface (position, heading, velocity, yaw) plus roll, pitch and heave as damped
  springs toward a target set by eight hull points against the waves and by every weight aboard. Each `Load` (a
  hand, a fish, the hold, the net) sits at a deck position, so five hands at the port rail list her to port (3.2 deg)
  and three tonnes of net on the stern lift the bow (2.6 deg). Bilge water sloshes to the low side and eats her
  stability (free surface), so a flooding boat lists harder as she fills.
- **Damage**: six sections at integrity 0-100; below 40 a section leaks (up to 60 kg/s), below 10 it floods
  (140 kg/s); green water comes over a rail she puts under or when she's on her beam (35 deg). She founders when
  her freeboard goes. A patch kit stops a section's leak until it's hit again; the bilge pump takes 22 kg a stroke.
- **The engine**: coal shovelled from the bunker into the firebox burns hotter the fuller the firebox is; the
  telegraph (slow astern, stop, slow, half, full) draws pressure into the shaft. Green gives full thrust; red for
  more than 5 s blows the relief valve (a fire, steam, and 20 s with the screw stopped). A sack (18 kg) runs her
  182 s at half (the doc: "about 3 minutes"). The screw's noise by telegraph (0/2/5/9) is ready for the web.
- **The crew**: top-down, 8-way at 3 m/s; past 12 deg of roll an unbraced hand slides to the low side, past 25 deg
  they fall, braced (Shift, or at a station) they hold; on her beam ends a hand at the rail goes over it. The
  ladder abaft the wheelhouse goes down to the engine room (boiler, bunker, bilge pump).
- **Stations** (the doc's list, laid out on a 22 x 6 m deck): the helm (A/D rudder, W/S or scroll telegraph), the
  boiler (left mouse shovels, right mouse bleeds), the bilge pumps work now; the rods, the net winch, the lantern,
  the sonar, the cannon, the gutting table, the air pump, the bell and the printer are placed and wait for their
  stages. One hand per station. The Bosun works 25% faster at the boiler and the pump and sees the six sections.
- **Art**: the deck is drawn in the boat's own frame at 12 canvas pixels a metre into the pixel canvas (twice the
  parkour scale on screen), bow to the right, so the planks stay crisp while the sea turns under her. The sea is
  black outside the lights (the lantern mast's 14 m pool, less in fog; the wheelhouse; the stern lamp), with swell
  bands, crest foam, a wake and foam along the hull, most on the low side; the view rides a few pixels toward the
  low side as she heels. The wheelhouse roof comes off when you're inside; below decks the deck darkens and the
  engine room shows (the firebox glowing with the fire, the pressure gauge's colour, the coal, water in her, steam
  when the valve blows). HUD: a telegraph dial and heading at the helm, a pressure gauge and the coal at the
  boiler, each section's state at the pumps; nothing else, on purpose.
- The arcade's reel now sails the Gannet solo ("Sail (solo)"). Shots: `--shots shots trawl_` (deck, engine room,
  wheelhouse, squall). `--trawl-boat-test`: 20 checks (the squall rolls her to about 19 deg, the Glass holds her
  still, the port-rail list, the net trim, making way on steam, the valve, the sack's burn time, a leak against one
  pump, two flooding sections sinking her, a patch, sliding, falling, going over the rail, the ladder and stations).

## Stage 2: lines and fishing

Code: `src/trawl_fish.cpp` (the fight, the bite, the bot angler, the rods aboard, `--trawl-fight`), tables in
`src/trawl_data.cpp` (tackle, line types, hooks, pull ratios, bot skills, the dummy fish), drawing in
`src/trawl_art.cpp` (`DrawLines`), controls and the reel gauge in `src/trawl.cpp`.

- **Tackle** (the doc's table): handline 8 kg, light rod 6 kg / 30 m, medium 15 kg / 25 m, heavy 40 kg / 15 m
  (2-speed), deep-drop 30 kg straight down, big-game chair 120 kg (2-speed), each with a reel speed, a spool and a
  rod softness. **Lines**: mono (18% stretch at its rating, 0.2 s shock window), braid (3%, 0.5 s, cut by teeth),
  wire leader (bite-proof, 1 s), glow. **Hooks**: small, circle (sets itself on the reel; falls out after 1.5 s of
  slack), big-game treble (holds best).
- **The line is physics**: tension = the stretch between the rod tip and the fish times a stiffness from the line's
  length, stretch and the rod's bend. Over the drag the line slips (pays out so the tension is the drag); the reel
  gains line only under the drag (a 2-speed reel drops to its low gear past half the drag). Over the line's
  (chafed, wrapped) rating longer than its shock window, it snaps; a drag breaking loose from rest jerks the line
  1.5x. Out of spool, it's gone. A 14-node Verlet chain gives the drawn line its sag in the water.
- **The fish** (doc, "Fight tuning"): pull = weight x the pattern's ratio (1.5x in a burst), stamina 20 + 6 sqrt(w)
  seconds, drained by tension / pull a second (x1.5 side pressure within 45 deg of square to its heading, x0.6
  straight back, pumping counts full on a dive), recovering 0.3 s a second under 20% of its pull, and it gives up
  under 15% and comes alongside. It swims its pull against the tension (free speed 2.5 m/s at full pull, 3.2 cap).
  Patterns: **Run** (4-8 s bursts every 10-15 s, wandering between), **Dive** (sounds; +5% pull per 10 m), **Jump**
  (leaps every 8-12 s while it has half its stamina: bow within 0.3 s or 40% it throws the hook), **Circle** (laps
  under the hull: each keel pass chafes 10% unless walked clear), **Cover** (dashes for rock within 15 m; in it the
  line chafes 5% a second until pulled out), **Roll** (each roll wraps the line: -10% rating without wire). Two
  patterns alternate every 15-30 s. A fifth of small-hook sets are light and shed on head-shakes; a hook also tears
  with (tension / rating)^2 while it thrashes (soft mouths more, treble less); a rough bottom chafes the line.
- **Landing**: under 5 kg lifted; 5-40 kg gaffed by a hand (skill), a miss sends it off on one more run; 40 kg and
  over the tail rope. Landed fish go into `Gannet::hold` (stage 4 grades and sells them).
- **The bite** (`Bite`): inspect (the tip ticks), 1-4 nibbles, the take (250 ms window, 150 ms wary, +75 ms for
  an Angler); striking early spooks it, a missed take drops the bait.
- **Bot angler** (`BotFight`, skills Green/Able/Old Hand from the doc plus bow and keel rates): drag at a third of
  the rating (half in the chair's harness; 0.6 to turn a fish before cover, 0.7 to pull it out), reels under 90% of
  the drag, leans the rod for side pressure, pumps on dives, bows on jumps.
- **The gate** (`depth.exe --trawl-fight all [N]`, Able bot, matched tackle; 500 fights each):

  | Fish | Tackle | Landed (target) | Fight (target) |
  |---|---|---|---|
  | 3 kg snapper | light rod | 86% (85%) | 36 s (20-40 s) |
  | 15 kg lingcod | medium rod | 71% (70-80%) | 107 s (60-120 s) |
  | 40 kg yellowfin | heavy rod | 58% (60%) | 195 s (180-300 s) |
  | 150 kg sturgeon | deep-drop reel, braid | 46% (45%) | 821 s (600-1200 s) |
  | 250 kg blue marlin | big-game chair, treble | 38% (40%) | 559 s (480-720 s) |

  `--trawl-fight <fish> [tackle 0-5] [N]` compares the three skills (`DEPTH_TRACE=1` prints a fight second by
  second). The dummy fish carry `pullK`/`staminaK`/`softMouth` where the pattern alone missed the doc's times
  (the sturgeon is sluggish and long, the yellowfin tireless and soft-mouthed, the marlin's bill holds a hook);
  stage 3's species records take them over.
- **On the Gannet**: four rods (port light, starboard medium, stern heavy and deep-drop; T changes tackle at an idle
  rod). Hold left mouse to charge a cast, release to throw at the mouse (never across the deck; the wind drifts it);
  the float sinks to its depth (scroll); a dummy bite table stands in for the web (one bite about every 18 s once
  settled). Space strikes the take and gaffs a fish alongside; left mouse reels; right mouse bows; the mouse held
  to one side of the line leans the rod; the wheel is the drag. The fish pulls the boat through the line (a force
  at the rail and a heel toward its side). The reel gauge shows the tension arc, the drag mark and the red past
  the rating, the line out and the depth; an Angler sees the species and its weight. Shots: `trawl_fishon`,
  `trawl_jump`. `--trawl-boat-test` now also runs 14 rod checks (the cast, sinking, the bite's windows, a fish on
  the Gannet heeling her, a snap with the drag screwed down, the drag holding a run).

## Stage 3: the Eclipse Lagoon's food web

Code: `src/trawl_eco.h/.cpp` (the web, the chart, the fields, the agents, bites and thieves, Wake, `--trawl-eco`,
`--trawl-eco-test`), species in `data/trawl/trawl_species.json`, drawing in `src/trawl_art.cpp` (`DrawLife`, the
seabed in `DrawSea`), the Gannet's side in `EcoTick` (trawl_eco.cpp) and `Gannet::StepRods` (trawl_fish.cpp).

- **A design call: the Trawl's web is its own code, not an adapter onto Red Tide's `rt::Ecosystem`.** Red Tide's
  engine is built round zones, doors, factions, tides and divers; wrapping it for an open 600 m lagoon seen from a
  boat would have been more code than writing the doc's rules directly. What's shared is the species file format
  and loader style (the same `json.h` reader) and the ideas (agents, a blood field that diffuses and drifts, senses
  by radius). The doc allows this: "the Trawl reuses that code to save time, not its design".
- **Species records** (`trawl_species.json`): the Lagoon's 21 species from the doc's roster plus the gull flock, in
  the shared format (class, size, tier, diet, blood threshold, senses, temperament, home, social) with the Trawl's
  four fields: depth band and night band, light response (drawn, neutral, shy), bite profile (baits, tackle) and
  fight pattern. The fight patterns feed stage 2's `Fight` directly (`Eco::SpecOf`).
- **Time**: one real second is one minute of the night (20:00-05:00 is nine real minutes). Population rates are per
  game hour; blood (3% a second), vibration and Wake (a point a real minute) run in real seconds.
- **The population layer**: every species' biomass for the whole ground, and five resources (plankton, benthos,
  algae, seagrass, carrion). Each consumer eats `q B f` an hour, where `f` is its prey's abundance through a
  saturating response (half at the start); it keeps 30% of that and loses the rest of its balance half to a constant
  rate and half to crowding. The model is **calibrated so the start numbers are its balance**: top down through the
  diet graph, each species' turnover is raised if its predators eat hard, and each resource's stock and regrowth
  come from its "turnover" (hours to eat through the standing stock). So a ground with no crew holds still, and a
  ground knocked 30% off balance settles back (15% mean deviation to 6% over three weeks).
- **Cascades** (doc, "How the web plays"): netting 60% of the forage leaves snapper, bonito and jacks hungrier the
  next night (hunger 0.50 to about 0.7), which the agents carry into more bites and bolder hunting; netting the
  parrotfish and surgeonfish lets algae grow and smother the coral (coral health falls below 0.9 by night three) and
  the reef species that shelter there thin out (their mortality rises as the coral goes).
- **The chart**: the atoll's island along the west edge, seagrass flats off it, a basin of 12-22 m with 28 coral
  heads, the crest curving down the east side (3-6 m at high water, caves in its face), the open sea beyond shelving
  to 40 m; nine sargassum rafts drifting on the wind. **The reef tide** falls 1.4 m through the night; the Gannet
  (1.8 m draught) goes aground on the crest or the island (she stops; the kedge anchor comes with stage 4).
- **The fields**: blood, sound and vibration on 4 m cells in five depth bands. Blood decays 3% a second, spreads and
  drifts down-current (0.2 m/s); a hungry hunter smells it over its scent radius (the reef shark 40 m) and follows
  the gradient once it passes its threshold; a lateral line (barracuda 24 m, sharks 30 m) feels a thrashing fish or
  a humming line. Timid fish flee a loud screw or a blast.
- **Light** (the first new layer): the lamps (hooded 4 m, low 8, full 14, the searchlight's cone) light the water
  and, farther, show as a glow. Drawn forage swims in to the light and mills round its pool (under the lamp is
  under the hull); **predators follow the forage** up after it, slower; shy species leave. Measured: after 30 minutes
  a full lantern has about 2,000 forage fish under the hull against 14 hooded, and a light rod by the rail draws
  7 bites in the next hour against 3 hooded (the doc: "roughly doubles bites after 30 minutes").
- **Vibration** (the second new layer): the screw, taut lines (over 30% of their rating) and a fish on a line
  (more as it thrashes) write into it: a barracuda pack 22 m off comes to a hooked fish.
- **Wake** (the third new layer): blood the crew put in, the screw's noise and bright light raise it; quiet and dark
  (screw stopped, lamp low or hooded, the slick gone) it settles a point a minute; a depth charge adds 15. Chumming
  all night on the Lagoon reaches about 21; three depth charges take it past 45. (The big three come in later
  stages; the Great White isn't on the Lagoon until the second deadline.)
- **The agent layer**: up to 260 groups (a school, a pack, one grouper) within 150 m of the boat, spawned out in the
  dark from the density map (past 14 groups a species' agents stand for bigger schools). They keep to their depth
  band and rise at dusk (sardines from 6.5 m to the surface by 21:24), lean toward their habitat, shelter under
  rafts (mahi, flying fish), hunt what they can sense (sight scaled by the light, a lateral line in the dark), eat
  and are satiated for a while. A threat reaching the lantern's edge is logged as an arrival (the crew's first tell).
  Gulls come in over the boat 20-40 s after fish are on deck or blood is at the surface (their thieving comes with
  stage 4's deck).
- **Fishing on the web**: a settled lure asks the agents near it (16 m, 8 m of depth) whether they take this tackle
  and bait; a matched bait bites about 3x as often (snapper take shrimp 3.6x as often as a popper); hunger raises it.
  The fish hooked is that species (its record's weight range and fight pattern) and leaves its school. **Depredation**:
  a hooked fish is prey; the barracuda strike it to the head (you reel in 45% of it, dead) and a reef shark takes it
  whole. A landed fish comes off the ground's biomass; one that gets away bleeds. Until stage 4's Chandler each rod
  carries a default bait (light rod shrimp, medium squid strip, heavy live bait, deep-drop dead bait).
- **On screen**: the web's fish in the lamplight (silver dashes for schools, shapes for the bigger fish, a dark fin at
  the lantern's edge for a shark, jellies as rings, gulls as white Vs wheeling over the boat), the seabed showing
  through the shallows (coral, seagrass, sand), sargassum on the surface; the lantern mast's scroll sets the lamp
  and the mouse aims the searchlight. Shots: `trawl_lagoon`, `trawl_searchlight`, `trawl_shark`.
- **Checks**: `depth.exe --trawl-eco lagoon 27` (three nights with a day between each, no crew: every species within
  bounds, "The ground is stable with no crew"); patterns `quiet`, `chumming`, `trawling`, `depth-charging` print
  populations by the hour, blood, Wake and threat arrivals. `depth.exe --trawl-eco-test`: 22 checks (the gate, the
  settling after a shock, both cascades, the lantern, the night rise, a shark following chum from down-current, a
  barracuda pack coming to vibration and heading a hooked fish, Wake up and down, bites and matched bait, harvest,
  the lamp doubling bites at the Gannet's rail, a rod fished through the night landing the web's fish).

## Stage 4: the session loop

Code: `src/trawl_session.h/.cpp` (the run, the dock's shops, the clock, the telegraph, customs, the count;
`--trawl-session-test`), the quay in `src/trawl_art.cpp` (`DrawQuay`), gutting and the stores in `src/trawl_boat.cpp`,
bait in `src/trawl_fish.cpp`, the dock panels and the HUD in `src/trawl.cpp`.

- **A run** (design doc, "The session loop"): a string of deadlines, three nights each. The quota starts at 400
  shillings for six hands (0.5x solo, 0.7x two, 0.85x four; three and five between), and after each met deadline
  rises 40% plus 60 (times the crew scale). The Owners count it after the third night's sale; short of it, the Gannet
  is repossessed and the run ends; met, 10 arcade tokens and the next deadline. The Lagoon's web carries over the
  three nights (a day of populations between each) and starts afresh with a new deadline.
- **The dock** is walked: moored, the Gannet's port side lies along a quay on the atoll island, a gangplank amidships.
  On the quay: the **chalkboard** (quota, sold this deadline, nights left, money; "Hand in to the Owners" after the
  third night), **the Chandler** (ice 5 per 20 kg, coal 10 a sack, shrimp and squid strips 4 a tin of ten, a chum
  bucket, a patch kit, a pocket watch, the medium, heavy and deep-drop rods at the Tackle table's prices), **the Fish
  Market scales** (every fish in the hold with its grade, freshness and value; sold, each line shows its sum),
  **the Owners' office** (shuttered: salvage comes with the wrecks) and **the Slipway** (hull plates, the bigger ice
  hold, a second pump, the compound engine, the hooded lantern mast that adds the searchlight, the big-game chair).
  The chart table is the helm: only the Eclipse Lagoon is charted yet (10 kg of coal to reach it and back).
- **Starting gear** (the doc's list): two handlines and a light rod (the rods take only tackle she owns: T cycles it),
  a patch kit, 20 kg of ice; 60 shillings (the doc leaves the starting money open; enough for bait and ice).
  Bait comes from the stores at each cast: shrimp on the light rod, squid strips on the heavier ones, a bare hook if
  she's out (fish take a bare hook a third as often).
- **Casting off**: all hands aboard; the night's conditions are rolled (calm, fog, rain or a squall) and printed with
  the moon and a rumour; fish kept from an earlier night lose a quarter if iced and rot if not. The wheelhouse clock
  waits at 20:00 until she clears **the harbour line** (a ring of buoys round the harbour mouth, green seaward, red
  toward the island); past it the night begins and the web wakes (the Gannet fishes only at night). Back inside the
  line she moors herself. **Still outside at 05:00, the customs cutter** seizes the hold, fines a tenth of the money
  and tows her in.
- **The night's clock**: read only in the wheelhouse or with a pocket watch. **The Owners' telegraph** prints on tape
  (the printer station shows the last ten lines; the newest shows for a moment): the quota, the night's conditions,
  MIDNIGHT, ONE HOUR at 04:00, hull damage, a first catch (SPECIMEN NOTED), harbour, the market's pay, customs, the
  count.
- **Fish value** = base price x weight x grade x freshness x glut (x 1.5 for the run's first of a species). Grade:
  hook 100%, less 10% for a bite taken out of it on the line (the barracuda's heads). Freshness falls 1% a real minute
  on deck and 0.2% gutted and iced. Glut: each species' price drops 3% for every 10 kg of it sold this deadline.
- **The gutting table**: hold left mouse to gut, grade and ice the fish in hand (1.2-4 s by weight; ice takes half
  its weight from the stores); the guts go over the rail as blood into the web, and fish left ungutted on deck draw
  the gulls.
- **Checks**: `depth.exe --trawl-session-test` (25 checks: the quota by crew size, the first tape, fish value, the
  first-catch bonus, glut, bite grades, buying, the Slipway, no casting off with a hand ashore, overnight losses, the
  clock waiting for the harbour line, the night and the web starting past it, the customs cutter, the count met and
  missed, the next quota, and a bot playing a whole solo deadline: bait, cast off, fish the port rod, gut and ice,
  home before 05:00, sell, three times; and a bot that stays out past 05:00 losing its hold). Shots: `trawl_dock`,
  `trawl_chandler`, `trawl_market`, `trawl_chart`, `trawl_clock`, `trawl_quota`.
- **Balance, not tuned yet**: one bot at one light rod lands about 20 small fish over a deadline and sells about 70
  shillings against the solo quota of 200. The doc's quota counts bots as crew (a solo player with five bot hands
  faces 400), so a lone hand is meant to be short; the bot crew and the tuning pass (stages 5 and 10) settle it.

## The first-person version (2026-09-30, local session)
The user asked for two versions of the Trawl, one top-down as it was and one first person like Red Tide, otherwise identical. `trawl_view3d.cpp` draws the same simulation in 3D through Red Tide's inked renderer; trawl.cpp chooses the view (`S.fp`), maps WASD to the look and aims with the crosshair. Nothing in the simulation changed (every Trawl test passes). Red Tide's renderer gained point lights and glowing draws, unused by Red Tide itself. Shots `trawl3d_*`. Left to do: proper crew figures, sky, spray and rain, a held-item viewmodel, and tuning the night's brightness in play.

## Bot crew (2026-09-30, local session)
- `src/trawl_bots.cpp`: `Gannet::StepBots` (inside the 60 Hz step, only when `botsOn`), `OrderBot`, `BotDoing`, and
  `--trawl-bot-test`. Every hand after the first is a bot. A small utility brain per bot (`Gannet::Brain`) rethinks
  twice a second, in this order:
  1. The skipper's order.
  2. Water in her: the fireman pumps her dry.
  3. Its role's watch. The skipper holds the Bosun's slot but stands at the helm, so if no bot is a Bosun, the first bot
     keeps the fire. Angler: a rod (port, starboard, stern). Diver: the gutting table when there's fish on deck, else a
     rod. Medic: the gutting table.
- Bots never steer and never decide to go home.
- **Walking:** down the ladder, in and out of the wheelhouse door, and a sidestep when stuck on the drum, the table or
  the mast.
- **Working:**
  - Boiler: shovels below green+0.12 and bleeds at the first red.
  - Pumps: pumps while there's water.
  - Gutting table: guts.
  - Rods: casts outboard. On the take, it strikes once at the skill's `hookSet`, then fights with `BotFight` and gaffs at
    the skill's `gaff` (Green, Able, Old Hand).
  - Net winch: only when ordered.
  - Overboard: swims for the stern ladder.
- **Barks:** "Fish on, port!", "Water in her!", "Man overboard!". They're drawn over the bot's head in both views
  (`CrewHeadOnScreen` in 3D).
- **Arcade:** choose 1-6 hands and the bots' skill. `StartTrawl(g, fp, crew, skill)`; the quota scales with the crew.
- **In game:** G orders the nearest free bot to the station you point at. In first person, `AimAtDeck` finds that
  station on the planks. Pointing at nothing sends every bot back to its watch. A crew list sits top right.
- **Shots:** `trawl_bots`, `trawl3d_bots`.
- **Not yet:**
  - Follow orders.
  - Throwing the life ring for a hand overboard.
  - Patching leaks.
  - Bots using items (rifle, flare).
