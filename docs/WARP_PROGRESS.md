# Warp Dodgeball: build log

Arcade game 12 in the Deep Arcade's **Slop** group. The design is `Reference_For_Future_MP_Games/Warp Dodgeball — Game Design Spec.pdf`; its OCR is in `docs/warp_pdf_pages/`.

## Decisions

### The engine
The spec names Godot 4 and Blender. Warp Dodgeball is built inside Depth instead, like every other arcade game:
- C++ and raylib;
- Red Tide's inked renderer;
- the shared crew figure (`fig::PoseFigure`);
- the arcade session for networking.

### Defaults for the spec's open questions
- First-person camera.
- Bots first, then networking.
- Squat is a quick duck (0.4 s, then a 0.6 s cooldown), separate from the held crouch.
- One portal pair per player; your portals close when you're out.
- Players never go through portals.
- Low-poly inked art.
- Catch key: F or the middle mouse button (the OCR lost the spec's key).

### Physics
- **Magnus constant:** 0.0024, tuned down from the spec's starting 0.0035 so a full curve drifts 2.88 m over 20 m. The spec asks for 2.5-3.5 m.
- **Drag:** the spec's ball (Cd 0.47, 0.25 kg, 21 cm) loses about 4% of its speed per metre. A 26 m/s throw reaches 13 m/s at 18 m.
- The catch windows go by speed at contact: 0.35 s below 12 m/s, 0.25 s up to 20 m/s, 0.15 s above that.

### Rules
- **Block:** a ball in your hands blocks any throw under 20 m/s. At 20 m/s or more it knocks your ball out, and you're out.
- **Catch:**
  - A press within half the window before contact catches.
  - At contact, the hit waits half the window for a late press.
  - An earlier press inside the 0.6 s lookahead is a fumble, and you're out.
- **Rebound-out:** your own live ball that went off a wall or through a portal gets you out. It can't for its first 0.3 s, unless it went through a portal.
- **Rush:** the balls start on the centre line. A ball grabbed in the rush must be carried behind your attack line (3 m) before it can be thrown.
- **Timeout:** the team with more players standing wins the round. A tie goes to sudden death: one ball and no portals.
- **Centre line:** feet over it is an out.

### Arenas
- **Classic:** 18 x 9 m with 2 m of run-off. Portal panels cover both end walls, the upper halves of the side walls, and the ceiling.
- **Extreme:** 30 x 16 m, mirrored:
  - three low covers per side, with portal-able backs;
  - two pillars with one portal face each;
  - two nests on 3.5 m decks, each with a ladder;
  - two deflectors;
  - two floor pads.

## Code
- `warp.h`, `warp.cpp`: the headless core at a fixed 120 Hz. Every number comes from `data/warp/warp_config.json`.
- `warp_bots.cpp`: the bots, `--warp-test` and `--warp-sim`.
  - Bots steer round the pieces, fetch balls and carry rush balls back.
  - They aim with lead and drop, and curve some throws.
  - They catch by skill, or else squat or dive.
  - Pro bots open a portal behind themselves and one on the ceiling over a target, then throw into their own wall.
- `warp_net.h`, `warp_net.cpp`: `WarpHost` on the arcade session, with snapshots at 30 Hz.
  - Seats alternate teams; empty places are bots.
  - The snapshot is one templated Visit.
  - Events travel as the newest 12 with a running count (`World::evCount`).
- `warp_game.cpp`: `Scene::Warp`, the first-person scene.
  - **The court:** walls, light-grey portal panels, team-tinted halves and lines.
  - **Portals:** rims of glowing segments, and a swirling face when the pair is open.
  - **Figures:** the shared crew figure in team colours.
  - **The ball:** a trail, and a viewmodel ball in your hand.
  - **HUD:** a charge ring, a catch ring that turns green inside the window, portal pips, a scoreboard and a kill feed.
  - **When you're out:** you watch from your end, with your place in the return line.
  - **Guests:** they draw smoothed positions, and live balls fly on between snapshots.

## Checks
- `depth.exe --warp-test`: the milestones' acceptance tests, all passing. They cover:
  - movement numbers and the throw curve;
  - 2 x 1000 hard throws that never tunnel through a wall;
  - the 2.5-3.5 m curve drift;
  - the catch windows, fumbles, late catches, the block and the knock-out;
  - portal transit (speed, direction and spin carried through) and rebound-out;
  - 4v4 bot matches in both arenas.
- `depth.exe --warp-sim <matches> <perTeam>`: about 3-7 minutes and 4 rounds a match, every match finished, a few portal transits per match.
- `depth.exe --warp-net-test`: the snapshot round-trip and each event delivered exactly once.
- `depth.exe --net-loop warp [lagMs] [mem]`: a host and three guests play a whole match and agree on the score. Snapshots are about 1.5 KB. It passed over loopback UDP and in memory.
- Shots: `warp_rush`, `warp_portals`, `warp_extreme`, `warp_catch`, `warp_bench`, `warp_won` and `arcade_warp`.

## Not done yet
- **Practice mode:** the spec's arc preview (`World::practice` exists but nothing draws it).
- **Gamepad:** controls are mouse and keyboard only.
- **Art and sound:** no music of its own (it uses shared cues), and no crowd or arena dressing.
- **Bots:** they don't climb the Extreme's ladders to the nests.