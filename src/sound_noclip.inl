// ================================================================ NOCLIP (the Deep Arcade's Backrooms)
// Included inside sound.cpp's anonymous namespace after sound_fowl.inl. Design doc p. 37, "Sound": the hum (Level 0's
// fluorescent buzz) is the game's identity and changes per level; entities each have a tell; hallucinated sounds use
// the same voices. No score: the levels are the music. The portal's charge is the loudest thing in a level.
struct NcState {
    NcAudio want; float s = 0, humS = 0, portalS = 0, labS = 0, ambT = 0; int lastLevel = -1; bool lastSurface = true;
};
NcState gNc;
std::vector<Bed> gNcBeds; int gNcBedKey = -999;
void NcSetupBeds(int key) {
    gNcBeds.clear(); gNcBedKey = key;
    auto bed = [&](int type, float f, float q, float gain, float lfoR, float lfoD, float ampR, float ampD) { Bed b{type, f, q, gain, lfoR, lfoD, ampR, ampD}; gNcBeds.push_back(b); };
    switch (key) {
        case -1: bed(0, 240, 0.7f, 0.045f, 0.05f, 0.3f, 0.1f, 0.4f); bed(1, 2400, 0.8f, 0.012f, 0.3f, 0.4f, 0.9f, 0.6f); break;   // the Surface: an industrial estate at night
        case 0: bed(0, 1200, 0.9f, 0.006f, 0.02f, 0.1f, 0.05f, 0.2f); break;                                                      // the Lobby: a hum (below) and damp air
        case 1: bed(0, 300, 0.7f, 0.014f, 0.05f, 0.3f, 0.08f, 0.5f); break;                                                       // the warehouse: a big room's air
        case 2: bed(2, 4500, 1, 0.008f, 0, 0, 0.6f, 0.8f); bed(0, 180, 0.8f, 0.016f, 0.08f, 0.3f, 0.15f, 0.5f); break;          // pipes: hiss, the boiler's breath
        case 3: bed(3, 60, 1, 0.02f, 0, 0, 0.5f, 0.2f); bed(0, 900, 0.9f, 0.01f, 0.2f, 0.4f, 3.0f, 0.3f); break;                // the station: transformer roar, machinery
        case 4: bed(0, 500, 0.7f, 0.012f, 0.03f, 0.2f, 0.06f, 0.3f); break;                                                      // the office: HVAC
        case 5: bed(1, 900, 0.8f, 0.006f, 0.2f, 0.4f, 0.5f, 0.5f); break;                                                         // the hotel: a corridor's hush
        case 6: bed(3, 38, 1, 0.006f, 0, 0, 0.05f, 0.5f); break;                                                                 // Lights Out: near silence (and you)
        case 7: bed(0, 220, 0.8f, 0.08f, 0.07f, 0.4f, 0.11f, 0.7f); break;                                                       // the ocean
        case 8: bed(0, 260, 0.8f, 0.012f, 0.1f, 0.3f, 0.1f, 0.5f); break;                                                        // the caves
        case 9: bed(2, 4800, 1, 0.009f, 0, 0, 11, 0.95f); break;                                                                  // the suburbs: crickets
        case 10: bed(0, 900, 0.6f, 0.03f, 0.1f, 0.5f, 0.12f, 0.7f); break;                                                       // the wheat: wind
        case 11: bed(0, 400, 0.7f, 0.018f, 0.05f, 0.3f, 0.08f, 0.5f); break;                                                     // the city's empty air
        case 12: bed(3, 45, 1, 0.02f, 0, 0, 0.7f, 0.6f); break;                                                                   // the living house: a heartbeat-ish throb
        case 13: bed(1, 3200, 0.9f, 0.006f, 0.5f, 0.5f, 0.3f, 0.6f); break;                                                      // glass: a high ringing air
        case 14: bed(0, 600, 0.7f, 0.012f, 0.03f, 0.2f, 0.06f, 0.3f); break;                                                     // the hospital
        case 15: bed(3, 120, 1, 0.012f, 0, 0, 0.2f, 0.2f); break;                                                                 // the system's hum
        case 16: bed(0, 300, 0.7f, 0.012f, 0.05f, 0.3f, 0.07f, 0.4f); break;                                                     // the asylum
        case 17: bed(0, 200, 0.8f, 0.024f, 0.06f, 0.4f, 0.09f, 0.6f); bed(3, 50, 1, 0.012f, 0, 0, 0.1f, 0.3f); break;           // the carrier: the sea, the hull
        case 18: bed(0, 700, 0.6f, 0.01f, 0.05f, 0.2f, 0.1f, 0.3f); break;                                                       // memories: a warm room
        default: bed(0, 150, 0.8f, 0.02f, 0.04f, 0.4f, 0.05f, 0.6f); break;                                                      // the abyss's draught
    }
}
void NcEvents(float dt) {
    const NcAudio& a = gNc.want; auto chance = [&](float perSec) { return R01() < perSec * dt; };
    Ctx c{1, 1, 1, 1, B_AMB, 0.6f};
    if (a.surface) { if (chance(0.05f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, RR(60, 90), 0, 2.5f, 0.012f); v.cut0 = 300; v.cut1 = 200; v.atk = 0.8f; } return; }   // a far lorry
    int L = a.level;
    if ((L == 1 || L == 8 || L == 7) && chance(0.5f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, RR(1300, 2400), 0, 0.12f, RR(0.01f, 0.025f)); v.f1 = v.f0 * 0.55f; v.decPow = 2.3f; v.send = 0.9f; }   // drips
    if ((L == 2 || L == 17) && chance(0.15f)) BuildCue(FindCue("amb.creak"), RR(0.2f, 0.5f), RR(-0.8f, 0.8f));                                                // pipes clank, the hull groans
    if (L == 4 && chance(0.02f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_SINE, 440, 480, 0.4f, 0.01f, k * 0.6f); v.decPow = 1; } }   // a distant phone
    if (L == 5 && chance(0.1f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SQR, RR(220, 440) * (R01() < 0.5f ? 1 : 1.5f), 0, 0.3f, 0.006f); v.cut0 = v.cut1 = 1200; v.send = 1; }   // jazz from somewhere
    if (L == 9 && chance(0.01f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); for (int k = 0; k < 6; k++) { Voice& v = Tone(c, W_SINE, 880 + (k % 3) * 220, 0, 0.18f, 0.006f, k * 0.2f); v.decPow = 1.5f; v.send = 1; } }   // the ice cream truck, far off
    if (L == 9 && chance(0.02f)) { PanGains(RR(-0.9f, 0.9f), c.gl, c.gr); Voice& v = Tone(c, W_SAW, 420, 260, 0.12f, 0.01f); v.fa0 = 900; v.fa1 = 700; v.fq = 2.5f; v.send = 1; }   // a far dog
    if (L == 14 && chance(0.03f)) { PanGains(RR(-0.5f, 0.5f), c.gl, c.gr); Voice& v = Tone(c, W_SINE, 660, 660, 0.4f, 0.012f); v.decPow = 1.3f; Voice& w = Tone(c, W_SINE, 880, 880, 0.4f, 0.01f, 0.4f); w.decPow = 1.3f; }   // the PA's chime
    if (L == 6 && chance(0.4f)) { Voice& v = Puff(c, 900, 600, 0.9f, 0.014f); v.atk = 0.4f; v.decPow = 1; }   // your own breathing
}
void NcUpdate(float blockT) {
    const NcAudio& a = gNc.want;
    gNc.s += ((a.on ? 1.0f : 0.0f) - gNc.s) * std::min(1.0f, blockT * 0.8f);
    if (gNc.s < 0.002f && !a.on) return;
    int key = a.surface ? -1 : a.level; if (gNcBedKey != key) NcSetupBeds(key);
    auto toward = [&](float& x, float target, float rate) { x += (target - x) * std::min(1.0f, blockT * rate); };
    bool humLevel = !a.surface && (a.level == 0 || a.level == 4 || a.level == 14 || a.level == 15 || a.level == 1) && !a.overtime && !a.lightsOut && !a.dead;
    toward(gNc.humS, humLevel ? 1.0f : a.inLab && !a.surface ? 0.5f : 0.0f, 1.5f);
    toward(gNc.portalS, a.charge > 0 && !a.surface ? 0.3f + a.charge : 0.0f, 2.0f);
    // the hum: a mains buzz with its harmonics, refreshed in long swells (louder when sanity is low)
    gNc.ambT += blockT;
    if (gNc.ambT >= 0.05f) {
        float dt = gNc.ambT; gNc.ambT = 0; NcEvents(dt);
        static float humT = 0; humT -= dt;
        if (humT <= 0 && gNc.humS > 0.02f) { humT = 1.9f; Ctx u{1, 1, 1, 1, B_AMB, 0.1f}; float g = 0.008f * gNc.humS * (1 + std::clamp((70 - a.sanity) / 70.0f, 0.0f, 1.0f)); for (int k = 1; k <= 3; k++) { Voice& v = Tone(u, k == 2 ? W_SQR : W_SAW, 60.0f * k * 2, 60.0f * k * 2, 2.2f, g / k); v.atk = 0.15f; v.decPow = 0.4f; v.cut0 = v.cut1 = 1800; } }
        static float portT = 0; portT -= dt;
        if (portT <= 0 && gNc.portalS > 0.02f) { portT = 0.5f; Ctx u{1, 1, 1, 1, B_SFX, 0.4f}; float f = 70 + 200 * std::min(1.0f, gNc.portalS); Voice& v = Tone(u, W_SAW, f, f * 1.05f, 0.6f, 0.03f * std::min(1.2f, gNc.portalS)); v.cut0 = 400; v.cut1 = 900; v.atk = 0.1f; v.decPow = 0.6f; v.vibR = 6; v.vibD = 0.02f; }
    }
}
float NcBedSample(float tt, int sampleIndex) {
    float out = 0;
    for (auto& b : gNcBeds) {
        if (b.gain <= 0) continue;
        if ((sampleIndex & 63) == 0) b.flt.Set(b.type == 0 ? 0 : b.type == 1 ? 2 : 1, b.f * (1 + b.lfoD * sinf(TAU * b.lfoR * tt)), b.q);
        float s = b.flt.Run(Noise());
        out += s * b.gain * (1 - b.ampD * (0.5f + 0.5f * sinf(TAU * b.ampR * tt + b.f)));
    }
    return out;
}
void NcCueImpl(int kind, float vol, float pan, float pitch) {
    if (vol < 0.01f) return;
    float p = std::clamp(pitch, 0.4f, 2.5f);
    Ctx c{vol, 1, 1, 1, B_SFX, 0.3f}; PanGains(pan, c.gl, c.gr);
    switch (kind) {
        case NCC_TELL: { Voice& v = Tone(c, W_SINE, 900 * p, 1300 * p, 0.25f, 0.025f); v.vibR = 14; v.vibD = 0.08f; v.decPow = 1.4f; v.send = 0.8f; break; }   // a giggle / something
        case NCC_HOWL: { Voice& v = Tone(c, W_SAW, 300, 520, 1.4f, 0.04f); v.fa0 = 800; v.fa1 = 1200; v.fq = 3; v.vibR = 5; v.vibD = 0.04f; v.atk = 0.3f; v.decPow = 1; v.send = 0.9f; break; }
        case NCC_HURT: { Thud(c, 100, 0.1f); Voice& v = Puff(c, 1500, 400, 0.12f, 0.05f); v.decPow = 2; break; }
        case NCC_DEATH: { Voice& v = Tone(c, W_SAW, 140, 60, 1.2f, 0.05f); v.cut0 = 600; v.cut1 = 200; v.decPow = 1.1f; v.send = 0.8f; break; }
        case NCC_NOCLIP: { Voice& v = Tone(c, W_SAW, 900, 40, 1.6f, 0.05f); v.cut0 = 4000; v.cut1 = 200; v.decPow = 0.9f; Voice& w = Puff(c, 6000, 300, 1.4f, 0.05f); w.decPow = 1; w.send = 1; break; }   // falling through
        case NCC_DOOR: { Thud(c, 120, 0.08f); Voice& v = Tone(c, W_SQR, 180, 160, 0.3f, 0.01f); v.cut0 = v.cut1 = 800; v.decPow = 2; break; }
        case NCC_PORTAL: { for (int k = 0; k < 4; k++) { Voice& v = Tone(c, W_SINE, 220 * (k + 1), 440 * (k + 1), 1.4f, 0.03f / (k + 1)); v.atk = 0.2f; v.decPow = 1.1f; v.send = 1; } break; }
        case NCC_OVERTIME: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_SQR, 110, 108, 0.6f, 0.03f, k * 0.8f); v.cut0 = v.cut1 = 700; v.decPow = 1; } break; }   // the lights going out, three times
        case NCC_BELL: { for (int k = 0; k < 3; k++) { Voice& v = Tone(c, W_FM, 900, 900, 0.8f, 0.04f, k * 0.35f); v.fmRatio = 3.5f; v.fmIndex = 4; v.decPow = 1.5f; } break; }
        case NCC_CLICK: { Voice& v = Tone(c, W_SQR, 1600, 1600, 0.05f, 0.05f); v.cut0 = v.cut1 = 4000; v.decPow = 3; break; }
        case NCC_PICKUP: { Voice& v = Puff(c, 2000, 1000, 0.1f, 0.03f); v.decPow = 2; Thud(c, 200, 0.03f); break; }
        case NCC_STEPS: { for (int k = 0; k < 4; k++) { Voice& v = Puff(c, 900, 500, 0.05f, 0.03f, k * 0.45f); v.decPow = 2.5f; Thud(c, 90, 0.02f, k * 0.45f); } break; }   // footsteps behind you
        default: { Voice& v = Tone(c, W_SINE, 600, 600, 0.1f, 0.03f); v.decPow = 2; break; }
    }
}
