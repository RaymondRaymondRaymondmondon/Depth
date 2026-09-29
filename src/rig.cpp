// ============================================================================
//  DEPTH - the shared figure rig: materials, the humanoid solve, clips, verlet chains, the face and
//  depth-sorted parts. See rig.h. The clip data lives in rig_clips.cpp.
// ============================================================================
#include "rig.h"
#include "game.h"
#include "ik.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rig {

// ============================================================ materials
// The figure shader inks and lights every part; on top of that each material answers the key light its own way:
// metal and wet things catch a hard specular glint, cloth stays matte, shell takes growth bands, glow ignores
// the light and blooms.
static Vector2 KeyDir() { Vector2 k = CurSceneLight().keyDir; float l = sqrtf(k.x * k.x + k.y * k.y); return l > 1e-3f ? Vector2{k.x / l, k.y / l} : Vector2{-0.55f, -0.83f}; }
static Color KeyCol() { return CurSceneLight().key; }

void MLimb(Vector2 a, Vector2 b, float wa, float wb, Color c, Mat m) {
    if (m == CLOTH) c = Tone(c, -0.04f);          // matte: a touch flatter and darker than bare forms
    ShadeLimb(a, b, wa, wb, c);
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    Vector2 n{-dy / len, dx / len}, k = KeyDir();
    float face = n.x * k.x + n.y * k.y;
    if (face < 0) { n = {-n.x, -n.y}; face = -face; }  // the side toward the light
    auto at = [&](float u, float side) { float w = wa + (wb - wa) * u; return Vector2{a.x + dx * u + n.x * w * side, a.y + dy * u + n.y * w * side}; };
    switch (m) {
        case METAL: { // a hard, narrow glint along the lit side, and a second faint one: polished
            Color hi = ColorBrightness(KeyCol(), 0.1f);
            DrawLineEx(at(0.15f, 0.45f), at(0.85f, 0.45f), std::max(1.0f, (wa + wb) * 0.16f), Fade(hi, 0.85f));
            DrawLineEx(at(0.3f, -0.55f), at(0.7f, -0.55f), std::max(1.0f, (wa + wb) * 0.08f), Fade(hi, 0.25f));
        } break;
        case WET: // a broken, wet shine
            for (int i = 0; i < 3; i++) DrawLineEx(at(0.12f + i * 0.28f, 0.5f), at(0.24f + i * 0.28f, 0.5f), std::max(1.0f, (wa + wb) * 0.12f), Fade(WHITE, 0.55f));
            break;
        case SHELL: // growth bands across the segment
            for (int i = 1; i < 4; i++) DrawLineEx(at(i / 4.0f, -0.9f), at(i / 4.0f, 0.9f), std::max(1.0f, (wa + wb) * 0.06f), Fade(Tone(c, -0.5f), 0.7f));
            break;
        case GLOW: Glow({(a.x + b.x) / 2, (a.y + b.y) / 2}, (len + wa + wb) * 0.9f, Fade(c, 0.35f)); break;
        default: break;
    }
}

void MBall(Vector2 c, float r, Color col, Mat m) {
    if (m == CLOTH) col = Tone(col, -0.04f);
    ShadeBall(c, r, col);
    Vector2 k = KeyDir();
    switch (m) {
        case METAL: DrawCircleV({c.x + k.x * r * 0.45f, c.y + k.y * r * 0.45f}, std::max(1.0f, r * 0.12f), Fade(ColorBrightness(KeyCol(), 0.2f), 0.7f)); break;
        case WET: DrawCircleV({c.x + k.x * r * 0.5f, c.y + k.y * r * 0.5f}, std::max(1.0f, r * 0.14f), Fade(WHITE, 0.7f)); break;
        case SHELL: DrawRing(c, r * 0.55f, r * 0.62f, 0, 360, 16, Fade(Tone(col, -0.45f), 0.6f)); break;
        case GLOW: Glow(c, r * 3.2f, Fade(col, 0.4f)); break;
        default: break;
    }
}

void MQuad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c, Mat m) {
    if (m == CLOTH) c = Tone(c, -0.04f);
    ShadeQuad(tl, tr, br, bl, c);
    if (m == METAL || m == WET) { // a sheen band across the upper third
        Vector2 a{tl.x + (bl.x - tl.x) * 0.2f, tl.y + (bl.y - tl.y) * 0.2f}, b{tr.x + (br.x - tr.x) * 0.2f, tr.y + (br.y - tr.y) * 0.2f};
        DrawLineEx(a, b, 1.2f, Fade(WHITE, m == METAL ? 0.45f : 0.3f));
    }
}

// ============================================================ poses
RPose Scaled(const RPose& p, float k) { RPose o; for (int i = 0; i < C_COUNT; i++) o.v[i] = p.v[i] * k; return o; }
RPose Mix(const RPose& a, const RPose& b, float k) { RPose o; for (int i = 0; i < C_COUNT; i++) o.v[i] = a.v[i] + (b.v[i] - a.v[i]) * k; return o; }

const char* BoneName(int b) {
    static const char* n[BONE_COUNT] = {"root", "hips", "spine", "chest", "neck", "head", "shoulder.F", "elbow.F", "wrist.F", "shoulder.B", "elbow.B", "wrist.B",
                                        "hip.F", "knee.F", "ankle.F", "hip.B", "knee.B", "ankle.B", "prop"};
    return b >= 0 && b < BONE_COUNT ? n[b] : "?";
}

// ============================================================ easing and clips
float EaseFn(Ease e, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    const float c = 1.70158f;
    switch (e) {
        case LINEAR: return u;
        case ANTICIPATE: return u * u * ((c + 1) * u - c);
        case SNAP: { float v = 1 - u; return 1 - v * v * v * v; }
        case OVERSHOOT: { float v = u - 1; return 1 + (c + 1) * v * v * v + c * v * v; }
        case SETTLE: return 1 - (1 - u) * expf(-4.0f * u) * cosf(11.0f * u);
        default: return u * u * (3 - 2 * u);
    }
}

RPose Clip::Sample(float t) const {
    RPose o;
    if (loop && dur > 0) { t = fmodf(t, dur); if (t < 0) t += dur; }
    for (const Track& tr : tracks) {
        const auto& k = tr.keys;
        if (k.empty()) continue;
        float v;
        if (t <= k.front().t) v = k.front().v;
        else if (t >= k.back().t) v = k.back().v;
        else {
            size_t i = 1;
            while (i < k.size() && k[i].t < t) i++;
            const Key &a = k[i - 1], &b = k[i];
            v = a.v + (b.v - a.v) * EaseFn(b.e, (t - a.t) / std::max(1e-4f, b.t - a.t));
        }
        o.v[tr.ch] += v;
    }
    return o;
}

// ============================================================ the humanoid solve
static Vector2 Dir(float a) { return {cosf(a), sinf(a)}; }
static Vector2 Add(Vector2 a, Vector2 b, float k = 1) { return {a.x + b.x * k, a.y + b.y * k}; }

Vector2 Solved::Chest(float x, float y) const { // chest frame: origin at the chest bone, x toward the facing, y down the spine
    float a = this->a[CHEST];
    Vector2 up = Dir(a), fwd{-up.y * f, up.x * f};
    if (f < 0) fwd = {up.y, -up.x};
    return {p[CHEST].x + (fwd.x * x - up.x * y) * s, p[CHEST].y + (fwd.y * x - up.y * y) * s};
}
Vector2 Solved::Head(float x, float y) const {
    float a = this->a[HEAD];
    Vector2 up = Dir(a), fwd = f > 0 ? Vector2{-up.y, up.x} : Vector2{up.y, -up.x};
    return {p[HEAD].x + (fwd.x * x - up.x * y) * s, p[HEAD].y + (fwd.y * x - up.y * y) * s};
}
Vector2 Solved::Hips(float x, float y) const {
    float a = this->a[HIPS];
    Vector2 up = Dir(a), fwd = f > 0 ? Vector2{-up.y, up.x} : Vector2{up.y, -up.x};
    return {p[HIPS].x + (fwd.x * x - up.x * y) * s, p[HIPS].y + (fwd.y * x - up.y * y) * s};
}
Vector2 Solved::Along(int bone, int child, float u, float side) const {
    Vector2 a0 = p[bone], b0 = p[child];
    float dx = b0.x - a0.x, dy = b0.y - a0.y, l = sqrtf(dx * dx + dy * dy);
    Vector2 n = l > 1e-4f ? Vector2{-dy / l, dx / l} : Vector2{0, 0};
    if (n.x * f < 0) n = {-n.x, -n.y};
    return {a0.x + dx * u + n.x * side * s, a0.y + dy * u + n.y * side * s};
}

Solved SolveHumanoid(const Build& b, const RPose& P, Vector2 feet, float s, float f) {
    Solved o;
    o.f = f; o.s = s; o.armZ = P[C_ARMZ];
    Vector2 root{feet.x + P[C_ROOTX] * f * s, feet.y + P[C_ROOTY] * s};
    o.p[ROOT] = root; o.a[ROOT] = -PI / 2;
    float legLen = b.thigh + b.shin, hipH = legLen * 0.975f;
    Vector2 hips{root.x + P[C_HIPX] * f * s, root.y + (-hipH + P[C_HIPY]) * s};
    float spineA = -PI / 2 + P[C_LEAN] * f;              // upright is straight up; lean tips toward the facing
    o.p[HIPS] = hips; o.a[HIPS] = spineA;
    o.p[SPINE] = hips; o.a[SPINE] = spineA;
    Vector2 chest = Add(hips, Dir(spineA), b.spine * s);
    float chestA = spineA + P[C_CHEST] * f;
    o.p[CHEST] = chest; o.a[CHEST] = chestA;
    Vector2 neck = Add(chest, Dir(chestA), b.chest * s);
    o.p[NECK] = neck; o.a[NECK] = chestA;
    float headA = chestA + P[C_HEAD] * f;
    o.p[HEAD] = Add(neck, Dir(headA), (b.neck + b.head) * s); o.a[HEAD] = headA;
    // shoulders sit a little below the neck, across the chest
    Vector2 up = Dir(chestA), fwd = f > 0 ? Vector2{-up.y, up.x} : Vector2{up.y, -up.x};
    Vector2 sl = Add(neck, up, -4 * s);
    o.p[SH_F] = Add(sl, fwd, b.shoulderW * s);
    o.p[SH_B] = Add(sl, fwd, -b.shoulderW * 0.93f * s);
    // arms reach for their hands (relative to the shoulders), elbows folding back and down
    Vector2 hF{o.p[SH_F].x + P[C_HFX] * f * s, o.p[SH_F].y + P[C_HFY] * s};
    Vector2 hB{o.p[SH_B].x + P[C_HBX] * f * s, o.p[SH_B].y + P[C_HBY] * s};
    o.p[EL_F] = ik::Knee(o.p[SH_F], hF, b.upper * s, b.fore * s, f);
    o.p[EL_B] = ik::Knee(o.p[SH_B], hB, b.upper * s, b.fore * s, f);
    // the IK clamps an out-of-reach hand to the arm's length: put the wrist where the forearm actually ends
    auto reach = [&](Vector2 el, Vector2 want) { float d = ik::Dist(el, want); return d > 1e-3f ? ik::Lerp(el, want, b.fore * s / d) : want; };
    o.p[WR_F] = reach(o.p[EL_F], hF);
    o.p[WR_B] = reach(o.p[EL_B], hB);
    // legs plant their feet and bend their knees forward
    Vector2 hipDir{f, 0};
    o.p[HIP_F] = Add(hips, hipDir, b.hipW * s);
    o.p[HIP_B] = Add(hips, hipDir, -b.hipW * s);
    Vector2 fF{root.x + (b.stanceF + P[C_FFX]) * f * s, root.y - P[C_FFY] * s};
    Vector2 fB{root.x + (b.stanceB + P[C_FBX]) * f * s, root.y - P[C_FBY] * s};
    o.p[KN_F] = ik::Knee(o.p[HIP_F], fF, b.thigh * s, b.shin * s, -f);
    o.p[KN_B] = ik::Knee(o.p[HIP_B], fB, b.thigh * s, b.shin * s, -f);
    o.p[AN_F] = reach(o.p[KN_F], fF);
    o.p[AN_B] = reach(o.p[KN_B], fB);
    auto ang = [](Vector2 a, Vector2 c) { return atan2f(c.y - a.y, c.x - a.x); };
    o.a[SH_F] = ang(o.p[SH_F], o.p[EL_F]); o.a[EL_F] = ang(o.p[EL_F], o.p[WR_F]); o.a[WR_F] = o.a[EL_F];
    o.a[SH_B] = ang(o.p[SH_B], o.p[EL_B]); o.a[EL_B] = ang(o.p[EL_B], o.p[WR_B]); o.a[WR_B] = o.a[EL_B];
    o.a[HIP_F] = ang(o.p[HIP_F], o.p[KN_F]); o.a[KN_F] = ang(o.p[KN_F], o.p[AN_F]); o.a[AN_F] = 0;
    o.a[HIP_B] = ang(o.p[HIP_B], o.p[KN_B]); o.a[KN_B] = ang(o.p[KN_B], o.p[AN_B]); o.a[AN_B] = 0;
    float w = P[C_WEAPON] * DEG2RAD;
    o.p[PROP] = o.p[WR_F];
    o.a[PROP] = f > 0 ? w : PI - w;
    return o;
}

// ============================================================ chains
void Chain::Init(Vector2 anchor, int n, float segLen, Vector2 dir) {
    seg = segLen;
    p.assign(n, anchor); q.assign(n, anchor);
    for (int i = 0; i < n; i++) p[i] = q[i] = {anchor.x + dir.x * seg * i, anchor.y + dir.y * seg * i};
    live = true;
}
void Chain::Shift(Vector2 d) { for (size_t i = 1; i < p.size(); i++) { p[i].x -= d.x; p[i].y -= d.y; q[i].x -= d.x; q[i].y -= d.y; } }
void Chain::Kick(Vector2 v) { for (size_t i = 1; i < p.size(); i++) { float k = (float)i / p.size(); q[i].x -= v.x * k / 60.0f; q[i].y -= v.y * k / 60.0f; } }
void Chain::Step(Vector2 anchor, Vector2 rest, float dt, Vector2 current) {
    if (p.empty()) return;
    dt = std::min(dt, 0.1f);
    int sub = std::max(1, (int)ceilf(dt / (1 / 90.0f)));
    float h = dt / sub;
    for (int sI = 0; sI < sub; sI++) {
        p[0] = anchor; q[0] = anchor;
        for (size_t i = 1; i < p.size(); i++) {
            Vector2 v{(p[i].x - q[i].x) * damp, (p[i].y - q[i].y) * damp};
            q[i] = p[i];
            float flutter = sinf((float)GetTime() * 2.3f + i * 0.9f) * 0.35f;   // water never holds perfectly still
            p[i].x += v.x + (rest.x * grav + current.x * (1 + flutter)) * h * h;
            p[i].y += v.y + (rest.y * grav + current.y * (1 + flutter)) * h * h;
        }
        for (int it = 0; it < 3; it++) {
            for (size_t i = 1; i < p.size(); i++) {
                Vector2 tgt{p[i - 1].x + rest.x * seg, p[i - 1].y + rest.y * seg}; // a little shape memory: it hangs, it doesn't pool
                p[i].x += (tgt.x - p[i].x) * stiff * 0.34f;
                p[i].y += (tgt.y - p[i].y) * stiff * 0.34f;
                float dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y, d = sqrtf(dx * dx + dy * dy);
                if (d < 1e-4f) continue;
                float k = seg / d;
                p[i] = {p[i - 1].x + dx * k, p[i - 1].y + dy * k};
            }
        }
    }
}
void Chain::Draw(float s) const {
    for (size_t i = 1; i < p.size(); i++) {
        float u0 = (i - 1) / (float)(p.size() - 1), u1 = i / (float)(p.size() - 1);
        MLimb(p[i - 1], p[i], (width0 + (width1 - width0) * u0) * s, (width0 + (width1 - width0) * u1) * s, col, mat);
    }
}

// ============================================================ the face
static float Hash(unsigned n) { n = (n << 13) ^ n; return ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffff) / 2147483647.0f; }
void Face::Update(float dt, unsigned seed) {
    next -= dt;
    shut = std::max(0.0f, shut - dt);
    if (next <= 0) {
        shut = 0.13f;
        static unsigned n = 0;
        next = 3 + 3 * Hash(seed * 31 + ++n);   // every 3-6 s
        if (Hash(seed + n * 7) < 0.15f) next = 0.25f; // now and then a double blink
    }
}
void DrawEyes(const Face& fc, Vector2 c, float spacing, float r, float s, float f, Color lid, Color iris) {
    for (int e = 0; e < 2; e++) {
        Vector2 ec{c.x + (e ? spacing : 0) * f * s, c.y};
        float rr = r * (e ? 0.86f : 1.0f) * s;         // the far eye is a little smaller: it turns away
        if (fc.Closed()) {
            DrawEllipse((int)ec.x, (int)ec.y, rr * 1.2f, rr * 0.8f, lid);
            DrawLineEx({ec.x - rr * 1.1f, ec.y + rr * 0.1f}, {ec.x + rr * 1.1f, ec.y + rr * 0.25f}, std::max(1.0f, rr * 0.35f), Color{12, 10, 10, 255});
            continue;
        }
        DrawEllipse((int)ec.x, (int)ec.y, rr * 1.15f, rr * 0.8f, Color{214, 206, 188, 255});
        Vector2 ip{ec.x + fc.look.x * f * rr * 0.4f, ec.y + fc.look.y * rr * 0.3f};
        DrawCircleV(ip, rr * 0.62f, iris);
        DrawCircleV(ip, rr * 0.3f, Color{10, 8, 8, 255});
        DrawLineEx({ec.x - rr * 1.25f, ec.y - rr * 0.75f}, {ec.x + rr * 1.25f, ec.y - rr * 0.6f}, std::max(1.0f, rr * 0.45f), Color{12, 10, 10, 255}); // the upper lid line
    }
}
void DrawMouth(const Face& fc, Vector2 c, float w, float s, float f, Color lip) {
    float hw = w * 0.5f * s;
    if (fc.mouth == 1) { // open: a shout
        DrawEllipse((int)(c.x + f * hw * 0.1f), (int)(c.y + 1.5f * s), hw, 2.8f * s, Color{30, 12, 12, 255});
        DrawLineEx({c.x - hw, c.y + 0.4f * s}, {c.x + hw, c.y + 0.2f * s}, 1.0f * s, lip);
    } else if (fc.mouth == 2) { // a grimace: bared teeth
        DrawRectangle((int)(c.x - hw), (int)(c.y - 0.6f * s), (int)(hw * 2), (int)std::max(1.0f, 2.0f * s), Color{224, 216, 196, 255});
        DrawLineEx({c.x - hw, c.y - 0.6f * s}, {c.x + hw, c.y - 1.2f * s}, 1.0f * s, Color{30, 12, 12, 255});
        DrawLineEx({c.x - hw, c.y + 1.4f * s}, {c.x + hw, c.y + 1.8f * s}, 1.0f * s, Color{30, 12, 12, 255});
    } else {
        DrawLineEx({c.x - hw, c.y}, {c.x + hw, c.y - 0.4f * s}, std::max(1.0f, 1.2f * s), lip);
    }
}

// ============================================================ per-figure state
static std::unordered_map<int, Instance>& Table() { static std::unordered_map<int, Instance> t; return t; }
Instance& Get(int key) {
    Instance& in = Table()[key];
    if (!in.init) { in.seed = (unsigned)key * 2654435761u + 7; in.face.next = 1 + 4 * Hash(in.seed); }
    return in;
}
void Tick(Instance& in, float t) {
    in.dt = in.init ? std::clamp(t - in.lastT, 0.0f, 0.1f) : 0.0f;
    in.lastT = t;
    in.init = true;
    in.face.Update(in.dt, in.seed);
    if (in.reaction >= 0) {
        in.reactT += in.dt;
        const Clip& c = GetClip(in.reaction);
        if (!c.loop && in.reactT > c.dur && in.reaction != CL_DEATH) in.reaction = -1;
    }
}
void React(int key, int clip) {
    Instance& in = Get(key);
    in.reaction = clip; in.reactT = 0;
    for (auto& ch : in.chains) ch.Kick({clip == CL_HIT || clip == CL_CRIT ? -260.0f : 0.0f, -120});
}

// ============================================================ parts
void Parts::Draw() {
    std::stable_sort(list.begin(), list.end(), [](const P& a, const P& b) { return a.z < b.z; });
    for (auto& p : list) p.fn();
    list.clear();
}

// ============================================================ the world
static Vector2 gOff{0, 0}, gCur{0, 0};
void SetWorldOffset(Vector2 off) { gOff = off; }
Vector2 WorldOffset() { return gOff; }
void SetCurrent(Vector2 c) { gCur = c; }
Vector2 Current() { return gCur; }

// ============================================================ the combat Pose, as rig channels
RPose FromPose(const ::Pose& p, float walk, float t, int style) {
    (void)style;
    auto ease = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };
    RPose o;
    float rz = ease(p.raise), rc = ease(p.reach), bz = ease(p.backRaise);
    o[C_HFY] += -64 * rz + 12 * rc;
    o[C_HFX] += -8 * rz + 40 * rc;
    o[C_HBY] += -58 * bz;
    o[C_HBX] += 6 * bz;
    o[C_HIPY] += 18 * p.crouch;
    o[C_LEAN] += 0.55f * p.lean;
    o[C_HEAD] += 0.32f * p.headDown;
    o[C_FFX] += 16 * p.stride;
    if (p.stride < 0) o[C_FBX] += 10 * p.stride;
    if (walk != 0) o += GetClip(CL_WALK).Sample(fmodf(walk, 2 * PI) / (2 * PI) * GetClip(CL_WALK).dur);
    if (p.tremble > 0) { // fear: the hands and head shake
        o[C_HFX] += sinf(t * 47) * 1.8f * p.tremble; o[C_HFY] += cosf(t * 41) * 1.4f * p.tremble;
        o[C_HBX] += sinf(t * 43 + 1) * 1.8f * p.tremble; o[C_HEAD] += sinf(t * 39) * 0.04f * p.tremble;
    }
    return o;
}

}  // namespace rig
