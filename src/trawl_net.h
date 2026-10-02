#pragma once
// The Trawl over the Deep Arcade's network session (stage 5; docs/design/5_Depth_Arcade_Networking.md, the Trawl
// design doc's "Networking"). Host-authoritative: the host runs the one real Gannet (a TrawlHost, the arcade's
// GameHost for G_TRAWL) and snapshots the whole world 20 times a second; every client mirrors the snapshot into its
// own TrawlWorld, which the same top-down and first-person renderers draw. A hand is played by sending its input
// (HandInput: the held keys, the presses since the last send, the aim on the deck) and the dock's buttons as
// commands. Solo play goes through the very same ApplyInput / DoCommand, so solo and network can't drift apart.
// Headless (no drawing); trawl.cpp is the scene for all three (solo, host, client).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <memory>
#include <string>

namespace arcade { class Session; }

namespace tw {

// Everything one run of the Trawl is: the boat and crew, the ground under her, the deadlines. A network mirror is
// one of these that never steps.
struct TrawlWorld {
    Gannet G;
    Eco eco;
    Session sess;
    void Rebind() { sess.G = &G; sess.E = &eco; if (G.eco) G.eco = &eco; }
};

// ---------------------------------------------------------------- a hand's input
enum : uint16_t {
    HI_LMB = 1, HI_LMB_P = 2, HI_RMB = 4, HI_RMB_P = 8, HI_SPACE_P = 16, HI_SHIFT = 32, HI_E_P = 64, HI_X_P = 128,
    HI_R_P = 256, HI_T_P = 512, HI_W_P = 1024, HI_S_P = 2048, HI_ORDER = 4096, HI_FOLLOW_P = 8192,
    HI_PRESSES = HI_LMB_P | HI_RMB_P | HI_SPACE_P | HI_E_P | HI_X_P | HI_R_P | HI_T_P | HI_W_P | HI_S_P | HI_ORDER | HI_FOLLOW_P,
};
struct HandInput {
    Vector2 wish{0, 0};          // where the hand walks (deck frame, length <= 1)
    Vector2 aim{0, 0};           // where it aims (deck frame: the mouse top-down, the crosshair's ray in first person)
    float steer = 0;             // A/D at the helm
    float wheel = 0;             // the mouse wheel since the last step (drag, depth, telegraph, lantern)
    uint16_t btn = 0;
    int8_t sel = -1;             // a slot picked (1-4 keys), -1 none
    int8_t order = -1;           // with HI_ORDER: the station a bot is sent to (-1: every bot back to its watch)
};
void WriteInput(const HandInput& in, Writer& w);
bool ReadInput(Reader& r, HandInput& in);
void MergeInput(HandInput& into, const HandInput& next);   // the newest held state; presses, the wheel and picks add up until used
void ClearPresses(HandInput& in);                          // after one step has used them
void ApplyInput(TrawlWorld& w, int c, const HandInput& in, float dt);   // one 60 Hz step of hand c (the presses act once)

// ---------------------------------------------------------------- the dock's buttons (and the deck locker)
enum Cmd : uint8_t { CMD_BUY, CMD_SELL, CMD_SLIP, CMD_CASTOFF, CMD_COUNT, CMD_CONTINUE, CMD_LOCKER_TAKE, CMD_LOCKER_STOW, CMD_CANOE, CMD_DELIVER, CMD_GUN_BUY, CMD_GUN_UPGRADE, CMD_GUN_ATTACH, CMD_AMMO, CMD_ELDER_GIVE, CMD_ELDER_BUY, CMD_REQUEST, CMD_WEAR_DROP, CMD_GROUND, CMD_ROLE_UP, CMD_CONSIGN_REWARD, CMD_WARDROBE, CMD_N };   // (WARDROBE: id "<skin>|<costume>")   // (ROLE_UP: arg rank * 10 + choice; CONSIGN_REWARD: arg 0 an upgrade, 1 10% off)   // (SELL and DELIVER: arg is a hold index, -1 every fish)
bool DoCommand(TrawlWorld& w, int c, int cmd, const std::string& id, int arg, std::string* why);

// the messages a client sends (Session::Act): an input frame, or a command
enum : uint8_t { ACT_INPUT = 0, ACT_CMD = 1 };
void WriteInputAction(const HandInput& in, Writer& w);
void WriteCmdAction(int cmd, const std::string& id, int arg, Writer& w);

// ---------------------------------------------------------------- the snapshot
void WriteWorld(const TrawlWorld& w, Writer& out);
bool ReadWorld(Reader& r, TrawlWorld& w);                  // into a mirror (rebuilds the chart when the ground changes)

// the host's game (arcade_games.cpp's MakeGameHost for G_TRAWL)
std::unique_ptr<arcade::GameHost> MakeTrawlHost();
TrawlWorld* TrawlHostWorld(arcade::GameHost* h);           // the host's real world (the host's own screen draws it), or null
uint32_t TrawlDataHash();                                  // the rules a peer must share (into arcade::DataHash)

int RunTrawlNetTest();                                     // depth.exe --trawl-net-test (inputs, commands, the snapshot round trip)
int RunTrawlNetLoop(bool forceMemory);                     // depth.exe --net-loop trawl [mem] (a host and five guests over loopback)

} // namespace tw
