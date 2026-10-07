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

        // the helm's instruments: the compass, the telegraph's order and the screw's speed, the depth and the order,
        // the rudder angle (drawn plainly for now; the brass dials on the bridge model come with the station pass)
        void HelmPanel(Nautilus n, bool helm)
        {
            var r = new Rect(Screen.width / 2 - 260, Screen.height - 150, 520, 120);
            GUI.color = new Color(0.05f, 0.04f, 0.02f, 0.72f); GUI.DrawTexture(r, Texture2D.whiteTexture); GUI.color = Color.white;
            var st = new GUIStyle(small) { fontSize = 15 }; st.normal.textColor = new Color(0.95f, 0.82f, 0.55f);
            float x = r.x + 16, y = r.y + 10;
            GUI.Label(new Rect(x, y, 240, 22), $"Telegraph  {Nautilus.TeleNames[n.telegraph]}", st);
            GUI.Label(new Rect(x, y + 24, 240, 22), $"Speed  {n.SpeedText}" + (n.powerK < 0.95f ? "  (no power)" : ""), st);
            GUI.Label(new Rect(x, y + 48, 240, 22), $"Heading  {n.heading:000}" + (n.holdHeading ? $"  holding {n.headingOrder:000}" : ""), st);
            GUI.Label(new Rect(x, y + 72, 240, 22), n.grounded > 0.5f ? "On the bottom" : "Afloat", st);
            if (helm)
            {
                GUI.Label(new Rect(x + 260, y, 240, 22), $"Depth  {n.Depth:0} m   order {n.depthOrder:0} m", st);
                GUI.Label(new Rect(x + 260, y + 24, 240, 22), "Rudder", st);
                var bar = new Rect(x + 330, y + 30, 160, 10);
                GUI.color = new Color(0, 0, 0, 0.6f); GUI.DrawTexture(bar, Texture2D.whiteTexture);
                GUI.color = new Color(0.95f, 0.8f, 0.4f);
                float c = bar.x + bar.width / 2, w = n.rudder * bar.width / 2;
                GUI.DrawTexture(new Rect(Mathf.Min(c, c + w), bar.y, Mathf.Max(2, Mathf.Abs(w)), bar.height), Texture2D.whiteTexture);
                GUI.color = Color.white;
                GUI.Label(new Rect(x + 260, y + 48, 260, 44), "A/D rudder  X centre  H hold\nW/S telegraph  Space/C depth  E leave", small);
            }
        }

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
            if (diver.aboard && diver.ship)
            {
                var g = Nautilus.FromLocal(diver.ship.Proxy.InverseTransformPoint(diver.transform.position));
                var room = diver.ship.L.RoomAt(g);
                GUI.Label(new Rect(24, 86, 400, 22), "Aboard the Nautilus" + (room != null ? ": " + room.name : "") + (diver.ship.power ? "" : "  (no power)"), small);
            }
            if (diver.manning != null && diver.ship) HelmPanel(diver.ship, diver.manning.kind == "helm");
            var mid = new GUIStyle(small) { alignment = TextAnchor.MiddleCenter, fontSize = 16 };
            if (!string.IsNullOrEmpty(diver.hint)) GUI.Label(new Rect(Screen.width / 2 - 300, Screen.height / 2 + 40, 600, 24), diver.hint, mid);
            if (!string.IsNullOrEmpty(diver.toast)) GUI.Label(new Rect(Screen.width / 2 - 300, Screen.height - 190, 600, 24), diver.toast, mid);
            GUI.Label(new Rect(Screen.width / 2 - 3, Screen.height / 2 - 11, 10, 20), "·", mid);
            if (Cursor.lockState != CursorLockMode.Locked) GUI.Label(new Rect(Screen.width / 2 - 120, Screen.height / 2 + 30, 300, 22), "Click to look around (Esc frees the mouse)", small);
        }
    }
}
