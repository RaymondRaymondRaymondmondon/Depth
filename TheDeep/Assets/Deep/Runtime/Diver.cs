// The diver: first person.
// - In the sea you swim where you look (WASD), rise with Space, sink with Ctrl, sprint with Shift; at the surface you
//   tread water with your head out. The tank: 45 s of air (the doc's survival meters come in stage 4), drained while
//   your head is under, refilled in air.
// - Aboard the Nautilus you walk (Space jumps), climb ladders (W/Space up, S/Ctrl down), and use what's in front of you
//   with E. The body lives in the ship's collision proxy (Nautilus.cs); the camera is mapped onto the real ship.
// - F switches the helmet lamp.
// The camera is its own object: `head` is where the eye is in whichever space the body is in.
using UnityEngine;

namespace Deep
{
    public class Diver : MonoBehaviour, ISense
    {
        // what the sea senses of this diver (Life.ISense)
        public Vector3 Eye => EyeWorld;
        public bool Inside => aboard || piloting != null;
        public Vector3 Velocity => vel;
        public bool Lamp => lampOn && !aboard;
        public Vector3 Look => cam ? cam.transform.forward : transform.forward;
        public int raftSeatNow => raftSeat;

        public Camera cam;
        public Transform head;
        public CharacterController cc;
        public float yaw, pitch;
        public Vector3 vel;
        public float oxygen = 45f, oxygenMax = 45f;
        public float health = 100f;      // (stage 4 brings the doc's full survival meters)
        public string lastHurtBy;
        public bool inputEnabled = true;
        public bool aboard, lampOn = true, climbing;
        public Nautilus ship;
        public bool HeadUnderAboard;     // aboard, with the head under flood water
        public bool uiOpen;
        public KiteSub piloting;         // in the Kite-Sub's seat
        public Raft onRaft; int raftSeat;              // a screen is open (CraftUI): the diver stands still and the mouse is free
        public NLStation manning;        // the station this hand is working (the helm, the telegraph...)
        public string hint = "";
        public string toast = ""; float toastT;
        public const float Swim = 3.6f, Sprint = 5.8f, Walk = 2.4f, Run = 4.0f, EyeHeight = 0.7f;

        public static Diver Spawn(Vector3 at)
        {
            var go = new GameObject("Diver");
            go.transform.position = at;
            var d = go.AddComponent<Diver>();
            d.cc = go.AddComponent<CharacterController>();
            d.cc.height = 1.6f; d.cc.radius = 0.35f; d.cc.center = Vector3.zero; d.cc.slopeLimit = 60; d.cc.stepOffset = 0.3f;
            d.head = new GameObject("Head").transform; d.head.SetParent(go.transform, false); d.head.localPosition = new Vector3(0, EyeHeight, 0);
            var cg = new GameObject("Eye");
            d.cam = cg.AddComponent<Camera>(); d.cam.tag = "MainCamera"; d.cam.nearClipPlane = 0.05f; d.cam.fieldOfView = 72;
            cg.AddComponent<AudioListener>();
            d.yaw = 90f;
            return d;
        }

        public void Toast(string s) { if (s != toast && !string.IsNullOrEmpty(s)) Sfx.UI("toast"); toast = s; toastT = 3.5f; }

        // a bite, a sting, a ram: at zero you black out and come to aboard the Nautilus (the campaign's death rules -
        // what you carried left where you fell - come with stage 7)
        public void Hurt(float dmg, string by)
        {
            if (!inputEnabled && DeepBoot.I && DeepBoot.I.GetComponent<Shots>()) return;   // (not in the screenshot harness)
            health -= dmg; lastHurtBy = by;
            Toast(health > 0 ? $"{by} bites! ({health:0} health)" : $"Taken by the {by}...");
            if (health > 0) return;
            if (Opening.Active && Opening.I.Respawn()) return;
            // the campaign's death rule: what you carried stays where you fell
            Drops.I?.LeaveHere(this);
            Sfx.Play2D("death");
            if (onRaft != null) LeaveRaft();
            if (piloting != null) LeaveKiteSub(true);
            health = 60f; oxygen = oxygenMax;
            if (ship) foreach (var s in ship.L.stations) if (s.kind == "lockers") { Board(ship.StandLocal(s), Nautilus.FacingYaw(s), $"You come to in the Dive Room. ({by})"); break; }
        }

        void Update()
        {
            float dt = Time.deltaTime;
            if (inputEnabled)
            {
                if (!uiOpen && Input.GetMouseButtonDown(0)) { Cursor.lockState = CursorLockMode.Locked; Cursor.visible = false; }
                if (!uiOpen && Input.GetKeyDown(KeyCode.Escape)) { Cursor.lockState = CursorLockMode.None; Cursor.visible = true; }
                if (Cursor.lockState == CursorLockMode.Locked)
                {
                    yaw += Input.GetAxisRaw("Mouse X") * 2.2f;
                    pitch = Mathf.Clamp(pitch - Input.GetAxisRaw("Mouse Y") * 2.2f, -88f, 88f);
                }
                if (Input.GetKeyDown(KeyCode.F)) lampOn = !lampOn;
            }
            transform.rotation = Quaternion.Euler(0, yaw, 0);
            head.localRotation = Quaternion.Euler(pitch, 0, 0);
            if (piloting != null) { piloting.Drive(this, dt); oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f); }
            else if (onRaft != null) { onRaft.Drive(this, dt); oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f); vel = Vector3.zero; }
            else if (aboard && manning != null) ManStep(dt);
            else if (aboard) WalkStep(dt); else SwimStep(dt);
            Uses();
            if (ship && ship.groundedMsg != null) { if (aboard) Toast(ship.groundedMsg); Net.Alert(ship.groundedMsg); ship.groundedMsg = null; }
            if (ship && ship.sys && ship.sys.alert != null) { if (aboard) Toast(ship.sys.alert); Net.Alert(ship.sys.alert); ship.sys.alert = null; }
            // F9 (testing): hole the room you're standing in
            if (inputEnabled && aboard && ship && ship.sys && Input.GetKeyDown(KeyCode.F9))
            {
                var gg = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
                if (ship.sys.AddBreach(ship.sys.RoomIndexAt(gg), 0.05f) != null) Toast("A plate gives way! The sea is coming in!");
            }
            if (toastT > 0) toastT -= dt; else toast = "";
        }

        // keys (SimHold lets the tests and shots hold one down)
        public KeyCode SimHold = KeyCode.None;
        bool Key(KeyCode k) => inputEnabled && !uiOpen && (Input.GetKey(k) || SimHold == k);
        float Axis(KeyCode pos, KeyCode neg) => (Key(pos) ? 1 : 0) - (Key(neg) ? 1 : 0);
        bool Down(KeyCode a, KeyCode b) => Key(a) || Key(b);
        public void SimStep(float dt) { if (aboard) WalkStep(dt); else SwimStep(dt); }

        // ---- ladders -------------------------------------------------------------------------------------------
        // Walk into a ladder and hold W to climb it (S to go down; Space lets go). At the top of the pilot house's
        // shaft you step off onto its floor; at the top of the Dive Room's you open the deck hatch (E); at the bottom
        // you step off. Standing at the top of the shaft, S takes hold of the ladder to climb down.
        NLLadder onLadder;

        NLLadder LadderAt(Vector3 g)
        {
            foreach (var l in ship.L.ladders)
                if (new Vector2(g.x - l.x, g.z - l.z).magnitude < 0.65f && g.y > l.y0 - 0.2f && g.y < l.y1 + 0.9f) return l;
            return null;
        }

        // where you step off at the top of a ladder with no hatch: onto the floor beyond the shaft
        Vector3 TopExit(NLLadder l)
        {
            float dir = Nautilus.ShaftX - l.x >= 0 ? 1f : -1f;
            return new Vector3(Nautilus.ShaftX + dir * (Nautilus.ShaftHalf + 0.5f), l.y1 + cc.height / 2 + 0.05f, l.z);
        }

        void PlaceGen(Vector3 g)
        {
            cc.enabled = false; transform.position = ship.ProxyPoint(Nautilus.G(g.x, g.y, g.z)); cc.enabled = true;
            vel = Vector3.zero;
        }

        // true while the ladder has you
        bool LadderStep(Vector3 g, float dt)
        {
            float climb = Axis(KeyCode.W, KeyCode.S);
            if (onLadder == null)
            {
                var l = LadderAt(g);
                if (l != null && climb > 0 && g.y < l.y1) { onLadder = l; climbing = true; }
                else if (climb < 0)
                    foreach (var t in ship.L.ladders)
                    {
                        if (!string.IsNullOrEmpty(t.hatch)) continue;
                        bool atTop = new Vector2(g.x - t.x, g.z - t.z).magnitude < 1.45f && g.y > t.y1 + 0.5f && g.y < t.y1 + 1.3f;
                        if (!atTop) continue;
                        onLadder = t; climbing = true;
                        PlaceGen(new Vector3(t.x + 0.25f * Mathf.Sign(Nautilus.ShaftX - t.x), t.y1 + 0.2f, t.z));
                        yaw = Nautilus.FacingYaw(new NLStation { facing = new[] { Mathf.Sign(t.x - Nautilus.ShaftX), 0f, 0f } }) + 180f;
                        return true;
                    }
                if (onLadder == null) { climbing = false; return false; }
            }
            var lad = onLadder;
            // let go
            if (inputEnabled && Input.GetKeyDown(KeyCode.Space))
            {
                onLadder = null; climbing = false;
                vel = -transform.forward * 1.2f + Vector3.up * 0.5f;
                return false;
            }
            // off the bottom
            if (climb < 0 && g.y <= lad.y0 + cc.height / 2 + 0.03f) { onLadder = null; climbing = false; return false; }
            // over the top
            if (climb > 0 && string.IsNullOrEmpty(lad.hatch) && g.y >= lad.y1 + 0.3f)
            {
                PlaceGen(TopExit(lad));
                onLadder = null; climbing = false;
                return true;
            }
            vel = new Vector3(0, climb * 1.9f, 0);
            if (!string.IsNullOrEmpty(lad.hatch) && g.y > lad.y1 - 0.85f && vel.y > 0) vel.y = 0;   // the hatch overhead
            cc.Move(vel * dt);
            return true;
        }

        void SwimStep(float dt)
        {
            var p = transform.position;
            float surf = Sea.SurfaceAt(p);
            bool atSurface = p.y + EyeHeight > surf - 0.15f;
            Vector3 wish = head.forward * Axis(KeyCode.W, KeyCode.S) + head.right * Axis(KeyCode.D, KeyCode.A)
                         + Vector3.up * ((Down(KeyCode.Space, KeyCode.Space) ? 1 : 0) - (Down(KeyCode.LeftControl, KeyCode.C) ? 1 : 0));
            if (wish.sqrMagnitude > 1) wish.Normalize();
            float speed = Down(KeyCode.LeftShift, KeyCode.LeftShift) ? Sprint : Swim;
            // water: quick to start, slow to stop (drag)
            vel = Vector3.Lerp(vel, wish * speed, 1 - Mathf.Exp(-dt * (wish.sqrMagnitude > 0 ? 2.6f : 1.4f)));
            if (atSurface)
            {
                // treading water: the head rides the swell; you can't climb out of the sea by swimming up
                float want = surf - EyeHeight + 0.25f;
                if (p.y > want && vel.y > 0) vel.y = 0;
                vel.y += (want - p.y) * 3f * dt;
            }
            cc.Move(vel * dt);
            bool headUnder = transform.position.y + EyeHeight < Sea.SurfaceAt(p) - 0.05f;
            oxygen = headUnder ? Mathf.Max(0, oxygen - dt) : Mathf.Min(oxygenMax, oxygen + dt * 12f);
        }

        void WalkStep(float dt)
        {
            var g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
            if (LadderStep(g, dt)) { oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f); return; }
            float fwd = Axis(KeyCode.W, KeyCode.S), side = Axis(KeyCode.D, KeyCode.A);
            var wish = transform.forward * fwd + transform.right * side;
            if (wish.sqrMagnitude > 1) wish.Normalize();
            float speed = Down(KeyCode.LeftShift, KeyCode.LeftShift) ? Run : Walk;
            // flood water: wading slows you; deeper than your chest you swim (Space up to the air, Ctrl down)
            float surf = ship.sys ? ship.sys.SurfaceAt(g) : float.NegativeInfinity;
            float feet = g.y - cc.height / 2;
            float wet = surf - feet;
            if (wet > 0.3f) speed *= Mathf.Lerp(0.8f, 0.55f, Mathf.Clamp01((wet - 0.3f) / 0.8f));
            var h = Vector3.Lerp(new Vector3(vel.x, 0, vel.z), wish * speed, 1 - Mathf.Exp(-dt * 12f));
            vel.x = h.x; vel.z = h.z;
            if (wet > 1.15f)
            {
                float swimUp = (Down(KeyCode.Space, KeyCode.Space) ? 1 : 0) - (Down(KeyCode.LeftControl, KeyCode.C) ? 1 : 0);
                float floatAt = surf - EyeHeight + 0.1f;   // the head just out of the water
                vel.y = Mathf.Lerp(vel.y, swimUp != 0 ? swimUp * 2f : Mathf.Clamp((floatAt - g.y) * 2f, -0.6f, 0.8f), 1 - Mathf.Exp(-dt * 4f));
                if (g.y > floatAt && vel.y > 0) vel.y = 0;
            }
            else if (cc.isGrounded)
            {
                vel.y = -1f;
                if (inputEnabled && Input.GetKeyDown(KeyCode.Space)) vel.y = 3.4f;
            }
            else vel.y -= 9.8f * dt;
            cc.Move(vel * dt);
            // air aboard: her life support refills the tank, unless your head is under the flood
            bool headUnder = g.y + EyeHeight < surf - 0.05f;
            HeadUnderAboard = headUnder;
            if (headUnder) oxygen = Mathf.Max(0, oxygen - dt);
            else if (ship.sys == null || ship.sys.LifeSupport) oxygen = Mathf.Min(oxygenMax, oxygen + dt * 12f);
            // down the moonpool's well: out into the sea under her keel
            var mp = ship.L.moonpool;
            g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
            if (mp != null && g.y < mp.y - 0.6f && g.x > mp.x0 && g.x < mp.x1) LeaveTo(Nautilus.G((mp.x0 + mp.x1) / 2, -Nautilus.Radius - 1.6f, (mp.z0 + mp.z1) / 2), "Out through the moonpool");
        }

        // the raft: into a seat (rowing with W/S and A/D), and out into the water beside it
        public void EnterRaft(Raft r, int seat)
        {
            onRaft = r; raftSeat = seat; aboard = false; manning = null; climbing = false; piloting = null;
            cc.enabled = false; transform.position = r.Seat(seat); cc.enabled = true;
            vel = Vector3.zero;
        }
        public void LeaveRaft()
        {
            var r = onRaft; if (r == null) return;
            onRaft = null;
            cc.enabled = false; transform.position = r.transform.position + r.transform.right * 1.8f + Vector3.down * 0.6f; cc.enabled = true;
            vel = Vector3.zero;
        }

        // the Kite-Sub: into its seat (it undocks if it's in the moonpool), and out again (docking it if it's under her)
        public void EnterKiteSub(KiteSub k)
        {
            if (k.docked) k.Undock();
            piloting = k; aboard = false; manning = null; climbing = false;
            cc.enabled = false; transform.position = k.Seat; cc.enabled = true;
            yaw = k.transform.eulerAngles.y; pitch = 0;
            Toast($"The Kite-Sub: mouse steers, W/S thrust, Space/Ctrl up and down, Shift burst, E to leave. Battery {k.battery * 100:0}%");
        }
        public void LeaveKiteSub(bool forced)
        {
            var k = piloting; if (k == null) return;
            piloting = null;
            if (!forced && ship && (k.transform.position - k.UnderDock).magnitude < 5f)
            {
                k.Dock();
                foreach (var s in ship.L.stations) if (s.kind == "moonpool") { Board(ship.StandLocal(s), Nautilus.FacingYaw(s), "The Kite-Sub docks; you climb out into the moonpool room."); break; }
                return;
            }
            cc.enabled = false; transform.position = k.transform.position + k.transform.right * 2.2f + Vector3.up * 0.5f; cc.enabled = true;
            vel = Vector3.zero;
            Toast("You leave the Kite-Sub where it is.");
        }

        // E: what's in front of you
        void Uses()
        {
            hint = "";
            if (!ship) return;
            bool e = inputEnabled && !uiOpen && Input.GetKeyDown(KeyCode.E);
            if (onRaft != null)
            {
                // from the raft straight up onto her deck, if it's alongside the hatch
                foreach (var hk in ship.L.hatches)
                    if (hk.kind == "deck" && (ship.WorldPoint(Nautilus.G(hk.outside)) - onRaft.transform.position).magnitude < 5.5f)
                    {
                        hint = "E  Climb onto her deck and down the hatch";
                        if (e) { onRaft = null; Board(Nautilus.G(hk.inside[0], hk.inside[1] - 1.2f, hk.inside[2] + 0.7f), 180f, "You haul yourself onto her slick deck, wrench the hatch open and drop through."); }
                        return;
                    }
                hint = "W/S  row    A/D  turn    E  leave the raft";
                if (e) LeaveRaft();
                return;
            }
            if (piloting != null)
            {
                bool atDock = (piloting.transform.position - piloting.UnderDock).magnitude < 5f;
                hint = atDock ? "E  Dock in the moonpool" : $"E  Leave the Kite-Sub    battery {piloting.battery * 100:0}%   hull {piloting.hull * 100:0}%";
                if (e) LeaveKiteSub(false);
                return;
            }
            // a satchel left by the dead, anywhere
            if (Drops.I != null && !climbing && manning == null && Drops.I.Near(this, e, ref hint)) return;
            if (aboard)
            {
                // the decorations (and placing one)
                if (Decor.I != null && !climbing && manning == null && Decor.I.Aimed(this, e, ref hint)) return;
                var g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(transform.position));
                if (climbing && onLadder != null && !(onLadder.hatch == "deck" && g.y > onLadder.y1 - 0.8f)) { hint = "W/S  climb    Space  let go"; return; }
                foreach (var t in ship.L.ladders)
                    if (string.IsNullOrEmpty(t.hatch) && !climbing && new Vector2(g.x - t.x, g.z - t.z).magnitude < 1.45f && g.y > t.y1 + 0.5f && g.y < t.y1 + 1.3f)
                    { hint = "S  Climb down the ladder"; return; }
                foreach (var l in ship.L.ladders)
                    if (l.hatch == "deck" && climbing && g.y > l.y1 - 0.8f)
                    {
                        hint = "E  Open the deck hatch";
                        if (e) foreach (var hk in ship.L.hatches) if (hk.kind == "deck") LeaveTo(Nautilus.G(hk.outside), "Out on her deck");
                        return;
                    }
                if (manning != null)
                {
                    hint = "E  Step away";
                    if (e) manning = null;
                    return;
                }
                // a breach in reach: hold E to patch it
                var sy = ship.sys;
                var br = sy != null ? sy.BreachNear(g + Vector3.up * 0.3f) : null;
                if (br != null)
                {
                    hint = $"Hold E  Patch the breach  {br.patch * 100:0}%";
                    if (inputEnabled && Input.GetKey(KeyCode.E) && sy.Patch(br, Time.deltaTime)) Toast("The breach is patched.");
                    return;
                }
                var s = ship.StationNear(transform.position);
                if (s == null) return;
                switch (s.kind)
                {
                    case "helm":
                    case "telegraph":
                        hint = "E  Take " + StationName(s.kind).ToLowerInvariant() + (ship.power ? "" : "  (no power: she won't answer)");
                        if (e) Man(s);
                        break;
                    case "power" when sy != null && sy.breakersTripped:
                        hint = $"Hold E  Reset the tripped breakers  {sy.resetT * 100:0}%";
                        if (inputEnabled && !uiOpen && Input.GetKey(KeyCode.E))
                        {
                            sy.resetT += Time.deltaTime / 3f;
                            if (sy.resetT >= 1f)
                            {
                                sy.breakersTripped = false; sy.state = PowerState.Silent; ship.power = true;
                                Net.Cmd(Net.C_BREAKERS);
                                Toast("The breakers clunk home. The batteries hold: lamps, life support, the ballast pumps. Silent running.");
                            }
                        }
                        else sy.resetT = Mathf.Max(0, sy.resetT - Time.deltaTime);
                        break;
                    case "power":
                    case "sonar":
                    case "pumps":
                        hint = "E  Take " + StationName(s.kind).ToLowerInvariant();
                        if (e) Man(s);
                        break;
                    case "boiler":
                    {
                        var hands = GetComponent<Hands>();
                        var can = ItemDB.Get("Synthetic Fuel Canister");
                        bool stoke = sy != null && sy.engineRepaired && can != null && hands != null && (hands.pack.Has(can) || ship.store.Has(can));
                        hint = sy == null ? "The boiler" : !sy.engineRepaired ? "E  The boiler: the steam engine needs repairing" : stoke ? $"E  Stoke the boiler with a Synthetic Fuel Canister (fuel {sy.fuel * 100:0}%)" : $"The boiler: fuel {sy.fuel * 100:0}%  (stoke it with Synthetic Fuel Canisters)";
                        if (e && sy != null && !sy.engineRepaired) GetComponent<CraftUI>()?.Open("craft", "boiler");
                        else if (e && stoke) { if (!hands.pack.Remove(can)) ship.store.Remove(can); sy.fuel = Mathf.Min(1f, sy.fuel + 0.35f); Net.Cmd(Net.C_STOKE); Toast("The firebox roars."); }
                        break;
                    }
                    case "airlock":
                        hint = "E  Cycle the airlock and swim out";
                        if (e) foreach (var hk in ship.L.hatches) if (hk.kind == "airlock") LeaveTo(Nautilus.G(hk.outside), "The airlock floods and the outer door swings open");
                        break;
                    case "moonpool":
                        if (KiteSub.I != null && KiteSub.I.docked && KiteSub.I.TakenByOther)
                            hint = "Someone is in the Kite-Sub's seat";
                        else if (KiteSub.I != null && KiteSub.I.docked)
                        {
                            hint = $"E  Board the Kite-Sub (battery {KiteSub.I.battery * 100:0}%)    or drop into the moonpool to dive";
                            if (e) EnterKiteSub(KiteSub.I);
                        }
                        else
                        {
                            hint = KiteSub.I == null ? "E  The moonpool's cradle: build the Kite-Sub    (or drop in to dive)" : "The Kite-Sub is out (drop into the moonpool to dive)";
                            if (e && KiteSub.I == null) GetComponent<CraftUI>()?.Open("craft", "moonpool");
                        }
                        break;
                    case "cabin":
                        hint = "A cabin, yours to make your own: Artisan Bench decorations (at the forge), then Tab, choose one, Place it";
                        break;
                    case "fabricator": case "forge": case "grill": case "desalinator": case "planter":
                        hint = "E  Use " + StationName(s.kind).ToLowerInvariant();
                        if (e) GetComponent<CraftUI>()?.Open("craft", s.kind);
                        break;
                    case "lockers": case "larder":
                        hint = "E  Open " + StationName(s.kind).ToLowerInvariant() + " (her stores)";
                        if (e) GetComponent<CraftUI>()?.Open("store", s.kind);
                        break;
                    case "oxygen":
                        hint = ship.sys != null && ship.sys.LifeSupport ? "E  Refill your tank" : "The oxygen rack (she has no power to charge it)";
                        if (e && ship.sys != null && ship.sys.LifeSupport) { oxygen = oxygenMax; Toast("Tank full."); }
                        break;
                    default:
                        hint = StationName(s.kind) + "  (comes alive in the next build)";
                        break;
                }
                return;
            }
            // in the sea: the raft (righting it if it's flipped)
            if (Raft.I != null && (Raft.I.transform.position - transform.position).magnitude < 3.2f)
            {
                hint = Raft.I.flipped ? "E  Right the raft" : "E  Climb into the raft";
                if (e) { if (Raft.I.flipped) Raft.I.Right(); else EnterRaft(Raft.I, Net.MySeat); }
                return;
            }
            // in the sea: the Kite-Sub where it was left, the airlock's outer door, the deck hatch, the moonpool from below
            if (KiteSub.I != null && !KiteSub.I.docked && !KiteSub.I.TakenByOther && (KiteSub.I.transform.position - transform.position).magnitude < 3.5f)
            {
                hint = "E  Climb into the Kite-Sub";
                if (e) EnterKiteSub(KiteSub.I);
                return;
            }
            var lp = Nautilus.FromLocal(ship.Body.InverseTransformPoint(transform.position));
            foreach (var hk in ship.L.hatches)
            {
                if ((new Vector3(hk.outside[0], hk.outside[1], hk.outside[2]) - lp).magnitude > 2.4f) continue;
                hint = hk.kind == "airlock" ? "E  Enter the airlock" : "E  Open the deck hatch and climb down";
                if (e)
                {
                    if (hk.kind == "airlock") Board(Nautilus.G(hk.inside), Nautilus.FacingYaw(new NLStation { facing = new[] { 0f, 0f, -1f } }), "The airlock drains. You're aboard.");
                    else Board(Nautilus.G(hk.inside[0], hk.inside[1] - 1.2f, hk.inside[2] + 0.7f), 180f, "You drop through the hatch.");
                }
                return;
            }
            var m = ship.L.moonpool;
            if (m != null && lp.x > m.x0 && lp.x < m.x1 && lp.z > m.z0 && lp.z < m.z1 && lp.y > -Nautilus.Radius - 3f && lp.y < -Nautilus.Radius + 0.5f)
            {
                hint = "E  Climb up into the moonpool";
                if (e) Board(Nautilus.G(m.x0 - 0.8f, Nautilus.Floor, 0), 90f, "You haul yourself out of the moonpool.");
            }
        }

        // working a station: the hand stands at it, looking about freely; the keys go to the station
        public void Man(NLStation s)
        {
            Board(ship.StandLocal(s), Nautilus.FacingYaw(s));
            manning = s;
            switch (s.kind)
            {
                case "helm": Toast(ship.power ? "A/D rudder, W/S telegraph, Space/C depth, H hold heading, X centre the rudder" : "The wheel turns, but nothing answers: she has no power."); break;
                case "telegraph": Toast("W/S ring the telegraph"); break;
                case "power": Toast("W/S choose her power: Dead in the water, Silent running, Engine"); break;
                case "sonar": Toast("Space: ping (loud: everything hungry hears it). The headphones listen."); break;
                case "pumps": Toast("You work the pump handles."); break;
            }
        }

        void ManStep(float dt)
        {
            if (!inputEnabled) return;
            var n = ship;
            switch (manning.kind)
            {
                case "power":
                {
                    int st = (int)n.sys.state;
                    if (Input.GetKeyDown(KeyCode.W)) st++;
                    if (Input.GetKeyDown(KeyCode.S)) st--;
                    st = Mathf.Clamp(st, 0, 2);
                    if (st != (int)n.sys.state)
                    {
                        var want = (PowerState)st;
                        if (want == PowerState.Engine && !n.sys.engineRepaired) Toast("The steam engine is wrecked: repair it at the boiler (Titanium and a Synthetic Fuel Canister).");
                        else if (want == PowerState.Engine && n.sys.fuel <= 0) Toast("The boiler's bunker is empty.");
                        else if (want == PowerState.Silent && n.sys.battery <= 0) Toast("The batteries are flat.");
                        else
                        {
                            n.sys.state = want; Net.Cmd(Net.C_POWER, st);
                            Toast(want == PowerState.Engine ? "The boiler roars and the dynamo takes the load." : want == PowerState.Silent ? "Silent running: battery drive, a quarter speed." : "Dead in the water. The lamps die and the air goes still.");
                        }
                    }
                    return;
                }
                case "sonar":
                    if (Input.GetKeyDown(KeyCode.Space) && n.sys.Ping()) Toast("PING");
                    return;
                case "pumps":
                    n.sys.pumpsManned = true;
                    return;
            }
            if (Input.GetKeyDown(KeyCode.W)) n.Telegraph(1);
            if (Input.GetKeyDown(KeyCode.S)) n.Telegraph(-1);
            if (manning.kind != "helm") return;
            float r = Axis(KeyCode.D, KeyCode.A);
            if (r != 0) { n.holdHeading = false; n.rudder = Mathf.Clamp(n.rudder + r * dt * 0.9f, -1f, 1f); }
            if (Input.GetKeyDown(KeyCode.X)) { n.holdHeading = false; n.rudder = 0; }
            if (Input.GetKeyDown(KeyCode.H)) { n.holdHeading = !n.holdHeading; n.headingOrder = n.heading; Toast(n.holdHeading ? $"Holding {n.heading:000}" : "Heading hold off"); }
            float dv = (Input.GetKey(KeyCode.C) || Input.GetKey(KeyCode.LeftControl) ? 1 : 0) - (Input.GetKey(KeyCode.Space) ? 1 : 0);
            if (dv != 0) n.depthOrder = Mathf.Clamp(n.depthOrder + dv * dt * 3f, 3.3f, 400f);
        }

        public static string StationName(string kind)
        {
            switch (kind)
            {
                case "helm": return "The helm"; case "telegraph": return "The engine telegraph"; case "sonar": return "The sonar";
                case "fabricator": return "The fabricator"; case "forge": return "The forge"; case "planter": return "The planter";
                case "larder": return "The larder"; case "grill": return "The galley grill"; case "desalinator": return "The desalinator";
                case "lockers": return "The suit lockers"; case "oxygen": return "The oxygen rack"; case "boiler": return "The boiler";
                case "pumps": return "The bilge pumps"; case "power": return "The switchboard";
            }
            return kind;
        }

        // aboard at a standing spot (ship local, feet), facing a yaw in the ship's frame
        public void Board(Vector3 standLocal, float yawShip, string msg = null)
        {
            if (msg != null && ship) Sfx.Shared(msg.Contains("airlock") ? "airlock" : msg.Contains("moonpool") ? "splash_out" : msg.Contains("Dive Room") || msg.Contains("come to") ? "respawn" : "hatch", ship.WorldPoint(standLocal), 1f, 1f, Medium.Aboard);
            aboard = true; climbing = false; manning = null; onLadder = null;
            cc.enabled = false; transform.position = ship.ProxyPoint(standLocal) + Vector3.up * (cc.height / 2 + 0.02f); cc.enabled = true;
            yaw = yawShip; vel = Vector3.zero;
            transform.rotation = Quaternion.Euler(0, yaw, 0);
            if (msg != null) Toast(msg);
        }

        // out into the sea at a point by the ship (ship local)
        public void LeaveTo(Vector3 local, string msg = null)
        {
            var w = ship.WorldPoint(local);
            var fwd = ship.RotToWorld(Quaternion.Euler(0, yaw, 0)) * Vector3.forward;
            if (msg != null) Sfx.Shared(msg.Contains("airlock") ? "airlock" : msg.Contains("moonpool") ? "splash_in" : "hatch", w, 1f, 1f, Medium.Aboard);
            aboard = false; climbing = false; manning = null;
            cc.enabled = false; transform.position = w; cc.enabled = true;
            yaw = Mathf.Atan2(fwd.x, fwd.z) * Mathf.Rad2Deg; vel = Vector3.zero;
            if (msg != null) Toast(msg);
        }

        void LateUpdate()
        {
            UnderwaterLook.Aboard = (aboard && !HeadUnderAboard) || piloting != null;
            if (onRaft != null) { transform.position = onRaft.Seat(raftSeat); cam.transform.SetPositionAndRotation(onRaft.Seat(raftSeat) + Vector3.up * 0.1f, Quaternion.Euler(pitch, yaw, Mathf.DeltaAngle(0f, onRaft.transform.eulerAngles.z) * 0.4f)); }
            else if (piloting != null) { transform.position = piloting.Seat; cam.transform.SetPositionAndRotation(piloting.Seat, piloting.transform.rotation); }
            else if (aboard && ship) cam.transform.SetPositionAndRotation(ship.ToWorld(head.position), ship.RotToWorld(head.rotation));
            else cam.transform.SetPositionAndRotation(head.position, head.rotation);
            // the helmet lamp, a little above and ahead of the eye
            var c = cam.transform;
            var lampAt = c.position + c.up * 0.12f + c.forward * 0.1f;
            Shader.SetGlobalVector("_DeepHeadPos", new Vector4(lampAt.x, lampAt.y, lampAt.z, lampOn ? 24f : 0f));
            Shader.SetGlobalVector("_DeepHeadDir", new Vector4(c.forward.x, c.forward.y, c.forward.z, 0.8f));
            Shader.SetGlobalVector("_DeepHeadCol", new Vector4(1.0f, 0.95f, 0.82f, aboard ? 0 : 1));
        }

        public Vector3 EyeWorld => onRaft != null ? onRaft.Seat(raftSeat) : piloting != null ? piloting.Seat : aboard && ship ? ship.ToWorld(head.position) : head.position;
        public float Depth => Mathf.Max(0, -EyeWorld.y);

        public void Place(Vector3 eye, float yawDeg, float pitchDeg)
        {
            aboard = false; climbing = false; manning = null;
            cc.enabled = false; transform.position = eye - Vector3.up * EyeHeight; cc.enabled = true;
            yaw = yawDeg; pitch = pitchDeg; vel = Vector3.zero;
            transform.rotation = Quaternion.Euler(0, yaw, 0); head.localRotation = Quaternion.Euler(pitch, 0, 0);
            LateUpdate();
        }

        // for shots: aboard, at a point in the generator's frame, looking along a yaw (ship frame) and pitch
        public void PlaceAboard(float gx, float gz, float floorY, float yawShip, float pitchDeg)
        {
            Board(Nautilus.G(gx, floorY, gz), yawShip);
            pitch = pitchDeg; head.localRotation = Quaternion.Euler(pitch, 0, 0);
            LateUpdate();
        }
    }
}
