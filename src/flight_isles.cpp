// The Flight, the expansion's islands (design doc pp. 43-45): six more starting islands (the Iceberg, Lighthouse Rock,
// the Shipwreck Island, the Mangrove, the Kelp Raft, the Cliff Town) and six more dangerous ones (Iron Island, the
// Whale, the Siren Rocks, the Maelstrom, the Ghost Ship, Bird Island): their shapes, sites and economies
// (Island::GenerateMore), and what each does in a match (World::StepIsles). The numbers are in
// data/flight/flight_long.json "isles". Headless.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fl {

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
float Flat(Vector3 a, Vector3 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

struct IsleData {
    float iceShells = 6, iceMeltPerDay = 1;
    float keeperFish = 5, keeperMin = 10, beamR = 400, beamSeen = 80;
    float wreckHold = 20, wreckRatDay = 0.15f; int wreckFlood = 6;
    float crocPerHour = 0.03f; float mangroveTwigs = 60;
    float gullsFish = 3;
    float fortRange = 150, fortEvery = 3, fortFlock = 6; int fortTolerated = 3; float fortStores = 30, flagPearls = 1;
    float whaleEvery = 6, whaleUnder = 0.5f, whaleWarn = 0.5f, whaleAugur = 1, whaleKrill = 4;
    float songR = 160, songEvery = 0.05f, songChance = 0.1f, songDays = 1; int sirenBowl = 1; float sirenFervour = 30;
    float maelR = 110, maelBelow = 30, maelKill = 0.2f, maelRocks = 12, calmWind = 2.5f, maelPearls = 1, maelFish = 2;
    float ghostDrift = 0.05f, ghostEggR = 150, ghostEggEvery = 0.1f;
    float birdMob = 10, birdLow = 8, birdConvPerPriest = 4, birdIbis = 10, birdGuano = 5;
};
const IsleData& ID() {
    static IsleData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_long.json");
    const Json& t = j["isles"];
    auto F = [&](const char* k, float& v) { if (t[k].IsNum()) v = t[k].F(v); };
    auto I = [&](const char* k, int& v) { if (t[k].IsNum()) v = t[k].I(v); };
    F("ice_shells", d.iceShells); F("ice_melt_per_day", d.iceMeltPerDay);
    F("keeper_fish", d.keeperFish); F("keeper_min", d.keeperMin); F("beam_m", d.beamR); F("beam_seen_m", d.beamSeen);
    F("wreck_hold", d.wreckHold); F("wreck_rat_day", d.wreckRatDay); I("wreck_flood_sites", d.wreckFlood);
    F("croc_per_hour", d.crocPerHour); F("mangrove_twigs", d.mangroveTwigs); F("gulls_fish", d.gullsFish);
    F("fort_range_m", d.fortRange); F("fort_every_s", d.fortEvery); F("fort_flock", d.fortFlock); I("fort_tolerated_nests", d.fortTolerated); F("fort_stores", d.fortStores); F("flag_pearls", d.flagPearls);
    F("whale_every_days", d.whaleEvery); F("whale_under_days", d.whaleUnder); F("whale_warn_days", d.whaleWarn); F("whale_augur_days", d.whaleAugur); F("whale_krill", d.whaleKrill);
    F("song_m", d.songR); F("song_every_days", d.songEvery); F("song_chance", d.songChance); F("song_days", d.songDays); I("siren_bowl", d.sirenBowl); F("siren_fervour", d.sirenFervour);
    F("maelstrom_m", d.maelR); F("maelstrom_below_m", d.maelBelow); F("maelstrom_kill", d.maelKill); F("maelstrom_rocks_m", d.maelRocks); F("calm_wind", d.calmWind); F("maelstrom_pearls", d.maelPearls); F("maelstrom_fish", d.maelFish);
    F("ghost_drift", d.ghostDrift); F("ghost_egg_m", d.ghostEggR); F("ghost_egg_every_days", d.ghostEggEvery);
    F("bird_mob", d.birdMob); F("bird_low_m", d.birdLow); F("bird_conv_per_priest", d.birdConvPerPriest); F("bird_ibis", d.birdIbis); F("bird_guano", d.birdGuano);
    return d;
}
}  // namespace

// ---------------------------------------------------------------- the shapes
void Island::GenerateMore(IsleType t, uint32_t s) {
    Rng R{s * 9781u + 13};
    auto grid = [&](float ext, float cl) { cell = cl; n = (int)(ext * 2 / cl) + 1; x0 = c.x - ext; z0 = c.z - ext; h.assign((size_t)n * n, -25); };
    auto each = [&](auto f) { for (int zi = 0; zi < n; zi++) for (int xi = 0; xi < n; xi++) { float lx = x0 + xi * cell - c.x, lz = z0 + zi * cell - c.z; h[(size_t)zi * n + xi] = f(lx, lz); } };
    auto at = [&](float a, float r) { Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r}; p.y = Height(p.x, p.z); return p; };
    auto stamp = [&](const Prop& pr) {   // (a prop stands in the heightmap: a bird lands on it)
        float top = pr.c.y + pr.half.y;
        for (int zi = 0; zi < n; zi++) for (int xi = 0; xi < n; xi++) {
            float x = x0 + xi * cell, z = z0 + zi * cell, dx = x - pr.c.x, dz = z - pr.c.z, ca = cosf(pr.yaw), sa = sinf(pr.yaw);
            float u = dx * ca + dz * sa, v = -dx * sa + dz * ca;
            if (fabsf(u) <= pr.half.x && fabsf(v) <= pr.half.z) h[(size_t)zi * n + xi] = std::max(h[(size_t)zi * n + xi], top);
        }
    };
    switch (t) {
    case IsleType::Iceberg: {
        // a floating berg: a tabular top 22 m up, ledges stepping down its flanks, a cave in its south face; deep water all round
        grid(90, 2);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3 + 2, sinf(a) * 3, s) - 0.5f) * 8;
            if (r < 26) return 22.0f;
            if (r < 34) return 14.0f;
            if (r < 42) return 6.0f;
            return -8 - 20 * Sm(42, 85, r);
        });
        radius = 42; hill = {c.x, 22, c.z};
        for (int j = 0; j < 5; j++) addSiteAt(at(j * 2 * PI / 5 + 0.3f, 30));
        for (int j = 0; j < 7; j++) addSiteAt(at(j * 2 * PI / 7, 38));
        for (int j = 0; j < 3; j++) addSiteAt({c.x - 4 + 4.0f * j, 6.2f, c.z + 36});   // (the cave's mouth: the fortress)
        nest = sites[0];
        props.push_back({{c.x, 3.2f, c.z + 36}, {7, 3, 3}, 9});   // (the cave's arch of ice)
        for (int j = 0; j < 10; j++) shellPts.push_back(at(j * 2 * PI / 10 + 0.15f, 40));   // (no twigs: shells frozen in the ice)
    } break;
    case IsleType::Lighthouse: {
        // a bare rock with a working lighthouse: the lamp gallery and the rocks round it are the sites; the keeper's cottage
        grid(70, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.08f, lz * 0.08f, s) - 0.5f) * 10; return r < 30 ? 8 * (1 - r / 30) + 1.5f + 3 * Fbm(lx * 0.15f, lz * 0.15f, s + 1) : -2 - 23 * Sm(30, 66, r); });
        radius = 30;
        Prop tower{{c.x, Height(c.x, c.z) + 14, c.z}, {3, 14, 3}, 2}; props.push_back(tower); stamp(tower);
        props.push_back({{c.x, tower.c.y + 15.5f, c.z}, {2.2f, 1.5f, 2.2f}, 8});   // (the lamp)
        Prop cot{{c.x + 12, Height(c.x + 12, c.z + 6) + 2.2f, c.z + 6}, {3.5f, 2.2f, 3}, 0, 0.4f}; props.push_back(cot); stamp(cot);
        props.push_back({{cot.c.x, cot.c.y + 2.8f, cot.c.z}, {4, 0.6f, 3.6f}, 1, 0.4f});
        hill = {c.x, tower.c.y + 14, c.z};
        for (int j = 0; j < 8; j++) { float a = j * PI / 4; addSiteAt({c.x + cosf(a) * 3.4f, tower.c.y + 13.6f, c.z + sinf(a) * 3.4f}); }   // (the lamp gallery)
        for (int j = 0; j < 10; j++) addSiteAt(at(j * 2 * PI / 10 + 0.2f, 18 + 6 * R()));
        nest = sites[8];
        for (int j = 0; j < 6; j++) { Vector3 p = at(j * 1.05f + 0.5f, 26); if (j % 2) shellPts.push_back(p); else twigPts.push_back({p, 2.0f}); }
    } break;
    case IsleType::Shipwreck: {
        // a galleon broken on a reef: a sand spit, the hull high and dry on it, three masts with their rigging, the stern cabin
        grid(90, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx * 0.5f + lz * lz) + (Fbm(lx * 0.05f, lz * 0.05f, s) - 0.5f) * 10; return r < 34 ? 1.2f + 0.8f * Fbm(lx * 0.1f, lz * 0.1f, s + 2) : -1.5f - 23 * Sm(34, 80, r); });
        radius = 48;
        Prop hull{{c.x, 3.0f, c.z}, {7, 3.5f, 26}, 6, 0.25f}; props.push_back(hull); stamp(hull);
        float ca = sinf(0.25f), sa = cosf(0.25f);
        auto along = [&](float v, float y) { return Vector3{c.x + ca * v, y, c.z + sa * v}; };
        for (int k = 0; k < 3; k++) { Vector3 m = along(-14.0f + 13 * k, 0); props.push_back({{m.x, 15, m.z}, {0.45f, 9 + 2.0f * (k == 1), 0.45f}, 7}); props.push_back({{m.x, 18, m.z}, {5, 0.2f, 0.2f}, 7, 0.25f}); }
        Prop cabin{along(20, 8.5f), {6, 2, 5}, 6, 0.25f}; props.push_back(cabin); stamp(cabin);
        hill = along(20, 10.5f);
        for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) { Vector3 m = along(-14.0f + 13 * k, 0); addSiteAt({m.x + (j - 1) * 2.0f, 12.0f + 3 * j, m.z}); }   // (in the rigging)
        for (int j = 0; j < 6; j++) addSiteAt(along(-18.0f + 6.5f * j, 6.7f));   // (the deck)
        for (int j = 0; j < 4; j++) addSiteAt(along(16.0f + 3 * j, 10.6f));      // (the stern cabin's roof)
        for (int j = 0; j < 3; j++) addSiteAt(along(-10.0f + 10 * j, 1.0f));     // (the hold: low, it floods)
        nest = sites[9];
        for (int k = 0; k < 3; k++) { Vector3 m = along(-14.0f + 13 * k, 0); twigPts.push_back({{m.x + 1, 6.8f, m.z}, 8.0f}); }   // (twigs from the rigging)
        for (int j = 0; j < 6; j++) shellPts.push_back(at(j * 1.05f, 30));
    } break;
    case IsleType::Mangrove: {
        // a tangle of roots over shallow water: the mangroves are the sites (low crowns); no beach to speak of
        grid(110, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.04f, lz * 0.04f, s) - 0.5f) * 18; return r < 70 ? -0.6f + 0.9f * Fbm(lx * 0.12f, lz * 0.12f, s + 5) : -1.5f - 23 * Sm(70, 105, r); });
        radius = 70; hill = c;
        for (int k = 0; k < 4000 && palms.size() < 60; k++) {
            float x = c.x + (R() * 2 - 1) * 64, z = c.z + (R() * 2 - 1) * 64;
            if (Vector2Distance({x, z}, {c.x, c.z}) > 62) continue;
            bool close = false; for (const auto& p : palms) if ((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < 30) close = true;
            if (close) continue;
            palms.push_back({x, std::max(0.0f, Height(x, z)), z}); palmH.push_back(5 + 2 * R());
        }
        for (size_t i = 0; i < palms.size() && sites.size() < 35; i++) if (i % 5 != 4) addSiteAt({palms[i].x, palms[i].y + palmH[i] * 0.9f, palms[i].z});
        for (size_t i = 0; i < palms.size(); i += 2) props.push_back({{palms[i].x, 0.8f, palms[i].z}, {1.6f, 0.8f, 1.6f}, 13, palms[i].x});   // (the roots)
        nest = sites[0];
        for (const auto& p : palms) twigPts.push_back({Vector3{p.x + 0.8f, p.y + 0.3f, p.z}, -1.0f});   // (endless twigs)
    } break;
    case IsleType::KelpRaft: {
        // a floating mat of kelp the size of an island, awash; its knots are the sites; fish shelter under it
        grid(90, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.06f, lz * 0.06f, s) - 0.5f) * 14; return r < 50 ? 0.35f : -6 - 19 * Sm(50, 86, r); });
        radius = 50; hill = {c.x, 0.35f, c.z};
        props.push_back({{c.x, 0.2f, c.z}, {44, 0.18f, 44}, 11, 0.3f});
        for (int j = 0; j < 20; j++) { float a = R() * 2 * PI, r = 6 + R() * 38; addSiteAt({c.x + cosf(a) * r, 0.45f, c.z + sinf(a) * r}); }
        nest = sites[0];
        for (int j = 0; j < 10; j++) { float a = j * 0.63f; twigPts.push_back({{c.x + cosf(a) * 30, 0.5f, c.z + sinf(a) * 30}, 4.0f}); }   // (kelp is twig-grade)
    } break;
    case IsleType::CliffTown: {
        // a human town up a cliff face: terraces of houses with balconies, a harbour at the cliff's foot
        grid(130, 2);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 2 + 9, sinf(a) * 2, s) - 0.5f) * 10;
            if (r >= 72) return -2 - 23 * Sm(72, 125, r);
            float y = 6 + 34 * Sm(-60, 30, -lz);   // (the cliff rises to the north: the harbour is south)
            if (lz > 30 && fabsf(lx) < 26) y = std::min(y, -4.5f);   // (the harbour)
            return y;
        });
        radius = 72; hill = {c.x, 40, c.z - 50};
        for (int row = 0; row < 4; row++) for (int k = 0; k < 4; k++) {   // (terraces of houses up the cliff)
            float x = c.x - 30 + 20.0f * k + (row % 2) * 8, z = c.z + 14 - row * 16.0f;
            float gy = Height(x, z);
            Prop house{{x, gy + 2.5f, z}, {4, 2.5f, 4}, 0}; props.push_back(house); stamp(house);
            props.push_back({{x, gy + 5.6f, z}, {4.6f, 0.7f, 4.6f}, 1});
        }
        for (int k = -1; k <= 1; k++) props.push_back({{c.x + k * 16.0f, 0.5f, c.z + 50}, {2, 0.3f, 14}, 3});
        props.push_back({{c.x - 10, 0.5f, c.z + 58}, {1.6f, 0.7f, 4.5f}, 4}); props.push_back({{c.x + 12, 0.5f, c.z + 60}, {1.6f, 0.7f, 4.5f}, 4});
        Prop pile{{c.x + 30, Height(c.x + 30, c.z + 20) + 1, c.z + 20}, {3, 1.0f, 2}, 5}; props.push_back(pile);
        for (const auto& pr : props) if (pr.kind == 1 && sites.size() < 16) addSiteAt({pr.c.x, pr.c.y + pr.half.y, pr.c.z});   // (the roofs)
        for (int j = 0; j < 14; j++) { float a = -PI * 0.9f + j * 0.13f; addSiteAt(at(a, 66)); }   // (the cliff's ledges)
        nest = sites[0];
        twigPts.push_back({{pile.c.x, pile.c.y + 1.2f, pile.c.z}, 30.0f});
        for (int j = 0; j < 6; j++) shellPts.push_back(at(PI * 0.5f + (j - 2.5f) * 0.25f, 60));
    } break;
    case IsleType::IronIsland: {
        // a naval fort on a low island: a square of stone walls with bastions, cannons on them, a flag over the keep
        grid(110, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.05f, lz * 0.05f, s) - 0.5f) * 10; return r < 62 ? 2.5f + 1.5f * Fbm(lx * 0.06f, lz * 0.06f, s + 4) : -2 - 23 * Sm(62, 105, r); });
        radius = 62;
        for (int k = 0; k < 4; k++) {
            float a = k * PI / 2;
            Prop wall{{c.x + cosf(a) * 30, 6, c.z + sinf(a) * 30}, {k % 2 ? 30.0f : 2.0f, 4, k % 2 ? 2.0f : 30.0f}, 10}; props.push_back(wall); stamp(wall);
            Prop bast{{c.x + cosf(a + PI / 4) * 42, 7, c.z + sinf(a + PI / 4) * 42}, {5, 5, 5}, 10}; props.push_back(bast); stamp(bast);
            props.push_back({{bast.c.x, 12.6f, bast.c.z}, {0.6f, 0.6f, 2.6f}, 12, a + PI / 4});   // (a cannon on the bastion)
        }
        Prop keep{{c.x, 8, c.z}, {8, 6, 8}, 10}; props.push_back(keep); stamp(keep);
        props.push_back({{c.x, 20, c.z}, {0.2f, 6, 0.2f}, 7}); props.push_back({{c.x + 1.8f, 24, c.z}, {1.6f, 1.0f, 0.05f}, 1});   // (the flag)
        hill = {c.x, 14, c.z};
        for (int j = 0; j < 8; j++) { float a = j * PI / 4 + 0.2f; addSiteAt({c.x + cosf(a) * 15, Height(c.x + cosf(a) * 15, c.z + sinf(a) * 15), c.z + sinf(a) * 15}); }   // (inside the walls)
        for (int j = 0; j < 6; j++) addSiteAt(at(j * 1.05f + 0.5f, 52));
        nest = sites[0];
        for (int j = 0; j < 4; j++) twigPts.push_back({at(j * PI / 2 + 0.7f, 50), 3.0f});
    } break;
    case IsleType::Whale: {
        // a sleeping great whale: a long dark back awash with a colony's worth of barnacles; its blowhole steams
        grid(90, 2);
        each([&](float lx, float lz) {
            float u = lx / 70, v = lz / 18; float d = u * u + v * v;
            return d < 1 ? 4.5f * sqrtf(1 - d) + 0.2f : -2 - 20 * Sm(1, 3, d);
        });
        radius = 70; hill = {c.x + 20, 4.6f, c.z};
        for (int j = 0; j < 14; j++) addSiteAt({c.x - 52 + j * 8.0f, 0, c.z + (j % 2 ? 4.0f : -4.0f)});
        for (auto& p : sites) p.y = Height(p.x, p.z);
        nest = sites[0];
        for (int j = 0; j < 12; j++) shellPts.push_back({c.x - 55 + j * 10.0f, Height(c.x - 55 + j * 10.0f, c.z + 9) + 0.1f, c.z + 9});   // (barnacles)
    } break;
    case IsleType::SirenRocks: {
        // a cluster of sea rocks round a pool where the Sirens sing
        grid(80, 2);
        each([&](float lx, float lz) {
            float best = -2 - 23 * Sm(20, 75, sqrtf(lx * lx + lz * lz));
            for (int k = 0; k < 6; k++) { float a = k * 1.05f + 0.3f, r = k == 0 ? 0 : 16; float dx = lx - cosf(a) * r, dz = lz - sinf(a) * r; float d = sqrtf(dx * dx + dz * dz); if (d < 7) best = std::max(best, (k == 0 ? 14 : 9) * (1 - d / 7) + 1); }
            return best;
        });
        radius = 24; hill = {c.x, Height(c.x, c.z), c.z};
        for (int k = 1; k < 6; k++) { float a = k * 1.05f + 0.3f; addSiteAt(at(a, 16)); addSiteAt(at(a + 0.12f, 12)); }
        nest = sites[0];
        for (int j = 0; j < 6; j++) shellPts.push_back(at(j * 1.05f, 22));
    } break;
    case IsleType::Maelstrom: {
        // a whirlpool a hundred metres wide with a stack of rocks at its centre
        grid(130, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz); if (r < 10) return 9 * (1 - r / 10) + 2; return -6 - 30 * Sm(10, 60, r) * (1 - Sm(60, 125, r)) - 14 * Sm(60, 125, r); });
        radius = 12; hill = {c.x, 11, c.z};
        for (int j = 0; j < 6; j++) addSiteAt(at(j * 1.05f, 5));
        nest = sites[0];
    } break;
    case IsleType::GhostShip: {
        // a drifting wreck crewed by the Drowned at night (its pose is set by World::SetGhostPose; drawn from its props)
        grid(30, 1);
        props.push_back({{c.x, 1.0f, c.z}, {5.5f, 2.5f, 20}, 6, -0.4f});
        props.push_back({{c.x - sinf(-0.4f) * 6, 11, c.z - cosf(-0.4f) * 6}, {0.4f, 9, 0.4f}, 7}); props.push_back({{c.x + sinf(-0.4f) * 8, 9, c.z + cosf(-0.4f) * 8}, {0.35f, 7, 0.35f}, 7});
        props.push_back({{c.x - sinf(-0.4f) * 16, 4.4f, c.z - cosf(-0.4f) * 16}, {4, 1.4f, 3.5f}, 6, -0.4f});   // (the captain's cabin)
        each([&](float lx, float lz) { float ca = cosf(-0.4f), sa = sinf(-0.4f); float u = lx * ca + lz * sa, v = -lx * sa + lz * ca; return fabsf(u) < 5.5f && fabsf(v) < 20 ? 3.5f : -25.0f; });
        radius = 20; hill = {c.x, 3.5f, c.z};
        for (int j = 0; j < 3; j++) addSiteAt({c.x + sinf(-0.4f) * (-10 + j * 8.0f), 3.6f, c.z + cosf(-0.4f) * (-10 + j * 8.0f)});
        nest = sites[0];
    } break;
    case IsleType::BirdIsland: {
        // a high white rock streaked with guano, every ledge taken by a wild mega-colony
        grid(120, 2);
        each([&](float lx, float lz) {
            float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3 + 4, sinf(a) * 3, s) - 0.5f) * 12;
            if (r >= 58) return -2 - 23 * Sm(58, 115, r);
            return 2 + 38 * Sm(58, 10, r) + 3 * Fbm(lx * 0.1f, lz * 0.1f, s + 6);
        });
        radius = 58; hill = {c.x, 42, c.z};
        for (int j = 0; j < 16; j++) addSiteAt(at(j * 2 * PI / 16, 30 + 10 * (j % 2)));
        nest = sites[0];
        for (int j = 0; j < 8; j++) shellPts.push_back(at(j * PI / 4 + 0.2f, 54));
    } break;
    // ---- the Long Flight's Far Sea (doc pp. 9-10)
    case IsleType::Thorns: {
        // the Archipelago of Thorns: a chain of twelve small islands, every one a nest site, under bramble
        grid(170, 2);
        std::vector<Vector3> islets;
        for (int k = 0; k < 12; k++) { float u = (k - 5.5f) / 5.5f; islets.push_back({c.x + u * 140, 0, c.z + sinf(u * 2.6f) * 40}); }
        each([&](float lx, float lz) {
            float best = -2 - 23 * Sm(20, 60, 1e9f);
            float dmin = 1e9f; for (const auto& p : islets) dmin = std::min(dmin, sqrtf((lx + c.x - p.x) * (lx + c.x - p.x) + (lz + c.z - p.z) * (lz + c.z - p.z)));
            best = dmin < 11 ? 4 * (1 - dmin / 11) + 1.2f : -2 - 20 * Sm(11, 45, dmin);
            return best;
        });
        radius = 150; hill = {islets[5].x, Height(islets[5].x, islets[5].z), islets[5].z};
        for (const auto& p : islets) { addSiteAt({p.x, Height(p.x, p.z), p.z}); props.push_back({{p.x + 3, Height(p.x, p.z) + 0.8f, p.z}, {2.5f, 0.8f, 2.5f}, 14}); }   // (the brambles)
        nest = sites[0];
        for (size_t k = 0; k < islets.size(); k += 2) twigPts.push_back({{islets[k].x - 3, Height(islets[k].x, islets[k].z) + 0.2f, islets[k].z}, 4.0f});
    } break;
    case IsleType::DrownedFleet: {
        // a fleet of wrecks from a lost navy, drifting together (no terrain: hulls and masts; World::SetFleetPose moves it)
        grid(60, 1);
        for (int k = 0; k < 5; k++) {
            float a = k * 1.25f, r = k == 0 ? 0 : 26; Vector3 p{c.x + cosf(a) * r, 1.0f, c.z + sinf(a) * r}; float yaw = 0.4f * k;
            props.push_back({p, {5, 2.5f, 18}, 6, yaw});
            props.push_back({{p.x, 10, p.z}, {0.4f, 8, 0.4f}, 7});
        }
        each([&](float lx, float lz) { for (const auto& pr : props) if (pr.kind == 6) { float dx = lx + c.x - pr.c.x, dz = lz + c.z - pr.c.z, ca = cosf(pr.yaw), sa = sinf(pr.yaw); float u = dx * ca + dz * sa, v = -dx * sa + dz * ca; if (fabsf(u) < 5 && fabsf(v) < 18) return 3.5f; } return -25.0f; });
        radius = 50; hill = {c.x, 3.5f, c.z};
        for (const auto& pr : props) if (pr.kind == 6) addSiteAt({pr.c.x, 3.6f, pr.c.z});
        nest = sites[0];
        for (const auto& pr : props) if (pr.kind == 7) twigPts.push_back({{pr.c.x + 1, 3.7f, pr.c.z}, 20.0f});   // (rigging twigs without end)
    } break;
    case IsleType::RocPeak: {
        // a mountain island with a single enormous nest at its top
        grid(170, 3);
        each([&](float lx, float lz) { float a = atan2f(lz, lx), r = sqrtf(lx * lx + lz * lz) + (Fbm(cosf(a) * 3 + 1, sinf(a) * 3, s) - 0.5f) * 20; if (r >= 120) return -2 - 23 * Sm(120, 165, r); return 2 + 110 * powf(1 - r / 120, 1.6f); });
        radius = 120; hill = {c.x, Height(c.x, c.z), c.z};
        props.push_back({{c.x, hill.y + 1.5f, c.z}, {9, 1.5f, 9}, 5});   // (the Roc''s nest)
        for (int j = 0; j < 10; j++) addSiteAt(at(j * 2 * PI / 10, 70));
        nest = sites[0];
        for (int j = 0; j < 6; j++) twigPts.push_back({at(j * 1.05f + 0.4f, 100), 3.0f});
    } break;
    case IsleType::MirrorLagoon: {
        // a lagoon so still it reflects the sky, in a ring of low reef
        grid(140, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz) + (Fbm(lx * 0.05f, lz * 0.05f, s) - 0.5f) * 6; float d = fabsf(r - 80); return d < 6 ? 1.2f : r < 80 ? -3.5f : -2 - 23 * Sm(86, 135, r); });
        radius = 86; hill = at(0, 80);
        for (int j = 0; j < 16; j++) addSiteAt(at(j * 2 * PI / 16, 80));
        nest = sites[0];
        for (int j = 0; j < 8; j++) shellPts.push_back(at(j * PI / 4 + 0.2f, 80));
    } break;
    case IsleType::IceShelf: {
        // a shelf of ice from the north: flat, white, cut by leads
        grid(160, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx * 0.4f + lz * lz) + (Fbm(lx * 0.03f, lz * 0.03f, s) - 0.5f) * 20; if (r >= 90) return -8 - 20 * Sm(90, 150, r); return (Fbm(lx * 0.06f, lz * 0.06f, s + 3) > 0.62f) ? -2.0f : 3.0f; });
        radius = 120; hill = {c.x, 3, c.z};
        for (int j = 0; j < 14; j++) { float a = j * 2 * PI / 14; Vector3 p{c.x + cosf(a) * 90, 0, c.z + sinf(a) * 40}; p.y = Height(p.x, p.z); if (p.y > 0) addSiteAt(p); }
        if (sites.empty()) addSiteAt({c.x, 3, c.z});
        nest = sites[0];
        for (int j = 0; j < 8; j++) shellPts.push_back(at(j * PI / 4, 60));
    } break;
    case IsleType::SunkenCity: {
        // Atlantis's spires breaking the surface: stone towers from a drowned plaza
        grid(120, 2);
        each([&](float lx, float lz) { float r = sqrtf(lx * lx + lz * lz); return r < 70 ? -1.5f - 1.5f * Fbm(lx * 0.1f, lz * 0.1f, s) : -4 - 21 * Sm(70, 115, r); });
        radius = 70;
        for (int k = 0; k < 9; k++) {
            float a = k * 0.7f, r = 10 + 6.0f * k; Vector3 p{c.x + cosf(a) * r, 0, c.z + sinf(a) * r}; float hh = 10 + 4.0f * (k % 4);
            Prop sp{{p.x, hh * 0.5f - 1, p.z}, {3, hh * 0.5f + 1, 3}, 10}; props.push_back(sp); stamp(sp);
            props.push_back({{p.x, hh + 1.2f, p.z}, {1.2f, 1.2f, 1.2f}, 8});   // (the spire''s glow)
            addSiteAt({p.x, hh, p.z});
        }
        hill = sites[8]; nest = sites[0];
    } break;
    default: break;
    }
}
void Island::addSiteAt(Vector3 p) { sites.push_back(p); }

// ---------------------------------------------------------------- what each does
float IcebergShells() { return ID().iceShells; }
float SongRange() { return ID().songR; }
int SirenBowl() { return ID().sirenBowl; }
bool World::IsleShields(int isle, int threat) const {
    if (isle < 0 || isle >= (int)isles.size()) return false;
    IsleType t = isles[isle].type;
    if (threat != NT_THEFT && threat != NT_TEAR) return false;
    if (Sanctuary(isle)) return true;   // (the Council's Sanctuary)
    if (t == IsleType::Iceberg || t == IsleType::Mangrove || t == IsleType::Maelstrom) return true;   // (sheer ice; the roots; the centre rocks)
    if (t == IsleType::Lighthouse) { float ph = DayPhase(); return ph < 0.2f || ph > 0.85f; }   // (the beam blinds night raiders)
    return false;
}
bool World::Calm() const { Vector2 w = WindAt(); return GreatNow(GE_CALM) || sqrtf(w.x * w.x + w.y * w.y) < ID().calmWind; }
int World::IsleOfType(IsleType t) const { for (int i = 0; i < (int)isles.size(); i++) if (isles[i].type == t) return i; return -1; }
void World::SetGhostPose() {
    int g = isx.ghost; if (g < 0 || g >= (int)isles.size()) return;
    Island& is = isles[g];
    // a slow circle about where it started (a pure function of the time, like the wreck: a guest's mirror agrees)
    float a = time * ID().ghostDrift / 60.0f;
    Vector3 want{isx.ghostC0.x + cosf(a) * 120 - 120, 0, isx.ghostC0.z + sinf(a) * 120};
    Vector3 d = Vector3Subtract(want, is.c);
    if (fabsf(d.x) < 0.01f && fabsf(d.z) < 0.01f) return;
    is.c = Vector3Add(is.c, d); is.hill = Vector3Add(is.hill, d); is.nest = Vector3Add(is.nest, d);
    is.x0 += d.x; is.z0 += d.z;
    for (auto& s : is.sites) s = Vector3Add(s, d);
    for (auto& p : is.props) p.c = Vector3Add(p.c, d);
    for (auto& o : is.outline) { o.x += d.x; o.y += d.z; }
}
void World::InitIsles() {
    isx = IsleState{};
    const IsleData& D = ID();
    int N = (int)sides.size() + 1;
    isx.birdConv.assign(N, 0);
    for (int i = 0; i < (int)isles.size(); i++) {
        switch (isles[i].type) {
        case IsleType::GhostShip: if (isx.ghost < 0) { isx.ghost = i; isx.ghostC0 = isles[i].c; } break;
        case IsleType::Whale: if (isx.whale < 0) { isx.whale = i; isx.whaleNext = (seasons > 0 ? SeasonDays(seasons) / (float)std::max(1, seasons) : D.whaleEvery) * DAY; } break;
        default: break;
        }
    }
    // the starting islands' gifts: the Shipwreck's hold of salted fish and its bell
    for (int s = 0; s < N; s++) {
        int h = HomeOf(s); if (h < 0 || h >= (int)isles.size()) continue;
        Colony& C = ColOf(s);
        if (isles[h].type == IsleType::Shipwreck && !C.caches.empty()) { for (int q = 0; q < (int)D.wreckHold; q++) C.caches[0].fish.push_back({0, 2, 0}); C.bell = true; }
        if (isles[h].type == IsleType::Iceberg) C.shells += 20;   // (the berg's first nests are ice: a start of shells)
    }
}
void World::StepIsles(float dt) {
    if (!wholeMap) return;
    const IsleData& D = ID();
    int N = (int)sides.size() + 1;
    if ((int)isx.birdConv.size() < N) isx.birdConv.resize(N, 0);
    float ph = DayPhase(); bool night = ph < 0.2f || ph > 0.85f;
    bool dayTick = fmodf(time, DAY) < dt;
    SetGhostPose();
    for (int s = 0; s < N; s++) {
        int h = HomeOf(s); if (h < 0 || h >= (int)isles.size()) continue;
        IsleType t = isles[h].type;
        Colony& C = ColOf(s);
        auto loseYoung = [&](int ni, const char* cause, bool all) {
            for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick)) { WithSide(s, [&] { BirdDies(b, cause); }); if (!all) break; }
        };
        auto fishIn = [&]() { int n = 0; for (const auto& c : C.caches) n += (int)c.fish.size(); return n; };
        auto takeFish = [&](int n) { for (auto& c : C.caches) while (n > 0 && !c.fish.empty()) { c.fish.pop_back(); n--; } };
        switch (t) {
        case IsleType::Iceberg: {
            // ice nests want shells, not twigs: a nest laid on the berg rises as soon as the stores have them
            for (auto& n : C.nests) if (!n.built && n.isle == h && C.shells >= (int)D.iceShells) { C.shells -= (int)D.iceShells; n.built = true; n.twigs = (float)NestTwigs(); SayTo(s, "An ice nest is cut into the berg (shells for its lining)."); }
            // it melts in Summer: a site lost a day (and the nest on it)
            if (dayTick && seasons > 0 && Season() == SEASON_SUMMER) {
                for (int k = (int)C.sites.size() - 1; k >= 0; k--) {
                    Site& st = C.sites[k]; if (st.isle != h || st.nest == -2) continue;
                    if (st.nest >= 0 && st.nest < (int)C.nests.size()) { Nest& n = C.nests[st.nest]; loseYoung(st.nest, "the ice melted under its nest", true); n.built = false; n.twigs = 0; n.mate = -1; n.bowl = 0; }
                    st.nest = -2;
                    SayTo(s, "Summer: the berg melts and a ledge goes into the sea.");
                    break;
                }
            }
        } break;
        case IsleType::Lighthouse: {
            const Island& is = isles[h];
            if (dayTick && fishIn() >= (int)D.keeperMin) { takeFish((int)D.keeperFish); C.pearls++; SayTo(s, "The lighthouse keeper trades a jar of lamp oil for fish (a pearl's worth)."); }
            if (night && fmodf(time, 2.0f) < dt) {
                Vector3 lamp = is.hill;
                WithSide(s, [&] { Reveal(lamp, D.beamR); });   // (the beam reveals the sea at night)
                for (int o = 0; o < N; o++) if (o != s) WithSide(o, [&] { Reveal(lamp, D.beamSeen); });   // (and everyone sees the island from anywhere)
            }
        } break;
        case IsleType::Shipwreck: {
            if (dayTick && Rand() < D.wreckRatDay) { for (int ni = 0; ni < (int)C.nests.size(); ni++) { bool egg = false; for (const auto& b : C.birds) egg |= b.alive && b.nest == ni && b.stage == BStage::Egg; if (egg && C.nests[ni].isle == h) { for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage == BStage::Egg) { WithSide(s, [&] { BirdDies(b, "eaten by the wreck's rats"); }); break; } break; } } }
            if (dayTick && seasons > 0 && Season() >= SEASON_AUTUMN && !isx.holdFlooded[std::min(s, 5)]) {   // (the wreck settles: the hold floods by Autumn)
                isx.holdFlooded[std::min(s, 5)] = true;
                int lost = 0;
                for (int k = 0; k < (int)C.sites.size() && lost < D.wreckFlood; k++) { Site& st = C.sites[k]; if (st.isle != h || st.pos.y > 1.6f) continue; if (st.nest >= 0 && st.nest < (int)C.nests.size()) { Nest& n = C.nests[st.nest]; loseYoung(st.nest, "the wreck's hold flooded", true); n.built = false; n.twigs = 0; n.mate = -1; } st.nest = -2; lost++; }
                SayTo(s, "Autumn: the wreck settles and the hold floods.");
            }
        } break;
        case IsleType::CliffTown: {
            bool watcher = false; for (const auto& b : C.birds) watcher |= b.alive && b.stage == BStage::Adult && b.role == Role::Watcher;
            if (dayTick && !watcher && fishIn() > 0) { takeFish((int)D.gullsFish); SayTo(s, "The cliff's wild gulls raid the caches (a Watcher would keep them off)."); }
        } break;
        default: break;
        }
    }
    // ---- the mangrove's crocodiles: a fisher diving there may be taken
    for (int i = 0; i < (int)isles.size(); i++) {
        const Island& is = isles[i];
        if (is.type != IsleType::Mangrove || fmodf(time, DAY / 24) >= dt) continue;
        for (int s = 0; s < N; s++) for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && b.task == Task::Dive && Flat(b.pos, is.c) < is.radius + 30 && Rand() < D.crocPerHour) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "taken by a crocodile under the mangroves"); }); }
    }
    // ---- Iron Island: the fort's cannons, the marines, its stores and its flag
    if (int ii = IsleOfType(IsleType::IronIsland); ii >= 0) {
        const Island& is = isles[ii];
        int holder = HolderOf(ii);
        isx.ironT += dt;
        if (isx.ironT >= D.fortEvery) {
            isx.ironT = 0;
            for (int s = 0; s < N; s++) for (auto& f : ColOf(s).flocks) {   // (any flock of more than six over it)
                int alive = 0; Bird* one = nullptr;
                for (int id : f.members) if (Bird* b = FindBird(s, id); b && Flat(b->pos, is.c) < D.fortRange) { alive++; one = b; }
                if (alive > (int)D.fortFlock && one) { Bird& b = *one; WithSide(s, [&] { BirdDies(b, "shot by Iron Island's cannon"); }); SayTo(s, "Iron Island's cannons fire on your flock!"); }
            }
        }
        if (dayTick) for (int s = 0; s < N; s++) {
            Colony& C = ColOf(s);
            int there = 0; for (const auto& n : C.nests) there += n.built && n.isle == ii;
            if (there > D.fortTolerated) for (int ni = 0; ni < (int)C.nests.size(); ni++) if (C.nests[ni].built && C.nests[ni].isle == ii) {   // (the garrison tolerates small colonies)
                bool hit = false; for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick)) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "shot by the fort's marines"); }); hit = true; break; }
                if (hit) { SayTo(s, "The fort's marines shoot into your nests: too many inside the walls."); break; }
            }
        }
        if (holder >= 0) {
            if (!isx.ironStores) { isx.ironStores = true; Colony& C = ColOf(holder); for (int q = 0; q < (int)D.fortStores && !C.caches.empty(); q++) C.caches[0].fish.push_back({0, 2, 0}); SayTo(holder, "Iron Island's stores of salt fish are yours."); }
            if (dayTick) { ColOf(holder).pearls += (int)D.flagPearls; SayTo(holder, "The towns pay for the fort's flag (a pearl)."); }
        }
    }
    // ---- the Whale: krill in its wake for the holder; it dives once a season (everything on it in the water)
    if (isx.whale >= 0) {
        const Island& is = isles[isx.whale];
        int holder = HolderOf(isx.whale);
        if (isx.whaleUnderT > 0) { isx.whaleUnderT -= dt; if (isx.whaleUnderT <= 0) for (int s = 0; s < N; s++) SayTo(s, "The Whale surfaces again."); }
        if (holder >= 0 && dayTick && isx.whaleUnderT <= 0) { Colony& C = ColOf(holder); for (int q = 0; q < (int)D.whaleKrill && !C.caches.empty(); q++) C.caches[0].fish.push_back({0, 1, 0}); }
        float left = isx.whaleNext - time;
        for (int s = 0; s < N; s++) {
            bool augur = false, nests = false; for (const auto& b : ColOf(s).birds) augur |= b.alive && b.stage == BStage::Adult && b.role == Role::Augur;
            for (const auto& n : ColOf(s).nests) nests |= n.built && n.isle == isx.whale;
            float warn = (augur ? D.whaleAugur : D.whaleWarn) * DAY;
            if (nests && left > 0 && left <= warn && left + dt > warn) SayTo(s, augur ? "Your Augur knows: the Whale will dive within a day. Move the eggs." : "The Whale stirs: it will dive soon. Move the eggs.");
        }
        if (time >= isx.whaleNext) {
            isx.whaleNext += (seasons > 0 ? SeasonDays(seasons) / (float)std::max(1, seasons) : D.whaleEvery) * DAY;
            isx.whaleUnderT = D.whaleUnder * DAY; isx.dives++;
            for (int s = 0; s < N; s++) {
                Colony& C = ColOf(s);
                for (int ni = 0; ni < (int)C.nests.size(); ni++) {
                    Nest& n = C.nests[ni]; if (n.isle != isx.whale || !n.built) continue;
                    for (auto& b : C.birds) if (b.alive && b.nest == ni && (b.stage == BStage::Egg || b.stage == BStage::Chick)) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "lost when the Whale dived"); }); }
                    n.built = false; n.twigs = 0; n.mate = -1; n.bowl = 0;
                }
                SayTo(s, "The Whale DIVES: everything on its back is in the sea.");
            }
            (void)is;
        }
    }
    // ---- the Siren Rocks: birds within the song fly to the rocks and sit (lost for a day) unless deaf; the holder's prize
    if (int si = IsleOfType(IsleType::SirenRocks); si >= 0) {
        const Island& is = isles[si];
        int holder = HolderOf(si);
        if (fmodf(time, D.songEvery * DAY) < dt) for (int s = 0; s < N; s++) {
            bool deaf = Founders()[FounderOf(s).def].key == "owl";
            for (const auto& b : ColOf(s).birds) deaf |= b.alive && b.stage == BStage::Adult && b.role == Role::Drummer;   // (the Drummers' sound)
            if (deaf) continue;
            for (auto& b : ColOf(s).birds) if (b.alive && b.stage == BStage::Adult && b.songT <= 0 && Flat(b.pos, is.c) < D.songR && Rand() < D.songChance) { b.songT = D.songDays * DAY; b.task = Task::Sit; SayTo(s, "A bird of yours hears the Sirens and flies to their rocks."); }
        }
        if (holder >= 0 && !isx.sirenGift) { isx.sirenGift = true; ColOf(holder).fervour = std::min(100.0f, ColOf(holder).fervour + D.sirenFervour); SayTo(holder, "The Sirens sing for you: fervour rises, and their mates come easy (a bowl needs a fish fewer)."); }
    }
    // ---- the Maelstrom: anything low over it is pulled in; the holder's flotsam
    if (int mi = IsleOfType(IsleType::Maelstrom); mi >= 0) {
        const Island& is = isles[mi];
        if (fmodf(time, 1.0f) < dt) for (int s = 0; s < N; s++) {
            for (auto& b : ColOf(s).birds) {
                if (!b.alive || b.stage != BStage::Adult) continue;
                float d = Flat(b.pos, is.c);
                if (d < D.maelR && d > D.maelRocks && b.pos.y < D.maelBelow && Rand() < D.maelKill) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "pulled into the Maelstrom"); }); }
            }
            Founder& F = FounderOf(s);
            float d = Flat(F.pos, is.c);
            if ((F.st == FState::Fly || F.st == FState::Strike || F.st == FState::Floating) && d < D.maelR * 0.6f && d > D.maelRocks && F.pos.y < D.maelBelow * 0.5f && Rand() < D.maelKill) {
                if (HumanOf(s)) WithSide(s, [&] { Kill("pulled into the Maelstrom"); }); else { F.st = FState::Dead; F.respawnT = 30; F.deaths++; }
            }
        }
        int holder = HolderOf(mi);
        if (holder >= 0 && dayTick) {
            Colony& C = ColOf(holder);
            C.pearls += (int)D.maelPearls; for (int q = 0; q < (int)D.maelFish && !C.caches.empty(); q++) C.caches[0].fish.push_back({0, 2, 0});
            if (!isx.maelRelic && seasons > 0 && RelicCount(holder) < RelicsMax()) for (int r = 0; r < RL_COUNT; r++) { bool held = false; for (int o = 0; o < N; o++) held |= (ColOf(o).relics >> r) & 1; if (!held) { C.relics |= 1u << r; isx.maelRelic = true; SayTo(holder, "Washed up on the Maelstrom's rocks: " + Relics()[r].name + "."); break; } }
            SayTo(holder, "The Maelstrom gives up what it swallowed: a pearl and salt fish.");
        }
    }
    // ---- the Ghost Ship: the Drowned take eggs at night; boarded by day, its cabin's relic and its cannon, once a season
    if (isx.ghost >= 0) {
        const Island& is = isles[isx.ghost];
        if (night && fmodf(time, D.ghostEggEvery * DAY) < dt) for (int s = 0; s < N; s++) {
            Colony& C = ColOf(s);
            for (int ni = 0; ni < (int)C.nests.size(); ni++) {
                if (!C.nests[ni].built || Flat(C.nests[ni].pos, is.c) > D.ghostEggR) continue;
                bool took = false; for (auto& b : C.birds) if (b.alive && b.nest == ni && b.stage == BStage::Egg) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "taken by the Drowned"); }); took = true; break; }
                if (took) { SayTo(s, "The Drowned come off the Ghost Ship in the night and take an egg."); break; }
            }
        }
        if (!night) for (int s = 0; s < N; s++) {
            const Founder& F = FounderOf(s);
            int season = seasons > 0 ? std::max(0, Season()) : (int)(time / (6 * DAY));
            if ((int)isx.ghostSeason.size() < N) isx.ghostSeason.resize(N, -1);
            if (F.st == FState::Dead || Flat(F.pos, is.c) > 12 || F.pos.y > is.hill.y + 10 || isx.ghostSeason[s] == season) continue;
            isx.ghostSeason[s] = season;
            if (seasons > 0 && RelicCount(s) < RelicsMax()) for (int r = 0; r < RL_COUNT; r++) { bool held = false; for (int o = 0; o < N; o++) held |= (ColOf(o).relics >> r) & 1; if (!held) { ColOf(s).relics |= 1u << r; SayTo(s, "In the Ghost Ship's cabin: " + Relics()[r].name + "."); break; } }
            int tgt = -1; float bd = 1e9f; for (int o = 0; o < N; o++) if (o != s && !ColOf(o).caches.empty()) { float d = Flat(ColOf(o).caches[0].pos, is.c); if (d < bd) { bd = d; tgt = o; } }
            if (tgt >= 0) { Blast(GroundAt(ColOf(tgt).caches[0].pos.x, ColOf(tgt).caches[0].pos.z), s, 0); SayTo(s, "You fire the Ghost Ship's cannon at " + SideName(tgt) + "."); SayTo(tgt, SideName(s) + " fires the Ghost Ship's cannon at your island!"); }
        }
    }
    // ---- Bird Island: the wild colony mobs anything that lands; priests convert it a flock at a time
    if (int bi = IsleOfType(IsleType::BirdIsland); bi >= 0) {
        const Island& is = isles[bi];
        int holder = HolderOf(bi);
        if (fmodf(time, 1.0f) < dt) for (int s = 0; s < N; s++) {
            if (s == holder) continue;
            for (auto& b : ColOf(s).birds) {
                if (!b.alive || b.stage != BStage::Adult || Flat(b.pos, is.c) > is.radius || b.pos.y > HeightAt(b.pos.x, b.pos.z) + D.birdLow) continue;
                b.hp -= D.birdMob;
                if (b.hp <= 0 || (b.role != Role::Tank && b.role != Role::Striker && b.hp < 20)) { Bird& bb = b; WithSide(s, [&] { BirdDies(bb, "mobbed by Bird Island's wild colony"); }); }
            }
        }
        if (dayTick) for (int s = 0; s < N; s++) {
            if (s == holder) continue;
            int priests = 0; for (const auto& b : ColOf(s).birds) priests += b.alive && b.stage == BStage::Adult && b.role == Role::Priest && Flat(b.pos, is.c) < 400;
            if (!priests) { for (const auto& b : ColOf(s).birds) priests += b.alive && b.stage == BStage::Adult && b.role == Role::Priest; priests = priests > 0 ? 1 : 0; }   // (priests at home still preach to its edge, a little)
            if (!priests) continue;
            isx.birdConv[s] += priests * D.birdConvPerPriest * (Founders()[FounderOf(s).def].key == "ibis" ? D.birdIbis : 1.0f);
            if (isx.birdConv[s] >= 100) {
                isx.birdConv[s] = 0;
                Colony& C = ColOf(s);
                int made = 0;
                for (const auto& p : is.sites) { if (made >= 4) break; Nest n; n.pos = p; n.isle = bi; n.built = true; n.twigs = (float)NestTwigs(); Site st; st.pos = p; st.isle = bi; st.nest = (int)C.nests.size(); C.sites.push_back(st); n.site = (int)C.sites.size() - 1; C.nests.push_back(n); made++; }
                for (int o = 0; o < N; o++) if (o != s) { Colony& O = ColOf(o); for (auto& n : O.nests) if (n.isle == bi) { n.built = false; n.twigs = 0; } }
                for (int o = 0; o < N; o++) SayTo(o, SideName(s) + "'s priests have converted Bird Island: its wild colony is theirs.");
            } else SayTo(s, TextFormat("Your priests preach at Bird Island's edge (%.0f%% converted).", isx.birdConv[s]));
        }
        if (holder >= 0 && dayTick) { Colony& C = ColOf(holder); C.wildMates = std::max(C.wildMates, 30); C.guano += D.birdGuano; }
    }
}
float World::IsleConv(int side) const { return side >= 0 && side < (int)isx.birdConv.size() ? isx.birdConv[side] : 0; }

}  // namespace fl
