// ============================================================================
//  DEPTH - the Pirate Ship's ParkourReference1.3 roster (docs/ParkourReference1.3.txt, "2. The Pirate Ship"),
//  adapted to the above-water fleet (the user's call: the Pirate Ship, Island and Cave stay above water).
//
//  The Grand Kraken is the apex. It is never scripted: a runtime director (PirateDirector) only decides when it may
//  rise - after a calm stretch, with the diver on a deck and away from the boss arena - and then it hunts on its own
//  terms: tentacles that rise out of the sea and slam across the deck where the diver is (a long, clear tell), and,
//  its signature, snapping a ship in half. The user chose real snaps anywhere, so every snap point is proven first:
//  the generator proposes them (GenSnap), the validator re-searches every hop around each one on a snapped copy of
//  the level with the real movement code, and the layout keeps only the proven ones (layout[3]). The Kraken only
//  ever breaks those, and gives the diver a couple of seconds' warning (tentacles wrapping the hull, the timbers
//  groaning) to get off the tear.
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;
enum KrakenEvent { KE_NONE = 0, KE_SLAM = 1, KE_SNAP = 2 };

Vector2 Lerp(Vector2 a, Vector2 b, float u) { u = Clamp01(u); return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }

// The striking tentacle: out of the sea at b.anchor, its tip rising over the target (the tell), slamming down onto it,
// lying there a moment, then drawn back. The tip is kept in b.territory (a giant has no territory of its own).
Vector2 SlamTip(const Beast& b) {
    Vector2 raised{b.goal.x, b.goal.y - 6 * T};
    switch (b.act) {
    case BeastAct::Coil: return Lerp(b.anchor, raised, b.actT / 0.7f);
    case BeastAct::Strike: return Lerp(raised, b.goal, b.actT / 0.22f);
    case BeastAct::Eat: return b.goal;
    case BeastAct::Wander: return Lerp(b.goal, b.anchor, b.actT / 0.8f);
    default: return b.anchor;
    }
}

void SlamImpact(BeastWorld& W, PlatformState& p, int i) {
    Beast& k = W.beasts[i];
    W.sounds.push_back({k.goal, 1.5f, 0.5f, i});
    for (int j = 0; j < (int)W.beasts.size(); j++) { // whatever was under it
        Beast& o = W.beasts[j];
        if (j == i || !Alive(o) || o.hidden || Has(Sp(W.biome, o.species), T_FLORA | T_GIANT)) continue;
        if (Dist(o.pos, k.goal) < 34) Kill(W, p, j, i);
        else if (Dist(o.pos, k.goal) < 5 * T) { o.vel = Add(o.vel, Mul(Norm(Sub(o.pos, k.goal)), 300)); o.vel.y -= 200; Remember(o, MEM_THREAT, i, k.id, k.goal, {0, 0}, 1.0f, W.time); }
    }
    for (size_t e = 0; e < p.enemies.size();) { // friendly fire: a pirate under the slam goes over the side
        Vector2 ep{p.enemies[e].pos.x + 16, p.enemies[e].pos.y + 16};
        if (Dist(ep, k.goal) < 40 && (p.enemies[e].type == 'P' || p.enemies[e].type == 'G')) { p.enemies.erase(p.enemies.begin() + e); continue; }
        e++;
    }
    Diver dv = SeeDiver(p);
    if (dv.alive && Dist(dv.pos, k.goal) < 3 * T && p.onGround) { p.vel.y = std::min(p.vel.y, -220.0f); p.onGround = false; } // the deck jumps under you
    if (!p.verifying) { PlatBurst(p, k.goal, 22, Color{150, 108, 66, 255}, 260, 0.8f, 3); PlatBurst(p, k.goal, 12, Color{200, 225, 250, 255}, 200, 0.6f, 2); }
}

// Which proven snap point to break: on the diver's ship or the next one along, a few tiles away from them - never
// right under their feet (they must be able to see it coming and get clear), preferring the way they're heading.
int ChooseSnap(const PlatformState& p, const Diver& dv) {
    int best = -1; float bs = 1e9f;
    for (int k = 0; k < (int)p.snaps.size(); k++) {
        if (p.snapped[k]) continue;
        const GenSnap& s = p.snaps[k];
        float cx = s.col * T + 16, d = fabsf(cx - dv.pos.x);
        if (d < 3 * T || d > 16 * T) continue;
        float score = d + ((cx - dv.pos.x) * (dv.vel.x >= 0 ? 1 : -1) < 0 ? 6 * T : 0);
        if (score < bs) { bs = score; best = k; }
    }
    return best;
}

void KrakenHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.special += dt;
    float sea = p.waterY > 0 ? p.waterY : b.pos.y;
    // the body keeps under the diver, deep below the waterline
    float want = dv.alive ? dv.pos.x : b.pos.x;
    b.vel.x += std::clamp(want - b.pos.x, -60.0f, 60.0f) * dt;
    b.vel.x *= 0.96f;
    b.pos.x += b.vel.x * dt;
    b.pos.y = sea + 5 * T; // (its rise out of the dark is drawn, from b.act/actT)
    W.apexPos = {b.pos.x, sea};
    b.territory = SlamTip(b);
    if (b.act != BeastAct::Flee && (b.special > 45 || b.special2 >= 2)) { b.act = BeastAct::Flee; b.actT = 0; }
    switch (b.act) {
    case BeastAct::Explore: if (b.actT > 3) { b.act = BeastAct::Idle; b.actT = 0; b.cooldown = 1.0f; } break; // rising out of the deep
    case BeastAct::Flee: if (b.actT > 3) { b.life = BeastLife::Gone; W.apexT = 0; W.calmT = 0; W.apexPos = {-1e9f, -1e9f}; } break;
    case BeastAct::Idle: {
        if (b.cooldown > 0 || !dv.alive) break;
        int snap = (b.target == KE_SLAM || b.carry < 0) ? ChooseSnap(p, dv) : -1;
        if (snap >= 0 && (b.special2 > 0 || b.special > 10 || R(W) < 0.5f)) { // the signature: wrap a ship and break it
            // one attack (the user): an arm rears out of the sea over the ship, the timbers groan, and the slam itself breaks her
            const GenSnap& sn = p.snaps[snap];
            float cx = sn.col * T + 16, side = dv.pos.x < cx ? 1.0f : -1.0f; // it rises on the far side of the break from you
            b.anchor = {cx + side * R(W, 4, 6) * T, sea};
            b.goal = {cx, (sn.bottom - 4 - p.genTop) * T};
            b.target = KE_SNAP; b.carry = snap; b.act = BeastAct::Coil; b.actT = 0;
            W.sounds.push_back({{cx, sea}, 1.0f, 0.6f, i});
        } else { // a slam across the deck where the diver stands (or is about to)
            float side = R(W) < 0.5f ? -1.0f : 1.0f;
            b.anchor = {dv.pos.x + side * R(W, 4, 7) * T, sea};
            b.goal = {dv.pos.x + std::clamp(dv.vel.x * 0.35f, -2 * T, 2 * T), dv.pos.y + 8};
            b.target = KE_SLAM; b.act = BeastAct::Coil; b.actT = 0;
        }
        break;
    }
    case BeastAct::Coil: {
        if (b.actT > (b.target == KE_SNAP ? 1.8f : 1.1f)) { b.act = BeastAct::Strike; b.actT = 0; } // a snap gets a longer tell: the hull groans first
        break;
    }
    case BeastAct::Strike: if (b.actT > 0.22f) { SlamImpact(W, p, i); if (b.target == KE_SNAP && b.carry >= 0) { PlatSnapShip(p, b.carry); b.special2++; } b.act = BeastAct::Eat; b.actT = 0; } break; // the slam that lands on the break tears the ship in two
    case BeastAct::Eat: if (b.actT > (b.target == KE_SNAP ? 0.8f : 0.5f)) { b.act = BeastAct::Wander; b.actT = 0; } break;
    case BeastAct::Wander: if (b.actT > 0.8f) { b.act = BeastAct::Idle; b.actT = 0; b.cooldown = R(W, 2.5f, 4.0f); if (b.target == KE_SNAP) b.carry = -1; } break;
    default: b.act = BeastAct::Idle; break;
    }
    b.territory = SlamTip(b);
}


// ---------------------------------------------------------------- the rest of the roster
constexpr int MIMIC_REST = 0;
bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
void BreakCrate(BeastWorld& W, PlatformState& p, int tx, int ty) { // a crate smashed to kindling (never part of a proven route's floor)
    if (PlatTileAt(p, tx, ty) != 'k') return;
    p.tiles[ty][tx] = '.';
    if (!p.verifying) PlatBurst(p, {tx * T + 16, ty * T + 16}, 14, Color{170, 124, 74, 255}, 200, 0.7f, 3);
    W.sounds.push_back({{tx * T + 16, ty * T + 16}, 0.8f, 0.4f, -1});
    BeastsTerrainChanged(p);
}

// The Rigging-Mimic Cuttlefish hangs from a yard as a fraying rope. A diver who leaps off the ratlines near it without
// first stopping to test the line is snatched mid-air; one who pauses on the ladder close by sees the "rope" twitch -
// it's found out, and slinks up onto the yard. A Siren's lantern weed's light shows it for what it is; a flashing
// curtain of copper moths distracts it. b.anchor is where it hangs from, b.territory its arm's tip.
Vector2 MimicRest(const BeastWorld& W, const Beast& b) { return {b.anchor.x + sinf(W.time * 1.3f + b.phase) * 4, b.anchor.y + 2.5f * T}; }
void MimicHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    bool onPole = p.onWeed || p.pose == 6, wasOnPole = b.ignoreId != 0;
    b.ignoreId = onPole ? 1 : 0;
    b.pos = b.anchor; b.vel = {0, 0};
    bool lit = false, distracted = false;
    for (const auto& o : W.beasts) {
        if (!Alive(o)) continue;
        if (o.species == PS_LANTERN && Dist(o.pos, MimicRest(W, b)) < 5 * T) lit = true;
        if (o.species == PS_MOTH && o.flashT > 0 && Dist(o.pos, b.anchor) < 5 * T) distracted = true;
    }
    Vector2 rest = MimicRest(W, b);
    switch (b.act) {
    default: b.act = BeastAct::Ambush; break;
    case BeastAct::Ambush: {
        b.special = 1; // disguised
        b.territory = rest;
        if (lit) { b.act = BeastAct::Flee; b.actT = 0; break; }
        // testing the line: stopped on the ratlines close by, the diver sees it twitch
        if (dv.alive && onPole && Len(dv.vel) < 40 && Dist(dv.pos, rest) < 5 * T) { b.special2 += dt; if (b.special2 > 0.5f) { b.act = BeastAct::Flee; b.actT = 0; W.sounds.push_back({b.pos, 0.2f, 0.2f, i}); } }
        else b.special2 = std::max(0.0f, b.special2 - dt * 0.5f);
        // the leap off the ladder, close by, untested: it strikes
        if (dv.alive && wasOnPole && !onPole && b.cooldown <= 0 && !distracted && Dist(dv.pos, rest) < 4.5f * T) { b.act = BeastAct::Coil; b.actT = 0; }
        break;
    }
    case BeastAct::Coil: // a twitch and an uncurl: the tell
        b.territory = Add(rest, Vector2{sinf(b.actT * 60) * 3, -b.actT * 20});
        if (b.actT > 0.3f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = dv.alive ? Add(dv.pos, Mul(dv.vel, 0.2f)) : rest; b.special = 0; }
        break;
    case BeastAct::Strike: {
        Vector2 aim = b.goal;
        if (Dist(aim, b.anchor) > 5 * T) aim = Add(b.anchor, Mul(Norm(Sub(aim, b.anchor)), 5 * T)); // its reach
        b.territory = Add(rest, Mul(Sub(aim, rest), Clamp01(b.actT / 0.2f)));
        if (b.actT > 0.5f) { b.act = BeastAct::Wander; b.actT = 0; b.cooldown = 4; }
        break;
    }
    case BeastAct::Wander: b.territory = Add(b.territory, Mul(Sub(rest, b.territory), std::min(1.0f, dt * 4))); if (b.actT > 1) b.act = BeastAct::Ambush; break;
    case BeastAct::Flee: // found out: up on the yard, harmless and plain to see, until it forgets
        b.special = 0; b.special2 = 0;
        b.territory = Add(b.territory, Mul(Sub(Vector2{b.anchor.x, b.anchor.y + 6}, b.territory), std::min(1.0f, dt * 3)));
        if (b.actT > 20 && !lit) { b.act = BeastAct::Ambush; b.actT = 0; }
        break;
    }
}

// The Cannoneer Mantis Shrimp hides in a crate's porthole looking down the deck. Anything fast crossing in front of it -
// a sprint, a dash - and it cocks and fires a cavitation strike down the line at the speed of a bullet: lethal, and it
// smashes the crates it passes through. Walk past it and it never fires. b.anchor is its porthole, b.goal the strike.
void MantisHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.pos = b.anchor; b.vel = {0, 0};
    switch (b.act) {
    default: b.act = BeastAct::Ambush; break;
    case BeastAct::Ambush: {
        if (b.cooldown > 0 || !dv.alive) break;
        float ahead = (dv.pos.x - b.anchor.x) * b.facing;
        bool fast = Len(dv.vel) > 300 || p.pose == 5;
        if (fast && ahead > 0.5f * T && ahead < 8 * T && fabsf(dv.pos.y - b.anchor.y) < 1.5f * T && W.nav.LineOfSight(b.anchor, dv.pos)) { b.act = BeastAct::Coil; b.actT = 0; }
        break;
    }
    case BeastAct::Coil: if (b.actT > 0.4f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = b.anchor; b.special = 0; W.sounds.push_back({b.anchor, 1.1f, 0.3f, i}); } break;
    case BeastAct::Strike: {
        float step = 1400 * dt;
        b.goal.x += b.facing * step; b.special += step;
        int tx = (int)floorf(b.goal.x / T), ty = (int)floorf(b.goal.y / T);
        if (PlatTileAt(p, tx, ty) == 'k') BreakCrate(W, p, tx, ty);
        for (int j = 0; j < (int)W.beasts.size(); j++) { Beast& o = W.beasts[j]; if (j != i && Alive(o) && !o.hidden && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && Dist(o.pos, b.goal) < 16) Kill(W, p, j, i); }
        if (W.nav.Solid(tx, ty) || b.special > 10 * T) { if (!p.verifying) PlatBurst(p, b.goal, 10, Color{220, 240, 255, 255}, 180, 0.4f, 2); b.act = BeastAct::Wander; b.actT = 0; b.cooldown = 3; }
        break;
    }
    case BeastAct::Wander: if (b.actT > 0.3f) b.act = BeastAct::Ambush; break;
    }
}

// The Timber-Shell Tortoise lumbers the length of a deck, turning at walls and edges; its shell (heaped with sunken
// cannonballs and planks) is a platform the diver can ride, and the crates in its way are crushed flat - lanes open up.
// b.anchor.y is the deck it walks on.
void TortoiseHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    int row = (int)floorf((b.anchor.y - 1) / T); // the open row it stands in
    if (b.special > 0) { b.special -= dt; if (b.special <= 0) b.facing = -b.facing; b.vel.x = 0; }
    else {
        int ax = (int)floorf((b.pos.x + b.facing * 40) / T);
        for (int r = row - 1; r <= row; r++) if (PlatTileAt(p, ax, r) == 'k') BreakCrate(W, p, ax, r); // crushed
        bool floor = W.nav.Solid(ax, row + 1) && !W.nav.Hazard(ax, row) && PlatTileAt(p, ax, row + 1) != 'f';
        bool clear = W.nav.Open(ax, row) && W.nav.Open(ax, row - 1);
        if (!floor || !clear || ax < 2 || ax * T > W.limitX - 2 * T) b.special = 1.5f; // turns round, slowly
        else b.vel.x = b.facing * 22 * (0.6f + 0.4f * Clamp01(b.health));
    }
    b.pos.x += b.vel.x * dt;
    b.pos.y = b.anchor.y - 20;
    b.act = b.special > 0 ? BeastAct::Idle : BeastAct::Wander;
    p.movers.push_back({{b.pos.x - 34, b.pos.y - 22, 68, 6}, b.vel});
    Diver dv = SeeDiver(p);
    Rectangle body{b.pos.x - 38, b.pos.y - 16, 76, 36}, d = PlatDiverBox(p);
    if (dv.alive && p.onMover < 0 && CheckCollisionRecs(body, d) && d.y + d.height > b.pos.y - 12) p.vel.x = (dv.pos.x > b.pos.x ? 1 : -1) * 220.0f; // shouldered aside
}

void PirateFlora(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    bool rushing = dv.alive && (p.pose == 5 || fabsf(dv.vel.x) > 330);
    switch (b.species) {
    case PS_BORER: { // worm-riddled planking: rush across it and it gives way right behind you
        if (b.act == BeastAct::Drift) { if (b.actT > 20) b.act = BeastAct::Idle; break; }
        int x0 = (int)b.anchor.x, row = (int)b.anchor.y, n = (int)b.special2;
        Rectangle run{x0 * T, (row - 1) * T, n * T, T + 4};
        if (rushing && CheckCollisionRecs(run, box)) {
            for (int k = 0; k < n; k++) {
                if (PlatTileAt(p, x0 + k, row) != 'f') continue;
                bool has = false;
                for (auto& c : p.crumbles) if (c.tx == x0 + k && c.ty == row) has = true;
                if (!has) p.crumbles.push_back({x0 + k, row, 0.25f}); // (a crumble breaks at 0.5 s)
            }
            b.act = BeastAct::Drift; b.actT = 0;
            W.sounds.push_back({b.pos, 0.3f, 0.3f, i});
        }
        break;
    }
    case PS_ROT: { // struck, a spore cloud, and a rotten reek the scavengers come for
        if (b.act == BeastAct::Drift) { if (b.actT > 25) b.act = BeastAct::Idle; break; }
        if (dv.alive && Len(dv.vel) > 200 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 12, box)) {
            AddCloud(W, {b.pos.x, b.pos.y - 14}, 64, 3.0f, 1);
            W.scent.Emit(W.scent.blood, {b.pos.x, b.pos.y - 12}, 40);
            b.act = BeastAct::Drift; b.actT = 0;
            W.sounds.push_back({b.pos, 0.3f, 0.3f, i});
            if (!p.verifying) PlatBurst(p, {b.pos.x, b.pos.y - 10}, 14, Color{150, 170, 90, 255}, 120, 0.8f, 2);
        }
        break;
    }
    case PS_MKELP: { // dash through the kelp lashed to a broken spar: it parts, and the spar swings back like a ram
        if (b.act == BeastAct::Eat) { if (b.actT > 1.2f) { b.act = BeastAct::Drift; b.actT = 0; } break; }
        if (b.act == BeastAct::Drift) { if (b.actT > 30) b.act = BeastAct::Idle; break; }
        if (rushing && CheckCollisionRecs({b.pos.x - 10, b.pos.y - 44, 20, 44}, box)) {
            b.act = BeastAct::Eat; b.actT = 0; b.facing = dv.vel.x > 0 ? -1.0f : 1.0f; // it swings back the way you came
            W.sounds.push_back({b.pos, 0.9f, 0.3f, i});
            for (int j = 0; j < (int)W.beasts.size(); j++) {
                Beast& o = W.beasts[j];
                if (!Alive(o) || o.hidden || Flora(W, o) || Has(Sp(W.biome, o.species), T_GIANT)) continue;
                float dx = (o.pos.x - b.pos.x) * b.facing;
                if (dx < 0 || dx > 4 * T || fabsf(o.pos.y - (b.pos.y - T)) > 2 * T) continue;
                o.stunT = std::max(o.stunT, 1.5f);
                o.vel = Add(o.vel, Vector2{b.facing * 360, -160});
                Hurt(W, p, j, 0.3f, b.species, false);
            }
        }
        break;
    }
    case PS_BARNACLE: // razor-sharp: it shreds anything big that brushes it (and a diver who dashes into it: see the touch)
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (Alive(o) && !o.hidden && !Flora(W, o) && o.mass >= 3 && Dist(o.pos, {b.pos.x, b.pos.y - 8}) < 20) Hurt(W, p, j, 0.6f * dt, b.species, false);
        }
        break;
    case PS_LANTERN: W.lights.push_back({{b.pos.x, b.pos.y - 14}, 5 * T, 0.8f + 0.2f * sinf(W.time * 2.5f + b.phase)}); break; // a pulsing glow
    default: break;
    }
}}  // namespace

void Pirate13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case PS_KRAKEN: KrakenHook(W, p, i, dt); break;
    case PS_CUTTLE: MimicHook(W, p, i, dt); break;
    case PS_MANTIS: MantisHook(W, p, i, dt); break;
    case PS_TORTOISE: TortoiseHook(W, p, i, dt); break;
    default: if (Flora(W, b)) PirateFlora(W, p, i, dt); break;
    }
}

bool Pirate13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    if (b.life != BeastLife::Alive || b.stunT > 0) return false;
    if (b.species == PS_CUTTLE) return b.act == BeastAct::Strike && CheckCollisionCircleRec(b.territory, 11, diver);
    if (b.species == PS_MANTIS) return b.act == BeastAct::Strike && CheckCollisionCircleRec(b.goal, 13, diver);
    if (b.species == PS_BARNACLE) return W.diverPose == 5 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 12, diver); // dashed straight into it
    if (b.species != PS_KRAKEN || b.target != KE_SLAM) return false;
    if (b.act != BeastAct::Strike && b.act != BeastAct::Eat) return false;
    Vector2 tip = SlamTip(b);
    for (int k = 0; k <= 8; k++) { // the whole length of the tentacle that's down across the deck, thicker toward the sea
        float u = k / 8.0f;
        Vector2 q = Lerp(tip, b.anchor, u);
        if (CheckCollisionCircleRec(q, 12 + 10 * u, diver)) return true;
    }
    return false;
}

// The runtime director: tension rises while something lethal is after the diver and falls in calm stretches; after a
// long enough calm the Grand Kraken may rise under the fleet. Never by the boss arena, never with the diver in the air.
void PirateDirector(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    bool apex = false, threatened = false;
    for (const auto& b : W.beasts) {
        if (!Alive(b)) continue;
        if (b.species == PS_KRAKEN) apex = true;
        if (b.target == BEAST_DIVER && (b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike) && dv.alive && Dist(b.pos, dv.pos) < 10 * T) threatened = true;
    }
    if (!apex) W.apexPos = {-1e9f, -1e9f};
    W.tension = Clamp01(W.tension + (threatened ? 0.3f : -0.05f) * dt);
    W.calmT = threatened ? 0 : W.calmT + dt;
    W.apexT += dt;
    if (apex || !dv.alive || p.waterY <= 0 || getenv("DEPTH_NOAPEX")) return;
    float wait = W.apexVisits == 0 ? 70.0f : 110.0f;
    if (W.apexT < wait || W.calmT < 20 || W.tension > 0.2f || !p.onGround || dv.pos.x > W.limitX - 24 * T) return;
    int k = NewBeast(W, PS_KRAKEN, {dv.pos.x, p.waterY + 5 * T});
    Beast& m = W.beasts[k];
    m.act = BeastAct::Explore; m.actT = 0; m.den = -1; m.carry = -1; m.target = KE_NONE; m.special = 0; m.special2 = 0;
    m.pers.abnormal = Abnormal::None;
    m.anchor = m.goal = m.territory = {dv.pos.x, p.waterY};
    W.apexVisits++;
    W.apexT = 0;
    W.sounds.push_back({{dv.pos.x, p.waterY}, 1.2f, 0.8f, k});
    if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "director: the Grand Kraken rises under tile %d", (int)(dv.pos.x / T));
}

// ---------------------------------------------------------------- the fleet's ecology planner (at build time)
// Mimics hang by the ratline ladders (where the route makes you leap off them), with a lantern weed below most of them;
// cannon-moss where you drop out of the rigging; mast-kelp lashed at the foot of masts; borers in rotten planking; a
// mantis in a crate porthole looking down a long stretch of open deck; a tortoise on a long deck; rot and barnacle
// clusters about the decks; copper moths nesting among the cargo.
void Pirate13Spawn(BeastWorld& W, PlatformState& p) {
    // the decks: per column, the lowest standing row over plank ('#') above the sea - not a yard, a rope or a crate top
    Spots S;
    for (int x = 12; x < W.nav.w - 8; x++) {
        if (x * T > W.limitX - 2 * T) break;
        int best = -1;
        for (int y = 2; y < W.nav.h - 1; y++)
            if (W.nav.Standable(x, y) && !W.nav.Hazard(x, y) && PlatTileAt(p, x, y + 1) == '#' && (p.waterY <= 0 || (y + 1) * T < p.waterY)) best = y;
        if (best >= 0) S.floor.push_back({x * T + 16.0f, (float)best});
    }
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].den = -1; W.beasts[k].act = BeastAct::Idle; W.beasts[k].pers.abnormal = Abnormal::None; return k; };
    auto deckAt = [&](int cx) -> int { // the standing row on the deck under column cx (-1 if none)
        for (const auto& f : S.floor) if ((int)floorf(f.x / T) == cx) return (int)f.y;
        return -1;
    };
    int limit = (int)(std::min((float)W.nav.w * T, W.limitX) / T) - 4;
    // the ratline ladders
    std::vector<int> ladders;
    for (int x = 12; x < limit; x++) {
        int run = 0;
        for (int y = 0; y < W.nav.h; y++) if (PlatTileAt(p, x, y) == 'l') run++;
        if (run >= 5) ladders.push_back(x);
    }
    int mimics = 0;
    for (size_t li = 0; li < ladders.size(); li++) {
        int lx = ladders[li];
        if (li > 0 && lx - ladders[li - 1] < 3) continue;
        int deck = deckAt(lx + 2) >= 0 ? deckAt(lx + 2) : deckAt(lx - 2);
        if (deck >= 0 && Hash(s, 8000 + lx) < 0.6f) plant(PS_CMOSS, {(lx + (Hash(s, 8100 + lx) < 0.5f ? 2 : -2)) * T + 16.0f, (deck + 1) * T}); // a soft landing out of the rigging
        if (deck >= 0 && Hash(s, 8200 + lx) < 0.45f) plant(PS_MKELP, {lx * T + 16.0f, (deck + 1) * T});
        if (mimics >= 3 || Hash(s, 8300 + lx) > 0.5f) continue;
        for (int y = 2; y < W.nav.h - 4; y++) // a yard beside the ladder, with room under it to hang
            for (int side = -1; side <= 1; side += 2) {
                int yx = lx + side * 3;
                if (PlatTileAt(p, yx, y) != '=' || !W.nav.Open(yx, y + 1) || !W.nav.Open(yx, y + 2) || !W.nav.Open(yx, y + 3)) continue;
                int k = NewBeast(W, PS_CUTTLE, {yx * T + 16.0f, (y + 1) * T});
                Beast& m = W.beasts[k];
                m.anchor = {yx * T + 16.0f, (y + 1) * T}; m.den = -1; m.act = BeastAct::Ambush; m.special = 1; m.pers.abnormal = Abnormal::None; m.territory = m.anchor;
                mimics++;
                if (Hash(s, 8400 + lx) < 0.35f) plant(PS_LANTERN, {lx * T + 16.0f, (y + 5) * T}); // growing on the mast, lighting the yard
                y = W.nav.h; break;
            }
    }
    // borers in the rotten planking
    for (int y = 1; y < W.nav.h - 1; y++)
        for (int x = 12; x < limit; x++) {
            if (PlatTileAt(p, x, y) != 'f' || PlatTileAt(p, x - 1, y) == 'f') continue;
            int n = 0; while (PlatTileAt(p, x + n, y) == 'f') n++;
            if (n >= 3 && Hash(s, 8500 + x) < 0.7f) { int k = plant(PS_BORER, {(x + n * 0.5f) * T, y * T}); W.beasts[k].anchor = {(float)x, (float)y}; W.beasts[k].special2 = (float)n; }
        }
    // a mantis in a crate porthole, looking down a long open stretch
    int mantis = 0;
    for (int y = 2; y < W.nav.h - 1 && mantis < 2; y++)
        for (int x = 14; x < limit - 10 && mantis < 2; x++) {
            if (PlatTileAt(p, x, y) != 'k' || !W.nav.Solid(x, y + 1) || Hash(s, 8600 + x) > 0.35f) continue;
            for (int side = -1; side <= 1; side += 2) {
                int open = 0;
                for (int k = 1; k <= 8; k++) if (W.nav.Standable(x + side * k, y) && W.nav.Open(x + side * k, y - 1)) open++; else break;
                if (open < 6) continue;
                int k = NewBeast(W, PS_MANTIS, {x * T + 16.0f, y * T + 16.0f});
                Beast& m = W.beasts[k];
                m.facing = (float)side; m.anchor = {x * T + 16.0f + side * 14, y * T + 18.0f}; m.den = -1; m.act = BeastAct::Ambush; m.pers.abnormal = Abnormal::None;
                mantis++; x += 30; break;
            }
        }
    // a tortoise on a long, flat deck
    for (size_t i = S.floor.size() / 4; i + 12 < S.floor.size(); i++) {
        bool flat = true;
        for (int k = 1; k < 12 && flat; k++) flat = S.floor[i + k].y == S.floor[i].y && S.floor[i + k].x - S.floor[i].x == k * T;
        if (!flat) continue;
        Vector2 f = S.floor[i + 6];
        int k = NewBeast(W, PS_TORTOISE, {f.x, (f.y + 1) * T - 20});
        W.beasts[k].anchor = {f.x, (f.y + 1) * T}; W.beasts[k].den = -1; W.beasts[k].pers.abnormal = Abnormal::None; W.beasts[k].facing = Hash(s, 8700) < 0.5f ? -1.0f : 1.0f;
        break;
    }
    // rot, barnacle clusters, copper moths
    for (size_t i = 0; i < S.floor.size(); i += 7) {
        float h = Hash(s, 8800 + (unsigned)i);
        Vector2 f = S.floor[i], root{f.x, (f.y + 1) * T};
        if (h < 0.18f) plant(PS_ROT, root);
        else if (h < 0.28f) plant(PS_BARNACLE, root);
        else if (h < 0.34f) { for (int k = 0; k < 6; k++) { int m = NewBeast(W, PS_MOTH, {f.x + (Hash(s, 8900 + (unsigned)i * 7 + k) - 0.5f) * 40, f.y * T - 10 - Hash(s, 9000 + (unsigned)i + k) * 30}); W.beasts[m].school = 200 + (int)i; W.beasts[m].den = -1; } }
    }
}
// ---------------------------------------------------------------- depth.exe --verify-pirate-ecosystem (the 1.3 part)
bool VerifyPirate13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-pirate13: FAILED - %s", what); ok = false; };
    PlatformState p;
    p.level = PL_PIRATE; p.layout = {606, 100, 0, 0x7fffffff}; // every candidate snap, for the mechanics (the real game keeps only the proven ones)
    PlatBuildLevel(p);
    BeastWorld& W = p.fauna;
    if (p.snaps.empty()) { fail("the fleet has no snap points"); return ok; }
    { int n[PS_COUNT] = {0}; for (const auto& b : W.beasts) if (b.life == BeastLife::Alive) n[b.species]++;
      TraceLog(LOG_WARNING, "verify-pirate13: a real fleet has %d mimics, %d mantis, %d tortoise, %d borer runs, %d moths, flora: %d cannon-moss, %d rot, %d mast-kelp, %d barnacle, %d lantern", n[PS_CUTTLE], n[PS_MANTIS], n[PS_TORTOISE], n[PS_BORER], n[PS_MOTH], n[PS_CMOSS], n[PS_ROT], n[PS_MKELP], n[PS_BARNACLE], n[PS_LANTERN]); }
    // stand the diver on the deck of a ship with a snap point, six tiles from it
    const GenSnap s = p.snaps[0];
    int cx = s.col + 6, cy = s.bottom - 4 - p.genTop - 1; // the deck (row Ds), not a yard up a mast
    if (PlatSolid(p, cx, cy) || !PlatSolid(p, cx, cy + 1)) { fail("no deck beside the snap point"); return ok; }
    Rectangle box = PlatDiverBox(p);
    Vector2 stand{cx * T + 6, (cy + 1) * T - box.height};
    auto hold = [&]() { p.pos = stand; p.vel = {0, 0}; p.onGround = true; p.deathTimer = 0; };
    hold();
    W.apexT = 999; W.calmT = 999; W.tension = 0;
    BeastsUpdate(p, 1 / 60.0f);
    int kr = -1;
    for (int i = 0; i < (int)W.beasts.size(); i++) if (Alive(W.beasts[i]) && W.beasts[i].species == PS_KRAKEN) kr = i;
    if (kr < 0) { fail("the director never raised the Grand Kraken"); return ok; }
    bool slammed = false, hit = false, snapped = false, left = false;
    for (int f = 0; f < 60 * 50 && !left; f++) {
        hold();
        BeastsUpdate(p, 1 / 60.0f);
        const Beast& k = W.beasts[kr];
        if (k.target == KE_SLAM && k.act == BeastAct::Eat) slammed = true;
        if (BeastsTouchDiver(p, PlatDiverBox(p))) hit = true;
        for (auto v : p.snapped) if (v) snapped = true;
        if (k.life != BeastLife::Alive || k.species != PS_KRAKEN) left = true;
    }
    int tornCol = -1;
    for (size_t k = 0; k < p.snaps.size(); k++) if (p.snapped[k]) tornCol = p.snaps[k].col;
    bool torn = tornCol >= 0;
    for (int y = std::max(0, s.top - p.genTop); torn && y <= s.bottom - p.genTop && tornCol == s.col; y++) if (PlatSolid(p, tornCol, y)) torn = false;
    TraceLog(LOG_WARNING, "verify-pirate13: kraken slammed %d, hit a diver standing still %d, snapped a ship %d (tear open %d), left %d", (int)slammed, (int)hit, (int)snapped, (int)torn, (int)left);
    if (!slammed) fail("the Grand Kraken never slammed a tentacle across the deck");
    if (!hit) fail("a slam never hit a diver who stood still under it");
    if (!snapped) fail("the Grand Kraken never snapped a ship");
    if (snapped && !torn) fail("a snapped ship still has timber across the tear");
    if (!left) fail("the Grand Kraken never left");
    // ---- the rest of the roster, on a bench: a ratline ladder with a yard, a crate porthole, a rotten plank run
    {
        auto bench = [&](PlatformState& q) {
            q = PlatformState{};
            q.level = PL_PIRATE; q.w = 70; q.h = 20;
            q.tiles.assign(q.h, std::string(q.w, '.'));
            for (int x = 0; x < q.w; x++) { q.tiles[18][x] = '#'; q.tiles[19][x] = '#'; }
            for (int y = 0; y < q.h; y++) { q.tiles[y][0] = '#'; q.tiles[y][q.w - 1] = '#'; }
            for (int y = 6; y <= 17; y++) q.tiles[y][10] = 'l';
            for (int x = 11; x <= 14; x++) q.tiles[6][x] = '=';
            BeastWorld& Q = q.fauna;
            Q = BeastWorld{}; Q.biome = PL_PIRATE; Q.active = true; Q.seed = 55; Q.rng = 5151;
            Q.nav.w = q.w; Q.nav.h = q.h; Q.nav.solid.assign((size_t)q.w * q.h, 0); Q.nav.hazard.assign((size_t)q.w * q.h, 0);
            for (int y = 0; y < q.h; y++) for (int x = 0; x < q.w; x++) Q.nav.solid[y * q.w + x] = PlatSolid(q, x, y) ? 1 : 0;
            Q.scent.Init(q.w, q.h, T); Q.apexT = -1e9f; // no Kraken on the bench
        };
        auto diverAt = [](PlatformState& q, Vector2 c, Vector2 v) { q.deathTimer = 0; Rectangle r = PlatDiverBox(q); q.pos.x += c.x - (r.x + r.width / 2); q.pos.y += c.y - (r.y + r.height / 2); q.vel = v; };
        auto mimicAt = [&](PlatformState& q) { int k = NewBeast(q.fauna, PS_CUTTLE, {13 * T + 16, 7 * T}); Beast& m = q.fauna.beasts[k]; m.anchor = {13 * T + 16, 7 * T}; m.act = BeastAct::Ambush; m.den = -1; m.pers.abnormal = Abnormal::None; return k; };
        // a leap off the ladder, untested: the mimic strikes
        PlatformState q; bench(q);
        int m = mimicAt(q);
        bool struck = false, hit = false;
        diverAt(q, {10 * T + 16, 9 * T}, {0, -150}); q.onWeed = true; BeastsUpdate(q, 1 / 60.0f);
        for (int f = 0; f < 60; f++) {
            q.onWeed = false; diverAt(q, {11.5f * T + 16, 9 * T}, {60, -40});
            BeastsUpdate(q, 1 / 60.0f);
            if (q.fauna.beasts[m].act == BeastAct::Strike) struck = true;
            if (BeastsTouchDiver(q, PlatDiverBox(q))) hit = true;
        }
        if (!struck || !hit) fail("a rigging-mimic didn't snatch a diver leaping off the ratlines untested");
        // testing the line first: it twitches, it's found out, and the leap is safe
        bench(q); m = mimicAt(q);
        for (int f = 0; f < 50; f++) { diverAt(q, {10 * T + 16, 9 * T}, {0, 0}); q.onWeed = true; BeastsUpdate(q, 1 / 60.0f); }
        bool revealed = q.fauna.beasts[m].act == BeastAct::Flee;
        for (int f = 0; f < 60; f++) { q.onWeed = false; diverAt(q, {11.5f * T + 16, 9 * T}, {220, -300}); BeastsUpdate(q, 1 / 60.0f); if (q.fauna.beasts[m].act == BeastAct::Strike) revealed = false; }
        if (!revealed) fail("pausing on the ratlines didn't reveal the rigging-mimic before the leap");
        // a lantern weed shows it up
        bench(q); m = mimicAt(q);
        { int l = NewBeast(q.fauna, PS_LANTERN, {11 * T, 10 * T}); q.fauna.beasts[l].den = -1; } // hanging on the mast
        BeastsUpdate(q, 1 / 60.0f);
        if (q.fauna.beasts[m].act != BeastAct::Flee) fail("a Siren's lantern weed didn't reveal the rigging-mimic");
        // the mantis: a fast diver across its line draws the strike; a walking one doesn't
        for (int fast = 0; fast < 2; fast++) {
            bench(q);
            q.tiles[17][30] = 'k';
            int mn = NewBeast(q.fauna, PS_MANTIS, {30 * T + 16, 17 * T + 16});
            Beast& M = q.fauna.beasts[mn]; M.facing = 1; M.anchor = {30 * T + 30, 17 * T + 18}; M.act = BeastAct::Ambush; M.den = -1;
            bool fired = false, killed = false;
            for (int f = 0; f < 120; f++) {
                diverAt(q, {36 * T, 17 * T + 18}, {fast ? 360.0f : 90.0f, 0});
                BeastsUpdate(q, 1 / 60.0f);
                if (q.fauna.beasts[mn].act == BeastAct::Strike) fired = true;
                if (BeastsTouchDiver(q, PlatDiverBox(q))) killed = true;
            }
            if (fast && (!fired || !killed)) fail("the cannoneer mantis didn't fire on a diver sprinting across its line");
            if (!fast && fired) fail("the cannoneer mantis fired on a diver walking past");
        }
        // the tortoise: it walks, carries a mover, and crushes a crate in its way
        bench(q);
        q.tiles[17][40] = 'k';
        { q.fauna.nav.solid[17 * q.w + 40] = 1; }
        int tt = NewBeast(q.fauna, PS_TORTOISE, {34 * T, 18 * T - 20}); q.fauna.beasts[tt].anchor = {34 * T, 18 * T}; q.fauna.beasts[tt].facing = 1; q.fauna.beasts[tt].den = -1;
        q.deathTimer = 1; q.pos = {-5000, -5000};
        for (int f = 0; f < 60 * 12; f++) BeastsUpdate(q, 1 / 60.0f);
        if (q.movers.empty()) fail("the tortoise's shell isn't a mover");
        if (PlatTileAt(q, 40, 17) == 'k') fail("the tortoise didn't crush the crate in its path");
        // borers: rushing across the rotten planks sets them to give way behind you
        bench(q);
        for (int x = 50; x < 55; x++) q.tiles[18][x] = 'f';
        { int bo = NewBeast(q.fauna, PS_BORER, {52.5f * T, 18 * T}); q.fauna.beasts[bo].anchor = {50, 18}; q.fauna.beasts[bo].special2 = 5; q.fauna.beasts[bo].den = -1; }
        diverAt(q, {51 * T, 18 * T - 14}, {640, 0}); q.pose = 5;
        BeastsUpdate(q, 1 / 60.0f);
        if (q.crumbles.size() < 5) fail("rushing over wood-borers didn't set the rotten planks to give way");
        q.pose = 0;
        // ship-rot, barnacle-cluster, cannon-moss, mast-kelp
        bench(q);
        int rot = NewBeast(q.fauna, PS_ROT, {20 * T, 18 * T}); q.fauna.beasts[rot].den = -1;
        diverAt(q, {20 * T, 18 * T - 14}, {300, 0}); BeastsUpdate(q, 1 / 60.0f);
        bool spores = false; for (const auto& c : q.fauna.ink) if (c.kind == 1) spores = true;
        if (!spores || q.fauna.scent.Sample(q.fauna.scent.blood, {20 * T, 18 * T - 8}) <= 0) fail("striking ship-rot didn't burst it into spores and a reek");
        int bar = NewBeast(q.fauna, PS_BARNACLE, {26 * T, 18 * T}); q.fauna.beasts[bar].den = -1;
        diverAt(q, {26 * T, 18 * T - 12}, {100, 0}); q.pose = 0; BeastsUpdate(q, 1 / 60.0f);
        bool walkSafe = !BeastsTouchDiver(q, PlatDiverBox(q));
        q.pose = 5; BeastsUpdate(q, 1 / 60.0f);
        bool dashDies = BeastsTouchDiver(q, PlatDiverBox(q));
        q.pose = 0;
        if (!walkSafe || !dashDies) fail("a barnacle-cluster should kill only a diver who dashes into it");
        int moss = NewBeast(q.fauna, PS_CMOSS, {32 * T, 18 * T}); q.fauna.beasts[moss].den = -1;
        diverAt(q, {32 * T, 18 * T - 13}, {0, 0});
        if (!BeastsSoftLanding(q)) fail("cannon-moss didn't count as a soft landing");
        int kelp = NewBeast(q.fauna, PS_MKELP, {44 * T, 18 * T}); q.fauna.beasts[kelp].den = -1;
        int dog = NewBeast(q.fauna, PS_DOG, {41 * T, 18 * T - 8}); q.fauna.beasts[dog].den = -1; q.fauna.beasts[dog].pers.abnormal = Abnormal::None;
        diverAt(q, {44 * T, 18 * T - 14}, {400, 0}); q.pose = 5; BeastsUpdate(q, 1 / 60.0f); q.pose = 0;
        if (q.fauna.beasts[dog].stunT <= 0) fail("dashing through mast-kelp didn't swing the spar into the dog on the diver's tail");
    }    if (ok) TraceLog(LOG_WARNING, "verify-pirate13: OK - the Kraken (director, slam, proven snap, leaving), the rigging-mimic (strike, tested line, lantern), the mantis, the tortoise, borers, rot, barnacles, cannon-moss and mast-kelp");
    return ok;
}

}  // namespace bk
