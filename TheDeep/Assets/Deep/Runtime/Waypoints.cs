// Waypoints (playtest: "when you first get to the ship there is no way point"): markers on the screen for where to go
// next, drawn over the world with a name and a distance, pinned to the screen's edge (with an arrow) when off it.
//   - the opening: the Nautilus's lights while rowing, then her deck hatch close to, then (aboard) the switchboard, then
//     the helm in the cupola;
//   - after it: the reef by the start, where gathering begins (until you've been there), and her deck hatch to find
//     her again whenever you're out in the sea more than 50 m from her.
using UnityEngine;

namespace Deep
{
    public class Waypoints : MonoBehaviour
    {
        Diver d; Nautilus ship; Vector3 reef;
        GUIStyle st;
        public static Waypoints Attach(Diver d, Nautilus ship, Vector3 start)
        {
            var w = d.gameObject.AddComponent<Waypoints>(); w.d = d; w.ship = ship;
            var bed = DeepBoot.I.seabed; w.reef = new Vector3(start.x + 30f, 0, start.z); w.reef.y = bed.SampleY(w.reef.x, w.reef.z) + 2f;
            return w;
        }

        Vector3 Station(string kind) { foreach (var s in ship.L.stations) if (s.kind == kind) return ship.WorldPoint(Nautilus.G(s.pos)) + ship.Body.up * 1.2f; return ship.Body.position; }
        Vector3 Hatch() { foreach (var h in ship.L.hatches) if (h.kind == "deck") return ship.WorldPoint(Nautilus.G(h.outside)); return ship.Body.position + Vector3.up * 5f; }

        void Update()
        {
            var c = Campaign.I;
            if (c != null && !c.reachedReef && !d.aboard && (d.EyeWorld - reef).magnitude < 25f && (Opening.I == null || Opening.I.done)) { c.reachedReef = true; d.Toast("The reef: gather here (knife: plants; hand: loose ore). Tab for your pack."); }
        }

        void OnGUI()
        {
            if (!d || !ship || d.uiOpen) return;
            var cam = Camera.main; if (!cam) return;
            if (st == null) { st = new GUIStyle(GUI.skin.label) { fontSize = 13, alignment = TextAnchor.MiddleCenter, fontStyle = FontStyle.Bold }; }
            edge = 0;
            var op = Opening.I;
            if (op != null && !op.done)
            {
                if (!op.boarded)
                {
                    float dist = (d.EyeWorld - ship.Body.position).magnitude;
                    if (dist > 35f) Mark(cam, ship.Body.position + Vector3.up * 8f, "The Nautilus", new Color(1f, 0.45f, 0.35f));
                    else Mark(cam, Hatch(), "Her deck hatch", new Color(1f, 0.8f, 0.4f));
                }
                else if (!op.powered) Mark(cam, Station("power"), "The switchboard (reset the breakers)", new Color(1f, 0.8f, 0.4f));
                else Mark(cam, Station("helm"), "The helm (dive below 15 m)", new Color(1f, 0.8f, 0.4f));
                return;
            }
            var c = Campaign.I;
            if (c != null && !c.reachedReef) Mark(cam, reef, "The reef (start gathering here)", new Color(0.5f, 1f, 0.75f));
            if (!d.aboard && d.piloting == null && (d.EyeWorld - ship.Body.position).magnitude > 50f) Mark(cam, Hatch(), "The Nautilus", new Color(1f, 0.8f, 0.4f));
        }

        int edge;   // marks pinned to the edge this frame (stacked so they never overlap)

        void Mark(Camera cam, Vector3 w, string name, Color col)
        {
            float dist = (w - cam.transform.position).magnitude;
            var sp = cam.WorldToScreenPoint(w);
            st.normal.textColor = col;
            string text = $"{name}  {dist:0} m";
            bool on = sp.z > 0 && sp.x > 30 && sp.x < Screen.width - 30 && sp.y > 30 && sp.y < Screen.height - 30;
            if (on)
            {
                float y = Screen.height - sp.y;
                GUI.color = new Color(col.r, col.g, col.b, 0.9f); GUI.DrawTexture(new Rect(sp.x - 5, y - 5, 10, 10), Texture2D.whiteTexture); GUI.color = Color.white;
                GUI.Label(new Rect(sp.x - 160, y - 28, 320, 20), text, st);
                return;
            }
            // off the screen: along its edge, toward it
            var local = cam.transform.InverseTransformDirection(w - cam.transform.position);
            float ang = Mathf.Atan2(local.x, local.z);
            float x = Mathf.Clamp(Screen.width / 2 + Mathf.Sin(ang) * Screen.width * 0.45f, 90, Screen.width - 90);
            float yy = local.y > 10f && Mathf.Abs(ang) < 0.6f ? 70 : local.y < -10f && Mathf.Abs(ang) < 0.6f ? Screen.height - 120 : Screen.height * 0.45f;
            yy += (yy > Screen.height * 0.6f ? -22 : 22) * edge++;
            string arrow = Mathf.Abs(ang) < 0.6f ? (local.y > 0 ? "▲ " : "▼ ") : ang < 0 ? "◀ " : "";
            GUI.Label(new Rect(x - 170, yy, 340, 20), arrow + text + (ang >= 0.6f ? " ▶" : ""), st);
        }
    }
}
