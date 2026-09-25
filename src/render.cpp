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
    RenderTexture2D scene{}, final{}, light{}, ocean{}, pixel{};
    Shader post{};
    int locTime = -1, locRes = -1, locVig = -1, locGrain = -1, locBloom = -1;
    float vignette = 0.45f, grain = 0.03f, bloom = 0.35f;
    bool lightsOpen = false;
};
ArtState A;
constexpr int LIGHT_DIV = 2; // the lightmap is half resolution: softer and cheaper

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
    A.pixel = LoadRenderTexture(PIXEL_W, PIXEL_H);
    SetTextureFilter(A.pixel.texture, TEXTURE_FILTER_POINT); // chunky pixels when scaled up

    A.post = LoadShaderFromMemory(nullptr, POST_FS);
    A.locTime = GetShaderLocation(A.post, "uTime");
    A.locRes = GetShaderLocation(A.post, "uRes");
    A.locVig = GetShaderLocation(A.post, "uVignette");
    A.locGrain = GetShaderLocation(A.post, "uGrain");
    A.locBloom = GetShaderLocation(A.post, "uBloom");
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
    UnloadShader(A.post);
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

// ============================================================= crew figures
// About 150 px tall at scale 1, standing on `feet`. `walk` is a phase in radians (0 = standing still).
void DrawCrewFigure(const Hero& h, Vector2 ft, float s, bool right, float walk, float t) {
    float f = right ? 1.0f : -1.0f;
    float x = ft.x;
    int seed = h.id * 7919 + 13;
    const Color skins[4] = {{238, 200, 168, 255}, {214, 168, 128, 255}, {176, 122, 86, 255}, {118, 80, 56, 255}};
    const Color hairs[5] = {{60, 40, 30, 255}, {150, 96, 50, 255}, {32, 28, 26, 255}, {205, 165, 100, 255}, {130, 126, 122, 255}};
    Color skin = skins[seed % 4], hair = hairs[(seed / 5) % 5];
    Color skinDk = ColorBrightness(skin, -0.25f);
    float swing = sinf(walk) * 12 * s;
    float y = ft.y - (walk != 0 ? fabsf(cosf(walk)) * 2.5f * s : 0);
    float breathe = sinf(t * 2.2f + h.id) * 1.0f * s;

    Color top, pants, boots{46, 36, 30, 255};
    switch (h.cls) {
        case HeroClass::Nurse:   top = {64, 104, 150, 255}; pants = {50, 50, 60, 255}; break;
        case HeroClass::Diver:   top = {132, 112, 78, 255}; pants = {120, 100, 70, 255}; boots = {150, 110, 50, 255}; break;
        case HeroClass::Captain: top = {34, 46, 86, 255};   pants = {30, 30, 40, 255}; break;
        default:                 top = {206, 112, 42, 255}; pants = {196, 104, 38, 255}; break;
    }
    Color topDk = ColorBrightness(top, -0.35f);

    auto leg = [&](float dx, float sw, Color col) {
        Vector2 hip{x + dx * s, y - 60 * s}, foot{x + dx * s + sw, y - 5 * s};
        Vector2 knee{(hip.x + foot.x) / 2 + f * 3 * s, (hip.y + foot.y) / 2};
        DrawLineEx(hip, knee, 11 * s, col);
        DrawLineEx(knee, foot, 10 * s, col);
        DrawCircleV(knee, 5 * s, col);
        float bw = h.cls == HeroClass::Diver ? 19 * s : 16 * s;
        DrawRectangleRounded({foot.x - bw / 2 + f * 3 * s, foot.y - 5 * s, bw, 10 * s}, 0.5f, 4, boots);
    };
    auto arm = [&](float dx, float sw, Color col, bool front) {
        Vector2 sh{x + dx * s, y - 103 * s + breathe}, hand{sh.x - sw * 0.8f + f * 4 * s, y - 64 * s + breathe};
        Vector2 el{(sh.x + hand.x) / 2 - f * 2 * s, (sh.y + hand.y) / 2};
        DrawLineEx(sh, el, 9 * s, col);
        DrawLineEx(el, hand, 8 * s, col);
        DrawCircleV(el, 4.5f * s, col);
        Color hc = h.cls == HeroClass::Diver ? Color{90, 70, 50, 255} : skin;
        DrawCircleV(hand, 4.8f * s, hc);
        if (front && h.cls == HeroClass::Mechanic) { // wrench
            DrawLineEx(hand, {hand.x + f * 4 * s, hand.y + 24 * s}, 4 * s, Color{150, 152, 160, 255});
            DrawCircleV({hand.x + f * 5 * s, hand.y + 26 * s}, 5 * s, Color{150, 152, 160, 255});
            DrawCircleV({hand.x + f * 5 * s, hand.y + 28 * s}, 2.5f * s, topDk);
        }
        if (front && h.cls == HeroClass::Diver) // harpoon
            DrawLineEx({hand.x - f * 14 * s, hand.y + 16 * s}, {hand.x + f * 30 * s, hand.y - 26 * s}, 3 * s, Color{140, 140, 150, 255});
    };

    // back limbs, legs, torso, front limbs, head
    arm(-f * 12, -swing, topDk, false);
    leg(-f * 4, -swing, ColorBrightness(pants, -0.3f));
    leg(f * 4, swing, pants);

    float ty = y - 112 * s + breathe;
    if (h.cls == HeroClass::Nurse) {
        DrawTri({x - 20 * s, y - 36 * s}, {x + 20 * s, y - 36 * s}, {x, y - 76 * s}, top); // skirt
        DrawRectangleRec({x - 20 * s, y - 60 * s, 40 * s, 24 * s}, top);
    }
    if (h.cls == HeroClass::Captain) DrawRectangleRounded({x - 19 * s, ty + 20 * s, 38 * s, 72 * s}, 0.25f, 4, top); // coat tails
    DrawRectangleRounded({x - 17 * s, ty, 34 * s, 56 * s}, 0.35f, 6, top);
    DrawRectangleRounded({x + (f > 0 ? 3 : -17) * s, ty + 2 * s, 14 * s, 54 * s}, 0.4f, 4, Fade(BLACK, 0.2f)); // shade side
    switch (h.cls) {
        case HeroClass::Nurse:
            DrawRectangleRounded({x - 11 * s, ty + 12 * s, 22 * s, 62 * s}, 0.3f, 4, Color{236, 232, 222, 255});
            DrawRectangleRec({x - 1.5f * s, ty + 22 * s, 3 * s, 10 * s}, Color{190, 40, 40, 255});
            DrawRectangleRec({x - 5 * s, ty + 25.5f * s, 10 * s, 3 * s}, Color{190, 40, 40, 255});
            break;
        case HeroClass::Captain:
            for (int k = 0; k < 3; k++) DrawCircleV({x + f * 6 * s, ty + (12 + k * 13) * s}, 2.2f * s, Pal::Brass);
            DrawRectangleRec({x - 17 * s, ty + 40 * s, 34 * s, 5 * s}, Color{70, 44, 26, 255}); // belt
            for (int k = -1; k <= 1; k += 2) {
                DrawRectangleRounded({x + k * 17 * s - 8 * s, ty - 2 * s, 16 * s, 6 * s}, 0.5f, 4, Pal::Brass);
                for (int j = 0; j < 4; j++) DrawLineEx({x + k * 17 * s - 6 * s + j * 4 * s, ty + 4 * s}, {x + k * 17 * s - 6 * s + j * 4 * s, ty + 9 * s}, 1.2f * s, Pal::Brass);
            }
            break;
        case HeroClass::Mechanic:
            DrawRectangleRec({x - 17 * s, ty, 34 * s, 16 * s}, Color{214, 204, 184, 255}); // shirt above the bib
            DrawRectangleRounded({x - 11 * s, ty + 10 * s, 22 * s, 22 * s}, 0.2f, 4, pants);
            DrawLineEx({x - 9 * s, ty + 12 * s}, {x - 13 * s, ty}, 3 * s, ColorBrightness(pants, -0.2f));
            DrawLineEx({x + 9 * s, ty + 12 * s}, {x + 13 * s, ty}, 3 * s, ColorBrightness(pants, -0.2f));
            DrawRectangleRec({x - 5 * s, ty + 16 * s, 10 * s, 7 * s}, ColorBrightness(pants, -0.2f)); // pocket
            break;
        case HeroClass::Diver:
            DrawRectangleRec({x - 17 * s, ty + 38 * s, 34 * s, 6 * s}, Color{70, 60, 50, 255}); // weight belt
            for (int k = 0; k < 3; k++) DrawRectangleRec({x - 13 * s + k * 10 * s, ty + 37 * s, 6 * s, 8 * s}, Color{100, 100, 108, 255});
            break;
        default: break;
    }
    arm(f * 12, swing, h.cls == HeroClass::Mechanic ? Color{214, 204, 184, 255} : top, true);

    Vector2 hd{x + f * 1.5f * s, y - 125 * s + breathe};
    if (h.cls == HeroClass::Diver) {
        // air hose curling behind, then the big brass helmet
        Vector2 prev{hd.x - f * 16 * s, hd.y + 4 * s};
        for (int k = 1; k <= 8; k++) {
            Vector2 p{hd.x - f * (16 + k * 5) * s, hd.y + 4 * s + k * 9 * s + sinf(t * 1.5f + k) * 2 * s};
            DrawLineEx(prev, p, 4 * s, Color{60, 56, 50, 255});
            prev = p;
        }
        DrawEllipse((int)x, (int)(ty + 2 * s), 22 * s, 8 * s, Pal::BrassDk); // breastplate
        DrawCircleV(hd, 20 * s, Pal::BrassDk);
        DrawCircleV({hd.x - 2 * s, hd.y - 2 * s}, 18.5f * s, Pal::Brass);
        DrawCircleV({hd.x - 7 * s, hd.y - 8 * s}, 6 * s, Fade(WHITE, 0.25f));
        Vector2 port{hd.x + f * 8 * s, hd.y + 1 * s};
        DrawCircleV(port, 10 * s, Pal::BrassDk);
        DrawCircleV(port, 8 * s, Color{24, 52, 62, 255});
        DrawCircleV({port.x - 2.5f * s, port.y - 3 * s}, 2.5f * s, Color{170, 225, 235, 200});
        for (int k = 0; k < 6; k++) {
            float a = k * PI / 3;
            DrawCircleV({hd.x + cosf(a) * 16 * s, hd.y + sinf(a) * 16 * s}, 1.6f * s, Pal::BrassDk);
        }
        return;
    }
    DrawRectangleRec({x - 4 * s, hd.y + 8 * s, 8 * s, 8 * s}, skinDk); // neck
    DrawCircleV({hd.x - f * 5 * s, hd.y}, 11 * s, hair);              // hair behind the head
    DrawCircleV(hd, 12.5f * s, skin);
    DrawCircleSector({hd.x - f * 2 * s, hd.y - 4 * s}, 12.5f * s, 180, 360, 16, hair); // hair on top
    DrawCircleSector({hd.x - f * 2 * s, hd.y - 2 * s}, 12.5f * s, f > 0 ? 90 : 0, f > 0 ? 180 : 90, 8, hair); // and at the back
    DrawCircleV({hd.x + f * 12 * s, hd.y + 1 * s}, 3 * s, skinDk);     // nose
    DrawCircleV({hd.x + f * 6 * s, hd.y + 0.5f * s}, 2.0f * s, Color{250, 246, 240, 255});
    DrawCircleV({hd.x + f * 6.6f * s, hd.y + 0.5f * s}, 1.4f * s, Color{30, 24, 20, 255});
    DrawLineEx({hd.x + f * 3.5f * s, hd.y - 3 * s}, {hd.x + f * 8.5f * s, hd.y - 3.5f * s}, 1.3f * s, ColorBrightness(hair, -0.2f)); // brow
    DrawLineEx({hd.x + f * 5 * s, hd.y + 6 * s}, {hd.x + f * 9 * s, hd.y + 6 * s}, 1.2f * s, skinDk);
    switch (h.cls) {
        case HeroClass::Nurse:
            DrawCircleV({hd.x - f * 12 * s, hd.y - 4 * s}, 6 * s, hair); // bun
            DrawRectangleRounded({hd.x - 11 * s, hd.y - 19 * s, 22 * s, 9 * s}, 0.4f, 4, Color{240, 238, 230, 255});
            DrawRectangleRec({hd.x - 1 * s, hd.y - 18 * s, 2 * s, 7 * s}, Color{190, 40, 40, 255});
            DrawRectangleRec({hd.x - 3.5f * s, hd.y - 15.5f * s, 7 * s, 2 * s}, Color{190, 40, 40, 255});
            break;
        case HeroClass::Captain:
            if (seed % 2) DrawCircleSector({hd.x + f * 3 * s, hd.y + 4 * s}, 10 * s, 0, 180, 12, Color{226, 222, 214, 255}); // beard
            DrawRectangleRounded({hd.x - 14 * s, hd.y - 22 * s, 28 * s, 12 * s}, 0.4f, 4, Color{24, 24, 30, 255});
            DrawRectangleRec({hd.x - 13 * s, hd.y - 13 * s, 26 * s, 3 * s}, Pal::Brass);
            DrawRectangleRounded({hd.x + (f > 0 ? 0 : -20) * s, hd.y - 11 * s, 20 * s, 4 * s}, 0.5f, 4, Color{16, 16, 20, 255});
            break;
        case HeroClass::Mechanic:
            DrawRectangleRec({hd.x - 13 * s, hd.y - 9 * s, 26 * s, 4 * s}, Color{60, 44, 32, 255});
            for (int k = -1; k <= 1; k += 2) {
                DrawCircleV({hd.x + k * 6 * s + f * 2 * s, hd.y - 8 * s}, 5 * s, Pal::Brass);
                DrawCircleV({hd.x + k * 6 * s + f * 2 * s, hd.y - 8 * s}, 3.2f * s, Color{120, 200, 210, 255});
            }
            DrawCircleV({hd.x + f * 3 * s, hd.y + 5 * s}, 3 * s, Fade(BLACK, 0.25f)); // soot
            break;
        default: break;
    }
}
