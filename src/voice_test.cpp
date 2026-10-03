// depth.exe --voice-test: the codec, the gate, the speakers' jitter buffers and shaping, and the arcade session's
// relay (a host and two guests over the in-memory transport). The microphone itself isn't needed: MicFeed stands in.
#include "voice.h"
#include "arcade_session.h"
#include "net.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace voice {

static std::vector<int16_t> Speech(int frames, float amp, uint32_t seed) {
    // something speech-like: a voiced buzz at a wandering pitch through two moving formant-ish peaks, with syllables
    std::vector<int16_t> v((size_t)frames * FRAME);
    double ph = 0; uint32_t s = seed;
    for (size_t i = 0; i < v.size(); i++) {
        float t = i / (float)RATE;
        float f0 = 120 + 30 * sinf(t * 2.1f);
        ph += f0 / RATE;
        float buzz = 0;
        for (int h = 1; h < 25; h++) {
            float fh = f0 * h, a1 = expf(-powf((fh - (600 + 250 * sinf(t * 3))) / 220, 2)), a2 = expf(-powf((fh - (1800 + 500 * sinf(t * 2.3f))) / 300, 2));
            buzz += (0.25f / h + a1 + 0.6f * a2) * sinf((float)(6.2831853 * ph * h));
        }
        s = s * 1664525u + 1013904223u;
        float noise = ((s >> 9) / 8388608.0f - 1) * 0.05f;
        float env = 0.5f + 0.5f * sinf(t * 6.2831853f * 3.5f);   // syllables
        v[i] = (int16_t)std::max(-32767.0f, std::min(32767.0f, (buzz * 0.12f + noise) * env * amp * 32767));
    }
    return v;
}

int RunVoiceTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Voice chat\n");
    // ---- the codec
    {
        std::vector<int16_t> in = Speech(50, 0.6f, 7), out(in.size());
        double sig = 0, err = 0;
        for (int f = 0; f < 50; f++) {
            uint8_t enc[ENC_BYTES];
            Encode(&in[(size_t)f * FRAME], enc);
            Decode(enc, sizeof enc, &out[(size_t)f * FRAME]);
        }
        for (size_t i = 0; i < in.size(); i++) { sig += (double)in[i] * in[i]; err += (double)(in[i] - out[i]) * (in[i] - out[i]); }
        double snr = 10 * log10(sig / std::max(err, 1.0));
        check(snr > 18, "IMA ADPCM at 16 kHz keeps speech clear (SNR " + std::to_string((int)snr) + " dB; 4 bits a sample, " + std::to_string(ENC_BYTES) + " bytes a 20 ms frame)");
        uint8_t bad[ENC_BYTES] = {}; bad[2] = 200; int16_t pcm[FRAME];
        check(!Decode(bad, sizeof bad, pcm) && !Decode(bad, 10, pcm), "a garbled or short frame is refused");
    }
    // ---- the gate
    {
        std::vector<int16_t> quiet(FRAME, 0), loud = Speech(1, 0.8f, 3);
        for (auto& q : quiet) q = (int16_t)((&q - quiet.data()) % 7 - 3);   // (hiss at -80 dB)
        Gate g;
        bool ptt = g.Step(quiet.data(), true, false, 0.5f) && !g.Step(loud.data(), false, false, 0.5f);
        check(ptt, "push to talk sends only while the key is held, whatever the level");
        Gate o;
        bool shut = !o.Step(quiet.data(), false, true, 1.0f), opens = o.Step(loud.data(), false, true, 0.5f);
        int held = 0; while (o.Step(quiet.data(), false, true, 0.5f) && held < 100) held++;
        check(shut && opens && held >= 14 && held <= 16, "an open mic stays shut on hiss, opens on a voice, and holds 300 ms after it (" + std::to_string(held * 20) + " ms)");
        Gate d;
        check(!d.Step(loud.data(), false, true, 0.0f) || FrameLevel(loud.data()) > 0.75f, "with no sensitivity only a shout opens it");
    }
    // ---- the speakers: in order, lost, out of order; hearing
    auto play = [](int speaker, int ms) {
        std::vector<float> buf((size_t)44100 * ms / 1000 * 2);
        int n = (int)buf.size() / 2, done = 0; double e = 0;
        std::vector<float> blk(64);
        while (done < n) { int k = std::min(32, n - done); Render(blk.data(), k, 44100); for (int i = 0; i < k * 2; i++) e += blk[i] * blk[i]; done += k; }
        (void)speaker;
        return sqrt(e / std::max(1, n * 2));
    };
    // frames arrive at the pace they're spoken: one every 20 ms of playback
    auto feed = [&](int speaker, const std::vector<int16_t>& sp, uint16_t seq0, bool* speakingMid) {
        int frames = (int)sp.size() / FRAME; double e = 0;
        for (int f = 0; f < frames; f++) {
            uint8_t enc[ENC_BYTES]; Encode(&sp[(size_t)f * FRAME], enc); Receive(speaker, (uint16_t)(seq0 + f), enc, sizeof enc);
            double r = play(speaker, 20); e += r * r;
            if (speakingMid && f == frames / 2) *speakingMid = Speaking(speaker);
        }
        return sqrt(e / std::max(1, frames));
    };
    {
        Reset(); GetStats() = Stats{};
        bool speaking = false;
        double rms = feed(1, Speech(40, 0.6f, 11), 65530, &speaking);   // (the sequence wraps)
        double after = play(1, 400);
        check(rms > 0.02 && GetStats().framesLost == 0, "forty frames in order play out (RMS " + std::to_string(rms).substr(0, 5) + ", none lost)");
        check(speaking && after < rms * 0.2 && !Speaking(1), "a speaker plays through the sequence's wrap, shows as speaking, and falls silent when the talk ends");
    }    {
        Reset(); GetStats() = Stats{};
        std::vector<int16_t> sp = Speech(50, 0.6f, 13);
        std::vector<std::vector<uint8_t>> pk;
        for (int f = 0; f < 50; f++) { std::vector<uint8_t> e(ENC_BYTES); Encode(&sp[(size_t)f * FRAME], e.data()); pk.push_back(e); }
        // every tenth lost, and pairs swapped (late UDP)
        std::vector<int> order;
        for (int f = 0; f < 50; f++) if (f % 10 != 5) order.push_back(f);
        for (size_t i = 2; i + 1 < order.size(); i += 6) std::swap(order[i], order[i + 1]);
        // fed a few at a time, as the frames arrive
        size_t at = 0; double e = 0;
        for (int tick = 0; tick < 60; tick++) {
            for (int k = 0; k < 1 && at < order.size(); k++, at++) Receive(2, (uint16_t)order[at], pk[order[at]].data(), ENC_BYTES);
            e += play(2, 20);
        }
        check(GetStats().framesLost >= 4 && GetStats().framesLost <= 6 && e > 0.1, "lost frames are concealed (" + std::to_string(GetStats().framesLost) + " of 50) and swapped ones put back in order");
    }
    {
        Reset();
        Hearing h; h.gain = 0; SetHearing(3, h);
        bool mid = false;
        double far = feed(3, Speech(30, 0.6f, 17), 0, &mid);
        check(far < 1e-4 && mid, "out of earshot: nothing heard, but the speaker still shows as speaking (a mouth moves)");
        Reset();
        auto heard = [&](Hearing hh, uint32_t seed) {
            Reset();
            std::vector<int16_t> s2 = Speech(30, 0.6f, seed);
            SetHearing(4, hh);
            return feed(4, s2, 0, nullptr);
        };
        Hearing plain, wall, radio, water, ghost; wall.muffle = 1; radio.radio = 1; water.bubble = 1; ghost.ghost = 1;
        double p = heard(plain, 21), w = heard(wall, 21), r = heard(radio, 21), b = heard(water, 21), gh = heard(ghost, 21);
        check(w < p * 0.8 && r > 0.005 && b < p && gh > 0.005, "a bulkhead muffles it, a walkie crackles it through, the water bubbles it, a ghost rings hollow");
    }
    // ---- the session's relay: a host and two guests in memory
    {
        using namespace arcade;
        Reset();
        Session host, a, b; std::string err;
        Profile ph{"Captain", 1}, pa{"Nurse", 2}, pb{"Diver", 3};
        bool up = host.Host(ph, G_SCUTTLE, &err, 47812, net::MakeMemoryTransport(), false) && a.Join(pa, "mem:47812", &err, 0, net::MakeMemoryTransport()) && b.Join(pb, "mem:47812", &err, 0, net::MakeMemoryTransport());
        double t = 0;
        auto step = [&](int n) { for (int i = 0; i < n; i++) { t += 1 / 60.0; host.Update(t, 1 / 60.0f); a.Update(t, 1 / 60.0f); b.Update(t, 1 / 60.0f); } };
        for (int i = 0; i < 300 && !(a.stage == S_LOBBY && b.stage == S_LOBBY); i++) step(1);
        step(10);
        uint8_t enc[ENC_BYTES]; std::vector<int16_t> sp = Speech(1, 0.5f, 5); Encode(sp.data(), enc);
        for (int k = 0; k < 10; k++) a.SendVoice((uint16_t)k, enc, sizeof enc);
        host.SendVoice(0, enc, sizeof enc);
        step(5);
        int hostGot = 0, bGot = 0, aGot = 0, bFromHost = 0;
        for (auto& v : host.voiceIn) hostGot += v.seat == a.mySeat;
        for (auto& v : b.voiceIn) { bGot += v.seat == a.mySeat; bFromHost += v.seat == host.mySeat; }
        aGot = (int)a.voiceIn.size();
        check(up && hostGot == 10 && bGot == 10 && bFromHost == 1 && aGot == 1 && a.voiceIn[0].seat == host.mySeat && host.voiceRelayed == 10,
              "the host hears a guest and passes it to the other guest, never back to the speaker; the host's own voice reaches both");
        host.Leave(); a.Leave(); b.Leave();
    }
    Reset();
    printf(fails ? "voice-test: %d check(s) failed\n" : "voice-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace voice
