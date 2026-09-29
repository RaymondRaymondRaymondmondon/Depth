# DEPTH: the whole game

*The master design document. It covers what Depth is, how its three games fit together, the shared art and sound direction, progression, economy, saving and the technical ground rules. Each game has its own document:*

| Document | Covers |
|---|---|
| **1_Depth_Overall.md** (this file) | Premise, the Nautilus hub, how the parts connect, economy, art, sound, controls, tech |
| [2_Depth_Expeditions.md](2_Depth_Expeditions.md) | The main game: rank-based roguelike expeditions (crew, classes, combat, enemies, bosses, relics, locations) |
| [3_Depth_Flats.md](3_Depth_Flats.md) | Flats, the creature card duel played at the salon's card table |
| [4_Depth_Parkour.md](4_Depth_Parkour.md) | The Periscope dives: seven generated platform levels and the 3D Abyss, with their living ecosystems |

---

## 1. Premise

You command the **Nautilus**, a brass-and-rivets submarine in the spirit of Jules Verne. From its grand salon you send your hands out into the deep:

- **Expeditions** (the main game) take a party of four crew into the Shallows (the Cave, the Island, the Weeds and Atlantis). They fight through rank-based battles like *Darkest Dungeon*, but the tone is brighter, wetter and more wondrous than grim.
- **Periscope dives** (the parkour game) send one diver through a hard, *Super Meat Boy*-style platform level full of living creatures. They're the main way to earn gold and relics.
- **Flats** (the card game) is a creature-duel card game against a card-sharp in the salon, in the manner of *Inscryption*. It's the gambler's way to earn gold.

The two side games fund the main game. Gold is deliberately scarce, and the Periscope and the card table are the intended income, so all three games feed into one loop.

**Tone:** brighter and more wondrous than Darkest Dungeon, but still dangerous. There's marine wonder, Victorian brass, sailors' superstition and drowned myth (Atlantis, Neptune, Cthulhu). Stress is called "Nerves", and a hero who breaks becomes *Rattled*, which is gentler than DD's afflictions.

---

## 2. The core loop

```
          +---------------------- THE NAUTILUS SALON (hub) ----------------------+
          |  recruit - equip - heal - upgrade - read - choose what to do next     |
          +----+--------------------------+-------------------------+-----------+
               |                          |                         |
         THE HELM                   THE PERISCOPE              THE CARD TABLE
     (expeditions: main)          (parkour dives)              (Flats duel)
     XP, loot, relics,            gold payout, relics          gold (cash out the pot)
     some gold; risk crew         unlocks next dive            risk the pot
               |                          |                         |
               +-------------> gold, relics, levelled crew <--------+
```

1. In the salon, pick a party of four, fit relics, choose loadouts, heal the wounded at the Ward and rest rattled crew in the Sick Bay.
2. Earn gold at the Periscope (skill) or the card table (risk).
3. Spend it on recruits, relics, healing and Workshop upgrades.
4. Dive deeper on expeditions: harder cave levels give more XP, loot and relics, and unlock the next tier.

---

## 3. The hub: the Nautilus's grand salon

The salon is **one fixed screen that never scrolls**. It's a true 3D room (`salon.cpp`, projected with `Proj()`) modelled on Captain Nemo's salon: a great window onto the sea, a pipe organ, bookshelves, brass instruments and rugs. Side-wall art is painted flat (`PaintWall`) and mapped onto the walls in perspective; floor furniture is drawn as billboards.

### Stations
| Station | What it does |
|---|---|
| **The Helm** | Choose a location and tier and launch an expedition |
| **Crew Quarters** | Choose the party, fit relics (two each) and pick each crew member's 4 of 8 abilities |
| **Radar Room** | Recruit new crew and buy relics from passing salvagers (Sonar scans) |
| **Periscope** | Choose a parkour dive, its options (checkpoints, Normal/Hard, bosses on/off), the dossier, and the music and effects volume |
| **Workshop** | Upgrade the Nautilus (reflector, bunks, sonar, infirmary gear, cargo netting) |
| **The Ward** | Pay gold to heal injured crew (Ward cost per HP) |
| **Sick Bay** | Rattled crew rest by the organ; they're cured but sit out the next expedition |
| **Library** (Bookshelf) | Lore on the crew, conditions and creatures |
| **The Card Table** | Play Flats against the ship's dealer |

### Salon life
- Only the Nautilus's own hands walk the salon, **two at a time plus the cat** (`ACTIVE_NPCS`). The expedition crew never appear there. The pool is the helmsman, radio operator, engineer, professor, steward and orderly.
- Each hand works at a sensible post (`WorkPose`) and idles when off post (cleaning a weapon, studying charts, drinking tea). Hands and the cat steer around the furniture (`OBSTACLES`).
- **The ship's cat** is pixel art with sitting, walking, purring and sleeping poses; it sleeps at about 30% of its stops. The user likes the cat and the moving crew, so keep them.
- The room must stay *logical*: every object has a reason to be where it is.

---

## 4. Progression and economy

### Gold (deliberately scarce)
- **Start with 60 gold.**
- **Income:**
  - Periscope payouts: Pipes 60 (plus up to 60 for speed), Hull 120, Pirate 200, Island 240, Cave 280, Weeds 320, Atlantis 360, Abyss 400. Hard pays x1.5.
  - Flats pot: cash out after any win.
  - Modest dungeon loot, richer in low light (up to 1.8x).
- **Costs:**
  - Workshop upgrades: 400 / 800 / 1400.
  - Relics: 145-320.
  - Ward healing: 4-9 gold per HP, depending on the Infirmary upgrade.
  - Sonar scans: 70 down to 25.
- All prices live in `data.cpp`.

### Unlocks
- **Expeditions:** four Shallows locations are open from the start. Each has its own ladder of 5 tiers (cave levels 0, 1, 3, 5 and 6). Clearing a tier unlocks the next, and earlier tiers stay playable.
- **Parkour:** a strict chain, Pipes → Hull → Pirate Ship → Island → Cave → Weeds → Atlantis → the Abyss. Clearing one unlocks the next.
- **Crew** start at level 0 and level up (max level 6) from expedition XP. Deeper levels give much more XP, so grinding the Shallows alone stalls out.

### Saving
- The game autosaves to `depth_save.txt` next to the exe (plain text, `save.cpp`) whenever you return to the salon, and on quit.
- "Start a new game" in the salon wipes the save.
- Saved: gold, roster (with loadouts, relics and XP), tiers cleared, unlocked dives, Periscope options and volumes, and stored relics.

---

## 5. Art direction (shared rules)

**All art is drawn in code. There are no image assets.** An optional `assets/` pipeline (skeletal sprites and parallax layers) can replace procedural drawing if files are ever added, but none are.

There are three visual languages:

| Where | Style |
|---|---|
| **Salon, expeditions, menus** | *Darkest Dungeon*: 2D with depth. Inked, painterly and heroic |
| **Parkour levels** | Retro pixel art at half resolution with point filtering (2 px art grid) |
| **Flats** | *Inscryption*, act 1: parchment cards, black ink linework, hatching, stipple, candlelit table |

### Darkest Dungeon style (salon and expeditions)
- **Characters must look three-dimensional.** They're built from lit cylinders, spheres and panels (`ShadeLimb`, `ShadeBall`, `ShadeQuad`) with chunky, heroic proportions. They're drawn inside `BeginFigure`/`EndFigure`, where the **figure shader** adds a thick ink outline, linework between parts and wrapped light and shadow.
- In the current style, figures are rendered as **ink illustrations**: black linework, Bayer-dithered stipple, and hatching over dusky paper tones, matching the cards.
- **Backgrounds** are inked and painterly. `InkPass()` adds edge ink, cross-hatching in the darkest shadows and canvas grain. Scenes use layered parallax, a lightmap (`LightsBegin`/`AddLight`/`AddCone`/`LightsEnd`) and generated textures.
- Tiny bright particles (dust, snow, steam) are drawn **after** `InkPass`, or the ink rings each one in black.
- Outside expeditions (`gDiveGear` false), figures get warmer, more saturated colour (`uVibrance`), and the Diver takes her helmet off.

### Pixel-art rules (parkour)
- World pixels equal screen pixels, on a 2 px art grid. Sprites have no fractional offsets and no rotation. Squash and stretch happen in whole art pixels.
- Creatures are **shaded pixel sprites** (`pixelart.h`): lit 4-band ramps, dithering, contour lines, patterns (scales, bands, spots, scutes, speckle, rings) and an ink outline.
- Tiles have **2.5D depth** (tops and sides recede up and to the right) and auto-join by an N/E/S/W bitmask.

### Rendering ground rules (technical)
- Use `DrawTri()`, not `DrawTriangle`, because raylib culls by winding.
- Textured custom geometry must use `RL_QUADS`.
- Draw into other textures with `BeginLayer`/`EndLayer`, never raw `BeginTextureMode`.
- The scene is supersampled (`SS = 2`). Leave targets with `EndTarget()`.

---

## 6. Sound direction (shared)

- **Everything is synthesized in code** (`sound.cpp`, `sound_parkour.cpp`). There are no audio files.
- **Music** is generative. Each parkour level has a long, evolving track built from layered synth voices, arranged into 12 sections that build on each other and swell tenser when an apex predator arrives. Each level has its own style:
  - Pipes: synth-forward, with rare pipe clangs.
  - Hull: a deep synth drone with nothing piercing or high-pitched.
  - Pirate: a synth sea-shanty.
  - Island: ambient synth with tribal chants.
  - Cave, Weeds, Atlantis and the Abyss each have their own palette.
- **Effects:**
  - A distinct voice for every beast, plus death and pain sounds.
  - Tiny details, like a bird's grab and a mouse's squeal, or the crunch of a shark's bite.
  - Subtle wind and water where a draft or current exists.
  - Movement sounds.
  - A soft, unannoying death sound for the diver.
- **Volume:** separate Music and Effects sliders on the Periscope, saved.

---

## 7. Controls (summary)

| Context | Controls |
|---|---|
| Menus, salon, expeditions, Flats | Mouse |
| Parkour | A/D or arrows to move. Space/W/Up to jump (hold for height). Wall slide and wall jump. Down to slide, brake or roll. Double-tap a direction to dash. Hold Up to glide underwater. Up/Down on kelp and ratlines. Esc gives up. A gamepad works too |
| Window | Resizable (min 640x360), letterboxed. F11 toggles borderless fullscreen |

---

## 8. Technical overview

- **Language and libraries:** C++17, raylib 5.5, CMake. Build with `.\build.ps1` (add `-Run` to launch); the output is `build\Release\depth.exe`.
- **Main source files:**
  - `main.cpp`: scenes and command-line tools.
  - `salon.cpp`: the hub.
  - `dungeon.cpp` and `data.cpp`: expeditions.
  - `render.cpp` and `enemyart.cpp`: figures.
  - `relics.cpp`.
  - `flats*.cpp`: the card game (the engine is headless).
  - `platformer.cpp`, `levelgen.cpp` and `beasts*.cpp`: parkour.
  - `abyss.cpp`: the 3D Abyss.
  - `sound*.cpp`.
  - `save.cpp`.
- **Self-tests and tools** (all headless options of `depth.exe`):
  - `--shots shots [filter]`: renders every screen to PNGs.
  - `--sprites out.png`: every sprite on a sheet.
  - `--sim N <level>`: expedition balance.
  - `--boss`: one boss, many fights.
  - `--relic-test`.
  - `--flats-sim N`, `--flats-ui-test N`.
  - `--verify`: every generated parkour level can be crossed.
  - `--verify-moves`, `--verify-beasts`, `--verify-<biome>-ecosystem`, `--verify-abyss`.
  - `--gen <level> <seed>`: prints a generated level as ASCII.
  - `--audio-test`.
- `CLAUDE.md` holds the running development notes. `ROADMAP.md` and `ECOSYSTEM_ROADMAP.md` hold the plans and logs.

---

## 9. Design pillars

1. **Three games, one economy.** Every side game has a real reason to be played: it funds the crew.
2. **Everything alive.** The salon crew work, the cat wanders, fighters breathe, and parkour creatures eat each other in plain view.
3. **Hard but fair.** Parkour is Super Meat Boy hard, and every generated level is proven crossable by the real movement code. Expeditions are dangerous, but Death's Door and Rattled are gentler than DD.
4. **Hand-drawn in code.** There are no assets, so every picture is procedural, inked or pixel-perfect, and consistent within its style.
5. **Discovery.** Dossiers, first-meeting hints and lore reward attention without forcing it.
