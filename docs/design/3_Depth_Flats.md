# DEPTH: Flats (the card game)

*A creature-duel card game played at the card table in the Nautilus's salon. See [1_Depth_Overall.md](1_Depth_Overall.md) for how it fits the economy.*

---

## 1. What it is

**Flats** is a roguelike creature duel in the manner of *Inscryption*'s first act. You sit across a candlelit table from a dealer and play sea creatures onto a 4x4 board. Each blow that finds an empty lane tips a set of **scales**, and a battle ends when they tip 8 points either way.

A **run** is a branching map of battles and events that ends at the **Atlantean Sovereign**. Every battle you win adds gold to the **pot**. You may **cash out** after any win and walk away with the pot, or press on for more; lose a battle and the pot stays on the table (unless you bought insurance).

It's the salon's **gambler's income**, next to the Periscope's skill income.

---

## 2. Art and presentation

- **Table:** laid out like Inscryption act 1. The dealer sits across the table in the salon (`DrawCardTable`, `DrawCardDealer`), with glowing board cells, a fan of cards in hand, a brass bell that ends your turn, the scales, a pack of bottled items, and a folder tab on the right with the full rules.
- **Card face:** a salvaged relic.
  - A driftwood frame around stained, salt-crusted parchment.
  - The title printed in ink with no banner.
  - A large inked illustration well (55% of the card).
  - Bone and driftwood stat plates on the bottom corners, and sigil seals between them.
- **Card backs** are weathered driftwood with a net and a stamped ship's wheel.
- **Creature art** follows Inscryption's ink style: pale parchment, black linework, hatching, brown stipple and a faint shadow-self behind. Each creature is a concept grid, upscaled with EPX and rendered as dithered ink (`flats_art.cpp`, `Paint`, cached per name). New creatures are drawn with the `shots/lab.ps1` toolkit.
- **Animation:** the UI plays the engine's events back as animation (per-cell `CellFx`): card flights (0.34 s), lunges, shakes, ghosts of the dead, floating numbers, particles, screen shake and dealer reactions.
- **Inspector:** hover any card to read it in full. The bell sits low so the inspector never covers it.

---

## 3. The rules

### The board
Four lanes by four rows:
- Row 0: the dealer's queue (cards they will play next).
- Row 1: the dealer's front.
- Row 2: your front.
- Row 3: your reserve.

### A card
Every card has:
- **Strength:** damage dealt to the card straight across, or to the scales if that lane is empty.
- **Defense:** damage it can take before it dies.
- **Weight:** decides who shoves whom.
- **Cost:**
  - Free.
  - **Blood:** sacrifice your own creatures on the board. Click them in sacrifice mode. A Ballast card counts as 3 blood.
  - **Bones:** tokens earned whenever one of your creatures dies.
- **Sigils:** up to 3 abilities.
- **Edition:** see below.

### A turn
1. **Upkeep:** choose ONE: **draw a card** from your deck, or **draw a Minnow** (a free 0/1). There's no longer a double draw. At the first two tables, the Novice always backs up an unlucky pick with a free minnow and the Tidewife does so every other turn, so beginners don't stall.
2. **Main:** lay cards in your front row and use pack items.
3. **Ring the bell:** every creature strikes the space straight ahead. A blow into an empty lane lands on the **scales**.
4. The dealer takes their turn: queued cards advance into their front row, and then they strike.

**Winning and losing:** the scales tip **±8** (Selenis is the exception, see below).

### Weight interactions
- A heavier striker **knocks a surviving target back a row**.
- A heavier card may be **laid onto an occupied lane of yours** and pushes the occupant into the reserve.
- A forced sideways move (Heavy Current) pushes lighter cards, and **crushes** a mover that isn't heavier than what's in its way.

### Momentum
Win a battle in **under 6 turns** for +1 Momentum (max 3). Spend it on a boon at the start of the next battle: two extra cards, three bones, or the scales +1.

---

## 4. Sigils (25)

| Sigil | Effect |
|---|---|
| Airborne | Flies over the creature in front and strikes the scales. Blocked by Mighty Leap |
| Twin Tide | Strikes the two neighbouring lanes instead of the one ahead |
| Tidecaller | Creatures beside it gain +1 strength |
| Sharp Quills | Whatever strikes it takes 1 damage |
| Brine | The creature across from it loses 1 strength |
| Venom | Anything it damages dies |
| Sentinel | Steps in front of an enemy that arrives across an empty lane |
| Burrower | When an empty lane is struck, slides in to take the blow |
| Undying | When it dies, a copy returns to your hand |
| Ballast | Counts as three blood when sacrificed |
| Many Lives | Survives being sacrificed |
| Spawn | When played, a copy (without Spawn) is added to your hand |
| Heavy Current | Shoves sideways each turn, pushing lighter creatures |
| Swimmer | Drifts one lane each turn, turning about at walls and blockers |
| Bone King | Leaves four bones when it dies |
| Scavenger | When played, you find an item (if your pack has room; otherwise a toast says so) |
| Fledgling | Grows into something bigger after a turn on the board |
| Repulsive | Creatures will not strike it |
| Waterborne | Submerges on the enemy's turn: blows pass over it to the scales |
| Phalanx | If an adjacent Phalanx creature is struck, this one takes the blow |
| Foresight | Sidesteps attacks; heals every Phalanx creature by 1 each turn's end |
| Mighty Leap | Blocks Airborne creatures |
| Massive | Fills all four lanes, can't be moved, and every attack strikes it |
| Tidal Pull | At the end of its turn, drags every enemy creature one lane sideways; the edge is fatal |

## 5. Editions
| Edition | Effect |
|---|---|
| **Foil** | +1 strength |
| **Gilt** | Slaying a card pays 4 gold into the pot |
| **Hex** | +2 strength, but 1 less defense |

---

## 6. The catalogue

Stats are strength / defense / weight.

### Your starting deck (12)
Hermit Crab, Flying Fish, Anglerfish, Pufferfish, Sea Urchin, Clownfish, Fry, Ship's Cat, Ballast Cask, Salvage Diver, Sailfish, Skeleton Sailor.

### Tier 1: cheap and simple
| Card | Stats | Cost | Sigils |
|---|---|---|---|
| Hermit Crab | 1/3/3 | 2 bones | Burrower |
| Flying Fish | 1/2/1 | 1 blood | Airborne |
| Anglerfish | 2/2/2 | 1 blood | Waterborne |
| Pufferfish | 1/2/1 | 2 bones | Sharp Quills |
| Sea Urchin | 0/3/3 | 2 bones | Sharp Quills |
| Clownfish | 1/1/1 | 1 blood | Spawn |
| Fry | 1/2/1 | 1 blood | Fledgling → Barracuda |
| Ship's Cat | 1/2/1 | 1 blood | Many Lives |
| Ballast Cask | 0/2/4 | 1 bone | Ballast |
| Salvage Diver | 2/2/2 | 1 blood | Scavenger |
| Mudskipper | 1/2/1 | 2 bones | Burrower |
| Sailfish | 2/2/2 | 1 blood | Airborne |
| Skeleton Sailor | 1/2/2 | 1 blood | Bone King |

### Tier 2
| Card | Stats | Cost | Sigils |
|---|---|---|---|
| Stingray | 1/2/2 | 2 blood | Venom, Waterborne |
| Manta Ray | 2/3/3 | 2 blood | Twin Tide |
| Sea Turtle | 1/5/4 | 2 blood | Sharp Quills |
| Swordfish | 2/2/2 | 2 blood | Airborne, Sharp Quills |
| Squid | 1/2/2 | 1 blood | Waterborne, Brine |
| Coral Queen | 1/3/2 | 2 blood | Tidecaller |
| Crab Sentinel | 2/3/3 | 2 blood | Sentinel |
| Ghost Crab | 2/1/2 | 4 bones | Undying |
| Sea Anemone | 0/4/3 | 3 bones | Repulsive |
| Hammerhead | 3/3/4 | 2 blood | none |
| Barracuda | 3/3/3 | 2 blood | none |
| Moray Eel | 3/2/1 | 2 blood | Airborne |
| Nautilus | 1/2/2 | 1 blood | Fledgling → Chambered Titan |

### Tier 3: heavy hitters
| Card | Stats | Cost | Sigils |
|---|---|---|---|
| Great White | 4/4/5 | 3 blood | none |
| Sperm Whale | 3/6/5 | 3 blood | Heavy Current |
| Kraken Spawn | 1/3/3 | 2 blood | Fledgling (→ **Kraken** 5/8/6, Quills, Tidecaller), Quills |
| Sea Serpent | 4/3/3 | 2 blood | Twin Tide, Brine |
| Drowned King | 3/4/4 | 3 blood | Bone King, Undying |
| Chambered Titan | 3/5/4 | 3 blood | Sharp Quills |

### Dealer-only cards (never offered)
- Bilge Rat 1/1/1.
- Deckhand 1/2/2.
- Rusted Anchor 0/4/5.
- Barnacle Husk 1/3/3 (Quills).
- **The Croupier** 4/8/5 (Twin Tide, Quills).
- Boulder 0/5/5.
- Black Goat 0/1/1 (Ballast).
- The drowned court: Atlantean Hoplite 1/3/3 (Phalanx), Sunken Oracle 0/2/1 (Foresight), Coral Golem 2/4/5 (Mighty Leap), and **Selenis, the Moon God** 4/40/9 (Massive, Tidal Pull).

Card rewards scale with depth: tier 1 offers mostly tier-1 cards, tier 3 mostly tiers 2 and 3. A reward has an 18-36% chance to come with an edition.

---

## 7. The dealers

| # | Dealer | Line | Twist | Deck | Payout |
|---|---|---|---|---|---|
| 0 | **The Novice** | "Sit. Cards don't bite. Much." | No tricks yet | Rats, deckhands, crabs, puffers | 20 |
| 1 | **The Tidewife** | "You learn quickly. Shame." | Her Shell creatures strike +1 | Coral Queen, Manta, Nautilus, husks, fry | 40 |
| 2 | **The Wreck-Broker** | "Now we play for real." | A Rusted Anchor already waits across from you | Stingrays, Ghost Crabs, Hammerheads, urchins | 70 |
| 3 | **The House** | "The house always wins. Prove me wrong." | Foil in the deck, draws two a turn, the Croupier at the table | Great Whites, Kraken Spawn, Sperm Whale, Mantas | 250 |
| 4 | **The Atlantean Sovereign** (boss) | "Kneel. The tide remembers every crown." | A drowned phalanx, and beneath it Selenis | 7 Hoplites, 2 Oracles, 3 Golems | 300 |

- **Elite battles** pay half again, and the dealer gets a totem, one more play per turn and extra foils.
- The **Tip Jar** charm adds 10 per win. Gilt slays add 4 each.
- Clearing the whole run adds +100.

### The Atlantean Sovereign (two phases)
- **Phase 1:** a drowned phalanx. The Hoplites shield each other, the Oracles heal them, and the Golems block your Airborne cards.
- The phase ends when the scales tip **+8 in your favour**. `RiseSelenis` then sacrifices the court, and **Selenis, the Moon God** (40 defense, Massive, Tidal Pull) rises. He's anchored at the dealer's front, lane 0, and fills all four lanes.
- **Phase 2:** every attack in every lane strikes him. At the end of each of his turns, **Tidal Pull** drags your rows sideways, and the edge of the board is fatal. You win by **killing him**.

---

## 8. The run map

The map has 9 layers: eight of choices (2-3 nodes each), then the boss. A dealer rules each stretch: Novice for layers 0-1, Tidewife for 2-3, Wreck-Broker for 4-5, the House after that. Layer 0 always offers a battle and a card pick or cache. The layer before the boss is always a breather (campfire, stall or trial). Every layer has at least one battle.

A node whose type repeats every one of its direct predecessors (and isn't a battle) is rerolled, so you can't meet the same event three times running down one path.

| Node | What happens |
|---|---|
| **Battle** | A dealer at the table. Win to bank the pot |
| **Elite battle** (layer 3+) | A harder dealer with a totem. Pays half again and gives a rare card |
| **Choose a card** | One of three |
| **Campfire** | Pick a card: +1 strength or +1 defense |
| **Barnacle Cluster** (splice) | Sacrifice one card to have its sigils encrusted onto a host, for good |
| **The Maelstrom** (sacrifice) | A vortex takes a card for good; every battle then starts with an extra bone |
| **Trial** | A card gives up 2 defense for 3 strength, for good |
| **The stall** | Spend the pot on cards, charms, items, trims, editions, insurance (pays half the pot on a lost battle) and rerolls |
| **Cache** | A free item for the pack |
| **The Boiling Vents** (layer 2+) | Warm a creature for +1 strength or defense; each further warming adds +25% risk that it boils away |
| **The Scrimshaw Artist** (layer 2+) | Carve a totem: a tribe (suit) head on a sigil base. That tribe gains the sigil whenever it's played |
| **The Abyssal Splicers** (layer 2+) | Fuse two copies of the same card into one monstrosity |
| **The Atlantean Sovereign** | The last table |

Suits (tribes): **Coin, Cup, Blade, Shell**.

---

## 9. Charms (up to 3; one is offered as the third reward, and more are sold at the stall)

| Charm | Effect |
|---|---|
| Pearl Necklace | Your Shell creatures enter with +1 defense |
| Sailor's Knot | Your first creature each turn costs 1 less blood |
| Brass Compass | Each upkeep you draw a second card |
| Lucky Tooth | Start each battle with an extra card and 2 bones |
| Iron Anchor | Your creatures weigh 1 more |
| Tip Jar | +10 gold in the pot for each battle won |
| Barbed Hook | Slaying a creature earns an extra bone |

## 10. Pack items (bottles)

| Item | Effect |
|---|---|
| Boulder in a Bottle | A 0/5 boulder appears in your hand |
| Black Goat Bottle | A black goat (worth three blood) appears in your hand |
| Fishhook | Pull one of the dealer's queued cards into your hand |
| Squid Ink | The dealer's creatures strike 1 weaker for two turns |
| Barnacle Bandage | Give one of your creatures +3 defense |
| Harpoon | Deal 3 damage to one of the dealer's creatures |

---

## 11. Code and tools

- **Engine** (headless, no raylib):
  - `flats_card.*`: cards, sigils, catalogue, `Rng`.
  - `flats_board.*`: `Board`, and `Battle` phases YOU_DRAW / YOU_MAIN / YOU_COMBAT / ... / FOE_END, advanced one micro-step at a time. Also the dealers, items, charms, the dealer AI and a sensible auto-player.
  - `flats_run.*`: `GameState` and `RunManager`, the map and deck-altering nodes.
- **Scene:** `flats.cpp`. Screens: map, momentum boon, battle, won, node panels, run over, and debug showcase pages.
- **Tools:**
  - `--flats-sim N` (with `DEPTH_TRACE=1` to print boards). The auto-player wins about 97 / 69 / 83 / 47% against the four dealers, and a sensible bot clears the whole run about 5-10% of the time.
  - `--flats-ui-test N` runs the real screen through N battles.
  - `--shots shots/Flats flats` renders every card, sheets, components, map events and both boss phases.
- Balance note: the single-draw upkeep was a real difficulty increase, so resim before assuming it's tuned.
