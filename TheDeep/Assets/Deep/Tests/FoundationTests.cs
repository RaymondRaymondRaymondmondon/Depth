// Stage 1's checks: the surface the CPU uses matches the displaced waves, and the depth bands cover the sea.
using NUnit.Framework;
using UnityEngine;

namespace Deep.Tests
{
    public class FoundationTests
    {
        [Test]
        public void SurfaceHeightMatchesTheDisplacedWaves()
        {
            // a point on the displaced surface must sit at the height Waves.Height reports at its own x, z
            for (int k = 0; k < 200; k++)
            {
                float x = k * 7.3f, z = k * 3.1f - 300, t = k * 0.37f;
                var p = Waves.Displace(x, z, t);
                Assert.AreEqual(p.y, Waves.Height(p.x, p.z, t), 0.03f, $"at {p}");
            }
        }

        [Test]
        public void EveryDepthHasOneBiome()
        {
            for (int i = 1; i < Biomes.All.Length; i++) Assert.AreEqual(Biomes.All[i - 1].bottom, Biomes.All[i].top, "the bands meet");
            Assert.AreEqual("The Sunlit Shallows", Biomes.At(10).name);
            Assert.AreEqual("The Kelp Labyrinth", Biomes.At(80).name);
            Assert.AreEqual("The Void Edge", Biomes.At(2500).name);
        }
    }
}
