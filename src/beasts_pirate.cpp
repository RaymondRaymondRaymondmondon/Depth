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
            b.target = KE_SNAP; b.carry = snap; b.act = BeastAct::Coil; b.actT = 0;
            W.sounds.push_back({{p.snaps[snap].col * T + 16, sea}, 1.0f, 0.6f, i});
        } else { // a slam across the deck where the diver stands (or is about to)
            float side = R(W) < 0.5f ? -1.0f : 1.0f;
            b.anchor = {dv.pos.x + side * R(W, 4, 7) * T, sea};
            b.goal = {dv.pos.x + std::clamp(dv.vel.x * 0.35f, -2 * T, 2 * T), dv.pos.y + 8};
            b.target = KE_SLAM; b.act = BeastAct::Coil; b.actT = 0;
        }
        break;
    }
    case BeastAct::Coil: {
        if (b.target == KE_SNAP) { if (b.actT > 2.2f) { PlatSnapShip(p, b.carry); b.special2++; b.act = BeastAct::Eat; b.actT = 0; } }
        else if (b.actT > 1.1f) { b.act = BeastAct::Strike; b.actT = 0; }
        break;
    }
    case BeastAct::Strike: if (b.actT > 0.22f) { SlamImpact(W, p, i); b.act = BeastAct::Eat; b.actT = 0; } break;
    case BeastAct::Eat: if (b.actT > (b.target == KE_SNAP ? 0.8f : 0.5f)) { b.act = BeastAct::Wander; b.actT = 0; } break;
    case BeastAct::Wander: if (b.actT > 0.8f) { b.act = BeastAct::Idle; b.actT = 0; b.cooldown = R(W, 2.5f, 4.0f); if (b.target == KE_SNAP) b.carry = -1; } break;
    default: b.act = BeastAct::Idle; break;
    }
    b.territory = SlamTip(b);
}

}  // namespace

void Pirate13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case PS_KRAKEN: KrakenHook(W, p, i, dt); break;
    default: break;
    }
}

bool Pirate13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive || b.species != PS_KRAKEN || b.target != KE_SLAM) return false;
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

// ---------------------------------------------------------------- depth.exe --verify-pirate-ecosystem (the 1.3 part)
bool VerifyPirate13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-pirate13: FAILED - %s", what); ok = false; };
    PlatformState p;
    p.level = PL_PIRATE; p.layout = {606, 100, 0, 0x7fffffff}; // every candidate snap, for the mechanics (the real game keeps only the proven ones)
    PlatBuildLevel(p);
    BeastWorld& W = p.fauna;
    if (p.snaps.empty()) { fail("the fleet has no snap points"); return ok; }
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
    if (ok) TraceLog(LOG_WARNING, "verify-pirate13: OK - the director, the slam and its tell, a proven snap, and the Kraken leaving");
    return ok;
}

}  // namespace bk
