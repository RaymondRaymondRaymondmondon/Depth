// Mineral deposits and wreck salvage (stage 4): what the diver gathers that isn't a plant or an animal.
//   - Titanium Ore (the design doc: "sandbar rock nodes" in the Shallows, "titanium nodes in kelp clearings"): ore
//     nodes on the rocky slopes and in the clearings, worked with the Starter Drill (three ore), and loose chunks
//     broken off them lying on the sand, picked up by hand (one) - how a crew with nothing but a knife gets the
//     titanium for its first drill.
//   - The wrecks: the Sunken Pirate Galleon in the Shallows (Salvaged Galleon Wood and Brass) and the tangled
//     dreadnought in the Kelp Labyrinth (Military-Grade Titanium and Rusted Iron Scrap), Blender models
//     (tools/artgen/deep_wrecks.py) whose JSON says where their salvage lies; their boxes are colliders.
// Taken deposits come back out of sight after a day or two.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    [System.Serializable] public class WreckSalvage { public string kind, item; public float[] pos; }
    [System.Serializable] public class WreckBox { public float[] c, h; }
    [System.Serializable] public class WreckFile { public WreckSalvage[] salvage; public WreckBox[] boxes; }

    public class Deposits : MonoBehaviour
    {
        public static Deposits I;
        public class Node { public string model, item, by; public Vector3 pos; public Quaternion rot; public float scale; public int yield; public bool taken; public float back; public string where; }
        public readonly List<Node> nodes = new List<Node>();
        public readonly List<(string name, Transform t)> wrecks = new List<(string, Transform)>();
        readonly Dictionary<string, Material> mats = new Dictionary<string, Material>();
        readonly List<Matrix4x4> batch = new List<Matrix4x4>(1023);
        Seabed bed;

        static Material VC()
        {
            var m = new Material(Shader.Find("Deep/Lit")) { enableInstancing = true };
            m.SetFloat("_UseVC", 1); m.SetFloat("_VCAlbedo", 1); m.SetFloat("_Roughness", 0.75f); m.SetFloat("_Metallic", 0.2f); m.SetFloat("_Cull", 0);
            return m;
        }

        public static Deposits Build(Seabed bed, Flora flora, Vector3 spawn, int seed)
        {
            var go = new GameObject("Deposits");
            var d = go.AddComponent<Deposits>(); I = d; d.bed = bed;
            var rnd = new System.Random(seed * 101 + 7);
            float R() => (float)rnd.NextDouble();
            // titanium: on the Shallows' rocky slopes and in the kelp clearings
            for (float z = 10; z < Seabed.Size - 10; z += 9f)
                for (float x = 10; x < Seabed.Size - 10; x += 9f)
                {
                    float jx = x + R() * 8f, jz = z + R() * 8f;
                    float y = bed.SampleY(jx, jz), depth = -y;
                    float slope = Slope(bed, jx, jz);
                    float rock = Mathf.PerlinNoise(jx * 0.015f + 31, jz * 0.015f - 17);
                    bool shallowsRock = depth > 6 && depth < 48 && slope > 0.35f && rock > 0.55f;
                    bool kelpClearing = depth > 55 && depth < 148 && Mathf.PerlinNoise(jx * 0.012f + seed, jz * 0.012f - seed) < 0.35f && rock > 0.6f;
                    if (!shallowsRock && !kelpClearing) continue;
                    float r = R();
                    if (r < 0.18f) d.Add("titanium_ore", "Titanium Ore", "drill", new Vector3(jx, y - 0.15f, jz), R() * 360f, 0.8f + R() * 0.6f, 3, shallowsRock ? "Sandbar rock nodes" : "titanium nodes in kelp clearings");
                    else if (r < 0.5f) d.Add("titanium_ore", "Titanium Ore", "hand", new Vector3(jx + R() * 2, bed.SampleY(jx, jz) + 0.02f, jz + R() * 2), R() * 360f, 0.22f + R() * 0.1f, 1, "a loose chunk of ore");
                }
            // the wrecks, well away from the start
            d.PlaceWreck("galleon", "the Sunken Pirate Galleon", spawn, 160f, 420f, 18f, 34f, rnd, flora);
            d.PlaceWreck("dreadnought", "the tangled dreadnought", spawn, 300f, 1100f, 95f, 140f, rnd, flora);
            Debug.Log($"DEEP DEPOSITS: {d.nodes.Count} deposits; wrecks " + string.Join(", ", d.wrecks.ConvertAll(w => $"{w.name} at {w.t.position}")));
            return d;
        }

        static float Slope(Seabed bed, float x, float z)
        {
            float a = bed.SampleY(x + 1.5f, z) - bed.SampleY(x - 1.5f, z), b = bed.SampleY(x, z + 1.5f) - bed.SampleY(x, z - 1.5f);
            return Mathf.Sqrt(a * a + b * b) / 3f;
        }

        void Add(string model, string item, string by, Vector3 pos, float yaw, float scale, int yield, string where)
        {
            nodes.Add(new Node { model = model, item = item, by = by, pos = pos, rot = Quaternion.Euler(0, yaw, 0), scale = scale, yield = yield, where = where });
        }

        // a wreck on flat ground at the right depth, in a ring round the start; its salvage where its JSON says
        void PlaceWreck(string name, string title, Vector3 around, float rMin, float rMax, float dMin, float dMax, System.Random rnd, Flora flora)
        {
            var mesh = ModelLibrary.Get("Wrecks/wreck_" + name, "wreck_" + name);
            var data = Resources.Load<TextAsset>("Wrecks/wreck_" + name);
            if (!mesh || !data) { Debug.LogWarning("DEEP: no wreck " + name); return; }
            var file = JsonUtility.FromJson<WreckFile>(data.text);
            Vector3 best = default; float bestScore = float.MaxValue;
            for (int k = 0; k < 600; k++)
            {
                float a = (float)rnd.NextDouble() * Mathf.PI * 2, r = Mathf.Lerp(rMin, rMax, (float)rnd.NextDouble());
                var p = around + new Vector3(Mathf.Cos(a) * r, 0, Mathf.Sin(a) * r);
                if (p.x < 60 || p.z < 60 || p.x > Seabed.Size - 60 || p.z > Seabed.Size - 60) continue;
                float y = bed.SampleY(p.x, p.z);
                if (-y < dMin || -y > dMax) continue;
                float s = Slope(bed, p.x, p.z) + Slope(bed, p.x + 15, p.z) + Slope(bed, p.x - 15, p.z) + Slope(bed, p.x, p.z + 15) + Slope(bed, p.x, p.z - 15);
                if (s < bestScore) { bestScore = s; best = new Vector3(p.x, y, p.z); }
            }
            if (bestScore == float.MaxValue) { Debug.LogWarning("DEEP: nowhere for " + name); return; }
            var go = new GameObject("Wreck: " + title);
            go.transform.SetPositionAndRotation(best + Vector3.down * 0.6f, Quaternion.Euler(0, (float)rnd.NextDouble() * 360f, 0));
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            var mr = go.AddComponent<MeshRenderer>(); mr.sharedMaterial = VC();
            foreach (var b in file.boxes)
            {
                var bc = go.AddComponent<BoxCollider>();
                bc.center = new Vector3(b.c[0], b.c[1], b.c[2]); bc.size = new Vector3(b.h[0], b.h[1], b.h[2]) * 2f;
            }
            flora?.Clear(go.transform, new Vector3(12f, 20f, name == "galleon" ? 20f : 40f));
            foreach (var sv in file.salvage)
            {
                var w = go.transform.TransformPoint(new Vector3(sv.pos[0], sv.pos[1], sv.pos[2]));
                if (sv.pos[1] < 0.6f) w.y = bed.SampleY(w.x, w.z) + 0.05f;
                Add(sv.kind, sv.item, "salvage", w, (float)rnd.NextDouble() * 360f, 1f, 2, title);
            }
            wrecks.Add((title, go.transform));
        }

        // ---- gathering --------------------------------------------------------------------------------------------
        public Node Pick(Vector3 o, Vector3 dir, float maxDist)
        {
            Node best = null; float bt = maxDist;
            foreach (var n in nodes)
            {
                if (n.taken) continue;
                var c = n.pos + Vector3.up * 0.4f * n.scale;
                float t = Vector3.Dot(c - o, dir);
                if (t < 0 || t > bt) continue;
                if ((o + dir * t - c).magnitude > 0.35f + 0.6f * n.scale) continue;
                bt = t; best = n;
            }
            return best;
        }

        // can this tool work it? (a loose chunk by hand; nodes with the drill; salvage by the resource table's tools)
        public static bool Works(Node n, string tool, out string need)
        {
            need = null;
            if (n.by == "hand") return true;
            if (n.by == "drill") { need = "the Starter Drill"; return tool == "drill"; }
            var it = ItemDB.Get(n.item);
            if (it != null && ItemDB.ToolTakes(it, tool)) return true;
            if (it != null && tool == "hand" && (it.tool ?? "").Contains("Survival Knife")) { need = "a knife"; return false; }
            need = it?.tool ?? "a tool";
            return false;
        }

        public void Take(Node n) { n.taken = true; n.back = Time.time + (n.by == "salvage" ? 3600f : 2400f); }

        void Update()
        {
            var cam = Camera.main; var c = cam ? cam.transform.position : Vector3.zero;
            foreach (var n in nodes)
                if (n.taken && Time.time > n.back && (n.pos - c).sqrMagnitude > 50f * 50f) n.taken = false;
            Draw(cam);
        }

        void Draw(Camera cam)
        {
            if (!cam) return;
            var planes = GeometryUtility.CalculateFrustumPlanes(cam);
            var by = new Dictionary<string, List<Matrix4x4>>();
            foreach (var n in nodes)
            {
                if (n.taken || (n.pos - cam.transform.position).sqrMagnitude > 90f * 90f) continue;
                if (!GeometryUtility.TestPlanesAABB(planes, new Bounds(n.pos, Vector3.one * 2f * Mathf.Max(1f, n.scale)))) continue;
                if (!by.TryGetValue(n.model, out var l)) by[n.model] = l = new List<Matrix4x4>();
                l.Add(Matrix4x4.TRS(n.pos, n.rot, Vector3.one * n.scale));
            }
            foreach (var kv in by)
            {
                var mesh = ModelLibrary.Get("Minerals/minerals", kv.Key); if (!mesh) continue;
                if (!mats.TryGetValue(kv.Key, out var m)) mats[kv.Key] = m = VC();
                var rp = new RenderParams(m) { shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On, receiveShadows = true };
                for (int i = 0; i < kv.Value.Count; i += 1023)
                {
                    batch.Clear(); for (int j = i; j < Mathf.Min(kv.Value.Count, i + 1023); j++) batch.Add(kv.Value[j]);
                    Graphics.RenderMeshInstanced(rp, mesh, 0, batch);
                }
            }
        }
    }
}
