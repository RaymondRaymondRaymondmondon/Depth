# NOCLIP: build log

Arcade Game 10, on the Deep Arcade's **Action** reel. The user moved it there; the spec says the Slop shelf. The design is `Reference_For_Future_MP_Games/NOCLIP — Arcade Game 10 Design Document.pdf`, and its OCR is in `docs/noclip_pdf_pages/`.

## Decisions (defaults taken; the user can change any of these)
- **Engine.** Built inside Depth, with its **own renderer**. The spec says NOCLIP deliberately doesn't use Depth's ink, so it's plain raylib 3D into `Mode3DRT` with two shaders: a level shader (texture, per-cell fluorescent light grid, headlamp, point lights, fog, breathing walls) and a VHS pass (scanlines, chroma bleed, tracking tear, grain, vignette).
- **Quota.** 40% growth through week 5 (600, 840, 1180, 1650, 2300), then 35%. The spec's numbers disagree with each other; this choice matches its week-5 figure.
- **Voice mimicry.** The Skin-Stealer "speaks" as a subtitle with a teammate's name, `[Name's voice, from somewhere close]`, plus a sound. Recording players' real voices for mimicry isn't built, so this is the spec's lobby alternative ("synthesized speech") used as the default.
- **Pacing.** The day is 06:00-24:00 in 15 real minutes (1.2 in-game minutes per second). Overtime runs to 06:00. At the morning, everyone in a lit Lab is pulled out with its crate, and anyone outside dies.
- **Loot.** Loot tables are 3-8 items per level (the spec asks for 15-20), with the spec's values and sizes.
- **The field map.** One map shared by the whole crew (the spec shares it within proximity, or synced at a Lab).

## Code (namespace `nc`)
- `noclip.h`, `noclip.cpp`: the core at 30 Hz.
  - **The campaign:** weeks, quotas, days, the forecast, the Fence's daily rates, contracts.
  - **The crew:** meters (sanity's drains and restores per the spec's table), injuries (bleed, sprain, break), carrying (pockets, hands, huge items carried by two), tools, Lost autopilot, downed and revives, Wanderers.
  - **Labs:** breaker or crowbar to open, fuel to bring online, the crate, the portal's 30 s charge (pause and resume), opening, extraction of every networked crate, jumps, breaches.
  - **Moving:** transit between levels by doors and noclips.
  - **The Surface's commands:** sell to the Bureau or the Fence, the shop, suits, Lab upgrades, insertion.
- `noclip_gen.cpp`: the twenty levels from kits:
  - maze (Level 0, and Level 13 in glass);
  - halls (1);
  - corridor graph (2, 14, 16);
  - machine grid (3, 15);
  - office (4);
  - hotel (5);
  - one long corridor (6);
  - a house over water (7);
  - cellular caves (8);
  - streets (9, 11);
  - wheat (10);
  - rooms kit (12, 18);
  - carrier (17);
  - inn on a cliff (19).

  Each places its Labs (with breakers), exits, loot, spawns and lights, then joins every region. `CheckReachable` proves every Lab reaches every exit.
- `noclip_ents.cpp`: 22 entities in 17 behaviour classes: light, hound, faceling, duller, wretch, clump, mimic, party, moth, watch, spider, leviathan, seer, orderly, sentry, warden, friend, innkeeper.
  - Spawning follows each level's hourly budget: doubled after 18:00, tripled in Overtime, and higher on forecast levels.
  - Also handled here: breaches, and tools on entities (crowbar, shotgun, camera with dossier bounties).
- `noclip_bots.cpp`:
  - **Bot salvagers:** they sell, restock, loot by value, return full pockets to the crate, signal and extract late, and revive teammates.
  - **Commands:** `--noclip-test`, `--noclip-gen <level> <seed>`, `--noclip-sim <crew> <days> <runs>`.
- `noclip_net.*`: `NoclipHost` with per-viewer snapshots (the campaign, crew and Labs; loot within 60 m and entities within 70 m on the viewer's level). Guests generate levels from the day's seed (`World::mirror`).
- `noclip_render.*`:
  - procedural textures (wallpaper, carpet, ceiling tiles, concrete, pipes, brick, damask, rock, flesh, glass...);
  - per-level meshes;
  - the light grid, with flicker, lights-out, Overtime, Lab power and a hallucinated flicker;
  - the Lab fittings;
  - entity and crew figures;
  - loot;
  - the Surface's warehouse;
  - the camcorder stamp.
- `noclip_game.cpp`: `Scene::Noclip`. It has the Surface tabs (whiteboard, loading bay, shop, Lab upgrades, dossier, insertion), first person, the wrist HUD, Lab panels (portal desk, commissary, monitors), the field map (M), prompts, subtitles for tells, client-side hallucinations, proximity voice (`voice::SetHearing`) and tokens (`noclip_profile.txt`).
- `sound_noclip.inl`: a hum per level, beds, ambient events, the portal's rising hum, and 12 cues.

## Checks
- `depth.exe --noclip-test`: all pass. It covers:
  - the data and the generators (20 levels x 4 seeds, Labs to exits);
  - the quota curve;
  - a full day: insertion, walls, pickup, crate, portal, extraction, sale;
  - Lost into a noclip, and a Lab restart and jump;
  - entities: a Hound, the Leviathan, a Skin-Stealer;
  - an Overtime night;
  - a bot crew's day.
- `depth.exe --noclip-gen <level> <seed>`: ASCII and the reachability check.
- `depth.exe --noclip-net-test`: the byte-identical round-trip, and the mirror's levels.
- `depth.exe --net-loop noclip [lagMs] [mem]`: four players work a day to extraction and agree.
- `depth.exe --audio-test`: NOCLIP's six states and 12 cues.
- Shots: `noclip_lobby`, `noclip_lab`, `noclip_surface`, `noclip_pipes`, `noclip_lightsout`, `noclip_map`, `noclip_desk`, `noclip_hound`, `noclip_insane`, `noclip_suburbs`, `arcade_noclip`.

## The second pass (2026-10-05)
- **Modes** (`World::Init`, chosen on the arcade panel with `<` `>` and passed through `NoclipOpts(bots, mode)`):
  - the Bureau (the campaign);
  - **Lost**: you wake far from any Lab on Level 1-4, with one fuel canister, no tools and one life. Restart a Lab (its breaker, then the generator) and extract to win.
  - **Noclip Roulette**: every exit noclips to a random level from 0 to 9, and the quota is x0.6.
  - **Lights Out**: every level follows Level 6's rule.
  - **Expedition**: one 60-minute day with no commissary, scored on credit plus the bay.
  - **Lonely**: solo with no bots, and more hallucinations.
  - **Skin-Stealer** (3+ players): one player is secretly the thing. It can't be hurt and takes one teammate per level (R). The crew wins by extracting without it.
  - The Surface shows each mode's ending.
- **Mood rules:**
  - Level 1's crates respawn out of sight.
  - Level 5's doors want a key card, except one in three.
  - Level 13's mirror sends a Mirror Thing after 5 s.
  - Level 14's gurney heals.
  - Level 15's terminal hack stuns the Sentries for 120 s.
  - Level 18's Stay prompt: Y stays.
- **Contracts:**
  - Retrieval (the named item in the bay), Survey (80% of the level seen) and Specimen (living loot) are checked and paid.
  - The Bureau's memo is written from the day's log.
- **Lab upgrades:**
  - The Siren (from the desk) pulls entities to the Lab door.
  - The cargo link sends the crate to another Lab, on a 50 s cooldown.
  - Floodlights hold Smilers off.
- **Gear:**
  - Chalk arrows are drawn on the floor.
  - Rope and a grapple carry you down or across pits.
  - The scanner is a radar of heartbeats within 20 m. Skin-Stealers and Mirror Things have none.
- **The profile:**
  - Career credit from Bureau sales sets the rank (Intern and up).
  - Starting gear comes with rank: a flashlight, then a radio, then a scanner.
  - The Bureau locker (arcade panel): 40 cosmetics bought with tokens or from the crate.
  - What you wear is sent to the host (`C_COSMETIC`) and drawn: sticker colours, the party hat, masks, the lava lamp, the exit sign, moth wings, the balloon, the Hound and Almond Water costumes.
- **Bots:**
  - They restart dormant Labs (breaker first) and stay with whoever carries the fuel in Lost.
  - One bot in three scouts a deeper door early in the day and comes back the way it went (`botFrom`).
- **New checks in `--noclip-test`:**
  - Lost escape by bots;
  - Expedition's score;
  - Roulette;
  - the impostor;
  - the Siren.
- **New shot:** `noclip_locker`.

## The level rules and the last contracts (2026-10-05, later)
- **Seals and levers:**
  - `World::seals` are cells shut for now; `Move` and `Path` treat them as walls. A Level 2 hatch rusts open after 90 s if nobody turns a valve, and a Level 12 doorway reopens after a minute.
  - `World::levers` are Level 2's valves and Level 3's breakers.
- **Level 2:** standing in a dead end can slam its hatch behind you (35%, once per dead end a day). Any valve opens every hatch on the level.
- **Level 3:** a breaker toggles `power`.
  - On: live floors work and the machines halve footstep noise.
  - Off: the level goes dark (like Lights Out) and the elevator exit is dead.
- **Level 5:** doors have room numbers (`World::RoomNumber`, shown as the prompt). Rooms ending in 3 are traps: a cut and a Hound.
- **Level 12:** every 35-55 s a doorway near the crew closes for a minute. The house's Seers already sealed rooms, and Lab Mo was already off the network.
- **Level 17:**
  - The carrier rolls (`World::Roll`, which also tilts the camera). On a big heel, loose loot slides a cell, and falling cargo can hit anyone standing.
  - The lower decks flood through the day (`FloodRow`: up to 45% of the rows by evening), slowing you and draining stamina. The water is drawn.
- **Rescue:** a lost salvager (entity `survivor`) waits on the contract's level. E makes them follow you, and they follow you through doors. The contract pays if they're in the Lab when its portal takes the crew.
- **The Party:** accepting an invitation puts you in the Level 5 ballroom, held there. A teammate's E pulls you out, and the contract pays when you're extracted.
- **Bots:**
  - They turn a valve when a hatch is sealed.
  - On Rescue days they go to the survivor and wait for them.
  - They no longer stall at the Surface on a full tool belt.
- **Bug fixed:** when the last of the crew extracted, `EndDay` ran twice in one step and a day was skipped. A new check covers it.
- **The sim's report:** `--noclip-sim` reports time by level properly. `DEPTH_SIMLOG=1` prints each run and a periodic trace.
- **Sim result:** a bot crew of 3 meets week 1's quota in 3 of 3 runs.

## Still simpler than the doc
- Level 3's cages and arc flashes; Level 17's bulkheads and the flight deck's wind.
- Rooms in Level 12 don't regenerate (doorways only close and reopen).
- Bots never accept a Party invitation, and don't fetch a partying human.
- Bots don't plan multi-level routes beyond one scouting hop.

## Playtest fixes (2026-10-05, the user's notes)
- **The portal was cut off by the ceiling.** Labs are now tall rooms: the ceiling is at max(level ceiling, 4.2 m) and the Lab walls go up to it. Where a lower corridor or wall meets the Lab, a lintel face closes the gap (`labCeil` in `Gfx`). The ring (3.3 m) fits.
- **People were boxes.** They're now figures on the shared crew rig, generated by `tools/artgen/noclip_crew.py` into `assets/noclip/`:
  - `crew_hazmat.glb`: the Bureau's yellow hazmat suit (after the user's reference): hood, black full-face gas mask with lenses and a filter, black gloves, boots with orange soles, black tape patches, a zip.
  - `crew_orange.glb`: the orange flight suit with the black visor helmet, the harness and badges. It's the new crate-only costume `bureau_orange` ("The Orange").
- **How they're drawn:** `DrawSuit` (noclip_render.cpp) poses them with `fig::PoseFigure` and draws them through NOCLIP's own level shader. That shader now does GPU skinning (`uSkinned`, `boneMatrices`).
- **Recolouring:** materials are recoloured by name (suit, trim, glove, boot, sole, mask, lens, patch...).
  - The crew's cosmetics recolour the suit (suits), the tape (vests), the hood rim (stickers) and the lenses (lamps).
  - Toppers and costumes sit on the head bone.
- **The cast:** every human-shaped entity wears the same suit, recoloured (`SuitOf`):
  - a Faceling's mask is skin, so it has no face;
  - Partygoers are bright, with a white face;
  - Patients wear a pale gown;
  - Wardens, Orderlies, Innkeepers and the Friend are scaled;
  - Neighbors are all black;
  - Mirror Things glow;
  - Skin-Stealers wear a friend's suit, a little too tall, and walk a little wrong;
  - the Scarecrow hangs on its pole, arms out;
  - Hounds crawl on all fours, pale.
- **Still boxes:** the old box figures remain only as a fallback when the art is missing.
- **New shots:** `noclip_crew`, `noclip_cast`. `noclip_hound` is now a three-quarter view.
## The look, the third pass (2026-10-05, the playtest: "the VHS filter is a little sickening", "the trash you pick up is just blocks", "you should be able to see in your inventory what items you picked up", "the enemies are blockier than they should be")
- **The tape effect is gentler and the player's choice.** The VHS pass has a strength (`View::tape`): wobble, tear, colour fringe, smear, scanlines and grain all scale with it. F8 cycles off / light (the default) / full, saved in `noclip_profile.txt`. Fear still adds its noise.
- **Loot is real things:** 57 archetype models (`tools/artgen/noclip_props.py` -> `assets/noclip/props/`: wallet, VHS tape, keys, office chair, desk lamp, toolbox, jerrycan, gramophone, grandfather clock, diving helmet, ship's bell, piano, TV, cake...).
  - Each of the 117 loot names maps to one by its words (`PropArchOf`).
  - They're drawn through the level shader in their own colours (`DrawPropModel`).
  - Anomalous things hover and turn.
- **The pack:** two pocket slots and the hands, each with a lit picture of the thing (`ncr::LootIcon`, rendered once into a small target), its name and its value; G drops, T throws.
- **The Lab and the creatures** (`tools/artgen/noclip_world.py` -> `assets/noclip/world/`):
  - The Lab: the portal ring (it turns as it charges), the console with three CRTs and levers, the drop-off bin, a vending machine, bunks, the generator (it shakes when running), the monitor wall, the archive shelves, the blast door, the breaker.
  - The exits are doors in frames under a green EXIT sign.
  - The creatures: the Smiler's face (glowing eyes and teeth), the Clump (on the ceiling until it drops), the Deathmoth (a body and two flapping wings), the giant spider, the Sentry robot, the Seer's eye (it blinks), the Leviathan's shadow.
  - The old boxes remain only as a fallback.
- **New shot:** `noclip_beasts`. `noclip_lab` now shows the pack with three items and loot on the floor.
