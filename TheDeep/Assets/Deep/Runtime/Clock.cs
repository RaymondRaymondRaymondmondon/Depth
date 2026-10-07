// The day (doc: dusk at 19:00, dawn at 05:00; deep species rise at night). One game day is 30 real minutes (an open
// call: the doc gives no length). The moon's phase steps once a day; day 0 is a new moon, day 14 a full moon.
using UnityEngine;

namespace Deep
{
    public class Clock : MonoBehaviour
    {
        public const float RealSecondsPerDay = 1800f;
        public float hour = 9.5f;      // 0..24
        public int day = 0;
        public bool frozen;            // (the screenshot harness holds the time)

        public float Daylight         // 0 at night, 1 at noon, with a soft dawn and dusk
        {
            get
            {
                float up = Mathf.Sin((hour - 6f) / 24f * Mathf.PI * 2f);    // sunrise 06:00, sunset 18:00
                return Mathf.Clamp01(up * 1.6f + 0.15f);
            }
        }
        public bool Night => hour >= 19f || hour < 5f;
        public float MoonFullness => 0.5f - 0.5f * Mathf.Cos(day % 28 / 28f * Mathf.PI * 2f);
        public Vector3 SunDirection   // the direction the light travels (from the sun toward the world)
        {
            get
            {
                float a = (hour - 6f) / 24f * Mathf.PI * 2f;   // 0 at sunrise, pi at sunset
                var toSun = new Vector3(Mathf.Cos(a) * 0.85f, Mathf.Sin(a), 0.35f).normalized;
                if (toSun.y < 0.05f) toSun = new Vector3(-Mathf.Cos(a) * 0.6f, 0.55f, -0.3f).normalized;   // (at night the moon lights the sea)
                return -toSun;
            }
        }

        void Update()
        {
            if (frozen) return;
            hour += Time.deltaTime * 24f / RealSecondsPerDay;
            if (hour >= 24f) { hour -= 24f; day++; }
        }
    }
}
