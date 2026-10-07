// The look of the sea, set every frame from where the camera is and the time of day:
// - fog: under water, the biome's water colour and visibility (blended across the depth bands), dimmed at night;
//   above water, a light haze toward the horizon colour
// - the sun: dimmer and bluer with depth (red light goes first), a pale moon at night
// - the light below the surface: absorption per metre and the water's ambient light, which the shaders apply at each
//   surface's own depth (DeepWater.hlsl), and the caustic texture they ripple over the shallows
// - light shafts from the surface, drifting marine snow, and URP post effects (bloom, ACES, a vignette under water)
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Deep
{
    public class UnderwaterLook : MonoBehaviour
    {
        public static bool Underwater;
        public static bool Aboard;   // the eye is inside the Nautilus (in her air)
        public static float CamDepth;
        public static Color WaterColor;
        Camera cam; Clock clock; Light sun; UniversalAdditionalLightData sunData;
        Texture2D caustic; Material sky;
        Volume volume; Vignette vignette; ColorAdjustments grade;
        Shafts shafts; ParticleSystem snow;

        public void Bind(Camera c, Clock k)
        {
            cam = c; clock = k;
            var sg = new GameObject("Sun"); sg.transform.SetParent(transform);
            sun = sg.AddComponent<Light>(); sun.type = LightType.Directional; sun.shadows = LightShadows.Hard; sun.shadowStrength = 0.75f;
            sunData = sg.AddComponent<UniversalAdditionalLightData>();
            RenderSettings.sun = sun;
            caustic = Caustics.Frames(1, 256, 1234)[0];
            Shader.SetGlobalTexture("_DeepCausticTex", caustic);
            sky = Resources.Load<Material>("DeepSky");
            RenderSettings.skybox = sky;
            RenderSettings.ambientMode = AmbientMode.Flat;
            RenderSettings.fog = true; RenderSettings.fogMode = FogMode.ExponentialSquared;

            var cd = cam.GetUniversalAdditionalCameraData();
            cd.renderPostProcessing = true; cd.antialiasing = AntialiasingMode.FastApproximateAntialiasing;
            var prof = ScriptableObject.CreateInstance<VolumeProfile>();
            var bloom = prof.Add<Bloom>(); bloom.intensity.Override(0.6f); bloom.threshold.Override(1.1f); bloom.scatter.Override(0.6f); bloom.highQualityFiltering.Override(false);
            var tm = prof.Add<Tonemapping>(); tm.mode.Override(TonemappingMode.ACES);
            vignette = prof.Add<Vignette>(); vignette.intensity.Override(0.2f); vignette.smoothness.Override(0.5f);
            grade = prof.Add<ColorAdjustments>(); grade.postExposure.Override(0.3f); grade.saturation.Override(5f); grade.contrast.Override(8f);
            var vg = new GameObject("PostFx"); vg.transform.SetParent(transform);
            volume = vg.AddComponent<Volume>(); volume.isGlobal = true; volume.sharedProfile = prof;

            shafts = new GameObject("Shafts").AddComponent<Shafts>(); shafts.transform.SetParent(transform);
            snow = MarineSnow.Make(transform);
        }

        void LateUpdate()
        {
            if (!cam) return;
            var p = cam.transform.position;
            float surf = Waves.Height(p.x, p.z, Time.time);
            Underwater = p.y < surf - 0.02f;
            CamDepth = Mathf.Max(0, -p.y);
            float day = clock.Daylight;
            float moon = 0.05f + 0.1f * clock.MoonFullness;

            // the sun (or the moon); the water's absorption is applied per surface in the shaders
            sun.transform.rotation = Quaternion.LookRotation(clock.SunDirection);
            float d = Underwater ? CamDepth : 0;
            var sunCol = Color.Lerp(new Color(0.55f, 0.62f, 0.85f), new Color(1.0f, 0.95f, 0.86f), day);
            float level = Mathf.Lerp(moon, 1.25f, day);
            sun.color = sunCol;
            sun.intensity = level;   // (the shaders absorb it at each surface's depth)

            // the water: the biome's colour at this depth, blended toward the next band, dimmed by the light left
            var b = Biomes.At(d); int bi = Biomes.IndexAt(d);
            Color wc = b.water; float vis = b.visibility;
            if (bi + 1 < Biomes.All.Length)
            {
                float t = Mathf.InverseLerp(b.bottom - 15, b.bottom, d);
                wc = Color.Lerp(wc, Biomes.All[bi + 1].water, t); vis = Mathf.Lerp(vis, Biomes.All[bi + 1].visibility, t);
            }
            float lightLeft = Mathf.Lerp(moon * 0.8f, 1f, day) * Mathf.Exp(-d / 200f);
            WaterColor = wc * Mathf.Max(0.05f, lightLeft);

            var skyTop = Color.Lerp(new Color(0.02f, 0.03f, 0.07f), new Color(0.22f, 0.45f, 0.85f), day);
            var horizon = Color.Lerp(new Color(0.05f, 0.06f, 0.10f), new Color(0.72f, 0.80f, 0.90f), day);
            float dusk = Mathf.Clamp01(1 - Mathf.Abs(day - 0.35f) * 4f);   // a warm horizon at dawn and dusk
            horizon = Color.Lerp(horizon, new Color(0.95f, 0.55f, 0.35f), dusk * 0.6f);

            if (Underwater)
            {
                RenderSettings.fogColor = WaterColor;
                RenderSettings.fogDensity = 1.3f / vis;     // (eased in the playtest: the helm couldn't see where she was going)
                RenderSettings.ambientLight = WaterColor * 1.6f + new Color(0.02f, 0.03f, 0.04f);
                cam.clearFlags = CameraClearFlags.SolidColor; cam.backgroundColor = WaterColor;
                cam.farClipPlane = Mathf.Min(400f, vis * 2.8f);
                vignette.intensity.value = 0.32f;
                grade.colorFilter.value = Color.Lerp(Color.white, new Color(0.85f, 1f, 1f), 0.6f);
            }
            else
            {
                RenderSettings.fogColor = horizon;
                RenderSettings.fogDensity = 0.0016f;
                RenderSettings.ambientLight = Color.Lerp(new Color(0.04f, 0.05f, 0.08f), new Color(0.45f, 0.52f, 0.6f), day);
                cam.clearFlags = CameraClearFlags.Skybox;
                cam.farClipPlane = 2000f;
                vignette.intensity.value = 0.15f;
                grade.colorFilter.value = Color.white;
                if (sky) { sky.SetColor("_SkyTint", Color.Lerp(new Color(0.1f, 0.1f, 0.2f), new Color(0.5f, 0.6f, 0.75f), day)); sky.SetFloat("_Exposure", Mathf.Lerp(0.15f, 1.3f, day)); }
            }

            // the water's light for the shaders: absorption per metre (red first; a gentle overall falloff, brighter than
            // real water so the Kelp Labyrinth stays readable), the ambient near the surface and at 150 m, and caustics
            Shader.SetGlobalVector("_DeepAbsorb", new Vector4(1f / 30f, 1f / 85f, 1f / 130f, 1f / 320f));
            float amb = Mathf.Lerp(moon * 0.6f, 1f, day);
            Shader.SetGlobalColor("_DeepAmbTop", new Color(0.16f, 0.34f, 0.36f) * amb);
            Shader.SetGlobalColor("_DeepAmbDeep", new Color(0.06f, 0.17f, 0.13f) * amb);
            Shader.SetGlobalFloat("_DeepCaustics", Mathf.Clamp01(day * 1.5f));

            Shader.SetGlobalVector("_DeepSunDir", clock.SunDirection);
            Shader.SetGlobalVector("_DeepSunColor", new Vector4(sunCol.r, sunCol.g, sunCol.b, day));
            Shader.SetGlobalColor("_DeepSkyTop", skyTop);
            Shader.SetGlobalColor("_DeepSkyHorizon", horizon);
            Shader.SetGlobalColor("_DeepWaterColor", WaterColor);
            Shader.SetGlobalFloat("_DeepCamDepth", d);
            Shader.SetGlobalFloat("_DeepUnderwater", Underwater ? 1 : 0);

            shafts.Tick(cam, clock, Underwater && !Aboard ? Mathf.Clamp01(day * 1.2f) * Mathf.Clamp01(1 - d / 70f) : 0);
            var em = snow.emission; em.enabled = Underwater && !Aboard;
            if (Aboard && snow.particleCount > 0) snow.Clear();
            snow.transform.position = p;
            var main = snow.main; main.startColor = Color.Lerp(new Color(0.6f, 0.7f, 0.7f, 0.5f), new Color(0.9f, 0.95f, 0.9f, 0.7f), day);
        }
    }
}
