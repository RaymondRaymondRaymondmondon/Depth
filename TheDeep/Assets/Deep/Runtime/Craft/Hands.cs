// The diver's hands (stage 4): the hotbar (1-5 picks a tool or weapon from the pack, 0 empties the hands), what's in
// them drawn in front of the eye, and the left mouse button using it on whatever the eye is on:
//   - a plant: gathered if the tool suits its tier (the design doc: tier 1 by hand or knife; tier 2 the Heated Blade
//     for soft and woody growth - Iron-Kelp - or the Starter Drill for hard growth, coral and ore; tier 3 needs the
//     Mid-Tier Drill or Heated Blade Mk II), into the pack as the plant's yield;
//   - an animal: the knife and the blade strike (the blade cauterises: less blood), the spear gun fires a spear
//     (15 m); every wound spills blood and noise (the Wake), and a kill gives up the animal's yield and its meat.
// The tools' noise (the doc's table): the knife near silent, the blades a low hiss, the Starter Drill 70 dB mid-band.
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Hands : MonoBehaviour
    {
        public Diver d;
        public Inventory pack = new Inventory(24);
        public int held = -1;                 // the pack slot in hand (-1: bare hands)
        public string aimHint = "";
        public float useT, cool;              // the swing or the drill's progress, and the cool-down
        Transform view; GameObject viewTool; string viewFor;
        readonly Dictionary<string, BiomeFlora> floraByKind = new Dictionary<string, BiomeFlora>();
        class BiomeFlora { public FloraEntry e; public string biome; }

        public ItemDef Held => held >= 0 && held < pack.slots.Count && pack.slots[held].count > 0 ? ItemDB.Get(pack.slots[held].id) : null;
        public static string ToolOf(ItemDef it)
        {
            if (it == null) return "hand";
            string n = it.name.ToLowerInvariant();
            if (n.Contains("heated blade")) return "blade";
            if (n.Contains("drill")) return "drill";
            if (n.Contains("spear")) return "spear";
            if (n.Contains("knife")) return "knife";
            return "hand";
        }

        public static Hands Attach(Diver d)
        {
            ItemDB.Load();
            var h = d.gameObject.AddComponent<Hands>();
            h.d = d;
            foreach (var t in SpeciesBook.Tables)
                if (t.flora != null) foreach (var f in t.flora) h.floraByKind[f.id] = new BiomeFlora { e = f, biome = t.biome };
            // the start (the doc): the Survival Knife
            var knife = ItemDB.Get("Survival Knife");
            if (knife != null) { h.pack.Add(knife); h.held = 0; }
            h.view = new GameObject("Held").transform;
            return h;
        }

        // the flora table entry for a planted kind ("kelp" is the Iron-Kelp)
        FloraEntry Entry(string kind)
        {
            if (kind == "kelp") { foreach (var kv in floraByKind) if (kv.Value.e.name == "Iron-Kelp") return kv.Value.e; return null; }
            return floraByKind.TryGetValue(kind, out var b) ? b.e : null;
        }

        // what the tool in hand takes from a source (from the resource table); `why` names the tool that's needed
        // when it takes nothing
        List<ItemDef> Takes(string source, string tool, out string why)
        {
            var all = ItemDB.From(source);
            var got = new List<ItemDef>();
            foreach (var r in all) if (ItemDB.ToolTakes(r, tool)) got.Add(r);
            why = got.Count > 0 || all.Count == 0 ? null : all[0].tool;
            return got;
        }

        void Update()
        {
            if (!d || d.aboard || !d.inputEnabled) { aimHint = ""; SetView(null); return; }
            for (int k = 0; k < 6; k++)
                if (Input.GetKeyDown(KeyCode.Alpha0 + k)) held = k == 0 ? -1 : HotbarSlot(k - 1);
            var it = Held; string tool = ToolOf(it);
            SetView(tool == "hand" ? null : tool);
            cool -= Time.deltaTime;

            var cam = d.cam.transform; var o = cam.position; var dir = cam.forward;
            var flora = DeepBoot.I ? DeepBoot.I.flora : null;
            var life = Life.I;
            aimHint = "";
            float reach = tool == "spear" ? 15f : 2.2f;
            var prey = life != null ? life.PickCreature(o, dir, reach) : null;
            Flora.Picked plant = default; bool hasPlant = flora && flora.Pick(o, dir, 2.6f, k => Entry(k) != null, out plant);
            if (hasPlant && prey != null && Vector3.Distance(o, prey.pos) < Vector3.Distance(o, plant.pos)) hasPlant = false;
            var dep = Deposits.I != null ? Deposits.I.Pick(o, dir, 2.8f) : null;
            if (dep != null && hasPlant && Vector3.Distance(o, dep.pos) > Vector3.Distance(o, plant.pos)) dep = null;
            if (dep != null) { hasPlant = false; prey = null; }
            bool locked = Cursor.lockState == CursorLockMode.Locked;
            bool fire = locked && Input.GetMouseButton(0);
            bool click = locked && Input.GetMouseButtonDown(0);

            if (dep != null)
            {
                var item = ItemDB.Get(dep.item);
                bool works = Deposits.Works(dep, tool, out string need);
                string what = dep.by == "hand" ? "A loose chunk of titanium ore" : dep.by == "drill" ? "A titanium ore node" : $"{dep.item} ({dep.where})";
                aimHint = works ? $"{what}  (hold LMB: take it)" : $"{what}  - needs {need}";
                if (works && fire)
                {
                    float need2 = tool == "drill" ? 2.2f : dep.by == "hand" ? 0.5f : 1.4f;
                    useT += Time.deltaTime / need2;
                    if (tool == "drill" && life != null && Time.frameCount % 15 == 0) life.sound.Emit(dep.pos, 70f, Band.Mid, 0.3f, "a drill");
                    if (useT >= 1f)
                    {
                        useT = 0;
                        if (item == null) d.Toast(dep.item + ": unknown item");
                        else if (pack.Add(item, dep.yield) > 0) d.Toast("Your pack is full.");
                        else { d.Toast($"+{dep.yield} {item.name}"); Deposits.I.Take(dep); Sfx.Shared("gather_ore", dep.pos, 1f, 1f); Sfx.UI("pickup"); }
                    }
                }
                else useT = 0;
                return;
            }
            if (hasPlant)
            {
                var f = Entry(plant.kind);
                var yields = Takes(f.name, tool, out string why);
                if (yields.Count == 0 && why == null) { var y0 = ItemDB.Get(f.yields); if (y0 != null && f.tier <= 1) yields.Add(y0); }
                aimHint = yields.Count > 0 ? $"{f.name}  (hold LMB: gather {string.Join(", ", yields.ConvertAll(y => y.name))})"
                        : why != null ? $"{f.name}  - needs: {why}" : f.name;
                if (yields.Count > 0 && fire)
                {
                    float need = tool == "drill" ? 1.6f : tool == "blade" ? 1.0f : tool == "knife" ? 0.7f : 1.1f;
                    useT += Time.deltaTime / need;
                    if (tool == "drill" && life != null && Time.frameCount % 15 == 0) life.sound.Emit(plant.pos, 70f, Band.Mid, 0.3f, "a drill");
                    if (tool == "blade" && life != null && Time.frameCount % 20 == 0) life.sound.Emit(plant.pos, 35f, Band.High, 0.3f, "a heated blade's hiss");
                    if (useT >= 1f)
                    {
                        useT = 0;
                        int n = plant.kind == "kelp" ? 2 : Mathf.Clamp(Mathf.RoundToInt(plant.scale * 1.2f), 1, 2);
                        var got = new List<string>(); bool full = false;
                        foreach (var y in yields) { if (pack.Add(y, n) > 0) full = true; else got.Add($"+{n} {y.name}"); }
                        if (got.Count > 0) { d.Toast(string.Join("  ", got)); flora.Take(plant, plant.kind == "kelp" ? 900f : 420f); Sfx.Shared("gather_plant", plant.pos, 1f, Random.Range(0.85f, 1.15f)); Sfx.UI("pickup"); }
                        if (full) d.Toast("Your pack is full.");
                    }
                }
                else useT = 0;
                return;
            }
            useT = 0;
            if (prey != null)
            {
                aimHint = prey.sp.e.name + (tool == "hand" ? "" : $"  (LMB: {(tool == "spear" ? "fire" : "strike")})");
                if (click && cool <= 0 && tool != "hand" && tool != "drill")
                {
                    float dmg = tool == "knife" ? 12f : tool == "blade" ? 20f : 34f;
                    cool = tool == "spear" ? 1.4f : 0.55f;
                    swing = 1f;
                    if (tool == "spear") life.sound.Emit(o, 60f, Band.Mid, 0.4f, "a speargun's thwack");
                    Sfx.Shared(tool == "spear" ? "spear_fire" : "knife_swing", o, 1f, 1f);
                    Sfx.Shared(tool == "spear" ? "spear_hit" : "knife_hit", prey.pos, 1f, Random.Range(0.9f, 1.1f));
                    bool dead = life.Wound(prey, dmg, o, tool == "blade", tool == "spear" ? "a spear strike" : "a blade strike");
                    if (dead) Loot(prey);
                    else d.Toast($"You wound the {prey.sp.e.name}.");
                }
            }
            else if (click && cool <= 0 && tool != "hand") { cool = 0.4f; swing = 1f; Sfx.Shared(tool == "spear" ? "spear_fire" : "knife_swing", o, 0.8f, 1f); if (tool == "spear") { life?.sound.Emit(o, 60f, Band.Mid, 0.4f, "a speargun's thwack"); cool = 1.4f; } }
        }

        // a kill gives up what the resource table says the animal gives (its meat, a jaw, blubber...)
        void Loot(Creature c)
        {
            var got = new List<string>();
            var parts = ItemDB.From(c.sp.e.name);
            if (parts.Count == 0) { var y = ItemDB.Get(c.sp.e.yields); if (y != null) parts.Add(y); }
            var meat = ItemDB.Get("Raw " + c.sp.e.name);
            if (meat != null && !parts.Contains(meat)) parts.Add(meat);
            foreach (var p in parts) if (pack.Add(p) == 0) got.Add(p.name);
            d.Toast(got.Count > 0 ? $"The {c.sp.e.name} is dead. +{string.Join(", +", got)}" : $"The {c.sp.e.name} is dead.");
        }

        // hotbar key k (0-4): the k-th tool or weapon in the pack
        int HotbarSlot(int k)
        {
            int n = 0;
            for (int i = 0; i < pack.slots.Count; i++)
            {
                var s = pack.slots[i]; if (s.count == 0) continue;
                var it = ItemDB.Get(s.id);
                if (it == null || (it.category != "tool" && it.category != "weapon")) continue;
                if (n++ == k) return i;
            }
            return held;
        }
        public List<int> Hotbar()
        {
            var l = new List<int>();
            for (int i = 0; i < pack.slots.Count && l.Count < 5; i++)
            {
                var s = pack.slots[i]; if (s.count == 0) continue;
                var it = ItemDB.Get(s.id);
                if (it != null && (it.category == "tool" || it.category == "weapon")) l.Add(i);
            }
            return l;
        }

        // ---- the tool in front of the eye ------------------------------------------------------------------------
        float swing;
        void SetView(string tool)
        {
            if (viewFor != tool)
            {
                if (viewTool) Destroy(viewTool);
                viewTool = tool == null ? null : BuildView(tool);
                viewFor = tool;
            }
        }

        void LateUpdate()
        {
            if (!viewTool || !d) return;
            var c = d.cam.transform;
            swing = Mathf.MoveTowards(swing, 0, Time.deltaTime * 3.5f);
            float drillShake = viewFor == "drill" && useT > 0 ? Mathf.Sin(Time.time * 70f) * 0.004f : 0;
            float bob = Mathf.Sin(Time.time * 2.2f) * 0.006f;
            var local = new Vector3(0.24f, -0.22f + bob + drillShake, 0.42f);
            var rot = Quaternion.Euler(-swing * 55f * (viewFor == "spear" ? 0.2f : 1f), -8f + swing * 25f, swing * -30f);
            if (viewFor == "spear") local.z -= swing * 0.08f;
            view.SetPositionAndRotation(c.TransformPoint(local), c.rotation * rot);
            viewTool.transform.SetPositionAndRotation(view.position, view.rotation);
            if (viewFor == "drill" && useT > 0) { var bit = viewTool.transform.Find("bit"); if (bit) bit.Rotate(0, 0, 900f * Time.deltaTime, Space.Self); }
        }

        static Material ToolMat(Color c, float metal, float rough, float emit = 0)
        {
            var m = new Material(Shader.Find("Deep/Lit"));
            m.SetColor("_BaseColor", c); m.SetFloat("_Metallic", metal); m.SetFloat("_Roughness", rough);
            m.SetFloat("_UseVC", 0); m.SetFloat("_Cull", 0); m.SetFloat("_Emit", emit);
            m.renderQueue = 2400;
            return m;
        }
        static GameObject Prim(PrimitiveType t, Transform p, Vector3 pos, Vector3 euler, Vector3 scale, Material m, string name = null)
        {
            var g = GameObject.CreatePrimitive(t); Object.Destroy(g.GetComponent<Collider>());
            g.transform.SetParent(p, false); g.transform.localPosition = pos; g.transform.localRotation = Quaternion.Euler(euler); g.transform.localScale = scale;
            var r = g.GetComponent<MeshRenderer>(); r.sharedMaterial = m; r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            if (name != null) g.name = name;
            return g;
        }

        // the tools, built in code for now (the Blender pass with the vehicles): a knife, a glowing blade, a drill, a gun
        GameObject BuildView(string tool)
        {
            var root = new GameObject("tool " + tool);
            var t = root.transform;
            var grip = ToolMat(new Color(0.12f, 0.1f, 0.08f), 0.1f, 0.7f);
            var steel = ToolMat(new Color(0.7f, 0.72f, 0.74f), 0.9f, 0.3f);
            var brass = ToolMat(new Color(0.62f, 0.45f, 0.19f), 0.85f, 0.35f);
            var yellow = ToolMat(new Color(0.85f, 0.62f, 0.12f), 0.2f, 0.5f);
            switch (tool)
            {
                case "knife":
                    Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0, -0.02f), new Vector3(90, 0, 0), new Vector3(0.028f, 0.06f, 0.028f), grip);
                    Prim(PrimitiveType.Cube, t, new Vector3(0, 0, 0.045f), Vector3.zero, new Vector3(0.05f, 0.012f, 0.012f), brass);
                    Prim(PrimitiveType.Cube, t, new Vector3(0, 0.004f, 0.13f), Vector3.zero, new Vector3(0.006f, 0.032f, 0.16f), steel);
                    break;
                case "blade":
                    Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0, -0.02f), new Vector3(90, 0, 0), new Vector3(0.032f, 0.07f, 0.032f), grip);
                    Prim(PrimitiveType.Cube, t, new Vector3(0, 0, 0.05f), Vector3.zero, new Vector3(0.06f, 0.03f, 0.02f), brass);
                    var hot = ToolMat(new Color(1f, 0.42f, 0.12f), 0.6f, 0.3f, 1.2f);
                    Prim(PrimitiveType.Cube, t, new Vector3(0, 0.004f, 0.17f), Vector3.zero, new Vector3(0.008f, 0.036f, 0.24f), hot);
                    break;
                case "drill":
                    Prim(PrimitiveType.Cube, t, new Vector3(0, -0.06f, -0.03f), new Vector3(15, 0, 0), new Vector3(0.04f, 0.1f, 0.05f), grip);
                    Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0, 0.05f), new Vector3(90, 0, 0), new Vector3(0.08f, 0.09f, 0.08f), yellow);
                    var bit = Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0, 0.17f), new Vector3(90, 0, 0), new Vector3(0.025f, 0.05f, 0.025f), steel, "bit");
                    Prim(PrimitiveType.Cube, bit.transform, new Vector3(0, 0.9f, 0), new Vector3(0, 45, 0), new Vector3(1.1f, 0.6f, 0.2f), steel);
                    break;
                case "spear":
                    Prim(PrimitiveType.Cube, t, new Vector3(0, -0.05f, -0.06f), new Vector3(15, 0, 0), new Vector3(0.035f, 0.09f, 0.05f), grip);
                    Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0, 0.12f), new Vector3(90, 0, 0), new Vector3(0.035f, 0.22f, 0.035f), steel);
                    Prim(PrimitiveType.Cylinder, t, new Vector3(0, 0.03f, 0.17f), new Vector3(90, 0, 0), new Vector3(0.008f, 0.3f, 0.008f), brass);
                    Prim(PrimitiveType.Cube, t, new Vector3(0, 0.03f, 0.48f), new Vector3(0, 0, 45), new Vector3(0.012f, 0.012f, 0.04f), steel);
                    break;
            }
            return root;
        }
    }
}
