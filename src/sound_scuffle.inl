// ================================================================ Scuffle (the Deep Arcade's stick fight, stage 9)
// Included inside sound.cpp's anonymous namespace after sound_mouthful.inl. Design doc pp. 21-22, "Sound design":
// Scuffle's sound is percussion: every impact is a hit, every stick has a yelp (pitched per player colour), the stage
// has a rhythm. The music is a fast brass-and-drum loop per world that drops out while the wall closes in and comes
// back as a riff on a win; Boss Arena has the boss's own ostinato; the Gauntlet a ticking clock under the loop.
struct SfState {
    SfAudio want;
    float s = 0, bandS = 1, wallS = 0, bossS = 0, darkS = 0;
    int tick = 0; float tickT = 0, bpm = 150, ambT = 0;
    int lastOver = 0, lastCount = -1;
};
SfState gSf;
std::vector<Bed> gSfBeds; int gSfBedWorld = -1;
// each world's key and its colour (root, mode, tempo)
struct SfPal { float root; int scale[7]; float bpm; };
const SfPal SF_PAL[6] = {
    {110.0f, {0, 2, 4, 5, 7, 9, 10}, 152},   // the Nautilus: mixolydian, a ship's band
    {98.0f, {0, 2, 3, 5, 7, 8, 10}, 140},    // the Cave: minor, echoing
    {130.8f, {0, 2, 4, 7, 9, 12, 14}, 160},  // the Reef: bright pentatonic
    {92.5f, {0, 1, 4, 5, 7, 8, 10}, 136},    // Atlantis: an old mode
    {82.4f, {0, 1, 3, 5, 6, 8, 10}, 128},    // the Void: locrian, sparse
    {116.5f, {0, 2, 4, 5, 7, 9, 11}, 168},   // the Salon: a saloon stomp
};
float SfNote(int deg, int oct) { const SfPal& p = SF_PAL[std::clamp(gSf.want.world, 0, 5)]; int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return p.root * powf(2.0f, oct + o + p.scale[d] / 12.0f); }
// a brass stab: a saw through a formant, a quick swell
void SfBrass(float f, float dur, float gain, float pan, float delay = 0) {
    Ctx u{1, 1, 1, 1, B_MUSIC, 0.4f}; PanGains(pan, u.gl, u.gr);
    Voice& v = Tone(u, W_SAW, f, 0, dur, gain, delay); v.f1 = f; v.fa0 = 900 + f * 2; v.fa1 = 1300 + f * 2; v.fq = 2.2f; v.atk = 0.02f; v.decPow = 1.1f; v.send = 0.35f;
}
// the riffs (degree per eighth, -99 a rest): the loop's hook, and the win's flourish. Composed here.
const int SF_RIFF[16] = {0, -99, 0, 2, -99, 4, -99, 2, 0, -99, 0, 4, 5, -99, 4, -99};
const int SF_WIN[8] = {0, 2, 4, 7, 4, 7, 9, 11};
void SfSetupBeds(int world) {
    gSfBeds.clear(); gSfBedWorld = world;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gSfBeds.push_back(b); };
    switch (world) {
        case 0: bed(3, 55, 1, 0.012f, 0, 0, 0.3f, 0.4f); bed(0, 300, 0.8f, 0.02f, 0.1f, 0.3f, 0.2f, 0.5f); break;        // the ship's hum, water on the hull
        case 1: bed(0, 260, 0.8f, 0.03f, 0.13f, 0.3f, 0.15f, 0.6f); break;                                                // the cave's wash
        case 2: bed(0, 520, 0.7f, 0.035f, 0.08f, 0.4f, 0.11f, 0.7f); bed(2, 4200, 1, 0.006f, 0, 0, 7, 0.8f); break;       // surge and crackle
        case 3: bed(0, 180, 0.8f, 0.03f, 0.07f, 0.3f, 0.09f, 0.5f); break;                                                // the drowned plaza
        case 4: bed(3, 31, 1, 0.016f, 0, 0, 0.05f, 0.7f); break;                                                           // the Void's silence, almost
        default: bed(1, 1800, 0.8f, 0.012f, 0.4f, 0.5f, 1.3f, 0.7f); break;                                                // the Salon's crowd murmur
    }
}
void SfEvents(float dt) {
    const SfAudio& a = gSf.want; auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.6f};
    if (a.world == 1 && chance(0.8f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(1400, 2600), 0, 0.12f, RR(0.012f, 0.03f)); v.f1 = v.f0 * 0.55f; v.decPow = 2.3f; v.send = 0.9f; }   // drips
    if (a.world == 0 && chance(0.12f)) BuildCue(FindCue("amb.creak"), RR(0.2f, 0.5f), RR(-0.8f, 0.8f));                    // the hull
    if (a.world == 5 && chance(0.15f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SQR, RR(400, 700), 0, 0.08f, 0.006f); v.cut0 = v.cut1 = 1600; v.decPow = 2; }   // glasses
    if (a.world == 4 && chance(0.02f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(36, 48), 0, 3.0f, 0.02f); v.cut0 = 160; v.cut1 = 90; v.atk = 0.7f; v.decPow = 1; v.send = 1; }   // the worm, far below
}
void SfTick() {
    const SfAudio& a = gSf.want;
    int t = gSf.tick, e = t % 8, bar = t / 8;
    float td = 30.0f / gSf.bpm;
    float band = gSf.bandS * (1 - gSf.bossS * 0.6f) * (a.replay ? 0.5f : 1.0f);
    static const int CH[4] = {0, 3, 4, 0};
    int ch = CH[bar % 4];
    if (band > 0.02f) {
        // the drums: kick on the beat, snare on two and four, hats on the eighths (the Void keeps only the kick)
        if (e % 2 == 0) PlayInst(I_KICK, 0, 0.25f, 0.06f * band, 0, 0.2f);
        if ((e == 2 || e == 6) && a.world != 4) PlayInst(I_SNARE, 0, 0.18f, 0.035f * band, 0.1f, 0.4f);
        if (a.world != 4) PlayInst(I_HAT, 0, 0.05f, (e % 2 ? 0.012f : 0.02f) * band, 0.3f, 0.3f);
        // the bass walks the chord; the brass plays the riff, a stab on each chord change
        if (e % 2 == 0) PlayInst(I_SAW_BASS, SfNote(ch + (e == 4 ? 4 : 0), -1), td * 1.6f, 0.04f * band, -0.1f, 0.3f);
        int rd = SF_RIFF[t % 16];
        if (rd != -99 && (bar / 4) % 2 == 0) SfBrass(SfNote(ch + rd, 1), td * 0.9f, 0.022f * band, 0.2f);
        if (e == 0) { SfBrass(SfNote(ch, 0), td * 1.5f, 0.018f * band, -0.3f); SfBrass(SfNote(ch + 2, 0), td * 1.5f, 0.015f * band, 0.3f); }
    }
    // the wall: the band thins as it closes in; a rising tone under everything
    if (gSf.wallS > 0.05f && e == 0) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.5f}; Voice& v = Tone(u, W_SAW, SfNote(0, -2) * (1 + gSf.wallS * 0.5f), SfNote(0, -2) * (1.1f + gSf.wallS * 0.5f), td * 8, 0.03f * gSf.wallS); v.cut0 = 300; v.cut1 = 900; v.decPow = 0.8f; }
    // the boss: its ostinato (hashed from which boss), heavier in its last phase
    if (gSf.bossS > 0.05f) {
        static const int O[6][4] = {{0, 0, 1, -1}, {0, 3, 0, -2}, {0, 1, 0, 6}, {0, 4, 2, 4}, {0, -1, -2, -1}, {0, 0, 4, 3}};
        int b = std::clamp(a.boss, 0, 5);
        if (e % 2 == 0) PlayInst(I_SAW_BASS, SfNote(O[b][(e / 2) % 4], -2), td * 1.6f, 0.06f * gSf.bossS, 0, 0.3f);
        if (e == 0 || (a.bossPhase >= 2 && e == 4)) PlayInst(I_LOGDRUM, 0, 0.3f, 0.05f * gSf.bossS, 0, 0.3f);
        if (a.bossPhase >= 1 && e == 0 && bar % 2 == 0) PlayInst(I_CHOIR, SfNote(O[b][0], 0), td * 16, 0.02f * gSf.bossS, 0, 0.4f);
    }
    // the Gauntlet: a clock under it all
    if (a.gauntlet && e % 2 == 0) { Ctx u{1, 1, 1, 1, B_MUSIC, 0.1f}; Voice& v = Tone(u, W_SQR, e % 4 == 0 ? 2200.0f : 1800.0f, 0, 0.02f, 0.01f); v.cut0 = v.cut1 = 4000; v.decPow = 3; }
}
void SfUpdate(float blockT) {
    const SfAudio& a = gSf.want;
    gSf.s += ((a.on ? 1.0f : 0.0f) - gSf.s) * std::min(1.0f, blockT * 0.8f);
    if (gSf.s < 0.002f && !a.on) return;
    if (gSfBedWorld != a.world) SfSetupBeds(a.world);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gSf.bandS, a.over ? 0.0f : 1.0f - std::clamp(a.wall * 1.5f, 0.0f, 1.0f), 1.5f);
    toward(gSf.wallS, a.wall, 1.0f);
    toward(gSf.bossS, a.boss >= 0 ? 1.0f : 0.0f, 0.8f);
    float want = SF_PAL[std::clamp(a.world, 0, 5)].bpm + (a.boss >= 0 ? 6.0f * a.bossPhase : 0.0f);
    gSf.bpm += (want - gSf.bpm) * std::min(1.0f, blockT * 0.5f);
    gSf.tickT += blockT; float td = 30.0f / gSf.bpm;
    while (gSf.tickT >= td) { gSf.tickT -= td; SfTick(); gSf.tick++; }
    // the countdown: a tick a second
    if (a.count >= 0 && a.count != gSf.lastCount) { Ctx u{1, 1, 1, 1, B_UI, 0.2f}; Voice& v = Tone(u, W_SINE, a.count == 0 ? 1760.0f : 1320.0f, 0, 0.08f, 0.04f); v.decPow = 2; }
    gSf.lastCount = a.count;
    // the round's end: the riff comes back on a win; a falling chord on a loss; a fanfare for the match
    if (a.over != gSf.lastOver && a.over) {
        if (a.over == 2) { SfBrass(SfNote(0, 0), 1.2f, 0.03f, -0.2f); SfBrass(SfNote(0, 0) * 0.94f, 1.4f, 0.025f, 0.2f, 0.25f); }
        else for (int k = 0; k < 8; k++) SfBrass(SfNote(SF_WIN[k], 1), k == 7 ? 1.0f : 0.14f, a.over == 3 ? 0.04f : 0.03f, -0.4f + 0.1f * k, k * 0.11f);
        if (a.over == 3) { PlayInst(I_SUB_BASS, SfNote(0, -1), 2.5f, 0.05f, 0, 0.3f); Ctx u{1, 1, 1, 1, B_MUSIC, 0.6f}; Voice& v = Puff(u, 7000, 2500, 1.4f, 0.06f, 0.88f); v.hp = 2500; v.decPow = 1.1f; }
    }
    gSf.lastOver = a.over;
    for (auto& b : gSfBeds) b.gain = std::max(0.0f, b.gain);   // (the beds keep their levels; the bus scales them)
    gSf.ambT += blockT;
    if (gSf.ambT >= 0.05f) { SfEvents(gSf.ambT); gSf.ambT = 0; }
}
float SfBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gSfBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
// the effects: percussion first. pitch is the yelp's voice
void SfCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.25f};
    PanGains(pan, c.gl, c.gr);
    auto yelp = [&](float f, float dur, float g) { Voice& v = Tone(c, W_SAW, f * p, f * p * 0.7f, dur, g); v.fa0 = 900 * p; v.fa1 = 700 * p; v.fq = 3; v.vibR = 18; v.vibD = 0.04f; v.decPow = 1.6f; };
    switch (kind) {
        case SFC_PUNCH: { Thud(c, 160, 0.05f); Voice& v = Puff(c, 2200, 900, 0.04f, 0.05f); v.decPow = 3; break; }
        case SFC_HIT: { Thud(c, 120, 0.08f); Voice& v = Puff(c, 1800, 500, 0.06f, 0.06f); v.decPow = 2.5f; yelp(520, 0.14f, 0.035f); break; }   // a hit and the yelp
        case SFC_HAYMAKER: { Thud(c, 80, 0.16f); Voice& v = Puff(c, 1400, 300, 0.12f, 0.1f); v.decPow = 2; yelp(440, 0.22f, 0.04f); break; }
        case SFC_KICK: { Thud(c, 100, 0.12f); Whoosh(c, 600, 1800, 0.12f, 0.04f); break; }
        case SFC_LAND: { Thud(c, 70, 0.06f * std::min(1.0f, vol * 2)); break; }
        case SFC_DIE: { Voice& v = Tone(c, W_SAW, 700 * p, 260 * p, 0.6f, 0.05f); v.fa0 = 1100 * p; v.fa1 = 500 * p; v.fq = 3; v.vibR = 7; v.vibD = 0.08f; v.decPow = 1.2f; Thud(c, 60, 0.12f); break; }   // a scream that falls away
        case SFC_THROW: { Whoosh(c, 500, 2200, 0.2f, 0.05f); break; }
        case SFC_SHOT: { Voice& v = Puff(c, 3000, 600, 0.09f, 0.1f); v.decPow = 3; Thud(c, 220, 0.05f); break; }
        case SFC_SHOT_HEAVY: { Voice& v = Puff(c, 1600, 200, 0.28f, 0.14f); v.decPow = 2; Thud(c, 70, 0.14f); v.send = 0.5f; break; }
        case SFC_SCATTER: { for (int k = 0; k < 3; k++) { Voice& v = Puff(c, RR(1800, 3200), 400, 0.12f, 0.06f, k * 0.008f); v.decPow = 2.5f; } Thud(c, 110, 0.1f); break; }
        case SFC_LASER: { Voice& v = Tone(c, W_SQR, 1800, 400, 0.18f, 0.03f); v.cut0 = 6000; v.cut1 = 1500; v.decPow = 1.5f; break; }
        case SFC_BUBBLE: { for (int k = 0; k < 4; k++) Bubble(c, k * 0.04f, RR(0.6f, 1.4f)); Voice& v = Tone(c, W_SINE, 900, 1400, 0.2f, 0.02f, 0.05f); v.vibR = 20; v.vibD = 0.1f; break; }   // the giggle
        case SFC_HISS: { Voice& v = Puff(c, 5000, 4000, 0.5f, 0.04f); v.hp = 3000; v.decPow = 1.2f; break; }   // snakes
        case SFC_EXPLODE: { Thud(c, 45, 0.3f); Voice& v = Puff(c, 2400, 150, 0.9f, 0.2f); v.decPow = 1.5f; v.send = 0.7f; break; }
        case SFC_BLOCK: { Voice& v = Tone(c, W_FM, 1600, 1500, 0.3f, 0.05f); v.fmRatio = 2.4f; v.fmIndex = 3; v.decPow = 2; break; }   // a clang
        case SFC_CRATE: { Thud(c, 140, 0.1f); for (int k = 0; k < 3; k++) { Voice& v = Puff(c, RR(900, 1800), 400, 0.06f, 0.05f, k * 0.03f); v.decPow = 2.5f; } break; }   // splintering wood
        case SFC_PICKUP: { Voice& v = Tone(c, W_SQR, 660, 990, 0.08f, 0.02f); v.cut0 = v.cut1 = 3000; v.decPow = 2; break; }
        case SFC_EMPTY: { Voice& v = Tone(c, W_SQR, 2400, 2400, 0.03f, 0.02f); v.cut0 = v.cut1 = 5000; v.decPow = 3; break; }   // click
        case SFC_SWING: { Whoosh(c, 400, 1400, 0.16f, 0.05f); break; }
        case SFC_WALL: { Voice& v = Tone(c, W_SAW, 60, 120, 2.0f, 0.06f); v.cut0 = 300; v.cut1 = 900; v.atk = 0.6f; v.decPow = 0.9f; v.send = 0.8f; break; }
        case SFC_EVENT: { Thud(c, 90, 0.12f); Voice& v = Tone(c, W_SINE, 440, 440, 0.6f, 0.03f); v.decPow = 1.4f; Voice& w = Tone(c, W_SINE, 660, 660, 0.6f, 0.02f, 0.12f); w.decPow = 1.4f; break; }   // a tell
        case SFC_FREEZE: { Voice& v = Tone(c, W_SINE, 2600 * p, 3400 * p, 0.12f, 0.03f); v.decPow = 2; for (int k = 0; k < 3; k++) { Voice& w = Puff(c, 7000, 5000, 0.04f, 0.02f, k * 0.03f); w.hp = 4000; } break; }   // a squeak and frost
        case SFC_BURN: { Voice& v = Puff(c, 3000, 2000, 0.6f, 0.04f); v.hp = 1200; v.tremR = 20; v.tremD = 0.6f; v.decPow = 1.2f; break; }   // a sizzle
        case SFC_ZAP: { Voice& v = Tone(c, W_SAW, 120, 90, 0.25f, 0.05f); v.tremR = 60; v.tremD = 0.8f; v.cut0 = 4000; v.cut1 = 2000; v.decPow = 1.5f; break; }
        case SFC_SPLASH: { Voice& v = Puff(c, 1200, 400, 0.4f, 0.08f); v.decPow = 1.6f; for (int k = 0; k < 4; k++) Bubble(c, 0.05f + k * 0.05f, RR(0.5f, 1.2f)); break; }   // a gurgle
        case SFC_GRAB: { Thud(c, 200, 0.05f); yelp(700, 0.1f, 0.025f); break; }
        case SFC_ROAR: { Voice& v = Tone(c, W_SAW, 70, 50, 1.4f, 0.12f); v.cut0 = 700; v.cut1 = 250; v.atk = 0.15f; v.vibR = 6; v.vibD = 0.05f; v.decPow = 1.1f; v.send = 0.8f; Voice& w = Puff(c, 900, 300, 1.2f, 0.06f); w.atk = 0.2f; w.decPow = 1.2f; break; }
        case SFC_SLAM: { Thud(c, 40, 0.3f); Voice& v = Puff(c, 1000, 120, 0.7f, 0.14f); v.decPow = 1.4f; v.send = 0.6f; break; }
        case SFC_HAT: { Voice& v = Tone(c, W_SINE, 900, 1600, 0.08f, 0.03f); v.decPow = 2; break; }   // a pop: a hat's off
        case SFC_INK: { Voice& v = Puff(c, 600, 200, 0.4f, 0.1f); v.decPow = 1.5f; break; }
        case SFC_CONFETTI: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SINE, RR(1200, 3000), 0, 0.15f, 0.01f, k * 0.03f); v.decPow = 2; } Thud(c, 160, 0.06f); break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
