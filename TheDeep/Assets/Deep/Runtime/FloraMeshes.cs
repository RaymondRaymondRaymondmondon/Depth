// Stand-in plant meshes built in code (the Blender models replace them later). Vertex colour: rgb is the colour
// variation, alpha how far up the plant the vertex sits (0 root .. 1 tip), which the shader uses for sway.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public static class FloraMeshes
    {
        class MB
        {
            public readonly List<Vector3> v = new List<Vector3>(); public readonly List<Color> c = new List<Color>(); public readonly List<int> t = new List<int>();
            public int V(Vector3 p, Color col) { v.Add(p); c.Add(col); return v.Count - 1; }
            public void Tri(int a, int b, int d) { t.Add(a); t.Add(b); t.Add(d); }
            public void Quad(int a, int b, int d, int e) { Tri(a, b, d); Tri(b, e, d); }
            public Mesh Done(string name)
            {
                var m = new Mesh { name = name };
                m.SetVertices(v); m.SetColors(c); m.SetTriangles(t, 0); m.RecalculateNormals(); m.RecalculateBounds();
                return m;
            }
        }
        static Color Var(Color baseC, System.Random r, float amt, float h) { float k = 1 + ((float)r.NextDouble() - 0.5f) * amt; return new Color(baseC.r * k, baseC.g * k, baseC.b * k, h); }

        // Iron-Kelp: a 30 m stalk (scaled per instance) of crossed ribbons, leaf blades alternating up it, a float at each blade
        public static Mesh Kelp(int seg, System.Random r)
        {
            var b = new MB(); const float H = 30f;
            var stalk = new Color(0.55f, 0.48f, 0.26f);
            for (int ribbon = 0; ribbon < 2; ribbon++)
            {
                Vector3 side = ribbon == 0 ? Vector3.right : Vector3.forward;
                int prevL = -1, prevR = -1;
                for (int i = 0; i <= seg; i++)
                {
                    float h = i / (float)seg, y = h * H;
                    var off = new Vector3(Mathf.Sin(h * 5f) * 0.35f, 0, Mathf.Cos(h * 4f) * 0.35f);
                    float w = Mathf.Lerp(0.32f, 0.12f, h);
                    int L = b.V(new Vector3(0, y, 0) + off - side * w, Var(stalk, r, 0.1f, h)), R = b.V(new Vector3(0, y, 0) + off + side * w, Var(stalk, r, 0.1f, h));
                    if (prevL >= 0) b.Quad(prevL, prevR, L, R);
                    prevL = L; prevR = R;
                }
            }
            var leaf = new Color(0.62f, 0.58f, 0.28f);
            int blades = seg * 3;
            for (int i = 1; i < blades; i++)
            {
                float h = i / (float)blades, y = h * H;
                float a = i * 2.4f;
                var dir = new Vector3(Mathf.Cos(a), 0, Mathf.Sin(a));
                var perp = new Vector3(-dir.z, 0, dir.x);
                var root = new Vector3(Mathf.Sin(h * 5f) * 0.35f, y, Mathf.Cos(h * 4f) * 0.35f);
                float len = Mathf.Lerp(2.6f, 1.5f, h) * (0.8f + 0.4f * (float)r.NextDouble()), wid = 0.55f;
                var col = Var(leaf, r, 0.25f, h);
                // a blade in three sections, curling up then drooping
                int p0 = b.V(root - perp * 0.04f, col), p1 = b.V(root + perp * 0.04f, col);
                for (int s = 1; s <= 3; s++)
                {
                    float f = s / 3f, ww = wid * Mathf.Sin(f * Mathf.PI * 0.9f + 0.2f);
                    var c = root + dir * len * f + Vector3.up * (0.45f * f - 0.6f * f * f);
                    var col2 = new Color(col.r, col.g, col.b, Mathf.Min(1, h + 0.04f * f));
                    int q0 = b.V(c - perp * ww * 0.5f, col2), q1 = b.V(c + perp * ww * 0.5f, col2);
                    b.Quad(p0, p1, q0, q1); p0 = q0; p1 = q1;
                }
                // the gas float at the blade's base
                var fp = root + dir * 0.12f + Vector3.up * 0.05f; var fc = new Color(0.78f, 0.68f, 0.32f, h);
                int f0 = b.V(fp + Vector3.up * 0.09f, fc), f1 = b.V(fp + dir * 0.07f, fc), f2 = b.V(fp + perp * 0.07f, fc), f3 = b.V(fp - dir * 0.07f, fc), f4 = b.V(fp - perp * 0.07f, fc), f5 = b.V(fp - Vector3.up * 0.09f, fc);
                b.Tri(f0, f1, f2); b.Tri(f0, f2, f3); b.Tri(f0, f3, f4); b.Tri(f0, f4, f1); b.Tri(f5, f2, f1); b.Tri(f5, f3, f2); b.Tri(f5, f4, f3); b.Tri(f5, f1, f4);
            }
            return b.Done("kelp" + seg);
        }

        public static Mesh GrassClump(System.Random r)
        {
            var b = new MB(); var g = new Color(0.45f, 0.62f, 0.30f);
            for (int k = 0; k < 16; k++)
            {
                float a = (float)r.NextDouble() * 6.283f, rad = (float)r.NextDouble() * 0.35f, h = 0.35f + (float)r.NextDouble() * 0.6f;
                var root = new Vector3(Mathf.Cos(a) * rad, 0, Mathf.Sin(a) * rad);
                var lean = new Vector3(Mathf.Cos(a), 0, Mathf.Sin(a)) * (0.1f + 0.2f * (float)r.NextDouble());
                var side = new Vector3(-Mathf.Sin(a + 1.2f), 0, Mathf.Cos(a + 1.2f));
                var col = Var(g, r, 0.35f, 0);
                int p0 = b.V(root - side * 0.025f, col), p1 = b.V(root + side * 0.025f, col);
                for (int s = 1; s <= 3; s++)
                {
                    float f = s / 3f; var c = root + Vector3.up * h * f + lean * f * f;
                    var cc = new Color(col.r * (0.9f + 0.3f * f), col.g * (0.9f + 0.3f * f), col.b, f);
                    float w = 0.025f * (1 - f * 0.8f);
                    int q0 = b.V(c - side * w, cc), q1 = b.V(c + side * w, cc);
                    b.Quad(p0, p1, q0, q1); p0 = q0; p1 = q1;
                }
            }
            return b.Done("seagrass");
        }

        public static Mesh TableCoral(System.Random r)
        {
            var b = new MB(); var cc = new Color(0.85f, 0.62f, 0.55f);
            // a short stalk
            int segs = 8; float sh = 0.45f;
            for (int i = 0; i < segs; i++)
            {
                float a0 = i * 6.283f / segs, a1 = (i + 1) * 6.283f / segs;
                int p0 = b.V(new Vector3(Mathf.Cos(a0) * 0.12f, 0, Mathf.Sin(a0) * 0.12f), Var(cc, r, 0.1f, 0)), p1 = b.V(new Vector3(Mathf.Cos(a1) * 0.12f, 0, Mathf.Sin(a1) * 0.12f), Var(cc, r, 0.1f, 0));
                int q0 = b.V(new Vector3(Mathf.Cos(a0) * 0.08f, sh, Mathf.Sin(a0) * 0.08f), Var(cc, r, 0.1f, 0)), q1 = b.V(new Vector3(Mathf.Cos(a1) * 0.08f, sh, Mathf.Sin(a1) * 0.08f), Var(cc, r, 0.1f, 0));
                b.Quad(p0, p1, q0, q1);
            }
            // the table: an irregular plate, thicker at the rim, two tiers
            for (int tier = 0; tier < 2; tier++)
            {
                float y = sh + tier * 0.18f, R = tier == 0 ? 0.95f : 0.6f;
                int n = 22; int c0 = b.V(new Vector3(0, y + 0.03f, 0), Var(cc, r, 0.1f, 0)); int first = -1, prev = -1;
                for (int i = 0; i <= n; i++)
                {
                    float a = i * 6.283f / n; float rr = R * (0.8f + 0.25f * Mathf.PerlinNoise(a * 1.3f + tier * 3, 0.5f));
                    int p = i == n ? first : b.V(new Vector3(Mathf.Cos(a) * rr, y + 0.06f * Mathf.Sin(a * 3), Mathf.Sin(a) * rr), Var(cc, r, 0.15f, 0));
                    if (first < 0) first = p;
                    if (prev >= 0) b.Tri(c0, p, prev);
                    prev = p;
                }
            }
            return b.Done("tablecoral");
        }

        public static Mesh BrainCoral(System.Random r)
        {
            var b = new MB(); var cc = new Color(0.82f, 0.74f, 0.48f);
            int la = 10, lo = 18; float R = 0.6f;
            var idx = new int[la + 1, lo + 1];
            for (int i = 0; i <= la; i++)
                for (int j = 0; j <= lo; j++)
                {
                    float th = i / (float)la * Mathf.PI * 0.5f, ph = j / (float)lo * Mathf.PI * 2f;
                    var n = new Vector3(Mathf.Cos(ph) * Mathf.Cos(th), Mathf.Sin(th), Mathf.Sin(ph) * Mathf.Cos(th));
                    float groove = 0.5f + 0.5f * Mathf.Sin(ph * 7 + th * 9 + Mathf.Sin(ph * 3) * 2);
                    var p = n * R * (0.92f + 0.08f * groove); p.y *= 0.7f;
                    var col = cc * (0.75f + 0.35f * groove); col.a = 0;
                    idx[i, j] = b.V(p, col);
                }
            for (int i = 0; i < la; i++) for (int j = 0; j < lo; j++) b.Quad(idx[i, j], idx[i, j + 1], idx[i + 1, j], idx[i + 1, j + 1]);
            return b.Done("braincoral");
        }

        public static Mesh SpireCoral(System.Random r)
        {
            var b = new MB(); var cc = new Color(0.78f, 0.42f, 0.58f);
            void Cone(Vector3 a, Vector3 d, float len, float rad, int depth)
            {
                var up = d.normalized; var s1 = Vector3.Cross(up, Mathf.Abs(up.y) < 0.9f ? Vector3.up : Vector3.right).normalized; var s2 = Vector3.Cross(up, s1);
                int n = 6; int tip = b.V(a + up * len, new Color(cc.r * 1.15f, cc.g * 1.1f, cc.b * 1.1f, 0.1f));
                int prev = -1, first = -1;
                for (int i = 0; i <= n; i++)
                {
                    float ang = i * 6.283f / n;
                    int p = i == n ? first : b.V(a + (s1 * Mathf.Cos(ang) + s2 * Mathf.Sin(ang)) * rad, Var(cc, r, 0.2f, 0));
                    if (first < 0) first = p;
                    if (prev >= 0) b.Tri(tip, prev, p);
                    prev = p;
                }
                if (depth > 0)
                    for (int k = 0; k < 2; k++)
                    {
                        var at = a + up * len * (0.35f + 0.25f * k);
                        var nd = (up + (s1 * ((float)r.NextDouble() - 0.5f) + s2 * ((float)r.NextDouble() - 0.5f)) * 1.4f).normalized;
                        Cone(at, nd, len * 0.55f, rad * 0.6f, depth - 1);
                    }
            }
            for (int k = 0; k < 4; k++)
            {
                float a = k * 1.7f; var base0 = new Vector3(Mathf.Cos(a) * 0.15f, 0, Mathf.Sin(a) * 0.15f);
                Cone(base0, new Vector3(Mathf.Cos(a) * 0.25f, 1, Mathf.Sin(a) * 0.25f), 1.2f + (float)r.NextDouble() * 0.8f, 0.09f, 2);
            }
            return b.Done("spirecoral");
        }

        public static Mesh Fan(System.Random r)
        {
            var b = new MB(); var cc = new Color(0.9f, 0.42f, 0.35f);
            int rays = 14, rings = 4; var idx = new int[rays + 1, rings + 1];
            for (int i = 0; i <= rays; i++)
                for (int k = 0; k <= rings; k++)
                {
                    float a = Mathf.Lerp(0.15f, Mathf.PI - 0.15f, i / (float)rays), f = k / (float)rings;
                    var p = new Vector3(Mathf.Cos(a) * 0.9f * f, Mathf.Sin(a) * 0.9f * f + 0.1f, 0.12f * Mathf.Sin(i * 1.3f) * f);
                    idx[i, k] = b.V(p, Var(cc, r, 0.2f, f));
                }
            for (int i = 0; i < rays; i++) for (int k = 0; k < rings; k++) b.Quad(idx[i, k], idx[i + 1, k], idx[i, k + 1], idx[i + 1, k + 1]);
            return b.Done("fan");
        }
    }
}
