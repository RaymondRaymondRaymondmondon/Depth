// The diver: first person. Under water you swim where you look (WASD), rise with Space, sink with Ctrl, sprint with
// Shift; at the surface you tread water with your head out. The tank: 45 s of air (the doc's survival meters come in
// stage 4), drained while your head is under, refilled at the surface.
using UnityEngine;

namespace Deep
{
    public class Diver : MonoBehaviour
    {
        public Camera cam;
        public CharacterController cc;
        public float yaw, pitch;
        public Vector3 vel;
        public float oxygen = 45f, oxygenMax = 45f;
        public bool inputEnabled = true;
        public const float Swim = 3.6f, Sprint = 5.8f, EyeHeight = 0.7f;

        public static Diver Spawn(Vector3 at)
        {
            var go = new GameObject("Diver");
            go.transform.position = at;
            var d = go.AddComponent<Diver>();
            d.cc = go.AddComponent<CharacterController>();
            d.cc.height = 1.6f; d.cc.radius = 0.35f; d.cc.center = Vector3.zero; d.cc.slopeLimit = 60; d.cc.stepOffset = 0.3f;
            var cg = new GameObject("Eye"); cg.transform.SetParent(go.transform, false); cg.transform.localPosition = new Vector3(0, EyeHeight, 0);
            d.cam = cg.AddComponent<Camera>(); d.cam.tag = "MainCamera"; d.cam.nearClipPlane = 0.05f; d.cam.fieldOfView = 72;
            cg.AddComponent<AudioListener>();
            d.yaw = 90f;
            return d;
        }

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
            }
            transform.rotation = Quaternion.Euler(0, yaw, 0);
            cam.transform.localRotation = Quaternion.Euler(pitch, 0, 0);

            var p = transform.position;
            float surf = Waves.Height(p.x, p.z, Time.time);
            float eyeY = p.y + EyeHeight;
            bool atSurface = eyeY > surf - 0.15f;

            Vector3 wish = Vector3.zero;
            if (inputEnabled)
            {
                var look = cam.transform.forward; var right = cam.transform.right;
                wish += look * ((Input.GetKey(KeyCode.W) ? 1 : 0) - (Input.GetKey(KeyCode.S) ? 1 : 0));
                wish += right * ((Input.GetKey(KeyCode.D) ? 1 : 0) - (Input.GetKey(KeyCode.A) ? 1 : 0));
                wish += Vector3.up * ((Input.GetKey(KeyCode.Space) ? 1 : 0) - (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.C) ? 1 : 0));
            }
            if (wish.sqrMagnitude > 1) wish.Normalize();
            float speed = Input.GetKey(KeyCode.LeftShift) && inputEnabled ? Sprint : Swim;
            var target = wish * speed;
            // water: quick to start, slow to stop (drag), a little buoyancy toward neutral
            vel = Vector3.Lerp(vel, target, 1 - Mathf.Exp(-dt * (wish.sqrMagnitude > 0 ? 2.6f : 1.4f)));
            if (atSurface)
            {
                // treading water: the head rides the swell; you can't climb out of the sea by swimming up
                float want = surf - EyeHeight + 0.25f;
                if (p.y > want && vel.y > 0) vel.y = 0;
                vel.y += (want - p.y) * 3f * dt;
            }
            cc.Move(vel * dt);

            // the tank
            bool headUnder = transform.position.y + EyeHeight < Waves.Height(p.x, p.z, Time.time) - 0.05f;
            oxygen = headUnder ? Mathf.Max(0, oxygen - dt) : Mathf.Min(oxygenMax, oxygen + dt * 12f);
        }

        public float Depth => Mathf.Max(0, -(transform.position.y + EyeHeight));
        public void Place(Vector3 eye, float yawDeg, float pitchDeg)
        {
            cc.enabled = false; transform.position = eye - Vector3.up * EyeHeight; cc.enabled = true;
            yaw = yawDeg; pitch = pitchDeg; vel = Vector3.zero;
            transform.rotation = Quaternion.Euler(0, yaw, 0); cam.transform.localRotation = Quaternion.Euler(pitch, 0, 0);
        }
    }
}
