// ================================================================ Red Tide (the Deep Arcade's shooter, stage 9)
// Included inside sound.cpp's anonymous namespace, after sound_expedition.inl (it borrows its signature voices).
// Music by the match's state: the calm is a drone and a pad with the bell counting down its last seconds; a tide is a
// pulse and a bass, a melodic layer once half the quota is in, and percussion and a far siren from tide 20. Blood in
// the water brings rising strings, an apex predator nearby its own motif panned toward it, a Hunt the faction's theme
// for the map, a boss its ostinato with everything else ducked, a downed diver a heartbeat and breathing under a
// muffled score, and the match's end a shanty. Each map has a palette, an ambience bed and its own little sounds.
struct RtPal { float root; int scale[7]; int n; float bpmCalm, bpmTide; int room; int lead; };
const RtPal RT_PAL[5] = {
    {73.42f, {0, 2, 3, 5, 7, 8, 10}, 7, 60, 104, RR_HALL, I_SQUEEZE},     // the Sunken Ship: brass and creaking timber
    {65.41f, {0, 1, 3, 5, 7, 8, 10}, 7, 54, 96, RR_CAVE, I_GLASS},        // the Underwater Cave: drips and glass
    {98.00f, {0, 2, 4, 7, 9}, 5, 66, 108, RR_OPENSEA, I_MARIMBA},         // the Reef: marimba and hand drums
    {58.27f, {0, 2, 3, 5, 7, 8, 11}, 7, 50, 92, RR_HALL, I_CHOIR},        // Atlantis: choir and organ
    {41.20f, {0, 1, 3, 5, 6, 8, 10}, 7, 44, 84, RR_CAVE, I_BELL},         // the Void: sub-bass and silence
};
struct RtState {
    RtAudio want;
    float s = 0, calmS = 1, tideS = 0, melS = 0, lateS = 0, bloodS = 0, predS = 0, huntS = 0, bossS = 0, downS = 0, overS = 0;
    int tick = 0; float tickT = 0, heartT = 0, breathT = 0, ambT = 0, bpm = 60, countT = -1;
    std::vector<std::pair<int, int>> motif; int motifAt = -1, motifNext = 0;
    int shantyAt = 0; int lastCount = -1;
};
RtState gRt;
std::vector<Bed> gRtBeds;
int gRtBedMap = -1;

float RtNote(int map, int deg, int oct) {
    const RtPal& p = RT_PAL[std::clamp(map, 0, 4)];
    int n = p.n, o = deg >= 0 ? deg / n : -((-deg + n - 1) / n), d = deg - o * n;
    return p.root * powf(2.0f, oct + o + p.scale[d] / 12.0f);
}
void RtSetupBeds(int map) {
    gRtBeds.clear();
    gRtBedMap = map;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gRtBeds.push_back(b); };
    switch (map) {
        case 0: bed(0, 240, 0.8f, 0.05f, 0.09f, 0.3f, 0.12f, 0.6f); bed(3, 49, 1, 0.008f, 0, 0, 0.15f, 0.6f); break;                 // the sea in the hold, the ship's low hum
        case 1: bed(0, 320, 0.8f, 0.045f, 0.13f, 0.3f, 0.17f, 0.6f); bed(1, 5200, 3, 0.005f, 0.05f, 0.1f, 0.07f, 0.4f); break;         // lapping water, a faint hiss
        case 2: bed(0, 480, 0.7f, 0.05f, 0.08f, 0.4f, 0.11f, 0.7f); bed(2, 4200, 1, 0.008f, 0, 0, 7, 0.8f); break;                    // the surge, snapping shrimp crackle
        case 3: bed(3, 55, 1, 0.014f, 0, 0, 0.2f, 0.5f); bed(0, 180, 0.8f, 0.04f, 0.07f, 0.3f, 0.09f, 0.5f); break;                    // the drowned halls
        default: bed(3, 30.9f, 1, 0.02f, 0, 0, 0.05f, 0.7f); bed(0, 80, 0.9f, 0.05f, 0.04f, 0.4f, 0.05f, 0.7f); break;                 // the void: sub-bass and pressure
    }
}
void RtEvents(float dt) {
    int map = gRt.want.map;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.7f};
    switch (map) {
        case 0:
            if (chance(0.15f)) BuildCue(FindCue("amb.creak"), RR(0.3f, 0.8f), RR(-0.8f, 0.8f));                                   // timber
            if (chance(0.05f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_FM, RR(300, 520), 0, 1.6f, 0.025f); v.f1 = v.f0 * 0.99f; v.fmRatio = 2.76f; v.fmIndex = 2.4f; v.fmIndex1 = 0.2f; v.decPow = 1.8f; v.send = 0.9f; } // brass rings
            if (chance(0.3f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 3; k++) Bubble(c, k * RR(0.04f, 0.1f), RR(0.8f, 1.8f)); }
            break;
        case 1:
            if (chance(1.0f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(1400, 2600), 0, 0.12f, RR(0.015f, 0.04f)); v.f1 = v.f0 * 0.55f; v.curve = 0.4f; v.decPow = 2.3f; v.send = 0.9f; } // drips
            if (chance(0.02f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); for (int k = 0; k < 6; k++) { Voice& v = Puff(c, RR(300, 900), 120, RR(0.2f, 0.5f), 0.05f, k * RR(0.05f, 0.15f)); v.decPow = 1.8f; v.send = 0.8f; } } // a far rockfall
            break;
        case 2:
            if (chance(0.5f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 4; k++) { Voice& v = Puff(c, 6000, 4000, 0.01f, 0.03f, k * RR(0.02f, 0.08f)); v.hp = 3000; } } // shrimp snaps
            if (chance(0.03f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(700, 1400), 0, 0.5f, 0.02f); v.f1 = v.f0 * RR(1.3f, 1.8f); v.vibR = 9; v.vibD = 0.05f; v.send = 0.8f; }  // a dolphin whistle
            if (chance(0.3f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 3; k++) Bubble(c, k * RR(0.04f, 0.1f), RR(0.7f, 1.6f)); }
            break;
        case 3:
            if (chance(0.03f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Puff(c, 400, 200, RR(1, 2), 0.04f); v.tremR = 9; v.tremD = 0.6f; v.atk = 0.4f; v.send = 0.7f; } // stone grinding
            if (chance(0.05f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RtNote(3, (int)RR(0, 5), 1), 0, 3, 0.008f); v.f1 = v.f0; v.fa0 = v.fa1 = 700; v.fb0 = v.fb1 = 1100; v.fq = 5; v.atk = 1; v.decPow = 1; v.send = 0.9f; } // a far choir
            break;
        default:
            if (chance(0.05f)) Groan(RR(45, 90), 0.03f, RR(-0.8f, 0.8f));
            if (chance(0.015f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(28, 40), 0, 4, 0.05f); v.f1 = v.f0 * 0.8f; v.atk = 1.2f; v.decPow = 1; v.send = 0.6f; } // something vast, far below
            break;
    }
}
std::vector<std::pair<int, int>> RtMotif(int map) {
    std::vector<std::pair<int, int>> m;
    int deg = 0, total = 0;
    while (total < 32) { int d = R01() < 0.5f ? 4 : R01() < 0.5f ? 2 : 8; deg = std::clamp(deg + (int)(R01() * 5) - 2, -1, RT_PAL[map].n + 2); m.push_back({R01() < 0.12f ? -99 : deg, d}); total += d; }
    return m;
}
void RtLead(int map, float f, float dur, float g) {
    switch (map) {
        case 0: PlayInst(I_SQUEEZE, f * 2, dur, g * 0.8f, 0.2f, 0.5f); break;
        case 1: GlassHarmonica(f * 2, dur * 1.6f, g, RR(-0.4f, 0.4f)); break;
        case 2: PlayInst(I_MARIMBA, f * 2, dur, g * 1.2f, 0.2f, 0.6f); break;
        case 3: PlayInst(I_CHOIR, f * 2, dur * 1.5f, g * 0.8f, -0.2f, 0.5f); break;
        default: PlayInst(I_BELL, f * 2, dur * 2, g * 0.6f, 0.2f, 0.4f); break;
    }
}
// the shanty that plays the match out: a composed 6/8 tune on the squeezebox over a hand drum (degrees, 8ths)
const int RT_SHANTY[][2] = {{4, 2}, {4, 1}, {4, 2}, {2, 1}, {0, 3}, {2, 3}, {4, 2}, {5, 1}, {4, 2}, {2, 1}, {1, 6},
                            {4, 2}, {4, 1}, {4, 2}, {2, 1}, {0, 3}, {2, 3}, {3, 2}, {2, 1}, {1, 2}, {-1, 1}, {0, 6}};
void RtTick() {
    const RtAudio& a = gRt.want;
    int map = std::clamp(a.map, 0, 4);
    int t = gRt.tick, step = t % 16, bar = t / 16;
    float td = 60.0f / gRt.bpm / 4, barDur = td * 16;
    static const int PROG[4] = {0, 5, 3, 4};
    int chord = PROG[(bar / 2) % 4] % RT_PAL[map].n;
    float duck = 1 - 0.55f * gRt.bossS;                          // a boss's theme ducks everything else
    float muff = 1 - 0.6f * gRt.downS;                           // a downed diver hears it all from far away
    float cm = gRt.calmS * duck * muff, tm = gRt.tideS * duck * muff;
    // ---- the calm: a drone and a pad
    if (cm > 0.02f) {
        if (step == 0 && bar % 2 == 0) { PlayInst(I_DRONE, RtNote(map, 0, 0), barDur * 2.6f, 0.05f * cm, -0.2f, 0.3f); PlayInst(I_DRONE, RtNote(map, 0, 0) * 1.4983f, barDur * 2.6f, 0.028f * cm, 0.2f, 0.3f); }
        if (map != 4 && step == 0 && bar % 4 == 0) for (int k = 0; k < 3; k++) PlayInst(map == 3 ? I_CHOIR : I_WARM_PAD, RtNote(map, chord + k * 2, 1), barDur * 4.4f, 0.013f * cm, -0.4f + k * 0.4f, 0.3f);
        if (step == 8 && bar % 2 == 1 && R01() < 0.6f) switch (map) {
            case 0: PlayInst(I_CLANK, RtNote(map, chord, 2), 1.2f, 0.01f * cm, RR(-0.5f, 0.5f), 0.3f); break;
            case 1: GlassHarmonica(RtNote(map, chord + 2, 2), barDur * 1.5f, 0.018f * cm, RR(-0.5f, 0.5f)); break;
            case 2: PlayInst(I_MARIMBA, RtNote(map, chord + 4, 2), 0.5f, 0.03f * cm, 0.3f, 0.4f); break;
            case 3: PlayInst(I_ORGAN, RtNote(map, chord, 1), barDur, 0.012f * cm, 0, 0.3f); break;
            default: Groan(RtNote(map, 0, 1), 0.018f * cm, RR(-0.6f, 0.6f)); break;
        }
    }
    // ---- a tide: the pulse, the bass, the melody at half quota, the late percussion and the siren
    if (tm > 0.02f) {
        if (step % 2 == 0) PlayInst(I_SAW_BASS, RtNote(map, chord, 1), td * 1.6f, 0.03f * tm * (step % 8 == 0 ? 1.0f : 0.6f), 0, 0.2f + 0.3f * gRt.lateS);
        if (step == 0 || step == 8) PlayInst(I_KICK, 0, 0.3f, 0.04f * tm, 0, 0.3f);
        switch (map) {
            case 0: if (step == 4 || step == 12) PlayInst(I_CLANK, RtNote(map, 0, 2), 0.6f, 0.008f * tm, 0.3f, 0.2f); break;
            case 1: if (step % 4 == 2 && R01() < 0.6f) Drip(RtNote(map, chord + (int)(R01() * 4), 3), 0.02f * tm, RR(-0.6f, 0.6f)); break;
            case 2: { static const int P[16] = {5, 0, 3, 0, 4, 0, 3, 0, 5, 0, 3, 0, 4, 0, 3, 2}; if (P[step]) PlayInst(I_HANDDRUM, 0, 0.2f, 0.025f * tm * P[step] / 5, 0.1f, 0.5f); break; }
            case 3: if (step % 2 == 0) ClockTick(0.015f * tm, 0.4f); break;
            default: if (step == 6) PlayInst(I_SUB_BASS, RtNote(map, 0, 0), td * 6, 0.05f * tm, 0, 0.3f); break;
        }
        if (step == 0 && bar % 2 == 0) PlayInst(map == 4 ? I_DRONE : I_PAD, RtNote(map, chord, 1), barDur * 2.2f, 0.016f * tm, 0, 0.3f);
        if (gRt.melS > 0.02f && !gRt.motif.empty()) {
            if (step == 0 && bar % 2 == 0) { gRt.motifAt = 0; gRt.motifNext = t; }
            if (gRt.motifAt >= 0 && t == gRt.motifNext) {
                auto m = gRt.motif[gRt.motifAt];
                if (m.first != -99) RtLead(map, RtNote(map, chord + m.first, 1), td * m.second, 0.03f * gRt.melS * tm);
                gRt.motifNext += m.second;
                if (++gRt.motifAt >= (int)gRt.motif.size()) gRt.motifAt = -1;
            }
        }
        if (gRt.lateS > 0.02f) {
            float l = gRt.lateS * tm;
            if (step == 4 || step == 12) PlayInst(I_SNARE, 0, 0.1f, 0.02f * l, -0.1f, 0.5f);
            if (step % 2 == 1) PlayInst(I_HAT, 0, 0.05f, 0.008f * l, 0.3f, 0.5f);
            if (step == 0 && bar % 8 == 0) { Ctx c{1, 1, 1, 1, B_MUSIC, 0.8f}; PanGains(RR(-0.7f, 0.7f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, 420, 620, barDur * 2, 0.008f * l); v.curve = 1; v.cut0 = v.cut1 = 1500; v.vibR = 0.5f; v.vibD = 0.12f; v.atk = barDur * 0.6f; v.decPow = 1; v.send = 0.9f; } // a far siren
        }
    }
    // ---- blood in the water: strings that climb with the scent
    if (gRt.bloodS > 0.05f && step == 0 && bar % 2 == 0) {
        float rise = powf(2.0f, gRt.bloodS * 5 / 12.0f);
        for (int k = 0; k < 2; k++) Bowed(RtNote(map, chord + k * 2, 2) * rise, barDur * 2.2f, 0.016f * gRt.bloodS * duck * muff, -0.3f + k * 0.6f);
    }
    // ---- an apex predator close by: its motif, panned toward it
    if (gRt.predS > 0.05f && step % 4 == 0 && bar % 2 == 0) {
        static const int PM[4] = {0, 1, 0, -2};
        float fr = RtNote(map, PM[step / 4], 0);
        Ctx c{1, 1, 1, 1, B_MUSIC, 0.3f}; PanGains(a.predatorPan, c.gl, c.gr);
        Voice& v = Tone(c, W_SAW, fr, fr * 0.99f, td * 3.5f, 0.045f * gRt.predS * muff); v.cut0 = 500; v.cut1 = 200; v.q = 2; v.decPow = 1.4f; v.send = 0.3f;
    }
    // ---- a Hunt: the faction's theme for the map
    if (gRt.huntS > 0.02f) {
        float h = gRt.huntS * duck * muff;
        switch (map) {
            case 0: if (step == 0 || step == 3 || step == 6) PlayInst(I_CLANK, RtNote(map, 0, 1), 0.8f, 0.014f * h, -0.2f, 0.3f); if (step == 0 && bar % 2 == 0) Conch(RtNote(map, 4, 1), barDur * 0.8f, 0.02f * h, 0.3f); break; // the Scrappers' anvil and horn
            case 1: if (step % 4 == 0) PlayInst(I_LOGDRUM, RtNote(map, step == 0 ? 0 : 2, 1), 0.3f, 0.035f * h, 0, 0.4f); break;                    // the Drowned's drums in the rock
            case 2: if (step % 4 == 0 || step == 14) PlayInst(I_HANDDRUM, 0, 0.2f, 0.035f * h, -0.2f, 0.5f); if (step == 8 && bar % 2 == 0) PlayChant(RtNote(map, chord, 2), 1 + (bar / 2) % 4, td * 4, 0.012f * h, 0.3f); break; // the tribe's chant
            case 3: if (step == 0 || step == 8) PlayInst(I_CHOIR, RtNote(map, chord + (step ? 2 : 0), 1), td * 7, 0.02f * h, 0, 0.4f); if (step % 4 == 0) PlayInst(I_KICK, 0, 0.3f, 0.03f * h, 0, 0.3f); break; // the Lost Ones' march
            default: if (step % 3 == 0) { Ctx c{1, 1, 1, 1, B_MUSIC, 0.4f}; Voice& v = Tone(c, W_FM, RtNote(map, 0, 1), RtNote(map, 0, 1), td * 2, 0.03f * h); v.fmRatio = 1.41f; v.fmIndex = 3; v.fmIndex1 = 0.5f; v.decPow = 2; } break; // the void cult's pulse
        }
    }
    // ---- a boss: its ostinato, sharper in its second phase
    if (gRt.bossS > 0.02f) {
        int every = a.bossPhase >= 2 ? 2 : 4;
        if (step % every == 0) {
            unsigned hsh = (unsigned)(map + 1) * 2654435761u;
            int k = (step / every) % 4, deg = (int)((hsh >> (k * 5)) % 5) - 1;
            float fr = RtNote(map, deg, 1) * (a.bossPhase >= 2 ? 1.0595f : 1.0f);
            PlayInst(I_SAW_BASS, fr, td * every * 0.9f, 0.045f * gRt.bossS * muff, 0, 0.4f + 0.2f * a.bossPhase);
            if (a.bossPhase >= 2) PlayInst(I_ARP_SAW, fr * 2, td * every * 0.6f, 0.012f * gRt.bossS * muff, 0.3f, 0.6f);
        }
        if (step == 0 && bar % 2 == 0) PlayInst(I_KICK, 0, 0.3f, 0.05f * gRt.bossS, 0, 0.3f);
    }
    // ---- the match is over: the shanty
    if (gRt.overS > 0.05f && step % 2 == 0) {
        int total = 0, at = (t / 2) % 48;   // 48 eighths round
        int i = 0; for (; i < (int)(sizeof RT_SHANTY / sizeof RT_SHANTY[0]); i++) { if (total == at) break; total += RT_SHANTY[i][1]; }
        if (total == at && i < (int)(sizeof RT_SHANTY / sizeof RT_SHANTY[0])) PlayInst(I_SQUEEZE, RtNote(map, RT_SHANTY[i][0], 2), td * 2 * RT_SHANTY[i][1] * 0.9f, 0.03f * gRt.overS, 0.1f, 0.5f);
        if (at % 6 == 0) { PlayInst(I_HANDDRUM, 0, 0.2f, 0.03f * gRt.overS, -0.2f, 0.4f); PlayInst(I_SUB_BASS, RtNote(map, (at / 12) % 2 ? 4 : 0, 1), td * 5, 0.04f * gRt.overS, 0, 0.3f); }
        if (at % 6 == 3) PlayInst(I_SHAKER, 0, 0.08f, 0.012f * gRt.overS, 0.3f, 0.4f);
    }
}
void RtUpdate(float blockT) {
    const RtAudio& a = gRt.want;
    gRt.s += ((a.on ? 1.0f : 0.0f) - gRt.s) * std::min(1.0f, blockT * 0.8f);
    if (gRt.s < 0.002f && !a.on) return;
    int map = std::clamp(a.map, 0, 4);
    if (gRtBedMap != map) RtSetupBeds(map);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    bool over = a.mode == 3;
    toward(gRt.calmS, a.mode == 0 ? 1.0f : 0.0f, 0.6f);
    toward(gRt.tideS, (a.mode == 1 || a.mode == 2) ? 1.0f : 0.0f, 1.0f);
    toward(gRt.melS, a.mode == 1 && a.quota >= 0.5f ? 1.0f : 0.0f, 0.5f);
    toward(gRt.lateS, !over && a.tide >= 20 ? 1.0f : 0.0f, 0.5f);
    toward(gRt.bloodS, over ? 0.0f : std::clamp(a.scent, 0.0f, 1.0f), 0.7f);
    toward(gRt.predS, over ? 0.0f : std::clamp(a.predator, 0.0f, 1.0f), 1.0f);
    toward(gRt.huntS, a.mode == 2 ? 1.0f : 0.0f, 0.8f);
    toward(gRt.bossS, a.boss && !over ? 1.0f : 0.0f, 1.2f);
    toward(gRt.downS, a.downed && !over ? 1.0f : 0.0f, 2.0f);
    toward(gRt.overS, over ? 1.0f : 0.0f, 1.5f);
    float want = over ? 116.0f : a.mode == 0 ? RT_PAL[map].bpmCalm : RT_PAL[map].bpmTide + (a.tide >= 20 ? 8 : 0);
    gRt.bpm += (want - gRt.bpm) * std::min(1.0f, blockT * (over ? 4.0f : 0.5f));
    if (gRt.motif.empty() || (a.mode == 0 && gRt.tideS < 0.05f)) gRt.motif = RtMotif(map);
    gRt.tickT += blockT;
    float td = 60.0f / gRt.bpm / 4;
    while (gRt.tickT >= td) { gRt.tickT -= td; RtTick(); gRt.tick++; }
    // the calm's last seconds: the tide bell counts them down
    if (a.mode == 0 && a.countdown > 0 && a.countdown <= 5.0f) {
        int sec = (int)ceilf(a.countdown);
        if (sec != gRt.lastCount) { gRt.lastCount = sec; PlayInst(I_BELL, RtNote(map, 4, 3) * (sec == 1 ? 1.335f : 1.0f), 1.4f, 0.03f, 0, 0.4f); }
    } else gRt.lastCount = -1;
    // breathing through the regulator, quicker with low health or blood about; a heartbeat when downed
    gRt.breathT += blockT;
    float stress = std::clamp(std::max(1 - a.hp, gRt.bloodS * 0.6f), 0.0f, 1.0f);
    float period = a.downed ? 1.1f : 4.2f - 2.6f * stress;
    if (!over && gRt.breathT >= period) {
        gRt.breathT = 0;
        Ctx c{1, 1, 1, 1, B_SFX, 0.2f};
        float g = (0.012f + 0.02f * stress + (a.downed ? 0.02f : 0)) * gRt.s;
        Voice& in = Puff(c, 900, 1400, period * 0.35f, g); in.atk = period * 0.15f; in.decPow = 1;
        for (int k = 0; k < 4; k++) Bubble(c, period * 0.5f + k * 0.07f, RR(0.9f, 1.6f));
    }
    if (gRt.downS > 0.05f) { gRt.heartT += blockT; if (gRt.heartT >= 0.8f) { gRt.heartT = 0; Heartbeat(0.09f * gRt.downS); } }
    gRt.ambT += blockT;
    if (gRt.ambT >= 0.05f) { RtEvents(gRt.ambT); gRt.ambT = 0; }
}
float RtBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gRtBeds) {
        float s;
        if (b.type == 3) { b.ph += b.f / SR; if (b.ph >= 1) b.ph -= 1; s = sinf(TAU * b.ph) + 0.5f * sinf(TAU * b.ph * 2) + 0.25f * sinf(TAU * b.ph * 3); }
        else {
            if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
            s = b.flt.Run(Noise());
        }
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out * (1 - 0.5f * gRt.downS);
}

// ---- voices of the Red Tide's species. The parkour names (ArchOf) don't know most of the sea's, so these come first.
Arch RtArchOf(const char* name) {
    static const struct { const char* key; Arch a; } K[] = {
        {"Goby", A_FISH}, {"Glowworm", A_JELLY}, {"Goliath", A_GRUNT}, {"Lobster", A_CLICK}, {"Matriarch", A_WHALE}, {"Wyrm", A_HISS}, {"Relict", A_KRAKEN},
        {"Leviathan", A_WHALE}, {"Sand Worm", A_WORM}, {"Turtle", A_GRUNT}, {"Loggerhead", A_GRUNT}, {"Leatherback", A_GRUNT}, {"Crocodile", A_GRUNT},
        {"Caiman", A_GRUNT}, {"Monitor", A_HISS}, {"Iguana", A_HISS}, {"Krait", A_HISS}, {"Seal", A_BARK}, {"Dolphin", A_CHATTER}, {"Dugong", A_WHALE},
        {"Squid", A_KRAKEN}, {"Toad", A_CROAK}, {"Newt", A_CROAK}, {"Salamander", A_CROAK}, {"Axolotl", A_CROAK}, {"Cormorant", A_BIRD}, {"Swiftlet", A_BIRD},
        {"Urchin", A_JELLY}, {"Starfish", A_JELLY}, {"Star", A_JELLY}, {"Sea Pig", A_JELLY}, {"Cucumber", A_JELLY}, {"Sand Dollar", A_JELLY}, {"Nudibranch", A_JELLY},
        {"Whelk", A_CLICK}, {"Sea Hare", A_JELLY}, {"Siphonophore", A_JELLY}, {"Man o' War", A_JELLY}, {"Krill", A_CLICK}, {"Amphipod", A_CLICK}, {"Hopper", A_CLICK},
        {"Crayfish", A_CLICK}, {"Water Bug", A_BUZZ}, {"Nymph", A_BUZZ}, {"Lamprey", A_EEL}, {"Hagfish", A_EEL}, {"Skate", A_RAY}, {"Stingray", A_RAY},
    };
    for (const auto& k : K) if (strstr(name, k.key)) return k.a;
    return ArchOf(name);
}
// play a species' cue from `dist` metres away: past 40 m it is muffled (the new voices get a low-pass) and quieter
void RtBeastImpl(const char* name, float size, int cue, float dist, float pan) {
    float att = std::clamp(1 - dist / 70.0f, 0.0f, 1.0f); att *= att;
    if (att < 0.01f) return;
    Arch ar = RtArchOf(name);
    float h = Hash01(name, 7);
    float pf = std::clamp(powf(std::max(0.2f, size * 0.6f), -0.5f), 0.35f, 2.2f) * (0.82f + 0.36f * h);
    Ctx c{att * std::clamp(0.6f + size * 0.12f, 0.6f, 1.4f), 1, 1, 1, B_SFX, 0.35f + 0.4f * std::clamp(dist / 60.0f, 0.0f, 1.0f)};
    if (ar == A_EEL) c.vol *= 2.6f;   // (the eels' breathy hiss sits well under the other voices)
    PanGains(pan, c.gl, c.gr);
    bool was[128]; for (int i = 0; i < 128; i++) was[i] = gV[i].on && gV[i].t > 0;
    PlayBeast(ar == A_PLANT ? A_JELLY : ar, cue, pf, std::min(size * 0.6f, 3.0f), c);
    if (dist > 40) {
        float cut = std::max(350.0f, 2400 - (dist - 40) * 60);
        for (int i = 0; i < 128; i++) if (gV[i].on && !was[i] && gV[i].t == 0) { Voice& v = gV[i]; v.cut0 = v.cut0 > 0 ? std::min(v.cut0, cut) : cut; v.cut1 = v.cut1 > 0 ? std::min(v.cut1, cut) : cut; }
    }
}
// the match's effects: guns, impacts, tonics, drops, the faction's arrival, hazards and the HUD
void RtCueImpl(int kind, float vol, float pan, float dist) {
    float att = std::clamp(1 - dist / 60.0f, 0.0f, 1.0f); att *= att;
    if (att * vol < 0.01f) return;
    Ctx c{vol * att, 1, 1, 1, B_SFX, 0.3f};
    PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case RTC_SHOT: { Voice& v = Puff(c, 2200, 500, 0.12f, 0.16f); v.decPow = 2.5f; Thud(c, 140, 0.12f); Voice& b = Tone(c, W_SINE, 900, 300, 0.08f, 0.05f); b.decPow = 2; break; } // a pneumatic dart gun, muffled by the water
        case RTC_HARPOON: { Whoosh(c, 300, 1200, 0.18f, 0.12f); Thud(c, 110, 0.14f); Voice& v = Tone(c, W_FM, 620, 600, 0.35f, 0.03f); v.fmRatio = 2.76f; v.fmIndex = 2; v.decPow = 2; break; }
        case RTC_HIT: { Voice& v = Puff(c, 900, 300, 0.1f, 0.14f); v.decPow = 2; Thud(c, 90, 0.1f); break; }                             // into flesh
        case RTC_WALL: { Voice& v = Tone(c, W_FM, RR(700, 1100), 0, 0.25f, 0.05f); v.f1 = v.f0 * 0.98f; v.fmRatio = 2.3f; v.fmIndex = 1.8f; v.decPow = 2.4f; Voice& p = Puff(c, 3000, 1000, 0.04f, 0.06f); p.hp = 800; break; }
        case RTC_BLAST: { Voice& v = Puff(c, 1200, 120, 1.1f, 0.3f); v.decPow = 1.6f; Thud(c, 55, 0.3f); for (int k = 0; k < 8; k++) Bubble(c, 0.1f + k * 0.05f, RR(1.2f, 2.5f)); break; }
        case RTC_ARC: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SQR, RR(90, 160), 0, 0.06f, 0.04f, k * 0.035f); v.f1 = v.f0 * 1.3f; v.cut0 = v.cut1 = 3000; v.noiseMix = 0.5f; } break; }
        case RTC_MELEE: { Whoosh(c, 500, 1400, 0.15f, 0.1f); Thud(c, 120, 0.12f, 0.08f); break; }
        case RTC_CRATE: { Thud(c, 70, 0.25f); for (int k = 0; k < 4; k++) { Voice& v = Puff(c, RR(400, 900), 150, 0.3f, 0.06f, k * 0.06f); v.decPow = 2; } break; }
        case RTC_PICKUP: { Ctx u = c; u.bus = B_UI; Voice& v = Tone(u, W_SINE, 660, 660, 0.12f, 0.06f); v.decPow = 2; Voice& w = Tone(u, W_SINE, 990, 990, 0.18f, 0.05f, 0.07f); w.decPow = 2; break; }
        case RTC_TONIC: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SINE, 240 + k * 30, 180 + k * 20, 0.12f, 0.05f, k * 0.11f); v.decPow = 2; } Voice& w = Tone(c, W_SINE, 520, 780, 0.5f, 0.03f, 0.6f); w.atk = 0.1f; w.send = 0.6f; break; } // glug glug, then a lift
        case RTC_DROP: { Ctx u = c; u.bus = B_UI; for (int k = 0; k < 3; k++) { Voice& v = Tone(u, W_FM, 523.25f * powf(1.26f, (float)k), 0, 0.5f, 0.04f, k * 0.09f); v.f1 = v.f0; v.fmRatio = 3.5f; v.fmIndex = 1.5f; v.fmIndex1 = 0.1f; v.decPow = 1.8f; v.send = 0.6f; } break; }
        case RTC_ARRIVAL: { Voice& v = Tone(c, W_SAW, 110, 110, 1.6f, 0.05f); v.fa0 = v.fa1 = 520; v.fb0 = v.fb1 = 900; v.fq = 4; v.atk = 0.3f; v.hold = 0.6f; v.decPow = 1.2f; v.send = 0.9f; Voice& w = Tone(c, W_SAW, 82.4f, 82.4f, 1.8f, 0.035f, 0.1f); w.cut0 = w.cut1 = 600; w.atk = 0.3f; w.send = 0.9f; break; } // a horn: they are coming
        case RTC_HAZARD: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SQR, 880, 880, 0.09f, 0.025f, k * 0.14f); v.cut0 = v.cut1 = 2000; } break; }
        case RTC_BELL: { Voice& v = Tone(c, W_FM, 330, 330, 3.2f, 0.09f); v.fmRatio = 2.76f; v.fmIndex = 3; v.fmIndex1 = 0.3f; v.decPow = 1.4f; v.send = 0.9f; Voice& w = Tone(c, W_FM, 165, 165, 3.6f, 0.05f, 0.02f); w.fmRatio = 1.41f; w.fmIndex = 1.2f; w.decPow = 1.3f; w.send = 0.9f; break; } // the tide bell
        case RTC_CLEAR: { Ctx u = c; u.bus = B_UI; const float fs[4] = {392, 493.9f, 587.3f, 784}; for (int k = 0; k < 4; k++) { Voice& v = Tone(u, W_TRI, fs[k], fs[k], 0.6f, 0.035f, k * 0.12f); v.decPow = 1.6f; v.send = 0.5f; } break; }
        case RTC_KILL: { Ctx u = c; u.bus = B_UI; Voice& v = Tone(u, W_SINE, 1400, 1400, 0.05f, 0.03f); v.decPow = 3; break; }
        case RTC_DOWN: { Voice& v = Tone(c, W_SAW, 220, 110, 1.4f, 0.05f); v.cut0 = 1400; v.cut1 = 300; v.curve = 0.6f; v.decPow = 1.2f; v.send = 0.7f; Heartbeat(0.08f); break; }
        case RTC_REVIVE: { Ctx u = c; u.bus = B_UI; Voice& v = Tone(u, W_SINE, 330, 660, 0.6f, 0.05f); v.atk = 0.1f; v.decPow = 1.5f; v.send = 0.6f; break; }
        case RTC_EMPTY: { Ctx u = c; u.bus = B_UI; Voice& v = Tone(u, W_SQR, 1800, 1700, 0.02f, 0.03f); v.cut0 = v.cut1 = 4000; break; }   // the click of an empty magazine
        case RTC_RELOAD: { for (int k = 0; k < 2; k++) { Voice& v = Tone(c, W_FM, 900 + k * 400, 0, 0.06f, 0.04f, k * 0.25f); v.f1 = v.f0; v.fmRatio = 2.3f; v.fmIndex = 2; v.decPow = 3; } break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
// a diver's quip: formant babble on the voice bus in that diver's register, one syllable per ~2.5 letters
void RtQuipImpl(int voice, int syllables, float pan) {
    static const float ROOT[4] = {150, 118, 205, 132};   // the Diver, the Whaler, the Stowaway, the Mechanic
    static const float FA[5][2] = {{700, 1150}, {450, 800}, {800, 1250}, {400, 1900}, {350, 700}};
    Ctx c{1, 1, 1, 1, B_VOICE, 0.25f};
    PanGains(pan, c.gl, c.gr);
    float f = ROOT[std::clamp(voice, 0, 3)], at = 0;
    int n = std::clamp(syllables, 1, 18);
    unsigned seed = (unsigned)(voice * 7919 + n * 104729);
    for (int k = 0; k < n; k++) {
        seed = seed * 1664525u + 1013904223u;
        int vw = (seed >> 16) % 5;
        float len = 0.09f + ((seed >> 8) % 100) / 100.0f * 0.08f;
        float pitch = f * (1 + 0.12f * sinf(k * 1.7f + voice)) * (k == n - 1 ? 0.88f : 1.0f);   // the phrase falls at its end
        Voice& v = Tone(c, W_SAW, pitch * 1.03f, pitch, len, 0.06f, at);
        v.curve = 0.3f; v.atk = 0.015f; v.hold = len * 0.4f; v.decPow = 1.4f; v.noiseMix = 0.06f; v.vibR = 6; v.vibD = 0.01f;
        v.fa0 = FA[vw][0]; v.fa1 = FA[(vw + 1) % 5][0]; v.fb0 = FA[vw][1]; v.fb1 = FA[(vw + 1) % 5][1]; v.fq = 5;
        at += len + (k % 4 == 3 ? 0.08f : 0.02f);
    }
}
