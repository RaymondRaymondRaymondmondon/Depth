// ================================================================ A Night Off (the Deep Arcade's night ashore)
// Included inside sound.cpp's anonymous namespace after sound_mouthful.inl. Design doc pp. 26-28, "Sound design": the Gull
// is heard before it's seen. The crowd bed is a murmur of layered formant voices whose density follows the crowd (it
// rises at the peak and thins at 2 a.m. until single conversations carry across the room); glass and wood; the jukebox's
// six songs (a shanty, a waltz, a music-hall number, a torch song, a jig, and the sad one after midnight that the bar
// sings along to); rain; the dog. Drink is audible: above 40 the mix low-passes and the murmur blurs, above 60 a faint
// ringing sits under everything, above 80 the jukebox is the only clear sound, and a blackout is silence. When the
// jukebox is quiet a ghost piano noodles from the stage; the morning screen plays a hungover waltz, with birds.
struct NoState {
    NoAudio want;
    float s = 0, ringS = 0;
    int tick = 0; float tickT = 0;
    int song = -9, mel = 0, melAt = 0; float songS = 0;
    float ambT = 0, ghostT = 0, voiceT = 0, birdT = 0;
};
NoState gNo;
void NoCueImpl(int kind, float vol, float pan, float pitch);
std::vector<Bed> gNoBeds;
bool gNoBedsReady = false;
struct NoSong { float root; int minor; float bpm; int bar; int inst, bassInst; float gain; std::vector<std::pair<int, int>> mel; int prog[4]; };
// composed here: (scale degree, eighths); -99 rests
const std::vector<NoSong>& NoSongs() {
    static std::vector<NoSong> S = {
        {110.0f, 1, 120, 6, I_SQUEEZE, I_SAW_BASS, 0.034f, {{0, 2}, {0, 1}, {2, 2}, {4, 1}, {3, 2}, {2, 1}, {1, 3}, {0, 2}, {0, 1}, {2, 2}, {4, 1}, {5, 3}, {4, 3}, {4, 2}, {5, 1}, {4, 2}, {2, 1}, {3, 2}, {1, 1}, {-1, 3}, {0, 6}}, {0, 5, 3, 4}},   // the shanty (6/8, minor)
        {146.8f, 0, 150, 6, I_FIDDLE, I_SUB_BASS, 0.03f, {{0, 2}, {2, 2}, {4, 2}, {7, 4}, {6, 2}, {5, 2}, {4, 2}, {2, 6}, {3, 2}, {5, 2}, {7, 2}, {9, 4}, {8, 2}, {7, 6}, {4, 2}, {5, 2}, {4, 2}, {2, 4}, {1, 2}, {0, 6}}, {0, 3, 4, 0}},   // the waltz (3/4)
        {130.8f, 0, 132, 8, I_PLUCK, I_SAW_BASS, 0.032f, {{4, 1}, {4, 1}, {5, 1}, {4, 1}, {2, 2}, {0, 2}, {1, 1}, {2, 1}, {4, 2}, {2, 4}, {4, 1}, {4, 1}, {5, 1}, {7, 1}, {6, 2}, {4, 2}, {2, 8}, {3, 2}, {5, 2}, {4, 2}, {2, 2}, {1, 4}, {0, 4}}, {0, 3, 4, 0}},   // the music-hall number (4/4)
        {98.0f, 1, 72, 8, I_FLUTE, I_SUB_BASS, 0.03f, {{4, 3}, {3, 1}, {2, 4}, {0, 4}, {2, 2}, {3, 2}, {4, 4}, {6, 3}, {5, 1}, {4, 8}, {3, 2}, {2, 2}, {1, 4}, {-1, 4}, {0, 8}}, {0, 3, 4, 0}},   // the torch song (slow, minor)
        {164.8f, 0, 186, 6, I_FIDDLE, I_PLUCK, 0.028f, {{0, 1}, {2, 1}, {4, 1}, {7, 1}, {4, 1}, {2, 1}, {1, 1}, {3, 1}, {5, 1}, {8, 1}, {5, 1}, {3, 1}, {0, 1}, {2, 1}, {4, 1}, {2, 1}, {1, 1}, {0, 1}, {-1, 3}, {0, 3}}, {0, 4, 0, 4}},   // the jig (6/8)
        {110.0f, 1, 84, 6, I_ORGAN, I_SUB_BASS, 0.032f, {{0, 2}, {2, 2}, {3, 2}, {4, 4}, {3, 2}, {2, 6}, {0, 2}, {-1, 2}, {0, 2}, {2, 6}, {3, 2}, {4, 2}, {5, 2}, {4, 4}, {2, 2}, {0, 6}}, {0, 5, 3, 4}},   // the sad one (3/4, minor; the bar sings)
        {146.8f, 0, 104, 6, I_PAD, I_SUB_BASS, 0.026f, {{0, 2}, {2, 2}, {4, 2}, {7, 4}, {6, 2}, {5, 2}, {4, 2}, {2, 6}, {1, 4}, {-99, 2}, {0, 6}}, {0, 3, 4, 0}},   // the hungover waltz (the morning)
    };
    return S;
}
float NoNote(const NoSong& s, int deg, int oct) {
    static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10};
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7;
    return s.root * powf(2.0f, oct + o + (s.minor ? MIN[d] : MAJ[d]) / 12.0f);
}
void NoSetupBeds() {
    gNoBeds.clear();
    auto bed = [&](int type, float f, float q, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, 0, lfoR, lfoD, ampR, ampD}; gNoBeds.push_back(b); };
    bed(1, 480, 2.2f, 0.7f, 0.35f, 4.3f, 0.75f);     // 0 the murmur, low (a crowd's vowels)
    bed(1, 1350, 2.6f, 0.9f, 0.3f, 5.1f, 0.8f);      // 1 the murmur, middle
    bed(1, 2500, 3.0f, 1.1f, 0.25f, 6.3f, 0.85f);    // 2 the murmur, high (consonants: it blurs first)
    bed(1, 3600, 0.8f, 0.2f, 0.3f, 0.8f, 0.3f);      // 3 rain on the windows and the yard
    bed(0, 130, 0.9f, 0.05f, 0.2f, 0.1f, 0.4f);      // 4 the room's low hum (the fire, the street)
    bed(3, 5800, 0, 0, 0, 0.2f, 0.2f);               // 5 the ringing (above 60)
    gNoBedsReady = true;
}
// a patron's gibberish: syllables of formant voice with a pitch and a rhythm; a laugh is falling "ha"s
void NoVoiceImpl(float pitch, float rhythm, int syl, float vol, float pan, bool laugh) {
    static const float V[5][2] = {{800, 1200}, {400, 2000}, {300, 2300}, {500, 900}, {350, 700}};
    Ctx c{vol, 1, 1, 1, B_AMB, 0.25f}; PanGains(pan, c.gl, c.gr);
    float t = 0, f = 110 * std::clamp(pitch, 0.6f, 2.4f);
    for (int k = 0; k < std::clamp(syl, 1, 14); k++) {
        int v = laugh ? 0 : (int)(R01() * 5) % 5;
        float dur = laugh ? 0.12f : RR(0.07f, 0.16f) / std::max(0.5f, rhythm);
        float f0 = laugh ? f * (1.6f - 0.08f * k) : f * RR(0.9f, 1.15f);
        Voice& x = Vox(c, f0, f0 * (laugh ? 0.85f : RR(0.9f, 1.1f)), dur, 0.05f, V[v][0], V[v][0] * 1.05f, V[v][1], V[v][1] * 0.97f, t);
        x.decPow = 1.3f;
        t += dur + (laugh ? 0.05f : RR(0.02f, 0.08f));
    }
}
void NoTick() {
    const NoAudio& a = gNo.want; const auto& S = NoSongs();
    // the song this tick (the jukebox: chosen by the night's clock, so every guest hears the same one)
    int want = a.over ? 6 : a.song;
    if (want != gNo.song) { gNo.song = want; gNo.mel = 0; gNo.melAt = gNo.tick; }
    if (gNo.song < 0 || gNo.song >= (int)S.size()) return;
    const NoSong& s = S[gNo.song];
    int t = gNo.tick - gNo.melAt, e = t % s.bar, bar = t / s.bar;
    float g = s.gain * gNo.songS * (a.drunk > 80 ? 1.15f : 1.0f) * a.songNear;
    int chord = s.prog[bar % 4];
    float td = 30.0f / s.bpm;
    // the bass and the chords: oom-pah-pah for the threes, a stride for the fours
    if (e == 0) PlayInst(s.bassInst, NoNote(s, chord, -1), td * 2, g * 1.2f, a.songPan - 0.1f, 0.4f);
    if (s.bar == 6 && (e == 2 || e == 4)) { PlayInst(I_PLUCK, NoNote(s, chord + 2, 0), td, g * 0.5f, a.songPan, 0.5f); PlayInst(I_PLUCK, NoNote(s, chord + 4, 0), td, g * 0.4f, a.songPan + 0.1f, 0.5f); }
    if (s.bar == 8 && (e == 2 || e == 6)) { PlayInst(I_PLUCK, NoNote(s, chord + 2, 0), td, g * 0.5f, a.songPan, 0.5f); PlayInst(I_PLUCK, NoNote(s, chord + 4, 0), td, g * 0.4f, a.songPan + 0.1f, 0.5f); }
    if (s.bar == 8 && e == 4) PlayInst(I_SUB_BASS, NoNote(s, chord + 4, -1), td * 2, g, a.songPan, 0.4f);
    // the tune; the sad one after midnight: the bar sings along (a choir under the organ)
    int total = 0, i = 0, n = (int)s.mel.size(), len = 0; for (const auto& m : s.mel) len += m.second;
    int at = t % std::max(1, len);
    for (; i < n; i++) { if (total == at) break; total += s.mel[i].second; }
    if (total == at && i < n && s.mel[i].first != -99) {
        float f = NoNote(s, s.mel[i].first, 1), dur = td * s.mel[i].second * 0.9f;
        PlayInst(s.inst, f, dur, g, a.songPan + 0.15f, 0.55f);
        if (gNo.song == 5 && a.singAlong) PlayInst(I_CHOIR, f * 0.5f, dur * 1.05f, g * 0.9f, a.songPan - 0.2f, 0.4f);
        if (gNo.song == 6) PlayInst(I_PAD, f * 1.012f, dur, g * 0.5f, 0.2f, 0.3f);   // (the hangover: slightly out of tune with itself)
    }
}
void NoEvents(float dt) {
    const NoAudio& a = gNo.want;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    // single conversations carry when the room thins (and laughter at the peak)
    float thin = std::clamp(1 - a.crowd / 30.0f, 0.0f, 1.0f);
    if (!a.over && a.crowd > 0 && chance(0.35f + 0.6f * thin)) NoVoiceImpl(RR(0.7f, 1.8f), RR(0.8f, 1.4f), 3 + (int)(R01() * 8), (0.15f + 0.35f * thin) * (a.drunk > 40 ? 0.7f : 1.0f), RR(-0.9f, 0.9f), R01() < 0.15f + 0.1f * (a.crowd / 40.0f));
    // glass and wood: clinks, a stool dragged, the till, the tab book's pen
    if (!a.over && chance(0.25f + a.crowd * 0.02f)) { Ctx c{RR(0.2f, 0.5f), 1, 1, 1, B_AMB, 0.3f}; PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(2600, 4200), 0, 0.25f, 0.05f); v.f1 = v.f0 * 0.995f; v.decPow = 2.6f; }
    if (!a.over && chance(0.03f + a.crowd * 0.002f)) { Ctx c{0.4f, 1, 1, 1, B_AMB, 0.2f}; PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Puff(c, 900, 500, 0.35f, 0.05f); v.fa0 = 600; v.fa1 = 400; v.fq = 3; }
    if (!a.over && chance(0.02f)) { Ctx c{0.35f, 1, 1, 1, B_AMB, 0.2f}; PanGains(0.3f, c.gl, c.gr); Voice& v = Tone(c, W_SINE, 2093, 0, 0.6f, 0.03f); v.f1 = v.f0; v.decPow = 1.8f; Tone(c, W_SINE, 2637, 0, 0.6f, 0.02f, 0.08f).decPow = 1.8f; }   // the till
    // the dog in the alley (or inside, once it's yours)
    if (!a.over && chance(a.dogInside ? 0.04f : 0.01f)) { Ctx c{a.dogInside ? 0.5f : 0.2f, 1, 1, 1, B_AMB, 0.3f}; PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 2; k++) { Voice& v = Vox(c, 260, 200, 0.12f, 0.06f, 900, 700, 1600, 1300, k * 0.2f); v.noiseMix = 0.25f; } }
    // the wake: the organ, slow chords under the quiet
    if (a.wake && !a.over && chance(0.18f)) NoCueImpl(NOC_ORGAN, 0.9f, -0.2f, 1);
    // the morning: birds
    if (a.over && chance(1.2f)) { Ctx c{RR(0.2f, 0.5f), 1, 1, 1, B_AMB, 0.4f}; PanGains(RR(-1, 1), c.gl, c.gr); int n = 2 + (int)(R01() * 4); float f = RR(2800, 4200); for (int k = 0; k < n; k++) { Voice& v = Tone(c, W_SINE, f * RR(0.95f, 1.15f), f * RR(1.1f, 1.4f), 0.07f, 0.03f, k * 0.11f); v.decPow = 1.5f; } }
    // the ghost piano: when the jukebox is quiet, a few notes from the stage by themselves
    if (!a.over && a.song < 0 && chance(0.6f)) { const NoSong& s = NoSongs()[5]; PlayInst(I_PLUCK, NoNote(s, (int)(R01() * 9) - 2, 0), 1.6f, 0.014f, -0.3f, 0.3f); }
}
void NoUpdate(float blockT) {
    const NoAudio& a = gNo.want;
    gNo.s += ((a.on && !a.blackout ? 1.0f : 0.0f) - gNo.s) * std::min(1.0f, blockT * (a.blackout ? 3.0f : 0.8f));
    if (gNo.s < 0.002f && !a.on) return;
    if (!gNoBedsReady) NoSetupBeds();
    gNo.songS += (((a.song >= 0 || a.over) ? 1.0f : 0.0f) - gNo.songS) * std::min(1.0f, blockT * 1.5f);
    gNo.ringS += ((a.drunk > 60 ? (a.drunk - 60) / 40 : 0.0f) - gNo.ringS) * std::min(1.0f, blockT);
    const auto& S = NoSongs();
    int si = a.over ? 6 : a.song;
    float bpm = si >= 0 && si < (int)S.size() ? S[si].bpm : 100;
    gNo.tickT += blockT; float td = 30.0f / bpm;
    while (gNo.tickT >= td) { gNo.tickT -= td; NoTick(); gNo.tick++; }
    // the beds: the murmur follows the crowd (thinning after 2 a.m.); drink blurs its top; rain; the ringing
    if (gNoBeds.size() >= 6) {
        float crowd = a.over ? 0 : std::clamp(a.crowd / 40.0f, 0.0f, 1.2f) * (a.hour >= 26 ? 0.45f : 1.0f) * (a.cartel ? 0.4f : 1.0f) * (a.wake ? 0.5f : 1.0f);
        float blur = std::clamp((a.drunk - 40) / 60, 0.0f, 1.0f);
        float s = gNo.s * (a.outside ? 0.35f : 1.0f);
        gNoBeds[0].gain = 0.05f * crowd * s * (1 + 0.3f * blur);
        gNoBeds[1].gain = 0.04f * crowd * s * (1 - 0.4f * blur);
        gNoBeds[2].gain = 0.018f * crowd * s * (1 - 0.8f * blur);
        gNoBeds[3].gain = (a.raining ? (a.outside ? 0.05f : 0.02f) : 0.0f) * gNo.s;
        gNoBeds[4].gain = 0.012f * gNo.s * (a.over ? 0 : 1);
        gNoBeds[5].gain = 0.004f * gNo.ringS * gNo.s;
    }
    gNo.ambT += blockT;
    if (gNo.ambT >= 0.05f) { NoEvents(gNo.ambT); gNo.ambT = 0; }
}
float NoBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gNoBeds) {
        if (b.gain <= 0) continue;
        float s;
        if (b.type == 3) { b.ph += b.f / SR; if (b.ph >= 1) b.ph -= 1; s = sinf(TAU * b.ph); }
        else { if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q); s = b.flt.Run(Noise()); }
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
// the effects
void NoCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.3f, 3.0f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f};
    PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case NOC_CLINK: { Voice& v = Tone(c, W_SINE, 3300 * p, 3280 * p, 0.35f, 0.07f); v.decPow = 2.5f; Tone(c, W_SINE, 4900 * p, 0, 0.2f, 0.03f, 0.01f).decPow = 3; break; }
        case NOC_GULP: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, 220 * p, 120 * p, 0.12f, 0.08f, k * 0.22f); v.curve = 0.6f; v.decPow = 2; } break; }
        case NOC_SCRAPE: { Voice& v = Puff(c, 1200, 600, 0.4f, 0.08f); v.fa0 = 700; v.fa1 = 450; v.fq = 4; v.decPow = 1.2f; break; }
        case NOC_TILL: { Tone(c, W_SINE, 2093, 0, 0.6f, 0.05f).decPow = 1.8f; Tone(c, W_SINE, 2637, 0, 0.6f, 0.04f, 0.08f).decPow = 1.8f; Thud(c, 300, 0.06f); break; }
        case NOC_PEN: { for (int k = 0; k < 5; k++) { Voice& v = Puff(c, 5000, 4000, 0.03f, 0.03f, k * 0.05f); v.hp = 3000; } break; }
        case NOC_STEP: { Thud(c, 90 * p, 0.12f); break; }
        case NOC_HICCUP: { Voice& v = Vox(c, 300, 420, 0.09f, 0.12f, 500, 700, 1500, 1700); v.decPow = 2; break; }
        case NOC_STUMBLE: { Thud(c, 70, 0.18f); Thud(c, 110, 0.12f, 0.15f); Voice& v = Puff(c, 900, 400, 0.3f, 0.05f, 0.05f); v.decPow = 1.6f; break; }
        case NOC_GLASS_DROP: { Thud(c, 400, 0.05f); for (int k = 0; k < 7; k++) { Voice& v = Tone(c, W_SINE, RR(3000, 6500), 0, RR(0.1f, 0.3f), 0.03f, 0.02f + k * 0.015f); v.decPow = 2.5f; } break; }
        case NOC_TAP: { Voice& v = Puff(c, 2400, 2000, 1.4f, 0.07f); v.fa0 = 2000; v.fa1 = 2200; v.fq = 1.5f; v.atk = 0.1f; v.decPow = 0.8f; break; }
        case NOC_DART: { Thud(c, 260, 0.2f); Voice& v = Puff(c, 3000, 900, 0.05f, 0.08f); v.decPow = 3; break; }
        case NOC_CHEER: { for (int k = 0; k < 7; k++) NoVoiceImpl(RR(0.8f, 1.6f), 1.4f, 2, 0.4f * vol, RR(-0.6f, 0.6f), true); Voice& v = Puff(c, 2000, 1500, 1.4f, 0.06f); v.fa0 = 1100; v.fa1 = 900; v.fq = 1.2f; v.atk = 0.15f; v.decPow = 1.2f; break; }
        case NOC_CHALK: { for (int k = 0; k < 3; k++) { Voice& v = Puff(c, 6000, 5000, 0.06f, 0.04f, k * 0.07f); v.hp = 3500; } break; }
        case NOC_POOL_CLICK: { Voice& v = Tone(c, W_SINE, 2600 * p, 2400 * p, 0.04f, 0.18f); v.decPow = 3; Voice& w = Puff(c, 7000, 5000, 0.015f, 0.12f); w.hp = 3000; break; }
        case NOC_POCKET: { Thud(c, 180, 0.2f); Thud(c, 120, 0.12f, 0.08f); Voice& v = Tone(c, W_SINE, 600, 300, 0.25f, 0.04f, 0.1f); v.decPow = 2; break; }
        case NOC_WINDMILL: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SAW, 420, 380, 0.5f, 0.03f, k * 0.6f); v.fa0 = 900; v.fa1 = 700; v.fq = 6; v.vibR = 7; v.vibD = 0.04f; v.decPow = 1.2f; } break; }
        case NOC_SPLASH: { Voice& v = Puff(c, 3500, 900, 0.6f, 0.14f); v.decPow = 1.6f; for (int k = 0; k < 6; k++) Bubble(c, 0.1f + k * 0.05f, RR(0.6f, 1.3f)); break; }
        case NOC_REELS: { for (int k = 0; k < 18; k++) { Voice& v = Tone(c, W_SQR, 1200 + 40 * (k % 3), 0, 0.02f, 0.03f, k * 0.05f); v.cut0 = v.cut1 = 3000; v.decPow = 3; } break; }
        case NOC_JACKPOT: { for (int k = 0; k < 12; k++) { Voice& v = Tone(c, W_SINE, k % 2 ? 1568.0f : 2093.0f, 0, 0.3f, 0.06f, k * 0.12f); v.f1 = v.f0; v.decPow = 1.6f; v.send = 0.6f; } break; }
        case NOC_SCRATCH: { Voice& v = Puff(c, 5000, 4500, 0.35f, 0.06f); v.hp = 2500; v.tremR = 22; v.tremD = 0.8f; break; }
        case NOC_CARD: { Voice& v = Puff(c, 6000, 3000, 0.05f, 0.08f); v.hp = 1500; v.decPow = 2.5f; Voice& w = Tone(c, W_SINE, 1760 * p, 0, 0.8f, 0.02f, 0.05f); w.f1 = w.f0; w.decPow = 1.6f; w.send = 0.7f; break; }   // (the fortune teller's: a chime per card)
        case NOC_CHIPS: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SINE, RR(3500, 5200), 0, 0.06f, 0.05f, k * 0.03f); v.decPow = 3; } break; }
        case NOC_SHUFFLE: { for (int k = 0; k < 14; k++) { Voice& v = Puff(c, 5500, 3500, 0.03f, 0.05f, k * 0.035f); v.hp = 2000; v.decPow = 2.4f; } break; }
        case NOC_SLAP: { Voice& v = Puff(c, 3500, 900, 0.12f, 0.2f); v.decPow = 2.2f; Thud(c, 200, 0.12f); break; }
        case NOC_PUNCH: { Thud(c, 75 * p, 0.3f); Voice& v = Puff(c, 1800, 500, 0.08f, 0.12f); v.decPow = 2.4f; break; }
        case NOC_WHISTLE_SLIDE: { Voice& v = Tone(c, W_SINE, 1800, 500, 0.45f, 0.06f); v.vibR = 9; v.vibD = 0.02f; v.decPow = 1.1f; break; }   // (the slide whistle at Wrecked)
        case NOC_SMASH: { Thud(c, 300, 0.12f); for (int k = 0; k < 12; k++) { Voice& v = Tone(c, W_SINE, RR(2500, 7500), 0, RR(0.1f, 0.4f), 0.035f, k * 0.012f); v.decPow = 2.4f; } Voice& w = Puff(c, 6000, 2000, 0.25f, 0.1f); w.hp = 1500; w.decPow = 2; break; }
        case NOC_CUE_CRACK: { Voice& v = Puff(c, 4000, 1500, 0.08f, 0.25f); v.decPow = 3; Thud(c, 160, 0.14f); break; }
        case NOC_CRASH: { Thud(c, 60, 0.35f); Thud(c, 95, 0.25f, 0.06f); for (int k = 0; k < 6; k++) { Voice& v = Puff(c, RR(800, 2400), 300, 0.15f, 0.08f, k * 0.04f); v.decPow = 2; } break; }   // a chair through a table
        case NOC_WINDOW: { Thud(c, 140, 0.2f); for (int k = 0; k < 20; k++) { Voice& v = Tone(c, W_SINE, RR(2000, 8000), 0, RR(0.2f, 0.7f), 0.03f, k * 0.02f); v.decPow = 2.2f; v.send = 0.5f; } break; }
        case NOC_SHOTGUN: { Thud(c, 40, 0.6f); Voice& v = Puff(c, 5000, 600, 0.9f, 0.3f); v.decPow = 1.3f; v.send = 1.0f; break; }   // (and the silence after it: the scene drops the crowd)
        case NOC_PARTY_CHEER: { for (int k = 0; k < 10; k++) NoVoiceImpl(RR(0.7f, 1.2f), 1.6f, 3, 0.45f * vol, RR(-0.8f, 0.8f), k % 3 == 0); break; }
        case NOC_WHISTLES: { for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_SINE, 1800, 2600, 0.35f, 0.04f, k * 0.25f); v.curve = 0.5f; v.decPow = 1.3f; } break; }
        case NOC_ENGINES: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SAW, 48 + 6 * k, 70 + 8 * k, 2.5f, 0.06f, k * 0.3f); v.cut0 = 400; v.cut1 = 700; v.tremR = 18 + 4 * k; v.tremD = 0.6f; v.atk = 0.6f; v.decPow = 1.1f; } break; }
        case NOC_KNOCK: { for (int k = 0; k < 3; k++) Thud(c, 150, 0.25f, k * 0.22f); break; }
        case NOC_POLICE_WHISTLE: { Voice& v = Tone(c, W_SINE, 2900, 2900, 0.8f, 0.07f); v.vibR = 30; v.vibD = 0.04f; v.atk = 0.02f; v.decPow = 0.9f; break; }
        case NOC_KITCHEN_DOOR: { Thud(c, 80, 0.3f); Voice& v = Tone(c, W_SAW, 300, 250, 0.4f, 0.03f); v.fa0 = 700; v.fa1 = 500; v.fq = 5; NoVoiceImpl(0.9f, 1.8f, 4, 0.6f, pan, false); break; }   // and a shout
        case NOC_BOLT: { Thud(c, 220, 0.2f); Voice& v = Puff(c, 3000, 1200, 0.25f, 0.1f, 0.05f); v.fa0 = 1800; v.fa1 = 900; v.fq = 4; v.decPow = 1.5f; break; }
        case NOC_ORGAN: { Ctx u = c; u.bus = B_MUSIC; const int F[4] = {0, 3, 7, 12}; for (int k = 0; k < 4; k++) { Voice& v = Tone(u, W_TRI, 98 * powf(2.0f, F[k] / 12.0f), 0, 4, 0.025f); v.f1 = v.f0; v.atk = 0.8f; v.decPow = 1.2f; v.send = 0.8f; } break; }
        case NOC_TUNING: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SAW, 440 * RR(0.97f, 1.03f), 440, 0.5f, 0.02f, k * 0.3f); v.cut0 = v.cut1 = 2000; v.decPow = 1.4f; } break; }
        case NOC_GOAT: { Voice& v = Vox(c, 420, 380, 0.7f, 0.08f, 800, 750, 1500, 1400); v.tremR = 11; v.tremD = 0.8f; v.decPow = 1.1f; break; }
        case NOC_DOG: { for (int k = 0; k < 2; k++) { Voice& v = Vox(c, 260, 200, 0.12f, 0.09f, 900, 700, 1600, 1300, k * 0.2f); v.noiseMix = 0.25f; } break; }
        case NOC_DOOR: { Voice& v = Tone(c, W_SAW, 180, 140, 0.6f, 0.03f); v.fa0 = 600; v.fa1 = 500; v.fq = 7; v.vibR = 5; v.vibD = 0.05f; Thud(c, 90, 0.15f, 0.55f); break; }
        case NOC_BIRDS: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SINE, RR(2800, 4000), RR(3500, 5000), 0.07f, 0.04f, k * 0.1f); v.decPow = 1.5f; } break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
