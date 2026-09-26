// ============================================================================
//  DEPTH - rendering: textures generated in code, lighting, post-processing,
//  shared props (pipes, gauges, gears) and the shaded crew figures.
//
//  Each frame is drawn into an offscreen image, then presented through a
//  shader that adds a little bloom, color grading, a vignette and film grain.
//  Scenes light themselves with a lightmap: LightsBegin(ambient), AddLight()
//  for every lamp, then LightsEnd() multiplies it over what's been drawn.
// ============================================================================
#include "game.h"
#include "relics.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
struct ArtState {
    Font body{}, bold{};
    bool ownBody = false, ownBold = false;
    Texture2D glow{}, tex[4]{};
    RenderTexture2D scene{}, final{}, light{}, ocean{}, pixel{}, fig{}, temp{}, backdrop{};
    Shader post{}, figShader{}, ink{}, blur{};
    int locBlurTexel = -1;
    int locTime = -1, locRes = -1, locVig = -1, locGrain = -1, locBloom = -1;
    int locFigTexel = -1, locFigOutline = -1, locInkRes = -1, locInkAmt = -1, locInkHatch = -1;
    float vignette = 0.45f, grain = 0.03f, bloom = 0.35f;
    bool lightsOpen = false;
};
ArtState A;
constexpr int LIGHT_DIV = 2; // the lightmap is half resolution: softer and cheaper
// The scene (and the character canvas) are drawn at twice the screen resolution and scaled down when
// presented: every edge is anti-aliased, so nothing looks pixelated outside the retro platform levels.
constexpr int SS = 2;
constexpr int FIG_W = 340, FIG_H = 440;
const Vector2 FIG_FEET = {FIG_W / 2.0f, FIG_H - 24.0f}; // where a figure's feet go on its canvas

// Characters are drawn onto their own canvas, then composited through this shader: it inks a thick
// outline around the silhouette and along the boundaries between parts, rolls the edges of the shape
// into shadow and catches light on the edges facing the lamp. That is what gives them volume.
const char* FIG_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uTexel;
uniform float uOutline;
out vec4 finalColor;
const vec3 INK = vec3(0.055, 0.042, 0.036);
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    vec2 uv = fragTexCoord;
    vec4 c = texture(texture0, uv);
    float o = 0.0, nearA = 0.0;
    float wv = 0.8 + 0.55 * hash(floor(uv / uTexel / 5.0)); // the ink line wavers in width, as if drawn by hand
    for (int i = 0; i < 16; i++) {
        float a = float(i) * 0.3927;
        vec2 d = vec2(cos(a), sin(a)) * uTexel;
        float heavy = 1.0 + 0.45 * (cos(a) + sin(a)) * 0.5;   // heavier on the shadow side
        o = max(o, texture(texture0, uv + d * uOutline * wv * heavy).a);
        nearA += texture(texture0, uv + d * 3.0).a;
    }
    nearA /= 16.0;
    if (c.a < 0.5) { finalColor = vec4(INK, o * fragColor.a); return; }
    vec3 l = texture(texture0, uv - vec2(uTexel.x, 0.0)).rgb, r = texture(texture0, uv + vec2(uTexel.x, 0.0)).rgb;
    vec3 u = texture(texture0, uv + vec2(0.0, uTexel.y)).rgb, dn = texture(texture0, uv - vec2(0.0, uTexel.y)).rgb;
    float edge = length(l - r) + length(u - dn);
    vec3 col = c.rgb;
    col *= mix(0.92, 1.0, smoothstep(0.45, 0.95, nearA));         // the form turns away at its edges
    float toward = texture(texture0, uv + vec2(-3.0, 3.0) * uTexel).a;
    float away = texture(texture0, uv + vec2(3.0, -3.0) * uTexel).a;
    col *= 1.0 + 0.25 * (away - toward);                          // light from the upper left
    // a thin, cool rim of reflected light along the shadowed edge, as in Darkest Dungeon's portraits
    float rimEdge = 1.0 - texture(texture0, uv + vec2(2.0, -1.0) * uTexel).a;
    col += vec3(0.05, 0.08, 0.09) * rimEdge * (1.0 - toward * 0.5);
    col = mix(col, INK, smoothstep(0.35, 0.9, edge) * 0.75);      // linework between parts
    // a little painted texture, so surfaces read as cloth, skin and metal rather than flat colour
    vec2 cell = floor(uv / uTexel / 2.0);
    col *= 0.94 + 0.1 * fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
    // hand-inked finish: a sickly, desaturated maritime palette, flat cel bands instead of gradients, and a
    // solid black block shadow along the edge facing away from the key light (upper left)
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(lum), col, 1.15) * 1.32;                 // keep the class colours vivid and lifted
    col = floor(col * 6.0 + 0.5) / 6.0;                     // flat comic bands, not darkening
    float e1 = texture(texture0, uv + vec2(2.5, 2.2) * uTexel).a, e2 = texture(texture0, uv + vec2(7.0, 6.0) * uTexel).a;
    // cross-hatching in the mid-tones and deeper shadow, cloth folds, grit, salt and rust
    vec2 px = uv / uTexel;
    float lum2 = dot(col, vec3(0.299, 0.587, 0.114));
    float h1 = step(0.80, fract((px.x + px.y) / 4.5)), h2 = step(0.80, fract((px.x - px.y) / 4.5));
    col *= 1.0 - 0.32 * h1 * smoothstep(0.30, 0.10, lum2);   // hatching only in the truly dark tones
    col *= 1.0 - 0.32 * h2 * smoothstep(0.16, 0.05, lum2);
    float fold = step(0.86, fract((px.x * 0.8 + px.y * 0.45) / 11.0 + hash(floor(px / 23.0)) * 0.5));
    col *= 1.0 - 0.09 * fold;
    float gr = hash(floor(px));
    col *= 1.0 - 0.10 * step(0.93, gr);
    col += 0.10 * step(0.992, gr);
    float rn = hash(floor(px / 9.0)) * 0.6 + hash(floor(px / 3.0)) * 0.4;
    col = mix(col, vec3(0.34, 0.17, 0.09), 0.12 * smoothstep(0.84, 0.95, rn));
    // strong directional rim light on the edges facing the lamp (upper left): gold, then pale cyan
    float rimA = 1.0 - texture(texture0, uv + vec2(-3.5, -3.0) * uTexel).a;
    float rimB = 1.0 - texture(texture0, uv + vec2(-6.5, -5.5) * uTexel).a;
    col = mix(col, vec3(1.0, 0.86, 0.52), rimA * 0.75);
    col = mix(col, vec3(0.62, 0.9, 1.0), rimB * (1.0 - rimA) * 0.4);
    if (e1 < 0.5) col = mix(col * 0.28, INK, 0.5);  // a thin, sharp shadow edge on the far side only: the fill keeps its colour
    else if (e2 < 0.5) col *= 0.82;
    finalColor = vec4(col * fragColor.rgb, fragColor.a);
}
)";

// A Darkest Dungeon-style finish for painted backgrounds: ink along edges, crosshatching in the
// shadows, a fixed canvas grain and a slightly muted palette.
const char* INK_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uRes;
uniform float uInk;
uniform float uHatch;
out vec4 finalColor;
float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float L(vec2 o) { return lum(texture(texture0, fragTexCoord + o / uRes).rgb); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    vec3 c = texture(texture0, fragTexCoord).rgb;
    float gx = -L(vec2(-1, 1)) - 2.0 * L(vec2(-1, 0)) - L(vec2(-1, -1)) + L(vec2(1, 1)) + 2.0 * L(vec2(1, 0)) + L(vec2(1, -1));
    float gy = -L(vec2(-1, -1)) - 2.0 * L(vec2(0, -1)) - L(vec2(1, -1)) + L(vec2(-1, 1)) + 2.0 * L(vec2(0, 1)) + L(vec2(1, 1));
    float ink = smoothstep(0.1, 0.4, length(vec2(gx, gy))) * uInk;
    float l = lum(c);
    vec2 px = fragTexCoord * uRes;
    float h1 = step(0.8, fract((px.x + px.y) / 6.0)) * (1.0 - smoothstep(0.03, 0.1, l));
    float h2 = step(0.8, fract((px.x - px.y) / 6.0)) * (1.0 - smoothstep(0.015, 0.05, l));
    float hatch = max(h1, h2) * uHatch;
    vec3 col = c * (1.0 - ink * 0.7) * (1.0 - hatch * 0.5);
    col += (hash(floor(px / 2.0)) - 0.5) * 0.04;
    col = mix(vec3(lum(col)) * vec3(1.05, 1.0, 0.92), col, 0.86);
    finalColor = vec4(col, 1.0);
}
)";

// A soft gaussian blur, for distant scenery (depth of field).
const char* BLUR_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uTexel;
out vec4 finalColor;
void main() {
    vec4 s = vec4(0.0);
    float w = 0.0;
    for (int x = -3; x <= 3; x++)
        for (int y = -3; y <= 3; y++) {
            float k = exp(-float(x * x + y * y) / 6.0);
            s += texture(texture0, fragTexCoord + vec2(float(x), float(y)) * uTexel) * k;
            w += k;
        }
    finalColor = vec4((s / w).rgb, 1.0) * fragColor;
}
)";

const char* POST_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float uTime;
uniform vec2 uRes;
uniform float uVignette;
uniform float uGrain;
uniform float uBloom;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 bright(vec2 uv) { return max(texture(texture0, uv).rgb - vec3(0.6), vec3(0.0)); }
void main() {
    vec2 uv = fragTexCoord;
    vec3 c = texture(texture0, uv).rgb;
    if (uBloom > 0.0) {
        vec2 px = 1.0 / uRes;
        vec3 b = vec3(0.0);
        for (int i = 0; i < 12; i++) {
            float a = float(i) * 0.5236;
            vec2 d = vec2(cos(a), sin(a)) * px;
            b += bright(uv + d * 4.0) + bright(uv + d * 11.0) * 0.7 + bright(uv + d * 22.0) * 0.4;
        }
        c += b / 12.0 * uBloom;
    }
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, c * vec3(0.9, 1.0, 1.08), (1.0 - l) * 0.3);  // cool shadows
    c = mix(c, c * vec3(1.07, 1.0, 0.9), l * 0.25);          // warm highlights
    c = (c - 0.5) * 1.05 + 0.5;
    vec2 d = uv - 0.5;
    d.x *= uRes.x / uRes.y;
    c *= mix(1.0 - uVignette, 1.0, smoothstep(1.05, 0.3, length(d)));
    c += (hash(uv * uRes + fract(uTime * 7.0) * 311.0) - 0.5) * uGrain;
    finalColor = vec4(c, 1.0);
}
)";

// ------------------------------------------------------------- tileable noise
float Hash(int x, int y, int seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}

// Value noise that wraps every px / py cells, so textures tile without seams.
float Noise(float x, float y, int px, int py, int seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto H = [&](int ix, int iy) { return Hash(((ix % px) + px) % px, ((iy % py) + py) % py, seed); };
    float a = H(x0, y0), b = H(x0 + 1, y0), c = H(x0, y0 + 1), d = H(x0 + 1, y0 + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// Fractal noise over a 0..1 texture coordinate, with `px` x `py` cells at the base octave.
float Fbm(float u, float v, int px, int py, int seed, int octaves = 4) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum += Noise(u * px, v * py, px, py, seed + o * 17) * amp;
        norm += amp;
        amp *= 0.5f;
        px *= 2;
        py *= 2;
    }
    return sum / norm;
}

unsigned char C8(float v) { return (unsigned char)std::clamp(v * 255.0f, 0.0f, 255.0f); }

template <typename F>
Texture2D MakeTexture(int w, int h, bool tile, F fn) {
    Color* px = (Color*)MemAlloc(w * h * sizeof(Color));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[y * w + x] = fn(x, y);
    Image img{px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (tile) {
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    } else {
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
    }
    return t;
}

Color Grey(float v) { return {C8(v), C8(v), C8(v), 255}; }

// Riveted hull plating: 2x2 plates per tile, bevelled seams, stains and streaks.
Color MetalPixel(int x, int y) {
    float u = x / 256.0f, v = y / 256.0f;
    float val = 0.66f + (Fbm(u, v, 6, 6, 11, 5) - 0.5f) * 0.3f + (Fbm(u, v, 2, 24, 23, 3) - 0.5f) * 0.1f;
    float stain = Fbm(u, v, 3, 3, 29, 3);
    if (stain > 0.58f) val -= (stain - 0.58f) * 0.9f;
    int px = x % 128, py = y % 128;
    if (px < 2 || py < 2) val += 0.16f;
    else if (px > 125 || py > 125) val -= 0.3f;
    auto rivet = [&](float cx, float cy) {
        float dx = px - cx, dy = py - cy, d = sqrtf(dx * dx + dy * dy);
        if (d < 4.0f) val = 0.66f + (-dx - dy) * 0.055f;
        else if (d < 5.5f) val -= 0.14f;
    };
    for (int k = 0; k < 4; k++) {
        rivet(16 + 32.0f * k, 8); rivet(16 + 32.0f * k, 120);
        rivet(8, 16 + 32.0f * k); rivet(120, 16 + 32.0f * k);
    }
    return Grey(val);
}

// Deck planks: four planks per tile with grain, gaps, butt joints and nails.
Color WoodPixel(int x, int y) {
    float u = x / 256.0f, v = y / 256.0f;
    int row = y / 64;
    float val = 0.62f + (Hash(row, 3, 5) - 0.5f) * 0.16f;
    val += (Fbm(u, v, 2, 48, 41, 4) - 0.5f) * 0.36f;
    val += (Fbm(u, v, 8, 8, 43, 3) - 0.5f) * 0.1f;
    int ry = y % 64;
    if (ry < 2) val -= 0.34f;
    else if (ry == 2) val += 0.08f;
    int jx = (row * 97 + 40) % 256;
    if (abs(x - jx) < 2) val -= 0.3f;
    for (int k = -1; k <= 1; k += 2) {
        float dx = x - (jx + k * 8.0f), dy1 = ry - 12.0f, dy2 = ry - 52.0f;
        if (dx * dx + dy1 * dy1 < 5 || dx * dx + dy2 * dy2 < 5) val = 0.3f;
    }
    return {C8(val), C8(val * 0.74f), C8(val * 0.52f), 255};
}

// Parchment: near-white so it can be tinted to any panel color.
Color PaperPixel(int x, int y) {
    float u = x / 256.0f, v = y / 256.0f;
    float val = 0.93f + (Fbm(u, v, 4, 4, 61, 5) - 0.5f) * 0.16f + (Fbm(u, v, 32, 3, 67, 2) - 0.5f) * 0.04f;
    return {C8(val), C8(val * 0.98f), C8(val * 0.94f), 255};
}

// Cave rock: layered noise with ridges.
Color RockPixel(int x, int y) {
    float u = x / 256.0f, v = y / 256.0f;
    float n = Fbm(u, v, 5, 5, 71, 5);
    float ridge = 1.0f - fabsf(Fbm(u, v, 3, 3, 73, 4) - 0.5f) * 2.0f;
    float val = 0.35f + n * 0.4f + ridge * ridge * 0.25f;
    return {C8(val * 0.92f), C8(val * 0.98f), C8(val), 255};
}

bool LoadFirstFont(Font& f, const char* const* paths, int count, int size) {
    for (int i = 0; i < count; i++) {
        if (!FileExists(paths[i])) continue;
        f = LoadFontEx(paths[i], size, nullptr, 0);
        if (f.texture.id == 0) continue;
        GenTextureMipmaps(&f.texture);
        SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
        return true;
    }
    return false;
}
}  // namespace

// ============================================================= setup
void InitArt() {
    const char* body[] = {"C:/Windows/Fonts/georgia.ttf", "/System/Library/Fonts/Supplemental/Georgia.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/TTF/DejaVuSerif.ttf"};
    const char* bold[] = {"C:/Windows/Fonts/georgiab.ttf", "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf"};
    A.ownBody = LoadFirstFont(A.body, body, 4, 48);
    if (!A.ownBody) A.body = GetFontDefault();
    A.ownBold = LoadFirstFont(A.bold, bold, 4, 64);
    if (!A.ownBold) A.bold = A.body;

    A.glow = MakeTexture(128, 128, false, [](int x, int y) {
        float dx = (x - 63.5f) / 64.0f, dy = (y - 63.5f) / 64.0f;
        float d = std::min(1.0f, sqrtf(dx * dx + dy * dy));
        float a = (1 - d) * (1 - d);
        return Color{255, 255, 255, C8(a)};
    });
    A.tex[(int)Tex::Metal] = MakeTexture(256, 256, true, MetalPixel);
    A.tex[(int)Tex::Wood] = MakeTexture(256, 256, true, WoodPixel);
    A.tex[(int)Tex::Paper] = MakeTexture(256, 256, true, PaperPixel);
    A.tex[(int)Tex::Rock] = MakeTexture(256, 256, true, RockPixel);

    A.scene = LoadRenderTexture(SCREEN_W * SS, SCREEN_H * SS);
    A.final = LoadRenderTexture(SCREEN_W, SCREEN_H);
    A.light = LoadRenderTexture(SCREEN_W / LIGHT_DIV, SCREEN_H / LIGHT_DIV);
    A.ocean = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(A.scene.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(A.light.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(A.ocean.texture, TEXTURE_FILTER_BILINEAR);
    A.pixel = LoadRenderTexture(PIXEL_W + 2, PIXEL_H + 2); // a pixel of margin allows smooth sub-pixel scrolling
    SetTextureFilter(A.pixel.texture, TEXTURE_FILTER_POINT); // chunky pixels when scaled up
    A.fig = LoadRenderTexture(FIG_W * SS, FIG_H * SS);
    SetTextureFilter(A.fig.texture, TEXTURE_FILTER_BILINEAR);
    A.temp = LoadRenderTexture(SCREEN_W * SS, SCREEN_H * SS);
    A.backdrop = LoadRenderTexture(SCREEN_W / 2, SCREEN_H / 2);
    SetTextureFilter(A.backdrop.texture, TEXTURE_FILTER_BILINEAR);
    A.blur = LoadShaderFromMemory(nullptr, BLUR_FS);
    A.locBlurTexel = GetShaderLocation(A.blur, "uTexel");

    A.post = LoadShaderFromMemory(nullptr, POST_FS);
    A.locTime = GetShaderLocation(A.post, "uTime");
    A.locRes = GetShaderLocation(A.post, "uRes");
    A.locVig = GetShaderLocation(A.post, "uVignette");
    A.locGrain = GetShaderLocation(A.post, "uGrain");
    A.locBloom = GetShaderLocation(A.post, "uBloom");
    A.figShader = LoadShaderFromMemory(nullptr, FIG_FS);
    A.locFigTexel = GetShaderLocation(A.figShader, "uTexel");
    A.locFigOutline = GetShaderLocation(A.figShader, "uOutline");
    A.ink = LoadShaderFromMemory(nullptr, INK_FS);
    A.locInkRes = GetShaderLocation(A.ink, "uRes");
    A.locInkAmt = GetShaderLocation(A.ink, "uInk");
    A.locInkHatch = GetShaderLocation(A.ink, "uHatch");
}

void UnloadArt() {
    if (A.ownBody) UnloadFont(A.body);
    if (A.ownBold) UnloadFont(A.bold);
    UnloadTexture(A.glow);
    for (auto& t : A.tex) UnloadTexture(t);
    UnloadRenderTexture(A.scene);
    UnloadRenderTexture(A.final);
    UnloadRenderTexture(A.light);
    UnloadRenderTexture(A.ocean);
    UnloadRenderTexture(A.pixel);
    UnloadRenderTexture(A.fig);
    UnloadRenderTexture(A.temp);
    UnloadRenderTexture(A.backdrop);
    UnloadShader(A.blur);
    UnloadShader(A.post);
    UnloadShader(A.figShader);
    UnloadShader(A.ink);
}

const Font& BodyFont() { return A.body; }
const Font& BoldFont() { return A.bold; }
Texture2D GetTex(Tex t) { return A.tex[(int)t]; }
RenderTexture2D& OceanRT() { return A.ocean; }
RenderTexture2D& PixelRT() { return A.pixel; }

// ============================================================= frame
void SetPost(float vignette, float grain, float bloom) {
    A.vignette = vignette;
    A.grain = grain;
    A.bloom = bloom;
}

// A pushed transform scales each vertex as it's submitted (a plain matrix change would apply to the whole
// pending batch when it flushes). It must be popped before leaving the target, which EndTarget does.
static bool targetScaled = false;
static void PushScale() {
    rlPushMatrix();
    rlScalef((float)SS, (float)SS, 1);
    targetScaled = true;
}
static void EndTarget() {
    if (targetScaled) { rlPopMatrix(); targetScaled = false; }
    EndTextureMode();
}
void EnterScene() {
    BeginTextureMode(A.scene);
    PushScale(); // everything is drawn in screen units onto the double-size scene
}

void BeginFrame() {
    EnterScene();
    ClearBackground(Pal::SeaDeep);
}

void EndFrame(float time) {
    if (A.lightsOpen) LightsEnd();
    EndTarget();

    BeginTextureMode(A.final);
    float res[2] = {(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(A.post, A.locTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locRes, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(A.post, A.locVig, &A.vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locGrain, &A.grain, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locBloom, &A.bloom, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(A.post);
    DrawTexturePro(A.scene.texture, {0, 0, (float)SCREEN_W * SS, -(float)SCREEN_H * SS}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTarget();

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureRec(A.final.texture, {0, 0, (float)SCREEN_W, -(float)SCREEN_H}, {0, 0}, WHITE);
    EndDrawing();
}

// Draw into another render texture for a while (the ocean, the pixel-art platformer), then come back.
void BeginLayer(RenderTexture2D& rt) {
    if (A.lightsOpen) LightsEnd();
    EndTarget();
    BeginTextureMode(rt);
}

void EndLayer() {
    EndTarget();
    EnterScene();
}

// Distant scenery: drawn (in screen units) into a half-size backdrop, then laid into the scene through
// a blur, so it sits out of focus behind whatever is drawn sharply in front of it.
void BeginBackdrop() {
    BeginLayer(A.backdrop);
    ClearBackground(BLACK);
    rlPushMatrix();
    rlScalef(0.5f, 0.5f, 1);
    targetScaled = true;
}

void EndBackdrop(float blur) {
    EndLayer();
    float texel[2] = {blur / (SCREEN_W / 2.0f), blur / (SCREEN_H / 2.0f)};
    SetShaderValue(A.blur, A.locBlurTexel, texel, SHADER_UNIFORM_VEC2);
    BeginShaderMode(A.blur);
    DrawTexturePro(A.backdrop.texture, {0, 0, SCREEN_W / 2.0f, -SCREEN_H / 2.0f}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

// raylib culls triangles wound the "wrong" way; drawing both windings means any order works.
void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

Image GrabFrame() {
    Image img = LoadImageFromTexture(A.final.texture);
    ImageFlipVertical(&img);
    return img;
}

bool SaveFrameShot(const char* path) {
    Image img = LoadImageFromTexture(A.final.texture);
    ImageFlipVertical(&img);
    bool ok = ExportImage(img, path);
    UnloadImage(img);
    return ok;
}

// ============================================================= lighting
void LightsBegin(Color ambient) {
    EndTarget();
    BeginTextureMode(A.light);
    ClearBackground(ambient);
    BeginBlendMode(BLEND_ADDITIVE);
    A.lightsOpen = true;
}

void AddLight(Vector2 p, float r, Color c, float k) {
    if (!A.lightsOpen) return;
    float s = 1.0f / LIGHT_DIV;
    Color cc{C8(c.r / 255.0f * k), C8(c.g / 255.0f * k), C8(c.b / 255.0f * k), 255};
    DrawTexturePro(A.glow, {0, 0, 128, 128}, {(p.x - r) * s, (p.y - r) * s, 2 * r * s, 2 * r * s}, {0, 0}, 0, cc);
}

void AddCone(Vector2 o, float angle, float spread, float length, Color c) {
    if (!A.lightsOpen) return;
    float s = 1.0f / LIGHT_DIV;
    const int SEG = 16;
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < SEG; i++) {
        float a0 = angle - spread + 2 * spread * i / SEG, a1 = angle - spread + 2 * spread * (i + 1) / SEG;
        // fade toward the cone's edges as well as its tip
        float e0 = 1.0f - fabsf((float)i / SEG * 2 - 1), e1 = 1.0f - fabsf((float)(i + 1) / SEG * 2 - 1);
        float em = (e0 + e1) * 0.5f;
        rlColor4ub(C8(c.r / 255.0f * em), C8(c.g / 255.0f * em), C8(c.b / 255.0f * em), 255);
        rlVertex2f(o.x * s, o.y * s);
        rlColor4ub(0, 0, 0, 255);
        rlVertex2f((o.x + cosf(a0) * length) * s, (o.y + sinf(a0) * length) * s);
        rlVertex2f((o.x + cosf(a1) * length) * s, (o.y + sinf(a1) * length) * s);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void LightsEnd() {
    if (!A.lightsOpen) return;
    EndBlendMode();
    EndTarget();
    EnterScene();
    BeginBlendMode(BLEND_MULTIPLIED);
    DrawTexturePro(A.light.texture, {0, 0, (float)A.light.texture.width, -(float)A.light.texture.height},
                   {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    EndBlendMode();
    A.lightsOpen = false;
}

void Glow(Vector2 p, float r, Color c) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTexturePro(A.glow, {0, 0, 128, 128}, {p.x - r, p.y - r, 2 * r, 2 * r}, {0, 0}, 0, c);
    EndBlendMode();
}

// ============================================================= textured shapes
void DrawTiled(Tex t, Rectangle dst, float scale, Color tint, Vector2 off) {
    DrawTexturePro(A.tex[(int)t], {off.x, off.y, dst.width / scale, dst.height / scale}, dst, {0, 0}, 0, tint);
}

// Fills a circle with whatever the texture shows at the same screen position (used for windows).
// Uses quads (one wedge each, with a repeated centre vertex): rlgl keeps the bound texture for
// quads, but resets it to plain white whenever the batch switches to triangles.
void DrawTexturedCircle(Texture2D tex, Vector2 c, float r, bool flipY) {
    const int SEG = 56;
    rlDisableBackfaceCulling();
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (int i = 0; i < SEG; i++) {
        float a0 = i * 2 * PI / SEG, a1 = (i + 1) * 2 * PI / SEG;
        Vector2 p[4] = {c, {c.x + cosf(a1) * r, c.y + sinf(a1) * r}, {c.x + cosf(a0) * r, c.y + sinf(a0) * r}, c};
        for (auto& q : p) {
            float u = q.x / tex.width, v = q.y / tex.height;
            rlTexCoord2f(u, flipY ? 1.0f - v : v);
            rlVertex2f(q.x, q.y);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void DrawVGradient(Rectangle r, Color top, Color bottom) {
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)ceilf(r.width), (int)ceilf(r.height), top, bottom);
}

// ============================================================= props
void DrawPipeH(float x1, float x2, float y, float r, Color base) {
    Color dk = ColorBrightness(base, -0.6f), hi = ColorBrightness(base, 0.5f);
    DrawVGradient({x1, y - r, x2 - x1, r}, dk, base);
    DrawVGradient({x1, y, x2 - x1, r}, base, dk);
    DrawRectangleRec({x1, y - r * 0.55f, x2 - x1, std::max(1.0f, r * 0.2f)}, Fade(hi, 0.65f));
}

void DrawPipeV(float x, float y1, float y2, float r, Color base) {
    Color dk = ColorBrightness(base, -0.6f), hi = ColorBrightness(base, 0.5f);
    DrawRectangleGradientH((int)(x - r), (int)y1, (int)r, (int)(y2 - y1), dk, base);
    DrawRectangleGradientH((int)x, (int)y1, (int)r, (int)(y2 - y1), base, dk);
    DrawRectangleRec({x - r * 0.55f, y1, std::max(1.0f, r * 0.2f), y2 - y1}, Fade(hi, 0.65f));
}

void DrawFlange(Vector2 c, float r, bool vertical, Color base) {
    Rectangle f = vertical ? Rectangle{c.x - r * 1.35f, c.y - r * 0.35f, r * 2.7f, r * 0.7f}
                           : Rectangle{c.x - r * 0.35f, c.y - r * 1.35f, r * 0.7f, r * 2.7f};
    Color dk = ColorBrightness(base, -0.5f);
    DrawRectangleRec(f, base);
    DrawRectangleLinesEx(f, 1.5f, dk);
    for (int k = -1; k <= 1; k += 2) {
        Vector2 b = vertical ? Vector2{c.x + k * r * 1.05f, c.y} : Vector2{c.x, c.y + k * r * 1.05f};
        DrawCircleV(b, std::max(1.5f, r * 0.14f), dk);
    }
}

void DrawGauge(Vector2 c, float r, float needle, Color face) {
    DrawCircleV({c.x + 2, c.y + 3}, r + 4, Fade(BLACK, 0.35f));
    DrawCircleV(c, r + 4, Pal::BrassDk);
    DrawRing(c, r, r + 4, 0, 360, 40, Pal::Brass);
    DrawCircleV(c, r, face);
    for (int k = 0; k <= 8; k++) {
        float a = (135 + k * 33.75f) * DEG2RAD;
        DrawLineEx({c.x + cosf(a) * r * 0.72f, c.y + sinf(a) * r * 0.72f}, {c.x + cosf(a) * r * 0.9f, c.y + sinf(a) * r * 0.9f},
                   k % 4 == 0 ? 2.0f : 1.0f, Pal::Ink);
    }
    float a = (135 + std::clamp(needle, 0.0f, 1.0f) * 270) * DEG2RAD;
    DrawLineEx(c, {c.x + cosf(a) * r * 0.8f, c.y + sinf(a) * r * 0.8f}, 2, Color{180, 40, 30, 255});
    DrawCircleV(c, r * 0.12f + 1, Pal::BrassDk);
    DrawCircleSector(c, r * 0.92f, 200, 250, 12, Fade(WHITE, 0.18f));
}

void DrawGear(Vector2 c, float r, int teeth, float rot, Color col) {
    Color dk = ColorBrightness(col, -0.4f);
    for (int k = 0; k < teeth; k++) {
        float a = rot + k * 2 * PI / teeth;
        DrawRectanglePro({c.x + cosf(a) * r, c.y + sinf(a) * r, r * 0.3f, r * 0.26f}, {r * 0.15f, r * 0.13f}, a * RAD2DEG, col);
    }
    DrawCircleV(c, r * 0.95f, col);
    DrawRing(c, r * 0.72f, r * 0.8f, 0, 360, 36, dk);
    for (int k = 0; k < 5; k++) {
        float a = rot + k * 2 * PI / 5 + 0.6f;
        DrawCircleV({c.x + cosf(a) * r * 0.45f, c.y + sinf(a) * r * 0.45f}, r * 0.14f, dk);
    }
    DrawCircleV(c, r * 0.2f, dk);
    DrawCircleV({c.x - r * 0.05f, c.y - r * 0.05f}, r * 0.1f, ColorBrightness(col, 0.3f));
}

// A relic's icon is chosen from its name, so each carries a distinct little picture: a wrench, a
// blade, a pistol, or a medical bag, tinted a touch differently per relic so even two wrenches read
// as separate items.
void DrawRelicIcon(int id, Vector2 c, float s) {
    const auto& relics = Relics();
    if (id < 0 || id >= (int)relics.size()) { DrawCircleV(c, 0.4f * s, Pal::Brass); return; }
    if (RelicSpriteGenerator::Has(id)) { // the hand-inked SVG icon
        Texture2D tx = RelicSpriteGenerator::Sprite(id);
        float z = s * 1.2f;
        DrawTexturePro(tx, {0, 0, (float)tx.width, -(float)tx.height}, {c.x - z / 2, c.y - z / 2, z, z}, {0, 0}, 0, WHITE);
        return;
    }
    const std::string& n = relics[id].name;
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    unsigned h = (unsigned)id * 2654435761u;
    Color tint{(unsigned char)(150 + (h >> 3 & 63)), (unsigned char)(120 + (h >> 9 & 55)), (unsigned char)(70 + (h >> 15 & 50)), 255};
    if (has("Wrench")) {
        Vector2 a{c.x - 0.55f * s, c.y - 0.55f * s}, b{c.x + 0.5f * s, c.y + 0.5f * s};
        DrawLineEx(a, b, 0.3f * s, ColorBrightness(tint, -0.1f));
        DrawRing(a, 0.26f * s, 0.42f * s, 190, 350, 8, tint);
        DrawRing(b, 0.26f * s, 0.42f * s, 10, 170, 8, tint);
    } else if (has("Sword") || has("Knife") || has("Butcher")) {
        DrawTri({c.x, c.y - 0.62f * s}, {c.x - 0.13f * s, c.y + 0.24f * s}, {c.x + 0.13f * s, c.y + 0.24f * s}, Color{212, 216, 222, 255});
        DrawRectangleRec({c.x - 0.24f * s, c.y + 0.2f * s, 0.48f * s, 0.1f * s}, Pal::BrassDk);
        DrawRectangleRec({c.x - 0.06f * s, c.y + 0.32f * s, 0.12f * s, 0.32f * s}, Color{92, 60, 36, 255});
    } else if (has("Flintlock") || has("Six-Shooter") || has("Rivet Gun")) {
        DrawRectangleRec({c.x - 0.5f * s, c.y - 0.06f * s, 0.7f * s, 0.16f * s}, Color{72, 72, 78, 255});
        DrawRectangleRec({c.x - 0.5f * s, c.y - 0.08f * s, 0.12f * s, 0.06f * s}, tint);
        DrawRectangleRec({c.x - 0.08f * s, c.y + 0.06f * s, 0.2f * s, 0.3f * s}, Color{92, 60, 36, 255});
    } else if (has("MedKit") || has("Syringe") || has("Pliers") || has("Backpack")) {
        DrawRectangleRounded({c.x - 0.44f * s, c.y - 0.34f * s, 0.88f * s, 0.68f * s}, 0.3f, 6, Color{222, 222, 212, 255});
        DrawRectangleRounded({c.x - 0.44f * s, c.y - 0.34f * s, 0.88f * s, 0.2f * s}, 0.5f, 6, tint);
        DrawRectangleRec({c.x - 0.06f * s, c.y - 0.14f * s, 0.12f * s, 0.4f * s}, Color{190, 40, 36, 255});
        DrawRectangleRec({c.x - 0.22f * s, c.y + 0.0f * s, 0.44f * s, 0.12f * s}, Color{190, 40, 36, 255});
    } else {
        ShadeBall(c, 0.4f * s, tint);
        DrawRing(c, 0.36f * s, 0.42f * s, 0, 360, 16, ColorBrightness(tint, -0.4f));
        DrawLineEx({c.x, c.y - 0.4f * s}, {c.x, c.y - 0.55f * s}, 0.08f * s, ColorBrightness(tint, -0.3f)); // a loop to hang it by
    }
}

void DrawItemIcon(ItemKind kind, int relicId, Vector2 c, float s) {
    if (kind != ItemKind::Relic && RelicSpriteGenerator::HasItem((int)kind)) { // aged brass, stained linen, rusted iron
        Texture2D tx = RelicSpriteGenerator::ItemSprite((int)kind);
        float z = s * 1.2f;
        DrawTexturePro(tx, {0, 0, (float)tx.width, -(float)tx.height}, {c.x - z / 2, c.y - z / 2, z, z}, {0, 0}, 0, WHITE);
        return;
    }
    switch (kind) {
        case ItemKind::Battery:
            DrawRectangleRounded({c.x - 0.3f * s, c.y - 0.46f * s, 0.6f * s, 0.92f * s}, 0.25f, 6, Color{58, 62, 58, 255});
            DrawRectangleRounded({c.x - 0.24f * s, c.y - 0.38f * s, 0.48f * s, 0.72f * s}, 0.2f, 6, Color{92, 156, 82, 255});
            DrawRectangleRec({c.x - 0.1f * s, c.y - 0.56f * s, 0.2f * s, 0.12f * s}, Color{40, 44, 40, 255});
            DrawRectangleRec({c.x - 0.03f * s, c.y - 0.2f * s, 0.06f * s, 0.28f * s}, Color{230, 255, 220, 255});
            DrawRectangleRec({c.x - 0.14f * s, c.y - 0.09f * s, 0.28f * s, 0.06f * s}, Color{230, 255, 220, 255});
            break;
        case ItemKind::Bandage:
            DrawCircleV(c, 0.42f * s, Color{234, 228, 210, 255});
            DrawRing(c, 0.3f * s, 0.42f * s, 0, 360, 20, Color{212, 202, 176, 255});
            DrawRectangleRec({c.x - 0.4f * s, c.y - 0.09f * s, 0.8f * s, 0.18f * s}, Color{202, 42, 38, 255});
            DrawRectangleRec({c.x - 0.09f * s, c.y - 0.4f * s, 0.18f * s, 0.8f * s}, Color{202, 42, 38, 255});
            break;
        case ItemKind::Key: {
            Vector2 bow{c.x - 0.18f * s, c.y - 0.22f * s};
            DrawRing(bow, 0.16f * s, 0.28f * s, 0, 360, 16, Pal::Brass);
            DrawLineEx({c.x, c.y - 0.06f * s}, {c.x + 0.42f * s, c.y + 0.36f * s}, 0.1f * s, Pal::Brass);
            DrawLineEx({c.x + 0.3f * s, c.y + 0.24f * s}, {c.x + 0.3f * s, c.y + 0.4f * s}, 0.08f * s, Pal::Brass);
            DrawLineEx({c.x + 0.42f * s, c.y + 0.36f * s}, {c.x + 0.42f * s, c.y + 0.5f * s}, 0.08f * s, Pal::Brass);
        } break;
        default: // Relic
            DrawRelicIcon(relicId, c, s);
            break;
    }
}

void DrawBrassPlate(Rectangle r, const char* text, int size) {
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.25f, 6, Fade(BLACK, 0.45f));
    DrawRectangleRounded(r, 0.25f, 6, Pal::BrassDk);
    DrawRectangleRounded({r.x + 2, r.y + 2, r.width - 4, r.height - 4}, 0.25f, 6, Pal::Brass);
    DrawRectangleRounded({r.x + 3, r.y + 3, r.width - 6, r.height * 0.45f}, 0.3f, 6, Fade(WHITE, 0.16f));
    for (int k = 0; k < 2; k++) {
        Vector2 s{k ? r.x + r.width - 9 : r.x + 9, r.y + r.height / 2};
        DrawCircleV(s, 3, Pal::BrassDk);
        DrawLineEx({s.x - 2, s.y - 2}, {s.x + 2, s.y + 2}, 1, Pal::Ink);
    }
    int w = MeasureTxt(text, size, true);
    float tx = r.x + (r.width - w) / 2, ty = r.y + (r.height - size) / 2;
    TxtBold(text, tx, ty + 1, size, Fade(WHITE, 0.35f));
    TxtBold(text, tx, ty, size, Color{58, 38, 18, 255});
}

void DrawShadowBlob(Vector2 feet, float w) {
    DrawEllipse((int)feet.x, (int)feet.y, w, w * 0.2f, Fade(BLACK, 0.4f));
    DrawEllipse((int)feet.x, (int)feet.y, w * 0.6f, w * 0.12f, Fade(BLACK, 0.3f));
}

// ============================================================= painterly passes
void InkPass(float ink, float hatch) {
    if (A.lightsOpen) LightsEnd();
    EndTarget();
    BeginTextureMode(A.temp);
    float res[2] = {(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(A.ink, A.locInkRes, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(A.ink, A.locInkAmt, &ink, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.ink, A.locInkHatch, &hatch, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(A.ink);
    DrawTextureRec(A.scene.texture, {0, 0, (float)SCREEN_W * SS, -(float)SCREEN_H * SS}, {0, 0}, WHITE);
    EndShaderMode();
    EndTarget();
    BeginTextureMode(A.scene);
    DrawTextureRec(A.temp.texture, {0, 0, (float)SCREEN_W * SS, -(float)SCREEN_H * SS}, {0, 0}, WHITE);
    PushScale();
}

Vector2 FigureFeet() { return FIG_FEET; }

// A transparent canvas for painting flat art (a bookcase, a console) that is then mapped onto a wall.
RenderTexture2D& ArtRT() {
    static RenderTexture2D rt = LoadRenderTexture(512, 768);
    return rt;
}

void BeginCanvas(RenderTexture2D& rt) {
    BeginLayer(rt);
    ClearBackground(BLANK);
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
}

void EndCanvas() {
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndBlendMode();
    EndLayer();
}

// Figures are drawn on a separate transparent canvas. The blend mode keeps its alpha solid wherever
// shading is layered on top, and culling is off so shapes can be wound either way.
void BeginFigure() {
    BeginLayer(A.fig);
    ClearBackground(BLANK);
    PushScale(); // characters are painted at double resolution too
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
}

void EndFigure(Vector2 feet, Color tint) {
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndBlendMode();
    EndLayer();
    float texel[2] = {1.0f / FIG_W, 1.0f / FIG_H}, outline = 2.6f;
    SetShaderValue(A.figShader, A.locFigTexel, texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(A.figShader, A.locFigOutline, &outline, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(A.figShader);
    DrawTexturePro(A.fig.texture, {0, 0, (float)FIG_W * SS, -(float)FIG_H * SS},
                   {roundf(feet.x - FIG_FEET.x), roundf(feet.y - FIG_FEET.y), (float)FIG_W, (float)FIG_H}, {0, 0}, 0, tint);
    EndShaderMode();
}

// ============================================================= shaded forms
// Light comes from the upper left. Limbs are shaded like cylinders, heads and joints like spheres,
// torsos with a lit side and a shadow side, so every part reads as a solid form.
static const Vector2 TO_LIGHT = {-0.55f, -0.83f};

Color Tone(Color c, float k) {
    if (k < 0) return ColorBrightness(c, std::max(-0.95f, k * 0.8f));
    k = std::min(k, 1.0f) * 0.5f;
    auto up = [k](unsigned char v, float to) { return (unsigned char)std::clamp(v + (to - v) * k, 0.0f, 255.0f); };
    return {up(c.r, 255), up(c.g, 242), up(c.b, 220), c.a};
}

static void Vtx(Vector2 p, Color c) {
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlVertex2f(p.x, p.y);
}

void ShadeBall(Vector2 c, float r, Color col) {
    DrawCircleV(c, r, Tone(col, -0.5f));
    Color lit = Tone(col, 0.15f);
    DrawCircleGradient((int)(c.x + TO_LIGHT.x * r * 0.32f), (int)(c.y + TO_LIGHT.y * r * 0.32f), r * 0.82f, lit, Fade(lit, 0));
}

void ShadeLimb(Vector2 a, Vector2 b, float wa, float wb, Color c) {
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    ShadeBall(a, wa, c);
    ShadeBall(b, wb, c);
    if (len < 0.01f) return;
    Vector2 n{-dy / len, dx / len};
    float face = n.x * TO_LIGHT.x + n.y * TO_LIGHT.y;
    auto shade = [&](float t) {
        float nz = sqrtf(std::max(0.0f, 1 - t * t));
        return Tone(c, t * face * 0.8f + nz * 0.3f - 0.15f - (1 - nz) * 0.35f);
    };
    const int S = 6;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < S; i++) {
        float t0 = -1 + 2.0f * i / S, t1 = -1 + 2.0f * (i + 1) / S;
        Color c0 = shade(t0), c1 = shade(t1);
        Vector2 A0{a.x + n.x * wa * t0, a.y + n.y * wa * t0}, A1{a.x + n.x * wa * t1, a.y + n.y * wa * t1};
        Vector2 B0{b.x + n.x * wb * t0, b.y + n.y * wb * t0}, B1{b.x + n.x * wb * t1, b.y + n.y * wb * t1};
        Vtx(A0, c0); Vtx(A1, c1); Vtx(B1, c1);
        Vtx(A0, c0); Vtx(B1, c1); Vtx(B0, c0);
    }
    rlEnd();
}

// A four-cornered panel (torso, coat, apron) lit from the left: rim, highlight band, core shadow.
void ShadeQuad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c) {
    const float U[4] = {0, 0.28f, 0.62f, 1}, K[4] = {-0.1f, 0.2f, -0.12f, -0.55f};
    auto lerp = [](Vector2 a, Vector2 b, float u) { return Vector2{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; };
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 3; i++) {
        Vector2 t0 = lerp(tl, tr, U[i]), t1 = lerp(tl, tr, U[i + 1]), b0 = lerp(bl, br, U[i]), b1 = lerp(bl, br, U[i + 1]);
        Color ct0 = Tone(c, K[i] + 0.06f), ct1 = Tone(c, K[i + 1] + 0.06f), cb0 = Tone(c, K[i] - 0.18f), cb1 = Tone(c, K[i + 1] - 0.18f);
        Vtx(t0, ct0); Vtx(t1, ct1); Vtx(b1, cb1);
        Vtx(t0, ct0); Vtx(b1, cb1); Vtx(b0, cb0);
    }
    rlEnd();
}

// ============================================================= crew figures
// About 165 px tall at scale 1, standing on `ft`. `walk` is a phase in radians; 0 means standing in a
// ready stance. All offsets below are written facing right and mirrored for facing left. `pose` bends
// the figure for combat animations: leaning, crouching, raising or thrusting the weapon arm.
void DrawCrewFigure(const Hero& h, Vector2 ft, float s, bool right, float walk, float t, const Pose& pose) {
    float f = right ? 1.0f : -1.0f, x = ft.x;
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{226, 186, 152, 255}, {198, 150, 112, 255}, {160, 110, 78, 255}, {108, 74, 52, 255}};
    const Color hairs[5] = {{52, 36, 28, 255}, {128, 80, 44, 255}, {28, 24, 22, 255}, {186, 148, 92, 255}, {120, 116, 112, 255}};
    Color skin = skins[seed % 4], hair = hairs[(seed / 5) % 5], skinDk = Tone(skin, -0.3f);
    bool walking = walk != 0;
    float sw = sinf(walk), lift = walking ? std::max(0.0f, cosf(walk)) : 0, liftB = walking ? std::max(0.0f, -cosf(walk)) : 0;
    float br = sinf(t * 2.0f + h.id) * 0.8f;
    float tj = pose.tremble * 1.8f; // fear and strain make the hands and head shake
    Vector2 shake{sinf(t * 47 + h.id) * tj, cosf(t * 39 + h.id * 2) * tj * 0.7f};
    float y = ft.y - (walking ? fabsf(cosf(walk)) * 2.5f * s : 0);
    // P maps a point written in "facing right, feet at 0" units to the screen, applying the pose:
    // the upper body tilts about the hips, and crouching lowers everything above the feet.
    auto P = [&](float dx, float dy) {
        float up = std::clamp((-dy - 8) / 78.0f, 0.0f, 1.0f);
        float lean = pose.lean * std::max(0.0f, -dy - 86) * 0.35f;
        float head = std::clamp((-dy - 136) / 6.0f, 0.0f, 1.0f); // the head can bow, snap back and shake
        return Vector2{x + (dx + lean + pose.crouch * 4 * up + head * (pose.headDown * 4 + shake.x)) * s * f,
                       y + (dy + pose.crouch * 16 * up + head * (pose.headDown * 6 + shake.y)) * s};
    };
    auto Q = [&](Vector2 bt, Vector2 fT, Vector2 fb, Vector2 bb, Color c) {
        if (f > 0) ShadeQuad(bt, fT, fb, bb, c); else ShadeQuad(fT, bt, bb, fb, c);
    };
    auto L = [](Vector2 a, Vector2 b, float k) { return Vector2{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}; };
    auto ease = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };

    Color top, legs, boots{44, 32, 26, 255}, sleeve, glove = skin, trim;
    switch (h.cls) {
        case HeroClass::Nurse:    top = {72, 94, 124, 255}; legs = {46, 42, 48, 255}; sleeve = top; trim = {230, 226, 214, 255}; break;
        case HeroClass::Diver:    top = {140, 114, 74, 255}; legs = top; boots = {96, 90, 84, 255}; sleeve = top; glove = {74, 58, 44, 255}; trim = Pal::BrassDk; break;
        case HeroClass::Captain:  top = {40, 50, 82, 255}; legs = {36, 34, 42, 255}; sleeve = top; trim = Pal::Brass; glove = {220, 214, 200, 255}; break;
        case HeroClass::Mechanic: top = {226, 112, 30, 255}; legs = {206, 96, 26, 255}; sleeve = top; trim = {70, 66, 64, 255}; glove = {66, 60, 56, 255}; boots = {60, 56, 52, 255}; break;
        case HeroClass::Whaler:   top = {84, 108, 138, 255}; legs = {64, 70, 84, 255}; boots = {92, 62, 36, 255}; sleeve = top; glove = {110, 76, 44, 255}; trim = {126, 88, 50, 255}; break;
        case HeroClass::Stowaway: top = {124, 94, 62, 255}; legs = {84, 72, 52, 255}; boots = {58, 48, 38, 255}; sleeve = top; glove = skin; trim = {176, 48, 46, 255}; break;
        case HeroClass::Merman:   top = {34, 168, 176, 255}; legs = {26, 132, 146, 255}; boots = {20, 104, 120, 255}; sleeve = top; glove = skin; trim = Pal::BrassDk; break;
        case HeroClass::Queen:    top = {150, 108, 170, 255}; legs = {92, 70, 112, 255}; boots = {70, 55, 62, 255}; sleeve = top; glove = {200, 190, 210, 255}; trim = {202, 172, 92, 255}; break;
        case HeroClass::Robot:    top = {198, 196, 190, 255}; legs = {150, 146, 140, 255}; boots = {90, 85, 80, 255}; sleeve = top; glove = {142, 110, 60, 255}; trim = Pal::Brass; break;
        case HeroClass::Wisp:     top = {150, 236, 226, 255}; legs = {120, 214, 214, 255}; boots = {96, 190, 204, 255}; sleeve = top; glove = {190, 250, 244, 255}; trim = {230, 255, 250, 255}; break;
        case HeroClass::Octopus:  top = {176, 60, 150, 255}; legs = {146, 50, 128, 255}; boots = {120, 40, 108, 255}; sleeve = top; glove = {190, 80, 164, 255}; trim = {232, 214, 226, 255}; break;
        case HeroClass::Siren:    top = {232, 112, 122, 255}; legs = {36, 118, 122, 255}; boots = {30, 96, 100, 255}; sleeve = top; glove = skin; trim = {244, 168, 160, 255}; break;
        default:                  top = {182, 222, 222, 255}; legs = {142, 192, 192, 255}; boots = {122, 172, 172, 255}; sleeve = top; glove = {202, 232, 232, 255}; trim = WHITE; break; // Wisp
    }
    bool npc = h.outfit >= 0; // the Nautilus's own hands wear uniforms instead of expedition gear
    switch (h.outfit) {
        case OUT_HELMSMAN:  top = {58, 72, 66, 255}; legs = {38, 38, 44, 255}; sleeve = top; trim = Pal::Brass; glove = {70, 50, 36, 255}; break;
        case OUT_RADIO:     top = {150, 140, 108, 255}; legs = {64, 58, 50, 255}; sleeve = top; trim = {120, 110, 88, 255}; break;
        case OUT_ENGINEER:  top = {120, 110, 96, 255}; legs = {64, 82, 112, 255}; sleeve = top; trim = {90, 80, 70, 255}; glove = {44, 40, 36, 255}; break;
        case OUT_PROFESSOR: top = {112, 76, 52, 255}; legs = {72, 62, 52, 255}; sleeve = top; trim = {206, 196, 176, 255}; break;
        case OUT_STEWARD:   top = {232, 230, 222, 255}; legs = {30, 30, 36, 255}; sleeve = top; trim = {40, 40, 46, 255}; glove = {240, 240, 236, 255}; break;
        case OUT_ORDERLY:   top = {196, 210, 204, 255}; legs = {84, 92, 92, 255}; sleeve = top; trim = {232, 232, 228, 255}; break;
        default: break;
    }
    Color brass = Pal::Brass, steel{176, 180, 188, 255};
    float bootW = h.cls == HeroClass::Diver ? 8.5f : 6.8f;

    auto leg = [&](float hx, float fx, float up, Color col) {
        Vector2 hip = P(hx, -86), foot = P(fx, -7 - up);
        Vector2 knee = P((hx + fx) * 0.5f + 5 + pose.crouch * 8, -46 - up * 0.5f + pose.crouch * 4);
        ShadeLimb(hip, knee, 9.5f * s, 7.8f * s, col);
        ShadeLimb(knee, foot, 7.6f * s, 5.6f * s, col);
        DrawLineEx(L(hip, knee, 0.3f), L(hip, knee, 0.85f), 1.0f * s, Tone(col, -0.35f)); // trouser crease
        Vector2 heel = P(fx - 3, -5 - up), toe = P(fx + 10, -4 - up);
        ShadeLimb(heel, toe, bootW * s, (bootW - 1.2f) * s, boots);
        DrawLineEx({heel.x - f * 2 * s, heel.y + bootW * 0.8f * s}, {toe.x + f * 3 * s, toe.y + (bootW - 1.5f) * 0.8f * s}, 1.6f * s, Tone(boots, -0.6f)); // sole
        DrawCircleV({toe.x - f * 1 * s, toe.y - 2.5f * s}, 1.6f * s, Tone(boots, 0.5f));  // shine on the toe cap
    };
    auto arm = [&](Vector2 sh, Vector2 el, Vector2 hd, Color upper, Color lower) {
        ShadeLimb(sh, el, 7.6f * s, 6.6f * s, upper);
        ShadeLimb(el, hd, 6.6f * s, 5.2f * s, lower);
        Vector2 cuff = L(el, hd, 0.78f);
        ShadeBall(cuff, 5.8f * s, h.cls == HeroClass::Mechanic ? upper : Tone(trim, -0.1f)); // cuff / rolled sleeve
        ShadeBall(hd, 6.0f * s, glove);
        ShadeBall({hd.x + f * 3.5f * s, hd.y - 2.5f * s}, 2.6f * s, glove); // thumb
    };
    Color forearm = h.cls == HeroClass::Mechanic ? skin : sleeve;

    // --- behind the body
    if (h.cls == HeroClass::Diver) { // air hose
        Vector2 prev = P(-14, -148);
        for (int k = 1; k <= 7; k++) {
            Vector2 p = P(-16 - k * 2.0f + (k > 4 ? (k - 4) * 2.5f : 0), -146 + k * 8 + sinf(t * 1.4f + k) * 1.5f);
            ShadeLimb(prev, p, 2.2f * s, 2.2f * s, Color{58, 52, 46, 255});
            prev = p;
        }
    }
    bool helmeted = h.cls == HeroClass::Diver || h.cls == HeroClass::Robot || h.cls == HeroClass::Octopus || h.cls == HeroClass::Wisp;
    if (!helmeted) ShadeBall(P(-3, -153), 11.5f * s, hair);
    {   // the far arm
        float bR = ease(pose.backRaise);
        Vector2 sh = P(-6, -128 + br);
        Vector2 el = walking ? P(-6 - sw * 8, -100) : L(P(-6, -100), P(-10, -158), bR);
        Vector2 hd = walking ? P(-4 - sw * 17, -72) : L(P(2, -74), P(-4, -192), bR);
        hd = {hd.x + shake.x * s, hd.y + shake.y * s};
        arm(sh, el, hd, Tone(sleeve, -0.25f), Tone(forearm, -0.25f));
    }
    leg(-3, walking ? -sw * 15 - 2 : -12 - pose.stride * 8, liftB * 6, Tone(legs, -0.22f));
    if (h.cls == HeroClass::Captain) Q(P(-19, -100), P(15, -100), P(19, -34), P(-25, -34), top); // greatcoat skirts
    leg(4, walking ? sw * 15 + 3 : 13 + pose.stride * 16, lift * 6, legs);
    if (h.cls == HeroClass::Nurse) {
        Q(P(-16, -100), P(16, -100), P(24, -38), P(-23, -38), top); // skirt
        for (int k = 0; k < 3; k++) DrawLineEx(P(-8 + k * 8.0f, -96), P(-11 + k * 11.0f, -42), 1.1f * s, Tone(top, -0.35f)); // folds
        if (!npc) {
            Q(P(-5, -124), P(16, -124), P(22, -42), P(-6, -42), Color{224, 218, 202, 255}); // apron
            DrawLineEx(P(6, -100), P(8, -46), 1.0f * s, Color{190, 184, 170, 255});
        }
    }

    // --- torso
    Q(P(-18, -135), P(19, -135), P(15, -86), P(-15, -86), top);
    ShadeBall(P(-13, -129), 9 * s, sleeve);
    ShadeBall(P(14, -129), 9 * s, sleeve);
    if (npc) switch (h.outfit) {
        case OUT_HELMSMAN: // a double-breasted pea coat
            for (int k = 0; k < 3; k++) { DrawCircleV(P(4, -124 + k * 11.0f), 1.6f * s, trim); DrawCircleV(P(12, -124 + k * 11.0f), 1.6f * s, trim); }
            ShadeLimb(P(-4, -136), P(13, -136), 3 * s, 3 * s, Tone(top, 0.2f));
            break;
        case OUT_RADIO:
            ShadeLimb(P(-4, -136), P(13, -136), 2.6f * s, 2.6f * s, trim);
            Q(P(4, -122), P(12, -122), P(12, -113), P(4, -113), Tone(top, -0.2f)); // a pocket with pencils in it
            DrawLineEx(P(6, -125), P(6, -119), 1.2f * s, Color{200, 60, 40, 255});
            break;
        case OUT_ENGINEER: // blue overalls over a work shirt
            Q(P(-11, -122), P(15, -122), P(15, -86), P(-13, -86), legs);
            ShadeLimb(P(-8, -122), P(-10, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            ShadeLimb(P(11, -122), P(11, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            break;
        case OUT_PROFESSOR: // tweed coat, waistcoat, watch chain
            Q(P(0, -128), P(14, -128), P(12, -96), P(0, -96), Color{70, 54, 44, 255});
            DrawTri(P(13, -134), P(5, -134), P(10, -120), Color{226, 222, 212, 255});
            DrawLineEx(P(2, -108), P(11, -104), 1.1f * s, Pal::Brass);
            break;
        case OUT_STEWARD: // white jacket, black bow tie
            DrawTri(P(6, -136), P(12, -132), P(6, -128), trim);
            DrawTri(P(18, -136), P(12, -132), P(18, -128), trim);
            for (int k = 0; k < 3; k++) DrawCircleV(P(10, -120 + k * 10.0f), 1.5f * s, Pal::Brass);
            break;
        default: // orderly: a stethoscope around the neck
            DrawRing(P(6, -126), 5 * s, 6.2f * s, 0, 180, 12, Color{90, 90, 96, 255});
            ShadeBall(P(6, -118), 2.2f * s, Color{170, 176, 180, 255});
            break;
    }
    else switch (h.cls) {
        case HeroClass::Nurse: {
            Q(P(-4, -127), P(16, -127), P(14, -98), P(-4, -98), Color{224, 218, 202, 255});
            Color red{176, 40, 36, 255};
            ShadeLimb(P(5, -121), P(5, -111), 1.6f * s, 1.6f * s, red);
            ShadeLimb(P(0, -116), P(10, -116), 1.6f * s, 1.6f * s, red);
            ShadeLimb(P(-6, -136), P(12, -136), 3 * s, 3 * s, trim); // collar
            Q(P(-15, -101), P(15, -101), P(15, -96), P(-15, -96), Tone(top, -0.3f)); // waistband
        } break;
        case HeroClass::Diver:
            Q(P(-21, -139), P(22, -139), P(17, -117), P(-17, -117), brass); // corselet
            for (int k = 0; k < 6; k++) DrawCircleV(P(-14 + k * 6.0f, -121), 1.2f * s, Pal::BrassDk);
            DrawLineEx(P(-12, -106), P(-10, -94), 1.0f * s, Tone(top, -0.35f));
            Q(P(-16, -95), P(16, -95), P(15, -87), P(-15, -87), Color{60, 54, 48, 255}); // weight belt
            for (int k = 0; k < 3; k++) ShadeBall(P(-8 + k * 8.0f, -91), 3 * s, Color{110, 110, 118, 255});
            break;
        case HeroClass::Captain:
            DrawTri(P(13, -134), P(5, -134), P(10, -116), Color{226, 222, 212, 255});
            Q(P(4, -134), P(9, -134), P(6, -104), P(2, -104), Tone(top, 0.25f)); // lapel
            for (int k = 0; k < 3; k++) DrawCircleV(P(11, -124 + k * 10.0f), 1.6f * s, brass);
            Q(P(-16, -98), P(16, -98), P(15, -92), P(-15, -92), Color{70, 46, 28, 255});
            ShadeBall(P(11, -95), 2.6f * s, brass);
            ShadeBall(P(-13, -133), 6.5f * s, brass); // epaulettes
            ShadeBall(P(15, -133), 6.5f * s, brass);
            for (int k = 0; k < 4; k++) DrawLineEx(P(8 + k * 2.2f, -128), P(8 + k * 2.2f, -123), 1.1f * s, Pal::BrassDk);
            break;
        case HeroClass::Mechanic: // overalls, tool belt
            Q(P(-11, -122), P(15, -122), P(15, -86), P(-13, -86), legs);
            ShadeLimb(P(-8, -122), P(-10, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            ShadeLimb(P(11, -122), P(11, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            ShadeBall(P(-8, -121), 1.8f * s, brass);
            ShadeBall(P(11, -121), 1.8f * s, brass);
            Q(P(0, -114), P(9, -114), P(9, -106), P(0, -106), Tone(legs, -0.15f)); // pocket
            Q(P(-16, -94), P(16, -94), P(15, -88), P(-15, -88), Color{92, 64, 40, 255});
            Q(P(-14, -92), P(-5, -92), P(-5, -80), P(-14, -80), Color{110, 78, 50, 255}); // pouch
            break;
        case HeroClass::Whaler: // a heavy oilskin coat, a coiled line across the chest
            Q(P(-19, -137), P(20, -137), P(16, -90), P(-16, -90), Tone(top, -0.15f));
            ShadeLimb(P(-14, -132), P(10, -100), 2.4f * s, 2.4f * s, trim); // the coiled line, slung diagonally
            ShadeBall(P(-14, -132), 3 * s, trim);
            Q(P(-6, -136), P(11, -136), P(11, -132), P(-6, -132), Tone(top, 0.2f)); // wide collar
            break;
        case HeroClass::Stowaway: // an open, patched vest over a bare chest
            Q(P(-16, -134), P(-4, -134), P(-6, -90), P(-16, -90), top);
            Q(P(6, -134), P(18, -134), P(16, -90), P(4, -90), top);
            ShadeLimb(P(-13, -100), P(-9, -92), 2 * s, 2.4f * s, trim); // a flask on a strap
            ShadeBall(P(-11, -96), 3.4f * s, Tone(trim, -0.1f));
            break;
        case HeroClass::Merman: // scaled chest, a bone necklace
            for (int k = 0; k < 3; k++) for (int j = 0; j < 2; j++) DrawCircleV(P(-10 + k * 8.0f, -122 + j * 10.0f), 2 * s, Tone(top, -0.2f));
            ShadeLimb(P(-8, -128), P(8, -128), 1.6f * s, 1.6f * s, Color{224, 214, 196, 255}); // necklace
            for (int k = 0; k < 3; k++) ShadeBall(P(-4 + k * 4.0f, -122), 1.4f * s, Color{224, 214, 196, 255});
            break;
        case HeroClass::Queen: // a tattered royal sash
            Q(P(-14, -133), P(10, -128), P(6, -90), P(-10, -95), Tone(trim, -0.1f));
            DrawTri(P(6, -90), P(2, -84), P(10, -88), Tone(trim, -0.1f)); // a torn hem
            Q(P(-16, -98), P(16, -98), P(15, -92), P(-15, -92), Tone(legs, -0.3f));
            break;
        case HeroClass::Robot: // riveted plating and a small gauge
            Q(P(-16, -135), P(17, -135), P(14, -90), P(-14, -90), Tone(top, -0.1f));
            for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) DrawCircleV(P(-11 + k * 10.0f, -128 + j * 14.0f), 1.1f * s, Tone(top, -0.4f));
            ShadeBall(P(6, -112), 5 * s, Color{40, 42, 46, 255});
            DrawCircleV(P(6, -112), 3.4f * s, Color{220, 200, 100, 255});
            DrawLineEx(P(6, -112), P(8, -115), 0.9f * s, Color{40, 30, 20, 255}); // the needle
            break;
        case HeroClass::Octopus: // a boneless mantle, no real "torso" seam
            Q(P(-17, -134), P(18, -134), P(20, -95), P(-19, -95), Tone(top, -0.1f));
            for (int k = 0; k < 3; k++) DrawCircleV(P(-10 + k * 9.0f, -110), 2.2f * s, Tone(top, 0.15f));
            break;
        case HeroClass::Siren: // a coral bodice, a flowing hem
            Q(P(-15, -132), P(16, -132), P(13, -100), P(-13, -100), trim);
            Q(P(-17, -100), P(17, -100), P(24, -84), P(-24, -84), Tone(legs, 0.1f)); // flowing lower hem
            for (int k = 0; k < 3; k++) DrawLineEx(P(-10 + k * 9.0f, -98), P(-14 + k * 12.0f, -86), 1.1f * s, Tone(legs, -0.25f));
            break;
        default: // Wisp: no real body at all, just a trailing glow
            for (int k = 0; k < 4; k++) DrawCircleV(P(-6 + k * 4.0f, -100 - k * 8.0f), (4 - k * 0.6f) * s, Fade(top, 0.5f - k * 0.1f));
            break;
    }

    // --- head
    ShadeLimb(P(1, -134), P(2, -143), 5.4f * s, 5.2f * s, skinDk);
    Vector2 hd = P(3, -152);
    if (helmeted && h.cls == HeroClass::Diver) {
        ShadeBall(hd, 19.5f * s, brass);
        DrawCircleSector({hd.x - f * 7 * s, hd.y - 8 * s}, 6 * s, 0, 360, 12, Fade(WHITE, 0.35f)); // polished highlight
        ShadeBall(P(-3, -169), 3.5f * s, Pal::BrassDk);
        ShadeBall(P(12, -151), 9 * s, Pal::BrassDk);
        DrawCircleV(P(12.5f, -151), 6.4f * s, Color{18, 40, 48, 255});
        for (int k = -1; k <= 1; k++) DrawLineEx(P(8, -151 + k * 3.0f), P(17, -151 + k * 3.0f), 0.9f * s, Pal::BrassDk); // grille
        DrawCircleV(P(10.5f, -153.5f), 1.9f * s, Color{170, 225, 235, 210});
        ShadeBall(P(-6, -150), 4.5f * s, Pal::BrassDk);
        DrawCircleV(P(-6, -150), 2.6f * s, Color{18, 40, 48, 255});
        for (int k = 0; k < 7; k++) DrawCircleV(P(-15 + k * 5.0f, -137), 1.3f * s, Pal::BrassDk);
    } else if (helmeted && h.cls == HeroClass::Robot) { // a riveted brass head with a single glass eye
        ShadeBall(hd, 12.5f * s, steel);
        Q(P(-9, -164), P(11, -164), P(11, -140), P(-9, -140), Tone(steel, -0.1f));
        ShadeBall(P(8, -151), 6.5f * s, Pal::BrassDk);
        DrawCircleV(P(8, -151), 4.4f * s, Color{110, 220, 220, 255});
        DrawCircleV(P(9, -151), 1.6f * s, Color{20, 30, 30, 255});
        for (int k = 0; k < 5; k++) DrawCircleV(P(-8 + (k % 3) * 8.0f, -162 + (k / 3) * 20.0f), 1.2f * s, brass);
        ShadeLimb(P(-2, -140), P(2, -134), 2 * s, 2 * s, steel); // neck bolt
    } else if (helmeted && h.cls == HeroClass::Octopus) { // a bulbous, boneless head, no jaw
        ShadeBall(hd, 13.5f * s, top);
        DrawEllipse((int)P(8, -151).x, (int)P(8, -151).y, 3.4f * s, 3.4f * s, Color{255, 220, 120, 255});
        DrawCircleV(P(8.6f, -151), 1.8f * s, Color{20, 10, 18, 255});
        for (int k = 0; k < 5; k++) DrawLineEx(P(-9 + k * 3.5f, -142), P(-11 + k * 3.8f, -134), 1.6f * s, Tone(top, -0.2f)); // trailing mantle fronds
    } else if (helmeted) { // Wisp: a soft, faceless glow
        Glow(hd, 22 * s, Fade(trim, 0.5f));
        ShadeBall(hd, 10.5f * s, Fade(top, 0.9f));
        DrawCircleV(hd, 6 * s, Fade(WHITE, 0.7f));
    } else {
        ShadeBall(P(-2, -150), 3 * s, Tone(skin, -0.12f));  // ear
        ShadeBall(hd, 11.8f * s, skin);
        ShadeBall(P(7, -145.5f), 8 * s, skin);              // jaw
        DrawEllipse((int)P(4, -145).x, (int)P(4, -145).y, 4 * s, 2.5f * s, Fade(skinDk, 0.45f)); // under the cheekbone
        DrawCircleSector(P(-1.5f, -155), 12.4f * s, 180, 360, 18, hair);
        DrawCircleSector(P(-3, -152), 11.8f * s, f > 0 ? 90 : 0, f > 0 ? 180 : 90, 10, hair);
        for (int k = 0; k < 4; k++) DrawLineEx(P(-9 + k * 4.0f, -165), P(-7 + k * 4.5f, -157), 0.9f * s, Tone(hair, -0.35f)); // strands
        Q(P(2, -156.4f), P(12.6f, -156.4f), P(12.6f, -148.6f), P(2, -149.2f), Color{6, 8, 12, 255}); // the eyes are lost under a heavy brow shadow
        DrawCircleV(P(9.4f, -152.4f), 0.85f * s, Color{214, 220, 206, 255});                          // just a pinprick of light in it
        ShadeBall(P(12, -148), 2.6f * s, skin);              // nose
        DrawCircleV(P(12, -146.5f), 0.8f * s, Tone(skin, -0.55f));
        DrawLineEx(P(7.5f, -142.8f), P(11.5f, -143.2f), 1.3f * s, Color{150, 80, 70, 255}); // lips
        DrawLineEx(P(8, -140.5f), P(11, -140.8f), 0.8f * s, Fade(skinDk, 0.6f));
        if (npc) switch (h.outfit) {
            case OUT_HELMSMAN: // a flat sailor's cap
                Q(P(-12, -168), P(13, -168), P(13, -160), P(-12, -160), Color{30, 34, 40, 255});
                ShadeLimb(P(4, -159), P(16, -158), 1.8f * s, 1.2f * s, Color{16, 16, 20, 255});
                ShadeBall(P(6, -165), 1.8f * s, Pal::Brass);
                break;
            case OUT_RADIO: // headphones
                DrawRing(P(-1, -154), 12.5f * s, 14.5f * s, 180, 360, 16, Color{50, 44, 40, 255});
                ShadeBall(P(-2, -150), 4.5f * s, Color{70, 56, 44, 255});
                break;
            case OUT_ENGINEER: // a cloth cap, and oil on the cheek
                Q(P(-11, -168), P(12, -166), P(13, -159), P(-11, -159), Color{80, 72, 64, 255});
                ShadeLimb(P(6, -159), P(17, -158), 2 * s, 1.4f * s, Color{70, 62, 54, 255});
                DrawCircleV(P(9, -146), 2.4f * s, Fade(BLACK, 0.25f));
                break;
            case OUT_PROFESSOR: // spectacles and grey whiskers
                DrawRing(P(8, -151), 2.6f * s, 3.4f * s, 0, 360, 12, Color{200, 180, 110, 255});
                ShadeLimb(P(7, -145), P(12, -145), 2 * s, 1.5f * s, Color{190, 186, 180, 255});
                break;
            case OUT_ORDERLY:
                Q(P(-8, -164), P(10, -164), P(10, -158), P(-8, -158), Color{236, 236, 232, 255});
                break;
            default: break;
        }
        else switch (h.cls) {
            case HeroClass::Nurse:
                ShadeBall(P(-11, -155), 5 * s, hair);           // bun
                Q(P(-8, -164), P(10, -164), P(10, -157), P(-8, -157), Color{236, 232, 222, 255});
                ShadeLimb(P(1, -163), P(1, -158), 0.9f * s, 0.9f * s, Color{176, 40, 36, 255});
                break;
            case HeroClass::Captain:
                if (seed % 2) {
                    ShadeBall(P(7, -142.5f), 7.5f * s, Color{204, 200, 192, 255});
                    ShadeLimb(P(7, -145), P(13, -145), 2 * s, 1.5f * s, Color{204, 200, 192, 255});
                    for (int k = 0; k < 4; k++) DrawLineEx(P(3 + k * 3.0f, -142), P(4 + k * 3.0f, -136), 0.8f * s, Color{160, 156, 150, 255});
                }
                Q(P(-11, -171), P(12, -169), P(12, -160), P(-10, -160), Color{26, 26, 32, 255});
                Q(P(-10, -162), P(12, -162), P(12, -159), P(-10, -159), brass);
                ShadeBall(P(8, -166), 2.2f * s, brass); // cap badge
                ShadeLimb(P(5, -159), P(18, -157), 2 * s, 1.3f * s, Color{16, 16, 20, 255});
                break;
            case HeroClass::Mechanic:
                ShadeLimb(P(-9, -156), P(11, -157), 1.8f * s, 1.8f * s, Color{66, 48, 34, 255});
                ShadeBall(P(6, -159), 4 * s, brass);
                DrawCircleV(P(6.4f, -159), 2.4f * s, Color{110, 190, 196, 255});
                DrawCircleV(P(8, -146), 2.5f * s, Fade(BLACK, 0.25f));
                for (int k = 0; k < 8; k++) DrawPixelV(P(4 + (k % 4) * 2.5f, -141 + (k / 4) * 2.0f), Tone(hair, -0.2f)); // stubble
                break;
            case HeroClass::Whaler: // a wide sou'wester hat
                Q(P(-13, -165), P(14, -165), P(14, -160), P(-13, -160), trim);
                ShadeBall(P(1, -166), 12.5f * s, trim);
                ShadeLimb(P(5, -159), P(19, -157), 2 * s, 1.3f * s, Tone(trim, -0.2f));
                break;
            case HeroClass::Stowaway: // a grimy bandana, tied off at the back
                DrawCircleSector(P(-1.5f, -155), 12.5f * s, 165, 345, 14, trim);
                ShadeBall(P(-11, -156), 2.6f * s, Tone(trim, -0.2f)); // the knot
                break;
            case HeroClass::Merman: // a crest of fins instead of hair
                for (int k = 0; k < 4; k++) DrawTri(P(-9 + k * 6.0f, -160), P(-6 + k * 6.0f, -160), P(-8 + k * 6.0f, -172 - k * 1.5f), Tone(top, 0.15f));
                break;
            case HeroClass::Queen: // a tarnished, once-golden crown
                Q(P(-10, -166), P(11, -166), P(11, -160), P(-10, -160), trim);
                for (int k = 0; k < 3; k++) DrawTri(P(-8 + k * 7.0f, -166), P(-3 + k * 7.0f, -166), P(-5 + k * 7.0f, -173), trim);
                ShadeBall(P(0, -168), 1.6f * s, Color{170, 90, 160, 255}); // a single remaining jewel
                break;
            case HeroClass::Siren: // pearls woven through her hair
                for (int k = 0; k < 3; k++) ShadeBall(P(-10 + k * 7.0f, -160 - (k % 2) * 4.0f), 1.6f * s, Color{230, 220, 226, 255});
                break;
            default: break;
        }
    }

    // --- the weapon arm, in front of everything, posed by `pose`
    float rz = ease(pose.raise), rc = ease(pose.reach);
    Vector2 sh = P(6, -128 + br);
    Vector2 elIdle = walking ? P(6 + sw * 8, -100) : P(14, -102), hdIdle = walking ? P(8 + sw * 17, -74) : P(26, -84);
    Vector2 el = L(L(elIdle, P(18, -160), rz), P(34, -120), rc);
    Vector2 hand = L(L(hdIdle, P(10, -192), rz), P(60, -114), rc);
    hand = {hand.x + shake.x * s, hand.y + shake.y * s};
    arm(sh, el, hand, sleeve, forearm);
    if (npc) { // the ship's hands carry the tools of their trade, not weapons
        switch (h.outfit) {
            case OUT_ENGINEER: { // a hammer
                float a = (-40 - 100 * rz + pose.weaponTilt) * DEG2RAD;
                Vector2 end{hand.x + cosf(a) * 26 * s * f, hand.y + sinf(a) * 26 * s};
                ShadeLimb(hand, end, 2 * s, 2 * s, Color{120, 84, 50, 255});
                ShadeLimb({end.x - sinf(a) * 6 * s * f, end.y + cosf(a) * 6 * s}, {end.x + sinf(a) * 6 * s * f, end.y - cosf(a) * 6 * s}, 3.4f * s, 3.4f * s, steel);
            } break;
            case OUT_PROFESSOR: // a book
                Q({hand.x - 2 * s * f, hand.y - 16 * s}, {hand.x + 10 * s * f, hand.y - 16 * s}, {hand.x + 10 * s * f, hand.y + 2 * s}, {hand.x - 2 * s * f, hand.y + 2 * s}, Color{110, 40, 36, 255});
                break;
            case OUT_STEWARD: // a silver tray with a glass on it
                ShadeLimb({hand.x - 14 * s * f, hand.y - 5 * s}, {hand.x + 18 * s * f, hand.y - 5 * s}, 2 * s, 2 * s, Color{200, 204, 210, 255});
                ShadeLimb({hand.x + 2 * s * f, hand.y - 8 * s}, {hand.x + 2 * s * f, hand.y - 18 * s}, 2.4f * s, 3 * s, Color{200, 226, 230, 200});
                break;
            default: break;
        }
        return;
    }
    if (walking && h.cls != HeroClass::Diver) return;
    // weapon angle in degrees: 0 points straight ahead, negative points up
    float base;
    switch (h.cls) {
        case HeroClass::Nurse: base = -8.0f; break;
        case HeroClass::Diver: base = -26.0f; break;
        case HeroClass::Captain: base = -64.0f; break;
        case HeroClass::Whaler: base = -18.0f; break;
        case HeroClass::Queen: base = -14.0f; break;
        case HeroClass::Siren: base = -12.0f; break;
        case HeroClass::Wisp: base = -20.0f; break;
        case HeroClass::Octopus: base = -22.0f; break;
        default: base = 70.0f; break; // Mechanic, Stowaway, Merman, Robot: a heavier overhead swing
    }
    float ang = (base - 110 * rz + pose.weaponTilt) * DEG2RAD;
    auto W = [&](float along) { return Vector2{hand.x + cosf(ang) * along * s * f, hand.y + sinf(ang) * along * s}; };
    switch (h.cls) {
        case HeroClass::Nurse: // a large syringe
            ShadeLimb(W(-8), W(-1), 1.3f * s, 1.3f * s, steel);
            ShadeLimb(W(2), W(16), 3.2f * s, 3.2f * s, Color{186, 214, 214, 255});
            ShadeLimb(W(6), W(13), 2 * s, 2 * s, Color{120, 200, 120, 255}); // the dose
            DrawLineEx(W(16), W(28), 1.2f * s, steel);
            break;
        case HeroClass::Diver: { // harpoon
            ShadeLimb(W(-30), W(36), 1.7f * s, 1.7f * s, steel);
            Vector2 tip = W(46), a = W(36);
            Vector2 n{-sinf(ang) * 5 * s * f, cosf(ang) * 5 * s};
            DrawTri(tip, {a.x + n.x, a.y + n.y}, {a.x - n.x, a.y - n.y}, steel);
        } break;
        case HeroClass::Captain: // cutlass
            ShadeLimb(W(2), W(36), 2.4f * s, 1.2f * s, Color{206, 210, 216, 255});
            DrawLineEx(W(4), W(34), 0.8f * s, Fade(WHITE, 0.6f));
            ShadeBall(hand, 4.2f * s, brass);
            break;
        case HeroClass::Mechanic: // a heavy wrench
            ShadeLimb(W(-4), W(30), 2.8f * s, 2.6f * s, steel);
            ShadeBall(W(33), 6.5f * s, steel);
            DrawCircleV(W(36), 2.8f * s, Tone(steel, -0.6f));
            break;
        case HeroClass::Whaler: { // a stubby harpoon gun
            ShadeLimb(W(-10), W(20), 3 * s, 2.6f * s, Tone(steel, -0.2f));
            ShadeLimb(W(16), W(40), 1.3f * s, 1.3f * s, steel); // the bolt, loaded
            Vector2 tip = W(44), a = W(40);
            Vector2 n{-sinf(ang) * 3 * s * f, cosf(ang) * 3 * s};
            DrawTri(tip, {a.x + n.x, a.y + n.y}, {a.x - n.x, a.y - n.y}, steel);
        } break;
        case HeroClass::Stowaway: // a broken bottle
            ShadeLimb(W(-2), W(14), 2.6f * s, 1.6f * s, Color{60, 110, 70, 200});
            DrawTri(W(14), W(20), W(11), Color{40, 90, 55, 220});
            break;
        case HeroClass::Merman: // a bone trident
            ShadeLimb(W(-6), W(30), 1.6f * s, 1.3f * s, Color{224, 214, 196, 255});
            for (int k = -1; k <= 1; k++) DrawLineEx(W(30), {W(30).x + k * 6 * s, W(30).y - 10 * s}, 1.1f * s, Color{224, 214, 196, 255});
            break;
        case HeroClass::Queen: // a jeweled scepter
            ShadeLimb(W(-6), W(28), 1.6f * s, 1.6f * s, trim);
            ShadeBall(W(31), 3.6f * s, Color{170, 90, 160, 255});
            break;
        case HeroClass::Robot: // a heavy pneumatic drill
            ShadeLimb(W(-4), W(24), 3 * s, 2.8f * s, steel);
            for (int k = 0; k < 3; k++) ShadeBall(W(24 + k * 4.0f), 3.2f * s - k * 0.4f, Tone(steel, -0.3f));
            break;
        case HeroClass::Octopus: // a small barbed spine
            ShadeLimb(W(-4), W(20), 1.4f * s, 0.8f * s, Tone(top, -0.2f));
            break;
        case HeroClass::Siren: // a shell-tipped rod
            ShadeLimb(W(-4), W(26), 1.3f * s, 1.3f * s, trim);
            ShadeBall(W(28), 2.8f * s, Color{230, 200, 220, 255});
            break;
        default: // Wisp: no weapon at all, just a mote of light in her hand
            Glow(W(14), 16 * s, Fade(top, 0.6f));
            DrawCircleV(W(14), 2.6f * s, WHITE);
            break;
    }
}

void DrawCrewFigureInked(const Hero& h, Vector2 feet, float s, bool right, float walk, float t, const Pose& pose, Color tint) {
    BeginFigure();
    DrawCrewFigure(h, FIG_FEET, s, right, walk, t, pose);
    EndFigure(feet, tint);
}

// ============================================================================
//  The bestiary, drawn in the heavy hand-inked style: thick ink outlines (added by the figure shader), a
//  desaturated maritime palette, eyes buried in shadow under masks, brows and hoods, and solid block shadows
//  cast down and away from the key light (no soft gradients on the shadow side). Each creature is built from
//  an archetype (a humanoid, a hound, a crab, a worm, a tentacled mass, ...) dressed with its own colours
//  and accessories, so every entry in the bestiary has its own silhouette.
// ============================================================================
namespace {
enum Acc { A_NONE = 0, A_SKULL = 1, A_IDOL = 2, A_SHELLCROWN = 4, A_COCONUT = 8, A_FEATHERS = 16, A_SPEAR = 32, A_STAFF = 64, A_TRIDENT = 128,
           A_SCEPTER = 256, A_HORNS = 512, A_FINS = 1024, A_BANDAGE = 2048, A_COWL = 4096, A_GLADIUS = 8192, A_CORAL = 16384, A_VOID = 32768 };
struct Look { Color skin, cloth, accent, glow; int acc; };
const Color BLK{6, 8, 12, 255};

// a cel-shadow: a solid black block on the far side of a round mass, away from the upper-left light
void BlockShadow(Vector2 c, float rx, float ry) { // a hard crescent along the lower-right rim, not a wash
    float r = std::max(rx, ry);
    DrawRing(c, r * 0.68f, r, -25, 105, 14, BLK);
}

void Humanoid(float cx, float by, float H, const Look& L, float bulk, float lean, float t, int uid, bool tail = false) {
    float sway = sinf(t * 1.6f + uid) * 1.5f;
    float hipY = by - 0.40f * H, shY = by - 0.70f * H, headY = by - 0.85f * H, hr = 0.075f * H * (0.8f + bulk * 0.4f);
    Vector2 hip{cx + lean * 0.3f, hipY}, sh{cx + lean, shY + sway * 0.4f}, head{cx + lean * 1.4f, headY + sway * 0.6f};
    float lw = 0.055f * H * bulk, tw = 0.095f * H * bulk;
    if (tail) { // a fish tail instead of legs: siren and merman
        Vector2 prev = hip;
        for (int i = 1; i <= 6; i++) {
            Vector2 q{cx + sinf(t * 2 + i * 0.7f + uid) * 8 * i * 0.3f + lean * 0.2f, hipY + i * (by - hipY) / 6.0f};
            ShadeLimb(prev, q, lw * (1.5f - i * 0.15f), lw * (1.5f - (i + 1) * 0.15f), L.cloth);
            prev = q;
        }
        DrawTri({prev.x, prev.y}, {prev.x - 26, by + 6}, {prev.x + 4, by - 6}, Tone(L.accent, -0.2f));
        DrawTri({prev.x, prev.y}, {prev.x + 26, by + 6}, {prev.x - 4, by - 6}, L.accent);
    } else {
        ShadeLimb({hip.x - 8, hipY}, {cx - 12, by - 3}, lw, lw * 0.85f, Tone(L.cloth, -0.25f));   // legs, boots in shadow
        ShadeLimb({hip.x + 8, hipY}, {cx + 14, by - 3}, lw, lw * 0.85f, L.cloth);
        DrawRectangle((int)cx - 20, (int)by - 8, 16, 8, BLK);
        DrawRectangle((int)cx + 6, (int)by - 8, 18, 8, BLK);
    }
    ShadeLimb(hip, sh, tw, tw * 1.15f, L.cloth);                                                   // a rugged, top-heavy torso
    BlockShadow({sh.x + 4, (sh.y + hip.y) / 2}, tw * 0.9f, (hip.y - sh.y) * 0.5f);
    DrawTri({sh.x - tw * 1.3f, sh.y}, {sh.x + tw * 1.3f, sh.y}, {sh.x + tw * 0.9f, sh.y + 22}, Tone(L.accent, -0.3f)); // torn cloth at the shoulders
    Vector2 elL{sh.x - tw * 1.5f, sh.y + 0.13f * H}, elR{sh.x + tw * 1.5f, sh.y + 0.13f * H};
    ShadeLimb({sh.x - tw, sh.y + 4}, elL, lw * 0.8f, lw * 0.7f, Tone(L.skin, -0.3f));
    ShadeLimb({sh.x + tw, sh.y + 4}, elR, lw * 0.8f, lw * 0.7f, L.skin);
    ShadeBall(head, hr, L.skin);
    BlockShadow(head, hr, hr);
    // eyes never show: a mask, a hood, or a heavy brow swallows them
    if (L.acc & A_SKULL) { ShadeBall({head.x, head.y + 2}, hr * 1.12f, Color{200, 192, 170, 255}); DrawEllipse((int)head.x - 5, (int)head.y, 4, 5, BLK); DrawEllipse((int)head.x + 5, (int)head.y, 4, 5, BLK); DrawRectangle((int)head.x - 6, (int)head.y + 8, 12, 4, BLK); }
    else if (L.acc & A_IDOL) { ShadeQuad({head.x - hr * 1.5f, head.y - hr * 1.8f}, {head.x + hr * 1.5f, head.y - hr * 1.8f}, {head.x + hr * 1.3f, head.y + hr * 1.7f}, {head.x - hr * 1.3f, head.y + hr * 1.7f}, Color{112, 78, 44, 255});
                                DrawRectangle((int)(head.x - hr * 0.9f), (int)head.y - 6, (int)(hr * 0.7f), 9, BLK); DrawRectangle((int)(head.x + hr * 0.2f), (int)head.y - 6, (int)(hr * 0.7f), 9, BLK); }
    else if (L.acc & A_COWL) { DrawTri({head.x - hr * 1.5f, head.y + hr * 1.2f}, {head.x + hr * 1.5f, head.y + hr * 1.2f}, {head.x, head.y - hr * 2.2f}, Tone(L.cloth, -0.35f)); DrawEllipse((int)head.x, (int)head.y + 1, hr * 0.8f, hr * 0.9f, BLK); DrawRectangle((int)head.x - 5, (int)head.y - 1, 3, 2, Fade(L.glow, 0.9f)); DrawRectangle((int)head.x + 2, (int)head.y - 1, 3, 2, Fade(L.glow, 0.9f)); }
    else { DrawRectangle((int)(head.x - hr), (int)(head.y - hr * 0.35f), (int)(hr * 2), (int)(hr * 0.7f), BLK); } // a black bar of brow-shadow where the eyes should be
    if (L.acc & A_BANDAGE) DrawRectangle((int)(head.x - hr), (int)(head.y - hr * 0.5f), (int)(hr * 2), (int)(hr * 0.9f), Color{178, 168, 140, 255});
    if (L.acc & A_HORNS) { DrawTri({head.x - hr, head.y - hr * 0.6f}, {head.x - hr * 0.3f, head.y - hr}, {head.x - hr * 1.6f, head.y - hr * 2.2f}, Color{200, 192, 170, 255}); DrawTri({head.x + hr, head.y - hr * 0.6f}, {head.x + hr * 0.3f, head.y - hr}, {head.x + hr * 1.6f, head.y - hr * 2.2f}, Color{200, 192, 170, 255}); }
    if (L.acc & A_FEATHERS) for (int k = -2; k <= 2; k++) DrawTri({head.x + k * 5, head.y - hr}, {head.x + k * 5 + 4, head.y - hr}, {head.x + k * 9, head.y - hr * 2.6f - (k & 1) * 6}, k & 1 ? Color{150, 60, 44, 255} : Color{60, 90, 84, 255});
    if (L.acc & A_COCONUT) { DrawCircleSector({head.x, head.y - 2}, hr * 1.25f, 180, 360, 14, Color{92, 62, 36, 255}); DrawLineEx({head.x - 8, head.y - hr}, {head.x + 2, head.y - hr * 1.2f}, 2, BLK); }
    if (L.acc & A_SHELLCROWN) for (int k = -2; k <= 2; k++) DrawTri({head.x + k * 7 - 4, head.y - hr * 0.8f}, {head.x + k * 7 + 4, head.y - hr * 0.8f}, {head.x + k * 7, head.y - hr * (1.7f + (k & 1) * 0.5f)}, Color{214, 200, 176, 255});
    if (L.acc & A_FINS) { DrawTri({head.x - hr, head.y - 4}, {head.x - hr * 2.2f, head.y - 12}, {head.x - hr * 1.6f, head.y + 8}, Fade(L.accent, 0.75f)); DrawTri({head.x + hr, head.y - 4}, {head.x + hr * 2.2f, head.y - 12}, {head.x + hr * 1.6f, head.y + 8}, Fade(L.accent, 0.75f)); }
    // weapons, held in the far hand
    Vector2 hand = elR;
    if (L.acc & A_SPEAR) { DrawLineEx({hand.x + 6, by}, {hand.x + 10, headY - 0.18f * H}, 4, Color{92, 62, 36, 255}); DrawTri({hand.x + 4, headY - 0.16f * H}, {hand.x + 16, headY - 0.16f * H}, {hand.x + 10, headY - 0.32f * H}, Color{30, 32, 38, 255}); }
    if (L.acc & A_STAFF) { DrawLineEx({hand.x + 6, by}, {hand.x + 8, headY - 0.14f * H}, 4, Color{110, 84, 50, 255}); DrawCircleV({hand.x + 8, headY - 0.16f * H}, 8, L.glow); }
    if (L.acc & A_SCEPTER) { DrawLineEx({hand.x + 6, by - 10}, {hand.x + 10, headY - 0.05f * H}, 4, Color{110, 84, 50, 255}); DrawTri({hand.x + 2, headY - 0.04f * H}, {hand.x + 18, headY - 0.04f * H}, {hand.x + 10, headY - 0.2f * H}, Color{214, 120, 110, 255}); }
    if (L.acc & A_TRIDENT) { DrawLineEx({hand.x + 6, by}, {hand.x + 8, headY - 0.22f * H}, 6, Color{104, 84, 66, 255}); for (int k = -1; k <= 1; k++) DrawLineEx({hand.x + 8 + k * 9, headY - 0.22f * H}, {hand.x + 8 + k * 9, headY - 0.36f * H}, 3, Color{120, 96, 80, 255}); }
    if (L.acc & A_GLADIUS) { DrawLineEx(hand, {hand.x + 26, hand.y - 8}, 5, Color{130, 96, 60, 255}); DrawCircleV(hand, 5, BLK); }
    if (L.acc & A_CORAL) for (int k = 0; k < 4; k++) DrawTri({sh.x + k * 9 - 18, sh.y - 2}, {sh.x + k * 9 - 12, sh.y - 2}, {sh.x + k * 9 - 15, sh.y - 20 - (k & 1) * 8}, Color{180, 90, 80, 255}); // living coral fused to the stone
    if (L.acc & A_VOID) for (int k = 0; k < 3; k++) { float a = t * 1.4f + k * 2.1f; DrawRectangle((int)(hip.x + cosf(a) * 30), (int)(hip.y + sinf(a) * 8 - 4), 4, 4, L.glow); }
}

void Hound(float cx, float by, float H, const Look& L, float t, int uid) {
    float run = sinf(t * 8 + uid) * 3;
    ShadeLimb({cx - 30, by - 0.42f * H}, {cx - 34 + run, by - 3}, 6, 4, Tone(L.cloth, -0.2f));
    ShadeLimb({cx + 22, by - 0.42f * H}, {cx + 26 - run, by - 3}, 6, 4, Tone(L.cloth, -0.2f));
    ShadeLimb({cx - 24, by - 0.55f * H}, {cx + 24, by - 0.58f * H}, 0.14f * H, 0.16f * H, L.cloth);        // an emaciated body
    for (int k = 0; k < 5; k++) DrawLineEx({cx - 14 + k * 8.0f, by - 0.5f * H}, {cx - 12 + k * 8.0f, by - 0.66f * H}, 2, Tone(L.skin, 0.3f)); // exposed ribs
    ShadeLimb({cx - 28, by - 0.6f * H}, {cx - 44, by - 0.78f * H}, 4, 2, L.cloth);                          // a whip of a tail
    ShadeLimb({cx + 22, by - 0.6f * H}, {cx + 34, by - 0.78f * H}, 8, 8, L.cloth);
    ShadeBall({cx + 48, by - 0.76f * H}, 0.09f * H, L.cloth);                                                // the head, jaw dripping in shadow
    DrawTri({cx + 52, by - 0.74f * H}, {cx + 76, by - 0.68f * H}, {cx + 52, by - 0.64f * H}, L.cloth);
    DrawRectangle((int)cx + 52, (int)(by - 0.7f * H), 22, 5, BLK);
    for (int k = 0; k < 3; k++) DrawTri({cx + 56.0f + k * 6, by - 0.66f * H}, {cx + 60.0f + k * 6, by - 0.66f * H}, {cx + 58.0f + k * 6, by - 0.6f * H}, Color{150, 20, 20, 255});
    DrawRectangle((int)cx + 44, (int)(by - 0.82f * H), 12, 5, BLK);                                          // the eyes, in shadow
    DrawTri({cx + 40, by - 0.84f * H}, {cx + 46, by - 0.84f * H}, {cx + 42, by - 0.94f * H}, L.cloth);
    for (int k = 0; k < 3; k++) DrawLineEx({cx + 44.0f + k * 5, by - 0.74f * H}, {cx + 46.0f + k * 5, by - 0.66f * H}, 2, L.accent);  // ash warpaint
}

void Crab(float cx, float by, float W, const Look& L, float t, int uid, bool queen) {
    float k = W / 100.0f;
    for (int i = 0; i < 4; i++) ShadeLimb({cx - 24 * k + i * 16 * k, by - 30 * k}, {cx - 40 * k + i * 22 * k + sinf(t * 6 + i + uid) * 3, by - 2}, 3.4f * k, 2.4f * k, Tone(L.cloth, -0.3f));
    ShadeBall({cx, by - 46 * k}, 34 * k, L.cloth);                                                             // a bloated, lopsided carapace
    ShadeBall({cx + 16 * k, by - 52 * k}, 24 * k, Tone(L.cloth, 0.1f));
    BlockShadow({cx, by - 44 * k}, 34 * k, 26 * k);
    for (int i = 0; i < 6; i++) DrawLineEx({cx - 30 * k + i * 10 * k, by - 60 * k}, {cx - 20 * k + i * 10 * k, by - 40 * k}, 1.5f, BLK); // heavy cross-hatching
    ShadeLimb({cx - 30 * k, by - 50 * k}, {cx - 56 * k, by - 78 * k}, 6 * k, 9 * k, L.cloth);                  // the huge jagged left claw
    DrawTri({cx - 74 * k, by - 96 * k}, {cx - 48 * k, by - 86 * k}, {cx - 58 * k, by - 70 * k}, Tone(L.cloth, 0.05f));
    DrawTri({cx - 74 * k, by - 96 * k}, {cx - 50 * k, by - 70 * k}, {cx - 76 * k, by - 72 * k}, Tone(L.cloth, -0.3f));
    ShadeLimb({cx + 30 * k, by - 48 * k}, {cx + 44 * k, by - 62 * k}, 4 * k, 4 * k, L.cloth);                  // a small withered right claw
    for (int s = -1; s <= 1; s += 2) { DrawLineEx({cx + s * 9 * k, by - 72 * k}, {cx + s * 10 * k, by - 86 * k}, 2, L.cloth); DrawCircleV({cx + s * 10 * k, by - 88 * k}, 3.5f * k, BLK); }
    if (queen) { for (int i = -2; i <= 2; i++) DrawTri({cx + i * 12 * k - 5, by - 74 * k}, {cx + i * 12 * k + 5, by - 74 * k}, {cx + i * 12 * k, by - 96 * k - (i & 1) * 8}, Color{200, 190, 168, 255});
                   for (int i = 0; i < 5; i++) DrawLineEx({cx - 20 * k + i * 10 * k, by - 40 * k}, {cx - 24 * k + i * 10 * k + sinf(t * 3 + i) * 6, by - 8 * k}, 2.5f, L.glow); } // parasitic worms
}

void WormBody(float cx, float by, float H, const Look& L, float t, int uid, bool ghost, bool serpent) {
    int segs = 11;
    Vector2 prev{cx, by - 2};
    for (int i = 1; i <= segs; i++) {
        float u = (float)i / segs;
        float sw = serpent ? sinf(t * 2 + i * 0.9f + uid) * 22 * u : sinf(t * 1.3f + i * 0.6f + uid) * 8 * u;
        Vector2 q{cx + sw, by - u * H * 0.95f};
        float w = (serpent ? 12 : 15) * (1 - u * 0.25f);
        Color c = ghost ? Color{188, 204, 196, 255} : L.cloth;
        ShadeLimb(prev, q, w, w * 0.92f, i % 2 ? Tone(c, -0.15f) : c);
        if (serpent) DrawCircleV({q.x, q.y}, 2.5f, Fade(L.glow, 0.9f));                                       // crackling veins along the spine
        else DrawRing(q, w * 0.55f, w * 0.8f, 0, 360, 10, Fade(BLK, 0.6f));                                     // concentric rings of teeth
        prev = q;
    }
    Vector2 top = prev;
    ShadeBall(top, 17, ghost ? Color{198, 214, 206, 255} : L.cloth);
    if (ghost) { DrawEllipse((int)top.x - 6, (int)top.y - 2, 4, 6, BLK); DrawEllipse((int)top.x + 6, (int)top.y - 2, 4, 6, BLK); Glow(top, 60, Fade(L.glow, 0.5f)); }
    else if (serpent) { DrawRectangle((int)top.x - 8, (int)top.y - 5, 16, 4, BLK); DrawTri({top.x + 6, top.y}, {top.x + 20, top.y + 6}, {top.x + 6, top.y + 8}, Color{220, 212, 190, 255}); }
    else { DrawCircleV(top, 10, BLK); for (int i = 0; i < 8; i++) { float a = i * PI / 4; DrawTri({top.x + cosf(a) * 8, top.y + sinf(a) * 8}, {top.x + cosf(a + 0.3f) * 8, top.y + sinf(a + 0.3f) * 8}, {top.x + cosf(a + 0.15f) * 15, top.y + sinf(a + 0.15f) * 15}, Color{214, 206, 180, 255}); } }
}

void DiverWreck(float cx, float by, float H, const Look& L, float t) {
    ShadeLimb({cx - 16, by - 0.4f * H}, {cx - 18, by - 3}, 15, 13, L.cloth);
    ShadeLimb({cx + 16, by - 0.4f * H}, {cx + 20, by - 3}, 15, 13, Tone(L.cloth, -0.2f));
    DrawRectangle((int)cx - 34, (int)by - 12, 26, 12, BLK); DrawRectangle((int)cx + 8, (int)by - 12, 28, 12, BLK);
    ShadeBall({cx, by - 0.55f * H}, 0.26f * H, L.cloth);                                                        // a bloated, waterlogged suit
    BlockShadow({cx + 4, by - 0.55f * H}, 0.26f * H, 0.26f * H);
    ShadeLimb({cx - 0.22f * H, by - 0.66f * H}, {cx - 0.3f * H, by - 0.42f * H}, 13, 11, L.cloth);
    ShadeLimb({cx + 0.22f * H, by - 0.66f * H}, {cx + 0.32f * H, by - 0.4f * H}, 13, 11, L.cloth);
    for (int i = 0; i < 4; i++) DrawLineEx({cx + 0.32f * H + 6, by - 0.4f * H + i * 4.0f}, {cx + 0.32f * H + 30, by - 0.28f * H + i * 6.0f}, 3, Color{60, 58, 56, 255}); // a chain
    ShadeBall({cx, by - 0.84f * H}, 0.13f * H, Color{112, 88, 50, 255});                                        // the helmet
    DrawRing({cx + 8, by - 0.84f * H}, 5, 9, 0, 360, 14, BLK);
    DrawCircleV({cx + 8, by - 0.84f * H}, 5, L.glow);                                                            // a cracked faceplate leaking dim light
    DrawLineEx({cx + 4, by - 0.88f * H}, {cx + 12, by - 0.8f * H}, 1.5f, BLK);
    Glow({cx + 8, by - 0.84f * H}, 44, Fade(L.glow, 0.5f));
}

void Totem(float cx, float by, float H, const Look& L, float t) {
    float w = 0.34f * H;
    ShadeQuad({cx - w, by - 0.55f * H}, {cx + w, by - 0.55f * H}, {cx + w * 0.8f, by}, {cx - w * 0.8f, by}, L.cloth);                    // stacked black basalt
    ShadeQuad({cx - w * 0.85f, by - 0.95f * H}, {cx + w * 0.85f, by - 0.95f * H}, {cx + w, by - 0.55f * H}, {cx - w, by - 0.55f * H}, Tone(L.cloth, -0.1f));
    BlockShadow({cx + w * 0.4f, by - 0.5f * H}, w * 0.7f, 0.4f * H);
    float pulse = 0.6f + 0.4f * sinf(t * 2);
    for (int i = 0; i < 6; i++) { float x = cx - w * 0.7f + i * w * 0.28f; DrawLineEx({x, by - 0.9f * H + (i & 1) * 20}, {x + 8, by - 0.15f * H - (i % 3) * 14}, 3, Fade(L.glow, 0.55f + 0.4f * pulse)); } // magma in the cracks
    Glow({cx, by - 0.55f * H}, 0.5f * H, Fade(L.glow, 0.18f + 0.12f * pulse));
    DrawCircleV({cx, by - 0.6f * H}, 0.11f * H, Fade(L.glow, 0.95f));                                                                   // a glowing amber core
    DrawRectangle((int)(cx - w * 0.55f), (int)(by - 0.82f * H), (int)(w * 0.42f), 12, BLK); DrawRectangle((int)(cx + w * 0.13f), (int)(by - 0.82f * H), (int)(w * 0.42f), 12, BLK);
    for (int i = 0; i < 7; i++) { float a = -PI / 2 + (i - 3) * 0.32f; DrawTri({cx + cosf(a) * w * 0.9f, by - 0.98f * H + sinf(a) * 6}, {cx + cosf(a + 0.1f) * w * 0.9f, by - 0.98f * H}, {cx + cosf(a) * w * 1.5f, by - 1.1f * H + sinf(a) * 20}, Fade(L.glow, 0.85f)); } // a corona of rays
}

void TentacleMass(float cx, float by, float H, const Look& L, float t, int uid, bool cosmic) {
    int n = cosmic ? 9 : 7;
    for (int i = 0; i < n; i++) {
        float ang = PI * (0.08f + 0.84f * i / (n - 1));
        Vector2 prev{cx + cosf(ang + PI) * 0.16f * H, by - 0.38f * H};
        for (int sgm = 1; sgm <= 7; sgm++) {
            float u = sgm / 7.0f, wob = sinf(t * 2 + i + sgm * 0.7f + uid) * 8 * u;
            Vector2 q{cx + (i - (n - 1) / 2.0f) * 0.11f * H * (0.6f + u) + wob, by - 0.38f * H + u * 0.38f * H};
            ShadeLimb(prev, q, 9 * (1 - u * 0.6f), 9 * (1 - (u + 0.14f) * 0.6f), Tone(L.cloth, -0.1f * (i & 1)));
            if (sgm % 2 == 0) DrawCircleV({q.x + 3, q.y}, 2, Fade(Color{190, 200, 170, 255}, 0.8f));             // suckers dripping slime
            prev = q;
        }
    }
    ShadeBall({cx, by - 0.62f * H}, 0.26f * H, L.cloth);                                                        // the bulbous mantle, shadowed from above
    DrawEllipse((int)cx, (int)(by - 0.78f * H), 0.24f * H, 0.11f * H, Fade(BLK, 0.9f));
    BlockShadow({cx + 6, by - 0.6f * H}, 0.26f * H, 0.26f * H);
    for (int s = -1; s <= 1; s += 2) { DrawRectangle((int)(cx + s * 0.1f * H - 8), (int)(by - 0.6f * H), 16, 6, BLK); DrawRectangle((int)(cx + s * 0.1f * H - 2), (int)(by - 0.6f * H + 1), 4, 3, Fade(L.glow, 0.9f)); }
    if (cosmic) { // wings of pure shadow, and a maw of ink
        for (int s = -1; s <= 1; s += 2) { DrawTri({cx + s * 0.2f * H, by - 0.8f * H}, {cx + s * 0.75f * H, by - 1.05f * H + sinf(t) * 8}, {cx + s * 0.5f * H, by - 0.45f * H}, BLK); DrawTri({cx + s * 0.75f * H, by - 1.05f * H + sinf(t) * 8}, {cx + s * 0.85f * H, by - 0.7f * H}, {cx + s * 0.5f * H, by - 0.45f * H}, Color{14, 10, 24, 255}); }
        DrawEllipse((int)cx, (int)(by - 0.5f * H), 0.14f * H, 0.07f * H, BLK);
        Glow({cx, by - 0.6f * H}, 0.7f * H, Fade(L.glow, 0.14f));
    }
}

void Shark(float cx, float by, float W, const Look& L, float t) {
    float k = W / 110.0f, sw = sinf(t * 2) * 4;
    ShadeLimb({cx - 60 * k, by - 40 * k + sw}, {cx + 40 * k, by - 54 * k}, 16 * k, 24 * k, L.cloth);
    DrawTri({cx - 96 * k, by - 66 * k + sw}, {cx - 56 * k, by - 40 * k + sw}, {cx - 70 * k, by - 14 * k + sw}, Tone(L.cloth, -0.2f));
    DrawTri({cx - 6 * k, by - 74 * k}, {cx + 22 * k, by - 72 * k}, {cx + 4 * k, by - 110 * k}, Tone(L.cloth, -0.1f));
    ShadeLimb({cx - 50 * k, by - 30 * k + sw}, {cx + 34 * k, by - 32 * k}, 8 * k, 12 * k, Color{198, 196, 184, 255});
    BlockShadow({cx, by - 46 * k}, 50 * k, 20 * k);
    DrawRectangle((int)(cx + 30 * k), (int)(by - 60 * k), (int)(14 * k), 6, BLK);                               // pitch-black eyes
    for (int i = 0; i < 6; i++) DrawTri({cx + 34 * k + i * 6 * k, by - 44 * k}, {cx + 39 * k + i * 6 * k, by - 44 * k}, {cx + 36 * k + i * 6 * k, by - 34 * k}, Color{230, 226, 210, 255});
    DrawLineEx({cx + 50 * k, by - 48 * k}, {cx + 56 * k, by - 30 * k}, 1.5f, Color{170, 170, 176, 255});       // fishhooks in the jaw
    DrawCircleLines((int)(cx + 56 * k), (int)(by - 28 * k), 4, Color{170, 170, 176, 255});
}

void Alien(float cx, float by, float H, const Look& L, float t, int uid) {
    Vector2 c{cx, by - 0.5f * H + sinf(t * 1.5f + uid) * 5};
    for (int i = 0; i < 9; i++) { // a non-Euclidean mass: shards that don't agree which way is up
        float a = t * 0.5f * (i & 1 ? 1 : -1) + i * 0.7f, r1 = 0.32f * H, r2 = 0.14f * H;
        Vector2 p1{c.x + cosf(a) * r1, c.y + sinf(a) * r1 * 0.8f}, p2{c.x + cosf(a + 0.35f) * r2, c.y + sinf(a + 0.35f) * r2}, p3{c.x + cosf(a - 0.35f) * r2, c.y + sinf(a - 0.35f) * r2};
        DrawTri(p1, p2, p3, i % 3 ? BLK : Color{14, 10, 26, 255});
        DrawLineEx(c, p1, 2, Fade(L.glow, 0.5f + 0.4f * sinf(t * 3 + i)));                                     // sharp glowing angular tendrils
    }
    DrawCircleV(c, 0.14f * H, BLK);                                                                              // no face at all
    DrawRing(c, 0.14f * H, 0.16f * H, 0, 360, 24, Fade(L.glow, 0.8f));
    Glow(c, 0.7f * H, Fade(L.glow, 0.12f));
}
}  // namespace

void DrawBestiaryFigure(const Enemy& e, Rectangle r, float t) {
    float cx = r.x + r.width / 2, by = r.y + r.height, H = r.height, bob = sinf(t * 2.2f + e.uid) * 2;
    int u = e.uid;
    switch (e.type) {
        // ---- the Island
        case EnemyType::TribalSpearman: Humanoid(cx, by, H * 0.98f, {{150, 104, 72, 255}, {96, 82, 64, 255}, {170, 60, 44, 255}, {255, 190, 90, 255}, A_SKULL | A_SPEAR}, 0.8f, 6, t, u); break;
        case EnemyType::WarDog: Hound(cx, by, H, {{120, 108, 96, 255}, {104, 92, 82, 255}, {214, 206, 190, 255}, {255, 90, 60, 255}, 0}, t, u); break;
        case EnemyType::TribalShaman: Humanoid(cx - 4, by, H * 0.9f, {{140, 100, 70, 255}, {88, 70, 56, 255}, {60, 96, 90, 255}, {150, 230, 120, 255}, A_COWL | A_FEATHERS | A_STAFF}, 0.75f, -10, t, u); break;
        case EnemyType::TribalDemigod: Humanoid(cx, by, H * 1.0f, {{110, 84, 60, 255}, {80, 60, 42, 255}, {150, 96, 50, 255}, {255, 180, 60, 255}, A_IDOL | A_FEATHERS}, 1.7f, 10, t, u); break;
        case EnemyType::CoconutQueen: Humanoid(cx, by, H * 0.95f, {{136, 104, 82, 255}, {84, 92, 62, 255}, {190, 70, 60, 255}, {220, 110, 100, 255}, A_COCONUT | A_SCEPTER}, 1.1f, 0, t, u); break;
        case EnemyType::SunGod: Totem(cx, by, H, {{40, 36, 34, 255}, {34, 30, 32, 255}, {255, 150, 40, 255}, {255, 160, 50, 255}, 0}, t); break;
        // ---- the Cave
        case EnemyType::DysCrustacean: Crab(cx, by, r.width * 1.05f, {{130, 60, 52, 255}, {124, 66, 56, 255}, {200, 120, 90, 255}, {170, 190, 120, 255}, 0}, t, u, false); break;
        case EnemyType::CrustaceanQueen: Crab(cx, by, r.width * 1.3f, {{110, 64, 62, 255}, {112, 68, 64, 255}, {210, 190, 160, 255}, {190, 200, 130, 255}, 0}, t, u, true); break;
        case EnemyType::GhostWorm: WormBody(cx, by, H * 0.95f, {{190, 206, 198, 255}, {190, 206, 198, 255}, {230, 240, 230, 255}, {170, 240, 200, 255}, 0}, t, u, true, false); break;
        case EnemyType::LostDiver: DiverWreck(cx, by, H, {{100, 110, 96, 255}, {92, 96, 84, 255}, {130, 100, 60, 255}, {255, 214, 100, 255}, 0}, t); break;
        // ---- the Weeds
        case EnemyType::FeralMerman: Humanoid(cx, by - bob, H, {{70, 92, 90, 255}, {48, 68, 72, 255}, {90, 130, 130, 255}, {150, 230, 220, 255}, A_FINS}, 0.85f, 8, t, u, true); break;
        case EnemyType::Siren: Humanoid(cx, by - bob, H, {{150, 160, 152, 255}, {70, 84, 84, 255}, {110, 130, 122, 255}, {200, 220, 200, 255}, A_FINS | A_BANDAGE}, 0.7f, -4, t, u, true); break;
        case EnemyType::GiantOctopus: TentacleMass(cx, by, H, {{62, 46, 74, 255}, {62, 46, 74, 255}, {150, 130, 170, 255}, {230, 200, 90, 255}, 0}, t, u, false); break;
        case EnemyType::ElectricEel: WormBody(cx, by, H * 0.95f, {{60, 82, 76, 255}, {56, 76, 70, 255}, {90, 130, 120, 255}, {120, 200, 255, 255}, 0}, t, u, false, true); break;
        case EnemyType::GreatWhite: Shark(cx, by, r.width * 1.25f, {{106, 118, 122, 255}, {100, 112, 116, 255}, {200, 200, 190, 255}, {0, 0, 0, 255}, 0}, t); break;
        case EnemyType::Neptune: Humanoid(cx, by, H * 1.0f, {{74, 110, 96, 255}, {52, 82, 62, 255}, {120, 90, 60, 255}, {150, 230, 200, 255}, A_TRIDENT | A_SHELLCROWN | A_FINS}, 1.5f, 6, t, u); break;
        // ---- Atlantis
        case EnemyType::LostInfantry: Humanoid(cx, by, H * 0.98f, {{120, 122, 114, 255}, {116, 100, 78, 255}, {170, 96, 80, 255}, {150, 130, 210, 255}, A_BANDAGE | A_GLADIUS | A_CORAL}, 1.0f, 4, t, u); break;
        case EnemyType::LostCultist: Humanoid(cx, by - 24 - bob * 2, H * 0.9f, {{110, 100, 124, 255}, {58, 44, 84, 255}, {150, 100, 210, 255}, {200, 130, 255, 255}, A_COWL | A_VOID}, 0.7f, 0, t, u); break;
        case EnemyType::ArmorLostOne: Humanoid(cx, by, H * 1.0f, {{104, 120, 96, 255}, {128, 96, 56, 255}, {60, 60, 66, 255}, {160, 130, 255, 255}, A_HORNS | A_CORAL | A_VOID}, 1.9f, 4, t, u); TentacleMass(cx + 10, by - 0.62f * H, H * 0.5f, {{22, 20, 30, 255}, {22, 20, 30, 255}, {60, 50, 90, 255}, {180, 120, 255, 255}, 0}, t, u, false); break;
        case EnemyType::AlienHorror: Alien(cx, by, H, {{0, 0, 0, 255}, {0, 0, 0, 255}, {0, 0, 0, 255}, {230, 60, 220, 255}, 0}, t, u); break;
        case EnemyType::Cthulhu: TentacleMass(cx, by, H, {{34, 46, 52, 255}, {34, 46, 52, 255}, {90, 130, 120, 255}, {130, 255, 170, 255}, 0}, t, u, true); break;
        default: break;
    }
}
