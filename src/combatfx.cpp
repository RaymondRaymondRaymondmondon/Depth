// ============================================================================
//  DEPTH - combat effects and camera (see combatfx.h).
// ============================================================================
#include "combatfx.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace cfx {
namespace {
struct Splash { Vector2 at; float life, max, size; bool crit, gold; unsigned seed; };
struct Number { Vector2 at; std::string text; Color col; float t, size; bool drop; };
struct Ripple { Vector2 at; float t; Color col; };
std::vector<Splash> splashes;
std::vector<Number> numbers;
std::vector<Ripple> ripples;
float shake = 0, shakeAmp = 0, flash = 0, dim = 0, zoom = 1, zoomTarget = 1, clock = 0;
Vector2 focus{640, 380}, focusTarget{640, 380};
bool focusing = false;

float Hash(unsigned n) { n = (n << 13) ^ n; return ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffff) / 2147483647.0f; }
}  // namespace

void Reset() { splashes.clear(); numbers.clear(); ripples.clear(); shake = flash = dim = 0; zoom = zoomTarget = 1; focusing = false; }

void Update(float dt) {
    clock += dt;
    for (auto& s : splashes) s.life -= dt;
    splashes.erase(std::remove_if(splashes.begin(), splashes.end(), [](const Splash& s) { return s.life <= 0; }), splashes.end());
    for (auto& n : numbers) n.t += dt;
    numbers.erase(std::remove_if(numbers.begin(), numbers.end(), [](const Number& n) { return n.t > 1.3f; }), numbers.end());
    for (auto& r : ripples) r.t += dt;
    ripples.erase(std::remove_if(ripples.begin(), ripples.end(), [](const Ripple& r) { return r.t > 0.9f; }), ripples.end());
    shake = std::max(0.0f, shake - dt);
    flash = std::max(0.0f, flash - dt * 6);
    dim = std::max(0.0f, dim - dt);
    zoomTarget = focusing ? 1.03f : 1.0f;
    zoom += (zoomTarget - zoom) * std::min(1.0f, dt * (focusing ? 3.0f : 14.0f));   // a slow lean in, a snap back
    focus.x += (focusTarget.x - focus.x) * std::min(1.0f, dt * 4); focus.y += (focusTarget.y - focus.y) * std::min(1.0f, dt * 4);
}

void Hit(Vector2 at, int dmg, bool crit, bool onHero) {
    unsigned seed = (unsigned)(at.x * 13 + at.y * 7 + clock * 1000);
    splashes.push_back({at, crit ? 1.6f : 1.1f, crit ? 1.6f : 1.1f, (crit ? 1.7f : 1.0f) * (18 + std::min(20, dmg) * 1.2f), crit, false, seed});
    numbers.push_back({{at.x + (onHero ? -10.0f : 10.0f), at.y - 40}, (crit ? "CRIT " : "") + std::to_string(dmg), crit ? Color{255, 220, 110, 255} : onHero ? Color{235, 70, 60, 255} : Color{245, 150, 110, 255}, 0, crit ? 34.0f : 28.0f, true});
    float amp = std::clamp(2.0f + dmg * 0.25f, 2.0f, 4.0f) * (crit ? 2.0f : 1.0f);
    shake = std::max(shake, 0.25f); shakeAmp = std::max(shakeAmp * (shake > 0.1f ? 1.0f : 0.0f), amp);
    focusing = false;   // the blow lands: the camera snaps back
    if (crit) { flash = 1; dim = 0.2f; }
}
void Miss(Vector2 at, const char* word) { numbers.push_back({{at.x, at.y - 30}, word, Color{220, 220, 210, 255}, 0, 22, false}); }
void Heal(Vector2 at, int amount) {
    numbers.push_back({{at.x, at.y - 30}, "+" + std::to_string(amount), Color{140, 240, 150, 255}, 0, 26, false});
    splashes.push_back({at, 1.2f, 1.2f, 30, false, true, (unsigned)(clock * 977)});
}
void Nerves(Vector2 at, int amount) {
    ripples.push_back({at, 0, amount > 0 ? Color{120, 170, 255, 255} : Color{200, 230, 255, 255}});
    if (amount != 0) numbers.push_back({{at.x + 24, at.y - 60}, (amount > 0 ? "+" : "") + std::to_string(amount) + " nerves", amount > 0 ? Color{150, 170, 255, 255} : Color{200, 230, 255, 255}, 0, 18, false});
}
void Word(Vector2 at, const char* word, Color c) { numbers.push_back({{at.x, at.y - 70}, word, c, 0, 17, false}); }
void Focus(Vector2 at, bool on) { focusing = on; if (on) focusTarget = at; }

void DrawWorld() {
    for (auto& s : splashes) {
        float u = 1 - s.life / s.max, a = std::min(1.0f, s.life / (s.max * 0.4f));
        if (s.gold) { // a heal: warm light rising, and ink flecks turning gold
            Glow({s.at.x, s.at.y - u * 60}, 70, Color{255, 210, 120, (unsigned char)(90 * a)});
            for (int k = 0; k < 10; k++) {
                float ang = Hash(s.seed + k) * 2 * PI, r = 20 + Hash(s.seed + k * 7) * 30;
                Vector2 p{s.at.x + cosf(ang) * r * 0.6f, s.at.y - u * (50 + Hash(s.seed + k * 3) * 60) + sinf(ang) * r * 0.3f};
                Color c = u < 0.35f ? Color{20, 16, 14, (unsigned char)(220 * a)} : Color{255, 214, 110, (unsigned char)(230 * a)};
                DrawCircleV(p, 2.2f, c);
            }
            continue;
        }
        // an ink splash: a blot, spatter thrown out along the blow, a few drips
        Color ink = s.crit ? Color{150, 20, 18, 255} : Color{110, 18, 16, 255};
        float grow = std::min(1.0f, u * 8);
        DrawCircleV(s.at, s.size * 0.5f * grow, Fade(ink, 0.85f * a));
        for (int k = 0; k < (s.crit ? 16 : 9); k++) {
            float ang = Hash(s.seed + k) * 2 * PI, dist = s.size * (0.4f + Hash(s.seed + k * 5) * 1.1f) * grow;
            Vector2 p{s.at.x + cosf(ang) * dist, s.at.y + sinf(ang) * dist * 0.7f + u * 30 * Hash(s.seed + k * 9)};
            DrawCircleV(p, (1.5f + Hash(s.seed + k * 11) * 4) * (s.crit ? 1.4f : 1), Fade(ink, 0.9f * a));
        }
        for (int k = 0; k < 3; k++) DrawLineEx({s.at.x - 8 + k * 8.0f, s.at.y}, {s.at.x - 8 + k * 8.0f, s.at.y + u * (20 + k * 12)}, 2.5f, Fade(ink, 0.8f * a));
    }
    for (auto& r : ripples) { // nerves: a cold ripple
        float u = r.t / 0.9f;
        DrawRing(r.at, 20 + u * 60, 23 + u * 60, 0, 360, 36, Fade(r.col, 0.6f * (1 - u)));
        DrawRing(r.at, 8 + u * 36, 10 + u * 36, 0, 360, 28, Fade(r.col, 0.4f * (1 - u)));
    }
}

void DrawOverlay() {
    for (auto& n : numbers) {
        float a = std::clamp((1.3f - n.t) / 0.35f, 0.0f, 1.0f);
        Vector2 p = n.at;
        if (n.drop) { // drops like a weight onto the target, bounces once, then settles
            float fall = std::min(1.0f, n.t / 0.22f);
            p.y += -40 * (1 - fall * fall);
            if (n.t > 0.22f && n.t < 0.45f) p.y -= sinf((n.t - 0.22f) / 0.23f * PI) * 9;
        } else p.y -= n.t * 34;
        int w = MeasureTxt(n.text, (int)n.size, true);
        TxtBold(n.text, p.x - w / 2 + 2, p.y + 2, (int)n.size, Color{0, 0, 0, (unsigned char)(220 * a)});
        TxtBold(n.text, p.x - w / 2, p.y, (int)n.size, Fade(n.col, a));
    }
    if (dim > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, dim * 1.5f));
    if (flash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(WHITE, 0.55f * flash));
}

void DrawStatus(Vector2 feet, float height, const Status& st, bool marked, float t) {
    Vector2 top{feet.x, feet.y - height};
    if (st.bleedTurns > 0) for (int k = 0; k < 3; k++) { // blood drips from the figure
        float ph = fmodf(t * 0.9f + k * 0.33f, 1.0f);
        Vector2 p{feet.x - 12 + k * 12.0f, feet.y - height * 0.55f + ph * height * 0.5f};
        DrawCircleV(p, 2.6f, Fade(Color{170, 20, 20, 255}, 1 - ph));
        DrawTri({p.x - 2.6f, p.y}, {p.x + 2.6f, p.y}, {p.x, p.y - 6}, Fade(Color{170, 20, 20, 255}, 1 - ph));
    }
    if (st.poisonTurns > 0) for (int k = 0; k < 4; k++) { // poison beads green and rises
        float ph = fmodf(t * 0.6f + k * 0.25f, 1.0f);
        DrawCircleV({feet.x - 16 + k * 10.0f + sinf(t * 3 + k) * 3, feet.y - height * 0.3f - ph * height * 0.5f}, 3.2f * (1 - ph * 0.5f), Fade(Color{120, 220, 80, 255}, 0.8f * (1 - ph)));
    }
    if (st.stunned > 0) for (int k = 0; k < 3; k++) { // small brass gears spin round the head
        float a = t * 2.5f + k * 2.09f;
        Vector2 p{top.x + cosf(a) * 26, top.y - 10 + sinf(a) * 7};
        DrawGear(p, 6, 6, t * 5 * (k % 2 ? 1 : -1), Pal::Brass);
    }
    if (marked) { // an ink crosshair hangs over it
        Vector2 c{top.x, top.y + height * 0.3f};
        float r = 16 + 2 * sinf(t * 4);
        DrawRing(c, r, r + 2.5f, 0, 360, 32, Color{20, 12, 10, 230});
        for (int k = 0; k < 4; k++) { float a = k * PI / 2; DrawLineEx({c.x + cosf(a) * (r - 6), c.y + sinf(a) * (r - 6)}, {c.x + cosf(a) * (r + 9), c.y + sinf(a) * (r + 9)}, 2.5f, Color{20, 12, 10, 230}); }
    }
}

float Zoom() { return zoom; }
Vector2 FocusPoint() { return focus; }
Vector2 Offset() {
    // a slow drift so the frame never freezes, and the shake of a blow
    Vector2 o{sinf(clock * 0.21f) * 3.0f, cosf(clock * 0.17f) * 2.0f};
    if (shake > 0) { float k = shake / 0.25f * shakeAmp; o.x += sinf(clock * 71) * k; o.y += cosf(clock * 57) * k * 0.7f; }
    return o;
}
float Dim() { return dim; }
}  // namespace cfx
