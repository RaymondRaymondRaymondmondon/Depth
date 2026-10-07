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
            look.Bind(diver.cam, clock);
            gameObject.AddComponent<Hud>().Bind(diver, clock);
            if (!string.IsNullOrEmpty(Args.Shot)) gameObject.AddComponent<Shots>().Run(Args.Shot, Args.ShotDir);
            var tm = seabed.terrain.materialTemplate;
            Debug.Log($"DEEP WORLD: terrain {seabed.terrain.terrainData.bounds} at {seabed.terrain.transform.position} mat {(tm ? tm.shader.name + " supported " + tm.shader.isSupported : "none")}; flora {flora.Total}; spawn {diver.transform.position}; H(300,768)={seabed.H(300, 768):0.0} sample={seabed.SampleY(300, 768):0.0}");
        }
    }
}
