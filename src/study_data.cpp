// The Study's data file (Master Reference: "every number lives in data.cpp or a Study data file"). The soundscape's
// layer controls and synthesis constants, the music styles, the presets, and the Study's timings and limits.
// The nature layers are "tuned by listening": change the tuning slots here, never in study_audio.cpp.
#include "study_audio.h"
#include "study_data.h"
#include <algorithm>

namespace study {

// Tuning slots per layer (what each of the 8 numbers means is written beside it).
static const LayerDef LAYERS[L_KIND_COUNT] = {
    // noise: drift is a very slow wander of level and tone (0.02-0.1 Hz, up to 3 dB) so the sound never feels frozen
    {"White", F_NOISE, 1, {{"drift", 0, 1, 0.5f, nullptr}},                     {0.02f, 0.1f, 3, 0, 0, 0, 0, 0}},   // drift Hz lo, hi, max dB
    {"Pink", F_NOISE, 1, {{"drift", 0, 1, 0.5f, nullptr}},                      {0.02f, 0.1f, 3, 0, 0, 0, 0, 0}},
    {"Brown", F_NOISE, 2, {{"rumble", 0, 1, 0.3f, nullptr}, {"drift", 0, 1, 0.5f, nullptr}},
                                                                                {0.02f, 0.1f, 3, 0.02f, 55, 9, 0, 0}},  // .., leak, sub Hz, sub dB max
    {"Grey", F_NOISE, 1, {{"drift", 0, 1, 0.5f, nullptr}},                      {0.02f, 0.1f, 3, 90, 7, 3400, -6, 9000}}, // .., low shelf Hz, +dB, dip Hz, dB, high shelf Hz
    {"Blue", F_NOISE, 1, {{"drift", 0, 1, 0.5f, nullptr}},                      {0.02f, 0.1f, 3, 0, 0, 0, 0, 0}},
    // nature
    {"Rain", F_NATURE, 4, {{"intensity", 0, 1, 0.55f, nullptr}, {"drop size", 0, 1, 0.5f, nullptr},
                           {"surface", 0, 4, 1, "roof|window|leaves|tent|sea"}, {"distance", 0, 1, 0.3f, nullptr}},
                                                                                {2600, 0.7f, 160, 900, 3200, 0.012f, 0.035f, 0.25f}}, // bed BP Hz, bed Q, drops/s at full, bubble Hz lo, hi, drop len lo, hi, bed gain
    {"Stream", F_NATURE, 3, {{"flow", 0, 1, 0.5f, nullptr}, {"babble", 0, 1, 0.5f, nullptr}, {"distance", 0, 1, 0.3f, nullptr}},
                                                                                {300, 5200, 0.25f, 3.0f, 30, 500, 1800, 0.04f}},  // band lo Hz, hi Hz, AM Hz lo, hi, bubbles/s at full, bubble Hz lo, hi, bubble len
    {"Surf", F_NATURE, 3, {{"wave size", 0, 1, 0.5f, nullptr}, {"period", 5, 14, 9, nullptr}, {"distance", 0, 1, 0.4f, nullptr}},
                                                                                {420, 3800, 0.35f, 0.25f, 0.6f, 0, 0, 0}},        // swell LP Hz, break HP Hz, break share of period, wash share, bed floor
    {"Wind", F_NATURE, 3, {{"strength", 0, 1, 0.5f, nullptr}, {"gustiness", 0, 1, 0.5f, nullptr}, {"whistle", 200, 1400, 600, nullptr}},
                                                                                {300, 1400, 0.15f, 1.2f, 18, 0.22f, 0, 0}},       // LP lo Hz, hi Hz, gust Hz lo, hi, whistle Q, whistle share
    {"Fire", F_NATURE, 3, {{"size", 0, 1, 0.6f, nullptr}, {"crackle", 0, 1, 0.5f, nullptr}, {"pops", 0, 1, 0.3f, nullptr}},
                                                                                {240, 45, 1.5f, 2500, 0.004f, 0.02f, 0, 0}},      // roar LP Hz, crackles/s at full, pops/s at full, crackle HP Hz, crackle len, pop len
    {"Forest", F_NATURE, 3, {{"time of day", 0, 3, 1, "dawn|day|dusk|night"}, {"density", 0, 1, 0.5f, nullptr}, {"distance", 0, 1, 0.4f, nullptr}},
                                                                                {5000, 3.0f, 4500, 22, 380, 0.08f, 0, 0}},        // rustle HP Hz, birds/s at full (dawn), cricket Hz, cricket pulse Hz, owl Hz, owls/s
    {"Thunder", F_NATURE, 2, {{"frequency", 0, 1, 0.4f, nullptr}, {"distance", 0, 1, 0.6f, nullptr}},
                                                                                {120, 40, 0.012f, 0.06f, 3.5f, 9, 0, 0}},         // rumble LP Hz, min Hz, rolls/s lo, hi, roll len lo, hi s
    {"Nautilus", F_NATURE, 2, {{"hum level", 0, 1, 0.5f, nullptr}, {"creak rate", 0, 1, 0.4f, nullptr}},
                                                                                {50, 0.05f, 0.25f, 1100, 0.04f, 280, 0, 0}},      // hum Hz, creaks/s lo, hi, sonar Hz, pings/s, glass wash LP Hz
    // music (its controls are MusicSettings; the strip is shared)
    {"Music", F_MUSIC, 0, {}, {0, 0, 0, 0, 0, 0, 0, 0}},
};
const LayerDef& Layer(int k) { return LAYERS[std::clamp(k, 0, L_KIND_COUNT - 1)]; }

// Progressions are scale degrees (0 = the tonic); each section of the music picks a row. -1 ends a row.
static const StyleDef STYLES[M_STYLE_COUNT] = {
    {"Ambient drift", 50, 90, 60, 4, false, 3, {"pads", "bowed", "glass"},
     {{0, 5, 3, 4, -1}, {0, 3, 5, 4, -1}, {5, 3, 0, 4, -1}, {0, 2, 5, 3, -1}}, 0.35f, 0.4f},
    {"Lo-fi", 70, 90, 78, 4, true, 4, {"e-piano", "kit", "bass", "vinyl"},
     {{1, 4, 0, 5, -1}, {3, 4, 2, 5, -1}, {0, 5, 1, 4, -1}, {5, 3, 0, 4, -1}}, 0.5f, 0.45f},
    {"Sparse piano", 50, 80, 58, 4, false, 1, {"piano"},
     {{0, 3, 4, 0, -1}, {5, 3, 0, 4, -1}, {0, 4, 5, 3, -1}, {3, 0, 4, 5, -1}}, 0.3f, 0.5f},
    {"Salon", 50, 90, 66, 3, false, 2, {"harmonium", "music box"},
     {{0, 3, 4, 0, -1}, {0, 5, 1, 4, -1}, {0, 4, 0, 3, -1}, {3, 4, 0, 5, -1}}, 0.45f, 0.5f},
    {"Lounge band", 90, 120, 104, 4, true, 5, {"squeezebox", "horn", "bass", "brushes", "room"},
     {{1, 4, 0, 5, -1}, {0, 5, 1, 4, -1}, {3, 3, 0, 5, -1}, {1, 4, 2, 5, -1}}, 0.55f, 0.5f},
};
const StyleDef& Style(int s) { return STYLES[std::clamp(s, 0, M_STYLE_COUNT - 1)]; }

LayerState NewLayer(int kind) {
    static uint32_t nextId = 1;
    LayerState s;
    s.id = nextId++;
    s.kind = std::clamp(kind, 0, L_KIND_COUNT - 1);
    const LayerDef& d = Layer(s.kind);
    for (int i = 0; i < d.nParams; i++) s.p[i] = d.p[i].def;
    if (s.kind == L_MUSIC) { const StyleDef& st = Style(s.music.style); s.music.bpm = st.bpmDef; s.music.density = st.density; s.music.brightness = st.brightness; }
    return s;
}

// ---- the presets that ship with the Study
static LayerState L(int kind, float level, std::initializer_list<float> params = {}) {
    LayerState s = NewLayer(kind); s.level = level;
    int i = 0; for (float v : params) if (i < MAX_PARAMS) s.p[i++] = v;
    return s;
}
static LayerState M(int style, float level, float bpm = 0) {
    LayerState s = NewLayer(L_MUSIC); s.level = level;
    s.music.style = style; const StyleDef& st = Style(style);
    s.music.bpm = bpm > 0 ? bpm : st.bpmDef; s.music.density = st.density; s.music.brightness = st.brightness;
    s.music.minor = style == M_AMBIENT || style == M_PIANO;
    return s;
}
static std::vector<Preset> MakePresets() {
    std::vector<Preset> v;
    auto add = [&](const char* name, std::initializer_list<LayerState> ls) { Preset p{}; p.name = name; p.n = 0; for (auto& l : ls) p.layers[p.n++] = l; v.push_back(p); };
    add("Deep focus",      {L(L_BROWN, 0.75f, {0.35f, 0.5f}), L(L_NAUTILUS, 0.5f, {0.6f, 0.25f})});
    add("Rainy library",   {L(L_RAIN, 0.65f, {0.5f, 0.45f, 1, 0.25f}), L(L_FIRE, 0.45f, {0.45f, 0.4f, 0.2f}), M(M_PIANO, 0.5f)});
    add("Mountain stream", {L(L_STREAM, 0.7f, {0.55f, 0.5f, 0.25f}), L(L_FOREST, 0.5f, {1, 0.45f, 0.4f}), L(L_WIND, 0.3f, {0.25f, 0.3f, 500})});
    add("Night watch",     {L(L_SURF, 0.6f, {0.5f, 10, 0.5f}), L(L_WIND, 0.35f, {0.3f, 0.4f, 450}), L(L_FOREST, 0.4f, {3, 0.4f, 0.5f}), M(M_AMBIENT, 0.4f, 56)});
    add("Storm at sea",    {L(L_RAIN, 0.75f, {0.9f, 0.7f, 0, 0.2f}), L(L_THUNDER, 0.6f, {0.6f, 0.5f}), L(L_WIND, 0.55f, {0.75f, 0.75f, 700}), L(L_NAUTILUS, 0.45f, {0.3f, 0.8f})});
    add("Late lounge",     {M(M_LOUNGE, 0.65f), L(L_BROWN, 0.25f, {0.2f, 0.3f})});
    add("Fireside",        {L(L_FIRE, 0.65f, {0.65f, 0.5f, 0.35f}), L(L_RAIN, 0.4f, {0.35f, 0.4f, 0, 0.45f}), M(M_SALON, 0.4f)});
    add("Pure grey",       {L(L_GREY, 0.75f, {0.4f})});
    return v;
}
int PresetCount() { static std::vector<Preset> p = MakePresets(); return (int)p.size(); }
const Preset& GetPreset(int i) { static std::vector<Preset> p = MakePresets(); return p[std::clamp(i, 0, (int)p.size() - 1)]; }

// ---- the music engine's instruments
static const InstrDef INSTR[I_COUNT] = {
    //  name        atk    dec   sus   rel    gain   cutoff  fmR   fmI   detune vib   pan
    {"pads",       1.8f,  1.0f, 0.8f, 2.5f,  0.10f, 1400,   0,    0,    9,     0.002f, 0},
    {"bowed",      0.5f,  0.6f, 0.8f, 1.2f,  0.09f, 1900,   0,    0,    4,     0.006f, 0.3f},
    {"glass",      0.01f, 2.4f, 0.0f, 1.5f,  0.07f, 6000,   3.5f, 1.4f, 0,     0,      -0.35f},
    {"e-piano",    0.005f,1.4f, 0.2f, 0.4f,  0.13f, 3800,   1.0f, 1.6f, 3,     0,      -0.15f},
    {"piano",      0.004f,3.2f, 0.0f, 0.8f,  0.16f, 5200,   0,    0.0004f, 0,  0,      0},
    {"harmonium",  0.12f, 0.3f, 0.85f,0.3f,  0.08f, 1700,   0,    0,    6,     0.003f, -0.2f},
    {"music box",  0.002f,1.6f, 0.0f, 0.6f,  0.09f, 7000,   7.0f, 1.2f, 0,     0,      0.35f},
    {"squeezebox", 0.08f, 0.3f, 0.8f, 0.25f, 0.08f, 2600,   0,    0,    7,     0.012f, -0.3f},
    {"horn",       0.09f, 0.4f, 0.75f,0.35f, 0.10f, 1500,   0,    0,    3,     0.008f, 0.3f},
    {"upright",    0.004f,1.1f, 0.0f, 0.3f,  0.20f, 900,    0,    0,    0,     0,      0},
    {"bass",       0.005f,0.8f, 0.4f, 0.2f,  0.17f, 700,    0,    0,    0,     0,      0},
    {"kick",       0.001f,0.28f,0.0f, 0.05f, 0.30f, 0,      0,    0,    0,     0,      0},
    {"brushes",    0.03f, 0.25f,0.0f, 0.1f,  0.05f, 5200,   0,    0,    0,     0,      0.1f},
    {"snare",      0.001f,0.16f,0.0f, 0.05f, 0.09f, 3000,   0,    0,    0,     0,      0.05f},
    {"hat",        0.001f,0.05f,0.0f, 0.02f, 0.035f,9000,   0,    0,    0,     0,      -0.2f},
    {"clink",      0.001f,0.5f, 0.0f, 0.2f,  0.035f,9000,   5.4f, 2.0f, 0,     0,      0.5f},
};
const InstrDef& Instrument(int i) { return INSTR[std::clamp(i, 0, I_COUNT - 1)]; }

// perceived-loudness gains (measured by --study-audio-test; 1 = not yet measured)
//                                          white  pink   brown  grey   blue   rain   stream surf   wind   fire   forest thunder nautilus music
static const float LAYER_NORM[L_KIND_COUNT] = {0.649f, 0.855f, 0.606f, 1.288f, 0.715f, 1.153f, 1.033f, 2.080f, 1.436f, 3.247f, 5.639f, 8.0f, 4.037f, 1};
//                                          ambient lo-fi  piano  salon  lounge
static const float STYLE_NORM[M_STYLE_COUNT] = {3.546f, 1.696f, 6.247f, 3.512f, 4.167f};   // (thunder is intermittent: it sits under the rest, not at the target)
float LayerNorm(int k) { return LAYER_NORM[std::clamp(k, 0, L_KIND_COUNT - 1)]; }
float StyleNorm(int s) { return STYLE_NORM[std::clamp(s, 0, M_STYLE_COUNT - 1)]; }

// ---- the Study's timings and limits (study_data.h)
const StudyNumbers& Numbers() {
    static const StudyNumbers N{};
    return N;
}
}
