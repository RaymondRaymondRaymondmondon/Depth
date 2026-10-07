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
            "nautilus", "nautilus_side", "nautilus_stern", "salon", "bridge", "pilothouse", "engine", "moonpool", "dark", "underway", "helm", "flooding", "sonar", "telegraph", "gauges", "life_reef", "life_school", "life_kelp", "life_night", "leviathan", "crew", "cabin", "ladder_top", "life_blood", "life_starve", "life_coral" };

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
                ship.sys.state = on ? PowerState.Engine : PowerState.Dead; ship.sys.fuel = 0.6f; ship.sys.battery = 0.6f;
                ship.power = on; ship.powerK = on ? 1 : 0;
            }
            switch (name)
            {
                case "nautilus": { b.clock.hour = 11f; Power(false); Look(ship.WorldPoint(Nautilus.G(34, 6, 24)), ship.WorldPoint(Nautilus.G(6, 0, 0))); return true; }
                case "nautilus_side": { b.clock.hour = 19.2f; Power(true); Look(ship.WorldPoint(Nautilus.G(12, 1.5f, 15)), ship.WorldPoint(Nautilus.G(12, 0.5f, 0))); return true; }
                case "nautilus_stern": { b.clock.hour = 10f; Power(false); Look(ship.WorldPoint(Nautilus.G(-46, 4, -14)), ship.WorldPoint(Nautilus.G(-20, 0, 0))); return true; }
                case "salon": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(20.5f, 1.2f, Nautilus.Floor, -90f, 4f); return true; }
                case "bridge": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(27.3f, 0.6f, Nautilus.Floor, 8f, 6f); return true; }
                case "pilothouse": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(Nautilus.ShaftX + 0.8f, 0f, 3.6f, 0f, 8f); return true; }
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
                case "life_coral":
                {
                    // the densest coral within the Shallows: the reef's life gathers there
                    b.clock.hour = 10.5f;
                    var pts = new System.Collections.Generic.List<Vector3>();
                    Vector3 best = Find(150, 420, 10, 18); int most = -1;
                    for (int k = 0; k < 300; k++)
                    {
                        var q = Find(100 + (k * 37) % 400, 140 + (k * 37) % 400, 6, 22);
                        b.flora.Near(q, 0, 12, pts, 400, "tablecoral", "braincoral", "spirecoral", "fananemone");
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
                case "kelp": { b.clock.hour = 11f; var p = Find(700, 1000, 80, 100); b.diver.Place(p + Vector3.up * 5f, 90, -6); return true; }
                case "up": { b.clock.hour = 12f; var p = Find(150, 400, 10, 15); b.diver.Place(new Vector3(p.x, -6f, p.z), 60, -62); return true; }
                case "above": { b.clock.hour = 17.8f; b.diver.Place(new Vector3(300, 0.6f, Seabed.Size / 2), 270, 2); return true; }
                case "night": { b.clock.hour = 23.5f; b.clock.day = 14; var p = Find(120, 420, 9, 14); b.diver.Place(p + Vector3.up * 2.6f, 80, 8); return true; }
                case "drop": { b.clock.hour = 12f; var p = Find(1080, 1150, 100, 150); b.diver.Place(p + Vector3.up * 8f, 90, -18); return true; }
            }
            return false;
        }
    }
}
