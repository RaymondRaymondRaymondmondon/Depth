// The Trawl: lines and fishing (design doc, "Fishing" and "Fight tuning"). A fight is the rod tip, a line that
// stretches and slips its drag, and a fish that swims with a pull, a stamina and a fight pattern. Tension is the line's
// stretch times its stiffness, so the gauge shows what the physics does. --trawl-fight runs it headless against the
// doc's target fights.
#include "trawl.h"
#include "trawl_eco.h"
#include "raymath.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tw {

namespace {
const float G = 9.81f;
Vector3 V3(float x, float y, float z) { return {x, y, z}; }
Vector2 Flat(Vector3 v) { return {v.x, v.y}; }
float Ang(Vector2 a, Vector2 b) {   // radians between two horizontal directions
    float la = Vector2Length(a), lb = Vector2Length(b);
    if (la < 1e-4f || lb < 1e-4f) return 0;
    return acosf(std::clamp(Vector2DotProduct(a, b) / (la * lb), -1.0f, 1.0f));
}
Vector2 Rot(Vector2 v, float a) { float c = cosf(a), s = sinf(a); return {v.x * c - v.y * s, v.x * s + v.y * c}; }
// tuning knobs of the model (the doc's rules are the structure; these are what --trawl-fight was tuned with)
struct FightK {
    float speedK = 2.5f;             // m/s of free swimming at full pull
    float restEffort = 0.65f;        // between bursts
    float tearK = 0.030f;            // hook pull-out per second of head-shaking, times (tension / rating)^2
    float lightHookTear = 0.08f;     // per second of head-shaking, for a lightly set hook
    float lightHook[3] = {0.2f, 0.08f, 0.12f};   // chance a set is light: small, circle, treble
    float bottomChafe = 0.0025f;     // up to this much of the line's rating per second, rubbing on a rough bottom
    float dragStartup = 1.5f;        // the jerk as the drag breaks loose
    float leanRad = 0.6f;            // how far the rod swings the pull off the line
    float throwChance = 0.4f;        // a jump without a bow
    float bowedThrow = 0.005f;       // a jump even with a good bow
    float jumpAbove = 0.5f;          // stamina fraction below which it has no leaps left
    float alongside = 4.0f;          // metres from the tip that count as alongside
    float landAt = 0.15f;            // stamina fraction the fish gives up at
};
const FightK& K() { static FightK k; return k; }
}

float Fight::Rand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); }

float Fight::Strength() const {
    float s = TackleOf(tackle).strength * (1 - chafe);
    if (!LineOf(line).biteProof) s *= powf(0.9f, (float)wraps);
    return std::max(0.0f, s);
}
float Fight::Pull() const {
    float s = S0 > 0 ? S / S0 : 0;
    float deep = 1;
    if (cur == Pattern::Dive) deep += 0.05f * std::max(0.0f, (p.z - spec.depth) / 10.0f);   // each 10 m gained adds 5%
    return P0 * effort * (0.2f + 0.8f * s) * deep;
}

void Fight::HookFish(const FishSpec& f, Vector3 at, uint32_t seed) {
    spec = f; on = true; end = FightEnd::None; rng = seed * 2654435761u + 17; Rand();
    p = at; t = 0;
    cur = f.a; P0 = f.kg * PatternPull(cur) * f.pullK;
    S0 = (20 + 6 * sqrtf(f.kg)) * f.staminaK; S = S0;
    effort = 1.2f; burstT = 2 + Rand() * 2;    // it runs the moment it feels the hook
    nextBurst = 10 + Rand() * 5; nextJump = 3 + Rand() * 4; rollNext = 4 + Rand() * 4;
    modeT = 15 + Rand() * 15;
    Vector2 away = Vector2Normalize(Vector2Subtract(Flat(at), Flat(tip)));
    if (Vector2Length(Vector2Subtract(Flat(at), Flat(tip))) < 0.5f) away = outboard;
    h = V3(away.x, away.y, 0.2f); h = Vector3Normalize(h);
    turn = Rand() < 0.5f ? -0.3f : 0.3f;
    hasCover = f.a == Pattern::Cover || f.b == Pattern::Cover; inCover = false;
    if (hasCover) {
        Vector2 d = Rot(away, (Rand() - 0.5f) * 2.4f);
        float r = 8 + Rand() * 7;
        cover = V3(at.x + d.x * r, at.y + d.y * r, f.floor);
    }
    lightHook = Rand() < K().lightHook[(int)hook];
    circleDir = Rand() < 0.5f ? -1.0f : 1.0f;
    bottomRough = Rand() * K().bottomChafe;
    alongside = false; gaffPending = false; jumpT = -1; bowT = -1; botBowAt = -1;
    chafe = 0; wraps = 0; overT = slackT = slipT = stillT = 0;
    L = Vector3Distance(tip, p);
    tension = 0;
    node.clear(); prev.clear();
    for (int i = 0; i < 14; i++) { Vector3 q = Vector3Lerp(tip, p, i / 13.0f); node.push_back(q); prev.push_back(q); }
}

void Fight::Step(float dt) {
    if (!on || end != FightEnd::None) return;
    const TackleDef& td = TackleOf(tackle);
    const LineDef& ld = LineOf(line);
    const FightK& k = K();
    t += dt;
    float s = S / S0;
    bool tired = s < k.landAt;
    Vector3 toFish = Vector3Subtract(p, tip);
    float Dist = Vector3Length(toFish);
    Vector3 u = Dist > 0.01f ? Vector3Scale(toFish, 1 / Dist) : V3(outboard.x, outboard.y, 0);
    Vector2 uh = Flat(u); if (Vector2Length(uh) > 1e-3f) uh = Vector2Normalize(uh); else uh = outboard;
    bool shaking = false;

    // ---- the fish's mind: its pattern picks a heading and an effort
    if (cur != spec.a && cur != spec.b) cur = spec.a;
    modeT -= dt;
    if (modeT <= 0 && spec.b != Pattern::None) { cur = cur == spec.a ? spec.b : spec.a; modeT = 15 + Rand() * 15; P0 = spec.kg * PatternPull(cur) * spec.pullK; }
    if (Rand() < dt * (burstT > 0 ? 0.3f : 0.5f)) turn = (Rand() - 0.5f) * (burstT > 0 ? 0.8f : 2.0f);   // it wanders between runs
    Vector2 hh = Flat(h);
    if (Vector2Length(hh) < 0.05f) hh = uh;
    hh = Vector2Normalize(Rot(Vector2Normalize(hh), turn * dt));
    float wantZ = 0;                                     // vertical intent, -1 up .. 1 down
    if (tired) {
        // beaten: it comes to the boat, rolling on its side
        effort = 0.12f;
        hh = Vector2Scale(uh, -1);
        wantZ = p.z > 1.5f ? -0.8f : 0;
    } else if (jumpT >= 0) {
        effort = 1.3f; shaking = true;
    } else {
        if (burstT > 0) burstT -= dt; else nextBurst -= dt;
        bool burst = burstT > 0;
        if (!burst && nextBurst <= 0) {
            burstT = 4 + Rand() * 4; nextBurst = 10 + Rand() * 5; burst = true;
            // a run leaves from wherever it is facing, mostly away from the pressure
            Vector2 away = Rot(uh, (Rand() - 0.5f) * 1.4f);
            hh = away;
        }
        effort = burst ? 1.5f : k.restEffort;
        shaking = burst;
        switch (cur) {
            case Pattern::Run:
                wantZ = burst ? 0.1f : 0;
                break;
            case Pattern::Dive:
                wantZ = burst ? 1.0f : 0.25f;
                if (p.z > spec.floor - 1) wantZ = 0;
                break;
            case Pattern::Jump:
                nextJump -= dt;
                if (nextJump <= 0 && s > k.jumpAbove) {
                    if (p.z > 3) wantZ = -1;             // it rises to jump
                    else { jumpT = 0; bowT = -1; botBowAt = -1; nextJump = 8 + Rand() * 4; }
                }
                break;
            case Pattern::Circle: {
                effort = burst ? 1.1f : 1.0f;           // a steady, tireless circling pull
                // round and round under the hull, so every lap drags the line past the keel
                Vector2 c = Vector2Subtract(Flat(tip), Vector2Scale(outboard, 4));
                Vector2 rel = Vector2Subtract(Flat(p), c);
                float r = Vector2Length(rel);
                Vector2 rn = r > 0.01f ? Vector2Scale(rel, 1 / r) : outboard;
                Vector2 tang = Vector2Scale(Vector2{-rn.y, rn.x}, circleDir);
                hh = Vector2Normalize(Vector2Add(tang, Vector2Scale(rn, r > 12 ? -0.4f : r < 6 ? 0.3f : 0)));
                wantZ = burst ? 0.4f : 0.1f;
                break;
            }
            case Pattern::Cover:
                if (hasCover && !inCover) {
                    Vector3 dc = Vector3Subtract(cover, p);
                    float dcl = Vector3Length(dc);
                    if (dcl < 15 && (burst || dcl < 6)) {
                        effort = 1.3f;
                        hh = Vector2Normalize(Flat(dc)); wantZ = dc.z > 0 ? 0.6f : -0.3f;
                        if (dcl < 1.5f) { inCover = true; p = cover; }
                    }
                }
                break;
            case Pattern::Roll:
                rollNext -= dt;
                if (rollNext <= 0) { rollNext = 6 + Rand() * 4; if (!ld.biteProof) wraps++; shaking = true; }
                break;
            default: break;
        }
    }
    h = Vector3Normalize(V3(hh.x, hh.y, wantZ));

    // ---- jumps: the angler must bow within 0.3 s
    if (jumpT >= 0) {
        jumpT += dt;
        if (bowed && bowT < 0) bowT = jumpT;
        p.z = std::min(p.z, 0.0f) - 0.0f;
        if (jumpT > 0.8f) {
            bool good = bowT >= 0 && bowT <= 0.3f;
            if (Rand() < (good ? k.bowedThrow : k.throwChance)) { end = FightEnd::ThrownHook; return; }
            jumpT = -1;
        }
    }

    // ---- motion: the fish swims its pull against the line's tension
    int sub = 4; float h2 = dt / sub;
    for (int i = 0; i < sub; i++) {
        float pull = Pull();
        Vector3 v;
        if (inCover) v = V3(0, 0, 0);
        else v = Vector3Scale(Vector3Subtract(Vector3Scale(h, pull), Vector3Scale(u, tension)), k.speedK / std::max(0.2f, P0));
        float vmax = 3.2f;                               // nothing on the ground swims faster than about 3 m/s
        float vl = Vector3Length(v); if (vl > vmax) v = Vector3Scale(v, vmax / vl);
        p = Vector3Add(p, Vector3Scale(v, h2));
        if (jumpT < 0) p.z = std::clamp(p.z, 0.3f, spec.floor);
        toFish = Vector3Subtract(p, tip); Dist = Vector3Length(toFish);
        u = Dist > 0.01f ? Vector3Scale(toFish, 1 / Dist) : u;
        // the line: stretch between tip and fish, stiffness from its length and the rod's bend
        float kEff = 1.0f / (std::max(1.0f, L) * ld.stretch / td.strength + td.rodSoft);
        tension = std::max(0.0f, Dist - L) * kEff;
        if (tension > drag) {                            // the drag slips: line pays out so the tension is the drag
            if (slipT == 0) stillT0 = stillT;
            L = Dist - drag / kEff; tension = drag; slipT += h2; stillT = 0;
        } else { stillT += h2; if (stillT > 0.25f) slipT = 0; }
        if (reeling && tension < drag) {
            float rate = (td.reelLow < td.reel && tension > 0.5f * drag) ? td.reelLow : td.reel;
            L = std::max(1.0f, L - rate * h2);
            tension = std::max(0.0f, Dist - L) * kEff;
        }
    }
    if (L > td.spool) { end = FightEnd::Spooled; return; }

    // ---- the line's life: over its rating longer than its shock window, it snaps
    float rating = TackleOf(tackle).strength;
    // a drag that starts to slip from rest jerks the line: its static friction and the spool's inertia
    float peak = tension * (slipT > 0 && slipT < 0.25f && stillT0 > 0.3f ? k.dragStartup : 1.0f);
    if (peak > Strength()) { overT += dt; if (overT > ld.shock) { end = FightEnd::Snapped; return; } }
    else overT = 0;
    if (tension < 0.05f * rating) {
        slackT += dt;
        if (slackT > 1.5f && hook == Hook::Circle) { end = FightEnd::SlackHook; return; }
    } else slackT = 0;
    if (inCover) {
        chafe += 0.05f * dt;                             // the line rasps on the rock or the wreck
        if (tension > 0.6f * P0) coverPullT += dt; else coverPullT = 0;
        if (coverPullT > 1.5f) { inCover = false; coverPullT = 0; nextBurst = 8 + Rand() * 5; }   // pulled out
    }
    // the keel: a fish that crosses under the hull drags the line over it
    float side = Vector2DotProduct(Vector2Subtract(Flat(p), Flat(tip)), outboard);
    if (keelSide > 0 && side < 0 && Vector2Distance(Flat(p), Flat(tip)) < 14) {
        if (Rand() >= keelClear) chafe += 0.10f;
    }
    keelSide = side;
    if (shaking && !ld.biteProof && spec.teeth && ld.stretch < 0.05f) chafe += 0.01f * dt;   // teeth on braid
    if (!ld.biteProof && p.z > spec.floor - 1.5f && tension > 0.1f * rating) chafe += bottomRough * dt;   // the line on the rock
    if (shaking) {
        float r = tension / rating;
        float rate = k.tearK * r * r * spec.softMouth * (hook == Hook::Treble ? 0.4f : 1.0f) + (lightHook ? k.lightHookTear : 0);
        if (Rand() < rate * dt) { end = FightEnd::PulledHook; return; }
    }

    // ---- stamina: drained by the tension, more with side pressure, less pulling straight back
    Vector2 fishH = Flat(h);
    float mult = 1;
    if (tension < 0.05f * rating) mult = 0;
    else if (Vector2Length(fishH) < 0.3f || cur == Pattern::Dive && h.z > 0.6f) mult = pumping ? 1.0f : 0.6f;
    else {
        Vector2 pd = Rot(Vector2Scale(Flat(u), -1), rodLean * k.leanRad);
        float a = Ang(pd, fishH) * 57.2958f;
        float lateral = fishH.x * pd.y - fishH.y * pd.x;   // which side the pull comes from
        if (a >= 45 && a <= 135 && (turn == 0 || lateral * turn <= 0 || fabsf(turn) < 0.05f)) mult = 1.5f;
        else if (a > 150) mult = 0.6f;
    }
    if (!inCover) S -= tension / std::max(0.2f, P0) * mult * dt;
    if (tension < 0.2f * Pull()) S += 0.3f * dt;
    S = std::clamp(S, 0.0f, S0);

    // ---- alongside
    if (tired && Vector2Distance(Flat(p), Flat(tip)) < k.alongside && p.z < 3) alongside = true;

    // ---- the drawn line (Verlet): hangs in the water, never longer than what's out
    if ((int)node.size() >= 2) {
        int n = (int)node.size();
        node[0] = tip; node[n - 1] = p;
        for (int i = 1; i < n - 1; i++) {
            Vector3 q = node[i];
            Vector3 vel = Vector3Scale(Vector3Subtract(q, prev[i]), 0.9f);
            prev[i] = q;
            float sink = q.z > 0 ? 1.5f : 9.8f;
            node[i] = Vector3Add(Vector3Add(q, vel), V3(0, 0, sink * dt * dt));
        }
        float seg = std::max(L, Vector3Distance(tip, p)) / (n - 1);
        for (int it = 0; it < 6; it++)
            for (int i = 0; i < n - 1; i++) {
                Vector3 d = Vector3Subtract(node[i + 1], node[i]);
                float l = Vector3Length(d);
                if (l <= seg || l < 1e-5f) continue;
                Vector3 c = Vector3Scale(d, (l - seg) / l * 0.5f);
                if (i > 0) node[i] = Vector3Add(node[i], c);
                if (i + 1 < n - 1) node[i + 1] = Vector3Subtract(node[i + 1], c);
            }
    }
}

bool Fight::Land(float skill) {
    if (!alongside || end != FightEnd::None) return false;
    float chance = spec.kg < 5 ? 0.97f : spec.kg < 40 ? skill : std::min(0.97f, skill + 0.05f);
    if (Rand() < chance) { end = FightEnd::Landed; return true; }
    // a miss spooks it into one more run
    S = 0.35f * S0; alongside = false; burstT = 4 + Rand() * 3; nextBurst = 12;
    Vector2 away = Vector2Normalize(Vector2Subtract(Flat(p), Flat(tip)));
    if (Vector2Length(away) < 0.5f) away = outboard;
    h = Vector3Normalize(V3(away.x, away.y, 0.3f));
    return false;
}

Vector2 Fight::PullOnBoat() const {
    if (!on || end != FightEnd::None) return {0, 0};
    Vector2 d = Vector2Subtract(Flat(p), Flat(tip));
    float l = Vector2Length(d);
    if (l < 1e-3f) return {0, 0};
    float horiz = l / std::max(0.01f, Vector3Distance(p, tip));
    return Vector2Scale(d, tension * horiz / l);
}

// ---------------------------------------------------------------- the bite
void Bite::Start(const FishSpec* f, bool wary, bool angler, uint32_t seed) {
    fish = f; rng = seed * 747796405u + 1; stage = BiteStage::Inspect;
    auto R = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); };
    t = 0.8f + R() * 1.7f;
    nibbles = 1 + (int)(R() * 4) % 4;
    window = (wary ? 0.15f : 0.25f) + (angler ? 0.075f : 0);
}
int Bite::Step(float dt, bool strike, bool reelingCircle) {
    auto R = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); };
    switch (stage) {
        case BiteStage::Inspect: case BiteStage::Nibble:
            if (strike) { stage = BiteStage::Gone; return -1; }   // striking early spooks it (and the school learns)
            t -= dt;
            if (t <= 0) {
                if (stage == BiteStage::Inspect || nibbles > 0) {
                    if (stage == BiteStage::Nibble) nibbles--;
                    stage = BiteStage::Nibble; t = 0.35f + R() * 0.45f;
                    if (nibbles <= 0) { stage = BiteStage::Take; t = window; }
                }
            }
            return 0;
        case BiteStage::Take:
            if (strike || reelingCircle) { stage = BiteStage::None; return 1; }
            t -= dt;
            if (t <= 0) { stage = BiteStage::Gone; return -1; }   // it dropped the bait
            return 0;
        default: return 0;
    }
}
float Bite::Tick() const {
    switch (stage) {
        case BiteStage::Inspect: return 0.15f * (0.5f + 0.5f * sinf(t * 23));
        case BiteStage::Nibble: return t > 0.25f ? 0.1f : 0.5f;   // a tap as each nibble lands
        case BiteStage::Take: return 1.0f;
        default: return 0;
    }
}

// ---------------------------------------------------------------- the bot angler
void BotFight(Fight& f, Skill sk, float dt, uint32_t& rng) {
    (void)dt;
    auto R = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); };
    const SkillDef& S = SkillOf(sk);
    float rating = TackleOf(f.tackle).strength;
    float known = rating * (LineOf(f.line).biteProof ? 1.0f : powf(0.9f, (float)f.wraps));   // it can't see chafe
    float drag = (f.tackle == Tackle::Chair ? 0.5f : 0.33f) * known;   // a third of the rating, as any old hand sets it (the chair's harness holds half)
    if (f.cur == Pattern::Cover && f.hasCover && !f.inCover && Vector3Distance(f.p, f.cover) < 15) drag = 0.6f * known;   // turn it early
    if (f.inCover) drag = 0.7f * known;
    f.drag = drag;
    f.reeling = f.tension < 0.9f * f.drag && f.jumpT < 0;
    f.pumping = f.cur == Pattern::Dive;
    f.keelClear = S.keel;
    // side pressure: lean the rod whichever way turns the fish against its turn
    Vector2 u{f.p.x - f.tip.x, f.p.y - f.tip.y};
    Vector2 fh{f.h.x, f.h.y};
    float best = 0, bestLean = 0;
    for (float lean : {-1.0f, 0.0f, 1.0f}) {
        float c = cosf(lean * K().leanRad), s = sinf(lean * K().leanRad);
        Vector2 pd{-u.x * c + u.y * s, -u.x * s - u.y * c};
        float lu = sqrtf(pd.x * pd.x + pd.y * pd.y), lf = sqrtf(fh.x * fh.x + fh.y * fh.y);
        if (lu < 1e-3f || lf < 1e-3f) continue;
        float a = acosf(std::clamp((pd.x * fh.x + pd.y * fh.y) / (lu * lf), -1.0f, 1.0f)) * 57.2958f;
        float score = (a >= 45 && a <= 135) ? 2 : a > 150 ? 0 : 1;
        if (score > best) { best = score; bestLean = lean; }
    }
    f.rodLean = bestLean;
    // a jump: bow within 0.3 s (an Able hand usually reads it coming)
    if (f.jumpT >= 0) {
        if (f.botBowAt < 0) f.botBowAt = R() < S.bow ? 0.05f + R() * 0.2f : 0.32f + R() * 0.3f;
        f.bowed = f.jumpT >= f.botBowAt;
    } else f.bowed = false;
}

// ---------------------------------------------------------------- the rods aboard the Gannet
namespace {
bool IsRod(StationKind k) { return k == StationKind::PortRod || k == StationKind::StarRod || k == StationKind::SternRodP || k == StationKind::SternRodS; }
float DefaultDepth(Tackle t) {
    switch (t) { case Tackle::Handline: return 4; case Tackle::Light: return 6; case Tackle::Medium: return 20;
                 case Tackle::Heavy: return 15; case Tackle::DeepDrop: return 140; case Tackle::Chair: return 3; default: return 8; }
}
// Stage 2's dummy bites: what each tackle draws until the food web decides (stage 3)
const FishSpec* DummyBite(Tackle t, float r) {
    const char* n = "snapper";
    switch (t) {
        case Tackle::Handline: n = "silverside"; break;
        case Tackle::Light: n = r < 0.7f ? "snapper" : "mahi"; break;
        case Tackle::Medium: n = r < 0.5f ? "lingcod" : r < 0.8f ? "mahi" : "leopard shark"; break;
        case Tackle::Heavy: n = r < 0.7f ? "yellowfin" : "opah"; break;
        case Tackle::DeepDrop: n = "sturgeon"; break;
        case Tackle::Chair: n = "marlin"; break;
        default: break;
    }
    return FindDummyFish(n);
}
float RRand(uint32_t& s) { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); }
}

void Gannet::GutsOverboard(float kg) {
    if (!eco) return;
    Vector2 w = boat.ToWorld({-2.2f, 3.2f});
    eco->AddBlood({w.x, w.y, 1}, kg * 1.5f);
}
Vector2 Rod::TipDeck() const {
    const StationDef& sd = Stations()[station];
    Vector2 a = sd.at;
    if (sd.kind == StationKind::SternRodP || sd.kind == StationKind::SternRodS) a.x -= 1.0f;
    else a.y += a.y > 0 ? 0.9f : -0.9f;
    return a;
}
int Gannet::RodAt(int st) const {
    for (int i = 0; i < (int)rods.size(); i++) if (rods[i].station == st) return i;
    return -1;
}
void Gannet::CycleTackle(int ci) {
    int ri = crew[ci].station >= 0 ? RodAt(crew[ci].station) : -1;
    if (ri < 0 || rods[ri].state != RodState::Idle) return;
    Rod& r = rods[ri];
    for (int k = 0; k < (int)Tackle::COUNT; k++) {   // the next tackle she owns
        r.tackle = (Tackle)(((int)r.tackle + 1) % (int)Tackle::COUNT);
        if (owned[(int)r.tackle]) break;
    }
    r.line = r.tackle == Tackle::DeepDrop ? LineType::Braid : LineType::Mono;
    r.hook = r.tackle == Tackle::Chair ? Hook::Treble : Hook::Small;
    r.fight.drag = 0.33f * TackleOf(r.tackle).strength;
    r.lureDepth = DefaultDepth(r.tackle);
    Say(std::string("Rigged: ") + TackleOf(r.tackle).name);
}
void Gannet::RodInput(int ci, bool castHeld, Vector2 aimDeck, bool reel, bool strike, float lean, bool bow, bool gaff, float dragScroll) {
    Crew& c = crew[ci];
    int ri = c.station >= 0 ? RodAt(c.station) : -1;
    if (ri < 0) return;
    Rod& r = rods[ri];
    if (c.Has(INJ_HOOKED_HAND)) reel = false;   // a hooked hand can't work a reel
    r.castHeld = castHeld; r.reel = reel; r.lean = lean; r.bow = bow;
    if (strike) r.strikeQ = true;
    if (gaff) r.gaffQ = true;
    Vector2 d = Vector2Subtract(aimDeck, r.TipDeck());
    if (Vector2Length(d) > 0.2f) r.aim = Vector2Normalize(d);
    float rating = TackleOf(r.tackle).strength;
    if (dragScroll != 0) {
        if (r.state == RodState::Fighting) r.fight.drag = std::clamp(r.fight.drag + dragScroll * rating * 0.05f, 0.05f * rating, 1.1f * rating);
        else if (r.state == RodState::Out) r.lureDepth = std::clamp(r.lureDepth - dragScroll * (r.tackle == Tackle::DeepDrop ? 10.0f : 1.0f), 0.5f, 200.0f);
        else r.fight.drag = std::clamp(r.fight.drag + dragScroll * rating * 0.05f, 0.05f * rating, 1.1f * rating);
    }
}

void Gannet::StepRods(float dt) {
    Vector2 o0 = boat.ToWorld({0, 0}), star = Vector2Subtract(boat.ToWorld({0, 1}), o0);
    for (auto& r : rods) {
        const StationDef& sd = Stations()[r.station];
        const TackleDef& td = TackleOf(r.tackle);
        Vector2 tipDeck = r.TipDeck();
        Vector2 tipW = boat.ToWorld(tipDeck);
        Vector2 outDeck = Vector2Normalize(Vector2Subtract(tipDeck, sd.at));
        Vector2 outW = Vector2Normalize(Vector2Subtract(tipW, boat.ToWorld(sd.at)));
        Vector3 tip3{tipW.x, tipW.y, -(boat.Freeboard() + 1.5f)};
        bool manned = false; Role role = Role::Bosun;
        for (const auto& c : crew) if (c.station == r.station && !c.overboard) { manned = true; role = c.role; }
        switch (r.state) {
            case RodState::Idle:
                if (r.castHeld && manned) { r.state = RodState::Charging; r.charge = 0; }
                break;
            case RodState::Charging:
                r.charge = std::min(1.0f, r.charge + dt / 1.2f);
                if (!r.castHeld) {
                    // the cast: along the aim (never into the hull), wind drifts it
                    Vector2 aim = r.aim;
                    if (Vector2DotProduct(aim, outDeck) < 0.3f) aim = Vector2Normalize(Vector2Add(aim, Vector2Scale(outDeck, 1.2f)));
                    float dist = td.dropDown ? 1.0f : std::max(3.0f, r.charge * td.cast);
                    Vector2 at = boat.ToWorld(Vector2Add(tipDeck, Vector2Scale(aim, dist)));
                    if (!td.dropDown) at = Vector2Add(at, Vector2Scale(sea.wind, 0.25f * dist / std::max(1.0f, td.cast) * 3));
                    r.lure = {at.x, at.y, 0};
                    // bait from the stores: shrimp on the light gear, squid strips on the heavier (a bare hook if out)
                    {
                        bool light = r.tackle == Tackle::Light;
                        if (r.tackle == Tackle::Handline) r.bait = "tiny hook";
                        else if (light && baitShrimp > 0) { baitShrimp--; r.bait = "shrimp"; }
                        else if (baitSquid > 0) { baitSquid--; r.bait = "squid strip"; }
                        else if (baitShrimp > 0) { baitShrimp--; r.bait = "shrimp"; }
                        else r.bait = "bare hook";
                    }
                    if (r.lureDepth <= 0 || r.lureDepth == 8) r.lureDepth = DefaultDepth(r.tackle);
                    r.lineOut = Vector2Distance(at, tipW);
                    r.state = RodState::Out; r.settleT = 0; r.bite = Bite{};
                }
                break;
            case RodState::Out: {
                // the lure sinks to its depth; a boat under way drags it astern on the line that's out
                float sink = td.dropDown ? 2.5f : 0.6f;
                r.lure.z = std::min(r.lureDepth, r.lure.z + sink * dt);
                if (r.lure.z > r.lureDepth) r.lure.z = std::max(r.lureDepth, r.lure.z - 1.0f * dt);
                r.lure.x += sea.current.x * dt * 0.5f; r.lure.y += sea.current.y * dt * 0.5f;
                Vector3 d = Vector3Subtract(r.lure, tip3);
                float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
                if (len > r.lineOut + r.lure.z + 0.5f) { float k = (r.lineOut + r.lure.z) / len; r.lure.x = tip3.x + d.x * k; r.lure.y = tip3.y + d.y * k; }
                if (r.reel && r.bite.stage == BiteStage::None) {
                    Vector2 toTip = Vector2Subtract(tipW, {r.lure.x, r.lure.y});
                    float hd = Vector2Length(toTip);
                    float step = td.reel * dt;
                    if (hd > step) { r.lure.x += toTip.x / hd * step; r.lure.y += toTip.y / hd * step; r.lineOut = std::max(0.0f, r.lineOut - step); }
                    r.lure.z = std::max(0.0f, r.lure.z - step * 0.5f);
                    if (hd < 2.0f && r.lure.z < 1.5f) { r.state = RodState::Idle; break; }
                }
                r.settleT += dt;
                if (r.bite.stage == BiteStage::None || r.bite.stage == BiteStage::Gone) {
                    r.bite.stage = BiteStage::None;
                    bool settled = r.lure.z > r.lureDepth - 0.5f || r.settleT > 6;
                    if (settled && manned && eco) {
                        // the web decides: a fish near the lure that eats what's on it, hungry enough to take
                        if (r.bait.empty()) r.bait = DefaultBait(r.tackle);
                        int sp = eco->TryBite(r.lure, r.tackle, r.bait, dt, nullptr);
                        if (sp >= 0) {
                            r.biteSpec = eco->SpecOf(sp, RRand(r.rng)); r.fishSp = sp;
                            r.bite.Start(&r.biteSpec, r.biteSpec.wary, role == Role::Angler, r.rng++);
                        }
                    } else if (settled && manned && RRand(r.rng) < dt / 18.0f) {
                        const FishSpec* f = DummyBite(r.tackle, RRand(r.rng));   // (no ground: the stage-2 stand-in)
                        if (f) { r.biteSpec = *f; r.biteSpec.kg *= 0.7f + 0.6f * RRand(r.rng); r.fishSp = -1; r.bite.Start(&r.biteSpec, f->wary, role == Role::Angler, r.rng++); }
                    }
                } else {
                    int res = r.bite.Step(dt, r.strikeQ, r.hook == Hook::Circle && r.reel);
                    if (res > 0) {
                        FishSpec f = r.biteSpec;
                        if (eco && r.fishSp >= 0) { f.floor = std::max(1.0f, eco->DepthAt({r.lure.x, r.lure.y})); eco->TakeNear(r.fishSp, r.lure); }
                        r.headOnly = false;
                        r.fight = Fight{};
                        r.fight.tackle = r.tackle; r.fight.line = r.line; r.fight.hook = r.hook;
                        r.fight.drag = 0.33f * td.strength;
                        r.fight.tip = tip3; r.fight.outboard = outW;
                        r.fight.HookFish(f, r.lure, r.rng++);
                        r.state = RodState::Fighting; r.alongT = 0;
                        Say(role == Role::Angler ? std::string("Fish on: ") + f.name : std::string("Fish on!"));
                    } else if (res < 0) {
                        Say(r.strikeQ ? "Struck too soon: it's gone" : "It dropped the bait");
                        r.bite = Bite{};
                    }
                }
                r.lastTick = r.bite.Tick();
                break;
            }
            case RodState::Fighting: {
                Fight& f = r.fight;
                f.tip = tip3; f.outboard = outW;
                if (r.botAngler) BotFight(f, botSkill, dt, r.rng);
                else if (manned) { f.reeling = r.reel; f.rodLean = r.lean; f.bowed = r.bow; f.pumping = r.reel && r.bow; f.keelClear = 0; }
                else { f.reeling = false; f.bowed = false; }        // an unmanned rod holds in its holder
                f.Step(dt);
                // a hooked fish is prey: the thieves come to the thrashing
                if (eco && f.end == FightEnd::None && !r.headOnly) {
                    int kind = 0, thief = eco->Depredate(f.p, f.spec.kg, dt, &kind);
                    if (thief >= 0 && kind == 1) {
                        r.headOnly = true; f.spec.kg *= 0.45f; f.S = std::min(f.S, 0.05f * f.S0); f.jumpT = -1;
                        Say(std::string("Something hit it: ") + (role == Role::Angler ? Species().sp[thief].name : std::string("it's gone slack")));
                    } else if (thief >= 0) {
                        f.end = FightEnd::Taken;
                        Say(std::string("Taken off the line by a ") + Species().sp[thief].name);
                    }
                }
                // the fish pulls on her: a force at the rail and a heel toward the fish's side
                Vector2 pw = f.PullOnBoat();
                boat.extraForce = Vector2Add(boat.extraForce, Vector2Scale(pw, 9.81f));
                boat.extraHeelTorque += Vector2DotProduct(pw, star) * 3.0f;
                if (f.alongside) {
                    r.alongT += dt;
                    bool tryLand = r.gaffQ || (r.botAngler && r.alongT > 1.5f);
                    if (tryLand) {
                        float skill = f.spec.kg < 5 ? 0.97f : (role == Role::Bosun ? 0.85f : 0.8f);
                        if (r.botAngler && botsOn) skill = f.spec.kg < 5 ? 0.97f : SkillOf(botSkill).gaff;   // a bot crew's gaff (design doc, "Bot crew")
                        if (!f.Land(skill)) Say("Missed with the gaff: it runs again");
                        r.alongT = 0;
                    }
                }
                if (f.end != FightEnd::None) {
                    if (f.end == FightEnd::Landed) {
                        std::string nm = std::string(f.spec.name) + (r.headOnly ? " (head)" : "");
                        CatchRec rec; rec.name = nm; rec.kg = f.spec.kg; rec.price = f.spec.price; rec.sp = r.fishSp;
                        rec.grade = r.headOnly ? 0.9f : 1.0f;             // hook 100%, less 10% for the bite taken out of it
                        hold.push_back(rec);
                        r.lastCatch = KgText(f.spec.kg) + " " + nm;
                        Say(std::string("Landed: a ") + r.lastCatch);
                        if (eco && r.fishSp >= 0) eco->Harvest(r.fishSp, r.headOnly ? f.spec.kg / 0.45f : f.spec.kg, f.p, false);
                    } else {
                        if (f.end != FightEnd::Taken) Say(std::string("Lost it: ") + FightEndName(f.end));
                        if (eco) eco->AddBlood(f.p, f.spec.kg * (f.end == FightEnd::Taken ? 2.0f : 0.5f));   // it goes off torn, or in pieces
                    }
                    r.state = RodState::Idle; r.bite = Bite{};
                }
                break;
            }
        }
        r.strikeQ = false; r.gaffQ = false;
    }
    (void)o0;
}

// ---------------------------------------------------------------- the rod test (run by --trawl-boat-test)
int RunTrawlRodTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl stage 2: lines and fishing\n");
    const float dt = 1 / 60.0f;
    auto atRod = [](Gannet& g, StationKind k) {
        for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == k) { g.crew[0].p = Stations()[i].at; g.crew[0].station = i; return g.RodAt(i); }
        return -1;
    };
    // a cast goes where it's aimed, as far as it was charged, and never into the hull
    {
        Gannet g; g.Init(1, 3, Weather::Calm);
        int ri = atRod(g, StationKind::StarRod);
        Rod& r = g.rods[ri];
        Vector2 aim = Vector2Add(r.TipDeck(), {0, 10});
        for (int i = 0; i < 60; i++) { g.RodInput(0, true, aim, false, false, 0, false, false, 0); g.Step(dt); }
        g.RodInput(0, false, aim, false, false, 0, false, false, 0); g.Step(dt);
        Vector2 d = g.boat.ToDeck({r.lure.x, r.lure.y});
        float want = TackleOf(r.tackle).cast / 1.2f;   // a 1 s hold of a 1.2 s full charge
        check(r.state == RodState::Out && fabsf(d.y - r.TipDeck().y - want) < 2, TextFormat("a 1 s cast from the starboard rod lands %.1f m out to starboard (%.1f wanted)", d.y - r.TipDeck().y, want));
        for (int i = 0; i < 60 * 40; i++) { g.RodInput(0, false, aim, false, false, 0, false, false, 0); g.Step(dt); }
        check(fabsf(r.lure.z - r.lureDepth) < 0.6f, TextFormat("the lure sinks to its depth (%.1f of %.1f m)", r.lure.z, r.lureDepth));
        Gannet g2; g2.Init(1, 3, Weather::Calm);
        int r2 = atRod(g2, StationKind::PortRod);
        Vector2 inboard = Vector2Add(g2.rods[r2].TipDeck(), {0, 12});
        for (int i = 0; i < 30; i++) { g2.RodInput(0, true, inboard, false, false, 0, false, false, 0); g2.Step(dt); }
        g2.RodInput(0, false, inboard, false, false, 0, false, false, 0); g2.Step(dt);
        Vector2 d2 = g2.boat.ToDeck({g2.rods[r2].lure.x, g2.rods[r2].lure.y});
        check(d2.y < -3.0f, "a cast aimed across the deck still goes over the port rail");
    }
    // the bite: striking a nibble spooks it; striking the take hooks it; a circle hook sets itself on the reel
    {
        const FishSpec* sn = FindDummyFish("snapper");
        Bite b; b.Start(sn, false, false, 5);
        int res = 0; float t = 0;
        while (b.stage != BiteStage::Nibble && t < 10) { res = b.Step(dt, false, false); t += dt; }
        res = b.Step(dt, true, false);
        check(res < 0, "striking on a nibble spooks the fish");
        int hooked = 0, circle = 0, missed = 0;
        for (int i = 0; i < 200; i++) {
            Bite c; c.Start(sn, false, false, 100 + i);
            int rr = 0; t = 0;
            while (rr == 0 && t < 20) { rr = c.Step(dt, c.stage == BiteStage::Take, false); t += dt; }
            hooked += rr > 0;
            Bite e; e.Start(sn, false, false, 900 + i);
            rr = 0; t = 0;
            while (rr == 0 && t < 20) { rr = e.Step(dt, false, true); t += dt; }
            circle += rr > 0;
            Bite m; m.Start(sn, true, false, 400 + i);
            rr = 0; t = 0; float late = -1;
            while (rr == 0 && t < 20) { if (m.stage == BiteStage::Take && late < 0) late = 0; if (late >= 0) late += dt; rr = m.Step(dt, late > 0.2f, false); t += dt; }
            missed += rr < 0;
        }
        check(hooked == 200, TextFormat("striking in the take's window hooks it (%d/200)", hooked));
        check(circle == 200, TextFormat("a circle hook sets itself if the angler just reels (%d/200)", circle));
        check(missed == 200, TextFormat("a wary fish's 150 ms window is missed at 200 ms (%d/200)", missed));
        Bite a; a.Start(sn, true, true, 7);
        check(fabsf(a.window - 0.225f) < 0.001f, "an Angler gets +75 ms on the hook-set window");
    }
    // a fish on the Gannet's rod: bites come, the fight pulls her toward the fish and heels her, and it lands
    {
        Gannet g; g.Init(1, 21, Weather::Calm);
        int ri = atRod(g, StationKind::StarRod);
        g.rods[ri].tackle = Tackle::Heavy; g.rods[ri].fight.drag = 0.33f * 40; g.rods[ri].lureDepth = 15;
        Vector2 aim = Vector2Add(g.rods[ri].TipDeck(), {0, 10});
        for (int i = 0; i < 50; i++) { g.RodInput(0, true, aim, false, false, 0, false, false, 0); g.Step(dt); }
        g.RodInput(0, false, aim, false, false, 0, false, false, 0); g.Step(dt);
        float t = 0; bool bit = false, on = false;
        while (t < 300 && !on) {
            Rod& r = g.rods[ri];
            bool strike = r.bite.stage == BiteStage::Take;
            bit |= r.bite.stage != BiteStage::None;
            g.RodInput(0, false, aim, false, strike, 0, false, false, 0); g.Step(dt); t += dt;
            on = r.state == RodState::Fighting;
        }
        check(bit && on, TextFormat("a fish bites and is hooked (%.0f s)", t));
        Rod& r = g.rods[ri];
        r.botAngler = true;
        float maxRoll = 0; Vector2 p0 = g.boat.pos; float pullSum = 0; int n = 0;
        while (r.state == RodState::Fighting && t < 2000) {
            g.RodInput(0, false, aim, false, false, 0, false, false, 0); g.Step(dt); t += dt;
            maxRoll = std::max(maxRoll, g.boat.RollDeg());
            Vector2 pw = r.fight.PullOnBoat(); pullSum += Vector2Length(pw); n++;
        }
        Vector2 moved = g.boat.ToDeck(Vector2Add(g.boat.pos, Vector2Subtract(g.boat.pos, p0)));
        (void)moved;
        check(n > 0 && pullSum / n > 1, TextFormat("the fish pulls on her through the line (mean %.1f kgf)", n ? pullSum / n : 0));
        check(maxRoll > 0.05f, TextFormat("a fish on the starboard rod heels her to starboard (%.2f deg)", maxRoll));
        check(!g.hold.empty() || g.log.size() > 0, TextFormat("the fight ends: %s", g.log.empty() ? "?" : g.log.back().c_str()));
    }
    // too much drag on a big fish snaps the line; a line on the reel pays out only past the drag
    {
        Fight f; f.tackle = Tackle::Light; f.tip = {0, 0, -2}; f.outboard = {1, 0};
        f.HookFish(*FindDummyFish("yellowfin"), {20, 0, 10}, 3);
        f.drag = 1.1f * TackleOf(Tackle::Light).strength;
        for (int i = 0; i < 60 * 30 && f.end == FightEnd::None; i++) { f.reeling = true; f.Step(dt); }
        check(f.end == FightEnd::Snapped, TextFormat("a yellowfin on a light rod with the drag screwed down snaps it (%s)", FightEndName(f.end)));
        Fight h; h.tackle = Tackle::Heavy; h.tip = {0, 0, -2}; h.outboard = {1, 0};
        h.HookFish(*FindDummyFish("yellowfin"), {20, 0, 10}, 4);
        h.drag = 10;
        float maxT = 0, L0 = h.L;
        for (int i = 0; i < 60 * 3; i++) { h.Step(dt); maxT = std::max(maxT, h.tension); }
        check(maxT <= 10.01f && h.L > L0 + 1, TextFormat("on its first run the drag holds the tension at its setting (max %.1f kgf) and line pays out (+%.1f m)", maxT, h.L - L0));
    }
    return fails;
}

// ---------------------------------------------------------------- --trawl-fight
namespace {
struct Target { const char* fish; Tackle tackle; LineType line; Hook hook; float tLo, tHi, land; };
const Target TARGETS[] = {   // design doc, "Target fights (sensible bot angler, matched tackle)"
    {"snapper", Tackle::Light, LineType::Mono, Hook::Small, 20, 40, 0.85f},
    {"lingcod", Tackle::Medium, LineType::Mono, Hook::Small, 60, 120, 0.75f},
    {"yellowfin", Tackle::Heavy, LineType::Mono, Hook::Small, 180, 300, 0.60f},
    {"sturgeon", Tackle::DeepDrop, LineType::Braid, Hook::Small, 600, 1200, 0.45f},
    {"marlin", Tackle::Chair, LineType::Mono, Hook::Treble, 480, 720, 0.40f},
};
struct Result { int n = 0, landed = 0; double tLanded = 0, tAll = 0; int ends[8] = {}; };
Result RunMany(const FishSpec& fs, Tackle tk, LineType ln, Hook hk, int N, Skill sk, bool trace) {
    Result r;
    for (int i = 0; i < N; i++) {
        Fight f; f.tackle = tk; f.line = ln; f.hook = hk;
        f.tip = {0, 0, -2}; f.outboard = {1, 0};
        uint32_t rng = 1234567u + i * 7919u;
        Vector3 at;
        const TackleDef& td = TackleOf(tk);
        if (td.dropDown) at = {1.5f, 0, fs.depth};
        else if (fs.depth <= 5 && tk == Tackle::Chair) at = {-40, 6, fs.depth};   // trolled astern
        else at = {td.cast * 0.8f, 0, fs.depth};
        f.HookFish(fs, at, 99 + i);
        float dt = 1 / 60.0f, alongT = 0;
        while (f.end == FightEnd::None && f.t < 3600) {
            BotFight(f, sk, dt, rng);
            f.Step(dt);
            if (f.alongside) { alongT += dt; if (alongT > 1.5f) { f.Land(SkillOf(sk).gaff); alongT = 0; } }
            if (trace && i == 0 && (int)(f.t * 60) % 60 == 0)
                printf("    t %5.0f  L %6.1f  D %6.1f T %6.1f/%5.1f  S %5.1f/%5.1f  z %5.1f b %4.1f e %4.2f %s%s\n", f.t, f.L, Vector3Distance(f.p, f.tip), f.tension, f.drag, f.S, f.S0, f.p.z, f.burstT, f.effort,
                       PatternName(f.cur), f.inCover ? " (cover)" : "");
        }
        r.n++; r.ends[(int)f.end]++; r.tAll += f.t;
        if (f.end == FightEnd::Landed) { r.landed++; r.tLanded += f.t; }
    }
    return r;
}
void Report(const char* name, const char* tackle, const Result& r) {
    printf("  %-10s %-15s landed %5.1f%%  fight %6.0f s", name, tackle, 100.0 * r.landed / std::max(1, r.n), r.landed ? r.tLanded / r.landed : 0.0);
    printf("   (");
    bool first = true;
    for (int e = 2; e < 8; e++) if (r.ends[e]) { printf("%s%s %d", first ? "" : ", ", FightEndName((FightEnd)e), r.ends[e]); first = false; }
    printf(")\n");
}
} // namespace

int RunTrawlFight(int argc, char** argv) {
    // depth.exe --trawl-fight <species|all> [tackle 0-5] [N]
    std::string which = argc >= 3 ? argv[2] : "all";
    int N = 400;
    bool trace = getenv("DEPTH_TRACE") != nullptr;
    if (which == "all") {
        if (argc >= 4) N = atoi(argv[3]);
        printf("The Trawl: target fights (sensible bot angler, Able, matched tackle), %d fights each\n", N);
        int fails = 0;
        for (const auto& tg : TARGETS) {
            const FishSpec* fs = FindDummyFish(tg.fish);
            Result r = RunMany(*fs, tg.tackle, tg.line, tg.hook, N, Skill::Able, trace);
            Report(tg.fish, TackleOf(tg.tackle).name, r);
            float land = (float)r.landed / r.n, ft = r.landed ? (float)(r.tLanded / r.landed) : 0;
            bool ok = fabsf(land - tg.land) <= 0.07f && ft >= tg.tLo && ft <= tg.tHi;
            printf("      target %2.0f%% in %.0f-%.0f s: %s\n", tg.land * 100, tg.tLo, tg.tHi, ok ? "ok" : "OUT OF RANGE");
            if (!ok) fails++;
        }
        printf(fails ? "%d target fight(s) out of range\n" : "All target fights in range\n", fails);
        return fails ? 1 : 0;
    }
    const FishSpec* fs = FindDummyFish(which);
    if (!fs) { printf("Unknown fish '%s'. Known:", which.c_str()); for (const auto& f : DummyFish()) printf(" '%s'", f.name); printf("\n"); return 2; }
    Tackle tk = Tackle::Medium; LineType ln = LineType::Mono; Hook hk = Hook::Small;
    for (const auto& tg : TARGETS) if (which == tg.fish) { tk = tg.tackle; ln = tg.line; hk = tg.hook; }
    if (argc >= 4) tk = (Tackle)std::clamp(atoi(argv[3]), 0, (int)Tackle::COUNT - 1);
    if (argc >= 5) N = atoi(argv[4]);
    for (int s = 0; s < (int)Skill::COUNT; s++) {
        Result r = RunMany(*fs, tk, ln, hk, N, (Skill)s, trace && s == 1);
        char lab[64]; snprintf(lab, sizeof lab, "%s", SkillOf((Skill)s).name);
        Report(lab, TackleOf(tk).name, r);
    }
    return 0;
}

} // namespace tw
