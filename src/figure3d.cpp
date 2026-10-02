// The shared humanoid figure's posing (see figure3d.h). Moved out of trawl_view3d.cpp so Red Tide's divers use it too.
#include "figure3d.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace fig {

// the rotation part of a skinning matrix (its columns normalised: a build or a breath may scale it)
static Quaternion RotOf(Matrix m) {
    Vector3 x = Vector3Normalize({m.m0, m.m1, m.m2}), y = Vector3Normalize({m.m4, m.m5, m.m6}), z = Vector3Normalize({m.m8, m.m9, m.m10});
    Matrix r = MatrixIdentity();
    r.m0 = x.x; r.m1 = x.y; r.m2 = x.z; r.m4 = y.x; r.m5 = y.y; r.m6 = y.z; r.m8 = z.x; r.m9 = z.y; r.m10 = z.z;
    return QuaternionNormalize(QuaternionFromMatrix(r));
}
// Two-bone IK: turns the upper arm and forearm so the fist closes on the target, the elbow bending down and out
// (toward the pole). The fist sits a hand's length past the wrist, so the wrist aims short of the target.
static void ArmIK(const rt::RigInfo& rig, rt::RigPose& pose, int side, Vector3 target) {
    const char* sd = side ? "R" : "L";
    int ua = rig.Find(TextFormat("upperarm.%s", sd)), fa = rig.Find(TextFormat("forearm.%s", sd)), hn = rig.Find(TextFormat("hand.%s", sd));
    if (ua < 0 || fa < 0 || hn < 0 || rig.parent[ua] < 0) return;
    std::vector<Matrix> skin = rt::SolveRig(rig, pose);
    int par = rig.parent[ua];
    Vector3 S = Vector3Transform(rig.joint[ua], skin[par]);
    float a = Vector3Distance(rig.joint[ua], rig.joint[fa]), b = Vector3Distance(rig.joint[fa], rig.joint[hn]);
    Vector3 toT = Vector3Subtract(target, S);
    float dT = std::max(Vector3Length(toT), 1e-4f);
    Vector3 dir = Vector3Scale(toT, 1 / dT);
    Vector3 W = Vector3Subtract(target, Vector3Scale(dir, 0.075f));   // the wrist, short of the fist
    float d = std::clamp(Vector3Distance(W, S), fabsf(a - b) + 0.01f, a + b - 0.005f);
    float ca = std::clamp((a * a + d * d - b * b) / (2 * a * d), -1.0f, 1.0f), sa = sqrtf(1 - ca * ca);
    Vector3 pole{-0.3f, -1.0f, side ? 0.6f : -0.6f};                   // the elbow: down, back a little, out to its side
    Vector3 bend = Vector3Subtract(pole, Vector3Scale(dir, Vector3DotProduct(pole, dir)));
    bend = Vector3Length(bend) > 1e-4f ? Vector3Normalize(bend) : Vector3{0, -1, 0};
    Vector3 E = Vector3Add(S, Vector3Add(Vector3Scale(dir, a * ca), Vector3Scale(bend, a * sa)));
    Vector3 Wd = Vector3Add(S, Vector3Scale(Vector3Normalize(Vector3Subtract(W, S)), d));
    Quaternion C = RotOf(skin[par]);
    Vector3 bindUA = Vector3Normalize(Vector3Subtract(rig.joint[fa], rig.joint[ua]));
    Vector3 bindFA = Vector3Normalize(Vector3Subtract(rig.joint[hn], rig.joint[fa]));
    Vector3 wantUA = Vector3RotateByQuaternion(Vector3Normalize(Vector3Subtract(E, S)), QuaternionInvert(C));
    Quaternion qUA = QuaternionFromVector3ToVector3(bindUA, wantUA);
    pose.rot[ua] = qUA;
    Quaternion U = QuaternionMultiply(C, qUA);
    Vector3 wantFA = Vector3RotateByQuaternion(Vector3Normalize(Vector3Subtract(Wd, E)), QuaternionInvert(U));
    pose.rot[fa] = QuaternionFromVector3ToVector3(bindFA, wantFA);
}

// the pose: rotations in the model's axes (x forward, y up, z the figure's right) about each bind joint
std::vector<Matrix> PoseFigure(const Model& m, const Build& L, const Pose& P, float t) {
    const rt::RigInfo& rig = rt::RigOf(m);
    rt::RigPose pose; pose.Reset((int)rig.name.size());
    auto B = [&](const char* n) { return rig.Find(n); };
    auto rot = [&](int b, Vector3 axis, float a) { if (b >= 0) pose.rot[b] = QuaternionMultiply(pose.rot[b], QuaternionFromAxisAngle(axis, a)); };
    const Vector3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    float br = sinf(P.breathe) * 0.5f + 0.5f;
    // build and height: the pelvis carries the whole figure; the chest and thighs broaden with the build
    int pel = B("pelvis"), ch = B("chest"), hd = B("head");
    if (pel >= 0) pose.scale[pel] = {L.height, L.height, L.height};
    if (ch >= 0) pose.scale[ch] = {L.build * (1 + 0.015f * br), 1 + 0.008f * br, L.build * (1 + 0.012f * br)};
    if (hd >= 0) pose.scale[hd] = {L.headW, L.headH, L.headW};
    for (const char* e : {"eye.L", "eye.R"}) { int b = B(e); if (b >= 0) pose.scale[b] = {1, std::max(0.08f, 1 - P.blink), 1}; }
    { int mo = B("mouth"); if (mo >= 0) pose.scale[mo] = {1, 0.3f + 1.1f * P.shout, 0.85f + 0.2f * P.shout}; }   // a thin line at rest
    // the walk: thighs swing, knees bend on the forward leg, a little hip roll; seated: thighs forward, shins down
    float s1 = sinf(P.walkPh), stride = 0.55f * P.walk;
    // swimming: a flutter kick from the hips, knees soft, the legs trailing straight behind the line of the body
    float k1 = sinf(P.kickPh) * 0.32f * P.swim;
    rot(B("thigh.L"), Z, s1 * stride + 1.45f * P.sit + k1); rot(B("thigh.R"), Z, -s1 * stride + 1.45f * P.sit - k1);
    rot(B("shin.L"), Z, -std::max(0.0f, -s1) * 0.7f * P.walk - 1.5f * P.sit - (0.25f + 0.2f * std::max(0.0f, sinf(P.kickPh + 1.2f))) * P.swim);
    rot(B("shin.R"), Z, -std::max(0.0f, s1) * 0.7f * P.walk - 1.5f * P.sit - (0.25f + 0.2f * std::max(0.0f, -sinf(P.kickPh + 1.2f))) * P.swim);
    for (const char* f : {"foot.L", "foot.R"}) rot(B(f), Z, -0.9f * P.swim);   // (the boots pointed back, toes trailing)
    rot(B("spine"), Y, s1 * 0.08f * P.walk);
    rot(B("spine"), Z, -0.05f * P.walk - 0.06f * P.reach);   // a lean into the stride and the work
    pose.offset.y = -fabsf(cosf(P.walkPh)) * 0.025f * P.walk - 0.42f * P.sit * L.height;
    // the arms: down out of the A-pose, then forward to the work or swinging with the stride; treading water: out and paddling
    float reachL = P.reachL < 0 ? P.reach : P.reachL;
    for (int side = 0; side < 2; side++) {
        const char* sd = side ? "R" : "L";
        float sg = side ? 1.0f : -1.0f;   // the right arm lowers about +X, the left about -X
        int ua = B(TextFormat("upperarm.%s", sd)), fa = B(TextFormat("forearm.%s", sd)), hn = B(TextFormat("hand.%s", sd));
        float reach = side ? P.reach : reachL;
        // (a rotation added later turns first: so the arm comes down out of the A-pose (X), then swings forward (Z),
        // then turns in toward the middle (Y))
        float down = 0.72f * (1 - P.tread) - 0.25f * P.tread;
        float swing = (side ? s1 : -s1) * 0.5f * P.walk * (1 - reach);
        float stroke = P.swim * (1 - reach) * (0.35f + 0.25f * sinf(P.kickPh * 0.5f + side * PI));   // a slow breast stroke
        float fwd = swing + reach * 1.1f + (side ? P.swingT * 1.8f : 0) + P.tread * 0.4f * sinf(t * 4 + side * 3) + stroke;
        float in = reach * 0.25f, elbow = 0.15f + reach * 0.5f + P.elbow * 1.2f + 0.3f * P.swim * (1 - reach);
        if (P.fp) {   // your own arms: tipped up to the look, the left under a long tool's fore-end
            fwd += P.aimUp + (side ? 0.0f : 0.1f * P.twoHand);
            in += side ? 0.12f : 0.12f + 0.35f * P.twoHand;
            elbow += side ? 0.0f : -0.35f * P.twoHand;
        }
        rot(ua, Y, sg * in);
        rot(ua, Z, fwd);
        rot(ua, X, sg * down);
        rot(fa, Z, elbow);
        // the hand turned on the wrist so the back of it is up (knuckles to the sky when the arm reaches forward):
        // a twist about the forearm's own line
        if (fa >= 0 && hn >= 0) {
            Vector3 axis = Vector3Normalize(Vector3Subtract(rig.joint[hn], rig.joint[fa]));
            rot(hn, axis, -sg * (0.4f + 1.1f * reach));
        }
        rot(hn, X, sg * 0.25f);
        // the fingers curl round what they hold
        for (const char* f : {"index", "middle", "ring", "pinky"})
            for (int k = 1; k <= 3; k++) rot(B(TextFormat("%s%d.%s", f, k, sd)), X, sg * P.grip * (k == 1 ? 0.9f : 1.1f));
        for (int k = 1; k <= 3; k++) rot(B(TextFormat("thumb%d.%s", k, sd)), Y, -sg * P.grip * 0.5f);
    }
    // the head: a slight sway with the breath, turned to look (a swimmer lifts it to see ahead)
    rot(B("neck"), Y, P.look * 0.4f); rot(hd, Y, P.look * 0.6f);
    rot(hd, Z, -P.nod + 0.02f * sinf(P.breathe) + 0.5f * P.swim);
    if (P.fp) {
        // your own body from the eye: the head (and its hat) gone, the arms brought up before you
        if (hd >= 0) pose.scale[hd] = {0.001f, 0.001f, 0.001f};
        int nk = B("neck"); if (nk >= 0) pose.scale[nk] = {0.4f, 0.4f, 0.4f};
    }
    for (int s = 0; s < 2; s++) if (P.ik[s]) ArmIK(rig, pose, s, P.target[s]);
    return rt::SolveRig(rig, pose);
}

Vector3 FistWorld(const Model& m, const std::vector<Matrix>& skin, Matrix frame, int side) {
    const rt::RigInfo& rig = rt::RigOf(m);
    int b = rig.Find(side ? "middle1.R" : "middle1.L");
    if (b < 0) b = rig.Find(side ? "hand.R" : "hand.L");
    Matrix w = rt::BoneWorld(rig, skin, b, frame);
    return {w.m12, w.m13, w.m14};
}

Matrix Frame(Vector3 feet, float yaw) {
    return MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(feet.x, feet.y, feet.z));
}

} // namespace fig
