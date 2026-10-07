// Meshes from the Blender-made glTFs in Resources (imported by glTFast as prefabs): each object in a file is one model,
// found by its name (the species' or plant's id). Loaded once per file.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public static class ModelLibrary
    {
        static readonly Dictionary<string, Dictionary<string, Mesh>> files = new Dictionary<string, Dictionary<string, Mesh>>();

        public static Mesh Get(string file, string id)
        {
            if (!files.TryGetValue(file, out var map))
            {
                files[file] = map = new Dictionary<string, Mesh>();
                var prefab = Resources.Load<GameObject>(file);
                if (!prefab) Debug.LogWarning("DEEP: no models in " + file);
                else
                    foreach (var mf in prefab.GetComponentsInChildren<MeshFilter>(true))
                        if (mf.sharedMesh) map[mf.gameObject.name] = mf.sharedMesh;
            }
            return id != null && map.TryGetValue(id, out var m) ? m : null;
        }
    }
}
