#pragma once
// Mouthful over the Deep Arcade's session (design doc p. 19, "Simulation and networking"): the host runs the round (the
// web, the NPC predators, the bots) and snapshots it 20 times a second; each guest keeps a mirror World built from the
// same seed and options and drawn by the same scene, swimming its own mouth ahead of the host. Every person's play,
// solo included, is an Input (WriteInput/ReadInput). The snapshot is one templated Visit (write and read): any field a
// screen draws must be in it. Interest management: the web's fish and the corpses within 100 m of the viewer.
#include "mouthful.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <memory>
#include <string>

namespace mf {
enum MfAct : uint8_t { MA_INPUT = 1, MA_HELLO = 2 };
void WriteInput(const Input& in, Writer& w);
bool ReadInput(Reader& r, Input& in);              // (after the MA_INPUT byte)
void OrderHello(Writer& w, const std::string& name, const std::string& look = "");
void WriteWorld(World& w, int viewer, Writer& out);
void PackWorld(World& w, int viewer, Writer& out); // compressed
bool ReadWorld(Reader& r, World& w, bool keepOwn, int* viewerOut = nullptr);
std::unique_ptr<arcade::GameHost> MakeMouthfulHost();
World* MouthfulHostWorld(arcade::GameHost* h);
std::string MouthfulHostOpts(int minutes, int botLevel, int fill, int mode = 0, int path = 2);
uint32_t MouthfulDataHash();
int RunMouthfulNetTest();                          // --mouthful-net-test
int RunMouthfulNetLoop(bool mem);                  // --net-loop mouthful [mem]
}
