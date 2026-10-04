# Scuffle (arcade game 9): build log

Design: `Reference_For_Future_MP_Games/Scuffle — Arcade Game 9 Design Document (a Stick Fight game).pdf` (24 pages; OCR
in `docs/scuffle_pdf_pages/`, the page images gitignored). A 2D physics brawler for 2-8 players: stick figures on small,
dangerous one-screen stages, weapons from the sky, the last stick standing wins the round.

The user is away (order of 2026-10-04: build it after A Night Off, don't stop). Calls made where the doc is open:

- **The doc's open questions (p. 24):** friendly fire in Teams on, with a lobby toggle; the wall on by default in
  Custom; editor codes kept to Scuffle's lobby; netcode starts host-authoritative with snapshots on the shared session
  layer and the local stick predicted (rollback is deferred and noted here as an open item).
- **The physics is ours** (the doc allows "written in the codebase"): a position-based particle engine (Verlet,
  bones, tile and body collisions) at a fixed 120 Hz with float math in a fixed order. The stick is an active ragdoll
  in two halves: a platformer controller box (it runs, jumps, climbs and collides with the tiles; it's what makes the
  stick controllable) and an eleven-particle stick figure pulled toward an animated pose around it (it's what makes the
  stick funny). Big hits, stuns and death let go of the pose; the controller then follows the pelvis until the stick
  gets up. The doc's "6-body ragdoll" is drawn as six limbs (head, torso, two arms, two legs) with elbows and knees.

## Stage 1: the ragdoll (done)
- `scuffle.h/.cpp` (headless, namespace `sf`): `Stage` (32 x 18 tiles of 0.6 m, `StageFromText`), `World`
  (`Init`, `Step` at 120 Hz, `Hash`), `Stick` (controller + `pt[J_COUNT]` particles), `Input` (all play, bots too).
- Movement per the doc (p. 2): run 8 m/s with a lean; a 3 m held jump (a tap is about 1.3 m); hold toward a wall to
  climb 3 m/s for 1.5 s, then slide; a wall jump at 45 degrees; duck (the head under the shot line, no moving); dive
  (a headfirst dash that lands prone, slow to get up); any input while ragdolled gets up in 0.4 s once the body is
  still; a 10 m fall stuns for 0.5 s; off the stage is death.
- Fists (p. 3): the jab (10, about 1.5 m), every third in a row a haymaker (20, about 3 m, a ragdoll), the kick
  (fire while diving: 15, 4 m), the grab (fire held against someone: a tap still jabs) and the throw (a thrown stick is
  a projectile: 15 to both). The block window (the first 0.15 s of a punch) arrives with bullets in stage 2.
- 100 HP; low health wobbles and pulses red; the dead stay as grey ragdolls on the stage.
- `BotInput`: a fists-only bot (Stumble, Scrap, Sharp): the nearest living stick, closing, punching in reach, jumping
  walls and small gaps, not walking off edges, ducking a punch now and then.
- The scene (`scuffle_game.cpp`, `Scene::Scuffle`): a match against bots on the stone stage, first to 5/10/20; a 1 s
  countdown, the fight, the winner's pose, the scoreboard of pips; Depth's ink (coloured sticks with an ink outline,
  round joints, the face of two dots and a line, crosses for dead eyes; inked, hatched stone on parchment; ink
  splashes for hits and deaths); the camera frames the living sticks (95 px a metre at the closest, the whole stage at
  the widest). Keys: A/D, W or Space (hold for height), S (duck; dive in the air), the mouse aims and clicks (J
  punches straight ahead), T taunts; the game menu has "Leave the fight". The arcade's reel 9 (`G_SCUFFLE`; not yet
  hostable) has the solo menu: bots 1-7, Stumble/Scrap/Sharp, first to 5/10/20.
- Checks: `depth.exe --scuffle-test` (16 checks: standing, running, the jump, the wall, the jab and the haymaker,
  ducking under a jab, the kick, the grab and throw, death, falling out, the 10 m stun, and two Sharp bots finishing
  rounds on the stone stage, about 25 s each); `--scuffle-determinism <seed>` (four bots for 40 s, twice: identical);
  `--scuffle-sim <players> <rounds>` (round length, wins by skill, deaths by cause). Shots `scuffle_fight`,
  `scuffle_haymaker`, `scuffle_match`, `arcade_scuffle`.

## Stage 2: crates, the first twelve weapons, the wall, matches, armed bots (done)
- `data/scuffle/scuffle_weapons.json`: all 48 weapons (the doc's 44 and four of Depth's own: the Tesla gaff, the flare
  pistol, the speargun, the dealer's bottle) with damage, rate, ammo, knock, recoil, speed, spread, headshot multiplier,
  explosions, and the build stage that switches each on; the arsenals' weights; crate and wall timings.
- `scuffle_arms.cpp`: crates on parachutes (the first at 3 s, then every 5 s; shoot the parachute and it drops; a hard
  fall crushes for 30; it opens on touch into the hand, or where it lands if it was shot); one weapon in hand (an empty
  hand picks one up; duck on another to swap); guns (pellets, spread, recoil that moves the body, the minigun's
  spin-up, headshots, the sniper's pierce, the harpoon's pin, bouncing fused grenades, rockets); melee swings (the
  cutlass's timed block); the block (a punch thrown at a bullet in the last 0.15 s sends it back along the punch; the
  frying pan stops everything); an empty gun is thrown (10); explosions; the wall (at 45 s the bulkheads flood from
  the bottom: half the stage by 60 s, all of it by 70 s; the finale's at 30 s).
- The stage-2 dozen: gas pistol, twin pistols, needler, carbine, scatter gun, harpoon rifle, sniper, minigun, grenade
  launcher, rocket launcher, cutlass, frying pan.
- `scuffle_match.cpp`: `Match` (the playlist shuffled by the seed, the countdown, the fight, the winner's pose; first to
  N; match point is a finale with the wall at 30 s; the doc's scoring: round 100, kill 20 (+10 knocked into a hazard,
  +20 by a block-deflect), survived to the wall 10, match 300). Three stone stages: the Stone Yard, the Steps, the
  Bridge. `--scuffle-sim <players> <rounds> [classic|melee|chaos|snakes|random]`.
- The bot now fetches crates and guns, keeps a weapon's range, leads its aim (skill sets the error), fires on line of
  sight, throws an empty gun, blocks bullets with a punch (Sharp), steers clear of bottomless pits, and climbs above
  the flood.
- The scene runs the engine's `Match`; it draws crates and parachutes, the weapons (in hand and loose), bullets,
  explosions, blocks and the flood; the HUD shows points and your weapon's ammo. Shot `scuffle_flood`.
- The gate: four Sharp bots finish a 10-round match (`--scuffle-test`, about 38 s a round). `--scuffle-sim 4 60`:
  39 s a round, no draws; deaths: falls 30%, the wall 26%, guns about 35%, fists and kicks 13% (the doc's targets
  are weapons 55%, hazards 30%, fists 10%, the wall 5%: the balance pass is stage 9).

## Stage 3: the Nautilus world, its hazards, codes and the editor (done)
- Tiles: stone, wood, ice (accel x0.12: you slide on), glass (breaks 0.6 s under weight), rope (one-way: land from
  above, hold down to drop through), electrified rail (live 1.6 s in 4: a touch kills), conveyors < and >. A 0.36 m
  step-up so sticks don't snag on one-tile ledges. Wrapping stages (off one edge, in at the other).
- Pieces (`Piece`, `scuffle_pieces.cpp`): pistons (slam on a rhythm, push, crush against stone), elevators (carry
  riders), steam vents (an updraft column while live), the propeller (sucks in, shreds), the torpedo tube (fires a
  stick who walks into its mouth across the stage), windows (glass that blows out at a set second).
- The text format (`scuffle_stage.cpp`: letters `# w i g - e < > S C P L V F T W`, `@n key=value` piece lines),
  codes (`StageToCode` "SCF1-..." and packs "SCP1-...": compressed, base64, an FNV checksum checked before
  decompressing, so garbage is refused instead of crashing raylib's inflate), `CheckReachable` (a BFS over landings
  with the real jump arcs: every spawn must reach every other).
- The Nautilus world (`scuffle_packs.cpp`, `BuildNautilus`): 20 layouts and their mirrors = 40 stages, written to
  `data/scuffle/stages/nautilus.txt` by `--scuffle-build-packs`; all 40 pass `--scuffle-verify-all`. The match's
  playlist is that pack (the stone stages are the fallback).
- **The editor** (`scuffle_editor.inl`, included at the end of `scuffle_game.cpp`; the arcade's Scuffle panel, "The
  editor"): paint tiles (left), clear (right), drag out a piece, place spawns (up to 8) and crate zones, select a piece
  and tune its numbers (dx/dy, travel, period, phase, on, power), rename, the world, wraps, New, the built-ins (< >, to
  see how they work), Mirror, Check, Copy code (to the clipboard), Paste code (a stage or a pack), Save (to
  `scuffle_library.txt` next to the exe, gitignored), Library >, Play (P: a match with bots on it; P again or "Back to
  the editor" returns with the edits). A stage only saves or shares once every spawn reaches every other.
- Checks: `--scuffle-test` (stage 3 adds the code round-trip, a garbage code refused, reachability failing on a cut-off
  spawn, pistons, the elevator, vents, the rail, the propeller, the tube, glass, rope, ice, conveyors, wrapping),
  `--scuffle-verify-all`, `--scuffle-verify <code>`. Shots `scuffle_editor`, `scuffle_editor_engine`.

## Stage 4: multiplayer (done)
- **The arcade seats eight now** (`arcade::MAX_PLAYERS` 8, `PROTOCOL` 4; every other game still caps itself through
  `Info().maxPlayers`; the lobby draws slimmer rows when a game seats more than six; two more AI seat names).
- `scuffle_net.h/.cpp`: the host (`G_SCUFFLE` is built; 30 snapshots a second) runs the real `Match` at 120 Hz. Every
  person's play, the host's own included, is a stream of numbered, quantised inputs (`InputFrame`, `QuantizeInput`,
  `WriteInputs`: moves as signed bytes, the aim as a 16-bit angle, jump/fire/taunt bits). The host plays each seat's
  inputs in order, one a step (a backlog over 12 is caught up by dropping the oldest), and a seat that goes quiet for
  1.5 s, or an AI seat, is fought by the bot. Each snapshot is the whole match (one templated `Visit`: the stage with
  its live tiles and pieces, every stick's every field, items, bullets, the last 32 events) for one viewer, with the
  number of that viewer's last played input (the ack); compressed and checksummed (about 2.6 KB).
- **The guest predicts** (`Predictor`): it steps its mirror with its own inputs at once; on each snapshot it starts again
  from the host's match and replays the inputs the host hasn't played yet. Everyone else is carried on their last
  movement but never their last attack (a punch or a shot the host hasn't seen would land here and be taken back).
  The scene then glides whatever the snapshot moved (`S.vis`, fading in about a tenth of a second); the splashes come
  only from the host's events.
- The scene: `StartScuffleNet` (the arcade starts it for host and guests), names from the session over the sticks and
  on the scoreboard, your box lit, "Waiting for a lost player", the match's end (the host: Rematch / Back to the lobby;
  a guest: Leave the table), the game menu keeps the session talking (`ScuffleMenuTick`: your stick stands and the bot
  takes over after 1.5 s). The lobby: the host picks first to 3/5/10, the arsenal, and the AI seats' skill.
- Calls made: a lost player doesn't pause the fight (`pauseOnLost` false; the bot stands in at once, and the player
  can rejoin with their token); friendly fire, teams and modes arrive with stage 7.
- **The gate:** `depth.exe --net-loop scuffle 100` (GameNetworkingSockets on loopback, 100 ms added to every packet
  each way: 200 ms round trip, harsher than the doc's "100 ms"): a host and seven guests whose inputs come from a
  bot's mind on their own mirror; the match is played out and every guest agrees on the rounds and the champion.
  Own-stick corrections with no one in reach: 99th percentile 0.000 m, worst 0.03 m, none over 10 cm. In a fight
  (someone within 3 m, a shot or blast near, or hit), corrections are real and unavoidable at that lag (another
  stick's next punch can't be known); the scene glides them.
- Checks: `--scuffle-net-test` (inputs round-trip quantised the same on both ends; the mirror writes back byte for
  byte; a packed snapshot reads to the same world; the mirror steps exactly like the host; damaged or cut-short
  snapshots refused; a guest's numbered inputs drive its stick with zero correction; hello names; a quiet guest's bot),
  `--net-loop scuffle [lagMs] [mem]`. Shots `scuffle_guest`, `arcade_lobby_scuffle`. `DEPTH_NETTRACE=1` prints every
  large correction made while free.
- For stage 9 (internet play): snapshots are full floats at 30 Hz, about 80 KB/s for each guest; quantising the
  particles would roughly halve it.

## Stage 5: the other five worlds, the generator, the finales (done)
- **Tiles:** crumbling floor `c` (gone 0.7 s after the first step), the Cave's crystal `x` (shatters into shrapnel when
  shot or blasted), the Reef's urchins `u` (sting and throw you off), water `~` and the Void's brine `b` (not solid:
  you swim, a stroke a jump press, sink gently; drown after 8 s with your head under, 3 s in brine; only the harpoon
  and the tesla gaff fire underwater), the Salon's bar `=`.
- **Pieces** (`scuffle_hazards.cpp`, each with a tell before it hurts and its own cause of death):
  - the Cave: stalactites (fall when shot), acid drips, the slipstream (a current; on a count it's the Reef's surge),
    the toad (its tongue from the pool), the Lobster's claw;
  - the Reef: reacher coral (holds you 1.2 s), eel holes (bite what's in front), sharks in the water below (0.5 s),
    the kraken;
  - Atlantis: grates (the Wyrm strikes up), the tuna lane (a ram), the sluice (floods a terrace on a cycle), columns
    (topple when shot, crush what's under the arc, lie as rubble);
  - the Void: the leviathan's lure (pulls), low gravity pockets, the sand worm's line;
  - the Salon: the crowd (bottles), the dartboard, the pool table (balls), the bouncer (anyone standing on the bar
    is thrown out), the dog.
  Any piece can sleep until a second (`Piece::start`): that's how the finales' set pieces wait.
- **Walls** (`StepWall`, per world): the Nautilus floods (kills); the Cave's ceiling comes down (kills); the Reef's
  tide comes in and Atlantis sinks (water you swim in and drown in); the Void's abyss widens from its side and takes
  the floor; closing time in the Salon, the bouncer clearing the room from the door.
- **The generator** (`GenerateStage`, `scuffle_build.cpp`): the world's ground (decks and pits; floor and ceiling;
  islands over the sharks' water; terraces; a floor with the abyss on one side; a closed room), 3-7 platforms on a grid
  in the world's materials, 1-3 of its hazards and 1-2 movers, eight spawns as far apart as it can, and the
  reachability check with the real movement code (a layout that fails is redrawn).
- **The stages:** each world's forty is its three signature stages (hand-built: the Chimney, the Slipstream, the
  Lobster; the Bommie, the Flats, the Drop-off; the Plaza, the Baths, the Cisterns; the Rim, the Overlook, the
  Reactor; the Bar, the Pool Table, the Yard) and a mirrored variant of each, plus seventeen from the generator
  (seeded per world, so the pack is the same each time it's written) and their mirrors. **A call made:** the doc
  asks for 40 hand-built stages a world; the signature stages are hand-built and the rest come from the generator,
  each checked, named, and written in the editor's text form so they can be opened and improved by hand.
  Every world has **three finales** (64 x 36, twice the size, the wall at 30 s, the set piece at 12 s: the propeller
  starts, the Lobster rises, the kraken reaches up from the drop-off, the Wyrm surfaces in the plaza, the leviathan's
  lure appears, closing time with the whole bar thrown in). Match point is always a finale. `--scuffle-build-packs`
  writes `data/scuffle/stages/<world>.txt` (about 45 KB each); `--scuffle-verify-all` passes all 258.
- **The text form** now writes each piece on its own `+<letter> x= y= w= h= ...` line, so a current or a pocket can
  lie over tiles (painting the letters over the grid lost the platforms under them). Letters in the grid still read.
- **The match's worlds:** the solo panel and the host's lobby pick all six worlds, one world, or endless (a fresh
  stage from the generator every round).
- **The look** (`scuffle_worldart.inl`): each world's paper and backdrop (the Cave's umber rock and glow-mould, the
  Reef's sun shafts and fish, Atlantis's colonnade, the Void's dark and the pale abyss, the Salon's panels, bottles and
  lamps), each world's stone, the new tiles, every piece and its tell, water drawn over the sticks, the walls, and
  the hazards' projectiles (shrapnel, bottles, darts, pool balls, acid). Pale ink and names in the dark worlds.
- **The gate** (`--scuffle-test`, stage 5's 29 checks): water, brine, low gravity, crumbling floors, urchins, every
  piece, "each world's wall closes in after 45 s" and "each world's finale set piece sleeps until its second, then
  kills". Shots `scuffle_world_<world>`, `scuffle_wall_<world>`, `scuffle_finale_<world>`.
- `--scuffle-sim 4 120` across all six worlds: rounds are 18 s (the new pits, the abyss and the sharks), falls 37% of
  deaths: for the balance pass (stage 9).

## Stage 6: the full arsenal, gear, trinkets, mutators, events (done)
- **All 48 weapons are in the crates** (`scuffle_special.cpp`, `scuffle_arms.cpp`):
  - What they do to a stick (`StepStatus`): burning (12 a second; water puts it out; wood catches and spreads), frozen
    (a statue, then a ragdoll; a hit shatters it for 20 more), bubbled (carried up and along, can't act; a hit pops it),
    netted (held 3 s), flipped (the gravity gun: up is down while the beam is on you; past the top is "fell into the
    sky"), trapped (a bear trap holds you until you're hit free).
  - What they leave (`Thing`, `StepThings`): bees (the nearest stick but their owner's, unless the owner is much the
    nearer), snakes (slither and bite; lunge when close; fists and blasts kill them), fish (come to chum in the Reef
    and the Void), the black hole (pulls everything within 6 m for 4 s; the middle is gone), portals (two per owner;
    sticks and bullets go through), a bear trap, a turret (10 s), a mine, a banana peel, a decoy, a spring, a stuck
    charge, beams.
  - The beams (`SpecialFire`): the tesla gun (an instant arc; it chains to two more sticks, and to anyone in the same
    water), the gravity gun, the laser (reflects off ice and glass, cuts ropes).
  - Thrown things fly as bullets with gravity and become things where they land.
  - The melee specials: the trident vaults, the pool cue breaks in two (two weapons), the sledgehammer breaks the
    tile it comes down on, the whip pulls, the oar rows twice as fast, the tesla gaff sparks on and its shock holds
    you, the frying pan cooks bees.
  - Duck and click throws whatever you hold (the axe spins for 60). The boomerang comes back (it hits you if you
    missed). Only the harpoon, the speargun and the tesla weapons work underwater.
- **Gear** (one crate in ten; a second slot on the gear button, E or the right mouse button; `StepGear`): grappling
  hook, shield, jetpack (3 s, recharges on the ground, burns whoever's under you), decoy, parachute, spring, rope.
  **Traps** (one crate in thirty): a snake, bees, or a flashbang. The bots use gear (the jetpack, the parachute and
  the hook out of falls, the shield against a gun, the rest now and then) and lob thrown things and gravity shots.
- **Trinkets** (14 and None, `scuffle_rules.cpp`): each with its edge and its cost, as the doc lists them; picked on
  the arcade's plate (the game picks if you don't), sent with hello, shown over each scoreboard box.
- **Mutators** (17, stackable; Random picks one each round): Low Gravity, Moon Shot, Ricochet, Big Heads, Infinite
  Ammo, One Hit, Ragdoll Royale, Snakes, Hot Potato, Blackout, Giants (x1.6: a true double wedges sticks in the
  stages' gaps), Tiny, Mirror, Vampire, Sudden Wall, Pacifist, Fast Forward. The lobby and the plate pick none, Random,
  or one (the engine stacks any number).
- **Mid-round events** (one round in four, at 10-30 s, a one-second tell and a banner): the Flood, the Reach, Lights
  Out, Crate Rain, Earthquake, Swap, the Dog, Gravity Flip, the Bouncer, Fish Storm.
- **The gate** (`--scuffle-arsenal [reps]`): every weapon against every other on three shapes (open: a platform over
  nothing; a corridor: a short low tunnel; vertical: a shaft of ledges), both sides, each weapon with its own ammo
  (spent, it's thrown, then fists), 30 s (then more health wins). With 3 reps (20,304 duels, about 3 minutes):
  **every weapon wins somewhere.** The four support items (the ink bomb, the portal gun, the banana, the bear trap:
  the doc gives them no damage of their own) are exempt (`"support": true` in the data). The shapes now favour what
  the doc says: knockback and long guns own the open platform, melee owns the corridor, explosives, thrown things and
  the pistols own the shaft.
- **The balance changes the gate needed** (all in `scuffle_weapons.json` unless noted; the doc's numbers were the
  start):
  - Engine rules:
    - a small knock is a small stagger (pistols no longer pin you);
    - a stick just back on its feet can't be ragdolled again for a moment (no stunlocks);
    - melee hits put the target's next shot or swing back 0.35 s (hitstun), and swings step into the blow;
    - your own explosions hurt you at 25%;
    - pellets top up a body's motion instead of stacking (the scatter gun's knock went from 10 to 13 to keep its
      flight).
  - The sniper's 0.5 s scope (it loses point-blank).
  - The frying pan's held block stops bullets rather than reflecting them (only a timed swing or punch reflects).
  - Fire 12 a second; the flamethrower's pellets 10 each over 6 m.
  - The ice gun 10 damage and a 1.4 s freeze. The bubble gun 8 damage, and a bubble disarms.
  - Bees 26 a sting every 0.25 s for 7 s.
  - Snakes 42 on the shot, then 25 a bite. Chum 55. The boomerang 45 (6 to you on a miss). The flare 30 at 1.5 a second.
  - Melee: the tesla gaff 40 with a shock, the fish's reach 1.0, the oar's knock 11, the sledgehammer's swing 0.6.
  - Thrown: limpets 100 in 2.6 m on a 1 s fuse, six of them; sticky bombs 85; mines 90 and four; bottles 45 and four.
  - The harpoon's pin 1 s.
- **The look** (`scuffle_arsenalart.inl`): every weapon's silhouette in hand and on the floor, the things, statuses on
  the bodies, gear in use, burning wood, the screen's ink, flash and dark (Lights Out and Blackout glow only where
  there's fire, a shot or a tell), the Flood's water, the Reach's arm, event banners, the hot potato's count. Shots
  `scuffle_arsenal`, `scuffle_blackout`.
- Checks: `--scuffle-test` adds stage 6 (41 checks: every strange weapon, thrown thing, melee special and gear, and
  gear and trap crates at one in ten and one in thirty) and stage 6b (40: every trinket, mutator and event, and one
  round in four having an event).

## Stage 7: the modes and the Gauntlet (done)
- **The modes** (`scuffle_modes.cpp`, `Mode` in scuffle.h; picked on the solo plate and in the lobby, carried to guests
  as the 7th field of the host options):
  - Classic: the last stick standing.
  - Teams: 2v2 or 4v4 by colour (a ring round the head, a bar under the scoreboard); friendly fire on (the doc), off
    if the lobby says so (`Match::friendlyFire`).
  - King of the Plank: a gold-roped plank, a point a second for a stick alone on it; first to 60; it moves every 20 s
    (3 s of warning); the dead come back after 3 s.
  - The Egg: hold it 30 s in all; the holder only punches; it breaks from a fall (a new one on the highest floor
    under the middle after 2 s).
  - Hot Potato: a bomb passes by touch, goes off at 10 s, then another.
  - Hunt: one Shark (200 HP, a harpoon, no crates, a fin over the head); its kills become sharks.
  - Duel: two sticks, best of 7 on finale stages; three weapons on cards during the count, 1-3 or a click picks.
  - Chaos: a random mutator, a random arsenal and an event each round.
  - Custom: whatever the lobby sets.
  - The Gauntlet: co-op, twenty generated stages that get worse, an exit hatch with a chequered flag, a clock
    (45 s, a second less each stage, never under 25), deaths counted; at the end the local board
    `scuffle_gauntlet.txt` (gitignored; best 50 kept, top 10 shown: more stages, then less time).
- **Bot personalities** (`Persona`): plain, rusher, camper, melee only, taunter.
- **The bots' map** (`Nav`/`MoveVia` in scuffle.cpp): standing cells with walk, step, drop and jump links, searched
  back from the goal by BFS and cached per stage layout and goal; bots use it for the Gauntlet's exit, the King's plank
  and a loose egg. It turned the Gauntlet from 0 stages cleared to clearing them.
- **Fixes along the way**: the egg now spawns on a floor (it used to fall from the top and break every time) and is
  picked up anywhere along the body (it sat at the feet, out of the pelvis's 0.6 m reach); a newly infected shark's
  respawn counts down; mode banners (the plank moving, a stick through the exit) no longer go through the mutator
  event banner (their codes 200/300 were out of its range).
- **Network**: every stage-7 field is in the snapshot `Visit` (stick team/shark/respawn/finished/balloon/persona;
  the world's mode, friendly fire, points, plank, potato clock, goal, start; the match's mode, team size, target,
  Duel offers and picks, the Gauntlet's run), and `Input::pick` rides in bits 4-5 of the button byte.
  `--scuffle-net-test` checks that King, the Egg, Hunt, Teams and the Gauntlet survive a snapshot byte for byte and
  that the mirror stays in step.
- **The gate** (`--scuffle-test` stage 7): each mode finishes with bots (King about 235 s, the Egg about 54 s, Hunt in
  two rounds, the Gauntlet clears a stage), plus King's scoring, the Hunt's shark and the Duel's pick.
- Shots: `scuffle_mode_teams|king|egg|hunt|duel|gauntlet`.

## Stage 8: Boss Arena (done)
- **Mode `MD_BOSS`** (`scuffle_boss.cpp`): everyone against a Depth boss on its own stage. The stage picker picks
  the boss (each world has one: the Cave the Lobster, the Nautilus the Kraken, Atlantis the Wyrm, the Reef the Sun God,
  the Void the Goliath, the Salon the Bouncer); "all six" fights them in the doc's order until one wins. The crew is
  one team (no friendly fire); a fallen stick is back after 4 s until the wall (at 150 s); the boss wins when no one
  is left or coming back. Health grows with the crew (half again per stick past one).
- **The boss** is a headless state machine (`Boss` in scuffle.h): hit circles (`BossPart`: armour takes a quarter,
  the body all, the weak point double) rebuilt from its pose every step; shots, blasts, fists, kicks and swings reach it
  through `BossStrike` (`BossTouch` for a bullet's contact); every attack has a tell and marks where it will land
  (`Boss::danger`: drawn as a hatched band; the bots step out of it or jump a low one); falling things are
  `Boss::marks` (dust and a growing shadow, then a hazard bullet). Three phases at two thirds and a third of its
  health: it roars (1.4 s) and **the stage changes** (`PhaseChange`).
- **The six:**
  - The Lobster: claw sweep along the floor (jump it), claw slam over you (the roof sheds crystal), bubbles; the eyes
    are the weak point. Phase 2: the roof cracks (more crystal). Phase 3: it scuttles forward, the middle rope snaps,
    the low ledge crumbles.
  - The Kraken: the eye surfaces beside the hull (the only real weak point) and sinks; arms rise over the gunwale and
    slam across the deck, or sweep at head height. Phase 2: two arms; the deckhouse is torn off. Phase 3: the deck
    breaks and the eye comes up through the gap.
  - The Wyrm: leaps out of one grate and arcs into another (the grate rattles and bubbles first); rears up spitting
    acid (its head open). Phase 2: acid in the leap, the terraces crumble. Phase 3: double leaps, the cistern floods
    (guns fail below the waterline).
  - The Sun God: fire beams that sweep under you (a thin red line first), sun-rain. Phase 2: both hands; the high ropes
    burn. Phase 3: it comes lower, its core splits open (a second weak point) and every scaffold catches fire.
  - The Goliath: inhales (everything is pulled toward the mouth; holding a wall or a line halves it; inside, it chews;
    then it snaps shut), spits a fan of bubbles, lurches forward. Its eye is the weak point, and the open mouth.
    Phase 2: the ledge before it goes. Phase 3: the low rope goes too.
  - The Bouncer: grabs and throws whoever is close, lobs stools, charges to the wall (dazed after: his head takes
    triple), stomps a shockwave. Phase 2: throws the tables. Phase 3: brings the chandelier down.
- **The bots** (`BossBot` in scuffle.cpp): out of the danger marks first (a jump over a low sweep), away from
  hurting parts, armed up, then the weak point from the weapon's range (or up close with fists and blades).
- **The look** (`scuffle_bossart.inl`): each boss inked from its own state (so a guest draws what the host sees),
  the tells, the health bar with phase notches and the roar line; the camera frames the boss too.
- **Network**: the whole `Boss` is in the snapshot `Visit`; `--scuffle-net-test` round-trips a Boss Arena match.
- **The gate** (`--scuffle-test` stage 8): every arena crossable; the weak point doubles and armour quarters; a real
  shot and a blast find it; the phases change every stage; **a duo of Sharp bots beats the Lobster** (7 of 8, about
  40 s); four Sharp bots beat each boss (about 50-80 s, 3-11 deaths a fight) reaching phase 3; one Stumble bot alone
  loses to the Kraken.
- Also fixed: the event banner carried over between matches; a team's, the sharks' or the crew's round win showed
  "A draw" on the round's banner.
- Shots: `scuffle_boss_<lobster|kraken|wyrm|sungod|goliath|bouncer>[_late]` (`DEPTH_BOSST` = the second to
  catch it from).

## Next
Stage 9: cosmetics (skins and hats; hats fly off), the crate, replays, sound, internet play, the balance pass (the
doc's targets: round length 25-45 s with four, 30-60 with eight, draws under 8%; deaths weapons 55%, hazards 30%,
fists and throws 10%, the wall 5%; no weapon over 12% of kills on Random). Gate: all targets met or logged.
