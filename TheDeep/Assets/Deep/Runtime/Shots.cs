// The screenshot harness (like Depth's --shots): TheDeep.exe -shot <name|all> -shotdir <folder>
// Each shot places the eye, holds the clock, lets a few frames settle, saves <folder>/deep_<name>.png, then quits.
using System.Collections;
using System.IO;
using UnityEngine;

namespace Deep
{
    public class Shots : MonoBehaviour
    {
        public static bool HideOcean;
        static readonly string[] All = { "debugdown", "debugair", "reef", "kelp", "up", "above", "night", "drop", "meadow",
            "nautilus", "nautilus_side", "nautilus_stern", "salon", "bridge", "pilothouse", "engine", "moonpool", "dark", "underway", "helm", "flooding", "sonar", "telegraph", "gauges", "life_reef", "life_school", "life_kelp", "life_night", "leviathan", "crew", "cabin", "ladder_top", "life_blood", "life_starve", "life_coral", "life_light", "life_engine", "life_moon", "wreck_galleon", "wreck_dreadnought", "deposit", "craft", "pack", "kitesub", "kitesub_fly", "opening_raft", "opening_board", "opening_storm", "opening_hunt" };

        public void Run(string which, string dir) { StartCoroutine(Go(which, dir)); }

        IEnumerator Go(string which, string dir)
        {
            Directory.CreateDirectory(dir);
            var boot = DeepBoot.I; boot.diver.inputEnabled = false; boot.clock.frozen = true;
            var list = which == "all" ? All : which.Split(',');
            yield return null;
            foreach (var name in list)
            {
                bool ok;
                var ui0 = boot.diver.GetComponent<CraftUI>(); if (ui0 != null && ui0.mode != null) ui0.Close();
                if (boot.diver.piloting != null) boot.diver.LeaveKiteSub(true);
                try { ok = Setup(name, boot); }
                catch (System.Exception ex) { Debug.LogError("DEEP SHOT: " + name + " failed: " + ex); continue; }
                if (!ok) { Debug.LogWarning("DEEP SHOT: unknown " + name); continue; }
                boot.diver.toast = "";
                for (int i = 0; i < 40; i++) yield return null;
                string path = Path.Combine(dir, "deep_" + name + ".png");
                ScreenCapture.CaptureScreenshot(path);
                yield return null; yield return null;
                Debug.Log("DEEP SHOT: " + path);
            }
            yield return null;
            Application.Quit();
        }

        static void LogLife(DeepBoot b)
        {
            var kinds = new System.Collections.Generic.Dictionary<string, int>();
            foreach (var c in b.life.live) if (c.alive) { kinds.TryGetValue(c.sp.e.name, out int k); kinds[c.sp.e.name] = k + 1; }
            var top = new System.Collections.Generic.List<string>();
            foreach (var kv in kinds) top.Add($"{kv.Key} {kv.Value}");
            var whys = new System.Collections.Generic.Dictionary<string, int>();
            foreach (var c in b.life.live) if (c.alive && c.why != null) { whys.TryGetValue(c.why, out int w); whys[c.why] = w + 1; }
            var wl = new System.Collections.Generic.List<string>(); foreach (var kv in whys) wl.Add($"{kv.Key} {kv.Value}");
            Debug.Log("DEEP WHY: " + string.Join(", ", wl));
            Debug.Log($"DEEP LIFE: {b.life.live.Count} alive, {kinds.Count} species; dormant {b.life.Count(CState.Dormant)} alert {b.life.Count(CState.Alert)} hunting {b.life.Count(CState.Hunting)} fleeing {b.life.Count(CState.Fleeing)} frenzy {b.life.Count(CState.Frenzy)} territorial {b.life.Count(CState.Territorial)}; " + string.Join(", ", top));
        }

        static bool Setup(string name, DeepBoot b)
        {
            var bed = b.seabed;
            Vector3 Find(float x0, float x1, float minD, float maxD)
            {
                for (int k = 0; k < 4000; k++)
                {
                    float x = Mathf.Lerp(x0, x1, (k * 0.618f) % 1f), z = Seabed.Size * (0.25f + 0.5f * ((k * 0.381f) % 1f));
                    float y = bed.H(x, z);
                    if (-y > minD && -y < maxD) return new Vector3(x, y, z);
                }
                return new Vector3((x0 + x1) / 2, -20, Seabed.Size / 2);
            }
            var ship = b.ship;
            void Look(Vector3 from, Vector3 to)
            {
                var d = to - from;
                b.diver.Place(from, Mathf.Atan2(d.x, d.z) * Mathf.Rad2Deg, -Mathf.Atan2(d.y, new Vector2(d.x, d.z).magnitude) * Mathf.Rad2Deg);
            }
            void Power(bool on)
            {
                ship.sys.state = on ? PowerState.Engine : PowerState.Dead; ship.sys.fuel = 0.6f; ship.sys.battery = 0.6f; ship.sys.engineRepaired = true;
                ship.power = on; ship.powerK = on ? 1 : 0;
            }
            switch (name)
            {
                case "nautilus": { b.clock.hour = 11f; Power(false); Look(ship.WorldPoint(Nautilus.G(34, 6, 24)), ship.WorldPoint(Nautilus.G(6, 0, 0))); return true; }
                case "nautilus_side": { b.clock.hour = 19.2f; Power(true); Look(ship.WorldPoint(Nautilus.G(12, 1.5f, 15)), ship.WorldPoint(Nautilus.G(12, 0.5f, 0))); return true; }
                case "nautilus_stern": { b.clock.hour = 10f; Power(false); Look(ship.WorldPoint(Nautilus.G(-46, 4, -14)), ship.WorldPoint(Nautilus.G(-20, 0, 0))); return true; }
                case "salon": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(20.5f, 1.2f, Nautilus.Floor, -90f, 4f); return true; }
                case "bridge": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(27.3f, 0.6f, Nautilus.Floor, 8f, 6f); return true; }
                case "pilothouse": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(Nautilus.ShaftX + 1.6f, 0f, 3.7f, 0f, 6f); return true; }
                case "engine": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(-15.5f, 0.5f, Nautilus.Floor, 186f, 4f); return true; }
                case "moonpool": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(-8.6f, -1.2f, Nautilus.Floor, 230f, 28f); return true; }
                case "underway":
                {
                    // power up, half ahead, lift off to 6 m above the bottom, a slow turn to starboard; 50 s of sailing
                    b.clock.hour = 11f; Power(true); ship.telegraph = 4; ship.depthOrder = ship.Depth - 6f; ship.rudder = 0.35f;
                    for (int i = 0; i < 1000; i++) ship.Sail(0.05f);
                    Debug.Log($"DEEP SAIL: speed {ship.speed:0.00} heading {ship.heading:0} depth {ship.Depth:0.0} order {ship.depthOrder:0.0} grounded {ship.grounded:0.0}");
                    Look(ship.WorldPoint(Nautilus.G(-8, 5, 22)), ship.WorldPoint(Nautilus.G(8, 0, 0)));
                    return true;
                }
                case "helm":
                {
                    b.clock.hour = 11f; Power(true); ship.telegraph = 3; ship.depthOrder = ship.Depth - 4f;
                    for (int i = 0; i < 400; i++) ship.Sail(0.05f);
                    foreach (var st in ship.L.stations) if (st.kind == "helm") b.diver.Man(st);
                    ship.rudder = 0.6f;
                    b.diver.pitch = 4; b.diver.head.localRotation = Quaternion.Euler(4, 0, 0);
                    return true;
                }
                case "flooding":
                {
                    // a breach in the Fabrication Bay, 90 s of the sea coming in (the electric pumps losing), the
                    // water running through the doors; looking along the flooded salon at the jet
                    b.clock.hour = 11f; Power(true);
                    var br = ship.sys.AddBreach(ship.sys.RoomIndexAt(new Vector3(20f, 0f, 0f)), 0.06f, new System.Random(3));
                    for (int i = 0; i < 1800; i++) { ship.sys.Step(0.05f); }
                    Debug.Log($"DEEP FLOOD: {ship.sys.WaterTonnes:0} t; levels " + string.Join(" ", ship.sys.rooms.ConvertAll(r => $"{r.r.id}={r.level:0.00}")));
                    b.diver.PlaceAboard(15.2f, 0f, Nautilus.Floor, Nautilus.FacingYaw(new NLStation { facing = new[] { 1f, 0f, Mathf.Sign(br.gen.z) * 0.35f } }), 10f);
                    return true;
                }
                case "sonar":
                {
                    b.clock.hour = 11f; Power(true);
                    foreach (var st in ship.L.stations) if (st.kind == "sonar") b.diver.Man(st);
                    ship.sys.Ping();
                    return true;
                }
                case "life_reef":
                {
                    b.clock.hour = 10.5f; var p = Find(120, 420, 10, 16);
                    b.life.WarmUp(p + Vector3.up * 3f, 40f);
                    b.diver.Place(p + Vector3.up * 3f, 80, 6);
                    LogLife(b);
                    return true;
                }
                case "life_school":
                {
                    // the nearest school, from a few metres off
                    b.clock.hour = 11f; var p = Find(150, 420, 12, 20);
                    b.life.WarmUp(p + Vector3.up * 4f, 40f);
                    Creature best = null; float bd = 1e9f;
                    foreach (var c in b.life.live) if (c.alive && c.group != 0 && c.sp.schooling && !c.sp.bottom) { float d = (c.pos - p).sqrMagnitude; if (d < bd) { bd = d; best = c; } }
                    if (best != null) Look(best.pos + new Vector3(4f, 1.5f, -6f) * Mathf.Max(1f, best.size * 3f), best.pos);
                    else b.diver.Place(p + Vector3.up * 4f, 80, 6);
                    LogLife(b);
                    return true;
                }
                case "life_kelp":
                {
                    b.clock.hour = 11f; var p = Find(700, 1000, 80, 100);
                    b.life.WarmUp(p + Vector3.up * 6f, 40f);
                    b.diver.Place(p + Vector3.up * 6f, 90, 0);
                    LogLife(b);
                    return true;
                }
                case "life_night":
                {
                    b.clock.hour = 22.5f; var p = Find(120, 420, 10, 16);
                    b.life.WarmUp(p + Vector3.up * 3f, 40f);
                    b.diver.Place(p + Vector3.up * 3f, 80, 6);
                    LogLife(b);
                    return true;
                }
                case "leviathan":
                {
                    b.clock.hour = 12f;
                    Creature lv = null; foreach (var c in b.life.live) if (c.sp.resident && c.sp.biome == "shallows") lv = c;
                    if (lv == null) return false;
                    b.life.WarmUp(lv.pos, 5f);
                    Look(lv.pos + Quaternion.LookRotation(lv.fwd) * new Vector3(28f, 8f, 10f), lv.pos);
                    return true;
                }
                case "wreck_galleon":
                case "wreck_dreadnought":
                {
                    var w = Deposits.I.wrecks.Find(x => x.name.Contains(name == "wreck_galleon" ? "Galleon" : "dreadnought"));
                    if (w.t == null) return false;
                    b.clock.hour = 11f;
                    var eye = w.t.TransformPoint(new Vector3(18f, 9f, -14f));
                    eye.y = Mathf.Max(eye.y, bed.SampleY(eye.x, eye.z) + 3f);
                    Look(eye, w.t.position + Vector3.up * 2f);
                    return true;
                }
                case "deposit":
                {
                    b.clock.hour = 11f;
                    Deposits.Node best = null;
                    foreach (var n in Deposits.I.nodes) if (n.by == "drill" && -n.pos.y < 45f) { best = n; break; }
                    if (best == null) return false;
                    var eye = best.pos + new Vector3(2.2f, 1.4f, -1.2f);
                    b.diver.Place(eye, 0, 0); Look(eye, best.pos + Vector3.up * 0.3f);
                    var h = b.diver.GetComponent<Hands>(); var drill = ItemDB.Get("Starter Drill");
                    if (h != null && drill != null) { h.pack.Add(drill); h.held = h.pack.slots.FindIndex(sl => sl.id == drill.id); }
                    return true;
                }
                case "craft":
                case "pack":
                {
                    b.clock.hour = 11f; Power(true);
                    foreach (var st in ship.L.stations) if (st.kind == "fabricator") b.diver.PlaceAboard(st.pos[0], st.pos[2], Nautilus.Floor, Nautilus.FacingYaw(st), 10f);
                    var h = b.diver.GetComponent<Hands>();
                    foreach (var (n, k) in new[] { ("Titanium Ore", 6), ("Solar-Carpet Algae", 5), ("Calcite Algae Flakes", 3), ("Crest Reed Shafts", 2), ("Golden Moss Filament", 2), ("Ribbon Grass Fiber", 3), ("Raw Chromis Glider", 2) })
                    { var it = ItemDB.Get(n); if (it != null) h.pack.Add(it, k); }
                    var ui = b.diver.GetComponent<CraftUI>();
                    if (name == "craft") { ui.Open("craft", "fabricator"); } else ui.Open("pack");
                    return true;
                }
                case "kitesub":
                {
                    b.clock.hour = 11f; Power(true);
                    KiteSub.Spawn(ship);
                    Look(ship.WorldPoint(Nautilus.G(-12, -8, 7)), KiteSub.I.transform.position);
                    return true;
                }
                case "opening_raft":
                case "opening_storm":
                case "opening_board":
                case "opening_hunt":
                {
                    if (Opening.I == null) Opening.Begin(b);
                    var o = Opening.I; var raft = Raft.I; raft.Right();
                    b.clock.hour = 21.6f;
                    if (b.diver.onRaft == null) b.diver.EnterRaft(raft, 0);
                    var toShip = ship.Body.position - raft.transform.position; toShip.y = 0;
                    if (name == "opening_raft") { o.t = 30f; b.weather.storm = 0.1f; b.weather.target = 0.1f; }
                    if (name == "opening_storm" || name == "opening_hunt") { o.t = name == "opening_hunt" ? 500f : 560f; b.weather.storm = 0.9f; b.weather.target = 0.9f; b.weather.flash = name == "opening_storm" ? 0.8f : 0f; }
                    if (name != "opening_board") { var sp = ship.Body.position + ship.Body.right * (name == "opening_raft" ? 260f : 110f); raft.transform.position = new Vector3(sp.x, 0, sp.z); toShip = ship.Body.position - raft.transform.position; toShip.y = 0; }
                    if (name == "opening_board")
                    {
                        o.t = 200f; b.weather.storm = 0.35f; b.weather.target = 0.35f;
                        foreach (var hk in ship.L.hatches)
                            if (hk.kind == "deck") { var hp = ship.WorldPoint(Nautilus.G(hk.outside)); raft.transform.position = new Vector3(hp.x, 0, hp.z) + ship.Body.right * 4f; }
                        toShip = ship.WorldPoint(Nautilus.G(0, 3, 0)) - raft.transform.position; toShip.y = 0;
                    }
                    b.diver.yaw = Mathf.Atan2(toShip.x, toShip.z) * Mathf.Rad2Deg; b.diver.pitch = name == "opening_board" ? -8f : 2f;
                    if (name == "opening_hunt") { b.diver.pitch = 25f; }
                    return true;
                }
                case "kitesub_fly":
                {
                    b.clock.hour = 10.5f; Power(true);
                    if (KiteSub.I == null) KiteSub.Spawn(ship);
                    b.diver.EnterKiteSub(KiteSub.I);
                    b.diver.yaw = KiteSub.I.transform.eulerAngles.y + 60f; b.diver.pitch = 8f;
                    for (int i = 0; i < 120; i++) KiteSub.I.Drive(b.diver, 1f / 60f);
                    return true;
                }
                case "life_light":
                {
                    // night on the reef with the helmet lamp on: light-drawn animals come to the beam, light-shy ones flee
                    b.clock.hour = 23f; b.clock.day = 3; var p = Find(150, 420, 10, 18);
                    b.diver.lampOn = true; b.diver.Place(p + Vector3.up * 3f, 80, 10);
                    b.life.WarmUp(p + Vector3.up * 3f, 30f);
                    LogLife(b);
                    return true;
                }
                case "life_engine":
                {
                    // her engine run for two minutes at full ahead: the Wake heats and the Reef-Crusher comes to ram her
                    b.clock.hour = 11f; Power(true); ship.telegraph = 5; ship.depthOrder = ship.Depth - 5f;
                    var eye = ship.WorldPoint(Nautilus.G(0, 8, 14));
                    for (int i = 0; i < 1200; i++) { ship.Sail(0.1f); ship.sys.Step(0.1f); b.life.Tick(0.1f, eye); }
                    Debug.Log($"DEEP ENGINE: Wake at her {b.life.sound.WakeAt(ship.Body.position):0.0} (tier {Acoustics.Tier(b.life.sound.WakeAt(ship.Body.position))}), breaches {ship.sys.breaches.Count}");
                    LogLife(b);
                    Look(eye, ship.Body.position);
                    return true;
                }
                case "life_moon":
                {
                    // a full moon: the plankton bloom brings the bait fish and their hunters up near the surface
                    b.clock.hour = 23.5f; b.clock.day = 14; var p = Find(150, 420, 18, 30);
                    var eye = new Vector3(p.x, -4f, p.z);
                    b.life.WarmUp(eye, 30f);
                    int high = 0, bait = 0; foreach (var c in b.life.live) if (c.alive && c.sp.schooling && c.sp.level == 1) { bait++; if (c.pos.y > -12f) high++; }
                    Debug.Log($"DEEP MOON: {high} of {bait} bait fish in the top 12 m");
                    b.diver.Place(eye, 80, 20);
                    LogLife(b);
                    return true;
                }
                case "life_coral":
                {
                    // the densest coral within the Shallows: the reef's life gathers there
                    b.clock.hour = 10.5f;
                    var pts = new System.Collections.Generic.List<Vector3>();
                    Vector3 best = Find(150, 420, 10, 18); int most = -1;
                    for (int k = 0; k < 300; k++)
                    {
                        var q = Find(100 + (k * 37) % 400, 140 + (k * 37) % 400, 6, 22);
                        b.flora.Near(q, 0, 12, pts, 400, "reef");
                        if (pts.Count > most) { most = pts.Count; best = q; }
                    }
                    var eye = best + new Vector3(-9f, 3.2f, -2f);
                    b.life.WarmUp(eye, 40f);
                    Look(eye, best + Vector3.up * 1f);
                    Debug.Log($"DEEP CORAL: {most} corals within 12 m of {best}");
                    LogLife(b);
                    return true;
                }
                case "life_blood":
                {
                    // a wounded fish bleeding 20 m ahead for 30 s: the hunters come and frenzy
                    b.clock.hour = 11f; var p = Find(150, 420, 12, 20);
                    var eye = p + Vector3.up * 4f;
                    b.life.WarmUp(eye, 20f);
                    var spill = eye + new Vector3(20f, -1f, 0);
                    for (int i = 0; i < 300; i++) { b.life.scent.Emit(spill, 0.6f); b.life.Tick(0.1f, eye); }
                    LogLife(b);
                    b.diver.Place(eye, 90, 4);
                    return true;
                }
                case "life_starve":
                {
                    // the crew nets 75% of the Shallows' grazers: the hunters starve, lose their fear and come for the diver
                    // and the Nautilus's larder
                    b.clock.hour = 11f; Power(false);
                    var pool = b.life.eco.Of("shallows"); b.life.eco.Harvest("shallows", 1, pool.B[1] * 0.75f);
                    var eye = ship.WorldPoint(Nautilus.G(4, 6, 10));
                    int breaches = ship.sys.breaches.Count;
                    for (int i = 0; i < 1200; i++) { b.life.Tick(0.1f, eye); ship.sys.Step(0.1f); }
                    Debug.Log($"DEEP STARVE: shallows hunters starving={pool.Starving(2)} hunger={pool.Hunger(2):0.00}; hull breaches {breaches} -> {ship.sys.breaches.Count}");
                    LogLife(b);
                    Look(eye, ship.WorldPoint(Nautilus.G(2, 0, 0)));
                    return true;
                }
                case "crew":
                {
                    b.clock.hour = 11f; Power(true);
                    var cr = ship.L.Room("crew");
                    b.diver.PlaceAboard(cr.x0 + 0.5f, 0f, Nautilus.Floor, 0f, 4f);
                    return true;
                }
                case "cabin":
                {
                    b.clock.hour = 11f; Power(true);
                    var cb = ship.L.cabins[1];
                    b.diver.PlaceAboard(cb.door[0], cb.door[1] * 0.4f, Nautilus.Floor, Mathf.Sign(cb.door[1]) > 0 ? 70f : -70f, 12f);
                    return true;
                }
                case "ladder_top":
                {
                    // climbing the pilot house ladder for 6 s from the Bridge: where does it leave you?
                    b.clock.hour = 11f; Power(true);
                    b.diver.PlaceAboard(Nautilus.ShaftX - 0.1f, 0f, Nautilus.Floor, 0f, 0f);
                    b.diver.inputEnabled = true; b.diver.SimHold = KeyCode.W;
                    for (int i = 0; i < 360; i++) b.diver.SimStep(1f / 60f);
                    b.diver.SimHold = KeyCode.None; b.diver.inputEnabled = false;
                    var gg = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(b.diver.transform.position));
                    Debug.Log($"DEEP LADDER: after climbing, at {gg} climbing={b.diver.climbing} room={(ship.L.RoomAt(gg)?.id ?? "-")}");
                    b.diver.pitch = 10; b.diver.head.localRotation = Quaternion.Euler(10, 0, 0);
                    return true;
                }
                case "telegraph":
                {
                    b.clock.hour = 11f; Power(true); ship.telegraph = 5;
                    b.diver.PlaceAboard(31.35f, 1.3f, Nautilus.Floor, 12f, 30f);
                    return true;
                }
                case "gauges":
                {
                    b.clock.hour = 10.3f; Power(true); ship.depthOrder = ship.Depth; ship.telegraph = 2;
                    b.diver.PlaceAboard(28.6f, 0.4f, Nautilus.Floor, -112f, 10f);
                    return true;
                }
                case "dark": { b.clock.hour = 11f; Power(false); b.diver.lampOn = true; b.diver.PlaceAboard(15.0f, 0.3f, Nautilus.Floor, 0f, 2f); return true; }
                case "debugdown": { b.clock.hour = 12f; var p = Find(150, 400, 10, 15); b.diver.Place(new Vector3(p.x, p.y + 6f, p.z), 0, 89); return true; }
                case "debugair": { HideOcean = true; b.clock.hour = 12f; b.diver.Place(new Vector3(400, 120f, 300), 45, 35); return true; }
                case "reef": { HideOcean = false; b.clock.hour = 10.5f; var p = Find(120, 420, 9, 14); b.diver.Place(p + Vector3.up * 2.6f, 80, 8); return true; }
                case "meadow": { b.clock.hour = 14f; var p = Find(80, 360, 5, 9); b.diver.Place(p + Vector3.up * 1.4f, 200, 12); return true; }
                case "kelp": { b.clock.hour = 11f; var p = Find(700, 1000, 80, 100); b.diver.Place(p + Vector3.up * 5f, 90, -6); Debug.Log($"DEEP KELP SHOT: eye {p + Vector3.up * 5f} H {bed.H(p.x, p.z):0.0} sample {bed.SampleY(p.x, p.z):0.0} far {Camera.main.farClipPlane:0}"); return true; }
                case "up": { b.clock.hour = 12f; var p = Find(150, 400, 10, 15); b.diver.Place(new Vector3(p.x, -6f, p.z), 60, -62); return true; }
                case "above": { b.clock.hour = 17.8f; b.diver.Place(new Vector3(300, 0.6f, Seabed.Size / 2), 270, 2); return true; }
                case "night": { b.clock.hour = 23.5f; b.clock.day = 14; var p = Find(120, 420, 9, 14); b.diver.Place(p + Vector3.up * 2.6f, 80, 8); return true; }
                case "drop": { b.clock.hour = 12f; var p = Find(1080, 1150, 100, 150); b.diver.Place(p + Vector3.up * 8f, 90, -18); return true; }
            }
            return false;
        }
    }
}
