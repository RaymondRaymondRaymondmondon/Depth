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

## Added in the second pass
- Pauses: 3 per player, up to 60 s each (F9 or Pause; anyone may resume after 10 s); no orders while paused.
- Autosave every 3 minutes (`fathoms_autosave.bin` next to the exe, solo and host); the arcade's Fathoms page has Resume save, and the host can choose to host the save (everyone rejoins by seat). `SaveFathoms`/`LoadFathoms` reuse the snapshot writer with nothing filtered, plus each player's explored chart and the RNG.
- Diplomacy offers between humans wait for the other player's Accept (`Player::offerFrom`); the AI still answers at once.
- Ally pings: Alt+click (attack), Ctrl+Alt (defend), Shift+Alt (danger), drawn on the map and the minimap.
- Team games from the lobby: free for all, teams of two, teams of three, three teams of two.
- The volcano is one walkable cone of rock with the altar on its summit (it used to sit inside impassable mountain tiles, so Volcano Ascension could never be won); a smoke plume, thick and dark while it erupts.
- The AI farms as the groves thin, moves idle workers to its other islands, trades surplus at the Exchange, and fields one hero only; in Captain AI matches every 2-player game now ends in conquest by about 30 minutes, with the Leviathan Era around minute 25.
- Camera: arrows, screen edges and middle-drag pan (A and S are orders).

## What is simpler than the doc (next passes)
- Delta compression of snapshots (each is a full fog-filtered state, ~5-6 KB at 10 Hz).
- Replays, join codes (the arcade's shared networking handles LAN/ZeroTier).
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
- F9 pauses (three each); the toast says Autosaved every 3 minutes; quit and press Resume save on the Fathoms page.
- Alt+click pings for allies; F2 diplomacy offers and Accept between two humans.

## Playtest fixes (2026-10-06)
- Nobody walks on water any more: only the tribes wade the shallows (`Passable`); everyone else needs a boat.
- Raids come by sea (`SeaRaid` in fathoms_neutral.cpp): pirates in transports, tribes in war canoes, which can be sunk before they land; the landed crew attack-move on. The tribes' first raids are staggered (75 s apart per site), so a new colony isn't swamped early.

