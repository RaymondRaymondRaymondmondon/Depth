// The nine underwater biomes stacked by depth (doc: "The ten ecosystems"), each with its water: the fog colour and
// visibility, and how fast sound fades (dB per metre, the Wake's absorption factor alpha).
using UnityEngine;

namespace Deep
{
    public struct BiomeDef
    {
        public string name; public float top, bottom; public Color water; public float visibility, absorb; public bool echo;
    }

    public static class Biomes
    {
        public static readonly BiomeDef[] All =
        {
            new BiomeDef { name = "The Sunlit Shallows", top = 0, bottom = 50, water = new Color(0.10f, 0.42f, 0.46f), visibility = 70, absorb = 0.005f },
            new BiomeDef { name = "The Kelp Labyrinth", top = 50, bottom = 150, water = new Color(0.10f, 0.32f, 0.26f), visibility = 48, absorb = 0.038f },
            new BiomeDef { name = "The Bioluminescent Caverns", top = 150, bottom = 300, water = new Color(0.02f, 0.06f, 0.12f), visibility = 30, absorb = 0.005f, echo = true },
            new BiomeDef { name = "The Thermal Vents", top = 300, bottom = 500, water = new Color(0.08f, 0.05f, 0.05f), visibility = 30, absorb = 0.005f },
            new BiomeDef { name = "The Drowned Forest", top = 500, bottom = 700, water = new Color(0.04f, 0.06f, 0.04f), visibility = 25, absorb = 0.01f },
            new BiomeDef { name = "The Crystal Reef", top = 700, bottom = 900, water = new Color(0.03f, 0.05f, 0.10f), visibility = 40, absorb = 0.005f },
            new BiomeDef { name = "The Brine Pools", top = 900, bottom = 1200, water = new Color(0.04f, 0.04f, 0.06f), visibility = 25, absorb = 0.005f },
            new BiomeDef { name = "The Abyssal Graveyard", top = 1200, bottom = 1800, water = new Color(0.01f, 0.015f, 0.025f), visibility = 20, absorb = 0.005f },
            new BiomeDef { name = "The Void Edge", top = 1800, bottom = 99999, water = new Color(0.005f, 0.005f, 0.015f), visibility = 15, absorb = 0.005f },
        };

        public static int IndexAt(float depth)   // depth in metres below the surface (positive)
        {
            for (int i = 0; i < All.Length; i++) if (depth < All[i].bottom) return i;
            return All.Length - 1;
        }
        public static BiomeDef At(float depth) => All[IndexAt(Mathf.Max(0, depth))];
    }
}
