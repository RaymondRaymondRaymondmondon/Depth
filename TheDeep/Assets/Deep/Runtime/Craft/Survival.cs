// The diver's body (stage 4): hunger, thirst and warmth beside the tank's oxygen and health (Diver.cs).
//   - Hunger and thirst fall steadily (faster working hard: sprinting, drilling); food and drink (made at the grill,
//     the desalinator) refill them. Empty, health drains.
//   - Warmth: the body drifts toward the water's temperature (cooler with depth and at night; the Kelp is cold) and back
//     toward 37 C aboard while she has life support (fastest by the boiler). Dead in the water, her rooms cool to the
//     sea (the design doc). Below 34 C you shiver (slower); below 32 C health drains.
// Rates come from Resources/Data/items.json's "survival" meters when they're given there, else the defaults below.
using UnityEngine;

namespace Deep
{
    public class Survival : MonoBehaviour
    {
        public Diver d;
        public float hunger = 100f, thirst = 100f, bodyC = 37f;
        public float hungerPerMin = 2.2f, thirstPerMin = 3.0f;     // (a full stomach lasts about 45 minutes, water 33)
        public float cabinC = 30f;                                 // the air aboard

        public static Survival Attach(Diver d)
        {
            var s = d.gameObject.AddComponent<Survival>(); s.d = d;
            var f = ItemDB.File;
            if (f?.survival?.meters != null)
                foreach (var m in f.survival.meters)
                {
                    if (m.drainPerMin <= 0) continue;
                    if (m.id == "hunger") s.hungerPerMin = m.drainPerMin;
                    if (m.id == "thirst") s.thirstPerMin = m.drainPerMin;
                }
            return s;
        }

        // the sea's temperature at a depth (warm sunlit shallows, the cold kelp; a few degrees down at night)
        public static float WaterC(float depth, float daylight)
        {
            float c = depth < 50f ? Mathf.Lerp(27f, 21f, depth / 50f) : Mathf.Lerp(16f, 9f, Mathf.Clamp01((depth - 50f) / 100f));
            return c - (1f - daylight) * 2.5f;
        }

        void Update()
        {
            if (!d || !d.inputEnabled) return;
            float dt = Time.deltaTime / 60f;   // minutes
            float work = d.vel.magnitude > 4.5f ? 1.6f : 1f;
            var hands = d.GetComponent<Hands>(); if (hands && hands.useT > 0) work += 0.5f;
            hunger = Mathf.Max(0, hunger - hungerPerMin * work * dt);
            thirst = Mathf.Max(0, thirst - thirstPerMin * work * dt);

            var clock = DeepBoot.I ? DeepBoot.I.clock : null;
            float day = clock ? clock.Daylight : 1f;
            var ship = d.ship;
            bool lifeSupport = ship && ship.sys && ship.sys.LifeSupport;
            float sea = WaterC(d.Depth, day);
            // the air aboard warms with life support (and the boiler), cools to the sea without
            if (ship)
            {
                float want = lifeSupport ? (ship.sys.state == PowerState.Engine ? 31f : 26f) : sea;
                cabinC = Mathf.MoveTowards(cabinC, want, dt * (lifeSupport ? 3f : 0.6f));
            }
            float env = d.aboard && !d.HeadUnderAboard ? cabinC : sea;
            float rate = d.aboard ? 0.35f : 0.12f;                     // per minute toward the surroundings (the suit insulates)
            if (d.aboard && lifeSupport && env > 24f) env = 37.5f;     // a warm cabin brings you back to normal
            bodyC = Mathf.MoveTowards(bodyC, env, Mathf.Abs(env - bodyC) * rate * dt + 0.0001f);
            bodyC = Mathf.Min(bodyC, 37.5f);

            // what it costs to run empty or cold
            float hurt = 0;
            if (hunger <= 0) hurt += 0.5f;
            if (thirst <= 0) hurt += 0.8f;
            if (bodyC < 32f) hurt += (32f - bodyC) * 0.6f;
            if (hurt > 0) { d.health -= hurt * Time.deltaTime; if (d.health <= 0) d.Hurt(1, hunger <= 0 ? "hunger" : thirst <= 0 ? "thirst" : "the cold"); }
            else if (hunger > 50 && thirst > 50 && bodyC > 34f) d.health = Mathf.Min(100f, d.health + 0.2f * Time.deltaTime);
        }

        public bool Shivering => bodyC < 34f;

        // eat or drink something from the pack
        public bool Consume(ItemDef it)
        {
            if (it == null || !it.Edible) return false;
            if (it.Raw)
            {
                // the doc: raw fish causes food poisoning unless it's grilled
                hunger = Mathf.Clamp(hunger + 12f, 0, 100); thirst = Mathf.Max(0, thirst - 5f); d.health = Mathf.Max(1f, d.health - 12f);
                d.Toast($"The raw {it.name.Substring(4)} turns your stomach. (Grill it.)");
                return true;
            }
            hunger = Mathf.Clamp(hunger + it.effects.hunger, 0, 100);
            thirst = Mathf.Clamp(thirst + it.effects.thirst, 0, 100);
            d.health = Mathf.Clamp(d.health + it.effects.health, 0, 100);
            d.oxygen = Mathf.Clamp(d.oxygen + it.effects.oxygen, 0, d.oxygenMax);
            return true;
        }
    }
}
