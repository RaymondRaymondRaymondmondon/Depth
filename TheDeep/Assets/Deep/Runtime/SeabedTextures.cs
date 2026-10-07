// The seabed's four surfaces, generated once at startup as tileable textures with normal maps: rippled sand,
// fractured rock, reef rubble (broken coral and shell grit) and dark mud with algae. Periodic noise makes them tile
// without seams. (A Blender/numpy texture set can replace these later; the layer setup stays the same.)
using UnityEngine;

namespace Deep
{
    public static class SeabedTextures
    {
        const int R = 512;
        static TerrainLayer[] layers;

        public static TerrainLayer[] Layers()
        {
            if (layers != null) return layers;
            layers = new[]
            {
                Make("sand", Sand, 7f, 0.25f),
                Make("rock", Rock, 9f, 0.15f),
                Make("rubble", Rubble, 5f, 0.2f),
                Make("mud", Mud, 8f, 0.35f),
            };
            return layers;
        }

        delegate void Painter(float u, float v, out Color c, out float h);

        static TerrainLayer Make(string name, Painter paint, float tile, float smooth)
        {
            var hgt = new float[R * R];
            var col = new Color[R * R];
            for (int j = 0; j < R; j++)
                for (int i = 0; i < R; i++)
                {
                    paint(i / (float)R, j / (float)R, out var c, out var h);
                    c.a = smooth;
                    col[j * R + i] = c; hgt[j * R + i] = h;
                }
            var alb = new Texture2D(R, R, TextureFormat.RGBA32, true, false) { name = name, wrapMode = TextureWrapMode.Repeat, anisoLevel = 4 };
            alb.SetPixels(col); alb.Apply(true, true);
            var nrm = new Texture2D(R, R, TextureFormat.RGBA32, true, true) { name = name + "_n", wrapMode = TextureWrapMode.Repeat, anisoLevel = 4 };
            var np = new Color[R * R];
            for (int j = 0; j < R; j++)
                for (int i = 0; i < R; i++)
                {
                    float hx = hgt[j * R + (i + 1) % R] - hgt[j * R + (i + R - 1) % R];
                    float hy = hgt[((j + 1) % R) * R + i] - hgt[((j + R - 1) % R) * R + i];
                    var n = new Vector3(-hx * 6f, -hy * 6f, 1f).normalized;
                    np[j * R + i] = new Color(n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f, 1);
                }
            nrm.SetPixels(np); nrm.Apply(true, true);
            return new TerrainLayer { name = name, diffuseTexture = alb, normalMapTexture = nrm, tileSize = new Vector2(tile, tile), normalScale = 1f, smoothness = smooth };
        }

        // ---- periodic value noise (tiles with period p cells)
        static float Hash(int x, int y, int s) { unchecked { uint h = (uint)(x * 374761393 + y * 668265263 + s * 2147483647); h = (h ^ (h >> 13)) * 1274126177; return (h ^ (h >> 16)) / 4294967295f; } }
        static float VNoise(float u, float v, int p, int s)
        {
            float x = u * p, y = v * p; int x0 = Mathf.FloorToInt(x), y0 = Mathf.FloorToInt(y);
            float fx = x - x0, fy = y - y0; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
            int a0 = ((x0 % p) + p) % p, a1 = (a0 + 1) % p, b0 = ((y0 % p) + p) % p, b1 = (b0 + 1) % p;
            float v00 = Hash(a0, b0, s), v10 = Hash(a1, b0, s), v01 = Hash(a0, b1, s), v11 = Hash(a1, b1, s);
            return Mathf.Lerp(Mathf.Lerp(v00, v10, fx), Mathf.Lerp(v01, v11, fx), fy);
        }
        static float Fbm(float u, float v, int p, int oct, int s)
        {
            float t = 0, a = 0.5f, n = 0;
            for (int o = 0; o < oct; o++) { t += a * VNoise(u, v, p << o, s + o * 17); n += a; a *= 0.5f; }
            return t / n;
        }
        // cellular (Worley) distance, periodic: for pebbles, cracks and coral fragments
        static float Cell(float u, float v, int p, int s, out float id)
        {
            float x = u * p, y = v * p; int cx = Mathf.FloorToInt(x), cy = Mathf.FloorToInt(y);
            float best = 9, second = 9; id = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                {
                    int gx = cx + dx, gy = cy + dy, wx = ((gx % p) + p) % p, wy = ((gy % p) + p) % p;
                    float px = gx + Hash(wx, wy, s), py = gy + Hash(wx, wy, s + 1);
                    float d = (px - x) * (px - x) + (py - y) * (py - y);
                    if (d < best) { second = best; best = d; id = Hash(wx, wy, s + 2); } else if (d < second) second = d;
                }
            return Mathf.Sqrt(second) - Mathf.Sqrt(best);
        }

        static void Sand(float u, float v, out Color c, out float h)
        {
            float warp = Fbm(u, v, 4, 3, 1) * 0.6f;
            float ripple = 0.5f + 0.5f * Mathf.Sin((v * 14f + warp * 3f + Fbm(u, v, 8, 2, 2) * 0.4f) * Mathf.PI * 2f);
            float grain = VNoise(u, v, 256, 3);
            h = ripple * 0.7f + grain * 0.2f + Fbm(u, v, 8, 3, 4) * 0.3f;
            float k = 0.85f + 0.15f * grain + 0.08f * ripple;
            c = new Color(0.80f * k, 0.72f * k, 0.55f * k);
            if (Hash((int)(u * 128), (int)(v * 128), 5) > 0.985f) c = Color.Lerp(c, new Color(0.95f, 0.92f, 0.86f), 0.7f);   // shell flecks
        }
        static void Rock(float u, float v, out Color c, out float h)
        {
            // weathered sedimentary rock: wavy strata, pitted, with a few fine fractures and an algae film in places
            float big = Fbm(u, v, 4, 5, 10);
            float strata = 0.5f + 0.5f * Mathf.Sin((v * 9f + Fbm(u, v, 4, 3, 13) * 1.6f) * Mathf.PI * 2f);
            float pits = Mathf.Clamp01(Cell(u, v, 18, 11, out float id) * 6f);
            float crack = Cell(u, v, 5, 14, out _);
            h = big * 0.7f + strata * 0.25f + pits * 0.15f - (crack < 0.03f ? 0.3f : 0);
            float k = 0.6f + 0.3f * big + 0.12f * strata + 0.05f * id;
            c = Color.Lerp(new Color(0.40f, 0.37f, 0.33f), new Color(0.50f, 0.46f, 0.40f), strata) * k;
            c = Color.Lerp(c, new Color(0.22f, 0.32f, 0.18f), Mathf.Clamp01((Fbm(u, v, 8, 3, 12) - 0.5f) * 3f) * 0.6f);   // algae film
            if (crack < 0.03f) c *= 0.7f;
        }
        static void Rubble(float u, float v, out Color c, out float h)
        {
            float d = Cell(u, v, 22, 20, out float id);
            float bump = Mathf.Clamp01(d * 3f);
            h = bump * 0.8f + Fbm(u, v, 16, 3, 21) * 0.3f;
            var coral = Color.Lerp(new Color(0.82f, 0.62f, 0.58f), new Color(0.72f, 0.70f, 0.62f), id);
            var grit = new Color(0.66f, 0.58f, 0.46f);
            c = Color.Lerp(grit, coral, bump) * (0.8f + 0.25f * VNoise(u, v, 128, 22));
        }
        static void Mud(float u, float v, out Color c, out float h)
        {
            float f = Fbm(u, v, 4, 5, 30);
            h = f * 0.5f + VNoise(u, v, 128, 31) * 0.15f;
            c = Color.Lerp(new Color(0.20f, 0.19f, 0.14f), new Color(0.27f, 0.30f, 0.18f), Fbm(u, v, 8, 3, 32));
            c *= 0.85f + 0.3f * f;
        }
    }
}
