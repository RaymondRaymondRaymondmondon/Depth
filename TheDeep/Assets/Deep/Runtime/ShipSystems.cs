// The Nautilus's systems (the design doc, "The Nautilus"): power states, the battery and the boiler's fuel, the noise
// she puts into the water, hull breaches, compartments flooding through the open doors, the pumps, and the sonar.
//
// Power states (chosen at the engine room's switchboard):
//   Engine   - the boiler drives the screw: full speed, the battery recharges, 85-110 dB (heard kilometres away)
//   Silent   - battery drive: a quarter speed, the battery drains, 15-25 dB
//   Dead     - full shutdown: no screw, lamps, life support or active sonar; 0 dB
// Breaches let the sea in by the pressure at her depth; water runs from room to room through the doors (each
// bulkhead's door has no sill); the pumps (electric while she has power, by hand when someone mans them) throw it
// out. The water's weight sinks her and its place along her trims her bow or stern down (Nautilus.Sail reads it).
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public enum PowerState { Dead, Silent, Engine }

    public class ShipSystems : MonoBehaviour
    {
        public Nautilus n;
        public PowerState state = PowerState.Dead;
        public float battery = 0.35f;     // 0..1
        public float fuel = 0.4f;         // the boiler's bunker, 0..1
        public bool engineRepaired;
        public bool breakersTripped;      // the derelict's main breakers, tripped: nothing gets power till they're reset
        public float resetT;              // a hand at the switchboard resetting them       // the derelict's steam engine must be repaired at the boiler first (the doc: "restore primary power")
        public float NoiseDb;             // what she's putting into the water now (the Wake system reads it, stage 3)
        public float pingNoiseT;          // a sonar ping's crack is still ringing out
        public bool pumpsManned;          // someone is working the pump handles this frame
        public string alert;              // a line for whoever is aboard ("The batteries are flat.")

        public float SpeedFactor => state == PowerState.Engine ? 1f : state == PowerState.Silent ? 0.25f : 0f;
        public bool LifeSupport => state != PowerState.Dead;

        // ---- compartments ---------------------------------------------------------------------------------------
        public class Room
        {
            public NLRoom r; public float level;            // water depth over the floor (m)
            public float area;                                // floor area (m^2)
            public List<(int other, float width)> doors = new List<(int, float)>();
            public Transform water;
        }
        public readonly List<Room> rooms = new List<Room>();
        public bool mirror;               // a crewmate's PC: the host runs her systems (Net.cs feeds this copy)
        static int nextBreach = 1;
        public class Breach { public int id; public int room; public Vector3 gen; public float size; public float patch; public ParticleSystem jet; }
        public readonly List<Breach> breaches = new List<Breach>();
        public float WaterTonnes { get { float t = 0; foreach (var r in rooms) t += r.level * r.area; return t * 1.025f; } }
        public float TrimMoment { get { float m = 0; foreach (var r in rooms) m += r.level * r.area * (r.r.x0 + r.r.x1) * 0.5f; return m * 1.025f; } }

        Material waterMat, jetMat;

        public static ShipSystems Attach(Nautilus n)
        {
            var s = n.gameObject.AddComponent<ShipSystems>();
            s.n = n;
            s.waterMat = new Material(Shader.Find("Deep/Glass"));
            s.waterMat.SetColor("_Tint", new Color(0.25f, 0.7f, 0.65f)); s.waterMat.SetFloat("_Clear", 0.5f);
            s.jetMat = new Material(Resources.Load<Shader>("Shaders/Snow"));
            foreach (var r in n.L.rooms)
            {
                if (r.id == "pilot") continue;
                var room = new Room { r = r, area = (r.x1 - r.x0) * r.half * 2f };
                var q = GameObject.CreatePrimitive(PrimitiveType.Quad);
                Destroy(q.GetComponent<Collider>());
                q.name = "Water " + r.id;
                q.transform.SetParent(n.Body, false);
                q.transform.localRotation = Quaternion.Euler(90, 0, 0);
                q.transform.localScale = new Vector3(r.half * 2f, r.x1 - r.x0, 1);
                var mr = q.GetComponent<MeshRenderer>(); mr.sharedMaterial = s.waterMat; mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
                q.SetActive(false);
                room.water = q.transform;
                s.rooms.Add(room);
            }
            // the doors: rooms that share a bulkhead with a door in it
            foreach (var d in n.L.doors)
            {
                int a = -1, b = -1;
                for (int i = 0; i < s.rooms.Count; i++)
                {
                    if (Mathf.Abs(s.rooms[i].r.x1 - d.x) < 0.01f) a = i;
                    if (Mathf.Abs(s.rooms[i].r.x0 - d.x) < 0.01f) b = i;
                }
                if (a >= 0 && b >= 0) { s.rooms[a].doors.Add((b, d.w)); s.rooms[b].doors.Add((a, d.w)); }
            }
            return s;
        }

        public int RoomIndexAt(Vector3 gen)
        {
            for (int i = 0; i < rooms.Count; i++)
            {
                var r = rooms[i].r;
                if (gen.x >= r.x0 && gen.x <= r.x1 && Mathf.Abs(gen.z) <= r.half + 0.2f && gen.y >= r.floor - 0.5f && gen.y <= r.ceil + 0.3f) return i;
            }
            return -1;
        }

        // the water's surface (generator frame y) in the room at a point, or -infinity if it's dry
        public float SurfaceAt(Vector3 gen)
        {
            int i = RoomIndexAt(gen);
            if (i < 0 || rooms[i].level < 0.01f) return float.NegativeInfinity;
            return rooms[i].r.floor + rooms[i].level;
        }

        // ---- breaches ---------------------------------------------------------------------------------------------
        // a hole in a room's side (size: the hole's area in m^2; a hard grounding makes one of about 0.04)
        public Breach AddBreach(int room, float size, System.Random rnd = null)
        {
            if (room < 0 || room >= rooms.Count) return null;
            if (mirror) { Net.Cmd(Net.C_BREACH, room, size); return null; }
            rnd ??= new System.Random();
            var r = rooms[room].r;
            float side = rnd.Next(2) == 0 ? -1 : 1;
            var gen = new Vector3(Mathf.Lerp(r.x0 + 0.6f, r.x1 - 0.6f, (float)rnd.NextDouble()), r.floor + 0.4f + (float)rnd.NextDouble() * 0.9f, side * (r.half - 0.02f));
            var b = new Breach { id = nextBreach++, room = room, gen = gen, size = size, jet = MakeJet(gen, -side) };
            breaches.Add(b);
            return b;
        }

        ParticleSystem MakeJet(Vector3 gen, float inward)
        {
            var go = new GameObject("Breach jet");
            go.transform.SetParent(n.Body, false);
            go.transform.localPosition = Nautilus.G(gen.x, gen.y, gen.z);
            go.transform.localRotation = Quaternion.LookRotation(Nautilus.G(0, -0.15f, inward));
            var ps = go.AddComponent<ParticleSystem>();
            ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = ps.main;
            main.simulationSpace = ParticleSystemSimulationSpace.Local;
            main.startLifetime = 0.7f; main.startSpeed = new ParticleSystem.MinMaxCurve(3f, 6f);
            main.startSize = new ParticleSystem.MinMaxCurve(0.04f, 0.12f); main.maxParticles = 400; main.gravityModifier = 0.6f;
            var em = ps.emission; em.rateOverTime = 220;
            var sh = ps.shape; sh.shapeType = ParticleSystemShapeType.Cone; sh.angle = 14f; sh.radius = 0.04f;
            var col = ps.colorOverLifetime; col.enabled = true;
            var g = new Gradient();
            g.SetKeys(new[] { new GradientColorKey(new Color(0.85f, 0.95f, 1f), 0), new GradientColorKey(new Color(0.6f, 0.8f, 0.85f), 1) },
                      new[] { new GradientAlphaKey(0.9f, 0), new GradientAlphaKey(0, 1) });
            col.color = g;
            var rr = go.GetComponent<ParticleSystemRenderer>(); rr.sharedMaterial = jetMat; rr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            ps.Play();
            return ps;
        }

        public Breach BreachNear(Vector3 gen, float reach = 1.6f)
        {
            Breach best = null; float bd = reach;
            foreach (var b in breaches) { float d = (b.gen - gen).magnitude; if (d < bd) { bd = d; best = b; } }
            return best;
        }

        // a hand working on a breach: three seconds of hammering closes it
        // a breach the host has (on a crewmate's PC): made to match
        public Breach MirrorBreach(int id, int room, Vector3 gen, float size)
        {
            float inward = gen.z > 0 ? -1 : 1;
            var b = new Breach { id = id, room = room, gen = gen, size = size, jet = MakeJet(gen, inward) };
            breaches.Add(b);
            return b;
        }
        public void DropBreach(Breach b) { breaches.Remove(b); if (b.jet) Destroy(b.jet.gameObject); }

        public bool Patch(Breach b, float dt)
        {
            if (mirror) Net.PatchWork(b.id, dt);
            b.patch += dt / 3f;
            if (b.patch < 1f) return false;
            breaches.Remove(b);
            if (b.jet) Destroy(b.jet.gameObject);
            return true;
        }

        // ---- the sonar ----------------------------------------------------------------------------------------------
        public Texture2D sonarMap;
        public float sonarAge = 999f, sonarHeading;
        public const float SonarRange = 260f;
        const int SonarRes = 96;

        // an active ping: the seabed round her painted as a depth chart (relative to her keel) - and a 115 dB crack
        // every curious or hungry thing within a kilometre hears
        public bool Ping()
        {
            if (!LifeSupport || battery < 0.01f) { alert = "The sonar is dead: she has no power."; return false; }
            if (mirror) Net.Cmd(Net.C_PING);
            else
            {
                battery -= 0.005f;
                pingNoiseT = 1.5f;
                if (Life.I != null) Life.I.sound.Emit(n.Body.position, 115f, Band.Ultra, 1.5f, "a sonar ping");
            }
            var bed = DeepBoot.I ? DeepBoot.I.seabed : null; if (!bed) return false;
            if (!sonarMap) { sonarMap = new Texture2D(SonarRes, SonarRes, TextureFormat.RGBA32, false) { filterMode = FilterMode.Bilinear, wrapMode = TextureWrapMode.Clamp }; }
            var c = n.Body.position; float keel = c.y - Nautilus.Radius;
            var px = new Color32[SonarRes * SonarRes];
            for (int j = 0; j < SonarRes; j++)
                for (int i = 0; i < SonarRes; i++)
                {
                    float u = (i + 0.5f) / SonarRes * 2 - 1, v = (j + 0.5f) / SonarRes * 2 - 1;
                    float d = Mathf.Sqrt(u * u + v * v);
                    if (d > 1) { px[j * SonarRes + i] = new Color32(0, 0, 0, 0); continue; }
                    float h = bed.SampleY(c.x + u * SonarRange, c.z + v * SonarRange);
                    float rel = h - keel;    // + above her keel (a danger), - below
                    // phosphor green: bright where the ground stands high, dark in the deeps; contour lines every 5 m
                    float k = Mathf.Clamp01(0.55f + rel / 40f);
                    float contour = Mathf.Abs(Mathf.Repeat(h, 5f) - 2.5f) < 0.35f ? 0.25f : 0f;
                    var col = rel > 2f ? new Color(1f, 0.6f, 0.3f) * Mathf.Clamp01(0.55f + rel / 30f) : new Color(0.25f, 1f, 0.55f) * (0.15f + 0.75f * k);
                    col += new Color(contour, contour, contour) * 0.6f;
                    col.a = 1;
                    px[j * SonarRes + i] = col;
                }
            // resources: the ping highlights deposits (white) and wrecks (yellow)
            if (Deposits.I != null)
            {
                void Mark(Vector3 w, Color32 col, int r)
                {
                    float u = (w.x - c.x) / SonarRange, v = (w.z - c.z) / SonarRange;
                    if (u * u + v * v > 1) return;
                    int i0 = (int)((u * 0.5f + 0.5f) * SonarRes), j0 = (int)((v * 0.5f + 0.5f) * SonarRes);
                    for (int dj = -r; dj <= r; dj++) for (int di = -r; di <= r; di++)
                    { int ii = i0 + di, jj = j0 + dj; if (ii >= 0 && jj >= 0 && ii < SonarRes && jj < SonarRes) px[jj * SonarRes + ii] = col; }
                }
                foreach (var nd in Deposits.I.nodes) if (!nd.taken && nd.by != "hand") Mark(nd.pos, new Color32(255, 255, 255, 255), 0);
                foreach (var w in Deposits.I.wrecks) Mark(w.t.position, new Color32(255, 220, 60, 255), 2);
            }
            sonarMap.SetPixels32(px); sonarMap.Apply();
            sonarAge = 0; sonarHeading = n.heading;
            return true;
        }

        // ---- every frame ------------------------------------------------------------------------------------------
        void Update()
        {
            float dt = Mathf.Min(Time.deltaTime, 0.05f);
            Step(dt);
        }

        public void Step(float dt)
        {
            if (mirror)
            {
                // the host's numbers arrive by the network; here only the water and the jets are shown
                n.power = state != PowerState.Dead;
                sonarAge += dt;
                foreach (var b in breaches)
                {
                    if (b.room < 0 || b.room >= rooms.Count || !b.jet) continue;
                    var rm = rooms[b.room]; bool sub = rm.r.floor + rm.level > b.gen.y;
                    var em0 = b.jet.emission; em0.rateOverTime = sub ? 60 : 220;
                    var mn = b.jet.main; mn.gravityModifier = sub ? -0.15f : 0.6f;
                }
                foreach (var r in rooms)
                {
                    bool on = r.level > 0.02f;
                    if (r.water.gameObject.activeSelf != on) r.water.gameObject.SetActive(on);
                    if (on) r.water.localPosition = Nautilus.G((r.r.x0 + r.r.x1) / 2, r.r.floor + r.level, 0);
                }
                pumpsManned = false;
                return;
            }
            float spd = Mathf.Abs(n.speed);
            // power
            if (state == PowerState.Engine)
            {
                fuel -= dt * (0.0003f + 0.0009f * spd / 5.6f);
                battery = Mathf.Min(1f, battery + dt * 0.004f);
                if (fuel <= 0) { fuel = 0; state = battery > 0 ? PowerState.Silent : PowerState.Dead; alert = "The boiler's fire goes out. She runs on her batteries."; }
            }
            else if (state == PowerState.Silent)
            {
                battery -= dt * (0.0008f + 0.0025f * spd / 1.4f + (pumpsRunning ? 0.001f : 0));
                if (battery <= 0) { battery = 0; state = PowerState.Dead; alert = "The batteries are flat. She goes dark."; }
            }
            n.power = state != PowerState.Dead;
            pingNoiseT = Mathf.Max(0, pingNoiseT - dt);
            NoiseDb = state == PowerState.Engine ? 85f + 25f * spd / 5.6f : state == PowerState.Silent ? 15f + 10f * spd / 1.4f : 0f;
            if (pumpsRunning) NoiseDb = Mathf.Max(NoiseDb, state == PowerState.Engine ? NoiseDb : 30f);
            if (pingNoiseT > 0) NoiseDb = Mathf.Max(NoiseDb, 115f);
            sonarAge += dt;

            // past her crush depth the hull groans, then plates give way (faster the deeper she goes)
            float over = n.Depth - n.crushDepth;
            if (over > 0)
            {
                crushT += dt * (1f + over / 5f);
                if (crushT > 12f)
                {
                    crushT = 0;
                    AddBreach(Random.Range(0, rooms.Count), 0.02f + over * 0.002f);
                    alert = $"The hull buckles! She's {over:0} m past her crush depth ({n.crushDepth:0} m).";
                }
                else if (crushT > 6f && !creakWarned) { creakWarned = true; alert = "The hull groans under the pressure."; }
            }
            else { crushT = Mathf.Max(0, crushT - dt); creakWarned = false; }

            // the sea coming in: through each breach at the pressure of her depth (and slower once the room is filling)
            float depth = Mathf.Max(0, n.Depth);
            foreach (var b in breaches)
            {
                var room = rooms[b.room];
                float head = Mathf.Max(0.5f, depth + (room.r.ceil - (room.r.floor + room.level)) * 0.2f);
                float q = b.size * 0.6f * Mathf.Sqrt(2 * 9.81f * head);
                room.level = Mathf.Min(room.r.ceil - room.r.floor, room.level + q * dt / room.area);
                bool submerged = room.r.floor + room.level > b.gen.y;
                var em = b.jet.emission; em.rateOverTime = submerged ? 60 : 220;
                var main = b.jet.main; main.gravityModifier = submerged ? -0.15f : 0.6f;
            }
            // from room to room through the doors
            for (int i = 0; i < rooms.Count; i++)
                foreach (var (j, w) in rooms[i].doors)
                {
                    if (j < i) continue;
                    float dl = rooms[i].level - rooms[j].level;
                    float flow = Mathf.Clamp(dl, -1, 1) * w * 1.6f * dt;   // m^3
                    flow = Mathf.Clamp(flow, -rooms[j].level * rooms[j].area * 0.5f, rooms[i].level * rooms[i].area * 0.5f);
                    rooms[i].level -= flow / rooms[i].area; rooms[j].level += flow / rooms[j].area;
                }
            // the pumps: electric ones while she has power, the hand pumps when someone works them
            float pump = (n.power ? 0.25f : 0f) + (pumpsManned ? 0.45f : 0f);
            pumpsRunning = pump > 0 && WaterTonnes > 0.05f;
            float left = pump * dt;
            for (int k = 0; k < 4 && left > 1e-5f; k++)
            {
                int wet = 0; foreach (var r in rooms) if (r.level > 0.001f) wet++;
                if (wet == 0) break;
                float each = left / wet;
                foreach (var r in rooms)
                {
                    if (r.level <= 0.001f) continue;
                    float take = Mathf.Min(each, r.level * r.area);
                    r.level -= take / r.area; left -= take;
                }
            }
            pumpsManned = false;
            // the water's sheets
            foreach (var r in rooms)
            {
                bool on = r.level > 0.02f;
                if (r.water.gameObject.activeSelf != on) r.water.gameObject.SetActive(on);
                if (on) r.water.localPosition = Nautilus.G((r.r.x0 + r.r.x1) / 2, r.r.floor + r.level, 0);
            }
        }
        public bool pumpsRunning;
        float crushT; bool creakWarned;
    }
}
