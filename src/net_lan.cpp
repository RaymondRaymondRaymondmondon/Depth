// LAN discovery for the Deep Arcade: a UDP beacon on port 47777 and a browser that lists what it hears.
// Plain Winsock, kept in its own file because windows.h and raylib.h can't share a translation unit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "net.h"
#include "net_msg.h"
#include <algorithm>
#pragma comment(lib, "ws2_32.lib")

namespace net {

static bool WsaUp() {
    static bool up = false;
    if (!up) { WSADATA d; up = WSAStartup(MAKEWORD(2, 2), &d) == 0; }
    return up;
}
static const char MAGIC[4] = {'D', 'P', 'T', 'H'};
constexpr uint8_t BEACON_VERSION = 1;

std::vector<std::string> LocalIPv4() {
    std::vector<std::string> out;
    if (!WsaUp()) return out;
    char host[256] = {};
    if (gethostname(host, sizeof host) != 0) return out;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* p = res; p; p = p->ai_next) {
        char buf[64];
        inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, buf, sizeof buf);
        if (std::find(out.begin(), out.end(), buf) == out.end()) out.push_back(buf);
    }
    freeaddrinfo(res);
    return out;
}

LanBeacon::~LanBeacon() { Stop(); }
bool LanBeacon::Start() {
    if (sock != -1) return true;
    if (!WsaUp()) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
    sock = (intptr_t)s;
    return true;
}
void LanBeacon::Announce(const LanGame& g) {
    if (sock == -1) return;
    Writer w;
    w.Bytes(MAGIC, 4); w.U8(BEACON_VERSION); w.U16(g.port); w.Str(g.game); w.Str(g.name); w.Str(g.code);
    w.U8(g.players); w.U8(g.maxPlayers); w.U8(g.inProgress); w.U32(g.build);
    // everywhere a neighbour could be listening: the whole-network broadcast, each adapter's /24 broadcast
    // (machines with VPN or VM adapters otherwise miss each other), and this machine itself
    std::vector<std::string> targets = {"255.255.255.255", "127.0.0.1"};
    for (const std::string& ip : LocalIPv4()) { size_t d = ip.rfind('.'); if (d != std::string::npos) targets.push_back(ip.substr(0, d) + ".255"); }
    for (const std::string& t : targets) {
        sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(LAN_PORT);
        inet_pton(AF_INET, t.c_str(), &to.sin_addr);
        sendto((SOCKET)sock, (const char*)w.b.data(), (int)w.b.size(), 0, (sockaddr*)&to, sizeof to);
    }
}
void LanBeacon::Stop() { if (sock != -1) { closesocket((SOCKET)sock); sock = -1; } }

LanBrowser::~LanBrowser() { Stop(); }
bool LanBrowser::Start() {
    if (sock != -1) return true;
    if (!WsaUp()) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);   // several copies on one PC can all browse
    sockaddr_in at{}; at.sin_family = AF_INET; at.sin_port = htons(LAN_PORT); at.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr*)&at, sizeof at) != 0) { closesocket(s); return false; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sock = (intptr_t)s;
    return true;
}
void LanBrowser::Poll(double now) {
    if (sock != -1) {
        uint8_t buf[1024];
        for (int guard = 0; guard < 64; guard++) {
            sockaddr_in from{}; int fl = sizeof from;
            int n = recvfrom((SOCKET)sock, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (n <= 0) break;
            Reader r(buf, n);
            if (n < 5 || memcmp(buf, MAGIC, 4) != 0) continue;
            r.i = 4;
            if (r.U8() != BEACON_VERSION) continue;
            LanGame g;
            char ip[64]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            g.addr = ip; g.port = (uint16_t)r.U16(); g.game = r.Str(); g.name = r.Str(); g.code = r.Str();
            g.players = r.U8(); g.maxPlayers = r.U8(); g.inProgress = r.U8() != 0; g.build = r.U32(); g.seen = now;
            if (r.bad) continue;
            // one lobby heard on several adapters: keep a single entry (prefer the real address to loopback)
            auto it = std::find_if(games.begin(), games.end(), [&](const LanGame& o) { return o.code == g.code; });
            if (it == games.end()) games.push_back(g);
            else { std::string keep = it->addr == "127.0.0.1" ? g.addr : it->addr; *it = g; it->addr = keep; }
        }
    }
    games.erase(std::remove_if(games.begin(), games.end(), [&](const LanGame& o) { return now - o.seen > 4.0; }), games.end());
}
void LanBrowser::Stop() { if (sock != -1) { closesocket((SOCKET)sock); sock = -1; } }
}
