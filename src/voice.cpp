// Voice chat (see voice.h): the codec, the microphone, the gate, the speakers' jitter buffers and their shaping.
// No raylib here: miniaudio's header pulls in windows.h. The miniaudio functions themselves are raylib's (raudio.c
// builds the implementation with these same switches, so the structures match).
#define MA_NO_JACK
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MA_COINIT_VALUE 2
#include "external/miniaudio.h"
#undef PlaySound

#include "voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace voice {

// ---------------------------------------------------------------- the codec (IMA ADPCM)
namespace {
const int IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
int gEncIndex = 0;   // the encoder's step carries over between frames (the header tells the decoder where it starts)
inline int Delta(int code, int idx) {
    int step = STEP[idx], d = step >> 3;
    if (code & 4) d += step;
    if (code & 2) d += step >> 1;
    if (code & 1) d += step >> 2;
    return code & 8 ? -d : d;
}
}  // namespace

void Encode(const int16_t* pcm, uint8_t* out) {
    int pred = pcm[0], idx = gEncIndex;
    out[0] = (uint8_t)(pred & 0xFF); out[1] = (uint8_t)((pred >> 8) & 0xFF); out[2] = (uint8_t)idx; out[3] = 0;
    uint8_t* body = out + 4;
    memset(body, 0, FRAME / 2);
    for (int i = 1; i < FRAME; i++) {
        int diff = pcm[i] - pred, code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        int step = STEP[idx];
        if (diff >= step) { code |= 4; diff -= step; }
        step >>= 1; if (diff >= step) { code |= 2; diff -= step; }
        step >>= 1; if (diff >= step) code |= 1;
        pred = std::clamp(pred + Delta(code, idx), -32768, 32767);
        idx = std::clamp(idx + IDX[code], 0, 88);
        int k = i - 1;
        body[k >> 1] |= (uint8_t)(code << ((k & 1) * 4));
    }
    gEncIndex = idx;
}
bool Decode(const uint8_t* in, size_t n, int16_t* pcm) {
    if (n < (size_t)ENC_BYTES || in[2] > 88) return false;
    int pred = (int16_t)(in[0] | (in[1] << 8)), idx = in[2];
    const uint8_t* body = in + 4;
    pcm[0] = (int16_t)pred;
    for (int i = 1; i < FRAME; i++) {
        int k = i - 1, code = (body[k >> 1] >> ((k & 1) * 4)) & 15;
        pred = std::clamp(pred + Delta(code, idx), -32768, 32767);
        idx = std::clamp(idx + IDX[code], 0, 88);
        pcm[i] = (int16_t)pred;
    }
    return true;
}

// ---------------------------------------------------------------- the microphone
namespace {
ma_device gDev;
bool gMicOpen = false;
std::mutex gMicMx;
std::vector<int16_t> gMicBuf;   // captured, not yet taken (capped at 2 s: a stalled game drops the oldest)
void MicCallback(ma_device*, void*, const void* in, ma_uint32 frames) {
    if (!in) return;
    std::lock_guard<std::mutex> lk(gMicMx);
    const int16_t* s = (const int16_t*)in;
    gMicBuf.insert(gMicBuf.end(), s, s + frames);
    if (gMicBuf.size() > (size_t)RATE * 2) gMicBuf.erase(gMicBuf.begin(), gMicBuf.end() - RATE * 2);
}
}  // namespace

bool MicOpen(std::string* err) {
    if (gMicOpen) return true;
    ma_device_config c = ma_device_config_init(ma_device_type_capture);
    c.capture.format = ma_format_s16;
    c.capture.channels = 1;
    c.sampleRate = RATE;
    c.periodSizeInMilliseconds = 20;
    c.dataCallback = MicCallback;
    if (ma_device_init(nullptr, &c, &gDev) != MA_SUCCESS) { if (err) *err = "no microphone could be opened"; return false; }
    if (ma_device_start(&gDev) != MA_SUCCESS) { ma_device_uninit(&gDev); if (err) *err = "the microphone wouldn't start"; return false; }
    gMicOpen = true;
    std::lock_guard<std::mutex> lk(gMicMx);
    gMicBuf.clear();
    return true;
}
void MicClose() {
    if (!gMicOpen) return;
    ma_device_uninit(&gDev);
    gMicOpen = false;
    std::lock_guard<std::mutex> lk(gMicMx);
    gMicBuf.clear();
}
bool MicIsOpen() { return gMicOpen; }
std::string MicName() { return gMicOpen ? std::string(gDev.capture.name) : std::string(); }
int MicFrames(std::vector<int16_t>& out) {
    std::lock_guard<std::mutex> lk(gMicMx);
    int n = (int)(gMicBuf.size() / FRAME);
    if (n <= 0) return 0;
    out.insert(out.end(), gMicBuf.begin(), gMicBuf.begin() + n * FRAME);
    gMicBuf.erase(gMicBuf.begin(), gMicBuf.begin() + n * FRAME);
    return n;
}
void MicFeed(const int16_t* pcm, int n) {
    std::lock_guard<std::mutex> lk(gMicMx);
    gMicBuf.insert(gMicBuf.end(), pcm, pcm + n);
}

// ---------------------------------------------------------------- the gate
float FrameLevel(const int16_t* pcm) {
    double e = 0;
    for (int i = 0; i < FRAME; i++) e += (double)pcm[i] * pcm[i];
    float rms = (float)sqrt(e / FRAME) / 32768.0f;
    float db = 20 * log10f(std::max(rms, 1e-6f));
    return std::clamp((db + 60) / 60, 0.0f, 1.0f);
}
bool Gate::Step(const int16_t* pcm, bool pushToTalk, bool openMic, float sensitivity) {
    if (!openMic) { open = pushToTalk; hold = 0; return open; }
    // open mic: the level must clear a threshold the sensitivity sets (more sensitive: a lower threshold)
    float thr = 0.75f - 0.5f * std::clamp(sensitivity, 0.0f, 1.0f);   // (-45 dBFS at full sensitivity, -15 at none)
    if (FrameLevel(pcm) >= thr) { open = true; hold = 0.3f; }
    else if (hold > 0) { hold -= FRAME / (float)RATE; open = hold > 0; }
    else open = false;
    return open || pushToTalk;
}

// ---------------------------------------------------------------- the speakers
namespace {
struct OnePole {   // a one-pole low-pass (hp = true: the high-pass that's left)
    float z = 0;
    float Run(float x, float a) { z += a * (x - z); return z; }
};
struct Speaker {
    std::map<uint16_t, std::vector<int16_t>> frames;   // decoded, waiting
    bool playing = false; uint16_t next = 0;
    std::vector<int16_t> cur, last; int curPos = 0;   // the frame being played (16 kHz), and the one before (concealment)
    double phase = 0;                                  // the resampler's position in cur, in 16 kHz samples
    float prev = 0, curS = 0;                          // the two samples the resampler blends
    Hearing want, shown;                               // what the game asked, and the eased value the mixer uses
    float lapse = 0;                                   // seconds since the game last set want
    float heardT = 1, level = 0;                       // since the last frame played; its loudness
    OnePole lp, lp2, hpL, bub; float crackle = 0, bubPh = 0; int noise = 1;
    std::vector<float> ghostBuf; int ghostPos = 0;
    int concealed = 0;
};
std::map<int, Speaker> gSp;
Stats gStats;
inline bool SeqBefore(uint16_t a, uint16_t b) { return (int16_t)(a - b) < 0; }
// the earliest frame waiting, in the sequence's own order (it wraps at 65535: the map's order isn't it)
uint16_t Earliest(const std::map<uint16_t, std::vector<int16_t>>& m) { uint16_t e = m.begin()->first; for (auto& kv : m) if (SeqBefore(kv.first, e)) e = kv.first; return e; }
float Rnd(int& s) { s = s * 1103515245 + 12345; return ((s >> 8) & 0xFFFF) / 32768.0f - 1; }
}  // namespace

Stats& GetStats() { return gStats; }

void Receive(int speaker, uint16_t seq, const uint8_t* data, size_t n) {
    std::vector<int16_t> pcm(FRAME);
    if (!Decode(data, n, pcm.data())) return;
    Speaker& s = gSp[speaker];
    if (s.playing && SeqBefore(seq, s.next)) { gStats.framesLate++; return; }   // (its moment has passed)
    s.frames[seq] = std::move(pcm);
    gStats.framesHeard++;
    while (s.frames.size() > 12) s.frames.erase(Earliest(s.frames));   // (far behind: catch up rather than lag)
}
void SetHearing(int speaker, const Hearing& h) { Speaker& s = gSp[speaker]; s.want = h; s.lapse = 0; if (!s.playing) s.shown = h; }   // (eased only while a voice plays: between words it simply changes)
bool Speaking(int speaker) { auto it = gSp.find(speaker); return it != gSp.end() && it->second.heardT < 0.15f && it->second.level > 0.05f; }
float Level(int speaker) { auto it = gSp.find(speaker); return it == gSp.end() ? 0.0f : it->second.level; }
bool AnyActive() { for (auto& kv : gSp) if (kv.second.playing && kv.second.shown.gain > 0.05f) return true; return false; }
void Tick(float dt) {
    for (auto& kv : gSp) {
        Speaker& s = kv.second;
        s.lapse += dt;
        if (s.lapse > 0.5f) s.want = Hearing{};   // (nobody's shaping it now: plain, as in the lobby)
        (void)0;
    }
}
void Reset() { gSp.clear(); gEncIndex = 0; }

// the next 16 kHz frame for a speaker: in order, or concealed (the last one, fading) when one is missing
static bool NextFrame(Speaker& s) {
    if (!s.playing) {
        if (s.frames.size() < 3) return false;           // 60 ms in hand before the first word plays
        s.playing = true; s.next = Earliest(s.frames); s.concealed = 0;
    }
    auto it = s.frames.find(s.next);
    if (it != s.frames.end()) {
        s.last = s.cur; s.cur = std::move(it->second); s.frames.erase(it);
        s.next++; s.concealed = 0;
        return true;
    }
    // missing: anything later waiting? then this one was lost (repeat the last, quieter); nothing at all: the talk ended
    if (s.frames.empty() || s.concealed >= 3) { s.playing = false; s.frames.clear(); return false; }
    gStats.framesLost++; gStats.concealed++;
    std::vector<int16_t> c = s.cur.empty() ? std::vector<int16_t>(FRAME, 0) : s.cur;
    for (auto& v : c) v = (int16_t)(v * 0.5f);
    s.last = s.cur; s.cur = c; s.next++; s.concealed++;
    // skip past a gap the queue has jumped over
    if (!s.frames.empty() && SeqBefore(Earliest(s.frames), s.next)) s.next = Earliest(s.frames);
    return true;
}

void Render(float* out, int n, int sr) {
    memset(out, 0, sizeof(float) * n * 2);
    const float step = (float)RATE / sr, dtS = 1.0f / sr;
    for (auto& kv : gSp) {
        Speaker& s = kv.second;
        // ease the hearing so a turn of the head or a door doesn't click
        float k = std::min(1.0f, n * dtS * 12);
        auto ease = [&](float& a, float b) { a += (b - a) * k; };
        ease(s.shown.gain, s.want.gain); ease(s.shown.pan, s.want.pan); ease(s.shown.muffle, s.want.muffle);
        ease(s.shown.radio, s.want.radio); ease(s.shown.bubble, s.want.bubble); ease(s.shown.ghost, s.want.ghost);
        const Hearing& h = s.shown;
        float cut = 7000 * powf(400.0f / 7000.0f, std::clamp(h.muffle + h.bubble * 0.6f, 0.0f, 1.0f));   // the low-pass
        float a = 1 - expf(-2 * 3.14159f * cut / sr);
        float aRadioLo = 1 - expf(-2 * 3.14159f * 2800.0f / sr), aRadioHi = 1 - expf(-2 * 3.14159f * 350.0f / sr);
        float aBub = 1 - expf(-2 * 3.14159f * 6.0f / sr);
        float gl = cosf((h.pan + 1) * 0.25f * 3.14159f) * 1.414f, gr = sinf((h.pan + 1) * 0.25f * 3.14159f) * 1.414f;
        if (h.ghost > 0.01f && s.ghostBuf.empty()) s.ghostBuf.assign((size_t)(sr * 0.085f), 0.0f);
        float energy = 0;
        s.heardT += n * dtS;   // (the clocks run with the sound itself, not the frame rate)
        for (int i = 0; i < n; i++) {
            // the resampler: linear between 16 kHz samples
            s.phase += step;
            while (s.phase >= 1) {
                s.phase -= 1;
                s.prev = s.curS;
                if (s.cur.empty() || s.curPos >= (int)s.cur.size()) {
                    if (NextFrame(s)) { s.curPos = 0; s.heardT = 0; }
                    else { s.cur.clear(); s.curPos = 0; }
                }
                s.curS = (!s.cur.empty() && s.curPos < (int)s.cur.size()) ? s.cur[s.curPos++] / 32768.0f : s.curS * 0.98f;
            }
            float v = s.prev + (s.curS - s.prev) * (float)s.phase;
            energy += v * v;                                                    // (the voice itself: a mouth moves even out of earshot)
            if (h.gain < 0.001f) continue;
            // the shaping
            v = s.lp.Run(v, a);
            if (h.radio > 0.01f) {
                float b = s.lp2.Run(v, aRadioLo); b -= s.hpL.Run(b, aRadioHi);   // the band a little speaker passes
                b = tanhf(b * 3) * 0.5f;                                        // and crushes
                if (s.crackle <= 0 && Rnd(s.noise) > 0.9996f) s.crackle = 0.02f + 0.03f * fabsf(Rnd(s.noise));
                if (s.crackle > 0) { s.crackle -= dtS; b += Rnd(s.noise) * 0.08f; }
                b += Rnd(s.noise) * 0.004f;                                     // the carrier's hiss
                v = v * (1 - h.radio) + b * h.radio * 1.4f;
            }
            if (h.bubble > 0.01f) {
                s.bubPh += dtS * (9 + 6 * s.bub.Run(Rnd(s.noise), aBub));
                float wob = 0.65f + 0.35f * sinf(s.bubPh * 6.283f);             // the voice breaks into bubbles
                v *= 1 - h.bubble * (1 - wob);
            }
            if (h.ghost > 0.01f && !s.ghostBuf.empty()) {
                float d = s.ghostBuf[s.ghostPos];
                s.ghostBuf[s.ghostPos] = v + d * 0.62f;                         // a cold, ringing hollow
                s.ghostPos = (s.ghostPos + 1) % (int)s.ghostBuf.size();
                v = v * (1 - 0.6f * h.ghost) + d * 0.9f * h.ghost;
            }
            v *= h.gain;
            out[i * 2] += v * gl;
            out[i * 2 + 1] += v * gr;
        }
        float lv = std::clamp((20 * log10f(std::max(sqrtf(energy / std::max(1, n)), 1e-6f)) + 50) / 50, 0.0f, 1.0f);
        s.level += (lv - s.level) * 0.3f;
    }
}

}  // namespace voice
