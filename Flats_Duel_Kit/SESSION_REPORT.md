# Session report, 2026-09-30: Flats Duel kit and ZeroTier online play

**For the next Claude Code session working on Depth.** This explains everything done in one session so you can carry on without re-deriving it.

It has two parts:
- **Part A:** the Flats Duel kit, a ready-made, tested package for arcade game 1.
- **Part B:** the ZeroTier setup that makes the arcade's LAN mode work across the internet.

Read CLAUDE.md first, as always. For the duel, `HANDOFF.md` and `GUIDE.md` in this folder go deeper than this report.

---

## 0. Where everything is

| What | Where |
|---|---|
| The kit (this folder) | `C:\Users\phill\Downloads\Depth\Flats_Duel_Kit\` (main checkout) and the same folder in the worktree `...\Depth\.claude\worktrees\github-cloud-connection-847418\Flats_Duel_Kit\` (identical) |
| Original build location | `C:\Users\phill\Downloads\Depth_FlatsDuel_Kit\` (the first copy, outside Depth; now superseded) |
| This session's branch | `claude/flats-multiplayer-arcade-1ae20e` (worktree above). The kit folder is **untracked and uncommitted** in both checkouts |
| The plan it follows | `Depth — Master Reference for Claude Code.pdf` (root), "Arcade game 1: Flats Duel" (pp. 30-31); `docs/design/5_Depth_Arcade_Networking.md` (step N5) |
| Claude's memory | `C:\Users\phill\.claude\projects\C--Users-phill-Downloads-Depth\memory\` (`depth-networking-steamfree.md` has the ZeroTier facts) |

**The user's instruction during the kit work was "do not modify the depth folder at all".** So nothing in Depth's source, CMake, docs or CLAUDE.md was changed. The kit was later copied into `Depth\Flats_Duel_Kit\` at the user's request, as a new folder only. Integrating it into `src\` is the next session's job, following GUIDE.md.

**The worktree guard.** A hook blocks the Write/Edit tools from writing into the main checkout (`C:\Users\phill\Downloads\Depth\...`) while a session runs in a worktree. Work in the worktree path.
- When the user explicitly asked for files in the main checkout, they were copied there with `robocopy` after their confirmation.
- Don't use that as a routine workaround. Ask first.

---

# Part A: the Flats Duel kit

## A1. The goal
The Master Reference makes Flats Duel the first of the Deep Arcade's multiplayer games: Flats against a person, best of three, on a symmetric board, with Draft, Constructed and Quick modes, timers, sideboarding, six emotes and dealer skins. It asks for three checks:
- a bot-versus-bot sim with a first-player win rate of 48-52%
- two clients finishing a best-of-three over LAN
- a packet log showing no client ever receives the opponent's hand

The user asked for the **assets and a how-to guide ready-made**, so the session that builds the real game has less to do.

## A2. What the existing code looked like (read in this session)
- **`flats_card.*`** (headless): the `Card` type, 25 sigils, and the catalogue of 45 cards (ids 0-44: Minnow, tiers 1-3, dealer-only tier 0, the Kraken, the Atlantean court, Selenis).
- **`flats_board.*`** (headless): the `Board` (a 4x4 grid) and `Battle` (turn phases `YOU_DRAW` ... `FOE_END`, advanced one micro-step at a time by `Advance`). It is **asymmetric**:
  - row 0 is the dealer's *queue* (`AdvanceQueue`, `FoeChoose`, the dealer AI)
  - charms, items, totems and the Novice's free Minnows exist only on your side
  - `Foresight` reacts only to your attacks; `Massive`/`Tidal Pull` are the boss's
- **`arcade_game.h`** is the interface every arcade game implements: `GameHost { Start(players, seed); Act(player, Reader&); Tick(dt, aiMask); Snapshot(viewer, Writer&); Over(); }`.
  - The session sends a snapshot after every `Act`/`Tick` that returns true (reliable, for turn-based games), and on a rejoin.
  - It has **no channel for game settings**: `Start` only gets a player count and a seed.
- **`arcade_games.cpp`** holds the registry (`Info`), `ScuttleHost` (the model to copy), `DataHash()` (every rule peers must share) and `MakeGameHost`. The Flats Duel row exists with `built = false`.
- **`flats.cpp`** is the single-player table screen. Everything it draws reads `U.bat` (the live `Battle`):
  - `DrawBattle`, `DrawBoardCard`, `Affordable`, `TryPlay`
  - `ApplyEvents(events, prevBoard)` animates any event list
  - `CellRect` gives the board geometry
  - `DrawDealer` draws the dealer, and `DrawTable` is drawn over him, so his hands are hidden
- **`data.cpp` `CueTable`**: a sound cue is `{name, bus, recipe, freq, dur, gain, pitchVar, variants, priority, maxSim, duck}`, with recipes from `CueRecipe` in `sound.h`.

## A3. The key design: the perspective flip
The duel doesn't rewrite the engine, and doesn't touch it. The real `Battle` is always kept **in the active player's frame**:
- Whoever's turn it is plays as `Side::YOU` with the real `Draw`/`Play`/`UseItem`/`EndTurn`/`Advance`.
- The Battle reaching `FOE_START` marks the end of the turn. The duel then:
  - calls `FlipBoard`: rows 0↔3 and 1↔2, the scale negated, and bones, rules, Undying returns and Scavenger finds swapped
  - swaps `hand`/`foeHand` and `deck`/`foeDeck`
  - sets the other player to `YOU_DRAW`
- Row 0 becomes "the opponent's reserve". A knocked-back card lands there and steps forward in its owner's own end phase, because `EndPhase(Side::YOU)` moves the reserve.
- `Battle::dealer = 2` turns off the Novice's and Tidewife's free Minnows. Charms, totems, boss and momentum are unused.
- `Serialize` flips again for each viewer, so **every client always sees itself in rows 2-3**, exactly as single-player draws. The screen can reuse flats.cpp's geometry, drawing and `ApplyEvents` unchanged.

The duel's own rules are applied around each engine call (`BeginStep`/`EndStep` in `flats_duel.cpp`):
- **Undying cap:** once a player has had 2 returns, their creatures on the board lose the sigil. If two die in one step, the extra copy is removed from the hand.
- **Scavenger finds** become a random pack item, if the pack has room.
- **"Big hit"** (4 or more in one blow) is recorded, so the opponent's figure can react.

**Setup is the game's own first phase** (`PH_SETUP`), so the session needed no changes. Player 0 (the host) picks the mode; both players pick a skin and items, submit a deck (Constructed) or pick a preset (Quick), and ready up.

## A4. The files in the kit

| File | What it is | Tested? |
|---|---|---|
| `src/flats_duel.h/.cpp` | The engine. Phases: `PH_SETUP` → `PH_DRAFT` (open Rochester draft) → `PH_BUILD` → `PH_PLAY` → `PH_GAME_OVER` → `PH_SIDEBOARD` → … → `PH_MATCH_OVER`. Also `Action` with `WriteAction`/`ReadAction`, `Legal`/`Apply`, `HostTick` (timers, the combat pacing of one lane per `stepSeconds`, AI seats), `Bot`, `Serialize(state, viewer)`/`ReadView` (a client's decoded `View`), `FlipBoard`/`FlipEvent`, `Simulate`, `VerifyNoLeak` and `VerifyViews`. | Yes |
| `src/flats_duel_data.h/.cpp` | Every number (`Tuning` / `Tune()`), `RulesOf(mode)`, `Banned`, `Draftable`, `DraftWeight`, `ValidateDeck`, 6 `Presets`, `WithStaples` (+2 Minnows +1 Ballast Cask), 10 dealer `Skin`s (colours, hat, hands, face, voice, tell, 6 lines each), 6 emotes, the token values, and `HashDuelData` for DataHash. | Yes |
| `src/flats_duel_host.inc` | `FlatsDuelHost : GameHost`: paste it into `arcade_games.cpp`. | Yes (the wire test) |
| `src/arcade_games_patch.md` | The 3 small edits to `arcade_games.cpp`. | n/a |
| `src/flats_duel_art.h/.cpp` | `DrawDuelDealer(skin, pose, anchor, scale, t, look, layer)`, drawn in `DrawDealer`'s style. Poses: `IdlePose`, `ExpressionPose` (6), `EmotePose` (6), `TellPose` (per skin), `ReachPose` (lays a card in a lane), combined with `Add`/`Mix`. **Layers:** `DL_BODY` goes before `DrawTable()`, `DL_HANDS` after the board. Also `DrawDuelIcon` (14 icons) and `DrawTurnClock`. raylib only. | Compiled and rendered |
| `src/flats_duel_scene.inc` | The client table screen, a "mirror battle": each snapshot is copied into `U.bat` so flats.cpp's drawing works, and every local rules call becomes a sent `Action`. The four phase panels are left as comments pointing at the mockups. | **Never compiled** (needs flats.cpp internals) |
| `src/duel_cues.inc` | 20 `CueTable` rows (`duel.*`, `duel.em.*`), existing recipes only. | **Never played** |
| `docs/6_Depth_Flats_Duel.md` | The design part for `docs/design/`, with every number measured. | n/a |
| `previews/*.png` | Skins, expressions, emotes, tells, reach, icons, the reel, and mockups of the table, draft, setup and sideboard. | Rendered |
| `tools/duel_check.cpp` | The checker: `sim`, `matrix`, `leak`, `views`, `wire`, `sweep`, `tune`, `cards`, plus a preset check. | Tool |
| `tools/render_previews.cpp` | Renders `previews/` (a hidden raylib window, off-screen targets). | Tool |
| `tools/build_kit.ps1` | Builds with VS 2026 into the kit's `build\` (gitignored). `-Previews` also renders; `-RefreshVendor` re-copies the engine from `Depth\src`; `-Depth <path>` points at Depth. | Tool |
| `vendor/` | Copies of `flats_card.*`, `flats_board.*`, `net_msg.h` and `arcade_game.h` from 2026-09-30, used only by the kit's build. | n/a |
| `HANDOFF.md`, `GUIDE.md`, `README.md` | The handover, the step-by-step integration manual, and the overview. | n/a |

Run everything with the line below. It prints "All duel checks pass." and rewrites `previews\`:
```bash
powershell -ExecutionPolicy Bypass -File C:\Users\phill\Downloads\Depth\Flats_Duel_Kit\tools\build_kit.ps1 -Previews
```

## A5. Results (bot vs bot)

**First player's win rate**, 4,000 matches per mode. The target is 48-52%.

| Draft | Constructed | Quick |
|---|---|---|
| 50.6% | 50.3% | 50.7% |

**Each preset's overall win rate**, 6,000 Quick-mode games:

| Bone | Blood | Airborne | Big Fish | Shell Wall | Swarm |
|---|---|---|---|---|---|
| 51% | 59% | 43% | 46% | 45% | 56% |

**The self-tests:**
- **Leak:** 21,012 snapshots passed. The test changes every hidden thing (the opponent's hand and deck order, items, the deck/sideboard split, the preset, the host's random state) and requires each snapshot to stay **byte-identical**.
- **Views:** 10,264 snapshots decoded exactly, and the two players' boards mirror.
- **Wire:** 40 matches finished with one seat playing only through bytes, with 0 failures.

**Game length:** about 5.2-5.5 rounds and about 71-76 combat steps a game. Past round 20 in 0.1-0.3% of games; ties under 0.01%.

**How the balance was found:**
1. With no bonus, the first player won 55-57%.
2. The Master Reference's "+1 bone and a Minnow" overshot to 43-44%. The Minnow alone is worth about 11 points.
3. A sweep of every combination found the shipped setting: +1 bone, and the scales start tipped toward the second player, 1 notch in Draft and 2 in Constructed/Quick. Preset decks give the first mover more tempo.
4. The presets started at 23-84%. The per-card statistic (`duel_check cards`) showed tier-3 finishers decide games: the side that plays a Sea Serpent, Great White, Sperm Whale, Chambered Titan or Drowned King wins 68-82%. Giving every preset about two finishers fixed it.

## A6. Decisions made (the user may change them; details in GUIDE.md §4)
1. **Second-player bonus:** the scale notch instead of the Minnow.
2. **Draft:** 13 packs of 4, open draft, keep 20 + 6 sideboard. The spec's 8 packs can't give 20 + 6.
3. **Match length:** Draft runs about 15-18 minutes, over the spec's 8-12. Quick is one game with 30 s turns.
4. **Items:** 2 chosen at setup, refilled each game. Scavenger finds are extra.
5. **The Fishhook** takes from the opponent's reserve (row 0).
6. **Game limits:** a round is both turns. Sudden death after round 20; a tie after round 30, replayed; the match is drawn at 5 games.
7. **Banned:** Gilt (no pot), Massive, Tidal Pull, and dealer-only cards. A Kraken is allowed only by growing from a Kraken Spawn.
8. **Staples:** Draft and Quick decks get 2 Minnows + a Ballast Cask. Constructed decks are taken as submitted.
9. **Viewer -1 is a spectator** (both hands hidden), not "everything" as in Scuttle.
10. **Tells** are visual only.
11. **The duel is a full-screen table** inside the arcade's bezel frame.
12. **The Sharp Quills 3-damage cap isn't implemented.** No catalogue card can trigger it; add it to `flats_board` if one ever can.

## A7. Open questions for the user
1. **Constructed "unlocked cards":** the save has no Flats collection. The kit allows the whole catalogue; the alternative is to start recording a collection.
2. **The Atlantean Sovereign skin:** it needs a "beat the Sovereign" flag in the save.
3. **Draft length:** accept 15-18 minutes, or shrink the draft?
4. **Arcade tokens:** should they ever unlock single-player cosmetics?

## A8. What to do next for the duel (GUIDE.md has each step's check)
1. **Engine and data into `src\`** + CMake + `--flats-duel-sim` in main.cpp (main.cpp is CRLF: use the Edit tool).
2. **GameHost** into `arcade_games.cpp`, plus a duel run in `--net-loop` (`net_test.cpp`) with the packet-log check.
3. **Art** into `src\`, plus a sprite page.
4. **The screen:**
   - compile and fix `flats_duel_scene.inc`
   - factor `DrawBattle` so single-player and the duel share it
   - build the four panels from the mockups
   - call it from `arcade.cpp`
   - add `duel_*` shots
5. **Sound cues**, then `--audio-test`.
6. **Arcade profile:** tokens and owned skins.
7. **Docs:** `docs/design/6_Depth_Flats_Duel.md`, a CLAUDE.md section, and MASTER_PROGRESS.

**Later:** a stronger bot (one-ply lookahead on a copied `Battle`), spectators, the opponent on stage 12's rig, card physics, voiced lines, and a retune whenever the catalogue changes.

---

# Part B: ZeroTier (online play without Steam)

## B1. Why ZeroTier
The user's chosen online plan (memory `depth-networking-steamfree`) is Steam-free, in four layers:
1. **A virtual LAN (ZeroTier):** zero code. The arcade's LAN mode just works across the internet.
2. **UPnP port forwarding.**
3. **A rendezvous server** with hole punching.
4. **A relay**, only if needed.

This session did layer 1 on the user's PC.

Depth's arcade networking is already built: GameNetworkingSockets is in `external/gns` (`GameNetworkingSockets_s.lib`, found by CMake), and the current `build\Release\depth.exe` (2026-09-30 15:01) has it.
- **Ports:** UDP 47777 for the LAN beacon and Browse, UDP 47778 for the game.
- **Joining:** by Browse (listening for beacons), by the 6-letter code (matched against beacons), or by typing an IP address.

## B2. The user's setup (done on 2026-09-30)

| Item | Value |
|---|---|
| ZeroTier network ID | `8d1c312afae623eb` (private: every device must be ticked **Auth** at my.zerotier.com) |
| Downloads | `C:\Users\phill\Downloads\ZeroTier One.msi` (the client, installed), `C:\Users\phill\Downloads\ZeroTierOne-dev.zip` (the source; github.com/zerotier/ZeroTierOne; not needed) |
| Service | `ZeroTierOneService`, running, automatic start |
| Tray UI | `C:\Program Files (x86)\ZeroTier\One\zerotier_desktop_ui.exe` |
| CLI | `C:\Program Files (x86)\ZeroTier\One\zerotier-cli.bat` or `C:\ProgramData\ZeroTier\One\zerotier-one_x64.exe -q`. **Needs an administrator shell**; the Claude session can't run it (`authtoken.secret` isn't readable) |
| Auto-assign pool | `10.82.215.1` - `10.82.215.254` (a /24: correct for Depth's Browse) |
| This PC's adapter | `ZeroTier One [8d1c312afae623eb]`, up |
| This PC's ZeroTier IP | **`10.82.215.253/24`** |
| Windows network profile | "Network 3" (the ZeroTier adapter) set to **Private**. Wi-Fi ("SkibidiGoon") left Public on purpose |
| Member advanced settings | "Do NOT Auto-Assign IPs", "Allow Ethernet Bridging" and "Exclude from SSO" all **off** (correct) |

**Not done yet:**
- No friend or second PC has joined. The ARP table on the ZeroTier adapter showed only the broadcast address, `10.82.215.255`.
- The first real test (host Scuttle, a friend joins) hasn't happened.
- Windows Firewall will ask about depth.exe the first time it hosts. The user should tick **Private**.

## B3. What happened, in order (useful if the user or a friend repeats it)
1. The service was running but the PC hadn't joined: no adapter.
2. The user found only Start-menu options (Run, Run as administrator, and so on). The Join option lives on the **tray icon's right-click menu**. The fallback is an admin `cmd` running:
```bash
"C:\Program Files (x86)\ZeroTier\One\zerotier-cli.bat" join 8d1c312afae623eb
```
3. The user opened Windows' **802.1X "Ethernet authentication settings"** by mistake. They were told to cancel and turn it back off; it's irrelevant.
4. After the join, the adapter was up but had no IP until the device was ticked **Auth** in Members. After that it got `10.82.215.253/24`.
5. Setting the profile to Private:
   - `Set-NetConnectionProfile -InterfaceAlias "ZeroTier One [8d1c312afae623eb]" ...` **failed with "Invalid query"**, because PowerShell treats `[...]` as a wildcard.
   - What worked, in an admin PowerShell:
```bash
Get-NetConnectionProfile -Name "Network 3" | Set-NetConnectionProfile -NetworkCategory Private
```
   - Verified with `Get-NetConnectionProfile`.

## B4. Steps for each friend
1. Install ZeroTier (zerotier.com/download, or the user's MSI).
2. Join `8d1c312afae623eb`: tray icon → Join New Network, or the admin `zerotier-cli.bat join` command.
3. The user ticks **Auth** for the friend's device in my.zerotier.com → Members.
4. Set the friend's ZeroTier network to Private. Find its name with `Get-NetConnectionProfile`, then run `Get-NetConnectionProfile -Name "<name>" | Set-NetConnectionProfile -NetworkCategory Private` in an admin PowerShell.
5. Run **the same `depth.exe`** as the host. The handshake rejects a different `BuildId()` or `DataHash()`.
   - Zip `Depth\build\Release\`, leaving out `depth_save.txt`, `settings.txt` and `arcade_profile.txt`.
   - If a DLL is missing, the friend needs the Microsoft Visual C++ Redistributable (x64).
   - Re-send after every rebuild.
6. Test: `ping 10.82.215.253` from the friend's PC. Then the user **Hosts** Scuttle in the arcade, and the friend clicks **Browse** or types `10.82.215.253` into Join.

**Troubleshooting:**

| Symptom | Cause |
|---|---|
| No ping replies | ZeroTier: authorization, or the device has no IP |
| Ping works, Join fails | Firewall: allow depth.exe on Private (and Public) |
| Browse empty, typed IP works | Broadcast is off in the network settings, or the subnet isn't /24 |
| "Different builds" | The friend has an old exe |

## B5. A code issue found (not fixed; the user hasn't approved changes)
`src/net_lan.cpp` `LanBeacon::Announce` sends the beacon to three places:
- `255.255.255.255`
- `127.0.0.1`
- each local IP's **/24** broadcast: it replaces the last octet with `.255`

**Two consequences:**
- Windows sends a `255.255.255.255` broadcast out of one interface only (usually the default route's), so it won't necessarily reach the ZeroTier adapter.
- A ZeroTier pool that isn't a /24 (such as 10.241.0.0/16) gets the wrong broadcast address.

The user's network is a /24, so Browse should work today. **The proper fix (about 30 lines):** enumerate adapters with `GetAdaptersAddresses` (iphlpapi), compute each IPv4 unicast address's real broadcast from `OnLinkPrefixLength`, and send to each. `LocalIPv4()` (gethostname + getaddrinfo) could move to the same API. Offer it; the user hasn't said yes.

## B6. Next networking steps
1. **The first real test:** two machines over ZeroTier finish a Scuttle match. This is networking step N1's own gate, and it's still open.
2. **Fix the subnet broadcast (B5).** Add a short "Playing over ZeroTier" help panel in `arcade.cpp`, whose Join text still says "until Depth is on Steam". The Steam plan was dropped, so rewrite it.
3. **Revise `docs/design/5_Depth_Arcade_Networking.md`.** It still makes Steam step N4; the user's plan is now ZeroTier → UPnP (miniupnpc) → rendezvous with GNS P2P custom signaling → relay only if needed. The Epic Online Services and itch.io alternatives are noted in memory.
4. **Networking step N3 leftovers:** the pause budget, the host's autosave and re-host, and tokens in the profile.
5. **Flats Duel over the network** (Part A, step N5), then the Trawl and Fathoms.
6. **Optional:** embed ZeroTier with its SDK (libzt) so friends don't install anything. Check ZeroTier's licence (Business Source License) before shipping it in a commercial game.

---

## Working notes for the next session
- **Git:** it isn't on PATH. Use `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.
- **Building:** `.\build.ps1`. The kit's script sets up the VS dev shell itself (it adds the VS Installer folder to PATH, because `Launch-VsDevShell` calls `vswhere` by name).
- **No Python** on this machine. `pdftotext` is at `C:\Program Files\Git\mingw64\bin\pdftotext.exe` and works for the Master Reference: `pdftotext -layout <pdf> out.txt`. The Read tool can't render PDFs here (no poppler).
- **Previews:** `LoadImageFromTexture` + `ImageFlipVertical` + `ExportImage` works in a hidden raylib window. raylib render targets **don't nest**: render cells first, then compose. The figure's hatching uses the scissor, so an outer scissor won't hold.
- **Other work in the main checkout:** it had uncommitted changes (CLAUDE.md, CMakeLists.txt, main.cpp, study.cpp, new course/expr/json files). That is the user's Study stage-17 work from another session, **not** this session's. Leave it alone.
