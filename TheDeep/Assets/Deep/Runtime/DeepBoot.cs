// The one object in the boot scene. It reads the command line (Depth's arcade launches The Deep with the crew's
// roles and the host's address; the screenshot harness passes -shot), then builds the world at runtime: the seabed,
// the flora, the ocean surface, the underwater look, the clock and the diver.
using UnityEngine;

namespace Deep
{
    public class DeepBoot : MonoBehaviour
    {
        public static DeepBoot I;
        public Seabed seabed;
        public Diver diver;
        public Clock clock;
        public UnderwaterLook look;
        public Flora flora;
        public Nautilus ship;
        public Life life;

        // the derelict Nautilus lies on the seabed a short swim from the start: in 16-30 m of water, on the flattest
        // ground found in a ring round the spawn, her keel settled into the sand with a slight list
        static Nautilus PlaceNautilus(Seabed bed, Vector3 spawn)
        {
            Vector3 best = spawn + new Vector3(60, 0, 0); float bestScore = float.MaxValue, bestYaw = 0;
            for (int k = 0; k < 192; k++)
            {
                float a = k * 0.2618f, r = 50 + (k % 8) * 24;
                var p = spawn + new Vector3(Mathf.Cos(a) * r, 0, Mathf.Sin(a) * r);
                float yaw = (k * 37) % 180;
                var f = new Vector3(Mathf.Sin(yaw * Mathf.Deg2Rad), 0, Mathf.Cos(yaw * Mathf.Deg2Rad));
                float h0 = bed.SampleY(p.x, p.z), hb = bed.SampleY(p.x + f.x * 30, p.z + f.z * 30), hs = bed.SampleY(p.x - f.x * 30, p.z - f.z * 30);
                float rr = bed.SampleY(p.x + f.z * 6, p.z - f.x * 6), rl = bed.SampleY(p.x - f.z * 6, p.z + f.x * 6);
                if (-h0 < 16 || -h0 > 30) continue;
                float score = Mathf.Abs(hb - hs) + Mathf.Abs(rr - rl) + Mathf.Abs(h0 - (hb + hs) / 2) * 2 + Mathf.Abs(r - 60) * 0.01f;
                if (score < bestScore) { bestScore = score; best = new Vector3(p.x, h0, p.z); bestYaw = yaw; }
            }
            var fw = new Vector3(Mathf.Sin(bestYaw * Mathf.Deg2Rad), 0, Mathf.Cos(bestYaw * Mathf.Deg2Rad));
            float bow = bed.SampleY(best.x + fw.x * 30, best.z + fw.z * 30), stern = bed.SampleY(best.x - fw.x * 30, best.z - fw.z * 30);
            float slope = (bow - stern) / 60f, pitch = -Mathf.Atan(slope) * Mathf.Rad2Deg;
            // her keel line clears the highest sand under her (she settles 0.5 m into it at most)
            var side = new Vector3(fw.z, 0, -fw.x);
            float c = float.MinValue;
            for (float s = -32; s <= 40; s += 2)
                for (float q = -2.5f; q <= 2.5f; q += 1.25f)
                {
                    var p = best + fw * s + side * q;
                    float taper = Mathf.Lerp(1f, 0.55f, Mathf.Clamp01(((s > 4 ? s - 8 : s) * Mathf.Sign(s) - 22f) / 12f));
                    c = Mathf.Max(c, bed.SampleY(p.x, p.z) - slope * s + Nautilus.Radius * taper - 0.5f);
                }
            var n = Nautilus.Build(new Vector3(best.x, c, best.z), bestYaw, pitch, 4f);
            I.flora.Clear(n.transform, new Vector3(12f, 14f, 42f));
            return n;
        }

        void Awake()
        {
            I = this;
            Application.targetFrameRate = 60;
            QualitySettings.vSyncCount = 0;
            Args.Parse();

            clock = gameObject.AddComponent<Clock>();
            clock.hour = Args.Hour >= 0 ? Args.Hour : 9.5f;

            seabed = new GameObject("Seabed").AddComponent<Seabed>();
            seabed.Build(Args.Seed);
            flora = new GameObject("Flora").AddComponent<Flora>();
            flora.Build(seabed, Args.Seed);
            new GameObject("OceanSurface").AddComponent<OceanSurface>();

            look = new GameObject("UnderwaterLook").AddComponent<UnderwaterLook>();
            diver = Diver.Spawn(seabed.SpawnPoint());
            ship = PlaceNautilus(seabed, diver.transform.position);
            diver.ship = ship;
            life = Life.Build(seabed, clock, diver, ship, Args.Seed);
            Hands.Attach(diver);
            Survival.Attach(diver);
            CraftUI.Attach(diver);
            look.Bind(diver.cam, clock);
            gameObject.AddComponent<Hud>().Bind(diver, clock);
            if (!string.IsNullOrEmpty(Args.Shot)) gameObject.AddComponent<Shots>().Run(Args.Shot, Args.ShotDir);
            var tm = seabed.terrain.materialTemplate;
            Debug.Log($"DEEP WORLD: terrain {seabed.terrain.terrainData.bounds} at {seabed.terrain.transform.position} mat {(tm ? tm.shader.name + " supported " + tm.shader.isSupported : "none")}; flora {flora.Total}; spawn {diver.transform.position}; H(300,768)={seabed.H(300, 768):0.0} sample={seabed.SampleY(300, 768):0.0}");
        }
    }
}
