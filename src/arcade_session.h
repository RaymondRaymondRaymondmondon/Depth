#pragma once
// The Deep Arcade's multiplayer session (docs/design/5_Depth_Arcade_Networking.md, step N1): one host, up to five
// guests, host-authoritative. The lobby (seats, ready, AI seats, chat), the handshake (protocol, build and data hash),
// heartbeats (a peer unheard for 6 s is lost), a pause while a player is lost, an AI takeover after 2 minutes, and
// rejoining with a token. The game played is Scuttle (scuttle.h); the host runs the engine and sends each seat what it
// may see. No raylib here: the arcade screen (arcade.cpp) draws it and --net-loop (net_test.cpp) drives it headless.
#include "net.h"
#include "scuttle.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arcade {

enum GameId : uint8_t { G_SCUTTLE = 0, G_COUNT };
const char* GameName(int g);
int GameMaxPlayers(int g);

constexpr int MAX_PLAYERS = 6;
constexpr uint8_t PROTOCOL = 1;
constexpr double LOST_AFTER = 6.0, TAKEOVER_AFTER = 120.0, PING_EVERY = 1.0, BEACON_EVERY = 1.0;
constexpr float AI_THINK = 0.9f, ROUND_PAUSE = 4.0f;

struct Profile { std::string name = "Diver"; uint64_t id = 0; };
uint32_t BuildId();     // this executable's build (DEPTH_BUILD_STAMP)
uint32_t DataHash();    // the rules every peer must agree on (Scuttle's deck and timers)
std::string MakeCode(uint32_t seed);   // a 6-character join code (no 0/O/1/I)

struct SeatInfo {
    bool used = false, ai = false, ready = false, lost = false, host = false;
    std::string name;
    uint64_t id = 0;
    int conn = -1;          // host side: the seat's connection (-1 for the host itself and AI seats)
    uint32_t token = 0;     // host side: what a dropped player shows to get this seat back
    double lostAt = 0, heard = 0;
    int ping = 0;
};

enum Role : uint8_t { R_NONE, R_HOST, R_CLIENT };
enum Stage : uint8_t { S_IDLE, S_CONNECTING, S_LOBBY, S_PLAYING, S_ENDED };

class Session {
public:
    ~Session();
    // start: host listens on `port` and announces on the LAN; a client connects to "ip[:port]" (or "mem:port")
    bool Host(const Profile& me, int game, std::string* err, uint16_t port = net::GAME_PORT, std::unique_ptr<net::Transport> t = nullptr, bool beacon = true);
    bool Join(const Profile& me, const std::string& addr, std::string* err, uint32_t rejoinToken = 0, std::unique_ptr<net::Transport> t = nullptr);
    void Leave();
    void Update(double now, float dt);   // every frame

    // the lobby
    void SetReady(bool r);
    void AddAI();                  // host
    void RemoveSeat(int seat);     // host: an AI seat, or kicks a player
    void Chat(const std::string& text);
    bool CanLaunch(std::string* why) const;
    bool Launch(std::string* why); // host: everyone ready, two or more seats
    void BackToLobby();            // host: after a match (guests must ready up again)

    // Scuttle
    void Act(const scuttle::Action& a);   // a client sends it; the host applies it at once
    int MyCrab() const;                   // my seat in the Scuttle engine (-1: watching)
    int CrabOfSeat(int lobbySeat) const;
    int SeatOfCrab(int crab) const;

    // what the screen shows
    Role role = R_NONE;
    Stage stage = S_IDLE;
    int game = G_SCUTTLE;
    int mySeat = -1;
    std::string code, hostName, status, endReason;
    SeatInfo seats[MAX_PLAYERS];
    std::vector<std::string> chat;
    scuttle::State view;                  // the host's own view (all), or what the host last sent us
    bool paused = false;                  // someone is lost (the host waits up to TAKEOVER_AFTER)
    float pauseLeft = 0;
    uint32_t rejoinToken = 0;             // client: keep it to get this seat back
    std::string hostAddr;                 // client: where we joined (for rejoin)
    int stateVersion = 0;                 // bumps whenever a new view arrives (the screen animates on it)
    int pingMs = 0;

    // tests
    net::Transport* transport() { return tr.get(); }
    int HostConnOfSeat(int seat) const { return seat >= 0 && seat < MAX_PLAYERS ? seats[seat].conn : -1; }
    int ServerConn() const { return server; }
    scuttle::State& Authoritative() { return truth; }
    int leaks = 0;                        // tests: states that carried another seat's hand or face-down bets (must stay 0)
    int rejects = 0;

private:
    std::unique_ptr<net::Transport> tr;
    net::LanBeacon beacon;
    bool beaconOn = false;
    Profile me;
    int server = -1;                      // client: the connection to the host
    double now = 0, lastPing = 0, lastBeacon = 0, heardHost = 0;
    struct Pending { int conn; double since; };
    std::vector<Pending> pending;         // host: connected, no HELLO yet
    scuttle::State truth;                 // host: the real game
    int crabSeat[scuttle::MAX_SEATS] = {-1, -1, -1, -1};   // host: Scuttle seat -> lobby seat
    float aiWait = 0, roundWait = 0;
    uint32_t rng = 1;

    void SendTo(int conn, const Writer& w);
    void Broadcast(const Writer& w);
    void SendLobby();
    void SendState();
    void SendChat(int seat, const std::string& text);
    void HostMessage(int conn, Reader& r);
    void ClientMessage(Reader& r);
    void HostHello(int conn, Reader& r);
    int SeatOfConn(int conn) const;
    void HostLost(int seat, const char* why);
    void HostTick(float dt);
    void End(const std::string& why);
    void Log(const std::string& s);
};
}
