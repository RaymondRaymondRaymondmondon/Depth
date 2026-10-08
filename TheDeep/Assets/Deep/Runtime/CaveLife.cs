// What fills the Caverns (stage 9), on the walls Caverns.cs built:
//   - the glow: the doc's twenty cave plants planted on the rock by habitat (vines and pouches hanging from the
//     ceilings, mats, lichens, shelves and filaments on the walls, caps, spires, tubules and clusters on the floors and
//     ledges), each glowing in its colour at its lux (Flora.shader; the pulsing ones pulse); their light gathered into
//     cluster lights that really light the wet walls (Nautilus.cs takes the nearest into its lamp list);
//   - the Abandoned City in the biggest cathedral: a grand plaza with a statue, a main street of columns and arches
//     overgrown with Lantern Vines and Phosphor Mats (bright), walls and broken columns along dark alleys, stairs, fallen
//     blocks; its salvage (Smooth Obsidian Stonework, Resonance Stones), and the Echo-Rays' egg clusters in the caves;
//   - the hazards (the doc): echoes (Acoustics adds 8 dB to anything heard down here), cave-ins (a loud enough noise can
//     bring the ceiling down: falling rock that hurts, a dust cloud, rubble), blinding strobes (strobing hunters and the
//     Flash-Stalker light the caves white as they close in; a Strobe Shroom bursts when brushed past), and the Acid Vine
//     that burns bare skin.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public partial class Caverns
    {
        // ---- the glow ---------------------------------------------------------------------------------------------
        public readonly List<(Vector3 p, Color col, float range)> glow = new List<(Vector3, Color, float)>();
        readonly List<Vector3> strobeShrooms = new List<Vector3>(), acidVines = new List<Vector3>();
        readonly Dictionary<int, float> shroomCool = new Dictionary<int, float>();

        static Color ColorOf(string c)
        {
            switch (c)
            {
                case "cyan": return new Color(0.2f, 0.95f, 1f); case "magenta": return new Color(1f, 0.25f, 0.85f); case "amber": return new Color(1f, 0.62f, 0.18f);
                case "green": return new Color(0.45f, 1f, 0.35f); case "white": return new Color(0.92f, 0.95f, 1f); case "red": return new Color(1f, 0.25f, 0.2f);
                case "violet": return new Color(0.65f, 0.35f, 1f);
            }
            return new Color(0.25f, 0.55f, 1f);
        }

        public void Plant(Flora flora, int seed)
        {
            BiomeTable tab = null; foreach (var t in SpeciesBook.Tables) if (t.biome == "caverns") tab = t;
            if (tab == null || tab.flora == null) { Debug.LogWarning("DEEP CAVERNS: no flora table"); return; }
            var rnd = new System.Random(seed * 211 + 9);
            float R() => (float)rnd.NextDouble();
            // the kinds, sorted by where they grow, with how common each is
            var byHab = new Dictionary<string, List<(int kind, FloraEntry f, float w)>>();
            foreach (var f in tab.flora)
            {
                string n = f.name.ToLowerInvariant();
                float sway = n.Contains("filament") ? 0.5f : n.Contains("vine") ? 0.3f : 0.02f;
                float glowK = f.light > 0 ? Mathf.Clamp(0.35f + Mathf.Log10(Mathf.Max(1f, Mathf.Min(f.light, 300f))) * 0.6f, 0.3f, 1.9f) : 0f;
                if (n.Contains("strobe shroom")) glowK = 0.6f;   // (dim until it bursts)
                int k = flora.CaveKind(f.id, sway, 0.5f, n.Contains("spire") || n.Contains("beacon") ? 110f : 70f, glowK, f.pulse);
                if (k < 0) continue;
                string hab = f.habitat == "ledge" ? "floor" : f.habitat == "crevice" ? "wall" : f.habitat == "drifting" ? "ceiling" : f.habitat ?? "wall";
                float w = n.Contains("spire") || n.Contains("beacon") ? 0.25f : n.Contains("strobe") ? 0.35f : 1f;
                if (!byHab.TryGetValue(hab, out var l)) byHab[hab] = l = new List<(int, FloraEntry, float)>();
                l.Add((k, f, w));
            }
            // the cluster lights: what glows within each 8 m cell, summed
            var cells = new Dictionary<long, (Vector3 sum, Vector3 col, float lux, Vector3 n)>();
            int planted = 0;
            foreach (var (p, nrm) in surface)
            {
                if (R() > 0.32f) continue;
                string hab = nrm.y < -0.5f ? "ceiling" : nrm.y > 0.5f ? "floor" : "wall";
                if (!byHab.TryGetValue(hab, out var opts) || opts.Count == 0) continue;
                // the city's floor is swept stone: only its streets get the overgrowth (the city builder does that)
                var cityCh = City; if (cityCh != null && (p - cityCh.c).sqrMagnitude < cityCh.r.x * cityCh.r.x && hab == "floor") continue;
                float tot = 0; foreach (var o in opts) tot += o.w;
                float pick = R() * tot; var ch = opts[0];
                foreach (var o in opts) { pick -= o.w; if (pick <= 0) { ch = o; break; } }
                float sc = 0.7f + 0.8f * R();
                var rot = Quaternion.FromToRotation(Vector3.up, nrm) * Quaternion.Euler(0, R() * 360f, 0);
                flora.Plant(ch.kind, p - nrm * 0.05f, rot, sc);
                planted++;
                string nm = ch.f.name.ToLowerInvariant();
                if (nm.Contains("strobe shroom")) strobeShrooms.Add(p);
                if (nm.Contains("acid vine")) acidVines.Add(p);
                if (ch.f.light > 0 && !nm.Contains("strobe"))
                {
                    long key = ((long)Mathf.FloorToInt(p.x / 8f) << 40) ^ ((long)Mathf.FloorToInt(p.y / 8f) << 20) ^ (uint)Mathf.FloorToInt(p.z / 8f);
                    cells.TryGetValue(key, out var c);
                    float lux = Mathf.Min(ch.f.light, 250f) * sc;
                    var cc = ColorOf(ch.f.color);
                    cells[key] = (c.sum + p * lux, c.col + new Vector3(cc.r, cc.g, cc.b) * lux, c.lux + lux, c.n + nrm * lux);
                }
            }
            foreach (var c in cells.Values)
            {
                if (c.lux < 30f) continue;
                var at = c.sum / c.lux + c.n.normalized * 1.2f;
                var cv = c.col / c.lux;
                float k = Mathf.Clamp(c.lux / 160f, 0.25f, 1.6f);
                glow.Add((at, new Color(cv.x, cv.y, cv.z) * k, Mathf.Clamp(3f + Mathf.Sqrt(c.lux) * 0.5f, 4f, 15f)));
            }
            Debug.Log($"DEEP CAVERNS: {planted} plants on the walls, {glow.Count} glow lights, {strobeShrooms.Count} strobe shrooms");
        }

        // the nearest glow (and any flash) for the lamp list
        public readonly List<(Vector3 p, float until, float lux)> flashes = new List<(Vector3, float, float)>();
        public void LightsNear(Vector3 cam, List<(float d, Vector3 w, Color col, float range)> into, int max)
        {
            int before = into.Count;
            for (int i = flashes.Count - 1; i >= 0; i--)
            {
                if (Time.time > flashes[i].until) { flashes.RemoveAt(i); continue; }
                float k = (flashes[i].until - Time.time) / 0.25f;
                into.Add(((flashes[i].p - cam).magnitude * 0.1f, flashes[i].p, Color.white * flashes[i].lux * k, 30f));
            }
            if (!UnderGround(cam) && (cam - new Vector3((X0 + X1) / 2, cam.y, (Z0 + Z1) / 2)).sqrMagnitude > 400f * 400f) return;
            var best = new List<(float, int)>();
            for (int i = 0; i < glow.Count; i++)
            {
                float d = (glow[i].p - cam).magnitude;
                if (d < glow[i].range + 25f) best.Add((d, i));
            }
            best.Sort((a, b) => a.Item1.CompareTo(b.Item1));
            for (int i = 0; i < best.Count && into.Count - before < max; i++) { var g = glow[best[i].Item2]; into.Add((best[i].Item1, g.p, g.col, g.range)); }
        }

        // ---- the Abandoned City ------------------------------------------------------------------------------------
        public readonly List<Vector3> cityStreetLights = new List<Vector3>();
        public void BuildCity(Flora flora, int seed)
        {
            var ch = City; if (ch == null) return;
            var rnd = new System.Random(seed * 53 + 3);
            float R() => (float)rnd.NextDouble();
            var mat = new Material(DeepShaders.Get("Deep/Lit")) { name = "obsidian" };
            mat.SetFloat("_UseVC", 1); mat.SetFloat("_VCAlbedo", 1); mat.SetFloat("_Roughness", 0.15f); mat.SetFloat("_Metallic", 0.3f); mat.SetFloat("_Cull", 0);
            var root = new GameObject("The Abandoned City").transform; root.SetParent(transform, false);
            int pieces = 0;
            Transform Piece(string id, Vector3 at, float yaw, float scale = 1f)
            {
                var m = ModelLibrary.Get("Caverns/city", id); if (!m) return null;
                // set it on the floor below the spot
                if (!Surface(at + Vector3.up * 2f, -1, 30f, out var fl)) return null;
                if (Sdf(fl + Vector3.up * 3f) > -1f) return null;   // (no room)
                var g = new GameObject(id); g.transform.SetParent(root, false);
                g.transform.SetPositionAndRotation(fl - Vector3.up * 0.15f, Quaternion.Euler(0, yaw, 0)); g.transform.localScale = Vector3.one * scale;
                g.AddComponent<MeshFilter>().sharedMesh = m;
                var mr = g.AddComponent<MeshRenderer>(); mr.sharedMaterial = mat;
                var bc = g.AddComponent<BoxCollider>(); bc.center = m.bounds.center; bc.size = m.bounds.size * 0.9f;
                pieces++;
                return g.transform;
            }
            var fwd = Quaternion.Euler(0, R() * 360f, 0) * Vector3.forward; var side = Vector3.Cross(Vector3.up, fwd);
            float yaw0 = Mathf.Atan2(fwd.x, fwd.z) * Mathf.Rad2Deg;
            var c = ch.c;
            // the plaza and its statue
            Piece("city_plinth", c, yaw0); Piece("city_statue", c + Vector3.up * 1.2f, yaw0 + 180f);
            for (int k = 0; k < 8; k++) { float a = k * 45f; Piece(k % 3 == 0 ? "city_column_broken" : "city_column", c + Quaternion.Euler(0, a, 0) * fwd * 14f, a); }
            // the main street: columns and arches either side, out both ways (bright: overgrown)
            for (int s = -1; s <= 1; s += 2)
                for (int i = 1; i <= 4; i++)
                {
                    var along = c + fwd * s * (16f + i * 9f);
                    if ((along - c).magnitude > ch.r.x * 0.95f) break;
                    if (i % 2 == 0) Piece("city_arch", along, yaw0 + 90f);
                    else { Piece("city_column", along + side * 5f, yaw0); Piece("city_column", along - side * 5f, yaw0); }
                    cityStreetLights.Add(along);
                }
            // the quarters either side: walls along dark alleys, broken columns, stairs, fallen blocks
            for (int k = 0; k < 26; k++)
            {
                var off = fwd * (R() * 2 - 1) * ch.r.x * 0.85f + side * (R() < 0.5f ? -1 : 1) * (10f + R() * ch.r.z * 0.7f);
                var at = c + off; at.y = c.y;
                float r = R();
                string id = r < 0.45f ? "city_wall" : r < 0.6f ? "city_column_broken" : r < 0.72f ? "city_block" : r < 0.82f ? "city_stairs" : r < 0.9f ? "city_plinth" : "city_column";
                Piece(id, at, yaw0 + (R() < 0.5f ? 0 : 90) + (R() - 0.5f) * 12f);
            }
            // the overgrowth on the main street: Lantern Vines hanging and Phosphor Mats on the stone, so the street is
            // bright and the alleys black (and the light that comes of it)
            int vine = flora.KindIndex("lantern_vine"), mats = flora.KindIndex("phosphor_mat");
            foreach (var s in cityStreetLights)
            {
                for (int k = 0; k < 6; k++)
                {
                    var p = s + side * ((R() - 0.5f) * 10f) + fwd * ((R() - 0.5f) * 6f) + Vector3.up * 6f;
                    if (mats >= 0 && Surface(p, -1, 20f, out var fl)) flora.Plant(mats, fl, Quaternion.Euler(0, R() * 360, 0), 1f + R());
                    if (vine >= 0 && Surface(p, +1, 30f, out var cl)) flora.Plant(vine, cl, Quaternion.Euler(180, R() * 360, 0), 1.2f + R());
                }
                glow.Add((s + Vector3.up * 2f, new Color(0.3f, 0.75f, 1f) * 1.3f, 14f));
            }
            // the salvage the doc names, about the city
            if (Deposits.I != null)
                for (int k = 0; k < 18; k++)
                {
                    var at = c + Quaternion.Euler(0, R() * 360, 0) * Vector3.forward * (6f + R() * ch.r.x * 0.8f);
                    if (!Surface(at + Vector3.up * 2f, -1, 30f, out var fl)) continue;
                    bool res = k % 3 == 0;
                    Deposits.I.AddNode(res ? "Caverns/city:resonance_stone" : "Caverns/city:obsidian_block", res ? "Resonance Stone" : "Smooth Obsidian Stonework", "salvage", fl, R() * 360f, 1f, res ? 1 : 2, "the Abandoned City");
                }
            Debug.Log($"DEEP CAVERNS: the Abandoned City: {pieces} pieces of stonework in {ch.name}");
        }

        // the Echo-Rays' egg clusters on cave floors (collected by hand)
        public void Eggs(int seed)
        {
            if (Deposits.I == null) return;
            var rnd = new System.Random(seed * 17 + 1);
            int n = 0;
            for (int k = 0; k < surface.Count && n < 14; k += 1 + rnd.Next(1400))
            {
                var (p, nr) = surface[k];
                if (nr.y < 0.7f) continue;
                Deposits.I.AddNode("Caverns/city:echo_egg_cluster", "Echo-Ray Egg", "hand", p, (float)rnd.NextDouble() * 360f, 1f, 1, "an Echo-Ray egg cluster");
                n++;
            }
        }

        // ---- hazards ------------------------------------------------------------------------------------------------
        float caveInCool;
        System.Random hz = new System.Random(5);

        // a loud sound down here: the doc's cave-ins (the host decides; a crewmate's PC is told)
        public void Heard(Vector3 pos, float db)
        {
            if (Net.IsGuest || db < 78f || Time.time < caveInCool) return;
            if (hz.NextDouble() > 0.03 + (db - 78f) * 0.012) return;
            if (!Surface(pos, +1, 30f, out var ceil)) return;
            caveInCool = Time.time + 25f;
            int seed = hz.Next(1 << 20);
            CaveIn(ceil, seed);
            Net.CaveIn(ceil, seed);
        }

        class Rock { public Transform t; public Vector3 v; public bool down; public float until; }
        readonly List<Rock> rocks = new List<Rock>();
        Material rockMat;
        public void CaveIn(Vector3 ceil, int seed)
        {
            var r = new System.Random(seed);
            if (!rockMat) { rockMat = new Material(DeepShaders.Get("Deep/Lit")); rockMat.SetColor("_BaseColor", new Color(0.45f, 0.43f, 0.4f)); rockMat.SetFloat("_UseVC", 0); rockMat.SetFloat("_Roughness", 0.9f); }
            for (int k = 0; k < 7; k++)
            {
                var g = GameObject.CreatePrimitive(k % 2 == 0 ? PrimitiveType.Cube : PrimitiveType.Sphere); Destroy(g.GetComponent<Collider>());
                g.GetComponent<MeshRenderer>().sharedMaterial = rockMat;
                float s = 0.4f + (float)r.NextDouble() * 0.9f;
                g.transform.localScale = new Vector3(s, s * 0.8f, s * 1.1f);
                g.transform.SetPositionAndRotation(ceil + new Vector3((float)r.NextDouble() * 4 - 2, -(float)r.NextDouble(), (float)r.NextDouble() * 4 - 2), Quaternion.Euler((float)r.NextDouble() * 360, (float)r.NextDouble() * 360, 0));
                rocks.Add(new Rock { t = g.transform, v = Vector3.down * (float)r.NextDouble(), until = Time.time + 300f });
            }
            Dust(ceil);
            Sfx.Play("cave_in", ceil, 1f, 1f, Medium.Water);
        }

        void Dust(Vector3 at)
        {
            var go = new GameObject("Cave-in dust"); go.transform.position = at;
            var ps = go.AddComponent<ParticleSystem>(); ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = ps.main; main.startLifetime = 6f; main.startSpeed = new ParticleSystem.MinMaxCurve(0.2f, 1.5f); main.startSize = new ParticleSystem.MinMaxCurve(0.6f, 2.2f);
            main.startColor = new Color(0.5f, 0.47f, 0.4f, 0.35f); main.gravityModifier = 0.05f; main.maxParticles = 160; main.duration = 1.5f; main.loop = false;
            var em = ps.emission; em.rateOverTime = 90;
            var sh = ps.shape; sh.shapeType = ParticleSystemShapeType.Sphere; sh.radius = 3f;
            go.GetComponent<ParticleSystemRenderer>().sharedMaterial = new Material(Resources.Load<Shader>("Shaders/Snow"));
            ps.Play();
            Destroy(go, 9f);
        }

        // the strobes: hunters that flash light the caves white as they close in; a Strobe Shroom bursts when brushed
        readonly Dictionary<int, float> nextStrobe = new Dictionary<int, float>();
        float hazT;
        void Update()
        {
            float dt = Time.deltaTime;
            // falling rock
            var d = DeepBoot.I ? DeepBoot.I.diver : null;
            for (int i = rocks.Count - 1; i >= 0; i--)
            {
                var rk = rocks[i];
                if (Time.time > rk.until) { Destroy(rk.t.gameObject); rocks.RemoveAt(i); continue; }
                if (rk.down) continue;
                rk.v += Vector3.down * 6f * dt; rk.v *= Mathf.Exp(-dt * 0.6f);
                var np = rk.t.position + rk.v * dt;
                if (Sdf(np) > -0.3f) { rk.down = true; Sfx.Play("gather_ore", np, 0.8f, 0.6f, Medium.Water); continue; }
                rk.t.position = np; rk.t.Rotate(40f * dt, 25f * dt, 0);
                if (!Net.IsGuest)
                {
                    if (d && !d.Inside && (d.EyeWorld - np).sqrMagnitude < 2.2f) { d.Hurt(28f, "a cave-in"); rk.down = true; }
                    foreach (var m in Net.Crew()) if (m.InWorld && !m.Inside && (m.Eye - np).sqrMagnitude < 2.2f) { Net.HurtMate(m, 28f, "a cave-in"); rk.down = true; }
                }
            }
            hazT -= dt; if (hazT > 0 || !d) return; hazT = 0.2f;
            var cam = Camera.main ? Camera.main.transform : null; if (!cam) return;
            if (!UnderGround(cam.position) && !InCave(cam.position)) return;
            // strobing hunters near the eye
            var life = Life.I;
            if (life != null)
                foreach (var c in life.live)
                {
                    if (!c.alive || c.sp.e.light != "strobe") continue;
                    bool attack = c.state == CState.Hunting || c.state == CState.Frenzy || c.state == CState.Territorial;
                    float dist = (c.pos - cam.position).magnitude;
                    if (!attack || dist > 55f) continue;
                    nextStrobe.TryGetValue(c.id, out float at);
                    if (Time.time < at) continue;
                    nextStrobe[c.id] = Time.time + (c.persistent ? 3f : 2.4f) + (float)hz.NextDouble() * 3f;
                    float lux = c.persistent || c.size > 8f ? 1f : 0.35f;
                    float facing = Mathf.Clamp01(0.4f + 0.6f * Vector3.Dot(cam.forward, (c.pos - cam.position).normalized));
                    UnderwaterLook.Strobe = Mathf.Max(UnderwaterLook.Strobe, lux * (1f - dist / 55f) * facing * 1.4f);
                    flashes.Add((c.pos, Time.time + 0.25f, lux * 4f));
                    Sfx.Play("strobe", c.pos, lux, c.persistent ? 0.7f : 1.1f, Medium.Water);
                }
            // a Strobe Shroom brushed past; Acid Vine against bare skin
            if (!d.Inside)
            {
                var eye = d.EyeWorld; bool moving = d.vel.magnitude > 0.8f;
                for (int i = 0; i < strobeShrooms.Count; i++)
                {
                    if ((strobeShrooms[i] - eye).sqrMagnitude > 4f || !moving) continue;
                    shroomCool.TryGetValue(i, out float cool); if (Time.time < cool) continue;
                    shroomCool[i] = Time.time + 20f;
                    UnderwaterLook.Strobe = Mathf.Max(UnderwaterLook.Strobe, 0.9f);
                    flashes.Add((strobeShrooms[i], Time.time + 0.25f, 3f));
                    Sfx.Shared("strobe", strobeShrooms[i], 0.6f, 1.4f, Medium.Water);
                    life?.sound.Emit(strobeShrooms[i], 60f, Band.High, 0.5f, "a strobe shroom's burst");
                }
                foreach (var a in acidVines)
                    if ((a - eye).sqrMagnitude < 0.9f) { d.Hurt(1.5f, "an Acid Vine"); break; }
            }
        }
    }
}
