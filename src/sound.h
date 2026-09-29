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

struct AudioVolumes { float master = 0.8f, music = 0.6f, sfx = 0.8f, ambience = 0.65f; };
AudioVolumes& Volumes();

void AudioInit();                       // after InitAudioDevice; a no-op without a device
void AudioClose();
void AudioFrame(float dt, bool parkour); // keep the stream fed; outside the parkour section the score and ambience fade out
void AudioLevel(int level);             // which score and ambience (a PL_ level, or PL_COUNT for the Abyss)
void AudioListener(Vector2 at);         // where the diver's ears are (world px)
void AudioDay(float daylight);          // 0 night .. 1 noon (the Island's crickets and flutes come out at night)
void AudioFlow(float amount);           // a draught or current around the diver, 0..1 (a subtle whoosh)
void AudioSlide(float amount);          // the scrape of a slide, 0..1
void AudioTension(float amount);        // an apex predator is near: the score tightens, 0..1
void SfxAt(Sfx s, Vector2 at, float vol = 1, float pitch = 1);
void Sfx2D(Sfx s, float vol = 1, float pitch = 1, float pan = 0);
void BeastSound(const char* name, float size, int cue, Vector2 at, float vol = 1); // every species gets its own voice from its name and size
float BeastCallRate(const char* name);  // how often it calls, per second, when nothing is happening
bool BeastIsSilent(const char* name);    // plants and the like: no calls
void AbyssSound(int kind, int cue, float dist, float pan); // the Abyss's creatures (AbyssCreatureKind)
bool AudioSelfTest(const char* wavPath); // depth.exe --audio-test [out.wav]

// ---------------------------------------------------------------- the whole game's sound (Master Reference, "Sound design")
// Buses: music, ambience, effects, UI, voice. Effects duck the music 3 dB on combat impacts; voice ducks everything 4 dB.
enum CueBus { CB_SFX, CB_MUSIC, CB_AMB, CB_UI, CB_VOICE };
// How a registered cue is synthesized (sound.cpp builds each from these recipes and the cue's numbers).
enum CueRecipe {
    CR_TICK, CR_CLICK, CR_CONFIRM, CR_CANCEL, CR_ERROR, CR_WHOOSH, CR_LATCH, CR_PLAQUE, CR_CREAK, CR_SONAR, CR_ORGAN2,
    CR_FLARE, CR_JELLYHUM, CR_GAUGE, CR_WHEEL, CR_RUNGS, CR_SPOOL, CR_TILT, CR_CLOCK, CR_STEP, CR_GEAR, CR_DOOR,
    CR_LADDER, CR_FANFARE, CR_CHALK, CR_PLUCK, CR_BELL, CR_THUD, CR_TOKEN, CR_DRUM, CR_COUNT
};
// One named cue: its bus, its recipe and base numbers, how it varies (random pitch +-pitchVar, and `variants`
// filter/length variants), its priority (low ones are dropped first when the 24 effect voices are full), how many of
// it may sound at once, and whether it ducks the music.
struct CueDef { const char* name; int bus, recipe; float freq, dur, gain, pitchVar; int variants, priority, maxSim; bool duck; };
const CueDef* CueTable(int& count);     // data.cpp: the registry
int CueIndex(const char* name);         // -1 if unknown
void PlayCue(const char* name, float vol = 1, float pan = 0);
// Outside the parkour section: the salon's waltz and ambience bed. station: the station screen that is open
// (a Scene as int, -1 for none) - it adds its motif; mourning: a crew member died, the organ plays alone.
void AudioHub(bool on, int station, bool mourning);
enum ReverbRoom { RR_SALON, RR_CAVE, RR_KELP, RR_HALL, RR_OPENSEA, RR_COUNT };
void AudioRoom(int room);               // the generated convolution reverb's room