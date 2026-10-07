// Items, recipes and inventories (stage 4; the design doc's "Crafting, resources, and upgrades" appendix, transcribed
// into Resources/Data/items.json). Every named resource and crafted thing is an ItemDef; a recipe is an item's
// ingredients and the station that makes it. Inventories are slot lists with stacks.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    [Serializable] public class Ingredient { public string item; public int count = 1; }
    [Serializable] public class ItemEffects { public float hunger, thirst, health, oxygen; public string other; }
    [Serializable]
    public class ItemEntry
    {
        public string id, name, category, station, stats, description;
        public int phase;
        public Ingredient[] ingredients;
        public ItemEffects effects;
    }
    [Serializable]
    public class ResourceEntry { public string id, name, kind, biome, source, tool, use; public int tier; }
    [Serializable] public class HullUpgradeEntry { public int phase; public string name; public float crushDepthM; public Ingredient[] ingredients; }
    [Serializable] public class StationEntry { public string name, room, does; }
    [Serializable] public class MeterEntry { public string id, name, notes; public float max, drainPerMin; }
    [Serializable] public class SurvivalEntry { public string[] notes; public MeterEntry[] meters; }
    [Serializable]
    public class ItemsFile
    {
        public SurvivalEntry survival;
        public ResourceEntry[] resources;
        public ItemEntry[] items;
        public HullUpgradeEntry[] hullUpgrades;
        public StationEntry[] stations;
    }

    public class ItemDef
    {
        public string id, name, category, station, stats, description, tool, source, biome;
        public int phase, tier;
        public bool resource;
        public List<(ItemDef item, int count)> recipe = new List<(ItemDef, int)>();
        public ItemEffects effects;
        public int MaxStack => category == "tool" || category == "weapon" || category == "equipment" || category == "vehicle" || category == "hull_upgrade" ? 1 : 20;
        public bool Raw => name.StartsWith("Raw ");
        public bool Edible => Raw || (effects != null && (effects.hunger != 0 || effects.thirst != 0 || effects.health != 0 || effects.oxygen != 0));
        public override string ToString() => name;
    }

    public static class ItemDB
    {
        public static readonly Dictionary<string, ItemDef> ById = new Dictionary<string, ItemDef>();
        public static readonly Dictionary<string, ItemDef> ByName = new Dictionary<string, ItemDef>(StringComparer.OrdinalIgnoreCase);
        public static ItemsFile File;
        public static List<HullUpgradeEntry> Hull = new List<HullUpgradeEntry>();

        public static string Slug(string name)
        {
            var sb = new System.Text.StringBuilder();
            foreach (char ch in name.ToLowerInvariant()) sb.Append(char.IsLetterOrDigit(ch) ? ch : '_');
            return sb.ToString().Trim('_').Replace("__", "_");
        }

        public static void Load()
        {
            if (File != null) return;
            var t = Resources.Load<TextAsset>("Data/items");
            File = t ? JsonUtility.FromJson<ItemsFile>(t.text) : new ItemsFile();
            if (File.resources != null)
                foreach (var r in File.resources)
                    Add(new ItemDef { id = r.id ?? Slug(r.name), name = r.name, category = "resource", resource = true, tier = r.tier, tool = r.tool, source = r.source, biome = r.biome, description = r.use });
            if (File.items != null)
                foreach (var e in File.items)
                    Add(new ItemDef { id = e.id ?? Slug(e.name), name = e.name, category = e.category ?? "other", station = e.station, stats = e.stats, description = e.description, phase = e.phase, effects = e.effects });
            if (File.items != null)
                foreach (var e in File.items)
                {
                    var d = Get(e.name); if (d == null || e.ingredients == null) continue;
                    foreach (var ing in e.ingredients)
                    {
                        var ii = Get(ing.item) ?? Add(new ItemDef { id = Slug(ing.item), name = ing.item, category = "resource", resource = true });
                        d.recipe.Add((ii, Mathf.Max(1, ing.count)));
                    }
                }
            if (File.hullUpgrades != null) Hull.AddRange(File.hullUpgrades);
            Debug.Log($"DEEP ITEMS: {ById.Count} items and resources, {Hull.Count} hull upgrades");
        }

        static ItemDef Add(ItemDef d)
        {
            if (ByName.TryGetValue(d.name, out var have)) return have;
            ById[d.id] = d; ByName[d.name] = d;
            return d;
        }

        public static ItemDef Get(string nameOrId)
        {
            if (string.IsNullOrEmpty(nameOrId)) return null;
            if (ByName.TryGetValue(nameOrId, out var d)) return d;
            return ById.TryGetValue(nameOrId, out d) ? d : ById.TryGetValue(Slug(nameOrId), out d) ? d : null;
        }

        // the resources a named source gives (a plant, an animal, a deposit, a wreck): every resource whose "source"
        // names it (Iron-Kelp gives its stalk, its fronds and its fibres; Coral Algae its calcite flakes)
        public static List<ItemDef> From(string sourceName)
        {
            var list = new List<ItemDef>();
            if (string.IsNullOrEmpty(sourceName)) return list;
            string n = sourceName.ToLowerInvariant();
            foreach (var d in ById.Values)
            {
                if (!d.resource || string.IsNullOrEmpty(d.source)) continue;
                string src = d.source.ToLowerInvariant();
                if (src == n || src.StartsWith(n + " ") || src.StartsWith(n + ",") || src.StartsWith(n + " (") || src.Contains(n + " mats") || src.Contains(n + " beds")) list.Add(d);
            }
            return list;
        }

        // can this tool take the resource? (the doc's tool column: "by hand", the Survival Knife, the Starter Drill, the
        // Heated Blade, "Mid-Tier Drill or Heated Blade Mk II", "Spear Gun, traps", "Hunting")
        public static bool ToolTakes(ItemDef r, string tool)
        {
            string t = (r.tool ?? "").ToLowerInvariant();
            if (t.Contains("mid-tier") && !t.Contains("starter") && !t.StartsWith("heated blade")) return false;
            if (t.Contains("mining laser")) return false;
            if (tool == "hand") return t.Contains("by hand") || t.Contains("collected");
            if (tool == "knife") return t.Contains("knife") || t.Contains("by hand") || t.Contains("collected") || t.Contains("hunting");
            if (tool == "blade") return t.Contains("heated blade") || t.Contains("knife") || t.Contains("by hand") || t.Contains("hunting");
            if (tool == "drill") return t.Contains("drill");
            if (tool == "spear") return t.Contains("spear") || t.Contains("hunting") || t.Contains("trap");
            return false;
        }

        // the recipes a station makes (matched loosely: "Fabrication Bay", "the fabricator"...)
        public static List<ItemDef> MadeAt(string stationKind)
        {
            var list = new List<ItemDef>();
            foreach (var d in ById.Values)
                if (d.recipe.Count > 0 && StationMatches(d.station, stationKind)) list.Add(d);
            list.Sort((a, b) => a.phase != b.phase ? a.phase.CompareTo(b.phase) : string.CompareOrdinal(a.name, b.name));
            return list;
        }

        // the doc's station names onto the rooms she has (the Hydro-Juicer stands in Hydroponics at the planter, the
        // Bio-Osmosis Filter beside the desalinator, the Artisan Bench with the forge)
        public static bool StationMatches(string station, string kind)
        {
            if (string.IsNullOrEmpty(station)) return false;
            string s = station.ToLowerInvariant();
            if (s.StartsWith("none")) return false;
            switch (kind)
            {
                case "fabricator": return s.Contains("fabricat");
                case "forge": return s.Contains("forge") || s.Contains("artisan");
                case "grill": return s.Contains("grill") || s.Contains("galley") || s.Contains("cook");
                case "desalinator": return s.Contains("desalinat") || s.Contains("distill") || s.Contains("osmosis");
                case "planter": return s.Contains("planter") || s.Contains("hydroponic") || s.Contains("juicer");
                case "boiler": return s.Contains("boiler") || s.Contains("engine room");
                case "moonpool": return s.Contains("moonpool") || s.Contains("vehicle");
                case "lockers": return s.Contains("dive room") || s.Contains("locker") || s.Contains("airlock");
                case "oxygen": return s.Contains("oxygen");
            }
            return false;
        }
    }

    // a list of slots with stacks
    [Serializable]
    public class Inventory
    {
        [Serializable] public class Slot { public string id; public int count; public float charge = 1f; }
        public List<Slot> slots = new List<Slot>();
        public int size;
        public Inventory(int size) { this.size = size; for (int i = 0; i < size; i++) slots.Add(new Slot()); }

        public int Count(ItemDef d) { int n = 0; foreach (var s in slots) if (s.id == d.id) n += s.count; return n; }
        public bool Has(ItemDef d, int n = 1) => Count(d) >= n;

        // add as many as fit; returns how many didn't
        public int Add(ItemDef d, int n = 1)
        {
            int max = d.MaxStack;
            foreach (var s in slots) { if (n <= 0) break; if (s.id == d.id && s.count < max) { int k = Mathf.Min(n, max - s.count); s.count += k; n -= k; } }
            foreach (var s in slots) { if (n <= 0) break; if (s.count == 0) { int k = Mathf.Min(n, max); s.id = d.id; s.count = k; s.charge = 1f; n -= k; } }
            return n;
        }

        public bool Remove(ItemDef d, int n = 1)
        {
            if (!Has(d, n)) return false;
            for (int i = slots.Count - 1; i >= 0 && n > 0; i--)
            {
                var s = slots[i];
                if (s.id != d.id) continue;
                int k = Mathf.Min(n, s.count); s.count -= k; n -= k;
                if (s.count == 0) s.id = null;
            }
            return true;
        }

        public bool CanMake(ItemDef d, Inventory also = null)
        {
            foreach (var (it, c) in d.recipe) if (Count(it) + (also != null ? also.Count(it) : 0) < c) return false;
            return true;
        }

        // craft from this inventory (and a second store, e.g. the ship's lockers): ingredients out, the item in
        public bool Make(ItemDef d, Inventory also = null)
        {
            if (!CanMake(d, also)) return false;
            foreach (var (it, c) in d.recipe)
            {
                int mine = Mathf.Min(Count(it), c);
                Remove(it, mine);
                if (c - mine > 0) also?.Remove(it, c - mine);
            }
            if (Add(d) > 0 && also != null) also.Add(d);
            return true;
        }

        public bool Full(ItemDef d) { foreach (var s in slots) if (s.count == 0 || (s.id == d.id && s.count < d.MaxStack)) return false; return true; }
        public void Clear() { foreach (var s in slots) { s.id = null; s.count = 0; } }
    }
}
