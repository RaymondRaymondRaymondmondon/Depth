// Red Tide's 3D core: mesh building, the CreatureBuilder (every body plan in RedTide_Art_BodyPlans.xlsx) and the
// inked low-poly renderer. See redtide_render.h.
#include "redtide_render.h"
#include "game.h"
#include "input.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace rt {

// ================================================================ mesh building
void MeshBuilder::Tri(Vector3 a, Vector3 b, Vector3 c, Color ca, Color cb, Color cc, Vector2 ta, Vector2 tb, Vector2 tc) {
    const Vector3 P[3] = {a, b, c};
    const Color C[3] = {ca, cb, cc};
    const Vector2 T[3] = {ta, tb, tc};
    for (int i = 0; i < 3; i++) {
        pos.push_back(P[i].x); pos.push_back(P[i].y); pos.push_back(P[i].z);
        uv.push_back(T[i].x); uv.push_back(T[i].y);
        col.push_back(C[i].r); col.push_back(C[i].g); col.push_back(C[i].b); col.push_back(C[i].a);
    }
}
void MeshBuilder::Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color cl, Vector2 t) {
    Tri(a, b, c, cl, t);
    Tri(a, c, d, cl, t);
}
void MeshBuilder::Lathe(float len, int rings, int segs, const std::function<float(float)>& rx, const std::function<float(float)>& ry,
                        Color top, Color belly, Vector3 off, float vCode, float u0, float u1) {
    auto P = [&](int r, int s) -> Vector3 {
        float u = (float)r / rings;
        float a = (float)s / segs * 2 * PI;
        float z = len * 0.5f - u * len;
        return {off.x + cosf(a) * rx(u), off.y + sinf(a) * ry(u), off.z + z};
    };
    auto C = [&](int s) -> Color {
        float a = (float)s / segs * 2 * PI;
        float up = sinf(a);                       // +1 top, -1 belly
        float k = std::clamp(0.5f + up * 0.9f, 0.0f, 1.0f);
        return ColorLerp(belly, top, k);
    };
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            Vector3 a = P(r, s), b = P(r + 1, s), c = P(r + 1, s + 1), d = P(r, s + 1);
            float ua = u0 + (u1 - u0) * (float)r / rings, ub = u0 + (u1 - u0) * (float)(r + 1) / rings;
            Color cs = C(s), cs1 = C(s + 1);
            // winding so faces point outward (both windings are drawn anyway: culling is off for creatures)
            Tri(a, b, c, cs, cs, cs1, {ua, vCode}, {ub, vCode}, {ub, vCode});
            Tri(a, c, d, cs, cs1, cs1, {ua, vCode}, {ub, vCode}, {ua, vCode});
        }
}
void MeshBuilder::Tube(const std::vector<Vector3>& pts, float r0, float r1, int segs, Color c0, Color c1, float vCode, float u0, float u1) {
    if (pts.size() < 2) return;
    int n = (int)pts.size();
    std::vector<Vector3> ringPrev;
    for (int i = 0; i < n; i++) {
        Vector3 dir = i < n - 1 ? Vector3Subtract(pts[i + 1], pts[i]) : Vector3Subtract(pts[i], pts[i - 1]);
        dir = Vector3Normalize(dir);
        Vector3 up = fabsf(dir.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        Vector3 s1 = Vector3Normalize(Vector3CrossProduct(dir, up)), s2 = Vector3CrossProduct(s1, dir);
        float t = (float)i / (n - 1);
        float r = r0 + (r1 - r0) * t;
        std::vector<Vector3> ring;
        for (int k = 0; k < segs; k++) {
            float a = (float)k / segs * 2 * PI;
            ring.push_back(Vector3Add(pts[i], Vector3Add(Vector3Scale(s1, cosf(a) * r), Vector3Scale(s2, sinf(a) * r))));
        }
        if (i > 0) {
            float ta = u0 + (u1 - u0) * (float)(i - 1) / (n - 1), tb = u0 + (u1 - u0) * t;
            Color ca = ColorLerp(c0, c1, (float)(i - 1) / (n - 1)), cb = ColorLerp(c0, c1, t);
            for (int k = 0; k < segs; k++) {
                int k1 = (k + 1) % segs;
                Tri(ringPrev[k], ring[k], ring[k1], ca, cb, cb, {ta, vCode}, {tb, vCode}, {tb, vCode});
                Tri(ringPrev[k], ring[k1], ringPrev[k1], ca, cb, ca, {ta, vCode}, {tb, vCode}, {ta, vCode});
            }
        }
        ringPrev = ring;
    }
}
void MeshBuilder::Octa(Vector3 c, float r, Color cl, float vCode) {
    Vector3 px{c.x + r, c.y, c.z}, nx{c.x - r, c.y, c.z}, py{c.x, c.y + r, c.z}, ny{c.x, c.y - r, c.z}, pz{c.x, c.y, c.z + r}, nz{c.x, c.y, c.z - r};
    Vector2 t{0, vCode};
    Tri(px, py, pz, cl, t); Tri(pz, py, nx, cl, t); Tri(nx, py, nz, cl, t); Tri(nz, py, px, cl, t);
    Tri(px, pz, ny, cl, t); Tri(pz, nx, ny, cl, t); Tri(nx, nz, ny, cl, t); Tri(nz, px, ny, cl, t);
}
void MeshBuilder::Box(Vector3 c, Vector3 h, Color cl, float vCode) {
    Vector3 v[8];
    for (int i = 0; i < 8; i++) v[i] = {c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z)};
    Vector2 t{0, vCode};
    Quad(v[0], v[2], v[3], v[1], cl, t); Quad(v[4], v[5], v[7], v[6], cl, t);
    Quad(v[0], v[1], v[5], v[4], cl, t); Quad(v[2], v[6], v[7], v[3], cl, t);
    Quad(v[0], v[4], v[6], v[2], cl, t); Quad(v[1], v[3], v[7], v[5], cl, t);
}
void MeshBuilder::Cone(Vector3 base, Vector3 tip, float r, int segs, Color cl, float vCode) {
    Vector3 dir = Vector3Normalize(Vector3Subtract(tip, base));
    Vector3 up = fabsf(dir.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    Vector3 s1 = Vector3Normalize(Vector3CrossProduct(dir, up)), s2 = Vector3CrossProduct(s1, dir);
    for (int k = 0; k < segs; k++) {
        float a0 = (float)k / segs * 2 * PI, a1 = (float)(k + 1) / segs * 2 * PI;
        Vector3 p0 = Vector3Add(base, Vector3Add(Vector3Scale(s1, cosf(a0) * r), Vector3Scale(s2, sinf(a0) * r)));
        Vector3 p1 = Vector3Add(base, Vector3Add(Vector3Scale(s1, cosf(a1) * r), Vector3Scale(s2, sinf(a1) * r)));
        Tri(p0, p1, tip, cl, {1, vCode});
        Tri(p1, p0, base, cl, {0, vCode});
    }
}
Mesh MeshBuilder::Build() const {
    Mesh m{};
    m.vertexCount = (int)(pos.size() / 3);
    m.triangleCount = m.vertexCount / 3;
    m.vertices = (float*)MemAlloc(sizeof(float) * pos.size());
    memcpy(m.vertices, pos.data(), sizeof(float) * pos.size());
    m.texcoords = (float*)MemAlloc(sizeof(float) * uv.size());
    memcpy(m.texcoords, uv.data(), sizeof(float) * uv.size());
    m.colors = (unsigned char*)MemAlloc(col.size());
    memcpy(m.colors, col.data(), col.size());
    // normals are computed per pixel from screen-space derivatives (flat shading that survives the swim shader);
    // the attribute is still filled so raylib's default shaders stay happy
    m.normals = (float*)MemAlloc(sizeof(float) * pos.size());
    for (int t = 0; t < m.triangleCount; t++) {
        Vector3 a{pos[t * 9], pos[t * 9 + 1], pos[t * 9 + 2]}, b{pos[t * 9 + 3], pos[t * 9 + 4], pos[t * 9 + 5]}, c{pos[t * 9 + 6], pos[t * 9 + 7], pos[t * 9 + 8]};
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (int k = 0; k < 3; k++) { m.normals[t * 9 + k * 3] = n.x; m.normals[t * 9 + k * 3 + 1] = n.y; m.normals[t * 9 + k * 3 + 2] = n.z; }
    }
    UploadMesh(&m, false);
    return m;
}

// ================================================================ the CreatureBuilder
static Json gTones;
static Color Tone(const std::string& name, Color def = {120, 120, 120, 255}) {
    if (gTones.IsNull()) gTones = LoadJsonFile(DataDir() + "/art/tones.json");
    const Json& t = gTones[name];
    if (!t.IsArr()) {
        // "warning brass gold" style names fall back to the accent they brighten
        if (name.rfind("warning ", 0) == 0) { Color c = Tone(name.substr(8), def); return {(unsigned char)std::min(255, c.r + 30), (unsigned char)std::min(255, c.g + 20), (unsigned char)std::min(255, c.b + 10), 255}; }
        return def;
    }
    return {(unsigned char)t[0].I(), (unsigned char)t[1].I(), (unsigned char)t[2].I(), 255};
}
static std::string Low(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool HasWord(const std::string& hay, const char* w) { return Low(hay).find(w) != std::string::npos; }

struct ArtRow {
    std::string beast, cls, plan, tail, fins, eye, mouth, armor, luminous, chains, idle, distinct;
    int size = 1; float length = 0.3f, girth = 0.25f, head = 0.28f;
    Color base{}, belly{}, accent{};
};

static ArtRow ReadArt(const std::string& mapKey, const std::string& name) {
    static std::map<std::string, Json> sheets;
    auto it = sheets.find(mapKey);
    if (it == sheets.end()) it = sheets.emplace(mapKey, LoadJsonFile(DataDir() + "/art/species_" + mapKey + ".json")).first;
    ArtRow a;
    a.beast = name;
    static Json patch = LoadJsonFile(DataDir() + "/art/art_patch.json");
    const Json& fix = patch[mapKey][name];
    for (const Json& r0 : it->second.a) {
        if (r0["beast"].Str0() != name) continue;
        Json r = r0;
        for (const auto& kv : fix.o) if (kv.first[0] != '_') { bool found = false; for (auto& ok : r.o) if (ok.first == kv.first) { ok.second = kv.second; found = true; } if (!found) r.o.push_back(kv); }
        a.cls = r["class"].Str0(); a.plan = Low(r["body_plan"].Str0()); a.size = r["size"].I(1);
        a.length = r["length_m"].F(0.3f); a.girth = r["girth_ratio"].F(0.25f); a.head = r["head_ratio"].F(0.28f);
        a.tail = r["tail_or_limb"].Str0(); a.fins = r["fins_or_legs"].Str0(); a.eye = r["eye_size"].Str0(); a.mouth = r["mouth"].Str0();
        a.armor = r["armor_plates"].Str0(); a.luminous = r["luminous"].Str0(); a.chains = r["chains"].Str0(); a.idle = r["idle"].Str0();
        a.distinct = r["distinctive_part"].Str0();
        a.base = Tone(r["base_tone"].Str0()); a.belly = Tone(r["belly_tone"].Str0(), a.base); a.accent = Tone(r["accent"].Str0(), {200, 40, 40, 255});
        return a;
    }
    // Not in the art sheet (e.g. the enemy faction): a plain human-sized silhouette
    a.plan = "diver"; a.size = 3; a.length = 1.8f; a.base = {70, 80, 86, 255}; a.belly = {110, 100, 80, 255}; a.accent = {214, 168, 72, 255};
    return a;
}

static float Smooth(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

// Fusiform profile: blunt head, widest at the head/body join, tapering to the tail stalk.
static std::function<float(float)> SpindleProfile(float rad, float headRatio, float stalk = 0.12f) {
    return [=](float u) {
        if (u < headRatio) return rad * (0.25f + 0.75f * sqrtf(std::max(0.0f, u / headRatio)));
        float t = (u - headRatio) / (1 - headRatio);
        return rad * std::max(stalk, 1 - t * t * (1 - stalk) * 1.05f);
    };
}

static void AddEyes(MeshBuilder& mb, float L, float rad, float headRatio, const ArtRow& a, float sideScale = 1) {
    float es = HasWord(a.eye, "huge") ? 0.24f : HasWord(a.eye, "large") ? 0.18f : HasWord(a.eye, "small") ? 0.08f : HasWord(a.eye, "tiny") ? 0.05f : 0.12f;
    float er = std::max(0.012f, rad * es * 1.6f);
    float z = L * 0.5f - L * headRatio * 0.45f;
    Color eye = HasWord(a.eye, "none") || HasWord(a.cls, "worm") ? a.base : a.accent;
    Color pupil{12, 12, 16, 255};
    if (HasWord(a.eye, "none")) return;
    for (int s = -1; s <= 1; s += 2) {
        mb.Octa({s * rad * 0.82f * sideScale, rad * 0.25f, z}, er, eye, 0);
        mb.Octa({s * rad * 0.9f * sideScale, rad * 0.27f, z + er * 0.2f}, er * 0.55f, pupil, 0);
    }
}

// A fin as a flat triangle (two-sided), with its root on the body.
static void Fin(MeshBuilder& mb, Vector3 root0, Vector3 root1, Vector3 tip, Color c, float u) {
    mb.Tri(root0, root1, tip, c, {u, 0.25f});
    mb.Tri(root1, root0, tip, c, {u, 0.25f});
}

static void Tail(MeshBuilder& mb, float L, float rad, const std::string& type, Color c) {
    float z = -L * 0.5f;
    float h = std::max(rad * 1.6f, L * 0.14f);
    std::string t = Low(type);
    Vector3 root0{0, rad * 0.1f, z + L * 0.02f}, root1{0, -rad * 0.1f, z + L * 0.02f};
    if (t.find("fork") != std::string::npos || t.find("lunate") != std::string::npos) {
        float depth = t.find("lunate") != std::string::npos ? 0.55f : 0.8f;
        Fin(mb, root0, root1, {0, h, z - h * depth}, c, 1);
        Fin(mb, root0, root1, {0, -h, z - h * depth}, c, 1);
    } else if (t.find("hetero") != std::string::npos) {
        Fin(mb, root0, root1, {0, h * 1.3f, z - h * 0.9f}, c, 1);   // sharks: the upper lobe longer
        Fin(mb, root0, root1, {0, -h * 0.6f, z - h * 0.5f}, c, 1);
    } else if (t.find("round") != std::string::npos || t.find("fan") != std::string::npos) {
        for (int k = -2; k <= 2; k++) Fin(mb, root0, root1, {0, k * h * 0.35f, z - h * 0.8f}, c, 1);
    } else if (t.find("whip") != std::string::npos) {
        mb.Tube({{0, 0, z}, {0, 0, z - L * 0.5f}}, rad * 0.12f, rad * 0.02f, 4, c, c, 0, 1, 1);
    } else if (t.find("continuous") == std::string::npos && t.find("none") == std::string::npos) {
        Fin(mb, root0, root1, {0, h * 0.5f, z - h}, c, 1);             // pointed
        Fin(mb, root0, root1, {0, -h * 0.5f, z - h}, c, 1);
    }
}

static void BuildFish(MeshBuilder& mb, const ArtRow& a, bool compressed, bool shark) {
    float L = a.length, rad = L * a.girth * 0.5f;
    float ry = compressed ? rad * 1.8f : rad, rx = compressed ? rad * 0.55f : rad;
    auto prof = SpindleProfile(1.0f, a.head, shark ? 0.15f : 0.1f);
    mb.Lathe(L, 10, compressed ? 8 : 7, [&](float u) { return rx * prof(u); }, [&](float u) { return ry * prof(u) * (shark ? 0.85f : 1); }, a.base, a.belly);
    Tail(mb, L, rad, a.tail, ColorLerp(a.base, a.belly, 0.3f));
    // dorsal fin(s)
    int dorsals = HasWord(a.fins, "x2") ? 2 : 1;
    for (int d = 0; d < dorsals; d++) {
        float z = L * (0.15f - d * 0.28f);
        float hgt = compressed ? ry * 1.2f : shark ? ry * 1.4f : ry * 0.9f;
        Fin(mb, {0, ry * 0.8f, z + L * 0.08f}, {0, ry * 0.8f, z - L * 0.12f}, {0, ry * 0.8f + hgt, z - L * (shark ? 0.1f : 0.04f)}, a.base, 0.4f + d * 0.2f);
    }
    // pectorals
    for (int s = -1; s <= 1; s += 2) {
        float z = L * 0.5f - L * a.head * 1.1f;
        float span = shark ? rad * 2.4f : rad * 1.4f;
        Fin(mb, {s * rx * 0.8f, -ry * 0.2f, z}, {s * rx * 0.8f, -ry * 0.25f, z - L * 0.08f}, {s * (rx + span), -ry * (shark ? 0.7f : 0.4f), z - L * 0.12f}, ColorLerp(a.base, a.belly, 0.5f), 0.3f);
    }
    // anal fin
    Fin(mb, {0, -ry * 0.8f, -L * 0.18f}, {0, -ry * 0.7f, -L * 0.3f}, {0, -ry * 1.4f, -L * 0.3f}, a.belly, 0.7f);
    if (shark) for (int s = -1; s <= 1; s += 2) for (int g = 0; g < 5; g++) {
        float z = L * 0.5f - L * a.head * (1.0f + g * 0.08f);
        mb.Box({s * rx * 0.95f, 0, z}, {rad * 0.02f, ry * 0.35f, L * 0.004f}, {20, 20, 24, 255}, 0);   // gill slits as ink lines
    }
    AddEyes(mb, L, std::max(rx, rad * 0.6f), a.head, a, compressed ? 1.2f : 1);
}

static void BuildEel(MeshBuilder& mb, const ArtRow& a, bool snake) {
    float L = a.length, rad = std::max(0.01f, L * a.girth * 0.5f);
    auto prof = [&](float u) { return rad * (u < 0.1f ? 0.6f + 4 * u : 1.0f - 0.8f * Smooth((u - 0.6f) / 0.4f)); };
    mb.Lathe(L, 16, 6, prof, prof, a.base, a.belly);
    if (!snake && HasWord(a.tail, "continuous")) Fin(mb, {0, rad * 0.9f, L * 0.2f}, {0, rad * 0.9f, -L * 0.5f}, {0, rad * 1.6f, -L * 0.3f}, a.base, 0.6f);
    if (snake) Fin(mb, {0, rad, -L * 0.4f}, {0, -rad, -L * 0.4f}, {0, 0, -L * 0.52f}, a.base, 1);
    // banding for snakes
    if (snake) for (int b = 1; b < 9; b++) { float z = L * 0.5f - L * b / 10.0f; mb.Box({0, 0, z}, {rad * 1.05f, rad * 1.05f, L * 0.012f}, a.accent, 0); }
    AddEyes(mb, L, rad, 0.08f, a);
}

static void BuildRay(MeshBuilder& mb, const ArtRow& a) {
    float span = a.length, len = span * 0.8f, th = span * 0.05f;
    // disc as a fan of triangles, wings marked (v = 0.5) for the wave
    int N = 14;
    for (int i = 0; i < N; i++) {
        float a0 = (float)i / N * 2 * PI, a1 = (float)(i + 1) / N * 2 * PI;
        auto rim = [&](float ang) { float r = HasWord(a.tail, "diamond") || HasWord(a.fins, "diamond") ? 1 / (fabsf(cosf(ang)) + fabsf(sinf(ang))) : 1; return Vector3{cosf(ang) * span * 0.5f * r, 0, sinf(ang) * len * 0.5f * r}; };
        Vector3 p0 = rim(a0), p1 = rim(a1);
        Color top = a.base, bot = a.belly;
        mb.Tri({0, th, 0}, p1, p0, top, top, top, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f});
        mb.Tri({0, -th * 0.5f, 0}, p0, p1, bot, bot, bot, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f});
    }
    mb.Tube({{0, 0, -len * 0.45f}, {0, 0, -len * 1.1f}}, span * 0.02f, span * 0.004f, 4, a.base, a.base, 0, 1, 1);
    for (int s = -1; s <= 1; s += 2) mb.Octa({s * span * 0.07f, th * 1.4f, len * 0.22f}, span * 0.025f, a.accent, 0);
}

static void BuildShell(MeshBuilder& mb, const ArtRow& a, bool croc) {
    float L = a.length;
    if (croc) {
        float rad = L * a.girth * 0.35f;
        auto prof = [&](float u) { return rad * (u < 0.28f ? 0.35f + u * 1.5f : 1 - 0.85f * Smooth((u - 0.45f) / 0.55f)); };
        mb.Lathe(L, 14, 6, prof, [&](float u) { return prof(u) * 0.55f; }, a.base, a.belly);
        for (int r = 0; r < 10; r++) for (int s = -1; s <= 1; s += 2) mb.Box({s * rad * 0.35f, rad * 0.55f, L * (0.25f - r * 0.06f)}, {rad * 0.1f, rad * 0.08f, L * 0.02f}, ColorLerp(a.base, {20, 20, 20, 255}, 0.3f), 0);
        for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++) {
            float z = k == 0 ? L * 0.2f : -L * 0.05f;
            mb.Tube({{s * rad * 0.8f, -rad * 0.2f, z}, {s * rad * 1.6f, -rad * 0.7f, z + L * 0.03f}, {s * rad * 1.8f, -rad * 1.1f, z + L * 0.05f}}, rad * 0.18f, rad * 0.1f, 4, a.base, a.belly, 0.5f + (k * 2 + (s > 0)) * 0.05f);
        }
        AddEyes(mb, L, rad * 0.5f, 0.2f, a);
        return;
    }
    // turtle: domed shell, flat body, four flippers, head
    float w = L * 0.4f, dome = L * 0.18f;
    mb.Lathe(L * 0.8f, 6, 10, [&](float u) { return w * sinf(PI * std::clamp(u, 0.02f, 0.98f)); }, [&](float u) { return dome * sinf(PI * std::clamp(u, 0.02f, 0.98f)); }, a.base, a.belly, {0, 0, 0});
    for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++) {
        float z = k == 0 ? L * 0.22f : -L * 0.25f, sp = k == 0 ? L * 0.5f : L * 0.3f;
        Fin(mb, {s * w * 0.6f, 0, z + L * 0.06f}, {s * w * 0.6f, 0, z - L * 0.06f}, {s * (w * 0.6f + sp), -dome * 0.2f, z - L * 0.12f}, a.belly, 0.5f + (k * 2 + (s > 0)) * 0.05f);
    }
    mb.Lathe(L * 0.22f, 4, 6, [&](float u) { return L * 0.07f * (1 - u * 0.4f); }, [&](float u) { return L * 0.06f * (1 - u * 0.4f); }, a.base, a.belly, {0, dome * 0.1f, L * 0.48f});
    for (int s = -1; s <= 1; s += 2) mb.Octa({s * L * 0.06f, dome * 0.2f, L * 0.55f}, L * 0.015f, a.accent, 0);
}

static void BuildCrab(MeshBuilder& mb, const ArtRow& a, bool shrimp) {
    float W = a.length;
    if (shrimp) {
        float L = W, rad = L * a.girth * 0.4f;
        mb.Lathe(L, 8, 6, [&](float u) { return rad * (1 - 0.6f * u); }, [&](float u) { return rad * (1.1f - 0.6f * u); }, a.base, a.belly);
        Tail(mb, L, rad, "fan", a.base);
        for (int s = -1; s <= 1; s += 2) mb.Tube({{s * rad * 0.4f, rad * 0.4f, L * 0.45f}, {s * rad * 2, rad * 2, L * 0.9f}, {s * rad * 3, rad * 1.2f, L * 1.3f}}, rad * 0.06f, rad * 0.02f, 3, a.accent, a.accent, 0.95f, 0, 1);
        for (int k = 0; k < 5; k++) for (int s = -1; s <= 1; s += 2) mb.Tube({{s * rad * 0.5f, -rad * 0.6f, L * (0.3f - k * 0.1f)}, {s * rad * 0.8f, -rad * 1.6f, L * (0.32f - k * 0.1f)}}, rad * 0.08f, rad * 0.04f, 3, a.base, a.base, 0.5f + k * 0.05f);
        AddEyes(mb, L, rad, 0.1f, a);
        return;
    }
    // crab: carapace, eight legs of two segments, two claws, eye stalks
    float cw = W * 0.5f, ch = W * 0.18f, cl = W * 0.36f;
    mb.Lathe(cl * 2, 5, 10, [&](float u) { return cw * sinf(PI * std::clamp(u, 0.03f, 0.97f)); }, [&](float u) { return ch * sinf(PI * std::clamp(u, 0.03f, 0.97f)); }, a.base, a.belly);
    for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 4; k++) {
        float z = cl * (0.4f - k * 0.28f);
        Vector3 hip{s * cw * 0.8f, 0, z}, knee{s * (cw * 1.4f), ch * 1.4f, z + cl * 0.05f}, foot{s * (cw * 1.9f), -ch * 1.6f, z + cl * 0.1f};
        mb.Tube({hip, knee, foot}, W * 0.035f, W * 0.015f, 3, a.base, a.belly, 0.5f + (k * 2 + (s > 0)) * 0.05f);
    }
    float claw = HasWord(a.fins, "claws") || HasWord(a.tail, "claws") ? 1.0f : 0.5f;
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh{s * cw * 0.6f, 0, cl * 0.8f}, el{s * cw * 1.0f, ch * 0.4f, cl * 1.3f};
        mb.Tube({sh, el}, W * 0.04f, W * 0.05f, 4, a.base, a.base, 0.9f);
        mb.Lathe(W * 0.25f * claw, 4, 6, [&](float u) { return W * 0.08f * claw * sinf(PI * std::clamp(u, 0.1f, 0.9f)); }, [&](float u) { return W * 0.05f * claw * sinf(PI * std::clamp(u, 0.1f, 0.9f)); }, a.accent, a.base, Vector3Add(el, {0, 0, W * 0.12f * claw}), 0.9f);
        mb.Tube({{s * cw * 0.2f, ch * 0.6f, cl * 0.9f}, {s * cw * 0.25f, ch * 1.4f, cl * 1.0f}}, W * 0.012f, W * 0.012f, 3, a.base, a.base, 0);
        mb.Octa({s * cw * 0.25f, ch * 1.5f, cl * 1.0f}, W * 0.025f, {12, 12, 16, 255}, 0);
    }
}

static void BuildCephalopod(MeshBuilder& mb, const ArtRow& a) {
    float L = a.length, mant = L * 0.45f, rad = mant * 0.38f;
    bool squid = a.beast.find("Squid") != std::string::npos || a.beast.find("squid") != std::string::npos || a.beast.find("Cuttle") != std::string::npos;
    // mantle points back (tail end), arms trail behind the head
    auto prof = [&](float u) { return rad * (squid ? (u < 0.3f ? 0.7f + u : 1.0f - 0.9f * Smooth((u - 0.4f) / 0.6f)) : sinf(PI * std::clamp(0.1f + u * 0.9f, 0.0f, 1.0f))); };
    mb.Lathe(mant, 8, 8, prof, prof, a.base, a.belly, {0, rad * 0.2f, -L * 0.05f});
    if (squid) for (int s = -1; s <= 1; s += 2) Fin(mb, {s * rad * 0.7f, 0, -L * 0.1f}, {s * rad * 0.3f, 0, -L * 0.28f}, {s * rad * 1.8f, 0, -L * 0.25f}, a.base, 0.25f);
    int arms = 8;
    for (int k = 0; k < arms; k++) {
        float ang = (float)k / arms * 2 * PI;
        Vector3 root{cosf(ang) * rad * 0.5f, sinf(ang) * rad * 0.5f, mant * 0.45f};
        std::vector<Vector3> pts;
        float armLen = L * (squid ? 0.5f : 0.6f);
        for (int j = 0; j <= 6; j++) {
            float t = j / 6.0f;
            pts.push_back(Vector3Add(root, {cosf(ang) * rad * 0.8f * t, sinf(ang) * rad * 0.8f * t, armLen * t}));
        }
        mb.Tube(pts, rad * 0.16f, rad * 0.02f, 4, a.base, a.belly, 0.5f + k * 0.05f);
    }
    for (int s = -1; s <= 1; s += 2) mb.Octa({s * rad * 0.8f, rad * 0.35f, mant * 0.4f}, rad * 0.22f, a.accent, 0);
}

static void BuildJelly(MeshBuilder& mb, const ArtRow& a) {
    float d = a.length, r = d * 0.5f;
    Color bell = ColorAlpha(a.base, 0.75f);
    mb.Lathe(r, 5, 12, [&](float u) { return r * sinf(PI * 0.5f * (0.2f + u * 0.8f)); }, [&](float u) { return r * sinf(PI * 0.5f * (0.2f + u * 0.8f)); }, bell, bell, {0, 0, 0});
    for (int k = 0; k < 10; k++) {
        float ang = (float)k / 10 * 2 * PI;
        std::vector<Vector3> pts;
        for (int j = 0; j <= 6; j++) pts.push_back({cosf(ang) * r * 0.8f, sinf(ang) * r * 0.8f, -r * 0.5f - j * d * 0.3f});
        mb.Tube(pts, d * 0.015f, d * 0.004f, 3, a.accent, a.base, 0.5f + (k % 8) * 0.05f);
    }
}

static void BuildRadial(MeshBuilder& mb, const ArtRow& a) {
    float r = a.length * 0.5f;
    bool star = HasWord(a.beast, "star") || HasWord(a.tail, "arm");
    if (star) {
        for (int k = 0; k < 5; k++) {
            float ang = k * 2 * PI / 5;
            mb.Tube({{0, 0, 0}, {cosf(ang) * r, 0, sinf(ang) * r}}, r * 0.18f, r * 0.04f, 4, a.base, a.belly, 0.5f + k * 0.05f);
        }
        mb.Octa({0, 0, 0}, r * 0.25f, a.base);
        return;
    }
    mb.Lathe(r * 2, 5, 8, [&](float u) { return r * sinf(PI * std::clamp(u, 0.05f, 0.95f)); }, [&](float u) { return r * 0.7f * sinf(PI * std::clamp(u, 0.05f, 0.95f)); }, a.base, a.belly);
    if (HasWord(a.beast, "urchin") || HasWord(a.beast, "crown")) for (int k = 0; k < 14; k++) {
        float th = k * 2.4f, ph = (k % 7) / 7.0f * PI;
        Vector3 d{cosf(th) * sinf(ph), cosf(ph), sinf(th) * sinf(ph)};
        mb.Cone(Vector3Scale(d, r * 0.6f), Vector3Scale(d, r * 1.7f), r * 0.07f, 3, a.accent);
    }
}

static void BuildColony(MeshBuilder& mb, const ArtRow& a) {
    // many small bodies: a loose cloud of tiny spindles
    float spread = std::max(0.6f, a.length * 6), bl = std::max(0.02f, a.length);
    for (int k = 0; k < 24; k++) {
        float h = k * 2.39996f;
        Vector3 c{cosf(h) * spread * (k % 5) / 5.0f, sinf(h * 1.7f) * spread * 0.3f, sinf(h) * spread * (k % 7) / 7.0f};
        mb.Lathe(bl, 2, 4, [&](float u) { return bl * 0.2f * sinf(PI * std::clamp(u, 0.1f, 0.9f)); }, [&](float u) { return bl * 0.2f * sinf(PI * std::clamp(u, 0.1f, 0.9f)); }, a.base, a.belly, c, 0.5f + (k % 8) * 0.05f);
    }
}

static void BuildLimbed(MeshBuilder& mb, const ArtRow& a, int legs, bool wings, bool flippers) {
    // amphibians, insects, spiders, birds, seals: a body, legs (or flippers) and optional wings
    float L = a.length, rad = L * std::max(0.12f, a.girth) * 0.5f;
    auto prof = SpindleProfile(rad, std::max(0.18f, a.head), flippers ? 0.2f : 0.35f);
    mb.Lathe(L, 8, 7, prof, prof, a.base, a.belly);
    for (int k = 0; k < legs / 2; k++) for (int s = -1; s <= 1; s += 2) {
        float z = L * (0.25f - k * (0.5f / std::max(1, legs / 2 - 1)));
        if (flippers) Fin(mb, {s * rad * 0.8f, -rad * 0.2f, z + L * 0.05f}, {s * rad * 0.8f, -rad * 0.2f, z - L * 0.05f}, {s * rad * 2.4f, -rad * 0.8f, z - L * 0.12f}, a.belly, 0.5f + (k * 2 + (s > 0)) * 0.05f);
        else mb.Tube({{s * rad * 0.8f, -rad * 0.2f, z}, {s * rad * 2.0f, rad * 0.6f, z + L * 0.05f}, {s * rad * 2.6f, -rad * 1.4f, z + L * 0.08f}}, rad * 0.14f, rad * 0.06f, 3, a.base, a.belly, 0.5f + (k * 2 + (s > 0)) * 0.05f);
    }
    if (wings) for (int s = -1; s <= 1; s += 2) Fin(mb, {s * rad * 0.7f, rad * 0.3f, L * 0.15f}, {s * rad * 0.7f, rad * 0.3f, -L * 0.1f}, {s * L * 0.9f, rad * 0.6f, -L * 0.05f}, a.base, 0.25f);
    AddEyes(mb, L, rad, std::max(0.18f, a.head), a);
}

static void BuildDiver(MeshBuilder& mb, Color suit, Color trim, Color helmet) {
    // a human in a hard suit, facing +Z (the enemy factions; the four player divers use their own suits)
    mb.Lathe(0.7f, 5, 8, [](float u) { return 0.22f * (1 - 0.2f * u); }, [](float u) { return 0.2f; }, suit, suit, {0, 0.25f, 0});
    mb.Octa({0, 0.62f, 0.02f}, 0.2f, helmet);
    mb.Octa({0, 0.62f, 0.2f}, 0.09f, trim);                   // the porthole
    for (int s = -1; s <= 1; s += 2) {
        mb.Tube({{s * 0.24f, 0.45f, 0.05f}, {s * 0.3f, 0.2f, 0.15f}, {s * 0.26f, 0.05f, 0.25f}}, 0.07f, 0.06f, 4, suit, trim, 0.5f + (s > 0) * 0.05f);
        mb.Tube({{s * 0.1f, -0.1f, 0}, {s * 0.12f, -0.5f, -0.05f}, {s * 0.12f, -0.85f, -0.2f}}, 0.08f, 0.07f, 4, suit, suit, 0.6f + (s > 0) * 0.05f);
        Fin(mb, {s * 0.12f, -0.85f, -0.2f}, {s * 0.12f, -0.9f, -0.15f}, {s * 0.14f, -0.95f, -0.55f}, trim, 1);
    }
    mb.Box({0, 0.35f, -0.22f}, {0.12f, 0.2f, 0.08f}, trim);  // the tank
}

static std::map<std::string, std::unique_ptr<CreatureModel>> gCreatures;
static Shader gLit{}, gND{};
static bool gShadersReady = false;
static void EnsureShaders();

const CreatureModel& Creature(const std::string& mapKey, const std::string& name) {
    std::string key = mapKey + "/" + name;
    auto it = gCreatures.find(key);
    if (it != gCreatures.end()) return *it->second;
    auto cm = std::make_unique<CreatureModel>();
    ArtRow a = ReadArt(mapKey, name);
    cm->species = name;
    cm->plan = a.plan;
    cm->length = a.length;
    cm->base = a.base; cm->belly = a.belly; cm->accent = a.accent;
    cm->luminous = HasWord(a.luminous, "yes");
    MeshBuilder mb;
    std::string plan = a.plan;
    if (plan == "leviathan") {
        // the base plan scaled up, with plates and barnacles (the Goliath: a grouper the size of a boiler)
        plan = HasWord(a.cls, "crust") ? "crab" : HasWord(a.cls, "ceph") ? "cephalopod" : HasWord(a.cls, "mammal") ? "cetacean" : HasWord(a.cls, "worm") ? "anguilliform" : "fusiform";
    }
    if (plan == "fusiform" || plan == "compressiform" || plan == "shark" || plan == "cetacean" || plan == "pinniped") {
        BuildFish(mb, a, plan == "compressiform", plan == "shark");
        if (plan == "pinniped") BuildLimbed(mb, a, 2, false, true);
        cm->anim = plan == "cetacean" || plan == "pinniped" ? AnimMode::CetaceanVertical : AnimMode::FishLateral;
        cm->waves = plan == "shark" ? 0.7f : 1.1f; cm->amp = plan == "shark" ? 0.05f : plan == "compressiform" ? 0.04f : 0.09f; cm->freq = plan == "shark" ? 3 : 7;
    } else if (plan == "anguilliform" || plan == "snake" || plan == "burrower") {
        BuildEel(mb, a, plan == "snake");
        cm->anim = AnimMode::EelFull; cm->waves = 2.0f; cm->amp = 0.12f; cm->freq = 4;
    } else if (plan == "depressiform") {
        BuildRay(mb, a);
        cm->anim = AnimMode::RayWing; cm->waves = 1; cm->amp = 0.18f; cm->freq = 2.5f;
    } else if (plan == "turtle" || plan == "crocodilian" || plan == "amphibian") {
        if (plan == "amphibian") BuildLimbed(mb, a, 4, false, false); else BuildShell(mb, a, plan == "crocodilian");
        cm->anim = plan == "crocodilian" ? AnimMode::FishLateral : AnimMode::Walker; cm->waves = 0.6f; cm->amp = 0.05f; cm->freq = 2;
    } else if (plan == "crab" || plan == "shrimp" || plan == "insect") {
        if (plan == "insect") BuildLimbed(mb, a, HasWord(a.fins, "8") ? 8 : 6, HasWord(a.fins, "wing"), false); else BuildCrab(mb, a, plan == "shrimp");
        cm->anim = AnimMode::Walker; cm->waves = 1; cm->amp = 0.06f; cm->freq = 6;
    } else if (plan == "cephalopod") {
        BuildCephalopod(mb, a);
        cm->anim = AnimMode::Cephalopod; cm->waves = 1.5f; cm->amp = 0.12f; cm->freq = 2;
    } else if (plan == "jelly") {
        BuildJelly(mb, a);
        cm->anim = AnimMode::JellyPulse; cm->freq = 1.5f; cm->amp = 0.12f;
    } else if (plan == "echinoderm") {
        BuildRadial(mb, a);
        cm->anim = AnimMode::Static; cm->amp = 0.02f;
    } else if (plan == "colony") {
        BuildColony(mb, a);
        cm->anim = AnimMode::Walker; cm->amp = 0.1f; cm->freq = 3;
    } else if (plan == "bird") {
        BuildLimbed(mb, a, 2, true, false);
        cm->anim = AnimMode::FishLateral; cm->amp = 0.03f;
    } else {
        BuildDiver(mb, a.base, a.accent, a.belly);
        cm->anim = AnimMode::Walker; cm->amp = 0.05f; cm->freq = 3;
        cm->length = 1.8f;
    }
    cm->radius = std::max(0.05f, cm->length * std::max(0.18f, a.girth) * 0.6f);
    for (size_t i = 0; i < mb.pos.size(); i++) cm->extent = std::max(cm->extent, fabsf(mb.pos[i]));
    cm->weakPoint = {0, 0, cm->length * 0.5f - cm->length * a.head * 0.9f};  // gills and eyes, behind the head
    if (IsWindowReady()) {
        EnsureShaders();
        Mesh mesh = mb.Build();
        cm->model = LoadModelFromMesh(mesh);
        cm->model.materials[0].shader = gLit;
        cm->ready = true;
    }
    auto* raw = cm.get();
    gCreatures[key] = std::move(cm);
    return *raw;
}

void UnloadCreatures() {
    for (auto& kv : gCreatures) if (kv.second->ready) UnloadModel(kv.second->model);
    gCreatures.clear();
}

// ================================================================ shaders
// The vertex shader animates by the body coordinates in texcoord (u along the body, v the part code) and passes
// the world position and view-space depth to the fragment shaders; both passes share it.
static const char* RT_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matView;
uniform int uAnim; uniform float uPhase; uniform float uAmp; uniform float uWaves; uniform float uLen; uniform float uInten;
out vec3 fragWorld; out vec4 fragColor; out float fragViewZ; out vec2 fragUV;
void main() {
    vec3 p = vertexPosition;
    float u = vertexTexCoord.x, v = vertexTexCoord.y;
    float L = max(uLen, 0.001);
    float w = uPhase - u * uWaves * 6.2832;
    float a = uAmp * L * uInten;
    if (uAnim == 0) { p.x += sin(w) * a * (0.15 + u * u * 1.3); if (v > 0.2 && v < 0.3) p.y += sin(uPhase * 1.7 + u * 3.0) * a * 0.3; }
    else if (uAnim == 1) { p.y += sin(w) * a * (0.15 + u * u * 1.3); }
    else if (uAnim == 2) { float s = clamp(abs(p.x) / (L * 0.5), 0.0, 1.0); p.y += sin(uPhase - p.z / L * 3.0) * a * s * s * 2.0; }
    else if (uAnim == 3) { p.x += sin(w) * a * (0.4 + 0.6 * u); }
    else if (uAnim == 4) { if (v < 0.4) { float k = 1.0 + 0.14 * sin(uPhase) * uInten; p.x *= k; p.y *= k; } else { p.x += sin(uPhase * 0.8 - u * 4.0) * u * L * 0.12; } }
    else if (uAnim == 5) { if (v >= 0.5) { float leg = floor((v - 0.5) * 20.0 + 0.5); p.y += max(0.0, sin(uPhase + leg * 3.1416)) * L * 0.06 * u * uInten; p.z += cos(uPhase + leg * 3.1416) * L * 0.04 * u * uInten; } }
    else if (uAnim == 6) { if (v >= 0.5) { float arm = (v - 0.5) * 20.0; p.x += sin(uPhase - u * 5.0 + arm) * u * L * 0.1 * uInten; p.y += cos(uPhase * 0.7 - u * 4.0 + arm * 1.3) * u * L * 0.08 * uInten; } else { float k = 1.0 + 0.06 * sin(uPhase) * uInten; p.x *= k; p.y *= k; } }
    else { p.y += sin(uPhase + p.x * 4.0) * a * 0.1; }
    vec4 wp = matModel * vec4(p, 1.0);
    fragWorld = wp.xyz;
    fragColor = vertexColor;
    fragUV = vertexTexCoord;
    fragViewZ = -(matView * wp).z;
    gl_Position = mvp * vec4(p, 1.0);
}
)";

// Fog banks (the Fog setting's high quality): the fog's density at a point, drifting with the wind in banks and
// thinning with height, instead of one even wall. uFogBank.x is how patchy (0: even fog), .y the clock.
#define RT_FOGBANK \
"uniform vec2 uFogBank;\n" \
"float fbH(vec2 p) { return fract(sin(dot(p, vec2(41.7, 289.3))) * 15731.743); }\n" \
"float fbN(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n" \
"  return mix(mix(fbH(i), fbH(i + vec2(1, 0)), f.x), mix(fbH(i + vec2(0, 1)), fbH(i + vec2(1, 1)), f.x), f.y); }\n" \
"float fogBank(vec3 wp) {\n" \
"  if (uFogBank.x <= 0.0) return 1.0;\n" \
"  vec2 p = wp.xz * 0.03 + vec2(uFogBank.y * 0.025, uFogBank.y * 0.011);\n" \
"  float n = fbN(p) * 0.65 + fbN(p * 2.7 + 3.1) * 0.35;\n" \
"  float h = exp(-max(wp.y, 0.0) * 0.08);\n" \
"  return mix(1.0, (0.25 + 1.6 * n * n) * (0.55 + 0.45 * h), uFogBank.x); }\n" \
"uniform float uWaterK, uDepthDark, uSurfW, uCaustK, uTimeW; uniform vec3 uAbsorb;\n" \
"vec3 sceneFog(vec3 col, vec3 wp, float dist, vec3 fogCol, float dens) {\n" \
"  if (uWaterK <= 0.0) return mix(col, fogCol, clamp(1.0 - exp(-dens * fogBank(wp) * dist), 0.0, 1.0));\n" \
"  vec3 T = exp(-uAbsorb * dist * fogBank(wp));\n" \
"  float deep = exp(-uDepthDark * max(uSurfW - wp.y, 0.0));\n" \
"  return col * T + fogCol * mix(0.3, 1.0, deep) * (1.0 - T); }\n" \
"float caust(vec3 wp) {\n" \
"  if (uCaustK <= 0.0) return 0.0;\n" \
"  vec2 p = wp.xz * 0.9; float t = uTimeW * 0.8;\n" \
"  vec2 q = p + vec2(sin(p.y * 1.7 + t), cos(p.x * 1.3 - t * 0.9)) * 0.6;\n" \
"  float a = abs(sin(q.x * 2.1 + t * 1.3) + sin(q.y * 2.4 - t * 1.1) + sin((q.x + q.y) * 1.6 + t * 0.7));\n" \
"  float c = pow(clamp(1.0 - a / 1.6, 0.0, 1.0), 4.0);\n" \
"  float near = clamp(1.0 - (uSurfW - wp.y) / 22.0, 0.0, 1.0);\n" \
"  return c * near * uCaustK; }\n"

static const char* RT_LIT_FS = "#version 330\n" RT_FOGBANK R"(
in vec3 fragWorld; in vec4 fragColor; in float fragViewZ; in vec2 fragUV;
uniform vec4 colDiffuse;
uniform vec3 uCam, uLampPos, uLampDir, uKey, uFill, uRim, uFog;
uniform float uLampRange, uLampCone, uFogDensity, uSurfaceY, uTime, uGlow, uSil;
uniform vec4 uPL[8]; uniform vec4 uPLC[8]; uniform int uPLN;   // point lights: xyz + radius; rgb (0..1) + strength
uniform int uSky;                                                // 1: the sky (stars, the moon, rain): unlit and unfogged
uniform sampler2D uShadowMap; uniform mat4 uLightVP; uniform int uHasShadow;
out vec4 finalColor;
float keyShadow(vec3 wp, vec3 n, vec3 L) {   // (the same as the physically based path's)
    if (uHasShadow == 0) return 1.0;
    float grazing = 1.0 - max(dot(n, L), 0.0);   // (a lamp grazing the planks streaks them with acne without more offset)
    vec4 lp = uLightVP * vec4(wp + n * (0.03 + 0.06 * grazing), 1.0);
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x <= 0.0 || q.y <= 0.0 || q.x >= 1.0 || q.y >= 1.0 || q.z >= 1.0) return 1.0;
    float bias = 0.0015 + 0.006 * grazing;
    float lit = 0.0; vec2 tx = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) lit += (q.z - bias > texture(uShadowMap, q.xy + vec2(x, y) * tx * 1.5).r) ? 0.0 : 1.0;
    return lit / 9.0;
}
// The map kit's surfaces (Red Tide's Visual Overhaul, phase 6): static geometry gets procedural material detail from
// where it is and what it faces, so no surface is a bare flat colour. uSurf: 0 off, 1 the Sunken Ship (warm: planks
// and mahogany panelling; cool: riveted plate with rust running down it), 2 the Cave (strata, mottled wet rock),
// 3 the Reef (rippled sand, lumpy coral rock), 4 Atlantis (marble ashlar, algae), 5 the Void (rusted plate, black sand).
uniform float uSurf;
float sh1(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float sn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(sh1(i), sh1(i + vec2(1, 0)), f.x), mix(sh1(i + vec2(0, 1)), sh1(i + vec2(1, 1)), f.x), f.y); }
float fbm2(vec2 p) { return sn(p) * 0.55 + sn(p * 2.3 + 7.1) * 0.3 + sn(p * 5.1 + 3.3) * 0.15; }
vec3 surfaceDetail(vec3 base, vec3 wp, vec3 n) {
    int style = int(uSurf + 0.5);
    vec3 an = abs(n);
    // the face's own 2D coordinates: floors and ceilings in xz, walls along their run and up
    vec2 uv = an.y > 0.7 ? wp.xz : (an.x > an.z ? wp.zy : wp.xy);
    float warm = base.r - base.b, lum = dot(base, vec3(0.3, 0.59, 0.11));
    float k = 1.0;
    if (style == 1 || style == 5) {   // the ship and the station: wood where it's warm, plate where it's cool
        if (warm > 0.08 && style == 1) {
            if (an.y > 0.7) {   // deck planks: 18 cm boards along x, seams, grain, each board its own shade
                float row = floor(wp.z / 0.18); float seam = smoothstep(0.0, 0.012, abs(fract(wp.z / 0.18) - 0.0)) * smoothstep(0.0, 0.012, abs(fract(wp.z / 0.18) - 1.0));
                float endj = step(0.985, fract(wp.x / 2.4 + sh1(vec2(row, 3.0))));
                k = (0.82 + 0.3 * sh1(vec2(row, 1.0))) * mix(0.55, 1.0, seam) * (1.0 - 0.4 * endj) * (0.9 + 0.2 * sn(vec2(wp.x * 9.0, row * 3.0)));
            } else {            // mahogany panelling: tall panels, a moulding at the dado, wallpaper peeling above it
                float px = fract(uv.x / 0.9); float frame = smoothstep(0.03, 0.06, px) * smoothstep(0.03, 0.06, 1.0 - px);
                float dado = abs(fract(uv.y) - 0.0);
                float above = step(1.1, mod(uv.y + 100.0, 3.2));
                float peel = smoothstep(0.55, 0.62, fbm2(uv * 1.3));
                k = mix(0.7, 1.0, frame) * (0.9 + 0.2 * sn(vec2(uv.x * 0.8, uv.y * 14.0)));
                if (above > 0.5) { base = mix(base, base * vec3(0.95, 0.88, 0.7) + vec3(0.06, 0.05, 0.02), 0.5 * (1.0 - peel)); }
                k *= 1.0 - 0.25 * smoothstep(0.0, 0.03, 0.03 - abs(fract(uv.y / 1.1) - 0.5) * 0.06);
            }
        } else {
            // riveted plate: 1.2 x 0.8 m plates, a dark seam, a row of rivets along each seam, rust bleeding down
            vec2 pc = vec2(uv.x / 1.2, uv.y / 0.8);
            vec2 f = fract(pc);
            float seam = min(min(f.x, 1.0 - f.x) * 1.2, min(f.y, 1.0 - f.y) * 0.8);
            float sline = smoothstep(0.004, 0.012, seam);
            vec2 rv = fract(uv / 0.1); float rivet = (f.y < 0.06 || f.y > 0.94) ? smoothstep(0.32, 0.18, length(rv - 0.5)) : 0.0;
            float plate = 0.86 + 0.24 * sh1(floor(pc));
            float rust = an.y < 0.7 ? smoothstep(0.45, 0.85, sn(vec2(uv.x * 6.0, uv.y * 0.6)) * fbm2(uv * 0.7 + 4.0) * 1.6) : smoothstep(0.55, 0.9, fbm2(uv * 0.6));
            base = mix(base, base * vec3(1.25, 0.72, 0.45) + vec3(0.12, 0.04, 0.0), 0.65 * rust);
            k = plate * mix(0.45, 1.0, sline) * (1.0 + 0.35 * rivet);
            if (style == 5 && an.y > 0.7 && lum < 0.2) { k = 0.85 + 0.25 * fbm2(wp.xz * 3.0); }   // (black sand)
        }
    } else if (style == 2) {   // the Cave: strata in the walls, mottled, wet
        float strata = sn(vec2(uv.x * 0.15, wp.y * 2.6 + sn(uv * 0.4) * 2.0));
        k = (0.72 + 0.5 * strata) * (0.85 + 0.3 * fbm2(uv * 1.7));
        if (an.y > 0.7) k = 0.8 + 0.35 * fbm2(wp.xz * 0.9);
    } else if (style == 3) {   // the Reef: rippled sand floors, lumpy encrusted rock
        if (an.y > 0.7 && lum > 0.35) { float rip = sin(wp.x * 5.0 + sn(wp.xz * 0.6) * 4.0) * 0.5 + 0.5; k = 0.88 + 0.16 * rip + 0.08 * sn(wp.xz * 8.0); }
        else k = 0.78 + 0.4 * fbm2(uv * 2.2);
    } else if (style == 4) {   // Atlantis: marble ashlar, the courses offset, algae in the joints and on the tops
        float course = floor(uv.y / 0.6); vec2 bl = vec2(uv.x / 1.4 + 0.5 * mod(course, 2.0), uv.y / 0.6);
        vec2 f = fract(bl); float joint = smoothstep(0.0, 0.03, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
        if (an.y > 0.7) { vec2 g = fract(wp.xz / 1.1); joint = smoothstep(0.0, 0.025, min(min(g.x, 1.0 - g.x), min(g.y, 1.0 - g.y))); }
        float vein = smoothstep(0.48, 0.5, abs(sn(uv * 1.5 + vec2(sn(uv * 3.0) * 2.0)) - 0.5) + 0.48) * 0.12;
        k = (0.9 + 0.15 * sh1(floor(bl))) * mix(0.55, 1.0, joint) * (1.0 - vein);
        float algae = smoothstep(0.5, 0.8, fbm2(uv * 0.8 + 2.0)) * (an.y > 0.7 ? 0.8 : 0.45) + (1.0 - joint) * 0.4;
        base = mix(base, vec3(0.2, 0.32, 0.16), 0.5 * clamp(algae, 0.0, 1.0));
    }
    return base * k;
}
void main() {
    if (uSky == 1) { finalColor = vec4(fragColor.rgb * colDiffuse.rgb, fragColor.a * colDiffuse.a); return; }
    vec3 n = normalize(cross(dFdx(fragWorld), dFdy(fragWorld)));
    vec3 V = normalize(uCam - fragWorld);
    if (dot(n, V) < 0.0) n = -n;
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    if (uSurf > 0.5) base = surfaceDetail(base, fragWorld, n);
    // key: the helmet lamp, a spotlight
    vec3 L = uLampPos - fragWorld; float d = length(L); L /= max(d, 0.0001);
    float cone = smoothstep(uLampCone, uLampCone + 0.18, dot(-L, normalize(uLampDir)));
    float att = clamp(1.0 - d / uLampRange, 0.0, 1.0); att *= att;
    float key = max(dot(n, L), 0.0) * cone * att * keyShadow(fragWorld, n, L);
    // fill: the sea's light from above; rim on the edge away from the key
    float fill = 0.35 + 0.35 * n.y;
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
    vec3 col = base * (uFill / 255.0 * fill * 2.6 + 0.08 + uKey / 255.0 * key * 1.6) + uRim / 255.0 * rim * 0.3 * (0.4 + key);
    // caustics near the surface
    float cz = clamp(1.0 - (uSurfaceY - fragWorld.y) / 18.0, 0.0, 1.0);
    float ca = sin(fragWorld.x * 1.3 + uTime * 1.1) * sin(fragWorld.z * 1.1 - uTime * 0.9) + sin((fragWorld.x + fragWorld.z) * 0.7 + uTime * 1.7);
    if (uWaterK > 0.0) col += base * uFill / 255.0 * caust(fragWorld) * 3.0 * max(n.y, 0.0);   // (under water: the sharper caustics)
    else col += base * max(ca, 0.0) * 0.18 * cz * max(n.y, 0.0);
    for (int i = 0; i < 8; i++) {                          // point lights (the Trawl's lamps, fires, flares)
        if (i >= uPLN) break;
        vec3 Lp = uPL[i].xyz - fragWorld; float dp = length(Lp);
        float ap = clamp(1.0 - dp / uPL[i].w, 0.0, 1.0); ap *= ap;
        col += base * uPLC[i].rgb * uPLC[i].a * ap * (0.25 + 0.75 * max(dot(n, Lp / max(dp, 0.0001)), 0.0)) * 1.6;
    }
    col += base * uGlow;                                   // luminous species glow in the dark
    // fog by distance (thicker the deeper the scene sets it; under water, absorbed channel by channel)
    col = sceneFog(col, fragWorld, fragViewZ, uFog / 255.0, uFogDensity);
    if (uSil > 0.5) col = vec3(0.0);
    finalColor = vec4(col, fragColor.a * colDiffuse.a);
}
)";

// The physically based path (the Trawl's visual overhaul, shared with Red Tide): glTF meshes with real normals and
// UVs, metallic-roughness GGX, normal maps through a cotangent frame from screen derivatives (no tangents needed),
// baked occlusion, emission; lit by the same lamp, points and fog as the inked path, plus the moon and a hemisphere
// ambient. Colour maths in linear light, written back in display space to sit beside the rest of the frame.
static const char* RT_PBR_VS = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
in vec4 vertexBoneIds; in vec4 vertexBoneWeights;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matView; uniform mat4 matNormal;
uniform mat4 boneMatrices[64]; uniform int uSkinned;
out vec3 fragWorld; out vec3 fragNormal; out vec2 fragUV; out vec4 fragColor; out float fragViewZ;
void main() {
    vec4 p = vec4(vertexPosition, 1.0);
    vec3 n = vertexNormal;
    if (uSkinned == 1) {   // the shared rig: four influences per vertex
        mat4 s = boneMatrices[int(vertexBoneIds.x)] * vertexBoneWeights.x + boneMatrices[int(vertexBoneIds.y)] * vertexBoneWeights.y
               + boneMatrices[int(vertexBoneIds.z)] * vertexBoneWeights.z + boneMatrices[int(vertexBoneIds.w)] * vertexBoneWeights.w;
        p = s * p; n = mat3(s) * n;
    }
    vec4 wp = matModel * p;
    fragWorld = wp.xyz;
    fragNormal = normalize((matNormal * vec4(n, 0.0)).xyz);
    fragUV = vertexTexCoord;
    fragColor = vertexColor;
    fragViewZ = -(matView * wp).z;
    gl_Position = mvp * p;
}
)";
static const char* RT_PBR_FS = "#version 330\n" RT_FOGBANK R"(
in vec3 fragWorld; in vec3 fragNormal; in vec2 fragUV; in vec4 fragColor; in float fragViewZ;
uniform sampler2D texture0; uniform sampler2D uMR; uniform sampler2D uNrm; uniform sampler2D uAO; uniform sampler2D uEmit;
uniform int uHasAlb, uHasMR, uHasNrm, uHasAO, uHasEmit, uVcAO;
uniform vec4 colDiffuse; uniform float uMetal, uRough; uniform vec3 uEmitCol; uniform float uWrap, uGlow;
uniform vec3 uCam, uLampPos, uLampDir, uKey, uFog; uniform float uLampRange, uLampCone, uFogDensity;
uniform vec4 uPL[8]; uniform vec4 uPLC[8]; uniform int uPLN;
uniform vec3 uMoonDir, uMoon, uSkyAmb, uSeaAmb; uniform float uMoonK, uAmbK, uSil;
uniform sampler2D uShadowMap; uniform mat4 uLightVP; uniform int uHasShadow;
uniform float uWet, uWetFloor, uFlash;
uniform float uGlass;   // 1: a glass part (drawn last, blended: clear face-on, silvered toward its edges)
out vec4 finalColor;
// the lamp's shadow: a 3x3 filtered look-up in its depth map (1 lit, 0 in shadow); outside the map, lit
float keyShadow(vec3 wp, vec3 n, vec3 L) {
    if (uHasShadow == 0) return 1.0;
    float grazing = 1.0 - max(dot(n, L), 0.0);   // (a lamp grazing the planks streaks them with acne without more offset)
    vec4 lp = uLightVP * vec4(wp + n * (0.03 + 0.06 * grazing), 1.0);
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x <= 0.0 || q.y <= 0.0 || q.x >= 1.0 || q.y >= 1.0 || q.z >= 1.0) return 1.0;
    float bias = 0.0015 + 0.006 * grazing;
    float lit = 0.0; vec2 tx = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) lit += (q.z - bias > texture(uShadowMap, q.xy + vec2(x, y) * tx * 1.5).r) ? 0.0 : 1.0;
    return lit / 9.0;
}
const float PI = 3.14159265;
vec3 toLin(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x, B = dp2perp * duv1.y + dp1perp * duv2.y;
    float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
    return mat3(T * im, B * im, N);
}
float D_GGX(float NdH, float a) { float a2 = a * a; float d = NdH * NdH * (a2 - 1.0) + 1.0; return a2 / (PI * d * d); }
float G_Smith(float NdV, float NdL, float r) { float k = (r + 1.0) * (r + 1.0) / 8.0; return (NdV / (NdV * (1.0 - k) + k)) * (NdL / (NdL * (1.0 - k) + k)); }
vec3 F_Schlick(float c, vec3 F0) { return F0 + (1.0 - F0) * pow(1.0 - c, 5.0); }
vec3 albedo; float metal, rough; vec3 F0, N, V;
vec3 shade(vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float NdL = dot(N, L), NdV = max(dot(N, V), 0.001), NdH = max(dot(N, H), 0.0);
    float wrapL = max((NdL + uWrap) / (1.0 + uWrap), 0.0);       // soft wrap for skin and cloth
    NdL = max(NdL, 0.0);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);
    vec3 spec = D_GGX(NdH, rough * rough) * G_Smith(NdV, NdL, rough) * F / max(4.0 * NdV * NdL, 0.001);
    vec3 kd = (1.0 - F) * (1.0 - metal);
    vec3 tint = mix(vec3(1.0), vec3(1.0, 0.8, 0.75), uWrap * (1.0 - NdL));   // the warm shadow edge of skin
    return (kd * albedo / PI * wrapL * tint + spec * NdL) * radiance;
}
void main() {
    // glTF's colour factors and vertex colours are linear already; only a texture's sRGB needs undoing
    // (an asset with tiling textures carries its baked occlusion in the vertex colours instead of a map)
    vec4 bc = uVcAO == 1 ? colDiffuse : colDiffuse * fragColor;
    albedo = bc.rgb;
    if (uHasAlb == 1 && uGlass < 1.5) { vec4 tx = texture(texture0, fragUV); albedo *= toLin(tx.rgb); bc.a *= tx.a; }   // (a sea-glass shell, uGlass 2: its own colour, not the texture's)
    if (uGlass < 0.5 && bc.a < 0.4) discard;
    metal = uMetal; rough = uRough;
    if (uHasMR == 1) { vec4 mr = texture(uMR, fragUV); rough *= mr.g; metal *= mr.b; }
    N = normalize(fragNormal);
    V = normalize(uCam - fragWorld);
    if (!gl_FrontFacing) N = -N;
    // rain: what it falls on goes darker and glossier, the flat tops most (below the deck's level stays dry)
    if (uWet > 0.0) {
        float w = uWet * mix(0.45, 1.0, smoothstep(0.2, 0.8, N.y)) * smoothstep(0.6, 1.0, fragWorld.y - uWetFloor);
        albedo *= mix(1.0, 0.62, w * (1.0 - metal));
        rough = mix(rough, rough * 0.3, w);
    }
    rough = clamp(rough, 0.04, 1.0);
    if (uHasNrm == 1) { vec3 tn = texture(uNrm, fragUV).xyz * 2.0 - 1.0; N = normalize(cotangentFrame(N, fragWorld, fragUV) * mix(tn, vec3(0.0, 0.0, 1.0), uWet * 0.35)); }   // (a film of water smooths the grain)
    F0 = mix(vec3(0.04), albedo, metal);
    vec3 col = vec3(0.0);
    // the lamp: a spot with a soft cone and inverse-square-ish fall-off to its range
    // (every light is skipped where it can't reach or faces away: this PC's integrated graphics pays per pixel)
    { vec3 L = uLampPos - fragWorld; float d = length(L); L /= max(d, 1e-4);
      float cone = smoothstep(uLampCone, uLampCone + 0.18, dot(-L, normalize(uLampDir)));
      float att = clamp(1.0 - d / uLampRange, 0.0, 1.0); att *= att;
      if (cone * att > 0.0 && dot(N, L) > -0.2) col += shade(L, toLin(uKey / 255.0) * cone * att * 5.0 * keyShadow(fragWorld, N, L)); }
    // the moon
    if (uMoonK > 0.0) col += shade(normalize(-uMoonDir), toLin(uMoon / 255.0) * uMoonK * 2.5);
    // the practical lights
    for (int i = 0; i < 8; i++) {
        if (i >= uPLN) break;
        vec3 L = uPL[i].xyz - fragWorld; float d = length(L);
        if (d >= uPL[i].w) continue;
        L /= max(d, 1e-4);
        if (dot(N, L) < -0.2 && uWrap < 0.01) continue;
        float att = 1.0 - d / uPL[i].w; att *= att;
        col += shade(L, toLin(uPLC[i].rgb) * uPLC[i].a * att * 6.0);
    }
    // hemisphere ambient, with the baked occlusion
    float ao = uHasAO == 1 ? texture(uAO, fragUV).r : 1.0;
    if (uVcAO == 1) ao *= fragColor.r;
    vec3 amb = mix(toLin(uSeaAmb / 255.0), toLin(uSkyAmb / 255.0), N.y * 0.5 + 0.5) * uAmbK;
    col += amb * albedo * (1.0 - metal * 0.7) * ao;
    col += F_Schlick(max(dot(N, V), 0.0), F0) * amb * ao * (1.0 - rough) * 0.8;   // a little of the sky in polished metal
    col *= mix(1.0, ao, 0.6);
    if (uFlash > 0.0) col += albedo * uFlash * vec3(0.75, 0.82, 1.0) * mix(0.25, 1.0, N.y * 0.5 + 0.5);   // lightning: the whole scene lit from the sky for an instant
    if (uHasEmit == 1) col += toLin(texture(uEmit, fragUV).rgb * uEmitCol);
    col += albedo * uGlow;
    col += albedo * toLin(uMoon / 255.0) * caust(fragWorld) * 2.0 * max(N.y, 0.0) * ao;   // (under water, near the surface)
    col = pow(col, vec3(1.0 / 2.2));
    col = sceneFog(col, fragWorld, fragViewZ, uFog / 255.0, uFogDensity);
    if (uSil > 0.5) col = vec3(0.0);
    float ga = uGlass > 1.5 ? mix(0.22, 0.75, pow(1.0 - max(dot(N, V), 0.0), 2.0)) : uGlass > 0.5 ? mix(0.18, 0.85, pow(1.0 - max(dot(N, V), 0.0), 3.0)) : 1.0;
    finalColor = vec4(col, ga);
}
)";

// The sea (the Trawl's world pass): Fresnel between the water's dark body (lit in the lamp's pool) and the reflected
// sky; the moon's glitter path; each lamp stretched on the swell; foam at the hull, the bow and in the wake, white crests
// in heavy weather; rain rings. Display-space colours, like the inked path beside it.
static const char* RT_WATER_FS = "#version 330\n" RT_FOGBANK R"(
in vec3 fragWorld; in vec3 fragNormal; in vec2 fragUV; in vec4 fragColor; in float fragViewZ;
uniform vec3 uCam, uMoonDir, uDeep, uZenith, uHorizon, uFog, uLampPos, uLampDir, uKey;
uniform float uFogDensity, uTime, uCrest, uRain, uAlpha, uMoonK, uLampRange, uLampCone;
uniform vec4 uPL[8]; uniform vec4 uPLC[8]; uniform int uPLN;
uniform vec2 uBoatPos; uniform float uBoatHead, uBoatSpeed, uBoatLen, uBoatBeam;
uniform vec4 uStain[8]; uniform int uStainN;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y); }
void main() {
    vec2 rp = fragWorld.xz;
    vec3 N = normalize(fragNormal);
    // the small ripples on the swell
    N = normalize(N + vec3((noise(rp * 3.0 + uTime * 0.6) - 0.5) * 0.22 + (noise(rp * 8.0 - uTime * 0.9) - 0.5) * 0.1, 0.0,
                           (noise(rp * 3.0 + 17.0 - uTime * 0.5) - 0.5) * 0.22 + (noise(rp * 8.0 + 5.0 + uTime) - 0.5) * 0.1));
    vec3 V = normalize(uCam - fragWorld);
    if (dot(N, V) < 0.0) N = -N;
    float NdV = max(dot(N, V), 0.0);
    float F = 0.02 + 0.98 * pow(1.0 - NdV, 5.0);
    vec3 R = reflect(-V, N);
    vec3 sky = mix(uHorizon / 255.0, uZenith / 255.0, clamp(R.y * 1.6, 0.0, 1.0));
    vec3 md = normalize(uMoonDir);
    float glit = pow(max(dot(R, md), 0.0), 140.0) * 5.0 * uMoonK * (0.5 + noise(rp * 14.0 + uTime * 3.0));
    vec3 refl = sky + vec3(0.8, 0.86, 0.95) * glit;
    for (int i = 0; i < 8; i++) {   // the lamps, stretched long on the swell
        if (i >= uPLN) break;
        vec3 L = uPL[i].xyz - fragWorld; float d = length(L); L /= max(d, 1e-3);
        refl += uPLC[i].rgb * uPLC[i].a * pow(max(dot(R, L), 0.0), 36.0) * clamp(1.0 - d / (uPL[i].w * 5.0), 0.0, 1.0) * 2.2;
    }
    vec3 Lk = uLampPos - fragWorld; float dk = length(Lk); Lk /= max(dk, 1e-3);
    float cone = smoothstep(uLampCone, uLampCone + 0.18, dot(-Lk, normalize(uLampDir)));
    float att = clamp(1.0 - dk / uLampRange, 0.0, 1.0); att *= att;
    float pool = cone * att;
    refl += uKey / 255.0 * pow(max(dot(R, Lk), 0.0), 50.0) * pool * 2.0;
    vec3 body = uDeep / 255.0 * (0.7 + 3.0 * pool) + uKey / 255.0 * pool * 0.06;
    vec3 col = mix(body, refl, F);
    // foam: along the hull and at the bow with way on her, the wake widening behind, crests in a blow
    vec2 d2 = rp - uBoatPos; float ch = cos(uBoatHead), sh = sin(uBoatHead);
    vec2 bl = vec2(d2.x * ch + d2.y * sh, -d2.x * sh + d2.y * ch);
    float hl = uBoatLen * 0.5, hb = uBoatBeam * 0.5;
    // never inside her hull (the engine room's eye is barely over the waterline): her plan, full amidships and
    // narrowing over the forward quarter
    float hbx = bl.x > hl * 0.45 ? max(0.5, hb - (bl.x - hl * 0.45) * 0.42) : hb;
    if (bl.x > -hl && bl.x < hl + 0.3 && abs(bl.y) < hbx - 0.12) discard;
    float e = length(vec2(bl.x / hl, bl.y / hb));
    float way = clamp(uBoatSpeed / 2.5, 0.0, 1.0);
    float foam = smoothstep(1.22, 1.0, e) * (0.25 + 0.75 * way);
    if (bl.x > hl * 0.55) foam += smoothstep(1.45, 1.0, e) * way;
    float along = -(bl.x + hl * 0.85);
    if (along > 0.0) {
        float w = hb * 0.5 + along * 0.33;
        foam += (smoothstep(w, 0.0, abs(bl.y)) * 0.55 + smoothstep(1.4, 0.0, abs(abs(bl.y) - w)) * 0.8) * exp(-along / (5.0 + uBoatSpeed * 9.0)) * way;
    }
    foam += uCrest * smoothstep(0.35, 0.9, fragWorld.y / max(0.2, uCrest * 2.2));
    foam = smoothstep(0.3, 0.75, foam * (0.45 + 0.75 * noise(rp * 4.0 + vec2(uTime * 0.4, 0.0))));
    vec3 foamCol = vec3(0.7, 0.76, 0.8) * (0.18 + 1.6 * pool) + vec3(0.6, 0.66, 0.75) * uMoonK * 0.15;
    col = mix(col, foamCol, foam);
    // blood on the water: dark, ragged-edged stains spreading off her scuppers and the gutting rail
    for (int i = 0; i < 8; i++) {
        if (i >= uStainN) break;
        float sd = length(rp - uStain[i].xy) / max(uStain[i].z, 0.1);
        if (sd > 1.4) continue;
        float rag = 0.75 + 0.5 * noise(rp * 1.7 + float(i) * 13.0);
        float m = smoothstep(rag, rag * 0.35, sd) * uStain[i].w;
        col = mix(col, vec3(0.16, 0.015, 0.015) * (0.35 + 1.4 * pool), m * 0.85);
    }
    if (uRain > 0.0) {   // rings where the drops land
        vec2 g = rp * 1.6, cell = floor(g), fp = fract(g);
        float h = hash(cell), tt = fract(uTime * 0.9 + h);
        float r = length(fp - vec2(hash(cell + 3.1), hash(cell + 7.7)));
        col += vec3(0.35) * smoothstep(0.05, 0.0, abs(r - tt * 0.45)) * (1.0 - tt) * uRain * (0.25 + pool);
    }
    float fog = 1.0 - exp(-uFogDensity * fogBank(fragWorld) * fragViewZ);
    col = mix(col, uFog / 255.0, clamp(fog, 0.0, 1.0));
    finalColor = vec4(col, uAlpha + foam * (1.0 - uAlpha));
}
)";
// The night sky on a dome round the eye: gradient, moon (lit in its phase), back-lit clouds, a fogged horizon.
static const char* RT_SKY_FS = R"(#version 330
in vec3 fragWorld; in vec3 fragNormal; in vec2 fragUV; in vec4 fragColor; in float fragViewZ;
uniform vec3 uCam, uZen, uHor, uCloudC, uMoonDir, uFog; uniform float uPhase, uCloudK, uTime, uFogK;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y); }
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * noise(p); p *= 2.03; a *= 0.5; } return s; }
void main() {
    vec3 dir = normalize(fragWorld - uCam);
    float h = dir.y;
    vec3 col = mix(uHor / 255.0, uZen / 255.0, smoothstep(-0.05, 0.55, h));
    vec3 md = normalize(uMoonDir);
    float a = dot(dir, md);
    vec3 moonC = vec3(0.92, 0.9, 0.82);
    float lit = abs(uPhase - 0.5) * 2.0;           // 0 full .. 1 new
    col += moonC * 0.12 * pow(max(a, 0.0), 40.0) * (1.0 - lit) * (1.0 - uCloudK * 0.6);   // the halo
    float rr = 0.032;
    if (a > cos(rr)) {
        vec3 rt = normalize(cross(md, vec3(0, 1, 0))), up = cross(rt, md);
        vec2 q = vec2(dot(dir - md * a, rt), dot(dir - md * a, up)) / sin(rr);    // on the disc, -1..1
        float z = sqrt(max(0.0, 1.0 - dot(q, q)));
        // the lit side: the disc as a sphere lit from an angle set by the phase (full: from behind the eye; new: from behind it)
        float th = (uPhase - 0.5) * 6.2832;
        float shade = clamp(dot(vec3(q, z), vec3(sin(th), 0.0, cos(th))) * 4.0, 0.0, 1.0);
        float mare = 0.82 + 0.18 * noise(q * 3.0 + 4.0);
        col = mix(col, moonC * mare * (0.08 + 0.92 * shade), smoothstep(1.0, 0.9, length(q)));
    }
    // clouds: drifting, thicker toward the horizon, their edges silvered near the moon
    if (h > -0.02) {
        vec2 cp = dir.xz / (h + 0.12) * 1.6 + vec2(uTime * 0.012, uTime * 0.004);
        float c = smoothstep(1.0 - uCloudK, 1.25 - uCloudK * 0.6, fbm(cp));
        float back = pow(max(a, 0.0), 6.0);
        vec3 cc = uCloudC / 255.0 * (0.6 + 0.4 * (1.0 - h)) + moonC * back * 0.35 * (1.0 - lit) * (1.0 - c * 0.5);
        col = mix(col, cc, c * 0.92);
    }
    col = mix(col, uFog / 255.0, uFogK * (1.0 - smoothstep(-0.02, 0.32, h)));
    finalColor = vec4(col, 1.0);
}
)";

// The normal/depth pass: view-space normal in rg, linear depth split over ba (16 bits).
static const char* RT_ND_FS = R"(#version 330
in vec3 fragWorld; in vec4 fragColor; in float fragViewZ; in vec2 fragUV;
uniform mat4 matView; uniform float uFar; uniform vec3 uCam;
out vec4 finalColor;
void main() {
    vec3 n = normalize(cross(dFdx(fragWorld), dFdy(fragWorld)));
    if (dot(n, uCam - fragWorld) < 0.0) n = -n;
    vec3 vn = normalize((matView * vec4(n, 0.0)).xyz);
    float dz = clamp(fragViewZ / uFar, 0.0, 1.0) * 65535.0;
    float hi = floor(dz / 256.0), lo = dz - hi * 256.0;
    finalColor = vec4(vn.xy * 0.5 + 0.5, hi / 255.0, lo / 255.0);
}
)";

// Screen-space ambient occlusion, its own pass at half the view's resolution (it was a third of the frame's cost on
// this PC run per screen pixel in the composite); the composite blurs it as it reads it.
static const char* RT_AO_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0;
uniform float uAORad, uFarD, uTanHalf, uAspect;
out vec4 finalColor;
#define uND texture0
float depthAt(vec2 uv) { vec4 t = texture(uND, uv); if (t.b == 0.0 && t.a == 0.0 && t.r == 0.0 && t.g == 0.0) return 1.0; return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0; }
vec3 normAt(vec2 uv) { vec4 t = texture(uND, uv); vec2 xy = t.rg * 2.0 - 1.0; return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy)))); }
float bayer(vec2 p) { int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0)); int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5); return float(m[y * 4 + x]) / 16.0; }
vec3 viewPos(vec2 uv, float dm) { return vec3((uv * 2.0 - 1.0) * vec2(uTanHalf * uAspect, uTanHalf) * dm, -dm); }
// eight taps on a disc in view space round the point, turned by a 4x4 pattern, each counting when the surface it
// lands on stands in front of it (and near enough to matter): the contact shadows
float ssao(vec2 uv, float d) {
    float dm = d * uFarD;
    if (d >= 0.999 || dm > 40.0) return 1.0;
    vec3 P = viewPos(uv, dm);
    vec3 n = normAt(uv);
    float rot = bayer(gl_FragCoord.xy) * 6.2832;
    float occ = 0.0;
    for (int i = 0; i < 8; i++) {
        float a = rot + float(i) * 2.39996;
        float r = uAORad * (0.25 + 0.75 * fract(float(i) * 0.618 + bayer(gl_FragCoord.yx)));
        vec3 dir = normalize(vec3(cos(a), sin(a), 0.6) + n);   // leaning out of the surface
        vec3 S = P + dir * r;
        vec2 su = (S.xy / -S.z) / vec2(uTanHalf * uAspect, uTanHalf) * 0.5 + 0.5;
        if (su.x < 0.0 || su.y < 0.0 || su.x > 1.0 || su.y > 1.0) continue;
        float sd = depthAt(su) * uFarD;
        float diff = -S.z - sd;                                 // positive: something stands in front of the tap
        occ += (diff > 0.02 ? 1.0 : 0.0) * smoothstep(uAORad * 3.0, 0.0, abs(dm - sd));
    }
    return 1.0 - occ / 8.0;
}
void main() { float a = ssao(fragTexCoord, depthAt(fragTexCoord)); finalColor = vec4(a, a, a, 1.0); }
)";

// The ink composite: Sobel edges on depth and normals (line weight by distance), Bayer stipple in shadow, paper
// grain, a vignette, and the red at the mask's edge that grows with the local scent.
static const char* RT_INK_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D uND; uniform sampler2D uAOTex; uniform vec2 uAOTexel;
uniform vec2 uRes; uniform float uTime; uniform float uBlood; uniform float uSil; uniform vec3 uFog;
uniform float uOutline, uStipple, uGrain; uniform vec3 uInkTint;
uniform float uAOK, uAORad, uFarD, uTanHalf, uAspect, uFilmic, uExposure, uGradeK, uSat; uniform vec3 uGradeLo, uGradeHi;
// under water (phase 2): the port's lens, the bloom and the light shafts (both from a quarter-resolution pass), the
// ink line fading into the water
uniform float uWaterC, uLens, uBloomK, uInkFade;
uniform sampler2D uBloomTex; uniform vec2 uBloomTexel;
out vec4 finalColor;
float depthAt(vec2 uv) { vec4 t = texture(uND, uv); if (t.b == 0.0 && t.a == 0.0 && t.r == 0.0 && t.g == 0.0) return 1.0; return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0; }
vec3 normAt(vec2 uv) { vec4 t = texture(uND, uv); vec2 xy = t.rg * 2.0 - 1.0; return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy)))); }
float bayer(vec2 p) { int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0)); int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5); return float(m[y * 4 + x]) / 16.0; }
// the half-resolution occlusion, softened by four bilinear taps (which is a 4x4 blur)
float aoAt(vec2 uv) {
    vec2 o = uAOTexel;
    return 0.25 * (texture(uAOTex, uv + vec2(o.x, o.y)).r + texture(uAOTex, uv + vec2(-o.x, o.y)).r
                 + texture(uAOTex, uv + vec2(o.x, -o.y)).r + texture(uAOTex, uv + vec2(-o.x, -o.y)).r);
}
vec3 filmic(vec3 x) {   // a gentle ACES-style curve (Narkowicz), keeping the night's darks
    x *= uExposure;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec2 uv = fragTexCoord;
    vec2 cc = uv - 0.5;
    float r2 = dot(cc * vec2(uAspect, 1.0), cc * vec2(uAspect, 1.0));
    if (uLens > 0.0) uv = 0.5 + cc * (1.0 - uLens * 0.045 * r2);   // the port's glass: a gentle barrel
    vec3 col = texture(texture0, uv).rgb;
    if (uLens > 0.0) {   // a chromatic fringe toward the rim of the port
        float f = uLens * 0.004 * r2;
        col.r = texture(texture0, 0.5 + (uv - 0.5) * (1.0 + f)).r;
        col.b = texture(texture0, 0.5 + (uv - 0.5) * (1.0 - f)).b;
    }
    float d = depthAt(uv);
    if (uAOK > 0.0) col *= mix(1.0, aoAt(uv), uAOK);
    if (uBloomK > 0.0 || uWaterC > 0.0) {   // the bloom off bright sources and the light shafts (the quarter-resolution pass)
        vec2 o = uBloomTexel;
        vec3 b = 0.25 * (texture(uBloomTex, uv + vec2(o.x, o.y)).rgb + texture(uBloomTex, uv + vec2(-o.x, o.y)).rgb
                       + texture(uBloomTex, uv + vec2(o.x, -o.y)).rgb + texture(uBloomTex, uv + vec2(-o.x, -o.y)).rgb);
        col += b;
    }
    // line weight: thicker near, thinner far (1.8 px near to 0.8 px at the far plane); a thin outline (uOutline < 1)
    // is a 1 px line at most; no outline, no edge taps
    float edge = 0.0;
    if (uOutline > 0.0) {
        float wpx = mix(1.8, 0.8, clamp(d * 3.0, 0.0, 1.0)) * (uOutline < 0.99 ? 0.55 : 1.0);
        vec2 px = wpx / uRes;
        float dd = 0.0; vec3 nn = normAt(uv); float ne = 0.0;
        for (int i = 0; i < 4; i++) {
            vec2 o = (i == 0) ? vec2(px.x, 0) : (i == 1) ? vec2(-px.x, 0) : (i == 2) ? vec2(0, px.y) : vec2(0, -px.y);
            float d2 = depthAt(uv + o);
            dd += abs(d2 - d);
            ne += 1.0 - max(dot(nn, normAt(uv + o)), 0.0);
        }
        edge = clamp(smoothstep(0.004 + d * 0.02, 0.012 + d * 0.05, dd) + smoothstep(0.35, 0.8, ne) * (1.0 - d), 0.0, 1.0);
    }
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    // Bayer stipple in the shadows (only on geometry, not the open water)
    if (d < 0.999 && uStipple > 0.5) {
        float th = bayer(gl_FragCoord.xy);
        float shade = smoothstep(0.07, 0.01, lum);
        if (th < shade * 0.7) col *= 0.45;
    }
    vec3 ink = uInkTint / 255.0;
    if (uInkFade > 0.0) {   // under water the line takes the water's colour and fades into it with distance
        edge *= 1.0 - uInkFade * smoothstep(0.04, 0.45, d);
        ink = mix(ink, uFog / 255.0 * 0.45, clamp(d * 3.0, 0.0, 1.0));
    }
    col = mix(col, ink, edge * 0.9 * uOutline);
    // paper grain that never scrolls, and a vignette (the helmet port's rim: darker and tighter with the lens)
    float g = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);
    col *= 1.0 - (0.06 - 0.06 * g) * uGrain;
    vec2 c = fragTexCoord - 0.5;
    float vig = smoothstep(0.85 - 0.08 * uLens, 0.3, length(c * vec2(1.25, 1.0)));
    col *= mix(0.72, 0.5, uLens) + mix(0.28, 0.5, uLens) * vig;
    // the red at the mask's edge (scent meter)
    col = mix(col, vec3(0.5, 0.02, 0.02), clamp(uBlood, 0.0, 1.0) * (1.0 - vig) * 0.8);
    if (uFilmic > 0.0) {
        // the filmic curve on linear light, then a split tone (cold darks, warm lamplight) and the saturation
        vec3 lin = pow(max(col, 0.0), vec3(2.2));
        vec3 f = pow(filmic(lin * 1.6), vec3(1.0 / 2.2));
        col = mix(col, f, uFilmic);
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        vec3 tone = mix(uGradeLo / 255.0, uGradeHi / 255.0, smoothstep(0.05, 0.6, l));
        col = mix(col, col * tone * 2.0, uGradeK);
        col = mix(vec3(l), col, uSat);
    }
    if (uSil > 0.5) col = d < 0.999 ? vec3(0.0) : vec3(0.85, 0.82, 0.74);
    finalColor = vec4(col, 1.0);
}
)";

// the light shafts (computed in the bloom pass, at a quarter of the view's resolution: they are soft, and were the
// open water's biggest cost at full resolution): top xyz + radius; dir xyz + length; rgb + strength
static const char* RT_SHAFTS = R"(
uniform vec3 uCamP, uCamF, uCamR, uCamU;
uniform vec4 uShA[8]; uniform vec4 uShB[8]; uniform vec4 uShC[8]; uniform int uShN;
// the light a shaft scatters toward the eye along the view ray up to the surface it ends on: the ray's closest pass
// to the shaft's axis (clamped to both segments), a soft Gaussian across it, fading down its length, flickering
float shafts(vec2 uv, float dm, out vec3 tint) {
    tint = vec3(0.0);
    if (uShN == 0) return 0.0;
    vec3 rd = normalize(uCamF + (uv.x * 2.0 - 1.0) * uTanHalf * uAspect * uCamR + (uv.y * 2.0 - 1.0) * uTanHalf * uCamU);
    float rayLen = dm / max(dot(rd, uCamF), 0.05);
    float sum = 0.0;
    for (int i = 0; i < 8; i++) {
        if (i >= uShN) break;
        vec3 A = uShA[i].xyz, D = normalize(uShB[i].xyz); float R = uShA[i].w, Lh = uShB[i].w;
        vec3 w0 = uCamP - A;
        float b = dot(rd, D), d = dot(rd, w0), e = dot(D, w0), den = 1.0 - b * b;
        float s = den > 1e-4 ? clamp((b * e - d) / den, 0.0, rayLen) : 0.0;   // along the view ray
        float u = clamp(e + b * s, 0.0, Lh);                                    // along the shaft
        s = clamp(dot(A + D * u - uCamP, rd), 0.0, rayLen);
        vec3 P = uCamP + rd * s, Q = A + D * u;
        float r = R * (1.0 + 0.6 * u / Lh);                                     // widening as it falls
        float g = exp(-pow(length(P - Q) / r, 2.0) * 2.2);
        float fade = smoothstep(0.0, 0.12 * Lh, u) * (1.0 - smoothstep(0.55 * Lh, Lh, u));
        float flick = 0.75 + 0.25 * sin(uTime * 0.9 + float(i) * 2.3 + u * 0.4);
        float k = g * fade * flick * uShC[i].a * min(1.0, rayLen / max(r, 0.5));
        sum += k; tint += uShC[i].rgb * k;
    }
    if (sum > 0.0) tint /= sum;
    return sum;
}
)";
// bloom (under water): the bright parts of the view (lamps, glow, sunlit sand, shafts' cores) at a quarter of its
// resolution, a 5x5 Gaussian of what passes the threshold; the composite blurs it again as it reads it
// (the shafts ride in the same pass and the same texture: the composite adds it as it is)
static const char* RT_BLOOM_HEAD = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D uND; uniform vec2 uTexel;
uniform float uBloomK, uWaterC, uTanHalf, uAspect, uTime, uFarD;
out vec4 finalColor;
)";
static const char* RT_BLOOM_MAIN = R"(
float depthAt(vec2 uv) { vec4 t = texture(uND, uv); if (t.b == 0.0 && t.a == 0.0 && t.r == 0.0 && t.g == 0.0) return 1.0; return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0; }
void main() {
    vec3 s = vec3(0.0); float w = 0.0;
    if (uBloomK > 0.0)
        for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) {
            vec3 c = texture(texture0, fragTexCoord + vec2(x, y) * uTexel).rgb;
            float l = dot(c, vec3(0.3, 0.59, 0.11)), k = exp(-float(x * x + y * y) * 0.3);
            s += c * smoothstep(0.78, 1.0, l) * k; w += k;
        }
    vec3 col = w > 0.0 ? s / w * 1.6 * uBloomK : vec3(0.0);
    if (uWaterC > 0.0) {
        vec3 st; float sh = shafts(fragTexCoord, depthAt(fragTexCoord) * uFarD, st);
        vec3 under = texture(texture0, fragTexCoord).rgb;
        col += st * sh * 0.55 * (1.0 - 0.5 * dot(under, vec3(0.333)));
    }
    finalColor = vec4(col, 1.0);
}
)";
static Shader gInk{}, gPbr{}, gNDPbr{}, gWaterSh{}, gSkySh{}, gDepthSh{}, gAOSh{}, gBloomSh{};
static RenderTexture2D gBloomRT{};
static int gInkBloomLoc = -1;
static int L_depthSkinned = -1, L_ao[4], L_inkAOTex = -1, L_inkAOTexel = -1;
static RenderTexture2D gAORT{};
static Model gSkyBall{};
static int L_pbr[40], L_pbrSkinned = -1, L_ndPbrSkinned = -1, L_ndPbrFar = -1, L_ndPbrCam = -1;
static const char* PBR_U[] = {"uHasAlb", "uHasMR", "uHasNrm", "uHasAO", "uHasEmit", "uMetal", "uRough", "uEmitCol", "uWrap", "uGlow",
                              "uCam", "uLampPos", "uLampDir", "uKey", "uFog", "uLampRange", "uLampCone", "uFogDensity", "uPL", "uPLC",
                              "uPLN", "uMoonDir", "uMoon", "uSkyAmb", "uSeaAmb", "uMoonK", "uAmbK", "uSil", "uVcAO"};
enum { PU_HASALB, PU_HASMR, PU_HASNRM, PU_HASAO, PU_HASEMIT, PU_METAL, PU_ROUGH, PU_EMITCOL, PU_WRAP, PU_GLOW,
       PU_CAM, PU_LAMPPOS, PU_LAMPDIR, PU_KEY, PU_FOG, PU_RANGE, PU_CONE, PU_FOGD, PU_PL, PU_PLC,
       PU_PLN, PU_MOONDIR, PU_MOON, PU_SKYAMB, PU_SEAAMB, PU_MOONK, PU_AMBK, PU_SIL, PU_VCAO, PU_COUNT };
static int L_inkOutline, L_inkStipple, L_inkGrain, L_inkTint;
static int L_inkX[16];
// the lamp's shadow map: a depth-only framebuffer (1024 square: this PC's integrated graphics is the target)
static RenderTexture2D gShadowRT{};
static const int SHADOW_UNIT = 12;                       // (texture unit 12: above the twelve material map slots)
static int L_litShadow[3], L_pbrShadow[3];               // the sampler, the light's view-projection, the switch
static Quality gQuality;
void SetQuality(const Quality& q) { gQuality = q; gQuality.scale = std::clamp(q.scale, 0.5f, 1.0f); }
const Quality& GetQuality() { return gQuality; }
void ApplyGameQuality() {
    // the player's Graphics page, or DEPTH_GFX="shadows,ao,fog,scale%" (0-3, 0/1, 0/1, 50-100) for measuring
    const Settings& S = GameSettings();
    int sh = S.gfxShadows, ao = S.gfxAO ? 1 : 0, fg = S.gfxFog, sc = S.gfxScale;
    if (const char* e = getenv("DEPTH_GFX")) sscanf(e, "%d,%d,%d,%d", &sh, &ao, &fg, &sc);
    static const int SIZE[4] = {0, 512, 1024, 2048};
    Quality q; q.shadow = SIZE[std::clamp(sh, 0, 3)]; q.ao = ao != 0; q.fog = fg; q.scale = sc / 100.0f;
    SetQuality(q);
}
static bool EnsureShadowMap() {
    int size = gQuality.shadow;
    if (size <= 0) return false;
    if (gShadowRT.id && gShadowRT.texture.width == size) return true;
    if (gShadowRT.id) { rlUnloadTexture(gShadowRT.texture.id); rlUnloadFramebuffer(gShadowRT.id); gShadowRT = {}; }   // (the quality changed)
    gShadowRT.id = rlLoadFramebuffer();
    if (!gShadowRT.id) return false;
    unsigned int dt = rlLoadTextureDepth(size, size, false);
    gShadowRT.texture = {dt, size, size, 1, 19};
    gShadowRT.depth = gShadowRT.texture;
    rlEnableFramebuffer(gShadowRT.id);
    rlFramebufferAttach(gShadowRT.id, dt, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool ok = rlFramebufferComplete(gShadowRT.id);
    rlDisableFramebuffer();
    if (!ok) { TraceLog(LOG_WARNING, "rt: the shadow map's framebuffer is incomplete: no lamp shadows"); rlUnloadFramebuffer(gShadowRT.id); gShadowRT.id = 0; }
    return ok;
}
static const char* INK_X[] = {"uAOK", "uAORad", "uFarD", "uTanHalf", "uAspect", "uFilmic", "uExposure", "uGradeK", "uSat", "uGradeLo", "uGradeHi"};
static RenderTexture2D gColorRT{}, gNDRT{};
static Model gCube{};
static int L_lit[16], L_nd[8], L_ink[8];
enum { LU_ANIM, LU_PHASE, LU_AMP, LU_WAVES, LU_LEN, LU_INTEN, LU_CAM, LU_LAMPPOS, LU_LAMPDIR, LU_KEY, LU_FILL, LU_RIM, LU_FOG, LU_RANGE, LU_CONE, LU_FOGD };
static int L_litSurf, L_litTime, L_litGlow, L_litSil, L_litPL, L_litPLC, L_litPLN, L_litSky;

static void EnsureShaders() {
    if (gShadersReady || !IsWindowReady()) return;
    gLit = LoadShaderFromMemory(RT_VS, RT_LIT_FS);
    gND = LoadShaderFromMemory(RT_VS, RT_ND_FS);
    gInk = LoadShaderFromMemory(nullptr, RT_INK_FS);
    const char* names[16] = {"uAnim", "uPhase", "uAmp", "uWaves", "uLen", "uInten", "uCam", "uLampPos", "uLampDir", "uKey", "uFill", "uRim", "uFog", "uLampRange", "uLampCone", "uFogDensity"};
    for (int i = 0; i < 16; i++) L_lit[i] = GetShaderLocation(gLit, names[i]);
    for (int i = 0; i < 6; i++) L_nd[i] = GetShaderLocation(gND, names[i]);
    L_nd[6] = GetShaderLocation(gND, "uFar");
    L_nd[7] = GetShaderLocation(gND, "uCam");
    L_litSurf = GetShaderLocation(gLit, "uSurfaceY");
    L_litTime = GetShaderLocation(gLit, "uTime");
    L_litGlow = GetShaderLocation(gLit, "uGlow");
    L_litSil = GetShaderLocation(gLit, "uSil");
    L_litPL = GetShaderLocation(gLit, "uPL");
    L_litPLC = GetShaderLocation(gLit, "uPLC");
    L_litPLN = GetShaderLocation(gLit, "uPLN");
    L_litSky = GetShaderLocation(gLit, "uSky");
    L_ink[0] = GetShaderLocation(gInk, "uND");
    L_ink[1] = GetShaderLocation(gInk, "uRes");
    L_ink[2] = GetShaderLocation(gInk, "uTime");
    L_ink[3] = GetShaderLocation(gInk, "uBlood");
    L_ink[4] = GetShaderLocation(gInk, "uSil");
    L_ink[5] = GetShaderLocation(gInk, "uFog");
    L_inkOutline = GetShaderLocation(gInk, "uOutline"); L_inkStipple = GetShaderLocation(gInk, "uStipple");
    L_inkGrain = GetShaderLocation(gInk, "uGrain"); L_inkTint = GetShaderLocation(gInk, "uInkTint");
    for (int i = 0; i < 11; i++) L_inkX[i] = GetShaderLocation(gInk, INK_X[i]);
    const char* SH[3] = {"uShadowMap", "uLightVP", "uHasShadow"};
    gPbr = LoadShaderFromMemory(RT_PBR_VS, RT_PBR_FS);
    for (int i = 0; i < 3; i++) { L_litShadow[i] = GetShaderLocation(gLit, SH[i]); L_pbrShadow[i] = GetShaderLocation(gPbr, SH[i]); }
    gDepthSh = LoadShaderFromMemory(RT_PBR_VS, "#version 330\nout vec4 finalColor;\nvoid main() { finalColor = vec4(0.0); }\n");
    L_depthSkinned = GetShaderLocation(gDepthSh, "uSkinned");
    gAOSh = LoadShaderFromMemory(nullptr, RT_AO_FS);
    gBloomSh = LoadShaderFromMemory(nullptr, (std::string(RT_BLOOM_HEAD) + RT_SHAFTS + RT_BLOOM_MAIN).c_str());
    { const char* N[4] = {"uAORad", "uFarD", "uTanHalf", "uAspect"}; for (int i = 0; i < 4; i++) L_ao[i] = GetShaderLocation(gAOSh, N[i]); }
    L_inkAOTex = GetShaderLocation(gInk, "uAOTex"); L_inkAOTexel = GetShaderLocation(gInk, "uAOTexel");
    for (int i = 0; i < PU_COUNT; i++) L_pbr[i] = GetShaderLocation(gPbr, PBR_U[i]);
    // the material maps DrawMesh binds: albedo in texture0, then the metallic-roughness, normal, occlusion and emission
    gPbr.locs[SHADER_LOC_MAP_ALBEDO] = GetShaderLocation(gPbr, "texture0");
    gPbr.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(gPbr, "uMR");
    gPbr.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(gPbr, "uNrm");
    gPbr.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(gPbr, "uAO");
    gPbr.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(gPbr, "uEmit");
    gPbr.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(gPbr, "matNormal");
    L_pbrSkinned = GetShaderLocation(gPbr, "uSkinned");
    // the normal/depth pass for the physically based path: the same vertex shader (so skinned figures bend there too)
    gNDPbr = LoadShaderFromMemory(RT_PBR_VS, RT_ND_FS);
    L_ndPbrSkinned = GetShaderLocation(gNDPbr, "uSkinned");
    L_ndPbrFar = GetShaderLocation(gNDPbr, "uFar");
    L_ndPbrCam = GetShaderLocation(gNDPbr, "uCam");
    gWaterSh = LoadShaderFromMemory(RT_PBR_VS, RT_WATER_FS);
    gWaterSh.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(gWaterSh, "matNormal");
    gSkySh = LoadShaderFromMemory(RT_PBR_VS, RT_SKY_FS);
    gSkyBall = LoadModelFromMesh(GenMeshSphere(85.0f, 24, 32));
    gColorRT = LoadRenderTexture(SCREEN_W, SCREEN_H);
    gNDRT = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(gColorRT.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(gNDRT.texture, TEXTURE_FILTER_POINT);
    MeshBuilder mb;
    mb.Box({0, 0, 0}, {0.5f, 0.5f, 0.5f}, WHITE, 0);
    gCube = LoadModelFromMesh(mb.Build());
    gShadersReady = true;
}
bool RenderReady() { return gShadersReady; }

void RenderShutdown() {
    if (!gShadersReady) return;
    UnloadCreatures();
    UnloadModel(gCube);
    UnloadRenderTexture(gColorRT);
    UnloadRenderTexture(gNDRT);
    UnloadShader(gLit); UnloadShader(gND); UnloadShader(gInk); UnloadShader(gPbr); UnloadShader(gNDPbr);
    gShadersReady = false;
}

// ================================================================ the frame
struct DrawCmd {
    const Model* model;
    Matrix world;
    int anim; float phase, amp, waves, len, inten, glow;
    Color tint;
    int sky = 0;                   // the colour pass only, unlit and unfogged
    int pbr = 0; float wrap = 0;   // the physically based path (every mesh of the model, its own materials)
    bool allGlass = false;         // every mesh drawn in the glass pass (a sea-glass shell over a Forged gun)
    bool noShadow = false;         // left out of the lamp's shadow pass (swaying flora: many, small, cheap to skip)
    int boneOff = -1, boneN = 0;   // a skinned pose: its matrices in gBonePool
    int recOff = 0, recN = 0;      // recoloured materials in gRecPool
    int partOff = -1, partN = 0;   // per-mesh local transforms in gPartPool (an asset's moving parts)
    int water = 0, skydome = 0;    // the sea's and the sky's own shaders (one of each a frame)
};
static WaterLook gWaterLook;
static SkyLook gSkyLook;
static std::map<std::string, int> gWaterLoc, gSkyLoc;
static int WL(const char* n) { auto it = gWaterLoc.find(n); if (it != gWaterLoc.end()) return it->second; return gWaterLoc[n] = GetShaderLocation(gWaterSh, n); }
static int SL(const char* n) { auto it = gSkyLoc.find(n); if (it != gSkyLoc.end()) return it->second; return gSkyLoc[n] = GetShaderLocation(gSkySh, n); }
static std::vector<Matrix> gPartPool;
static std::map<const Model*, AssetInfo> gAssetInfo;
static std::vector<Matrix> gBonePool;
static std::vector<Recolor> gRecPool;
static std::map<const Model*, std::vector<std::string>> gMatNames;   // glTF material names, index = raylib material - 1
static std::set<const Model*> gVcAO;                                  // assets whose vertex colours are baked occlusion
static std::set<const Model*> gBig;                                   // ...and of those, the big ones (a depth prepass pays)
static std::vector<DrawCmd> gQueue;
static Camera3D gCam;
static SceneLight gLight;

static void SetV3(Shader s, int loc, Vector3 v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC3); }
static void SetF(Shader s, int loc, float v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetI(Shader s, int loc, int v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }
static Vector3 C3(Color c) { return {(float)c.r, (float)c.g, (float)c.b}; }

void RenderBegin(const Camera3D& cam, const SceneLight& light) {
    EnsureShaders();
    ApplyGameQuality();
    gCam = cam;
    gLight = light;
    gQueue.clear();
    gBonePool.clear(); gRecPool.clear(); gPartPool.clear();
}

void DrawCreature(const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float intensity, Color tint) {
    if (!cm.ready) return;
    Matrix world = MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixMultiply(MatrixRotateX(-pitch), MatrixRotateY(yaw))), MatrixTranslate(pos.x, pos.y, pos.z));
    gQueue.push_back({&cm.model, world, (int)cm.anim, phase, cm.amp, cm.waves, cm.length, intensity, cm.luminous ? 0.6f : 0.0f, tint});
}
void DrawStatic(const Model& m, Matrix world, Color tint) {
    gQueue.push_back({&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, tint});
}
void DrawCubeM(Matrix world, Color col) {
    gQueue.push_back({&gCube, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, col});
}
void DrawSky(const Model& m, Matrix world, Color tint) {
    DrawCmd d{&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, tint};
    d.sky = 1;
    gQueue.push_back(d);
}
void DrawCubeGlow(Matrix world, Color col, float glow) {
    gQueue.push_back({&gCube, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, glow, col});
}
void DrawStaticGlow(const Model& m, Matrix world, Color tint, float glow) {
    gQueue.push_back({&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, glow, tint});
}
void DrawPbr(const Model& m, Matrix world, Color tint, float wrap) {
    DrawCmd d{&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, tint};
    d.pbr = 1; d.wrap = wrap;
    gQueue.push_back(d);
}
static bool gNextNoShadow = false;
void SetNextNoShadow() { gNextNoShadow = true; }
void DrawPbrSkinned(const Model& m, Matrix world, const std::vector<Matrix>& skin, const std::vector<Recolor>& recolor, float wrap, Color tint) {
    DrawCmd d{&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, tint};
    d.pbr = 1; d.wrap = wrap; d.noShadow = gNextNoShadow; gNextNoShadow = false;
    d.boneOff = (int)gBonePool.size(); d.boneN = (int)skin.size();
    gBonePool.insert(gBonePool.end(), skin.begin(), skin.end());
    d.recOff = (int)gRecPool.size(); d.recN = (int)recolor.size();
    for (Recolor r : recolor) {   // (given in display colour; the materials are linear)
        auto lin = [](unsigned char v) { return (unsigned char)std::clamp(powf(v / 255.0f, 2.2f) * 255.0f + 0.5f, 0.0f, 255.0f); };
        r.c = {lin(r.c.r), lin(r.c.g), lin(r.c.b), r.c.a};
        gRecPool.push_back(r);
    }
    gQueue.push_back(d);
}

const AssetInfo* AssetInfoOf(const Model* m) { auto it = gAssetInfo.find(m); return it == gAssetInfo.end() ? nullptr : &it->second; }
bool AssetMaterial(const Model* m, const std::string& name, Material* out) {
    auto it = gMatNames.find(m);
    if (!m || it == gMatNames.end()) return false;
    for (size_t i = 0; i < it->second.size(); i++)
        if (it->second[i] == name && (int)i + 1 < m->materialCount) { *out = m->materials[i + 1]; return true; }
    return false;
}
void MarkVertexOcclusion(const Model* m, bool big) { gVcAO.insert(m); if (big) gBig.insert(m); else gBig.erase(m); }
static bool gNextAllGlass = false;
void SetNextPbrGlass(bool on) { gNextAllGlass = on; }
void DrawPbrParts(const Model& m, Matrix world, const std::vector<Matrix>& partLocal, Color tint, float glow) {
    DrawCmd d{&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, glow, tint};
    d.pbr = 1; d.allGlass = gNextAllGlass; gNextAllGlass = false;
    d.partOff = (int)gPartPool.size(); d.partN = (int)partLocal.size();
    gPartPool.insert(gPartPool.end(), partLocal.begin(), partLocal.end());
    gQueue.push_back(d);
}
void AddLateLight(Vector3 p, float r, Color c, float k) { gLight.AddPoint(p, r, c, k); }
void DrawWater(const Model& m, const WaterLook& w) {
    gWaterLook = w;
    DrawCmd d{&m, MatrixIdentity(), (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, WHITE};
    d.water = 1;
    gQueue.push_back(d);
}
void DrawSkyDome(const SkyLook& s) {
    gSkyLook = s;
    DrawCmd d{&gSkyBall, MatrixIdentity(), (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, WHITE};
    d.skydome = 1; d.sky = 1;
    gQueue.push_back(d);
}
static void DrawWaterCmd(const DrawCmd& d) {
    const WaterLook& w = gWaterLook;
    Shader s = gWaterSh;
    auto v3 = [&](const char* n, Vector3 v) { int l = WL(n); if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_VEC3); };
    auto f1 = [&](const char* n, float v) { int l = WL(n); if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_FLOAT); };
    v3("uCam", gCam.position); v3("uMoonDir", Vector3Normalize(Vector3Negate(gLight.moonDir)));
    v3("uDeep", C3(w.deep)); v3("uZenith", C3(w.zenith)); v3("uHorizon", C3(w.horizon)); v3("uFog", C3(gLight.fog));
    v3("uLampPos", gLight.lampPos); v3("uLampDir", gLight.lampDir); v3("uKey", C3(gLight.key));
    f1("uFogDensity", gLight.fogDensity); f1("uTime", gLight.time); f1("uCrest", w.crest); f1("uRain", w.rain); f1("uAlpha", w.alpha);
    f1("uMoonK", w.moonK); f1("uLampRange", gLight.lampRange); f1("uLampCone", gLight.lampCone);
    f1("uBoatHead", w.boatHeading); f1("uBoatSpeed", w.boatSpeed); f1("uBoatLen", w.boatLen); f1("uBoatBeam", w.boatBeam);
    { int l = WL("uBoatPos"); if (l >= 0) SetShaderValue(s, l, &w.boatPos, SHADER_UNIFORM_VEC2); }
    { int n = std::clamp(w.stains, 0, 8); int l = WL("uStain"); if (l >= 0 && n > 0) SetShaderValueV(s, l, w.stain, SHADER_UNIFORM_VEC4, n); SetI(s, WL("uStainN"), n); }
    float pl[4 * SceneLight::MAX_POINTS] = {}, plc[4 * SceneLight::MAX_POINTS] = {};
    int np = std::min(gLight.nPoints, SceneLight::MAX_POINTS);
    for (int i = 0; i < np; i++) { const auto& q = gLight.points[i]; pl[i * 4] = q.p.x; pl[i * 4 + 1] = q.p.y; pl[i * 4 + 2] = q.p.z; pl[i * 4 + 3] = std::max(0.1f, q.r); plc[i * 4] = q.c.r / 255.0f; plc[i * 4 + 1] = q.c.g / 255.0f; plc[i * 4 + 2] = q.c.b / 255.0f; plc[i * 4 + 3] = q.k; }
    { int l = WL("uPL"); if (l >= 0) SetShaderValueV(s, l, pl, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS); }
    { int l = WL("uPLC"); if (l >= 0) SetShaderValueV(s, l, plc, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS); }
    { int l = WL("uPLN"); if (l >= 0) SetShaderValue(s, l, &np, SHADER_UNIFORM_INT); }
    Model& m = const_cast<Model&>(*d.model);
    Material mat = m.materials[0]; mat.shader = s;
    for (int i = 0; i < m.meshCount; i++) DrawMesh(m.meshes[i], mat, d.world);
}
static void DrawSkyCmd(const DrawCmd& d) {
    const SkyLook& k = gSkyLook;
    Shader s = gSkySh;
    auto v3 = [&](const char* n, Vector3 v) { int l = SL(n); if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_VEC3); };
    auto f1 = [&](const char* n, float v) { int l = SL(n); if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_FLOAT); };
    v3("uCam", gCam.position); v3("uZen", C3(k.zenith)); v3("uHor", C3(k.horizon)); v3("uCloudC", C3(k.cloud));
    v3("uMoonDir", Vector3Normalize(k.moonDir)); v3("uFog", C3(gLight.fog));
    f1("uPhase", k.moonPhase); f1("uCloudK", k.cloudCover); f1("uTime", k.time); f1("uFogK", std::clamp(gLight.fogDensity * 9.0f, 0.0f, 1.0f));
    Model& m = const_cast<Model&>(*d.model);
    Material mat = m.materials[0]; mat.shader = s;
    rlDisableDepthMask();
    DrawMesh(m.meshes[0], mat, MatrixTranslate(gCam.position.x, gCam.position.y, gCam.position.z));
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

// ---------------------------------------------------------------- the rig
const RigInfo& RigOf(const Model& m) {
    static std::map<const Model*, RigInfo> cache;
    auto it = cache.find(&m);
    if (it != cache.end()) return it->second;
    RigInfo r;
    for (int i = 0; i < m.boneCount; i++) {
        r.parent.push_back(m.bones[i].parent);
        r.joint.push_back(m.bindPose[i].translation);
        r.name.push_back(m.bones[i].name);
        if (getenv("DEPTH_RIGDBG")) { Transform b = m.bindPose[i]; printf("rig %2d %-12s parent %2d  t(%.3f %.3f %.3f) r(%.2f %.2f %.2f %.2f) s(%.2f %.2f %.2f)\n", i, m.bones[i].name, m.bones[i].parent, b.translation.x, b.translation.y, b.translation.z, b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w, b.scale.x, b.scale.y, b.scale.z); }
    }
    return cache[&m] = r;
}
std::vector<Matrix> SolveRig(const RigInfo& rig, const RigPose& pose) {
    int n = (int)rig.parent.size();
    std::vector<Matrix> M(n);
    std::vector<char> done(n, 0);
    // each bone: about its bind joint, its own rotation and scale, then its parent's posed frame (bones may come in any order)
    std::function<const Matrix&(int)> solve = [&](int b) -> const Matrix& {
        if (done[b]) return M[b];
        Vector3 h = rig.joint[b];
        Quaternion q = b < (int)pose.rot.size() ? pose.rot[b] : QuaternionIdentity();
        Vector3 s = b < (int)pose.scale.size() ? pose.scale[b] : Vector3{1, 1, 1};
        Matrix local = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-h.x, -h.y, -h.z), MatrixScale(s.x, s.y, s.z)), QuaternionToMatrix(q)), MatrixTranslate(h.x, h.y, h.z));
        int p = rig.parent[b];
        M[b] = p >= 0 && p < n ? MatrixMultiply(local, solve(p)) : MatrixMultiply(local, MatrixTranslate(pose.offset.x, pose.offset.y, pose.offset.z));
        done[b] = 1;
        return M[b];
    };
    for (int b = 0; b < n; b++) solve(b);
    return M;
}
Matrix BoneWorld(const RigInfo& rig, const std::vector<Matrix>& skin, int bone, Matrix world) {
    if (bone < 0 || bone >= (int)skin.size()) return world;
    Vector3 h = rig.joint[bone];
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(h.x, h.y, h.z), skin[bone]), world);
}

std::string AssetDir() {
    static std::string dir;
    if (dir.empty()) { dir = DataDir() + "/../../assets"; if (!DirectoryExists(dir.c_str())) dir = "assets"; }
    return dir;
}
const Model* LoadAsset(const std::string& relPath) {
    static std::map<std::string, std::unique_ptr<Model>> cache;
    auto it = cache.find(relPath);
    if (it != cache.end()) return it->second.get();
    std::string path = AssetDir() + "/" + relPath;
    std::unique_ptr<Model> m;
    if (IsWindowReady() && FileExists(path.c_str())) {
        EnsureShaders();
        m = std::make_unique<Model>(LoadModel(path.c_str()));
        if (m->meshCount == 0) m.reset();
        else {
            // mipmaps and anisotropic filtering on every map: tiling planks and plating shimmer without them
            std::set<unsigned int> done;
            for (int i = 0; i < m->materialCount; i++)
                for (int k = 0; k <= MATERIAL_MAP_BRDF; k++) {
                    Texture2D& tx = m->materials[i].maps[k].texture;
                    if (tx.id == 0 || tx.id == rlGetTextureIdDefault() || done.count(tx.id)) continue;
                    done.insert(tx.id);
                    GenTextureMipmaps(&tx);
                    SetTextureFilter(tx, TEXTURE_FILTER_TRILINEAR);
                    rlTextureParameters(tx.id, RL_TEXTURE_FILTER_ANISOTROPIC, 4);
                }
        }
    }
    if (!m) TraceLog(LOG_WARNING, "rt: asset %s not found", path.c_str());
    if (m && path.size() > 4 && path.substr(path.size() - 4) == ".glb") {
        // the material names from the glb's own JSON chunk (raylib keeps only their order: material i + 1)
        int sz = 0; unsigned char* data = LoadFileData(path.c_str(), &sz);
        if (data && sz > 20) {
            uint32_t jlen; memcpy(&jlen, data + 12, 4);
            if (20 + (int)jlen <= sz) {
                Json j; std::string err;
                if (ParseJson(std::string((const char*)data + 20, jlen), j, &err)) {
                    for (const Json& mt : j["materials"].a) gMatNames[m.get()].push_back(mt["name"].Str0());
                    if (j["scenes"].IsArr() && !j["scenes"].a.empty() && j["scenes"][0]["extras"]["depth_vcao"].I(0)) {
                        gVcAO.insert(m.get());
                        int tris = 0; for (int i = 0; i < m->meshCount; i++) tris += m->meshes[i].triangleCount;
                        if (tris > 20000) gBig.insert(m.get());
                    }
                    // the parts and markers (raylib makes one mesh per primitive, walking the nodes in order)
                    AssetInfo info;
                    auto v3 = [](const Json& a, Vector3 def) { return a.IsArr() && a.a.size() >= 3 ? Vector3{a[0].F(0), a[1].F(0), a[2].F(0)} : def; };
                    auto blenderToGl = [](Vector3 b) { return Vector3{b.x, b.z, -b.y}; };   // (extras keep Blender's axes)
                    for (const Json& n : j["nodes"].a) {
                        Vector3 t = v3(n["translation"], {0, 0, 0});
                        const Json& ex = n["extras"];
                        if (n["mesh"].IsNull()) {
                            if (ex["marker"].I(0)) info.markers.push_back({n["name"].Str0(), {t, blenderToGl(v3(ex["dir"], {1, 0, 0}))}});
                            continue;
                        }
                        int prims = (int)j["meshes"][n["mesh"].I(0)]["primitives"].a.size();
                        AssetPart p;
                        p.name = n["name"].Str0(); p.group = ex["group"].Str0(); p.kind = ex["kind"].Str0(); p.parent = ex["parent"].Str0(); p.glass = ex["glass"].I(0) != 0;
                        if (p.group.empty()) p.group = "static";
                        p.pivot = t; p.axis = blenderToGl(v3(ex["axis"], {0, 1, 0})); p.amount = ex["amount"].F(0);
                        for (int k = 0; k < std::max(1, prims); k++) info.parts.push_back(p);
                    }
                    if ((int)info.parts.size() == m->meshCount) gAssetInfo[m.get()] = info;
                    else TraceLog(LOG_WARNING, "rt: %s: %d parts in the JSON for %d meshes (no moving parts)", path.c_str(), (int)info.parts.size(), m->meshCount);
                }
            }
        }
        if (data) UnloadFileData(data);
    }
    const Model* raw = m.get();
    cache[relPath] = std::move(m);
    return raw;
}
void DrawWorldCube(Vector3 c, Vector3 size, Color col) {
    Matrix world = MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(c.x, c.y, c.z));
    gQueue.push_back({&gCube, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, col});
}

static int gGlassMode = 0;     // 0: everything but glass (and note there was some), 1: the glass alone (the last, blended pass)
static bool gGlassSeen = false;
static void DrawPbrCmd(const DrawCmd& d, Shader sh, bool lit) {
    Model& m = const_cast<Model&>(*d.model);
    Matrix world = MatrixMultiply(m.transform, d.world);
    if (lit) { SetF(gPbr, L_pbr[PU_WRAP], d.wrap); SetF(gPbr, L_pbr[PU_GLOW], d.glow); SetI(gPbr, L_pbr[PU_VCAO], gVcAO.count(d.model) ? 1 : 0); }
    auto names = gMatNames.find(d.model);
    for (int i = 0; i < m.meshCount; i++) {
        bool glass = names != gMatNames.end() && m.meshMaterial[i] >= 1 && m.meshMaterial[i] - 1 < (int)names->second.size() && names->second[m.meshMaterial[i] - 1].find("glass") != std::string::npos;
        if (d.allGlass) glass = true;
        if (!glass) { auto ai = gAssetInfo.find(d.model); glass = ai != gAssetInfo.end() && i < (int)ai->second.parts.size() && ai->second.parts[i].glass; }   // (a baked part tagged glass)
        if (glass != (gGlassMode == 1)) { if (glass) gGlassSeen = true; continue; }
        Material mat = m.materials[m.meshMaterial[i]];
        Shader keep = mat.shader;
        mat.shader = sh;
        // (mat.maps points into the model's own material: the recolour and the tint below are undone after the draw,
        // or they would stay on the model and leak into the next figure that shares it)
        Color keepCol = mat.maps[MATERIAL_MAP_ALBEDO].color;
        // the pose: this draw's matrices into the mesh (several sailors share one model)
        bool skinned = d.boneOff >= 0 && m.meshes[i].boneMatrices && m.meshes[i].boneCount > 0;
        if (skinned) memcpy(m.meshes[i].boneMatrices, gBonePool.data() + d.boneOff, sizeof(Matrix) * std::min(d.boneN, m.meshes[i].boneCount));
        SetI(sh, sh.id == gPbr.id ? L_pbrSkinned : L_ndPbrSkinned, skinned ? 1 : 0);
        if (lit) {
            if (names != gMatNames.end() && m.meshMaterial[i] >= 1 && m.meshMaterial[i] - 1 < (int)names->second.size())
                for (int k = 0; k < d.recN; k++)
                    if (names->second[m.meshMaterial[i] - 1] == gRecPool[d.recOff + k].material) mat.maps[MATERIAL_MAP_ALBEDO].color = gRecPool[d.recOff + k].c;
            Color a = mat.maps[MATERIAL_MAP_ALBEDO].color;
            mat.maps[MATERIAL_MAP_ALBEDO].color = {(unsigned char)(a.r * d.tint.r / 255), (unsigned char)(a.g * d.tint.g / 255), (unsigned char)(a.b * d.tint.b / 255), a.a};
            // (raylib's default 1x1 white texture counts as no map)
            auto has = [&](int k) { return mat.maps[k].texture.id > 0 && mat.maps[k].texture.id != rlGetTextureIdDefault() ? 1 : 0; };
            SetI(gPbr, L_pbr[PU_HASALB], has(MATERIAL_MAP_ALBEDO));
            SetI(gPbr, L_pbr[PU_HASMR], has(MATERIAL_MAP_ROUGHNESS));
            SetI(gPbr, L_pbr[PU_HASNRM], has(MATERIAL_MAP_NORMAL));
            SetI(gPbr, L_pbr[PU_HASAO], has(MATERIAL_MAP_OCCLUSION));
            SetI(gPbr, L_pbr[PU_HASEMIT], has(MATERIAL_MAP_EMISSION));
            SetF(gPbr, L_pbr[PU_METAL], mat.maps[MATERIAL_MAP_METALNESS].value);
            SetF(gPbr, L_pbr[PU_ROUGH], mat.maps[MATERIAL_MAP_ROUGHNESS].value);
            { static int lg = GetShaderLocation(gPbr, "uGlass"); SetF(gPbr, lg, d.allGlass ? 2.0f : glass ? 1.0f : 0.0f); }
            Color e = mat.maps[MATERIAL_MAP_EMISSION].color;
            SetV3(gPbr, L_pbr[PU_EMITCOL], {e.r / 255.0f, e.g / 255.0f, e.b / 255.0f});
        }
        Matrix w = d.partOff >= 0 && i < d.partN ? MatrixMultiply(gPartPool[d.partOff + i], world) : world;
        DrawMesh(m.meshes[i], mat, w);
        mat.maps[MATERIAL_MAP_ALBEDO].color = keepCol;
        (void)keep;
    }
}

static bool gShadowPass = false;   // (the lamp's depth pass: the sea and the sky don't cast its shadows)
static void DrawQueue(Shader sh, bool lit) {
    // the big baked assets (the boat, the quay) first lay down their depth alone, so the expensive lighting runs
    // once per pixel instead of once per overlapping surface
    bool pre = false;
    if (lit)
        for (const DrawCmd& d : gQueue) {
            if (!d.pbr || !gBig.count(d.model)) continue;
            if (!pre) { rlDrawRenderBatchActive(); rlColorMask(false, false, false, false); SetI(gDepthSh, L_depthSkinned, 0); pre = true; }
            Model& m = const_cast<Model&>(*d.model);
            Matrix world = MatrixMultiply(m.transform, d.world);
            for (int i = 0; i < m.meshCount; i++) { Material mat = m.materials[m.meshMaterial[i]]; mat.shader = gDepthSh; DrawMesh(m.meshes[i], mat, world); }
        }
    if (pre) { rlDrawRenderBatchActive(); rlColorMask(true, true, true, true); }
    for (const DrawCmd& d : gQueue) {
        if (pre && d.pbr && gBig.count(d.model)) { rlDrawRenderBatchActive(); rlDisableDepthMask(); DrawPbrCmd(d, gPbr, true); rlDrawRenderBatchActive(); rlEnableDepthMask(); continue; }
        if (d.skydome) { if (lit) DrawSkyCmd(d); continue; }
        if (d.water && gShadowPass) continue;
        if (d.noShadow && gShadowPass) continue;
        if (d.water) { if (lit) DrawWaterCmd(d); else { DrawCmd e = d; e.pbr = 1; e.world = MatrixIdentity(); Model& m = const_cast<Model&>(*d.model); Material mat = m.materials[0]; mat.shader = gNDPbr; SetI(gNDPbr, L_ndPbrSkinned, 0); for (int i = 0; i < m.meshCount; i++) DrawMesh(m.meshes[i], mat, MatrixIdentity()); } continue; }
        if (d.pbr) {
            if (!lit) DrawPbrCmd(d, gNDPbr, false);   // the normal/depth pass: the same skinning, the same edges
            else DrawPbrCmd(d, gPbr, true);
            continue;
        }
        Model& m = const_cast<Model&>(*d.model);
        m.materials[0].shader = sh;
        m.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = d.tint;
        const int* L = lit ? L_lit : L_nd;
        SetI(sh, L[LU_ANIM], d.anim);
        SetF(sh, L[LU_PHASE], d.phase);
        SetF(sh, L[LU_AMP], d.amp);
        SetF(sh, L[LU_WAVES], d.waves);
        SetF(sh, L[LU_LEN], d.len);
        SetF(sh, L[LU_INTEN], d.inten);
        if (!lit && d.sky) continue;
        if (lit) { SetF(sh, L_litGlow, d.glow); SetI(sh, L_litSky, d.sky); static int ls = GetShaderLocation(gLit, "uSurf"); SetF(sh, ls, d.anim == (int)AnimMode::Static && !d.sky ? gLight.surf : 0.0f); }
        DrawMesh(m.meshes[0], m.materials[0], d.world);
    }
    // the glass (gauges' faces, cartridges, ports, tanks), last: blended over what's behind it, writing no depth
    if (lit && gGlassSeen) {
        rlDrawRenderBatchActive(); BeginBlendMode(BLEND_ALPHA); rlDisableDepthMask();
        gGlassMode = 1;
        for (const DrawCmd& d : gQueue) if (d.pbr && !d.water && !d.skydome) DrawPbrCmd(d, gPbr, true);
        gGlassMode = 0;
        rlDrawRenderBatchActive(); rlEnableDepthMask(); EndBlendMode();
    }
    gGlassSeen = false;
}

void RenderEnd() {
    if (!gShadersReady) return;
    const float FAR = 90.0f;
    // the 3D view's own resolution (the composite scales it to the screen)
    int vw = (int)roundf(SCREEN_W * gQuality.scale), vh = (int)roundf(SCREEN_H * gQuality.scale);
    if (gColorRT.texture.width != vw) {
        UnloadRenderTexture(gColorRT); UnloadRenderTexture(gNDRT);
        gColorRT = LoadRenderTexture(vw, vh); gNDRT = LoadRenderTexture(vw, vh);
        SetTextureFilter(gColorRT.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(gNDRT.texture, TEXTURE_FILTER_POINT);   // (depth must never blend across an edge)
    }
    // colour pass
    SetV3(gLit, L_lit[LU_CAM], gCam.position);
    SetV3(gLit, L_lit[LU_LAMPPOS], gLight.lampPos);
    SetV3(gLit, L_lit[LU_LAMPDIR], gLight.lampDir);
    SetV3(gLit, L_lit[LU_KEY], C3(gLight.key));
    SetV3(gLit, L_lit[LU_FILL], C3(gLight.fill));
    SetV3(gLit, L_lit[LU_RIM], C3(gLight.rim));
    SetV3(gLit, L_lit[LU_FOG], C3(gLight.fog));
    SetF(gLit, L_lit[LU_RANGE], gLight.lampRange);
    SetF(gLit, L_lit[LU_CONE], gLight.lampCone);
    SetF(gLit, L_lit[LU_FOGD], gLight.fogDensity);
    SetF(gLit, L_litSurf, gLight.surfaceY);
    SetF(gLit, L_litTime, gLight.time);
    SetF(gLit, L_litSil, gLight.silhouette);
    {
        float pl[4 * SceneLight::MAX_POINTS] = {}, plc[4 * SceneLight::MAX_POINTS] = {};
        int np = std::min(gLight.nPoints, SceneLight::MAX_POINTS);
        for (int i = 0; i < np; i++) {
            const auto& q = gLight.points[i];
            pl[i * 4] = q.p.x; pl[i * 4 + 1] = q.p.y; pl[i * 4 + 2] = q.p.z; pl[i * 4 + 3] = std::max(0.1f, q.r);
            plc[i * 4] = q.c.r / 255.0f; plc[i * 4 + 1] = q.c.g / 255.0f; plc[i * 4 + 2] = q.c.b / 255.0f; plc[i * 4 + 3] = q.k;
        }
        if (L_litPL >= 0) SetShaderValueV(gLit, L_litPL, pl, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS);
        if (L_litPLC >= 0) SetShaderValueV(gLit, L_litPLC, plc, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS);
        SetI(gLit, L_litPLN, np);
        // the physically based path sees the same lights, and the moon and the ambient besides
        if (L_pbr[PU_PL] >= 0) SetShaderValueV(gPbr, L_pbr[PU_PL], pl, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS);
        if (L_pbr[PU_PLC] >= 0) SetShaderValueV(gPbr, L_pbr[PU_PLC], plc, SHADER_UNIFORM_VEC4, SceneLight::MAX_POINTS);
        SetI(gPbr, L_pbr[PU_PLN], np);
        SetV3(gPbr, L_pbr[PU_CAM], gCam.position);
        SetV3(gPbr, L_pbr[PU_LAMPPOS], gLight.lampPos);
        SetV3(gPbr, L_pbr[PU_LAMPDIR], gLight.lampDir);
        SetV3(gPbr, L_pbr[PU_KEY], C3(gLight.key));
        SetV3(gPbr, L_pbr[PU_FOG], C3(gLight.fog));
        SetF(gPbr, L_pbr[PU_RANGE], gLight.lampRange);
        SetF(gPbr, L_pbr[PU_CONE], gLight.lampCone);
        SetF(gPbr, L_pbr[PU_FOGD], gLight.fogDensity);
        SetV3(gPbr, L_pbr[PU_MOONDIR], Vector3Normalize(gLight.moonDir));
        SetV3(gPbr, L_pbr[PU_MOON], C3(gLight.moon));
        SetV3(gPbr, L_pbr[PU_SKYAMB], C3(gLight.skyAmb));
        SetV3(gPbr, L_pbr[PU_SEAAMB], C3(gLight.seaAmb));
        SetF(gPbr, L_pbr[PU_MOONK], gLight.moonK);
        SetF(gPbr, L_pbr[PU_AMBK], gLight.ambK);
        SetF(gPbr, L_pbr[PU_SIL], gLight.silhouette);
    }
    {   // rain on surfaces, lightning
        static int lW = -2, lF, lL;
        if (lW == -2) { lW = GetShaderLocation(gPbr, "uWet"); lF = GetShaderLocation(gPbr, "uWetFloor"); lL = GetShaderLocation(gPbr, "uFlash"); }
        SetF(gPbr, lW, gLight.wet); SetF(gPbr, lF, gLight.wetFloor); SetF(gPbr, lL, gLight.flash);
    }
    {   // under water: absorption (scaled with the scene's fog density), the depth's darkening, caustics
        static int lw[2][6]; static bool got = false;
        static const char* N[6] = {"uWaterK", "uAbsorb", "uDepthDark", "uSurfW", "uCaustK", "uTimeW"};
        if (!got) { for (int k = 0; k < 6; k++) { lw[0][k] = GetShaderLocation(gLit, N[k]); lw[1][k] = GetShaderLocation(gPbr, N[k]); } got = true; }
        Vector3 ab = Vector3Scale(gLight.absorb, gLight.fogDensity / 0.045f);
        for (int s = 0; s < 2; s++) {
            Shader sh = s ? gPbr : gLit;
            SetF(sh, lw[s][0], gLight.water); SetV3(sh, lw[s][1], ab); SetF(sh, lw[s][2], gLight.depthDark);
            SetF(sh, lw[s][3], gLight.surfaceY); SetF(sh, lw[s][4], gLight.water > 0 ? gLight.causticK : 0.0f); SetF(sh, lw[s][5], gLight.time);
        }
    }
    {   // fog banks (high fog quality)
        Vector2 fb{gQuality.fog ? gLight.fogBanks : 0.0f, gLight.time};
        static int lL = -1, lP = -1;
        if (lL < 0) { lL = GetShaderLocation(gLit, "uFogBank"); lP = GetShaderLocation(gPbr, "uFogBank"); }
        if (lL >= 0) SetShaderValue(gLit, lL, &fb, SHADER_UNIFORM_VEC2);
        if (lP >= 0) SetShaderValue(gPbr, lP, &fb, SHADER_UNIFORM_VEC2);
        int lW = WL("uFogBank"); if (lW >= 0) SetShaderValue(gWaterSh, lW, &fb, SHADER_UNIFORM_VEC2);
    }
    // the lamp's shadow map: the frame's geometry from the lamp, depth only
    bool shadow = gLight.keyShadow && gLight.silhouette < 0.5f && EnsureShadowMap();
    if (shadow) {
        Camera3D lc{};
        lc.position = gLight.lampPos;
        lc.target = Vector3Add(gLight.lampPos, gLight.lampDir);
        lc.up = fabsf(gLight.lampDir.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        lc.fovy = gLight.keyShadowFov; lc.projection = CAMERA_PERSPECTIVE;
        BeginLayer(gShadowRT);
        rlClearScreenBuffers();
        rlSetClipPlanes(0.2, std::max(4.0, (double)gLight.lampRange));
        BeginMode3D(lc);
        Matrix lightVP = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
        rlDisableBackfaceCulling();
        gShadowPass = true;
        DrawQueue(gND, false);
        gShadowPass = false;
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        EndMode3D();
        rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
        EndLayer();
        int unit = SHADOW_UNIT;
        for (int k = 0; k < 2; k++) {
            Shader s = k ? gPbr : gLit; const int* Ls = k ? L_pbrShadow : L_litShadow;
            if (Ls[0] >= 0) SetShaderValue(s, Ls[0], &unit, SHADER_UNIFORM_INT);
            if (Ls[1] >= 0) SetShaderValueMatrix(s, Ls[1], lightVP);
            SetI(s, Ls[2], 1);
        }
        rlActiveTextureSlot(SHADOW_UNIT); rlEnableTexture(gShadowRT.texture.id); rlActiveTextureSlot(0);
    } else { SetI(gLit, L_litShadow[2], 0); SetI(gPbr, L_pbrShadow[2], 0); }
    BeginLayer(gColorRT);
    ClearBackground(gLight.silhouette > 0.5f ? Color{216, 209, 189, 255} : gLight.fog);
    BeginMode3D(gCam);
    rlDisableBackfaceCulling();
    DrawQueue(gLit, true);
    rlEnableBackfaceCulling();
    EndMode3D();
    EndLayer();
    // normal/depth pass
    SetF(gND, L_nd[6], FAR);
    SetV3(gND, L_nd[7], gCam.position);
    SetF(gNDPbr, L_ndPbrFar, FAR);
    SetV3(gNDPbr, L_ndPbrCam, gCam.position);
    BeginLayer(gNDRT);
    ClearBackground(BLANK);
    BeginMode3D(gCam);
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlDisableColorBlend();          // depth rides in the alpha channel: it must be written, not blended
    DrawQueue(gND, false);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    rlEnableBackfaceCulling();
    EndMode3D();
    EndLayer();
    // the occlusion at half the view's resolution (drawn with the same flip as the composite, so its texels line up
    // with the normal/depth target's)
    float aoK = gQuality.ao ? gLight.aoK : 0.0f;
    if (aoK > 0) {
        if (gAORT.texture.width != vw / 2) {
            if (gAORT.id) UnloadRenderTexture(gAORT);
            gAORT = LoadRenderTexture(vw / 2, vh / 2);
            SetTextureFilter(gAORT.texture, TEXTURE_FILTER_BILINEAR);
        }
        SetF(gAOSh, L_ao[0], gLight.aoRadius); SetF(gAOSh, L_ao[1], FAR);
        SetF(gAOSh, L_ao[2], tanf(gCam.fovy * DEG2RAD * 0.5f)); SetF(gAOSh, L_ao[3], (float)SCREEN_W / SCREEN_H);
        BeginLayer(gAORT);
        ClearBackground(WHITE);
        BeginShaderMode(gAOSh);
        DrawTexturePro(gNDRT.texture, {0, 0, (float)gNDRT.texture.width, -(float)gNDRT.texture.height}, {0, 0, (float)gAORT.texture.width, (float)gAORT.texture.height}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndLayer();
    }
    // the ink composite into the scene
    Vector2 res{(float)vw, (float)vh};   // (the view's texel size: the edge and occlusion taps are per texel)
    SetShaderValue(gInk, L_ink[1], &res, SHADER_UNIFORM_VEC2);
    SetF(gInk, L_ink[2], gLight.time);
    SetF(gInk, L_ink[3], gLight.bloodTint);
    SetF(gInk, L_ink[4], gLight.silhouette);
    SetV3(gInk, L_ink[5], C3(gLight.fog));
    SetF(gInk, L_inkOutline, gLight.outline); SetF(gInk, L_inkStipple, gLight.stipple); SetF(gInk, L_inkGrain, gLight.grain);
    SetV3(gInk, L_inkTint, C3(gLight.outlineTint));
    SetF(gInk, L_inkX[0], aoK); SetF(gInk, L_inkX[1], gLight.aoRadius); SetF(gInk, L_inkX[2], FAR);
    SetF(gInk, L_inkX[3], tanf(gCam.fovy * DEG2RAD * 0.5f)); SetF(gInk, L_inkX[4], (float)SCREEN_W / SCREEN_H);
    SetF(gInk, L_inkX[5], gLight.filmic); SetF(gInk, L_inkX[6], gLight.exposure); SetF(gInk, L_inkX[7], gLight.gradeK);
    SetF(gInk, L_inkX[8], gLight.saturation); SetV3(gInk, L_inkX[9], C3(gLight.gradeLo)); SetV3(gInk, L_inkX[10], C3(gLight.gradeHi));
    static int rtView = getenv("DEPTH_RTVIEW") ? atoi(getenv("DEPTH_RTVIEW")) : 0;   // debug: 1 raw normal/depth, 2 raw colour
    if (rtView == 1) { DrawTexturePro(gNDRT.texture, {0, 0, (float)gNDRT.texture.width, -(float)gNDRT.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE); return; }
    if (rtView == 2) { DrawTexturePro(gColorRT.texture, {0, 0, (float)gColorRT.texture.width, -(float)gColorRT.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE); return; }
    // the quarter-resolution pass: the bloom over the colour, and the light shafts through the water
    float bloomK = gLight.silhouette > 0.5f ? 0.0f : gLight.bloom;
    int nShafts = gLight.water > 0 && gLight.silhouette < 0.5f ? std::min(gLight.nShafts, SceneLight::MAX_SHAFTS) : 0;
    bool quarter = bloomK > 0 || nShafts > 0;
    if (quarter) {
        if (gBloomRT.texture.width != std::max(1, vw / 4)) {
            if (gBloomRT.id) UnloadRenderTexture(gBloomRT);
            gBloomRT = LoadRenderTexture(std::max(1, vw / 4), std::max(1, vh / 4));
            SetTextureFilter(gBloomRT.texture, TEXTURE_FILTER_BILINEAR);
        }
        static int lu[16]; static bool got = false;
        static const char* N[16] = {"uTexel", "uBloomK", "uWaterC", "uTanHalf", "uAspect", "uTime", "uFarD", "uCamP", "uCamF", "uCamR", "uCamU", "uShA", "uShB", "uShC", "uShN", "uND"};
        if (!got) { for (int k = 0; k < 16; k++) lu[k] = GetShaderLocation(gBloomSh, N[k]); got = true; }
        Vector2 tx{2.0f / vw, 2.0f / vh};
        SetShaderValue(gBloomSh, lu[0], &tx, SHADER_UNIFORM_VEC2);
        SetF(gBloomSh, lu[1], bloomK); SetF(gBloomSh, lu[2], nShafts > 0 ? 1.0f : 0.0f);
        SetF(gBloomSh, lu[3], tanf(gCam.fovy * DEG2RAD * 0.5f)); SetF(gBloomSh, lu[4], (float)SCREEN_W / SCREEN_H);
        SetF(gBloomSh, lu[5], gLight.time); SetF(gBloomSh, lu[6], FAR);
        Vector3 F = Vector3Normalize(Vector3Subtract(gCam.target, gCam.position));
        Vector3 R = Vector3Normalize(Vector3CrossProduct(F, gCam.up)), U = Vector3CrossProduct(R, F);
        SetV3(gBloomSh, lu[7], gCam.position); SetV3(gBloomSh, lu[8], F); SetV3(gBloomSh, lu[9], R); SetV3(gBloomSh, lu[10], U);
        float a[32] = {}, b[32] = {}, c[32] = {};
        for (int i = 0; i < nShafts; i++) {
            const auto& s = gLight.shafts[i];
            Vector3 dn = Vector3Normalize(s.dir);
            a[i * 4] = s.top.x; a[i * 4 + 1] = s.top.y; a[i * 4 + 2] = s.top.z; a[i * 4 + 3] = std::max(0.1f, s.radius);
            b[i * 4] = dn.x; b[i * 4 + 1] = dn.y; b[i * 4 + 2] = dn.z; b[i * 4 + 3] = std::max(0.5f, s.length);
            c[i * 4] = s.c.r / 255.0f; c[i * 4 + 1] = s.c.g / 255.0f; c[i * 4 + 2] = s.c.b / 255.0f; c[i * 4 + 3] = s.k;
        }
        if (lu[11] >= 0) SetShaderValueV(gBloomSh, lu[11], a, SHADER_UNIFORM_VEC4, SceneLight::MAX_SHAFTS);
        if (lu[12] >= 0) SetShaderValueV(gBloomSh, lu[12], b, SHADER_UNIFORM_VEC4, SceneLight::MAX_SHAFTS);
        if (lu[13] >= 0) SetShaderValueV(gBloomSh, lu[13], c, SHADER_UNIFORM_VEC4, SceneLight::MAX_SHAFTS);
        SetI(gBloomSh, lu[14], nShafts);
        BeginLayer(gBloomRT);
        ClearBackground(BLACK);
        BeginShaderMode(gBloomSh);
        if (lu[15] >= 0) SetShaderValueTexture(gBloomSh, lu[15], gNDRT.texture);
        DrawTexturePro(gColorRT.texture, {0, 0, (float)gColorRT.texture.width, -(float)gColorRT.texture.height}, {0, 0, (float)gBloomRT.texture.width, (float)gBloomRT.texture.height}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndLayer();
    }
    {   // the water's composite: the lens, the quarter pass added, the ink's fade
        static int lu[8]; static bool got = false;
        static const char* N[6] = {"uWaterC", "uLens", "uBloomK", "uInkFade", "uBloomTex", "uBloomTexel"};
        if (!got) { for (int k = 0; k < 6; k++) lu[k] = GetShaderLocation(gInk, N[k]); got = true; }
        SetF(gInk, lu[0], nShafts > 0 ? 1.0f : 0.0f); SetF(gInk, lu[1], gLight.lens); SetF(gInk, lu[2], bloomK); SetF(gInk, lu[3], gLight.inkFade);
        Vector2 bt{quarter ? 1.2f / gBloomRT.texture.width : 0.0f, quarter ? 1.2f / gBloomRT.texture.height : 0.0f};
        SetShaderValue(gInk, lu[5], &bt, SHADER_UNIFORM_VEC2);
        gInkBloomLoc = lu[4];
    }
    BeginShaderMode(gInk);
    SetShaderValueTexture(gInk, L_ink[0], gNDRT.texture);
    if (quarter && gInkBloomLoc >= 0) SetShaderValueTexture(gInk, gInkBloomLoc, gBloomRT.texture);
    if (aoK > 0 && L_inkAOTex >= 0) {
        SetShaderValueTexture(gInk, L_inkAOTex, gAORT.texture);
        Vector2 tx{0.75f / gAORT.texture.width, 0.75f / gAORT.texture.height};
        SetShaderValue(gInk, L_inkAOTexel, &tx, SHADER_UNIFORM_VEC2);
    }
    DrawTexturePro(gColorRT.texture, {0, 0, (float)gColorRT.texture.width, -(float)gColorRT.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

} // namespace rt

namespace rt {
Body BodyOf(const std::string& artKey, const std::string& name) {
    const CreatureModel& cm = Creature(artKey, name);
    Body b;
    b.length = cm.length;
    b.radius = cm.radius;
    return b;
}
} // namespace rt
