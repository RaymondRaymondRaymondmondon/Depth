// NOCLIP's renderer (see noclip_render.h).
#include "noclip_render.h"
#include "game.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstring>
#include <string>

using namespace nc;

namespace ncr {
namespace {

// ---------------------------------------------------------------- shaders
const char* LEVEL_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel; uniform float uTime; uniform float uBreath;
out vec2 fragTexCoord; out vec3 fragPos; out vec3 fragNormal; out vec4 fragColor;
void main() {
    vec3 p = vertexPosition; vec3 n = vertexNormal;
    float wall = abs(n.y) < 0.5 ? 1.0 : 0.0;
    p += n * wall * uBreath * sin(uTime * 1.3 + p.x * 0.7 + p.z * 0.5 + p.y) * 0.07;   // the walls breathe at the edge of sanity
    vec4 wp = matModel * vec4(p, 1.0);
    fragPos = wp.xyz; fragNormal = normalize(mat3(matModel) * n); fragTexCoord = vertexTexCoord; fragColor = vertexColor;
    gl_Position = mvp * vec4(p, 1.0);
})";
const char* LEVEL_FS = R"(#version 330
in vec2 fragTexCoord; in vec3 fragPos; in vec3 fragNormal; in vec4 fragColor;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform sampler2D uLight; uniform vec2 uLevelSize; uniform vec3 uLightColor; uniform float uAmbient; uniform float uCeil;
uniform vec3 uLampPos; uniform vec3 uLampDir; uniform float uLampOn; uniform float uLampRange; uniform float uLampCone;
uniform vec3 uFog; uniform float uFogDensity; uniform vec3 uCam; uniform float uEmissive;
uniform vec4 uPoint[8]; uniform vec4 uPointCol[8]; uniform int uPoints;
out vec4 finalColor;
void main() {
    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (tex.a < 0.02) discard;
    // the ceiling's fluorescent grid, blurred into pools; brighter up near the panels (overlit)
    float L = texture(uLight, fragPos.xz / uLevelSize).r;
    float up = clamp(fragPos.y / max(uCeil, 0.5), 0.0, 1.0);
    vec3 light = uLightColor * L * (1.15 + 0.25 * up) + vec3(uAmbient);
    // the headlamp: a soft spot from the eye
    vec3 tl = uLampPos - fragPos; float d = length(tl); vec3 l = tl / max(d, 0.001);
    float spot = smoothstep(uLampCone, uLampCone + 0.12, dot(-l, uLampDir));
    float att = clamp(1.0 - d / uLampRange, 0.0, 1.0); att *= att;
    float nd = max(dot(fragNormal, l), 0.0) * 0.65 + 0.35;
    light += vec3(1.0, 0.95, 0.82) * (spot * 1.5 + 0.18 * att) * att * nd * uLampOn;
    for (int i = 0; i < uPoints; i++) { vec3 pv = uPoint[i].xyz - fragPos; float pd = length(pv); float a = clamp(1.0 - pd / uPoint[i].w, 0.0, 1.0); light += uPointCol[i].rgb * uPointCol[i].a * a * a * (max(dot(fragNormal, pv / max(pd, 0.001)), 0.0) * 0.6 + 0.4); }
    vec3 c = tex.rgb * light + tex.rgb * uEmissive;
    float fog = 1.0 - exp(-uFogDensity * length(fragPos - uCam));
    c = mix(c, uFog, clamp(fog, 0.0, 1.0));
    finalColor = vec4(c, tex.a);
})";
const char* VHS_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform float uTime; uniform float uNoise; uniform vec2 uRes; uniform float uBlack;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec2 uv = fragTexCoord;
    float wob = sin(uv.y * 220.0 + uTime * 3.0) * 0.0007 * (1.0 + uNoise * 5.0);
    float band = fract(uv.y * 0.6 - uTime * 0.07); float tear = smoothstep(0.0, 0.015, band) * smoothstep(0.03, 0.015, band);
    uv.x += wob + tear * 0.004 * (1.0 + uNoise * 3.0);
    float ca = 0.0022 * (1.0 + uNoise * 2.0);
    vec3 c = vec3(texture(texture0, uv + vec2(ca, 0.0)).r, texture(texture0, uv).g, texture(texture0, uv - vec2(ca, 0.0)).b);
    c = mix(c, (texture(texture0, uv + vec2(0.003, 0.0)).rgb + texture(texture0, uv - vec2(0.003, 0.0)).rgb) * 0.5, 0.25);   // the smear
    float scan = 0.9 + 0.1 * sin(uv.y * uRes.y * 3.14159);
    float n = hash(uv * uRes + floor(uTime * 30.0));
    c = c * scan + (n - 0.5) * (0.05 + uNoise * 0.12) + tear * 0.06;
    vec2 v = uv - 0.5; c *= 1.0 - dot(v, v) * 0.75;
    float g = dot(c, vec3(0.3, 0.59, 0.11)); c = mix(c, vec3(g), 0.12); c = pow(max(c, 0.0), vec3(0.95));
    c *= 1.0 - uBlack;
    finalColor = vec4(c, 1.0);
})";

struct Shader3D { Shader sh{}; int time, breath, light, levelSize, lightColor, ambient, ceil, lampPos, lampDir, lampOn, lampRange, lampCone, fog, fogDensity, cam, emissive, point, pointCol, points; bool ok = false; };
Shader3D S3; Shader gVhs{}; int gVhsTime, gVhsNoise, gVhsRes, gVhsBlack; bool gVhsOk = false;
void EnsureShaders() {
    if (S3.ok) return;
    S3.sh = LoadShaderFromMemory(LEVEL_VS, LEVEL_FS); S3.ok = true;
    auto loc = [&](const char* n) { return GetShaderLocation(S3.sh, n); };
    S3.time = loc("uTime"); S3.breath = loc("uBreath"); S3.light = loc("uLight"); S3.levelSize = loc("uLevelSize"); S3.lightColor = loc("uLightColor"); S3.ambient = loc("uAmbient"); S3.ceil = loc("uCeil");
    S3.lampPos = loc("uLampPos"); S3.lampDir = loc("uLampDir"); S3.lampOn = loc("uLampOn"); S3.lampRange = loc("uLampRange"); S3.lampCone = loc("uLampCone"); S3.fog = loc("uFog"); S3.fogDensity = loc("uFogDensity"); S3.cam = loc("uCam"); S3.emissive = loc("uEmissive");
    S3.point = loc("uPoint"); S3.pointCol = loc("uPointCol"); S3.points = loc("uPoints");
    S3.sh.locs[SHADER_LOC_MATRIX_MODEL] = loc("matModel");
    S3.sh.locs[SHADER_LOC_MAP_EMISSION] = loc("uLight");   // (the light grid rides along as a material map: DrawMesh binds those)
    gVhs = LoadShaderFromMemory(nullptr, VHS_FS); gVhsTime = GetShaderLocation(gVhs, "uTime"); gVhsNoise = GetShaderLocation(gVhs, "uNoise"); gVhsRes = GetShaderLocation(gVhs, "uRes"); gVhsBlack = GetShaderLocation(gVhs, "uBlack"); gVhsOk = true;
}

// ---------------------------------------------------------------- the procedural textures (flat and overlit)
uint32_t Hh(uint32_t a) { a ^= a >> 16; a *= 0x7feb352d; a ^= a >> 15; a *= 0x846ca68b; a ^= a >> 16; return a; }
float Nz(int x, int y, int s) { return (Hh((uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(s * 83492791)) & 0xFFFF) / 65535.0f; }
Color Mx(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k)}; }
std::map<std::string, Texture2D> gTex;
Texture2D Tex(const std::string& name, Color base) {
    std::string key = name + std::to_string(base.r) + "_" + std::to_string(base.g) + "_" + std::to_string(base.b);
    auto it = gTex.find(key); if (it != gTex.end()) return it->second;
    const int N = 128; Image img = GenImageColor(N, N, base); Color* px = (Color*)img.data;
    auto put = [&](int x, int y, Color c) { px[((y % N + N) % N) * N + ((x % N + N) % N)] = c; };
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float n = Nz(x, y, 1), n2 = Nz(x / 4, y / 4, 2); Color c = base;
        if (name == "wallpaper") { float stripe = ((x / 8) % 2) ? 0.06f : 0.0f; float chev = (abs(((x + y / 2) % 16) - 8) < 1) ? 0.08f : 0; c = Mx(base, BLACK, 0.04f + stripe + chev + n * 0.05f); if (y % 64 < 2) c = Mx(c, BLACK, 0.1f); c = Mx(c, Color{120, 110, 60, 255}, n2 * 0.12f); }
        else if (name == "carpet") { c = Mx(base, n > 0.5f ? WHITE : BLACK, fabsf(n - 0.5f) * 0.3f); c = Mx(c, Color{90, 80, 50, 255}, n2 * 0.18f); }
        else if (name == "tiles") { bool grid = x % 32 < 2 || y % 32 < 2; c = grid ? Mx(base, BLACK, 0.25f) : Mx(base, WHITE, n * 0.06f); if (!grid && Nz(x / 3, y / 3, 9) > 0.92f) c = Mx(c, BLACK, 0.2f); }
        else if (name == "concrete") { c = Mx(base, n > 0.5f ? WHITE : BLACK, fabsf(n - 0.5f) * 0.18f + n2 * 0.08f); if (Nz(x / 16, y / 16, 3) > 0.85f) c = Mx(c, Color{60, 60, 60, 255}, 0.15f); }
        else if (name == "pipes") { c = Mx(base, BLACK, n * 0.15f); int band = y % 32; if (band > 6 && band < 14) c = Mx(Color{120, 100, 80, 255}, BLACK, 0.3f - 0.3f * sinf((band - 6) / 8.0f * PI)); if (band > 20 && band < 24) c = Mx(Color{150, 70, 50, 255}, BLACK, 0.2f); }
        else if (name == "brick") { int row = y / 12, off = (row % 2) * 12; bool mortar = y % 12 < 2 || (x + off) % 24 < 2; c = mortar ? Color{150, 140, 130, 255} : Mx(base, BLACK, n * 0.25f); }
        else if (name == "grate") { bool bar = x % 8 < 2 || y % 8 < 2; c = bar ? Mx(base, WHITE, 0.15f) : Mx(base, BLACK, 0.5f); }
        else if (name == "plaster") { c = Mx(base, BLACK, n * 0.07f + n2 * 0.05f); }
        else if (name == "damask") { float d = sinf(x * 0.2f) * sinf(y * 0.2f) + sinf((x + y) * 0.1f); c = Mx(base, d > 0.6f ? Color{200, 160, 80, 255} : BLACK, d > 0.6f ? 0.35f : n * 0.15f); }
        else if (name == "planks") { int pl = x / 16; c = Mx(base, BLACK, (Nz(pl, 0, 4) * 0.25f) + n * 0.08f + (x % 16 < 1 ? 0.3f : 0)); if (Nz(pl, y / 30, 5) > 0.95f) c = Mx(c, BLACK, 0.4f); }
        else if (name == "rock") { c = Mx(base, n2 > 0.5f ? WHITE : BLACK, fabsf(n2 - 0.5f) * 0.3f + n * 0.1f); }
        else if (name == "siding") { c = Mx(base, BLACK, (y % 10 < 2 ? 0.25f : 0) + n * 0.05f); }
        else if (name == "grass") { c = Mx(base, n > 0.5f ? Color{90, 120, 60, 255} : Color{30, 40, 24, 255}, fabsf(n - 0.5f) * 0.6f); }
        else if (name == "dirt") { c = Mx(base, BLACK, n * 0.3f); }
        else if (name == "windows") { bool win = (x % 32 > 6 && x % 32 < 26) && (y % 32 > 6 && y % 32 < 24); c = win ? Mx(Color{40, 50, 60, 255}, WHITE, n * 0.1f) : Mx(base, BLACK, n * 0.1f); }
        else if (name == "asphalt") { c = Mx(base, n > 0.5f ? WHITE : BLACK, fabsf(n - 0.5f) * 0.25f); if (x % 64 < 3 && y % 32 < 16) c = Color{220, 210, 120, 255}; }
        else if (name == "flesh") { float v = sinf(x * 0.15f + n2 * 6) * sinf(y * 0.11f); c = Mx(base, v > 0.7f ? Color{150, 40, 50, 255} : Color{230, 180, 170, 255}, v > 0.7f ? 0.5f : n * 0.15f); }
        else if (name == "glass") { c = Mx(base, WHITE, (x + y) % 64 < 6 ? 0.4f : n * 0.05f); c.a = 110; }
        else if (name == "lino") { bool chk = ((x / 32) + (y / 32)) % 2; c = Mx(base, chk ? WHITE : BLACK, 0.08f + n * 0.05f); }
        else if (name == "panels") { bool seam = x % 64 < 2 || y % 64 < 2; c = seam ? Mx(base, BLACK, 0.15f) : Mx(base, WHITE, n * 0.04f); if (x % 64 > 50 && y % 64 > 50 && x % 64 < 58 && y % 64 < 58) c = Color{120, 200, 255, 255}; }
        else if (name == "steel") { c = Mx(base, BLACK, n * 0.12f); if ((x % 32 == 4 || x % 32 == 28) && (y % 32 == 4 || y % 32 == 28)) c = Mx(base, WHITE, 0.4f); if (y % 64 < 2) c = Mx(c, BLACK, 0.3f); }
        else if (name == "kids") { c = Mx(base, BLACK, n * 0.05f); if (Nz(x / 16, y / 16, 7) > 0.8f && (x % 16 - 8) * (x % 16 - 8) + (y % 16 - 8) * (y % 16 - 8) < 20) c = Color{(unsigned char)(100 + Nz(x / 16, y / 16, 8) * 150), 120, 200, 255}; }
        else if (name == "lab") { c = Mx(base, BLACK, n * 0.1f); if (y % 64 < 3) c = Color{170, 140, 80, 255}; if (x % 32 < 1) c = Mx(c, BLACK, 0.3f); }
        else if (name == "fabric") { c = Mx(base, BLACK, ((x + y) % 4 == 0 ? 0.1f : 0) + n * 0.06f); }
        else if (name == "wheat") { float stalk = ((x * 7 + (int)(Nz(x, 0, 6) * 40)) % 9 < 2) ? 0.0f : 0.3f; c = Mx(Color{220, 190, 110, 255}, Color{120, 100, 50, 255}, stalk + n * 0.2f); if (y < 20 && Nz(x / 2, y / 3, 11) > 0.5f) c = Color{240, 210, 120, 255}; }
        else if (name == "sky") { c = Mx(base, WHITE, n > 0.995f ? 0.6f : 0); }
        else { c = Mx(base, BLACK, n * 0.1f); }
        put(x, y, c);
    }
    Texture2D t = LoadTextureFromImage(img); UnloadImage(img); GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    gTex[key] = t; return t;
}
Texture2D White() { static Texture2D w{}; if (!w.id) { Image i = GenImageColor(4, 4, WHITE); w = LoadTextureFromImage(i); UnloadImage(i); } return w; }

// ---------------------------------------------------------------- the level's meshes (built once a day per level)
struct MeshB { std::vector<float> v, t, n; std::vector<unsigned char> c;
    void Quad(Vector3 a, Vector3 b, Vector3 c2, Vector3 d, Vector3 nn, Vector2 ta, Vector2 tb, Vector2 tc, Vector2 td, Color col = WHITE) {
        Vector3 P[6] = {a, b, c2, a, c2, d}; Vector2 T[6] = {ta, tb, tc, ta, tc, td};
        for (int i = 0; i < 6; i++) { v.push_back(P[i].x); v.push_back(P[i].y); v.push_back(P[i].z); t.push_back(T[i].x); t.push_back(T[i].y); n.push_back(nn.x); n.push_back(nn.y); n.push_back(nn.z); c.push_back(col.r); c.push_back(col.g); c.push_back(col.b); c.push_back(col.a); }
    }
    Model Build() {
        Mesh m{}; m.vertexCount = (int)v.size() / 3; m.triangleCount = m.vertexCount / 3; if (!m.vertexCount) return Model{};
        m.vertices = (float*)MemAlloc(v.size() * sizeof(float)); memcpy(m.vertices, v.data(), v.size() * sizeof(float));
        m.texcoords = (float*)MemAlloc(t.size() * sizeof(float)); memcpy(m.texcoords, t.data(), t.size() * sizeof(float));
        m.normals = (float*)MemAlloc(n.size() * sizeof(float)); memcpy(m.normals, n.data(), n.size() * sizeof(float));
        m.colors = (unsigned char*)MemAlloc(c.size()); memcpy(m.colors, c.data(), c.size());
        UploadMesh(&m, false); return LoadModelFromMesh(m);
    }
};
struct LevelGfx { uint32_t seed = 0; int id = -1; Model walls{}, floors{}, ceils{}, low{}, glass{}, water{}, lab{}, wheat{}; Texture2D lightTex{}; std::vector<uint8_t> lightPx; bool sky = false; };
std::map<int, LevelGfx> gG;
bool Outdoor(int id) { return id == 9 || id == 10 || id == 11; }
float WallH(const Level& L, int x, int z) { int id = L.id; if (id == 11) return 14 + (int)(Nz(x / 6, z / 6, 77) * 26); if (id == 9) return 3.2f; if (id == 10) return 6.0f; if (id == 7 && L.At(x, z) == T_VOID) return 0; return D().levels[id].ceil; }
LevelGfx& Gfx(const Level& L) {
    LevelGfx& G = gG[L.id];
    if (G.seed == L.seed && G.id == L.id && G.walls.meshCount) return G;
    for (Model* m : {&G.walls, &G.floors, &G.ceils, &G.low, &G.glass, &G.water, &G.lab, &G.wheat}) if (m->meshCount) { UnloadModel(*m); *m = Model{}; }
    if (G.lightTex.id) UnloadTexture(G.lightTex);
    G.seed = L.seed; G.id = L.id; G.sky = Outdoor(L.id);
    const LevelDef& def = D().levels[L.id]; float C = CELL, ceil = def.ceil;
    MeshB walls, floors, ceils, low, glass, water, lab, wheat;
    auto solidish = [&](int x, int z) { uint8_t t = L.At(x, z); return t == T_WALL || t == T_VOID || t == T_LABWALL || t == T_GLASS; };
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) {
        uint8_t t = L.At(x, z); float x0 = x * C, z0 = z * C, x1 = x0 + C, z1 = z0 + C;
        bool roomRoof = !G.sky || (L.Flags(x, z) & CF_ROOM);
        if (L.Walkable(x, z) || t == T_LOW) {
            float fy = t == T_WATER ? -0.6f : t == T_DEEP ? -4.0f : t == T_PIT ? -40.0f : 0.0f;
            Color fc = (L.Flags(x, z) & CF_STAIN) ? Color{200, 190, 160, 255} : (L.Flags(x, z) & CF_MANILA) ? Color{250, 236, 200, 255} : WHITE;
            MeshB& fm = t == T_LABFLOOR ? lab : floors;
            if (t != T_PIT) fm.Quad({x0, fy, z0}, {x0, fy, z1}, {x1, fy, z1}, {x1, fy, z0}, {0, 1, 0}, {x0 / C, z0 / C}, {x0 / C, z1 / C}, {x1 / C, z1 / C}, {x1 / C, z0 / C}, fc);
            if (t == T_WATER || t == T_DEEP) water.Quad({x0, -0.05f, z0}, {x0, -0.05f, z1}, {x1, -0.05f, z1}, {x1, -0.05f, z0}, {0, 1, 0}, {x0 / 4, z0 / 4}, {x0 / 4, z1 / 4}, {x1 / 4, z1 / 4}, {x1 / 4, z0 / 4});
            if (roomRoof && t != T_PIT && !(L.id == 7 && t == T_DEEP)) { float cy = ceil; (t == T_LABFLOOR ? lab : ceils).Quad({x0, cy, z0}, {x1, cy, z0}, {x1, cy, z1}, {x0, cy, z1}, {0, -1, 0}, {x0 / C, z0 / C}, {x1 / C, z0 / C}, {x1 / C, z1 / C}, {x0 / C, z1 / C}); }
            if (t == T_WHEAT) { float h = 1.5f; for (int s = 0; s < 2; s++) { float o = (s + 0.5f) * C / 2; wheat.Quad({x0, 0, z0 + o}, {x1, 0, z0 + o}, {x1, h, z0 + o}, {x0, h, z0 + o}, {0, 0, 1}, {0, 1}, {1, 1}, {1, 0}, {0, 0}); wheat.Quad({x0 + o, 0, z0}, {x0 + o, 0, z1}, {x0 + o, h, z1}, {x0 + o, h, z0}, {1, 0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}); } }
            if (t == T_LOW) {   // a low partition / rack / pole: a box 1.3 m tall
                float h = L.id == 10 ? 3.0f : 1.3f; Color lc = L.id == 1 ? Color{170, 150, 110, 255} : WHITE;
                low.Quad({x0, h, z0}, {x0, h, z1}, {x1, h, z1}, {x1, h, z0}, {0, 1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, lc);
                low.Quad({x0, 0, z0}, {x1, 0, z0}, {x1, h, z0}, {x0, h, z0}, {0, 0, -1}, {0, h / C}, {1, h / C}, {1, 0}, {0, 0}, lc);
                low.Quad({x1, 0, z1}, {x0, 0, z1}, {x0, h, z1}, {x1, h, z1}, {0, 0, 1}, {0, h / C}, {1, h / C}, {1, 0}, {0, 0}, lc);
                low.Quad({x0, 0, z1}, {x0, 0, z0}, {x0, h, z0}, {x0, h, z1}, {-1, 0, 0}, {0, h / C}, {1, h / C}, {1, 0}, {0, 0}, lc);
                low.Quad({x1, 0, z0}, {x1, 0, z1}, {x1, h, z1}, {x1, h, z0}, {1, 0, 0}, {0, h / C}, {1, h / C}, {1, 0}, {0, 0}, lc);
            }
            continue;
        }
        if (!solidish(x, z) || t == T_VOID) continue;
        // a wall cell: its faces toward open cells
        float h = WallH(L, x, z); if (h <= 0) continue;
        MeshB& wm = t == T_GLASS ? glass : t == T_LABWALL ? lab : walls;
        Color wc = (L.Flags(x, z) & CF_MANILA) ? Color{250, 236, 190, 255} : (L.Flags(x, z) & CF_MIRROR) ? Color{220, 230, 240, 255} : WHITE;
        auto open = [&](int nx, int nz) { return !solidish(nx, nz); };
        float v1 = h / C;
        if (open(x, z - 1)) wm.Quad({x1, 0, z0}, {x0, 0, z0}, {x0, h, z0}, {x1, h, z0}, {0, 0, -1}, {x1 / C, v1}, {x0 / C, v1}, {x0 / C, 0}, {x1 / C, 0}, wc);
        if (open(x, z + 1)) wm.Quad({x0, 0, z1}, {x1, 0, z1}, {x1, h, z1}, {x0, h, z1}, {0, 0, 1}, {x0 / C, v1}, {x1 / C, v1}, {x1 / C, 0}, {x0 / C, 0}, wc);
        if (open(x - 1, z)) wm.Quad({x0, 0, z0}, {x0, 0, z1}, {x0, h, z1}, {x0, h, z0}, {-1, 0, 0}, {z0 / C, v1}, {z1 / C, v1}, {z1 / C, 0}, {z0 / C, 0}, wc);
        if (open(x + 1, z)) wm.Quad({x1, 0, z1}, {x1, 0, z0}, {x1, h, z0}, {x1, h, z1}, {1, 0, 0}, {z1 / C, v1}, {z0 / C, v1}, {z0 / C, 0}, {z1 / C, 0}, wc);
        if (G.sky && !(L.Flags(x, z) & CF_ROOM)) wm.Quad({x0, h, z0}, {x0, h, z1}, {x1, h, z1}, {x1, h, z0}, {0, 1, 0}, {0, 0}, {0, 1}, {1, 1}, {1, 0}, wc);   // (roofs under the open sky)
    }
    G.walls = walls.Build(); G.floors = floors.Build(); G.ceils = ceils.Build(); G.low = low.Build(); G.glass = glass.Build(); G.water = water.Build(); G.lab = lab.Build(); G.wheat = wheat.Build();
    auto setTex = [&](Model& m, Texture2D tex) { if (m.meshCount) { m.materials[0].shader = S3.sh; m.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex; } };
    setTex(G.walls, Tex(def.tex[0], def.wall)); setTex(G.floors, Tex(def.tex[1], def.floor)); setTex(G.ceils, Tex(def.tex[2], def.top));
    setTex(G.low, Tex(L.id == 4 ? "fabric" : L.id == 10 ? "planks" : "concrete", L.id == 4 ? Color{110, 120, 140, 255} : L.id == 10 ? Color{120, 90, 60, 255} : Color{140, 120, 90, 255}));
    setTex(G.glass, Tex("glass", Color{200, 230, 240, 255})); setTex(G.water, Tex("concrete", Color{40, 70, 90, 255})); setTex(G.lab, Tex("lab", Color{110, 114, 118, 255})); setTex(G.wheat, Tex("wheat", Color{210, 180, 100, 255}));
    G.lightPx.assign(L.w * L.h, 0);
    Image img = GenImageColor(L.w, L.h, BLACK); ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE); G.lightTex = LoadTextureFromImage(img); UnloadImage(img);
    SetTextureFilter(G.lightTex, TEXTURE_FILTER_BILINEAR); SetTextureWrap(G.lightTex, TEXTURE_WRAP_CLAMP);
    return G;
}
// the light grid this frame: steady panels, flickering ones, Level 1's lights-out, Overtime, Labs with power
void UpdateLights(const World& w, const Level& L, LevelGfx& G, float t, float sanity, const Player* me) {
    bool out = w.overtime || (L.id == 1 && w.lightsOutT > 0);
    for (int i = 0; i < L.w * L.h; i++) {
        uint8_t l = L.light[i]; float b = 0;
        if (l == 255) b = 1; else if (l >= 128) { float ph = (l - 128) * 0.37f; b = (sinf(t * (3 + (l % 7)) + ph) > -0.6f && Nz(i, (int)(t * 8), 3) > 0.08f) ? 0.9f : 0.15f; } else if (l > 0) b = l / 255.0f;
        if (out) b *= 0.04f;
        G.lightPx[i] = (uint8_t)(std::clamp(b, 0.0f, 1.0f) * 255);
    }
    // a Lab with its generator running is lit; one without is dark
    for (int k = 0; k < (int)L.labs.size(); k++) { const LabPlan& lp = L.labs[k]; bool on = false; for (const auto& s : w.labs) if (s.level == L.id && s.idx == k && s.online && s.fuel > 0) on = true; float pulse = 1; for (const auto& s : w.labs) if (s.level == L.id && s.idx == k && s.charging) pulse = 0.6f + 0.4f * sinf(t * 8); for (int z = lp.z0; z <= lp.z1; z++) for (int x = lp.x0; x <= lp.x1; x++) if ((x - lp.x0) % 2 == 1 && (z - lp.z0) % 2 == 1) G.lightPx[z * L.w + x] = on ? (uint8_t)(230 * pulse) : 0; }
    // spread each panel's light to its neighbours (walls included): pools, not dots
    { std::vector<uint8_t> src = G.lightPx; for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) { int i = z * L.w + x; int m = src[i]; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= L.w || zz >= L.h || (!dx && !dz)) continue; m = std::max(m, (int)(src[zz * L.w + xx] * (dx && dz ? 0.7f : 0.85f))); } G.lightPx[i] = (uint8_t)m; } }
    // a hallucinated flicker only this player sees (sanity 69-50)
    if (me && sanity < 70 && sanity >= 10) { int cx = L.CellX(me->p.x), cz = L.CellZ(me->p.z); int x = cx + (int)(sinf(t * 0.3f) * 3), z = cz + (int)(cosf(t * 0.23f) * 3); if (x >= 0 && z >= 0 && x < L.w && z < L.h && Nz((int)(t * 6), 1, 4) > 0.5f) G.lightPx[z * L.w + x] = 0; }
    UpdateTexture(G.lightTex, G.lightPx.data());
}

// ---------------------------------------------------------------- drawing helpers through the shader (a tinted white texture)
Model gCube{}, gSphere{}, gCyl{}; bool gPrims = false;
float gEmissive = 0;
void Prims() { if (gPrims) return; gPrims = true; gCube = LoadModelFromMesh(GenMeshCube(1, 1, 1)); gSphere = LoadModelFromMesh(GenMeshSphere(0.5f, 10, 12)); gCyl = LoadModelFromMesh(GenMeshCylinder(0.5f, 1, 10)); for (Model* m : {&gCube, &gSphere, &gCyl}) { m->materials[0].shader = S3.sh; m->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = White(); } }
void Emis(float e) { gEmissive = e; SetShaderValue(S3.sh, S3.emissive, &gEmissive, SHADER_UNIFORM_FLOAT); }
void DrawM(Model& m, Matrix world, Color c, float emis = 0) { if (emis != gEmissive) Emis(emis); m.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = c; m.transform = world; DrawModel(m, {0, 0, 0}, 1, WHITE); }
void Box(Vector3 c, Vector3 s, Color col, float yaw = 0, float emis = 0) { DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateY(-yaw)), MatrixTranslate(c.x, c.y, c.z)), col, emis); }
void BoxM(Matrix frame, Vector3 at, Vector3 s, Color col, float emis = 0) { DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixTranslate(at.x, at.y, at.z)), frame), col, emis); }
void BallM(Matrix frame, Vector3 at, Vector3 s, Color col, float emis = 0) { DrawM(gSphere, MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixTranslate(at.x, at.y, at.z)), frame), col, emis); }
Matrix Frame(Vector3 at, float yaw) { return MatrixMultiply(MatrixRotateY(-yaw + PI / 2), MatrixTranslate(at.x, at.y, at.z)); }   // (local +z faces along yaw)

// a humanoid of boxes: the Backrooms' cast and the crew. phase: the walk; crawl: on all fours (the Hound)
struct Look { float height = 1.8f, width = 0.45f; Color body{}, head{}, legs{}; bool face = true, crawl = false, gown = false, hat = false, vest = false, lamp = false, party = false; Color hatC{250, 210, 40, 255}, vestC{240, 200, 30, 255}; };
void Figure(Vector3 at, float yaw, const Look& L, float phase) {
    Matrix f = Frame(at, yaw); float H = L.height, W = L.width; float sw = sinf(phase) * 0.5f;
    if (L.crawl) {
        BoxM(f, {0, 0.75f, 0}, {W, 0.35f, 1.3f}, L.body);
        BallM(f, {0, 0.85f, 0.8f}, {0.38f, 0.32f, 0.42f}, L.head);
        for (int s = -1; s <= 1; s += 2) for (int e = -1; e <= 1; e += 2) DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -0.35f, 0), MatrixScale(0.12f, 0.75f, 0.12f)), MatrixRotateX(sw * s * e)), MatrixMultiply(MatrixTranslate(s * W * 0.45f, 0.72f, e * 0.5f), f)), L.legs);
        BoxM(f, {0, 0.72f, 1.05f}, {0.36f, 0.08f, 0.2f}, Color{40, 10, 10, 255});   // (the too-wide mouth)
        return;
    }
    float legH = H * 0.47f, torso = H * 0.33f;
    for (int s = -1; s <= 1; s += 2) DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -legH / 2, 0), MatrixScale(W * 0.38f, legH, W * 0.38f)), MatrixRotateX(sw * s)), MatrixMultiply(MatrixTranslate(s * W * 0.27f, legH, 0), f)), L.legs);
    BoxM(f, {0, legH + torso / 2, 0}, {W, torso, W * 0.55f}, L.gown ? Mx(L.body, WHITE, 0.2f) : L.body);
    if (L.gown) BoxM(f, {0, legH * 0.75f, 0}, {W * 1.05f, legH * 0.5f, W * 0.6f}, L.body);
    if (L.vest) BoxM(f, {0, legH + torso * 0.55f, 0}, {W * 1.04f, torso * 0.8f, W * 0.6f}, L.vestC);
    for (int s = -1; s <= 1; s += 2) DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -torso / 2, 0), MatrixScale(W * 0.22f, torso * 1.05f, W * 0.22f)), MatrixRotateX(-sw * s * 0.8f)), MatrixMultiply(MatrixTranslate(s * W * 0.62f, legH + torso, 0), f)), L.body);
    float hy = legH + torso + H * 0.1f;
    BallM(f, {0, hy, 0}, {H * 0.13f, H * 0.15f, H * 0.13f}, L.head);
    if (L.face) { for (int s = -1; s <= 1; s += 2) BallM(f, {s * H * 0.04f, hy + H * 0.02f, H * 0.12f}, {0.035f, 0.035f, 0.02f}, Color{20, 20, 20, 255}); }
    if (L.hat) { BoxM(f, {0, hy + H * 0.12f, 0}, {H * 0.3f, H * 0.06f, H * 0.3f}, L.hatC); BoxM(f, {0, hy + H * 0.09f, H * 0.04f}, {H * 0.34f, H * 0.015f, H * 0.36f}, L.hatC); }
    if (L.lamp) BallM(f, {0, hy + H * 0.12f, H * 0.16f}, {0.06f, 0.06f, 0.04f}, Color{255, 250, 220, 255}, 2.0f);
    if (L.party) { DrawM(gCyl, MatrixMultiply(MatrixMultiply(MatrixScale(0.16f, 0.3f, 0.16f), MatrixTranslate(0, hy + H * 0.13f, 0)), f), Color{230, 60, 160, 255}); BoxM(f, {0, hy - 0.02f, H * 0.12f}, {0.16f, 0.025f, 0.02f}, Color{20, 20, 20, 255}); }
}

// ---------------------------------------------------------------- the things in a level
Color LootColor(int def) { uint32_t h = Hh((uint32_t)def * 2654435761u); return {(unsigned char)(90 + (h & 0x7F)), (unsigned char)(80 + ((h >> 8) & 0x7F)), (unsigned char)(70 + ((h >> 16) & 0x7F)), 255}; }
void DrawLoot(const Loot& l, Vector3 at, float t, float yaw) {
    if (l.def < 0) return; const LootDef& ld = D().loot[l.def]; Color c = LootColor(l.def); float emis = ld.props.find("glowing") != std::string::npos ? 1.4f : 0;
    float bob = ld.anomalous ? sinf(t * 2) * 0.05f + 0.1f : 0;
    if (ld.size == "s") Box({at.x, 0.08f + bob, at.z}, {0.22f, 0.12f, 0.16f}, c, yaw, emis);
    else if (ld.size == "m") Box({at.x, 0.25f + bob, at.z}, {0.5f, 0.5f, 0.4f}, c, yaw, emis);
    else if (ld.size == "l") Box({at.x, 0.45f, at.z}, {0.9f, 0.9f, 0.6f}, c, yaw, emis);
    else { Box({at.x, 0.6f, at.z}, {1.6f, 1.2f, 1.0f}, c, yaw, emis); Box({at.x, 1.22f, at.z}, {1.65f, 0.05f, 1.05f}, Mx(c, BLACK, 0.3f), yaw); }
}
void DrawLabFittings(const World& w, const Level& L, float t) {
    for (int k = 0; k < (int)L.labs.size(); k++) {
        const LabPlan& lp = L.labs[k]; const LabState* s = nullptr; for (const auto& x : w.labs) if (x.level == L.id && x.idx == k) s = &x;
        bool on = s && s->online && s->fuel > 0; Color brass{180, 140, 70, 255}, steel{130, 136, 142, 255};
        for (const auto& sp : lp.spots) {
            Vector3 a = sp.at;
            switch (sp.part) {
                case LP_RING: {   // the ring of machinery round the portal
                    float spin = s && s->charging ? t * (2 + s->charge * 6) : 0; float glow = s ? (s->openT > 0 ? 3.0f : s->charging ? 0.5f + s->charge * 1.5f : 0) : 0;
                    for (int q = 0; q < 16; q++) { float an = q * PI / 8 + spin; Box({a.x + cosf(an) * 1.6f, 1.5f + sinf(an) * 1.4f, a.z}, {0.35f, 0.35f, 0.5f}, q % 2 ? brass : steel, an, glow * 0.3f); }
                    if (glow > 0) Box({a.x, 1.5f, a.z}, {2.4f, 2.4f, 0.05f}, s && s->openT > 0 ? Color{200, 230, 255, 255} : Color{120, 160, 255, 255}, 0, glow);
                    Box({a.x, 0.05f, a.z}, {3.4f, 0.1f, 1.4f}, steel); break;
                }
                case LP_DESK: Box({a.x, 0.5f, a.z}, {1.8f, 1.0f, 0.7f}, steel); for (int q = 0; q < 3; q++) Box({a.x - 0.6f + q * 0.6f, 1.25f, a.z}, {0.45f, 0.4f, 0.4f}, Color{50, 50, 54, 255}, 0, on ? 0.2f : 0); break;
                case LP_CRATE: Box({a.x, 0.6f, a.z}, {1.3f, 1.2f, 1.0f}, Color{90, 110, 80, 255}); Box({a.x, 0.2f, a.z + 0.9f}, {1.2f, 0.12f, 0.8f}, Color{40, 40, 44, 255}); if (s) for (int q = 0; q < std::min(6, (int)s->crate.size()); q++) Box({a.x - 0.4f + (q % 3) * 0.4f, 1.3f + (q / 3) * 0.25f, a.z}, {0.3f, 0.2f, 0.3f}, LootColor(s->crate[q].def)); break;
                case LP_SHOP: Box({a.x, 0.95f, a.z}, {0.9f, 1.9f, 0.8f}, Color{150, 40, 40, 255}); Box({a.x, 1.4f, a.z - 0.41f}, {0.6f, 0.4f, 0.02f}, Color{200, 230, 255, 255}, 0, on ? 0.8f : 0); break;
                case LP_BUNK: Box({a.x, 0.4f, a.z}, {0.9f, 0.3f, 2.0f}, Color{90, 90, 70, 255}); Box({a.x, 1.3f, a.z}, {0.9f, 0.1f, 2.0f}, Color{90, 90, 70, 255}); break;
                case LP_GEN: Box({a.x, 0.7f, a.z}, {1.3f, 1.4f, 1.0f}, Color{170, 120, 40, 255}, 0, on ? 0.1f + 0.05f * sinf(t * 30) : 0); break;
                case LP_MONITORS: for (int q = 0; q < 6; q++) Box({a.x, 0.9f + (q / 3) * 0.55f, a.z - 0.6f + (q % 3) * 0.6f}, {0.3f, 0.45f, 0.55f}, Color{40, 44, 40, 255}, 0, on ? 0.6f : 0); break;
                case LP_ARCHIVE: for (int q = 0; q < 3; q++) Box({a.x, 0.7f, a.z - 0.6f + q * 0.6f}, {0.6f, 1.4f, 0.5f}, Color{110, 110, 100, 255}); break;
            }
        }
        // the blast door, shut or open
        Vector3 d = L.Center(lp.doorX, lp.doorZ); bool open = s && s->doorOpen;
        Box({d.x + (open ? 1.0f : 0), 1.3f, d.z}, {CELL * 0.95f, 2.6f, 0.3f}, Color{90, 92, 96, 255}); Box({d.x + (open ? 1.0f : 0), 2.0f, d.z}, {1.2f, 0.2f, 0.32f}, Color{240, 200, 40, 255});
        Vector3 br = L.Center(lp.breakerX, lp.breakerZ); Box({br.x, 1.2f, br.z}, {0.4f, 0.6f, 0.2f}, Color{60, 66, 60, 255}); Box({br.x, 1.25f, br.z - 0.12f}, {0.08f, 0.2f, 0.08f}, open ? Color{60, 200, 80, 255} : Color{220, 50, 40, 255}, 0, 0.8f);
    }
    // the exits: doors in frames, stairwells, ladders; noclip spots are just a little wrong
    for (const auto& e : L.exits) {
        Vector3 c = L.Center(e.cx, e.cz);
        if (!e.noclip) { Box({c.x, 1.1f, c.z}, {1.0f, 2.2f, 0.2f}, Color{140, 100, 60, 255}); Box({c.x, 2.3f, c.z}, {0.5f, 0.18f, 0.06f}, Color{40, 200, 80, 255}, 0, 1.2f); }
        else if (e.kind == "noclip") Box({c.x, 0.02f, c.z}, {CELL * 0.8f, 0.02f, CELL * 0.8f}, Color{250, 236, 200, 255}, 0, 0.1f + 0.1f * sinf(t * 1.7f));
    }
}
Look LookOf(const std::string& id) {
    Look k;
    if (id == "hound") { k.crawl = true; k.body = {150, 140, 130, 255}; k.head = {170, 160, 150, 255}; k.legs = k.body; }
    else if (id == "faceling") { k.body = {60, 70, 90, 255}; k.head = {220, 200, 190, 255}; k.legs = {50, 50, 60, 255}; k.face = false; }
    else if (id == "duller") { k.body = {110, 110, 110, 255}; k.head = {130, 130, 130, 255}; k.legs = k.body; k.face = false; }
    else if (id == "wretch") { k.body = {100, 104, 96, 255}; k.head = {120, 120, 110, 255}; k.legs = k.body; k.height = 1.5f; }
    else if (id == "skinstealer" || id == "mirrorthing" || id == "crew") { k.body = id == "crew" ? Color{40, 50, 80, 255} : Color{230, 225, 220, 255}; k.head = {240, 236, 230, 255}; k.legs = k.body; k.height = 2.2f; k.width = 0.35f; if (id == "mirrorthing") { k.body = {200, 230, 240, 255}; k.head = k.body; } }
    else if (id == "partygoer") { k.body = {250, 220, 60, 255}; k.head = {250, 230, 90, 255}; k.legs = k.body; k.party = true; }
    else if (id == "neighbor") { k.body = {20, 20, 24, 255}; k.head = {20, 20, 24, 255}; k.legs = k.body; k.face = false; k.height = 1.9f; }
    else if (id == "orderly") { k.body = {120, 170, 160, 255}; k.head = {230, 220, 210, 255}; k.legs = k.body; k.height = 2.4f; k.width = 0.4f; }
    else if (id == "patient") { k.body = {200, 210, 220, 255}; k.head = {220, 200, 190, 255}; k.legs = {220, 200, 190, 255}; k.gown = true; }
    else if (id == "warden") { k.body = {40, 40, 50, 255}; k.head = {200, 180, 170, 255}; k.legs = k.body; k.hat = true; k.hatC = {30, 30, 40, 255}; k.height = 2.0f; }
    else if (id == "friend") { k.body = {240, 120, 120, 255}; k.head = {240, 210, 190, 255}; k.legs = {60, 80, 160, 255}; k.height = 1.1f; k.width = 0.35f; }
    else if (id == "innkeeper") { k.body = {80, 40, 30, 255}; k.head = {220, 200, 180, 255}; k.legs = {30, 30, 30, 255}; k.height = 2.1f; }
    else if (id == "scarecrow") { k.body = {150, 120, 70, 255}; k.head = {200, 170, 110, 255}; k.legs = k.body; k.hat = true; k.hatC = {90, 70, 40, 255}; }
    else { k.body = {120, 120, 120, 255}; k.head = {150, 150, 150, 255}; k.legs = k.body; }
    return k;
}
void DrawEntity(const World& w, const Entity& e, float t, int me) {
    const EntityDef& ed = D().entities[e.def]; const std::string& id = ed.id; float ph = t * 6 + e.uid;
    // a Skin-Stealer wearing someone: their look
    if ((id == "skinstealer" || id == "crew") && e.mimicOf >= 0 && e.mimicOf < (int)w.crew.size()) { Look k; k.body = {60, 70, 80, 255}; k.head = {220, 190, 160, 255}; k.legs = {50, 50, 60, 255}; k.hat = k.vest = k.lamp = true; Figure(e.p, e.yaw, k, ph * 0.9f); return; }
    if (id == "smiler") {   // only a face of glowing eyes and teeth in the dark
        Matrix f = Frame(Vector3Add(e.p, {0, 1.6f, 0}), e.yaw);
        for (int s = -1; s <= 1; s += 2) BallM(f, {s * 0.18f, 0.12f, 0}, {0.13f, 0.08f, 0.04f}, {255, 255, 230, 255}, 3.0f);
        for (int q = -4; q <= 4; q++) BoxM(f, {q * 0.07f, -0.18f + fabsf((float)q) * 0.025f, 0}, {0.05f, 0.09f, 0.03f}, {255, 255, 240, 255}, 2.5f);
        return;
    }
    if (id == "clump") { for (int q = 0; q < 9; q++) { float a = q * 0.7f + t; BallM(Frame(Vector3Add(e.p, {0, e.st == ES_ATTACK ? 0.6f : 2.2f, 0}), 0), {cosf(a) * 0.4f, sinf(a * 1.3f) * 0.3f, sinf(a) * 0.4f}, {0.25f, 0.5f, 0.2f}, {170, 140, 130, 255}); } return; }
    if (id == "deathmoth") { Matrix f = Frame(Vector3Add(e.p, {0, 1.8f + sinf(t * 5 + e.uid) * 0.3f, 0}), e.yaw); BallM(f, {0, 0, 0}, {0.25f, 0.25f, 0.6f}, {120, 100, 80, 255}); for (int s = -1; s <= 1; s += 2) { float fl = sinf(t * 18 + e.uid) * 0.6f; DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(s * 0.6f, 0, 0), MatrixScale(1.2f, 0.03f, 0.9f)), MatrixRotateZ(s * fl)), f), {170, 150, 120, 255}); } return; }
    if (id == "spider") { Matrix f = Frame(Vector3Add(e.p, {0, 0.6f, 0}), e.yaw); BallM(f, {0, 0, -0.3f}, {0.8f, 0.6f, 0.9f}, {40, 34, 30, 255}); BallM(f, {0, 0, 0.4f}, {0.5f, 0.4f, 0.5f}, {50, 40, 34, 255}); for (int q = 0; q < 8; q++) { float s = q < 4 ? -1.0f : 1.0f, z = (q % 4 - 1.5f) * 0.3f; DrawM(gCube, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(s * 0.5f, 0, 0), MatrixScale(1.0f, 0.06f, 0.06f)), MatrixRotateZ(-s * (0.5f + sinf(ph + q) * 0.2f))), MatrixMultiply(MatrixTranslate(s * 0.2f, 0, z), f)), {40, 34, 30, 255}); } for (int q = 0; q < 6; q++) BallM(f, {(q - 2.5f) * 0.08f, 0.15f, 0.82f}, {0.05f, 0.05f, 0.03f}, {255, 60, 40, 255}, 2.0f); return; }
    if (id == "leviathan") { if (fmodf(t, 40) < 6) { float k = fmodf(t, 40) / 6; Box({e.p.x + 30, -3 - 2 * sinf(k * PI), e.p.z}, {40, 6, 12}, {10, 16, 20, 255}); } return; }
    if (id == "seer") { Matrix f = Frame(Vector3Add(e.p, {0, 1.5f, 0}), e.yaw); bool open = fmodf(t + e.uid, 7) > 1.5f; BallM(f, {0, 0, 0}, {0.3f, open ? 0.22f : 0.03f, 0.1f}, {240, 230, 220, 255}); if (open) BallM(f, {0, 0, 0.05f}, {0.1f, 0.1f, 0.05f}, {40, 20, 20, 255}); return; }
    if (id == "sentry") { Matrix f = Frame(Vector3Add(e.p, {0, 0, 0}), e.yaw); BoxM(f, {0, 0.6f, 0}, {0.6f, 0.9f, 0.6f}, {220, 224, 230, 255}); BallM(f, {0, 1.2f, 0}, {0.4f, 0.3f, 0.4f}, {200, 204, 210, 255}); BoxM(f, {0, 1.2f, 0.2f}, {0.25f, 0.06f, 0.05f}, {255, 40, 40, 255}, 3.0f); return; }
    Look k = LookOf(id);
    if (id == "scarecrow") { Box({e.p.x, 1.5f, e.p.z}, {0.12f, 3.0f, 0.12f}, {100, 80, 50, 255}); Figure(Vector3Add(e.p, {0, 1.0f, 0}), e.yaw, k, 0); return; }
    Figure(e.p, e.yaw, k, Vector2Length({e.v.x, e.v.z}) > 0.2f ? ph : 0);
    if (id == "partygoer") { BallM(Frame(Vector3Add(e.p, {0.5f, 2.6f + sinf(t * 1.5f) * 0.1f, 0}), 0), {0, 0, 0}, {0.35f, 0.42f, 0.35f}, {240, 60, 90, 255}); Box({e.p.x + 0.5f, 1.9f, e.p.z}, {0.01f, 1.2f, 0.01f}, WHITE); }
    (void)me;
}
void DrawCrewMember(const World& w, const Player& p, float t, bool asFaceling) {
    if (p.st == PS_SURFACE || p.st == PS_TAKEN) return;
    if (p.st == PS_DEAD) return;   // (Wanderers are invisible to the living)
    Look k; k.body = {70, 80, 100, 255}; k.head = {220, 186, 156, 255}; k.legs = {50, 56, 70, 255}; k.hat = k.vest = true; k.lamp = p.lamp && p.battery > 0;
    if (p.vest >= 0) { const std::string& id = D().cosmetics[p.vest].id; k.vestC = id == "vest_orange" ? Color{255, 120, 30, 255} : id == "vest_lime" ? Color{170, 240, 40, 255} : id == "vest_pink" ? Color{255, 120, 200, 255} : id == "vest_blue" ? Color{60, 120, 255, 255} : id == "vest_black" ? Color{30, 30, 34, 255} : k.vestC; }
    if (p.suitCos >= 0) { const std::string& id = D().cosmetics[p.suitCos].id; k.body = id == "suit_navy" ? Color{30, 40, 80, 255} : id == "suit_olive" ? Color{80, 90, 50, 255} : id == "wallpaper_suit" ? Color{200, 180, 90, 255} : id == "director_coat" ? Color{20, 20, 24, 255} : id == "suit_hivis" ? Color{250, 200, 30, 255} : k.body; }
    if (asFaceling) { k = LookOf("faceling"); }
    Vector3 at = p.p; if (p.st == PS_DOWNED) { Box(Vector3Add(at, {0, 0.25f, 0}), {0.5f, 0.3f, 1.6f}, k.body, p.yaw); Box(Vector3Add(at, {0, 0.25f, 0.9f}), {0.35f, 0.3f, 0.3f}, k.head, p.yaw); return; }
    Figure(at, p.yaw, k, Vector2Length({p.vel.x, p.vel.z}) > 0.3f ? t * 8 + p.id : 0);
    if (p.hands.def >= 0) DrawLoot(p.hands, Vector3Add(at, {cosf(p.yaw) * 0.5f, 0.9f, sinf(p.yaw) * 0.5f}), t, p.yaw);
    (void)w;
}

}  // namespace

// ---------------------------------------------------------------- the frame
static void SetCommon(const World& w, const View& v, const Level* L, LevelGfx* G) {
    const LevelDef* def = L ? &D().levels[L->id] : nullptr;
    float t = v.t, breath = std::clamp((70 - v.sanity) / 70.0f, 0.0f, 1.0f) * (v.ghost ? 0 : 1);
    SetShaderValue(S3.sh, S3.time, &t, SHADER_UNIFORM_FLOAT); SetShaderValue(S3.sh, S3.breath, &breath, SHADER_UNIFORM_FLOAT);
    Vector2 ls = L ? Vector2{L->w * CELL, L->h * CELL} : Vector2{40, 30}; SetShaderValue(S3.sh, S3.levelSize, &ls, SHADER_UNIFORM_VEC2);
    Color lc = def ? def->light : Color{255, 170, 80, 255}; Vector3 lcol = {lc.r / 255.0f, lc.g / 255.0f, lc.b / 255.0f}; SetShaderValue(S3.sh, S3.lightColor, &lcol, SHADER_UNIFORM_VEC3);
    float amb = def ? (def->id == 6 ? 0.0f : def->id == 10 || def->id == 11 ? (w.overtime ? 0.05f : 0.45f) : def->id == 9 ? 0.03f : 0.06f) : 0.08f; if (w.overtime && def && def->id != 10 && def->id != 11) amb *= 0.3f; if (v.ghost) amb += 0.25f;
    SetShaderValue(S3.sh, S3.ambient, &amb, SHADER_UNIFORM_FLOAT);
    float ceil = def ? def->ceil : 6; SetShaderValue(S3.sh, S3.ceil, &ceil, SHADER_UNIFORM_FLOAT);
    Vector3 lp = v.cam.position, ld = Vector3Normalize(Vector3Subtract(v.cam.target, v.cam.position)); float on = v.lamp ? 1.0f : 0.0f, range = v.lampRange * (v.flashlight ? 1.8f : 1), cone = v.flashlight ? 0.93f : 0.8f;
    SetShaderValue(S3.sh, S3.lampPos, &lp, SHADER_UNIFORM_VEC3); SetShaderValue(S3.sh, S3.lampDir, &ld, SHADER_UNIFORM_VEC3); SetShaderValue(S3.sh, S3.lampOn, &on, SHADER_UNIFORM_FLOAT); SetShaderValue(S3.sh, S3.lampRange, &range, SHADER_UNIFORM_FLOAT); SetShaderValue(S3.sh, S3.lampCone, &cone, SHADER_UNIFORM_FLOAT);
    Color fogC = def ? (def->id == 6 ? Color{0, 0, 0, 255} : def->id == 10 ? Color{170, 170, 160, 255} : def->id == 11 ? Color{170, 176, 186, 255} : def->id == 9 ? Color{6, 6, 12, 255} : Mx(def->wall, BLACK, 0.55f)) : Color{20, 14, 8, 255};
    if (w.overtime && def && def->id != 10 && def->id != 11) fogC = {0, 0, 0, 255};
    Vector3 fog = {fogC.r / 255.0f, fogC.g / 255.0f, fogC.b / 255.0f}; float fd = def ? (def->id == 6 ? 0.12f : def->id == 8 ? 0.07f : def->id == 10 ? 0.025f : def->id == 1 ? 0.04f : 0.035f) : 0.04f;
    SetShaderValue(S3.sh, S3.fog, &fog, SHADER_UNIFORM_VEC3); SetShaderValue(S3.sh, S3.fogDensity, &fd, SHADER_UNIFORM_FLOAT); SetShaderValue(S3.sh, S3.cam, &lp, SHADER_UNIFORM_VEC3);
    // point lights: dropped flares and glow sticks, open portals, carried glowing loot
    Vector4 pts[8], cols[8]; int n = 0;
    for (const auto& it : w.items) { if (n >= 8 || it.level != v.level) continue; if (it.loot.def < 0 && it.noiseT > 0) { const std::string& u = D().items[-1 - it.loot.def].use; if (u == "flare") { pts[n] = {it.p.x, 0.4f, it.p.z, 12}; cols[n] = {1, 0.25f, 0.15f, 1.6f + 0.3f * sinf(v.t * 20)}; n++; } else if (u == "glowstick") { pts[n] = {it.p.x, 0.2f, it.p.z, 5}; cols[n] = {0.3f, 1, 0.4f, 0.8f}; n++; } } else if (it.loot.def >= 0 && D().loot[it.loot.def].props.find("glowing") != std::string::npos && Vector3Distance(it.p, v.cam.position) < 25) { pts[n] = {it.p.x, 0.5f, it.p.z, 5}; cols[n] = {0.6f, 0.8f, 1, 0.7f}; n++; } }
    for (const auto& l : w.labs) if (n < 8 && l.level == v.level && (l.openT > 0 || l.charging) && L) { Vector3 r = L->labs[l.idx].spots[0].at; pts[n] = {r.x, 1.5f, r.z, 10}; cols[n] = {0.5f, 0.7f, 1, l.openT > 0 ? 2.0f : 0.4f + l.charge}; n++; }
    for (const auto& p : w.crew) if (n < 8 && p.level == v.level && p.Alive() && p.id != v.me && p.lamp && p.battery > 0) { pts[n] = {p.p.x + cosf(p.yaw) * 2, 1.0f, p.p.z + sinf(p.yaw) * 2, 6}; cols[n] = {1, 0.95f, 0.8f, 0.6f}; n++; }
    SetShaderValueV(S3.sh, S3.point, pts, SHADER_UNIFORM_VEC4, std::max(1, n)); SetShaderValueV(S3.sh, S3.pointCol, cols, SHADER_UNIFORM_VEC4, std::max(1, n)); SetShaderValue(S3.sh, S3.points, &n, SHADER_UNIFORM_INT);
    static Texture2D dark{}; if (!dark.id) { Image i = GenImageColor(2, 2, BLACK); dark = LoadTextureFromImage(i); UnloadImage(i); }
    Texture2D lt = G ? G->lightTex : dark;
    for (Model* m : {&gCube, &gSphere, &gCyl}) if (m->meshCount) m->materials[0].maps[MATERIAL_MAP_EMISSION].texture = lt;
    if (G) for (Model* m : {&G->walls, &G->floors, &G->ceils, &G->low, &G->glass, &G->water, &G->lab, &G->wheat}) if (m->meshCount) m->materials[0].maps[MATERIAL_MAP_EMISSION].texture = lt;
    Emis(0);
}
static void VhsBlit(const View& v) {
    RenderTexture2D& rt3d = Mode3DRT();
    float t = v.t; float noise = std::clamp(v.noise, 0.0f, 1.0f); Vector2 res{(float)SCREEN_W, (float)SCREEN_H}; float black = std::clamp(v.blackout, 0.0f, 1.0f);
    SetShaderValue(gVhs, gVhsTime, &t, SHADER_UNIFORM_FLOAT); SetShaderValue(gVhs, gVhsNoise, &noise, SHADER_UNIFORM_FLOAT); SetShaderValue(gVhs, gVhsRes, &res, SHADER_UNIFORM_VEC2); SetShaderValue(gVhs, gVhsBlack, &black, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(gVhs);
    DrawTexturePro(rt3d.texture, {0, 0, (float)rt3d.texture.width, -(float)rt3d.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    EndShaderMode();
}
void Render(const World& w, const View& v) {
    EnsureShaders(); Prims();
    auto it = w.levels.find(v.level); if (it == w.levels.end()) { ClearBackground(BLACK); return; }
    const Level& L = it->second; LevelGfx& G = Gfx(L);
    UpdateLights(w, L, G, v.t, v.sanity, v.me >= 0 && v.me < (int)w.crew.size() ? &w.crew[v.me] : nullptr);
    const LevelDef& def = D().levels[L.id];
    BeginLayer(Mode3DRT());
    Color clear = def.id == 6 || w.overtime ? BLACK : def.id == 10 ? Color{150, 150, 145, 255} : def.id == 11 ? Color{170, 176, 186, 255} : def.id == 9 ? Color{4, 4, 10, 255} : Mx(def.wall, BLACK, 0.55f);
    ClearBackground(clear);
    BeginMode3D(v.cam);
    SetCommon(w, v, &L, &G);
    rlDisableBackfaceCulling();
    for (Model* m : {&G.floors, &G.ceils, &G.walls, &G.low, &G.lab, &G.wheat}) if (m->meshCount) { m->materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE; DrawModel(*m, {0, 0, 0}, 1, WHITE); }
    DrawLabFittings(w, L, v.t);
    for (const auto& item : w.items) if (item.level == v.level && Vector3Distance(item.p, v.cam.position) < 45) { if (item.loot.def >= 0) DrawLoot(item.loot, item.p, v.t, (item.loot.uid % 7) * 0.9f); else { const std::string& u = D().items[-1 - item.loot.def].use; Color c = u == "flare" ? Color{255, 60, 40, 255} : u == "glowstick" ? Color{80, 255, 120, 255} : u == "musicbox" ? Color{200, 160, 90, 255} : Color{150, 150, 160, 255}; Box(Vector3Add(item.p, {0, 0.1f, 0}), {0.12f, 0.12f, 0.3f}, c, 0, u == "flare" || u == "glowstick" ? 3.0f : 0); } }
    for (const auto& e : w.ents) if (e.level == v.level && Vector3Distance(e.p, v.cam.position) < 50) DrawEntity(w, e, v.t, v.me);
    for (const auto& e : v.fakes) DrawEntity(w, e, v.t, v.me);
    for (const auto& p : w.crew) if (p.level == v.level && p.id != v.me) DrawCrewMember(w, p, v.t, v.teammatesAsFacelings);
    // the transparent last: glass, water
    for (Model* m : {&G.glass, &G.water}) if (m->meshCount) { BeginBlendMode(BLEND_ALPHA); m->materials[0].maps[MATERIAL_MAP_DIFFUSE].color = m == &G.water ? Color{120, 160, 200, 170} : Color{255, 255, 255, 120}; DrawModel(*m, {0, 0, 0}, 1, WHITE); EndBlendMode(); }
    rlEnableBackfaceCulling();
    EndMode3D(); EndLayer();
    VhsBlit(v);
}
// the Surface: the Bureau's warehouse at night under sodium light (a small fixed room drawn the same way)
void RenderSurface(const World& w, const View& v) {
    EnsureShaders(); Prims();
    BeginLayer(Mode3DRT()); ClearBackground(Color{12, 8, 4, 255});
    BeginMode3D(v.cam); SetCommon(w, v, nullptr, nullptr);
    float amb = 0.5f; SetShaderValue(S3.sh, S3.ambient, &amb, SHADER_UNIFORM_FLOAT);
    Vector4 pts[4] = {{6, 6, 6, 18}, {16, 6, 6, 18}, {26, 6, 6, 18}, {16, 3, 16, 12}}; Vector4 cols[4] = {{1, 0.6f, 0.2f, 1.4f}, {1, 0.6f, 0.2f, 1.4f}, {1, 0.6f, 0.2f, 1.4f}, {0.5f, 0.7f, 1, 0.6f}}; int n = 4;
    SetShaderValueV(S3.sh, S3.point, pts, SHADER_UNIFORM_VEC4, n); SetShaderValueV(S3.sh, S3.pointCol, cols, SHADER_UNIFORM_VEC4, n); SetShaderValue(S3.sh, S3.points, &n, SHADER_UNIFORM_INT);
    rlDisableBackfaceCulling();
    Box({16, -0.05f, 10}, {32, 0.1f, 20}, Color{110, 110, 112, 255}); Box({16, 4, 0}, {32, 8, 0.2f}, Color{120, 116, 108, 255}); Box({0, 4, 10}, {0.2f, 8, 20}, Color{120, 116, 108, 255}); Box({32, 4, 10}, {0.2f, 8, 20}, Color{120, 116, 108, 255});
    Box({16, 8, 10}, {32, 0.2f, 20}, Color{60, 58, 54, 255});
    for (int k = 0; k < 3; k++) Box({6.0f + k * 10, 7.6f, 6}, {1.2f, 0.2f, 0.4f}, Color{255, 170, 60, 255}, 0, 2.5f);   // the sodium lamps
    // the portal ring, identical to the Labs'; the sales counter; the whiteboard; pallets; the loading bay door, the Fence's van beyond
    for (int q = 0; q < 16; q++) { float an = q * PI / 8 + v.t * 0.2f; Box({16 + cosf(an) * 1.6f, 1.5f + sinf(an) * 1.4f, 3}, {0.35f, 0.35f, 0.5f}, q % 2 ? Color{180, 140, 70, 255} : Color{130, 136, 142, 255}, an); }
    Box({7, 0.55f, 10}, {5, 1.1f, 1}, Color{80, 60, 40, 255}); Box({26, 2.2f, 0.2f}, {6, 2.4f, 0.1f}, Color{235, 235, 228, 255}, 0, 0.2f);
    for (int k = 0; k < 5; k++) Box({4.0f + k * 2.4f, 0.4f, 17}, {2.0f, 0.8f, 1.6f}, Color{150, 120, 80, 255});
    for (size_t k = 0; k < w.bay.size() && k < 30; k++) DrawLoot(w.bay[k], {12.0f + (k % 6) * 1.1f, 0, 7.5f + (k / 6) * 1.1f}, v.t, 0);
    Box({24, 3, 19.9f}, {8, 6, 0.2f}, Color{70, 74, 78, 255});   // the loading door
    Box({30, 1.2f, 15}, {2.4f, 2.2f, 5}, Color{200, 200, 196, 255}); Box({30, 1.0f, 12.4f}, {2.2f, 1.2f, 0.1f}, Color{60, 80, 100, 255});   // Dez's van
    rlEnableBackfaceCulling();
    EndMode3D(); EndLayer();
    VhsBlit(v);
}
void Stamp(const std::string& left, const std::string& right, float t) {
    Color c{235, 235, 225, 255};
    if (fmodf(t, 1.6f) < 1.0f) { DrawCircle(36, 34, 8, Color{230, 30, 30, 255}); }
    TxtBold("REC", 52, 22, 22, c);
    TxtBold(left, 30, SCREEN_H - 50.0f, 22, c);
    TxtBold(right, SCREEN_W - 30.0f - MeasureTxt(right, 22, true), SCREEN_H - 50.0f, 22, c);
}
void Unload() { for (auto& g : gG) { for (Model* m : {&g.second.walls, &g.second.floors, &g.second.ceils, &g.second.low, &g.second.glass, &g.second.water, &g.second.lab, &g.second.wheat}) if (m->meshCount) UnloadModel(*m); if (g.second.lightTex.id) UnloadTexture(g.second.lightTex); } gG.clear(); }

}  // namespace ncr
