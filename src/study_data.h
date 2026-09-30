#pragma once
// The Study's timings and limits (the numbers live in study_data.cpp's Numbers()).
namespace study {
struct StudyNumbers {
    // entering
    float descentSeconds = 1.6f;        // the hatch wheel turns, the camera drops down the ladder
    float crossfadeSeconds = 1.5f;      // the salon's sound out, the Study's in
    // focus mode
    float focusIdleSeconds = 10;        // no mouse movement for this long: the rail and panels fade out
    float focusFadeSeconds = 2.5f;
    // the chronometer (all adjustable in the drawer; these are the defaults and bounds)
    int workMinutes = 25, breakMinutes = 5, longBreakMinutes = 15, roundsPerLongBreak = 4;
    int minMinutes = 1, maxMinutes = 120;
    // calm motion
    float maxIdleHz = 0.5f;             // idle motion runs at this or slower
    float flashLimit = 0.10f;           // no region may change brightness by more than this ...
    float flashWindow = 0.5f;           // ... in under this many seconds
    float focusDimMax = 0.8f;           // the Focus Dim slider's top
    float minMotionSpeed = 0.3f;        // at full Focus Dim, idle motion runs at 30% speed
    int lowPowerFps = 30;
    // the mixer
    float smoothingSeconds = 0.05f;     // every parameter change is smoothed (no clicks)
    float loudnessTargetDb = -27;       // every layer at level 1 is matched to this perceived loudness
    float limiterCeiling = 0.89f;       // -1 dBFS
    float musicCompThreshold = 0.25f, musicCompRatio = 3;   // the gentle compressor on the music bus
    float breakDuckDb = -12;
    int fadeChoices[5] = {0, 15, 30, 60, 90};
    float sectionFadeBars = 8;          // a new section of the music fades in over at least this many bars
};
const StudyNumbers& Numbers();

// the music engine's instruments (all synthesized): envelope, level, filter, FM and detune constants
enum Instr { I_PAD, I_BOWED, I_GLASS, I_EPIANO, I_PIANO, I_HARMONIUM, I_MUSICBOX, I_SQUEEZE, I_HORN, I_UPRIGHT,
             I_BASS, I_KICK, I_BRUSH, I_SNARE, I_HAT, I_CLINK, I_COUNT };
struct InstrDef {
    const char* name;
    float atk, dec, sus, rel;   // seconds, seconds, level 0..1, seconds
    float gain, cutoff;         // level, low-pass Hz at brightness 0.5
    float fmRatio, fmIndex;     // FM voices; for the piano, fmIndex is its inharmonicity
    float detune, vibrato;      // cents of detune between oscillators, vibrato depth (fraction of pitch)
    float pan;                  // where it sits
};
const InstrDef& Instrument(int i);

// perceived-loudness matching: the gain that brings each layer (at level 1, default controls) to loudnessTargetDb.
// --study-audio-test measures them; when a layer's sound is retuned, paste its new number from the test's report.
float LayerNorm(int kind);
float StyleNorm(int style);
}
