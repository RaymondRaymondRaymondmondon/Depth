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
#include "study_audio.h"
#include "study_data.h"
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

enum Osc { W_SINE, W_TRI, W_SAW, W_SQR, W_NOISE, W_FM, W_PLUCK };
enum Bus { B_SFX, B_MUSIC, B_AMB, B_UI, B_VOICE };   // the same order as CueBus

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
    int cue = -1, inst = 0, prio = 0;         // which registered cue (and which playing of it) this voice belongs to
    int ksLen = 0, ksI = 0;                   // a plucked string (Karplus-Strong): its delay line lives in gKs
    float ksDecay = 0.996f;
};
Voice gV[128];
float gKs[128][1024];                        // the plucked strings' delay lines, one per voice slot (kept out of Voice so it stays small)
int gTagCue = -1, gTagInst = 0, gTagPrio = 0; // voices started while a cue is being built are tagged with it
int gNextInst = 1;
float gDuck = 0, gVoiceS = 0;                // music ducking (combat impacts), and how much a voice is speaking

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
    gV[pick].cue = gTagCue; gV[pick].inst = gTagInst; gV[pick].prio = gTagPrio;
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
int gLevel = -1;
bool gUnderwater = false;
float gScene = 0, gSceneTarget = 0; // the score and ambience fade in and out with the parkour section

float gDay = 1, gFlow = 0, gFlowS = 0, gSlide = 0, gSlideS = 0, gTension = 0, gTensionS = 0;
Vector2 gEar{0, 0};
float gCallCool = 0;

struct Bed { int type; float f, q, gain, lfoR, lfoD, ampR, ampD; Biquad flt; float ph = 0, ph2 = 0; };
std::vector<Bed> gBeds;
Biquad gFlowF, gSlideF, gSlideF2, gUwL, gUwR;
float gAmbT = 0, gClock = 0;



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
        if (c.pitch > 1.3f) { Voice& r = Tone(c, W_FM, 420, 415, 0.12f, 0.015f); r.fmRatio = 2.7f; r.fmIndex = 0.8f; r.fmIndex1 = 0.1f; } // pipe steel rings, low and short
        break; }
    case Sfx::WallJump: { Voice& v = Puff(c, 1800, 1200, 0.07f, 0.15f); v.fa0 = 1500; v.fa1 = 1100; PlaySfx(Sfx::Jump, c); break; }
    case Sfx::Dash: Whoosh(c, uw ? 300 : 500, uw ? 1400 : 2600, 0.24f, 0.38f); if (uw) for (int k = 0; k < 4; k++) Bubble(c, 0.05f + k * 0.03f, RR(0.7f, 1.2f)); break;
    case Sfx::Roll: for (int k = 0; k < 3; k++) Thud(c, 110 - k * 10, 0.15f, k * 0.09f); Whoosh(c, 400, 900, 0.25f, 0.12f); break;
    case Sfx::Stun: Thud(c, 60, 0.4f); Vox(c, 160, 110, 0.3f, 0.1f, 700, 450, 1150, 850, 0.05f); break;
    case Sfx::Grab: Puff(c, 3000, 1500, 0.05f, 0.12f).hp = 900; Thud(c, 130, 0.12f, 0.02f); break;
    case Sfx::Backflip: Whoosh(c, 700, 2000, 0.3f, 0.22f); break;
    case Sfx::Glide: Whoosh(c, 300, 500, 0.5f, 0.1f); break;
    case Sfx::Splash: {
        Voice& v = Puff(c, 4000, 500, 0.6f, 0.4f); v.atk = 0.01f; v.decPow = 1.4f;
        for (int k = 0; k < 8; k++) Bubble(c, 0.1f + k * 0.05f, RR(0.6f, 1.6f));
        break; }
    case Sfx::Wade: { Voice& v = Puff(c, 2400, 900, 0.15f, 0.08f); v.fa0 = 1200; v.fa1 = 800; break; }
    case Sfx::Death: { // (the user found the cry annoying) a soft falling tone and a muffled thud
        Voice& v = Tone(c, W_SINE, 330, 150, 0.9f, 0.12f); v.decPow = 1.5f; v.send = 0.6f; v.curve = 0.7f;
        Thud(c, 55, 0.25f, 0.04f);
        break; }
    case Sfx::DeathWater: {
        Voice& v = Tone(c, W_SINE, 300, 140, 1.0f, 0.1f); v.decPow = 1.5f; v.send = 0.7f; v.curve = 0.7f;
        for (int k = 0; k < 6; k++) Bubble(c, 0.05f + k * 0.07f, RR(0.9f, 1.8f));
        break; }
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
    case A_JELLY: { float f = 260 * pf; for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SINE, f * (k ? 1.5f : 1), f * (k ? 1.5f : 1), 1.0f, 0.02f, k * 0.2f); v.atk = 0.1f; v.send = 0.7f; } break; } // a soft low hum (the high chime was grating)
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
        if (chance(0.025f)) { pan(); c.vol = RR(0.3f, 0.7f); Voice& v = Tone(c, W_FM, RR(200, 400), 0, 0.9f, 0.03f); v.f1 = v.f0 * 0.99f; v.fmRatio = 2.76f; v.fmIndex = 2.5f; v.fmIndex1 = 0.1f; v.send = 0.7f; } // a clank somewhere in the ducts
        if (chance(0.12f)) { pan(); Voice& v = Puff(c, 6000, 3000, RR(0.6f, 1.5f), 0.05f); v.hp = 2500; v.atk = 0.1f; } // a valve venting steam
        if (chance(0.05f)) { pan(); for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, 800, 500, 0.05f, 0.025f, k * 0.12f); v.send = 0.8f; } } // a drip on steel
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

        break;
    }
}

// ---------------------------------------------------------------- the score
// Each level has a long, evolving synth track (the user: "evolving sounds that build on top of each other ... a very
// long track for each level"). A Song is a scale, a tempo, chord progressions and a set of Layers - drone, pads,
// arpeggios, bass, a lead that develops a motif (or, on the Pirate Ship, plays a composed shanty), drums, chants,
// rare accents. The track runs through twelve sections of sixteen bars (8-12 minutes); each layer is active in some
// sections only and fades in and out over four bars, the arpeggios slowly mutate, the filters open and close with
// the section's intensity, and an apex nearby pushes the intensity up. After the twelfth section it goes round
// again with the progressions, motifs and patterns re-rolled, so it never repeats exactly.
enum Inst { I_PAD, I_WARM_PAD, I_CHOIR, I_ORGAN, I_ARP_SAW, I_ARP_SQR, I_PLUCK, I_BELL, I_MARIMBA, I_FLUTE, I_FIDDLE, I_SYNTH_LEAD,
            I_SUB_BASS, I_SAW_BASS, I_DRONE, I_KICK, I_SNARE, I_HAT, I_LOGDRUM, I_HANDDRUM, I_SHAKER, I_CLANK, I_GLASS, I_WHALE,
            I_SQUEEZE, I_CHUFF };
enum Role { R_DRONE, R_PAD, R_ARP, R_BASS, R_LEAD, R_DRUM, R_CHANT, R_STAB, R_ACCENT };
struct Layer {
    int role = R_PAD, inst = I_PAD, altInst = -1; // altInst: played instead at night (the Island's flute)
    float gain = 0.03f; uint32_t mask = 0xFFF; int oct = 1; float pan = 0;
    int pat[16] = {}, base[16] = {}, pit[16] = {}; int steps = 16;
    int barMod = 0;                     // chants: 0 every bar, 1 only even bars (the call), 2 only odd bars (the response)
    bool mutate = false, echo = false, absolute = false; // absolute: a composed melody in scale degrees, not relative to the chord
    float accentProb = 0;               // accents: chance per bar
    std::vector<std::pair<int, int>> motif; // lead: (degree, ticks); -99 = rest
    int phrase = -1, nextTick = 0, phraseBase = 0;
    float cur = 0;
};
struct Song {
    float root = 65.41f; int scale[8] = {0, 2, 3, 5, 7, 8, 10}; int n = 7;
    float bpm = 60; int tpb = 16, tpbeat = 4, barsPerSection = 16, sections = 12;
    int progA[16] = {0}, nA = 1, progB[16] = {0}, nB = 1, chordBars = 2; uint32_t sectionB = 0;
    std::vector<Layer> layers;
    float rev = 0.85f, damp = 0.35f, wet = 0.3f;
};
Song gSong;
int gTick = 0; float gTickT = 0;
const float ARC[12] = {0.12f, 0.22f, 0.32f, 0.42f, 0.52f, 0.48f, 0.62f, 0.76f, 0.88f, 0.7f, 0.45f, 0.25f}; // the track's rise and fall

float Note(const Song& s, int deg, int oct) { // a scale degree (any integer) to Hz
    int n = s.n, o = (deg >= 0 ? deg / n : -((-deg + n - 1) / n)), d = deg - o * n;
    return s.root * powf(2.0f, oct + o + s.scale[d] / 12.0f);
}
float TickDur() { return 60.0f / gSong.bpm / gSong.tpbeat; }

void PlayInst(int inst, float f, float dur, float gain, float pan, float bright, bool echo = false) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f};
    PanGains(pan, c.gl, c.gr);
    auto make = [&](float delay, float g) -> Voice& {
        switch (inst) {
        case I_PAD: { Voice& v = Tone(c, W_SAW, f, f, dur, g, delay); v.det = 0.006f; v.atk = dur * 0.3f; v.hold = dur * 0.3f; v.decPow = 1; v.cut0 = v.cut1 = 450 + 2200 * bright; v.send = 0.6f; return v; }
        case I_WARM_PAD: { Voice& v = Tone(c, W_TRI, f, f, dur, g, delay); v.det = 0.007f; v.atk = dur * 0.35f; v.hold = dur * 0.25f; v.decPow = 1; v.cut0 = v.cut1 = 700 + 1800 * bright; v.send = 0.65f; return v; }
        case I_CHOIR: { Voice& v = Tone(c, W_SAW, f, f, dur, g * 2.2f, delay); v.det = 0.005f; v.atk = dur * 0.3f; v.hold = dur * 0.3f; v.decPow = 1; v.fa0 = v.fa1 = 750; v.fb0 = v.fb1 = 1150; v.fq = 5; v.vibR = 5; v.vibD = 0.008f; v.send = 0.7f; return v; }
        case I_ORGAN: { Voice& v = Tone(c, W_FM, f, f, dur, g, delay); v.fmRatio = 2; v.fmIndex = 0.7f; v.atk = 0.08f; v.hold = dur * 0.6f; v.decPow = 1; v.send = 0.6f; return v; }
        case I_ARP_SAW: case I_ARP_SQR: { Voice& v = Tone(c, inst == I_ARP_SAW ? W_SAW : W_SQR, f, f, dur, g, delay); v.atk = 0.004f; v.decPow = 2; v.cut0 = 500 + 3500 * bright; v.cut1 = v.cut0 * 0.35f; v.q = 1.4f; v.send = 0.45f; return v; }
        case I_PLUCK: { Voice& v = Tone(c, W_SAW, f, f, dur, g, delay); v.atk = 0.002f; v.decPow = 2.6f; v.cut0 = 1800 + 2500 * bright; v.cut1 = 400; v.send = 0.55f; return v; }
        case I_BELL: { Voice& v = Tone(c, W_FM, f, f, dur, g, delay); v.fmRatio = 3.5f; v.fmIndex = 2.2f; v.fmIndex1 = 0.1f; v.atk = 0.002f; v.decPow = 1.6f; v.send = 0.7f; return v; }
        case I_MARIMBA: { Voice& v = Tone(c, W_FM, f, f, std::min(dur, 0.6f), g, delay); v.fmRatio = 4; v.fmIndex = 1.8f; v.fmIndex1 = 0; v.atk = 0.002f; v.decPow = 3; v.send = 0.35f; return v; }
        case I_FLUTE: { Voice& v = Tone(c, W_SINE, f, f, dur, g, delay); v.noiseMix = 0.1f; v.atk = 0.09f; v.hold = dur * 0.4f; v.vibR = 5; v.vibD = 0.01f; v.send = 0.55f; return v; }
        case I_FIDDLE: { Voice& v = Tone(c, W_SAW, f, f, dur, g, delay); v.atk = 0.05f; v.hold = dur * 0.5f; v.vibR = 5.5f; v.vibD = 0.012f; v.fa0 = v.fa1 = 1100; v.fb0 = v.fb1 = 2400; v.fq = 3; v.send = 0.45f; return v; }
        case I_SYNTH_LEAD: { Voice& v = Tone(c, W_SAW, f * 0.985f, f, dur, g, delay); v.curve = 0.08f; v.det = 0.004f; v.atk = 0.015f; v.hold = dur * 0.55f; v.decPow = 1.4f; v.cut0 = 900 + 2600 * bright; v.cut1 = v.cut0 * 0.7f; v.vibR = 5; v.vibD = 0.006f; v.send = 0.45f; return v; }
        case I_SUB_BASS: { Voice& v = Tone(c, W_SINE, f, f, dur, g, delay); v.atk = 0.01f; v.hold = dur * 0.5f; v.decPow = 1.2f; v.send = 0.15f; return v; }
        case I_SAW_BASS: { Voice& v = Tone(c, W_SAW, f, f, dur, g, delay); v.atk = 0.004f; v.decPow = 1.5f; v.cut0 = 260 + 900 * bright; v.cut1 = 180; v.q = 1.5f; v.send = 0.12f; return v; }
        case I_DRONE: { Voice& v = Tone(c, W_TRI, f, f, dur, g, delay); v.det = 0.003f; v.atk = dur * 0.4f; v.hold = dur * 0.3f; v.decPow = 1; v.cut0 = v.cut1 = 350 + 700 * bright; v.send = 0.5f; return v; }
        case I_KICK: { Voice& v = Tone(c, W_SINE, 115, 44, 0.35f, g * 1.6f, delay); v.curve = 0.35f; v.decPow = 2.2f; v.send = 0.1f; return v; }
        case I_SNARE: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_NOISE, 100, 100, 0.07f, g, delay + k * 0.012f); v.fa0 = v.fa1 = 1400; v.fq = 1.5f; v.decPow = 2.6f; v.send = 0.35f; } Voice& v = Tone(c, W_NOISE, 100, 100, 0.16f, g * 0.6f, delay + 0.03f); v.hp = 900; v.decPow = 2.4f; v.send = 0.4f; return v; } // a hand clap
        case I_HAT: { Voice& v = Tone(c, W_NOISE, 100, 100, 0.045f, g, delay); v.hp = 7000; v.decPow = 3; v.send = 0.2f; return v; }
        case I_LOGDRUM: { Voice& v = Tone(c, W_FM, f, f * 0.7f, 0.3f, g * 1.4f, delay); v.fmRatio = 1.6f; v.fmIndex = 1.2f; v.fmIndex1 = 0; v.curve = 0.4f; v.decPow = 2.4f; v.send = 0.3f; return v; }
        case I_HANDDRUM: { Voice& v = Tone(c, W_SINE, 280, 190, 0.12f, g, delay); v.curve = 0.4f; v.decPow = 2.5f; Voice& s = Tone(c, W_NOISE, 100, 100, 0.03f, g * 0.5f, delay); s.fa0 = s.fa1 = 2500; s.fq = 2; return v; }
        case I_SHAKER: { Voice& v = Tone(c, W_NOISE, 100, 100, 0.08f, g, delay); v.hp = 5000; v.atk = 0.02f; v.decPow = 2; v.send = 0.2f; return v; }
        case I_CLANK: { Voice& v = Tone(c, W_FM, f, f * 0.99f, 1.4f, g, delay); v.fmRatio = 2.76f; v.fmIndex = 2.6f; v.fmIndex1 = 0.2f; v.atk = 0.002f; v.decPow = 1.8f; v.send = 0.85f; return v; }
        case I_GLASS: { Voice& v = Tone(c, W_FM, f, f, 1.2f, g, delay); v.fmRatio = 3; v.fmIndex = 1; v.fmIndex1 = 0; v.atk = 0.002f; v.decPow = 2.5f; v.send = 0.85f; return v; }
        case I_WHALE: { Voice& v = Tone(c, W_SINE, f, f * RR(1.15f, 1.4f), dur, g, delay); v.atk = dur * 0.3f; v.vibR = 4; v.vibD = 0.015f; v.curve = 1.5f; v.send = 0.85f; return v; }
        case I_SQUEEZE: { Voice& v = Tone(c, W_SQR, f, f, dur, g, delay); v.atk = 0.02f; v.decPow = 1.3f; v.fa0 = v.fa1 = 900; v.fb0 = v.fb1 = 1900; v.fq = 2.5f; v.tremR = 6; v.tremD = 0.2f; v.send = 0.35f; return v; }
        default: { Voice& v = Tone(c, W_NOISE, 100, 100, 0.1f, g, delay); v.cut0 = v.cut1 = 1100; v.atk = 0.004f; v.decPow = 2.2f; v.send = 0.35f; return v; } // I_CHUFF: a piston's breath
        }
    };
    make(0, gain);
    if (echo) { float d = 60.0f / gSong.bpm * 0.75f; make(d, gain * 0.38f); make(d * 2, gain * 0.15f); } // a dotted-eighth echo
}
void PlayChant(float f, int vowel, float dur, float gain, float pan) { // a group of voices on one syllable: "ho", "ha", "ey", "oo"
    static const float FA[5][2] = {{0, 0}, {450, 800}, {800, 1200}, {500, 1750}, {350, 700}};
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f};
    for (int k = 0; k < 3; k++) {
        PanGains(pan + (k - 1) * 0.3f, c.gl, c.gr);
        float ff = f * (k == 2 ? 0.5f : 1.0f) * (1 + (k - 1) * 0.004f); // unison, and one voice an octave down
        Voice& v = Tone(c, W_SAW, ff * 1.02f, ff, dur, gain * (k == 2 ? 0.6f : 1.0f), k * 0.012f);
        v.curve = 0.1f; v.atk = 0.03f; v.hold = dur * 0.5f; v.decPow = 1.3f; v.noiseMix = 0.08f; v.vibR = 5.5f; v.vibD = 0.015f;
        v.fa0 = v.fa1 = FA[vowel][0]; v.fb0 = v.fb1 = FA[vowel][1]; v.fq = 6; v.send = 0.5f;
    }
}

int ChordTone(const Song& s, int chord, int k) { return chord + (k % 3) * 2 + (k / 3) * s.n; }
int FifthIdx(const Song& s) { for (int k = 0; k < s.n; k++) if (s.scale[k] == 7) return k; return s.n / 2; }

std::vector<std::pair<int, int>> MakeMotif(const Song& s) { // a short phrase the lead keeps coming back to and developing
    std::vector<std::pair<int, int>> m;
    int total = 0, limit = s.tpb * 3, deg = 0;
    const int durs[4] = {s.tpbeat, s.tpbeat, s.tpbeat * 2, s.tpbeat / 2 > 0 ? s.tpbeat / 2 : 1};
    while (total < limit) {
        int d = durs[(int)(R01() * 4)];
        if (total + d > limit) d = limit - total;
        if (R01() < 0.15f && total > 0) m.push_back({-99, d});
        else { deg = std::clamp(deg + (int)(R01() * 5) - 2, -2, s.n + 2); m.push_back({deg, d}); }
        total += d;
    }
    return m;
}
void Pat(Layer& L, std::initializer_list<int> v, int steps) { int k = 0; for (int x : v) { if (k < 16) L.pat[k] = L.base[k] = x; k++; } L.steps = steps; }
void Pit(Layer& L, std::initializer_list<int> v) { int k = 0; for (int x : v) if (k < 16) L.pit[k++] = x; }
uint32_t Sec(std::initializer_list<int> v) { uint32_t m = 0; for (int x : v) m |= 1u << x; return m; }
uint32_t Span(int a, int b) { uint32_t m = 0; for (int x = a; x <= b; x++) m |= 1u << x; return m; }

Song MakeSong(int lv) {
    Song s;
    auto add = [&](int role, int inst, float gain, uint32_t mask, int oct) -> Layer& { Layer L; L.role = role; L.inst = inst; L.gain = gain; L.mask = mask; L.oct = oct; for (auto& x : L.pat) x = -1; for (auto& x : L.base) x = -1; s.layers.push_back(L); return s.layers.back(); };
    auto prog = [&](int* p, int& n, std::initializer_list<int> v) { n = 0; for (int x : v) p[n++] = x; };
    switch (lv) {
    case PL_PIPES: { // synth-forward: a pulsing analogue sequence over a saw pad, a piston rhythm, and only the odd clang of the pipes
        s.root = 73.42f; { int sc[7] = {0, 2, 3, 5, 7, 9, 10}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 100;
        prog(s.progA, s.nA, {0, 3, 5, 4}); prog(s.progB, s.nB, {5, 3, 0, 6}); s.chordBars = 2; s.sectionB = Sec({4, 5, 8, 9});
        add(R_DRONE, I_DRONE, 0.055f, Span(0, 11), 0);
        add(R_PAD, I_PAD, 0.03f, Span(1, 10), 1);
        { Layer& L = add(R_ARP, I_ARP_SQR, 0.028f, Span(2, 10), 2); Pat(L, {0, 1, 2, 3, 2, 1, 0, 1, 0, 2, 3, 4, 3, 2, 1, 2}, 16); L.mutate = true; L.pan = 0.25f; }
        { Layer& L = add(R_ARP, I_ARP_SAW, 0.018f, Span(6, 9), 3); Pat(L, {0, -1, 2, -1, 3, -1, 2, -1, 4, -1, 3, -1, 2, -1, 1, -1}, 16); L.mutate = true; L.pan = -0.35f; }
        { Layer& L = add(R_BASS, I_SAW_BASS, 0.06f, Span(3, 9), 0); Pat(L, {1, 0, 1, 0, 1, 0, 2, 0, 1, 0, 1, 0, 3, 0, 2, 0}, 16); }
        { Layer& L = add(R_LEAD, I_SYNTH_LEAD, 0.034f, Sec({5, 6, 7, 8, 9}), 2); L.motif = MakeMotif(s); }
        { Layer& L = add(R_DRUM, I_CHUFF, 0.05f, Span(4, 9), 0); Pat(L, {6, 0, 0, 3, 0, 0, 5, 0, 6, 0, 0, 3, 0, 0, 5, 2}, 16); }
        { Layer& L = add(R_DRUM, I_KICK, 0.06f, Span(6, 8), 0); Pat(L, {8, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0}, 16); }
        { Layer& L = add(R_DRUM, I_HAT, 0.022f, Span(5, 9), 0); Pat(L, {0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 3}, 16); }
        { Layer& L = add(R_ACCENT, I_CLANK, 0.03f, Span(0, 11), 2); L.accentProb = 0.07f; } // a pipe rings, now and then
        s.rev = 0.84f; s.damp = 0.35f; s.wet = 0.3f;
        break; }
    case PL_HULL: { // the open-water synth the user liked: slow quartal pads, a soft echoing pluck, a singing lead
        s.root = 55.0f; { int sc[5] = {0, 3, 5, 7, 10}; memcpy(s.scale, sc, sizeof sc); } s.n = 5; s.bpm = 62;
        prog(s.progA, s.nA, {0, 3, 2, 4}); prog(s.progB, s.nB, {2, 0, 3, 1}); s.chordBars = 4; s.sectionB = Sec({4, 5, 8, 9});
        add(R_DRONE, I_DRONE, 0.065f, Span(0, 11), 0);
        add(R_PAD, I_WARM_PAD, 0.04f, Span(1, 11), 1);
        { Layer& L = add(R_ARP, I_PLUCK, 0.022f, Span(3, 9), 2); Pat(L, {0, -1, 2, -1, -1, 1, -1, -1, 3, -1, -1, 2, -1, -1, 1, -1}, 16); L.echo = true; L.mutate = true; }
        { Layer& L = add(R_LEAD, I_WHALE, 0.04f, Sec({2, 3, 5, 6, 7, 8, 10}), 2); L.motif = MakeMotif(s); }
        { Layer& L = add(R_BASS, I_SUB_BASS, 0.06f, Span(4, 10), 0); Pat(L, {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16); }
        add(R_PAD, I_PAD, 0.02f, Span(6, 9), 2);
        { Layer& L = add(R_DRUM, I_KICK, 0.035f, Span(6, 8), 0); Pat(L, {6, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0}, 16); }
        s.rev = 0.9f; s.damp = 0.5f; s.wet = 0.38f;
        break; }
    case PL_PIRATE: { // a synth sea shanty in 6/8: stomp and clap, an oom-pah squeezebox, and a tune of its own on a lead synth
        s.root = 82.41f; { int sc[7] = {0, 2, 3, 5, 7, 8, 10}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 68; s.tpb = 12; s.tpbeat = 6; s.barsPerSection = 32; // the tune twice through per section
        prog(s.progA, s.nA, {0, 0, 5, 3, 0, 0, 4, 0, 0, 4, 3, 6, 0, 2, 6, 0}); prog(s.progB, s.nB, {0, 0, 5, 3, 0, 0, 4, 0, 0, 4, 3, 6, 0, 2, 6, 0}); s.chordBars = 1;
        { Layer& L = add(R_BASS, I_SAW_BASS, 0.07f, Span(1, 11), 0); Pat(L, {1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0}, 12); }
        { Layer& L = add(R_STAB, I_SQUEEZE, 0.026f, Span(0, 11), 1); Pat(L, {0, 0, 4, 0, 3, 0, 0, 0, 4, 0, 3, 0}, 12); }
        { Layer& L = add(R_DRUM, I_KICK, 0.07f, Span(1, 11), 0); Pat(L, {8, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0}, 12); }
        { Layer& L = add(R_DRUM, I_SNARE, 0.05f, Span(3, 10), 0); Pat(L, {0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0}, 12); }
        { Layer& L = add(R_DRUM, I_SHAKER, 0.02f, Span(5, 9), 0); Pat(L, {3, 0, 2, 0, 2, 0, 3, 0, 2, 0, 2, 0}, 12); }
        // the tune (an original shanty): eight bars of call, eight of chorus, in scale degrees of E minor
        std::vector<std::pair<int, int>> tune = {
            {4, 4}, {4, 2}, {4, 2}, {3, 2}, {2, 2},   {0, 4}, {2, 2}, {4, 6},   {5, 4}, {5, 2}, {4, 2}, {3, 2}, {2, 2},   {3, 6}, {1, 6},
            {4, 4}, {4, 2}, {4, 2}, {3, 2}, {2, 2},   {0, 4}, {2, 2}, {4, 4}, {6, 2},   {7, 4}, {6, 2}, {4, 2}, {3, 2}, {1, 2},   {0, 12},
            {7, 6}, {6, 6},   {4, 4}, {5, 2}, {4, 6},   {3, 4}, {4, 2}, {5, 2}, {4, 2}, {3, 2},   {1, 6}, {-99, 6},
            {7, 6}, {6, 6},   {4, 4}, {5, 2}, {7, 4}, {6, 2},   {4, 4}, {3, 2}, {1, 2}, {2, 2}, {1, 2},   {0, 12}};
        { Layer& L = add(R_LEAD, I_SYNTH_LEAD, 0.05f, Sec({2, 3, 6, 7, 8, 10}), 2); L.motif = tune; L.absolute = true; L.pan = 0.1f; }
        { Layer& L = add(R_LEAD, I_ARP_SQR, 0.022f, Sec({7, 8}), 3); L.motif = tune; L.absolute = true; L.pan = -0.3f; } // doubled an octave up at the height
        { Layer& L = add(R_LEAD, I_FIDDLE, 0.03f, Sec({4, 5, 9}), 2); L.motif = tune; L.absolute = true; L.pan = 0.3f; }
        { Layer& L = add(R_ARP, I_ARP_SAW, 0.018f, Span(4, 9), 3); Pat(L, {0, -1, 2, -1, 1, -1, 3, -1, 2, -1, 1, -1}, 12); L.mutate = true; }
        add(R_PAD, I_PAD, 0.022f, Span(6, 10), 1);
        s.rev = 0.62f; s.damp = 0.4f; s.wet = 0.16f;
        break; }
    case PL_ISLAND: { // ambient synth under the jungle, a log drum, and the tribe's chanting - a call and its answer
        s.root = 98.0f; { int sc[5] = {0, 2, 4, 7, 9}; memcpy(s.scale, sc, sizeof sc); } s.n = 5; s.bpm = 84;
        prog(s.progA, s.nA, {0, 3, 1, 4}); prog(s.progB, s.nB, {3, 0, 4, 2}); s.chordBars = 2; s.sectionB = Sec({5, 6, 9});
        add(R_DRONE, I_DRONE, 0.05f, Span(0, 11), 0);
        add(R_PAD, I_WARM_PAD, 0.035f, Span(1, 11), 1);
        { Layer& L = add(R_DRUM, I_LOGDRUM, 0.06f, Span(2, 10), 0); Pat(L, {8, 0, 0, 5, 0, 0, 6, 0, 0, 0, 7, 0, 0, 4, 0, 0}, 16); }
        { Layer& L = add(R_DRUM, I_HANDDRUM, 0.035f, Span(3, 9), 0); Pat(L, {0, 0, 4, 0, 5, 0, 0, 3, 0, 4, 0, 0, 5, 0, 3, 4}, 16); }
        { Layer& L = add(R_DRUM, I_SHAKER, 0.018f, Span(4, 9), 0); Pat(L, {3, 0, 2, 0, 3, 0, 2, 0, 3, 0, 2, 0, 3, 0, 2, 2}, 16); }
        { Layer& L = add(R_CHANT, I_CHOIR, 0.045f, Sec({3, 4, 6, 7, 8}), 1); Pat(L, {1, 0, 1, 0, 2, 0, 0, 0, 3, 0, 3, 0, 1, 0, 0, 0}, 16); Pit(L, {2, 0, 2, 0, 1, 0, 0, 0, 3, 0, 2, 0, 0, 0, 0, 0}); L.barMod = 1; L.pan = -0.2f; } // the call
        { Layer& L = add(R_CHANT, I_CHOIR, 0.04f, Sec({3, 4, 6, 7, 8}), 1); Pat(L, {2, 0, 2, 0, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0}, 16); Pit(L, {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); L.barMod = 2; L.pan = 0.2f; }  // the answer
        { Layer& L = add(R_LEAD, I_MARIMBA, 0.038f, Sec({2, 5, 6, 9, 10}), 2); L.altInst = I_FLUTE; L.motif = MakeMotif(s); }
        { Layer& L = add(R_ARP, I_MARIMBA, 0.018f, Span(5, 8), 2); Pat(L, {0, -1, 1, 2, -1, 1, -1, 3, 0, -1, 2, -1, 1, -1, 3, -1}, 16); L.mutate = true; }
        s.rev = 0.6f; s.damp = 0.5f; s.wet = 0.15f;
        break; }
    case PL_CAVE: { // dark ambient: a drone, tuned drips echoing off the rock, a far choir, a slow deep pulse
        s.root = 65.41f; { int sc[7] = {0, 1, 3, 5, 7, 8, 10}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 52;
        prog(s.progA, s.nA, {0, 1, 0, 5}); prog(s.progB, s.nB, {5, 6, 1, 0}); s.chordBars = 4; s.sectionB = Sec({5, 6, 9});
        add(R_DRONE, I_DRONE, 0.075f, Span(0, 11), 0);
        add(R_PAD, I_WARM_PAD, 0.03f, Span(2, 10), 1);
        { Layer& L = add(R_ARP, I_GLASS, 0.018f, Span(1, 10), 2); Pat(L, {0, -1, -1, -1, -1, -1, 2, -1, -1, -1, 1, -1, -1, -1, -1, -1}, 16); L.echo = true; L.mutate = true; }
        { Layer& L = add(R_BASS, I_SUB_BASS, 0.06f, Span(4, 9), 0); Pat(L, {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16); }
        { Layer& L = add(R_LEAD, I_CHOIR, 0.022f, Sec({6, 7, 8, 9}), 1); L.motif = MakeMotif(s); }
        { Layer& L = add(R_DRUM, I_KICK, 0.03f, Span(5, 8), 0); Pat(L, {6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16); }
        s.rev = 0.93f; s.damp = 0.25f; s.wet = 0.5f;
        break; }
    case PL_WEEDS: { // bright and swaying: shimmering pads, a harp in echoes, bells, a gentle shaker
        s.root = 87.31f; { int sc[7] = {0, 2, 4, 6, 7, 9, 11}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 72;
        prog(s.progA, s.nA, {0, 1, 0, 4}); prog(s.progB, s.nB, {5, 1, 3, 4}); s.chordBars = 2; s.sectionB = Sec({4, 5, 8, 9});
        add(R_PAD, I_WARM_PAD, 0.04f, Span(0, 11), 1);
        { Layer& L = add(R_ARP, I_PLUCK, 0.024f, Span(1, 10), 2); Pat(L, {0, 1, 2, 3, 4, 3, 2, 1, 0, 2, 4, 5, 4, 2, 1, -1}, 16); L.echo = true; L.mutate = true; }
        { Layer& L = add(R_BASS, I_SUB_BASS, 0.05f, Span(3, 9), 0); Pat(L, {1, 0, 0, 0, 0, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 16); }
        { Layer& L = add(R_LEAD, I_BELL, 0.028f, Span(4, 9), 2); L.motif = MakeMotif(s); }
        { Layer& L = add(R_DRUM, I_SHAKER, 0.016f, Span(3, 9), 0); Pat(L, {3, 0, 2, 0, 3, 0, 2, 0, 3, 0, 2, 0, 3, 0, 2, 0}, 16); }
        { Layer& L = add(R_DRUM, I_HANDDRUM, 0.025f, Span(5, 8), 0); Pat(L, {5, 0, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0}, 16); }
        add(R_PAD, I_PAD, 0.018f, Span(6, 9), 2);
        s.rev = 0.8f; s.damp = 0.45f; s.wet = 0.3f;
        break; }
    case PL_ATLANTIS: { // a drowned cathedral: choir and organ, bells turning slowly, a deep tom
        s.root = 58.27f; { int sc[7] = {0, 2, 3, 5, 7, 8, 11}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 54;
        prog(s.progA, s.nA, {0, 5, 3, 4}); prog(s.progB, s.nB, {5, 3, 6, 4}); s.chordBars = 4; s.sectionB = Sec({5, 6, 9});
        add(R_DRONE, I_DRONE, 0.06f, Span(0, 11), 0);
        add(R_PAD, I_CHOIR, 0.045f, Span(1, 11), 1);
        add(R_PAD, I_ORGAN, 0.022f, Span(4, 9), 1);
        { Layer& L = add(R_ARP, I_BELL, 0.022f, Span(3, 9), 2); Pat(L, {0, -1, 1, -1, 2, -1, 3, -1, 2, -1, 1, -1, 0, -1, -1, -1}, 16); L.mutate = true; }
        { Layer& L = add(R_LEAD, I_FLUTE, 0.03f, Span(5, 8), 2); L.motif = MakeMotif(s); }
        { Layer& L = add(R_BASS, I_SUB_BASS, 0.05f, Span(3, 10), 0); Pat(L, {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16); }
        { Layer& L = add(R_DRUM, I_LOGDRUM, 0.05f, Span(5, 9), 0); Pat(L, {7, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0}, 16); }
        s.rev = 0.92f; s.damp = 0.35f; s.wet = 0.42f;
        break; }
    default: { // the Abyss: a drone and a cluster that never resolves, a far groan, glass, and a heartbeat when it gets close
        s.root = 43.65f; { int sc[7] = {0, 1, 3, 5, 6, 8, 10}; memcpy(s.scale, sc, sizeof sc); } s.n = 7; s.bpm = 40;
        prog(s.progA, s.nA, {0, 1, 0, 4}); prog(s.progB, s.nB, {1, 0, 4, 0}); s.chordBars = 4; s.sectionB = Sec({6, 7});
        add(R_DRONE, I_DRONE, 0.085f, Span(0, 11), 0);
        add(R_PAD, I_WARM_PAD, 0.03f, Span(2, 10), 1);
        { Layer& L = add(R_ACCENT, I_WHALE, 0.05f, Span(0, 11), 1); L.accentProb = 0.08f; }
        { Layer& L = add(R_ARP, I_GLASS, 0.014f, Span(4, 8), 2); Pat(L, {0, -1, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1}, 16); L.echo = true; L.mutate = true; }
        { Layer& L = add(R_DRUM, I_KICK, 0.04f, Span(6, 9), 0); Pat(L, {6, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16); }
        s.rev = 0.95f; s.damp = 0.3f; s.wet = 0.45f;
        break; }
    }
    return s;
}

void MusicTick() {
    if (gLevel < 0) return;
    Song& S = gSong;
    int bar = gTick / S.tpb, step = gTick % S.tpb, bps = S.barsPerSection, total = bps * S.sections;
    int section = (bar / bps) % S.sections;
    if (bar % total == 0 && step == 0 && bar > 0) { // round again: re-roll what makes this pass its own
        for (auto& L : S.layers) { if (!L.absolute && !L.motif.empty()) L.motif = MakeMotif(S); for (int k = 0; k < 16; k++) L.pat[k] = L.base[k]; }
        if (R01() < 0.5f) { std::swap(S.progA, S.progB); std::swap(S.nA, S.nB); }
    }
    bool isB = (S.sectionB >> section) & 1;
    const int* pr = isB ? S.progB : S.progA; int np = isB ? S.nB : S.nA;
    int chord = pr[(bar / S.chordBars) % np];
    float inten = std::clamp(ARC[section % 12] + 0.3f * gTensionS, 0.0f, 1.0f);
    float bright = std::clamp(0.2f + 0.65f * inten + 0.12f * sinf(gTick * TickDur() * 0.03f), 0.0f, 1.0f);
    float barDur = TickDur() * S.tpb;
    bool barStart = step == 0, chordStart = barStart && bar % S.chordBars == 0;
    for (auto& L : S.layers) {
        float target = ((L.mask >> section) & 1) ? L.gain : 0.0f;
        float rate = L.gain / (S.tpb * 4.0f); // four bars to fade in or out
        L.cur = L.cur < target ? std::min(target, L.cur + rate) : std::max(target, L.cur - rate);
        if (L.cur < 0.0005f) { L.phrase = -1; continue; }
        float g = L.cur * (0.75f + 0.5f * inten);
        int inst = L.altInst >= 0 && gDay < 0.4f ? L.altInst : L.inst;
        switch (L.role) {
        case R_DRONE:
            if (barStart && bar % 4 == 0) { float d = barDur * 4 * 1.3f; PlayInst(inst, Note(S, 0, L.oct), d, g, -0.2f, bright); PlayInst(inst, Note(S, FifthIdx(S), L.oct), d, g * 0.6f, 0.2f, bright); }
            break;
        case R_PAD:
            if (chordStart) { int nt = inten > 0.6f ? 4 : 3; for (int k = 0; k < nt; k++) PlayInst(inst, Note(S, ChordTone(S, chord, k), L.oct), barDur * S.chordBars * 1.2f, g / sqrtf((float)nt), -0.5f + k * 0.33f, bright); }
            break;
        case R_ARP: {
            if (L.mutate && barStart && bar % 4 == 0 && R01() < 0.3f) { int k = (int)(R01() * L.steps); L.pat[k] = R01() < 0.25f ? -1 : (int)(R01() * 5); } // the sequence drifts
            int v = L.pat[step % L.steps];
            if (v >= 0 && R01() < 0.55f + 0.45f * inten) PlayInst(inst, Note(S, ChordTone(S, chord, v), L.oct), TickDur() * 2.5f, g, L.pan + 0.3f * sinf(gTick * 0.05f), bright, L.echo);
            break; }
        case R_BASS: { int v = L.pat[step % L.steps]; if (v > 0) PlayInst(inst, Note(S, v == 2 ? chord + 2 * 2 : chord, L.oct + (v == 3 ? 1 : 0)), TickDur() * (S.tpbeat * 1.8f), g, 0, bright); break; }
        case R_STAB: { int v = L.pat[step % L.steps]; if (v > 0) for (int k = 0; k < 3; k++) PlayInst(inst, Note(S, ChordTone(S, chord, k), L.oct), TickDur() * 1.6f, g * v / 6.0f, -0.3f + k * 0.3f, bright); break; }
        case R_DRUM: { int v = L.pat[step % L.steps]; if (v > 0 && (inten > 0.3f || R01() < 0.7f)) PlayInst(inst, Note(S, 0, 2), 0.3f, g * v / 7.0f, L.pan, bright); break; }
        case R_CHANT: {
            if ((L.barMod == 1 && bar % 2 != 0) || (L.barMod == 2 && bar % 2 == 0)) break;
            int v = L.pat[step % L.steps];
            if (v > 0) PlayChant(Note(S, chord + L.pit[step % L.steps], L.oct + 1), v, TickDur() * 1.8f, g, L.pan);
            break; }
        case R_ACCENT:
            if (barStart && R01() < L.accentProb) PlayInst(inst, Note(S, chord + (int)(R01() * 3) * 2, L.oct), barDur * 1.5f, g, RR(-0.7f, 0.7f), bright);
            break;
        case R_LEAD: {
            int period = L.absolute ? 16 : 4; // a composed tune runs sixteen bars; a motif phrase starts every four
            if (barStart && bar % period == 0 && !L.motif.empty()) {
                L.phrase = 0; L.nextTick = gTick;
                L.phraseBase = L.absolute ? 0 : chord;
                if (!L.absolute && R01() < 0.35f) { auto& m = L.motif[(int)(R01() * L.motif.size())]; if (m.first != -99) m.first += R01() < 0.5f ? 1 : -1; } // develop it a little each time
            }
            if (L.phrase >= 0 && gTick == L.nextTick) {
                auto m = L.motif[L.phrase];
                if (m.first != -99) PlayInst(inst, Note(S, L.phraseBase + m.first, L.oct - 1), TickDur() * m.second * 1.05f, g, L.pan, bright, L.echo);
                L.nextTick += m.second;
                if (++L.phrase >= (int)L.motif.size()) L.phrase = -1;
            }
            break; }
        }
    }
    // an apex is near: under everything, a low tremolo string and a pulse on every beat
    if (gTensionS > 0.25f && step % S.tpbeat == 0) {
        PlayInst(I_SUB_BASS, Note(S, chord, 0), TickDur() * S.tpbeat * 0.9f, 0.08f * gTensionS, 0, bright);
        if (barStart && bar % 2 == 0) for (int k = 0; k < 2; k++) { Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f}; PanGains(k ? 0.5f : -0.5f, c.gl, c.gr); Voice& tr = Tone(c, W_SAW, Note(S, chord + k, 1), 0, barDur * 2.1f, 0.03f * gTensionS); tr.f1 = tr.f0; tr.cut0 = tr.cut1 = 900; tr.tremR = 7.5f; tr.tremD = 0.6f; tr.atk = barDur * 0.3f; tr.decPow = 1; tr.send = 0.5f; }
    }
}

// ================================================================ the rest of the ship's sound (Master Reference)
// ---------------------------------------------------------------- a generated convolution reverb
// Each room type's impulse is built from filtered noise: a handful of early reflections, then a tail that decays
// exponentially and darkens as it goes. It is convolved in 512-sample partitions by FFT (overlap-save), so even the
// three-second cave costs little.
struct Cpx { float r, i; };
void FFT(Cpx* a, int n, bool inv) {
    for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = TAU / len * (inv ? 1 : -1);
        Cpx wl{cosf(ang), sinf(ang)};
        for (int i = 0; i < n; i += len) {
            Cpx w{1, 0};
            for (int j = 0; j < len / 2; j++) {
                Cpx u = a[i + j], v = a[i + j + len / 2];
                Cpx t{v.r * w.r - v.i * w.i, v.r * w.i + v.i * w.r};
                a[i + j] = {u.r + t.r, u.i + t.i};
                a[i + j + len / 2] = {u.r - t.r, u.i - t.i};
                w = {w.r * wl.r - w.i * wl.i, w.r * wl.i + w.i * wl.r};
            }
        }
    }
    if (inv) for (int i = 0; i < n; i++) { a[i].r /= n; a[i].i /= n; }
}
struct ConvRev {
    static constexpr int B = 512, N = 1024, H = N / 2 + 1;
    int P = 0, xPos = 0, pos = 0, room = -1;
    std::vector<std::vector<Cpx>> HL, HR, X;
    std::vector<float> in = std::vector<float>(B, 0), prev = std::vector<float>(B, 0), outL = std::vector<float>(B, 0), outR = std::vector<float>(B, 0);
    void Build(int r) {
        room = r;
        //                 T60  predelay(ms) bright early  gain
        const float PR[RR_COUNT][5] = {{1.4f, 12, 0.55f, 8, 0.32f},   // the salon: wood and brass, warm, not long
                                       {3.0f, 24, 0.35f, 14, 0.30f},  // a cave: long, dark, many reflections
                                       {1.8f, 8, 0.3f, 4, 0.26f},     // kelp: soft, absorbent
                                       {2.4f, 30, 0.7f, 10, 0.3f},    // a stone hall (Atlantis): bright and long
                                       {0.9f, 60, 0.4f, 2, 0.2f}};    // the open sea: little but a far return
        const float* p = PR[std::clamp(r, 0, RR_COUNT - 1)];
        int len = (int)(p[0] * SR);
        P = (len + B - 1) / B;
        HL.assign(P, std::vector<Cpx>(H)); HR.assign(P, std::vector<Cpx>(H)); X.assign(P, std::vector<Cpx>(H, Cpx{0, 0}));
        for (int ch = 0; ch < 2; ch++) {
            std::vector<float> ir(P * B, 0);
            uint32_t seed = 0x1234567u + ch * 7919u + r * 104729u;
            auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float)(seed & 0xFFFFFF) / 8388608.0f - 1.0f; };
            int pre = (int)(p[1] * SR / 1000);
            for (int e = 0; e < (int)p[3]; e++) { int at = pre + (int)((0.004f + 0.07f * (e + rnd() * 0.5f + 0.5f) / p[3]) * SR); if (at < len) ir[at] += rnd() * 0.7f * (1 - e / p[3] * 0.6f); }
            float lp = 0;
            for (int n = pre; n < len; n++) {
                float u = (float)n / len, env = expf(-6.9f * n / (p[0] * SR));
                float a = std::clamp(p[2] * (1 - 0.8f * u), 0.03f, 0.95f); // the tail darkens as it decays
                lp += (rnd() - lp) * a;
                ir[n] += lp * env * 0.6f;
            }
            double e2 = 0; for (float x : ir) e2 += x * x;
            float norm = p[4] / (float)sqrt(std::max(1e-9, e2));
            std::vector<Cpx> buf(N);
            for (int q = 0; q < P; q++) {
                for (int k = 0; k < N; k++) buf[k] = {k < B ? ir[q * B + k] * norm : 0.0f, 0};
                FFT(buf.data(), N, false);
                for (int k = 0; k < H; k++) (ch ? HR : HL)[q][k] = buf[k];
            }
        }
        xPos = 0; pos = 0;
        std::fill(prev.begin(), prev.end(), 0.0f); std::fill(outL.begin(), outL.end(), 0.0f); std::fill(outR.begin(), outR.end(), 0.0f);
    }
    void Process() {
        std::vector<Cpx> f(N);
        for (int k = 0; k < N; k++) f[k] = {k < B ? prev[k] : in[k - B], 0};
        FFT(f.data(), N, false);
        for (int k = 0; k < H; k++) X[xPos][k] = f[k];
        for (int ch = 0; ch < 2; ch++) {
            auto& Hs = ch ? HR : HL;
            std::vector<Cpx> y(N, Cpx{0, 0});
            for (int q = 0; q < P; q++) {
                const auto& x = X[(xPos - q + P) % P];
                const auto& h = Hs[q];
                for (int k = 0; k < H; k++) { y[k].r += x[k].r * h[k].r - x[k].i * h[k].i; y[k].i += x[k].r * h[k].i + x[k].i * h[k].r; }
            }
            for (int k = 1; k < N / 2; k++) y[N - k] = {y[k].r, -y[k].i};
            FFT(y.data(), N, true);
            auto& o = ch ? outR : outL;
            for (int k = 0; k < B; k++) o[k] = y[B + k].r;   // overlap-save: the second half is the clean block
        }
        prev = in;
        xPos = (xPos + 1) % P;
    }
    void Run(float x, float& l, float& r) {
        if (P == 0) { l = r = 0; return; }
        l = outL[pos]; r = outR[pos]; in[pos] = x;
        if (++pos == B) { Process(); pos = 0; }
    }
};
ConvRev gConv;
int gRoomWant = RR_SALON;

#include "sound_expedition.inl"
#include "sound_redtide.inl"
float gTestBusOpen = 0;   // --audio-test: open the music and ambience buses with no scene playing

// ---------------------------------------------------------------- registered cues
int FindCue(const char* name) {
    int n; const CueDef* c = CueTable(n);
    for (int i = 0; i < n; i++) if (strcmp(c[i].name, name) == 0) return i;
    return -1;
}
void KillInstance(int inst) { for (auto& v : gV) if (v.on && v.inst == inst) v.on = false; }
// Builds cue `idx` from its recipe. Voices started in here are tagged with the cue (see NewVoice).
void BuildCue(int idx, float vol, float pan) {
    int n; const CueDef& d = CueTable(n)[idx];
    int bus = d.bus;
    // how many of it are sounding, and how full the effect voices are
    if (bus != CB_MUSIC && bus != CB_AMB) {
        int oldest = 1 << 30, instances = 0, used = 0, lowInst = -1, lowPrio = 99;
        std::vector<int> seen;
        for (auto& v : gV) {
            if (!v.on) continue;
            if (v.bus == B_SFX || v.bus == B_UI || v.bus == B_VOICE) { used++; if (v.cue >= 0 && v.prio < lowPrio) { lowPrio = v.prio; lowInst = v.inst; } }
            if (v.cue == idx && std::find(seen.begin(), seen.end(), v.inst) == seen.end()) { seen.push_back(v.inst); instances++; oldest = std::min(oldest, v.inst); }
        }
        if (instances >= d.maxSim) KillInstance(oldest);
        if (used >= 24) { if (lowInst >= 0 && lowPrio <= d.priority) KillInstance(lowInst); else return; } // the 24-voice cap: low priority goes first
    }
    int k = d.variants > 1 ? GetRandomValue(0, d.variants - 1) : 0;
    float kv = d.variants > 1 ? (float)k / (d.variants - 1) : 0.5f;
    float pitch = 1 + RR(-d.pitchVar, d.pitchVar), lenK = 0.88f + 0.24f * kv, cutK = 0.9f + 0.2f * (1 - kv);
    Ctx c{vol, pitch, 1, 1, bus, bus == B_UI ? 0.08f : 0.3f};
    PanGains(pan, c.gl, c.gr);
    gTagCue = idx; gTagInst = gNextInst++; gTagPrio = d.priority;
    float f = d.freq, dur = d.dur * lenK, g = d.gain;
    switch (d.recipe) {
    case CR_TICK: { Voice& v = Tone(c, W_SINE, f, f * 0.8f, dur, g); v.decPow = 2.5f; Voice& w = Puff(c, 6000 * cutK, 4000, 0.012f, g * 0.4f); w.hp = 2500; break; }
    case CR_CLICK: { Voice& w = Puff(c, 5000 * cutK, 2000, 0.018f, g * 0.9f); w.hp = 1200; Voice& v = Tone(c, W_SINE, f, f * 0.6f, dur, g * 0.6f); v.decPow = 3; break; }
    case CR_CONFIRM: for (int i = 0; i < 2; i++) { Voice& v = Tone(c, W_FM, f * (i ? 1.5f : 1), 0, dur, g, i * 0.08f); v.f1 = v.f0; v.fmRatio = 3.01f; v.fmIndex = 1.4f; v.fmIndex1 = 0.1f; v.decPow = 2; } break;
    case CR_CANCEL: for (int i = 0; i < 2; i++) { Voice& v = Tone(c, W_FM, f * (i ? 0.75f : 1), 0, dur, g, i * 0.08f); v.f1 = v.f0; v.fmRatio = 2.0f; v.fmIndex = 1.0f; v.fmIndex1 = 0.1f; v.decPow = 2; } break;
    case CR_ERROR: { Voice& v = Tone(c, W_SQR, f, f * 0.9f, dur, g * 0.5f); v.cut0 = v.cut1 = 600 * cutK; Thud(c, 80, g * 0.8f); break; }
    case CR_WHOOSH: Whoosh(c, f * 0.6f, f * 2.2f * cutK, dur, g); break;
    case CR_LATCH: { Voice& w = Puff(c, 7000, 3000, 0.02f, g); w.hp = 2500; Voice& v = Tone(c, W_FM, f, 0, dur, g * 0.5f, 0.01f); v.f1 = v.f0; v.fmRatio = 3.1f; v.fmIndex = 2.5f; v.fmIndex1 = 0.2f; v.decPow = 2.5f; break; }
    case CR_PLAQUE: { Voice& v = Tone(c, W_NOISE, 100, 100, dur, g); v.fa0 = f * 0.7f; v.fa1 = f * 1.4f * cutK; v.fq = 4; v.atk = dur * 0.4f; Voice& t = Tone(c, W_SINE, f * 2, f * 2, 0.15f, g * 0.25f, dur * 0.8f); t.decPow = 2; break; }
    case CR_CREAK: { Voice& v = Tone(c, W_SAW, f, f * RR(0.85f, 1.2f), dur, g); v.fa0 = v.fa1 = RR(380, 620) * cutK; v.fq = 9; v.vibR = RR(7, 13); v.vibD = 0.07f; v.atk = dur * 0.2f; v.send = 0.5f; break; }
    case CR_SONAR: for (int e = 0; e < 3; e++) { Voice& v = Tone(c, W_SINE, f, f * 0.97f, 0.5f, g * (e ? 0.35f / e : 1), e * 0.38f); v.decPow = 1.6f; v.send = 0.6f; } break;
    case CR_ORGAN2: for (int i = 0; i < 2; i++) for (int h = 1; h <= 3; h++) { Voice& v = Tone(c, W_SINE, f * (i ? 1.5f : 1) * h, 0, dur * 0.55f, g / (h * h), i * dur * 0.45f); v.f1 = v.f0; v.atk = 0.08f; v.decPow = 0.8f; v.send = 0.6f; } break;
    case CR_FLARE: { Voice& v = Puff(c, 1400 * cutK, 300, dur, g); v.atk = 0.03f; Voice& w = Tone(c, W_SINE, 90, 60, dur * 0.6f, g * 0.5f); w.decPow = 2; break; }
    case CR_JELLYHUM: for (int i = 0; i < 2; i++) { Voice& v = Tone(c, W_FM, f * (i ? 1.5f : 1), 0, dur, g * (i ? 0.5f : 1), i * 0.1f); v.f1 = v.f0 * 1.01f; v.fmRatio = 2.0f; v.fmIndex = 0.8f; v.atk = dur * 0.35f; v.decPow = 1.2f; v.send = 0.7f; } break;
    case CR_GAUGE: for (int i = 0; i < 3; i++) { Voice& v = Puff(c, 9000, 6000, 0.008f, g, i * RR(0.03f, 0.07f)); v.hp = f; } break;
    case CR_WHEEL: for (int i = 0; i < 6; i++) { Voice& w = Puff(c, 5000 * cutK, 3000, 0.02f, g, i * 0.11f); w.hp = 1500; Voice& v = Tone(c, W_FM, f, 0, 0.12f, g * 0.25f, i * 0.11f); v.f1 = v.f0; v.fmRatio = 2.7f; v.fmIndex = 1.5f; v.decPow = 3; } break;
    case CR_RUNGS: for (int i = 0; i < 4; i++) { Voice& v = Tone(c, W_SINE, f * (1 - i * 0.04f), f * 0.7f, 0.09f, g, i * dur / 4); v.decPow = 3; Voice& w = Tone(c, W_NOISE, 100, 100, 0.03f, g * 0.5f, i * dur / 4); w.fa0 = w.fa1 = 900; w.fq = 3; } break;
    case CR_SPOOL: { Voice& v = Tone(c, W_SAW, f, f * 3.2f, dur, g * 0.6f); v.cut0 = 200; v.cut1 = 1800; v.atk = dur * 0.5f; v.decPow = 1; v.det = 0.01f; Voice& r = Puff(c, 180, 700, dur, g * 0.7f); r.atk = dur * 0.4f; Voice& h = Puff(c, 6000, 3000, 0.7f, g * 0.3f, dur * 0.85f); h.hp = 2000; break; }
    case CR_TILT: { Voice& v = Tone(c, W_SAW, f, f * 0.8f, dur, g); v.fa0 = 300; v.fa1 = 260; v.fq = 6; v.atk = 0.4f; v.vibR = 3; v.vibD = 0.05f; v.send = 0.6f; for (int i = 0; i < 3; i++) { Voice& w = Tone(c, W_SAW, RR(140, 220), 0, 0.4f, g * 0.3f, 0.3f + i * 0.45f); w.f1 = w.f0 * 0.9f; w.fa0 = w.fa1 = 500; w.fq = 9; } break; }
    case CR_CLOCK: { Voice& v = Tone(c, W_SINE, f * (k % 2 ? 0.8f : 1), f * 0.7f, dur, g); v.decPow = 3; Voice& w = Puff(c, 8000, 5000, 0.006f, g * 0.6f); w.hp = 3000; break; }
    case CR_STEP: { Voice& v = Tone(c, W_SINE, f, f * 0.6f, dur, g); v.decPow = 2.5f; Voice& w = Puff(c, 900 * cutK, 300, dur, g * 0.6f); w.decPow = 2.5f; break; }
    case CR_GEAR: { for (int i = 0; i < 8; i++) { Voice& w = Puff(c, 8000, 5000, 0.01f, g, i * dur / 8); w.hp = 3000; } Voice& v = Tone(c, W_TRI, f, f * 1.6f, dur, g * 0.3f); v.atk = 0.1f; break; }
    case CR_DOOR: { Voice& v = Tone(c, W_SAW, f, f * 1.5f, dur, g); v.fa0 = 450; v.fa1 = 700; v.fq = 10; v.vibR = 11; v.vibD = 0.08f; v.atk = 0.1f; Thud(c, 90, g * 0.6f, dur * 0.9f); break; }
    case CR_LADDER: { Voice& r = Puff(c, 500 * cutK, 350, dur, g); r.tremR = 14; r.tremD = 0.5f; Voice& v = Tone(c, W_SINE, 1400, 1700, 0.12f, g * 0.25f, dur * 0.7f); v.vibR = 20; v.vibD = 0.02f; break; }
    case CR_FANFARE: { const float st[4] = {1, 1.26f, 1.5f, 2}; for (int i = 0; i < 4; i++) { Voice& v = Tone(c, W_SAW, f * st[i], 0, i == 3 ? dur * 0.6f : 0.2f, g, i * 0.13f); v.f1 = v.f0; v.cut0 = 2200; v.cut1 = 1200; v.det = 0.005f; v.atk = 0.02f; v.decPow = i == 3 ? 1.2f : 2; v.send = 0.5f; } break; }
    case CR_CHALK: { Voice& v = Tone(c, W_NOISE, 100, 100, dur, g); v.fa0 = f * cutK; v.fa1 = f * 0.8f; v.fq = 5; v.tremR = 30; v.tremD = 0.6f; break; }
    case CR_PLUCK: { Voice& v = Tone(c, W_PLUCK, f, f, dur, g); v.atk = 0.001f; v.decPow = 1; v.hold = dur * 0.5f; v.send = 0.4f; break; }
    case CR_BELL: { Voice& v = Tone(c, W_FM, f, f, dur, g); v.fmRatio = 5.4f; v.fmIndex = 1.8f; v.fmIndex1 = 0.1f; v.atk = 0.002f; v.decPow = 2.2f; v.send = 0.5f; break; }
    case CR_THUD: Thud(c, f, g); break;
    case CR_TOKEN: { Voice& v = Tone(c, W_FM, f, f, 0.4f, g * 0.6f); v.fmRatio = 3.3f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2.5f; for (int i = 0; i < 6; i++) { Voice& w = Puff(c, 6000 - i * 600, 3000, 0.015f, g * 0.4f, 0.1f + i * (0.08f - i * 0.006f)); w.hp = 1500; } Thud(c, 110, g * 0.8f, dur * 0.85f); break; }
    case CR_DRUM: { Voice& v = Tone(c, W_SINE, f * 2, f, dur, g); v.curve = 0.3f; v.decPow = 2; Puff(c, 1200, 300, 0.05f, g * 0.5f); break; }
    // ---- expeditions (stage 8)
    case CR_STEP_STONE: { Voice& w = Puff(c, 3200 * cutK, 900, 0.035f, g); w.hp = 400; w.decPow = 2.5f; Voice& v = Tone(c, W_SINE, f, f * 0.6f, 0.06f, g * 0.6f); v.decPow = 2.5f; break; }
    case CR_STEP_SAND: { Voice& w = Puff(c, 2400 * cutK, 1200, dur, g); w.hp = 700; w.atk = dur * 0.3f; w.decPow = 1.6f; break; }
    case CR_STEP_KELP: { Voice& w = Tone(c, W_NOISE, 100, 100, dur, g); w.fa0 = 700 * cutK; w.fa1 = 400; w.fq = 3; w.atk = 0.01f; w.decPow = 1.8f; Voice& v = Tone(c, W_SINE, f, f * 0.7f, 0.08f, g * 0.4f); v.decPow = 2.5f; break; }
    case CR_STEP_MARBLE: { Voice& w = Puff(c, 7000 * cutK, 3000, 0.02f, g); w.hp = 1500; Voice& v = Tone(c, W_FM, f, f, 0.25f, g * 0.25f); v.fmRatio = 3.1f; v.fmIndex = 1.2f; v.fmIndex1 = 0; v.decPow = 2.5f; v.send = 0.7f; break; }
    case CR_BREATH: { Voice& a = Tone(c, W_NOISE, 100, 100, dur * 0.45f, g); a.fa0 = 500; a.fa1 = 1400; a.fq = 2; a.atk = dur * 0.2f; a.decPow = 1.2f;
                      Voice& b = Tone(c, W_NOISE, 100, 100, dur * 0.5f, g * 0.8f, dur * 0.5f); b.fa0 = 1200; b.fa1 = 450; b.fq = 2; b.atk = dur * 0.15f; b.decPow = 1.4f;
                      Voice& r = Puff(c, 7000, 5000, 0.12f, g * 0.3f, dur * 0.45f); r.hp = 4000; break; } // in, the regulator's hiss, out
    case CR_TORCH: { Voice& w = Puff(c, 6000, 3000, 0.015f, g); w.hp = 2500; Voice& v = Tone(c, W_SQR, f, f, dur, g * 0.12f, 0.02f); v.cut0 = v.cut1 = 700; v.tremR = 40; v.tremD = 0.4f; break; }
    case CR_STATIC: for (int i = 0; i < 6; i++) { Voice& w = Puff(c, RR(2000, 7000), 1500, RR(0.01f, 0.05f), g * RR(0.4f, 1), i * RR(0.02f, 0.08f)); w.hp = 800; } break;
    case CR_HIT_SLASH: { Whoosh(c, f, f * 3 * cutK, dur, g); Voice& v = Tone(c, W_NOISE, 100, 100, 0.08f, g * 0.6f, dur * 0.8f); v.fa0 = 4500; v.fa1 = 2500; v.fq = 4; break; }
    case CR_HIT_THRUST: { Whoosh(c, f * 1.5f, f * 4 * cutK, dur * 0.6f, g); Voice& v = Puff(c, 5000, 2000, 0.03f, g * 0.7f, dur * 0.55f); v.hp = 1500; break; }
    case CR_HIT_BLUNT: { Whoosh(c, f * 0.6f, f * 1.6f * cutK, dur, g * 0.8f); Thud(c, 75, g * 0.9f, dur * 0.85f); break; }
    case CR_HIT_SHOT: { Voice& v = Puff(c, 6500 * cutK, 1200, 0.12f, g); v.hp = 600; Thud(c, 95, g * 0.6f); Voice& e = Puff(c, 1500, 400, 0.45f, g * 0.2f, 0.05f); e.send = 0.7f; break; }
    case CR_HIT_THROW: { Whoosh(c, f, f * 2 * cutK, dur, g * 0.7f); for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_SINE, RR(2400, 3800), 0, 0.12f, g * 0.25f, dur * 0.9f + i * 0.03f); v.f1 = v.f0 * 0.97f; v.decPow = 2.5f; } break; } // a flask breaks
    case CR_HIT_CAST: { for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_FM, f * (1 + i * 0.5f), f * (1.5f + i * 0.5f), dur, g * 0.5f, i * 0.05f); v.fmRatio = 2.01f; v.fmIndex = 2; v.fmIndex1 = 0.3f; v.atk = dur * 0.4f; v.send = 0.7f; } break; }
    case CR_HIT_SONG: { Voice& v = Vox(c, f, f * 1.12f, dur, g, 700, 900, 1150, 1500); v.atk = dur * 0.2f; v.hold = dur * 0.4f; v.vibR = 5.5f; v.vibD = 0.02f; v.send = 0.7f; break; }
    case CR_IMP_FLESH: { Voice& v = Tone(c, W_SINE, f * 1.6f, f * 0.6f, 0.12f, g); v.curve = 0.4f; v.decPow = 2.5f; Voice& w = Puff(c, 1400 * cutK, 400, 0.08f, g * 0.7f); w.decPow = 2.5f; break; }
    case CR_IMP_SHELL: { Voice& w = Puff(c, 5500 * cutK, 2500, 0.03f, g); w.hp = 1800; for (int i = 0; i < 3; i++) { Voice& v = Puff(c, 4000, 2000, 0.012f, g * 0.5f, 0.015f + i * 0.02f); v.hp = 2500; } Thud(c, f, g * 0.4f); break; }
    case CR_IMP_METAL: { Voice& v = Tone(c, W_FM, f, f * 0.99f, dur, g); v.fmRatio = 2.76f; v.fmIndex = 2.4f; v.fmIndex1 = 0.3f; v.decPow = 2; v.send = 0.5f; Voice& w = Puff(c, 7000, 3000, 0.02f, g * 0.6f); w.hp = 2000; break; }
    case CR_IMP_STONE: { for (int i = 0; i < 4; i++) { Voice& w = Puff(c, RR(1200, 2600) * cutK, 300, RR(0.04f, 0.08f), g * 0.6f, i * 0.02f); w.decPow = 2.3f; } Thud(c, f, g * 0.7f); break; }
    case CR_CRACK: { Voice& w = Puff(c, 9000, 3500, 0.05f, g); w.hp = 1200; w.atk = 0.001f; Voice& v = Tone(c, W_SQR, f, f * 0.5f, 0.08f, g * 0.3f); v.cut0 = 3000; v.cut1 = 800; Voice& e = Puff(c, 2500, 800, 0.4f, g * 0.2f, 0.03f); e.send = 0.8f; break; }
    case CR_DRIPS: for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_SINE, f * RR(0.9f, 1.2f), 0, 0.09f, g, i * RR(0.1f, 0.2f)); v.f1 = v.f0 * 0.5f; v.curve = 0.4f; v.decPow = 2.4f; v.send = 0.6f; } break;
    case CR_BUBBLING: for (int i = 0; i < 7; i++) { Voice& v = Tone(c, W_SINE, RR(f * 0.7f, f * 1.4f), 0, 0.06f, g, i * RR(0.04f, 0.09f)); v.f1 = v.f0 * 1.8f; v.curve = 0.6f; v.decPow = 2; } break;
    case CR_CHIME: { const float st[3] = {1, 1.26f, 1.5f}; for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_FM, f * st[i], f * st[i], dur, g * 0.6f, i * 0.07f); v.fmRatio = 4; v.fmIndex = 1.2f; v.fmIndex1 = 0.1f; v.decPow = 1.8f; v.send = 0.7f; } break; }
    case CR_RIPPLE: for (int i = 0; i < 4; i++) { Voice& v = Tone(c, W_SINE, f * (1.3f - i * 0.1f), f * (1.0f - i * 0.1f), dur * 0.6f, g * 0.5f, i * 0.06f); v.tremR = 11; v.tremD = 0.5f; v.decPow = 1.6f; v.send = 0.8f; } break;
    case CR_HEART: for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SINE, f, f * 0.6f, 0.18f, g * (k ? 0.7f : 1), k * 0.22f); v.curve = 0.4f; v.decPow = 2.2f; } break;
    case CR_GRUNT: { Voice& v = Vox(c, f, f * 0.8f, dur, g, 600, 480, 1050, 900); v.atk = 0.01f; v.decPow = 1.6f; Puff(c, 1200, 500, 0.06f, g * 0.3f); break; }
    case CR_FALL: { Voice& v = Vox(c, f, f * 0.5f, dur, g, 700, 400, 1100, 800); v.atk = 0.02f; v.decPow = 1.3f; v.send = 0.6f; Thud(c, 60, g * 0.8f, dur * 0.8f); break; }
    case CR_STAB: { const float st[3] = {1, 1.19f, 1.5f}; for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_SAW, f * st[i], 0, dur, g * 0.5f); v.f1 = v.f0; v.det = 0.006f; v.cut0 = 3500; v.cut1 = 900; v.atk = 0.005f; v.decPow = 1.8f; v.send = 0.5f; } Puff(c, 6000, 2000, 0.05f, g * 0.4f); break; }
    case CR_GONG: { Voice& v = Tone(c, W_FM, f, f * 0.98f, dur, g); v.fmRatio = 1.41f; v.fmIndex = 3; v.fmIndex1 = 0.4f; v.atk = 0.005f; v.decPow = 1.5f; v.send = 0.8f; Thud(c, 55, g * 0.8f); break; }
    case CR_SWELL: { for (int i = 0; i < 3; i++) { Voice& v = Tone(c, W_SAW, f * (i ? (i == 1 ? 1.5f : 2.02f) : 1), 0, dur, g * 0.45f); v.f1 = v.f0; v.det = 0.008f; v.cut0 = 300; v.cut1 = 2400; v.atk = dur * 0.6f; v.decPow = 1.2f; v.send = 0.7f; } Voice& n = Puff(c, 300, 3000, dur, g * 0.3f); n.atk = dur * 0.7f; break; }
    case CR_PHASE: { const float st[4] = {1, 1.0595f, 1.4142f, 2.12f}; for (int i = 0; i < 4; i++) { Voice& v = Tone(c, W_SAW, f * st[i], 0, dur, g * 0.35f, i * 0.03f); v.f1 = v.f0 * 0.97f; v.det = 0.01f; v.cut0 = 2500; v.cut1 = 500; v.tremR = 6; v.tremD = 0.4f; v.decPow = 1.2f; v.send = 0.8f; } Voice& gg = Tone(c, W_FM, f * 0.5f, f * 0.49f, dur, g * 0.5f); gg.fmRatio = 1.41f; gg.fmIndex = 3; gg.decPow = 1.5f; gg.send = 0.8f; break; }
    default: break;
    }
    gTagCue = -1; gTagInst = 0; gTagPrio = 0;
    if (d.duck) gDuck = 1;
}

// ---------------------------------------------------------------- the salon: a Verne-era waltz and the ship's bed
// Harmonium and music box in 3/4, D minor turning to F: an oom-pah-pah under a composed sixteen-bar tune that
// varies each time round (up an octave, a breath of rests, a counter-line). An open station adds its motif; when a
// crew member has died the waltz stops and the organ plays alone.
struct HubMusic { bool on = false; int station = -1; bool mourning = false; float s = 0; int tick = 0; float tickT = 0; };
HubMusic gHub;
Biquad gDeckL, gDeckR;       // the Study: the waltz heard through the deck
struct HubBed { float ph = 0; Biquad flt; };
HubBed gHubHum, gHubWater;
float gHubAmbT = 0, gClockT = 0;
float Midi(float m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }
const int WALTZ_CHORD[16][3] = {{50, 53, 57}, {55, 58, 62}, {50, 53, 57}, {50, 53, 57}, {48, 52, 55}, {50, 53, 57}, {48, 52, 55}, {53, 57, 60},
                                {55, 58, 62}, {50, 53, 57}, {57, 61, 64}, {50, 53, 57}, {53, 57, 60}, {55, 58, 62}, {57, 61, 64}, {50, 53, 57}};
struct MNote { int m; int beats; };
const MNote WALTZ_TUNE[] = {{69, 3}, {74, 1}, {72, 1}, {70, 1}, {69, 2}, {67, 1}, {65, 3}, {64, 1}, {65, 1}, {67, 1}, {69, 2}, {62, 1}, {64, 1}, {65, 1}, {67, 1}, {69, 3},
                            {70, 1}, {69, 1}, {67, 1}, {65, 2}, {69, 1}, {67, 1}, {65, 1}, {64, 1}, {62, 3}, {65, 1}, {67, 1}, {69, 1}, {70, 2}, {74, 1}, {73, 2}, {76, 1}, {74, 3}};
float HubTickDur() { return 60.0f / 84.0f / 2; } // eighth notes of a waltz at 84 to the beat
void Harmonium(float f, float dur, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.45f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SAW, f, f, dur, gain); v.det = 0.004f; v.cut0 = v.cut1 = 1500; v.q = 0.9f; v.atk = std::min(0.09f, dur * 0.3f); v.decPow = 0.7f; v.tremR = 5.5f; v.tremD = 0.08f;
}
void MusicBox(float f, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.55f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_FM, f, f, 1.7f, gain); v.fmRatio = 5.4f; v.fmIndex = 1.5f; v.fmIndex1 = 0.08f; v.atk = 0.002f; v.decPow = 2.3f;
}
void OrganChord(const int* ch, float dur, float gain) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.7f};
    for (int i = -1; i < 3; i++) {
        float f = Midi(i < 0 ? ch[0] - 12 : ch[i]);
        for (int h = 1; h <= 2; h++) { PanGains(i * 0.25f, c.gl, c.gr); Voice& v = Tone(c, W_SINE, f * h, f * h, dur, gain / h); v.atk = dur * 0.25f; v.decPow = 0.6f; v.vibR = 4.5f; v.vibD = 0.002f; }
    }
}
void HubTick() {
    int t = gHub.tick, bar = (t / 6) % 16, cycle = t / 96, step = t % 6;
    const int* ch = WALTZ_CHORD[bar];
    if (gHub.mourning) { // the organ alone, slow
        if (t % 12 == 0) OrganChord(ch, HubTickDur() * 12 * 1.05f, 0.03f);
        return;
    }
    // oom-pah-pah
    if (step == 0) Harmonium(Midi(ch[0] - 12), HubTickDur() * 2.6f, 0.07f, -0.1f);
    if (step == 2 || step == 4) for (int i = 0; i < 3; i++) Harmonium(Midi(ch[i] + (i == 0 ? 12 : 0)), HubTickDur() * 1.1f, 0.025f, 0.15f);
    // the tune on the music box: which note starts on this eighth?
    bool breath = cycle % 3 == 2 && bar >= 8 && bar < 12;
    int at = 0;
    for (const MNote& n : WALTZ_TUNE) {
        if (at == (t % 96) && !breath) MusicBox(Midi(n.m + 12 + (cycle % 2 ? 12 : 0)), 0.065f, 0.25f);
        at += n.beats * 2;
    }
    // the open station's motif
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f};
    switch ((Scene)gHub.station) {
    case Scene::SickLeave: if (t % 12 == 0) OrganChord(ch, HubTickDur() * 12, 0.012f); break;
    case Scene::Radar: { PanGains(0.4f, c.gl, c.gr); Voice& v = Tone(c, W_SINE, Midi(ch[step % 3] + 24), 0, 0.35f, 0.018f); v.f1 = v.f0; v.decPow = 2; v.send = 0.8f; break; }
    case Scene::Cards: { Voice& v = Puff(c, 7000, 4000, 0.03f, step == 0 ? 0.05f : 0.025f); v.hp = 2500; break; }
    case Scene::Workshop: if (step == 0) { Voice& v = Tone(c, W_FM, 1320, 1320, 0.5f, 0.02f); v.fmRatio = 2.76f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2.5f; } break;
    case Scene::Helm: if (step == 0) { Voice& v = Tone(c, W_SAW, Midi(ch[0] - 12), 0, HubTickDur() * 6, 0.02f); v.f1 = v.f0; v.cut0 = v.cut1 = 500; v.atk = 0.2f; v.decPow = 0.8f; } break;
    case Scene::Bookshelf: if (bar % 2 == 0) { Voice& v = Tone(c, W_PLUCK, Midi(ch[step % 3] + 24), 0, 0.8f, 0.03f); v.f1 = v.f0; v.decPow = 1; } break;
    case Scene::Crew: { at = 0; for (const MNote& n : WALTZ_TUNE) { if (at == (t % 96)) { Voice& v = Tone(c, W_SINE, Midi(n.m + 12), 0, n.beats * HubTickDur() * 1.8f, 0.012f); v.f1 = v.f0; v.vibR = 5; v.vibD = 0.01f; v.atk = 0.05f; } at += n.beats * 2; } break; }
    case Scene::Ward: if (step == 3) MusicBox(Midi(ch[1] + 12), 0.018f, -0.3f); break;
    case Scene::Arcade: { at = 0; for (const MNote& n : WALTZ_TUNE) { if (at == (t % 96)) { Voice& v = Tone(c, W_SQR, Midi(n.m + 24), 0, n.beats * HubTickDur() * 1.2f, 0.01f); v.f1 = v.f0; v.cut0 = v.cut1 = 3000; } at += n.beats * 2; } break; }
    default: break;
    }
}
// the salon's little sounds: the wall clock, the hull creaking with the roll, footsteps of the hands overhead
void HubEvents(float dt) {
    gClockT += dt;
    if (gClockT >= 1.0f) { gClockT -= 1.0f; static int tk = 0; BuildCue(FindCue("amb.clock"), 0.35f + 0.1f * (tk++ % 2), 0.55f); }
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    if (chance(0.12f)) BuildCue(FindCue("amb.creak"), RR(0.4f, 0.9f), RR(-0.8f, 0.8f));
    if (chance(0.05f)) { float p = RR(-0.9f, 0.9f); for (int k = 0; k < GetRandomValue(3, 6); k++) { int ci = FindCue("amb.step"); if (ci >= 0) { gTagCue = ci; Ctx c{0.3f, 1, 1, 1, B_AMB, 0.4f}; PanGains(p, c.gl, c.gr); Voice& v = Tone(c, W_SINE, 120, 72, 0.12f, 0.08f, k * 0.45f); v.decPow = 2.5f; gTagCue = -1; } } }
    if ((gHub.mourning || (Scene)gHub.station == Scene::SickLeave) && chance(0.1f)) BuildCue(FindCue("amb.organbreath"), 0.6f, -0.5f);
}
// the Study bus: the soundscape mixer, faded in over the Master Reference's 1.5 s as the salon fades out
bool gStudyOn = false; float gStudyS = 0; float gStudyBuf[CTRL * 2];
void Render(float* out, int frames) {
    EnsureRev();
    if (gConv.room != gRoomWant) gConv.Build(gRoomWant);
    const float dtS = 1.0f / SR;
    float muL = 0, muR = 0; (void)muL; (void)muR;
    for (int base = 0; base < frames; base += CTRL) {
        int n = std::min(CTRL, frames - base);
        float blockT = n * dtS;
        gStudyS = std::clamp(gStudyS + (gStudyOn ? 1.0f : -1.0f) * blockT / study::Numbers().crossfadeSeconds, 0.0f, 1.0f);
        if (gStudyS > 0.0005f) study::Render(gStudyBuf, n, SR);
        // ---- control rate: scene fade, the score's clock, ambient events, loops
        gClock += blockT;
        gScene += (gSceneTarget - gScene) * std::min(1.0f, blockT * 0.8f);
        if (gLevel >= 0 && gSceneTarget > 0) {
            gTickT += blockT;
            float td = TickDur();
            if (gSong.tpbeat == 6 && (gTick % 2) == 1) td *= 1.0f; // (straight 16ths)
            while (gTickT >= td) { gTickT -= td; MusicTick(); gTick++; }
            gAmbT += blockT;
            if (gAmbT >= 0.05f) { AmbientEvents(gAmbT); gAmbT = 0; }
        }
        gHub.s += ((gHub.on ? 1.0f : 0.0f) - gHub.s) * std::min(1.0f, blockT * 0.7f);
        ExpUpdate(blockT);
        RtUpdate(blockT);
        if (gHub.on) {
            gHub.tickT += blockT;
            while (gHub.tickT >= HubTickDur()) { gHub.tickT -= HubTickDur(); HubTick(); gHub.tick++; }
            gHubAmbT += blockT;
            if (gHubAmbT >= 0.05f) { HubEvents(gHubAmbT); gHubAmbT = 0; }
        }
        gDuck = std::max(0.0f, gDuck - blockT * 2.0f);
        { bool speaking = false; for (auto& v : gV) if (v.on && v.bus == B_VOICE) { speaking = true; break; } gVoiceS += ((speaking ? 1.0f : 0.0f) - gVoiceS) * std::min(1.0f, blockT * 8); }
        bool deck = gHub.on && (Scene)gHub.station == Scene::Study;
        gDeckL.Set(0, deck ? 500.0f : 18000.0f, 0.707f); gDeckR.Set(0, deck ? 500.0f : 18000.0f, 0.707f);
        gHubWater.flt.Set(0, 260 + 80 * sinf(gClock * 0.21f), 0.8f);
        gFlowS += (gFlow - gFlowS) * std::min(1.0f, blockT * 3);
        gTensionS += (gTension - gTensionS) * std::min(1.0f, blockT * 0.5f); // tension comes on over a couple of seconds and ebbs slowly
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
        float voiceDuck = 1 - 0.37f * gVoiceS;   // a voice ducks everything else 4 dB
        float roomS = std::max({gHub.s, gExp.s, gRt.s, gTestBusOpen});  // aboard, or on an expedition: the generated rooms
        float musicLevel = std::max(gScene, roomS) * (1 - 0.29f * gDuck); // combat impacts duck the music 3 dB
        float busG[5] = {gVol.sfx * voiceDuck, gVol.music * musicLevel * voiceDuck, gVol.ambience * std::max(gScene, roomS) * voiceDuck, gVol.sfx, gVol.sfx};
        for (int i = 0; i < n; i++) {
            float sL = 0, sR = 0, mL = 0, mR = 0, send = 0, uL = 0, uR = 0, musL = 0, musR = 0;
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
                case W_PLUCK: {
                    float* ks = gKs[&v - gV];
                    if (v.ksLen == 0) { v.ksLen = std::clamp((int)(SR / std::max(20.0f, f)), 2, 1023); for (int k = 0; k < v.ksLen; k++) ks[k] = Noise(); v.ksI = 0; }
                    s = ks[v.ksI];
                    int nx = v.ksI + 1 < v.ksLen ? v.ksI + 1 : 0;
                    ks[v.ksI] = (ks[v.ksI] + ks[nx]) * 0.5f * v.ksDecay;
                    v.ksI = nx;
                } break;
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
                else if (v.bus == B_UI || v.bus == B_VOICE) { uL += s * v.gl; uR += s * v.gr; }
                else if (v.bus == B_MUSIC) { musL += s * v.gl; musR += s * v.gr; }
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
            if (gRt.s > 0.002f) { float e = RtBedSample(gClock + i * dtS, base + i) * gVol.ambience * gRt.s * voiceDuck; mL += e; mR += e * 0.92f; send += e * 0.3f; }
            if (gExp.s > 0.002f) { float e = ExpBedSample(gClock + i * dtS, base + i) * gVol.ambience * gExp.s * voiceDuck; mL += e; mR += e * 0.92f; send += e * 0.3f; }
            // the music (heard through the deck from the Study)
            mL += gDeckL.Run(musL); mR += gDeckR.Run(musR);
            // the salon's bed: the engine's hum, and the sea against the great window
            if (gHub.s > 0.01f) {
                gHubHum.ph += 50 * dtS; if (gHubHum.ph >= 1) gHubHum.ph -= 1;
                float hum = (sinf(TAU * gHubHum.ph) + 0.4f * sinf(TAU * gHubHum.ph * 2) + 0.2f * sinf(TAU * gHubHum.ph * 3)) * 0.022f;
                float water = gHubWater.flt.Run(Noise()) * 0.05f * (0.6f + 0.4f * sinf(gClock * 0.33f));
                float amb = (hum + water) * gVol.ambience * gHub.s * voiceDuck;
                mL += amb; mR += amb * 0.9f + water * 0.01f;
                send += amb * 0.2f;
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
            // outside the parkour section, rooms ring with the generated convolution reverb instead
            float cw = roomS > gScene ? 1.0f : 0.0f, cL = 0, cR = 0;
            if (roomS > 0.002f) gConv.Run(send * cw, cL, cR);   // only while a room is sounding
            rL = rL * (1 - cw) + cL * 2.2f; rR = rR * (1 - cw) + cR * 2.2f;
            float L = (sL + mL + uL + rL * gRevWet) * gVol.master, R = (sR + mR + uR + rR * gRevWet) * gVol.master;
            if (gStudyS > 0.0005f) { float sg = gStudyS * gVol.master; L += gStudyBuf[i * 2] * sg; R += gStudyBuf[i * 2 + 1] * sg; }
            out[(base + i) * 2] = tanhf(L);
            out[(base + i) * 2 + 1] = tanhf(R);
        }
    }
}

} // namespace

AudioVolumes& Volumes() { return gVol; }

int CueIndex(const char* name) { return FindCue(name); }
static bool gCueSuppressed = false;
void SetAudioSuppressed(bool on) { gCueSuppressed = on; }   // EnemyBrain's lookahead plays fights out silently
void PlayCue(const char* name, float vol, float pan) {
    if (!gReady || gCueSuppressed) return;
    int i = FindCue(name);
    if (i >= 0) BuildCue(i, vol, pan);
}
void AudioHub(bool on, int station, bool mourning) {
    gHub.on = on; gHub.station = station; gHub.mourning = mourning;
    if (on) gRoomWant = RR_SALON;
}
void AudioStudy(bool on) { gStudyOn = on; }
void AudioRoom(int room) { gRoomWant = std::clamp(room, 0, RR_COUNT - 1); }
void AudioExpedition(const ExpAudio& a) {
    gExp.want = a;
    if (a.on) gRoomWant = EXP_PAL[std::clamp(a.loc, 0, 5)].room;
}
void AudioRedTide(const RtAudio& a) {
    gRt.want = a;
    if (a.on) gRoomWant = RT_PAL[std::clamp(a.map, 0, 4)].room;
}
void RedTideCue(int kind, float vol, float pan, float dist) { if (gReady && !gCueSuppressed) RtCueImpl(kind, vol, pan, dist); }
void RedTideBeast(const char* species, float size, int cue, float dist, float pan) { if (gReady && !gCueSuppressed && species) RtBeastImpl(species, size, cue, dist, pan); }
void RedTideQuip(int voice, int syllables, float pan) { if (gReady && !gCueSuppressed) RtQuipImpl(voice, syllables, pan); }
float AudioBeat() { return gBeat; }
void AudioReact(int kind) { if (gReady && !gCueSuppressed) ExpReact(kind); }
void CombatVoice(int enemyType, float size, int cue, float pan) {
    if (!gReady || gCueSuppressed) return;
    CombatVoiceImpl(enemyType, size, cue, pan);
}

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
    gSong = MakeSong(level); // a fresh arrangement: the motifs and patterns are rolled anew each time
    gRevFb = gSong.rev; gRevDamp = gSong.damp; gRevWet = gSong.wet;
    gTick = 0; gTickT = 0;
    for (auto& v : gV) if (v.bus != B_SFX) v.on = false;
    SetupBeds(level);
}
void AudioListener(Vector2 at) { gEar = at; }
void AudioDay(float d) { gDay = d; }
void AudioFlow(float a) { gFlow = std::clamp(a, 0.0f, 1.0f); }
void AudioSlide(float a) { gSlide = std::clamp(a, 0.0f, 1.0f); }
void AudioTension(float a) { gTension = std::clamp(a, 0.0f, 1.0f); }

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
        gTension = gTensionS = lv == PL_HULL ? 1.0f : 0.0f; // the Hull pass also plays the apex tension
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
    // the whole of each level's track, section by section: every section must sound, and the arc should rise and fall
    for (int lv = 0; lv <= PL_COUNT; lv++) {
        for (auto& v : gV) v.on = false;
        gLevel = -1; AudioLevel(lv); gSceneTarget = 1; gScene = 1; gTension = gTensionS = 0; gBeds.clear();
        float secDur = TickDur() * gSong.tpb * gSong.barsPerSection;
        int N = (int)(secDur * SR);
        std::vector<float> b(N * 2);
        printf("%-10s %4.1f min:", lv < PL_COUNT ? TextFormat("track %d", lv) : "abyss", secDur * gSong.sections / 60);
        for (int sct = 0; sct < gSong.sections; sct++) {
            for (int at = 0; at < N; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, N - at));
            double sum = 0; float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else { sum += x * x; pk = std::max(pk, fabsf(x)); } }
            float db = 20 * log10f(std::max(1e-6f, sqrtf((float)(sum / b.size()))));
            printf(" %.0f", db);
            if (bad || db < -50 || pk > 0.97f) { ok = false; printf("!"); }
            if (wavPath && (lv == PL_PIRATE || lv == PL_ISLAND) && sct % 3 == 2) all.insert(all.end(), b.begin(), b.begin() + std::min((size_t)b.size(), (size_t)SR * 2 * 20)); // 20 s samples of those two into the file
        }
        printf(" dB\n");
    }
    // every effect, and every beast voice for every cue, alone: none may come out silent or broken
    AudioLevel(PL_PIRATE); gSceneTarget = 0; gScene = 0;
    auto solo = [&](auto fire) { for (auto& v : gV) v.on = false; fire(); std::vector<float> b(SR * 2 * 2); for (int at = 0; at < SR * 2; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, SR * 2 - at)); float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else pk = std::max(pk, fabsf(x)); } return bad ? -1.0f : pk; };
    int silent = 0;
    for (int k = 0; k < (int)Sfx::COUNT; k++) { float pk = solo([&] { Sfx2D((Sfx)k); }); if (pk < 0.02f && (Sfx)k != Sfx::SoftLand) { printf("  effect %d is silent or broken (peak %.3f)\n", k, pk); silent++; } }
    for (const char* b : beasts) for (int cue = 0; cue < 7; cue++) { float pk = solo([&] { gCallCool = 0; BeastSound(b, 1.0f, cue, {0, 0}); }); if (pk < 0.01f && !(cue <= CUE_ALARM && strstr(b, "Fungus")) && !(cue == CUE_CALL && strstr(b, "Shark"))) { printf("  %s cue %d is silent or broken (peak %.3f)\n", b, cue, pk); silent++; } }
    printf("%d silent or broken voices\n", silent);
    if (silent) ok = false;
    // expeditions (stage 8): every location walking (lit, then dark and near the boss) and fighting (winning; a hero
    // in danger at Death's Door; the level boss in its second phase). Every state must sound and none may clip.
    gLevel = -1; gSceneTarget = 0; gScene = 0; gHub = HubMusic{};
    {
        const char* LOCN[6] = {"cave", "island", "weeds", "atlantis", "trench", "hadal"};
        auto expPass = [&](int loc, ExpAudio st, const char* label, float secs) {
            for (auto& v : gV) v.on = false;
            gExp = ExpState{}; gExp.want = st; gExp.s = 1; gExpBedLoc = -1; gRoomWant = EXP_PAL[loc].room;
            gExp.walkS = st.mode == 0 ? 1.0f : 0.0f; gExp.fightS = st.mode == 1 ? 1.0f : 0.0f;
            int N = (int)(SR * secs);
            std::vector<float> b(N * 2);
            for (int at = 0; at < N; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, N - at));
            double sum = 0; float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else { sum += x * x; pk = std::max(pk, fabsf(x)); } }
            float db = 20 * log10f(std::max(1e-6f, sqrtf((float)(sum / b.size()))));
            bool pass = !bad && db > -48 && pk < 0.97f;
            printf("expedition %-9s %-8s rms %5.1f dB  peak %.2f%s\n", LOCN[loc], label, db, pk, pass ? "" : "  FAIL");
            if (!pass) ok = false;
            if (wavPath && (loc == 1 || loc == 3)) all.insert(all.end(), b.begin(), b.end());
        };
        for (int loc = 0; loc < 6; loc++) {
            ExpAudio st; st.on = true; st.loc = loc;
            expPass(loc, st, "walk", 8);
            st.light = 0.12f; st.bossNear = 0.9f; expPass(loc, st, "dark", 8);
            st = ExpAudio{}; st.on = true; st.loc = loc; st.mode = 1; st.winning = true; expPass(loc, st, "fight", 8);
            st.winning = false; st.danger = true; st.door = true; expPass(loc, st, "danger", 8);
            st = ExpAudio{}; st.on = true; st.loc = loc; st.mode = 1; st.bossType = (int)LocationLevelBoss((Location)loc); st.phase2 = true; expPass(loc, st, "boss", 8);
        }
        gExp = ExpState{}; gExpBeds.clear(); gExpBedLoc = -1;
        // every enemy's voice, pain and death, alone
        int mute = 0;
        for (int t = 0; t < (int)EnemyType::COUNT; t++) for (int cue : {CUE_ALARM, CUE_PAIN, CUE_DEATH}) {
            float pk = solo([&] { CombatVoiceImpl(t, 1.5f, cue, 0); });
            if (pk < 0.01f) { printf("  enemy %d cue %d is silent or broken (peak %.3f)\n", t, cue, pk); mute++; }
        }
        printf("%d enemies x voice/pain/death: %d silent or broken\n", (int)EnemyType::COUNT, mute);
        if (mute) ok = false;
    }
    // Red Tide (stage 9d): every map in every state (the calm counting down, a tide early and past half quota with
    // blood and an apex near, a late Hunt, a boss in its second phase, a downed diver, the match over), every effect,
    // the four quip voices, and every species' voice, pain, death and feeding, near and muffled at 50 m
    {
        const char* MAPN[5] = {"ship", "cave", "reef", "atlantis", "void"};
        auto rtPass = [&](RtAudio st, const char* label, float secs) {
            for (auto& v : gV) v.on = false;
            gRt = RtState{}; gRt.want = st; gRt.s = 1; gRtBedMap = -1; gRoomWant = RT_PAL[st.map].room;
            gRt.calmS = st.mode == 0 ? 1.0f : 0.0f; gRt.tideS = st.mode == 1 || st.mode == 2 ? 1.0f : 0.0f;
            int N = (int)(SR * secs);
            std::vector<float> b(N * 2);
            for (int at = 0; at < N; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, N - at));
            double sum = 0; float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else { sum += x * x; pk = std::max(pk, fabsf(x)); } }
            float db = 20 * log10f(std::max(1e-6f, sqrtf((float)(sum / b.size()))));
            bool pass = !bad && db > -48 && pk < 0.97f;
            printf("red tide %-9s %-8s rms %5.1f dB  peak %.2f%s\n", MAPN[st.map], label, db, pk, pass ? "" : "  FAIL");
            if (!pass) ok = false;
            if (wavPath && (st.map == 0 || st.map == 2)) all.insert(all.end(), b.begin(), b.end());
        };
        for (int mp = 0; mp < 5; mp++) {
            RtAudio st; st.on = true; st.map = mp; st.countdown = 4.5f; rtPass(st, "calm", 6);
            st = RtAudio{}; st.on = true; st.map = mp; st.mode = 1; st.tide = 3; st.quota = 0.2f; rtPass(st, "tide", 8);
            st.quota = 0.7f; st.scent = 0.8f; st.predator = 0.8f; st.predatorPan = -0.6f; rtPass(st, "blood", 8);
            st = RtAudio{}; st.on = true; st.map = mp; st.mode = 2; st.tide = 22; st.hp = 0.3f; rtPass(st, "hunt", 8);
            st = RtAudio{}; st.on = true; st.map = mp; st.mode = 1; st.tide = 12; st.boss = true; st.bossPhase = 2; rtPass(st, "boss", 8);
            st = RtAudio{}; st.on = true; st.map = mp; st.mode = 1; st.tide = 8; st.downed = true; st.hp = 0; rtPass(st, "downed", 6);
            st = RtAudio{}; st.on = true; st.map = mp; st.mode = 3; st.tide = 14; rtPass(st, "over", 8);
        }
        gRt = RtState{}; gRtBeds.clear(); gRtBedMap = -1;
        int mute = 0;
        for (int k = 0; k < RTC_COUNT; k++) { float pk = solo([&] { RtCueImpl(k, 1, 0, 0); }); if (pk < 0.01f || pk > 0.97f) { printf("  red tide effect %d is %s (peak %.3f)\n", k, pk < 0.01f ? "silent" : "clipping", pk); mute++; } }
        for (int v = 0; v < 4; v++) { float pk = solo([&] { RtQuipImpl(v, 9, 0); }); if (pk < 0.01f || pk > 0.97f) { printf("  quip voice %d is silent or clipping (peak %.3f)\n", v, pk); mute++; } }
        int total = 0;
        float wet0 = gRevWet; gRevWet = 0;   // (the parkour reverb's tail from the passes above would mask the quiet voices,
        gFlow = gFlowS = 0; gSlide = gSlideS = 0;   // and so would the Pirate pass's current, left running)
        for (int k = 0; k < 3; k++) solo([&] {});
        float floorPk = solo([&] {});
        for (int mp = 0; mp < 5; mp++) {
            std::vector<std::pair<std::string, int>> sp;
            RedTideSpeciesForAudio(mp, sp);
            for (const auto& s : sp) for (int cue : {CUE_CALL, CUE_PAIN, CUE_DEATH, CUE_CHEW}) {
                total++;
                float nearPk = solo([&] { RtBeastImpl(s.first.c_str(), (float)s.second, cue, 5, 0); });
                float farPk = solo([&] { RtBeastImpl(s.first.c_str(), (float)s.second, cue, 50, 0); });
                bool noCall = cue == CUE_CALL && RtArchOf(s.first.c_str()) == A_SHARK;   // (a shark has no call: it is the water moving)
                if (!noCall && (nearPk < std::max(0.01f, floorPk * 2) || nearPk > 0.97f || farPk <= 0.0005f || farPk >= nearPk)) { printf("  %s (%s) cue %d: near %.3f far %.3f\n", s.first.c_str(), MAPN[mp], cue, nearPk, farPk); mute++; }
            }
        }
        gRevWet = wet0;
        printf("red tide: %d species cues, %d effects, 4 quip voices: %d silent, clipping or unmuffled (floor %.4f)\n", total, (int)RTC_COUNT, mute, floorPk);
        if (mute) ok = false;
    }
    // the salon: the waltz and its bed, mourning, and each station's motif; then every registered cue on its own    gLevel = -1; gSceneTarget = 0; gScene = 0;
    auto hubPass = [&](const char* label, int station, bool mourning, float secs) {
        for (auto& v : gV) v.on = false;
        gHub = HubMusic{}; gHub.on = true; gHub.station = station; gHub.mourning = mourning; gHub.s = 1;
        int N = (int)(SR * secs);
        std::vector<float> b(N * 2);
        for (int at = 0; at < N; at += BLOCK) Render(&b[at * 2], std::min(BLOCK, N - at));
        double sum = 0; float pk = 0; int bad = 0; for (float x : b) { if (!std::isfinite(x)) bad++; else { sum += x * x; pk = std::max(pk, fabsf(x)); } }
        float db = 20 * log10f(std::max(1e-6f, sqrtf((float)(sum / b.size()))));
        bool pass = !bad && db > -45 && pk < 0.97f;
        printf("salon %-12s rms %5.1f dB  peak %.2f%s\n", label, db, pk, pass ? "" : "  FAIL");
        if (!pass) ok = false;
        if (wavPath && station < 0 && !mourning) all.insert(all.end(), b.begin(), b.end());
    };
    hubPass("waltz", -1, false, 20);
    hubPass("mourning", -1, true, 12);
    const Scene stations[] = {Scene::Helm, Scene::Crew, Scene::Radar, Scene::Ward, Scene::SickLeave, Scene::Bookshelf, Scene::Workshop, Scene::Cards, Scene::Arcade, Scene::Study};
    const char* stationNames[] = {"helm", "crew", "radar", "ward", "sick bay", "library", "workshop", "cards", "arcade", "study"};
    for (int i = 0; i < 10; i++) hubPass(stationNames[i], (int)stations[i], false, 8);
    gHub = HubMusic{};
    int nc; const CueDef* cues = CueTable(nc);
    int silentCues = 0;
    gFlow = gFlowS = 0; gSlide = gSlideS = 0; gTestBusOpen = 1;   // (the music and ambience cues play on buses that only a scene opens)
    for (int i = 0; i < nc; i++) {
        float pk = solo([&] { BuildCue(i, 1, 0); });
        if (pk < 0.01f || pk > 0.97f) { printf("  cue %s is %s (peak %.3f)\n", cues[i].name, pk < 0.01f ? "silent" : "clipping", pk); silentCues++; }
    }
    gTestBusOpen = 0;
    printf("%d registered cues, %d silent or clipping\n", nc, silentCues);
    if (silentCues) ok = false;
    if (wavPath) {
        std::vector<short> pcm(all.size());
        for (size_t i = 0; i < all.size(); i++) pcm[i] = (short)(std::clamp(all[i], -1.0f, 1.0f) * 32000);
        ::Wave w{(unsigned)(all.size() / 2), SR, 16, 2, pcm.data()};
        ExportWave(w, wavPath);
    }
    gReady = false;
    return ok;
}