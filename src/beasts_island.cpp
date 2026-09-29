// ============================================================================
//  DEPTH - the Island's ParkourReference1.3 roster (docs/ParkourReference1.3.txt, "3. The Island"), kept above water
//  (the user's call) - the lagoons become the island's ravines and open ground.
//
//  The Goliath Island Beetle lumbers across the open ground with the tribe's idols on its stone shell - a moving
//  platform. The Mangrove Stalker waits at the foot of drops, camouflaged, and snaps at whatever lands in front of it
//  (land on the move - roll out - or lure its snap through a poison-dart vine, which paralyses its jaw). The Totem-
//  Centipede feels the ground: hard landings, a drum-fungus's boom, or firefly dust glowing on you wake it, the tribe's
//  drums sound, and it bursts up through the ground ahead. The Arch-Serpent is the apex: the runtime director lets it
//  rise out of a ravine near the diver; it rears (a long, clear tell), strikes, and its thrashing smashes the bridges
//  behind you (never the way ahead; a respawn puts them back). Flora: drum-fungus (a landing launches you high - and
//  wakes the centipede), poison-dart vines (fatal to touch), razor-palm roots (trip you at a sprint; lacerate
//  pursuers), Idol's bloom (a fresh dash), mangrove root-sponges (a landing bounces back at full speed).
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;
bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
Vector2 Lerp(Vector2 a, Vector2 b, float u) { u = Clamp01(u); return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }
bool SegmentHits(Vector2 a, Vector2 b, float r, Rectangle box) {
    for (int k = 0; k <= 10; k++) if (CheckCollisionCircleRec(Lerp(a, b, k / 10.0f), r, box)) return true;
    return false;
}

// ---------------------------------------------------------------- the Goliath Island Beetle: a walking platform
void BeetleHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    int row = (int)floorf((b.anchor.y - 1) / T);
    if (b.special > 0) { b.special -= dt; b.vel.x = 0; if (b.special <= 0) b.facing = -b.facing; }
    else {
        int ax = (int)floorf((b.pos.x + b.facing * 44) / T);
        bool floor = W.nav.Solid(ax, row + 1) && !W.nav.Hazard(ax, row) && PlatTileAt(p, ax, row + 1) != 'f';
        bool clear = W.nav.Open(ax, row) && W.nav.Open(ax, row - 1);
        if (!floor || !clear) b.special = 1.5f;
        else b.vel.x = b.facing * 22;
    }
    b.pos.x += b.vel.x * dt;
    b.pos.y = b.anchor.y - 22;
    b.act = b.special > 0 ? BeastAct::Idle : BeastAct::Wander;
    p.movers.push_back({{b.pos.x - 36, b.anchor.y - 44, 72, 6}, b.vel}); // the flat stone top of its shell, idols and all
}

// ---------------------------------------------------------------- the Mangrove Stalker: it waits where you land
// b.anchor: its lair at the foot of a drop; b.goal: the snap's aim; b.territory: its jaws. Ambush -> Coil -> Strike.
Vector2 StalkerJaw(const Beast& b) {
    if (b.act == BeastAct::Strike) return Lerp(b.anchor, b.goal, b.actT / 0.18f);
    return {b.anchor.x + b.facing * 10, b.anchor.y};
}
void StalkerHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.pos = b.anchor; b.vel = {0, 0};
    b.stunT = std::max(0.0f, b.stunT - dt);
    bool landed = p.onGround && b.ignoreId == 0; // the moment the diver's feet hit the ground
    b.ignoreId = p.onGround ? 1 : 0;
    switch (b.act) {
    default: b.act = BeastAct::Ambush; break;
    case BeastAct::Ambush: {
        b.special = 1; // camouflaged among the roots
        if (b.stunT > 0 || b.cooldown > 0 || !dv.alive) break;
        if (landed && Dist(dv.pos, b.anchor) < 2.8f * T) { b.act = BeastAct::Coil; b.actT = 0; b.facing = dv.pos.x > b.anchor.x ? 1.0f : -1.0f; }
        break;
    }
    case BeastAct::Coil: b.special = 0; if (b.actT > 0.3f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = dv.alive ? dv.pos : b.anchor; if (Dist(b.goal, b.anchor) > 3 * T) b.goal = Add(b.anchor, Mul(Norm(Sub(b.goal, b.anchor)), 3 * T)); } break;
    case BeastAct::Strike: {
        Vector2 jaw = StalkerJaw(b);
        for (const auto& o : W.beasts) // snapping through a poison-dart vine: its jaw locks
            if (Alive(o) && o.species == IS_DARTVINE && SegmentHits(b.anchor, jaw, 8, {o.pos.x - 3, o.pos.y, 6, o.special2})) { b.stunT = 6; b.act = BeastAct::Idle; b.actT = 0; W.sounds.push_back({b.pos, 0.4f, 0.3f, i}); break; }
        if (b.act == BeastAct::Strike && b.actT > 0.45f) { b.act = BeastAct::Wander; b.actT = 0; b.cooldown = 3; }
        break;
    }
    case BeastAct::Idle: if (b.stunT <= 0) b.act = BeastAct::Ambush; break; // paralysed jaw
    case BeastAct::Wander: if (b.actT > 0.6f) b.act = BeastAct::Ambush; break;
    }
    b.territory = StalkerJaw(b);
}

// ---------------------------------------------------------------- the Totem-Centipede: it feels the ground
void CentipedeHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    switch (b.act) {
    default: b.act = BeastAct::Idle; break;
    case BeastAct::Idle: {
        for (const auto& s : W.sounds) if (s.source == -1 && dv.alive && Dist(s.pos, dv.pos) < 3 * T) b.special += s.intensity * dt * 3; // landings, booms (a hard landing adds about its loudness)
        if (W.glowSuitT > 0) b.special += dt * 0.5f; // firefly dust: it can feel where you are
        b.special = std::max(0.0f, b.special - dt * 0.15f);
        b.territory = b.anchor;
        if (b.special < 1.5f || b.cooldown > 0 || !dv.alive || !p.onGround) break;
        float ax = dv.pos.x + std::clamp(dv.vel.x * 0.6f, -4 * T, 4 * T);
        int cx = (int)floorf(ax / T), feet = (int)floorf((dv.pos.y + 14) / T), fy = W.nav.h;
        for (int yy = feet - 2; yy <= feet + 2; yy++) if (W.nav.Standable(cx, yy)) { fy = yy + 1; break; }
        if (fy >= W.nav.h) { b.special = 1.0f; break; }
        int top = fy - 1;
        while (top > 0 && W.nav.Open(cx, top - 1) && fy - top < 4) top--;
        b.anchor = {cx * T + 16, fy * T}; b.goal = {cx * T + 16, top * T + 6};
        b.act = BeastAct::Coil; b.actT = 0; b.special = 0;
        break;
    }
    case BeastAct::Coil: // it's waking: the tribe's drums, and the ground shakes where it will come up
        b.territory = b.anchor;
        if (fmodf(b.actT, 0.35f) < dt) W.sounds.push_back({b.anchor, 0.3f, 0.3f, i});
        if (!p.verifying && fmodf(b.actT, 0.1f) < dt) PlatBurst(p, {b.anchor.x + R(W, -20, 20), b.anchor.y - 2}, 2, Color{180, 150, 100, 255}, 60, 0.4f, 2);
        if (b.actT > 1.4f) { b.act = BeastAct::Strike; b.actT = 0; W.sounds.push_back({b.anchor, 1.4f, 0.5f, i}); if (!p.verifying) PlatBurst(p, b.anchor, 18, Color{190, 160, 110, 255}, 240, 0.7f, 3); }
        break;
    case BeastAct::Strike:
        b.territory = Lerp(b.anchor, b.goal, b.actT / 0.25f);
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (j != i && Alive(o) && !o.hidden && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && fabsf(o.pos.x - b.anchor.x) < 20 && o.pos.y > b.territory.y - 10 && o.pos.y < b.anchor.y) Kill(W, p, j, i);
        }
        if (b.actT > 0.6f) { b.act = BeastAct::Wander; b.actT = 0; }
        break;
    case BeastAct::Wander: b.territory = Lerp(b.goal, b.anchor, b.actT / 0.8f); if (b.actT > 0.8f) { b.act = BeastAct::Idle; b.actT = 0; b.cooldown = 7; } break;
    }
    b.pos = b.anchor;
}

// ---------------------------------------------------------------- the Arch-Serpent (apex)
// b.anchor: where it rises from the ravine; b.goal: its strike's aim; b.territory: its head. Explore (rising) -> Coil
// (rearing: the tell) -> Strike -> Eat (thrashing: the bridges behind you go) -> Wander (settling) -> ... -> Flee.
Vector2 SerpentHead(const Beast& b) {
    Vector2 reared{b.anchor.x, b.anchor.y - 8 * T};
    switch (b.act) {
    case BeastAct::Explore: return Lerp(b.anchor, Vector2{b.anchor.x, b.anchor.y - 5 * T}, b.actT / 1.5f);
    case BeastAct::Coil: return Lerp(Vector2{b.anchor.x, b.anchor.y - 5 * T}, reared, b.actT / 1.6f);
    case BeastAct::Strike: return Lerp(reared, b.goal, b.actT / 0.3f);
    case BeastAct::Eat: return b.goal;
    case BeastAct::Wander: return Lerp(b.goal, Vector2{b.anchor.x, b.anchor.y - 5 * T}, b.actT / 1.0f);
    case BeastAct::Flee: return Lerp(Vector2{b.anchor.x, b.anchor.y - 5 * T}, b.anchor, b.actT / 2.0f);
    default: return Vector2{b.anchor.x, b.anchor.y - 5 * T};
    }
}
void SerpentHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.special += dt;
    W.apexPos = b.anchor;
    if (b.act != BeastAct::Flee && (b.special > 40 || b.special2 >= 2)) { b.act = BeastAct::Flee; b.actT = 0; }
    switch (b.act) {
    case BeastAct::Explore: if (b.actT > 1.5f) { b.act = BeastAct::Coil; b.actT = 0; W.sounds.push_back({b.anchor, 1.2f, 0.6f, i}); } break;
    case BeastAct::Coil: if (b.actT > 1.6f) { b.act = BeastAct::Strike; b.actT = 0; b.goal = dv.alive ? dv.pos : b.goal; if (Dist(b.goal, b.anchor) > 10 * T) b.goal = Add(b.anchor, Mul(Norm(Sub(b.goal, b.anchor)), 10 * T)); } break;
    case BeastAct::Strike:
        if (b.actT > 0.3f) {
            b.act = BeastAct::Eat; b.actT = 0; b.special2++;
            W.sounds.push_back({b.goal, 1.5f, 0.5f, i});
            AddCloud(W, b.goal, 6 * T, 0.6f, 4);
            // its thrashing smashes the bridges it can reach - only behind the diver; a respawn puts them back
            for (int x = (int)floorf(b.anchor.x / T) - 8; x <= (int)floorf(b.anchor.x / T) + 8; x++) {
                if (dv.alive && (x * T - dv.pos.x) * (dv.vel.x >= 0 ? 1 : -1) > -3 * T) continue;
                for (int y = 0; y < W.nav.h; y++) {
                    if (PlatTileAt(p, x, y) != 'f') continue;
                    bool has = false;
                    for (auto& c : p.crumbles) if (c.tx == x && c.ty == y) has = true;
                    if (!has) p.crumbles.push_back({x, y, 0.3f});
                }
            }
            if (dv.alive && p.onGround && Dist(dv.pos, b.goal) < 5 * T) { p.vel.x += (dv.pos.x > b.goal.x ? 1 : -1) * 260.0f; p.vel.y = -240; p.onGround = false; } // the ground heaves
        }
        break;
    case BeastAct::Eat: if (b.actT > 0.6f) { b.act = BeastAct::Wander; b.actT = 0; } break;
    case BeastAct::Wander: if (b.actT > 1.0f) { b.act = BeastAct::Coil; b.actT = 0; } break;
    case BeastAct::Flee: if (b.actT > 2.0f) { b.life = BeastLife::Gone; W.apexT = 0; W.calmT = 0; W.apexPos = {-1e9f, -1e9f}; } break;
    default: b.act = BeastAct::Explore; b.actT = 0; break;
    }
    b.territory = SerpentHead(b);
    b.pos = b.territory;
}

// ---------------------------------------------------------------- flora and fodder
void IslandFlora(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    b.flashT -= dt;
    switch (b.species) {
    case IS_RAZOR: // sharp roots: at a sprint they trip you; a big pursuer is cut up (for good)
        if (dv.alive && p.onGround && fabsf(dv.vel.x) > 300 && CheckCollisionRecs({b.pos.x - 16, b.pos.y - 10, 32, 10}, box) && b.special2 <= 0) { p.vel.x *= 0.45f; b.special2 = 1; b.flashT = 0.3f; }
        if (!CheckCollisionRecs({b.pos.x - 16, b.pos.y - 10, 32, 10}, box)) b.special2 = 0;
        for (int j = 0; j < (int)W.beasts.size(); j++) { Beast& o = W.beasts[j]; if (Alive(o) && !Flora(W, o) && o.mass >= 3 && fabsf(o.pos.x - b.pos.x) < 16 && fabsf(o.pos.y - b.pos.y) < 24 && fabsf(o.vel.x) > 60) Hurt(W, p, j, 0.5f * dt, b.species, false); }
        break;
    case IS_BLOOM: // brush past it: a mist, and your dash is fresh
        if (dv.alive && b.cooldown <= 0 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 12, box)) { p.dashReady = true; b.cooldown = 3; b.flashT = 0.6f; if (!p.verifying) PlatBurst(p, {b.pos.x, b.pos.y - 8}, 10, Color{255, 220, 240, 255}, 80, 0.8f, 2); }
        break;
    default: break;
    }
}

}  // namespace

void Island13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case IS_GBEETLE: BeetleHook(W, p, i, dt); break;
    case IS_MSTALKER: StalkerHook(W, p, i, dt); break;
    case IS_CENTIPEDE: CentipedeHook(W, p, i, dt); break;
    case IS_SERPENT: SerpentHook(W, p, i, dt); break;
    case IS_SKIPPER: // it skips rather than walks
        if (b.grounded && fabsf(b.vel.x) > 10 && b.hopT <= 0) { b.vel.y = -220; b.vel.x *= 1.8f; b.hopT = 0.35f; }
        break;
    default: if (Flora(W, b)) IslandFlora(W, p, i, dt); break;
    }
}

bool Island13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive || b.stunT > 0) return false;
    if (b.species == IS_MSTALKER) return b.act == BeastAct::Strike && CheckCollisionCircleRec(b.territory, 13, diver);
    if (b.species == IS_CENTIPEDE) return b.act == BeastAct::Strike && SegmentHits(b.anchor, b.territory, 15, diver);
    if (b.species == IS_SERPENT) return (b.act == BeastAct::Strike || b.act == BeastAct::Eat) && SegmentHits(b.anchor, b.territory, 18, diver);
    if (b.species == IS_DARTVINE) return CheckCollisionRecs({b.pos.x - 3, b.pos.y, 6, b.special2}, diver); // the dripping strands
    return false;
}

// Firefly dust, and the director that lets the Arch-Serpent rise.
void Island13Tick(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    W.glowSuitT = std::max(0.0f, W.glowSuitT - dt);
    if (dv.alive && PlatTileAt(p, (int)floorf(dv.pos.x / T), (int)floorf((dv.pos.y + 10) / T)) == '~') W.glowSuitT = 0; // a pool rinses the firefly dust off
    if (dv.alive) for (const auto& o : W.beasts) if (Alive(o) && o.species == IS_FIREFLY && Dist(o.pos, dv.pos) < 26) { W.glowSuitT = 12; break; } // bright dust on the suit
    bool apex = false, threatened = false;
    for (const auto& b : W.beasts) {
        if (!Alive(b)) continue;
        if (b.species == IS_SERPENT) apex = true;
        if (b.target == BEAST_DIVER && (b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike) && dv.alive && Dist(b.pos, dv.pos) < 10 * T) threatened = true;
    }
    if (!apex) W.apexPos = {-1e9f, -1e9f};
    W.tension = Clamp01(W.tension + (threatened ? 0.3f : -0.05f) * dt);
    W.calmT = threatened ? 0 : W.calmT + dt;
    W.apexT += dt;
    if (apex || !dv.alive || getenv("DEPTH_NOAPEX")) return;
    float wait = W.apexVisits == 0 ? 60.0f : 100.0f;
    if (W.apexT < wait || W.calmT < 20 || W.tension > 0.2f || !p.onGround) return;
    // it lives in its lairs: it rises from one 4-14 tiles from the diver (ahead preferred)
    if (!W.lairs.empty()) {
        int best = -1; float bs = 1e9f;
        for (int k = 0; k < (int)W.lairs.size(); k++) {
            float d = W.lairs[k].x - dv.pos.x, ad = fabsf(d);
            if (ad < 4 * T || ad > 14 * T || W.lairs[k].x > W.limitX - 4 * T) continue;
            float score = ad + (d * (dv.vel.x >= 0 ? 1 : -1) < 0 ? 5 * T : 0);
            if (score < bs) { bs = score; best = k; }
        }
        if (best < 0) return;
        int k = NewBeast(W, IS_SERPENT, W.lairs[best]);
        Beast& sp = W.beasts[k];
        sp.anchor = W.lairs[best]; sp.act = BeastAct::Explore; sp.actT = 0; sp.den = -1; sp.special = 0; sp.special2 = 0; sp.pers.abnormal = Abnormal::None;
        sp.goal = dv.pos;
        W.apexVisits++; W.apexT = 0;
        return;
    }
    // (no lairs - a synthetic test arena): a ravine near the diver, a column whose floor is far below the diver's
    int feet = (int)floorf((dv.pos.y + 14) / T);
    for (int d = 4; d <= 14; d++)
        for (int sgn = 1; sgn >= -1; sgn -= 2) {
            int cx = (int)floorf(dv.pos.x / T) + sgn * d;
            if (cx < 3 || cx >= W.nav.w - 3 || cx * T > W.limitX - 4 * T) continue;
            bool open = true;
            for (int y = feet - 4; y <= feet + 1 && open; y++) open = W.nav.Open(cx, y);
            int fl = W.nav.FloorBelow(cx, feet);
            if (!open || (fl < W.nav.h && fl - feet < 5)) continue; // (no floor at all below counts as a ravine too)
            int k = NewBeast(W, IS_SERPENT, {cx * T + 16, (feet + 5) * T});
            Beast& s = W.beasts[k];
            s.anchor = {cx * T + 16, std::min((float)fl, (float)(feet + 6)) * T}; s.act = BeastAct::Explore; s.actT = 0; s.den = -1; s.special = 0; s.special2 = 0; s.pers.abnormal = Abnormal::None;
            s.goal = dv.pos;
            W.apexVisits++; W.apexT = 0;
            if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "director: the Arch-Serpent rises from the ravine at tile %d", cx);
            return;
        }
}

// ---------------------------------------------------------------- the planner (at build time)
void Island13Spawn(BeastWorld& W, PlatformState& p) {
    Spots S; S.Build(W, p, 12, W.nav.w - 8);
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].den = -1; W.beasts[k].act = BeastAct::Idle; W.beasts[k].pers.abnormal = Abnormal::None; return k; };
    auto root = [&](Vector2 f) { return Vector2{f.x, (f.y + 1) * T}; };
    // mangrove stalkers at the foot of drops (a spot three or more rows below the one before it)
    int stalkers = 0;
    for (size_t i = 1; i < S.floor.size() && stalkers < 3; i++) {
        if (S.floor[i].y - S.floor[i - 1].y < 3 || S.floor[i].x - S.floor[i - 1].x > 1.5f * T || Hash(s, 9700 + (unsigned)i) > 0.6f) continue;
        size_t j = std::min(S.floor.size() - 1, i + 1);
        Vector2 f = S.floor[j];
        int k = NewBeast(W, IS_MSTALKER, root(f));
        Beast& m = W.beasts[k]; m.anchor = {f.x, (f.y + 1) * T - 10}; m.den = -1; m.act = BeastAct::Ambush; m.pers.abnormal = Abnormal::None; m.facing = -1;
        // a poison-dart vine hangs by most of them, if there's a bough to hang it from - the counter
        stalkers++; i += 20;
    }
    // poison-dart vines under boughs and overhangs with room to walk beneath (their tips stay 1.3 tiles clear of the floor)
    int vines = 0;
    for (int cx = 14; cx < W.nav.w - 10 && vines < 8; cx += 2) { // any overhang - a bough, a hut floor, a temple lintel - with standing room under it
        if (cx * T > W.limitX - 4 * T) break;
        for (int c = 2; c < W.nav.h - 4; c++) {
            if (!W.nav.Solid(cx, c - 1) || !W.nav.Open(cx, c)) continue;
            int fy = W.nav.FloorBelow(cx, c);
            int gap = fy - c;
            if (fy >= W.nav.h || gap < 3 || gap > 6 || W.nav.Hazard(cx, fy - 1) || Hash(s, 9800 + cx * 31 + c) > 0.25f) continue;
            float top = c * T, len = std::min(2.0f * T, fy * T - 1.3f * T - top);
            if (len < 12) continue;
            int k = plant(IS_DARTVINE, {cx * T + 16.0f, top}); W.beasts[k].special2 = len;
            vines++; cx += 6; break;
        }
    }
    // along the ground: drum-fungus, razor roots, Idol's bloom (fireflies about it), root-sponges
    for (size_t i = 0; i < S.floor.size(); i += 6) {
        float h = Hash(s, 9900 + (unsigned)i);
        Vector2 f = S.floor[i];
        if (h < 0.08f) plant(IS_DRUM, root(f));
        else if (h < 0.16f) plant(IS_RAZOR, root(f));
        else if (h < 0.22f) { plant(IS_BLOOM, root(f)); for (int k = 0; k < 5; k++) { int b = NewBeast(W, IS_FIREFLY, {f.x + (Hash(s, 9950 + (unsigned)i + k) - 0.5f) * 40, f.y * T - Hash(s, 9990 + (unsigned)i + k) * 30}); W.beasts[b].school = 400 + (int)i; W.beasts[b].den = -1; } }
        else if (h < 0.29f) plant(IS_SPONGE, root(f));
    }
    // mud-skippers; the beetle on a long run of ground; the centipede under the middle of the island
    for (int k = 0; k < 2; k++) { Vector2 c = S.Stand(S.At(0.3f + 0.4f * k), 4); for (int j = 0; j < 4; j++) { int b = NewBeast(W, IS_SKIPPER, {c.x + (j - 1.5f) * 10, c.y}); W.beasts[b].school = 500 + k; } }
    for (size_t i = S.floor.size() / 3; i + 10 < S.floor.size(); i++) {
        bool flat = true;
        for (int k = 1; k < 10 && flat; k++) flat = S.floor[i + k].y == S.floor[i].y && S.floor[i + k].x - S.floor[i].x == k * T;
        if (!flat) continue;
        Vector2 f = S.floor[i + 5];
        int b = NewBeast(W, IS_GBEETLE, {f.x, root(f).y - 22}); W.beasts[b].anchor = root(f); W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None;
        break;
    }
    // the Arch-Serpent's lairs: burrows in open ground with headroom to rear, spread along the island
    for (float fr : {0.3f, 0.58f, 0.84f}) {
        Vector2 f = S.At(fr); bool room = true;
        for (int y = (int)f.y - 9; y < (int)f.y && room; y++) room = W.nav.Open((int)floorf(f.x / T), y);
        if (room) W.lairs.push_back(root(f));
    }
    { int b = NewBeast(W, IS_CENTIPEDE, root(S.At(0.5f))); W.beasts[b].anchor = root(S.At(0.5f)); W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None; }
}

// ---------------------------------------------------------------- depth.exe --verify-island-ecosystem (the 1.3 part)
bool VerifyIsland13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-island13: FAILED - %s", what); ok = false; };
    auto bench = [](PlatformState& q, int w, int h) {
        q = PlatformState{};
        q.level = PL_ISLAND; q.w = w; q.h = h;
        q.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { q.tiles[h - 1][x] = '#'; q.tiles[h - 2][x] = '#'; }
        for (int y = 0; y < h; y++) { q.tiles[y][0] = '#'; q.tiles[y][w - 1] = '#'; }
        q.pos = {-5000, -5000}; q.deathTimer = 1;
    };
    auto build = [](PlatformState& q) {
        BeastWorld& Q = q.fauna;
        Q = BeastWorld{}; Q.biome = PL_ISLAND; Q.active = true; Q.seed = 33; Q.rng = 4545;
        Q.nav.w = q.w; Q.nav.h = q.h; Q.nav.solid.assign((size_t)q.w * q.h, 0); Q.nav.hazard.assign((size_t)q.w * q.h, 0);
        for (int y = 0; y < q.h; y++) for (int x = 0; x < q.w; x++) Q.nav.solid[y * q.w + x] = PlatSolid(q, x, y) ? 1 : 0;
        Q.scent.Init(q.w, q.h, T); Q.apexT = -1e9f;
    };
    auto diverAt = [](PlatformState& q, Vector2 c, Vector2 v) { q.deathTimer = 0; Rectangle r = PlatDiverBox(q); q.pos.x += c.x - (r.x + r.width / 2); q.pos.y += c.y - (r.y + r.height / 2); q.vel = v; };
    auto calm = [](Beast& b) { b.pers.abnormal = Abnormal::None; b.den = -1; };
    const float dt = 1 / 60.0f;
    for (unsigned seed : {303u, 304u, 305u}) {
        PlatformState r; r.level = PL_ISLAND; r.layout = {(int)seed, 100};
        PlatBuildLevel(r);
        int n[IS_COUNT] = {0};
        for (const auto& b : r.fauna.beasts) if (b.life == BeastLife::Alive) n[b.species]++;
        TraceLog(LOG_WARNING, "verify-island13: island #%u has %d stalkers, %d centipede, %d beetle, %d skippers, %d fireflies; %d drum, %d dart-vine, %d razor, %d bloom, %d sponge", seed, n[IS_MSTALKER], n[IS_CENTIPEDE], n[IS_GBEETLE], n[IS_SKIPPER], n[IS_FIREFLY], n[IS_DRUM], n[IS_DARTVINE], n[IS_RAZOR], n[IS_BLOOM], n[IS_SPONGE]);
        if (n[IS_CENTIPEDE] < 1) fail("a real island has no Totem-Centipede");
    }
    PlatformState q;
    // 1) the Mangrove Stalker snaps at a diver landing in front of it; snapping through a dart-vine locks its jaw
    for (int vine = 0; vine < 2; vine++) {
        bench(q, 50, 16); build(q);
        int st = NewBeast(q.fauna, IS_MSTALKER, {20 * T, 14 * T}); calm(q.fauna.beasts[st]); q.fauna.beasts[st].anchor = {20 * T, 14 * T - 10}; q.fauna.beasts[st].act = BeastAct::Ambush;
        if (vine) { int v = NewBeast(q.fauna, IS_DARTVINE, {21 * T, 11 * T}); calm(q.fauna.beasts[v]); q.fauna.beasts[v].special2 = 2.6f * T; }
        diverAt(q, {22 * T, 12 * T}, {0, 200}); q.onGround = false; BeastsUpdate(q, dt);
        bool hit = false, locked = false;
        for (int f = 0; f < 60; f++) {
            diverAt(q, {22 * T, 14 * T - 13}, {0, 0}); q.onGround = true;
            BeastsUpdate(q, dt);
            if (!vine && Island13Touch(q.fauna, q.fauna.beasts[st], PlatDiverBox(q))) hit = true;
            if (q.fauna.beasts[st].stunT > 0) locked = true;
        }
        if (!vine && !hit) fail("the Mangrove Stalker didn't snap at a diver landing in front of it");
        if (vine && !locked) fail("snapping through a poison-dart vine didn't lock the stalker's jaw");
    }
    // 2) the Totem-Centipede: hard landings wake it - drums, a shudder - and it bursts up ahead
    {
        bench(q, 60, 16); build(q);
        int ce = NewBeast(q.fauna, IS_CENTIPEDE, {30 * T, 14 * T}); calm(q.fauna.beasts[ce]); q.fauna.beasts[ce].anchor = {30 * T, 14 * T};
        bool woke = false, burst = false;
        for (int f = 0; f < 60 * 5 && !burst; f++) {
            diverAt(q, {26 * T, 14 * T - 13}, {0, 0}); q.onGround = true;
            if (f % 30 == 0) BeastsNoise(q, {26 * T, 14 * T}, 1.0f); // a hard landing
            BeastsUpdate(q, dt);
            if (q.fauna.beasts[ce].act == BeastAct::Coil) woke = true;
            if (q.fauna.beasts[ce].act == BeastAct::Strike) burst = true;
        }
        if (!woke || !burst) fail("hard landings didn't wake the Totem-Centipede");
    }
    // 3) flora: a sponge bounces a landing back; a drum-fungus launches you and booms; Idol's bloom refreshes the dash; razor roots trip a sprint
    {
        bench(q, 60, 16); build(q);
        int sp = NewBeast(q.fauna, IS_SPONGE, {10 * T, 14 * T}); calm(q.fauna.beasts[sp]);
        diverAt(q, {10 * T, 14 * T - 13}, {0, 0}); q.onGround = true;
        if (BeastsLandingLaunch(q, 600) < 560) fail("a root-sponge didn't throw a landing back at full speed");
        int dr = NewBeast(q.fauna, IS_DRUM, {20 * T, 14 * T}); calm(q.fauna.beasts[dr]);
        diverAt(q, {20 * T, 14 * T - 13}, {0, 0});
        size_t sounds = q.fauna.sounds.size();
        if (BeastsLandingLaunch(q, 500) < 900 || q.fauna.sounds.size() == sounds) fail("a drum-fungus didn't launch the diver with a boom");
        int bl = NewBeast(q.fauna, IS_BLOOM, {30 * T, 14 * T}); calm(q.fauna.beasts[bl]);
        diverAt(q, {30 * T, 14 * T - 12}, {100, 0}); q.dashReady = false;
        BeastsUpdate(q, dt);
        if (!q.dashReady) fail("Idol's bloom didn't refresh the diver's dash");
        int rz = NewBeast(q.fauna, IS_RAZOR, {40 * T, 14 * T}); calm(q.fauna.beasts[rz]);
        diverAt(q, {40 * T, 14 * T - 13}, {340, 0}); q.onGround = true;
        BeastsUpdate(q, dt);
        if (q.vel.x > 200) fail("razor-palm roots didn't trip a sprinting diver");
    }
    // 4) the Arch-Serpent: the director raises it from a ravine by the diver; it rears, strikes, and smashes the bridge behind
    {
        bench(q, 70, 20);
        for (int x = 30; x <= 34; x++) { q.tiles[18][x] = '.'; q.tiles[19][x] = '.'; } // a ravine
        for (int x = 20; x <= 24; x++) { q.tiles[18][x] = 'f'; } // a bridge (plank) behind the diver
        build(q);
        diverAt(q, {27 * T, 18 * T - 13}, {150, 0}); q.onGround = true;
        q.fauna.apexT = 999; q.fauna.calmT = 999;
        BeastsUpdate(q, dt);
        int se = -1;
        for (int i = 0; i < (int)q.fauna.beasts.size(); i++) if (Alive(q.fauna.beasts[i]) && q.fauna.beasts[i].species == IS_SERPENT) se = i;
        if (se < 0) fail("the director never raised the Arch-Serpent");
        else {
            bool reared = false, hit = false, left = false;
            for (int f = 0; f < 60 * 45 && !left; f++) {
                diverAt(q, {27 * T, 18 * T - 13}, {150, 0}); q.onGround = true;
                BeastsUpdate(q, dt);
                const Beast& b = q.fauna.beasts[se];
                if (b.act == BeastAct::Coil) reared = true;
                if (Island13Touch(q.fauna, b, PlatDiverBox(q))) hit = true;
                if (!Alive(b) || b.species != IS_SERPENT) left = true;
            }
            if (!reared || !hit) fail("the Arch-Serpent didn't rear and strike at a diver standing by its ravine");
            if (q.crumbles.empty() && q.tiles[18][22] == 'f') fail("the Arch-Serpent's thrashing didn't smash the bridge behind the diver");
            if (!left) fail("the Arch-Serpent never left");
        }
    }
    if (ok) TraceLog(LOG_WARNING, "verify-island13: OK - the Mangrove Stalker and the dart-vine, the Totem-Centipede, sponges, drum-fungus, Idol's bloom, razor roots, and the Arch-Serpent");
    return ok;
}

}  // namespace bk
