// The seabed for phases 1-2: one 1.5 km square Unity Terrain built at runtime. Along x it runs from the Sunlit
// Shallows (a reef shelf 3-45 m down with lagoons, sandbars and coral patches) down a slope into the Kelp Labyrinth
// (50-150 m, a maze of ridged canyon walls), to the drop-off at the far edge that will lead to the Caverns.
using UnityEngine;

namespace Deep
{
    public class Seabed : MonoBehaviour
    {
        public const float Size = 1536f, Bottom = -360f, Top = 12f;
        public const int Res = 769;   // heightmap samples per side (2 m apart)
        public Terrain terrain;
        public float[,] heights;      // world y per sample (for flora placement without the terrain's sampler)
        int seed;

        public static float ShallowsEnd = 480f, KelpEnd = 1150f;

        public void Build(int seed)
        {
            this.seed = seed;
            var td = new TerrainData { heightmapResolution = Res };
            td.size = new Vector3(Size, Top - Bottom, Size);
            heights = new float[Res, Res];
            var h01 = new float[Res, Res];
            for (int j = 0; j < Res; j++)
                for (int i = 0; i < Res; i++)
                {
                    float x = i * Size / (Res - 1), z = j * Size / (Res - 1);
                    float y = HeightAt(x, z);
                    heights[j, i] = y;
                    h01[j, i] = Mathf.Clamp01((y - Bottom) / (Top - Bottom));
                }
            td.SetHeights(0, 0, h01);
            td.terrainLayers = SeabedTextures.Layers();
            td.alphamapResolution = 512;
            td.SetAlphamaps(0, 0, Splat(td));

            var go = Terrain.CreateTerrainGameObject(td);
            go.name = "SeabedTerrain";
            go.transform.SetParent(transform);
            go.transform.position = new Vector3(0, Bottom, 0);
            terrain = go.GetComponent<Terrain>();
            var mat = new Material(Resources.Load<Shader>("Shaders/Seabed"));
            mat.SetVector("_Tiles", new Vector4(7, 9, 5, 8));
            terrain.materialTemplate = mat;
            terrain.heightmapPixelError = 7;      // (the integrated GPU: coarser far LOD; the fog hides it)
            terrain.basemapDistance = 20000;    // (always the full shader: there is no separate base map shader)
            terrain.drawInstanced = false;     // (the custom shader reads the mesh normals)
            terrain.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.TwoSided;
        }

        float N(float x, float z, float f, int o) => Mathf.PerlinNoise(x * f + 31.7f * o + seed * 13.1f, z * f - 17.3f * o + seed * 7.9f);
        float Fbm(float x, float z, float f, int oct, int o)
        {
            float s = 0, a = 1, n = 0;
            for (int i = 0; i < oct; i++) { s += a * (N(x, z, f, o + i) * 2 - 1); n += a; a *= 0.5f; f *= 2.03f; }
            return s / n;
        }
        float Ridge(float x, float z, float f, int o) { float v = 1 - Mathf.Abs(Fbm(x, z, f, 3, o)); return v * v; }

        // world y of the seabed at (x, z): negative is below the surface
        public float HeightAt(float x, float z)
        {
            // a gentle domain warp so nothing lines up with the axes
            float wx = x + 60 * Fbm(x, z, 0.0021f, 2, 1), wz = z + 60 * Fbm(x, z, 0.0021f, 2, 2);

            // the Sunlit Shallows: a reef shelf with lagoons (basins), sandbars (ridges near the surface) and coral heads
            float shelf = -16f + 9f * Fbm(wx, wz, 0.006f, 4, 3);
            float lagoon = Mathf.SmoothStep(0, 1, (N(wx, wz, 0.004f, 4) - 0.55f) * 4f);
            shelf -= lagoon * 16f;
            float bar = Mathf.Max(0, Ridge(wx, wz, 0.0035f, 5) - 0.72f) * 48f;
            shelf += bar;                                       // (sandbars climb to 2-3 m: they ground a heavy sub)
            shelf += Mathf.Max(0, N(wx, wz, 0.05f, 6) - 0.62f) * 14f;   // coral heads
            shelf = Mathf.Min(shelf, -2.2f);

            // the Kelp Labyrinth: canyon floors 60-140 m down between ridged walls
            float t = Mathf.InverseLerp(ShallowsEnd, KelpEnd, wx);
            float kelpFloor = Mathf.Lerp(-62f, -140f, t) + 8f * Fbm(wx, wz, 0.01f, 3, 7);
            float walls = Ridge(wx, wz, 0.0085f, 8);
            walls = Mathf.SmoothStep(0, 1, (walls - 0.45f) * 3.2f);
            float kelp = kelpFloor + walls * (34f + 14f * N(wx, wz, 0.02f, 9));
            kelp += 2.5f * Fbm(wx, wz, 0.06f, 2, 10);

            // the slope between, and the drop-off past the labyrinth
            float s = Mathf.SmoothStep(0, 1, Mathf.InverseLerp(ShallowsEnd - 90f, ShallowsEnd + 70f, wx));
            float y = Mathf.Lerp(shelf, kelp, s);
            float drop = Mathf.SmoothStep(0, 1, Mathf.InverseLerp(KelpEnd, Size - 60f, wx));
            y = Mathf.Lerp(y, -320f + 20f * Fbm(wx, wz, 0.01f, 3, 11), drop);

            // the map's other edges fall away too (the sea goes on; the world here ends in deep water)
            float edge = Mathf.Min(Mathf.Min(x, Size - x), Mathf.Min(z, Size - z));
            y = Mathf.Lerp(-300f, y, Mathf.SmoothStep(0, 1, edge / 70f));
            return Mathf.Clamp(y, Bottom + 2, Top - 1);
        }

        // splat weights: 0 sand, 1 rock, 2 reef rubble, 3 mud and algae
        float[,,] Splat(TerrainData td)
        {
            int r = td.alphamapResolution;
            var a = new float[r, r, 4];
            for (int j = 0; j < r; j++)
                for (int i = 0; i < r; i++)
                {
                    float u = i / (r - 1f), v = j / (r - 1f);
                    float x = u * Size, z = v * Size;
                    float y = H(x, z);
                    float steep = td.GetSteepness(u, v);
                    float rock = Mathf.SmoothStep(0, 1, (steep - 24f) / 14f);
                    float deep = Mathf.SmoothStep(0, 1, (-y - 45f) / 25f);
                    float rubble = (1 - deep) * Mathf.SmoothStep(0, 1, (N(x, z, 0.03f, 20) - 0.5f) * 5f);
                    float mud = deep * (0.6f + 0.4f * N(x, z, 0.02f, 21));
                    float sand = Mathf.Max(0, 1 - rubble - mud);
                    float w0 = sand * (1 - rock), w1 = rock + 0.04f, w2 = rubble * (1 - rock), w3 = mud * (1 - rock);
                    float sum = w0 + w1 + w2 + w3;
                    a[j, i, 0] = w0 / sum; a[j, i, 1] = w1 / sum; a[j, i, 2] = w2 / sum; a[j, i, 3] = w3 / sum;
                }
            return a;
        }

        // fast: bilinear in the precomputed grid (the flora and the splat use this)
        public float H(float x, float z)
        {
            float fx = Mathf.Clamp(x / Size * (Res - 1), 0, Res - 1.001f), fz = Mathf.Clamp(z / Size * (Res - 1), 0, Res - 1.001f);
            int i = (int)fx, j = (int)fz; float u = fx - i, v = fz - j;
            return Mathf.Lerp(Mathf.Lerp(heights[j, i], heights[j, i + 1], u), Mathf.Lerp(heights[j + 1, i], heights[j + 1, i + 1], u), v);
        }

        public float SampleY(float x, float z) => terrain ? terrain.SampleHeight(new Vector3(x, 0, z)) + terrain.transform.position.y : HeightAt(x, z);

        public Vector3 SpawnPoint()
        {
            // a shallow spot near the shelf's middle, at the surface
            for (int k = 0; k < 400; k++)
            {
                float x = 120 + (k % 20) * 9f, z = Size * 0.5f + (k / 20) * 9f;
                if (HeightAt(x, z) < -8f) return new Vector3(x, -1.2f, z);
            }
            return new Vector3(150, -1.2f, Size * 0.5f);
        }
    }
}
