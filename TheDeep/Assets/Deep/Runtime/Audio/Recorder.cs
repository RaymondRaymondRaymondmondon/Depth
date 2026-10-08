// The screenshot harness's ears (stage 8): on the camera, it records what the listener hears (after every filter and
// the reverb), so a "listen_" shot can write it to a WAV beside the PNG and report its level and brightness.
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Deep
{
    public class Recorder : MonoBehaviour
    {
        public bool on;
        readonly List<float> left = new List<float>(), right = new List<float>();
        readonly object gate = new object();
        public int rate;

        void Awake() { rate = AudioSettings.outputSampleRate; }

        void OnAudioFilterRead(float[] data, int channels)
        {
            if (!on) return;
            lock (gate)
                for (int i = 0; i < data.Length; i += channels) { left.Add(data[i]); right.Add(channels > 1 ? data[i + 1] : data[i]); }
        }

        public void Begin() { lock (gate) { left.Clear(); right.Clear(); } on = true; }

        // stop, write the WAV (16-bit stereo) and say how loud and how bright it was
        public string End(string path)
        {
            on = false;
            float[] l, r; lock (gate) { l = left.ToArray(); r = right.ToArray(); }
            double s2 = 0; float peak = 0; double zc = 0;
            for (int i = 0; i < l.Length; i++) { float m = (l[i] + r[i]) * 0.5f; s2 += m * m; peak = Mathf.Max(peak, Mathf.Abs(l[i]), Mathf.Abs(r[i])); if (i > 0 && (l[i] >= 0) != (l[i - 1] >= 0)) zc++; }
            float rms = l.Length > 0 ? Mathf.Sqrt((float)(s2 / l.Length)) : 0;
            using (var f = new BinaryWriter(File.Create(path)))
            {
                int n = l.Length, bytes = n * 4;
                f.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); f.Write(36 + bytes); f.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt "));
                f.Write(16); f.Write((short)1); f.Write((short)2); f.Write(rate); f.Write(rate * 4); f.Write((short)4); f.Write((short)16);
                f.Write(System.Text.Encoding.ASCII.GetBytes("data")); f.Write(bytes);
                for (int i = 0; i < n; i++) { f.Write((short)Mathf.Clamp(l[i] * 32767f, -32768, 32767)); f.Write((short)Mathf.Clamp(r[i] * 32767f, -32768, 32767)); }
            }
            float secs = l.Length / (float)Mathf.Max(1, rate);
            return $"{secs:0.0} s, rms {20 * Mathf.Log10(Mathf.Max(rms, 1e-6f)):0.0} dBFS, peak {20 * Mathf.Log10(Mathf.Max(peak, 1e-6f)):0.0} dBFS, brightness ~{zc / Mathf.Max(0.01f, secs) / 2:0} Hz";
        }
    }
}
