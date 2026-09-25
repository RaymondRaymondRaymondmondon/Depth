# Depth: notes for Claude

Depth is a 2D game in C++17 with raylib 5.5, built with CMake (see README.md for build steps and the file map). It's a brighter, underwater take on Darkest Dungeon: a submarine hub (the Nautilus), roguelike rank-based expeditions, and platformer levels.

The vertical slice was built in an earlier claude.ai chat, "Computer game development". Balance was tuned there by auto-playing 400 expeditions: a mindless auto-player wins the Cave about 88% of the time, loses someone now and then, and usually gets a crew member Rattled. All art is placeholder shapes drawn in code.

## Building on this PC
Run `.\build.ps1` (add `-Run` to launch). This machine has Visual Studio 2026 (not 2022) and no git on PATH; the script sets up the VS dev shell and uses VS's bundled git. Output: `build\Release\depth.exe`.

## Design calls made where the design doc was open (the user may change any of these)
- **Stress ("Nerves")**: at 100 a hero becomes Rattled: less accurate, easier to hit, sometimes freezes. Sick Leave cures it, but the hero sits out the next expedition. Deliberately gentler than Darkest Dungeon's afflictions.
- **Tiers**: the design doc's 0/1/3/5/6 tiers were collapsed into Shallows (levels 0-2), Deep (3-4), and Abyssal (5-6). Only Shallows exists.
- **Flashlight**: low light means more stress and enemies hit harder, but loot is up to 1.8x richer.
- **Death's Door**: at 0 HP a hero keeps fighting; each further hit has a 35% chance to kill.
- **Relics**: simple stat bonuses. Two per hero, lost if the hero dies.
- **Bleed vs poison**: currently identical. There's a Library note about making poison stack.

## Locations
Built: the Cave (Shallows: 3 random rooms, then the Lobster mini-boss) and the Pipes (platformer).
Menu-only, "coming soon": Island, Weeds, Atlantis, Hull, Pirate Ship.

## Roadmap (suggested order)
1. Expand to 8 abilities per class with a choose-4 loadout.
2. Add a Workshop station for upgrades, split off from the overloaded Radar.
3. Build the Hull level with its Kraken fight.
4. Add the other three locations.
