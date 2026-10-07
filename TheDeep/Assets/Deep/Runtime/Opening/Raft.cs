// The inflatable life raft (stage 5; the design doc: the starting craft, up to four crew, rowed; "flipped by storms or
// surface-feeding leviathans; torn by sharp crystal and coral"; later stowed on the Nautilus's deck for trips to the
// island). It rides the swell (its height and tilt from Waves), drifts with the wind, and is rowed:
//   W/S pull the oars (stroke by stroke), A/D turn, E leave it (into the water beside it).
// A steep storm sea can flip it; so can a leviathan rising under it. Swim to it and E to right it again.
using UnityEngine;

namespace Deep
{
    public class Raft : MonoBehaviour
    {
        public static Raft I;
        public float heading;                 // degrees
        public Vector3 vel;
        public bool flipped;
        Transform model, oarL, oarR;
        public float stroke;                  // 0..1 through an oar stroke while rowing
        float pitchV, rollV;
        public bool mirror;                   // a crewmate's PC: the host moves it (Net.cs feeds `netPos`...)
        public Vector3 netPos, netVel; public float netHeading;
        // each seat's oars: the crew row together (the host adds every seat's pull)
        public readonly float[] row = new float[4], turn = new float[4], inT = new float[4];
        public int occupied;                  // which seats have someone in them (a bit each; the host works it out)

        public static Raft Spawn(Vector3 at, float heading)
        {
            var go = new GameObject("Raft"); var r = go.AddComponent<Raft>(); I = r;
            r.heading = heading;
            go.transform.position = at;
            r.model = new GameObject("model").transform; r.model.SetParent(go.transform, false);
            var mat = new Material(Shader.Find("Deep/Lit")); mat.SetFloat("_UseVC", 1); mat.SetFloat("_VCAlbedo", 1); mat.SetFloat("_Roughness", 0.6f); mat.SetFloat("_Cull", 0);
            var body = ModelLibrary.Get("Vehicles/raft", "raft"); var oar = ModelLibrary.Get("Vehicles/raft", "raft_oar");
            if (body) Part(r.model, "body", body, mat);
            if (oar)
            {
                r.oarL = Part(r.model, "oar L", oar, mat).transform; r.oarR = Part(r.model, "oar R", oar, mat).transform;
            }
            return r;
        }

        static GameObject Part(Transform parent, string name, Mesh m, Material mat)
        {
            var g = new GameObject(name); g.transform.SetParent(parent, false);
            g.AddComponent<MeshFilter>().sharedMesh = m;
            var mr = g.AddComponent<MeshRenderer>(); mr.sharedMaterial = mat;
            return g;
        }

        public Vector3 Seat(int i) => transform.TransformPoint(new Vector3(i % 2 == 0 ? -0.35f : 0.35f, 0.75f, i < 2 ? 0.2f : -0.45f));

        void Update()
        {
            float dt = Mathf.Min(Time.deltaTime, 0.05f);
            var p = transform.position;
            if (mirror)
            {
                // where the host says it is, smoothly
                var want = netPos;
                if ((want - p).sqrMagnitude > 25f) p = want; else p = Vector3.Lerp(p, want, 1 - Mathf.Exp(-dt * 6f));
                heading = Mathf.LerpAngle(heading, netHeading, 1 - Mathf.Exp(-dt * 6f));
                vel = netVel;
                var d0 = DeepBoot.I ? DeepBoot.I.diver : null;
                if (flipped && d0 != null && d0.onRaft == this) { d0.LeaveRaft(); d0.Toast("The raft goes over! You're in the water."); }
            }
            else
            {
                Pull(dt);
                // the wind and the water's drag
                var wind = Weather.Wind;
                vel += new Vector3(wind.x, 0, wind.y) * 0.04f * dt;
                vel *= Mathf.Exp(-dt * 0.45f);
                p += vel * dt;
            }
            float t = Waves.T;
            p.y = Waves.Height(p.x, p.z, t) + (flipped ? 0.05f : 0.08f);
            // tilt with the swell
            var f = Quaternion.Euler(0, heading, 0) * Vector3.forward; var rgt = Quaternion.Euler(0, heading, 0) * Vector3.right;
            float hf = Waves.Height(p.x + f.x * 1.4f, p.z + f.z * 1.4f, t), hb = Waves.Height(p.x - f.x * 1.4f, p.z - f.z * 1.4f, t);
            float hr = Waves.Height(p.x + rgt.x * 0.9f, p.z + rgt.z * 0.9f, t), hl = Waves.Height(p.x - rgt.x * 0.9f, p.z - rgt.z * 0.9f, t);
            float tp = -Mathf.Atan2(hf - hb, 2.8f) * Mathf.Rad2Deg, tr = Mathf.Atan2(hr - hl, 1.8f) * Mathf.Rad2Deg;
            pitchV = Mathf.Lerp(pitchV, tp, 1 - Mathf.Exp(-dt * 5f)); rollV = Mathf.Lerp(rollV, tr, 1 - Mathf.Exp(-dt * 5f));
            transform.SetPositionAndRotation(p, Quaternion.Euler(pitchV, heading, rollV + (flipped ? 180f : 0f)));
            // a storm sea can flip it: steep water and a wild chance
            if (!mirror && !flipped && Weather.Storm > 0.7f && Mathf.Abs(tr) > 24f && Random.value < dt * 0.25f) Flip("A breaking wave flips the raft!");
            // the oars sweep while rowing, rest otherwise
            if (oarL && oarR)
            {
                float sw = Mathf.Sin(stroke * Mathf.PI * 2f);
                oarL.localPosition = new Vector3(-0.9f, 0.35f, 0.1f); oarR.localPosition = new Vector3(0.9f, 0.35f, 0.1f);
                oarL.localRotation = Quaternion.Euler(0, 180f + sw * 35f, -12f + Mathf.Cos(stroke * Mathf.PI * 2f) * 8f);
                oarR.localRotation = Quaternion.Euler(0, -sw * 35f, 12f - Mathf.Cos(stroke * Mathf.PI * 2f) * 8f);
            }
        }

        public void Flip(string why)
        {
            if (flipped) return;
            flipped = true; vel = Vector3.zero;
            var d = DeepBoot.I ? DeepBoot.I.diver : null;
            if (d != null && d.onRaft == this) { d.LeaveRaft(); d.Toast(why + " You're in the water."); }
        }
        public void Right() { flipped = false; if (mirror) Net.Cmd(Net.C_RIGHTRAFT); }

        // rowed by the diver in it: this seat's oars (on a crewmate's PC they go to the host)
        public void Drive(Diver d, float dt)
        {
            if (flipped) return;
            float r = d.inputEnabled && !d.uiOpen ? (Input.GetKey(KeyCode.W) ? 1 : 0) - (Input.GetKey(KeyCode.S) ? 1 : 0) : 0;
            float tn = d.inputEnabled && !d.uiOpen ? (Input.GetKey(KeyCode.D) ? 1 : 0) - (Input.GetKey(KeyCode.A) ? 1 : 0) : 0;
            Oars(d.raftSeatNow, r, tn);
            if (mirror) Net.RaftOars(d.raftSeatNow, r, tn);
        }
        public void Oars(int seat, float r, float tn)
        {
            if (seat < 0 || seat > 3) return;
            row[seat] = r; turn[seat] = tn; inT[seat] = Time.time;
        }

        // every seat's pull: two rowers drive it harder (but not twice as hard); the oars sweep while anyone rows
        void Pull(float dt)
        {
            float r = 0, tn = 0; int n = 0;
            for (int i = 0; i < 4; i++)
                if (Time.time - inT[i] < 0.35f && (row[i] != 0 || turn[i] != 0)) { r += row[i]; tn += turn[i]; n++; }
            if (n == 0) return;
            r = Mathf.Clamp(r, -1.6f, 1.6f); tn = Mathf.Clamp(tn, -1.4f, 1.4f);
            float before = stroke;
            stroke = Mathf.Repeat(stroke + dt / 1.1f, 1f);
            // the pull is in the first half of each stroke
            if (stroke < 0.5f)
            {
                var f = Quaternion.Euler(0, heading, 0) * Vector3.forward;
                vel += f * r * 2.1f * dt;
                heading += tn * 32f * dt * (r == 0 ? 1.4f : 1f);
            }
            if (before > stroke && Life.I != null) Life.I.sound.Emit(transform.position, 34f, Band.Mid, 0.3f, "oars");
        }
    }
}
