// The Blender-made bodies (tools/artgen/deep_creatures.py -> Resources/Creatures/creatures_<biome>.glb, imported by
// glTFast): one mesh per species, found by the species' id (each glTF node is named by it). A species without one
// falls back to CreatureMeshes' code-built body.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public static class CreatureLibrary
    {
        static Dictionary<string, Mesh> meshes;

        static void Load()
        {
            meshes = new Dictionary<string, Mesh>();
            foreach (var b in new[] { "shallows", "kelp" })
            {
                var prefab = Resources.Load<GameObject>("Creatures/creatures_" + b);
                if (!prefab) { Debug.LogWarning("DEEP SEA: no creature models for " + b); continue; }
                foreach (var mf in prefab.GetComponentsInChildren<MeshFilter>(true))
                    if (mf.sharedMesh) meshes[mf.gameObject.name] = mf.sharedMesh;
            }
            Debug.Log($"DEEP SEA: {meshes.Count} Blender bodies");
        }

        public static Mesh Get(string id)
        {
            if (meshes == null) Load();
            return id != null && meshes.TryGetValue(id, out var m) ? m : null;
        }
    }
}
