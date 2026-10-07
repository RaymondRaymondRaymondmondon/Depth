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

        // the sonar's screen: the last ping's chart (north up, fading), the ship at the centre, her heading
        void SonarPanel(Nautilus n)
        {
            var sy = n.sys; float size = 300;
            var r = new Rect(Screen.width / 2 - size / 2, Screen.height / 2 - size / 2 - 20, size, size);
            GUI.color = new Color(0.01f, 0.05f, 0.03f, 0.9f); GUI.DrawTexture(new Rect(r.x - 10, r.y - 10, r.width + 20, r.height + 44), Texture2D.whiteTexture);
            if (sy.sonarMap && sy.sonarAge < 40f)
            {
                GUI.color = new Color(1, 1, 1, Mathf.Clamp01(1.2f - sy.sonarAge / 40f));
                // the chart is drawn with world +z up; flip so north is up on the screen
                GUI.DrawTextureWithTexCoords(r, sy.sonarMap, new Rect(0, 0, 1, 1));
            }
            GUI.color = new Color(0.4f, 1f, 0.6f);
            var c = r.center;
            float hd = n.heading * Mathf.Deg2Rad;
            for (int k = 0; k < 12; k++)
            {
                var d = new Vector2(Mathf.Sin(hd), -Mathf.Cos(hd)) * (k * 4);
                GUI.DrawTexture(new Rect(c.x + d.x - 1.5f, c.y + d.y - 1.5f, 3, 3), Texture2D.whiteTexture);
            }
            GUI.DrawTexture(new Rect(c.x - 3, c.y - 3, 6, 6), Texture2D.whiteTexture);
            GUI.color = Color.white;
            var st = new GUIStyle(small); st.normal.textColor = new Color(0.5f, 1f, 0.65f);
            GUI.Label(new Rect(r.x - 60, r.yMax + 6, r.width + 160, 22), sy.sonarAge < 999f ? $"Ping {sy.sonarAge:0} s ago   {ShipSystems.SonarRange:0} m   orange: ground standing over her keel" : "Space: ping", st);
            // passive: the headphones - big animals by their sound, with bearings relative to her bow
            var heard = Life.I != null ? Life.I.Listen(n.Body.position, 900f) : null;
            if (heard == null || heard.Count == 0) GUI.Label(new Rect(r.x, r.y - 34, r.width + 200, 22), "Passive: nothing on the headphones but the sea.", st);
            else
            {
                for (int i = 0; i < Mathf.Min(4, heard.Count); i++)
                {
                    var h = heard[i];
                    string what = h.size > 20 ? "a vast, slow call" : h.size > 6 ? "something large" : "a big swimmer";
                    string move = h.speed > 3 ? ", closing fast" : h.speed > 1 ? ", moving" : ", lying still";
                    GUI.Label(new Rect(r.xMax + 20, r.y + i * 22, 340, 22), $"{Mathf.Repeat(h.bearing - n.heading, 360f):000} rel  {h.range:0} m  {what}{move}", st);
                    // and a blip on the chart
                    var d = new Vector2(Mathf.Sin(h.bearing * Mathf.Deg2Rad), -Mathf.Cos(h.bearing * Mathf.Deg2Rad)) * Mathf.Min(1f, h.range / ShipSystems.SonarRange) * r.width / 2;
                    GUI.color = new Color(1f, 0.35f, 0.3f); GUI.DrawTexture(new Rect(c.x + d.x - 3, c.y + d.y - 3, 6, 6), Texture2D.whiteTexture); GUI.color = Color.white;
                }
                GUI.Label(new Rect(r.x, r.y - 34, r.width + 200, 22), $"Passive: {heard.Count} contact{(heard.Count == 1 ? "" : "s")} on the headphones", st);
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
            var sv = diver.GetComponent<Survival>();
            if (sv != null)
            {
                void Small(float y, float v, Color c, string n)
                {
                    var b = new Rect(r.x, y, 220, 6);
                    GUI.color = new Color(0, 0, 0, 0.5f); GUI.DrawTexture(b, Texture2D.whiteTexture);
                    GUI.color = c; GUI.DrawTexture(new Rect(b.x, b.y, b.width * Mathf.Clamp01(v), b.height), Texture2D.whiteTexture);
                    GUI.color = Color.white; GUI.Label(new Rect(b.xMax + 6, y - 8, 160, 20), n, small);
                }
                Small(r.y - 90, sv.hunger / 100f, new Color(0.95f, 0.7f, 0.3f), "food");
                Small(r.y - 78, sv.thirst / 100f, new Color(0.4f, 0.75f, 1f), "water");
                Small(r.y - 66, (sv.bodyC - 28f) / 9.5f, sv.Shivering ? new Color(0.5f, 0.7f, 1f) : new Color(1f, 0.6f, 0.4f), sv.Shivering ? $"{sv.bodyC:0.0} C shivering" : $"{sv.bodyC:0.0} C");
            }
            var hands = diver.GetComponent<Hands>();
            if (hands != null && !diver.aboard)
            {
                var bar = hands.Hotbar();
                float bx = Screen.width / 2 - 5 * 66 / 2;
                for (int k = 0; k < 5; k++)
                {
                    var br = new Rect(bx + k * 66, Screen.height - 58, 62, 40);
                    GUI.color = new Color(0, 0, 0, 0.45f); GUI.DrawTexture(br, Texture2D.whiteTexture);
                    if (k < bar.Count)
                    {
                        var it = ItemDB.Get(hands.pack.slots[bar[k]].id);
                        GUI.color = bar[k] == hands.held ? new Color(1f, 0.85f, 0.5f) : Color.white;
                        GUI.Label(new Rect(br.x + 3, br.y + 1, br.width - 4, br.height), $"{k + 1} {it?.name}", small);
                    }
                    GUI.color = Color.white;
                }
                if (!string.IsNullOrEmpty(hands.aimHint))
                    GUI.Label(new Rect(Screen.width / 2 - 300, Screen.height / 2 + 18, 600, 22), hands.aimHint, new GUIStyle(small) { alignment = TextAnchor.MiddleCenter });
                if (hands.useT > 0) { GUI.color = new Color(1, 1, 1, 0.7f); GUI.DrawTexture(new Rect(Screen.width / 2 - 40, Screen.height / 2 + 42, 80 * hands.useT, 4), Texture2D.whiteTexture); GUI.color = Color.white; }
            }
            var hb = new Rect(r.x, r.y - 52, 220, 8);
            GUI.color = new Color(0, 0, 0, 0.5f); GUI.DrawTexture(hb, Texture2D.whiteTexture);
            GUI.color = diver.health > 35 ? new Color(0.55f, 0.9f, 0.5f) : new Color(1f, 0.35f, 0.3f);
            GUI.DrawTexture(new Rect(hb.x, hb.y, hb.width * Mathf.Clamp01(diver.health / 100f), hb.height), Texture2D.whiteTexture);
            GUI.color = Color.white;
            GUI.Label(new Rect(Screen.width - 90, 18, 80, 22), $"{fps:0} fps", small);
            if (diver.aboard && diver.ship)
            {
                var g = Nautilus.FromLocal(diver.ship.Proxy.InverseTransformPoint(diver.transform.position));
                var room = diver.ship.L.RoomAt(g);
                var sy = diver.ship.sys;
                var cab = diver.ship.L.CabinAt(g);
                GUI.Label(new Rect(24, 86, 500, 22), "Aboard the Nautilus" + (room != null ? ": " + room.name : "") + (cab != null ? ", " + cab.name : ""), small);
                if (sy != null)
                {
                    string ps = sy.state == PowerState.Engine ? "Engine" : sy.state == PowerState.Silent ? "Silent running" : "Dead in the water";
                    GUI.Label(new Rect(24, 106, 500, 22), $"{ps}   battery {sy.battery * 100:0}%   fuel {sy.fuel * 100:0}%   {sy.NoiseDb:0} dB", small);
                    float wt = sy.WaterTonnes;
                    if (wt > 0.5f || sy.breaches.Count > 0)
                    {
                        var warn = new GUIStyle(small); warn.normal.textColor = new Color(1f, 0.55f, 0.45f);
                        GUI.Label(new Rect(24, 126, 500, 22), $"Flooding: {wt:0} t of water aboard, {sy.breaches.Count} breach{(sy.breaches.Count == 1 ? "" : "es")} open", warn);
                    }
                }
            }
            if (diver.manning != null && diver.ship)
            {
                if (diver.manning.kind == "sonar") SonarPanel(diver.ship);
                else if (diver.manning.kind == "helm" || diver.manning.kind == "telegraph") HelmPanel(diver.ship, diver.manning.kind == "helm");
            }
            var mid = new GUIStyle(small) { alignment = TextAnchor.MiddleCenter, fontSize = 16 };
            if (!string.IsNullOrEmpty(diver.hint)) GUI.Label(new Rect(Screen.width / 2 - 300, Screen.height / 2 + 40, 600, 24), diver.hint, mid);
            if (!string.IsNullOrEmpty(diver.toast)) GUI.Label(new Rect(Screen.width / 2 - 300, Screen.height - 190, 600, 24), diver.toast, mid);
            GUI.Label(new Rect(Screen.width / 2 - 3, Screen.height / 2 - 11, 10, 20), "·", mid);
            if (Cursor.lockState != CursorLockMode.Locked) GUI.Label(new Rect(Screen.width / 2 - 120, Screen.height / 2 + 30, 300, 22), "Click to look around (Esc frees the mouse)", small);
        }
    }
}
