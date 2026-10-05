#pragma once
// NOCLIP over the arcade session (design doc p. 34, "Multiplayer"): host-authoritative. The host steps the real World;
// each player gets their own snapshot (the campaign, the crew, the Labs, and only the loot and entities on their own
// level near them). Levels aren't sent: a guest generates them from the day's seed (World::mirror rolls no loot).
// The snapshot is one templated Visit: any new field a screen draws must be added there (--noclip-net-test checks it).
#include "arcade_game.h"
#include "noclip.h"
#include <memory>
#include <string>

namespace nc {
std::unique_ptr<arcade::GameHost> MakeNoclipHost();
World* NoclipHostWorld(arcade::GameHost* h);
int NoclipSeatPlayer(arcade::GameHost* h, int seat);
std::string NoclipOpts(int bots, int mode);
void WriteInput(const Input& in, Writer& w);
void ReadInput(Reader& r, Input& in);
void WriteCommand(const Command& c, Writer& w);
bool ReadCommand(Reader& r, Command& c);
void WriteWorld(const World& w, int viewer, Writer& out);
bool ReadWorld(Reader& r, World& w, uint32_t* evSeen);
uint32_t NoclipDataHash();
int RunNoclipNetTest();
int RunNoclipNetLoop(int lagMs, bool mem);
}
