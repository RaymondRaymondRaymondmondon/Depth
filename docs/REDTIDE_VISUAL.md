# Red Tide: the Visual Overhaul (build notes)

The spec: `Red_Tide_Reference/Red Tide — Visual Overhaul Spec.pdf` (25 pages, 10 phases). Its text, page by page, is in
`docs/redtide_vis_pages/` (OCR from `tools\pdfpages.ps1`; look at the PNGs where the OCR drops a table).

## The target machine (spec, "Performance": recorded in phase 1)
- **CPU:** 12th Gen Intel Core i5-12600HX (12 cores, 16 threads)
- **GPU:** Intel UHD Graphics (integrated; 2 GB shared, driver 32.0.101.7080)
- **RAM:** 15.7 GB
- **Screen:** 1920 x 1200
- The spec asks for 60 fps at 1080p with four players and 300 agents. **The user's call (2026-10-02): 60 fps with the 3D
  view at 720p internal, scaled up** (the menu's Graphics page already has the resolution scale, shared with the Trawl).

## The user's decisions (2026-10-02, over the spec's defaults)
- Asset sources are **Blender Python scripts plus the exported .glb**, no .blend files (`tools/artgen/`, the folder
  the Trawl's overhaul already uses; the spec's `tools/blender/` would split the shared kit in two). Run headless with
  `.\tools\artgen\run.ps1 [name]`. Everything is generated: no downloaded textures or models.
- **No approval gates**: phase after phase, renders shown afterwards.
- **The divers use the Trawl crew's stylised look** (smooth chunky figures, simple faces), on the same skeleton, in
  period diving gear; the spec's realism goes into the gear and the guns.
- Red Tide's existing Locker skins and Salt Charms stay as they are; the token skins come last (a separate task, with
  the Trawl's).
- The repo is public: keep assets small.

## Shared with the Trawl (spec: "build these once in shared code")
- The renderer (`redtide_render.*`: `LoadAsset`, `DrawPbr`, `DrawPbrSkinned`, `DrawPbrParts`, the Quality settings, AO,
  fog banks), the generators' common kit (`tools/artgen/common.py`, `tiles.py`), the gun-part kit (`gunkit.py`).
- **The humanoid figure** (`figure3d.h/.cpp`, phase 1): the skeleton from `tools/artgen/crew.py`, posed in code
  (`fig::PoseFigure`: walk, swim with a flutter kick, tread, reach, grip, breathe, blink, shout, two-bone arm IK;
  `fig::FistWorld`). The Trawl's sailors and Red Tide's divers both use it.

## Phase 1: the pipeline (done 2026-10-02)
- `tools/artgen/rt_divers.py` builds the four divers on crew.py's rig and body (crew.py is imported as a library):
  - **the Diver**: a round brass helmet with three grilled ports, a bolted corselet, an air hose from the back, a
    weighted belt with lead blocks, a sheath knife, lead-soled boots with brass toe caps;
  - **the Whaler**: a hood drawn over a smaller face-plate helmet, a long slicker with toggles, a rope belt and a
    harpoon-line coil, tarred gloves;
  - **the Stowaway**: a dented helmet with one odd copper port, a suit patched in another canvas, a bottle on a cord
    at the chest, a sack on the back, odd boots tied with string;
  - **the Mechanic**: a riveted hard suit (cuirass, plated limbs, brass joint rings), a domed helmet with a grid of six
    small ports, two gauges and a valve on the chest, a tool roll.
  Helmets ride the chest bone (a standard dress helmet is bolted to its corselet; the head turns inside it). The face
  is the crew's face, seen through open ports (an opaque glass pane hid it; glass comes with the rendering phase).
  Named flat materials (skin, top, trousers, boots, hat, accent, leather, metal, glass) so a skin is a recolour.
- `redtide_vis.h/.cpp`: `DiverModel(voice)`, `DrawDiverFigure` (with each diver's temperament from the quips: the
  Whaler's forward lean, the Stowaway's sway, the Mechanic's tight hands) and the studio (`DrawRedTideStudio`).
- In the game, teammates and bot divers are drawn as their diver (`DrawTeammate` in redtide_game.cpp): treading water
  when still, lying into the flutter kick when swimming, on their back when downed, pale when dead.
- **Harness:** `depth.exe --shots shots/vis rvis_` (studio sets are `DebugRedTideShot(g, 200 + n)`):
  `rvis_3_divers` (front, side, back), `rvis_3_faces`, `rvis_3_swim`, `rvis_3_crew_salon` (three divers in the Sunken
  Ship's salon, in play).
- The baked guns (`assets/shared/weapons`, the Trawl's catalogue) and the skinned fish (`assets/trawl/fish`) already
  load through the same renderer; Red Tide's own guns and creatures come in phases 4 and 5.
- **Frame cost now** (1280 x 720 window, uncapped, `DEPTH_UNCAPPED=1 DEPTH_SHOTFRAMES=300`): the salon 8.2 ms, the
  engine room 8.2, the reef wall 7.3, a Wrecker hunt 8.5; three divers in the salon 9.2 (each skinned diver about
  0.35 ms, skinned on the CPU).

## Phase 2: underwater rendering (2026-10-02)
- **The water** (`SceneLight::water` and friends in redtide_render.h; the Trawl leaves them off, so its look is unchanged):
  - per-channel absorption in place of the grey fog (`absorb`, scaled with the zone's fog density, so the maps'
    palettes still tune it; a palette may give `absorb` and `depth_dark`): red goes first, then green;
  - the in-scattered water colour darkens with depth below the surface (`depthDark`);
  - soft animated caustics on up-facing surfaces near the surface, in both the inked and the PBR paths (`causticK`:
    0.9 in open water, 0.35 inside).
  The shared snippet is `sceneFog()` / `caust()` in `RT_FOGBANK`.
- **Light shafts** (`SceneLight::AddShaft`): each is a volume (a top, a direction, a radius widening as it falls, a
  length); the light it scatters toward the eye is integrated analytically from the view ray's closest pass to its
  axis, up to the surface the ray ends on. Red Tide places them through the room's four nearest portholes (in and
  down), and in open water on a fixed 9 m world grid from the surface (they stand still as you swim through).
  They are computed with the bloom in one quarter-resolution pass (`RT_BLOOM_HEAD + RT_SHAFTS + RT_BLOOM_MAIN`); at
  full resolution they cost the open reef 8 ms.
- **Bloom** off bright sources (threshold 0.78), **the helmet port's lens** (a gentle barrel, a chromatic fringe toward
  the rim, a darker vignette), **the ink line** tinted by the water and fading into it with distance (`inkFade`), the
  stipple off, a softer grain, SSAO on, a light filmic curve.
- **Particles:** marine snow as soft specks fading with distance, blood as soft dark-red clouds (near black in the
  deep, never orange or pink), and bubbles (`FxBubbles` / `FxStep` / `FxDrawBubbles` in redtide_vis.cpp: rising
  faster when bigger, wobbling, swelling as they rise, popping at the surface) from every diver's helmet exhaust on
  each breath (quicker when swimming hard, winded or hurt).
- **Settings** (Graphics page, saved as the `redtide_look` line): the ink line Off / Thin (default) / Full, the
  stipple (off by default), the helmet lens (the motion-comfort switch).
- **Frame cost** (1280 x 720, uncapped): the salon 10.4 ms, the bridge 10.4, the reef wall 9.7, a Wrecker hunt 11.2,
  three divers in the salon 12.0. All inside the 16.7 ms budget at 720p.
- **Not done yet from the spec's rendering section** (later phases): the air pocket's mirrored rippling surface from
  below; silt clouds where divers and beasts touch the bottom; ichor and oil ribbons (the Lost Ones, the Sentinels);
  bubbles from guns, slipstreams and vents; the brass helmet HUD as a model (with the divers' first person, phase 3);
  the brightness and fog calibration screen and the colourblind setting for blood, ichor and scent.

## Phase 3: the divers (2026-10-02)
- **Token skins as material sets** (`DiverSkinColours`): the Locker's suits recolour the canvas (top, trousers) and its
  helmets the helmet and its trim (hat, accent): Verdigris (green patina), Red Tide (deep red canvas, rust-red brass),
  Bone (bleached ivory), Pearl (nacre), Atlantean (bronze and gold). The silhouette never changes. The local diver
  wears the profile's suit and helmet in first person; `rvis_3_skins` shows every diver in every set.
- **A renderer fix** that came with it: a PBR draw's recolour and tint were written into the model's shared material
  and never undone, so they leaked into the next figure drawing the same model (and a tint compounded frame on
  frame). `DrawPbrCmd` now restores the colour after each mesh. (The Trawl's sailors recolour every material they
  use, which had hidden it.)
- **First-person arms** (`diver_<name>_fp.glb`: the diver without the helmet and chest gear, which would fill the
  view; `DrawFirstPersonArms`): drawn at viewmodel scale (0.7) with the shoulders just under the eye, the right fist
  on the gun's grip and the left on its fore-end (cupped under a pistol's grip, on the gatling's crank, on the
  magazine while reloading) by two-bone IK; each diver's gloves (rubber, tarred, mismatched mitts, steel). The
  first-person gun moved out to 0.4 m so the hands are in view.
- **First-person hands, second pass** (after the user's playtest, 2026-10-02: the arms were out of proportion, at odd
  angles, and the hands looked awful). A real viewmodel replaces the headless body:
  - `tools/artgen/rt_fphands.py` -> `fp_<diver>.glb`: a right hand modelled closed round a pistol grip (index on the
    trigger, thumb over the frame), a left hand cupping a fore-end, a mirrored left grip hand, and per arm a gauntlet
    cuff (brass ring) and a sleeve, each rigid on its own bone.
  - `DrawViewmodelHands` puts them straight on the baked gun's `grip_r` / `grip_l` markers in the gun's own frame (so they
    kick and roll with it), slanted by the kind of grip (pistol grips 72-78 degrees, a stock's wrist 58, a haft 0), and
    runs each forearm from its wrist out of the bottom corner.
  - Sidearms are held in one hand; two-handed guns cup the fore-end (it drops toward the magazine in a reload); the Twin
    Gannets one in each fist; melee in the right. The guns sit further out to the lower right, turned a little in
    toward the crosshair.
  - Gloves: dark rubber (the Diver), tar (the Whaler), the Stowaway's mitts, the Mechanic's steel.
  - Shots `rvis_3_hands` (`DEPTH_RTGUN=<id>`) and `rvis_3_handstudio` (from outside; `DEPTH_VMDBG=1` paints glove,
    sleeve and trim red, green and blue). The old IK arms remain only for a gun with no model.
- **The helmet's port** (`DrawHelmetPort`): the brass rim with rivets round the view, the dark copper of the helmet in
  the corners, two faint reflections on the glass, and droplets running down it in an air pocket. Off with the Helmet
  lens setting.
- **Not done yet:** third-person weapon holds for teammates (phase 4 gives them their guns), downs and revives,
  states (netted, grabbed, swallowed, parasite, stunned, poisoned breath), the head turning to a teammate who speaks,
  the HUD's gauges as real dials, cracks on the glass when badly hurt.

## Phase 4: the guns (first part, 2026-10-02)
- `tools/artgen/weapons_rt.py` (on the shared gun kit, `gunkit.py`) builds Red Tide's own weapons into
  `assets/redtide/weapons/<id>.glb`, every one a machine for firing under water: gas bulbs with valves and seals,
  pressure gauges whose needle drops as the gas is used, bubble-vent shrouds, rubber O-rings at the joints, drain ports,
  sealed breech covers, lanyard rings, maker's panels. Built so far: **the Cormorant** (six-dart cylinder that turns a
  sixth a shot, a crane that swings out, the gas bulb in the grip, a side gauge), **the Gannet** (ten-dart drum, a
  vented shroud, the screw-in cartridge under the barrel), **the Needler Mk I** (a glass cartridge of steel needles on
  top that slides out for the reload, a gas-bottle stock, a perforated cooling shroud), **the Sea-Pattern Carbine**
  (a rubber-sealed breech cover, a booted bolt, a sealed brass clip, a rubber muzzle cap on a cord), **Flechette 12**
  (the pump, the loading gate, a red flechette shell going in), **the Long Speargun** (a pneumatic brass tube, the
  barbed spear shown while loaded, the line to its reel, the charging lever), **the diver's knife** and **the
  Boarding Axe**.
- `DrawRtWeapon` poses the parts by group (hammer, trigger, cylinder/drum by rounds fired, latch, clip, bolt, pump,
  load shown while loaded, gauge by what's left); in first person the baked gun replaces the old one where it exists
  (at 1.5x, a viewmodel's licence: the stylised gloves would hide a true-size pistol), the fists on its own
  `grip_r` / `grip_l` markers. Shot `rvis_4_guns` (each gun side on, the Cormorant firing, the Needler and the
  Flechette mid-reload).
- **Built since (phase 4, second part):** Needler Mk II (a drum-shaped glass cartridge on the side), Trawlerman's Rifle (box magazine of brass clips, booted bolt, ladder sight), Bolt Harpoon (side quiver, winch crank, brass scope), Chumthrower (glass tank with the chum inside, hand pump, nozzle), Gatling Needler (six barrels that spin, a glass hopper of needles, a motor, a gas hose and strap), Twin Gannets (a mirrored Gannet in the left fist), Stormlock (finned barrel, side magazine, fins), Drum Flechette, Reef Rattler (three-chambered glass cartridges, the striker), Cannon Harpoon (explosive head with a fuse, recoil spring, shoulder pad), Limpet Launcher (four-round drum of charges, glue pad, fuse light), Net Gun (bell muzzle with the folded net and its weights), Harpoon Cannon; the Tesla Gaff (battery, copper coils, hook) and the Trident (bronze, barbed); the wonder weapons: the Galvanic Rod (a dynamo on a pole with a crank), the Resonator (brass horns and crystal), the Anemone Gun (coral and shell over a brass core, polyps in the barrel), the Tide Staff (a crystal in golden fins) and the Abyssal Lure (a caged lure on a stalk). 26 models, about 13 MB.
- **Still to do for the guns:** the Sawtooth; the Forge's sea-glass finish and the alternate ammunition shown on the gun; the wonder weapons' living effects (arcs, ringing, pulsing polyps, swirling water, the lure's heartbeat); glass reads as opaque in the PBR path; third-person guns for teammates; spent clips sinking and the gas guns' bubble bursts.

## Phase 5: the creature kit (2026-10-02)
- **Fish** (`DrawCreaturePbr` in redtide_vis.cpp): every fish-shaped species (plans fusiform, compressiform, shark,
  depressiform, anguilliform) draws on the Trawl's rigged fish (`assets/trawl/fish`: tuna, herring, perch, deep, pike,
  shark, ray, flat, billfish, eel archetypes on a four-bone spine; the name picks among them: a barracuda is the pike,
  a mackerel the tuna, a flounder the flat), painted from the species record's three colours (back, belly, fins) and
  swimming its spine at the agent's pace; rays beat their wings.
- **The other body plans** (`tools/artgen/creatures_rt.py` -> `assets/redtide/creatures/cr_<plan>.glb`, posed in code):
  the **crab** (carapace, eye stalks, clawed arms whose pincers open and shut, eight two-part legs stepping in
  alternate pairs), the **shrimp** (six segments curling, tail fan, rostrum, long antennae), the **cephalopod**
  (mantle, eyes, eight arms of four bones trailing in waves), the **jelly** (a pulsing bell, eight tentacles, oral
  arms), the **turtle** (carapace with scutes, plastron, head on its neck, beating front flippers) and the
  **cetacean** (a dolphin's body undulating up and down, flukes, a dorsal fin; seals use it too).
- The nearest 40 creatures within 28 m a frame get the rigged models; the rest, the bosses, and the plans not yet
  rigged (echinoderms, colonies, worms, birds, amphibians, crocodilians, leviathans, the factions) keep the
  CreatureBuilder models. `DEPTH_OLDCREATURES=1` draws the old ones for comparison. Frame cost after: the lagoon
  10.6 ms, the bridge 12.3, the cave mouth 11.5.
- **Not done yet:** the hero species' individual sculpts (the sharks, groupers, orcas, mantas, the giant squid, the
  Ghost Worm ...); weak points modelled visibly; the states on the body (hunting, fed, wounded, fleeing, netted,
  parasite, camouflage, dead); schools as instanced boids with phase offsets; flora with bones.

## Phase 6: the Sunken Ship (first part, 2026-10-02) and the map kit's surfaces for every map
- **Procedural surfaces on the levels' static geometry** (`surfaceDetail` in the inked shader, `SceneLight::surf` by
  map): the surface's facing and colour choose its material, so no wall or floor is a bare flat colour any more.
  The Sunken Ship (and the Void's station): riveted hull plate (1.2 x 0.8 m plates, dark seams, rows of rivets, rust
  bleeding down the walls) where it is cool; where it is warm, 18 cm deck planks (seams, butt joints, grain, each board
  its own shade) on the floors and mahogany panelling on the walls (panels, a dado moulding, wallpaper peeling above
  it). The Cave: strata and mottled wet rock. The Reef: rippled sand, encrusted rock. Atlantis: marble ashlar in offset
  courses, veins, algae in the joints and on the tops. The Void: rusted plate and black sand.
- Frame cost: the cabins 13.7 ms, Atlantis's lower town 14.9, the reef wall 11.2, a Wrecker hunt 13.7.
- **Not done yet for the Ship:** its modular kit as real models (the Marguerite's hull, funnel and masts outside; the
  salon's sideways chandelier, furniture piled against the low walls; the galley's tiles; the engine room's generator
  and the Goliath's nest), the Wreckers on figures, the Goliath's hero sculpt, the Galvanic Rod's arcs, and the traps
  as animated set pieces.
- **The human factions on the diver rig** (`DrawFactionFigure`, redtide_game.cpp): the **Wreckers** in rust-red
  patched gear (the Cutter with a knife, the Speargunner's long speargun, the Netman's net gun, the Foreman in the
  riveted hard suit with the Harpoon Cannon), the **Drowned** as pale ghosts in their hoods (the Deckhand's boarding
  axe, the Lantern Bearer's lantern on a pole, tridents), the **Remnant** (brass-domed sentinels with gatlings,
  pearl-suited researchers with needlers, hooded cultists with staffs; the Station Chief's mech keeps its model). Each
  holds its baked weapon in its right fist. Shots `rvis_6_wreckers`, `rvis_7_drowned`, `rvis_10_remnant` (a squad
  lined up, the match held still: `S.freeze`). The Reef Raiders (free divers without helmets) and the Lost Ones (not
  divers) still use their CreatureBuilder models.
- **The Ship's kit** (`tools/artgen/ship_rt.py` -> `assets/redtide/ship/`, baked like the guns; placed by `ShipDressing`
  in redtide_game.cpp and drawn with the level within 50 m): the salon's chandelier (two tiers of candle arms and glass
  drops) hanging askew, armchairs piled against the wall with one thrown on top, a table tipped on its side and one
  standing, the upright piano; the galley's range with its copper pots, crates and barrels; a bunk in every cabin; the
  engine room's generator; the bridge's wheel and binnacle; the funnel on the foredeck; barrels on the stern. Frame
  cost: the salon 13.9 ms, the engine room 14.7 (the Goliath's nest).
- Small fish only get the rigged models within 10 m (big creatures to 28 m), and up to 60 a frame, so a school close by
  doesn't use up the budget the sharks need.
- **The bosses as their own rigged models** (`creatures_rt.py`: `cr_goliath|lobster|orca|wyrm|angler`; `DrawBossPbr`,
  by `Match::bossKind`): the **Goliath** (a heavy grouper with armour plates over the gills and flanks, barnacles, two
  old harpoons and a chain in it; its gills glow red in their windows), the **Lobster** (pale and eyeless, crystal coral
  growing from its shell, a veined underside, walking with its tail flexing), the **Matriarch** (an orca, black and
  white, a scar and a tall dorsal), the **Cistern Wyrm** (a serpent on twelve bones with bronze collars, a crest and
  horns, undulating), the **Lantern Leviathan** (a vast anglerfish, a mouth of needle teeth, the pale lure on its rod,
  swaying). Still stylised assemblies of rounded parts, not the spec's hero sculpts; phase changes don't show yet.

## Phases 7-10: the other maps' kits (first pass, 2026-10-02)
- `tools/artgen/maps_rt.py` -> `assets/redtide/maps/` (baked like the guns): the Cave's stalagmite clusters and crystal
  coral; the Reef's brain, table, staghorn and fan corals and a giant clam; Atlantis's fluted column, bronze statue,
  amphora and brazier; the Void's specimen tank and glass sponge. In `BuildLevelModel` they stand in for the props the
  levels already place as boxes (stalagmites, crystals, brain and table corals, staghorn, fans, amphorae, tanks), scaled
  to each prop's box, and every other coral head along a reef wall is a brain or staghorn model; the reef's corals are
  tinted from a palette of saturated pinks, oranges, purples and greens (the baked coral is pale).
- Frame cost: the reef forest 13.3 ms, the bommie 12.6, the cave cathedral 13.8, Atlantis's forum 15.0.
- **Not yet:** the columns, statues, braziers, clam and sponge models aren't placed (no prop kind asks for them yet);
  each map's landmark per zone, its traps as animated set pieces, the air pocket's surface, the mangroves and
  sea grass as bending flora, the station's modules as models.

## Skins and costumes for both games (2026-10-02, the user's last step)
- `skins.h/.cpp` (the core, headless), `skins_data.cpp` (the catalogue), `skins_ui.cpp` (the Wardrobe page).
  **80 per game:** 15 in the store bought outright with tokens (Trawl 200-600, Red Tide 120-400) and 65 from crates (25
  common, 20 rare, 15 super rare, 5 legendary), every one named and coloured (the Trawl's from the harbour, its
  grounds and their fish, the Owners and the weather; Red Tide's from the reefs, the wrecks, the factions and, for the
  legendaries, the five bosses).
- **Characters only, silhouettes fixed:** a skin is four colours on the figure's own materials (top, trousers, hat or
  helmet, trim). Red Tide: your first-person arms (and the Locker's suit and helmet underneath); the Trawl: your own
  hand in the 3D view (`gLocalSlot` in trawl_view3d.cpp). Not yet: the Trawl's top-down figures, and teammates'
  skins over the network (each player's choice is local).
- **Crates:** earned at milestones (Red Tide: tide 10, 20 and 30 in a match, and the boss, in `AwardMatch`; the Trawl:
  every met deadline, in `Session::Count`, only on a real run: `Session::wardrobe`) and bought for 150 tokens. A roll
  picks the rarity (60 / 27 / 10 / 3 %), then a skin of it; **a skin you already own is a failed roll: nothing given,
  the crate spent** (the user's call). The legendary roll plays the boss stinger.
- **Tokens:** Red Tide's are its arcade profile's (the Locker's too); the Trawl's run tokens weren't kept anywhere, so
  the Wardrobe keeps a Trawl wallet, paid at each met deadline. Saved in `skins_save.txt` next to the exe (owned,
  worn, crates, the Trawl's tokens); tests and `--shots` never write it (`skins::gNoSave`).
- **Where:** Red Tide's Locker room has a "The Wardrobe" button; the arcade's Trawl reel a "Wardrobe" button. Shots
  `skins_trawl`, `skins_redtide`. `depth.exe --skins-test` checks the counts (15/25/20/15/5, unique ids, prices), the
  store, buying and opening crates, that a duplicate gives nothing and spends the crate, the odds over 10,000 rolls,
  and that only owned skins can be worn.
- Note: `--redtide-profile-test` fails one check ("a curious fish comes to the flare's light"), an ecosystem check none
  of this touches; not investigated.

## Costumes for both games (2026-10-02, after the user's note and A Night Off's examples)
- **What they are:** whole outfits worn over the figure that change how it looks. The colour skins above stay as they
  are, and a costume is worn over whatever skin is worn.
- **The models:** `tools/artgen/costumes.py` builds 32 of them on crew.py's skeleton, which both games' figures share,
  so one costume fits a sailor or a diver. Each is a padded suit grown round the skeleton (soft, weighted) plus rigid
  pieces on single bones: hoods with the face open, shells, fins, claws, hats and props. They go to
  `assets/shared/costumes/costume_<model>.glb`, about 6.5 MB in all.
  `fig::DrawCostume` draws one over any figure with that figure's own skinning matrices, matched by bone name.
- **The line-up, 20 per game** (`Costumes()` in skins_data.cpp), in A Night Off's tiers:
  - Red Tide:
    - common: Lobster Suit, Crab Shell, Puffed Up, Moon Jelly, Hermit's Lodgings, Starfish, Kelp Ghillie, Barnacled
    - rare: Shark Suit, Octopus, Lantern Angler, Sea Turtle, the Drowned's Sheet, Living Reef
    - super rare: Diving Bell, Contact Mine, Swordfish, Kraken Hood
    - special: the Goliath, the Nautilus (a submarine round the waist, the periscope as a hat)
  - The Trawl:
    - common: Fish Costume, Lobster Suit, the Gull, the Sack, Sandwich Board, Life Ring, Scarecrow, the Big Sou'wester
    - rare: Octopus, Crab, Mermaid Tail, Ghost Sheet, the Pirate, Lantern Angler
    - super rare: Great White, the Marlin, Over a Barrel, Kraken Hood
    - special: the Gannet (your boat round your waist), the Matriarch (an orca suit with three balloon orcas)
- **Getting them (my call; the user may change it):** they're bought outright on the Wardrobe's new **Costumes** tab,
  kept apart from the skins and their crates. Red Tide's cost 250 / 450 / 700 / 1000 tokens by tier; the Trawl's
  350 / 650 / 1000 / 1500 in its own tokens. Hovering one tries it on in a live 3D preview of your figure
  (`skins::gPreview`, set by each game).
- **Where they show:**
  - your figure in the Wardrobe;
  - in the Trawl, every hand's figure: each player's skin and costume reach the boat by `CMD_WARDROBE` and travel in
    the snapshot (`Crew::skin`, `Crew::costume`), so crewmates see them;
  - in first person, the sleeves take the costume's colour.
  - Not yet: Red Tide's other players (it has no networking yet), and the Trawl's top-down pixel figures.
- **Checks:**
  - `--skins-test` checks the counts, unique ids, a model for every costume, buying and wearing.
  - Shots: `costumes_gallery_trawl` / `costumes_gallery_redtide` (`DEPTH_SKINPAGE=0..1`), `costumes_wardrobe_*`, and
    the skins galleries `skins_gallery_*` (`DEPTH_SKINPAGE=0..7`).

## Later the same day
- **Glass:** clear in the PBR path. Glass parts are drawn last, blended, with a Fresnel edge: clear face on, silvered
  toward the rim. A part counts as glass if its material is named `glass`, or if the gun kit tagged it `glass` before
  the bake merged materials. That covers:
  - Red Tide's guns: the Needler's cartridge (its needles show), the gauges, the hoppers;
  - the ship's chandelier drops;
  - the specimen tanks and sponges.
  The Trawl's shared weapons weren't rebaked: each rebake adds about 13 MB to the public repo's history, so do it
  once, together with other changes to them.
- **Creature states on the body:**
  - a badly wounded animal trails a thread of blood;
  - a camouflaged one at rest takes on the water's colour.
  Still to do: netted, parasite, and dead bodies.
- **`--redtide-profile-test` passes again.** Its flare check put the curious fish beside the nocturnal one and a
  lamprey, and the fish rightly fled them. A burning flare also now outdraws a fish that's investigating something
  else.
- **The Sawtooth** (spec p.12, "a real sawfish rostrum with teeth, bound to a handle") isn't in the game's weapon data
  (data/redtide). It needs a design decision, its stats, before a model would show anywhere.
- **The air pocket's surface from below** needs a water line inside the air zones; today they're wholly dry.
- **Interactables as their own models.** The spec: "each get a distinct, well-lit, detailed model that never gets lost
  in the decor". `tools/artgen/stations_rt.py` -> `assets/redtide/stations/`, drawn by `DrawStationModel` in
  redtide_game.cpp. Each stands on its station's floor facing into the room, with moving parts posed by `DrawRtProp`.
  - **Tonic machine:** a brass cabinet, the bottle lit in the tonic's colour behind a port.
  - **Davy's Locker:** an iron-bound sea chest, its lantern buoy riding the swell above with the lamp lit.
  - **The Pressure Forge:** a riveted copper vessel, sea-glass glowing in its door.
  - **Workbench.**
  - **Power switch:** a knife switch whose lever follows the power.
  - **Cache:** a strongbox; the lid opens.
  - **Racks:** the real gun hung across a board on two pegs, with a chalk outline.
  - **The bought doors in each map's style:** the ship's and the Void's riveted bulkhead (dogs, wheel and price plate
    on both faces), the cave's rockfall shored with timber, the reef's net hung with floats, Atlantis's bronze grille;
    scaled to each doorway.
  - **The traps as set pieces:** the cargo drop is a banded crate in a cargo net on a hook. The cave's rockfall is a
    stalactite trembling at the roof, then a heap of rubble.
  - `DEPTH_OLDSTATIONS=1` draws the old boxes.
- **The HUD as the helmet's instruments** (`DrawDial`, `DrawCounter`, `DrawPortCracks`):
  - brass dials with needles for pressure (health), the scent meter, the predator pulse (the nearest tier-4+ hunter's
    closeness; the needle trembles with it) and the air (your wind);
  - scrip on a mechanical counter whose drums roll;
  - cracks across the port when badly hurt, and fully when down.
- **First-person moments:**
  - **Drinking a tonic:** the left hand brings a glass flask (`flask.glb`, the tonic glowing inside) up to the
    helmet's valve and tips it.
  - **Reviving:** the gun goes down out of view while both hands work below.
  - Shots: `DEPTH_VMDRINK=<0..1>`, `DEPTH_VMREVIVE=1`.
- **Accessibility** (the Graphics page, Red Tide's two columns):
  - **Viewmodel sway:** off holds the gun still.
  - **Colourblind:** blood, wound trails, the scent clouds and the scent dial in amber.
  - **Fog density:** x0.6 to 1.4, with a calibration strip.
  - Brightness has its own calibration strip on the Settings page.
- **The Forge's sea-glass:** a forged gun carries a frosted green-blue glass shell, its light breathing.
  `SetNextPbrGlass` draws a whole model in the blended glass pass, coloured by its tint (shader `uGlass` 2).
  Shot: `DEPTH_FORGED=1`.
- **Wonder weapons alive in the hand**, drawn after the ink:
  - the Galvanic Rod's arcs, with a flickering light;
  - the Resonator's rings of beads;
  - the Anemone Gun's pulsing polyps (gold when Forged);
  - the Tide Staff's swirl;
  - the Abyssal Lure's heartbeat.
  The poles' heads sit past the port's edge at rest, so theirs show as you aim down.
- **The map responds:**
  - silt kicked up along the bottom;
  - the Lost Ones bleed ichor and the Sentinels oil;
  - corpses (the ecosystem's, which weren't drawn) lie belly-up on their own models (`CreatureRoll`), settle to the
    floor, pale with age, and jerk and bleed while fed on. Shot: `DEPTH_STATION=corpse`.
- **The dossier** shows each beast on its rigged model (lit for the PBR path) and factions on the diver figure.
- **The reef's sand** was a pale (210,192,146) that blew out to white under the surface light; it's (168,150,112) now,
  and the caustics show on it.
  - Shots: `rvis_3_hands` with `DEPTH_STATION=tonic|locker|forge|power|workbench|cache|rack|door|traps` and
    `DEPTH_RTMAP=<map>`.

## Afterwards (2026-10-02)
- Teammates hold their current gun's baked model in third person (`DrawTeammate`).
- Gas, needle, spear and gatling guns breathe out a burst of bubbles from the muzzle as they fire.
- Landmarks from the map kits (`MapDressing`): Atlantis's forum and gate have a bronze statue and columns, its chapel
  braziers, its town columns; giant clams on the reef's sand flats and lagoon; glass sponges in the Void's galleries.
