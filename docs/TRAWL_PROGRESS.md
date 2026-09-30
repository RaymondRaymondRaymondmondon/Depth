# The Trawl: build log

Design: `The_Trawl_Reference/The Trawl — Arcade Game 2 Design Document (Draft).pdf` (50 pages). Numbers live in
`src/trawl_data.cpp` (and, from stage 3, species records in `data/trawl/trawl_species.json`).

Build order (the design doc's own):

| Stage | Work | Gate | Status |
|---|---|---|---|
| 1 | The boat alone: hull physics, engine, stations, top-down deck, movement, one player | The boat rolls, lists under weight, and sinks when flooded | **Done** (`--trawl-boat-test`) |
| 2 | Lines and fishing: Verlet lines, cast, bite, fight, landing against a dummy fish | `--trawl-fight` numbers in range | **Done** (`--trawl-fight all`) |
| 3 | Ecosystem port: the water column, light layer, vibration, Wake, the Lagoon's species | `--trawl-eco lagoon` curves stable with no crew | |
| 4 | The session loop: dock, Chandler, sail, clock, sell, quota | A solo night on the Lagoon is playable end to end | |
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
