// Marine snow: specks of organic matter drifting slowly down through the light, in a box round the eye.
using UnityEngine;

namespace Deep
{
    public static class MarineSnow
    {
        public static ParticleSystem Make(Transform parent)
        {
            var go = new GameObject("MarineSnow"); go.transform.SetParent(parent);
            var ps = go.AddComponent<ParticleSystem>();
            ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = ps.main;
            main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.startLifetime = 9f; main.startSpeed = 0.05f; main.startSize = new ParticleSystem.MinMaxCurve(0.015f, 0.05f);
            main.maxParticles = 1400; main.gravityModifier = 0.0015f;
            var em = ps.emission; em.rateOverTime = 150;
            var sh = ps.shape; sh.shapeType = ParticleSystemShapeType.Box; sh.scale = new Vector3(26, 16, 26);
            var noise = ps.noise; noise.enabled = true; noise.strength = 0.06f; noise.frequency = 0.25f;
            var col = ps.colorOverLifetime; col.enabled = true;
            var g = new Gradient();
            g.SetKeys(new[] { new GradientColorKey(Color.white, 0), new GradientColorKey(Color.white, 1) },
                      new[] { new GradientAlphaKey(0, 0), new GradientAlphaKey(1, 0.2f), new GradientAlphaKey(1, 0.8f), new GradientAlphaKey(0, 1) });
            col.color = g;
            var r = go.GetComponent<ParticleSystemRenderer>();
            r.sharedMaterial = new Material(Resources.Load<Shader>("Shaders/Snow"));
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            ps.Play();
            return ps;
        }
    }
}
