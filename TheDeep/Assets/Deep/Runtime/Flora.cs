// The plants of phases 1-2, drawn instanced: the doc's twenty flora of each biome (Blender models from
// tools/artgen/deep_flora.py) planted by habitat - meadows of sea-grass with reeds at their sandy edges, reefs of coral,
// sponges, anemones, ferns and algae, filaments and moss on the rock faces, and in the Labyrinth the Iron-Kelp stalks
// (code-built, swaying, with a far LOD) with what grows on them and on the clearings' floor. Placed once on a 32 m
// grid; only the cells near the eye are drawn (the water's fog hides the rest). Each kind has a group (reef, meadow,
// rock, kelp, floor) the sea life looks for.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Flora : MonoBehaviour
    {
        const float CellSize = 32f;
        class Kind { public string name, group; public Mesh mesh, far; public Material mat; public float drawDist; public Dictionary<long, List<Matrix4x4>> cells = new Dictionary<long, List<Matrix4x4>>(); }
        readonly List<Kind> kinds = new List<Kind>();
        Seabed bed;
        readonly List<Matrix4x4> batch = new List<Matrix4x4>(1023);
        public int Total;
        public static float DistScale = 1f;      // Perf.cs: how far plants are drawn (1 full)

        static long Key(int cx, int cz) => ((long)cx << 32) ^ (uint)cz;

        public void Build(Seabed seabed, int seed)
        {
            bed = seabed;
            var rnd = new System.Random(seed * 31 + 5);
            float R() => (float)rnd.NextDouble();
            // Iron-Kelp stays the code-built stalk (it sways along its height and has a far LOD); everything else is the
            // doc's flora as Blender models (tools/artgen/deep_flora.py)
            var kelp = AddKind("kelp", FloraMeshes.Kelp(14, rnd), FloraMeshes.Kelp(6, rnd), new Color(0.75f, 0.68f, 0.42f), 1.6f, 0.55f, 95f, "kelp");
            Kind K(string id, float sway, float transl, float dist, string group, float glow = 0)
            {
                var m = ModelLibrary.Get("Flora/flora_shallows", id) ?? ModelLibrary.Get("Flora/flora_kelp", id);
                if (!m) { Debug.LogWarning("DEEP FLORA: no model for " + id); return null; }
                var k = AddKind(id, m, null, Color.white, sway, transl, dist, group);
                k.mat.SetFloat("_Glow", glow);
                return k;
            }
            // the Shallows
            var ribbon = K("ribbon_sea_grass", 0.3f, 0.5f, 45f, "meadow"); var glade = K("glade_sea_grass", 0.15f, 0.5f, 40f, "meadow");
            var velvetReed = K("velvet_reed", 0.25f, 0.3f, 50f, "meadow"); var crestReed = K("crest_reed", 0.2f, 0.3f, 55f, "meadow");
            var pearlBloom = K("pearl_bloom", 0.1f, 0.4f, 35f, "meadow");
            var table = K("table_coral", 0f, 0.1f, 75f, "reef"); var spire = K("spire_coral", 0.02f, 0.1f, 75f, "reef");
            var crown = K("crown_sponge", 0f, 0.05f, 70f, "reef"); var sunSponge = K("sun_sponge", 0f, 0.1f, 55f, "reef");
            var fan = K("fan_anemone", 0.35f, 0.6f, 55f, "reef"); var petal = K("petal_fern", 0.15f, 0.5f, 40f, "reef");
            var lace = K("lace_fern", 0.2f, 0.85f, 40f, "reef"); var shimmer = K("shimmer_bloom", 0.15f, 0.5f, 35f, "reef");
            var feather = K("feather_kelplet", 0.35f, 0.5f, 45f, "reef"); var coralAlgae = K("coral_algae", 0f, 0.1f, 45f, "reef");
            var carpet = K("solar_carpet_algae", 0.02f, 0.3f, 45f, "reef");
            var verdant = K("verdant_algae", 0.25f, 0.6f, 40f, "rock"); var golden = K("golden_moss", 0f, 0.1f, 40f, "rock");
            var bloomMoss = K("bloom_moss", 0f, 0.2f, 40f, "rock"); var solarKelplet = K("solar_kelplet", 0.3f, 0.5f, 45f, "rock");
            // the Kelp Labyrinth: on and among the stalks, and on the floor of the clearings
            var glowBulb = K("glow_bulb", 0.05f, 0.6f, 60f, "kelp", 1.4f); var rootKelp = K("root_kelp", 0f, 0.1f, 80f, "kelp");
            var rubbery = K("rubbery_bracket", 0.05f, 0.3f, 45f, "kelp"); var rib = K("rib_bracket", 0.05f, 0.3f, 45f, "kelp");
            var coil = K("coil_tubeworm_flora", 0f, 0.1f, 40f, "kelp"); var strangle = K("strangle_spongeweed", 0f, 0.2f, 45f, "kelp");
            var bulbs = K("bulb_cluster", 0.2f, 0.5f, 40f, "kelp"); var emerald = K("emerald_pod", 0.3f, 0.6f, 40f, "kelp");
            var tangle = K("tangle_vines", 0.5f, 0.4f, 55f, "kelp"); var vineLace = K("vine_lace", 0.5f, 0.7f, 55f, "kelp");
            var canopy = K("canopy_vines", 0.3f, 0.4f, 80f, "kelp"); var strandLace = K("strand_lace", 0.6f, 0.7f, 60f, "kelp");
            var shadowT = K("shadow_tendril", 0.4f, 0f, 45f, "floor"); var spindle = K("spindle_tendril", 0.05f, 0.2f, 45f, "floor");
            var vascular = K("vascular_moss", 0f, 0.2f, 45f, "floor"); var latch = K("latch_moss", 0f, 0.1f, 40f, "floor");
            var silt = K("silt_spongeweed", 0f, 0.1f, 45f, "floor"); var gasp = K("gasp_tubeworm_flora", 0f, 0.1f, 45f, "floor");
            var bark = K("bark_pod", 0f, 0.1f, 35f, "floor");

            void P(Kind k, Vector3 p, float yaw, float s) { if (k != null) Put(k, p, yaw, Vector3.one * s); }

            for (float z = 4; z < Seabed.Size - 4; z += 2.6f)
                for (float x = 4; x < Seabed.Size - 4; x += 2.6f)
                {
                    float jx = x + R() * 2.4f, jz = z + R() * 2.4f;
                    float y = bed.SampleY(jx, jz);
                    float depth = -y;
                    float slope = Slope(jx, jz);
                    float r = R();
                    float yaw = R() * 360f;
                    var at = new Vector3(jx, y, jz);
                    if (depth > 52 && depth < 150 && slope < 0.9f)
                    {
                        // Iron-Kelp: dense forests broken by clearings (the maze's corridors run along the canyon floors)
                        float forest = Mathf.PerlinNoise(jx * 0.012f + seed, jz * 0.012f - seed);
                        if (r < Mathf.Clamp01((forest - 0.3f) * 2.2f) * 0.9f)
                        {
                            float h = Mathf.Min(depth - 6f, 18f + R() * 26f);
                            Put(kelp, new Vector3(jx, y - 0.3f, jz), yaw, new Vector3(1, h / 30f, 1));
                            // what grows on a stalk: glow bulbs at its foot, a holdfast now and then, shelves and pods and
                            // bladders up it, a curtain of vines to the next, the canopy at the top of the tall ones
                            float q = R();
                            if (q < 0.35f) P(glowBulb, at, yaw, 0.8f + R() * 0.8f);
                            else if (q < 0.43f) P(rootKelp, new Vector3(jx, y - 0.2f, jz), yaw, 0.8f + R() * 0.6f);
                            for (int k = 0; k < 3; k++)
                            {
                                float hh = (0.1f + R() * 0.75f) * h, w = R();
                                var on = new Vector3(jx, y + hh, jz);
                                if (w < 0.18f) P(R() < 0.5f ? rubbery : rib, on, R() * 360f, 0.8f + R() * 0.6f);
                                else if (w < 0.3f) P(bulbs, on, R() * 360f, 0.8f + R() * 0.7f);
                                else if (w < 0.4f) P(emerald, on, R() * 360f, 0.8f + R() * 0.6f);
                                else if (w < 0.47f) P(coil, new Vector3(jx, y + hh * 0.2f, jz), R() * 360f, 0.9f);
                                else if (w < 0.51f) P(strangle, new Vector3(jx, y + hh * 0.5f, jz), R() * 360f, 0.9f + R() * 0.6f);
                            }
                            float v = R();
                            if (v < 0.05f) P(tangle, new Vector3(jx, y + h * (0.3f + R() * 0.4f), jz), yaw, 1.2f + R());
                            else if (v < 0.08f) P(vineLace, new Vector3(jx, y + h * (0.3f + R() * 0.4f), jz), yaw, 1.2f + R());
                            if (h > 32f && R() < 0.25f) P(R() < 0.5f ? canopy : strandLace, new Vector3(jx, y + h - 1.5f, jz), yaw, 1.5f + R());
                        }
                        else
                        {
                            // the clearings' floor
                            if (r < 0.62f) { }
                            else if (r < 0.72f) P(vascular, at, yaw, 0.9f + R() * 0.8f);
                            else if (r < 0.79f) P(latch, at, yaw, 0.9f + R() * 0.6f);
                            else if (r < 0.85f) P(silt, at, yaw, 0.8f + R() * 0.6f);
                            else if (r < 0.89f) P(gasp, at, yaw, 0.8f + R() * 0.6f);
                            else if (r < 0.92f) P(bark, at, yaw, 1f + R() * 0.4f);
                            else if (r < 0.95f) P(shadowT, at, yaw, 0.8f + R() * 0.7f);
                            else if (r < 0.97f) P(spindle, at, yaw, 0.8f + R() * 0.6f);
                        }
                    }
                    else if (depth > 3 && depth < 46)
                    {
                        float meadow = Mathf.PerlinNoise(jx * 0.02f + 7 + seed, jz * 0.02f + seed);
                        float reef = Mathf.PerlinNoise(jx * 0.03f + 11, jz * 0.03f - 3 + seed);
                        if (slope >= 0.7f)
                        {
                            // rock faces: filaments, moss cushions, the small solar kelp
                            if (r < 0.18f) P(verdant, at, yaw, 0.8f + R() * 0.8f);
                            else if (r < 0.25f) P(golden, at, yaw, 0.7f + R() * 0.6f);
                            else if (r < 0.32f) P(bloomMoss, at, yaw, 0.7f + R() * 0.6f);
                            else if (r < 0.37f) P(solarKelplet, at, yaw, 0.8f + R() * 0.8f);
                        }
                        else if (meadow > 0.55f && r < 0.8f)
                        {
                            float g = R();
                            if (g < 0.006f) P(pearlBloom, at, yaw, 1f + R() * 0.5f);
                            else if (g < 0.6f) P(ribbon, at, yaw, 0.7f + R() * 0.6f);
                            else P(glade, at, yaw, 0.8f + R() * 0.6f);
                        }
                        else if (meadow > 0.5f && depth < 14f && r < 0.4f)
                            P(R() < 0.6f ? velvetReed : crestReed, at, yaw, 0.8f + R() * 0.6f);   // the reeds line the meadows' sandy edges
                        else if (reef > 0.58f)
                          for (int pass = 0; pass < (reef > 0.66f ? 3 : 2); pass++)   // the reef's heart grows thick
                          {
                            if (pass > 0) { r = R(); yaw = R() * 360f; at = new Vector3(jx + (R() - 0.5f) * 2.2f, 0, jz + (R() - 0.5f) * 2.2f); at.y = bed.SampleY(at.x, at.z); }
                            float s = 0.8f + 1.3f * R();
                            if (r < 0.10f) P(table, at, yaw, s);
                            else if (r < 0.22f) P(spire, at + Vector3.down * 0.1f, yaw, s);
                            else if (r < 0.28f) P(crown, at, yaw, 0.7f + R() * 0.8f);
                            else if (r < 0.36f) P(sunSponge, at, yaw, 0.8f + R() * 0.8f);
                            else if (r < 0.44f) P(fan, at, yaw, 0.6f + R() * 0.8f);
                            else if (r < 0.50f) P(petal, at, yaw, 0.8f + R() * 0.6f);
                            else if (r < 0.54f) P(lace, at, yaw, 0.8f + R() * 0.6f);
                            else if (r < 0.58f) P(shimmer, at, yaw, 1f + R() * 0.6f);
                            else if (r < 0.63f) P(feather, at, yaw, 0.8f + R() * 0.8f);
                            else if (r < 0.71f) P(coralAlgae, at, yaw, 0.8f + R() * 0.8f);
                            else if (r < 0.77f) P(carpet, at, yaw, 0.8f + R() * 0.8f);
                          }
                    }
                }
            Debug.Log($"DEEP FLORA: {Total} plants of {kinds.Count} kinds");
        }

        Kind AddKind(string name, Mesh m, Mesh far, Color tint, float sway, float translucency, float dist, string group)
        {
            var mat = new Material(Resources.Load<Shader>("Shaders/Flora")) { enableInstancing = true, name = name };
            mat.SetColor("_Tint", tint); mat.SetFloat("_Sway", sway); mat.SetFloat("_Translucency", translucency);
            mat.SetFloat("_Gloss", name.Contains("coral") || name.Contains("sponge") ? 0.25f : 0.08f);
            var k = new Kind { name = name, group = group, mesh = m, far = far, mat = mat, drawDist = dist };
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

        // the caves' kinds (CaveLife.cs): one of the caverns' models, glowing (and pulsing) at its own strength
        public int CaveKind(string id, float sway, float transl, float dist, float glow, float pulse)
        {
            for (int i = 0; i < kinds.Count; i++) if (kinds[i].name == id) return i;
            var m = ModelLibrary.Get("Flora/flora_caverns", id);
            if (!m) { Debug.LogWarning("DEEP FLORA: no cave model for " + id); return -1; }
            var k = AddKind(id, m, null, Color.white, sway, transl, dist, "cave");
            k.mat.SetFloat("_Glow", glow); k.mat.SetFloat("_Pulse", pulse);
            return kinds.Count - 1;
        }
        public int KindIndex(string id) { for (int i = 0; i < kinds.Count; i++) if (kinds[i].name == id) return i; return -1; }
        public void Plant(int kind, Vector3 p, Quaternion r, float s)
        {
            if (kind < 0 || kind >= kinds.Count) return;
            var k = kinds[kind];
            long key = Key(Mathf.FloorToInt(p.x / CellSize), Mathf.FloorToInt(p.z / CellSize));
            if (!k.cells.TryGetValue(key, out var list)) k.cells[key] = list = new List<Matrix4x4>();
            list.Add(Matrix4x4.TRS(p, r, Vector3.one * s));
            Total++;
        }

        // pull up whatever grows where a test says (the caves' mouths)
        public void ClearWhere(System.Func<Vector3, bool> test)
        {
            foreach (var k in kinds) foreach (var list in k.cells.Values) Total -= list.RemoveAll(m => test(m.GetColumn(3)));
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
                if (System.Array.IndexOf(names, k.group) < 0 && System.Array.IndexOf(names, k.name) < 0) continue;
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

        // ---- gathering: the plant the diver is looking at, taking it, and its regrowth ------------------------------
        public struct Picked { public string kind; public long key; public int index; public Vector3 pos; public float scale; }
        struct Regrow { public Kind k; public long key; public Matrix4x4 m; public float at; }
        readonly List<Regrow> regrow = new List<Regrow>();

        // the nearest planted thing along a ray (within maxDist), of the kinds ok() accepts
        public bool Pick(Vector3 o, Vector3 dir, float maxDist, System.Func<string, bool> ok, out Picked best)
        {
            best = default; float bt = float.MaxValue; bool found = false;
            int c0x = Mathf.FloorToInt((o.x - maxDist - 4) / CellSize), c1x = Mathf.FloorToInt((o.x + maxDist + 4) / CellSize);
            int c0z = Mathf.FloorToInt((o.z - maxDist - 4) / CellSize), c1z = Mathf.FloorToInt((o.z + maxDist + 4) / CellSize);
            foreach (var k in kinds)
            {
                if (!ok(k.name)) continue;
                for (int x = c0x; x <= c1x; x++)
                    for (int z = c0z; z <= c1z; z++)
                    {
                        long key = Key(x, z);
                        if (!k.cells.TryGetValue(key, out var list)) continue;
                        for (int i = 0; i < list.Count; i++)
                        {
                            var m = list[i];
                            Vector3 p = m.GetColumn(3);
                            float s = m.GetColumn(0).magnitude;
                            // a kelp stalk is hit anywhere along its height; everything else round its middle
                            Vector3 aim = p + Vector3.up * (k.name == "kelp" ? Mathf.Clamp(o.y - p.y, 0, 30f * m.GetColumn(1).magnitude) : 0.35f * s);
                            float t = Vector3.Dot(aim - o, dir);
                            if (t < 0 || t > maxDist || t >= bt) continue;
                            float perp = (o + dir * t - aim).magnitude;
                            if (perp > 0.45f + 0.35f * s) continue;
                            bt = t; found = true;
                            best = new Picked { kind = k.name, key = key, index = i, pos = aim, scale = s };
                        }
                    }
            }
            return found;
        }

        // take it: it's gone until it regrows (out of sight, after `seconds`)
        public static System.Action<string, long, Vector3, float> OnTake;   // (Net.cs passes it to the crew)

        public void Take(Picked p, float seconds)
        {
            OnTake?.Invoke(p.kind, p.key, p.pos, seconds);
            foreach (var k in kinds)
            {
                if (k.name != p.kind || !k.cells.TryGetValue(p.key, out var list) || p.index >= list.Count) continue;
                regrow.Add(new Regrow { k = k, key = p.key, m = list[p.index], at = Time.time + seconds });
                list[p.index] = list[list.Count - 1]; list.RemoveAt(list.Count - 1);
                Total--;
                return;
            }
        }

        // the plant standing nearest a point (the network self-test)
        public bool NearestPlant(Vector3 at, out Picked best)
        {
            best = default; float bd = float.MaxValue; bool found = false;
            int cx = Mathf.FloorToInt(at.x / CellSize), cz = Mathf.FloorToInt(at.z / CellSize);
            foreach (var k in kinds)
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++)
                    {
                        long key = Key(cx + dx, cz + dz);
                        if (!k.cells.TryGetValue(key, out var list)) continue;
                        for (int i = 0; i < list.Count; i++)
                        {
                            Vector3 p = list[i].GetColumn(3); float d = (p - at).sqrMagnitude;
                            if (d < bd) { bd = d; found = true; best = new Picked { kind = k.name, key = key, index = i, pos = p, scale = list[i].GetColumn(0).magnitude }; }
                        }
                    }
            return found;
        }

        // a crewmate took one: the plant of that kind in that cell standing at that point (its base)
        public bool TakeAt(string kind, long key, Vector3 at, float seconds)
        {
            foreach (var k in kinds)
            {
                if (k.name != kind || !k.cells.TryGetValue(key, out var list)) continue;
                int best = -1; float bd = 4f;
                for (int i = 0; i < list.Count; i++)
                {
                    Vector3 p = list[i].GetColumn(3);
                    float d = new Vector2(p.x - at.x, p.z - at.z).sqrMagnitude;
                    if (d < bd) { bd = d; best = i; }
                }
                if (best < 0) return false;
                regrow.Add(new Regrow { k = k, key = key, m = list[best], at = Time.time + seconds });
                list[best] = list[list.Count - 1]; list.RemoveAt(list.Count - 1);
                Total--;
                return true;
            }
            return false;
        }
        // what's been taken and not grown back yet (for a crewmate joining): kind, cell, where, seconds left
        public System.Collections.Generic.IEnumerable<(string kind, long key, Vector3 pos, float left)> Taken()
        {
            foreach (var r in regrow) yield return (r.k.name, r.key, (Vector3)r.m.GetColumn(3), Mathf.Max(0, r.at - Time.time));
        }

        void Update()
        {
            if (regrow.Count == 0) return;
            var cam = Camera.main; var c = cam ? cam.transform.position : Vector3.zero;
            for (int i = regrow.Count - 1; i >= 0; i--)
            {
                var r = regrow[i];
                if (Time.time < r.at || ((Vector3)r.m.GetColumn(3) - c).sqrMagnitude < 40f * 40f) continue;
                if (!r.k.cells.TryGetValue(r.key, out var list)) r.k.cells[r.key] = list = new List<Matrix4x4>();
                list.Add(r.m); Total++;
                regrow.RemoveAt(i);
            }
        }

        void LateUpdate()
        {
            if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null) return;   // (the headless self-test)
            var cam = Camera.main; if (!cam) return;
            var c = cam.transform.position;
            var planes = GeometryUtility.CalculateFrustumPlanes(cam);
            foreach (var k in kinds)
            {
                float dist = Mathf.Min(k.drawDist * DistScale, cam.farClipPlane);
                int r = Mathf.CeilToInt(dist / CellSize);
                int cx0 = Mathf.FloorToInt(c.x / CellSize), cz0 = Mathf.FloorToInt(c.z / CellSize);
                var rp = new RenderParams(k.mat) { shadowCastingMode = k.name == "kelp" || k.name == "table_coral" || k.name == "root_kelp" ? UnityEngine.Rendering.ShadowCastingMode.On : UnityEngine.Rendering.ShadowCastingMode.Off, receiveShadows = true };
                for (int dz = -r; dz <= r; dz++)
                    for (int dx = -r; dx <= r; dx++)
                    {
                        if (!k.cells.TryGetValue(Key(cx0 + dx, cz0 + dz), out var list)) continue;
                        var center = new Vector3((cx0 + dx + 0.5f) * CellSize, c.y, (cz0 + dz + 0.5f) * CellSize);
                        float dd = new Vector2(center.x - c.x, center.z - c.z).magnitude;
                        if (dd > dist + CellSize * 0.71f) continue;
                        var b = new Bounds(new Vector3(center.x, -160, center.z), new Vector3(CellSize + 8, 340, CellSize + 8));
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
