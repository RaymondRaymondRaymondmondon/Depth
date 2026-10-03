// The Flight, stage 3: the map (design doc pp. 13-20). Every island type's generator, the four arrangements with
// rotational fairness, the generated sea (Red Tide's ecosystem over the whole map, its far zones asleep as numbers),
// the fog of what your birds have seen, scouts and their reports. Headless.
#include "flight.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace fl {

const char* IsleTypeName(IsleType t) {
    static const char* N[] = {"Tropical Island", "Sea Stack", "Fishing Town", "Atoll", "Islet", "Kraken Cove", "Skull Island", "Volcano Island", "Reef Garden", "The Wreck",
                              "The Iceberg", "Lighthouse Rock", "Shipwreck Island", "The Mangrove", "The Kelp Raft", "The Cliff Town", "Iron Island", "The Whale", "Siren Rocks", "The Maelstrom", "The Ghost Ship", "Bird Island"};
    return N[std::clamp((int)t, 0, (int)IsleType::COUNT - 1)];
}
const char* ArrangementName(Arrangement a) {
    static const char* N[] = {"Archipelago", "Safe Distance", "Ring", "Chain"};
    return N[std::clamp((int)a, 0, (int)Arrangement::COUNT - 1)];
}
const char* AltName(Alt a) { return a == Alt::High ? "high" : a == Alt::Mid ? "mid" : "low"; }
float AltHeight(Alt a) { return a == Alt::High ? 120.0f : a == Alt::Mid ? 40.0f : 12.0f; }
float AltSight(Alt a) { return a == Alt::High ? 250.0f : a == Alt::Mid ? 120.0f : 60.0f; }

// ---------------------------------------------------------------- noise (the same as flight.cpp's)
namespace {
float H2(int x, int z, uint32_t s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f; }
float VN(float x, float z, uint32_t s) {
    int xi = (int)floorf(x), zi = (int)floorf(z); float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = H2(xi, zi, s), b = H2(xi + 1, zi, s), c = H2(xi, zi + 1, s), d = H2(xi + 1, zi + 1, s);
    return (a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz;
}
float Fbm(float x, float z, uint32_t s) { float t = 0, a = 1, n = 0; for (int o = 0; o < 4; o++) { t += VN(x, z, s + o * 17) * a; n += a; a *= 0.5f; x *= 2.03f; z *= 2.03f; } return t / n; }
float Sm(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
struct Rng { uint32_t s; float operator()() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; } };
}  // namespace

// ---------------------------------------------------------------- the islands
void Island::BuildOutline() {
    // the coast as the chart draws it: from the grid's edge inward, the first land on each of 72 bearings
    outline.clear();
    if (n == 0) return;
    float ext = (n - 1) * cell * 0.5f;
    for (int k = 0; k < 72; k++) {
        float a = k * 2 * PI / 72, r = 0;
        for (float d = ext; d > 0; d -= cell * 0.5f) if (Height(c.x + cosf(a) * d, c.z + sinf(a) * d) > 0.15f) { r = d; break; }
        outline.push_back({c.x + cosf(a) * r, c.z + sinf(a) * r});
    }
}
void Island::Generate(IsleType t, uint32_t s, Vector3 centre) {
    *this = Island{};
    type = t; seed = s; c = {centre.x, 0, centre.z};
    Rng R{s * 9781u + 13};
    auto grid = [&](float ext, float cl) { cell = cl; n = (int)(ext * 2 / cl) + 1; x0 = c.x - ext; z0 = c.z - ext; h.assign((size_t)n * n, -25); };
    auto each = [&](auto f) { for (int zi = 0; zi < n; zi++) for (int xi = 0; xi < n; xi++) { float lx = x0 + xi * cell - c.x, lz = z0 + zi * cell - c.z; h[(size_t)zi * n + xi] = f(lx, lz); } };
    auto addSite = [&](Vector3 p) { sites.push_back(p); };
    auto beachSpots = [&](int count, float lo, float hi, float minGap, std::vector<Vector3>& out, float rmax) {
        for (int k = 0, made = 0; k < 6000 && made < count; k++) {
            float a = R() * 2 * PI, r = R() * rmax;
            Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r};
            float y = Height(p.x, p.z);
            if (y < lo || y > hi) continue;
            bool close = false; for (const auto& q : out) if (Vector2Distance({q.x, q.z}, {p.x, p.z}) < minGap) close = true;
            if (close) continue;
            p.y = y; out.push_back(p); made++;
        }
    };
    switch (t) {
    case IsleType::Tropical: {
        Generate(s);   // (stage 1's island, at the origin)
        type = t; c = {centre.x, 0, centre.z};
        x0 += c.x; z0 += c.z;
        for (auto& p : palms) p = Vector3Add(p, c);
        hill = Vector3Add(hill, c); nest = Vector3Add(nest, c);
        radius = 88;
        // sites: the palm crowns nearest home (the nest first), and the beach
        std::vector<int> order(palms.size()); for (int i = 0; i < (int)order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return Vector3Distance(palms[a], nest) < Vector3Distance(palms[b], nest); });
        for (int k = 0; k < (int)order.size() && (int)sites.size() < 32; k++) { int i = order[k]; addSite({palms[i].x, palms[i].y + palmH[i] * 0.97f, palms[i].z}); }
        std::vector<Vector3> beach; beachSpots(8, 0.6f, 1.6f, 12, beach, 95);
        for (auto& b : beach) addSite(b);
        for (const auto& p : palms) twigPts.push_back({Vector3{p.x + 0.8f, 0, p.z}, -1.0f});
        std::vector<Vector3> drift; beachSpots(12, 0.3f, 1.6f, 8, drift, 100);
        for (size_t k = 0; k < drift.size(); k++) { if (k % 2) shellPts.push_back(drift[k]); else twigPts.push_back({drift[k], 2.0f}); }
        for (auto& tp : twigPts) tp.first.y = Height(tp.first.x, tp.first.z) + 0.2f;
    } break;
    case IsleType::Stack: {
        // a terraced column 120 m tall: its ledges are the nest sites (all on cliffs); deep water all round
        grid(80, 2);
        static const float tops[6] = {120, 100, 80, 55, 30, 8}, rad[6] = {20, 23.5f, 27, 30.5f, 34, 37.5f};
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3 + 10, sinf(a) * 3, s) - 0.5f) * 5;
            for (int k = 0; k < 6; k++) if (r < rad[k]) return tops[k] - (k > 0 ? 0.0f : 0.0f);
            return -6 - 19 * Sm(38, 78, r);
        });
        radius = 38; hill = {c.x, 120, c.z};
        for (int k = 1; k <= 4; k++) for (int j = 0; j < 5; j++) {
            float a = (j + 0.5f * k) * 2 * PI / 5 + R() * 0.3f, r = (rad[k - 1] + rad[k]) * 0.5f;
            Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r}; p.y = Height(p.x, p.z);
            addSite(p);
        }
        nest = sites[0];
        for (int j = 0; j < 12; j++) {   // driftwood and shells on the lowest ledge (no trees: the stack's twigs are what the sea brings)
            float a = j * 2 * PI / 12 + 0.2f, r = 36; Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r}; p.y = Height(p.x, p.z) + 0.2f;
            if (j % 3 == 2) shellPts.push_back(p); else twigPts.push_back({p, 3.0f});
        }
    } break;
    case IsleType::Town: {
        // a low island with a harbour to the south: houses, a church tower, docks and boats; a woodpile by the quay
        grid(140, 2);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 2 + 5, sinf(a) * 2, s) - 0.5f) * 14;
            float y = r < 70 ? 2.5f + 1.5f * Fbm(lx * 0.03f, lz * 0.03f, s + 3) : -1.5f - 23.5f * Sm(70, 135, r);
            if (fabsf(lx) < 32 && lz > 28) y = std::min(y, -4.5f);   // (the harbour)
            return y;
        });
        radius = 75; hill = {c.x - 20, 0, c.z - 25};
        Vector3 tower{c.x - 20, 0, c.z - 25};
        props.push_back({{tower.x, 12, tower.z}, {4, 12, 4}, 2}); props.push_back({{tower.x, 27, tower.z}, {2.2f, 3, 2.2f}, 1});
        for (int k = 0, made = 0; k < 400 && made < 14; k++) {
            float a = R() * 2 * PI, r = 14 + R() * 44;
            Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r};
            if (fabsf(p.x - c.x) < 38 && p.z - c.z > 20) continue;
            if (Vector2Distance({p.x, p.z}, {tower.x, tower.z}) < 12) continue;
            bool close = false; for (const auto& q : props) if (Vector2Distance({q.c.x, q.c.z}, {p.x, p.z}) < 13) close = true;
            if (close || Height(p.x, p.z) < 1.5f) continue;
            float gy = Height(p.x, p.z), yaw = R() * PI;
            props.push_back({{p.x, gy + 2.5f, p.z}, {4, 2.5f, 5}, 0, yaw});
            props.push_back({{p.x, gy + 5.6f, p.z}, {4.6f, 0.7f, 5.6f}, 1, yaw});
            made++;
        }
        for (int k = -1; k <= 1; k++) props.push_back({{c.x + k * 18.0f, 0.5f, c.z + 50}, {2, 0.3f, 16}, 3});
        for (int k = 0; k < 3; k++) props.push_back({{c.x - 18 + k * 18.0f + 6, 0.5f, c.z + 58 - k * 4.0f}, {1.6f, 0.7f, 4.5f}, 4});
        props.push_back({{c.x + 26, 3.6f, c.z + 20}, {3, 1.0f, 2}, 5});
        // the houses, the tower and the docks stand in the heightmap (a bird lands on a roof or the quay)
        for (const auto& pr : props) {
            if (pr.kind == 4) continue;
            float top = pr.c.y + pr.half.y;
            for (int zi = 0; zi < n; zi++) for (int xi = 0; xi < n; xi++) {
                float x = x0 + xi * cell, z = z0 + zi * cell;
                float dx = x - pr.c.x, dz = z - pr.c.z, ca = cosf(pr.yaw), sa = sinf(pr.yaw);
                float u = dx * ca + dz * sa, v = -dx * sa + dz * ca;
                if (fabsf(u) <= pr.half.x && fabsf(v) <= pr.half.z) h[(size_t)zi * n + xi] = std::max(h[(size_t)zi * n + xi], top);
            }
        }
        for (const auto& pr : props) if (pr.kind == 1) addSite({pr.c.x, pr.c.y + pr.half.y, pr.c.z});   // roofs and the tower
        for (int j = 0; j < 4; j++) addSite({tower.x + (j % 2 ? 4.5f : -4.5f), 18, tower.z + (j < 2 ? 4.5f : -4.5f)});   // the tower's ledges
        for (const auto& pr : props) if (pr.kind == 4 || pr.kind == 3) if ((int)sites.size() < 25) addSite({pr.c.x, pr.c.y + pr.half.y + 0.2f, pr.kind == 3 ? pr.c.z + pr.half.z - 1 : pr.c.z});
        nest = sites[0];
        twigPts.push_back({{c.x + 26, 4.8f, c.z + 20}, 30.0f});   // the woodpile
        std::vector<Vector3> drift; beachSpots(10, 0.3f, 1.6f, 10, drift, 85);
        for (size_t k = 0; k < drift.size(); k++) { if (k % 2) shellPts.push_back(drift[k]); else twigPts.push_back({drift[k], 2.0f}); }
    } break;
    case IsleType::Atoll: {
        // a ring of sand round a lagoon, a channel through it to the east; a few palms
        grid(120, 2);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz);
            float d = fabsf(r - 66) - (Fbm(cosf(a) * 3, sinf(a) * 3 + 7, s) - 0.5f) * 6;
            float y = d < 7 ? 1.4f + 1.0f * Fbm(lx * 0.08f, lz * 0.08f, s + 2) : r < 66 ? -4.5f + 1.5f * Fbm(lx * 0.05f, lz * 0.05f, s + 4) : -2 - 23 * Sm(74, 118, r);
            if (fabsf(a) < 0.12f && r > 52 && r < 82) y = std::min(y, -3.0f);   // (the channel)
            return y;
        });
        radius = 74; hill = c;
        for (int j = 0; j < 15; j++) { float a = 0.3f + j * 2 * PI / 15; Vector3 p{c.x + cosf(a) * 66, 0, c.z + sinf(a) * 66}; p.y = std::max(0.5f, Height(p.x, p.z)); addSite(p); }
        nest = sites[0];
        for (int j = 0; j < 12; j++) { float a = 0.5f + j * 2 * PI / 12 + R() * 0.2f; Vector3 p{c.x + cosf(a) * 66, 0, c.z + sinf(a) * 66}; p.y = Height(p.x, p.z); if (p.y > 0.6f) { palms.push_back(p); palmH.push_back(5 + 3 * R()); } }
        for (const auto& p : palms) twigPts.push_back({Vector3{p.x + 0.8f, p.y + 0.2f, p.z}, -1.0f});
        std::vector<Vector3> drift; beachSpots(12, 0.4f, 2.0f, 9, drift, 80);
        for (size_t k = 0; k < drift.size(); k++) { if (k % 2) twigPts.push_back({drift[k], 2.0f}); else shellPts.push_back(drift[k]); }
    } break;
    case IsleType::Islet: {
        grid(45, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.1f, lz * 0.1f, s) - 0.5f) * 6; return r < 16 ? 6 * (1 - r / 16) + 0.5f : -2 - 23 * Sm(16, 44, r); });
        radius = 16; hill = {c.x, 6, c.z};
        for (int j = 0; j < 4; j++) { float a = j * PI / 2 + 0.4f; Vector3 p{c.x + cosf(a) * 6, 0, c.z + sinf(a) * 6}; p.y = Height(p.x, p.z); addSite(p); }
        nest = sites[0];
        twigPts.push_back({{c.x + 12, Height(c.x + 12, c.z) + 0.2f, c.z}, 2.0f});
    } break;
    case IsleType::KrakenCove: {
        // a crescent of cliffs round a deep cove that opens to the south
        grid(160, 3);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 2, sinf(a) * 2 + 3, s) - 0.5f) * 10;
            bool gap = fabsf(atan2f(lz, lx) - PI * 0.5f) < 0.55f;
            if (r < 55 || (gap && r < 95)) return -30.0f;
            if (r < 90) return 35 * Sm(55, 62, r) * (1 - Sm(84, 90, r)) + 1;
            return -6 - 19 * Sm(90, 150, r);
        });
        radius = 90; hill = {c.x, 36, c.z - 72};
        for (int j = 0; j < 12; j++) { float a = -PI * 0.5f + (j - 5.5f) * 0.38f; Vector3 p{c.x + cosf(a) * 72, 0, c.z + sinf(a) * 72}; p.y = Height(p.x, p.z); if (p.y > 20) addSite(p); }
        nest = sites.empty() ? hill : sites[0];
    } break;
    case IsleType::Skull: {
        // a big jungle island with a skull-faced mountain in its middle
        grid(230, 3);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3, sinf(a) * 3 + 11, s) - 0.5f) * 30;
            if (r >= 140) return -2 - 23 * Sm(140, 225, r);
            float m = 70 * expf(-(lx * lx + lz * lz) / (2 * 55.0f * 55.0f));
            float eyes = 12 * (expf(-((lx - 18) * (lx - 18) + (lz - 40) * (lz - 40)) / 120.0f) + expf(-((lx + 18) * (lx + 18) + (lz - 40) * (lz - 40)) / 120.0f));
            return 3 + m - eyes + 4 * Fbm(lx * 0.04f, lz * 0.04f, s) - 3 * Sm(120, 140, r);
        });
        radius = 140; hill = {c.x, 70, c.z};
        for (int k = 0; k < 3000 && palms.size() < 90; k++) {
            float x = c.x + (R() * 2 - 1) * 135, z = c.z + (R() * 2 - 1) * 135, y = Height(x, z);
            if (y < 1.5f || y > 45) continue;
            bool close = false; for (const auto& p : palms) if ((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < 64) close = true;
            if (close) continue;
            palms.push_back({x, y, z}); palmH.push_back(10 + 6 * R());   // (the tallest trees on the map)
        }
        for (size_t i = 0; i < palms.size() && sites.size() < 30; i += 3) addSite({palms[i].x, palms[i].y + palmH[i] * 0.97f, palms[i].z});
        nest = sites.empty() ? hill : sites[0];
        for (const auto& p : palms) twigPts.push_back({Vector3{p.x + 0.8f, p.y + 0.2f, p.z}, -1.0f});
    } break;
    case IsleType::Volcano: {
        // a cone with a crater lake at its top
        grid(190, 3);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3, sinf(a) * 3 + 21, s) - 0.5f) * 14;
            if (r >= 110) return -2 - 23 * Sm(110, 185, r);
            float y = 62 * powf(1 - r / 110, 0.8f) + 1;
            if (r < 20) y = 46 + 4 * Sm(14, 20, r);
            return y;
        });
        radius = 110; hill = {c.x, 63, c.z};
        for (int j = 0; j < 12; j++) { float a = j * 2 * PI / 12; Vector3 p{c.x + cosf(a) * 70, 0, c.z + sinf(a) * 70}; p.y = Height(p.x, p.z); addSite(p); }
        nest = sites[0];
    } break;
    case IsleType::ReefGarden: {
        // a coral flat just under the water, one sand cay
        grid(160, 3);
        each([&](float lx, float lz) {
            float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.02f, lz * 0.02f, s) - 0.5f) * 20;
            if (r < 12) return 1.4f;
            if (r < 100) return -1.6f - 2.0f * Fbm(lx * 0.07f, lz * 0.07f, s + 1);
            return -4 - 21 * Sm(100, 155, r);
        });
        radius = 100; hill = {c.x, 1.4f, c.z};
        for (int j = 0; j < 4; j++) { float a = j * PI / 2; Vector3 p{c.x + cosf(a) * 5, 1.4f, c.z + sinf(a) * 5}; addSite(p); }
        nest = sites[0];
        for (int j = 0; j < 6; j++) shellPts.push_back({c.x + cosf(j * 1.05f) * 9, 1.5f, c.z + sinf(j * 1.05f) * 9});
    } break;
    case IsleType::Wreck: {
        // a drifting hulk (it drifts and sinks in stage 7): its deck is the only land
        grid(30, 1);
        props.push_back({{c.x, 1.0f, c.z}, {6, 2.5f, 22}, 6, 0.2f});
        props.push_back({{c.x, 11, c.z - 6}, {0.4f, 9, 0.4f}, 7}); props.push_back({{c.x, 9, c.z + 9}, {0.35f, 7, 0.35f}, 7});
        each([&](float lx, float lz) { float ca = cosf(0.2f), sa = sinf(0.2f); float u = lx * ca + lz * sa, v = -lx * sa + lz * ca; return fabsf(u) < 6 && fabsf(v) < 22 ? 3.5f : -25.0f; });
        radius = 22; hill = {c.x, 3.5f, c.z};
        for (int j = 0; j < 4; j++) addSite({c.x + sinf(0.2f) * (-12 + j * 8.0f), 3.6f, c.z + cosf(0.2f) * (-12 + j * 8.0f)});
        nest = sites[0];
        twigPts.push_back({{c.x, 3.7f, c.z + 4}, 12.0f});   // (its rigging)
    } break;
    default: GenerateMore(t, s); break;   // (the expansion's islands: flight_isles.cpp)
    }
    if (hill.y == 0 && n > 0) hill.y = Height(hill.x, hill.z);
    BuildOutline();
}

// ---------------------------------------------------------------- the layout (doc p14: arrangements, fairness)
std::vector<IsleSpec> LayoutMap(const MapOpts& o) {
    std::vector<IsleSpec> v;
    int N = std::clamp(o.players, 2, 6);
    Rng R{o.seed * 2654435761u + 7};
    static const IsleType starts[4] = {IsleType::Tropical, IsleType::Stack, IsleType::Town, IsleType::Atoll};
    static const IsleType danger[3] = {IsleType::Skull, IsleType::Volcano, IsleType::ReefGarden};
    // the long match (the expansion, doc pp. 43-45): ten starting islands and eleven dangerous ones
    static const IsleType startsL[10] = {IsleType::Tropical, IsleType::Stack, IsleType::Town, IsleType::Atoll, IsleType::Iceberg, IsleType::Lighthouse, IsleType::Shipwreck, IsleType::Mangrove, IsleType::KelpRaft, IsleType::CliffTown};
    static const IsleType dangerL[8] = {IsleType::Skull, IsleType::IronIsland, IsleType::Volcano, IsleType::Whale, IsleType::ReefGarden, IsleType::SirenRocks, IsleType::Maelstrom, IsleType::BirdIsland};
    bool L = o.seasons > 0; uint32_t dOff = (o.seed * 7u) % 8u;
    auto dangerAt = [&](int i) { return L ? dangerL[(i + dOff) % 8] : danger[i % 3]; };
    auto startType = [&](int i) { return i == 0 ? o.home : L ? startsL[(int)(R() * 10) % 10] : starts[(int)(R() * 4) % 4]; };
    auto add = [&](IsleType t, float x, float z, int start, const std::string& name) { IsleSpec s; s.type = t; s.c = {x, 0, z}; s.start = start; s.name = name; v.push_back(s); };
    auto startName = [&](int i) { return i == 0 ? std::string("Your island") : TextFormat("Rival %d's island", i); };
    float th0 = PI * 0.5f;   // (slot 0 due south of the middle)
    if (o.arr == Arrangement::Chain) {
        float d = 520;
        for (int i = 0; i < N; i++) add(startType(i), i * d, 0, i, startName(i));
        for (int i = 0; i + 1 < N; i++) {
            add(dangerAt(i), (i + 0.5f) * d, 330, -1, IsleTypeName(dangerAt(i)));
            add(IsleType::Town, (i + 0.5f) * d, -330, -1, "Fishing Town");
            add(IsleType::Islet, (i + 0.5f) * d + (R() - 0.5f) * 40, (R() - 0.5f) * 60, -1, "Islet");
        }
        add(IsleType::KrakenCove, (N - 1) * d * 0.5f, 760, -1, "Kraken Cove");
        add(IsleType::Wreck, (N - 1) * d * 0.5f, -620, -1, "The Wreck");
        if (L) add(IsleType::GhostShip, (N - 1) * d * 0.5f + 300, 560, -1, "The Ghost Ship");
    } else {
        float d = o.arr == Arrangement::SafeDistance ? 900 + 300 * R() : o.arr == Arrangement::Ring ? 480 : 350 + 150 * R();
        float Rr = std::max(d / (2 * sinf(PI / N)), o.arr == Arrangement::SafeDistance ? 700.0f : 560.0f);
        for (int i = 0; i < N; i++) { float a = th0 + i * 2 * PI / N; add(startType(i), cosf(a) * Rr, sinf(a) * Rr, i, startName(i)); }
        add(IsleType::KrakenCove, 0, 0, -1, "Kraken Cove");
        for (int i = 0; i < N; i++) {
            float a = th0 + (i + 0.5f) * 2 * PI / N;
            if (o.arr != Arrangement::Ring) add(dangerAt(i), cosf(a) * Rr * 0.55f, sinf(a) * Rr * 0.55f, -1, IsleTypeName(dangerAt(i)));
            else add(IsleType::ReefGarden, cosf(a) * Rr * 0.6f, sinf(a) * Rr * 0.6f, -1, "Reef Garden");
            add(IsleType::Town, cosf(a) * Rr * 1.3f, sinf(a) * Rr * 1.3f, -1, "Fishing Town");
            float ai = th0 + (i + 0.5f) * 2 * PI / N;
            add(IsleType::Islet, cosf(ai) * Rr * 0.92f, sinf(ai) * Rr * 0.92f, -1, "Islet");
        }
        float aw = th0 + PI + (N % 2 ? PI / N : 0.0f);   // (opposite your island, in a gap of the dangerous ring)
        add(IsleType::Wreck, cosf(aw) * Rr * 0.3f, sinf(aw) * Rr * 0.3f, -1, "The Wreck");
        if (L) add(IsleType::GhostShip, cosf(aw + PI) * Rr * 1.05f, sinf(aw + PI) * Rr * 1.05f, -1, "The Ghost Ship");
    }
    // number the neutral names; slot 0 to the origin
    Vector3 o0 = v[0].c;
    int counts[(int)IsleType::COUNT] = {};
    for (auto& s : v) {
        s.c = Vector3Subtract(s.c, o0);
        if (s.start < 0 && s.type != IsleType::KrakenCove && s.type != IsleType::Wreck && s.type != IsleType::GhostShip) s.name += " " + std::to_string(++counts[(int)s.type]);
    }
    return v;
}

// ---------------------------------------------------------------- the sea over the whole map
namespace {
struct ZoneT { const char* tag; Rectangle r; float depth; };
struct SpawnT { const char* species; int count; };
void ZonesFor(IsleType t, std::vector<ZoneT>& out) {
    auto ring = [&](float a, float b, float depth) {
        out.push_back({"North water", {-b, -b, 2 * b, b - a}, depth});
        out.push_back({"South water", {-b, a, 2 * b, b - a}, depth});
        out.push_back({"West water", {-b, -a, b - a, 2 * a}, depth});
        out.push_back({"East water", {a, -a, b - a, 2 * a}, depth});
    };
    switch (t) {
    case IsleType::Tropical:
        out.push_back({"The Lagoon", {-40, 50, 80, 45}, -3}); out.push_back({"Reef Shallows", {-150, 95, 300, 45}, -6});
        out.push_back({"West Shelf", {-230, -140, 120, 235}, -14}); out.push_back({"East Shelf", {110, -140, 120, 235}, -14});
        out.push_back({"North Shelf", {-110, -230, 220, 120}, -14}); out.push_back({"The Drop-off", {-260, 140, 520, 80}, -35});
        break;
    case IsleType::Stack: ring(40, 150, -40); break;
    case IsleType::Town: out.push_back({"Harbour", {-32, 28, 64, 50}, -5}); ring(80, 190, -14); break;
    case IsleType::Atoll: out.push_back({"Atoll Lagoon", {-56, -56, 112, 112}, -4.5f}); ring(76, 180, -12); break;
    case IsleType::Islet: ring(20, 80, -10); break;
    case IsleType::KrakenCove: out.push_back({"The Cove", {-52, -52, 104, 104}, -30}); ring(95, 200, -30); break;
    case IsleType::Skull: ring(145, 260, -14); break;
    case IsleType::Volcano: ring(112, 220, -20); break;
    case IsleType::ReefGarden: out.push_back({"The Garden", {-98, -98, 196, 196}, -4}); ring(102, 220, -30); break;
    case IsleType::Wreck: out.push_back({"Wreck water", {-80, -80, 160, 160}, -30}); break;
    case IsleType::Iceberg: out.push_back({"Under the berg", {-50, -50, 100, 100}, -30}); ring(50, 150, -40); break;
    case IsleType::Lighthouse: ring(34, 140, -16); break;
    case IsleType::Shipwreck: out.push_back({"The reef", {-60, -40, 120, 80}, -5}); ring(60, 160, -14); break;
    case IsleType::Mangrove: out.push_back({"Under the roots", {-64, -64, 128, 128}, -2.5f}); ring(74, 170, -10); break;
    case IsleType::KelpRaft: out.push_back({"Under the raft", {-50, -50, 100, 100}, -12}); ring(54, 150, -20); break;
    case IsleType::CliffTown: out.push_back({"Harbour", {-26, 30, 52, 44}, -5}); ring(76, 180, -14); break;
    case IsleType::IronIsland: ring(66, 170, -14); break;
    case IsleType::Whale: out.push_back({"The wake", {-110, -40, 220, 80}, -20}); break;
    case IsleType::SirenRocks: ring(28, 120, -18); break;
    case IsleType::Maelstrom: out.push_back({"The edge of the whirl", {-125, -125, 250, 250}, -30}); break;
    case IsleType::GhostShip: break;   // (it drifts)
    case IsleType::BirdIsland: ring(62, 170, -14); break;
    default: break;
    }
}
std::vector<SpawnT> SpawnsFor(IsleType t, const std::string& tag) {
    bool ring = tag.find(" water") != std::string::npos;
    switch (t) {
    case IsleType::Tropical:
        if (tag == "The Lagoon") return {{"Anchovy", 36}, {"Mullet", 10}, {"Crab", 8}};
        if (tag == "Reef Shallows") return {{"Snapper", 10}, {"Cleaner Wrasse", 4}, {"Green Turtle", 1}, {"Blacktip", 2}, {"Reef Shark", 1}};
        if (tag == "West Shelf" || tag == "East Shelf") return {{"Mackerel", 36}};
        if (tag == "North Shelf") return {{"Sardine", 60}, {"Snapper", 12}};
        if (tag == "The Drop-off") return {{"Squid", 20}, {"Barracuda", 4}, {"Sardine", 60}, {"Reef Shark", 1}};
        break;
    case IsleType::Stack: return {{"Mackerel", 32}, {"Sardine", 50}, {"Squid", 12}, {"Tuna", 3}};   // (deep water all round: the guano feeds it; no shallows, so it fishes at the rises)
    case IsleType::Town:
        if (tag == "Harbour") return {{"Mullet", 14}, {"Sardine", 30}, {"Crab", 10}};
        return {{"Mackerel", 16}, {"Sardine", 20}};
    case IsleType::Atoll:
        if (tag == "Atoll Lagoon") return {{"Snapper", 12}, {"Mullet", 10}, {"Anchovy", 30}, {"Cleaner Wrasse", 4}};
        return {{"Snapper", 6}, {"Mackerel", 12}, {"Blacktip", tag == "South water" || tag == "North water" ? 1 : 0}};
    case IsleType::Islet: return {{"Sardine", 12}};
    case IsleType::KrakenCove:
        if (tag == "The Cove") return {{"Squid", 40}, {"Tuna", 6}, {"Mackerel", 30}};   // (the richest water; the kraken is stage 7)
        return {{"Mackerel", 20}, {"Sardine", 30}};
    case IsleType::Skull: return {{"Mackerel", 18}, {"Sardine", 24}, {"Barracuda", 1}};
    case IsleType::Volcano: return {{"Sardine", 20}, {"Squid", 6}};
    case IsleType::ReefGarden:
        if (tag == "The Garden") return {{"Snapper", 16}, {"Cleaner Wrasse", 6}, {"Anchovy", 30}, {"Crab", 6}};
        return {{"Reef Shark", 1}, {"Barracuda", 2}, {"Mackerel", 12}};
    case IsleType::Wreck: return {{"Sardine", 20}, {"Crab", 6}};
    case IsleType::Iceberg: if (tag == "Under the berg") return {{"Squid", 20}, {"Sardine", 40}, {"Tuna", 3}}; return {{"Mackerel", 18}, {"Squid", 8}};   // (deep fish and krill under it)
    case IsleType::Lighthouse: return {{"Mackerel", 20}, {"Sardine", 30}, {"Snapper", 6}};
    case IsleType::Shipwreck: if (tag == "The reef") return {{"Snapper", 12}, {"Crab", 8}, {"Anchovy", 24}}; return {{"Mackerel", 14}, {"Sardine", 20}};
    case IsleType::Mangrove: if (tag == "Under the roots") return {{"Crab", 24}, {"Mullet", 14}, {"Anchovy", 30}}; return {{"Mackerel", 12}, {"Sardine", 18}};   // (crabs and shellfish; the crocodiles keep sharks out)
    case IsleType::KelpRaft: if (tag == "Under the raft") return {{"Sardine", 50}, {"Snapper", 14}, {"Anchovy", 30}}; return {{"Mackerel", 16}};   // (fish shelter under it)
    case IsleType::CliffTown: if (tag == "Harbour") return {{"Mullet", 14}, {"Sardine", 26}, {"Crab", 8}}; return {{"Mackerel", 16}, {"Sardine", 20}};
    case IsleType::IronIsland: return {{"Mackerel", 18}, {"Sardine", 20}};
    case IsleType::Whale: return {{"Anchovy", 60}, {"Sardine", 40}};   // (krill in its wake: endless size-1 feed)
    case IsleType::SirenRocks: return {{"Snapper", 10}, {"Mackerel", 14}, {"Barracuda", 1}};
    case IsleType::Maelstrom: return {{"Squid", 16}, {"Mackerel", 20}, {"Tuna", 2}};
    case IsleType::BirdIsland: return {{"Sardine", 30}, {"Mackerel", 20}};
    default: break;
    }
    (void)ring;
    return {};
}
bool Touch(const Rectangle& a, const Rectangle& b, float pad) {
    return a.x - pad <= b.x + b.width && b.x - pad <= a.x + a.width && a.y - pad <= b.y + b.height && b.y - pad <= a.y + a.height;
}
float RectDist(const Rectangle& r, Vector3 p) {
    float dx = std::max({r.x - p.x, 0.0f, p.x - (r.x + r.width)}), dz = std::max({r.y - p.z, 0.0f, p.z - (r.y + r.height)});
    return sqrtf(dx * dx + dz * dz);
}
bool Catchable(const rt::Species& s) { return !s.isDiver && !s.isEnemy && s.tier < 4 && s.size <= 4 && !s.Has("protected"); }
}  // namespace

float World::HeightAt(float x, float z) const {
    if (!wholeMap) return island.Height(x, z);
    float best = -60;
    for (const auto& is : isles) if (is.Covers(x, z)) best = std::max(best, is.Height(x, z));
    return best;
}
Vector3 World::NormalAt(float x, float z) const {
    float e = 2;
    float hx = HeightAt(x + e, z) - HeightAt(x - e, z), hz = HeightAt(x, z + e) - HeightAt(x, z - e);
    return Vector3Normalize({-hx, 2 * e, -hz});
}
int World::IsleAt(float x, float z, float pad) const {
    for (int i = 0; i < (int)isles.size(); i++) if (Vector2Distance({x, z}, {isles[i].c.x, isles[i].c.z}) < isles[i].radius + pad) return i;
    return -1;
}

void World::Init(const std::string& founderKey, uint32_t seed, const MapOpts& o) {
    // stage 1's setup first (the founder, the wind, the Founder's body), then the whole map over it
    Init(founderKey, seed);
    opts = o; opts.seed = seed;
    wholeMap = true;
    auto specs = LayoutMap(opts);
    isles.clear();
    for (size_t i = 0; i < specs.size(); i++) {
        Island is; is.Generate(specs[i].type, seed * 31u + (uint32_t)i * 977u, specs[i].c);
        is.name = specs[i].name; is.start = specs[i].start;
        isles.push_back(std::move(is));
    }
    home = 0;
    island = isles[home];
    // the sea: Red Tide's species and food web, the zones laid round every island and an open-sea grid round them all
    const rt::MapData& base = rt::Map(seaKey);
    sea = std::make_unique<rt::MapData>();
    rt::MapData& m = *sea;
    m.key = base.key; m.title = "The Flight's sea"; m.species = base.species; m.attacks = base.attacks; m.diet = base.diet; m.foodNames = base.foodNames;
    m.tunables = base.tunables; m.tides = base.tides; m.extra = base.extra; m.faction = base.faction; m.enemySpecies = base.enemySpecies;
    std::vector<std::pair<int, std::string>> zoneIsle;   // (island, tag) per zone
    zoneDay.clear();
    for (int i = 0; i < (int)isles.size(); i++) {
        std::vector<ZoneT> zs; ZonesFor(isles[i].type, zs);
        for (const auto& zt : zs) {
            rt::Zone z; z.name = isles[i].name + ": " + zt.tag;
            z.plan = {isles[i].c.x + zt.r.x, isles[i].c.z + zt.r.y, zt.r.width, zt.r.height};
            z.y0 = zt.depth; z.y1 = 0;
            m.zones.push_back(z); zoneIsle.push_back({i, zt.tag});
            zoneDay.push_back(isles[i].type == IsleType::Stack ? -2.6f : -3.8f);   // (round a stack the deep water wells up: shoals sit higher by day)
            for (const auto& sp : SpawnsFor(isles[i].type, zt.tag)) if (sp.count > 0) { rt::SpawnRow r; r.zone = z.name; r.species = sp.species; r.count = sp.count; r.respawnS = 0; m.spawns.push_back(r); }
        }
    }
    float bx0 = 1e9f, bz0 = 1e9f, bx1 = -1e9f, bz1 = -1e9f;
    for (const auto& z : m.zones) { bx0 = std::min(bx0, z.plan.x); bz0 = std::min(bz0, z.plan.y); bx1 = std::max(bx1, z.plan.x + z.plan.width); bz1 = std::max(bz1, z.plan.y + z.plan.height); }
    bx0 -= 200; bz0 -= 200; bx1 += 200; bz1 += 200;
    const float OC = 500;
    int ox = (int)ceilf((bx1 - bx0) / OC), oz = (int)ceilf((bz1 - bz0) / OC);
    for (int j = 0; j < oz; j++) for (int i = 0; i < ox; i++) {
        rt::Zone z; z.name = TextFormat("Open sea %c%d", 'A' + i, j + 1);
        z.plan = {bx0 + i * OC, bz0 + j * OC, OC, OC}; z.y0 = -60; z.y1 = 0;
        m.zones.push_back(z); zoneIsle.push_back({-1, "open"}); zoneDay.push_back(-3.8f);
        auto row = [&](const char* sp, int n) { rt::SpawnRow r; r.zone = z.name; r.species = sp; r.count = n; r.respawnS = 0; m.spawns.push_back(r); };
        row("Sardine", 30); row("Flying Fish", 10);
        if ((i + j) % 2 == 0) row("Tuna", 2);
        if ((i + 2 * j) % 3 == 0) row("Mahi", 1);
        if ((i + j) % 4 == 1) row("Reef Shark", 1);
    }
    // passages: every pair of zones that touch or overlap
    for (int a = 0; a < (int)m.zones.size(); a++) for (int b = a + 1; b < (int)m.zones.size(); b++)
        if (Touch(m.zones[a].plan, m.zones[b].plan, 0.5f)) { rt::Link l; l.from = a; l.to = b; l.passage = "open water"; m.links.push_back(l); }
    rt::RebuildMapGeometry(m);
    // the ecosystem starts empty (the zones wake as birds come near; StepMap) - the spawn rows are each ground's capacity
    std::vector<int> keep(m.spawns.size()); for (size_t i = 0; i < m.spawns.size(); i++) { keep[i] = m.spawns[i].count; m.spawns[i].count = 0; }
    eco = rt::Ecosystem{};
    eco.Init(m, seed ? seed : 1, 1, 1);
    for (size_t i = 0; i < m.spawns.size(); i++) m.spawns[i].count = keep[i];
    me.agent = eco.AddDiver(0, {0, 50, 0});
    if (me.agent >= 0) eco.agents[me.agent].alive = false;
    HookDiverHits();
    multi = o.multi; matchLen = o.minutes * 60; human = (o.humanMask & 1) != 0;
    if (multi) timeScale = 1;
    // the colony on its island, the grounds' stocks (asleep, full), the live zones, the fog
    stocks.clear();
    InitColony();
    for (auto& s : stocks) s.pop = s.K;
    liveZone.assign(m.zones.size(), 0); zoneNearT.assign(m.zones.size(), -1e9f);
    inshoreZone = -1;
    for (int z = 0; z < (int)m.zones.size(); z++) if (zoneIsle[z].first == home && inshoreZone < 0) inshoreZone = z;
    lagoonZone = inshoreZone;
    know = Knowledge{};
    know.cell = 25; know.x0 = bx0; know.z0 = bz0;
    know.nx = (int)ceilf((bx1 - bx0) / know.cell); know.nz = (int)ceilf((bz1 - bz0) / know.cell);
    know.seen.assign((size_t)know.nx * know.nz, -1);
    know.isle.assign(isles.size(), 0); know.sight.assign(isles.size(), Sighting{});
    know.ground.assign(m.zones.size(), GroundInfo{});
    Knowledge blank = know;                     // (every side starts knowing only its own island)
    know.isle[home] = 2;
    // the other starting islands (stage 4): each holds a colony run by the same colony code (its own Founder, a random
    // species, a livery) swapped in to step; a bot's, or (a networked match) a person's
    rivals.clear(); sides.clear();
    static const Color LIV[5] = {{206, 64, 56, 255}, {64, 112, 210, 255}, {70, 170, 90, 255}, {200, 130, 40, 255}, {150, 80, 180, 255}};
    Rng RR{seed * 7477u + 5};
    for (int i = 0; i < (int)isles.size(); i++) {
        if (isles[i].start <= 0) continue;
        Side sd; sd.slot = isles[i].start; sd.name = TextFormat("Rival %d", isles[i].start); sd.livery = LIV[(isles[i].start - 1) % 5];
        sides.push_back(std::move(sd));
        int k = (int)sides.size() - 1;
        SwapSide(k);
        island = isles[i]; home = i;
        me = Founder{}; me.def = (int)(RR() * Founders().size()) % (int)Founders().size();
        me.st = FState::Perched; me.pos = island.nest; me.hunger = 1; me.stamina = Def().stamina; me.hp = Def().hp;
        me.strikeLen = Def().key == "taloned" ? 1.0f : 0.5f;
        human = ((o.humanMask >> (k + 1)) & 1) != 0;
        founderBot = !human; fb = Bird{};
        know = blank; know.isle[home] = 2; log.clear();
        if (human) { me.hunger = 0.8f; me.yaw = PI * 0.5f; me.agent = eco.AddDiver(k + 1, {0, 50, 0}); if (me.agent >= 0) eco.agents[me.agent].alive = false; }
        InitColony();
        col.side = k + 1;
        inshoreZone = -1; for (int z = 0; z < (int)zoneIsle.size(); z++) if (zoneIsle[z].first == i && inshoreZone < 0) inshoreZone = z;
        lagoonZone = inshoreZone;
        if (human) { Reveal(me.pos, 120, home); Say(TextFormat("%s, %s, %d islands. Fill your nest's courtship bowl (three fish) to call a mate.", ArrangementName(opts.arr), IsleTypeName(island.type), (int)isles.size())); }
        SwapSide(k);
    }
    me.st = FState::Perched; me.pos = island.nest;
    InitTowns();
    InitDanger();
    seasons = o.seasons; InitSeasons();   // (the long match: seasons and their events)
    InitIsles();
    InitRelics();
    InitFactions();
    truceUntil.assign((sides.size() + 1) * (sides.size() + 1), -1);
    Reveal(me.pos, 120, home);
    StepMap(0);
    Say(TextFormat("%s, %s, %d islands. Fill your nest's courtship bowl (three fish) to call a mate.", ArrangementName(opts.arr), IsleTypeName(island.type), (int)isles.size()));
}

int World::HomeOf(int side) const { return side == cur ? home : side == 0 ? sides[cur - 1].home : side > 0 && side <= (int)sides.size() ? sides[side - 1].home : -1; }
int World::OwnerOf(int isle) const { for (int s = 0; s <= (int)sides.size(); s++) if (HomeOf(s) == isle) return s; return -1; }
bool World::HumanOf(int side) const { return side == cur ? human : side == 0 ? sides[cur - 1].human : side > 0 && side <= (int)sides.size() ? sides[side - 1].human : false; }
bool World::BotFlown(int side) const { return side == cur ? founderBot : side == 0 ? sides[cur - 1].founderBot : side > 0 && side <= (int)sides.size() ? sides[side - 1].founderBot : true; }

// ---------------------------------------------------------------- the fog and the sightings
bool Knowledge::Seen(float x, float z) const { return SeenAt(x, z) >= 0; }
float Knowledge::SeenAt(float x, float z) const {
    int i = (int)((x - x0) / cell), j = (int)((z - z0) / cell);
    if (i < 0 || j < 0 || i >= nx || j >= nz) return -1;
    return seen[(size_t)j * nx + i];
}
void World::Reveal(Vector3 p, float radius, int landedIsle) {
    if (!wholeMap || know.nx == 0) return;
    int i0 = std::max(0, (int)((p.x - radius - know.x0) / know.cell)), i1 = std::min(know.nx - 1, (int)((p.x + radius - know.x0) / know.cell));
    int j0 = std::max(0, (int)((p.z - radius - know.z0) / know.cell)), j1 = std::min(know.nz - 1, (int)((p.z + radius - know.z0) / know.cell));
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        float cx = know.x0 + (i + 0.5f) * know.cell, cz = know.z0 + (j + 0.5f) * know.cell;
        if ((cx - p.x) * (cx - p.x) + (cz - p.z) * (cz - p.z) <= radius * radius) know.seen[(size_t)j * know.nx + i] = time;
    }
    // an island flown over is known as a silhouette; one landed on, in full (a guest's mirror only lifts the fog: the
    // host keeps the rest of its knowledge and sends it)
    if (mirror) return;
    for (int k = 0; k < (int)isles.size(); k++) {
        if (Vector2Distance({p.x, p.z}, {isles[k].c.x, isles[k].c.z}) < radius + isles[k].radius && know.isle[k] < 1) {
            know.isle[k] = 1;
            know.log.push_back({time, 1, "Sighted: " + isles[k].name + " (" + IsleTypeName(isles[k].type) + ")", isles[k].c});
        }
    }
    if (landedIsle >= 0 && landedIsle < (int)isles.size() && know.isle[landedIsle] < 2) {
        know.isle[landedIsle] = 2;
        know.log.push_back({time, 1, "Landed on " + isles[landedIsle].name + ": charted in full", isles[landedIsle].c});
    }
}

// What a bird sees of an island from a height (doc p19): high sees far but counts nests to within 30%; mid and low
// count them exactly (mid is seen by Watchers, low is a target: stage 4).
static Sighting Observe(World& w, int isle, Alt alt, Rng& R) {
    Sighting s; s.t = w.time; s.alt = (int)alt; s.scouts = 1;
    int nests = 0, caches = 0, birds = 0;
    // whose island it is (whichever colony is swapped in, the others wait in their sides)
    for (int s = 0; s <= (int)w.sides.size(); s++) {
        int h = s == w.cur ? w.home : s == 0 ? w.sides[w.cur - 1].home : w.sides[s - 1].home;
        if (h != isle) continue;
        if (w.DecreeOf(s).hidden) continue;   // (a Fog Bank: the island shows nothing)
        Colony& C = w.ColOf(s);
        for (const auto& n : C.nests) nests += n.built && n.style != NS_BURROW;   // (a burrow is hidden from scouts)
        caches = (int)C.caches.size();
        birds = 1; for (const auto& b : C.birds) birds += b.alive && b.stage != BStage::Egg;
    }
    if (alt == Alt::High) {
        auto off = [&](int v) { if (v == 0) return 0; float k = 1 + (R() * 2 - 1) * 0.3f; int o = (int)roundf(v * k); return o == v ? (R() < 0.5f ? std::max(0, v - 1) : v + 1) : std::max(0, o); };
        s.nests = off(nests); s.caches = caches; s.birds = off(birds); s.exact = false;
    } else { s.nests = nests; s.caches = caches; s.birds = birds; s.exact = true; }
    return s;
}
Sighting World::TrueSighting(int isle) { Rng R{1}; return Observe(*this, isle, Alt::Mid, R); }
static GroundInfo ObserveGround(World& w, int zone) {
    GroundInfo g; g.t = w.time; g.stock = w.StockOf(zone);
    int preds = 0;
    for (const auto& s : w.stocks) if (s.zone == zone) { const rt::Species& sp = w.eco.map->species[s.sp]; if (sp.tier >= 3 && !Catchable(sp)) preds += (int)roundf(w.liveZone[zone] ? 0 : s.pop); }
    if (w.liveZone[zone]) for (const auto& a : w.eco.agents) if (a.alive && a.zone == zone && w.eco.map->species[a.sp].tier >= 3 && !Catchable(w.eco.map->species[a.sp])) preds++;
    g.predators = std::min(1.0f, preds / 3.0f);
    for (const auto& b : w.col.birds) if (b.alive && b.role == Role::Fisher && b.stage == BStage::Adult && w.eco.ZoneAt({b.goal.x, -1, b.goal.z}) == zone) g.fishers++;
    return g;
}
static const char* YieldWord(float stock) { return stock > 0.75f ? "abundant" : stock > 0.45f ? "fair" : stock > 0.15f ? "thin" : "fished out"; }

bool World::SendScout(int isle, int zone, Vector3 at, Alt alt) {
    for (auto& b : col.birds) {
        if (!b.alive || b.stage != BStage::Adult || b.role != Role::Scout || b.retrainT > 0 || b.hasOrder) continue;
        b.hasOrder = true; b.observed = false; b.scoutIsle = isle; b.scoutZone = zone; b.alt = alt; b.task = Task::Fly; b.taskT = 0;
        b.scoutAt = isle >= 0 ? isles[isle].c : at;
        Say(TextFormat("A scout flies %s to %s.", AltName(alt), isle >= 0 ? isles[isle].name.c_str() : zone >= 0 ? eco.map->zones[zone].name.c_str() : "the mark"));
        return true;
    }
    return false;
}
void World::ScoutReport(Bird& b) {
    float age = time - b.obsT;
    std::string ago = age > DAY * 0.1f ? TextFormat(" (seen %.1f days ago)", age / DAY) : "";
    if (b.scoutIsle >= 0) {
        Sighting s = b.obs;
        Sighting& k = know.sight[b.scoutIsle];
        // two scouts on the same target within half a day cross-check: the counts become exact
        if (k.t >= 0 && fabsf(k.t - s.t) < DAY * 0.5f && (!k.exact || !s.exact) && k.scouts >= 1) {
            Rng R{1}; Sighting truth = Observe(*this, b.scoutIsle, Alt::Mid, R);
            s.nests = truth.nests; s.caches = truth.caches; s.birds = truth.birds; s.exact = true; s.scouts = k.scouts + 1;
        }
        k = s;
        if (know.isle[b.scoutIsle] < 1) know.isle[b.scoutIsle] = 1;
        const Island& is = isles[b.scoutIsle];
        int owner = OwnerOf(b.scoutIsle);
        std::string who = owner == cur ? "your colony" : owner >= 0 ? SideName(owner) + "'s colony" : "no colony";
        know.log.push_back({time, 1, TextFormat("%s (%s, from %s): %s, %s%d nests, %d caches, %s%d birds%s%s", is.name.c_str(), IsleTypeName(is.type), AltName((Alt)s.alt), who.c_str(),
                                                s.exact ? "" : "about ", s.nests, s.caches, s.exact ? "" : "about ", s.birds, s.scouts > 1 ? " (cross-checked)" : "", ago.c_str()), is.c});
    } else if (b.scoutZone >= 0) {
        know.ground[b.scoutZone] = b.gobs;
        const GroundInfo& g = b.gobs;
        know.log.push_back({time, 0, TextFormat("%s: %s, sharks %s%s%s", eco.map->zones[b.scoutZone].name.c_str(), YieldWord(g.stock), g.predators > 0.6f ? "many" : g.predators > 0.2f ? "some" : "few",
                                                g.fishers ? TextFormat(", %d of your fishers on it", g.fishers) : "", ago.c_str()), eco.map->zones[b.scoutZone].Center()});
    }
    Say("A scout reports: " + know.log.back().text);
}

// ---------------------------------------------------------------- the map's step: the fog, the scouts, the waking sea
int SpawnFishSlot(rt::Ecosystem& eco, int sp, Vector3 pos, int zone) {
    // (reuse a dead slot: the web only ever appends, and zones wake and sleep all match long)
    int i = eco.Spawn(sp, pos, zone);
    for (int k = 0; k + 1 < (int)eco.agents.size(); k++) {
        rt::Agent& d = eco.agents[k];
        if (!d.alive && d.diver < 0 && d.held <= 0) { d = eco.agents[i]; eco.agents.pop_back(); return k; }
    }
    return i;
}
// the fog round the side in the fields: the Founder sees farther the higher it flies; every colony bird sees round it;
// a scout by its height (a person's side only: a bot's knowledge isn't kept)
void World::StepFog(float dt) {
    if (!wholeMap || know.nx == 0) return;
    if (!fogNow && dt != 0) return;
    if (me.st != FState::Dead) {
        int landed = me.st == FState::Perched ? IsleAt(me.pos.x, me.pos.z, 10) : -1;
        Reveal(me.pos, std::clamp(45 + 1.6f * me.pos.y, 45.0f, 300.0f) * (FogNow() ? FogSight() : 1.0f) * BendNow().scout * (HasRelic(cur, RL_LENS) ? Relics()[RL_LENS].scout : 1.0f), mirror ? -1 : landed);   // (the Lamp Lens)
    }
    for (const auto& b : col.birds) if (b.alive && (b.stage == BStage::Adult || b.stage == BStage::Mate) && Vector3Length(b.vel) > 0.5f)
        Reveal(b.pos, (b.role == Role::Scout && b.hasOrder ? AltSight(b.alt) * BendNow().scout : 45) * (FogNow() ? FogSight() : 1.0f));
}
void World::StepMap(float dt) {
    if (!wholeMap) return;
    liveT += dt;
    if (human) StepFog(dt);
    // the sea wakes where birds are (within 300 m) and sleeps where none have been for 20 s
    if (liveT >= 1 || dt == 0) {
        liveT = 0;
        std::vector<Vector3> pts;
        for (int s = 0; s <= (int)sides.size(); s++) {   // (every side's Founder and fishers, and every flock: a fight wakes the water under it)
            const Founder& F = FounderOf(s);
            if (F.st != FState::Dead && (s == 0 || HumanOf(s) || !BotFlown(s))) pts.push_back(F.pos);
            if (s != cur) for (const auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && b.role == Role::Fisher && HumanOf(s)) pts.push_back(b.task == Task::Search || b.task == Task::Dive ? b.pos : b.goal);
            for (const auto& fl : ColOf(s).flocks) if (!fl.members.empty()) pts.push_back(fl.pos);
        }
        for (const auto& b : col.birds) if (b.alive && b.stage == BStage::Adult && (b.role == Role::Fisher || b.role == Role::Scout)) pts.push_back(b.task == Task::Search || b.task == Task::Dive || b.role == Role::Scout ? b.pos : b.goal);
        const rt::MapData& m = *eco.map;
        for (int z = 0; z < (int)m.zones.size(); z++) {
            bool near = false;
            for (const auto& p : pts) if (RectDist(m.zones[z].plan, p) < 300) { near = true; break; }
            if (near) zoneNearT[z] = time;
            if (near && !liveZone[z]) {
                liveZone[z] = 1;
                for (auto& s : stocks) if (s.zone == z) {
                    int n = (int)roundf(s.pop);
                    const rt::Zone& Z = m.zones[z];
                    Vector3 c{Z.plan.x + Z.plan.width * (0.2f + 0.6f * Rand()), (Z.y0 + Z.y1) * 0.5f, Z.plan.y + Z.plan.height * (0.2f + 0.6f * Rand())};
                    const rt::Species& sp = m.species[s.sp];
                    bool school = sp.social == "school" || sp.social == "pack";
                    for (int k = 0; k < n; k++) {
                        Vector3 p = school ? Vector3Add(c, {Rand() * 8 - 4, Rand() * 2 - 1, Rand() * 8 - 4}) : Vector3{Z.plan.x + Z.plan.width * Rand(), Z.y0 * (0.2f + 0.6f * Rand()), Z.plan.y + Z.plan.height * Rand()};
                        int ai = SpawnFishSlot(eco, s.sp, Z.Clamp(p), z);
                        eco.agents[ai].homeZone = z;
                    }
                    s.births = 0;
                }
            } else if (!near && liveZone[z] && time - zoneNearT[z] > 20) {
                liveZone[z] = 0;
                for (auto& s : stocks) if (s.zone == z) {
                    int n = 0;
                    for (auto& a : eco.agents) if (a.alive && a.diver < 0 && a.sp == s.sp && a.homeZone == z) { n++; a.alive = false; }
                    s.pop = (float)n;
                }
            }
        }
    }
}

// A scout's turn (doc p19): out to the target at its height, six seconds looking round it, then home; within 60 m of
// a nest, a cache or the Founder it reports. With no order it waits at the first cache.
void World::ScoutStep(Bird& b, float dt) {
    const RoleDef& R = RoleOf(Role::Scout);
    if (b.hunger < 0.3f && !b.hasOrder && BirdEatsAtCache(b, dt)) return;
    if (!b.hasOrder || (Walled(home, cur) && !b.observed && Vector3Distance(b.pos, col.caches[0].pos) < 30)) { b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {1.5f, 0.4f, -1.5f}), R.speed, dt, 0.4f); return; }   // (a hostile Wall over the island: the scouts can't get out)
    float y = AltHeight(b.alt);
    if (!b.observed) {
        Vector3 at{b.scoutAt.x, std::max(y, HeightAt(b.scoutAt.x, b.scoutAt.z) + 15), b.scoutAt.z};
        if (b.task == Task::Fly) { if (MoveTo(b, at, R.speed, dt, 8)) { b.task = Task::Search; b.taskT = 0; } return; }
        // circling the target, looking
        b.taskT += dt * Econ().workPace;
        float a = b.taskT * 0.6f;
        float r = b.scoutIsle >= 0 ? isles[b.scoutIsle].radius * 0.8f + 20 : 40;
        MoveTo(b, {at.x + cosf(a) * r, at.y, at.z + sinf(a) * r}, R.speed * 0.7f, dt, 1);
        if (b.taskT > 6) {
            Rng rr{(uint32_t)(b.id * 7919 + (int)time * 13 + 1)};
            bool exact = DecreeNow().scoutsExact || Elders(cur, VT_KEEN) > 0;   // (Scouts Aloft; a Keen elder)
            if (b.scoutIsle >= 0) { b.obs = Observe(*this, b.scoutIsle, exact ? Alt::Mid : b.alt, rr); if (exact) b.obs.alt = (int)b.alt;   // (Scouts Aloft: exact)
                                    if (DecreeNow().scoutsBlind) { b.obs.nests = b.obs.caches = b.obs.birds = 0; b.obs.exact = false; } }   // (your own Fog Bank: your scouts see nothing)
            if (b.scoutZone >= 0) b.gobs = ObserveGround(*this, b.scoutZone);
            b.obsT = time; b.observed = true; b.task = Task::Deliver;
        }
        return;
    }
    // home with it: the report is made within 60 m of a nest, a cache, or the Founder
    Vector3 homeAt = Vector3Add(col.caches[0].pos, {0, 6, 0});
    float best = Vector3Distance(b.pos, homeAt);
    if (me.st != FState::Dead && Vector3Distance(b.pos, me.pos) < best) { best = Vector3Distance(b.pos, me.pos); homeAt = me.pos; }
    auto flat = [&](Vector3 a) { return Vector2Distance({b.pos.x, b.pos.z}, {a.x, a.z}); };
    float rr = 60 * (BuiltOf(col, ST_LOOKOUT) ? LookoutReport() : 1.0f);   // (a Lookout: twice as far)
    bool near = me.st != FState::Dead && flat(me.pos) < rr;
    for (const auto& n : col.nests) if (flat(n.pos) < rr) near = true;
    for (const auto& c : col.caches) if (flat(c.pos) < rr) near = true;
    if (near) { ScoutReport(b); b.hasOrder = false; b.task = Task::Idle; return; }
    Vector3 cruise{homeAt.x, flat(homeAt) < 150 ? homeAt.y + 10 : std::max(y, homeAt.y + 10), homeAt.z};   // (down over home to report)
    MoveTo(b, cruise, R.speed, dt, 4);
}

// ---------------------------------------------------------------- --flight-fair [seed]
int RunFlightFairTest(int argc, char** argv) {
    int fails = 0, cases = 0;
    uint32_t seed0 = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    printf("The Flight: map fairness (doc p14: equal distance to the nearest dangerous island, town and neighbour)\n");
    for (uint32_t seed = seed0; seed < seed0 + 5; seed++)
    for (int a = 0; a < (int)Arrangement::COUNT; a++)
    for (int n = 2; n <= 6; n++) {
        MapOpts o; o.arr = (Arrangement)a; o.players = n; o.seed = seed; o.home = (IsleType)(seed % 4);
        auto v = LayoutMap(o);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {0, 0, 0};
        for (const auto& s : v) {
            if (s.start < 0) continue;
            float d[3] = {1e9f, 1e9f, 1e9f};
            for (const auto& t : v) {
                if (&t == &s) continue;
                float dist = Vector3Distance(s.c, t.c);
                if (IsDangerous(t.type) && !IsDrifting(t.type)) d[0] = std::min(d[0], dist);   // (the wreck drifts)
                if (t.type == IsleType::Town && t.start < 0) d[1] = std::min(d[1], dist);
                if (t.start >= 0) d[2] = std::min(d[2], dist);
            }
            for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], d[k]); hi[k] = std::max(hi[k], d[k]); }
        }
        // nothing overlaps: every pair of islands clear of each other's land (and a sea between)
        float closest = 1e9f;
        for (size_t i = 0; i < v.size(); i++) for (size_t j = i + 1; j < v.size(); j++) {
            static const float RAD[] = {88, 38, 75, 74, 16, 90, 140, 110, 100, 22};
            float gap = Vector3Distance(v[i].c, v[j].c) - RAD[(int)v[i].type] - RAD[(int)v[j].type];
            if (v[i].type == IsleType::Wreck || v[j].type == IsleType::Wreck) gap += 30;   // (the wreck drifts: it only has to clear the land)
            closest = std::min(closest, gap);
        }
        bool ok = true;
        for (int k = 0; k < 3; k++) if (hi[k] > lo[k] * 1.02f + 0.5f) ok = false;
        if (closest < 40) ok = false;
        cases++;
        if (!ok) { fails++; printf("  FAIL  seed %u %s %d players: dangerous %.0f-%.0f, town %.0f-%.0f, neighbour %.0f-%.0f, closest coasts %.0f m\n", seed, ArrangementName(o.arr), n, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], closest); }
        else if (seed == seed0) printf("  ok    %-13s %d players: %2d islands, nearest dangerous %.0f m, town %.0f m, neighbour %.0f m, closest coasts %.0f m\n", ArrangementName(o.arr), n, (int)v.size(), lo[0], lo[1], lo[2], closest);
    }
    printf(fails ? "flight-fair: %d of %d maps unfair\n" : "flight-fair: all %d maps fair\n", fails ? fails : cases, cases);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --flight-scout-test (the stage-3 gate)
int RunFlightScoutTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 3: the map, the fog, scouts and reports\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    MapOpts o; o.players = 4; o.arr = Arrangement::Archipelago; o.home = IsleType::Tropical;
    auto fresh = [&](World& w, int scouts) {
        w.Init("taloned", 3, o);
        w.ape.isle = -1; w.kraken.isle = -1;   // (scouting alone: the dangerous islands have their own test)
        for (int k = 0; k < scouts; k++) { Bird b; b.id = w.col.nextId++; b.stage = BStage::Adult; b.role = Role::Scout; b.pos = w.col.caches[0].pos; b.hunger = 1; w.col.birds.push_back(b); }
        w.me.hunger = 1;
    };
    World w; fresh(w, 0);
    int starts = 0, danger = 0, towns = 0; for (const auto& is : w.isles) { starts += is.start >= 0; danger += IsDangerous(is.type); towns += is.type == IsleType::Town && is.start < 0; }
    check(starts == 4 && danger >= 5 && towns == 4 && Vector3Length(w.island.c) < 0.01f, TextFormat("an archipelago of %d islands: 4 starting islands (yours at the origin), %d dangerous, %d towns", (int)w.isles.size(), danger, towns));
    bool types = true; for (int t = 0; t < (int)IsleType::COUNT; t++) { Island is; is.Generate((IsleType)t, 5, {0, 0, 0}); if (is.sites.empty() || is.outline.size() != 72) types = false; }
    { Island a; a.Generate(IsleType::Stack, 5, {}); Island b; b.Generate(IsleType::Town, 5, {}); Island c; c.Generate(IsleType::Atoll, 5, {}); Island d; d.Generate(IsleType::Tropical, 5, {});
      check(types && a.sites.size() == 20 && b.sites.size() == 25 && c.sites.size() == 15 && d.sites.size() == 40 && a.hill.y > 100 && c.Height(c.c.x, c.c.z) < -2,
            TextFormat("every island type generates; nest sites: stack %d, town %d, atoll %d, tropical %d (doc: 20, 25, 15, 40); the stack stands %.0f m", (int)a.sites.size(), (int)b.sites.size(), (int)c.sites.size(), (int)d.sites.size(), a.hill.y)); }
    int live = 0; for (auto z : w.liveZone) live += z;
    int alive = 0; for (const auto& a : w.eco.agents) alive += a.alive && a.diver < 0;
    check(live > 0 && live < (int)w.liveZone.size() / 2 && alive < 1500, TextFormat("the sea sleeps where no bird is: %d of %d zones awake, %d fish in the web", live, (int)w.liveZone.size(), alive));
    int known = 0; for (auto s : w.know.isle) known += s > 0;
    check(w.know.isle[w.home] == 2 && known <= 3, TextFormat("the fog: your island charted, %d islands known at the start", known));
    int target = -1; for (int i = 0; i < (int)w.isles.size(); i++) if (w.isles[i].start == 1) target = i;
    // (the rival there: a few nests built for the test, so there's something to count)
    auto seedRival = [&](World& v) { for (int s = 1; s <= (int)v.sides.size(); s++) if (v.sides[s - 1].home == target) { Colony& C = v.ColOf(s); for (int k = 1; k < 6 && k < (int)C.sites.size(); k++) { Nest n; n.site = k; n.pos = C.sites[k].pos; n.built = true; C.sites[k].nest = (int)C.nests.size(); C.nests.push_back(n); } } };
    seedRival(w);
    int trueNests = 0; for (int s = 1; s <= (int)w.sides.size(); s++) if (w.sides[s - 1].home == target) for (const auto& n : w.ColOf(s).nests) trueNests += n.built;
    float dist = Vector3Distance(w.isles[target].c, w.island.c);
    // ---- one scout at each height, each in its own world (so no cross-check)
    int cells[3] = {};
    for (int alt = 0; alt < 3; alt++) {
        World v; fresh(v, 1); seedRival(v);
        v.SendScout(target, -1, {}, (Alt)alt);
        float t0 = v.time; bool reported = false;
        while (v.time - t0 < World::DAY * 3 && !reported) { v.Step(0.1f, FounderInput{}); reported = v.know.sight[target].t >= 0; }   // (800 m out and back, and the night's rest since stage 6)
        const Sighting& s = v.know.sight[target];
        int seenNear = 0; float R = AltSight((Alt)alt);
        for (int j = 0; j < v.know.nz; j++) for (int i = 0; i < v.know.nx; i++) {
            float cx = v.know.x0 + (i + 0.5f) * v.know.cell, cz = v.know.z0 + (j + 0.5f) * v.know.cell;
            if (Vector2Distance({cx, cz}, {v.isles[target].c.x, v.isles[target].c.z}) < 300 && v.know.seen[(size_t)j * v.know.nx + i] >= 0) seenNear++;
        }
        cells[alt] = seenNear;
        bool counted = alt == (int)Alt::High ? (!s.exact && abs(s.nests - trueNests) <= (int)ceilf(trueNests * 0.3f) + 1) : (s.exact && s.nests == trueNests);
        bool logged = !v.know.log.empty() && v.know.log.back().text.find(v.isles[target].name) != std::string::npos;
        check(reported && s.alt == alt && counted && logged && v.know.isle[target] >= 1,
              TextFormat("a scout flown %s to %s (%.0f m off) comes home and reports %s%d nests (truly %d) after %.2f days; it saw %d fog cells round the island (sight %.0f m)", AltName((Alt)alt), v.isles[target].name.c_str(), dist,
                         s.exact ? "" : "about ", s.nests, trueNests, (v.time - t0) / World::DAY, seenNear, R) + std::string(TextFormat(" [scout: %s]", [&] { for (const auto& b : v.col.birds) if (b.role == Role::Scout) return b.alive ? (b.netT > 0 ? "netted" : b.observed ? "observed, flying home" : "alive, not there yet") : (b.cause.empty() ? "dead" : b.cause.c_str()); return "gone"; }())));
    }
    check(cells[2] > cells[1] && cells[1] > cells[0], "the higher it flies, the more of the map it clears");
    // ---- two high scouts on the same island cross-check: the count becomes exact
    {
        World v; fresh(v, 2); seedRival(v);
        v.SendScout(target, -1, {}, Alt::High); v.SendScout(target, -1, {}, Alt::High);
        float t0 = v.time; int reports = 0;
        while (v.time - t0 < World::DAY * 2 && reports < 2) { size_t before = v.know.log.size(); v.Step(0.1f, FounderInput{}); for (size_t k = before; k < v.know.log.size(); k++) reports += v.know.log[k].text.find(" nests") != std::string::npos; }
        const Sighting& s = v.know.sight[target];
        check(reports == 2 && s.exact && s.nests == trueNests && s.scouts == 2, TextFormat("two scouts on the same island cross-check: %d nests, exact (%d reports)", s.nests, reports));
    }
    // ---- a ground: its yield and its sharks
    {
        World v; fresh(v, 1);
        int zone = -1; for (int z = 0; z < (int)v.eco.map->zones.size(); z++) if (v.eco.map->zones[z].name.find(v.isles[target].name) == 0) { zone = z; break; }
        v.SendScout(-1, zone, v.eco.map->zones[zone].Center(), Alt::Mid);
        float t0 = v.time;
        while (v.time - t0 < World::DAY * 2 && v.know.ground[zone].t < 0) v.Step(0.1f, FounderInput{});
        const GroundInfo& g = v.know.ground[zone];
        check(g.t >= 0 && fabsf(g.stock - v.StockOf(zone)) < 0.25f, TextFormat("a scout over %s reports its yield (%.0f%% of what it holds) and sharks", v.eco.map->zones[zone].name.c_str(), g.stock * 100));
    }
    // ---- the cost of a whole map
    {
        World v; fresh(v, 0);
        auto t0 = std::chrono::steady_clock::now(); int steps = 0;
        for (float t = 0; t < World::DAY * 0.5f; t += 0.1f, steps++) v.Step(0.1f, FounderInput{});
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
        printf("  (a whole map steps in %.2f ms at 10 Hz of game time; %d zones, %d links)\n", ms, (int)v.eco.map->zones.size(), (int)v.eco.map->links.size());
        check(ms < 8, "a whole map is cheap enough to run in a frame");
    }
    printf(fails ? "flight-scout-test: %d check(s) failed\n" : "flight-scout-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl