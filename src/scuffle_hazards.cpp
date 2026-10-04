// Scuffle stage 5: the other five worlds' pieces at runtime (doc pp. 9-11). Headless. Each piece has a tell before it
// hurts (a glow, a shadow, dust: the scene draws `prog`), and anything that kills names itself as the cause.
//   The Cave: stalactites (fall when shot), acid drips, currents (the slipstream; the Reef's surge on a count), the toad's
//   tongue from its pool, the Lobster's claw (a sweep on a timer).
//   The Reef: reacher coral (holds you), eel holes (bite what's in front), sharks in the water below, the kraken (the
//   drop-off's finale: tentacles slam up).
//   Atlantis: grates (the Wyrm strikes up), the tuna lane (a ram), the sluice (floods a terrace), columns (topple when shot).
//   The Void: the leviathan's lure (it pulls), low gravity pockets, the sand worm's line.
//   The Salon: the crowd (bottles), the dartboard, the pool table (balls), the bouncer (anyone on the bar is thrown out),
//   the dog.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace sf {

static Rectangle Box(const Piece& p) { return {p.x * TILE, p.y * TILE, p.w * TILE, p.h * TILE}; }
static bool Overlap(const Stick& k, float x0, float y0, float x1, float y1) { return k.pos.x + k.halfW > x0 && k.pos.x - k.halfW < x1 && k.pos.y + k.height > y0 && k.pos.y < y1; }
static bool PelvisIn(const Stick& k, Rectangle r) { Vector2 c = k.pt[J_PELVIS].p; return c.x >= r.x && c.x < r.x + r.width && c.y >= r.y && c.y < r.y + r.height; }
static int Blame(const World& w, const Stick& k) { return k.lastHitBy >= 0 && w.t - k.lastHitT < 4 ? k.lastHitBy : -1; }
static float Cycle(const World& w, const Piece& p) { return fmodf(std::max(0.0f, w.t - p.start) + p.phase * p.period, std::max(0.2f, p.period)); }
// once per cycle, on its first step (the cycle's number is kept in `hold` for these pieces)
static bool NewCycle(const World& w, Piece& p) { int c = (int)floorf((std::max(0.0f, w.t - p.start) + p.phase * p.period) / std::max(0.2f, p.period)); if (c == p.hold) return false; p.hold = c; return true; }

void World::Nudge(Stick& k, Vector2 d) {
    if (!k.alive) { for (auto& a : k.pt) a.q = Vector2Subtract(a.q, Vector2Scale(d, 0.6f)); return; }
    if (k.st == S_RAGDOLL) { for (auto& a : k.pt) a.q = Vector2Subtract(a.q, d); return; }
    Vector2 np{k.pos.x + d.x, k.pos.y + d.y};
    if (!BoxHits(np.x - k.halfW, np.y, np.x + k.halfW, np.y + k.height)) { k.pos = np; for (auto& a : k.pt) { a.p = Vector2Add(a.p, d); a.q = Vector2Add(a.q, d); } }
    else if (!BoxHits(np.x - k.halfW, k.pos.y, np.x + k.halfW, k.pos.y + k.height)) k.pos.x = np.x;
}
int World::HazardBullet(int kind, Vector2 at, Vector2 v, float dmg, float knock, float grav) {
    Bullet b; b.p = at; b.v = v; b.owner = -1; b.weapon = -1; b.hazard = kind; b.dmg = dmg; b.knock = knock; b.grav = grav; b.life = 4; b.pierce = 1;
    bullets.push_back(b);
    return (int)bullets.size() - 1;
}
bool World::ShotAt(Vector2 at, float radius) {
    bool any = false;
    // stalactites (a shot anywhere on one brings it down) and columns (a shot topples them)
    for (auto& p : stage.pieces) {
        if ((p.kind != PK_STALACTITE && p.kind != PK_COLUMN) || p.prog > 0 || p.broken || t < p.start) continue;
        Rectangle r = Box(p);
        float cx = std::clamp(at.x, r.x, r.x + r.width), cy = std::clamp(at.y, r.y, r.y + r.height);
        if (Vector2Distance(at, {cx, cy}) <= radius + 0.05f) { p.prog = STEP; any = true; Emit(EV_HIT, at, -1, -1, 0); }
    }
    // crystal shatters into shrapnel
    int x0 = (int)floorf((at.x - radius) / TILE), x1 = (int)floorf((at.x + radius) / TILE), y0 = (int)floorf((at.y - radius) / TILE), y1 = (int)floorf((at.y + radius) / TILE);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (stage.At(x, y) != T_CRYSTAL) continue;
        Vector2 c{(x + 0.5f) * TILE, (y + 0.5f) * TILE};
        if (Vector2Distance(c, at) > radius + TILE * 0.75f) continue;
        stage.Set(x, y, T_EMPTY); any = true;
        Emit(EV_EXPLODE, c, -1, -1, 0.4f);
        for (int n = 0; n < 6; n++) { float a = n * (2 * PI / 6) + Rand() * 0.6f; int b = HazardBullet(-2, c, {cosf(a) * 15, sinf(a) * 15}, 12, 4, 0); bullets[b].life = 0.45f; bullets[b].age = 0.3f; }
    }
    return any;
}

void World::StepHazard(Piece& p) {
    Rectangle r = Box(p);
    float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
    switch (p.kind) {
    case PK_STALACTITE: {
        if (p.broken) return;
        if (p.prog <= 0 && p.phase > 0 && t >= p.phase) p.prog = STEP;
        if (p.prog <= 0) return;
        p.prog += STEP;
        float fall = 0.5f * gravity * p.prog * p.prog;
        p.off = {0, -fall};
        float fy0 = y0 - fall, fy1 = y1 - fall;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, x0 + 0.05f, fy0, x1 - 0.05f, fy1)) Hit(k, Blame(*this, k), 70, {0, -1}, 6, true, "a stalactite");
        int tx = (int)floorf((x0 + x1) * 0.5f / TILE), ty = (int)floorf((fy0 - 0.02f) / TILE);
        if (fy0 < -2 || (stage.Solid(tx, ty) && fy0 < y0 - 0.3f)) { p.broken = true; Emit(EV_EXPLODE, {(x0 + x1) * 0.5f, std::max(0.0f, fy0)}, -1, -1, 0.5f); }
        return;
    }
    case PK_DRIP: {
        if (NewCycle(*this, p)) HazardBullet(PK_DRIP, {(x0 + x1) * 0.5f, y0 - 0.05f}, {0, -1}, p.power, 2, gravity * 0.8f);
        return;
    }
    case PK_STREAM: {
        bool live = p.period <= 0 || Cycle(*this, p) < p.on;
        p.prog = live ? 1 : 0;
        if (!live) return;
        Vector2 d = Vector2Scale(Vector2Normalize({(float)p.dx, (float)p.dy}), p.power * STEP);
        for (auto& k : sticks) if (k.present && PelvisIn(k, r)) Nudge(k, d);
        for (auto& it : items) if (it.alive && it.holder < 0 && it.a.p.x >= x0 && it.a.p.x < x1 && it.a.p.y >= y0 && it.a.p.y < y1) { it.a.q = Vector2Subtract(it.a.q, Vector2Scale(d, 0.5f)); it.b.q = Vector2Subtract(it.b.q, Vector2Scale(d, 0.5f)); }
        return;
    }
    case PK_TOAD: {
        // the tongue lashes out of the pool along dx and back (0.6 s); whatever it touches goes in
        if (p.prog <= 0 && p.cool <= 0) { p.prog = STEP; p.cool = p.period; }
        if (p.prog <= 0) return;
        p.prog += STEP;
        if (p.prog > 0.6f) { p.prog = 0; return; }
        Vector2 o{p.dx > 0 ? x1 : x0, y1 + 0.35f};
        float reach = p.travel * TILE * sinf(PI * p.prog / 0.6f);
        float a = std::min(o.x, o.x + p.dx * reach), b = std::max(o.x, o.x + p.dx * reach);
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, a, o.y - 0.25f, b, o.y + 0.5f)) {
            Kill(k, Blame(*this, k), "the toad");
            Vector2 pull = Vector2Normalize(Vector2Subtract({(x0 + x1) * 0.5f, y0}, k.pt[J_PELVIS].p));
            for (auto& q : k.pt) q.q = Vector2Subtract(q.q, Vector2Scale(pull, 9 * STEP));
            p.prog = std::max(p.prog, 0.3f);   // (it reels in)
        }
        return;
    }
    case PK_CLAW: {
        // a tell (`on` s), then the claw sweeps its rectangle along dx in 0.8 s
        float u = Cycle(*this, p);
        p.prog = u < p.on ? u / p.on : u < p.on + 0.8f ? 1 + (u - p.on) / 0.8f : 0;
        if (p.prog < 1) return;
        float s = p.prog - 1, cx = p.dx >= 0 ? x0 + (x1 - x0) * s : x1 - (x1 - x0) * s;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, cx - 0.7f, y0, cx + 0.7f, y1)) Hit(k, Blame(*this, k), 100, Vector2Normalize({(float)(p.dx ? p.dx : 1), 0.6f}), 18, true, "the Lobster's claw");
        return;
    }
    case PK_REACHER: {
        // a touch: held fast for `on` s (5 damage), then let go, and it won't take the same stick again for 1.5 s
        if (p.hold >= 0) {
            if (p.hold >= (int)sticks.size() || !sticks[p.hold].alive) { p.hold = -1; return; }
            Stick& k = sticks[p.hold];
            p.prog -= STEP;
            k.pos = p.prevOff; k.vel = {0, 0}; k.knockT = std::max(k.knockT, 0.05f);
            if (p.prog <= 0) { k.hazT = 1.5f; p.hold = -1; }
            return;
        }
        for (auto& k : sticks) if (k.present && k.alive && k.st != S_RAGDOLL && k.hazT <= 0 && Overlap(k, x0 - 0.08f, y0 - 0.08f, x1 + 0.08f, y1 + 0.08f)) {
            p.hold = k.id; p.prog = p.on; p.prevOff = k.pos; Hit(k, Blame(*this, k), 5, {0, 0.1f}, 0, false, "reacher coral");
            Emit(EV_GRAB, k.pt[J_PELVIS].p, k.id, -1, 0);
            break;
        }
        return;
    }
    case PK_EEL: {
        // something in front of the hole: a 0.35 s tell, then a bite
        float mx = p.dx >= 0 ? x1 : x0, fx0 = std::min(mx, mx + p.dx * p.travel * TILE), fx1 = std::max(mx, mx + p.dx * p.travel * TILE);
        if (p.prog <= 0) {
            if (p.cool > 0) return;
            for (const auto& k : sticks) if (k.present && k.alive && Overlap(k, fx0, y0 - 0.3f, fx1, y1 + 0.3f)) { p.prog = STEP; break; }
            return;
        }
        p.prog += STEP;
        if (p.prog < 0.35f) return;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, fx0, y0 - 0.3f, fx1, y1 + 0.3f)) Hit(k, Blame(*this, k), p.power, Vector2Normalize({(float)(p.dx ? p.dx : 1), 0.4f}), 8, true, "an eel");
        Emit(EV_PUNCH, {mx + p.dx * 0.6f, (y0 + y1) * 0.5f}, -1, -1, 0);
        p.prog = 0; p.cool = p.period;
        return;
    }
    case PK_SHARK: {
        // the water below: a stick in it `on` seconds is taken
        for (auto& k : sticks) {
            if (!k.present || !k.alive) continue;
            if (PelvisIn(k, r)) { k.sharkT += STEP; if (k.sharkT > p.on) { Kill(k, Blame(*this, k), "a shark"); Emit(EV_EXPLODE, k.pt[J_PELVIS].p, -1, -1, 0.3f); } }
            else k.sharkT = std::max(0.0f, k.sharkT - STEP);
        }
        return;
    }
    case PK_KRAKEN: {
        // the drop-off's finale: every period a tentacle's shadow grows under a stick (`on` s), then it slams up the full height
        float u = Cycle(*this, p);
        if (NewCycle(*this, p)) {   // (pick the target: a living stick over the rectangle, else anywhere along it)
            float tx = x0 + Rand() * (x1 - x0);
            for (int tries = 0; tries < 8; tries++) { const Stick& k = sticks[(int)(Rand() * sticks.size()) % sticks.size()]; if (k.alive && k.present && k.pos.x > x0 && k.pos.x < x1) { tx = k.pos.x + (Rand() - 0.5f) * 1.2f; break; } }
            p.prevOff.x = std::clamp(tx, x0 + 0.6f, x1 - 0.6f);
        }
        p.prog = u < p.on ? u / p.on : u < p.on + 0.7f ? 1 + (u - p.on) / 0.7f : 0;
        if (p.prog < 1) return;
        float cx = p.prevOff.x;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, cx - 0.6f, -3, cx + 0.6f, y1 + 2)) Hit(k, Blame(*this, k), 100, {0, 1}, 16, true, "the kraken");
        return;
    }
    case PK_GRATE: {
        // the Wyrm: a tell, then it strikes up out of the grate `travel` tiles for 0.6 s
        float u = Cycle(*this, p);
        p.prog = u < p.on ? u / p.on : u < p.on + 0.6f ? 1 + (u - p.on) / 0.6f : 0;
        if (p.prog < 1) return;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, x0 - 0.1f, y1, x1 + 0.1f, y1 + p.travel * TILE)) Hit(k, Blame(*this, k), 100, {0, 1}, 14, true, "the Wyrm");
        return;
    }
    case PK_TUNA: {
        // every period a tuna rams through the lane at 22 m/s (the lane's rows)
        float u = Cycle(*this, p), len = x1 - x0 + 4, run = len / 22;
        p.prog = u < run ? u / run : 0;
        if (p.prog <= 0) return;
        float hx = p.dx >= 0 ? x0 - 2 + len * p.prog : x1 + 2 - len * p.prog;
        for (auto& k : sticks) if (k.present && k.alive && k.hazT <= 0 && Overlap(k, hx - 0.8f, y0, hx + 0.8f, y1)) { k.hazT = 1; Hit(k, Blame(*this, k), 25, Vector2Normalize({(float)(p.dx ? p.dx : 1), 0.35f}), p.power, true, "a tuna"); }
        return;
    }
    case PK_SLUICE: { p.prog = Cycle(*this, p) < p.on ? 1 : 0; return; }   // (InLiquid reads it)
    case PK_COLUMN: {
        // standing until shot (or at `phase` seconds), then it topples along dx over a second, crushing what's under its arc,
        // and lies as rubble on the floor
        if (p.broken) return;
        if (p.prog <= 0 && p.phase > 0 && t >= p.phase) p.prog = STEP;
        if (p.prog <= 0) return;
        if (p.prog <= STEP * 1.5f) for (int y = p.y; y < p.y + p.h; y++) for (int x = p.x; x < p.x + p.w; x++) stage.Set(x, y, T_EMPTY);   // (it lifts off its tiles)
        p.prog += STEP;
        float u = std::min(1.0f, p.prog), a = PI / 2 * (1 - u * u);
        Vector2 piv{p.dx >= 0 ? x1 : x0, y0}, dir{(p.dx >= 0 ? 1.0f : -1.0f) * cosf(a), sinf(a)};
        float L = r.height;
        for (auto& k : sticks) {
            if (!k.present || !k.alive || k.hazT > 0) continue;
            for (int j : {J_HEAD, J_NECK, J_PELVIS}) {
                Vector2 d = Vector2Subtract(k.pt[j].p, piv); float along = Vector2DotProduct(d, dir);
                if (along < 0 || along > L) continue;
                if (fabsf(d.x * dir.y - d.y * dir.x) < r.width * 0.5f + 0.25f) { k.hazT = 1; Hit(k, Blame(*this, k), 80, {dir.y * (p.dx >= 0 ? 1.0f : -1.0f), -dir.x * 0.3f - 0.4f}, 10, true, "a falling column"); break; }
            }
        }
        if (u >= 1) {
            p.broken = true; Emit(EV_EXPLODE, Vector2Add(piv, Vector2Scale(dir, L * 0.6f)), -1, -1, 0.6f);
            int len = (int)lroundf(L / TILE), sx = p.dx >= 0 ? p.x + p.w : p.x - 1;   // (rubble: a low wall along the floor where it fell)
            for (int i = 0; i < len; i++) { int x = sx + (p.dx >= 0 ? i : -i); if (!stage.Solid(x, p.y)) stage.Set(x, p.y, T_STONE); }
        }
        return;
    }
    case PK_LURE: {
        // a light that pulls: within `travel` tiles, toward the lure, stronger near it
        Vector2 c{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}; float R = p.travel * TILE;
        p.prog = 1;
        for (auto& k : sticks) {
            if (!k.present) continue;
            Vector2 d = Vector2Subtract(c, k.pt[J_PELVIS].p); float L = Vector2Length(d);
            if (L > R || L < 0.05f) continue;
            Nudge(k, Vector2Scale(d, p.power * (1 - L / R) * STEP / L));
        }
        return;
    }
    case PK_LOWG: return;   // (GravityAt reads it)
    case PK_WORM: {
        // the sand worm: dust rises along its line under a stick (`on` s), then it bursts up two tiles wide
        float u = Cycle(*this, p);
        if (NewCycle(*this, p)) {
            float tx = x0 + Rand() * (x1 - x0);
            for (int tries = 0; tries < 8; tries++) { const Stick& k = sticks[(int)(Rand() * sticks.size()) % sticks.size()]; if (k.alive && k.present && k.pos.x > x0 && k.pos.x < x1 && k.pos.y < y1 + 1.5f) { tx = k.pos.x; break; } }
            p.prevOff.x = std::clamp(tx, x0 + TILE, x1 - TILE);
        }
        p.prog = u < p.on ? u / p.on : u < p.on + 0.5f ? 1 + (u - p.on) / 0.5f : 0;
        if (p.prog < 1) return;
        float cx = p.prevOff.x;
        for (auto& k : sticks) if (k.present && k.alive && Overlap(k, cx - TILE, y0 - 0.2f, cx + TILE, y1 + 2 * TILE)) Hit(k, Blame(*this, k), 100, {0, 1}, 15, true, "the sand worm");
        return;
    }
    case PK_CROWD: {
        // a bottle, lobbed at someone (the crowd's aim isn't good)
        if (!NewCycle(*this, p)) return;
        std::vector<int> live; for (const auto& k : sticks) if (k.alive && k.present) live.push_back(k.id);
        if (live.empty()) return;
        const Stick& k = sticks[live[(int)(Rand() * live.size()) % live.size()]];
        Vector2 from{x0 + Rand() * (x1 - x0), (y0 + y1) * 0.5f}, to = Vector2Add(k.pt[J_NECK].p, {(Rand() - 0.5f) * 1.6f, 0});
        float g = gravity * 0.6f, T = std::max(0.5f, Vector2Distance(from, to) / 13);
        HazardBullet(PK_CROWD, from, {(to.x - from.x) / T, (to.y - from.y) / T + 0.5f * g * T}, p.power, 6, g);
        p.prog = 1;
        return;
    }
    case PK_DART: {
        float u = Cycle(*this, p);
        p.prog = u < 0.4f ? 1 - u / 0.4f : 0;   // (the board shudders as a dart leaves)
        if (NewCycle(*this, p)) HazardBullet(PK_DART, {p.dx >= 0 ? x1 + 0.05f : x0 - 0.05f, (y0 + y1) * 0.5f}, {p.dx >= 0 ? 26.0f : -26.0f, 0}, p.power, 3, 0);
        return;
    }
    case PK_POOL: {
        // a ball along the table's top
        if (!NewCycle(*this, p)) return;
        int b = HazardBullet(PK_POOL, {p.dx >= 0 ? x0 + 0.1f : x1 - 0.1f, y1 + 0.42f}, {p.dx >= 0 ? 11.0f : -11.0f, 0}, p.power, 10, 0);
        bullets[b].pierce = 3; bullets[b].life = (x1 - x0 + 3) / 11;
        return;
    }
    case PK_BOUNCER: {
        // anyone standing on the bar this long is thrown out, toward the nearer edge and up
        int who = -1;
        for (const auto& k : sticks) if (k.present && k.alive && k.grounded && stage.At((int)floorf(k.pos.x / TILE), (int)floorf((k.pos.y - 0.05f) / TILE)) == T_BAR) { who = k.id; break; }
        if (who < 0 || who != p.hold) { p.hold = who; p.prog = 0; return; }
        p.prog += STEP;
        if (p.prog < p.on) return;
        Stick& k = sticks[who];
        float side = k.pos.x < stage.Width() / 2 ? -1.0f : 1.0f;
        Hit(k, Blame(*this, k), 20, Vector2Normalize({side, 0.55f}), 24, true, "the bouncer");
        k.thrownT = 1.0f; k.thrownBy = -1;
        Emit(EV_THROW, k.pt[J_NECK].p, k.id, -1, 1);
        p.hold = -1; p.prog = 0;
        return;
    }
    case PK_DOG: {
        // the dog runs its stretch (off.x: where it is, along x; dy: which way) and goes for the nearest stick on it
        if (p.prevOff.x < x0 || p.prevOff.x > x1) { p.prevOff.x = (x0 + x1) * 0.5f; p.dy = 1; }
        float target = -1; float best = 6;
        for (const auto& k : sticks) if (k.present && k.alive && k.pos.x > x0 - 0.5f && k.pos.x < x1 + 0.5f && fabsf(k.pos.y - y0) < 1.5f && fabsf(k.pos.x - p.prevOff.x) < best) { best = fabsf(k.pos.x - p.prevOff.x); target = k.pos.x; }
        if (target >= 0) p.dy = target > p.prevOff.x ? 1 : -1;
        p.prevOff.x += p.dy * (target >= 0 ? 7.0f : 3.5f) * STEP;
        if (p.prevOff.x < x0 + 0.4f) { p.prevOff.x = x0 + 0.4f; p.dy = 1; }
        if (p.prevOff.x > x1 - 0.4f) { p.prevOff.x = x1 - 0.4f; p.dy = -1; }
        p.prog += STEP;   // (its gait)
        for (auto& k : sticks) if (k.present && k.alive && k.hazT <= 0 && Overlap(k, p.prevOff.x - 0.45f, y0, p.prevOff.x + 0.45f, y0 + 0.6f)) { k.hazT = p.period; Hit(k, Blame(*this, k), p.power, Vector2Normalize({(float)p.dy, 0.5f}), 6, false, "the dog"); Emit(EV_PUNCH, k.pt[J_PELVIS].p, -1, -1, 0); }
        return;
    }
    default: return;
    }
}

} // namespace sf
