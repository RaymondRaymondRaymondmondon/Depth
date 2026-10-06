# Ball Pit Brawl: build log

Arcade game 13, Fighting group. The spec is `Reference_For_Future_MP_Games/Ball Pit Brawl — Game Design Spec.pdf`
(OCR and page images in `docs/ballpit_pdf_pages/`). The user (2026-10-06): "build this until you are done", no
questions; the open questions below were answered with the spec's own defaults.

## Decisions (the spec left these open, or the OCR lost a cell)
- **Engine:** built in Depth like every other arcade game (the spec names Godot, as Warp Dodgeball's did).
- **Cannon balls are instant knockouts** (the spec's default), limited by the arc, the exposed seat, overheating and the
  shared supply. Thrown balls 25 m/s, cannon 40 m/s; darts 14-30 m/s. Balls are deadly until their first bounce.
- **Bots first, then online:** both are in (bots fill every match; the arcade session carries up to 12).
- **The knife** is a melee weapon with backstab knockouts (the spec's default).
- **Streaks count all score** (objectives included), the spec's default.
- **Gun table cells the OCR lost:** the dual mini blasters cost 450 with a 12-dart magazine (2 x 6); the other costs
  follow in order (pump 600, burst 750, flywheel 900). Reload times not in the table are 1.2-3.0 s by size.
- **Fall damage:** 30 per metre beyond 3 m (the spec only says 3 m is safe).
- **Health regeneration:** 20 per second after 4 s without damage.
- **Disarm the bomb:** sides swap from round 6; team 0 attacks first.

## What's built
- `src/ballpit.h/.cpp`: the headless core at 60 Hz. The 60 x 30 m hall built from a kit on a 1 m grid
  (`MakeArena`): two team towers (level 2 base with spawn and store, level 3 with two cannons, the roof with the flag),
  the central structure (levels 2, 3 and the roof with two neutral cannons), galleries along both long walls (netted
  on the atrium side, punching bags and rollers), a rope bridge and a net tunnel at level 3 each side, four tube slides
  and two spirals, two crawl tunnels, five ball pits, the conveyor and its hubs. The cover table (`BlocksShot`,
  `BlocksSight`): panels and pads stop everything, nets stop shots but not sight, frames and rails stop neither.
- The traversal table (`StepPlayer`): every speed is the spec's and `--ballpit-test` measures them.
- Weapons: the eleven foam guns (`data/ballpit/ballpit_config.json`), the knife, the vacuum, darts that drop and stay
  on the floor 60 s (cap 400, oldest first).
- The ball loop: pits (instanced looks, real balls only in flight), throwing, cannons (6/s, overheating, arcs), the
  belts and the lift (8 s, emptiest hopper first, overflow to the central pit). `World::BallsTotal()` is constant.
- Score/cash/streak, the store (guns, darts, the disarm kit, the joke shelf), assists, the six streak rewards (refill,
  health box, kids, RC car, drone, tank), the eleven joke items, the four modes.
- `src/ballpit_bots.cpp`: a navigation graph (a 1 m grid on every floor, drops, ladders, slides), A*, bots that shop,
  fight with lead and drop, throw balls, man cannons, vacuum, and play every mode's objective. The milestone tests.
- `src/ballpit_net.*`: `BallPitHost` on the arcade session, a per-viewer snapshot (floor darts only near you).
- `src/ballpit_game.cpp`: the scene (one static mesh for the hall, the pits as ball mosaics plus real balls, cannons,
  people on the shared figure, the blasters in your hands with the first-person hands, the HUD, the store, the
  loadout, the scoreboard).

## Checks
- `depth.exe --ballpit-test`: the spec's milestone tests 1-7 (climb to level 3 and slide down; the speeds;
  submerged immune to darts; 2 knife hits / 3 starter darts / a back stab; pit pickup 0.4 s, a ball KO, a bounced
  ball harmless; the cannon refills only from the conveyor; buying with exact score; the vacuum takes both teams'
  darts; each mode completes with bots and the right winner).
- `depth.exe --ballpit-net-test`, `depth.exe --net-loop ballpit [lagMs] [mem]`.
- `depth.exe --ballpit-sim <matches> <mode 0-3> <players>`.
- Shots: `--shots shots bp_` (atrium, bridge, cannon, store, submerged, slide, ground, over, rewards, flag, overview,
  down, bomb) and `arcade_ballpit`.
