// The Bioluminescent Caverns (stage 9; the design doc's biome 3, 150 to 300 m): "limestone tunnel networks and huge
// cathedral caverns ... tight squeezes need the Kite-Sub or swimming. Upper chambers hold air pockets with bat roosts.
// The Abandoned Underwater City lies here." Built at start-up from the world's seed, in the rock under the far end of
// the Kelp Labyrinth and the drop-off beyond it:
//   - the layout: cathedral chambers (one of them the drowned city's, the biggest), two upper chambers holding air
//     pockets, winding tunnels joining them (a spanning tree and a couple of loops), narrow squeezes, a sinkhole down
//     through the Kelp's floor and mouths opening on the drop-off's slope;
//   - a signed distance field over the region (negative in the water of the caves; tunnels blended smoothly into the
//     chambers, roughened with noise into limestone), on a 2.5 m grid kept for the sea life (walls to steer from);
//   - the walls: meshed by marching tetrahedra in chunks (each with a MeshCollider), vertex-coloured limestone (strata,
//     wet streaks, silt on the floors); the terrain gets holes where the caves break the surface;
//   - the air pockets' water: a sheet at each pocket's level (above it you breathe).
// Everything else that lives here (plants, animals, deposits, the city's ruins, the glow lighting the walls) is placed
// on these walls and in this water (CaveFlora / Life / Deposits / CaveCity).
using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;

namespace Deep
{
    public partial class Caverns : MonoBehaviour
    {
        public static Caverns I;
        // the region (world metres) and its grid
        public const float X0 = 930f, X1 = 1470f, Z0 = 420f, Z1 = 1120f, Y0 = -310f, Y1 = -70f, Cell = 2.5f;
        public int nx, ny, nz;
        public float[] sdf;                                 // at the grid's points; < 0 in the caves' water (or air)
        Seabed bed;

        public class Chamber { public string name; public Vector3 c, r; public bool air, city; public float level; public Transform water; }
        public readonly List<Chamber> chambers = new List<Chamber>();
        struct Seg { public Vector3 a, b; public float ra, rb; }
        readonly List<Seg> segs = new List<Seg>();
        public readonly List<Vector3> mouths = new List<Vector3>();     // where the caves open to the sea
        public readonly List<(Vector3 p, Vector3 n)> surface = new List<(Vector3, Vector3)>();   // points on the walls (for planting)
        public int Triangles;

        // ---- the layout ---------------------------------------------------------------------------------------------
        System.Random rnd;
        float R() => (float)rnd.NextDouble();
        float RR(float a, float b) => a + (b - a) * R();

        float Ground(float x, float z) => bed.HeightAt(x, z);

        void Layout(int seed)
        {
            rnd = new System.Random(seed * 977 + 41);
            // the city's cathedral first (the biggest), then the others, all below the ground with rock to spare
            bool Fits(Vector3 c, Vector3 r)
            {
                if (c.x - r.x < X0 + 12 || c.x + r.x > X1 - 12 || c.z - r.z < Z0 + 12 || c.z + r.z > Z1 - 12 || c.y - r.y < Y0 + 8) return false;
                for (int k = -1; k <= 1; k++) for (int m = -1; m <= 1; m++)
                    if (c.y + r.y > Ground(c.x + k * r.x, c.z + m * r.z) - 10f) return false;
                foreach (var o in chambers) if ((new Vector3((o.c.x - c.x) / (o.r.x + r.x), (o.c.y - c.y) / (o.r.y + r.y), (o.c.z - c.z) / (o.r.z + r.z))).magnitude < 1.15f) return false;
                return true;
            }
            void Place(string name, Vector3 rad, float yLo, float yHi, bool city = false, bool air = false)
            {
                for (int k = 0; k < 400; k++)
                {
                    var c = new Vector3(RR(X0 + 40, X1 - 120), RR(yLo, yHi), RR(Z0 + 40, Z1 - 40));
                    if (air) { float g = Ground(c.x, c.z); c.y = g - 12f - rad.y; }
                    if (!Fits(c, rad)) continue;
                    var ch = new Chamber { name = name, c = c, r = rad, city = city, air = air };
                    if (air) ch.level = c.y + rad.y * 0.35f;
                    chambers.Add(ch);
                    return;
                }
            }
            Place("The Drowned City", new Vector3(52, 24, 44), -270, -215, city: true);
            Place("The Cathedral", new Vector3(34, 26, 30), -260, -190);
            Place("The Lantern Hall", new Vector3(28, 18, 26), -250, -180);
            Place("The Spire Gallery", new Vector3(24, 20, 22), -280, -200);
            Place("The Pale Chamber", new Vector3(22, 14, 24), -240, -175);
            Place("The Bat Roost", new Vector3(18, 14, 18), 0, 0, air: true);
            Place("The Whispering Roost", new Vector3(16, 12, 16), 0, 0, air: true);
            // tunnels: a spanning tree by distance, then two loops
            int n = chambers.Count;
            var inTree = new bool[n]; inTree[0] = true;
            var edges = new List<(int a, int b)>();
            for (int step = 1; step < n; step++)
            {
                float best = float.MaxValue; int ba = -1, bb = -1;
                for (int a = 0; a < n; a++) if (inTree[a]) for (int b = 0; b < n; b++) if (!inTree[b]) { float d = (chambers[a].c - chambers[b].c).magnitude; if (d < best) { best = d; ba = a; bb = b; } }
                if (bb < 0) break;
                inTree[bb] = true; edges.Add((ba, bb));
            }
            for (int k = 0; k < 2; k++) { int a = rnd.Next(n), b = rnd.Next(n); if (a != b) edges.Add((a, b)); }
            foreach (var (a, b) in edges) Tunnel(chambers[a].c, chambers[b].c, RR(4f, 6f), 4);
            // squeezes: narrow side passages (the Kite-Sub's, or a swimmer's)
            for (int k = 0; k < 3; k++)
            {
                var a = chambers[rnd.Next(n)];
                var dir = Quaternion.Euler(RR(-15, 15), RR(0, 360), 0) * Vector3.forward;
                var end = a.c + dir * RR(45, 80); end.y = Mathf.Min(end.y, Ground(end.x, end.z) - 10f);
                end.x = Mathf.Clamp(end.x, X0 + 20, X1 - 20); end.z = Mathf.Clamp(end.z, Z0 + 20, Z1 - 20);
                Tunnel(a.c + dir * a.r.x * 0.8f, end, RR(2.3f, 2.8f), 3, true);
            }
            // the sinkhole: from the chamber nearest the Kelp's end straight up through its floor
            Chamber sink = null; foreach (var ch in chambers) if (!ch.air && !ch.city && ch.c.x < 1120f && (sink == null || ch.c.y + ch.r.y > sink.c.y + sink.r.y)) sink = ch;
            if (sink == null) sink = chambers[1];
            {
                var p = sink.c + Vector3.up * sink.r.y * 0.7f;
                var pts = new List<Vector3> { p };
                while (p.y < Ground(p.x, p.z) + 10f) { p += new Vector3(RR(-2, 2), 6f, RR(-2, 2)); pts.Add(p); }
                for (int i = 0; i + 1 < pts.Count; i++) segs.Add(new Seg { a = pts[i], b = pts[i + 1], ra = 6.5f, rb = 6.5f });
                mouths.Add(new Vector3(p.x, Ground(p.x, p.z), p.z));
            }
            // mouths on the drop-off: from the two chambers furthest out, heading out (+x) until clear of the slope
            var byX = new List<Chamber>(chambers); byX.Sort((a, b) => b.c.x.CompareTo(a.c.x));
            for (int k = 0; k < 2 && k < byX.Count; k++)
            {
                var ch = byX[k];
                var p = ch.c + Vector3.right * ch.r.x * 0.7f;
                var pts = new List<Vector3> { p };
                for (int i = 0; i < 60 && p.y < Ground(p.x, p.z) + 6f && p.x < X1 - 8; i++) { p += new Vector3(6f, RR(-1.2f, 0.8f), RR(-2.5f, 2.5f)); pts.Add(p); }
                for (int i = 0; i < 2; i++) { p += new Vector3(5f, 0, 0); pts.Add(p); }
                for (int i = 0; i + 1 < pts.Count; i++) segs.Add(new Seg { a = pts[i], b = pts[i + 1], ra = 5.5f, rb = 5.5f });
                mouths.Add(p);
            }
        }

        // a winding tunnel from a to b (control points wandering off the line, kept under the ground)
        void Tunnel(Vector3 a, Vector3 b, float radius, int wiggles, bool squeeze = false)
        {
            var ctrl = new List<Vector3> { a, a };
            var d = b - a; var side = Vector3.Cross(d, Vector3.up).normalized;
            for (int i = 1; i <= wiggles; i++)
            {
                var p = a + d * (i / (wiggles + 1f)) + side * RR(-18, 18) + Vector3.up * RR(-8, 8);
                p.y = Mathf.Clamp(p.y, Y0 + 10f, Ground(p.x, p.z) - 9f);
                ctrl.Add(p);
            }
            ctrl.Add(b); ctrl.Add(b);
            Vector3 last = a; float lastR = radius;
            for (int i = 1; i + 2 < ctrl.Count; i++)
                for (int k = 1; k <= 6; k++)
                {
                    float t = k / 6f;
                    var p = CatmullRom(ctrl[i - 1], ctrl[i], ctrl[i + 1], ctrl[i + 2], t);
                    float r = squeeze ? radius : radius * (0.75f + 0.5f * VNoise.N2(p.x * 0.05f, p.z * 0.05f));
                    segs.Add(new Seg { a = last, b = p, ra = lastR, rb = r });
                    last = p; lastR = r;
                }
        }
        static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
        {
            float t2 = t * t, t3 = t2 * t;
            return 0.5f * (2f * p1 + (-p0 + p2) * t + (2f * p0 - 5f * p1 + 4f * p2 - p3) * t2 + (-p0 + 3f * p1 - 3f * p2 + p3) * t3);
        }

        // ---- the distance field ---------------------------------------------------------------------------------------
        static float SMin(float a, float b, float k) { float h = Mathf.Clamp01(0.5f + 0.5f * (b - a) / k); return Mathf.Lerp(b, a, h) - k * h * (1 - h); }
        static float Noise3(Vector3 p)
        {
            // cheap 3D value noise from three Perlin planes
            return VNoise.N3(p.x, p.y, p.z);
        }

        // the analytic field at a point (used to fill the grid)
        float Field(Vector3 p, List<int> nearC, List<int> nearS)
        {
            float d = 50f;
            foreach (int i in nearC)
            {
                var c = chambers[i];
                var q = new Vector3((p.x - c.c.x) / c.r.x, (p.y - c.c.y) / c.r.y, (p.z - c.c.z) / c.r.z);
                float k = q.magnitude;
                float e = (k - 1f) * Mathf.Min(c.r.x, Mathf.Min(c.r.y, c.r.z));
                // flatter floors (silt settles), ragged ceilings
                if (p.y < c.c.y - c.r.y * 0.55f) e = Mathf.Max(e, (c.c.y - c.r.y * 0.62f) - p.y);
                d = SMin(d, e, 6f);
            }
            foreach (int i in nearS)
            {
                var s = segs[i];
                var ab = s.b - s.a; float t = Mathf.Clamp01(Vector3.Dot(p - s.a, ab) / Mathf.Max(1e-4f, ab.sqrMagnitude));
                float e = (p - (s.a + ab * t)).magnitude - Mathf.Lerp(s.ra, s.rb, t);
                d = SMin(d, e, 3f);
            }
            // limestone: lumps and flutes, stalactite-ish ripples on the ceilings
            d += (Noise3(p * 0.11f) - 0.5f) * 3.2f + (Noise3(p * 0.37f + Vector3.one * 9f) - 0.5f) * 1.1f;
            return d;
        }

        void FillGrid()
        {
            nx = Mathf.CeilToInt((X1 - X0) / Cell) + 1; ny = Mathf.CeilToInt((Y1 - Y0) / Cell) + 1; nz = Mathf.CeilToInt((Z1 - Z0) / Cell) + 1;
            sdf = new float[nx * ny * nz];
            // a coarse index: which shapes come near each 20 m block
            const float B = 20f;
            int bx = Mathf.CeilToInt((X1 - X0) / B), by = Mathf.CeilToInt((Y1 - Y0) / B), bz = Mathf.CeilToInt((Z1 - Z0) / B);
            var blockC = new List<int>[bx * by * bz]; var blockS = new List<int>[bx * by * bz];
            for (int i = 0; i < blockC.Length; i++) { blockC[i] = new List<int>(); blockS[i] = new List<int>(); }
            void Index(Vector3 lo, Vector3 hi, int id, bool chamber)
            {
                int ax = Mathf.Clamp((int)((lo.x - X0) / B), 0, bx - 1), ay = Mathf.Clamp((int)((lo.y - Y0) / B), 0, by - 1), az = Mathf.Clamp((int)((lo.z - Z0) / B), 0, bz - 1);
                int cx = Mathf.Clamp((int)((hi.x - X0) / B), 0, bx - 1), cy = Mathf.Clamp((int)((hi.y - Y0) / B), 0, by - 1), cz = Mathf.Clamp((int)((hi.z - Z0) / B), 0, bz - 1);
                for (int z = az; z <= cz; z++) for (int y = ay; y <= cy; y++) for (int x = ax; x <= cx; x++) (chamber ? blockC : blockS)[(z * by + y) * bx + x].Add(id);
            }
            var pad = Vector3.one * 12f;
            for (int i = 0; i < chambers.Count; i++) Index(chambers[i].c - chambers[i].r - pad, chambers[i].c + chambers[i].r + pad, i, true);
            for (int i = 0; i < segs.Count; i++) { var s = segs[i]; float r = Mathf.Max(s.ra, s.rb); Index(Vector3.Min(s.a, s.b) - pad - Vector3.one * r, Vector3.Max(s.a, s.b) + pad + Vector3.one * r, i, false); }
            Parallel.For(0, nz, k =>
            {
                for (int j = 0; j < ny; j++)
                    for (int i = 0; i < nx; i++)
                    {
                        var p = new Vector3(X0 + i * Cell, Y0 + j * Cell, Z0 + k * Cell);
                        int b = (Mathf.Min((int)((p.z - Z0) / B), bz - 1) * by + Mathf.Min((int)((p.y - Y0) / B), by - 1)) * bx + Mathf.Min((int)((p.x - X0) / B), bx - 1);
                        sdf[(k * ny + j) * nx + i] = blockC[b].Count + blockS[b].Count == 0 ? 50f : Field(p, blockC[b], blockS[b]);
                    }
            });
            // the region's own edges are rock
            for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++)
                if (i == 0 || j == 0 || k == 0 || i == nx - 1 || j == ny - 1 || k == nz - 1) { int id = (k * ny + j) * nx + i; sdf[id] = Mathf.Max(sdf[id], 1f); }
        }

        // ---- queries (the sea life, the Kite-Sub, the diver's air) -------------------------------------------------------
        public bool InRegion(Vector3 p) => p.x > X0 + Cell && p.x < X1 - Cell && p.y > Y0 + Cell && p.y < Y1 - Cell && p.z > Z0 + Cell && p.z < Z1 - Cell;
        float G(int i, int j, int k) => sdf[(k * ny + j) * nx + i];
        public float Sdf(Vector3 p)
        {
            if (!InRegion(p)) return 50f;
            float fx = (p.x - X0) / Cell, fy = (p.y - Y0) / Cell, fz = (p.z - Z0) / Cell;
            int i = (int)fx, j = (int)fy, k = (int)fz; float u = fx - i, v = fy - j, w = fz - k;
            float a = Mathf.Lerp(G(i, j, k), G(i + 1, j, k), u), b = Mathf.Lerp(G(i, j + 1, k), G(i + 1, j + 1, k), u);
            float c = Mathf.Lerp(G(i, j, k + 1), G(i + 1, j, k + 1), u), d = Mathf.Lerp(G(i, j + 1, k + 1), G(i + 1, j + 1, k + 1), u);
            return Mathf.Lerp(Mathf.Lerp(a, b, v), Mathf.Lerp(c, d, v), w);
        }
        public Vector3 Grad(Vector3 p)
        {
            const float e = 1.25f;
            return new Vector3(Sdf(p + Vector3.right * e) - Sdf(p - Vector3.right * e), Sdf(p + Vector3.up * e) - Sdf(p - Vector3.up * e), Sdf(p + Vector3.forward * e) - Sdf(p - Vector3.forward * e)) / (2 * e);
        }
        // in the caves' water (or air): inside the field's hollow and below the ground (or in a mouth)
        public bool InCave(Vector3 p) => Sdf(p) < 0f;
        // in the zone where the caves are (under the ground in the region): a creature here steers by the field
        public bool UnderGround(Vector3 p) => InRegion(p) && p.y < Ground(p.x, p.z) - 0.5f;

        // the air pocket a point is in (above its level, inside the chamber), or null
        public Chamber PocketAt(Vector3 p)
        {
            foreach (var c in chambers)
            {
                if (!c.air || p.y < c.level) continue;
                var q = new Vector3((p.x - c.c.x) / (c.r.x + 3), (p.y - c.c.y) / (c.r.y + 3), (p.z - c.c.z) / (c.r.z + 3));
                if (q.sqrMagnitude < 1f) return c;
            }
            return null;
        }
        // the water's surface over a point: an air pocket's level if it's under one, else none (+inf)
        public float PocketSurface(Vector3 p)
        {
            foreach (var c in chambers)
            {
                if (!c.air) continue;
                var q = new Vector2((p.x - c.c.x) / (c.r.x + 2), (p.z - c.c.z) / (c.r.z + 2));
                if (q.sqrMagnitude < 1f && p.y > c.c.y - c.r.y - 2 && p.y < c.c.y + c.r.y + 4) return c.level;
            }
            return float.PositiveInfinity;
        }

        // a free point in the caves' water near a point (for spawning): at least `clear` from any wall
        public bool FreePoint(Vector3 around, float rMin, float rMax, float clear, System.Random r, out Vector3 at)
        {
            for (int k = 0; k < 24; k++)
            {
                var dir = new Vector3((float)r.NextDouble() * 2 - 1, ((float)r.NextDouble() * 2 - 1) * 0.6f, (float)r.NextDouble() * 2 - 1);
                if (dir.sqrMagnitude < 0.01f) continue;
                var p = around + dir.normalized * (rMin + (float)r.NextDouble() * (rMax - rMin));
                if (!InRegion(p) || Sdf(p) > -clear || PocketAt(p) != null) continue;
                at = p; return true;
            }
            at = default; return false;
        }
        // the cave floor (or ceiling) straight below (above) a point in the caves, marching the field
        public bool Surface(Vector3 p, float dir, float maxDist, out Vector3 hit)
        {
            for (float t = 0; t < maxDist; t += 1f)
            {
                var q = p + Vector3.up * dir * t;
                if (Sdf(q) > 0f) { hit = q - Vector3.up * dir * 0.5f; return true; }
            }
            hit = p; return false;
        }
        // a straight swim from a to b stays in the water
        public bool Clear(Vector3 a, Vector3 b, float margin)
        {
            float len = (b - a).magnitude; if (len < 0.01f) return true;
            for (float t = 0; t <= len; t += 2f) if (Sdf(Vector3.Lerp(a, b, t / len)) > -margin) return false;
            return true;
        }

        // ---- the walls ----------------------------------------------------------------------------------------------
        static readonly int[,] Tets = { { 0, 1, 3, 7 }, { 0, 3, 2, 7 }, { 0, 2, 6, 7 }, { 0, 6, 4, 7 }, { 0, 4, 5, 7 }, { 0, 5, 1, 7 } };
        Material wallMat;

        void BuildWalls()
        {
            wallMat = new Material(DeepShaders.Get("Deep/Lit")) { name = "limestone" };
            wallMat.SetFloat("_UseVC", 1); wallMat.SetFloat("_VCAlbedo", 1); wallMat.SetFloat("_Roughness", 0.75f); wallMat.SetFloat("_Metallic", 0f); wallMat.SetFloat("_Cull", 2);
            const int C = 20;   // cells per chunk side
            var jobs = new List<(int i0, int j0, int k0)>();
            for (int k = 0; k < nz - 1; k += C) for (int j = 0; j < ny - 1; j += C) for (int i = 0; i < nx - 1; i += C) jobs.Add((i, j, k));
            var results = new (List<Vector3> v, List<Vector3> n, List<Color> c, List<int> t)[jobs.Count];
            Parallel.For(0, jobs.Count, ji => results[ji] = Chunk(jobs[ji].i0, jobs[ji].j0, jobs[ji].k0, C));
            var surfRnd = new System.Random(7);
            for (int ji = 0; ji < jobs.Count; ji++)
            {
                var r = results[ji];
                if (r.t == null || r.t.Count == 0) continue;
                var m = new Mesh { indexFormat = r.v.Count > 65000 ? UnityEngine.Rendering.IndexFormat.UInt32 : UnityEngine.Rendering.IndexFormat.UInt16 };
                m.SetVertices(r.v); m.SetNormals(r.n); m.SetColors(r.c); m.SetTriangles(r.t, 0); m.RecalculateBounds();
                var go = new GameObject("cave chunk " + ji); go.transform.SetParent(transform, false);
                go.AddComponent<MeshFilter>().sharedMesh = m;
                var mr = go.AddComponent<MeshRenderer>(); mr.sharedMaterial = wallMat; mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                go.AddComponent<MeshCollider>().sharedMesh = m;
                chunks.Add(mr);
                Triangles += r.t.Count / 3;
                // sample the walls for planting: a point per ~6 m^2 or so
                for (int t = 0; t < r.t.Count; t += 3)
                {
                    if (surfRnd.NextDouble() > 0.18) continue;
                    var a = r.v[r.t[t]]; var b = r.v[r.t[t + 1]]; var c = r.v[r.t[t + 2]];
                    float w1 = (float)surfRnd.NextDouble(), w2 = (float)surfRnd.NextDouble(); if (w1 + w2 > 1) { w1 = 1 - w1; w2 = 1 - w2; }
                    surface.Add((a + (b - a) * w1 + (c - a) * w2, (r.n[r.t[t]] + r.n[r.t[t + 1]] + r.n[r.t[t + 2]]).normalized));
                }
            }
        }
        public readonly List<MeshRenderer> chunks = new List<MeshRenderer>();

        (List<Vector3>, List<Vector3>, List<Color>, List<int>) Chunk(int i0, int j0, int k0, int C)
        {
            var verts = new List<Vector3>(); var norms = new List<Vector3>(); var cols = new List<Color>(); var tris = new List<int>();
            var cache = new Dictionary<long, int>();
            var cp = new Vector3[8]; var cv = new float[8]; var cid = new long[8];
            for (int k = k0; k < Mathf.Min(k0 + C, nz - 1); k++)
                for (int j = j0; j < Mathf.Min(j0 + C, ny - 1); j++)
                    for (int i = i0; i < Mathf.Min(i0 + C, nx - 1); i++)
                    {
                        // skip cubes all rock or all water
                        bool any = false, all = true;
                        for (int c = 0; c < 8; c++)
                        {
                            int x = i + (c & 1), y = j + ((c >> 1) & 1), z = k + ((c >> 2) & 1);
                            cv[c] = G(x, y, z); cid[c] = ((long)z * ny + y) * nx + x;
                            cp[c] = new Vector3(X0 + x * Cell, Y0 + y * Cell, Z0 + z * Cell);
                            if (cv[c] < 0) any = true; else all = false;
                        }
                        if (!any || all) continue;
                        for (int t = 0; t < 6; t++)
                        {
                            int a = Tets[t, 0], b = Tets[t, 1], c2 = Tets[t, 2], d = Tets[t, 3];
                            Tet(new[] { a, b, c2, d }, cp, cv, cid, verts, norms, cols, tris, cache);
                        }
                    }
            return (verts, norms, cols, tris);
        }

        void Tet(int[] q, Vector3[] cp, float[] cv, long[] cid, List<Vector3> verts, List<Vector3> norms, List<Color> cols, List<int> tris, Dictionary<long, int> cache)
        {
            var inside = new List<int>(4); var outside = new List<int>(4);
            foreach (int c in q) (cv[c] < 0 ? inside : outside).Add(c);
            if (inside.Count == 0 || inside.Count == 4) return;
            int V(int a, int b)
            {
                long ka = cid[a], kb = cid[b]; long key = ka < kb ? ka * 2654435761L + kb : kb * 2654435761L + ka;
                if (cache.TryGetValue(key, out int idx)) return idx;
                float t = cv[a] / (cv[a] - cv[b]);
                var p = Vector3.Lerp(cp[a], cp[b], t);
                var n = -Grad(p); if (n.sqrMagnitude < 1e-8f) n = Vector3.up; n.Normalize();
                idx = verts.Count; verts.Add(p); norms.Add(n); cols.Add(Stone(p, n));
                cache[key] = idx; return idx;
            }
            void Tri(int a, int b, int c, Vector3 into)
            {
                var pa = verts[a]; var pb = verts[b]; var pc = verts[c];
                // above the ground there's no rock: drop it (the terrain has a hole there instead)
                var mid = (pa + pb + pc) / 3f;
                if (mid.y > bed.H(mid.x, mid.z) + 0.8f) return;
                // the face looks into the water
                if (Vector3.Dot(Vector3.Cross(pb - pa, pc - pa), into) < 0) { tris.Add(a); tris.Add(c); tris.Add(b); }
                else { tris.Add(a); tris.Add(b); tris.Add(c); }
            }
            if (inside.Count == 1 || inside.Count == 3)
            {
                bool one = inside.Count == 1;
                int lone = one ? inside[0] : outside[0];
                var others = one ? outside : inside;
                int a = V(lone, others[0]), b = V(lone, others[1]), c = V(lone, others[2]);
                var into = (one ? cp[lone] : (cp[others[0]] + cp[others[1]] + cp[others[2]]) / 3f) - (verts[a] + verts[b] + verts[c]) / 3f;
                Tri(a, b, c, into);
            }
            else
            {
                int a = V(inside[0], outside[0]), b = V(inside[0], outside[1]), c = V(inside[1], outside[1]), d = V(inside[1], outside[0]);
                var into = (cp[inside[0]] + cp[inside[1]]) * 0.5f - (verts[a] + verts[b] + verts[c] + verts[d]) * 0.25f;
                Tri(a, b, c, into); Tri(a, c, d, into);
            }
        }

        // the limestone's colour: pale strata, darker wet streaks down the walls, silt on the floors, the city's black
        Color Stone(Vector3 p, Vector3 n)
        {
            float strata = 0.5f + 0.5f * Mathf.Sin(p.y * 1.7f + VNoise.N2(p.x * 0.05f, p.z * 0.05f) * 6f);
            var c = Color.Lerp(new Color(0.46f, 0.44f, 0.4f), new Color(0.62f, 0.6f, 0.54f), strata);
            float wet = VNoise.N2(p.x * 0.4f + p.z * 0.1f, p.y * 0.05f);
            if (Mathf.Abs(n.y) < 0.5f) c = Color.Lerp(c, new Color(0.25f, 0.26f, 0.25f), Mathf.Clamp01((wet - 0.55f) * 3f));
            if (n.y > 0.6f) c = Color.Lerp(c, new Color(0.33f, 0.29f, 0.24f), 0.6f);
            if (n.y < -0.6f) c *= 0.8f;
            foreach (var ch in chambers)
                if (ch.city && ((p - ch.c).sqrMagnitude < (ch.r.x + 4) * (ch.r.x + 4)) && n.y > 0.5f) c = Color.Lerp(c, new Color(0.08f, 0.08f, 0.1f), 0.7f);
            c.a = 0;
            return c;
        }

        // ---- the terrain: holes where the caves break through ------------------------------------------------------------
        void Holes()
        {
            var t = bed.terrain; if (!t) return;
            var td = t.terrainData;
            int hr = td.holesResolution;
            var holes = td.GetHoles(0, 0, hr, hr);
            int cut = 0;
            for (int j = 0; j < hr; j++)
                for (int i = 0; i < hr; i++)
                {
                    float x = (i + 0.5f) * Seabed.Size / hr, z = (j + 0.5f) * Seabed.Size / hr;
                    if (x < X0 || x > X1 || z < Z0 || z > Z1) continue;
                    float y = bed.HeightAt(x, z);
                    if (Sdf(new Vector3(x, y - 0.6f, z)) < -0.2f) { holes[j, i] = false; cut++; }
                }
            td.SetHoles(0, 0, holes);
            Debug.Log($"DEEP CAVERNS: {cut} terrain hole cells");
        }

        // ---- the air pockets' water ----------------------------------------------------------------------------------
        void PocketWater()
        {
            foreach (var c in chambers)
            {
                if (!c.air) continue;
                var q = GameObject.CreatePrimitive(PrimitiveType.Quad); Destroy(q.GetComponent<Collider>());
                q.name = "Pocket water " + c.name; q.transform.SetParent(transform, false);
                q.transform.position = new Vector3(c.c.x, c.level, c.c.z); q.transform.rotation = Quaternion.Euler(90, 0, 0);
                q.transform.localScale = new Vector3(c.r.x * 2.2f, c.r.z * 2.2f, 1);
                var m = new Material(DeepShaders.Get("Deep/Glass")); m.SetColor("_Tint", new Color(0.1f, 0.35f, 0.45f)); m.SetFloat("_Clear", 0.4f);
                var mr = q.GetComponent<MeshRenderer>(); mr.sharedMaterial = m; mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                c.water = q.transform;
            }
        }

        // ---- building it all ----------------------------------------------------------------------------------------
        public static Caverns Build(Seabed bed, int seed)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var cv = new GameObject("Caverns").AddComponent<Caverns>(); I = cv;
            cv.bed = bed;
            cv.Layout(seed);
            long tLayout = sw.ElapsedMilliseconds;
            cv.FillGrid();
            long tField = sw.ElapsedMilliseconds;
            cv.BuildWalls();
            long tWalls = sw.ElapsedMilliseconds;
            cv.Holes();
            cv.PocketWater();
            Debug.Log($"DEEP CAVERNS: {cv.chambers.Count} chambers, {cv.segs.Count} tunnel segments, {cv.mouths.Count} mouths, {cv.Triangles} triangles in {cv.chunks.Count} chunks, {cv.surface.Count} wall points; layout {tLayout} ms, field {tField - tLayout} ms, walls {tWalls - tField} ms, all {sw.ElapsedMilliseconds} ms");
            foreach (var c in cv.chambers) Debug.Log($"DEEP CAVERNS: {c.name} at {c.c} r {c.r}{(c.air ? $" air above {c.level:0}" : "")}");
            foreach (var m in cv.mouths) Debug.Log($"DEEP CAVERNS: mouth at {m}");
            return cv;
        }

        // only the walls near the eye are drawn (the rest is behind rock or past the fog anyway)
        void LateUpdate()
        {
            var cam = Camera.main; if (!cam) return;
            var p = cam.transform.position;
            float far = 170f;
            foreach (var r in chunks) { bool on = r.bounds.SqrDistance(p) < far * far; if (r.enabled != on) r.enabled = on; }
        }

        public Chamber City { get { foreach (var c in chambers) if (c.city) return c; return chambers.Count > 0 ? chambers[0] : null; } }
        public Chamber Biggest(bool notCity)
        {
            Chamber b = null; foreach (var c in chambers) if (!c.air && (!notCity || !c.city) && (b == null || c.r.sqrMagnitude > b.r.sqrMagnitude)) b = c;
            return b;
        }
    }

    // the water's surface over a point: the sea's swell, or an air pocket's level in the caves
    public static class Sea
    {
        public static float SurfaceAt(Vector3 p)
        {
            var c = Caverns.I;
            if (c != null && p.y < -100f) { float s = c.PocketSurface(p); if (!float.IsPositiveInfinity(s)) return s; return float.PositiveInfinity; }
            return Waves.Height(p.x, p.z, Waves.T);
        }
    }
}

namespace Deep
{
    // thread-safe value noise (the caves are built on worker threads; Unity's own noise may not be)
    public static class VNoise
    {
        static float H(int x, int y, int z) { unchecked { uint h = (uint)(x * 374761393 + y * 668265263 + z * 1442695041); h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65535f; } }
        static float S(float t) => t * t * (3 - 2 * t);
        public static float N3(float x, float y, float z)
        {
            int ix = (int)System.Math.Floor(x), iy = (int)System.Math.Floor(y), iz = (int)System.Math.Floor(z);
            float fx = S(x - ix), fy = S(y - iy), fz = S(z - iz);
            float a = H(ix, iy, iz) + (H(ix + 1, iy, iz) - H(ix, iy, iz)) * fx, b = H(ix, iy + 1, iz) + (H(ix + 1, iy + 1, iz) - H(ix, iy + 1, iz)) * fx;
            float c = H(ix, iy, iz + 1) + (H(ix + 1, iy, iz + 1) - H(ix, iy, iz + 1)) * fx, d = H(ix, iy + 1, iz + 1) + (H(ix + 1, iy + 1, iz + 1) - H(ix, iy + 1, iz + 1)) * fx;
            float e = a + (b - a) * fy, f = c + (d - c) * fy;
            return e + (f - e) * fz;
        }
        public static float N2(float x, float y) => N3(x, y, 0.5f);
    }
}
