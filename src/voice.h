#pragma once
// ============================================================================
//  Voice chat for the Deep Arcade (the Trawl's design doc: "Talking. Communication is the core skill, so the game
//  carries it, not a third-party app"). Headless apart from the microphone:
//   - the codec: IMA ADPCM, 16 kHz mono, 4 bits a sample, in 20 ms frames that each carry their own first sample and
//     step, so a lost packet costs only its own 20 ms (164 bytes a frame, 66 kbit/s: plenty on a LAN or ZeroTier);
//   - the microphone (miniaudio, which raylib already builds in), opened only while voice is wanted;
//   - the sender: push-to-talk (the Talk action) or an open mic behind a level gate;
//   - the speakers: a jitter buffer each (60 ms to start, concealment for a lost frame), resampled into the mixer's
//     voice bus and shaped by how the game says each is heard (Hearing: distance, walls, a walkie's radio, the water,
//     a ghost's cold reverb).
//  The arcade session carries the frames (M_VOICE, unreliable; the host relays them). A speaker is a lobby seat.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace voice {

constexpr int RATE = 16000;               // samples a second
constexpr int FRAME = 320;                // 20 ms
constexpr int ENC_BYTES = 4 + FRAME / 2;  // the first sample (2), the step index (1), a spare (1), 4 bits a sample after it
constexpr int SELF = 99;                  // the mic test's loopback speaker

// ---- the codec
void Encode(const int16_t* pcm, uint8_t* out);                    // FRAME samples -> ENC_BYTES
bool Decode(const uint8_t* in, size_t n, int16_t* pcm);           // ENC_BYTES -> FRAME samples

// ---- the microphone
bool MicOpen(std::string* err = nullptr);
void MicClose();
bool MicIsOpen();
std::string MicName();
int MicFrames(std::vector<int16_t>& out);  // appends the whole frames captured since the last call; returns how many
void MicFeed(const int16_t* pcm, int n);   // (tests: as if the microphone heard it)

// ---- the sender's gate
float FrameLevel(const int16_t* pcm);      // 0..1, a speech-weighted loudness (RMS in dBFS mapped from -60..0)
struct Gate {
    float hold = 0;                        // open mic: stays open 300 ms after the voice drops (no clipped word ends)
    bool open = false;
    bool Step(const int16_t* pcm, bool pushToTalk, bool openMic, float sensitivity);   // true: send this frame
};

// ---- the speakers
struct Hearing {
    float gain = 1;                        // 0: not heard at all
    float pan = 0;                         // -1 left .. 1 right
    float muffle = 0;                      // 0 clear .. 1 through a bulkhead (a low-pass from 7 kHz down to 400 Hz)
    float radio = 0;                       // a walkie or a helmet radio: band-limited, a little crushed, crackling
    float bubble = 0;                      // a voice in the water: bubbling and dull
    float ghost = 0;                       // the dead: a cold, hollow reverb
};
void Receive(int speaker, uint16_t seq, const uint8_t* data, size_t n);
void SetHearing(int speaker, const Hearing& h);   // the game says how a speaker is heard, every frame (it lapses to plain after half a second)
bool Speaking(int speaker);                // heard in the last 150 ms (HUD icons, a diver's mouth)
float Level(int speaker);                  // the loudness being played now, 0..1
bool AnyActive();                          // someone is being heard (the mixer ducks the rest)
void Render(float* stereo, int n, int sr); // writes n frames of interleaved stereo at sr (the voice bus)
void Tick(float dt);                       // the main loop, once a frame (the hearings' lapse clock)
void Reset();                              // a session ends: every speaker forgotten

// ---- stats (tests, the settings page)
struct Stats { int framesSent = 0, framesHeard = 0, framesLost = 0, framesLate = 0, concealed = 0; };
Stats& GetStats();

int RunVoiceTest();                        // depth.exe --voice-test

}  // namespace voice
