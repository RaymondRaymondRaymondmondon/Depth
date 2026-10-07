// Currents and scent (the design doc, "Scent on the currents"): a wound spills blood and chemical signatures into the
// water at a concentration (ppm); the cloud drifts downstream along the current, spreads and fades; predators swim up
// the gradient to the source. Over 0.1 ppm a creature notices (Alert); over 1.5 ppm hunters and scavengers frenzy.
// The field is a 2D grid over the map (the cloud is treated as filling the water column it's in), advected
// semi-Lagrangian by Currents.At, diffused and decaying. Headless.
using UnityEngine;

namespace Deep
{
    public static class Currents
    {
        // the water's movement (m/s) at a point and time: a gentle tidal drift that swings round twice a day, bent
        // into slow eddies; the Kelp Labyrinth's channels run stronger
        public static Vector2 At(float x, float z, float depth, float hour)
        {
            float tide = Mathf.Sin(hour / 12.42f * Mathf.PI * 2f);
            var baseDir = new Vector2(0.8f, 0.6f) * (0.12f + 0.1f * tide);
            float e1 = Mathf.Sin(x * 0.006f + hour * 0.3f) * Mathf.Cos(z * 0.007f), e2 = Mathf.Cos(x * 0.005f) * Mathf.Sin(z * 0.006f - hour * 0.2f);
            var eddy = new Vector2(e1, e2) * 0.08f;
            float k = depth > 50f && depth < 150f ? 1.6f : 1f;
            return (baseDir + eddy) * k;
        }
    }

    public class Scent
    {
        public const float CellM = 8f;
        public const float Notice = 0.1f, Frenzy = 1.5f;     // ppm (the doc's thresholds)
        public readonly int n;
        float[] c, tmp, vx, vz;
        float flowHour = -99f;
        const float HalfLife = 75f, Diffuse = 0.6f;          // seconds; m^2/s
        public float hour = 10f;

        public Scent(float mapSize) { n = Mathf.CeilToInt(mapSize / CellM); c = new float[n * n]; tmp = new float[n * n]; vx = new float[n * n]; vz = new float[n * n]; }

        // a spill: `ppm` at the cell it lands in (a bleeding fish ~2-4, a wounded diver ~3, a carcass ~6)
        public void Emit(Vector3 p, float ppm)
        {
            int x = Mathf.FloorToInt(p.x / CellM), z = Mathf.FloorToInt(p.z / CellM);
            if (x < 0 || z < 0 || x >= n || z >= n) return;
            c[z * n + x] += ppm;
        }

        public float At(Vector3 p)
        {
            float fx = p.x / CellM - 0.5f, fz = p.z / CellM - 0.5f;
            int x0 = Mathf.FloorToInt(fx), z0 = Mathf.FloorToInt(fz);
            float tx = fx - x0, tz = fz - z0;
            return Mathf.Lerp(Mathf.Lerp(Cell(x0, z0), Cell(x0 + 1, z0), tx), Mathf.Lerp(Cell(x0, z0 + 1), Cell(x0 + 1, z0 + 1), tx), tz);
        }
        float Cell(int x, int z) => x < 0 || z < 0 || x >= n || z >= n ? 0 : c[z * n + x];

        // which way the scent grows stronger (unit, on the horizontal), and how strong it is here
        public Vector3 Gradient(Vector3 p, out float here)
        {
            here = At(p);
            float h = CellM;
            float gx = At(p + new Vector3(h, 0, 0)) - At(p - new Vector3(h, 0, 0));
            float gz = At(p + new Vector3(0, 0, h)) - At(p - new Vector3(0, 0, h));
            var g = new Vector3(gx, 0, gz);
            return g.sqrMagnitude > 1e-12f ? g.normalized : Vector3.zero;
        }

        public float Total() { float t = 0; foreach (var v in c) t += v; return t; }

        // one step: carried by the current (sampling upstream), spread to neighbours, faded. Only cells holding scent
        // (or beside them) cost anything that matters; the whole grid is small (192 x 192).
        public void Step(float dt)
        {
            float fade = Mathf.Pow(0.5f, dt / HalfLife);
            float dif = Mathf.Clamp01(Diffuse * dt / (CellM * CellM)) * 4f;
            dif = Mathf.Min(dif, 0.24f * 4f);
            if (Mathf.Abs(hour - flowHour) > 0.05f)     // the current field changes slowly: refresh it now and then
            {
                flowHour = hour;
                for (int z = 0; z < n; z++)
                    for (int x = 0; x < n; x++)
                    {
                        var v = Currents.At((x + 0.5f) * CellM, (z + 0.5f) * CellM, 30f, hour);
                        vx[z * n + x] = v.x; vz[z * n + x] = v.y;
                    }
            }
            for (int z = 0; z < n; z++)
                for (int x = 0; x < n; x++)
                {
                    float sx = x - vx[z * n + x] * dt / CellM, sz = z - vz[z * n + x] * dt / CellM;
                    int x0 = Mathf.FloorToInt(sx), z0 = Mathf.FloorToInt(sz);
                    float tx = sx - x0, tz = sz - z0;
                    float a = Mathf.Lerp(Mathf.Lerp(Cell(x0, z0), Cell(x0 + 1, z0), tx), Mathf.Lerp(Cell(x0, z0 + 1), Cell(x0 + 1, z0 + 1), tx), tz);
                    tmp[z * n + x] = a;
                }
            for (int z = 0; z < n; z++)
                for (int x = 0; x < n; x++)
                {
                    float m = tmp[z * n + x];
                    float nb = Tmp(x - 1, z) + Tmp(x + 1, z) + Tmp(x, z - 1) + Tmp(x, z + 1);
                    c[z * n + x] = (m + (nb * 0.25f - m) * dif) * fade;
                    if (c[z * n + x] < 1e-5f) c[z * n + x] = 0;
                }
        }
        float Tmp(int x, int z) => x < 0 || z < 0 || x >= n || z >= n ? 0 : tmp[z * n + x];
    }
}
