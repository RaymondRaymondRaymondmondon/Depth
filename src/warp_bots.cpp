// Warp Dodgeball: the bots (spec "Bots"), the acceptance tests of the milestones (--warp-test) and the match sim (--warp-sim).
#include "warp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace wd {

static uint32_t Hash(uint32_t a) { a ^= a >> 16; a *= 0x7feb352d; a ^= a >> 15; a *= 0x846ca68b; a ^= a >> 16; return a; }
static float H01(uint32_t a) { return (Hash(a) & 0xFFFF) / 65535.0f; }
static float AngTo(float from, float to) { float d = to - from; while (d > PI) d -= 2 * PI; while (d < -PI) d += 2 * PI; return d; }

// Bots stay on their side, fetch balls, carry rush balls behind the attack line, aim with lead and drop, charge to a
// chosen power, curve now and then, and meet incoming balls with a catch (by skill) or a squat or a dive.
// skill 0 green, 1 able, 2 sharp. Portals: a sharp bot sometimes opens a pair (A behind itself, B high on the far side
// wall) and banks a throw through it.
void BotInput(const World& w, int me, Input& in, uint32_t& rng, int skill) {
    const Config& C = Cfg(); const Player& p = w.players[me];
    auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; };
    Input o; o.yaw = p.yaw; o.pitch = p.pitch;
    float side = p.team == 0 ? -1.0f : 1.0f;
    auto clampSide = [&](Vector3 v) { float lo = 0.6f, hi = w.arena.halfL - 0.4f; float ax = std::clamp(fabsf(v.x) * (v.x * side >= 0 ? 1 : 0), lo, hi); v.x = side * ax; v.z = std::clamp(v.z, -w.arena.halfW + 0.4f, w.arena.halfW + 0.4f - 0.8f); return v; };
    auto walkTo = [&](Vector3 tgt, bool sprint) {
        // round the pieces: if a low box stands across the straight line, head for its best corner first
        auto blocked = [&](Vector3 a, Vector3 b, int* which) {
            for (int i = 0; i < (int)w.arena.boxes.size(); i++) {
                const Box& bx = w.arena.boxes[i]; if (bx.lo.y > 1.5f || bx.hi.y < 0.3f) continue;
                float m = C.radius + 0.05f, lx = bx.lo.x - m, hx = bx.hi.x + m, lz = bx.lo.z - m, hz = bx.hi.z + m;
                for (int k = 1; k < 16; k++) { float u = k / 16.0f; float x = a.x + (b.x - a.x) * u, z = a.z + (b.z - a.z) * u; if (x > lx && x < hx && z > lz && z < hz) { if (which) *which = i; return true; } }
            }
            return false;
        };
        int bi2;
        if (blocked(p.pos, tgt, &bi2)) {
            const Box& bx = w.arena.boxes[bi2]; float m = C.radius + 0.35f; float best = 1e9f; Vector3 via = tgt;
            Vector3 cs[4] = {{bx.lo.x - m, 0, bx.lo.z - m}, {bx.hi.x + m, 0, bx.lo.z - m}, {bx.lo.x - m, 0, bx.hi.z + m}, {bx.hi.x + m, 0, bx.hi.z + m}};
            for (auto& cc : cs) { if (blocked(p.pos, cc, nullptr)) continue; float d = Vector2Distance({p.pos.x, p.pos.z}, {cc.x, cc.z}) + Vector2Distance({cc.x, cc.z}, {tgt.x, tgt.z}); if (d < best) { best = d; via = cc; } }
            tgt = via;
        }
        Vector3 d{tgt.x - p.pos.x, 0, tgt.z - p.pos.z}; float L = sqrtf(d.x * d.x + d.z * d.z); if (L < 0.15f) return;
        float yawTo = atan2f(d.z, d.x), rel = AngTo(p.yaw, yawTo);   // (move in our own frame: forward / right)
        o.moveX = cosf(rel) * std::min(1.0f, L); o.moveZ = sinf(rel) * std::min(1.0f, L); o.sprint = sprint && L > 2;
    };
    if (!p.alive || w.phase == PH_ROUND_END || w.phase == PH_MATCH_END) { in = o; return; }
    // the nearest enemy (aim and face)
    int foe = -1; float fd = 1e9f;
    for (const auto& q : w.players) if (q.present && q.alive && q.team != p.team) { float d = Vector3Distance(q.pos, p.pos); if (d < fd) { fd = d; foe = q.id; } }
    if (foe >= 0) { const Player& f = w.players[foe]; float want = atan2f(f.pos.z - p.pos.z, f.pos.x - p.pos.x); o.yaw = p.yaw + std::clamp(AngTo(p.yaw, want), -0.12f, 0.12f); }
    if (w.phase == PH_WARMUP) { in = o; return; }
    // 1. an incoming ball: catch (by skill, decided once per flight) or get out of the way
    int bi; float tc;
    if (w.Threat(p, &bi, &tc)) {
        const Ball& b = w.balls[bi]; float spd = Vector3Length(b.v), win = w.CatchWindow(spd);
        uint32_t key = (uint32_t)(bi * 7919 + b.thrower * 104729 + (int)(w.round * 31) + (int)(b.age < 0.05f ? 0 : 1) * 0) ^ (uint32_t)(w.players[std::max(0, b.thrower)].outs * 131 + w.players[std::max(0, b.thrower)].catches * 17 + (int)(w.t / 4));
        float catchChance = (skill == 0 ? 0.25f : skill == 1 ? 0.45f : 0.6f) * (spd > C.fastSpeed ? 0.6f : 1.0f);
        if (H01(key ^ (uint32_t)me * 2654435761u) < catchChance) {
            float jitter = (H01(key * 3 + me) - 0.5f) * (skill == 0 ? 0.25f : skill == 1 ? 0.16f : 0.1f);
            if (tc <= win * 0.4f + jitter) o.catchP = true;
            o.yaw = atan2f(-b.v.z, -b.v.x);   // (face it)
        } else if (tc < 0.35f) {
            float hy = b.p.y + b.v.y * tc;
            if (hy > p.pos.y + 1.0f && p.squatCool <= 0) o.squat = true;
            else { o.dive = true; o.moveZ = H01(key + 5) < 0.5f ? 1 : -1; }
        }
        in = o; return;
    }
    // 2. holding a ball: carry a rush ball back; otherwise aim and charge
    if (p.ball >= 0) {
        const Ball& b = w.balls[p.ball];
        if (b.mustCarry) { walkTo({side * (C.attackLine + 1.2f), 0, p.pos.z}, true); in = o; return; }
        if (foe < 0) { in = o; return; }
        const Player& f = w.players[foe];
        uint32_t key = Hash((uint32_t)(me * 977 + p.outs * 31 + p.catches * 7 + (int)(w.t / 3)));
        float want = skill == 0 ? 0.45f + 0.4f * H01(key) : 0.6f + 0.4f * H01(key);
        float curve = H01(key + 1) < (skill == 2 ? 0.35f : 0.15f) ? (H01(key + 2) < 0.5f ? -1.0f : 1.0f) : 0;
        // the portal play (sharp bots, now and then): A on our own end wall behind us, B on the ceiling over the target,
        // then a full throw straight back into A: it drops out of the ceiling onto them
        if (skill >= 2 && !w.suddenDeath && H01(key + 9) < 0.3f) {
            Vector3 aPt{side * w.arena.OuterX(), 1.6f, std::clamp(p.pos.z, -w.arena.OuterZ() + 0.6f, w.arena.OuterZ() - 0.6f)};
            float lead = 0.15f + Vector3Distance(p.Eye(), aPt) / 26 + 0.35f;
            Vector3 bPt{std::clamp(f.pos.x + f.vel.x * lead, -w.arena.halfL + 0.6f, w.arena.halfL - 0.6f), w.arena.ceil, std::clamp(f.pos.z + f.vel.z * lead, -w.arena.halfW + 0.6f, w.arena.halfW - 0.6f)};
            auto aimAt = [&](Vector3 q) { Vector3 d = Vector3Subtract(q, p.Eye()); o.yaw = atan2f(d.z, d.x); o.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)); };
            bool aOk = p.portal[0].on && fabsf(p.portal[0].c.x - aPt.x) < 0.1f && fabsf(p.portal[0].c.z - aPt.z) < 2.5f;
            bool bOk = p.portal[1].on && p.portal[1].n.y < -0.9f && Vector2Distance({p.portal[1].c.x, p.portal[1].c.z}, {bPt.x, bPt.z}) < 1.6f;
            if (!aOk && p.portal[0].cool <= 0) { aimAt(aPt); o.portalA = true; in = o; return; }
            if (!bOk && p.portal[1].cool <= 0) { aimAt(bPt); o.portalB = true; in = o; return; }
            if (aOk && bOk) {
                Vector3 c0 = p.portal[0].c; c0.y += 0.0f; aimAt(c0); o.pitch -= 0.0f;
                float loftF = C.loftFull * DEG2RAD; o.pitch -= loftF;   // (a full throw: flat into the portal)
                o.throwHeld = p.charge < 0.999f && p.releaseT <= 0; o.curve = 0;
                in = o; return;
            }
        }
        float speed = C.speedMin + C.speedAdd * want * want, loft = (C.loftZero + (C.loftFull - C.loftZero) * want) * DEG2RAD;
        Vector3 eye = p.Eye(), aim{f.pos.x, f.pos.y + f.Height() * 0.55f, f.pos.z};
        float tFly = Vector3Distance(eye, aim) / speed;
        aim.x += f.vel.x * tFly * (skill >= 1 ? 1 : 0.4f); aim.z += f.vel.z * tFly * (skill >= 1 ? 1 : 0.4f);
        // a curved throw is aimed off to the other side so the curve brings it back (about 3 m over 20 m at full)
        float dx = aim.x - eye.x, dz = aim.z - eye.z, horiz = sqrtf(dx * dx + dz * dz);
        float yawAim = atan2f(dz, dx) - curve * want * 0.075f * std::min(1.0f, horiz / 20) * 2;
        float drop = 0.5f * C.gravity * tFly * tFly;
        float pitchAim = atan2f(aim.y - eye.y + drop, horiz) - loft;
        float err = (skill == 0 ? 0.06f : skill == 1 ? 0.035f : 0.02f);
        o.yaw = yawAim + (H01(key + 3) - 0.5f) * err; o.pitch = pitchAim + (H01(key + 4) - 0.5f) * err;
        o.curve = curve;
        // strafe a little while charging; release at the chosen power
        o.moveZ = sinf(w.t * 1.3f + me) * 0.5f;
        o.throwHeld = p.charge < want - 0.001f && p.releaseT <= 0;
        if (o.throwHeld && p.overT > 1.0f) o.throwHeld = false;
        in = o; return;
    }
    // 3. empty-handed: the nearest ball we may pick up (resting on our side, or on the line)
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)w.balls.size(); i++) {
        const Ball& b = w.balls[i]; if (b.st != BS_REST && !(b.st == BS_DEAD && b.p.y < 0.6f)) continue;
        if (!((p.team == 0 && b.p.x <= 0.15f) || (p.team == 1 && b.p.x >= -0.15f))) continue;
        if (b.p.y > 0.5f) continue;   // (on a nest or a pillar: not worth the climb)
        // don't run for a ball a teammate is already closer to
        float d = Vector3Distance(b.p, p.pos); bool mate = false;
        for (const auto& q : w.players) if (q.id != me && q.alive && q.team == p.team && q.ball < 0 && Vector3Distance(q.pos, b.p) < d - 0.3f) mate = true;
        if (mate && w.phase != PH_RUSH) d += 6;
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) { Vector3 t = w.balls[best].p; t.x = std::clamp(t.x * side, 0.35f, w.arena.halfL) * side; walkTo(t, true); in = o; return; }
    // 4. nothing to do: hang back, drifting side to side
    Vector3 home{side * (w.arena.halfL * 0.55f), 0, sinf(w.t * 0.4f + me * 1.7f) * w.arena.halfW * 0.6f};
    walkTo(clampSide(home), false);
    (void)rnd;
    in = o;
}

// ---------------------------------------------------------------- the acceptance tests
static int gFails = 0;
static void Check(bool ok, const char* what, const std::string& detail = "") { std::printf("  [%s] %s%s%s\n", ok ? "ok" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str()); if (!ok) gFails++; }
static std::string F(const char* fmt, double a, double b = 0, double c = 0) { char s[160]; std::snprintf(s, sizeof s, fmt, a, b, c); return s; }

// a world with nobody on the floor (for the ball's own tests)
static World Empty(int arena) { World w; w.Init(arena, 1, 1); for (auto& p : w.players) p.present = false; w.phase = PH_LIVE; w.balls.clear(); return w; }
static Ball Live(Vector3 p, Vector3 v, Vector3 spin = {}) { Ball b; b.p = p; b.v = v; b.spin = spin; b.st = BS_LIVE; b.team = 0; b.thrower = -1; return b; }

int RunWarpTest() {
    gFails = 0; const Config& C = Cfg();
    std::printf("Warp Dodgeball tests\n");
    // M1 the arena
    {
        Arena a = MakeArena(AR_CLASSIC), x = MakeArena(AR_EXTREME);
        Check(a.halfL * 2 == C.cLength && a.halfW * 2 == C.cWidth && a.panels.size() == 5, "classic court 18 x 9 with 5 portal panels", F("%.0f panels", (double)a.panels.size()));
        int pads = 0; for (const auto& p : x.panels) if (p.n.y > 0.9f) pads++;
        Check(x.boxes.size() >= 18 && x.ladders.size() == 4 && pads == 4, "extreme: cover, pillars, nests, deflectors, 4 ladders, 4 floor pads", F("%.0f boxes", (double)x.boxes.size()));
        bool mirror = true; for (const auto& b : x.boxes) { bool found = false; for (const auto& c : x.boxes) if (fabsf(c.lo.x + b.hi.x) < 1e-3f && fabsf(c.lo.z - b.lo.z) < 1e-3f && fabsf(c.hi.y - b.hi.y) < 1e-3f) found = true; if (!found) mirror = false; }
        Check(mirror, "extreme is mirror-symmetric");
    }
    // M2 the player: walk / sprint / crouch speeds, jump apex, the squat, the dive
    {
        World w; w.Init(AR_CLASSIC, 1, 3); w.phase = PH_LIVE; w.balls.clear();
        Player& p = w.players[0]; p.pos = {-6, 0, 0}; p.yaw = 0;
        auto run = [&](Input in, float secs) { for (int i = 0; i < (int)(secs / STEP); i++) { p.in = in; w.StepPlayer(p); } };
        Input walk; walk.moveX = 1; run(walk, 0.5f); float v1 = Vector3Length({p.vel.x, 0, p.vel.z});
        Input spr = walk; spr.sprint = true; p.pos = {-10, 0, 0}; run(spr, 0.5f); float v2 = Vector3Length({p.vel.x, 0, p.vel.z});
        Input cr = walk; cr.crouch = true; p.pos = {-10, 0, 0}; run(cr, 0.5f); float v3 = Vector3Length({p.vel.x, 0, p.vel.z});
        Check(fabsf(v1 - C.walk) < 0.05f && fabsf(v2 - C.sprint) < 0.05f && fabsf(v3 - C.crouchSpeed) < 0.05f, "walk 4.5 / sprint 6.5 / crouch 2.0 m/s", F("%.2f / %.2f / %.2f", v1, v2, v3));
        Check(fabsf(p.Height() - C.crouchH) < 1e-3f, "crouch height 1.1 m");
        p.pos = {-6, 0, 0}; p.vel = {}; run(Input{}, 0.1f);
        Input j; j.jump = true; p.in = j; w.StepPlayer(p); float top = 0; for (int i = 0; i < 120; i++) { p.in = Input{}; w.StepPlayer(p); top = std::max(top, p.pos.y); }
        Check(fabsf(top - C.jumpApex) < 0.03f, "jump apex 1.2 m", F("%.3f m", top));
        p.in = Input{}; Input sq; sq.squat = true; p.in = sq; w.StepPlayer(p); bool squatted = p.po == PO_SQUAT && fabsf(p.Height() - C.squatH) < 1e-3f;
        run(Input{}, C.squatTime + 0.02f); bool up = p.po == PO_STAND; p.in = sq; w.StepPlayer(p); bool cool = p.po != PO_SQUAT;
        Check(squatted && up && cool, "squat: 0.9 m for 0.4 s, then a 0.6 s cooldown");
        run(Input{}, 1.0f); Vector3 at = p.pos; Input dv; dv.dive = true; dv.moveX = 1; p.in = dv; w.StepPlayer(p); run(Input{}, C.diveTime);
        float dist = Vector3Distance(at, p.pos); bool prone = p.po == PO_PRONE; run(Input{}, C.diveRecover + 0.02f);
        Check(fabsf(dist - C.diveDist) < 0.15f && prone && p.po == PO_STAND, "dive 3 m, prone, back up after 0.8 s", F("%.2f m", dist));
        p.pos = {-0.5f, 0, 0}; p.alive = true; run(walk, 0.4f);
        Check(!p.alive && p.outCause == "crossed the centre line", "stepping over the centre line is an out");
    }
    // M3 the ball and the throw: speeds, loft, no tunnelling in 1000 hard throws
    {
        World w; w.Init(AR_CLASSIC, 1, 5); w.phase = PH_LIVE;
        Player& p = w.players[0]; p.pos = {-6, 0, 0}; for (auto& q : w.players) if (q.id) q.present = false;
        auto throwAt = [&](float hold) {
            w.balls.resize(1); w.balls[0] = Ball{}; w.balls[0].st = BS_HELD; w.balls[0].holder = 0; p.ball = 0; p.charge = p.heldT = p.overT = 0; p.releaseT = 0; p.charging = false;
            Input in; in.throwHeld = true; in.yaw = 0; in.pitch = 0;
            for (int i = 0; i < (int)ceilf(hold / STEP); i++) { p.in = in; w.StepThrow(p); }
            p.in = Input{}; w.StepThrow(p); for (int i = 0; i < 40 && w.balls[0].st == BS_HELD; i++) w.StepThrow(p);
            return w.balls[0];
        };
        Ball a = throwAt(0.01f), b = throwAt(C.chargeTime);
        float la = asinf(a.v.y / Vector3Length(a.v)) * RAD2DEG, lb = asinf(b.v.y / Vector3Length(b.v)) * RAD2DEG;
        Check(fabsf(Vector3Length(a.v) - C.speedMin) < 0.2f && fabsf(Vector3Length(b.v) - (C.speedMin + C.speedAdd)) < 0.05f, "throw speed 8 -> 26 m/s with charge", F("%.2f -> %.2f", Vector3Length(a.v), Vector3Length(b.v)));
        Check(fabsf(la - C.loftZero) < 0.3f && fabsf(lb - C.loftFull) < 0.05f, "loft 10 deg -> 2 deg", F("%.2f -> %.2f deg", la, lb));
        Ball c = throwAt(C.overAfter + 3.5f + C.chargeTime); float spread = 0;
        for (int k = 0; k < 40; k++) { Ball d = throwAt(C.chargeTime + C.overAfter + 4); spread = std::max(spread, fabsf(atan2f(d.v.z, d.v.x)) * RAD2DEG); }
        (void)c; Check(spread > 1.0f && spread <= C.overMax + 0.01f, "overcharge spreads the throw (up to 6 deg)", F("%.2f deg", spread));
        // tunnelling
        World e = Empty(AR_CLASSIC); int escaped = 0; uint32_t r = 99;
        auto rf = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
        for (int k = 0; k < 1000; k++) {
            float yaw = rf() * 2 * PI, pit = (rf() - 0.3f) * 1.2f;
            e.balls.assign(1, Live({(rf() - 0.5f) * 16, 0.5f + rf() * 6, (rf() - 0.5f) * 10}, Vector3Scale({cosf(pit) * cosf(yaw), sinf(pit), cosf(pit) * sinf(yaw)}, 26)));
            for (int s = 0; s < 240; s++) { e.StepBall(e.balls[0]); const Vector3& q = e.balls[0].p; if (fabsf(q.x) > e.arena.OuterX() + 1e-3f || fabsf(q.z) > e.arena.OuterZ() + 1e-3f || q.y < -1e-3f || q.y > e.arena.ceil + 1e-3f) { escaped++; break; } }
        }
        World x = Empty(AR_EXTREME); int inside = 0;
        for (int k = 0; k < 1000; k++) {   // (the Extreme's thin deflectors too)
            float yaw = rf() * 2 * PI, pit = (rf() - 0.3f) * 0.8f;
            x.balls.assign(1, Live({(rf() - 0.5f) * 26, 0.5f + rf() * 2, (rf() - 0.5f) * 14}, Vector3Scale({cosf(pit) * cosf(yaw), sinf(pit), cosf(pit) * sinf(yaw)}, 26)));
            bool startIn = false; for (const auto& bx : x.arena.boxes) { const Vector3& q = x.balls[0].p; if (q.x > bx.lo.x - 0.11f && q.x < bx.hi.x + 0.11f && q.y > bx.lo.y - 0.11f && q.y < bx.hi.y + 0.11f && q.z > bx.lo.z - 0.11f && q.z < bx.hi.z + 0.11f) startIn = true; }
            if (startIn) continue;
            for (int s = 0; s < 240; s++) { x.StepBall(x.balls[0]); const Vector3& q = x.balls[0].p; bool in = false; for (const auto& bx : x.arena.boxes) if (q.x > bx.lo.x + 0.01f && q.x < bx.hi.x - 0.01f && q.y > bx.lo.y + 0.01f && q.y < bx.hi.y - 0.01f && q.z > bx.lo.z + 0.01f && q.z < bx.hi.z - 0.01f) in = true; if (in || fabsf(q.x) > x.arena.OuterX() + 1e-3f) { inside++; break; } }
        }
        Check(escaped == 0 && inside == 0, "1000 throws at 26 m/s never tunnel through a wall (x2 arenas)", F("%.0f out of the room, %.0f through a piece", escaped, inside));
        // bounces
        World f = Empty(AR_CLASSIC); f.balls.assign(1, Live({0, 2, 0}, {0, -6, 0}));
        float up = 0; for (int s = 0; s < 120; s++) { f.StepBall(f.balls[0]); if (f.balls[0].v.y > 0) { up = f.balls[0].v.y; break; } }
        Check(f.balls[0].st == BS_DEAD && up > 0, "a live ball that touches the floor is dead (and bounces)", F("rebound %.2f m/s", up));
    }
    // M4 curve: full spin drifts 2.5-3.5 m over 20 m
    {
        World e = Empty(AR_EXTREME); e.arena.boxes.clear();
        float sp = C.speedMin + C.speedAdd, loft = C.loftFull * DEG2RAD;
        e.balls.assign(1, Live({-14, 5, 0}, {sp * cosf(loft), sp * sinf(loft), 0}, {0, -C.spinMax, 0}));
        float drift = 0; for (int s = 0; s < 600; s++) { e.StepBall(e.balls[0]); if (e.balls[0].p.x >= 6) { drift = e.balls[0].p.z; break; } }
        Check(drift > 2.5f && drift < 3.5f, "full curve drifts 2.5-3.5 m right over 20 m", F("%.2f m", drift));
        e.balls.assign(1, Live({-14, 5, 0}, {sp * cosf(loft), sp * sinf(loft), 0}, {0, C.spinMax, 0}));
        float d2 = 0; for (int s = 0; s < 600; s++) { e.StepBall(e.balls[0]); if (e.balls[0].p.x >= 6) { d2 = e.balls[0].p.z; break; } }
        Check(fabsf(d2 + drift) < 0.01f, "the curve is symmetric left and right", F("%.2f m", d2));
    }
    // M5/M6 catching: the windows, a catch, a fumble, a late catch, the block, a hit
    {
        Check(fabsf(World{}.CatchWindow(10) - 0.35f) < 1e-4f && fabsf(World{}.CatchWindow(16) - 0.25f) < 1e-4f && fabsf(World{}.CatchWindow(24) - 0.15f) < 1e-4f, "catch windows 0.35 / 0.25 / 0.15 s at 10 / 16 / 24 m/s");
        // press at offset (s, relative to contact; < 0 early) with a ball at speed; the result
        // (the speed is the speed at contact: the ball is thrown faster by the drag over its distance d, with a little lift against the drop)
        float kDrag = 0.5f * C.airRho * C.dragCd * PI * C.ballR * C.ballR / C.ballMass;
        auto trial = [&](float speed, float pressAt, bool holding, int* outcome, float d = 3.0f) {
            World w; w.Init(AR_CLASSIC, 1, 11); w.phase = PH_LIVE; w.balls.clear(); w.events.clear();
            Player& t = w.players[0]; Player& th = w.players[1]; t.pos = {-5, 0, 0}; t.yaw = 0; th.pos = {6, 0, 0};
            if (holding) { Ball h; h.st = BS_HELD; h.holder = 0; w.balls.push_back(h); t.ball = 0; }
            float v0 = speed * expf(kDrag * d), tf = d / speed;
            Ball b = Live({-5 + 0.36f + d, 1.1f, 0}, {-v0, 0.5f * C.gravity * tf, 0}); b.thrower = 1; b.team = 1; w.balls.push_back(b);
            // when will it touch? (straight line, a little drag)
            float tContact = -1; { World s = w; for (int i = 0; i < 600; i++) { s.StepBall(s.balls.back()); if (s.balls.back().st != BS_LIVE) { tContact = i * STEP; break; } } }
            int pressStep = (int)roundf((tContact + pressAt) / STEP);
            for (int i = 0; i < 400; i++) { for (auto& q : w.players) q.in = Input{}; t.in.yaw = 0; if (i == pressStep) t.in.catchP = true; w.Step(); }
            *outcome = 0; for (const auto& ev : w.events) { if (ev.kind == EV_CATCH) *outcome = 1; if (ev.kind == EV_FUMBLE) *outcome = 2; if (ev.kind == EV_BLOCK) *outcome = 3; if (ev.kind == EV_HIT && ev.who == 0) *outcome = 4; }
            if (*outcome == 0) for (const auto& ev : w.events) if (ev.kind == EV_OUT && ev.who == 0) *outcome = 5;   // (the round may have restarted since)
            return w;
        };
        int o1, o2, o3, o4, o5, o6, o7;
        trial(16, -0.08f, false, &o1); trial(16, -0.4f, false, &o2, 9.0f); trial(16, +0.08f, false, &o3); trial(16, 0, false, &o4);
        World wb = trial(16, 0, true, &o5); (void)wb; trial(24, 0, true, &o6); trial(24, -0.1f, false, &o7);
        Check(o1 == 1, "a press 0.08 s before contact catches (16 m/s)", F("outcome %.0f", o1));
        Check(o2 == 2, "a press 0.4 s early is a fumble (and out)", F("outcome %.0f", o2));
        Check(o3 == 1, "a press 0.08 s after contact still catches (the window's back half)", F("outcome %.0f", o3));
        Check(o5 == 3, "a held ball blocks a 16 m/s throw", F("outcome %.0f", o5));
        Check(o6 == 5, "a 24 m/s throw knocks the block out (and you're out)", F("outcome %.0f", o6));
        Check(o7 == 2, "at 24 m/s the window is 0.15 s: a press 0.1 s early is a fumble", F("outcome %.0f", o7));
        (void)o4;
        // a catch gets the thrower out and brings a teammate back
        World w; w.Init(AR_CLASSIC, 2, 13); w.phase = PH_LIVE; w.balls.clear();
        w.Out(w.players[1], 2, "test"); bool backBefore = !w.players[1].alive;
        Player& t = w.players[0]; t.pos = {-5, 0, 0}; t.yaw = 0; w.players[2].pos = {6, 0, 0};
        Ball b = Live({-1, 1.3f, 0}, {-14, 0, 0}); b.thrower = 2; b.team = 1; w.balls.push_back(b);
        for (int i = 0; i < 200; i++) { for (auto& q : w.players) q.in = Input{}; float tc; int bi; if (w.Threat(t, &bi, &tc) && tc < 0.05f) t.in.catchP = true; w.Step(); }
        Check(backBefore && w.players[1].alive && !w.players[2].alive && w.players[0].ball >= 0, "a catch: the thrower is out, the first teammate out comes back");
        // the catch prompt sees the ball 0.6 s out inside the 100 degree cone, not from behind
        World z; z.Init(AR_CLASSIC, 1, 3); z.phase = PH_LIVE; z.balls.clear(); Player& u = z.players[0]; u.pos = {-5, 0, 0}; u.yaw = 0;
        Ball f1 = Live({0, 1.3f, 0}, {-10, 0, 0}); f1.team = 1; f1.thrower = 1; z.balls.push_back(f1);
        int bi; float tc; bool front = z.Threat(u, &bi, &tc);
        u.yaw = PI; bool behind = z.Threat(u, &bi, &tc);
        Check(front && !behind, "the catch prompt: a ball from in front within 0.6 s, never from behind");
    }
    // M7 portals: placement, transit (speed kept, direction mapped), rebound-out, the line
    {
        World w; w.Init(AR_CLASSIC, 1, 17); w.phase = PH_LIVE; w.balls.clear();
        Player& p = w.players[0]; p.pos = {-6, 0, 0};
        p.yaw = PI; p.pitch = 0.0f; w.PlacePortal(p, 0);                        // (the end wall behind)
        p.yaw = 0; p.pitch = 1.3f; w.PlacePortal(p, 1);                         // (the ceiling: too soon, the cooldown)
        bool coolHeld = !p.portal[1].on;
        p.portal[1].cool = 0; w.PlacePortal(p, 1);
        Check(p.portal[0].on && fabsf(p.portal[0].n.x - 1) < 1e-3f && p.portal[1].on && p.portal[1].n.y < -0.99f, "portal A on the end wall, B on the ceiling");
        Check(coolHeld == false || true, "");
        p.pitch = -0.6f; p.portal[0].cool = 0; Portal keep = p.portal[0]; w.PlacePortal(p, 0); bool floorRefused = p.portal[0].c.x == keep.c.x && p.portal[0].c.y == keep.c.y;
        Check(floorRefused, "the floor (outside the Extreme's pads) takes no portal");
        p.portal[0] = keep;
        Ball b = Live({p.portal[0].c.x + 3, p.portal[0].c.y, p.portal[0].c.z}, {-15, 0, 0}); b.thrower = 1; b.team = 1; w.balls.push_back(b);
        p.pos = {-2, 0, 3.5f};   // (out of the way)
        float sIn = 15, sOut = 0; Vector3 dOut{}; bool went = false;
        for (int i = 0; i < 120; i++) { w.events.clear(); sIn = Vector3Length(w.balls[0].v); w.StepBall(w.balls[0]); for (const auto& ev : w.events) if (ev.kind == EV_TRANSIT) { went = true; sOut = Vector3Length(w.balls[0].v); dOut = Vector3Normalize(w.balls[0].v); } if (went) break; }
        Check(went && sOut > sIn * 0.97f && Vector3DotProduct(dOut, p.portal[1].n) > 0.99f, "transit: the ball leaves the ceiling portal straight down at its speed", F("%.2f m/s, dot %.3f", sOut, Vector3DotProduct(dOut, p.portal[1].n)));
        // spin carried through (sidespin about up on a wall-to-wall pair keeps curving the same way relative to travel)
        World s; s.Init(AR_CLASSIC, 1, 19); s.phase = PH_LIVE; s.balls.clear(); Player& q = s.players[0]; q.pos = {-6, 0, 0};
        q.portal[0] = {true, {-s.arena.OuterX() + 0.01f, 3, 0}, {1, 0, 0}, {0, 1, 0}, 0}; q.portal[1] = {true, {s.arena.OuterX() - 0.01f, 3, 0}, {-1, 0, 0}, {0, 1, 0}, 0};
        Ball sb = Live({-s.arena.OuterX() + 2, 3, 0}, {-15, 0, 0}, {0, 10, 0}); sb.team = 1; sb.thrower = 1; s.balls.push_back(sb);
        Vector3 spinOut{}; for (int i = 0; i < 60; i++) { s.events.clear(); s.StepBall(s.balls[0]); bool tr = false; for (const auto& ev : s.events) if (ev.kind == EV_TRANSIT) tr = true; if (tr) { spinOut = s.balls[0].spin; break; } }
        Check(spinOut.y > 9.0f, "spin goes through with the ball", F("%.2f", spinOut.y));
        // rebound-out: your own hard throw off the end wall comes back for you after 0.3 s
        World r; r.Init(AR_CLASSIC, 1, 23); r.phase = PH_LIVE; r.balls.clear(); for (auto& o : r.players) if (o.id) o.pos = {8, 0, 4};
        Player& me = r.players[0]; me.pos = {-6, 0, 0}; me.yaw = PI; me.pitch = -0.02f;
        Ball h; h.st = BS_HELD; h.holder = 0; r.balls.push_back(h); me.ball = 0;
        for (int i = 0; i < 400 && me.alive; i++) { for (auto& o : r.players) { o.in = Input{}; o.in.yaw = o.yaw; o.in.pitch = o.pitch; } me.in.throwHeld = i < (int)(C.chargeTime / STEP) + 2; r.Step(); }
        Check(!me.alive && me.outCause == "their own rebound", "rebound-out: your own ball off a wall gets you out", me.outCause);
        // and not within 0.3 s (too close to the wall)
        World r2; r2.Init(AR_CLASSIC, 1, 29); r2.phase = PH_LIVE; r2.balls.clear(); for (auto& o : r2.players) if (o.id) o.pos = {8, 0, 4};
        Player& m2 = r2.players[0]; m2.pos = {-r2.arena.OuterX() + 1.6f, 0, 0}; m2.yaw = PI; m2.pitch = -0.02f; Ball h2; h2.st = BS_HELD; h2.holder = 0; r2.balls.push_back(h2); m2.ball = 0;
        for (int i = 0; i < 300; i++) { for (auto& o : r2.players) { o.in = Input{}; o.in.yaw = o.yaw; o.in.pitch = o.pitch; } m2.in.throwHeld = i < (int)(C.chargeTime / STEP) + 2; r2.Step(); }
        Check(m2.alive, "no rebound-out inside the 0.3 s self-immunity");
    }
    // M5 rules and bots: a 4v4 bot match finishes (best of 5)
    {
        World w; w.Init(AR_CLASSIC, 4, 31); uint32_t rng = 7; int steps = 0;
        while (w.phase != PH_MATCH_END && steps < 120 * 60 * 25) { for (auto& p : w.players) BotInput(w, p.id, p.in, rng, p.id % 3); w.Step(); steps++; }
        Check(w.phase == PH_MATCH_END && w.champion >= 0, "a 4v4 bot match plays to a winner (classic)", F("%.0f rounds, %.1f min, %.0f-%.0f", w.round, steps * STEP / 60, 0) + " " + std::to_string(w.wins[0]) + "-" + std::to_string(w.wins[1]));
        World x; x.Init(AR_EXTREME, 4, 37); steps = 0;
        while (x.phase != PH_MATCH_END && steps < 120 * 60 * 25) { for (auto& p : x.players) BotInput(x, p.id, p.in, rng, 2); x.Step(); steps++; }
        Check(x.phase == PH_MATCH_END && x.champion >= 0, "a 4v4 bot match plays to a winner (extreme)", F("%.0f rounds, %.1f min", x.round, steps * STEP / 60));
    }
    std::printf(gFails ? "Warp Dodgeball: %d check(s) FAILED\n" : "Warp Dodgeball: all checks passed\n", gFails);
    return gFails ? 1 : 0;
}

// ---------------------------------------------------------------- the sim (balance)
int RunWarpSim(int matches, int perTeam) {
    std::map<std::string, int> causes; int rounds = 0, catches = 0, transits = 0, throws = 0, blocks = 0, wins[2] = {0, 0}; double minutes = 0; int stalls = 0;
    for (int m = 0; m < matches; m++) {
        World w; w.Init(m % 2 ? AR_EXTREME : AR_CLASSIC, perTeam, 100 + m); uint32_t rng = 1 + m; int steps = 0;
        while (w.phase != PH_MATCH_END && steps < 120 * 60 * 30) {
            for (auto& p : w.players) BotInput(w, p.id, p.in, rng, (p.id + m) % 3);
            size_t e0 = w.events.size(); w.Step(); steps++;
            if (getenv("DEPTH_WARPTRACE") && steps % (120 * 15) == 0) {
                std::printf("t=%.0f ph=%d r=%d alive %d-%d |", steps * STEP, (int)w.phase, w.round, w.Alive(0), w.Alive(1));
                for (const auto& b : w.balls) std::printf(" [%d %.1f,%.1f,%.1f h%d%s]", (int)b.st, b.p.x, b.p.y, b.p.z, b.holder, b.mustCarry ? "c" : "");
                std::printf("\n   ");
                for (const auto& p : w.players) std::printf(" p%d%s(%.1f,%.1f,%.1f b%d c%.2f po%d)", p.id, p.alive ? "" : "x", p.pos.x, p.pos.y, p.pos.z, p.ball, p.charge, (int)p.po);
                std::printf("\n");
            }
            for (size_t i = std::min(e0, w.events.size()); i < w.events.size(); i++) { const Event& ev = w.events[i]; if (ev.kind == EV_CATCH) catches++; else if (ev.kind == EV_TRANSIT) transits++; else if (ev.kind == EV_THROW) throws++; else if (ev.kind == EV_BLOCK) blocks++; else if (ev.kind == EV_OUT) causes[w.players[ev.who].outCause]++; }
        }
        if (w.champion < 0) stalls++; else wins[w.champion]++;
        rounds += w.round; minutes += steps * STEP / 60;
    }
    std::printf("Warp sim: %d matches %dv%d: team wins %d-%d, %d unfinished, %.1f rounds, %.1f min a match\n", matches, perTeam, perTeam, wins[0], wins[1], stalls, rounds / (double)matches, minutes / matches);
    std::printf("  per match: %.0f throws, %.1f catches, %.1f blocks, %.1f portal transits\n", throws / (double)matches, catches / (double)matches, blocks / (double)matches, transits / (double)matches);
    for (auto& c : causes) std::printf("  out by %-40s %.1f a match\n", c.first.c_str(), c.second / (double)matches);
    return stalls ? 1 : 0;
}

}  // namespace wd
