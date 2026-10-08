// The campaign (stage 7; the user's decision: a long saved campaign per crew, the host saves). One file next to the game
// (deep_campaign.json, or -save <file>): the world's seed and everything the crew have changed in it - the clock, the
// Nautilus (where she is, her orders, power, battery, fuel, repairs, crush depth, flooding and open breaches, her
// stores), the Kite-Sub and the raft, the opening (done or not), the plants and deposits taken and when they grow
// back, the decorations aboard, the satchels left by the dead, the seas' biomass pools (the harvest's mark on the food
// web), the captain's log (creatures met, the deepest dive), and each diver's own record by name (pack, hand, health,
// air, food, water, warmth, rest, where they were). A crewmate's PC sends the host its diver's record every 15 s and
// gets it back in the welcome when it rejoins, so everyone picks up where they left off.
// Saved every two minutes, on F5, and on quitting; started fresh with -newgame (the arcade's "New campaign").
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Deep
{
    [Serializable] public class SlotSave { public string id; public int count; }
    [Serializable] public class BreachSave { public int room; public Vector3 gen; public float size, patch; }
    [Serializable] public class ShipSave
    {
        public Vector3 pos; public Quaternion rot; public float heading, depthOrder, headingOrder; public int telegraph; public bool holdDepth, holdHeading;
        public int state; public float battery, fuel, crushDepth; public bool engineRepaired, breakersTripped, geothermal;
        public List<float> rooms = new List<float>(); public List<BreachSave> breaches = new List<BreachSave>();
        public List<SlotSave> store = new List<SlotSave>();
    }
    [Serializable] public class KiteSave { public bool exists, docked; public Vector3 pos; public Quaternion rot; public float battery, hull; }
    [Serializable] public class RaftSave { public bool exists, flipped; public Vector3 pos; public float heading; }
    [Serializable] public class FloraSave { public string kind; public long key; public Vector3 pos; public float left; }
    [Serializable] public class DepositSave { public int index; public float left; }
    [Serializable] public class DecorSave { public int id; public string item; public Vector3 local; public Quaternion rot; }
    [Serializable] public class DropSave { public int id; public string owner; public bool aboard; public Vector3 pos; public List<SlotSave> items = new List<SlotSave>(); }
    [Serializable] public class PoolSave { public string biome; public float[] B; public float memory, harvested; }
    [Serializable] public class CrewSave
    {
        public string name; public List<SlotSave> pack = new List<SlotSave>(); public int held;
        public float health, oxygen, hunger, thirst, bodyC, rested;
        public int where; public Vector3 pos; public float yaw;      // where: 0 in the sea, 1 aboard (pos in her proxy frame)
    }
    [Serializable] public class SaveFile
    {
        public int version = 1, seed; public string savedAt;
        public float hour; public int day; public bool openingDone;
        public ShipSave ship = new ShipSave(); public KiteSave kite = new KiteSave(); public RaftSave raft = new RaftSave();
        public List<FloraSave> flora = new List<FloraSave>(); public List<DepositSave> deposits = new List<DepositSave>();
        public List<DecorSave> decor = new List<DecorSave>(); public List<DropSave> drops = new List<DropSave>();
        public List<PoolSave> pools = new List<PoolSave>(); public List<CrewSave> crew = new List<CrewSave>();
        public List<string> met = new List<string>(); public float deepest;
        public bool stocked;      // her starting spares (batteries, fuel) have been put in her stores
        public bool reachedReef;  // the crew have found the reef where gathering starts (Waypoints)
    }

    public class Campaign : MonoBehaviour
    {
        public static Campaign I;
        public SaveFile loaded;                         // what was read at the start (null: a new campaign)
        public readonly HashSet<string> met = new HashSet<string>();
        public float deepest;
        public bool reachedReef;
        public readonly Dictionary<string, CrewSave> crew = new Dictionary<string, CrewSave>();   // every diver's last record, by name
        public bool saving = true;                      // (off in the screenshot harness and the tests)
        float autoT, metT, sendT;
        public const float AutoEvery = 120f;

        public static string PathOf => !string.IsNullOrEmpty(Args.Save) ? Args.Save : System.IO.Path.Combine(System.IO.Path.GetDirectoryName(Application.dataPath) ?? ".", "deep_campaign.json");

        // before the world is built: is there a campaign to continue? (its seed builds the world)
        public static SaveFile Peek()
        {
            if (Args.NewGame || !string.IsNullOrEmpty(Args.Shot) || Args.NetTest) return null;
            try
            {
                if (!File.Exists(PathOf)) return null;
                var s = JsonUtility.FromJson<SaveFile>(File.ReadAllText(PathOf));
                return s != null && s.version >= 1 ? s : null;
            }
            catch (Exception e) { Debug.LogWarning("DEEP SAVE: couldn't read " + PathOf + ": " + e.Message); return null; }
        }

        public static Campaign Attach(SaveFile loaded)
        {
            var c = new GameObject("Campaign").AddComponent<Campaign>(); I = c;
            c.loaded = loaded;
            c.saving = string.IsNullOrEmpty(Args.Shot) && !Args.NetTest && !Net.IsGuest && !Args.SaveTest;
            if (loaded != null) { foreach (var m in loaded.met) c.met.Add(m); c.deepest = loaded.deepest; foreach (var cr in loaded.crew) if (cr != null && !string.IsNullOrEmpty(cr.name)) c.crew[cr.name] = cr; }
            return c;
        }

        static List<SlotSave> Slots(Inventory inv)
        {
            var l = new List<SlotSave>();
            foreach (var s in inv.slots) l.Add(new SlotSave { id = s.count > 0 ? s.id : null, count = s.count });
            return l;
        }
        static void Fill(Inventory inv, List<SlotSave> l)
        {
            if (l == null) return;
            while (inv.slots.Count < l.Count) inv.slots.Add(new Inventory.Slot());
            inv.size = inv.slots.Count;
            for (int i = 0; i < inv.slots.Count; i++)
            {
                var s = inv.slots[i];
                if (i < l.Count && l[i].count > 0 && ItemDB.Get(l[i].id) != null) { s.id = l[i].id; s.count = l[i].count; s.charge = 1f; }
                else { s.id = null; s.count = 0; }
            }
        }

        // ---- capturing ------------------------------------------------------------------------------------------
        public static CrewSave CaptureDiver(Diver d, string name)
        {
            var c = new CrewSave { name = name };
            var h = d.GetComponent<Hands>(); var sv = d.GetComponent<Survival>();
            if (h != null) { c.pack = Slots(h.pack); c.held = h.held; }
            c.health = d.health; c.oxygen = d.oxygen;
            if (sv != null) { c.hunger = sv.hunger; c.thirst = sv.thirst; c.bodyC = sv.bodyC; c.rested = sv.restedT; }
            if (d.aboard) { c.where = 1; c.pos = d.transform.position - Nautilus.ProxyOrigin; }
            else { c.where = 0; c.pos = d.onRaft != null ? d.onRaft.transform.position + Vector3.right * 1.5f : d.piloting != null ? d.piloting.transform.position + Vector3.right * 2f : d.EyeWorld; }
            c.yaw = d.yaw;
            return c;
        }

        public SaveFile Capture()
        {
            var b = DeepBoot.I; var ship = b.ship; var sy = ship.sys;
            var f = new SaveFile { seed = Args.Seed, savedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm") };
            f.hour = b.clock.hour; f.day = b.clock.day;
            f.openingDone = Opening.I == null || Opening.I.done;
            var s = f.ship;
            s.pos = ship.Body.position; s.rot = ship.Body.rotation; s.heading = ship.heading; s.depthOrder = ship.depthOrder; s.headingOrder = ship.headingOrder;
            s.telegraph = ship.telegraph; s.holdDepth = ship.holdDepth; s.holdHeading = ship.holdHeading;
            s.state = (int)sy.state; s.battery = sy.battery; s.fuel = sy.fuel; s.crushDepth = ship.crushDepth; s.engineRepaired = sy.engineRepaired; s.breakersTripped = sy.breakersTripped; s.geothermal = sy.geothermal;
            foreach (var r in sy.rooms) s.rooms.Add(r.level);
            foreach (var br in sy.breaches) s.breaches.Add(new BreachSave { room = br.room, gen = br.gen, size = br.size, patch = br.patch });
            s.store = Slots(ship.store);
            var k = KiteSub.I;
            if (k) { f.kite.exists = true; f.kite.docked = k.docked; f.kite.pos = k.transform.position; f.kite.rot = k.transform.rotation; f.kite.battery = k.battery; f.kite.hull = k.hull; }
            var raft = Raft.I;
            if (raft) { f.raft.exists = true; f.raft.flipped = raft.flipped; f.raft.pos = raft.transform.position; f.raft.heading = raft.heading; }
            if (b.flora) foreach (var t in b.flora.Taken()) f.flora.Add(new FloraSave { kind = t.kind, key = t.key, pos = t.pos, left = t.left });
            if (Deposits.I != null) for (int i = 0; i < Deposits.I.nodes.Count; i++) { var n = Deposits.I.nodes[i]; if (n.taken) f.deposits.Add(new DepositSave { index = i, left = Mathf.Max(0, n.back - Time.time) }); }
            if (Decor.I != null) foreach (var p in Decor.I.placed.Values) f.decor.Add(new DecorSave { id = p.id, item = p.item, local = p.local, rot = p.rot });
            if (Drops.I != null) foreach (var dr in Drops.I.drops.Values)
            {
                var ds = new DropSave { id = dr.id, owner = dr.owner, aboard = dr.aboard, pos = dr.pos };
                foreach (var kv in dr.items) ds.items.Add(new SlotSave { id = kv.Key, count = kv.Value });
                f.drops.Add(ds);
            }
            if (Life.I != null) foreach (var p in Life.I.eco.pools) f.pools.Add(new PoolSave { biome = p.biome, B = (float[])p.B.Clone(), memory = p.memory, harvested = p.harvested });
            crew[Net.I ? Net.I.myName : "Diver"] = CaptureDiver(b.diver, Net.I ? Net.I.myName : "Diver");
            foreach (var kv in crew) f.crew.Add(kv.Value);
            f.met.AddRange(met); f.deepest = deepest; f.stocked = true; f.reachedReef = reachedReef;
            return f;
        }

        public bool Save()
        {
            if (!saving) return false;
            try
            {
                var f = Capture();
                var tmp = PathOf + ".tmp";
                File.WriteAllText(tmp, JsonUtility.ToJson(f));
                if (File.Exists(PathOf)) File.Delete(PathOf);
                File.Move(tmp, PathOf);
                Debug.Log($"DEEP SAVE: {PathOf} (day {f.day + 1}, {f.crew.Count} divers, {f.decor.Count} decorations)");
                return true;
            }
            catch (Exception e) { Debug.LogWarning("DEEP SAVE: couldn't write " + PathOf + ": " + e.Message); return false; }
        }

        // ---- restoring (after the world is built from the save's seed) -------------------------------------------------
        public void Apply(SaveFile f)
        {
            if (f == null) return;
            var b = DeepBoot.I; var ship = b.ship; var sy = ship.sys;
            b.clock.hour = f.hour; b.clock.day = f.day;
            var s = f.ship;
            ship.Restore(s.pos, s.rot, s.heading, s.telegraph, s.depthOrder, s.headingOrder, s.holdDepth, s.holdHeading);
            sy.state = (PowerState)Mathf.Clamp(s.state, 0, 2); sy.battery = s.battery; sy.fuel = s.fuel; ship.crushDepth = Mathf.Max(30f, s.crushDepth);
            sy.engineRepaired = s.engineRepaired; sy.breakersTripped = s.breakersTripped; sy.geothermal = s.geothermal; ship.power = sy.state != PowerState.Dead; ship.powerK = ship.power ? 1 : 0;
            for (int i = 0; i < sy.rooms.Count && i < s.rooms.Count; i++) sy.rooms[i].level = s.rooms[i];
            foreach (var br in new List<ShipSystems.Breach>(sy.breaches)) sy.DropBreach(br);
            foreach (var br in s.breaches) { var nb = sy.RestoreBreach(br.room, br.gen, br.size); if (nb != null) nb.patch = br.patch; }
            if (f.kite.exists)
            {
                var k = KiteSub.Spawn(ship);
                if (!f.kite.docked) { k.Undock(); k.transform.SetPositionAndRotation(f.kite.pos, f.kite.rot); }
                k.battery = f.kite.battery; k.hull = f.kite.hull;
            }
            if (f.openingDone && f.raft.exists)
            {
                var r = Raft.I ? Raft.I : Raft.Spawn(f.raft.pos, f.raft.heading);
                r.transform.position = f.raft.pos; r.heading = f.raft.heading; r.flipped = f.raft.flipped;
            }
            if (b.flora) foreach (var t in f.flora) b.flora.TakeAt(t.kind, t.key, t.pos, t.left);
            if (Deposits.I != null) foreach (var t in f.deposits) Deposits.I.TakeIndex(t.index, t.left);
            if (Decor.I != null) foreach (var d in f.decor) Decor.I.Add(d.id, d.item, d.local, d.rot);
            // her stores after the lockers (they make room for what's in them)
            Fill(ship.store, s.store);
            if (Drops.I != null) foreach (var d in f.drops) { var items = new Dictionary<string, int>(); foreach (var it in d.items) items[it.id] = it.count; Drops.I.Add(d.id, d.owner, d.aboard, d.pos, items); }
            if (!f.stocked) { StockStores(ship); sy.engineRepaired = true; }
            reachedReef = f.reachedReef || f.openingDone && f.day > 0;
            if (Life.I != null) foreach (var p in f.pools) foreach (var pool in Life.I.eco.pools) if (pool.biome == p.biome && p.B != null && p.B.Length == pool.B.Length) { Array.Copy(p.B, pool.B, p.B.Length); pool.memory = p.memory; pool.harvested = p.harvested; }
            if (f.openingDone && crew.TryGetValue(Net.I ? Net.I.myName : "Diver", out var me)) ApplyDiver(b.diver, me);
            Debug.Log($"DEEP SAVE: continued the campaign from {f.savedAt} (day {f.day + 1})");
        }

        // the derelict's stores at the start: spare batteries for the switchboard and fuel for the boiler
        public static void StockStores(Nautilus ship)
        {
            var bat = ItemDB.Get("Spare Battery Bank"); var fuel = ItemDB.Get("Synthetic Fuel Canister");
            if (bat != null) ship.store.Add(bat, 4);
            if (fuel != null) ship.store.Add(fuel, 6);
        }

        public static void ApplyDiver(Diver d, CrewSave c)
        {
            if (c == null) return;
            var h = d.GetComponent<Hands>(); var sv = d.GetComponent<Survival>();
            if (h != null) { Fill(h.pack, c.pack); h.held = c.held; }
            d.health = Mathf.Max(20f, c.health); d.oxygen = c.oxygen > 0 ? c.oxygen : d.oxygenMax;
            if (sv != null) { sv.hunger = c.hunger; sv.thirst = c.thirst; sv.bodyC = c.bodyC > 20f ? c.bodyC : 37f; sv.restedT = c.rested; }
            if (c.where == 1) d.Board(c.pos - Vector3.up * (d.cc.height / 2 + 0.02f), c.yaw);
            else d.Place(c.pos, c.yaw, 0);
        }

        // ---- every frame ----------------------------------------------------------------------------------------------
        void Update()
        {
            var b = DeepBoot.I; if (!b || !b.diver) return;
            // the log: what comes within 8 m of the eye in the water, and the deepest dive
            metT += Time.deltaTime;
            if (metT > 1f && Life.I != null)
            {
                metT = 0;
                var eye = b.diver.EyeWorld; deepest = Mathf.Max(deepest, b.diver.Depth);
                if (b.ship) deepest = Mathf.Max(deepest, b.ship.Depth);
                foreach (var c in Life.I.live) if (c.alive && (c.pos - eye).sqrMagnitude < 64f + c.size * c.size && met.Add(c.sp.e.name) && met.Count > 1) b.diver.Toast($"New to the log: the {c.sp.e.name}");
            }
            if (Net.IsGuest)
            {
                // a crewmate's PC: its diver's record goes to the host now and then
                sendT += Time.deltaTime;
                if (sendT > 15f) { sendT = 0; Net.SendCrewRecord(CaptureDiver(b.diver, Net.I.myName)); }
                return;
            }
            if (!saving) return;
            autoT += Time.deltaTime;
            if (autoT > AutoEvery) { autoT = 0; Save(); }
            if (b.diver.inputEnabled && Input.GetKeyDown(KeyCode.F5) && Save()) { Sfx.UI("save"); b.diver.Toast("The campaign is saved."); }
        }

        void OnApplicationQuit()
        {
            if (Net.IsGuest) { var b = DeepBoot.I; if (b && b.diver) Net.SendCrewRecord(CaptureDiver(b.diver, Net.I.myName)); return; }
            Save();
        }

        // ---- the save self-test (tools\deep.ps1 savetest): capture, change things, restore, compare ----------------
        public void SelfTest()
        {
            var b = DeepBoot.I; var ship = b.ship; int pass = 0, fail = 0;
            void Check(bool ok, string what) { if (ok) pass++; else fail++; Debug.Log($"DEEP SAVETEST: {(ok ? "PASS" : "FAIL")} {what}"); }
            // set up a campaign worth saving
            ship.sys.breakersTripped = false; ship.sys.state = PowerState.Silent; ship.sys.battery = 0.77f; ship.crushDepth = 50f; ship.telegraph = 4; ship.depthOrder = 12f;
            ship.sys.AddBreach(2, 0.04f);
            var ore = ItemDB.Get("Titanium Ore"); ship.store.Add(ore, 7);
            var h = b.diver.GetComponent<Hands>(); var glass = ItemDB.Get("Salvaged Galleon Wood"); if (glass != null) h.pack.Add(glass, 3);
            var lk = ItemDB.Get("dreadnought_brass_locker");
            Decor.I.Add(Decor.I.nextId++, "galleon_captains_bed", Nautilus.G(16f, Nautilus.Floor, 0.5f), Quaternion.Euler(0, 90, 0));
            if (lk != null) Decor.I.Add(Decor.I.nextId++, lk.id, Nautilus.G(18f, Nautilus.Floor, -0.8f), Quaternion.identity);
            Drops.I.Add(Drops.I.nextId++, "Tester", false, ship.Body.position + new Vector3(20, 0, 5), new Dictionary<string, int> { { ore.id, 4 } });
            KiteSub.Spawn(ship);
            met.Add("Test Fish"); deepest = 33f;
            b.clock.hour = 14.25f; b.clock.day = 5;
            if (b.flora.NearestPlant(b.diver.EyeWorld, out var plant)) b.flora.Take(plant, 600f);
            int floraAfter = b.flora.Total;
            var before = Capture();
            string json = JsonUtility.ToJson(before);
            Check(json.Length > 500, $"the save is written ({json.Length} bytes)");
            var back = JsonUtility.FromJson<SaveFile>(json);
            // scramble the live state, then restore it from the file's copy
            ship.sys.battery = 0.1f; ship.crushDepth = 30f; ship.telegraph = 2; ship.store.Clear(); h.pack.Clear();
            foreach (var id in new List<int>(Decor.I.placed.Keys)) Decor.I.Remove(id);
            foreach (var id in new List<int>(Drops.I.drops.Keys)) Drops.I.Remove(id);
            foreach (var br in new List<ShipSystems.Breach>(ship.sys.breaches)) ship.sys.DropBreach(br);
            b.clock.hour = 3f; b.clock.day = 0;
            Apply(back);
            Check(Mathf.Abs(ship.sys.battery - 0.77f) < 1e-3f && ship.crushDepth == 50f && ship.telegraph == 4 && Mathf.Abs(ship.depthOrder - 12f) < 1e-3f, "her battery, crush depth and orders");
            Check(ship.sys.breaches.Count == 1, "her open breach");
            Check(ore != null && ship.store.Count(ore) == 7, $"her stores ({(ore != null ? ship.store.Count(ore) : -1)} ore)");
            Check(ship.store.slots.Count == Decor.BaseStore + 20, $"the brass locker's room in her stores ({ship.store.slots.Count} slots)");
            Check(Decor.I.placed.Count == 2, "the decorations aboard");
            Check(Drops.I.drops.Count == 1, "the satchel on the seabed");
            Check(glass == null || h.pack.Count(glass) == 3, "the diver's pack");
            Check(Mathf.Abs(b.clock.hour - 14.25f) < 1e-3f && b.clock.day == 5, "the clock");
            Check(KiteSub.I != null, "the Kite-Sub");
            Check(back.met.Contains("Test Fish") && Mathf.Abs(back.deepest - 33f) < 1e-3f, "the captain's log");
            Check(back.flora.Count >= 1 && b.flora.Total <= floraAfter, "the plants taken");
            Debug.Log($"DEEP SAVETEST: {pass} passed, {fail} failed");
            Application.Quit(fail == 0 ? 0 : 1);
        }
    }
}
