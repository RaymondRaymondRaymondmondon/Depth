// ================================================================ the Trawl (the Deep Arcade's fishing game)
// Included inside sound.cpp's anonymous namespace after sound_redtide.inl. Design doc, "Sound design": sound is
// information. Music by the session's state: a concertina shanty with harbour bells at the dock (a new verse after
// each met deadline), its refrain over the engine sailing out and home (slower at the night's end), a low drone and a
// lone fiddle on a quiet night, a taut rising string figure that follows the line's tension when a fish over 20 kg is
// on, silence when a Wake-sized threat is within 60 m (the first tell), a pounding drum and hull groans for the big
// three, a brass fanfare or a single church bell at the count. The Lagoon's bed: surf on the crest, insects on the
// atoll, gulls at dusk, drums on canoe nights. Effects for the gear, the boat, the guns and the threats' tells.
struct TwState {
    TwAudio want;
    float s = 0, dockS = 0, sailS = 0, nightS = 0, fishS = 0, threatS = 0, bigS = 0, metS = 0, missS = 0;
    int tick = 0; float tickT = 0, bpm = 96;
    float chuffT = 0, creakT = 0, ambT = 0, humT = 0, drumT = 0, bellT = 0;
    int shantyAt = 0, lastVerse = -1, resultPlayed = 0;
};
TwState gTw;
std::vector<Bed> gTwBeds;
int gTwBedGround = -1;
const float TW_ROOT = 98.0f;   // G: the shanty's key
const int TW_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};   // major (the Lagoon); the other grounds will darken it
float TwNote(int deg, int oct) { int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7), d = deg - o * 7; return TW_ROOT * powf(2.0f, oct + o + TW_SCALE[d] / 12.0f); }
// the shanty, in 6/8: (degree, eighths); -99 a rest. A verse and the refrain (the second line), composed here.
const int TW_SHANTY[][2] = {{0, 2}, {0, 1}, {2, 2}, {4, 1}, {4, 2}, {2, 1}, {0, 3}, {-99, 1}, {4, 1}, {5, 1},
                            {6, 2}, {4, 1}, {2, 2}, {4, 1}, {1, 3}, {1, 3},
                            {4, 2}, {4, 1}, {7, 2}, {6, 1}, {4, 2}, {2, 1}, {0, 3}, {-99, 1}, {0, 1}, {1, 1},
                            {2, 2}, {4, 1}, {1, 2}, {-1, 1}, {0, 6}};
const int TW_SHANTY_N = (int)(sizeof TW_SHANTY / sizeof TW_SHANTY[0]);
const int TW_REFRAIN_FROM = 16;   // the refrain starts at the 17th note

void TwSetupBeds(int ground) {
    gTwBeds.clear();
    gTwBedGround = ground;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gTwBeds.push_back(b); };
    // surf on the crest (slow swells of filtered noise), the night wind, the engine's rumble (gain set live), water in her
    bed(0, 420, 0.6f, 0.045f, 0.05f, 0.4f, 0.07f, 0.8f);
    bed(1, 1800, 1.2f, 0.012f, 0.03f, 0.5f, 0.11f, 0.5f);
    bed(3, 36, 1, 0.0f, 0, 0, 0.3f, 0.3f);
    bed(0, 160, 1.5f, 0.0f, 0.3f, 0.3f, 0.9f, 0.7f);
}
void TwEvents(float dt) {
    const TwAudio& a = gTw.want;
    auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.6f};
    float night = gTw.nightS + gTw.sailS;
    // insects on the atoll (near the quay, loud; at sea, a far shimmer), gulls at dusk, a far whale now and then
    if (chance(a.moored ? 3.0f : 0.6f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SQR, RR(3800, 5200), 0, RR(0.08f, 0.2f), a.moored ? 0.006f : 0.002f); v.f1 = v.f0 * 1.02f; v.cut0 = v.cut1 = 6000; v.tremR = RR(18, 30); v.tremD = 0.8f; v.decPow = 1; }
    if (night > 0.3f && a.clock < 0.12f && chance(0.5f)) { PanGains(RR(-0.8f, 0.8f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(1100, 1500), 0, 0.5f, 0.012f); v.f1 = v.f0 * 0.7f; v.fa0 = 1200; v.fa1 = 900; v.fb0 = 2600; v.fb1 = 2200; v.fq = 4; v.vibR = 7; v.vibD = 0.04f; v.curve = 0.6f; v.decPow = 1.5f; v.send = 0.8f; }
    if (a.gulls > 0.1f && chance(1.2f * a.gulls)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(1000, 1400), 0, RR(0.3f, 0.6f), 0.02f * a.gulls); v.f1 = v.f0 * 0.75f; v.fa0 = 1300; v.fa1 = 1000; v.fb0 = 2800; v.fb1 = 2400; v.fq = 4; v.vibR = 8; v.vibD = 0.05f; v.curve = 0.5f; v.decPow = 1.4f; v.send = 0.7f; }
    if (night > 0.3f && chance(0.02f)) { PanGains(RR(-0.5f, 0.5f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(180, 260), 0, 2.5f, 0.012f); v.f1 = v.f0 * 1.6f; v.atk = 0.8f; v.decPow = 1.2f; v.send = 0.9f; }   // a whale, far off
    // rain and wind by the weather
    if (a.weather >= 2 && chance(a.weather >= 3 ? 25.0f : 10.0f)) { PanGains(RR(-1, 1), c.gl, c.gr); Voice& v = Puff(c, RR(3000, 6000), 2000, 0.02f, 0.006f); v.hp = 1500; }
    if (a.weather >= 3 && chance(0.4f)) { PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Puff(c, 500, 1200, RR(1.5f, 3), 0.02f); v.atk = 0.8f; v.decPow = 1; }   // a gust
    // a barracuda pack: fast ticks on the hull mic
    if (a.barracuda > 0.1f && chance(6.0f * a.barracuda)) { Ctx h = c; h.bus = B_SFX; PanGains(RR(-0.7f, 0.7f), h.gl, h.gr); Voice& v = Tone(h, W_SQR, RR(2400, 3200), 0, 0.012f, 0.03f * a.barracuda); v.f1 = v.f0 * 0.8f; v.cut0 = v.cut1 = 5000; v.decPow = 3; }
    // canoe night: drums carried over the water, closer alongside
    if (a.canoe > 0) {
        gTw.drumT += dt;
        float per = a.canoe == 2 ? 0.42f : 0.5f, g = a.canoe == 2 ? 0.05f : 0.022f;
        if (gTw.drumT >= per) { gTw.drumT = 0; int k = (int)(R01() * 3); PlayInst(I_LOGDRUM, TwNote(k == 0 ? 0 : k == 1 ? 2 : -3, 0), 0.3f, g, a.canoe == 2 ? 0.5f : 0.7f, 0.4f); if (a.canoe == 2 && R01() < 0.3f) PlayChant(TwNote(0, 2), 1 + (int)(R01() * 3), 0.4f, 0.014f, 0.5f); }
    } else gTw.drumT = 0;
}
void TwShantyNote(int i, float td, float gain, bool squeeze, bool fiddle, bool bells) {
    int deg = TW_SHANTY[i][0], len = TW_SHANTY[i][1];
    if (deg == -99) return;
    float f = TwNote(deg, 2), dur = td * len * 0.9f;
    if (squeeze) PlayInst(I_SQUEEZE, f, dur, gain, 0.1f, 0.5f);
    if (fiddle) PlayInst(I_FIDDLE, f, dur * 1.1f, gain * 0.8f, -0.3f, 0.4f);
    if (bells && (i == 0 || i == TW_REFRAIN_FROM)) PlayInst(I_BELL, TwNote(0, 3), 1.6f, gain * 0.8f, 0.3f, 0.3f);
}
void TwTick() {
    const TwAudio& a = gTw.want;
    int t = gTw.tick;                              // eighths in 6/8
    float td = 60.0f / gTw.bpm / 2;                // an eighth
    int step = t % 6, bar = t / 6;
    float quiet = 1 - gTw.threatS;                 // a Wake-sized threat near: the music stops
    float dm = gTw.dockS * quiet, sm = gTw.sailS * quiet, nm = gTw.nightS * quiet;
    // ---- the shanty: at the dock the whole tune (a verse more each met deadline: the fiddle joins, then the bells);
    // sailing, the refrain alone over the engine
    if (dm > 0.02f || sm > 0.02f) {
        int total = 0, i = 0, at = t % 48;
        for (; i < TW_SHANTY_N; i++) { if (total == at) break; total += TW_SHANTY[i][1]; }
        bool refrainOnly = sm > dm;
        if (total == at && i < TW_SHANTY_N && (!refrainOnly || i >= TW_REFRAIN_FROM)) {
            float g = 0.032f * std::max(dm, sm);
            TwShantyNote(i, td, g, true, a.verse >= 1 || refrainOnly, a.verse >= 2 && !refrainOnly);
        }
        if (step == 0) { PlayInst(I_SUB_BASS, TwNote(bar % 4 == 2 ? 4 : bar % 4 == 3 ? 3 : 0, 1), td * 5, 0.035f * std::max(dm, sm), 0, 0.3f); }
        if (step == 3) PlayInst(I_HANDDRUM, 0, 0.2f, 0.02f * std::max(dm, sm), -0.2f, 0.4f);
        if (dm > 0.02f && step == 0 && bar % 8 == 0) PlayInst(I_BELL, TwNote(4, 3), 2.5f, 0.02f * dm, 0.5f, 0.3f);   // the harbour bells
    }
    // ---- a quiet night: a low drone, and now and then a fragment of the shanty on a lone fiddle
    if (nm > 0.02f) {
        if (step == 0 && bar % 4 == 0) { PlayInst(I_DRONE, TwNote(0, 0), td * 26, 0.045f * nm, -0.2f, 0.25f); PlayInst(I_DRONE, TwNote(4, 0), td * 26, 0.022f * nm, 0.2f, 0.25f); }
        if (step == 0 && bar % 8 == 4 && R01() < 0.5f) gTw.shantyAt = (bar / 8) % 2 ? TW_REFRAIN_FROM : 0;
        if (gTw.shantyAt >= 0 && gTw.shantyAt < TW_SHANTY_N && step % 2 == 0) {
            int i = gTw.shantyAt++;
            if (TW_SHANTY[i][0] != -99) PlayInst(I_FIDDLE, TwNote(TW_SHANTY[i][0], 1), td * 2 * TW_SHANTY[i][1] * 1.2f, 0.022f * nm, -0.3f, 0.35f);
            if (i == TW_REFRAIN_FROM - 1 || i == TW_SHANTY_N - 1) gTw.shantyAt = -1;
        }
    } else gTw.shantyAt = -1;
    // ---- fish on: a taut string figure that climbs with the tension
    if (gTw.fishS > 0.05f && step % 2 == 0) {
        float rise = powf(2.0f, a.tension * 7 / 12.0f);
        static const int FIG[3] = {0, 2, 4};
        Bowed(TwNote(FIG[(t / 2) % 3], 2) * rise, td * 2.2f, 0.028f * gTw.fishS * quiet, 0.2f);
        if (a.tension > 0.7f && step == 0) Bowed(TwNote(6, 2) * rise, td * 4, 0.02f * gTw.fishS * quiet, -0.2f);
    }
    // ---- the big three: a pounding low drum and a hull-groan pulse
    if (gTw.bigS > 0.02f) {
        if (step == 0 || step == 3) { PlayInst(I_KICK, 0, 0.35f, 0.07f * gTw.bigS, 0, 0.3f); PlayInst(I_SUB_BASS, TwNote(0, 0), td * 2.5f, 0.05f * gTw.bigS, 0, 0.2f); }
        if (step == 0 && bar % 2 == 1) Groan(RR(50, 80), 0.05f * gTw.bigS, RR(-0.6f, 0.6f));
    }
}
void TwUpdate(float blockT) {
    const TwAudio& a = gTw.want;
    gTw.s += ((a.on ? 1.0f : 0.0f) - gTw.s) * std::min(1.0f, blockT * 0.8f);
    if (gTw.s < 0.002f && !a.on) return;
    if (gTwBedGround != a.ground) TwSetupBeds(a.ground);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    toward(gTw.dockS, a.mode == 0 ? 1.0f : 0.0f, 0.7f);
    toward(gTw.sailS, a.mode == 1 ? 1.0f : 0.0f, 0.7f);
    toward(gTw.nightS, a.mode == 2 && !a.fishOn && !a.bigThree ? 1.0f : 0.0f, 0.4f);
    toward(gTw.fishS, a.mode == 2 && a.fishOn && !a.bigThree ? 1.0f : 0.0f, 1.5f);
    toward(gTw.threatS, std::clamp(a.threat, 0.0f, 1.0f), 2.0f);
    toward(gTw.bigS, a.bigThree ? 1.0f : 0.0f, 1.2f);
    float want = a.mode == 0 ? 100.0f : a.mode == 1 ? (a.homeward > 0.5f ? 78.0f - 14 * a.clock : 92.0f) : 84.0f;
    gTw.bpm += (want - gTw.bpm) * std::min(1.0f, blockT * 0.5f);
    gTw.tickT += blockT;
    float td = 60.0f / gTw.bpm / 2;
    while (gTw.tickT >= td) { gTw.tickT -= td; TwTick(); gTw.tick++; }
    // the count: a brass fanfare on the chalkboard, or a single church bell
    if (a.mode == 3 || a.mode == 4) {
        if (!gTw.resultPlayed) {
            gTw.resultPlayed = 1;
            if (a.mode == 3) { const int F[5] = {0, 4, 7, 9, 11}; for (int k = 0; k < 5; k++) { Conch(TwNote(F[k] % 7, 1 + F[k] / 7), 0.55f, 0.05f, -0.3f + 0.15f * k); PlayInst(I_SQUEEZE, TwNote(F[k] % 7, 2 + F[k] / 7), 0.5f, 0.03f, 0.2f, 0.5f); } PlayInst(I_SUB_BASS, TwNote(0, 0), 2.5f, 0.05f, 0, 0.3f); }
            else PlayInst(I_BELL, TwNote(0, 1) * 0.5f, 6.0f, 0.09f, 0, 0.2f);
        }
    } else gTw.resultPlayed = 0;
    // the engine: chuffs by the telegraph (the bed's rumble follows), hull creaks with the roll, water in her
    if (!gTwBeds.empty()) {
        int tg = std::abs(a.telegraph);
        gTwBeds[2].gain = (tg == 0 ? 0.0f : 0.006f + 0.006f * tg) * gTw.s * (a.moored ? 0.3f : 1.0f);
        gTwBeds[3].gain = 0.03f * std::clamp(a.bilge, 0.0f, 1.0f) * gTw.s;
        gTwBeds[0].gain = (a.moored ? 0.02f : 0.045f + 0.01f * a.weather) * gTw.s;
        gTwBeds[1].gain = (0.006f + 0.006f * a.weather) * gTw.s;
        if (tg > 0) {
            gTw.chuffT += blockT;
            float per = tg == 1 ? 1.1f : tg == 2 ? 0.75f : 0.5f;
            if (gTw.chuffT >= per) { gTw.chuffT = 0; Ctx c{1, 1, 1, 1, B_AMB, 0.3f}; Voice& v = Puff(c, 260, 120, per * 0.5f, (0.02f + 0.012f * tg) * gTw.s); v.atk = 0.02f; v.decPow = 2; Thud(c, 48, 0.012f * gTw.s); }
        }
    }
    gTw.creakT += blockT;
    if (gTw.creakT > 0.5f && R01() < std::clamp((a.roll - 4) / 20.0f, 0.0f, 1.0f) * 0.6f) { gTw.creakT = 0; Ctx c{1, 1, 1, 1, B_AMB, 0.4f}; PanGains(RR(-0.6f, 0.6f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(90, 160), 0, RR(0.4f, 0.9f), 0.025f * gTw.s); v.f1 = v.f0 * RR(0.8f, 1.2f); v.cut0 = 400; v.cut1 = 900; v.noiseMix = 0.3f; v.tremR = 11; v.tremD = 0.5f; v.decPow = 1.5f; v.send = 0.5f; }
    gTw.ambT += blockT;
    if (gTw.ambT >= 0.05f) { TwEvents(gTw.ambT); gTw.ambT = 0; }
}
float TwBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gTwBeds) {
        if (b.gain <= 0) continue;
        float s;
        if (b.type == 3) { b.ph += b.f / SR; if (b.ph >= 1) b.ph -= 1; s = sinf(TAU * b.ph) + 0.6f * sinf(TAU * b.ph * 2) + 0.3f * sinf(TAU * b.ph * 3) + 0.2f * sinf(TAU * b.ph * 5); }
        else {
            if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
            s = b.flt.Run(Noise());
        }
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
// the effects: the gear, the boat, the guns, the threats' tells and the dock
void TwCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f};
    PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case TWC_REEL: { Voice& v = Tone(c, W_SQR, 1800 * p, 1200 * p, 0.015f, 0.05f); v.cut0 = v.cut1 = 4000; v.decPow = 3; Thud(c, 300 * p, 0.015f); break; }   // the ratchet, pitched by the tension
        case TWC_DRAG: { for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SQR, 2200 * p, 1900 * p, 0.012f, 0.03f, k * 0.03f); v.cut0 = v.cut1 = 5000; v.decPow = 3; } break; }   // the drag's zip
        case TWC_HUM: { Voice& v = Tone(c, W_SINE, 180 * p, 240 * p, 0.6f, 0.04f); v.atk = 0.2f; v.decPow = 1; v.vibR = 14; v.vibD = 0.01f; v.send = 0.5f; break; }   // the line singing before a snap
        case TWC_SNAP: { Voice& v = Puff(c, 4000, 800, 0.08f, 0.14f); v.decPow = 3; Voice& w = Tone(c, W_SINE, 900, 200, 0.12f, 0.06f); w.decPow = 2.5f; break; }
        case TWC_CREAK: { Voice& v = Tone(c, W_SAW, 220 * p, 260 * p, 0.35f, 0.03f); v.cut0 = 600; v.cut1 = 1200; v.noiseMix = 0.25f; v.tremR = 13; v.tremD = 0.6f; v.decPow = 1.5f; break; }   // the rod
        case TWC_SPLASH: { Voice& v = Puff(c, 1500, 300, 0.5f, 0.12f); v.decPow = 1.8f; for (int k = 0; k < 5; k++) Bubble(c, 0.1f + k * 0.06f, RR(0.8f, 1.6f)); break; }
        case TWC_GAFF: { Thud(c, 120, 0.14f); Voice& v = Puff(c, 900, 300, 0.1f, 0.06f); v.decPow = 2; break; }
        case TWC_FLOP: { for (int k = 0; k < 4; k++) { Thud(c, RR(90, 140), 0.07f, k * RR(0.12f, 0.22f)); Voice& v = Puff(c, 700, 300, 0.08f, 0.03f, k * 0.17f); v.decPow = 2; } break; }
        case TWC_BITE: { Voice& v = Tone(c, W_SINE, 700 * p, 500 * p, 0.04f, 0.03f); v.decPow = 2.5f; break; }   // the tip ticks
        case TWC_STRIKE: { Whoosh(c, 300, 900, 0.12f, 0.08f); Voice& v = Tone(c, W_SINE, 160, 120, 0.2f, 0.05f); v.decPow = 2; break; }   // the hook set
        case TWC_TELEGRAPH: { Voice& v = Tone(c, W_FM, 1400, 1400, 0.5f, 0.05f); v.fmRatio = 3.1f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2; v.send = 0.5f; Voice& w = Tone(c, W_FM, 1900, 1900, 0.4f, 0.03f, 0.12f); w.fmRatio = 3.1f; w.fmIndex = 2; w.decPow = 2; break; }   // ting-ting
        case TWC_VALVE: { Voice& v = Puff(c, 2500, 6000, 1.8f, 0.14f); v.atk = 0.05f; v.hold = 0.8f; v.decPow = 1.4f; Voice& w = Tone(c, W_SAW, 1800, 2300, 1.2f, 0.03f); w.cut0 = w.cut1 = 4000; w.decPow = 1.2f; break; }   // the relief valve's shriek
        case TWC_HULL: { Thud(c, 60, 0.18f); Voice& v = Tone(c, W_SAW, 110, 70, 0.6f, 0.05f); v.cut0 = 500; v.cut1 = 200; v.noiseMix = 0.3f; v.decPow = 1.6f; v.send = 0.6f; break; }   // a rock, a ram
        case TWC_PUMP: { Voice& v = Puff(c, 400, 900, 0.25f, 0.05f); v.atk = 0.08f; v.decPow = 1.5f; Thud(c, 80, 0.04f, 0.2f); for (int k = 0; k < 2; k++) Bubble(c, 0.25f + k * 0.08f, RR(1, 1.8f)); break; }
        case TWC_WINCH: { Voice& v = Tone(c, W_SQR, 95 * p, 95 * p, 0.3f, 0.025f); v.cut0 = v.cut1 = 700; v.tremR = 22 * p; v.tremD = 0.9f; v.decPow = 1; Voice& w = Puff(c, 800, 1600, 0.3f, 0.015f); w.decPow = 1; break; }
        case TWC_WARP: { Voice& v = Tone(c, W_SAW, 70, 55, 1.2f, 0.04f); v.cut0 = 300; v.cut1 = 160; v.noiseMix = 0.4f; v.tremR = 7; v.tremD = 0.6f; v.atk = 0.3f; v.decPow = 1.2f; v.send = 0.6f; break; }   // warps groaning under load
        case TWC_SNAG: { Voice& v = Tone(c, W_SAW, 600, 1400, 1.4f, 0.07f); v.cut0 = 1200; v.cut1 = 3000; v.noiseMix = 0.5f; v.atk = 0.1f; v.decPow = 1.3f; Thud(c, 50, 0.12f); break; }   // the warps shriek
        case TWC_CODEND: { Voice& v = Puff(c, 1200, 300, 1.0f, 0.12f); v.decPow = 1.6f; for (int k = 0; k < 8; k++) { Thud(c, RR(80, 160), 0.05f, 0.3f + k * RR(0.08f, 0.16f)); } break; }   // the pile on the deck
        case TWC_RIFLE: { Voice& v = Puff(c, 3000, 400, 0.25f, 0.3f); v.decPow = 2.5f; Thud(c, 90, 0.2f); Voice& w = Tone(c, W_SINE, 500, 150, 0.3f, 0.08f); w.decPow = 2; break; }
        case TWC_SHOTGUN: { Voice& v = Puff(c, 2000, 200, 0.4f, 0.35f); v.decPow = 2; Thud(c, 60, 0.3f); break; }
        case TWC_SPEAR: { Voice& v = Puff(c, 2500, 800, 0.08f, 0.12f); v.decPow = 2.5f; Whoosh(c, 400, 1400, 0.12f, 0.06f); break; }
        case TWC_HARPOON: { Thud(c, 70, 0.3f); Whoosh(c, 250, 1100, 0.25f, 0.12f); Voice& v = Tone(c, W_SINE, 1500, 2200, 0.8f, 0.025f, 0.1f); v.tremR = 30; v.tremD = 0.5f; v.decPow = 1.4f; break; }   // the cannon, then the tether's whine
        case TWC_CHARGE: { Thud(c, 35, 0.4f); Voice& v = Puff(c, 600, 80, 1.6f, 0.25f); v.decPow = 1.5f; for (int k = 0; k < 10; k++) Bubble(c, 0.3f + k * 0.07f, RR(1.5f, 3)); break; }   // felt through the hull
        case TWC_FLARE: { Voice& v = Puff(c, 1500, 3000, 0.4f, 0.1f); v.decPow = 2; Voice& w = Tone(c, W_SINE, 600, 1800, 0.5f, 0.03f); w.decPow = 1.5f; break; }
        case TWC_BUMP: { Thud(c, 45, 0.25f); Voice& v = Tone(c, W_SAW, 80, 50, 0.9f, 0.05f); v.cut0 = 300; v.cut1 = 120; v.noiseMix = 0.3f; v.decPow = 1.5f; v.send = 0.7f; break; }   // a shark against the hull: nothing, then this
        case TWC_TICKS: { for (int k = 0; k < 5; k++) { Voice& v = Tone(c, W_SQR, RR(2400, 3200), 0, 0.012f, 0.04f, k * RR(0.04f, 0.09f)); v.f1 = v.f0 * 0.8f; v.cut0 = v.cut1 = 5000; v.decPow = 3; } break; }
        case TWC_GULL: { Voice& v = Tone(c, W_SAW, RR(1000, 1400), 0, 0.5f, 0.05f); v.f1 = v.f0 * 0.75f; v.fa0 = 1300; v.fa1 = 1000; v.fb0 = 2800; v.fb1 = 2400; v.fq = 4; v.vibR = 8; v.vibD = 0.05f; v.curve = 0.5f; v.decPow = 1.4f; v.send = 0.6f; break; }
        case TWC_OVERBOARD: { Voice& v = Puff(c, 1200, 250, 0.7f, 0.18f); v.decPow = 1.6f; for (int k = 0; k < 8; k++) Bubble(c, 0.15f + k * 0.06f, RR(0.8f, 2)); break; }
        case TWC_RING: { Whoosh(c, 200, 600, 0.4f, 0.06f); Voice& v = Puff(c, 1000, 300, 0.3f, 0.06f, 0.5f); v.decPow = 2; break; }
        case TWC_BELL: { Voice& v = Tone(c, W_FM, 520, 520, 2.5f, 0.1f); v.fmRatio = 2.76f; v.fmIndex = 3; v.fmIndex1 = 0.3f; v.decPow = 1.4f; v.send = 0.9f; break; }   // the ship's bell
        case TWC_TAPE: { Ctx u = c; u.bus = B_UI; for (int k = 0; k < 6; k++) { Voice& v = Tone(u, W_SQR, 2200, 2200, 0.01f, 0.02f, k * 0.045f); v.cut0 = v.cut1 = 5000; v.decPow = 3; } Voice& w = Tone(u, W_FM, 2600, 2600, 0.5f, 0.03f, 0.3f); w.fmRatio = 2.5f; w.fmIndex = 1.5f; w.decPow = 2; break; }   // the printer, and its bell
        case TWC_SELL: { Ctx u = c; u.bus = B_UI; for (int k = 0; k < 4; k++) { Voice& v = Tone(u, W_FM, RR(2400, 3400), 0, 0.3f, 0.03f, k * 0.07f); v.f1 = v.f0; v.fmRatio = 3.7f; v.fmIndex = 2; v.fmIndex1 = 0.2f; v.decPow = 2.2f; } break; }   // shillings on the counter
        case TWC_FANFARE: { const int F[5] = {0, 4, 7, 9, 11}; for (int k = 0; k < 5; k++) Conch(TwNote(F[k] % 7, 1 + F[k] / 7), 0.6f, 0.05f, -0.3f + 0.15f * k); break; }
        case TWC_CHURCH: { PlayInst(I_BELL, TwNote(0, 1) * 0.5f, 6.0f, 0.09f, 0, 0.2f); break; }
        case TWC_CANOE: { for (int k = 0; k < 4; k++) PlayInst(I_LOGDRUM, TwNote(k % 2 ? 2 : 0, 0), 0.3f, 0.05f, 0.5f, 0.4f * (k + 1) / 4.0f); PlayChant(TwNote(0, 2), 2, 0.5f, 0.02f, 0.5f); break; }
        case TWC_DEATH: { PlayInst(I_BELL, TwNote(0, 1), 4.0f, 0.06f, 0, 0.2f); Voice& v = Tone(c, W_SAW, 110, 55, 2.5f, 0.03f); v.cut0 = 600; v.cut1 = 150; v.decPow = 1.2f; v.send = 0.8f; break; }
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
