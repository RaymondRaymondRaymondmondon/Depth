# Depth: Deep Arcade networking

The shared multiplayer layer for the four Deep Arcade games (Scuttle, Flats Duel, the Trawl, Fathoms). This design follows two references:
- the Master Reference's "Shared networking layer" table (host-authoritative, reliable and unreliable channels, LAN broadcast, a virtual LAN for now, a small relay server later, version check, 2-minute reconnects, a shared frame, profiles in the save)
- the Fathoms document's "Multiplayer and networking" section (20-tick host, delta and fog-filtered updates, a bandwidth budget, the connection modes, a headless server, autosave and rejoin)

It is written for the actual audience: **you and a few friends, 2 to 6 players per match, at most two servers.**

**The user's answers (2026-09-30):**
- The friends are all in the United States, in different states.
- There is no preferred cloud provider.
- **Depth will go on Steam so the friends can play it with the user.** Until then, the user tests it alone.

This makes **Steam the online path**: Valve's relay network and Steam lobbies replace the join-code server, which is demoted to an optional fallback (section 4). The GNS source is in the repo root as `GameNetworkingSockets-master.zip`.

## 1. Decisions in one place

| Question | Decision | Why |
|---|---|---|
| Who runs the game? | **The host's game is the authority.** Clients send commands and draw what the host sends back. | Both references require it. A cheating client can't do anything the host doesn't allow, and nothing has to be deterministic. |
| Networking library | **GameNetworkingSockets (GNS)**, Valve's open-source library. | It provides reliable and unreliable messages, encryption, and router traversal (ICE). It is the same API as Steam's networking, so a later Steam build swaps the backend, not the game code (section 6). |
| Fallback library | ENet, only if GNS won't build cleanly on this PC. | ENet is small and easy, but it has no encryption, no router traversal and no Steam parity. |
| LAN | Broadcast discovery plus direct connect. No server. | Section 3. |
| Online, before Steam | A virtual LAN (ZeroTier) or Direct IP. | These work as soon as LAN works, with no server to write or pay for. They're enough for testing. |
| Online, proper | **Steam**: Valve's relay network (SDR), Steam lobbies and friend invites. | The user is putting Depth on Steam. Steam hides IPs, gets through routers and runs the relays, and no server of ours is needed. Section 6. |
| Our own server | Optional fallback only: one small US-central VM (lobby + coturn) if a non-Steam build is ever wanted. | Friends in several US states are all well served by one central server, so the second server isn't needed. Section 4. |

## 2. The shape of the code

```
  games (Scuttle, Flats Duel, Trawl, Fathoms)
        |  ArcadeGame interface: commands in, snapshots out
  arcade_session   host/client roles, slots, handshake, heartbeats, reconnect, pause, AI takeover
  arcade_lobby     the lobby: slots, ready levers, settings, chat, Launch
        |  NetTransport interface
  net_gns.cpp      GameNetworkingSockets backend (now)      net_steam.cpp  Steam backend (later)
  net_discovery    LAN beacon and Browse list               lobby_client   join codes and signaling to the server
```

### NetTransport (net_transport.h)
This is the only file that knows which library is underneath.

```cpp
struct NetAddr { std::string text; };                 // "192.168.1.20:47778", or a peer id for P2P/Steam
enum NetChannel { CH_CONTROL, CH_STATE, CH_BULK };    // reliable ordered / unreliable / reliable large
struct NetEvent { enum Kind { Connected, Disconnected, Message } kind; int conn; NetChannel ch; std::vector<uint8_t> data; };
class NetTransport {
public:
    virtual bool Listen(uint16_t port) = 0;                          // host
    virtual int  Connect(const NetAddr& to) = 0;                     // client: direct address
    virtual int  ConnectP2P(const std::string& peerId) = 0;          // client: through the lobby server's signaling
    virtual void Send(int conn, NetChannel ch, const void* p, int n) = 0;
    virtual void Poll(std::vector<NetEvent>& out) = 0;               // once per frame
    virtual void Close(int conn) = 0;
    virtual NetStats Stats(int conn) = 0;                            // ping, loss, bytes per second
};
```

### ArcadeGame (arcade_game.h)
Each game plugs in with this interface. The session layer never looks inside a game's messages.

```cpp
class ArcadeGame {
public:
    virtual void HostStart(const LobbySettings&, const std::vector<Seat>&) = 0;
    virtual void HostCommand(int seat, const uint8_t* cmd, int n) = 0;   // validated here: owns it, can afford it, it's legal
    virtual void HostTick(float dt) = 0;
    virtual void BuildSnapshot(int seat, Writer& w, int baselineTick) = 0; // only what that seat may see
    virtual void ClientSnapshot(Reader& r) = 0;
    virtual void SeatLost(int seat) = 0;  virtual void SeatBack(int seat) = 0;   // pause, AI takeover, rejoin
    virtual void Draw(float dt) = 0;
    virtual std::vector<uint8_t> SaveMatch() = 0;  virtual void LoadMatch(const uint8_t*, int) = 0;
};
```

What each game sends:

| Game | Model | Host tick | Snapshot | Hidden information |
|---|---|---|---|---|
| Scuttle | turn-based | on change | whole table state, small | each seat gets only its own hand; others see card backs and bet tokens face down |
| Flats Duel | turn-based, `flats_board` engine | on change | whole board plus event list, which the client plays back as animation | never send the opponent's hand, deck order or sideboard (the Master Reference's packet-log test checks this) |
| The Trawl | real-time co-op | 20 Hz | positions, station states, threats | none (co-op), but everything happens at the host |
| Fathoms | real-time RTS | 20 Hz | delta-compressed, positions to 1/16 tile, only changed units | **fog filter:** a unit is never sent to a seat that can't see it |

Clients draw about 100 ms behind the host and interpolate between snapshots. A command gets instant local feedback (the click marker and the unit's acknowledgement sound), and the host's confirmation follows 50–150 ms later.

### Message format
- Every message is `[u8 type][payload]`, little-endian, with varints for counts. `Writer`/`Reader` helpers go in `net_msg.h`.
- CH_CONTROL (reliable, ordered): handshake, lobby changes, chat, commands, pause and resign.
- CH_STATE (unreliable): snapshots. A lost one is replaced by the next, and each snapshot carries the last tick the client acknowledged, so the delta is taken from something the client actually has.
- CH_BULK (reliable): large one-off payloads, such as the Fathoms map seed and settings, and a match save sent to a rejoining player.

### The handshake
1. Client to host: `HELLO {protocol, build id, data hash, game id, display name, profile id, rejoin token?}`.
   - build id: the git commit, baked in by CMake.
   - data hash: an FNV hash of every balance table, so `data.cpp`'s numbers must match.
   - profile id: a random 64-bit id, saved once in `depth_save.txt` with the display name and arcade tokens.
2. Host to client: `WELCOME {seat, lobby state}` or `REJECT {reason}`. Reasons: a different build, a different data, the lobby is full, the match has started without this player, or a wrong code.
3. Heartbeats every 1 s both ways. A seat with no traffic for 6 s is **lost**.
   - The game pauses for up to **2 minutes** (both references).
   - If the player comes back with their rejoin token, they take their seat again and get the match state on CH_BULK.
   - Otherwise an **AI takes the seat** (Scuttle's simple bot, the Flats auto-player, the Trawl's crew bot, a Fathoms AI) until they return.
4. Pauses: each player gets 3 pauses of up to 60 s per match (from Fathoms). The host can always pause.
5. Autosave: the host saves the match every 3 minutes (Fathoms; the Trawl saves between nights). If the host crashes, they reload the save and re-open the lobby **with the same join code**, and everyone rejoins. There is no host migration; that's out of scope for a game among friends.

## 3. The LAN game

**Hosting.**
1. Arcade → Host → pick a game. The session opens a GNS listen socket on **UDP 47778** and the lobby screen appears.
2. The host broadcasts a **beacon** every second on **UDP 47777**: `"DPTH" | protocol | game id | lobby name | players/max | port | join code | in progress?`. It goes to 255.255.255.255 and to each network adapter's subnet broadcast address, because Windows machines with several adapters (VirtualBox, VPNs) otherwise miss each other.
3. The first time you host, Windows Firewall asks to allow depth.exe. The host screen should say so ("Allow access when Windows asks").

**Joining.**
- **Browse** listens on 47777 and lists every beacon heard in the last 4 s: game, lobby name, seats, ping. Click one to join.
- **Join** takes a 6-character code. On a LAN the code is simply matched against the beacons, so a code works on LAN and online alike.
- **Direct IP** is a small "Connect to address" field for anyone who wants it.

**The lobby** (from the Master Reference):
- a roster board of 2–6 slots (per game)
- display names
- a faction or role pick
- a ready lever per player
- host settings; Fathoms has its full settings table
- chat
- a Launch handle that lights when everyone is ready

The host can fill empty slots with AI or close them. Fathoms rule: faction picks are hidden until the host locks the lobby.

**Test without a second PC:** `depth.exe --net-loop <game>` runs a host and two clients inside one process over loopback, plays a scripted match, and checks that everyone ends in the same state. For Flats Duel it also checks from the packet log that no client ever received the opponent's hand. `--net-loop <game> lag` adds GNS's built-in fake lag and packet loss (100 ms, 2%) to prove the interpolation and reconnect paths.

## 4. The online game without Steam (optional fallback)

With Steam planned, **this section isn't on the build path.** Rungs 1 and 2 are how to test with a friend before the Steam build exists. Rung 3 is kept for the record, in case a non-Steam build is ever wanted. If it is, one server in the central US (Chicago or Dallas) covers friends in any state at under 60 ms.

The online options come in rungs; each works before the next exists:

| Rung | What you do | Code needed | Server | Cost |
|---|---|---|---|---|
| 1. Virtual LAN | Everyone installs **ZeroTier** (it passes LAN broadcasts, so Browse just works) or **Tailscale**. Tailscale drops broadcasts, so joiners use Direct IP with the host's 100.x address. | none beyond LAN | none | free |
| 2. Direct IP | The host forwards UDP 47778 on their router; friends type the host's public address | none beyond LAN | none | free |
| 3. **Join codes** (the main mode) | Host gets a code like `KRAKEN` → friends type it → connected, whatever their routers | lobby client + server | 1 (or 2) small cloud servers | about $5 a month each |

### Rung 3 in detail

**On the server** (one small Linux VM: 1 vCPU, 1 GB RAM. Hetzner, DigitalOcean, Vultr, or Oracle Cloud's free tier):
1. **`depth-lobby`**, a small C++ program of a few hundred lines in `relay/`, built with CMake like the game. It listens on TCP 7777 (TLS optional; the join code is the secret) and does three jobs:
   - **Rooms and join codes.** A host registers a room and gets a 6-character code from an alphabet without look-alikes (no 0/O, 1/I). Rooms expire 10 minutes after the host disappears. Codes are only accepted when the host's game and data hash match the joiner's, so the server also enforces the version check.
   - **Signaling.** It passes the connection offers and ICE candidates between the host and each joiner. GNS's P2P mode (`ConnectP2PCustomSignaling`) needs exactly this and nothing else.
   - **Browse Online.** It can list open public rooms, though for friends the code is enough.
2. **coturn**, the standard open-source STUN/TURN server, needs no code from us:
   - STUN on UDP 3478 lets GNS discover each player's public address and punch through home routers. This succeeds for most home connections, and then traffic flows **directly between players**, not through the server.
   - TURN relays the traffic when punching fails (strict or mobile-carrier NAT). The lobby hands each player short-lived TURN credentials (HMAC with a shared secret, coturn's standard "REST API" auth), so the relay isn't open to the world.
3. Both run from one `docker-compose.yml` in `relay/`, with a README: rent a VM, install Docker, copy the folder, `docker compose up -d`, open the ports (TCP 7777, UDP 3478, UDP 49152–49999 for TURN).

**In the game:**
- `servers.txt` next to the exe lists up to two lobby servers, for example `lobby-us.example.net:7777` and `lobby-eu.example.net:7777`.
- When hosting, the game pings both and registers with the nearer one.
- **The first letter of the join code says which server holds the room**, so a joiner's game knows where to look without asking both.
- If one server is down, hosting falls back to the other.
- Two servers only make sense if your friends are spread across continents; otherwise one server does everything and the second line stays empty.

**Bandwidth and cost:**
- Direct connections cost the server nothing.
- Relayed traffic follows Fathoms' estimate of 10–20 KB/s per client: a relayed 6-player match is about 1 Mbps. The cheapest VMs include 1–20 TB a month, so a group of friends will never come close.
- Scuttle and Flats Duel use almost nothing.

**Security for a friends' game:**
- GNS encrypts every connection end to end.
- The host validates every command.
- The lobby rate-limits code guesses (5 wrong codes a minute per IP).
- There are no accounts and no passwords stored anywhere.

### Optional: a headless host
`depth.exe --host-headless <game> [--code KRAKEN]` runs a match with no window, on the server or any PC, so no player has to host. This is useful for long Fathoms matches, and the Fathoms document plans it for later. It needs the game's simulation to run without raylib's window: the Flats engine already does, and Fathoms is designed that way. One small VM can run one or two headless Fathoms matches.

## 5. What stays out, deliberately
- Accounts, matchmaking with strangers, leaderboards, anti-cheat beyond host authority.
- Host migration: the host's autosave and a re-host with the same code cover it.
- Deterministic lockstep: both references rule it out. Replays, when they come, record the host's snapshots.
- Anything that makes the main game depend on the network. The arcade never pays gold into the single-player economy (from the Master Reference).

## 6. Steam: the online path (the user plans to release there)

Steam gives, for free:
- **Steam Datagram Relay:** Valve's relay network, which hides players' IP addresses. No server of our own is needed.
- **Steam lobbies:** these replace join codes.
- **Friends-list invites** and "Join Game" from the overlay.

It also costs something:
- a $100 Steam Direct fee per game
- the Steamworks SDK and its agreement
- **every player needs Steam and a copy of the game** (or a playtest key)

Friends can play before the public release through Steam's **Playtest** feature or with **keys** from the Steamworks site.

**Testing alone before an App ID exists:**
- Two copies of depth.exe on one PC can play each other over the LAN mode (127.0.0.1), and --net-loop runs a whole match inside one program. That covers everything except Steam's own services.
- Steam's own services (lobbies, invites, the relay network) need **two Steam accounts on two PCs**, because Steam doesn't let one account join itself. Until the  App ID exists, development can use Valve's public test App ID **480 (Spacewar)**.
- A spare laptop with a second (free) Steam account is enough.

**What makes a later switch cheap:**
- **GNS is the open-source version of Steam's own networking library**, with the same classes and calls (`ISteamNetworkingSockets`, connections, messages, lanes). The game already speaks that API, so `net_steam.cpp` is a thin second backend behind `NetTransport`.
- The two can't sensibly be linked into one exe, so the backend is chosen when building: `cmake -DDEPTH_STEAM=ON`.
- The lobby layer has one more seam, `LobbyDirectory`, with two implementations:
  - `LobbyServerDirectory`: join codes through `depth-lobby`
  - `SteamLobbyDirectory`: Steam lobbies and invites, where the join code simply becomes the lobby's metadata so the Join box still works
- Nothing in the games or the session layer changes.
- Profiles stay in `depth_save.txt`; on Steam the profile id becomes the Steam id.

**Estimated work for the Steam backend:** a few days to a week of code (the SDK, lobbies, invites, the rich-presence "Join" button), plus Valve's store setup, which is paperwork rather than code.

**What the user needs to get:** the **Steamworks SDK** (from partner.steamgames.com; the download needs a free Steamworks partner account, and the SDK isn't on GitHub). The  Steam Direct fee buys the App ID, which is needed for keys, playtests and release, but not for development against App ID 480.

## 7. Build and dependencies
- **GNS on Windows:** built from `GameNetworkingSockets-master.zip` (the user's download) with `-DUSE_CRYPTO=BCrypt`, which uses Windows' own cryptography, so there's no OpenSSL. Its native ICE client (`ENABLE_ICE`, on by default) handles router traversal, so there's no WebRTC. **Its one dependency, protobuf, comes through the vcpkg that ships with Visual Studio 2026** (`VC\vcpkg`, confirmed present), using GNS's own `vcpkg.json` manifest. Nothing else needs downloading. For a reproducible build, a tagged release (v1.4.x) is safer than a `master` snapshot; it works either way.
- This is the one heavy dependency in an otherwise self-contained build. If it fights the VS 2026 toolchain, the fallback is ENet for LAN and Direct IP, plus our own simple UDP relay in `depth-lobby` instead of coturn. That fallback gives up encryption and Steam parity.
- The lobby server builds on Linux with the same CMake and needs only the C++ standard library and sockets.
- Ports:

  | Port | Protocol | Use |
  |---|---|---|
  | 47777 | UDP | LAN discovery |
  | 47778 | UDP | game |
  | 7777 | TCP | lobby server |
  | 3478 | UDP | STUN/TURN |
  | 49152–49999 | UDP | TURN relays |

## 8. Build order (Master Reference stage 11, then 12–14)

| Step | Work | Check |
|---|---|---|
| N1 | `NetTransport` on GNS; `arcade_session` handshake, heartbeats, version check; the LAN beacon and Browse; the lobby screen; Scuttle as the first game | `--net-loop scuttle` passes; two PCs on your network finish a Scuttle match |
| N2 | Direct IP, and a short "play over ZeroTier or Tailscale" guide in the arcade's help | a friend elsewhere finishes a match over ZeroTier |
| N3 | Reconnect (2-minute pause, rejoin token), AI takeover, pause budget, the host's autosave and re-host; profiles and tokens in the save | `--net-loop scuttle lag` survives a dropped client and a rejoin |
| N4 | `net_steam.cpp` + `SteamLobbyDirectory` behind `-DDEPTH_STEAM=ON`: Steam lobbies, invites, the relay network (App ID 480 until Depth's own) | two PCs with two Steam accounts join from an invite (this replaces the Master Reference's join-code gate) |
| N5 | Flats Duel over the layer | `--flats-duel-sim` 48–52%; the packet-log test shows no hand leaked; a best-of-three with a friend |
| N6 | The Trawl, then Fathoms (20-tick snapshots, delta, fog filter, `--host-headless`) | their own gates in the Master Reference and the Fathoms document |
| N7 (only if wanted) | `relay/`: `depth-lobby` + coturn for a non-Steam build, one US-central server | two PCs on different networks join by code |

## 9. Settled
- One region (the US): one server would be enough, and with Steam, none is needed.
- Steam is the release and online platform; its backend moves up to step N4.
- No cloud provider is needed unless step N7 is ever built.
