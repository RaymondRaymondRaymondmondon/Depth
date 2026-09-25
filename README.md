# Depth: vertical slice

A playable first slice of Depth: the Nautilus hub, one roguelike expedition into the Cave, and the Pipes platformer.
Written in C++17 with [raylib](https://www.raylib.com/).

## Building it

You need three things: a C++ compiler, CMake, and Git. The first build downloads raylib automatically, so it needs internet and takes a minute or two. Later builds are fast.

### Windows
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
- **Hub and menus:** mouse.
- **Combat:** click an ability, then click a highlighted target. Right-click cancels.
- **Pipes:** A/D or arrow keys to move, Space/W/Up to jump (hold for a higher jump), Esc to give up the run.

## What's in the slice
- **The Nautilus**, with all seven stations: Helm, Crew, Radar, Ward, Sick Leave, Library, and Periscope.
- **The Cave (Shallows):** three random rooms (fights or treasure), then the Lobster mini-boss. It has the flashlight, rank-based combat, stress ("Nerves"), Death's Door, and relic loot.
- **Four classes:** Nurse, Diver, Captain, and Mechanic, with four abilities each.
- **The Pipes:** four shuffled chunks out of five, with checkpoints. Beating it reshuffles the layout.

The Island, Weeds, Atlantis, Hull, and Pirate Ship appear in the menus as "coming soon".

## Where things live
| File | What it does | Edit it to... |
|---|---|---|
| `src/game.h` | All the data types and the shared game state | add a new stat, status, or field |
| `src/data.cpp` | Classes, abilities, relics, enemies, and XP | **balance the game**, add classes, abilities, relics, or enemies |
| `src/hub.cpp` | The Nautilus and every station screen | change menus and shops |
| `src/dungeon.cpp` | Expeditions and combat | change combat rules, light, or room generation |
| `src/platformer.cpp` | The Pipes | **design new level chunks** (they're drawn as text: see the legend at the top of the file) |
| `src/ui.cpp` | Buttons, panels, and text helpers | change the look of the UI |
| `src/main.cpp` | The window and main loop | rarely needs changing |

Good first experiments: tweak an enemy's stats in `MakeEnemy` (data.cpp), give a class a new ability in `BuildNurse` and its siblings, or draw a sixth Pipes chunk in `CHUNKS` (platformer.cpp) (bump the array size `CHUNKS[5]` to `[6]` and add `5` to the list in `GeneratePipesLayout`).
