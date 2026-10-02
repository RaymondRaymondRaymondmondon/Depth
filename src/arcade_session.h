#pragma once
// The Deep Arcade's multiplayer session (docs/design/5_Depth_Arcade_Networking.md): one host, up to five guests,
// host-authoritative, for every arcade game. The lobby (seats, ready, AI seats, chat), the handshake (protocol, build
// and data hash), heartbeats (a peer unheard for 6 s is lost), a pause while a player is lost, an AI takeover after
// 2 minutes, and rejoining with a token. The game itself is a GameHost (arcade_game.h): turn-based games are sent to
// each player whenever they change, real-time ones at their snapshot rate on the unreliable channel. Each player only
// ever receives the snapshot written for them. No raylib here: arcade.cpp draws it, --net-loop drives it headless.
#include "arcade_game.h"
#include "net.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arcade {

constexpr int MAX_PLAYERS = 6;
constexpr uint8_t PROTOCOL = 3;   // (3: big real-time snapshots go in parts)
constexpr double LOST_AFTER = 6.0, TAKEOVER_AFTER = 120.0, PING_EVERY = 1.0, BEACON_EVERY = 1.0;

struct Profile { std::string name = "Diver"; uint64_t id = 0; };
uint32_t BuildId();     // this executable's build (DEPTH_BUILD_STAMP)
uint32_t DataHash();    // the rules every peer must agree on (arcade_games.cpp)
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
    void RemoveSeat(int seat);     // host: an AI seat, or gives a player's seat away
    void Chat(const std::string& text);
    bool CanLaunch(std::string* why) const;
    bool Launch(std::string* why); // host: everyone ready, enough seats, the game is aboard
    void BackToLobby();            // host: after a match (guests must ready up again)
    bool Rematch(std::string* why);// host: after a match, the same seats straight into a new one

    // the game
    void Act(const Writer& action);          // my action, in the game's own format (a client sends it; the host applies it)
    int MyPlayer() const { return PlayerOfSeat(mySeat); }   // my index in the game (-1: watching)
    int PlayerOfSeat(int lobbySeat) const;
    int SeatOfPlayer(int player) const;
    const std::vector<uint8_t>& Snapshot() const { return snapshot; }   // what I may see of the game (the game decodes it)

    // what the screen shows
    Role role = R_NONE;
    Stage stage = S_IDLE;
    int game = G_SCUTTLE;
    int mySeat = -1;
    std::string code, hostName, status, endReason;
    SeatInfo seats[MAX_PLAYERS];
    std::vector<std::string> chat;
    bool paused = false;                  // someone is lost (the host waits up to TAKEOVER_AFTER)
    float pauseLeft = 0;
    uint32_t rejoinToken = 0;             // client: keep it to get this seat back
    std::string hostAddr;                 // client: where we joined (for rejoin)
    std::string gameOpts;                 // host: the game's options for the next launch (Red Tide: the map key)
    int stateVersion = 0;                 // bumps whenever a new snapshot arrives (the screen decodes and animates on it)
    int rejects = 0;
    int snapshotsReceived = 0;            // tests

    // tests
    GameHost* HostGame() { return truth.get(); }
    int ServerConn() const { return server; }

private:
    std::unique_ptr<net::Transport> tr;
    net::LanBeacon beacon;
    bool beaconOn = false;
    Profile me;
    int server = -1;                      // client: the connection to the host
    double now = 0, lastPing = 0, lastBeacon = 0, heardHost = 0, lastSnap = 0;
    struct Pending { int conn; double since; };
    std::vector<Pending> pending;         // host: connected, no HELLO yet
    std::unique_ptr<GameHost> truth;      // host: the real game
    int playerSeat[MAX_PLAYERS] = {-1, -1, -1, -1, -1, -1};   // game player -> lobby seat
    uint32_t rng = 1, snapSeq = 0, lastSeq = 0;
    uint32_t partSeq = 0; int partsHave = 0; std::vector<std::vector<uint8_t>> parts;   // client: a big snapshot arriving in parts
    std::vector<uint8_t> snapshot;

    void SendTo(int conn, const Writer& w, net::Channel ch = net::CH_CONTROL);
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
