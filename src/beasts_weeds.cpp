// ============================================================================
//  DEPTH - the Weeds: the user's food web merged with ParkourReference1.3's Seaweed ("5. The Seaweed").
//
//  The user's web stays the core (plankton -> seahorses -> mermen and the shark; barracuda -> mermen; rays shock,
//  and the electrified dead feed crabs and fungus). From 1.3: the tiger shark is now the Leviathan Tiger Shark, which
//  tracks blood across the whole forest through the scent grid; the scavenger crab is the Bristle-Crab, which crunches
//  underfoot (a sound every hunter hears). New: the Goliath Manatee (a passive giant that follows the scent of grazed
//  blood-kelp and clears the tangle-vine it swims through; its back is a mover), the Mimic Octopus-Stalker (flawless
//  camouflage on kelp stalks; it strikes whatever passes fast), the Harpoon Mantis (a cliff burrow, a bullet-fast
//  harpoon strike with a clear windup; popping an air-weed near it stuns it), Silver-Fin Sardines (big reactive
//  schools: swim inside one and hunters lose you), and the flora: blood-kelp (grazed, it bleeds scent), luminescent
//  anemones (a safe light; predators that crash into one are stung and slowed), air-weed (pop it: a buoyant launch
//  straight up), tangle-vine (a drag-thicket only the manatee clears) and spore-pods (a blinding cloud).
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;
bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
bool Clear(const NavGrid& N, Vector2 at, int rx, int ry) {
    int cx = (int)floorf(at.x / T), cy = (int)floorf(at.y / T);
    for (int dy = -ry; dy <= ry; dy++) for (int dx = -rx; dx <= rx; dx++) if (!N.Open(cx + dx, cy + dy)) return false;
    return true;
}
Vector2 Lerp(Vector2 a, Vector2 b, float u) { u = Clamp01(u); return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }
bool SegmentHits(Vector2 a, Vector2 b, float r, Rectangle box) { for (int k = 0; k <= 10; k++) if (CheckCollisionCircleRec(Lerp(a, b, k / 10.0f), r, box)) return true; return false; }

// ---------------------------------------------------------------- the Leviathan: it smells blood anywhere in the forest
void LeviathanHook(BeastWorld& W, int i, float dt) {
    Beast& b = W.beasts[i];
    b.special -= dt;
    if (b.special > 0 || b.hunger < 0.3f) return;
    b.special = 1.5f;
    float best = 0.4f; int bx = -1, by = -1;
    for (int y = 1; y < W.scent.h - 1; y++) for (int x = 1; x < W.scent.w - 1; x++) { float v = W.scent.blood[y * W.scent.w + x]; if (v > best) { best = v; bx = x; by = y; } }
    if (bx >= 0) Remember(b, MEM_FOOD, -3, 0, {bx * W.scent.cell + 16, by * W.scent.cell + 16}, {0, 0}, 0.9f, W.time);
}

// ---------------------------------------------------------------- the Goliath Manatee
void ManateeHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    // the grazed blood-kelp it can smell, if any is close enough; else it browses along
    Vector2 goal{b.pos.x + b.facing * 5 * T, b.anchor.y};
    for (const auto& o : W.beasts) if (Alive(o) && o.species == WS_BLOODKELP && o.flashT > 0 && Dist(o.pos, b.pos) < 16 * T) { goal = {o.pos.x, o.pos.y - 2 * T}; break; }
    Vector2 d = Sub(goal, b.pos);
    Vector2 want = Len(d) < 8 ? Vector2{0, 0} : Mul(Norm(d), 40);
    b.vel = Add(b.vel, Mul(Sub(want, b.vel), std::min(1.0f, dt * 1.5f)));
    Vector2 np = Add(b.pos, Mul(b.vel, dt));
    if (Clear(W.nav, np, 2, 1) && np.x > 3 * T && np.x < std::min(W.nav.w * T, W.limitX) - 3 * T) b.pos = np;
    else { b.facing = -b.facing; b.vel = {0, 0}; }
    if (fabsf(b.vel.x) > 4) b.facing = b.vel.x > 0 ? 1.0f : -1.0f;
    for (auto& o : W.beasts) // it browses the tangle-vine away as it goes
        if (Alive(o) && o.species == WS_TANGLE && fabsf(o.pos.x - b.pos.x) < 40 && b.pos.y > o.pos.y - 3 * T - 24 && b.pos.y < o.pos.y + 8) { o.life = BeastLife::Gone; if (!p.verifying) PlatBurst(p, o.pos, 16, Color{60, 100, 50, 255}, 120, 0.8f, 3); }
    b.act = BeastAct::Wander;
    p.movers.push_back({{b.pos.x - 50, b.pos.y - 22, 100, 6}, b.vel});
}

// ---------------------------------------------------------------- the Mimic Octopus-Stalker: an ambush on the fast lines
void OctoHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.pos = b.anchor; b.vel = {0, 0};
    bool lit = false, masked = false;
    for (const auto& o : W.beasts) {
        if (!Alive(o)) continue;
        if (o.species == WS_LANEMONE && Dist(o.pos, b.anchor) < 4.5f * T) lit = true;
        if (o.species == WS_SARDINE && dv.alive && Dist(o.pos, dv.pos) < 40) masked = true;
    }
    switch (b.act) {
    default: b.act = BeastAct::Ambush; break;
    case BeastAct::Ambush:
        b.special = lit ? 0.0f : 1.0f; // in the anemone's light its colours don't match the kelp
        b.territory = b.anchor;
        if (lit || masked || b.cooldown > 0 || !dv.alive) break;
        if (Dist(dv.pos, b.anchor) < 2.6f * T && Len(dv.vel) > 280) { b.act = BeastAct::Coil; b.actT = 0; }
        break;
    case BeastAct::Coil: b.special = 0; if (b.actT > 0.25f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = dv.alive ? Add(dv.pos, Mul(dv.vel, 0.15f)) : b.anchor; if (Dist(b.goal, b.anchor) > 3 * T) b.goal = Add(b.anchor, Mul(Norm(Sub(b.goal, b.anchor)), 3 * T)); } break;
    case BeastAct::Strike: b.territory = Lerp(b.anchor, b.goal, b.actT / 0.15f); if (b.actT > 0.45f) { b.act = BeastAct::Wander; b.actT = 0; b.cooldown = 3; } break;
    case BeastAct::Wander: b.territory = Lerp(b.goal, b.anchor, b.actT / 0.6f); if (b.actT > 0.6f) b.act = BeastAct::Ambush; break;
    }
    (void)dt;
}

// ---------------------------------------------------------------- the Harpoon Mantis: a cliff burrow, a bullet-fast strike
void HarpoonHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.pos = b.anchor; b.vel = {0, 0};
    b.stunT = std::max(0.0f, b.stunT - dt);
    switch (b.act) {
    default: b.act = BeastAct::Ambush; break;
    case BeastAct::Ambush:
        b.territory = b.anchor;
        if (b.stunT > 0 || b.cooldown > 0 || !dv.alive) break;
        if (Dist(dv.pos, b.anchor) < 7 * T && (dv.pos.x - b.anchor.x) * b.facing > 0 && W.nav.LineOfSight(b.anchor, dv.pos)) { b.act = BeastAct::Coil; b.actT = 0; b.goal = dv.pos; }
        break;
    case BeastAct::Coil: // the windup: it rears back in its burrow, clubs cocked - its aim is fixed now
        if (b.stunT > 0) { b.act = BeastAct::Ambush; break; }
        if (b.actT > 0.55f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = Add(b.anchor, Mul(Norm(Sub(b.goal, b.anchor)), 9 * T)); W.sounds.push_back({b.anchor, 1.1f, 0.3f, i}); }
        break;
    case BeastAct::Strike: {
        b.territory = Lerp(b.anchor, b.goal, b.actT / 0.2f);
        int tx = (int)floorf(b.territory.x / T), ty = (int)floorf(b.territory.y / T);
        if (W.nav.Solid(tx, ty)) b.goal = b.territory; // stops at the rock
        for (int j = 0; j < (int)W.beasts.size(); j++) { Beast& o = W.beasts[j]; if (j != i && Alive(o) && !o.hidden && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && Dist(o.pos, b.territory) < 12) Kill(W, p, j, i); }
        if (b.actT > 0.5f) { b.act = BeastAct::Wander; b.actT = 0; }
        break;
    }
    case BeastAct::Wander: b.territory = Lerp(b.goal, b.anchor, b.actT / 0.5f); if (b.actT > 0.5f) { b.act = BeastAct::Ambush; b.cooldown = 2.5f; } break;
    }
}

// ---------------------------------------------------------------- flora, the bristle-crab's crunch
void WeedsFlora(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    b.flashT -= dt;
    switch (b.species) {
    case WS_BLOODKELP: // grazed, it bleeds - a scent the manatee follows, and the Leviathan too
        if (dv.alive && b.cooldown <= 0 && Len(dv.vel) > 150 && CheckCollisionRecs({b.pos.x - 6, b.pos.y - 40, 12, 40}, box)) {
            b.cooldown = 10; b.flashT = 18;
            W.scent.Emit(W.scent.blood, {b.pos.x, b.pos.y - 20}, 25);
        }
        if (b.flashT > 0) W.scent.Emit(W.scent.blood, {b.pos.x, b.pos.y - 20}, 2 * dt);
        break;
    case WS_LANEMONE: // a safe light; a predator that crashes into it is stung and slowed
        W.lights.push_back({{b.pos.x, b.pos.y - 12}, 4 * T, 0.8f});
        for (auto& o : W.beasts) if (Alive(o) && !o.hidden && !Flora(W, o) && Sp(W.biome, o.species).lethal && Dist(o.pos, {b.pos.x, b.pos.y - 10}) < 18 + Sp(W.biome, o.species).radius) { o.stunT = std::max(o.stunT, 2.5f); o.vel = Mul(o.vel, 0.3f); }
        break;
    case WS_AIRWEED: // pop it: a burst of buoyancy that throws you straight up - and knocks a nearby mantis senseless
        if (b.act == BeastAct::Drift) { if (b.actT > 12) b.act = BeastAct::Idle; break; }
        if (dv.alive && CheckCollisionCircleRec({b.pos.x, b.pos.y - 10}, 12, box)) {
            b.act = BeastAct::Drift; b.actT = 0;
            p.vel.y = -900; p.onGround = false; p.dashReady = true;
            if (p.pose == 1 || p.pose == 2 || p.pose == 4) p.pose = 0;
            W.sounds.push_back({b.pos, 0.6f, 0.3f, i});
            for (auto& o : W.beasts) if (Alive(o) && o.species == WS_HMANTIS && Dist(o.pos, b.pos) < 5 * T) { o.stunT = 8; if (o.act == BeastAct::Coil) o.act = BeastAct::Ambush; }
            if (!p.verifying) PlatBubbles(p, {b.pos.x, b.pos.y - 10}, 16);
        }
        break;
    case WS_TANGLE: // a thicket that drags at everything in it - only the manatee gets through it cleanly
        if (dv.alive && CheckCollisionRecs({b.pos.x - 16, b.pos.y - 3 * T, 32, 3 * T}, box)) p.vel = Mul(p.vel, 0.85f);
        for (auto& o : W.beasts) if (Alive(o) && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && fabsf(o.pos.x - b.pos.x) < 16 && o.pos.y < b.pos.y && o.pos.y > b.pos.y - 3 * T) o.vel = Mul(o.vel, 0.8f);
        break;
    case WS_SPOREPOD: // ruptured: the water goes blind
        if (b.act == BeastAct::Drift) { if (b.actT > 20) b.act = BeastAct::Idle; break; }
        if (dv.alive && Len(dv.vel) > 150 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 11, box)) { b.act = BeastAct::Drift; b.actT = 0; AddCloud(W, {b.pos.x, b.pos.y - 16}, 90, 4.0f, 1); W.sounds.push_back({b.pos, 0.3f, 0.3f, i}); }
        break;
    default: break;
    }
    b.cooldown -= 0; // (the engine ticks cooldown)
}

}  // namespace

void Weeds13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case WS_SHARK: LeviathanHook(W, i, dt); break;
    case WS_CRAB: { // a bristle-crab crunching underfoot: every hunter nearby hears it
        Diver dv = SeeDiver(p);
        if (dv.alive && p.onGround && b.cooldown <= 0 && CheckCollisionCircleRec(b.pos, 8, PlatDiverBox(p))) {
            b.cooldown = 1.5f;
            W.sounds.push_back({b.pos, 0.6f, 0.4f, -1});
            Remember(b, MEM_THREAT, BEAST_DIVER, 0, dv.pos, dv.vel, 1.0f, W.time);
        }
        break;
    }
    case WS_MANATEE: ManateeHook(W, p, i, dt); break;
    case WS_OCTOSTALKER: OctoHook(W, p, i, dt); break;
    case WS_HMANTIS: HarpoonHook(W, p, i, dt); break;
    default: if (Flora(W, b)) WeedsFlora(W, p, i, dt); break;
    }
}

bool Weeds13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive || b.stunT > 0) return false;
    if (b.species == WS_OCTOSTALKER) return b.act == BeastAct::Strike && SegmentHits(b.anchor, b.territory, 9, diver);
    if (b.species == WS_HMANTIS) return b.act == BeastAct::Strike && CheckCollisionCircleRec(b.territory, 9, diver);
    return false;
}

// The sardines' cover: swimming inside a school, the hunters lose the thread of you.
void Weeds13Tick(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    if (!dv.alive) return;
    int n = 0;
    for (const auto& o : W.beasts) if (Alive(o) && o.species == WS_SARDINE && Dist(o.pos, dv.pos) < 48) n++;
    if (n >= 5) for (auto& o : W.beasts) for (auto& m : o.mem) if (m.source == BEAST_DIVER) m.strength *= std::max(0.0f, 1.0f - 3.0f * dt);
}

// ---------------------------------------------------------------- the planner (at build time)
void Weeds13Spawn(BeastWorld& W, PlatformState& p) {
    Spots S; S.Build(W, p, 12, W.nav.w - 8);
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].den = -1; W.beasts[k].act = BeastAct::Idle; W.beasts[k].pers.abnormal = Abnormal::None; return k; };
    auto root = [&](Vector2 f) { return Vector2{f.x, (f.y + 1) * T}; };
    int limit = (int)(std::min((float)W.nav.w * T, W.limitX) / T) - 6;
    // mimic octopus-stalkers on kelp stalks ('w') - the fast lines through the forest
    int octo = 0;
    for (int x = 16; x < limit && octo < 4; x++)
        for (int y = 2; y < W.nav.h - 2; y++) {
            if (PlatTileAt(p, x, y) != 'w' || PlatTileAt(p, x, y - 1) == 'w' || Hash(s, 10100 + x) > 0.12f) continue;
            int k = NewBeast(W, WS_OCTOSTALKER, {x * T + 16, y * T + 40.0f});
            Beast& o = W.beasts[k]; o.anchor = o.pos; o.den = -1; o.act = BeastAct::Ambush; o.special = 1; o.pers.abnormal = Abnormal::None;
            if (Hash(s, 10150 + x) < 0.35f) plant(WS_LANEMONE, {x * T + 16 + 2 * T, (float)W.nav.FloorBelow(x + 2, y) * T}); // a light nearby: the counter
            octo++; x += 25; break;
        }
    // harpoon mantises in burrows on the cliffs (a wall face just over the sea floor), each with air-weed in reach
    int mantis = 0;
    for (int x = 18; x < limit && mantis < 3; x++) {
        if (Hash(s, 10200 + x) > 0.1f) continue;
        for (int y = 3; y < W.nav.h - 2; y++)
            for (int sd = -1; sd <= 1; sd += 2) {
                if (!W.nav.Solid(x, y) || !W.nav.Open(x + sd, y) || !W.nav.Open(x + sd * 2, y) || W.nav.FloorBelow(x + sd, y) - y > 2) continue;
                int k = NewBeast(W, WS_HMANTIS, {sd > 0 ? (x + 1) * T + 4 : x * T - 4, y * T + 16.0f});
                Beast& m = W.beasts[k]; m.anchor = m.pos; m.facing = (float)sd; m.den = -1; m.act = BeastAct::Ambush; m.pers.abnormal = Abnormal::None;
                int ax = x + sd * 3, af = W.nav.FloorBelow(ax, y - 2);
                if (af < W.nav.h) plant(WS_AIRWEED, {ax * T + 16.0f, af * T});
                mantis++; y = W.nav.h; x += 30; break;
            }
    }
    // the rest along the sea floor
    for (size_t i = 0; i < S.floor.size(); i += 5) {
        float h = Hash(s, 10300 + (unsigned)i);
        Vector2 f = S.floor[i];
        if (h < 0.09f) plant(WS_BLOODKELP, root(f));
        else if (h < 0.15f) plant(WS_LANEMONE, root(f));
        else if (h < 0.23f) plant(WS_AIRWEED, root(f));
        else if (h < 0.29f) plant(WS_SPOREPOD, root(f));
        else if (h < 0.33f) { plant(WS_TANGLE, root(f)); if (i + 8 < S.floor.size()) plant(WS_BLOODKELP, root(S.floor[i + 8])); } // a thicket, and the kelp that would bring the manatee
    }
    // sardine schools, and the manatee where there's open water
    for (int k = 0; k < 2; k++) { Vector2 c = S.Above(W, S.At(0.25f + 0.45f * k), 4); for (int j = 0; j < 12; j++) { int b = NewBeast(W, WS_SARDINE, {c.x + (j % 4) * 10.0f, c.y + (j / 4) * 8.0f}); W.beasts[b].school = 600 + k; } }
    for (int tries = 0; tries < 30; tries++) {
        Vector2 f = S.At(0.3f + 0.4f * Hash(s, 10400 + tries));
        Vector2 at{f.x, (f.y - 3) * T + 16};
        if (!Clear(W.nav, at, 2, 1)) continue;
        int b = NewBeast(W, WS_MANATEE, at); W.beasts[b].anchor = at; W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None;
        break;
    }
}

// ---------------------------------------------------------------- depth.exe --verify-weeds-ecosystem (the 1.3 part)
bool VerifyWeeds13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-weeds13: FAILED - %s", what); ok = false; };
    auto bench = [](PlatformState& q, int w, int h) {
        q = PlatformState{};
        q.level = PL_WEEDS; q.w = w; q.h = h;
        q.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { q.tiles[h - 1][x] = '#'; q.tiles[h - 2][x] = '#'; q.tiles[0][x] = '#'; }
        for (int y = 0; y < h; y++) { q.tiles[y][0] = '#'; q.tiles[y][w - 1] = '#'; }
        q.pos = {-5000, -5000}; q.deathTimer = 1;
    };
    auto build = [](PlatformState& q) {
        BeastWorld& Q = q.fauna;
        Q = BeastWorld{}; Q.biome = PL_WEEDS; Q.active = true; Q.seed = 71; Q.rng = 7171;
        Q.nav.w = q.w; Q.nav.h = q.h; Q.nav.solid.assign((size_t)q.w * q.h, 0); Q.nav.hazard.assign((size_t)q.w * q.h, 0);
        for (int y = 0; y < q.h; y++) for (int x = 0; x < q.w; x++) Q.nav.solid[y * q.w + x] = PlatSolid(q, x, y) ? 1 : 0;
        Q.scent.Init(q.w, q.h, T); Q.apexT = -1e9f;
    };
    auto diverAt = [](PlatformState& q, Vector2 c, Vector2 v) { q.deathTimer = 0; Rectangle r = PlatDiverBox(q); q.pos.x += c.x - (r.x + r.width / 2); q.pos.y += c.y - (r.y + r.height / 2); q.vel = v; };
    auto calm = [](Beast& b) { b.pers.abnormal = Abnormal::None; b.den = -1; };
    const float dt = 1 / 60.0f;
    for (unsigned seed : {707u, 708u, 709u}) {
        PlatformState r; r.level = PL_WEEDS; r.layout = {(int)seed, 100};
        PlatBuildLevel(r);
        int n[WS_COUNT] = {0};
        for (const auto& b : r.fauna.beasts) if (b.life == BeastLife::Alive) n[b.species]++;
        TraceLog(LOG_WARNING, "verify-weeds13: weeds #%u has %d leviathans, %d manatee, %d octo-stalkers, %d harpoon mantis, %d sardines; %d blood-kelp, %d anemone, %d air-weed, %d tangle, %d spore-pod", seed, n[WS_SHARK], n[WS_MANATEE], n[WS_OCTOSTALKER], n[WS_HMANTIS], n[WS_SARDINE], n[WS_BLOODKELP], n[WS_LANEMONE], n[WS_AIRWEED], n[WS_TANGLE], n[WS_SPOREPOD]);
    }
    PlatformState q;
    // 1) the octopus-stalker strikes what passes it fast, not what drifts by slowly; an anemone's light gives it away
    for (int c = 0; c < 3; c++) {
        bench(q, 50, 16); build(q);
        int o = NewBeast(q.fauna, WS_OCTOSTALKER, {20 * T, 10 * T}); calm(q.fauna.beasts[o]); q.fauna.beasts[o].anchor = {20 * T, 10 * T}; q.fauna.beasts[o].act = BeastAct::Ambush;
        if (c == 2) { int a = NewBeast(q.fauna, WS_LANEMONE, {20 * T, 14 * T}); calm(q.fauna.beasts[a]); }
        bool hit = false;
        for (int f = 0; f < 90; f++) { diverAt(q, {21.5f * T, 10 * T}, {c == 1 ? 60.0f : 360.0f, 0}); BeastsUpdate(q, dt); if (BeastsTouchDiver(q, PlatDiverBox(q))) hit = true; }
        if (c == 0 && !hit) fail("the octopus-stalker didn't strike a diver speeding past it");
        if (c == 1 && hit) fail("the octopus-stalker struck a diver drifting past slowly");
        if (c == 2 && hit) fail("the octopus-stalker struck from inside an anemone's light");
    }
    // 2) the harpoon mantis: a windup, then a strike down its line; an air-weed popped nearby knocks it senseless
    {
        bench(q, 50, 16); build(q);
        for (int y = 8; y < 14; y++) q.tiles[y][10] = '#';
        build(q);
        int m = NewBeast(q.fauna, WS_HMANTIS, {11 * T + 4, 13 * T + 16}); calm(q.fauna.beasts[m]); q.fauna.beasts[m].anchor = {11 * T + 4, 13 * T + 16}; q.fauna.beasts[m].facing = 1;
        bool hit = false;
        for (int f = 0; f < 90; f++) { diverAt(q, {15 * T, 13 * T + 16}, {0, 0}); BeastsUpdate(q, dt); if (BeastsTouchDiver(q, PlatDiverBox(q))) hit = true; }
        if (!hit) fail("the harpoon mantis didn't strike a diver in front of its burrow");
        bench(q, 50, 16); for (int y = 8; y < 14; y++) q.tiles[y][10] = '#'; build(q);
        m = NewBeast(q.fauna, WS_HMANTIS, {11 * T + 4, 13 * T + 16}); calm(q.fauna.beasts[m]); q.fauna.beasts[m].anchor = {11 * T + 4, 13 * T + 16}; q.fauna.beasts[m].facing = 1;
        int aw = NewBeast(q.fauna, WS_AIRWEED, {14 * T, 14 * T}); calm(q.fauna.beasts[aw]);
        diverAt(q, {14 * T, 14 * T - 10}, {0, 0}); BeastsUpdate(q, dt);
        if (q.fauna.beasts[m].stunT <= 0) fail("popping an air-weed didn't stun the harpoon mantis");
        if (q.vel.y > -600) fail("popping an air-weed didn't launch the diver upward");
    }
    // 3) blood-kelp draws the manatee, which clears the tangle-vine; the Leviathan smells the blood from far off
    {
        bench(q, 80, 20); build(q);
        int bk = NewBeast(q.fauna, WS_BLOODKELP, {40 * T, 18 * T}); calm(q.fauna.beasts[bk]);
        int tv = NewBeast(q.fauna, WS_TANGLE, {34 * T, 18 * T}); calm(q.fauna.beasts[tv]);
        int ma = NewBeast(q.fauna, WS_MANATEE, {28 * T, 15 * T + 16}); calm(q.fauna.beasts[ma]); q.fauna.beasts[ma].anchor = {28 * T, 15 * T + 16};
        int sh = NewBeast(q.fauna, WS_SHARK, {70 * T, 10 * T}); calm(q.fauna.beasts[sh]); q.fauna.beasts[sh].hunger = 0.9f;
        diverAt(q, {40 * T, 17 * T}, {250, 0}); BeastsUpdate(q, dt);
        q.deathTimer = 1; q.pos = {-5000, -5000};
        bool smelled = false;
        for (int f = 0; f < 60 * 20; f++) { BeastsUpdate(q, dt); for (const auto& mm : q.fauna.beasts[sh].mem) if (mm.kind == MEM_FOOD && mm.strength > 0.5f && fabsf(mm.pos.x - 40 * T) < 4 * T) smelled = true; }
        if (q.fauna.beasts[tv].life != BeastLife::Gone) fail("the manatee, lured by the blood-kelp, didn't clear the tangle-vine");
        if (!smelled) fail("the Leviathan didn't smell the grazed blood-kelp from across the forest");
    }
    // 4) sardines: inside a school the hunters lose you; a bristle-crab crunched underfoot is heard
    {
        bench(q, 50, 16); build(q);
        int ba = NewBeast(q.fauna, WS_BARRACUDA, {30 * T, 10 * T}); calm(q.fauna.beasts[ba]);
        Remember(q.fauna.beasts[ba], MEM_PREY, BEAST_DIVER, 0, {20 * T, 10 * T}, {0, 0}, 1.0f, 0);
        for (int j = 0; j < 8; j++) { int sd = NewBeast(q.fauna, WS_SARDINE, {20 * T + (j % 4) * 8.0f, 10 * T + (j / 4) * 8.0f}); calm(q.fauna.beasts[sd]); }
        for (int f = 0; f < 30; f++) { diverAt(q, {20 * T + 12, 10 * T + 4}, {0, 0}); BeastsUpdate(q, dt); }
        float left = 0; for (const auto& mm : q.fauna.beasts[ba].mem) if (mm.source == BEAST_DIVER) left = std::max(left, mm.strength);
        if (left > 0.5f) fail("hiding in a sardine school didn't shake off the barracuda");
        int cr = NewBeast(q.fauna, WS_CRAB, {40 * T, 14 * T - 4}); calm(q.fauna.beasts[cr]);
        size_t before = q.fauna.sounds.size();
        diverAt(q, {40 * T, 14 * T - 13}, {0, 0}); q.onGround = true; BeastsUpdate(q, dt);
        bool crunch = false; for (const auto& snd : q.fauna.sounds) if (snd.intensity >= 0.6f && Dist(snd.pos, q.fauna.beasts[cr].pos) < 20) crunch = true;
        if (!crunch) fail("stepping on a bristle-crab made no crunch");
        (void)before;
    }
    if (ok) TraceLog(LOG_WARNING, "verify-weeds13: OK - the octopus-stalker (speed, light), the harpoon mantis and the air-weed, blood-kelp and the manatee, the Leviathan's nose, sardine cover, the bristle-crab's crunch");
    return ok;
}

}  // namespace bk
