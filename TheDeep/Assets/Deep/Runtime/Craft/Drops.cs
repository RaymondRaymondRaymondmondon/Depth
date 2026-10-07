// The death rule (the user's decision for the campaign): a diver who dies wakes in the Dive Room, and everything they
// carried is left where they died, in a canvas satchel with a small amber lamp that shows through the murk. The HUD
// points to your own (the newest); anyone in the crew can recover a satchel (E beside it): what fits goes in the pack,
// the rest stays in it. Satchels are the host's (Net.cs carries leaving and recovering) and the campaign saves them.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Drops : MonoBehaviour
    {
        public static Drops I;
        public class Drop
        {
            public int id; public string owner; public bool aboard; public Vector3 pos;   // the world, or (aboard) her local frame
            public Dictionary<string, int> items = new Dictionary<string, int>();
            public Transform t; public Material beacon;
            public int Count { get { int k = 0; foreach (var v in items.Values) k += v; return k; } }
        }
        public readonly Dictionary<int, Drop> drops = new Dictionary<int, Drop>();
        public int nextId = 1;
        Material lit, glow;
        Diver diver; Nautilus ship;

        public static Drops Attach(Diver d, Nautilus ship)
        {
            var dr = new GameObject("Drops").AddComponent<Drops>(); I = dr;
            dr.diver = d; dr.ship = ship;
            dr.lit = new Material(Shader.Find("Deep/Lit")); dr.lit.SetFloat("_UseVC", 1); dr.lit.SetFloat("_VCAlbedo", 1); dr.lit.SetFloat("_Roughness", 0.8f); dr.lit.SetFloat("_Cull", 0);
            dr.glow = new Material(dr.lit); dr.glow.SetFloat("_Emit", 1);
            return dr;
        }

        public Vector3 World(Drop d) => d.aboard && ship ? ship.WorldPoint(d.pos) : d.pos;

        public Drop Add(int id, string owner, bool aboard, Vector3 pos, Dictionary<string, int> items)
        {
            if (drops.ContainsKey(id)) Remove(id);
            var d = new Drop { id = id, owner = owner, aboard = aboard, pos = pos, items = new Dictionary<string, int>(items) };
            var go = new GameObject("Satchel " + owner);
            d.t = go.transform;
            if (aboard && ship) { d.t.SetParent(ship.Body, false); d.t.localPosition = pos; }
            else
            {
                // it settles on the bottom below where they died
                var bed = DeepBoot.I ? DeepBoot.I.seabed : null;
                if (bed) d.pos.y = Mathf.Max(bed.SampleY(pos.x, pos.z) + 0.02f, Mathf.Min(pos.y, -0.5f));
                d.t.position = d.pos;
                if (bed) d.pos.y = bed.SampleY(pos.x, pos.z) + 0.02f;
                d.t.position = d.pos;
            }
            d.t.localRotation = Quaternion.Euler(0, (id * 73) % 360, 0);
            void Part(string suf, Material m)
            {
                var mesh = ModelLibrary.Get("Decor/decor", "drop_satchel" + suf); if (!mesh) return;
                var g = new GameObject("part"); g.transform.SetParent(d.t, false);
                g.AddComponent<MeshFilter>().sharedMesh = mesh; g.AddComponent<MeshRenderer>().sharedMaterial = m;
            }
            Part("", lit); Part("_glow", glow);
            // the amber lamp: a glow that carries through the water
            var q = GameObject.CreatePrimitive(PrimitiveType.Quad); Destroy(q.GetComponent<Collider>());
            q.transform.SetParent(d.t, false); q.transform.localPosition = new Vector3(0, 0.75f, 0);
            d.beacon = new Material(Shader.Find("Deep/Glow")); d.beacon.SetColor("_Color", new Color(1f, 0.7f, 0.2f)); d.beacon.SetFloat("_Size", 0.8f); d.beacon.SetFloat("_Intensity", 1.6f);
            q.GetComponent<MeshRenderer>().sharedMaterial = d.beacon;
            drops[id] = d;
            nextId = Mathf.Max(nextId, id + 1);
            return d;
        }

        public void Remove(int id)
        {
            if (!drops.TryGetValue(id, out var d)) return;
            if (d.t) Destroy(d.t.gameObject);
            if (d.beacon) Destroy(d.beacon);
            drops.Remove(id);
        }

        // a death: the pack is left here (Diver.Hurt calls this before the diver wakes aboard)
        public void LeaveHere(Diver d)
        {
            var hands = d.GetComponent<Hands>(); if (hands == null) return;
            var items = new Dictionary<string, int>();
            foreach (var s in hands.pack.slots) if (s.count > 0 && !string.IsNullOrEmpty(s.id)) { items.TryGetValue(s.id, out int k); items[s.id] = k + s.count; }
            if (items.Count == 0) return;
            hands.pack.Clear(); hands.held = -1;
            bool aboard = d.aboard && ship;
            var pos = aboard ? ship.Body.InverseTransformPoint(ship.ToWorld(d.transform.position - Vector3.up * 0.75f)) : d.EyeWorld;
            Make(Net.I ? Net.I.myName : "Diver", aboard, pos, items);
        }

        // the host makes it (a crewmate's PC asks the host)
        public void Make(string owner, bool aboard, Vector3 pos, Dictionary<string, int> items)
        {
            if (Net.IsGuest) { Net.DropMake(owner, aboard, pos, items); return; }
            var dr = Add(nextId++, owner, aboard, pos, items);
            Net.DropAdded(dr);
        }

        // put what fits into the pack; anything left stays in the satchel (returns what was taken, by name)
        public static List<string> Into(Hands h, Dictionary<string, int> items, Dictionary<string, int> left)
        {
            var got = new List<string>();
            foreach (var kv in items)
            {
                var it = ItemDB.Get(kv.Key); if (it == null) continue;
                int over = h.pack.Add(it, kv.Value);
                if (kv.Value - over > 0) got.Add($"{kv.Value - over} {it.name}");
                if (over > 0) left[kv.Key] = over;
            }
            return got;
        }

        // E beside one (Diver.Uses asks)
        public bool Near(Diver d, bool e, ref string hint)
        {
            Drop best = null; float bd = 2.6f;
            var eye = d.EyeWorld;
            foreach (var dr in drops.Values) { float k = (World(dr) - eye).magnitude; if (k < bd) { bd = k; best = dr; } }
            if (best == null) return false;
            string whose = Net.I && best.owner == Net.I.myName ? "your" : best.owner + "'s";
            hint = $"E  Recover {whose} pack ({best.Count} things)";
            if (!e) return true;
            if (Net.IsGuest) { Net.DropTake(best.id); return true; }
            Recover(best.id, d.GetComponent<Hands>(), d);
            return true;
        }

        // the host: give a satchel's contents to a diver (this PC's, or a crewmate's through the network)
        public Dictionary<string, int> TakeAll(int id)
        {
            if (!drops.TryGetValue(id, out var dr)) return null;
            var items = dr.items; Remove(id); Net.DropRemoved(id);
            return items;
        }
        public void Recover(int id, Hands h, Diver d)
        {
            if (!drops.TryGetValue(id, out var dr)) return;
            var at = dr.pos; bool aboard = dr.aboard; string owner = dr.owner;
            var items = TakeAll(id);
            Give(items, h, d, owner, aboard, at);
        }
        public void Give(Dictionary<string, int> items, Hands h, Diver d, string owner, bool aboard, Vector3 at)
        {
            if (items == null || h == null) return;
            var left = new Dictionary<string, int>();
            var got = Into(h, items, left);
            d.Toast(got.Count > 0 ? "Recovered: " + string.Join(", ", got) + (left.Count > 0 ? "  (your pack is full: the rest stays)" : "") : "Your pack is full.");
            if (left.Count > 0) Make(owner, aboard, at, left);
        }

        // the HUD: a mark toward this diver's newest satchel
        void OnGUI()
        {
            if (!diver || Net.I == null) return;
            Drop mine = null;
            foreach (var dr in drops.Values) if (dr.owner == Net.I.myName && (mine == null || dr.id > mine.id)) mine = dr;
            if (mine == null) return;
            var cam = Camera.main; if (!cam) return;
            var w = World(mine);
            float dist = (w - cam.transform.position).magnitude;
            var sp = cam.WorldToScreenPoint(w + Vector3.up * 0.8f);
            var st = new GUIStyle(GUI.skin.label) { fontSize = 13, alignment = TextAnchor.MiddleCenter, fontStyle = FontStyle.Bold };
            st.normal.textColor = new Color(1f, 0.75f, 0.3f);
            string text = $"Your pack  {dist:0} m";
            if (sp.z > 0 && sp.x > 0 && sp.x < Screen.width && sp.y > 0 && sp.y < Screen.height)
                GUI.Label(new Rect(sp.x - 90, Screen.height - sp.y - 24, 180, 20), "▼ " + text, st);
            else
            {
                // off screen: along the top edge, toward it
                var local = cam.transform.InverseTransformDirection(w - cam.transform.position);
                float x = Mathf.Clamp(Screen.width / 2 + Mathf.Atan2(local.x, local.z) / Mathf.PI * Screen.width / 2, 60, Screen.width - 60);
                GUI.Label(new Rect(x - 90, 8, 180, 20), (local.x < 0 ? "◀ " : "") + text + (local.x >= 0 ? " ▶" : ""), st);
            }
        }
    }
}
