// ============================================================================
//  DEPTH - the Hull's ParkourReference1.3 roster (docs/ParkourReference1.3.txt, "1. The Hull").
//
//  Four large beasts - the Hull-Grazer Whale (a passive giant whose armoured back is a moving platform), the
//  Siphon Octopus (anchored in an exhaust tube, it pulls whatever swims near into its lair and uses the dead as
//  bait), the Electric Hull-Crusher Eel (the old moray, grown: it slams the plating and the shockwave kills your
//  momentum) and the Steel-Biter Megalodon (the apex: its shadow darkens the deck, its strikes throw anything
//  that isn't anchored) - two fodder beasts (barnacle-mites, pilot-fish) and five flora that are the smart
//  player's tools: rust-algae (a blinding, corroding cloud), bio-electric hydroids (a stun), pressure-anemones (a
//  vault that jets pursuers back), metal-eater moss (it strips whatever follows your line) and hull-kelp (swing
//  round a corner; an anchor against the megalodon's turbulence).
//
//  The apex is never scripted. A small runtime director (HullTick) only decides *when* one may roam in - after a
//  calm stretch, and only where there is cover within reach - and the shark's own senses do the rest. Where the
//  flora go is planned when the level is built (Hull13Spawn): anchors are spaced so no open stretch is longer
//  than a short sprint, hydroids guard the siphon lairs, anemones sit at the start of long runs.
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;

// Is every cell in the (2rx+1) x (2ry+1) box around `at` open water? Giants only go where their bodies fit.
bool Clear(const NavGrid& N, Vector2 at, int rx, int ry) {
    int cx = (int)floorf(at.x / T), cy = (int)floorf(at.y / T);
    for (int dy = -ry; dy <= ry; dy++)
        for (int dx = -rx; dx <= rx; dx++)
            if (!N.Open(cx + dx, cy + dy)) return false;
    return true;
}

// Solid rock or plating within two tiles over the diver's head: a crevice the big things can't reach into.
bool LowCeiling(const BeastWorld& W, const PlatformState& p) {
    Rectangle r = PlatDiverBox(p);
    int ty = (int)floorf(r.y / T);
    for (int tx = (int)floorf((r.x + 2) / T); tx <= (int)floorf((r.x + r.width - 2) / T); tx++)
        for (int k = 1; k <= 2; k++) if (W.nav.Solid(tx, ty - k)) return true;
    return false;
}

float Speed(Vector2 v) { return Len(v); }
bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
bool Giant(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_GIANT); }

// The top of a whale's armoured back - the platform the diver rides.
Rectangle WhaleBack(const Beast& b) { return {b.pos.x - 70, b.pos.y - 27, 140, 8}; }
bool UnderWhale(const BeastWorld& W, Vector2 at) {
    for (const auto& b : W.beasts)
        if (Alive(b) && b.species == HS_WHALE && fabsf(at.x - b.pos.x) < 96 && at.y > b.pos.y && at.y - b.pos.y < 4.5f * T) return true;
    return false;
}

// A giant swims straight for its goal, and simply doesn't go where its body won't fit (it slides along the edge).
void GiantSwim(BeastWorld& W, Beast& b, Vector2 goal, float speed, float accel, int rx, int ry, float dt) {
    Vector2 to = Sub(goal, b.pos);
    Vector2 desired = Len(to) < 8 ? Vector2{0, 0} : Mul(Norm(to), speed);
    Vector2 dv = Sub(desired, b.vel);
    float m = Len(dv), a = accel * dt * (0.5f + 0.5f * Clamp01(b.health)); // an injured giant turns wide
    if (m > a) dv = Mul(dv, a / m);
    b.vel = Add(b.vel, dv);
    Vector2 np = Add(b.pos, Mul(b.vel, dt));
    np.x = std::clamp(np.x, 3 * T, std::min(W.nav.w * T - 3 * T, W.limitX - 3 * T));
    if (Clear(W.nav, np, rx, ry)) b.pos = np;
    else if (Clear(W.nav, {np.x, b.pos.y}, rx, ry)) { b.pos.x = np.x; b.vel.y *= -0.3f; }
    else if (Clear(W.nav, {b.pos.x, np.y}, rx, ry)) { b.pos.y = np.y; b.vel.x *= -0.3f; }
    else b.vel = Mul(b.vel, 0.3f);
    if (fabsf(b.vel.x) > 10) b.facing = b.vel.x > 0 ? 1.0f : -1.0f;
}

// Open water for a giant near column x: the lowest row (closest to the deck, but at least `lift` tiles above
// it) where its body fits. -1 if there's none.
float OpenRow(const BeastWorld& W, float x, int rx, int ry, int lift) {
    int cx = (int)floorf(x / T);
    int f = W.nav.FloorBelow(cx, 2);
    for (int y = std::min(W.nav.h - 2, f - lift); y >= 2; y--)
        if (Clear(W.nav, {cx * T + 16, y * T + 16.0f}, rx, ry)) return y * T + 16.0f;
    return -1;
}

// Is the diver on a hull-kelp strand (its flow line, root to swaying tip)?
bool OnKelp(const BeastWorld& W, const Beast& b, Rectangle box) {
    Vector2 root = b.pos, top{b.pos.x + sinf(W.time * 1.1f + b.phase) * 8, b.pos.y - 72};
    Vector2 c{box.x + box.width / 2, box.y + box.height / 2};
    float u = Clamp01((c.y - top.y) / std::max(1.0f, root.y - top.y));
    Vector2 on{top.x + (root.x - top.x) * u, top.y + (root.y - top.y) * u};
    return Dist(on, c) <= 16 && c.y >= top.y - 8;
}

// ---------------------------------------------------------------- flora triggers (body impact)
void ShatterRust(BeastWorld& W, PlatformState& p, int i) {
    Beast& a = W.beasts[i];
    if (a.act == BeastAct::Drift) return;
    a.act = BeastAct::Drift; a.actT = 0;
    AddCloud(W, {a.pos.x, a.pos.y - 14}, 70, 3.5f, 3);
    W.sounds.push_back({a.pos, 0.3f, 0.3f, i});
    if (!p.verifying) PlatBurst(p, {a.pos.x, a.pos.y - 8}, 14, Color{190, 100, 44, 255}, 120, 0.9f, 2);
}
void Discharge(BeastWorld& W, PlatformState& p, int i) { // a hydroid's static shockwave: everything close is stunned - the siphon longest
    Beast& h = W.beasts[i];
    h.special2 = 5.0f; h.flashT = 0.5f; h.act = BeastAct::Eat; h.actT = 0;
    AddCloud(W, {h.pos.x, h.pos.y - 10}, 4.5f * T, 0.45f, 4);
    W.sounds.push_back({h.pos, 0.6f, 0.4f, i});
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& o = W.beasts[j];
        if (!Alive(o) || (o.hidden && o.species != HS_SIPHON) || Flora(W, o) || o.species == HS_EEL || Dist(o.pos, h.pos) > 4.5f * T) continue; // the eel is electric itself: it's drawn to it
        o.stunT = std::max(o.stunT, o.species == HS_SIPHON ? 3.5f : 1.8f);
        Remember(o, MEM_THREAT, i, h.id, h.pos, {0, 0}, 0.8f, W.time);
    }
    if (!p.verifying) PlatBurst(p, {h.pos.x, h.pos.y - 10}, 12, Color{150, 220, 255, 255}, 200, 0.4f, 2);
}
void Slam(BeastWorld& W, PlatformState& p, int i) { // the Hull-Crusher eel drives itself into the plating
    Beast& e = W.beasts[i];
    int cx = (int)floorf(e.pos.x / T), fy = W.nav.FloorBelow(cx, (int)floorf(e.pos.y / T));
    Vector2 o{e.pos.x, fy * T};
    e.vel = {0, 320};
    AddCloud(W, o, 8 * T, 0.6f, 4);
    W.sounds.push_back({o, 1.3f, 0.5f, i});
    Diver dv = SeeDiver(p);
    if (dv.alive && p.onGround && fabsf(dv.pos.x - o.x) < 8 * T && fabsf(dv.pos.y - o.y) < 3 * T) { // the deck bucks under your feet
        p.vel.x *= 0.1f; p.vel.y = -300;
        p.onGround = false;
        if (p.pose == 1 || p.pose == 2 || p.pose == 5) p.pose = 0;
        p.boostT = 0; p.slickT = 0;
    }
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& b = W.beasts[j];
        if (!Alive(b) || j == i || Dist(b.pos, o) > 8 * T) continue;
        if (b.species == HS_RUST) ShatterRust(W, p, j);
        else if (!Flora(W, b) && !Giant(W, b) && (b.grounded || Sp(W.biome, b.species).move == MoveMode::Walk) && Dist(b.pos, o) < 6 * T) b.stunT = std::max(b.stunT, 1.0f);
    }
    if (!p.verifying) { PlatBurst(p, o, 20, Color{160, 230, 255, 255}, 260, 0.5f, 2); PlatBurst(p, o, 10, Color{200, 190, 170, 255}, 140, 0.6f, 3); }
}

void FloraHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    b.special2 -= dt;
    b.flashT -= dt;
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    float sp = Speed(dv.vel);
    switch (b.species) {
    case HS_RUST: { // brittle metallic growth: grazed at speed, it bursts into a rust cloud; it grows back
        if (b.act == BeastAct::Drift) { if (b.actT > 18) { b.act = BeastAct::Idle; b.actT = 0; } break; }
        if (dv.alive && sp > 230 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 13, box)) ShatterRust(W, p, i);
        break;
    }
    case HS_HYDROID: {
        if (b.act == BeastAct::Eat && b.actT > 0.4f) { b.act = BeastAct::Idle; b.actT = 0; }
        if (dv.alive && b.special2 <= 0 && (sp > 150 || p.pose == 5) && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 13, box)) Discharge(W, p, i);
        break;
    }
    case HS_PANEMONE: { // vault off it at speed: flung forward, and its jet blows back whatever is on your tail
        if (b.act == BeastAct::Eat && b.actT > 0.4f) { b.act = BeastAct::Idle; b.actT = 0; }
        if (!dv.alive || b.special2 > 0 || !CheckCollisionCircleRec({b.pos.x, b.pos.y - 9}, 14, box)) break;
        if (fabsf(dv.vel.x) < 220 && dv.vel.y < 250) break;
        float s = fabsf(dv.vel.x) > 40 ? (dv.vel.x > 0 ? 1.0f : -1.0f) : (p.facingRight ? 1.0f : -1.0f);
        p.vel.x = s * std::clamp(fabsf(dv.vel.x) * 1.3f, 440.0f, 610.0f);
        p.vel.y = -610;
        p.onGround = false; p.boostT = 0.8f; p.dashReady = true;
        if (p.pose == 1 || p.pose == 2 || p.pose == 4 || p.pose == 7) p.pose = 0;
        b.special2 = 1.0f; b.act = BeastAct::Eat; b.actT = 0; b.facing = s;
        W.sounds.push_back({b.pos, 0.35f, 0.3f, i});
        for (int j = 0; j < (int)W.beasts.size(); j++) { // the jet, out the back
            Beast& o = W.beasts[j];
            if (!Alive(o) || o.hidden || Flora(W, o) || j == i) continue;
            float dx = (o.pos.x - b.pos.x) * -s;
            if (dx < -T || dx > 5 * T || fabsf(o.pos.y - b.pos.y) > 3 * T) continue;
            if (Giant(W, o)) { o.vel.x += -s * 160; continue; }
            o.vel = Add(o.vel, Vector2{-s * 520, -60});
            o.stunT = std::max(o.stunT, 0.6f);
            Remember(o, MEM_THREAT, i, b.id, b.pos, {0, 0}, 0.7f, W.time);
            if (o.act == BeastAct::Strike || o.act == BeastAct::Coil) { o.act = BeastAct::Hunt; o.cooldown = std::max(o.cooldown, 1.5f); }
        }
        if (!p.verifying) { PlatBurst(p, {b.pos.x - s * 10, b.pos.y - 10}, 16, Color{200, 236, 250, 255}, 240, 0.5f, 2); PlatBubbles(p, {b.pos.x, b.pos.y - 10}, 6); }
        break;
    }
    case HS_MOSS: { // acid: anything with armour to lose that crosses it loses some - for good
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (!Alive(o) || o.hidden || Flora(W, o) || o.mass < 1.0f || fabsf(o.pos.x - b.pos.x) > 22 || o.pos.y > b.pos.y + 4 || b.pos.y - o.pos.y > 30) continue;
            Hurt(W, p, j, 0.35f * dt, b.species, false);
            b.flashT = 0.3f; // it fizzes
        }
        break;
    }
    case HS_KELP: { // hull-kelp: an anchor, and a line to swing round a corner on
        if (b.act == BeastAct::Eat && b.actT > 0.5f) { b.act = BeastAct::Idle; b.actT = 0; }
        if (!dv.alive || !OnKelp(W, b, box)) break;
        p.anchored = true;
        if (b.special2 > 0 || fabsf(p.vel.x) < 230 || p.pose == 9) break;
        float s = p.vel.x > 0 ? 1.0f : -1.0f, v = fabsf(p.vel.x);
        if (p.upHeld) p.vel = {s * 70, -std::min(v * 1.05f, 700.0f)};              // up round the corner of a wall
        else if (p.inDown && !p.onGround) p.vel = {s * 70, std::min(v * 0.9f, 620.0f)}; // down round the lip of a drop
        else break;
        p.onGround = false; p.boostT = 0.5f; p.dashReady = true;
        if (p.pose != 0 && p.pose != 7 && p.pose != 4) p.pose = 0;
        b.special2 = 0.6f; b.act = BeastAct::Eat; b.actT = 0; b.facing = s;
        W.sounds.push_back({b.pos, 0.15f, 0.2f, -1});
        break;
    }
    case HS_MITES: { // a carpet of barnacle-mites: dash (or slide, or roll) through and they burst - the slime makes you slick
        if (b.act == BeastAct::Drift) { if (b.actT > 25) { b.act = BeastAct::Idle; b.actT = 0; } break; }
        Rectangle mat{b.pos.x - 38, b.pos.y - 10, 76, 12};
        if (dv.alive && CheckCollisionRecs(box, mat) && (p.pose == 5 || p.pose == 1 || p.pose == 2 || fabsf(dv.vel.x) > 330)) {
            b.act = BeastAct::Drift; b.actT = 0;
            p.slickT = 1.4f;
            W.sounds.push_back({b.pos, 0.2f, 0.2f, i});
            if (!p.verifying) PlatBurst(p, b.pos, 18, Color{176, 150, 110, 255}, 150, 0.5f, 2);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- the four large beasts
void WhaleHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    // it cruises the length of the hull a few tiles over the plating, grazing; blocked, it turns round slowly
    if (b.special > 0) { b.special -= dt; b.vel = Mul(b.vel, 0.96f); if (b.special <= 0) b.facing = -b.facing; }
    else {
        float aheadX = b.pos.x + b.facing * 5 * T;
        float row = OpenRow(W, aheadX, 3, 1, 3);
        bool edge = aheadX < 4 * T || aheadX > std::min(W.nav.w * T - 4 * T, W.limitX - 4 * T);
        if (row < 0 || edge || fabsf(row - b.pos.y) > 5 * T) b.special = 2.0f; // nowhere to go that way
        else GiantSwim(W, b, {aheadX, row + sinf(W.time * 0.4f + b.phase) * 6}, 38 * (0.6f + 0.4f * Clamp01(b.health)), 60, 3, 1, dt);
    }
    if (b.special > 0) GiantSwim(W, b, b.pos, 0, 60, 3, 1, dt);
    b.act = b.special > 0 ? BeastAct::Idle : BeastAct::Wander;
    p.movers.push_back({WhaleBack(b), b.vel});
    (void)p;
}

// The Siphon Octopus: anchored in its tube (b.anchor is the mouth, b.facing the way it opens). It reads the water:
// anything that swims or walks close enough, it draws in - a telegraphed breath (Coil), then the suction (Strike)
// - and what reaches the mouth, it keeps. The dead are left at the mouth: bait for the next thing.
void SiphonHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    b.pos = {b.anchor.x + b.facing * 6, b.anchor.y};
    b.vel = {0, 0};
    b.special = 1;
    b.hidden = true; // tucked in its tube: nothing sees it coming (drawn by DrawSiphon, not the ordinary pass)
    b.stunT = std::max(0.0f, b.stunT - dt);
    if (b.stunT > 0) { if (b.act == BeastAct::Coil || b.act == BeastAct::Strike) { b.act = BeastAct::Ambush; b.cooldown = 3; } return; }
    Diver dv = SeeDiver(p);
    Vector2 mouth = b.anchor;
    const float R = 6.5f * T;
    auto near = [&](Vector2 at) { return Dist(at, mouth) < R && (at.x - mouth.x) * b.facing > -8 && W.nav.LineOfSight(Add(mouth, Vector2{b.facing * 4, 0}), at); };
    switch (b.act) {
    default: b.act = BeastAct::Ambush; b.actT = 0; break;
    case BeastAct::Ambush: {
        if (b.cooldown > 0) break;
        bool wants = dv.alive && near(dv.pos) && Dist(dv.pos, mouth) < 5.5f * T;
        for (const auto& o : W.beasts) if (!wants && Alive(o) && !o.hidden && Pref(W.biome, b.species, o.species) > 0 && near(o.pos) && Dist(o.pos, mouth) < 4.5f * T) wants = true;
        if (wants) { b.act = BeastAct::Coil; b.actT = 0; } // silent: the only tell is the tube's rim flexing and the water stirring
        break;
    }
    case BeastAct::Coil: if (b.actT > 1.0f) { b.act = BeastAct::Strike; b.actT = 0; } break; // the tell: the rim flexes and the water stirs
    case BeastAct::Strike: {
        if (b.actT > 1.8f) { b.act = BeastAct::Ambush; b.actT = 0; b.cooldown = 4.5f; break; }
        if (dv.alive && near(dv.pos)) { // the pull: strongest at the mouth; hold on to something and it's half
            float d = Dist(dv.pos, mouth), a = 2400 * (1 - d / R) * (p.anchored ? 0.45f : 1.0f);
            p.vel = Add(p.vel, Mul(Norm(Sub(mouth, dv.pos)), a * dt));
        }
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.hidden || Flora(W, o) || Giant(W, o) || !near(o.pos)) continue;
            float d = Dist(o.pos, mouth);
            o.vel = Add(o.vel, Mul(Norm(Sub(mouth, o.pos)), 4200 * (1 - d / R) * dt / std::max(0.3f, sqrtf(o.mass))));
            if (d < 20 && Pref(W.biome, b.species, o.species) > 0) { // caught: killed and left at the mouth as bait
                Kill(W, p, j, i);
                o.pos = Add(mouth, Vector2{b.facing * 14, 4}); o.vel = {0, 0};
                b.hunger = Clamp01(b.hunger - 0.3f);
            }
        }
        break;
    }
    }
}

// The Steel-Biter Megalodon. Brought in by the director (never placed by the level), it patrols the open water
// near the diver, takes what it can reach - a wounded eel, an octopus in the open, a diver with water all round
// them - and leaves once it has fed or grown bored. Its strike is a long, straight, telegraphed lunge; the
// turbulence throws anything that isn't anchored.
bool Exposed(const BeastWorld& W, const PlatformState& p, Vector2 at) {
    return Clear(W.nav, at, 1, 1) && !LowCeiling(W, p) && !UnderWhale(W, at);
}
Vector2 JawOf(const Beast& b) { return {b.pos.x + b.facing * 88, b.pos.y + 4}; }
void MegalodonHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.special += dt; // time on the scene
    b.stunT = std::max(0.0f, b.stunT - dt);
    W.apexPos = b.pos;
    float fit = 0.6f + 0.4f * Clamp01(b.health); // rust and moss scars slow it for good
    auto targetPos = [&](Vector2& tp) -> bool {
        if (b.target == BEAST_DIVER) { if (!dv.alive || !Exposed(W, p, dv.pos)) return false; tp = dv.pos; return true; }
        if (!Valid(W, b.target, b.targetId) || !Alive(W.beasts[b.target]) || W.beasts[b.target].hidden) return false;
        tp = W.beasts[b.target].pos;
        return Clear(W.nav, tp, 1, 1);
    };
    // leaving: fed, bored, or chased off by a respawn
    if (b.act != BeastAct::Flee && (b.special > 50 || b.hunger < 0.15f)) { b.act = BeastAct::Flee; b.actT = 0; b.anchor = {b.pos.x < dv.pos.x ? -40 * T : 40 * T, 0}; }
    switch (b.act) {
    case BeastAct::Flee: {
        Vector2 goal{b.pos.x + (b.anchor.x < 0 ? -8 * T : 8 * T), b.pos.y - 2 * T};
        GiantSwim(W, b, goal, 200 * fit, 300, 2, 1, dt);
        if (fabsf(b.pos.x - dv.pos.x) > 26 * T || b.actT > 25) { b.life = BeastLife::Gone; W.apexT = 0; W.calmT = 0; W.apexPos = {-1e9f, -1e9f}; }
        return;
    }
    case BeastAct::Coil: { // the tell: it stops, curls, and its jaws open
        b.vel = Mul(b.vel, 0.9f);
        Vector2 tp;
        if (targetPos(tp)) b.facing = tp.x > b.pos.x ? 1.0f : -1.0f;
        if (b.actT > 0.9f) {
            if (!targetPos(tp)) { b.act = BeastAct::Explore; b.actT = 0; break; }
            b.goal = Mul(Norm(Sub(tp, JawOf(b))), 1.0f); // the lunge's line is fixed now: dodge it
            b.act = BeastAct::Strike; b.actT = 0;
            W.sounds.push_back({b.pos, 1.2f, 0.5f, i});
            AddCloud(W, JawOf(b), 5 * T, 0.5f, 4);
            // the turbulence: anything not anchored within reach is torn loose and thrown
            if (dv.alive && Dist(dv.pos, JawOf(b)) < 9 * T && !p.anchored) {
                Vector2 away = Norm(Sub(dv.pos, b.pos));
                p.vel.x += away.x * 520; p.vel.y += away.y * 260 - 260;
                p.onGround = false;
                if (p.pose == 1 || p.pose == 2 || p.pose == 6) p.pose = 0;
            }
            for (auto& o : W.beasts) if (Alive(o) && !o.hidden && !Flora(W, o) && !Giant(W, o) && Dist(o.pos, b.pos) < 8 * T) { o.vel = Add(o.vel, Mul(Norm(Sub(o.pos, b.pos)), 380)); o.stunT = std::max(o.stunT, 0.4f); }
        }
        return;
    }
    case BeastAct::Strike: {
        Vector2 dir = b.goal;
        b.vel = Mul(dir, 640 * fit);
        Vector2 np = Add(b.pos, Mul(b.vel, dt));
        if (!Clear(W.nav, np, 2, 1) || b.actT > 0.6f) { b.act = BeastAct::Hunt; b.actT = 0; b.cooldown = 2.2f; b.vel = Mul(b.vel, 0.2f); break; }
        b.pos = np;
        b.facing = dir.x >= 0 ? 1.0f : -1.0f;
        for (int j = 0; j < (int)W.beasts.size(); j++) { // what's in the jaws is taken, whatever it is
            Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.hidden || Flora(W, o) || Giant(W, o) || Dist(o.pos, JawOf(b)) > 34) continue;
            Kill(W, p, j, i);
            o.life = BeastLife::Gone; // swallowed
            b.hunger = Clamp01(b.hunger - 0.45f);
        }
        return;
    }
    default: break;
    }
    // patrol and hunt: choose what to go after a few times a second
    b.thinkT -= dt;
    if (b.thinkT <= 0) {
        b.thinkT = 0.3f;
        const int NONE = -99;
        int best = NONE; float bestS = 0;
        if (dv.alive && Dist(dv.pos, b.pos) < 11 * T && Exposed(W, p, dv.pos) && W.nav.LineOfSight(b.pos, dv.pos)) { best = BEAST_DIVER; bestS = 0.5f + 0.5f * b.hunger; }
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            float pref = Pref(W.biome, b.species, o.species);
            if (!Alive(o) || o.hidden || pref <= 0.1f || Dist(o.pos, b.pos) > 12 * T || !Clear(W.nav, o.pos, 1, 1) || !W.nav.LineOfSight(b.pos, o.pos)) continue;
            float s = pref * (1.0f + 1.5f * (1 - Clamp01(o.health))) * (1 - Dist(o.pos, b.pos) / (14 * T)); // blood in the water: the wounded first
            if (s > bestS) { bestS = s; best = j; }
        }
        if (best != NONE) { b.act = BeastAct::Hunt; b.target = best; b.targetId = best >= 0 ? W.beasts[best].id : 0; }
        else if (b.act == BeastAct::Hunt) { b.act = BeastAct::Explore; b.actT = 0; }
    }
    Vector2 tp;
    if (b.act == BeastAct::Hunt && targetPos(tp)) {
        float side = tp.x > b.pos.x ? -1.0f : 1.0f; // line up level with it, a lunge's length off
        Vector2 set{tp.x + side * 4.5f * T, tp.y - 0.5f * T};
        GiantSwim(W, b, set, 170 * fit, 360, 2, 1, dt);
        if (Dist(set, b.pos) < 2 * T) b.facing = tp.x > b.pos.x ? 1.0f : -1.0f; // lined up: it turns to face it
        if (b.cooldown <= 0 && b.stunT <= 0 && Dist(tp, b.pos) < 7 * T && (tp.x - b.pos.x) * b.facing > 0 && W.nav.LineOfSight(JawOf(b), tp)) { b.act = BeastAct::Coil; b.actT = 0; }
        return;
    }
    if (b.act == BeastAct::Hunt) { b.act = BeastAct::Explore; b.actT = 0; }
    // patrolling: long passes over the open water around the diver, high up
    b.act = BeastAct::Explore;
    if (b.actT > 7 || Dist(b.goal, b.pos) < 2 * T || b.goal.y <= 0) {
        b.actT = 0;
        float x = (dv.alive ? dv.pos.x : b.pos.x) + (R(W) < 0.5f ? -1 : 1) * R(W, 5, 11) * T;
        float row = OpenRow(W, x, 2, 1, 5);
        b.goal = row > 0 ? Vector2{x, row - R(W, 0, 2) * T} : b.pos;
    }
    GiantSwim(W, b, b.goal, 120 * fit, 200, 2, 1, dt);
}

}  // namespace

// ---------------------------------------------------------------- hooks, called from HullHooks
void Hull13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (Flora(W, b) || b.species == HS_MITES) { FloraHook(W, p, i, dt); return; }
    switch (b.species) {
    case HS_WHALE: WhaleHook(W, p, i, dt); break;
    case HS_SIPHON: SiphonHook(W, p, i, dt); break;
    case HS_MEGALODON: MegalodonHook(W, p, i, dt); break;
    case HS_EEL: { // the Hull-Crusher: close enough to the deck and hunting someone standing on it, it slams the plating
        b.special -= dt;
        if (b.act == BeastAct::Puffed) { // winding up (it crackles), then down it comes
            b.vel = Mul(b.vel, 0.8f);
            b.flashT = 0.2f;
            if (b.actT > 0.7f) { Slam(W, p, i); b.act = BeastAct::Hunt; b.actT = 0; b.special = 8; b.cooldown = std::max(b.cooldown, 1.0f); }
            break;
        }
        Diver dv = SeeDiver(p);
        int cx = (int)floorf(b.pos.x / T), cy = (int)floorf(b.pos.y / T);
        bool low = W.nav.FloorBelow(cx, cy) - cy <= 3;
        if (!b.hidden && b.special <= 0 && b.stunT <= 0 && b.act == BeastAct::Hunt && b.target == BEAST_DIVER && dv.alive && p.onGround && low &&
            Dist(dv.pos, b.pos) < 7 * T && Dist(dv.pos, b.pos) > 2.5f * T) { b.act = BeastAct::Puffed; b.actT = 0; }
        break;
    }
    default: break;
    }
}

bool Hull13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive || b.stunT > 0) return false;
    if (b.species == HS_SIPHON) return b.act == BeastAct::Strike && CheckCollisionCircleRec(b.anchor, 22, diver);
    if (b.species == HS_MEGALODON) return (b.act == BeastAct::Strike || (b.act == BeastAct::Coil && b.actT > 0.7f)) && CheckCollisionCircleRec(JawOf(b), 30, diver);
    return false;
}

// ---------------------------------------------------------------- per tick: rust corrosion, the diver's anchor, the director
void HullTick(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    // anchored? (kelp sets it in its hook, later this tick; the rest is known now)
    p.anchored = dv.alive && (p.onWeed || p.pose == 6 || p.pose == 9 || LowCeiling(W, p) || UnderWhale(W, dv.pos));
    for (const auto& b : W.beasts) if (dv.alive && !p.anchored && Alive(b) && b.species == HS_KELP && OnKelp(W, b, PlatDiverBox(p))) p.anchored = true;
    // rust clouds eat the armour of whatever hunts through them
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& o = W.beasts[j];
        if (!Alive(o) || o.hidden || Flora(W, o) || o.mass < 1.0f) continue;
        for (const auto& k : W.ink) if (k.kind == 3 && Dist(k.pos, o.pos) < k.r) { Hurt(W, p, j, 0.12f * dt, HS_RUST, false); break; }
    }
    // the director: tension rises while something lethal is after the diver, and falls in calm stretches
    bool apex = false, threatened = false;
    for (const auto& b : W.beasts) {
        if (!Alive(b)) continue;
        if (b.species == HS_MEGALODON) apex = true;
        if (b.target == BEAST_DIVER && (b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike) && dv.alive && Dist(b.pos, dv.pos) < 10 * T) threatened = true;
    }
    if (!apex) W.apexPos = {-1e9f, -1e9f};
    W.tension = Clamp01(W.tension + (threatened ? 0.3f : -0.05f) * dt);
    W.calmT = threatened ? 0 : W.calmT + dt;
    W.apexT += dt;
    if (getenv("DEPTH_BEASTLOG") && fmodf(W.time, 2.0f) < dt) TraceLog(LOG_WARNING, "   director: apex %d alive %d apexT %.0f calm %.0f tension %.2f", (int)apex, (int)dv.alive, W.apexT, W.calmT, W.tension);
    if (apex || !dv.alive || getenv("DEPTH_NOAPEX")) return;
    float wait = W.apexVisits == 0 ? 60.0f : 95.0f;
    if (W.apexT < wait || W.calmT < 20 || W.tension > 0.2f) return;
    // only where a smart player has somewhere to go: an anchor or a crevice within a short sprint
    bool refuge = false;
    for (const auto& b : W.beasts) if (Alive(b) && b.species == HS_KELP && Dist(b.pos, dv.pos) < 7 * T) refuge = true;
    for (int dx = -6; dx <= 6 && !refuge; dx++) {
        int cx = (int)floorf(dv.pos.x / T) + dx;
        int f = W.nav.FloorBelow(cx, (int)floorf(dv.pos.y / T) - 3);
        if (f < W.nav.h && W.nav.Open(cx, f - 1) && (W.nav.Solid(cx, f - 3) || W.nav.Solid(cx, f - 2))) refuge = true;
    }
    if (!refuge) return;
    // it comes in from out of view, behind or ahead
    for (int tries = 0; tries < 6; tries++) {
        float side = R(W) < 0.5f ? -1.0f : 1.0f, x = dv.pos.x + side * 24 * T;
        if (x < 4 * T || x > std::min(W.nav.w * T - 4 * T, W.limitX - 4 * T)) continue;
        float row = OpenRow(W, x, 2, 1, 5);
        if (row < 0) continue;
        int k = NewBeast(W, HS_MEGALODON, {x, row});
        Beast& m = W.beasts[k];
        m.facing = -side; m.act = BeastAct::Explore; m.actT = 99; m.hunger = 0.7f; m.special = 0; m.den = -1;
        m.pers.abnormal = Abnormal::None;
        W.apexVisits++;
        W.apexT = 0;
        if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "director: the megalodon roams in at tile %d (diver at %d)", (int)(x / T), (int)(dv.pos.x / T));
        return;
    }
}

// ---------------------------------------------------------------- the ecology planner (at build time)
// Placement with a purpose: anchors (hull-kelp) at the corners and wherever an open stretch would otherwise run
// longer than a short sprint from cover; anemones where long runs start; rust-algae and mite carpets along the
// open deck; moss by the breaches; hydroids guarding each siphon's tube; a whale where there's room to cruise.
void Hull13Spawn(BeastWorld& W, PlatformState& p) {
    const NavGrid& N = W.nav;
    Spots S; S.Build(W, p, 10, N.w - 4);
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto root = [&](Vector2 f) { return Vector2{f.x, (f.y + 1) * T}; };
    auto flat = [&](size_t i, int n) { for (int k = 1; k < n; k++) { if (i + k >= S.floor.size() || S.floor[i + k].y != S.floor[i].y || S.floor[i + k].x - S.floor[i].x != k * T) return false; } return true; };
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].act = BeastAct::Idle; W.beasts[k].den = -1; return k; };
    std::vector<float> used; // columns already holding a plant
    auto freeAt = [&](float x, float r) { for (float u : used) if (fabsf(u - x) < r) return false; return true; };
    // 0. the Siphon Octopus: in an exhaust tube on a tall wall face by the deck - two on a hull, well apart - with
    //    hydroids on the rivets in front of it (the answer to its pull). Placed first, so nothing crowds them out.
    struct Lair { int wx, ox, y, sd; float score; };
    std::vector<Lair> cand;
    for (int cx = 30; cx < N.w - 12; cx++) {
        if (cx * T > W.limitX - 8 * T) break;
        for (int sd = -1; sd <= 1; sd += 2) {
            int wx = cx, ox = cx + sd;
            int f = N.FloorBelow(ox, 2);
            if (f >= N.h || N.Hazard(ox, f - 1)) continue;
            int y = f - 2; // a tube two tiles up the wall
            if (N.Solid(wx, y) && N.Solid(wx, y - 1) && N.Solid(wx, y + 1) && N.Open(ox, y) && N.Open(ox + sd, y) && N.Open(ox, y - 1) && N.Open(ox + sd * 2, y) && N.Open(ox + sd * 3, y))
                cand.push_back({wx, ox, y, sd, Hash(s, 6000 + cx * 2 + (sd > 0))});
        }
    }
    std::sort(cand.begin(), cand.end(), [](const Lair& a, const Lair& b) { return a.score < b.score; });
    std::vector<float> lairs;
    for (const auto& c : cand) {
        if (lairs.size() >= 2) break;
        bool apart = true;
        for (float l : lairs) if (fabsf(l - c.wx * T) < 60 * T) apart = false;
        if (!apart) continue;
        int k = NewBeast(W, HS_SIPHON, {0, 0});
        Beast& o = W.beasts[k];
        o.facing = (float)c.sd; o.anchor = {c.sd > 0 ? (c.wx + 1) * T : c.wx * T, c.y * T + 16.0f}; o.pos = o.anchor; o.den = -1; o.act = BeastAct::Ambush; o.hidden = true;
        o.pers.abnormal = Abnormal::None;
        lairs.push_back(c.wx * T);
        for (int h = 2, placed = 0; h < 10 && placed < 2; h++) {
            int hx = c.ox + c.sd * h;
            int hf = N.FloorBelow(hx, c.y - 2);
            if (hf >= N.h || !N.Open(hx, hf - 1) || N.Hazard(hx, hf - 1) || !freeAt(hx * T + 16, 1.5f * T)) continue;
            plant(HS_HYDROID, {hx * T + 16.0f, hf * T}); used.push_back(hx * T + 16); placed++; h += 2;
        }
    }
    // 1. anchors: every corner (a step of two tiles or more, or the lip of a gap), then wherever a spot on the deck
    //    would be more than COVER tiles from cover (a kelp anchor, or a crevice with plating low overhead)
    std::vector<float> cover;
    for (const auto& f : S.floor) { int cx = (int)floorf(f.x / T), cy = (int)f.y; if (N.Solid(cx, cy - 1) || N.Solid(cx, cy - 2)) cover.push_back(f.x); }
    auto kelpAt = [&](Vector2 at) { plant(HS_KELP, root(at)); used.push_back(at.x); cover.push_back(at.x); };
    for (size_t i = 1; i < S.floor.size(); i++) {
        Vector2 f = S.floor[i], prev = S.floor[i - 1];
        bool step = fabsf(f.y - prev.y) >= 2 && f.x - prev.x < 1.5f * T, gap = f.x - prev.x > 1.5f * T;
        if (step && freeAt(f.x, 5 * T)) kelpAt(f.y < prev.y ? f : prev); // on the high side of the step, at its edge
        if (gap) { if (freeAt(prev.x, 4 * T)) kelpAt(prev); if (freeAt(f.x, 4 * T)) kelpAt(f); } // both lips of a gap
    }
    const float COVER = 6.5f * T;
    for (const auto& f : S.floor) {
        float near = 1e9f;
        for (float c : cover) near = std::min(near, fabsf(c - f.x));
        if (near > COVER) kelpAt(f);
    }
    // 2. pressure-anemones at the start of long flat runs
    float lastAnemone = -1e9f;
    for (size_t i = 1; i < S.floor.size(); i++)
        if (S.floor[i].y != S.floor[i - 1].y && flat(i, 8) && S.floor[i].x - lastAnemone > 20 * T && freeAt(S.floor[i + 1].x, 2 * T)) {
            plant(HS_PANEMONE, root(S.floor[i + 1])); used.push_back(S.floor[i + 1].x); lastAnemone = S.floor[i].x;
        }
    // 3. rust-algae, mite carpets and moss along the open deck
    for (size_t i = 0; i + 3 < S.floor.size(); i += 3) {
        float h = Hash(s, 5000 + (unsigned)i);
        Vector2 f = S.floor[i + 1];
        if (!flat(i, 3) || !freeAt(f.x, 3 * T)) continue;
        if (h < 0.16f) { plant(HS_RUST, root(f)); used.push_back(f.x); }
        else if (h < 0.26f) { plant(HS_MITES, root(f)); used.push_back(f.x); }
        else if (h < 0.33f) { plant(HS_MOSS, root(f)); used.push_back(f.x); }
    }
    // 5. a Hull-Grazer Whale (two on a long hull), somewhere it has room to cruise
    int whales = N.w > 260 ? 2 : 1;
    for (int k = 0, tries = 0; k < whales && tries < 40; tries++) {
        float x = (0.25f + 0.5f * Hash(s, 7000 + tries)) * std::min(N.w * T, W.limitX);
        float row = OpenRow(W, x, 3, 1, 3);
        if (row < 0 || !Clear(N, {x + 4 * T, row}, 3, 1) || !Clear(N, {x - 4 * T, row}, 3, 1)) continue;
        int b = NewBeast(W, HS_WHALE, {x, row});
        W.beasts[b].facing = Hash(s, 7100 + k) < 0.5f ? -1.0f : 1.0f; W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None;
        k++;
    }
}


// ---------------------------------------------------------------- depth.exe --verify-beasts (the Hull 1.3 part)
bool VerifyHull13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-hull13: FAILED - %s", what); ok = false; };
    auto arena = [](PlatformState& p, int w, int h) { // open water over a deck two tiles thick, walled in
        p = PlatformState{};
        p.level = PL_HULL; p.w = w; p.h = h;
        p.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { p.tiles[h - 1][x] = '#'; p.tiles[h - 2][x] = '#'; p.tiles[0][x] = '#'; }
        for (int y = 0; y < h; y++) { p.tiles[y][0] = '#'; p.tiles[y][w - 1] = '#'; }
        p.pos = {-5000, -5000}; p.deathTimer = 1;
    };
    auto build = [](PlatformState& p) {
        BeastWorld& W = p.fauna;
        W = BeastWorld{};
        W.biome = PL_HULL; W.active = true; W.seed = 777; W.rng = 4242;
        W.nav.w = p.w; W.nav.h = p.h;
        W.nav.solid.assign((size_t)p.w * p.h, 0); W.nav.hazard.assign((size_t)p.w * p.h, 0);
        for (int y = 0; y < p.h; y++) for (int x = 0; x < p.w; x++) W.nav.solid[y * p.w + x] = PlatSolid(p, x, y) ? 1 : 0;
        W.scent.Init(p.w, p.h, TILE);
    };
    auto diverAt = [](PlatformState& p, Vector2 c, Vector2 v) { // place the diver's box centred on c
        p.deathTimer = 0;
        Rectangle r = PlatDiverBox(p);
        p.pos.x += c.x - (r.x + r.width / 2); p.pos.y += c.y - (r.y + r.height / 2);
        p.vel = v;
    };
    auto calm = [](Beast& b) { b.pers.abnormal = Abnormal::None; b.den = -1; };
    const float dt = 1 / 60.0f;
    // 1) the siphon draws the diver toward its tube; a hydroid struck at speed stuns it; a pilot-fish is taken and left as bait
    {
        PlatformState p; arena(p, 60, 20);
        for (int y = 9; y <= 17; y++) p.tiles[y][10] = '#';
        build(p);
        BeastWorld& W = p.fauna;
        int si = NewBeast(W, HS_SIPHON, {0, 0});
        W.beasts[si].facing = 1; W.beasts[si].anchor = {11 * T, 16 * T + 16}; W.beasts[si].act = BeastAct::Ambush; calm(W.beasts[si]);
        diverAt(p, {15 * T, 15 * T}, {0, 0});
        float pull = 0;
        for (int f = 0; f < 60 * 4; f++) { p.vel = {0, 0}; BeastsUpdate(p, dt); pull = std::min(pull, p.vel.x); if (getenv("DEPTH_BEASTLOG") && f % 30 == 0) TraceLog(LOG_WARNING, "  siphon t=%d act %s cd %.2f pos (%.0f,%.0f) life %d diver (%.0f,%.0f) alive %d", f, BeastActName(W.beasts[si].act), W.beasts[si].cooldown, W.beasts[si].pos.x, W.beasts[si].pos.y, (int)W.beasts[si].life, SeeDiver(p).pos.x, SeeDiver(p).pos.y, (int)SeeDiver(p).alive); }
        if (pull > -5) fail("the siphon octopus never drew the diver toward its tube");
        int hy = NewBeast(W, HS_HYDROID, {14 * T, 18 * T}); calm(W.beasts[hy]); W.beasts[hy].special2 = 0;
        W.beasts[si].cooldown = 0; W.beasts[si].act = BeastAct::Ambush;
        diverAt(p, {14 * T, 18 * T - 12}, {400, 0});
        BeastsUpdate(p, dt);
        if (W.beasts[si].stunT <= 0) fail("striking a hydroid didn't stun the siphon octopus");
        p.deathTimer = 1; p.pos = {-5000, -5000};
        W.beasts[si].stunT = 0; W.beasts[si].cooldown = 0; W.beasts[si].act = BeastAct::Ambush;
        int fish = NewBeast(W, HS_PILOT, {13 * T, 16 * T + 16}); calm(W.beasts[fish]); W.beasts[fish].school = -1;
        bool taken = false;
        for (int f = 0; f < 60 * 8 && !taken; f++) { BeastsUpdate(p, dt); if (W.beasts[fish].life == BeastLife::Corpse) taken = true;
            if (getenv("DEPTH_BEASTLOG") && f % 30 == 0) TraceLog(LOG_WARNING, "  bait t=%d siphon %s cd %.1f stun %.1f | fish %s (%.0f,%.0f) v (%.0f,%.0f) life %d", f, BeastActName(W.beasts[si].act), W.beasts[si].cooldown, W.beasts[si].stunT, BeastActName(W.beasts[fish].act), W.beasts[fish].pos.x, W.beasts[fish].pos.y, W.beasts[fish].vel.x, W.beasts[fish].vel.y, (int)W.beasts[fish].life); }
        if (!taken) fail("the siphon octopus never took a pilot-fish swimming by its tube");
        else if (Dist(W.beasts[fish].pos, W.beasts[si].anchor) > 2.5f * T) fail("the siphon's kill wasn't left at its tube as bait");
    }
    // 2) the flora: kelp swings you up a corner (and anchors you), mites make you slick, an anemone vaults you and blows a pursuer back, rust clouds and corrodes
    {
        PlatformState p; arena(p, 70, 20);
        build(p);
        BeastWorld& W = p.fauna;
        int kelp = NewBeast(W, HS_KELP, {20 * T, 18 * T}); calm(W.beasts[kelp]);
        diverAt(p, {20 * T, 18 * T - 30}, {320, 0}); p.upHeld = true;
        BeastsUpdate(p, dt);
        if (p.vel.y > -200) fail("hull-kelp didn't swing a running diver up round the corner");
        if (!p.anchored) fail("holding hull-kelp didn't count as anchored");
        p.upHeld = false;
        int mites = NewBeast(W, HS_MITES, {30 * T, 18 * T}); calm(W.beasts[mites]);
        diverAt(p, {30 * T, 18 * T - 14}, {640, 0}); p.pose = 5;
        BeastsUpdate(p, dt);
        if (p.slickT <= 0 || W.beasts[mites].act != BeastAct::Drift) fail("dashing through barnacle-mites didn't crush them and leave the diver slick");
        p.pose = 0;
        int an = NewBeast(W, HS_PANEMONE, {45 * T, 18 * T}); calm(W.beasts[an]);
        int crab = NewBeast(W, HS_CRAB, {42 * T, 18 * T - 8}); calm(W.beasts[crab]);
        diverAt(p, {45 * T, 18 * T - 14}, {300, 0});
        BeastsUpdate(p, dt);
        if (p.vel.y > -500 || p.vel.x < 380) fail("a pressure-anemone didn't vault the diver forward");
        if (W.beasts[crab].vel.x > -200 && W.beasts[crab].stunT <= 0) fail("the anemone's jet didn't blow back the crab on the diver's tail");
        int rust = NewBeast(W, HS_RUST, {60 * T, 18 * T}); calm(W.beasts[rust]);
        int eel = NewBeast(W, HS_EEL, {60 * T, 17 * T}); calm(W.beasts[eel]);
        diverAt(p, {60 * T, 18 * T - 14}, {300, 0});
        BeastsUpdate(p, dt);
        bool cloud = false; for (const auto& k : W.ink) if (k.kind == 3) cloud = true;
        if (!cloud) fail("grazing rust-algae at speed didn't burst it into a rust cloud");
        p.deathTimer = 1; p.pos = {-5000, -5000};
        for (int f = 0; f < 90; f++) { W.beasts[eel].pos = {60 * T, 17 * T}; BeastsUpdate(p, dt); }
        if (W.beasts[eel].health >= 0.99f) fail("the rust cloud didn't corrode the eel caught in it");
        if (W.scent.Sample(W.scent.blood, W.beasts[eel].pos) <= 0 && W.beasts[eel].health < 0.8f) fail("a wounded eel didn't bleed into the water");
    }
    // 3) herding: sprint through pilot-fish and they bolt along your line
    {
        PlatformState p; arena(p, 40, 20);
        build(p);
        BeastWorld& W = p.fauna;
        int fish = NewBeast(W, HS_PILOT, {20 * T + 30, 12 * T}); calm(W.beasts[fish]);
        diverAt(p, {20 * T, 12 * T}, {420, 0});
        for (int f = 0; f < 10; f++) { p.vel = {420, 0}; BeastsUpdate(p, dt); }
        if (W.beasts[fish].act != BeastAct::Flee || W.beasts[fish].vel.x < 60) fail("sprinting through pilot-fish didn't scatter them along the diver's line");
    }
    // 4) the Hull-Crusher eel slams the plating: a diver running on the deck loses their momentum
    {
        PlatformState p; arena(p, 50, 20);
        build(p);
        BeastWorld& W = p.fauna;
        int eel = NewBeast(W, HS_EEL, {20 * T, 16 * T}); calm(W.beasts[eel]); W.beasts[eel].hunger = 1;
        Beast& e = W.beasts[eel];
        bool slammed = false;
        for (int f = 0; f < 60 * 6 && !slammed; f++) {
            diverAt(p, {25 * T, 18 * T - 20}, {300, 0}); p.onGround = true;
            if (e.act != BeastAct::Puffed) { e.act = BeastAct::Hunt; e.target = BEAST_DIVER; e.pos = {20 * T, 16 * T}; }
            BeastsUpdate(p, dt);
            for (const auto& k : W.ink) if (k.kind == 4) slammed = true;
        }
        if (!slammed) fail("a hunting Hull-Crusher eel near the deck never slammed it");
        else if (fabsf(p.vel.x) > 60 || p.vel.y > -200) fail("the eel's slam didn't knock the running diver's momentum out");
    }
    // 5) the whale: it cruises, and its back is a platform
    {
        PlatformState p; arena(p, 80, 20);
        build(p);
        BeastWorld& W = p.fauna;
        int wh = NewBeast(W, HS_WHALE, {40 * T, 14 * T + 16}); calm(W.beasts[wh]); W.beasts[wh].facing = 1;
        float x0 = W.beasts[wh].pos.x;
        for (int f = 0; f < 60 * 4; f++) BeastsUpdate(p, dt);
        if (p.movers.size() != 1) fail("the whale's back isn't a mover");
        if (fabsf(W.beasts[wh].pos.x - x0) < 40) fail("the Hull-Grazer whale didn't cruise");
    }
    // 6) the director brings the megalodon in after a calm stretch; exposed, the diver is struck at and thrown; in a crevice, never
    for (int crevice = 0; crevice < 2; crevice++) {
        PlatformState p; arena(p, 120, 30);
        if (crevice) for (int x = 55; x <= 65; x++) p.tiles[25][x] = '#'; // a low roof over the deck: a crevice
        build(p);
        BeastWorld& W = p.fauna;
        int kelp = NewBeast(W, HS_KELP, {58 * T, 28 * T}); calm(W.beasts[kelp]);
        Vector2 at = crevice ? Vector2{60 * T, 27 * T + 8} : Vector2{60 * T, 24 * T};
        diverAt(p, at, {0, 0});
        W.apexT = 200; W.calmT = 60;
        BeastsUpdate(p, dt);
        int m = -1;
        for (int i = 0; i < (int)W.beasts.size(); i++) if (Alive(W.beasts[i]) && W.beasts[i].species == HS_MEGALODON) m = i;
        if (m < 0) { fail("the director never brought the megalodon in"); break; }
        bool coiled = false, thrown = false, bit = false;
        for (int f = 0; f < 60 * 30; f++) {
            diverAt(p, at, {0, 0});
            BeastsUpdate(p, dt);
            if (Len(p.vel) > 300) thrown = true;
            if (W.beasts[m].act == BeastAct::Coil && W.beasts[m].target == BEAST_DIVER) coiled = true;
            if (BeastsTouchDiver(p, PlatDiverBox(p))) bit = true;
            if (getenv("DEPTH_BEASTLOG") && f % 60 == 0) { const Beast& g = W.beasts[m]; TraceLog(LOG_WARNING, "  mega[%d] t=%d %s tgt %d pos (%.0f,%.0f) goal (%.0f,%.0f) cd %.1f life %d | diver (%.0f,%.0f) anchored %d", crevice, f / 60, BeastActName(g.act), g.target, g.pos.x, g.pos.y, g.goal.x, g.goal.y, g.cooldown, (int)g.life, SeeDiver(p).pos.x, SeeDiver(p).pos.y, (int)p.anchored); }
        }
        if (!crevice && !coiled) fail("the megalodon never went for a diver hanging in open water");
        if (!crevice && !thrown) fail("the megalodon's strike didn't throw an unanchored diver");
        if (crevice && (coiled || bit)) fail("the megalodon went for a diver sheltering in a crevice");
        if (crevice && thrown) fail("a diver under a low roof was torn loose by the turbulence");
    }
    // 7) the ecology planner on real Hulls: every standing spot on the deck within 7 tiles of cover (hull-kelp or a crevice),
    //    every Hull has its whale, and hydroids guard every siphon tube
    for (unsigned seed : {404u, 11u, 77u, 1234u, 9001u, 31337u}) {
        PlatformState p;
        p.level = PL_HULL; p.layout = {(int)seed, 100};
        PlatBuildLevel(p);
        const BeastWorld& W = p.fauna;
        Spots S; S.Build(W, p, 10, W.nav.w - 4);
        std::vector<float> kelp;
        int whales = 0, siphons = 0, guarded = 0, flora = 0;
        for (const auto& b : W.beasts) {
            if (!Alive(b)) continue;
            if (b.species == HS_KELP) kelp.push_back(b.pos.x);
            if (b.species == HS_WHALE) whales++;
            if (Has(Sp(PL_HULL, b.species), T_FLORA)) flora++;
            if (b.species == HS_SIPHON) {
                siphons++;
                for (const auto& h : W.beasts) if (Alive(h) && h.species == HS_HYDROID && Dist(h.pos, b.anchor) < 10 * T) { guarded++; break; }
            }
        }
        std::vector<float> cover = kelp;
        for (const auto& fl : S.floor) { int cx = (int)floorf(fl.x / T), cy = (int)fl.y; if (W.nav.Solid(cx, cy - 1) || W.nav.Solid(cx, cy - 2)) cover.push_back(fl.x); }
        float worst = 0;
        for (const auto& fl : S.floor) { float near = 1e9f; for (float c : cover) near = std::min(near, fabsf(c - fl.x)); worst = std::max(worst, near / T); }
        TraceLog(LOG_WARNING, "verify-hull13: Hull #%u - %d flora (%d kelp), %d whale(s), %d siphon(s) (%d guarded), farthest standing spot from cover %.1f tiles", seed, flora, (int)kelp.size(), whales, siphons, guarded, worst);
        if (worst > 7.0f) fail("a real Hull has a standing spot more than 7 tiles from any cover");
        if (whales < 1) fail("a real Hull has no Hull-Grazer whale");
        if (guarded < siphons) fail("a siphon tube has no hydroids near it");
    }
    if (ok) TraceLog(LOG_WARNING, "verify-hull13: OK - siphon pull, hydroid stun, bait, kelp swing and anchor, mites, anemone vault and jet, rust cloud and corrosion, herding, eel slam, the whale's back, the megalodon and its refuges");
    return ok;
}
}  // namespace bk
