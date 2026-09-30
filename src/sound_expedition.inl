// ================================================================ expeditions (Master Reference stage 8)
// Included inside sound.cpp's anonymous namespace (it uses the voice pool and instruments there).
// Walking: a drone per location, a slow pulse that quickens as the light fails, and a low tone that rises as the
// party nears the boss room. Combat: the location's rhythm, a melodic layer while the party is winning, a dissonant
// layer when a hero is below 30% HP or above 80 nerves, a heartbeat at Death's Door, and a theme per boss with a
// second-phase variation. Stingers on crits and kills. Each location has its own ambience bed and events.
void BuildCue(int idx, float vol, float pan);
int FindCue(const char* name);

struct ExpPal { float root; int scale[7]; int n; float bpmWalk, bpmFight; int room; };
const ExpPal EXP_PAL[6] = {
    {65.41f, {0, 1, 3, 5, 7, 8, 10}, 7, 54, 92, RR_CAVE},      // the Cave: glass harmonica, drips
    {98.00f, {0, 2, 4, 7, 9}, 5, 60, 100, RR_OPENSEA},         // the Island: log drums, conch, chant
    {87.31f, {0, 2, 4, 6, 7, 9, 11}, 7, 56, 90, RR_KELP},      // the Weeds: bowed strings, whale calls
    {58.27f, {0, 2, 3, 5, 7, 8, 11}, 7, 50, 84, RR_HALL},      // Atlantis: choir, sub-bass organ, ticking
    {41.20f, {0, 1, 3, 5, 6, 8, 10}, 7, 44, 76, RR_CAVE},      // the Trench: pressure, groans, near silence
    {36.71f, {0, 1, 3, 5, 6, 8, 10}, 7, 40, 72, RR_HALL},      // the Hadal
};
struct ExpState {
    ExpAudio want;
    float s = 0, walkS = 1, fightS = 0, winS = 0, dangerS = 0, doorS = 0, phaseS = 0;
    int tick = 0; float tickT = 0, pulseT = 0, heartT = 0, ambT = 0, bpm = 54;
    std::vector<std::pair<int, int>> motif; int motifAt = -1, motifNext = 0;
};
ExpState gExp;
float gBeat = 0, gTorchPh = 0;   // the music's beat, for the Cave's mould glow; the torch's hum
std::vector<Bed> gExpBeds;
int gExpBedLoc = -1;

float ExpNote(int loc, int deg, int oct) {
    const ExpPal& p = EXP_PAL[std::clamp(loc, 0, 5)];
    int n = p.n, o = deg >= 0 ? deg / n : -((-deg + n - 1) / n), d = deg - o * n;
    return p.root * powf(2.0f, oct + o + p.scale[d] / 12.0f);
}
// signature voices of the locations
void Drip(float f, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.8f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SINE, f * 1.6f, f, 0.22f, gain); v.curve = 0.4f; v.decPow = 2.2f; v.send = 0.9f;
}
void GlassHarmonica(float f, float dur, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.8f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SINE, f, f, dur, gain); v.atk = dur * 0.25f; v.hold = dur * 0.3f; v.decPow = 1.2f; v.tremR = 4.5f; v.tremD = 0.25f; v.send = 0.8f;
    Voice& h = Tone(c, W_SINE, f * 2.01f, f * 2.01f, dur * 0.8f, gain * 0.25f); h.atk = dur * 0.3f; h.decPow = 1.4f; h.send = 0.8f;
}
void Conch(float f, float dur, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.6f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SAW, f * 0.94f, f, dur, gain); v.curve = 0.25f; v.atk = 0.25f; v.hold = dur * 0.4f; v.decPow = 1.3f;
    v.fa0 = v.fa1 = 520; v.fb0 = v.fb1 = 900; v.fq = 4; v.noiseMix = 0.12f; v.vibR = 4; v.vibD = 0.01f; v.send = 0.7f;
}
void Bowed(float f, float dur, float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.6f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SAW, f, f, dur, gain); v.det = 0.004f; v.atk = dur * 0.35f; v.hold = dur * 0.3f; v.decPow = 1;
    v.fa0 = v.fa1 = 800; v.fb0 = v.fb1 = 2000; v.fq = 2.5f; v.vibR = 5; v.vibD = 0.008f; v.send = 0.6f;
}
void Groan(float f, float gain, float pan) { // metal under pressure
    Ctx c{1, 1, 1, 1, B_AMB, 0.8f}; PanGains(pan, c.gl, c.gr);
    Voice& v = Tone(c, W_SAW, f, f * RR(0.8f, 1.1f), RR(1.2f, 2.4f), gain); v.fa0 = RR(180, 300); v.fa1 = v.fa0 * 0.8f; v.fq = 8; v.vibR = RR(3, 7); v.vibD = 0.04f; v.atk = 0.4f; v.send = 0.8f;
}
void ClockTick(float gain, float pan) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f}; PanGains(pan, c.gl, c.gr);
    Voice& w = Puff(c, 6000, 3500, 0.012f, gain); w.hp = 2000;
    Voice& v = Tone(c, W_SINE, 1800, 1500, 0.04f, gain * 0.3f); v.decPow = 3;
}
void Heartbeat(float gain) {
    Ctx c{1, 1, 1, 1, B_MUSIC, 0.15f};
    for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_SINE, 70, 42, 0.18f, gain * (k ? 0.7f : 1), k * 0.22f); v.curve = 0.4f; v.decPow = 2.2f; }
}

void ExpSetupBeds(int loc) {
    gExpBeds.clear();
    gExpBedLoc = loc;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gExpBeds.push_back(b); };
    switch (loc) {
        case 0: bed(0, 320, 0.8f, 0.05f, 0.13f, 0.3f, 0.17f, 0.6f); bed(1, 5200, 3, 0.006f, 0.05f, 0.1f, 0.07f, 0.4f); break;                 // water lapping, mould hiss
        case 1: bed(0, 520, 0.7f, 0.07f, 0.08f, 0.4f, 0.11f, 0.8f); bed(1, 4600, 7, 0.012f, 0.3f, 0.05f, 13, 0.55f); break;                  // surf, insects
        case 2: bed(0, 260, 0.8f, 0.06f, 0.09f, 0.3f, 0.13f, 0.5f); bed(1, 900, 3, 0.012f, 0.2f, 0.4f, 0.3f, 0.6f); break;                   // the kelp's water, its sway
        case 3: bed(3, 55, 1, 0.018f, 0, 0, 0.2f, 0.5f); bed(0, 180, 0.8f, 0.04f, 0.07f, 0.3f, 0.09f, 0.5f); break;                          // the eye's hum, the drowned halls
        case 4: bed(0, 90, 0.9f, 0.07f, 0.05f, 0.4f, 0.06f, 0.6f); bed(3, 41, 1, 0.012f, 0, 0, 0.1f, 0.7f); break;                          // pressure
        default: bed(0, 70, 0.9f, 0.05f, 0.04f, 0.4f, 0.05f, 0.7f); bed(3, 36.7f, 1, 0.008f, 0, 0, 0.07f, 0.8f); break;                    // the Hadal: almost nothing
    }
}

// the location's little sounds
void ExpEvents(float dt) {
    int loc = gExp.want.loc;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.7f};
    switch (loc) {
        case 0:
            if (chance(1.1f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(1400, 2600), 0, 0.12f, RR(0.02f, 0.05f)); v.f1 = v.f0 * 0.55f; v.curve = 0.4f; v.decPow = 2.3f; v.send = 0.9f; } // drips
            if (chance(0.02f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); for (int k = 0; k < 6; k++) { Voice& v = Puff(c, RR(300, 900), 120, RR(0.2f, 0.5f), 0.06f, k * RR(0.05f, 0.15f)); v.decPow = 1.8f; v.send = 0.8f; } } // distant rockfall
            break;
        case 1:
            if (chance(0.1f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Puff(c, 5000, 2500, RR(0.6f, 1.2f), 0.03f); v.hp = 1500; v.atk = 0.3f; v.tremR = 18; v.tremD = 0.4f; } // palm rustle
            if (chance(0.02f)) { Ctx b = c; PanGains(RR(-0.9f, 0.9f), b.gl, b.gr); b.vol = 0.25f; PlayBeast(A_BARK, CUE_CALL, 1.0f, 1.0f, b); } // a dog, far off
            if (chance(0.03f)) { float p = RR(-0.8f, 0.8f); for (int k = 0; k < 6; k++) { Ctx d = c; PanGains(p, d.gl, d.gr); Voice& v = Tone(d, W_FM, 150, 105, 0.3f, 0.03f, k * 0.25f + (k % 2) * 0.08f); v.fmRatio = 1.6f; v.fmIndex = 1.2f; v.curve = 0.4f; v.decPow = 2.4f; v.send = 0.8f; } } // drums far off
            break;
        case 2:
            if (chance(0.1f)) BuildCue(FindCue("amb.creak"), RR(0.3f, 0.6f), RR(-0.8f, 0.8f));   // kelp creak
            if (chance(0.4f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 3; k++) Bubble(c, k * RR(0.04f, 0.1f), RR(0.7f, 1.6f)); }
            if (chance(0.02f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(180, 320), 0, RR(2, 3.5f), 0.03f); v.f1 = v.f0 * RR(1.2f, 1.5f); v.atk = 0.7f; v.vibR = 4; v.vibD = 0.02f; v.send = 0.9f; } // whale song
            if (chance(0.08f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 5; k++) { Voice& v = Puff(c, 3000, 2000, 0.02f, 0.03f, k * 0.035f); v.hp = 1200; } } // fish flutter
            break;
        case 3:
            if (chance(0.05f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_FM, RR(900, 1600), 0, 0.3f, 0.02f, k * RR(0.08f, 0.2f)); v.f1 = v.f0; v.fmRatio = 2.76f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2.4f; v.send = 0.7f; } } // chains
            if (chance(0.03f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Puff(c, 400, 200, RR(1, 2), 0.05f); v.tremR = 9; v.tremD = 0.6f; v.atk = 0.4f; v.send = 0.7f; } // stone grinding
            if (chance(0.05f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, ExpNote(3, (int)RR(0, 5), 1), 0, 3, 0.01f); v.f1 = v.f0; v.fa0 = v.fa1 = 700; v.fb0 = v.fb1 = 1100; v.fq = 5; v.atk = 1; v.decPow = 1; v.send = 0.9f; } // a low choir
            break;
        default:
            if (chance(loc == 4 ? 0.06f : 0.03f)) Groan(RR(60, 110), 0.035f, RR(-0.8f, 0.8f));
            if (loc == 4 && chance(0.08f)) BuildCue(FindCue("amb.creak"), RR(0.3f, 0.5f), RR(-0.8f, 0.8f));
            if (loc == 5 && chance(0.02f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, ExpNote(5, (int)RR(0, 4), 2), 0, 4, 0.006f); v.f1 = v.f0; v.fa0 = v.fa1 = 600; v.fb0 = v.fb1 = 1000; v.fq = 5; v.atk = 1.5f; v.decPow = 1; v.send = 0.95f; }
            break;
    }
}

std::vector<std::pair<int, int>> ExpMotif(int loc) { // a short phrase for the winning layer, in scale degrees and 16ths
    std::vector<std::pair<int, int>> m;
    int deg = 0, total = 0;
    while (total < 32) { int d = R01() < 0.5f ? 4 : R01() < 0.5f ? 2 : 8; deg = std::clamp(deg + (int)(R01() * 5) - 2, -1, EXP_PAL[loc].n + 2); m.push_back({R01() < 0.12f ? -99 : deg, d}); total += d; }
    return m;
}
void ExpLead(int loc, float f, float dur, float g) {
    switch (loc) {
        case 0: GlassHarmonica(f * 2, dur * 1.6f, g, RR(-0.4f, 0.4f)); break;
        case 1: PlayInst(I_MARIMBA, f * 2, dur, g * 1.2f, 0.2f, 0.6f); break;
        case 2: Bowed(f * 2, dur * 1.4f, g, 0.2f); break;
        case 3: PlayInst(I_CHOIR, f * 2, dur * 1.5f, g * 0.8f, -0.2f, 0.5f); break;
        default: PlayInst(I_BELL, f * 2, dur * 2, g * 0.7f, 0.2f, 0.4f); break;
    }
}

// one sixteenth of the expedition's music
void ExpTick() {
    const ExpAudio& a = gExp.want;
    int loc = std::clamp(a.loc, 0, 5);
    int t = gExp.tick, step = t % 16, bar = t / 16;
    float td = 60.0f / gExp.bpm / 4, barDur = td * 16;
    static const int PROG[4] = {0, 5, 3, 4};
    int chord = PROG[(bar / 2) % 4] % EXP_PAL[loc].n;
    bool deep = loc >= 4;
    float w = gExp.walkS, f = gExp.fightS;
    // ---- walking: drone, a pad, the location's colour, the boss's approach
    if (w > 0.02f) {
        if (step == 0 && bar % 2 == 0) { PlayInst(I_DRONE, ExpNote(loc, 0, 0), barDur * 2.6f, 0.05f * w, -0.2f, 0.3f); PlayInst(I_DRONE, ExpNote(loc, 0, 0) * 1.4983f, barDur * 2.6f, 0.03f * w, 0.2f, 0.3f); } // root and fifth
        if (!deep && step == 0 && bar % 4 == 0) for (int k = 0; k < 3; k++) PlayInst(loc == 3 ? I_CHOIR : I_WARM_PAD, ExpNote(loc, chord + k * 2, 1), barDur * 4.4f, 0.014f * w, -0.4f + k * 0.4f, 0.3f);
        if (step == 8 && bar % 2 == 1) switch (loc) {
            case 0: if (R01() < 0.6f) GlassHarmonica(ExpNote(loc, chord + (int)(R01() * 3) * 2, 2), barDur * 1.5f, 0.02f * w, RR(-0.5f, 0.5f)); break;
            case 1: if (bar % 8 == 1) Conch(ExpNote(loc, 0, 2), barDur * 1.2f, 0.03f * w, -0.4f); else if (R01() < 0.5f) PlayChant(ExpNote(loc, chord, 2), 1 + (int)(R01() * 4), td * 6, 0.012f * w, 0.4f); break;
            case 2: Bowed(ExpNote(loc, chord + 2, 2), barDur * 2, 0.02f * w, 0.3f); break;
            case 3: break;
            default: if (R01() < 0.3f) Groan(ExpNote(loc, 0, 1), 0.02f * w, RR(-0.6f, 0.6f)); break;
        }
        if (loc == 3 && step % 4 == 0 && R01() < 0.7f) ClockTick(0.02f * w * (step % 8 ? 0.7f : 1), 0.5f); // the ticking that isn't a clock
        if (a.bossNear > 0.05f && step == 0 && bar % 2 == 0) { // a low tone that climbs as the boss room nears
            Ctx c{1, 1, 1, 1, B_MUSIC, 0.4f};
            float fr = ExpNote(loc, 0, 0) * powf(2.0f, a.bossNear * 7 / 12.0f);
            Voice& v = Tone(c, W_TRI, fr, fr, barDur * 2.2f, 0.035f * a.bossNear * w); v.atk = barDur * 0.6f; v.decPow = 1; v.tremR = 3 + 3 * a.bossNear; v.tremD = 0.3f; v.send = 0.5f;
        }
    }
    // ---- combat: the rhythm, the bass, and the layers
    if (f > 0.02f) {
        float g = f;
        switch (loc) { // the base rhythm
            case 0: { static const int P[16] = {6, 0, 0, 3, 0, 0, 4, 0, 5, 0, 0, 3, 0, 4, 0, 0}; if (P[step]) PlayInst(I_HANDDRUM, 0, 0.2f, 0.03f * g * P[step] / 6, 0.1f, 0.5f); if (step % 4 == 2 && R01() < 0.6f) Drip(ExpNote(loc, chord + (int)(R01() * 4), 3), 0.02f * g, RR(-0.6f, 0.6f)); break; }
            case 1: { static const int P[16] = {8, 0, 0, 5, 0, 0, 6, 0, 0, 0, 7, 0, 0, 4, 0, 5}; if (P[step]) PlayInst(I_LOGDRUM, ExpNote(loc, (step % 8) ? 2 : 0, 1), 0.3f, 0.045f * g * P[step] / 8, -0.1f, 0.5f); if (step % 2 == 1) PlayInst(I_SHAKER, 0, 0.1f, 0.012f * g, 0.3f, 0.5f); break; }
            case 2: { static const int P[16] = {5, 0, 3, 0, 4, 0, 3, 0, 5, 0, 3, 0, 4, 0, 3, 2}; PlayInst(I_SHAKER, 0, 0.1f, 0.01f * g * P[step] / 5, 0.25f, 0.5f); if (step == 0 || step == 10) PlayInst(I_HANDDRUM, 0, 0.2f, 0.03f * g, -0.2f, 0.5f); break; }
            case 3: { if (step == 0 || step == 8) PlayInst(I_LOGDRUM, ExpNote(loc, 0, 0), 0.4f, 0.05f * g, 0, 0.3f); if (step % 2 == 0) ClockTick(0.018f * g, 0.4f); break; }
            default: { if (step == 0 || step == 6) PlayInst(I_KICK, 0, 0.3f, 0.05f * g, 0, 0.3f); if (step == 12 && R01() < 0.5f) PlayInst(I_CLANK, ExpNote(loc, 0, 2), 1, 0.012f * g, RR(-0.5f, 0.5f), 0.3f); break; }
        }
        if (step == 0 || step == 8) PlayInst(I_SUB_BASS, ExpNote(loc, chord, 1), td * 7, 0.05f * g, 0, 0.4f);
        if (step == 0 && bar % 2 == 0) PlayInst(deep ? I_DRONE : I_PAD, ExpNote(loc, chord, 1), barDur * 2.2f, 0.018f * g, 0, 0.3f + 0.3f * gExp.dangerS);
        // the winning layer: the location's melodic voice
        if (gExp.winS > 0.02f && !gExp.motif.empty()) {
            if (step == 0 && bar % 2 == 0) { gExp.motifAt = 0; gExp.motifNext = t; }
            if (gExp.motifAt >= 0 && t == gExp.motifNext) {
                auto m = gExp.motif[gExp.motifAt];
                if (m.first != -99) ExpLead(loc, ExpNote(loc, chord + m.first, 1), td * m.second, 0.035f * gExp.winS * g);
                gExp.motifNext += m.second;
                if (++gExp.motifAt >= (int)gExp.motif.size()) gExp.motifAt = -1;
            }
        }
        // the dissonant layer: a hero near death or near breaking
        if (gExp.dangerS > 0.02f && step == 4 && bar % 2 == 0) {
            Ctx c{1, 1, 1, 1, B_MUSIC, 0.6f};
            float r = ExpNote(loc, chord, 2);
            const float iv[3] = {1.0f, 1.0595f, 1.4142f}; // a minor second and a tritone over the root
            for (int k = 0; k < 3; k++) { PanGains(-0.5f + k * 0.5f, c.gl, c.gr); Voice& v = Tone(c, W_SAW, r * iv[k], r * iv[k] * 0.995f, barDur * 1.8f, 0.012f * gExp.dangerS * g); v.cut0 = v.cut1 = 1400; v.tremR = 7; v.tremD = 0.5f; v.atk = barDur * 0.4f; v.decPow = 1; v.send = 0.6f; }
        }
        // a boss's theme: an ostinato of its own, sharper and doubled in its second phase
        if (a.bossType >= 0) {
            int every = gExp.phaseS > 0.5f ? 2 : 4;
            if (step % every == 0) {
                unsigned h = (unsigned)a.bossType * 2654435761u;
                int k = (step / every) % 4, deg = (int)((h >> (k * 5)) % 5) - 1;
                float fr = ExpNote(loc, deg, 1) * (gExp.phaseS > 0.5f ? 1.0595f : 1.0f);
                PlayInst(I_SAW_BASS, fr, td * every * 0.9f, 0.04f * g, 0, 0.4f + 0.4f * gExp.phaseS);
                if (gExp.phaseS > 0.5f) PlayInst(I_ARP_SAW, fr * 2, td * every * 0.6f, 0.012f * g, 0.3f, 0.6f);
            }
        }
    }
}

void ExpUpdate(float blockT) {
    const ExpAudio& a = gExp.want;
    gExp.s += ((a.on ? 1.0f : 0.0f) - gExp.s) * std::min(1.0f, blockT * 0.8f);
    if (gExp.s < 0.002f && !a.on) return;
    int loc = std::clamp(a.loc, 0, 5);
    if (gExpBedLoc != loc) ExpSetupBeds(loc);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gExp.walkS, a.mode == 0 ? 1.0f : 0.0f, 0.6f);
    toward(gExp.fightS, a.mode == 1 ? 1.0f : 0.0f, 1.2f);
    toward(gExp.winS, a.mode == 1 && a.winning ? 1.0f : 0.0f, 0.5f);
    toward(gExp.dangerS, a.mode == 1 && a.danger ? 1.0f : 0.0f, 0.8f);
    toward(gExp.doorS, a.door ? 1.0f : 0.0f, 1.5f);
    toward(gExp.phaseS, a.phase2 ? 1.0f : 0.0f, 2.0f);
    float want = a.mode == 1 ? EXP_PAL[loc].bpmFight : EXP_PAL[loc].bpmWalk;
    gExp.bpm += (want - gExp.bpm) * std::min(1.0f, blockT * 0.5f);
    if (gExp.motif.empty() || (a.mode != 1 && gExp.fightS < 0.05f)) gExp.motif = ExpMotif(loc);
    gExp.tickT += blockT;
    float td = 60.0f / gExp.bpm / 4;
    while (gExp.tickT >= td) { gExp.tickT -= td; ExpTick(); gExp.tick++; }
    // the walking pulse quickens as the light fails
    if (gExp.walkS > 0.05f) {
        gExp.pulseT += blockT;
        float period = 1.7f - 1.0f * (1 - std::clamp(a.light, 0.0f, 1.0f));
        if (gExp.pulseT >= period) { gExp.pulseT = 0; PlayInst(I_KICK, 0, 0.3f, 0.028f * gExp.walkS * (0.6f + 0.4f * (1 - a.light)), 0, 0.2f); }
    }
    // Death's Door: a heartbeat under everything
    if (gExp.doorS > 0.05f) { gExp.heartT += blockT; if (gExp.heartT >= 0.9f) { gExp.heartT = 0; Heartbeat(0.09f * gExp.doorS); } }
    gExp.ambT += blockT;
    if (gExp.ambT >= 0.05f) { ExpEvents(gExp.ambT); gExp.ambT = 0; }
}

// the expedition's ambience bed, one sample (called from Render)
float ExpBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gExpBeds) {
        float s;
        if (b.type == 3) { b.ph += b.f / SR; if (b.ph >= 1) b.ph -= 1; s = sinf(TAU * b.ph) + 0.5f * sinf(TAU * b.ph * 2) + 0.25f * sinf(TAU * b.ph * 3); }
        else {
            if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
            s = b.flt.Run(Noise());
        }
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}

// ---- combat voices: every enemy gets a voice, a pain cry and a death from its kind and size
Arch EnemyArch(int type) {
    switch ((EnemyType)type) {
        case EnemyType::SeaLouse: case EnemyType::CaveShrimp: case EnemyType::Lobster: case EnemyType::DysCrustacean: case EnemyType::CrustaceanQueen:
        case EnemyType::BarnacleCrab: case EnemyType::MantisShrimp: return A_CLICK;
        case EnemyType::BrineWorm: case EnemyType::GhostWorm: return A_WORM;
        case EnemyType::WarDog: return A_BARK;
        case EnemyType::GiantOctopus: case EnemyType::AlienHorror: case EnemyType::Cthulhu: case EnemyType::StarSpawn: case EnemyType::AbyssalEye: return A_KRAKEN;
        case EnemyType::ElectricEel: return A_EEL;
        case EnemyType::GreatWhite: return A_GRUNT;   // (a shark has no call of its own: it grunts and thrashes)
        case EnemyType::ArmorLostOne: case EnemyType::SunGod: return A_STONE;
        case EnemyType::LanternAngler: return A_FISH;
        case EnemyType::KelpWraith: return A_JELLY;
        case EnemyType::Leviathan: return A_WHALE;
        default: return A_HUMAN;
    }
}
void CombatVoiceImpl(int type, float size, int cue, float pan) {
    Arch ar = EnemyArch(type);
    float h = Hash01(TextFormat("enemy%d", type), 11);
    float pf = std::clamp(powf(std::max(0.2f, size), -0.5f), 0.35f, 2.2f) * (0.82f + 0.36f * h);
    if (type == (int)EnemyType::Siren || type == (int)EnemyType::CoconutQueen || type == (int)EnemyType::DrownedOracle) pf *= 1.6f; // higher voices
    Ctx c{0.8f, 1, 1, 1, B_SFX, 0.35f};
    PanGains(pan, c.gl, c.gr);
    PlayBeast(ar, cue, pf, size, c);
}
// music that answers the fight: the Island's chant rises when its Shaman heals; whale calls answer the Siren
void ExpReact(int kind) {
    int loc = std::clamp(gExp.want.loc, 0, 5);
    if (kind == 0) for (int k = 0; k < 3; k++) {
        float f = ExpNote(loc, k * 2, 2);
        Ctx c{1, 1, 1, 1, B_MUSIC, 0.5f};
        (void)c;
        PlayChant(f, 1 + k % 4, 0.5f, 0.03f, -0.3f + k * 0.3f);
        for (auto& v : gV) if (v.on && v.bus == B_MUSIC && v.t == 0 && v.delay == 0) v.delay = k * 0.35f;   // one after another, rising
    }
    else {
        Ctx c{1, 1, 1, 1, B_MUSIC, 0.9f};
        for (int k = 0; k < 2; k++) { PanGains(k ? 0.6f : -0.6f, c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(200, 280), 0, 2.2f, 0.04f, k * 0.9f); v.f1 = v.f0 * RR(1.25f, 1.5f); v.atk = 0.6f; v.vibR = 4; v.vibD = 0.02f; v.send = 0.9f; }
    }
}