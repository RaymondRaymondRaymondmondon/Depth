// Caustic light frames for the sun's cookie: the bright web of a wavy surface focusing sunlight on the seabed. Two
// layers of moving Worley cell edges, whose points circle so the loop has no seam. Values sit around 0.5 with bright
// lines near 1, so the cookie dims the light between the lines (the sun is brightened to make up for it).
using UnityEngine;

namespace Deep
{
    public static class Caustics
    {
        public static Texture2D[] Frames(int count, int res, int seed)
        {
            var frames = new Texture2D[count];
            var rnd = new System.Random(seed);
            const int P = 5;   // cells per side (periodic)
            var ox = new float[2, P, P]; var oy = new float[2, P, P]; var ph = new float[2, P, P]; var rr = new float[2, P, P];
            for (int l = 0; l < 2; l++) for (int a = 0; a < P; a++) for (int b = 0; b < P; b++)
                    { ox[l, a, b] = (float)rnd.NextDouble(); oy[l, a, b] = (float)rnd.NextDouble(); ph[l, a, b] = (float)rnd.NextDouble() * 6.283f; rr[l, a, b] = 0.15f + 0.2f * (float)rnd.NextDouble(); }
            var px = new Color32[res * res];
            for (int f = 0; f < count; f++)
            {
                float t = f / (float)count * 6.2831853f;
                for (int j = 0; j < res; j++)
                    for (int i = 0; i < res; i++)
                    {
                        float sum = 0;
                        for (int l = 0; l < 2; l++)
                        {
                            float u = (i / (float)res + l * 0.37f) * P, v = (j / (float)res + l * 0.21f) * P;
                            int cx = Mathf.FloorToInt(u), cy = Mathf.FloorToInt(v);
                            float f1 = 9, f2 = 9;
                            for (int dy = -1; dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++)
                                {
                                    int gx = cx + dx, gy = cy + dy, wx = ((gx % P) + P) % P, wy = ((gy % P) + P) % P;
                                    float a = ph[l, wx, wy] + t * (l == 0 ? 1 : -1);
                                    float qx = gx + ox[l, wx, wy] + Mathf.Cos(a) * rr[l, wx, wy], qy = gy + oy[l, wx, wy] + Mathf.Sin(a) * rr[l, wx, wy];
                                    float d = Mathf.Sqrt((qx - u) * (qx - u) + (qy - v) * (qy - v));
                                    if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
                                }
                            float edge = 1 - Mathf.Clamp01((f2 - f1) * 3.2f);
                            sum += Mathf.Pow(edge, 5);
                        }
                        float c = Mathf.Clamp01(0.42f + sum * 0.75f);
                        byte bb = (byte)(c * 255);
                        px[j * res + i] = new Color32(bb, bb, bb, 255);
                    }
                var tex = new Texture2D(res, res, TextureFormat.RGBA32, true, true) { wrapMode = TextureWrapMode.Repeat, filterMode = FilterMode.Bilinear, name = "caustic" + f };
                tex.SetPixels32(px); tex.Apply(true, true);
                frames[f] = tex;
            }
            return frames;
        }
    }
}
