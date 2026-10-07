// The diver's screens (stage 4), drawn plainly for now (the brass-and-paper UI comes with the polish stage):
//   Tab               the pack: click food or drink to have it, a tool or weapon to take it in hand
//   E at a station    its recipes (the fabricator, the forge, the grill, the desalinator...): the selected recipe's
//                     ingredients, what you have of each (your pack and the ship's stores together), Make
//   E at the lockers  the ship's stores: click to move things between your pack and her lockers
// Esc or Tab closes. While a screen is open the mouse is free and the diver doesn't move.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class CraftUI : MonoBehaviour
    {
        public Diver d; public Hands h; public Survival s;
        public string mode;           // null, "pack", "craft", "store"
        public string station;        // the station kind for "craft"
        ItemDef sel; Vector2 scroll;
        GUIStyle title, label, small, button, cell;

        public static CraftUI Attach(Diver d)
        {
            var u = d.gameObject.AddComponent<CraftUI>();
            u.d = d; u.h = d.GetComponent<Hands>(); u.s = d.GetComponent<Survival>();
            return u;
        }

        public void Open(string m, string st = null)
        {
            mode = m; station = st; sel = null; scroll = Vector2.zero;
            d.uiOpen = true;
            Cursor.lockState = CursorLockMode.None; Cursor.visible = true;
        }
        public void Close()
        {
            mode = null; d.uiOpen = false;
            Cursor.lockState = CursorLockMode.Locked; Cursor.visible = false;
        }

        Inventory Store => d.ship ? d.ship.store : null;

        void Update()
        {
            if (!d.inputEnabled) return;
            if (mode != null && (Input.GetKeyDown(KeyCode.Escape) || Input.GetKeyDown(KeyCode.Tab))) { Close(); return; }
            if (mode == null && Input.GetKeyDown(KeyCode.Tab)) Open("pack");
        }

        static string StationTitle(string k)
        {
            switch (k)
            {
                case "fabricator": return "The Fabricator"; case "forge": return "The Abyssal Forge"; case "grill": return "The Pressure Grill";
                case "desalinator": return "The Thermal Desalinator and the Bio-Osmosis Filter"; case "planter": return "Hydroponics: the Hydro-Juicer"; case "moonpool": return "The Moonpool"; case "boiler": return "The Boiler";
            }
            return k;
        }

        void Styles()
        {
            if (title != null) return;
            title = new GUIStyle(GUI.skin.label) { fontSize = 20, fontStyle = FontStyle.Bold }; title.normal.textColor = new Color(0.95f, 0.85f, 0.6f);
            label = new GUIStyle(GUI.skin.label) { fontSize = 14, wordWrap = true }; label.normal.textColor = new Color(0.9f, 0.88f, 0.8f);
            small = new GUIStyle(label) { fontSize = 12 };
            button = new GUIStyle(GUI.skin.button) { fontSize = 14, alignment = TextAnchor.MiddleLeft };
            cell = new GUIStyle(GUI.skin.button) { fontSize = 11, wordWrap = true, alignment = TextAnchor.UpperLeft };
        }

        void Box(Rect r, float a = 0.85f) { GUI.color = new Color(0.06f, 0.05f, 0.04f, a); GUI.DrawTexture(r, Texture2D.whiteTexture); GUI.color = Color.white; }

        void OnGUI()
        {
            if (mode == null || !d) return;
            Styles();
            float W = Mathf.Min(1100, Screen.width - 60), H = Mathf.Min(620, Screen.height - 80);
            var r = new Rect((Screen.width - W) / 2, (Screen.height - H) / 2, W, H);
            Box(r);
            if (mode == "craft") Craft(new Rect(r.x + 16, r.y + 12, W * 0.56f, H - 24));
            else if (mode == "store") Grid(new Rect(r.x + 16, r.y + 12, W * 0.47f, H - 24), Store, "Her lockers (click to take)", false);
            else Info(new Rect(r.x + 16, r.y + 12, W * 0.47f, H - 24));
            Grid(new Rect(r.x + W * (mode == "craft" ? 0.6f : 0.52f), r.y + 12, W * (mode == "craft" ? 0.38f : 0.46f), H - 24), h.pack,
                 mode == "store" ? "Your pack (click to stow)" : "Your pack", true);
            GUI.Label(new Rect(r.x + 16, r.yMax - 26, 500, 22), "Esc or Tab to close", small);
        }

        // the pack's grid: in the store, clicking moves an item across; otherwise it eats, drinks or takes a tool in hand
        void Grid(Rect r, Inventory inv, string head, bool pack)
        {
            if (inv == null) return;
            GUI.Label(new Rect(r.x, r.y, r.width, 26), head, title);
            int cols = 4; float cw = (r.width - 8) / cols, ch = 54;
            for (int i = 0; i < inv.slots.Count; i++)
            {
                var sl = inv.slots[i];
                var cr = new Rect(r.x + (i % cols) * cw, r.y + 34 + (i / cols) * (ch + 4), cw - 4, ch);
                if (cr.yMax > r.yMax) break;
                var it = sl.count > 0 ? ItemDB.Get(sl.id) : null;
                bool inHand = pack && i == h.held;
                GUI.color = inHand ? new Color(1f, 0.85f, 0.5f) : Color.white;
                if (GUI.Button(cr, it == null ? "" : $"{it.name}\n x{sl.count}", cell) && it != null)
                {
                    if (mode == "store")
                    {
                        var other = pack ? Store : h.pack;
                        if (other != null && other.Add(it) == 0) inv.Remove(it);
                    }
                    else if (it.Edible && s.Consume(it)) { inv.Remove(it); d.Toast($"You have the {it.name}."); }
                    else if (it.category == "tool" || it.category == "weapon") { h.held = i; d.Toast($"{it.name} in hand"); }
                    else sel = it;
                }
                GUI.color = Color.white;
            }
        }

        // with no station: the meters and the selected thing's description
        void Info(Rect r)
        {
            GUI.Label(new Rect(r.x, r.y, r.width, 26), "The diver", title);
            float y = r.y + 36;
            void Meter(string n, float v, float max, Color c)
            {
                GUI.Label(new Rect(r.x, y, 120, 20), n, label);
                var b = new Rect(r.x + 120, y + 5, r.width - 140, 10); Box(b, 0.6f);
                GUI.color = c; GUI.DrawTexture(new Rect(b.x, b.y, b.width * Mathf.Clamp01(v / max), b.height), Texture2D.whiteTexture); GUI.color = Color.white;
                y += 26;
            }
            Meter("Health", d.health, 100, new Color(0.55f, 0.9f, 0.5f));
            Meter("Food", s.hunger, 100, new Color(0.95f, 0.7f, 0.3f));
            Meter("Water", s.thirst, 100, new Color(0.4f, 0.75f, 1f));
            Meter($"Warmth {s.bodyC:0.0} C", s.bodyC - 28f, 9.5f, s.Shivering ? new Color(0.5f, 0.7f, 1f) : new Color(1f, 0.6f, 0.4f));
            Meter("Oxygen", d.oxygen, d.oxygenMax, new Color(0.5f, 0.85f, 1f));
            if (d.ship) GUI.Label(new Rect(r.x, y + 10, r.width, 22), $"The Nautilus: crush depth {d.ship.crushDepth:0} m", label);
            if (sel != null)
            {
                GUI.Label(new Rect(r.x, y + 50, r.width, 24), sel.name, title);
                GUI.Label(new Rect(r.x, y + 78, r.width, 120), (sel.description ?? "") + (sel.source != null ? $"\nFrom: {sel.source}" : "") + (sel.stats != null ? $"\n{sel.stats}" : ""), label);
            }
        }

        void Craft(Rect r)
        {
            GUI.Label(new Rect(r.x, r.y, r.width, 26), StationTitle(station), title);
            var list = ItemDB.MadeAt(station);
            var listRect = new Rect(r.x, r.y + 34, r.width * 0.48f, r.height - 70);
            var view = new Rect(0, 0, listRect.width - 18, list.Count * 30 + 4);
            scroll = GUI.BeginScrollView(listRect, scroll, view);
            for (int i = 0; i < list.Count; i++)
            {
                var it = list[i];
                bool can = h.pack.CanMake(it, Store);
                GUI.color = it == sel ? new Color(1f, 0.85f, 0.5f) : can ? Color.white : new Color(0.65f, 0.65f, 0.65f);
                if (GUI.Button(new Rect(0, i * 30, view.width, 28), $"{it.name}" + (it.phase > 2 ? $"  (phase {it.phase})" : ""), button)) sel = it;
            }
            GUI.color = Color.white;
            GUI.EndScrollView();
            if (list.Count == 0) GUI.Label(new Rect(r.x, r.y + 40, listRect.width, 60), "Nothing is made here yet.", label);
            if (sel == null) return;
            var dr = new Rect(r.x + r.width * 0.52f, r.y + 34, r.width * 0.48f, r.height - 70);
            GUI.Label(new Rect(dr.x, dr.y, dr.width, 24), sel.name, title);
            GUI.Label(new Rect(dr.x, dr.y + 28, dr.width, 90), (sel.description ?? "") + (sel.stats != null ? $"\n{sel.stats}" : ""), small);
            float y = dr.y + 124;
            foreach (var (ing, n) in sel.recipe)
            {
                int have = h.pack.Count(ing) + (Store != null ? Store.Count(ing) : 0);
                GUI.color = have >= n ? new Color(0.7f, 1f, 0.7f) : new Color(1f, 0.6f, 0.55f);
                GUI.Label(new Rect(dr.x, y, dr.width, 20), $"{have}/{n}  {ing.name}", label);
                y += 22;
            }
            GUI.color = Color.white;
            bool ok = h.pack.CanMake(sel, Store);
            GUI.enabled = ok;
            if (GUI.Button(new Rect(dr.x, Mathf.Min(y + 10, dr.yMax - 34), 160, 32), "Make", button) || (ok && Event.current.type == EventType.KeyDown && Event.current.keyCode == KeyCode.Return))
                Make(sel);
            GUI.enabled = true;
        }

        // stations that take time: the desalinator boils a cup of seawater in two minutes (the doc), with power
        readonly System.Collections.Generic.Dictionary<string, float> busyUntil = new System.Collections.Generic.Dictionary<string, float>();
        public static float Minutes(ItemDef it)
        {
            var o = it.effects?.other; if (string.IsNullOrEmpty(o)) return 0;
            var m = System.Text.RegularExpressions.Regex.Match(o, @"(\d+(\.\d+)?) minutes? each");
            return m.Success ? float.Parse(m.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture) : 0;
        }

        void Make(ItemDef it)
        {
            float mins = Minutes(it);
            if (mins > 0)
            {
                if (d.ship && !d.ship.power) { d.Toast("The desalinator needs power."); return; }
                if (busyUntil.TryGetValue(station, out float t) && Time.time < t) { d.Toast($"Still boiling: {t - Time.time:0} s"); return; }
                busyUntil[station] = Time.time + mins * 60f;
            }
            if (!h.pack.Make(it, Store)) return;
            // the Kite-Sub is launched into her moonpool's cradle
            if (it.name.IndexOf("Kite-Sub", System.StringComparison.OrdinalIgnoreCase) >= 0 && it.category == "vehicle" && d.ship)
            {
                h.pack.Remove(it); Store?.Remove(it);
                KiteSub.Spawn(d.ship);
                Net.Cmd(Net.C_KITE);
                d.Toast("The Kite-Sub is lowered into the moonpool's cradle.");
                Close();
                return;
            }
            // the steam engine's repair: she can run on her engine again
            if (it.name == "Steam engine" && d.ship && d.ship.sys)
            {
                h.pack.Remove(it); Store?.Remove(it);
                d.ship.sys.engineRepaired = true;
                Net.Cmd(Net.C_ENGINE);
                d.Toast("The steam engine turns over. The switchboard can give her the Engine now.");
                Close();
                return;
            }
            // a hull upgrade goes straight onto her: the crush depth rises
            if (it.category == "hull_upgrade" && d.ship)
            {
                foreach (var u in ItemDB.Hull)
                    if (string.Equals(u.name, it.name, System.StringComparison.OrdinalIgnoreCase) && u.crushDepthM > d.ship.crushDepth)
                    {
                        d.ship.crushDepth = u.crushDepthM;
                        Net.Cmd(Net.C_HULL, Mathf.RoundToInt(u.crushDepthM));
                        h.pack.Remove(it); Store?.Remove(it);
                        d.Toast($"{it.name} fitted: she can dive to {u.crushDepthM:0} m.");
                        return;
                    }
            }
            d.Toast($"Made: {it.name}");
        }
    }
}
