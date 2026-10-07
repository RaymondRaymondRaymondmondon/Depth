// God rays: long soft beams hanging from the surface along the sun's refracted direction, scattered round the eye.
// Each is a strip that turns about its own axis to face the camera; the shader fades its ends and ripples it.
using UnityEngine;

namespace Deep
{
    public class Shafts : MonoBehaviour
    {
        const int Count = 12;
        Mesh strip; Material mat;
        readonly Vector3[] spot = new Vector3[Count];
        readonly float[] width = new float[Count], phase = new float[Count];
        readonly Matrix4x4[] mats = new Matrix4x4[Count];
        MaterialPropertyBlock mpb;
        readonly float[] fades = new float[Count];

        void Awake()
        {
            mat = new Material(Resources.Load<Shader>("Shaders/Shaft")) { enableInstancing = true };
            strip = new Mesh { name = "shaft" };
            strip.vertices = new[] { new Vector3(-0.5f, 0, 0), new Vector3(0.5f, 0, 0), new Vector3(-0.5f, -1, 0), new Vector3(0.5f, -1, 0) };
            strip.uv = new[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1), new Vector2(1, 1) };
            strip.triangles = new[] { 0, 2, 1, 1, 2, 3 };
            strip.bounds = new Bounds(new Vector3(0, -0.5f, 0), Vector3.one * 2);
            mpb = new MaterialPropertyBlock();
            for (int i = 0; i < Count; i++) { spot[i] = new Vector3(9999, 0, 0); phase[i] = Random.value * 10; }
        }

        public void Tick(Camera cam, Clock clock, float strength)
        {
            if (strength <= 0.01f) return;
            var c = cam.transform.position;
            // the beam's direction: the sun's ray bent toward vertical by the surface (Snell, n = 1.33)
            var d = clock.SunDirection; var h = new Vector3(d.x, 0, d.z) * (1f / 1.33f);
            var dir = new Vector3(h.x, -Mathf.Sqrt(Mathf.Max(0.05f, 1 - h.sqrMagnitude)), h.z).normalized;
            for (int i = 0; i < Count; i++)
            {
                var flat = new Vector2(spot[i].x - c.x, spot[i].z - c.z);
                if (flat.magnitude > 46f)   // re-seed beams that drifted out of range, ahead of the eye where we can see them
                {
                    var fwd = cam.transform.forward; var a = Random.insideUnitCircle * 40f + new Vector2(fwd.x, fwd.z).normalized * 18f;
                    spot[i] = new Vector3(c.x + a.x, 0, c.z + a.y); width[i] = Random.Range(4f, 10f);
                }
                float len = 55f;
                var axis = dir;
                var toCam = c - spot[i]; toCam -= Vector3.Dot(toCam, axis) * axis;
                var side = Vector3.Cross(axis, toCam.normalized).normalized;
                var fwd2 = Vector3.Cross(side, axis);
                var rot = Quaternion.LookRotation(fwd2, -axis);
                mats[i] = Matrix4x4.TRS(spot[i], rot, new Vector3(width[i], len, 1));
                fades[i] = strength * (0.5f + 0.5f * Mathf.Sin(Time.time * 0.3f + phase[i]));
            }
            mpb.SetFloatArray("_Fade", fades);
            var rp = new RenderParams(mat) { matProps = mpb, shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off, receiveShadows = false, worldBounds = new Bounds(c, Vector3.one * 200) };
            Graphics.RenderMeshInstanced(rp, strip, 0, mats);
        }
    }
}
