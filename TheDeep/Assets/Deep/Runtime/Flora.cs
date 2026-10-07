// The plants of phases 1-2, drawn instanced: the Iron-Kelp forests of the Labyrinth (tall stalks with leaf blades and
// gas floats), sea-grass meadows and coral heads (table, brain and spire) on the Shallows' reef. Placed once on a
// 32 m grid from the seabed and noise; only the cells near the eye are drawn (the water's fog hides the rest).
// These are stand-in meshes built in code; the Blender models replace them later with the same placement.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Flora : MonoBehaviour
    {
        const float CellSize = 32f;
        class Kind { public string name; public Mesh mesh, far; public Material mat; public float drawDist; public Dictionary<long, List<Matrix4x4>> cells = new Dictionary<long, List<Matrix4x4>>(); }
        readonly List<Kind> kinds = new List<Kind>();
        Seabed bed;
        readonly List<Matrix4x4> batch = new List<Matrix4x4>(1023);
        public int Total;

        static long Key(int cx, int cz) => ((long)cx << 32) ^ (uint)cz;

        public void Build(Seabed seabed, int seed)
        {
            bed = seabed;
            var rnd = new System.Random(seed * 31 + 5);
            var kelp = AddKind("kelp", FloraMeshes.Kelp(14, rnd), FloraMeshes.Kelp(6, rnd), new Color(0.75f, 0.68f, 0.42f), 1.6f, 0.55f, 95f);
            var grass = AddKind("seagrass", FloraMeshes.GrassClump(rnd), null, new Color(0.62f, 0.85f, 0.42f), 0.25f, 0.5f, 45f);
            var table = AddKind("tablecoral", FloraMeshes.TableCoral(rnd), null, new Color(0.95f, 0.72f, 0.62f), 0.0f, 0.1f, 75f);
            var brain = AddKind("braincoral", FloraMeshes.BrainCoral(rnd), null, new Color(0.88f, 0.80f, 0.55f), 0.0f, 0.05f, 75f);
            var spire = AddKind("spirecoral", FloraMeshes.SpireCoral(rnd), null, new Color(0.80f, 0.45f, 0.62f), 0.05f, 0.1f, 75f);
            var fan = AddKind("fananemone", FloraMeshes.Fan(rnd), null, new Color(0.95f, 0.45f, 0.38f), 0.35f, 0.6f, 55f);

            for (float z = 4; z < Seabed.Size - 4; z += 2.6f)
                for (float x = 4; x < Seabed.Size - 4; x += 2.6f)
                {
                    float jx = x + (float)rnd.NextDouble() * 2.4f, jz = z + (float)rnd.NextDouble() * 2.4f;
                    float y = bed.H(jx, jz);
                    float depth = -y;
                    float slope = Slope(jx, jz);
                    float r = (float)rnd.NextDouble();
                    float yaw = (float)rnd.NextDouble() * 360f;
                    if (depth > 52 && depth < 150 && slope < 0.9f)
                    {
                        // Iron-Kelp: dense forests broken by clearings (the maze's corridors run along the canyon floors)
                        float forest = Mathf.PerlinNoise(jx * 0.012f + seed, jz * 0.012f - seed);
                        if (r < Mathf.Clamp01((forest - 0.3f) * 2.2f) * 0.9f)
                        {
                            float h = Mathf.Min(depth - 6f, 18f + (float)rnd.NextDouble() * 26f);
                            Put(kelp, new Vector3(jx, y - 0.3f, jz), yaw, new Vector3(1, h / 30f, 1));
                        }
                    }
                    else if (depth > 3 && depth < 46 && slope < 0.5f)
                    {
                        float meadow = Mathf.PerlinNoise(jx * 0.02f + 7 + seed, jz * 0.02f + seed);
                        float reef = Mathf.PerlinNoise(jx * 0.03f + 11, jz * 0.03f - 3 + seed);
                        if (meadow > 0.55f && r < 0.8f) Put(grass, new Vector3(jx, y, jz), yaw, Vector3.one * (0.7f + 0.6f * (float)rnd.NextDouble()));
                        else if (reef > 0.58f)
                        {
                            float s = 0.6f + 1.2f * (float)rnd.NextDouble();
                            if (r < 0.12f) Put(table, new Vector3(jx, y, jz), yaw, Vector3.one * s);
                            else if (r < 0.26f) Put(brain, new Vector3(jx, y - 0.2f, jz), yaw, Vector3.one * s);
                            else if (r < 0.40f) Put(spire, new Vector3(jx, y - 0.1f, jz), yaw, Vector3.one * s);
                            else if (r < 0.50f) Put(fan, new Vector3(jx, y, jz), yaw, Vector3.one * (0.6f + 0.6f * (float)rnd.NextDouble()));
                        }
                    }
                }
        }

        Kind AddKind(string name, Mesh m, Mesh far, Color tint, float sway, float translucency, float dist)
        {
            var mat = new Material(Resources.Load<Shader>("Shaders/Flora")) { enableInstancing = true, name = name };
            mat.SetColor("_Tint", tint); mat.SetFloat("_Sway", sway); mat.SetFloat("_Translucency", translucency); mat.SetFloat("_Gloss", name.Contains("coral") ? 0.25f : 0.08f);
            var k = new Kind { name = name, mesh = m, far = far, mat = mat, drawDist = dist };
            kinds.Add(k); return k;
        }

        float Slope(float x, float z)
        {
            float a = bed.H(x + 1.5f, z) - bed.H(x - 1.5f, z), b = bed.H(x, z + 1.5f) - bed.H(x, z - 1.5f);
            return Mathf.Sqrt(a * a + b * b) / 3f;
        }

        void Put(Kind k, Vector3 p, float yaw, Vector3 s)
        {
            long key = Key(Mathf.FloorToInt(p.x / CellSize), Mathf.FloorToInt(p.z / CellSize));
            if (!k.cells.TryGetValue(key, out var list)) k.cells[key] = list = new List<Matrix4x4>();
            list.Add(Matrix4x4.TRS(p, Quaternion.Euler(0, yaw, 0), s));
            Total++;
        }

        // clear the plants out from under something placed on the seabed (the Nautilus): every plant whose root lies in
        // the box (local half extents about a transform)
        public void Clear(Transform t, Vector3 half)
        {
            foreach (var k in kinds)
                foreach (var list in k.cells.Values)
                    Total -= list.RemoveAll(m =>
                    {
                        var p = t.InverseTransformPoint(m.GetColumn(3));
                        return Mathf.Abs(p.x) < half.x && Mathf.Abs(p.y) < half.y && Mathf.Abs(p.z) < half.z;
                    });
        }

        // planted things of the given kinds within a ring round a point (the reef fish gather over the coral, the kelp
        // species among the stalks)
        public void Near(Vector3 c, float rMin, float rMax, List<Vector3> into, int max, params string[] names)
        {
            into.Clear();
            int c0x = Mathf.FloorToInt((c.x - rMax) / CellSize), c1x = Mathf.FloorToInt((c.x + rMax) / CellSize);
            int c0z = Mathf.FloorToInt((c.z - rMax) / CellSize), c1z = Mathf.FloorToInt((c.z + rMax) / CellSize);
            foreach (var k in kinds)
            {
                if (System.Array.IndexOf(names, k.name) < 0) continue;
                for (int x = c0x; x <= c1x; x++)
                    for (int z = c0z; z <= c1z; z++)
                    {
                        if (!k.cells.TryGetValue(Key(x, z), out var list)) continue;
                        foreach (var m in list)
                        {
                            Vector3 p = m.GetColumn(3);
                            float d = new Vector2(p.x - c.x, p.z - c.z).magnitude;
                            if (d >= rMin && d <= rMax) { into.Add(p); if (into.Count >= max) return; }
                        }
                    }
            }
        }

        void LateUpdate()
        {
            var cam = Camera.main; if (!cam) return;
            var c = cam.transform.position;
            var planes = GeometryUtility.CalculateFrustumPlanes(cam);
            foreach (var k in kinds)
            {
                float dist = Mathf.Min(k.drawDist, cam.farClipPlane);
                int r = Mathf.CeilToInt(dist / CellSize);
                int cx0 = Mathf.FloorToInt(c.x / CellSize), cz0 = Mathf.FloorToInt(c.z / CellSize);
                var rp = new RenderParams(k.mat) { shadowCastingMode = k.name == "kelp" ? UnityEngine.Rendering.ShadowCastingMode.On : UnityEngine.Rendering.ShadowCastingMode.Off, receiveShadows = true };
                for (int dz = -r; dz <= r; dz++)
                    for (int dx = -r; dx <= r; dx++)
                    {
                        if (!k.cells.TryGetValue(Key(cx0 + dx, cz0 + dz), out var list)) continue;
                        var center = new Vector3((cx0 + dx + 0.5f) * CellSize, c.y, (cz0 + dz + 0.5f) * CellSize);
                        float dd = new Vector2(center.x - c.x, center.z - c.z).magnitude;
                        if (dd > dist + CellSize * 0.71f) continue;
                        var b = new Bounds(new Vector3(center.x, -60, center.z), new Vector3(CellSize + 8, 200, CellSize + 8));
                        if (!GeometryUtility.TestPlanesAABB(planes, b)) continue;
                        rp.worldBounds = b;
                        var mesh = (k.far != null && dd > dist * 0.45f) ? k.far : k.mesh;
                        for (int i = 0; i < list.Count; i += 1023)
                        {
                            int n = Mathf.Min(1023, list.Count - i);
                            Graphics.RenderMeshInstanced(rp, mesh, 0, list, n, i);
                        }
                    }
            }
        }
    }
}
