// The Deep's sounds are made, not recorded (stage 8): every effect, bed and instrument is synthesised once at start-up
// from these building blocks into a float buffer (44.1 kHz mono), then turned into an AudioClip (Bank.cs). Pure math,
// no Unity calls, so the bank can render on worker threads.
using System;

namespace Deep
{
    public static class Synth
    {
        public const int Rate = 44100;
        public const float TwoPi = (float)(Math.PI * 2);

        public static int N(float seconds) => Math.Max(1, (int)(seconds * Rate));

        // ---- a small, seedable random --------------------------------------------------------------------------
        public class Rng
        {
            uint s;
            public Rng(int seed) { s = (uint)seed * 2654435761u + 0x9E3779B9u; if (s == 0) s = 1; }
            public uint Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
            public float F() => (Next() & 0xFFFFFF) / 16777216f;            // 0..1
            public float S() => F() * 2f - 1f;                              // -1..1
            public float R(float a, float b) => a + (b - a) * F();
        }

        // ---- sources -------------------------------------------------------------------------------------------
        public static float[] White(int n, Rng r) { var a = new float[n]; for (int i = 0; i < n; i++) a[i] = r.S(); return a; }
        public static float[] Pink(int n, Rng r)
        {
            var a = new float[n]; float b0 = 0, b1 = 0, b2 = 0;
            for (int i = 0; i < n; i++)
            {
                float w = r.S();
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                a[i] = (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
            }
            return a;
        }
        public static float[] Brown(int n, Rng r) { var a = new float[n]; float y = 0; for (int i = 0; i < n; i++) { y = (y + r.S() * 0.04f) * 0.998f; a[i] = y * 3f; } return a; }

        public enum Wave { Sine, Saw, Square, Tri }
        static float Osc(Wave w, float ph)
        {
            ph -= (float)Math.Floor(ph);
            switch (w)
            {
                case Wave.Saw: return ph * 2f - 1f;
                case Wave.Square: return ph < 0.5f ? 1f : -1f;
                case Wave.Tri: return ph < 0.5f ? ph * 4f - 1f : 3f - ph * 4f;
                default: return (float)Math.Sin(ph * TwoPi);
            }
        }
        // an oscillator whose frequency follows f(t) (seconds)
        public static float[] Tone(int n, Func<float, float> freq, Wave w = Wave.Sine, float phase0 = 0)
        {
            var a = new float[n]; double ph = phase0;
            for (int i = 0; i < n; i++) { float t = i / (float)Rate; ph += freq(t) / Rate; a[i] = Osc(w, (float)ph); }
            return a;
        }
        public static float[] Tone(int n, float hz, Wave w = Wave.Sine) => Tone(n, t => hz, w);

        // FM: a carrier at f, modulated at f * ratio with an index that follows idx(t)
        public static float[] FM(int n, float f, float ratio, Func<float, float> idx)
        {
            var a = new float[n]; double pc = 0, pm = 0;
            for (int i = 0; i < n; i++)
            {
                float t = i / (float)Rate;
                pm += f * ratio / Rate; pc += f / Rate;
                a[i] = (float)Math.Sin(pc * TwoPi + idx(t) * Math.Sin(pm * TwoPi));
            }
            return a;
        }

        // a plucked string (Karplus-Strong), damped by `decay` (0.99..0.999) and brightened by `bright` (0..1)
        public static float[] Pluck(int n, float hz, Rng r, float decay = 0.996f, float bright = 0.5f)
        {
            var a = new float[n]; int len = Math.Max(2, (int)(Rate / hz));
            var buf = new float[len];
            for (int i = 0; i < len; i++) buf[i] = r.S();
            int p = 0; float last = 0;
            for (int i = 0; i < n; i++)
            {
                float x = buf[p];
                int q = (p + 1) % len;
                float y = (x * (0.5f + bright * 0.5f) + buf[q] * (0.5f - bright * 0.5f)) * decay;
                buf[p] = y * 0.5f + last * 0.5f; last = y;
                a[i] = x; p = q;
            }
            return a;
        }

        // ---- filters (biquads, RBJ) ---------------------------------------------------------------------------
        public static void LowPass(float[] a, float hz, float q = 0.707f) => Biquad(a, 0, t => hz, q);
        public static void HighPass(float[] a, float hz, float q = 0.707f) => Biquad(a, 1, t => hz, q);
        public static void BandPass(float[] a, float hz, float q = 1f) => Biquad(a, 2, t => hz, q);
        public static void LowPass(float[] a, Func<float, float> hz, float q = 0.707f) => Biquad(a, 0, hz, q);
        public static void BandPass(float[] a, Func<float, float> hz, float q = 1f) => Biquad(a, 2, hz, q);
        // kind 0 low, 1 high, 2 band (constant peak), the cutoff following hz(t)
        public static void Biquad(float[] a, int kind, Func<float, float> hz, float q)
        {
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float lastF = -1;
            for (int i = 0; i < a.Length; i++)
            {
                if ((i & 31) == 0)
                {
                    float f = Math.Max(10f, Math.Min(Rate * 0.45f, hz(i / (float)Rate)));
                    if (Math.Abs(f - lastF) > 0.01f)
                    {
                        lastF = f;
                        float w = TwoPi * f / Rate, cs = (float)Math.Cos(w), sn = (float)Math.Sin(w), al = sn / (2 * q), a0 = 1 + al;
                        if (kind == 0) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; }
                        else if (kind == 1) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; }
                        else { b0 = al; b1 = 0; b2 = -al; }
                        a1 = -2 * cs; a2 = 1 - al;
                        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
                    }
                }
                float x = a[i];
                float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                a[i] = y;
            }
        }
        // a resonant bank (metal, wood): the input rings at each mode's frequency, decaying at its own rate
        public static float[] Modes(float[] input, float[] hz, float[] decay, float[] gain)
        {
            var o = new float[input.Length];
            for (int m = 0; m < hz.Length; m++)
            {
                float w = TwoPi * hz[m] / Rate, r = (float)Math.Exp(-1.0 / (decay[m] * Rate));
                float c = 2 * r * (float)Math.Cos(w), y1 = 0, y2 = 0;
                for (int i = 0; i < o.Length; i++) { float y = input[i] * (1 - r) + c * y1 - r * r * y2; y2 = y1; y1 = y; o[i] += y * gain[m]; }
            }
            return o;
        }

        // ---- shaping ----------------------------------------------------------------------------------------------
        // attack, hold, release (seconds) - an exponential-ish release
        public static void Env(float[] a, float att, float hold, float rel, float curve = 3f)
        {
            int na = (int)(att * Rate), nh = (int)(hold * Rate), nr = Math.Max(1, (int)(rel * Rate));
            for (int i = 0; i < a.Length; i++)
            {
                float g;
                if (i < na) g = i / (float)Math.Max(1, na);
                else if (i < na + nh) g = 1;
                else { float t = (i - na - nh) / (float)nr; g = t >= 1 ? 0 : (float)Math.Pow(1 - t, curve); }
                a[i] *= g;
            }
        }
        public static void Shape(float[] a, Func<float, float> g) { for (int i = 0; i < a.Length; i++) a[i] *= g(i / (float)Rate); }
        public static void Decay(float[] a, float seconds) { float k = (float)Math.Exp(-1.0 / (seconds * Rate)), g = 1; for (int i = 0; i < a.Length; i++) { a[i] *= g; g *= k; } }
        public static void Fade(float[] a, float inS, float outS)
        {
            int ni = (int)(inS * Rate), no = (int)(outS * Rate);
            for (int i = 0; i < ni && i < a.Length; i++) a[i] *= i / (float)ni;
            for (int i = 0; i < no && i < a.Length; i++) a[a.Length - 1 - i] *= i / (float)no;
        }
        public static float[] Mix(int n, params (float[] a, float g)[] parts)
        {
            var o = new float[n];
            foreach (var (a, g) in parts) for (int i = 0; i < n && i < a.Length; i++) o[i] += a[i] * g;
            return o;
        }
        public static void Add(float[] into, float[] a, float g, int at = 0)
        {
            for (int i = 0; i < a.Length; i++) { int j = i + at; if (j >= 0 && j < into.Length) into[j] += a[i] * g; }
        }
        public static void Soft(float[] a, float drive = 1f) { for (int i = 0; i < a.Length; i++) a[i] = (float)Math.Tanh(a[i] * drive); }
        public static void Normalize(float[] a, float peak = 0.9f)
        {
            float m = 1e-6f; foreach (var x in a) m = Math.Max(m, Math.Abs(x));
            float k = peak / m; for (int i = 0; i < a.Length; i++) a[i] *= k;
        }
        public static void RemoveDC(float[] a) { float x1 = 0, y1 = 0; for (int i = 0; i < a.Length; i++) { float y = a[i] - x1 + 0.995f * y1; x1 = a[i]; y1 = y; a[i] = y; } }
        // a cheap room: a few feedback comb echoes (the space a sound rings in, baked in)
        public static float[] Smear(float[] a, float seconds, float wet, float tail = 0)
        {
            int extra = (int)(tail * Rate);
            var o = new float[a.Length + extra]; Array.Copy(a, o, a.Length);
            int[] d = { (int)(0.0297f * Rate), (int)(0.0371f * Rate), (int)(0.0411f * Rate), (int)(0.0437f * Rate) };
            float g = (float)Math.Pow(0.001, 0.035 / Math.Max(0.05, seconds));
            var w = new float[o.Length];
            foreach (var dl in d)
            {
                var buf = new float[dl]; int p = 0;
                for (int i = 0; i < o.Length; i++) { float y = buf[p]; buf[p] = (i < a.Length ? a[i] : 0) + y * g; p = (p + 1) % dl; w[i] += y * 0.25f; }
            }
            LowPass(w, 3500);
            for (int i = 0; i < o.Length; i++) o[i] = o[i] * (1 - wet * 0.5f) + w[i] * wet;
            return o;
        }
        // make a buffer loop seamlessly: its last `xf` seconds fade into its start
        public static float[] Loopable(float[] a, float xf)
        {
            int k = Math.Min(a.Length / 3, (int)(xf * Rate));
            var o = new float[a.Length - k];
            Array.Copy(a, o, o.Length);
            for (int i = 0; i < k; i++) { float t = i / (float)k; o[i] = a[i] * t + a[o.Length + i] * (1 - t); }
            return o;
        }

        // one bubble: a sine whose pitch rises as it breaks free, decaying fast (Minnaert resonance)
        public static void Bubble(float[] into, int at, float hz, float amp, float dur)
        {
            int n = (int)(dur * Rate); double ph = 0;
            for (int i = 0; i < n; i++)
            {
                int j = at + i; if (j >= into.Length) break;
                float t = i / (float)n;
                ph += hz * (1 + t * 1.6f) / Rate;
                into[j] += (float)Math.Sin(ph * TwoPi) * amp * (float)Math.Exp(-t * 5f) * Math.Min(1f, i / 40f);
            }
        }

        public static float Midi(float note) => 440f * (float)Math.Pow(2, (note - 69) / 12.0);

        // the numbers the audio test checks
        public static (float peak, float rms, float dc) Stats(float[] a)
        {
            double s = 0, s2 = 0; float p = 0;
            foreach (var x in a) { p = Math.Max(p, Math.Abs(x)); s += x; s2 += x * x; }
            return (p, (float)Math.Sqrt(s2 / Math.Max(1, a.Length)), (float)(s / Math.Max(1, a.Length)));
        }
    }
}
