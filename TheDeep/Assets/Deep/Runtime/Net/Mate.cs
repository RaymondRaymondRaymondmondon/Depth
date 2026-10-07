// A crewmate on another PC (stage 6): their diver as this PC sees it. Their pose arrives about 15 times a second (Net.cs)
// and is drawn a tenth of a second behind, between the samples either side, so it moves smoothly. Aboard, the pose is
// in the Nautilus's own frame (like this PC's diver, they walk in her proxy), so they move with her as she rolls.
// The body is diver.glb's parts (tools/artgen/deep_diver.py), posed in code: swimming (stretched out along the way
// they're going, a flutter kick; treading water upright when still), walking (legs and arms swinging with the pace),
// climbing, rowing in the raft, working a station. The canvas suit is tinted by seat; their helmet lamp lights the
// water like this diver's (Nautilus.cs adds it to the lamps the shaders get), and the sea senses them (Life.ISense).
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public struct MatePose
    {
        public byte mode;           // 0 swimming, 1 aboard, 2 in the raft, 3 in the Kite-Sub, 4 not in the world yet
        public Vector3 pos;         // the body's middle: in the world, or (aboard) in her proxy's frame from ProxyOrigin
        public float yaw, pitch;    // the look (aboard: the yaw is in the ship's frame)
        public Vector3 vel;
        public byte flags;          // 1 lamp, 2 climbing, 4 sprinting, 8 rowing
        public byte station;        // the station being worked (Mate.Stations index), 255 none
        public byte health, seat;   // health 0..100; the raft seat
        public const int Lamp = 1, Climb = 2, Sprint = 4, Row = 8;

        public void Write(NetW w)
        {
            w.U8(mode).V3(pos).F(yaw).F(pitch).V3s(vel).U8(flags).U8(station).U8(health).U8(seat);
        }
        public static MatePose Read(NetR r)
        {
            return new MatePose { mode = (byte)r.U8(), pos = r.V3(), yaw = r.F(), pitch = r.F(), vel = r.V3s(), flags = (byte)r.U8(), station = (byte)r.U8(), health = (byte)r.U8(), seat = (byte)r.U8() };
        }
    }

    public class Mate : MonoBehaviour, ISense
    {
        public int seat; public string mateName; public ulong clientId;
        public readonly Interp<MatePose> poses = new Interp<MatePose>();
        public MatePose cur;
        public float lastHeard;
        Vector3 eye, look = Vector3.forward, worldVel;
        bool inside;

        public static readonly string[] Stations = { "helm", "telegraph", "power", "sonar", "pumps" };
        public static readonly Color[] SuitColours =
        {
            new Color(0.85f, 0.72f, 0.5f),    // ochre canvas
            new Color(0.48f, 0.6f, 0.78f),    // slate blue
            new Color(0.58f, 0.66f, 0.42f),   // olive
            new Color(0.82f, 0.45f, 0.36f),   // rust red
        };

        public Vector3 Eye => eye;
        public bool Inside => inside;
        public Vector3 Velocity => worldVel;
        public bool Lamp => (cur.flags & MatePose.Lamp) != 0 && cur.mode == 0;
        public Vector3 Look => look;
        public bool OnRaft => cur.mode == 2;
        public bool InWorld => poses.Any && cur.mode != 4;
        public float Health => cur.health;

        // the body's parts and their joints
        Transform root, torso, helmet, uarmL, uarmR, farmL, farmR, thighL, thighR, shinL, shinR, finL, finR;
        float phase;          // the gait's or the kick's phase
        Material suit;

        public static Mate Make(int seat, string name)
        {
            var go = new GameObject("Mate " + name);
            var m = go.AddComponent<Mate>(); m.seat = seat; m.mateName = name;
            m.cur.mode = 4;
            m.BuildBody();
            return m;
        }

        void BuildBody()
        {
            suit = new Material(Shader.Find("Deep/Lit")) { name = "diver suit " + seat };
            suit.SetFloat("_UseVC", 1); suit.SetFloat("_VCAlbedo", 1); suit.SetFloat("_Roughness", 0.55f); suit.SetFloat("_Metallic", 0.35f); suit.SetFloat("_Cull", 0);
            suit.SetColor("_Tint", SuitColours[Mathf.Clamp(seat, 0, 3)]);
            root = new GameObject("body").transform; root.SetParent(transform, false);
            torso = Part(root, "diver_torso", Vector3.zero);
            helmet = Part(torso, "diver_helmet", new Vector3(0, 0.58f, 0));
            uarmL = Part(torso, "diver_uarm", new Vector3(-0.25f, 0.5f, 0)); uarmR = Part(torso, "diver_uarm", new Vector3(0.25f, 0.5f, 0));
            farmL = Part(uarmL, "diver_farm", new Vector3(0, -0.3f, 0)); farmR = Part(uarmR, "diver_farm", new Vector3(0, -0.3f, 0));
            thighL = Part(root, "diver_thigh", new Vector3(-0.1f, -0.02f, 0)); thighR = Part(root, "diver_thigh", new Vector3(0.1f, -0.02f, 0));
            shinL = Part(thighL, "diver_shin", new Vector3(0, -0.42f, 0)); shinR = Part(thighR, "diver_shin", new Vector3(0, -0.42f, 0));
            finL = Part(shinL, "diver_fin", new Vector3(0, -0.42f, 0)); finR = Part(shinR, "diver_fin", new Vector3(0, -0.42f, 0));
            root.gameObject.SetActive(false);
        }

        Transform Part(Transform parent, string mesh, Vector3 at)
        {
            var g = new GameObject(mesh); g.transform.SetParent(parent, false); g.transform.localPosition = at;
            var m = ModelLibrary.Get("Vehicles/diver", mesh);
            if (m)
            {
                g.AddComponent<MeshFilter>().sharedMesh = m;
                var r = g.AddComponent<MeshRenderer>(); r.sharedMaterial = suit;
            }
            return g.transform;
        }

        public void Heard(float t, MatePose p) { poses.Add(t, p); lastHeard = Time.time; }

        // the pose now: between the two samples either side of a tenth of a second ago
        void Update()
        {
            var ship = Nautilus.I;
            if (!poses.At(Net.Now - 0.12f, out var a, out var b, out float k)) return;
            var p = b;
            if (a.mode == b.mode)
            {
                p.pos = Vector3.Lerp(a.pos, b.pos, k);
                p.yaw = Mathf.LerpAngle(a.yaw, b.yaw, k); p.pitch = Mathf.Lerp(a.pitch, b.pitch, k);
                p.vel = Vector3.Lerp(a.vel, b.vel, k);
            }
            cur = p;
            bool aboard = p.mode == 1 && ship;
            inside = aboard || p.mode == 3;
            Vector3 body; Quaternion rot;
            if (aboard)
            {
                body = ship.ToWorld(Nautilus.ProxyOrigin + p.pos);
                rot = ship.RotToWorld(Quaternion.Euler(0, p.yaw, 0));
                worldVel = Vector3.zero;
            }
            else
            {
                body = p.pos; rot = Quaternion.Euler(0, p.yaw, 0); worldVel = p.vel;
            }
            var lookRot = aboard ? ship.RotToWorld(Quaternion.Euler(p.pitch, p.yaw, 0)) : Quaternion.Euler(p.pitch, p.yaw, 0);
            look = lookRot * Vector3.forward;
            eye = body + rot * Vector3.up * Diver.EyeHeight;
            Pose(p, body, rot, lookRot, Time.deltaTime);
        }

        // ---- the body's pose ------------------------------------------------------------------------------------
        static Quaternion X(float deg) => Quaternion.Euler(deg, 0, 0);

        void Pose(MatePose p, Vector3 body, Quaternion rot, Quaternion lookRot, float dt)
        {
            bool show = p.mode != 3 && p.mode != 4;
            if (root.gameObject.activeSelf != show) root.gameObject.SetActive(show);
            if (!show) return;
            float speed = p.vel.magnitude;
            float armL = 10, armR = 10, foreL = -15, foreR = -15, thL = 0, thR = 0, knL = 0, knR = 0, spread = 0;
            float headPitch = Mathf.Clamp(Mathf.DeltaAngle(0, p.pitch), -50, 50);
            Vector3 hip = body + rot * new Vector3(0, 0.04f, 0);
            Quaternion bodyRot = rot;
            bool fins = p.mode == 0;
            switch (p.mode)
            {
                case 0:   // in the sea: stretched out along the way they swim, or upright treading water
                {
                    float swim = Mathf.Clamp01((speed - 0.3f) / 1.5f);
                    phase += dt * Mathf.Lerp(3f, 7.5f, swim) * ((p.flags & MatePose.Sprint) != 0 ? 1.4f : 1f);
                    var dir = speed > 0.3f ? p.vel / speed : lookRot * Vector3.forward;
                    var down = Vector3.ProjectOnPlane(Vector3.down, dir); if (down.sqrMagnitude < 0.01f) down = rot * Vector3.back;
                    var flat = Quaternion.LookRotation(down.normalized, dir);
                    bodyRot = Quaternion.Slerp(rot * X(12f), flat, swim);
                    hip = body;
                    float kick = Mathf.Sin(phase) * Mathf.Lerp(18f, 26f, swim);
                    thL = kick; thR = -kick; knL = 18 + Mathf.Max(0, Mathf.Sin(phase + 1f)) * 22; knR = 18 + Mathf.Max(0, -Mathf.Sin(phase + 1f)) * 22;
                    armL = Mathf.Lerp(-25f + Mathf.Sin(phase * 0.4f) * 15f, 8f, swim); armR = Mathf.Lerp(-25f - Mathf.Sin(phase * 0.4f) * 15f, 8f, swim);
                    foreL = foreR = Mathf.Lerp(-35f, -8f, swim);
                    spread = Mathf.Lerp(18f, 6f, swim);
                    headPitch = Mathf.Lerp(headPitch, -55f, swim);
                    break;
                }
                case 1:   // aboard: walking, climbing, or at a station
                {
                    hip = body + rot * new Vector3(0, 0.04f, 0);
                    if ((p.flags & MatePose.Climb) != 0)
                    {
                        phase += dt * 4f * Mathf.Clamp(Mathf.Abs(p.vel.y), 0.2f, 2f);
                        float c = Mathf.Sin(phase);
                        armL = -150 + c * 20; armR = -150 - c * 20; foreL = foreR = -30;
                        thL = -40 + c * 25; thR = -40 - c * 25; knL = 60 - c * 20; knR = 60 + c * 20;
                        break;
                    }
                    float pace = new Vector2(p.vel.x, p.vel.z).magnitude;
                    phase += dt * pace * 3.4f;
                    float sw = Mathf.Sin(phase) * Mathf.Clamp01(pace / 2.4f);
                    thL = -sw * 30; thR = sw * 30; knL = Mathf.Max(0, Mathf.Sin(phase + 1.2f)) * 40 * Mathf.Clamp01(pace); knR = Mathf.Max(0, -Mathf.Sin(phase + 1.2f)) * 40 * Mathf.Clamp01(pace);
                    armL = sw * 25; armR = -sw * 25; foreL = foreR = -20;
                    if (p.station != 255)
                    {
                        // hands on the wheel, the handle, the switches, the headphones, the pump bars
                        string st = p.station < Stations.Length ? Stations[p.station] : "";
                        if (st == "pumps") { phase += dt * 3f; float pm = Mathf.Sin(phase); armL = armR = -70 + pm * 25; foreL = foreR = -40; }
                        else if (st == "sonar") { armL = armR = -150; foreL = foreR = -110; }
                        else { armL = armR = -55; foreL = foreR = -35; }
                    }
                    break;
                }
                case 2:   // in the raft: seated, rowing
                {
                    hip = body + rot * new Vector3(0, -0.45f, 0);
                    thL = thR = -85; knL = knR = 85;
                    bool rowing = (p.flags & MatePose.Row) != 0;
                    if (rowing) phase += dt * Mathf.PI * 2 / 1.1f;
                    float st = rowing ? Mathf.Sin(phase) : 0;
                    armL = armR = -60 + st * 30; foreL = foreR = -30 - st * 20;
                    bodyRot = rot * X(st * 10f);
                    break;
                }
            }
            root.SetPositionAndRotation(hip, bodyRot);
            helmet.localRotation = X(headPitch * 0.6f);
            uarmL.localRotation = X(armL) * Quaternion.Euler(0, 0, -spread - 6); uarmR.localRotation = X(armR) * Quaternion.Euler(0, 0, spread + 6);
            farmL.localRotation = X(foreL); farmR.localRotation = X(foreR);
            thighL.localRotation = X(thL) * Quaternion.Euler(0, 0, -spread * 0.4f); thighR.localRotation = X(thR) * Quaternion.Euler(0, 0, spread * 0.4f);
            shinL.localRotation = X(knL); shinR.localRotation = X(knR);
            // fins on in the sea, pointed along the leg; boots flat aboard
            if (finL.gameObject.activeSelf != fins) { finL.gameObject.SetActive(fins); finR.gameObject.SetActive(fins); }
            finL.localRotation = X(70); finR.localRotation = X(70);
        }

        // their helmet lamp, as a light for the shaders (Nautilus.cs gathers these)
        public bool LampLight(out Vector3 at, out Vector3 dir)
        {
            at = eye + look * 0.3f + Vector3.up * 0.1f; dir = look;
            return (cur.flags & MatePose.Lamp) != 0 && InWorld && cur.mode != 1 && cur.mode != 3;
        }

        void OnDestroy() { if (suit) Destroy(suit); }
    }
}
