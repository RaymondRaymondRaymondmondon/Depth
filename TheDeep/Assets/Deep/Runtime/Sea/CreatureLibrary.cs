// The Blender-made bodies (tools/artgen/deep_creatures.py -> Resources/Creatures/creatures_<biome>.glb): one mesh per
// species, found by the species' id. A species without one falls back to CreatureMeshes' code-built body.
using UnityEngine;

namespace Deep
{
    public static class CreatureLibrary
    {
        public static Mesh Get(string id) => ModelLibrary.Get("Creatures/creatures_shallows", id) ?? ModelLibrary.Get("Creatures/creatures_kelp", id) ?? ModelLibrary.Get("Creatures/creatures_caverns", id) ?? ModelLibrary.Get("Creatures/creatures_vents", id);
    }
}
