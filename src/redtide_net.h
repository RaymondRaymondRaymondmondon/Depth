#pragma once
namespace voice { struct Hearing; }   // (voice.h)
// Red Tide over the Deep Arcade's network session (the design doc's stage 4: four divers, one match; the session
// layer is docs/design/5_Depth_Arcade_Networking.md). Host-authoritative, like the Trawl: the host runs the one real
// Match (a RedTideHost, the arcade's GameHost for G_RED_TIDE) and snapshots it 20 times a second; each guest keeps a
// mirror Match (built from the same map and seed, so the level, props and stations are already there) and the
// snapshot overwrites everything that moves. A diver is played by sending its input (DiverInput: the look, the swim,
// the held buttons and the presses since the last send). Solo play goes through the same ApplyDiverInput.
// Headless (no drawing); redtide_game.cpp is the scene for all three (solo, host, guest).
#include "redtide_match.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <memory>
#include <string>

namespace rt {

// ---------------------------------------------------------------- a diver's input
enum : uint32_t {
    DI_FIRE = 1, DI_ADS = 2, DI_SPRINT = 4, DI_USE = 8,                                  // held
    DI_USE_P = 16, DI_RELOAD_P = 32, DI_MELEE_P = 64, DI_THROW_P = 128, DI_TAC_P = 256,     // presses
    DI_BUILD_P = 512, DI_BRUSH_P = 1024, DI_BENCH_P = 2048, DI_DRUM_P = 4096, DI_CHARM_P = 8192, DI_PING_P = 16384,
    DI_PRESSES = DI_USE_P | DI_RELOAD_P | DI_MELEE_P | DI_THROW_P | DI_TAC_P | DI_BUILD_P | DI_BRUSH_P | DI_BENCH_P | DI_DRUM_P | DI_CHARM_P | DI_PING_P,
};
struct DiverInput {
    float yaw = 0, pitch = 0;    // the look (the client owns its own view)
    Vector3 wish{0, 0, 0};       // where it swims (world, length <= 1)
    float vert = 0;              // up/down (-1..1)
    uint32_t btn = 0;
    int8_t slot = -1;            // a weapon picked (1-3 keys, the wheel), -1 none
};
void WriteDiverInput(const DiverInput& in, Writer& w);
bool ReadDiverInput(Reader& r, DiverInput& in);
void MergeDiverInput(DiverInput& into, const DiverInput& next);   // the newest look and held keys; presses and picks add up until used
void ClearDiverPresses(DiverInput& in);
void ApplyDiverInput(Match& m, int d, const DiverInput& in, float dt);   // one step of diver d (the presses act once)

// the messages a client sends (Session::Act)
enum : uint8_t { RA_INPUT = 0, RA_LOOK = 1 };
void WriteInputAction(const DiverInput& in, Writer& w);
// the diver's look (the Locker's suit and helmet, the Wardrobe's skin and costume), sent once and on a change
void WriteLookAction(const std::string& suit, const std::string& helmet, const std::string& skin, const std::string& costume, Writer& w);

// ---------------------------------------------------------------- the snapshot
// (everything a screen draws or the HUD reads; the effects log rides along as its newest events with their absolute
// numbers, so a mirror's screen replays each hit and blast once, however many snapshots carry it)
void WriteMatch(const Match& m, Writer& out);
// into a mirror: (re)built from the map and seed when they change; `keepLook` (a diver index, -1 none) keeps that
// diver's yaw and pitch (the guest's own view) and blends its position toward the host's instead of jumping
bool ReadMatch(Reader& r, Match& m, int keepLook = -1);
// The Long Night (a host-side save, redtide_longnight_<map>.sav next to the exe): a small head (tide, clock) the menus
// read cheaply, then the match's snapshot. Loading fits it to the seats: new seats get a fresh diver at the start,
// seats no longer there are played by bots. A finished Long Night is cleared.
// voice chat (voice.h): how diver `you` hears diver `them`: the team's helmet radios (clearer up close, where the water
// carries it too; crackling when downed), and in Poachers the rival pair only through the water, close by
voice::Hearing HearDiver(const Match& m, int you, int them);
int RunRedTideVoiceTest();                               // depth.exe --redtide-voice-test
bool SaveLongNight(const Match& m);
bool LongNightSaved(const std::string& map, int* tide = nullptr, float* time = nullptr);
bool LoadLongNight(const std::string& map, Match& m, int seats);
void ClearLongNight(const std::string& map);
// keeps a Long Night saved while it's played: at every calm, every two minutes, and cleared when it ends
struct LongNightSaver {
    int lastPhase = -1; float t = 0;
    void Tick(const Match& m, float dt);
};

// the host's game (arcade_games.cpp's MakeGameHost for G_RED_TIDE); Configure takes "<map>[:<mode key>]"
std::unique_ptr<arcade::GameHost> MakeRedTideHost();
Match* RedTideHostMatch(arcade::GameHost* h);              // the host's real match (the host's own screen draws it), or null
uint32_t RedTideDataHash();                                // the rules a peer must share (into arcade::DataHash)

int RunRedTideNetTest();                                   // depth.exe --redtide-net-test
int RunRedTideNetLoop(bool forceMemory);                   // depth.exe --net-loop redtide [mem] (a host and three guests)

} // namespace rt
