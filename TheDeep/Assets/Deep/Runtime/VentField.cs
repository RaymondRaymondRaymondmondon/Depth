// The Thermal Vents (stage 10; the design doc's biome 4, 300 to 500 m): "volcanic ridges, black smoker chimneys, and
// basalt plateaus spewing superheated mineral plumes. Boiling water pillars damage hulls with heat and cause turbulence.
// Rich in copper and thermal energy." Hazards: "extreme heat stress on the hull (needs Thermal Hull Shielding), sudden
// geysers, turbulence that throws subs against volcanic glass, and the Boiler Worm erupting from the rock."
//
// A second terrain east of the first (x 1536-2560 across the whole 1.5 km), joined to the drop-off's foot at 300 m and
// falling to 520 m at its far edges: basalt ridges, terraced plateaus, and a rift winding through it where the vents
// crowd. Built from the world's seed. On it:
//   - black smokers: chimneys (Blender, tools/artgen/deep_vents.py) in clusters along the rift and the ridges, each
//     with its plume of black mineral cloud rising (particles while it's near the eye), its throat glowing;
//   - geysers: cones in the rift that bubble, rumble, then erupt for a few seconds - a boiling pillar 70 m high;
//   - magma: the basalt's cracks glow red where the ground is hot (the seabed shader, `_Basalt`), and light the water;
//   - heat (HeatAt, degrees over the sea's own): over a smoker, in its plume, in an erupting pillar. It cooks a diver,
//     stresses the Nautilus's hull (Thermal Hull Shielding stops it), and wears down the Kite-Sub;
//   - turbulence (TurbulenceAt): round an eruption it throws a sub about (into the volcanic glass);
//   - the Boiler Worm: it waits in the rock of the rift and feels vibration; something moving loud near its lair gets a
//     rumble of warning, then the worm erupts from the rock and strikes.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class VentField : MonoBehaviour
    {
        public static VentField I;
        public const float X0 = 1536f, X1 = 2560f, Z0 = 0f, Z1 = 1536f, Bottom = -560f, Top = -200f;
        public const int Res = 769;
        public Terrain terrain;
        float[,] heights;
        int seed;
        Seabed main;

        public class Vent
        {
            public Vector3 pos; public bool geyser; public float height, radius;
            public Transform model; public ParticleSystem plume;
            public float t, period, phase; public int state;           // geysers: 0 idle, 1 warning, 2 erupting
            public bool Erupting => geyser && state == 2;
        }
        public readonly List<Vent> vents = new List<Vent>();
        public readonly List<(Vector3 p, Color col, float range)> glow = new List<(Vector3, Color, float)>();
        float[] riftZ;       // the rift's centre line: z at each 8 m of x

        // ---- the ground -------------------------------------------------------------------------------------------
        float Nz(float x, float z, float f, int o) => Mathf.PerlinNoise(x * f + 51.7f * o + seed * 3.1f, z * f - 23.3f * o + seed * 1.7f);
        float Fbm(float x, float z, float f, int oct, int o) { float s = 0, a = 1, n = 0; for (int i = 0; i < oct; i++) { s += a * (Nz(x, z, f, o + i) * 2 - 1); n += a; a *= 0.5f; f *= 2.05f; } return s / n; }
        float Ridge(float x, float z, float f, int o) { float v = 1 - Mathf.Abs(Fbm(x, z, f, 3, o)); return v * v; }

        public float RiftZ(float x)
        {
            float u = Mathf.Clamp((x - X0) / 8f, 0, riftZ.Length - 1.001f); int i = (int)u;
            return Mathf.Lerp(riftZ[i], riftZ[i + 1], u - i);
        }
        public float RiftDist(Vector3 p) => Mathf.Abs(p.z - RiftZ(p.x));

        public float HeightAt(float x, float z)
        {
            float t = x - X0;
            float y = Mathf.Lerp(-305f, -405f, Mathf.SmoothStep(0, 1, t / 280f));
            // ridges and plateaus
            float r = Ridge(x, z, 0.0055f, 1);
            y += (r - 0.45f) * 85f;
            float plateau = Mathf.SmoothStep(0, 1, (Nz(x, z, 0.004f, 3) - 0.55f) * 6f);
            float terr = Mathf.Floor(y / 9f) * 9f + Mathf.SmoothStep(0, 9, Mathf.Repeat(y, 9f)) * 0.0f;
            y = Mathf.Lerp(y, terr + 2f, plateau * 0.8f);
            y += 4f * Fbm(x, z, 0.04f, 3, 5);
            // the rift: a winding trench where the vents crowd
            float d = Mathf.Abs(z - RiftZ(x));
            y -= 70f * Mathf.Exp(-(d * d) / (2f * 40f * 40f)) * Mathf.SmoothStep(0, 1, t / 160f);
            // the far edges fall away into deeper water (the biomes below come later)
            float edge = Mathf.Min(X1 - x, Mathf.Min(z - Z0, Z1 - z));
            y = Mathf.Lerp(-540f, y, Mathf.SmoothStep(0, 1, edge / 110f));
            // the seam: the drop-off's foot (the main seabed's edge stands at 300 m)
            y = Mathf.Lerp(-300f, y, Mathf.SmoothStep(0, 1, t / 70f));
            return Mathf.Clamp(y, Bottom + 2, Top - 1);
        }

        public float H(float x, float z)
        {
            float fx = Mathf.Clamp((x - X0) / (X1 - X0) * (Res - 1), 0, Res - 1.001f), fz = Mathf.Clamp((z - Z0) / (Z1 - Z0) * (Res - 1), 0, Res - 1.001f);
            int i = (int)fx, j = (int)fz; float u = fx - i, v = fz - j;
            return Mathf.Lerp(Mathf.Lerp(heights[j, i], heights[j, i + 1], u), Mathf.Lerp(heights[j + 1, i], heights[j + 1, i + 1], u), v);
        }
        public float SampleY(float x, float z) => terrain ? terrain.SampleHeight(new Vector3(x, 0, z)) + terrain.transform.position.y : HeightAt(x, z);
        public static bool Covers(float x) => x > X0;

        // ---- building it ------------------------------------------------------------------------------------------
        public static VentField Build(Seabed main, int seed)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var vf = new GameObject("The Thermal Vents").AddComponent<VentField>(); I = vf;
            vf.seed = seed; vf.main = main;
            var rnd = new System.Random(seed * 131 + 7);
            // the rift's line: a slow meander across the field's middle
            vf.riftZ = new float[(int)((X1 - X0) / 8f) + 2];
            float z0 = 560f + (float)rnd.NextDouble() * 400f, ph = (float)rnd.NextDouble() * 6f;
            for (int i = 0; i < vf.riftZ.Length; i++) { float x = i * 8f; vf.riftZ[i] = z0 + 180f * Mathf.Sin(x * 0.0042f + ph) + 60f * Mathf.Sin(x * 0.011f + ph * 2f); }
            vf.BuildTerrain();
            long tTerrain = sw.ElapsedMilliseconds;
            vf.PlaceVents(rnd);
            Debug.Log($"DEEP VENTS: the field's terrain in {tTerrain} ms; {vf.vents.Count} vents ({vf.vents.FindAll(v => v.geyser).Count} geysers), {vf.glow.Count} glow lights; all {sw.ElapsedMilliseconds} ms");
            return vf;
        }

        void BuildTerrain()
        {
            var td = new TerrainData { heightmapResolution = Res };
            td.size = new Vector3(X1 - X0, Top - Bottom, Z1 - Z0);
            heights = new float[Res, Res];
            var h01 = new float[Res, Res];
            for (int j = 0; j < Res; j++)
                for (int i = 0; i < Res; i++)
                {
                    float x = X0 + i * (X1 - X0) / (Res - 1), z = Z0 + j * (Z1 - Z0) / (Res - 1);
                    float y = HeightAt(x, z);
                    heights[j, i] = y; h01[j, i] = Mathf.Clamp01((y - Bottom) / (Top - Bottom));
                }
            td.SetHeights(0, 0, h01);
            td.terrainLayers = SeabedTextures.Layers();
            td.alphamapResolution = 512;
            // splat: 0 basalt (smooth flows), 1 rock (steep), 2 ash and sulphur (the plateaus), 3 heat (the rift: where
            // the cracks glow)
            int r = td.alphamapResolution; var a = new float[r, r, 4];
            for (int j = 0; j < r; j++)
                for (int i = 0; i < r; i++)
                {
                    float u = i / (r - 1f), v = j / (r - 1f), x = X0 + u * (X1 - X0), z = Z0 + v * (Z1 - Z0);
                    float steep = td.GetSteepness(u, v);
                    float rock = Mathf.SmoothStep(0, 1, (steep - 22f) / 14f);
                    float heat = Mathf.Exp(-Mathf.Pow(Mathf.Abs(z - RiftZ(x)) / 55f, 2)) * Mathf.SmoothStep(0, 1, (x - X0) / 200f);
                    float ash = (1 - heat) * Mathf.SmoothStep(0, 1, (Nz(x, z, 0.02f, 9) - 0.5f) * 4f);
                    float bas = Mathf.Max(0, 1 - ash - heat);
                    float w0 = bas * (1 - rock), w1 = rock + 0.03f, w2 = ash * (1 - rock), w3 = heat * (1 - rock * 0.5f), s = w0 + w1 + w2 + w3;
                    a[j, i, 0] = w0 / s; a[j, i, 1] = w1 / s; a[j, i, 2] = w2 / s; a[j, i, 3] = w3 / s;
                }
            td.SetAlphamaps(0, 0, a);
            var go = Terrain.CreateTerrainGameObject(td);
            go.name = "VentsTerrain"; go.transform.SetParent(transform);
            go.transform.position = new Vector3(X0, Bottom, Z0);
            terrain = go.GetComponent<Terrain>();
            var mat = new Material(Resources.Load<Shader>("Shaders/Seabed"));
            mat.SetVector("_Tiles", new Vector4(7, 9, 5, 8)); mat.SetFloat("_Basalt", 1f);
            terrain.materialTemplate = mat;
            terrain.heightmapPixelError = 8; terrain.basemapDistance = 20000; terrain.drawInstanced = false;
            terrain.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        }

        // ---- the vents ----------------------------------------------------------------------------------------------
        Material ventMat, smokeMat, steamMat;
        void PlaceVents(System.Random rnd)
        {
            float R() => (float)rnd.NextDouble();
            ventMat = new Material(DeepShaders.Get("Deep/Lit")) { name = "vent crust" };
            ventMat.SetFloat("_UseVC", 1); ventMat.SetFloat("_VCAlbedo", 1); ventMat.SetFloat("_Roughness", 0.85f); ventMat.SetFloat("_GlowVA", 2.2f); ventMat.SetFloat("_Cull", 2);
            smokeMat = new Material(Resources.Load<Shader>("Shaders/Smoke")) { name = "smoke" };
            steamMat = new Material(Resources.Load<Shader>("Shaders/Smoke")) { name = "steam" };
            var root = new GameObject("Vents").transform; root.SetParent(transform, false);
            // clusters of smokers along the rift (and a few on the ridges), geysers in the rift
            for (int c = 0; c < 14; c++)
            {
                float x = X0 + 180f + R() * (X1 - X0 - 300f);
                bool rift = c < 10;
                float z = rift ? RiftZ(x) + (R() - 0.5f) * 60f : Z0 + 120f + R() * (Z1 - Z0 - 240f);
                int n = 3 + rnd.Next(4);
                for (int k = 0; k < n; k++)
                {
                    var p = new Vector3(x + (R() - 0.5f) * 40f, 0, z + (R() - 0.5f) * 40f);
                    p.y = HeightAt(p.x, p.z);
                    float pick = R();
                    string id = pick < 0.25f ? "chimney_tall" : pick < 0.65f ? "chimney_mid" : pick < 0.9f ? "chimney_short" : "vent_mound";
                    float h = id == "chimney_tall" ? 22f : id == "chimney_mid" ? 12f : id == "chimney_short" ? 6f : 1.5f;
                    float s = 0.8f + R() * 0.5f;
                    var v = new Vent { pos = p, height = h * s, radius = 1.5f * s };
                    v.model = Piece(root, id, p, R() * 360f, s);
                    vents.Add(v);
                    glow.Add((p + Vector3.up * (v.height + 1.5f), new Color(1f, 0.42f, 0.12f) * 1.4f, 10f));
                }
                if (rift)
                {
                    // a geyser near each rift cluster, and basalt columns about
                    var g = new Vector3(x + (R() - 0.5f) * 70f, 0, RiftZ(x) + (R() - 0.5f) * 30f); g.y = HeightAt(g.x, g.z);
                    var gv = new Vent { pos = g, geyser = true, height = 1.6f, radius = 3f, period = 25f + R() * 35f, phase = R() * 40f };
                    gv.model = Piece(root, "geyser_cone", g, R() * 360f, 1f);
                    vents.Add(gv);
                    for (int k = 0; k < 2; k++) { var b = new Vector3(x + (R() - 0.5f) * 90f, 0, z + (R() - 0.5f) * 90f); b.y = HeightAt(b.x, b.z); Piece(root, "basalt_columns", b, R() * 360f, 1f + R()); }
                    for (int k = 0; k < 4; k++) { var b = new Vector3(x + (R() - 0.5f) * 60f, 0, RiftZ(x) + (R() - 0.5f) * 40f); b.y = HeightAt(b.x, b.z); Piece(root, "glass_shard", b, R() * 360f, 0.8f + R()); }
                }
            }
            // the magma's glow along the rift's floor (the cracks the shader lights)
            for (float x = X0 + 160f; x < X1 - 80f; x += 26f)
            {
                var p = new Vector3(x, 0, RiftZ(x) + (R() - 0.5f) * 20f); p.y = HeightAt(p.x, p.z) + 3f;
                glow.Add((p, new Color(1f, 0.3f, 0.06f) * 1.1f, 13f));
            }
        }

        Transform Piece(Transform root, string id, Vector3 at, float yaw, float scale)
        {
            var m = ModelLibrary.Get("Vents/vents", id); if (!m) return null;
            var g = new GameObject(id); g.transform.SetParent(root, false);
            g.transform.SetPositionAndRotation(at - Vector3.up * 0.3f, Quaternion.Euler(0, yaw, 0)); g.transform.localScale = Vector3.one * scale;
            g.AddComponent<MeshFilter>().sharedMesh = m;
            var mr = g.AddComponent<MeshRenderer>(); mr.sharedMaterial = ventMat; mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            var c = g.AddComponent<CapsuleCollider>(); c.center = m.bounds.center; c.height = m.bounds.size.y; c.radius = Mathf.Min(m.bounds.extents.x, m.bounds.extents.z) * 0.7f;
            return g.transform;
        }

        // the plume: black mineral cloud boiling up out of a smoker (or a geyser's steam), made while it's near the eye
        ParticleSystem MakePlume(Vent v)
        {
            var go = new GameObject(v.geyser ? "Geyser pillar" : "Smoker plume"); go.transform.SetParent(transform, false);
            go.transform.position = v.pos + Vector3.up * v.height;
            var ps = go.AddComponent<ParticleSystem>(); ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = ps.main; main.simulationSpace = ParticleSystemSimulationSpace.World; main.maxParticles = v.geyser ? 600 : 260;
            main.startLifetime = v.geyser ? 3.5f : 9f; main.startSpeed = v.geyser ? new ParticleSystem.MinMaxCurve(14f, 22f) : new ParticleSystem.MinMaxCurve(1.5f, 3f);
            main.startSize = v.geyser ? new ParticleSystem.MinMaxCurve(1.2f, 3.5f) : new ParticleSystem.MinMaxCurve(0.8f, 2.2f);
            main.startColor = v.geyser ? new Color(0.75f, 0.85f, 0.88f, 0.35f) : new Color(0.04f, 0.035f, 0.03f, 0.75f);
            var em = ps.emission; em.rateOverTime = v.geyser ? 0 : 26;
            var sh = ps.shape; sh.shapeType = ParticleSystemShapeType.Cone; sh.angle = v.geyser ? 6f : 12f; sh.radius = v.radius * 0.35f; sh.rotation = new Vector3(-90, 0, 0);
            var sz = ps.sizeOverLifetime; sz.enabled = true; sz.size = new ParticleSystem.MinMaxCurve(1f, new AnimationCurve(new Keyframe(0, 0.6f), new Keyframe(1, v.geyser ? 2.2f : 4.5f)));
            var col = ps.colorOverLifetime; col.enabled = true;
            var g = new Gradient(); g.SetKeys(new[] { new GradientColorKey(Color.white, 0), new GradientColorKey(Color.white, 1) }, new[] { new GradientAlphaKey(0, 0), new GradientAlphaKey(1, 0.1f), new GradientAlphaKey(0.7f, 0.6f), new GradientAlphaKey(0, 1) });
            col.color = g;
            var noise = ps.noise; noise.enabled = true; noise.strength = v.geyser ? 0.8f : 0.6f; noise.frequency = 0.3f;
            var r = go.GetComponent<ParticleSystemRenderer>(); r.sharedMaterial = v.geyser ? steamMat : smokeMat; r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            ps.Play();
            return ps;
        }

        // ---- heat and turbulence -------------------------------------------------------------------------------------
        // degrees over the sea's own at a point: over a smoker's throat and up its plume, in an erupting pillar
        public float HeatAt(Vector3 p)
        {
            if (p.x < X0 + 60f) return 0;
            float h = 0;
            foreach (var v in vents)
            {
                float dx = p.x - v.pos.x, dz = p.z - v.pos.z; float r2 = dx * dx + dz * dz;
                if (r2 > 900f) continue;
                float top = v.pos.y + v.height, up = p.y - top;
                if (v.geyser)
                {
                    if (v.state == 2 && up > -3f && up < 70f) h = Mathf.Max(h, 95f * Mathf.Exp(-r2 / (2f * 4.5f * 4.5f)));
                    else h = Mathf.Max(h, 25f * Mathf.Exp(-r2 / 8f) * Mathf.Exp(-Mathf.Max(0, up) / 4f));
                }
                else
                {
                    float spread = 2f + Mathf.Max(0, up) * 0.18f;
                    if (up > -v.height && up < 45f) h = Mathf.Max(h, (up < 0 ? 35f : 80f * Mathf.Exp(-up / 25f)) * Mathf.Exp(-r2 / (2f * spread * spread)));
                }
            }
            // the rift's floor is warm everywhere
            float d = RiftDist(p), above = p.y - SampleY(p.x, p.z);
            h += 6f * Mathf.Exp(-d * d / (2f * 45f * 45f)) * Mathf.Exp(-Mathf.Max(0, above) / 15f);
            return h;
        }

        // the water's push round an eruption (and the worm's burst): m/s^2
        public Vector3 TurbulenceAt(Vector3 p)
        {
            var f = Vector3.zero;
            foreach (var v in vents)
            {
                if (!v.Erupting) continue;
                var d = p - v.pos; float r = new Vector2(d.x, d.z).magnitude;
                if (r > 28f || d.y > 80f || d.y < -5f) continue;
                float k = (1f - r / 28f);
                float t = Time.time * 3.1f + v.phase;
                f += (Vector3.up * 5f + new Vector3(Mathf.Sin(t), 0, Mathf.Cos(t * 1.3f)) * 9f + new Vector3(d.x, 0, d.z).normalized * 4f) * k;
            }
            if (worm != null && worm.state >= 1 && worm.state <= 2) { var d = p - worm.lair; if (d.magnitude < 25f) f += new Vector3(Mathf.Sin(Time.time * 17f), Mathf.Sin(Time.time * 11f), Mathf.Cos(Time.time * 13f)) * 3f * (1 - d.magnitude / 25f); }
            return f;
        }

        // ---- the Boiler Worm --------------------------------------------------------------------------------------------
        public class Worm { public Vector3 lair; public Creature c; public int state; public float t, cool; public Vector3 aim; public string name; }
        public Worm worm;

        // the worm's lair: in the rift, and its creature (Life makes the animal; the field drives it)
        public void Settle(Life life)
        {
            if (life == null) return;
            Creature w = null;
            foreach (var c in life.live) if (c.persistent && c.sp.biome == "vents") w = c;
            if (w == null) return;
            float x = X0 + 400f + (float)new System.Random(seed).NextDouble() * 400f;
            var lair = new Vector3(x, 0, RiftZ(x)); lair.y = HeightAt(lair.x, lair.z);
            worm = new Worm { lair = lair, c = w, name = w.sp.e.name };
            w.forced = true; w.held = true; w.home = lair; w.pos = lair - Vector3.up * (w.size * 0.6f); w.fwd = new Vector3(0, 1, 0.08f).normalized;
            Debug.Log($"DEEP VENTS: the {w.sp.e.name} ({w.size:0} m) waits in the rock at {lair}");
        }

        public void WormStep(float dt)
        {
            var w = worm; if (w == null || w.c == null || !w.c.alive) return;
            var c = w.c; float L = c.size;
            w.t += dt; w.cool -= dt;
            switch (w.state)
            {
                case 0:   // waiting in the rock: something moving loud near the lair?
                {
                    c.pos = Vector3.Lerp(c.pos, w.lair - Vector3.up * (L * 0.6f), dt);
                    c.state = CState.Dormant;
                    if (w.cool > 0 || Net.IsGuest) break;
                    foreach (var (pos, loud) in Vibrations())
                        if (loud && (pos - w.lair).sqrMagnitude < 26f * 26f) { w.state = 1; w.t = 0; w.aim = pos; Sfx.Shared("worm_rumble", w.lair, 1f, 1f, Medium.Water); break; }
                    break;
                }
                case 1:   // the warning: the rock shudders, bubbles, a rumble (two seconds)
                    c.state = CState.Alert;
                    if (w.t > 2f) { w.state = 2; w.t = 0; Sfx.Shared("worm_burst", w.lair, 1f, 1f, Medium.Water); Life.I?.sound.Emit(w.lair, 100f, Band.Low, 1.2f, "the Boiler Worm erupting"); }
                    break;
                case 2:   // erupting: it rears out of the rock at what woke it, and strikes
                {
                    c.state = CState.Hunting;
                    var to = (w.aim + Vector3.down * 1f - w.lair); var dir = to.sqrMagnitude > 0.01f ? to.normalized : Vector3.up;
                    float k = Mathf.Clamp01(w.t / 0.7f);
                    c.pos = w.lair + dir * (L * 0.45f * k) + Vector3.up * (L * 0.15f * k);
                    c.fwd = Vector3.Slerp(Vector3.up, dir, 0.6f).normalized;
                    if (w.t > 0.5f && w.t < 0.9f && !Net.IsGuest) Strike(c.pos + c.fwd * L * 0.4f, w.name);
                    if (w.t > 4.5f) { w.state = 3; w.t = 0; }
                    break;
                }
                case 3:   // back into the rock
                    c.pos = Vector3.Lerp(c.pos, w.lair - Vector3.up * (L * 0.6f), dt * 1.5f);
                    c.state = CState.Dormant;
                    if (w.t > 2f) { w.state = 0; w.cool = 25f; }
                    break;
            }
        }

        // what the worm can feel: divers moving (loud when swimming hard), the Nautilus's engine, the Kite-Sub's props
        IEnumerable<(Vector3 pos, bool loud)> Vibrations()
        {
            var life = Life.I;
            if (life != null) foreach (var d in life.divers) if (d != null && !d.Inside) yield return (d.Eye, d.Velocity.magnitude > 1.4f);
            var ship = Nautilus.I; if (ship && ship.sys != null) yield return (ship.Body.position, ship.sys.NoiseDb > 40f);
            var k = KiteSub.I; if (k && !k.docked) yield return (k.transform.position, k.vel.magnitude > 1.5f);
        }

        void Strike(Vector3 at, string by)
        {
            var life = Life.I;
            if (life != null) foreach (var d in life.divers)
                {
                    if (d == null || d.Inside || (d.Eye - at).sqrMagnitude > 5f * 5f) continue;
                    if (d is Diver dv) dv.Hurt(60f, by); else Net.HurtMate(d, 60f, by);
                    Sfx.Shared("bite", d.Eye, 1.2f, 0.6f, Medium.Water);
                }
            var ship = Nautilus.I;
            if (ship && ship.sys != null)
            {
                var lp = ship.Body.InverseTransformPoint(at);
                if (lp.z > Nautilus.SternX - 4f && lp.z < Nautilus.BowX + 4f && new Vector2(lp.x, lp.y).magnitude < Nautilus.Radius + 6f && worm.t < 0.55f)
                {
                    ship.sys.AddBreach(ship.sys.RoomIndexAt(Nautilus.FromLocal(new Vector3(0, 0, lp.z))), 0.05f);
                    ship.groundedMsg = $"The {by} bursts from the rock and strikes the hull!";
                }
            }
            var k = KiteSub.I;
            if (k && !k.docked && (k.transform.position - at).sqrMagnitude < 36f) k.hull = Mathf.Max(0, k.hull - 0.35f);
        }

        // ---- every frame ------------------------------------------------------------------------------------------------
        float hazT;
        void Update()
        {
            float dt = Time.deltaTime;
            var cam = Camera.main ? Camera.main.transform.position : Vector3.zero;
            foreach (var v in vents)
            {
                // plumes only near the eye
                bool near = (v.pos - cam).sqrMagnitude < 160f * 160f;
                if (near && v.plume == null) v.plume = MakePlume(v);
                if (!near && v.plume != null) { Destroy(v.plume.gameObject); v.plume = null; }
                if (!v.geyser) continue;
                // geysers: the same clock on every PC (the sea's time)
                float t = Mathf.Repeat(Waves.T + v.phase, v.period);
                int st = t > v.period - 6f ? 2 : t > v.period - 8f ? 1 : 0;
                if (st != v.state)
                {
                    if (st == 1) Sfx.Play("geyser_rumble", v.pos, 1f, 1f, Medium.Water);
                    if (st == 2) { Sfx.Play("geyser", v.pos + Vector3.up * 4f, 1f, 1f, Medium.Water); if (!Net.IsGuest) Life.I?.sound.Emit(v.pos, 96f, Band.Low, 6f, "a geyser"); }
                    v.state = st;
                }
                if (v.plume != null) { var em = v.plume.emission; em.rateOverTime = v.state == 2 ? 180 : v.state == 1 ? 25 : 4; }
            }
            WormStep(dt);
            hazT -= dt; if (hazT > 0) return; hazT = 0.25f;
            Hazards(0.25f);
        }

        // heat on the diver, the Nautilus and the Kite-Sub; turbulence throwing them about
        void Hazards(float dt)
        {
            var d = DeepBoot.I ? DeepBoot.I.diver : null; if (!d) return;
            if (!d.Inside && d.EyeWorld.x > X0)
            {
                float h = HeatAt(d.EyeWorld);
                if (h > 22f) d.Hurt((h - 22f) * 0.12f * dt * 4f, "scalding water");
                var turb = TurbulenceAt(d.EyeWorld); if (turb.sqrMagnitude > 0.1f) d.vel += turb * dt;
            }
            if (Net.IsGuest) return;
            var ship = Nautilus.I;
            if (ship && ship.sys != null && ship.Body.position.x > X0 - 40f)
            {
                float h = 0;
                for (float s = -30f; s <= 40f; s += 14f) h = Mathf.Max(h, HeatAt(ship.WorldPoint(new Vector3(0, -Nautilus.Radius, s))));
                ship.sys.Heat(h, dt);
                var turb = TurbulenceAt(ship.Body.position); if (turb.sqrMagnitude > 0.1f) ship.Shove(turb * dt * 0.25f);
            }
            var k = KiteSub.I;
            if (k && !k.docked && k.transform.position.x > X0)
            {
                float h = HeatAt(k.transform.position);
                if (h > 30f) { k.hull = Mathf.Max(0, k.hull - (h - 30f) * 0.0008f * dt * 4f); if (k.hull < 0.3f && Time.frameCount % 120 == 0) d.Toast("The Kite-Sub's seals are cooking!"); }
                var turb = TurbulenceAt(k.transform.position); if (turb.sqrMagnitude > 0.1f) k.vel += turb * dt * 1.4f;
            }
        }

        // the vents' lights for the lamp list (the throats' glow, the magma under the rift), nearest first
        public void LightsNear(Vector3 cam, List<(float d, Vector3 w, Color col, float range)> into, int max)
        {
            if (cam.x < X0 - 120f) return;
            var best = new List<(float, int)>();
            for (int i = 0; i < glow.Count; i++) { float dd = (glow[i].p - cam).magnitude; if (dd < glow[i].range + 30f) best.Add((dd, i)); }
            best.Sort((a, b) => a.Item1.CompareTo(b.Item1));
            int n = 0;
            for (int i = 0; i < best.Count && n < max; i++, n++) { var g = glow[best[i].Item2]; into.Add((best[i].Item1, g.p, g.col * (0.85f + 0.15f * Mathf.Sin(Time.time * 1.7f + i)), g.range)); }
            foreach (var v in vents) if (v.Erupting && (v.pos - cam).sqrMagnitude < 90f * 90f) into.Add(((v.pos - cam).magnitude, v.pos + Vector3.up * 8f, new Color(0.8f, 0.9f, 1f) * 1.2f, 22f));
        }

        // ---- the vents' growth: by the table's habitats, on the field the generator made ----------------------------------
        public void Plant(Flora flora)
        {
            BiomeTable tab = null; foreach (var t in SpeciesBook.Tables) if (t.biome == "vents") tab = t;
            if (tab == null || tab.flora == null) return;
            var rnd = new System.Random(seed * 89 + 4);
            float R() => (float)rnd.NextDouble();
            var by = new Dictionary<string, List<(int k, FloraEntry f)>>();
            foreach (var f in tab.flora)
            {
                string n = f.name.ToLowerInvariant();
                float glowK = f.light > 0 ? Mathf.Clamp(0.4f + Mathf.Log10(Mathf.Max(1f, f.light)) * 0.6f, 0.4f, 1.8f) : 0f;
                int k = flora.CaveKind(f.id, n.Contains("grass") || n.Contains("filament") ? 0.3f : 0.03f, 0.3f, 75f, glowK, f.pulse, "Flora/flora_vents");
                if (k < 0) continue;
                string hab = f.habitat ?? "basalt";
                if (!by.TryGetValue(hab, out var l)) by[hab] = l = new List<(int, FloraEntry)>();
                l.Add((k, f));
            }
            (int k, FloraEntry f) Pick(string hab) { if (!by.TryGetValue(hab, out var l) || l.Count == 0) return (-1, null); return l[rnd.Next(l.Count)]; }
            int planted = 0;
            void Put(string hab, Vector3 p, Vector3 nrm, float scale)
            {
                var (k, f) = Pick(hab); if (k < 0) return;
                flora.Plant(k, p, Quaternion.FromToRotation(Vector3.up, nrm) * Quaternion.Euler(0, R() * 360f, 0), scale);
                planted++;
                if (f.light > 0 && R() < 0.3f) { var cc = f.color == "white" ? new Color(1f, 0.95f, 0.85f) : f.color == "red" ? new Color(1f, 0.2f, 0.08f) : new Color(1f, 0.5f, 0.15f); glow.Add((p + nrm, cc * 0.8f, 6f)); }
            }
            // on the chimneys, up their sides; spores and blooms drifting over their throats
            foreach (var v in vents)
            {
                if (v.geyser) continue;
                for (int i = 0; i < 10 + (int)v.height; i++)
                {
                    float a = R() * 6.283f, h = R() * v.height * 0.9f, rr = v.radius * (1.2f - 0.5f * h / Mathf.Max(1f, v.height)) + 0.1f;
                    var nrm = new Vector3(Mathf.Cos(a), 0.2f, Mathf.Sin(a)).normalized;
                    Put("chimney", v.pos + new Vector3(Mathf.Cos(a) * rr, h, Mathf.Sin(a) * rr), nrm, 0.7f + R() * 0.7f);
                }
                for (int i = 0; i < 3; i++) Put("plume", v.pos + new Vector3((R() - 0.5f) * 4f, v.height + 2f + R() * 6f, (R() - 0.5f) * 4f), Vector3.up, 0.8f + R());
            }
            // across the field: cracks in the rift, basalt on the slopes and plateaus, sediment on the flats
            for (float z = Z0 + 20f; z < Z1 - 20f; z += 7f)
                for (float x = X0 + 60f; x < X1 - 20f; x += 7f)
                {
                    float jx = x + (R() - 0.5f) * 6f, jz = z + (R() - 0.5f) * 6f;
                    if (R() > 0.35f) continue;
                    float y = HeightAt(jx, jz);
                    float sx = HeightAt(jx + 1.5f, jz) - HeightAt(jx - 1.5f, jz), sz = HeightAt(jx, jz + 1.5f) - HeightAt(jx, jz - 1.5f);
                    var nrm = new Vector3(-sx / 3f, 1f, -sz / 3f).normalized;
                    float rift = Mathf.Abs(jz - RiftZ(jx));
                    string hab = rift < 45f && R() < 0.7f ? "crack" : nrm.y < 0.82f ? "basalt" : R() < 0.5f ? "sediment" : "basalt";
                    Put(hab, new Vector3(jx, y - 0.05f, jz), nrm, 0.8f + R() * 0.8f);
                }
            Debug.Log($"DEEP VENTS: {planted} plants on the field and the chimneys");
        }

        // the vents near a point (the sea life's spawning near the smokers)
        public bool NearVent(Vector3 around, float rMin, float rMax, System.Random r, out Vent v)
        {
            v = null; float best = float.MaxValue;
            foreach (var x in vents)
            {
                float d = (x.pos - around).magnitude;
                if (d < rMin || d > rMax) continue;
                float score = d + (float)r.NextDouble() * 60f;
                if (score < best) { best = score; v = x; }
            }
            return v != null;
        }
    }
}
