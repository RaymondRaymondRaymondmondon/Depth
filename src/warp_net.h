#pragma once
// Warp Dodgeball over the arcade session: host-authoritative. The host steps the real World at 120 Hz and snapshots it
// at 30 Hz; guests mirror it and draw it with the same scene. All play, solo or networked, is a wd::Input.
// The snapshot is one templated Visit (write and read): any new World/Player/Ball field a screen draws must be added there;
// --warp-net-test checks the mirror rewrites byte-identical.
#include "arcade_game.h"
#include "warp.h"
#include <memory>
#include <string>

namespace wd {
std::unique_ptr<arcade::GameHost> MakeWarpHost();
World* WarpHostWorld(arcade::GameHost* h);        // (the host's own screen draws the real world)
int WarpSeatPlayer(arcade::GameHost* h, int seat); // a seat's player id in the world (-1: none)
std::string WarpOpts(int arena, int skill, int fill); // the lobby's options
void WriteInput(const Input& in, Writer& w);
void ReadInput(Reader& r, Input& in);
void WriteWorld(const World& w, Writer& out, uint32_t evTotal);
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal);
int RunWarpNetTest();
int RunWarpNetLoop(int lagMs, bool mem);   // --net-loop warp [lagMs] [mem]
}
