#pragma once
// The Study's soundscape mixer (Master Reference, "The Study: the soundscape mixer"): up to 8 layers of synthesized
// noise, nature and generative music, each with a channel strip, into a master with low/high cut, warmth and an
// always-on limiter. Everything is made here in code; nothing is a recording. Headless (no raylib): the game mixes it
// into its audio stream as the Study bus, and --study-audio-test renders it offline. Every tuning number lives in
// study_data.cpp.
#include <cstdint>
#include <string>
#include <vector>

namespace study {

enum LayerKind : uint8_t {
    L_WHITE, L_PINK, L_BROWN, L_GREY, L_BLUE,                                      // noise
    L_RAIN, L_STREAM, L_SURF, L_WIND, L_FIRE, L_FOREST, L_THUNDER, L_NAUTILUS,       // nature
    L_MUSIC,                                                                         // generative music
    L_KIND_COUNT
};
enum MusicStyle : uint8_t { M_AMBIENT, M_LOFI, M_PIANO, M_SALON, M_LOUNGE, M_STYLE_COUNT };
enum Family : uint8_t { F_NOISE, F_NATURE, F_MUSIC };

constexpr int MAX_LAYERS = 8, MAX_PARAMS = 4, MAX_INSTR = 5;

// one control of a layer: a slider, or a choice when `choices` is set ("roof|window|leaves|tent|sea")
struct ParamDef { const char* name; float lo, hi, def; const char* choices; };
struct LayerDef {
    const char* name; Family family; int nParams; ParamDef p[MAX_PARAMS];
    float tuning[8];            // the layer's synthesis constants (see study_data.cpp for what each slot means)
};
const LayerDef& Layer(int kind);

struct StyleDef {
    const char* name; float bpmLo, bpmHi, bpmDef; int beatsPerBar; bool swing;
    int nInstr; const char* instr[MAX_INSTR];
    int progression[4][8];      // chord roots as scale degrees (0-6), -1 ends a row; one row is picked per section
    float density, brightness;  // defaults
};
const StyleDef& Style(int style);

// the music layer's own settings (the rest of its strip is shared with every layer)
struct MusicSettings {
    int style = M_AMBIENT;
    int key = 2;                // semitones above C (2 = D)
    bool minor = false;
    float bpm = 64, density = 0.5f, brightness = 0.5f, variation = 0.5f;
    bool instrOn[MAX_INSTR] = {true, true, true, true, true};
    uint32_t seed = 1;          // a session the player liked replays from its seed
};

struct LayerState {
    uint32_t id = 0;            // identity across edits (NewLayer hands out fresh ones), so removing a layer never restarts another
    int kind = L_BROWN;
    float level = 0.7f;         // 0..1 (level-matched: equal settings sound equally loud)
    bool mute = false, solo = false;
    float tone = 0;             // -1 dark .. +1 bright
    float width = 0.7f;         // 0 mono .. 1 wide
    float pan = 0;              // -1 .. 1
    float p[MAX_PARAMS] = {};   // the layer's own controls (defaults from its LayerDef)
    MusicSettings music;
};
LayerState NewLayer(int kind);

struct MasterState {
    float volume = 0.8f, lowCut = 30, highCut = 16000, warmth = 0.2f;
    int fadeMinutes = 0;        // 0 off, else 15/30/60/90: everything fades to silence over that time
    bool duckOnBreak = true;    // the chronometer's breaks duck the music
};

struct Preset { const char* name; int n; LayerState layers[MAX_LAYERS]; };
int PresetCount();
const Preset& GetPreset(int i);          // the 8 that ship with the Study

// ---- the mixer
struct MixerState { std::vector<LayerState> layers; MasterState master; };

void SetState(const MixerState& s);      // the game hands over the whole desk (parameters are smoothed over 50 ms)
const MixerState& GetState();
void SetBreak(bool onBreak);             // the chronometer: on a break the music ducks (if the desk says so)
void RestartFade();                      // the fade timer starts again from full
float FadeProgress();                    // 0 .. 1 of the fade timer
void Render(float* interleavedLR, int frames, int sampleRate);   // adds nothing: writes the Study's output
double Beat();                           // the music engine's beat clock (beats since start): the Lounge band plays to it
int BeatsPerBar();
bool MusicPlaying();                     // a music layer is audible
float RainLevel();                       // 0..1: how hard any rain layer falls (the Study window shows it)
float FireSize();                        // 0..1: the Fire layer's size (0 = off: the hearth falls to embers)
bool LoungeBandPlaying();                // the Lounge band style is on and audible
void ResetEngine(uint32_t seed);         // fresh generators (tests)
}
