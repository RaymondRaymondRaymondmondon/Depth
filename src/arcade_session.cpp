// The Deep Arcade's session: lobby, handshake, heartbeats, drop/rejoin, and the host-authoritative game (see arcade_session.h).
#include "arcade_session.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef DEPTH_BUILD_STAMP
#define DEPTH_BUILD_STAMP "dev"
#endif

namespace arcade {

enum Msg : uint8_t { M_HELLO = 1, M_WELCOME, M_REJECT, M_LOBBY, M_READY, M_CHAT, M_LAUNCH, M_ACTION, M_STATE, M_PING, M_PONG, M_BYE, M_STATE_PART };
// a real-time snapshot bigger than this goes as parts (GameNetworkingSockets sends an unreliable message over about
// 16 KB as a reliable one, which would queue behind loss); a guest puts them back together, and a lost part only
// loses that one snapshot
constexpr size_t STATE_PART = 12000;

uint32_t BuildId() { static const char* s = DEPTH_BUILD_STAMP; return Fnv1a(s, strlen(s)); }
std::string MakeCode(uint32_t seed) {
    static const char A[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string c;
    uint32_t r = seed ? seed : 0x1234567u;
    for (int i = 0; i < 6; i++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; c += A[r % 32]; }
    return c;
}
static uint32_t Rnd(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }

Session::~Session() { Leave(); }

void Session::Log(const std::string& s) { chat.push_back(s); if (chat.size() > 40) chat.erase(chat.begin()); }

bool Session::Host(const Profile& p, int g, std::string* err, uint16_t port, std::unique_ptr<net::Transport> t, bool withBeacon) {
    Leave();
    me = p;
    if (!t) {
        std::string e;
        if (!net::Init(&e)) { if (err) *err = e; return false; }
        t = net::MakeTransport();
    }
    if (!t || !t->Listen(port, err)) return false;
    tr = std::move(t);
    role = R_HOST; stage = S_LOBBY; game = g;
    rng = (uint32_t)(p.id ^ (p.id >> 32)) ^ (uint32_t)(now * 1000) ^ 0xA5A5F00Du; Rnd(rng);
    code = MakeCode(Rnd(rng));
    hostName = p.name;
    for (auto& s : seats) s = SeatInfo{};
    seats[0].used = true; seats[0].host = true; seats[0].ready = true; seats[0].name = p.name; seats[0].id = p.id;
    mySeat = 0;
    chat.clear(); snapshot.clear();
    Log("You are hosting " + std::string(Info(g).name) + ". Code " + code + ".");
    beaconOn = withBeacon && beacon.Start();
    status = "Hosting";
    return true;
}

bool Session::Join(const Profile& p, const std::string& addr, std::string* err, uint32_t token, std::unique_ptr<net::Transport> t) {
    Leave();
    me = p;
    if (!t) {
        std::string e;
        if (!net::Init(&e)) { if (err) *err = e; return false; }
        t = net::MakeTransport();
    }
    if (!t) { if (err) *err = "no network"; return false; }
    tr = std::move(t);
    server = tr->Connect(addr, err);
    if (server < 0) { tr.reset(); return false; }
    role = R_CLIENT; stage = S_CONNECTING; hostAddr = addr; rejoinToken = token;
    heardHost = -1;   // (the clock starts at the next Update: `now` may be stale)
    lastSeq = 0;
    chat.clear(); snapshot.clear();
    status = "Connecting to " + addr + "...";
    return true;
}

void Session::Leave() {
    if (tr) {
        Writer w; w.U8(M_BYE); w.Str(role == R_HOST ? "The host closed the table." : me.name + " left.");
        if (role == R_HOST) Broadcast(w); else if (server >= 0) SendTo(server, w);
        tr->CloseAll();
        tr.reset();
    }
    if (beaconOn) { beacon.Stop(); beaconOn = false; }
    role = R_NONE; stage = S_IDLE; server = -1; mySeat = -1; paused = false; pending.clear();
    truth.reset();
    for (int& c : playerSeat) c = -1;
}

void Session::End(const std::string& why) {
    endReason = why; status = why;
    if (tr) { tr->CloseAll(); tr.reset(); }
    if (beaconOn) { beacon.Stop(); beaconOn = false; }
    stage = S_ENDED; server = -1;
}

// ---- sending
void Session::SendTo(int conn, const Writer& w, net::Channel ch) { if (tr && conn >= 0) tr->Send(conn, ch, w.b.data(), (int)w.b.size()); }
void Session::Broadcast(const Writer& w) { for (auto& s : seats) if (s.used && s.conn >= 0 && !s.lost) SendTo(s.conn, w); }
void Session::SendLobby() {
    Writer w; w.U8(M_LOBBY); w.U8(stage); w.U8(game); w.U8(paused); w.F32(pauseLeft);
    for (auto& s : seats) {
        w.U8(s.used);
        if (!s.used) continue;
        w.U8((s.ai ? 1 : 0) | (s.ready ? 2 : 0) | (s.lost ? 4 : 0) | (s.host ? 8 : 0));
        w.Str(s.name); w.U16((uint32_t)std::clamp(s.ping, 0, 65535));
    }
    for (int k = 0; k < MAX_PLAYERS; k++) w.U8((uint8_t)(playerSeat[k] + 1));
    Broadcast(w);
}
// every player gets the game as they may see it; real-time games go unreliable with a sequence number (newest wins)
void Session::SendState() {
    if (!truth) return;
    bool rt = Info(game).realtime;
    snapSeq++;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        SeatInfo& s = seats[i];
        if (!s.used || s.conn < 0 || s.lost) continue;
        Writer body; truth->Snapshot(PlayerOfSeat(i), body);
        if (rt && body.b.size() > STATE_PART) {
            uint32_t n = (uint32_t)((body.b.size() + STATE_PART - 1) / STATE_PART);
            for (uint32_t k = 0; k < n && n < 256; k++) {
                size_t at = k * STATE_PART, len = std::min(STATE_PART, body.b.size() - at);
                Writer w; w.U8(M_STATE_PART); w.U32(snapSeq); w.U8(k); w.U8(n); w.Bytes(body.b.data() + at, len);
                SendTo(s.conn, w, net::CH_STATE);
            }
            continue;
        }
        Writer w; w.U8(M_STATE); w.U32(snapSeq);
        w.Bytes(body.b.data(), body.b.size());
        SendTo(s.conn, w, rt ? net::CH_STATE : net::CH_CONTROL);
    }
    // the host plays too: its screen gets the same filtered view as everyone else
    Writer w; truth->Snapshot(PlayerOfSeat(mySeat), w);
    snapshot = std::move(w.b);
    stateVersion++;
}
void Session::SendChat(int seat, const std::string& text) {
    std::string t = text.substr(0, 120);
    Writer w; w.U8(M_CHAT); w.U8((uint8_t)seat); w.Str(t);
    Broadcast(w);
    Log(seats[seat].name + ": " + t);
}

// ---- seats
int Session::SeatOfConn(int conn) const { for (int i = 0; i < MAX_PLAYERS; i++) if (seats[i].used && seats[i].conn == conn) return i; return -1; }
int Session::PlayerOfSeat(int seat) const { for (int k = 0; k < MAX_PLAYERS; k++) if (playerSeat[k] == seat && seat >= 0) return k; return -1; }
int Session::SeatOfPlayer(int p) const { return p >= 0 && p < MAX_PLAYERS ? playerSeat[p] : -1; }

void Session::SetReady(bool r) {
    if (role == R_HOST) return;
    Writer w; w.U8(M_READY); w.U8(r); SendTo(server, w);
}
void Session::AddAI() {
    if (role != R_HOST || stage != S_LOBBY) return;
    int used = 0; for (auto& s : seats) used += s.used;
    if (used >= Info(game).maxPlayers) return;
    static const char* NAMES[] = {"Bosun Bot", "Old Salt", "Crabby", "Barnacle Bill", "Deckhand"};
    for (int i = 0; i < MAX_PLAYERS; i++) if (!seats[i].used) {
        int nAi = 0; for (auto& s : seats) nAi += s.used && s.ai;
        seats[i] = SeatInfo{}; seats[i].used = seats[i].ai = seats[i].ready = true; seats[i].name = NAMES[nAi % 5];
        Log(seats[i].name + " (AI) takes a seat.");
        SendLobby();
        return;
    }
}
void Session::RemoveSeat(int i) {
    if (role != R_HOST || i <= 0 || i >= MAX_PLAYERS || !seats[i].used || stage != S_LOBBY) return;
    if (seats[i].conn >= 0) {
        Writer w; w.U8(M_BYE); w.Str("The host gave your seat away."); SendTo(seats[i].conn, w);
        tr->Close(seats[i].conn, "kicked");
    }
    Log(seats[i].name + " leaves the table.");
    seats[i] = SeatInfo{};
    SendLobby();
}
void Session::Chat(const std::string& text) {
    if (text.empty()) return;
    if (role == R_HOST) SendChat(0, text);
    else if (role == R_CLIENT) { Writer w; w.U8(M_CHAT); w.U8(0); w.Str(text.substr(0, 120)); SendTo(server, w); }
}
bool Session::CanLaunch(std::string* why) const {
    auto no = [&](const std::string& m) { if (why) *why = m; return false; };
    const GameInfo& gi = Info(game);
    if (role != R_HOST) return no("Only the host can start.");
    if (stage != S_LOBBY) return no("Already playing.");
    if (!gi.built) return no(std::string(gi.name) + " hasn't come aboard yet.");
    int n = 0;
    for (auto& s : seats) if (s.used) { n++; if (!s.ready) return no("Waiting for everyone to be ready."); }
    if (n < gi.minPlayers) return no(gi.minPlayers == 2 ? "Needs two or more: invite someone or add an AI." : "Needs more players.");
    if (n > gi.maxPlayers) return no("Too many seats for this game.");
    return true;
}
bool Session::Launch(std::string* why) {
    if (!CanLaunch(why)) return false;
    truth = MakeGameHost(game);
    if (!truth) { if (why) *why = "That game isn't aboard."; return false; }
    int n = 0;
    for (int& c : playerSeat) c = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) if (seats[i].used) playerSeat[n++] = i;
    truth->Configure(gameOpts);
    truth->Start(n, Rnd(rng) | 1);
    stage = S_PLAYING; lastSnap = now;
    Writer w; w.U8(M_LAUNCH); w.U8(game); Broadcast(w);
    Log("The game begins: " + std::string(Info(game).name) + ".");
    SendLobby();
    SendState();
    return true;
}
void Session::BackToLobby() {
    if (role != R_HOST || stage != S_PLAYING) return;
    stage = S_LOBBY;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        SeatInfo& s = seats[i];
        if (!s.used) continue;
        if (s.lost) { s = SeatInfo{}; continue; }      // whoever never came back leaves the table
        if (!s.ai && !s.host) s.ready = false;
    }
    for (int& c : playerSeat) c = -1;
    truth.reset();
    paused = false;
    SendLobby();
}

bool Session::Rematch(std::string* why) {
    if (role != R_HOST || stage != S_PLAYING || !truth || !truth->Over()) { if (why) *why = "The match isn't over."; return false; }
    BackToLobby();
    for (auto& s : seats) if (s.used) s.ready = true;   // (anyone who'd rather not can leave the table)
    return Launch(why);
}

void Session::Act(const Writer& action) {
    if (stage != S_PLAYING) return;
    if (role == R_CLIENT) {
        Writer w; w.U8(M_ACTION); w.Bytes(action.b.data(), action.b.size()); SendTo(server, w);
    } else if (role == R_HOST && truth && !paused) {
        int p = MyPlayer();
        Reader r(action.b);
        if (p >= 0 && truth->Act(p, r)) SendState();
    }
}

// ---- the host's side
void Session::HostLost(int i, const char* why) {
    SeatInfo& s = seats[i];
    if (!s.used || s.lost) return;
    if (stage == S_PLAYING) {
        s.lost = true; s.lostAt = now; s.conn = -1;
        Log(s.name + " lost the connection. Waiting for them (" + (why ? why : "") + ").");
    } else {
        Log(s.name + " left.");
        s = SeatInfo{};
    }
    SendLobby();
}
void Session::HostHello(int conn, Reader& r) {
    pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending& p) { return p.conn == conn; }), pending.end());
    uint32_t proto = r.U8(), build = r.U32(), data = r.U32();
    std::string name = r.Str(); uint64_t id = r.U64(); uint32_t token = r.U32();
    auto reject = [&](const std::string& why) {
        Writer w; w.U8(M_REJECT); w.Str(why); SendTo(conn, w);
        tr->Close(conn, why.c_str());
    };
    if (r.bad || proto != PROTOCOL) return reject("That copy of Depth speaks a different arcade protocol.");
    if (build != BuildId()) return reject("Different builds of Depth: everyone needs the same version.");
    if (data != DataHash()) return reject("Different game data: everyone needs the same version.");
    if (name.empty()) name = "Diver";
    name = name.substr(0, 20);
    int seat = -1;
    if (token) for (int i = 0; i < MAX_PLAYERS; i++) if (seats[i].used && seats[i].lost && seats[i].token == token) seat = i;
    if (seat >= 0) {
        SeatInfo& s = seats[seat];
        s.lost = false; s.ai = false; s.conn = conn; s.heard = now;
        Log(s.name + " is back.");
    } else {
        if (stage != S_LOBBY) return reject("A game is already under way at that table.");
        int used = 0; for (auto& s : seats) used += s.used;
        if (used >= Info(game).maxPlayers) return reject("The table is full.");
        for (int i = 0; i < MAX_PLAYERS && seat < 0; i++) if (!seats[i].used) seat = i;
        if (seat < 0) return reject("The table is full.");
        // two divers called the same: number the second
        std::string base = name; int k = 2;
        for (bool clash = true; clash;) { clash = false; for (auto& s : seats) if (s.used && s.name == name) { clash = true; name = base + " " + std::to_string(k++); } }
        SeatInfo& s = seats[seat];
        s = SeatInfo{}; s.used = true; s.name = name; s.id = id; s.conn = conn; s.heard = now;
        do s.token = Rnd(rng); while (!s.token);
        Log(name + " joins the table.");
    }
    Writer w; w.U8(M_WELCOME); w.U8((uint8_t)seat); w.U32(seats[seat].token); w.Str(code); w.U8(game); w.Str(hostName);
    SendTo(conn, w);
    SendLobby();
    if (stage == S_PLAYING) SendState();
}
void Session::HostMessage(int conn, Reader& r) {
    int type = r.U8();
    int seat = SeatOfConn(conn);
    if (seat < 0) {
        if (type == M_HELLO && std::any_of(pending.begin(), pending.end(), [&](const Pending& p) { return p.conn == conn; })) HostHello(conn, r);
        return;
    }
    SeatInfo& s = seats[seat];
    s.heard = now;
    switch (type) {
        case M_READY: s.ready = r.U8() != 0; if (!r.bad) SendLobby(); break;
        case M_CHAT: { r.U8(); std::string t = r.Str(); if (!r.bad && !t.empty()) SendChat(seat, t); } break;
        case M_ACTION: {
            int p = PlayerOfSeat(seat);   // a client can only ever act as its own player
            if (stage != S_PLAYING || paused || !truth || p < 0 || s.ai) break;
            if (truth->Act(p, r) && !Info(game).realtime) SendState();
        } break;
        case M_PONG: { uint32_t sent = r.U32(); s.ping = (int)((uint32_t)(now * 1000) - sent); } break;
        case M_BYE:
            tr->Close(conn, "bye");
            HostLost(seat, "left");
            if (stage == S_PLAYING) seats[seat].lostAt = now - TAKEOVER_AFTER;   // they chose to go: the AI steps in at once
            break;
        default: break;
    }
}
void Session::HostTick(float dt) {
    // anyone lost: pause the table; after two minutes the AI plays for them (they can still come back)
    paused = false; pauseLeft = 0;
    for (auto& s : seats) if (s.used && s.lost && !s.ai) {
        double left = TAKEOVER_AFTER - (now - s.lostAt);
        if (left <= 0) { s.ai = true; Log("The AI takes over for " + s.name + "."); SendLobby(); }
        else { paused = true; pauseLeft = std::max(pauseLeft, (float)left); }
    }
    if (paused || !truth) return;
    uint32_t aiMask = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) { int s = playerSeat[p]; if (s >= 0 && seats[s].ai) aiMask |= 1u << p; }
    bool changed = truth->Tick(dt, aiMask);
    const GameInfo& gi = Info(game);
    if (gi.realtime) { if (now - lastSnap >= 1.0 / gi.snapshotHz) { lastSnap = now; SendState(); } }
    else if (changed) SendState();
}

// ---- the client's side
void Session::ClientMessage(Reader& r) {
    heardHost = now;
    int type = r.U8();
    switch (type) {
        case M_WELCOME: {
            mySeat = r.U8(); rejoinToken = r.U32(); code = r.Str(); game = r.U8(); hostName = r.Str();
            if (r.bad) { End("The host sent a garbled welcome."); break; }
            stage = S_LOBBY; status = "At " + hostName + "'s table";
            Log("You join " + hostName + "'s table (" + Info(game).name + ").");
        } break;
        case M_REJECT: rejects++; End(r.Str()); break;
        case M_LOBBY: {
            int st = r.U8(); int g = r.U8(); bool pz = r.U8() != 0; float pl = r.F32();
            SeatInfo ns[MAX_PLAYERS];
            for (int i = 0; i < MAX_PLAYERS; i++) {
                ns[i].used = r.U8() != 0;
                if (!ns[i].used) continue;
                int f = r.U8(); ns[i].ai = f & 1; ns[i].ready = f & 2; ns[i].lost = f & 4; ns[i].host = f & 8;
                ns[i].name = r.Str(); ns[i].ping = r.U16();
            }
            int ps[MAX_PLAYERS]; for (int& c : ps) c = (int)r.U8() - 1;
            if (r.bad) break;
            game = g; paused = pz; pauseLeft = pl;
            for (int i = 0; i < MAX_PLAYERS; i++) seats[i] = ns[i];
            for (int k = 0; k < MAX_PLAYERS; k++) playerSeat[k] = ps[k];
            if (st == S_LOBBY || st == S_PLAYING) {
                if (st == S_LOBBY && stage == S_PLAYING) snapshot.clear();
                stage = (Stage)st;
            }
        } break;
        case M_CHAT: { int seat = r.U8(); std::string t = r.Str(); if (!r.bad && seat < MAX_PLAYERS) Log((seats[seat].used ? seats[seat].name : std::string("?")) + ": " + t); } break;
        case M_LAUNCH: { int g = r.U8(); stage = S_PLAYING; lastSeq = 0; partSeq = 0; parts.clear(); partsHave = 0;Log("The game begins: " + std::string(Info(g).name) + "."); } break;
        case M_STATE: {
            uint32_t seq = r.U32();
            if (r.bad || (seq <= lastSeq && lastSeq - seq < 0x80000000u)) break;   // a late unreliable snapshot: the newer one already arrived
            lastSeq = seq;
            snapshot.assign(r.p + r.i, r.p + r.n);
            stateVersion++; snapshotsReceived++;
        } break;
        case M_STATE_PART: {
            uint32_t seq = r.U32(); int k = (int)r.U8(), n = (int)r.U8();
            if (r.bad || n < 2 || k >= n || (seq <= lastSeq && lastSeq - seq < 0x80000000u)) break;
            if (seq != partSeq) { partSeq = seq; parts.assign(n, {}); partsHave = 0; }   // (a newer snapshot: the old one's parts are dropped)
            if ((int)parts.size() != n || !parts[k].empty()) break;
            parts[k].assign(r.p + r.i, r.p + r.n);
            if (parts[k].empty()) break;
            if (++partsHave < n) break;
            lastSeq = seq;
            snapshot.clear();
            for (auto& p : parts) snapshot.insert(snapshot.end(), p.begin(), p.end());
            parts.clear(); partsHave = 0;
            stateVersion++; snapshotsReceived++;
        } break;
        case M_PING: { uint32_t t = r.U32(); Writer w; w.U8(M_PONG); w.U32(t); SendTo(server, w); } break;
        case M_BYE: End(r.Str()); break;
        default: break;
    }
}

// ---- every frame
void Session::Update(double t, float dt) {
    now = t;
    if (!tr) return;
    if (heardHost < 0) heardHost = now;
    std::vector<net::Event> evs;
    tr->Poll(evs);
    for (auto& e : evs) {
        if (!tr) break;   // (a message ended the session)
        if (role == R_HOST) {
            if (e.kind == net::Event::Connected) pending.push_back({e.conn, now});
            else if (e.kind == net::Event::Disconnected) {
                pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending& p) { return p.conn == e.conn; }), pending.end());
                int s = SeatOfConn(e.conn); if (s >= 0) HostLost(s, e.info.c_str());
            } else { Reader r(e.data); HostMessage(e.conn, r); }
        } else if (role == R_CLIENT) {
            if (e.kind == net::Event::Connected && e.conn == server) {
                Writer w; w.U8(M_HELLO); w.U8(PROTOCOL); w.U32(BuildId()); w.U32(DataHash()); w.Str(me.name); w.U64(me.id); w.U32(rejoinToken);
                SendTo(server, w);
                heardHost = now; status = "Saying hello...";
            } else if (e.kind == net::Event::Disconnected) {
                End(stage == S_CONNECTING ? "Couldn't reach that table" + (e.info.empty() ? std::string(".") : ": " + e.info) : "Lost the connection to the host.");
            } else { Reader r(e.data); ClientMessage(r); }
        }
    }
    if (!tr) return;
    if (role == R_HOST) {
        // hellos that never came, heartbeats, the lost
        for (auto& p : pending) if (now - p.since > 10) tr->Close(p.conn, "no hello");
        pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const Pending& p) { return now - p.since > 10; }), pending.end());
        if (now - lastPing >= PING_EVERY) {
            lastPing = now;
            Writer w; w.U8(M_PING); w.U32((uint32_t)(now * 1000)); Broadcast(w);
            if (paused) SendLobby();   // (the countdown on everyone's screen)
        }
        for (int i = 0; i < MAX_PLAYERS; i++) {
            SeatInfo& s = seats[i];
            if (s.used && s.conn >= 0 && !s.lost && now - s.heard > LOST_AFTER) { tr->Close(s.conn, "timed out"); HostLost(i, "timed out"); }
        }
        if (beaconOn && now - lastBeacon >= BEACON_EVERY) {
            lastBeacon = now;
            net::LanGame g; g.game = Info(game).name; g.name = hostName; g.code = code;
            for (auto& s : seats) g.players += s.used;
            g.maxPlayers = Info(game).maxPlayers; g.inProgress = stage == S_PLAYING; g.build = BuildId();
            beacon.Announce(g);
        }
        if (stage == S_PLAYING) HostTick(dt);
    } else if (role == R_CLIENT) {
        double limit = stage == S_CONNECTING ? 10.0 : LOST_AFTER;
        if (now - heardHost > limit) End(stage == S_CONNECTING ? "Couldn't reach that table (no answer)." : "The host stopped answering.");
    }
}
}
