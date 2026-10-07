// nautilus_layout.json, written by tools/artgen/deep_nautilus.py beside the models: where the rooms, doors, stations,
// lamps, hatches, ladders and windows are, in the generator's frame (x bow, y up, z starboard, metres about the hull's
// axis). Nautilus.G turns those into the ship's Unity frame (z bow, y up, x starboard).
using System;
using UnityEngine;

namespace Deep
{
    [Serializable] public class NLRoom { public string id, name; public float x0, x1, half, floor, ceil; }
    [Serializable] public class NLDoor { public float x, z, w, h; }
    [Serializable] public class NLStation { public string id, kind, room; public float[] pos, facing; }
    [Serializable] public class NLLamp { public string room; public float[] pos; }
    [Serializable] public class NLHatch { public string kind; public float x, y, z; public float[] inside, outside; }
    [Serializable] public class NLLadder { public float x, z, y0, y1; public string hatch; }
    [Serializable] public class NLWindow { public string room; public float x, y, z, ra, rb; }
    [Serializable] public class NLMoonpool { public float x0, x1, z0, z1, y; }
    [Serializable] public class NLLight { public string kind; public float[] pos, dir; }
    [Serializable] public class NLGauges { public float[] depth, clock; }

    [Serializable]
    public class NautilusLayout
    {
        public NLRoom[] rooms;
        public NLDoor[] doors;
        public NLStation[] stations;
        public NLLamp[] lamps;
        public NLHatch[] hatches;
        public NLLadder[] ladders;
        public NLWindow[] windows;
        public NLMoonpool moonpool;
        public NLLight[] lights;
        public float[] helmWheel;
        public NLGauges gauges, bridgeGauges;

        public static NautilusLayout Load()
        {
            var t = Resources.Load<TextAsset>("Models/nautilus_layout");
            if (!t) { Debug.LogError("DEEP: nautilus_layout.json missing"); return new NautilusLayout(); }
            return JsonUtility.FromJson<NautilusLayout>(t.text);
        }

        public NLRoom Room(string id) { foreach (var r in rooms) if (r.id == id) return r; return null; }

        // the room a point (generator frame) is in, or null
        public NLRoom RoomAt(Vector3 g)
        {
            foreach (var r in rooms)
                if (g.x >= r.x0 && g.x <= r.x1 && Mathf.Abs(g.z) <= r.half + 0.2f && g.y >= r.floor - 0.5f && g.y <= r.ceil + 0.2f) return r;
            return null;
        }
    }
}
