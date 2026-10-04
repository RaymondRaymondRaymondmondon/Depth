#pragma once
// A Night Off over the Deep Arcade's session (doc p. 26: host-authoritative through the shared layer; conversations,
// games, bets and fights resolved on the host; a few dozen agents). The host runs the night and snapshots it 20 times
// a second; each guest keeps a mirror Night drawn by the same scene. Every person's play, solo included, is an Input
// (WriteInput/ReadInput). The snapshot is one templated Visit (write and read): any field a screen draws must be in it.
// A dropped player's seat is played by the bot (Night::BotPlayer) until they rejoin.
#include "nightoff.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <memory>
#include <string>

namespace no {
enum NoAct : uint8_t { NA_INPUT = 1, NA_HELLO = 2 };
void WriteInput(const Input& in, Writer& w);
bool ReadInput(Reader& r, Input& in);                 // (after the NA_INPUT byte)
void OrderHello(Writer& w, const std::string& name, int crew, const std::string& profile = "");   // (profile: ProfileSummary, applied by the host)
void WriteNight(Night& n, int viewer, Writer& out);
void PackNight(Night& n, int viewer, Writer& out);     // compressed
bool ReadNight(Reader& r, Night& n, int* viewerOut = nullptr);
void ReplayShot(GameSeat& g);                          // a guest rebuilds the last pool or golf shot's frames from the shot
std::unique_ptr<arcade::GameHost> MakeNightHost();
Night* NightHostWorld(arcade::GameHost* h);
std::string NightHostOpts(int mode, int crowd, bool pvp, float startMinutes = 0);
uint32_t NightDataHash();
int RunNightNetTest();                                 // --night-net-test
int RunNightNetLoop(bool mem);                         // --net-loop night [mem]
}
