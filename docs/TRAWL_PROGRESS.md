# The Trawl: build log

Design: `The_Trawl_Reference/The Trawl â€” Arcade Game 2 Design Document (Draft).pdf` (50 pages). Numbers live in
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

## The Visual Overhaul, phase 1: the pipeline (2026-10-01; The_Trawl_Reference/The Trawl â€” Visual Overhaul Spec.pdf)
- **The user's answers to the spec's open questions:** Blender is the build-time tool (5.2.2 LTS, installed by the user); the hard outlines go, with a thin colour-tinted outline (at most 1 px, never black) as an option; the frame-rate target is this PC (Intel UHD integrated graphics).
- **Generators** (`tools/artgen/`, Python run by Blender headless; `.\tools\artgen\run.ps1 [name]`): `common.py` (bevelled boxes, bored cylinders, tubes along a path, lofted superellipse solids for stocks and fore-ends; procedural materials: blued and case-hardened steel with curvature edge wear, brass, oiled walnut with grain along the length, weathered skin reddened at the nose and cheeks; `bake_pbr` unwraps every part onto one atlas and bakes base colour, roughness, metallic, a tangent-space normal and AO, packs them and swaps in one image material; `export_glb` writes glTF 2.0 with the images embedded). The outputs are committed (`assets/shared/test/`), so the game builds without Blender. Lesson: change nothing about a baked image (not even its colour space) before it is packed, or Blender regenerates it black.
- **Test assets:** `test_carbine.py` (an 1890s-style lever carbine, 95 cm, part by part: receiver and tangs, loading gate, slotted screws, saddle ring, bored barrel with a crown, magazine tube and cap, barrel band, fore-end and cap, ramp, blade and brass bead, ladder rear sight, hammer and spur, trigger, lever loop, stock with a wrist and comb, butt plate; 2048 px set, about 3 minutes to bake); `test_head.py` (a sphere pushed into a face: brow, sockets, nose with wings and nostrils, cheekbones, lips, chin; ears, neck, eyeballs with an iris and wet clearcoat, upper lids, a hair shell, stubble; 1024 px).
- **The renderer** (shared, `redtide_render.*`): `rt::LoadAsset("shared/test/carbine_test.glb")` (cached; assets/ beside data/) and `rt::DrawPbr(model, world, tint, wrap)`: GGX metallic-roughness, normal maps through a cotangent frame from screen derivatives (no tangents needed), baked occlusion, emission, a wrap-diffuse term for skin and cloth, in linear light; lit by the same lamp, points and fog as the inked path plus a directional moon (`SceneLight::moonDir/moon/moonK`) and a hemisphere ambient (`skyAmb/seaAmb/ambK`). The ink composite takes `outline` (weight; below 1 a thinner line), `outlineTint`, `stipple` and `grain`; Red Tide keeps the defaults and looks as before.
- **The harness** (`depth.exe --shots shots/vis tvis_`): `tvis_1_helm_fog`, `tvis_2_gutting_rain`, `tvis_3_dock`, `tvis_4_carbine_rain`, `tvis_5_starboard_rod`, `tvis_6_roles` (each role front, side, back), `tvis_6_faces`, `tvis_7_guns` (the baked test carbine side and three-quarter beside the old box rifle), `tvis_7_test_head`, `tvis_8_bots`; the turnarounds are a studio stage (`DrawTrawlStudio`, `TrawlScene::studio`). The before set is kept in shots/vis_before (gitignored).
- **Phase 1's bar** (a test gun and a test head load in-game with full PBR materials) is met. Known for later phases: the head's sculpt is soft (no mouth line to speak of, a stepped hairline, ears set back) and is only a pipeline test; the carbine's steel is too worn and silver, the walnut blotchy; the outlines are still the full ink until phase 2 turns the Trawl's off.

## The Visual Overhaul, phase 3 first: the crew (2026-10-01; the user asked for characters before lighting)
- **The look is the user's reference** (a screenshot of a smooth low-poly sailor in an orange boiler suit with real hands holding a rod): smooth, chunky, rounded people with a bald egg of a head, white eyes with dark pupils, a small nose, oversized hands with three-segment fingers; not the spec's sculpted realism, never box faces.
- **Generator** `tools/artgen/crew.py` (about 5 s for all four): one skeleton (pelvis, spine, chest, neck, head, eyes; clavicle, upper arm, forearm, hand and five three-bone fingers a side; thigh, shin, foot, toe) in an A-pose; the body is one continuous mesh grown from the stick skeleton by the Skin modifier and subdivided (no seams at the joints), skinned by automatic weights; head, ears, nose, eyes and the role's pieces are rigid on their bone; soft clothes (the Bosun's bib apron, the Medic's coat skirt and satchel strap, the Diver's rolled dress) weighted automatically. Roles: Bosun (peaked cap, leather apron, rolled sleeves, patch kit), Angler (oilskins and a sou'wester long at the back, bait tin), Diver (sweater, the diving dress rolled at the waist, a lamp on a headband, a calf knife), Medic (long coat, round spectacles, red armband, satchel). Flat named materials and AO baked into the vertex colour. Outputs `assets/shared/crew/crew_<role>.glb`.
- **Renderer** (shared): GPU skinning in the physically based shader and its normal/depth twin (`boneMatrices[64]`, `uSkinned`); `rt::RigOf` (bones, parents, bind joints), `rt::RigPose` (per bone a rotation in the model's axes about its bind joint, and a scale), `rt::SolveRig` (the skinning matrices), `rt::BoneWorld`, `rt::DrawPbrSkinned(model, world, skin, recolor)`; material names read from the glb's own JSON so `Recolor{"skin", ...}` works. glTF colour factors and vertex colours are treated as linear (only textures are converted).
- **The sailors** (trawl_view3d.cpp): `LookOf(crew)` gives every hand an identity from its slot (six skin tones; the role's outfit shades, oilskins in faded yellow, tar black, oxblood or bottle green; build, height, head size), the same on every client. `PoseSailor` poses walk (stride, knee bend, hip roll, a bob), work at a station (arms to the work, a nod), carrying, seated rowing, treading water, a melee swing, breathing, blinking (every four seconds or so), the head turned or tipped, fingers curled to a grip, the hand turned on the wrist so its back is up when reaching. Every former figure case (deck, quay, shore, skiff, overboard, fallen, dead) uses it; the old box figures remain only as a fallback when the assets are missing.
- **First person:** your own sailor drawn with the camera, head hidden: two real hands before you, backs up, fingers round the grip; at a station they reach to the work; a held tool sits in the right fist, a long one with the left hand toward its fore-end; the swing, the kick and the reload drive the arms. Lesson: a rotation added to a bone's pose turns first, so the arm comes down out of the A-pose (X), then swings forward (Z), then turns in (Y).
- **Harness:** `tvis_6_roles`, `tvis_6_faces`, `tvis_8_bots` show the new crew; `tvis_9_fp_rig` is the first-person pose seen from outside (debug).
- **Second pass:** two-bone arm IK (`ArmIK`: the fist on a target, the elbow down and out) locks the hands to the rod (right up the handle, left on the reel), the helm's spokes (turning with the rudder) and, in first person, the held tool: the tool keeps its tuned swing, kick and dip and both hands follow it (the left under a long tool's fore-end, letting go through a reload); a mouth on its own bone (a thin line at rest, working while a bot barks); beards as separate small assets on the head bone, tinted by the sailor's hair colour (full, walrus moustache, mutton chops; about two in three hands have one); the Bosun's rolled sleeves end in a cuff; name tags over the other hands (a player's arcade name, a bot's period name from its slot; Kess in the shakedown), warm white, fading beyond 15 m; sea legs (each hand leans against her roll and pitch at the feet, arms out to brace past 12 degrees).
- **Not yet:** facial expressions beyond the shout (fear, strain, a grin), wet and bloodied hands and clothes, the outlines (phase 2 removes them).

## The Visual Overhaul, phase 2: lighting (2026-10-01; tuned for this PC's Intel UHD graphics, the user's target)
- **No hard ink in the Trawl:** the composite's outline weight, stipple and grain are now `SceneLight` settings; the Trawl turns the outline and stipple off and keeps a faint grain. Settings has "Thin outlines (the Trawl)" (`Settings::trawlOutline`, saved as `trawl_outline`): a 1 px line tinted slate, never black. Red Tide keeps its full ink. The brightness calibration strip was already in Settings.
- **Contact shadows:** screen-space ambient occlusion in the composite from the existing normal/depth pass (eight taps on a disc in view space, turned by a 4x4 pattern; `SceneLight::aoK/aoRadius`): boots, crates, fish and table legs sit on the deck.
- **The lamp casts shadows:** a 1024 square depth map from the lantern mast along its beam (`SceneLight::keyShadow`, `keyShadowFov`), rendered from the frame's own draw queue (skinned figures included), sampled 3x3 in both the inked and the physically based shaders (texture unit 12, above the material maps).
- **Filmic grade:** an ACES-style curve on linear light, then a split tone (cold blue darks, warm lamplight) and the saturation, per weather (fog greyer and flatter, rain cooler).
- **Halos:** each of the eight nearest lamps blooms in the wet air, wide in fog, a ring in rain, faint on a clear night.
- **First person** body moved 20 cm behind the eye: the shoulders stay out of the view and the arms reach forward to the work. The rifle in hand is now the baked lever carbine (`DrawHeldItem`).
- **Frame rate:** `DEPTH_SHOTFRAMES=300 depth.exe --shots shots/vis tvis_` prints the average frame time per view: every harness view holds the 60 Hz vsync cap (16.7-17.1 ms) on this PC with four crew, rain, shadows and occlusion.
- **Not yet in phase 2:** the moon's directional light and its shadow on the non-PBR geometry, wet surfaces in rain, a moon glitter path on the sea (those belong with the world pass, phase 6).

## The Visual Overhaul, phase 5: the weapons (2026-10-01)
- **Every weapon in the catalogue is a baked model** (`assets/shared/weapons/<id>.glb`, 40 of them, about 14 MB with JPEG texture sets): the nine sidearms, ten long guns, two specials, fifteen melee and four thrown, each built part by part on its period reference (the spec's table) by `tools/artgen/weapons.py` with the gun kit (`gunkit.py`: parts, period materials - blued and case-hardened steel, brass, walnut, rosewood, silver with engraving, verdigris, iron, horn, bone, rope, leather, glass and more - and the conventions). Recipes: `weapons_side.py`, `weapons_long.py`, `weapons_melee.py`. The old starter items wear their catalogue counterparts (`WeaponModelId`).
- **Moving parts:** each moving part is its own object whose origin is its pivot, with node extras `group/kind/axis/amount/parent` (hammers, triggers, cylinders, pepperbox barrels, the chatter gun's pan, levers, bolts, break-actions, latches, ejector stars, pumps, a flintlock's frizzen, the bow's string halves, and "show" parts: rounds, shells, spears, arrows, rockets, rivets); `rt::AssetInfoOf` reads them and the markers (grip_r, grip_l, muzzle, eject, sight, mount_*) from the glb; `PoseWeapon` + `GroupValue` turn what the gun is doing (`GunAnimOf`: the shot's instant, the action's cycle, the reload's progress, rounds fired) into each part's transform, parts riding parts (the revolver's cylinder rides its break).
- **In the hands:** `DrawWeapon` puts the grip marker in the fist and returns where the left hand goes (a long gun's fore-end, a pistol cupped) and the muzzle; first person and the crew both use it. Reloads stay in view, tipped and rolled so the open action shows; the left hand lets go to work it.
- **Attachments** are their own models (`tools/artgen/attachments.py`, 13: sight, night glass, eyeglass scope, lodestone sight, compensator, choke, baffle, bayonet, extended and drum magazines, steam feed, oilskin cover, speed loader) on the weapon's mount markers; the speed loader appears at the cylinder during a revolver's reload. Not modelled: sling (needs a strap between swivels), bone stock, rifled barrel, hair trigger, fletching.
- **Upgrade tiers** show as the finish: cleaned, fresh bluing, then a warm gold cast (an approximation of the spec's engraved, gold-inlaid tier three, which would need a second baked texture set per gun).
- **Effects:** a muzzle flash that is a real light for an instant (`rt::AddLateLight`), thick white smoke from black-powder guns (blunderbuss, punt gun, flintlocks, pepperbox, derringer, coach shotgun) and a wisp from the others, drifting and spreading; spent cases thrown from the ejection port of repeaters (and dropped by break-actions and revolvers when opened) that tumble and bounce on the deck (`GunEvents`). Only powder guns flash, smoke and throw cases.
- **Texture size: 1024, not the spec's 2048** - the game renders at 1280x720, so a gun never covers more than about 600 pixels; 2048 sets would have cost ten times the repository space (the repo is public). raylib's JPEG support is switched on for this (`CMakeLists.txt`).
- **Harness:** `tvis_7_guns` (the firearms rack; `DEPTH_RACK=melee` for the melee and thrown), `tvis_7_gun_states` (one weapon at rest, firing, mid-reload; `DEPTH_GUN=<id>`), `tvis_4_sidearm_rain` (first person with `DEPTH_GUN`, `DEPTH_FIRE=1` or `DEPTH_RELOAD=1`). `.\tools\artgen\run.ps1 weapons` rebuilds them all in four parallel batches (about 7 minutes).
- **Not yet:** a bounce sound for the cases; aim-down-sights (the sight markers are in the models); wetness and blood on the guns.

## The Visual Overhaul, phase 6: the world (2026-10-01)
- **The sea** (`rt::DrawWater`, `RT_WATER_FS`): smooth normals from the swell each frame; Fresnel between the water's dark body and the reflected sky; the moon's glitter path (dimmed by its phase and cloud); the lantern's pool; foam along her hull, a bow wave and a widening wake with way on her, crests in a blow; rain rings; the Grotto dark. It never draws inside her hull (the engine room's eye is barely over the waterline).
- **The sky** (`rt::DrawSkyDome`, `RT_SKY_FS`): a gradient dome, the moon in the session's phase (`sess.moon`) with its lit side toward its light, clouds back-lit by it (cover by weather), stars hidden by cloud and fog. `L.moonDir` is the same direction.
- **The Gannet rebuilt** (`tools/artgen/boat.py` -> `assets/trawl/boat.glb`, 85k triangles, 4 MB) on the exact old layout: the hull from HalfBeam3 with a near-plumb stem (the old keel line left the fo'c'sle's floor outside her), carvel planking painted red over antifouling and a boot-top, the name on both bows, bulwarks with stanchions, a cap rail, freeing ports and a rubbing strake; a planked deck with real hatch openings; the wheelhouse with glazed windows, side lights, a roof rail and a lamp; a turned binnacle and a laminated wheel; the funnel with its band, whistle and soot; the lantern mast with a caged lamp, yard, shrouds, ratlines and stays; the gantry, trawl drum and steam winch; the harpoon gun on its pedestal; the bell in a belfry; the gutting table, fish pound, sea chest, air pump with flywheels, rod holders, bollards, fenders, the anchor and clutter; below, a riveted Scotch boiler with its furnace door and gauges, a twin-cylinder engine and flywheel, the bunker, the bilge pump, chequer plate, beams and frames, ice pounds, bunks that follow her flare, the magazine and the cot. `DEPTH_OLDBOAT=1` draws the old models (and fish) for comparisons.
- **Big surfaces tile**: `tools/artgen/tiles.py` makes tileable PBR sets with numpy (frequency-filtered noise wraps exactly): deck planks with caulked seams and trenails, painted strakes with chips, antifouling with barnacles, varnished and painted boards, painted iron and plated bulkheads with rust runs and rivets, chequer plate, dressed stone, shingles, weathered shed boards. UVs are in metres; ambient occlusion (and grime: funnel soot, waterway dirt) is baked into the vertex colours, which the PBR shader reads as occlusion for assets whose glTF scene carries `depth_vcao` (`gVcAO`).
- **The quay** (`tools/artgen/dock.py` -> `assets/trawl/dock.glb`, 34k triangles): a timber wharf on piles with bracing, fender piles and a ladder, the stone apron behind, five sheds with signboards (the Gunsmith's shop is new: barred window and a long gun on its bracket; the Fish Market open-fronted with trestles and the Owners' steelyard scales), gas lamps, iron bollards, the gangplank, the chalkboard easel, the slipway's cradle and winch, a cargo crane, crates, barrels, lobster pots, coils and a drying net. Its windows glow.
- **Fish** (`tools/artgen/fish.py` -> `assets/trawl/fish/fish_<arch>.glb`, ten archetypes: tuna, herring, perch, deep, eel, shark, ray, flat, billfish, pike): lofted bodies with an elliptical head, fins with rays, eyes in a gold ring, gill covers (a shark's five slits), the jaw line; a scale normal map, a silvered belly (metallic, so it catches the sky) and a countershaded back in the vertex colour; skinned to a four-bone spine. `FishArchOf` maps species names to archetypes, `FishColours` paints them (back, belly, fin), `DrawFishPbr` swims them (a fight beats harder with the tension, a fish on deck gasps and arches through a slap). A budget of 36 a frame, only within 15-25 m of the eye; inverts, turtles, jellies and birds keep the old models.
- **Performance on this PC**: the big assets draw a depth-only prepass first (`gDepthSh`), so the PBR lighting runs once per pixel; lights skip pixels they can't reach. All harness views hold 60 fps (16.7-17.7 ms). PBR surfaces now take the lamp's shadow (its uniforms were looked up before the shader loaded). Asset textures get mipmaps and 4x anisotropic filtering.
- **Harness:** `tvis_10_boat_bow|stern|deck` (the boat studio), `tvis_3_dock`, `tvis_11_fish` (the archetypes; `DEPTH_FISHPHASE`), `tvis_11_deck_catch`. `.\tools\artgen\run.ps1 boat|dock|fish` rebuilds them.
- **Not yet:** the Grotto's and the Atoll's land are still the old procedural meshes; the skiff is the old model; rays don't flap their wings.

## The Visual Overhaul, phase 7: performance, settings, the last primitives (2026-10-02)
- **The spec's performance case** is a harness shot now: `tvis_12_six_rain` (six hands on deck in rain, seen from the gantry, a catch on the planks). `DEPTH_UNCAPPED=1` turns off vsync and the frame cap so `DEPTH_SHOTFRAMES` measures the real cost; `DEPTH_GFX="shadows,ao,fog,scale"` overrides the settings.
- **Measured on this PC (Intel UHD, 1280x720, uncapped), before -> after:** helm in fog 18.1 -> 11.5 ms, six in rain 16.2 -> 11.4 ms, dock 15.6 -> 10.5 ms; every harness view under 12 ms (later runs drift about 20% slower as the GPU warms, still under 16.7). The big cost was SSAO, run per screen pixel in the composite with 8 taps (about 6 ms): it is now its own pass at half the view's resolution, blurred as the composite reads it (0.7 ms). The composite also skips the outline's edge taps when outlines are off (the Trawl's default), the sea no longer draws into the lamp's shadow map, and the depth prepass is kept for the big assets (over 20k triangles) only.
- **What each setting costs** (helm in fog, ms): lamp shadows off -1.5, high (2048) +1.4; AO off -0.7; fog plain -0.4; resolution 75% -1.8, 50% -3.0.
- **Settings** (the game menu's new Graphics page, shown in the Trawl, Red Tide and the arcade; saved as the `gfx` line in settings.txt): lamp shadows Off / Low 512 / Medium 1024 / High 2048, ambient occlusion, fog Plain / Lantern halos, resolution 50-100% (the 3D view renders smaller and the composite scales it up). `rt::Quality`, `rt::SetQuality`, `rt::ApplyGameQuality` (called by `RenderBegin`). High fog also makes the fog **drift in banks and thin with height** (`SceneLight::fogBanks`, the `RT_FOGBANK` snippet in the lit, PBR and water shaders) instead of a wall; the Trawl sets it by weather.
- **No raw primitives on the Gannet** (`tools/artgen/props.py` -> `assets/trawl/props/`): hatch covers (planked, ring handles) and iron battens on the coamings, the open hatch is now the deck's real opening; the watertight door (rim, six dogs, a wheel); red and green can buoys with lamp cages, rocking on the swell; the life ring (white and red quarters, a grab line), spinning through the air; a clinker skiff on the old one's half-breadths (lapped strakes, the red sheer strake, varnished lining and floor, thwarts with knees, rowlocks, the lantern post, a coiled painter) and oars that square in the pull and feather on the recovery. Spray drops are round.
- **Shadow acne:** a grazing-angle normal offset and bias in both shadow look-ups.
- LODs: the fish already fall back to the cheap unit model beyond 15-25 m and past a 36-a-frame budget; buoys beyond 45 m are only their lamps; nothing else on the boat is far enough from the eye to need one.

## Loose ends and the spec's leftovers (2026-10-02)
- **Wrecks on the sonar** as their own hard return (a hull on the scope and the side profile, marked "a wreck, N m down"); a bot on the sonar calls each once a night. Wrecks are never placed under the kelp, in skiff water or in the harbour mouth (the Gannet must lie over them).
- **The sim dives** (`DiveStep` in trawl_sim.cpp): once a night, with the suit and two hands, it lies over the nearest hardhat wreck with one-diver salvage (braking astern), a bot goes down while another keeps the air pump (bots now man the pump during any hardhat dive), and the diver works the rooms to the breach and up in the basket. The planner never routes back across the harbour line at night. The hardhat costs **200** (the user's call). `DEPTH_SIMHARDHAT=1` starts with the suit aboard. Lagoon, careful, four hands, suit aboard: 8/8 deadlines, dive 9% of the money.
- **Perks the doc names that were missing:** the Diver's +15 s helmet air, quicker footing in the silt and no bends from a recall; the Medic's CPR (a hand who drowns with a Medic aboard goes limp for 15 s; hauled in on the ring in that time, they're revived). A two-handed long gun can't be fired with a broken arm (the Visual Overhaul Spec), now enforced.
- **Visuals:** rain wets everything above deck (darker, glossier), lightning in squalls and storms with thunder, light shafts under the lamps in rain and fog, splashes on deck and rail and drips off the rigging; fish dull after death, are opened on the gutting table with a fillet beside, blood from the scuppers and the rail stains the sea, rays beat their wings; guns aim down the sights (right mouse), lower and raise on a switch, inspect (I, or idling), clear a misfire, hang one-handed with a broken arm, and cases clink on the planks; the landings and the Grotto are baked props on tiled terrain (sand, wet sand, jungle, rock, cave rock, deck, marble).
- **Still to do from the design doc (not built):** (role upgrades and consignments: built, below) the lantern's filter colours; walkies and proximity voice (needs voice chat). From the Visual Overhaul Spec: a separate viewmodel FOV, per-gun mechanically true step reloads beyond the current ones, gutted fish models in the hold, Glimmer variants' glow, threats' scars and slow heavy animation (the White, the Kraken).

## Role upgrades and the Owners' consignments (2026-10-02)
- **Role upgrades** (design doc, "The crew of six"): ranks open after the 1st, 2nd and 4th met deadlines (`Session::RankOpen`, `metCount`); each hand picks one of two per rank at the dock chalkboard's **Role upgrades** page (`CMD_ROLE_UP`, `Session::ChooseUp`); a choice is final; an upgrade belongs to the role (switch roles and you start again at rank 1); bots take a fixed preference (`ApplyUps`). The bits are `Crew::ups` (`Crew::Up(u)`, `RoleUp` in trawl.h, names and notes in trawl_data.cpp). Effects: Shipwright (+30 integrity on a patch), Stoker (overpressure grace 10 s, the screw a noise step quieter), Old Salt (no sliding; crew near slide later), Deck Boss (bots +20%), Iron Hull (+25 per section), Full Steam (+20% speed 20 s a night); Light Touch (slack 3 s), Heavy Hand (side pressure x1.25), Reader (the tip ticks 0.5 s before a run, `Rod::readWarn`), Strong Line (+20%, `Fight::holderUps`), Trophy Hunter (`CatchRec::trophy`: a first catch +50% more), Bait Master (+25% bites, chum twice the scent); Deep Lungs (+15 s air), Glint Eye (salvage glints through the bulkheads), Pressure Hardened (the hardhat on a bell wreck down to 90 m), Wreck Rat (locked cabins without a crowbar: now the upgrade, not every Diver), Strongback (two-diver lifts alone), Old Hand (no hose snags); Field Surgeon (the cot sets two injuries at once, twice as quick), Warm Blankets (+8 s overboard for everyone), Second Wind (CPR window 30 s), Steady Nerves, Miracle Worker (once a deadline), Ghost Speaker (sees the dead aboard).
- **The Owners' consignments**: one named salvage item a deadline in a wreck on a named ground (`Session::consign`, `consignGround`, placed in the deepest room of a wreck by `PlaceConsignment`), named on the tape and the chalkboard. In the hold at the count: the reward is chosen on the count panel (`CMD_CONSIGN_REWARD`: a Slipway fitting worth up to 600 fitted free, or 10% off the next quota). Missed: the next quota +25% and the dearest fitting repossessed (its effect taken off her too); two missed in a row ends the run.
- Both are in the snapshot for guests. `--trawl-session-test` checks the ranks, the role reset, a bot's preference, Strong Line, Trophy Hunter, Warm Blankets, a missed and a delivered consignment. Shot: `trawl_roleups`.

## The Lagoon's other landings and the skiff's refits (2026-10-02; design doc v2, "Islands", "Skiff upgrades")
- **The Old Lighthouse rock** (`LK_LIGHTHOUSE`): carved on the reef inside the crest across the lagoon from the Atoll
  (hashed from the seed, after the rest of the chart). The keeper's hearth in the tower's lee (sheltered: rain doesn't
  reach it), his strongbox in the tower (100-250, locked: a brass key), his logbook on the rock (picking it up marks the
  Sandbar's buried chests), and the great lens (salvage, 3 kg). Top-down a white tower with a red band; in first person a
  tall white tower, red band, the lamp room's dark glass, the keeper's stove smoking.
- **The Sandbar** (`LK_SANDBAR`): bare sand between the Atoll and the basin's middle (clear of where she fishes): no fire,
  no trader, 1-3 chests buried (80-300) where a bottle's map, three chart pieces or the logbook marks them. The tape warns
  at 01:30; at 02:00 the tide makes over it (`Gannet::FloodSandbar`): whoever is on it is in the water, the skiff floats
  off, anything left (and every chest not dug) is gone, and nobody can land on it again that night. Dry the next night.
- `Landing::kind` and `flooded` now travel in the snapshot (the kind didn't before).
- **The skiff's refits** (`Gannet::skiffUps`, `SkiffUp` bits; the Slipway sells them, a repossession takes them back):
  skiff lantern 60 (bow light 6 -> 10 m), planked-up sides 200 (hull 40 -> 70), bigger skiff 450 (three seats, 250 kg),
  skiff crate 50 (birds can't steal from her), trolling holders 80 (while she's under way a rod astern can take a fish
  of up to 12 kg, one every 20 s at most, from the web's own bites), muffled oarlocks 70 (rowing noise halved), flare
  mortar 90 (W in the skiff: a flare over her, three a night), steam launch kit 500 (X in the skiff: 3 m/s with nobody
  rowing, the mouse buttons steer, noise 4).
- Not yet: the traders' skiff refits (the outrigger, the sealers' sail, the kelp-cutter prow, the quiet launch engine,
  the cave rudder, the Atlantean prow); a funnel on the skiff when the launch kit is fitted.
- Tests in `--trawl-skiff-test`; shots `trawl_lighthouse`, `trawl_sandbar`, `trawl3d_lighthouse`, `trawl3d_sandbar`.

## Netcode polish (2026-10-02)
- **Snapshot trimming**: a busy night's world went from 17.1 KB to 9.7 KB. The web's agents are quantised (position 16
  bits across the chart, depth 16, velocity 8 a component, the flash 8, the clock in 60ths, the hurt only when hurt):
  8.4 -> 3.0 KB for 215. Sonar returns the same way (4.3 KB for 179 -> about 2.3). A catch record no longer carries its
  species' name (the guest looks it up) and its twelve yes/no facts go as one set of bits. The mirror still writes back
  the bytes it read (`--trawl-net-test` prints where the bytes go).
- The arcade session (protocol 3) now sends any real-time snapshot over 12 KB in parts, so a late-night hold can't push
  the Trawl's snapshot into GameNetworkingSockets' reliable fallback.
- **Reel prediction**: a guest holding the reel during a fight shortens the line on its own mirror at the reel's rate
  (`Fight::PredictReel`: the reel's own arithmetic from `Fight::Step`, not while the drag slips), so the line and the
  tension gauge answer the button at once; the host's next snapshot puts the truth back.
- Next: a real LAN night on two or more PCs (the user's).

## The weapons' odds and ends (2026-10-02; design doc v2 weapon and attachment tables)
- **Throwables** (`WC_THROWN`): the Gunsmith now sells the crackerjack, the lamp-oil bottle and dynamite (the depth charge stays at the Chandler). Thrown over the rail on an arc (`Projectile::payload` = the catalogue row), each goes off where it hits the water (`Gannet::ThrownLands`): the crackerjack stuns fish (`Eco::Stun`, `EcoAgent::stunT`) and drops thieving birds within 4 m for 3 s; dynamite is a 4 m blast (`Eco::DepthCharge(p, fl, radius)`) whose fish float up chum-grade (20%) and kills a swimmer in range; lamp oil burns on the water for 8 s and sets the Drowned within 3 m alight (40).
- **The bayonet**: an empty gun with one stabs (15 at 1.8 m, a melee finish) instead of clicking.
- **The lodestone sight**: every fifth shot (`Crew::shotsFired`) flies true and counts as a head shot (`Projectile::snapHead`).
- **The bone stock**: a kill with the gun adds 0.1 to the Killscore (`Projectile::boneStock`).
- **Wet powder**: a hand who has been overboard misfires 40% of the time for two minutes after (`Crew::wetT`), whatever the weather; the oilskin's rule still applies.
- Checks in `--trawl-gear-test` (each of the above).

## The cup of something yellow (2026-10-02; the user's request)
- `Item::Cup` (appended after `Weapon`). The Chandler sells it for 5 shillings ("cup"), and it goes into the buyer's own hands. **It never runs dry** (the user: "infinite yellow liquid so a player can continue to pour it").
- Hold the use button to pour a stream just ahead of you (`Crew::pourT`, `pourAt`). Any other hand standing under it on the same deck is drenched (`Crew::yellow` rises 0.9 a second to 1). A drenched hand fades over 45 s, and a swim rinses it off. The log says "Hand N is drenched in something yellow", and a bot barks ("Oi!", "Why is it warm?"). Purely a prank: no gameplay effect.
- Drawn top-down (the cup in hand, a dotted stream and splash, the drenched figure tinted, with a yellow puddle, a wet glint and drips) and in first person (a tin cup brimming yellow, the stream from the raised hand, the puddle round the boots, drips off the shoulders; the multiply tint alone was lost on dark oilskins). In the snapshot. Checked in `--trawl-gear-test`; shots `trawl_cup`, `trawl3d_cup`.

## The Visual Overhaul, its leftovers (2026-10-05)
- **Faces:** the shared figure has `fear`, `strain` and `grin` (`fig::Pose`). They scale the eye and mouth bones:
  - fear: wide eyes and a round open mouth;
  - strain: a squint and a clenched, stretched line;
  - grin: squinting eyes and a wide open smile.
  - When: a hand in the water, held by the Grotto or the kelp, fallen or bleeding is afraid. On a fighting rod they strain with the line's tension, and under a load over 15 kg. Everyone grins for a moment when a fish comes aboard.
- **Wet and bloodied:** a soaked hand is darker (drying over two minutes, from `Crew::wetT`).
  - Blood builds on their clothes and hands at the gutting table and fades over a minute; a wound bleeds onto them; a swim rinses it.
  - Your own first-person hands show it most.
- **Glimmer variants shimmer** like oil on water (the colours run along the body) and glow faintly, on deck, in the hold and in your hands.
- **Gutted fish in the hold:** up to eighteen opened fish lie in rows on the ice pounds below.
- **Harness:** `tvis_13_expressions` (at rest, afraid and soaked, straining, grinning and bloodied).
- **The heavy threats:**
  - The size-5 threats (the White among them) swim with a slow, heavy beat, roll into their turns, rise and fall a little, and carry pale rake scars down both flanks when near.
  - The Kraken's arms are slower and ponderous: thick at the root, tapering to a tip that curls back, dark and wet at the base and paler toward the tip, with pale suckers along the inside and old scars across them.
- **Still not done from the spec:** a separate viewmodel field of view, and mechanically true step reloads for every gun.
### Clutter and the landings' look (2026-10-05)
- 15 new props in tools/artgen/props.py (bucket, fish box, crate, lobster pot, rope coil, tackle box, oil can, mop, net pile, tarp, oilskin on a peg, seal, shore crab, sea chest, driftwood), assets/trawl/props.
- The Gannet's deck carries them against the bulwarks and in the corners (`DrawDeckClutter`, also in the boat studio shots), the quay between the shop doors and along its edge; nothing collides with them. `DEPTH_NOCLUTTER=1` hides them.
- Landings: the seals, the bull, the crabs, the caches (sea chests), the beach junk (crates) and the fish on the fire and the sand are models now (the fish are the baked species, browning on the stick); each beach has driftwood, and the lived-in ones a lobster pot, a bucket and a net pile by the hut. The keepers (elder, Old Hoskins, the foreman, ...) are the shared sailor rig in their colours with beards, idling and gesturing. The fire's flames and smoke are in the world (glowing tongues; a plume of dark low-poly puffs, greyer to black as the fish burns) instead of 2D circles drawn over everything.


### The grounds' scenery (2026-10-05)
- Nine scenery props (props.py: bush, shorerock, reeds, kelpfloat, sargassum, wreckribs, column, archruin, stalagmite) placed by `EnsureLand` from the chart (one cell in four, a hash per cell so every client builds the same): bushes in the jungle, rocks and reeds along the shore, kelp and sargassum riding the swell (`PropAt::floats`: up and down with the sea, tipped by its slope), a wreck's ribs over the Grotto's wrecks, Atlantis's broken columns and arches over its reef, stalagmites under the Grotto's walls.
- A clear night has a faint moonlight (by the moon's phase), a sky ambient and a moonlit-blue haze, so the land and ruins stand dark against it instead of vanishing.
- The war canoe is the skiff drawn long with six paddlers rowing; the ghost ship is a drowned grey-green twin of the Gannet with tattered sails; the choir's singer is a pale drowned figure risen to the chest.
- Shots `tvis_14_ground_<lagoon|weeds|grotto|atlantis>` look out over each ground from the bow.


### The crew's faces, after the user's low-poly references (2026-10-05)
- crew.py: a head in proportion (a little jaw and chin, not an egg), small dark eyes with a pin of light under brows, a modest nose, ears tucked in, a mouth line, and hair (a cap over the crown and the back with a hairline; under a cap or a sou'wester only the fringe shows). The body is one subdivision level with an auto-smooth crease, so its facets read like the references' low-poly people. `hair` is a recoloured material: the Trawl's sailors wear their own hair colour, A Night Off's people a natural one from their colours (`HairFor`), the Flight's townsfolk theirs. Heads are no longer oversized (`LookOf`: 0.98-1.05, was 1.08-1.2). Every game that uses the shared crew figure gets the new heads.

- **The user's two reference images, as roles:** the Angler now wears a bucket hat (with a band) and an orange life vest (two fat front floats with stitched channels, a back panel, a collar, two buckled straps) over a shirt and dark trousers; the Diver an orange boiler suit (collar, zip, chest pocket, belt) with the headlamp. `LookOf` varies the shirts, the hats and the suit's orange per sailor.


- Fix (2026-10-05): the landings' terrain began its innermost ring at radius 0, a zero-width quad whose normal came out NaN, so the middle of every island (the sandbar, the lighthouse rock...) drew as a black disc in first person; the ring now starts just off the centre. The Old Lighthouse landing uses the Flight's banded lighthouse model.

- Playtest (2026-10-06): fishing still too hard. The take's window is 1.2 s (wary 0.9 s, +0.25 for an Angler); an early strike on a nibble spooks the fish only one time in three (otherwise it backs off and comes again); an unbowed jump throws the hook 15% of the time (was 40%); hook pull-outs about half as often; a circle hook allows 2.5 s of slack; no snaps in a fight's first 3 s. The --trawl-fight targets were raised to match (90/78/69/54/56%). The night runs 11 real minutes (`NIGHT_RATE`; was 9), every event keeping its clock time. The shakedown sends you back to land another fish if the one for the gutting lesson goes over the rail.
