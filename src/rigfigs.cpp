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
