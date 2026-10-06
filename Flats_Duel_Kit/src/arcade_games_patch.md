# arcade_games.cpp: the three edits that bring Flats Duel aboard

This file shows the edits as small before/after snippets, not a diff tool's patch, so they can be applied by hand or with the Edit tool.

## 1. Includes (top of the file)
```cpp
#include "arcade_game.h"
#include "arcade_session.h"
#include "flats_duel.h"      // + Flats Duel
#include "scuttle.h"
```

## 2. The registry row in `Info()`
```cpp
        {"Flats Duel", 2, 2, false, 0, true},     // was: false (built)
```
The duel is turn-based (`realtime = false`), so the session sends a snapshot after every change on the reliable channel. That's exactly what the engine expects: one micro-step per `Act`/`Tick` that returns true.

## 3. The host class, the hash and the factory
- Paste `src/flats_duel_host.inc` into the anonymous namespace next to `ScuttleHost`.
- In `DataHash()`, after the Scuttle lines:
```cpp
    flats::duel::HashDuelData(w);   // every duel number, the catalogue, presets and skins
```
- In `MakeGameHost`:
```cpp
        case G_FLATS_DUEL: return std::make_unique<FlatsDuelHost>();
```

## Something to be aware of in the session (no change needed today)
`Session::SendState` calls `Snapshot(PlayerOfSeat(i))`. A seat that isn't a player would get `-1`. Scuttle treats `-1` as "everything", but the duel treats it as a **spectator** (both hands hidden) on purpose, so a future spectator seat can never receive a hand. Every seat is a player today, so `-1` never happens yet. When spectators are added (the Master Reference allows up to 4), keep `-1` meaning spectator.
