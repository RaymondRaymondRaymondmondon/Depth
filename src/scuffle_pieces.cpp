// Scuffle stage 3: the moving and hazardous pieces at runtime (doc pp. 9-11). Headless. Pistons slam on a rhythm and
// crush what they pin against a wall; elevators carry whoever stands on them; steam vents blow a column upward; the
// electrified rails are live on a rhythm (a touch kills); the propeller shreds and sucks; a torpedo tube fires a stick
// that walks into its mouth across the stage; a window's glass breaks at its second; glass breaks under a stick's
// weight after 0.6 s.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace sf {

static Rectangle MoverRect(const Piece& p) { return {p.x * TILE + p.off.x, p.y * TILE + p.off.y, p.w * TILE, p.h * TILE}; }
bool World::MoverHits(float x0, float y0, float x1, float y1, int* which) const {
    for (int i = 0; i < (int)stage.pieces.size(); i++) {
        const Piece& p = stage.pieces[i];
        if (p.kind != PK_PISTON && p.kind != PK_ELEVATOR) continue;
        Rectangle r = MoverRect(p);
        if (x1 > r.x + 1e-4f && x0 < r.x + r.width - 1e-4f && y1 > r.y + 1e-4f && y0 < r.y + r.height - 1e-4f) { if (which) *which = i; return true; }
    }
    return false;
}
bool World::RopeUnder(float x0, float x1, float yOld, float yNew) const {
    int ix0 = (int)floorf(x0 / TILE), ix1 = (int)floorf((x1 - 1e-4f) / TILE);
    int iy0 = (int)floorf(yNew / TILE), iy1 = (int)floorf(yOld / TILE);
    for (int y = iy0; y <= iy1; y++) { float top = (y + 1) * TILE; if (top > yOld + 1e-3f || top < yNew - 1e-3f) continue; for (int x = ix0; x <= ix1; x++) if (stage.OneWay(x, y)) return true; }
    return false;
}
bool World::RailLive(int x, int y) const { (void)y; return fmodf(t * 0.25f + x * 0.013f, 1.0f) < 0.4f; }   // (live 1.6 s in 4)

void World::StepPieces() {
    if (glassT.size() != stage.t.size()) glassT.assign(stage.t.size(), 0);
    for (auto& k : sticks) k.hazT = std::max(0.0f, k.hazT - STEP);
    for (int i = 0; i < (int)stage.pieces.size(); i++) {
        Piece& p = stage.pieces[i];
        if (t < p.start) continue;   // (asleep: a finale's set piece before its second)
        p.cool = std::max(0.0f, p.cool - STEP);
        if (p.kind >= PK_STALACTITE) { StepHazard(p); continue; }
        if ((p.kind == PK_PISTON || p.kind == PK_ELEVATOR) && moversStopped) continue;   // (the Earthquake stopped them)
        if (p.kind == PK_PISTON || p.kind == PK_ELEVATOR) {
            float u = fmodf(t / std::max(0.2f, p.period) + p.phase, 1.0f), ext;
            if (p.kind == PK_ELEVATOR) ext = 0.5f - 0.5f * cosf(u * 2 * PI);
            else { float hold = std::clamp(p.on / p.period, 0.05f, 0.7f); ext = u < 0.06f ? u / 0.06f : u < 0.06f + hold ? 1.0f : u < 0.06f + hold + 0.25f ? 1 - (u - 0.06f - hold) / 0.25f : 0.0f; }
            p.prevOff = p.off;
            p.off = {p.dx * p.travel * TILE * ext, p.dy * p.travel * TILE * ext};
            Vector2 d = Vector2Subtract(p.off, p.prevOff);
            if (d.x == 0 && d.y == 0) continue;
            Rectangle r = MoverRect(p), r0{r.x - d.x, r.y - d.y, r.width, r.height};
            for (auto& k : sticks) {
                if (!k.present || !k.alive || k.st == S_RAGDOLL) continue;
                float kx0 = k.pos.x - k.halfW, kx1 = k.pos.x + k.halfW;
                bool riding = kx1 > r0.x && kx0 < r0.x + r0.width && fabsf(k.pos.y - (r0.y + r0.height)) < 0.06f;
                bool inside = kx1 > r.x && kx0 < r.x + r.width && k.pos.y + k.height > r.y && k.pos.y < r.y + r.height;
                if (riding) { k.pos = Vector2Add(k.pos, d); if (d.y > 0) k.pos.y = r.y + r.height; }
                else if (inside) {
                    // pushed: along the piston's stroke; pinned against stone, crushed
                    Vector2 np = k.pos;
                    if (fabsf(d.x) > fabsf(d.y)) np.x = d.x > 0 ? r.x + r.width + k.halfW + 0.01f : r.x - k.halfW - 0.01f;
                    else np.y = d.y > 0 ? r.y + r.height + 0.01f : r.y - k.height - 0.01f;
                    if (BoxHits(np.x - k.halfW, np.y, np.x + k.halfW, np.y + k.height) && !MoverHits(np.x - k.halfW, np.y, np.x + k.halfW, np.y + k.height)) { Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 3 ? k.lastHitBy : -1, p.kind == PK_PISTON ? "a piston" : "an elevator"); Emit(EV_CRUSH, k.pos, k.id); }
                    else { k.pos = np; k.vel = Vector2Add(k.vel, Vector2Scale(d, 1 / STEP * 0.5f)); }
                }
            }
            continue;
        }
        if (p.kind == PK_VENT) {
            float u = fmodf(t / std::max(0.2f, p.period) + p.phase, 1.0f);
            bool live = u < std::clamp(p.on / p.period, 0.05f, 0.95f);
            if (!live) continue;
            float x0 = p.x * TILE, x1 = (p.x + p.w) * TILE, y0 = (p.y + p.h) * TILE, y1 = y0 + p.travel * TILE;
            for (auto& k : sticks) {
                if (!k.present) continue;
                if (k.alive && k.st != S_RAGDOLL && k.pos.x > x0 - k.halfW && k.pos.x < x1 + k.halfW && k.pos.y < y1 && k.pos.y + k.height > y0) { k.vel.y = std::max(k.vel.y, 15 * p.power); k.knockT = 0.1f; if (k.st != S_WALL) k.st = S_AIR; }
                for (auto& a : k.pt) if (a.p.x > x0 && a.p.x < x1 && a.p.y > y0 && a.p.y < y1) a.q.y -= 0.1f * p.power * STEP * 60;
            }
            for (auto& it : items) if (it.alive && it.holder < 0 && it.a.p.x > x0 && it.a.p.x < x1 && it.a.p.y > y0 && it.a.p.y < y1) { it.a.q.y -= 0.12f * STEP * 60; it.b.q.y -= 0.12f * STEP * 60; }
            continue;
        }
        if (p.kind == PK_PROPELLER) {
            Vector2 c{(p.x + p.w * 0.5f) * TILE, (p.y + p.h * 0.5f) * TILE}; float hw = p.w * TILE * 0.5f, hh = p.h * TILE * 0.5f;
            for (auto& k : sticks) {
                if (!k.present) continue;
                Vector2 to = Vector2Subtract(c, k.pt[J_PELVIS].p); float L = Vector2Length(to);
                if (L < 4.5f && L > 0.01f) { Nudge(k, Vector2Scale(to, 3.2f * p.power * (1.15f - L / 4.5f) * STEP / L)); for (auto& a : k.pt) a.q = Vector2Subtract(a.q, Vector2Scale(to, 0.6f * p.power * STEP * STEP / L)); }
                bool in = fabsf(k.pt[J_PELVIS].p.x - c.x) < hw + 0.2f && fabsf(k.pt[J_PELVIS].p.y - c.y) < hh + 0.4f;
                if (k.alive && in) { Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, "the propeller"); Emit(EV_EXPLODE, c, -1, -1, 0.5f); }
            }
            continue;
        }
        if (p.kind == PK_TUBE) {
            float x0 = p.x * TILE, x1 = (p.x + p.w) * TILE, y0 = p.y * TILE, y1 = (p.y + p.h + 1) * TILE;
            if (p.cool > 0) continue;
            for (auto& k : sticks) {
                if (!k.present || !k.alive || k.st == S_RAGDOLL) continue;
                if (k.pos.x + k.halfW < x0 || k.pos.x - k.halfW > x1 || k.pos.y > y1 || k.pos.y + k.height < y0) continue;
                Vector2 v{p.dx * p.power, 2.5f + p.dy * p.power};
                k.st = S_RAGDOLL; k.ragT = 1.0f; k.stiff = 0;
                for (auto& a : k.pt) a.q = Vector2Subtract(a.p, Vector2Scale(v, STEP));
                k.vel = v; p.cool = std::max(0.4f, p.period);
                Emit(EV_THROW, k.pt[J_NECK].p, k.id, -1, 1);
                break;
            }
            continue;
        }
        if (p.kind == PK_WINDOW && !p.broken && t >= p.phase) {
            p.broken = true;
            for (int y = p.y; y < p.y + p.h; y++) for (int x = p.x; x < p.x + p.w; x++) if (stage.At(x, y) == T_GLASS) stage.Set(x, y, T_EMPTY);
            Emit(EV_EXPLODE, {(p.x + p.w * 0.5f) * TILE, (p.y + p.h * 0.5f) * TILE}, -1, -1, 0.3f);
        }
    }
    // crumbling floors keep going once started: gone 0.7 s after the first step
    for (int i = 0; i < (int)stage.t.size(); i++) if (stage.t[i] == T_CRUMBLE && glassT[i] > 0) { glassT[i] += STEP; if (glassT[i] > 0.7f) { stage.t[i] = T_EMPTY; glassT[i] = 0; Emit(EV_HIT, {(i % stage.w + 0.5f) * TILE, (i / stage.w + 1) * TILE}, -1, -1, 0); } }
    // the rails (a touch while live kills) and glass under weight
    for (auto& k : sticks) {
        if (!k.present || !k.alive) continue;
        int ix0 = (int)floorf((k.pos.x - k.halfW - 0.04f) / TILE), ix1 = (int)floorf((k.pos.x + k.halfW + 0.04f) / TILE), iy0 = (int)floorf((k.pos.y - 0.04f) / TILE), iy1 = (int)floorf((k.pos.y + k.height + 0.04f) / TILE);
        bool zapped = false;
        for (int y = iy0; y <= iy1 && !zapped; y++) for (int x = ix0; x <= ix1 && !zapped; x++) if (stage.At(x, y) == T_RAIL && RailLive(x, y)) zapped = true;
        if (zapped) { Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, "an electrified rail"); Emit(EV_EXPLODE, k.pt[J_PELVIS].p, -1, -1, 0.2f); continue; }
        // urchins: a touch stings and throws you off (stage 5)
        if (k.hazT <= 0) {
            int ux0 = (int)floorf((k.pos.x - k.halfW - 0.06f) / TILE), ux1 = (int)floorf((k.pos.x + k.halfW + 0.06f) / TILE), uy0 = (int)floorf((k.pos.y - 0.06f) / TILE), uy1 = (int)floorf((k.pos.y + k.height) / TILE);
            for (int y = uy0; y <= uy1 && k.hazT <= 0; y++) for (int x = ux0; x <= ux1 && k.hazT <= 0; x++) if (stage.At(x, y) == T_URCHIN) {
                Vector2 away = Vector2Normalize(Vector2Subtract(k.pt[J_PELVIS].p, {(x + 0.5f) * TILE, (y + 0.5f) * TILE})); away.y = std::max(away.y, 0.5f);
                k.hazT = 0.6f; Hit(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, 15, Vector2Normalize(away), 9, false, "urchins");
            }
            if (!k.alive) continue;
        }
        if (k.grounded) {
            int gx = (int)floorf(k.pos.x / TILE), gy = (int)floorf((k.pos.y - 0.05f) / TILE);
            if (stage.At(gx, gy) == T_CRUMBLE && gy >= 0 && gx >= 0 && gx < stage.w && glassT[gy * stage.w + gx] <= 0) glassT[gy * stage.w + gx] = STEP;   // (a crumbling floor starts to go)
            bool window = false; for (const auto& p : stage.pieces) window |= p.kind == PK_WINDOW && !p.broken && gx >= p.x && gx < p.x + p.w && gy >= p.y && gy < p.y + p.h;   // (the window holds until its second)
            if (!window && stage.At(gx, gy) == T_GLASS && gy >= 0 && gx >= 0 && gx < stage.w) { float& g = glassT[gy * stage.w + gx]; g += STEP; if (g > 0.6f) { stage.Set(gx, gy, T_EMPTY); Emit(EV_HIT, {(gx + 0.5f) * TILE, (gy + 1) * TILE}, -1, -1, 0); } }
        }
    }
}

} // namespace sf
