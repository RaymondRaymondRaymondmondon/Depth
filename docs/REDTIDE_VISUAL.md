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
