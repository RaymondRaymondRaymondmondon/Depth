// ================================================================ the Flight (the Deep Arcade's bird RTS)
// Included inside sound.cpp's anonymous namespace after sound_trawl.inl. Design doc p32, "Sound": the sea and the wind
// are the bed (surf by the shore's kind, wind rising with altitude, rigging on a town's boats, the cove breathing slowly
// while the kraken sleeps); a colony's chorus swells with its size, so a scout hears one before it sees it; chicks peep
// when they're hungry; a windy brass-and-strings theme layers with your colony's size, a war motif enters when flocks
// engage, a dawn chorus and a dusk hush mark each game day, and the kraken has a theme nobody wants to hear.
struct FlState {
    FlAudio want;
    float s = 0, themeS = 0, warS = 0, krakenS = 0, dawnS = 0, nightS = 0, hushS = 0, uwS = 0;
    int tick = 0; float tickT = 0, bpm = 84;
    float ambT = 0, rigT = 0, breathT = 0;
    int phrase = 0, resultPlayed = 0;
};
FlState gFl;
std::vector<Bed> gFlBeds;
bool gFlBedsReady = false;
const float FL_ROOT = 110.0f;   // A: the theme's key
const int FL_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};
float FlNote(int deg, int oct) { int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return FL_ROOT * powf(2.0f, oct + o + FL_SCALE[d] / 12.0f); }
// the theme, in 3/4 (degree, beats; -99 a rest): a long rising line, a turn at the top, a fall home. Composed here.
const int FL_THEME[][2] = {{0, 2}, {2, 1}, {4, 3}, {5, 2}, {4, 1}, {7, 3}, {6, 2}, {4, 1}, {5, 2}, {2, 1}, {4, 3},
                           {4, 2}, {5, 1}, {7, 2}, {9, 1}, {8, 3}, {7, 2}, {5, 1}, {4, 2}, {2, 1}, {0, 3}, {-99, 3}};
const int FL_THEME_N = (int)(sizeof FL_THEME / sizeof FL_THEME[0]);
const int FL_CHORDS[4] = {0, 5, 3, 4};   // a bar each: I, vi, IV, V

void FlSetupBeds() {
    gFlBeds.clear();
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gFlBeds.push_back(b); };
    bed(0, 380, 0.6f, 0, 0.06f, 0.4f, 0.09f, 0.85f);   // 0 the open sea's swell
    bed(1, 900, 0.8f, 0, 0.13f, 0.5f, 0.2f, 0.6f);      // 1 the wind (gain and pitch by height and speed)
    bed(0, 1400, 0.7f, 0, 0.12f, 0.5f, 0.16f, 0.9f);    // 2 surf on the shore
    bed(0, 90, 1.2f, 0, 0.05f, 0.3f, 0.12f, 0.95f);     // 3 the cove breathing (a slow, vast swell)
    bed(1, 2600, 1.5f, 0, 0.4f, 0.4f, 0.35f, 0.7f);     // 4 rain and spray in a storm
    gFlBedsReady = true;
}
// the world's sounds by chance: the colony's chorus, hungry peeps, rigging at a town, gulls, thunder, the dawn chorus
void FlEvents(float dt) {
    const FlAudio& a = gFl.want;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.5f};
    float mute = 1 - 0.85f * gFl.uwS;
    auto chirp = [&](float f, float gain, float pan, float dur) { Ctx k = c; PanGains(pan, k.gl, k.gr); Voice& v = Tone(k, W_SINE, f, f * RR(1.15f, 1.5f), dur, gain * mute); v.curve = 0.5f; v.vibR = RR(18, 30); v.vibD = 0.04f; v.decPow = 1.6f; v.send = 0.5f; };
    // a colony's chorus: cries and calls, more of them the bigger and nearer it is
    if (a.chorus > 0.02f && chance(14.0f * a.chorus)) {
        PanGains(RR(-0.9f, 0.9f), c.gl, c.gr);
        Voice& v = Tone(c, W_SAW, RR(700, 1300), 0, RR(0.2f, 0.45f), 0.012f * (0.4f + a.chorus) * mute);
        v.f1 = v.f0 * RR(0.7f, 0.9f); v.fa0 = 1100; v.fa1 = 850; v.fb0 = 2500; v.fb1 = 2100; v.fq = 4; v.vibR = 9; v.vibD = 0.05f; v.curve = 0.5f; v.decPow = 1.4f; v.send = 0.7f;
    }
    // hungry chicks: thin peeps, insistent
    if (a.hungry > 0.05f && chance(6.0f * a.hungry)) for (int k = 0; k < 3; k++) chirp(RR(2600, 3400), 0.012f * a.hungry, RR(-0.4f, 0.4f), 0.06f);
    // a town's boats: rigging slapping masts, a block creaking
    if (a.town > 0.05f) {
        gFl.rigT += dt;
        if (gFl.rigT > RR(0.5f, 1.4f)) { gFl.rigT = 0; Ctx k = c; PanGains(RR(-0.7f, 0.7f), k.gl, k.gr); Voice& v = Tone(k, W_FM, RR(900, 1300), 0, 0.25f, 0.012f * a.town * mute); v.f1 = v.f0; v.fmRatio = 2.4f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2.4f; v.send = 0.5f;
                                       if (R01() < 0.3f) { Voice& w = Tone(k, W_SAW, RR(160, 240), 0, 0.5f, 0.012f * a.town * mute); w.f1 = w.f0 * 1.2f; w.cut0 = 500; w.cut1 = 900; w.tremR = 12; w.tremD = 0.6f; w.decPow = 1.4f; } }
    }
    // gulls along any shore by day
    if (a.surf > 0.2f && gFl.nightS < 0.5f && chance(0.35f * a.surf)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(1000, 1400), 0, RR(0.3f, 0.6f), 0.012f * a.surf * mute); v.f1 = v.f0 * 0.75f; v.fa0 = 1300; v.fa1 = 1000; v.fb0 = 2800; v.fb1 = 2400; v.fq = 4; v.vibR = 8; v.vibD = 0.05f; v.curve = 0.5f; v.decPow = 1.4f; v.send = 0.7f; }
    // the dawn chorus: the whole sea's birds at first light
    if (gFl.dawnS > 0.05f && chance(9.0f * gFl.dawnS)) { float f = RR(1800, 4200); int n = 1 + (int)(R01() * 4); float pan = RR(-1, 1); for (int k = 0; k < n; k++) { Ctx kk = c; PanGains(pan, kk.gl, kk.gr); Voice& v = Tone(kk, W_SINE, f, f * RR(0.8f, 1.4f), 0.07f, 0.008f * gFl.dawnS * mute, k * 0.09f); v.curve = 0.5f; v.decPow = 1.8f; v.send = 0.6f; } }
    // night: a far shearwater moaning, the odd splash
    if (gFl.nightS > 0.5f && chance(0.06f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(300, 420), 0, 1.4f, 0.01f * mute); v.f1 = v.f0 * 1.4f; v.fa0 = 600; v.fa1 = 800; v.fq = 5; v.atk = 0.4f; v.vibR = 6; v.vibD = 0.04f; v.decPow = 1.2f; v.send = 0.9f; }
    // a storm: thunder now and then
    if (a.storm && chance(0.07f)) { Voice& k = Puff(c, 2200, 300, 0.4f, 0.2f * mute); k.decPow = 2; for (int i = 0; i < 3; i++) { Voice& v = Puff(c, RR(250, 380), 70, 2.4f + i * 0.6f, (0.16f - i * 0.03f) * mute, 0.2f + i * RR(0.25f, 0.5f)); v.decPow = 1.1f; v.send = 0.9f; } }
}
void FlTick() {
    const FlAudio& a = gFl.want;
    int t = gFl.tick;                     // quarter notes in 3/4
    float td = 60.0f / gFl.bpm;
    int beat = t % 3, bar = t / 3;
    float hush = 1 - 0.7f * gFl.hushS;    // (the dusk hush and the night bring the theme down)
    float th = gFl.themeS * hush * (1 - 0.8f * gFl.krakenS) * (1 - 0.85f * gFl.uwS);
    float grow = std::clamp(a.colony / 60.0f, 0.0f, 1.0f);   // the colony's size: more of the band plays
    int chord = FL_CHORDS[bar % 4];
    // ---- strings: the bed of the theme, always (a long bowed chord a bar)
    if (th > 0.02f && beat == 0) {
        Bowed(FlNote(chord, 0), td * 3.4f, 0.024f * th, -0.3f);
        Bowed(FlNote(chord + 2, 0), td * 3.4f, 0.018f * th, 0.3f);
        if (grow > 0.25f) Bowed(FlNote(chord + 4, 1), td * 3.4f, 0.014f * th * grow, 0);
    }
    // ---- the bass: a pizzicato-ish root on the downbeat once the colony is under way
    if (th > 0.02f && grow > 0.08f && beat == 0) PlayInst(I_SUB_BASS, FlNote(chord, -1), td * 2.5f, 0.04f * th, 0, 0.3f);
    // ---- brass: the theme itself, from 12 birds; a second horn a sixth below from 40
    if (th > 0.02f && a.colony >= 12) {
        int total = 0, i = 0, at = t % 36;
        for (; i < FL_THEME_N; i++) { if (total == at) break; total += FL_THEME[i][1]; }
        if (total == at && i < FL_THEME_N && FL_THEME[i][0] != -99) {
            float f = FlNote(FL_THEME[i][0], 1), dur = td * FL_THEME[i][1] * 0.95f;
            Conch(f, dur, 0.03f * th, -0.15f);
            if (a.colony >= 40) Conch(FlNote(FL_THEME[i][0] - 2, 1), dur, 0.018f * th, 0.25f);
            if (a.colony >= 70) PlayInst(I_FLUTE, FlNote(FL_THEME[i][0], 2), dur, 0.016f * th, 0.4f, 0.5f);
        }
    } else if (th > 0.02f && beat == 1 && R01() < 0.5f) {
        PlayInst(I_FLUTE, FlNote(chord + 4 + (int)(R01() * 3), 1), td * 1.6f, 0.012f * th, 0.3f, 0.4f);   // (a small colony: a lone pipe over the strings)
    }
    // ---- war: drums and a low brass ostinato
    if (gFl.warS > 0.03f) {
        float w = gFl.warS * (1 - 0.6f * gFl.krakenS);
        if (beat == 0) PlayInst(I_KICK, 0, 0.3f, 0.06f * w, 0, 0.3f);
        PlayInst(I_SNARE, 0, 0.12f, (beat == 2 ? 0.035f : 0.018f) * w, 0.2f, 0.5f);
        static const int OST[6] = {0, 0, 3, 0, 4, 2};
        Conch(FlNote(OST[t % 6], 0), td * 0.9f, 0.026f * w, -0.2f);
    }
    // ---- the kraken: a theme nobody wants to hear (a slow tritone heave in the deep strings, a far moan)
    if (gFl.krakenS > 0.03f) {
        float k = gFl.krakenS;
        if (beat == 0 && bar % 2 == 0) { Bowed(FlNote(0, -1), td * 6, 0.05f * k, -0.4f); Bowed(FlNote(0, -1) * 1.414f, td * 6, 0.04f * k, 0.4f); }
        if (beat == 0 && bar % 4 == 1) Groan(RR(45, 70), 0.05f * k, RR(-0.5f, 0.5f));
        if (a.kraken >= 2 && beat == 0) PlayInst(I_KICK, 0, 0.6f, 0.07f * k, 0, 0.2f);
    }
}
void FlUpdate(float blockT) {
    const FlAudio& a = gFl.want;
    gFl.s += ((a.on ? 1.0f : 0.0f) - gFl.s) * std::min(1.0f, blockT * 0.8f);
    if (gFl.s < 0.002f && !a.on) return;
    if (!gFlBedsReady) FlSetupBeds();
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    float ph = a.dayPhase;
    bool night = ph < 0.18f || ph > 0.86f;
    toward(gFl.themeS, a.over ? 0.0f : 1.0f, 0.5f);
    toward(gFl.warS, std::clamp(a.war, 0.0f, 1.0f), 1.0f);
    toward(gFl.krakenS, a.kraken > 0 ? (a.kraken >= 2 ? 1.0f : 0.6f) : 0.0f, 0.6f);
    toward(gFl.dawnS, ph > 0.18f && ph < 0.28f ? 1.0f : 0.0f, 0.4f);
    toward(gFl.nightS, night ? 1.0f : 0.0f, 0.3f);
    toward(gFl.hushS, (ph > 0.74f && ph < 0.86f) || night ? 1.0f : 0.0f, 0.3f);
    toward(gFl.uwS, a.underwater ? 1.0f : 0.0f, 6.0f);
    float want = 84 + 18 * gFl.warS;
    gFl.bpm += (want - gFl.bpm) * std::min(1.0f, blockT * 0.5f);
    gFl.tickT += blockT;
    float td = 60.0f / gFl.bpm;
    while (gFl.tickT >= td) { gFl.tickT -= td; FlTick(); gFl.tick++; }
    // the end of a match: a horn call for a win, a single low bell for a loss
    if (a.over) { if (!gFl.resultPlayed) { gFl.resultPlayed = 1;
        if (a.over == 1) { const int F[5] = {0, 2, 4, 7, 9}; for (int k = 0; k < 5; k++) Conch(FlNote(F[k], 1), 0.9f - 0.1f * k, 0.05f, -0.3f + 0.15f * k); PlayInst(I_SUB_BASS, FlNote(0, -1), 3, 0.05f, 0, 0.3f); }
        else PlayInst(I_BELL, FlNote(0, 0) * 0.5f, 6, 0.08f, 0, 0.2f); } }
    else gFl.resultPlayed = 0;
    // the beds: the swell everywhere, the wind with height and speed (and a storm), surf near a shore, the cove's breath
    if (gFlBeds.size() >= 5) {
        float m = 1 - 0.75f * gFl.uwS;
        float hgt = std::clamp(a.altitude / 120.0f, 0.0f, 1.0f), spd = std::clamp(a.speed / 30.0f, 0.0f, 1.0f);
        gFlBeds[0].gain = 0.04f * (1 - 0.6f * hgt) * gFl.s * m + 0.03f * gFl.uwS * gFl.s;
        gFlBeds[1].gain = (0.006f + 0.02f * hgt + 0.014f * spd + 0.002f * a.wind + (a.storm ? 0.02f : 0.0f)) * gFl.s * m;
        gFlBeds[1].f = 600 + 900 * hgt + 600 * spd;
        float sf = a.surfType == 1 ? 700.0f : a.surfType == 2 ? 2200.0f : 1400.0f;   // (cliffs boom low, a reef hisses)
        gFlBeds[2].f = sf; gFlBeds[2].gain = 0.05f * a.surf * (1 - 0.5f * hgt) * gFl.s * m;
        gFlBeds[3].gain = 0.06f * a.cove * gFl.s * m;
        gFlBeds[4].gain = (a.storm ? 0.03f : 0.0f) * gFl.s * m;
    }
    gFl.ambT += blockT;
    if (gFl.ambT >= 0.05f) { FlEvents(gFl.ambT); gFl.ambT = 0; }
}
float FlBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gFlBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
// the effects: the Founder's flight and fishing, the colony, the war, the dangers, the reports
void FlCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f};
    PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case FLC_FLAP: { Voice& v = Puff(c, 700 / p, 300 / p, 0.14f, 0.05f); v.atk = 0.03f; v.decPow = 2; break; }   // a wingbeat
        case FLC_DIVE: { Voice& v = Tone(c, W_SINE, 900 * p, 2600 * p, 1.1f, 0.03f); v.curve = 1.6f; v.atk = 0.4f; v.decPow = 0.6f; Voice& w = Puff(c, 1200, 4000, 1.1f, 0.03f); w.atk = 0.6f; w.decPow = 0.8f; break; }   // the dive's whistle
        case FLC_SPLASH: { Voice& v = Puff(c, 1800, 300, 0.45f, 0.16f); v.decPow = 1.8f; Thud(c, 90, 0.08f); for (int k = 0; k < 6; k++) Bubble(c, 0.08f + k * 0.05f, RR(0.6f, 1.4f)); break; }
        case FLC_STRUGGLE: { for (int k = 0; k < 5; k++) { Voice& v = Puff(c, RR(600, 1400), 300, 0.12f, 0.05f, k * RR(0.08f, 0.15f)); v.decPow = 2; } break; }   // wings beating the water
        case FLC_SLAP: { for (int k = 0; k < 3; k++) Thud(c, RR(110, 160), 0.06f, k * RR(0.1f, 0.2f)); Voice& v = Puff(c, 900, 400, 0.06f, 0.04f); v.decPow = 2; break; }   // the fish slapping the nest
        case FLC_CALL: {   // the Founder's signature cry, by species (pitch)
            Voice& v = Tone(c, W_SAW, 1100 * p, 0, 0.55f, 0.05f); v.f1 = v.f0 * 0.72f; v.fa0 = 1200 * p; v.fa1 = 900 * p; v.fb0 = 2600 * p; v.fb1 = 2200 * p; v.fq = 4; v.vibR = 9; v.vibD = 0.05f; v.curve = 0.5f; v.decPow = 1.3f; v.send = 0.7f;
            Voice& w = Tone(c, W_SAW, 1250 * p, 0, 0.35f, 0.035f, 0.5f); w.f1 = w.f0 * 0.8f; w.fa0 = 1300 * p; w.fa1 = 1000 * p; w.fb0 = 2800 * p; w.fb1 = 2400 * p; w.fq = 4; w.vibR = 9; w.vibD = 0.05f; w.decPow = 1.4f; w.send = 0.7f; break; }
        case FLC_PEEP: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SINE, 2900 * p, 3400 * p, 0.07f, 0.04f, k * 0.12f); v.decPow = 1.8f; } break; }
        case FLC_WINGBEATS: { for (int k = 0; k < 6; k++) { Voice& v = Puff(c, 900, 350, 0.12f, 0.12f, k * 0.16f); v.atk = 0.03f; v.decPow = 2; } break; }   // a flock in formation
        case FLC_SHRIEK: { Voice& v = Tone(c, W_SAW, 2400 * p, 1500 * p, 0.7f, 0.06f); v.fa0 = 2400; v.fa1 = 1700; v.fb0 = 3600; v.fb1 = 3000; v.fq = 5; v.vibR = 14; v.vibD = 0.04f; v.decPow = 1.2f; v.send = 0.6f; Whoosh(c, 600, 2400, 0.7f, 0.05f); break; }   // a Striker's dive
        case FLC_HIT: { Thud(c, 140, 0.08f); for (int k = 0; k < 3; k++) { Voice& v = Puff(c, RR(1500, 3000), 800, 0.05f, 0.04f, k * 0.03f); v.decPow = 2.5f; } break; }   // a blow: feathers
        case FLC_FALL: { Voice& v = Tone(c, W_SAW, 900 * p, 400 * p, 0.6f, 0.04f); v.fa0 = 1000; v.fa1 = 700; v.fq = 4; v.decPow = 1.5f; v.send = 0.6f; break; }   // a bird falls, crying
        case FLC_NET: { Whoosh(c, 500, 2500, 0.18f, 0.08f); Voice& v = Puff(c, 3000, 1000, 0.08f, 0.06f, 0.15f); v.decPow = 2; break; }   // the net's whip
        case FLC_BOMB: { Thud(c, 40, 0.35f); Voice& v = Puff(c, 1400, 120, 1.4f, 0.28f); v.decPow = 1.6f; v.send = 0.6f; break; }   // the thud
        case FLC_BURN: { for (int k = 0; k < 10; k++) { Voice& v = Puff(c, RR(1500, 4000), 2000, 0.03f, 0.03f, k * RR(0.05f, 0.15f)); v.decPow = 2.5f; } Voice& w = Puff(c, 500, 700, 1.6f, 0.04f); w.atk = 0.3f; w.decPow = 1; break; }   // the hedge burning
        case FLC_SCREAM: { Voice& v = Tone(c, W_SAW, 1600 * p, 1100 * p, 1.0f, 0.06f); v.fa0 = 1500; v.fa1 = 1200; v.fb0 = 3200; v.fb1 = 2800; v.fq = 3; v.vibR = 18; v.vibD = 0.08f; v.decPow = 1.1f; v.send = 0.7f; break; }   // the Screamer
        case FLC_ROUT: { for (int k = 0; k < 7; k++) { Voice& v = Tone(c, W_SAW, RR(900, 1600), 0, RR(0.2f, 0.4f), 0.025f, k * RR(0.05f, 0.14f)); v.f1 = v.f0 * 0.7f; v.fa0 = 1200; v.fa1 = 900; v.fq = 4; v.decPow = 1.4f; v.send = 0.6f; } break; }   // a scatter of cries
        case FLC_ROAR: { Voice& v = Tone(c, W_SAW, 70, 48, 2.6f, 0.16f); v.fa0 = 260; v.fa1 = 180; v.fq = 3; v.noiseMix = 0.35f; v.vibR = 5; v.vibD = 0.05f; v.atk = 0.3f; v.decPow = 1.1f; v.send = 0.9f; Thud(c, 30, 0.3f); for (int k = 0; k < 10; k++) Bubble(c, 0.2f + k * 0.12f, RR(2, 4)); break; }   // the kraken surfaces
        case FLC_REPORT: { Ctx u = c; u.bus = B_UI; Voice& v = Tone(u, W_SAW, 1300, 0, 0.3f, 0.03f); v.f1 = 1000; v.fa0 = 1300; v.fa1 = 1000; v.fq = 4; v.decPow = 1.4f; for (int k = 0; k < 5; k++) { Voice& w = Puff(u, 4000, 2500, 0.04f, 0.025f, 0.35f + k * 0.06f); w.hp = 1800; w.decPow = 2; } break; }   // a call and a chalk scribble
        case FLC_MAP: { Ctx u = c; u.bus = B_UI; Voice& v = Puff(u, 3500, 1500, 0.3f, 0.05f); v.hp = 1200; v.atk = 0.05f; v.decPow = 1.5f; break; }   // the paper rustle
        case FLC_PEARL: { Ctx u = c; u.bus = B_UI; for (int k = 0; k < 3; k++) { Voice& v = Tone(u, W_FM, 2200 + 500 * k, 0, 0.5f, 0.03f, k * 0.08f); v.f1 = v.f0; v.fmRatio = 3.5f; v.fmIndex = 1.5f; v.decPow = 2; v.send = 0.6f; } break; }
        case FLC_ERUPT: { Thud(c, 28, 0.45f); for (int i = 0; i < 4; i++) { Voice& v = Puff(c, RR(200, 400), 60, 3.0f + i * 0.5f, 0.2f - i * 0.03f, i * 0.3f); v.decPow = 1.1f; v.send = 0.9f; } break; }   // the volcano
        case FLC_THUNDER: { Voice& k = Puff(c, 2400, 400, 0.35f, 0.3f); k.decPow = 2; for (int i = 0; i < 4; i++) { Voice& v = Puff(c, RR(260, 380), 70, 2.6f + i * 0.6f, 0.24f - i * 0.04f, 0.15f + i * RR(0.25f, 0.5f)); v.decPow = 1.1f; v.send = 0.9f; } Thud(c, 38, 0.35f); break; }
        case FLC_ROCK: { Whoosh(c, 200, 700, 0.6f, 0.08f); Thud(c, 60, 0.2f); break; }   // the ape's rock
        case FLC_BELL: { Voice& v = Tone(c, W_FM, 700 * p, 700 * p, 2.0f, 0.06f); v.fmRatio = 2.76f; v.fmIndex = 3; v.fmIndex1 = 0.3f; v.decPow = 1.4f; v.send = 0.9f; break; }   // the shrine's (or the wreck's) bell
        case FLC_LAND: { Voice& v = Puff(c, 500, 200, 0.2f, 0.06f); v.decPow = 2; Thud(c, 120, 0.03f); break; }
        case FLC_HATCH: { for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_SQR, RR(2400, 3600), 0, 0.01f, 0.03f, k * RR(0.05f, 0.12f)); v.f1 = v.f0 * 0.8f; v.cut0 = v.cut1 = 5000; v.decPow = 3; } Voice& w = Tone(c, W_SINE, 3000, 3600, 0.08f, 0.03f, 0.5f); w.decPow = 1.8f; break; }   // shell cracking, then a peep
        case FLC_EGG: { Voice& v = Tone(c, W_SINE, 1500, 1500, 0.4f, 0.03f); v.decPow = 2; v.send = 0.5f; break; }   // an egg laid
        case FLC_DEATH: { PlayInst(I_BELL, FlNote(0, 1), 4.0f, 0.05f, 0, 0.2f); Voice& v = Tone(c, W_SAW, 600, 220, 1.5f, 0.03f); v.fa0 = 900; v.fa1 = 500; v.fq = 4; v.decPow = 1.2f; v.send = 0.8f; break; }   // the Founder falls
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
// a colony's song (the Long Flight): a short phrase on a pentatonic scale from its seed, the species' pitch
void FlSongImpl(uint32_t seed, float pitch, float vol, float pan) {
    if (vol < 0.01f) return;
    Ctx c{vol, 1, 1, 1, B_MUSIC, 0.5f};
    PanGains(pan, c.gl, c.gr);
    uint32_t h = seed * 2654435761u + 7;
    auto nx = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
    static const int PENT[5] = {0, 2, 4, 7, 9};
    float t = 0; int deg = (int)(nx() * 5);
    for (int k = 0; k < 8; k++) {
        deg = std::clamp(deg + (int)(nx() * 5) - 2, -2, 9);
        int o = deg >= 0 ? deg / 5 : -1, d = ((deg % 5) + 5) % 5;
        float f = FL_ROOT * 2 * std::clamp(pitch, 0.5f, 2.0f) * powf(2.0f, o + PENT[d] / 12.0f), dur = nx() < 0.3f ? 0.5f : 0.25f;
        Voice& v = Tone(c, W_SAW, f, f, dur * 1.2f, 0.035f, t); v.fa0 = f * 1.5f; v.fa1 = f * 1.2f; v.fq = 3; v.vibR = 6; v.vibD = 0.03f; v.decPow = 1.2f; v.send = 0.6f;
        t += dur;
    }
}
