// The diver: first person.
// - In the sea you swim where you look (WASD), rise with Space, sink with Ctrl, sprint with Shift; at the surface you
//   tread water with your head out. The tank: 45 s of air (the doc's survival meters come in stage 4), drained while
//   your head is under, refilled in air.
// - Aboard the Nautilus you walk (Space jumps), climb ladders (W/Space up, S/Ctrl down), and use what's in front of you
//   with E. The body lives in the ship's collision proxy (Nautilus.cs); the camera is mapped onto the real ship.
// - F switches the helmet lamp.
// The camera is its own object: `head` is where the eye is in whichever space the body is in.
using UnityEngine;

namespace Deep
{
    public class Diver : MonoBehaviour
    {
        public Camera cam;
        public Transform head;
        public CharacterController cc;
        public float yaw, pitch;
        public Vector3 vel;
        public float oxygen = 45f, oxygenMax = 45f;
        public bool inputEnabled = true;
        public bool aboard, lampOn = true, climbing;
        public Nautilus ship;
        public NLStation manning;        // the station this hand is working (the helm, the telegraph...)
        public string hint = "";
        public string toast = ""; float toastT;
        public const float Swim = 3.6f, Sprint = 5.8f, Walk = 2.4f, Run = 4.0f, EyeHeight = 0.7f;

        public static Diver Spawn(Vector3 at)
        {
            var go = new GameObject("Diver");
            go.transform.position = at;
            var d = go.AddComponent<Diver>();
            d.cc = go.AddComponent<CharacterController>();
            d.cc.height = 1.6f; d.cc.radius = 0.35f; d.cc.center = Vector3.zero; d.cc.slopeLimit = 60; d.cc.stepOffset = 0.3f;
            d.head = new GameObject("Head").transform; d.head.SetParent(go.transform, false); d.head.localPosition = new Vector3(0, EyeHeight, 0);
            var cg = new GameObject("Eye");
            d.cam = cg.AddComponent<Camera>(); d.cam.tag = "MainCamera"; d.cam.nearClipPlane = 0.05f; d.cam.fieldOfView = 72;
            cg.AddComponent<AudioListener>();
            d.yaw = 90f;
            return d;
        }

        public void Toast(string s) { toast = s; toastT = 3.5f; }

        void Update()
        {
            float dt = Time.deltaTime;
            if (inputEnabled)
            {
                if (Input.GetMouseButtonDown(0)) { Cursor.lockState = CursorLockMode.Locked; Cursor.visible = false; }
                if (Input.GetKeyDown(KeyCode.Escape)) { Cursor.lockState = CursorLockMode.None; Cursor.visible = true; }
                if (Cursor.lockState == CursorLockMode.Locked)
                {
                    yaw += Input.GetAxisRaw("Mouse X") * 2.2f;
                    pitch = Mathf.Clamp(pitch - Input.GetAxisRaw("Mouse Y") * 2.2f, -88f, 88f);
                }
                if (Input.GetKeyDown(KeyCode.F)) lampOn = !lampOn;
            }
            transform.rotation = Quaternion.Euler(0, yaw, 0);
            head.localRotation = Quaternion.Euler(pitch, 0, 0);
            if (aboard && manning != null) ManStep(dt);
            else if (aboard) WalkStep(dt); else SwimStep(dt);
            Uses();
            if (ship && ship.groundedMsg != null) { if (aboard) Toast(ship.groundedMsg); ship.groundedMsg = null; }
            if (toastT > 0) toastT -= dt; else toast = "";
        }

        float Axis(KeyCode pos, KeyCode neg) => inputEnabled ? (Input.GetKey(pos) ? 1 : 0) - (Input.GetKey(neg) ? 1 : 0) : 0;
        bool Down(KeyCode a, KeyCode b) => inputEnabled && (Input.GetKey(a) || Input.GetKey(b));

        void SwimStep(float dt)
        {
            var p = transform.position;
            float surf = Waves.Height(p.x, p.z, Time.time);
            bool atSurface = p.y + EyeHeight > surf - 0.15f;
            Vector3 wish = head.forward * Axis(KeyCode.W, KeyCode.S) + head.right * Axis(KeyCode.D, KeyCode.A)
                         + Vector3.up * ((Down(KeyCode.Space, KeyCode.Space) ? 1 : 0) - (Down(KeyCode.LeftControl, KeyCode.C) ? 1 : 0));
            if (wish.sqrMagnitude > 1) wish.Normalize();
            float speed = Down(KeyCode.LeftShift, KeyCode.LeftShift) ? Sprint : Swim;
            // water: quick to start, slow to stop (drag)
            vel = Vector3.Lerp(vel, wish * speed, 1 - Mathf.Exp(-dt * (wish.sqrMagnitude > 0 ? 2.6f : 1.4f)));
            if (atSurface)
            {
                // treading water: the head rides the swell; you can't climb out of the sea by swimming up
                float want = surf - EyeHeight + 0.25f;
                if (p.y > want && vel.y > 0) vel.y = 0;
                vel.y += (want - p.y) * 3f * dt;
            }
            cc.Move(vel * dt);
            bool headUnder = transform.position.y + EyeHeight < Waves.Height(p.x, p.z, Time.time) - 0.05f;
            oxygen = headUnder ? Mathf.Max(0, oxygen - dt) : Mathf.Min(oxygenMax, oxygen + dt * 12f);
        }

        void WalkStep(float dt)
        {
            // ladders: stand in one's column and climb with W/Space, down with S/Ctrl
            var g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
            NLLadder lad = null;
            foreach (var l in ship.L.ladders)
                if (new Vector2(g.x - l.x, g.z - l.z).magnitude < 0.6f && g.y > l.y0 - 0.2f && g.y < l.y1 + 0.85f) lad = l;
            float fwd = Axis(KeyCode.W, KeyCode.S), side = Axis(KeyCode.D, KeyCode.A);
            float up = (Down(KeyCode.Space, KeyCode.W) ? 1 : 0) - (Down(KeyCode.S, KeyCode.LeftControl) ? 1 : 0);
            climbing = lad != null && (climbing || Mathf.Abs(up) > 0) && !(g.y < lad.y0 + 0.85f && up < 0 && !climbing);
            if (lad != null && climbing)
            {
                vel = new Vector3(0, up * 1.9f, 0);
                if (g.y > lad.y1 + 0.75f) vel += transform.forward * fwd * 1.2f;   // step off at the top
                if (g.y < lad.y0 + 0.82f && up < 0) climbing = false;
                cc.Move(vel * dt);
                oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f);
                return;
            }
            climbing = false;
            var wish = transform.forward * fwd + transform.right * side;
            if (wish.sqrMagnitude > 1) wish.Normalize();
            float speed = Down(KeyCode.LeftShift, KeyCode.LeftShift) ? Run : Walk;
            var h = Vector3.Lerp(new Vector3(vel.x, 0, vel.z), wish * speed, 1 - Mathf.Exp(-dt * 12f));
            vel.x = h.x; vel.z = h.z;
            if (cc.isGrounded)
            {
                vel.y = -1f;
                if (inputEnabled && Input.GetKeyDown(KeyCode.Space)) vel.y = 3.4f;
            }
            else vel.y -= 9.8f * dt;
            cc.Move(vel * dt);
            oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f);
            // down the moonpool's well: out into the sea under her keel
            var mp = ship.L.moonpool;
            g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
            if (mp != null && g.y < mp.y - 0.6f && g.x > mp.x0 && g.x < mp.x1) LeaveTo(Nautilus.G((mp.x0 + mp.x1) / 2, -Nautilus.Radius - 1.6f, (mp.z0 + mp.z1) / 2), "Out through the moonpool");
        }

        // E: what's in front of you
        void Uses()
        {
            hint = "";
            if (!ship) return;
            bool e = inputEnabled && Input.GetKeyDown(KeyCode.E);
            if (aboard)
            {
                var g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
                foreach (var l in ship.L.ladders)
                    if (l.hatch == "deck" && climbing && g.y > l.y1 - 0.8f)
                    {
                        hint = "E  Open the deck hatch";
                        if (e) foreach (var hk in ship.L.hatches) if (hk.kind == "deck") LeaveTo(Nautilus.G(hk.outside), "Out on her deck");
                        return;
                    }
                if (manning != null)
                {
                    hint = "E  Step away";
                    if (e) manning = null;
                    return;
                }
                var s = ship.StationNear(transform.position);
                if (s == null) return;
                switch (s.kind)
                {
                    case "helm":
                    case "telegraph":
                        hint = "E  Take " + StationName(s.kind).ToLowerInvariant() + (ship.power ? "" : "  (no power: she won't answer)");
                        if (e) Man(s);
                        break;
                    case "power":
                        hint = ship.power ? "E  Throw the main breaker (lights out)" : "E  Close the main breaker (power the lamps)";
                        if (e) { ship.power = !ship.power; Toast(ship.power ? "The dynamo takes the load. The lamps come up." : "The lamps die."); }
                        break;
                    case "airlock":
                        hint = "E  Cycle the airlock and swim out";
                        if (e) foreach (var hk in ship.L.hatches) if (hk.kind == "airlock") LeaveTo(Nautilus.G(hk.outside), "The airlock floods and the outer door swings open");
                        break;
                    case "moonpool":
                        hint = "Drop into the moonpool to dive";
                        break;
                    default:
                        hint = StationName(s.kind) + "  (comes alive in the next build)";
                        break;
                }
                return;
            }
            // in the sea: the airlock's outer door, the deck hatch, the moonpool from below
            var lp = Nautilus.FromLocal(ship.Body.InverseTransformPoint(transform.position));
            foreach (var hk in ship.L.hatches)
            {
                if ((new Vector3(hk.outside[0], hk.outside[1], hk.outside[2]) - lp).magnitude > 2.4f) continue;
                hint = hk.kind == "airlock" ? "E  Enter the airlock" : "E  Open the deck hatch and climb down";
                if (e)
                {
                    if (hk.kind == "airlock") Board(Nautilus.G(hk.inside), Nautilus.FacingYaw(new NLStation { facing = new[] { 0f, 0f, -1f } }), "The airlock drains. You're aboard.");
                    else Board(Nautilus.G(hk.inside[0], hk.inside[1] - 1.2f, hk.inside[2] + 0.7f), 180f, "You drop through the hatch.");
                }
                return;
            }
            var m = ship.L.moonpool;
            if (m != null && lp.x > m.x0 && lp.x < m.x1 && lp.z > m.z0 && lp.z < m.z1 && lp.y > -Nautilus.Radius - 3f && lp.y < -Nautilus.Radius + 0.5f)
            {
                hint = "E  Climb up into the moonpool";
                if (e) Board(Nautilus.G(m.x0 - 0.8f, Nautilus.Floor, 0), 90f, "You haul yourself out of the moonpool.");
            }
        }

        // working a station: the hand stands at it, looking about freely; the keys go to the station
        public void Man(NLStation s)
        {
            Board(ship.StandLocal(s), Nautilus.FacingYaw(s));
            manning = s;
            if (s.kind == "helm") Toast(ship.power ? "A/D rudder, W/S telegraph, Space/C depth, H hold heading, X centre the rudder" : "The wheel turns, but nothing answers: she has no power.");
            else Toast("W/S ring the telegraph");
        }

        void ManStep(float dt)
        {
            if (!inputEnabled) return;
            var n = ship;
            if (Input.GetKeyDown(KeyCode.W)) n.Telegraph(1);
            if (Input.GetKeyDown(KeyCode.S)) n.Telegraph(-1);
            if (manning.kind != "helm") return;
            float r = Axis(KeyCode.D, KeyCode.A);
            if (r != 0) { n.holdHeading = false; n.rudder = Mathf.Clamp(n.rudder + r * dt * 0.9f, -1f, 1f); }
            if (Input.GetKeyDown(KeyCode.X)) { n.holdHeading = false; n.rudder = 0; }
            if (Input.GetKeyDown(KeyCode.H)) { n.holdHeading = !n.holdHeading; n.headingOrder = n.heading; Toast(n.holdHeading ? $"Holding {n.heading:000}" : "Heading hold off"); }
            float dv = (Input.GetKey(KeyCode.C) || Input.GetKey(KeyCode.LeftControl) ? 1 : 0) - (Input.GetKey(KeyCode.Space) ? 1 : 0);
            if (dv != 0) n.depthOrder = Mathf.Clamp(n.depthOrder + dv * dt * 3f, 3.3f, 400f);
        }

        public static string StationName(string kind)
        {
            switch (kind)
            {
                case "helm": return "The helm"; case "telegraph": return "The engine telegraph"; case "sonar": return "The sonar";
                case "fabricator": return "The fabricator"; case "forge": return "The forge"; case "planter": return "The planter";
                case "larder": return "The larder"; case "grill": return "The galley grill"; case "desalinator": return "The desalinator";
                case "lockers": return "The suit lockers"; case "oxygen": return "The oxygen rack"; case "boiler": return "The boiler";
                case "pumps": return "The bilge pumps";
            }
            return kind;
        }

        // aboard at a standing spot (ship local, feet), facing a yaw in the ship's frame
        public void Board(Vector3 standLocal, float yawShip, string msg = null)
        {
            aboard = true; climbing = false; manning = null;
            cc.enabled = false; transform.position = ship.ProxyPoint(standLocal) + Vector3.up * (cc.height / 2 + 0.02f); cc.enabled = true;
            yaw = yawShip; vel = Vector3.zero;
            transform.rotation = Quaternion.Euler(0, yaw, 0);
            if (msg != null) Toast(msg);
        }

        // out into the sea at a point by the ship (ship local)
        public void LeaveTo(Vector3 local, string msg = null)
        {
            var w = ship.WorldPoint(local);
            var fwd = ship.RotToWorld(Quaternion.Euler(0, yaw, 0)) * Vector3.forward;
            aboard = false; climbing = false; manning = null;
            cc.enabled = false; transform.position = w; cc.enabled = true;
            yaw = Mathf.Atan2(fwd.x, fwd.z) * Mathf.Rad2Deg; vel = Vector3.zero;
            if (msg != null) Toast(msg);
        }

        void LateUpdate()
        {
            UnderwaterLook.Aboard = aboard;
            if (aboard && ship) cam.transform.SetPositionAndRotation(ship.ToWorld(head.position), ship.RotToWorld(head.rotation));
            else cam.transform.SetPositionAndRotation(head.position, head.rotation);
            // the helmet lamp, a little above and ahead of the eye
            var c = cam.transform;
            var lampAt = c.position + c.up * 0.12f + c.forward * 0.1f;
            Shader.SetGlobalVector("_DeepHeadPos", new Vector4(lampAt.x, lampAt.y, lampAt.z, lampOn ? 24f : 0f));
            Shader.SetGlobalVector("_DeepHeadDir", new Vector4(c.forward.x, c.forward.y, c.forward.z, 0.8f));
            Shader.SetGlobalVector("_DeepHeadCol", new Vector4(1.0f, 0.95f, 0.82f, aboard ? 0 : 1));
        }

        public Vector3 EyeWorld => aboard && ship ? ship.ToWorld(head.position) : head.position;
        public float Depth => Mathf.Max(0, -EyeWorld.y);

        public void Place(Vector3 eye, float yawDeg, float pitchDeg)
        {
            aboard = false; climbing = false; manning = null;
            cc.enabled = false; transform.position = eye - Vector3.up * EyeHeight; cc.enabled = true;
            yaw = yawDeg; pitch = pitchDeg; vel = Vector3.zero;
            transform.rotation = Quaternion.Euler(0, yaw, 0); head.localRotation = Quaternion.Euler(pitch, 0, 0);
            LateUpdate();
        }

        // for shots: aboard, at a point in the generator's frame, looking along a yaw (ship frame) and pitch
        public void PlaceAboard(float gx, float gz, float floorY, float yawShip, float pitchDeg)
        {
            Board(Nautilus.G(gx, floorY, gz), yawShip);
            pitch = pitchDeg; head.localRotation = Quaternion.Euler(pitch, 0, 0);
            LateUpdate();
        }
    }
}
