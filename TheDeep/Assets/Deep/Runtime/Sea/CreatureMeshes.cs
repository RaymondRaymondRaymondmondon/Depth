// The creatures' bodies, built in code per species from its body plan (fish, shark, ray, eel, turtle, squid,
// octopus, crab, shrimp, urchin, snail, mammal, jelly, worm, leviathan), one metre long along +z (the head at +z),
// scaled to the animal's size when drawn. Vertex colours carry the pattern (countershading, bars, an accent, dark
// eyes; alpha = how translucent: fins 1, body 0). UV0 drives the swim in Deep/Creature: u = flap weight (wings,
// flippers, legs, the mantle's radius), v = tailness (0 at the nose, 1 at the tip of the tail).
// A Blender pass will replace the leviathans and key species later; these are the readable first bodies.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public static class CreatureMeshes
    {
        // the swim mode Deep/Creature uses for a body plan
        public static int Mode(string kind)
        {
            switch (kind)
            {
                case "ray": case "turtle": return 1;           // flap
                case "eel": case "worm": case "leviathan": return 2;   // undulate
                case "squid": case "jelly": case "octopus": return 3;  // pulse
                case "crab": return 4;                         // crawl
                case "urchin": case "snail": return 5;         // still
                case "mammal": return 6;                       // a fluke beating up and down
            }
            return 0;                                          // fish: the tail sweeps side to side
        }

        class MB
        {
            public readonly List<Vector3> v = new List<Vector3>(); public readonly List<Color> c = new List<Color>();
            public readonly List<Vector2> uv = new List<Vector2>(); public readonly List<int> t = new List<int>();
            public int V(Vector3 p, Color col, float flap, float tail) { v.Add(p); c.Add(col); uv.Add(new Vector2(flap, tail)); return v.Count - 1; }
            public void Tri(int a, int b, int d) { t.Add(a); t.Add(b); t.Add(d); }
            public void Quad(int a, int b, int d, int e) { Tri(a, b, d); Tri(a, d, e); }
            public Mesh Build(string name)
            {
                var m = new Mesh { name = name };
                m.SetVertices(v); m.SetColors(c); m.SetUVs(0, uv); m.SetTriangles(t, 0);
                m.RecalculateNormals(); m.RecalculateBounds();
                return m;
            }
        }

        static float H(string s, int k) { unchecked { int h = 7 + k * 131; foreach (char ch in s) h = h * 37 + ch; return (h & 0xffff) / 65535f; } }

        // a body of revolution along z from z0 (tail) to z1 (nose): radius(t) for t in 0..1 (0 the tail end), the
        // cross-section an ellipse wr wide and hr high (times the radius); paint(t, angle) gives each vertex's colour
        static void Lathe(MB b, float z0, float z1, int rings, int segs, System.Func<float, float> radius, float wr, float hr,
                          System.Func<float, float, Color> paint, float yOff = 0, float flap = 0)
        {
            int start = b.v.Count;
            for (int i = 0; i <= rings; i++)
            {
                float t = i / (float)rings, z = Mathf.Lerp(z0, z1, t), r = radius(t);
                for (int k = 0; k <= segs; k++)
                {
                    float a = k / (float)segs * Mathf.PI * 2f;
                    var p = new Vector3(Mathf.Sin(a) * r * wr, Mathf.Cos(a) * r * hr + yOff, z);
                    b.V(p, paint(t, a), flap, Mathf.Clamp01(0.5f - z));
                }
            }
            for (int i = 0; i < rings; i++)
                for (int k = 0; k < segs; k++)
                {
                    int a = start + i * (segs + 1) + k, d = a + segs + 1;
                    b.Quad(a, d, d + 1, a + 1);
                }
        }

        // a flat fin through the given points (a fan from the first), translucent
        static void Fin(MB b, Color col, float flap, params Vector3[] pts)
        {
            col.a = 1;
            int s = b.v.Count;
            foreach (var p in pts) b.V(p, col, flap, Mathf.Clamp01(0.5f - p.z));
            for (int i = 1; i < pts.Length - 1; i++) b.Tri(s, s + i, s + i + 1);
        }

        static Color Body(SpeciesDef sp, float t, float a, float bars, float eyeT)
        {
            float up = Mathf.Cos(a);                                // +1 on the back, -1 on the belly
            var c = Color.Lerp(sp.belly, sp.top, Mathf.SmoothStep(0, 1, up * 0.5f + 0.6f));
            if (bars > 0 && Mathf.Repeat(t * bars, 1f) < 0.32f && up > -0.3f) c = Color.Lerp(c, sp.accent, 0.75f);
            if (eyeT > 0 && Mathf.Abs(t - eyeT) < 0.035f && Mathf.Abs(Mathf.Abs(Mathf.Sin(a)) - 0.92f) < 0.12f && up > -0.1f) c = new Color(0.02f, 0.02f, 0.02f);
            c.a = 0;
            return c;
        }

        public static Mesh For(SpeciesDef sp)
        {
            var b = new MB();
            string n = sp.e.name;
            float bars = H(n, 4) < 0.45f ? Mathf.Round(3 + H(n, 5) * 6) : 0;
            switch (sp.kind)
            {
                case "shark": Shark(b, sp); break;
                case "ray": Ray(b, sp); break;
                case "eel": case "worm": Eel(b, sp, sp.kind == "worm" ? 0.035f : 0.05f, bars); break;
                case "leviathan": Leviathan(b, sp); break;
                case "turtle": Turtle(b, sp); break;
                case "squid": Squid(b, sp, false); break;
                case "octopus": Squid(b, sp, true); break;
                case "jelly": Jelly(b, sp); break;
                case "crab": Crab(b, sp); break;
                case "urchin": Urchin(b, sp); break;
                case "snail": Snail(b, sp); break;
                case "mammal": Mammal(b, sp); break;
                default: Fish(b, sp, bars, sp.kind == "shrimp"); break;
            }
            return b.Build(sp.e.id ?? n);
        }

        static void Fish(MB b, SpeciesDef sp, float bars, bool shrimp)
        {
            string n = sp.e.name;
            float hr = 0.28f + 0.3f * H(n, 6), wr = 0.12f + 0.08f * H(n, 7);
            System.Func<float, float> r = t => t < 0.12f ? Mathf.Lerp(0.05f, 0.16f, t / 0.12f) : 0.5f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.18f, 1f, (t - 0.12f) / 0.88f)), 0.7f) + 0.004f;
            Lathe(b, -0.42f, 0.5f, 18, 14, r, wr * 2f, hr * 2f, (t, a) => Body(sp, t, a, bars, 0.86f));
            var fin = Color.Lerp(sp.accent, sp.top, 0.4f);
            float fork = 0.12f + 0.1f * H(n, 8);
            Fin(b, fin, 0, new Vector3(0, 0, -0.4f), new Vector3(0, hr * 0.9f, -0.58f - fork * 0.3f), new Vector3(0, hr * 0.25f, -0.5f), new Vector3(0, -hr * 0.25f, -0.5f), new Vector3(0, -hr * 0.9f, -0.58f - fork * 0.3f));
            Fin(b, fin, 0, new Vector3(0, hr * 0.85f, 0.15f), new Vector3(0, hr * (1.2f + H(n, 9)), -0.02f), new Vector3(0, hr * 0.7f, -0.2f));
            Fin(b, fin, 0, new Vector3(0, -hr * 0.8f, -0.05f), new Vector3(0, -hr * 1.1f, -0.15f), new Vector3(0, -hr * 0.6f, -0.25f));
            for (int s = -1; s <= 1; s += 2)
                Fin(b, fin, 0.3f, new Vector3(s * wr * 0.9f, -hr * 0.2f, 0.18f), new Vector3(s * wr * 2.2f, -hr * 0.5f, 0.02f), new Vector3(s * wr, -hr * 0.3f, 0.05f));
            if (shrimp)
                for (int s = -1; s <= 1; s += 2)
                    Fin(b, sp.accent, 0, new Vector3(s * 0.02f, hr * 0.2f, 0.5f), new Vector3(s * 0.12f, hr * 0.6f, 1.1f), new Vector3(s * 0.03f, hr * 0.22f, 0.52f));
        }

        static void Shark(MB b, SpeciesDef sp)
        {
            System.Func<float, float> r = t => t < 0.15f ? Mathf.Lerp(0.03f, 0.08f, t / 0.15f) : 0.5f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.2f, 0.98f, (t - 0.15f) / 0.85f)), 0.6f);
            Lathe(b, -0.45f, 0.5f, 20, 14, r, 0.38f, 0.36f, (t, a) => Body(sp, t, a, 0, 0.88f));
            var fin = sp.top * 0.9f;
            Fin(b, fin, 0, new Vector3(0, 0.12f, 0.12f), new Vector3(0, 0.36f, -0.06f), new Vector3(0, 0.12f, -0.1f));      // the dorsal
            Fin(b, fin, 0, new Vector3(0, 0.02f, -0.42f), new Vector3(0, 0.3f, -0.6f), new Vector3(0, 0.05f, -0.48f), new Vector3(0, -0.16f, -0.55f));   // the tail
            for (int s = -1; s <= 1; s += 2)
                Fin(b, fin, 0.4f, new Vector3(s * 0.08f, -0.05f, 0.2f), new Vector3(s * 0.34f, -0.14f, 0.0f), new Vector3(s * 0.1f, -0.07f, 0.08f));
        }

        static void Ray(MB b, SpeciesDef sp)
        {
            // a diamond wing: rows across the span, thin at the tips; the tail a whip behind
            int R = 10, C = 16;
            int s0 = b.v.Count;
            for (int i = 0; i <= R; i++)
                for (int k = 0; k <= C; k++)
                {
                    float z = Mathf.Lerp(-0.25f, 0.45f, i / (float)R), u = Mathf.Lerp(-1, 1, k / (float)C);
                    float half = 0.62f * (1f - Mathf.Abs((z - 0.12f) / 0.4f)) ;
                    half = Mathf.Max(0.04f, half);
                    float x = u * half, y = 0.05f * (1 - u * u) * (1 - Mathf.Abs(z));
                    var col = Body(sp, (z + 0.5f), 0.2f, 0, 0);
                    if (Mathf.Abs(u) < 0.15f && Mathf.Abs(z - 0.3f) < 0.03f) col = Color.black;
                    b.V(new Vector3(x, y, z), col, Mathf.Abs(u), 0.3f);
                }
            for (int i = 0; i < R; i++)
                for (int k = 0; k < C; k++)
                {
                    int a = s0 + i * (C + 1) + k, d = a + C + 1;
                    b.Quad(a, d, d + 1, a + 1);
                }
            Lathe(b, -0.75f, -0.24f, 6, 5, t => 0.02f * (0.3f + t), 1, 1, (t, a) => Body(sp, t, a, 0, 0));
        }

        static void Eel(MB b, SpeciesDef sp, float rad, float bars)
        {
            System.Func<float, float> r = t => rad * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.06f, 0.94f, t)), 0.35f);
            Lathe(b, -0.5f, 0.5f, 40, 10, r, 1f, 1.15f, (t, a) => Body(sp, t, a, bars * 2, 0.95f));
            Fin(b, sp.accent * 0.8f, 0, new Vector3(0, rad, 0.3f), new Vector3(0, rad * 2.2f, -0.1f), new Vector3(0, rad * 1.6f, -0.45f), new Vector3(0, 0, -0.5f));
        }

        static void Leviathan(MB b, SpeciesDef sp)
        {
            // a long armoured body, a heavy head, plates along the spine, three pairs of fins
            System.Func<float, float> r = t => 0.075f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.05f, 0.9f, t)), 0.45f) * (t > 0.82f ? 1.25f : 1f);
            Lathe(b, -0.5f, 0.5f, 48, 14, r, 1.1f, 1f, (t, a) => Body(sp, t, a, 9, 0.92f));
            for (int i = 0; i < 14; i++)
            {
                float z = Mathf.Lerp(-0.35f, 0.38f, i / 13f);
                Fin(b, sp.accent * 0.6f, 0, new Vector3(0, 0.06f, z + 0.03f), new Vector3(0, 0.11f, z - 0.01f), new Vector3(0, 0.06f, z - 0.03f));
            }
            for (int p = 0; p < 3; p++)
                for (int s = -1; s <= 1; s += 2)
                {
                    float z = 0.25f - p * 0.25f;
                    Fin(b, sp.top * 0.8f, 0.5f, new Vector3(s * 0.06f, -0.02f, z + 0.04f), new Vector3(s * 0.2f, -0.06f, z - 0.05f), new Vector3(s * 0.07f, -0.03f, z - 0.04f));
                }
            Fin(b, sp.top * 0.8f, 0, new Vector3(0, 0, -0.48f), new Vector3(0, 0.12f, -0.62f), new Vector3(0, -0.12f, -0.62f));
        }

        static void Turtle(MB b, SpeciesDef sp)
        {
            System.Func<float, float> r = t => 0.4f * Mathf.Pow(Mathf.Sin(Mathf.PI * t), 0.5f);
            Lathe(b, -0.35f, 0.35f, 12, 16, r, 1.0f, 0.42f, (t, a) => { var c = Color.Lerp(sp.belly, sp.top, Mathf.Cos(a) * 0.5f + 0.5f); if (Mathf.Repeat(t * 4 + a, 1f) < 0.08f) c *= 0.6f; c.a = 0; return c; }, 0.02f);
            Lathe(b, 0.3f, 0.5f, 5, 8, t => 0.09f * Mathf.Sin(Mathf.PI * Mathf.Lerp(0.3f, 1f, t)), 1, 0.9f, (t, a) => Body(sp, t, a, 0, 0.7f), 0.02f);
            var skin = Color.Lerp(sp.belly, sp.top, 0.4f);
            for (int s = -1; s <= 1; s += 2)
            {
                Fin(b, skin, 1f, new Vector3(s * 0.3f, 0, 0.18f), new Vector3(s * 0.85f, -0.05f, -0.05f), new Vector3(s * 0.32f, 0, 0.04f));
                Fin(b, skin, 0.5f, new Vector3(s * 0.25f, 0, -0.25f), new Vector3(s * 0.45f, -0.02f, -0.42f), new Vector3(s * 0.2f, 0, -0.32f));
            }
        }

        static void Squid(MB b, SpeciesDef sp, bool octopus)
        {
            if (octopus)
            {
                Lathe(b, -0.05f, 0.4f, 10, 12, t => 0.25f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.15f, 1f, t)), 0.6f), 1, 1, (t, a) => Body(sp, t, a, 0, 0.25f), 0.12f, 1f);
                for (int k = 0; k < 8; k++)
                {
                    float a = k / 8f * Mathf.PI * 2;
                    var d = new Vector3(Mathf.Cos(a), 0, Mathf.Sin(a));
                    Fin(b, sp.top, 0.6f, new Vector3(0, 0.02f, 0) + d * 0.08f, d * 0.55f + new Vector3(0, -0.08f, 0) + Vector3.Cross(d, Vector3.up) * 0.03f, d * 0.55f + new Vector3(0, -0.06f, 0) - Vector3.Cross(d, Vector3.up) * 0.03f);
                }
                return;
            }
            Lathe(b, -0.1f, 0.5f, 14, 12, t => 0.12f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.25f, 1f, t)), 0.5f), 1, 1, (t, a) => Body(sp, t, a, 0, 0.12f), 0, 1f);
            for (int s = -1; s <= 1; s += 2) Fin(b, sp.accent, 0.4f, new Vector3(0, 0, 0.42f), new Vector3(s * 0.2f, 0, 0.3f), new Vector3(0, 0, 0.18f));
            for (int k = 0; k < 8; k++)
            {
                float a = k / 8f * Mathf.PI * 2;
                var d = new Vector3(Mathf.Cos(a), Mathf.Sin(a), 0) * 0.05f;
                Fin(b, sp.top, 0.2f, d + new Vector3(0, 0, -0.08f), d * 1.6f + new Vector3(0.01f, 0, -0.5f), d + new Vector3(-0.01f, 0, -0.1f));
            }
        }

        static void Jelly(MB b, SpeciesDef sp)
        {
            var col = sp.accent;
            Lathe(b, -0.15f, 0.15f, 8, 16, t => 0.4f * Mathf.Sqrt(Mathf.Max(0.01f, t)), 1, 1, (t, a) => { var c = col; c.a = 1; return c; }, 0, 1f);
            for (int k = 0; k < 10; k++)
            {
                float a = k / 10f * Mathf.PI * 2;
                var d = new Vector3(Mathf.Cos(a), Mathf.Sin(a), 0) * 0.3f;
                Fin(b, col, 0.3f, d + new Vector3(0, 0, -0.14f), d * 0.8f + new Vector3(0.02f, 0, -0.9f), d + new Vector3(-0.02f, 0, -0.15f));
            }
        }

        static void Crab(MB b, SpeciesDef sp)
        {
            Lathe(b, -0.3f, 0.3f, 8, 14, t => 0.5f * Mathf.Pow(Mathf.Sin(Mathf.PI * t), 0.5f), 1.2f, 0.35f, (t, a) => Body(sp, t, a, 0, 0.9f), 0.12f);
            for (int s = -1; s <= 1; s += 2)
            {
                for (int k = 0; k < 4; k++)
                {
                    float z = 0.15f - k * 0.12f;
                    Fin(b, sp.top * 0.85f, 1f, new Vector3(s * 0.35f, 0.12f, z), new Vector3(s * 0.75f, -0.05f, z - 0.05f), new Vector3(s * 0.36f, 0.1f, z - 0.04f));
                }
                Fin(b, sp.accent, 0.5f, new Vector3(s * 0.25f, 0.14f, 0.25f), new Vector3(s * 0.4f, 0.16f, 0.62f), new Vector3(s * 0.15f, 0.14f, 0.55f), new Vector3(s * 0.2f, 0.13f, 0.3f));
            }
        }

        static void Urchin(MB b, SpeciesDef sp)
        {
            Lathe(b, -0.25f, 0.25f, 8, 12, t => 0.5f * Mathf.Sin(Mathf.PI * t), 1, 0.8f, (t, a) => Body(sp, t, a, 0, 0), 0.2f);
            for (int k = 0; k < 40; k++)
            {
                var d = Quaternion.Euler(H(sp.e.name + k, 1) * 160 - 80, H(sp.e.name + k, 2) * 360, 0) * Vector3.up;
                if (d.y < -0.2f) d.y = -d.y;
                var p0 = d * 0.2f + Vector3.up * 0.2f; var perp = Vector3.Cross(d, Vector3.right).normalized * 0.015f;
                Fin(b, sp.top * 0.6f, 0, p0 + perp, p0 + d * 0.35f, p0 - perp);
            }
        }

        static void Snail(MB b, SpeciesDef sp)
        {
            Lathe(b, -0.4f, 0.4f, 6, 10, t => 0.18f, 1, 0.3f, (t, a) => Body(sp, t, a, 0, 0.95f), 0.05f);
            Lathe(b, -0.3f, 0.2f, 12, 14, t => 0.32f * Mathf.Pow(Mathf.Sin(Mathf.PI * t), 0.6f) * (1 - t * 0.4f), 1, 1.1f, (t, a) => { var c = Color.Lerp(sp.accent, sp.top, Mathf.Repeat(t * 5 + a * 0.3f, 1f)); c.a = 0; return c; }, 0.3f);
        }

        static void Mammal(MB b, SpeciesDef sp)
        {
            System.Func<float, float> r = t => t < 0.12f ? Mathf.Lerp(0.06f, 0.14f, t / 0.12f) : 0.5f * Mathf.Pow(Mathf.Sin(Mathf.PI * Mathf.Lerp(0.15f, 0.97f, (t - 0.12f) / 0.88f)), 0.5f);
            Lathe(b, -0.42f, 0.5f, 18, 14, r, 0.62f, 0.55f, (t, a) => Body(sp, t, a, 0, 0.9f));
            Fin(b, sp.top, 0, new Vector3(0, 0, -0.42f), new Vector3(0.25f, 0, -0.6f), new Vector3(-0.25f, 0, -0.6f));
            for (int s = -1; s <= 1; s += 2) Fin(b, sp.top, 0.6f, new Vector3(s * 0.25f, -0.08f, 0.22f), new Vector3(s * 0.45f, -0.18f, 0.08f), new Vector3(s * 0.26f, -0.1f, 0.1f));
        }
    }
}
