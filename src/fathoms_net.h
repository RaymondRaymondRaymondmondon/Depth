#pragma once
// Fathoms over the arcade session (the doc's "Multiplayer and networking"): host-authoritative. The host steps the real
// World at 20 ticks a second, runs the AI seats, validates every Command (World::Apply), and sends each client only
// what that client can see (fog-filtered: an enemy unit's position never reaches a player who can't see it). Guests
// build the identical map from the Settings in the snapshot (World::Init is deterministic), then mirror the dynamic
// state. All play, solo included, is a Command. The snapshot is one templated Visit (write and read): any new field a
// screen draws must be added there; --fathoms-net-test checks the mirror rewrites byte-identical.
#include "arcade_game.h"
#include "fathoms.h"
#include <memory>
#include <string>

namespace fa {
std::unique_ptr<arcade::GameHost> MakeFathomsHost();
World* FathomsHostWorld(arcade::GameHost* h);            // (the host's own screen draws the real world, through its own player's eyes)
int FathomsSeatPlayer(arcade::GameHost* h, int seat);    // a seat's player
std::string FathomsOpts(const Settings& s);              // the lobby's options (map, victory, factions per seat, AI levels)
bool ParseFathomsOpts(const std::string& o, Settings& s);
void WriteCommand(const Command& c, Writer& w);
bool ReadCommand(Reader& r, Command& c);
void WriteWorld(const World& w, Writer& out, int viewer, uint32_t evTotal, bool filter = true);   // (filter false: write all the world holds, for a mirror's re-write)
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal);
uint32_t FathomsDataHash();
int RunFathomsNetTest();
int RunFathomsNetLoop(int lagMs, bool mem);   // --net-loop fathoms [lagMs] [mem]
}
