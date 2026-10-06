// ================================================================ Fathoms (the Deep Arcade's island strategy game)
// Included inside sound.cpp's anonymous namespace after sound_ballpit.inl. An age-of-sail score that grows with the
// eras: in the Sail Era a fiddle and squeezebox shanty over a slow pad; the Steam Era adds a chugging bass and snare;
// the Leviathan Era a choir and a deep organ drone. Battle near the camera brings in war drums and a darker minor
// line; a storm rumbles under it all. The bed is the open sea: waves, wind and gulls, louder zoomed out, with a lava
// roar near an erupting volcano. Effects: muskets, cannon, the bell of a finished building, era fanfares, alarms.
struct FaState {
    FaAudio want;
    float s = 0, battleS = 0, eraS[3] = {1, 0, 0};
    int tick = 0; float tickT = 0, bpm = 96, ambT = 0; int lastOver = 0;
};
FaState gFa;
std::vector<Bed> gFaBeds; int gFaBedKind = -1;
float FaNote(int deg, int oct, bool minor = false) {
    static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10};
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return 196.0f * powf(2.0f, oct + o + (minor ? MIN : MAJ)[d] / 12.0f);   // (G)
}
void FaSetupBeds(int kind) {
    gFaBeds.clear(); gFaBedKind = kind;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gFaBeds.push_back(b); };
    bed(0, 420, 0.6f, 0.030f, 0.08f, 0.5f, 0.12f, 0.75f);   // the waves rolling in
    bed(1, 900, 0.8f, 0.010f, 0.05f, 0.6f, 0.07f, 0.6f);    // the wind
    if (kind == 1) bed(0, 120, 0.8f, 0.04f, 0.3f, 0.4f, 0.5f, 0.5f);   // a storm's rumble
    if (kind == 2) bed(0, 90, 0.9f, 0.05f, 0.6f, 0.3f, 1.2f, 0.4f);    // lava roaring
}
void FaEvents(float dt) {
    const FaAudio& a = gFa.want; auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.5f};
    if (chance(0.18f * (0.4f + a.zoom))) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 2 + (int)(R01() * 2); k++) { Voice& v = Tone(c, W_SAW, RR(1800, 2400), RR(1200, 1600), RR(0.12f, 0.22f), 0.004f, k * RR(0.15f, 0.3f)); v.fa0 = 2600; v.fa1 = 2200; v.fq = 4; v.decPow = 1.3f; v.send = 0.6f; } }   // gulls
    if (chance(0.08f * a.busy)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SQR, RR(500, 700), 0, 0.05f, 0.006f); v.cut0 = v.cut1 = 2400; v.decPow = 2.5f; Thud(c, RR(180, 260), 0.01f, 0.1f); }   // hammers and work on the island
    if (chance(0.05f * a.busy)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); PlayInst(I_BELL, FaNote(4, 2), 0.8f, 0.006f, RR(-0.5f, 0.5f), 0.4f); }   // a ship's bell somewhere
    if (a.storm > 0.5f && chance(0.05f)) { Thud(c, 50, 0.08f); Voice& v = Puff(c, 600, 120, 2.5f, 0.03f); v.atk = 0.05f; v.decPow = 1.2f; }   // thunder
}
void FaTick() {
    const FaAudio& a = gFa.want; int t = gFa.tick, e = t % 12, bar = t / 12; float td = 60.0f / gFa.bpm / 2;   // (6/8: twelve eighths a bar of two)
    bool minor = gFa.battleS > 0.5f;
    static const int CH[4] = {0, 3, 4, 0}, CHM[4] = {0, 5, 3, 4};
    int ch = (minor ? CHM : CH)[bar % 4];
    float s0 = gFa.eraS[0], s1 = gFa.eraS[1], s2 = gFa.eraS[2], bt = gFa.battleS;
    // the pad: always, quietly
    if (e == 0) for (int k = 0; k < 3; k++) PlayInst(I_WARM_PAD, FaNote(ch + k * 2, 0, minor), td * 12, 0.010f, -0.3f + 0.3f * k, 0.4f);
    // the shanty (Sail and Steam): fiddle and squeezebox
    static const int TUNE[24] = {4, -99, 4, 5, -99, 7, 9, -99, 7, 5, -99, 4, 2, -99, 2, 4, -99, 5, 4, -99, 2, 0, -99, -99};
    int d = TUNE[t % 24];
    if (d != -99 && (s0 + s1) > 0.05f && (bar % 8) < 6) PlayInst(bt > 0.5f ? I_FIDDLE : (bar / 8) % 2 ? I_SQUEEZE : I_FIDDLE, FaNote(d, 1, minor), td * 1.8f, 0.013f * (s0 + s1 * 0.8f) * (1 - bt * 0.5f), 0.15f, 0.55f);
    if ((e == 0 || e == 6) && (s0 + s1) > 0.05f) PlayInst(I_PLUCK, FaNote(ch, -1, minor), td * 3, 0.02f * (s0 + s1), -0.1f, 0.4f);
    // Steam: the engine room's chug and a snare
    if (s1 + s2 > 0.05f) { if (e % 3 == 0) PlayInst(I_CHUFF, 90, td * 0.8f, 0.012f * (s1 + s2 * 0.6f), 0, 0.3f); if (e == 6) PlayInst(I_SNARE, 200, td, 0.012f * (s1 + s2), 0.1f, 0.4f); if (e == 0) PlayInst(I_SAW_BASS, FaNote(ch, -2, minor), td * 4, 0.02f * (s1 + s2), 0, 0.3f); }
    // Leviathan: the choir and a deep organ
    if (s2 > 0.05f && e == 0 && bar % 2 == 0) { PlayInst(I_CHOIR, FaNote(ch + 4, 0, minor), td * 22, 0.012f * s2, -0.2f, 0.4f); PlayInst(I_ORGAN, FaNote(ch, -2, minor), td * 22, 0.012f * s2, 0.2f, 0.3f); }
    // battle: war drums and a low line
    if (bt > 0.05f) { if (e == 0 || e == 3 || e == 6 || e == 9) PlayInst(I_LOGDRUM, e == 0 ? 70.0f : 90.0f, td * 2, 0.04f * bt, 0, 0.4f); if (e == 6 || e == 10) PlayInst(I_HANDDRUM, 160, td, 0.02f * bt, 0.2f, 0.5f); if (e % 2 == 0) PlayInst(I_SAW_BASS, FaNote(ch, -2, true), td * 1.6f, 0.016f * bt, 0, 0.35f + 0.2f * bt); }
}
void FaUpdate(float blockT) {
    const FaAudio& a = gFa.want;
    gFa.s += ((a.on ? 1.0f : 0.0f) - gFa.s) * std::min(1.0f, blockT * 0.8f);
    if (gFa.s < 0.002f && !a.on) return;
    int bedKind = a.lava > 0.5f ? 2 : a.storm > 0.5f ? 1 : 0; if (gFaBedKind != bedKind) FaSetupBeds(bedKind);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gFa.battleS, a.battle, 0.6f);
    for (int e = 0; e < 3; e++) toward(gFa.eraS[e], a.era == e ? 1.0f : 0.0f, 0.25f);
    float want = 92 + 24 * gFa.battleS; gFa.bpm += (want - gFa.bpm) * std::min(1.0f, blockT * 0.3f);
    gFa.tickT += blockT; float td = 60.0f / gFa.bpm / 2;
    while (gFa.tickT >= td) { gFa.tickT -= td; FaTick(); gFa.tick++; }
    if (a.over && a.over != gFa.lastOver) {
        if (a.over == 1) for (int k = 0; k < 6; k++) PlayInst(I_SYNTH_LEAD, FaNote(k == 5 ? 7 : k * 2 % 7, 1), k == 5 ? 1.4f : 0.2f, 0.03f, -0.3f + 0.1f * k, 0.6f, false);
        else for (int k = 0; k < 4; k++) PlayInst(I_ORGAN, FaNote(4 - k, 0, true), 0.6f + k * 0.3f, 0.03f, 0, 0.3f);
    }
    gFa.lastOver = a.over;
    gFa.ambT += blockT; if (gFa.ambT >= 0.05f) { FaEvents(gFa.ambT); gFa.ambT = 0; }
}
float FaBedSample(float tt, int sampleIndex) {
    float out = 0, z = 0.5f + gFa.want.zoom * 0.8f;
    for (auto& b : gFaBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * z * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
void FaCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch * RR(0.92f, 1.08f), 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f}; PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case FAC_SHOT: { Voice& v = Puff(c, 3000 * p, 600, 0.12f, 0.05f); v.decPow = 2.6f; Thud(c, 160 * p, 0.03f); break; }   // a musket crack
        case FAC_BOOM: { Thud(c, 55 * p, 0.12f); Voice& v = Puff(c, 1200, 120, 0.7f, 0.07f); v.decPow = 1.6f; break; }       // a cannon
        case FAC_DIE: { Voice& v = Tone(c, W_SAW, 300 * p, 140 * p, 0.25f, 0.016f); v.fa0 = 900; v.fa1 = 500; v.fq = 2; v.decPow = 1.6f; Thud(c, 110, 0.03f, 0.1f); break; }
        case FAC_BUILT: { for (int k = 0; k < 3; k++) PlayInst(I_BELL, FaNote(k * 2, 2), 0.7f, 0.02f, pan, 0.5f); Thud(c, 220, 0.03f); break; }
        case FAC_READY: { Voice& v = Tone(c, W_TRI, 660 * p, 880 * p, 0.12f, 0.02f); v.decPow = 1.5f; break; }
        case FAC_TECH: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, FaNote(k * 2 + 4, 1), 0, 0.3f, 0.02f, k * 0.08f); v.decPow = 1.6f; } break; }
        case FAC_ERA: { for (int k = 0; k < 5; k++) PlayInst(I_SYNTH_LEAD, FaNote(k == 4 ? 7 : k * 2, 1), k == 4 ? 1.0f : 0.18f, 0.03f, 0, 0.6f); PlayInst(I_SUB_BASS, FaNote(0, -2), 1.6f, 0.05f, 0, 0.3f); break; }
        case FAC_ALARM: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SQR, 880, 660, 0.15f, 0.025f, k * 0.22f); v.cut0 = v.cut1 = 2500; v.decPow = 1.2f; } break; }   // the ship's bell rung fast
        case FAC_RAZED: { Thud(c, 70, 0.1f); Voice& v = Puff(c, 1500, 200, 1.2f, 0.06f); v.decPow = 1.3f; for (int k = 0; k < 5; k++) Thud(c, RR(150, 400), 0.02f, 0.1f + k * 0.08f); break; }
        case FAC_ERUPT: { Thud(c, 40, 0.16f); Voice& v = Puff(c, 900, 100, 3.0f, 0.08f); v.atk = 0.1f; v.decPow = 1.1f; break; }
        case FAC_TREMOR: { Voice& v = Tone(c, W_SINE, 38, 34, 2.5f, 0.08f); v.tremR = 9; v.tremD = 0.7f; v.atk = 0.4f; v.decPow = 0.9f; break; }
        case FAC_DRUMS: { for (int k = 0; k < 8; k++) PlayInst(I_LOGDRUM, k % 4 == 0 ? 70.0f : 95.0f, 0.25f, 0.05f, pan, 0.4f); break; }
        case FAC_RELIC: { for (int k = 0; k < 4; k++) PlayInst(I_GLASS, FaNote(k * 2 + 7, 1), 1.2f, 0.02f, pan, 0.6f); break; }
        case FAC_KRAKEN: { Voice& v = Tone(c, W_SAW, 60, 45, 2.0f, 0.06f); v.fa0 = 300; v.fa1 = 200; v.fq = 3; v.vibR = 4; v.vibD = 0.05f; v.atk = 0.3f; v.decPow = 1; break; }
        case FAC_MOLT: { Voice& v = Puff(c, 2500, 800, 0.4f, 0.04f); v.tremR = 30; v.tremD = 0.6f; v.decPow = 1.4f; break; }
        case FAC_CONVERT: { PlayInst(I_CHOIR, FaNote(4, 0, true), 1.2f, 0.03f, pan, 0.5f); break; }
        case FAC_ULTIMATE: { PlayInst(I_ORGAN, FaNote(0, -2, true), 2.5f, 0.05f, 0, 0.4f); Thud(c, 45, 0.14f); PlayInst(I_CHOIR, FaNote(4, 0, true), 2.5f, 0.04f, 0, 0.5f); break; }
        case FAC_HERO: { Voice& v = Tone(c, W_SAW, 300, 600, 0.3f, 0.03f); v.fa0 = 1200; v.fa1 = 2400; v.fq = 2; v.decPow = 1.3f; Thud(c, 120, 0.04f); break; }
        case FAC_CLICK: { Voice& v = Tone(c, W_SQR, 1400, 1300, 0.04f, 0.05f); v.cut0 = v.cut1 = 4000; v.decPow = 3; break; }
        case FAC_DENY: { Voice& v = Tone(c, W_SQR, 220, 180, 0.15f, 0.02f); v.cut0 = v.cut1 = 1200; v.decPow = 1.5f; break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
