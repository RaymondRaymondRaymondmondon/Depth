// --study-audio-test: renders every Study layer and music style for 60 s offline and fails on silence, clipping,
// DC offset or a detectable loop (an autocorrelation check); measures each one's perceived loudness for the
// level-matching table (study_data.cpp); checks every preset; and times 8 layers against the 5% CPU budget.
#include "study_audio.h"
#include "study_data.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace {
constexpr int SR = 44100;

void Fft(std::vector<std::complex<double>>& a, bool inv) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * 3.14159265358979 / len * (inv ? -1 : 1);
        std::complex<double> wl(cos(ang), sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; j++) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
    if (inv) for (auto& x : a) x /= (double)n;
}
// the highest normalized autocorrelation of the (band-limited, decimated) signal at lags from 1 s to 29 s:
// a loop of any length in that range shows up as a value near 1
double LoopScore(const std::vector<float>& mono) {
    const int dec = 11; const double fs = (double)SR / dec;
    std::vector<double> d; d.reserve(mono.size() / dec);
    for (size_t i = 0; i + dec <= mono.size(); i += dec) { double s = 0; for (int k = 0; k < dec; k++) s += mono[i + k]; d.push_back(s / dec); }
    double mean = 0; for (double x : d) mean += x; mean /= std::max<size_t>(1, d.size());
    size_t n = 1; while (n < d.size() * 2) n <<= 1;
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < d.size(); i++) a[i] = d[i] - mean;
    Fft(a, false);
    for (auto& x : a) x = std::norm(x);
    Fft(a, true);
    double r0 = a[0].real(); if (r0 <= 0) return 0;
    double best = 0;
    size_t lo = (size_t)(1 * fs), hi = std::min((size_t)(29 * fs), d.size() / 2);
    for (size_t lag = lo; lag < hi; lag++) best = std::max(best, a[lag].real() / r0 * ((double)d.size() / (d.size() - lag)));   // (unbiased)
    return best;
}
struct Report { double rmsDb, kDb, dc, peak, loop; int clipped; };
Report Measure(const std::vector<float>& lr) {
    Report r{};
    size_t n = lr.size() / 2;
    double sum = 0, sq = 0, ksq = 0; float peak = 0; int clip = 0;
    const float ceil = study::Numbers().limiterCeiling;
    // a K-weighting-like filter for perceived loudness: a high-pass near 60 Hz and a +4 dB shelf above 1.5 kHz
    double hp1 = 0, hpPrev = 0, shelf = 0;
    const double aHp = exp(-2 * 3.14159265 * 60 / SR), aSh = exp(-2 * 3.14159265 * 1500 / SR);
    std::vector<float> mono(n);
    for (size_t i = 0; i < n; i++) {
        float m = 0.5f * (lr[i * 2] + lr[i * 2 + 1]);
        mono[i] = m;
        sum += m; sq += (double)m * m;
        peak = std::max(peak, std::max(fabsf(lr[i * 2]), fabsf(lr[i * 2 + 1])));
        if (fabsf(lr[i * 2]) >= ceil - 1e-4f || fabsf(lr[i * 2 + 1]) >= ceil - 1e-4f) clip++;
        double h = aHp * (hp1 + m - hpPrev); hpPrev = m; hp1 = h;
        shelf = aSh * shelf + (1 - aSh) * h;
        double k = h + (h - shelf) * 0.58;   // (+4 dB above the shelf)
        ksq += k * k;
    }
    r.dc = sum / std::max<size_t>(1, n);
    r.rmsDb = 10 * log10(std::max(1e-12, sq / std::max<size_t>(1, n)));
    r.kDb = 10 * log10(std::max(1e-12, ksq / std::max<size_t>(1, n)));
    r.peak = peak; r.clipped = clip;
    r.loop = LoopScore(mono);
    return r;
}
std::vector<float> RenderFor(const study::MixerState& st, float seconds, uint32_t seed) {
    study::ResetEngine(seed);
    study::SetState(st);
    std::vector<float> out((size_t)(seconds * SR) * 2);
    const int block = 2048;
    for (size_t f = 0; f < out.size() / 2; f += block) study::Render(out.data() + f * 2, (int)std::min<size_t>(block, out.size() / 2 - f), SR);
    return out;
}
void WriteWav(const char* path, const std::vector<float>& lr) {
    FILE* fp = fopen(path, "wb"); if (!fp) return;
    uint32_t n = (uint32_t)(lr.size() / 2), bytes = n * 4;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, fp); };
    fwrite("RIFF", 1, 4, fp); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, fp); w32(16); w16(1); w16(2); w32(SR); w32(SR * 4); w16(4); w16(16);
    fwrite("data", 1, 4, fp); w32(bytes);
    for (float s : lr) { int v = (int)std::clamp(s * 32767.0f, -32768.0f, 32767.0f); w16((uint16_t)(int16_t)v); }
    fclose(fp);
}
}

int RunStudyAudioTest(const char* wavPath, float seconds) {
    using namespace study;
    const StudyNumbers& N = Numbers();
    int fails = 0;
    std::vector<float> preview;
    auto check = [&](const char* name, const std::vector<float>& lr, bool loudness, double* kOut) {
        Report r = Measure(lr);
        double norm = pow(10, (N.loudnessTargetDb - r.kDb) / 20);
        bool silent = r.rmsDb < -60, clip = r.clipped > (int)(lr.size() / 2 / 2000), dc = fabs(r.dc) > 0.003, loop = r.loop > 0.6;
        printf("  %-22s rms %6.1f dB  loud %6.1f dB  peak %.2f  dc %+.4f  loop %.2f%s%s%s%s", name, r.rmsDb, r.kDb, r.peak, r.dc, r.loop,
               silent ? "  SILENT" : "", clip ? "  CLIPS" : "", dc ? "  DC" : "", loop ? "  LOOPS" : "");
        if (loudness) printf("   (norm x%.3f)", norm);
        printf("\n");
        if (kOut) *kOut = r.kDb;
        if (silent || clip || dc || loop) fails++;
        if (wavPath) preview.insert(preview.end(), lr.begin(), lr.begin() + std::min<size_t>(lr.size(), (size_t)SR * 2 * 8));
    };
    printf("Study soundscape: every layer for %.0f s (default controls, level 1)\n", seconds);
    double kLayer[L_KIND_COUNT] = {}, kStyle[M_STYLE_COUNT] = {};
    for (int k = 0; k < L_KIND_COUNT; k++) {
        if (k == L_MUSIC) continue;
        MixerState st; LayerState l = NewLayer(k); l.level = 1; st.layers.push_back(l);
        check(Layer(k).name, RenderFor(st, seconds, 1000 + k), true, &kLayer[k]);
    }
    printf("Music styles for %.0f s\n", seconds);
    for (int s = 0; s < M_STYLE_COUNT; s++) {
        MixerState st; LayerState l = NewLayer(L_MUSIC); l.level = 1; l.music.style = s; l.music.bpm = Style(s).bpmDef; l.music.seed = 77 + s; st.layers.push_back(l);
        check(Style(s).name, RenderFor(st, seconds, 2000 + s), true, &kStyle[s]);
    }
    // every control position that changes a layer's sound in kind: each rain surface, each time of day
    printf("Rain surfaces and forest times of day (20 s each)\n");
    for (int surf = 0; surf < 5; surf++) { MixerState st; LayerState l = NewLayer(L_RAIN); l.level = 1; l.p[2] = (float)surf; st.layers.push_back(l); char nm[40]; snprintf(nm, 40, "rain, surface %d", surf); check(nm, RenderFor(st, 20, 3000 + surf), false, nullptr); }
    for (int tod = 0; tod < 4; tod++) { MixerState st; LayerState l = NewLayer(L_FOREST); l.level = 1; l.p[0] = (float)tod; st.layers.push_back(l); char nm[40]; snprintf(nm, 40, "forest, time %d", tod); check(nm, RenderFor(st, 20, 3100 + tod), false, nullptr); }
    printf("Presets (30 s each)\n");
    for (int p = 0; p < PresetCount(); p++) { const Preset& pr = GetPreset(p); MixerState st; for (int i = 0; i < pr.n; i++) st.layers.push_back(pr.layers[i]); check(pr.name, RenderFor(st, 30, 4000 + p), false, nullptr); }
    // level matching: every layer within 3 dB of the target once its norm is applied
    printf("Level matching (target %.0f dB):\n", N.loudnessTargetDb);
    int off = 0;
    for (int k = 0; k < L_KIND_COUNT; k++) { if (k == L_MUSIC) continue; double got = kLayer[k]; if (fabs(got - N.loudnessTargetDb) > 3 && k != L_THUNDER) { printf("  %s is %.1f dB off (set LAYER_NORM[%d] = %.3f)\n", Layer(k).name, got - N.loudnessTargetDb, k, LayerNorm(k) * pow(10, (N.loudnessTargetDb - got) / 20)); off++; } }
    for (int s = 0; s < M_STYLE_COUNT; s++) { double got = kStyle[s]; if (fabs(got - N.loudnessTargetDb) > 3) { printf("  %s is %.1f dB off (set STYLE_NORM[%d] = %.3f)\n", Style(s).name, got - N.loudnessTargetDb, s, StyleNorm(s) * pow(10, (N.loudnessTargetDb - got) / 20)); off++; } }
    if (off) { printf("  %d layer(s) not level-matched\n", off); fails++; } else printf("  all within 3 dB\n");
    // the CPU budget: 8 layers at once
    {
        MixerState st;
        int kinds[8] = {L_BROWN, L_RAIN, L_STREAM, L_WIND, L_FIRE, L_FOREST, L_NAUTILUS, L_MUSIC};
        for (int k : kinds) { LayerState l = NewLayer(k); if (k == L_MUSIC) { l.music.style = M_LOUNGE; l.music.bpm = 110; } l.level = 0.7f; st.layers.push_back(l); }
        auto t0 = std::chrono::steady_clock::now();
        std::vector<float> lr = RenderFor(st, seconds, 5000);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double pct = 100.0 * secs / seconds;
        printf("CPU: 8 layers for %.0f s rendered in %.2f s = %.1f%% of one core%s\n", seconds, secs, pct, pct > 5 ? "  OVER BUDGET" : "");
        if (pct > 5) fails++;
        check("all 8 layers", lr, false, nullptr);
    }
    if (wavPath) { WriteWav(wavPath, preview); printf("wrote %s (8 s of each)\n", wavPath); }
    printf(fails ? "study-audio-test: %d problem(s)\n" : "study-audio-test: OK\n", fails);
    return fails ? 1 : 0;
}
