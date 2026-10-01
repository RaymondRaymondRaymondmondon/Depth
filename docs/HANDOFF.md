# Handoff: the Deep Arcade games (Red Tide and The Trawl)

This file is for the next Claude Code session, working in the user's local folder
`C:\Users\phill\Downloads\Depth`. That folder holds the **LAN and online multiplayer work** (the shared arcade networking
layer, and Red Tide's stages 4 and 10). The work described here was done in a cloud session on the GitHub branch
`claude/sharp-johnson-wjq303` of `raymondraymondraymondmondon/depth`. **Merge this branch into the local folder,
then carry on there.** Read `CLAUDE.md` first: it holds the project's rules and art direction, and has a section for each
game.

## 0. First steps in the local folder

1. Get this branch into the local folder (commands are at the end of this file). The local folder's networking code and
   Red Tide stage 4/10 work **must not be overwritten**. If both sides changed the same files (likely: `CMakeLists.txt`,
   `src/main.cpp`, `src/game.h`, `src/menu.cpp`, `src/salon.cpp`, `src/redtide_*`, `docs/REDTIDE_PROGRESS.md`,
   `CLAUDE.md`), merge them by hand and keep both sides.
2. Build with `.\build.ps1` and run every check listed in section 4.
3. `main.cpp` and `game.h` use CRLF line endings. Edit them with the Edit tool; PowerShell `.Replace` with LF strings
   silently misses.

## 1. What this branch contains (newest first)

| Commit | What |
|---|---|
| (this commit) | **The Trawl stage 6, in progress** (shooting, the net, set gear, death and ghosts), plus this handoff and the Trawl design doc text |
| 1448940 | The Trawl stage 4: the session loop |
| 6b19ff7, dec0a49 | The Trawl stage 3: the Eclipse Lagoon's food web |
| 31b3b96 | The Trawl stage 2: lines and fishing |
| 55637e5 | The Trawl stage 1: the boat alone |
| c49c454 | Red Tide: salvage builds, the chum bag, the flare, the cleaning brush |
| 22bf8f1 | Red Tide: zones built from many boxes; Atlantis's ring wall |
| 447173e | Red Tide: the ink bomb; keys dropped where a carrier died |
| c75a2ed ... 78bda45 | Red Tide stage 9 (a-d): arcade profile, Salt Charms and skins, hidden quests, sound and quips |
| 216bc83, 4fa185e, ebb273d, ... | Red Tide stages 5-8 (Cave, Coral Reef, Atlantis, Approaching the Void) |

Red Tide details: `docs/REDTIDE_PROGRESS.md` and the Red Tide section of `CLAUDE.md`. On this branch Red Tide has stages 1-3
and 5-9 plus the extras above. **Stages 4 (networking) and 10 were built separately in the local folder**, so that copy
is the one to keep for those. Red Tide's design is in `Red_Tide_Reference/`.

## 2. The Trawl: state of play

Design doc: `docs/The_Trawl_Design_Doc.txt` (the text of "The Trawl - Arcade Game 2 Design Document (Draft)", 50
pages; the PDF may also be in the local folder). Build log: `docs/TRAWL_PROGRESS.md` (a section per stage, with the
design calls made). The build order is the doc's own (page 48):

| Stage | Work | State |
|---|---|---|
| 1 | The boat alone: hull physics, engine, stations, top-down deck, movement | **Done** (`--trawl-boat-test`) |
| 2 | Lines and fishing: Verlet lines, cast, bite, fight, landing | **Done** (`--trawl-fight all`: the doc's five target fights in range) |
| 3 | Ecosystem: water column, light, vibration, Wake, the Lagoon's species | **Done** (`--trawl-eco lagoon 27`, `--trawl-eco-test`) |
| 4 | Session loop: dock, Chandler, sail, clock, sell, quota | **Done** (`--trawl-session-test`) |
| 5 | Networking: six players, voice, prediction for reeling. Gate: `--net-loop trawl` and a six-player LAN night | **Done in the local folder** (`--net-loop trawl`, `--trawl-net-test`; see docs/TRAWL_PROGRESS.md "Stage 5"). Left: reeling prediction, voice, a six-player LAN night |
| 6 | Shooting, the net, set gear, death and ghosts | **Mostly built, not finished** (see 2.2) |
| 7 | The Weeds and the Grotto with their threats | Not started |
| 8 | Diving and wrecks | Not started |
| 9 | Atlantis Waters, the Kraken, the Ghost Ship | Not started |
| 10 | Tuning pass and sound | Not started |

### 2.1 Code map (all under `src/`, namespace `tw`)
- `trawl.h`: the headless core: `TrawlData` numbers (`D()`), `Sea`, `Boat`, tackle, lines, hooks, `FishSpec`, `Fight`,
  `Bite`, bot angler, `Station`s, `Crew` (with slots, injuries and life), `Rod`, `CatchRec`, projectiles, floaters, `Trawl`
  (the net), `Longline`, `Pot`, `LifeRing`, and `Gannet` (the boat plus crew as one 60 Hz step).
- `trawl_data.cpp`: every number (tackle, lines, patterns, bot skills, stations, dummy fish).
- `trawl_boat.cpp`: hull physics, the engine, leaks, the deck's walkable area, the quay, the crew's movement and
  stations, gutting, `--trawl-boat-test`.
- `trawl_fish.cpp`: the fight model, the bite, `BotFight`, rods on the Gannet (`RodInput`/`StepRods`), bait,
  depredation, `--trawl-fight`.
- `trawl_eco.h/.cpp`: the food web: species loader (`data/trawl/trawl_species.json`), the population model calibrated
  to its start, the Lagoon chart and tide, the blood, sound and vibration fields, agents near the boat, light, Wake,
  `TryBite`, `Depredate`, `EcoTick` (what the Gannet puts into the water), and stage 6's hooks (`HitAgent`,
  `DamageAgent`, `Sweep`, `DensityAt`, 12 m `DepthCharge`). Also `--trawl-eco` and `--trawl-eco-test`.
- `trawl_session.h/.cpp`: the run: deadlines, quota, night clock, harbour line, customs, the Owners' tape, the market,
  the Chandler, the Slipway, total loss, revival and fines at the dock, `--trawl-session-test`.
- `trawl_gear.cpp` (stage 6): items and slots, firing and ballistics, the harpoon cannon, the net, set gear, the life
  ring, overboard, injuries, death, `--trawl-gear-test`.
- `trawl_art.h/.cpp`: top-down pixel art in the boat's frame: sea (with the Lagoon's seabed), boat, crew (ghosts,
  injuries), lines and fish, the web's life, the quay, and stage 6's gear in the water.
- `trawl.cpp`: the scene: input, overlays per station, dock panels, HUD, shots (`DebugTrawlShot`).
- Entry: the salon's Deep Arcade reel ("Sail (solo)") calls `StartTrawl`, which starts a solo run at the quay.

### 2.2 Stage 6: what's done and what's left
Done and tested (`depth.exe --trawl-gear-test`, 43 checks, all passing):
- **Four slots a hand plus a deck locker** (new station `StationKind::Locker`). Starting kit: two gaffs, a fish priest,
  the life ring.
- **Weapons** (`Item`, `ItemOf`): speargun (tethered), flare pistol (a burning light the web reacts to), rifle, shotgun,
  depth charge (thrown, or rolled off the stern), bandages, knife, longline, crab pot. The Chandler sells them all, with
  ammunition.
- **Ballistics**: gravity and drag in the air; a round loses 90% on entering the water and stops within 1.5 m, while
  spears and harpoons carry on. Projectiles move in sub-steps of at most 0.25 m, so fast rounds can't skip through a
  target. Aiming at something underwater means aiming short of it.
- **Hits**: shot fish float as `Floater`s, graded rifle 70%, spear 85%, harpoon 75%, explosive 30%, depth charge 50%.
  Gaff them from the rail with E; a tether reels them in by itself. Gulls can be shot. Friendly fire is on.
- **Harpoon cannon** (Slipway, 700): a hit on something big becomes a tethered fight on 120 kg steel that tows the
  boat. Hold left mouse to winch; right mouse releases. Explosive heads cost 80 each.
- **The net** (`Trawl`): 15 s to shoot. It fills from the schools it sweeps (`Eco::Sweep`), drags and trims the boat by
  the stern, and hauls at half speed without a second hand. The cod end dumps onto the sorting deck at grade 90%. Jellies
  in the net burn whoever empties it. It snags on the reef or crest: back her off astern for 4 s, or cut it loose with a
  knife and lose it (the Slipway sells a new net, and a bigger trawl).
- **Set gear**: longlines and pots fish the population wherever they lie (not the agents near the boat). Longlines get
  robbed (a barracuda leaves the head). Haul with E at a buoy or float.
- **Overboard**: 25 s before drowning (12 s in a squall). The screw kills a swimmer near the stern; with the screw
  stopped, a swimmer can climb the stern ladder. The life ring is thrown on its rope and hauled back in. Sharks come
  for swimmers.
- **Injuries**: hooked hand (can't reel), broken arm (half rate; falls on the wet deck), burn (slower), bite (bleeds
  into the water until bandaged). A second serious injury in a night is death.
- **Death**: a dead hand is a ghost (touches only the bell; threats within 10 m show as pale outlines for a moment after
  it moves). A body lost to the sea costs 8% of money at the dock.
- **Total loss** (every hand dead, or the boat sunk): the catch is lost, the Owners charge 25%, and the next night starts
  at the dock.
- **Fines**: 30 for a depth charge in the Lagoon; 50 for a protected turtle kept aboard past 60 s. Bycatch goes back
  over the side at the gutting table before gutting starts.
- **Scene**: inventory HUD, 1-4 to pick a slot, left mouse uses the slot off-station (right mouse sights), R reloads,
  E gaffs, hauls or takes a station. The net winch, harpoon and locker have overlays. Swimming has a breathing vignette.
  Ghost view. `DrawGear` draws the net mesh, warps, buoys, floaters, shots, flares, rings and swimmers.

Left to do for stage 6:
1. **Screenshots**: `DebugTrawlShot` cases 15-20 (net filling, rifle and floater with gulls, overboard with the ring, ghost,
   harpoon fight, locker) were drafted but **not applied**. Write them and add `trawl_net`, `trawl_rifle`,
   `trawl_overboard`, `trawl_ghost`, `trawl_harpoon`, `trawl_locker` to `TakeShots` in `main.cpp`. Then look at the PNGs
   and fix what reads badly.
2. **Docs**: a stage 6 section in `docs/TRAWL_PROGRESS.md` (mark stage 6 done in the table), and a stage 6 line in
   `CLAUDE.md`'s Trawl section.
3. **Not built yet, from the doc**:
   - The fish priest's effect (landed fish flopping and sliding off a rolling deck).
   - Carrying big fish in both hands.
   - The hand lamp (F).
   - Throwing items short distances.
   - A second hand sent in on a line to rescue a swimmer.
   - The Medic's CPR role perk.
   - Ghosts reading the sonar (the sonar station itself isn't built: see 3).
   - Ghost-only voice.
   - The Ghost Ship stealing set gear (stage 9).
4. Balance: nothing in stage 6 is tuned (damage, HP, net catch rate, set-gear rates, the depth charge's haul).

### 2.3 Known issues and decisions to revisit
- **Solo quota**: a bot at one light rod sells about 68 of the solo 200 over a deadline. The doc counts bots as crew (a
  solo player with five bot hands faces 400), so the fix is **bot crew** (doc section "Bots") plus tuning. Until then a
  solo run is always repossessed. The net and set gear (stage 6) raise income a lot; re-measure with them.
- **The Trawl's food web is its own code**, not an adapter onto Red Tide's `rt::Ecosystem`. That engine is built around
  zones, doors, factions and tides, and wrapping it would have been more code than writing the doc's rules directly.
  Only the JSON reader is shared.
- **Blood**: at the doc's 3% a second a slick only trails about 7 m, so smell works over a radius (the reef shark smells
  out to 40 m).
- **Starting money** is 60 shillings, and the net grade is 90%; the doc leaves both open. The quota for 3 and 5 hands is
  interpolated. The harbour line is a 70 m ring round the atoll's quay.
- **Stand-in fish**: `DummyFish()` is used by `--trawl-fight` and by a Gannet with no ground. Its sturgeon, yellowfin and
  marlin carry `pullK`, `staminaK` and `softMouth` values tuned to hit the doc's fight times.
- **Not built**:
  - The sonar station (passive and active pings, the depth dial).
  - The Stir clock (a night's threat curve).
  - The shakedown night (Kess).
  - Role upgrades.
  - The Catch Log and trophies across runs.
  - Arcade tokens paid into the arcade profile.
  - Island war canoes, and the Lagoon's "king tide", "turtle nesting" and "canoe night" events.
  - The kedge anchor for getting off after grounding (right now she just stops).
  - The Owners' salvage office (salvage arrives with stage 8).
  - The crew board (Tab).
  - The ping wheel and the speaking tube.

## 3. What to do next, in order
1. **Merge** this branch into the local folder, build, and run all checks (section 4).
2. Finish **Trawl stage 6** (section 2.2, items 1-2).
3. **Trawl stage 5, networking**, on the arcade networking layer that's in the local folder:
   - Host-authoritative, 6 players, 20 Hz snapshots filtered by what each client can know (fish only in the light, on a
     sonar ping, or while hooked).
   - The rod holder's tension gauge predicted locally from the chain physics.
   - Proximity voice.
   - `--net-loop trawl` (a host and five bot clients over loopback).

   The simulation is already headless and stepped on the host: `Gannet::Step`, `Session::Step`, `EcoTick`. Inputs go
   through `Gannet::Move`, `RodInput`, `UseItem`, `NetInput`, `HarpoonInput`, `Primary`, `Scroll` and `TakeStation`, so
   they can be sent as messages.
4. **Bots** (doc section "Bots"): bot crew with roles and skills (`SkillOf` already holds reaction, hook-set, gaff, bow
   and keel). They're needed for solo balance and for `--trawl-sim`.
5. The **sonar station** and the **Stir clock**; then **stage 7** (the Weeds and the Grotto: their species go into
   `trawl_species.json` as new `grounds`, each with a chart builder beside `BuildChart` in `trawl_eco.cpp` and its
   signature rule), stage 8 (diving and wrecks), stage 9 (Atlantis Waters, the Kraken, the Ghost Ship), and stage 10
   (tuning and sound: the doc's balance targets are on page 47-48).
6. Red Tide: anything left in `docs/REDTIDE_PROGRESS.md`, reconciled with the local folder's stage 4/10 work.

## 4. Checks (run after merging, and after any change)
```
depth.exe --trawl-boat-test      (stage 1 + 2's rod checks)
depth.exe --trawl-fight all      (must print "All target fights in range")
depth.exe --trawl-eco lagoon 27  (must print "The ground is stable with no crew")
depth.exe --trawl-eco-test
depth.exe --trawl-session-test
depth.exe --trawl-gear-test
depth.exe --shots shots trawl_   (then look at shots\trawl_*.png)
depth.exe --redtide-test, --redtide-match-test, --redtide-map-test cave|reef|atlantis|void, --web-check all, --eco-test ship
depth.exe --audio-test           (slow; covers Red Tide's sound)
```
All of these passed at this commit on Linux (built with `cmake --build build_rel`).

## 5. Getting this branch into `C:\Users\phill\Downloads\Depth`
Git isn't on PATH on that PC; use Visual Studio's copy:
`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"`

If that folder is a git clone of the same GitHub repo:
```
cd C:\Users\phill\Downloads\Depth
git status                      (commit or stash the local LAN/online work first)
git fetch origin claude/sharp-johnson-wjq303
git merge origin/claude/sharp-johnson-wjq303
```
Resolve conflicts by keeping both sides (the local networking and Red Tide 4/10 work, and this branch's Red Tide extras
and the Trawl).

If it is not a git clone: clone this branch next to it
(`git clone -b claude/sharp-johnson-wjq303 https://github.com/raymondraymondraymondmondon/depth Depth-cloud`) and
merge the two folders: copy in the files that are only in `Depth-cloud` (`src/trawl*`, `data/trawl/`, `docs/*TRAWL*`,
`docs/HANDOFF.md`, `docs/The_Trawl_Design_Doc.txt`, and the Red Tide files listed in section 1). Merge shared files by
hand.
