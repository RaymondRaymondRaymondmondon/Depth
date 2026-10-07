// The Nautilus: Verne's submarine, found derelict on the seabed (stage 2 of docs/DEEP_PROGRESS.md).
//
// Two copies of her:
// - the Body: what everyone sees, wherever she is (models from tools/artgen/deep_nautilus.py, imported by glTFast,
//   their materials swapped for Deep/Lit so the water lights them like the rest of the sea), with colliders round
//   the outside for swimmers;
// - the Proxy: invisible, fixed, upright, far above the world (ProxyOrigin), holding only the colliders of her
//   rooms. Anyone aboard walks in the Proxy with ordinary physics; the camera is mapped onto the Body
//   (ToWorld / RotToWorld), so she can move, roll and pitch while the crew walk about her without sliding.
// Both share one local frame: z toward the bow, y up, x to starboard (NautilusLayout's x/y/z are G()'d into it).
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Nautilus : MonoBehaviour
    {
        public static Nautilus I;
        public static readonly Vector3 ProxyOrigin = new Vector3(0, 5000, 0);
        public NautilusLayout L;
        public Transform Body, Proxy;
        public bool power;
        public float powerK;          // eased 0..1, what the lamps and globes show
        public const float Floor = -1.6f, Radius = 4f, ShaftX = 20.95f, ShaftHalf = 0.47f;

        // the generator's frame (x bow, y up, z starboard) -> the ship's local frame
        public static Vector3 G(float x, float y, float z) => new Vector3(z, y, x);
        public static Vector3 G(float[] p) => new Vector3(p[2], p[1], p[0]);
        public static Vector3 FromLocal(Vector3 l) => new Vector3(l.z, l.y, l.x);    // and back

        public Vector3 ToWorld(Vector3 proxyPos) => Body.TransformPoint(Proxy.InverseTransformPoint(proxyPos));
        public Vector3 ToProxy(Vector3 world) => Proxy.TransformPoint(Body.InverseTransformPoint(world));
        public Quaternion RotToWorld(Quaternion proxyRot) => Body.rotation * Quaternion.Inverse(Proxy.rotation) * proxyRot;
        public Vector3 ProxyPoint(Vector3 local) => Proxy.TransformPoint(local);
        public Vector3 WorldPoint(Vector3 local) => Body.TransformPoint(local);

        readonly List<(Vector3 local, Color col, float range, bool water, int room)> lamps = new List<(Vector3, Color, float, bool, int)>();
        readonly Vector4[] lampPos = new Vector4[24], lampCol = new Vector4[24];

        public static Nautilus Build(Vector3 at, float yaw, float pitch, float roll)
        {
            var go = new GameObject("Nautilus");
            var n = go.AddComponent<Nautilus>();
            I = n;
            n.L = NautilusLayout.Load();
            n.Body = go.transform;
            n.Body.SetPositionAndRotation(at, Quaternion.Euler(pitch, yaw, roll));
            var rb = go.AddComponent<Rigidbody>(); rb.isKinematic = true; rb.useGravity = false;   // (her colliders move)
            n.LoadModel("nautilus_hull", false);
            n.LoadModel("nautilus_interior", true);
            n.HullColliders();
            n.Proxy = new GameObject("Nautilus proxy").transform;
            n.Proxy.SetPositionAndRotation(ProxyOrigin, Quaternion.identity);
            n.RoomColliders();
            n.Lamps();
            n.MoonpoolWater();
            return n;
        }

        // ---- the models -------------------------------------------------------------------------------------------
        static readonly Dictionary<Material, Material> converted = new Dictionary<Material, Material>();

        void LoadModel(string name, bool interior)
        {
            var prefab = Resources.Load<GameObject>("Models/" + name);
            if (!prefab) { Debug.LogError("DEEP: model missing " + name); return; }
            var go = Instantiate(prefab, Body, false);
            go.name = name;
            // glTF's frame is the generator's (x bow); glTFast mirrors x on import, and a quarter turn puts the bow at +z
            go.transform.localPosition = Vector3.zero;
            go.transform.localRotation = Quaternion.Euler(0, 90, 0);
            go.transform.localScale = Vector3.one;
            var b = new Bounds(); bool first = true;
            foreach (var r in go.GetComponentsInChildren<Renderer>())
            {
                var mats = r.sharedMaterials;
                for (int i = 0; i < mats.Length; i++) mats[i] = Convert(mats[i], interior);
                r.sharedMaterials = mats;
                r.shadowCastingMode = interior ? UnityEngine.Rendering.ShadowCastingMode.Off : UnityEngine.Rendering.ShadowCastingMode.On;
                var lb = r.bounds; lb.center = Body.InverseTransformPoint(lb.center);
                if (first) { b = lb; first = false; } else b.Encapsulate(lb);
            }
            Debug.Log($"DEEP NAUTILUS: {name} local bounds {b.min} .. {b.max}");
        }

        static Texture Tex(Material m, params string[] keys)
        {
            foreach (var p in m.GetTexturePropertyNames())
                foreach (var k in keys)
                    if (p.ToLowerInvariant().Contains(k) && m.GetTexture(p)) return m.GetTexture(p);
            return null;
        }
        static float Num(Material m, float def, params string[] names)
        {
            foreach (var k in names) if (m.HasProperty(k)) return m.GetFloat(k);
            return def;
        }

        static Material Convert(Material src, bool interior)
        {
            if (!src) return src;
            if (converted.TryGetValue(src, out var done)) return done;
            string n = src.name.ToLowerInvariant();
            Material m;
            if (n.StartsWith("glass") || n.StartsWith("lens"))
            {
                m = new Material(Shader.Find("Deep/Glass")) { name = src.name };
                if (n.StartsWith("lens")) { m.SetColor("_Tint", new Color(1f, 0.9f, 0.6f)); m.SetFloat("_Clear", 0.6f); }
            }
            else
            {
                m = new Material(Shader.Find("Deep/Lit")) { name = src.name };
                var bc = Tex(src, "basecolor", "_basemap", "_maintex");
                var nm = Tex(src, "normal", "_bumpmap");
                var mr = Tex(src, "metallicroughness");
                if (bc) m.SetTexture("_BaseMap", bc);
                if (nm) { m.SetTexture("_BumpMap", nm); m.SetFloat("_HasBump", 1); }
                if (mr) m.SetTexture("_MRMap", mr);
                Color col = Color.white;
                foreach (var k in new[] { "baseColorFactor", "_BaseColor", "_Color" }) if (src.HasProperty(k)) { col = src.GetColor(k); break; }
                m.SetColor("_BaseColor", col);
                m.SetFloat("_Metallic", Num(src, 0, "metallicFactor", "_Metallic"));
                m.SetFloat("_Roughness", src.HasProperty("roughnessFactor") ? src.GetFloat("roughnessFactor") : 1 - Num(src, 0, "_Smoothness", "_Glossiness"));
                m.SetFloat("_Interior", interior && !n.StartsWith("hull") ? 1 : 0);
                m.SetFloat("_Emit", n.StartsWith("lamp") ? 1 : 0);
                // the outside is one-sided (the pilot house and hull are hollow shells seen from inside too); the
                // fittings inside are two-sided, since the generator's booleans leave some faces turned
                m.SetFloat("_Cull", interior ? 0 : 2);
                Debug.Log($"DEEP NAUTILUS: material '{src.name}' shader {src.shader.name}; textures [{string.Join(",", src.GetTexturePropertyNames())}] -> base {(bc ? bc.name : "-")} normal {(nm ? nm.name : "-")} mr {(mr ? mr.name : "-")}");
            }
            converted[src] = m;
            return m;
        }

        // ---- colliders --------------------------------------------------------------------------------------------
        // outside: the hull's cigar, the deck and the pilot house, for the swimmers
        void HullColliders()
        {
            var cap = gameObject.AddComponent<CapsuleCollider>();
            cap.direction = 2; cap.radius = Radius; cap.height = 70f; cap.center = Vector3.zero;
            AddBox(Body, (-12f, 4.15f, 0f), (17f, 0.25f, 1.2f));          // the deck
            AddBox(Body, (21.8f, 4.9f, 0f), (1.35f, 1.1f, 1.0f));          // the pilot house
        }

        static void AddBox(Transform parent, (float x, float y, float z) c, (float x, float y, float z) half)
        {
            var bc = parent.gameObject.AddComponent<BoxCollider>();
            bc.center = G(c.x, c.y, c.z);
            bc.size = G(half.x * 2, half.y * 2, half.z * 2);
        }

        // a horizontal slab from x0..x1, z0..z1 (generator frame) at height y, thickness t (downwards from y)
        void Slab(float x0, float x1, float z0, float z1, float y, float t)
        {
            if (x1 - x0 < 0.01f || z1 - z0 < 0.01f) return;
            AddBox(Proxy, ((x0 + x1) / 2, y - t / 2, (z0 + z1) / 2), ((x1 - x0) / 2, t / 2, (z1 - z0) / 2));
        }
        // a slab with a rectangular hole
        void SlabHole(float x0, float x1, float z0, float z1, float y, float t, float hx0, float hx1, float hz0, float hz1)
        {
            Slab(x0, hx0, z0, z1, y, t); Slab(hx1, x1, z0, z1, y, t);
            Slab(hx0, hx1, z0, hz0, y, t); Slab(hx0, hx1, hz1, z1, y, t);
        }
        // a wall across her (constant x) from z0..z1, y0..y1
        void Across(float x, float z0, float z1, float y0, float y1)
        {
            if (z1 - z0 < 0.01f || y1 - y0 < 0.01f) return;
            AddBox(Proxy, (x, (y0 + y1) / 2, (z0 + z1) / 2), (0.08f, (y1 - y0) / 2, (z1 - z0) / 2));
        }
        // a wall along her (constant z) from x0..x1
        void Along(float z, float x0, float x1, float y0, float y1)
        {
            if (x1 - x0 < 0.01f) return;
            AddBox(Proxy, ((x0 + x1) / 2, (y0 + y1) / 2, z), ((x1 - x0) / 2, (y1 - y0) / 2, 0.08f));
        }

        // inside: floors, ceilings, side walls, the bulkheads with their doors, the pilot house and its shaft, and the
        // moonpool's well - all boxes from the layout, so walking is smooth and nothing snags
        void RoomColliders()
        {
            var mp = L.moonpool;
            foreach (var r in L.rooms)
            {
                if (r.id == "pilot")
                {
                    SlabHole(r.x0, r.x1, -r.half, r.half, r.floor, 0.2f, ShaftX - ShaftHalf, ShaftX + ShaftHalf, -ShaftHalf, ShaftHalf);
                    Slab(r.x0, r.x1, -r.half, r.half, r.ceil + 0.2f, 0.2f);
                    Along(-r.half - 0.08f, r.x0, r.x1, r.floor, r.ceil); Along(r.half + 0.08f, r.x0, r.x1, r.floor, r.ceil);
                    Across(r.x0 - 0.08f, -r.half, r.half, r.floor, r.ceil); Across(r.x1 + 0.08f, -r.half, r.half, r.floor, r.ceil);
                    continue;
                }
                if (r.id == "moonpool" && mp != null && mp.x1 > mp.x0)
                    SlabHole(r.x0, r.x1, -r.half - 0.1f, r.half + 0.1f, r.floor, 0.3f, mp.x0, mp.x1, mp.z0, mp.z1);
                else
                    Slab(r.x0, r.x1, -r.half - 0.1f, r.half + 0.1f, r.floor, 0.3f);
                if (r.x0 < ShaftX && ShaftX < r.x1)
                    SlabHole(r.x0, r.x1, -r.half - 0.1f, r.half + 0.1f, r.ceil + 0.3f, 0.3f, ShaftX - ShaftHalf, ShaftX + ShaftHalf, -ShaftHalf, ShaftHalf);
                else
                    Slab(r.x0, r.x1, -r.half - 0.1f, r.half + 0.1f, r.ceil + 0.3f, 0.3f);
                Along(-r.half - 0.08f, r.x0, r.x1, r.floor, r.ceil); Along(r.half + 0.08f, r.x0, r.x1, r.floor, r.ceil);
            }
            // the bulkheads: one at every room boundary, a door in each (and a solid wall at the bow and the stern)
            var xs = new SortedSet<float>();
            foreach (var r in L.rooms) if (r.id != "pilot") { xs.Add(r.x0); xs.Add(r.x1); }
            foreach (var x in xs)
            {
                float hw = 0, top = Floor;
                foreach (var r in L.rooms) if (r.id != "pilot" && (Mathf.Abs(r.x0 - x) < 0.01f || Mathf.Abs(r.x1 - x) < 0.01f)) { hw = Mathf.Max(hw, r.half + 0.2f); top = Mathf.Max(top, r.ceil); }
                NLDoor door = null;
                foreach (var d in L.doors) if (Mathf.Abs(d.x - x) < 0.01f) door = d;
                if (door == null) { Across(x, -hw, hw, Floor, top); continue; }
                Across(x, -hw, door.z - door.w / 2, Floor, top);
                Across(x, door.z + door.w / 2, hw, Floor, top);
                Across(x, door.z - door.w / 2, door.z + door.w / 2, Floor + door.h, top);
            }
            // the shaft from the Bridge's ceiling up to the pilot house's floor
            var pilot = L.Room("pilot"); float shaftTop = pilot != null ? pilot.floor : 3.6f;
            Across(ShaftX - ShaftHalf - 0.08f, -ShaftHalf, ShaftHalf, 2.2f, shaftTop);
            Across(ShaftX + ShaftHalf + 0.08f, -ShaftHalf, ShaftHalf, 2.2f, shaftTop);
            Along(-ShaftHalf - 0.08f, ShaftX - ShaftHalf, ShaftX + ShaftHalf, 2.2f, shaftTop);
            Along(ShaftHalf + 0.08f, ShaftX - ShaftHalf, ShaftX + ShaftHalf, 2.2f, shaftTop);
            // the moonpool's well down to the keel
            if (mp != null && mp.x1 > mp.x0)
            {
                Across(mp.x0 - 0.08f, mp.z0, mp.z1, -Radius, Floor); Across(mp.x1 + 0.08f, mp.z0, mp.z1, -Radius, Floor);
                Along(mp.z0 - 0.08f, mp.x0, mp.x1, -Radius, Floor); Along(mp.z1 + 0.08f, mp.x0, mp.x1, -Radius, Floor);
            }
        }

        // the moonpool's water: a sheet of glassy sea in the well, where the sea stands inside her
        void MoonpoolWater()
        {
            var mp = L.moonpool; if (mp == null || mp.x1 <= mp.x0) return;
            var q = GameObject.CreatePrimitive(PrimitiveType.Quad);
            Destroy(q.GetComponent<Collider>());
            q.name = "Moonpool water";
            q.transform.SetParent(Body, false);
            q.transform.localPosition = G((mp.x0 + mp.x1) / 2, mp.y, (mp.z0 + mp.z1) / 2);
            q.transform.localRotation = Quaternion.Euler(90, 0, 0);
            q.transform.localScale = new Vector3(mp.z1 - mp.z0, mp.x1 - mp.x0, 1);
            var m = new Material(Shader.Find("Deep/Glass"));
            m.SetColor("_Tint", new Color(0.2f, 0.75f, 0.7f)); m.SetFloat("_Clear", 0.45f);
            q.GetComponent<MeshRenderer>().sharedMaterial = m;
            q.GetComponent<MeshRenderer>().shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
        }

        // ---- light ------------------------------------------------------------------------------------------------
        void Lamps()
        {
            for (int i = 0; i < L.lamps.Length; i++)
                lamps.Add((G(L.lamps[i].pos), new Color(1f, 0.74f, 0.46f) * 2.4f, 7.5f, false, i));
            foreach (var li in L.lights)
            {
                var p = G(li.pos);
                if (li.kind == "flood") lamps.Add((p + G(li.dir) * 3f, new Color(0.85f, 0.92f, 1f) * 9f, 30f, true, -1));
                else if (li.kind == "lantern") lamps.Add((p + G(li.dir) * 1.5f, new Color(1f, 0.85f, 0.6f) * 6f, 22f, true, -1));
                else lamps.Add((p, new Color(1f, 0.78f, 0.5f) * 2.2f, 5f, false, -1));
            }
        }

        // the most the shaders get each frame: the nearest lamps to the camera
        readonly List<(float d, int i)> near = new List<(float, int)>();
        void LateUpdate()
        {
            powerK = Mathf.MoveTowards(powerK, power ? 1 : 0, Time.deltaTime * 0.8f);
            Shader.SetGlobalFloat("_DeepPower", powerK);
            var cam = Camera.main; if (!cam) return;
            var c = cam.transform.position;
            near.Clear();
            for (int i = 0; i < lamps.Count; i++)
            {
                float d = (WorldPoint(lamps[i].local) - c).magnitude;
                if (d < lamps[i].range + 40f) near.Add((d, i));
            }
            near.Sort((a, b) => a.d.CompareTo(b.d));
            int n = 0;
            float t = Time.time;
            foreach (var (_, i) in near)
            {
                if (n >= lampPos.Length) break;
                var lp = lamps[i];
                Color col;
                if (powerK > 0.01f)
                {
                    // the lamps come up unevenly, a stutter as the dynamo takes the load
                    float k = Mathf.Clamp01(powerK * 1.4f - (lp.room >= 0 ? (lp.room % 5) * 0.08f : 0.2f));
                    col = lp.col * k * (powerK < 1 ? 0.8f + 0.2f * Mathf.PerlinNoise(t * 9f, i) : 1f);
                }
                else if (lp.room >= 0 && lp.room % 3 == 0)
                    col = new Color(0.5f, 0.06f, 0.03f) * (0.35f + 0.25f * Mathf.PerlinNoise(t * 1.3f, i * 3.1f));   // the emergency lamps
                else continue;
                var w = WorldPoint(lp.local);
                lampPos[n] = new Vector4(w.x, w.y, w.z, lp.range);
                lampCol[n] = new Vector4(col.r, col.g, col.b, lp.water ? 1 : 0);
                n++;
            }
            Shader.SetGlobalVectorArray("_DeepLampPos", lampPos);
            Shader.SetGlobalVectorArray("_DeepLampCol", lampCol);
            Shader.SetGlobalFloat("_DeepLampCount", n);
        }

        // ---- sailing ----------------------------------------------------------------------------------------------
        // She is heavy: the screw takes many seconds to bring her up to the telegraph's speed, the rudder only bites
        // with way on her, the ballast chases the depth ordered at under a metre a second, and the planes pitch her
        // nose as she climbs or dives. Without power the screw stops and she drifts to a halt where she is. She
        // grounds on the seabed (a scrape, and the bow is thrown up) and can surface no higher than her deck awash.
        public static readonly string[] TeleNames = { "Full astern", "Half astern", "Stop", "Slow ahead", "Half ahead", "Full ahead" };
        static readonly float[] TeleSpeed = { -2.4f, -1.2f, 0f, 1.6f, 3.4f, 5.6f };
        public int telegraph = 2;            // index into TeleNames
        public float rudder;                 // -1 hard to port .. 1 hard to starboard
        public bool holdHeading, holdDepth = true;
        public float headingOrder, depthOrder;
        public float speed, vSpeed, heading, grounded;
        float pitchV, rollV, restPitch, restRoll;
        bool sailingInit;
        public float Depth => -Body.position.y;
        public string groundedMsg;

        void InitSailing()
        {
            sailingInit = true;
            var e = Body.rotation.eulerAngles;
            heading = e.y; restPitch = Mathf.DeltaAngle(0, e.x); restRoll = Mathf.DeltaAngle(0, e.z);
            pitchV = restPitch; rollV = restRoll;
            headingOrder = heading; depthOrder = Depth;
        }

        // the lowest her axis may sit at a point s metres along her (the keel tapers to the bow and stern)
        float KeelFloor(Vector3 pos, Vector3 fwd, float s)
        {
            var bed = DeepBoot.I ? DeepBoot.I.seabed : null; if (!bed) return float.MinValue;
            var p = pos + fwd * s;
            float taper = Mathf.Lerp(1f, 0.55f, Mathf.Clamp01((Mathf.Abs(s) - 22f) / 12f));
            return bed.SampleY(p.x, p.z) + Radius * taper - 0.45f;
        }

        void Update() { Sail(Mathf.Min(Time.deltaTime, 0.05f)); }

        public void Sail(float dt)
        {
            if (!sailingInit) InitSailing();
            bool engine = powerK > 0.95f;
            float want = engine ? TeleSpeed[telegraph] : 0f;
            float acc = Mathf.Abs(want) > Mathf.Abs(speed) && Mathf.Sign(want) == Mathf.Sign(speed + 1e-4f) ? 0.22f : 0.35f;
            speed = Mathf.MoveTowards(speed, want, acc * dt);

            if (holdHeading) rudder = Mathf.Clamp(Mathf.DeltaAngle(heading, headingOrder) / 20f, -1f, 1f);
            float bite = Mathf.Clamp01(Mathf.Abs(speed) / 2.2f) * Mathf.Sign(speed);
            float yawRate = rudder * 6.5f * bite;
            heading = Mathf.Repeat(heading + yawRate * dt, 360f);

            // ballast: toward the depth ordered (a depth hold), or held level
            float wantV = engine || powerK > 0.2f ? Mathf.Clamp((depthOrder - Depth) * 0.12f, -0.75f, 0.75f) : 0f;
            vSpeed = Mathf.MoveTowards(vSpeed, wantV, 0.12f * dt);

            var rot = Quaternion.Euler(0, heading, 0);
            var fwd = rot * Vector3.forward;
            var pos = Body.position + fwd * speed * dt + Vector3.down * vSpeed * dt;
            // the surface: no higher than her deck awash
            float top = -3.3f;
            if (pos.y > top) { pos.y = top; vSpeed = Mathf.Min(vSpeed, 0); if (depthOrder < -top) depthOrder = -top; }
            // the seabed: lift her over it; a hard bow strike stops her
            float floorMid = KeelFloor(pos, fwd, 0), floorBow = KeelFloor(pos, fwd, 30f * Mathf.Sign(speed + 1e-4f)), floorStern = KeelFloor(pos, fwd, -30f * Mathf.Sign(speed + 1e-4f));
            float need = Mathf.Max(floorMid, Mathf.Max(floorBow, floorStern));
            bool onBottom = pos.y <= need + 0.05f;
            if (pos.y < need)
            {
                if (floorBow > pos.y + 0.6f && Mathf.Abs(speed) > 0.8f)
                {
                    speed *= 0.3f; groundedMsg = "She strikes the bottom!";
                }
                pos.y = need; vSpeed = Mathf.Min(vSpeed, 0);
                if (depthOrder > Depth + 0.5f) depthOrder = -pos.y;
            }
            grounded = onBottom ? grounded + dt : 0;

            // attitude: resting, she lies as she settled; under way, the planes pitch her and she heels into a turn
            float tp, tr;
            if (onBottom && Mathf.Abs(speed) < 0.3f) { tp = restPitch; tr = restRoll; }
            else
            {
                tp = Mathf.Clamp(vSpeed * 9f, -9f, 9f) + (onBottom ? Mathf.Atan2(floorStern - floorBow, 60f) * Mathf.Rad2Deg * Mathf.Sign(speed + 1e-4f) : 0);
                tr = -yawRate * 0.9f + Mathf.Sin(Time.time * 0.37f) * 0.4f;
                if (!onBottom) { restPitch = Mathf.Lerp(restPitch, 0, dt * 0.2f); restRoll = Mathf.Lerp(restRoll, 0, dt * 0.2f); }
            }
            pitchV = Mathf.Lerp(pitchV, tp, 1 - Mathf.Exp(-dt * 0.8f));
            rollV = Mathf.Lerp(rollV, tr, 1 - Mathf.Exp(-dt * 0.8f));
            Body.SetPositionAndRotation(pos, Quaternion.Euler(pitchV, heading, rollV));
        }

        public string SpeedText => $"{speed * 1.944f:0.0} kn";
        public void Telegraph(int step) { telegraph = Mathf.Clamp(telegraph + step, 0, TeleNames.Length - 1); }

        // ---- what a hand aboard can use ---------------------------------------------------------------------------
        public NLStation StationNear(Vector3 proxyPos, float reach = 1.3f)
        {
            NLStation best = null; float bd = reach;
            var g = FromLocal(Proxy.InverseTransformPoint(proxyPos));
            foreach (var s in L.stations)
            {
                float d = new Vector2(s.pos[0] - g.x, s.pos[2] - g.z).magnitude;
                if (d < bd && Mathf.Abs(g.y - s.pos[1] - 0.8f) < 1.3f) { bd = d; best = s; }
            }
            return best;
        }

        // the ship's local point of a station's standing spot (feet), and the yaw (ship frame) to face it
        public Vector3 StandLocal(NLStation s) => G(s.pos);
        public static float FacingYaw(NLStation s) => Mathf.Atan2(s.facing[2], s.facing[0]) * Mathf.Rad2Deg;   // G: local x = gen z, local z = gen x
    }
}
