// The Nautilus's moving fittings, built here so they can turn (the Blender model holds the fixed parts: pedestals,
// dial faces): the helm's wheel turns with the rudder; the Bridge telegraph's handle stands at the order rung; the
// depth gauges' needles (the pilot house's has a red one for the depth ordered) and the Bridge clock's hands.
// Each sits on a pivot whose local z is the axis it turns about, pointing the way the viewer looks, so a positive
// angle turns it clockwise to the viewer; 0 is straight up.
using UnityEngine;

namespace Deep
{
    public class ShipFittings : MonoBehaviour
    {
        Nautilus n;
        Transform wheel, handle, pilotDepth, pilotOrder, bridgeDepth, clockH, clockM;
        float wheelA, handleA, pDepthA, pOrderA, bDepthA;
        Material brass, black, red;

        public static ShipFittings Attach(Nautilus n)
        {
            var f = n.gameObject.AddComponent<ShipFittings>();
            f.n = n;
            f.brass = Mat(new Color(0.62f, 0.45f, 0.19f), 0.85f, 0.35f);
            f.black = Mat(new Color(0.03f, 0.03f, 0.03f), 0.2f, 0.6f);
            f.red = Mat(new Color(0.6f, 0.05f, 0.03f), 0.1f, 0.5f);
            f.Build();
            return f;
        }

        static Material Mat(Color c, float metal, float rough)
        {
            var m = new Material(Shader.Find("Deep/Lit"));
            m.SetColor("_BaseColor", c); m.SetFloat("_Metallic", metal); m.SetFloat("_Roughness", rough);
            m.SetFloat("_UseVC", 0); m.SetFloat("_Interior", 1); m.SetFloat("_Cull", 0);
            return m;
        }

        // a pivot at a point (generator frame), its local z along `look` (ship local)
        Transform Pivot(string name, float x, float y, float z, Vector3 look)
        {
            var t = new GameObject(name).transform;
            t.SetParent(n.Body, false);
            t.localPosition = Nautilus.G(x, y, z);
            t.localRotation = Quaternion.LookRotation(look, Vector3.up);
            return t;
        }

        GameObject Part(PrimitiveType type, Transform parent, Vector3 pos, Vector3 euler, Vector3 scale, Material m)
        {
            var g = GameObject.CreatePrimitive(type);
            Destroy(g.GetComponent<Collider>());
            g.transform.SetParent(parent, false);
            g.transform.localPosition = pos; g.transform.localRotation = Quaternion.Euler(euler); g.transform.localScale = scale;
            var r = g.GetComponent<MeshRenderer>(); r.sharedMaterial = m; r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            return g;
        }

        // a needle: a tapered bar from the hub up to `len`, and a hub cap
        Transform Needle(Transform pivot, float len, float w, Material m, float zOff)
        {
            var t = new GameObject("needle").transform; t.SetParent(pivot, false); t.localPosition = new Vector3(0, 0, zOff);
            Part(PrimitiveType.Cube, t, new Vector3(0, len * 0.4f, 0), Vector3.zero, new Vector3(w, len, 0.006f), m);
            Part(PrimitiveType.Cube, t, new Vector3(0, len * 0.93f, 0), new Vector3(0, 0, 45), new Vector3(w * 1.2f, w * 1.2f, 0.006f), m);
            Part(PrimitiveType.Cylinder, t, Vector3.zero, new Vector3(90, 0, 0), new Vector3(w * 2.6f, 0.006f, w * 2.6f), brass);
            return t;
        }

        void Build()
        {
            var fwd = Vector3.forward;     // ship local +z is the bow (gen +x)
            // the helm's wheel: a rim of 24 short bars, eight spokes with turned handles, a boss
            var hw = n.L.helmWheel;
            float wx = hw != null && hw.Length == 3 ? hw[0] - 0.08f : 22.6f, wy = hw != null && hw.Length == 3 ? hw[1] : 4.72f;
            var wp = Pivot("Helm wheel", wx, wy, 0, fwd);
            wheel = new GameObject("wheel").transform; wheel.SetParent(wp, false);
            const float R = 0.36f; const int seg = 24;
            for (int i = 0; i < seg; i++)
            {
                float a = (i + 0.5f) * 360f / seg;
                var p = Quaternion.Euler(0, 0, a) * new Vector3(0, R, 0);
                Part(PrimitiveType.Cylinder, wheel, p, new Vector3(0, 0, a + 90), new Vector3(0.035f, 2 * Mathf.PI * R / seg * 0.53f, 0.035f), brass);
            }
            for (int i = 0; i < 8; i++)
            {
                float a = i * 45f;
                Part(PrimitiveType.Cylinder, wheel, Quaternion.Euler(0, 0, a) * new Vector3(0, R * 0.5f, 0), new Vector3(0, 0, a), new Vector3(0.022f, R * 0.5f, 0.022f), brass);
                Part(PrimitiveType.Cylinder, wheel, Quaternion.Euler(0, 0, a) * new Vector3(0, R + 0.08f, 0), new Vector3(0, 0, a), new Vector3(0.03f, 0.07f, 0.03f), brass);
                Part(PrimitiveType.Sphere, wheel, Quaternion.Euler(0, 0, a) * new Vector3(0, R + 0.15f, 0), Vector3.zero, Vector3.one * 0.045f, brass);
            }
            Part(PrimitiveType.Cylinder, wheel, Vector3.zero, new Vector3(90, 0, 0), new Vector3(0.11f, 0.05f, 0.11f), brass);
            Part(PrimitiveType.Cylinder, wp, new Vector3(0, 0, 0.09f), new Vector3(90, 0, 0), new Vector3(0.05f, 0.09f, 0.05f), brass);   // the shaft

            // the pilot house's depth gauge (its dial faces the helmsman)
            var pg = n.L.gauges != null && n.L.gauges.depth != null && n.L.gauges.depth.Length == 3 ? n.L.gauges.depth : new[] { 22.95f, 4.85f, -0.6f };
            var pp = Pivot("Pilot depth gauge", pg[0], pg[1], pg[2], fwd);
            pilotOrder = Needle(pp, 0.11f, 0.008f, red, -0.012f);
            pilotDepth = Needle(pp, 0.13f, 0.012f, black, -0.02f);

            // the Bridge telegraph's handle, on the face toward the hand at it
            var tpos = new[] { 23.9f, 0, 1.5f };
            foreach (var st in n.L.stations) if (st.kind == "telegraph") tpos = new[] { st.pos[0] + 0.5f, 0, st.pos[2] };
            var tp = Pivot("Telegraph handle", tpos[0], Nautilus.Floor + 1.22f, tpos[2], fwd);
            handle = new GameObject("handle").transform; handle.SetParent(tp, false);
            Part(PrimitiveType.Cube, handle, new Vector3(0, 0.13f, 0), Vector3.zero, new Vector3(0.035f, 0.26f, 0.025f), brass);
            Part(PrimitiveType.Cylinder, handle, new Vector3(0, 0.27f, -0.04f), new Vector3(90, 0, 0), new Vector3(0.045f, 0.06f, 0.045f), black);
            Part(PrimitiveType.Cylinder, handle, Vector3.zero, new Vector3(90, 0, 0), new Vector3(0.07f, 0.02f, 0.07f), brass);
            // the order rungs round the dial: short black ticks at the six positions
            for (int i = 0; i < Nautilus.TeleNames.Length; i++)
            {
                var tk = new GameObject("tick").transform; tk.SetParent(tp, false); tk.localRotation = Rot(TeleAngle(i));
                Part(PrimitiveType.Cube, tk, new Vector3(0, 0.19f, 0.004f), Vector3.zero, new Vector3(0.012f, 0.04f, 0.004f), i == 2 ? red : black);
            }

            // the Bridge: the depth gauge on the port wall and the clock on the starboard wall (both face into the room)
            float gx = n.L.bridgeGauges != null && n.L.bridgeGauges.depth != null ? n.L.bridgeGauges.depth[0] : 27f;
            var bd = Pivot("Bridge depth gauge", gx, 0.8f, -1.81f, -Vector3.right);
            bridgeDepth = Needle(bd, 0.22f, 0.016f, black, 0);
            var bc = Pivot("Bridge clock", gx, 0.8f, 1.81f, Vector3.right);
            clockH = Needle(bc, 0.14f, 0.018f, black, 0);
            clockM = Needle(bc, 0.22f, 0.012f, black, -0.006f);
        }

        // clockwise to the viewer looking down the pivot's +z (Unity turns a positive z angle anticlockwise)
        static Quaternion Rot(float clockwise) => Quaternion.Euler(0, 0, -clockwise);

        // the telegraph's order rungs: Stop straight up, ahead to the right, astern to the left
        static float TeleAngle(int i) => (i - 2) * 28f;
        // a gauge's face: 0 m at the bottom left, 300 m at the bottom right (270 degrees)
        static float GaugeAngle(float depth) => -135f + 270f * Mathf.Clamp01(depth / 300f);

        void LateUpdate()
        {
            float dt = Time.deltaTime;
            float k = 1 - Mathf.Exp(-dt * 6f);
            wheelA = Mathf.Lerp(wheelA, n.rudder * 420f, k);
            handleA = Mathf.Lerp(handleA, TeleAngle(n.telegraph), 1 - Mathf.Exp(-dt * 10f));
            // the needles lag and shiver a little, like real ones; dead in the water they still read (they're mechanical)
            float jitter = Mathf.Sin(Time.time * 13f) * 0.4f * Mathf.Clamp01(Mathf.Abs(n.speed));
            pDepthA = Mathf.Lerp(pDepthA, GaugeAngle(n.Depth) + jitter, 1 - Mathf.Exp(-dt * 3f));
            pOrderA = Mathf.Lerp(pOrderA, GaugeAngle(n.depthOrder), k);
            bDepthA = Mathf.Lerp(bDepthA, GaugeAngle(n.Depth) + jitter, 1 - Mathf.Exp(-dt * 3f));
            wheel.localRotation = Rot(wheelA);
            handle.localRotation = Rot(handleA);
            pilotDepth.localRotation = Rot(pDepthA);
            pilotOrder.localRotation = Rot(pOrderA);
            bridgeDepth.localRotation = Rot(bDepthA);
            var c = DeepBoot.I ? DeepBoot.I.clock : null;
            if (c)
            {
                float h = c.hour % 12f;
                clockH.localRotation = Rot(h * 30f);
                clockM.localRotation = Rot((c.hour * 60f % 60f) * 6f);
            }
        }
    }
}
