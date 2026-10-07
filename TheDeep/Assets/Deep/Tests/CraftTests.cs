// Stage 4's checks: stacks and slots, crafting from the pack and the ship's stores together, the item tables loading
// with every recipe's ingredients known, and the sea's temperature and the body's warmth.
using NUnit.Framework;
using UnityEngine;

namespace Deep.Tests
{
    public class CraftTests
    {
        static ItemDef Res(string n) => new ItemDef { id = ItemDB.Slug(n), name = n, category = "resource", resource = true };

        [Test]
        public void StacksFillAndOverflow()
        {
            var inv = new Inventory(2);
            var ore = Res("Titanium Ore");
            Assert.AreEqual(0, inv.Add(ore, 25), "25 ore fit in two stacks of 20");
            Assert.AreEqual(25, inv.Count(ore));
            Assert.AreEqual(15, inv.Add(ore, 30), "then only 15 more fit");
            Assert.IsTrue(inv.Remove(ore, 40));
            Assert.AreEqual(0, inv.Count(ore));
            Assert.IsNull(inv.slots[0].id, "emptied slots are free again");
            var knife = new ItemDef { id = "knife", name = "Survival Knife", category = "tool" };
            var inv2 = new Inventory(1);
            Assert.AreEqual(1, inv2.Add(knife, 2), "tools don't stack");
        }

        [Test]
        public void CraftingTakesFromThePackThenTheStores()
        {
            var a = Res("Ribbon Grass Fiber"); var b = Res("Solar-Carpet Calcite");
            var rope = new ItemDef { id = "rope", name = "Rope", category = "component" };
            rope.recipe.Add((a, 3)); rope.recipe.Add((b, 1));
            var pack = new Inventory(6); var store = new Inventory(6);
            pack.Add(a, 2); store.Add(a, 4); store.Add(b, 1);
            Assert.IsFalse(pack.CanMake(rope), "not from the pack alone");
            Assert.IsTrue(pack.CanMake(rope, store), "but with her stores");
            Assert.IsTrue(pack.Make(rope, store));
            Assert.AreEqual(1, pack.Count(rope));
            Assert.AreEqual(0, pack.Count(a), "the pack's two fibres went first");
            Assert.AreEqual(3, store.Count(a), "then one from the stores");
            Assert.AreEqual(0, store.Count(b));
            Assert.IsFalse(pack.Make(rope, store), "and there's not enough for another");
        }

        [Test]
        public void TheItemTablesLoad()
        {
            ItemDB.Load();
            Assert.Greater(ItemDB.ById.Count, 20, "items and resources loaded from Resources/Data/items.json");
            Assert.IsNotNull(ItemDB.Get("Survival Knife"), "the start's knife exists");
            int recipes = 0;
            foreach (var d in ItemDB.ById.Values) if (d.recipe.Count > 0) recipes++;
            Assert.Greater(recipes, 10, "with recipes");
            Assert.Greater(ItemDB.Hull.Count, 1, "and the hull upgrades");
        }

        // every phase 1-2 recipe's raw ingredients must be gatherable in the world: from a plant or an animal of the
        // two biomes' tables, the titanium deposits, the wrecks' salvage, or seawater
        [Test]
        public void EveryPhaseOneAndTwoRecipeCanBeGathered()
        {
            ItemDB.Load(); SpeciesBook.Load();
            var world = new System.Collections.Generic.HashSet<string>(System.StringComparer.OrdinalIgnoreCase)
                { "Titanium Ore", "Salvaged Galleon Wood", "Salvaged Galleon Brass", "Military-Grade Titanium", "Rusted Iron Scrap" };
            foreach (var t in SpeciesBook.Tables)
            {
                if (t.flora != null) foreach (var f in t.flora) foreach (var r in ItemDB.From(f.name)) world.Add(r.name);
                if (t.fauna != null) foreach (var a in t.fauna) foreach (var r in ItemDB.From(a.name)) world.Add(r.name);
            }
            var missing = new System.Collections.Generic.List<string>();
            var seen = new System.Collections.Generic.HashSet<ItemDef>();
            void Need(ItemDef d, string forWhat)
            {
                if (!seen.Add(d)) return;
                if (d.recipe.Count > 0) { foreach (var (it, _) in d.recipe) Need(it, d.name); return; }
                if (!d.resource && d.category != "resource") return;          // made from nothing at its station (water)
                if (!world.Contains(d.name)) missing.Add($"{d.name} (for {forWhat})");
            }
            foreach (var d in ItemDB.ById.Values)
                if (d.phase >= 1 && d.phase <= 2 && d.recipe.Count > 0 && d.category != "other") Need(d, d.name);
            Assert.IsEmpty(missing, "not gatherable anywhere: " + string.Join("; ", missing));
        }

        [Test]
        public void TheSeaIsColderDeepAndAtNight()
        {
            Assert.Greater(Survival.WaterC(5, 1), Survival.WaterC(40, 1));
            Assert.Greater(Survival.WaterC(40, 1), Survival.WaterC(100, 1), "the kelp is colder than the shallows");
            Assert.Greater(Survival.WaterC(10, 1), Survival.WaterC(10, 0), "and night colder than day");
            Assert.Less(Survival.WaterC(140, 0), 12f);
        }
    }
}
