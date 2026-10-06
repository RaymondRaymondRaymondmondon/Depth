# Fathoms: build log

Design: `Reference_For_Future_MP_Games/Fathoms — RTS Design Document.pdf` (37 pages; OCR in `docs/fathoms_pdf_pages/`).
A 2-6 player steampunk island RTS: six factions, three eras, a generated archipelago, neutral powers, LAN/internet play.

## What is built (all eight of the doc's stages have a first pass)

| Stage | What | Where | Check |
|---|---|---|---|
| 1 Foundations | the balance file, the tile map, commands, the fixed 20-tick step | `fathoms.h`, `fathoms.cpp`, `fathoms_data.cpp`, `data/fathoms/fathoms_balance.json` | `--fathoms-test 1` |
| 2 Economy | gathering, drop-offs, Harbor, Cottages, farms, the Exchange, population, workers building together | `fathoms.cpp` | `--fathoms-test 2` (the AI alone: 18-19 workers at 6 min, ~30 at 12, Steam at ~10.3 min) |
| 3 Combat | the damage formula, counters, morale, eras, the Workshop's forge lines | `fathoms.cpp` | `--fathoms-test 3`, `--fathoms-balance` (all ten matchups' winners match the doc) |
| 4 Islands and navy | the generator with the fairness rules, ships, transports, landings, Lighthouses, coaling range | `fathoms_map.cpp`, `fathoms.cpp` | `--fathoms-test 4` (includes a full AI 1v1) |
| 5 Multiplayer | the arcade session: host-authoritative, fog-filtered snapshots, commands | `fathoms_net.*` | `--fathoms-net-test`, `--net-loop fathoms [lagMs] [mem]` |
| 6/8 Factions | all six: mechanics, uniques, heroes and their actives, faction techs, ultimates, wonders | `fathoms_faction.cpp` | `--fathoms-test 6` |
| 7 Neutral powers | pirate coves (deals, counter-bids, black market, reputation, Blackbeard), tribes (raids, growth, tribute, Kinship, conquest), volcanoes (eruptions, lava, the Sun God, the altar), ruins and relics, wrecks and traps, the Ghost Ship, the Kraken, weather | `fathoms_neutral.cpp` | `--fathoms-test 7` |
| AI | economy, expansion (walking over shallows or by Transport), military counters, waves by sea and by landing, defence, tribes, pirates, black-market Ichor, faction rules; four difficulties | `fathoms_ai.cpp` | `--fathoms-sim N [players] [minutes] [ai 0-3]` |
| Scene | overhead RTS camera, drag-select, smart right-click, command card, minimap, panels (diplomacy, cove, tribe, Exchange, Logbook, help), alerts | `fathoms_game.cpp`, code art `fathoms_art.cpp` | shots `fa_*`, `arcade_fathoms` |
| Sound | an age-of-sail score by era, war drums near battle, the sea's bed, storms, lava, every effect | `sound_fathoms.inl` | `--audio-test` |

`--fathoms-test` (no stage) runs everything (about 80 s). `DEPTH_FTRACE=1|2|3` prints the AI players' economy every 30 s
of a test (2: every gatherer; 3: unit and building counts); `DEPTH_AITRACE=1` prints the AI's expansions and waves.

## Defaults where the doc was open (the user may change any of these)
- Island sizes (diameters): Home 30, Fertile 15-18, Mining 13-16, Coal 10, Reef 8, Ruin 10, Tribal 17, Cove 13, Volcano 22.
- Every finished building holds the ground within 4 tiles (the Harbor and Colony Hall 12, a Lighthouse 14): without it
  the home island ran out of room to build.
- Tribal raiders and pirate landing parties come ashore near their target (the canoes and the transport are implied).
- The lobby's AI seats share one difficulty; factions are chosen per seat by the host (random by default).
- Snapshots go out 10 times a second; positions are sixteenths of a tile, hit points and timers quarters.
- Large towns (every other one) have a Tribal Chieftain or a Witch Doctor; spear-throwers are a unit (`tribal_thrower`).
- The Sun God's beam fires every 6 s; the Kraken holds the nearest ship within 6 tiles.
- Pirate deals land their pirates directly near the target; Ransom is not built (nothing captures units yet).
- Ichor for the AI: when it has an Exchange it sells surplus for Doubloons and buys Ichor at the black market.

## What is simpler than the doc (next passes)
- Delta compression of snapshots (each is a full fog-filtered state, ~5-6 KB at 10 Hz).
- Pausing (3 per player), autosave every 3 minutes, replays, join codes (the arcade's shared networking handles LAN/ZeroTier).
- Diplomacy offers to humans are accepted at once by the AI and need no acceptance step between humans yet.
- Unit models are code art (primitives); the doc's Blender pass is for later.
- The AI is decent but not strong: Captain and Admiral conquer about half of 2-player matches by 35 minutes, and the
  Leviathan Era is rare before 30 minutes in AI-only games.

## Testing checklist (what to look at when playing)
- Start a solo game from the Strategy reel: pick a faction, the rivals' level, the map and the victory types.
- Workers: drag-select, right-click a grove/seam; Cottages when the population bar fills; a Farmstead and a Mine Shed.
- Advance to Steam at the Harbor (two of Barracks/Stable/Dock/Farmstead/Mine Shed).
- A Transport: right-click it with workers selected (they board), U then click a shore to unload; build a Lighthouse.
- Click a pirate cove (the black market, hire a raid) and a tribal town (tribute; Kinship with the Islanders' Shaman).
- Watch for minute 6 (tribes raid), 10 (the volcano erupts), 15 (Ghost Ship on 4+ players), 20 (the Kraken).
- The Nautilus Logbook (F3), heroes' actives (R), the ultimate (Leviathan Era, at the Harbor).
- Host/Join for friends: each player sees only what their units see.
