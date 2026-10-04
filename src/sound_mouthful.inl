// ================================================================ Mouthful (the Deep Arcade's eat-and-grow arena)
// Included inside sound.cpp's anonymous namespace after sound_flight.inl. Design doc p. 19-20, "Sound design": the bands
// have beds (surf and gulls in the shallows, the reef's snapping-shrimp crackle, a wide quiet in the blue, the vents'
// hiss and the pressure drone in the trench); growth is heard (your swim heavier with tier); bites snap, crunch and gulp;
// every ability has a voice (the stonefish's none); the apex sharks have a two-note motif inside 40 m, the orca pod
// clicks, the leviathan's single deep note wakes the trench; the crown gets a brass fanfare, a slow drum while held and
// a crash when the king dies. The music is a bright reef theme that darkens by band and quickens with your tier; dusk
// brings the night layer, high tide a countdown; the blobfish has its own four notes, in a minor key.
struct MfState {
    MfAudio want;
    float s = 0, themeS = 0, deepS = 0, nightS = 0, apexS = 0, orcaS = 0, crownS = 0, blobS = 0, highS = 0;
    int tick = 0; float tickT = 0, bpm = 96;
    float ambT = 0, clickT = 0, engineT = 0;
    int resultPlayed = 0, lastHigh = -1;
};
MfState gMf;
std::vector<Bed> gMfBeds;
bool gMfBedsReady = false;
const float MF_ROOT = 130.81f;   // C: the reef theme's key
const int MF_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};
float MfNote(int deg, int oct) { int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return MF_ROOT * powf(2.0f, oct + o + MF_SCALE[d] / 12.0f); }
// the reef theme in 4/4 (degree, eighths; -99 a rest): bouncy, sunlit, a little silly. Composed here.
const int MF_THEME[][2] = {{4, 2}, {2, 1}, {4, 1}, {5, 2}, {4, 2}, {7, 3}, {6, 1}, {4, 2}, {2, 2}, {0, 2}, {2, 1}, {4, 1}, {2, 2}, {1, 2}, {0, 4}, {-99, 4}};
const int MF_THEME_N = (int)(sizeof MF_THEME / sizeof MF_THEME[0]);
const int MF_CHORDS[4] = {0, 5, 3, 4};   // a bar each: I, vi, IV, V

void MfSetupBeds() {
    gMfBeds.clear();
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gMfBeds.push_back(b); };
    bed(0, 1200, 0.6f, 0, 0.1f, 0.4f, 0.12f, 0.85f);    // 0 the surf over the shallows (heard from below)
    bed(1, 5200, 2.0f, 0, 0.9f, 0.5f, 3.1f, 0.9f);      // 1 the reef's crackle (snapping shrimp: a fizz that flutters)
    bed(0, 160, 0.8f, 0, 0.03f, 0.3f, 0.05f, 0.6f);     // 2 the blue's wide quiet (a low swell, almost nothing)
    bed(0, 60, 1.4f, 0, 0.02f, 0.2f, 0.04f, 0.5f);      // 3 the pressure drone of the trench
    bed(1, 2400, 1.2f, 0, 0.6f, 0.6f, 1.4f, 0.8f);      // 4 the vents' hiss
    bed(0, 110, 1.0f, 0, 0.5f, 0.3f, 7.0f, 0.4f);       // 5 a boat's engine overhead (a throb)
    gMfBedsReady = true;
}
// the world's sounds by chance: gulls at the surface over the shallows, the reef's clicks, far whale song in the blue,
// creaks and groans in the trench, the orca pod's clicks, the boat's engine throb
void MfEvents(float dt) {
    const MfAudio& a = gMf.want;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.5f};
    if (a.band == 0 && a.depth < 6 && chance(0.25f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(1000, 1400), 0, RR(0.3f, 0.6f), 0.006f); v.f1 = v.f0 * 0.75f; v.fa0 = 1300; v.fa1 = 1000; v.fq = 4; v.vibR = 8; v.vibD = 0.05f; v.cut0 = v.cut1 = 1800; v.decPow = 1.4f; v.send = 0.7f; }   // gulls, muffled through the surface
    if (a.band == 1 && chance(2.5f)) { PanGains(RR(-1, 1), c.gl, c.gr); Voice& v = Puff(c, RR(4000, 7000), 3000, 0.012f, 0.012f); v.hp = 2500; v.decPow = 3; }   // a shrimp's snap
    if (a.band == 3 && chance(0.03f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(180, 320), 0, 3.0f, 0.012f); v.f1 = v.f0 * RR(1.2f, 1.6f); v.atk = 0.6f; v.vibR = 4; v.vibD = 0.03f; v.decPow = 1.1f; v.send = 1.0f; }   // a far whale
    if (a.band == 4 && chance(0.12f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(40, 70), 0, 2.2f, 0.02f); v.f1 = v.f0 * 0.8f; v.cut0 = 200; v.cut1 = 120; v.atk = 0.5f; v.decPow = 1.2f; v.send = 0.9f; }   // the trench groans
    if (gMf.orcaS > 0.05f) {   // the orca pod: bursts of clicks and a whistle
        gMf.clickT += dt;
        if (gMf.clickT > RR(0.4f, 1.2f)) { gMf.clickT = 0; float pan = RR(-0.8f, 0.8f); int n = 6 + (int)(R01() * 10); for (int k = 0; k < n; k++) { Ctx kk = c; PanGains(pan, kk.gl, kk.gr); Voice& v = Puff(kk, 6000, 4000, 0.006f, 0.03f * gMf.orcaS, k * RR(0.02f, 0.05f)); v.hp = 3000; v.decPow = 3; }
            if (R01() < 0.3f) { Ctx kk = c; PanGains(pan, kk.gl, kk.gr); Voice& w = Tone(kk, W_SINE, RR(5000, 7000), RR(7000, 9000), 0.6f, 0.01f * gMf.orcaS); w.curve = 0.6f; w.decPow = 1.2f; w.send = 0.6f; } }
    }
}
void MfTick() {
    const MfAudio& a = gMf.want;
    int t = gMf.tick;                     // eighths in 4/4
    float td = 30.0f / gMf.bpm;
    int e = t % 8, bar = t / 8;
    float th = gMf.themeS * (1 - 0.7f * gMf.deepS) * (1 - 0.6f * gMf.apexS) * (1 - 0.8f * gMf.orcaS) * (a.dead ? 0.4f : 1.0f);
    int chord = MF_CHORDS[bar % 4];
    float dark = gMf.deepS;
    // the reef theme: a marimba bass and chords in the light, a pluck melody; in the deep the same tune on a pad, slower
    if (th > 0.02f) {
        if (e == 0 || e == 4) PlayInst(dark > 0.5f ? I_SUB_BASS : I_MARIMBA, MfNote(chord, dark > 0.5f ? -1 : 0), td * 3, 0.04f * th, -0.2f, 0.4f);
        if ((e == 2 || e == 6) && dark < 0.6f) { PlayInst(I_MARIMBA, MfNote(chord + 2, 1), td * 1.5f, 0.022f * th * (1 - dark), 0.2f, 0.6f); PlayInst(I_MARIMBA, MfNote(chord + 4, 1), td * 1.5f, 0.018f * th * (1 - dark), 0.3f, 0.6f); }
        if (e == 0 && dark > 0.2f) PlayInst(I_PAD, MfNote(chord, 0), td * 8, 0.03f * th * dark, 0, 0.3f);
        // the melody: the theme every other pass; the tier brings more of the band in
        int total = 0, i = 0, at = t % 64;
        for (; i < MF_THEME_N; i++) { if (total == at) break; total += MF_THEME[i][1]; }
        if (total == at && i < MF_THEME_N && MF_THEME[i][0] != -99 && (bar / 8) % 2 == 0) {
            float f = MfNote(MF_THEME[i][0], 1), dur = td * MF_THEME[i][1] * 0.9f;
            PlayInst(dark > 0.5f ? I_GLASS : I_PLUCK, f, dur, 0.03f * th, 0.1f, 0.5f);
            if (a.tier >= 4) PlayInst(I_FLUTE, MfNote(MF_THEME[i][0] + 2, 1), dur, 0.012f * th, -0.3f, 0.4f);
        }
        if (a.tier >= 3 && dark < 0.5f) { if (e % 2 == 0) PlayInst(I_SHAKER, 0, 0.08f, 0.012f * th, 0.4f, 0.5f); if (e == 4) PlayInst(I_HANDDRUM, 0, 0.2f, 0.02f * th, -0.3f, 0.4f); }
        if (a.tier >= 6 && e == 0) PlayInst(I_LOGDRUM, 0, 0.3f, 0.025f * th, 0, 0.4f);
    }
    // dusk: the night layer (a slow pad and a lone bell over the theme)
    if (gMf.nightS > 0.05f && e == 0 && bar % 2 == 0) { PlayInst(I_WARM_PAD, MfNote(chord, -1), td * 16, 0.03f * gMf.nightS, 0, 0.2f); if (R01() < 0.4f) PlayInst(I_BELL, MfNote(chord + 4, 1), 3, 0.012f * gMf.nightS, RR(-0.5f, 0.5f), 0.3f); }
    // an apex shark within 40 m: its two notes, low, over and over
    if (gMf.apexS > 0.05f && (e == 0 || e == 3)) PlayInst(I_SAW_BASS, e == 0 ? MfNote(0, -2) : MfNote(0, -2) * 1.0595f, td * 2.5f, 0.06f * gMf.apexS, a.apexPan, 0.3f);
    // the orca pod: a hunting ostinato in the low strings under its clicks
    if (gMf.orcaS > 0.05f && e % 2 == 0) { static const int O[4] = {0, 0, 1, -1}; PlayInst(I_SAW_BASS, MfNote(O[(e / 2) % 4], -2), td * 1.6f, 0.05f * gMf.orcaS, 0, 0.35f); if (e == 0) PlayInst(I_LOGDRUM, 0, 0.3f, 0.04f * gMf.orcaS, 0, 0.3f); }
    // the crown held: a slow drum
    if (gMf.crownS > 0.05f && e == 0 && bar % 2 == 0) PlayInst(I_KICK, 0, 0.5f, 0.05f * gMf.crownS, 0, 0.2f);
    // the blobfish's theme: four notes, minor, whenever one is on screen
    if (gMf.blobS > 0.1f && bar % 4 == 0 && e % 2 == 0) { static const float B[4] = {0, 3, 2, -2}; PlayInst(I_SQUEEZE, MF_ROOT * 2 * powf(2.0f, B[e / 2] / 12.0f), td * 1.8f, 0.03f * gMf.blobS, 0.2f, 0.3f); }
}
void MfUpdate(float blockT) {
    const MfAudio& a = gMf.want;
    gMf.s += ((a.on ? 1.0f : 0.0f) - gMf.s) * std::min(1.0f, blockT * 0.8f);
    if (gMf.s < 0.002f && !a.on) return;
    if (!gMfBedsReady) MfSetupBeds();
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gMf.themeS, a.over ? 0.0f : 1.0f, 0.5f);
    toward(gMf.deepS, std::clamp((a.depth - 20) / 120.0f, 0.0f, 1.0f), 0.5f);
    toward(gMf.nightS, a.dusk, 0.3f);
    toward(gMf.apexS, a.apex, 1.5f);
    toward(gMf.orcaS, a.orcas, 1.0f);
    toward(gMf.crownS, a.crown ? 1.0f : 0.0f, 0.6f);
    toward(gMf.blobS, a.blobfish ? 1.0f : 0.0f, 1.0f);
    float want = 92 + 5 * a.tier - 14 * gMf.deepS;   // (quicker with your tier, slower down deep)
    gMf.bpm += (want - gMf.bpm) * std::min(1.0f, blockT * 0.5f);
    gMf.tickT += blockT;
    float td = 30.0f / gMf.bpm;
    while (gMf.tickT >= td) { gMf.tickT -= td; MfTick(); gMf.tick++; }
    // high tide: a countdown tick in the last ten seconds, a soft one each second before that
    if (a.highTide > 0) { int sec = (int)ceilf(a.highTide); if (sec != gMf.lastHigh) { gMf.lastHigh = sec; Ctx u{1, 1, 1, 1, B_UI, 0.2f}; Voice& v = Tone(u, W_SINE, sec <= 10 ? 1760.0f : 1320.0f, 0, 0.06f, sec <= 10 ? 0.04f : 0.012f); v.decPow = 2; } }
    else gMf.lastHigh = -1;
    // the end of the round: a fanfare for a win, a wet little gulp of a chord for the rest
    if (a.over) { if (!gMf.resultPlayed) { gMf.resultPlayed = 1;
        if (a.over == 1) { const int F[5] = {0, 4, 7, 9, 11}; for (int k = 0; k < 5; k++) PlayInst(I_SYNTH_LEAD, MfNote(F[k], 1), 0.5f + (k == 4 ? 1.0f : 0.0f), 0.04f, -0.3f + 0.15f * k, 0.6f); PlayInst(I_SUB_BASS, MfNote(0, -1), 3, 0.05f, 0, 0.3f); }
        else { PlayInst(I_PAD, MfNote(0, 0), 3, 0.03f, 0, 0.3f); PlayInst(I_PAD, MfNote(3, 0) * 0.94f, 3, 0.025f, 0, 0.3f); } } }
    else gMf.resultPlayed = 0;
    // the beds by band: surf in the shallows, the reef's crackle, the blue's quiet, the trench's drone and vents; a boat overhead
    if (gMfBeds.size() >= 6) {
        float s = gMf.s;
        gMfBeds[0].gain = (a.band == 0 ? 0.035f * std::clamp(1 - a.depth / 15, 0.2f, 1.0f) : 0.0f) * s;
        gMfBeds[1].gain = (a.band == 1 ? 0.012f : a.band == 2 ? 0.005f : 0.0f) * s;
        gMfBeds[2].gain = (a.band == 3 || a.band == 2 ? 0.03f : 0.008f) * s;
        gMfBeds[3].gain = (a.band == 4 ? 0.05f : 0.0f) * s;
        gMfBeds[4].gain = (a.band == 4 ? 0.012f : 0.0f) * s;
        gMfBeds[5].gain = 0.04f * a.boat * s;
    }
    gMf.ambT += blockT;
    if (gMf.ambT >= 0.05f) { MfEvents(gMf.ambT); gMf.ambT = 0; }
}
float MfBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gMfBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
// the effects: bites, swallows, abilities, the dangers, the crown; pitch: lower for bigger mouths
void MfCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.25f, 3.0f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f};
    PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case MFC_SNAP: { Voice& v = Puff(c, 3500 * p, 1500 * p, 0.05f, 0.08f); v.hp = 600; v.decPow = 3; Thud(c, 300 * p, 0.03f); break; }   // a miss: the jaws close on water
        case MFC_CRUNCH: { Thud(c, 140 * p, 0.1f); for (int k = 0; k < 4; k++) { Voice& v = Puff(c, RR(1200, 3000) * p, 600, 0.04f, 0.06f, k * 0.02f); v.decPow = 2.5f; } Voice& w = Puff(c, 900 * p, 300, 0.2f, 0.04f, 0.05f); w.decPow = 1.6f; break; }   // a hit: wet
        case MFC_GULP: { Voice& v = Tone(c, W_SINE, 320 * p, 110 * p, 0.25f + 0.2f / p, 0.12f); v.curve = 0.7f; v.decPow = 1.6f; for (int k = 0; k < 4; k++) Bubble(c, 0.08f + k * 0.06f, RR(0.5f, 1.2f) / p); break; }   // a swallow (bigger with the mass)
        case MFC_CHOMP: { Thud(c, 60, 0.25f); Voice& v = Tone(c, W_SAW, 90, 50, 0.6f, 0.08f); v.cut0 = 600; v.cut1 = 200; v.decPow = 1.5f; for (int k = 0; k < 6; k++) Bubble(c, 0.1f + k * 0.07f, RR(1.5f, 3)); break; }   // the king's chomp
        case MFC_DASH: { Whoosh(c, 800 * p, 3000 * p, 0.3f, 0.07f); break; }   // the dash's whip
        case MFC_INK: { Voice& v = Puff(c, 600, 200, 0.4f, 0.12f); v.atk = 0.01f; v.decPow = 1.5f; for (int k = 0; k < 5; k++) Bubble(c, k * 0.04f, RR(0.6f, 1.4f)); break; }   // the ink's blurt
        case MFC_FRENZY: { Voice& v = Tone(c, W_SAW, 70 * p, 90 * p, 1.2f, 0.06f); v.tremR = 14; v.tremD = 0.7f; v.cut0 = 500; v.cut1 = 800; v.decPow = 1.2f; break; }   // the frenzy's thrum
        case MFC_CLAW: { Voice& v = Tone(c, W_SQR, 1800 * p, 900 * p, 0.03f, 0.08f); v.cut0 = v.cut1 = 5000; v.decPow = 3; Thud(c, 400 * p, 0.04f); break; }   // a claw's snap
        case MFC_SLAM: { Thud(c, 45, 0.3f); Voice& v = Puff(c, 900, 150, 0.8f, 0.12f); v.decPow = 1.4f; v.send = 0.6f; break; }   // the slam's thud
        case MFC_INTAKE: { Voice& v = Puff(c, 400, 1800, 0.45f, 0.08f); v.atk = 0.35f; v.decPow = 0.8f; Voice& w = Tone(c, W_SINE, 1400 * p, 2600 * p, 0.12f, 0.04f, 0.45f); w.decPow = 2; break; }   // the puffer's intake and squeak
        case MFC_POP: { Thud(c, 120, 0.2f); Voice& v = Puff(c, 4000, 600, 0.25f, 0.18f); v.decPow = 2.2f; for (int k = 0; k < 10; k++) Bubble(c, k * 0.03f, RR(0.4f, 1.0f)); break; }   // the detonation
        case MFC_HOOK: { Voice& v = Tone(c, W_FM, 2400, 2000, 0.25f, 0.05f); v.fmRatio = 3.1f; v.fmIndex = 2; v.decPow = 2; Thud(c, 200, 0.06f); break; }   // hooked: a metallic jerk
        case MFC_NET: { Whoosh(c, 400, 1600, 0.6f, 0.06f); Voice& v = Puff(c, 3000, 1500, 0.8f, 0.03f); v.hp = 1500; v.decPow = 1.2f; break; }   // the net's hiss
        case MFC_LEVIATHAN: { Voice& v = Tone(c, W_SAW, 36, 30, 4.5f, 0.22f); v.cut0 = 180; v.cut1 = 90; v.atk = 0.8f; v.vibR = 2; v.vibD = 0.02f; v.decPow = 0.9f; v.send = 1.0f; Thud(c, 28, 0.2f); break; }   // its single deep note
        case MFC_FANFARE: { Ctx u = c; u.bus = B_MUSIC; const int F[4] = {0, 4, 7, 12}; for (int k = 0; k < 4; k++) { Voice& v = Tone(u, W_SAW, MfNote(0, 1) * powf(2.0f, F[k] / 12.0f), 0, k == 3 ? 1.0f : 0.18f, 0.035f, k * 0.14f); v.f1 = v.f0; v.fa0 = 1200; v.fa1 = 1600; v.fq = 2; v.decPow = 1.2f; v.send = 0.6f; } break; }   // brass: the crown taken
        case MFC_CRASH: { Thud(c, 50, 0.25f); Voice& v = Puff(c, 7000, 2500, 1.6f, 0.14f); v.hp = 2500; v.decPow = 1.1f; v.send = 0.7f; break; }   // the king dies
        case MFC_RESPAWN: { for (int k = 0; k < 8; k++) Bubble(c, k * 0.05f, RR(0.3f, 0.8f)); Voice& v = Tone(c, W_SINE, 880, 1320, 0.3f, 0.03f, 0.2f); v.decPow = 1.8f; break; }   // a fry again
        case MFC_FORK: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SINE, 1000 + 300 * k, 0, 0.5f, 0.02f, k * 0.05f); v.f1 = v.f0 * 1.5f; v.decPow = 1.6f; v.send = 0.7f; } break; }   // the shimmer of a new form
        case MFC_SWIM: { Voice& v = Puff(c, 500 / p, 260 / p, 0.25f + 0.15f / p, 0.14f); v.atk = 0.06f; v.decPow = 1.6f; break; }   // a tail stroke: heavier with the tier
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
