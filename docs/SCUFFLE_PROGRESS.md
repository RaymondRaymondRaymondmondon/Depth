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

## Next
Stage 4: multiplayer (host-authoritative snapshots on the arcade session, `G_SCUFFLE` built, eight players on a LAN).
Gate: eight players at 100 ms with no visible correction.
