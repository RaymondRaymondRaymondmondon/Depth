# Flats Duel kit: what was done, and what comes next

**For the Claude Code session that implements Flats Duel.** This file is the handover. `GUIDE.md` is the step-by-step manual; `docs/6_Depth_Flats_Duel.md` is the design.

Kit written 2026-09-30, against the Depth code of that day (branch `claude/flats-multiplayer-arcade-1ae20e`, after commit `6d77be9`).

## Where things stand

| Part | State | Where |
|---|---|---|
| Rules engine (headless) | Done, compiled, tested | `src/flats_duel.h/.cpp` |
| Numbers, presets, skins, emotes | Done, tested, balanced by simulation | `src/flats_duel_data.h/.cpp` |
| Network host for the arcade session | Done, tested through bytes | `src/flats_duel_host.inc` + `src/arcade_games_patch.md` |
| Opponent art, icons, turn clock | Done, compiled, rendered | `src/flats_duel_art.h/.cpp`, `previews/` |
| Client table screen | Reference only, **never compiled** | `src/flats_duel_scene.inc` |
| Setup / draft / build / sideboard panels | Mockups only | `previews/mockup_*.png` |
| Sound cues | Written, **never played** | `src/duel_cues.inc` |
| Design document | Ready to copy | `docs/6_Depth_Flats_Duel.md` |
| `--flats-duel-sim`, `--net-loop` duel run, shots | Not written: the kit's `tools/duel_check.cpp` is the model | GUIDE.md steps 1, 2, 4 |

**Nothing in the game's own source was changed.** Every file here still has to be copied or pasted into `src\` (GUIDE.md says where).

## What was done

### 1. Read the plan and the code
- **Documents:** the Master Reference's Flats Duel section (pages 30-31) and the arcade networking design (`docs/design/5_Depth_Arcade_Networking.md`).
- **Networking code:** the arcade's `GameHost` interface, `arcade_session`, and Scuttle as the model game.
- **Flats code:** the single-player engine (`flats_card`, `flats_board`), and in `flats.cpp` its drawing (`DrawDealer`, `DrawBattle`, `ApplyEvents`, `CellRect`).

### 2. Solved the core problem: a symmetric board from an asymmetric engine
The single-player engine plays "you" against "the dealer": dealer-only queue rules, a dealer AI, and charms and items only on your side. Rewriting it would have risked single-player. Instead the duel keeps the real `Battle` in the **active player's frame**:
- Whoever's turn it is plays as `Side::YOU` with the real `Draw`/`Play`/`UseItem`/`EndTurn`/`Advance`.
- At the turn's end the board is flipped, and the hands and decks change places.
- Row 0 (the dealer's queue) simply becomes the opponent's reserve.

`Serialize` flips again for each viewer, so every client always sees itself in rows 2-3, exactly as single-player draws. **No existing engine, session or network file needs changing.**

### 3. Built the engine around it (`flats_duel.cpp`)
- **Phases:** setup at the table, then an open draft, deck building, a game, and sideboarding.
- **Rule changes for two humans:**
  - Undying returns at most twice per player per game
  - the banned cards
  - items chosen at setup
  - a round limit, with sudden death and a tie limit
- **The host's side:**
  - turn timers: when one runs out, the upkeep draws from the deck and the bell rings itself
  - pacing: one combat lane per step, so both screens can animate every strike
  - AI seats, which the session also uses to replace a dropped player after 2 minutes
  - validation of every action
- **The wire:**
  - a compact action format
  - per-viewer snapshots: the full state they may see, plus the last step's events in their own frame, so a rejoin just works

### 4. Proved it
The kit's checker (`tools/duel_check.cpp`) runs thousands of bot matches through the host's real clock.

- **Leak test:** changes everything the viewer shouldn't know (the opponent's hand and deck order, items, the deck/sideboard split, the host's random state) and requires the snapshot to come out **byte-for-byte** the same. 21,012 snapshots passed.
- **Views test:** both players decode every snapshot exactly, and their boards mirror. 10,264 snapshots passed.
- **Wire test:** one seat plays only through bytes (`WriteAction` → `GameHost::Act` → `Snapshot` → `ReadView`). 40 matches, 0 failures.

### 5. Balanced it by simulation
- **First-player edge.** Unbalanced, the first player won 55-57%. The Master Reference's fix (+1 bone and a Minnow) overshot to 43-44%. After sweeping every combination:
  - shipped: +1 bone, and the scales start 1 notch (Draft) or 2 notches (Constructed/Quick) toward the second player
  - result: the first player wins **50.6% / 50.3% / 50.7%** (4,000 matches a mode; target 48-52)
- **The six presets** started at 23-84%. A per-card statistic showed the tier-3 finishers decide games: the side that plays one wins 68-82%. Giving each preset about two finishers brought them to **43-59%** overall.

### 6. Drew the opponent (`flats_duel_art.cpp`)
- **Style:** the same technique as today's `DrawDealer` (inked layered ellipses, a cold rim light, hatching, slivers of light for eyes), so a skinned opponent looks at home at the table.
- **10 skins** with their own hats, hands, faces and lines.
- **Poses:** expressions, emotes, per-skin tells, and a **reach** that lays a card in a lane.
- **Body and hands are separate layers.** In the game the table covers the dealer's hands, so the body is drawn before the table and the hands after the board, resting on the felt.

### 7. Wrote the rest
- the icons and turn clock
- the reference screen: a "mirror battle" that reuses all of flats.cpp's drawing, with every local rules call replaced by a sent action
- the sound cues
- the design doc and GUIDE.md
- preview renders of every asset, and layout mockups of each screen

## Decisions made (the user may change any of them)
The full list, with reasons, is in GUIDE.md section 4. The ones to know:
- **Second-player bonus:** a scale notch instead of the Minnow (measured above).
- **Draft:** 13 packs, not the spec's 8. Eight can't give 20 cards plus a 6-card sideboard.
- **Match length:** Draft runs about 15-18 minutes, longer than the spec's 8-12. Quick is one game with 30 s turns.
- **Setup lives inside the game**, not the lobby, so the session is untouched. Only the host picks the mode.
- **Viewer -1 means spectator** (both hands hidden), unlike Scuttle's "everything". That's safer for the spectators the Master Reference plans.

## Open questions for the user (ask before building these parts)
1. **Constructed "unlocked cards":** the save has no Flats collection. Allow the whole catalogue (the current default), or start recording a collection from runs?
2. **The Atlantean Sovereign skin:** its unlock needs a "beat the Sovereign" flag in the save.
3. **Draft length:** is 15-18 minutes OK, or shrink it (`draftPacks` 10, or `pickSeconds` 8)?
4. **Arcade tokens:** should they ever unlock anything in single-player? This is the Master Reference's own open question.

## What comes next

### To finish the duel (the plan in GUIDE.md, in order)
1. **Engine and data into `src\`,** plus `--flats-duel-sim` in main.cpp (port `duel_check`'s sim, matrix, preset, leak and views checks). Gate: the rates above reproduce.
2. **The GameHost** into `arcade_games.cpp`, and a duel run in `--net-loop` with the packet-log check. Gate: `--net-loop` and `--net-loop 100` pass.
3. **The art** into `src\`, with a sprite page. Gate: `--sprites` matches `previews/dealer_skins.png`.
4. **The table screen:**
   - compile and fix `flats_duel_scene.inc`
   - factor `DrawBattle` so the hand fan, bell, items and input are shared, not copied
   - build the four panels from the mockups
   - call it from `arcade.cpp`
   - add `duel_*` shots

   Gate: the shots look right, and a practice duel against the AI plays through by hand.
5. **Sound:** paste the cues. Gate: `--audio-test` passes.
6. **Profile:** tokens and owned skins in the arcade profile.
7. **Docs:** copy the design doc into `docs/design/`, add a CLAUDE.md section, and log the numbers in `docs/MASTER_PROGRESS.md`.

### After that: improvements
- **A stronger bot.** The auto-player is the single-player "sensible player" plus simple item and draft rules. It over-values big blood cards and under-plays Tidecaller and Brine, so Coral Queen and Squid read as weak. Options:
  - **one-ply lookahead:** try each legal play on a copy of the Battle and score the board after combat. The engine is cheap enough; `Battle` copies are small.
  - **human logs:** record real duels and retune presets and bans from the logs, not only from bot games.
- **Spectators** (up to 4 in the Master Reference). The engine already serializes viewer -1 with both hands hidden. The session needs non-player seats that `PlayerOfSeat` maps to -1.
- **Rigged opponent.** When stage 12's rigged dealer arrives (`ShadeLimb`/`ShadeBall` inside the figure shader), keep `Skin` and `DealerPose` and re-implement `DrawDuelDealer` on the rig. The poses are already channel-style offsets.
- **Card physics.** The Master Reference's `flats_physics.cpp` (card bodies with springs) would serve both single-player and the duel. The duel's events are the same event list, so it needs nothing extra.
- **Voiced lines.** Each skin has lines and a voice pitch and rate. Showing them as ink speech on the felt at the right moments (greet, ahead, behind, big hit, win, lose), with the formant mumble, is cheap.
- **Pause budget and match autosave.** These are the networking doc's N3 items, not built for any game yet.
- **Balance watch.** If the Flats catalogue gains cards (the Master Reference's twelve new sigils and deck archetypes), rerun `duel_check sweep`, `matrix` and `cards`. A card with both Burrower and Sharp Quills would need the 3-damage quill cap added to `flats_board`.

## Rebuilding and rechecking the kit
The script builds `tools\duel_check.exe` and `tools\render_previews.exe` into the kit's `build\` folder (gitignored). Run it from wherever the kit folder is:
```bash
powershell -ExecutionPolicy Bypass -File Flats_Duel_Kit\tools\build_kit.ps1 -Previews
```
- The script finds the Flats engine and raylib through its `-Depth` parameter, default `C:\Users\phill\Downloads\Depth`.
- Add `-RefreshVendor` to re-copy the Flats engine from `Depth\src` first. Do it if `flats_board` or `flats_card` changed since 2026-09-30, then rerun the balance checks.
- It prints "All duel checks pass." and rewrites `previews\`.
