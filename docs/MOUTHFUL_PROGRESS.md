# Mouthful (Deep Arcade game 8): build log

**Design:** `Reference_For_Future_MP_Games/Mouthful — Arcade Game 8 Design Document.pdf`. Its OCR is in `docs/mouthful_pdf_pages/`.

Built 2026-10-03, all six stages of the doc's build order. A first version of each is in; balance is ongoing (see the end).

## Code
| File | What |
|---|---|
| `src/mouthful.h/.cpp` | The headless core: the arena, tiers, forms, eating rules, abilities, passives, the crown, respawn, bots, NPC predators, the leviathan |
| `src/mouthful_events.cpp` | The dangers that aren't players: the boat, the orca pod, the red tide, the whale fall, dusk, the eel garden |
| `src/mouthful_skins.cpp` | Skins, the crate, the wardrobe, round tokens, the profile |
| `src/mouthful_net.h/.cpp` | `MouthfulHost` on the arcade session, the snapshot `Visit`, guest mirror and prediction, tests |
| `src/mouthful_game.cpp` | The scene: drawing, HUD, fork cards, results, the wardrobe page, sound diffing |
| `src/mouthful_tools.cpp` | `--mouthful-test`, `--mouthful-round`, `--mouthful-duel` |
| `src/sound_mouthful.inl` | The music, band beds and cues |

## Data (`data/mouthful/`)
- `mouthful_tiers.json`, `mouthful_paths.json` (every form), `mouthful_sea.json` (prey worth and spawns), `mouthful_scoring.json`, `mouthful_bots.json`, `mouthful_skins.json`.
- The web: `sea/reef/` in Red Tide's format, written by `tools/mouthful_data.ps1`, with art rows in `art/species_mouthful_reef.json`.
- Map key `mouthful_reef`: `rt::Map` and the art lookup redirect `mouthful_` keys.

## Checks
- `depth.exe --mouthful-test`: the rules, the dangers, the modes.
- `--mouthful-net-test`: the snapshot round-trips byte for byte.
- `--mouthful-skins-test`
- `--net-loop mouthful [mem]`: six players finish a 15-minute round, the doc's stage-3 gate.
- `--mouthful-round <bots> <minutes> [seed] [runs] [mode]`: set `DEPTH_MFTRACE=1` for a per-minute board, or `DEPTH_MFBOT=<name>` to follow one bot.
- `--mouthful-duel <formA> <formB> <mass> [runs]`: set `DEPTH_DUELTRACE=1` to log the bites.
- `--audio-test` covers Mouthful: 8 states and every cue.
- Shots `mouthful_*` (fry, reef, wall, blue, trench, fork, king, results, lineup, guest, boat, orcas, redtide, whalefall, dusk, skins, wardrobe) and `arcade_mouthful`.

## Decisions made where the doc was open or silent (the user may change any)
- **The crown:** passes straight to the killer, as the doc says; it doesn't drop as an item. A king killed by an NPC leaves the crown free.
- **Paths:** always chosen at the first fork, not locked in the lobby. The fork offers your last path first.
- **Crustaceans:** floor-only; the upwelling doesn't lift them.
- **The arena:** one arena (the reef shelf).
- **Fights:** a fight bite feeds the biter 30% of what it tears off; the swallow is the meal. This was set by duels, because a full feed snowballed every fight to the first biter.
- **Evasion:** a quicker fish on the move slips some bites (12% plus 60% of the speed difference, +30% in a dash). The doc has no evasion; it's what makes the paths' speeds matter.
- **Lengths:** tiers 4, 5, 7 and 8 lost their lengths in the OCR. We use 1, 2, 6 and 10 m. Claw ranges and turn rates are ours.
- **Bots and the crown:** with people in the round, bots never take the crown (the doc's throttle).
- **Mouthful's own wallet:** skins are bought with Mouthful tokens kept in `mouthful_wardrobe.txt`, not a shared arcade token.

## Balance: where it stands (bot-only rounds, 11 bots, 15 minutes)
- **Growth (a lone Shark bot):** tier 4 at 3 min, tier 6 at 7 min, the crown at 10.3 min. This meets the doc's targets.
- **Twelve mouths:**
  - The first king came at about 6-9 min, in half the rounds.
  - The crown changed hands about 0.5 times a round; the doc wants 3-6. **Not met.** A crowned king snowballs (swallowing a player gives its whole mass, per the doc), and only the leviathan or a pack of tier-7s can bring it down. Expect players hunting the king to do better than the bots; revisit after a playtest.
- **Deaths by cause:**

  | Cause | Now | Target |
  |---|---|---|
  | Players | about 70% | 60% |
  | NPC predators | about 27% | 25% |
  | The boat and hazards | about 2% | 10% |
  | The leviathan | about 1% | 5% |

- **Duels at tier 2 (70 mass, 40 runs each):**
  - Mirrors: 42-54%.
  - Squid beats moray 67% and dogfish 57%. Puffer beats dogfish 71%.
  - Moray beats puffer only 37% (the doc says the eel should win).
  - The crab wins every floor fight (100%), and the shark beats the crab 100%: both too strong.
  - **Not yet within 55-65% across the table.**

- Visual polish (2026-10-05): the reef's coral heads are four kinds (staghorn, brain, table, pillar) instead of lathed logs; giant kelp has a holdfast, a stipe, bladders and wavy blades, in eight groups that sway (a shear in `DrawWorld`); seagrass grows in tufts; lumpy boulders (`Lump`) dress every band (shallows stones, reef rubble, scree at the wall's foot, ledges on its face, basalt pillars in the trench with glowing colonies), plus sea fans (`Fan`), tube sponges, glass sponges on the blue's silt, tube worms round the vents; the wall caves have a rocky lip; motes, plankton, ink puffs and corpses are round; the whale fall has arched ribs; garden eels lean in the current; the boat overhead is the Trawl's Gannet.
- The king's crown is a gold band with five points, pearls and rubies; skin patches, beads and studs are round; the hats are moulded shapes (a bicorne, a chef's toque, a turning wind-up key, ribbon streamers, ribs arching down the sides, curved horns, a rice ball with its nori band, a little submarine, an angler's lure on a stalk). The skins self-test reads prices from the catalogue (it still expected the pre-halving 50 tokens).
