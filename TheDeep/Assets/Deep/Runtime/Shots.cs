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
            "nautilus", "nautilus_side", "nautilus_stern", "salon", "bridge", "pilothouse", "engine", "moonpool", "dark", "underway", "helm" };

        public void Run(string which, string dir) { StartCoroutine(Go(which, dir)); }

        IEnumerator Go(string which, string dir)
        {
            Directory.CreateDirectory(dir);
            var boot = DeepBoot.I; boot.diver.inputEnabled = false; boot.clock.frozen = true;
            var list = which == "all" ? All : which.Split(',');
            yield return null;
            foreach (var name in list)
            {
                if (!Setup(name, boot)) { Debug.LogWarning("DEEP SHOT: unknown " + name); continue; }
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
            void Power(bool on) { ship.power = on; ship.powerK = on ? 1 : 0; }
            switch (name)
            {
                case "nautilus": { b.clock.hour = 11f; Power(false); Look(ship.WorldPoint(Nautilus.G(34, 6, 24)), ship.WorldPoint(Nautilus.G(6, 0, 0))); return true; }
                case "nautilus_side": { b.clock.hour = 19.2f; Power(true); Look(ship.WorldPoint(Nautilus.G(12, 1.5f, 15)), ship.WorldPoint(Nautilus.G(12, 0.5f, 0))); return true; }
                case "nautilus_stern": { b.clock.hour = 10f; Power(false); Look(ship.WorldPoint(Nautilus.G(-46, 4, -14)), ship.WorldPoint(Nautilus.G(-20, 0, 0))); return true; }
                case "salon": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(12.5f, 1.2f, Nautilus.Floor, -90f, 4f); return true; }
                case "bridge": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(19.3f, 0.6f, Nautilus.Floor, 8f, 6f); return true; }
                case "pilothouse": { b.clock.hour = 11f; Power(true); b.diver.PlaceAboard(21.75f, 0f, 3.6f, 0f, 8f); return true; }
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
                    b.diver.pitch = 4; b.diver.head.localRotation = Quaternion.Euler(4, 0, 0);
                    return true;
                }
                case "dark": { b.clock.hour = 11f; Power(false); b.diver.lampOn = true; b.diver.PlaceAboard(7.0f, 0.3f, Nautilus.Floor, 0f, 2f); return true; }
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
