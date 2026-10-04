#pragma once
// The one interface every Deep Arcade game implements for the network session (docs/design/5_Depth_Arcade_Networking.md).
// The session (arcade_session.*) owns everything shared: seats, AI seats, the lobby, the handshake, heartbeats, the
// pause, rejoining and sending. A game only knows its rules: it starts, takes a player's action, ticks, and writes
// what one player may see. Turn-based games send a snapshot whenever something changed (reliable); real-time games
// are snapshotted at a fixed rate on the unreliable channel and clients keep only the newest.
// No raylib here: games are headless so the host, the sims and --net-loop all run them the same way.
#include "net_msg.h"
#include <cstdint>
#include <memory>

namespace arcade {

// the reels of the cabinet, in order (the beacon carries the name, not the number)
enum GameId : uint8_t { G_FLATS_DUEL, G_TRAWL, G_SCUTTLE, G_FATHOMS, G_RED_TIDE, G_FLIGHT, G_MOUTHFUL, G_NIGHT_OFF, G_COUNT, G_TEST_DRIFT = 250 };

struct GameInfo {
    const char* name;
    int minPlayers, maxPlayers;   // AI seats count as players
    bool realtime;                // snapshots at snapshotHz on the unreliable channel
    float snapshotHz;
    bool built;                   // has its GameHost been written yet?
    bool pauseOnLost = true;      // does the table wait while a player is lost? (The Flight runs on: a dropped colony keeps its last orders)
};
const GameInfo& Info(int game);

class GameHost {
public:
    virtual ~GameHost() = default;
    virtual void Configure(const std::string& opts) { (void)opts; }   // the host's options, before Start (Red Tide: the map)
    virtual void Start(int players, uint32_t seed) = 0;
    virtual bool Act(int player, Reader& r) = 0;               // an action from `player` (already checked to be theirs): true if it changed the game
    virtual bool Tick(float dt, uint32_t aiPlayers) = 0;       // timers and the AI players (a bit per player): true if it changed the game
    virtual void Snapshot(int viewer, Writer& w) const = 0;    // what `viewer` may see (-1: everything)
    virtual bool Over() const = 0;
};
std::unique_ptr<GameHost> MakeGameHost(int game);   // nullptr for a game that hasn't come aboard yet
}
