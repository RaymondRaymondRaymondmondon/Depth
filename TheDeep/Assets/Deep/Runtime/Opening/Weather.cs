// The surface weather (stage 5): the storm of the opening (the design doc: board her and dive "before a surface storm or
// a surface-hunting leviathan destroys them"). As `storm` rises the swell builds (Waves.Calm, held under the waves'
// folding limit), the wind gets up (it pushes the raft), rain sheets down over the surface, the sky closes in, and
// lightning flashes now and then (_DeepFlash, read by UnderwaterLook). Under water you only notice the dimmer light and
// the flashes.
using UnityEngine;

namespace Deep
{
    public class Weather : MonoBehaviour
    {
        public static Weather I;
        public float storm, target;           // 0 calm .. 1 the full storm
        public float flash;                   // the last lightning's glare, fading
        public Vector2 windDir = new Vector2(0.8f, 0.6f).normalized;
        float nextBolt = 8f;
        ParticleSystem rain;

        public static float Storm => I ? I.storm : 0f;
        public static Vector2 Wind => I ? I.windDir * I.storm * 7f : Vector2.zero;   // m/s at the surface

        public static Weather Make()
        {
            var w = new GameObject("Weather").AddComponent<Weather>(); I = w;
            w.MakeRain();
            return w;
        }

        void MakeRain()
        {
            var go = new GameObject("Rain"); go.transform.SetParent(transform);
            rain = go.AddComponent<ParticleSystem>();
            rain.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = rain.main; main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.startLifetime = 1.4f; main.startSpeed = 0; main.startSize = new ParticleSystem.MinMaxCurve(0.02f, 0.035f); main.maxParticles = 3000;
            main.gravityModifier = 1.6f;
            var em = rain.emission; em.rateOverTime = 0;
            var sh = rain.shape; sh.shapeType = ParticleSystemShapeType.Box; sh.scale = new Vector3(40, 1, 40);
            var col = rain.colorOverLifetime; col.enabled = true;
            var g = new Gradient();
            g.SetKeys(new[] { new GradientColorKey(new Color(0.75f, 0.8f, 0.85f), 0), new GradientColorKey(new Color(0.75f, 0.8f, 0.85f), 1) },
                      new[] { new GradientAlphaKey(0.55f, 0), new GradientAlphaKey(0.55f, 1) });
            col.color = g;
            var r = go.GetComponent<ParticleSystemRenderer>();
            r.sharedMaterial = new Material(Resources.Load<Shader>("Shaders/Snow"));
            r.renderMode = ParticleSystemRenderMode.Stretch; r.velocityScale = 0.06f; r.lengthScale = 1f;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            rain.Play();
        }

        void Update()
        {
            float dt = Time.deltaTime;
            storm = Mathf.MoveTowards(storm, target, dt / 80f);
            Waves.Calm = 1f + 0.7f * storm;
            flash = Mathf.MoveTowards(flash, 0, dt * 4f);
            if (storm > 0.55f)
            {
                nextBolt -= dt;
                if (nextBolt <= 0) { flash = 1f; nextBolt = Random.Range(5f, 14f) / storm; }
            }
            Shader.SetGlobalFloat("_DeepFlash", flash);
            Shader.SetGlobalFloat("_DeepStorm", storm);
            // rain over the camera when it's in the air
            var cam = Camera.main;
            var em = rain.emission;
            bool above = cam && !UnderwaterLook.Underwater && !UnderwaterLook.Aboard;
            em.rateOverTime = above ? storm * storm * 2200f : 0;
            if (cam) rain.transform.position = cam.transform.position + Vector3.up * 14f + new Vector3(Wind.x, 0, Wind.y) * 1.2f;
            var vel = rain.velocityOverLifetime; vel.enabled = true; vel.x = Wind.x * 0.8f; vel.z = Wind.y * 0.8f; vel.y = -2f;
        }
    }
}
