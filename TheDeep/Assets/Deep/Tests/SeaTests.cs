// Stage 3's checks: the sound law, the Wake's tiers and cooling, scent drifting downstream and fading, and the food
// web holding its balance untouched and cascading when the grazers are over-harvested.
using NUnit.Framework;
using UnityEngine;

namespace Deep.Tests
{
    public class SeaTests
    {
        [Test]
        public void SoundFadesWithDistanceAndAbsorption()
        {
            // 100 dB at 1 m: -40 dB of spreading at 100 m, and the Kelp's absorption takes 3.8 dB more there
            Assert.AreEqual(60f, Acoustics.LevelAt(100f, 100f, 0f), 0.01f);
            Assert.AreEqual(56.2f, Acoustics.LevelAt(100f, 100f, 0.038f), 0.01f);
            Assert.Greater(Acoustics.Reach(110f, 0.005f), Acoustics.Reach(110f, 0.038f), "the kelp swallows sound");
            Assert.Greater(Acoustics.Reach(110f, 0.005f), 1000f, "an engine carries for kilometres in open water");
        }

        [Test]
        public void WakeRisesWithNoiseAndCools()
        {
            var a = new Acoustics(1536f);
            var p = new Vector3(400, -20, 400);
            for (int i = 0; i < 20; i++) { a.Emit(p, 115f, Band.Ultra, 1f); a.Step(1f); }
            float hot = a.WakeAt(p);
            Assert.GreaterOrEqual(Acoustics.Tier(hot), 2, $"twenty sonar pings make the water hot ({hot})");
            for (int i = 0; i < 1200; i++) a.Step(1f);
            Assert.Less(a.WakeAt(p), hot * 0.01f, "and it cools when the noise stops");
            a.BloodSpill(p, 5f);
            for (int i = 0; i < 3000; i++) a.Step(1f);
            Assert.Greater(a.WakeAt(p), 0.5f, "blood leaves the zone's Wake raised for good");
        }

        [Test]
        public void HearingIsByBand()
        {
            var a = new Acoustics(1536f);
            a.Emit(new Vector3(500, -20, 500), 115f, Band.Ultra, 2f, "ping");
            var ear = new Vector3(560, -20, 500);
            Assert.Greater(a.Hear(ear, 1u << (int)Band.Ultra, out _, out _), Acoustics.Ambient + 15f, "an echolocator hears the ping");
            Assert.AreEqual(Acoustics.Ambient, a.Hear(ear, 1u << (int)Band.Low, out _, out _), 0.001f, "a lateral line doesn't");
        }

        [Test]
        public void ScentDriftsDownstreamAndFades()
        {
            var s = new Scent(1536f) { hour = 6f };
            var src = new Vector3(600, -20, 600);
            s.Emit(src, 4f);
            float total0 = s.Total();
            for (int i = 0; i < 300; i++) s.Step(0.2f);    // a minute
            var flow = Currents.At(src.x, src.z, 20, 6f);
            var down = src + new Vector3(flow.x, 0, flow.y).normalized * 12f;
            var up = src - new Vector3(flow.x, 0, flow.y).normalized * 12f;
            Assert.Greater(s.At(down), s.At(up), "the cloud drifts downstream");
            Assert.Less(s.Total(), total0, "and fades");
            var g = s.Gradient(down + Vector3.right * 2, out float here);
            Assert.Greater(here, 0f);
            for (int i = 0; i < 6000; i++) s.Step(0.2f);
            Assert.Less(s.At(src) + s.At(down), Scent.Notice, "twenty minutes later it's gone");
        }

        [Test]
        public void TheWebHoldsItsBalanceUntouched()
        {
            var e = Ecology.FirstBuild();
            for (int d = 0; d < 60 * 48; d++) e.Step(1f / 48f, 0.47f);     // sixty days, steps of half an hour, average daylight
            foreach (var p in e.pools)
                for (int l = 0; l < 5; l++)
                    Assert.AreEqual(1f, p.Abundance(l), 0.05f, $"{p.biome} B{l}");
        }

        [Test]
        public void OverHarvestingStarvesTheHunters()
        {
            var e = Ecology.FirstBuild();
            var p = e.Of("shallows");
            Assert.IsFalse(p.Starving(2));
            e.Harvest("shallows", 1, p.B[1] * 0.7f);       // the crew nets 70% of the grazers
            Assert.IsTrue(p.Starving(2), "mesopredators starve at once");
            Assert.Greater(p.Hunger(2), 0.6f);
            for (int d = 0; d < 40 * 48; d++) e.Step(1f / 48f, 0.47f);
            Assert.Less(p.Abundance(2), 0.97f, "the hunters' numbers fall");
            Assert.Greater(p.Abundance(1), 0.5f, "and the grazers recover");
        }
    }
}
