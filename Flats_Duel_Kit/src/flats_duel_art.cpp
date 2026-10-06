#include "flats_duel_art.h"
#include <algorithm>
#include <cmath>

namespace flats {
namespace duel {

namespace {
constexpr float PI_F = 3.14159265f;
Color C(Rgb c, unsigned char a = 255) { return Color{c.r, c.g, c.b, a}; }
Color Mul(Color c, float k) { return Color{(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
Color Lerp(Color a, Color b, float t) { return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)}; }
// raylib culls by winding: draw both (the game's DrawTri does the same; use it instead inside Depth if you like)
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col); DrawTriangle(a, c, b, col); }
void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) { Tri(a, b, c, col); Tri(a, c, d, col); }
void Glow(Vector2 c, float r, Color col) { for (int i = 6; i >= 1; i--) DrawCircleV(c, r * i / 6.0f, Fade(col, col.a / 255.0f * 0.22f)); }
void Hatch(Rectangle r, Color col, float spacing, float w) {
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    for (float d = -r.height; d < r.width; d += spacing) DrawLineEx({r.x + d, r.y}, {r.x + d + r.height, r.y + r.height}, w, col);
    EndScissorMode();
}
// diagonal ink hatching clipped to an ellipse (no scissor, so it never shows as a square on a lighter backdrop)
void HatchEllipse(Vector2 c, float rx, float ry, Color col, float spacing, float w) {
    const float k = 0.70710678f;   // lines run down-right at 45 degrees
    float reach = std::max(rx, ry) * 1.5f;
    for (float o = -reach * 2; o <= reach * 2; o += spacing) {
        // the line through c + o * (k, -k) with direction (k, k): solve ((x - cx) / rx)^2 + ((y - cy) / ry)^2 = 1
        float px = o * k, py = -o * k, dx = k, dy = k;
        float A = dx * dx / (rx * rx) + dy * dy / (ry * ry), B = 2 * (px * dx / (rx * rx) + py * dy / (ry * ry)), Cc = px * px / (rx * rx) + py * py / (ry * ry) - 1;
        float disc = B * B - 4 * A * Cc;
        if (disc <= 0) continue;
        float s0 = (-B - sqrtf(disc)) / (2 * A), s1 = (-B + sqrtf(disc)) / (2 * A);
        DrawLineEx({c.x + px + dx * s0, c.y + py + dy * s0}, {c.x + px + dx * s1, c.y + py + dy * s1}, w, col);
    }
}
Vector2 Rot(Vector2 p, Vector2 o, float a) { float c = cosf(a), s = sinf(a); return {o.x + (p.x - o.x) * c - (p.y - o.y) * s, o.y + (p.x - o.x) * s + (p.y - o.y) * c}; }
float Ease(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
float Bump(float u, float a, float b) { if (u <= a || u >= b) return 0; float x = (u - a) / (b - a); return sinf(x * PI_F); }   // 0 -> 1 -> 0 over [a, b]
float Hash(float x) { float s = sinf(x * 12.9898f) * 43758.5453f; return s - floorf(s); }
const Color INK{6, 8, 14, 255}, RIM{92, 150, 190, 255};
}  // namespace

DealerPose Add(const DealerPose& a, const DealerPose& b) {
    DealerPose r = a;
    r.bob += b.bob; r.lean += b.lean; r.headX += b.headX; r.headY += b.headY; r.headTilt += b.headTilt; r.shoulders += b.shoulders;
    r.handL.x += b.handL.x; r.handL.y += b.handL.y; r.handR.x += b.handR.x; r.handR.y += b.handR.y;
    r.palmsUp = std::max(a.palmsUp, b.palmsUp); r.hatLift += b.hatLift; r.hatTilt += b.hatTilt;
    r.brow += b.brow; r.browSkew += b.browSkew; r.mouth += b.mouth; r.mouthOpen = std::max(a.mouthOpen, b.mouthOpen); r.mouthSkew += b.mouthSkew;
    r.eyeOpen = a.eyeOpen * b.eyeOpen; r.eyeWide = std::max(a.eyeWide, b.eyeWide);
    r.gulp = std::max(a.gulp, b.gulp); r.sweat = std::max(a.sweat, b.sweat); r.jitter = std::max(a.jitter, b.jitter);
    r.holdCard = b.holdCard >= 0 ? b.holdCard : a.holdCard;
    return r;
}
DealerPose Mix(const DealerPose& a, const DealerPose& b, float w) {
    auto L = [w](float x, float y) { return x + (y - x) * w; };
    DealerPose r;
    r.bob = L(a.bob, b.bob); r.lean = L(a.lean, b.lean); r.headX = L(a.headX, b.headX); r.headY = L(a.headY, b.headY); r.headTilt = L(a.headTilt, b.headTilt);
    r.shoulders = L(a.shoulders, b.shoulders);
    r.handL = {L(a.handL.x, b.handL.x), L(a.handL.y, b.handL.y)}; r.handR = {L(a.handR.x, b.handR.x), L(a.handR.y, b.handR.y)};
    r.palmsUp = L(a.palmsUp, b.palmsUp); r.hatLift = L(a.hatLift, b.hatLift); r.hatTilt = L(a.hatTilt, b.hatTilt);
    r.brow = L(a.brow, b.brow); r.browSkew = L(a.browSkew, b.browSkew); r.mouth = L(a.mouth, b.mouth); r.mouthOpen = L(a.mouthOpen, b.mouthOpen);
    r.mouthSkew = L(a.mouthSkew, b.mouthSkew); r.eyeOpen = L(a.eyeOpen, b.eyeOpen); r.eyeWide = L(a.eyeWide, b.eyeWide);
    r.gulp = L(a.gulp, b.gulp); r.sweat = L(a.sweat, b.sweat); r.jitter = L(a.jitter, b.jitter);
    r.holdCard = w > 0.5f ? b.holdCard : a.holdCard;
    return r;
}

const char* ExpressionName(int ex) {
    static const char* N[EX_COUNT] = {"neutral", "ahead", "behind", "flinch", "wins", "loses"};
    return N[std::clamp(ex, 0, EX_COUNT - 1)];
}

DealerPose IdlePose(float t, int skin) {
    DealerPose p;
    p.bob = sinf(t * 1.1f) * 0.7f;
    float bl = fmodf(t + skin * 1.7f, 4.3f + 0.4f * (skin % 3));   // a blink every 4-5 s
    if (bl < 0.14f) p.eyeOpen = fabsf(bl - 0.07f) / 0.07f;
    p.handL = {sinf(t * 0.7f + skin) * 0.8f, cosf(t * 0.9f) * 0.5f};
    p.handR = {sinf(t * 0.6f + 2 + skin) * 0.8f, cosf(t * 0.8f + 1) * 0.5f};
    if (SkinOf(skin).hands == HANDS_FUMBLE) p.jitter = 0.35f;
    return p;
}

DealerPose ExpressionPose(int ex) {
    DealerPose p;
    switch (ex) {
        case EX_AHEAD: p.lean = 0.6f; p.brow = -0.6f; p.mouth = 0.8f; p.mouthSkew = 0.3f; p.headY = 2; p.handL = {6, 2}; p.handR = {-6, 2}; break;
        case EX_BEHIND: p.lean = -0.5f; p.brow = 0.8f; p.mouth = -0.8f; p.headY = -1; p.sweat = 0.6f; p.handL = {10, -4}; p.handR = {-10, -4}; break;
        case EX_FLINCH: p.lean = -0.8f; p.headX = -5; p.headY = -4; p.headTilt = -0.12f; p.eyeOpen = 0.25f; p.mouth = -0.6f; p.mouthOpen = 0.5f; p.brow = 0.5f; break;
        case EX_WIN: p.lean = -0.3f; p.headY = -3; p.mouth = 1.2f; p.mouthOpen = 0.35f; p.brow = 0.3f; p.handL = {-8, -2}; p.handR = {8, -2}; break;
        case EX_LOSE: p.lean = 0.2f; p.headY = 7; p.headTilt = 0.06f; p.mouth = -1.0f; p.brow = 0.9f; p.eyeOpen = 0.6f; p.shoulders = -0.5f; break;
        default: break;
    }
    return p;
}

DealerPose EmotePose(int e, float u) {
    DealerPose p;
    u = std::clamp(u, 0.0f, 1.0f);
    switch (e) {
        case EM_NOD: { float k = Bump(u, 0.0f, 0.45f) + 0.8f * Bump(u, 0.45f, 0.9f); p.headY = 7 * k; p.brow = -0.2f * k; p.mouth = 0.2f; } break;
        case EM_SNEER: { float k = Ease(u / 0.25f) * (1 - Ease((u - 0.75f) / 0.25f)); p.headTilt = -0.1f * k; p.headX = -3 * k; p.browSkew = 0.9f * k; p.brow = -0.4f * k; p.mouthSkew = 1.0f * k; p.mouth = -0.2f * k; p.lean = -0.2f * k; } break;
        case EM_SHRUG: { float k = Bump(u, 0.05f, 0.95f); p.shoulders = 1.0f * k; p.palmsUp = k; p.handL = {-18 * k, -32 * k}; p.handR = {18 * k, -32 * k}; p.headTilt = 0.1f * k; p.brow = 0.7f * k; p.mouth = -0.3f * k; } break;
        case EM_TIP_HAT: {
            float reach = Ease(u / 0.3f) * (1 - Ease((u - 0.75f) / 0.25f)), lift = Bump(u, 0.28f, 0.8f);
            p.handR = {-24 * reach, -74 * reach}; p.hatLift = 9 * lift; p.hatTilt = 0.35f * lift; p.headY = 4 * lift; p.mouth = 0.5f * reach;
        } break;
        case EM_SLOW_CLAP: {
            float in = Ease(u / 0.12f) * (1 - Ease((u - 0.88f) / 0.12f));
            float clap = 0.5f + 0.5f * cosf(u * 3 * 2 * PI_F);   // three claps
            p.handL = {(36 + 8 * clap) * in, -34 * in}; p.handR = {-(36 + 8 * clap) * in, -34 * in};
            p.mouth = 0.4f * in; p.mouthSkew = 0.4f * in; p.brow = -0.3f * in;
        } break;
        case EM_GULP: { float k = Bump(u, 0.0f, 1.0f); p.eyeWide = k; p.brow = 0.9f * k; p.gulp = Bump(u, 0.25f, 0.75f); p.sweat = k; p.lean = -0.2f * k; p.mouth = -0.4f * k; } break;
        default: break;
    }
    return p;
}

DealerPose ReachPose(Vector2 target, float u, Vector2 anchor, float scale) {
    DealerPose p;
    bool right = target.x >= anchor.x;                    // the hand on that side reaches
    float dx = (target.x - anchor.x) / (4.0f * scale) - (right ? 52.5f : -52.5f);
    float dy = (target.y - anchor.y) / (3.1f * scale) + 96.0f;
    float out = Ease(u / 0.42f) * (1 - Ease((u - 0.62f) / 0.38f));
    float slap = Bump(u, 0.40f, 0.52f);                   // the card goes down with a little slap
    Vector2 off{dx * out, dy * out + 3 * slap};
    if (right) p.handR = off; else p.handL = off;
    p.lean = 0.35f * out; p.headY = 2 * out; p.headX = (target.x - anchor.x) / (4.0f * scale) * 0.06f * out;
    if (u < 0.46f) p.holdCard = right ? 1 : 0;
    return p;
}

DealerPose TellPose(int skin, float u) {
    DealerPose p;
    float k = Bump(u, 0.0f, 1.0f);
    switch (SkinOf(skin).hands) {
        case HANDS_FUMBLE: p.handR = {-6 * k, -22 * Bump(u, 0.1f, 0.5f) - 6 * k}; p.jitter = 1.0f * k; p.headX = 3 * k; break;   // drops a card, snatches it back
        case HANDS_WET: p.handL = {14 * k, -58 * k}; p.headTilt = 0.08f * k; break;                                   // wrings her hair
        case HANDS_RINGED: p.handR = {-18 * k, -14 * k - 5 * Bump(fmodf(u * 4, 1.0f), 0, 1)}; break;                   // taps the anchor weight
        case HANDS_MECHANICAL: p.handR = {-6 * k, -16 * k - 6 * (Bump(u, 0.1f, 0.3f) + Bump(u, 0.35f, 0.55f))}; break;              // two clicks
        case HANDS_BONE: p.headY = -2 * k; p.lean = 0.2f * k; break;                                                  // his court leans in (drawn behind)
        case HANDS_PAWS: p.handR = {-12 * Bump(u, 0.2f, 0.6f), -18 * Bump(u, 0.2f, 0.6f)}; p.headTilt = -0.08f * k; break;   // bats a card
        default: p.handR = {-4 * k, -20 * k}; p.headY = 1 * k; break;                                                 // checks a watch / trims the wick
    }
    return p;
}

// ---------------------------------------------------------------- the figure
void DrawDuelDealer(const Skin& sk, const DealerPose& P, Vector2 anchor, float scale, float t, Vector2 look, int layer) {
    const float lean = std::clamp(P.lean, -1.0f, 1.0f);
    const float sc = scale * (1 + 0.05f * lean);
    const float sx = 4.0f * sc, sy = 3.1f * sc;
    const float cx = anchor.x, base = anchor.y + (P.bob + lean * 6) * sy;
    auto V = [&](float dx, float dy) { return Vector2{cx + dx * sx, base + dy * sy}; };
    const Vector2 neck = V(0, -134);
    auto H = [&](float dx, float dy) {   // head-space: moves and tilts with the head
        Vector2 p{cx + (dx + P.headX) * sx, base + (dy + P.headY) * sy};
        return Rot(p, {neck.x + P.headX * sx, neck.y + P.headY * sy}, P.headTilt);
    };
    auto Ell = [&](Vector2 c, float rx, float ry, Color col) { DrawEllipse((int)c.x, (int)c.y, rx, ry, col); };
    const Color cloak = C(sk.cloak), cloakLt = C(sk.cloakLit), cloakDk = Mul(cloak, 0.55f), trim = C(sk.trim), trimDk = Mul(trim, 0.6f);
    const Color skin = C(sk.skin), skinDk = Mul(skin, 0.6f), eyes = C(sk.eyes), gem = C(sk.gem);
    const float jit = P.jitter * sinf(t * 31) * 0.8f;
    const bool body = layer != DL_HANDS, hands = layer != DL_BODY;
    const float tableEdge = anchor.y - 302 * scale;   // where flats.cpp's DrawTable() starts to cover him

    if (body) {
    // 0. the Sovereign's court looms behind him: two drowned hoplites in silhouette
    if (sk.face == FACE_SKULL) {
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            Vector2 c = V(s2 * 78.0f, -120 + sinf(t * 0.8f + s2) * 2);
            Ell(c, 26 * sx, 40 * sy, Fade(Color{18, 30, 38, 255}, 0.85f));
            Ell({c.x, c.y - 44 * sy}, 12 * sx, 12 * sy, Fade(Color{18, 30, 38, 255}, 0.85f));
            Tri({c.x - 14 * sx, c.y - 52 * sy}, {c.x + 14 * sx, c.y - 52 * sy}, {c.x, c.y - 74 * sy}, Fade(Color{18, 30, 38, 255}, 0.85f));   // crested helm
            for (int e2 = -1; e2 <= 1; e2 += 2) DrawRectangle((int)(c.x + e2 * 4 * sx - 4), (int)(c.y - 46 * sy), 8, 3, Fade(eyes, 0.5f));
            DrawLineEx({c.x + s2 * 22 * sx, c.y + 30 * sy}, {c.x + s2 * 26 * sx, c.y - 90 * sy}, 4, Fade(Color{40, 52, 58, 255}, 0.9f));   // a spear
        }
    }
    // 1. the back mantle
    { Vector2 c = V(0, -76); Ell(c, 66 * sx + 6, 70 * sy + 6, INK); Ell(c, 66 * sx, 70 * sy, cloakDk); }
    // 2. the body, lit down its left edge
    { float shY = -116 - P.shoulders * 7;
    Ell(V(0, shY), 60 * sx + 6, 30 * sy + 6, INK); Ell(V(0, shY), 60 * sx, 30 * sy, cloak);
    { Vector2 b = V(0, -70); Ell(b, 58 * sx + 6, 56 * sy + 6, INK); Ell(b, 58 * sx, 56 * sy, cloak); Ell(V(-20, -68), 30 * sx, 48 * sy, cloakLt); Ell(V(8, -40), 44 * sx, 22 * sy, cloakDk); }
    for (int i = -2; i <= 2; i++) DrawLineEx(V(i * 9.0f, -112), V(i * 10.0f + (i > 0 ? 3.0f : -3.0f), -30), 3, cloakDk); }
    DrawRing(V(0, -70), 58 * sx - 3, 58 * sx + 2, 150, 210, 30, Fade(RIM, 0.8f));
    }
    float shY = -116 - P.shoulders * 7;
    // 3. the arms and hands (the hands pass draws them on the felt, over the table, with the forearm below its edge)
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        Vector2 off = s2 < 0 ? P.handL : P.handR;
        Vector2 sh = V(s2 * 42.0f, -122 - P.shoulders * 8), hd = V(s2 * 52.0f + off.x, -102 + off.y);
        if (body) { DrawLineEx(sh, hd, 84 * sc, INK); DrawLineEx(sh, hd, 76 * sc, cloak);
                    if (s2 < 0) DrawLineEx({sh.x - 20 * sc, sh.y}, {hd.x - 20 * sc, hd.y}, 10 * sc, cloakLt); }
        if (!hands) continue;
        if (layer == DL_HANDS && hd.y > tableEdge && sh.y < hd.y) {   // the forearm, from the table's edge down to the hand
            float k = std::clamp((tableEdge - sh.y) / (hd.y - sh.y), 0.0f, 1.0f);
            Vector2 e{sh.x + (hd.x - sh.x) * k, sh.y + (hd.y - sh.y) * k};
            DrawLineEx(e, hd, 84 * sc, INK); DrawLineEx(e, hd, 76 * sc, cloak);
        }
        Vector2 cf = V(s2 * 51.5f + off.x, -105 + off.y);
        DrawLineEx({cf.x - 30 * sc, cf.y}, {cf.x + 30 * sc, cf.y}, 16 * sc, INK); DrawLineEx({cf.x - 28 * sc, cf.y}, {cf.x + 28 * sc, cf.y}, 11 * sc, trim);
        Vector2 g = V(s2 * 52.5f + off.x + jit * (s2 > 0 ? 1.0f : -0.6f), -96 + off.y);
        bool mech = sk.hands == HANDS_MECHANICAL && s2 > 0;
        Color hand = sk.hands == HANDS_GLOVED || sk.hands == HANDS_RINGED ? Color{34, 34, 40, 255}
                   : sk.hands == HANDS_WET ? Lerp(skin, Color{120, 190, 180, 255}, 0.4f)
                   : sk.hands == HANDS_BONE ? Color{206, 198, 172, 255} : mech ? trim : skin;
        float up = P.palmsUp;
        if (sk.hands == HANDS_PAWS) {   // round paws with toe beans
            Ell(g, 40 * sc, 30 * sc, INK); Ell(g, 36 * sc, 26 * sc, hand);
            for (int i = -1; i <= 1; i++) Ell({g.x + i * 13.0f * sc, g.y + 12 * sc}, 6 * sc, 5 * sc, Color{226, 150, 160, 255});
            Ell({g.x, g.y - 2 * sc}, 10 * sc, 8 * sc, Color{226, 150, 160, 255});
        } else if (sk.hands == HANDS_BONE) {   // bare knuckle-bones and long phalanges
            Ell(g, 30 * sc, 18 * sc, INK); Ell(g, 26 * sc, 14 * sc, hand);
            for (int i = -2; i <= 1; i++) { Vector2 a{g.x + i * 11.0f * sc + 5 * sc, g.y + 6 * sc}, b{a.x + s2 * 2 * sc, a.y + (22 - 10 * up) * sc};
                DrawLineEx(a, b, 8 * sc, INK); DrawLineEx(a, b, 4.5f * sc, hand); DrawCircleV({(a.x + b.x) / 2, (a.y + b.y) / 2}, 3.2f * sc, hand); }
        } else {
            Ell(g, 42 * sc, 32 * sc, INK); Ell(g, 38 * sc, 28 * sc, hand);
            float fl = sk.hands == HANDS_WET ? 32.0f : 26.0f;   // the Tidewife's long wet fingers
            for (int i = -1; i <= 1; i++) {
                Vector2 a{g.x + i * 13.0f * sc, g.y + 10 * sc}, b{a.x + s2 * 3.0f * sc, a.y + (fl - 16 - 12 * up) * sc};
                DrawLineEx(a, b, 11 * sc, INK); DrawLineEx(a, b, 7 * sc, hand);
                if (mech) { DrawCircleV(a, 3.4f * sc, trimDk); DrawCircleV(b, 3.0f * sc, trimDk); }
            }
            if (mech) {   // rivets and a knuckle plate
                DrawRectangleRounded({g.x - 26 * sc, g.y - 14 * sc, 52 * sc, 16 * sc}, 0.4f, 4, trimDk);
                for (int i = -2; i <= 2; i++) DrawCircleV({g.x + i * 10.0f * sc, g.y - 6 * sc}, 2.4f * sc, Mul(trim, 1.3f));
            }
            if (sk.hands == HANDS_RINGED || sk.hands == HANDS_GLOVED) DrawRing({g.x + 13.0f * s2 * sc, g.y + 20 * sc}, 5 * sc, 9 * sc, 0, 360, 10, trim);
            if (sk.hands == HANDS_RINGED) for (int i = -1; i <= 1; i += 2) DrawRing({g.x + i * 13.0f * sc, g.y + 17 * sc}, 4 * sc, 7.5f * sc, 0, 360, 10, i < 0 ? gem : trim);
            if (sk.hands == HANDS_WET) for (int i = 0; i < 3; i++) { float ph = fmodf(t * 0.7f + i * 0.37f + s2, 1.0f); DrawCircleV({g.x + (i - 1) * 12.0f * sc, g.y + (24 + ph * 30) * sc}, 2.6f * sc, Fade(Color{170, 230, 230, 255}, 1 - ph)); }
            if ((sk.hands == HANDS_FUMBLE && s2 > 0 && P.holdCard < 0) || P.holdCard == (s2 < 0 ? 0 : 1)) {   // a card held (a little askew)
                Vector2 k{g.x - 8 * sc, g.y - 30 * sc};
                Vector2 q[4] = {{-12, -18}, {12, -18}, {12, 18}, {-12, 18}};
                for (auto& v : q) v = Rot({k.x + v.x * sc, k.y + v.y * sc}, k, 0.35f + jit * 0.2f);
                Quad(q[0], q[1], q[2], q[3], INK);
                Vector2 in[4]; for (int i = 0; i < 4; i++) in[i] = {k.x + (q[i].x - k.x) * 0.84f, k.y + (q[i].y - k.y) * 0.84f};
                Quad(in[0], in[1], in[2], in[3], Color{120, 86, 60, 255});
            }
        }
        DrawLineEx({g.x - 24 * sc, g.y - 12 * sc}, {g.x - 8 * sc, g.y - 16 * sc}, 2 * sc, Fade(RIM, 0.7f));
    }
    if (!body) return;
    // 4. the mantle over the shoulders, trimmed
    Ell(V(0, -117 - P.shoulders * 7), 54 * sx + 5, 17 * sy + 5, INK); Ell(V(0, -117 - P.shoulders * 7), 54 * sx, 17 * sy, cloakLt);
    DrawLineEx(V(-50, -108 - P.shoulders * 6), V(50, -108 - P.shoulders * 6), 6 * sc, trim);
    for (int i = -2; i <= 2; i++) { DrawLineEx(V(i * 15.0f, -109), V(i * 15.0f, -99), 3 * sc, trim); Ell(V(i * 15.0f, -98), 5 * sc, 5 * sc, trim); }
    // the throat: a gulp is a bob sliding down it
    if (P.gulp > 0) Ell(V(0, -134 + P.gulp * 8), 6 * sx, 3 * sy, Fade(skinDk, 0.8f));
    // 5. behind the head: a hood or a shawl
    bool hood = sk.hat == HAT_HOOD || sk.hat == HAT_SHAWL;
    Color hoodCol = sk.hat == HAT_SHAWL ? cloakLt : cloak;
    if (hood) {
        Tri(H(-34, -126), H(34, -126), H(0, -152), INK); Tri(H(-30, -126), H(30, -126), H(0, -148), cloakDk);
        Tri(H(-27, -140), H(27, -140), H(1, sk.hat == HAT_SHAWL ? -166 : -172), INK);
        Tri(H(-25, -141), H(25, -141), H(1, sk.hat == HAT_SHAWL ? -162 : -168), hoodCol);
        Tri(H(-38, -122), H(-20, -150), H(-14, -122), hoodCol); Tri(H(38, -122), H(20, -150), H(14, -122), hoodCol);
        DrawLineEx(H(-38, -122), H(-24, -156), 3, Fade(RIM, 0.9f));
        { Vector2 c = H(0, -160); Ell(c, 30 * sx + 5, 28 * sy + 5, INK); Ell(c, 30 * sx, 28 * sy, hoodCol); Ell(H(0, -159), 25 * sx, 23 * sy, cloakDk); }
    }
    // 6. the face
    Vector2 m0 = look;
    float lookx = std::clamp((m0.x - cx) * 0.012f, -4.0f, 4.0f), looky = std::clamp((m0.y - H(0, -160).y) * 0.006f, -2.0f, 2.0f);
    float open = std::clamp(P.eyeOpen, 0.0f, 1.0f) * (1 + 0.5f * P.eyeWide);
    auto Brows = [&](float y, float w, float thick, Color col) {   // two brow strokes that raise, lower and skew
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float lift = P.brow * 2.2f + (s2 > 0 ? P.browSkew * 2.5f : -P.browSkew * 0.8f);
            Vector2 a = H(s2 * 2.0f, y - lift + (P.brow < 0 ? -P.brow * 1.8f : 0)), b = H(s2 * (2.0f + w), y - lift - (P.brow < 0 ? P.brow * 0.6f : P.brow * 0.9f));
            DrawLineEx(a, b, thick, col);
        }
    };
    auto Mouth = [&](float y, float w, Color col, float thick) {
        Vector2 l = H(-w, y - P.mouth * 2.2f - P.mouthSkew * -0.3f), r = H(w, y - P.mouth * 2.2f - P.mouthSkew * 2.2f), mid = H(0, y + P.mouth * 1.4f);
        if (P.mouthOpen > 0.05f) { Ell(H(0, y + 1), w * 0.55f * sx, (1.2f + 3.0f * P.mouthOpen) * sy, Color{10, 4, 6, 255}); }
        DrawLineEx(l, mid, thick, col); DrawLineEx(mid, r, thick, col);
    };
    auto Sweat = [&](float x, float y) {
        if (P.sweat <= 0.05f) return;
        Vector2 d = H(x, y + fmodf(t * 0.5f, 1.0f) * 6);
        DrawCircleV(d, 4.5f * sc, Fade(Color{190, 230, 255, 255}, 0.8f * P.sweat)); Tri({d.x - 4 * sc, d.y}, {d.x + 4 * sc, d.y}, {d.x, d.y - 9 * sc}, Fade(Color{190, 230, 255, 255}, 0.8f * P.sweat));
    };
    switch (sk.face) {
        case FACE_WAX: {   // the Dealer's own: mostly shadow, one lit cheek, a brow bar, slivers of light for eyes
            Ell(H(1, -158), 15.5f * sx, 15.5f * sy, skinDk); Ell(H(-3.5f, -156), 10 * sx, 13 * sy, skin);
            for (int s2 = -1; s2 <= 1; s2 += 2) Ell(H(s2 * 8.0f, -149), 3.2f * sx, 3.6f * sy, Fade(BLACK, 0.3f));
            DrawLineEx(H(1, -161), H(2.5f, -150), 5, skinDk);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 e = H(s2 * 6.5f + lookx, -160 + looky);
                Glow(e, 28 * sc, Fade(eyes, 0.3f));
                float h = std::max(1.0f, 6 * sc * open);
                DrawRectangle((int)(e.x - 12 * sc), (int)(e.y - h / 2), (int)(24 * sc), (int)h, eyes);
            }
            Brows(-167, 11, 9 * sc, INK);
            Mouth(-142, 8, Color{4, 4, 8, 255}, 4 * sc);
            Sweat(9, -168);
        } break;
        case FACE_WEATHERED: {   // a sailor's face: tan, a beard, real eyes
            Ell(H(0, -157), 15 * sx + 4, 16 * sy + 4, INK); Ell(H(0, -157), 15 * sx, 16 * sy, skin); Ell(H(-4, -160), 8 * sx, 9 * sy, Mul(skin, 1.12f));
            Ell(H(0, -144), 14 * sx, 8 * sy, Mul(cloakDk, 1.2f));   // the beard
            for (int i = -3; i <= 3; i++) DrawLineEx(H(i * 3.0f, -148), H(i * 3.4f, -138), 2 * sc, Mul(cloakDk, 0.8f));
            DrawLineEx(H(0.5f, -160), H(2, -151), 5 * sc, skinDk);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 e = H(s2 * 6.0f, -161);
                Ell(e, 3.6f * sx, std::max(0.6f, 2.4f * open) * sy, Color{236, 232, 220, 255});
                if (open > 0.2f) DrawCircleV({e.x + lookx * sc * 1.2f, e.y + looky * sc}, 3.6f * sc, Color{30, 26, 24, 255});
                DrawCircleV({e.x - 2 * sc, e.y - 2 * sc}, 1.4f * sc, Fade(eyes, 0.9f));
            }
            Brows(-166, 6, 5 * sc, Mul(cloakDk, 0.7f));
            Mouth(-147.5f, 6, Color{178, 84, 70, 255}, 3.6f * sc);   // lips warm enough to read against the beard
            Sweat(10, -168);
        } break;
        case FACE_SKULL: {   // drowned and crowned: a skull with the tide's light in its sockets
            Color bone{206, 206, 186, 255};
            Ell(H(0, -158), 15 * sx + 4, 16 * sy + 4, INK); Ell(H(0, -158), 15 * sx, 16 * sy, bone); Ell(H(-4, -161), 8 * sx, 9 * sy, Mul(bone, 1.08f));
            Ell(H(0, -144), 10 * sx, 6 * sy, Mul(bone, 0.85f));
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 e = H(s2 * 6.2f, -160);
                Ell(e, 4.6f * sx, 4.2f * sy * (0.6f + 0.4f * open), Color{12, 16, 22, 255});
                Glow({e.x + lookx * sc, e.y + looky * sc}, 20 * sc, Fade(eyes, 0.55f));
                DrawCircleV({e.x + lookx * sc, e.y + looky * sc}, 3.4f * sc * open, eyes);
            }
            Tri(H(-1.8f, -152), H(1.8f, -152), H(0, -148), Color{20, 18, 20, 255});
            for (int k = -3; k <= 3; k++) DrawRectangle((int)H(k * 2.4f - 1, -143 + P.mouthOpen * 2).x, (int)H(0, -143 + P.mouthOpen * 2).y, (int)(7 * sc), (int)(8 * sc), Mul(bone, 1.05f));
            DrawLineEx(H(-9, -143), H(9, -143), 1.6f * sc, Color{40, 36, 34, 255});
            for (int i = 0; i < 4; i++) DrawLineEx(H(-12 + i * 7.0f, -170), H(-10 + i * 7.0f, -165), 1.5f * sc, Fade(Color{60, 110, 90, 255}, 0.8f));   // weed in the cracks
        } break;
        case FACE_BRASS: {   // Old Ironsides: the face is a porthole in a diving helmet (the helmet is drawn with the hats)
        } break;
        case FACE_CAT: {   // the Ship's Cat
            Ell(H(0, -156), 17 * sx + 4, 15 * sy + 4, INK); Ell(H(0, -156), 17 * sx, 15 * sy, skin); Ell(H(-5, -160), 8 * sx, 7 * sy, Mul(skin, 1.25f));
            Ell(H(0, -147), 8 * sx, 5 * sy, Mul(skin, 1.5f));   // the muzzle
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 e = H(s2 * 7.0f, -159);
                Ell(e, 4.2f * sx, 3.8f * sy * std::max(0.1f, open), eyes);
                if (open > 0.2f) Ell({e.x + lookx * sc, e.y}, 1.0f * sx, 3.2f * sy * open, Color{10, 10, 10, 255});
                for (int w = -1; w <= 1; w++) DrawLineEx(H(s2 * 6.0f, -147 + w * 1.4f), H(s2 * 19.0f, -148 + w * 3.2f), 1.4f * sc, Fade(Color{230, 226, 210, 255}, 0.8f));
            }
            Tri(H(-2.2f, -151), H(2.2f, -151), H(0, -148.5f), Color{226, 140, 150, 255});
            Mouth(-146, 3.5f, Color{30, 20, 20, 255}, 2 * sc);
            Brows(-165, 4, 3 * sc, Mul(skin, 0.5f));
        } break;
        default: break;
    }
    // 7. hats (after the face)
    Vector2 hatO = H(0, -172);
    auto Hat = [&](float dx, float dy) { Vector2 p = H(dx, dy - P.hatLift); return Rot(p, {hatO.x, hatO.y - P.hatLift * sy}, P.hatTilt); };
    switch (sk.hat) {
        case HAT_TRICORN: {
            Tri(Hat(-30, -168), Hat(30, -168), Hat(0, -190), INK);
            Quad(Hat(-34, -166), Hat(34, -166), Hat(24, -180), Hat(-24, -180), INK);
            Quad(Hat(-31, -167), Hat(31, -167), Hat(22, -178), Hat(-22, -178), cloakDk);
            Tri(Hat(-20, -177), Hat(20, -177), Hat(0, -188), cloak);
            DrawLineEx(Hat(-31, -167), Hat(31, -167), 3 * sc, trim);
            DrawLineEx(Hat(-31, -167), Hat(-24, -178), 3 * sc, trim); DrawLineEx(Hat(31, -167), Hat(24, -178), 3 * sc, trim);
            DrawCircleV(Hat(0, -171), 4 * sc, gem);
        } break;
        case HAT_TOPHAT: {
            Ell(Hat(0, -170), 26 * sx + 4, 4 * sy + 4, INK); Ell(Hat(0, -170), 26 * sx, 4 * sy, cloakDk);
            Quad(Hat(-15, -171), Hat(15, -171), Hat(14, -193), Hat(-14, -193), INK);   // (at scale 1 the brim of a taller hat leaves the screen)
            Quad(Hat(-13, -172), Hat(13, -172), Hat(12, -191.5f), Hat(-12, -191.5f), cloak);
            Quad(Hat(-13, -172), Hat(-4, -172), Hat(-4, -191.5f), Hat(-12, -191.5f), cloakLt);
            Quad(Hat(-13, -173), Hat(13, -173), Hat(13, -178), Hat(-13, -178), trim);
            Ell(Hat(0, -191.5f), 12 * sx, 2.2f * sy, cloakLt);
            // and a monocle over the right eye
            DrawRing(H(6.5f, -160), 9 * sc, 12 * sc, 0, 360, 16, trim); DrawLineEx(H(9, -156), H(14, -130), 1.5f * sc, trim);
        } break;
        case HAT_CROWN: {
            Quad(Hat(-15, -170), Hat(15, -170), Hat(15, -176), Hat(-15, -176), INK);
            Quad(Hat(-14, -171), Hat(14, -171), Hat(14, -175), Hat(-14, -175), trim);
            for (int i = -2; i <= 2; i++) {
                Tri(Hat(i * 6.0f - 3.4f, -175), Hat(i * 6.0f + 3.4f, -175), Hat(i * 6.0f, -186 - (i == 0 ? 4 : 0)), INK);
                Tri(Hat(i * 6.0f - 2.6f, -175), Hat(i * 6.0f + 2.6f, -175), Hat(i * 6.0f, -184 - (i == 0 ? 4 : 0)), trim);
                DrawCircleV(Hat(i * 6.0f, -173), 1.8f * sc, gem);
            }
            Glow(Hat(0, -186), 36 * sc, Fade(gem, 0.3f + 0.1f * sinf(t * 2)));
        } break;
        case HAT_CAP: {
            Ell(Hat(0, -172), 17 * sx + 4, 6 * sy + 4, INK); Ell(Hat(0, -172), 17 * sx, 6 * sy, cloak); Ell(Hat(-4, -174), 9 * sx, 3 * sy, cloakLt);
            Quad(Hat(-16, -168), Hat(16, -168), Hat(12, -164), Hat(-12, -164), INK);
            DrawLineEx(Hat(-16, -168), Hat(16, -168), 4 * sc, trim);
            DrawCircleV(Hat(0, -174), 4 * sc, trim); DrawCircleV(Hat(0, -174), 2.2f * sc, gem);
        } break;
        case HAT_HELMET: {   // a brass diving helmet: the porthole is the face
            Vector2 c = H(0, -160);
            Ell(c, 24 * sx + 5, 24 * sy + 5, INK); Ell(c, 24 * sx, 24 * sy, trim); Ell(H(-7, -166), 11 * sx, 11 * sy, Mul(trim, 1.25f));
            Ell(H(0, -140), 22 * sx, 5 * sy, trimDk);                                     // the breastplate collar
            for (int i = 0; i < 8; i++) { float a = i * PI_F / 4; DrawCircleV({c.x + cosf(a) * 17 * sx, c.y + sinf(a) * 17 * sy}, 3 * sc, trimDk); }
            Vector2 port = H(0, -159);
            DrawCircleV(port, 12 * sx + 4, INK); DrawCircleV(port, 12 * sx, trimDk); DrawCircleV(port, 10 * sx, Color{10, 34, 40, 255});
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 e{port.x + s2 * 4.5f * sx + lookx * sc, port.y - 1 * sy + looky * sc};
                Glow(e, 18 * sc, Fade(eyes, 0.5f)); DrawCircleV(e, 3.6f * sc * std::max(0.2f, open), eyes);
            }
            DrawCircleSector(port, 10 * sx, 200, 250, 8, Fade(WHITE, 0.25f));   // the glass's glint
            for (int s2 = -1; s2 <= 1; s2 += 2) { Vector2 sp = H(s2 * 20.0f, -162); DrawCircleV(sp, 5 * sx, INK); DrawCircleV(sp, 4 * sx, Color{10, 34, 40, 255}); }   // side ports
            float bub = fmodf(t * 0.6f, 1.0f);
            for (int i = 0; i < 3; i++) { float b = fmodf(bub + i * 0.33f, 1.0f); DrawCircleLines((int)(H(18, -186).x + sinf(b * 9 + i) * 6), (int)(H(18, -186).y - b * 80 * sc), (2 + 3 * b) * sc, Fade(Color{190, 230, 240, 255}, 1 - b)); }
        } break;
        case HAT_EARS: {
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Tri(H(s2 * 6.0f, -166), H(s2 * 17.0f, -164), H(s2 * 15.0f, -182), INK);
                Tri(H(s2 * 7.5f, -166.5f), H(s2 * 15.5f, -165), H(s2 * 14.2f, -179), skin);
                Tri(H(s2 * 9.5f, -167), H(s2 * 14.0f, -166), H(s2 * 13.4f, -176), Color{220, 150, 160, 255});
            }
        } break;
        case HAT_SHAWL: {   // the Tidewife's wet hair falls from under the shawl, with pearl earrings
            for (int i = 0; i < 6; i++) {   // three strands down each side of the face
                float x = (i < 3 ? -19.5f + i * 2.6f : 14.3f + (i - 3) * 2.6f), sway = sinf(t * 1.3f + i) * 1.2f;
                DrawLineEx(H(x * 0.9f, -164), H(x + sway, -128 - (i % 2) * 4), 5 * sc, INK);
                DrawLineEx(H(x * 0.9f, -164), H(x + sway, -128 - (i % 2) * 4), 3 * sc, Color{30, 70, 72, 255});
            }
            for (int s2 = -1; s2 <= 1; s2 += 2) { DrawCircleV(H(s2 * 15.5f, -150), 3.4f * sc, trim); DrawCircleV(H(s2 * 15.5f - 0.6f, -150.8f), 1.2f * sc, WHITE); }
        } break;
        default: break;
    }
    // 8. ink hatching over the dark masses, and the brooch at the throat
    HatchEllipse(V(0, -70), 58 * sx, 56 * sy, Fade(BLACK, 0.30f), 6 * sc, 1.4f * sc);                 // the body
    HatchEllipse(V(0, shY), 60 * sx, 30 * sy, Fade(BLACK, 0.22f), 8 * sc, 1.2f * sc);                 // the shoulders
    if (hood) HatchEllipse(H(0, -160), 30 * sx, 28 * sy, Fade(BLACK, 0.18f), 7 * sc, 1.1f * sc);        // the hood (over the face too: it sits in shadow)
    { Vector2 c = V(0, -124 - P.shoulders * 6); Ell(c, 16 * sc, 15 * sc, INK); Ell(c, 13 * sc, 12 * sc, trim); Ell(c, 8 * sc, 7 * sc, gem);
      Glow(c, 40 * sc, Fade(gem, 0.3f + 0.12f * sinf(t * 2))); }
}

// ---------------------------------------------------------------- icons
void DrawTurnClock(Vector2 c, float r, float frac, float warn, float t) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    Color brass{196, 160, 84, 255}, dk{96, 72, 40, 255}, red{220, 80, 60, 255};
    bool hot = frac < warn;
    DrawCircleV(c, r + 4, INK);
    DrawCircleV(c, r, Color{20, 22, 28, 255});
    DrawRing(c, r * 0.72f, r, -90, -90 + 360 * frac, 48, hot ? Lerp(red, Color{255, 160, 120, 255}, 0.5f + 0.5f * sinf(t * 12)) : brass);
    DrawRing(c, r * 0.72f, r, -90 + 360 * frac, 270, 48, dk);
    for (int i = 0; i < 12; i++) { float a = i * PI_F / 6; DrawLineEx({c.x + cosf(a) * r * 0.6f, c.y + sinf(a) * r * 0.6f}, {c.x + cosf(a) * r * 0.7f, c.y + sinf(a) * r * 0.7f}, 2, dk); }
    float a = -PI_F / 2 + 2 * PI_F * frac;
    DrawLineEx(c, {c.x + cosf(a) * r * 0.55f, c.y + sinf(a) * r * 0.55f}, 3, hot ? red : brass);
    DrawCircleV(c, 4, brass);
}

void DrawDuelIcon(int icon, Vector2 c, float s, Color ink, float t) {
    const float u = s / 64.0f;   // icons are designed on a 64-unit square
    auto P = [&](float x, float y) { return Vector2{c.x + x * u, c.y + y * u}; };
    const float w = std::max(1.5f, 3.2f * u);
    Color fill = Fade(ink, 0.18f);
    auto Card = [&](float x, float y, float rot, Color face) {
        Vector2 o = P(x, y), q[4] = {P(x - 9, y - 13), P(x + 9, y - 13), P(x + 9, y + 13), P(x - 9, y + 13)};
        for (auto& v : q) v = Rot(v, o, rot);
        Quad(q[0], q[1], q[2], q[3], face);
        for (int i = 0; i < 4; i++) DrawLineEx(q[i], q[(i + 1) % 4], w * 0.8f, ink);
    };
    auto Face = [&](float mouth, float browL, float browR) {   // a small inked head for the emote icons
        DrawCircleV(P(0, 0), 20 * u, fill); DrawRing(P(0, 0), 20 * u - w, 20 * u, 0, 360, 32, ink);
        DrawLineEx(P(-11, -9 - browL), P(-3, -8 + browL * 0.3f), w, ink); DrawLineEx(P(3, -8 + browR * 0.3f), P(11, -9 - browR), w, ink);
        DrawCircleV(P(-7, -2), 2.4f * u, ink); DrawCircleV(P(7, -2), 2.4f * u, ink);
        DrawLineEx(P(-8, 9 - mouth), P(0, 9 + mouth), w, ink); DrawLineEx(P(0, 9 + mouth), P(8, 9 - mouth), w, ink);
    };
    switch (icon) {
        case IC_NOD: Face(2, 0, 0); for (int i = 0; i < 2; i++) DrawLineEx(P(26, -10 + i * 10), P(26, -4 + i * 10), w, ink); Tri(P(22, 4), P(30, 4), P(26, 10), ink); break;
        case IC_SNEER: Face(0, -1, 4); DrawLineEx(P(4, 10), P(9, 6), w, ink); break;
        case IC_SHRUG:
            Face(-1, 3, 3);
            DrawLineEx(P(-20, 18), P(-30, 8), w, ink); DrawLineEx(P(20, 18), P(30, 8), w, ink);
            DrawLineEx(P(-34, 8), P(-26, 8), w, ink); DrawLineEx(P(26, 8), P(34, 8), w, ink);
            break;
        case IC_TIP_HAT: {
            Face(2, 0, 0);
            Vector2 o = P(0, -22); float r = -0.35f + 0.05f * sinf(t * 3);
            Vector2 q[4] = {P(-12, -22), P(12, -22), P(10, -40), P(-10, -40)};
            for (auto& v : q) v = Rot(v, o, r);
            Quad(q[0], q[1], q[2], q[3], ink);
            DrawLineEx(Rot(P(-20, -22), o, r), Rot(P(20, -22), o, r), w * 1.4f, ink);
        } break;
        case IC_SLOW_CLAP: {
            float k = 0.5f + 0.5f * sinf(t * 3);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                Vector2 h = P(s2 * (6 + 8 * k), 2);
                DrawEllipse((int)h.x, (int)h.y, 9 * u, 15 * u, fill); DrawEllipseLines((int)h.x, (int)h.y, 9 * u, 15 * u, ink);
                for (int f = -1; f <= 1; f++) DrawLineEx({h.x + f * 3 * u, h.y - 15 * u}, {h.x + f * 3 * u, h.y - 22 * u}, w * 0.8f, ink);
            }
            for (int i = 0; i < 3; i++) DrawLineEx(P(-20 + i * 20, -26), P(-16 + i * 16, -34), w * 0.7f, Fade(ink, 0.7f));
        } break;
        case IC_GULP:
            Face(-1, 4, 4);
            DrawCircleV(P(15, -16), 3.6f * u, ink); Tri(P(11.6f, -15), P(18.4f, -15), P(15, -23), ink);
            DrawCircleV(P(0, 14), 3 * u, ink);
            break;
        case IC_DRAFT:   // a fan of cards and an arrow passing one across
            Card(-10, 4, -0.35f, fill); Card(0, 0, 0, fill); Card(10, 4, 0.35f, fill);
            DrawLineEx(P(-22, -24), P(20, -24), w, ink); Tri(P(20, -30), P(20, -18), P(28, -24), ink);
            break;
        case IC_CONSTRUCTED:   // a deck box with a wax seal
            DrawRectangleRounded({P(-16, -20).x, P(-16, -20).y, 32 * u, 42 * u}, 0.15f, 4, fill);
            DrawRectangleRoundedLinesEx({P(-16, -20).x, P(-16, -20).y, 32 * u, 42 * u}, 0.15f, 4, w, ink);
            DrawLineEx(P(-16, -10), P(16, -10), w * 0.8f, ink);
            DrawCircleV(P(0, 8), 8 * u, Color{180, 50, 50, 255}); DrawRing(P(0, 8), 6 * u, 8 * u, 0, 360, 16, ink);
            break;
        case IC_QUICK:   // an hourglass
            Tri(P(-14, -24), P(14, -24), P(0, 0), fill); Tri(P(-14, 24), P(14, 24), P(0, 0), ink);
            DrawLineEx(P(-14, -24), P(14, 24), w, ink); DrawLineEx(P(14, -24), P(-14, 24), w, ink);
            DrawLineEx(P(-18, -24), P(18, -24), w * 1.3f, ink); DrawLineEx(P(-18, 24), P(18, 24), w * 1.3f, ink);
            break;
        case IC_REEL: {   // two hands of cards facing over the brass scales
            DrawLineEx(P(0, -26), P(0, 20), w, ink);
            float tilt = 0.18f * sinf(t * 1.2f);
            Vector2 l = Rot(P(-22, -18), P(0, -18), tilt), r = Rot(P(22, -18), P(0, -18), tilt);
            DrawLineEx(l, r, w, ink);
            for (Vector2 e : {l, r}) { DrawLineEx(e, {e.x - 8 * u, e.y + 12 * u}, w * 0.6f, ink); DrawLineEx(e, {e.x + 8 * u, e.y + 12 * u}, w * 0.6f, ink);
                DrawCircleSector({e.x, e.y + 12 * u}, 9 * u, 0, 180, 10, ink); }
            Card(-20, 18, -0.25f, fill); Card(20, 18, 0.25f, fill);
            DrawEllipse((int)P(0, 22).x, (int)P(0, 22).y, 12 * u, 3 * u, ink);
        } break;
        case IC_PEARL:
            DrawCircleV(c, 12 * u, Color{232, 228, 214, 255}); DrawRing(c, 12 * u - w * 0.7f, 12 * u, 0, 360, 24, ink);
            DrawCircleV(P(-4, -4), 3.6f * u, WHITE);
            break;
        case IC_SIDEBOARD:   // two cards swapping
            Card(-9, 2, -0.2f, fill); Card(9, -2, 0.2f, fill);
            DrawLineEx(P(-20, -24), P(14, -24), w * 0.8f, ink); Tri(P(14, -28), P(14, -20), P(20, -24), ink);
            DrawLineEx(P(20, 26), P(-14, 26), w * 0.8f, ink); Tri(P(-14, 22), P(-14, 30), P(-20, 26), ink);
            break;
        case IC_TIMER: DrawTurnClock(c, 24 * u, 0.62f, 0.2f, t); break;
        case IC_PACK:   // a folded paper pack tied with twine
            DrawRectangleRounded({P(-20, -16).x, P(-16, -16).y, 40 * u, 32 * u}, 0.1f, 4, Color{206, 190, 150, 255});
            DrawRectangleRoundedLinesEx({P(-20, -16).x, P(-16, -16).y, 40 * u, 32 * u}, 0.1f, 4, w, ink);
            DrawLineEx(P(0, -16), P(0, 16), w, Color{120, 80, 50, 255}); DrawLineEx(P(-20, 0), P(20, 0), w, Color{120, 80, 50, 255});
            DrawCircleV(P(0, 0), 3.4f * u, Color{120, 80, 50, 255});
            DrawLineEx(P(0, 0), P(-8, -10), w * 0.7f, Color{120, 80, 50, 255}); DrawLineEx(P(0, 0), P(8, -10), w * 0.7f, Color{120, 80, 50, 255});
            break;
        default: break;
    }
}

}  // namespace duel
}  // namespace flats
