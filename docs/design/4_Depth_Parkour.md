# DEPTH: the Parkour game (Periscope dives)

*Seven generated platform levels and a 3D vertical descent, each a living ecosystem. See [1_Depth_Overall.md](1_Depth_Overall.md) for how dives fund the rest of the game.*

---

## 1. What it is

At the **Periscope** you send one diver through a single, brutally hard platform level for gold and relics. It's **like Super Meat Boy: the hard part is the platforming itself.** Precise landings, gears that force a low or high arc, timed jets, wall-jump chimneys and move-requiring set-pieces are the challenge. Enemies and beasts are extra danger on top.

What makes Depth's parkour its own:
- **Every dive is a new random level** (`StartPlatform(g, level, true)`). It's procedurally generated from the physics, set-pieces, hazards and the ecosystem, then proven crossable by the real movement code. A death replays the same level (it's rebuilt).
- **Every level is a living food web.** Creatures sense, remember, hunt, flee, eat each other (in view, and in the background), carry kills to dens, and leave corpses and scent. The diver is part of that web, not the centre of it.
- **Apex predators are never scripted.** A director brings them in when the level has been calm for a while and the diver is in a fair position.

---

## 2. The dive flow

1. **Periscope chart:** the dives are listed on the left, and the selected one is shown in detail on the right (`Game::periscopeSel`). Options, all saved:
   - **Checkpoints** on or off. On forfeits the relic.
   - **Normal / Hard.** Normal turns gears into empty space, jets into floor with a shorter firing window, and mines into spikes. It's still a real crossing, just gentler. Hard pays x1.5.
   - **Boss on or off** for the Hull and the Pirate Ship. Off gives an arena with no boss and no relic.
   - **Music and Effects volume.**
   - **The dossier:** a file folder holding a sheet for every beast and flora, the food web (drawn), and a creative description of the level.
2. **The run:** reach the exit. Touching anything lethal kills you and restarts the whole level (or the checkpoint). The death screen names the killer and a counter-tip for 5 s ("Taken by the ...").
3. **Payout at the exit** (no collectible coins):

| Level | Payout | Unlocked by |
|---|---|---|
| 1. The Pipes | 60 (+0-60 speed bonus under 120 s) | Start |
| 2. The Hull | 120 (+ Kraken relic chance) | Pipes |
| 3. The Pirate Ship | 200 + 1 relic (Ghost Ship: 2x gold, 2 relics) | Hull |
| 4. The Island | 240 | Pirate Ship |
| 5. The Cave | 280 | Island |
| 6. The Weeds | 320 | Cave |
| 7. Atlantis | 360 | Weeds |
| 8. The Abyss (3D) | 400 | Atlantis |

Hard pays x1.5. Difficulty ramps strictly with this order.

---

## 3. Art and presentation

- **Retro pixel art.** Levels are drawn at half resolution into `PixelRT()` and scaled up with point filtering, with sub-pixel camera offsets for smooth scrolling. World pixels equal screen pixels on a **2 px art grid**; sprites have no fractional offsets and no rotation.
- **Camera:** locked exactly to the diver on both axes (no easing, no lookahead). It only stops at the level edges.
- **Tiles have 2.5D depth** (`DrawDepth`: tops and sides recede up and to the right). The diver casts a shadow on the wall behind. Tiles auto-join by an N/E/S/W bitmask: pipe elbows, tees and crosses, coral crowns and tendrils, ship keels and trim, mast lashings, sagging rope. Wear is drawn per level (`DrawTileDetail`).
- **Every hazard** (spikes, gears, jets) gets the same pulsing orange corner brackets.
- **Backgrounds** (`BackgroundSystem`): far, mid and fore `ParallaxLayer`s per zone, with the fore layer drawn in front of the diver. Each level's background is unique (never copied) and **alive**:
  - Background beasts from the food web hunt and eat each other in a dynamic scene. You can keep moving, or stop and watch.
  - Two background scenes per level.
  - The Island has a day/night cycle.
- **Creatures** are shaded pixel sprites (`pixelart.h` / `beastart.cpp`): lit 4-band ramps, dithering, contours, patterns and an ink outline. Walkers plant their feet with IK (`ik.h`, FABRIK and the two-bone knee) and step in diagonal pairs. Tails are fixed-length chains. Injured animals limp.
- **The diver:** pixel-art poses (`PlatformState::pose`) for run, slide, roll, dash, glide, climb, balance, ledge-hang and backflip. Squash and stretch happen in whole art pixels. There are bubbles when underwater.

---

## 4. Movement (the diver)

Movement runs at a fixed **240 Hz** and is snappy (high acceleration). The constants live in `namespace kin` and are shared by the game, the generator and the validator.

| Move | How |
|---|---|
| Run | A/D or arrows |
| Jump | Space/W/Up. Hold for height (about 3.8 tiles), tap for a hop. The longest same-height gap is about 6.5 tiles |
| Wall slide and wall jump | Push into a wall in mid-air, then jump. Two walls up to 4 tiles apart can be climbed |
| Slide | Down while running (short hitbox; jump out of it for a slide-jump). Goes under low beams ('h') and spider webs |
| Brake | Down alone while falling |
| Roll | Down plus a direction on a hard landing: turns the fall into speed |
| Stun | A landing faster than `STUN_V` without a roll stuns briefly. **Short falls never stop you** |
| Dash | Double-tap a direction. Once per jump, on every level; 8-way underwater with Up/Down |
| Glide | Underwater, **hold Up** while falling (hydro-glide) |
| Poles ('w' kelp, 'l' ratlines) | Up/Down climbs, Down slides fast, climbing off the top balances on the tip. Shift+direction+jump backflips. Seaweed slows a fall |
| Pole hop | Jump forward off '=' for a flatter, faster hop |
| Ledge grab | Fall past a wall's lip while pushing into it; Up hauls you over |
| Movers | Ride a whale's back, a tortoise's shell, a manatee, the Orichalcum Leviathan |

Relics on the rank-1 crew member shape the dive: pickup radius, run speed (Syringe +5%) and lamp radius (Awakened Lantern +30%).

---

## 5. Level generation (`levelgen.h/.cpp`)

- **Kinematic generator:** `CalculateValidJumpArc` derives the reachable arcs from the movement constants.
  - **Pass 1** lays the critical path with **set-pieces**: steam boost, crumbling run, gear gauntlet, barnacle shaft, ship-to-ship gap, wall-jump shafts, the Piston Corridor, Gun Crossfire.
  - **Pass 2** adds hazards, enemies, gunner perches, light cues, dens and the ecosystem.
- **Move-requiring set-pieces** (`GenerateLevel` post-pass) ramp with the unlock order (1, 2, 2, 3, 3, 4, 4 per level): 7-8 tile spike beds that need a dash, 4-tall ledge walls, low beams to slide under, and slimy walls ('s', no grip) off-route.
- **Validation:** `GeneratePlatLayout` checks every hop with the real movement code (weighted A*, `ValidateGenerated` / `Crossable`, which also dashes, slides and grabs ledges). Any failure redraws the level (0.25-2 s). Only the boss arenas are hand-made.
- **Difficulty:** the `ParamsFor` margins run from 0.76 up to 0.97 across the unlock order, and later levels are longer.
- **Tile legend:**

| Tile | Meaning |
|---|---|
| `#` | Solid |
| `=` / `\|` | Pipes (solid) |
| `x` | Spikes / urchins / limpets |
| `g` | Gear |
| `t` | Jet or live plating |
| `v` | Steam vent (updraft) |
| `f` | Fragile scaffolding or grating (gone 0.5 s after you land) |
| `b` | Barnacle wall (1.5x wall jumps) |
| `w` | Kelp / weed pole |
| `l` | Ratlines |
| `r` | Rigging rope |
| `D` | Creature den |
| `R` | Rock or boulder |
| `T` | Torpedo tube |
| `N` | Cannon |
| `y` | Barrel chute |
| `k` | Cover barrel |
| `P` | Door-ambush pirate |
| `G` | Musket pirate |
| `h` | Low beam |
| `s` | Slime wall |
| `~` | Shallow pool (wading x0.6) |
| `i` | Below-deck interior |
| `o` | Faint light cue |

- **Tools:**
  - `depth.exe --verify` must report "All generated levels can be crossed".
  - `--gen <level> <seed>` prints a level as ASCII.
  - `DEPTH_GENLOG=1` shows the set-pieces placed.
  - `DEPTH_FULL=1 --verify` searches every shaft climb.

---

## 6. The ecosystem engine (`beasts.h/.cpp`)

One engine drives every biome's creatures. Each biome plugs in a `BiomeDef` (species table, food web, spawn plan, hooks, sizes, and what is lethal or touchable).

- **Minds:** senses (sight, sound, lateral line, tremor, smell), memory, utility-based choice, personalities (including 12 abnormal profiles), hunger, fear and fatigue.
- **World:** a food web, corpses, a scent grid (blood draws predators), A* navigation, walker ledge sense and landing checks (`SafeArc`), and breakable tiles re-read every tick.
- **Traits** (`BeastTrait`): striker, charger, den ambush, camouflage, mobber, groomer, parasite and host, trap, defensive, toxic, curl, flash, echo, lightseek, klepto, carry (predators carry kills to a den or quiet spot), giant (a mover), flora (sessile).
- **Universal rules:**
  - **Persistent injuries:** injured animals are slower, bleed scent and are preferred by predators.
  - **Proximity herding:** sprinting, sliding or dashing near small fauna frightens them along your line.
  - **Stun:** stunned beasts aren't lethal.
- **Food-web director:** if nothing has been eaten on screen for 20 s, a hungry predator stalks visible prey, or prey and a matching predator are drawn to the diver's height, so the web plays out where you can see it.
- **Apex directors** (one per biome) bring the top predator in after a stretch of calm, never unfairly, and send it away after it's fed, after a time limit, or on respawn. `DEPTH_NOAPEX=1` disables them.
- **Discovery:** first-meeting hints (`BeastHint`, deduplicated) name a creature and its counter the first time you meet it. The dossier sheets hold the full story.
- **Tests:**
  - `--verify-beasts` and `--verify-<pipe|pirate|island|cave|weeds|atlantis>-ecosystem` check each biome's chains plus a minute of real-level life.
  - `DEPTH_BEASTLOG=1` prints behaviour histograms.
  - `--shots shots fauna` renders each biome's busiest cluster.

---

## 7. The levels

### 1. The Pipes: the Nautilus's own ducts
> *"The Nautilus breathes through these ducts. Steam hisses through a thousand joints... None of it cares that you are here. Only the jumps can kill you, and they will, and often."*

- **Structure:** claustrophobic, enclosed ducts (the fill is solid), lit only by the diver's **helmet lamp** (`DrawLampDarkness`).
  - Pipe runs ('=') stand on risers ('|') and join into continuous runs.
  - Pipe chambers drop through T and elbow joints (`PipeDrop`).
  - Set-pieces include the steam boost, gear gauntlet, crumbling run and the **Piston Corridor** (two crumbling plates into a vent launch).
- **Art:** far steel plating and mid pipework pulled toward grey, access plates, hazard tape, drips. Steam-lamp light cues.
- **Creatures (harmless; they ignore the diver):** Dust Moth, Water-Spider, Centipede, Blind Pipe-Rat, Rust-Mite, Pillbug, Scavenger Mouse, Cockroach, Glow-Beetle, Cave Cricket. The moths go to the glow-beetles, the spiders string the trusses, and the rats and mice scavenge.
- **No enemies:** only the jumps kill.
- **Sound:** synth-forward, with rare pipe clangs.

### 2. The Hull: the Nautilus's back, reef-grown
> *"A reef has grown on the old rivets... A grazing whale drifts over, and something far larger follows its shadow. The Kraken keeps its lair where the deck ends."*

- **Structure:** a submarine deck of steel plating (9 tiles thick, never breached) running near plating → open water → drop-off → trench mouth (`BuildTrench`). Features:
  - A conning-tower climb.
  - Limpet and urchin beds.
  - Live plating.
  - Rock reef boulders.
  - Kelp beds.
  - **Torpedo tubes** (`PlatLauncher`, a timed shot with a warning glow).
  - The **cavern detour** (doorway, wall-jump chimney to a high tunnel, drop shaft).
  - The **ballast vent** (a floor vent lifts you onto a gantry over urchins).
- **Art:** steel plating, god-rays, caustics, silt, and a distant whale in the background.
- **Base web:** Pilot-Fish, Cleaner Shrimp, Octopus, Pufferfish (lethal when puffed), Hull-Leech, Stinging Anemone, Hermit Crab, Brittle-Star, Barnacle Crab.
- **The 1.3 roster:**
  - **Hull-Crusher Eel** (lethal): near a diver standing on the deck, it winds up and slams the plating. The shockwave kills your momentum, stuns walkers and shatters rust-algae.
  - **Hull-Grazer Whale** (giant): cruises 3 tiles over the deck. Its flat armoured back is a **mover** you can ride, and under it the megalodon can't reach you.
  - **Siphon Octopus:** hidden in an exhaust tube on a wall. A silent 1 s windup (the rim flexes), then 1.8 s of suction, lethal only at the mouth. Its kills are left there as bait; hydroids stun it.
  - **Steel-Biter Megalodon** (apex): a director brings it in from about 24 tiles away after 60 s (then 95 s) of calm. It patrols open water and goes for the wounded, or a diver with water all round them. It telegraphs a straight lunge (0.9 s: it stops, shudders and opens its jaws). The turbulence throws anything within 9 tiles that isn't **anchored** (kelp, a pole, a ledge, a crevice, or under the whale). It never targets a diver in cover, and leaves when fed or after 50 s.
  - **Barnacle-mites:** dash, slide or roll through them for 1.4 s of higher top speed.
- **Flora:**
  - **Rust-algae:** graze it at speed to throw a blinding cloud that corrodes pursuers.
  - **Bio-electric hydroids:** strike them at speed for a stun shockwave.
  - **Pressure-anemones:** vault off them; the jet stuns whatever is behind you.
  - **Metal-eater moss:** injures anything heavy that crosses it.
  - **Hull-kelp:** anchors you. Up swings you up a wall; Down in the air swings you down a drop.
- **Ecology planner** (`Hull13Spawn`): everything is placed with a purpose. Siphons are guarded by hydroids, kelp sits at every step and gap lip, and no standing spot is more than about 6.5 tiles from cover.
- **Boss: the Kraken** (optional). A colossal mythological beast: a body looming in the abyss, and tentacles that rise and slam. `KRAKEN_SCALE` sizes the one body you **stomp**. Beating it gives a chance at a relic. The arena is `HULL_ARENA` (or `HULL_ARENA_NOBOSS`).
- **Sound:** a deep synth drone, with nothing high-pitched.

### 3. The Pirate Ship: a fleet in an endless storm (above water)
> *"A fleet that never made port... Under the waterline the Grand Kraken waits for a hull to hold. Blackbeard keeps the captain's cabin, and he does not share."*

- **Structure** (`BuildFleet`): deck, deck, hatch, hold, hold, companionway, cabin, spread across a **fleet** of ships:
  - Each ship has a raised quarter deck with a stern step and a stepped bow with a bowsprit (the leap-off point).
  - Shallow hatches have spikes one tile down.
  - Below-deck cabins ('i') are drawn with lamps and doors.
  - Ratline masts ('l') are climbable; one barricade-shaft mast is a solid wall.
  - Across gaps too wide to jump: a mast of shrinking yardarms up to a **rigging rope** ('r') to the next mast.
  - **The sea** is the hazard: falling between ships drowns you (`waterY`, splashes).
  - Set-pieces: gun batteries ('N' cannons), barrel runs ('y'), rotten planking, and the **Gun Crossfire** (two cannons firing across the lane on staggered timers).
- **Enemies:** 'P' pirates burst out of a deckhouse door to stab; 'G' gunners shoot aimed musket balls from behind a 'k' barrel. Touching an enemy is fatal. **Friendly fire** is on: musket balls and blasts kill other pirates and parakeets.
- **Ghost Ship variant** (about 1 run in 8): skeleton crew, fog, rotting timber, torn sails, ghostfire lanterns, and everything **1.6x faster**. It pays double gold and 2 relics.
- **Art:** rain, low fog, lightning, gulls, rain dimples, mist, lanterns hanging from the yards, and background hulls on the water line.
- **Base web:** Bilge Rat, Ship's Cat, Powder Monkey, Guard Dog (lethal only when berserk from fleas), Flea Swarm, Barn Owl (carries rats off in its talons), Gull, Albatross.
- **The 1.3 roster:**
  - **Grand Kraken** (apex, giant): after 70 s (then 110 s) of calm, its vast shape looms under the fleet. **A slam and a snap in one attack:** a tentacle rises 4-7 tiles away and hangs over the diver, its shadow growing for 1.1 s. It slams down (lethal along its length for 0.7 s, knocking pirates overboard) **and in the same attack** two arms wrap a proven **snap point** and tear the ship. Enemies and launchers go, and the sunk half drops a row, taking anything standing on it. The snaps are real, anywhere they're proven crossable (`GenSnap`, `PlatSnapShip`, snap mask in the layout). It leaves after two snaps or 45 s.
  - **Timber-Shell Tortoise:** a giant walker drawn as a real turtle, with a domed scute shell (a mover), scaly legs, a beaked head and a tail. It turns at walls, edges and rotten planks, and crushes crates.
  - **Rigging-Mimic Cuttlefish:** hangs as a fraying rope under a yard. Leap off the nearby ladder without pausing and it snatches you (0.3 s twitch, 5-tile reach). Pause on the ladder 0.5 s to reveal it. Siren's lantern weed reveals it for good, and copper moths distract it.
  - **Cannoneer Mantis Shrimp:** in a crate porthole over 6+ tiles of deck. Anything *fast* crossing its line gets a 0.4 s tell, then a bullet-fast lethal strike. Walk past and it never fires.
  - **Wood-borer worms:** rush across rotten planks and they give way 0.25 s behind you.
  - **Copper-scale moths:** flashing fodder.
- **Flora:**
  - **Cannon-moss:** silent landings that never stun.
  - **Ship-rot fungus:** a spore cloud and a reek that scavengers follow.
  - **Mast-kelp:** dash through it and the spar swings back into your pursuers.
  - **Barnacle-cluster:** kills a diver who *dashes* into it; walking is safe.
  - **Siren's lantern weed:** light.
- **Boss: Blackbeard** (optional; arena `CABIN_ARENA`). He's tall (36 x 72) and **deadly from every side, including from above**, except while **dazed** after charging into a wall. A charge only dazes him if it carried at least 5 tiles (`chargeStartX`). Stomp him while dazed to open the exit. He guarantees a relic and often two (not with checkpoints on).
- **Sound:** a synth sea-shanty.

### 4. The Island: idols, jungle and a volcano (above water)
> *"An island of idols to gods no one remembers. A volcano smokes over stilt villages, temples and a river... day turns to night while you climb. Learn who is out at night."*

- **Structure:** jungle terraces, ravines, plank bridges, shallow pools ('~', wading x0.6, which rinses firefly dust), and move set-pieces.
- **Art:** the richest backdrop in the game, baked into textures (`IslandArtPrepare`). An erupting volcano, stilt villages with villagers, temples, totems and idols, sacrificial altars, dense jungle (palms swaying in 3 frames, trees, ferns, bushes), a beach with canoes, flowing rivers, and a **day/night cycle**.
- **Base web:** Wild Boar (lethal when charging), Tree Snake (lethal strike), Monitor Lizard, Fruit Bat, Orb Spider, Coconut Crab, Dart Frog, Gull, Hunting Dog, Coconuts.
- **The 1.3 roster:**
  - **Goliath Island Beetle:** a giant walker whose idol-carrying shell is a mover.
  - **Mangrove Stalker:** camouflaged at the foot of drops. It snaps at a diver who lands within 2.8 tiles (0.3 s tell). Snapping through a poison-dart vine paralyses its jaw for 6 s.
  - **Totem-Centipede:** a fully drawn segmented centipede with a carved totem head. Ground vibration wakes it (hard landings, drum-fungus booms, firefly dust on you): 1.4 s of tribal drums and a shuddering floor, then it bursts up ahead.
  - **Arch-Serpent** (apex): lives in a **lair**, a rock den in a ravine on roomy flat ground. After a calm stretch it rises 4-14 tiles away, rears for 1.6 s and strikes. Its thrash heaves the ground and smashes plank bridges *behind* the diver (restored on respawn). It strikes twice, then goes home.
  - **Mud-skippers:** fodder.
  - **Glow-fireflies:** their dust on your suit for 12 s lets the centipede feel you.
- **Flora:**
  - **Drum-fungus:** a landing launches you at 1050 px/s with a boom.
  - **Poison-dart vines:** fatal to touch.
  - **Razor-palm roots:** trip a sprint and lacerate big pursuers.
  - **Idol's bloom:** a fresh dash.
  - **Root-sponges:** a landing bounces back at 97%.
- **Sound:** ambient synth with tribal chants.

### 5. The Cave: a flooded cave (underwater)
> *"Everything down here hunts by feel, by sound, by tremor... Somewhere in the tunnels, the Abyssal Arachnid has strung its webs at the height of a diver's head."*

- **The Cave is underwater** (the user's correction): water dash, hydro-glide and bubbles. The backgrounds are black water with rising bubbles and hanging silt.
- **Base web:** Cave Cusk-Eel (roosts in ceiling crevices, hunts by lateral line), Glow Jelly, Blind Olm, Silt Isopod, Cave Leech (a lethal ceiling drop), Giant Tube Worm, Glow-Shrimp (fodder that swarms to light), Blind Cave Loach.
- **The 1.3 roster:**
  - **Crystal-Shelled Tortoise:** a giant on tall legs you can walk under. Its glowing shell deflects dropping leeches and is a mover.
  - **Echo-Stalker** (lethal while hunting): blind; it hunts any noise (footfalls, landings, dashes). With nothing to go on it throws a stone, and anything moving near where it lands gives itself away. **Stand still.**
  - **Tremor Worm:** a sprint, slide or dash builds up tremor. The floor ahead shudders for 1.2 s (dust, the loaches bolt) before it bursts up through 4 tiles.
  - **Abyssal Arachnid** (apex): after 60 s (then 100 s) of calm it strings 1-2 webs across low tunnels ahead. Each is a sheet from the roof to 20 px off the floor, so **slide under it**. A drained husk always hangs in each web. The strands glint only within 1.5 tiles, unless glow-shrimp fluid is on your suit or a lantern-shroom is near.
- **Flora:**
  - **Lantern-shrooms:** light.
  - **Acid-lichen:** lethal to touch.
  - **Vibration-spores:** deafen stalkers and call the worm.
  - **Cave-cabbage:** slide through it and everything forgets you.
  - **Nerve-root:** pins small animals.

### 6. The Weeds: a sunlit kelp forest (underwater)
> *"Beautiful from above. The seabed is the danger... Bleed here and the Leviathan will come."*

- **Structure** (`BuildWeeds`): kelp floats over urchin carpets, buried electric rays, currents and sea-stack chimneys.
- **The web** (the user's own): phytoplankton → kelp seahorses → barracuda → mermen; mermen and tiger sharks hunt each other; electric rays shock whatever bumps them; the electrified bodies feed bristle-crabs and parasitic fungus.
- **Sizes relative to the diver:** plankton, seahorse, crab and fungus are small; the ray is about 1x; the barracuda is a bit larger; the shark and merman are 3-4x.
- **Roster:**
  - **Leviathan Tiger Shark** (apex): every 1.5 s when hungry it heads for the strongest blood in the scent grid.
  - **Merman.**
  - **Bristle-Crab:** stepping on one makes a crunch every hunter hears.
  - **Goliath Manatee:** a giant mover that follows grazed blood-kelp and clears tangle-vine.
  - **Mimic Octopus-Stalker:** camouflaged on kelp stalks; strikes anything faster than 280 px/s within 2.6 tiles. It can't see you inside an anemone's light or a sardine school.
  - **Harpoon Mantis:** a cliff burrow; a 0.55 s windup with a fixed aim, then a 9-tile harpoon. Pop a nearby air-weed to stun it for 8 s.
  - **Silver-Fin Sardines:** hunters' memory of you fades fast inside the school.
- **Flora:**
  - **Blood-kelp:** 18 s of bleeding scent.
  - **Luminescent anemone:** light; stings crashing predators.
  - **Air-weed:** pop it to launch straight up.
  - **Tangle-vine:** drags.
  - **Spore-pod:** a blinding cloud.

### 7. Atlantis: the drowned city (underwater, dark)
> *"The Phalanx still holds its rows, the gargoyles still watch the mirrors, and the Lost Ones still walk to the temple, listening. Keep moving. Poseidon's Scourge only catches what stops."*

- **Structure** (`BuildAtlantis`): temple halls, grand stairs, broken aqueducts, colonnades, towers and fountain currents.
- **Roster:**
  - **Glyph Wisps.**
  - **Temple Shrimp.**
  - **Anglerfish** (a lure light).
  - **Lost Ones:** hunt by sound.
  - **Phalanx Crustacean:** camouflaged as rubble. Fast movement within 7 tiles in its row triggers a 0.6 s charge glow, then a lethal hard-light beam down the row.
  - **Gargoyle-Moray:** a den ambusher that sees in mirrors; it knows where you are within 5 tiles around corners, unless sun-crystal kelp glare is near.
  - **Orichalcum Leviathan:** a crystal whale running a lane, a transit line you can ride.
  - **Poseidon's Scourge** (apex): wakes 14 tiles behind a diver on the move, closes fast, then keeps pace at 250 px/s, smashing crumbling stone. It catches you if you **stop**, and leaves after 18 s.
  - **Crystal-minnows:** dash through them for a blinding flash; hunters forget you and are stunned.
  - **Mosaic-snails:** their slime makes you slick.
- **Flora:**
  - **Sun-crystal kelp:** glare that blinds the gargoyles.
  - **Prism-moss:** turns a fall into a sideways dash.
  - **Aqueduct-vines:** swing; Up/Down turns it 90°.
  - **Stasis-lilies:** stop everything within 4 tiles for 2 s.
  - **Ruin-spores:** bring the stonework down on your pursuers.

### 8. The Abyss: a 3D vertical descent (`abyss.cpp`)
> *"A vertical trench falling into the black, twice as wide as it looks, lit only by your lamp... The Trench Maw rises from the bottom when it hears you."*

- **Rendering:** fully 3D on raylib's own 3D pipeline (Camera3D), separate from the 2D engine. It's meant to feel **semi-real and terrifying**.
  - A lighting shader on the rock (`ROCK_FS`): the helmet lamp is a spotlight, with fbm relief, strata, slime, wet glints, bioluminescent flecks, and fog that thickens with depth.
  - Creatures are fogged the same way.
  - The diver is a real 3D model: helmet, porthole, a lamp with a visible beam, tank, kicking legs, fins and breath.
- **Structure:** twice as wide as before (trench radius 20, bulging to 24 at caverns), so you can avoid attackers. There's a ledge or alcove every ~18 m, three horizontal cavern bands you must drift across to find the gap, survey buoys every 100 m, and a floor.
- **Movement:** dash, hydro-glide, hydro-parachute and slipstream.
- **Base web:** glass sponges → giant isopods → a gulper eel, plus bioluminescent plankton.
- **The 1.3 roster:**
  - **Whale-Fall Scavenger:** its carcass is cover from suction.
  - **Angler-Cephalopod:** a void-moss lair and a lure; 0.6 s tell.
  - **Pressure-Ghost:** a 10 s cycle: it swells, then pulls you toward it.
  - **Trench-Maw** (apex): after 75 s (then 120 s) of calm it rises from 45 m below with an 8.5 m jaw. A pressure-bulb blast within 18 m drives it back. There's a HUD warning.
  - **Trench-krill:** lit only in your wake along vents.
  - **Slime-hagfish:** slick water makes you fall faster.
- **Flora:**
  - **Vent tube worms.**
  - **Void-moss:** dash through it to stay hidden for 3 s.
  - **Pressure-bulbs.**
  - **Abyssal coral.**
  - **Ghost-kelp:** restores stamina and resets your dash.
- **Test:** `--verify-abyss`.

---

## 8. Boss switches and relic rules

| Level | Boss | Kill | Reward | Off |
|---|---|---|---|---|
| Hull | The Kraken | Stomp its body (optional) | Chance at a relic | `HULL_ARENA_NOBOSS`, no relic |
| Pirate Ship | Blackbeard | Stomp him while dazed (opens the exit) | 1 relic, often 2 (not with checkpoints) | `CABIN_ARENA_NOBOSS`, no relic |

Only bosses can be stomped. Every other enemy is lethal to touch. `--verify` checks both arenas.

---

## 9. Sound

- A long, evolving generative synth track per level (12 layered sections that build on each other), in each level's own style (see [1_Depth_Overall.md](1_Depth_Overall.md) §6). It swells tenser when an apex arrives.
- Every beast has its own voice, death and pain sounds.
- Wind and water sounds where a draft or current exists.
- Subtle movement sounds.
- A soft death sound for the diver.
- `--audio-test` checks for silent voices.

---

## 10. Verification checklist (run after changing generation, movement or beasts)

```
depth.exe --verify                       (must say "All generated levels can be crossed")
depth.exe --verify-moves
depth.exe --verify-beasts
depth.exe --verify-pipe-ecosystem   (and pirate, island, cave, weeds, atlantis)
depth.exe --verify-abyss
depth.exe --play <0-7>                   (runs a dive live)
depth.exe --shots shots <filter>         (hull13, kraken_, pirate13_, island13_, cave13_, weeds13_, fauna...)
```
