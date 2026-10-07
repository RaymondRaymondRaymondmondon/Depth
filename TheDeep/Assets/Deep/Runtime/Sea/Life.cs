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

    public class Creature
    {
        public SpeciesDef sp; public Vector3 pos, vel, fwd = Vector3.forward, goal, home, nest;
        public CState state; public float hunger, health = 1f, stateT, thinkT, biteCool, displayT, size;
        public Creature target; public bool targetDiver, hasNest, alive = true, persistent;
        public int group;
        public string why;      // what set the current state off (for the tests and the HUD)
    }

    public class Life : MonoBehaviour
    {
        public static Life I;
        public Ecology eco; public Acoustics sound; public Scent scent;
        public readonly List<Creature> live = new List<Creature>();
        public const float Radius = 150f, Despawn = 195f;
        public int maxLive = 420;
        Seabed bed; Clock clock; Diver diver; Nautilus ship;
        readonly Dictionary<SpeciesDef, (Mesh mesh, Material mat)> looks = new Dictionary<SpeciesDef, (Mesh, Material)>();
        readonly List<Matrix4x4> batch = new List<Matrix4x4>(1023);
        float spawnT, scentT, ecoT, hullT; int nextGroup = 1;
        System.Random rnd;

        class DiverNoise : Acoustics.ISource
        {
            public Diver d;
            public bool Sounding(out Vector3 pos, out float db, out Band band)
            {
                pos = d.EyeWorld; band = Band.Low;
                float v = d.aboard ? 0 : d.vel.magnitude;
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

        public static Life Build(Seabed bed, Clock clock, Diver diver, Nautilus ship, int seed)
        {
            SpeciesBook.Load();
            var l = new GameObject("Life").AddComponent<Life>();
            I = l;
            l.bed = bed; l.clock = clock; l.diver = diver; l.ship = ship; l.rnd = new System.Random(seed * 7919 + 13);
            l.eco = Ecology.FirstBuild();
            l.sound = new Acoustics(Seabed.Size);
            l.scent = new Scent(Seabed.Size);
            l.sound.sources.Add(new DiverNoise { d = diver });
            if (ship) l.sound.sources.Add(new ShipNoise { n = ship });
            l.SpawnLeviathans();
            return l;
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
            m.SetFloat("_Glow", s.e.light == "attracted" && s.biome == "kelp" ? 0.15f : 0f);
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
            return new Vector2(lo, hi);
        }

        bool Active(SpeciesDef s) => clock.Night ? s.night : s.day;

        // a spot for one of these within the ring round the diver, or false
        readonly List<Vector3> anchors = new List<Vector3>();
        static readonly string[] Reef = { "tablecoral", "braincoral", "spirecoral", "fananemone" }, Kelp = { "kelp" };

        // not where the diver is looking, close in: a group appearing out of nowhere in plain sight would show
        bool InView(Vector3 p)
        {
            var cam = Camera.main; if (!cam) return false;
            var d = p - cam.transform.position; float dist = d.magnitude;
            return dist < 45f && Vector3.Dot(d / Mathf.Max(0.01f, dist), cam.transform.forward) > 0.45f;
        }

        bool SpotFor(SpeciesDef s, Vector3 around, float rMin, float rMax, out Vector3 at)
        {
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
                // the territory: somewhere in its band, far enough from the start not to be met at once
                Vector3 best = new Vector3(Seabed.Size / 2, -60, Seabed.Size / 2); float bestScore = float.MaxValue;
                for (int k = 0; k < 400; k++)
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
            var c = new Creature { sp = s, pos = p, home = p, goal = p, group = group, size = s.size * (0.85f + 0.3f * (float)rnd.NextDouble()) };
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
                float want = s.density * area * abundance * (Active(s) ? 1f : 0.35f) * 0.12f;
                near.TryGetValue(s, out int have);
                if (have >= want || (have > 0 && have + s.groupMin > want * 1.5f)) continue;
                if (rnd.NextDouble() > Mathf.Clamp01(want - have)) continue;
                if (!SpotFor(s, eye, 15f, Radius - 30f, out var at)) continue;
                int n = s.schooling ? rnd.Next(s.groupMin, s.groupMax + 1) : rnd.Next(Mathf.Min(s.groupMin, 4), Mathf.Max(Mathf.Min(s.groupMin, 4), Mathf.Min(s.groupMax, 4)) + 1);
                int g = nextGroup++;
                for (int i = 0; i < n && count < maxLive; i++, count++)
                {
                    var off = new Vector3((float)rnd.NextDouble() - 0.5f, ((float)rnd.NextDouble() - 0.5f) * 0.4f, (float)rnd.NextDouble() - 0.5f) * (1.5f + n * 0.25f) * Mathf.Max(0.4f, s.size);
                    var q = at + off; if (s.bottom) q.y = Bed(q) + 0.1f; else q.y = Mathf.Max(q.y, Bed(q) + 0.8f);
                    Make(s, q, n > 1 ? g : 0);
                }
                k++;
            }
        }

        // ---- the senses and the states -------------------------------------------------------------------------
        bool DiverIn(Creature c, float range) => diver && !diver.aboard && (diver.EyeWorld - c.pos).sqrMagnitude < range * range;

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
                bool diverScare = s.level == 1 && diver && !diver.aboard && DiverIn(c, 3f + s.size * 2f) && diver.vel.magnitude > 2.5f;
                if (c.health < 0.25f || threat != null || rumble || diverScare)
                {
                    var from = threat != null ? threat.pos : diverScare ? diver.EyeWorld : heardAt;
                    var away = c.pos - from; away.y *= 0.3f; if (away.sqrMagnitude < 0.01f) away = -c.fwd;
                    c.goal = c.pos + away.normalized * 25f + Vector3.down * 4f;
                    Set(c, CState.Fleeing, threat != null ? "a hunter" : rumble ? "a rumble" : c.health < 0.25f ? "hurt" : "the diver");
                    return;
                }
            }
            if (c.state == CState.Fleeing && c.stateT < 4f) return;

            // blood frenzy: hunters and scavengers strike anything that moves
            if (s.level >= 2 && smell > Scent.Frenzy)
            {
                c.target = Nearest(c, perceive, o => o.sp.size < c.size * 1.6f && o.sp != s);
                c.targetDiver = c.target == null && DiverIn(c, perceive);
                c.goal = c.pos + grad * 10f;
                Set(c, CState.Frenzy, "blood");
                return;
            }

            // territorial: an intruder near the nest
            if (c.hasNest && diver && !diver.aboard && (diver.EyeWorld - c.nest).sqrMagnitude < 50f * 50f * (s.IsLeviathan ? 4f : 0.25f))
            {
                if (c.state != CState.Territorial) c.displayT = 0;
                c.targetDiver = true; c.target = null;
                Set(c, CState.Territorial, "an intruder near its nest");
                return;
            }
            if (s.IsLeviathan && ship && ship.sys && ship.sys.NoiseDb > 80f && (ship.Body.position - c.nest).sqrMagnitude < 300f * 300f)
            {
                c.targetDiver = false; c.target = null; c.goal = ship.Body.position;
                Set(c, CState.Territorial, "an engine in its territory");
                return;
            }

            // hunting: hungry and prey in its senses
            if (c.hunger > 0.6f || starving)
            {
                var prey = Nearest(c, perceive, o => s.Eats(o.sp) && o.size < c.size * 1.3f);
                bool diverPrey = (s.lethal || starving) && s.level >= 2 && DiverIn(c, perceive) && (c.hunger > 0.8f || starving);
                if (prey != null || diverPrey)
                {
                    c.target = prey; c.targetDiver = prey == null;
                    Set(c, CState.Hunting, prey != null ? prey.sp.e.name : "the diver");
                    return;
                }
                if (starving && ship && s.level >= 2 && s.size >= 0.8f && (ship.Body.position - c.pos).sqrMagnitude < perceive * perceive)
                {
                    c.target = null; c.targetDiver = false; c.goal = ship.Body.position;
                    Set(c, CState.Hunting, "the Nautilus's larder");
                    return;
                }
            }

            // alert: a sound 15 dB over the ambient, or a whiff of scent
            if (heard > Acoustics.Ambient + 15f || smell > Scent.Notice)
            {
                c.goal = smell > Scent.Notice && grad != Vector3.zero ? c.pos + grad * 12f : heardAt;
                Set(c, CState.Alert, smell > Scent.Notice ? "scent" : heardWhat ?? "a noise");
                return;
            }

            // dormant: wander the home range, in the band for the hour
            Set(c, CState.Dormant, null);
            if ((c.goal - c.pos).sqrMagnitude < 4f || c.stateT > 12f || prev != CState.Dormant)
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
                    else if (c.targetDiver && diver) aim = diver.EyeWorld;
                    break;
                case CState.Territorial:
                    c.displayT += dt;
                    var foe = c.targetDiver && diver ? diver.EyeWorld : c.goal;
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
                var toC = centre - c.pos; float spread = 1.2f + Mathf.Sqrt(g.n) * c.size * 1.4f;
                wantV += toC * (toC.magnitude > spread ? 0.6f : -0.2f) + (avg - c.vel) * 0.5f;
                if (c.state == CState.Dormant) wantV = Vector3.Lerp(wantV, avg + toC * 0.3f, 0.5f);
            }

            // keep off the seabed (or on it, for the crawlers), under the surface, and clear of the Nautilus
            float floor = Bed(c.pos);
            if (s.bottom) { wantV.y = 0; }
            else
            {
                float clear = floor + 0.8f + c.size * 0.3f;
                if (c.pos.y < clear + 1f) wantV.y = Mathf.Max(wantV.y, (clear + 1f - c.pos.y) * 1.5f);
                if (c.pos.y > -1.2f) wantV.y = Mathf.Min(wantV.y, -0.5f);
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
            if (s.bottom) c.pos.y = Bed(c.pos) + 0.05f + c.size * 0.1f;
            else c.pos.y = Mathf.Max(c.pos.y, floor + 0.3f + c.size * 0.2f);
            c.pos.x = Mathf.Clamp(c.pos.x, 5, Seabed.Size - 5); c.pos.z = Mathf.Clamp(c.pos.z, 5, Seabed.Size - 5);

            // contact: the strike lands
            c.biteCool -= dt;
            if (c.biteCool > 0) return;
            float reach = c.size * 0.55f + 0.35f;
            if ((c.state == CState.Hunting || c.state == CState.Frenzy) && c.target != null && c.target.alive && (c.target.pos - c.pos).sqrMagnitude < (reach + c.target.size * 0.4f) * (reach + c.target.size * 0.4f))
                Eat(c, c.target);
            else if ((c.state == CState.Hunting || c.state == CState.Frenzy || (c.state == CState.Territorial && c.displayT >= 4f)) && c.targetDiver && DiverIn(c, reach + 0.9f))
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
            c.hunger = Mathf.Max(0, c.hunger - Mathf.Clamp(prey.size / Mathf.Max(0.05f, c.size) * 1.5f, 0.2f, 0.9f));
            c.biteCool = 2f; c.target = null;
            Set(c, CState.Dormant, null);
        }

        void Bite(Creature c)
        {
            float dmg = Mathf.Clamp(c.size * 9f, 4f, 70f) * (c.state == CState.Frenzy ? 1.4f : 1f);
            diver.Hurt(dmg, c.sp.e.name);
            scent.Emit(diver.EyeWorld, 1.8f);
            sound.BloodSpill(diver.EyeWorld, 1f);
            c.biteCool = 1.6f + c.size * 0.2f;
            c.hunger = Mathf.Max(0, c.hunger - 0.25f);
            if (c.state == CState.Territorial) { c.displayT = 0; Set(c, CState.Dormant, null); c.goal = c.nest; }
        }

        void Ram(Creature c)
        {
            c.biteCool = 18f;
            sound.Emit(c.pos, 95f, Band.Low, 0.8f, "a hull strike");
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
            Tick(dt, diver ? diver.EyeWorld : Vector3.zero);
            Draw();
        }

        public void Tick(float dt, Vector3 eye)
        {
            sound.Step(dt);
            scentT += dt; if (scentT >= 0.2f) { scent.hour = clock ? clock.hour : 10f; scent.Step(scentT); scentT = 0; }
            ecoT += dt; if (ecoT >= 1f) { eco.Step(ecoT / Clock.RealSecondsPerDay, clock ? clock.Daylight : 0.47f); ecoT = 0; }
            hullT -= dt;
            spawnT -= dt; if (spawnT <= 0) { spawnT = 0.5f; Populate(eye); }

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
            live.RemoveAll(c => !c.alive || (!c.persistent && (c.pos - eye).sqrMagnitude > Despawn * Despawn));
        }

        void Draw()
        {
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
