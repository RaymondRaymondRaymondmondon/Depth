// ============================================================================
//  DEPTH - Atlantis merged with ParkourReference1.3's Atlantis ("6. Atlantis").
//
//  Kept: glyph wisps, temple shrimp, anglerfish, Lost Ones. Merged: the stone guardian is the Phalanx Crustacean
//  (camouflaged as rubble; fast movement in front of it charges its crystal shell, then a hard-light shockwave sweeps
//  down its line), the glyph eel is the Gargoyle-Moray (it coils in its niche and watches the halls in polished
//  mirrors - it knows where you are around corners - but a sun-crystal kelp's glare blinds it). New: the Orichalcum
//  Leviathan (a crystal-crusted whale that runs the length of the plazas and back: a fast transit line to ride),
//  Poseidon's Scourge (the apex: the director wakes it behind you and it chases, the ruins collapsing in its wake -
//  keep moving), crystal-minnows (a hydro-dash startles them into a blinding flash that shakes off pursuers) and
//  mosaic-snails (their glassy slime makes you slick). Flora: sun-crystal kelp (glare), prism-moss (a landing turned
//  into a dash), aqueduct-vines (a swing round a pillar), stasis-lilies (a gel that stops a charge dead) and ruin-spores
//  (strike one and the stonework around it comes down on whatever follows).
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;
constexpr int SLIME = 2; // BeastProp kind: a mosaic-snail's slime
bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
bool Clear(const NavGrid& N, Vector2 at, int rx, int ry) {
    int cx = (int)floorf(at.x / T), cy = (int)floorf(at.y / T);
    for (int dy = -ry; dy <= ry; dy++) for (int dx = -rx; dx <= rx; dx++) if (!N.Open(cx + dx, cy + dy)) return false;
    return true;
}
bool Lethal(const BeastWorld& W, const Beast& o) { const SpeciesDef& S = Sp(W.biome, o.species); return S.lethal || S.diverPrey > 0; }

// ---------------------------------------------------------------- the Phalanx Crustacean's hard-light shockwave
// b.anchor.x: charge time; b.special2: beam time left; b.goal: the beam's far end.
void PhalanxHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    if (b.special2 > 0) { // the beam: everything in its line is crushed
        b.special2 -= dt;
        b.vel = {0, 0};
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.hidden || Flora(W, o) || Has(Sp(W.biome, o.species), T_GIANT)) continue;
            if ((o.pos.x - b.pos.x) * b.facing > 0 && fabsf(o.pos.x - b.pos.x) < fabsf(b.goal.x - b.pos.x) && fabsf(o.pos.y - b.pos.y) < 24) Kill(W, p, j, i);
        }
        return;
    }
    if (b.anchor.x > 0) { // charging: the crystal shell brightens
        b.anchor.x += dt; b.vel = {0, 0}; b.flashT = 0.2f;
        if (b.anchor.x > 0.6f) {
            b.anchor.x = 0; b.special2 = 0.35f; b.cooldown = 3;
            float end = b.pos.x;
            for (int k = 1; k <= 8; k++) { float x = b.pos.x + b.facing * k * T; if (W.nav.Solid((int)floorf(x / T), (int)floorf(b.pos.y / T))) break; end = x; }
            b.goal = {end + b.facing * 16, b.pos.y};
            W.sounds.push_back({b.pos, 1.3f, 0.4f, i});
        }
        return;
    }
    if (b.cooldown > 0 || !dv.alive || b.act == BeastAct::Strike) return;
    float ahead = (dv.pos.x - b.pos.x);
    bool fast = Len(dv.vel) > 300 || p.pose == 5;
    if (fast && fabsf(ahead) < 7 * T && fabsf(dv.pos.y - b.pos.y) < 1.5f * T && W.nav.LineOfSight(b.pos, dv.pos)) { b.facing = ahead > 0 ? 1.0f : -1.0f; b.anchor.x = 0.001f; b.special = 0; }
}

// ---------------------------------------------------------------- the Orichalcum Leviathan: a transit line
void OLeviathanHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    if (b.special > 0) { b.special -= dt; b.vel.x = 0; if (b.special <= 0) b.facing = -b.facing; } // a pause at each end: time to climb aboard
    else {
        Vector2 np{b.pos.x + b.facing * 130 * dt, b.anchor.y + sinf(W.time * 0.7f) * 4};
        if (Clear(W.nav, {np.x + b.facing * 2 * T, np.y}, 2, 1) && np.x > 4 * T && np.x < std::min(W.nav.w * T, W.limitX) - 4 * T) { b.vel.x = (np.x - b.pos.x) / dt; b.pos = np; }
        else b.special = 2.0f;
    }
    b.act = b.special > 0 ? BeastAct::Idle : BeastAct::Wander;
    W.lights.push_back({b.pos, 4 * T, 0.6f}); // its crystals glow
    p.movers.push_back({{b.pos.x - 60, b.pos.y - 24, 120, 6}, {b.special > 0 ? 0.0f : b.facing * 130, 0}});
}

// ---------------------------------------------------------------- Poseidon's Scourge: the chase
// b.anchor: where its head is going; b.territory: its head; b.special: time on the scene.
void ScourgeHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.special += dt;
    W.apexPos = b.pos;
    if (b.act == BeastAct::Flee) {
        b.pos.y += 80 * dt; b.pos.x -= b.facing * 120 * dt;
        if (b.actT > 3) { b.life = BeastLife::Gone; W.apexT = 0; W.calmT = 0; W.apexPos = {-1e9f, -1e9f}; }
        b.territory = b.pos;
        return;
    }
    if (!dv.alive) { b.act = BeastAct::Flee; b.actT = 0; return; }
    float d = Dist(dv.pos, b.pos);
    if (b.special > 18 || d > 26 * T) { b.act = BeastAct::Flee; b.actT = 0; return; }
    // it closes fast from far behind, then keeps a relentless pace: stop and it has you
    float speed = b.special < 2 ? 0 : d > 9 * T ? 380.0f : 250.0f;
    Vector2 dir = Norm(Sub(dv.pos, b.pos));
    b.vel = Mul(dir, speed);
    b.pos = Add(b.pos, Mul(b.vel, dt));
    if (fabsf(b.vel.x) > 5) b.facing = b.vel.x > 0 ? 1.0f : -1.0f;
    b.act = b.special < 2 ? BeastAct::Coil : BeastAct::Hunt; // (the first two seconds: its roar, as it wakes)
    b.territory = b.pos;
    // the ruins give way around it: crumbling stonework it passes breaks, and the rubble shakes loose
    int cx = (int)floorf(b.pos.x / T), cy = (int)floorf(b.pos.y / T);
    for (int y = cy - 3; y <= cy + 3; y++)
        for (int x = cx - 3; x <= cx + 3; x++) {
            if (PlatTileAt(p, x, y) != 'f') continue;
            bool has = false; for (auto& c : p.crumbles) if (c.tx == x && c.ty == y) has = true;
            if (!has) p.crumbles.push_back({x, y, 0.3f});
        }
    if (!p.verifying && W.nav.Solid(cx, cy) && fmodf(W.time, 0.15f) < dt) PlatBurst(p, b.pos, 6, Color{220, 214, 200, 255}, 200, 0.8f, 3); // smashing through marble
    for (int j = 0; j < (int)W.beasts.size(); j++) { Beast& o = W.beasts[j]; if (j != i && Alive(o) && !o.hidden && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && Dist(o.pos, b.pos) < 34) Kill(W, p, j, i); }
}

// ---------------------------------------------------------------- flora, fodder, the moray's mirrors
bool OnVine(const BeastWorld& W, const Beast& b, Rectangle box) { // an aqueduct-vine hanging from b.pos down b.special2 px
    Vector2 top = b.pos, bottom{b.pos.x + sinf(W.time * 0.9f + b.phase) * 6, b.pos.y + b.special2};
    Vector2 c{box.x + box.width / 2, box.y + box.height / 2};
    float u = Clamp01((c.y - top.y) / std::max(1.0f, bottom.y - top.y));
    Vector2 on{top.x + (bottom.x - top.x) * u, top.y + (bottom.y - top.y) * u};
    return Dist(on, c) < 16 && c.y > top.y - 6 && c.y < bottom.y + 10;
}
void AtlantisFlora(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    b.flashT -= dt;
    switch (b.species) {
    case AS_SUNKELP: W.lights.push_back({{b.pos.x, b.pos.y - 24}, 5 * T, 1.0f}); break; // a glare the morays can't bear (see the moray's hook)
    case AS_AVINE: { // an aqueduct-vine: an anchor, and a swing round a pillar
        if (b.act == BeastAct::Eat && b.actT > 0.5f) { b.act = BeastAct::Idle; b.actT = 0; }
        if (!dv.alive || !OnVine(W, b, box)) break;
        p.anchored = true;
        if (b.special > 0 || fabsf(p.vel.x) < 230 || p.onGround) break;
        float s = p.vel.x > 0 ? 1.0f : -1.0f, v = fabsf(p.vel.x);
        if (p.upHeld) p.vel = {s * 70, -std::min(v * 1.05f, 700.0f)};
        else if (p.inDown) p.vel = {s * 70, std::min(v * 0.9f, 620.0f)};
        else { p.vel = {-s * v * 0.95f, -120}; } // no direction held: the swing carries you back round the other way
        p.boostT = 0.5f; p.dashReady = true;
        b.special = 0.6f; b.act = BeastAct::Eat; b.actT = 0; b.facing = s;
        break;
    }
    case AS_LILY: // struck, a dense gel: anything charging nearby stops dead
        if (b.act == BeastAct::Drift) { if (b.actT > 15) b.act = BeastAct::Idle; break; }
        if (dv.alive && Len(dv.vel) > 150 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 12, box)) {
            b.act = BeastAct::Drift; b.actT = 0;
            for (auto& o : W.beasts) if (Alive(o) && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && Dist(o.pos, b.pos) < 4 * T) { o.stunT = std::max(o.stunT, 2.0f); o.vel = {0, 0}; if (o.act == BeastAct::Strike || o.act == BeastAct::Coil) o.act = BeastAct::Hunt; o.anchor.x = 0; o.special2 = 0; }
            AddCloud(W, {b.pos.x, b.pos.y - 10}, 4 * T, 0.8f, 4);
            if (!p.verifying) PlatBurst(p, {b.pos.x, b.pos.y - 8}, 14, Color{200, 240, 230, 255}, 100, 1.0f, 3);
        }
        break;
    case AS_RUINSPORE: // struck, the stonework it's eaten through gives way on whatever is behind you
        if (b.act == BeastAct::Drift) { if (b.actT > 25) b.act = BeastAct::Idle; break; }
        if (dv.alive && Len(dv.vel) > 150 && CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 12, box)) {
            b.act = BeastAct::Drift; b.actT = 0;
            float back = dv.vel.x > 0 ? -1.0f : 1.0f;
            W.sounds.push_back({b.pos, 1.2f, 0.5f, i});
            for (int j = 0; j < (int)W.beasts.size(); j++) {
                Beast& o = W.beasts[j];
                if (!Alive(o) || o.hidden || Flora(W, o) || Has(Sp(W.biome, o.species), T_GIANT)) continue;
                float dx = (o.pos.x - b.pos.x) * back;
                if (dx < -T || dx > 5 * T || fabsf(o.pos.y - b.pos.y) > 3 * T) continue;
                if (o.mass < 30) Kill(W, p, j, -1); else { o.stunT = std::max(o.stunT, 3.0f); Hurt(W, p, j, 0.4f, b.species, false); }
            }
            if (!p.verifying) for (int k = 0; k < 6; k++) PlatBurst(p, {b.pos.x + back * k * 20, b.pos.y - 3 * T}, 4, Color{220, 214, 200, 255}, 160, 1.0f, 4);
        }
        break;
    default: break;
    }
}

}  // namespace

void Atlantis13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case AS_GUARDIAN: PhalanxHook(W, p, i, dt); break;
    case AS_EEL: { // the Gargoyle-Moray: its mirrors show it the hall around the corner - unless the sun-crystals' glare is in them
        bool glare = false;
        for (const auto& o : W.beasts) if (Alive(o) && o.species == AS_SUNKELP && Dist(o.pos, b.pos) < 4.5f * T) glare = true;
        Diver dv = SeeDiver(p);
        if (glare) { Forget(b, BEAST_DIVER, 0); if (b.target == BEAST_DIVER) { b.act = BeastAct::Ambush; b.target = -1; } }
        else if (dv.alive && Dist(dv.pos, b.pos) < 5 * T) Remember(b, MEM_PREY, BEAST_DIVER, 0, dv.pos, dv.vel, 0.9f, W.time);
        break;
    }
    case AS_OLEVIATHAN: OLeviathanHook(W, p, i, dt); break;
    case AS_SCOURGE: ScourgeHook(W, p, i, dt); break;
    case AS_CMINNOW: { // startled by a hydro-dash: a blinding flash, and whatever was hunting you loses you
        Diver dv = SeeDiver(p);
        b.special -= dt;
        if (dv.alive && p.pose == 5 && b.special <= 0 && Dist(dv.pos, b.pos) < 50) {
            b.flashT = 1.2f; b.special = 6;
            for (auto& o : W.beasts) if (Alive(o) && Lethal(W, o) && Dist(o.pos, b.pos) < 4 * T) { Forget(o, BEAST_DIVER, 0); o.stunT = std::max(o.stunT, 1.0f); }
        }
        break;
    }
    case AS_SNAIL: // it leaves a glassy trail on the marble
        b.special -= dt;
        if (b.special <= 0 && b.grounded) { b.special = 0.8f; W.props.push_back({{b.pos.x, b.pos.y + 4}, {0, 0}, 0, SLIME, i}); }
        break;
    default: if (Flora(W, b)) AtlantisFlora(W, p, i, dt); break;
    }
}

bool Atlantis13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive || b.stunT > 0) return false;
    if (b.species == AS_GUARDIAN && b.special2 > 0) { // the hard-light beam
        float x0 = std::min(b.pos.x, b.goal.x), x1 = std::max(b.pos.x, b.goal.x);
        return CheckCollisionRecs({x0, b.pos.y - 20, x1 - x0, 40}, diver);
    }
    if (b.species == AS_SCOURGE && b.act == BeastAct::Hunt) return CheckCollisionCircleRec(b.territory, 26, diver);
    return false;
}

// Slime trails, and the director that wakes the Scourge.
void Atlantis13Tick(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    for (auto& k : W.props) if (k.kind == SLIME) k.t += dt;
    W.props.erase(std::remove_if(W.props.begin(), W.props.end(), [](const BeastProp& k) { return k.kind == SLIME && k.t > 25; }), W.props.end());
    while (std::count_if(W.props.begin(), W.props.end(), [](const BeastProp& k) { return k.kind == SLIME; }) > 80) { auto it = std::find_if(W.props.begin(), W.props.end(), [](const BeastProp& k) { return k.kind == SLIME; }); W.props.erase(it); }
    if (dv.alive && p.onGround) { Rectangle r = PlatDiverBox(p); for (const auto& k : W.props) if (k.kind == SLIME && fabsf(k.pos.x - (r.x + r.width / 2)) < 12 && fabsf(k.pos.y - (r.y + r.height)) < 8) { p.slickT = std::max(p.slickT, 0.5f); break; } }
    bool apex = false, threatened = false;
    for (const auto& b : W.beasts) {
        if (!Alive(b)) continue;
        if (b.species == AS_SCOURGE) apex = true;
        if (b.target == BEAST_DIVER && (b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike) && dv.alive && Dist(b.pos, dv.pos) < 10 * T) threatened = true;
    }
    if (!apex) W.apexPos = {-1e9f, -1e9f};
    W.tension = Clamp01(W.tension + (threatened ? 0.3f : -0.05f) * dt);
    W.calmT = threatened ? 0 : W.calmT + dt;
    W.apexT += dt;
    if (apex || !dv.alive || getenv("DEPTH_NOAPEX")) return;
    float wait = W.apexVisits == 0 ? 70.0f : 110.0f;
    if (W.apexT < wait || W.calmT < 20 || W.tension > 0.2f || !p.onGround || Len(dv.vel) < 150) return; // it wakes behind a diver who is on the move
    float back = dv.vel.x > 0 ? -1.0f : 1.0f;
    int k = NewBeast(W, AS_SCOURGE, {dv.pos.x + back * 14 * T, dv.pos.y - 2 * T});
    Beast& s = W.beasts[k];
    s.facing = -back; s.act = BeastAct::Coil; s.actT = 0; s.den = -1; s.special = 0; s.pers.abnormal = Abnormal::None; s.territory = s.pos;
    W.sounds.push_back({s.pos, 1.8f, 1.0f, k});
    W.apexVisits++; W.apexT = 0;
    if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "director: Poseidon's Scourge wakes behind tile %d", (int)(dv.pos.x / T));
}

// ---------------------------------------------------------------- the planner (at build time)
void Atlantis13Spawn(BeastWorld& W, PlatformState& p) {
    Spots S; S.Build(W, p, 12, W.nav.w - 8);
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].den = -1; W.beasts[k].act = BeastAct::Idle; W.beasts[k].pers.abnormal = Abnormal::None; return k; };
    auto root = [&](Vector2 f) { return Vector2{f.x, (f.y + 1) * T}; };
    // sun-crystal kelp by the moray niches (dens), about half of them
    for (size_t d = 0; d < W.dens.size(); d++) if (Hash(s, 11000 + (unsigned)d) < 0.5f) plant(AS_SUNKELP, {W.dens[d].pos.x + 2 * T, W.dens[d].pos.y});
    // aqueduct-vines hanging from overhangs, with room to swing under
    int vines = 0;
    for (int cx = 14; cx < W.nav.w - 10 && vines < 6; cx += 2) {
        if (cx * T > W.limitX - 4 * T) break;
        for (int c = 2; c < W.nav.h - 4; c++) {
            if (!W.nav.Solid(cx, c - 1) || !W.nav.Open(cx, c)) continue;
            int fy = W.nav.FloorBelow(cx, c);
            if (fy >= W.nav.h || fy - c < 4 || fy - c > 8 || Hash(s, 11100 + cx * 17 + c) > 0.2f) continue;
            int k = plant(AS_AVINE, {cx * T + 16.0f, c * T}); W.beasts[k].special2 = std::min(3.0f * T, (fy - c - 1.5f) * T);
            vines++; cx += 10; break;
        }
    }
    for (size_t i = 0; i < S.floor.size(); i += 6) {
        float h = Hash(s, 11200 + (unsigned)i);
        Vector2 f = S.floor[i];
        if (h < 0.10f) plant(AS_PRISM, root(f));
        else if (h < 0.18f) plant(AS_LILY, root(f));
        else if (h < 0.25f) plant(AS_RUINSPORE, root(f));
        else if (h < 0.30f) { int b = NewBeast(W, AS_SNAIL, S.Stand(f, 5)); W.beasts[b].pers.abnormal = Abnormal::None; }
    }
    for (int k = 0; k < 3; k++) { Vector2 c = S.Above(W, S.At(0.2f + 0.3f * k), 3); for (int j = 0; j < 6; j++) { int b = NewBeast(W, AS_CMINNOW, {c.x + (j % 3) * 8.0f, c.y + (j / 3) * 6.0f}); W.beasts[b].school = 700 + k; } }
    // the Orichalcum Leviathan where there's a long open lane
    for (int tries = 0; tries < 40; tries++) {
        Vector2 f = S.At(0.2f + 0.6f * Hash(s, 11300 + tries));
        Vector2 at{f.x, (f.y - 3) * T + 16};
        if (!Clear(W.nav, at, 2, 1) || !Clear(W.nav, {at.x + 5 * T, at.y}, 2, 1) || !Clear(W.nav, {at.x - 5 * T, at.y}, 2, 1)) continue;
        int b = NewBeast(W, AS_OLEVIATHAN, at); W.beasts[b].anchor = at; W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None;
        break;
    }
}

// ---------------------------------------------------------------- depth.exe --verify-atlantis-ecosystem (the 1.3 part)
bool VerifyAtlantis13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-atlantis13: FAILED - %s", what); ok = false; };
    auto bench = [](PlatformState& q, int w, int h) {
        q = PlatformState{};
        q.level = PL_ATLANTIS; q.w = w; q.h = h;
        q.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { q.tiles[h - 1][x] = '#'; q.tiles[h - 2][x] = '#'; q.tiles[0][x] = '#'; }
        for (int y = 0; y < h; y++) { q.tiles[y][0] = '#'; q.tiles[y][w - 1] = '#'; }
        q.pos = {-5000, -5000}; q.deathTimer = 1;
    };
    auto build = [](PlatformState& q) {
        BeastWorld& Q = q.fauna;
        Q = BeastWorld{}; Q.biome = PL_ATLANTIS; Q.active = true; Q.seed = 81; Q.rng = 8181;
        Q.nav.w = q.w; Q.nav.h = q.h; Q.nav.solid.assign((size_t)q.w * q.h, 0); Q.nav.hazard.assign((size_t)q.w * q.h, 0);
        for (int y = 0; y < q.h; y++) for (int x = 0; x < q.w; x++) Q.nav.solid[y * q.w + x] = PlatSolid(q, x, y) ? 1 : 0;
        Q.scent.Init(q.w, q.h, T); Q.apexT = -1e9f;
    };
    auto diverAt = [](PlatformState& q, Vector2 c, Vector2 v) { q.deathTimer = 0; Rectangle r = PlatDiverBox(q); q.pos.x += c.x - (r.x + r.width / 2); q.pos.y += c.y - (r.y + r.height / 2); q.vel = v; };
    auto calm = [](Beast& b) { b.pers.abnormal = Abnormal::None; b.den = -1; };
    const float dt = 1 / 60.0f;
    for (unsigned seed : {808u, 809u, 810u}) {
        PlatformState r; r.level = PL_ATLANTIS; r.layout = {(int)seed, 100};
        PlatBuildLevel(r);
        int n[AS_COUNT] = {0};
        for (const auto& b : r.fauna.beasts) if (b.life == BeastLife::Alive || b.hidden) n[b.species]++;
        TraceLog(LOG_WARNING, "verify-atlantis13: atlantis #%u has %d phalanx, %d morays, %d leviathan, %d minnows, %d snails; %d sun-kelp, %d prism, %d vines, %d lilies, %d ruin-spores", seed, n[AS_GUARDIAN], n[AS_EEL], n[AS_OLEVIATHAN], n[AS_CMINNOW], n[AS_SNAIL], n[AS_SUNKELP], n[AS_PRISM], n[AS_AVINE], n[AS_LILY], n[AS_RUINSPORE]);
    }
    PlatformState q;
    // 1) the Phalanx: fast in front of it and its beam fires; slow, it doesn't; a stasis-lily stops the charge
    for (int c = 0; c < 3; c++) {
        bench(q, 50, 16); build(q);
        int g = NewBeast(q.fauna, AS_GUARDIAN, {20 * T, 14 * T - 8}); calm(q.fauna.beasts[g]); q.fauna.beasts[g].special = 1;
        if (c == 2) { int l = NewBeast(q.fauna, AS_LILY, {23 * T, 14 * T}); calm(q.fauna.beasts[l]); }
        bool hit = false;
        for (int f = 0; f < 60; f++) {
            diverAt(q, {c == 2 ? 23 * T : 25 * T, 14 * T - 13}, {c == 1 ? 80.0f : 360.0f, 0}); q.onGround = true;
            BeastsUpdate(q, dt);
            if (BeastsTouchDiver(q, PlatDiverBox(q))) hit = true;
        }
        if (c == 0 && !hit) fail("the Phalanx's shockwave didn't fire on a diver racing in front of it");
        if (c == 1 && hit) fail("the Phalanx fired on a diver walking past");
        if (c == 2 && hit) fail("a stasis-lily didn't stop the Phalanx's charge");
    }
    // 2) prism-moss turns a landing into a dash; the Orichalcum Leviathan carries you; a snail's slime makes you slick
    {
        bench(q, 80, 20); build(q);
        int pm = NewBeast(q.fauna, AS_PRISM, {10 * T, 18 * T}); calm(q.fauna.beasts[pm]);
        diverAt(q, {10 * T, 18 * T - 13}, {0, 600}); q.facingRight = true;
        float r = BeastsLandingLaunch(q, 600);
        if (r >= 0 || q.vel.x < 500) fail("prism-moss didn't turn a landing into a dash");
        int ol = NewBeast(q.fauna, AS_OLEVIATHAN, {40 * T, 14 * T + 16}); calm(q.fauna.beasts[ol]); q.fauna.beasts[ol].anchor = {40 * T, 14 * T + 16}; q.fauna.beasts[ol].facing = 1;
        float x0 = q.fauna.beasts[ol].pos.x;
        q.deathTimer = 1; q.pos = {-5000, -5000};
        for (int f = 0; f < 60; f++) BeastsUpdate(q, dt);
        if (q.movers.empty() || q.fauna.beasts[ol].pos.x - x0 < 80) fail("the Orichalcum Leviathan isn't a fast-moving platform");
        int sn = NewBeast(q.fauna, AS_SNAIL, {60 * T, 18 * T - 5}); calm(q.fauna.beasts[sn]);
        for (int f = 0; f < 120; f++) BeastsUpdate(q, dt);
        Vector2 at = q.fauna.beasts[sn].pos;
        diverAt(q, {at.x, 18 * T - 13}, {0, 0}); q.onGround = true; q.slickT = 0;
        BeastsUpdate(q, dt);
        if (q.slickT <= 0) fail("a mosaic-snail's slime didn't make the diver slick");
    }
    // 3) crystal-minnows: a dash through them flashes, and the hunter nearby loses you
    {
        bench(q, 50, 16); build(q);
        int an = NewBeast(q.fauna, AS_ANGLER, {25 * T, 10 * T}); calm(q.fauna.beasts[an]);
        Remember(q.fauna.beasts[an], MEM_PREY, BEAST_DIVER, 0, {20 * T, 10 * T}, {0, 0}, 1.0f, 0);
        int mn = NewBeast(q.fauna, AS_CMINNOW, {22 * T, 10 * T}); calm(q.fauna.beasts[mn]);
        diverAt(q, {22 * T, 10 * T}, {600, 0}); q.pose = 5; BeastsUpdate(q, dt); q.pose = 0;
        float left = 0; for (const auto& m : q.fauna.beasts[an].mem) if (m.source == BEAST_DIVER) left = std::max(left, m.strength);
        if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "  minnow flash %.2f cd %.2f life %d pos (%.0f,%.0f) | angler memory %.2f stun %.2f life %d", q.fauna.beasts[mn].flashT, q.fauna.beasts[mn].cooldown, (int)q.fauna.beasts[mn].life, q.fauna.beasts[mn].pos.x, q.fauna.beasts[mn].pos.y, left, q.fauna.beasts[an].stunT, (int)q.fauna.beasts[an].life);
        if (q.fauna.beasts[mn].flashT <= 0 || left > 0.1f) fail("a dash through crystal-minnows didn't flash and blind the anglerfish");
    }
    // 4) Poseidon's Scourge: it wakes behind a running diver, chases, catches one who stops, and leaves
    {
        bench(q, 120, 20); build(q);
        diverAt(q, {60 * T, 18 * T - 13}, {300, 0}); q.onGround = true;
        q.fauna.apexT = 999; q.fauna.calmT = 999;
        BeastsUpdate(q, dt);
        int sc = -1; for (int i = 0; i < (int)q.fauna.beasts.size(); i++) if (Alive(q.fauna.beasts[i]) && q.fauna.beasts[i].species == AS_SCOURGE) sc = i;
        if (sc < 0) fail("the director never woke Poseidon's Scourge");
        else {
            bool caught = false, left = false;
            for (int f = 0; f < 60 * 25 && !left; f++) {
                diverAt(q, {60 * T, 18 * T - 13}, {0, 0}); q.onGround = true; // standing still
                BeastsUpdate(q, dt);
                if (BeastsTouchDiver(q, PlatDiverBox(q))) caught = true;
                if (!Alive(q.fauna.beasts[sc]) || q.fauna.beasts[sc].species != AS_SCOURGE) left = true;
            }
            if (!caught) fail("Poseidon's Scourge never caught a diver who stopped running");
            if (!left) fail("Poseidon's Scourge never left");
        }
    }
    if (ok) TraceLog(LOG_WARNING, "verify-atlantis13: OK - the Phalanx beam and the stasis-lily, prism-moss, the Orichalcum Leviathan, snail slime, crystal-minnows, Poseidon's Scourge");
    return ok;
}

}  // namespace bk
