// ============================================================================
//  DEPTH - the Cave's ParkourReference1.3 roster (docs/ParkourReference1.3.txt, "4. The Cave"). The Cave is a
//  flooded, pitch-black cavern (the user's correction), so its whole ecology is aquatic and acoustic.
//
//  The Crystal-Shelled Tortoise is moving cover: tall enough to walk under, and its shell turns aside the leeches
//  that drop from the ceiling. The Echo-Stalker is blind - it hunts noise, and tests the dark by throwing rocks and
//  listening for what moves. The Tremor Worm swims through solid rock: run or dash hard across its ground and the
//  floor ahead shudders (the loaches bolt) before it bursts up through it. The Abyssal Arachnid is the apex: the
//  runtime director lets it in after a calm stretch, and it strings near-invisible webs across the tunnels ahead,
//  from the ceiling to just off the floor - slide under them. Each web carries a drained husk as a warning; glow-
//  shrimp fluid on your suit, or a lantern-shroom's light, shows the strands. The flora are the tools: lantern-
//  shrooms light junctions, acid-lichen kills on touch but strips what you lure into it, a vibration-spore's blast
//  deafens the stalker and calls up the worm, cave-cabbage hides you if you slide through it, nerve-root pins fodder.
// ============================================================================
#include "beasts_internal.h"
#include <cstdlib>

namespace bk {
namespace {

constexpr float T = TILE;
constexpr int ROCK = 1; // BeastProp kind: a rock the Echo-Stalker has thrown

bool Flora(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_FLORA); }
Vector2 Lerp(Vector2 a, Vector2 b, float u) { u = Clamp01(u); return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }
bool SegmentHits(Vector2 a, Vector2 b, float r, Rectangle box) {
    for (int k = 0; k <= 10; k++) if (CheckCollisionCircleRec(Lerp(a, b, k / 10.0f), r, box)) return true;
    return false;
}

// ---------------------------------------------------------------- the Echo-Stalker: a blind hunter of sound
void StalkerHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    b.special2 = std::max(0.0f, b.special2 - dt); // deafened (a vibration-spore's blast)
    if (b.special2 > 0) { for (auto& m : b.mem) if (m.source == BEAST_DIVER) m.strength = 0; return; }
    Diver dv = SeeDiver(p);
    const SpeciesDef& S = Sp(W.biome, b.species);
    for (const auto& s : W.sounds) { // any sound not its own - above all the diver's - is something to hunt
        if (s.source != -1) continue;
        float d = Dist(s.pos, b.pos), heard = s.intensity * S.hearing / (1.0f + 0.00012f * d * d);
        if (heard > 0.08f && dv.alive) Remember(b, MEM_PREY, BEAST_DIVER, 0, s.pos, {0, 0}, std::min(1.0f, heard * 3), W.time);
    }
    // nothing to go on: it throws a stone into the dark, and listens for what moves when it lands
    float known = 0;
    for (const auto& m : b.mem) if (m.source == BEAST_DIVER && m.kind == MEM_PREY) known = std::max(known, Recall(b, m, W.time));
    if (known < 0.1f && b.cooldown <= 0 && b.act != BeastAct::Coil && b.act != BeastAct::Strike) {
        b.cooldown = R(W, 5, 7);
        W.props.push_back({{b.pos.x, b.pos.y - 10}, {b.facing * R(W, 180, 300), -220}, 0, ROCK, i});
    }
}

// ---------------------------------------------------------------- the Tremor Worm: it swims through the rock
// b.special: how much running it has felt; b.anchor: the breach point (on the floor); b.goal: how far up it bursts;
// b.territory: its head. Idle -> Coil (1.2 s of shuddering floor) -> Strike (up through the tunnel) -> Wander (back down).
void TremorHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    bool running = dv.alive && ((p.onGround && fabsf(dv.vel.x) > 300) || p.pose == 5 || p.pose == 1);
    switch (b.act) {
    default: b.act = BeastAct::Idle; break;
    case BeastAct::Idle: {
        b.special = running ? b.special + dt : std::max(0.0f, b.special - dt * 0.5f);
        b.territory = b.anchor;
        if (b.special < 1.2f || b.cooldown > 0 || !dv.alive) break;
        // it rises where the runner is going
        float ax = dv.pos.x + std::clamp(dv.vel.x * 0.7f, -5 * T, 5 * T);
        int cx = (int)floorf(ax / T), fy = W.nav.FloorBelow(cx, (int)floorf(dv.pos.y / T) - 1);
        if (fy >= W.nav.h || abs(fy - (int)floorf((dv.pos.y + 16) / T)) > 2 || !W.nav.Open(cx, fy - 1)) { b.special = 0.8f; break; }
        int top = fy - 1;
        while (top > 0 && W.nav.Open(cx, top - 1) && fy - top < 4) top--;
        b.anchor = {cx * T + 16, fy * T}; b.goal = {cx * T + 16, top * T + 6};
        b.act = BeastAct::Coil; b.actT = 0; b.special = 0;
        break;
    }
    case BeastAct::Coil: { // the tell: the floor shudders, grit lifts, and the loaches bolt
        b.territory = b.anchor;
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (o.species != CS_LOACH || !Alive(o) || Dist(o.pos, b.anchor) > 8 * T) continue;
            Remember(o, MEM_THREAT, i, b.id, b.anchor, {0, 0}, 1.0f, W.time);
            if (o.act != BeastAct::Flee) { o.act = BeastAct::Flee; o.actT = 0; o.goal = FleeTarget(W, j, b.anchor); o.thinkT = 1; }
        }
        if (fmodf(b.actT, 0.3f) < dt) W.sounds.push_back({b.anchor, 0.35f, 0.3f, i});
        if (!p.verifying && fmodf(b.actT, 0.1f) < dt) PlatBurst(p, {b.anchor.x + R(W, -20, 20), b.anchor.y - 2}, 2, Color{90, 84, 80, 255}, 60, 0.4f, 2);
        if (b.actT > 1.2f) { b.act = BeastAct::Strike; b.actT = 0; W.sounds.push_back({b.anchor, 1.4f, 0.5f, i}); if (!p.verifying) PlatBurst(p, b.anchor, 18, Color{110, 100, 96, 255}, 240, 0.7f, 3); }
        break;
    }
    case BeastAct::Strike: {
        b.territory = Lerp(b.anchor, b.goal, b.actT / 0.25f);
        for (int j = 0; j < (int)W.beasts.size(); j++) { // whatever is in the way is taken
            Beast& o = W.beasts[j];
            if (j != i && Alive(o) && !o.hidden && !Flora(W, o) && !Has(Sp(W.biome, o.species), T_GIANT) && fabsf(o.pos.x - b.anchor.x) < 20 && o.pos.y > b.territory.y - 10 && o.pos.y < b.anchor.y) Kill(W, p, j, i);
        }
        if (b.actT > 0.6f) { b.act = BeastAct::Wander; b.actT = 0; }
        break;
    }
    case BeastAct::Wander: b.territory = Lerp(b.goal, b.anchor, b.actT / 0.8f); if (b.actT > 0.8f) { b.act = BeastAct::Idle; b.actT = 0; b.cooldown = 6; } break;
    }
    b.pos = b.anchor;
}

// ---------------------------------------------------------------- the Crystal-Shelled Tortoise: moving cover
void TortoiseHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    if (b.facing == 0) b.facing = 1;
    int row = (int)floorf((b.anchor.y - 1) / T);
    if (b.special > 0) { b.special -= dt; b.vel.x = 0; if (b.special <= 0 && b.special2 > 0) { b.facing = -b.facing; b.special2 = 0; } }
    else {
        int ax = (int)floorf((b.pos.x + b.facing * 46) / T);
        bool floor = W.nav.Solid(ax, row + 1) && !W.nav.Hazard(ax, row);
        bool clear = W.nav.Open(ax, row) && W.nav.Open(ax, row - 1) && W.nav.Open(ax, row - 2);
        if (!floor || !clear) { b.special = 1.5f; b.special2 = 1; }
        else if (Hash((unsigned)b.id, (unsigned)(W.time * 0.25f)) < 0.2f) b.special = 2.0f; // grazing the glowing moss a while
        else b.vel.x = b.facing * 20;
    }
    b.pos.x += b.vel.x * dt;
    b.pos.y = b.anchor.y - 52;
    b.act = b.special > 0 ? BeastAct::Idle : BeastAct::Wander;
    p.movers.push_back({{b.pos.x - 40, b.anchor.y - 72, 80, 6}, b.vel}); // the crystal dome's top
    for (auto& o : W.beasts) // the shell turns a dropping leech aside
        if (Alive(o) && o.species == CS_LEECH && o.special == 1 && fabsf(o.pos.x - b.pos.x) < 46 && o.pos.y > b.anchor.y - 80 && o.pos.y < b.anchor.y - 30) {
            o.special = 2; o.actT = 0; o.vel = {(o.pos.x > b.pos.x ? 1.0f : -1.0f) * 160, -40};
        }
    W.lights.push_back({{b.pos.x, b.anchor.y - 60}, 3 * T, 0.5f}); // its crystals glow
}

// ---------------------------------------------------------------- the Abyssal Arachnid (apex) and its webs
bool UnderWeb(const BeastWorld::Web& w, Rectangle box) { return box.x < w.top.x + 3 && box.x + box.width > w.top.x - 3 && box.y < w.bottom.y && box.y + box.height > w.top.y; }
void ArachnidHook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    b.special += dt;
    W.apexPos = b.pos;
    // lurking in the dark over its webs; anything caught in one, it comes down for
    float lastX = -1e9f;
    for (const auto& w : W.webs) lastX = std::max(lastX, w.top.x);
    bool passed = dv.alive && (dv.pos.x - lastX) * (b.facing) > 5 * T; // (facing: the way the diver was heading when it came)
    if (b.act != BeastAct::Flee && (b.special > 40 || passed)) { b.act = BeastAct::Flee; b.actT = 0; }
    if (b.act == BeastAct::Flee) {
        b.pos.y -= 60 * dt; // back up into the dark
        if (b.actT > 2.5f) { b.life = BeastLife::Gone; W.webs.clear(); W.apexT = 0; W.calmT = 0; W.apexPos = {-1e9f, -1e9f}; }
        return;
    }
    b.act = BeastAct::Ambush;
    for (int j = 0; j < (int)W.beasts.size(); j++) { // anything that blunders into a web is held fast, and drained
        Beast& o = W.beasts[j];
        if (j == i || !Alive(o) || o.hidden || Flora(W, o) || Has(Sp(W.biome, o.species), T_GIANT)) continue;
        Rectangle ob{o.pos.x - 4, o.pos.y - 4, 8, 8};
        for (auto& w : W.webs) if (UnderWeb(w, ob)) { o.stunT = std::max(o.stunT, 5.0f); o.vel = {0, 0}; if (R(W) < dt * 0.3f) Kill(W, p, j, i); }
    }
    (void)dt;
}

// ---------------------------------------------------------------- flora
void CaveFlora(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    Rectangle box = PlatDiverBox(p);
    switch (b.species) {
    case CS_LSHROOM: W.lights.push_back({{b.pos.x, b.pos.y - 10}, 4.5f * T, 0.8f}); break;
    case CS_LICHEN: // acid: whatever big brushes it is eaten into (and the diver dies of it - see the touch)
        for (int j = 0; j < (int)W.beasts.size(); j++) { Beast& o = W.beasts[j]; if (Alive(o) && !o.hidden && !Flora(W, o) && o.mass >= 1 && Dist(o.pos, b.pos) < 18) Hurt(W, p, j, 0.5f * dt, b.species, false); }
        break;
    case CS_VSPORE: { // a drum of a fungus: struck, a blast of sound - deafening the stalker, and calling up the worm
        if (b.act == BeastAct::Drift) { if (b.actT > 20) b.act = BeastAct::Idle; break; }
        if (!dv.alive || !CheckCollisionCircleRec({b.pos.x, b.pos.y - 8}, 11, box) || (Len(dv.vel) < 150 && p.pose != 5)) break;
        b.act = BeastAct::Drift; b.actT = 0;
        W.sounds.push_back({b.pos, 2.0f, 0.6f, i});
        AddCloud(W, {b.pos.x, b.pos.y - 10}, 5 * T, 0.5f, 4);
        for (auto& o : W.beasts) {
            if (!Alive(o)) continue;
            if (o.species == CS_STALKER && Dist(o.pos, b.pos) < 12 * T) { o.special2 = 6; o.act = BeastAct::Wander; o.thinkT = 1; }
            if (o.species == CS_TREMOR && o.act == BeastAct::Idle) { o.special = 99; o.cooldown = 0; } // (it rises toward the diver, who is right here)
        }
        if (!p.verifying) PlatBurst(p, {b.pos.x, b.pos.y - 8}, 16, Color{200, 180, 230, 255}, 200, 0.6f, 2);
        break;
    }
    case CS_CABBAGE: // slide through the dense leaves and you come out clean: no trail, nothing that was following you knows
        if (dv.alive && p.pose == 1 && CheckCollisionRecs({b.pos.x - 18, b.pos.y - 16, 36, 16}, box)) {
            for (auto& o : W.beasts) { Forget(o, BEAST_DIVER, 0); if (o.target == BEAST_DIVER && (o.act == BeastAct::Hunt)) { o.act = BeastAct::Wander; o.thinkT = 1.5f; } }
            for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
                int cx = (int)floorf(dv.pos.x / W.scent.cell) + dx, cy = (int)floorf(dv.pos.y / W.scent.cell) + dy;
                if (cx >= 0 && cy >= 0 && cx < W.scent.w && cy < W.scent.h) W.scent.trail[cy * W.scent.w + cx] = 0;
            }
            b.flashT = 0.4f;
        }
        b.flashT -= dt;
        break;
    case CS_NROOT: // paralytic barbs: small things that touch it freeze - easy meat
        for (auto& o : W.beasts) if (Alive(o) && !o.hidden && !Flora(W, o) && o.mass < 1 && Dist(o.pos, {b.pos.x, b.pos.y - 6}) < 14) { o.stunT = std::max(o.stunT, 4.0f); o.vel = {0, 0}; }
        break;
    default: break;
    }
}

}  // namespace

void Cave13Hook(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    switch (b.species) {
    case CS_STALKER: StalkerHook(W, p, i, dt); break;
    case CS_TREMOR: TremorHook(W, p, i, dt); break;
    case CS_CTORTOISE: TortoiseHook(W, p, i, dt); break;
    case CS_ARACHNID: ArachnidHook(W, p, i, dt); break;
    default: if (Flora(W, b)) CaveFlora(W, p, i, dt); break;
    }
}

bool Cave13Touch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    if (b.life != BeastLife::Alive) return false;
    if (b.species == CS_TREMOR) return b.act == BeastAct::Strike && SegmentHits(b.anchor, b.territory, 16, diver);
    if (b.species == CS_LICHEN) return CheckCollisionCircleRec(b.pos, 10, diver);
    if (b.species == CS_ARACHNID && b.act != BeastAct::Flee) { for (const auto& w : W.webs) if (UnderWeb(w, diver)) return true; }
    return false;
}

// Rocks in flight, glow-shrimp splatter, webs revealed, and the director.
void Cave13Tick(BeastWorld& W, PlatformState& p, float dt) {
    Diver dv = SeeDiver(p);
    // the stalker's stones: they sink, clatter, and whatever moves near the clatter gives itself away
    for (auto& k : W.props) {
        if (k.kind != ROCK) continue;
        k.t += dt;
        k.vel.y = std::min(k.vel.y + 700 * dt, 260.0f);
        bool g;
        Collide(W.nav, k.pos, k.vel, 3, 3, dt, g);
        if (g || k.t > 3) {
            W.sounds.push_back({k.pos, 0.5f, 0.3f, k.owner});
            if (dv.alive && Dist(dv.pos, k.pos) < 3.5f * T && Len(dv.vel) > 60 && k.owner >= 0 && k.owner < (int)W.beasts.size() && W.beasts[k.owner].special2 <= 0)
                Remember(W.beasts[k.owner], MEM_PREY, BEAST_DIVER, 0, dv.pos, dv.vel, 1.0f, W.time); // the echo came back off something moving
            k.t = 99;
        }
    }
    W.props.erase(std::remove_if(W.props.begin(), W.props.end(), [](const BeastProp& k) { return k.kind == ROCK && k.t >= 99; }), W.props.end());
    // glow-shrimp splatter: rush through a swarm and your suit glows a while - and shows up the webs you pass
    W.glowSuitT = std::max(0.0f, W.glowSuitT - dt);
    bool rushing = dv.alive && (p.pose == 5 || p.pose == 1 || Len(dv.vel) > 300);
    if (rushing) for (const auto& o : W.beasts) if (Alive(o) && o.species == CS_GSHRIMP && Dist(o.pos, dv.pos) < 40) { W.glowSuitT = 15; break; }
    for (auto& w : W.webs) {
        w.t += dt;
        if (W.glowSuitT > 0 && dv.alive && fabsf(dv.pos.x - w.top.x) < 3.5f * T) w.revealed = true;
    }
    // the director: after a calm stretch, the arachnid strings the tunnels ahead
    bool apex = false, threatened = false;
    for (const auto& b : W.beasts) {
        if (!Alive(b)) continue;
        if (b.species == CS_ARACHNID) apex = true;
        if (b.target == BEAST_DIVER && (b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike) && dv.alive && Dist(b.pos, dv.pos) < 10 * T) threatened = true;
    }
    if (!apex) W.apexPos = {-1e9f, -1e9f};
    W.tension = Clamp01(W.tension + (threatened ? 0.3f : -0.05f) * dt);
    W.calmT = threatened ? 0 : W.calmT + dt;
    W.apexT += dt;
    if (apex || !dv.alive || getenv("DEPTH_NOAPEX")) return;
    float wait = W.apexVisits == 0 ? 60.0f : 100.0f;
    if (W.apexT < wait || W.calmT < 20 || W.tension > 0.2f || !p.onGround) return;
    float dir = dv.vel.x < -20 ? -1.0f : 1.0f;
    std::vector<BeastWorld::Web> webs;
    for (int d = 6; d <= 20 && webs.size() < 2; d++) { // tunnels ahead: a flat floor with a ceiling 2-5 tiles over it
        int cx = (int)floorf(dv.pos.x / T) + (int)dir * d;
        if (cx < 3 || cx >= W.nav.w - 3 || cx * T > W.limitX - 4 * T) break;
        int feet = (int)floorf((dv.pos.y + 14) / T), fy = W.nav.h; // a standing cell near the diver's own level (never the roof)
        for (int yy = feet - 3; yy <= feet + 3; yy++) if (W.nav.Standable(cx, yy)) { fy = yy + 1; break; }
        if (fy >= W.nav.h || W.nav.Hazard(cx, fy - 1)) continue;
        int c = fy - 1;
        while (c > 0 && W.nav.Open(cx, c - 1)) c--;
        int h = fy - c;
        if (h < 2 || h > 5) continue;
        bool flat = W.nav.Standable(cx - 1, fy - 1) && W.nav.Standable(cx + 1, fy - 1) && W.nav.Standable(cx - 2, fy - 1) && W.nav.Standable(cx + 2, fy - 1);
        if (!flat || (!webs.empty() && fabsf(webs.back().top.x - (cx * T + 16)) < 5 * T)) continue;
        BeastWorld::Web w; w.top = {cx * T + 16, c * T}; w.bottom = {cx * T + 16, fy * T - 20};
        for (const auto& o : W.beasts) if (Alive(o) && o.species == CS_LSHROOM && Dist(o.pos, w.bottom) < 4 * T) w.revealed = true; // lit
        webs.push_back(w);
        d += 4;
    }
    if (webs.empty()) return;
    W.webs = webs;
    int k = NewBeast(W, CS_ARACHNID, {webs[0].top.x + dir * T, webs[0].top.y + 10});
    Beast& a = W.beasts[k];
    a.facing = dir; a.act = BeastAct::Ambush; a.den = -1; a.special = 0; a.pers.abnormal = Abnormal::None;
    W.apexVisits++; W.apexT = 0;
    if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "director: the Abyssal Arachnid strings %d web(s) ahead of tile %d", (int)webs.size(), (int)(dv.pos.x / T));
}

// ---------------------------------------------------------------- the planner (at build time)
void Cave13Spawn(BeastWorld& W, PlatformState& p) {
    Spots S; S.Build(W, p, 12, W.nav.w - 8);
    if (S.floor.size() < 20) return;
    unsigned s = W.seed;
    auto plant = [&](int species, Vector2 at) { int k = NewBeast(W, species, at); W.beasts[k].den = -1; W.beasts[k].act = BeastAct::Idle; W.beasts[k].pers.abnormal = Abnormal::None; return k; };
    auto root = [&](Vector2 f) { return Vector2{f.x, (f.y + 1) * T}; };
    // lantern-shrooms at the junctions (steps, ledges, shaft feet), every dozen tiles or so
    float lastLight = -1e9f;
    for (size_t i = 1; i < S.floor.size(); i++)
        if (fabsf(S.floor[i].y - S.floor[i - 1].y) >= 2 && S.floor[i].x - lastLight > 10 * T) { plant(CS_LSHROOM, root(S.floor[i])); lastLight = S.floor[i].x; }
    // the rest along the floor
    for (size_t i = 0; i < S.floor.size(); i += 5) {
        float h = Hash(s, 9500 + (unsigned)i);
        Vector2 f = S.floor[i];
        if (h < 0.10f) plant(CS_VSPORE, root(f));
        else if (h < 0.20f) plant(CS_CABBAGE, root(f));
        else if (h < 0.28f) plant(CS_NROOT, root(f));
    }
    // acid-lichen on the walls of wide chambers (never a chimney you'd wall-jump), up off the floor
    int lichen = 0;
    for (int x = 14; x < W.nav.w - 10 && lichen < 6; x++) {
        if (x * T > W.limitX - 4 * T) break;
        if (Hash(s, 9600 + x) > 0.08f) continue;
        for (int y = 3; y < W.nav.h - 3; y++)
            for (int sd = -1; sd <= 1; sd += 2) {
                if (!W.nav.Solid(x, y) || !W.nav.Open(x + sd, y)) continue;
                bool wide = true;
                for (int k = 1; k <= 6 && wide; k++) wide = W.nav.Open(x + sd * k, y);
                int fl = W.nav.FloorBelow(x + sd, y);
                if (!wide || fl - y < 2 || fl - y > 4) continue;
                plant(CS_LICHEN, {sd > 0 ? (x + 1) * T + 2 : x * T - 2, y * T + 16.0f});
                lichen++; y = W.nav.h; break;
            }
    }
    // loaches in little schools; the tortoise on a long floor; a stalker or two; the tremor worm under the middle stretch
    for (int k = 0; k < 3; k++) { Vector2 c = S.Above(W, S.At(0.2f + 0.3f * k), 1); for (int j = 0; j < 4; j++) { int b = NewBeast(W, CS_LOACH, {c.x + j * 8.0f, c.y + (j % 2) * 6.0f}); W.beasts[b].school = 300 + k; } }
    for (size_t i = S.floor.size() / 3; i + 10 < S.floor.size(); i++) {
        bool flat = true;
        for (int k = 1; k < 10 && flat; k++) flat = S.floor[i + k].y == S.floor[i].y && S.floor[i + k].x - S.floor[i].x == k * T && W.nav.Open((int)(S.floor[i + k].x / T), (int)S.floor[i].y - 2);
        if (!flat) continue;
        Vector2 f = S.floor[i + 5];
        int b = NewBeast(W, CS_CTORTOISE, {f.x, (f.y + 1) * T - 52}); W.beasts[b].anchor = root(f); W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None;
        break;
    }
    for (int k = 0; k < 2; k++) { Vector2 f = S.At(0.35f + 0.35f * k); int b = NewBeast(W, CS_STALKER, S.Stand(f, 12)); W.beasts[b].pers.abnormal = Abnormal::None; W.beasts[b].cooldown = 3; }
    { int b = NewBeast(W, CS_TREMOR, root(S.At(0.5f))); W.beasts[b].anchor = root(S.At(0.5f)); W.beasts[b].den = -1; W.beasts[b].pers.abnormal = Abnormal::None; }
}

// ---------------------------------------------------------------- depth.exe --verify-cave-ecosystem (the 1.3 part)
bool VerifyCave13() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-cave13: FAILED - %s", what); ok = false; };
    auto bench = [](PlatformState& q, int w, int h) { // a flooded tunnel: floor rows h-2..h-1
        q = PlatformState{};
        q.level = PL_CAVE; q.w = w; q.h = h;
        q.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { q.tiles[h - 1][x] = '#'; q.tiles[h - 2][x] = '#'; q.tiles[0][x] = '#'; }
        for (int y = 0; y < h; y++) { q.tiles[y][0] = '#'; q.tiles[y][w - 1] = '#'; }
        q.pos = {-5000, -5000}; q.deathTimer = 1;
    };
    auto build = [](PlatformState& q) {
        BeastWorld& Q = q.fauna;
        Q = BeastWorld{}; Q.biome = PL_CAVE; Q.active = true; Q.seed = 91; Q.rng = 777;
        Q.nav.w = q.w; Q.nav.h = q.h; Q.nav.solid.assign((size_t)q.w * q.h, 0); Q.nav.hazard.assign((size_t)q.w * q.h, 0);
        for (int y = 0; y < q.h; y++) for (int x = 0; x < q.w; x++) Q.nav.solid[y * q.w + x] = PlatSolid(q, x, y) ? 1 : 0;
        Q.scent.Init(q.w, q.h, T); Q.apexT = -1e9f;
    };
    auto diverAt = [](PlatformState& q, Vector2 c, Vector2 v) { q.deathTimer = 0; Rectangle r = PlatDiverBox(q); q.pos.x += c.x - (r.x + r.width / 2); q.pos.y += c.y - (r.y + r.height / 2); q.vel = v; };
    auto calm = [](Beast& b) { b.pers.abnormal = Abnormal::None; b.den = -1; };
    const float dt = 1 / 60.0f;
    PlatformState q;
    for (unsigned seed : {505u, 12u, 777u}) { // what the planner puts in a real flooded cave
        PlatformState r; r.level = PL_CAVE; r.layout = {(int)seed, 100};
        PlatBuildLevel(r);
        int n[CS_COUNT] = {0};
        for (const auto& b : r.fauna.beasts) if (b.life == BeastLife::Alive) n[b.species]++;
        TraceLog(LOG_WARNING, "verify-cave13: cave #%u has %d stalkers, %d tremor worm, %d tortoise, %d loaches; %d shrooms, %d lichen, %d spores, %d cabbage, %d nerve-root", seed, n[CS_STALKER], n[CS_TREMOR], n[CS_CTORTOISE], n[CS_LOACH], n[CS_LSHROOM], n[CS_LICHEN], n[CS_VSPORE], n[CS_CABBAGE], n[CS_NROOT]);
        if (n[CS_STALKER] < 1 || n[CS_TREMOR] < 1 || n[CS_LSHROOM] < 1) fail("a real cave is missing its stalker, tremor worm or lantern-shrooms");
    }
    // 1) the Echo-Stalker: a silent diver close by goes unnoticed; a noise brings it; deafened, it ignores the noise
    for (int noisy = 0; noisy < 2; noisy++) {
        bench(q, 60, 16); build(q);
        int st = NewBeast(q.fauna, CS_STALKER, {20 * T, 14 * T - 8}); calm(q.fauna.beasts[st]); q.fauna.beasts[st].cooldown = 99; q.fauna.beasts[st].hunger = 1;
        bool hunted = false;
        for (int f = 0; f < 60 * 3; f++) {
            diverAt(q, {26 * T, 14 * T - 13}, {0, 0}); q.onGround = true;
            if (noisy && f % 30 == 0) BeastsNoise(q, {26 * T, 14 * T}, 0.6f);
            BeastsUpdate(q, dt);
            if (q.fauna.beasts[st].target == BEAST_DIVER && q.fauna.beasts[st].act == BeastAct::Hunt) hunted = true;
        }
        if (noisy && !hunted) fail("a noisy diver didn't draw the Echo-Stalker");
        if (!noisy && hunted) fail("the Echo-Stalker hunted a diver who made no sound");
    }
    // 2) its stones: with nothing to go on it throws one, and a diver moving near where it lands is found
    {
        bench(q, 60, 16); build(q);
        int st = NewBeast(q.fauna, CS_STALKER, {20 * T, 14 * T - 8}); calm(q.fauna.beasts[st]); q.fauna.beasts[st].cooldown = 0; q.fauna.beasts[st].facing = 1;
        bool threw = false, found = false;
        for (int f = 0; f < 60 * 4; f++) {
            diverAt(q, {24 * T, 14 * T - 13}, {80, 0}); q.onGround = true;
            BeastsUpdate(q, dt);
            if (!q.fauna.props.empty()) threw = true;
            for (const auto& m : q.fauna.beasts[st].mem) if (m.source == BEAST_DIVER && m.kind == MEM_PREY && m.strength > 0.5f) found = true;
        }
        if (!threw) fail("the Echo-Stalker never threw a stone to test the dark");
        if (!found) fail("a diver moving where the stone landed wasn't found by its echo");
    }
    // 3) the Tremor Worm: a sprint across its ground and the floor ahead shudders (the loaches bolt), then it bursts up
    {
        bench(q, 80, 16); build(q);
        int tw = NewBeast(q.fauna, CS_TREMOR, {40 * T, 14 * T}); calm(q.fauna.beasts[tw]); q.fauna.beasts[tw].anchor = {40 * T, 14 * T};
        int lo = NewBeast(q.fauna, CS_LOACH, {32 * T, 12 * T}); calm(q.fauna.beasts[lo]); q.fauna.beasts[lo].school = -1;
        bool coiled = false, panicked = false, hit = false;
        for (int f = 0; f < 60 * 5; f++) {
            diverAt(q, {30 * T, 14 * T - 13}, {360, 0}); q.onGround = true;
            BeastsUpdate(q, dt);
            const Beast& w = q.fauna.beasts[tw];
            if (w.act == BeastAct::Coil) coiled = true;
            if (coiled && q.fauna.beasts[lo].act == BeastAct::Flee) panicked = true;
            if (w.act == BeastAct::Strike) { diverAt(q, {w.anchor.x, 14 * T - 13}, {0, 0}); if (BeastsTouchDiver(q, PlatDiverBox(q))) hit = true; }
        }
        if (!coiled) fail("sprinting across the Tremor Worm's ground didn't bring it up");
        if (!panicked) fail("the loaches didn't bolt from the shuddering floor");
        if (!hit) fail("the Tremor Worm's burst didn't take a diver standing on the breach");
    }
    // 4) a vibration-spore: struck, it deafens the stalker and calls the worm
    {
        bench(q, 60, 16); build(q);
        int st = NewBeast(q.fauna, CS_STALKER, {20 * T, 14 * T - 8}); calm(q.fauna.beasts[st]);
        int tw = NewBeast(q.fauna, CS_TREMOR, {40 * T, 14 * T}); calm(q.fauna.beasts[tw]); q.fauna.beasts[tw].anchor = {40 * T, 14 * T};
        int sp = NewBeast(q.fauna, CS_VSPORE, {26 * T, 14 * T}); calm(q.fauna.beasts[sp]);
        diverAt(q, {26 * T, 14 * T - 13}, {300, 0}); q.onGround = true;
        BeastsUpdate(q, dt); BeastsUpdate(q, dt);
        if (q.fauna.beasts[st].special2 <= 0) fail("a vibration-spore's blast didn't deafen the Echo-Stalker");
        if (q.fauna.beasts[tw].act != BeastAct::Coil) fail("a vibration-spore's blast didn't call up the Tremor Worm");
    }
    // 5) cave-cabbage: slide through it and the stalker loses you; 6) nerve-root pins a loach; 7) acid-lichen kills on touch
    {
        bench(q, 60, 16); build(q);
        int st = NewBeast(q.fauna, CS_STALKER, {20 * T, 14 * T - 8}); calm(q.fauna.beasts[st]);
        Remember(q.fauna.beasts[st], MEM_PREY, BEAST_DIVER, 0, {30 * T, 13 * T}, {0, 0}, 1.0f, 0);
        int cb = NewBeast(q.fauna, CS_CABBAGE, {30 * T, 14 * T}); calm(q.fauna.beasts[cb]);
        diverAt(q, {30 * T, 14 * T - 8}, {300, 0}); q.pose = 1; q.onGround = true;
        BeastsUpdate(q, dt); q.pose = 0;
        bool still = false; for (const auto& m : q.fauna.beasts[st].mem) if (m.source == BEAST_DIVER && m.strength > 0) still = true;
        if (still) fail("sliding through cave-cabbage didn't shake off the Echo-Stalker");
        int nr = NewBeast(q.fauna, CS_NROOT, {40 * T, 14 * T}); calm(q.fauna.beasts[nr]);
        int lo = NewBeast(q.fauna, CS_LOACH, {40 * T, 14 * T - 6}); calm(q.fauna.beasts[lo]); q.fauna.beasts[lo].school = -1;
        q.deathTimer = 1; q.pos = {-5000, -5000}; BeastsUpdate(q, dt);
        if (q.fauna.beasts[lo].stunT <= 0) fail("nerve-root didn't pin a loach");
        int li = NewBeast(q.fauna, CS_LICHEN, {50 * T, 12 * T}); calm(q.fauna.beasts[li]);
        diverAt(q, {50 * T, 12 * T}, {0, 0});
        if (!BeastsTouchDiver(q, PlatDiverBox(q))) fail("acid-lichen didn't kill on contact");
    }
    // 8) the tortoise: it walks, carries a mover, and turns a dropping leech aside
    {
        bench(q, 60, 16); build(q);
        int tt = NewBeast(q.fauna, CS_CTORTOISE, {20 * T, 14 * T - 52}); calm(q.fauna.beasts[tt]); q.fauna.beasts[tt].anchor = {20 * T, 14 * T}; q.fauna.beasts[tt].facing = 1;
        int le = NewBeast(q.fauna, CS_LEECH, {20 * T, 14 * T - 60}); calm(q.fauna.beasts[le]); q.fauna.beasts[le].special = 1; q.fauna.beasts[le].vel = {0, 200}; q.fauna.beasts[le].territory = {20 * T, 2 * T};
        BeastsUpdate(q, dt);
        if (q.fauna.beasts[le].special != 2) fail("the crystal shell didn't turn a dropping leech aside");
        float x0 = q.fauna.beasts[tt].pos.x;
        for (int f = 0; f < 60 * 6; f++) BeastsUpdate(q, dt);
        if (q.movers.empty()) fail("the crystal tortoise's shell isn't a mover");
        if (fabsf(q.fauna.beasts[tt].pos.x - x0) < 10) fail("the crystal tortoise never moved");
    }
    // 9) the arachnid: the director strings a web across a tunnel ahead; standing it catches you, sliding you pass under;
    //    glow-shrimp on your suit shows it; it leaves once you're past
    {
        bench(q, 80, 16);
        for (int x = 36; x < 60; x++) q.tiles[10][x] = '#'; // a low tunnel roof: four tiles of headroom
        build(q);
        diverAt(q, {26 * T, 14 * T - 13}, {200, 0}); q.onGround = true;
        q.fauna.apexT = 999; q.fauna.calmT = 999;
        BeastsUpdate(q, dt);
        if (q.fauna.webs.empty()) { fail("the director never let the arachnid string a web"); }
        else {
            const auto w = q.fauna.webs[0];
            diverAt(q, {w.top.x, 14 * T - 13}, {0, 0});
            bool standing = BeastsTouchDiver(q, PlatDiverBox(q));
            q.pose = 1; // sliding: the short hitbox
            Rectangle low = PlatDiverBox(q);
            bool sliding = false;
            for (const auto& b : q.fauna.beasts) if (Alive(b) && b.species == CS_ARACHNID && Cave13Touch(q.fauna, b, low)) sliding = true;
            q.pose = 0;
            if (!standing) fail("a standing diver walked through the arachnid's web");
            if (sliding) fail("a sliding diver couldn't pass under the arachnid's web");
            int gs = NewBeast(q.fauna, CS_GSHRIMP, {w.top.x - 2 * T, 14 * T - 12}); calm(q.fauna.beasts[gs]);
            diverAt(q, {w.top.x - 2 * T, 14 * T - 13}, {360, 0});
            BeastsUpdate(q, dt);
            if (q.fauna.glowSuitT <= 0 || !q.fauna.webs[0].revealed) fail("glow-shrimp splatter didn't show up the web");
            bool left = false;
            for (int f = 0; f < 60 * 6 && !left; f++) { diverAt(q, {q.fauna.webs.empty() ? 70 * T : q.fauna.webs.back().top.x + 7 * T, 14 * T - 13}, {0, 0}); BeastsUpdate(q, dt); left = q.fauna.webs.empty(); }
            if (!left) fail("the arachnid didn't leave (and take its webs) once the diver was past");
        }
    }
    if (ok) TraceLog(LOG_WARNING, "verify-cave13: OK - the Echo-Stalker (noise, stones, deafened), the Tremor Worm and the loaches' warning, spores, cabbage, nerve-root, lichen, the crystal tortoise, and the arachnid's webs");
    return ok;
}

}  // namespace bk
