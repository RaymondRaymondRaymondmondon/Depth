# Flats Duel: how to bring it into Depth
**For Claude Code working in the Depth repository.** This kit sits in `Depth\Flats_Duel_Kit\` and is not part of the build. It was developed without changing any of Depth's own files. It holds a finished, tested rules engine for the duel, its data, the opponent art, icons, sound cues, and reference code for the screen. This guide says where each piece goes, in what order, and how to check each step.

**Start with `HANDOFF.md`**: what was done, what was measured, and what comes next. Then read these, as CLAUDE.md asks:
- the Master Reference, "Arcade game 1: Flats Duel" (pages 30-31)
- `docs/design/5_Depth_Arcade_Networking.md`, step N5
- `docs/6_Depth_Flats_Duel.md` in this kit: the duel's design, with every number measured

---

## 0. What's in the kit

| Path | What it is | Status |
|---|---|---|
| `src/flats_duel.h/.cpp` | The headless duel engine: setup, open draft, build, games, sideboard, timers, pacing, AI seats, hidden-information serialization, actions over the wire, the simulator and self-tests. | **Compiled and tested** against copies of Depth's `flats_card`/`flats_board` |
| `src/flats_duel_data.h/.cpp` | Every number (`Tuning`), the modes, the ban list, deck rules, the six presets, the 10 dealer skins with their lines, the emotes, and `HashDuelData` for the arcade's DataHash. | **Tested** |
| `src/flats_duel_host.inc` | The `FlatsDuelHost : GameHost` class to paste into `arcade_games.cpp`. | **Compiled and tested** (the `wire` check drives it through bytes) |
| `src/arcade_games_patch.md` | The three small edits to `arcade_games.cpp`. | Instructions |
| `src/flats_duel_art.h/.cpp` | The opponent across the table, in any skin, plus the duel's icons and turn clock. The figure comes with poses for idle, expressions, emotes, tells, and the reach to lay a card. raylib only. | **Compiled and rendered** (see `previews/`) |
| `src/flats_duel_scene.inc` | The client's table screen, written against flats.cpp's real helper names, using a "mirror battle". | **Reference, NOT compiled** (it needs flats.cpp's internals). Expect small fixes |
| `src/duel_cues.inc` | 20 cue rows for `data.cpp`'s `CueTable`, using only existing recipes. | Untested (run `--audio-test`) |
| `docs/6_Depth_Flats_Duel.md` | The design part for `docs/design/`. | Ready to copy |
| `previews/*.png` | Every asset rendered, plus layout mockups of the table, draft, setup and sideboard screens. | Visual reference |
| `tools/duel_check.cpp` | The kit's checker: sim, preset matrix, leak test, view test, wire test, sweep, tune, per-card stats. | The model for `--flats-duel-sim` |
| `tools/render_previews.cpp` | Renders `previews/`. | Tool |
| `tools/build_kit.ps1` | Builds both tools with VS 2026 (`-Previews` also renders; `-RefreshVendor` re-copies the engine from `Depth\src`). | Tool |
| `vendor/` | Read-only copies of `flats_card.*`, `flats_board.*`, `net_msg.h` and `arcade_game.h` from Depth, as of 2026-09-30. | Don't copy back |

To see it all work before touching Depth:
```bash
powershell -ExecutionPolicy Bypass -File C:\Users\phill\Downloads\Depth\Flats_Duel_Kit\tools\build_kit.ps1 -Previews
```
It prints "All duel checks pass." and rewrites `previews\`.

---

## 1. The idea that makes it small: the perspective flip
The single-player engine (`flats_board`) always plays "you" (rows 2-3) against "the dealer" (rows 0-1). The duel reuses it **unchanged**:
- The `Battle` is always kept in the **active player's frame**. Whoever's turn it is plays as `Side::YOU` with the real `Draw`/`Play`/`UseItem`/`EndTurn`/`Advance`.
- When the Battle reaches `FOE_START`, the turn is over. The duel then:
  - flips the board with `FlipBoard`: rows 0↔3 and 1↔2, the scales negated, bones, rules, returns and finds swapped
  - swaps `hand`/`foeHand` and `deck`/`foeDeck`
  - hands the table to the other player at `YOU_DRAW`
- Row 0, the dealer's queue in single-player, is simply the opponent's reserve here. A knocked-back card lands there, and it steps forward in its owner's own end phase.
- `Serialize` flips again as needed, so **every viewer always sees themselves in rows 2-3**, exactly as flats.cpp draws single-player. The screen can reuse `CellRect`, `DrawBoardCard`, `ApplyEvents` and the rest as they are.
- `Battle::dealer = 2` switches off the Novice's and Tidewife's free-Minnow training wheels. Charms, totems, boss and momentum are all off.

**Nothing in `flats_board.*`, `flats_card.*`, `arcade_session.*` or `net*.cpp` has to change.**

---

## 2. Step by step

Each step ends with a check. Don't start the next step until it passes.

### Step 1: the engine and its data (headless)
1. Copy `src/flats_duel.h`, `src/flats_duel.cpp`, `src/flats_duel_data.h` and `src/flats_duel_data.cpp` into `Depth\src\`.
2. CMakeLists.txt: add `src/flats_duel.cpp src/flats_duel_data.cpp` to the source list next to `src/flats_board.cpp`.
3. main.cpp: add `--flats-duel-sim`, modelled on `tools/duel_check.cpp`. At minimum, call `flats::duel::Simulate(n, mode, seed)` for each mode and print the first-player rate, then run `VerifyNoLeak` and `VerifyViews`. Port `RunMatrix` and `RunPresetCheck` too. Exit non-zero if a check fails, like the other self-tests.
   - main.cpp uses **CRLF** line endings, so edit it with the Edit tool, not PowerShell `.Replace`.
4. Build with `.\build.ps1`.
5. **Check:**
```bash
build\Release\depth.exe --flats-duel-sim 4000
```
Expect about 50.6 / 50.3 / 50.7% first-player wins (Draft / Constructed / Quick), the presets within 40-60%, and the leak and views tests PASS.

If a number moves, the vendor copies have drifted from Depth's engine. Compare `vendor\flats_board.cpp` with `src\flats_board.cpp`. If the engine changed since 2026-09-30, rerun the sweep in step 7 and retune `Tuning`.

### Step 2: the GameHost
1. Apply `src/arcade_games_patch.md`:
   - the include
   - `built = true` on the Flats Duel row
   - paste `flats_duel_host.inc` into `arcade_games.cpp`'s anonymous namespace
   - `flats::duel::HashDuelData(w)` in `DataHash()`
   - the `case G_FLATS_DUEL` in `MakeGameHost`
2. Extend `--net-loop` (`net_test.cpp`) with a duel run, following its Scuttle run:
   - a host plus two guests (or host + guest + an AI seat)
   - a Draft-mode match driven by `flats::duel::Bot` through `WriteAction` → `Session::Act`
   - a timeout (don't act for a turn)
   - a sideboard swap and an emote
   - a dropped guest who rejoins by token mid-game
   - both screens agreeing on the winner
3. **The packet-log check the Master Reference asks for.** In the net loop, decode every snapshot a guest receives with `ReadView`, and require:
   - `v.hand` holds exactly what that guest's own hand holds on the host
   - no `Drew` event with `v.evActor == 1` has an amount other than -1
   - the byte test from `VerifyNoLeak`: perturb the other player's hidden state on the host and re-serialize; the bytes must not change
4. **Check:**
```bash
build\Release\depth.exe --net-loop
build\Release\depth.exe --net-loop 100
```
Both pass. The second run adds lag and loss.

### Step 3: the opponent art
1. Copy `src/flats_duel_art.h/.cpp` into `src\` and add the `.cpp` to CMake.
2. `Tri` and `Glow` inside it are local stand-ins for the game's `DrawTri`/`Glow`. Either keep them or swap in the game's versions; they draw the same.
3. **Draw order matters:**
   1. `DrawDuelDealer(..., DL_BODY)`
   2. `DrawTable()`
   3. the board's slots and cards
   4. `DrawDuelDealer(..., DL_HANDS)`

   flats.cpp's own `DrawDealer` hides its hands under the table. The duel's hands must show, because they lay cards.
4. **Tiny bright particles:** the brooch glow is drawn inside the figure; that's fine. Any dust or bubble added later goes after `InkPass` (CLAUDE.md).
5. Add a sprite page, as CLAUDE.md asks for new characters: a `DrawDuelSkinsSpritePage` that draws the 10 skins. Copy `PageSkins` in `tools/render_previews.cpp`, which already frames them with the table.
6. **Check:**
```bash
build\Release\depth.exe --sprites sprites.png
```
Compare the result with `previews/dealer_skins.png`.

The figure is flat-shaded like today's `DrawDealer`, not rig-based. When stage 12's rigged dealer lands (`ShadeLimb`/`ShadeBall` between `BeginFigure`/`EndFigure`), keep the `Skin` and `DealerPose` data and re-implement `DrawDuelDealer` on the rig. The poses are channel offsets, and they map onto rig channels directly.

### Step 4: the table screen
1. Paste `src/flats_duel_scene.inc` into flats.cpp's anonymous namespace after `DrawBattle`. Fix whatever doesn't compile; it was written against the real names but never compiled.
2. Finish the four phase panels it leaves as comments, using the mockups (`previews/mockup_setup.png`, `mockup_draft.png`, `mockup_sideboard.png`) and flats.cpp's own `DeckGrid`, `Button` and `DrawInspector`.
3. Where the scene says "the rest of DrawBattle's body", **factor DrawBattle** so the hand fan, the draw buttons, the items, the bell and the input handling can be shared. Don't copy them. Every mutation of `U.bat` in single-player becomes an `Action` sent in the duel (the substitutions are listed in the scene file).
4. `DrawScale` prints "DEALER"/"YOU": give it a name parameter for the opponent.
5. Export `FlatsDuelScene(snapshot, stateVersion, send, me, them)`. Call it from `arcade.cpp` when `gSess.stage == S_PLAYING && gSess.game == G_FLATS_DUEL`:
   - `snapshot` is `gSess.Snapshot()`
   - `stateVersion` is `gSess.stateVersion`
   - `send` does `WriteAction` then `gSess.Act`
6. Keep arcade.cpp's shared frame on top: the pause banner, Table Talk, leaving (with its double confirm), and the match result with Rematch / Back to the lobby.
7. The Master Reference wants every arcade game inside the same porthole bezel. The duel is a full table (the design choice made here, flagged in section 4), so draw the bezel's brass rim as a frame around it.
8. `arcade.cpp`'s reel list already has the Flats Duel row. Draw `DrawDuelIcon(IC_REEL, ...)` on it (see `previews/reel_flats_duel.png`), and let the practice table start a duel against one AI seat, as Scuttle's does.
9. Add shots to `TakeShots` in main.cpp: `duel_setup`, `duel_draft`, `duel_build`, `duel_play`, `duel_reach`, `duel_sideboard`, `duel_result`. Use a memory-transport session (`DebugArcadeShot` shows how) with an AI opponent, stepped with `HostTick` to the right phase.
10. **Check:**
```bash
build\Release\depth.exe --shots shots duel_
```
Look at every PNG. Then play a practice duel by hand: draft, build, three games, sideboard, emotes, and let the clock run out once.

### Step 5: sound
1. Paste `src/duel_cues.inc` into `data.cpp`'s `CueTable` after the `arc.*` rows.
2. The screen plays:
   - `duel.turn` on your turn
   - `duel.reach` when their hand moves
   - `duel.clock` for your last 10 seconds
   - the emote cues (`EmoteOf(e).cue`)
   - `duel.game`, `duel.gamelost` and `duel.match` on the results
   - `duel.pick`, `duel.pass`, `duel.pack`, `duel.swap` and `duel.ready` in their panels
   - `duel.sudden` when `v.suddenDeath` turns on
3. Music: reuse the Flats table's music. It already speeds up when the scales pass ±5, per the Master Reference's sound table.
4. The skins' `voicePitch`/`voiceRate` drive the formant mumble for their lines, like single-player's dealers.
5. **Check:**
```bash
build\Release\depth.exe --audio-test
```
It passes: every registered cue sounds and nothing clips.

### Step 6: profile, tokens, skins
The Master Reference keeps the arcade profile (name, tokens, cosmetics, stats per game) in `depth_save.txt`. Today `arcade_profile.txt` holds only the name and id.
1. Add tokens (`TOKENS_PER_MATCH` 10, `TOKENS_PER_WIN` 25) and owned skins.
2. The setup panel only offers owned skins. The host does **not** check ownership: skins are cosmetic, and a profile is local.
3. The Sovereign skin unlocks when the single-player Sovereign is beaten. That needs a flag in the save; see the open questions.
4. Never pay gold into the main game.

### Step 7: docs and CLAUDE.md
1. Copy `docs/6_Depth_Flats_Duel.md` into `docs/design/`.
2. Add a short "Flats Duel" section to CLAUDE.md: the perspective flip, the files, `--flats-duel-sim`, the draw order (body, table, board, hands), and the tuning numbers.
3. Tick N5 in `5_Depth_Arcade_Networking.md` and log the balance numbers in `docs/MASTER_PROGRESS.md`.

---

## 3. Tuning later
Every number is in `Tuning` (`flats_duel_data.h`). Change numbers there, never in the rules. The kit's checker has the tools to retune:
```bash
duel_check tune 3000 <bones> <minnows> <skipDraw 0/1> <holdFire 0/1> <scaleNotches>
duel_check sweep 2000
duel_check matrix 6000
duel_check cards 3000 2
duel_check cards 3000 0
```
- `tune`: the first-player rate in every mode for one setting
- `sweep`: the whole grid
- `matrix`: every preset against every other
- `cards ... 2`: which cards win, in Quick
- `cards ... 0`: which cards win, in Draft

Port the ones you want into `--flats-duel-sim`.

**The bot's biases.** The auto-player (`Bot`) is the single-player sensible player plus item and draft heuristics. It over-values big blood cards and under-plays Tidecaller and Brine, so Coral Queen and Squid read weak. Human results will differ: log real matches before trusting the per-card table.

---

## 4. Decisions made here (the user may change any of them)
1. **Second-player bonus:** +1 bone, and the scales start 1-2 notches toward them, instead of the spec's Minnow, which overshoots to 43%. Measured in section 2 of the design doc.
2. **Draft:** 13 packs of 4, open (both see every pick), keep 20 + 6 sideboard. The spec's "8 packs of 4" can't give 20 cards each.
3. **Match length:** a Draft match runs longer than the Master Reference's 8-12 minutes.
   - The draft is 52 picks at up to 12 s each, about 4 minutes at a normal pace.
   - Then a 75 s build.
   - Then games of about 5 rounds, roughly 4-5 minutes each with human turns.
   - That's about 15-18 minutes. Quick (one game, 30 s turns) fits the "five-minute game".

   To shorten Draft: `draftPacks` 10 with `deckFromDraft` 20 (no sideboard), or `pickSeconds` 8.
4. **Items:** two chosen at setup, refilled each game, with Scavenger finds on top.
5. **The Fishhook** targets the opponent's reserve.
6. **Round limit:** a round is both turns. Sudden death after 20. A tie after 30, replayed. A match of 5 games with no winner is drawn.
7. **Quick** is a single game.
8. **Staples:** Draft and Quick decks get 2 Minnows + a Ballast Cask. Constructed decks are taken as submitted.
9. **Setup** happens at the table as the game's first phase (no lobby or session change). Only player 0, the host, picks the mode.
10. **Viewer -1** is a spectator (both hands hidden), not "everything".
11. **Tells** are visual only.
12. **The duel is a full-screen table** with the arcade's bezel as a frame, not a small screen inside the porthole.

## 5. Open questions for the user
- Constructed says "cards they have unlocked in single-player", but the save has no Flats collection. Should Constructed allow the whole catalogue (the kit's default), or should a collection be recorded from runs, for example every card that has been in a deck when a run ended?
- How should the Atlantean Sovereign skin unlock? It needs a "beat the Sovereign" flag in the save.
- Is a 15-18 minute Draft match acceptable, or should the draft shrink (see decision 3)?
- Should arcade tokens ever unlock single-player cosmetics (the Master Reference's own open question)?
