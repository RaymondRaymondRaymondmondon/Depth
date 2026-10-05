#pragma once
// Fowl Play over the arcade session: host-authoritative. The host steps the real World at 60 Hz and snapshots it at
// 20 Hz; guests mirror it. A guest's shots carry how far behind the host it sees the birds (Input::lagSteps, up to
// 150 ms), and the host rewinds the birds that far before testing the shot (the doc's lag compensation).
// The snapshot is one templated Visit: any new field a screen draws must be added there (--fowl-net-test checks it).
#include "arcade_game.h"
#include "fowl.h"
#include <memory>
#include <string>

namespace fp {
std::unique_ptr<arcade::GameHost> MakeFowlHost();
World* FowlHostWorld(arcade::GameHost* h);
int FowlSeatPlayer(arcade::GameHost* h, int seat);
std::string FowlOpts(int mode, int skill, int fill);
void WriteInput(const Input& in, Writer& w);
void ReadInput(Reader& r, Input& in);
void WriteCommand(const Command& c, Writer& w);
bool ReadCommand(Reader& r, Command& c);
void WriteWorld(const World& w, Writer& out);
bool ReadWorld(Reader& r, World& w, uint32_t* evSeen);
uint32_t FowlDataHash();
int RunFowlNetTest();
int RunFowlNetLoop(int lagMs, bool mem);
}
