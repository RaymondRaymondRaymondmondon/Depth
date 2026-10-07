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
        static readonly string[] All = { "debugdown", "debugair", "reef", "kelp", "up", "above", "night", "drop", "meadow" };

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
            switch (name)
            {
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
