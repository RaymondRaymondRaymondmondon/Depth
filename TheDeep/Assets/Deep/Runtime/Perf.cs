// Keeping the frame rate on this PC (stage 7; the user: "optimize it to be able to run on this PC", Intel UHD). The game
// aims at 60 fps. When the frame rate stays under 42 for a few seconds (a crowded reef, the storm, the whole crew in
// view) it steps the costly things down a level - how far plants are drawn, how many animals are kept alive round the
// divers, the shadows' reach, the camera's far plane in clear water - and steps back up after a long stretch over 57.
// Four levels; F3 shows the frame rate and the level.
using UnityEngine;

namespace Deep
{
    public class Perf : MonoBehaviour
    {
        public static Perf I;
        public int level;                  // 0 full .. 3 lightest
        public float fps = 60f;
        float lowT, highT; bool show;
        float baseShadow;
        static readonly float[] FloraScale = { 1f, 0.8f, 0.65f, 0.5f };
        static readonly int[] Animals = { 420, 340, 260, 190 };
        static readonly float[] ShadowScale = { 1f, 0.75f, 0.55f, 0.4f };
        static readonly float[] FarScale = { 1f, 0.9f, 0.8f, 0.7f };

        public static Perf Attach()
        {
            var p = new GameObject("Perf").AddComponent<Perf>(); I = p;
            p.baseShadow = QualitySettings.shadowDistance;
            p.Apply();
            return p;
        }

        void Apply()
        {
            Flora.DistScale = FloraScale[level];
            if (Life.I != null) Life.I.maxLive = Animals[level];
            QualitySettings.shadowDistance = baseShadow * ShadowScale[level];
            UnderwaterLook.FarScale = FarScale[level];
        }

        void Update()
        {
            float dt = Mathf.Max(1e-4f, Time.unscaledDeltaTime);
            fps = Mathf.Lerp(fps, 1f / dt, 0.05f);
            if (Input.GetKeyDown(KeyCode.F3)) show = !show;
            if (!string.IsNullOrEmpty(Args.Shot) || Args.NetTest) return;   // (the harness measures; it doesn't adapt)
            if (fps < 42f) { lowT += dt; highT = 0; } else if (fps > 57f) { highT += dt; lowT = 0; } else { lowT = Mathf.Max(0, lowT - dt); highT = 0; }
            if (lowT > 4f && level < 3) { level++; lowT = 0; Apply(); Debug.Log($"DEEP PERF: {fps:0} fps, lighter (level {level})"); }
            if (highT > 15f && level > 0) { level--; highT = 0; Apply(); Debug.Log($"DEEP PERF: {fps:0} fps, fuller (level {level})"); }
        }

        void OnGUI()
        {
            if (!show) return;
            var st = new GUIStyle(GUI.skin.label) { fontSize = 13 }; st.normal.textColor = Color.white;
            GUI.Label(new Rect(Screen.width - 260, Screen.height - 64, 250, 22), $"{fps:0} fps   detail level {level} of 3", st);
            GUI.Label(new Rect(Screen.width - 260, Screen.height - 44, 250, 22), $"animals {(Life.I != null ? Life.I.live.Count : 0)}   plants x{Flora.DistScale:0.00}", st);
        }
    }
}
