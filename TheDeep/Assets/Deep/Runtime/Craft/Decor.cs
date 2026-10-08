// Decorating the Nautilus (stage 7; the design doc's Artisan Bench and its cosmetics catalog). The bench stands with
// the forge (ItemDB.StationMatches): it turns salvage, organics, glowing flora and trophies into furniture, lighting
// and curios, made into the pack like anything else. Aboard, open the pack (Tab), choose a decoration and "Place it":
//   a ghost of it follows the eye (green where it can go, red where it can't: floor things on floors, wall things on
//   walls, ceiling things on ceilings, inside her rooms); R or the wheel turns it; the left button sets it down;
//   the right button or Esc puts it away.
// Aim at a placed decoration: X takes it down into the pack. Some do something (the catalog's notes):
//   the lights glow without her power (bioluminescence and chemistry), adding their light to the room;
//   the brass locker (+20) and the torpedo-tube locker (+40) add to her stores;
//   the bed and the hammock: E to rest - "Well Rested" for 20 minutes (a deeper breath: +10% air; slower hunger);
//   the Captain's Log desk: E to read the log (the creatures met, the days aboard, the deepest dive);
//   the steam gauge's needle shows her depth; the armillary sphere turns and glows with the water's warmth.
// The crew share it all: the host keeps the list (Net.cs carries placing and taking down) and the campaign saves it.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Decor : MonoBehaviour
    {
        public static Decor I;
        public enum Mount { Floor, Wall, Ceiling }
        public class Def { public string id; public Mount mount; public Color light; public float range; public int storage; public string use; public Bounds bounds; }
        public class Placed { public int id; public string item; public Vector3 local; public Quaternion rot; public Transform body, proxy; public Transform needle, spin; }

        static readonly Dictionary<string, Def> defs = new Dictionary<string, Def>();
        public readonly Dictionary<int, Placed> placed = new Dictionary<int, Placed>();
        public int nextId = 1;
        Nautilus ship; Diver diver;
        Material lit, glow, glass, ghostOk, ghostBad;
        public const int BaseStore = 48;

        // the catalog's mounts, lights, storage and uses (ids as in items.json)
        static void D(string id, Mount m, Color light = default, float range = 0, int storage = 0, string use = null)
            => defs[id] = new Def { id = id, mount = m, light = light, range = range, storage = storage, use = use };
        static Decor()
        {
            var blue = new Color(0.35f, 0.6f, 1f) * 1.6f; var green = new Color(0.35f, 1f, 0.75f) * 1.3f;
            D("leviathan_bone_display", Mount.Wall);
            D("bioluminescent_wall_planter", Mount.Wall, green, 4.5f);
            D("woven_kelp_rug", Mount.Floor);
            D("galleon_brass_porthole_frame", Mount.Wall);
            D("captains_log_desk", Mount.Floor, new Color(1f, 0.8f, 0.5f) * 1.2f, 2.5f, 0, "log");
            D("galleon_captains_bed", Mount.Floor, default, 0, 0, "rest");
            D("grazer_blubber_lounge_chair", Mount.Floor);
            D("dreadnought_brass_locker", Mount.Floor, default, 0, 20);
            D("abyssal_bone_table", Mount.Floor);
            D("phosphor_mat_chandelier", Mount.Ceiling, blue, 7f);
            D("lantern_vine_desk_lamp", Mount.Floor, new Color(1f, 0.82f, 0.45f) * 1.6f, 4f);
            D("shallows_algae_terrarium", Mount.Floor, new Color(0.4f, 1f, 0.5f) * 0.6f, 2f);
            D("beetle_chitin_sconce", Mount.Wall, new Color(0.45f, 1f, 0.35f) * 2f, 5f);
            D("reef_snapper_jaw_mount", Mount.Wall);
            D("suspended_stalker_hound_core", Mount.Floor, new Color(1f, 0.35f, 0.25f) * 1.2f, 3f);
            D("dreadnought_steam_gauge", Mount.Wall, default, 0, 0, "gauge");
            D("echo_ray_acoustic_chimes", Mount.Ceiling);
            D("dreadnought_torpedo_tube_locker", Mount.Floor, default, 0, 40);
            D("ancient_masonry_pedestal", Mount.Floor);
            D("galleon_map_board", Mount.Wall);
            D("captains_armillary_sphere", Mount.Floor, new Color(0.5f, 0.85f, 1f), 3f, 0, "armillary");
            D("jelly_bioluminescence_tube", Mount.Floor, new Color(0.6f, 0.45f, 1f) * 1.8f, 6f);
            D("iron_kelp_bonsai", Mount.Floor);
            D("thermal_coral_planter", Mount.Wall, new Color(1f, 0.3f, 0.2f), 3.5f);
            D("echo_ray_specimen_tank", Mount.Floor);
            D("restored_galleon_harpsichord", Mount.Floor);
            D("grazer_hide_hammock", Mount.Floor, default, 0, 0, "rest");
            D("abyssal_brew_keg", Mount.Floor);
            D("tangle_serpent_shed_scale", Mount.Wall);
            D("city_resonance_crystal", Mount.Floor, new Color(0.55f, 0.8f, 1f) * 1.4f, 4f);
            D("scavenger_hydraulic_pincer", Mount.Wall);
            D("dreadnought_helm_wheel", Mount.Wall);
        }

        public static Def DefOf(ItemDef it) => it != null && defs.TryGetValue(it.id, out var d) ? d : null;
        public static bool IsDecor(ItemDef it) => DefOf(it) != null;

        public static Decor Attach(Nautilus ship, Diver diver)
        {
            var d = new GameObject("Decor").AddComponent<Decor>(); I = d;
            d.ship = ship; d.diver = diver;
            d.lit = new Material(DeepShaders.Get("Deep/Lit")); d.lit.SetFloat("_UseVC", 1); d.lit.SetFloat("_VCAlbedo", 1); d.lit.SetFloat("_Roughness", 0.6f); d.lit.SetFloat("_Metallic", 0.25f); d.lit.SetFloat("_Cull", 0); d.lit.SetFloat("_Interior", 1); d.lit.renderQueue = 1960;
            d.glow = new Material(d.lit); d.glow.SetFloat("_Glow", 1.4f);   // (its own light, not her power's)
            d.glass = new Material(DeepShaders.Get("Deep/Glass")); d.glass.SetColor("_Tint", new Color(0.7f, 0.85f, 0.85f)); d.glass.SetFloat("_Clear", 0.85f);
            d.ghostOk = new Material(DeepShaders.Get("Deep/Glass")); d.ghostOk.SetColor("_Tint", new Color(0.3f, 1f, 0.5f)); d.ghostOk.SetFloat("_Clear", 0.35f);
            d.ghostBad = new Material(DeepShaders.Get("Deep/Glass")); d.ghostBad.SetColor("_Tint", new Color(1f, 0.3f, 0.25f)); d.ghostBad.SetFloat("_Clear", 0.35f);
            foreach (var def in defs.Values) def.bounds = Bounds(def.id);
            return d;
        }

        static Bounds Bounds(string id)
        {
            Bounds b = default; bool any = false;
            foreach (var suf in new[] { "", "_glass", "_glow" })
            {
                var m = ModelLibrary.Get("Decor/decor", id + suf); if (!m) continue;
                if (!any) { b = m.bounds; any = true; } else b.Encapsulate(m.bounds);
            }
            return any ? b : new Bounds(Vector3.up * 0.4f, Vector3.one * 0.8f);
        }

        // the model's parts under a parent (base, glass, glow)
        Transform Build(string id, Transform parent, Material over = null)
        {
            var root = new GameObject(id).transform; root.SetParent(parent, false);
            void Part(string suf, Material mat)
            {
                var m = ModelLibrary.Get("Decor/decor", id + suf); if (!m) return;
                var g = new GameObject(id + suf); g.transform.SetParent(root, false);
                g.AddComponent<MeshFilter>().sharedMesh = m;
                var r = g.AddComponent<MeshRenderer>(); r.sharedMaterial = over ? over : mat;
                r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            }
            Part("", lit); Part("_glass", glass); Part("_glow", glow);
            return root;
        }

        // ---- placing and taking down ----------------------------------------------------------------------------
        public Placed Add(int id, string item, Vector3 local, Quaternion rot)
        {
            if (placed.ContainsKey(id)) Remove(id);
            var p = new Placed { id = id, item = item, local = local, rot = rot };
            p.body = Build(item, ship.Body); p.body.localPosition = local; p.body.localRotation = rot;
            // a box in her proxy, so it stands in the way and the eye can find it
            var px = new GameObject("decor " + id); px.transform.SetParent(ship.Proxy, false);
            px.transform.localPosition = local; px.transform.localRotation = rot;
            var def = defs.TryGetValue(item, out var dd) ? dd : null;
            var bc = px.AddComponent<BoxCollider>();
            var bb = def != null ? def.bounds : new Bounds(Vector3.up * 0.4f, Vector3.one * 0.8f);
            bc.center = bb.center; bc.size = Vector3.Max(bb.size, Vector3.one * 0.12f);
            if (item == "woven_kelp_rug") bc.size = new Vector3(bc.size.x, 0.02f, bc.size.z);   // (walked over)
            px.AddComponent<DecorRef>().id = id;
            p.proxy = px.transform;
            if (item == "dreadnought_steam_gauge")
            {
                var nm = ModelLibrary.Get("Decor/decor", "dreadnought_steam_gauge_needle");
                if (nm)
                {
                    var g = new GameObject("needle"); g.transform.SetParent(p.body, false); g.transform.localPosition = new Vector3(0, 1.5f, 0.1f);
                    g.AddComponent<MeshFilter>().sharedMesh = nm; g.AddComponent<MeshRenderer>().sharedMaterial = lit;
                    p.needle = g.transform;
                }
            }
            if (item == "captains_armillary_sphere") p.spin = p.body;
            placed[id] = p;
            nextId = Mathf.Max(nextId, id + 1);
            Restow();
            return p;
        }

        public void Remove(int id)
        {
            if (!placed.TryGetValue(id, out var p)) return;
            if (p.body) Destroy(p.body.gameObject);
            if (p.proxy) Destroy(p.proxy.gameObject);
            placed.Remove(id);
            Restow();
        }

        // her stores grow with the lockers placed aboard
        public int StoreBonus { get { int k = 0; foreach (var p in placed.Values) if (defs.TryGetValue(p.item, out var d)) k += d.storage; return k; } }
        void Restow()
        {
            var inv = ship.store; int want = BaseStore + StoreBonus;
            while (inv.slots.Count < want) inv.slots.Add(new Inventory.Slot());
            // (shrinking: only empty slots at the end go)
            for (int i = inv.slots.Count - 1; i >= want && inv.slots[i].count == 0; i--) inv.slots.RemoveAt(i);
            inv.size = inv.slots.Count;
        }

        // ---- the ghost while placing ------------------------------------------------------------------------------
        string ghostHint = "";
        ItemDef placing; Transform ghost; float ghostYaw; bool ghostOkNow; Vector3 ghostLocal; Quaternion ghostRot; string ghostWhy;
        public bool Placing => placing != null;

        public void BeginPlace(ItemDef it)
        {
            CancelPlace();
            if (!diver.aboard) { diver.Toast("Decorations go aboard the Nautilus."); return; }
            placing = it; ghostYaw = 0;
            ghost = Build(it.id, ship.Body, ghostOk);
            diver.Toast($"Placing the {it.name}: LMB set it down, R or the wheel turns it, RMB cancels");
        }
        public void CancelPlace() { placing = null; if (ghost) Destroy(ghost.gameObject); ghost = null; }

        void UpdateGhost()
        {
            if (!diver.aboard || diver.uiOpen) { CancelPlace(); return; }
            var def = defs[placing.id];
            var o = diver.head.position; var dir = diver.head.forward;
            ghostOkNow = false; ghostWhy = null;
            if (Input.GetKeyDown(KeyCode.R)) ghostYaw += 15f;
            ghostYaw += Input.mouseScrollDelta.y * 15f;
            if (Physics.Raycast(o, dir, out var hit, 4.5f, ~0, QueryTriggerInteraction.Ignore) && hit.collider.transform.IsChildOf(ship.Proxy))
            {
                var n = ship.Proxy.InverseTransformDirection(hit.normal);
                var local = ship.Proxy.InverseTransformPoint(hit.point);
                Mount at = n.y > 0.7f ? Mount.Floor : n.y < -0.7f ? Mount.Ceiling : Mount.Wall;
                if (at == Mount.Wall)
                {
                    // wall things are drawn from the floor up: their origin is on the wall at the floor below the aim
                    var flat = new Vector3(n.x, 0, n.z).normalized; ghostRot = Quaternion.LookRotation(flat, Vector3.up);
                    local.y = Physics.Raycast(hit.point + hit.normal * 0.25f, Vector3.down, out var fh, 4f, ~0, QueryTriggerInteraction.Ignore) ? ship.Proxy.InverseTransformPoint(fh.point).y : Nautilus.Floor;
                    local += flat * 0.02f;
                }
                else ghostRot = Quaternion.Euler(0, ghostYaw, 0);
                ghostLocal = local;
                var gen = Nautilus.FromLocal(local + Vector3.up * 0.5f);
                bool inside = ship.sys != null && ship.sys.RoomIndexAt(gen) >= 0;
                bool onDecor = hit.collider.GetComponent<DecorRef>() != null;
                if (at != def.mount) ghostWhy = def.mount == Mount.Floor ? "it stands on a floor" : def.mount == Mount.Wall ? "it hangs on a wall" : "it hangs from a ceiling";
                else if (!inside) ghostWhy = "not there (inside her rooms)";
                else if (onDecor && def.mount != Mount.Floor) ghostWhy = "something is there";
                else ghostOkNow = true;
            }
            else { ghostLocal = ship.Proxy.InverseTransformPoint(o + dir * 2.5f); ghostRot = Quaternion.Euler(0, ghostYaw, 0); ghostWhy = "aim at a floor, a wall or a ceiling"; }
            ghost.localPosition = ghostLocal; ghost.localRotation = ghostRot;
            foreach (var r in ghost.GetComponentsInChildren<MeshRenderer>()) r.sharedMaterial = ghostOkNow ? ghostOk : ghostBad;
            ghostHint = ghostOkNow ? $"LMB  Set down the {placing.name}    R turn    RMB cancel" : $"The {placing.name}: {ghostWhy}";
            if (Input.GetMouseButtonDown(1) || Input.GetKeyDown(KeyCode.Escape)) { CancelPlace(); return; }
            if (ghostOkNow && Input.GetMouseButtonDown(0))
            {
                var hands = diver.GetComponent<Hands>();
                if (hands == null || !hands.pack.Remove(placing)) { diver.Toast("It isn't in your pack any more."); CancelPlace(); return; }
                if (Net.IsGuest) Net.DecorPlace(placing.id, ghostLocal, ghostRot);
                else { var p = Add(nextId++, placing.id, ghostLocal, ghostRot); Net.DecorAdded(p); }
                diver.Toast($"The {placing.name} is set down.");
                Sfx.Play("place", ship.WorldPoint(ghostLocal), 1f, 1f, Medium.Aboard);
                CancelPlace();
            }
        }

        // ---- aiming at one (Diver.Uses asks first, aboard) -----------------------------------------------------------
        float logShow;
        public void ShowLog() { logShow = 30f; }
        public bool Aimed(Diver d, bool e, ref string hint)
        {
            if (Placing) { hint = ghostHint; return true; }
            if (!Physics.Raycast(d.head.position, d.head.forward, out var hit, 2.8f, ~0, QueryTriggerInteraction.Ignore)) return false;
            var dr = hit.collider.GetComponent<DecorRef>(); if (dr == null || !placed.TryGetValue(dr.id, out var p)) return false;
            var it = ItemDB.Get(p.item); var def = defs.TryGetValue(p.item, out var dd) ? dd : null;
            string name = it != null ? it.name : p.item;
            string use = def?.use == "rest" ? "E  Rest    " : def?.use == "log" ? "E  Read the captain's log    " : "";
            hint = $"{use}X  Take down the {name}";
            if (e && def?.use == "rest") d.GetComponent<Survival>()?.Rest();
            if (e && def?.use == "log") logShow = logShow > 0 ? 0 : 30f;
            if (d.inputEnabled && !d.uiOpen && Input.GetKeyDown(KeyCode.X))
            {
                if (def != null && def.storage > 0 && FreeStoreSlots() < def.storage) { d.Toast("Empty that many of her lockers first."); return true; }
                var hands = d.GetComponent<Hands>();
                if (it == null || hands == null || hands.pack.Add(it) > 0) { d.Toast("Your pack is full."); return true; }
                if (Net.IsGuest) Net.DecorTake(p.id);
                else { Remove(p.id); Net.DecorRemoved(p.id); }
                d.Toast($"You take down the {name}.");
                Sfx.Play("place", ship.WorldPoint(p.local), 0.7f, 1.2f, Medium.Aboard);
            }
            return true;
        }
        int FreeStoreSlots() { int k = 0; foreach (var s in ship.store.slots) if (s.count == 0) k++; return k; }

        // ---- every frame --------------------------------------------------------------------------------------------
        void Update()
        {
            if (Placing) UpdateGhost();
            if (logShow > 0) logShow -= Time.deltaTime;
            foreach (var p in placed.Values)
            {
                if (p.needle) p.needle.localRotation = Quaternion.Euler(0, 0, Mathf.Lerp(112f, -112f, Mathf.Clamp01(ship.Depth / Mathf.Max(30f, ship.crushDepth))) + Mathf.Sin(Time.time * 7f) * 0.6f);
                if (p.spin) foreach (Transform c in p.spin) if (c.name.EndsWith("_glow")) c.localRotation = Quaternion.Euler(0, Time.time * 20f, 0);
            }
        }

        // the decorations' lights, for her lamp list (Nautilus.cs): local point, colour, range
        public IEnumerable<(Vector3 local, Color col, float range)> Lights()
        {
            foreach (var p in placed.Values)
            {
                if (!defs.TryGetValue(p.item, out var d) || d.range <= 0) continue;
                var col = d.light;
                if (d.use == "armillary") col = Color.Lerp(new Color(0.35f, 0.6f, 1f), new Color(1f, 0.55f, 0.3f), Mathf.Clamp01((Survival.WaterC(ship.Depth, 1f) - 15f) / 12f));
                var c = p.rot * d.bounds.center; c.y = Mathf.Max(c.y, 0.3f);
                yield return (p.local + c, col, d.range);
            }
        }

        // ---- the captain's log --------------------------------------------------------------------------------------
        void OnGUI()
        {
            if (logShow <= 0 || Campaign.I == null) return;
            var c = Campaign.I;
            var r = new Rect(Screen.width / 2 - 320, Screen.height / 2 - 230, 640, 460);
            GUI.color = new Color(0.12f, 0.09f, 0.05f, 0.92f); GUI.DrawTexture(r, Texture2D.whiteTexture); GUI.color = Color.white;
            var t = new GUIStyle(GUI.skin.label) { fontSize = 20, fontStyle = FontStyle.Bold }; t.normal.textColor = new Color(0.95f, 0.85f, 0.6f);
            var s = new GUIStyle(GUI.skin.label) { fontSize = 13, wordWrap = true }; s.normal.textColor = new Color(0.92f, 0.88f, 0.78f);
            GUI.Label(new Rect(r.x + 20, r.y + 14, r.width - 40, 28), "The Captain's Log", t);
            var clock = DeepBoot.I.clock;
            GUI.Label(new Rect(r.x + 20, r.y + 48, r.width - 40, 40), $"Day {clock.day + 1} of the voyage. Deepest dive: {c.deepest:0} m. Her crush depth: {ship.crushDepth:0} m. Creatures recorded: {c.met.Count} of {SpeciesBook.All.Count}.", s);
            var names = new List<string>(c.met); names.Sort();
            GUI.Label(new Rect(r.x + 20, r.y + 92, r.width - 40, r.height - 120), names.Count == 0 ? "No creatures recorded yet. Get close to them in the water." : string.Join(",  ", names), s);
            GUI.Label(new Rect(r.x + 20, r.yMax - 26, r.width - 40, 22), "E to close", s);
        }
    }

    public class DecorRef : MonoBehaviour { public int id; }
}
