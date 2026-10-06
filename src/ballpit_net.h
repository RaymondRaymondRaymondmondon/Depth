#pragma once
// Ball Pit Brawl over the arcade session: host-authoritative. The host steps the real World at 60 Hz and snapshots it
// at 20 Hz (per viewer: the floor darts are only the ones near you); guests mirror it and draw it with the same scene.
// All play, solo or networked, is a bp::Input. The snapshot is one templated Visit (write and read): any new World,
// Player, Dart, Ball, Cannon or Ent field a screen draws must be added there; --ballpit-net-test checks the mirror
// rewrites byte-identical.
#include "arcade_game.h"
#include "ballpit.h"
#include <memory>
#include <string>

namespace bp {
std::unique_ptr<arcade::GameHost> MakeBallPitHost();
World* BallPitHostWorld(arcade::GameHost* h);          // (the host's own screen draws the real world)
int BallPitSeatPlayer(arcade::GameHost* h, int seat);   // a seat's player id (-1: none)
std::string BallPitOpts(int mode, int skill, int fill);  // the lobby's options
void WriteInput(const Input& in, Writer& w);
void ReadInput(Reader& r, Input& in);
void WriteWorld(const World& w, Writer& out, uint32_t evTotal, int viewer);
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal);
uint32_t BallPitDataHash();
int RunBallPitNetTest();
int RunBallPitNetLoop(int lagMs, bool mem);   // --net-loop ballpit [lagMs] [mem]
}
