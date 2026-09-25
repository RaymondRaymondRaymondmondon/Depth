# Depth: vertical slice

A playable first slice of Depth: the Nautilus hub, one roguelike expedition into the Cave, and three platform levels.
Written in C++17 with [raylib](https://www.raylib.com/).

## Building it

You need three things: a C++ compiler, CMake, and Git. The first build downloads raylib automatically, so it needs internet and takes a minute or two. Later builds are fast.

### Windows
**Shortcut:** if Visual Studio (2022 or later, with "Desktop development with C++") is installed, just run `.\build.ps1 -Run` from this folder in any PowerShell. It finds Visual Studio, uses its bundled CMake and Git, builds, and launches the game.

Manual steps:
1. Install **Visual Studio 2022 Community** (free). In the installer, tick **"Desktop development with C++"**. That includes the compiler and CMake.
2. Install **Git for Windows** (git-scm.com).
3. Open **"Developer PowerShell for VS 2022"** from the Start menu, `cd` into this folder, and run:
   ```
   cmake -B build
   cmake --build build --config Release
   .\build\Release\depth.exe
   ```
   *(Alternative: in Visual Studio choose File > Open > Folder, pick this folder, wait for CMake to finish, then pick `depth.exe` from the green Run dropdown.)*

### Mac
```
xcode-select --install        # compiler + git
brew install cmake            # needs Homebrew: brew.sh
cmake -B build
cmake --build build --config Release
./build/depth
```

### Linux (Ubuntu/Debian)
```
sudo apt install build-essential cmake git libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -B build
cmake --build build --config Release
./build/depth
```

After you change code, just run `cmake --build build --config Release` again.

## Controls
- **The Nautilus salon:** click any station in the room, or click a crew member to open Crew Quarters with them selected.
- **Menus:** mouse.
- **Combat:** click an ability, then click a highlighted target. Right-click cancels.
- **Platform levels:** A/D or arrow keys to move, Space/W/Up to jump (hold for a higher jump, tap for a hop). Push into a wall in mid-air to slide, and jump to kick off it. Esc gives up the run. A gamepad works too.

## What's in the slice
- **The Nautilus's grand salon**, a single-screen 3D room modelled on Captain Nemo's salon, with eight stations around it: Crew Quarters, Library, Radar Room, Helm, Periscope, Workshop, Sick Bay (by the organ), and the Ward. The ocean shows through the great window and steam pipes run along the ceiling. The ship's own hands and Barnacle, the ship's cat, wander the room; click the cat for a purr.
- **Saving:** the game saves itself whenever you return to the salon, and when you quit. "Start a new game" (bottom left of the salon) wipes the save.
- **The Cave**, at levels 0, 1, 3, 5 and 6: beating one unlocks the next, and earlier levels stay open. Random rooms (fights or treasure), then the Lobster mini-boss. The cave is painted in layers that slide past as the party walks, and every attack, throw, shot and heal is animated. It has the flashlight (which really lights the scene), rank-based combat, stress ("Nerves"), Death's Door, and relic loot.
- **Four classes:** Nurse, Diver, Captain, and Mechanic. Each has eight abilities, and a crew member brings four of them. Four are available from the start; the rest unlock at levels 1, 1, 2, and 3.
- **The Workshop:** lasting upgrades bought with gold (flashlight reflector, bunk extension, sonar array, infirmary gear).
- **Three platform levels at the Periscope**, in a deliberately retro pixel-art style (everything else aims for Darkest Dungeon's inked, painterly look). The challenge is the jumping, as in Super Meat Boy:
  - **The Pipes**: 6 sections, no enemies at all.
  - **The Hull**: 5 sections along the outside of the Nautilus, with crabs, leaping eels, urchins and mines, then the Kraken (optional; stomp it for a relic).
  - **The Pirate Ship**: across the deck, down the hatch into the hold, up the companionway, and into Blackbeard's cabin. Pirates burst out of doors to stab you or shoot from behind barrels. Blackbeard is deadly to touch until you trick his charge into a wall; stomp him while he's dazed.
  - Enemies are deadly to touch; only bosses can be stomped. Clearing a level unlocks the next and reshuffles its sections.
  - Options at the Periscope: **Normal** difficulty leaves out the gears, mines, spiked balls and jets (**Hard** keeps them, for +50% bonus gold). A death sends you back to the start, unless you turn **checkpoints** on, which means no relic.

The Island, Weeds, and Atlantis appear in the menus as "coming soon".

## Where things live
| File | What it does | Edit it to... |
|---|---|---|
| `src/game.h` | All the data types and the shared game state | add a new stat, status, or field |
| `src/data.cpp` | Classes, abilities, relics, enemies, XP, and Workshop upgrade tables | **balance the game**, add classes, abilities, relics, or enemies |
| `src/hub.cpp` | The Nautilus deck and every station screen | change the deck, menus, and shops |
| `src/render.cpp` | Lighting, post-processing, generated textures, fonts, props, crew figures | change the look of the game |
| `src/dungeon.cpp` | Expeditions and combat | change combat rules, light, or room generation |
| `src/platformer.cpp` | The three platform levels, their bosses, and the level verifier | **design new level sections** (they're drawn as text: see the legend at the top of the file) |
| `src/ui.cpp` | Buttons, panels, and text helpers | change the look of the UI |
| `src/main.cpp` | The window and main loop | rarely needs changing |

## Developer switches
- `depth.exe --sim 1000 [level] [random]` auto-plays 1000 expeditions and prints win rate, deaths, and how often someone ends up Rattled. The default auto-player heals anyone below 40% HP and otherwise hits the weakest enemy with its strongest attack; add `random` for a player who picks anything.
- `depth.exe --shots shots` renders every screen to PNG files in the `shots` folder and quits. The folder has to exist.
- `depth.exe --sprites sprites.png` draws every sprite in the game (crew poses, the ship's hands, the cat, cave creatures, and the platform sprites and tiles) onto one sheet.
- `depth.exe --verify` proves every platform section can be crossed. It searches button presses using the real movement code, so a jump that looks possible but isn't gets caught. Run it after designing a section. It takes about a minute and a half.

Good first experiments: tweak an enemy's stats in `MakeEnemy` (data.cpp), give a class a new ability in `BuildNurse` and its siblings, or draw a new platform section: add a block of text (24 wide) like `PIPE_RISER`, `HULL` or `DECK_WAIST` in platformer.cpp and list it in `Lv()`, then run `--verify`.
