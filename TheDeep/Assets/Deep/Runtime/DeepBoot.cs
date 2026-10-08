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
        public Weather weather;
        public Caverns caverns;

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

        bool waiting; GameObject waitCam;

        void Awake()
        {
            I = this;
            Application.targetFrameRate = 60;
            QualitySettings.vSyncCount = 0;
            Application.runInBackground = true;      // (the crew keep sailing while this window isn't in front)
            Args.Parse();
            Net.Begin();
            // a crewmate's PC waits for the host's welcome: the world is built from the host's seed
            if (Net.IsGuest)
            {
                waiting = true;
                waitCam = new GameObject("Waiting");
                var c = waitCam.AddComponent<Camera>(); c.clearFlags = CameraClearFlags.SolidColor; c.backgroundColor = new Color(0.01f, 0.04f, 0.06f);
                return;
            }
            // a campaign to continue: its seed builds the world (the host's or a solo diver's; a crewmate's comes from the host)
            var save = Campaign.Peek();
            BuildWorld(save != null ? save.seed : Args.Seed, save);
        }

        void Update()
        {
            if (!waiting || Net.I == null || Net.I.welcome == null) return;
            waiting = false;
            Destroy(waitCam);
            BuildWorld(Net.I.welcomeSeed, null);
            Net.I.ApplyWelcome();
            if (Args.NetTest) Net.I.StartTest();
        }

        void BuildWorld(int seed, SaveFile save)
        {
            Args.Seed = seed;
            clock = gameObject.AddComponent<Clock>();
            clock.hour = Args.Hour >= 0 ? Args.Hour : 9.5f;

            seabed = new GameObject("Seabed").AddComponent<Seabed>();
            seabed.Build(Args.Seed);
            flora = new GameObject("Flora").AddComponent<Flora>();
            flora.Build(seabed, Args.Seed);
            caverns = Caverns.Build(seabed, Args.Seed);
            flora.ClearWhere(p => caverns.InCave(p + Vector3.down * 0.6f));   // (nothing grows over the caves' mouths)
            new GameObject("OceanSurface").AddComponent<OceanSurface>();

            look = new GameObject("UnderwaterLook").AddComponent<UnderwaterLook>();
            diver = Diver.Spawn(seabed.SpawnPoint());
            ship = PlaceNautilus(seabed, diver.transform.position);
            diver.ship = ship;
            life = Life.Build(seabed, clock, diver, ship, Args.Seed);
            Deposits.Build(seabed, flora, diver.transform.position, Args.Seed);
            Deposits.I?.SerpentScales(seabed, Args.Seed);
            caverns.Plant(flora, Args.Seed); caverns.BuildCity(flora, Args.Seed); caverns.Eggs(Args.Seed);
            Hands.Attach(diver);
            Survival.Attach(diver);
            CraftUI.Attach(diver);
            Decor.Attach(ship, diver);
            Drops.Attach(diver, ship);
            Campaign.Attach(save);
            Perf.Attach();
            Sfx.Attach(); Soundscape.Attach(diver, ship); Score.Attach(diver);
            if (Args.AudioTest) { int bad = Bank.Test(); Application.Quit(bad == 0 ? 0 : 1); }
            weather = Weather.Make();
            weather.mirror = Net.IsGuest;
            // the opening: the night raft, the storm, boarding her (not in the screenshot harness unless asked; not in a
            // campaign that's past it)
            bool past = save != null && save.openingDone;
            if (!Args.SkipOpening && !past && string.IsNullOrEmpty(Args.Shot) && !Args.SaveTest) Opening.Begin(this);
            if (save != null) Campaign.I.Apply(save);
            if (Args.SaveTest) Campaign.I.SelfTest();
            if (Net.IsHost) { Net.I.WorldReady(); if (Args.NetTest) Net.I.StartTest(); }
            look.Bind(diver.cam, clock);
            gameObject.AddComponent<Hud>().Bind(diver, clock);
            if (!string.IsNullOrEmpty(Args.Shot)) gameObject.AddComponent<Shots>().Run(Args.Shot, Args.ShotDir);
            var tm = seabed.terrain.materialTemplate;
            Debug.Log($"DEEP WORLD: terrain {seabed.terrain.terrainData.bounds} at {seabed.terrain.transform.position} mat {(tm ? tm.shader.name + " supported " + tm.shader.isSupported : "none")}; flora {flora.Total}; spawn {diver.transform.position}; H(300,768)={seabed.H(300, 768):0.0} sample={seabed.SampleY(300, 768):0.0}");
        }
    }
}
