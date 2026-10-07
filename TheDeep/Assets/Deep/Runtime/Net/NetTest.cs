// The crew on screen (names over their helmets, the crew list, the connecting screen) and the network self-test.
//
// The self-test (tools\deep.ps1 nettest) runs a host and two crewmates as three processes on this PC over real UDP
// (127.0.0.1): the host drives a script, asking the crewmates to do things and report what they see, and checks each
// answer against its own world - the crew connecting and seated, a crewmate's swimming seen by the host, the ship
// sailed by the host seen in the same place by both, the helm worked from a crewmate's PC, a plant taken and gone on
// every PC, the shared stores, the animals near a crewmate the same on both sides, a crewmate's strike wounding the
// host's animal, a bite on a crewmate, a breach seen and patched from a crewmate's PC, the crew rowing the raft, and a
// crewmate leaving. It prints "DEEP NETTEST: n passed, m failed" and the host exits with 0 only if all passed.
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public partial class Net
    {
        // ---- what the crew sees -----------------------------------------------------------------------------------
        GUIStyle label, small;
        void OnGUI()
        {
            if (!Online) return;
            if (label == null)
            {
                label = new GUIStyle(GUI.skin.label) { fontSize = 14, alignment = TextAnchor.MiddleCenter, fontStyle = FontStyle.Bold };
                small = new GUIStyle(GUI.skin.label) { fontSize = 13 };
            }
            // connecting, failing, building
            if (!worldUp || failed)
            {
                if (worldUp && !failed) return;
                var r = new Rect(Screen.width / 2 - 300, Screen.height / 2 - 40, 600, 80);
                GUI.color = new Color(0, 0, 0, 0.75f); GUI.DrawTexture(r, Texture2D.whiteTexture); GUI.color = Color.white;
                var st = new GUIStyle(label) { fontSize = 18, wordWrap = true }; st.normal.textColor = failed ? new Color(1f, 0.7f, 0.6f) : new Color(0.75f, 0.95f, 1f);
                GUI.Label(r, status + (failed && quitAt > 0 ? $"\n(closing in {Mathf.Max(0, quitAt - Time.time):0} s)" : ""), st);
                return;
            }
            var cam = Camera.main;
            // names over helmets
            if (cam)
                foreach (var m in mates.Values)
                {
                    if (!m || !m.InWorld || m.cur.mode == 3) continue;
                    var p = m.Eye + Vector3.up * 0.55f;
                    var sp = cam.WorldToScreenPoint(p);
                    float dist = (p - cam.transform.position).magnitude;
                    if (sp.z <= 0 || dist > 80f) continue;
                    var st = new GUIStyle(label); st.normal.textColor = Mate.SuitColours[m.seat] * 1.15f;
                    GUI.Label(new Rect(sp.x - 120, Screen.height - sp.y - 12, 240, 24), m.mateName + (m.Health < 40 ? $"  ({m.Health:0})" : ""), st);
                }
            // the crew list, top right under the clock
            float y = 118;
            var head = new GUIStyle(small); head.normal.textColor = new Color(0.8f, 0.9f, 0.95f);
            GUI.Label(new Rect(Screen.width - 260, y, 250, 20), IsHost ? "Crew (you're hosting)" : "Crew", head); y += 20;
            Row(mySeat, myName + " (you)", D ? D.health : 100, D ? (D.onRaft ? "in the raft" : D.piloting ? "in the Kite-Sub" : D.aboard ? "aboard" : "in the sea") : "", ref y);
            foreach (var m in mates.Values)
                if (m) Row(m.seat, m.mateName, m.Health, !m.InWorld ? "coming aboard" : m.cur.mode == 2 ? "in the raft" : m.cur.mode == 3 ? "in the Kite-Sub" : m.cur.mode == 1 ? "aboard" : "in the sea", ref y);
        }

        void Row(int seat, string name, float hp, string where, ref float y)
        {
            GUI.color = Mate.SuitColours[Mathf.Clamp(seat, 0, 3)]; GUI.DrawTexture(new Rect(Screen.width - 260, y + 5, 10, 10), Texture2D.whiteTexture); GUI.color = Color.white;
            var st = new GUIStyle(small); st.normal.textColor = hp < 30 ? new Color(1f, 0.55f, 0.5f) : new Color(0.85f, 0.88f, 0.9f);
            GUI.Label(new Rect(Screen.width - 244, y, 240, 20), $"{name}  {hp:0}  {where}", st);
            y += 18;
        }

        // ---- the self-test ----------------------------------------------------------------------------------------
        public static bool Testing;
        int passed, failedN;
        readonly Dictionary<int, (int step, byte[] data)> reports = new Dictionary<int, (int, byte[])>();
        readonly List<(float t, Vector3 pos)> shipTrail = new List<(float, Vector3)>();

        void Check(bool ok, string what)
        {
            if (ok) passed++; else failedN++;
            Debug.Log($"DEEP NETTEST: {(ok ? "PASS" : "FAIL")} {what}");
        }

        void Ask(int seat, int step, NetW args = null)
        {
            reports.Remove(seat);
            ulong id = ulong.MaxValue; foreach (var kv in seatOf) if (kv.Value == seat) id = kv.Key;
            var w = new NetW().U8(M_TEST).U8(step);
            if (args != null) for (int i = 0; i < args.n; i++) w.U8(args.b[i]);
            if (id != ulong.MaxValue) Send(id, w, true);
        }

        IEnumerator Await(int seat, int step, float timeout = 10f)
        {
            float t0 = Time.time;
            while (Time.time - t0 < timeout && !(reports.TryGetValue(seat, out var rp) && rp.step == step)) yield return null;
        }
        NetR Report(int seat, int step) => reports.TryGetValue(seat, out var rp) && rp.step == step ? new NetR(rp.data) : null;

        void TestReport(int seat, NetR r)
        {
            int step = r.U8();
            var rest = new List<byte>(); while (r.More) rest.Add((byte)r.U8());
            reports[seat] = (step, rest.ToArray());
        }

        public void StartTest() { Testing = true; if (IsHost) StartCoroutine(HostScript()); }

        void LateUpdate()
        {
            if (Testing && IsHost && Ship) { shipTrail.Add((Time.time, Ship.Body.position)); if (shipTrail.Count > 2000) shipTrail.RemoveAt(0); }
        }
        Vector3 ShipAt(float t)
        {
            for (int i = shipTrail.Count - 1; i > 0; i--)
                if (shipTrail[i - 1].t <= t) return Vector3.Lerp(shipTrail[i - 1].pos, shipTrail[i].pos, Mathf.InverseLerp(shipTrail[i - 1].t, shipTrail[i].t, t));
            return shipTrail.Count > 0 ? shipTrail[0].pos : Vector3.zero;
        }

        IEnumerator HostScript()
        {
            var d = D; var ship = Ship;
            d.inputEnabled = false;
            Debug.Log("DEEP NETTEST: waiting for two crewmates");
            float t0 = Time.time;
            while (Time.time - t0 < 150f) { int up = 0; foreach (var m in mates.Values) if (m && m.InWorld) up++; if (up >= 2) break; yield return null; }
            var list = new List<Mate>(); foreach (var m in mates.Values) if (m && m.InWorld) list.Add(m);
            Check(list.Count >= 2, $"two crewmates connected and in the world ({list.Count})");
            if (list.Count < 2) { Finish(); yield break; }
            list.Sort((a, b) => a.seat.CompareTo(b.seat));
            var A = list[0]; var B = list[1];
            Check(A.seat != B.seat && A.seat != mySeat && B.seat != mySeat, $"seats are distinct (host {mySeat}, {A.mateName} {A.seat}, {B.mateName} {B.seat})");

            // 1. a crewmate swims; the host sees them there
            var spot = ship.Body.position + ship.Body.right * 14f + Vector3.up * 1f;
            Ask(A.seat, 1, new NetW().V3(spot));
            yield return Await(A.seat, 1);
            yield return new WaitForSeconds(1.5f);
            Check((A.Eye - spot).magnitude < 1.5f, $"the host sees {A.mateName} where they swam ({(A.Eye - spot).magnitude:0.00} m off)");
            var spotB = ship.Body.position - ship.Body.right * 12f + ship.Body.forward * 6f;
            Ask(B.seat, 1, new NetW().V3(spotB));
            yield return Await(B.seat, 1);

            // 2. the host sails her; both see her in the same place
            ship.sys.breakersTripped = false; ship.sys.state = PowerState.Silent; ship.sys.battery = 1f; ship.power = true; ship.telegraph = 5;
            yield return new WaitForSeconds(5f);
            foreach (var m in new[] { A, B })
            {
                Ask(m.seat, 2);
                yield return Await(m.seat, 2);
                var r = Report(m.seat, 2);
                if (r == null) { Check(false, $"{m.mateName} reports the ship"); continue; }
                float at = r.F(); var pos = r.V3();
                var want = ShipAt(at);
                Check((pos - want).magnitude < 1.5f && ship.speed > 0.2f, $"{m.mateName} sees her where the host had her ({(pos - want).magnitude:0.00} m; under way at {ship.speed:0.0} m/s)");
            }

            // 3. the helm from a crewmate's PC
            Ask(A.seat, 3);
            yield return Await(A.seat, 3);
            yield return new WaitForSeconds(1.2f);
            Check(Mathf.Abs(ship.depthOrder - 9f) < 0.5f && ship.telegraph == 3, $"{A.mateName} at the helm: depth order {ship.depthOrder:0.0} m, telegraph {Nautilus.TeleNames[ship.telegraph]}");
            Ask(A.seat, 4); yield return Await(A.seat, 4);
            ship.telegraph = 2;

            // 4. a plant taken on one PC is gone on all
            var flora = DeepBoot.I.flora; int before = flora.Total;
            Ask(B.seat, 5);
            yield return Await(B.seat, 5);
            yield return new WaitForSeconds(1.5f);
            Check(flora.Total == before - 1, $"a plant {B.mateName} took is gone from the host's sea ({before} -> {flora.Total})");
            Ask(A.seat, 6); yield return Await(A.seat, 6);
            var ra = Report(A.seat, 6);
            Check(ra != null && ra.I32() == flora.Total, $"and from {A.mateName}'s");

            // 5. the shared stores
            var ore = ItemDB.Get("Titanium Ore");
            int had = ore != null ? ship.store.Count(ore) : 0;
            Ask(B.seat, 7);
            yield return Await(B.seat, 7);
            yield return new WaitForSeconds(1.5f);
            Check(ore != null && ship.store.Count(ore) == had + 3, $"three ore {B.mateName} stowed are in the host's lockers ({(ore != null ? ship.store.Count(ore) : -1)})");
            Ask(A.seat, 8); yield return Await(A.seat, 8);
            var rs = Report(A.seat, 8);
            Check(rs != null && rs.I32() == had + 3, $"and {A.mateName} sees them too");

            // 6. the animals near a crewmate
            yield return new WaitForSeconds(4f);
            Ask(A.seat, 9);
            yield return Await(A.seat, 9);
            var rc = Report(A.seat, 9);
            int seen = rc != null ? rc.I32() : 0, cid = rc != null ? rc.I32() : -1; var cpos = rc != null ? rc.V3() : Vector3.zero;
            Creature hostC = null; Life.I.byId.TryGetValue(cid, out hostC);
            Check(seen > 5, $"{A.mateName} sees the animals round them ({seen})");
            Check(hostC != null && (hostC.pos - cpos).magnitude < 6f, $"the same animal in the same place on both PCs ({(hostC != null ? (hostC.pos - cpos).magnitude : -1):0.0} m)");

            // 7. a crewmate's strike wounds the host's animal
            if (hostC != null)
            {
                float hp = hostC.health;
                Ask(A.seat, 10, new NetW().I32(cid));
                yield return Await(A.seat, 10);
                yield return new WaitForSeconds(1f);
                Check(!hostC.alive || hostC.health < hp, $"{A.mateName}'s strike wounds the {hostC.sp.e.name} ({hp:0.00} -> {(hostC.alive ? hostC.health : 0):0.00})");
            }
            else Check(false, "a strike on the shared animal");

            // 8. a bite on a crewmate
            Ask(B.seat, 11); yield return Await(B.seat, 11);
            float hb = Report(B.seat, 11)?.F() ?? -1;
            HurtMate(B, 10f, "a test bite");
            yield return new WaitForSeconds(1f);
            Ask(B.seat, 11); yield return Await(B.seat, 11);
            float ha = Report(B.seat, 11)?.F() ?? -1;
            Check(hb > 0 && ha <= hb - 9.9f, $"{B.mateName} feels the bite ({hb:0} -> {ha:0})");

            // 9. a breach seen and patched from a crewmate's PC
            ship.sys.AddBreach(2, 0.05f);
            yield return new WaitForSeconds(1f);
            Ask(A.seat, 12); yield return Await(A.seat, 12);
            var rb = Report(A.seat, 12);
            Check(rb != null && rb.I32() == 1, $"{A.mateName} sees the breach");
            Ask(A.seat, 13); yield return Await(A.seat, 13);
            yield return new WaitForSeconds(1.5f);
            Check(ship.sys.breaches.Count == 0, $"{A.mateName} patches it ({ship.sys.breaches.Count} left)");

            // 10. the raft, rowed by a crewmate
            if (Raft.I == null) Raft.Spawn(spotB + Vector3.right * 3f, 0f);
            var r0 = Raft.I.transform.position; r0.y = 0;
            Ask(B.seat, 14);
            yield return Await(B.seat, 14);
            yield return new WaitForSeconds(4.5f);
            var r1 = Raft.I.transform.position; r1.y = 0;
            Check((Raft.I.occupied & (1 << B.seat)) != 0, $"{B.mateName} is in the raft (seats {Raft.I.occupied})");
            Check((r1 - r0).magnitude > 1f, $"{B.mateName}'s rowing moves it ({(r1 - r0).magnitude:0.0} m)");

            // 11. a crewmate leaves
            Ask(B.seat, 15);
            t0 = Time.time;
            while (Time.time - t0 < 20f && mates.ContainsKey(B.seat)) yield return null;
            Check(!mates.ContainsKey(B.seat), $"{B.mateName} leaves and the host lets them go ({Time.time - t0:0.0} s)");
            Ask(A.seat, 15);
            yield return new WaitForSeconds(1f);
            Finish();
        }

        void Finish()
        {
            Debug.Log($"DEEP NETTEST: {passed} passed, {failedN} failed");
            StartCoroutine(QuitSoon(failedN == 0 && passed > 0 ? 0 : 1));
        }
        IEnumerator QuitSoon(int code)
        {
            yield return new WaitForSeconds(1f);
            if (nm && nm.IsListening) nm.Shutdown();
            yield return null;
            Application.Quit(code);
        }

        // a crewmate does what it's asked and says what it sees
        void TestStep(NetR r)
        {
            int step = r.U8();
            var d = D; var ship = Ship;
            var w = new NetW().U8(M_REPORT).U8(step);
            switch (step)
            {
                case 1: { var at = r.V3(); d.inputEnabled = false; d.Place(at, 0, 0); break; }
                case 2: w.F(Now - 0.1f).V3(ship.Body.position); break;
                case 3:
                    foreach (var s in ship.L.stations) if (s.kind == "helm") { d.Man(s); break; }
                    ship.depthOrder = 9f; ship.telegraph = 3;
                    break;
                case 4: d.manning = null; d.Place(ship.Body.position + ship.Body.right * 14f + Vector3.up, 0, 0); break;
                case 5:
                {
                    var fl = DeepBoot.I.flora;
                    if (fl.NearestPlant(d.EyeWorld, out var p)) fl.Take(p, 900f);
                    break;
                }
                case 6: w.I32(DeepBoot.I.flora.Total); break;
                case 7: { var ore = ItemDB.Get("Titanium Ore"); if (ore != null) ship.store.Add(ore, 3); break; }
                case 8: { var ore = ItemDB.Get("Titanium Ore"); w.I32(ore != null ? ship.store.Count(ore) : -1); break; }
                case 9:
                {
                    Creature best = null; float bd = float.MaxValue; int n = 0;
                    foreach (var c in Life.I.live) { if (!c.alive) continue; n++; float dd = (c.pos - d.EyeWorld).sqrMagnitude; if (!c.persistent && dd < bd) { bd = dd; best = c; } }
                    w.I32(n).I32(best != null ? best.id : -1).V3(best != null ? best.netPos + best.netVel * Mathf.Clamp(Now - best.netT, 0, 0.5f) : Vector3.zero);
                    break;
                }
                case 10: { int id = r.I32(); if (Life.I.byId.TryGetValue(id, out var c)) Life.I.Wound(c, 3f, d.EyeWorld, false, "a test strike"); break; }
                case 11: w.F(d.health); break;
                case 12: w.I32(ship.sys.breaches.Count); break;
                case 13: if (ship.sys.breaches.Count > 0) ship.sys.Patch(ship.sys.breaches[0], 3.2f); break;
                case 14: StartCoroutine(Row()); break;
                case 15: StartCoroutine(QuitSoon(0)); return;
            }
            ToHost(w, true);
        }

        IEnumerator Row()
        {
            float t0 = Time.time;
            while (!Raft.I && Time.time - t0 < 3f) yield return null;
            if (!Raft.I) yield break;
            D.EnterRaft(Raft.I, mySeat);
            t0 = Time.time;
            while (Time.time - t0 < 4f) { Raft.I.Oars(mySeat, 1f, 0f); oarsT = 1f; RaftOars(mySeat, 1f, 0f); yield return null; }
        }
    }
}
