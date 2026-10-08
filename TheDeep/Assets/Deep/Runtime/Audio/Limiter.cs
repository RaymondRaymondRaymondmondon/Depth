// The last thing before the speakers (stage 8): a soft limiter on the listener, so a crowded moment (a strobe, a
// cave-in, the danger music and a roar at once) bends instead of clipping. Below about -6 dBFS it does nothing.
using UnityEngine;

namespace Deep
{
    public class Limiter : MonoBehaviour
    {
        float gain = 1f;
        void OnAudioFilterRead(float[] data, int channels)
        {
            for (int i = 0; i < data.Length; i += channels)
            {
                float peak = 0;
                for (int c = 0; c < channels; c++) peak = Mathf.Max(peak, Mathf.Abs(data[i + c]));
                // a fast attack, slow release gain toward keeping the peak under 0.8, then a soft knee for what slips by
                float want = peak * gain > 0.8f ? 0.8f / Mathf.Max(peak, 1e-6f) : 1f;
                gain = want < gain ? Mathf.Lerp(gain, want, 0.5f) : Mathf.Lerp(gain, want, 0.0002f);
                for (int c = 0; c < channels; c++)
                {
                    float x = data[i + c] * gain;
                    float ax = Mathf.Abs(x);
                    if (ax > 0.5f) x = Mathf.Sign(x) * (0.5f + 0.48f * (float)System.Math.Tanh((ax - 0.5f) / 0.48f));
                    data[i + c] = x;
                }
            }
        }
    }
}
