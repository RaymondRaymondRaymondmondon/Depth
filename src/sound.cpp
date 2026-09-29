// The parkour section's sound: a small software synthesizer (see sound.h).
//
// Voices: each is an oscillator (sine, triangle, saw, square, noise or FM) with an exponential pitch glide,
// vibrato, an attack/hold/decay envelope, an optional swept low-pass, a high-pass, and up to two formant
// band-passes (which is how the cries, growls, meows and "arrr"s get their vowels). A sound effect is a handful
// of voices started together or in a little sequence. Voices go to one of three buses - effects, score,
// ambience - each with its own volume; effects are muffled by a low-pass on the underwater levels, and all
// three send some signal to a Freeverb-style reverb sized per level (a steel duct rings, a cavern goes on).
//
// The score is generative: per level a scale, a tempo, a pad timbre, a lead timbre and a few rules (a chord
// every eight beats, a bass under it, a sparse lead walking the scale, a level's own percussion), kept quiet
// and slow - it's meant to sit under the play, mostly peaceful. The ambience bed is filtered noise with slow
// LFOs (wind, surf, the hum of machinery, the pressure of deep water) plus little random events (drips,
// bubbles, birdsong, insects, creaks, clanks).
#include "sound.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <chrono>
#include <cstdio>

namespace {

constexpr int SR = 44100, BLOCK = 2048, CTRL = 32; // sample rate, stream buffer, control-rate block
constexpr float TAU = 6.2831853f;
AudioStream gStream{};
bool gReady = false;
AudioVolumes gVol;
float gBuf[BLOCK * 2];

uint32_t gRng = 0x9E3779B9u;
inline float Noise() { gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5; return (float)(gRng & 0xFFFFFF) / 8388608.0f - 1.0f; }
inline float R01() { return (Noise() + 1) * 0.5f; }
inline float RR(float a, float b) { return a + (b - a) * R01(); }
float Hash01(const char* s, unsigned salt) { unsigned h = 2166136261u ^ salt; for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u; return (h % 10007) / 10007.0f; }

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Set(int type, float f, float q) { // 0 low-pass, 1 high-pass, 2 band-pass (0 dB peak)
        f = std::clamp(f, 20.0f, SR * 0.45f);
        float w = TAU * f / SR, cs = cosf(w), sn = sinf(w), al = sn / (2 * std::max(0.1f, q)), a0 = 1 + al;
        if (type == 0) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; }
        else if (type == 1) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; }
        else { b0 = al; b1 = 0; b2 = -al; }
        a1 = -2 * cs; a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    float Run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

enum Osc { W_SINE, W_TRI, W_SAW, W_SQR, W_NOISE, W_FM };
enum Bus { B_SFX, B_MUSIC, B_AMB };

struct Voice {
    bool on = false;
    int bus = B_SFX, wave = W_SINE;
    float f0 = 440, f1 = 440, curve = 1;      // pitch glides from f0 to f1 (exponentially; curve bends it)
    float dur = 0.2f, t = 0, delay = 0;
    float atk = 0.004f, hold = 0, decPow = 1.6f;
    float gain = 0.3f, gl = 1, gr = 1, send = 0.2f;
    float vibR = 0, vibD = 0, tremR = 0, tremD = 0;
    float noiseMix = 0, det = 0;              // noise blended into the tone; a detuned second oscillator (chorus)
    float cut0 = 0, cut1 = 0, q = 0.8f, hp = 0;
    float fa0 = 0, fa1 = 0, fb0 = 0, fb1 = 0, fq = 7; // two formant band-passes, gliding
    float fmRatio = 2, fmIndex = 0, fmIndex1 = -1;
    // state
    float ph = 0, ph2 = 0, ph3 = 0, fcur = 440, env0 = 0, env1 = 0, fmi = 0;
    int ctr = 0;
    Biquad lp, hpf, fa, fb;
};
Voice gV[128];

Voice& NewVoice(int bus = B_SFX) {
    int pick = -1; float worst = -1;
    for (int i = 0; i < 128; i++) {
        if (!gV[i].on) { pick = i; break; }
        float u = gV[i].t / gV[i].dur;
        if (gV[i].bus != B_MUSIC && u > worst) { worst = u; pick = i; } // steal the most finished effect, never the score
    }
    if (pick < 0) pick = 0;
    gV[pick] = Voice{};
    gV[pick].on = true;
    gV[pick].bus = bus;
    return gV[pick];
}

float EnvAt(const Voice& v, float t) {
    if (t < 0) return 0;
    if (t < v.atk) return t / v.atk;
    if (t < v.atk + v.hold) return 1;
    float d = std::max(1e-4f, v.dur - v.atk - v.hold), u = (t - v.atk - v.hold) / d;
    return u >= 1 ? 0 : powf(1 - u, v.decPow);
}

// ---- the reverb
struct Comb { std::vector<float> b; int i = 0; float st = 0; float Run(float x, float fb, float damp) { float y = b[i]; st = y * (1 - damp) + st * damp; b[i] = x + st * fb; if (++i >= (int)b.size()) i = 0; return y; } };
struct Allp { std::vector<float> b; int i = 0; float Run(float x) { float bo = b[i]; float y = -x + bo; b[i] = x + bo * 0.5f; if (++i >= (int)b.size()) i = 0; return y; } };
Comb gCombL[4], gCombR[4];
Allp gApL[2], gApR[2];
float gRevFb = 0.8f, gRevDamp = 0.3f, gRevWet = 0.25f;

// ---- the level's score and ambience
struct Score {
    float root; int scale[8]; int n; float bpm;
    int padWave; float padCut, padGain, padDet;
    int leadWave; float leadCut, leadGain, leadProb; int leadOct; float leadBeats; float leadFm;
    float bassGain;
    float rev, damp, wet;
};
const Score SCORES[PL_COUNT + 1] = {
    // the Pipes: D dorian, slow, dark saw pads and metallic bell tones that ring down the ducts
    {73.42f, {0, 2, 3, 5, 7, 9, 10}, 7, 58, W_SAW, 650, 0.05f, 0.004f, W_FM, 2600, 0.05f, 0.22f, 2, 3.0f, 2.76f, 0.05f, 0.86f, 0.35f, 0.32f},
    // the Hull: A minor pentatonic, open water: soft sine pads, long gliding whale-like lead notes
    {55.0f, {0, 3, 5, 7, 10}, 5, 50, W_TRI, 900, 0.06f, 0.003f, W_SINE, 1800, 0.05f, 0.18f, 3, 4.0f, 0, 0.05f, 0.9f, 0.5f, 0.38f},
    // the Pirate Ship: E aeolian, a slow 6/8 lilt: a reedy squeezebox pad and a fiddle lead
    {82.41f, {0, 2, 3, 5, 7, 8, 10}, 7, 72, W_SQR, 1100, 0.035f, 0.006f, W_SAW, 2200, 0.04f, 0.3f, 2, 1.5f, 0, 0.05f, 0.62f, 0.4f, 0.14f},
    // the Island: G major pentatonic, marimba by day and a wooden flute by night, over a soft log drum
    {98.0f, {0, 2, 4, 7, 9}, 5, 84, W_SINE, 1400, 0.05f, 0.003f, W_SINE, 4000, 0.06f, 0.35f, 2, 1.0f, 4.0f, 0.04f, 0.55f, 0.5f, 0.12f},
    // the Cave: C phrygian, very slow; a drone and glassy plinks, like tuned drips, in a huge reverb
    {65.41f, {0, 1, 3, 5, 7, 8, 10}, 7, 44, W_SINE, 500, 0.06f, 0.002f, W_SINE, 5000, 0.045f, 0.2f, 3, 2.0f, 3.0f, 0.06f, 0.93f, 0.25f, 0.5f},
    // the Weeds: F lydian, shimmering chorus pads and a harp
    {87.31f, {0, 2, 4, 6, 7, 9, 11}, 7, 66, W_TRI, 1600, 0.05f, 0.007f, W_SAW, 3000, 0.045f, 0.33f, 2, 1.0f, 0, 0.04f, 0.8f, 0.45f, 0.3f},
    // Atlantis: B-flat harmonic minor, a drowned choir and deep bells
    {58.27f, {0, 2, 3, 5, 7, 8, 11}, 7, 48, W_SAW, 1800, 0.045f, 0.005f, W_FM, 2000, 0.05f, 0.2f, 2, 4.0f, 1.41f, 0.05f, 0.92f, 0.35f, 0.42f},
    // the Abyss: F locrian, barely moving: a sub drone, a dissonant cluster, a far-off groan now and then
    {43.65f, {0, 1, 3, 5, 6, 8, 10}, 7, 36, W_SINE, 400, 0.07f, 0.002f, W_SINE, 700, 0.05f, 0.1f, 1, 6.0f, 0, 0.08f, 0.95f, 0.3f, 0.45f},
};
int gLevel = -1;
bool gUnderwater = false;
float gScene = 0, gSceneTarget = 0; // the score and ambience fade in and out with the parkour section
float gBeatT = 0; int gBeat = 0; int gChord = 0; int gLeadDeg = 7;
float gDay = 1, gFlow = 0, gFlowS = 0, gSlide = 0, gSlideS = 0;
Vector2 gEar{0, 0};
float gCallCool = 0;

struct Bed { int type; float f, q, gain, lfoR, lfoD, ampR, ampD; Biquad flt; float ph = 0, ph2 = 0; };
std::vector<Bed> gBeds;
Biquad gFlowF, gSlideF, gSlideF2, gUwL, gUwR;
float gAmbT = 0, gClock = 0;

float Note(const Score& s, int deg, int oct) { // a scale degree (any integer) to Hz
    int n = s.n, o = (deg >= 0 ? deg / n : -((-deg + n - 1) / n)), d = deg - o * n;
    return s.root * powf(2.0f, oct + o + s.scale[d] / 12.0f);
}

void PanGains(float pan, float& gl, float& gr) { pan = std::clamp(pan, -1.0f, 1.0f); gl = sqrtf((1 - pan) * 0.5f) * 1.414f; gr = sqrtf((1 + pan) * 0.5f) * 1.414f; }

// ---------------------------------------------------------------- building blocks for effects
struct Ctx { float vol, pitch, gl, gr; int bus; float send; };
Voice& Tone(const Ctx& c, int wave, float f0, float f1, float dur, float gain, float delay = 0) {
    Voice& v = NewVoice(c.bus);
    v.wave = wave; v.f0 = f0 * c.pitch; v.f1 = f1 * c.pitch; v.dur = dur; v.gain = gain * c.vol; v.delay = delay;
    v.gl = c.gl; v.gr = c.gr; v.send = c.send;
    return v;
}
Voice& Puff(const Ctx& c, float cut0, float cut1, float dur, float gain, float delay = 0) { // filtered noise
    Voice& v = Tone(c, W_NOISE, 100, 100, dur, gain, delay);
    v.cut0 = cut0 * c.pitch; v.cut1 = cut1 * c.pitch;
    return v;
}
Voice& Vox(const Ctx& c, float f0, float f1, float dur, float gain, float fa0, float fa1, float fb0, float fb1, float delay = 0) { // a voiced cry
    Voice& v = Tone(c, W_SAW, f0, f1, dur, gain, delay);
    v.fa0 = fa0 * sqrtf(c.pitch); v.fa1 = fa1 * sqrtf(c.pitch); v.fb0 = fb0 * sqrtf(c.pitch); v.fb1 = fb1 * sqrtf(c.pitch);
    v.noiseMix = 0.08f; v.atk = 0.02f; v.vibR = 6; v.vibD = 0.015f;
    return v;
}
void Bubble(const Ctx& c, float delay, float size) {
    Voice& v = Tone(c, W_SINE, 380 / size, 1300 / size, 0.05f + 0.03f * size, 0.18f, delay);
    v.curve = 0.6f; v.atk = 0.002f; v.decPow = 2.2f;
}
void Thud(const Ctx& c, float f, float gain, float delay = 0) {
    Voice& v = Tone(c, W_SINE, f * 1.8f, f * 0.7f, 0.16f, gain, delay); v.curve = 0.4f; v.decPow = 2.5f;
    Puff(c, 900, 200, 0.07f, gain * 0.6f, delay).decPow = 3;
}
void Crunch(const Ctx& c, int bites, float size) { // a bite: a wet crack and a grinding of bone (a shark's jaws)
    for (int k = 0; k < bites; k++) {
        float d = k * RR(0.07f, 0.12f) * size;
        Voice& v = Puff(c, 2600 / size, 700 / size, 0.06f + 0.04f * size, 0.5f, d); v.hp = 250; v.decPow = 2.4f;
        Voice& w = Tone(c, W_SQR, 140 / size, 70 / size, 0.05f, 0.12f, d); w.cut0 = 900; w.cut1 = 300; w.decPow = 3;
        for (int s = 0; s < 3; s++) { Voice& cl = Tone(c, W_NOISE, 100, 100, 0.012f, 0.25f, d + 0.02f + s * 0.018f); cl.hp = 1800; cl.cut0 = cl.cut1 = 5000; }
    }
}
void Whoosh(const Ctx& c, float f0, float f1, float dur, float gain) {
    Voice& v = Tone(c, W_NOISE, 100, 100, dur, gain); v.fa0 = f0; v.fa1 = f1; v.fq = 2.5f; v.atk = dur * 0.35f; v.decPow = 1.4f;
}

// ---------------------------------------------------------------- the player's and the world's effects
void PlaySfx(Sfx s, Ctx c) {
    bool uw = gUnderwater;
    switch (s) {
    case Sfx::Jump: {
        Voice& v = Tone(c, W_SINE, 190, 380, 0.11f, 0.12f); v.curve = 0.5f;
        Puff(c, 1600, 500, 0.08f, 0.14f).hp = 200;
        if (uw) for (int k = 0; k < 3; k++) Bubble(c, 0.02f + k * 0.04f, RR(0.8f, 1.4f));
        break; }
    case Sfx::Land: Thud(c, 90, 0.28f); if (uw) Bubble(c, 0.03f, 1.6f); break;
    case Sfx::SoftLand: Puff(c, 700, 300, 0.12f, 0.1f); break; // cannon-moss: silent, almost
    case Sfx::HardLand: Thud(c, 70, 0.45f); Thud(c, 50, 0.3f, 0.05f); Vox(c, 150, 105, 0.22f, 0.13f, 650, 520, 1100, 900, 0.04f); break;
    case Sfx::Step: {
        Voice& v = Puff(c, 2200, 700, 0.03f, 0.07f); v.hp = 300; v.decPow = 2.2f;
        Voice& w = Tone(c, W_SINE, 95, 60, 0.05f, 0.08f); w.decPow = 2.5f;
        if (c.pitch > 1.3f) { Voice& r = Tone(c, W_FM, 900, 880, 0.18f, 0.025f); r.fmRatio = 2.7f; r.fmIndex = 1.2f; r.fmIndex1 = 0.1f; } // steel rings
        break; }
    case Sfx::WallJump: { Voice& v = Puff(c, 1800, 1200, 0.07f, 0.15f); v.fa0 = 1500; v.fa1 = 1100; PlaySfx(Sfx::Jump, c); break; }
    case Sfx::Dash: Whoosh(c, uw ? 300 : 500, uw ? 1400 : 2600, 0.24f, 0.38f); if (uw) for (int k = 0; k < 4; k++) Bubble(c, 0.05f + k * 0.03f, RR(0.7f, 1.2f)); break;
    case Sfx::Roll: for (int k = 0; k < 3; k++) Thud(c, 110 - k * 10, 0.15f, k * 0.09f); Whoosh(c, 400, 900, 0.25f, 0.12f); break;
    case Sfx::Stun: Thud(c, 60, 0.5f); Vox(c, 170, 110, 0.35f, 0.16f, 700, 450, 1150, 850, 0.05f); { Voice& r = Tone(c, W_SINE, 2400, 2300, 0.6f, 0.03f, 0.1f); r.tremR = 14; r.tremD = 0.8f; } break;
    case Sfx::Grab: Puff(c, 3000, 1500, 0.05f, 0.12f).hp = 900; Thud(c, 130, 0.12f, 0.02f); break;
    case Sfx::Backflip: Whoosh(c, 700, 2000, 0.3f, 0.22f); break;
    case Sfx::Glide: Whoosh(c, 300, 500, 0.5f, 0.1f); break;
    case Sfx::Splash: {
        Voice& v = Puff(c, 4000, 500, 0.6f, 0.4f); v.atk = 0.01f; v.decPow = 1.4f;
        for (int k = 0; k < 8; k++) Bubble(c, 0.1f + k * 0.05f, RR(0.6f, 1.6f));
        break; }
    case Sfx::Wade: { Voice& v = Puff(c, 2400, 900, 0.15f, 0.08f); v.fa0 = 1200; v.fa1 = 800; break; }
    case Sfx::Death: // a cry cut short, and the thud
        Vox(c, 260, 170, 0.5f, 0.22f, 850, 500, 1250, 950); Thud(c, 60, 0.4f, 0.25f); break;
    case Sfx::DeathWater: // muffled - the cry comes out as a burst of bubbles
        { Voice& v = Vox(c, 230, 150, 0.45f, 0.14f, 700, 450, 1100, 900); v.cut0 = 900; v.cut1 = 500; }
        for (int k = 0; k < 16; k++) Bubble(c, 0.05f + k * 0.035f, RR(0.5f, 1.8f));
        break;
    case Sfx::Respawn: { Voice& a = Tone(c, W_SINE, 660, 660, 0.9f, 0.08f); a.send = 0.6f; Voice& b = Tone(c, W_SINE, 990, 990, 0.9f, 0.06f, 0.12f); b.send = 0.6f; break; }
    case Sfx::Win: for (int k = 0; k < 4; k++) { float f = 392 * powf(2, (k == 0 ? 0 : k == 1 ? 4 : k == 2 ? 7 : 12) / 12.0f); Voice& v = Tone(c, W_FM, f, f, 0.9f, 0.1f, k * 0.12f); v.fmRatio = 4; v.fmIndex = 1.5f; v.fmIndex1 = 0; v.send = 0.5f; } break;
    case Sfx::Launch: { Voice& v = Tone(c, W_SINE, 110, 520, 0.35f, 0.25f); v.vibR = 18; v.vibD = 0.05f; v.curve = 0.7f; Thud(c, 80, 0.3f); break; }
    case Sfx::Pistol: { Voice& v = Puff(c, 6000, 1200, 0.1f, 0.45f); v.hp = 700; Thud(c, 90, 0.3f); Voice& e = Puff(c, 1500, 400, 0.5f, 0.1f, 0.05f); e.send = 0.6f; break; }
    case Sfx::Blunderbuss: { Voice& v = Puff(c, 3500, 300, 0.45f, 0.6f); v.decPow = 1.8f; Thud(c, 55, 0.6f); for (int k = 0; k < 6; k++) { Voice& cr = Puff(c, 5000, 3000, 0.02f, 0.2f, 0.03f + k * 0.03f); cr.hp = 2000; } break; }
    case Sfx::Cannon: { Voice& v = Puff(c, 2000, 150, 0.9f, 0.7f); v.decPow = 1.5f; Thud(c, 45, 0.7f); Voice& r = Puff(c, 400, 120, 1.4f, 0.2f, 0.1f); r.send = 0.7f; break; }
    case Sfx::Torpedo: { Whoosh(c, 300, 900, 0.5f, 0.3f); Voice& p = Tone(c, W_SQR, 85, 95, 0.9f, 0.08f); p.cut0 = p.cut1 = 500; p.tremR = 22; p.tremD = 0.7f; for (int k = 0; k < 5; k++) Bubble(c, k * 0.06f, 1.2f); break; }
    case Sfx::BombThrow: { Voice& v = Puff(c, 7000, 5000, 0.5f, 0.07f); v.hp = 3000; v.tremR = 30; v.tremD = 0.5f; Whoosh(c, 500, 900, 0.3f, 0.1f); break; }
    case Sfx::Blast: { Voice& v = Puff(c, 4000, 150, 1.1f, 0.75f); v.decPow = 1.3f; Thud(c, 40, 0.8f); Voice& r = Puff(c, 600, 100, 1.8f, 0.25f, 0.1f); r.send = 0.8f; break; }
    case Sfx::Ink: { Voice& v = Puff(c, 900, 400, 0.18f, 0.25f); v.fa0 = 500; v.fa1 = 300; Bubble(c, 0.02f, 1.5f); break; }
    case Sfx::Barrel: { Voice& v = Puff(c, 500, 300, 0.7f, 0.25f); v.tremR = 9; v.tremD = 0.6f; Thud(c, 120, 0.25f); break; }
    case Sfx::Crumble: for (int k = 0; k < 5; k++) { Voice& v = Puff(c, 1800, 600, 0.06f, 0.18f, k * 0.05f + RR(0, 0.02f)); v.hp = 300; } break;
    case Sfx::BossHit: Thud(c, 70, 0.6f); { Voice& v = Puff(c, 3000, 800, 0.15f, 0.35f); v.hp = 400; } break;
    case Sfx::KrakenRoar: {
        Voice& v = Tone(c, W_SAW, 62, 44, 2.0f, 0.4f); v.fa0 = 420; v.fa1 = 300; v.fb0 = 850; v.fb1 = 600; v.fq = 5; v.tremR = 13; v.tremD = 0.35f; v.atk = 0.2f; v.send = 0.5f;
        Voice& s2 = Tone(c, W_SINE, 40, 32, 2.0f, 0.35f); s2.atk = 0.3f;
        for (int k = 0; k < 10; k++) Bubble(c, 0.1f + k * 0.12f, RR(1.5f, 2.5f));
        break; }
    case Sfx::KrakenWarn: { Voice& v = Puff(c, 350, 250, 0.8f, 0.25f); v.atk = 0.3f; for (int k = 0; k < 10; k++) Bubble(c, k * 0.07f, RR(1.2f, 2.4f)); break; }
    case Sfx::KrakenSwipe: Whoosh(c, 150, 600, 0.7f, 0.5f); { Voice& v = Puff(c, 300, 120, 0.8f, 0.3f); v.atk = 0.2f; } break;
    case Sfx::KrakenDeath: { Voice& v = Tone(c, W_SAW, 70, 25, 3.5f, 0.4f); v.fa0 = 450; v.fa1 = 250; v.fb0 = 900; v.fb1 = 500; v.fq = 5; v.tremR = 8; v.tremD = 0.4f; v.atk = 0.1f; v.send = 0.7f; for (int k = 0; k < 24; k++) Bubble(c, 0.2f + k * 0.12f, RR(1.2f, 3.0f)); break; }
    case Sfx::BBGrowl: Vox(c, 115, 95, 0.75f, 0.28f, 700, 500, 1100, 1350); break;                          // "Arrr"
    case Sfx::BBCharge: for (int k = 0; k < 4; k++) Thud(c, 85, 0.25f, k * 0.14f); Vox(c, 150, 190, 0.5f, 0.2f, 750, 800, 1150, 1200); break; // boots and a bellow
    case Sfx::BBCrash: { for (int k = 0; k < 6; k++) { Voice& v = Puff(c, 2200, 500, 0.12f, 0.3f, k * 0.04f); v.hp = 200; } Thud(c, 55, 0.6f); Voice& cr = Tone(c, W_SAW, 180, 120, 0.6f, 0.08f, 0.1f); cr.fa0 = cr.fa1 = 500; cr.vibR = 9; cr.vibD = 0.08f; break; } // splintering timber, a creak
    case Sfx::BBHurt: Vox(c, 200, 150, 0.4f, 0.3f, 780, 600, 1200, 1000); break;
    case Sfx::BBDeath: Vox(c, 190, 70, 1.8f, 0.3f, 750, 400, 1150, 800); Thud(c, 50, 0.5f, 1.0f); break;
    case Sfx::Bubbles: for (int k = 0; k < 5; k++) Bubble(c, k * 0.05f, RR(0.7f, 1.5f)); break;
    case Sfx::PirateCry: Vox(c, RR(170, 220), 120, 0.45f, 0.24f, 800, 450, 1200, 900); break;
    case Sfx::BirdSquawk: for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SAW, 900, 1500, 0.12f, 0.12f, k * 0.14f); v.fa0 = 1800; v.fa1 = 2400; v.fq = 4; v.curve = 0.5f; } break;
    case Sfx::Clank: { Voice& v = Tone(c, W_FM, 420, 410, 0.7f, 0.06f); v.fmRatio = 2.76f; v.fmIndex = 3; v.fmIndex1 = 0.2f; v.send = 0.6f; break; }
    default: break;
    }
}

// ---------------------------------------------------------------- beast voices
// Every species gets a voice from its kind (read from its name) and its size (bigger is lower, slower, louder),
// nudged by a hash of its name so no two species sound quite alike.
enum Arch { A_SQUEAK, A_MEOW, A_BARK, A_CHATTER, A_HOOT, A_BIRD, A_BUZZ, A_CLICK, A_HISS, A_GRUNT, A_CROAK, A_BAT,
            A_FISH, A_EEL, A_SHARK, A_WHALE, A_KRAKEN, A_JELLY, A_WORM, A_HUMAN, A_RAY, A_STONE, A_PLANT };
Arch ArchOf(const char* name) {
    static const struct { const char* key; Arch a; } K[] = {
        {"Rat", A_SQUEAK}, {"Mouse", A_SQUEAK}, {"Cat", A_MEOW}, {"Dog", A_BARK}, {"Monkey", A_CHATTER}, {"Owl", A_HOOT},
        {"Gull", A_BIRD}, {"Albatross", A_BIRD}, {"Eagle", A_BIRD}, {"Bat", A_BAT}, {"Frog", A_CROAK}, {"Boar", A_GRUNT},
        {"Snake", A_HISS}, {"Serpent", A_HISS}, {"Lizard", A_HISS}, {"Stalker", A_HISS}, {"Centipede", A_CLICK},
        {"Flea", A_BUZZ}, {"Moth", A_BUZZ}, {"Firefly", A_BUZZ}, {"Beetle", A_BUZZ}, {"Roach", A_BUZZ}, {"Cockroach", A_BUZZ}, {"Cricket", A_BUZZ}, {"Mite", A_BUZZ}, {"Skipper", A_CROAK},
        {"Crab", A_CLICK}, {"Shrimp", A_CLICK}, {"Isopod", A_CLICK}, {"Pillbug", A_CLICK}, {"Spider", A_CLICK}, {"Arachnid", A_CLICK}, {"Mantis", A_CLICK},
        {"Tortoise", A_STONE}, {"Snail", A_CLICK}, {"Phalanx", A_STONE}, {"Brittle", A_CLICK}, {"Hermit", A_CLICK},
        {"Whale", A_WHALE}, {"Manatee", A_WHALE}, {"Orichalcum", A_WHALE}, {"Kraken", A_KRAKEN}, {"Octopus", A_KRAKEN}, {"Cuttlefish", A_KRAKEN}, {"Siphon", A_KRAKEN},
        {"Shark", A_SHARK}, {"Megalodon", A_SHARK}, {"Barracuda", A_SHARK}, {"Scourge", A_SHARK},
        {"Eel", A_EEL}, {"Moray", A_EEL}, {"Leech", A_EEL}, {"Worm", A_WORM}, {"Tremor", A_WORM},
        {"Jelly", A_JELLY}, {"Wisp", A_JELLY}, {"Plankton", A_JELLY}, {"Anemone", A_JELLY}, {"Hydroid", A_JELLY},
        {"Merman", A_HUMAN}, {"Lost One", A_HUMAN}, {"Echo", A_BAT}, {"Ray", A_RAY}, {"Gargoyle", A_STONE}, {"Guardian", A_STONE},
        {"Fish", A_FISH}, {"Sardine", A_FISH}, {"Minnow", A_FISH}, {"Loach", A_FISH}, {"Seahorse", A_FISH}, {"Olm", A_FISH}, {"Cusk", A_FISH}, {"Puffer", A_FISH}, {"Angler", A_FISH}, {"Pilot", A_FISH},
        {"Fungus", A_PLANT}, {"Moss", A_PLANT}, {"Kelp", A_PLANT}, {"Weed", A_PLANT}, {"Vine", A_PLANT}, {"Root", A_PLANT}, {"Bloom", A_PLANT}, {"Lily", A_PLANT},
        {"Spore", A_PLANT}, {"Shroom", A_PLANT}, {"Lichen", A_PLANT}, {"Cabbage", A_PLANT}, {"Algae", A_PLANT}, {"Sponge", A_PLANT}, {"Barnacle", A_PLANT}, {"Borer", A_PLANT}, {"Coconut", A_PLANT},
    };
    for (const auto& k : K) if (strstr(name, k.key)) return k.a;
    return A_FISH;
}

void PlayBeast(Arch a, int cue, float pf, float big, Ctx c) {
    // pf: pitch factor (higher = smaller, shriller); big: 0 tiny .. 1+ giant (louder, slower)
    float d = 1.0f / std::max(0.4f, pf); // durations stretch for big beasts
    bool pain = cue == CUE_PAIN, death = cue == CUE_DEATH, alarm = cue == CUE_ALARM, strike = cue == CUE_STRIKE;
    float up = pain ? 1.35f : alarm ? 1.2f : death ? 1.15f : 1.0f; // hurt and frightened voices go up
    if (cue == CUE_CHEW) { Crunch(c, big > 1.5f ? 2 : 1, std::clamp(big, 0.4f, 2.5f)); return; }
    if (cue == CUE_GRAB) { Whoosh(c, 600, 1500, 0.2f, 0.25f); Thud(c, 160 * pf, 0.2f, 0.08f); return; } // talons closing
    switch (a) {
    case A_SQUEAK: { int n = death ? 1 : pain ? 2 : 1 + (int)(R01() * 3);
        for (int k = 0; k < n; k++) { Voice& v = Tone(c, W_SINE, 2600 * pf * up, (death ? 1500 : 3400) * pf * up, death ? 0.35f : 0.07f, 0.14f, k * 0.09f); v.vibR = 40; v.vibD = 0.04f; v.curve = 0.5f; }
        break; }
    case A_MEOW: Vox(c, 420 * pf * up, (death ? 250 : 330) * pf * up, (alarm || strike ? 0.35f : 0.6f) * (death ? 1.6f : 1), 0.18f, 500, 900, 900, 1600); if (strike) { Voice& h = Puff(c, 7000, 5000, 0.3f, 0.12f); h.hp = 2500; } break; // a hiss when it pounces
    case A_BARK: {
        if (cue == CUE_CALL && R01() < 0.5f) { Voice& g2 = Tone(c, W_SAW, 95 * pf, 85 * pf, 0.8f, 0.14f); g2.fa0 = g2.fa1 = 500; g2.tremR = 28; g2.tremD = 0.5f; break; } // a growl
        int n = death ? 1 : 1 + (int)(R01() * 2);
        for (int k = 0; k < n; k++) { Voice& v = Vox(c, 260 * pf * up, 180 * pf * up, death ? 0.8f : 0.13f, 0.28f, 700, 550, 1300, 1000, k * 0.2f); v.noiseMix = 0.3f; v.atk = 0.005f; v.decPow = 2; }
        break; }
    case A_CHATTER: for (int k = 0; k < (death ? 2 : 5); k++) Vox(c, (500 + 150 * (k % 2)) * pf * up, 420 * pf * up, 0.08f, 0.14f, 600, 800, 1000, 1400, k * 0.1f); break; // "oo-oo-aa"
    case A_HOOT: for (int k = 0; k < (death ? 1 : 2); k++) { Voice& v = Tone(c, W_SINE, 390 * pf * up, 340 * pf * up, 0.4f, 0.14f, k * 0.55f); v.atk = 0.06f; v.vibR = 5; v.vibD = 0.02f; v.noiseMix = 0.1f; v.send = 0.5f; } break;
    case A_BIRD: { int n = death ? 1 : 1 + (int)(R01() * 3);
        for (int k = 0; k < n; k++) { Voice& v = Tone(c, W_SAW, 1100 * pf * up, 1600 * pf * up, death ? 0.5f : 0.15f, 0.1f, k * 0.17f); v.fa0 = 1800 * pf; v.fa1 = 2500 * pf; v.fq = 4; v.curve = 0.5f; v.send = 0.35f; }
        break; }
    case A_BUZZ: { Voice& v = Tone(c, W_SAW, 180 * pf * up, 200 * pf * up, death ? 0.2f : 0.5f, 0.04f); v.cut0 = v.cut1 = 3000; v.tremR = 90 * pf; v.tremD = 0.6f; v.atk = 0.1f;
        if (a == A_BUZZ && pf > 1.4f && gDay < 0.5f) { Voice& cr = Tone(c, W_SINE, 4200, 4200, 0.3f, 0.03f); cr.tremR = 40; cr.tremD = 1; } // a cricket's chirp
        break; }
    case A_CLICK: { int n = strike ? 6 : death ? 3 : 3 + (int)(R01() * 4);
        for (int k = 0; k < n; k++) { Voice& v = Tone(c, W_NOISE, 100, 100, 0.012f, 0.22f, k * RR(0.03f, 0.07f)); v.hp = 1500 * pf; v.cut0 = v.cut1 = 7000; }
        if (strike && big > 0.8f) { Voice& s2 = Puff(c, 5000, 1000, 0.05f, 0.5f, 0.2f); s2.hp = 900; } // the mantis's cavitation crack
        break; }
    case A_HISS: { Voice& v = Puff(c, 7000, 4000, (strike ? 0.25f : 0.7f) * d, 0.16f); v.hp = 2500; v.atk = 0.05f; if (death) { v.dur = 1.0f; v.cut1 = 1500; } if (strike) Whoosh(c, 400, 1200, 0.2f, 0.2f); break; }
    case A_GRUNT: for (int k = 0; k < (death ? 1 : alarm || pain ? 1 : 2); k++) { Voice& v = Vox(c, (pain || death ? 380 : 120) * pf, (pain || death ? 260 : 90) * pf, death ? 0.9f : pain ? 0.35f : 0.18f, 0.25f, 600, 500, 1000, 900, k * 0.25f); v.noiseMix = 0.35f; } break; // a boar's grunt, or its squeal
    case A_CROAK: for (int k = 0; k < (death ? 1 : 3); k++) { Voice& v = Tone(c, W_SQR, 160 * pf * up, 110 * pf * up, 0.1f, 0.1f, k * 0.14f); v.cut0 = v.cut1 = 900; v.tremR = 45; v.tremD = 0.6f; } break;
    case A_BAT: for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, 5200 * pf, 3600 * pf, 0.03f, 0.07f, k * 0.06f); v.curve = 0.5f; } break;
    case A_FISH: { // fish are quiet: a flick of bubbles, a click, and the pop of a struck body
        if (death || pain) { Thud(c, 200 * pf, 0.12f); for (int k = 0; k < 3; k++) Bubble(c, k * 0.04f, 1 / pf); }
        else if (alarm) Whoosh(c, 900, 1800, 0.12f, 0.08f);
        else for (int k = 0; k < 2; k++) Bubble(c, k * 0.08f, RR(0.6f, 1.0f) / pf);
        break; }
    case A_EEL: { Voice& v = Puff(c, 900, 500, 0.4f * d, 0.12f); v.fa0 = 600; v.fa1 = 400; v.tremR = 11; v.tremD = 0.4f; if (strike) Whoosh(c, 300, 1100, 0.25f, 0.3f); if (death) { Voice& g2 = Tone(c, W_SINE, 160 * pf, 90 * pf, 0.6f, 0.1f); g2.vibR = 7; g2.vibD = 0.05f; } break; }
    case A_SHARK: { // sharks make no call: the sound is the water moving round them, and the bite
        if (strike) { Whoosh(c, 200, 700, 0.4f, 0.4f); Crunch(c, 1, std::clamp(big, 0.8f, 2.5f)); }
        else if (death || pain) { Thud(c, 60, 0.3f); for (int k = 0; k < 6; k++) Bubble(c, k * 0.06f, 1.8f); }
        else { Voice& v = Puff(c, 300, 200, 1.2f, 0.08f * std::min(2.0f, big)); v.atk = 0.4f; }
        break; }
    case A_WHALE: { // long gliding moans, singing across the water
        float f = 90 * pf * up;
        Voice& v = Tone(c, W_SINE, f, f * RR(1.3f, 1.8f), (death ? 3.5f : 2.2f) * d, 0.18f); v.atk = 0.5f; v.vibR = 4; v.vibD = 0.02f; v.curve = 1.6f; v.send = 0.8f;
        Voice& h = Tone(c, W_TRI, f * 2, f * 2.9f, 1.8f * d, 0.05f, 0.2f); h.atk = 0.5f; h.send = 0.8f;
        break; }
    case A_KRAKEN: { Voice& v = Tone(c, W_SAW, 70 * pf * up, 50 * pf * up, (death ? 2.5f : 1.2f) * d, 0.22f); v.fa0 = 380; v.fa1 = 280; v.fb0 = 800; v.fb1 = 600; v.fq = 5; v.tremR = 10; v.tremD = 0.4f; v.atk = 0.15f; v.send = 0.5f;
        for (int k = 0; k < 5; k++) Bubble(c, 0.1f + k * 0.1f, 2); if (strike) Whoosh(c, 200, 700, 0.4f, 0.3f); break; }
    case A_JELLY: { float f = 700 * pf; for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_FM, f * (k ? 1.5f : 1), f * (k ? 1.5f : 1), 1.2f, 0.035f, k * 0.2f); v.fmRatio = 3.5f; v.fmIndex = 1; v.fmIndex1 = 0; v.atk = 0.02f; v.send = 0.7f; } break; } // a glassy chime
    case A_WORM: { Voice& v = Puff(c, 220, 140, 1.5f * d, 0.25f); v.atk = 0.4f; v.tremR = 6; v.tremD = 0.5f; Voice& s2 = Tone(c, W_SINE, 38, 34, 1.5f * d, 0.2f); s2.atk = 0.4f; break; } // a rumble in the rock
    case A_HUMAN: { // mermen and Lost Ones: a voice, eerie and wordless
        float f = (a == A_HUMAN ? 150 : 150) * pf * up;
        Voice& v = Vox(c, f, f * (death ? 0.6f : strike ? 1.3f : 0.95f), (death ? 1.2f : strike ? 0.35f : 0.9f), 0.16f, 750, 500, 1150, 850); v.send = 0.55f; v.vibD = 0.03f;
        break; }
    case A_RAY: for (int k = 0; k < 8; k++) { Voice& v = Tone(c, W_NOISE, 100, 100, 0.015f, 0.2f, k * RR(0.01f, 0.05f)); v.hp = 3000; v.cut0 = v.cut1 = 9000; } { Voice& z = Tone(c, W_SQR, 60, 60, 0.3f, 0.05f); z.cut0 = z.cut1 = 2000; } break; // an electric crackle
    case A_STONE: { Voice& v = Puff(c, 600, 300, 0.5f * d, 0.2f); v.tremR = 25; v.tremD = 0.7f; if (strike) Thud(c, 70, 0.4f); if (death) for (int k = 0; k < 5; k++) Thud(c, 90, 0.2f, k * 0.08f); break; } // grinding stone and shell
    case A_PLANT: { if (strike || death) { Voice& v = Tone(c, W_SINE, 240 * pf, 90 * pf, 0.15f, 0.15f); v.curve = 0.4f; Puff(c, 2500, 800, 0.1f, 0.1f); } break; } // a pop, a snap
    }
}

void EnsureRev() {
    static bool made = false;
    if (made) return;
    made = true;
    const int CL[4] = {1116, 1188, 1277, 1356}, AL[2] = {556, 441};
    for (int k = 0; k < 4; k++) { gCombL[k].b.assign(CL[k], 0); gCombR[k].b.assign(CL[k] + 23, 0); }
    for (int k = 0; k < 2; k++) { gApL[k].b.assign(AL[k], 0); gApR[k].b.assign(AL[k] + 23, 0); }
}

void SetupBeds(int lv) { // each level's bed of sound
    gBeds.clear();
    auto add = [](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gBeds.push_back(b); };
    switch (lv) {
    case PL_PIPES: add(2, 5000, 0.7f, 0.012f, 0.1f, 0.2f, 0.07f, 0.6f); add(3, 50, 1, 0.05f, 0, 0, 0.2f, 0.2f); add(0, 250, 0.7f, 0.05f, 0.05f, 0.3f, 0.03f, 0.5f); break; // steam, the machine hum, the rumble of the ship
    case PL_HULL: add(0, 280, 0.7f, 0.12f, 0.04f, 0.4f, 0.05f, 0.5f); add(1, 900, 3, 0.012f, 0.07f, 0.5f, 0.11f, 0.8f); break;      // deep water pressing, and a far-off current
    case PL_PIRATE: add(1, 700, 1.5f, 0.07f, 0.09f, 0.6f, 0.13f, 0.7f); add(0, 400, 0.7f, 0.1f, 0.07f, 0.5f, 0.1f, 0.9f); add(2, 6000, 0.7f, 0.02f, 0.2f, 0.1f, 0.5f, 0.2f); break; // wind, the swell, the rain
    case PL_ISLAND: add(0, 500, 0.7f, 0.05f, 0.05f, 0.5f, 0.09f, 0.9f); add(1, 2500, 1.2f, 0.01f, 0.1f, 0.4f, 0.2f, 0.5f); break;    // surf, the breeze in the leaves
    case PL_CAVE: add(0, 160, 0.7f, 0.14f, 0.03f, 0.3f, 0.04f, 0.4f); add(3, 36, 1, 0.04f, 0, 0, 0.05f, 0.5f); break;               // the black water, and the drone of the rock
    case PL_WEEDS: add(0, 400, 0.7f, 0.08f, 0.06f, 0.4f, 0.07f, 0.5f); add(2, 7000, 0.7f, 0.008f, 0.15f, 0.3f, 0.3f, 0.6f); add(1, 700, 2, 0.02f, 0.05f, 0.5f, 0.09f, 0.7f); break; // sunlit water, the shimmer, the swaying current
    case PL_ATLANTIS: add(0, 200, 0.7f, 0.12f, 0.03f, 0.4f, 0.05f, 0.5f); add(3, 29, 1, 0.05f, 0, 0, 0.03f, 0.6f); add(1, 1200, 6, 0.008f, 0.02f, 0.6f, 0.04f, 0.8f); break; // deep water, a hum in the stone, a singing resonance
    default: add(0, 120, 0.7f, 0.18f, 0.02f, 0.3f, 0.03f, 0.5f); add(3, 27, 1, 0.07f, 0, 0, 0.02f, 0.7f); break; // the Abyss: pressure, and something vast and low
    }
}

// the level's little random sounds (the ambience bus), checked 20 times a second
void AmbientEvents(float dt) {
    if (gLevel < 0 || gScene < 0.05f) return;
    Ctx c{1, 1, 1, 1, B_AMB, 0.4f};
    auto pan = [&]() { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); };
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    switch (gLevel) {
    case PL_PIPES:
        if (chance(0.25f)) { pan(); c.vol = RR(0.3f, 0.9f); Voice& v = Tone(c, W_FM, RR(300, 700), 0, 0.9f, 0.04f); v.f1 = v.f0 * 0.99f; v.fmRatio = 2.76f; v.fmIndex = 2.5f; v.fmIndex1 = 0.1f; v.send = 0.7f; } // a clank somewhere in the ducts
        if (chance(0.12f)) { pan(); Voice& v = Puff(c, 6000, 3000, RR(0.6f, 1.5f), 0.05f); v.hp = 2500; v.atk = 0.1f; } // a valve venting steam
        if (chance(0.08f)) { pan(); for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, 1500, 900, 0.05f, 0.04f, k * 0.12f); v.send = 0.8f; } } // a drip on steel
        break;
    case PL_HULL: case PL_WEEDS:
        if (chance(0.6f)) { pan(); c.vol = RR(0.2f, 0.6f); for (int k = 0; k < 1 + (int)(R01() * 3); k++) Bubble(c, k * 0.06f, RR(0.6f, 1.6f)); }
        if (gLevel == PL_WEEDS && chance(0.08f)) { pan(); c.vol = 0.4f; Voice& v = Tone(c, W_TRI, RR(1500, 2500), 0, 0.15f, 0.03f); v.f1 = v.f0 * 1.2f; v.send = 0.7f; } // a click of shrimp in the kelp
        if (gLevel == PL_HULL && chance(0.05f)) { pan(); c.vol = 0.5f; Voice& v = Tone(c, W_SINE, 120, 60, 1.2f, 0.06f); v.atk = 0.2f; v.send = 0.9f; } // the hull groaning with the pressure
        break;
    case PL_PIRATE:
        if (chance(0.15f)) { pan(); c.vol = RR(0.4f, 0.8f); Voice& v = Tone(c, W_SAW, RR(90, 160), 0, RR(0.4f, 0.9f), 0.05f); v.f1 = v.f0 * RR(0.8f, 1.2f); v.fa0 = v.fa1 = RR(350, 600); v.fq = 9; v.vibR = RR(6, 14); v.vibD = 0.08f; v.atk = 0.1f; } // the timbers creak
        if (chance(0.06f)) { pan(); c.vol = 0.3f; PlaySfx(Sfx::BirdSquawk, c); }                                      // gulls
        if (chance(0.03f)) { pan(); c.vol = 0.35f; Voice& v = Puff(c, 400, 60, 3.5f, 0.3f); v.atk = 0.05f; v.decPow = 1.2f; v.send = 0.8f; } // thunder, far off
        break;
    case PL_ISLAND:
        if (gDay > 0.4f) { if (chance(0.35f)) { pan(); c.vol = RR(0.3f, 0.8f); int n = 2 + (int)(R01() * 4); float f = RR(2000, 3800); for (int k = 0; k < n; k++) { Voice& v = Tone(c, W_SINE, f, f * RR(1.1f, 1.4f), 0.07f, 0.05f, k * 0.11f); v.curve = 0.5f; } } } // birdsong by day
        else { if (chance(1.2f)) { pan(); c.vol = RR(0.2f, 0.5f); Voice& v = Tone(c, W_SINE, RR(4000, 4800), 0, 0.25f, 0.02f); v.f1 = v.f0; v.tremR = 45; v.tremD = 1; } // crickets at night
               if (chance(0.05f)) { pan(); c.vol = 0.5f; for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SINE, 420, 380, 0.35f, 0.05f, k * 0.5f); v.atk = 0.05f; } } } // an owl
        if (chance(0.1f)) { pan(); c.vol = 0.4f; Voice& v = Puff(c, 1200, 400, 1.8f, 0.07f); v.atk = 0.7f; } // a wave on the beach
        break;
    case PL_CAVE:
        if (chance(0.5f)) { pan(); c.vol = RR(0.3f, 0.9f); Voice& v = Tone(c, W_SINE, RR(700, 1600), 0, 0.06f, 0.06f); v.f1 = v.f0 * 1.8f; v.curve = 0.4f; v.send = 0.9f; } // a drip, ringing in the cavern
        if (chance(0.2f)) { pan(); c.vol = 0.3f; Bubble(c, 0, 1.4f); }
        break;
    case PL_ATLANTIS:
        if (chance(0.06f)) { pan(); c.vol = 0.5f; Voice& v = Tone(c, W_FM, RR(90, 140), 0, 3.0f, 0.05f); v.f1 = v.f0; v.fmRatio = 1.41f; v.fmIndex = 2; v.fmIndex1 = 0; v.send = 0.9f; } // a bell somewhere in the drowned city
        if (chance(0.3f)) { pan(); c.vol = 0.3f; Bubble(c, 0, 1.2f); }
        break;
    default: // the Abyss
        if (chance(0.04f)) { pan(); c.vol = 0.5f; Voice& v = Tone(c, W_SINE, RR(40, 60), 0, 4.0f, 0.12f); v.f1 = v.f0 * RR(0.6f, 1.3f); v.atk = 1.2f; v.vibR = 3; v.vibD = 0.03f; v.send = 0.9f; } // something vast, far below
        if (chance(0.1f)) { pan(); c.vol = 0.4f; Voice& v = Tone(c, W_SINE, 1800, 1700, 0.8f, 0.015f); v.send = 1; } // the pressure pinging in your helmet
        break;
    }
}

void ScoreBeat() {
    if (gLevel < 0) return;
    const Score& s = SCORES[gLevel];
    float beat = 60.0f / s.bpm;
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f};
    if (gBeat % 8 == 0) { // a new chord: I, IV, V or vi of the level's scale, slow to swell and slow to fade
        static const int CH[4] = {0, 3, 4, 5};
        gChord = CH[(int)(R01() * 4)] % s.n;
        int deg[3] = {gChord, gChord + 2, gChord + 4};
        for (int k = 0; k < 3; k++) {
            PanGains(-0.5f + k * 0.5f, c.gl, c.gr);
            Voice& v = Tone(c, s.padWave, Note(s, deg[k], 1), 0, beat * 10, s.padGain);
            v.f1 = v.f0; v.atk = beat * 2.5f; v.hold = beat * 4; v.decPow = 1; v.det = s.padDet; v.cut0 = v.cut1 = s.padCut; v.send = 0.6f;
            if (gLevel == PL_ATLANTIS) { v.fa0 = v.fa1 = 750; v.fb0 = v.fb1 = 1150; v.fq = 5; v.vibR = 5; v.vibD = 0.008f; v.gain *= 2.2f; } // a drowned choir: "aah"
            if (gLevel == PL_PIRATE) { v.tremR = 5.5f; v.tremD = 0.25f; } // the squeezebox's bellows
        }
        PanGains(0, c.gl, c.gr);
        Voice& b = Tone(c, W_SINE, Note(s, gChord, 0), 0, beat * 9, s.bassGain);
        b.f1 = b.f0; b.atk = beat; b.hold = beat * 5; b.decPow = 1; b.send = 0.3f;
        if (gLevel == PL_COUNT) { Voice& cl = Tone(c, W_SINE, Note(s, gChord + 1, 1), 0, beat * 9, s.padGain * 0.5f); cl.f1 = cl.f0 * 1.01f; cl.atk = beat * 3; cl.send = 0.9f; } // the Abyss's cluster: a second that never resolves
    }
    // the lead: sparse, walking the scale
    float prob = s.leadProb * (gLevel == PL_COUNT ? 0.3f : 1);
    if (R01() < prob) {
        int step = (int)(R01() * 5) - 2; if (step == 0) step = 1;
        gLeadDeg = std::clamp(gLeadDeg + step, s.n, s.n * 3);
        if (R01() < 0.3f) gLeadDeg = gChord + s.n * (1 + (int)(R01() * 2)); // lean back to the chord
        PanGains(RR(-0.4f, 0.4f), c.gl, c.gr);
        float f = Note(s, gLeadDeg, s.leadOct - 1);
        Voice& v = Tone(c, s.leadWave, f, f, beat * s.leadBeats, s.leadGain);
        v.cut0 = v.cut1 = s.leadCut; v.send = 0.55f;
        switch (gLevel) {
        case PL_PIPES: v.fmRatio = s.leadFm; v.fmIndex = 2.2f; v.fmIndex1 = 0.1f; v.atk = 0.003f; v.decPow = 2.2f; break; // struck steel
        case PL_HULL: v.atk = beat * 0.8f; v.vibR = 4; v.vibD = 0.012f; v.f1 = f * (R01() < 0.3f ? 1.06f : 1.0f); break; // a slow, singing line
        case PL_PIRATE: v.atk = 0.08f; v.vibR = 5.5f; v.vibD = 0.012f; v.fa0 = v.fa1 = 1100; v.fb0 = v.fb1 = 2400; v.fq = 3; v.gain *= 1.3f; break; // a fiddle
        case PL_ISLAND:
            if (gDay > 0.4f) { v.wave = W_FM; v.fmRatio = 4; v.fmIndex = 1.8f; v.fmIndex1 = 0; v.atk = 0.002f; v.decPow = 3; v.dur = beat * 0.9f; } // a marimba by day
            else { v.wave = W_SINE; v.noiseMix = 0.12f; v.atk = 0.12f; v.vibR = 5; v.vibD = 0.01f; v.f0 = v.f1 = f * 2; } // a wooden flute by night
            break;
        case PL_CAVE: v.wave = W_FM; v.fmRatio = 3; v.fmIndex = 1.2f; v.fmIndex1 = 0; v.atk = 0.002f; v.decPow = 2.5f; v.send = 0.9f; v.f0 = v.f1 = f * 2; break; // a tuned drip
        case PL_WEEDS: v.atk = 0.002f; v.decPow = 2.6f; v.dur = beat * 1.6f; v.cut0 = 4000; v.cut1 = 800; break; // a harp
        case PL_ATLANTIS: v.fmRatio = s.leadFm; v.fmIndex = 3; v.fmIndex1 = 0.2f; v.atk = 0.002f; v.decPow = 1.4f; v.send = 0.85f; break; // a deep bell
        default: v.atk = beat; v.vibR = 3; v.vibD = 0.03f; v.f1 = f * 0.8f; v.send = 0.95f; break; // a groan from the dark
        }
    }
    // the level's own pulse
    if (gLevel == PL_ISLAND && (gBeat % 4 == 0 || gBeat % 4 == 3)) { PanGains(0.2f, c.gl, c.gr); Voice& d = Tone(c, W_SINE, 150, 70, 0.3f, 0.07f); d.curve = 0.4f; d.decPow = 2.5f; d.send = 0.2f; } // a log drum, soft
    if (gLevel == PL_PIPES && gBeat % 16 == 8 && R01() < 0.6f) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& d = Tone(c, W_FM, 110, 110, 1.6f, 0.05f); d.fmRatio = 2.76f; d.fmIndex = 3; d.fmIndex1 = 0.3f; d.send = 0.8f; } // the ship's heart
    if (gLevel == PL_COUNT && gBeat % 12 == 0) { Voice& d = Tone(c, W_SINE, 45, 38, 0.5f, 0.1f); d.curve = 0.5f; Voice& d2 = Tone(c, W_SINE, 45, 38, 0.5f, 0.07f, 0.35f); d2.curve = 0.5f; } // a slow heartbeat
}

void Render(float* out, int frames) {
    EnsureRev();
    const float dtS = 1.0f / SR;
    float muL = 0, muR = 0; (void)muL; (void)muR;
    for (int base = 0; base < frames; base += CTRL) {
        int n = std::min(CTRL, frames - base);
        float blockT = n * dtS;
        // ---- control rate: scene fade, the score's clock, ambient events, loops
        gClock += blockT;
        gScene += (gSceneTarget - gScene) * std::min(1.0f, blockT * 0.8f);
        if (gLevel >= 0 && gSceneTarget > 0) {
            gBeatT += blockT;
            float beat = 60.0f / SCORES[gLevel].bpm;
            if (gLevel == PL_PIRATE && gBeat % 2 == 1) beat *= 1.25f; // a lilt
            if (gBeatT >= beat) { gBeatT -= beat; ScoreBeat(); gBeat++; }
            gAmbT += blockT;
            if (gAmbT >= 0.05f) { AmbientEvents(gAmbT); gAmbT = 0; }
        }
        gFlowS += (gFlow - gFlowS) * std::min(1.0f, blockT * 3);
        gSlideS += (gSlide - gSlideS) * std::min(1.0f, blockT * 12);
        gFlowF.Set(2, (gUnderwater ? 350 : 600) + 500 * gFlowS + 150 * sinf(gClock * 0.7f), 1.2f);
        gSlideF.Set(1, 900, 0.7f); gSlideF2.Set(0, 3500, 0.7f);
        float uwCut = gUnderwater ? 2600.0f : 18000.0f;
        gUwL.Set(0, uwCut, 0.707f); gUwR.Set(0, uwCut, 0.707f);
        for (auto& v : gV) {
            if (!v.on || v.delay > 0) continue;
            float u = v.t / v.dur;
            if (u >= 1) { v.on = false; continue; }
            v.fcur = v.f0 * powf(std::max(0.01f, v.f1 / std::max(1.0f, v.f0)), powf(u, v.curve));
            if (v.cut0 > 0) v.lp.Set(0, v.cut0 * powf(std::max(0.01f, v.cut1 / v.cut0), u), v.q);
            if (v.hp > 0) v.hpf.Set(1, v.hp, 0.707f);
            if (v.fa0 > 0) v.fa.Set(2, v.fa0 + (v.fa1 - v.fa0) * u, v.fq);
            if (v.fb0 > 0) v.fb.Set(2, v.fb0 + (v.fb1 - v.fb0) * u, v.fq);
            v.fmi = v.fmIndex1 >= 0 ? v.fmIndex + (v.fmIndex1 - v.fmIndex) * u : v.fmIndex;
            v.env0 = EnvAt(v, v.t); v.env1 = EnvAt(v, v.t + blockT);
        }
        float busG[3] = {gVol.sfx, gVol.music * gScene, gVol.ambience * gScene};
        for (int i = 0; i < n; i++) {
            float sL = 0, sR = 0, mL = 0, mR = 0, send = 0;
            float fi = (float)i / n;
            for (auto& v : gV) {
                if (!v.on) continue;
                if (v.delay > 0) { v.delay -= dtS; continue; }
                float f = v.fcur;
                if (v.vibD > 0) f *= 1 + v.vibD * sinf(TAU * v.vibR * v.t);
                v.ph += f * dtS; if (v.ph >= 1) v.ph -= 1;
                float s;
                switch (v.wave) {
                case W_SINE: s = sinf(TAU * v.ph); break;
                case W_TRI: s = 1 - 4 * fabsf(v.ph - 0.5f); break;
                case W_SAW: s = 2 * v.ph - 1; break;
                case W_SQR: s = v.ph < 0.5f ? 0.7f : -0.7f; break;
                case W_NOISE: s = Noise(); break;
                default: v.ph2 += f * v.fmRatio * dtS; if (v.ph2 >= 1) v.ph2 -= 1; s = sinf(TAU * v.ph + v.fmi * sinf(TAU * v.ph2)); break;
                }
                if (v.det > 0) { v.ph3 += f * (1 + v.det) * dtS; if (v.ph3 >= 1) v.ph3 -= 1; s = 0.6f * s + 0.6f * (v.wave == W_SAW ? 2 * v.ph3 - 1 : v.wave == W_TRI ? 1 - 4 * fabsf(v.ph3 - 0.5f) : sinf(TAU * v.ph3)); }
                if (v.noiseMix > 0) s = s * (1 - v.noiseMix) + Noise() * v.noiseMix;
                if (v.fa0 > 0) { float a = v.fa.Run(s); s = v.fb0 > 0 ? (a + v.fb.Run(s) * 0.7f) * 2.2f : a * 2.2f; }
                if (v.cut0 > 0) s = v.lp.Run(s);
                if (v.hp > 0) s = v.hpf.Run(s);
                float e = v.env0 + (v.env1 - v.env0) * fi;
                if (v.tremD > 0) e *= 1 - v.tremD * (0.5f + 0.5f * sinf(TAU * v.tremR * v.t));
                s *= e * v.gain * busG[v.bus];
                if (v.bus == B_SFX) { sL += s * v.gl; sR += s * v.gr; }
                else { mL += s * v.gl; mR += s * v.gr; }
                send += s * v.send;
                v.t += dtS;
            }
            // the ambience bed
            for (auto& b : gBeds) {
                float tt = gClock + i * dtS;
                float s;
                if (b.type == 3) { b.ph += b.f * dtS; if (b.ph >= 1) b.ph -= 1; s = sinf(TAU * b.ph) + 0.5f * sinf(TAU * b.ph * 2) + 0.25f * sinf(TAU * b.ph * 3); }
                else {
                    if (((i + base) & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
                    s = b.flt.Run(Noise());
                }
                float amp = b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
                if (gLevel == PL_ISLAND && b.type == 1) amp *= 0.5f + gDay; // the breeze drops at night
                s *= amp * gVol.ambience * gScene;
                mL += s; mR += s * (b.type == 1 ? 0.8f : 1.0f);
                send += s * 0.3f;
            }
            // the draught or current around the diver, and a slide's scrape
            if (gFlowS > 0.01f) { float s = gFlowF.Run(Noise()) * gFlowS * 0.18f * gVol.sfx; sL += s; sR += s; }
            if (gSlideS > 0.01f) { float s = gSlideF2.Run(gSlideF.Run(Noise())) * gSlideS * 0.1f * gVol.sfx; sL += s; sR += s; }
            // effects are muffled under water
            sL = gUwL.Run(sL); sR = gUwR.Run(sR);
            // the reverb
            float rin = send * 0.25f, rL = 0, rR = 0;
            for (int k = 0; k < 4; k++) { rL += gCombL[k].Run(rin, gRevFb, gRevDamp); rR += gCombR[k].Run(rin, gRevFb, gRevDamp); }
            for (int k = 0; k < 2; k++) { rL = gApL[k].Run(rL); rR = gApR[k].Run(rR); }
            float L = (sL + mL + rL * gRevWet) * gVol.master, R = (sR + mR + rR * gRevWet) * gVol.master;
            out[(base + i) * 2] = tanhf(L);
            out[(base + i) * 2 + 1] = tanhf(R);
        }
    }
}

} // namespace

AudioVolumes& Volumes() { return gVol; }

void AudioInit() {
    if (!IsAudioDeviceReady() || gReady) return;
    SetAudioStreamBufferSizeDefault(BLOCK);
    gStream = LoadAudioStream(SR, 32, 2);
    if (!IsAudioStreamValid(gStream)) return;
    PlayAudioStream(gStream);
    gReady = true;
}
void AudioClose() { if (gReady) { UnloadAudioStream(gStream); gReady = false; } }

void AudioFrame(float dt, bool parkour) {
    (void)dt;
    if (!gReady) return;
    gSceneTarget = parkour && gLevel >= 0 ? 1.0f : 0.0f;
    if (!parkour) { gFlow = 0; gSlide = 0; }
    gCallCool = std::max(0.0f, gCallCool - dt);
    int guard = 0;
    while (IsAudioStreamProcessed(gStream) && guard++ < 4) {
        Render(gBuf, BLOCK);
        UpdateAudioStream(gStream, gBuf, BLOCK);
    }
}

void AudioLevel(int level) {
    if (level == gLevel) return;
    gLevel = level;
    if (level < 0) return;
    gUnderwater = level == PL_HULL || level == PL_CAVE || level == PL_WEEDS || level == PL_ATLANTIS || level == PL_COUNT;
    const Score& s = SCORES[level];
    gRevFb = s.rev; gRevDamp = s.damp; gRevWet = s.wet;
    gBeat = 0; gBeatT = 0; gLeadDeg = s.n * 2;
    for (auto& v : gV) if (v.bus != B_SFX) v.on = false;
    SetupBeds(level);
}
void AudioListener(Vector2 at) { gEar = at; }
void AudioDay(float d) { gDay = d; }
void AudioFlow(float a) { gFlow = std::clamp(a, 0.0f, 1.0f); }
void AudioSlide(float a) { gSlide = std::clamp(a, 0.0f, 1.0f); }

static bool Place(Vector2 at, float vol, Ctx& c, float range = 1100) {
    float dx = at.x - gEar.x, dy = at.y - gEar.y, d = sqrtf(dx * dx + dy * dy);
    float att = std::clamp(1 - d / range, 0.0f, 1.0f);
    att *= att;
    if (att * vol < 0.02f) return false;
    c.vol = vol * att;
    PanGains(dx / 700, c.gl, c.gr);
    return true;
}
void SfxAt(Sfx s, Vector2 at, float vol, float pitch) {
    if (!gReady) return;
    Ctx c{vol, pitch, 1, 1, B_SFX, 0.25f};
    if (!Place(at, vol, c)) return;
    PlaySfx(s, c);
}
void Sfx2D(Sfx s, float vol, float pitch, float pan) {
    if (!gReady) return;
    Ctx c{vol, pitch, 1, 1, B_SFX, 0.25f};
    PanGains(pan, c.gl, c.gr);
    PlaySfx(s, c);
}
void BeastSound(const char* name, float size, int cue, Vector2 at, float vol) {
    if (!gReady || !name) return;
    if (cue == CUE_CALL) { if (gCallCool > 0) return; gCallCool = 0.18f; } // a crowd doesn't all call at once
    Arch a = ArchOf(name);
    float h = Hash01(name, 7);
    float pf = std::clamp(powf(std::max(0.2f, size), -0.5f), 0.35f, 2.2f) * (0.82f + 0.36f * h); // bigger is lower; each species its own pitch
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f};
    float louder = cue == CUE_CALL ? 0.6f : 1.0f;
    if (!Place(at, vol * louder * std::clamp(0.6f + size * 0.4f, 0.6f, 1.6f), c, 900 + size * 400)) return;
    PlayBeast(a, cue, pf, size, c);
}
float BeastCallRate(const char* name) {
    switch (ArchOf(name)) {
    case A_PLANT: case A_SHARK: return 0;
    case A_BIRD: case A_BUZZ: case A_SQUEAK: case A_CHATTER: return 0.06f;
    case A_CROAK: case A_BAT: case A_CLICK: return 0.05f;
    case A_WHALE: case A_KRAKEN: case A_WORM: return 0.025f;
    case A_JELLY: case A_FISH: return 0.02f;
    default: return 0.035f;
    }
}
bool BeastIsSilent(const char* name) { return ArchOf(name) == A_PLANT; }

void AbyssSound(int kind, int cue, float dist, float pan) {
    if (!gReady) return;
    float att = std::clamp(1 - dist / 30.0f, 0.0f, 1.0f); att *= att;
    if (att < 0.02f) return;
    Ctx c{att, 1, 1, 1, B_SFX, 0.6f};
    PanGains(pan, c.gl, c.gr);
    switch ((AbyssCreatureKind)kind) {
    case AbyssCreatureKind::GiantIsopod: case AbyssCreatureKind::WhaleFall: PlayBeast(A_CLICK, cue, 0.6f, 1.5f, c); break;
    case AbyssCreatureKind::GulperEel: case AbyssCreatureKind::TrenchWorm: case AbyssCreatureKind::SlimeHagfish: PlayBeast(A_EEL, cue, 0.7f, 1.5f, c); break;
    case AbyssCreatureKind::VampireSquid: case AbyssCreatureKind::AnglerCephalopod: PlayBeast(A_KRAKEN, cue, 1.2f, 1.0f, c); break;
    case AbyssCreatureKind::Leviathan: PlayBeast(A_WHALE, cue, 0.4f, 4.0f, c); break;
    case AbyssCreatureKind::TrenchMaw: PlayBeast(A_KRAKEN, cue, 0.35f, 5.0f, c); break;
    case AbyssCreatureKind::PressureGhost: { Voice& v = Tone(c, W_NOISE, 100, 100, 1.5f, 0.3f); v.fa0 = 200; v.fa1 = 700; v.fq = 3; v.atk = 1.0f; break; } // the long inhale
    case AbyssCreatureKind::GlassSponge: if (cue == CUE_DEATH) for (int k = 0; k < 8; k++) { Voice& v = Tone(c, W_SINE, RR(2500, 5000), 0, 0.3f, 0.06f, k * 0.03f); v.f1 = v.f0; v.send = 0.9f; } break; // shattering glass
    case AbyssCreatureKind::PressureBulb: PlaySfx(Sfx::Blast, c); break;
    default: PlayBeast(A_JELLY, cue, 1.0f, 0.5f, c); break;
    }
}

// depth.exe --audio-test [out.wav]: render every level's score and ambience with a string of effects and beast
// voices over it, offline, and report the levels (peak, loudness, anything broken) and how fast it renders.
bool AudioSelfTest(const char* wavPath) {
    gReady = true; // no device: render by hand
    std::vector<float> all;
    bool ok = true;
    const char* beasts[] = {"Bilge Rat", "Ship's Cat", "Guard Dog", "Barn Owl", "Gull", "Wild Boar", "Tree Snake", "Dart Frog", "Fruit Bat", "Leviathan Tiger Shark", "Hull-Grazer Whale",
                            "Grand Kraken", "Merman", "Electric Ray", "Phalanx Crustacean", "Glow Jelly", "Tremor Worm", "Pistol Shrimp", "Powder Monkey", "Barracuda", "Tribal Drum-Fungus"};
    double renderMs = 0; int renderedFrames = 0;
    for (int lv = 0; lv <= PL_COUNT; lv++) {
        AudioLevel(lv); gSceneTarget = 1; gScene = 1; gDay = lv == PL_ISLAND ? 0.2f : 1.0f; gEar = {0, 0};
        const int SECS = 8, N = SR * SECS;
        std::vector<float> buf(N * 2);
        int fired = 0;
        for (int at = 0; at < N; at += BLOCK) {
            float tsec = (float)at / SR;
            if (tsec > 1 + fired * 0.35f && fired < (int)Sfx::COUNT) { Sfx2D((Sfx)fired, 0.9f, 1, RR(-0.6f, 0.6f)); fired++; }
            if (((at / BLOCK) % 9) == 0) { const char* b = beasts[(at / BLOCK / 9) % 21]; BeastSound(b, RR(0.5f, 3.0f), (at / BLOCK / 9) % 7, {RR(-400, 400), RR(-200, 200)}); }
            if (lv == PL_PIRATE) { AudioFlow(0.5f); AudioSlide(tsec > 4 && tsec < 5 ? 1.0f : 0.0f); }
            auto t0 = std::chrono::steady_clock::now();
            Render(&buf[at * 2], std::min(BLOCK, N - at));
            renderMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); renderedFrames += std::min(BLOCK, N - at);
        }
        double sum = 0; float peak = 0; int bad = 0, clip = 0;
        for (float x : buf) { if (!std::isfinite(x)) { bad++; continue; } sum += x * x; peak = std::max(peak, fabsf(x)); if (fabsf(x) > 0.97f) clip++; }
        float rms = sqrtf((float)(sum / buf.size()));
        printf("%-16s peak %.2f  rms %.3f  (%.1f dB)  clipped %d  broken %d\n", lv < PL_COUNT ? TextFormat("level %d", lv) : "the Abyss", peak, rms, 20 * log10f(std::max(1e-6f, rms)), clip, bad);
        if (bad > 0 || rms < 0.005f || rms > 0.5f) ok = false;
        all.insert(all.end(), buf.begin(), buf.end());
        for (auto& v : gV) v.on = false;
    }
    printf("render speed: %.1fx real time\n", (renderedFrames / (double)SR) / (renderMs / 1000.0));
    // every effect, and every beast voice for every cue, alone: none may come out silent or broken
    AudioLevel(PL_PIRATE); gSceneTarget = 0; gScene = 0;
    auto solo = [&](auto fire) { for (auto& v : gV) v.on = false; fire(); std::vector<float> b(SR * 2 * 2); for (int at = 0; at < SR * 2; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, SR * 2 - at)); float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else pk = std::max(pk, fabsf(x)); } return bad ? -1.0f : pk; };
    int silent = 0;
    for (int k = 0; k < (int)Sfx::COUNT; k++) { float pk = solo([&] { Sfx2D((Sfx)k); }); if (pk < 0.02f && (Sfx)k != Sfx::SoftLand) { printf("  effect %d is silent or broken (peak %.3f)\n", k, pk); silent++; } }
    for (const char* b : beasts) for (int cue = 0; cue < 7; cue++) { float pk = solo([&] { gCallCool = 0; BeastSound(b, 1.0f, cue, {0, 0}); }); if (pk < 0.01f && !(cue <= CUE_ALARM && strstr(b, "Fungus")) && !(cue == CUE_CALL && strstr(b, "Shark"))) { printf("  %s cue %d is silent or broken (peak %.3f)\n", b, cue, pk); silent++; } }
    printf("%d silent or broken voices\n", silent);
    if (silent) ok = false;
    if (wavPath) {
        std::vector<short> pcm(all.size());
        for (size_t i = 0; i < all.size(); i++) pcm[i] = (short)(std::clamp(all[i], -1.0f, 1.0f) * 32000);
        ::Wave w{(unsigned)(all.size() / 2), SR, 16, 2, pcm.data()};
        ExportWave(w, wavPath);
    }
    gReady = false;
    return ok;
}