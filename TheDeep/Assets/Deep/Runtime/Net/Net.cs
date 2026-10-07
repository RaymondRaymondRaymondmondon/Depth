// Multiplayer (stage 6; the design doc: 1-4 player online co-op). Depth's Deep Arcade gathers the crew (Host / Join /
// Browse, over the LAN or ZeroTier) and starts The Deep on every PC: the host with -role host, the others with
// -role join -addr <the host> (-port, -seat, -name). Unity Netcode for GameObjects carries the messages over Unity
// Transport (UDP, port 47779 by default) as named messages; nothing here is a networked prefab, because the whole world
// is built at runtime from the seed.
//
// The host is the authority: it builds the world from its seed, runs the sea (Life), sails the Nautilus, keeps her
// systems, the shared stores, the raft, the clock and the weather. A crewmate's PC connects first, is told the seed and
// the state of things (the welcome), builds the same world, and from then on mirrors the host:
//   host -> crew   POSES (15 Hz: every diver, the raft, the Kite-Sub), SHIP (20 Hz), SEA (10 Hz: the animals near each
//                  crewmate), WORLD (2 Hz: clock, weather, the opening), STORE (on change), FLORA/DEPOSIT (taken),
//                  HURT (a bite on that crewmate), ALERT (her alarms)
//   crew -> host   POSE (15 Hz), SHIPCTL (the helm and the telegraph, while manned), CMD (the switchboard, the
//                  breakers, a patch, a ping, a repair, an upgrade...), STOREDELTA, OARS, EMIT (a sound for the sea),
//                  WOUND (a strike on an animal), FLORA/DEPOSIT (taken)
// Each diver is their own PC's: they move, breathe, carry their pack and die there; the others see them through Mate.
// The sea's time is shared (Waves.T), so the swell is the same everywhere.
using System;
using System.Collections.Generic;
using System.Text;
using Unity.Collections;
using Unity.Netcode;
using Unity.Netcode.Transports.UTP;
using UnityEngine;

namespace Deep
{
    public enum NetRole { Solo, Host, Guest }

    public partial class Net : MonoBehaviour
    {
        public static Net I;
        public static NetRole Role = NetRole.Solo;
        public static bool IsHost => Role == NetRole.Host;
        public static bool IsGuest => Role == NetRole.Guest;
        public static bool Online => Role != NetRole.Solo;
        public static int MySeat => I != null ? I.mySeat : 0;
        public static float Now => IsGuest && I != null ? Time.time + I.offset : Time.time;   // the host's clock
        public const string Proto = "DEEP-NET-1";
        public const int MaxCrew = 4, ProtoVersion = 1;

        // host -> crew
        const byte M_WELCOME = 1, M_ROSTER = 2, M_POSES = 3, M_SHIP = 4, M_SEA = 5, M_WORLD = 6, M_STORE = 7, M_FLORA = 8, M_DEPOSIT = 9, M_HURT = 10, M_ALERT = 11, M_TEST = 12;
        // crew -> host
        const byte M_POSE = 20, M_SHIPCTL = 21, M_CMD = 22, M_STOREDELTA = 23, M_OARS = 24, M_EMIT = 25, M_WOUND = 26, M_REPORT = 27;
        // commands
        public const byte C_POWER = 1, C_BREAKERS = 2, C_PATCH = 3, C_BREACH = 4, C_STOKE = 5, C_ENGINE = 6, C_HULL = 7, C_KITE = 8, C_PING = 9, C_RIGHTRAFT = 10;

        NetworkManager nm; UnityTransport utp;
        public int mySeat; public string myName = "Diver";
        public float offset; bool offsetSet;
        public string status = ""; public bool connected, failed; float quitAt = -1;
        public readonly Dictionary<int, Mate> mates = new Dictionary<int, Mate>();       // the rest of the crew, by seat
        readonly Dictionary<ulong, int> seatOf = new Dictionary<ulong, int>();           // host: each guest's seat
        readonly Dictionary<ulong, (int seat, string name)> pending = new Dictionary<ulong, (int, string)>();
        public byte[] welcome; public int welcomeSeed;                                    // guest: kept until the world is built
        public bool worldUp;
        float poseT, shipT, seaT, worldT, storeT, ctlT, oarsT, patchT, ctlHold;
        int seaTick;

        Diver D => DeepBoot.I ? DeepBoot.I.diver : null;
        Nautilus Ship => Nautilus.I;

        // ---- starting up ----------------------------------------------------------------------------------------
        public static Net Begin()
        {
            var go = new GameObject("Net");
            I = go.AddComponent<Net>();
            Role = Args.Role == "host" ? NetRole.Host : Args.Role == "join" ? NetRole.Guest : NetRole.Solo;
            I.mySeat = Mathf.Clamp(Args.Seat, 0, MaxCrew - 1);
            I.myName = string.IsNullOrEmpty(Args.Name) ? "Diver" : Args.Name;
            return I;
        }

        // (NGO registers its message types after the scene's Awake, so the network starts here, a moment later)
        void Start() { if (Role != NetRole.Solo) StartNet(); }

        void StartNet()
        {
            // the NetworkManager is set up on an inactive object, so its Awake sees the config
            var ngo = new GameObject("Netcode"); ngo.SetActive(false);   // (NGO refuses to run under a parent)
            utp = ngo.AddComponent<UnityTransport>();
            nm = ngo.AddComponent<NetworkManager>();
            nm.NetworkConfig = new NetworkConfig { NetworkTransport = utp, EnableSceneManagement = false, ConnectionApproval = true, TickRate = 30, ClientConnectionBufferTimeout = 60 };
            utp.MaxPayloadSize = 1 << 18; utp.MaxPacketQueueSize = 1024;
            utp.ConnectTimeoutMS = 1000; utp.MaxConnectAttempts = 60; utp.DisconnectTimeoutMS = 20000;
            ngo.SetActive(true);
            nm.OnClientConnectedCallback += OnConnected;
            nm.OnClientDisconnectCallback += OnDisconnected;
            ushort port = (ushort)Mathf.Clamp(Args.Port, 1024, 65535);
            if (IsHost)
            {
                utp.SetConnectionData(true, "127.0.0.1", port, "0.0.0.0");
                nm.ConnectionApprovalCallback = Approve;
                if (!nm.StartHost()) { failed = true; status = $"Couldn't open port {port} for the crew."; }
                else status = $"Hosting on port {port}";
            }
            else
            {
                utp.SetConnectionData(true, Args.Addr, port);
                nm.NetworkConfig.ConnectionData = Encoding.UTF8.GetBytes($"{Proto}|{mySeat}|{myName}");
                if (!nm.StartClient()) { failed = true; status = "Couldn't start the network."; }
                else status = $"Calling the Nautilus at {Args.Addr}:{port}...";
            }
            if (nm.CustomMessagingManager != null) nm.CustomMessagingManager.RegisterNamedMessageHandler("deep", OnMsg);
            Debug.Log($"DEEP NET: {Role} seat {mySeat} '{myName}' {Args.Addr}:{port}");
        }

        // the host lets a diver aboard: the same version of the game, and a free seat (theirs if it's free)
        void Approve(NetworkManager.ConnectionApprovalRequest req, NetworkManager.ConnectionApprovalResponse res)
        {
            res.CreatePlayerObject = false;
            if (req.ClientNetworkId == NetworkManager.ServerClientId) { res.Approved = true; return; }
            var parts = Encoding.UTF8.GetString(req.Payload ?? new byte[0]).Split(new[] { '|' }, 3);
            if (parts.Length < 3 || parts[0] != Proto) { res.Approved = false; res.Reason = "That PC has a different version of The Deep."; return; }
            int.TryParse(parts[1], out int want);
            bool Taken(int s) { if (s == mySeat) return true; foreach (var kv in seatOf) if (kv.Value == s) return true; foreach (var kv in pending) if (kv.Value.seat == s) return true; return false; }
            int seat = want >= 0 && want < MaxCrew && !Taken(want) ? want : -1;
            for (int s = 0; s < MaxCrew && seat < 0; s++) if (!Taken(s)) seat = s;
            if (seat < 0) { res.Approved = false; res.Reason = "The crew is full (four divers)."; return; }
            pending[req.ClientNetworkId] = (seat, parts[2]);
            res.Approved = true;
        }

        void OnConnected(ulong id)
        {
            if (IsGuest)
            {
                if (id == nm.LocalClientId) { connected = true; status = "Aboard. Waiting for the host's world..."; }
                return;
            }
            if (id == NetworkManager.ServerClientId) { connected = true; return; }
            if (!pending.TryGetValue(id, out var p)) return;
            pending.Remove(id);
            seatOf[id] = p.seat;
            if (mates.TryGetValue(p.seat, out var old) && old) DropMate(p.seat);
            var m = Mate.Make(p.seat, p.name); m.clientId = id;
            mates[p.seat] = m;
            Life.I?.AddDiver(m);
            SendWelcome(id, p.seat);
            SendRoster();
            D?.Toast($"{p.name} joins the crew.");
            Debug.Log($"DEEP NET: {p.name} joined in seat {p.seat}");
        }

        void OnDisconnected(ulong id)
        {
            if (IsGuest)
            {
                if (id == nm.LocalClientId || id == NetworkManager.ServerClientId)
                {
                    failed = true;
                    string why = nm.DisconnectReason;
                    status = !connected ? (string.IsNullOrEmpty(why) ? $"No answer from the Nautilus at {Args.Addr}." : why) : "The host has left. The Deep will close.";
                    quitAt = Time.time + 8f;
                    Debug.Log("DEEP NET: lost the host: " + status);
                }
                return;
            }
            pending.Remove(id);
            if (!seatOf.TryGetValue(id, out int seat)) return;
            seatOf.Remove(id);
            string name = mates.TryGetValue(seat, out var m) && m ? m.mateName : "A diver";
            DropMate(seat);
            SendRoster();
            D?.Toast($"{name} has left the crew.");
            Debug.Log($"DEEP NET: {name} left (seat {seat})");
        }

        void DropMate(int seat)
        {
            if (!mates.TryGetValue(seat, out var m)) return;
            mates.Remove(seat);
            if (m) { Life.I?.RemoveDiver(m); Destroy(m.gameObject); }
        }

        void OnDestroy() { if (nm && nm.IsListening) nm.Shutdown(); }
        void OnApplicationQuit() { if (nm && nm.IsListening) nm.Shutdown(); }

        // ---- sending ------------------------------------------------------------------------------------------------
        bool Up => nm != null && nm.IsListening && nm.CustomMessagingManager != null;

        void Send(ulong to, NetW w, bool reliable)
        {
            if (!Up) return;
            var d = reliable || w.n > 1100 ? (w.n > 1100 ? NetworkDelivery.ReliableFragmentedSequenced : NetworkDelivery.ReliableSequenced) : NetworkDelivery.Unreliable;
            var fb = new FastBufferWriter(w.n, Allocator.Temp);
            try
            {
                fb.WriteBytesSafe(w.b, w.n);
                nm.CustomMessagingManager.SendNamedMessage("deep", to, fb, d);
            }
            catch (Exception e) { Debug.LogWarning("DEEP NET: send failed: " + e.Message); }
            finally { fb.Dispose(); }
        }
        void ToHost(NetW w, bool reliable) => Send(NetworkManager.ServerClientId, w, reliable);
        void ToCrew(NetW w, bool reliable, ulong except = ulong.MaxValue)
        {
            foreach (var id in seatOf.Keys) if (id != except) Send(id, w, reliable);
        }

        void OnMsg(ulong from, FastBufferReader reader)
        {
            int n = reader.Length - reader.Position;
            if (n <= 0) return;
            var arr = new byte[n];
            reader.ReadBytesSafe(ref arr, n);
            try { if (IsHost) HostHandle(from, arr); else GuestHandle(arr); }
            catch (Exception e) { Debug.LogError("DEEP NET: bad message " + arr[0] + ": " + e); }
        }

        // ---- the welcome: the seed and the state of things ---------------------------------------------------------
        void SendWelcome(ulong id, int seat)
        {
            var w = new NetW().U8(M_WELCOME).I32(ProtoVersion).I32(Args.Seed).U8(seat).F(Time.time);
            var boot = DeepBoot.I;
            w.F(boot.clock.hour).I32(boot.clock.day);
            var wx = Weather.I; w.F(wx ? wx.storm : 0).F(wx ? wx.target : 0);
            w.U8(Opening.I == null ? 0 : Opening.I.done ? 2 : 1).F(Opening.I ? Opening.I.t : 0);
            var sy = Ship.sys; w.Bool(sy.engineRepaired).F(Ship.crushDepth).Bool(KiteSub.I != null);
            WriteCounts(w, Counts(Ship.store));
            var flora = new List<(string kind, long key, Vector3 pos, float left)>();
            if (boot.flora) foreach (var f in boot.flora.Taken()) flora.Add(f);
            w.I32(flora.Count); foreach (var f in flora) w.S(f.kind).L64(f.key).V3(f.pos).F(f.left);
            var deps = new List<(int i, float left)>();
            if (Deposits.I != null) for (int i = 0; i < Deposits.I.nodes.Count; i++) if (Deposits.I.nodes[i].taken) deps.Add((i, Mathf.Max(0, Deposits.I.nodes[i].back - Time.time)));
            w.I32(deps.Count); foreach (var d in deps) w.I32(d.i).F(d.left);
            Send(id, w, true);
        }

        // the guest: the seed comes first (the world is built from it); the rest is applied once it's built
        void ReadWelcomeHead(byte[] b)
        {
            var r = new NetR(b); r.U8();
            int ver = r.I32(); welcomeSeed = r.I32(); mySeat = r.U8(); float t = r.F();
            if (ver != ProtoVersion) { failed = true; status = "The host has a different version of The Deep."; quitAt = Time.time + 8f; return; }
            offset = t - Time.time; offsetSet = true; Waves.TimeOffset = offset;
            welcome = b;
            status = "Building the world...";
        }

        public void ApplyWelcome()
        {
            var r = new NetR(welcome); r.U8(); r.I32(); r.I32(); r.U8(); r.F();
            var boot = DeepBoot.I;
            boot.clock.hour = r.F(); boot.clock.day = r.I32();
            float storm = r.F(), target = r.F();
            if (Weather.I) { Weather.I.storm = storm; Weather.I.target = target; Weather.I.mirror = true; }
            int opening = r.U8(); float ot = r.F();
            if (Opening.I != null)
            {
                if (opening != 1) Opening.I.EndForLateJoin();
                else Opening.I.t = ot;
            }
            Ship.sys.engineRepaired = r.Bool(); Ship.crushDepth = r.F();
            if (r.Bool()) KiteSub.Spawn(Ship);
            SetStore(ReadCounts(r));
            storeBase = Counts(Ship.store);
            int nf = r.I32();
            for (int i = 0; i < nf; i++) { string kind = r.S(); long key = r.L64(); var pos = r.V3(); float left = r.F(); boot.flora?.TakeAt(kind, key, pos, left); }
            int nd = r.I32();
            for (int i = 0; i < nd; i++) { int idx = r.I32(); float left = r.F(); Deposits.I?.TakeIndex(idx, left); }
            welcome = null;
            WorldReady();
            status = "";
            Debug.Log($"DEEP NET: welcomed aboard in seat {mySeat}; world seed {Args.Seed}");
        }

        // the world is built: hook the sea, the ship and the stores up to the network
        public void WorldReady()
        {
            worldUp = true;
            Flora.OnTake = (kind, key, pos, secs) =>
            {
                if (!Online) return;
                var w = new NetW().U8(M_FLORA).S(kind).L64(key).V3(pos).F(secs);
                if (IsGuest) ToHost(w, true); else ToCrew(w, true);
            };
            Deposits.OnTake = (i, secs) =>
            {
                if (!Online) return;
                var w = new NetW().U8(M_DEPOSIT).I32(i).F(secs);
                if (IsGuest) ToHost(w, true); else ToCrew(w, true);
            };
            if (!IsGuest) return;
            if (Life.I) Life.I.mirror = true;
            if (Ship) { Ship.mirror = true; if (Ship.sys) Ship.sys.mirror = true; }
            if (Weather.I) Weather.I.mirror = true;
            if (Raft.I) Raft.I.mirror = true;
            Acoustics.Forward = (pos, db, band, secs, what) => ToHost(new NetW().U8(M_EMIT).V3(pos).F(db).U8((int)band).F(secs).S(what ?? ""), false);
        }

        // ---- the roster ----------------------------------------------------------------------------------------------
        void SendRoster()
        {
            var w = new NetW().U8(M_ROSTER).U8(1 + seatOf.Count);
            w.U8(mySeat).S(myName);
            foreach (var kv in seatOf) w.U8(kv.Value).S(mates.TryGetValue(kv.Value, out var m) && m ? m.mateName : "Diver");
            ToCrew(w, true);
        }

        // ---- every frame --------------------------------------------------------------------------------------------
        void Update()
        {
            if (quitAt > 0 && Time.time > quitAt) { quitAt = -1; Application.Quit(); }
            if (!Online || !worldUp || !Up) return;
            float dt = Time.deltaTime;
            if (IsHost) HostTick(dt); else GuestTick(dt);
        }

        // ---- my own diver as the crew sees it ------------------------------------------------------------------------
        MatePose MyPose()
        {
            var d = D; var p = new MatePose { mode = 4, station = 255 };
            if (!d) return p;
            p.mode = (byte)(d.onRaft != null ? 2 : d.piloting != null ? 3 : d.aboard ? 1 : 0);
            p.pos = p.mode == 1 ? d.transform.position - Nautilus.ProxyOrigin : d.transform.position;
            p.yaw = d.yaw; p.pitch = d.pitch; p.vel = d.vel;
            bool keys = d.inputEnabled && !d.uiOpen;
            p.flags = (byte)((d.lampOn ? MatePose.Lamp : 0) | (d.climbing ? MatePose.Climb : 0) | (keys && Input.GetKey(KeyCode.LeftShift) ? MatePose.Sprint : 0)
                    | (d.onRaft != null && keys && (Input.GetKey(KeyCode.W) || Input.GetKey(KeyCode.S) || Input.GetKey(KeyCode.A) || Input.GetKey(KeyCode.D)) ? MatePose.Row : 0));
            p.station = d.manning != null ? (byte)Mathf.Max(0, Array.IndexOf(Mate.Stations, d.manning.kind)) : (byte)255;
            if (d.manning != null && Array.IndexOf(Mate.Stations, d.manning.kind) < 0) p.station = 255;
            p.health = (byte)Mathf.Clamp(Mathf.RoundToInt(d.health), 0, 100);
            p.seat = (byte)d.raftSeatNow;
            return p;
        }

        static void WriteKite(NetW w, KiteSub k) => w.Bool(k.docked).V3(k.transform.position).Q(k.transform.rotation).F(k.battery).F(k.hull);
        static void ReadKite(NetR r, KiteSub k, bool apply)
        {
            bool docked = r.Bool(); var pos = r.V3(); var rot = r.Q(); float bat = r.F(), hull = r.F();
            if (!apply || k == null) return;
            if (docked && !k.docked) k.Dock();
            else if (!docked && k.docked) k.Undock();
            if (!docked)
            {
                var p = k.transform.position;
                k.transform.SetPositionAndRotation((pos - p).sqrMagnitude > 25f ? pos : Vector3.Lerp(p, pos, 0.5f), Quaternion.Slerp(k.transform.rotation, rot, 0.5f));
            }
            k.battery = bat; k.hull = hull;
        }

        // ---- the host ----------------------------------------------------------------------------------------------
        void HostTick(float dt)
        {
            var d = D; var ship = Ship;
            // who's in the raft, who's flying the Kite-Sub, who's on the pumps
            if (Raft.I)
            {
                int occ = d && d.onRaft == Raft.I ? 1 << d.raftSeatNow : 0;
                foreach (var m in mates.Values) if (m && m.OnRaft) occ |= 1 << m.cur.seat;
                Raft.I.occupied = occ;
            }
            if (KiteSub.I)
            {
                int pilot = d && d.piloting != null ? mySeat : -1;
                foreach (var m in mates.Values) if (m && m.InWorld && m.cur.mode == 3 && Time.time - m.lastHeard < 2f) pilot = m.seat;
                KiteSub.I.pilotSeat = pilot;
            }
            foreach (var m in mates.Values)
                if (m && m.cur.mode == 1 && m.cur.station == 4 && Time.time - m.lastHeard < 0.5f && ship && ship.sys) ship.sys.pumpsManned = true;

            poseT += dt;
            if (poseT >= 1f / 15f)
            {
                poseT = 0;
                var w = new NetW().U8(M_POSES).F(Time.time);
                int count = 1; foreach (var m in mates.Values) if (m && m.poses.Any) count++;
                w.U8(count);
                w.U8(mySeat); MyPose().Write(w);
                foreach (var m in mates.Values) if (m && m.poses.Any) { w.U8(m.seat); m.poses.Latest.Write(w); }
                WriteRaftKite(w);
                ToCrew(w, false);
            }
            shipT += dt;
            if (shipT >= 1f / 20f && ship) { shipT = 0; ToCrew(ShipState(ship), false); }
            seaT += dt;
            if (seaT >= 0.1f && Life.I) { seaT = 0; seaTick++; foreach (var kv in seatOf) if (mates.TryGetValue(kv.Value, out var m) && m && m.InWorld) SendSea(kv.Key, m.Eye); }
            worldT += dt;
            if (worldT >= 0.5f) { worldT = 0; ToCrew(WorldState(), false); }
            storeT += dt;
            if (storeT >= 0.2f && ship) { storeT = 0; HostStore(ship); }
        }

        void WriteRaftKite(NetW w)
        {
            var r = Raft.I;
            w.Bool(r);
            if (r) w.V3(r.transform.position).F(r.heading).V3s(r.vel).Bool(r.flipped).F(r.stroke).U8(r.occupied);
            var k = KiteSub.I;
            w.Bool(k);
            if (k) { w.U8(k.pilotSeat < 0 ? 255 : k.pilotSeat); WriteKite(w, k); }
        }

        NetW ShipState(Nautilus n)
        {
            var sy = n.sys;
            var w = new NetW().U8(M_SHIP).F(Time.time).V3(n.Body.position).Q(n.Body.rotation);
            w.F(n.speed).F(n.vSpeed).F(n.heading).U8(n.telegraph).F(n.rudder);
            w.U8((n.holdHeading ? 1 : 0) | (n.holdDepth ? 2 : 0) | (sy.engineRepaired ? 4 : 0) | (sy.breakersTripped ? 8 : 0) | (sy.pumpsRunning ? 16 : 0));
            w.F(n.headingOrder).F(n.depthOrder).U8((int)sy.state).F(sy.battery).F(sy.fuel).F(sy.resetT).F(n.crushDepth).F(sy.NoiseDb).F(n.grounded);
            w.U8(sy.rooms.Count); foreach (var r in sy.rooms) w.F(r.level);
            w.U8(Mathf.Min(sy.breaches.Count, 60));
            for (int i = 0; i < Mathf.Min(sy.breaches.Count, 60); i++) { var b = sy.breaches[i]; w.I32(b.id).U8(b.room).V3(b.gen).F(b.size).F(b.patch); }
            return w;
        }

        NetW WorldState()
        {
            var c = DeepBoot.I.clock; var wx = Weather.I;
            var w = new NetW().U8(M_WORLD).F(Time.time).F(c.hour).I32(c.day);
            w.F(wx ? wx.storm : 0).F(wx ? wx.target : 0).F(wx ? wx.flash : 0);
            w.U8(Opening.I == null ? 0 : Opening.I.done ? 2 : 1).F(Opening.I ? Opening.I.t : 0);
            return w;
        }

        // the animals near a crewmate: the close ones every tenth of a second, the far ones every fifth
        Dictionary<SpeciesDef, int> spIndex;
        void SendSea(ulong to, Vector3 eye)
        {
            if (spIndex == null) { spIndex = new Dictionary<SpeciesDef, int>(); for (int i = 0; i < SpeciesBook.All.Count; i++) spIndex[SpeciesBook.All[i]] = i; }
            const int Chunk = 34;
            NetW w = null; int inChunk = 0, countAt = 0;
            void Flush() { if (w == null) return; w.b[countAt] = (byte)inChunk; Send(to, w, false); w = null; inChunk = 0; }
            foreach (var c in Life.I.live)
            {
                if (!c.alive) continue;
                float d2 = (c.pos - eye).sqrMagnitude;
                if (d2 > 190f * 190f && !c.persistent) continue;
                if (d2 > 70f * 70f && (seaTick + c.id) % 2 != 0) continue;
                if (w == null) { w = new NetW().U8(M_SEA).F(Time.time); countAt = w.n; w.U8(0); }
                float yaw = Mathf.Atan2(c.fwd.x, c.fwd.z) * Mathf.Rad2Deg, pitch = Mathf.Asin(Mathf.Clamp(c.fwd.y, -1, 1)) * Mathf.Rad2Deg;
                w.I32(c.id).I16(spIndex.TryGetValue(c.sp, out int si) ? si : 0).V3(c.pos).V3s(c.vel)
                 .U8(Mathf.RoundToInt(Mathf.Repeat(yaw, 360f) / 360f * 255f)).U8(Mathf.RoundToInt((pitch + 90f) / 180f * 255f))
                 .U8(Mathf.Clamp(Mathf.RoundToInt((c.size / Mathf.Max(0.01f, c.sp.size) - 0.5f) * 255f), 0, 255))
                 .U8((int)c.state).U8(Mathf.Clamp(Mathf.RoundToInt(c.health * 255f), 0, 255));
                if (++inChunk >= Chunk) Flush();
            }
            Flush();
        }

        void HostHandle(ulong from, byte[] b)
        {
            var r = new NetR(b); int type = r.U8();
            seatOf.TryGetValue(from, out int seat);
            mates.TryGetValue(seat, out var mate);
            var ship = Ship;
            switch (type)
            {
                case M_POSE:
                {
                    float t = r.F(); var p = MatePose.Read(r);
                    if (mate) mate.Heard(t, p);
                    if (p.mode == 3 && r.Bool()) ReadKite(r, KiteSub.I, KiteSub.I != null);
                    break;
                }
                case M_SHIPCTL:
                {
                    int mask = r.U8(); int tele = r.U8(); float rud = r.F(); int holds = r.U8(); float ho = r.F(), dor = r.F();
                    if (!ship) break;
                    if ((mask & 1) != 0) ship.telegraph = Mathf.Clamp(tele, 0, Nautilus.TeleNames.Length - 1);
                    if ((mask & 2) != 0) { ship.rudder = Mathf.Clamp(rud, -1, 1); ship.holdHeading = (holds & 1) != 0; ship.holdDepth = (holds & 2) != 0; ship.headingOrder = ho; ship.depthOrder = Mathf.Clamp(dor, 3.3f, 400f); }
                    break;
                }
                case M_CMD:
                {
                    int c = r.U8(); int a = r.I32(); float f = r.F();
                    DoCmd(c, a, f, mate);
                    break;
                }
                case M_STOREDELTA:
                {
                    int seq = r.I32(); var delta = ReadCounts(r);
                    if (ship) foreach (var kv in delta)
                    {
                        var it = ItemDB.Get(kv.Key); if (it == null) continue;
                        if (kv.Value > 0) ship.store.Add(it, kv.Value);
                        else if (kv.Value < 0) ship.store.Remove(it, Mathf.Min(-kv.Value, ship.store.Count(it)));
                    }
                    storeAck[from] = seq; storeDirty = true;
                    break;
                }
                case M_OARS:
                {
                    int s = r.U8(); float row = r.F(), turn = r.F();
                    Raft.I?.Oars(s, row, turn);
                    break;
                }
                case M_EMIT:
                {
                    var pos = r.V3(); float db = r.F(); var band = (Band)r.U8(); float secs = r.F(); string what = r.S();
                    Life.I?.sound.Emit(pos, db, band, secs, string.IsNullOrEmpty(what) ? null : what);
                    break;
                }
                case M_WOUND:
                {
                    int id = r.I32(); float dmg = r.F(); var fromPos = r.V3(); bool caut = r.Bool(); string how = r.S();
                    if (Life.I != null && Life.I.byId.TryGetValue(id, out var cr) && cr.alive) Life.I.Wound(cr, dmg, fromPos, caut, how);
                    break;
                }
                case M_FLORA:
                {
                    string kind = r.S(); long key = r.L64(); var pos = r.V3(); float secs = r.F();
                    DeepBoot.I.flora?.TakeAt(kind, key, pos, secs);
                    ToCrew(new NetW().U8(M_FLORA).S(kind).L64(key).V3(pos).F(secs), true, from);
                    break;
                }
                case M_DEPOSIT:
                {
                    int i = r.I32(); float secs = r.F();
                    Deposits.I?.TakeIndex(i, secs);
                    ToCrew(new NetW().U8(M_DEPOSIT).I32(i).F(secs), true, from);
                    break;
                }
                case M_REPORT:
                    TestReport(seat, r);
                    break;
            }
        }

        void DoCmd(int c, int a, float f, Mate by)
        {
            var ship = Ship; var sy = ship ? ship.sys : null;
            if (sy == null) return;
            switch (c)
            {
                case C_POWER:
                {
                    var want = (PowerState)Mathf.Clamp(a, 0, 2);
                    if (sy.breakersTripped) break;
                    if (want == PowerState.Engine && (!sy.engineRepaired || sy.fuel <= 0)) break;
                    if (want == PowerState.Silent && sy.battery <= 0) break;
                    sy.state = want;
                    break;
                }
                case C_BREAKERS: sy.breakersTripped = false; sy.resetT = 1f; sy.state = PowerState.Silent; ship.power = true; break;
                case C_PATCH:
                    foreach (var b in sy.breaches) if (b.id == a) { sy.Patch(b, f); break; }
                    break;
                case C_BREACH: sy.AddBreach(a, f > 0 ? f : 0.05f); break;
                case C_STOKE: sy.fuel = Mathf.Min(1f, sy.fuel + 0.35f); break;
                case C_ENGINE: sy.engineRepaired = true; break;
                case C_HULL: ship.crushDepth = Mathf.Max(ship.crushDepth, a); break;
                case C_KITE: KiteSub.Spawn(ship); break;
                case C_PING: sy.Ping(); break;
                case C_RIGHTRAFT: Raft.I?.Right(); break;
            }
        }

        // ---- the shared stores ---------------------------------------------------------------------------------------
        // The host's copy is the truth. A crewmate's PC changes its own copy at once (it feels immediate) and sends what
        // changed; the host applies it and sends everyone the whole store, saying which of their changes it has seen, so
        // changes still on the way are laid back over it.
        readonly Dictionary<ulong, int> storeAck = new Dictionary<ulong, int>();
        bool storeDirty; Dictionary<string, int> storeSent = new Dictionary<string, int>();
        Dictionary<string, int> storeBase = new Dictionary<string, int>();
        readonly List<(int seq, Dictionary<string, int> delta)> storePending = new List<(int, Dictionary<string, int>)>();
        int storeSeq;

        static Dictionary<string, int> Counts(Inventory inv)
        {
            var d = new Dictionary<string, int>();
            foreach (var s in inv.slots) if (s.count > 0 && !string.IsNullOrEmpty(s.id)) { d.TryGetValue(s.id, out int k); d[s.id] = k + s.count; }
            return d;
        }
        static bool Same(Dictionary<string, int> a, Dictionary<string, int> b)
        {
            if (a.Count != b.Count) return false;
            foreach (var kv in a) if (!b.TryGetValue(kv.Key, out int v) || v != kv.Value) return false;
            return true;
        }
        static void WriteCounts(NetW w, Dictionary<string, int> d) { w.I16(d.Count); foreach (var kv in d) w.S(kv.Key).I32(kv.Value); }
        static Dictionary<string, int> ReadCounts(NetR r)
        {
            var d = new Dictionary<string, int>(); int n = r.I16() & 0xffff;
            for (int i = 0; i < n; i++) { string id = r.S(); int k = r.I32(); d[id] = k; }
            return d;
        }
        void SetStore(Dictionary<string, int> counts)
        {
            var inv = Ship.store;
            if (Same(Counts(inv), counts)) return;
            inv.Clear();
            foreach (var kv in counts) { var it = ItemDB.Get(kv.Key); if (it != null && kv.Value > 0) inv.Add(it, kv.Value); }
        }

        void HostStore(Nautilus ship)
        {
            var now = Counts(ship.store);
            if (!storeDirty && Same(now, storeSent)) return;
            storeDirty = false; storeSent = now;
            foreach (var id in seatOf.Keys)
            {
                storeAck.TryGetValue(id, out int ack);
                var w = new NetW().U8(M_STORE).I32(ack); WriteCounts(w, now);
                Send(id, w, true);
            }
        }

        void GuestStore()
        {
            var now = Counts(Ship.store);
            var delta = new Dictionary<string, int>();
            foreach (var kv in now) { storeBase.TryGetValue(kv.Key, out int was); if (kv.Value != was) delta[kv.Key] = kv.Value - was; }
            foreach (var kv in storeBase) if (!now.ContainsKey(kv.Key)) delta[kv.Key] = -kv.Value;
            if (delta.Count == 0) return;
            storeSeq++;
            storePending.Add((storeSeq, delta));
            storeBase = now;
            var w = new NetW().U8(M_STOREDELTA).I32(storeSeq); WriteCounts(w, delta);
            ToHost(w, true);
        }

        void GuestStoreArrived(int ack, Dictionary<string, int> host)
        {
            storePending.RemoveAll(p => p.seq <= ack);
            var want = new Dictionary<string, int>(host);
            foreach (var p in storePending) foreach (var kv in p.delta) { want.TryGetValue(kv.Key, out int k); want[kv.Key] = k + kv.Value; }
            var clean = new Dictionary<string, int>(); foreach (var kv in want) if (kv.Value > 0) clean[kv.Key] = kv.Value;
            SetStore(clean);
            storeBase = Counts(Ship.store);
        }

        // ---- a crewmate's PC ------------------------------------------------------------------------------------------
        readonly Interp<(Vector3 pos, Quaternion rot)> shipPose = new Interp<(Vector3, Quaternion)>();
        readonly Dictionary<int, float> patchAcc = new Dictionary<int, float>();
        readonly Dictionary<int, float> patchedAt = new Dictionary<int, float>();

        void HostTime(float t)
        {
            float sample = t - Time.time;
            if (!offsetSet) { offset = sample; offsetSet = true; }
            else offset = sample > offset ? Mathf.Lerp(offset, sample, 0.3f) : Mathf.Lerp(offset, sample, 0.01f);
            Waves.TimeOffset = offset;
        }

        void GuestTick(float dt)
        {
            var d = D; var ship = Ship;
            // her pose, smoothly between the host's words
            if (ship && shipPose.At(Now - 0.1f, out var a, out var b, out float k))
                ship.Body.SetPositionAndRotation(Vector3.Lerp(a.pos, b.pos, k), Quaternion.Slerp(a.rot, b.rot, k));
            if (KiteSub.I && d && d.piloting != null) KiteSub.I.pilotSeat = mySeat;
            poseT += dt;
            if (poseT >= 1f / 15f)
            {
                poseT = 0;
                var w = new NetW().U8(M_POSE).F(Now); var p = MyPose(); p.Write(w);
                if (p.mode == 3) { w.Bool(KiteSub.I); if (KiteSub.I) WriteKite(w, KiteSub.I); }
                ToHost(w, false);
            }
            // the helm and the telegraph, while this hand is on them
            bool atHelm = d && d.manning != null && (d.manning.kind == "helm" || d.manning.kind == "telegraph");
            if (atHelm) ctlHold = 1f; else ctlHold -= dt;
            ctlT += dt;
            if (atHelm && ctlT >= 0.1f && ship)
            {
                ctlT = 0;
                int mask = d.manning.kind == "helm" ? 3 : 1;
                ToHost(new NetW().U8(M_SHIPCTL).U8(mask).U8(ship.telegraph).F(ship.rudder).U8((ship.holdHeading ? 1 : 0) | (ship.holdDepth ? 2 : 0)).F(ship.headingOrder).F(ship.depthOrder), false);
            }
            patchT += dt;
            if (patchT >= 0.1f && patchAcc.Count > 0)
            {
                patchT = 0;
                foreach (var kv in patchAcc) { ToHost(new NetW().U8(M_CMD).U8(C_PATCH).I32(kv.Key).F(kv.Value), true); patchedAt[kv.Key] = Time.time; }
                patchAcc.Clear();
            }
            storeT += dt;
            if (storeT >= 0.2f && ship) { storeT = 0; GuestStore(); }
        }

        void GuestHandle(byte[] b)
        {
            var r = new NetR(b); int type = r.U8();
            if (type == M_WELCOME) { if (!worldUp && welcome == null) ReadWelcomeHead(b); return; }
            if (type == M_ROSTER) { Roster(r); return; }
            if (!worldUp) return;
            var ship = Ship;
            switch (type)
            {
                case M_POSES:
                {
                    float t = r.F(); HostTime(t);
                    int n = r.U8();
                    for (int i = 0; i < n; i++)
                    {
                        int seat = r.U8(); var p = MatePose.Read(r);
                        if (seat == mySeat) continue;
                        if (!mates.TryGetValue(seat, out var m) || !m) { m = Mate.Make(seat, "Diver"); mates[seat] = m; }
                        m.Heard(t, p);
                    }
                    if (r.Bool())
                    {
                        var pos = r.V3(); float hd = r.F(); var vel = r.V3s(); bool flipped = r.Bool(); float stroke = r.F(); int occ = r.U8();
                        if (!Raft.I) { Raft.Spawn(pos, hd); }
                        var raft = Raft.I; raft.mirror = true;
                        raft.netPos = pos; raft.netHeading = hd; raft.netVel = vel; raft.flipped = flipped; raft.occupied = occ;
                        if (!(D && D.onRaft == raft && Input.GetKey(KeyCode.W))) raft.stroke = stroke;
                    }
                    if (r.Bool())
                    {
                        int pilot = r.U8();
                        if (!KiteSub.I && ship) KiteSub.Spawn(ship);
                        bool mine = D && D.piloting != null;
                        ReadKite(r, KiteSub.I, !mine);
                        if (!mine && KiteSub.I) KiteSub.I.pilotSeat = pilot == 255 ? -1 : pilot;
                    }
                    break;
                }
                case M_SHIP: ApplyShip(r, ship); break;
                case M_SEA:
                {
                    float t = r.F(); int n = r.U8();
                    var life = Life.I; if (!life) break;
                    for (int i = 0; i < n; i++)
                    {
                        int id = r.I32(); int si = r.I16(); var pos = r.V3(); var vel = r.V3s();
                        float yaw = r.U8() / 255f * 360f, pitch = r.U8() / 255f * 180f - 90f;
                        float sizeK = r.U8() / 255f + 0.5f; var st = (CState)r.U8(); float hp = r.U8() / 255f;
                        if (si < 0 || si >= SpeciesBook.All.Count) continue;
                        var sp = SpeciesBook.All[si];
                        var fwd = Quaternion.Euler(-pitch, yaw, 0) * Vector3.forward;
                        life.Heard(id, sp, pos, vel, fwd, sp.size * sizeK, st, hp, t);
                    }
                    break;
                }
                case M_WORLD:
                {
                    float t = r.F(); HostTime(t);
                    var c = DeepBoot.I.clock;
                    float hour = r.F(); int day = r.I32();
                    if (Mathf.Abs(Mathf.DeltaAngle(c.hour * 15f, hour * 15f)) > 0.75f || c.day != day) { c.hour = hour; c.day = day; }
                    else c.hour = Mathf.Repeat(c.hour + Mathf.DeltaAngle(c.hour * 15f, hour * 15f) / 15f * 0.2f, 24f);
                    float storm = r.F(), target = r.F(), flash = r.F();
                    var wx = Weather.I;
                    if (wx) { wx.mirror = true; wx.target = target; if (Mathf.Abs(wx.storm - storm) > 0.02f) wx.storm = storm; if (flash > wx.flash + 0.3f) wx.flash = flash; }
                    int opening = r.U8(); float ot = r.F();
                    if (Opening.I != null && !Opening.I.done)
                    {
                        if (opening != 1) Opening.I.EndForLateJoin();
                        else Opening.I.t = Mathf.Lerp(Opening.I.t, ot, 0.5f);
                    }
                    break;
                }
                case M_STORE: { int ack = r.I32(); GuestStoreArrived(ack, ReadCounts(r)); break; }
                case M_FLORA: { string kind = r.S(); long key = r.L64(); var pos = r.V3(); float secs = r.F(); DeepBoot.I.flora?.TakeAt(kind, key, pos, secs); break; }
                case M_DEPOSIT: { int i = r.I32(); float secs = r.F(); Deposits.I?.TakeIndex(i, secs); break; }
                case M_HURT: { float dmg = r.F(); string by = r.S(); D?.Hurt(dmg, by); break; }
                case M_ALERT: { string msg = r.S(); if (D && (D.aboard || msg.Contains("raft"))) D.Toast(msg); break; }
                case M_TEST: TestStep(r); break;
            }
        }

        void Roster(NetR r)
        {
            int n = r.U8(); var seen = new HashSet<int>();
            for (int i = 0; i < n; i++)
            {
                int seat = r.U8(); string name = r.S();
                if (seat == mySeat) continue;
                seen.Add(seat);
                if (!mates.TryGetValue(seat, out var m) || !m) { m = Mate.Make(seat, name); mates[seat] = m; if (worldUp) Life.I?.AddDiver(m); }
                m.mateName = name; m.gameObject.name = "Mate " + name;
            }
            foreach (var s in new List<int>(mates.Keys)) if (!seen.Contains(s)) DropMate(s);
        }

        void ApplyShip(NetR r, Nautilus n)
        {
            if (!n) return;
            var sy = n.sys;
            float t = r.F(); HostTime(t);
            var pos = r.V3(); var rot = r.Q();
            shipPose.Add(t, (pos, rot));
            n.speed = r.F(); n.vSpeed = r.F(); n.heading = r.F();
            int tele = r.U8(); float rud = r.F(); int flags = r.U8(); float ho = r.F(), dor = r.F();
            var dv = D;
            bool atHelm = dv && dv.manning != null && (dv.manning.kind == "helm" || dv.manning.kind == "telegraph");
            if (ctlHold <= 0 && !atHelm)
            {
                n.telegraph = tele; n.rudder = rud; n.holdHeading = (flags & 1) != 0; n.holdDepth = (flags & 2) != 0; n.headingOrder = ho; n.depthOrder = dor;
            }
            sy.engineRepaired = (flags & 4) != 0; sy.breakersTripped = (flags & 8) != 0; sy.pumpsRunning = (flags & 16) != 0;
            sy.state = (PowerState)r.U8(); sy.battery = r.F(); sy.fuel = r.F();
            float reset = r.F(); if (!sy.breakersTripped || reset > sy.resetT) sy.resetT = reset;
            n.crushDepth = r.F(); sy.NoiseDb = r.F(); n.grounded = r.F();
            n.power = sy.state != PowerState.Dead;
            int rooms = r.U8();
            for (int i = 0; i < rooms; i++) { float lv = r.F(); if (i < sy.rooms.Count) sy.rooms[i].level = lv; }
            int nb = r.U8(); var ids = new HashSet<int>();
            for (int i = 0; i < nb; i++)
            {
                int id = r.I32(); int room = r.U8(); var gen = r.V3(); float size = r.F(), patch = r.F();
                ids.Add(id);
                ShipSystems.Breach have = null; foreach (var x in sy.breaches) if (x.id == id) have = x;
                if (have == null)
                {
                    // one we've just finished patching here, the host not yet: don't bring it back for a moment
                    if (patchedAt.TryGetValue(id, out float pt) && Time.time - pt < 1.5f) continue;
                    have = sy.MirrorBreach(id, room, gen, size);
                }
                have.size = size; if (!patchedAt.TryGetValue(id, out float pa) || Time.time - pa > 1f) have.patch = patch;
            }
            for (int i = sy.breaches.Count - 1; i >= 0; i--) if (!ids.Contains(sy.breaches[i].id)) sy.DropBreach(sy.breaches[i]);
        }

        // ---- the static doors the rest of the game calls (no-ops when playing alone) --------------------------------
        public static void Cmd(byte c, int a = 0, float f = 0)
        {
            if (!IsGuest || I == null || !I.worldUp) return;
            I.ToHost(new NetW().U8(M_CMD).U8(c).I32(a).F(f), true);
        }
        public static void Alert(string msg)
        {
            if (!IsHost || I == null || string.IsNullOrEmpty(msg)) return;
            I.ToCrew(new NetW().U8(M_ALERT).S(msg), true);
        }
        public static void HurtMate(ISense who, float dmg, string by)
        {
            if (!IsHost || I == null || !(who is Mate m)) return;
            I.Send(m.clientId, new NetW().U8(M_HURT).F(dmg).S(by ?? "something"), true);
        }
        public static void SendWound(int id, float dmg, Vector3 from, bool cauterise, string how)
        {
            if (!IsGuest || I == null) return;
            I.ToHost(new NetW().U8(M_WOUND).I32(id).F(dmg).V3(from).Bool(cauterise).S(how ?? ""), true);
        }
        public static void RaftOars(int seat, float row, float turn)
        {
            if (!IsGuest || I == null) return;
            I.oarsT += Time.deltaTime;
            if (I.oarsT < 0.1f) return;
            I.oarsT = 0;
            I.ToHost(new NetW().U8(M_OARS).U8(seat).F(row).F(turn), false);
        }
        public static void PatchWork(int id, float dt)
        {
            if (!IsGuest || I == null) return;
            I.patchAcc.TryGetValue(id, out float k); I.patchAcc[id] = k + dt;
        }

        // the crewmates' helmet lamps, for the shaders' lamp list (Nautilus.cs)
        public static IEnumerable<Mate> Crew()
        {
            if (I == null) yield break;
            foreach (var m in I.mates.Values) if (m) yield return m;
        }
    }
}
