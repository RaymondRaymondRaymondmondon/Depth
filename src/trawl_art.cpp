// The Trawl's procedural pixel art (design doc, "Art"): a top-down deck drawn at twice the parkour pixel scale into
// the pixel canvas, in the boat's own frame (bow to the right) so the planks stay crisp while the sea turns under her.
// The sea is black outside the light: only the lantern's pool shows water, foam and glints.
#include "trawl_art.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace tw {

static float H01(int x, int y, int s) { unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (unsigned)(s * 83492791); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xFFFF) / 65535.0f; }
static Color Mix(Color a, Color b, float t) { t = std::clamp(t, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255}; }
static Color Dim(Color c, float k) { return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; }

Vector2 View::ToCanvas(Vector2 deck) const { return {floorf(center.x + deck.x * ppm), floorf(center.y + deck.y * ppm)}; }
Vector2 View::DeckOfCanvas(Vector2 c) const { return {(c.x - center.x) / ppm, (c.y - center.y) / ppm}; }
static void Px(const View& v, Vector2 deck, float w, float h, Color c) {   // a deck-space box, snapped to the canvas grid
    Vector2 p = v.ToCanvas(deck);
    DrawRectangle((int)p.x, (int)p.y, (int)std::max(1.0f, w * v.ppm), (int)std::max(1.0f, h * v.ppm), c);
}
static void PxC(const View& v, Vector2 deck, float w, float h, Color c) { Px(v, {deck.x - w / 2, deck.y - h / 2}, w, h, c); }

// how much light reaches a deck point (0 dark .. 1 lit): the lantern, the wheelhouse glow, deck lamps
float View::LightAt(Vector2 deck) const {
    float best = 0;
    for (const auto& l : lights) {
        float d = Vector2Distance(deck, l.at);
        if (d < l.r) best = std::max(best, l.k * (1 - d / l.r) * (1 - d / l.r) * 1.4f);
    }
    return std::min(1.0f, best);
}

// ---------------------------------------------------------------- the sea
void DrawSea(const Gannet& g, const View& v) {
    const int C = 3;   // canvas pixels per sea cell
    Vector2 f = g.boat.Forward(), s{-f.y, f.x};
    for (int cy = 0; cy < PIXEL_H + 2; cy += C) for (int cx = 0; cx < PIXEL_W + 2; cx += C) {
        Vector2 d = v.DeckOfCanvas({(float)cx + C / 2.0f, (float)cy + C / 2.0f});
        float lit = v.LightAt(d);
        if (lit < 0.02f) {
            // outside the light: black, with a rare glint of moon on a crest
            Vector2 w{g.boat.pos.x + f.x * d.x + s.x * d.y, g.boat.pos.y + f.y * d.x + s.y * d.y};
            float hgt = g.sea.Height(w.x, w.y);
            if (v.moon > 0 && hgt > g.sea.swell * 0.62f && H01((int)(w.x * 2), (int)(w.y * 2), (int)(g.time * 3)) < 0.05f * v.moon) DrawRectangle(cx, cy, 1, 1, Color{120, 140, 150, 255});
            continue;
        }
        Vector2 w{g.boat.pos.x + f.x * d.x + s.x * d.y, g.boat.pos.y + f.y * d.x + s.y * d.y};
        float hgt = g.sea.Height(w.x, w.y) / std::max(0.2f, g.sea.swell);
        // bands of swell: dark troughs, lighter faces, foam on the crests
        Color deep{6, 22, 30, 255}, face{26, 74, 84, 255}, foam{170, 196, 196, 255};
        float band = floorf((0.5f + hgt * 0.5f) * 5) / 5;          // (in steps: swell reads as bands)
        Color c = Mix(deep, face, band);
        if (hgt > 0.8f && H01((int)(w.x * 3), (int)(w.y * 3), (int)(g.time * 2)) < 0.12f + g.sea.swell * 0.1f) c = Mix(c, foam, 0.7f);
        float k = 0.25f + 0.75f * lit;
        DrawRectangle(cx, cy, C, C, Dim(c, k));
    }
    // the boat's wake and the bow wave, in the light
    float sp = g.boat.Speed();
    if (sp > 0.4f) for (int i = 0; i < 40; i++) {
        float x = -11.5f - i * 0.45f, spread = 0.4f + i * 0.09f;
        for (int k = -1; k <= 1; k += 2) {
            Vector2 d{x, k * spread + sinf(g.time * 3 + i) * 0.1f};
            float lit = v.LightAt(d);
            if (lit < 0.05f || H01(i, k, (int)(g.time * 8)) > std::min(1.0f, sp / 4)) continue;
            Px(v, d, 0.2f, 0.2f, Dim(Color{200, 220, 220, 255}, 0.3f + 0.7f * lit));
        }
    }
}

// ---------------------------------------------------------------- the boat
static float HalfBeamArt(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }

void DrawBoat(const Gannet& g, const View& v) {
    const Boat& b = g.boat;
    float t = g.time;
    Color hull{70, 26, 24, 255}, hullDk{40, 14, 14, 255}, rail{150, 120, 80, 255};
    Color plank{112, 84, 56, 255}, plankDk{92, 68, 44, 255}, iron{64, 68, 70, 255}, brass{200, 160, 70, 255};
    // foam where the swell meets the hull, most on the low side
    for (float x = -11.0f; x < 10.8f; x += 0.25f) for (int side = -1; side <= 1; side += 2) {
        float hb = HalfBeamArt(x) + 0.15f;
        Vector2 d{x, side * hb};
        float low = side * b.RollDeg() > 0 ? 1.0f : 0.3f;
        if (H01((int)(x * 4), side, (int)(t * 5)) < 0.35f * low + 0.1f) Px(v, d, 0.25f, 0.2f, Dim(Color{180, 204, 204, 255}, 0.2f + 0.6f * v.LightAt(d)));
    }
    // the hull, stepped plate by plate (the bow's taper in half-metre steps), a dark waterline shadow on the low side
    float lowSide = b.roll >= 0 ? 1.0f : -1.0f;
    for (float x = -11.0f; x < 10.8f; x += 0.5f) {
        float hb = HalfBeamArt(x + 0.25f);
        Px(v, {x, -hb - 0.25f + (lowSide < 0 ? -0.1f : 0.0f) * fabsf(b.RollDeg()) / 10}, 0.5f, 2 * hb + 0.5f + fabsf(b.RollDeg()) / 100, Dim(hullDk, 0.5f + 0.5f * v.LightAt({x, 0})));
        Px(v, {x, -hb}, 0.5f, 2 * hb, Dim(hull, 0.35f + 0.65f * v.LightAt({x, 0})));
        // the deck, planked fore and aft
        float dhb = hb - 0.3f;
        if (dhb <= 0.1f) continue;
        for (float y = -dhb; y < dhb - 0.01f; y += 0.25f) {
            int row = (int)((y + 3) * 4);
            bool seam = ((int)((x + 11) * 2) + row * 3) % 7 == 0;
            Color c = row % 2 ? plank : plankDk;
            if (seam) c = Dim(c, 0.8f);
            float wet = b.bilge > 0 ? 0 : 0;
            (void)wet;
            Px(v, {x, y}, 0.5f, std::min(0.25f, dhb - y), Dim(c, 0.3f + 0.7f * v.LightAt({x + 0.25f, y})));
        }
        // the rails
        Px(v, {x, -dhb - 0.1f}, 0.5f, 0.12f, Dim(rail, 0.3f + 0.7f * v.LightAt({x, -dhb})));
        Px(v, {x, dhb - 0.02f}, 0.5f, 0.12f, Dim(rail, 0.3f + 0.7f * v.LightAt({x, dhb})));
    }
    auto L = [&](Vector2 d) { return 0.3f + 0.7f * v.LightAt(d); };
    // the stern gantry and the net drum
    PxC(v, {-10.6f, 0}, 0.3f, 5.4f, Dim(iron, L({-10.6f, 0})));
    PxC(v, {-9.4f, 0}, 1.0f, 2.4f, Dim(Color{80, 70, 56, 255}, L({-9.4f, 0})));
    for (int k = -4; k <= 4; k++) PxC(v, {-9.4f, k * 0.25f}, 0.9f, 0.06f, Dim(Color{120, 110, 80, 255}, L({-9.4f, 0})));
    // the hold hatch and the engine room hatch with its ladder
    PxC(v, {-2.2f, -0.9f}, 1.6f, 1.2f, Dim(Color{60, 48, 34, 255}, L({-2.2f, -0.9f})));
    PxC(v, {-2.2f, -0.9f}, 1.3f, 0.9f, Dim(Color{78, 62, 44, 255}, L({-2.2f, -0.9f})));
    PxC(v, {-3.4f, -1.8f}, 0.8f, 0.8f, Dim(Color{24, 20, 18, 255}, L({-3.4f, -1.8f})));
    for (int k = 0; k < 3; k++) PxC(v, {-3.4f, -2.05f + k * 0.25f}, 0.6f, 0.06f, Dim(Color{140, 120, 80, 255}, L({-3.4f, -1.8f})));
    // the gutting table
    PxC(v, {-2.2f, 2.2f}, 1.4f, 0.7f, Dim(Color{150, 150, 140, 255}, L({-2.2f, 2.2f})));
    // the air pump beside the diving gantry, the stern rods, the side rods in their holders
    PxC(v, {-6.2f, 2.65f}, 0.6f, 0.4f, Dim(brass, L({-6.2f, 2.65f})));
    for (Vector2 r : {Vector2{-10.4f, -2.1f}, Vector2{-10.4f, 2.1f}, Vector2{-0.8f, -2.8f}, Vector2{-0.8f, 2.8f}}) PxC(v, r, 0.2f, 0.2f, Dim(Color{40, 36, 30, 255}, L(r)));
    // the harpoon cannon on the bow
    PxC(v, {9.6f, 0}, 0.8f, 0.8f, Dim(iron, L({9.6f, 0})));
    Px(v, {9.8f, -0.1f}, 1.0f, 0.2f, Dim(Color{40, 44, 46, 255}, L({9.6f, 0})));
    // the bell
    PxC(v, {6.0f, 2.05f}, 0.35f, 0.35f, Dim(brass, L({6, 2})));
    // the wheelhouse: a roof from outside; cut away (its floor, the helm, the sonar and the printer) from inside
    Color wood{98, 76, 58, 255}, roof{60, 64, 62, 255};
    if (v.inWheelhouse) {
        PxC(v, {3.0f, 0}, 4.2f, 4.2f, Dim(wood, L({3, 0})));
        PxC(v, {3.0f, 0}, 3.8f, 3.8f, Dim(Color{120, 96, 70, 255}, L({3, 0})));
        PxC(v, {1.0f, 0}, 0.2f, 1.4f, Dim(Color{120, 96, 70, 255}, L({1, 0})));   // (the door)
        // the wheel
        Vector2 hc = v.ToCanvas({4.55f, 0});
        float a = b.rudder * 2.5f;
        DrawCircleLines((int)hc.x, (int)hc.y, 0.45f * v.ppm, Dim(brass, L({4.5f, 0})));
        for (int k = 0; k < 4; k++) { float aa = a + k * 0.785f; DrawLine((int)(hc.x - cosf(aa) * 6), (int)(hc.y - sinf(aa) * 6), (int)(hc.x + cosf(aa) * 6), (int)(hc.y + sinf(aa) * 6), Dim(brass, L({4.5f, 0}))); }
        PxC(v, {2.4f, -1.55f}, 0.9f, 0.5f, Dim(Color{30, 50, 40, 255}, L({2.4f, -1.5f})));    // the sonar scope
        PxC(v, {2.4f, -1.55f}, 0.5f, 0.3f, Color{60, (unsigned char)(160 + 60 * sinf(t * 2)), 110, 255});
        PxC(v, {2.4f, 1.55f}, 0.8f, 0.5f, Dim(brass, L({2.4f, 1.5f})));                    // the telegraph printer
        PxC(v, {4.0f, -1.6f}, 0.5f, 0.4f, Dim(Color{180, 170, 150, 255}, L({4, -1.5f})));   // the clock
    } else {
        PxC(v, {3.0f, 0}, 4.3f, 4.3f, Dim(Color{36, 38, 36, 255}, L({3, 0})));
        PxC(v, {3.0f, 0}, 4.0f, 4.0f, Dim(roof, L({3, 0})));
        for (int k = 0; k < 4; k++) PxC(v, {1.4f + k * 1.05f, 0}, 0.08f, 3.8f, Dim(Color{50, 54, 52, 255}, L({3, 0})));
        PxC(v, {4.5f, 1.4f}, 0.3f, 0.3f, Dim(Color{120, 90, 60, 255}, L({4.5f, 1.4f})));   // the funnel's stay
    }
    // the funnel, aft of the wheelhouse, smoking with the fire
    PxC(v, {0.6f, 1.0f}, 0.7f, 0.7f, Dim(Color{30, 28, 28, 255}, L({0.6f, 1})));
    PxC(v, {0.6f, 1.0f}, 0.5f, 0.5f, Color{(unsigned char)(40 + 120 * std::clamp(b.firebox / 6, 0.0f, 1.0f)), 30, 20, 255});
    // the lantern mast: the lamp's housing and its glow
    PxC(v, {0.2f, 0}, 0.5f, 0.5f, Dim(iron, L({0.2f, 0})));
    PxC(v, {0.2f, 0}, 0.3f, 0.3f, Color{255, 220, 140, 255});
    // the engine room, seen from below: the rest of the deck dark above
    if (v.viewerDeck == 1) {
        DrawRectangle(0, 0, PIXEL_W + 2, PIXEL_H + 2, Fade(BLACK, 0.55f));
        PxC(v, {-5.65f, 0}, 5.8f, 5.0f, Color{36, 32, 30, 255});
        PxC(v, {-5.65f, 0}, 5.5f, 4.6f, Color{58, 52, 46, 255});
        for (int k = 0; k < 11; k++) PxC(v, {-8.2f + k * 0.5f, 0}, 0.04f, 4.6f, Color{50, 44, 40, 255});
        // the boiler: its drum against the port side, the firebox door glowing with the fire, the gauge
        PxC(v, {-4.8f, -1.55f}, 2.0f, 1.4f, Color{70, 64, 60, 255});
        PxC(v, {-4.8f, -1.55f}, 1.7f, 1.1f, Color{88, 80, 74, 255});
        float glow = std::clamp(b.firebox / 6, 0.1f, 1.0f) * (0.8f + 0.2f * sinf(t * 9));
        PxC(v, {-4.8f, -0.85f}, 0.7f, 0.25f, Color{(unsigned char)(120 + 135 * glow), (unsigned char)(50 + 110 * glow), 20, 255});
        Color gauge = b.pressure > D().redAt ? Color{230, 60, 40, 255} : b.pressure > D().greenHi ? Color{230, 190, 60, 255} : b.pressure > D().greenLo ? Color{90, 200, 90, 255} : Color{150, 150, 150, 255};
        PxC(v, {-4.0f, -1.2f}, 0.35f, 0.35f, gauge);
        // the coal bunker, the bilge pump, the water in her
        float coal = std::clamp(b.bunker / (D().sackKg * 6), 0.0f, 1.0f);
        PxC(v, {-7.4f, -1.3f}, 1.4f, 1.6f, Color{30, 28, 26, 255});
        PxC(v, {-7.4f, -1.3f + 0.8f * (1 - coal)}, 1.2f, 1.4f * coal + 0.05f, Color{20, 20, 22, 255});
        PxC(v, {-7.0f, 1.9f}, 0.6f, 0.6f, Color{90, 110, 120, 255});
        if (b.bilge > 50) { float k = std::clamp(b.bilge / 20000, 0.1f, 1.0f); DrawRectangle(0, 0, 0, 0, BLANK); PxC(v, {-5.65f, 0}, 5.4f, 4.5f, Fade(Color{30, 70, 90, 255}, 0.25f + 0.5f * k)); }
        if (b.valveT > 0) for (int k = 0; k < 20; k++) PxC(v, {-4.2f + H01(k, 1, (int)(t * 10)) * 2, -1.6f + H01(k, 2, (int)(t * 10)) * 2}, 0.2f, 0.2f, Fade(WHITE, 0.6f));
        if (b.fireT > 0) for (int k = 0; k < 10; k++) PxC(v, {-5.2f + H01(k, 3, (int)(t * 12)) * 1.2f, -0.6f + H01(k, 4, (int)(t * 12)) * 0.6f}, 0.15f, 0.15f, Color{255, (unsigned char)(120 + 100 * H01(k, 5, (int)(t * 12))), 40, 255});
    }
}

// ---------------------------------------------------------------- the crew (top-down: a hat, shoulders in oilskins, arms)
Color RoleColor(Role r) {
    switch (r) {
        case Role::Bosun: return {40, 56, 90, 255};
        case Role::Angler: return {214, 180, 50, 255};
        case Role::Diver: return {120, 130, 90, 255};
        default: return {220, 220, 210, 255};
    }
}
void DrawCrewMember(const Crew& c, const View& v, float t, bool you) {
    if (c.overboard) return;
    if (c.deck != v.viewerDeck && v.viewerDeck == 0) return;     // (below decks, out of sight)
    Vector2 p = v.ToCanvas(c.p);
    float k = 0.35f + 0.65f * v.LightAt(c.p);
    Color coat = Dim(RoleColor(c.role), k), dark = Dim(Color{30, 26, 22, 255}, k), skin = Dim(Color{214, 170, 130, 255}, k);
    if (c.fallen) {   // flat on the deck
        DrawRectangle((int)p.x - 4, (int)p.y - 2, 9, 5, coat);
        DrawRectangle((int)p.x + 3, (int)p.y - 1, 3, 3, skin);
        return;
    }
    float bob = c.station < 0 && Vector2Length(c.v) > 0.3f ? (sinf(t * 12) > 0 ? 1.0f : 0.0f) : 0;
    Vector2 f = c.facing;
    // shadow, shoulders, arms toward the facing, the sou'wester hat
    DrawRectangle((int)p.x - 3, (int)p.y - 3 + 1, 7, 7, Fade(BLACK, 0.35f));
    DrawRectangle((int)p.x - 3, (int)p.y - 3, 7, 7, coat);
    DrawRectangle((int)(p.x + f.x * 3 - 1), (int)(p.y + f.y * 3 - 1 + bob), 2, 2, skin);
    DrawRectangle((int)(p.x - f.y * 3), (int)(p.y + f.x * 3), 2, 2, Dim(coat, 0.8f));
    DrawRectangle((int)(p.x + f.y * 3 - 1), (int)(p.y - f.x * 3 - 1), 2, 2, Dim(coat, 0.8f));
    DrawRectangle((int)p.x - 2, (int)p.y - 2, 5, 5, dark);
    DrawRectangle((int)p.x - 1, (int)p.y - 1, 3, 3, Dim(RoleColor(c.role), k * 0.8f));
    if (you) DrawRectangleLines((int)p.x - 5, (int)p.y - 5, 11, 11, Fade(Color{255, 240, 200, 255}, 0.25f + 0.2f * sinf(t * 4)));
}

} // namespace tw
