// ================================================================ Ball Pit Brawl (the Deep Arcade's play-centre shooter)
// Included inside sound.cpp's anonymous namespace after sound_fowl.inl. Bright, plastic and silly: a bouncy synth-pop
// loop in the play centre's speakers (a calmer groove in the warm-up, the full band in play, faster and higher as the
// match nears its end, muffled to a thump when you're under the balls); the hall's bed is the big room, the kids far off,
// the blowers of the ball lift and the belts' rattle. Foam guns are a soft "pfft" and a toy click; balls boing; cannons
// whump; the joke shop toots, buzzes and squawks.
struct BpState {
    BpAudio want;
    float s = 0, bandS = 0, calmS = 0, under = 0;
    int tick = 0; float tickT = 0, bpm = 124, ambT = 0;
    int lastPhase = -1;
};
BpState gBp;
std::vector<Bed> gBpBeds; int gBpBedKind = -1;
float BpNote(int deg, int oct, float root = 293.7f) { static const int S[7] = {0, 2, 4, 5, 7, 9, 11}; int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return root * powf(2.0f, oct + o + S[d] / 12.0f); }
void BpSynth(int wave, float f, float dur, float gain, float pan, float cut, float delay = 0) {
    Ctx u{1, 1, 1, 1, B_MUSIC, 0.18f}; PanGains(pan, u.gl, u.gr);
    float c = cut * (1 - gBp.under * 0.85f);
    Voice& v = Tone(u, wave, f, 0, dur, gain, delay); v.cut0 = v.cut1 = std::max(250.0f, c); v.atk = 0.006f; v.decPow = 1.3f;
}
// I - V - vi - IV in D major, a hook that answers itself
const int BP_CHORD[4] = {0, 4, 5, 3};
const int BP_HOOK[32] = {4, -99, 4, 2, 4, -99, 7, -99, 5, -99, 4, 2, 0, -99, -99, -99, 4, -99, 4, 2, 4, -99, 9, -99, 7, 5, 4, -99, 2, -99, -99, -99};
const int BP_HOOK2[32] = {7, -99, 9, -99, 11, 9, 7, -99, 5, -99, 7, 5, 4, -99, 2, -99, 4, 5, 7, -99, 9, -99, 7, 5, 4, -99, 2, 4, 0, -99, -99, -99};
void BpSetupBeds(int kind) {
    gBpBeds.clear(); gBpBedKind = kind;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gBpBeds.push_back(b); };
    bed(1, 1700, 0.7f, 0.010f, 0.35f, 0.45f, 2.3f, 0.75f);   // the kids far off: a chattering band of noise that comes and goes
    bed(0, 160, 0.9f, 0.016f, 0.04f, 0.2f, 0.07f, 0.3f);     // the blowers of the ball lift and the hall's air
    bed(1, 520, 2.5f, 0.006f, 0, 0, 13, 0.6f);               // the belts' rattle
    if (kind == 1) bed(0, 380, 0.7f, 0.03f, 0.2f, 0.4f, 0.4f, 0.5f);   // under the balls: rustle and pressure
}
void BpEvents(float dt) {
    const BpAudio& a = gBp.want; auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.6f};
    if (chance(0.22f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(700, 1100), RR(1100, 1500), RR(0.18f, 0.4f), 0.006f); v.fa0 = 2200; v.fa1 = 2600; v.fq = 3; v.decPow = 1.2f; v.send = 0.8f; }   // a far shriek of glee
    if (chance(0.12f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, RR(1300, 2400), 0, 0.07f, 0.004f, k * 0.09f); v.decPow = 2; v.send = 0.7f; } }   // an arcade machine somewhere
    if (chance(0.3f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Puff(c, RR(1800, 3000), 900, 0.05f, 0.006f); v.decPow = 3; v.send = 0.5f; }   // a ball dropping down a gutter
    (void)a;
}
void BpTick() {
    const BpAudio& a = gBp.want; int t = gBp.tick, e = t % 8, bar = t / 8; float td = 30.0f / gBp.bpm;
    int ch = BP_CHORD[bar % 4]; float cut = 2600 + 2400 * a.intensity;
    // the warm-up groove: soft keys and a shaker
    if (gBp.calmS > 0.02f) {
        float g = gBp.calmS;
        if (e == 0) for (int k = 0; k < 3; k++) BpSynth(W_TRI, BpNote(ch + k * 2, 0), td * 7, 0.012f * g, -0.3f + 0.3f * k, 2200);
        if (e % 2 == 1) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.1f}; Voice& v = Puff(u, 7000, 5000, 0.04f, 0.008f * g); v.hp = 4000; v.decPow = 2.5f; }
        if (e == 0 || e == 3) BpSynth(W_SINE, BpNote(ch, -2), td * 2.5f, 0.05f * g, 0, 600);
    }
    // the band: four on the floor, a clap, a bouncing octave bass, stabs, the hook
    if (gBp.bandS > 0.02f) {
        float g = gBp.bandS;
        if (e % 2 == 0) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.0f}; Voice& v = Tone(u, W_SINE, 120, 48, 0.12f, 0.1f * g); v.decPow = 2.5f; }   // the kick
        if (e == 2 || e == 6) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.25f}; Voice& v = Puff(u, 2600 * (1 - gBp.under * 0.6f), 1500, 0.09f, 0.03f * g); v.decPow = 2.2f; }   // the clap
        if (e % 2 == 1) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.1f}; Voice& v = Puff(u, 9000, 7000, 0.03f, 0.006f * g * (1 - gBp.under)); v.hp = 6000; v.decPow = 3; }   // the hats
        BpSynth(W_SAW, BpNote(ch, e % 2 ? -1 : -2), td * 0.9f, 0.032f * g, 0, 700 + 500 * a.intensity);   // the bass
        if (e == 1 || e == 4 || e == 6) for (int k = 0; k < 3; k++) BpSynth(W_SQR, BpNote(ch + k * 2, 0), td * 0.5f, 0.007f * g, -0.4f + 0.4f * k, cut);   // the stabs
        const int* L = (bar / 4) % 2 ? BP_HOOK2 : BP_HOOK; int d = L[t % 32];
        if (d != -99 && (bar % 8 < 6 || a.intensity > 0.5f)) BpSynth(W_SQR, BpNote(d, a.intensity > 0.7f ? 2 : 1), td * 0.95f, 0.02f * g, 0.1f, cut + 800);
        if (a.intensity > 0.6f && e % 2 == 0) BpSynth(W_TRI, BpNote(ch + (t % 3) * 2, 2), td * 0.4f, 0.006f * g, -0.2f, 6000);   // the arpeggio when it's close
    }
}
void BpUpdate(float blockT) {
    const BpAudio& a = gBp.want;
    gBp.s += ((a.on ? 1.0f : 0.0f) - gBp.s) * std::min(1.0f, blockT * 0.8f);
    if (gBp.s < 0.002f && !a.on) return;
    int bedKind = a.submerged ? 1 : 0; if (gBpBedKind != bedKind) BpSetupBeds(bedKind);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gBp.bandS, a.phase == 1 ? 1.0f : 0.0f, 1.0f);
    toward(gBp.calmS, a.phase == 0 ? 1.0f : 0.0f, 1.0f);
    toward(gBp.under, a.submerged ? 1.0f : 0.0f, 4.0f);
    float want = 118 + 16 * a.intensity;
    gBp.bpm += (want - gBp.bpm) * std::min(1.0f, blockT * 0.4f);
    gBp.tickT += blockT; float td = 30.0f / gBp.bpm;
    while (gBp.tickT >= td) { gBp.tickT -= td; BpTick(); gBp.tick++; }
    if (a.phase != gBp.lastPhase && a.phase == 2) {   // the final whistle: a fanfare, or a sad trombone
        if (a.won) { for (int k = 0; k < 6; k++) BpSynth(W_SQR, BpNote(k == 5 ? 7 : k * 2 % 7, 1), k == 5 ? 0.9f : 0.13f, 0.03f, -0.3f + 0.1f * k, 5000, k * 0.12f); PlayInst(I_SUB_BASS, BpNote(0, -2), 2.0f, 0.05f, 0, 0.3f); }
        else for (int k = 0; k < 4; k++) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.3f}; Voice& v = Tone(u, W_SAW, BpNote(4 - k, 0) * 0.5f, BpNote(4 - k, 0) * 0.47f, k == 3 ? 1.0f : 0.4f, 0.03f, k * 0.42f); v.fa0 = v.fa1 = 700; v.fq = 2; v.vibR = k == 3 ? 6.0f : 0.0f; v.vibD = 0.02f; v.decPow = 1; }
    }
    gBp.lastPhase = a.phase;
    gBp.ambT += blockT; if (gBp.ambT >= 0.05f) { BpEvents(gBp.ambT); gBp.ambT = 0; }
}
float BpBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gBpBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
void BpCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.25f}; PanGains(pan, c.gl, c.gr);
    auto click = [&]() { Voice& v = Tone(c, W_SQR, 2600 * p, 2400 * p, 0.012f, 0.018f); v.cut0 = v.cut1 = 6000; v.decPow = 3; };
    auto boing = [&](float f, float g) { Voice& v = Tone(c, W_SINE, f * p, f * p * 1.9f, 0.18f, g); v.vibR = 22; v.vibD = 0.06f; v.decPow = 1.6f; };
    switch (kind) {
        case BPC_DART: { click(); Voice& v = Puff(c, 2200 * p, 700, 0.07f, 0.06f); v.decPow = 2.2f; Thud(c, 200 * p, 0.025f); break; }   // pfft
        case BPC_DART_BIG: { click(); Voice& v = Puff(c, 1500 * p, 400, 0.14f, 0.09f); v.decPow = 1.8f; Thud(c, 120, 0.05f); Voice& w = Tone(c, W_SINE, 500, 260, 0.1f, 0.02f); w.decPow = 2; break; }
        case BPC_HIT: { Voice& v = Puff(c, 1200, 600, 0.05f, 0.05f); v.decPow = 2.5f; Thud(c, 260 * p, 0.04f); break; }   // a foam thwack
        case BPC_KNIFE: { Voice& v = Puff(c, 3000, 1200, 0.16f, 0.05f); v.hp = 900; v.atk = 0.03f; v.decPow = 1.5f; Thud(c, 180, 0.03f, 0.12f); break; }   // a swish and a squish
        case BPC_THROW: { Voice& v = Puff(c, 1800, 3500, 0.18f, 0.04f); v.hp = 600; v.atk = 0.05f; v.decPow = 1.2f; break; }
        case BPC_BOUNCE: boing(300, 0.045f); break;
        case BPC_CANNON: { Thud(c, 85, 0.11f); Voice& v = Puff(c, 900, 300, 0.18f, 0.08f); v.decPow = 2; boing(180, 0.02f); break; }   // whump
        case BPC_OVERHEAT: { Voice& v = Puff(c, 5000, 3000, 1.0f, 0.05f); v.hp = 2500; v.decPow = 0.9f; Voice& w = Tone(c, W_SQR, 700, 500, 0.3f, 0.012f); w.cut0 = w.cut1 = 2400; w.tremR = 12; w.tremD = 0.8f; break; }
        case BPC_VACUUM: { Voice& v = Tone(c, W_SAW, 180, 210, 0.25f, 0.018f); v.fa0 = 1800; v.fa1 = 2200; v.fq = 2; v.decPow = 0.8f; Voice& w = Puff(c, 3000, 3500, 0.25f, 0.02f); w.hp = 2000; w.decPow = 0.8f; break; }
        case BPC_SLIDE: { Voice& v = Puff(c, 900, 2200, 1.0f, 0.05f); v.atk = 0.1f; v.decPow = 0.9f; Voice& w = Tone(c, W_SINE, 900, 1400, 0.6f, 0.012f); w.vibR = 9; w.vibD = 0.05f; w.decPow = 1.1f; break; }   // a whoosh and a squeak
        case BPC_KO: { Voice& v = Tone(c, W_SINE, 900 * p, 220 * p, 0.3f, 0.05f); v.decPow = 1.4f; Thud(c, 140, 0.05f); boing(420, 0.03f); for (int k = 0; k < 3; k++) { Voice& s = Tone(c, W_SINE, 1700 + k * 350, 0, 0.12f, 0.012f, 0.18f + k * 0.08f); s.decPow = 2; } break; }   // bonk, and the stars
        case BPC_STREAK: { for (int k = 0; k < 5; k++) BpSynth(W_SQR, BpNote(k * 2, 1), 0.1f, 0.03f, 0, 5000, k * 0.06f); break; }
        case BPC_BUY: { Voice& v = Tone(c, W_FM, 2100, 2100, 0.4f, 0.04f); v.fmRatio = 3.1f; v.fmIndex = 2.5f; v.decPow = 1.8f; Thud(c, 500, 0.02f); for (int k = 0; k < 4; k++) { Voice& w = Tone(c, W_SQR, 1200, 1200, 0.01f, 0.012f, 0.05f + k * 0.04f); w.cut0 = w.cut1 = 3000; w.decPow = 3; } break; }   // ka-ching
        case BPC_RELOAD: { click(); Thud(c, 320, 0.03f, 0.1f); Voice& v = Tone(c, W_SQR, 900, 700, 0.04f, 0.014f, 0.2f); v.cut0 = v.cut1 = 3500; v.decPow = 2; break; }
        case BPC_DRY: { click(); Voice& v = Tone(c, W_SQR, 1800, 1700, 0.02f, 0.05f, 0.05f); v.cut0 = v.cut1 = 5000; v.decPow = 3; break; }
        case BPC_HORN: { Voice& v = Tone(c, W_SAW, 600, 640, 0.7f, 0.045f); v.fa0 = 1500; v.fa1 = 1700; v.fq = 2.5f; v.vibR = 7; v.vibD = 0.01f; v.atk = 0.02f; v.decPow = 0.6f; Voice& w = Puff(c, 2500, 2500, 0.08f, 0.02f); w.decPow = 2; break; }   // the party horn
        case BPC_KAZOO: { static const int M[8] = {0, 2, 4, 2, 0, 4, 7, 7}; for (int k = 0; k < 8; k++) { Voice& v = Tone(c, W_SAW, BpNote(M[k], 0), 0, k == 7 ? 0.6f : 0.3f, 0.03f, k * 0.33f); v.fa0 = v.fa1 = 1300; v.fq = 4; v.vibR = 5.5f; v.vibD = 0.025f; v.tremR = 70; v.tremD = 0.4f; v.decPow = 0.7f; } break; }
        case BPC_SLURP: { Voice& v = Puff(c, 1200, 3200, 1.6f, 0.04f); v.tremR = 16; v.tremD = 0.8f; v.atk = 0.1f; v.decPow = 0.8f; Bubble(c, 1.5f, 0.6f); break; }   // a very long slurp
        case BPC_POP: { Voice& v = Puff(c, 3200, 900, 0.09f, 0.09f); v.decPow = 2.5f; for (int k = 0; k < 6; k++) { Voice& s = Tone(c, W_SINE, RR(2500, 4500), 0, 0.08f, 0.006f, 0.05f + k * 0.05f); s.decPow = 2; } break; }   // the confetti popper
        case BPC_FART: { Voice& v = Tone(c, W_SAW, 95, 70, 0.7f, 0.06f); v.fa0 = 500; v.fa1 = 300; v.fq = 3; v.tremR = 32; v.tremD = 0.7f; v.vibR = 9; v.vibD = 0.1f; v.decPow = 0.9f; break; }   // the whoopee cushion
        case BPC_SQUAWK: { Voice& v = Tone(c, W_SAW, 900 * p, 600 * p, 0.35f, 0.04f); v.fa0 = 1800; v.fa1 = 1300; v.fq = 4; v.vibR = 18; v.vibD = 0.08f; v.decPow = 1.2f; break; }   // the rubber chicken
        case BPC_KIDS: { for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_SAW, RR(900, 1400), RR(1400, 1900), RR(0.3f, 0.6f), 0.02f, k * RR(0.05f, 0.2f)); v.fa0 = 2600; v.fa1 = 3000; v.fq = 3; v.vibR = RR(6, 12); v.vibD = 0.05f; v.decPow = 1.1f; } break; }   // shrieking kids
        case BPC_BOOM: { Thud(c, 60, 0.14f); Voice& v = Puff(c, 1800, 200, 0.9f, 0.1f); v.decPow = 1.4f; boing(140, 0.03f); for (int k = 0; k < 6; k++) { Voice& s = Tone(c, W_SINE, RR(2000, 4000), 0, 0.1f, 0.008f, 0.2f + k * 0.06f); s.decPow = 2; } break; }   // a foam bomb: poof, and confetti
        case BPC_BEEP: { Voice& v = Tone(c, W_SQR, 1500, 1500, 0.07f, 0.02f); v.cut0 = v.cut1 = 4000; v.decPow = 2; break; }
        case BPC_FLAG: { for (int k = 0; k < 3; k++) BpSynth(W_SQR, BpNote(k * 2 + 4, 1), 0.14f, 0.03f, 0, 5000, k * 0.1f); break; }
        case BPC_CAPTURE: { for (int k = 0; k < 6; k++) BpSynth(W_SQR, BpNote(k == 5 ? 7 : k * 2 % 7, 1), k == 5 ? 0.7f : 0.12f, 0.03f, -0.3f + 0.1f * k, 5000, k * 0.1f); break; }
        case BPC_GRAB: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SINE, RR(500, 900), RR(400, 700), 0.05f, 0.04f, k * 0.04f); v.decPow = 2.2f; } break; }   // reaching into the balls: plastic rattle
        case BPC_PIT: { for (int k = 0; k < 10; k++) { Voice& v = Tone(c, W_SINE, RR(400, 900), RR(300, 700), 0.05f, 0.015f, k * 0.03f); v.decPow = 2.5f; } Thud(c, 160, 0.04f); break; }   // landing in a pit
        case BPC_ROCKET: { Voice& v = Puff(c, 900, 2400, 0.6f, 0.05f); v.atk = 0.05f; v.decPow = 1; Voice& w = Tone(c, W_SAW, 200, 400, 0.6f, 0.012f); w.fa0 = w.fa1 = 800; w.fq = 2; w.decPow = 1; break; }
        case BPC_SPLASH: { Thud(c, 110, 0.09f); Voice& v = Puff(c, 2000, 500, 0.3f, 0.08f); v.decPow = 1.8f; break; }
        case BPC_STEP: { Thud(c, 140 * p, 0.03f); Voice& v = Puff(c, 1200, 800, 0.03f, 0.012f); v.decPow = 3; break; }
        case BPC_CLIMB: { Voice& v = Puff(c, 900, 600, 0.12f, 0.02f); v.decPow = 2; Thud(c, 220, 0.015f); break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
