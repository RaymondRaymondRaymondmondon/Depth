# Fowl Play: build log

Arcade Game 11, on the Deep Arcade's **Slop** shelf. The design document is `Reference_For_Future_MP_Games/Fowl Play — Arcade Game 11 Design Document.pdf`; its OCR is in `docs/fowl_pdf_pages/`.

## Decisions (defaults taken; the user can change any of these)
- **Engine.** Built inside Depth like the other arcade games: C++/raylib, Red Tide's inked renderer and the shared crew figure. Everything is drawn in code.
- **The four open questions** (the doc's last page):
  - Shooting another player's gun is **cosmetic**: it knocks their hat off.
  - The mystery-gun machine **never** gives the Zapper.
  - The UFO is a **once-per-match** event in the final round.
  - Money **resets** each match.
- **Prices the OCR lost:** the Light Machine Gun is $600 and the Belt-fed Heavy $700. Both are marked as guesses in `fowlplay_guns.json`.
- **The Golden Hour** (round 15) doubles each bird's **pay**, not its count. Doubling the count would make one round decide the match.
- **Slots.** The doc gives "a random gun" as the prize for three ducks, three dogs or three guns. At a $10 bet that prize is cash (x6.5) instead. At $25 it is a gun priced at most 8x the bet, and at $50 at most 10x. This keeps the house edge near 8% at every bet: returns are 0.93 / 0.92 / 0.87 over 10,000 pulls each.
- **Keys.** Mouse aims and shoots, right mouse scopes or fires both barrels, A/D steps inside the stall, Q/E leans, Ctrl crouches, R reloads, 1/2 or the wheel swaps guns, and H swaps with the hook.
  - V (held) wipes the lens smudge; the doc only said "hold a key".
  - T scratches a ticket from your pocket.
  - Between hunts, WASD walks the room and E uses a station.

## Code (namespace `fp`)
- `fowl.h`, `fowl.cpp`: the headless core at 60 Hz.
  - The data loads from `data/fowl/fowlplay_*.json`; the file names are the doc's seven.
  - **Birds:** 16 kinds with their own flight patterns (zigzag, skim, high, flock, erratic, dive, glide, convoy, loop, arc, ground, ufo), plus the effects that steer them (bread, honk, plunger, blower, repellent).
  - **Shooting:** hitscan bullets with spread, falloff past the gun's range, and the bird-seeker sticker. Shots also hit the cardboard moose, the dog and other players' hats. The killing shot gets the bird.
  - **The fun guns:** projectiles (crab, beam, net, bread, boomerang, bubble, firework, plunger, banana, flare) and streams (magnet, blower). The Honker, the Lightning Rod's chain and the Mega Zapper's cone are instant.
  - **The dog:** it flushes, retrieves in an order (the Dog's Bone first, the dog-shooter last, a Dog's Grudge never), laughs, and holds the birds up at the tally.
  - **Rounds:** waves, perfect waves, a bonus wave of clays every third round with the grey pistol for everyone, the tally, and the intermission with its bell, patience interest and stall gates.
- `fowl_shop.cpp`: every purchase is a `Command` the host applies. It covers:
  - the gun counter with Mr. Zappa's per-player deal and the pegboard;
  - the mystery machine;
  - the slots (house edge) and the scratch-offs;
  - the Slop Shop: match cosmetics, 14 sabotage items with their counters, at most two per target, +50% on the leader, and the Menace title.
- `fowl_bots.cpp`: the bots, `--fowl-test`, `--fowl-sim` and `--fowl-gamble-sim`.
  - Bots pick targets by value and angle, aim with lead and drift, and wipe smudges.
  - In the room they walk through the gates to the stations to buy, gamble a little, counter what's coming, and grief the leader when rich.
- `fowl_net.h`, `fowl_net.cpp`: `FowlHost` on the arcade session.
  - Snapshots go out at 20 Hz, as one templated Visit. Birds that have gone for good aren't sent.
  - Guest shots carry `lagSteps`, and the host rewinds birds up to 150 ms (`Bird::hist`).
  - Bots fill the empty stalls.
- `fowl_art.cpp`: the art:
  - the marsh at golden hour: the sun, reeds, dead trees, a treeline and the hill;
  - the porch's six stalls: posts, rail, partitions, hooks, flags, glitter, the cardboard moose and the bagpiper;
  - the clubhouse room: the glass case, the pegboard, Mr. Zappa, the gumball machine, six slots, the scratch counter, the raccoon's neon booth, the trophy wall and the bar;
  - the birds, with flapping wings, eyes (decoys have none) and wide-eyed hits;
  - the dog, in its outfits;
  - the toy guns: two-tone grey moulded plastic, red trigger, ribbed slide and the red Zappa stamp, each in its real silhouette;
  - the projectiles and the other shooters.
- `fowl_game.cpp`: `Scene::Fowl`.
  - **First person:** the viewmodel gun kicks and reloads; the scope zooms.
  - **The 8-bit strip:** round, bullets, the wave's birds and score.
  - **The board:** each stall with its sabotage pips.
  - **Panels:** the room's stations, and the tally board.
  - **The podium:** the end-of-match stats.
  - **Tokens:** paid at the end. The locker (40 permanent cosmetics in four tiers, plus a crate) is saved in `fowl_profile.txt`.
- `sound_fowl.inl`:
  - the chiptune march, which speeds up each round;
  - the clubhouse piano with the Slop jingle;
  - the bagpiper (Loud Neighbor);
  - the marsh, room and night beds;
  - 24 effects (toy-gun shots by class, bird calls, the dog, slots, the bell and fanfares).

## Checks
- `depth.exe --fowl-test`: all pass. It covers:
  - the arsenal counts, Zapper credit and pay, a stolen goose, the armored belly, decoys and the dog;
  - each fun gun's effect;
  - the slot edge at every bet;
  - sabotage: the leader markup, sold out, counters, early reload, the lawyer;
  - a full 15-round bot match in which the bots shop.
- `depth.exe --fowl-sim <matches> <players> <skill>`: the balance targets.
  - A solo mid-skill bot shoots 8.4 birds a round in rounds 1-3. The target is 6-9.
  - It shoots 19.1 a round in rounds 10-12. The target is 20-30; a top bot gets 30.7.
  - No fun gun takes more than about 7% of kills.
  - The heaviest gambler rarely wins.
- `depth.exe --fowl-gamble-sim [pulls]`.
- `depth.exe --fowl-net-test`: the byte-identical snapshot round-trip over two minutes.
- `depth.exe --net-loop fowl [lagMs] [mem]`: a host and five guests play Quick Draw to the podium. Guests' shots land, and everyone agrees on the board. Snapshots are 4-8 KB.
- `depth.exe --audio-test`: the six Fowl states, and every effect non-silent.
- Shots: `fowl_hunt`, `fowl_room`, `fowl_counter`, `fowl_tally`, `fowl_slop`, `fowl_sabotage`, `fowl_podium`, `fowl_round13`, `fowl_bonus`, `fowl_night`, `fowl_locker`, `arcade_fowl`.

## Not done yet, or simpler than the doc
- Victory dances are bought but not animated at the podium.
- The Duck-call underbarrel and Laser pointer are passive stat attachments; the duck call's "call ducks to you" button isn't wired.
- Gamepad.
- Practice mode is the normal rounds with free guns, not a bird picker.

## Playtest fix (2026-10-05): the machines
The user said the gumball and slot machines didn't look like well-made machines. They're now modelled in Blender (`tools/artgen/fowl_props.py`, output in `assets/fowl/`) and drawn with the PBR path.
- **The gumball machine** (the mystery-gun machine, 2.7 m):
  - a fluted cast-iron pedestal on a flared foot;
  - a red body with chrome bands, a coin plate and a price plate, and a chute with a flap;
  - a glass globe packed with 170 gumballs, and a red cap with a knob.
  - The crank (`gumball_crank.glb`) turns while a capsule is coming.
- **The slot row:** six vintage one-armed bandits on wooden stands.
  - The cabinets have chrome side rails, gold pinstripes, a rounded crown with a marquee and a ridge of lamps, a chrome bezel round three reel windows under glass, a jackpot window, a coin slot and a payout tray.
  - The arm (`slot_handle.glb`) drops on a pull.
  - Machine k shows player k's own reels: they roll in turn and stop on the result.
  - The crown's lamps chase, and flash on a win.
- **New shots:** `fowl_gumball`, `fowl_slots`.
## Playtest fix (2026-10-05)
- **F picks up a gun from the floor**, apart from E (the stations). Before, E opened the nearest station first, so a dropped gun beside the counter couldn't be taken. The prompt and the How to play page say so.

- Visual polish (2026-10-05): toy guns are built from rounded moulded parts (`RoundCube`, `gRoundBoxes` in fowl_art.cpp); the marsh's dead trees, branches, lily pads (with flowers) and the floating log are round now.
- The clubhouse furnished (2026-10-05): `tools/artgen/fowl_props.py` now also builds the glass gun case, the pegboard with hooks (the packs hang as printed cards), the scratch counter and ticket dispenser, the Slop Shop booth (striped awning, curtain), the trophy wall (cups, a mounted decoy), the bar (bottles, taps, stools), turned porch posts and beadboard partitions, and `clubdecor` (beams, chair rail, duck prints, two dusk windows, a moose head, a decoy shelf, pendant lamps, a rug, a pot-bellied stove with firelight, a coat rack). The old boxes remain as the fallback.
- The marsh's own life (2026-10-05): frogs on the lily pads (one hops now and then), two grey herons wading far out that stab at the water, dragonflies darting over the reeds by day and fireflies blinking by night; the stall plaques are enamel with round pips, the ammo tray holds real cartridges.

## Playtest fixes (2026-10-06)
- The Slop Shop was rebuilt (tools/artgen/fowl_props.py `slopshop`): a fairground booth with a lit `SLOP SHOP` marquee (chasing bulbs in fowl_art.cpp), bunting, candy-striped poles, three shelves of the stock (rubber ducks, jars of bees, glitter bombs, gift boxes, party hats, ghost peppers, whoopee cushions, the bagpipe), a till, a bell, a lollipop jar, a chalk price board, and a cardboard moose beside it.
- The raccoon is a real model (`raccoon.glb`: fused fur, mask, vest, bow tie, boater, ringed tail), paws on the counter; it sways and breathes.
- The gun counter's sign reads MR. ZAPPA'S. The Slop Shop panel's sabotage rows no longer print the name over the description.
- Shots `fowl_booth_slop`, `fowl_booth_guns` (the booths without a panel).

