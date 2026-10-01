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
- **Emergencies** (second pass), each handed once a tick to the nearest free bot. A bot fighting a fish keeps fighting
  it, and the fireman is pulled off the boiler last.
  - **A hand overboard:** the bot takes the ship's life ring from whoever has it (or the locker) and goes to the rail
    nearest the swimmer. It throws within 17.5 m, hauls a miss back and throws again, then hauls them in.
  - **A leak:** the bot goes to the section (`SectionSpot`) and patches it with the ship's kits (`PatchKits`).
- **Patching** is now real for everyone: `Gannet::StartPatch`. Stand in the leaking section and press E. It takes 6 s,
  or 3 for a Bosun. Walking off, falling or a station cancels it. A kit is borrowed from whoever carries the ship's
  kits. The HUD prompts "E: patch the leak".
- **F (follow me):** the nearest free bot follows you, down the ladder too; press F again and it goes back to its watch
  (`OrderFollow`).
- **Not yet:** bots using items (the rifle, flares).

## Stage 5: networking (2026-09-30, local session)
- **`src/trawl_net.h/.cpp`**, host-authoritative, on the Deep Arcade's session layer:
  - `TrawlWorld` (Gannet + Eco + Session) is one run.
  - The **host** runs the real world inside `TrawlHost` (the arcade `GameHost` for `G_TRAWL`; `Info` says built, 1-6
    players, real time at 20 Hz). The host's own screen draws that world directly.
  - A **guest** mirrors each snapshot into its own `TrawlWorld` (`ReadWorld`), and the same top-down and first-person
    renderers draw it. The guest draws between the last two snapshots: the boat's pose and the hands' places,
    `Interpolate`.
- **Input:** a hand is played by `HandInput`.
  - It carries held keys, the presses since the last step, the deck-frame aim, steer, wheel, a slot pick and a bot
    order.
  - The scene builds it in `Gather()`.
  - `ApplyInput` is the one headless function that turns it into Gannet calls. Solo goes through it too, so solo and
    network play can't drift apart.
- **Commands:** dock buttons and the locker are `DoCommand` commands (`CMD_BUY/SELL/SLIP/CASTOFF/COUNT/CONTINUE/
  LOCKER_TAKE/LOCKER_STOW`). The host checks each one (dock-only commands need her moored at the dock).
- **The snapshot:** one templated `Visit` walks every field the screens draw, both for writing and for reading.
  - It covers the session and tape, the ground's moving parts, the sea and the boat, the stores, the crew and bot
    brains, the rods and fights, the hold, and what's in the water (shots, floaters, flares, the net mesh, set gear,
    rings), plus the locker.
  - The chart isn't sent: `Eco::initSeed` lets a mirror rebuild the same chart.
  - About 1.6 kB at the dock and about 10 kB on a busy night (20 Hz, unreliable channel, GNS fragments).
- **AI seats** are the bot crew: a lobby AI seat, or a player who leaves or is lost past the session's grace period,
  becomes a bot hand.
- **In the arcade:**
  - The Trawl reel's Host/Join/Browse work.
  - A launched match opens the Trawl scene (`StartTrawlNet`).
  - The reel line toggles the view the match starts in; V still switches aboard.
  - In the game menu, "Leave the match" works like this: the host takes the table back to the lobby, and a guest
    hands its hand to a bot.
  - While the menu is open the session keeps pumping (`TrawlMenuTick`): a crew at sea doesn't pause.
  - The HUD shows the other players' names, and a banner while a hand has lost the connection.
- **Checks:**
  - `depth.exe --trawl-net-test`: input frames, merge rules, by-input station taking, commands, the snapshot round
    trip (byte-identical rewrite), chart reuse, cut-off snapshots refused, and the host game with an AI seat.
  - `depth.exe --net-loop trawl [mem]`: a host and five guests launch. Every guest mirrors six hands. A guest walks to
    the port rod by input and takes it. A guest buys bait. Cast-off is refused while the skipper is on the quay, then
    succeeds. A guest casts a line. A guest leaves and its hand becomes a bot. The mirrors agree with the host. It
    passes in memory and over GNS loopback UDP.
  - Shots: `trawl_guest`, `trawl3d_guest` (a guest's own screen in a live in-memory match).
- **Not yet:**
  - Prediction for reeling (the design doc's "prediction for reeling": the guest sees its own rod 50-100 ms late).
  - Voice.
  - Trimming the snapshot (the hold's names, agents far from the boat).
  - A six-player LAN night with real people (the stage's human gate).

## Playtest fixes: the mouse look, the squall, the sonar and the chart (2026-09-30)
- **Mouse look (Red Tide and the Trawl's first person):** on the user's machine raylib's `DisableCursor` didn't capture
  the pointer, so the look stopped at the screen's edge.
  - `MouseLook(on)` (input.h) now hides the pointer and warps it back to the window's middle every frame, returning
    the movement.
  - The main loop's `MouseLookFrameEnd` gives the pointer back to any scene that stopped asking for it, and while the
    game menu is open.
  - The sonar station takes the pointer in first person, like the locker.
- **The squall sank her:** she rolled past 80 deg and filled with green water in about 2 minutes, whatever the helm did.
  The doc says a squall rolls her "up to 20 deg". Now (`--trawl-sail-diag` measures it):
  - `rollDamp` is 0.35.
  - The waves' roll is soft-capped per weather (`WeatherRoll`: 5/7/12/20/30 deg).
  - Green water scales with how far the rail is under.
  - The bilge's slosh is gentler, and `gmEff` has a floor of 0.15.
  - Result: calm 3 deg, rain 8, squall 18, all with no water shipped. A storm (not rolled yet) washes the deck, about 3 t
    in 4 minutes at slow ahead, and only capsizes her driven full ahead with the helm hard over.
- **The sonar station** (`trawl_sonar.cpp`, the doc's "The sonar"), headless in `Gannet::sonar` and in the snapshot:
  - Passive returns near her are drowned out at half speed or more.
  - The active ping fires every 3 s out to 150 m. Returns live 6 s, and the ping is noise in the water.
  - The depth dial (scroll) has four bands.
  - Left click a contact to mark it: a 10 s bearing arrow for every hand (`DrawMarkArrows`) and a line on the tape.
  - A bot on the sonar pings every 6 s and marks the largest school and any threat.
  - The scope (`DrawSonarScope`, heading-up) shows the shore and the shoals as hard returns, schools as dotted clouds,
    fish as blips, gear as squares, and something big as a heavy blot. The side profile under it shows the seabed and
    the returns by depth along her heading.
- **The chart at the helm** (`DrawChart`): the ground's own depths drawn into a texture, north up to match the helm's
  compass. It shows land, the shoals she grounds on, the reef, seagrass and open water, plus the harbour line, the
  Gannet's heading, the marks, and the bearing and distance to the harbour line.
- Shots: `trawl_sonar`, `trawl_helmchart`, `trawl3d_helmchart`. Remaining from the doc's sonar: a second operator
  (Slipway), the side-scan toggle (the profile is always on), kelp shadow, Grotto echo twins, and the Kraken whiteout.

## Finishing the Lagoon, part 1: `--trawl-sim`, the lethal paths, the Stir clock, weather and variants (2026-09-30)
The plan (the user's order): the sim first, then the Stir clock and the Lagoon's special nights, then sound, the
shakedown night, and a tuning pass against the doc's targets. Then the Weeds and the Grotto, diving, Atlantis.

- **`depth.exe --trawl-sim <ground> <nights> [crew 1-6] [careful|greedy|reckless] [runs] [green|able|oldhand]`**
  (`trawl_sim.cpp`): the doc's balance tool (page 47). A scripted skipper plays hand 0 (buys the night's consumables,
  picks marks from the chart, tows the trawl with a bot on the winch, backs off groundings, hauls 40 minutes before
  turning for home, answers canoes by pattern); the other hands are the real bots. It reports quota met, money per
  night by source (`CatchRec::src`), deaths by cause, overboard and rams, each threat's first sighting, a large threat
  late in the night, when the skipper turned for home, the variants rolled, and CPU per night. `DEPTH_TRACE=1` traces.
- **Lethal paths on the Lagoon** (the user chose to add them): the reef shark rams a section when it is alongside in
  blood past its threshold (`D().ramDamage` 15 every `ramEvery` 20 s, and a heel of `ramHeel` that slides unbraced
  hands); a running fish past 45% of the line's rating while she rolls past 12 degrees toward it drags the angler over
  the rail (`D().railDrag`; bots let go by skill). Chum buckets are real (`Gannet::ThrowChum`).
- **The Stir clock** (`Eco::Stir`, json `"stir"`): threats materialise about the boat in proportion to the ground's
  curve (safe 120 min, then rising to 05:00 with exponent 1.5, floor 0.1 for small threats, none for size 5+) plus the
  Wake, never nearer than 60 m, and grow hungry with it.
- **Weather turns mid-night** one night in three (forecast on the tape, right 70%). **Variants**: Bait run, Red tide,
  King tide, Turtle nesting, Canoe night (a canoe alongside for a minute: trade / tribute / refuse; `CMD_CANOE`, the
  HUD prompt). The carcass, derelict and storm wreck wait for the diving stage.

### Numbers (`--trawl-sim lagoon 3 <crew> <pattern> <runs>`, Able bots, starting gear)
Before any tuning, six hands met the quota 100% on the net alone (375 shillings a night, 97% net), three hands 50-67%,
solo never (17-26 a night: the handlines catch bait fish). Threats were "arriving" at 20:00 because they materialised
22 m from the boat; the Stir clock fixed that.

| Crew, pattern | net yield | Quota met | Money / night | Deaths / night | Rams / night |
|---|---|---|---|---|---|
| 6, careful | 1.0 | 4/4 | 371 | 0 | 0 |
| 6, greedy | 1.0 | 4/4 | 426 | 0 | 0 |
| 6, careful | 0.5 | 4/4 | 315 | 0 | 0.08 |
| 6, careful | 0.25 | 5/6 (83%) | 248 | 0 | 0.06 |
| 6, greedy | 0.25 | 4/4 | 276 | 0 | 0 |
| 3, careful | 0.25 | 2/4 | 103 | 0 | 0 |
| 1, careful | 0.5 | 0/2 | 26 | 0 | 0 |

`D().netYield` is 0.25 now (six careful hands at 83% against the doc's 85%). Deviations to log: **deaths are 0.0 against
the doc's 0.3**, because bots brace at stations and rescue well, so the lethal paths only bite human crews; the
greedy pattern still clears the quota every time; three hands sit at 50% and lose a hold to the cutter now and then;
solo play cannot meet even the halved quota with the starting tackle. The tuning pass will take these up (a net-yield
grid, the glut, haul times, the solo quota).

### Skipper fixes and the three-hand result
The skipper keeps the fire itself when nobody mans the boiler (a short crew's only fireman may be on the winch), keeps
the winch hand until the net is in, counts the net's load as progress, and re-orders a hand to the winch when turning
for home with the net down. Three careful hands now meet the quota 3 of 4 (188 a night); six 5 of 6.

## Finishing the Lagoon, part 2: sound (2026-09-30)
`sound_trawl.inl` on the shared voice pool and buses: the music by the session's state (the dock shanty with a verse
per met deadline, the refrain over the engine, the quiet night's drone and fiddle, the fish-on string figure following
tension, silence with a Wake-sized threat within 60 m, the big three's drum and groans, the fanfare and the church
bell), the Lagoon's bed (surf, wind and rain, insects, gulls at dusk, a whale, barracuda ticks, canoe drums) and
36 effects diffed from the world each frame in `TrawlAudioFrame` (trawl.cpp). `--audio-test` renders 12 states and
every effect, and checks the music drops by 6 dB with a threat near. Not yet: the Weeds/Grotto/Atlantis beds and tells
(their grounds don't exist), diving sounds, voice.

## Finishing the Lagoon, part 3: the shakedown night (2026-09-30)
The doc's "First night": solo on the Lagoon with a fixed seed, no quota, deaths that don't count, Kess aboard as an
Old Hand bot. Ten chalked lessons from casting off to the market, with the shark called in on the guts and Kess
slipping over the side (`Session::shake`, `ShakeStep`; the arcade's "Shakedown night" button; a Skip button and
Back to the arcade on the HUD). `--trawl-shakedown-test` plays it through with a scripted hand in about 7.5 minutes
of sim time. Not yet: the Field Manual, the shakedown offered automatically to a lobby with a new player (networked
shakedowns), the sardine ball marked for the net lesson.

### Short crews with the net (sim, netYield 0.25, careful)
Solo 3/3 deadlines met (123 a night against the halved quota of 200), two hands 3/3 (262 against 280), three hands
3/4 (188), six 5/6 (248). Two hands out-earn three in the sim because the short-crew skipper works the winch without
walking (a sim shortcut; a real hand runs between the helm and the winch). Deaths stay at 0.0 with bots.

## Finishing the Lagoon, part 4: the tuning grid, and playtest round 3 (2026-10-01)
`DEPTH_NETYIELD` and `DEPTH_GLUT` override the two levers for a grid (six careful Able hands, 4 runs each):

| net yield | glut 3%/10 kg | glut 6%/10 kg |
|---|---|---|
| 0.15 | 50% met, 195/night | 50%, 186 |
| 0.25 | 100%, 239 | 100%, 222 |
| 0.40 | 100%, 353 | (not run) |

Three hands: 75% at 0.15, 100% at 0.25 (variance: an earlier 4-run set at 0.25 gave 75%). The glut hardly moves the
result (a crew sells a dozen species); the net's yield is the lever. `D().netYield` is now **0.2**, between the two
rows, for the doc's 85%. The three-hand case is left as it falls (short crews a step harder, never hopeless).

Playtest round 3 (the user): the 250 ms hook-set window was unplayable with the space bar: now 0.7 s (0.5 wary,
+0.25 Angler). A speargun with three spears is in the starting kit and the Chandler's guns are about 40% cheaper.
Weapon animations in the Trawl (top-down and first person: swings, kicks, flashes, reloads, the spear sliding home)
and in Red Tide (reload with the magazine coming out and going home, a wind-up and chop for melee, muzzle flash and
bubble puff); Red Tide's gun models rebuilt with a dozen parts each.

## The deck kill and jumping (playtest round 3 follow-up, 2026-10-01)
- **Landed fish:**
  - Every landing site sets `CatchRec::deckAt`:
    - a rod: 1.5 m inboard of the rod;
    - the cod end: spilled over the sorting deck;
    - a gaff, a longline or a pot: inboard of the hand;
    - a tether: inboard of the rail;
    - the harpoon: the bow.
  - Fish dead in the water come aboard `dead`.
- **The flopping:** a live, ungutted fish flops for the nearer rail (`StepDeckFish`: a hop every 7 s plus 0.5 s/kg, netted fish every 16 s). Past the rail it goes back over the side with a little blood.
- **Killing it:**
  - The priest, a knife, or a gaff with no floater kills it where it lies (`KillDeckFish`, reach 1.6 m).
  - A round, a pellet or a spear passing within 0.45 m kills it, at 10% off the grade.
  - A bot at the gutting table clubs anything on deck.
  - Gulls still take any un-gutted fish under 3 kg.
- **Drawing:** deck fish are drawn in both views (top-down in `DrawGear`: a live one arches and slaps, a dead one lies in a smear of blood).
- **Shot fish:** a fish shot deeper than 1.5 m with nothing on it sinks in blood rather than floating. Spraying rounds at passing fish feeds the water, not the hold.
- **Jumping:** Space off a station (`Gannet::Jump`, `Crew::z/vz`). The rail can be cleared, and landing off the deck is overboard (the life ring's job). Drawn top-down as a lift with the shadow left behind, and in first person as the eye rising.
- **Tests:** `--trawl-gear-test` checks:
  - an untended fish goes over within two minutes;
  - the priest's reach;
  - a dead fish stays put;
  - the gutting-table bot clubs;
  - a jump at the rail is a swim and one amidships lands.
- `--trawl-boat-test`'s hook-set checks were updated to the playtest windows (0.7 s, wary 0.5 s, an Angler +0.25 s).
- **Sim:** `--trawl-sim lagoon 3 6 careful 3`: quota met 3 of 3, 197 a night, 0 deaths.

## Step 1: the economy spine (2026-10-01)
- **The three ways to use a fish** (the design doc v2, "Economy and progression"):
  - **The Owners' quota scales** (`DockKind::Scales`, a new weighhouse on the quay): credit at the fish's full value, no glut, no shillings. Fish under 70% fresh are turned away and stay in the hold (`Session::Deliver`, `QuotaValue`).
  - **The Fish Market**: shillings into the purse, with glut. It counts nothing toward the quota (`Session::Sell(idx)`).
  - **Keeping it**: what is neither delivered nor sold stays aboard for barter and the larder. Traders come with the Atoll.
- **Only delivered fish count.** `Session::sold` is now the quota credit delivered. Credit past the quota carries into the next deadline at half value (`carried`, `CREDIT_CARRY`).
- **Panels:** the market and scales panels list the hold with a Deliver/Sell button per fish and an all button. The chalkboard says what counts. The shakedown's last lesson teaches the scales.
- **Commands and networking:** `CMD_DELIVER` and `CMD_SELL` take a hold index (-1 for all). The snapshot carries the last delivery, the rejected count and the credit carried.
- **The sim's skipper** delivers to stay on pace (a third of the quota a night, plus 10%), best fish first, and sells the rest. Short-handed, he now hauls the net himself on the way home. Before this, a solo crew could be seized at 05:00 dragging a full net.
- **Results** (`--trawl-sim lagoon 3 <crew> careful 3`): six hands met 2 of 3 deadlines (about 203 a night); solo met 3 of 3.
- **Not done yet:**
  - Consignments: they need wrecks and landings.
  - The Pier Wheel and Glimmer variants.
  - The Killscore and cooking factors in the value (steps 2 and 5).
- Shots: `trawl_scales`.

## Step 2: the deck kill and the Killscore (2026-10-01)
- **Hit points:** every fish of 1 kg or more comes aboard alive with HP = 8 + 6 x kg^0.75 (`DeckFishHP`). Smaller fish die on landing. Set up the moment a fish is aboard, in `StepDeckFish`, so every landing site gets it.
- **Blows:**
  - Melee (`KillDeckFish`): the priest does 14 and counts as a headshot, the knife 11, the gaff 9, bare hands 5.
  - Rounds, pellets and spears: their own damage. A pass through the front fifth of the fish is a headshot.
  - Everything goes through `HitDeckFish`.
- **The Killscore:** only the finishing blow counts. The bonuses multiply up to 4x:
  - melee 1.2, one-hit 1.5, headshot 1.25, airborne 1.3 (a flop throws it 0.45 s), long shot 1.3 (over 25 m), heavy seas 1.15 (past 15 deg), in the dark 1.2.
  - An explosive overkill (more than twice the remaining HP) voids the score and recovers only 40% of the weight.
  - The Killscore multiplies the fish's value at the market and at the scales.
  - The score pops up over the fish in both views (`DrawDeckFx`).
- **Deck behaviours** (`DeckBehaviourOf`, by name):
  - flopper (the flop);
  - thrasher (20 kg and over: a tail slap knocks hands flat);
  - biter (barracuda, moray, sharks: bites within reach);
  - spearer (a bill lunge, a serious injury, every 6-10 s);
  - grabber (an octopus or squid of 1.5 kg or more grabs a hand, inks them for 3 s, and drags them toward the rail and over);
  - pincher (crabs, lobster: a pinched hand);
  - stinger (triggerfish, jellies, rays: stings a bare hand).
- **Blood on deck:** 1 a hit, 3 for pellets and explosives, half for a headshot kill (`Gannet::deckBlood`). It drains through the scuppers into the sea at 20% a second, and is drawn top-down as streaks to the low side.
- **Bots:**
  - A bot clubs any dangerous landed fish within 1.6 m (self-defence).
  - The table's bot uses the table's priest.
  - Fixed a latent bug: a fallen bot never got up (it skipped `Move`, where a hand rises). A thrashing fish or a wet-deck fall left a bot down for the rest of the night.
- **Tests:** `--trawl-gear-test` covers the HP table, two priest blows on a 4 kg snapper (x1.50), a one-hit long headshot, an airborne club, an overkill, the value multiplier, blood draining, a barracuda biting, an octopus dragging a hand overboard, and the Lagoon's behaviours.
- **Sim:** `--trawl-sim lagoon 3 6 careful 3`: 3 of 3 deadlines, 272 a night, 0 deaths. `DEPTH_NODECKACT=1` turns the behaviours off for diagnosis. The sim's trace prints the winch hand's state every 20 s.

## Step 3: weapons and the Gunsmith (2026-10-01)
- **The catalogue is data:** `data/trawl/weapons.tsv` (41 weapons from the doc's pages 29-31) and `data/trawl/attachments.tsv` (18), loaded by `trawl_weapons.cpp`.
- **In a slot:** a catalogue weapon is `Item::Weapon` (`Slot::wpn`, `lvl`, `att[3]`, `ammo`, `spare`). The older items keep their behaviour.
  - Melee weapons hit deck fish with their damage and reach. The coral club's headshots do 1.5x.
  - Guns fire their projectiles with damage x1.15 per upgrade. Rapid guns fire while held. The nitro express's recoil pushes the shooter back; the punt gun knocks them down.
  - **Wet powder:** cartridge guns misfire 10% in rain, 25% in a squall, 40% in a storm, unless they have an oilskin breech.
  - A shot aimed at the deck flies at deck height, so a fish on the planks can be shot. Before, the shot aimed at the sea beneath and passed under it.
- **Carrying and ammunition:** a hand holds the loaded magazine plus one spare reload (R loads it). The spare refills from the ship's magazine stock (`Gannet::ammo*`) while the hand stands at the deck locker. The fo'c'sle magazine locker comes with below decks.
- **The Gunsmith** (`DockKind::Gunsmith` on the quay, -7.6, -8.2):
  - It sells its own rows; the nitro express from deadline 3.
  - A hand carries one long weapon at most.
  - Three damage upgrades per gun (60/150/300, doubled for the carbine, chatter gun and long rifle).
  - Its own attachments, fitted by the doc's table.
  - Ammunition packs into the locker.
  - Thrown weapons stay off sale until throwing exists.
  - Commands: `CMD_GUN_BUY/UPGRADE/ATTACH/AMMO`. Everything is in the snapshot.
- **Drawing:** held catalogue weapons are drawn as their nearest kind (`DrawItemOf`). The HUD names them (`SlotName`).
- **Tests** (`--trawl-gear-test`): the catalogue loads; buying; the third-deadline rule; the one-long-weapon rule; upgrade price and damage; attachment fit rules; a revolver killing a deck fish; reloads from the spare and restocking at the locker; ammo packs; a 25% squall misfire rate.
- Shot: `trawl_gunsmith`.
- **Not yet:**
  - Trader-only weapons (they come with the landings).
  - Throwing; the bayonet's melee.
  - The lodestone and bone-stock effects.
  - Misfires after a swim.
  - Two-slot weapons blocking a second slot.

## Step 4, first part: the catch crates and the birds' rule (2026-10-01)
- **The six lidded catch crates** on the aft deck: E beside a dead fish swings it into a crate (`Gannet::CrateFish`, `CatchRec::crated`). It's safe from birds, still waits to be gutted, and is hidden in both views.
- **The birds' rule** (doc v2, "Birds and the catch crates"): a gull flock over the deck takes only a dead fish left out (not crated, not gutted, not alive), the heaviest it can lift (3 kg), every 4 s (`EcoTick`).
- **Tests:** `--trawl-gear-test` checks that crating works and that the gulls take the dead fish but not a crated or a live one. The older gull test is updated to the rule.
- **Sim:** `--trawl-sim lagoon 3 6 careful 2`: 2 of 2 deadlines, 302 a night.
- **Left of step 4:**
  - The brown pelican (5 kg, 12 sh) and the frigatebird (2 kg, 15 sh, harries other birds) as Lagoon species.
  - A shot bird drops its stolen fish where it falls and is itself a catch.
  - The gutting table unattended for 10 s counts as "left out".
  - Bots crating loose dead fish when idle.
  - Junk (a message in a bottle, a brass key, torn chart pieces) from net hauls and the sea, kept for the landings.

## Step 4, second part: the thieves, the bigger birds, junk (2026-10-01)
- **Brown pelican** (lifts 5 kg, 12 sh) and **frigatebird** (lifts 2 kg, 15 sh) are Lagoon species (`trawl_species.json`, `lifts`). They follow a gull flock in (35% / 25% per arrival).
- A bird over her takes the heaviest dead, uncrated, ungutted fish the birds there can lift (the lightest bird able to lift it does the taking), every 4 s. It becomes a **`Gannet::Thief`** climbing away over the near rail with the fish hanging under it (drawn in both views, in the snapshot).
- **Shoot it down** (`DropThief`): bird and fish fall on her deck if over it, otherwise afloat (gaff or tether). The bird is a catch (`BirdOf`: gull 4, pelican 12, frigatebird 15) and always Airborne (x1.3 Killscore).
- **A frigatebird harries** any other thief until it drops its fish mid-air (`DropFish`).
- The "gulls give up" rule only applies to flocks, so a lone pelican isn't driven off by one pellet.
- **Bots crate the catch** when birds are over her: the gutting-table hand swings dead fish into the crates (`CrateFish(i, 12)`).
- The doc's "gutting table left unattended 10 s" is already covered: every dead fish on the deck is fair game unless it's crated or gutted.
- **Junk** (`FindJunk`, `D().junkPerHaul` 25% a net haul): a message in a bottle, a brass key or a torn chart piece. They're kept on the Gannet (`junkBottles/Keys/Charts`, in the snapshot, on the HUD) for the landings in step 5.
- Tests in `--trawl-gear-test`: a pelican takes a 4.5 kg fish, a round brings it down onto the deck worth 15.6 sh, and a frigatebird makes a gull drop its fish. Sim (6 careful hands): 282 sh a night, every deadline met.
- **Junk, to the doc's full table** (pages 25-27; `JunkTable()` in trawl_gear.cpp, with ground bits for later grounds):
  - a cast reeled home empty after fishing a while brings up junk 10% of the time (`D().junkPerCast`, `CastJunk`), and a net haul brings one to three pieces;
  - sellable junk is a stowed `CatchRec` with `junk` set: the market pays its flat value (no freshness, no glut), and the quota scales never take it;
  - someone's lobster trap holds 1-3 crabs or lobsters, and an old boot sometimes holds a crab;
  - bottles, keys and chart pieces are counters for the landings.
  - The oil lantern, the rusty knife and the Gunsmith's cleaning (the pistol, the watch) only sell for now.
  - The weights (how often each piece comes up) are my call.
  - Checked in `--trawl-session-test`. Sim: 341 sh a night.

## Step 5: the skiff and the Atoll (2026-10-01; design doc v2, "The skiff", "Skiff destinations", "Islands", "Cooking")
- **The skiff** (`trawl_skiff.cpp`, `Skiff`, `DECK_SKIFF`):
  - The davit station at the stern: hold left mouse 8 s to lower her, or 10 s to haul her up (alongside, the Gannet stopped). A pause keeps the progress; the button has to be let go between the two jobs.
  - Space beside the davit drops into her; E climbs back up the stern ladder.
  - The oars are the two mouse buttons (left port, right starboard, both together pull straight). On the beat she makes 1.5 m/s with one rower and 2.2 with two; a bot aboard pulls on the human's beat. A stroke within 0.32 s of the last catches a crab: she stops for a second. Every stroke writes noise into the water.
  - She rolls with the sea's slope across her beam and capsizes past 25 deg: everyone goes in and her fish float off. A swimmer beside her rights her by holding left mouse for 4 s (my call), then E climbs in.
  - One section of 40; at 0 she goes down. She carries 150 kg. Her bow lantern (6 m) shines into the web and both views.
  - Left behind: anyone not aboard the Gannet at the harbour line is dead, body lost, fined 25. A skiff left out is towed in for 40 (my call).
- **The Atoll** (`trawl_landing.cpp`, `Landing`, `DECK_SHORE`):
  - An islet of 13 m sand in the Lagoon basin, out toward the crest (`Eco::landingAt`), hashed from the chart seed.
  - E near it in the skiff runs her up on the sand and steps ashore; the first stroke shoves her off again. Ashore you walk with WASD (in the Gannet's frame, as on screen) and carry one thing at a time.
  - **The fire pit** cooks to the doc's curve: 1.0x to 1.5x over 10 s + 1 s a kg, held 5 s, burnt to 0.3x over 5 more. Rain puts the open fire out; relighting takes 10 s standing by it. Cooked fish keep (no freshness loss), and their cook multiplier counts in the market and at the scales. R eats a cooked fish of 2 kg or more to mend a minor injury.
  - **Caches:** the sloop's strongbox (locked: a brass key from junk), plus either a sea chest under the palms or a buried chest. A bottle's map or three chart pieces mark the buried one with an X; it takes 5 s of digging. Chests are 12-38 kg of salvage worth 50-200, carried to the skiff.
  - **Residents:** crabs on the beach (E catches one, sometimes with a pinch); a moray in the little lagoon that bites a wading hand (then 15 s quiet).
  - **The elder** takes fish at 150% of their value as trade credit, for his goods: the coral club, the shark-tooth blade, the tribal longbow and feather fletching. He's closed to a crew that refused the canoes (`foughtCanoes`). His panel opens with E (CMD_ELDER_GIVE / CMD_ELDER_BUY).
  - **Birds and smoke:** cooking smoke and fish on the beach or in a laden skiff draw the birds (`Eco::birdDraw`), which steal from the beach, the fire and the skiff (`StealFrom`).
  - **Life round the skiff:** the web keeps a second bubble of life round the skiff once she's 60 m+ from the Gannet (`Eco::skiffOn`), so the Atoll and the skiff's water aren't empty.
- **Drawing:**
  - Top-down: the skiff's hull, oars on the stroke and lantern. The Atoll: sand lit a square metre at a time by the fire, palms, the lagoon, the sloop, the hut and the elder, caches, crabs, fire and smoke. The view slides off the Gannet to follow you out.
  - First person: a built clinker skiff (seated eye, rowing arms and oars). A smooth islet with palms, the hut, the sloop and the elder. Flames and smoke are drawn over the frame, because the ink pass boxes small cubes.
- **Tests and shots:** `depth.exe --trawl-skiff-test`; shots `trawl_skiff`, `trawl_davit`, `trawl_atoll`, `trawl3d_skiff`, `trawl3d_davit`, `trawl3d_atoll`.
- **The eco test's lantern ratio** now averages eight seeds and allows up to 5x: the Atoll's reef ring moved it (3.4x-4.6x).
- **Next (5d):**
  - the skiff-only marks (the Crest Pass and the Sargassum Line, twice the bite rate);
  - fishing from the skiff;
  - predators that prefer the smallest vessel with the most blood;
  - the sonar showing the skiff as a bright blip;
  - bots in the skiff.
- **5d (`ae3e2d1`): skiff water, her line, and the dangers.**
  - **The marks** (`Eco::marks`, `MarkAt`):
    - The Crest Pass: a coral maze through the crest, 1.2 m deep, where the Gannet grounds.
    - The Sargassum Line: a weed bank toward the crest; a turning screw in it fouls and she keeps about 40% of her way.
    - A skiff line inside either mark gets twice the bite rate. The sim skipper avoids both.
  - **The skiff's line** (`Gannet::skiffRod`, `StepSkiffRod`; trawl_fish.cpp):
    - T in the skiff switches between the oars and the line. The line has the same controls and reel gauge as a rod station.
    - Under 30 kg lands in her bottom boards, killed at the waterline; a thrasher of 10 kg or more rocks her hard.
    - Bigger fish are killed alongside onto the tow line (`Gannet::towed`). The tow drags at her (48 kg: 1.16 against 1.52 m/s), bleeds into the water, comes aboard when she's hauled up, and parts if she capsizes.
    - A hooked fish tows the skiff and heels her.
  - **Sharks ram the skiff first:** a hungry rammer within 4 m of her, in 60% of the blood the Gannet would need, heels her hard and takes up to 14 of her 40.
  - **Finding her:** the sonar shows the skiff as a bright pulsing blip, with the marks as dashed rings. The helm chart shows the Atoll, the marks and the skiff. Mark arrows point from wherever you are.
  - **Bots:** a bot following you (F) walks to the davit and drops in after you, rows on your beat, keeps her while you're ashore, and comes back up with you. A bot in the water climbs into a nearby skiff.
  - **The shakedown** now counts fish landed (`landedSmall`/`landedBig`), not fish still in the hold: the birds steal the small dead ones.
  - **Not built:**
    - walkies and proximity voice (no voice chat yet), the flares/bell kit, the skiff's anchor;
    - the Slipway's steam launch kit (500: 3 m/s, noise 4) and the skiff upgrades (outrigger, painted eyes);
    - junk on the skiff's line;
    - the Old Lighthouse rock and the Sandbar landings.

## Step 6: mini-bosses, boss lures, harbour requests, charms (2026-10-01; design doc v2, pages 47-48 and 70-71)
- **Mini-bosses** (`trawl_quest.cpp`, `MiniBosses()`): the Lagoon's two both live in the Crest Pass coral.
  - Old Snapjaw: a giant moray, 30 kg, a biter, Cover then Run. Worth a flat 180; drops a jaw full of old hooks.
  - The Crest Grouper: 70 kg, a thrasher, Dive then Cover. Worth 240; drops a barnacled brass lure.
- **Boss lures:** 60 at the Chandler, half price if anyone wears the brass lure.
  - R on the skiff's line arms one on the heaviest rod aboard.
  - Cast into boss water (the Crest Pass), it spends the lure, adds 10 to the Wake, and 6-12 s later that deadline's next mini-boss takes it (`StepBoss`).
  - Outside boss water nothing answers and the lure stays on.
  - A mini-boss is worth its flat value times the Killscore and cooking (`CatchRec::boss`; never spoils overnight). Its drop goes to `Gannet::drops`.
- **Charms** (one a hand, `Crew::charm`; lost with a body lost at sea):
  - the lucky coin (Chandler 50): Glimmer variants twice as likely;
  - the shark tooth (the elder, 120): +0.1 Killscore on a melee finish;
  - the tribal anklet (the elder, 90): +8 s before drowning;
  - the old hooks (Snapjaw's drop): the line can't snap for its first 15 s (`Fight::noSnapUntil`), my reading of "never breaks on a fish's first run";
  - the brass lure (the Grouper's drop): boss lures cost half.
- **Glimmer variants:** 2% of landed hook fish (`OnLanded`), worth 3x.
- **Harbour requests** (`Session::requests`, rolled each deadline; the chalkboard panel, CMD_REQUEST):
  - The cook: a named species cooked to 1.5x, for 3x its value and the spice rub (cooking 25% faster for the deadline).
  - The naturalist: a protected catch returned with the Chandler's tag gun (30), for a rare-fish lure (bites x1.3 for a night, used at cast-off).
  - The collector: a named Glimmer, for 3 tokens and 100. The doc's Pier Wheel and cosmetic don't exist yet.
  - The gunsmith's apprentice: three kills at 2.5x or better in one night, for a free attachment at the Gunsmith.
  - Mother Carey: a mini-boss drop, for a legend lure (`legendLures`; the legendary fish are still to be designed).
  - Drops can be worn instead (CMD_WEAR_DROP).
- **Tests and shot:** `depth.exe --trawl-quest-test` (25 checks); shot `trawl_chalkboard`. Sim (6 careful hands): every deadline met, about 220 sh a night.

## Step 7: below decks (2026-10-01; design doc v2, pages 16-18; `trawl_below.cpp`)
- **Deck 1 now has three spaces:**
  - the engine room (down the aft ladder; a hand taking the ladder lands at its foot);
  - the fish hold (x -2.9..0.8), through the main hatch at (-1, -1) or the watertight door (E at it; shut, the hold ends at the bulkhead);
  - the fo'c'sle (x 5.2..9), through the fore hatch at (7.6, 0). It has the magazine locker (`StationKind::Magazine`: ammunition restocks here now, not at the deck locker) and the Medic's cot (`StationKind::Cot`: 10 s on it with the Medic within 2.5 m sets a broken arm or works out a hook).
- **Hatches** (`Gannet::hatches`): open, shut or battened. E goes through an open hatch, or opens a shut one in 4 s. R on deck shuts one, then battens it in 10 s; a battened hatch can't be opened from below. Rolling past 25 deg, every open hatch ships 30 kg of water a second.
- **Oil lamps** (one a space): out past 20 deg of roll, 3 s to relight (E).
- **The fire** from a blown relief valve burns until smothered (E within 1.8 m of the firebox, 3 s). Left 30 s, it reaches the coal bunker: half the coal and 15 off the stern plating. It burns whoever stands in it.
- **Bilge eels:** with the bilge past 2,000 kg they bite whoever is below.
- **The skiff:** with nobody at the davit, Space in the skiff alongside hooks her on from the water (25 s, the Gannet stopped) (`FinishRecovery`).
- **Tests:** `depth.exe --trawl-below-test` (15 checks).
- **Still to do for step 7:** drawing the hold, the fo'c'sle, the hatches, the door and the lamps in both views; the HUD's crew board (who is below, in the skiff, ashore); and the bots' use of the magazine.
- **Step 7, drawn:**
  - Top-down: hatch covers on deck (open, shut, battened); below, the hold (ice pounds, the iced catch, the door with its dog), the fo'c'sle (bunks, the magazine locker, the cot) and the lamps. A space whose lamp is out goes black, and lit lamps are real lights.
  - First person: the hold and fo'c'sle built into the boat model (floors, walls, a doorway in the bulkhead, ladders under both hatches, ice pounds, bunks, the cot, the magazine), the door leaf when shut, lamp glows and point lights, hatch coamings and covers on deck.
  - The crew list is the crew board: [engine]/[hold]/[fo'c'sle]/[skiff]/[ashore].

## Step 8, first part: the Weeds (2026-10-01; design doc v2, pages 38-40 and 43-44; `docs/trawl_rosters_weeds_grotto.md`)
- **The ground** (`Eco::BuildWeedsChart`): the same island and quay; a clear apron off the island, then the kelp forest (golden canopy, north-south lanes), urchin barrens, rock pinnacles (a net that crosses one snags), the seaward edge shelving to 60 m. Chosen at the chart table (`CMD_GROUND`, `Session::SetGround`); coal to reach it 25 kg (`CoalToReach`).
- **The roster:** twenty Weeds species plus the Great White in `trawl_species.json` (the `weeds` block), tuned so the ground holds with no crew (`--trawl-eco weeds 27`).
- **Signatures:** under way in the canopy the screw fouls (a chance every second, about once in 15 s; half her way until a swimmer at the stern cuts it free, 4 s, the screw stopped); a net through the canopy fills with kelp that weighs like fish and is worth nothing; the otter raft (shooting near it: an 80 sh fine).
- **The threats** (`trawl_weeds.cpp`, `Gannet::StepWeeds`; my design calls, the doc gives one line each):
  - *Sirens*: on rocks 55-85 m off, singing 40 s; the wheel pulls toward her (a third of a turn a second with a hand at the helm, who must steer against it; three times that with nobody there). A flare within 40 m, a shot within 5 m or the searchlight inside 45 m drives her off; reaching her (12 m) is 25 off a bow section.
  - *Kelp Wraiths*: a hand at the rail while she lies in or by the canopy is taken by the ankle (held still; 8 s, then over the side). E beside them, or their own knife, cuts them free; a bot cuts itself free in 4 s, and the nearest free bot runs to cut a tangled hand loose (bot task 4). The kelp crown keeps them off.
  - *Feral Mermen*: at a net towed near the kelp, splashing at the cod end for 12 s; a shot, a flare or the searchlight on it sends them off; otherwise the cod end is slit and the catch spills (blood in the water).
  - Drawn in both views (the Siren pale on her rock with her song spreading, splashes and a pale arm at the cod end, kelp wound round a tangled hand with a countdown), HUD lines, and sound (her wordless falling line, splashing, the wet drag of the kelp).
- **The landings** (`LandingKind`: `LK_SEALROCK`, `LK_CANNERY`; the Weeds chart carves both):
  - *Seal Rock*: a bare islet near the seaward edge. The sealers' hut (stove at its door: rain doesn't reach it), Old Hoskins (buys birds only, twice their value, in shillings), the seals' haul-out with a bull seal that bites, a sea chest in the hut and a buried tin trunk (100-350).
  - *The Cannery Pier*: a round loading stage on pilings off the island. The cannery shed (the safe inside: locked, 150-350, a brass key), the boiler (cooking, sheltered), the last foreman (cooked fish only, 150%, in shillings), and Kelp Wraiths in the pilings: a hand at the stage's edge is taken and pulled off into the water.
- **The variants** (doc v2 page 40; the Weeds roll Bait run 8%, Red tide 6%, Tuna run 8%, Kelp storm 8%, Mermen's market 6%):
  - *Tuna run*: from 22:00 the bluefin come along the edge (x4), and the Great White's share rises with them (x1.6, standing in for "Wake 30 instead of 40").
  - *Kelp storm*: 14 extra drift mats (they foul the screw too), fouling twice as often, yellowtail x2.5. The extra mats are gone the next night.
  - *Mermen's market*: the mermen come alongside once (the canoe's visit: singing instead of drums); a fifth of the hold, heaviest first, buys abalone worth a third more and a drowned relic (60-160); waved off, they go quietly. They leave the nets alone that night.
- **A grounding fix for every ground:** aground, motion toward deeper water is no longer pushed back, so backing off frees her (before, backing pushed her further onto the island and she stayed there).
- **Tests:** `depth.exe --trawl-weeds-test` (26 checks: every threat, the bot rescue, the crown, both landings and traders, the safe, the pier's wraiths, the market). Shots `trawl_weeds_threats`, `trawl3d_weeds_threats`.
- **The sim, later in the day:** marks east of the apron (x > 130), kelp fills a net at a third of the old rate (about 5 kg a second through the canopy), more halibut, bat rays, vermilion and yellowtail in open water. Still well under the doc's 65% (one deadline in eight at 130-150 sh a night): the Weeds' money is in the kelp and on the rods, and the sim's skipper is a net fisherman. Open.
- **The sim** (`--trawl-sim weeds 3 6 careful N`): the skipper now buys coal first, keeps enough market money for the next night, turns home earlier on the Weeds (2.6 m/s and 45 minutes in hand), answers mermen at the net with a spear or a flare, keeps marks and courses 30 m off the landings, gives up a mark after three groundings, and lays tow legs only over 12 m of clear water 6 m either side.

## Step 8, second part: the Grotto (2026-10-01; design doc v2, pages 38-41, 44-45, 48-50, 61; `trawl_grotto.cpp`)
- **The ground** (`Eco::BuildGrottoChart`): the same island and quay; open water to a headland, pierced by a sea arch (8 m of water), into a cave 300 x 470 m: black water 18-42 m deep, mould ledges round the walls (`H_WALL`, glowing), five smugglers' wrecks (`H_WRECK`), seven stalagmite pillars, the Smugglers' Shelf on the north wall and Bone Beach on the south. Skiff marks: the Side Galleries (Lanternjaw's boss water) and the Still Pool (the Pale Abbot's). Coal 40 kg.
- **The roster:** the doc's 17 species and the Lantern Angler (a threat species), the web running on the algae resource as the mould (`--trawl-eco grotto 27`: stable).
- **The arch closes** with the tide at a time posted on the telegraph at cast off (02:30-04:00, `Session::archCloseAt`): after it, the arch has no water under her keel (`Eco::archOpen`, `InArch`) until 05:00, so a crew still inside meets the customs cutter (counted late). It never closes on her while she's in it.
- **The echo:** every noise in the Grotto feeds the sound layer at twice its value, and a loud one inside the cave (30 or more: a gunshot, a charge) can bring a stalactite down within 6 m of it (`Eco::rockfalls`): on her, 12 off a section, and a hand underneath gets a broken arm.
- **The threats** (my design calls from the doc's threat table):
  - *Lantern Angler*: a second light 12-20 m off one side while her lantern burns; after 6 s it takes the nearest hand on that side and walks them to the rail (0.8 m/s) and over. E beside them shakes them out of it; a flare within 20 m blinds it; hooding the lantern sends it away.
  - *Ghost Worm*: vibration in the cave (a fighting line, the net, the screw; halved with the lantern low) wakes it; it circles 15 s (the lines hum), then every 10 s bites through a fighting line, takes the net, or coils the hull (she stops dead, 6 off a section every 4 s). Quiet for 8 s and it sinks away; six hits on its head (a harpoon counts three) kill it.
  - *Isopods*: ungutted fish or blood on the deck for 20 s, clicking on the anchor chain, then 30 aboard: they strip an ungutted fish every 6 s and bite ankles. A clean deck starves them off; a stamp (any melee blow) kills five.
  - *Drowned sailors*: within 40 m of a wreck with the lantern lit, 15 s of knocking on the hull, then one over the rail: it walks at the nearest hand (0.6 m/s), grabs, and drags them to the rail (0.4 m/s) and over. 40 hp; a blow of 12 or more breaks its grip; bots fight one in reach.
- **The landings:** the Smugglers' Shelf (the quartermaster buys salvage at its full value in shillings and sells the smugglers' goods for shillings: air pistol, blunderbuss, air rifle, lodestone sight; 2-3 stashes 200-600, one locked) and Bone Beach (volcanic vents: cooking 30% slower, never burns, never rained out; the hermit trades his gear (the galvanic prod, the bone stock) for bones and skulls; bone piles on the sand and a lost crew's kit 200-500).
- **Mini-bosses:** the Pale Abbot (a giant white conger, 60 kg, grabber, 700, the white skull: Anglers' lures don't work on the wearer) and Lanternjaw (an old cave anglerfish, 45 kg, biter, 800, the glass lantern: a light the wearer carries draws nothing). Boss lures 200.
- **Variants:** glass eel run 6% (eels x4, pale cod x2, anglers come twice as often), rockfall 8% (stalactites at half the noise; the new chamber's untouched wreck waits for diving), mould bloom 8% (everything bites 1.6x, no anglers, the Drowned find her anywhere in the cave).
- **Drawn:** top-down, the mould glows beyond the lamp, the cave's rock, wreck timbers, the landings, the four threats; first person, the cave walls rising to a roof hung with stalactites, the mould as its own glowing model, no sky under the roof. Shots `trawl_grotto`, `trawl3d_grotto`.
- **Tests:** `depth.exe --trawl-grotto-test` (33 checks).
- **The sim** routes through the arch (`Via`, `RouteClear`) and leaves before it closes: 2 of 4 deadlines (50%, the doc's 45%) at 200 sh a night, 0.19 deaths a night.

## Step 8, fourth part: diving (2026-10-01; design doc v2, pages 55-57; `trawl_wreck.*`, `trawl_dive.cpp`)
- **Wrecks** (`GenerateWreck`, `CheckWreck`, `GroundWrecks`): the doc's five types (sloop, whaler, smuggler hull, galleon, Atlantean terrace) with its depths, room counts, salvage budgets and locked cabins; decks cut into rooms on a 4 x 3 m grid joined by doors, hatches and (30%) a squeeze to the richest room; breaches torn until every room is within 40 m of hose; an air pocket per five rooms; 40% of the value behind locks; residents from the ground's roster (Drowned and Ghost Worm hatchlings in the Grotto and Atlantis, cursed idols in Atlantis). 30% lie on their side (a flag only so far). `depth.exe --trawl-wreck <seed> [type] [count]` prints and checks them (about 16% fail a first draw and are redrawn; every ground's set passes). `Session::PlaceWrecks` sets each deadline's wrecks where the chart's depth suits them.
- **The dive** (`Gannet::dive`, `DECK_DIVE`): the hardhat suit (Chandler, 350); T at the stern with her lying still within 15 m of a wreck goes down at 1 m/s (a bell wreck says it needs the bell: not built). The air pump is a station: a stroke every half second at most adds 0.1 to the gauge, which falls 0.08 a second; out of the green the helmet holds 30 s. Drifting more than 15 m off fouls the hose. Below: Space plus a direction moves through the nearest door/hatch/squeeze (no salvage through a squeeze); E lifts an item (a locked cabin wants a crowbar or a Diver), and at a breach puts it in the basket (into the hold: junk, CS_DIVE); R is the two tugs. Faster than 1.2 m/s up: the bends. Cursed idols: +10 Wake and the Eye blinks.
- **The view:** the diver sees the wreck in section, lit round the helmet; the deck sees the gauge and the air. Shot `trawl_dive`. **Tests:** `depth.exe --trawl-dive-test` (11 checks).
- **Since then (same day):** the diving bell (Slipway, 1,200: two divers to 120 m, its own air at the breach, no pump); residents that act; the salvage basket on its own line (`basketLine`, landed after `depth / 1.5` s); two-diver lifts; the Rockfall chamber and the Eye-open relics; and **the side-view dive scene** (`trawl_divescene.cpp`): the wreck carved into tiles (`'#'` timber, `'l'` ladders; a room is 6 x 5 tiles) and walked with the parkour levels' own movement (`PlatStepPlayer`, the Cave's water physics). Walking into another room asks the host to move there (`HI_ORDER`), so the room rules (doors, squeezes, locks, silt, the hose's reach) stay in `DiveMove`. The air hose (or the bell's lifeline) is a Verlet rope from the breach to the helmet, kept out of the timbers; a run of hose past 32 m can snag on the way into a room (4 s held). The deck hides its HUD while you're down. Tests `--trawl-dive-test`, `--trawl-divescene-test`; shot `trawl_dive`.
- **Not yet:** the sonar showing wrecks, the sim using dives.

## Step 8, third part: Atlantis Waters (2026-10-01; design doc v2, pages 38, 41, 46-50, 58, 61; `trawl_atlantis.cpp`; roster notes `docs/trawl_roster_atlantis.md`)
- **The ground** (`Eco::BuildAtlantisChart`): the island and quay; a shelf off it; the drowned city's terraces stepping down from 30 m (sunken gardens on `H_REEF`, broken tower tops); the slope through deep coral (`H_HOLES`) to 200 m; the Trench's edge to 400 m, where the Pale Eye lies (`Eco::eyeP`). Marks: the Terrace Rim, the Veil Drift (Broadbill's boss water), the Whale Road (Old Red's). Coal 80 kg.
- **The roster:** the doc's 20 species (the sperm whale and the sunfish never bite), with the great white and the lantern angler; `--trawl-eco atlantis 27` is stable.
- **The landings:** the Drowned Stair (the eternal brazier: twice as fast, never out, +2 Wake a fish; the Keeper of the Stair takes fish as offerings, and a bowl (400-1200) opens only for 100 of them), the Watchtower stump (no trader; lighting its signal fire shows every skiff mark on the sonar for 4 minutes and adds 15 Wake; the tower cache 300-800, locked), the Cult Landing (the bonfire 1.5x; the cult quartermaster sells dark goods (the obsidian dagger) for shillings; taking the cult's hoard (500-1200) brings the longboats).
- **The threats** (`Gannet::StepAtlantis`; my calls from the doc's table):
  - *The Pale Eye*: while the searchlight is swung toward it (within 0.35 rad, 260 m) or a ping is open within 120 m of it, the Wake rises twice as fast (`Eco::wakeMul`). It blinks when the Ghost Ship or the Kraken is coming.
  - *The Deep Choir*: Wake 25+: 30 s of song; every hand on deck off station walks to the nearest rail (0.5 m/s) and over. A ring of the bell (`bellT`, now a real ring with a 1.2 s cooldown) breaks it for 12 s; the singer surfaces 20-30 m off for 4 s in every 10 and a shot within 5 m ends it.
  - *A Cult longboat*: Wake 15+ (or the hoard taken, or the Procession): circles at 30 m chumming blood every 4 s. Three hits on the rowers, or 25 s above 3 m/s (and nobody looking at the Eye), or 2 minutes, and it goes.
  - *The Ghost Ship*: Wake 65, or the bell rung more than six times: a bell answers, 30 s later it's alongside, three Drowned board (the Grotto's `StepDrowned`, shared) and the set gear is gone. 25 s above 3.5 m/s before it reaches her and she outruns it; put the boarders down and it leaves a relic chest (300-700). Once a night.
  - *The Kraken*: Wake 90: 20 s of the Glass (the sonar whites out), then an arm every 6 s: a hand over the side (not one with the beak pendant), the net torn away, or 20 off a section. A harpoon near her makes the next arm let go; the net in, the lantern out and full steam for 15 s and it lets her go; else 90 s. Once a night.
- **Mini-bosses:** Broadbill (400 kg, spearer, 2000, the inscribed bill: spearers can't hurt the wearer) and Old Red (250 kg, grabber, 2500, the beak pendant: grabbers can't hold the wearer). Boss lures 400.
- **Variants:** the Eye wide open 8% (`wakeDrift` 1.5 a minute; the terraces' glowing relics wait for diving), swordfish night 8% (swordfish x4, the sperm whale and giant squid x3), procession 6% (longboats from the start).
- **Fixed for every ground:** a bot at the net winch no longer re-shoots the net after the skipper turns for home (`Gannet::netLast`); the empty hauls it caused were behind many of the sim's seizures.
- **Drawn** in both views; shots `trawl_atlantis`, `trawl3d_atlantis`. **Tests:** `depth.exe --trawl-atlantis-test` (20 checks).
- **Sims after tuning** (Able bots, careful, 3 hands... 6 hands): Lagoon 83% (276 sh), Weeds 38% (175 sh), Grotto 60% (222 sh), Atlantis 83% at 586 sh with 0.89 deaths a night. Atlantis's threats now also come on the night's Stir (Choir 0.45, longboat 0.35, Ghost Ship 0.7, Kraken 0.85) and the bell calms the Choir only 6 s. Atlantis is still far too rich for the shared quota (the doc's 25%): open, a design question (a quota per ground, or fewer big fish).
- **Open:** the telegraph's idle rumours name Lagoon species on every ground; the sim's Atlantis numbers aren't in yet (the first run fished the island's shelf and was seized; the skipper now fishes 20-90 m east of x 150).

## Step 8, fifth part: the Weeds' balance, the ground hardening, spray (2026-10-01)
- **Bot anglers read the sounder** (Able and Old Hand): every 6 s with the line out and nothing biting, the lure goes to the depth of the most valuable fish its tackle takes within 16 m (price x kg x school^0.3 x hunger), clear of the floor. Before this every bot fished at the rod's default depth, so the Weeds' kelp bass, sheephead and halibut (20-25 m down) never saw a bait. Green hands leave the counter alone.
- **Bots rig a rod over a handline** when the rack holds one (the medium if she has it). With the starting kit only the port rod was a rod and the rest handlines, which take only kelp perch and anchovy.
- **An idle angler steps to an empty gutting table** when a fish is flopping on deck and goes back to a rod once the deck is clear (with six hands nobody stood at the table, and kelp bass flopped back over the side).
- **The sim's skipper** (trawl_sim.cpp): on the Weeds, a course round the canopy (A* on an 8 m grid out of kelp, shoals and skiff water, pulled straight where a leg is clear), kelp-edge marks fished on the rods (8-14 m off the canopy, scored on both sides of the edge), a snagged tow leg given up for the nearest edge mark, a medium rod bought past the Lagoon, and an hour in hand for the way home. `DEPTH_TRACE_AGENTS=1` (with `DEPTH_TRACE`) lists what swims within 30 m each minute and where the lures sit.
- **Sims** (careful, 6 hands, Able): the Weeds 38-50% across four 16-deadline runs (about 45%), 255 sh a night, hook 16-20% (was 38% at 164 sh, hook 8-11%); seizures down from 5 in 24 nights to 2 in 48 (the course planner crosses kelp and drift mats only at 12x cost, so a boat already in the canopy takes the shortest way out). The Lagoon 92% of 12 at 290 sh (was 83%). **Open:** the Weeds are still short of the doc's 65%. The remaining failures are nights whose marks pay 20-60 sh, not lost nights; the next lever is the ground's economy (rod-fish density or prices at the kelp edge), a design call for the user.
- **The ground hardens** (design doc): from the second deadline, threats get +10% hunger growth and +5% size (HP, ram damage) a deadline (`Eco::toughness`, `ThreatScale`); and once a night, late (Stir past 0.6, after 01:00), a threat from the next ground may stray in 85 m off (35% + 15% a deadline): the Lagoon a great white, the Weeds a lantern angler, the Grotto a great white (`Eco::visitor`, set by `Session::CastOff`; never in the shakedown). The session test checks the second deadline.
- **First person:** spray. Wherever the bow, the bow quarters or a rail dips under the sea with way on her, white water flies up and falls back, and the stem throws a steady bow wave at speed (read from the boat's pose and `Sea::Height`, so a guest's mirror throws the same spray; faintly self-lit so it reads at night). Shot `trawl3d_spray` warms it up (`gSprayWarm`), but from the foredeck the bulwarks hide the waterline: the spray is in the scene (about 58 drops at 5.6 m/s in a squall), the shot doesn't show it well yet. The crew figures, sky, rain and held items were already done.
