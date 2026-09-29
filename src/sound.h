#pragma once
// The parkour section's sound, all synthesized in code (sound.cpp): one stereo stream mixing a pool of voices
// (oscillators, noise, formant filters, pitch and filter sweeps), a generative ambient score and an ambience bed
// per level, a reverb send, and an underwater low-pass. Nothing is loaded from disk.
#include "raylib.h"

enum class Sfx {
    Jump, Land, HardLand, Step, WallJump, Dash, Roll, Stun, Grab, Backflip, Glide, Splash, Wade,
    Death, DeathWater, Respawn, Win, Launch, SoftLand,
    Pistol, Blunderbuss, Cannon, Torpedo, BombThrow, Blast, Ink, Barrel, Crumble,
    BossHit, KrakenRoar, KrakenWarn, KrakenSwipe, KrakenDeath, BBGrowl, BBCharge, BBCrash, BBHurt, BBDeath,
    Bubbles, PirateCry, BirdSquawk, Clank,
    COUNT
};
enum BeastCue { CUE_CALL, CUE_ALARM, CUE_STRIKE, CUE_PAIN, CUE_DEATH, CUE_CHEW, CUE_GRAB };

struct AudioVolumes { float master = 0.8f, music = 0.5f, sfx = 0.8f, ambience = 0.65f; };
AudioVolumes& Volumes();

void AudioInit();                       // after InitAudioDevice; a no-op without a device
void AudioClose();
void AudioFrame(float dt, bool parkour); // keep the stream fed; outside the parkour section the score and ambience fade out
void AudioLevel(int level);             // which score and ambience (a PL_ level, or PL_COUNT for the Abyss)
void AudioListener(Vector2 at);         // where the diver's ears are (world px)
void AudioDay(float daylight);          // 0 night .. 1 noon (the Island's crickets and flutes come out at night)
void AudioFlow(float amount);           // a draught or current around the diver, 0..1 (a subtle whoosh)
void AudioSlide(float amount);          // the scrape of a slide, 0..1
void SfxAt(Sfx s, Vector2 at, float vol = 1, float pitch = 1);
void Sfx2D(Sfx s, float vol = 1, float pitch = 1, float pan = 0);
void BeastSound(const char* name, float size, int cue, Vector2 at, float vol = 1); // every species gets its own voice from its name and size
float BeastCallRate(const char* name);  // how often it calls, per second, when nothing is happening
bool BeastIsSilent(const char* name);    // plants and the like: no calls
void AbyssSound(int kind, int cue, float dist, float pan); // the Abyss's creatures (AbyssCreatureKind)
bool AudioSelfTest(const char* wavPath); // depth.exe --audio-test [out.wav]
