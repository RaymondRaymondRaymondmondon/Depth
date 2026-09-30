// ============================================================================
//  DEPTH - figures built on the shared rig (rig.h). Each one: a Build (proportions), a rest pose, clips layered
//  over it (idle, breathing, the combat Pose, reactions), chains for everything that should trail (coat skirts,
//  a sash, a censer, manacle chains, robe rags), a face that blinks and tracks, and depth-sorted parts in
//  materials. Silhouettes first: big hands and weapons, wide shoulders, long torsos, small heads.
//  Done so far: the Captain (hero) and the Lost One Cultist (enemy); the rest still use render.cpp / enemyart.cpp.
// ============================================================================
#include "game.h"
#include "rig.h"
#include <algorithm>
#include <cmath>

using namespace rig;

namespace {
Vector2 L2(Vector2 a, Vector2 b, float k) { return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}; }
Vector2 Off(Vector2 a, float x, float y, float s, float f) { return {a.x + x * s * f, a.y + y * s}; }

// the acting clip set by the combat scene for the next enemy drawn (its attack, its flinch)
int gActClip = -1;
float gActT = 0;

// keep an instance's chains in step with where the figure is on screen: when the figure moves, the chains
// keep their place in the world for a moment and trail behind; a jump (a different screen) re-hangs them
bool FollowWorld(Instance& in, float s) {
    Vector2 off = WorldOffset();
    Vector2 d{off.x - in.lastOff.x, off.y - in.lastOff.y};
    in.lastOff = off;
    bool jump = fabsf(d.x) > 160 || fabsf(d.y) > 160 || in.chains.empty() || in.dt == 0;
    if (!jump) for (auto& c : in.chains) c.Shift(d);
    (void)s;
    return jump;
}

// a hanging cloth panel between two chains: quads strip by strip, shaded as cloth
void Panel(const Chain& a, const Chain& b, Color c, float darken) {
    size_t n = std::min(a.p.size(), b.p.size());
    for (size_t k = 1; k < n; k++) {
        float u = k / (float)(n - 1);
        MQuad(a.p[k - 1], b.p[k - 1], b.p[k], a.p[k], Tone(c, darken - u * 0.12f), CLOTH);
    }
}
}  // namespace

void RigSetActing(int clip, float t) { gActClip = clip; gActT = t; }

// ============================================================================ THE CAPTAIN
// Wide shoulders under brass epaulettes, a long greatcoat whose skirts are three chains (so they swing and trail),
// a red sash end, a big gloved hand on a heavy cutlass held high, a clockwork prosthetic for the other.
void DrawRigCaptain(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
    Color skin = skins[seed % 4], coat{40, 50, 82, 255}, coatDk = Tone(coat, -0.3f), trousers{36, 34, 42, 255}, boots{34, 26, 22, 255};
    Color brass = Pal::Brass, steel{196, 200, 208, 255}, glove{222, 216, 200, 255}, red{150, 36, 34, 255};

    Build b;
    b.thigh = 44; b.shin = 42; b.upper = 29; b.fore = 27; b.spine = 28; b.chest = 26; b.neck = 7; b.head = 11;
    b.shoulderW = 18.5f; b.hipW = 7; b.stanceF = 17; b.stanceB = -15;   // a wide, planted stance

    // the rest pose (Darkest Dungeon's ready stance): knees bent, weight forward, chin down, the cutlass held low and
    // forward across the body, point toward the foe; the prosthetic clenched at his side
    RPose P;
    P[C_HIPY] = 5; P[C_HIPX] = 2; P[C_LEAN] = 0.12f; P[C_CHEST] = 0.05f; P[C_HEAD] = 0.06f;
    P[C_HFX] = 20; P[C_HFY] = 40; P[C_WEAPON] = 18;
    P[C_HBX] = -4; P[C_HBY] = 44;
    P += GetClip(CL_IDLE).Sample(t + h.id * 1.7f);
    P += GetClip(CL_BREATHE).Sample(t + h.id);
    float stressW = std::clamp(pose.tremble * 1.4f, 0.0f, 1.0f);
    if (stressW > 0) P += Scaled(GetClip(CL_STRESSED).Sample(t), stressW);
    P[C_WEAPON] *= 1 - std::clamp(std::max(pose.reach, pose.raise), 0.0f, 1.0f);   // the low guard gives way to the strike
    P += FromPose(pose, walk, t, 0);
    P[C_WEAPON] += -110 * std::clamp(pose.raise, 0.0f, 1.0f) + pose.weaponTilt;
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);

    Solved S = SolveHumanoid(b, P, ft, s, f);

    // the face: blink, look toward the foe (or glance about when rattled), shout on the strike, grimace when hurt
    in.face.look = {1.0f, stressW > 0.4f ? sinf(t * 1.3f) * 0.6f : 0.1f};
    in.face.mouth = pose.headDown < -0.3f ? 2 : (pose.reach > 0.5f || pose.raise > 0.7f) ? 1 : 0;

    // chains: three coat skirts from the waist, and the sash end
    bool jump = FollowWorld(in, s);
    Vector2 skirtRest{-0.18f * f, 1};
    float rl = sqrtf(skirtRest.x * skirtRest.x + 1); skirtRest = {skirtRest.x / rl, 1 / rl};
    Vector2 anchors[4] = {S.Hips(-15, 2), S.Hips(-2, 4), S.Hips(12, 3), S.Hips(15, -4)};
    if (jump || in.chains.size() != 4) {
        in.chains.assign(4, Chain{});
        for (int i = 0; i < 3; i++) { in.chains[i].Init(anchors[i], 6, 9.5f * s, skirtRest); in.chains[i].stiff = 0.35f; in.chains[i].grav = 420; }
        in.chains[3].Init(anchors[3], 5, 6 * s, {0.1f * f, 1}); in.chains[3].col = red; in.chains[3].width0 = 3.2f; in.chains[3].width1 = 2.2f; in.chains[3].stiff = 0.2f;
    }
    Vector2 cur = Current();
    for (int i = 0; i < 4; i++) in.chains[i].Step(anchors[i], i < 3 ? skirtRest : Vector2{0.1f * f, 1}, in.dt, cur);

    Parts parts;
    // --- the back arm (the prosthetic): behind the body, unless a cross-body swing brings it round
    float backZ = S.armZ > 0.5f ? 2.5f : -2.0f;
    parts.Add(backZ, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 8.4f * s, 7.2f * s, Tone(coat, -0.22f), CLOTH);
        MBall(S.p[EL_B], 4.0f * s, Tone(steel, -0.15f), METAL);                                            // an exposed elbow joint
        MLimb(S.p[EL_B], S.p[WR_B], 6.6f * s, 5.6f * s, Tone(steel, -0.1f), METAL);                       // a plated forearm
        for (int k = 0; k < 3; k++) DrawCircleV(S.Along(EL_B, WR_B, 0.25f + k * 0.25f, 0), 0.9f * s, Tone(steel, -0.5f));
        MBall(S.p[WR_B], 7.0f * s, steel, METAL);                                                          // the clamp-hand, big
        DrawCircleV(S.p[WR_B], 2.2f * s, brass);
        DrawLineEx(Off(S.p[WR_B], 2, 3, s, f), Off(S.p[WR_B], 9, 5, s, f), 2.6f * s, Tone(steel, -0.3f));  // its two fingers
        DrawLineEx(Off(S.p[WR_B], 1, 5, s, f), Off(S.p[WR_B], 7, 9, s, f), 2.6f * s, Tone(steel, -0.35f));
    });
    // --- legs, and the coat's skirts between them
    auto leg = [&](int hip, int kn, int an, Color col) {
        MLimb(S.p[hip], S.p[kn], 10.5f * s, 8.6f * s, col, CLOTH);
        MLimb(S.p[kn], S.p[an], 8.4f * s, 6.6f * s, col, CLOTH);
        Vector2 heel = Off(S.p[an], -3, 1, s, f), toe = Off(S.p[an], 13, 3, s, f);
        MLimb(S.Along(kn, an, 0.52f, 0), S.Along(kn, an, 0.7f, 0), 9.6f * s, 9.2f * s, Tone(boots, 0.25f), WET); // a folded boot cuff
        MLimb(heel, toe, 7.6f * s, 6.4f * s, boots, WET);
        DrawLineEx(Off(heel, -2, 7, s, f), Off(toe, 3, 5.5f, s, f), 1.8f * s, Tone(boots, -0.6f));        // the sole
    };
    parts.Add(-1.6f, [&] { leg(HIP_B, KN_B, AN_B, Tone(trousers, -0.25f)); });
    parts.Add(-1.3f, [&] { Panel(in.chains[0], in.chains[1], coatDk, -0.05f); });                             // the coat's back skirt
    parts.Add(-1.0f, [&] { leg(HIP_F, KN_F, AN_F, trousers); });
    parts.Add(-0.8f, [&] {
        Panel(in.chains[1], in.chains[2], coat, 0.02f);                                                       // its front skirt, over the legs
        for (int i = 0; i < 3; i += 2) { // gold piping down the front edge and along the hem
            const Chain& c = in.chains[i == 0 ? 1 : 2];
            for (size_t k = 1; k < c.p.size(); k++) DrawLineEx(c.p[k - 1], c.p[k], 1.3f * s, i == 2 ? brass : Fade(brass, 0.0f));
        }
        for (size_t k = 1; k < in.chains[1].p.size(); k++) { // two folds running down the front skirt
            DrawLineEx(L2(in.chains[1].p[k - 1], in.chains[2].p[k - 1], 0.35f), L2(in.chains[1].p[k], in.chains[2].p[k], 0.3f), 1.2f * s, Fade(Color{10, 10, 20, 255}, 0.6f));
            DrawLineEx(L2(in.chains[1].p[k - 1], in.chains[2].p[k - 1], 0.7f), L2(in.chains[1].p[k], in.chains[2].p[k], 0.72f), 1.0f * s, Fade(Color{10, 10, 20, 255}, 0.5f));
        }
        DrawLineEx(in.chains[1].Tip(), in.chains[2].Tip(), 1.8f * s, brass);
        DrawLineEx(in.chains[0].Tip(), in.chains[1].Tip(), 1.4f * s, Tone(brass, -0.3f));
    });
    // --- the torso: the greatcoat, lapels, buttons, sash, medals, epaulettes
    parts.Add(0, [&] {
        MQuad(S.Chest(-21, -23), S.Chest(22, -23), S.Hips(14, 1), S.Hips(-14, 1), coat, CLOTH);   // broad at the chest, narrow at the waist
        MQuad(S.Chest(4, -24), S.Chest(10, -24), S.Chest(7, 8), S.Chest(2, 8), Tone(coat, 0.25f), CLOTH);   // the lapel
        DrawTri(S.Chest(14, -24), S.Chest(5, -24), S.Chest(10, -6), Color{226, 222, 212, 255});           // the shirt front
        for (int k = 0; k < 3; k++) MBall(S.Chest(12, -13 + k * 10.0f), 1.9f * s, brass, METAL);
        MQuad(S.Chest(-17, -20), S.Chest(-10, -23), S.Hips(15, -3), S.Hips(9, 2), red, CLOTH);             // the baldric, shoulder to hip
        DrawLineEx(S.Chest(-13, -21), S.Hips(12, -1), 0.9f * s, Fade(Color{60, 10, 10, 255}, 0.7f));
        MQuad(S.Hips(-16, -5), S.Hips(16, -5), S.Hips(15, 1), S.Hips(-15, 1), Color{70, 46, 28, 255}, WET);  // the belt
        MBall(S.Hips(11, -2), 2.8f * s, brass, METAL);
        for (int k = 0; k < 3; k++) { // medals on ribbons
            Vector2 top = S.Chest(-8 + k * 5.5f, -16), md = S.Chest(-8 + k * 5.5f, -9);
            DrawLineEx(top, md, 1.8f * s, k == 1 ? Color{40, 70, 150, 255} : Color{170, 36, 36, 255});
            MBall(md, 2.2f * s, brass, METAL);
        }
        Vector2 chainA = S.Chest(-6, -20), chainB = S.Chest(10, -3);                                       // a watch chain looped into a pocket
        for (int k = 0; k <= 5; k++) { float u = k / 5.0f; Vector2 p = L2(chainA, chainB, u); DrawCircleV({p.x, p.y + sinf(u * PI) * 3.2f * s}, 0.9f * s, brass); }
        MLimb(Off(S.p[SH_B], -7, 1, s, f), Off(S.p[SH_B], 6, -1, s, f), 5.6f * s, 5.0f * s, Tone(brass, -0.1f), METAL); // an epaulette: a flat dome
        for (int k = 0; k < 6; k++) DrawLineEx(Off(S.p[SH_B], -8 + k * 2.8f, 4, s, f), Off(S.p[SH_B], -8 + k * 2.9f, 12, s, f), 1.4f * s, Pal::BrassDk); // its fringe
        for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-12 + k * 9.0f, -18), S.Hips(-10 + k * 9.0f, -14), 1.2f * s, Fade(Color{10, 10, 20, 255}, 0.55f)); // folds down the coat
    });
    // --- the sash end, hanging from the hip in front of the coat
    parts.Add(0.2f, [&] { in.chains[3].Draw(s); });
    // --- the head: a square jaw, a heavy nose, eyes sunk in the shadow of the cap's peak, a grey beard on some
    parts.Add(0.5f, [&] {
        MLimb(S.Chest(1, -26), S.Head(0, 9), 8.2f * s, 7.4f * s, Tone(skin, -0.08f), SKIN);                 // a thick neck, set forward
        MQuad(S.Chest(-7, -24), S.Chest(9, -24), S.Head(8, 13), S.Head(-5, 12), Color{226, 222, 212, 255}, CLOTH); // the high collar
        MBall(S.Head(-9, 1), 3.4f * s, Tone(skin, -0.1f), SKIN);                                           // the ear
        MBall(S.p[HEAD], 14.2f * s, skin, SKIN);                                                            // the skull
        MQuad(S.Head(-6, 1), S.Head(12, 0), S.Head(10.5f, 13), S.Head(-2, 14.5f), skin, SKIN);              // a square jaw
        MLimb(S.Head(9.5f, -2), S.Head(13.5f, 4.5f), 2.0f * s, 3.0f * s, Tone(skin, 0.05f), SKIN);          // the nose
        DrawLineEx(S.Head(4, 5), S.Head(6, 9.5f), 1.1f * s, Fade(Color{40, 20, 16, 255}, 0.6f));           // the cheek's crease
        // the shadow of the cap's peak across the eyes, and two eyes in it that catch the light (and blink)
        DrawTri(S.Head(-3, -6.5f), S.Head(15, -6.5f), S.Head(13, -4.5f), Color{30, 16, 14, 90});           // the peak's thin shadow
        DrawLineEx(S.Head(2, -4.6f), S.Head(7, -4.2f), 1.8f * s, Color{40, 26, 20, 255});                  // heavy brows, set apart
        DrawLineEx(S.Head(8.5f, -4.2f), S.Head(12.5f, -4.8f), 1.8f * s, Color{40, 26, 20, 255});
        for (int k = 0; k < 2; k++) {
            Vector2 e = S.Head(k ? 9.5f : 3.5f, -2.5f);
            DrawEllipse((int)e.x, (int)e.y, (k ? 1.9f : 2.4f) * s, 1.2f * s, Color{120, 110, 96, 255});
            if (!in.face.Closed()) { DrawCircleV({e.x + f * 0.5f * s, e.y}, 0.95f * s, Color{30, 22, 20, 255}); DrawCircleV({e.x + f * 0.2f * s, e.y - 0.3f * s}, 0.35f * s, Color{236, 230, 214, 255}); }
            else DrawLineEx({e.x - 2.2f * s, e.y}, {e.x + 2.2f * s, e.y + 0.3f * s}, 0.9f * s, Color{30, 20, 18, 255});
        }
        if (seed % 2) { // a grey beard and moustache, cut square
            Color gb{150, 146, 140, 255};   // a short full beard along the jaw, and a heavy moustache
            MLimb(S.Head(-4, 6), S.Head(7, 13), 4.2f * s, 5.4f * s, gb, CLOTH);
            MLimb(S.Head(7, 13), S.Head(11.5f, 8), 5.0f * s, 3.2f * s, gb, CLOTH);
            MLimb(S.Head(6, 6.2f), S.Head(13.5f, 7.2f), 2.0f * s, 1.5f * s, Tone(gb, 0.1f), CLOTH);
            for (int k = 0; k < 4; k++) DrawLineEx(S.Head(0 + k * 2.8f, 9 + k * 0.8f), S.Head(1 + k * 2.8f, 14 + k * 0.3f), 0.8f * s, Tone(gb, -0.45f));
            if (in.face.mouth) DrawEllipse((int)S.Head(9, 9).x, (int)S.Head(9, 9).y, 2.4f * s, 1.6f * s, Color{30, 12, 12, 255});
        } else {
            DrawMouth(in.face, S.Head(8, 8.5f), 6.0f, s, f, Color{120, 64, 56, 255});
            for (int k = 0; k < 10; k++) DrawCircleV(S.Head(1 + (k % 5) * 2.2f, 9 + (k / 5) * 2.0f), 0.45f * s, Fade(Color{40, 30, 26, 255}, 0.6f)); // stubble
        }
        // the naval cap: a broad crown sloping forward, a brass band and badge, a black peak
        MQuad(S.Head(-12, -19), S.Head(14, -21), S.Head(13, -8), S.Head(-11, -7), Color{28, 28, 36, 255}, CLOTH);
        MQuad(S.Head(-11, -9), S.Head(13, -10), S.Head(13, -6.5f), S.Head(-11, -6), Tone(brass, -0.15f), METAL);
        MBall(S.Head(7, -14), 2.4f * s, brass, METAL);
        MLimb(S.Head(3, -6.5f), S.Head(17, -5), 2.4f * s, 1.4f * s, Color{12, 12, 16, 255}, WET);
    });    // --- the weapon arm, the cutlass and the big gloved hand, in front of everything
    parts.Add(1.0f, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 8.8f * s, 7.6f * s, coat, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 7.6f * s, 6.4f * s, coat, CLOTH);
        MBall(S.Along(EL_F, WR_F, 0.8f, 0), 6.8f * s, Tone(brass, -0.25f), CLOTH);                          // a gold-braided cuff
        float a = S.a[PROP];
        Vector2 dir{cosf(a), sinf(a)};
        auto W = [&](float along) { return Vector2{S.p[WR_F].x + dir.x * along * s, S.p[WR_F].y + dir.y * along * s}; };
        MLimb(W(3), W(48), 3.4f * s, 1.2f * s, steel, METAL);                                               // a heavy blade, curved at the tip
        DrawLineEx(W(6), W(44), 0.9f * s, Fade(WHITE, 0.55f));
        MLimb(W(-7), W(0), 2.2f * s, 2.2f * s, Color{60, 40, 30, 255}, CLOTH);                              // the grip
        MBall(S.p[WR_F], 7.6f * s, glove, CLOTH);                                                            // the gloved fist, big
        MBall(Off(S.p[WR_F], 4, -3, s, f), 3.0f * s, glove, CLOTH);                                          // the thumb
        DrawRing(W(1), 5.6f * s, 7.4f * s, 0, 360, 16, brass);                                               // the basket guard
        MBall(W(-8), 2.6f * s, brass, METAL);                                                                // the pommel
        MLimb(Off(S.p[SH_F], -6, -1, s, f), Off(S.p[SH_F], 7, 1, s, f), 6.0f * s, 5.4f * s, brass, METAL);     // the near epaulette
        for (int k = 0; k < 6; k++) DrawLineEx(Off(S.p[SH_F], -7 + k * 2.9f, 4, s, f), Off(S.p[SH_F], -7 + k * 3.0f, 13, s, f), 1.4f * s, Pal::BrassDk);
    });
    parts.Draw();
}

// ============================================================================ THE NURSE
// A naval field nurse in a crouched, ready stance: a long blue-grey coat whose skirts swing (two chains), a white
// apron over it that swings on its own (two more), a red cross on the bib, a bandolier of glass vials, a folded
// white cap on a bun with a red ribbon trailing, a big syringe held like a dagger and a satchel at the hip.
void DrawRigNurse(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
    const Color hairs[4] = {{40, 28, 22, 255}, {90, 58, 34, 255}, {150, 96, 50, 255}, {26, 22, 22, 255}};
    Color skin = skins[seed % 4], hair = hairs[(seed / 4) % 4], coat{72, 94, 124, 255}, coatDk = Tone(coat, -0.3f), apron{232, 226, 212, 255};
    Color legs{46, 42, 48, 255}, boots{44, 32, 26, 255}, red{176, 40, 36, 255}, leather{110, 76, 48, 255}, steel{196, 200, 208, 255}, glass{150, 208, 228, 255};

    Build b;
    b.thigh = 40; b.shin = 40; b.upper = 26; b.fore = 25; b.spine = 26; b.chest = 23; b.neck = 7; b.head = 12;
    b.shoulderW = 14.5f; b.hipW = 6; b.stanceF = 16; b.stanceB = -14;

    // the rest pose: low and ready, the syringe held point-forward at the hip, the other hand on the satchel
    RPose P;
    P[C_HIPY] = 6; P[C_LEAN] = 0.16f; P[C_CHEST] = 0.06f; P[C_HEAD] = -0.04f;
    P[C_HFX] = 18; P[C_HFY] = 34; P[C_WEAPON] = -8;
    P[C_HBX] = -8; P[C_HBY] = 42;
    P += GetClip(CL_IDLE).Sample(t + h.id * 1.7f);
    P += GetClip(CL_BREATHE).Sample(t + h.id);
    float stressW = std::clamp(pose.tremble * 1.4f, 0.0f, 1.0f);
    if (stressW > 0) P += Scaled(GetClip(CL_STRESSED).Sample(t), stressW);
    P += FromPose(pose, walk, t, 0);
    P[C_WEAPON] += -80 * std::clamp(pose.raise, 0.0f, 1.0f) + pose.weaponTilt;
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);
    Solved S = SolveHumanoid(b, P, ft, s, f);

    in.face.look = {1.0f, stressW > 0.4f ? sinf(t * 1.3f) * 0.6f : 0.05f};
    in.face.mouth = pose.headDown < -0.3f ? 2 : (pose.reach > 0.5f || pose.raise > 0.7f) ? 1 : 0;

    // chains: coat skirts (back, front), apron (two), the ribbon from her bun
    bool jump = FollowWorld(in, s);
    Vector2 rest{-0.14f * f, 1};
    float rl = sqrtf(rest.x * rest.x + 1); rest = {rest.x / rl, 1 / rl};
    Vector2 anchors[5] = {S.Hips(-14, 2), S.Hips(13, 2), S.Hips(-5, 0), S.Hips(10, 0), S.Head(-12, -8)};
    if (jump || in.chains.size() != 5) {
        in.chains.assign(5, Chain{});
        for (int i = 0; i < 2; i++) { in.chains[i].Init(anchors[i], 6, 9.5f * s, rest); in.chains[i].stiff = 0.35f; in.chains[i].grav = 420; }
        for (int i = 2; i < 4; i++) { in.chains[i].Init(anchors[i], 5, 8.5f * s, rest); in.chains[i].stiff = 0.3f; in.chains[i].grav = 380; }
        in.chains[4].Init(anchors[4], 5, 5.5f * s, {-0.5f * f, 1}); in.chains[4].col = red; in.chains[4].width0 = 3.0f; in.chains[4].width1 = 2.0f; in.chains[4].stiff = 0.15f;
    }
    Vector2 cur = Current();
    for (int i = 0; i < 5; i++) in.chains[i].Step(anchors[i], i < 4 ? rest : Vector2{-0.5f * f, 1}, in.dt, cur);

    Parts parts;
    // the back arm: its hand on the satchel at her hip
    parts.Add(-2.0f, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 7.6f * s, 6.8f * s, Tone(coat, -0.22f), CLOTH);
        MLimb(S.p[EL_B], S.p[WR_B], 6.6f * s, 5.4f * s, Tone(coat, -0.18f), CLOTH);
        MBall(S.Along(EL_B, WR_B, 0.85f, 0), 5.6f * s, Tone(apron, -0.2f), CLOTH);   // a turned-back white cuff
        MBall(S.p[WR_B], 5.4f * s, Tone(skin, -0.12f), SKIN);
    });
    // the satchel, slung at the back hip
    parts.Add(-1.8f, [&] {
        Vector2 c = S.Hips(-12, 10);
        MQuad(Off(c, -9, -8, s, f), Off(c, 9, -8, s, f), Off(c, 8, 9, s, f), Off(c, -8, 9, s, f), leather, CLOTH);
        MQuad(Off(c, -9, -8, s, f), Off(c, 9, -8, s, f), Off(c, 8, -1, s, f), Off(c, -8, -1, s, f), Tone(leather, 0.15f), CLOTH); // its flap
        MBall(Off(c, 0, -1, s, f), 1.6f * s, Pal::Brass, METAL);
    });
    auto leg = [&](int hip, int kn, int an, Color col) {
        MLimb(S.p[hip], S.p[kn], 9.0f * s, 7.4f * s, col, CLOTH);
        MLimb(S.p[kn], S.p[an], 7.4f * s, 5.8f * s, col, CLOTH);
        MLimb(S.Along(kn, an, 0.35f, 0), S.p[an], 7.8f * s, 6.6f * s, boots, WET);                    // tall laced boots
        for (int k = 0; k < 3; k++) DrawLineEx(S.Along(kn, an, 0.45f + k * 0.17f, -4), S.Along(kn, an, 0.5f + k * 0.17f, 4), 0.9f * s, Color{226, 220, 204, 255});
        Vector2 heel = Off(S.p[an], -3, 1, s, f), toe = Off(S.p[an], 12, 3, s, f);
        MLimb(heel, toe, 6.6f * s, 5.6f * s, boots, WET);
        DrawLineEx(Off(heel, -2, 6, s, f), Off(toe, 3, 5, s, f), 1.6f * s, Tone(boots, -0.6f));
    };
    parts.Add(-1.6f, [&] { leg(HIP_B, KN_B, AN_B, Tone(legs, -0.25f)); });
    parts.Add(-1.3f, [&] { Panel(in.chains[0], in.chains[1], coatDk, -0.05f); });                     // the coat skirt behind
    parts.Add(-1.0f, [&] { leg(HIP_F, KN_F, AN_F, legs); });
    parts.Add(-0.7f, [&] {                                                                              // the apron over the legs
        Panel(in.chains[2], in.chains[3], apron, 0.0f);
        for (size_t k = 1; k < in.chains[2].p.size(); k++)
            DrawLineEx(L2(in.chains[2].p[k - 1], in.chains[3].p[k - 1], 0.5f), L2(in.chains[2].p[k], in.chains[3].p[k], 0.45f), 1.1f * s, Fade(Color{120, 110, 96, 255}, 0.7f));
        DrawLineEx(in.chains[2].Tip(), in.chains[3].Tip(), 1.4f * s, Tone(apron, -0.35f));             // a stained hem
    });
    // the torso: the coat, the apron's bib with its red cross, the bandolier of vials
    parts.Add(0, [&] {
        MQuad(S.Chest(-17, -21), S.Chest(18, -21), S.Hips(13, 3), S.Hips(-13, 3), coat, CLOTH);
        MQuad(S.Chest(-9, -19), S.Chest(12, -19), S.Hips(11, 1), S.Hips(-6, 1), apron, CLOTH);          // the bib
        Vector2 cx = S.Chest(2, -8);
        MQuad(Off(cx, -1.8f, -6, s, f), Off(cx, 1.8f, -6, s, f), Off(cx, 1.8f, 6, s, f), Off(cx, -1.8f, 6, s, f), red, CLOTH);
        MQuad(Off(cx, -6, -1.8f, s, f), Off(cx, 6, -1.8f, s, f), Off(cx, 6, 1.8f, s, f), Off(cx, -6, 1.8f, s, f), red, CLOTH);
        MQuad(S.Hips(-14, -5), S.Hips(14, -5), S.Hips(13, 1), S.Hips(-13, 1), Tone(leather, -0.2f), WET);   // the belt
        MLimb(S.Chest(-15, -20), S.Hips(12, -4), 2.4f * s, 2.4f * s, leather, CLOTH);                       // the bandolier
        for (int k = 0; k < 4; k++) {
            Vector2 v = L2(S.Chest(-12, -17), S.Hips(9, -7), 0.12f + k * 0.22f);
            MLimb(v, {v.x, v.y + 7 * s}, 2.4f * s, 2.4f * s, k == 2 ? Color{150, 220, 120, 255} : glass, GLOW);
            DrawCircleV({v.x, v.y - 1.2f * s}, 1.1f * s, Color{150, 110, 70, 255});
        }
        DrawLineEx(S.Chest(-14, -18), S.Hips(-11, -6), 1.1f * s, Fade(Color{10, 10, 20, 255}, 0.55f));  // folds
        DrawLineEx(S.Chest(15, -18), S.Hips(11, -6), 1.1f * s, Fade(Color{10, 10, 20, 255}, 0.55f));
    });
    // the head: a bun with a ribbon, the folded white cap with its cross, a narrow face, steady eyes
    parts.Add(0.3f, [&] { in.chains[4].Draw(s); });
    parts.Add(0.5f, [&] {
        MLimb(S.Chest(1, -24), S.Head(0, 8), 6.0f * s, 5.4f * s, Tone(skin, -0.08f), SKIN);
        MLimb(S.Chest(-7, -22), S.Chest(9, -22), 3.2f * s, 3.2f * s, Tone(coat, 0.1f), CLOTH);           // a turned-down collar
        MBall(S.Head(-10, -2), 6.2f * s, hair, CLOTH);                                                     // the bun
        MBall(S.Head(-5, -4), 12.0f * s, hair, CLOTH);                                                     // hair swept back under the cap
        MBall(S.p[HEAD], 12.4f * s, skin, SKIN);
        MQuad(S.Head(-4, 1), S.Head(10, 0), S.Head(8, 11.5f), S.Head(-1, 12.5f), skin, SKIN);             // a narrow jaw
        MLimb(S.Head(9.5f, -1), S.Head(12.5f, 4), 1.6f * s, 2.4f * s, Tone(skin, 0.05f), SKIN);           // the nose
        DrawEyes(in.face, S.Head(6.5f, -1.5f), 5.2f, 1.9f, s, f, Tone(skin, -0.35f), Color{60, 80, 70, 255});
        DrawLineEx(S.Head(3, -4.8f), S.Head(8, -4.4f), 1.3f * s, hair);                                   // brows
        DrawLineEx(S.Head(9.5f, -4.4f), S.Head(12, -4.9f), 1.3f * s, hair);
        DrawMouth(in.face, S.Head(8, 7.5f), 4.2f, s, f, Color{140, 70, 64, 255});
        // the cap: a folded white band standing up off the head, a red cross on its front
        MQuad(S.Head(-9, -17), S.Head(9, -18), S.Head(10, -8), S.Head(-10, -8), Color{236, 232, 222, 255}, CLOTH);
        DrawLineEx(S.Head(-9, -12.5f), S.Head(10, -13), 0.9f * s, Fade(Color{140, 130, 118, 255}, 0.8f));
        Vector2 cc = S.Head(3, -13);
        DrawLineEx(Off(cc, -2.6f, 0, s, f), Off(cc, 2.6f, 0, s, f), 1.5f * s, red);
        DrawLineEx(Off(cc, 0, -2.6f, s, f), Off(cc, 0, 2.6f, s, f), 1.5f * s, red);
    });
    // the syringe arm, in front
    parts.Add(1.0f, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 7.8f * s, 6.8f * s, coat, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 6.8f * s, 5.6f * s, coat, CLOTH);
        MBall(S.Along(EL_F, WR_F, 0.85f, 0), 5.8f * s, apron, CLOTH);                                     // the white cuff
        float a = S.a[PROP];
        Vector2 dir{cosf(a), sinf(a)};
        auto W = [&](float along) { return Vector2{S.p[WR_F].x + dir.x * along * s, S.p[WR_F].y + dir.y * along * s}; };
        MLimb(W(-10), W(-3), 1.4f * s, 1.4f * s, steel, METAL);                                          // the plunger
        MLimb(W(-11), W(-9.5f), 4.2f * s, 4.2f * s, steel, METAL);
        MLimb(W(2), W(20), 3.6f * s, 3.6f * s, Color{186, 214, 214, 255}, WET);                          // the glass barrel
        MLimb(W(4), W(14), 2.2f * s, 2.2f * s, Color{120, 220, 130, 255}, GLOW);                          // the dose
        MLimb(W(20), W(32), 0.8f * s, 0.4f * s, steel, METAL);                                           // the needle
        MBall(S.p[WR_F], 5.8f * s, skin, SKIN);                                                            // the fist
        MBall(Off(S.p[WR_F], 3, -3, s, f), 2.4f * s, skin, SKIN);
    });
    parts.Draw();
}

// ============================================================================ THE DIVER
// Broad and heavy in a canvas diving suit: a brass corselet with shoulder bolts, a lead-weighted belt, lead boots,
// a copper tank on his back with a hose that swings, a coiled line at the belt, a harpoon spear held low and
// forward, a knife in the other hand. On an expedition (gDiveGear) the porthole helmet; aboard, a knit cap.
void DrawRigDiver(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
    Color skin = skins[seed % 4], canvas{112, 106, 82, 255}, canvasDk = Tone(canvas, -0.25f), brass = Pal::Brass, copper{176, 104, 62, 255};
    Color lead{92, 90, 88, 255}, rubber{40, 36, 34, 255}, steel{196, 200, 208, 255}, wood{110, 76, 48, 255}, rope{168, 144, 104, 255};
    extern bool gDiveGear;

    Build b;
    b.thigh = 40; b.shin = 38; b.upper = 27; b.fore = 26; b.spine = 25; b.chest = 26; b.neck = 6; b.head = 12;
    b.shoulderW = 20; b.hipW = 8; b.stanceF = 19; b.stanceB = -17;

    // the rest pose: low and square, the harpoon levelled at the foe's knees, the knife held back at the hip
    RPose P;
    P[C_HIPY] = 7; P[C_LEAN] = 0.1f; P[C_CHEST] = 0.04f;
    P[C_HFX] = 22; P[C_HFY] = 36; P[C_WEAPON] = -14;
    P[C_HBX] = -6; P[C_HBY] = 40;
    P += GetClip(CL_IDLE).Sample(t + h.id * 1.7f);
    P += GetClip(CL_BREATHE).Sample(t + h.id);
    float stressW = std::clamp(pose.tremble * 1.4f, 0.0f, 1.0f);
    if (stressW > 0) P += Scaled(GetClip(CL_STRESSED).Sample(t), stressW);
    P[C_WEAPON] *= 1 - std::clamp(std::max(pose.reach, pose.raise), 0.0f, 1.0f);
    P += FromPose(pose, walk, t, 0);
    P[C_WEAPON] += -70 * std::clamp(pose.raise, 0.0f, 1.0f) + pose.weaponTilt;
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);
    Solved S = SolveHumanoid(b, P, ft, s, f);

    in.face.look = {1.0f, stressW > 0.4f ? sinf(t * 1.3f) * 0.6f : 0.05f};
    in.face.mouth = pose.headDown < -0.3f ? 2 : (pose.reach > 0.5f || pose.raise > 0.7f) ? 1 : 0;

    // chains: the air hose off the tank's valve, the coiled line's loose end
    bool jump = FollowWorld(in, s);
    Vector2 anchors[2] = {S.Chest(-24, -10), S.Hips(-6, 8)};
    if (jump || in.chains.size() != 2) {
        in.chains.assign(2, Chain{});
        in.chains[0].Init(anchors[0], 7, 7 * s, {-0.6f * f, 0.8f}); in.chains[0].col = rubber; in.chains[0].width0 = 3.4f; in.chains[0].width1 = 3.0f; in.chains[0].mat = WET; in.chains[0].stiff = 0.25f; in.chains[0].grav = 300;
        in.chains[1].Init(anchors[1], 5, 5.5f * s, {0.1f * f, 1}); in.chains[1].col = rope; in.chains[1].width0 = 1.6f; in.chains[1].width1 = 1.3f; in.chains[1].stiff = 0.1f;
    }
    Vector2 cur = Current();
    in.chains[0].Step(anchors[0], {-0.6f * f, 0.8f}, in.dt, cur);
    in.chains[1].Step(anchors[1], {0.1f * f, 1}, in.dt, cur);

    Parts parts;
    // the tank on his back, and its hose
    parts.Add(-3.0f, [&] {
        Vector2 a = S.Chest(-20, -16), c = S.Hips(-19, -8);
        MLimb(a, c, 8.6f * s, 8.6f * s, copper, METAL);
        MBall(a, 8.6f * s, Tone(copper, 0.05f), METAL);
        MBall(Off(a, 0, -8, s, f), 2.6f * s, brass, METAL);                                              // the valve
        for (int k = 0; k < 2; k++) MLimb(L2(a, c, 0.25f + k * 0.45f), L2(a, c, 0.3f + k * 0.45f), 9.2f * s, 9.2f * s, Tone(brass, -0.2f), METAL); // straps
    });
    parts.Add(-2.8f, [&] { in.chains[0].Draw(s); });
    // the knife arm, behind
    parts.Add(-2.0f, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 9.6f * s, 8.6f * s, canvasDk, CLOTH);
        MLimb(S.p[EL_B], S.p[WR_B], 8.6f * s, 7.4f * s, canvasDk, CLOTH);
        MLimb(S.Along(EL_B, WR_B, 0.7f, 0), S.p[WR_B], 8.4f * s, 7.8f * s, rubber, WET);                  // a rubber cuff
        MLimb(S.p[WR_B], Off(S.p[WR_B], 16, 4, s, f), 2.0f * s, 0.6f * s, steel, METAL);                 // the knife
        MBall(S.p[WR_B], 6.4f * s, Tone(canvas, -0.35f), CLOTH);                                          // a heavy mitt
    });
    auto leg = [&](int hip, int kn, int an, Color col) {
        MLimb(S.p[hip], S.p[kn], 11.6f * s, 10.0f * s, col, CLOTH);
        MLimb(S.p[kn], S.p[an], 10.0f * s, 8.6f * s, col, CLOTH);
        MLimb(S.Along(kn, an, 0.55f, 0), S.p[an], 10.6f * s, 10.0f * s, lead, METAL);                    // lead boots
        Vector2 heel = Off(S.p[an], -5, 2, s, f), toe = Off(S.p[an], 15, 4, s, f);
        MLimb(heel, toe, 9.0f * s, 8.0f * s, lead, METAL);
        MLimb(Off(heel, -2, 7, s, f), Off(toe, 3, 7, s, f), 2.4f * s, 2.4f * s, Tone(lead, -0.4f), METAL); // the thick sole
        for (int k = 0; k < 2; k++) MBall(Off(S.p[an], -2 + k * 9.0f, -2, s, f), 1.4f * s, brass, METAL);
    };
    parts.Add(-1.6f, [&] { leg(HIP_B, KN_B, AN_B, canvasDk); });
    parts.Add(-1.0f, [&] { leg(HIP_F, KN_F, AN_F, canvas); });
    // the torso: canvas, the corselet over it, the weight belt with its line
    parts.Add(0, [&] {
        MQuad(S.Chest(-22, -20), S.Chest(22, -20), S.Hips(16, 4), S.Hips(-16, 4), canvas, CLOTH);
        for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-12 + k * 11.0f, -6), S.Hips(-10 + k * 10.0f, -10), 1.2f * s, Fade(Color{40, 26, 12, 255}, 0.55f)); // canvas folds
        MQuad(S.Chest(-24, -26), S.Chest(24, -26), S.Chest(19, -8), S.Chest(-19, -8), brass, METAL);     // the corselet
        for (int k = 0; k < 7; k++) MBall(L2(S.Chest(-21, -10), S.Chest(21, -10), k / 6.0f), 1.3f * s, Tone(brass, -0.35f), METAL); // its bolts
        MBall(S.Chest(-22, -22), 3.4f * s, Tone(brass, -0.1f), METAL);                                   // shoulder bolts
        MBall(S.Chest(22, -22), 3.4f * s, Tone(brass, -0.1f), METAL);
        MQuad(S.Hips(-17, -7), S.Hips(17, -7), S.Hips(16, 2), S.Hips(-16, 2), Tone(wood, -0.3f), WET);   // the weight belt
        for (int k = 0; k < 4; k++) MQuad(S.Hips(-14 + k * 8.0f, -6), S.Hips(-9 + k * 8.0f, -6), S.Hips(-9 + k * 8.0f, 1), S.Hips(-14 + k * 8.0f, 1), lead, METAL);
        Vector2 coil = S.Hips(-8, 6);                                                                     // the coiled line
        for (int k = 0; k < 3; k++) DrawRing(coil, (4.5f + k * 1.6f) * s, (5.5f + k * 1.6f) * s, 0, 360, 16, Tone(rope, -0.1f * k));
    });
    parts.Add(0.2f, [&] { in.chains[1].Draw(s); });
    // the head: the porthole helmet on an expedition, a knit cap and a beard aboard
    parts.Add(0.5f, [&] {
        if (gDiveGear) {
            Vector2 hc = S.Head(1, 0);
            MBall(hc, 17 * s, brass, METAL);
            MLimb(S.Chest(-12, -25), S.Chest(12, -25), 5 * s, 5 * s, Tone(brass, -0.25f), METAL);        // the neck ring
            Vector2 port = S.Head(8, 0);
            MBall(port, 8.4f * s, Tone(brass, -0.3f), METAL);
            MBall(port, 6.6f * s, Color{40, 58, 60, 255}, WET);                                          // the faceplate glass
            DrawCircleV(Off(port, -2, -2.5f, s, f), 1.6f * s, Fade(WHITE, 0.7f));
            for (int k = 0; k < 4; k++) { float a = k * PI / 2 + PI / 4; DrawLineEx({port.x + cosf(a) * 3.5f * s, port.y + sinf(a) * 3.5f * s}, {port.x + cosf(a) * 6.4f * s, port.y + sinf(a) * 6.4f * s}, 1.2f * s, Tone(brass, -0.4f)); } // the grille
            MBall(S.Head(-2, -12), 4.2f * s, Tone(brass, -0.15f), METAL);                                  // the top port
            MBall(S.Head(-8, 5), 3.4f * s, Tone(brass, -0.2f), METAL);                                     // the side port
            Glow(S.Head(4, -10), 18 * s, Fade(Color{255, 230, 170, 255}, 0.18f));                          // the helmet lamp
            MBall(S.Head(4, -12), 2.6f * s, Color{255, 236, 190, 255}, GLOW);
        } else {
            MLimb(S.Chest(1, -24), S.Head(0, 8), 7.6f * s, 7.0f * s, Tone(skin, -0.08f), SKIN);
            MBall(S.p[HEAD], 13.2f * s, skin, SKIN);
            MQuad(S.Head(-5, 1), S.Head(11, 0), S.Head(10, 12), S.Head(-2, 13.5f), skin, SKIN);
            MLimb(S.Head(10, -1), S.Head(14, 4.5f), 2.2f * s, 3.2f * s, Tone(skin, 0.05f), SKIN);          // a broken nose
            Color beard{70, 50, 36, 255};
            MLimb(S.Head(-4, 5), S.Head(7, 13), 4.6f * s, 6.0f * s, beard, CLOTH);
            MLimb(S.Head(7, 13), S.Head(12, 8), 5.2f * s, 3.6f * s, beard, CLOTH);
            DrawEyes(in.face, S.Head(7, -1.5f), 5.2f, 1.8f, s, f, Tone(skin, -0.35f), Color{50, 60, 70, 255});
            DrawLineEx(S.Head(3, -4.5f), S.Head(12.5f, -4.5f), 2.0f * s, beard);                           // one heavy brow
            MQuad(S.Head(-13, -15), S.Head(12, -16), S.Head(13, -5), S.Head(-13, -4), Color{60, 70, 84, 255}, CLOTH); // the knit cap
            for (int k = 0; k < 6; k++) DrawLineEx(S.Head(-11 + k * 4.5f, -15), S.Head(-11 + k * 4.5f, -6), 0.8f * s, Fade(Color{20, 24, 30, 255}, 0.6f));
            MLimb(S.Head(-13, -6), S.Head(13, -6), 2.6f * s, 2.6f * s, Color{74, 86, 102, 255}, CLOTH);   // its rolled brim
        }
    });
    // the harpoon arm, in front
    parts.Add(1.0f, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 10.2f * s, 9.0f * s, canvas, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 9.0f * s, 7.8f * s, canvas, CLOTH);
        MLimb(S.Along(EL_F, WR_F, 0.7f, 0), S.p[WR_F], 8.8f * s, 8.2f * s, rubber, WET);
        float a = S.a[PROP];
        Vector2 dir{cosf(a), sinf(a)};
        auto W = [&](float along) { return Vector2{S.p[WR_F].x + dir.x * along * s, S.p[WR_F].y + dir.y * along * s}; };
        MLimb(W(-26), W(40), 2.4f * s, 2.2f * s, wood, CLOTH);                                           // the long shaft
        MLimb(W(38), W(46), 3.4f * s, 3.4f * s, brass, METAL);                                           // the collar
        MLimb(W(46), W(62), 4.6f * s, 0.6f * s, steel, METAL);                                           // the barbed head
        Vector2 n{-dir.y, dir.x};
        for (int sd = -1; sd <= 1; sd += 2) DrawTri(W(50), {W(50).x + n.x * 6 * s * sd, W(50).y + n.y * 6 * s * sd}, W(55), Tone(steel, -0.2f));
        MBall(S.p[WR_F], 6.8f * s, Tone(canvas, -0.35f), CLOTH);                                          // the mitt
        MBall(S.Chest(22, -22), 3.6f * s, Tone(brass, 0.05f), METAL);                                    // the near shoulder bolt, over the arm
    });
    parts.Draw();
}

// ============================================================================ THE MECHANIC
// The crew's wall: the biggest silhouette, orange overalls under a leather apron with riveted patches, bare
// forearms, a riveted hull-plate strapped to his back forearm as a shield, a huge pneumatic wrench fed by a hose
// from the tank on his back, welding goggles pushed up on his brow, an oily rag swinging from his back pocket.
void DrawRigMechanic(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
    Color skin = skins[seed % 4], orange{206, 104, 34, 255}, orangeDk = Tone(orange, -0.28f), leather{104, 70, 44, 255}, iron{98, 100, 104, 255};
    Color steel{176, 180, 188, 255}, brass = Pal::Brass, boots{44, 36, 30, 255}, rag{174, 90, 60, 255}, rubber{40, 36, 34, 255}, hair{50, 36, 28, 255};

    Build b;
    b.thigh = 40; b.shin = 38; b.upper = 29; b.fore = 28; b.spine = 27; b.chest = 29; b.neck = 9; b.head = 12;
    b.shoulderW = 23; b.hipW = 9; b.stanceF = 20; b.stanceB = -19;

    // the rest pose: a wall. Low, square, the plate raised before him, the wrench hefted on his shoulder
    RPose P;
    P[C_HIPY] = 8; P[C_LEAN] = 0.12f; P[C_CHEST] = 0.04f; P[C_HEAD] = -0.05f;
    P[C_HBX] = 40; P[C_HBY] = 8;                        // the shield arm, raised out in front
    P[C_HFX] = 16; P[C_HFY] = 42; P[C_WEAPON] = 58;     // the wrench hanging low and forward, its head near the deck
    P += GetClip(CL_IDLE).Sample(t + h.id * 1.7f);
    P += GetClip(CL_BREATHE).Sample(t + h.id);
    float stressW = std::clamp(pose.tremble * 1.4f, 0.0f, 1.0f);
    if (stressW > 0) P += Scaled(GetClip(CL_STRESSED).Sample(t), stressW);
    float act = std::clamp(std::max(pose.reach, pose.raise), 0.0f, 1.0f);
    P[C_WEAPON] *= 1 - act; P[C_HFX] *= 1 - act; P[C_HFY] *= 1 - act;
    P += FromPose(pose, walk, t, 0);
    P[C_WEAPON] += -90 * std::clamp(pose.raise, 0.0f, 1.0f) + pose.weaponTilt;
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);
    Solved S = SolveHumanoid(b, P, ft, s, f);

    in.face.look = {1.0f, stressW > 0.4f ? sinf(t * 1.3f) * 0.6f : 0.0f};
    in.face.mouth = pose.headDown < -0.3f ? 2 : (pose.reach > 0.5f || pose.raise > 0.7f) ? 1 : 0;

    float a = S.a[PROP];
    Vector2 dir{cosf(a), sinf(a)};
    auto W = [&](float along) { return Vector2{S.p[WR_F].x + dir.x * along * s, S.p[WR_F].y + dir.y * along * s}; };

    // chains: the pneumatic hose from the tank to the wrench's grip, the rag
    bool jump = FollowWorld(in, s);
    Vector2 anchors[2] = {S.Chest(-20, -18), S.Hips(-16, 4)};
    if (jump || in.chains.size() != 2) {
        in.chains.assign(2, Chain{});
        in.chains[0].Init(anchors[0], 8, 8 * s, {0.3f * f, 1}); in.chains[0].col = rubber; in.chains[0].width0 = in.chains[0].width1 = 3.0f; in.chains[0].mat = WET; in.chains[0].stiff = 0.05f; in.chains[0].grav = 380;
        in.chains[1].Init(anchors[1], 4, 6 * s, {-0.2f * f, 1}); in.chains[1].col = rag; in.chains[1].width0 = 4.0f; in.chains[1].width1 = 2.8f; in.chains[1].stiff = 0.2f;
    }
    Vector2 cur = Current();
    in.chains[0].Step(anchors[0], {0.3f * f, 1}, in.dt, cur);
    in.chains[1].Step(anchors[1], {-0.2f * f, 1}, in.dt, cur);
    // the hose's far end is held at the wrench's grip: pull its tip there, and let the verlet sag carry the rest
    {
        Chain& c = in.chains[0];
        Vector2 grip = W(-8);
        for (size_t k = 1; k < c.p.size(); k++) {
            float u = k / (float)(c.p.size() - 1);
            Vector2 straight = L2(anchors[0], grip, u);
            straight.y += sinf(u * PI) * 22 * s;                                                          // it sags between them
            c.p[k] = L2(c.p[k], straight, 0.35f + 0.65f * u * u);
        }
    }

    Parts parts;
    // the tank cluster on his back
    parts.Add(-3.0f, [&] {
        for (int k = 0; k < 2; k++) {
            Vector2 a0 = S.Chest(-24 + k * 5.0f, -24 + k * 3.0f), a1 = S.Hips(-22 + k * 5.0f, -12);
            MLimb(a0, a1, 7.0f * s, 7.0f * s, k ? Tone(iron, 0.1f) : Color{150, 60, 40, 255}, METAL);
            MBall(a0, 7.0f * s, k ? Tone(iron, 0.15f) : Color{160, 66, 44, 255}, METAL);
        }
        MBall(S.Chest(-20, -30), 2.8f * s, brass, METAL);
    });
    parts.Add(-2.8f, [&] { in.chains[1].Draw(s); });
    auto leg = [&](int hip, int kn, int an, Color col) {
        MLimb(S.p[hip], S.p[kn], 12.4f * s, 10.8f * s, col, CLOTH);
        MLimb(S.p[kn], S.p[an], 10.8f * s, 9.0f * s, col, CLOTH);
        MQuad(S.Along(hip, kn, 0.75f, -9), S.Along(hip, kn, 0.75f, 9), S.Along(kn, an, 0.25f, 8), S.Along(kn, an, 0.25f, -8), leather, CLOTH); // a leather knee pad
        MLimb(S.Along(kn, an, 0.6f, 0), S.p[an], 10.0f * s, 9.6f * s, boots, WET);
        Vector2 heel = Off(S.p[an], -4, 2, s, f), toe = Off(S.p[an], 15, 4, s, f);
        MLimb(heel, toe, 9.0f * s, 8.2f * s, boots, WET);
        MBall(Off(toe, -2, -1, s, f), 4.2f * s, steel, METAL);                                           // a steel toecap
        DrawLineEx(Off(heel, -2, 7, s, f), Off(toe, 3, 6, s, f), 2.0f * s, Tone(boots, -0.6f));
    };
    parts.Add(-1.6f, [&] { leg(HIP_B, KN_B, AN_B, orangeDk); });
    parts.Add(-1.0f, [&] { leg(HIP_F, KN_F, AN_F, orange); });
    // the torso: overalls, the bib and straps, the leather apron with riveted patches, the tool belt
    parts.Add(0, [&] {
        MQuad(S.Chest(-25, -22), S.Chest(25, -22), S.Hips(19, 4), S.Hips(-19, 4), orange, CLOTH);
        MQuad(S.Chest(-12, -18), S.Chest(14, -18), S.Hips(15, 16), S.Hips(-12, 16), leather, CLOTH);      // the apron
        for (int k = 0; k < 2; k++) { // riveted patches of plate on the apron
            Vector2 c = S.Chest(-3 + k * 6.0f, -8 + k * 14.0f);
            MQuad(Off(c, -7, -5, s, f), Off(c, 7, -6, s, f), Off(c, 7, 5, s, f), Off(c, -7, 6, s, f), iron, METAL);
            for (int r = 0; r < 4; r++) MBall(Off(c, r % 2 ? 5.5f : -5.5f, r / 2 ? 4.2f : -4.2f, s, f), 1.0f * s, Tone(iron, 0.35f), METAL);
        }
        DrawLineEx(S.Chest(-16, -22), S.Chest(-12, -18), 2.4f * s, orangeDk);                            // the overall straps
        DrawLineEx(S.Chest(17, -22), S.Chest(14, -18), 2.4f * s, orangeDk);
        MQuad(S.Hips(-20, -6), S.Hips(20, -6), S.Hips(19, 2), S.Hips(-19, 2), Tone(leather, -0.3f), WET);  // the tool belt
        MLimb(S.Hips(4, -2), S.Hips(6, 12), 1.8f * s, 1.6f * s, steel, METAL);                            // a screwdriver
        MLimb(S.Hips(-6, -2), S.Hips(-4, 10), 2.8f * s, 2.0f * s, Tone(steel, -0.2f), METAL);             // pliers
        MBall(S.Hips(14, -2), 2.8f * s, brass, METAL);
        for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-20 + k * 14.0f, -14), S.Hips(-16 + k * 14.0f, -8), 1.3f * s, Fade(Color{60, 24, 6, 255}, 0.5f)); // folds
    });
    // the head: a thick neck, a heavy jaw, stubble, goggles pushed up on a shaved head
    parts.Add(0.5f, [&] {
        MLimb(S.Chest(2, -24), S.Head(0, 8), 10.0f * s, 9.0f * s, Tone(skin, -0.1f), SKIN);               // a bull neck
        MBall(S.p[HEAD], 12.8f * s, skin, SKIN);
        MQuad(S.Head(-6, 0), S.Head(12, -1), S.Head(12, 13), S.Head(-3, 14.5f), skin, SKIN);              // a heavy, square jaw
        for (int k = 0; k < 14; k++) DrawCircleV(S.Head(1 + (k % 7) * 1.7f, 8 + (k / 7) * 2.6f), 0.5f * s, Fade(hair, 0.7f)); // stubble
        MLimb(S.Head(10.5f, -1), S.Head(14, 4.5f), 2.4f * s, 3.4f * s, Tone(skin, 0.05f), SKIN);           // a flat nose
        MBall(S.Head(-9, 1), 3.2f * s, Tone(skin, -0.1f), SKIN);                                         // the ear
        DrawEyes(in.face, S.Head(7.5f, -1.5f), 5.0f, 1.7f, s, f, Tone(skin, -0.4f), Color{70, 60, 50, 255});
        DrawLineEx(S.Head(3, -4.2f), S.Head(13, -4.8f), 2.2f * s, hair);                                  // one scowling brow
        DrawMouth(in.face, S.Head(9, 9), 5.0f, s, f, Color{120, 70, 60, 255});
        MLimb(S.Head(-11, -8), S.Head(11, -9), 2.6f * s, 2.6f * s, Color{60, 50, 44, 255}, CLOTH);         // the goggle strap
        for (int k = 0; k < 2; k++) { Vector2 g = S.Head(k ? 8.0f : 1.0f, -10); MBall(g, 3.8f * s, brass, METAL); MBall(g, 2.6f * s, Color{60, 110, 90, 255}, WET); }
    });
    // the shield arm (the back arm), in front of everything: a riveted hull-plate
    parts.Add(1.2f, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 11.6f * s, 10.6f * s, orangeDk, CLOTH);
        MLimb(S.p[EL_B], S.p[WR_B], 10.0f * s, 8.6f * s, Tone(skin, -0.1f), SKIN);
        Vector2 c = Off(S.p[WR_B], 4, 0, s, f);   // held out on the fist
        Vector2 q[4] = {Off(c, -4, -22, s, f), Off(c, 14, -19, s, f), Off(c, 14, 19, s, f), Off(c, -4, 22, s, f)};
        Color plate{150, 146, 138, 255};
        MQuad(q[0], q[1], q[2], q[3], plate, METAL);
        for (int k = 0; k < 5; k++) { MBall(L2(q[0], q[3], 0.1f + k * 0.2f), 1.5f * s, Tone(plate, 0.3f), METAL); MBall(L2(q[1], q[2], 0.1f + k * 0.2f), 1.5f * s, Tone(plate, 0.3f), METAL); }
        DrawLineEx(L2(q[0], q[1], 0.5f), L2(q[3], q[2], 0.5f), 1.2f * s, Fade(Color{20, 20, 24, 255}, 0.6f)); // a seam
        DrawLineEx(L2(q[0], q[3], 0.3f), L2(q[1], q[2], 0.45f), 2.0f * s, Fade(Color{150, 70, 40, 255}, 0.7f)); // a rust streak
    });
    parts.Add(0.9f, [&] { in.chains[0].Draw(s); });
    // the wrench arm, frontmost
    parts.Add(1.0f, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 12.0f * s, 11.0f * s, orange, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 10.6f * s, 9.0f * s, skin, SKIN);                                   // a bare, heavy forearm
        MLimb(S.Along(EL_F, WR_F, 0.2f, 0), S.Along(EL_F, WR_F, 0.28f, 0), 11.4f * s, 11.4f * s, orange, CLOTH); // the rolled sleeve
        MLimb(W(-12), W(44), 4.6f * s, 4.2f * s, steel, METAL);                                          // the wrench's long handle
        MLimb(W(-12), W(-4), 5.4f * s, 5.4f * s, rubber, WET);                                           // its grip
        Vector2 hd = W(50), n{-dir.y, dir.x};                                                             // the head: open jaws
        MBall(hd, 11 * s, Tone(steel, -0.1f), METAL);
        MLimb(hd, {hd.x + dir.x * 10 * s + n.x * 7 * s, hd.y + dir.y * 10 * s + n.y * 7 * s}, 6.0f * s, 5.0f * s, steel, METAL);
        MLimb(hd, {hd.x + dir.x * 10 * s - n.x * 7 * s, hd.y + dir.y * 10 * s - n.y * 7 * s}, 6.0f * s, 5.0f * s, steel, METAL);
        MBall(W(34), 4.0f * s, brass, METAL);                                                            // the adjusting screw
        MBall(S.p[WR_F], 8.0f * s, Color{66, 60, 56, 255}, CLOTH);                                        // a work glove
    });
    parts.Draw();
}

// ============================================================================ shared hero helpers
namespace {
const Color SKINS[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
// Layer a hero's rest pose with the shared clips and the combat Pose: idle, breathing, fear, the guard giving
// way to the strike, the combat pose, the raise, a reaction.
RPose LayerHero(RPose P, const Hero& h, Instance& in, const Pose& pose, float walk, float t, float raiseDeg, float* stressOut) {
    P += GetClip(CL_IDLE).Sample(t + h.id * 1.7f);
    P += GetClip(CL_BREATHE).Sample(t + h.id);
    float stressW = std::clamp(pose.tremble * 1.4f, 0.0f, 1.0f);
    if (stressW > 0) P += Scaled(GetClip(CL_STRESSED).Sample(t), stressW);
    P[C_WEAPON] *= 1 - std::clamp(std::max(pose.reach, pose.raise), 0.0f, 1.0f);
    P += FromPose(pose, walk, t, 0);
    P[C_WEAPON] += raiseDeg * std::clamp(pose.raise, 0.0f, 1.0f) + pose.weaponTilt;
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);
    in.face.look = {1.0f, stressW > 0.4f ? sinf(t * 1.3f) * 0.6f : 0.05f};
    in.face.mouth = pose.headDown < -0.3f ? 2 : (pose.reach > 0.5f || pose.raise > 0.7f) ? 1 : 0;
    if (stressOut) *stressOut = stressW;
    return P;
}
// a leg: thigh and shin in cloth, a boot with a sole; `bootFrom` is where the boot starts up the shin (0 = knee)
void StdLeg(const Solved& S, int hip, int kn, int an, float wt, float ws, Color col, Color boot, float bootFrom, float s, float f) {
    MLimb(S.p[hip], S.p[kn], wt * s, (wt * 0.85f) * s, col, CLOTH);
    MLimb(S.p[kn], S.p[an], ws * s, (ws * 0.8f) * s, col, CLOTH);
    MLimb(S.Along(kn, an, bootFrom, 0), S.p[an], ws * 1.02f * s, ws * 0.9f * s, boot, WET);
    Vector2 heel = Off(S.p[an], -3, 1, s, f), toe = Off(S.p[an], 13, 3, s, f);
    MLimb(heel, toe, ws * 0.86f * s, ws * 0.74f * s, boot, WET);
    DrawLineEx(Off(heel, -2, ws * 0.8f, s, f), Off(toe, 3, ws * 0.65f, s, f), 1.6f * s, Tone(boot, -0.6f));
}
}  // namespace

// ============================================================================ THE WHALER
// A long gun of the whaling fleets: a sou'wester over a weathered face, a heavy blue oilskin coat whose skirts
// swing, a coil of line over the shoulder, a bandolier of brass shells, the shoulder-fired harpoon gun levelled
// from the hip at the far ranks, a flensing knife at the belt.
void DrawRigWhaler(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    Color skin = SKINS[seed % 4], coat{84, 108, 138, 255}, coatDk = Tone(coat, -0.3f), legs{64, 70, 84, 255}, boots{92, 62, 36, 255};
    Color hat{150, 118, 60, 255}, leather{110, 76, 44, 255}, rope{168, 144, 104, 255}, brass = Pal::Brass, steel{180, 184, 190, 255}, wood{96, 64, 40, 255};

    Build b;
    b.thigh = 41; b.shin = 40; b.upper = 27; b.fore = 26; b.spine = 26; b.chest = 25; b.neck = 7; b.head = 12;
    b.shoulderW = 17; b.hipW = 7; b.stanceF = 18; b.stanceB = -17;
    RPose P;
    P[C_HIPY] = 5; P[C_LEAN] = 0.08f; P[C_HEAD] = 0.04f;
    P[C_HFX] = 16; P[C_HFY] = 26; P[C_WEAPON] = -6;     // the gun levelled from the hip
    P[C_HBX] = 30; P[C_HBY] = 20;                       // the other hand under the barrel
    P = LayerHero(P, h, in, pose, walk, t, -40, nullptr);
    Solved S = SolveHumanoid(b, P, ft, s, f);

    bool jump = FollowWorld(in, s);
    Vector2 rest{-0.16f * f, 1};
    float rl = sqrtf(rest.x * rest.x + 1); rest = {rest.x / rl, 1 / rl};
    Vector2 anchors[4] = {S.Hips(-15, 2), S.Hips(-2, 4), S.Hips(13, 3), S.Head(-12, -4)};
    if (jump || in.chains.size() != 4) {
        in.chains.assign(4, Chain{});
        for (int i = 0; i < 3; i++) { in.chains[i].Init(anchors[i], 6, 9.2f * s, rest); in.chains[i].stiff = 0.35f; in.chains[i].grav = 420; }
        in.chains[3].Init(anchors[3], 4, 5 * s, {-0.6f * f, 1}); in.chains[3].col = Tone(hat, -0.2f); in.chains[3].width0 = 5; in.chains[3].width1 = 3; in.chains[3].stiff = 0.3f; // the hat's long back brim
    }
    Vector2 cur = Current();
    for (int i = 0; i < 4; i++) in.chains[i].Step(anchors[i], i < 3 ? rest : Vector2{-0.6f * f, 1}, in.dt, cur);

    float a = S.a[PROP];
    Vector2 dir{cosf(a), sinf(a)}, n{-dir.y, dir.x};
    auto W = [&](float along, float side = 0) { return Vector2{S.p[WR_F].x + dir.x * along * s + n.x * side * s, S.p[WR_F].y + dir.y * along * s + n.y * side * s}; };

    Parts parts;
    parts.Add(-1.6f, [&] { StdLeg(S, HIP_B, KN_B, AN_B, 9.6f, 8.2f, Tone(legs, -0.25f), boots, 0.4f, s, f); });
    parts.Add(-1.3f, [&] { Panel(in.chains[0], in.chains[1], coatDk, -0.05f); });
    parts.Add(-1.0f, [&] { StdLeg(S, HIP_F, KN_F, AN_F, 9.6f, 8.2f, legs, boots, 0.4f, s, f); });
    parts.Add(-0.8f, [&] { Panel(in.chains[1], in.chains[2], coat, 0.02f); DrawLineEx(in.chains[1].Tip(), in.chains[2].Tip(), 1.6f * s, Tone(coat, -0.45f)); });
    parts.Add(0, [&] {
        MQuad(S.Chest(-19, -22), S.Chest(20, -22), S.Hips(15, 2), S.Hips(-15, 2), coat, CLOTH);
        for (int k = 0; k < 4; k++) MBall(S.Chest(10, -15 + k * 8.0f), 1.8f * s, Tone(brass, -0.2f), METAL);   // toggles
        MQuad(S.Hips(-16, -5), S.Hips(16, -5), S.Hips(15, 1), S.Hips(-15, 1), leather, WET);
        MLimb(S.Hips(8, -2), S.Hips(12, 14), 2.6f * s, 1.0f * s, steel, METAL);                                // the flensing knife
        MLimb(S.Chest(15, -20), S.Hips(-12, -4), 2.6f * s, 2.6f * s, leather, CLOTH);                          // the shell bandolier
        for (int k = 0; k < 5; k++) MLimb(L2(S.Chest(12, -17), S.Hips(-9, -6), 0.1f + k * 0.2f), L2(S.Chest(12, -13), S.Hips(-9, -2), 0.1f + k * 0.2f), 1.8f * s, 1.8f * s, brass, METAL);
        Vector2 coil = S.Chest(-12, -14);                                                                      // the coil of line on the shoulder
        for (int k = 0; k < 3; k++) DrawRing(coil, (6 + k * 1.8f) * s, (7.2f + k * 1.8f) * s, 0, 360, 18, Tone(rope, -0.12f * k));
        for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-14 + k * 12.0f, -12), S.Hips(-12 + k * 12.0f, -8), 1.2f * s, Fade(Color{10, 14, 24, 255}, 0.5f));
    });
    // the head: a weathered, bearded face under a sou'wester, its long back brim swinging
    parts.Add(0.3f, [&] { in.chains[3].Draw(s); });
    parts.Add(0.5f, [&] {
        MLimb(S.Chest(1, -24), S.Head(0, 8), 7.0f * s, 6.6f * s, Tone(skin, -0.08f), SKIN);
        MLimb(S.Chest(-8, -22), S.Chest(9, -22), 4.0f * s, 4.0f * s, Color{150, 40, 38, 255}, CLOTH);          // a red neckerchief
        MBall(S.p[HEAD], 12.8f * s, skin, SKIN);
        MQuad(S.Head(-5, 1), S.Head(11, 0), S.Head(10, 12), S.Head(-2, 13), skin, SKIN);
        MLimb(S.Head(10, -1), S.Head(13.5f, 4.5f), 2.0f * s, 3.0f * s, Tone(skin, 0.05f), SKIN);
        Color beard{120, 96, 70, 255};
        MLimb(S.Head(-3, 6), S.Head(8, 13), 3.6f * s, 4.4f * s, beard, CLOTH);                                // a chin beard
        DrawEyes(in.face, S.Head(7.5f, -0.5f), 5.0f, 1.7f, s, f, Tone(skin, -0.4f), Color{70, 90, 110, 255});
        DrawLineEx(S.Head(4, -3.4f), S.Head(12, -3.4f), 1.6f * s, beard);                                     // a squint
        MQuad(S.Head(-11, -16), S.Head(10, -17), S.Head(12, -5), S.Head(-12, -4), hat, CLOTH);                // the sou'wester's crown
        MLimb(S.Head(-14, -4), S.Head(18, -6), 2.4f * s, 1.6f * s, Tone(hat, -0.1f), CLOTH);                   // its front brim, turned up
    });
    // the harpoon gun, and the arms that hold it
    parts.Add(0.8f, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 8.4f * s, 7.4f * s, Tone(coat, -0.15f), CLOTH);
        MLimb(S.p[EL_B], S.p[WR_B], 7.4f * s, 6.2f * s, Tone(coat, -0.15f), CLOTH);
        MBall(S.p[WR_B], 5.6f * s, Color{110, 76, 44, 255}, CLOTH);
    });
    parts.Add(1.0f, [&] {
        MLimb(W(-26, 3), W(-4, 1), 5.6f * s, 4.4f * s, wood, CLOTH);                                           // the stock
        MLimb(W(-4, 0), W(50, 0), 3.8f * s, 3.4f * s, Tone(steel, -0.25f), METAL);                               // the barrel
        MLimb(W(4, 0), W(14, 0), 5.4f * s, 5.4f * s, brass, METAL);                                              // the breech
        MLimb(W(50, 0), W(62, 0), 3.0f * s, 0.8f * s, steel, METAL);                                             // the harpoon's head out of the muzzle
        for (int sd = -1; sd <= 1; sd += 2) DrawTri(W(54, 0), W(52, 5.0f * sd), W(58, 0), Tone(steel, -0.2f));
        MLimb(S.p[SH_F], S.p[EL_F], 8.8f * s, 7.6f * s, coat, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 7.6f * s, 6.4f * s, coat, CLOTH);
        MBall(S.p[WR_F], 5.8f * s, Color{110, 76, 44, 255}, CLOTH);                                              // a leather glove on the trigger
    });
    parts.Draw();
}

// ============================================================================ THE STOWAWAY
// Thin, hunched and swaying: a patched jacket too big for him, a long red scarf trailing, a knit cap, a green
// bottle in one hand and a broken-necked one as a shiv in the other, rags tied round his boots. He weaves on
// his feet (a slow drunken sway over the idle) and never quite stands straight.
void DrawRigStowaway(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    const float f = right ? 1.0f : -1.0f;
    Instance& in = Get(h.id);
    Tick(in, t);
    int seed = h.id * 7919 + 13;
    Color skin = SKINS[seed % 4], jacket{124, 94, 62, 255}, legs{84, 72, 52, 255}, boots{58, 48, 38, 255}, red{176, 48, 46, 255};
    Color bottle{60, 140, 80, 255}, patch{150, 120, 70, 255}, cap{70, 60, 50, 255}, rag{150, 140, 118, 255}, hair{60, 44, 30, 255};

    Build b;
    b.thigh = 39; b.shin = 39; b.upper = 26; b.fore = 25; b.spine = 25; b.chest = 22; b.neck = 7; b.head = 12;
    b.shoulderW = 13.5f; b.hipW = 5.5f; b.stanceF = 14; b.stanceB = -16;
    RPose P;
    float sway = sinf(t * 1.1f + h.id) * 0.5f + sinf(t * 0.43f + h.id * 2) * 0.5f;          // the drunken weave
    P[C_HIPY] = 6; P[C_HIPX] = sway * 5; P[C_LEAN] = 0.22f + sway * 0.06f; P[C_CHEST] = 0.12f; P[C_HEAD] = 0.12f + sway * 0.08f;
    P[C_HFX] = 14; P[C_HFY] = 30; P[C_WEAPON] = -30;    // the shiv, held low and forward
    P[C_HBX] = 12; P[C_HBY] = 22 + sway * 3;            // the bottle, held against his chest
    P = LayerHero(P, h, in, pose, walk, t, -60, nullptr);
    Solved S = SolveHumanoid(b, P, ft, s, f);

    bool jump = FollowWorld(in, s);
    Vector2 anchors[2] = {S.Chest(-6, -24), S.Chest(-2, -22)};
    if (jump || in.chains.size() != 2) {
        in.chains.assign(2, Chain{});
        in.chains[0].Init(anchors[0], 7, 6.5f * s, {-0.5f * f, 1}); in.chains[0].col = red; in.chains[0].width0 = 5; in.chains[0].width1 = 3.2f; in.chains[0].stiff = 0.15f; in.chains[0].grav = 300;
        in.chains[1].Init(anchors[1], 4, 6 * s, {0.1f * f, 1}); in.chains[1].col = Tone(red, -0.15f); in.chains[1].width0 = 4.4f; in.chains[1].width1 = 3.0f; in.chains[1].stiff = 0.25f;
    }
    Vector2 cur = Current();
    in.chains[0].Step(anchors[0], {-0.5f * f, 1}, in.dt, cur);
    in.chains[1].Step(anchors[1], {0.1f * f, 1}, in.dt, cur);

    Parts parts;
    parts.Add(-2.5f, [&] { in.chains[0].Draw(s); });
    // the bottle arm, clutched to his chest (in front of the jacket)
    parts.Add(0.7f, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 8.6f * s, 7.6f * s, Tone(jacket, -0.2f), CLOTH);                           // sleeves too big for him
        MLimb(S.p[EL_B], S.p[WR_B], 7.6f * s, 6.8f * s, Tone(jacket, -0.2f), CLOTH);
        Vector2 bt = Off(S.p[WR_B], 2, -4, s, f);
        MLimb(bt, Off(bt, 1, -16, s, f), 4.6f * s, 4.6f * s, bottle, WET);
        MLimb(Off(bt, 1, -16, s, f), Off(bt, 1, -22, s, f), 1.8f * s, 1.6f * s, Tone(bottle, -0.1f), WET);
        MBall(Off(bt, 1, -23, s, f), 1.6f * s, Color{150, 110, 70, 255}, CLOTH);                                // the cork
        MBall(S.p[WR_B], 4.6f * s, skin, SKIN);
    });
    auto leg = [&](int hip, int kn, int an, Color col) {
        StdLeg(S, hip, kn, an, 8.0f, 7.0f, col, boots, 0.55f, s, f);
        for (int k = 0; k < 3; k++) DrawLineEx(S.Along(kn, an, 0.6f + k * 0.12f, -5), S.Along(kn, an, 0.64f + k * 0.12f, 5), 1.8f * s, rag); // rags bound round the boot
    };
    parts.Add(-1.6f, [&] { leg(HIP_B, KN_B, AN_B, Tone(legs, -0.25f)); });
    parts.Add(-1.0f, [&] { leg(HIP_F, KN_F, AN_F, legs); });
    parts.Add(0, [&] {
        MQuad(S.Chest(-16, -21), S.Chest(17, -21), S.Hips(15, 12), S.Hips(-14, 12), jacket, CLOTH);           // a jacket down past his hips
        MQuad(S.Chest(-10, -6), S.Chest(-1, -7), S.Chest(-1, 3), S.Chest(-10, 3), patch, CLOTH);               // patches
        MQuad(S.Hips(3, -2), S.Hips(12, -3), S.Hips(12, 6), S.Hips(3, 7), Tone(patch, -0.15f), CLOTH);
        for (int k = 0; k < 6; k++) DrawLineEx(S.Chest(-10 + (k % 3) * 4.5f, k < 3 ? -7.5f : 3.5f), S.Chest(-9 + (k % 3) * 4.5f, k < 3 ? -5.5f : 1.5f), 0.8f * s, Color{40, 30, 20, 255}); // stitches
        MLimb(S.Hips(-15, 0), S.Hips(15, 1), 2.4f * s, 2.4f * s, Color{120, 100, 70, 255}, CLOTH);              // a rope belt
        for (int k = 0; k < 3; k++) DrawLineEx(S.Chest(-12 + k * 10.0f, -14), S.Hips(-11 + k * 10.0f, 8), 1.1f * s, Fade(Color{30, 20, 10, 255}, 0.5f));
        MLimb(S.Chest(12, -16), S.Hips(-6, 2), 2.0f * s, 2.0f * s, Color{70, 50, 34, 255}, CLOTH);             // a strap
        MBall(S.Hips(-8, 6), 5.4f * s, Color{90, 66, 44, 255}, CLOTH);                                          // a stolen pouch
    });
    // the head: a thin face, stubble, a red nose, a cap pulled down, the scarf wound at his throat
    parts.Add(0.5f, [&] {
        MLimb(S.Chest(1, -23), S.Head(0, 8), 5.2f * s, 4.8f * s, Tone(skin, -0.08f), SKIN);
        MLimb(S.Chest(-8, -22), S.Chest(8, -21), 5.0f * s, 5.0f * s, red, CLOTH);                              // the scarf, wound round
        MBall(S.p[HEAD], 12.0f * s, skin, SKIN);
        MQuad(S.Head(-4, 1), S.Head(10, 0), S.Head(8.5f, 12), S.Head(-1, 12.5f), skin, SKIN);
        MLimb(S.Head(9.5f, 0), S.Head(13, 4.5f), 2.0f * s, 3.2f * s, Color{200, 110, 96, 255}, SKIN);          // a red nose
        for (int k = 0; k < 10; k++) DrawCircleV(S.Head(1 + (k % 5) * 2.0f, 8 + (k / 5) * 2.2f), 0.5f * s, Fade(hair, 0.7f));
        DrawEyes(in.face, S.Head(7, -0.5f), 4.8f, 1.8f, s, f, Tone(skin, -0.35f), Color{90, 80, 60, 255});
        DrawLineEx(S.Head(4, -2.2f), S.Head(11, -2.0f), 1.4f * s, Tone(skin, -0.4f));                          // heavy lids: half shut
        DrawMouth(in.face, S.Head(8, 8.5f), 4.0f, s, f, Color{130, 66, 60, 255});
        MBall(S.Head(-3, -7), 12.5f * s, cap, CLOTH);                                                         // a slouched knit cap pulled low
        MBall(S.Head(-11, -12), 7.0f * s, Tone(cap, -0.1f), CLOTH);                                           // slumped to the back
        MLimb(S.Head(-12, -3), S.Head(11, -4), 2.6f * s, 2.6f * s, Tone(cap, 0.15f), CLOTH);
        MBall(S.Head(-16, -15), 3.4f * s, red, CLOTH);                                                        // its bobble
        for (int k = 0; k < 3; k++) DrawLineEx(S.Head(-12, -1 + k * 3.0f), S.Head(-10, -2 + k * 3.0f), 1.4f * s, hair); // lank hair out the back
    });
    parts.Add(0.6f, [&] { in.chains[1].Draw(s); });
    // the shiv arm, in front: a broken bottle neck
    parts.Add(1.0f, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 8.8f * s, 7.8f * s, jacket, CLOTH);
        MLimb(S.p[EL_F], S.p[WR_F], 7.8f * s, 7.0f * s, jacket, CLOTH);
        float a = S.a[PROP];
        Vector2 dir{cosf(a), sinf(a)};
        auto W = [&](float along) { return Vector2{S.p[WR_F].x + dir.x * along * s, S.p[WR_F].y + dir.y * along * s}; };
        MLimb(W(-4), W(6), 2.4f * s, 2.8f * s, bottle, WET);                                                  // the neck in his fist
        Vector2 n{-dir.y, dir.x};
        Vector2 base = W(6);
        DrawTri({base.x + n.x * 4 * s, base.y + n.y * 4 * s}, {base.x - n.x * 4 * s, base.y - n.y * 4 * s}, W(20), Fade(Tone(bottle, 0.2f), 0.9f)); // jagged glass
        DrawTri({base.x + n.x * 4 * s, base.y + n.y * 4 * s}, W(14), {W(12).x + n.x * 6 * s, W(12).y + n.y * 6 * s}, Fade(Tone(bottle, 0.1f), 0.9f));
        MBall(S.p[WR_F], 4.8f * s, skin, SKIN);
    });
    parts.Draw();
}

// ============================================================================ THE LOST ONE CULTIST
// Hooded, hovering over a turning rune circle; broken manacle chains hang from both wrists and a censer swings
// from its cord (the signature idle: everything that hangs from it sways); robe rags trail below the hem; one
// hand raises an orb of void, the other holds a book. Nothing under the hood but two eyes.
void DrawRigCultist(const Enemy& e, Rectangle r, float t) {
    const float f = -1.0f;   // enemies face the party, on the left
    float s = r.height / 150.0f;
    Vector2 ft{r.x + r.width / 2, r.y + r.height};
    Instance& in = Get(1000000 + e.uid);
    Tick(in, t);
    const Color robe{62, 46, 92, 255}, robeDk{36, 26, 58, 255}, trim{150, 102, 214, 255}, glow{204, 134, 255, 255}, bone{224, 214, 192, 255}, coral{184, 92, 82, 255}, iron{70, 70, 78, 255};
    float pulse = 0.5f + 0.5f * sinf(t * 3 + e.uid);

    Build b;
    b.thigh = 30; b.shin = 30; b.upper = 25; b.fore = 24; b.spine = 30; b.chest = 26; b.neck = 5; b.head = 12; b.shoulderW = 15; b.hipW = 5;
    RPose P;
    P[C_ROOTY] = -34 + sinf(t * 1.8f + e.uid) * 4;    // it hovers
    P[C_HFX] = 26; P[C_HFY] = -26;                     // the orb, raised toward the party
    P[C_HBX] = 12; P[C_HBY] = 14;                      // the book, held close
    P[C_LEAN] = 0.1f;
    P += GetClip(CL_BREATHE).Sample(t * 0.8f + e.uid);
    P[C_HFY] += sinf(t * 1.2f + e.uid) * 4;           // the orb hand weaves slowly
    P[C_HEAD] += sinf(t * 0.7f + e.uid) * 0.08f;
    if (gActClip >= 0) { P += GetClip(gActClip).Sample(gActT); }
    if (in.reaction >= 0) P += GetClip(in.reaction).Sample(in.reactT);
    Solved S = SolveHumanoid(b, P, ft, s, f);
    in.face.look = {1, 0};

    // chains: two manacle chains, the censer on its cord, and six rags at the hem
    bool jump = FollowWorld(in, s);
    const int RAGS = 6;
    Vector2 hem[RAGS];
    for (int i = 0; i < RAGS; i++) hem[i] = S.Hips(-22 + i * 9.0f, 40 + (i % 2) * 3.0f);
    Vector2 anchors[3 + RAGS] = {S.Along(EL_F, WR_F, 0.9f, 0), S.Along(EL_B, WR_B, 0.9f, 0), S.Hips(-14, 6)};
    for (int i = 0; i < RAGS; i++) anchors[3 + i] = hem[i];
    if (jump || (int)in.chains.size() != 3 + RAGS) {
        in.chains.assign(3 + RAGS, Chain{});
        for (int i = 0; i < 2; i++) { in.chains[i].Init(anchors[i], 7, 5.5f * s, {0, 1}); in.chains[i].mat = METAL; in.chains[i].col = iron; in.chains[i].damp = 0.97f; in.chains[i].stiff = 0.02f; in.chains[i].width0 = in.chains[i].width1 = 1.8f; }
        in.chains[2].Init(anchors[2], 6, 5 * s, {0, 1}); in.chains[2].col = bone; in.chains[2].width0 = in.chains[2].width1 = 0.9f; in.chains[2].damp = 0.985f; in.chains[2].stiff = 0;
        for (int i = 0; i < RAGS; i++) { Chain& c = in.chains[3 + i]; c.Init(anchors[3 + i], 5, 6 * s, {0, 1}); c.col = i % 2 ? robe : robeDk; c.width0 = 3.6f; c.width1 = 1.2f; c.stiff = 0.3f; c.grav = 200; }
    }
    Vector2 cur = Current();
    for (int i = 0; i < 3 + RAGS; i++) {
        // the censer swings on its own rhythm: a slow push each way, so the idle is never still
        Vector2 push = i == 2 ? Vector2{cur.x + sinf(t * 1.6f + e.uid) * 900, cur.y} : cur;
        in.chains[i].Step(anchors[i], {0, 1}, in.dt, push);
    }

    Parts parts;
    // the rune circle on the ground, turning (it doesn't hover)
    parts.Add(-5, [&] {
        Vector2 g{ft.x, ft.y - 2 * s};
        DrawEllipse((int)g.x, (int)g.y, 46 * s, 10 * s, Fade(Color{6, 8, 12, 255}, 0.45f));
        DrawEllipseLines((int)g.x, (int)g.y, 44 * s, 9 * s, Fade(glow, 0.55f + 0.3f * pulse));
        DrawEllipseLines((int)g.x, (int)g.y, 32 * s, 6.5f * s, Fade(glow, 0.4f));
        for (int i = 0; i < 12; i++) { float a = t * 0.6f + i * PI / 6; DrawRectangle((int)(g.x + cosf(a) * 38 * s - 1.5f * s), (int)(g.y + sinf(a) * 8 * s - 1.5f * s), (int)(3 * s), (int)(3 * s), Fade(glow, 0.5f + 0.4f * sinf(t * 2 + i))); }
        Glow(g, 50 * s, Fade(glow, 0.08f + 0.05f * pulse));
    });
    // the book arm, behind
    parts.Add(-2, [&] {
        MLimb(S.p[SH_B], S.p[EL_B], 10 * s, 9 * s, Tone(robe, -0.15f), CLOTH);
        MLimb(S.p[EL_B], S.p[WR_B], 9 * s, 7 * s, Tone(robe, -0.1f), CLOTH);
        Vector2 bk = S.p[WR_B];
        MQuad(Off(bk, -4, -14, s, f), Off(bk, 5, -16, s, f), Off(bk, 5, -2, s, f), Off(bk, -4, 0, s, f), bone, CLOTH);
        MQuad(Off(bk, 5, -16, s, f), Off(bk, 14, -14, s, f), Off(bk, 14, 0, s, f), Off(bk, 5, -2, s, f), Tone(bone, -0.15f), CLOTH);
        for (int i = 0; i < 3; i++) DrawLineEx(Off(bk, -2, -12 + i * 4.0f, s, f), Off(bk, 3, -13 + i * 4.0f, s, f), 0.8f * s, Fade(glow, 0.9f));
        Glow(Off(bk, 5, -8, s, f), 22 * s, Fade(glow, 0.14f));
        MBall(bk, 3.2f * s, bone, SKIN);
    });
    parts.Add(-1.8f, [&] { in.chains[1].Draw(s); for (size_t k = 0; k < in.chains[1].p.size(); k += 2) DrawRing(in.chains[1].p[k], 1.2f * s, 2.4f * s, 0, 360, 8, Tone(iron, 0.2f)); });
    // the rags at the hem, then the robe over them
    parts.Add(-1, [&] { for (int i = 0; i < RAGS; i++) in.chains[3 + i].Draw(s); });
    parts.Add(0, [&] {
        Vector2 sl = S.Chest(-16, -22), sr = S.Chest(15, -22);
        MQuad(sl, sr, hem[RAGS - 1], hem[0], robe, CLOTH);                                                   // the robe
        for (int k = 0; k < 4; k++) DrawLineEx(L2(sl, sr, 0.15f + k * 0.22f), L2(hem[0], hem[RAGS - 1], 0.1f + k * 0.27f), 1.2f * s, Fade(Color{10, 6, 16, 255}, 0.55f)); // folds falling to the hem
        MQuad(S.Chest(-3, -22), S.Chest(6, -22), L2(hem[0], hem[RAGS - 1], 0.62f), L2(hem[0], hem[RAGS - 1], 0.44f), Tone(trim, -0.6f), CLOTH); // an embroidered panel
        for (int i = 0; i < 5; i++) DrawLineEx(S.Chest(-1 + i * 0.4f, -16 + i * 12.0f), S.Chest(6 + i * 0.6f, -12 + i * 12.0f), 1.0f * s, Fade(glow, 0.5f + 0.4f * sinf(t * 2 + i)));
        MLimb(S.Hips(-20, 4), S.Hips(18, 4), 2.6f * s, 2.6f * s, Color{120, 100, 70, 255}, CLOTH);                  // the cord at the waist
    });
    // the censer on its cord, swinging
    parts.Add(0.3f, [&] {
        in.chains[2].Draw(s);
        Vector2 c = in.chains[2].Tip();
        MBall(c, 4.6f * s, Tone(trim, -0.25f), METAL);
        for (int k = 0; k < 3; k++) DrawCircleV({c.x + sinf(t * 2 + k) * 3 * s, c.y - (6 + fmodf(t * 10 + k * 4, 14)) * s}, (1.6f + k * 0.4f) * s, Fade(Color{190, 170, 220, 255}, 0.25f)); // smoke
        Glow(c, 14 * s, Fade(glow, 0.12f + 0.08f * pulse));
    });
    // the hood: nothing under it but two eyes, and a crown of coral and bone
    parts.Add(0.5f, [&] {
        Vector2 hc = S.p[HEAD];
        MBall(hc, 16 * s, robe, CLOTH);
        DrawTri(S.Head(-12, -2), S.Head(12, -2), S.Head(-2, -28), robeDk);
        DrawTri(S.Head(-12, -2), S.Head(0, -2), S.Head(-2, -28), robe);
        MBall(S.Head(4, 2), 11 * s, Color{6, 8, 12, 255}, CLOTH);
        if (!in.face.Closed()) {
            DrawCircleV(S.Head(8, 1), 1.7f * s, Fade(glow, 0.95f)); DrawCircleV(S.Head(1, 1), 1.7f * s, Fade(glow, 0.95f));
            Glow(S.Head(4, 1), 18 * s, Fade(glow, 0.16f));
        } else {
            DrawLineEx(S.Head(6.5f, 1.2f), S.Head(9.5f, 1.2f), 0.8f * s, Fade(glow, 0.5f)); DrawLineEx(S.Head(-0.5f, 1.2f), S.Head(2.5f, 1.2f), 0.8f * s, Fade(glow, 0.5f));
        }
        for (int i = 0; i < 4; i++) DrawTri(S.Head(-10 + i * 5.0f, -12), S.Head(-6 + i * 5.0f, -12), S.Head(-8 + i * 5.0f, -22 - (i % 2) * 5.0f), i % 2 ? coral : bone);
    });
    // the orb arm, in front, with its manacle chain
    parts.Add(1, [&] {
        MLimb(S.p[SH_F], S.p[EL_F], 11 * s, 10 * s, robe, CLOTH);                                             // the wide sleeve
        MLimb(S.p[EL_F], S.p[WR_F], 10 * s, 6 * s, robe, CLOTH);
        for (int i = 0; i < 3; i++) DrawLineEx(S.Along(EL_F, WR_F, 0.2f + i * 0.2f, 3), S.Along(EL_F, WR_F, 0.3f + i * 0.2f, -6), 1.4f * s, Fade(glow, 0.5f));
        Vector2 hd = S.p[WR_F];
        MBall(hd, 3.6f * s, bone, SKIN);
        for (int i = 0; i < 3; i++) DrawLineEx(hd, Off(hd, 6 + i, -4 + i * 3.0f, s, f), 1.1f * s, bone);   // bony fingers
        Vector2 orb = Off(hd, 11, -9, s, f);
        MBall(orb, 9.5f * s, Tone(glow, -0.2f), GLOW);
        Glow(orb, 40 * s, Fade(glow, 0.22f + 0.14f * pulse));
        for (int i = 0; i < 3; i++) { float a = t * 2 + i * 2.1f; DrawCircleV({orb.x + cosf(a) * 14 * s, orb.y + sinf(a) * 5 * s}, 1.2f * s, Fade(WHITE, 0.8f)); }
        in.chains[0].Draw(s);
        for (size_t k = 0; k < in.chains[0].p.size(); k += 2) DrawRing(in.chains[0].p[k], 1.2f * s, 2.4f * s, 0, 360, 8, Tone(iron, 0.3f));
        MLimb(S.Along(EL_F, WR_F, 0.82f, 0), S.Along(EL_F, WR_F, 0.98f, 0), 5 * s, 5 * s, iron, METAL);   // the manacle
    });
    // shards orbiting it
    parts.Add(1.2f, [&] {
        for (int i = 0; i < 4; i++) {
            float a = t * 1.3f + i * 1.57f;
            Vector2 c = S.Chest(cosf(a) * 34, 6 + sinf(a) * 8);
            DrawTri(c, {c.x + 3 * s, c.y + 4 * s}, {c.x - 2 * s, c.y + 8 * s}, Fade(glow, 0.8f));
        }
    });
    parts.Draw();
}
