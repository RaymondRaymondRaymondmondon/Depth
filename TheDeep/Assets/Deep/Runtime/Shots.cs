// The screenshot harness (like Depth's --shots): TheDeep.exe -shot <name|all> -shotdir <folder>
// Each shot places the eye, holds the clock, lets a few frames settle, saves <folder>/deep_<name>.png, then quits.
using System.Collections;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Deep
{
    public class Shots : MonoBehaviour
    {
        public static bool HideOcean;
        static readonly string[] All = { "debugdown", "debugair", "reef", "kelp", "up", "above", "night", "drop", "meadow",
            "nautilus", "nautilus_side", "nautilus_stern", "salon", "bridge", "pilothouse", "engine", "moonpool", "dark", "underway", "helm", "flooding", "sonar", "telegraph", "gauges", "life_reef", "life_school", "life_kelp", "life_night", "leviathan", "crew", "cabin", "ladder_top", "life_blood", "life_starve", "life_coral", "life_light", "life_engine", "life_moon", "wreck_galleon", "wreck_dreadnought", "deposit", "craft", "pack", "kitesub", "kitesub_fly", "opening_raft", "opening_board", "opening_storm", "opening_hunt", "crew_sea", "crew_aboard", "crew_raft", "decor_cabin", "decor_salon", "decor_ghost", "drop_satchel", "captains_log", "cave_city", "cave_hall", "cave_tunnel", "cave_sinkhole", "cave_mouth", "cave_pocket", "cave_life", "cave_dive", "vents_field", "vents_smoker", "vents_geyser", "vents_rift", "vents_worm", "vents_life" };

        public void Run(string which, string dir) { StartCoroutine(Go(which, dir)); }

        IEnumerator Go(string which, string dir)
        {
            Directory.CreateDirectory(dir);
            var boot = DeepBoot.I; boot.diver.inputEnabled = false; boot.clock.frozen = true;
            var list = which == "all" ? All : which.Split(',');
            yield return null;
            while (!Bank.Ready) yield return null;
            foreach (var name in list)
            {
                bool ok;
                var ui0 = boot.diver.GetComponent<CraftUI>(); if (ui0 != null && ui0.mode != null) ui0.Close();
                if (boot.diver.piloting != null) boot.diver.LeaveKiteSub(true);
                try { ok = Setup(name, boot); }
                catch (System.Exception ex) { Debug.LogError("DEEP SHOT: " + name + " failed: " + ex); continue; }
                if (!ok) { Debug.LogWarning("DEEP SHOT: unknown " + name); continue; }
                boot.diver.toast = "";
                for (int i = 0; i < 20; i++) yield return null;
                float ft0 = Time.realtimeSinceStartup;
                for (int i = 0; i < 20; i++) yield return null;
                float shotFps = 20f / Mathf.Max(1e-3f, Time.realtimeSinceStartup - ft0);
                if (name.StartsWith("listen_"))
                {
                    // listen: six seconds of what the ear hears, to a WAV
                    var cam = Camera.main; var rec = cam.GetComponent<Recorder>(); if (!rec) rec = cam.gameObject.AddComponent<Recorder>();
                    for (int i = 0; i < 60; i++) yield return null;
                    rec.Begin();
                    float t0 = Time.realtimeSinceStartup;
                    while (Time.realtimeSinceStartup - t0 < 6f) yield return null;
                    string wav = Path.Combine(dir, "deep_" + name + ".wav");
                    Debug.Log($"DEEP LISTEN: {name}: {rec.End(wav)}; music {Score.I.mood}");
                }
                string path = Path.Combine(dir, "deep_" + name + ".png");
                ScreenCapture.CaptureScreenshot(path);
                yield return null; yield return null;
                Debug.Log($"DEEP SHOT: {path} ({shotFps:0} fps)");
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
                case "listen_reef":
                case "listen_kelp":
                case "listen_engine":
                case "listen_breach":
                case "listen_storm":
                case "listen_danger":
                {
                    b.diver.inputEnabled = true;    // (the regulator breathes only for a diver in play)
                    Score.I.Kick();
                    if (name == "listen_reef") { b.clock.hour = 11f; Power(false); var p = Find(140, 400, 8, 25); Look(p + Vector3.up * 3f, p + new Vector3(10, 1, 4)); b.life.WarmUp(p, 20f); }
                    if (name == "listen_kelp") { b.clock.hour = 11f; Power(false); var p = Find(700, 1000, 80, 100); Look(p + Vector3.up * 5f, p + new Vector3(10, 4, 2)); b.life.WarmUp(p, 20f); }
                    if (name == "listen_engine") { b.clock.hour = 11f; Power(true); ship.telegraph = 4; for (int i = 0; i < 300; i++) ship.Sail(0.05f); b.diver.PlaceAboard(-17f, 0.5f, Nautilus.Floor, 186f, 4f); }
                    if (name == "listen_breach") { b.clock.hour = 11f; Power(true); ship.telegraph = 2; ship.sys.AddBreach(ship.sys.RoomIndexAt(new Vector3(12f, 0, 0)), 0.05f); b.diver.PlaceAboard(14f, 0.4f, Nautilus.Floor, 90f, 0f); }
                    if (name == "listen_storm")
                    {
                        Opening.Begin(b); Opening.I.t = 520f; b.weather.storm = 0.9f; b.weather.target = 0.9f; b.weather.flash = 1f;
                        b.diver.yaw = 30f; b.diver.pitch = 5f;
                    }
                    if (name == "listen_danger")
                    {
                        b.clock.hour = 11f; Power(false);
                        var p = Find(140, 400, 8, 25); Look(p + Vector3.up * 3f, p + new Vector3(10, 1, 4));
                        Creature hunter = null; foreach (var c in b.life.live) if (c.alive && c.sp.level >= 2 && c.size > 1.5f && !c.persistent) { hunter = c; break; }
                        if (hunter == null) foreach (var c in b.life.live) if (c.alive && c.persistent) { hunter = c; break; }
                        if (hunter != null) { hunter.forced = true; hunter.pos = b.diver.EyeWorld + new Vector3(12, 0, 6); hunter.state = CState.Hunting; hunter.goal = b.diver.EyeWorld; }
                    }
                    return true;
                }
                case "cave_city":
                case "cave_hall":
                case "cave_tunnel":
                case "cave_sinkhole":
                case "cave_mouth":
                case "cave_pocket":
                {
                    b.clock.hour = 11f; Power(false);
                    var cv = Caverns.I;
                    if (name == "cave_city" || name == "cave_hall")
                    {
                        var ch = name == "cave_city" ? cv.City : cv.Biggest(true);
                        var eye = ch.c + new Vector3(-ch.r.x * 0.6f, -ch.r.y * 0.1f, -ch.r.z * 0.3f);
                        Look(eye, ch.c + new Vector3(ch.r.x * 0.4f, -ch.r.y * 0.3f, ch.r.z * 0.2f));
                    }
                    else if (name == "cave_tunnel")
                    {
                        var r = new System.Random(3); Vector3 at;
                        var ch = cv.Biggest(true);
                        if (!cv.FreePoint(ch.c, ch.r.magnitude * 0.9f, ch.r.magnitude * 1.6f, 2.5f, r, out at)) at = ch.c;
                        var dir = -cv.Grad(at); Look(at, at + new Vector3(dir.z, 0, -dir.x) * 10f);
                    }
                    else if (name == "cave_sinkhole")
                    {
                        var m = cv.mouths[0];
                        Look(m + new Vector3(14f, 10f, 8f), m + Vector3.down * 6f);
                    }
                    else if (name == "cave_mouth")
                    {
                        var m = cv.mouths[cv.mouths.Count - 1];
                        Look(m + new Vector3(22f, 4f, 6f), m + new Vector3(-8f, -2f, 0f));
                    }
                    else
                    {
                        Caverns.Chamber pk = null; foreach (var c in cv.chambers) if (c.air) pk = c;
                        Look(new Vector3(pk.c.x - pk.r.x * 0.5f, pk.level + 1.2f, pk.c.z - pk.r.z * 0.4f), new Vector3(pk.c.x + pk.r.x * 0.3f, pk.level + 3f, pk.c.z));
                    }
                    b.diver.lampOn = true;
                    if (name != "cave_sinkhole" && name != "cave_mouth") b.life.WarmUp(b.diver.EyeWorld, 15f);
                    return true;
                }
                case "vents_field":
                case "vents_smoker":
                case "vents_geyser":
                case "vents_rift":
                case "vents_worm":
                case "vents_life":
                case "listen_vents":
                {
                    b.clock.hour = 11f; Power(false);
                    var vf = VentField.I;
                    VentField.Vent smoker = null, geyser = null;
                    foreach (var v in vf.vents) { if (v.geyser) { if (geyser == null) geyser = v; } else if (smoker == null || v.height > smoker.height) smoker = v; }
                    if (name == "vents_field")
                    {
                        // a cluster of chimneys from 45 m off: the field lit by its own glow
                        var at = smoker.pos;
                        Look(at + new Vector3(-38f, smoker.height * 0.8f + 6f, -26f), at + Vector3.up * smoker.height * 0.35f);
                    }
                    else if (name == "vents_smoker" || name == "vents_life" || name == "listen_vents")
                    {
                        var top = smoker.pos + Vector3.up * smoker.height;
                        Look(smoker.pos + new Vector3(16f, smoker.height * 0.6f + 4f, -12f), top - Vector3.up * smoker.height * 0.4f);
                        if (name != "vents_smoker") { b.life.WarmUp(b.diver.EyeWorld, 25f); LogLife(b); }
                        // the heat test: a diver in the plume cooks; the Nautilus over the vents heats unless shielded
                        float h = vf.HeatAt(top + Vector3.up * 5f), far = vf.HeatAt(top + new Vector3(40f, 0, 40f));
                        Debug.Log($"DEEP VENTTEST: {(h > 30f && far < 8f ? "PASS" : "FAIL")} heat over the smoker {h:0} C above the sea, 40 m off {far:0}");
                        int b0 = ship.sys.breaches.Count;
                        ship.sys.heatStress = 0; ship.crushDepth = 300f; for (int i = 0; i < 40; i++) ship.sys.Heat(60f, 0.5f);
                        int split = ship.sys.breaches.Count - b0; float unshielded = ship.sys.heatStress;
                        ship.sys.heatStress = 0; ship.crushDepth = 500f; for (int i = 0; i < 40; i++) ship.sys.Heat(60f, 0.5f);
                        Debug.Log($"DEEP VENTTEST: {(split > 0 && ship.sys.heatStress == 0 && ship.sys.breaches.Count - b0 == split ? "PASS" : "FAIL")} 20 s over a vent unshielded splits {split} seam(s) (stress {unshielded:0.00}); with Thermal Hull Shielding nothing ({ship.sys.heatStress:0.00})");
                        foreach (var br in new System.Collections.Generic.List<ShipSystems.Breach>(ship.sys.breaches)) ship.sys.DropBreach(br);
                        ship.crushDepth = 30f; ship.sys.heatStress = 0; ship.sys.alert = null;
                        if (name == "listen_vents") { b.diver.inputEnabled = true; Score.I.Kick(); }
                    }
                    else if (name == "vents_geyser")
                    {
                        // make it erupt now (its clock is the sea's)
                        geyser.phase = Mathf.Repeat(geyser.period - 3f - Waves.T, geyser.period);
                        Look(geyser.pos + new Vector3(30f, 14f, -26f), geyser.pos + Vector3.up * 20f);
                        Debug.Log($"DEEP VENTTEST: {(vf.TurbulenceAt(geyser.pos + Vector3.up * 10f).magnitude > 1f ? "PASS" : "FAIL")} an eruption's turbulence ({vf.TurbulenceAt(geyser.pos + Vector3.up * 10f).magnitude:0.0})");
                    }
                    else if (name == "vents_rift")
                    {
                        float x = VentField.X0 + 300f, z = vf.RiftZ(x);
                        var eye = new Vector3(x, vf.HeightAt(x, z) + 9f, z);
                        Look(eye, new Vector3(x + 60f, vf.HeightAt(x + 60f, vf.RiftZ(x + 60f)) + 2f, vf.RiftZ(x + 60f)));
                    }
                    else
                    {
                        var w = vf.worm;
                        if (w == null) { Debug.Log("DEEP VENTTEST: FAIL no Boiler Worm"); return true; }
                        var eye = w.lair + new Vector3(14f, 6f, -10f);
                        Look(eye, w.lair + Vector3.up * 10f);
                        // something loud swims by: the worm should warn, then erupt
                        b.diver.vel = Vector3.forward * 3f;
                        w.cool = 0; w.state = 0;
                        var start = w.c.pos;
                        Debug.Log($"DEEP VENTTEST: the worm waits at {start} (lair {w.lair})");
                        int seen = 0;
                        for (int i = 0; i < 600 && !(w.state == 2 && w.t > 0.9f); i++) { b.diver.vel = Vector3.forward * 3f; vf.WormStep(1f / 60f); seen |= 1 << w.state; }
                        Debug.Log($"DEEP VENTTEST: {((seen & 6) == 6 ? "PASS" : "FAIL")} the worm warned and erupted (states seen {seen}); it rose {(w.c.pos - start).magnitude:0.0} m out of the rock");
                        b.diver.vel = Vector3.zero;
                    }
                    return true;
                }
                case "death_cold":
                {
                    // the playtest's crash: a diver dies of the cold in the water with things in the pack
                    b.clock.hour = 11f; Power(true);
                    var p = Find(140, 400, 8, 25); Look(p + Vector3.up * 3f, p + new Vector3(10, 1, 4));
                    var h = b.diver.GetComponent<Hands>(); var ore = ItemDB.Get("Titanium Ore"); h.pack.Add(ore, 3);
                    b.diver.inputEnabled = true;
                    var sv = b.diver.GetComponent<Survival>(); sv.bodyC = 20f; b.diver.health = 0.5f;
                    int drops0 = Drops.I.drops.Count;
                    for (int i = 0; i < 10 && b.diver.health < 50f; i++) b.diver.Hurt(5f, "the cold");
                    Debug.Log($"DEEP DEATHTEST: {(Drops.I.drops.Count == drops0 + 1 && b.diver.aboard && b.diver.health >= 50f ? "PASS" : "FAIL")} died of the cold: a satchel left ({Drops.I.drops.Count - drops0}), woke aboard ({b.diver.aboard}), health {b.diver.health:0}");
                    b.diver.inputEnabled = false; sv.bodyC = 37f;
                    return true;
                }
                case "cave_dive":
                {
                    // swim down the sinkhole: does the diver get through the Kelp's floor into the caves? (and breathe in a pocket)
                    b.clock.hour = 11f; Power(false);
                    var cv = Caverns.I; var m = cv.mouths[0];
                    b.diver.Place(m + Vector3.up * 6f, 0, 80f);
                    b.diver.inputEnabled = true; b.diver.SimHold = KeyCode.C;
                    float startY = b.diver.EyeWorld.y, ground = b.seabed.HeightAt(m.x, m.z);
                    for (int i = 0; i < 600; i++) { b.diver.SimStep(1f / 60f); Physics.SyncTransforms(); }
                    b.diver.SimHold = KeyCode.None; b.diver.inputEnabled = false;
                    var eye = b.diver.EyeWorld;
                    {
                        var td = b.seabed.terrain.terrainData; int hr = td.holesResolution;
                        int hi = Mathf.FloorToInt(eye.x / Seabed.Size * hr), hj = Mathf.FloorToInt(eye.z / Seabed.Size * hr);
                        var holes = td.GetHoles(hi - 2, hj - 2, 5, 5); int open = 0; foreach (var hv in holes) if (!hv) open++;
                        bool hit = Physics.Raycast(eye, Vector3.down, out var rh, 20f);
                        Debug.Log($"DEEP CAVETEST: under the diver: sdf at the ground {cv.Sdf(new Vector3(eye.x, ground - 0.6f, eye.z)):0.0}, {open}/25 hole cells round it, ray down hits {(hit ? rh.collider.name + " at " + rh.point.y.ToString("0.0") : "nothing")}");
                    }
                    Debug.Log($"DEEP CAVETEST: {(eye.y < ground - 8f && cv.InCave(eye) ? "PASS" : "FAIL")} swam down the sinkhole from {startY:0.0} to {eye.y:0.0} (the Kelp's floor at {ground:0.0}; in the caves: {cv.InCave(eye)})");
                    Caverns.Chamber pk = null; foreach (var c in cv.chambers) if (c.air) pk = c;
                    var air = new Vector3(pk.c.x, pk.level + 1.2f, pk.c.z);
                    Debug.Log($"DEEP CAVETEST: {(Sea.SurfaceAt(air) < air.y ? "PASS" : "FAIL")} an air pocket's surface at {Sea.SurfaceAt(air):0.0} under an eye at {air.y:0.0}");
                    Look(eye, eye + Vector3.down * 10f + Vector3.forward * 4f);
                    return true;
                }
                case "cave_life":
                case "listen_caves":
                {
                    b.clock.hour = 11f; Power(false);
                    var ch = Caverns.I.Biggest(true);
                    var eye = ch.c + new Vector3(-ch.r.x * 0.35f, 0, 0);
                    Look(eye, ch.c + new Vector3(ch.r.x * 0.3f, -ch.r.y * 0.25f, ch.r.z * 0.1f));
                    b.life.WarmUp(eye, 30f);
                    LogLife(b);
                    if (name == "listen_caves") { b.diver.inputEnabled = true; Score.I.Kick(); }
                    return true;
                }
                case "crew_sea":
                case "crew_aboard":
                case "crew_raft":
                {
                    // crewmates as another PC draws them (Mate.cs), posed by hand
                    foreach (var mm in Net.I.mates.Values) if (mm) Object.Destroy(mm.gameObject);
                    Net.I.mates.Clear();
                    Mate Crewmate(int seat, string nm, MatePose p)
                    {
                        var m = Mate.Make(seat, nm); p.health = 100; p.seat = (byte)seat; if (p.station == 0 && p.mode != 1) p.station = 255;
                        m.Heard(Time.time - 0.5f, p); m.Heard(Time.time, p);
                        Net.I.mates[seat] = m; return m;
                    }
                    if (name == "crew_sea")
                    {
                        b.clock.hour = 10.5f; Power(false);
                        var at = ship.WorldPoint(Nautilus.G(-6, -1, 11));
                        Look(at + new Vector3(0, 0.5f, -6.5f), at + new Vector3(0, -0.4f, 0));
                        var f = b.diver.cam.transform.forward; var rgt = b.diver.cam.transform.right;
                        Crewmate(1, "Ann", new MatePose { mode = 0, pos = at - rgt * 1.6f, yaw = Mathf.Atan2(rgt.x, rgt.z) * Mathf.Rad2Deg, vel = rgt * 2.4f, flags = MatePose.Lamp, station = 255 });
                        Crewmate(2, "Bo", new MatePose { mode = 0, pos = at + rgt * 1.6f + Vector3.up * 0.4f, yaw = Mathf.Atan2(-f.x, -f.z) * Mathf.Rad2Deg, vel = Vector3.zero, flags = MatePose.Lamp, station = 255 });
                        Crewmate(3, "Cy", new MatePose { mode = 0, pos = at + f * 4f - rgt * 0.5f + Vector3.down * 0.6f, yaw = Mathf.Atan2(-rgt.x, -rgt.z) * Mathf.Rad2Deg + 30f, pitch = 20f, vel = Quaternion.Euler(20f, Mathf.Atan2(-rgt.x, -rgt.z) * Mathf.Rad2Deg + 30f, 0) * Vector3.forward * 3.5f, flags = MatePose.Sprint, station = 255 });
                    }
                    else if (name == "crew_aboard")
                    {
                        b.clock.hour = 11f; Power(true);
                        b.diver.PlaceAboard(20.5f, 0.2f, Nautilus.Floor, 180f, 6f);
                        float y = Nautilus.Floor + 0.82f;
                        Crewmate(1, "Ann", new MatePose { mode = 1, pos = Nautilus.G(17.2f, y, 0.4f), yaw = 90f, vel = Nautilus.G(0, 0, 0) + new Vector3(0, 0, 0), station = 255 });
                        Crewmate(2, "Bo", new MatePose { mode = 1, pos = Nautilus.G(15.4f, y, -0.9f), yaw = 70f, vel = new Vector3(0, 0, 2.2f), station = 255 });
                    }
                    else
                    {
                        b.clock.hour = 17.8f; Power(false);
                        var at = ship.WorldPoint(Nautilus.G(4, 0, 22)); at.y = 0;
                        var raft = Raft.I ? Raft.I : Raft.Spawn(at, ship.heading + 90f);
                        raft.transform.position = at; raft.heading = ship.heading + 90f; raft.flipped = false;
                        for (int i = 0; i < 30; i++) { raft.Oars(1, 1, 0); raft.Oars(3, 1, 0); }
                        var hf = Quaternion.Euler(0, raft.heading, 0);
                        Look(at + hf * new Vector3(4.5f, 1.8f, 3.5f), at + Vector3.up * 0.4f);
                        Crewmate(1, "Ann", new MatePose { mode = 2, pos = raft.Seat(1), yaw = raft.heading, flags = MatePose.Row, station = 255 });
                        Crewmate(3, "Cy", new MatePose { mode = 2, pos = raft.Seat(3), yaw = raft.heading, flags = MatePose.Row, station = 255 });
                    }
                    return true;
                }
                case "decor_cabin":
                case "decor_salon":
                case "decor_ghost":
                case "drop_satchel":
                case "captains_log":
                {
                    b.clock.hour = 11f; Power(true);
                    foreach (var id in new List<int>(Decor.I.placed.Keys)) Decor.I.Remove(id);
                    Decor.I.CancelPlace();
                    void Put(string item, float gx, float gy, float gz, float yaw) => Decor.I.Add(Decor.I.nextId++, item, Nautilus.G(gx, gy, gz), Quaternion.Euler(0, yaw, 0));
                    if (name == "decor_cabin")
                    {
                        var cb = ship.L.cabins[1]; bool plus = cb.z0 > 0;
                        float zo = plus ? cb.z1 : cb.z0, zi = plus ? cb.z0 : cb.z1, inward = plus ? -90f : 90f, cx = (cb.x0 + cb.x1) / 2;
                        var crewRoom = ship.L.Room("crew");
                        Put("woven_kelp_rug", cx, Nautilus.Floor, (zo + zi) / 2, 90f);
                        Put("dreadnought_brass_locker", cb.x1 - 0.45f, Nautilus.Floor, zo - Mathf.Sign(zo) * 0.32f, inward);
                        Put("phosphor_mat_chandelier", cx, crewRoom.ceil, (zo + zi) / 2, 0);
                        Put("bioluminescent_wall_planter", cx - 0.2f, Nautilus.Floor + 0.6f, zo - Mathf.Sign(zo) * 0.02f, inward);
                        Put("reef_snapper_jaw_mount", cb.x0 + 0.02f, Nautilus.Floor, (zo + zi) / 2 + Mathf.Sign(zo) * 0.3f, 0f);
                        Put("dreadnought_steam_gauge", cb.x1 - 0.02f, Nautilus.Floor, (zo + zi) / 2 - Mathf.Sign(zo) * 0.2f, 180f);
                        Put("grazer_blubber_lounge_chair", cb.x1 - 0.6f, Nautilus.Floor, (zo + zi) / 2 - Mathf.Sign(zo) * 0.1f, 200f);
                        b.diver.PlaceAboard(cb.door[0], cb.door[1] * 0.4f, Nautilus.Floor, Mathf.Sign(cb.door[1]) > 0 ? 70f : -70f, 14f);
                    }
                    else if (name == "decor_salon" || name == "captains_log")
                    {
                        float F = Nautilus.Floor;
                        Put("restored_galleon_harpsichord", 17.0f, F, -1.15f, -90f);
                        Put("captains_armillary_sphere", 16.2f, F, 1.25f, 0f);
                        Put("jelly_bioluminescence_tube", 14.6f, F, -1.3f, 0f);
                        Put("echo_ray_specimen_tank", 15.2f, F, 1.3f, 90f);
                        Put("abyssal_brew_keg", 13.6f, F, 1.25f, 0f);
                        Put("ancient_masonry_pedestal", 18.2f, F, 1.35f, 0f);
                        Put("iron_kelp_bonsai", 18.2f, F + 1.07f, 1.35f, 30f);
                        Put("captains_log_desk", 19.0f, F, -1.2f, 90f);
                        Put("lantern_vine_desk_lamp", 18.7f, F + 0.79f, -1.35f, 0f);
                        b.diver.PlaceAboard(21.0f, 0.2f, F, 180f, 8f);
                        if (name == "captains_log")
                        {
                            foreach (var s in SpeciesBook.All) if (s.biome != "kelp" && Campaign.I.met.Count < 23) Campaign.I.met.Add(s.e.name);
                            Campaign.I.deepest = 41f; Decor.I.ShowLog();
                        }
                    }
                    else if (name == "decor_ghost")
                    {
                        var bedIt = ItemDB.Get("galleon_captains_bed");
                        b.diver.GetComponent<Hands>().pack.Add(bedIt);
                        b.diver.PlaceAboard(21.0f, 0.2f, Nautilus.Floor, 180f, 32f);
                        b.diver.uiOpen = false;
                        Decor.I.BeginPlace(bedIt);
                    }
                    else
                    {
                        b.clock.hour = 10.2f; Power(false);
                        var at = ship.WorldPoint(Nautilus.G(-14, -1, 14));
                        var items = new Dictionary<string, int> { { "titanium_ore", 5 }, { "survival_knife", 1 } };
                        foreach (var id in new List<int>(Drops.I.drops.Keys)) Drops.I.Remove(id);
                        var dr = Drops.I.Add(Drops.I.nextId++, Net.I.myName, false, at, items);
                        var w = Drops.I.World(dr);
                        Look(w + new Vector3(3.2f, 1.6f, -3.2f), w + Vector3.up * 0.3f);
                    }
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
