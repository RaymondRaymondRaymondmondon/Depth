// The game's shaders, loaded once from Resources and kept (playtest: Unity's Shader.Find crashed natively when a
// diver died and the satchel's beacon asked for "Deep/Glow" mid-game). Use DeepShaders.Get("Deep/Lit"), never
// Shader.Find.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public static class DeepShaders
    {
        static readonly Dictionary<string, Shader> cache = new Dictionary<string, Shader>();
        static readonly Dictionary<string, string> paths = new Dictionary<string, string>
        {
            { "Deep/Lit", "Shaders/DeepLit" }, { "Deep/Glass", "Shaders/DeepGlass" }, { "Deep/Glow", "Shaders/Glow" },
            { "Deep/Creature", "Shaders/Creature" }, { "Deep/Flora", "Shaders/Flora" }, { "Deep/Seabed", "Shaders/Seabed" },
            { "Deep/Smoke", "Shaders/Smoke" }, { "Deep/Snow", "Shaders/Snow" }, { "Deep/Shaft", "Shaders/Shaft" }, { "Deep/OceanSurface", "Shaders/OceanSurface" },
        };

        public static Shader Get(string name)
        {
            if (cache.TryGetValue(name, out var s) && s) return s;
            s = paths.TryGetValue(name, out var path) ? Resources.Load<Shader>(path) : null;
            if (!s) Debug.LogWarning("DEEP: no shader " + name);
            cache[name] = s;
            return s;
        }

        // load them all at the start (nothing is looked up for the first time in the middle of play)
        public static void Warm() { foreach (var k in paths.Keys) Get(k); }
    }
}
