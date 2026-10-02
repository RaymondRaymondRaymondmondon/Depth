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
