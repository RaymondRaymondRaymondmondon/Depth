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
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
struct ArtState {
    Font body{}, bold{};
    bool ownBody = false, ownBold = false;
    Texture2D glow{}, tex[4]{};
    RenderTexture2D scene{}, final{}, light{}, ocean{}, pixel{}, fig{}, temp{};
    Shader post{}, figShader{}, ink{};
    int locTime = -1, locRes = -1, locVig = -1, locGrain = -1, locBloom = -1;
    int locFigTexel = -1, locFigOutline = -1, locInkRes = -1, locInkAmt = -1, locInkHatch = -1;
    float vignette = 0.45f, grain = 0.03f, bloom = 0.35f;
    bool lightsOpen = false;
};
ArtState A;
constexpr int LIGHT_DIV = 2; // the lightmap is half resolution: softer and cheaper
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
void main() {
    vec2 uv = fragTexCoord;
    vec4 c = texture(texture0, uv);
    float o = 0.0, nearA = 0.0;
    for (int i = 0; i < 16; i++) {
        float a = float(i) * 0.3927;
        vec2 d = vec2(cos(a), sin(a)) * uTexel;
        o = max(o, texture(texture0, uv + d * uOutline).a);
        nearA += texture(texture0, uv + d * 3.0).a;
    }
    nearA /= 16.0;
    if (c.a < 0.5) { finalColor = vec4(INK, o * fragColor.a); return; }
    vec3 l = texture(texture0, uv - vec2(uTexel.x, 0.0)).rgb, r = texture(texture0, uv + vec2(uTexel.x, 0.0)).rgb;
    vec3 u = texture(texture0, uv + vec2(0.0, uTexel.y)).rgb, dn = texture(texture0, uv - vec2(0.0, uTexel.y)).rgb;
    float edge = length(l - r) + length(u - dn);
    vec3 col = c.rgb;
    col *= mix(0.8, 1.0, smoothstep(0.45, 0.95, nearA));          // the form turns away at its edges
    float toward = texture(texture0, uv + vec2(-3.0, 3.0) * uTexel).a;
    float away = texture(texture0, uv + vec2(3.0, -3.0) * uTexel).a;
    col *= 1.0 + 0.25 * (away - toward);                          // light from the upper left
    // a thin, cool rim of reflected light along the shadowed edge, as in Darkest Dungeon's portraits
    float rimEdge = 1.0 - texture(texture0, uv + vec2(2.0, -1.0) * uTexel).a;
    col += vec3(0.16, 0.24, 0.32) * rimEdge * (1.0 - toward * 0.5);
    col = mix(col, INK, smoothstep(0.35, 0.9, edge) * 0.75);      // linework between parts
    // a little painted texture, so surfaces read as cloth, skin and metal rather than flat colour
    vec2 cell = floor(uv / uTexel / 2.0);
    col *= 0.94 + 0.1 * fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
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

    A.scene = LoadRenderTexture(SCREEN_W, SCREEN_H);
    A.final = LoadRenderTexture(SCREEN_W, SCREEN_H);
    A.light = LoadRenderTexture(SCREEN_W / LIGHT_DIV, SCREEN_H / LIGHT_DIV);
    A.ocean = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(A.scene.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(A.light.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(A.ocean.texture, TEXTURE_FILTER_BILINEAR);
    A.pixel = LoadRenderTexture(PIXEL_W + 2, PIXEL_H + 2); // a pixel of margin allows smooth sub-pixel scrolling
    SetTextureFilter(A.pixel.texture, TEXTURE_FILTER_POINT); // chunky pixels when scaled up
    A.fig = LoadRenderTexture(FIG_W, FIG_H);
    A.temp = LoadRenderTexture(SCREEN_W, SCREEN_H);

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

void BeginFrame() {
    BeginTextureMode(A.scene);
    ClearBackground(Pal::SeaDeep);
}

void EndFrame(float time) {
    if (A.lightsOpen) LightsEnd();
    EndTextureMode();

    BeginTextureMode(A.final);
    float res[2] = {(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(A.post, A.locTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locRes, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(A.post, A.locVig, &A.vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locGrain, &A.grain, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.post, A.locBloom, &A.bloom, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(A.post);
    DrawTextureRec(A.scene.texture, {0, 0, (float)SCREEN_W, -(float)SCREEN_H}, {0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureRec(A.final.texture, {0, 0, (float)SCREEN_W, -(float)SCREEN_H}, {0, 0}, WHITE);
    EndDrawing();
}

// Draw into another render texture for a while (the ocean, the pixel-art platformer), then come back.
void BeginLayer(RenderTexture2D& rt) {
    if (A.lightsOpen) LightsEnd();
    EndTextureMode();
    BeginTextureMode(rt);
}

void EndLayer() {
    EndTextureMode();
    BeginTextureMode(A.scene);
}

// raylib culls triangles wound the "wrong" way; drawing both windings means any order works.
void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
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
    EndTextureMode();
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
    EndTextureMode();
    BeginTextureMode(A.scene);
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
    EndTextureMode();
    BeginTextureMode(A.temp);
    float res[2] = {(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(A.ink, A.locInkRes, res, SHADER_UNIFORM_VEC2);
    SetShaderValue(A.ink, A.locInkAmt, &ink, SHADER_UNIFORM_FLOAT);
    SetShaderValue(A.ink, A.locInkHatch, &hatch, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(A.ink);
    DrawTextureRec(A.scene.texture, {0, 0, (float)SCREEN_W, -(float)SCREEN_H}, {0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();
    BeginTextureMode(A.scene);
    DrawTextureRec(A.temp.texture, {0, 0, (float)SCREEN_W, -(float)SCREEN_H}, {0, 0}, WHITE);
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
    DrawTextureRec(A.fig.texture, {0, 0, (float)FIG_W, -(float)FIG_H}, {roundf(feet.x - FIG_FEET.x), roundf(feet.y - FIG_FEET.y)}, tint);
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
    float y = ft.y - (walking ? fabsf(cosf(walk)) * 2.5f * s : 0);
    // P maps a point written in "facing right, feet at 0" units to the screen, applying the pose:
    // the upper body tilts about the hips, and crouching lowers everything above the feet.
    auto P = [&](float dx, float dy) {
        float up = std::clamp((-dy - 8) / 78.0f, 0.0f, 1.0f);
        float lean = pose.lean * std::max(0.0f, -dy - 86) * 0.35f;
        return Vector2{x + (dx + lean + pose.crouch * 4 * up) * s * f, y + (dy + pose.crouch * 16 * up) * s};
    };
    auto Q = [&](Vector2 bt, Vector2 fT, Vector2 fb, Vector2 bb, Color c) {
        if (f > 0) ShadeQuad(bt, fT, fb, bb, c); else ShadeQuad(fT, bt, bb, fb, c);
    };
    auto L = [](Vector2 a, Vector2 b, float k) { return Vector2{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}; };
    auto ease = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };

    Color top, legs, boots{44, 32, 26, 255}, sleeve, glove = skin, trim;
    switch (h.cls) {
        case HeroClass::Nurse:   top = {72, 94, 124, 255}; legs = {46, 42, 48, 255}; sleeve = top; trim = {230, 226, 214, 255}; break;
        case HeroClass::Diver:   top = {140, 114, 74, 255}; legs = top; boots = {96, 90, 84, 255}; sleeve = top; glove = {74, 58, 44, 255}; trim = Pal::BrassDk; break;
        case HeroClass::Captain: top = {40, 50, 82, 255}; legs = {36, 34, 42, 255}; sleeve = top; trim = Pal::Brass; glove = {220, 214, 200, 255}; break;
        default:                 top = {196, 184, 158, 255}; legs = {170, 96, 46, 255}; sleeve = top; trim = {110, 70, 40, 255}; glove = {92, 66, 44, 255}; break;
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
    if (h.cls != HeroClass::Diver) ShadeBall(P(-3, -153), 11.5f * s, hair);
    {   // the far arm
        float bR = ease(pose.backRaise);
        Vector2 sh = P(-6, -128 + br);
        Vector2 el = walking ? P(-6 - sw * 6, -105) : L(P(-5, -105), P(-8, -150), bR);
        Vector2 hd = walking ? P(-4 - sw * 13, -84) : L(P(5, -88), P(-2, -172), bR);
        arm(sh, el, hd, Tone(sleeve, -0.25f), Tone(forearm, -0.25f));
    }
    leg(-3, walking ? -sw * 15 - 2 : -12, liftB * 6, Tone(legs, -0.22f));
    if (h.cls == HeroClass::Captain) Q(P(-19, -100), P(15, -100), P(19, -34), P(-25, -34), top); // greatcoat skirts
    leg(4, walking ? sw * 15 + 3 : 13, lift * 6, legs);
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
        default: // mechanic's overalls, tool belt
            Q(P(-11, -122), P(15, -122), P(15, -86), P(-13, -86), legs);
            ShadeLimb(P(-8, -122), P(-10, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            ShadeLimb(P(11, -122), P(11, -133), 1.6f * s, 1.6f * s, Tone(legs, -0.2f));
            ShadeBall(P(-8, -121), 1.8f * s, brass);
            ShadeBall(P(11, -121), 1.8f * s, brass);
            Q(P(0, -114), P(9, -114), P(9, -106), P(0, -106), Tone(legs, -0.15f)); // pocket
            Q(P(-16, -94), P(16, -94), P(15, -88), P(-15, -88), Color{92, 64, 40, 255});
            Q(P(-14, -92), P(-5, -92), P(-5, -80), P(-14, -80), Color{110, 78, 50, 255}); // pouch
            break;
    }

    // --- head
    ShadeLimb(P(1, -134), P(2, -143), 5.4f * s, 5.2f * s, skinDk);
    Vector2 hd = P(3, -152);
    if (h.cls == HeroClass::Diver) {
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
    } else {
        ShadeBall(P(-2, -150), 3 * s, Tone(skin, -0.12f));  // ear
        ShadeBall(hd, 11.8f * s, skin);
        ShadeBall(P(7, -145.5f), 8 * s, skin);              // jaw
        DrawEllipse((int)P(4, -145).x, (int)P(4, -145).y, 4 * s, 2.5f * s, Fade(skinDk, 0.45f)); // under the cheekbone
        DrawCircleSector(P(-1.5f, -155), 12.4f * s, 180, 360, 18, hair);
        DrawCircleSector(P(-3, -152), 11.8f * s, f > 0 ? 90 : 0, f > 0 ? 180 : 90, 10, hair);
        for (int k = 0; k < 4; k++) DrawLineEx(P(-9 + k * 4.0f, -165), P(-7 + k * 4.5f, -157), 0.9f * s, Tone(hair, -0.35f)); // strands
        DrawEllipse((int)P(7.5f, -151).x, (int)P(7.5f, -151).y, 3.6f * s, 2.6f * s, Fade(Color{40, 20, 16, 255}, 0.45f)); // eye socket
        DrawEllipse((int)P(8, -151).x, (int)P(8, -151).y, 2.2f * s, 1.3f * s, Color{236, 230, 220, 255});
        DrawCircleV(P(8.8f, -151), 1.1f * s, Color{40, 60, 70, 255});
        DrawLineEx(P(5.5f, -152.6f), P(10.5f, -152.4f), 0.9f * s, Tone(skin, -0.5f));   // lid
        DrawLineEx(P(4, -155.2f), P(10.8f, -155.6f), 1.6f * s, Tone(hair, -0.3f));        // brow
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
            default: break;
        }
    }

    // --- the weapon arm, in front of everything, posed by `pose`
    float rz = ease(pose.raise), rc = ease(pose.reach);
    Vector2 sh = P(6, -128 + br);
    Vector2 elIdle = walking ? P(6 + sw * 6, -106) : P(12, -106), hdIdle = walking ? P(8 + sw * 13, -86) : P(22, -98);
    Vector2 el = L(L(elIdle, P(16, -150), rz), P(30, -122), rc);
    Vector2 hand = L(L(hdIdle, P(8, -174), rz), P(46, -118), rc);
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
    float base = h.cls == HeroClass::Nurse ? -8.0f : h.cls == HeroClass::Diver ? -26.0f : h.cls == HeroClass::Captain ? -64.0f : 70.0f;
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
        default: // a heavy wrench
            ShadeLimb(W(-4), W(30), 2.8f * s, 2.6f * s, steel);
            ShadeBall(W(33), 6.5f * s, steel);
            DrawCircleV(W(36), 2.8f * s, Tone(steel, -0.6f));
            break;
    }
}

void DrawCrewFigureInked(const Hero& h, Vector2 feet, float s, bool right, float walk, float t, const Pose& pose, Color tint) {
    BeginFigure();
    DrawCrewFigure(h, FIG_FEET, s, right, walk, t, pose);
    EndFigure(feet, tint);
}

