// The Deep Arcade transport on Valve's GameNetworkingSockets (the open-source twin of Steam's networking; see
// docs/design/5_Depth_Arcade_Networking.md). Built only when CMake found external/gns (DEPTH_HAVE_GNS); otherwise
// the stub at the bottom reports that networking isn't available and the arcade says so.
#include "net.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#ifdef DEPTH_HAVE_GNS
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

namespace net {

namespace {
bool gInit = false;
class GnsTransport;
std::vector<GnsTransport*> gAll;   // every live transport: the status callback is global
void OnStatus(SteamNetConnectionStatusChangedCallback_t* info);
}

bool Available() { return true; }
bool Init(std::string* err) {
    if (gInit) return true;
    SteamDatagramErrMsg msg;
    if (!GameNetworkingSockets_Init(nullptr, msg)) { if (err) *err = msg; return false; }
    SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning,
        [](ESteamNetworkingSocketsDebugOutputType, const char* m) { fprintf(stderr, "[net] %s\n", m); });
    gInit = true;
    return true;
}
void Shutdown() { if (gInit) { GameNetworkingSockets_Kill(); gInit = false; } }
void SetFakeLag(int ms, float lossPct) {
    auto* u = SteamNetworkingUtils();
    u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, ms);
    u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, lossPct);
    u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Recv, lossPct);
}

namespace {
class GnsTransport : public Transport {
public:
    ISteamNetworkingSockets* s = SteamNetworkingSockets();
    HSteamListenSocket listen = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
    std::set<HSteamNetConnection> conns;
    std::vector<Event> pending;
    // the session speaks small positive ids; GNS handles are 32-bit and would turn negative as an int
    std::map<HSteamNetConnection, int> idOf;
    std::map<int, HSteamNetConnection> hOf;
    int nextId = 1;
    int Id(HSteamNetConnection h) { auto it = idOf.find(h); if (it != idOf.end()) return it->second; int id = nextId++; idOf[h] = id; hOf[id] = h; return id; }
    HSteamNetConnection H(int id) { auto it = hOf.find(id); return it == hOf.end() ? k_HSteamNetConnection_Invalid : it->second; }
    void Forget(HSteamNetConnection h) { auto it = idOf.find(h); if (it != idOf.end()) { hOf.erase(it->second); idOf.erase(it); } }

    GnsTransport() { group = s->CreatePollGroup(); gAll.push_back(this); }
    ~GnsTransport() override { CloseAll(); s->DestroyPollGroup(group); gAll.erase(std::remove(gAll.begin(), gAll.end(), this), gAll.end()); }

    SteamNetworkingConfigValue_t Callback() { SteamNetworkingConfigValue_t o; o.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnStatus); return o; }
    bool Listen(uint16_t port, std::string* err) override {
        SteamNetworkingIPAddr a; a.Clear(); a.m_port = port;
        SteamNetworkingConfigValue_t o = Callback();
        listen = s->CreateListenSocketIP(a, 1, &o);
        if (listen == k_HSteamListenSocket_Invalid) { if (err) *err = "couldn't open UDP port " + std::to_string(port) + " (is another host running?)"; return false; }
        return true;
    }
    int Connect(const std::string& addr, std::string* err) override {
        SteamNetworkingIPAddr a; a.Clear();
        std::string text = addr.find(':') == std::string::npos ? addr + ":" + std::to_string(GAME_PORT) : addr;
        if (!a.ParseString(text.c_str())) { if (err) *err = "not an address: " + addr; return -1; }
        SteamNetworkingConfigValue_t o = Callback();
        HSteamNetConnection c = s->ConnectByIPAddress(a, 1, &o);
        if (c == k_HSteamNetConnection_Invalid) { if (err) *err = "couldn't start connecting"; return -1; }
        conns.insert(c); s->SetConnectionPollGroup(c, group);
        return Id(c);
    }
    void Send(int conn, Channel ch, const void* data, int n) override {
        // the channel rides in the first byte; reliable messages are ordered per connection
        std::vector<uint8_t> buf((size_t)n + 1);
        buf[0] = ch; if (n) memcpy(buf.data() + 1, data, n);
        int flags = ch == CH_STATE ? k_nSteamNetworkingSend_Unreliable : k_nSteamNetworkingSend_Reliable;
        HSteamNetConnection h = H(conn);
        if (h == k_HSteamNetConnection_Invalid) return;
        s->SendMessageToConnection(h, buf.data(), (uint32)buf.size(), flags, nullptr);
    }
    void Poll(std::vector<Event>& out) override {
        s->RunCallbacks();
        for (auto& e : pending) out.push_back(std::move(e));
        pending.clear();
        SteamNetworkingMessage_t* msgs[64];
        for (int guard = 0; guard < 16; guard++) {
            int k = s->ReceiveMessagesOnPollGroup(group, msgs, 64);
            if (k <= 0) break;
            for (int i = 0; i < k; i++) {
                SteamNetworkingMessage_t* m = msgs[i];
                if (m->m_cbSize >= 1) {
                    Event e; e.kind = Event::Message; e.conn = Id(m->m_conn);
                    const uint8_t* p = (const uint8_t*)m->m_pData;
                    e.ch = (Channel)p[0]; e.data.assign(p + 1, p + m->m_cbSize);
                    out.push_back(std::move(e));
                }
                m->Release();
            }
        }
    }
    void Close(int conn, const char* why) override {
        HSteamNetConnection h = H(conn);
        if (!conns.count(h)) return;
        s->CloseConnection(h, 0, why, true);   // linger: queued reliable messages still go
        conns.erase(h); Forget(h);
    }
    void CloseAll() override {
        for (auto c : conns) s->CloseConnection(c, 0, "closing", true);
        conns.clear(); idOf.clear(); hOf.clear();
        if (listen != k_HSteamListenSocket_Invalid) { s->CloseListenSocket(listen); listen = k_HSteamListenSocket_Invalid; }
    }
    Stats GetStats(int conn) override {
        Stats st;
        SteamNetConnectionRealTimeStatus_t r;
        if (s->GetConnectionRealTimeStatus(H(conn), &r, 0, nullptr) == k_EResultOK) {
            st.pingMs = r.m_nPing; st.quality = r.m_flConnectionQualityLocal; st.outKBps = r.m_flOutBytesPerSec / 1024; st.inKBps = r.m_flInBytesPerSec / 1024;
        }
        return st;
    }
    void Status(SteamNetConnectionStatusChangedCallback_t* info) {
        HSteamNetConnection c = info->m_hConn;
        bool mine = conns.count(c) || (listen != k_HSteamListenSocket_Invalid && info->m_info.m_hListenSocket == listen);
        if (!mine) return;
        switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting:
                if (info->m_info.m_hListenSocket == listen && !conns.count(c)) {   // a joiner knocking: let them in (the handshake decides)
                    if (s->AcceptConnection(c) == k_EResultOK) { conns.insert(c); s->SetConnectionPollGroup(c, group); }
                    else s->CloseConnection(c, 0, "accept failed", false);
                }
                break;
            case k_ESteamNetworkingConnectionState_Connected: {
                Event e; e.kind = Event::Connected; e.conn = Id(c);
                char buf[SteamNetworkingIPAddr::k_cchMaxString]; info->m_info.m_addrRemote.ToString(buf, sizeof buf, true); e.info = buf;
                pending.push_back(e);
            } break;
            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
                Event e; e.kind = Event::Disconnected; e.conn = Id(c); e.info = info->m_info.m_szEndDebug;
                pending.push_back(e);
                s->CloseConnection(c, 0, nullptr, false);
                conns.erase(c); Forget(c);
            } break;
            default: break;
        }
    }
};
void OnStatus(SteamNetConnectionStatusChangedCallback_t* info) { for (GnsTransport* t : gAll) t->Status(info); }
}

std::unique_ptr<Transport> MakeTransport() { return gInit ? std::make_unique<GnsTransport>() : nullptr; }
}

#else   // ---- no network library in this build

namespace net {
bool Available() { return false; }
bool Init(std::string* err) { if (err) *err = "this build of Depth has no network library (run tools/build_gns.ps1, then build again)"; return false; }
void Shutdown() {}
void SetFakeLag(int, float) {}
std::unique_ptr<Transport> MakeTransport() { return nullptr; }
}
#endif
