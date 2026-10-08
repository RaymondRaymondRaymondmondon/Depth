// The living sea near the crew (the design doc, "Living ecosystem simulation" and "Creature AI states").
// The biomass pools (Ecology) say how much of each level the biome holds; Life keeps the animals within ~150 m of the
// diver alive as individuals, spawning groups out of the pools at the edge of sight and letting them go again
// beyond it. The two biomes' leviathans are always alive, patrolling their territories.
//
// Each animal moves between the doc's six states:
//   Dormant      grazing, resting, schooling, wandering its home range (the default)
//   Alert        noise 15 dB over the ambient in a band it hears, or scent over 0.1 ppm: turns toward it, approaches
//   Hunting      hunger over 60% and prey (or a diver) in its senses: species tactics (chase, ambush, ram)
//   Frenzy       blood over 1.5 ppm: strikes anything that moves, the Nautilus's hull included
//   Territorial  an intruder within 50 m of its nest: a warning display, then the attack
//   Fleeing      health under 25%, an apex rumble over 90 dB, or a hunter closing: runs for the rocks and weed
// Hunger rises with time and with the pool's state (starving hunters: senses x3.5, no fear, they ram the hull).
// Daily migration: night-active animals rise at dusk; the Kelp's hunters come up into the Shallows in the dark.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public enum CState { Dormant, Alert, Hunting, Frenzy, Territorial, Fleeing }

    // a diver the sea can sense: this PC's own (Diver) or a crewmate's, seen through the network (Mate)
    public interface ISense
    {
        Vector3 Eye { get; }          // the eye, in the world
        bool Inside { get; }          // aboard the Nautilus or in the Kite-Sub: out of reach
        Vector3 Velocity { get; }
        bool Lamp { get; }            // the helmet lamp is on, in the water
        Vector3 Look { get; }         // where the eye is looking
    }

    public class Creature
    {
        public int id;            // the same animal on every PC (the host numbers them)
        public SpeciesDef sp; public Vector3 pos, vel, fwd = Vector3.forward, goal, home, nest;
        public ISense diverT;     // the diver it's after, when targetDiver
        public Vector3 netPos, netVel, netFwd; public float netT, seenT;   // a crewmate's PC: the host's last word on it
        public CState state; public float hunger, health = 1f, stateT, thinkT, biteCool, displayT, size;
        public Creature target; public bool targetDiver, hasNest, alive = true, persistent;
        public int group;
        public bool forced;     // driven from outside (the opening's surface hunt): Think leaves it alone
        public string why;      // what set the current state off (for the tests and the HUD)
        public float visitT;    // time spent at a cleaning station
        public Creature station;
    }

    public class Life : MonoBehaviour
    {
        public static Life I;
        public Ecology eco; public Acoustics sound; public Scent scent;
        public readonly List<Creature> live = new List<Creature>();
        public const float Radius = 150f, Despawn = 195f;
        public int maxLive = 420;
        Seabed bed; Clock clock; Diver diver; Nautilus ship;
        public readonly List<ISense> divers = new List<ISense>();   // everyone in the crew (the host's sea senses them all)
        public bool mirror;       // a crewmate's PC: the host runs the sea; this one draws what it's told (Net.cs)
        public readonly Dictionary<int, Creature> byId = new Dictionary<int, Creature>();
        int nextId = 1;
        readonly Dictionary<SpeciesDef, (Mesh mesh, Material mat)> looks = new Dictionary<SpeciesDef, (Mesh, Material)>();
        readonly List<Matrix4x4> batch = new List<Matrix4x4>(1023);
        float spawnT, scentT, ecoT, hullT; int nextGroup = 1;
        System.Random rnd;

        class DiverNoise : Acoustics.ISource
        {
            public ISense d;
            public bool Sounding(out Vector3 pos, out float db, out Band band)
            {
                pos = d.Eye; band = Band.Low;
                float v = d.Inside ? 0 : d.Velocity.magnitude;
                db = v < 0.4f ? 0 : 38f + v * 4f;        // flippers: low-frequency displacement stalkers can track
                return db > 0;
            }
        }
        class ShipNoise : Acoustics.ISource
        {
            public Nautilus n;
            public bool Sounding(out Vector3 pos, out float db, out Band band)
            {
                pos = n.Body.position; band = Band.Low; db = n.sys ? n.sys.NoiseDb : 0;
                return db > 0;
            }
        }

        ParticleSystem motes;

        // the plankton the lamp draws (the doc: light attracts light-drawn species): copepods and larvae aren't
        // individuals here (they're the pools' biomass), so a cloud of glinting motes gathers in the beam, thick at night
        void MakeMotes()
        {
            var go = new GameObject("Lamp motes");
            motes = go.AddComponent<ParticleSystem>();
            motes.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = motes.main; main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.startLifetime = 6f; main.startSpeed = 0.15f; main.startSize = new ParticleSystem.MinMaxCurve(0.012f, 0.04f); main.maxParticles = 900;
            var em = motes.emission; em.rateOverTime = 0;
            var sh = motes.shape; sh.shapeType = ParticleSystemShapeType.Cone; sh.angle = 18f; sh.radius = 0.3f; sh.length = 12f;
            sh.shapeType = ParticleSystemShapeType.ConeVolume;
            var noise = motes.noise; noise.enabled = true; noise.strength = 0.25f; noise.frequency = 0.6f;
            var col = motes.colorOverLifetime; col.enabled = true;
            var g = new Gradient();
            g.SetKeys(new[] { new GradientColorKey(new Color(0.85f, 1f, 0.9f), 0), new GradientColorKey(new Color(0.6f, 0.9f, 1f), 1) },
                      new[] { new GradientAlphaKey(0, 0), new GradientAlphaKey(1, 0.25f), new GradientAlphaKey(1, 0.7f), new GradientAlphaKey(0, 1) });
            col.color = g;
            var r = go.GetComponent<ParticleSystemRenderer>(); r.sharedMaterial = new Material(Resources.Load<Shader>("Shaders/Snow")); r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            motes.Play();
        }

        void Motes()
        {
            if (!motes) return;
            bool on = diver && diver.lampOn && !diver.aboard && UnderwaterLook.Underwater;
            var pool = eco.Of(diver && diver.Depth > 50f ? "kelp" : "shallows");
            float rich = pool != null ? Mathf.Clamp(pool.Abundance(1), 0.2f, 2f) : 1f;
            var em = motes.emission; em.rateOverTime = on ? (clock.Night ? 140f : 25f) * rich : 0f;
            var c = diver.cam.transform;
            motes.transform.SetPositionAndRotation(c.position + c.forward * 0.8f, c.rotation);
        }

        public static Life Build(Seabed bed, Clock clock, Diver diver, Nautilus ship, int seed)
        {
            SpeciesBook.Load();
            var l = new GameObject("Life").AddComponent<Life>();
            I = l;
            l.bed = bed; l.clock = clock; l.diver = diver; l.ship = ship; l.rnd = new System.Random(seed * 7919 + 13);
            l.eco = Ecology.FirstBuild();
            l.sound = new Acoustics(Seabed.Size);
            l.scent = new Scent(Seabed.Size);
            l.AddDiver(diver);
            if (ship) l.sound.sources.Add(new ShipNoise { n = ship });
            l.SpawnLeviathans();
            l.MakeMotes();
            return l;
        }

        // a crewmate joins the sea (or leaves it)
        public void AddDiver(ISense d)
        {
            if (d == null || divers.Contains(d)) return;
            divers.Add(d); sound.sources.Add(new DiverNoise { d = d });
        }
        public void RemoveDiver(ISense d)
        {
            divers.Remove(d);
            sound.sources.RemoveAll(s => s is DiverNoise dn && dn.d == d);
            foreach (var c in live) if (c.diverT == d) { c.diverT = null; c.targetDiver = false; }
        }

        // the nearest diver out in the water within range of a point (or null)
        ISense NearDiver(Vector3 p, float range)
        {
            ISense best = null; float bd = range * range;
            foreach (var d in divers)
            {
                if (d == null || d.Inside) continue;
                float dd = (d.Eye - p).sqrMagnitude;
                if (dd < bd) { bd = dd; best = d; }
            }
            return best;
        }

        // ---- looks ----------------------------------------------------------------------------------------------
        (Mesh, Material) Look(SpeciesDef s)
        {
            if (looks.TryGetValue(s, out var lk)) return lk;
            var mesh = CreatureLibrary.Get(s.e.id) ?? CreatureMeshes.For(s);
            var m = new Material(Shader.Find("Deep/Creature")) { enableInstancing = true, name = s.e.name };
            int mode = CreatureMeshes.Mode(s.kind);
            if (s.bottom && (s.kind == "snail" || (s.kind == "octopus" && !s.e.name.Contains("Octo")))) mode = 5;   // shells and stars lie still
            m.SetFloat("_Mode", mode);
            m.SetFloat("_Freq", Mathf.PI * 2f * Mathf.Clamp(2.2f / Mathf.Sqrt(s.size), 0.2f, 4f));
            m.SetFloat("_Amp", mode == 1 ? 0.25f : mode == 2 ? 0.05f : mode == 3 ? 0.12f : mode == 4 ? 0.06f : mode == 6 ? 0.07f : 0.09f);
            m.SetFloat("_Glow", s.e.light == "attracted" && s.biome == "kelp" ? 0.15f : s.biome == "caverns" && (s.e.light == "glow" || s.e.light == "strobe") ? 0.6f : s.biome == "caverns" ? 0.08f : 0f);
            looks[s] = (mesh, m);
            return (mesh, m);
        }

        // ---- where things can live ------------------------------------------------------------------------------
        float Bed(Vector3 p) => bed.SampleY(p.x, p.z);

        // the depth band a species keeps to at this hour (its table's band, the hunters of the kelp rising at night)
        Vector2 Band01(SpeciesDef s)
        {
            float lo = s.e.depthMin, hi = s.e.depthMax;
            if (clock.Night && s.level >= 2 && s.biome == "kelp") lo = Mathf.Max(5f, lo - 45f);
            if (clock.Night && s.level == 1 && s.night && !s.bottom) lo = Mathf.Max(2f, lo * 0.6f);
            if (clock.Night && clock.MoonFullness > 0.8f && (BaitFish(s) || (s.level == 2 && s.e.habitat == "open"))) { lo = 2f; hi = Mathf.Max(lo + 6f, Mathf.Min(hi, 25f)); }
            return new Vector2(lo, hi);
        }

        bool Active(SpeciesDef s) => clock.Night ? s.night : s.day;

        // a spot for one of these within the ring round the diver, or false
        readonly List<Vector3> anchors = new List<Vector3>();
        static readonly string[] Reef = { "reef" }, Kelp = { "kelp" };

        // not where the diver is looking, close in: a group appearing out of nowhere in plain sight would show
        bool InView(Vector3 p)
        {
            var cam = Camera.main; if (!cam) return false;
            var d = p - cam.transform.position; float dist = d.magnitude;
            return dist < 45f && Vector3.Dot(d / Mathf.Max(0.01f, dist), cam.transform.forward) > 0.45f;
        }

        // ---- the caves (Caverns.cs): the caverns' animals live in its water; nothing else comes down there ---------
        static Caverns Cv => Caverns.I;
        static bool Caves(Vector3 p) => Cv != null && (Cv.UnderGround(p) || Cv.InCave(p));
        public static bool Bat(SpeciesDef s) => s.e.habitat == "air";

        bool CaveSpot(SpeciesDef s, Vector3 around, float rMin, float rMax, out Vector3 at)
        {
            at = default;
            var cv = Cv; if (cv == null) return false;
            if (Bat(s))
            {
                // the roosts: the air over a pocket's water, near the eye
                foreach (var ch in cv.chambers)
                {
                    if (!ch.air || (ch.c - around).sqrMagnitude > (rMax + ch.r.x) * (rMax + ch.r.x)) continue;
                    for (int k = 0; k < 8; k++)
                    {
                        var p = new Vector3(ch.c.x + ((float)rnd.NextDouble() - 0.5f) * ch.r.x, Mathf.Lerp(ch.level + 1.5f, ch.c.y + ch.r.y * 0.8f, (float)rnd.NextDouble()), ch.c.z + ((float)rnd.NextDouble() - 0.5f) * ch.r.z);
                        if (cv.Sdf(p) < -1f && !InView(p)) { at = p; return true; }
                    }
                }
                return false;
            }
            if (!cv.FreePoint(around, rMin, Mathf.Min(rMax, 90f), 1f + s.size * 0.6f, rnd, out var q)) return false;
            if (s.bottom || s.e.habitat == "floor" || s.e.habitat == "crevice") { if (!cv.Surface(q, -1, 30f, out q)) return false; }
            else if (s.e.habitat == "ceiling") { if (!cv.Surface(q, +1, 30f, out q)) return false; }
            if (InView(q)) return false;
            at = q; return true;
        }

        bool SpotFor(SpeciesDef s, Vector3 around, float rMin, float rMax, out Vector3 at)
        {
            bool caveSpecies = s.biome == "caverns", eyeInCaves = Caves(around);
            if (caveSpecies || eyeInCaves)
            {
                at = default;
                return caveSpecies && eyeInCaves && CaveSpot(s, around, rMin, rMax, out at);
            }
            var band = Band01(s);
            // reef and kelp animals gather where the reef and the kelp are
            string h = s.e.habitat;
            var flora = DeepBoot.I ? DeepBoot.I.flora : null;
            if (flora && !s.bottom && (h == "reef" || h == "kelp" || h == "lagoon"))
            {
                flora.Near(around, rMin, rMax, anchors, 60, h == "kelp" ? Kelp : Reef);
                for (int k = 0; k < 6 && anchors.Count > 0; k++)
                {
                    var p = anchors[rnd.Next(anchors.Count)];
                    float water = -Bed(p);
                    if (water < band.x + 1f || water > band.y + 6f) continue;
                    p.y = Bed(p) + (h == "kelp" ? 2f + (float)rnd.NextDouble() * 10f : 0.6f + (float)rnd.NextDouble() * 2.5f) + s.size * 0.3f;
                    p.y = Mathf.Min(p.y, -1.5f);
                    if (InView(p)) continue;
                    at = p; return true;
                }
            }
            for (int k = 0; k < 10; k++)
            {
                double a = rnd.NextDouble() * Mathf.PI * 2, r = rMin + rnd.NextDouble() * (rMax - rMin);
                var p = around + new Vector3((float)System.Math.Cos(a) * (float)r, 0, (float)System.Math.Sin(a) * (float)r);
                if (p.x < 10 || p.z < 10 || p.x > Seabed.Size - 10 || p.z > Seabed.Size - 10) continue;
                float floor = Bed(p), water = -floor;
                if (water < band.x + 1f) continue;
                if (s.bottom) { if (water > band.y + 5f) continue; p.y = floor + 0.1f; if (InView(p)) continue; at = p; return true; }
                float top = -Mathf.Max(1.5f, band.x), low = Mathf.Max(floor + 1.2f + s.size * 0.3f, -band.y);
                if (top < low) continue;
                // reef and lagoon fish stay near the bottom; open-water swimmers anywhere in their band
                bool near = s.e.habitat == "reef" || s.e.habitat == "lagoon" || s.e.habitat == "kelp" || s.e.habitat == "crevice";
                p.y = near ? Mathf.Min(top, low + 1f + (float)rnd.NextDouble() * 6f) : Mathf.Lerp(low, top, (float)rnd.NextDouble());
                if (InView(p)) continue;
                at = p; return true;
            }
            at = default; return false;
        }

        // ---- the leviathans: one per biome, always alive, in a territory of their own ----------------------------
        void SpawnLeviathans()
        {
            foreach (var s in SpeciesBook.All)
            {
                if (!s.resident) continue;
                bool already = false; foreach (var c in live) if (c.persistent && c.sp.biome == s.biome) already = true;
                if (already) continue;
                // the territory: somewhere in its band, far enough from the start not to be met at once (the caverns'
                // giant keeps to its biggest cathedral)
                Vector3 best = new Vector3(Seabed.Size / 2, -60, Seabed.Size / 2); float bestScore = float.MaxValue;
                if (s.biome == "caverns")
                {
                    var ch = Cv != null ? Cv.Biggest(true) : null; if (ch == null) continue;
                    best = ch.c; bestScore = -1;
                }
                for (int k = 0; k < 400 && bestScore >= 0; k++)
                {
                    var p = new Vector3(60 + (float)rnd.NextDouble() * (Seabed.Size - 120), 0, 60 + (float)rnd.NextDouble() * (Seabed.Size - 120));
                    float water = -Bed(p);
                    float mid = (s.e.depthMin + s.e.depthMax) / 2;
                    float score = Mathf.Abs(water - mid - 6f) + (diver ? Mathf.Max(0, 350 - Vector3.Distance(p, diver.EyeWorld)) : 0);
                    if (score < bestScore) { bestScore = score; best = p; best.y = Bed(p) + 6f + s.size * 0.2f; }
                }
                var lv = Make(s, best, 0);
                lv.persistent = true; lv.hasNest = true; lv.nest = best; lv.home = best;
                Debug.Log($"DEEP SEA: {s.e.name} ({s.size:0} m) holds its territory at {best}");
            }
        }

        Creature Make(SpeciesDef s, Vector3 p, int group)
        {
            var c = new Creature { id = nextId++, sp = s, pos = p, home = p, goal = p, group = group, size = s.size * (0.85f + 0.3f * (float)rnd.NextDouble()) };
            byId[c.id] = c;
            c.fwd = Quaternion.Euler(0, (float)rnd.NextDouble() * 360f, 0) * Vector3.forward;
            c.hunger = 0.2f + 0.3f * (float)rnd.NextDouble();
            c.thinkT = (float)rnd.NextDouble() * 0.3f;
            // reef defenders and the solitary hunters keep a nest or a den
            c.hasNest = s.level >= 2 && !s.schooling && s.aggression >= 0.6f;
            c.nest = p;
            live.Add(c);
            return c;
        }

        // keep each species near the diver at its share of the pool
        void Populate(Vector3 eye)
        {
            int count = 0; foreach (var c in live) if (c.alive) count++;
            if (count >= maxLive) return;
            var near = new Dictionary<SpeciesDef, int>();
            foreach (var c in live) if (c.alive && (c.pos - eye).sqrMagnitude < Radius * Radius) { near.TryGetValue(c.sp, out int k); near[c.sp] = k + 1; }
            float area = Mathf.PI * Radius * Radius / 10000f;
            // a few species a round, in a shuffled order, so the mix fills in evenly
            int tries = 0;
            for (int k = 0; k < 6 && tries < 40; tries++)
            {
                var s = SpeciesBook.All[rnd.Next(SpeciesBook.All.Count)];
                if (s.resident || s.plankton) continue;
                var pool = eco.Of(s.biome);
                float abundance = pool != null ? pool.Abundance(Mathf.Min(s.level, 4)) : 1f;
                float bloom = clock.Night && clock.MoonFullness > 0.8f ? (BaitFish(s) ? 2.2f : s.level == 2 && s.e.habitat == "open" ? 1.5f : 1f) : 1f;
                float want = s.density * area * abundance * (Active(s) ? 1f : 0.35f) * 0.12f * bloom;
                near.TryGetValue(s, out int have);
                if (have >= want || (have > 0 && have + s.groupMin > want * 1.5f)) continue;
                if (rnd.NextDouble() > Mathf.Clamp01(want - have)) continue;
                if (!SpotFor(s, eye, 15f, Radius - 30f, out var at)) continue;
                int n = s.schooling ? rnd.Next(s.groupMin, s.groupMax + 1) : rnd.Next(Mathf.Min(s.groupMin, 4), Mathf.Max(Mathf.Min(s.groupMin, 4), Mathf.Min(s.groupMax, 4)) + 1);
                int g = nextGroup++;
                for (int i = 0; i < n && count < maxLive; i++, count++)
                {
                    var off = new Vector3((float)rnd.NextDouble() - 0.5f, ((float)rnd.NextDouble() - 0.5f) * 0.4f, (float)rnd.NextDouble() - 0.5f) * (1.5f + n * 0.25f) * Mathf.Max(0.4f, s.size);
                    var q = at + off;
                    if (Caves(at)) { if (Cv.Sdf(q) > -0.4f - s.size * 0.3f) q = at; }
                    else if (s.bottom) q.y = Bed(q) + 0.1f; else q.y = Mathf.Max(q.y, Bed(q) + 0.8f);
                    Make(s, q, n > 1 ? g : 0);
                }
                k++;
            }
        }

        // ---- the doc's named behaviours (see the tables' "interactions") ----------------------------------------
        static bool Has(SpeciesDef s, params string[] words) { foreach (var w in words) if (s.e.name.Contains(w)) return true; return false; }
        static bool Cleaner(SpeciesDef s) => Has(s, "Cleaner Shrimp", "Gleaner Shrimp", "Moss-Nibbler Wrasse");
        static bool Ambusher(SpeciesDef s) => Has(s, "Moray", "Scorpion-Lurker", "Sponge-Mimic", "Bulb-Lure", "Mantis");
        static bool Bloodhound(SpeciesDef s) => Has(s, "Fin-Shark", "Tangle Dogfish", "Stalker-Hound");     // smell blood from 300 m
        static bool Clicker(SpeciesDef s) => Has(s, "Carrion-Crab");
        static bool Boomer(SpeciesDef s) => Has(s, "Maw Grouper", "Grouper Titan");
        static bool BaitFish(SpeciesDef s) => s.level == 1 && s.schooling && !s.bottom;

        // the light on a creature: the diver's helmet lamp (the doc: about 1,000 lux at 1 m) in its beam, and the
        // Nautilus's floods; lux falls off with the square of the distance and the water takes its share
        float LuxAt(Vector3 p)
        {
            float lux = 0;
            foreach (var dv in divers)
            {
                if (dv == null || !dv.Lamp || dv.Inside) continue;
                var d = p - dv.Eye; float r = d.magnitude;
                if (r < 40f && Vector3.Dot(d / Mathf.Max(0.01f, r), dv.Look) > 0.82f) lux += 1000f / Mathf.Max(1f, r * r) * Mathf.Exp(-r * 0.05f);
            }
            if (ship && ship.powerK > 0.5f)
            {
                var bow = ship.WorldPoint(Nautilus.G(Nautilus.BowX - 10f, -2.3f, 0)); float r = (p - bow).magnitude;
                if (r < 60f) lux += 4000f / Mathf.Max(1f, r * r) * Mathf.Exp(-r * 0.05f);
            }
            return lux;
        }

        // ---- the senses and the states -------------------------------------------------------------------------
        bool DiverIn(Creature c, float range) => NearDiver(c.pos, range) != null;
        // the diver this one is after is within range of it
        bool TargetIn(Creature c, float range) => c.diverT != null && !c.diverT.Inside && (c.diverT.Eye - c.pos).sqrMagnitude < range * range;

        Creature Nearest(Creature c, float range, System.Func<Creature, bool> ok)
        {
            Creature best = null; float bd = range * range;
            foreach (var o in live)
            {
                if (!o.alive || o == c || !ok(o)) continue;
                float d = (o.pos - c.pos).sqrMagnitude;
                if (d < bd) { bd = d; best = o; }
            }
            return best;
        }

        void Think(Creature c)
        {
            if (c.forced) return;
            var s = c.sp;
            var pool = eco.Of(s.biome);
            bool starving = pool != null && pool.Starving(s.level);
            float perceive = s.perceive * (starving ? 3.5f : 1f) * (s.IsLeviathan ? 2.5f : 1f);
            float heard = sound.Hear(c.pos, s.bands, out var heardAt, out var heardWhat);
            var grad = scent.Gradient(c.pos, out float smell);
            var prev = c.state;

            // fleeing: hurt, a leviathan's rumble, or a hunter closing (starving animals have no fear)
            if (!starving && s.level < 3)
            {
                Creature threat = Nearest(c, perceive * 0.6f, o => o.sp.Eats(s) && (o.state == CState.Hunting || o.state == CState.Frenzy || o.sp.IsLeviathan));
                bool rumble = s.level < 3 && sound.Hear(c.pos, 1u, out _, out _) > 90f;
                var near = NearDiver(c.pos, 3f + s.size * 2f);
                bool diverScare = s.level == 1 && near != null && near.Velocity.magnitude > 2.5f;
                bool blinded = s.e.light == "repelled" && LuxAt(c.pos) > 20f;
                if (c.health < 0.25f || threat != null || rumble || diverScare || blinded)
                {
                    var lit = blinded ? NearDiver(c.pos, 40f) : null;
                var from = threat != null ? threat.pos : diverScare ? near.Eye : lit != null ? lit.Eye : heardAt;
                    var away = c.pos - from; away.y *= 0.3f; if (away.sqrMagnitude < 0.01f) away = -c.fwd;
                    c.goal = c.pos + away.normalized * 25f + Vector3.down * 4f;
                    // bait fish ball up and rise (the doc's bait balls); everything else runs for cover - and a pack of
                    // snappers drives its prey toward the morays waiting in the crevices
                    if (BaitFish(s) && threat != null) c.goal = c.pos + away.normalized * 8f + Vector3.up * 3f;
                    if (threat != null && Has(threat.sp, "Reef-Snapper"))
                    {
                        var moray = Nearest(c, 30f, o => Has(o.sp, "Moray"));
                        if (moray != null) c.goal = Vector3.Lerp(c.goal, moray.pos, 0.6f);
                    }
                    Set(c, CState.Fleeing, threat != null ? "a hunter" : rumble ? "a rumble" : blinded ? "the light" : c.health < 0.25f ? "hurt" : "the diver");
                    return;
                }
            }
            if (c.state == CState.Fleeing && c.stateT < 4f) return;

            // blood frenzy: hunters and scavengers strike anything that moves
            if (s.level >= 2 && smell > Scent.Frenzy)
            {
                c.target = Nearest(c, perceive, o => o.sp.size < c.size * 1.6f && o.sp != s);
                c.diverT = c.target == null ? NearDiver(c.pos, perceive) : null;
                c.targetDiver = c.diverT != null;
                c.goal = c.pos + grad * 10f;
                Set(c, CState.Frenzy, "blood");
                return;
            }

            // territorial: an intruder near the nest (the Reef-Crusher ignores a silent diver; the Tangle-Serpent strikes
            // only what comes within 15 m of where it hangs; a grouper booms its warning)
            bool ignoresDivers = Has(s, "Reef-Crusher");
            float nestR = Has(s, "Tangle-Serpent") ? 15f + c.size * 0.3f : s.IsLeviathan ? 100f : 25f;
            var intruder = c.hasNest && !ignoresDivers ? NearDiver(c.nest, nestR) : null;
            if (intruder != null)
            {
                if (c.state != CState.Territorial) c.displayT = 0;
                c.targetDiver = true; c.diverT = intruder; c.target = null;
                Set(c, CState.Territorial, "an intruder near its nest");
                return;
            }
            bool wraps = Has(s, "Tangle-Serpent") && ship && (ship.Body.position - c.pos).sqrMagnitude < Mathf.Pow(15f + Nautilus.Radius + c.size * 0.3f, 2);
            if (wraps || (s.IsLeviathan && ship && ship.sys && ship.sys.NoiseDb > 70f && !Has(s, "Tangle-Serpent") && (ship.Body.position - c.nest).sqrMagnitude < 400f * 400f))
            {
                c.targetDiver = false; c.diverT = null; c.target = null; c.goal = ship.Body.position;
                Set(c, CState.Territorial, "an engine in its territory");
                return;
            }

            // a cleaning station: the predators visiting the cleaners stay calm while they're cleaned
            if (c.station != null && c.station.alive && c.visitT < 8f && !starving)
            {
                c.visitT += 0.25f; c.goal = c.station.pos + Vector3.up * (c.size * 0.6f);
                Set(c, CState.Dormant, "being cleaned");
                return;
            }
            c.station = null;

            // hunting: hungry and prey in its senses (an ambusher waits in its crevice until prey is all but on it)
            if (c.hunger > 0.6f || starving)
            {
                float reach = Ambusher(s) && !starving ? 2.5f + c.size : perceive;
                var prey = Nearest(c, reach, o => s.Eats(o.sp) && o.size < c.size * 1.3f);
                var dp = (s.lethal || starving) && s.level >= 2 && (c.hunger > 0.8f || starving) ? NearDiver(c.pos, perceive) : null;
                if (prey != null || dp != null)
                {
                    c.target = prey; c.targetDiver = prey == null; c.diverT = prey == null ? dp : null;
                    Set(c, CState.Hunting, prey != null ? prey.sp.e.name : "the diver");
                    return;
                }
                if (starving && ship && s.level >= 2 && s.size >= 0.8f && (ship.Body.position - c.pos).sqrMagnitude < perceive * perceive)
                {
                    c.target = null; c.targetDiver = false; c.diverT = null; c.goal = ship.Body.position;
                    Set(c, CState.Hunting, "the Nautilus's larder");
                    return;
                }
            }

            // alert: a sound 15 dB over the ambient, a whiff of scent (a shark's nose finds a trace fifty times fainter:
            // blood from 300 m), or a light it's drawn to
            float notice = Bloodhound(s) ? Scent.Notice * 0.02f : Scent.Notice;
            if (smell > notice && smell <= Scent.Notice && grad != Vector3.zero)
            {
                c.goal = c.pos + grad * 25f;
                Set(c, CState.Alert, "blood far off");
                return;
            }
            var lamp = s.e.light == "attracted" && !s.bottom && LuxAt(c.pos) > 1f ? NearDiver(c.pos, 45f) : null;
            if (lamp != null)
            {
                c.goal = lamp.Eye + lamp.Look * 4f;
                Set(c, CState.Alert, "the light");
                return;
            }
            // the Wake: hot water draws the curious grazers and scavengers (tier 1), the mesopredators (tier 2) and the
            // biome's apex (tier 3) to the noisiest thing in it
            int tier = Acoustics.Tier(sound.WakeAt(c.pos));
            int need = s.level == 3 ? 3 : s.level == 2 ? 2 : s.level == 4 ? 1 : 9;
            if (tier >= need && c.state != CState.Alert && rnd.NextDouble() < 0.05)
            {
                var loud = NearDiver(c.pos, 400f);
                c.goal = ship && ship.sys && ship.sys.NoiseDb > 40f ? ship.Body.position : loud != null ? loud.Eye : c.pos;
                Set(c, CState.Alert, $"the Wake (tier {tier})");
                return;
            }
            if (heard > Acoustics.Ambient + 15f || smell > Scent.Notice)
            {
                // they come closer to see - to a distance: grazers keep well off, hunters and scavengers come in
                float standoff = s.level == 1 ? 30f : s.level == 4 ? 6f : 14f;
                var off = c.pos - heardAt; off.y *= 0.3f;
                c.goal = smell > Scent.Notice && grad != Vector3.zero ? c.pos + grad * 12f : heardAt + (off.sqrMagnitude > 0.01f ? off.normalized : Vector3.right) * standoff;
                Set(c, CState.Alert, smell > Scent.Notice ? "scent" : heardWhat ?? "a noise");
                return;
            }

            // dormant: a well-fed hunter or a grazing giant sometimes visits a cleaning station nearby
            if (s.level >= 2 && !Cleaner(s) && c.hunger < 0.5f && rnd.NextDouble() < 0.004)
            {
                var cl = Nearest(c, 40f, o => Cleaner(o.sp));
                if (cl != null) { c.station = cl; c.visitT = 0; }
            }
            if (Boomer(s) && rnd.NextDouble() < 0.003) { sound.Emit(c.pos, 72f, Band.Low, 1.2f, "a grouper's boom"); Sfx.Shared("grouper_boom", c.pos, 1f, Mathf.Clamp(3f / c.size, 0.7f, 1.2f), Medium.Water); }
            if (Clicker(s) && smell > 0.6f && rnd.NextDouble() < 0.05) { sound.Emit(c.pos, 50f, Band.High, 0.4f, "a carrion-crab's clicking"); Sfx.Shared("crab_clicks", c.pos, 1f, 1f, Medium.Water); }
            // dormant: wander the home range, in the band for the hour
            Set(c, CState.Dormant, null);
            if (((c.goal - c.pos).sqrMagnitude < 4f || c.stateT > 12f || prev != CState.Dormant) && Caves(c.pos))
            {
                float range = s.IsLeviathan ? 45f : 4f + c.size * 8f;
                if (Bat(s)) { CaveSpot(s, c.home, 0, 40f, out var bg); if (bg != default) c.goal = bg; }
                else if (Cv.FreePoint(c.home, 0, range, 1f + c.size * 0.5f, rnd, out var g2) && Cv.Clear(c.pos, g2, c.size * 0.4f)) c.goal = g2;
                c.stateT = 0;
            }
            else if ((c.goal - c.pos).sqrMagnitude < 4f || c.stateT > 12f || prev != CState.Dormant)
            {
                var band = Band01(s);
                float range = s.IsLeviathan ? 160f : 6f + c.size * 10f;
                var g = c.home + new Vector3(((float)rnd.NextDouble() - 0.5f) * range * 2, 0, ((float)rnd.NextDouble() - 0.5f) * range * 2);
                float floor = Bed(g);
                g.y = s.bottom ? floor + 0.1f : Mathf.Clamp(Mathf.Lerp(-band.y, -band.x, (float)rnd.NextDouble()), floor + 1f + c.size * 0.3f, -1.5f);
                if (!Active(s) && !s.bottom) g.y = Mathf.Min(g.y, floor + 1.5f + c.size * 0.3f);   // resting near the bottom
                c.goal = g; c.stateT = 0;
            }
        }

        void Set(Creature c, CState st, string why)
        {
            if (c.state != st) { c.state = st; c.stateT = 0; }
            c.why = why;
        }

        // ---- moving -------------------------------------------------------------------------------------------
        readonly Dictionary<int, (Vector3 sum, Vector3 vel, int n)> groups = new Dictionary<int, (Vector3, Vector3, int)>();

        void Move(Creature c, float dt)
        {
            var s = c.sp;
            Vector3 aim = c.goal; float speed = s.cruise * 0.6f;
            switch (c.state)
            {
                case CState.Alert: speed = s.cruise; break;
                case CState.Fleeing: speed = s.burst; break;
                case CState.Hunting: case CState.Frenzy:
                    speed = s.burst;
                    if (c.target != null && c.target.alive) aim = c.target.pos + c.target.vel * 0.5f;
                    else if (c.targetDiver && c.diverT != null) aim = c.diverT.Eye;
                    break;
                case CState.Territorial:
                    c.displayT += dt;
                    var foe = c.targetDiver && c.diverT != null ? c.diverT.Eye : c.goal;
                    if (c.displayT < 4f)
                    {
                        // the warning display: circling the intruder at a distance
                        var off = c.pos - foe; off.y = 0; if (off.sqrMagnitude < 0.1f) off = Vector3.right;
                        float r = 8f + c.size;
                        aim = foe + Quaternion.Euler(0, 40f, 0) * off.normalized * r; speed = s.cruise * 1.2f;
                    }
                    else { aim = foe; speed = s.burst; }
                    break;
            }
            if (!Active(s) && c.state == CState.Dormant) speed *= 0.4f;
            if (!c.persistent && c.size < 0.15f) speed *= 0.8f;

            var want = aim - c.pos;
            float dist = want.magnitude;
            var wantV = dist > 0.01f ? want / dist * Mathf.Min(speed, dist * 1.5f + 0.1f) : Vector3.zero;

            // schools: hold together and swim the same way
            if (c.group != 0 && groups.TryGetValue(c.group, out var g) && g.n > 1 && c.state != CState.Hunting)
            {
                var centre = g.sum / g.n; var avg = g.vel / g.n;
                var toC = centre - c.pos; float spread = (1.2f + Mathf.Sqrt(g.n) * c.size * 1.4f) * (c.state == CState.Fleeing && BaitFish(c.sp) ? 0.35f : 1f);
                wantV += toC * (toC.magnitude > spread ? 0.6f : -0.2f) + (avg - c.vel) * 0.5f;
                if (c.state == CState.Dormant) wantV = Vector3.Lerp(wantV, avg + toC * 0.3f, 0.5f);
            }

            // keep off the seabed (or on it, for the crawlers), under the surface, and clear of the Nautilus
            bool inCaves = Caves(c.pos);
            float floor = inCaves ? float.MinValue : Bed(c.pos);
            if (inCaves)
            {
                float sd = Cv.Sdf(c.pos), keep = 0.5f + c.size * 0.45f;
                if (sd > -keep) { var gr = Cv.Grad(c.pos); if (gr.sqrMagnitude > 1e-6f) wantV -= gr.normalized * (sd + keep) * 4f; }
                float lvl = Cv.PocketSurface(c.pos);
                if (Bat(s)) { if (c.pos.y < lvl + 1f) wantV.y = Mathf.Max(wantV.y, 2f); }
                else if (!float.IsPositiveInfinity(lvl) && c.pos.y > lvl - 0.6f) wantV.y = Mathf.Min(wantV.y, -0.6f);
                if (s.bottom) wantV.y = Mathf.Min(wantV.y, 0f);
            }
            else if (s.bottom) { wantV.y = 0; }
            else
            {
                float clear = floor + 0.8f + c.size * 0.3f;
                if (c.pos.y < clear + 1f) wantV.y = Mathf.Max(wantV.y, (clear + 1f - c.pos.y) * 1.5f);
                if (c.pos.y > -1.2f) wantV.y = Mathf.Min(wantV.y, -0.5f);   // (the open sea's surface)
            }
            if (ship)
            {
                var lp = ship.Body.InverseTransformPoint(c.pos);
                float along = Mathf.Clamp(lp.z, Nautilus.SternX, Nautilus.BowX);
                var axis = new Vector3(0, 0, along);
                var rad = lp - axis; float rr = rad.magnitude, keep = Nautilus.Radius + 1f + c.size * 0.5f;
                if (rr < keep && !(c.state == CState.Hunting && c.target == null && !c.targetDiver))
                    wantV += ship.Body.TransformDirection(rad.normalized) * (keep - rr) * 3f;
            }

            float acc = Mathf.Max(1f, speed * 2f);
            c.vel = Vector3.MoveTowards(c.vel, wantV, acc * dt);
            if (c.vel.sqrMagnitude > 0.0004f)
            {
                var dir = c.vel.normalized;
                if (s.bottom || s.kind == "ray" || s.kind == "turtle") dir = new Vector3(dir.x, dir.y * 0.3f, dir.z).normalized;
                c.fwd = Vector3.RotateTowards(c.fwd, dir, s.turn * Mathf.Deg2Rad * dt, 1f);
            }
            c.pos += c.vel * dt;
            if (inCaves)
            {
                // a crawler clings to the cave floor beneath it; nothing passes into the rock
                if (s.bottom && Cv.Surface(c.pos + Vector3.up * 0.5f, -1, 4f, out var fl)) c.pos.y = Mathf.Lerp(c.pos.y, fl.y + 0.05f + c.size * 0.1f, 0.3f);
                float sd = Cv.Sdf(c.pos);
                if (sd > -0.2f) { var gr = Cv.Grad(c.pos); if (gr.sqrMagnitude > 1e-6f) c.pos -= gr.normalized * (sd + 0.25f); }
            }
            else if (s.bottom) c.pos.y = Bed(c.pos) + 0.05f + c.size * 0.1f;
            else c.pos.y = Mathf.Max(c.pos.y, floor + 0.3f + c.size * 0.2f);
            c.pos.x = Mathf.Clamp(c.pos.x, 5, Seabed.Size - 5); c.pos.z = Mathf.Clamp(c.pos.z, 5, Seabed.Size - 5);

            // contact: the strike lands
            c.biteCool -= dt;
            if (c.biteCool > 0) return;
            float reach = c.size * 0.55f + 0.35f;
            if ((c.state == CState.Hunting || c.state == CState.Frenzy) && c.target != null && c.target.alive && (c.target.pos - c.pos).sqrMagnitude < (reach + c.target.size * 0.4f) * (reach + c.target.size * 0.4f))
                Eat(c, c.target);
            else if ((c.state == CState.Hunting || c.state == CState.Frenzy || (c.state == CState.Territorial && c.displayT >= 4f)) && c.targetDiver && TargetIn(c, reach + 0.9f))
                Bite(c);
            else if (ship && (c.state == CState.Hunting || c.state == CState.Frenzy || c.state == CState.Territorial) && c.target == null && !c.targetDiver)
            {
                var lp = ship.Body.InverseTransformPoint(c.pos);
                if (lp.z > Nautilus.SternX - 1f && lp.z < Nautilus.BowX + 1f && new Vector2(lp.x, lp.y).magnitude < Nautilus.Radius + reach + 0.6f) Ram(c);
            }
        }

        void Eat(Creature c, Creature prey)
        {
            prey.alive = false;
            scent.Emit(prey.pos, 2.5f + prey.size * 2f);
            sound.Emit(prey.pos, 55f + c.size * 3f, Band.Mid, 0.6f, "a kill");
            if (c.size > 0.6f) Sfx.Shared("kill", prey.pos, Mathf.Clamp01(c.size / 3f) * 0.7f + 0.3f, Mathf.Clamp(1.5f / c.size, 0.7f, 1.4f), Medium.Water);
            c.hunger = Mathf.Max(0, c.hunger - Mathf.Clamp(prey.size / Mathf.Max(0.05f, c.size) * 1.5f, 0.2f, 0.9f));
            c.biteCool = 2f; c.target = null;
            Set(c, CState.Dormant, null);
        }

        void Bite(Creature c)
        {
            float dmg = Mathf.Clamp(c.size * 9f, 4f, 70f) * (c.state == CState.Frenzy ? 1.4f : 1f);
            var who = c.diverT; if (who == null) return;
            if (who is Diver dv) dv.Hurt(dmg, c.sp.e.name);
            else Net.HurtMate(who, dmg, c.sp.e.name);
            scent.Emit(who.Eye, 1.8f);
            sound.BloodSpill(who.Eye, 1f);
            Sfx.Shared("bite", who.Eye, 1f, Mathf.Clamp(2f / c.size, 0.6f, 1.3f), Medium.Water);
            c.biteCool = 1.6f + c.size * 0.2f;
            c.hunger = Mathf.Max(0, c.hunger - 0.25f);
            if (c.state == CState.Territorial) { c.displayT = 0; Set(c, CState.Dormant, null); c.goal = c.nest; }
        }

        void Ram(Creature c)
        {
            c.biteCool = 18f;
            sound.Emit(c.pos, 95f, Band.Low, 0.8f, "a hull strike");
            Sfx.Shared("ram", c.pos, 1f, 1f, Medium.Water);
            if (hullT > 0 || !ship.sys) return;
            hullT = 25f;
            // they go for the stores: the Larder and Hydroponics, else whatever compartment they hit
            int room = ship.sys.RoomIndexAt(new Vector3(2.5f, 0, 0));
            ship.sys.AddBreach(room, 0.015f + c.size * 0.006f);
            ship.groundedMsg = $"Something is ramming the hull! ({c.sp.e.name})";
        }

        // ---- every frame ------------------------------------------------------------------------------------------
        void Update()
        {
            float dt = Mathf.Min(Time.deltaTime, 0.05f);
            if (mirror) Follow(dt);
            else Tick(dt, diver ? diver.EyeWorld : Vector3.zero);
            Draw();
            Motes();
        }

        public void Tick(float dt, Vector3 eye)
        {
            sound.Step(dt);
            scentT += dt; if (scentT >= 0.2f) { scent.hour = clock ? clock.hour : 10f; scent.Step(scentT); scentT = 0; }
            ecoT += dt; if (ecoT >= 1f) { eco.Step(ecoT / Clock.RealSecondsPerDay, clock ? clock.Daylight : 0.47f); ecoT = 0; }
            hullT -= dt;
            spawnT -= dt;
            if (spawnT <= 0)
            {
                spawnT = 0.5f;
                // round each diver in the water in turn (the one in the tests and shots if there's no one else)
                int k = 0; foreach (var d in divers) if (d != null && !(d is Diver)) k++;
                if (k == 0) Populate(eye);
                else { var d = divers[popTurn++ % divers.Count]; if (d != null) Populate(d is Diver ? eye : d.Eye); }
            }

            groups.Clear();
            foreach (var c in live)
            {
                if (!c.alive || c.group == 0) continue;
                groups.TryGetValue(c.group, out var g);
                groups[c.group] = (g.sum + c.pos, g.vel + c.vel, g.n + 1);
            }
            foreach (var c in live)
            {
                if (!c.alive) continue;
                c.stateT += dt;
                c.hunger = Mathf.Min(1f, c.hunger + dt / (c.sp.level == 1 ? 400f : c.sp.IsLeviathan ? 2400f : 900f));
                if (c.sp.level == 1 && c.state == CState.Dormant) c.hunger = Mathf.Max(0, c.hunger - dt / 300f);   // grazing as it goes
                c.thinkT -= dt;
                if (c.thinkT <= 0) { c.thinkT = 0.25f; Think(c); }
                Move(c, dt);
            }
            // let go of what's out of range (back into the pool), and the dead
            live.RemoveAll(c =>
            {
                bool gone = !c.alive || (!c.persistent && FarFromAll(c.pos, eye));
                if (gone) byId.Remove(c.id);
                return gone;
            });
        }

        int popTurn;
        bool FarFromAll(Vector3 p, Vector3 eye)
        {
            if ((p - eye).sqrMagnitude <= Despawn * Despawn) return false;
            foreach (var d in divers) if (d != null && !(d is Diver) && (p - d.Eye).sqrMagnitude <= Despawn * Despawn) return false;
            return true;
        }

        // ---- a crewmate's PC: the animals the host says are near, moved smoothly between its words -----------------
        public void Heard(int id, SpeciesDef s, Vector3 pos, Vector3 vel, Vector3 fwd, float size, CState st, float health, float t)
        {
            if (!byId.TryGetValue(id, out var c))
            {
                if (dead.Contains(id)) return;
                c = new Creature { id = id, sp = s, pos = pos, fwd = fwd, size = size, home = pos, goal = pos };
                byId[id] = c; live.Add(c);
            }
            c.netPos = pos; c.netVel = vel; c.netFwd = fwd; c.netT = t; c.seenT = Time.time;
            c.state = st; c.health = health; c.size = size;
        }
        readonly HashSet<int> dead = new HashSet<int>();     // killed here, before the host has heard (don't bring them back)

        void Follow(float dt)
        {
            float now = Net.Now;
            for (int i = live.Count - 1; i >= 0; i--)
            {
                var c = live[i];
                if (!c.alive || Time.time - c.seenT > 1.2f) { byId.Remove(c.id); live.RemoveAt(i); continue; }
                var want = c.netPos + c.netVel * Mathf.Clamp(now - c.netT, 0f, 0.5f);
                if ((want - c.pos).sqrMagnitude > 64f) c.pos = want;
                else c.pos = Vector3.Lerp(c.pos, want, 1 - Mathf.Exp(-dt * 8f));
                c.vel = c.netVel;
                if (c.netFwd.sqrMagnitude > 0.01f) c.fwd = Vector3.Slerp(c.fwd, c.netFwd, 1 - Mathf.Exp(-dt * 6f)).normalized;
            }
            if (dead.Count > 200) dead.Clear();
        }

        void Draw()
        {
            if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null) return;   // (the headless self-test)
            var cam = Camera.main; if (!cam) return;
            var planes = GeometryUtility.CalculateFrustumPlanes(cam);
            float far = cam.farClipPlane;
            var byKind = new Dictionary<SpeciesDef, List<Matrix4x4>>();
            foreach (var c in live)
            {
                if (!c.alive) continue;
                float d = (c.pos - cam.transform.position).magnitude;
                if (d > far + c.size) continue;
                if (!GeometryUtility.TestPlanesAABB(planes, new Bounds(c.pos, Vector3.one * c.size * 1.4f))) continue;
                if (!byKind.TryGetValue(c.sp, out var l)) byKind[c.sp] = l = new List<Matrix4x4>();
                var up = c.sp.bottom ? Vector3.up : Vector3.Lerp(Vector3.up, -Vector3.Cross(c.fwd, Vector3.Cross(Vector3.up, c.fwd)).normalized, 0f);
                l.Add(Matrix4x4.TRS(c.pos, Quaternion.LookRotation(c.fwd, up), Vector3.one * c.size));
            }
            foreach (var kv in byKind)
            {
                var (mesh, mat) = Look(kv.Key);
                var rp = new RenderParams(mat) { shadowCastingMode = kv.Key.size > 1.2f ? UnityEngine.Rendering.ShadowCastingMode.On : UnityEngine.Rendering.ShadowCastingMode.Off, receiveShadows = true };
                var list = kv.Value;
                for (int i = 0; i < list.Count; i += 1023)
                {
                    batch.Clear();
                    for (int j = i; j < Mathf.Min(list.Count, i + 1023); j++) batch.Add(list[j]);
                    Graphics.RenderMeshInstanced(rp, mesh, 0, batch);
                }
            }
        }

        // ---- the diver's weapons ---------------------------------------------------------------------------------
        // the animal nearest along a ray (a strike or a spear's flight), within maxDist
        public Creature PickCreature(Vector3 o, Vector3 dir, float maxDist, float slack = 0.3f)
        {
            Creature best = null; float bt = maxDist;
            foreach (var c in live)
            {
                if (!c.alive) continue;
                float t = Vector3.Dot(c.pos - o, dir);
                if (t < 0 || t > bt) continue;
                float perp = (o + dir * t - c.pos).magnitude;
                if (perp > c.size * 0.4f + slack) continue;
                bt = t; best = c;
            }
            return best;
        }

        // hit points: a small fish dies to one knife stroke, a shark takes a dozen, a leviathan shrugs it off
        public static float HitPoints(Creature c) => 6f + 14f * Mathf.Pow(c.size, 1.3f) * (c.sp.level == 3 ? 3f : 1f);

        // a wound: blood in the water (less if the blade cauterises), noise, and the animal flees or turns on the diver;
        // returns true if it died
        public bool Wound(Creature c, float damage, Vector3 from, bool cauterise, string how)
        {
            if (mirror)
            {
                Net.SendWound(c.id, damage, from, cauterise, how);
                c.health -= damage / HitPoints(c);
                if (c.health > 0f) return false;
                c.alive = false; dead.Add(c.id);
                return true;
            }
            c.health -= damage / HitPoints(c);
            float blood = cauterise ? 0.4f : 1.5f + c.size;
            scent.Emit(c.pos, blood);
            sound.BloodSpill(c.pos, cauterise ? 0.3f : 1f);
            sound.Emit(c.pos, 50f + c.size * 4f, Band.Mid, 0.6f, how);
            if (c.health <= 0f)
            {
                c.alive = false;
                var pool = eco.Of(c.sp.biome);
                if (pool != null) eco.Harvest(c.sp.biome, Mathf.Min(c.sp.level, 4), c.sp.mass);
                scent.Emit(c.pos, 2f + c.size);
                return true;
            }
            // the hurt: small things bolt; anything bold enough turns on what hurt it
            if (c.sp.level >= 2 && c.sp.aggression > 0.35f && c.health > 0.25f)
            {
                c.targetDiver = true; c.target = null; c.hunger = Mathf.Max(c.hunger, 0.9f);
                c.diverT = NearDiver(from, 30f);
                c.targetDiver = c.diverT != null;
                Set(c, CState.Hunting, "wounded by the diver");
            }
            else
            {
                var away = c.pos - from; away.y *= 0.3f;
                c.goal = c.pos + away.normalized * 30f; Set(c, CState.Fleeing, "wounded");
            }
            return false;
        }

        // ---- for the sonar and the HUD ----------------------------------------------------------------------------
        public struct Contact { public float bearing, range, size, speed; public Creature c; }
        // what the passive sonar's headphones pick up: big or loud animals within range, with bearings
        public List<Contact> Listen(Vector3 from, float range)
        {
            var list = new List<Contact>();
            foreach (var c in live)
            {
                if (!c.alive || c.size < 2.5f) continue;
                var d = c.pos - from; float r = d.magnitude;
                if (r > range * Mathf.Clamp01(c.size / 12f + 0.3f)) continue;
                list.Add(new Contact { bearing = Mathf.Repeat(Mathf.Atan2(d.x, d.z) * Mathf.Rad2Deg, 360f), range = r, size = c.size, speed = c.vel.magnitude, c = c });
            }
            list.Sort((a, b) => a.range.CompareTo(b.range));
            return list;
        }

        // the screenshot harness and the tests: let the sea run for a while around a point, so it's populated
        public void WarmUp(Vector3 eye, float seconds, float dt = 0.1f)
        {
            for (float t = 0; t < seconds; t += dt) { spawnT = Mathf.Min(spawnT, 0); Tick(dt, eye); }
        }

        public int Count(CState st) { int k = 0; foreach (var c in live) if (c.alive && c.state == st) k++; return k; }
    }
}
