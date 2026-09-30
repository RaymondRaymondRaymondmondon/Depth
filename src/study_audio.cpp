// The Study's soundscape mixer (see study_audio.h). Every layer is synthesized: noise colours, nature from filtered
// noise and grains, and generative music from a seeded rule-based composer. Numbers live in study_data.cpp.
#include "study_audio.h"
#include "study_data.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace study {
namespace {

constexpr float TAU = 6.2831853f;
float gSR = 44100;
float PanL(float p) { return cosf((p + 1) * 0.25f * 3.14159265f); }
float PanR(float p) { return sinf((p + 1) * 0.25f * 3.14159265f); }

struct Rng {
    uint32_t s = 1;
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float U() { return (Next() >> 8) * (1.0f / 16777216.0f); }          // 0..1
    float N() { return U() * 2 - 1; }                                   // -1..1
    float R(float a, float b) { return a + (b - a) * U(); }
    int I(int n) { return n > 0 ? (int)(Next() % (uint32_t)n) : 0; }
    bool Chance(float p) { return U() < p; }
};

enum { BQ_LP, BQ_HP, BQ_BP, BQ_LS, BQ_HS, BQ_PK };
struct BQ {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void Set(int type, float f, float q, float db = 0) {
        f = std::clamp(f, 10.0f, gSR * 0.45f); q = std::max(0.1f, q);
        float w = TAU * f / gSR, cw = cosf(w), sw = sinf(w), al = sw / (2 * q), A = powf(10, db / 40);
        float n0, n1, n2, d0, d1, d2;
        switch (type) {
        case BQ_LP: n0 = (1 - cw) / 2; n1 = 1 - cw; n2 = n0; d0 = 1 + al; d1 = -2 * cw; d2 = 1 - al; break;
        case BQ_HP: n0 = (1 + cw) / 2; n1 = -(1 + cw); n2 = n0; d0 = 1 + al; d1 = -2 * cw; d2 = 1 - al; break;
        case BQ_BP: n0 = al; n1 = 0; n2 = -al; d0 = 1 + al; d1 = -2 * cw; d2 = 1 - al; break;
        case BQ_LS: { float s = 2 * sqrtf(A) * al;
            n0 = A * ((A + 1) - (A - 1) * cw + s); n1 = 2 * A * ((A - 1) - (A + 1) * cw); n2 = A * ((A + 1) - (A - 1) * cw - s);
            d0 = (A + 1) + (A - 1) * cw + s; d1 = -2 * ((A - 1) + (A + 1) * cw); d2 = (A + 1) + (A - 1) * cw - s; } break;
        case BQ_HS: { float s = 2 * sqrtf(A) * al;
            n0 = A * ((A + 1) + (A - 1) * cw + s); n1 = -2 * A * ((A - 1) + (A + 1) * cw); n2 = A * ((A + 1) + (A - 1) * cw - s);
            d0 = (A + 1) - (A - 1) * cw + s; d1 = 2 * ((A - 1) - (A + 1) * cw); d2 = (A + 1) - (A - 1) * cw - s; } break;
        default: n0 = 1 + al * A; n1 = -2 * cw; n2 = 1 - al * A; d0 = 1 + al / A; d1 = -2 * cw; d2 = 1 - al / A; break;
        }
        b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
    float Run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
struct OnePole { float a = 1, y = 0; void Set(float f) { a = 1 - expf(-TAU * std::clamp(f, 1.0f, gSR * 0.45f) / gSR); } float Run(float x) { y += a * (x - y); return y; } };
// Paul Kellet's pink noise filter (-3 dB/octave)
struct Pink {
    float b[7] = {};
    float Run(float w) {
        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
        float o = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f; b[6] = w * 0.115926f;
        return o * 0.11f;
    }
};
float DbToLin(float db) { return powf(10, db / 20); }

// a slow wander between two rates: the "drift" of every noise layer and the gusts of the wind
struct Wander {
    float v = 0, target = 0, t = 0, period = 1;
    float Step(float dt, float hzLo, float hzHi, Rng& r) {
        t += dt;
        if (t >= period) { t = 0; period = 1.0f / r.R(hzLo, hzHi); target = r.N(); }
        float k = std::min(1.0f, dt / std::max(0.05f, period * 0.5f));
        v += (target - v) * k;
        return v;
    }
};

// ---------------------------------------------------------------- grains (drops, bubbles, crackles, birds, creaks)
struct Grain {
    bool on = false; int type = 0;
    float t = 0, dur = 0.02f, f0 = 1000, f1 = 1000, amp = 0.1f, pan = 0, ph = 0, ph2 = 0, p1 = 0, p2 = 0;
    BQ bq; float gl = -1, gr = 0;
    float GL() { if (gl < 0) { gl = PanL(pan); gr = PanR(pan); } return gl; }
    float GR() { GL(); return gr; }
};
struct GrainPool {
    Grain g[64];
    Grain* Get() { for (auto& x : g) if (!x.on) { x = Grain{}; x.on = true; return &x; } return nullptr; }
};

// ---------------------------------------------------------------- generators
struct Gen {
    Rng r;
    virtual ~Gen() = default;
    virtual void Render(float* L, float* R, int n, const LayerState& s, double t) = 0;   // adds nothing; writes n frames
};

// ---- noise colours: two independent channels (the strip's width folds them together), plus drift
struct NoiseGen : Gen {
    int kind; Pink pl, pr; float brL = 0, brR = 0, prevL = 0, prevR = 0;
    BQ gL[3], gR[3], sub, subR; Wander dLevel, dTone; OnePole toneL, toneR;
    explicit NoiseGen(int k) : kind(k) {
        const LayerDef& d = Layer(k);
        if (k == L_GREY) { gL[0].Set(BQ_LS, d.tuning[3], 0.7f, d.tuning[4]); gL[1].Set(BQ_PK, d.tuning[5], 1.0f, d.tuning[6]); gL[2].Set(BQ_HS, d.tuning[7], 0.7f, 4); for (int i = 0; i < 3; i++) gR[i] = gL[i]; }
        if (k == L_BROWN) { sub.Set(BQ_LP, d.tuning[4], 0.8f); subR = sub; }
    }
    float Colour(float w, Pink& p, float& br, float& prev, BQ* g, int ch) {
        switch (kind) {
        case L_WHITE: return w * 0.35f;
        case L_PINK: return p.Run(w);
        case L_BROWN: { br = br * (1 - Layer(L_BROWN).tuning[3] * 0.05f) + w * 0.02f; return br * 3.2f; }
        case L_GREY: { float x = p.Run(w); for (int i = 0; i < 3; i++) x = g[i].Run(x); return x * 0.6f; }
        default: { float x = p.Run(w); float o = (x - prev) * 1.6f; prev = x; (void)ch; return o; } // blue: pink differentiated
        }
    }
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(kind);
        float drift = s.p[kind == L_BROWN ? 1 : 0];
        float dt = n / gSR;
        float lv = DbToLin(-d.tuning[2] * drift * (0.5f + 0.5f * dLevel.Step(dt, d.tuning[0], d.tuning[1], r)));
        float tone = 0.5f + 0.5f * dTone.Step(dt, d.tuning[0], d.tuning[1], r);
        float cut = 18000 - drift * 7000 * tone;
        toneL.Set(cut); toneR.Set(cut);
        float rumble = kind == L_BROWN ? s.p[0] : 0;
        float subG = DbToLin(d.tuning[5] * rumble) - 1;
        for (int i = 0; i < n; i++) {
            float a = Colour(r.N(), pl, brL, prevL, gL, 0), b = Colour(r.N(), pr, brR, prevR, gR, 1);
            if (kind == L_BROWN) { a += sub.Run(a) * subG; b += subR.Run(b) * subG; }
            L[i] = toneL.Run(a) * lv; R[i] = toneR.Run(b) * lv;
        }
    }
};

// ---- rain: a filtered bed plus granular drops whose sound depends on the surface they land on
struct RainGen : Gen {
    BQ bedL, bedR, distL, distR; GrainPool gp; float acc = 0;
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_RAIN);
        float inten = s.p[0], size = s.p[1], dist = s.p[3];
        int surf = (int)lroundf(s.p[2]);
        static const float SURF_BED[5] = {1800, 3400, 2600, 900, 2200};    // roof, window, leaves, tent, sea
        bedL.Set(BQ_BP, SURF_BED[surf] * (1 - 0.3f * size), d.tuning[1]); bedR = bedL;
        float cut = 14000 - 11000 * dist;
        distL.Set(BQ_LP, cut, 0.707f); distR.Set(BQ_LP, cut, 0.707f);
        float rate = d.tuning[2] * powf(inten, 1.4f) * (1 - 0.4f * dist);
        float bedG = d.tuning[7] * (0.3f + inten) * (1 + dist);
        for (int i = 0; i < n; i++) {
            acc += rate / gSR;
            while (acc >= 1 || (acc > 0 && r.U() < acc * 0.02f)) {
                acc -= 1;
                if (Grain* g = gp.Get()) {
                    g->type = surf; g->pan = r.N() * 0.9f;
                    g->amp = r.R(0.15f, 1.0f) * (0.4f + size) * 0.45f;
                    g->dur = r.R(d.tuning[5], d.tuning[6]) * (0.7f + size * 0.8f);
                    float sz = 1.4f - size;
                    switch (surf) {
                    case 0: g->f0 = r.R(160, 320) * sz; g->f1 = g->f0 * 0.8f; g->bq.Set(BQ_LP, 1500, 0.8f); g->dur *= 1.6f; break;        // roof: a thud
                    case 1: g->f0 = r.R(2800, 5200) * sz; g->f1 = g->f0; g->bq.Set(BQ_HP, 3000, 0.9f); g->dur *= 0.5f; break;          // window: a tick
                    case 2: g->f0 = r.R(900, 2400); g->f1 = g->f0; g->bq.Set(BQ_BP, r.R(1400, 3400), 1.2f); break;                     // leaves: a patter
                    case 3: g->f0 = r.R(250, 460) * sz; g->f1 = g->f0 * 0.9f; g->bq.Set(BQ_BP, 700, 1.5f); g->dur *= 1.4f; break;       // tent: taut cloth
                    default: g->f0 = r.R(d.tuning[3], d.tuning[4] * 0.6f) * sz; g->f1 = g->f0 * 2.2f; g->bq.Set(BQ_HP, 400, 0.7f); break; // sea: a bubble
                    }
                }
            }
            float bl = bedL.Run(r.N()) * bedG, br = bedR.Run(r.N()) * bedG, gl = 0, gr = 0;
            for (auto& g : gp.g) {
                if (!g.on) continue;
                float u = g.t / g.dur;
                if (u >= 1) { g.on = false; continue; }
                float env = (1 - u) * (1 - u) * std::min(1.0f, g.t * 4000);
                float f = g.f0 + (g.f1 - g.f0) * u;
                g.ph += f / gSR; if (g.ph >= 1) g.ph -= 1;
                float tonal = sinf(TAU * g.ph), noise = g.bq.Run(r.N());
                float mix = g.type == 4 ? tonal * 0.8f + noise * 0.2f : g.type == 1 ? tonal * 0.3f + noise * 0.9f : g.type == 2 ? noise : tonal * 0.5f + noise * 0.7f;
                float v = mix * env * g.amp;
                gl += v * g.GL(); gr += v * g.GR();
                g.t += 1 / gSR;
            }
            L[i] = distL.Run(bl + gl); R[i] = distR.Run(br + gr);
        }
    }
};

// ---- a stream: band-passed noise bands with their own slow amplitude wander, plus bubble chirps
struct StreamGen : Gen {
    BQ bandL[5], bandR[5], distL, distR; Wander am[5]; GrainPool gp; float acc = 0;
    StreamGen() {
        const LayerDef& d = Layer(L_STREAM);
        for (int k = 0; k < 5; k++) { float f = d.tuning[0] * powf(d.tuning[1] / d.tuning[0], k / 4.0f); bandL[k].Set(BQ_BP, f, 1.6f); bandR[k].Set(BQ_BP, f * 1.07f, 1.6f); }
    }
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_STREAM);
        float flow = s.p[0], babble = s.p[1], dist = s.p[2], dt = n / gSR;
        float g[5];
        for (int k = 0; k < 5; k++) g[k] = (0.55f + 0.45f * babble * am[k].Step(dt, d.tuning[2], d.tuning[3] * (0.3f + babble), r)) * (0.3f + flow) * (k > 2 ? 0.7f + 0.6f * flow : 1.0f) * 0.55f;
        float cut = 15000 - 10000 * dist;
        distL.Set(BQ_LP, cut, 0.707f); distR.Set(BQ_LP, cut, 0.707f);
        float rate = d.tuning[4] * babble * (0.4f + flow);
        for (int i = 0; i < n; i++) {
            acc += rate / gSR;
            if (acc >= 1) { acc -= 1; if (Grain* b = gp.Get()) { b->f0 = r.R(d.tuning[5], d.tuning[6]); b->f1 = b->f0 * r.R(1.4f, 2.2f); b->dur = d.tuning[7] * r.R(0.5f, 1.5f); b->amp = r.R(0.05f, 0.18f); b->pan = r.N() * 0.7f; } }
            float a = 0, b2 = 0;
            for (int k = 0; k < 5; k++) { a += bandL[k].Run(r.N()) * g[k]; b2 += bandR[k].Run(r.N()) * g[k]; }
            for (auto& gr : gp.g) {
                if (!gr.on) continue;
                float u = gr.t / gr.dur;
                if (u >= 1) { gr.on = false; continue; }
                gr.ph += (gr.f0 + (gr.f1 - gr.f0) * u) / gSR; if (gr.ph >= 1) gr.ph -= 1;
                float v = sinf(TAU * gr.ph) * sinf(3.14159f * u) * gr.amp;
                a += v * gr.GL(); b2 += v * gr.GR();
                gr.t += 1 / gSR;
            }
            L[i] = distL.Run(a); R[i] = distR.Run(b2);
        }
    }
};

// ---- surf: slow swells on a wave period, a hiss where it breaks, then the wash
struct SurfGen : Gen {
    BQ swellL, swellR, hissL, hissR, washL, washR; float u = 0, period = 9, size = 1;
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_SURF);
        float waveSize = s.p[0], dist = s.p[2];
        for (int i = 0; i < n; i++) {
            u += 1 / (gSR * period);
            if (u >= 1) { u -= 1; period = s.p[1] * r.R(0.85f, 1.15f); size = r.R(0.7f, 1.3f); }
            if ((i & 31) == 0) {
                float swell = 0.5f - 0.5f * cosf(TAU * std::min(u / 0.65f, 1.0f) * 0.5f);
                swellL.Set(BQ_LP, d.tuning[0] * (0.5f + swell) * (1.2f - 0.6f * dist), 0.7f); swellR = swellL;
                hissL.Set(BQ_HP, d.tuning[1] * (1 - 0.5f * dist), 0.7f); hissR = hissL;
                washL.Set(BQ_BP, 900 * (1.1f - 0.5f * dist), 0.6f); washR = washL;
            }
            float swell = u < 0.65f ? 0.5f - 0.5f * cosf(3.14159f * u / 0.65f) : std::max(0.0f, 1 - (u - 0.65f) / 0.35f);
            float brk = u > 0.62f ? expf(-(u - 0.62f) / (d.tuning[2] * 0.3f)) : 0;
            float wash = u > 0.66f ? expf(-(u - 0.66f) / d.tuning[3]) : 0;
            float amp = (0.35f + waveSize) * size * (1 - 0.4f * dist);
            float floor = d.tuning[4] * 0.15f;
            float a = swellL.Run(r.N()) * (floor + swell) * 0.9f + hissL.Run(r.N()) * brk * 0.35f * (1 - 0.6f * dist) + washL.Run(r.N()) * wash * 0.4f;
            float b = swellR.Run(r.N()) * (floor + swell) * 0.9f + hissR.Run(r.N()) * brk * 0.35f * (1 - 0.6f * dist) + washR.Run(r.N()) * wash * 0.4f;
            L[i] = a * amp; R[i] = b * amp;
        }
    }
};

// ---- wind: low-passed noise under a gust envelope, and a resonant whistle band that rises with the gusts
struct WindGen : Gen {
    BQ lpL, lpR, whL, whR; Wander gust, pitch;
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_WIND);
        float str = s.p[0], gusty = s.p[1], wp = s.p[2], dt = n / gSR;
        float gv = 0.5f + 0.5f * gust.Step(dt, d.tuning[2], d.tuning[3] * (0.3f + gusty), r);
        float g = (1 - gusty) * 0.6f + gusty * gv;
        float cut = d.tuning[0] + (d.tuning[1] - d.tuning[0]) * g * (0.5f + str);
        lpL.Set(BQ_LP, cut, 0.9f); lpR.Set(BQ_LP, cut * 1.05f, 0.9f);
        float wf = wp * (0.85f + 0.3f * g + 0.05f * pitch.Step(dt, 0.1f, 0.4f, r));
        whL.Set(BQ_BP, wf, d.tuning[4]); whR.Set(BQ_BP, wf * 1.01f, d.tuning[4]);
        float amp = (0.25f + str) * (0.3f + 0.7f * g), wamp = d.tuning[5] * g * g * (0.3f + str) * 3;
        for (int i = 0; i < n; i++) {
            L[i] = lpL.Run(r.N()) * amp + whL.Run(r.N()) * wamp;
            R[i] = lpR.Run(r.N()) * amp + whR.Run(r.N()) * wamp;
        }
    }
};

// ---- fire: a low roar, crackle impulses, and now and then a pop
struct FireGen : Gen {
    BQ roarL, roarR, crk, pop; Wander flick; GrainPool gp; float accC = 0, accP = 0;
    FireGen() { const LayerDef& d = Layer(L_FIRE); roarL.Set(BQ_LP, d.tuning[0], 0.7f); roarR = roarL; crk.Set(BQ_HP, d.tuning[3], 0.7f); pop.Set(BQ_LP, 900, 0.8f); }
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_FIRE);
        float size = s.p[0], crackle = s.p[1], pops = s.p[2], dt = n / gSR;
        float fl = 0.75f + 0.25f * flick.Step(dt, 0.3f, 1.5f, r);
        float rateC = d.tuning[1] * crackle * (0.3f + size), rateP = d.tuning[2] * pops;
        for (int i = 0; i < n; i++) {
            accC += rateC / gSR * r.R(0.2f, 1.8f); accP += rateP / gSR;
            if (accC >= 1) { accC -= 1; if (Grain* g = gp.Get()) { g->type = 0; g->dur = d.tuning[4] * r.R(0.5f, 1.5f); g->amp = r.R(0.1f, 0.6f) * (0.4f + size); g->pan = r.N() * 0.5f; } }
            if (accP >= 1) { accP -= 1; if (Grain* g = gp.Get()) { g->type = 1; g->dur = d.tuning[5] * r.R(0.7f, 1.4f); g->amp = r.R(0.4f, 1.0f) * (0.4f + size); g->pan = r.N() * 0.4f; } }
            float a = roarL.Run(r.N()) * size * fl * 1.3f, b = roarR.Run(r.N()) * size * fl * 1.3f;
            for (auto& g : gp.g) {
                if (!g.on) continue;
                float u = g.t / g.dur;
                if (u >= 1) { g.on = false; continue; }
                float env = (1 - u) * (1 - u) * (1 - u);
                float v = g.type == 0 ? crk.Run(r.N()) * env * g.amp : (pop.Run(r.N()) * 1.6f + (u < 0.05f ? r.N() * 0.5f : 0)) * env * g.amp;
                a += v * g.GL(); b += v * g.GR();
                g.t += 1 / gSR;
            }
            L[i] = a; R[i] = b;
        }
    }
};

// ---- a forest: leaf rustle, birds from FM chirp generators with per-species patterns, crickets, a distant owl
struct ForestGen : Gen {
    BQ rustL, rustR, distL, distR, owlF; Wander wind; GrainPool gp; float accB = 0, accO = 0;
    float crickPh[3] = {0, 0.3f, 0.7f}, crickCar[3] = {0, 0, 0}, crickF[3] = {1.0f, 1.049f, 0.973f}, crickRate[3] = {1, 1.13f, 0.87f};
    float owlT = -1, owlNotes[3] = {0, 0.55f, 0.8f};
    ForestGen() { rustL.Set(BQ_HP, Layer(L_FOREST).tuning[0], 0.7f); rustR = rustL; owlF.Set(BQ_BP, 700, 2); }
    // species: base Hz, sweep, note length, notes per song, gap, FM ratio, FM index
    struct Species { float f, sweep, len; int notes; float gap, fmR, fmI; };
    static const Species& Sp(int i) {
        static const Species S[5] = {{3200, 1.3f, 0.06f, 6, 0.05f, 2.0f, 0.4f}, {2400, 0.7f, 0.25f, 2, 0.12f, 1.0f, 0.2f},
                                     {4200, 1.6f, 0.03f, 12, 0.02f, 3.0f, 0.6f}, {1800, 1.15f, 0.14f, 4, 0.08f, 1.5f, 1.0f}, {5200, 0.85f, 0.04f, 3, 0.2f, 2.0f, 0.3f}};
        return S[i % 5];
    }
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_FOREST);
        int tod = (int)lroundf(s.p[0]); float dens = s.p[1], dist = s.p[2], dt = n / gSR;
        static const float BIRDS[4] = {1.0f, 0.35f, 0.15f, 0.0f}, CRICK[4] = {0, 0, 0.6f, 1.0f}, OWL[4] = {0.1f, 0, 0.3f, 1.0f};
        float wv = 0.5f + 0.5f * wind.Step(dt, 0.05f, 0.25f, r);
        float cut = 15000 - 10000 * dist;
        distL.Set(BQ_LP, cut, 0.707f); distR.Set(BQ_LP, cut, 0.707f);
        float rateB = d.tuning[1] * BIRDS[tod] * (0.2f + dens);
        float crick = CRICK[tod] * (0.3f + dens) * (1 - 0.5f * dist);
        for (int i = 0; i < n; i++) {
            accB += rateB / gSR;
            if (accB >= 1) {   // a bird sings a short song: a run of chirps
                accB -= 1;
                int sp = r.I(5); const Species& S = Sp(sp);
                float pan = r.N() * 0.9f, f = S.f * r.R(0.85f, 1.15f), amp = r.R(0.03f, 0.1f) * (1 - 0.5f * dist);
                for (int k = 0; k < S.notes; k++) if (Grain* g = gp.Get()) {
                    g->type = sp; g->t = -(k * (S.len + S.gap)); g->dur = S.len * r.R(0.8f, 1.2f);
                    g->f0 = f * r.R(0.95f, 1.05f); g->f1 = g->f0 * S.sweep; g->amp = amp; g->pan = pan; g->p1 = S.fmR; g->p2 = S.fmI;
                }
            }
            accO += d.tuning[5] * OWL[tod] * dens / gSR;
            if (accO >= 1 && owlT < 0) { accO = 0; owlT = 0; }
            float a = rustL.Run(r.N()) * 0.07f * wv * (0.4f + dens), b = rustR.Run(r.N()) * 0.07f * wv * (0.4f + dens);
            for (auto& g : gp.g) {
                if (!g.on) continue;
                if (g.t < 0) { g.t += 1 / gSR; continue; }
                float u = g.t / g.dur;
                if (u >= 1) { g.on = false; continue; }
                float f = g.f0 + (g.f1 - g.f0) * u;
                g.ph += f / gSR; if (g.ph >= 1) g.ph -= 1; g.ph2 += f * g.p1 / gSR; if (g.ph2 >= 1) g.ph2 -= 1;
                float v = sinf(TAU * g.ph + g.p2 * sinf(TAU * g.ph2)) * sinf(3.14159f * u) * g.amp;
                a += v * g.GL(); b += v * g.GR();
                g.t += 1 / gSR;
            }
            if (crick > 0.001f) for (int k = 0; k < 3; k++) {   // crickets: pulse trains in chirps of four
                crickPh[k] += d.tuning[3] * crickRate[k] / gSR; if (crickPh[k] >= 1) crickPh[k] -= 1;
                float chirp = fmodf(crickPh[k] * 0.25f + k * 0.37f, 1.0f) < 0.55f ? 1.0f : 0.0f;
                float pulse = sinf(3.14159f * fmodf(crickPh[k] * 4, 1.0f));
                crickCar[k] += d.tuning[2] * crickF[k] / gSR; if (crickCar[k] >= 1) crickCar[k] -= 1;
                float c = sinf(TAU * crickCar[k]) * pulse * pulse * chirp * crick * 0.012f;
                a += c * (k == 0 ? 1.0f : 0.5f); b += c * (k == 2 ? 1.0f : 0.5f);
            }
            if (owlT >= 0) {   // "hoo ... hoo-hoo"
                owlT += 1 / gSR;
                float v = 0;
                for (float st : owlNotes) { float u = (owlT - st) / 0.32f; if (u > 0 && u < 1) v += sinf(TAU * d.tuning[4] * (1 - 0.08f * u) * owlT) * sinf(3.14159f * u) * 0.05f; }
                v += owlF.Run(r.N()) * 0.004f * (owlT < 1.2f);
                a += v * 0.8f; b += v * 0.5f;
                if (owlT > 1.4f) owlT = -1;
            }
            L[i] = distL.Run(a); R[i] = distR.Run(b);
        }
    }
};

// ---- distant thunder: rumbles at random intervals
struct ThunderGen : Gen {
    BQ lpL, lpR, crk; float wait = 5, t = -1, dur = 6; Wander wob; float amp = 1;
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const LayerDef& d = Layer(L_THUNDER);
        float freq = s.p[0], dist = s.p[1], dt = n / gSR;
        if (t < 0) { wait -= dt; if (wait <= 0) { t = 0; dur = r.R(d.tuning[4], d.tuning[5]) * (1 + dist * 0.5f); amp = r.R(0.5f, 1.0f); } }
        float cut = d.tuning[1] + (d.tuning[0] - d.tuning[1]) * (1 - dist) * (0.6f + 0.4f * wob.Step(dt, 0.5f, 3, r));
        lpL.Set(BQ_LP, cut, 0.9f); lpR.Set(BQ_LP, cut * 1.1f, 0.9f); crk.Set(BQ_HP, 1800, 0.7f);
        for (int i = 0; i < n; i++) {
            float env = 0, cr = 0;
            if (t >= 0) {
                float u = t / dur;
                env = u < 0.06f ? u / 0.06f : expf(-(u - 0.06f) * 3.5f) * (0.75f + 0.25f * sinf(t * 7.3f) * sinf(t * 2.1f));
                if (u < 0.02f && dist < 0.5f) cr = crk.Run(r.N()) * (1 - u / 0.02f) * (0.5f - dist) * 0.6f;
                t += 1 / gSR;
                if (t >= dur) { t = -1; float rate = d.tuning[2] + (d.tuning[3] - d.tuning[2]) * freq; wait = r.R(0.4f, 1.6f) / rate; }
            }
            float g = env * amp * (1.2f - 0.5f * dist) * 1.8f;
            L[i] = lpL.Run(r.N()) * g + cr; R[i] = lpR.Run(r.N()) * g + cr;
        }
    }
};

// ---- the Nautilus: the engine's hum, hull creaks, water on the glass, a far sonar ping
struct NautilusGen : Gen {
    float hph = 0, pingT = -1, accC = 0, accP = 0; BQ wL, wR, creakF; GrainPool gp; Wander drift, swell;
    void Render(float* L, float* R, int n, const LayerState& s, double t) override {
        const LayerDef& d = Layer(L_NAUTILUS);
        float hum = s.p[0], rate = d.tuning[1] + (d.tuning[2] - d.tuning[1]) * s.p[1];
        float dt = n / gSR, hz = d.tuning[0] * (1 + 0.02f * drift.Step(dt, 0.08f, 0.35f, r)), hs = 0.8f + 0.2f * swell.Step(dt, 0.05f, 0.2f, r);
        wL.Set(BQ_LP, d.tuning[5] * (1 + 0.3f * sinf((float)t * 0.21f)), 0.8f); wR.Set(BQ_LP, d.tuning[5] * (1 + 0.3f * sinf((float)t * 0.17f + 1)), 0.8f);
        for (int i = 0; i < n; i++) {
            hph += hz / gSR; if (hph >= 1) hph -= 1;
            float h = (sinf(TAU * hph) + 0.45f * sinf(TAU * hph * 2) + 0.2f * sinf(TAU * hph * 3) + 0.08f * sinf(TAU * hph * 5)) * 0.09f * hum * hs;
            float water = 0.08f;
            accC += rate / gSR; accP += d.tuning[4] / gSR;
            if (accC >= 1) { accC -= 1; if (Grain* g = gp.Get()) { g->dur = r.R(0.5f, 1.6f); g->f0 = r.R(90, 200); g->f1 = g->f0 * r.R(0.6f, 1.5f); g->amp = r.R(0.05f, 0.14f); g->pan = r.N() * 0.8f; g->bq.Set(BQ_BP, g->f0 * 4, 6); } }
            if (accP >= 1 && pingT < 0) { accP = 0; pingT = 0; }
            float a = h + wL.Run(r.N()) * water, b = h * 0.95f + wR.Run(r.N()) * water;
            for (auto& g : gp.g) {   // a creak: a stick-slip buzz through a hull resonance
                if (!g.on) continue;
                float u = g.t / g.dur;
                if (u >= 1) { g.on = false; continue; }
                float f = g.f0 + (g.f1 - g.f0) * u;
                g.ph += f * (1 + 0.3f * r.N()) / gSR; if (g.ph >= 1) g.ph -= 1;
                float v = g.bq.Run(g.ph * 2 - 1) * sinf(3.14159f * u) * g.amp;
                a += v * g.GL(); b += v * g.GR();
                g.t += 1 / gSR;
            }
            if (pingT >= 0) {   // the sonar, and its echo off something far away
                float v = sinf(TAU * d.tuning[3] * pingT) * expf(-pingT * 2.2f) * 0.035f + (pingT > 0.9f ? sinf(TAU * d.tuning[3] * (pingT - 0.9f)) * expf(-(pingT - 0.9f) * 2.6f) * 0.012f : 0);
                a += v * 0.6f; b += v;
                pingT += 1 / gSR; if (pingT > 4) pingT = -1;
            }
            L[i] = a; R[i] = b;
        }
    }
};

// ---------------------------------------------------------------- the generative music engine
struct MVoice {
    bool on = false; int inst = 0; float delay = 0, t = 0, hold = 0, f = 440, amp = 0.1f, pan = 0;
    float ph[6] = {}, ph2 = 0, rel0 = 0; bool released = false; BQ lp, bp; float ks[1024]; int ksLen = 0, ksI = 0;
    float gl = -1, gr = 0, dc = 1, pAmp[6] = {1, 1, 1, 1, 1, 1}, pDec[6] = {};
    float GL() { if (gl < 0) { gl = PanL(pan); gr = PanR(pan); } return gl; }
    float GR() { GL(); return gr; }
};
constexpr int NV = 40;

struct MusicGen : Gen {
    MVoice v[NV];
    double beats = 0; long long stepIdx = -1; int bar = 0;
    int style = 0; int stepsPerBar = 16; bool swing = false;
    // the section: a progression row, patterns and a motif, and the roles fading in and out
    int secStart = 0, secLen = 8, row = 0; float roleGain[MAX_INSTR] = {1, 1, 1, 1, 1}, roleTarget[MAX_INSTR] = {1, 1, 1, 1, 1};
    int motif[8] = {0, 1, 2, 1, 0, -1, 0, 2}; int motifSteps[8] = {0, 2, 4, 6, 8, 10, 12, 14}; int motifLen = 5;
    int lastMel = 7, lastBass = 0; int chordNotes[4] = {60, 64, 67, 71};
    uint8_t kitPat[3][16] = {}; uint8_t compPat[16] = {};
    BQ vinylLP; float crackAcc = 0; float clinkAcc = 0; BQ murmur;
    uint32_t seed = 1; bool seeded = false;

    // pitch helpers
    int ScaleSemis(int deg, bool minor) { static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10}; int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7); int d = deg - o * 7; return (minor ? MIN[d] : MAJ[d]) + 12 * o; }
    float Hz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

    void NewSection(const MusicSettings& m) {
        secStart = bar; secLen = r.Chance(0.5f) ? 8 : 16;
        row = r.I(4);
        const StyleDef& st = Style(style);
        for (int k = 0; k < st.nInstr; k++) roleTarget[k] = (k == 0 || r.Chance(0.55f + 0.45f * m.density)) ? 1.0f : 0.25f;
        motifLen = 3 + r.I(4);
        for (int k = 0; k < motifLen; k++) { motif[k] = r.I(5) - 2; motifSteps[k] = std::min(15, k * 16 / motifLen + (r.Chance(0.3f) ? 1 : 0)); }
        for (int s = 0; s < 16; s++) {
            kitPat[0][s] = s == 0 || (s == 10 && r.Chance(0.7f)) || (s == 7 && r.Chance(0.3f));
            kitPat[1][s] = s == 4 || s == 12;
            kitPat[2][s] = (s % 2 == 0) || r.Chance(0.15f * m.density);
            compPat[s] = s == 0 || (s == (r.Chance(0.5f) ? 6 : 10)) || (s == 14 && r.Chance(0.3f));
        }
    }
    void Chord(int bar, const MusicSettings& m, int* degOut) {
        const StyleDef& st = Style(style);
        int len = 0; while (len < 8 && st.progression[row][len] >= 0) len++;
        if (len == 0) len = 1;
        *degOut = st.progression[row][(bar - secStart) % len];
        int root = 60 + m.key;
        bool sevenths = style == M_LOFI || style == M_LOUNGE;
        int base[4] = {*degOut, *degOut + 2, *degOut + 4, *degOut + 6};
        int nn = sevenths ? 4 : 3;
        // voice-lead: each chord tone moves to the octave nearest the previous chord
        for (int k = 0; k < nn; k++) {
            int mnote = root - 12 + ScaleSemis(base[k], m.minor);
            while (mnote < chordNotes[k] - 6) mnote += 12;
            while (mnote > chordNotes[k] + 6) mnote -= 12;
            chordNotes[k] = std::clamp(mnote, 50, 76);
        }
        if (!sevenths) chordNotes[3] = chordNotes[0] + 12;
    }
    void Note(int inst, int midi, float beatsLong, float vel, float delaySec, float bpm, float pan = 99) {
        MVoice* p = nullptr;
        for (auto& x : v) if (!x.on) { p = &x; break; }
        if (!p) { float oldest = -1; for (auto& x : v) if (x.t > oldest) { oldest = x.t; p = &x; } }
        const InstrDef& id = Instrument(inst);
        float hum = r.R(-0.007f, 0.007f);
        *p = MVoice{};
        p->on = true; p->inst = inst; p->delay = std::max(0.0f, delaySec + hum); p->f = Hz(midi);
        p->hold = beatsLong * 60 / bpm; p->amp = id.gain * vel * r.R(0.85f, 1.1f);
        p->pan = pan < 50 ? pan : id.pan + r.N() * 0.1f;
        for (float& ph : p->ph) ph = r.U();
    }
    // one step of the grid: the style decides who plays
    void Step(int s, float offSec, const MusicSettings& m) {
        const StyleDef& st = Style(style);
        float bpm = m.bpm, beatSec = 60 / bpm;
        int stepsPerBeat = 4;
        if (swing && s % 2 == 1) offSec += beatSec / 4 * (style == M_LOUNGE ? 0.66f : 0.4f);   // swung 16ths
        auto on = [&](int k) { return k < st.nInstr && m.instrOn[k] && roleGain[k] > 0.02f; };
        auto rg = [&](int k) { return roleGain[k]; };
        if (s == 0) {
            if (bar - secStart >= secLen) NewSection(m);
            int deg; Chord(bar, m, &deg); lastBass = deg;
        }
        int root = 60 + m.key;
        float dens = m.density;
        auto melodyNote = [&](int inst, int octaveShift, float beatsLong, float vel) {
            // a stepwise walk on the scale, pulled toward chord tones on strong steps; never a leap of more than a 4th
            int k = -1; for (int i = 0; i < motifLen; i++) if (motifSteps[i] == s) k = i;
            int step = k >= 0 ? motif[k] : (r.I(3) - 1);
            if (r.Chance(m.variation * 0.35f)) step += r.I(3) - 1;
            step = std::clamp(step, -3, 3);
            lastMel = std::clamp(lastMel + step, 2, 13);
            int midi = root + ScaleSemis(lastMel, m.minor) + octaveShift;
            if (s % 4 == 0) { int best = midi, bd = 99; for (int c = 0; c < 3; c++) for (int o = -24; o <= 24; o += 12) { int cn = chordNotes[c] + o; if (abs(cn - midi) < bd) { bd = abs(cn - midi); best = cn; } } midi = best; }
            Note(inst, midi, beatsLong, vel, offSec, bpm);
        };
        switch (style) {
        case M_AMBIENT:
            if (s == 0 && on(0) && (bar % 2 == 0 || r.Chance(0.3f))) for (int c = 0; c < 3; c++) Note(I_PAD, chordNotes[c] - 12 + (c == 2 ? 12 : 0), 8, 0.8f * rg(0), offSec + c * 0.05f, bpm, (c - 1) * 0.6f);
            if (s % 8 == 0 && on(1) && r.Chance(0.35f + 0.5f * dens)) melodyNote(I_BOWED, 0, r.R(2, 4), 0.8f * rg(1));
            if (s % 2 == 0 && on(2) && r.Chance(0.06f + 0.15f * dens)) Note(I_GLASS, chordNotes[r.I(3)] + 24, 3, 0.7f * rg(2), offSec, bpm);
            break;
        case M_LOFI:
            if (on(1)) {
                if (kitPat[0][s]) Note(I_KICK, 36, 0.5f, 0.9f * rg(1), offSec, bpm, 0);
                if (kitPat[1][s]) Note(I_SNARE, 50, 0.3f, 0.8f * rg(1), offSec, bpm);
                if (kitPat[2][s]) Note(I_HAT, 80, 0.1f, (s % 4 == 0 ? 0.8f : 0.5f) * rg(1), offSec, bpm);
            }
            if (on(0) && compPat[s]) for (int c = 0; c < 4; c++) Note(I_EPIANO, chordNotes[c], s == 0 ? 2.5f : 1.2f, 0.7f * rg(0), offSec + c * 0.012f, bpm);
            if (on(0) && s % 4 == 2 && r.Chance(0.15f + 0.35f * dens) && (bar % 4) >= 2) melodyNote(I_EPIANO, 12, 0.6f, 0.6f * rg(0));
            if (on(2) && (s == 0 || (s == 8 && r.Chance(0.7f)) || (s == 14 && r.Chance(0.3f)))) Note(I_BASS, root - 24 + ScaleSemis(lastBass + (s == 8 && r.Chance(0.4f) ? 4 : 0), m.minor), s == 0 ? 1.5f : 0.9f, 0.9f * rg(2), offSec, bpm, 0);
            break;
        case M_PIANO:
            if (!on(0)) break;
            if (s == 0) Note(I_PIANO, chordNotes[0] - 12, 3.5f, 0.55f, offSec, bpm, -0.25f);
            if (s == 8 && r.Chance(0.5f)) { Note(I_PIANO, chordNotes[1], 3, 0.35f, offSec, bpm, -0.1f); Note(I_PIANO, chordNotes[2], 3, 0.35f, offSec + 0.02f, bpm, -0.05f); }
            if ((bar / 2) % 2 == 0 && s % 4 == 0 && r.Chance(0.25f + 0.45f * dens)) melodyNote(I_PIANO, 12, r.R(1, 2.5f), 0.55f);   // a phrase, then a rest of the same length
            break;
        case M_SALON: {
            int beat = s / 4;
            if (on(0) && s % 4 == 0) {
                if (beat == 0) Note(I_HARMONIUM, root - 24 + ScaleSemis(lastBass, m.minor), 0.9f, 0.9f * rg(0), offSec, bpm, -0.2f);    // oom
                else for (int c = 0; c < 3; c++) Note(I_HARMONIUM, chordNotes[c], 0.6f, 0.55f * rg(0), offSec, bpm, 0.1f);           // pah-pah
            }
            if (on(1) && s % 2 == 0 && r.Chance(0.2f + 0.4f * dens) && ((bar - secStart) % 4) < 3) melodyNote(I_MUSICBOX, 12, 1, 0.8f * rg(1));
        } break;
        case M_LOUNGE: {
            bool lastBarOfSection = (bar - secStart) == secLen - 1;
            if (lastBarOfSection) { if (on(4) && s == 0) Note(I_CLINK, 96, 0.5f, 0.6f, offSec, bpm, 0.5f); break; }   // between songs: the band rests
            if (on(2) && s % 4 == 0) {   // a walking bass: root, then steps toward the next chord
                int beat = s / 4, deg = lastBass + (beat == 0 ? 0 : beat == 1 ? 2 : beat == 2 ? 4 : 5);
                Note(I_UPRIGHT, root - 24 + ScaleSemis(deg, m.minor), 0.9f, 0.9f * rg(2), offSec, bpm, 0);
            }
            if (on(3) && s % 4 == 0) Note(I_BRUSH, 60, 0.9f, ((s / 4) % 2 == 1 ? 1.0f : 0.6f) * rg(3), offSec, bpm);
            if (on(3) && (s == 4 || s == 12)) Note(I_SNARE, 50, 0.2f, 0.25f * rg(3), offSec, bpm);
            if (on(0) && s == 0) for (int c = 0; c < 4; c++) Note(I_SQUEEZE, chordNotes[c], 3.5f, 0.45f * rg(0), offSec + c * 0.01f, bpm);
            if (on(1) && ((bar - secStart) % 4) < 2 && s % 4 == 0 && r.Chance(0.3f + 0.4f * dens)) melodyNote(I_HORN, 12, r.R(1, 3), 0.75f * rg(1));
        } break;
        }
    }
    float VoiceSample(MVoice& x, float bright) {
        const InstrDef& id = Instrument(x.inst);
        float t = x.t, env;
        float atk = std::max(0.001f, id.atk), rel = std::max(0.01f, id.rel);
        if (t < atk) env = t / atk;
        else env = id.sus + (1 - id.sus) * expf(-(t - atk) / std::max(0.01f, id.dec));
        if (t > x.hold) { if (!x.released) { x.released = true; x.rel0 = env; } env = x.rel0 * expf(-(t - x.hold) / rel * 3); if (t > x.hold + rel * 1.2f) x.on = false; }
        else if (id.sus <= 0 && env < 0.001f && t > atk) x.on = false;
        float f = id.vibrato > 0 ? x.f * (1 + id.vibrato * sinf(TAU * 5.2f * t)) : x.f;
        float s = 0;
        auto adv = [&](int k, float fr) { x.ph[k] += fr / gSR; if (x.ph[k] >= 1) x.ph[k] -= 1; return x.ph[k]; };
        if (x.dc == 1 && id.detune > 0) x.dc = powf(2, id.detune / 1200);
        float dc = x.dc;
        switch (x.inst) {
        case I_PAD: s = (adv(0, f) * 2 - 1) + (adv(1, f * dc) * 2 - 1) + (adv(2, f / dc) * 2 - 1); s = x.lp.Run(s) * 0.45f; break;
        case I_BOWED: s = x.lp.Run((adv(0, f) * 2 - 1) + (adv(1, f * dc) * 2 - 1) * 0.6f) * 0.7f; break;
        case I_HARMONIUM: case I_SQUEEZE: { float p0 = adv(0, f), p1 = adv(1, f * dc); s = x.lp.Run((p0 < 0.3f ? 1.0f : -0.43f) + (p1 * 2 - 1) * 0.7f) * 0.6f; } break;
        case I_HORN: { float br = std::min(1.0f, t / 0.15f); x.lp.Set(BQ_LP, id.cutoff * (0.4f + 0.8f * br) * (0.5f + bright), 1.2f); s = x.lp.Run((adv(0, f) * 2 - 1) + (adv(1, f * dc) * 2 - 1) * 0.5f) * 0.7f; } break;
        case I_GLASS: case I_EPIANO: case I_MUSICBOX: case I_CLINK: {
            float idx = id.fmIndex * expf(-t * (x.inst == I_EPIANO ? 3.0f : 1.5f));
            float mod = sinf(TAU * adv(1, f * id.fmRatio));
            s = sinf(TAU * adv(0, f) + idx * mod);
            if (x.inst == I_EPIANO) s += 0.25f * sinf(TAU * adv(2, f * 2)) * expf(-t * 4);
        } break;
        case I_PIANO: {   // additive: inharmonic partials, the high ones dying first, a hammer tap at the start
            float B = id.fmIndex;
            if (x.pDec[0] == 0) for (int k = 0; k < 6; k++) x.pDec[k] = expf(-(0.6f + (k + 1) * 0.9f) / gSR);
            for (int k = 0; k < 6; k++) { int n = k + 1; float pf = f * n * sqrtf(1 + B * n * n); if (pf > gSR * 0.45f) break; s += sinf(TAU * adv(k, pf)) * x.pAmp[k] / n; x.pAmp[k] *= x.pDec[k]; }
            if (t < 0.01f) s += r.N() * (1 - t / 0.01f) * 0.25f;
            s *= 0.8f;
        } break;
        case I_UPRIGHT: case I_BASS: {   // Karplus-Strong: a plucked string
            if (x.ksLen == 0) { x.ksLen = std::clamp((int)(gSR / std::max(30.0f, f)), 2, 1023); for (int k = 0; k < x.ksLen; k++) x.ks[k] = r.N(); x.ksI = 0; }
            s = x.ks[x.ksI]; int nx = x.ksI + 1 < x.ksLen ? x.ksI + 1 : 0;
            x.ks[x.ksI] = (x.ks[x.ksI] + x.ks[nx]) * 0.5f * (x.inst == I_UPRIGHT ? 0.996f : 0.998f); x.ksI = nx;
            s = x.lp.Run(s) * 1.4f + sinf(TAU * adv(0, f)) * 0.3f * expf(-t * 3);
        } break;
        case I_KICK: { float kf = 45 + 70 * expf(-t * 30); s = sinf(TAU * adv(0, kf)) * 1.2f; } break;
        case I_SNARE: s = x.bp.Run(r.N()) * 1.3f + sinf(TAU * adv(0, 190)) * 0.3f * expf(-t * 30); break;
        case I_BRUSH: s = x.bp.Run(r.N()) * (0.5f + 0.5f * sinf(3.14159f * std::min(1.0f, t / 0.25f))); break;
        case I_HAT: s = x.bp.Run(r.N()); break;
        default: break;
        }
        return s * env * x.amp;
    }
    void Render(float* L, float* R, int n, const LayerState& s, double) override {
        const MusicSettings& m = s.music;
        if (!seeded || m.seed != seed || m.style != style) {   // a new seed or style: start composing afresh
            seeded = true; seed = m.seed; style = m.style; r.s = m.seed ? m.seed * 2654435761u + 1 : 1;
            for (auto& x : v) x.on = false;
            const StyleDef& st = Style(style);
            stepsPerBar = st.beatsPerBar * 4; swing = st.swing; bar = 0; secStart = 0; stepIdx = -1; beats = 0;
            for (int k = 0; k < MAX_INSTR; k++) roleGain[k] = 0;
            NewSection(m);
            vinylLP.Set(BQ_HP, 3000, 0.7f); murmur.Set(BQ_BP, 450, 1.5f);
        }
        float bpm = std::clamp(m.bpm, Style(style).bpmLo, Style(style).bpmHi);
        double dBeat = bpm / 60.0 / gSR;
        const StudyNumbers& N = Numbers();
        float barSec = 60 / bpm * Style(style).beatsPerBar;
        float ramp = n / gSR / std::max(1.0f, N.sectionFadeBars * barSec);
        for (int k = 0; k < MAX_INSTR; k++) { float tg = roleTarget[k] * (m.instrOn[k] ? 1.0f : 0.0f); roleGain[k] += std::clamp(tg - roleGain[k], -ramp, ramp); }
        // steps that fall inside this block
        double endBeats = beats + dBeat * n;
        long long lastStep = (long long)floor(endBeats * 4);
        for (long long st = stepIdx + 1; st <= lastStep; st++) {
            double stepBeat = st / 4.0;
            float off = (float)((stepBeat - beats) / dBeat / gSR);
            int s16 = (int)(st % stepsPerBar);
            bar = (int)(st / stepsPerBar);
            Step(s16, std::max(0.0f, off), m);
        }
        stepIdx = lastStep; beats = endBeats;
        float bright = m.brightness;
        for (auto& x : v) if (x.on) {
            const InstrDef& id = Instrument(x.inst);
            if (id.cutoff > 0) x.lp.Set(BQ_LP, id.cutoff * (0.4f + 1.2f * bright), 0.7f);
            if (x.inst == I_SNARE || x.inst == I_BRUSH) x.bp.Set(BQ_BP, x.inst == I_SNARE ? 2800 : 5200, 0.8f);
            if (x.inst == I_HAT) x.bp.Set(BQ_HP, 8000, 0.7f);
        }
        bool vinyl = style == M_LOFI && m.instrOn[3], room = style == M_LOUNGE && m.instrOn[4];
        for (int i = 0; i < n; i++) {
            float a = 0, b = 0;
            for (auto& x : v) {
                if (!x.on) continue;
                if (x.delay > 0) { x.delay -= 1 / gSR; continue; }
                float smp = VoiceSample(x, bright);
                a += smp * x.GL(); b += smp * x.GR();
                x.t += 1 / gSR;
            }
            if (vinyl) {   // crackle and a faint hiss
                crackAcc += 9 / gSR * r.R(0.2f, 1.8f);
                float c = 0; if (crackAcc >= 1) { crackAcc -= 1; c = r.N() * 0.25f; }
                float h = vinylLP.Run(r.N()) * 0.006f + c * 0.3f;
                a += h * roleGain[3]; b += h * roleGain[3];
            }
            if (room) {   // the lounge's room: a low murmur, and a glass now and then
                clinkAcc += 0.06f / gSR;
                if (clinkAcc >= 1) { clinkAcc = 0; Note(I_CLINK, 96 + r.I(6), 0.4f, r.R(0.3f, 0.8f), 0, bpm, r.N()); }
                float mu = murmur.Run(r.N()) * 0.02f * roleGain[4];
                a += mu; b += mu * 0.9f;
            }
            L[i] = a; R[i] = b;
        }
    }
};

std::unique_ptr<Gen> MakeGen(int kind, uint32_t seed) {
    std::unique_ptr<Gen> g;
    switch (kind) {
    case L_RAIN: g = std::make_unique<RainGen>(); break;
    case L_STREAM: g = std::make_unique<StreamGen>(); break;
    case L_SURF: g = std::make_unique<SurfGen>(); break;
    case L_WIND: g = std::make_unique<WindGen>(); break;
    case L_FIRE: g = std::make_unique<FireGen>(); break;
    case L_FOREST: g = std::make_unique<ForestGen>(); break;
    case L_THUNDER: g = std::make_unique<ThunderGen>(); break;
    case L_NAUTILUS: g = std::make_unique<NautilusGen>(); break;
    case L_MUSIC: g = std::make_unique<MusicGen>(); break;
    default: g = std::make_unique<NoiseGen>(kind); break;
    }
    g->r.s = seed ? seed : 1;
    return g;
}

// ---------------------------------------------------------------- the desk
struct Smooth { float v = 0, t = 0; bool init = false; void To(float x) { t = x; if (!init) { v = x; init = true; } } float Step(float k) { v += (t - v) * k; return v; } };
struct Channel {
    uint32_t id = 0; int kind = 0, style = -1;
    std::unique_ptr<Gen> gen;
    LayerState want;                // what the desk asks for
    LayerState cur;                 // smoothed controls handed to the generator
    Smooth gain, pan, width, tone;
    BQ toneLoL, toneLoR, toneHiL, toneHiR;
    bool dying = false;
};
struct Engine {
    MixerState state;
    std::vector<Channel> ch;
    uint32_t seed = 12345;
    double t = 0;
    bool onBreak = false; Smooth breakGain;
    double fadeT = 0;
    BQ lowL, lowR, highL, highR;
    float compEnv = 0, limGain = 1;
    float lastLowCut = -1, lastHighCut = -1;
    std::vector<float> bufL, bufR, musL, musR, ambL, ambR;
    MusicGen* music = nullptr; float musicAudible = 0;

    void Sync() {
        for (auto& c : ch) c.dying = true;
        bool anySolo = false;
        for (auto& l : state.layers) anySolo |= l.solo;
        for (const LayerState& l : state.layers) {
            Channel* c = nullptr;
            for (auto& x : ch) if (x.id == l.id && x.kind == l.kind) { c = &x; break; }
            if (!c) {
                ch.emplace_back(); c = &ch.back();
                c->id = l.id; c->kind = l.kind; c->gen = MakeGen(l.kind, seed ^ (l.id * 2654435761u));
                c->cur = l; c->gain.To(0); c->gain.init = true; c->gain.v = 0;
            }
            c->dying = false;
            c->want = l;
            bool audible = !l.mute && (!anySolo || l.solo);
            float norm = LayerNorm(l.kind) * (l.kind == L_MUSIC ? StyleNorm(l.music.style) : 1.0f);
            c->gain.To(audible ? l.level * l.level * norm : 0);   // (a squared taper: the slider feels even)
            c->pan.To(l.pan); c->width.To(l.width); c->tone.To(l.tone);
        }
        for (auto& c : ch) if (c.dying) c.gain.To(0);
    }
    void RenderBlock(float* out, int n) {
        const StudyNumbers& N = Numbers();
        if ((int)bufL.size() < n) { bufL.resize(n); bufR.resize(n); musL.resize(n); musR.resize(n); ambL.resize(n); ambR.resize(n); }
        std::fill(musL.begin(), musL.begin() + n, 0.0f); std::fill(musR.begin(), musR.begin() + n, 0.0f);
        std::fill(ambL.begin(), ambL.begin() + n, 0.0f); std::fill(ambR.begin(), ambR.begin() + n, 0.0f);
        float k = 1 - expf(-(n / gSR) / N.smoothingSeconds);
        music = nullptr; float mAud = 0;
        for (auto& c : ch) {
            float g = c.gain.Step(k), pan = c.pan.Step(k), width = c.width.Step(k), tone = c.tone.Step(k);
            for (int p = 0; p < MAX_PARAMS; p++) c.cur.p[p] += (c.want.p[p] - c.cur.p[p]) * k;
            if (Layer(c.kind).family == F_NATURE) for (int p = 0; p < Layer(c.kind).nParams; p++) if (Layer(c.kind).p[p].choices) c.cur.p[p] = c.want.p[p]; // (choices snap)
            c.cur.music = c.want.music; c.cur.kind = c.kind;
            if (g < 0.0005f && c.gain.t <= 0) continue;
            c.gen->Render(bufL.data(), bufR.data(), n, c.cur, t);
            if (c.kind == L_MUSIC) { music = static_cast<MusicGen*>(c.gen.get()); mAud = std::max(mAud, g); }
            // tone: a tilt around 800 Hz; width: mid/side; pan: constant power
            {
                c.toneLoL.Set(BQ_LS, 800, 0.7f, -6 * tone); c.toneHiL.Set(BQ_HS, 800, 0.7f, 6 * tone);
                c.toneLoR.b0 = c.toneLoL.b0; c.toneLoR.b1 = c.toneLoL.b1; c.toneLoR.b2 = c.toneLoL.b2; c.toneLoR.a1 = c.toneLoL.a1; c.toneLoR.a2 = c.toneLoL.a2;
                c.toneHiR.b0 = c.toneHiL.b0; c.toneHiR.b1 = c.toneHiL.b1; c.toneHiR.b2 = c.toneHiL.b2; c.toneHiR.a1 = c.toneHiL.a1; c.toneHiR.a2 = c.toneHiL.a2;
            }
            c.cur.tone = tone;
            float pl = PanL(pan) * 1.4142f, pr = PanR(pan) * 1.4142f;
            float* dL = c.kind == L_MUSIC ? musL.data() : ambL.data(); float* dR = c.kind == L_MUSIC ? musR.data() : ambR.data();
            for (int i = 0; i < n; i++) {
                float a = c.toneHiL.Run(c.toneLoL.Run(bufL[i])), b = c.toneHiR.Run(c.toneLoR.Run(bufR[i]));
                float mid = (a + b) * 0.5f, side = (a - b) * 0.5f * width;
                dL[i] += (mid + side) * g * pl; dR[i] += (mid - side) * g * pr;
            }
        }
        musicAudible = mAud;
        ch.erase(std::remove_if(ch.begin(), ch.end(), [](const Channel& c) { return c.dying && c.gain.v < 0.0005f; }), ch.end());
        // the music bus: a gentle compressor (no sudden loudness), ducked on the chronometer's breaks
        breakGain.To(onBreak && state.master.duckOnBreak ? DbToLin(N.breakDuckDb) : 1.0f);
        float bk = 1 - expf(-(n / gSR) / 2.0f);
        float bg = breakGain.Step(bk);
        // the master: low/high cut, warmth, volume, the fade timer, the limiter
        const MasterState& M = state.master;
        if (M.lowCut != lastLowCut) { lowL.Set(BQ_HP, std::max(10.0f, M.lowCut), 0.707f); lowR = lowL; lastLowCut = M.lowCut; }
        if (M.highCut != lastHighCut) { highL.Set(BQ_LP, std::min(gSR * 0.45f, M.highCut), 0.707f); highR = highL; lastHighCut = M.highCut; }
        float fade = 1;
        if (M.fadeMinutes > 0) { float u = (float)(fadeT / (M.fadeMinutes * 60.0)); fade = u >= 1 ? 0 : 0.5f + 0.5f * cosf(3.14159f * u); }
        float drive = 1 + 2.5f * M.warmth, dnorm = 1 / tanhf(drive * 0.5f) * 0.5f;
        float relK = expf(-1 / (0.15f * gSR));
        for (int i = 0; i < n; i++) {
            float ml = musL[i], mr = musR[i];
            float lev = std::max(fabsf(ml), fabsf(mr));
            compEnv = lev > compEnv ? compEnv + (lev - compEnv) * 0.01f : compEnv * 0.9995f;
            float cg = compEnv > N.musicCompThreshold ? powf(N.musicCompThreshold / compEnv, 1 - 1 / N.musicCompRatio) : 1.0f;
            float L = ambL[i] + ml * cg * bg, R = ambR[i] + mr * cg * bg;
            L = highL.Run(lowL.Run(L)); R = highR.Run(lowR.Run(R));
            if (M.warmth > 0.01f) { L = tanhf(L * drive * 0.5f) * dnorm; R = tanhf(R * drive * 0.5f) * dnorm; }
            L *= M.volume * fade; R *= M.volume * fade;
            float pk = std::max(fabsf(L), fabsf(R));
            if (pk * limGain > N.limiterCeiling) limGain = N.limiterCeiling / std::max(1e-6f, pk);
            else limGain = 1 - (1 - limGain) * relK;
            out[i * 2] = std::clamp(L * limGain, -N.limiterCeiling, N.limiterCeiling);
            out[i * 2 + 1] = std::clamp(R * limGain, -N.limiterCeiling, N.limiterCeiling);
        }
        t += n / gSR; fadeT += n / gSR;
    }
};
Engine& E() { static Engine e; return e; }
}  // namespace

void SetState(const MixerState& s) {
    Engine& e = E();
    if (s.master.fadeMinutes != e.state.master.fadeMinutes) e.fadeT = 0;
    e.state = s;
    if ((int)e.state.layers.size() > MAX_LAYERS) e.state.layers.resize(MAX_LAYERS);
    e.Sync();
}
const MixerState& GetState() { return E().state; }
void SetBreak(bool b) { E().onBreak = b; }
void RestartFade() { E().fadeT = 0; }
float FadeProgress() { const Engine& e = E(); return e.state.master.fadeMinutes > 0 ? std::min(1.0f, (float)(e.fadeT / (e.state.master.fadeMinutes * 60.0))) : 0.0f; }
void Render(float* out, int frames, int sampleRate) {
    gSR = (float)sampleRate;
    Engine& e = E();
    for (int base = 0; base < frames; base += 64) e.RenderBlock(out + base * 2, std::min(64, frames - base));
}
double Beat() { MusicGen* m = E().music; return m ? m->beats : E().t * 100 / 60.0; }
int BeatsPerBar() { MusicGen* m = E().music; return m ? Style(m->style).beatsPerBar : 4; }
bool MusicPlaying() { return E().music && E().musicAudible > 0.01f; }
float RainLevel() {
    float v = 0;
    for (auto& c : E().ch) if (c.kind == L_RAIN && !c.dying) v = std::max(v, c.cur.p[0] * std::min(1.0f, c.gain.v * 2.5f));
    return std::min(1.0f, v);
}
float FireSize() {
    float v = 0;
    for (auto& c : E().ch) if (c.kind == L_FIRE && !c.dying && c.gain.t > 0) v = std::max(v, c.cur.p[0]);
    return v;
}
bool LoungeBandPlaying() { MusicGen* m = E().music; return m && m->style == M_LOUNGE && E().musicAudible > 0.01f; }
void ResetEngine(uint32_t seed) { Engine& e = E(); e.ch.clear(); e.seed = seed; e.t = 0; e.fadeT = 0; e.limGain = 1; e.compEnv = 0; e.lastLowCut = e.lastHighCut = -1; e.Sync(); }
}
