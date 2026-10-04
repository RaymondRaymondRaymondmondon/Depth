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

## Next
Stage 2: crates, the first 12 weapons (and the block window), the wall, rounds and matches in the engine, bots that
use weapons. Gate: four bots finish a 10-round match.
