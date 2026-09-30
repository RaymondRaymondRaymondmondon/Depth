#pragma once
// The Deep Arcade's transport (docs/design/5_Depth_Arcade_Networking.md): the only interface that knows a network
// library sits underneath. net_gns.cpp implements it with Valve's GameNetworkingSockets (built into Depth when
// external/gns exists; see tools/build_gns.ps1); a Steam backend can replace it later behind the same interface.
// net_lan.cpp does the LAN discovery beacon with plain sockets. No raylib or Windows headers here.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace net {

enum Channel : uint8_t { CH_CONTROL = 0, CH_STATE = 1, CH_BULK = 2 };   // reliable ordered / unreliable / reliable large
constexpr uint16_t GAME_PORT = 47778, LAN_PORT = 47777;

struct Event {
    enum Kind { Connected, Disconnected, Message } kind = Message;
    int conn = -1;
    Channel ch = CH_CONTROL;
    std::vector<uint8_t> data;
    std::string info;             // why a connection closed; the peer's address when it connects
};
struct Stats { int pingMs = 0; float quality = 1; float outKBps = 0, inKBps = 0; };

bool Available();                 // was Depth built with a network library?
bool Init(std::string* err);      // once, before any transport
void Shutdown();
void SetFakeLag(int ms, float lossPct);   // for --net-loop ... lag

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool Listen(uint16_t port, std::string* err) = 0;        // host
    virtual int Connect(const std::string& addr, std::string* err) = 0; // client: "ip" or "ip:port"; returns the connection
    virtual void Send(int conn, Channel ch, const void* data, int n) = 0;
    virtual void Poll(std::vector<Event>& out) = 0;                  // call every frame
    virtual void Close(int conn, const char* why) = 0;
    virtual void CloseAll() = 0;
    virtual Stats GetStats(int conn) = 0;
};
std::unique_ptr<Transport> MakeTransport();
// an in-process transport (no sockets): hosts and clients in one process find each other by port. For --net-loop
// when this build has no network library, and for tests of the session logic. Addresses are "mem:<port>".
std::unique_ptr<Transport> MakeMemoryTransport();
void MemoryDrop(int conn);   // tests: cut a memory connection as if the cable were pulled

// ---- LAN discovery: the host announces its lobby on UDP 47777 once a second; Browse lists what it hears
struct LanGame {
    std::string addr;             // where the beacon came from
    uint16_t port = GAME_PORT;
    std::string game, name, code;
    int players = 0, maxPlayers = 0;
    bool inProgress = false;
    uint32_t build = 0;
    double seen = 0;
};
class LanBeacon {
public:
    ~LanBeacon();
    bool Start();
    void Announce(const LanGame& g);   // sends one beacon (call about once a second)
    void Stop();
private:
    intptr_t sock = -1;
};
class LanBrowser {
public:
    ~LanBrowser();
    bool Start();
    void Poll(double now);             // drains beacons; forgets lobbies unheard for 4 s
    void Stop();
    std::vector<LanGame> games;
private:
    intptr_t sock = -1;
};
std::vector<std::string> LocalIPv4();   // this machine's addresses (shown when hosting)
}
