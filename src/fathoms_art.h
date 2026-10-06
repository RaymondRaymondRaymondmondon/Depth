#pragma once
// Fathoms' code art (fathoms_art.cpp): every unit, ship, building, resource node and neutral site built from lit
// primitives (rt::MeshBuilder), in each faction's palette with the owner's colour on sails, flags and sashes; and the
// archipelago's terrain mesh. Scale: one tile is one world unit; x east, z south; the sea's surface is y = 0.
#include "fathoms.h"
#include "raylib.h"

namespace fa {
Color PlayerColor(int c);                         // the six lobby colours
Color FactionTone(int faction);                   // each faction's base tone (hulls, armour, stone)
const Model& UnitModel(int def, int faction, int color);
const Model& BuildingModel(int def, int faction, int color);
const Model& NodeModel(int kind);
const Model& SiteModel(int kind, int variant);    // cove fortress, tribal town, volcano altar, sunken ruin
const Model& RingModel();                         // a flat ring (selection, rally, ground marks)
float TileY(const World& w, float x, float y);    // the ground's height under a point (0 at sea)
struct TerrainMesh { Model model{}; bool ready = false; uint32_t key = 0; std::vector<unsigned char> base; int verts = 0; };
void BuildTerrain(const World& w, TerrainMesh& t); // land and sea floor from the tile map (rebuilt when lava comes or goes)
void ShadeTerrain(const World& w, TerrainMesh& t, int viewer);   // explored-but-unseen ground dimmed (vertex colours)
void UnloadFathomsArt();
}
