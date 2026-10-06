# Depth: Flats Duel (Deep Arcade game 1)

Proposed as a new part of the game bible: copy it to `docs/design/6_Depth_Flats_Duel.md` when the duel goes in. CLAUDE.md asks that the design docs stay current. It follows the Master Reference's "Arcade game 1: Flats Duel" section. Where the numbers had to change, the change and the measurement behind it are given.

## What it is
Flats against a person, at the same candlelit table, best of three. The opponent sits in the dealer's chair, drawn in the **dealer skin** they chose. They lean in when they're ahead, flinch at a big hit, reach across to lay their cards, and play one of six emotes.

## The board
The board is symmetric. From each player's own seat:

| Row | What it is |
|---|---|
| 0 | the opponent's reserve |
| 1 | the opponent's front |
| 2 | your front |
| 3 | your reserve |

There is no dealer queue. Both players follow the same upkeep: draw a card or a Minnow. Everything else is the single-player rules engine, unchanged.

## A turn
1. Upkeep: draw a card or a Minnow.
2. Play cards and use items.
3. Ring the bell.
4. Combat, lane by lane.
5. The end phase: Fledglings grow, the reserve steps forward, and so on.

Then the table passes. Turns have a timer: 60 s, or 30 s in Quick. When it runs out, the upkeep draws from the deck and the bell rings itself.

## Second-player balance (changed from the Master Reference)
The Master Reference asks for "one extra bone and a Minnow in hand" and a first-player win rate of 48-52%. In 3,000-4,000 bot matches per setting:

| Setting | First player wins |
|---|---|
| no bonus | 55-57% |
| the spec's bone + Minnow | 43-44% (the Minnow alone is worth about 11 points) |
| **shipped:** +1 bone, and the scales start tipped toward the second player, 1 notch in Draft and 2 in Constructed/Quick | **50.6% / 50.3% / 50.7%** |

Preset and constructed decks give the first player more tempo than drafted ones, hence the second notch there.

## How a game ends
- The scales tip at ±8, as in single-player.
- A **round** is both players' turns. After round 20, the side the scales favour wins.
- If the scales are level after round 20, it's **sudden death**: the next tip of any amount wins.
- Still level after round 30: the game is tied and replayed.
- A match of 5 games with no winner is drawn.
- In bot play these limits almost never matter: games average 5.2-5.5 rounds, past round 20 in 0.1-0.2% of them, and ties in under 0.01%.

## Rule changes for two humans
- **Banned:** Massive, Tidal Pull, and every dealer-only card. The Boulder and the Black Goat still come out of their bottles, and a Kraken still grows from a Kraken Spawn.
- **Undying** returns at most twice per player per game. After that, that player's creatures on the board lose it.
- **Spawn** copies can't Spawn. The engine already works this way.
- **Sharp Quills** caps at 3 damage per turn from one creature. In today's catalogue this can't happen: no card has both Burrower and Sharp Quills, and nothing else lets one creature be struck more than three times in a turn. Add the cap to `flats_board` if such a card ever appears.
- **Items:** each player picks 2 pack items at setup, and they are refilled every game. A Scavenger find adds a random one, up to the pack's 3 slots.
- **The Fishhook** pulls a card from the opponent's reserve (row 0) instead of the dealer's queue.
- **Gilt** cards aren't allowed, because a duel has no pot.
- **No charms, totems, momentum or pot.**

## Modes

| Mode | Decks | Games | Timer |
|---|---|---|---|
| **Draft** (default) | An open "Rochester" draft: 13 packs of 4. The opener picks, then the other, then the opener, then the other. Both see every pack and every pick. You keep 20 of your 26 picks; the other 6 are the sideboard. Then 2 Minnows and a Ballast Cask are added. | best of three | 60 s |
| **Constructed** | A 20-30 card deck and up to a 6-card sideboard. At most 3 of a card, and at most 2 of a tier-3 card. The host checks the list against the catalogue. | best of three | 60 s |
| **Quick** | One of six presets (Bone, Blood, Airborne, Big Fish, Shell Wall, Swarm), each 20 cards plus the Minnows and the Cask | one game | 30 s |

**Why 13 packs, not the Master Reference's 8.** 8 packs of 4 split between two players gives each 16 cards, which can't make "20 plus a 6-card sideboard". 13 packs give each player 26.

**Between games** (Draft and Constructed): up to 3 swaps between deck and sideboard, then both players ready up. The loser of each game goes first in the next.

## Preset balance
In 6,000 Quick-mode bot games, each preset's overall win rate:

| Preset | Wins |
|---|---|
| Bone | 51% |
| Blood | 59% |
| Airborne | 43% |
| Big Fish | 46% |
| Shell Wall | 45% |
| Swarm | 56% |

Every preset carries about two tier-3 finishers. The side that plays a Sea Serpent, Great White, Sperm Whale, Chambered Titan or Drowned King wins 68-82% of the games it's played in, so a deck without them couldn't keep up.

## Setup (the first phase of the match, at the table)
- Both players pick a dealer skin and two items.
- Player 0 (the host) picks the mode. Changing it un-readies everyone.
- In Constructed, each player submits a deck. In Quick, each picks a preset.
- When both are ready, the match begins.

## Hidden information
A player's snapshot never contains:
- the opponent's hand (only its size)
- either deck's order (only counts)
- the opponent's items (only a count, until one is used)
- the opponent's split between deck and sideboard
- the opponent's preset, before the game shows it
- the host's random-number state

The opponent's draws animate face down. A spectator (viewer -1) sees neither hand.

The kit's leak test checks all of this: it changes every hidden thing and requires the snapshot to come out **byte for byte** the same. It passed on 21,012 snapshots.

## Presentation
- **The opponent** is drawn by `DrawDuelDealer` in one of **10 skins**:
  - Free: the Novice, the Tidewife, the Wreck-Broker and the House.
  - The Atlantean Sovereign: for clearing the Sovereign in single-player.
  - For arcade tokens: the Harbourmaster (60), the Lamplighter (80), Old Ironsides (120), the Siren (150) and the Ship's Cat (200).
- **Each skin has:**
  - a hat, hands and a face
  - a voice pitch and rate for the formant mumble
  - a thinking tell
  - six lines: greet, ahead, behind, big hit, wins, loses
- **Expressions:** neutral, ahead, behind, flinch, wins, loses.
- **Tells are visual only.** Duel skins have no hidden rules, unlike single-player's dealers.
- **Emotes:** nod, sneer, shrug, tip hat, slow clap, gulp, on a 6 s cooldown, played by the sender's figure.
- **The hands:**
  - The body is drawn before the table and the hands after the board, so the hands rest on the felt.
  - When the opponent plays, the nearer hand reaches out and lays the card with a slap.
- **Arcade tokens** are cosmetic only: 10 per match, 25 per win.

## Checks
- `depth.exe --flats-duel-sim N [mode]`: bot versus bot. The first-player win rate must be 48-52%.
- `--flats-duel-sim` should also run the preset matrix: every preset 40-60% overall.
- `--net-loop duel`: a host and two guests finish a best-of-three with a draft, timers, sideboarding and emotes. The packet-log check requires that no client ever receives the opponent's hand.
