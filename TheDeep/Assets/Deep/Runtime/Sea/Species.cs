// The species tables (Resources/Data/species_<biome>.json, extracted from the design doc's rosters) and what the
// simulation derives from each entry: speeds, senses, body plan, colours and where in the water it lives.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    [Serializable]
    public class FloraEntry { public string id, name, type, description, yields, use, habitat; public int tier; public string[] eatenBy; }

    [Serializable]
    public class FaunaEntry
    {
        public string id, name, role, kind, description, habitat, activity, light, tactics, yields, sound;
        public int level; public float sizeM, depthMin, depthMax, aggression; public bool schooling, lethalToDiver;
        public string[] diet, predators, hearing, estimated;
        public int[] groupSize;
    }

    [Serializable]
    public class BiomeTable
    {
        public string biome, name, layout, look, hazards, foodWeb;
        public float depthMin, depthMax, absorptionDbPerM;
        public FloraEntry[] flora; public FaunaEntry[] fauna; public string[] interactions;
    }

    // a species as the simulation uses it
    public class SpeciesDef
    {
        public int index; public string biome;
        public FaunaEntry e;
        public string Name => e.name;
        public int level;                 // trophic: 1 grazer, 2 hunter, 3 apex, 4 decomposer
        public string kind;
        public float size, cruise, burst, turn, perceive, mass;
        public uint bands;                // hearing (Acoustics band bits)
        public bool bottom, surface, schooling, night, day, lethal;
        public int groupMin, groupMax;
        public float aggression;
        public Color top, belly, accent;
        public HashSet<string> diet = new HashSet<string>();
        public float density;             // individuals per 10,000 m^2 of suitable water at the balance

        public bool plankton;             // part of the pools, not spawned as animals
        public bool resident;             // the biome's leviathan: always alive in its territory
        public bool Eats(SpeciesDef s) => diet.Contains(s.e.name);
        public bool IsLeviathan => level == 3 && size >= 25f;
    }

    public static class SpeciesBook
    {
        public static readonly List<SpeciesDef> All = new List<SpeciesDef>();
        public static readonly List<BiomeTable> Tables = new List<BiomeTable>();

        public static void Load()
        {
            if (All.Count > 0) return;
            foreach (var b in new[] { "shallows", "kelp" })
            {
                var t = Resources.Load<TextAsset>("Data/species_" + b);
                if (!t) { Debug.LogWarning("DEEP SEA: no species table for " + b); continue; }
                BiomeTable tab;
                try { tab = JsonUtility.FromJson<BiomeTable>(t.text); }
                catch (Exception ex) { Debug.LogError("DEEP SEA: bad species table " + b + ": " + ex.Message); continue; }
                Tables.Add(tab);
                if (tab.fauna == null) continue;
                foreach (var f in tab.fauna) All.Add(Derive(f, tab));
            }
            for (int i = 0; i < All.Count; i++) All[i].index = i;
            // each biome's resident leviathan is its biggest (the Reef-Crusher, the Tangle-Serpent); any other giant
            // of the tables roams rarely
            foreach (var tab in Tables)
            {
                SpeciesDef big = null;
                foreach (var s in All) if (s.biome == tab.biome && s.IsLeviathan && (big == null || s.size > big.size)) big = s;
                if (big != null) big.resident = true;
            }
            Debug.Log($"DEEP SEA: {All.Count} species from {Tables.Count} tables");
        }

        static float Hash(string s, int k) { unchecked { int h = 17 + k * 31; foreach (char c in s) h = h * 31 + c; return (h & 0xffff) / 65535f; } }

        static SpeciesDef Derive(FaunaEntry f, BiomeTable tab)
        {
            var s = new SpeciesDef { e = f, biome = tab.biome, level = Mathf.Clamp(f.level, 1, 4), kind = string.IsNullOrEmpty(f.kind) ? "fish" : f.kind };
            s.size = Mathf.Clamp(f.sizeM > 0 ? f.sizeM : 0.4f, 0.03f, 80f);
            // the sessile and creeping things the tables call "other": shells, stars and cucumbers live on the bottom
            string ln = f.name.ToLowerInvariant();
            bool shell = ln.Contains("barnacle") || ln.Contains("mussel") || ln.Contains("clam") || ln.Contains("scallop") || ln.Contains("tunicate");
            bool star = ln.Contains("star") || ln.Contains("brittle");
            bool cuke = ln.Contains("cucumber");
            if (shell) { s.kind = "snail"; f.habitat = "seabed"; }
            else if (star) { s.kind = "octopus"; f.habitat = "seabed"; }
            else if (cuke) { s.kind = "worm"; f.habitat = "seabed"; }
            else if (s.kind == "other") s.kind = "fish";
            if (ln.Contains("feather duster") || ln.Contains("christmas-worm") || ln.Contains("fan worm")) f.habitat = "seabed";
            if (f.depthMax <= f.depthMin) { f.depthMin = tab.depthMin; f.depthMax = tab.depthMax; }
            // speed scales with length (body lengths a second fall as animals grow), set by body plan
            float bl;
            switch (s.kind)
            {
                case "crab": case "shrimp": case "snail": case "urchin": case "worm": bl = 0.25f; break;
                case "jelly": bl = 0.15f; break;
                case "turtle": case "mammal": bl = 0.35f; break;
                case "ray": bl = 0.6f; break;
                case "eel": bl = 0.5f; break;
                case "squid": case "octopus": bl = 0.8f; break;
                case "shark": bl = 0.55f; break;
                case "leviathan": bl = 0.18f; break;
                default: bl = 1.2f; break;
            }
            s.cruise = Mathf.Clamp(bl * Mathf.Pow(s.size, 0.55f), 0.05f, 4.5f);
            if (shell) s.cruise = 0.001f;
            s.burst = s.cruise * (s.level == 2 ? 3.2f : 2.5f);
            s.turn = Mathf.Clamp(260f / (1f + s.size), 10f, 260f);
            s.perceive = Mathf.Clamp(18f + s.size * 6f, 12f, 160f);
            s.mass = 0.02f * s.size * s.size * s.size;   // tonnes (roughly a fish's)
            s.bands = Acoustics.Bands(f.hearing);
            string h = f.habitat ?? "open";
            s.bottom = h == "seabed" || h == "sand" || h == "crevice" || h == "cave" || s.kind == "crab" || s.kind == "snail" || s.kind == "urchin" || s.kind == "worm";
            s.surface = h == "surface";
            s.schooling = f.schooling;
            s.groupMin = f.groupSize != null && f.groupSize.Length > 0 ? Mathf.Max(1, f.groupSize[0]) : 1;
            s.groupMax = f.groupSize != null && f.groupSize.Length > 1 ? Mathf.Max(s.groupMin, f.groupSize[1]) : s.groupMin;
            if (s.schooling) { s.groupMin = Mathf.Max(s.groupMin, 6); s.groupMax = Mathf.Clamp(s.groupMax, s.groupMin, 40); }
            s.groupMax = Mathf.Min(s.groupMax, 40); s.groupMin = Mathf.Min(s.groupMin, s.groupMax);
            s.night = f.activity != "day"; s.day = f.activity != "night";
            s.lethal = f.lethalToDiver;
            s.plankton = f.role == "Plankton" || s.size < 0.02f;
            s.aggression = Mathf.Clamp01(f.aggression);
            if (f.diet != null) foreach (var d in f.diet) s.diet.Add(d);
            // abundance: many small grazers, few hunters, the apex alone
            s.density = s.IsLeviathan ? 0.004f : s.level == 1 ? Mathf.Clamp(30f / (0.2f + s.size), 2f, 120f) : s.level == 2 ? Mathf.Clamp(6f / (0.5f + s.size), 0.4f, 8f) : s.level == 4 ? 6f : 0.02f;
            if (s.schooling) s.density *= 2f;
            if (s.bottom && s.size < 0.25f) s.density *= 0.12f;     // the tiny sessile things are scenery, not a crowd
            if (s.kind == "jelly") s.density *= 0.5f;
            // colours: reef species bright, open-water ones silvered and countershaded, the deep and the kelp dimmer
            float hue = Hash(f.name, 1), sat = tab.biome == "shallows" ? 0.55f + 0.35f * Hash(f.name, 2) : 0.3f + 0.3f * Hash(f.name, 2);
            float val = 0.45f + 0.4f * Hash(f.name, 3);
            if (s.kind == "shark" || s.kind == "leviathan" || h == "open") { sat *= 0.35f; val *= 0.8f; }
            s.top = Color.HSVToRGB(hue, sat, val * 0.75f);
            s.belly = Color.Lerp(s.top, new Color(0.92f, 0.9f, 0.82f), 0.7f);
            s.accent = Color.HSVToRGB(Mathf.Repeat(hue + 0.45f, 1f), Mathf.Min(1, sat + 0.25f), Mathf.Min(1, val + 0.2f));
            return s;
        }
    }
}
