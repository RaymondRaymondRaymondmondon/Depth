// The opening (stage 5; the design doc, "The start and progression"): "Players spawn at night on a vulnerable inflatable
// life raft in the middle of a dark, rolling ocean. They row through dangerous surface waters, navigating by the faint,
// dying emergency lights of a massive derelict submarine breaching the surface: the Nautilus. The first goal is to
// board it, restore primary power, and dive before a surface storm or a surface-hunting leviathan destroys them."
//
// How it runs:
//   - Night. The Nautilus wallows at the surface a few hundred metres off, dark, her breakers tripped, red emergency
//     lamps stuttering (a beacon on her cupola, her windows' red glow). The crew sit in the raft.
//   - The objectives, in order: row to her lights; board her (her deck hatch, climbing from the raft or the water);
//     reset her tripped breakers at the switchboard (the Engine Room: the batteries come back - Silent running); take
//     the helm and dive below 15 m.
//   - The storm builds from the start, full in about ten minutes (the swell, wind, rain, lightning; a breaking sea can
//     flip the raft). If the crew are still on the surface after eight minutes, the Shallows' leviathan rises to hunt
//     it: it circles the raft and rams it over, and if she's still up when it arrives it rams her hull.
//   - Diving completes it: the storm passes overhead, and the campaign begins.
// Dying before boarding puts you back in the raft (righted, where it drifted). Skipped with -skipopening, and in the
// screenshot harness unless a shot asks for it.
using UnityEngine;

namespace Deep
{
    public class Opening : MonoBehaviour
    {
        public static Opening I;
        public static bool Active => I != null && !I.done;
        public bool done, boarded, powered;
        public float t;                       // seconds since the start
        Nautilus ship; Diver diver; Raft raft; Weather wx; Clock clock;
        Transform beacon; Material beaconMat;
        Creature hunter; float huntT, nextRam;
        public const float StormFull = 600f, HuntAfter = 480f;

        public static Opening Begin(DeepBoot b)
        {
            var o = new GameObject("Opening").AddComponent<Opening>(); I = o;
            o.ship = b.ship; o.diver = b.diver; o.clock = b.clock; o.wx = b.weather;
            // night, the moon nearly new: the lights are what you steer by
            b.clock.hour = 21.6f; b.clock.day = 2;
            // she wallows at the surface, dark, her breakers tripped
            var ship = b.ship;
            var p = ship.Body.position; p.y = -3.3f;
            ship.Body.position = p;
            ship.ResetAttitude();
            ship.sys.state = PowerState.Dead; ship.sys.breakersTripped = true; ship.sys.battery = 0.3f;
            ship.power = false; ship.powerK = 0;
            ship.depthOrder = 3.3f;
            // the raft a few hundred metres off, facing her lights
            var fromShip = (b.diver.transform.position - ship.Body.position); fromShip.y = 0;
            if (fromShip.magnitude < 150f) fromShip = fromShip.normalized * 260f;
            if (fromShip.magnitude > 340f) fromShip = fromShip.normalized * 320f;
            var at = ship.Body.position + fromShip; at.y = 0;
            float heading = Mathf.Atan2(-fromShip.x, -fromShip.z) * Mathf.Rad2Deg;
            o.raft = Raft.Spawn(at, heading);
            b.diver.EnterRaft(o.raft, 0);
            b.diver.yaw = heading; b.diver.pitch = 2f;
            o.MakeBeacon();
            b.diver.Toast("Night. A dark shape on the swell ahead, red lights failing on her back. Row to her (W, A/D).");
            return o;
        }

        void MakeBeacon()
        {
            var q = GameObject.CreatePrimitive(PrimitiveType.Quad); Destroy(q.GetComponent<Collider>());
            q.name = "Emergency beacon";
            var pr = ship.L.Room("pilot");
            q.transform.SetParent(ship.Body, false);
            q.transform.localPosition = Nautilus.G((pr.x0 + pr.x1) / 2f, pr.ceil + 3.2f, 0);   // on the cupola's masthead, clear of the swell
            beaconMat = new Material(Shader.Find("Deep/Glow"));
            beaconMat.SetColor("_Color", new Color(1f, 0.12f, 0.05f)); beaconMat.SetFloat("_Size", 2.5f);
            q.GetComponent<MeshRenderer>().sharedMaterial = beaconMat;
            beacon = q.transform;
        }

        // the objectives, in order
        public string Objective
        {
            get
            {
                if (!boarded)
                {
                    float d = Vector3.Distance(diver.EyeWorld, ship.Body.position);
                    return d > 30f ? $"Row toward the red lights ({d:0} m)" : "Board her: the deck hatch amidships (E)";
                }
                if (!powered) return "Reset her tripped breakers at the switchboard (the Engine Room, aft)";
                return "Take the helm in the glass cupola and dive below 15 m (Space/C sets the depth)";
            }
        }

        void Update()
        {
            if (done) return;
            float dt = Time.deltaTime;
            t += dt;
            // the storm builds through the opening
            wx.target = Mathf.Clamp01(t / StormFull) * 1.0f;
            // the beacon stutters like a dying lamp until her power is back
            float flick = powered ? 0f : (Mathf.PerlinNoise(t * 3f, 0.3f) > 0.35f ? 1f : 0.35f) * (0.6f + 0.4f * Mathf.Sin(t * 2.2f));
            if (beaconMat) beaconMat.SetFloat("_Intensity", flick * 7f);

            if (!boarded && diver.aboard) { boarded = true; diver.Toast("Aboard. The air is stale and the lamps are red. Find the switchboard aft and reset the breakers."); }
            if (!powered && !ship.sys.breakersTripped) { powered = true; diver.Toast("The batteries come back. Climb to the cupola, take the helm, and dive."); }

            // a dive below 15 m with her power on ends it
            if (powered && ship.Depth > 15f)
            {
                done = true;
                wx.target = 0f;
                if (beacon) Destroy(beacon.gameObject);
                ReleaseHunter();
                diver.Toast("The Nautilus slips under. Above you the storm rages on, and the sea goes quiet. The campaign begins.");
                return;
            }
            Hunt(dt);
        }

        // the surface hunter: the Shallows' leviathan rises after eight minutes if anything is still on the surface
        void Hunt(float dt)
        {
            if (t < HuntAfter || Life.I == null) return;
            if (hunter == null)
            {
                foreach (var c in Life.I.live) if (c.alive && c.sp.resident && c.sp.biome == "shallows") hunter = c;
                if (hunter == null) return;
                hunter.forced = true; hunter.state = CState.Hunting; hunter.why = "the surface hunt";
                var near = (raft && !raft.flipped ? raft.transform.position : ship.Body.position) + new Vector3(60f, -25f, -40f);
                hunter.pos = near; hunter.vel = Vector3.zero;
                diver.Toast("Something vast moves under the swell.");
            }
            huntT += dt;
            // what's on the surface: the raft, a diver in the water, or her if she hasn't dived
            Vector3 prey;
            bool diverUp = !diver.aboard && diver.piloting == null && diver.EyeWorld.y > -3f;
            if (raft && !raft.flipped && diver.onRaft == raft) prey = raft.transform.position;
            else if (diverUp) prey = diver.EyeWorld;
            else prey = ship.Body.position;
            // it circles once, wide and shallow, then comes straight in from below
            float circle = Mathf.Clamp01(1f - huntT / 22f);
            var off = Quaternion.Euler(0, huntT * 14f, 0) * new Vector3(0, -8f, 35f) * circle;
            hunter.goal = prey + off + Vector3.down * (circle > 0 ? 6f : 1f);
            hunter.state = CState.Hunting;
            nextRam -= dt;
            float reach = hunter.size * 0.35f + 3f;
            if (nextRam <= 0 && (hunter.pos - prey).magnitude < reach)
            {
                nextRam = 14f;
                if (raft && diver.onRaft == raft) raft.Flip("The leviathan rises under you and throws the raft over!");
                else if (diverUp) diver.Hurt(45f, hunter.sp.e.name);
                else if (ship.sys != null)
                {
                    ship.sys.AddBreach(ship.sys.RoomIndexAt(new Vector3(2.5f, 0, 0)), 0.04f);
                    ship.groundedMsg = "The leviathan rams her hull! Dive!";
                }
                huntT = 0;   // it circles again
            }
        }

        void ReleaseHunter() { if (hunter != null) { hunter.forced = false; hunter = null; } }

        // dying before she's boarded: back in the raft, righted, where it drifted
        public bool Respawn()
        {
            if (done || boarded || !raft) return false;
            raft.Right();
            diver.health = 70f; diver.oxygen = diver.oxygenMax;
            diver.EnterRaft(raft, 0);
            diver.Toast("You come to, coughing, in the raft.");
            return true;
        }

        void OnGUI()
        {
            if (done || diver == null || diver.GetComponent<CraftUI>()?.mode != null) return;
            var st = new GUIStyle(GUI.skin.label) { fontSize = 15, alignment = TextAnchor.UpperRight, wordWrap = true };
            st.normal.textColor = new Color(1f, 0.85f, 0.6f);
            var r = new Rect(Screen.width - 470, 52, 450, 60);
            GUI.color = new Color(0, 0, 0, 0.45f); GUI.DrawTexture(new Rect(r.x - 6, r.y - 4, r.width + 12, 52), Texture2D.whiteTexture); GUI.color = Color.white;
            GUI.Label(r, Objective, st);
            string sky = wx.storm < 0.3f ? "The wind is rising" : wx.storm < 0.7f ? "The storm is building" : "The storm is on you";
            GUI.Label(new Rect(r.x, r.y + 24, r.width, 22), sky + (hunter != null ? "  -  something is hunting the surface" : ""), new GUIStyle(st) { fontSize = 13 });
        }
    }
}
