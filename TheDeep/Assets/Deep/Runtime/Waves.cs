// The sea surface: four Gerstner waves around y = 0. The ocean shader (Shaders/OceanSurface.shader) evaluates the
// same waves, so the CPU knows exactly where the surface is (breathing, the waterline, buoyancy, the raft).
using UnityEngine;

namespace Deep
{
    public static class Waves
    {
        // per wave: direction x, direction z, wavelength (m), steepness; amplitude = steepness / k
        public static readonly Vector4[] W =
        {
            new Vector4(1.0f, 0.3f, 38f, 0.18f),
            new Vector4(0.6f, -0.8f, 21f, 0.16f),
            new Vector4(-0.4f, 1.0f, 11f, 0.12f),
            new Vector4(0.9f, 0.9f, 5.5f, 0.08f),
        };
        public static float Calm = 1f;
        public static float TimeOffset;                         // a crewmate's PC: the host's clock minus ours (Net.cs)
        public static float T => Time.time + TimeOffset;        // the sea's time, the same on every PC   // 0 glassy .. 1 normal .. 2.5 a storm (the opening's weather)

        // the displaced position of the surface point that started at (x, z)
        public static Vector3 Displace(float x, float z, float t)
        {
            Vector3 p = new Vector3(x, 0, z);
            for (int i = 0; i < W.Length; i++)
            {
                var w = W[i]; var d = new Vector2(w.x, w.y).normalized;
                float k = 2f * Mathf.PI / w.z, c = Mathf.Sqrt(9.81f / k), s = w.w * Calm, a = s / k;
                float f = k * (d.x * x + d.y * z - c * t);
                p.x += d.x * a * Mathf.Cos(f); p.z += d.y * a * Mathf.Cos(f); p.y += a * Mathf.Sin(f);
            }
            return p;
        }

        // the surface height at (x, z): two fixed-point steps undo the sideways displacement
        public static float Height(float x, float z, float t)
        {
            float px = x, pz = z;
            for (int it = 0; it < 3; it++) { var d = Displace(px, pz, t); px -= d.x - x; pz -= d.z - z; }
            return Displace(px, pz, t).y;
        }

        public static void Upload(float t)
        {
            for (int i = 0; i < W.Length; i++) Shader.SetGlobalVector("_DeepWave" + i, new Vector4(W[i].x, W[i].y, W[i].z, W[i].w * Calm));
            Shader.SetGlobalFloat("_DeepTime", t);
        }
    }
}
