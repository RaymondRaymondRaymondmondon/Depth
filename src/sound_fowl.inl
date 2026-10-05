// ================================================================ Fowl Play (the Deep Arcade's duck shoot)
// Included inside sound.cpp's anonymous namespace after sound_scuffle.inl. Design doc p. 15, "Sound": plastic. Every
// gun has a toy click under its real sound (the Zapper's zap is a beep and a crack); ducks quack, geese honk, swans
// hiss, the phoenix shrieks, the UFO hums; the dog barks, sniffs and laughs. The hunt is a chiptune march that speeds
// up each round; the clubhouse a jaunty piano loop; the Slop Shop its own tacky jingle; a jackpot is a fanfare.
struct FpState {
    FpAudio want;
    float s = 0, marchS = 0, pianoS = 0, nightS = 0, pipeS = 0;
    int tick = 0; float tickT = 0, bpm = 120, ambT = 0;
    int lastPhase = -1;
};
FpState gFp;
std::vector<Bed> gFpBeds; int gFpBedKind = -1;
// C major for the march, F for the piano; degree -> frequency
float FpNote(int deg, int oct, float root = 261.6f) { static const int S[7] = {0, 2, 4, 5, 7, 9, 11}; int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return root * powf(2.0f, oct + o + S[d] / 12.0f); }
// a chip voice: a square through a gentle low-pass, quick decay
void FpChip(int wave, float f, float dur, float gain, float pan, float delay = 0) {
    Ctx u{1, 1, 1, 1, B_MUSIC, 0.15f}; PanGains(pan, u.gl, u.gr);
    Voice& v = Tone(u, wave, f, 0, dur, gain, delay); v.cut0 = v.cut1 = 5200; v.atk = 0.004f; v.decPow = 1.4f;
}
// the march: a lead over an oom-pah bass and a noise snare (composed here; two 8-bar strains)
const int FP_LEAD[32] = {4, -99, 4, 5, 7, -99, 7, -99, 5, 4, 2, -99, 0, -99, -99, -99, 2, -99, 2, 4, 5, -99, 4, 2, 4, -99, 2, -99, 0, -99, -99, -99};
const int FP_LEAD2[32] = {7, -99, 9, 7, 6, -99, 7, -99, 9, -99, 11, 9, 7, -99, -99, -99, 4, 5, 7, -99, 4, 5, 7, -99, 9, 7, 5, 4, 2, -99, -99, -99};
const int FP_CHORD[8] = {0, 0, 3, 4, 0, 5, 3, 4};
const int FP_PIANO[16] = {0, 2, 4, 2, 5, 4, 2, -99, 3, 5, 7, 5, 4, -99, 2, -99};
void FpSetupBeds(int kind) {
    gFpBeds.clear(); gFpBedKind = kind;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gFpBeds.push_back(b); };
    if (kind == 0) { bed(0, 700, 0.7f, 0.018f, 0.06f, 0.4f, 0.12f, 0.6f); bed(2, 5200, 1, 0.004f, 0, 0, 9, 0.9f); }   // the marsh: wind in the reeds, insects
    else if (kind == 1) { bed(0, 240, 0.8f, 0.012f, 0.1f, 0.3f, 0.2f, 0.5f); bed(1, 1500, 0.8f, 0.01f, 0.4f, 0.5f, 1.1f, 0.7f); }   // the clubhouse: the room, a murmur
    else { bed(2, 4800, 1, 0.008f, 0, 0, 12, 0.95f); bed(0, 400, 0.8f, 0.012f, 0.05f, 0.4f, 0.1f, 0.6f); }               // night: crickets, a breeze
}
void FpEvents(float dt) {
    const FpAudio& a = gFp.want; auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.5f};
    if (a.phase == 1 && chance(0.25f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SQR, RR(180, 260), 0, 0.07f, 0.01f, k * 0.11f); v.cut0 = v.cut1 = 900; v.decPow = 2; } }   // frogs
    if (a.phase == 1 && chance(0.06f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, 500, 380, 0.16f, 0.012f); v.fa0 = 1100; v.fa1 = 900; v.fq = 3; v.decPow = 1.5f; v.send = 0.7f; }   // a far quack
    if (a.phase == 3 && chance(0.1f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(900, 1300), 0, 0.06f, 0.008f); v.decPow = 2; }   // a slot's ding somewhere
}
void FpTick() {
    const FpAudio& a = gFp.want; int t = gFp.tick, e = t % 8, bar = t / 8; float td = 30.0f / gFp.bpm;
    // the march (the hunt; the bonus wave races)
    if (gFp.marchS > 0.02f) {
        float g = gFp.marchS * (1 - gFp.pipeS * 0.7f);
        int ch = FP_CHORD[bar % 8];
        if (e % 4 == 0) FpChip(W_TRI, FpNote(ch, -2), td * 1.8f, 0.05f * g, -0.1f);
        if (e % 4 == 2) { FpChip(W_SQR, FpNote(ch + 2, 0), td * 0.8f, 0.012f * g, 0.25f); FpChip(W_SQR, FpNote(ch + 4, 0), td * 0.8f, 0.01f * g, -0.25f); }
        if (e == 2 || e == 6) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.1f}; Voice& v = Puff(u, 6000, 2500, 0.06f, 0.025f * g); v.decPow = 2.5f; }   // the noise snare
        if (e % 2 == 0) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.0f}; Voice& v = Tone(u, W_SQR, 110, 55, 0.05f, 0.03f * g); v.cut0 = v.cut1 = 400; v.decPow = 3; }   // the chip kick
        const int* L = (bar / 8) % 2 ? FP_LEAD2 : FP_LEAD; int d = L[t % 32];
        if (d != -99) FpChip(W_SQR, FpNote(d, a.golden ? 1 : 0), td * 0.9f, 0.016f * g, 0.0f);
    }
    // the piano (the clubhouse): stride bass on the beat, a chord off it, the tune on top
    if (gFp.pianoS > 0.02f) {
        float g = gFp.pianoS; int ch = FP_CHORD[(bar / 2) % 8];
        if (e % 4 == 0) PlayInst(I_PLUCK, FpNote(ch, -2, 174.6f), td * 2, 0.05f * g, -0.2f, 0.4f);
        if (e % 4 == 2) for (int k = 0; k < 3; k++) PlayInst(I_PLUCK, FpNote(ch + k * 2, 0, 174.6f), td * 1.5f, 0.018f * g, 0.15f, 0.5f);
        int d = FP_PIANO[t % 16]; if (d != -99 && (bar / 4) % 2 == 0) PlayInst(I_PLUCK, FpNote(ch + d, 1, 174.6f), td * 1.2f, 0.024f * g, 0.3f, 0.6f);
        // the Slop Shop's tacky jingle while you stand at it
        if (a.slop && e % 2 == 0) PlayInst(I_BELL, FpNote((t * 3) % 7, 2, 174.6f), td, 0.008f * g, 0.5f, 0.6f);
    }
    // the bagpiper next to your stall: a drone and a chanter that drown the birds
    if (gFp.pipeS > 0.05f && e == 0) {
        Ctx u{1, 1, 1, 1, B_MUSIC, 0.2f}; Voice& v = Tone(u, W_SAW, 110, 110, td * 8, 0.035f * gFp.pipeS); v.fa0 = v.fa1 = 900; v.fq = 4; v.decPow = 0.5f;
        Voice& w = Tone(u, W_SAW, FpNote((bar * 3) % 7, 1, 233.1f), 0, td * 4, 0.02f * gFp.pipeS); w.fa0 = w.fa1 = 1400; w.fq = 5; w.vibR = 6; w.vibD = 0.02f;
    }
}
void FpUpdate(float blockT) {
    const FpAudio& a = gFp.want;
    gFp.s += ((a.on ? 1.0f : 0.0f) - gFp.s) * std::min(1.0f, blockT * 0.8f);
    if (gFp.s < 0.002f && !a.on) return;
    int bedKind = a.night ? 2 : a.phase == 3 ? 1 : 0; if (gFpBedKind != bedKind) FpSetupBeds(bedKind);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gFp.marchS, (a.phase == 1 || a.phase == 2) ? 1.0f : 0.0f, 1.2f);
    toward(gFp.pianoS, a.phase == 3 ? 1.0f : 0.0f, 1.2f);
    toward(gFp.pipeS, a.bagpipe ? 1.0f : 0.0f, 1.0f);
    float want = a.phase == 3 ? 112 : 112 + 5.0f * std::max(0, a.round - 1) + (a.phase == 2 ? 30 : 0);   // (speeds up each round)
    gFp.bpm += (want - gFp.bpm) * std::min(1.0f, blockT * 0.6f);
    gFp.tickT += blockT; float td = 30.0f / gFp.bpm;
    while (gFp.tickT >= td) { gFp.tickT -= td; FpTick(); gFp.tick++; }
    // the tally's jingle, the podium's fanfare
    if (a.phase != gFp.lastPhase) {
        if (a.phase == 4) for (int k = 0; k < 4; k++) FpChip(W_SQR, FpNote(k * 2, 1), 0.12f, 0.025f, 0, k * 0.1f);
        if (a.phase == 5) { for (int k = 0; k < 8; k++) FpChip(W_SQR, FpNote(FP_LEAD2[k * 2] == -99 ? 7 : FP_LEAD2[k * 2], 1), k == 7 ? 0.8f : 0.14f, 0.03f, -0.3f + 0.08f * k, k * 0.13f); PlayInst(I_SUB_BASS, FpNote(0, -2), 2.0f, 0.05f, 0, 0.3f); }
    }
    gFp.lastPhase = a.phase;
    gFp.ambT += blockT; if (gFp.ambT >= 0.05f) { FpEvents(gFp.ambT); gFp.ambT = 0; }
}
float FpBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gFpBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out * (1 - gFp.pipeS * 0.6f);
}
// the effects
void FpCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.2f}; PanGains(pan, c.gl, c.gr);
    auto click = [&]() { Voice& v = Tone(c, W_SQR, 3200, 3200, 0.012f, 0.02f); v.cut0 = v.cut1 = 7000; v.decPow = 3; };   // the toy click under every gun
    auto quack = [&](float f, float g) { for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SAW, f * p, f * p * 0.8f, 0.11f, g, k * 0.14f); v.fa0 = 1100 * p; v.fa1 = 800 * p; v.fq = 3.5f; v.decPow = 1.3f; } };
    switch (kind) {
        case FPC_ZAP: { click(); Voice& v = Tone(c, W_SQR, 1800 * p, 1500 * p, 0.05f, 0.025f); v.cut0 = v.cut1 = 6000; v.decPow = 2; Voice& w = Puff(c, 4000, 1200, 0.07f, 0.06f, 0.01f); w.decPow = 3; break; }   // a beep and a crack
        case FPC_BOOM: { click(); Thud(c, 90, 0.12f); Voice& v = Puff(c, 2200, 300, 0.22f, 0.12f); v.decPow = 2; break; }
        case FPC_RATTLE: { click(); Voice& v = Puff(c, 3500, 1500, 0.04f, 0.06f); v.decPow = 3; Thud(c, 160, 0.03f); break; }
        case FPC_CRACK: { click(); Voice& v = Puff(c, 5000, 900, 0.12f, 0.1f); v.decPow = 2.5f; Thud(c, 120, 0.05f); v.send = 0.5f; break; }
        case FPC_POP: { click(); Voice& v = Tone(c, W_SINE, 500 * p, 900 * p, 0.08f, 0.04f); v.decPow = 2; break; }
        case FPC_RAY: { Voice& v = Tone(c, W_SQR, 300, 1200, 0.5f, 0.03f); v.cut0 = 3000; v.cut1 = 1200; v.vibR = 30; v.vibD = 0.1f; v.decPow = 1.2f; break; }
        case FPC_QUACK: quack(480, 0.04f); break;
        case FPC_HONK: { Voice& v = Tone(c, W_SAW, 220 * p, 200 * p, 0.3f, 0.05f); v.fa0 = 700; v.fa1 = 650; v.fq = 4; v.decPow = 1; break; }
        case FPC_HISS: { Voice& v = Puff(c, 5000, 4000, 0.5f, 0.05f); v.hp = 2500; v.decPow = 1.2f; break; }
        case FPC_SHRIEK: { Voice& v = Tone(c, W_SAW, 1500, 2600, 0.4f, 0.04f); v.fa0 = 2400; v.fa1 = 3000; v.fq = 3; v.vibR = 14; v.vibD = 0.06f; v.decPow = 1.3f; break; }
        case FPC_HUM: { Voice& v = Tone(c, W_SINE, 140, 150, 1.2f, 0.05f); v.vibR = 4; v.vibD = 0.05f; v.atk = 0.2f; v.decPow = 1; Voice& w = Tone(c, W_SINE, 280, 300, 1.2f, 0.02f); w.vibR = 5; w.vibD = 0.08f; break; }
        case FPC_BARK: { for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SAW, 420, 260, 0.1f, 0.05f, k * 0.18f); v.fa0 = 900; v.fa1 = 700; v.fq = 2.5f; v.decPow = 1.6f; Thud(c, 140, 0.04f, k * 0.18f); } break; }
        case FPC_LAUGH: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SAW, 520 - k * 30, 400 - k * 30, 0.09f, 0.04f, k * 0.12f); v.fa0 = 1000; v.fa1 = 800; v.fq = 3; v.decPow = 1.4f; } break; }   // heh heh heh
        case FPC_SQUEAK: { Voice& v = Tone(c, W_SINE, 1800, 2600, 0.1f, 0.04f); v.decPow = 1.6f; break; }
        case FPC_PING: { Voice& v = Tone(c, W_FM, 2400, 2300, 0.25f, 0.04f); v.fmRatio = 2.8f; v.fmIndex = 2; v.decPow = 2; break; }
        case FPC_SPLASH: { Voice& v = Puff(c, 1400, 400, 0.3f, 0.05f); v.decPow = 1.6f; for (int k = 0; k < 3; k++) Bubble(c, 0.04f + k * 0.05f, RR(0.6f, 1.2f)); break; }
        case FPC_DING: { Voice& v = Tone(c, W_SINE, 1760, 1760, 0.5f, 0.04f); v.decPow = 1.6f; Voice& w = Tone(c, W_SINE, 2640, 2640, 0.5f, 0.02f, 0.06f); w.decPow = 1.6f; break; }
        case FPC_FANFARE: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SQR, FpNote(k == 5 ? 7 : k * 2 % 7, 1), 0, k == 5 ? 0.6f : 0.12f, 0.03f, k * 0.11f); v.cut0 = v.cut1 = 5000; v.decPow = 1.4f; } break; }
        case FPC_SCRATCH: { Voice& v = Puff(c, 3000, 2500, 0.5f, 0.08f); v.hp = 1500; v.tremR = 18; v.tremD = 0.7f; v.decPow = 1; break; }
        case FPC_CRANK: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SQR, 900, 900, 0.015f, 0.02f, k * 0.07f); v.cut0 = v.cut1 = 4000; v.decPow = 3; } Thud(c, 200, 0.05f, 0.45f); break; }
        case FPC_BELL: { for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_FM, 1320, 1320, 0.8f, 0.05f, k * 0.25f); v.fmRatio = 3.5f; v.fmIndex = 4; v.decPow = 1.5f; } break; }
        case FPC_BOO: { Voice& v = Puff(c, 600, 500, 0.8f, 0.04f); v.atk = 0.15f; v.decPow = 1; Voice& w = Tone(c, W_SAW, 140, 120, 0.8f, 0.02f); w.fa0 = w.fa1 = 500; w.fq = 2; w.atk = 0.2f; break; }   // the crowd boos
        case FPC_CHEER: { Voice& v = Puff(c, 2500, 1800, 1.0f, 0.05f); v.atk = 0.1f; v.decPow = 1; v.tremR = 9; v.tremD = 0.3f; break; }
        case FPC_RELOAD: { click(); Thud(c, 260, 0.03f, 0.08f); Voice& v = Tone(c, W_SQR, 700, 500, 0.05f, 0.015f, 0.16f); v.cut0 = v.cut1 = 3000; v.decPow = 2; break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
