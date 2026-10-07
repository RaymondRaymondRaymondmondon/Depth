// Sound and the Wake (the design doc, "Sound and the Wake system").
// Sound spreads in all directions and fades with distance and with the biome's absorption:
//     P(d) = P0 - 20 log10(d) - a d          (P0 the source level in dB at 1 m, a in dB per metre)
// It comes in four bands, and each creature hears only its own (low: lateral lines of the leviathans and open-water
// giants, 10-50 Hz; mid: hunters, 100-800 Hz; high: echolocating and light-shy fauna, 1.5-6 kHz; ultrasonic:
// echolocators and the active sonar ping). Everything loud also writes Wake Heat into the water column where it is
// heard: a hidden, slowly cooling value that draws bigger threats as it rises (tier 1 30-50 dB curious grazers and
// scavengers, tier 2 50-80 dB mesopredators, tier 3 80+ dB or blood: the biome's apex). Blood raises a zone's Wake
// for good (the floor).
// Headless: no MonoBehaviour; Sea.cs steps it.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public enum Band { Low, Mid, High, Ultra }

    public class Acoustics
    {
        public const float Ambient = 38f;            // the sea's own murmur, dB (a creature notices +15 over it)
        public struct Sound { public Vector3 pos; public float db; public Band band; public float until; public string what; }
        public interface ISource { bool Sounding(out Vector3 pos, out float db, out Band band); }

        readonly List<Sound> sounds = new List<Sound>();
        public readonly List<ISource> sources = new List<ISource>();   // continuous: the Nautilus's engine, swimmers
        public float now;

        // the Wake grid over the map (cells of CellM metres)
        public const float CellM = 32f;
        public readonly int n;
        public readonly float[] heat, floor;
        const float HalfLife = 150f;

        public Acoustics(float mapSize)
        {
            n = Mathf.CeilToInt(mapSize / CellM);
            heat = new float[n * n]; floor = new float[n * n];
        }

        public static float Absorption(float depth) => Biomes.At(depth).absorb;

        // the level heard at distance d from a source of P0 dB (in water whose absorption is a)
        public static float LevelAt(float p0, float d, float a) => p0 - 20f * Mathf.Log10(Mathf.Max(1f, d)) - a * d;

        // how far a sound carries before it sinks under the ambient (for the Wake and for culling)
        public static float Reach(float p0, float a)
        {
            float lo = 1, hi = 6000;
            for (int i = 0; i < 30; i++) { float m = (lo + hi) / 2; if (LevelAt(p0, m, a) > Ambient) lo = m; else hi = m; }
            return lo;
        }

        // on a crewmate's PC the host's sea does the hearing: sounds made here go to it (Net.cs sets this)
        public static System.Action<Vector3, float, Band, float, string> Forward;

        public void Emit(Vector3 pos, float db, Band band, float seconds = 0.5f, string what = null)
        {
            if (Forward != null) { Forward(pos, db, band, seconds, what); return; }
            sounds.Add(new Sound { pos = pos, db = db, band = band, until = now + seconds, what = what });
            AddWake(pos, db, seconds);
        }

        // Wake heat: the louder and the longer, the hotter, over the area where it's heard above the ambient
        void AddWake(Vector3 pos, float db, float seconds)
        {
            if (db < 30f) return;
            float a = Absorption(-pos.y);
            float reach = Mathf.Min(Reach(db, a), 900f);
            float amount = (db - 30f) * Mathf.Clamp(seconds, 0.2f, 5f) * 0.05f;
            Splat(pos, reach, amount, heat);
        }

        void Splat(Vector3 pos, float radius, float amount, float[] grid)
        {
            int r = Mathf.CeilToInt(radius / CellM);
            int cx = Mathf.FloorToInt(pos.x / CellM), cz = Mathf.FloorToInt(pos.z / CellM);
            for (int j = -r; j <= r; j++)
                for (int i = -r; i <= r; i++)
                {
                    int x = cx + i, z = cz + j; if (x < 0 || z < 0 || x >= n || z >= n) continue;
                    float d = new Vector2(i, j).magnitude * CellM;
                    if (d > radius) continue;
                    grid[z * n + x] += amount * (1f - d / (radius + 1f));
                }
        }

        // blood in the water raises the zone's Wake for good (the doc)
        public void BloodSpill(Vector3 pos, float amount)
        {
            Splat(pos, 64f, amount * 4f, heat);
            Splat(pos, 48f, amount * 0.8f, floor);
        }

        public void Step(float dt)
        {
            now += dt;
            sounds.RemoveAll(s => s.until < now);
            float k = Mathf.Pow(0.5f, dt / HalfLife);
            for (int i = 0; i < heat.Length; i++) heat[i] = Mathf.Max(floor[i], heat[i] * k);
            // continuous sources keep writing heat while they sound
            foreach (var src in sources)
                if (src.Sounding(out var p, out var db, out var b) && db >= 30f)
                    Splat(p, Mathf.Min(Reach(db, Absorption(-p.y)), 900f), (db - 30f) * dt * 0.02f, heat);
        }

        public float WakeAt(Vector3 p)
        {
            int x = Mathf.Clamp(Mathf.FloorToInt(p.x / CellM), 0, n - 1), z = Mathf.Clamp(Mathf.FloorToInt(p.z / CellM), 0, n - 1);
            return heat[z * n + x];
        }

        // the Wake tier at a point: 0 calm, 1 curious grazers and scavengers, 2 mesopredators, 3 the apex
        public static int Tier(float wake) => wake < 3f ? 0 : wake < 12f ? 1 : wake < 40f ? 2 : 3;

        // the loudest thing a listener with these bands hears at p, and where it is
        public float Hear(Vector3 p, uint bands, out Vector3 from, out string what)
        {
            float best = Ambient; from = p; what = null;
            float a = Absorption(-p.y);
            foreach (var s in sounds)
            {
                if ((bands & (1u << (int)s.band)) == 0) continue;
                float l = LevelAt(s.db, Vector3.Distance(p, s.pos), a);
                if (l > best) { best = l; from = s.pos; what = s.what; }
            }
            foreach (var src in sources)
            {
                if (!src.Sounding(out var sp, out var db, out var b) || (bands & (1u << (int)b)) == 0) continue;
                float l = LevelAt(db, Vector3.Distance(p, sp), a);
                if (l > best) { best = l; from = sp; what = "engine"; }
            }
            return best;
        }

        public static uint Bands(IEnumerable<string> names)
        {
            uint m = 0;
            if (names != null)
                foreach (var s in names)
                    switch (s) { case "low": m |= 1; break; case "mid": m |= 2; break; case "high": m |= 4; break; case "ultra": m |= 8; break; }
            return m == 0 ? 2u : m;
        }
    }
}
