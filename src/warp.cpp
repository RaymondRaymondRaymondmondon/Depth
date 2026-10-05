// Warp Dodgeball's headless core (see warp.h). The spec's sections map to the pieces here: the arena (MakeArena),
// movement and postures (StepPlayer), throwing and curve (StepThrow), ball physics with continuous collision and the
// Magnus force (StepBall), portals and transit (PlacePortal, inside StepBall), catching (StepCatch, Threat), rules,
// the opening rush, rounds and the sideline (Step, Out, Return), and the bots (warp_bots.cpp).
#include "warp.h"
#include "json.h"
#include "redtide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace wd {

// ---------------------------------------------------------------- the config
static Config LoadCfg() {
    Config c; Json j = LoadJsonFile(rt::DataDir() + "/../warp/warp_config.json");
    const Json& p = j["player"]; const Json& t = j["throw"]; const Json& b = j["ball"]; const Json& k = j["catch"]; const Json& o = j["portal"]; const Json& m = j["match"]; const Json& cl = j["classic"]; const Json& ex = j["extreme"];
    c.height = p["height"].F(c.height); c.radius = p["radius"].F(c.radius); c.walk = p["walk"].F(c.walk); c.sprint = p["sprint"].F(c.sprint); c.crouchSpeed = p["crouch_speed"].F(c.crouchSpeed); c.ladderSpeed = p["ladder_speed"].F(c.ladderSpeed);
    c.jumpApex = p["jump_apex"].F(c.jumpApex); c.airControl = p["air_control"].F(c.airControl); c.gravity = p["gravity"].F(c.gravity); c.crouchH = p["crouch_height"].F(c.crouchH); c.squatH = p["squat_height"].F(c.squatH);
    c.squatTime = p["squat_time"].F(c.squatTime); c.squatCool = p["squat_cooldown"].F(c.squatCool); c.diveDist = p["dive_distance"].F(c.diveDist); c.diveTime = p["dive_time"].F(c.diveTime); c.proneH = p["prone_height"].F(c.proneH);
    c.diveRecover = p["dive_recover"].F(c.diveRecover); c.chargeSlow = p["charge_slow"].F(c.chargeSlow); c.eyeFromTop = p["eye_from_top"].F(c.eyeFromTop);
    c.chargeTime = t["charge_time"].F(c.chargeTime); c.speedMin = t["speed_min"].F(c.speedMin); c.speedAdd = t["speed_add"].F(c.speedAdd); c.loftZero = t["loft_zero_deg"].F(c.loftZero); c.loftFull = t["loft_full_deg"].F(c.loftFull);
    c.overAfter = t["overcharge_after"].F(c.overAfter); c.overRate = t["overcharge_rate_deg"].F(c.overRate); c.overMax = t["overcharge_max_deg"].F(c.overMax); c.releaseTime = t["release_time"].F(c.releaseTime); c.selfImmune = t["self_immunity"].F(c.selfImmune);
    c.spinMax = t["spin_max"].F(c.spinMax); c.magnusK = t["magnus_k"].F(c.magnusK); c.spinDecay = t["spin_decay"].F(c.spinDecay); c.spinBounce = t["spin_bounce"].F(c.spinBounce);
    c.ballR = b["diameter"].F(0.21f) / 2; c.ballMass = b["mass"].F(c.ballMass); c.dragCd = b["drag_cd"].F(c.dragCd); c.airRho = b["air_density"].F(c.airRho); c.bounceFloor = b["bounce_floor"].F(c.bounceFloor); c.bounceWall = b["bounce_wall"].F(c.bounceWall);
    c.friction = b["friction"].F(c.friction); c.restSpeed = b["rest_speed"].F(c.restSpeed);
    c.lookahead = k["lookahead"].F(c.lookahead); c.coneDeg = k["cone_deg"].F(c.coneDeg); c.slowSpeed = k["slow_speed"].F(c.slowSpeed); c.fastSpeed = k["fast_speed"].F(c.fastSpeed);
    c.winSlow = k["window_slow"].F(c.winSlow); c.winMid = k["window_mid"].F(c.winMid); c.winFast = k["window_fast"].F(c.winFast);
    c.portalW = o["width"].F(c.portalW); c.portalH = o["height"].F(c.portalH); c.portalCool = o["cooldown"].F(c.portalCool); c.exitIgnore = o["exit_ignore"].F(c.exitIgnore);
    c.roundTime = m["round_time"].F(c.roundTime); c.roundsToWin = m["rounds_to_win"].I(c.roundsToWin); c.teamSize = m["team_size"].I(c.teamSize); c.attackLine = m["attack_line"].F(c.attackLine);
    c.rushWhistle = m["rush_whistle"].F(c.rushWhistle); c.roundEndPause = m["round_end_pause"].F(c.roundEndPause); c.friendlyFire = m["friendly_fire"].Bool0(c.friendlyFire); c.headshots = m["headshots_count"].Bool0(c.headshots);
    c.cLength = cl["length"].F(c.cLength); c.cWidth = cl["width"].F(c.cWidth); c.cRunoff = cl["runoff"].F(c.cRunoff); c.cWall = cl["wall"].F(c.cWall); c.cCeil = cl["ceiling"].F(c.cCeil); c.cBalls = cl["balls"].I(c.cBalls);
    c.xLength = ex["length"].F(c.xLength); c.xWidth = ex["width"].F(c.xWidth); c.xWall = ex["wall"].F(c.xWall); c.xCeil = ex["ceiling"].F(c.xCeil); c.xBalls = ex["balls"].I(c.xBalls);
    return c;
}
static Config& CfgStore() { static Config c = LoadCfg(); return c; }
const Config& Cfg() { return CfgStore(); }
Config& CfgMutable() { return CfgStore(); }

// ---------------------------------------------------------------- the arenas (spec "Arenas"; mirror-symmetric, enclosed)
Arena MakeArena(int kind) {
    const Config& C = Cfg(); Arena a; a.kind = kind;
    auto panel = [&](Vector3 c, Vector3 n, Vector3 u, float hw, float hh) { a.panels.push_back({c, n, u, hw, hh}); };
    if (kind == AR_CLASSIC) {
        a.halfL = C.cLength / 2; a.halfW = C.cWidth / 2; a.runoff = C.cRunoff; a.wall = C.cWall; a.ceil = C.cCeil;
        float X = a.OuterX(), Z = a.OuterZ();
        // the full end wall behind each team; the upper half (3 to 6 m) of both side walls; the whole ceiling. Not the floor
        panel({-X, a.wall / 2, 0}, {1, 0, 0}, {0, 1, 0}, Z, a.wall / 2);
        panel({X, a.wall / 2, 0}, {-1, 0, 0}, {0, 1, 0}, Z, a.wall / 2);
        panel({0, a.wall * 0.75f, -Z}, {0, 0, 1}, {0, 1, 0}, X, a.wall / 4);
        panel({0, a.wall * 0.75f, Z}, {0, 0, -1}, {0, 1, 0}, X, a.wall / 4);
        panel({0, a.ceil, 0}, {0, -1, 0}, {1, 0, 0}, Z, X);   // (u along x: hw is across, hh along the length)
    } else {
        a.halfL = C.xLength / 2; a.halfW = C.xWidth / 2; a.runoff = 0; a.wall = C.xWall; a.ceil = C.xCeil;
        float X = a.OuterX(), Z = a.OuterZ();
        panel({-X, a.wall / 2, 0}, {1, 0, 0}, {0, 1, 0}, Z, a.wall / 2); panel({X, a.wall / 2, 0}, {-1, 0, 0}, {0, 1, 0}, Z, a.wall / 2);
        panel({0, a.wall / 2, -Z}, {0, 0, 1}, {0, 1, 0}, X, a.wall / 2); panel({0, a.wall / 2, Z}, {0, 0, -1}, {0, 1, 0}, X, a.wall / 2);
        panel({0, a.ceil, 0}, {0, -1, 0}, {1, 0, 0}, Z, X);
        // each half: 3 low cover blocks, 2 pillars, 2 nests (deck at 3.5 m) with a ladder each, 2 deflectors, 2 floor pads (mirrored)
        for (int s = -1; s <= 1; s += 2) {
            auto box = [&](float x, float z, float hx, float hz, float y0, float y1, uint8_t faces, int k) { Box b; b.lo = {s * x - hx, y0, z - hz}; b.hi = {s * x + hx, y1, z + hz}; b.portalFaces = faces; b.kind = k; a.boxes.push_back(b); };
            uint8_t back = s < 0 ? 1 : 2;   // (the face toward its own end wall: -x for the left half... see below)
            back = s < 0 ? (1 << 0) : (1 << 1);
            box(4.5f, -4.0f, 0.5f, 1.0f, 0, 1.0f, back, 1); box(4.5f, 4.0f, 0.5f, 1.0f, 0, 1.0f, back, 1); box(7.5f, 0, 0.5f, 1.0f, 0, 1.0f, back, 1);   // low cover (2 m wide x 1 m)
            box(9.5f, -5.5f, 0.75f, 0.75f, 0, a.wall, 1 << 5, 2); box(9.5f, 5.5f, 0.75f, 0.75f, 0, a.wall, 1 << 4, 2);   // pillars (one side face each)
            box(12.5f, -5.5f, 1.5f, 1.5f, 3.2f, 3.5f, 0, 3); box(12.5f, 5.5f, 1.5f, 1.5f, 3.2f, 3.5f, 0, 3);   // nest decks (3 x 3 at 3.5 m)
            a.ladders.push_back({{s * 11.0f - s * 0.0f, 0, -5.5f}, 3.5f, {(float)s, 0, 0}}); a.ladders.back().base.x = s * 10.9f;
            a.ladders.push_back({{s * 10.9f, 0, 5.5f}, 3.5f, {(float)s, 0, 0}});
            box(6.5f, -6.5f, 1.3f, 0.12f, 0, 2.5f, 0, 4); box(6.5f, 6.5f, 1.3f, 0.12f, 0, 2.5f, 0, 4);   // deflectors (approximated square-on)
            panel({s * 3.5f, 0.0f, -3.0f}, {0, 1, 0}, {1, 0, 0}, 1.0f, 1.0f); panel({s * 3.5f, 0.0f, 3.0f}, {0, 1, 0}, {1, 0, 0}, 1.0f, 1.0f);   // floor pads
            // portal faces of the pieces as panels too (cover backs, a pillar face)
        }
        for (const auto& b : a.boxes) for (int f = 0; f < 6; f++) if ((b.portalFaces >> f) & 1) {
            Vector3 c{(b.lo.x + b.hi.x) / 2, (b.lo.y + b.hi.y) / 2, (b.lo.z + b.hi.z) / 2}, n{}, u{0, 1, 0}; float hw = 0, hh = (b.hi.y - b.lo.y) / 2;
            if (f == 0) { c.x = b.lo.x; n = {-1, 0, 0}; hw = (b.hi.z - b.lo.z) / 2; } else if (f == 1) { c.x = b.hi.x; n = {1, 0, 0}; hw = (b.hi.z - b.lo.z) / 2; }
            else if (f == 4) { c.z = b.lo.z; n = {0, 0, -1}; hw = (b.hi.x - b.lo.x) / 2; } else if (f == 5) { c.z = b.hi.z; n = {0, 0, 1}; hw = (b.hi.x - b.lo.x) / 2; } else continue;
            panel(c, n, u, hw, hh);
        }
    }
    return a;
}

// ---------------------------------------------------------------- small helpers
float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
int World::Alive(int team) const { int n = 0; for (const auto& p : players) if (p.present && p.alive && p.team == team) n++; return n; }
float Player::Height() const { const Config& C = Cfg(); return po == PO_CROUCH ? C.crouchH : po == PO_SQUAT ? C.squatH : (po == PO_DIVE || po == PO_PRONE) ? C.proneH : C.height; }
Vector3 Player::Eye() const { return {pos.x, pos.y + Height() - Cfg().eyeFromTop, pos.z}; }
static Vector3 Right(Vector3 n, Vector3 u) { return Vector3CrossProduct(u, n); }   // (right-handed: n out of the surface, u up, right across)
static float SegDist(Vector3 p, Vector3 a, Vector3 b, Vector3* q = nullptr) { Vector3 ab = Vector3Subtract(b, a); float L2 = Vector3LengthSqr(ab); float u = L2 > 0 ? std::clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / L2, 0.0f, 1.0f) : 0; Vector3 c = Vector3Add(a, Vector3Scale(ab, u)); if (q) *q = c; return Vector3Distance(p, c); }

float World::GroundAt(Vector3 p, float r) const {
    float g = 0;
    for (const auto& b : arena.boxes) if (p.x + r > b.lo.x && p.x - r < b.hi.x && p.z + r > b.lo.z && p.z - r < b.hi.z && b.hi.y <= p.y + 0.35f) g = std::max(g, b.hi.y);
    return g;
}
bool World::RaySolid(Vector3 o, Vector3 d, float maxT, float* tHit) const {
    float best = maxT; bool any = false;
    for (const auto& b : arena.boxes) {
        float t0 = 0, t1 = best; bool ok = true;
        for (int ax = 0; ax < 3 && ok; ax++) {
            float oo = ax == 0 ? o.x : ax == 1 ? o.y : o.z, dd = ax == 0 ? d.x : ax == 1 ? d.y : d.z, lo = ax == 0 ? b.lo.x : ax == 1 ? b.lo.y : b.lo.z, hi = ax == 0 ? b.hi.x : ax == 1 ? b.hi.y : b.hi.z;
            if (fabsf(dd) < 1e-6f) { if (oo < lo || oo > hi) ok = false; continue; }
            float a = (lo - oo) / dd, c = (hi - oo) / dd; if (a > c) std::swap(a, c);
            t0 = std::max(t0, a); t1 = std::min(t1, c); if (t0 > t1) ok = false;
        }
        if (ok && t0 < best) { best = t0; any = true; }
    }
    if (tHit) *tHit = best;
    return any;
}
bool World::RayPanel(Vector3 o, Vector3 d, float maxT, Vector3* hit, int* panel) const {
    float best = maxT; int bi = -1;
    for (int i = 0; i < (int)arena.panels.size(); i++) {
        const Panel& P = arena.panels[i]; float den = Vector3DotProduct(d, P.n); if (den >= -1e-5f) continue;   // (only from the front)
        float t = Vector3DotProduct(Vector3Subtract(P.c, o), P.n) / den; if (t <= 0 || t >= best) continue;
        Vector3 h = Vector3Add(o, Vector3Scale(d, t)), rel = Vector3Subtract(h, P.c), R = Right(P.n, P.u);
        if (fabsf(Vector3DotProduct(rel, R)) > P.hw || fabsf(Vector3DotProduct(rel, P.u)) > P.hh) continue;
        best = t; bi = i;
    }
    if (bi < 0) return false;
    float ts; if (RaySolid(o, d, best, &ts) && ts < best - 0.01f) return false;   // (a dark surface in the way)
    if (hit) *hit = Vector3Add(o, Vector3Scale(d, best)); if (panel) *panel = bi; return true;
}

// ---------------------------------------------------------------- the world
void World::Init(int arenaKind, int perTeam, uint32_t seed) {
    arena = MakeArena(arenaKind); rng = seed ? seed * 2654435761u + 7 : 1; for (int i = 0; i < 3; i++) Rand();
    players.clear();
    int n = std::clamp(perTeam, 1, 6);
    for (int tm = 0; tm < 2; tm++) for (int k = 0; k < n; k++) { Player p; p.id = (int)players.size(); p.team = tm; players.push_back(p); }
    round = 0; wins[0] = wins[1] = 0; champion = -1; events.clear(); t = 0;
    NewRound();
}
void World::NewRound() {
    const Config& C = Cfg(); round++; roundWinner = -1; phase = PH_WARMUP; phaseT = 0;
    sideline[0].clear(); sideline[1].clear();
    int per[2] = {0, 0}; for (const auto& p : players) per[p.team]++;
    int idx[2] = {0, 0};
    for (auto& p : players) {
        int k = idx[p.team]++; float s = p.team == 0 ? -1.0f : 1.0f;
        float z = (k - (per[p.team] - 1) * 0.5f) * std::min(1.6f, arena.halfW * 1.6f / std::max(1, per[p.team]));
        Input keep = p.in; std::string nm = p.name; Portal keepP[2] = {p.portal[0], p.portal[1]}; (void)keepP;
        int id = p.id, tm = p.team, outs = p.outs, cat = p.catches; bool pres = p.present;
        p = Player{}; p.id = id; p.team = tm; p.present = pres; p.name = nm; p.in = keep; p.outs = outs; p.catches = cat;
        p.pos = {s * (arena.halfL - 1.0f), 0, z}; p.yaw = p.team == 0 ? 0 : PI;
    }
    balls.clear();
    int nb = suddenDeath ? 1 : arena.kind == AR_CLASSIC ? C.cBalls : C.xBalls;
    for (int i = 0; i < nb; i++) { Ball b; b.p = {0, C.ballR, (i - (nb - 1) * 0.5f) * std::min(1.4f, arena.halfW * 1.7f / nb)}; b.st = BS_REST; b.mustCarry = true; balls.push_back(b); }
    Emit(EV_ROUND, {0, 0, 0}, -1, -1, (float)round);
}
void World::Out(Player& p, int by, const char* cause) {
    if (!p.alive) return;
    p.alive = false; p.outs++; p.outCause = cause ? cause : "";
    if (p.ball >= 0 && p.ball < (int)balls.size()) { Ball& b = balls[p.ball]; b.holder = -1; b.st = BS_DEAD; b.v = {0, 1, 0}; p.ball = -1; }
    for (auto& q : p.portal) q.on = false;   // (your portals close; a ball in transit finishes)
    sideline[p.team].push_back(p.id); p.sidelineOrder = (int)sideline[p.team].size();
    Emit(EV_OUT, p.pos, p.id, by);
}
void World::Return(int team) {
    if (sideline[team].empty()) return;
    int id = sideline[team].front(); sideline[team].erase(sideline[team].begin());
    Player& p = players[id]; float s = team == 0 ? -1.0f : 1.0f;
    p.alive = true; p.po = PO_STAND; p.pos = {s * (arena.halfL - 0.8f), 0, 0}; p.vel = {}; p.ball = -1; p.sidelineOrder = -1;
    Emit(EV_RETURN, p.pos, id);
}
float World::CatchWindow(float speed) const { const Config& C = Cfg(); return speed < C.slowSpeed ? C.winSlow : speed <= C.fastSpeed ? C.winMid : C.winFast; }

// ---------------------------------------------------------------- movement and postures (spec "Player movement and controls")
void World::StepPlayer(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    p.yaw = in.yaw; p.pitch = std::clamp(in.pitch, -1.45f, 1.45f);
    p.squatCool = std::max(0.0f, p.squatCool - dt); p.poT += dt;
    for (auto& q : p.portal) q.cool = std::max(0.0f, q.cool - dt);
    Vector3 fwd{cosf(p.yaw), 0, sinf(p.yaw)}, rgt{-sinf(p.yaw), 0, cosf(p.yaw)};
    Vector3 wish = Vector3Add(Vector3Scale(fwd, in.moveX), Vector3Scale(rgt, in.moveZ)); float wl = Vector3Length(wish); if (wl > 1) wish = Vector3Scale(wish, 1 / wl);
    // the postures: a dive (a 3 m lunge, then prone 0.8 s); a squat (a tap: 0.4 s at full depth, then up); crouch (held)
    if (p.po == PO_DIVE) { if (p.poT >= C.diveTime) { p.po = PO_PRONE; p.poT = 0; p.vel = {0, 0, 0}; } }
    else if (p.po == PO_PRONE) { if (p.poT >= C.diveRecover) { p.po = PO_STAND; p.poT = 0; } }
    else if (p.po == PO_SQUAT) { if (p.poT >= C.squatTime) { p.po = PO_STAND; p.poT = 0; p.squatCool = C.squatCool; } }
    else if (p.po == PO_LADDER) {}
    else {
        if (in.dive && p.grounded && !p.charging) { p.po = PO_DIVE; p.poT = 0; p.diveDir = wl > 0.1f ? Vector3Normalize(wish) : fwd; p.vel = Vector3Scale(p.diveDir, C.diveDist / C.diveTime); }
        else if (in.squat && p.squatCool <= 0 && p.grounded) { p.po = PO_SQUAT; p.poT = 0; p.vel.x = p.vel.z = 0; }
        else p.po = in.crouch && p.grounded ? PO_CROUCH : PO_STAND;
    }
    // ladders (Extreme): forward into one climbs; the top mantles onto the deck; jump drops off
    if (p.po == PO_LADDER) {
        const Ladder& L = arena.ladders[p.ladder];
        p.pos.x = L.base.x - L.into.x * 0.35f; p.pos.z = L.base.z - L.into.z * 0.35f; p.vel = {};
        p.pos.y += in.moveX * C.ladderSpeed * dt; p.pos.y = std::max(0.0f, p.pos.y);
        if (p.pos.y >= L.top) { p.po = PO_STAND; p.pos = {L.base.x + L.into.x * 0.6f, L.top, L.base.z + L.into.z * 0.6f}; p.grounded = true; p.ladder = -1; }
        else if (in.jump || (p.pos.y <= 0 && in.moveX < 0)) { p.po = PO_STAND; p.ladder = -1; p.vel = Vector3Scale(L.into, -2); }
        return;
    }
    if (p.po == PO_STAND || p.po == PO_CROUCH) for (int i = 0; i < (int)arena.ladders.size(); i++) {
        const Ladder& L = arena.ladders[i];
        if (fabsf(p.pos.x - L.base.x) < 0.6f && fabsf(p.pos.z - L.base.z) < 0.6f && Vector3DotProduct(wish, L.into) > 0.5f && p.pos.y < 0.2f && p.ball < 0) { p.po = PO_LADDER; p.ladder = i; p.poT = 0; return; }
    }
    // horizontal motion: walk / sprint (no charging) / crouch; charging slows to 70%; in the air, 30% control
    if (p.po != PO_DIVE && p.po != PO_PRONE) {
        float spd = p.po == PO_CROUCH ? C.crouchSpeed : p.po == PO_SQUAT ? 0 : (in.sprint && !p.charging) ? C.sprint : C.walk;
        if (p.charging) spd *= C.chargeSlow;
        Vector3 want = Vector3Scale(wish, spd);
        float k = p.grounded ? 1.0f : C.airControl;
        p.vel.x += (want.x - p.vel.x) * std::min(1.0f, dt * 14 * k); p.vel.z += (want.z - p.vel.z) * std::min(1.0f, dt * 14 * k);
        if (in.jump && p.grounded && (p.po == PO_STAND || p.po == PO_CROUCH)) { p.vel.y = sqrtf(2 * C.gravity * C.jumpApex); p.grounded = false; p.po = PO_STAND; }
    }
    p.vel.y -= C.gravity * dt;
    Vector3 np = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
    // the walls and the pieces (a circle in xz against boxes it overlaps in height)
    float X = arena.OuterX() - C.radius, Z = arena.OuterZ() - C.radius;
    np.x = std::clamp(np.x, -X, X); np.z = std::clamp(np.z, -Z, Z);
    for (const auto& b : arena.boxes) {
        if (np.y >= b.hi.y - 0.35f || np.y + p.Height() <= b.lo.y) continue;
        float cx = std::clamp(np.x, b.lo.x, b.hi.x), cz = std::clamp(np.z, b.lo.z, b.hi.z), dx = np.x - cx, dz = np.z - cz, d = sqrtf(dx * dx + dz * dz);
        if (d < C.radius) {
            if (d < 1e-4f) { float px = std::min(np.x - b.lo.x, b.hi.x - np.x), pz = std::min(np.z - b.lo.z, b.hi.z - np.z); if (px < pz) np.x = np.x - b.lo.x < b.hi.x - np.x ? b.lo.x - C.radius : b.hi.x + C.radius; else np.z = np.z - b.lo.z < b.hi.z - np.z ? b.lo.z - C.radius : b.hi.z + C.radius; }
            else { np.x = cx + dx / d * C.radius; np.z = cz + dz / d * C.radius; }
        }
    }
    float g = GroundAt(np, C.radius * 0.7f);
    if (np.y <= g) { np.y = g; if (p.vel.y < 0) p.vel.y = 0; p.grounded = true; } else p.grounded = np.y - g < 0.02f;
    if (np.y + p.Height() > arena.ceil) { np.y = arena.ceil - p.Height(); p.vel.y = std::min(p.vel.y, 0.0f); }
    p.pos = np;
    // the centre line: feet across it is an out
    if (p.alive && ((p.team == 0 && p.pos.x - C.radius * 0.2f > 0) || (p.team == 1 && p.pos.x + C.radius * 0.2f < 0)) && p.pos.y < 0.5f) { Emit(EV_LINE, p.pos, p.id); Out(p, -1, "crossed the centre line"); }
}

// ---------------------------------------------------------------- throwing and curve (spec "Throwing")
void World::StepThrow(Player& p) {
    const Config& C = Cfg(); const Input& in = p.in; float dt = STEP;
    bool canThrow = p.ball >= 0 && p.po != PO_DIVE && p.po != PO_PRONE && p.po != PO_SQUAT && p.po != PO_LADDER && !(in.sprint && Vector3Length({p.vel.x, 0, p.vel.z}) > C.walk + 0.2f);
    if (p.releaseT > 0) {   // the release animation: the ball leaves at its end
        p.releaseT -= dt;
        if (p.releaseT <= 0 && p.ball >= 0) {
            Ball& b = balls[p.ball];
            float ch = p.charge, speed = C.speedMin + C.speedAdd * ch * ch;
            float loft = (C.loftZero + (C.loftFull - C.loftZero) * ch) * DEG2RAD;
            float spread = std::min(C.overMax, std::max(0.0f, p.overT - C.overAfter) * C.overRate) * DEG2RAD;
            float yaw = p.yaw + (Rand() - 0.5f) * 2 * spread, pitch = p.pitch + loft + (Rand() - 0.5f) * 2 * spread;
            Vector3 d{cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
            b.st = BS_LIVE; b.holder = -1; b.thrower = p.id; b.team = p.team; b.viaPortal = b.viaWall = false; b.age = 0; b.ignoreT = 0;
            b.p = Vector3Add(p.Eye(), Vector3Add(Vector3Scale(d, 0.45f), {0, -0.15f, 0}));
            b.v = Vector3Scale(d, speed);
            b.spin = {0, -p.relCurve * ch * C.spinMax, 0};   // (sidespin about the vertical: right curve with a positive input)
            p.ball = -1; p.charge = 0; p.heldT = 0; p.overT = 0;
            Emit(EV_THROW, b.p, p.id, -1, speed);
        }
        return;
    }
    if (!canThrow) { p.charging = false; p.charge = 0; p.heldT = 0; p.overT = 0; return; }
    if (p.ball >= 0 && balls[p.ball].mustCarry) { p.charging = false; return; }   // (a rush ball must go behind the attack line first)
    if (in.throwHeld && !in.cancel) {
        p.charging = true; p.heldT += dt; p.charge = std::min(1.0f, p.heldT / C.chargeTime);
        if (p.charge >= 1) p.overT += dt;
    } else if (p.charging) {
        p.charging = false;
        if (in.cancel) { p.charge = 0; p.heldT = 0; p.overT = 0; return; }
        p.releaseT = C.releaseTime; p.relCurve = std::clamp(in.curve, -1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------- portals (spec "Portal system")
void World::PlacePortal(Player& p, int which) {
    const Config& C = Cfg();
    if (p.portal[which].cool > 0 || suddenDeath) { return; }
    Vector3 hit; int pi;
    if (!RayPanel(p.Eye(), p.Look(), 60, &hit, &pi)) { Emit(EV_PORTAL_FAIL, Vector3Add(p.Eye(), Vector3Scale(p.Look(), 3)), p.id, -1, (float)which); return; }
    const Panel& P = arena.panels[pi]; Vector3 R = Right(P.n, P.u), rel = Vector3Subtract(hit, P.c);
    float x = Vector3DotProduct(rel, R), y = Vector3DotProduct(rel, P.u);
    float hw = C.portalW / 2, hh = C.portalH / 2;
    if (P.hw < hw || P.hh < hh) { Emit(EV_PORTAL_FAIL, hit, p.id, -1, (float)which); return; }   // (the oval doesn't fit)
    x = std::clamp(x, -P.hw + hw, P.hw - hw); y = std::clamp(y, -P.hh + hh, P.hh - hh);   // (slid along to fit flat)
    Vector3 c = Vector3Add(P.c, Vector3Add(Vector3Scale(R, x), Vector3Scale(P.u, y)));
    // a floor or ceiling portal: up runs along your look, so it opens toward you
    Vector3 u = P.u;
    if (fabsf(P.n.y) > 0.9f) { Vector3 f = Vector3Normalize({p.Look().x, 0, p.Look().z}); u = Vector3Length(f) > 0.1f ? f : P.u; }
    p.portal[which] = {true, Vector3Add(c, Vector3Scale(P.n, 0.01f)), P.n, u, C.portalCool};
    Emit(EV_PORTAL_PLACE, c, p.id, -1, (float)which);
}

// ---------------------------------------------------------------- the ball (spec "Ball physics", "Transit", "Rebound out")
void World::StepBall(Ball& b) {
    const Config& C = Cfg(); float dt = STEP;
    if (b.st == BS_HELD) { if (b.holder >= 0) { const Player& h = players[b.holder]; Vector3 f = h.Look(); b.p = Vector3Add(h.Eye(), Vector3Add(Vector3Scale(f, 0.4f), {0, -0.35f, 0})); b.v = {}; } return; }
    if (b.st == BS_REST) { b.v = {}; return; }
    b.age += dt; b.ignoreT = std::max(0.0f, b.ignoreT - dt);
    float area = PI * C.ballR * C.ballR, kDrag = 0.5f * C.airRho * C.dragCd * area / C.ballMass;
    // continuous collision: sub-steps of at most 5 cm (a 26 m/s ball moves 0.22 m a tick)
    float spd = Vector3Length(b.v); int n = std::max(1, (int)ceilf(spd * dt / 0.05f)); float h = dt / n;
    for (int s = 0; s < n; s++) {
        Vector3 v = b.v; float sp = Vector3Length(v);
        Vector3 acc{0, -C.gravity, 0};
        if (sp > 0.01f) acc = Vector3Add(acc, Vector3Scale(v, -kDrag * sp));
        acc = Vector3Add(acc, Vector3Scale(Vector3CrossProduct(b.spin, v), C.magnusK / C.ballMass));   // (Magnus: F = k (spin x velocity))
        b.v = Vector3Add(b.v, Vector3Scale(acc, h));
        Vector3 prev = b.p; b.p = Vector3Add(b.p, Vector3Scale(b.v, h));
        // portals: the centre crossing a portal's plane, inside its oval, while both ends of the pair exist
        bool moved = false;
        // (the plane is the surface pushed out by the ball's radius: the ball goes in as it touches the surface)
        if (b.ignoreT <= 0) for (auto& o : players) {
            if (!o.portal[0].on || !o.portal[1].on) continue;
            for (int e = 0; e < 2 && !moved; e++) {
                const Portal& A = o.portal[e]; const Portal& B = o.portal[1 - e];
                float d0 = Vector3DotProduct(Vector3Subtract(prev, A.c), A.n) - C.ballR, d1 = Vector3DotProduct(Vector3Subtract(b.p, A.c), A.n) - C.ballR;
                if (!(d0 >= 0 && d1 < 0)) continue;
                Vector3 RA = Right(A.n, A.u), cross = Vector3Lerp(prev, b.p, d0 / std::max(1e-6f, d0 - d1)), rel = Vector3Subtract(cross, A.c);
                float lx = Vector3DotProduct(rel, RA), ly = Vector3DotProduct(rel, A.u);
                if ((lx * lx) / (0.25f * C.portalW * C.portalW) + (ly * ly) / (0.25f * C.portalH * C.portalH) > 1) continue;
                // out of the other end: (right, up, normal) -> (-right, up, -normal) in the exit's frame (a 180 degree turn about up)
                Vector3 RB = Right(B.n, B.u);
                auto xf = [&](Vector3 w) { float r = Vector3DotProduct(w, RA), u = Vector3DotProduct(w, A.u), nn = Vector3DotProduct(w, A.n); return Vector3Add(Vector3Add(Vector3Scale(RB, -r), Vector3Scale(B.u, u)), Vector3Scale(B.n, -nn)); };
                b.p = Vector3Add(Vector3Add(B.c, Vector3Add(Vector3Scale(RB, -lx), Vector3Scale(B.u, ly))), Vector3Scale(B.n, C.ballR + 0.02f));
                b.v = xf(b.v); b.spin = xf(b.spin);
                b.viaPortal = true; b.ignoreT = C.exitIgnore;
                Emit(EV_TRANSIT, b.p, o.id, b.thrower, Vector3Length(b.v));
                moved = true;
            }
            if (moved) break;
        }
        if (moved) continue;
        // the room and the pieces: walls and ceiling bounce (0.7); the floor and any walkable top (< 45 degrees) bounce (0.6) and kill a live ball
        float r = C.ballR, X = arena.OuterX() - r, Z = arena.OuterZ() - r;
        auto bounce = [&](Vector3 n, float e, bool ground) {
            float vn = Vector3DotProduct(b.v, n); if (vn >= 0) return;
            Vector3 vt = Vector3Subtract(b.v, Vector3Scale(n, vn));
            b.v = Vector3Add(Vector3Scale(n, -vn * e), Vector3Scale(vt, ground ? 1 - C.friction * 0.25f : 0.95f));
            b.spin = Vector3Scale(b.spin, C.spinBounce);
            if (ground) { if (b.st == BS_LIVE) { b.st = BS_DEAD; Emit(EV_BOUNCE, b.p, -1, b.thrower, 1); } }
            else { if (b.st == BS_LIVE) b.viaWall = true; if (fabsf(vn) > 2) Emit(EV_BOUNCE, b.p, -1, b.thrower, 0); }
        };
        {
            if (b.p.x < -X) { b.p.x = -X; bounce({1, 0, 0}, C.bounceWall, false); } if (b.p.x > X) { b.p.x = X; bounce({-1, 0, 0}, C.bounceWall, false); }
            if (b.p.z < -Z) { b.p.z = -Z; bounce({0, 0, 1}, C.bounceWall, false); } if (b.p.z > Z) { b.p.z = Z; bounce({0, 0, -1}, C.bounceWall, false); }
            if (b.p.y > arena.ceil - r) { b.p.y = arena.ceil - r; bounce({0, -1, 0}, C.bounceWall, false); }
            for (const auto& bx : arena.boxes) {
                Vector3 q{std::clamp(b.p.x, bx.lo.x, bx.hi.x), std::clamp(b.p.y, bx.lo.y, bx.hi.y), std::clamp(b.p.z, bx.lo.z, bx.hi.z)};
                Vector3 d = Vector3Subtract(b.p, q); float L = Vector3Length(d); if (L >= r || L < 1e-6f) continue;
                Vector3 nn = Vector3Scale(d, 1 / L); b.p = Vector3Add(q, Vector3Scale(nn, r));
                bounce(nn, nn.y > 0.707f ? C.bounceFloor : C.bounceWall, nn.y > 0.707f);
            }
        }
        if (b.p.y < r) { b.p.y = r; bounce({0, 1, 0}, C.bounceFloor, true); }
        // the players: a live ball gets the first one it touches out (and then goes dead); a dead ball just knocks off them
        if (b.st == BS_LIVE) for (auto& o : players) {
            if (!o.present || !o.alive) continue;
            bool own = o.id == b.thrower;
            if (own && !(b.viaPortal || b.viaWall)) continue;                      // (only a rebound or a portal shot comes back for you)
            if (own && b.age < C.selfImmune && !b.viaPortal) continue;
            if (!own && o.team == b.team && !C.friendlyFire) continue;
            Vector3 a{o.pos.x, o.pos.y + 0.25f, o.pos.z}, top{o.pos.x, o.pos.y + std::max(0.3f, o.Height() - 0.25f), o.pos.z}, q;
            if (o.po == PO_DIVE || o.po == PO_PRONE) { Vector3 f = Vector3Normalize(o.po == PO_DIVE ? o.diveDir : Vector3{cosf(o.yaw), 0, sinf(o.yaw)}); a = {o.pos.x - f.x * 0.8f, o.pos.y + 0.25f, o.pos.z - f.z * 0.8f}; top = {o.pos.x + f.x * 0.8f, o.pos.y + 0.25f, o.pos.z + f.z * 0.8f}; }
            if (SegDist(b.p, a, top, &q) > Cfg().radius + r) continue;
            if (!C.headshots && b.p.y > o.pos.y + o.Height() - 0.3f) { b.st = BS_DEAD; continue; }
            // holding a ball: a block (the incoming ball goes dead) unless it's hard enough to knock yours out
            if (o.ball >= 0 && !own) {
                float s2 = Vector3Length(b.v);
                if (s2 < C.fastSpeed) { b.st = BS_DEAD; b.v = Vector3Scale(b.v, -0.3f); Emit(EV_BLOCK, b.p, o.id, b.thrower, s2); break; }
                b.st = BS_DEAD; Out(o, b.thrower, "the block was knocked out of their hands"); break;
            }
            // the catch: pressed in the window (or just before, waiting the back half), or a fumble for an early press
            float w = CatchWindow(Vector3Length(b.v));
            if (!own && t - o.catchPressT <= w * 0.5f) {
                b.st = BS_HELD; b.holder = o.id; o.ball = (int)(&b - &balls[0]); b.mustCarry = false; o.catches++;
                Emit(EV_CATCH, b.p, o.id, b.thrower, w);
                if (b.thrower >= 0) Out(players[b.thrower], o.id, "caught");
                Return(o.team);
                break;
            }
            if (!own && t - o.catchPressT <= Cfg().lookahead) { b.st = BS_DEAD; b.v = Vector3Scale(b.v, -0.2f); Emit(EV_FUMBLE, b.p, o.id, b.thrower); Out(o, b.thrower, "fumbled the catch"); break; }
            if (!own) { o.pendingHit = (int)(&b - &balls[0]); o.pendingT = w * 0.5f; b.st = BS_DEAD; b.v = Vector3Scale(b.v, -0.25f); break; }   // (a late press can still make it a catch)
            b.st = BS_DEAD; b.v = Vector3Scale(b.v, -0.25f); Emit(EV_HIT, b.p, o.id, b.thrower); Out(o, b.thrower, "their own rebound"); break;
        }
    }
    b.spin = Vector3Scale(b.spin, 1 - C.spinDecay * dt);
    // a dead ball on the ground that has slowed comes to rest (and can be picked up)
    if (b.st == BS_DEAD && b.p.y <= C.ballR + GroundAt(b.p, 0.01f) + 0.02f && Vector3Length(b.v) < C.restSpeed * 4) { b.v = Vector3Scale(b.v, 1 - dt * 3); if (Vector3Length(b.v) < C.restSpeed) { b.st = BS_REST; b.v = {}; } }
}

// a pending hit: the back half of the window for a late press, else the ordinary hit
void World::StepCatch(Player& p) {
    if (p.in.catchP) p.catchPressT = t;
    if (p.pendingHit < 0) return;
    p.pendingT -= STEP;
    Ball& b = balls[p.pendingHit];
    if (p.catchPressT > t - STEP * 0.5f && p.alive && p.ball < 0) {   // (pressed just now: inside the window's back half)
        b.st = BS_HELD; b.holder = p.id; p.ball = p.pendingHit; b.mustCarry = false; p.catches++;
        Emit(EV_CATCH, b.p, p.id, b.thrower, 0);
        if (b.thrower >= 0) Out(players[b.thrower], p.id, "caught");
        Return(p.team); p.pendingHit = -1; return;
    }
    if (p.pendingT <= 0) { Emit(EV_HIT, p.pos, p.id, b.thrower); Out(p, b.thrower, "hit"); p.pendingHit = -1; }
}

bool World::Threat(const Player& p, int* ballOut, float* tContact) const {
    const Config& C = Cfg(); if (!p.alive || p.ball >= 0 || p.po == PO_DIVE || p.po == PO_PRONE || p.po == PO_SQUAT || p.po == PO_LADDER) return false;
    int bi = -1; float bt = 1e9f;
    Vector3 face{cosf(p.yaw), 0, sinf(p.yaw)};
    for (int i = 0; i < (int)balls.size(); i++) {
        const Ball& b = balls[i]; if (b.st != BS_LIVE || (b.team == p.team && b.thrower != p.id)) continue;
        if (b.thrower == p.id && !(b.viaPortal || b.viaWall)) continue;
        // straight-line prediction (gravity included) to the body's axis, 0.6 s ahead
        for (float tt = 0; tt <= C.lookahead; tt += 0.02f) {
            Vector3 q{b.p.x + b.v.x * tt, b.p.y + b.v.y * tt - 0.5f * C.gravity * tt * tt, b.p.z + b.v.z * tt};
            Vector3 a{p.pos.x, p.pos.y + 0.25f, p.pos.z}, top{p.pos.x, p.pos.y + p.Height() - 0.25f, p.pos.z};
            if (SegDist(q, a, top) <= C.radius + C.ballR + 0.05f) {
                Vector3 from = Vector3Normalize({-b.v.x, 0, -b.v.z});
                if (Vector3DotProduct(from, face) >= cosf(C.coneDeg * 0.5f * DEG2RAD) && tt < bt) { bt = tt; bi = i; }
                break;
            }
        }
    }
    if (bi < 0) return false;
    if (ballOut) *ballOut = bi; if (tContact) *tContact = bt; return true;
}

// ---------------------------------------------------------------- the step and the match (spec "Match structure")
void World::Step() {
    const Config& C = Cfg(); t += STEP; phaseT += STEP;
    if (phase == PH_MATCH_END) return;
    if (phase == PH_ROUND_END) { if (phaseT >= C.roundEndPause) { if (champion >= 0) phase = PH_MATCH_END; else NewRound(); } return; }
    if (phase == PH_WARMUP) { for (auto& p : players) { p.vel = {}; p.yaw = p.in.yaw; p.pitch = p.in.pitch; } if (phaseT >= C.rushWhistle) { phase = PH_RUSH; phaseT = 0; Emit(EV_WHISTLE, {0, 0, 0}); } return; }
    for (auto& p : players) if (p.present && p.alive) {
        StepPlayer(p);
        if (!p.alive) continue;
        if (p.in.portalA) PlacePortal(p, 0);
        if (p.in.portalB) PlacePortal(p, 1);
        StepThrow(p);
        // pick up: walk over a resting ball on your side (in the rush, the centre line's are anyone's), hands empty
        if (p.ball < 0 && p.po != PO_LADDER) for (int i = 0; i < (int)balls.size(); i++) {
            Ball& b = balls[i]; if (b.st != BS_REST) continue;
            bool mySide = (p.team == 0 && b.p.x <= 0.15f) || (p.team == 1 && b.p.x >= -0.15f);
            if (!mySide) continue;
            if (Vector2Distance({p.pos.x, p.pos.z}, {b.p.x, b.p.z}) < C.radius + 0.35f && b.p.y < p.pos.y + 1.2f) { b.st = BS_HELD; b.holder = p.id; p.ball = i; Emit(EV_PICKUP, b.p, p.id); break; }
        }
        // a rush ball, carried back behind your attack line, can be thrown
        if (p.ball >= 0 && balls[p.ball].mustCarry && fabsf(p.pos.x) >= C.attackLine) balls[p.ball].mustCarry = false;
    }
    for (auto& p : players) if (p.present) StepCatch(p);
    for (auto& b : balls) StepBall(b);
    if (phase == PH_RUSH && phaseT > 0.1f) phase = PH_LIVE;
    // the round: one team gone, or the clock; a timeout goes to the team with more standing; a tie, sudden death
    int a0 = Alive(0), a1 = Alive(1);
    int winner = -1; bool over = false;
    if (a0 == 0 || a1 == 0) { over = true; winner = a0 > 0 ? 0 : a1 > 0 ? 1 : -1; }
    else if (phaseT >= C.roundTime && phase == PH_LIVE) { over = true; winner = a0 > a1 ? 0 : a1 > a0 ? 1 : -1; }
    if (over) {
        if (winner < 0) { suddenDeath = true; Emit(EV_ROUND, {0, 0, 0}, -1, -1, -1); round--; NewRound(); return; }   // (sudden death: one ball, no portals; the round again)
        suddenDeath = false; roundWinner = winner; wins[winner]++; phase = PH_ROUND_END; phaseT = 0;
        if (wins[winner] >= C.roundsToWin) champion = winner;
    }
}

}  // namespace wd
