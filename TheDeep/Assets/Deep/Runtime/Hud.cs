// A plain first HUD (stage 1): depth, the biome, oxygen and the clock. The real wrist display comes with stage 4.
using UnityEngine;

namespace Deep
{
    public class Hud : MonoBehaviour
    {
        Diver diver; Clock clock; GUIStyle big, small; float fps = 60;
        public bool hidden;
        public void Bind(Diver d, Clock c) { diver = d; clock = c; }

        void Update() { fps = Mathf.Lerp(fps, 1f / Mathf.Max(0.001f, Time.unscaledDeltaTime), 0.05f); if (Input.GetKeyDown(KeyCode.F1)) hidden = !hidden; }

        void OnGUI()
        {
            if (hidden || !diver) return;
            if (big == null)
            {
                big = new GUIStyle(GUI.skin.label) { fontSize = 22, fontStyle = FontStyle.Bold }; big.normal.textColor = new Color(0.85f, 0.95f, 1f);
                small = new GUIStyle(GUI.skin.label) { fontSize = 14 }; small.normal.textColor = new Color(0.8f, 0.9f, 0.95f);
            }
            float d = diver.Depth;
            GUI.Label(new Rect(24, 18, 400, 30), $"{d:0} m", big);
            GUI.Label(new Rect(24, 46, 400, 22), UnderwaterLook.Underwater ? Biomes.At(d).name : "The surface", small);
            int h = (int)clock.hour, m = (int)((clock.hour - h) * 60);
            GUI.Label(new Rect(24, 66, 400, 22), $"{h:00}:{m:00}  day {clock.day + 1}", small);
            // oxygen: a bar, red when low
            var r = new Rect(24, Screen.height - 46, 220, 14);
            GUI.color = new Color(0, 0, 0, 0.5f); GUI.DrawTexture(r, Texture2D.whiteTexture);
            float f = diver.oxygen / diver.oxygenMax;
            GUI.color = f > 0.3f ? new Color(0.5f, 0.85f, 1f) : new Color(1f, 0.35f, 0.3f);
            GUI.DrawTexture(new Rect(r.x, r.y, r.width * f, r.height), Texture2D.whiteTexture);
            GUI.color = Color.white;
            GUI.Label(new Rect(r.x, r.y - 22, 300, 22), $"Oxygen {diver.oxygen:0} s", small);
            GUI.Label(new Rect(Screen.width - 90, 18, 80, 22), $"{fps:0} fps", small);
            if (Cursor.lockState != CursorLockMode.Locked) GUI.Label(new Rect(Screen.width / 2 - 120, Screen.height / 2 + 30, 300, 22), "Click to look around (Esc frees the mouse)", small);
        }
    }
}
