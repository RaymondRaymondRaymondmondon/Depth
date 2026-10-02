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
uniform vec4 uPL[8]; uniform vec4 uPLC[8]; uniform int uPLN;   // point lights: xyz + radius; rgb (0..1) + strength
uniform int uSky;                                                // 1: the sky (stars, the moon, rain): unlit and unfogged
uniform sampler2D uShadowMap; uniform mat4 uLightVP; uniform int uHasShadow;
out vec4 finalColor;
float keyShadow(vec3 wp, vec3 n, vec3 L) {   // (the same as the physically based path's)
    if (uHasShadow == 0) return 1.0;
    vec4 lp = uLightVP * vec4(wp + n * 0.03, 1.0);
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x <= 0.0 || q.y <= 0.0 || q.x >= 1.0 || q.y >= 1.0 || q.z >= 1.0) return 1.0;
    float bias = 0.0015 + 0.004 * (1.0 - max(dot(n, L), 0.0));
    float lit = 0.0; vec2 tx = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) lit += (q.z - bias > texture(uShadowMap, q.xy + vec2(x, y) * tx * 1.5).r) ? 0.0 : 1.0;
    return lit / 9.0;
}
void main() {
    if (uSky == 1) { finalColor = vec4(fragColor.rgb * colDiffuse.rgb, fragColor.a * colDiffuse.a); return; }
    vec3 n = normalize(cross(dFdx(fragWorld), dFdy(fragWorld)));
    vec3 V = normalize(uCam - fragWorld);
    if (dot(n, V) < 0.0) n = -n;
    vec3 base = fragColor.rgb * colDiffuse.rgb;
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
    col += base * max(ca, 0.0) * 0.18 * cz * max(n.y, 0.0);
    for (int i = 0; i < 8; i++) {                          // point lights (the Trawl's lamps, fires, flares)
        if (i >= uPLN) break;
        vec3 Lp = uPL[i].xyz - fragWorld; float dp = length(Lp);
        float ap = clamp(1.0 - dp / uPL[i].w, 0.0, 1.0); ap *= ap;
        col += base * uPLC[i].rgb * uPLC[i].a * ap * (0.25 + 0.75 * max(dot(n, Lp / max(dp, 0.0001)), 0.0)) * 1.6;
    }
    col += base * uGlow;                                   // luminous species glow in the dark
    // fog by distance (thicker the deeper the scene sets it)
    float fog = 1.0 - exp(-uFogDensity * fragViewZ);
    col = mix(col, uFog / 255.0, clamp(fog, 0.0, 1.0));
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
static const char* RT_PBR_FS = R"(#version 330
in vec3 fragWorld; in vec3 fragNormal; in vec2 fragUV; in vec4 fragColor; in float fragViewZ;
uniform sampler2D texture0; uniform sampler2D uMR; uniform sampler2D uNrm; uniform sampler2D uAO; uniform sampler2D uEmit;
uniform int uHasAlb, uHasMR, uHasNrm, uHasAO, uHasEmit;
uniform vec4 colDiffuse; uniform float uMetal, uRough; uniform vec3 uEmitCol; uniform float uWrap, uGlow;
uniform vec3 uCam, uLampPos, uLampDir, uKey, uFog; uniform float uLampRange, uLampCone, uFogDensity;
uniform vec4 uPL[8]; uniform vec4 uPLC[8]; uniform int uPLN;
uniform vec3 uMoonDir, uMoon, uSkyAmb, uSeaAmb; uniform float uMoonK, uAmbK, uSil;
uniform sampler2D uShadowMap; uniform mat4 uLightVP; uniform int uHasShadow;
out vec4 finalColor;
// the lamp's shadow: a 3x3 filtered look-up in its depth map (1 lit, 0 in shadow); outside the map, lit
float keyShadow(vec3 wp, vec3 n, vec3 L) {
    if (uHasShadow == 0) return 1.0;
    vec4 lp = uLightVP * vec4(wp + n * 0.03, 1.0);
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x <= 0.0 || q.y <= 0.0 || q.x >= 1.0 || q.y >= 1.0 || q.z >= 1.0) return 1.0;
    float bias = 0.0015 + 0.004 * (1.0 - max(dot(n, L), 0.0));
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
    vec4 bc = colDiffuse * fragColor;
    albedo = bc.rgb;
    if (uHasAlb == 1) { vec4 tx = texture(texture0, fragUV); albedo *= toLin(tx.rgb); bc.a *= tx.a; }
    if (bc.a < 0.4) discard;
    metal = uMetal; rough = uRough;
    if (uHasMR == 1) { vec4 mr = texture(uMR, fragUV); rough *= mr.g; metal *= mr.b; }
    rough = clamp(rough, 0.04, 1.0);
    N = normalize(fragNormal);
    V = normalize(uCam - fragWorld);
    if (!gl_FrontFacing) N = -N;
    if (uHasNrm == 1) { vec3 tn = texture(uNrm, fragUV).xyz * 2.0 - 1.0; N = normalize(cotangentFrame(N, fragWorld, fragUV) * tn); }
    F0 = mix(vec3(0.04), albedo, metal);
    vec3 col = vec3(0.0);
    // the lamp: a spot with a soft cone and inverse-square-ish fall-off to its range
    { vec3 L = uLampPos - fragWorld; float d = length(L); L /= max(d, 1e-4);
      float cone = smoothstep(uLampCone, uLampCone + 0.18, dot(-L, normalize(uLampDir)));
      float att = clamp(1.0 - d / uLampRange, 0.0, 1.0); att *= att;
      col += shade(L, toLin(uKey / 255.0) * cone * att * 5.0 * keyShadow(fragWorld, N, L)); }
    // the moon
    col += shade(normalize(-uMoonDir), toLin(uMoon / 255.0) * uMoonK * 2.5);
    // the practical lights
    for (int i = 0; i < 8; i++) {
        if (i >= uPLN) break;
        vec3 L = uPL[i].xyz - fragWorld; float d = length(L); L /= max(d, 1e-4);
        float att = clamp(1.0 - d / uPL[i].w, 0.0, 1.0); att *= att;
        col += shade(L, toLin(uPLC[i].rgb) * uPLC[i].a * att * 6.0);
    }
    // hemisphere ambient, with the baked occlusion
    float ao = uHasAO == 1 ? texture(uAO, fragUV).r : 1.0;
    vec3 amb = mix(toLin(uSeaAmb / 255.0), toLin(uSkyAmb / 255.0), N.y * 0.5 + 0.5) * uAmbK;
    col += amb * albedo * (1.0 - metal * 0.7) * ao;
    col += F_Schlick(max(dot(N, V), 0.0), F0) * amb * ao * (1.0 - rough) * 0.8;   // a little of the sky in polished metal
    col *= mix(1.0, ao, 0.6);
    if (uHasEmit == 1) col += toLin(texture(uEmit, fragUV).rgb * uEmitCol);
    col += albedo * uGlow;
    col = pow(col, vec3(1.0 / 2.2));
    float fog = 1.0 - exp(-uFogDensity * fragViewZ);
    col = mix(col, uFog / 255.0, clamp(fog, 0.0, 1.0));
    if (uSil > 0.5) col = vec3(0.0);
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

// The ink composite: Sobel edges on depth and normals (line weight by distance), Bayer stipple in shadow, paper
// grain, a vignette, and the red at the mask's edge that grows with the local scent.
static const char* RT_INK_FS = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor;
uniform sampler2D texture0; uniform sampler2D uND;
uniform vec2 uRes; uniform float uTime; uniform float uBlood; uniform float uSil; uniform vec3 uFog;
uniform float uOutline, uStipple, uGrain; uniform vec3 uInkTint;
uniform float uAOK, uAORad, uFarD, uTanHalf, uAspect, uFilmic, uExposure, uGradeK, uSat; uniform vec3 uGradeLo, uGradeHi;
out vec4 finalColor;
float depthAt(vec2 uv) { vec4 t = texture(uND, uv); if (t.b == 0.0 && t.a == 0.0 && t.r == 0.0 && t.g == 0.0) return 1.0; return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0; }
vec3 normAt(vec2 uv) { vec4 t = texture(uND, uv); vec2 xy = t.rg * 2.0 - 1.0; return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy)))); }
float bayer(vec2 p) { int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0)); int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5); return float(m[y * 4 + x]) / 16.0; }
vec3 viewPos(vec2 uv, float dm) { return vec3((uv * 2.0 - 1.0) * vec2(uTanHalf * uAspect, uTanHalf) * dm, -dm); }
// screen-space ambient occlusion: eight taps on a disc in view space round the point, turned by a 4x4 pattern, each
// counting when the surface it lands on stands in front of it (and near enough to matter): the contact shadows
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
vec3 filmic(vec3 x) {   // a gentle ACES-style curve (Narkowicz), keeping the night's darks
    x *= uExposure;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec2 uv = fragTexCoord;
    vec3 col = texture(texture0, uv).rgb;
    float d = depthAt(uv);
    if (uAOK > 0.0) col *= mix(1.0, ssao(uv, d), uAOK);
    // line weight: thicker near, thinner far (1.8 px near to 0.8 px at the far plane); a thin outline (uOutline < 1)
    // is a 1 px line at most
    float wpx = mix(1.8, 0.8, clamp(d * 3.0, 0.0, 1.0)) * (uOutline < 0.99 ? 0.55 : 1.0);
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
    if (d < 0.999 && uStipple > 0.5) {
        float th = bayer(gl_FragCoord.xy);
        float shade = smoothstep(0.07, 0.01, lum);
        if (th < shade * 0.7) col *= 0.45;
    }
    vec3 ink = uInkTint / 255.0;
    col = mix(col, ink, edge * 0.9 * uOutline);
    // paper grain that never scrolls, and a vignette
    float g = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);
    col *= 1.0 - (0.06 - 0.06 * g) * uGrain;
    vec2 c = uv - 0.5;
    float vig = smoothstep(0.85, 0.3, length(c * vec2(1.25, 1.0)));
    col *= 0.72 + 0.28 * vig;
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

static Shader gInk{}, gPbr{}, gNDPbr{};
static int L_pbr[40], L_pbrSkinned = -1, L_ndPbrSkinned = -1, L_ndPbrFar = -1, L_ndPbrCam = -1;
static const char* PBR_U[] = {"uHasAlb", "uHasMR", "uHasNrm", "uHasAO", "uHasEmit", "uMetal", "uRough", "uEmitCol", "uWrap", "uGlow",
                              "uCam", "uLampPos", "uLampDir", "uKey", "uFog", "uLampRange", "uLampCone", "uFogDensity", "uPL", "uPLC",
                              "uPLN", "uMoonDir", "uMoon", "uSkyAmb", "uSeaAmb", "uMoonK", "uAmbK", "uSil"};
enum { PU_HASALB, PU_HASMR, PU_HASNRM, PU_HASAO, PU_HASEMIT, PU_METAL, PU_ROUGH, PU_EMITCOL, PU_WRAP, PU_GLOW,
       PU_CAM, PU_LAMPPOS, PU_LAMPDIR, PU_KEY, PU_FOG, PU_RANGE, PU_CONE, PU_FOGD, PU_PL, PU_PLC,
       PU_PLN, PU_MOONDIR, PU_MOON, PU_SKYAMB, PU_SEAAMB, PU_MOONK, PU_AMBK, PU_SIL, PU_COUNT };
static int L_inkOutline, L_inkStipple, L_inkGrain, L_inkTint;
static int L_inkX[16];
// the lamp's shadow map: a depth-only framebuffer (1024 square: this PC's integrated graphics is the target)
static RenderTexture2D gShadowRT{};
static const int SHADOW_SIZE = 1024, SHADOW_UNIT = 12;   // (texture unit 12: above the twelve material map slots)
static int L_litShadow[3], L_pbrShadow[3];               // the sampler, the light's view-projection, the switch
static bool EnsureShadowMap() {
    if (gShadowRT.id) return true;
    gShadowRT.id = rlLoadFramebuffer();
    if (!gShadowRT.id) return false;
    unsigned int dt = rlLoadTextureDepth(SHADOW_SIZE, SHADOW_SIZE, false);
    gShadowRT.texture = {dt, SHADOW_SIZE, SHADOW_SIZE, 1, 19};
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
    for (int i = 0; i < 3; i++) { L_litShadow[i] = GetShaderLocation(gLit, SH[i]); L_pbrShadow[i] = GetShaderLocation(gPbr, SH[i]); }
    gPbr = LoadShaderFromMemory(RT_PBR_VS, RT_PBR_FS);
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
    int boneOff = -1, boneN = 0;   // a skinned pose: its matrices in gBonePool
    int recOff = 0, recN = 0;      // recoloured materials in gRecPool
};
static std::vector<Matrix> gBonePool;
static std::vector<Recolor> gRecPool;
static std::map<const Model*, std::vector<std::string>> gMatNames;   // glTF material names, index = raylib material - 1
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
    gBonePool.clear(); gRecPool.clear();
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
void DrawPbrSkinned(const Model& m, Matrix world, const std::vector<Matrix>& skin, const std::vector<Recolor>& recolor, float wrap, Color tint) {
    DrawCmd d{&m, world, (int)AnimMode::Static, 0, 0, 0, 1, 0, 0, tint};
    d.pbr = 1; d.wrap = wrap;
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
    }
    if (!m) TraceLog(LOG_WARNING, "rt: asset %s not found", path.c_str());
    if (m && path.size() > 4 && path.substr(path.size() - 4) == ".glb") {
        // the material names from the glb's own JSON chunk (raylib keeps only their order: material i + 1)
        int sz = 0; unsigned char* data = LoadFileData(path.c_str(), &sz);
        if (data && sz > 20) {
            uint32_t jlen; memcpy(&jlen, data + 12, 4);
            if (20 + (int)jlen <= sz) {
                Json j; std::string err;
                if (ParseJson(std::string((const char*)data + 20, jlen), j, &err))
                    for (const Json& mt : j["materials"].a) gMatNames[m.get()].push_back(mt["name"].Str0());
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

static void DrawPbrCmd(const DrawCmd& d, Shader sh, bool lit) {
    Model& m = const_cast<Model&>(*d.model);
    Matrix world = MatrixMultiply(m.transform, d.world);
    if (lit) { SetF(gPbr, L_pbr[PU_WRAP], d.wrap); SetF(gPbr, L_pbr[PU_GLOW], d.glow); }
    auto names = gMatNames.find(d.model);
    for (int i = 0; i < m.meshCount; i++) {
        Material mat = m.materials[m.meshMaterial[i]];
        Shader keep = mat.shader;
        mat.shader = sh;
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
            Color e = mat.maps[MATERIAL_MAP_EMISSION].color;
            SetV3(gPbr, L_pbr[PU_EMITCOL], {e.r / 255.0f, e.g / 255.0f, e.b / 255.0f});
        }
        DrawMesh(m.meshes[i], mat, world);
        (void)keep;
    }
}

static void DrawQueue(Shader sh, bool lit) {
    for (const DrawCmd& d : gQueue) {
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
        if (lit) { SetF(sh, L_litGlow, d.glow); SetI(sh, L_litSky, d.sky); }
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
        DrawQueue(gND, false);
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
    // the ink composite into the scene
    Vector2 res{(float)SCREEN_W, (float)SCREEN_H};
    SetShaderValue(gInk, L_ink[1], &res, SHADER_UNIFORM_VEC2);
    SetF(gInk, L_ink[2], gLight.time);
    SetF(gInk, L_ink[3], gLight.bloodTint);
    SetF(gInk, L_ink[4], gLight.silhouette);
    SetV3(gInk, L_ink[5], C3(gLight.fog));
    SetF(gInk, L_inkOutline, gLight.outline); SetF(gInk, L_inkStipple, gLight.stipple); SetF(gInk, L_inkGrain, gLight.grain);
    SetV3(gInk, L_inkTint, C3(gLight.outlineTint));
    SetF(gInk, L_inkX[0], gLight.aoK); SetF(gInk, L_inkX[1], gLight.aoRadius); SetF(gInk, L_inkX[2], FAR);
    SetF(gInk, L_inkX[3], tanf(gCam.fovy * DEG2RAD * 0.5f)); SetF(gInk, L_inkX[4], (float)SCREEN_W / SCREEN_H);
    SetF(gInk, L_inkX[5], gLight.filmic); SetF(gInk, L_inkX[6], gLight.exposure); SetF(gInk, L_inkX[7], gLight.gradeK);
    SetF(gInk, L_inkX[8], gLight.saturation); SetV3(gInk, L_inkX[9], C3(gLight.gradeLo)); SetV3(gInk, L_inkX[10], C3(gLight.gradeHi));
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
