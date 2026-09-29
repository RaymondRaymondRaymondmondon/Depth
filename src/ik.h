// ============================================================================
//  DEPTH - procedural animation (ParkourReference1.2.pdf, "Procedural Animation: FABRIK"): chains of fixed-length
//  bones that reach for a target. Legs plant their feet on the ground and step when the body gets too far
//  ahead (the gait lives in beasts.cpp); tentacles, tails and necks bend toward whatever they're reaching for.
// ============================================================================
#pragma once
#include "raylib.h"
#include <cmath>

namespace ik {
inline Vector2 Lerp(Vector2 a, Vector2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
inline float Dist(Vector2 a, Vector2 b) { float dx = b.x - a.x, dy = b.y - a.y; return sqrtf(dx * dx + dy * dy); }

// Forward And Backward Reaching Inverse Kinematics: p[0] is the pinned root, p[n-1] the tip; len[k] is the bone
// from p[k] to p[k+1]. Out of reach, the chain simply points at the target.
inline void Fabrik(Vector2* p, int n, const float* len, Vector2 target, int iters = 10, float tol = 0.5f) {
    if (n < 2) return;
    Vector2 root = p[0];
    float total = 0;
    for (int k = 0; k < n - 1; k++) total += len[k];
    if (Dist(root, target) >= total) {
        for (int k = 0; k < n - 1; k++) {
            float d = Dist(p[k], target), l = d > 1e-4f ? len[k] / d : 0;
            p[k + 1] = Lerp(p[k], target, l);
        }
        return;
    }
    for (int it = 0; it < iters && Dist(p[n - 1], target) > tol; it++) {
        p[n - 1] = target; // backward: from the tip
        for (int k = n - 2; k >= 0; k--) {
            float d = Dist(p[k + 1], p[k]), l = d > 1e-4f ? len[k] / d : 0;
            p[k] = Lerp(p[k + 1], p[k], l);
        }
        p[0] = root; // forward: from the root
        for (int k = 0; k < n - 1; k++) {
            float d = Dist(p[k], p[k + 1]), l = d > 1e-4f ? len[k] / d : 0;
            p[k + 1] = Lerp(p[k], p[k + 1], l);
        }
    }
}

// The two-bone special case, solved exactly: where the knee (or elbow) goes for a hip and a foot. bend picks
// which way it folds (+1 or -1, relative to the hip-to-foot direction).
inline Vector2 Knee(Vector2 hip, Vector2 foot, float l1, float l2, float bend) {
    float d = Dist(hip, foot);
    float lo = fabsf(l1 - l2) + 0.01f, hi = l1 + l2 - 0.01f;
    float dc = d < lo ? lo : d > hi ? hi : d;
    Vector2 dir = d > 1e-4f ? Vector2{(foot.x - hip.x) / d, (foot.y - hip.y) / d} : Vector2{0, 1};
    float a = (l1 * l1 - l2 * l2 + dc * dc) / (2 * dc);
    float h = sqrtf(fmaxf(0.0f, l1 * l1 - a * a));
    return {hip.x + dir.x * a - dir.y * h * bend, hip.y + dir.y * a + dir.x * h * bend};
}
}  // namespace ik
