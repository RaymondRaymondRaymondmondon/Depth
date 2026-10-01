# Red Tide map data tools

Everything a Red Tide map needs is data. These scripts build the five map workbooks and export them to JSON.

## Files
- `species_ship.py`, `species_cave.py`, `species_reef.py`, `species_atlantis.py`, `species_void.py` — the source data for each map (species, attacks, diet, flora, layout, spawn pools, boss, faction). Edit these to change a map at the source.
- `build_map.py` — builds one workbook from a species module: `python build_map.py species_cave` → `RedTide_UnderwaterCave_data.xlsx`. `species_ship.py` also holds the shared SIZE table (HP, bounty, blood, speed by size class 1–7) that every map uses.
- `export_json.py` — exports a workbook to the JSON files the game loads: `python export_json.py RedTide_UnderwaterCave_data.xlsx redtide_maps/cave`.

## Workflow
1. Tune in the workbook (blue cells) or in the species module, whichever is the source of truth for your team. If the workbook is the source, skip step 2.
2. `python build_map.py species_<map>` to rebuild a workbook from its module.
3. Recalculate formulas (open and save in Excel, or LibreOffice headless: `soffice --headless --convert-to xlsx --outdir . <file>`), so formula cells carry values.
4. `python export_json.py <workbook> redtide_maps/<map>` — the game loads `redtide_maps/<map>/*.json` at startup.

## JSON contents
`species.json` (one object per beast), `attacks.json`, `diet.json` (predator → {food: weight}), `flora.json`, `spawn.json`, `tunables.json`, `tide_curve.json` (40 tides), `blockout.json` (zones, links, points_of_interest, and any map-specific tables such as slipstreams, corridors, building_types, aqueduct, cistern_grates, station_modules, overlooks, lure_beacons, plus design_notes), `boss.json`, `faction.json`.

Requirements: Python 3, openpyxl.
