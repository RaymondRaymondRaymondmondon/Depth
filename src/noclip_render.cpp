// NOCLIP's renderer (see noclip_render.h).
#include "noclip_render.h"
#include "game.h"
#include "rlgl.h"
#include "figure3d.h"
#include "redtide_render.h"
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
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor; in vec4 vertexBoneIds; in vec4 vertexBoneWeights;
uniform mat4 mvp; uniform mat4 matModel; uniform float uTime; uniform float uBreath; uniform mat4 boneMatrices[64]; uniform int uSkinned;
out vec2 fragTexCoord; out vec3 fragPos; out vec3 fragNormal; out vec4 fragColor;
void main() {
    vec3 p = vertexPosition; vec3 n = vertexNormal;
    if (uSkinned == 1) {   // (the crew rig's figures: GPU skinning, as raylib uploads mesh.boneMatrices)
        mat4 s = boneMatrices[int(vertexBoneIds.x)] * vertexBoneWeights.x + boneMatrices[int(vertexBoneIds.y)] * vertexBoneWeights.y
               + boneMatrices[int(vertexBoneIds.z)] * vertexBoneWeights.z + boneMatrices[int(vertexBoneIds.w)] * vertexBoneWeights.w;
        p = (s * vec4(p, 1.0)).xyz; n = normalize(mat3(s) * n);
        gl_Position = mvp * vec4(p, 1.0); vec4 wq = matModel * vec4(p, 1.0); fragPos = wq.xyz; fragNormal = normalize(mat3(matModel) * n); fragTexCoord = vertexTexCoord; fragColor = vertexColor; return;
    }
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
uniform sampler2D texture0; uniform float uTime; uniform float uNoise; uniform vec2 uRes; uniform float uBlack; uniform float uTape;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec2 uv = fragTexCoord;
    // (the playtest: "a little sickening": uTape scales the tape's wobble, tear, colour fringe, smear and grain; 0 a clean picture,
    // 0.35 the default light touch, 1 the full camcorder. Fear still adds its noise.)
    float k = uTape;
    float wob = sin(uv.y * 220.0 + uTime * 3.0) * 0.0007 * (k + uNoise * 4.0);
    float band = fract(uv.y * 0.6 - uTime * 0.07); float tear = smoothstep(0.0, 0.015, band) * smoothstep(0.03, 0.015, band) * min(1.0, k + uNoise);
    uv.x += wob + tear * 0.004 * (1.0 + uNoise * 3.0);
    float ca = 0.0022 * (k + uNoise * 1.5);
    vec3 c = vec3(texture(texture0, uv + vec2(ca, 0.0)).r, texture(texture0, uv).g, texture(texture0, uv - vec2(ca, 0.0)).b);
    c = mix(c, (texture(texture0, uv + vec2(0.003, 0.0)).rgb + texture(texture0, uv - vec2(0.003, 0.0)).rgb) * 0.5, 0.25 * k);   // the smear
    float scan = 1.0 - 0.1 * k + 0.1 * k * sin(uv.y * uRes.y * 3.14159);
    float n = hash(uv * uRes + floor(uTime * 30.0));
    c = c * scan + (n - 0.5) * (0.05 * k + uNoise * 0.12) + tear * 0.06;
    vec2 v = uv - 0.5; c *= 1.0 - dot(v, v) * 0.75;
    float g = dot(c, vec3(0.3, 0.59, 0.11)); c = mix(c, vec3(g), 0.12); c = pow(max(c, 0.0), vec3(0.95));
    c *= 1.0 - uBlack;
    finalColor = vec4(c, 1.0);
})";

struct Shader3D { Shader sh{}; int skinned = -1; int time, breath, light, levelSize, lightColor, ambient, ceil, lampPos, lampDir, lampOn, lampRange, lampCone, fog, fogDensity, cam, emissive, point, pointCol, points; bool ok = false; };
Shader3D S3; Shader gVhs{}; int gVhsTime, gVhsNoise, gVhsRes, gVhsBlack, gVhsTape; bool gVhsOk = false;
void EnsureShaders() {
    if (S3.ok) return;
    S3.sh = LoadShaderFromMemory(LEVEL_VS, LEVEL_FS); S3.ok = true;
    auto loc = [&](const char* n) { return GetShaderLocation(S3.sh, n); };
    S3.time = loc("uTime"); S3.breath = loc("uBreath"); S3.light = loc("uLight"); S3.levelSize = loc("uLevelSize"); S3.lightColor = loc("uLightColor"); S3.ambient = loc("uAmbient"); S3.ceil = loc("uCeil");
    S3.lampPos = loc("uLampPos"); S3.lampDir = loc("uLampDir"); S3.lampOn = loc("uLampOn"); S3.lampRange = loc("uLampRange"); S3.lampCone = loc("uLampCone"); S3.fog = loc("uFog"); S3.fogDensity = loc("uFogDensity"); S3.cam = loc("uCam"); S3.emissive = loc("uEmissive");
    S3.point = loc("uPoint"); S3.pointCol = loc("uPointCol"); S3.points = loc("uPoints"); S3.skinned = loc("uSkinned");
    S3.sh.locs[SHADER_LOC_MATRIX_MODEL] = loc("matModel");
    S3.sh.locs[SHADER_LOC_MAP_EMISSION] = loc("uLight");   // (the light grid rides along as a material map: DrawMesh binds those)
    gVhs = LoadShaderFromMemory(nullptr, VHS_FS); gVhsTime = GetShaderLocation(gVhs, "uTime"); gVhsNoise = GetShaderLocation(gVhs, "uNoise"); gVhsRes = GetShaderLocation(gVhs, "uRes"); gVhsBlack = GetShaderLocation(gVhs, "uBlack"); gVhsTape = GetShaderLocation(gVhs, "uTape"); gVhsOk = true;
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
    const LevelDef& def = D().levels[L.id]; float C = CELL, ceil = def.ceil, labCeil = std::max(ceil, 4.2f);   // (the Labs are tall rooms: the portal ring stands 3.3 m)
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
            if (roomRoof && t != T_PIT && !(L.id == 7 && t == T_DEEP)) { float cy = t == T_LABFLOOR ? labCeil : ceil; (t == T_LABFLOOR ? lab : ceils).Quad({x0, cy, z0}, {x1, cy, z0}, {x1, cy, z1}, {x0, cy, z1}, {0, -1, 0}, {x0 / C, z0 / C}, {x1 / C, z0 / C}, {x1 / C, z1 / C}, {x0 / C, z1 / C}); }
            if (t == T_LABFLOOR && labCeil > ceil) {   // where a lower corridor or wall meets the Lab: close the gap above it
                const int D4[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                for (auto& d : D4) { int nx = x + d[0], nz = z + d[1]; uint8_t nt = L.At(nx, nz); if (nt == T_LABFLOOR || nt == T_LABWALL) continue;
                    float lo = solidish(nx, nz) ? WallH(L, nx, nz) : ceil; if (lo >= labCeil) continue; float v0 = lo / C, v1 = labCeil / C;
                    if (d[1] == -1) lab.Quad({x0, lo, z0}, {x1, lo, z0}, {x1, labCeil, z0}, {x0, labCeil, z0}, {0, 0, 1}, {0, v0}, {1, v0}, {1, v1}, {0, v1});
                    if (d[1] == 1) lab.Quad({x1, lo, z1}, {x0, lo, z1}, {x0, labCeil, z1}, {x1, labCeil, z1}, {0, 0, -1}, {0, v0}, {1, v0}, {1, v1}, {0, v1});
                    if (d[0] == -1) lab.Quad({x0, lo, z1}, {x0, lo, z0}, {x0, labCeil, z0}, {x0, labCeil, z1}, {1, 0, 0}, {0, v0}, {1, v0}, {1, v1}, {0, v1});
                    if (d[0] == 1) lab.Quad({x1, lo, z0}, {x1, lo, z1}, {x1, labCeil, z1}, {x1, labCeil, z0}, {-1, 0, 0}, {0, v0}, {1, v0}, {1, v1}, {0, v1}); }
            }
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
        float h = WallH(L, x, z); if (h <= 0) continue; if (t == T_LABWALL) h = std::max(h, labCeil);
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
    bool out = w.overtime || (L.id == 1 && w.lightsOutT > 0) || w.mode == 3 || (L.id == 3 && !w.power);
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

// ---------------------------------------------------------------- people on the shared crew rig (tools/artgen/noclip_crew.py)
// The Bureau's yellow hazmat suit (and the orange costume) as skinned figures, posed by fig::PoseFigure and drawn
// through the level shader (lit by the light grid and the lamps, fogged); the Backrooms' human-shaped cast wear the
// same suit recoloured (a Faceling's mask is skin: no face).
Texture2D gCurLight{};
struct Rc { const char* mat; Color c; };
struct SuitLook { int model = 0; std::vector<Rc> rc; float height = 1, build = 1, emis = 0; float walkMul = 1; bool crawl = false; float tread = 0; };
bool DrawSuit(const SuitLook& L, Vector3 at, float yaw, float t, float walk, float walkPh, Matrix pre = MatrixIdentity(), Vector3* headOut = nullptr) {
    const Model* m = rt::LoadAsset(L.model == 1 ? "noclip/crew_orange.glb" : "noclip/crew_hazmat.glb");
    if (!m || !S3.ok) return false;
    static std::map<const Model*, std::vector<Color>> orig; static std::map<const Model*, std::map<std::string, int>> byName;
    if (!orig.count(m)) {
        std::vector<Color> o; for (int i = 0; i < m->materialCount; i++) o.push_back(m->materials[i].maps[MATERIAL_MAP_DIFFUSE].color); orig[m] = o;
        for (const char* n : {"suit", "trim", "glove", "boot", "sole", "mask", "lens", "patch", "badge", "metal", "skin", "seal", "stripe"}) { Material mt; if (rt::AssetMaterial(m, n, &mt)) for (int i = 0; i < m->materialCount; i++) if (m->materials[i].maps == mt.maps) byName[m][n] = i; }
    }
    fig::Pose P; P.walk = walk; P.walkPh = walkPh * L.walkMul; P.breathe = t * 1.6f; P.grip = 0.55f; P.blink = 0;
    P.tread = L.tread;
    if (L.crawl) { P.crouch = 0.75f; P.reach = 1; P.elbow = 0.1f + 0.25f * (0.5f + 0.5f * sinf(walkPh * L.walkMul)) * walk; P.grip = 0.9f; P.nod = -0.9f;   // (on all fours: the body pitched forward, arms down to the floor as forelegs, the head up)
        pre = MatrixMultiply(MatrixMultiply(MatrixRotateZ(-1.05f), MatrixTranslate(-0.35f, 0.42f, 0)), pre); }
    fig::Build B; B.height = L.height; B.build = L.build;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    Matrix frame = MatrixMultiply(pre, fig::Frame(at, -yaw));
    std::vector<Color> col = orig[m]; for (const auto& r : L.rc) { auto it = byName[m].find(r.mat); if (it != byName[m].end()) col[it->second] = r.c; }
    int one = 1, zero = 0; SetShaderValue(S3.sh, S3.skinned, &one, SHADER_UNIFORM_INT);
    if (L.emis != gEmissive) Emis(L.emis);
    for (int i = 0; i < m->meshCount; i++) {
        Mesh& mesh = m->meshes[i];
        if (mesh.boneMatrices && mesh.boneCount > 0) memcpy(mesh.boneMatrices, skin.data(), sizeof(Matrix) * std::min((int)skin.size(), mesh.boneCount));
        Material mat = m->materials[m->meshMaterial[i]]; mat.shader = S3.sh;
        Material copy = mat; MaterialMap maps[12]; memcpy(maps, mat.maps, sizeof(maps)); copy.maps = maps;   // (12: raylib's MAX_MATERIAL_MAPS)
        maps[MATERIAL_MAP_DIFFUSE].color = col[m->meshMaterial[i]];
        maps[MATERIAL_MAP_DIFFUSE].texture = White(); maps[MATERIAL_MAP_EMISSION].texture = gCurLight;
        DrawMesh(mesh, copy, frame);
    }
    SetShaderValue(S3.sh, S3.skinned, &zero, SHADER_UNIFORM_INT);
    if (headOut) { const rt::RigInfo& rig = rt::RigOf(*m); int hb = rig.Find("head"); Matrix hw = hb >= 0 ? rt::BoneWorld(rig, skin, hb, frame) : frame; *headOut = {hw.m12, hw.m13, hw.m14}; }
    return true;
}
SuitLook SuitOf(const std::string& id) {   // the Backrooms' human-shaped cast in the suit's shape
    SuitLook s; Color skin{214, 180, 150, 255};
    auto rc = [&](std::initializer_list<Rc> l) { s.rc.assign(l.begin(), l.end()); };
    if (id == "hound") { rc({{"suit", {150, 140, 132, 255}}, {"trim", {140, 128, 120, 255}}, {"mask", {170, 160, 150, 255}}, {"lens", {30, 10, 10, 255}}, {"patch", {110, 100, 96, 255}}, {"glove", {160, 150, 140, 255}}, {"boot", {150, 140, 132, 255}}, {"sole", {150, 140, 132, 255}}, {"metal", {60, 20, 20, 255}}}); s.crawl = true; s.build = 0.85f; s.walkMul = 1.6f; }
    else if (id == "scarecrow") { rc({{"suit", {150, 120, 70, 255}}, {"trim", {120, 90, 50, 255}}, {"mask", {200, 170, 110, 255}}, {"lens", {20, 16, 10, 255}}, {"patch", {90, 70, 40, 255}}, {"glove", {200, 180, 110, 255}}, {"boot", {120, 90, 50, 255}}, {"sole", {120, 90, 50, 255}}, {"metal", {90, 70, 40, 255}}}); s.tread = 1; }
    else if (id == "faceling") rc({{"suit", {58, 66, 86, 255}}, {"trim", {48, 54, 70, 255}}, {"mask", skin}, {"lens", skin}, {"patch", {58, 66, 86, 255}}, {"glove", skin}, {"boot", {40, 40, 46, 255}}, {"sole", {40, 40, 46, 255}}, {"metal", skin}});
    else if (id == "duller") rc({{"suit", {120, 120, 120, 255}}, {"trim", {110, 110, 110, 255}}, {"mask", {130, 130, 130, 255}}, {"lens", {130, 130, 130, 255}}, {"patch", {110, 110, 110, 255}}, {"glove", {120, 120, 120, 255}}, {"boot", {100, 100, 100, 255}}, {"sole", {100, 100, 100, 255}}, {"metal", {130, 130, 130, 255}}});
    else if (id == "wretch") { rc({{"suit", {92, 98, 84, 255}}, {"trim", {80, 84, 70, 255}}, {"mask", {120, 112, 96, 255}}, {"lens", {200, 40, 30, 255}}, {"patch", {60, 40, 30, 255}}, {"glove", {120, 112, 96, 255}}}); s.height = 0.85f; s.build = 0.9f; s.walkMul = 1.4f; }
    else if (id == "survivor") rc({{"suit", {170, 140, 60, 255}}, {"trim", {140, 110, 40, 255}}, {"patch", {60, 40, 20, 255}}});
    else if (id == "partygoer") rc({{"suit", {250, 214, 70, 255}}, {"trim", {240, 120, 180, 255}}, {"mask", {255, 250, 230, 255}}, {"lens", {20, 20, 20, 255}}, {"patch", {240, 80, 160, 255}}});
    else if (id == "neighbor") { rc({{"suit", {18, 18, 22, 255}}, {"trim", {18, 18, 22, 255}}, {"mask", {18, 18, 22, 255}}, {"lens", {18, 18, 22, 255}}, {"patch", {18, 18, 22, 255}}, {"glove", {18, 18, 22, 255}}, {"boot", {18, 18, 22, 255}}, {"sole", {18, 18, 22, 255}}, {"metal", {18, 18, 22, 255}}}); s.height = 1.06f; }
    else if (id == "orderly") { rc({{"suit", {120, 170, 160, 255}}, {"trim", {100, 150, 140, 255}}, {"mask", {230, 230, 225, 255}}, {"lens", {30, 30, 30, 255}}, {"patch", {200, 200, 196, 255}}}); s.height = 1.3f; s.build = 0.9f; }
    else if (id == "patient") rc({{"suit", {200, 210, 220, 255}}, {"trim", {180, 190, 200, 255}}, {"mask", skin}, {"lens", {40, 40, 40, 255}}, {"glove", skin}, {"boot", skin}, {"sole", skin}, {"patch", {170, 180, 190, 255}}});
    else if (id == "warden") { rc({{"suit", {40, 40, 50, 255}}, {"trim", {30, 30, 40, 255}}, {"mask", {200, 180, 170, 255}}, {"lens", {20, 20, 20, 255}}, {"patch", {200, 170, 60, 255}}}); s.height = 1.12f; s.build = 1.15f; }
    else if (id == "friend") { rc({{"suit", {240, 120, 120, 255}}, {"trim", {60, 80, 160, 255}}, {"mask", {240, 210, 190, 255}}, {"lens", {20, 20, 20, 255}}}); s.height = 0.62f; }
    else if (id == "innkeeper") { rc({{"suit", {80, 40, 30, 255}}, {"trim", {60, 30, 24, 255}}, {"mask", {220, 200, 180, 255}}, {"lens", {20, 20, 20, 255}}}); s.height = 1.15f; s.build = 1.2f; }
    else if (id == "mirrorthing") { rc({{"suit", {200, 230, 240, 255}}, {"trim", {200, 230, 240, 255}}, {"mask", {220, 240, 250, 255}}, {"lens", {255, 255, 255, 255}}, {"glove", {200, 230, 240, 255}}, {"boot", {200, 230, 240, 255}}}); s.emis = 0.25f; }
    else if (id == "skinstealer" || id == "crew") { rc({{"suit", {230, 225, 220, 255}}, {"trim", {230, 225, 220, 255}}, {"mask", {240, 236, 230, 255}}, {"lens", {240, 236, 230, 255}}, {"glove", {240, 236, 230, 255}}, {"boot", {230, 225, 220, 255}}, {"patch", {230, 225, 220, 255}}}); s.height = 1.22f; s.build = 0.85f; }
    else s.model = -1;   // (not human-shaped: the boxes)
    return s;
}

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
// the loot as real things (tools/artgen/noclip_props.py): each name to an archetype model by its words
const char* PropArchOf(const std::string& nameIn) {
    std::string n = nameIn; for (auto& ch : n) ch = (char)tolower(ch);
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    static const std::pair<const char*, const char*> MAP[] = {
        {"battery box", "batterybox"}, {"car battery", "batterybox"}, {"tractor battery", "batterybox"}, {"batteries", "batteries"},
        {"wallet", "wallet"}, {"vhs", "tape"}, {"keys on", "keys"}, {"lanyard", "keys"}, {"flashlight", "flashlight"}, {"backpack", "backpack"},
        {"office chair", "chair"}, {"desk lamp", "lamp"}, {"pallet", "pallet"}, {"can of food", "can"}, {"badge", "badge"}, {"key card", "badge"},
        {"usb", "badge"}, {"data drive", "badge"}, {"crate", "crate"}, {"chest", "crate"}, {"toolbox", "toolbox"}, {"lunchbox", "toolbox"},
        {"jeweler", "toolbox"}, {"register drawer", "toolbox"}, {"jerrycan", "jerrycan"}, {"mask", "mask"}, {"gauge", "gauge"}, {"valve", "valve"},
        {"jar", "jar"}, {"egg", "jar"}, {"copper pipe", "pipe"}, {"fuse", "fuse"}, {"radio tube", "fuse"}, {"radio", "radio"}, {"coil", "coil"},
        {"transformer", "coil"}, {"stapler", "stapler"}, {"album", "book"}, {"photo", "photo"}, {"ledger", "book"}, {"journal", "book"},
        {"yearbook", "book"}, {"log", "book"}, {"files", "book"}, {"computer", "tower"}, {"server", "tower"}, {"console", "tower"},
        {"coffee", "coffee"}, {"plant", "plant"}, {"filing cabinet", "cabinet"}, {"jewelry", "jewelry"}, {"cufflink", "jewelry"},
        {"pocket watch", "jewelry"}, {"gramophone", "gramophone"}, {"champagne", "bottle"}, {"almond water", "bottle"}, {"tray", "tray"},
        {"silver", "tray"}, {"clock", "clock"}, {"camera", "camera"}, {"lantern", "lantern"}, {"diving helmet", "helmet"}, {"bell", "bell"},
        {"crystal", "crystal"}, {"ore", "crystal"}, {"lens", "crystal"}, {"glassware", "crystal"}, {"sculpture", "crystal"}, {"tooth", "skull_tooth"},
        {"toy", "toy"}, {"remote", "remote"}, {"tv", "tv"}, {"piano", "piano"}, {"grain", "sack"}, {"quilt", "quilt"}, {"art", "frame"},
        {"mirror", "frame"}, {"furniture", "chair"}, {"cake", "cake"}, {"instruments", "instruments"}, {"equipment", "instruments"},
        {"black box", "instruments"}, {"stock", "instruments"}, {"lucky die", "die"}, {"compass", "compass"}, {"exit sign", "exitsign"},
        {"key", "key"}, {"luggage", "luggage"}, {"effects", "luggage"}, {"safe", "safe"}, {"generator", "machine"}, {"machine", "machine"},
        {"mining", "machine"}, {"farm tools", "machine"}, {"prototype", "machine"}, {"robot", "machine"}, {"projector", "machine"},
        {"tack", "machine"}, {"restraints", "machine"}, {"wrench", "toolbox"}, {"heart", "jar"},
    };
    for (const auto& m : MAP) if (has(m.first)) return m.second;
    return "crate";
}
const Model* PropModel(int def) {
    static std::map<int, const Model*> cache; auto it = cache.find(def); if (it != cache.end()) return it->second;
    const Model* m = def >= 0 && def < (int)D().loot.size() ? rt::LoadAsset(std::string("noclip/props/") + PropArchOf(D().loot[def].name) + ".glb") : nullptr;
    cache[def] = m; return m;
}
void DrawPropModel(const Model& m, Matrix world, float emis) {   // a baked prop through the level shader (its own colours)
    if (emis != gEmissive) Emis(emis);
    for (int i = 0; i < m.meshCount; i++) {
        Material mat = m.materials[m.meshMaterial[i]]; MaterialMap maps[12]; memcpy(maps, mat.maps, sizeof(maps)); mat.maps = maps; mat.shader = S3.sh;
        maps[MATERIAL_MAP_DIFFUSE].texture = White(); maps[MATERIAL_MAP_EMISSION].texture = gCurLight;
        DrawMesh(m.meshes[i], mat, MatrixMultiply(m.transform, world));
    }
}
const Model* WorldModel(const char* name) { return rt::LoadAsset(std::string("noclip/world/") + name + ".glb"); }
// a fitting or a creature at a place, turned to a yaw (its front along the yaw), with an extra local transform first
bool DrawWorld(const char* name, Vector3 at, float yaw, float emis = 0, Matrix local = MatrixIdentity()) {
    const Model* m = WorldModel(name); if (!m) return false;
    DrawPropModel(*m, MatrixMultiply(local, Frame(at, yaw)), emis); return true;
}
Color LootColor(int def) { uint32_t h = Hh((uint32_t)def * 2654435761u); return {(unsigned char)(90 + (h & 0x7F)), (unsigned char)(80 + ((h >> 8) & 0x7F)), (unsigned char)(70 + ((h >> 16) & 0x7F)), 255}; }
void DrawLoot(const Loot& l, Vector3 at, float t, float yaw) {
    if (l.def < 0) return; const LootDef& ld = D().loot[l.def]; Color c = LootColor(l.def); float emis = ld.props.find("glowing") != std::string::npos ? 1.4f : 0;
    float bob = ld.anomalous ? sinf(t * 2) * 0.05f + 0.1f : 0;
    if (const Model* pm = PropModel(l.def)) {   // (the real thing; an anomalous one hovers and turns)
        float spin = ld.anomalous ? t * 0.8f : 0;
        DrawPropModel(*pm, MatrixMultiply(MatrixRotateY(-yaw + PI / 2 + spin), MatrixTranslate(at.x, at.y + bob, at.z)), emis + (ld.anomalous ? 0.25f : 0));
        return;
    }
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
                    if (WorldModel("ring")) { DrawWorld("ring", a, PI / 2, glow * 0.12f, MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.5f, 0), MatrixRotateZ(spin)), MatrixTranslate(0, 1.5f, 0))); }
                    else for (int q = 0; q < 16; q++) { float an = q * PI / 8 + spin; Box({a.x + cosf(an) * 1.6f, 1.5f + sinf(an) * 1.4f, a.z}, {0.35f, 0.35f, 0.5f}, q % 2 ? brass : steel, an, glow * 0.3f); }
                    if (glow > 0) Box({a.x, 1.5f, a.z}, {2.4f, 2.4f, 0.05f}, s && s->openT > 0 ? Color{200, 230, 255, 255} : Color{120, 160, 255, 255}, 0, glow);
                    Box({a.x, 0.05f, a.z}, {3.4f, 0.1f, 1.4f}, steel); break;
                }
                case LP_DESK: if (DrawWorld("desk", a, PI / 2, on ? 0.04f : 0)) break; Box({a.x, 0.5f, a.z}, {1.8f, 1.0f, 0.7f}, steel); for (int q = 0; q < 3; q++) Box({a.x - 0.6f + q * 0.6f, 1.25f, a.z}, {0.45f, 0.4f, 0.4f}, Color{50, 50, 54, 255}, 0, on ? 0.2f : 0); break;
                case LP_CRATE: if (!DrawWorld("bin", a, PI / 2)) { Box({a.x, 0.6f, a.z}, {1.3f, 1.2f, 1.0f}, Color{90, 110, 80, 255}); Box({a.x, 0.2f, a.z + 0.9f}, {1.2f, 0.12f, 0.8f}, Color{40, 40, 44, 255}); } if (s) for (int q = 0; q < std::min(6, (int)s->crate.size()); q++) Box({a.x - 0.4f + (q % 3) * 0.4f, 1.3f + (q / 3) * 0.25f, a.z}, {0.3f, 0.2f, 0.3f}, LootColor(s->crate[q].def)); break;
                case LP_SHOP: if (DrawWorld("vending", a, -PI / 2, on ? 0.12f : 0)) break; Box({a.x, 0.95f, a.z}, {0.9f, 1.9f, 0.8f}, Color{150, 40, 40, 255}); Box({a.x, 1.4f, a.z - 0.41f}, {0.6f, 0.4f, 0.02f}, Color{200, 230, 255, 255}, 0, on ? 0.8f : 0); break;
                case LP_BUNK: if (DrawWorld("bunk", a, PI / 2)) break; Box({a.x, 0.4f, a.z}, {0.9f, 0.3f, 2.0f}, Color{90, 90, 70, 255}); Box({a.x, 1.3f, a.z}, {0.9f, 0.1f, 2.0f}, Color{90, 90, 70, 255}); break;
                case LP_GEN: if (DrawWorld("generator", a, PI / 2, on ? 0.04f + 0.03f * sinf(t * 30) : 0, on ? MatrixTranslate(0.004f * sinf(t * 70), 0, 0) : MatrixIdentity())) break; Box({a.x, 0.7f, a.z}, {1.3f, 1.4f, 1.0f}, Color{170, 120, 40, 255}, 0, on ? 0.1f + 0.05f * sinf(t * 30) : 0); break;
                case LP_MONITORS: if (DrawWorld("monitors", a, PI / 2, on ? 0.35f : 0)) break; for (int q = 0; q < 6; q++) Box({a.x, 0.9f + (q / 3) * 0.55f, a.z - 0.6f + (q % 3) * 0.6f}, {0.3f, 0.45f, 0.55f}, Color{40, 44, 40, 255}, 0, on ? 0.6f : 0); break;
                case LP_ARCHIVE: if (DrawWorld("archive", a, PI / 2)) break; for (int q = 0; q < 3; q++) Box({a.x, 0.7f, a.z - 0.6f + q * 0.6f}, {0.6f, 1.4f, 0.5f}, Color{110, 110, 100, 255}); break;
            }
        }
        // the blast door, shut or open
        Vector3 d = L.Center(lp.doorX, lp.doorZ); bool open = s && s->doorOpen;
        if (!DrawWorld("blastdoor", {d.x + (open ? 1.0f : 0), 0, d.z}, PI / 2)) { Box({d.x + (open ? 1.0f : 0), 1.3f, d.z}, {CELL * 0.95f, 2.6f, 0.3f}, Color{90, 92, 96, 255}); Box({d.x + (open ? 1.0f : 0), 2.0f, d.z}, {1.2f, 0.2f, 0.32f}, Color{240, 200, 40, 255}); }
        Vector3 br = L.Center(lp.breakerX, lp.breakerZ); if (!DrawWorld("breaker", {br.x, 0, br.z}, PI / 2)) Box({br.x, 1.2f, br.z}, {0.4f, 0.6f, 0.2f}, Color{60, 66, 60, 255}); Box({br.x, 1.25f, br.z - 0.12f}, {0.08f, 0.2f, 0.08f}, open ? Color{60, 200, 80, 255} : Color{220, 50, 40, 255}, 0, 0.8f);
    }
    // the exits: doors in frames, stairwells, ladders; noclip spots are just a little wrong
    for (const auto& e : L.exits) {
        Vector3 c = L.Center(e.cx, e.cz);
        if (!e.noclip) { if (!DrawWorld("exitdoor", c, PI / 2)) Box({c.x, 1.1f, c.z}, {1.0f, 2.2f, 0.2f}, Color{140, 100, 60, 255}); Box({c.x, 2.45f, c.z - 0.11f}, {0.42f, 0.12f, 0.02f}, Color{40, 200, 80, 255}, 0, 1.4f); }
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
    else if (id == "survivor") { k.body = {200, 140, 40, 255}; k.head = {210, 180, 150, 255}; k.legs = {60, 60, 70, 255}; k.hat = true; k.hatC = {240, 200, 40, 255}; }
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
    float spd = Vector2Length({e.v.x, e.v.z});
    if ((id == "skinstealer" || id == "crew") && e.mimicOf >= 0 && e.mimicOf < (int)w.crew.size()) { SuitLook s; s.height = 1.04f; s.walkMul = 0.85f; if (DrawSuit(s, e.p, e.yaw, t, std::clamp(spd / 3, 0.0f, 1.0f), ph * 0.9f)) return; }   // (a friend's suit, a little too tall, walking a little wrong)
    if (id == "smiler" && DrawWorld("smiler", e.p, e.yaw, 2.6f)) return;
    if (id == "clump" && WorldModel("clump")) { bool down = e.st == ES_ATTACK; DrawWorld("clump", Vector3Add(e.p, {0, down ? 0 : 2.4f, 0}), e.yaw + t * 0.2f, 0, down ? MatrixIdentity() : MatrixRotateX(PI)); return; }   // (it clings to the ceiling and drops)
    if (id == "deathmoth" && WorldModel("moth_body")) {
        Vector3 at = Vector3Add(e.p, {0, 1.8f + sinf(t * 5 + e.uid) * 0.3f, 0}); float fl = sinf(t * 18 + e.uid) * 0.7f;
        DrawWorld("moth_body", at, e.yaw);
        DrawWorld("moth_wing", at, e.yaw, 0, MatrixMultiply(MatrixRotateZ(fl), MatrixTranslate(0.15f, 0.05f, 0)));
        DrawWorld("moth_wing", at, e.yaw, 0, MatrixMultiply(MatrixRotateZ(PI - fl), MatrixTranslate(-0.15f, 0.05f, 0)));
        return;
    }
    if (id == "spider" && DrawWorld("spider", Vector3Add(e.p, {0, sinf(ph * 2) * 0.03f, 0}), e.yaw, 0, MatrixScale(1.2f, 1.2f, 1.2f))) return;
    if (id == "sentry" && DrawWorld("sentry", e.p, e.yaw, e.stunT > 0 ? 0 : 0.15f)) return;
    if (id == "seer" && WorldModel("seer")) { bool open = fmodf(t + e.uid, 7) > 1.5f; DrawWorld("seer", Vector3Add(e.p, {0, 1.5f, 0}), e.yaw, 0.1f, MatrixScale(1, open ? 1.0f : 0.15f, 1)); return; }
    if (id == "leviathan" && WorldModel("leviathan")) { if (fmodf(t, 40) < 6) { float k = fmodf(t, 40) / 6; DrawWorld("leviathan", {e.p.x + 30, -3 - 2 * sinf(k * PI), e.p.z}, 0); } return; }
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
    { SuitLook s = SuitOf(id); if (s.model >= 0) { Vector3 head{}; bool pole = id == "scarecrow"; if (pole) Box({e.p.x, 1.5f, e.p.z - 0.15f}, {0.12f, 3.0f, 0.12f}, {100, 80, 50, 255}); if (DrawSuit(s, pole ? Vector3Add(e.p, {0, 0.9f, 0}) : e.p, e.yaw, t, pole ? 0 : std::clamp(spd / 3, 0.0f, 1.0f), ph, MatrixIdentity(), &head)) {
        if (pole) { Matrix f = Frame(Vector3Add(head, {0, 0.16f, 0}), e.yaw); BoxM(f, {0, 0, 0}, {0.32f, 0.12f, 0.32f}, {120, 90, 50, 255}); BoxM(f, {0, -0.05f, 0}, {0.56f, 0.02f, 0.56f}, {120, 90, 50, 255}); }
        if (id == "partygoer") { DrawM(gCyl, MatrixMultiply(MatrixScale(0.14f, 0.26f, 0.14f), MatrixTranslate(head.x, head.y + 0.16f, head.z)), Color{230, 60, 160, 255}); BallM(Frame(Vector3Add(e.p, {0.5f, 2.6f + sinf(t * 1.5f) * 0.1f, 0}), 0), {0, 0, 0}, {0.35f, 0.42f, 0.35f}, {240, 60, 90, 255}); Box({e.p.x + 0.5f, 1.9f, e.p.z}, {0.01f, 1.2f, 0.01f}, WHITE); }
        if (id == "warden") { Matrix f = Frame(Vector3Add(head, {0, 0.17f, 0}), e.yaw); BoxM(f, {0, 0, 0}, {0.36f, 0.07f, 0.36f}, Color{30, 30, 40, 255}); BoxM(f, {0, -0.03f, 0.06f}, {0.4f, 0.015f, 0.42f}, Color{30, 30, 40, 255}); }
        if (id == "survivor") { Matrix f = Frame(Vector3Add(head, {0, 0.15f, 0}), e.yaw); BoxM(f, {0, 0, 0}, {0.34f, 0.08f, 0.34f}, Color{240, 200, 40, 255}); }
        return; } } }
    Look k = LookOf(id);
    if (id == "scarecrow") { Box({e.p.x, 1.5f, e.p.z}, {0.12f, 3.0f, 0.12f}, {100, 80, 50, 255}); Figure(Vector3Add(e.p, {0, 1.0f, 0}), e.yaw, k, 0); return; }   // (only without the art on disk)
    Figure(e.p, e.yaw, k, Vector2Length({e.v.x, e.v.z}) > 0.2f ? ph : 0);
    if (id == "partygoer") { BallM(Frame(Vector3Add(e.p, {0.5f, 2.6f + sinf(t * 1.5f) * 0.1f, 0}), 0), {0, 0, 0}, {0.35f, 0.42f, 0.35f}, {240, 60, 90, 255}); Box({e.p.x + 0.5f, 1.9f, e.p.z}, {0.01f, 1.2f, 0.01f}, WHITE); }
    (void)me;
}
std::string CosId(int i) { return i >= 0 && i < (int)D().cosmetics.size() ? D().cosmetics[i].id : std::string(); }
SuitLook PlayerSuit(const Player& p, float t, bool asFaceling) {
    std::string h = CosId(p.hat), c = CosId(p.costume), su = CosId(p.suitCos), ve = CosId(p.vest), la = CosId(p.lamp_c);
    SuitLook s;
    if (asFaceling) s = SuitOf("faceling");
    else {
        if (c == "bureau_orange") s.model = 1;   // (the unique costume: the orange suit and the visor helmet)
        // the coverall's colour (suits), the tape and the hood's rim (vests and stickers)
        Color suitC = su == "suit_navy" ? Color{40, 54, 100, 255} : su == "suit_olive" ? Color{96, 106, 60, 255} : su == "suit_grey" ? Color{140, 140, 136, 255} : su == "wallpaper_suit" ? Color{214, 196, 110, 255} : su == "director_coat" ? Color{26, 26, 30, 255} : su == "suit_hivis" ? Color{250, 200, 30, 255} : su == "carpet_suit" ? Color{150, 120, 80, 255} : su == "concrete_suit" ? Color{150, 150, 146, 255} : su == "glass_suit" ? Color{190, 220, 230, 255} : Color{0, 0, 0, 0};
        if (suitC.a) { s.rc.push_back({"suit", suitC}); s.rc.push_back({"trim", Mx(suitC, BLACK, 0.18f)}); }
        Color tape = ve == "vest_orange" ? Color{255, 120, 30, 255} : ve == "vest_lime" ? Color{170, 240, 40, 255} : ve == "vest_pink" ? Color{255, 120, 200, 255} : ve == "vest_blue" ? Color{60, 120, 255, 255} : ve == "vest_black" ? Color{20, 20, 22, 255} : ve == "wallpaper_vest" ? Color{214, 196, 110, 255} : Color{0, 0, 0, 0};
        if (tape.a) s.rc.push_back({"patch", tape});
        Color rim = h == "st_smiley" ? Color{250, 220, 40, 255} : h == "st_bureau" ? Color{60, 80, 160, 255} : h == "st_hazard" ? Color{250, 140, 20, 255} : h == "st_stars" ? Color{200, 200, 230, 255} : Color{0, 0, 0, 0};
        if (rim.a) s.rc.push_back({"trim", rim});
        Color lens = la == "lamp_warm" ? Color{120, 90, 40, 255} : la == "lamp_cold" ? Color{60, 90, 130, 255} : la == "lamp_green" ? Color{40, 120, 60, 255} : la == "lamp_red" ? Color{140, 30, 30, 255} : la == "lamp_purple" ? Color{90, 40, 130, 255} : la == "lamp_uv" ? Color{70, 30, 160, 255} : la == "lamp_rainbow" ? ColorFromHSV(fmodf(t * 60, 360), 0.8f, 0.7f) : Color{0, 0, 0, 0};
        if (lens.a) s.rc.push_back({"lens", lens});
    }
    return s;
}
void DrawCrewMember(const World& w, const Player& p, float t, bool asFaceling) {
    if (p.st == PS_SURFACE || p.st == PS_TAKEN) return;
    if (p.st == PS_DEAD) return;   // (Wanderers are invisible to the living)
    std::string h = CosId(p.hat), c = CosId(p.costume);
    SuitLook s = PlayerSuit(p, t, asFaceling);
    Vector3 at = p.p; float spd = Vector2Length({p.vel.x, p.vel.z});
    Matrix pre = MatrixIdentity();
    if (p.st == PS_DOWNED) { pre = MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(0, 0.18f, 0)); spd = 0; }   // (on their back)
    Vector3 head{};
    if (!DrawSuit(s, at, p.yaw, t + p.id * 1.7f, std::clamp(spd / 3.0f, 0.0f, 1.0f), t * 8 + p.id, pre, &head)) {   // (no art on disk: the old boxes)
        Look k; k.body = {200, 160, 30, 255}; k.head = {30, 30, 30, 255}; k.legs = k.body; k.lamp = p.lamp && p.battery > 0;
        Figure(at, p.yaw, k, spd > 0.3f ? t * 8 + p.id : 0); return;
    }
    if (p.st == PS_DOWNED) return;
    // the headlamp on the hood's brow, toppers and costumes over the figure
    Vector3 fw{cosf(p.yaw), 0, sinf(p.yaw)};
    if (p.lamp && p.battery > 0) BallM(Frame(Vector3Add(head, Vector3Add(Vector3Scale(fw, 0.13f), {0, 0.12f, 0})), p.yaw), {0, 0, 0}, {0.05f, 0.05f, 0.035f}, Color{255, 250, 220, 255}, 2.0f);
    if (asFaceling) return;
    Matrix f = Frame(at, p.yaw); float hy = head.y - at.y + 0.08f, H = 1.8f;
    Matrix hf = Frame(Vector3Add(head, {0, 0.08f, 0}), p.yaw);
    if (h == "party_hat") BallM(hf, {0, 0.18f, 0}, {0.11f, 0.2f, 0.11f}, {240, 60, 160, 255});
    if (h == "pipe_hat") DrawM(gCyl, MatrixMultiply(MatrixScale(0.12f, 0.28f, 0.12f), MatrixTranslate(head.x, head.y + 0.14f, head.z)), {120, 124, 130, 255});
    if (h == "scarecrow_hat") { BoxM(hf, {0, 0.12f, 0}, {0.32f, 0.12f, 0.32f}, {120, 90, 50, 255}); BoxM(hf, {0, 0.07f, 0}, {0.52f, 0.02f, 0.52f}, {120, 90, 50, 255}); }
    if (h == "faceling_mask") BallM(hf, {0, -0.08f, 0.15f}, {0.13f, 0.16f, 0.05f}, {235, 230, 225, 255});
    if (h == "smiler_grin") BoxM(hf, {0, -0.12f, 0.17f}, {0.18f, 0.04f, 0.02f}, {255, 255, 255, 255}, 2.0f);
    if (h == "lava_lamp") BallM(hf, {0, 0.2f, 0}, {0.08f, 0.16f + 0.03f * sinf(t * 2), 0.08f}, {255, 90, 40, 255}, 2.5f);
    if (h == "exit_hat") BoxM(hf, {0, 0.16f, 0.06f}, {0.2f, 0.07f, 0.02f}, {40, 255, 90, 255}, 3.0f);
    if (c == "moth_wings") { float fl = 0.25f * sinf(t * 6); BoxM(f, {-0.35f, H * 0.62f, -0.18f}, {0.5f, 0.6f + fl, 0.02f}, {200, 190, 160, 255}); BoxM(f, {0.35f, H * 0.62f, -0.18f}, {0.5f, 0.6f - fl, 0.02f}, {200, 190, 160, 255}); }
    if (c == "balloon") { BallM(Frame(Vector3Add(at, {0.4f, H + 0.6f + sinf(t * 1.5f + p.id) * 0.08f, 0}), 0), {0, 0, 0}, {0.3f, 0.36f, 0.3f}, {90, 200, 255, 255}); Box({at.x + 0.4f, H * 0.5f + 0.6f, at.z}, {0.01f, H, 0.01f}, WHITE); }
    if (c == "hound_costume") { BoxM(hf, {-0.1f, 0.1f, 0}, {0.06f, 0.16f, 0.1f}, {60, 50, 40, 255}); BoxM(hf, {0.1f, 0.1f, 0}, {0.06f, 0.16f, 0.1f}, {60, 50, 40, 255}); BoxM(f, {0, H * 0.45f, -0.3f}, {0.06f, 0.06f, 0.35f}, {60, 50, 40, 255}); }
    if (c == "almond_costume") BallM(f, {0, H * 0.55f, 0}, {0.42f, 0.6f, 0.38f}, {220, 200, 160, 255});
    (void)hy;
    if (p.hands.def >= 0) DrawLoot(p.hands, Vector3Add(at, {cosf(p.yaw) * 0.5f, 0.9f, sinf(p.yaw) * 0.5f}), t, p.yaw);
    (void)w;
}
// the first-person arms (tools/artgen/noclip_crew.py fp_<suit>.glb): the suit's sleeves and rubber gloves in the
// bottom corners, swinging with the walk; a carried thing is held between both fists, the flashlight in the right.
void DrawHands(const World& w, const View& v) {
    if (v.me < 0 || v.me >= (int)w.crew.size() || v.ghost) return;
    const Player& p = w.crew[v.me]; if (p.st != PS_ALIVE) return;
    SuitLook s = PlayerSuit(p, v.t, false);
    const Model* m = rt::LoadAsset(s.model == 1 ? "noclip/fp_orange.glb" : "noclip/fp_hazmat.glb"); if (!m || !S3.ok) return;
    static std::map<const Model*, std::map<std::string, int>> byName;
    if (!byName.count(m)) { byName[m]; for (const char* n : {"suit", "trim", "glove", "patch"}) { Material mt; if (rt::AssetMaterial(m, n, &mt)) for (int i = 0; i < m->materialCount; i++) if (m->materials[i].maps == mt.maps) byName[m][n] = i; } }
    Vector3 f = Vector3Normalize(Vector3Subtract(v.cam.target, v.cam.position)), r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
    Vector3 o = v.cam.position;
    Matrix B = {-r.x, u.x, f.x, o.x, -r.y, u.y, f.y, o.y, -r.z, u.z, f.z, o.z, 0, 0, 0, 1};   // (x to the left, y up, z ahead: a proper rotation)
    float spd = std::clamp(Vector2Length({p.vel.x, p.vel.z}) / 3.0f, 0.0f, 1.0f), ph = v.t * 8 + p.id;
    bool carry = p.hands.def >= 0, torch = p.tools[std::clamp(p.sel, 0, 4)].item == ItemIndex("flashlight");
    Matrix mirror = MatrixScale(-1, 1, 1);
    for (int side = 0; side < 2; side++) {   // 0 the right arm, 1 the left (the right one mirrored)
        float sw = sinf(ph + side * PI) * spd, breathe = sinf(v.t * 1.6f) * 0.004f;
        float ex = carry ? 0.16f : 0.2f, yaw = carry ? 0.4f : 0.22f, pitch = carry ? 0.36f : 0.3f, ey = -0.25f;
        if (!carry && side == 1 && !torch) { ex = 0.23f; pitch = 0.2f; ey = -0.28f; }   // (the free left hand rides lower)
        Matrix arm = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.82f, 0.82f, 0.82f), MatrixRotateX(-pitch - sw * 0.06f)), MatrixRotateY(yaw)), MatrixTranslate(-ex, ey + breathe + fabsf(sw) * 0.012f, 0.1f + sw * 0.03f));
        Matrix local = side == 0 ? arm : MatrixMultiply(MatrixMultiply(mirror, arm), mirror);
        Matrix world = MatrixMultiply(local, B);
        if (s.emis != gEmissive) Emis(s.emis);
        for (int i = 0; i < m->meshCount; i++) {
            Material mat = m->materials[m->meshMaterial[i]]; MaterialMap maps[12]; memcpy(maps, mat.maps, sizeof(maps)); mat.maps = maps; mat.shader = S3.sh;
            for (const auto& rc : s.rc) { auto it = byName[m].find(rc.mat); if (it != byName[m].end() && it->second == m->meshMaterial[i]) maps[MATERIAL_MAP_DIFFUSE].color = rc.c; }
            maps[MATERIAL_MAP_DIFFUSE].texture = White(); maps[MATERIAL_MAP_EMISSION].texture = gCurLight;
            DrawMesh(m->meshes[i], mat, MatrixMultiply(m->transform, world));
        }
        if (side == 0 && torch && !carry) {   // the flashlight in the right fist, its lens lit while the beam is on
            DrawM(gCyl, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.026f, 0.2f, 0.026f), MatrixRotateX(PI / 2)), MatrixTranslate(0, 0.0f, 0.3f)), world), Color{30, 30, 34, 255});
            DrawM(gCyl, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.036f, 0.04f, 0.036f), MatrixRotateX(PI / 2)), MatrixTranslate(0, 0.0f, 0.49f)), world), Color{60, 60, 66, 255});
            BallM(world, {0, 0, 0.53f}, {0.06f, 0.06f, 0.012f}, v.flashlight ? Color{255, 250, 225, 255} : Color{120, 120, 110, 255}, v.flashlight ? 3.0f : 0);
        }
    }
    if (carry) {   // the carried thing, fitted to the space between the fists
        if (const Model* pm = PropModel(p.hands.def)) {
            BoundingBox bb = GetModelBoundingBox(*pm); Vector3 c = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f), e = Vector3Subtract(bb.max, bb.min);
            float k = 0.26f / std::max(0.05f, std::max(e.x, std::max(e.y, e.z)));
            DrawPropModel(*pm, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-c.x, -c.y, -c.z), MatrixScale(k, k, k)), MatrixTranslate(0, -0.2f, 0.42f)), B), 0);
        }
    }
    Emis(0);
}
// a loot item's picture for the pack: its model in a small target, lit from the upper left (a tiny shader of its own)
Texture2D LootIconImpl(int def) {
    static std::map<int, RenderTexture2D> icons; auto it = icons.find(def); if (it != icons.end()) return it->second.texture;
    static Shader ish{}; static int lLight = -1;
    if (!ish.id) {
        ish = LoadShaderFromMemory("#version 330\nin vec3 vertexPosition; in vec3 vertexNormal; uniform mat4 mvp; uniform mat4 matModel; out vec3 n;\nvoid main(){ n = normalize(mat3(matModel) * vertexNormal); gl_Position = mvp * vec4(vertexPosition, 1.0); }\n",
                                   "#version 330\nin vec3 n; uniform vec4 colDiffuse; uniform vec3 uL; out vec4 finalColor;\nvoid main(){ float d = max(dot(normalize(n), normalize(uL)), 0.0); finalColor = vec4(colDiffuse.rgb * (0.35 + 0.75 * d), 1.0); }\n");
        lLight = GetShaderLocation(ish, "uL"); ish.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(ish, "matModel");
    }
    RenderTexture2D rt = LoadRenderTexture(96, 96);
    const Model* m = PropModel(def);
    BeginLayer(rt); ClearBackground(BLANK);
    if (m) {
        BoundingBox bb = GetModelBoundingBox(*m); Vector3 c = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f); float r = std::max(0.05f, Vector3Distance(bb.min, bb.max) * 0.5f);
        Camera3D cam{}; cam.target = c; cam.position = Vector3Add(c, Vector3Scale(Vector3Normalize({0.9f, 0.7f, 1.4f}), r * 2.6f)); cam.up = {0, 1, 0}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE;
        Vector3 Ld{-0.4f, 1.0f, 0.7f}; SetShaderValue(ish, lLight, &Ld, SHADER_UNIFORM_VEC3);
        BeginMode3D(cam);
        for (int i = 0; i < m->meshCount; i++) { Material mat = m->materials[m->meshMaterial[i]]; mat.shader = ish; DrawMesh(m->meshes[i], mat, m->transform); }
        EndMode3D();
    }
    EndLayer();
    icons[def] = rt; return rt.texture;
}
}  // namespace
Texture2D LootIcon(int def) { return LootIconImpl(def); }

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
    Texture2D lt = G ? G->lightTex : dark; gCurLight = lt;
    for (Model* m : {&gCube, &gSphere, &gCyl}) if (m->meshCount) m->materials[0].maps[MATERIAL_MAP_EMISSION].texture = lt;
    if (G) for (Model* m : {&G->walls, &G->floors, &G->ceils, &G->low, &G->glass, &G->water, &G->lab, &G->wheat}) if (m->meshCount) m->materials[0].maps[MATERIAL_MAP_EMISSION].texture = lt;
    Emis(0);
}
static void VhsBlit(const View& v) {
    RenderTexture2D& rt3d = Mode3DRT();
    float t = v.t; float noise = std::clamp(v.noise, 0.0f, 1.0f); Vector2 res{(float)SCREEN_W, (float)SCREEN_H}; float black = std::clamp(v.blackout, 0.0f, 1.0f);
    { float tape = v.tape; SetShaderValue(gVhs, gVhsTape, &tape, SHADER_UNIFORM_FLOAT); } SetShaderValue(gVhs, gVhsTime, &t, SHADER_UNIFORM_FLOAT); SetShaderValue(gVhs, gVhsNoise, &noise, SHADER_UNIFORM_FLOAT); SetShaderValue(gVhs, gVhsRes, &res, SHADER_UNIFORM_VEC2); SetShaderValue(gVhs, gVhsBlack, &black, SHADER_UNIFORM_FLOAT);
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
    // seals (a Level 2 hatch, a Level 12 doorway grown shut), valves and breakers, Level 17's flood
    for (const auto& s : w.seals) if (s.level == v.level) { Vector3 c{(s.cx + 0.5f) * CELL, 0, (s.cz + 0.5f) * CELL}; if (s.kind == 0) { Box(Vector3Add(c, {0, 1.3f, 0}), {CELL, 2.6f, CELL}, Color{90, 80, 60, 255}); Box(Vector3Add(c, {0, 1.3f, 0}), {CELL * 0.5f, 0.12f, CELL * 1.02f}, Color{150, 120, 60, 255}); } else { Box(Vector3Add(c, {0, 1.3f, 0}), {CELL * 0.98f, 2.6f, CELL * 0.98f}, Color{170, 90, 90, 255}); BallM(Frame(Vector3Add(c, {0, 1.5f, 0}), 0), {0, 0, 0}, {0.3f, 0.12f + 0.06f * sinf(v.t * 1.3f + s.cx), CELL * 0.52f}, Color{120, 40, 50, 255}); } }
    for (const auto& lv2 : w.levers) if (lv2.level == v.level && Vector3Distance(lv2.at, v.cam.position) < 40) { if (lv2.kind == 0) { Box(Vector3Add(lv2.at, {0, 0.5f, 0}), {0.12f, 1.0f, 0.12f}, Color{120, 100, 70, 255}); Matrix f = Frame(Vector3Add(lv2.at, {0, 1.05f, 0}), v.t * 0.2f); for (int k = 0; k < 4; k++) BoxM(MatrixMultiply(MatrixRotateY(k * PI / 4), f), {0, 0, 0}, {0.6f, 0.05f, 0.05f}, Color{200, 60, 40, 255}); }
        else { Box(Vector3Add(lv2.at, {0, 0.9f, 0}), {0.6f, 1.2f, 0.3f}, Color{70, 74, 80, 255}); Box(Vector3Add(lv2.at, {0, w.power ? 1.2f : 0.7f, 0.2f}), {0.08f, 0.3f, 0.08f}, Color{220, 200, 60, 255}); Box(Vector3Add(lv2.at, {0.2f, 1.4f, 0.16f}), {0.06f, 0.06f, 0.02f}, w.power ? Color{60, 255, 80, 255} : Color{255, 50, 40, 255}, 3.0f); } }
    if (v.level == 17) { auto it = w.levels.find(17); if (it != w.levels.end()) { const Level& lv = it->second; int row = w.FloodRow(lv); if (row < lv.h) { float z0 = row * CELL, z1 = lv.h * CELL; Box({lv.w * CELL * 0.5f, 0.35f + 0.05f * sinf(v.t), (z0 + z1) * 0.5f}, {lv.w * CELL, 0.02f, z1 - z0}, Color{50, 80, 90, 200}); } } }
    for (const auto& m : w.marks) if (m.level == v.level && Vector3Distance(m.at, v.cam.position) < 30) { Matrix f = Frame(Vector3Add(m.at, {0, 1.4f, 0}), m.yaw); BoxM(f, {0, 0, 0.05f}, {0.5f, 0.06f, 0.02f}, Color{240, 240, 235, 255}, 0.4f); BoxM(f, {0.2f, 0.08f, 0.05f}, {0.2f, 0.06f, 0.02f}, Color{240, 240, 235, 255}, 0.4f); }
    for (const auto& e : v.fakes) DrawEntity(w, e, v.t, v.me);
    for (const auto& p : w.crew) if (p.level == v.level && p.id != v.me) DrawCrewMember(w, p, v.t, v.teammatesAsFacelings);
    if (v.hands) DrawHands(w, v);
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
