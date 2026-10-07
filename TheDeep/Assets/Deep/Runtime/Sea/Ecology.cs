// Biomass and trophic cascades (the design doc, "Living ecosystem simulation"): each biome's living mass is split
// across five levels - B0 producers (regrowing from the biome's energy: sunlight in the shallows), B1 grazers, B2
// mesopredators, B3 apex leviathans, B4 decomposers - and energy climbs each level at 12% efficiency.
//   - The pools are calibrated so the starting values are the balance point: every rate constant is solved from them
//     (like the Trawl's lagoon), so a sea nobody touches holds steady and only the crew's harvesting moves it.
//   - Feeding has a saturating (type II) response, half-saturated at the prey's starting biomass, so when grazers are
//     over-harvested the hunters' intake falls, they starve, and (below half their prey's balance) enter STARVING:
//     perception x3.5, no fear, attacking divers and the Nautilus.
//   - Starving apex leviathans leave their territory for shallower biomes (`ApexInvading`).
//   - Ecosystem memory: harvesting adds to the biome's memory, which decays only over days and makes its hunters
//     hungrier and bolder on later dives.
// Time is in game days. Headless.
using System;
using UnityEngine;

namespace Deep
{
    public class Ecology
    {
        public const float Efficiency = 0.12f;        // the doc's eta
        public class Pool
        {
            public string biome;
            public float[] B = new float[5], start = new float[5];
            public float r0, K, m4, memory;
            public float[] a = new float[4], m = new float[4];    // feeding rate of level L on L-1 (L = 1..3), metabolism of L
            public float harvested;                                // tonnes taken by the crew (this campaign)
            public float Abundance(int l) => start[l] > 0 ? B[l] / start[l] : 0;
            // how hungry a level's animals are: 0 fed, 1 starving (their prey at half its balance or less)
            public float Hunger(int l) => l <= 0 ? 0 : Mathf.Clamp01(1.0f - (Abundance(l - 1) - 0.5f) * 2f) * 0.8f + Mathf.Clamp01(memory) * 0.2f;
            public bool Starving(int l) => l >= 1 && l <= 3 && Abundance(l - 1) < 0.5f;
        }

        public readonly Pool[] pools;

        // turnover (days) of each level's mass at the balance: grazers a week, hunters a fortnight, leviathans two months
        static readonly float[] Turnover = { 0, 7f, 14f, 60f };

        public Ecology(params (string biome, float b0, float b1, float b2, float b3, float b4)[] biomes)
        {
            pools = new Pool[biomes.Length];
            for (int i = 0; i < biomes.Length; i++)
            {
                var (name, b0, b1, b2, b3, b4) = biomes[i];
                var p = new Pool { biome = name };
                p.B[0] = b0; p.B[1] = b1; p.B[2] = b2; p.B[3] = b3; p.B[4] = b4;
                Array.Copy(p.B, p.start, 5);
                // from the top down: each level's intake must replace what it loses (metabolism + being eaten)
                float eatenAbove = 0;
                for (int l = 3; l >= 1; l--)
                {
                    p.m[l] = 1f / Turnover[l];
                    float intake = (p.m[l] * p.B[l] + eatenAbove) / Efficiency;   // prey mass it must eat a day
                    p.a[l] = intake / (p.B[l] * 0.5f);                             // (the response is half-saturated at the start)
                    eatenAbove = intake;
                }
                // producers regrow logistically to K = 2 x start: growth at the start equals what the grazers eat
                p.K = 2f * b0;
                p.r0 = eatenAbove / (b0 * (1f - b0 / p.K));
                // decomposers: fed by the dead (a third of every level's metabolism), breaking down at their own rate
                float dead = 0; for (int l = 1; l <= 3; l++) dead += p.m[l] * p.B[l] * 0.33f;
                p.m4 = b4 > 0 ? dead / b4 : 0;
                pools[i] = p;
            }
        }

        // the standard two biomes of the first build (tonnes): the Shallows and the Kelp Labyrinth
        public static Ecology FirstBuild() => new Ecology(
            ("shallows", 2400f, 260f, 30f, 3.0f, 40f),
            ("kelp", 3600f, 340f, 42f, 4.5f, 60f));

        public Pool Of(string biome) { foreach (var p in pools) if (p.biome == biome) return p; return null; }

        // energy: the sun drives the shallows' producers (daylight averaged to 1 over a day, so the balance holds)
        public static float Energy(float daylight) => daylight / 0.47f;

        public void Step(float days, float daylight)
        {
            float e = Energy(daylight);
            foreach (var p in pools)
            {
                var B = p.B; var s = p.start;
                float[] eat = new float[4];
                for (int l = 1; l <= 3; l++) eat[l] = p.a[l] * B[l] * B[l - 1] / (B[l - 1] + s[l - 1]);
                float dead = 0;
                float d0 = p.r0 * B[0] * (1f - B[0] / p.K) * e - eat[1];
                for (int l = 1; l <= 3; l++)
                {
                    float above = l < 3 ? eat[l + 1] : 0;
                    float d = Efficiency * eat[l] - p.m[l] * B[l] - above;
                    dead += p.m[l] * B[l] * 0.33f;
                    B[l] = Mathf.Max(s[l] * 0.01f, B[l] + d * days);
                }
                B[0] = Mathf.Clamp(B[0] + d0 * days, s[0] * 0.02f, p.K);
                B[4] = Mathf.Max(0, B[4] + (dead - p.m4 * B[4]) * days);
                p.memory = Mathf.Max(0, p.memory - days / 6f);     // the sea forgets over about a week
            }
        }

        // the crew takes mass out of a level (rations, spearfishing): it's gone from the web, and remembered
        public void Harvest(string biome, int level, float tonnes)
        {
            var p = Of(biome); if (p == null) return;
            p.B[level] = Mathf.Max(p.start[level] * 0.01f, p.B[level] - tonnes);
            p.harvested += tonnes;
            p.memory = Mathf.Min(2f, p.memory + tonnes / Mathf.Max(0.01f, p.start[level]) * 3f);
        }

        // a starving apex leaves its territory for the shallower biome next door
        public bool ApexInvading(string biome) { var p = Of(biome); return p != null && p.Starving(3); }
    }
}
