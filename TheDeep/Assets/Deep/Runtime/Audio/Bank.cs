// Every sound in The Deep (stage 8), as a recipe: rendered once at start-up on worker threads (Synth.cs), then made into
// AudioClips on the main thread. A cue has variants (each rendered with its own seed, picked at random when it plays),
// a bus (effects, ambience, interface, music) and how far it carries. Beds and machinery are seamless loops.
//   -audiotest renders the whole bank and checks each sound: not silent, not clipped, no DC offset, loops seamless.
using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using static Deep.Synth;

namespace Deep
{
    public enum Bus { Fx, Amb, Ui, Music }

    public class Cue
    {
        public string name; public Bus bus; public int variants = 1; public bool loop;
        public float vol = 1f, near = 2f, far = 80f;           // its loudness, and the distances it carries over (m)
        public Func<Rng, float[]> make;
        public float[][] data; public AudioClip[] clips;
    }

    public static class Bank
    {
        public static readonly Dictionary<string, Cue> Cues = new Dictionary<string, Cue>();
        public static bool Ready;
        static Task rendering;

        static void C(string name, Bus bus, Func<Rng, float[]> make, int variants = 1, bool loop = false, float vol = 1f, float near = 2f, float far = 80f)
            => Cues[name] = new Cue { name = name, bus = bus, make = make, variants = variants, loop = loop, vol = vol, near = near, far = far };

        // ---- recipes ---------------------------------------------------------------------------------------------
        static float[] Noise(float sec, Rng r, float lo, float hi)
        {
            var a = White(N(sec), r);
            if (hi < 20000) LowPass(a, hi); if (lo > 20) HighPass(a, lo);
            return a;
        }
        static float[] Bubbles(float sec, Rng r, int count, float lo, float hi, float amp = 0.5f)
        {
            var a = new float[N(sec)];
            for (int k = 0; k < count; k++)
            {
                float t = (float)Math.Pow(r.F(), 1.4) * sec * 0.85f;
                Bubble(a, (int)(t * Rate), r.R(lo, hi), amp * r.R(0.4f, 1f), r.R(0.02f, 0.07f));
            }
            return a;
        }
        static float[] Metal(float sec, Rng r, float baseHz, float ring, float hard = 1f)
        {
            var x = new float[N(sec)];
            for (int i = 0; i < 260; i++) x[i] = r.S() * (1 - i / 260f) * hard;
            float[] hz = { baseHz, baseHz * 2.32f, baseHz * 3.91f, baseHz * 5.43f, baseHz * 7.12f };
            float[] dc = { ring, ring * 0.7f, ring * 0.45f, ring * 0.3f, ring * 0.2f };
            float[] g = { 1f, 0.7f, 0.5f, 0.35f, 0.25f };
            for (int i = 0; i < hz.Length; i++) hz[i] *= r.R(0.97f, 1.03f);
            return Modes(x, hz, dc, g);
        }
        static float[] Thud(float sec, Rng r, float hz, float decay)
        {
            var a = Tone(N(sec), t => hz * (1 + 1.5f * (float)Math.Exp(-t * 30)));
            Decay(a, decay);
            var n = Noise(sec, r, 80, 900); Decay(n, decay * 0.3f);
            return Mix(a.Length, (a, 1f), (n, 0.5f));
        }
        static float[] Whoosh(float sec, Rng r, float lo, float hi)
        {
            var a = Pink(N(sec), r);
            BandPass(a, t => lo + (hi - lo) * (float)Math.Sin(Math.PI * Math.Min(1, t / sec)), 0.9f);
            Shape(a, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / sec)), 1.5));
            return a;
        }
        static float[] Groan(float sec, Rng r, float hz, float wobble)
        {
            float ph = r.F() * 10;
            var a = Tone(N(sec), t => hz * (1 + wobble * (float)Math.Sin(t * 1.7f + ph) + 0.3f * wobble * (float)Math.Sin(t * 5.3f)), Wave.Saw);
            var bp = r.R(250, 700);
            BandPass(a, t => bp * (1 + 0.4f * (float)Math.Sin(t * 0.9f + ph)), 3f);
            var n = Noise(sec, r, 100, 1200); Shape(n, t => 0.3f);
            var o = Mix(a.Length, (a, 1f), (n, 0.25f));
            Shape(o, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / sec)), 0.8));
            return o;
        }
        static float[] Bell(float sec, float hz, float bright)
        {
            var a = FM(N(sec), hz, 3.5f, t => bright * 4f * (float)Math.Exp(-t * 3));
            var b = FM(N(sec), hz * 2.76f, 1.4f, t => 1.5f * (float)Math.Exp(-t * 5));
            var o = Mix(a.Length, (a, 1f), (b, 0.3f));
            Decay(o, sec * 0.3f); Fade(o, 0.002f, 0.05f);
            return o;
        }
        static float[] Bed(float sec, Func<Rng, float[]> f, Rng r) => Loopable(f(r), 1.2f);

        static void Recipes()
        {
            if (Cues.Count > 0) return;
            // ---- the diver -----------------------------------------------------------------------------------
            C("breath_in", Bus.Fx, r => { var a = Noise(1.1f, r, 900, 4200); BandPass(a, 2200, 1.4f); Shape(a, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / 1.1f)), 1.2) * (1 + 0.2f * (float)Math.Sin(t * 70))); return a; }, 3, vol: 0.32f);
            C("breath_out", Bus.Fx, r => { var a = Noise(0.9f, r, 300, 2500); Shape(a, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / 0.9f)), 2) * 0.5f); Add(a, Bubbles(1.6f, r, 34, 350, 1100, 0.55f), 1f, 0); return a; }, 3, vol: 0.38f);
            C("bubbles", Bus.Fx, r => Bubbles(1.0f, r, 18, 300, 1400), 4, vol: 0.4f, near: 1.5f, far: 25f);
            C("swim", Bus.Fx, r => Whoosh(0.7f, r, 120, 600), 3, vol: 0.25f, near: 1f, far: 12f);
            C("splash_in", Bus.Fx, r => { var a = Noise(0.8f, r, 200, 9000); Env(a, 0.005f, 0.05f, 0.6f); Add(a, Bubbles(1.2f, r, 40, 300, 1500, 0.6f), 0.8f, 3000); return a; }, 2, vol: 0.7f, far: 40f);
            C("splash_out", Bus.Fx, r => { var a = Noise(0.6f, r, 400, 7000); Env(a, 0.01f, 0.05f, 0.45f); return a; }, 2, vol: 0.5f, far: 30f);
            C("o2_low", Bus.Ui, r => { var a = Tone(N(0.5f), t => t < 0.18f ? 880 : 660); Shape(a, t => t < 0.16f || (t > 0.22f && t < 0.42f) ? 0.5f : 0f); Fade(a, 0.002f, 0.02f); return a; }, vol: 0.35f);
            C("heartbeat", Bus.Ui, r => { var a = new float[N(0.9f)]; Add(a, Thud(0.25f, r, 55, 0.08f), 1f); Add(a, Thud(0.25f, r, 50, 0.07f), 0.7f, N(0.24f)); return a; }, vol: 0.6f);
            C("hurt", Bus.Fx, r => { var a = Thud(0.5f, r, 70, 0.12f); var v = Tone(N(0.45f), t => 150 - t * 60, Wave.Saw); BandPass(v, 700, 2f); Env(v, 0.01f, 0.1f, 0.3f); Add(a, v, 0.5f); return a; }, 3, vol: 0.8f);
            C("death", Bus.Fx, r => { var a = Tone(N(2.6f), t => 110 * (float)Math.Exp(-t * 0.6f), Wave.Saw); LowPass(a, 500); Env(a, 0.05f, 0.4f, 2f); return Smear(a, 2f, 0.5f, 1f); }, vol: 0.6f);
            C("respawn", Bus.Ui, r => { var a = Bell(1.8f, 784, 0.4f); Add(a, Bell(1.8f, 1175, 0.3f), 0.5f, N(0.12f)); return a; }, vol: 0.35f);
            C("step_metal", Bus.Fx, r => { var a = Metal(0.35f, r, r.R(900, 1300), 0.06f, 0.6f); Add(a, Thud(0.2f, r, 90, 0.04f), 0.6f); return a; }, 5, vol: 0.32f, near: 1f, far: 18f);
            C("step_wood", Bus.Fx, r => { var a = Thud(0.25f, r, r.R(110, 150), 0.05f); var k = Metal(0.2f, r, r.R(280, 360), 0.03f, 0.4f); Add(a, k, 0.5f); return a; }, 5, vol: 0.3f, near: 1f, far: 18f);
            C("step_rug", Bus.Fx, r => { var a = Noise(0.15f, r, 100, 900); Env(a, 0.005f, 0.02f, 0.1f); return a; }, 3, vol: 0.15f, near: 1f, far: 8f);
            C("ladder", Bus.Fx, r => { var a = Thud(0.16f, r, r.R(170, 230), 0.03f); Add(a, Metal(0.12f, r, r.R(420, 520), 0.015f, 0.12f), 0.15f); LowPass(a, 1800); return a; }, 4, vol: 0.12f, near: 1f, far: 10f);
            C("land", Bus.Fx, r => { var a = Thud(0.4f, r, 75, 0.09f); Add(a, Metal(0.4f, r, 700, 0.1f, 0.5f), 0.4f); return a; }, 2, vol: 0.5f);
            // ---- tools -----------------------------------------------------------------------------------------
            C("knife_swing", Bus.Fx, r => Whoosh(0.3f, r, 300, 1800), 3, vol: 0.35f, near: 1f, far: 15f);
            C("knife_hit", Bus.Fx, r => { var a = Thud(0.3f, r, 120, 0.05f); var s = Noise(0.2f, r, 400, 3000); Env(s, 0.002f, 0.02f, 0.15f); Add(a, s, 0.6f); return a; }, 3, vol: 0.6f, far: 30f);
            C("blade_loop", Bus.Fx, r => Bed(4f, rr => { var a = Noise(4f, rr, 2500, 12000); Shape(a, t => 0.5f + 0.5f * (float)Math.Abs(Math.Sin(t * 37)) * rr.F()); var h = Tone(N(4f), t => 240 + 6 * (float)Math.Sin(t * 9)); return Mix(a.Length, (a, 0.6f), (h, 0.15f)); }, r), loop: true, vol: 0.3f, far: 25f);
            C("drill_loop", Bus.Fx, r => Bed(3f, rr =>
            {
                int n = N(3f);
                var m = Tone(n, t => 118 + 3 * (float)Math.Sin(t * 13), Wave.Saw); LowPass(m, 1400);
                var w = Tone(n, t => 940 + 25 * (float)Math.Sin(t * 7), Wave.Square); BandPass(w, 1800, 4f);
                var g = Noise(3f, rr, 600, 5000); Shape(g, t => 0.4f + 0.6f * rr.F());
                return Mix(n, (m, 0.6f), (w, 0.25f), (g, 0.35f));
            }, r), loop: true, vol: 0.45f, near: 2f, far: 60f);
            C("drill_bite", Bus.Fx, r => { var a = Noise(0.4f, r, 300, 4000); Shape(a, t => r.F() < 0.3f ? 1 : 0.3f); Env(a, 0.01f, 0.2f, 0.2f); return a; }, 3, vol: 0.4f, far: 40f);
            C("spear_fire", Bus.Fx, r => { var p = Pluck(N(0.7f), r.R(150, 190), r, 0.993f, 0.6f); var th = Thud(0.3f, r, 160, 0.04f); var o = Mix(p.Length, (p, 0.7f), (th, 0.8f)); Add(o, Whoosh(0.35f, r, 400, 2400), 0.4f); return o; }, 2, vol: 0.7f, far: 50f);
            C("spear_hit", Bus.Fx, r => { var a = Thud(0.35f, r, 100, 0.06f); Add(a, Noise(0.15f, r, 600, 4000), 0.3f); return a; }, 2, vol: 0.6f, far: 35f);
            C("gather_plant", Bus.Fx, r => { var a = Noise(0.25f, r, 1500, 8000); Shape(a, t => r.F() < 0.2f ? 1 : 0.1f); Env(a, 0.002f, 0.05f, 0.15f); Add(a, Bubbles(0.4f, r, 6, 500, 1400, 0.3f), 1f); return a; }, 4, vol: 0.5f, far: 15f);
            C("gather_ore", Bus.Fx, r => { var a = Thud(0.4f, r, 140, 0.06f); var c = Noise(0.3f, r, 1200, 9000); Env(c, 0.001f, 0.01f, 0.2f); Add(a, c, 0.7f); return a; }, 3, vol: 0.6f, far: 25f);
            C("pickup", Bus.Ui, r => { var a = Tone(N(0.18f), t => 520 + t * 1600); Env(a, 0.005f, 0.03f, 0.12f); return a; }, 2, vol: 0.25f);
            // ---- the interface ---------------------------------------------------------------------------------
            C("ui_open", Bus.Ui, r => { var a = Noise(0.25f, r, 800, 5000); Env(a, 0.03f, 0.05f, 0.15f); Add(a, Metal(0.25f, r, 1400, 0.04f, 0.3f), 0.5f); return a; }, vol: 0.25f);
            C("ui_close", Bus.Ui, r => { var a = Noise(0.2f, r, 500, 3000); Env(a, 0.01f, 0.03f, 0.14f); return a; }, vol: 0.2f);
            C("ui_click", Bus.Ui, r => Metal(0.12f, r, 2200, 0.02f, 0.5f), vol: 0.25f);
            C("ui_craft", Bus.Ui, r => { var a = new float[N(1.1f)]; for (int k = 0; k < 6; k++) Add(a, Metal(0.15f, r, 1600 + k * 60, 0.02f, 0.4f), 0.5f, N(k * 0.07f)); Add(a, Bell(1.0f, 1046, 0.5f), 0.5f, N(0.5f)); return a; }, vol: 0.35f);
            C("ui_error", Bus.Ui, r => { var a = Tone(N(0.3f), 180, Wave.Square); LowPass(a, 900); Env(a, 0.005f, 0.15f, 0.1f); return a; }, vol: 0.2f);
            C("toast", Bus.Ui, r => { var a = Tone(N(0.22f), 660); var b = Tone(N(0.22f), 990); var o = Mix(a.Length, (a, 0.6f), (b, 0.3f)); Env(o, 0.005f, 0.02f, 0.18f); return o; }, vol: 0.12f);
            C("place", Bus.Fx, r => { var a = Thud(0.4f, r, 95, 0.08f); Add(a, Metal(0.3f, r, 500, 0.05f, 0.4f), 0.3f); return a; }, 2, vol: 0.55f, far: 20f);
            C("eat", Bus.Ui, r => { var a = Noise(0.5f, r, 200, 1800); Shape(a, t => (float)Math.Abs(Math.Sin(t * 22)) * (1 - t * 1.8f)); return a; }, 2, vol: 0.3f);
            C("drink", Bus.Ui, r => { var a = new float[N(0.7f)]; for (int k = 0; k < 4; k++) Add(a, Thud(0.12f, r, 220 + k * 20, 0.03f), 0.5f, N(k * 0.16f)); return a; }, vol: 0.3f);
            C("save", Bus.Ui, r => { var a = Bell(1.4f, 523, 0.3f); Add(a, Bell(1.4f, 784, 0.25f), 0.6f, N(0.15f)); return a; }, vol: 0.25f);
            // ---- the Nautilus ----------------------------------------------------------------------------------
            C("engine_loop", Bus.Fx, r => Bed(4f, rr =>
            {
                // a steam engine: the pistons' beat (2.5 a second at the bed's pitch), the boiler's roar, the shaft's hum
                int n = N(4f);
                var beat = new float[n]; for (int k = 0; k < 10; k++) Add(beat, Thud(0.35f, rr, 48, 0.09f), 1f, N(k * 0.4f));
                var roar = Brown(n, rr); LowPass(roar, 220);
                var hum = Tone(n, 36, Wave.Saw); LowPass(hum, 140);
                return Mix(n, (beat, 0.7f), (roar, 0.6f), (hum, 0.35f));
            }, r), loop: true, vol: 0.55f, near: 6f, far: 400f);
            C("battery_loop", Bus.Fx, r => Bed(3f, rr => { int n = N(3f); var h = Tone(n, 60); var h2 = Tone(n, 120); var h3 = Tone(n, 180, Wave.Tri); var w = Noise(3f, rr, 2000, 6000); return Mix(n, (h, 0.35f), (h2, 0.25f), (h3, 0.08f), (w, 0.02f)); }, r), loop: true, vol: 0.18f, near: 3f, far: 30f);
            C("whir_loop", Bus.Fx, r => Bed(3f, rr => { int n = N(3f); var m = Tone(n, t => 310 + 4 * (float)Math.Sin(t * 3)); var g = Noise(3f, rr, 200, 800); return Mix(n, (m, 0.3f), (g, 0.25f)); }, r), loop: true, vol: 0.22f, near: 4f, far: 120f);
            C("hull_creak", Bus.Fx, r => Groan(r.R(1.6f, 3.2f), r, r.R(45, 95), r.R(0.06f, 0.18f)), 6, vol: 0.5f, near: 8f, far: 120f);
            C("hull_groan", Bus.Fx, r => { var a = Groan(r.R(3f, 4.5f), r, r.R(28, 40), 0.25f); Soft(a, 1.5f); return a; }, 3, vol: 0.8f, near: 10f, far: 300f);
            C("hull_pop", Bus.Fx, r => Metal(0.8f, r, r.R(300, 600), 0.25f, 1f), 4, vol: 0.45f, near: 6f, far: 80f);
            C("breach", Bus.Fx, r => { var a = Thud(1.2f, r, 45, 0.3f); var m = Metal(1.4f, r, 210, 0.4f, 1.2f); var h = Noise(1.4f, r, 1500, 12000); Env(h, 0.02f, 0.4f, 0.9f); return Mix(m.Length, (a, 1f), (m, 0.6f), (h, 0.5f)); }, 2, vol: 0.9f, near: 6f, far: 200f);
            C("jet_loop", Bus.Fx, r => Bed(3f, rr => { var a = Noise(3f, rr, 1200, 14000); var b = Noise(3f, rr, 200, 900); Shape(b, t => 0.6f + 0.4f * rr.F()); return Mix(a.Length, (a, 0.5f), (b, 0.4f)); }, r), loop: true, vol: 0.5f, near: 1.5f, far: 18f);
            C("flood_loop", Bus.Fx, r => Bed(4f, rr => { var a = Pink(N(4f), rr); LowPass(a, t => 700 + 400 * (float)Math.Sin(t * 1.3), 0.9f); Add(a, Bubbles(4f, rr, 60, 150, 600, 0.35f), 1f); return a; }, r), loop: true, vol: 0.45f, near: 3f, far: 25f);
            C("pump_loop", Bus.Fx, r => Bed(2.4f, rr => { int n = N(2.4f); var a = new float[n]; for (int k = 0; k < 3; k++) { Add(a, Thud(0.5f, rr, 70, 0.12f), 0.8f, N(k * 0.8f)); var s = Noise(0.5f, rr, 300, 1500); Env(s, 0.1f, 0.1f, 0.25f); Add(a, s, 0.35f, N(k * 0.8f + 0.25f)); } return a; }, r), loop: true, vol: 0.4f, near: 3f, far: 30f);
            C("alarm_bell", Bus.Fx, r => { var a = new float[N(1.4f)]; for (int k = 0; k < 18; k++) Add(a, Metal(0.12f, r, 1650, 0.05f, 0.8f), 0.6f, N(k * 0.045f)); Decay(a, 2f); return a; }, vol: 0.4f, near: 4f, far: 60f);
            C("telegraph", Bus.Fx, r => { var a = Bell(1.6f, 1480, 0.7f); Add(a, Bell(1.6f, 1480, 0.7f), 0.8f, N(0.28f)); Add(a, Metal(0.2f, r, 400, 0.05f, 1f), 0.5f); return a; }, vol: 0.5f, near: 3f, far: 40f);
            C("helm_creak", Bus.Fx, r => { var a = Groan(0.6f, r, r.R(160, 220), 0.1f); return a; }, 3, vol: 0.25f, near: 1f, far: 10f);
            C("switch", Bus.Fx, r => { var a = Thud(0.4f, r, 80, 0.06f); Add(a, Metal(0.4f, r, 900, 0.08f, 1f), 0.5f); return a; }, 2, vol: 0.6f, near: 2f, far: 25f);
            C("power_up", Bus.Fx, r => { var a = Tone(N(2.5f), t => 30 + 30 * Math.Min(1, t / 1.8f), Wave.Saw); LowPass(a, 400); Env(a, 0.3f, 1.4f, 0.8f); var c = Thud(0.5f, r, 60, 0.15f); Add(a, c, 1f); return a; }, vol: 0.7f, near: 6f, far: 60f);
            C("power_down", Bus.Fx, r => { var a = Tone(N(2.5f), t => 60 * (float)Math.Exp(-t * 1.2f) + 15, Wave.Saw); LowPass(a, 400); Env(a, 0.01f, 0.3f, 2.1f); return a; }, vol: 0.6f, near: 6f, far: 60f);
            C("sonar_ping", Bus.Fx, r => { var a = Tone(N(1.2f), t => 1480 - t * 25); Env(a, 0.004f, 0.12f, 1.0f, 2f); return Smear(a, 3f, 0.6f, 2.5f); }, vol: 0.6f, near: 20f, far: 800f);
            C("sonar_echo", Bus.Fx, r => { var a = Tone(N(0.6f), 1440); Env(a, 0.01f, 0.05f, 0.5f); LowPass(a, 2500); return Smear(a, 1.5f, 0.7f, 1f); }, vol: 0.2f);
            C("hatch", Bus.Fx, r => { var sq = Tone(N(0.6f), t => 420 + 180 * (float)Math.Sin(t * 9), Wave.Saw); BandPass(sq, 900, 4f); Env(sq, 0.05f, 0.3f, 0.2f); var a = new float[N(1.3f)]; Add(a, sq, 0.4f); Add(a, Metal(0.7f, r, 380, 0.3f, 1.2f), 0.8f, N(0.6f)); Add(a, Thud(0.5f, r, 60, 0.1f), 0.8f, N(0.6f)); return a; }, 2, vol: 0.6f, near: 3f, far: 40f);
            C("airlock", Bus.Fx, r => { var a = Pink(N(2.4f), r); LowPass(a, t => 300 + 1200 * Math.Min(1, t / 1.5f)); Env(a, 0.2f, 1.4f, 0.8f); Add(a, Bubbles(2.4f, r, 50, 200, 900, 0.4f), 0.8f); Add(a, Metal(0.6f, r, 300, 0.3f, 1f), 0.7f, N(1.9f)); return a; }, vol: 0.6f, near: 3f, far: 30f);
            C("stoke", Bus.Fx, r => { var a = Noise(1.6f, r, 80, 2200); Env(a, 0.05f, 0.4f, 1.1f); Add(a, Metal(0.5f, r, 250, 0.2f, 1f), 0.6f); return a; }, vol: 0.6f, near: 3f, far: 30f);
            C("ground", Bus.Fx, r => { var a = Brown(N(2.5f), r); LowPass(a, 300); var s = Noise(2.5f, r, 300, 3000); Shape(s, t => 0.3f + 0.7f * (float)Math.Abs(Math.Sin(t * 13 + r.F()))); var o = Mix(a.Length, (a, 1f), (s, 0.45f)); Env(o, 0.05f, 1.2f, 1.2f); Add(o, Metal(2.4f, r, 140, 0.8f, 1.2f), 0.5f); return o; }, 2, vol: 0.9f, near: 10f, far: 400f);
            C("ram", Bus.Fx, r => { var a = Thud(2f, r, 38, 0.5f); Add(a, Metal(2.2f, r, 160, 1.2f, 2f), 0.8f); Soft(a, 1.3f); return a; }, 2, vol: 1f, near: 12f, far: 600f);
            C("kite_loop", Bus.Fx, r => Bed(2f, rr => { int n = N(2f); var m = Tone(n, t => 420 + 10 * (float)Math.Sin(t * 6), Wave.Tri); var p = Noise(2f, rr, 150, 1500); Shape(p, t => 0.6f + 0.4f * (float)Math.Sin(t * 60)); return Mix(n, (m, 0.2f), (p, 0.35f)); }, r), loop: true, vol: 0.45f, near: 3f, far: 120f);
            C("dock", Bus.Fx, r => { var a = Thud(0.6f, r, 70, 0.15f); Add(a, Metal(0.8f, r, 450, 0.3f, 1f), 0.6f); return a; }, vol: 0.7f, near: 3f, far: 40f);
            // ---- the surface -----------------------------------------------------------------------------------
            C("waves_loop", Bus.Amb, r => Bed(12f, rr => { var a = Pink(N(12f), rr); LowPass(a, t => 500 + 900 * (float)Math.Pow(0.5 + 0.5 * Math.Sin(t * 0.9 + Math.Sin(t * 0.37) * 2), 2), 0.8f); Shape(a, t => 0.45f + 0.55f * (float)Math.Pow(0.5 + 0.5 * Math.Sin(t * 0.9 + Math.Sin(t * 0.37) * 2), 1.5)); return a; }, r), loop: true, vol: 0.5f);
            C("wind_loop", Bus.Amb, r => Bed(10f, rr => { var a = Pink(N(10f), rr); BandPass(a, t => 500 + 350 * (float)Math.Sin(t * 0.5 + Math.Sin(t * 1.3)), 2f); Shape(a, t => 0.5f + 0.5f * (float)Math.Sin(t * 0.33)); return a; }, r), loop: true, vol: 0.4f);
            C("rain_loop", Bus.Amb, r => Bed(6f, rr => { var a = Noise(6f, rr, 1500, 11000); var d = new float[a.Length]; for (int k = 0; k < 900; k++) Add(d, Metal(0.03f, rr, rr.R(2500, 6000), 0.006f, 0.3f), rr.R(0.2f, 0.6f), (int)(rr.F() * a.Length)); return Mix(a.Length, (a, 0.35f), (d, 0.6f)); }, r), loop: true, vol: 0.45f);
            C("thunder", Bus.Amb, r => { var a = Brown(N(6f), r); LowPass(a, t => 900 * (float)Math.Exp(-t * 0.9) + 120); Env(a, 0.02f, 0.4f, 5f, 2f); var c = Noise(0.4f, r, 600, 8000); Env(c, 0.002f, 0.05f, 0.35f); Add(a, c, 0.6f); return Smear(a, 3f, 0.4f, 1f); }, 3, vol: 0.9f);
            C("oars", Bus.Fx, r => { var a = Whoosh(0.5f, r, 300, 2500); Add(a, Bubbles(0.6f, r, 10, 300, 900, 0.35f), 0.8f, N(0.2f)); return a; }, 3, vol: 0.45f, near: 2f, far: 30f);
            C("raft_creak", Bus.Fx, r => { var a = Groan(0.7f, r, r.R(220, 320), 0.15f); LowPass(a, 1800); return a; }, 3, vol: 0.25f, near: 2f, far: 15f);
            C("raft_flip", Bus.Fx, r => { var a = Noise(1.5f, r, 100, 8000); Env(a, 0.01f, 0.3f, 1.1f); Add(a, Bubbles(2f, r, 70, 200, 1500, 0.7f), 0.9f, N(0.3f)); return a; }, vol: 0.9f, near: 4f, far: 80f);
            // ---- the sea --------------------------------------------------------------------------------------
            C("amb_shallows", Bus.Amb, r => Bed(14f, rr =>
            {
                // the reef's crackle (snapping shrimp), the swell's slow breath far above, a fizz of tiny bubbles
                int n = N(14f);
                var crackle = new float[n]; for (int k = 0; k < 2600; k++) { int at = (int)(rr.F() * n); float g = rr.R(0.05f, 0.5f); for (int i = 0; i < 18 && at + i < n; i++) crackle[at + i] += rr.S() * g * (1 - i / 18f); }
                HighPass(crackle, 2500);
                var swell = Pink(n, rr); LowPass(swell, 220); Shape(swell, t => 0.6f + 0.4f * (float)Math.Sin(t * TwoPi / 7f));
                var fizz = Bubbles(14f, rr, 70, 900, 3000, 0.08f);
                return Mix(n, (crackle, 0.5f), (swell, 0.9f), (fizz, 1f));
            }, r), loop: true, vol: 0.5f);
            C("amb_kelp", Bus.Amb, r => Bed(16f, rr =>
            {
                int n = N(16f);
                var low = Brown(n, rr); LowPass(low, 140);
                var creaks = new float[n]; for (int k = 0; k < 4; k++) Add(creaks, Groan(rr.R(1.2f, 2.2f), rr, rr.R(180, 320), 0.12f), 0.2f, (int)(rr.F() * (n - N(2.3f))));
                var clicks = new float[n]; for (int k = 0; k < 400; k++) Add(clicks, Metal(0.02f, rr, rr.R(3000, 5000), 0.003f, 0.2f), rr.R(0.1f, 0.4f), (int)(rr.F() * n));
                return Mix(n, (low, 0.9f), (creaks, 1f), (clicks, 0.5f));
            }, r), loop: true, vol: 0.5f);
            C("amb_deep", Bus.Amb, r => Bed(18f, rr => { int n = N(18f); var a = Brown(n, rr); LowPass(a, 90); var t = Tone(n, x => 41 + 0.6f * (float)Math.Sin(x * 0.2f)); Shape(t, x => 0.15f + 0.1f * (float)Math.Sin(x * 0.31f)); return Mix(n, (a, 1f), (t, 0.6f)); }, r), loop: true, vol: 0.5f);
            C("amb_aboard", Bus.Amb, r => Bed(12f, rr =>
            {
                int n = N(12f);
                var room = Brown(n, rr); LowPass(room, 160);
                var drips = new float[n]; for (int k = 0; k < 7; k++) Add(drips, Metal(0.4f, rr, rr.R(1100, 1700), 0.12f, 0.15f), 0.4f, (int)(rr.F() * (n - N(0.5f))));
                var ticks = new float[n]; for (int k = 0; k < 3; k++) Add(ticks, Metal(0.3f, rr, rr.R(400, 700), 0.08f, 0.3f), 0.25f, (int)(rr.F() * (n - N(0.4f))));
                return Mix(n, (room, 0.6f), (drips, 1f), (ticks, 1f));
            }, r), loop: true, vol: 0.45f);
            C("underside_loop", Bus.Amb, r => Bed(8f, rr => { var a = Pink(N(8f), rr); LowPass(a, 400); Shape(a, t => 0.4f + 0.6f * (float)Math.Pow(0.5 + 0.5 * Math.Sin(t * 1.1 + Math.Sin(t * 0.4)), 2)); Add(a, Bubbles(8f, rr, 40, 400, 1200, 0.2f), 0.8f); return a; }, r), loop: true, vol: 0.45f);
            // ---- the animals ----------------------------------------------------------------------------------
            C("grouper_boom", Bus.Fx, r => { var a = Tone(N(1.6f), t => 62 - t * 8); Shape(a, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / 0.35f)), 1) * (float)Math.Exp(-t * 2.4)); var b = Tone(N(1.6f), 124); Shape(b, t => 0.2f * (float)Math.Exp(-t * 4)); return Smear(Mix(a.Length, (a, 1f), (b, 1f)), 1.5f, 0.4f, 0.8f); }, 2, vol: 0.8f, near: 8f, far: 250f);
            C("crab_clicks", Bus.Fx, r => { var a = new float[N(1f)]; for (int k = 0; k < 22; k++) Add(a, Metal(0.03f, r, r.R(2500, 4200), 0.004f, 0.6f), r.R(0.4f, 1f), N(k * 0.04f + r.F() * 0.02f)); return a; }, 3, vol: 0.4f, near: 2f, far: 40f);
            C("kill", Bus.Fx, r => { var a = Thud(0.5f, r, 110, 0.08f); var c = Noise(0.4f, r, 400, 3500); Shape(c, t => r.F() < 0.25f ? 1 : 0.2f); Env(c, 0.002f, 0.15f, 0.2f); Add(a, c, 0.6f); Add(a, Whoosh(0.5f, r, 150, 900), 0.6f); return a; }, 4, vol: 0.6f, near: 3f, far: 70f);
            C("bite", Bus.Fx, r => { var a = Thud(0.4f, r, 90, 0.06f); var s = Noise(0.15f, r, 800, 6000); Env(s, 0.001f, 0.02f, 0.12f); Add(a, s, 0.9f); var c = Noise(0.3f, r, 300, 2000); Shape(c, t => r.F() < 0.3f ? 1 : 0.1f); Add(a, c, 0.4f, N(0.05f)); return a; }, 3, vol: 0.9f, near: 3f, far: 50f);
            C("school_rush", Bus.Fx, r => { var a = Whoosh(1.4f, r, 300, 1600); Add(a, Bubbles(1.4f, r, 25, 600, 2000, 0.15f), 1f); return a; }, 3, vol: 0.35f, near: 3f, far: 25f);
            C("hunter_growl", Bus.Fx, r => { var a = Tone(N(1.4f), t => 70 + 15 * (float)Math.Sin(t * 6), Wave.Saw); var n = Noise(1.4f, r, 80, 900); var o = Mix(a.Length, (a, 0.7f), (n, 0.6f)); BandPass(o, t => 300 + 250 * (float)Math.Sin(t * 3), 1.5f); Env(o, 0.15f, 0.6f, 0.6f); Soft(o, 2f); return o; }, 3, vol: 0.7f, near: 5f, far: 120f);
            C("lev_call", Bus.Fx, r =>
            {
                // a leviathan's call: a slow, sliding song that fills the water (whale-like, but wrong)
                float len = r.R(4.5f, 7f), f0 = r.R(55, 95), f1 = f0 * r.R(1.6f, 2.6f), ph = r.F();
                var a = Tone(N(len), t => f0 + (f1 - f0) * (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / len) + ph * 0.3), 2) + 6 * (float)Math.Sin(t * 5), Wave.Saw);
                LowPass(a, t => 300 + 900 * (float)Math.Sin(Math.PI * Math.Min(1, t / len)), 2f);
                var sub = Tone(N(len), t => f0 * 0.5f);
                var o = Mix(a.Length, (a, 0.8f), (sub, 0.5f));
                Shape(o, t => (float)Math.Pow(Math.Sin(Math.PI * Math.Min(1, t / len)), 1.2));
                return Smear(o, 4f, 0.6f, 3f);
            }, 4, vol: 1f, near: 30f, far: 1500f);
            C("lev_roar", Bus.Fx, r =>
            {
                var a = Tone(N(3.2f), t => 48 + 30 * (float)Math.Exp(-t * 0.8) + 8 * (float)Math.Sin(t * 11), Wave.Saw);
                var n = Brown(N(3.2f), r);
                var o = Mix(a.Length, (a, 0.8f), (n, 0.8f));
                BandPass(o, t => 260 + 500 * (float)Math.Exp(-t), 1.2f);
                Env(o, 0.08f, 1.4f, 1.6f); Soft(o, 2.5f);
                return Smear(o, 3f, 0.5f, 2f);
            }, 3, vol: 1f, near: 20f, far: 900f);
            // ---- the caves ------------------------------------------------------------------------------------
            C("amb_caves", Bus.Amb, r => Bed(16f, rr =>
            {
                // drips falling into still water, their echoes long; a low stone hum; now and then the rock ticking
                int n = N(16f);
                var hum = Brown(n, rr); LowPass(hum, 70);
                var drips = new float[n];
                for (int k = 0; k < 22; k++) { var dp = Tone(N(0.25f), t => rr.R(900, 1900) * (1 + 1.2f * (float)System.Math.Exp(-t * 40))); Env(dp, 0.001f, 0.01f, 0.2f); Add(drips, Smear(dp, 2.5f, 0.75f, 1.6f), rr.R(0.1f, 0.45f), (int)(rr.F() * (n - N(2f)))); }
                var ticks = new float[n]; for (int k = 0; k < 5; k++) Add(ticks, Metal(0.3f, rr, rr.R(500, 900), 0.06f, 0.4f), 0.3f, (int)(rr.F() * (n - N(0.4f))));
                return Mix(n, (hum, 1f), (drips, 1f), (ticks, 0.8f));
            }, r), loop: true, vol: 0.55f);
            C("bats_loop", Bus.Amb, r => Bed(8f, rr => { int n = N(8f); var a = new float[n]; for (int k = 0; k < 160; k++) { var ch = Tone(N(0.05f), t => rr.R(5500, 8500) - t * 30000); Env(ch, 0.002f, 0.01f, 0.03f); Add(a, ch, rr.R(0.1f, 0.5f), (int)(rr.F() * (n - N(0.06f)))); } var flap = Pink(n, rr); BandPass(flap, 600, 1.5f); Shape(flap, t => 0.15f * (0.5f + 0.5f * (float)System.Math.Sin(t * 37))); return Mix(n, (a, 1f), (flap, 1f)); }, r), loop: true, vol: 0.35f);
            C("cave_in", Bus.Fx, r => { var a = Brown(N(4f), r); LowPass(a, 400); Env(a, 0.05f, 1.2f, 2.5f); var k = new float[N(4f)]; for (int i = 0; i < 14; i++) Add(k, Thud(0.5f, r, r.R(60, 160), 0.1f), r.R(0.3f, 1f), N(0.2f + r.F() * 2.8f)); var o = Mix(a.Length, (a, 1f), (k, 0.9f)); Soft(o, 1.4f); return Smear(o, 3f, 0.5f, 2f); }, 2, vol: 1f, near: 10f, far: 300f);
            C("strobe", Bus.Fx, r => { var a = Noise(0.35f, r, 2000, 14000); Env(a, 0.001f, 0.03f, 0.3f); var z = Tone(N(0.35f), t => 120 + 4000 * (float)System.Math.Exp(-t * 30), Wave.Square); Env(z, 0.001f, 0.02f, 0.25f); return Smear(Mix(a.Length, (a, 0.7f), (z, 0.4f)), 1.5f, 0.5f, 0.6f); }, 3, vol: 0.7f, near: 4f, far: 120f);

            // ---- the vents -------------------------------------------------------------------------------------
            C("amb_vents", Bus.Amb, r => Bed(16f, rr =>
            {
                // the ground's low growl, a seething fizz, distant crackling (the magma under the basalt), a boom far off
                int n = N(16f);
                var growl = Brown(n, rr); LowPass(growl, t => 60 + 25 * (float)System.Math.Sin(t * 0.4f), 1.2f);
                var fizz = Noise(16f, rr, 1800, 9000); Shape(fizz, t => 0.25f + 0.15f * (float)System.Math.Sin(t * 0.7f));
                var crack = new float[n]; for (int k = 0; k < 300; k++) { int at = (int)(rr.F() * n); float g = rr.R(0.05f, 0.35f); for (int i = 0; i < 30 && at + i < n; i++) crack[at + i] += rr.S() * g * (1 - i / 30f); }
                LowPass(crack, 2500);
                var boom = new float[n]; Add(boom, Thud(2.5f, rr, 34, 0.7f), 0.6f, (int)(rr.F() * (n - N(2.6f))));
                return Mix(n, (growl, 1f), (fizz, 0.4f), (crack, 0.7f), (boom, 0.8f));
            }, r), loop: true, vol: 0.55f);
            C("smoker_loop", Bus.Fx, r => Bed(4f, rr => { var a = Pink(N(4f), rr); LowPass(a, 900); var h = Noise(4f, rr, 2500, 12000); Shape(h, t => 0.3f); var b = Bubbles(4f, rr, 40, 120, 500, 0.4f); return Mix(a.Length, (a, 0.9f), (h, 0.35f), (b, 0.6f)); }, r), loop: true, vol: 0.6f, near: 3f, far: 70f);
            C("geyser_rumble", Bus.Fx, r => { var a = Brown(N(2.5f), r); LowPass(a, 180); Env(a, 0.4f, 1.6f, 0.5f); Add(a, Bubbles(2.5f, r, 60, 100, 600, 0.6f), 0.7f); return a; }, 2, vol: 0.9f, near: 6f, far: 250f);
            C("geyser", Bus.Fx, r => { var a = Noise(6.5f, r, 120, 7000); Env(a, 0.15f, 4.8f, 1.5f); var low = Brown(N(6.5f), r); LowPass(low, 140); Env(low, 0.1f, 5f, 1.4f); var o = Mix(a.Length, (a, 0.7f), (low, 1f)); Add(o, Bubbles(6.5f, r, 300, 150, 1200, 0.5f), 0.8f); Soft(o, 1.3f); return o; }, 2, vol: 1f, near: 10f, far: 400f);
            C("worm_rumble", Bus.Fx, r => { var a = Brown(N(2.2f), r); LowPass(a, 90); Env(a, 0.5f, 1.2f, 0.5f); var g = Groan(2.2f, r, 30, 0.3f); Add(a, g, 0.6f); Soft(a, 1.4f); return a; }, 2, vol: 1f, near: 15f, far: 300f);
            C("worm_burst", Bus.Fx, r => { var a = Thud(2.5f, r, 40, 0.6f); var c = Noise(1.8f, r, 200, 4000); Shape(c, t => r.F() < 0.3f ? 1 : 0.2f); Env(c, 0.01f, 0.5f, 1.2f); Add(a, c, 0.7f); Add(a, Bubbles(2.5f, r, 120, 100, 800, 0.6f), 0.6f); var roar = Tone(N(2.5f), t => 38 + 12 * (float)System.Math.Exp(-t * 2), Wave.Saw); BandPass(roar, 300, 1f); Env(roar, 0.05f, 1f, 1.2f); Add(a, roar, 0.6f); Soft(a, 1.6f); return Smear(a, 2.5f, 0.5f, 1.5f); }, 2, vol: 1f, near: 15f, far: 500f);

            // ---- instruments (one note each, played at other pitches by the score) --------------------------------
            C("ins_organ", Bus.Music, r => { int n = N(5f); var o = new float[n]; float f = Midi(48); float[] h = { 1, 2, 3, 4, 6, 8 }; float[] g = { 1, 0.6f, 0.3f, 0.35f, 0.15f, 0.12f }; for (int k = 0; k < h.Length; k++) Add(o, Tone(n, t => f * h[k] * (1 + 0.002f * (float)Math.Sin(t * 5.5f + k))), g[k] * 0.4f); Env(o, 0.12f, 3.6f, 1.2f); return Smear(o, 2.5f, 0.45f, 1.5f); });
            C("ins_pad", Bus.Music, r => { int n = N(8f); var o = new float[n]; float f = Midi(48); for (int k = 0; k < 4; k++) { float det = 1 + (k - 1.5f) * 0.004f; Add(o, Tone(n, t => f * det, Wave.Saw), 0.25f); } LowPass(o, t => 700 + 500 * (float)Math.Sin(t * 0.6f)); Env(o, 2.2f, 3.3f, 2.4f, 2f); return o; });
            C("ins_celesta", Bus.Music, r => Smear(Bell(3f, Midi(72), 0.35f), 2f, 0.4f, 1f));
            C("ins_harp", Bus.Music, r => { var a = Pluck(N(3.5f), Midi(60), r, 0.997f, 0.35f); Fade(a, 0.001f, 0.3f); return Smear(a, 2f, 0.35f, 1f); });
            C("ins_cello", Bus.Music, r => { int n = N(5f); var a = Tone(n, t => Midi(48) * (1 + 0.006f * (float)Math.Sin(t * 5.2f) * Math.Min(1, t)), Wave.Saw); LowPass(a, 1600); BandPass(a, 520, 0.8f); Env(a, 0.4f, 3.2f, 1.3f); return Smear(a, 2f, 0.35f, 1f); });
            C("ins_drone", Bus.Music, r => { int n = N(10f); var a = Tone(n, Midi(36)); var b = Tone(n, Midi(43)); var c = Tone(n, Midi(36) * 2.003f); var o = Mix(n, (a, 0.6f), (b, 0.35f), (c, 0.15f)); Env(o, 3f, 4f, 3f, 2f); return o; });
            C("ins_piano", Bus.Music, r => { int n = N(4f); var p = Pluck(n, Midi(60), r, 0.9985f, 0.8f); var s = Tone(n, Midi(60)); Decay(s, 1.2f); var o = Mix(n, (p, 0.6f), (s, 0.4f)); Fade(o, 0.001f, 0.2f); return Smear(o, 2f, 0.35f, 1f); });
            C("sting_danger", Bus.Music, r => { int n = N(3.5f); var o = new float[n]; foreach (var m in new[] { 36, 37, 43, 48 }) Add(o, Tone(n, Midi(m), Wave.Saw), 0.2f); LowPass(o, t => 300 + 1800 * (float)Math.Exp(-t * 1.5)); Env(o, 0.01f, 0.8f, 2.5f); Soft(o, 1.5f); return Smear(o, 2.5f, 0.5f, 1.5f); });
        }

        // ---- rendering ---------------------------------------------------------------------------------------------
        // the float buffers on worker threads (a second or two for the whole bank), then the clips on the main thread
        public static void BeginRender()
        {
            Recipes();
            if (rendering != null) return;
            var list = new List<Cue>(Cues.Values);
            rendering = Task.Run(() =>
            {
                Parallel.ForEach(list, c =>
                {
                    c.data = new float[c.variants][];
                    for (int v = 0; v < c.variants; v++)
                    {
                        try
                        {
                            var a = c.make(new Rng(c.name.GetHashCode() * 31 + v * 7919 + 17));
                            if (c.loop) { double m = 0; foreach (var x in a) m += x; m /= a.Length; for (int i = 0; i < a.Length; i++) a[i] -= (float)m; }   // (a filter would break the seam)
                            else RemoveDC(a);
                            Normalize(a, 0.9f);
                            c.data[v] = a;
                        }
                        catch (Exception) { c.data[v] = new float[N(0.1f)]; }
                    }
                });
            });
        }

        public static bool Finish()
        {
            if (Ready) return true;
            if (rendering == null || !rendering.IsCompleted) return false;
            foreach (var c in Cues.Values)
            {
                c.clips = new AudioClip[c.variants];
                for (int v = 0; v < c.variants; v++)
                {
                    var clip = AudioClip.Create(c.name + (c.variants > 1 ? "_" + v : ""), c.data[v].Length, 1, Rate, false);
                    clip.SetData(c.data[v], 0);
                    c.clips[v] = clip;
                }
            }
            Ready = true;
            return true;
        }

        public static AudioClip Clip(string name, int variant = -1)
        {
            if (!Ready || !Cues.TryGetValue(name, out var c) || c.clips == null) return null;
            return c.clips[variant < 0 ? UnityEngine.Random.Range(0, c.clips.Length) : Mathf.Clamp(variant, 0, c.clips.Length - 1)];
        }

        // ---- the audio test --------------------------------------------------------------------------------------------
        public static int Test()
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            BeginRender();
            rendering.Wait();
            Debug.Log($"DEEP AUDIOTEST: the bank rendered in {sw.ElapsedMilliseconds} ms");
            int bad = 0, n = 0; double secs = 0;
            foreach (var c in Cues.Values)
                for (int v = 0; v < c.variants; v++)
                {
                    var a = c.data[v]; n++; secs += a.Length / (double)Rate;
                    var (peak, rms, dc) = Stats(a);
                    bool nan = false; foreach (var x in a) if (float.IsNaN(x) || float.IsInfinity(x)) { nan = true; break; }
                    // a loop's seam: the step from its end back to its start, against how far the sound moves from one
                    // sample to the next anyway (noise moves a lot; a click would stand out from it)
                    float seam = 0;
                    if (c.loop) { double md = 0; for (int i = 1; i < a.Length; i++) md = System.Math.Max(md, System.Math.Abs(a[i] - a[i - 1])); seam = Mathf.Abs(a[a.Length - 1] - a[0]) / (float)System.Math.Max(1e-4, md); }
                    var why = new List<string>();
                    if (nan) why.Add("NaN");
                    if (rms < 0.01f) why.Add($"silent (rms {rms:0.000})");
                    if (peak > 0.99f) why.Add($"clipped ({peak:0.00})");
                    if (Mathf.Abs(dc) > 0.02f) why.Add($"DC {dc:0.000}");
                    if (c.loop && seam > 1.05f) why.Add($"loop seam {seam:0.00} of the largest step");
                    if (why.Count > 0) { bad++; Debug.Log($"DEEP AUDIOTEST: FAIL {c.name}[{v}]: {string.Join(", ", why)}"); }
                }
            Debug.Log($"DEEP AUDIOTEST: {n - bad} of {n} sounds pass ({Cues.Count} cues, {secs:0} s of audio)");
            return bad;
        }
    }
}
