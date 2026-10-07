// The Kite-Sub (stage 4; the design doc: the moonpool "deploys, docks, and upgrades the Kite-Sub", the agile mini-sub
// for tight places): made at the moonpool, it hangs in her well while docked and charges from her batteries.
//   Board it at the moonpool (or swim up to it where you left it) with E. Fly: the mouse steers, W/S thrust, A/D
//   strafe, Space/Ctrl rise and dive, Shift for a burst. Its props are mid-band noise (the Wake hears it). Bring it
//   back under the moonpool and E docks it (you climb out into the moonpool room); E anywhere else leaves it parked
//   and puts you in the water beside it. Its own battery drains under way; past its crush depth the glass creaks and
//   the hull takes damage.
using UnityEngine;

namespace Deep
{
    public class KiteSub : MonoBehaviour
    {
        public static KiteSub I;
        public Nautilus ship; public bool docked = true;
        public float battery = 1f, hull = 1f, yaw, pitch, roll;
        public Vector3 vel;
        public const float CrushDepth = 200f, MaxSpeed = 6.5f, Boost = 9f;
        Transform model;

        public static KiteSub Spawn(Nautilus ship)
        {
            if (I) return I;
            var go = new GameObject("Kite-Sub");
            var k = go.AddComponent<KiteSub>(); I = k; k.ship = ship;
            k.model = new GameObject("model").transform; k.model.SetParent(go.transform, false);
            var body = ModelLibrary.Get("Vehicles/kite_sub", "kite_sub");
            var glass = ModelLibrary.Get("Vehicles/kite_sub", "kite_sub_glass");
            if (body)
            {
                var mat = new Material(Shader.Find("Deep/Lit")); mat.SetFloat("_UseVC", 1); mat.SetFloat("_Roughness", 0.55f); mat.SetFloat("_Metallic", 0.5f); mat.SetFloat("_Cull", 0);
                Part(k.model, "body", body, mat, true);
            }
            if (glass)
            {
                var g = new Material(Shader.Find("Deep/Glass")); g.SetColor("_Tint", new Color(0.6f, 0.75f, 0.72f)); g.SetFloat("_Clear", 0.9f);
                Part(k.model, "glass", glass, g, false);
            }
            k.Dock();
            return k;
        }

        static void Part(Transform parent, string name, Mesh m, Material mat, bool shadows)
        {
            var g = new GameObject(name); g.transform.SetParent(parent, false);
            g.AddComponent<MeshFilter>().sharedMesh = m;
            var r = g.AddComponent<MeshRenderer>(); r.sharedMaterial = mat;
            r.shadowCastingMode = shadows ? UnityEngine.Rendering.ShadowCastingMode.On : UnityEngine.Rendering.ShadowCastingMode.Off;
        }

        // the dock: in the moonpool's well, nose to her bow, riding with her
        Vector3 DockLocal()
        {
            var mp = ship.L.moonpool;
            return Nautilus.G((mp.x0 + mp.x1) / 2f, mp.y - 1.0f, (mp.z0 + mp.z1) / 2f);
        }
        public Vector3 UnderDock => ship.WorldPoint(Nautilus.G((ship.L.moonpool.x0 + ship.L.moonpool.x1) / 2f, -Nautilus.Radius - 2.2f, 0));

        public void Dock()
        {
            docked = true; vel = Vector3.zero;
            transform.SetParent(ship.Body, false);
            transform.localPosition = DockLocal();
            transform.localRotation = Quaternion.identity;
        }

        public void Undock()
        {
            docked = false;
            transform.SetParent(null, true);
            transform.position = UnderDock;
            var e = ship.Body.rotation.eulerAngles; yaw = e.y; pitch = 0; roll = 0;
            transform.rotation = Quaternion.Euler(0, yaw, 0);
        }

        public Vector3 Seat => transform.TransformPoint(new Vector3(0, 0.12f, 1.05f));

        void Update()
        {
            if (docked) battery = Mathf.Min(1f, battery + Time.deltaTime * (ship.power ? 0.01f : 0f));
        }

        // flown by the diver in its seat (Diver calls this every frame while piloting)
        public void Drive(Diver d, float dt)
        {
            yaw = d.yaw; pitch = Mathf.Clamp(d.pitch, -70f, 70f);
            float fwd = Ax(d, KeyCode.W, KeyCode.S), side = Ax(d, KeyCode.D, KeyCode.A), up = Ax(d, KeyCode.Space, KeyCode.LeftControl);
            bool boost = d.inputEnabled && !d.uiOpen && Input.GetKey(KeyCode.LeftShift);
            var rot = Quaternion.Euler(pitch, yaw, 0);
            var wish = rot * new Vector3(side * 0.6f, 0, fwd) + Vector3.up * up * 0.7f;
            if (battery <= 0) wish = Vector3.zero;
            float max = boost ? Boost : MaxSpeed;
            vel = Vector3.Lerp(vel, Vector3.ClampMagnitude(wish, 1f) * max, 1 - Mathf.Exp(-dt * (wish.sqrMagnitude > 0 ? 1.4f : 0.8f)));
            battery = Mathf.Max(0, battery - dt * wish.magnitude * (boost ? 0.0045f : 0.0022f));
            var p = transform.position + vel * dt;
            // the seabed, the surface and the Nautilus
            var bed = DeepBoot.I.seabed;
            float floor = bed.SampleY(p.x, p.z) + 1.0f;
            if (p.y < floor) { p.y = floor; if (vel.y < 0) vel.y = 0; if (vel.magnitude > 4f) Bump(d, vel.magnitude); }
            float surf = Waves.Height(p.x, p.z, Time.time) - 0.6f;
            if (p.y > surf) { p.y = surf; vel.y = Mathf.Min(vel.y, 0); }
            var lp = ship.Body.InverseTransformPoint(p);
            var axis = new Vector3(0, 0, Mathf.Clamp(lp.z, Nautilus.SternX, Nautilus.BowX));
            var rad = lp - axis; float keep = Nautilus.Radius + 1.3f;
            if (rad.magnitude < keep) { lp = axis + rad.normalized * keep; p = ship.Body.TransformPoint(lp); vel *= 0.5f; }
            transform.position = p;
            roll = Mathf.Lerp(roll, -side * 12f, 1 - Mathf.Exp(-dt * 3f));
            transform.rotation = Quaternion.Euler(pitch, yaw, roll);
            // its props in the water, and the pressure
            if (vel.magnitude > 0.5f && Life.I != null && Time.frameCount % 10 == 0) Life.I.sound.Emit(p, 52f + vel.magnitude * 2f, Band.Mid, 0.3f, "a mini-sub's props");
            float over = -p.y - CrushDepth;
            if (over > 0) { hull -= dt * over * 0.002f; if (Time.frameCount % 240 == 0) d.Toast($"The Kite-Sub's glass creaks: {over:0} m past its depth."); }
            if (hull <= 0) { d.Toast("The Kite-Sub's canopy gives way!"); d.LeaveKiteSub(true); hull = 0.3f; }
        }

        void Bump(Diver d, float speed) { hull = Mathf.Max(0, hull - speed * 0.01f); if (Time.frameCount % 30 == 0) d.Toast("The Kite-Sub scrapes the bottom."); }

        static float Ax(Diver d, KeyCode a, KeyCode b) => d.inputEnabled && !d.uiOpen ? (Input.GetKey(a) ? 1 : 0) - (Input.GetKey(b) ? 1 : 0) : 0;
    }
}
