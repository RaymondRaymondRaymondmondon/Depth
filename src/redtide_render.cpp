// Red Tide's 3D core: mesh building, the CreatureBuilder (every body plan in RedTide_Art_BodyPlans.xlsx) and the
// inked low-poly renderer. See redtide_render.h.
#include "redtide_render.h"
#include "game.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

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

static const char* RT_LIT_FS = R"(#version 330
in vec3 fragWorld; in vec4 fragColor; in float fragViewZ; in vec2 fragUV;
uniform vec4 colDiffuse;
uniform vec3 uCam, uLampPos, uLampDir, uKey, uFill, uRim, uFog;
uniform float uLampRange, uLampCone, uFogDensity, uSurfaceY, uTime, uGlow, uSil;
out vec4 finalColor;
void main() {
    vec3 n = normalize(cross(dFdx(fragWorld), dFdy(fragWorld)));
    vec3 V = normalize(uCam - fragWorld);
    if (dot(n, V) < 0.0) n = -n;
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    // key: the helmet lamp, a spotlight
    vec3 L = uLampPos - fragWorld; float d = length(L); L /= max(d, 0.0001);
    float cone = smoothstep(uLampCone, uLampCone + 0.18, dot(-L, normalize(uLampDir)));
    float att = clamp(1.0 - d / uLampRange, 0.0, 1.0); att *= att;
    float key = max(dot(n, L), 0.0) * cone * att;
    // fill: the sea's light from above; rim on the edge away from the key
    float fill = 0.35 + 0.35 * n.y;
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
    vec3 col = base * (uFill / 255.0 * fill * 2.6 + 0.08 + uKey / 255.0 * key * 1.6) + uRim / 255.0 * rim * 0.3 * (0.4 + key);
    // caustics near the surface
    float cz = clamp(1.0 - (uSurfaceY - fragWorld.y) / 18.0, 0.0, 1.0);
    float ca = sin(fragWorld.x * 1.3 + uTime * 1.1) * sin(fragWorld.z * 1.1 - uTime * 0.9) + sin((fragWorld.x + fragWorld.z) * 0.7 + uTime * 1.7);
    col += base * max(ca, 0.0) * 0.18 * cz * max(n.y, 0.0);
    col += base * uGlow;                                   // luminous species glow in the dark
    // fog by distance (thicker the deeper the scene sets it)
    float fog = 1.0 - exp(-uFogDensity * fragViewZ);
    col = mix(col, uFog / 255.0, clamp(fog, 0.0, 1.0));
    if (uSil > 0.5) col = vec3(0.0);
    finalColor = vec4(col, fragColor.a * colDiffuse.a);
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

// The ink composite: Sobel edges on depth and normals (line weight by distance), Bayer stipple in shadow, paper
// grain, a vignette, and the red at the mask's edge that grows with the local scent.
static const char* RT_INK_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D uND;
uniform vec2 uRes; uniform float uTime; uniform float uBlood; uniform float uSil; uniform vec3 uFog;
out vec4 finalColor;
float depthAt(vec2 uv) { vec4 t = texture(uND, uv); if (t.b == 0.0 && t.a == 0.0 && t.r == 0.0 && t.g == 0.0) return 1.0; return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0; }
vec3 normAt(vec2 uv) { vec4 t = texture(uND, uv); vec2 xy = t.rg * 2.0 - 1.0; return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy)))); }
float bayer(vec2 p) { int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0)); int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5); return float(m[y * 4 + x]) / 16.0; }
void main() {
    vec2 uv = fragTexCoord;
    vec3 col = texture(texture0, uv).rgb;
    float d = depthAt(uv);
    // line weight: thicker near, thinner far (1.8 px near to 0.8 px at the far plane)
    float wpx = mix(1.8, 0.8, clamp(d * 3.0, 0.0, 1.0));
    vec2 px = wpx / uRes;
    float dd = 0.0; vec3 nn = normAt(uv); float ne = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 o = (i == 0) ? vec2(px.x, 0) : (i == 1) ? vec2(-px.x, 0) : (i == 2) ? vec2(0, px.y) : vec2(0, -px.y);
        float d2 = depthAt(uv + o);
        dd += abs(d2 - d);
        ne += 1.0 - max(dot(nn, normAt(uv + o)), 0.0);
    }
    float edge = smoothstep(0.004 + d * 0.02, 0.012 + d * 0.05, dd) + smoothstep(0.35, 0.8, ne) * (1.0 - d);
    edge = clamp(edge, 0.0, 1.0);
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    // Bayer stipple in the shadows (only on geometry, not the open water)
    if (d < 0.999) {
        float th = bayer(gl_FragCoord.xy);
        float shade = smoothstep(0.07, 0.01, lum);
        if (th < shade * 0.7) col *= 0.45;
    }
    vec3 ink = vec3(0.05, 0.05, 0.07);
    col = mix(col, ink, edge * 0.9);
    // paper grain that never scrolls, and a vignette
    float g = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);
    col *= 0.94 + 0.06 * g;
    vec2 c = uv - 0.5;
    float vig = smoothstep(0.85, 0.3, length(c * vec2(1.25, 1.0)));
    col *= 0.72 + 0.28 * vig;
    // the red at the mask's edge (scent meter)
    col = mix(col, vec3(0.5, 0.02, 0.02), clamp(uBlood, 0.0, 1.0) * (1.0 - vig) * 0.8);
    if (uSil > 0.5) col = d < 0.999 ? vec3(0.0) : vec3(0.85, 0.82, 0.74);
    finalColor = vec4(col, 1.0);
}
)";

static Shader gInk{};
static RenderTexture2D gColorRT{}, gNDRT{};
static Model gCube{};
static int L_lit[16], L_nd[8], L_ink[8];
enum { LU_ANIM, LU_PHASE, LU_AMP, LU_WAVES, LU_LEN, LU_INTEN, LU_CAM, LU_LAMPPOS, LU_LAMPDIR, LU_KEY, LU_FILL, LU_RIM, LU_FOG, LU_RANGE, LU_CONE, LU_FOGD };
static int L_litSurf, L_litTime, L_litGlow, L_litSil;

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
    L_ink[0] = GetShaderLocation(gInk, "uND");
    L_ink[1] = GetShaderLocation(gInk, "uRes");
    L_ink[2] = GetShaderLocation(gInk, "uTime");
    L_ink[3] = GetShaderLocation(gInk, "uBlood");
    L_ink[4] = GetShaderLocation(gInk, "uSil");
    L_ink[5] = GetShaderLocation(gInk, "uFog");
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
    UnloadShader(gLit); UnloadShader(gND); UnloadShader(gInk);
    gShadersReady = false;
}

// ================================================================ the frame
struct DrawCmd {
    const Model* model;
    Matrix world;
    int anim; float phase, amp, waves, len, inten, glow;
    Color tint;
};
static std::vector<DrawCmd> gQueue;
static Camera3D gCam;
static SceneLight gLight;

static void SetV3(Shader s, int loc, Vector3 v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC3); }
static void SetF(Shader s, int loc, float v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetI(Shader s, int loc, int v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }
static Vector3 C3(Color c) { return {(float)c.r, (float)c.g, (float)c.b}; }

void RenderBegin(const Camera3D& cam, const SceneLight& light) {
    EnsureShaders();
    gCam = cam;
    gLight = light;
    gQueue.clear();
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
void DrawWorldCube(Vector3 c, Vector3 size, Color col) {
    Matrix world = MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(c.x, c.y, c.z));
    gQueue.push_back({&gCube, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, col});
}

static void DrawQueue(Shader sh, bool lit) {
    for (const DrawCmd& d : gQueue) {
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
        if (lit) SetF(sh, L_litGlow, d.glow);
        DrawMesh(m.meshes[0], m.materials[0], d.world);
    }
}

void RenderEnd() {
    if (!gShadersReady) return;
    const float FAR = 90.0f;
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
    // the ink composite into the scene
    Vector2 res{(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(gInk, L_ink[1], &res, SHADER_UNIFORM_VEC2);
    SetF(gInk, L_ink[2], gLight.time);
    SetF(gInk, L_ink[3], gLight.bloodTint);
    SetF(gInk, L_ink[4], gLight.silhouette);
    SetV3(gInk, L_ink[5], C3(gLight.fog));
    static int rtView = getenv("DEPTH_RTVIEW") ? atoi(getenv("DEPTH_RTVIEW")) : 0;   // debug: 1 raw normal/depth, 2 raw colour
    if (rtView == 1) { DrawTexturePro(gNDRT.texture, {0, 0, (float)gNDRT.texture.width, -(float)gNDRT.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE); return; }
    if (rtView == 2) { DrawTexturePro(gColorRT.texture, {0, 0, (float)gColorRT.texture.width, -(float)gColorRT.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE); return; }
    BeginShaderMode(gInk);
    SetShaderValueTexture(gInk, L_ink[0], gNDRT.texture);
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
