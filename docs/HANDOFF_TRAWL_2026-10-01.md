# Handoff: the Trawl and Red Tide, 1 October 2026

Written for the next Claude session. It says what was done in this worktree, what is half-finished on disk right now,
what the user has decided, and what comes next. Read it with CLAUDE.md (sections "The Trawl", "The Trawl: finishing
the Lagoon", "Playtest round 3") and `docs/TRAWL_PROGRESS.md`.

## Where you are

- Worktree: `C:\Users\phill\Downloads\Depth\.claude\worktrees\depth-folder-game-review-8b23d7`, branch
  `claude/depth-folder-game-review-8b23d7`, base `master`. The playable exe is `build\Release\depth.exe` in this
  worktree (the main folder's exe is old until the branch is merged).
- Build with the PowerShell tool only: `.\build.ps1` (the Bash tool can't run it: execution policy). Git is
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`.
  No python on this machine. Several Trawl files are CRLF (`main.cpp`, `game.h`, `trawl_art.cpp`, `trawl_view3d.cpp`,
  `trawl_eco.cpp`, `trawl_fish.cpp`, `trawl_session.cpp`): perl/sed multi-line edits silently fail there, use the Edit tool.
- Standing orders from the user (memory `depth-working-prefs`, `depth-trawl-lagoon-plan`): work autonomously, commit
  often, ask design questions whenever they come up, the game may be closed at any time to rebuild.

## The reference material (read it, the repo's old text was wrong)

- The Trawl's real design doc is the 79-page PDF `C:\Users\phill\Downloads\Depth\The_Trawl_Reference\The Trawl — Arcade
  Game 2 Design Document (Draft).pdf`. `docs/The_Trawl_Design_Doc.txt` was decoded from an older 50-page draft and
  lacks half the game. The full OCR is `docs/The_Trawl_Design_Doc_v2_ocr.txt` (page PNGs in `docs/trawl_pdf_pages/`;
  OCR drops some table cells, so look at the PNGs for tables). Key sections and their OCR lines: the Killscore and the
  deck kill (~1040-1131), cooking (~1147), junk (~1302), birds and catch crates (~1312), the skiff (4198-4260),
  skiff destinations and landings (4247-4400), islands/fires/traders/treasure (4389-4570), economy (4570+), weapons
  catalogue and Gunsmith, Slipway and skiff upgrades (~5020-5135), stations (13, incl. the skiff davit, ~486).
- Red Tide's doc is the 90-page PDF in `Red_Tide_Reference`, OCR `docs/Red_Tide_Design_Doc_ocr.txt`; its seven
  workbooks are dumped to TSV in `docs/redtide_ref/` by `tools/xlsxdump.ps1` (no Excel needed); the tool zips are
  unpacked there too. Memory file `depth-reference-docs-v2` summarises both.

## What was done in this worktree (all committed, newest first)

1. `e43f27a` Playtest round 3: hook-set window 0.7 s (`Bite::Start`), cheaper weapons and a starting speargun, weapon
   animations in both Trawl views and in Red Tide, Red Tide's gun models rebuilt (`BuildGuns`), the net-yield grid
   (`DEPTH_NETYIELD`, `DEPTH_GLUT`).
2. `340772a`, `c026186` The shakedown night with Kess (`Session::shake`, `SHAKE_LINES`, `--trawl-shakedown-test`,
   the arcade's "Shakedown night (with Kess)" button); the solo sim skipper tows.
3. `87516cc` Finishing the Lagoon: `--trawl-sim` (`src/trawl_sim.cpp`, a scripted skipper with careful/greedy/reckless
   patterns), the lethal paths (reef shark hull rams in `EcoTick`, rail drag in `StepRods`, real chum), the Stir clock
   (`Eco::Stir()`, json `"stir"`), mid-night weather turns, nightly variants (Bait run, Red tide, King tide, Turtle
   nesting, Canoe night with the trade/tribute/refuse choice), Trawl sound (`sound_trawl.inl`, `TwAudio`, `TrawlCue`).
4. `302101e` Mouse look by warping, squall roll fix, the sonar station and helm chart.

Balance numbers at the time of writing (`--trawl-sim lagoon 3 <crew> careful 6`): six careful hands meet quota 83%
(about 226 a night, net yield 0.2), three hands 75%, two 100%, solo with the net 100%. Deaths with bots are 0.0 a
night (the doc wants about 0.3; logged as a deviation).

## What is UNCOMMITTED on disk right now (builds as of the last check, but the last edits are untested)

The playtest asked for two things: a landed fish that flops for the rail until you club or shoot it, and jumping (a
careless jump clears the rail, which is what the life ring is for). The work is wired but not yet built, tested or
committed. Files touched: `src/trawl.h`, `trawl_boat.cpp`, `trawl_gear.cpp`, `trawl_fish.cpp`, `trawl_bots.cpp`,
`trawl_net.cpp`, `trawl_art.cpp`, `trawl_view3d.cpp`, `trawl_eco.h/.cpp`, `trawl_session.cpp`.

- `CatchRec` gained `dead`, `flopT`, `deckAt` (and they're in the `Visit` snapshot in trawl_net.cpp). Every landing
  site now sets `deckAt` (rod: 1.5 m inboard of the rod; cod end: spilled over the sorting deck at x≈-8.6; gaff,
  longline, pot: inboard of the hand; tether: inboard of the rail; harpoon: the bow). Fish that were dead in the water
  (gaffed floaters, tethered kills, harpooned, heads) come aboard `dead`.
- `Gannet::StepDeckFish(dt)` (called from `StepGear`): a live, ungutted fish hops every 7 s + 0.5 s/kg (netted fish
  every 16 s), 0.3-0.8 m, 65% toward the nearer rail; past |y| > 2.8 it goes back over the side with a little blood.
  Nothing flops while moored.
- `Gannet::KillDeckFish(ci, reach)`: priest and knife call it on a press (`UseItem`), the gaff calls it when there is
  no floater to gaff; a bot at the gutting table clubs anything on deck within 12 m every 2.5 s (`trawl_bots.cpp`,
  uses `c.cool` as its timer). A bullet, pellet or spear passing a live deck fish within 0.45 m at deck height kills it
  and takes 10% off its grade (`StepGear` projectile loop).
- `HitShot`: a fish killed deeper than 1.5 m with nothing on it now sinks in blood (`MeanKg()*6`) instead of floating;
  wounds bleed `MeanKg()*2`. Spraying rounds at passing fish feeds the water, not the hold (the user's rule).
- Jump: `Crew::z/vz`, `Gannet::Jump` (Space off a station in `ApplyInput`; 4 m/s up on deck, 2.2 below), airborne
  physics in `Gannet::Move` with `Walkable(p, deck, air)` allowing the rail +1.6 m; landing off the deck calls
  `GoOverboard(ci, "jumped over the rail")`. Drawing: top-down lifts the figure by `c.z * ppm * 0.7` and leaves the
  shadow (`DrawCrewMember`); first person adds `c.z` to the eye (`Eye3D`) and the body frame (`DrawHand`).
- Deck fish are drawn in first person (after the floaters in `DrawTrawl3D`: on their sides, the live ones arch and
  slap). **Not yet drawn top-down**: add a loop over `g.hold` in `DrawGear` (trawl_art.cpp) using `FishMark`.
- Also in the WIP: `H_KELP`, `H_BARREN` habitats in trawl_eco.h (names "kelp", "barren") for the Weeds/Grotto later;
  Chandler ammo packs (flares 6/20, spears 10/12, rounds 30/15, shells 24/18) in trawl_session.cpp.

**To finish it:** add the top-down fish drawing; build; add gear-test checks (a fish on deck flops over the side in
time if nobody acts; the priest within 1.6 m kills it; a bot at the table clubs it; a jump off the rail puts the hand
in the water and the ring rescues them); run `--trawl-gear-test`, `--trawl-bot-test`, `--trawl-session-test`,
`--trawl-net-test`, `--trawl-eco-test`, `--trawl-shakedown-test`, `--trawl-sim lagoon 3 6 careful 3` (watch that the
flopping doesn't wreck the bots' quota numbers: the gutting-table bot should club everything); commit; log it in
`docs/TRAWL_PROGRESS.md` and CLAUDE.md.

## The user's decisions (1 October 2026)

- Cooking is ashore only (the doc's rule): every cooking fire is on a landing reached by skiff. The galley stove is for
  warmth and coffee, not cooking.
- The doc's quota rule stands: only fish delivered at the market count toward the quota, and the Owners reject
  anything under 70% freshness.
- Build the skiff plus the Atoll landing first ("and anything you feel necessary around it"), then carry on; the
  other two Lagoon landings (the Old Lighthouse rock, the Sandbar) and the Weeds come after.
- Weapons must pay for themselves and be fun with friends: shoot/club a landed fish to kill it, shoot gulls stealing
  the catch, fight off enemies; the market sells plenty of ammo so running out is hard.
- The "life raft" the user mentions is the existing life ring.
- Nightly variants, the canoe choice and the Lagoon's lethal paths are as built (memory `depth-trawl-lagoon-plan`).

## Progress after the handoff (second session, 1 October 2026)

- `6d6dfe6`: the WIP above is finished. Deck fish are drawn top-down, the gear tests were added, and the hook-set test was updated.
- `8daeae1`: **step 1, the economy spine.**
  - The Owners' quota scales (`DockKind::Scales`, `Session::Deliver`/`QuotaValue`) are apart from the Fish Market (`Sell(idx)`).
  - Only delivered fish count; under 70% fresh is rejected; no glut; credit past the quota carries at half (`carried`).
  - Per-fish Deliver/Sell, `CMD_DELIVER`.
  - The sim skipper paces deliveries, and hauls the net himself when short-handed.
- `7ff6195`: **step 2, the deck kill.** HP, `HitDeckFish`, the Killscore (to 4x, overkill), seven deck behaviours, deck blood, the popups (`DrawDeckFx`), bot self-defence, and the fix for fallen bots that never rose.
- Next: **step 3, weapons and the Gunsmith** (the doc's ~5020-5135), then steps 4-8 below.
  - The user said to go on through every step without waiting.
  - At 95% of the 5-hour limit: pause, update this section, and schedule a resume (memory `depth-working-prefs`).
- **Step 3 in progress: the plan (my design calls; the user is away):**
  - **The data is transcribed.** `data/trawl/weapons.tsv` has all 46 weapons from doc pages 29-31: id, name, class, damage, pellets, speed, magazine, noise, slots, where, price, ammo kind, ammo price, special. `data/trawl/attachments.tsv` has the 18 attachments from pages 32-33.
  - **The Gunsmith rules:** three damage upgrades per gun, +15% each, at 60/150/300 (doubled for the carbine, chatter gun and long rifle), and up to three attachments.
  - **Carrying:** the loaded magazine plus one spare reload on the body. Everything else lives in the magazine locker in the fo'c'sle.
  - **Wet powder:** cartridge guns misfire 10% in rain, 25% in a squall, and 40% in a storm or after their carrier was in the water. The Chandler's oilskin case (20) stops it.
  - **Slots:** two-slot weapons take two slots, and a hand carries at most one of them.
  - **Code, `trawl_weapons.h/.cpp`:**
    - `WeaponDef`/`AttachmentDef` loaded from the TSVs (`Weapons()`, `WeaponIndex`).
    - `Slot` gains `wpn` (a catalogue index), `dmgLvl`, `att[3]` and `spare`. The old `Item`s map onto catalogue rows.
    - A melee weapon calls `KillDeckFish` with its catalogue damage and reach.
    - A gun fires `Shot::Bullet` (or pellets when it has more than one), with its damage, fire rate and noise, scaled by upgrades and attachments.
    - Reload takes a spare from `Gannet::ammo[kind]`, the magazine stock. Until below decks (step 7), the deck locker stands in for the magazine locker.
  - **The Gunsmith:** a new quay station (`DockKind::Gunsmith`) with a tabbed panel (weapons / upgrades / attachments / ammo). Only the `where == gunsmith` rows are sold there; `gunsmith3` opens from deadline 3. The trader-only weapons come with the landings (the Atoll's coral club, shark-tooth blade, longbow and fletching).
  - **Wiring:** commands `CMD_GUN_BUY` / `CMD_GUN_UPGRADE` / `CMD_GUN_ATTACH` / `CMD_AMMO`; the slot fields and the ammo stock go into the snapshot's `Visit`; checks in `--trawl-gear-test`; shots `trawl_gunsmith`.
- **Step 3 is DONE** (see docs/TRAWL_PROGRESS.md "Step 3"). **Next: step 4, birds and junk** (doc v2 ~1302-1312: gulls/frigatebirds stealing unattended fish, the six catch crates, junk from the sea), then step 5, the skiff and the Atoll (both views).
- **Step 4 first part DONE** (crates and the birds' rule; see TRAWL_PROGRESS "Step 4, first part" for what is left: pelican/frigatebird, a shot bird drops its fish, the 10 s table rule, bots crating, junk). Then step 5 (the skiff and the Atoll, both views).
- **Step 4 spec** (doc v2, pages 26-28; read from the PNGs):
  - A dead fish is safe only in the **six lidded catch crates** on the aft deck, or in the hold.
  - Anything dead elsewhere is fair game for birds: on the deck; on the gutting table if it's left unattended 10 s; in the skiff; on a beach.
  - Birds are agents in the web's air layer. They're drawn by dead fish, smoke, chum, net hauls and gutting.
  - A bird grabs the heaviest fish it can lift and flies off. Shooting it down drops the bird and the fish where they fall.
  - Every bird kill is Airborne. Birds sell.
  - The Lagoon's birds:

    | Bird | Lifts | Value | Behaviour |
    |---|---|---|---|
    | Herring gull | 3 kg | 4 | Flocks up to 12; picks the deck clean |
    | Brown pelican | 5 kg | 12 | Scoops from the skiff and the net; swallows whole |
    | Frigatebird | 2 kg | 15 | Harries other birds until they drop their fish |

  - Other grounds have the cormorant, skua, cave swifts, storm petrel and albatross.
  - Bots put loose fish in the crates when idle.
  - **Junk** (any ground, 0 sh):
    - a message in a bottle (a treasure map to a spot on a landing);
    - a brass key (opens one named chest on the landings);
    - a torn chart piece (three reveal a hidden skiff mark or a buried cache).
  - Junk never fights; it takes room in the skiff and the hold.
- **Step 4 plan:**
  - `CatchRec::crated`: fish in a crate, safe and not on deck.
  - A crate station on the aft deck: E with a dead fish near you crates it, and bots crate when idle.
  - Change the gull steal in `EcoTick` (trawl_eco.cpp ~851: it takes any un-gutted fish under 3 kg every 4 s) to take only dead, uncrated, ungutted fish, the heaviest it can lift, by bird type. Add the pelican and the frigatebird to the Lagoon's species (json).
  - A shot bird drops its fish onto the deck (or as a floater) and is itself a 4/12/15-shilling catch.
  - Junk as `CatchRec` with `junk=true` (from net hauls and floating), kept for the landings.
- **Step 3 status (old notes; it is finished):**
  - **Built and wired:**
    - `trawl_weapons.h/.cpp`: loads the TSVs (via `TrawlDataPath()`) and has `AttachmentFits`, `UpgradePrice`, and `WeaponDamage`/`Magazine`/`Cooldown`/`Reach`/`Spread`/`Noise`.
    - `Item::Weapon` plus the `Slot` fields (`wpn`, `lvl`, `spare`, `att[3]`) and `SlotName`.
    - `UseItem`'s weapon branch: melee via `KillDeckFish(ci, reach, dmg, head)`; guns fire with catalogue damage; rapid guns fire while held; wet powder misfires; recoil.
    - `Reload` from the spare; `RestockAtLocker` (`Gannet::ammo*` stocks, in `StepGear` while a hand is at the Locker station).
    - `Session::GunBuy/GunUpgrade/GunAttach/AmmoBuy`.
    - `DockKind::Gunsmith` at the quay (-7.6, -8.2).
    - `CMD_GUN_BUY/UPGRADE/ATTACH/AMMO` and the `Visit` fields.
  - **Left to do:**
    - The Gunsmith **panel** in trawl.cpp `Panels()`, with tabs: guns for sale / the selected slot's upgrades and attachments / ammo packs.
    - The Gunsmith's **shed** in `DrawQuay` (trawl_art.cpp).
    - Anywhere the HUD shows `ItemOf(sl.it).name` should use `SlotName(sl)`.
    - A held catalogue gun or blade drawn in both views (map it by class onto the rifle, revolver or knife drawings).
    - **Tests** in `--trawl-gear-test`: buy a revolver, fire it at a deck fish, an upgrade raises damage 15%, attachment fit rules, reload from the spare, restock at the locker, wet-powder misfire, the one-long-weapon rule.
    - The shot `trawl_gunsmith`, a docs entry, and a commit.
- The user's answers this session:
  - The skiff and the Atoll in **both views**.
  - Order: WIP, then economy, then the skiff.
  - My stale sound WIP in the main folder was discarded.
  - The user is away: make the design calls and log them here.

## The plan from here (the Trawl)

The order agreed after reading the full doc. Step 1 was about to start when this handoff was written.

1. **Economy spine**: the three ways to use a fish (quota scales vs market vs barter), only delivered fish count,
   70% freshness rejection, Owners' consignments, the full price/freshness/grade pipeline per the doc's Economy
   section; coal per ground 10/25/40/80.
2. **Deck kill and the Killscore**: finish the WIP above, then the doc's deck behaviours per species (every fish of
   1 kg+ comes aboard alive with HP), the Killscore bonus (clean kill vs mess), blood on deck.
3. **Weapons and the Gunsmith**: the ~45-weapon catalogue (melee, sidearms, long guns, specials, thrown), damage
   upgrades and 18 attachments, the magazine locker below decks, more ammo at the Chandler.
4. **Birds and junk**: gulls and frigatebirds stealing unattended fish (10 s on deck, in the skiff, on a beach), the six
   catch crates, junk from the sea (chart pieces that reveal a hidden skiff mark, brass keys, bottles).
5. **The skiff and the Atoll** (the user's pick): the davit station (lower 8 s, recover 10 s alongside the stern with
   the Gannet stopped), rowing on the two mouse buttons (1.5 m/s one rower, 2.2 two; "catching a crab" stops it 1 s;
   each stroke writes noise), 150 kg, one section of 40 integrity, capsizes past 25 degrees, bow lantern 6 m, the
   sonar operator sees it as a bright blip and marks show as bearing arrows, walkies/flares/bell, left behind at the
   harbour line = lost for the night; predators prefer the smallest vessel with the most blood; the skiff-only marks
   (the Crest Pass, the Sargassum Line, twice the bite rate); the Atoll as a small top-down map (the deck's art and
   movement): fire pit (rain puts it out), the tribe's elder (trades only for fish at 150%, refuses crews who fought
   the canoes), 1-2 caches, crabs and a moray in its lagoon, smoke that draws birds and predators. Cooking happens
   here (the doc's cooking section).
6. **Mini-bosses, boss lures, harbour requests, charms.**
7. **Below decks** (fo'c'sle, chain locker, hold, bilge; intruders) and the 13 stations.
8. **The Weeds and the Grotto** (new grounds: one chart each, same quay), then Atlantis Waters; diving.

## Red Tide: the user's answers to the deviation list (1 October 2026)

Kept as built: the Ship confines divers to the hull (portholes); tides 1-3 "ecosystem calm"; tacticals and the brush
sold at the workbench; the Sand Worm scripted; Supper Call; quest keys team-held on the Ship (unanswered, so unchanged).
Two things to do:

- **Atlantis at 0.7 scale instead of 0.4** (`plan_override`/`poi_scale` in `data/redtide/maps/atlantis/extra.json`,
  the ring wall and "Beyond the Wall" radii follow the scale; re-run `--redtide-map-test atlantis` and
  `--web-check atlantis`, re-time the aqueduct check, and look at `--shots shots redtide_atlantis`).
- **Population stability must be fixed** ("somehow this has to be fixed"): `--eco-test ship` fails 3 seeds in 10 and
  `--eco-test cave` fails, because a singleton mid-predator (barracuda, grouper, octopus; the cave's snapping turtle and
  giant salamander eaten by the caiman) is eaten and its 240-600 s respawn falls outside the ten-minute window.
  Candidate fixes: a floor of one for singleton species (respawn fast when the last one dies), apex beasts not
  eating the last of a species, or the test counting a species as stable if it is scheduled to respawn.

Still not built in Red Tide (the user hasn't asked for these yet): four-player networking (stage 4), the modes,
pause/ping/scoreboard/lamp toggle, enemy weapon upgrades by tide, the Sawtooth, inspect animations, tonic jingles, the
wreck's listing, the Reef's wonder-weapon quest, the repair kit, the stage-10 balance pass.

## Tools and checks you'll use

- Trawl: `--trawl-gear-test`, `--trawl-bot-test`, `--trawl-session-test`, `--trawl-net-test`, `--trawl-eco-test`,
  `--trawl-shakedown-test`, `--trawl-fight all`, `--trawl-sim <ground> <nights> [crew] [careful|greedy|reckless] [runs] [green|able|oldhand]`
  (`DEPTH_TRACE=1` for a line a minute), `--shots shots trawl_` / `trawl3d_`, `--audio-test`.
- Red Tide: `--eco-test <map>`, `--redtide-map-test <map>`, `--web-check all`, `--redtide-sim`, `--shots shots redtide_<map>`.
- Known pre-existing failure: `--audio-test` reports "amb.organbreath is silent" on the main build too.

## Progress, third session (1 October 2026)
- Step 4 is DONE: `2a9dd03` (birds), `4d98fb2` (junk to the doc's table).
- Step 5:
  - 5a, the skiff core: `0c07bb9`.
  - 5b, the skiff drawn in both views: `dc2f3a4`.
  - 5c, the Atoll: `81a5953` headless, `53017a6` drawn. See docs/TRAWL_PROGRESS.md "Step 5".
- **Next is 5d:**
  - Skiff-only marks: the Crest Pass and the Sargassum Line give twice the bite rate.
  - Fishing from the skiff: a handline in the skiff; under 30 kg lands in her, bigger fish are killed alongside or tow-roped.
  - Predators prefer the smallest vessel with the most blood: rams on the skiff, which a shark capsizes.
  - The sonar shows the skiff as a bright blip.
  - Bots: F makes a bot follow you into the skiff and row on your beat.
- Then steps 6-8.
- 5d is DONE (`ae3e2d1`); step 5 is complete. The "Not built" list in TRAWL_PROGRESS covers what's left of it.
- **Next: step 6.** Mini-bosses, boss lures, harbour requests and charms (doc v2: the mini-boss section around OCR line 3404, harbour requests, and the charms table under Economy). Read the PNG pages for the tables.
- **Step 6 is DONE** (`91e2209`; TRAWL_PROGRESS "Step 6").
- **Step 7 plan (below decks; doc v2, pages 16-18; the user is away, so these are my calls):**
  - All 13 stations of the doc's table already exist; only the unmanned-davit rule is missing (a skiff alongside with nobody at the davit is hooked on from the water, 25 s).
  - **Deck 1 grows two spaces:**
    - The fish hold, under the main hatch on deck at about (-1.0, -1.0): x -2.9..0.8, |y| < 2.3. It joins the engine room through a watertight door at x = -2.9 (|y| < 0.6; E at it shuts or opens it).
    - The fo'c'sle, under the fore hatch at (7.6, 0): x 5.2..9.0, |y| < 1.9. It holds the bunks, the magazine locker (ammunition restocks move here from the deck locker; update the gear test) and the Medic's cot (lie there 10 s with the Medic within 2 m: one serious injury healed).
  - **Hatches:** open, shut (4 s to open from either side) or battened (10 s; can't be opened from below). E at an open hatch goes down or up; R at a hatch cycles open, shut, battened. While she rolls past 25 deg, every open hatch ships water into the bilge.
  - **Oil lamps below:** one per space, out past 20 deg of roll, relit in 3 s (E). The top-down below-deck view and first person are lit by them.
  - **Bilge eels:** with the bilge past 2000 kg, eels bite a hand below now and then. Fire already exists (`boat.fireT`); check how it spreads.
  - **The crew board:** the HUD shows who's below.
  - **Drawing:** the hold and the fo'c'sle in trawl_art (`viewerDeck == 1`) and in first person (BuildBoat's interior).
- **Step 7's logic is DONE** (`2f2273b`). **Next:** draw the hold, the fo'c'sle, the hatches, the door and the lamps in the top-down view (trawl_art, `viewerDeck == 1`) and in first person (BuildBoat's interior); add the crew board to the HUD crew list (trawl.cpp ~873: who is below, in the skiff, ashore). Then step 8 (the Weeds, the Grotto, Atlantis, diving).
- **Step 7 is DONE** (`2f2273b` logic, `87f4059` drawn).
- **Step 8 plan (the user's order: the Weeds and the Grotto first, one chart each from the same quay, then Atlantis Waters, then diving).**
  - Doc v2 OCR: grounds around lines 2236-2293; the species tables around 1197-1360 (read the PNGs, pages ~20-23); diving at 2743 and 3002; Atlantis at 3062.
  - **The Weeds:**
    - A kelp forest with golden canopy mats and clear lanes; everything big lives at the edges.
    - Catch: anchovy balls and kelp perch (bait); kelp bass, sheephead, rockfish, bonito; bluefin and yellowtail on the edge runs; halibut; abalone by diving.
    - Flora: kelp (shelters fish, blocks sonar, fouls lines); urchin barrens; sea otter rafts.
    - Signature: running the engine through the canopy wraps the screw (speed halves until a hand cuts it free from the stern ladder, in the water). Nets fill with worthless kelp, and hooked fish run into the canopy to chafe the line.
    - Threats: Sirens (pull the helm toward the rocks), Kelp Wraiths (entangle a hand at the rail), a Great White on the seaward edge, Feral Mermen who cut nets.
    - Mini-boss: the Kelp King (sea bass, 250 kg, 600, a kelp crown; boss lure 120) in the Inner Lanes. Gold Tail (yellowtail, 30 kg, 400) is listed under the Lagoon's row but its boss water is the Seaward Rocks.
    - Landings: Seal Rock (Old Hoskins pays double for birds) and the Cannery Pier.
  - **The Grotto:**
    - A sea-arch cave: black water, glowing mould, smugglers' wrecks.
    - Catch: cave fish, cave shrimp, glass eels, lantern fish, cave cod, giant isopods (pots), the Grotto sturgeon.
    - Signature: the arch closes between 02:30 and 04:00 (on the tape); a crew still inside is shut in until 05:00, which counts as late.
    - Echo: sound doubles, and loud noise drops stalactites.
    - Threats: Lantern Anglers, Ghost Worms, the Drowned, isopod swarms up the anchor chain.
  - **The plan:**
    - Pick the ground on the chalkboard or chart table (coal to reach: 10/25/40/80).
    - Per ground: an `Eco::BuildChart` branch, its species and ground block in trawl_species.json, its signature mechanics, its skiff marks, its landings and mini-bosses, then sound and art tints.
    - Check `--trawl-eco <ground> 27` stability and the sim per ground (the doc's targets: Lagoon 85%, Weeds 65%, Grotto 45%, Atlantis 25%).
- **Step 8 so far:**
  - `3a5b19b`: the Weeds' ground: twenty species (`data/trawl/trawl_species.json`), the chart (`Eco::BuildWeedsChart`: the same west shore and quay, a kelp forest with lanes, barrens, reefs, the seaward edge) and three skiff marks; stable with no crew.
  - Then: the chart table picks tonight's ground (`Session::SetGround`, CMD_GROUND; coal `CoalToReach` 10/25/40/80).
  - The sim on the Weeds already meets 100% (the doc wants 65%): nothing hard is in yet.
- **Next for the Weeds:**
  - Kelp fouls the screw: under way in H_KELP, a `screwFouled` flag halves her way until a hand in the water at the stern cuts it free (4 s).
  - Nets fill with kelp worth nothing.
  - Threats: a Great White on the seaward edge (a `ramsHull` species, ~25 damage); Sirens pulling the helm; Kelp Wraiths entangling a hand at the rail; Feral Mermen cutting nets.
  - The otter cascade: gunfire near otters scares them, and an otter killed costs an 80 fine.
  - Mini-bosses: `MiniBosses()` needs a ground column (the Kelp King, 250 kg, lure 120; Gold Tail, 30 kg). `BossCast` should pick by the mark's ground.
  - Landings: Seal Rock and the Cannery Pier.
  - Art and sound tints per ground: kelp drawn in both views (H_KELP cells).
  - Re-tune until the sim lands near 65%.
- Then the Grotto (the roster is in docs/trawl_rosters_weeds_grotto.md; the rest of its table is on p0045.png), Atlantis, and diving.
- **The Weeds, state at `3a7894a`:**
  - **Built:**
    - the species, the chart and three marks;
    - choosing the ground at the chart table;
    - kelp fouls the screw (a chance under way; a swimmer cuts it, `CutScrew`);
    - the Great White;
    - the mini-bosses on their marks (the Kelp King, Gold Tail; lures 120) and their charms (the kelp crown, the golden scale).
  - **The sim fixes:** coal for the ground, a wider range, an earlier turn home when far out, kelp avoided, a snagged net and a fouled screw cut free.
  - **Sim result (6 careful hands):** about 25-33% of deadlines met against the doc's 65%. Income is about 116 sh a night, and a broke crew can't buy the 25 kg of coal by the third night.
  - **Next:**
    - Tune the economy: fish density at the seaward edge, the net yield over the lanes, perhaps a lower quota scale per ground, or a cheaper run out.
    - Draw kelp (H_KELP cells: canopy mats in both views; drift kelp mats as rafts).
    - Kelp in nets (worthless weight).
    - Threats: Sirens, Wraiths (the kelp crown makes a hand immune), mermen.
    - The otter fine (80).
    - The landings (Seal Rock, the Cannery Pier).
  - Then the Grotto.
- `b323fff`: the Weeds' kelp is drawn (top-down mats and barrens; first person tints the sea grid's vertex colours in `UpdateSea`), and the tape names the ground.
- **Still open for the Weeds:**
  - Economy tuning toward 65%.
  - Kelp in nets.
  - Sirens, Wraiths, mermen.
  - The otter fine.
  - Seal Rock and the Cannery Pier.
  - Ground-specific nightly variants: the Lagoon's are rolled everywhere today (turtles and canoes on the Weeds).
- `32772ec`: kelp in nets (worthless weight); the otter fine.
- Then the sim skipper puts tow marks first when he has three or more hands. The Weeds sim is still 33% at 99 sh a night, with 3 nights seized.
  - **Diagnosis:** the towable marks lie ~250 m out over low-value forage (anchovy 0.6/kg). Income can't cover the 25 kg of coal by night three, so she runs out of steam and comes home late. (`DEPTH_TRACE=1` shows `p 0.00` in the last lines.)
  - **Levers:**
    - put the Weeds' valuable mid-tier fish (kelp bass, sheephead, rockfish, yellowtail) where a net can reach them: larger `start` counts, habitat weight on `open`/`sea`;
    - give the Weeds its own net yield, or let a lane tow through thin kelp;
    - check the coal burn at the Weeds' longer runs.
