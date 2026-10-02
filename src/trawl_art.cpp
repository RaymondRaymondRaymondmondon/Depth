// The Trawl's procedural pixel art (design doc, "Art"): a top-down deck drawn at twice the parkour pixel scale into
// the pixel canvas, in the boat's own frame (bow to the right) so the planks stay crisp while the sea turns under her.
// The sea is black outside the light: only the lantern's pool shows water, foam and glints.
#include "skins.h"
#include "trawl_art.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace tw {

static float H01(int x, int y, int s) { unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (unsigned)(s * 83492791); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xFFFF) / 65535.0f; }
static Color Mix(Color a, Color b, float t) { t = std::clamp(t, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255}; }
static Color Dim(Color c, float k) { return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; }

Vector2 View::ToCanvas(Vector2 deck) const { return {floorf(center.x + deck.x * ppm), floorf(center.y + deck.y * ppm)}; }
Vector2 View::DeckOfCanvas(Vector2 c) const { return {(c.x - center.x) / ppm, (c.y - center.y) / ppm}; }
static void Px(const View& v, Vector2 deck, float w, float h, Color c) {   // a deck-space box, snapped to the canvas grid
    Vector2 p = v.ToCanvas(deck);
    DrawRectangle((int)p.x, (int)p.y, (int)std::max(1.0f, w * v.ppm), (int)std::max(1.0f, h * v.ppm), c);
}
static void PxC(const View& v, Vector2 deck, float w, float h, Color c) { Px(v, {deck.x - w / 2, deck.y - h / 2}, w, h, c); }

// how much light reaches a deck point (0 dark .. 1 lit): the lantern, the wheelhouse glow, deck lamps
float View::LightAt(Vector2 deck) const {
    float best = 0;
    for (const auto& l : lights) {
        float d = Vector2Distance(deck, l.at);
        if (d >= l.r) continue;
        float k = l.k * (1 - d / l.r) * (1 - d / l.r) * 1.4f;
        if (l.half > 0 && d > 1.5f) {
            Vector2 to = Vector2Scale(Vector2Subtract(deck, l.at), 1 / d);
            float c = Vector2DotProduct(to, l.dir), edge = cosf(l.half);
            if (c < edge) continue;
            k = l.k * std::clamp((c - edge) / (1 - edge) * 3, 0.0f, 1.0f) * (1 - d / l.r * 0.7f);
        }
        best = std::max(best, k);
    }
    return std::min(1.0f, best);
}

// ---------------------------------------------------------------- the sea
void DrawSea(const Gannet& g, const View& v) {
    const int C = 3;   // canvas pixels per sea cell
    Vector2 f = g.boat.Forward(), s{-f.y, f.x};
    for (int cy = 0; cy < PIXEL_H + 2; cy += C) for (int cx = 0; cx < PIXEL_W + 2; cx += C) {
        Vector2 d = v.DeckOfCanvas({(float)cx + C / 2.0f, (float)cy + C / 2.0f});
        float lit = v.LightAt(d);
        if (lit < 0.02f) {
            // outside the light: black, with a rare glint of moon on a crest
            Vector2 w{g.boat.pos.x + f.x * d.x + s.x * d.y, g.boat.pos.y + f.y * d.x + s.y * d.y};
            // (the Grotto: the mould on the walls glows in the dark, the cave's own light)
            if (g.eco && g.eco->ground == "grotto" && g.eco->HabAt(w) == H_WALL) {
                float n = H01((int)(w.x * 1.2f), (int)(w.y * 1.2f), 17);
                if (n < 0.4f) { float pulse = 0.55f + 0.45f * sinf(g.time * 0.6f + n * 20); DrawRectangle(cx, cy, C, C, Dim(Color{70, 170, 150, 255}, (0.18f + 0.3f * n) * pulse)); }
                continue;
            }
            float hgt = g.sea.Height(w.x, w.y);
            if (v.moon > 0 && hgt > g.sea.swell * 0.62f && H01((int)(w.x * 2), (int)(w.y * 2), (int)(g.time * 3)) < 0.05f * v.moon) DrawRectangle(cx, cy, 1, 1, Color{120, 140, 150, 255});
            continue;
        }
        Vector2 w{g.boat.pos.x + f.x * d.x + s.x * d.y, g.boat.pos.y + f.y * d.x + s.y * d.y};
        float hgt = g.sea.Height(w.x, w.y) / std::max(0.2f, g.sea.swell);
        // bands of swell: dark troughs, lighter faces, foam on the crests
        Color deep{6, 22, 30, 255}, face{26, 74, 84, 255}, foam{170, 196, 196, 255};
        float band = floorf((0.5f + hgt * 0.5f) * 5) / 5;          // (in steps: swell reads as bands)
        Color c = Mix(deep, face, band);
        if (g.eco) {
            // the Lagoon's floor shows through the shallows under the lamp: coral, seagrass, sand
            float dep = g.eco->DepthAt(w);
            int hb = g.eco->HabAt(w);
            bool cave = g.eco->ground == "grotto" && w.x > g.eco->archX0 - 2;
            if (hb == H_LAND) { DrawRectangle(cx, cy, C, C, Dim(cave ? Color{66, 62, 60, 255} : Color{150, 138, 100, 255}, 0.3f + 0.7f * lit)); continue; }
            if (hb == H_WALL) c = Mix(c, H01((int)(w.x * 1.2f), (int)(w.y * 1.2f), 17) < 0.4f ? Color{70, 170, 150, 255} : Color{40, 60, 56, 255}, 0.45f);   // mould on the ledges
            if (hb == H_WRECK) c = Mix(c, H01((int)(w.x * 1.5f), (int)(w.y * 0.5f), 23) < 0.5f ? Color{86, 64, 44, 255} : Color{50, 40, 32, 255}, 0.55f);   // a smugglers' hull under the water
            if (cave && hb == H_OPEN) c = Mix(c, Color{4, 10, 14, 255}, 0.4f);   // (black water under the roof)
            if (hb == H_KELP) {
                // the Weeds' canopy: golden-brown kelp mats lying on the surface, swaying with the swell, dark gaps between
                float n = H01((int)(w.x * 1.6f), (int)(w.y * 1.6f), 9), sway = sinf(g.time * 0.8f + w.x * 0.3f + w.y * 0.2f);
                Color kelp = n < 0.55f ? Color{120, 92, 34, 255} : n < 0.8f ? Color{150, 118, 44, 255} : Color{70, 58, 30, 255};
                c = Mix(c, kelp, 0.75f + 0.1f * sway);
            } else if (hb == H_BARREN && dep < 14) c = Mix(c, Color{70, 64, 72, 255}, expf(-dep / 6) * 0.6f);   // urchin barrens: bare purple-grey rock
            if (dep < 10) {
                Color bed = hb == H_SEAGRASS ? Color{40, 96, 60, 255} : (hb == H_REEF || hb == H_CREST) ? Color{150, 110, 110, 255} : Color{150, 150, 120, 255};
                if ((hb == H_REEF || hb == H_CREST) && H01((int)(w.x * 1.3f), (int)(w.y * 1.3f), 3) < 0.35f) bed = Color{200, 150, 130, 255};
                if (hb == H_SEAGRASS && H01((int)(w.x * 2), (int)(w.y * 2), 5) < 0.4f) bed = Color{60, 120, 70, 255};
                c = Mix(c, bed, expf(-dep / 3.5f) * 0.75f);
            }
        }
        if (hgt > 0.8f && H01((int)(w.x * 3), (int)(w.y * 3), (int)(g.time * 2)) < 0.12f + g.sea.swell * 0.1f) c = Mix(c, foam, 0.7f);
        float k = 0.25f + 0.75f * lit;
        DrawRectangle(cx, cy, C, C, Dim(c, k));
    }
    // the boat's wake and the bow wave, in the light
    float sp = g.boat.Speed();
    if (sp > 0.4f) for (int i = 0; i < 40; i++) {
        float x = -11.5f - i * 0.45f, spread = 0.4f + i * 0.09f;
        for (int k = -1; k <= 1; k += 2) {
            Vector2 d{x, k * spread + sinf(g.time * 3 + i) * 0.1f};
            float lit = v.LightAt(d);
            if (lit < 0.05f || H01(i, k, (int)(g.time * 8)) > std::min(1.0f, sp / 4)) continue;
            Px(v, d, 0.2f, 0.2f, Dim(Color{200, 220, 220, 255}, 0.3f + 0.7f * lit));
        }
    }
}

// ---------------------------------------------------------------- the web's life in the light
static void FishMark(const View& v, Vector2 deck, Vector2 dir, float len, float wid, Color c) {
    Vector2 cc = v.ToCanvas(deck);
    for (int k = 0; k <= 6; k++) {
        float t = k / 6.0f;
        Vector2 q = Vector2Add(cc, Vector2Scale(dir, len * v.ppm * (0.5f - t)));
        DrawCircleV(q, std::max(0.5f, wid * v.ppm * 0.5f * sinf(PI * (0.15f + t * 0.75f))), c);
    }
    Vector2 tb = Vector2Add(cc, Vector2Scale(dir, -len * v.ppm * 0.5f)), sd{-dir.y, dir.x};
    Vector2 a = Vector2Add(tb, Vector2Add(Vector2Scale(dir, -wid * v.ppm), Vector2Scale(sd, wid * v.ppm * 0.7f)));
    Vector2 b = Vector2Add(tb, Vector2Add(Vector2Scale(dir, -wid * v.ppm), Vector2Scale(sd, -wid * v.ppm * 0.7f)));
    DrawTriangle(tb, a, b, c); DrawTriangle(tb, b, a, c);
}
void DrawLife(const Gannet& g, const View& v, bool air) {
    if (!g.eco) return;
    const Eco& e = *g.eco;
    const auto& S = Species().sp;
    const Boat& b = g.boat;
    Vector2 fwd = b.Forward(), side{-fwd.y, fwd.x};
    auto toDeckDir = [&](Vector2 w) { Vector2 d{Vector2DotProduct(w, fwd), Vector2DotProduct(w, side)}; float l = Vector2Length(d); return l > 1e-3f ? Vector2Scale(d, 1 / l) : Vector2{1, 0}; };
    if (!air) {
        // sargassum rafts: golden weed on the surface
        for (const auto& rf : e.rafts) {
            Vector2 dc = b.ToDeck(rf.p);
            if (fabsf(dc.x) > 30 || fabsf(dc.y) > 18) continue;
            for (int k = 0; k < 40; k++) {
                float ang = H01(k, (int)rf.r, 7) * 6.2832f, rr = sqrtf(H01(k, 3, (int)rf.r)) * rf.r;
                Vector2 q{dc.x + cosf(ang) * rr, dc.y + sinf(ang) * rr * 0.7f};
                float lit = v.LightAt(q);
                if (lit < 0.04f) continue;
                Px(v, q, 0.3f, 0.3f, Dim(Color{170, 130, 50, 255}, 0.3f + 0.7f * lit));
            }
        }
    }
    for (size_t i = 0; i < e.agents.size(); i++) {
        const EcoAgent& a = e.agents[i];
        const SpeciesRec& r = S[a.sp];
        bool isAir = r.band == BAND_AIR;
        if (isAir != air) continue;
        Vector2 dc = b.ToDeck({a.p.x, a.p.y});
        if (fabsf(dc.x) > 32 || fabsf(dc.y) > 20) continue;
        if (!air && fabsf(dc.y) < 2.8f && dc.x > -11.5f && dc.x < 10.5f) continue;   // under her hull
        float lit = v.LightAt(dc) * expf(-std::max(0.0f, a.p.z) / 7.0f);
        if (!air && v.ghost && v.ghostSee > 0 && r.threat && Vector2Distance(dc, v.ghostAt) < 10) {
            // what the dead see: a pale outline in the dark
            Vector2 cc = v.ToCanvas(dc); float len = std::clamp(0.4f + sqrtf(r.MeanKg()) * 0.3f, 0.5f, 3.5f);
            DrawCircleLines((int)cc.x, (int)cc.y, len * v.ppm * 0.5f, Fade(Color{200, 230, 255, 255}, 0.6f * std::min(1.0f, v.ghostSee)));
            if (lit < 0.04f) continue;
        }
        if (!air && lit < 0.04f) {
            // in the dark only a flash of silver near the surface gives a school away
            if (a.p.z < 1.5f && a.flash > 0) { Vector2 c = v.ToCanvas(dc); DrawPixel((int)c.x, (int)c.y, Color{150, 170, 180, 255}); }
            continue;
        }
        float vis = air ? 1.0f : std::clamp(lit * 1.6f, 0.0f, 1.0f);
        Vector2 hd = toDeckDir({a.v.x, a.v.y});
        Color base = r.cls == "jelly" ? Color{190, 200, 230, 255} : r.cls == "reptile" ? Color{80, 110, 70, 255} : r.cls == "invert" ? Color{170, 110, 90, 255}
                   : r.tier == 1 ? Color{170, 190, 200, 255} : r.tier >= 4 ? Color{70, 80, 90, 255} : Color{120, 140, 130, 255};
        if (r.name == "mahi-mahi") base = Color{150, 190, 80, 255};
        if (r.name == "parrotfish") base = Color{90, 170, 160, 255};
        if (r.name == "snapper") base = Color{190, 100, 90, 255};
        if (r.tier == 1 && r.cls == "fish") base = Color{205, 220, 228, 255};
        float alpha = air ? 0.95f : (r.tier >= 3 ? 0.55f : 0.45f) + 0.45f * vis;
        Color c = Fade(Dim(base, 0.55f + 0.45f * vis), alpha * std::clamp(1.2f - a.p.z / 12, 0.35f, 1.0f));
        if (air) {
            // a flock of gulls: pale Vs wheeling over the boat (a pelican: a big brown V; a frigatebird: a long black one)
            int bk = BirdKindOf(r.name);
            float w = bk == BIRD_PELICAN ? 5 : bk == BIRD_FRIGATE ? 6 : 3;
            Color bc = bk == BIRD_PELICAN ? Color{170, 135, 100, 255} : bk == BIRD_FRIGATE ? Color{30, 30, 36, 255} : WHITE;
            for (int k = 0; k < std::min(a.count, 16); k++) {
                float ang = H01((int)i, k, 1) * 6.2832f + g.time * (0.6f + H01(k, 2, (int)i));
                float rr = 1.5f + H01(k, (int)i, 3) * 3;
                Vector2 q = Vector2Add(dc, {cosf(ang) * rr, sinf(ang) * rr});
                Vector2 cq = v.ToCanvas(q); float flap = sinf(g.time * (bk == BIRD_GULL ? 9 : 4) + k) > 0 ? 1.0f : 0.0f;
                DrawLineV({cq.x - w, cq.y - flap}, cq, Fade(bc, 0.9f)); DrawLineV(cq, {cq.x + w, cq.y - flap}, Fade(bc, 0.9f));
                if (bk != BIRD_GULL) DrawLineV({cq.x - w * 0.5f, cq.y - flap * 0.5f + 1}, {cq.x + w * 0.5f, cq.y - flap * 0.5f + 1}, Fade(bc, 0.6f));
            }
            continue;
        }
        if (a.count > 3) {
            // a school: its fish scattered round the centre, turning together
            int nd = std::min(a.count, 18);
            float spread = std::min(4.0f, 0.4f + sqrtf((float)a.count) * 0.12f);
            for (int k = 0; k < nd; k++) {
                float ang = H01((int)i, k, 11) * 6.2832f, rr = sqrtf(H01(k, (int)i, 12)) * spread;
                Vector2 q = Vector2Add(dc, {cosf(ang) * rr + sinf(g.time * 2 + k) * 0.15f, sinf(ang) * rr * 0.7f});
                float len = std::clamp(0.12f + sqrtf(r.MeanKg()) * 0.25f, 0.15f, 0.8f);
                if (len < 0.3f) { Vector2 cq = v.ToCanvas(q); Vector2 t = v.ToCanvas(Vector2Subtract(q, Vector2Scale(hd, len))); DrawLineV(cq, t, c); }
                else FishMark(v, q, hd, len, len * 0.3f, c);
            }
        } else {
            for (int k = 0; k < a.count; k++) {
                Vector2 q = Vector2Add(dc, {H01((int)i, k, 5) * 1.2f - 0.6f, H01(k, (int)i, 6) * 1.2f - 0.6f});
                float len = std::clamp(0.3f + sqrtf(r.MeanKg()) * 0.3f, 0.3f, 3.5f);
                if (r.cls == "jelly") { Vector2 cq = v.ToCanvas(q); DrawCircleLines((int)cq.x, (int)cq.y, 1.5f + len, c); continue; }
                FishMark(v, q, hd, len, len * (r.cls == "reptile" ? 0.7f : 0.28f), c);
                if (r.tier >= 4 && a.p.z < 2.5f) {   // a fin at the lantern's edge
                    Vector2 cq = v.ToCanvas(Vector2Add(q, Vector2Scale(hd, len * 0.1f)));
                    DrawTriangle({cq.x, cq.y - 4}, {cq.x - 3, cq.y + 1}, {cq.x + 3, cq.y + 1}, Fade(Color{40, 44, 50, 255}, 0.95f));
                    DrawTriangle({cq.x, cq.y - 4}, {cq.x + 3, cq.y + 1}, {cq.x - 3, cq.y + 1}, Fade(Color{40, 44, 50, 255}, 0.95f));
                }
            }
        }
    }
}

// ---------------------------------------------------------------- rods, lines, lures, fish
void DrawLines(const Gannet& g, const View& v) {
    const Boat& b = g.boat;
    Color lineC{210, 210, 190, 255}, rodC{120, 86, 50, 255};
    for (const auto& r : g.rods) {
        const StationDef& sd = Stations()[r.station];
        Vector2 tip = r.TipDeck();
        bool fighting = r.state == RodState::Fighting;
        float bend = fighting ? std::clamp(r.fight.tension / TackleOf(r.tackle).strength, 0.0f, 1.2f) : 0;
        // the line's far end in the boat frame
        Vector2 end = tip;
        if (r.state == RodState::Out) end = b.ToDeck({r.lure.x, r.lure.y});
        if (fighting) end = b.ToDeck({r.fight.p.x, r.fight.p.y});
        Vector2 dir = Vector2Subtract(end, tip);
        if (Vector2Length(dir) > 0.01f) dir = Vector2Normalize(dir); else dir = Vector2Normalize(Vector2Subtract(tip, sd.at));
        // the rod: from its holder to the tip, bowed toward the line as the tension loads it
        Vector2 base = Vector2Lerp(sd.at, tip, 0.15f);
        Vector2 bentTip = Vector2Add(tip, Vector2Scale(dir, 0.35f * bend));
        float tick = r.state == RodState::Out ? r.lastTick * 0.25f : 0;
        bentTip = Vector2Add(bentTip, Vector2Scale(dir, tick * sinf(g.time * 40)));
        Vector2 mid = Vector2Add(Vector2Lerp(base, bentTip, 0.5f), Vector2Scale(dir, 0.18f * bend));
        float lr = 0.4f + 0.6f * v.LightAt(tip);
        DrawLineEx(v.ToCanvas(base), v.ToCanvas(mid), 2, Dim(rodC, lr));
        DrawLineEx(v.ToCanvas(mid), v.ToCanvas(bentTip), 1, Dim(rodC, lr));
        if (r.state == RodState::Charging) {
            Vector2 a = Vector2Add(tip, Vector2Scale(r.aim, 1.0f + r.charge * TackleOf(r.tackle).cast));
            for (float k = 0.1f; k < 1; k += 0.1f) { Vector2 q = Vector2Lerp(tip, a, k); Vector2 c = v.ToCanvas(q); DrawPixel((int)c.x, (int)c.y, Fade(lineC, 0.5f)); }
        }
        if (r.state == RodState::Out) {
            // the line to the float, the float and its rings; a bite makes it bob
            float lit = std::max(0.15f, v.LightAt(end));
            DrawLineV(v.ToCanvas(bentTip), v.ToCanvas(end), Fade(Dim(lineC, lit), 0.7f));
            Vector2 c = v.ToCanvas(end);
            float bob = r.lastTick;
            DrawRectangle((int)c.x - 1, (int)c.y - 1, 2, 2, Dim(Color{230, 90, 60, 255}, 0.4f + 0.6f * lit));
            if (bob > 0.05f) DrawCircleLines((int)c.x, (int)c.y, 2 + bob * 3 + fmodf(g.time * 6, 2), Fade(Dim(lineC, lit), 0.6f));
        }
        if (fighting) {
            const Fight& f = r.fight;
            // the line: the Verlet nodes, fading as they go down into the dark
            Vector2 prev = v.ToCanvas(bentTip);
            for (size_t i = 1; i < f.node.size(); i++) {
                Vector2 d = b.ToDeck({f.node[i].x, f.node[i].y});
                float depth = std::max(0.0f, f.node[i].z);
                float lit = std::max(0.12f, v.LightAt(d)) * std::clamp(1 - depth / 30, 0.25f, 1.0f);
                Color lc = f.tension > 0.9f * f.Strength() ? Color{240, 120, 90, 255} : lineC;
                Vector2 c = v.ToCanvas(d);
                DrawLineV(prev, c, Fade(Dim(lc, lit), f.tension < 0.05f * TackleOf(f.tackle).strength ? 0.35f : 0.9f));
                prev = c;
            }
            // the fish: a shadow under the water, bright when it jumps, rolling on its side when beaten
            Vector2 fd = end;
            bool underHull = fabsf(fd.y) < 3.0f && fd.x > -12 && fd.x < 11 && f.jumpT < 0;
            if (underHull) continue;
            float len = std::clamp(0.35f + sqrtf(f.spec.kg) * 0.22f, 0.4f, 4.5f), wid = len * 0.28f;
            Vector2 hd = Vector2Normalize(b.ToDeck(Vector2Add(b.ToWorld({0, 0}), Vector2{f.h.x, f.h.y})));
            if (Vector2Length(hd) < 0.1f) hd = dir;
            float lit = std::max(0.1f, v.LightAt(fd));
            float under = std::clamp(1 - f.p.z / 12, 0.15f, 1.0f);
            bool air = f.jumpT >= 0;
            Color fc = air ? Color{190, 210, 220, 255} : Dim(Color{30, 50, 60, 255}, 0.6f + 0.4f * lit);
            Vector2 cc = v.ToCanvas(fd);
            float alpha = air ? 1.0f : 0.35f + 0.55f * under;
            // a tapered body along its heading, a forked tail, and a bill on the billfish
            Vector2 hc = hd;
            for (int k = 0; k <= 10; k++) {
                float tt = k / 10.0f;
                Vector2 q = Vector2Add(cc, Vector2Scale(hc, len * v.ppm * (0.5f - tt)));
                float rad = std::max(0.6f, wid * v.ppm * 0.5f * sinf(PI * (0.12f + tt * 0.8f)));
                DrawCircleV(q, rad, Fade(fc, alpha));
            }
            Vector2 tb = Vector2Add(cc, Vector2Scale(hc, -len * v.ppm * 0.5f)), sd2{-hc.y, hc.x};
            Vector2 t1 = Vector2Add(tb, Vector2Add(Vector2Scale(hc, -wid * v.ppm * 0.9f), Vector2Scale(sd2, wid * v.ppm * 0.8f)));
            Vector2 t2 = Vector2Add(tb, Vector2Add(Vector2Scale(hc, -wid * v.ppm * 0.9f), Vector2Scale(sd2, -wid * v.ppm * 0.8f)));
            DrawTriangle(tb, t1, t2, Fade(fc, alpha)); DrawTriangle(tb, t2, t1, Fade(fc, alpha));
            if (std::string(f.spec.name) == "marlin") DrawLineEx(Vector2Add(cc, Vector2Scale(hc, len * v.ppm * 0.5f)), Vector2Add(cc, Vector2Scale(hc, len * v.ppm * 0.8f)), 1, Fade(fc, alpha));
            if (air || (f.p.z < 1 && f.effort > 1)) {
                for (int k = 0; k < 8; k++) {
                    float ang = k * 0.785f + g.time * 2;
                    float rr = (air ? f.jumpT * 3 : 1.0f) * v.ppm * 0.5f + 2;
                    DrawPixel((int)(cc.x + cosf(ang) * rr), (int)(cc.y + sinf(ang) * rr), Fade(Color{220, 230, 230, 255}, 0.4f + 0.5f * lit));
                }
            }
        }
    }
}

// ---------------------------------------------------------------- the crew's things in the water: shots, shot fish,
// the net, set gear, the life ring, hands overboard (world positions drawn in her frame)
void DrawGear(const Gannet& g, const View& v) {
    const Boat& b = g.boat;
    auto C = [&](Vector2 w) { return v.ToCanvas(b.ToDeck(w)); };
    auto lit = [&](Vector2 w) { return std::max(0.15f, v.LightAt(b.ToDeck(w))); };
    // the net: two warps from the stern gallows to the mouth, the mesh behind, fading with depth
    const Trawl& n = g.net;
    if ((n.state == NetState::Down || n.state == NetState::Snagged || n.state == NetState::Hauling) && n.meshInit) {
        Vector2 s1 = b.ToWorld({-11.2f, -1.2f}), s2 = b.ToWorld({-11.2f, 1.2f});
        Color warp = n.state == NetState::Snagged ? Color{240, 120, 90, 255} : Color{160, 150, 130, 255};
        DrawLineV(C(s1), C({n.node[0].x, n.node[0].y}), Fade(warp, 0.8f));
        DrawLineV(C(s2), C({n.node[7].x, n.node[7].y}), Fade(warp, 0.8f));
        for (int j = 0; j < 6; j++) for (int i = 0; i < 8; i++) {
            Vector3 q = n.node[j * 8 + i];
            float a = std::clamp(0.7f - q.z / 16, 0.12f, 0.7f) * (0.4f + 0.6f * lit({q.x, q.y}));
            Color mc = Fade(Color{150, 160, 150, 255}, a);
            if (i < 7) { Vector3 r = n.node[j * 8 + i + 1]; DrawLineV(C({q.x, q.y}), C({r.x, r.y}), mc); }
            if (j < 5) { Vector3 r = n.node[(j + 1) * 8 + i]; DrawLineV(C({q.x, q.y}), C({r.x, r.y}), mc); }
        }
        // the cod end's bulk as it fills
        Vector3 cod = Vector3Lerp(n.node[5 * 8 + 3], n.node[5 * 8 + 4], 0.5f);
        float r = 1 + std::min(8.0f, n.load / 60);
        DrawCircleV(C({cod.x, cod.y}), r, Fade(Color{120, 130, 120, 255}, 0.35f));
    }
    // set gear: longline buoys (red flags with a lamp) and the line between; pot floats
    for (const auto& L : g.longlines) {
        Vector2 a = C(L.a), c2 = C(L.b);
        for (int k = 0; k <= 20; k++) { Vector2 q = Vector2Lerp(a, c2, k / 20.0f); DrawPixel((int)q.x, (int)q.y, Fade(Color{200, 190, 160, 255}, 0.4f)); }
        for (Vector2 q : {a, c2}) {
            DrawRectangle((int)q.x - 1, (int)q.y - 1, 3, 3, Color{200, 50, 40, 255});
            if (fmodf(g.time, 1.6f) < 0.8f) DrawCircleV(q, 3, Fade(Color{255, 120, 90, 255}, 0.25f));
        }
    }
    for (const auto& p : g.pots) { Vector2 q = C(p.p); DrawRectangle((int)q.x - 1, (int)q.y - 1, 3, 3, Color{230, 200, 60, 255}); }
    // shot fish afloat: belly-up, a slick of blood round them
    for (const auto& f : g.floaters) {
        Vector2 q = C(f.p); float L = lit(f.p);
        DrawCircleV(q, 2.5f + std::min(4.0f, f.kg * 0.1f), Fade(Color{120, 20, 20, 255}, 0.25f));
        float len = std::clamp(2.0f + sqrtf(f.kg) * 2, 2.0f, 12.0f);
        DrawRectangle((int)(q.x - len / 2), (int)q.y - 1, (int)len, 2, Dim(Color{220, 220, 205, 255}, L));
    }
    // a bird making off with a fish (its shadow below, the fish hanging from it): shoot it down
    for (const auto& th : g.thieves) {
        Vector2 q = C(th.p); float up = -th.z * 0.8f;
        DrawEllipse((int)q.x, (int)q.y, 4, 2, Fade(BLACK, 0.25f));
        Vector2 b2{q.x, q.y - up};
        float w = th.kind == BIRD_PELICAN ? 5 : th.kind == BIRD_FRIGATE ? 6 : 3;
        Color bc = th.kind == BIRD_PELICAN ? Color{170, 135, 100, 255} : th.kind == BIRD_FRIGATE ? Color{30, 30, 36, 255} : WHITE;
        float flap = sinf(g.time * 8) > 0 ? 1.5f : 0.0f;
        DrawLineV({b2.x - w, b2.y - flap}, b2, bc); DrawLineV(b2, {b2.x + w, b2.y - flap}, bc);
        DrawRectangle((int)b2.x - 1, (int)b2.y + 1, 2, (int)std::clamp(2 + sqrtf(th.fish.kg) * 2, 2.0f, 6.0f), Color{205, 210, 200, 255});
    }
    // blood on the planking, running to the scuppers on the low side (design doc v2, "Blood through the scuppers")
    if (g.deckBlood > 0.3f && !g.moored) {
        float k = std::clamp(g.deckBlood / 12, 0.1f, 1.0f);
        float side = g.boat.roll >= 0 ? 1.0f : -1.0f;
        for (int s = 0; s < 6; s++) {
            Vector2 a{-9.0f + s * 1.1f, side * 1.2f}, b{a.x + 0.3f, side * 2.8f};
            DrawLineEx(v.ToCanvas(a), v.ToCanvas(b), 1.0f + k * 1.5f, Fade(Color{110, 18, 16, 255}, 0.35f * k));
        }
    }
    // fish on the deck (not yet gutted): where they came aboard; a live one arches and slaps toward the rail, a
    // dead one lies still with a smear of blood under it
    for (size_t i = 0; i < g.hold.size(); i++) {
        const CatchRec& h = g.hold[i];
        if (h.gutted || g.moored || h.crated) continue;   // (crated fish are under the crates' lids)
        float L = std::max(0.25f, v.LightAt(h.deckAt));
        float len = std::clamp(0.25f + sqrtf(std::max(0.01f, h.kg)) * 0.32f, 0.25f, 1.8f);
        float base = h.heading;
        float wig = h.dead ? 0.0f : sinf(g.time * 11 + i * 1.7f) * 0.45f * (0.4f + 0.6f * fabsf(sinf(g.time * 1.3f + i)));
        Vector2 dir{cosf(base + wig), sinf(base + wig)};
        if (h.dead) DrawCircleV(v.ToCanvas(h.deckAt), std::max(1.0f, len * v.ppm * 0.45f), Fade(Color{110, 20, 18, 255}, 0.45f));
        Color fc = h.bycatch || h.protectedSp ? Color{150, 170, 120, 255} : Color{200, 205, 210, 255};
        FishMark(v, h.deckAt, dir, len, len * 0.3f, Dim(h.dead ? ColorLerp(fc, Color{120, 110, 100, 255}, 0.4f) : fc, L));
    }
    // projectiles: a round's streak, pellets, a spear or harpoon (and its tether), a flare arcing
    for (const auto& p : g.shots) {
        Vector2 q = C({p.p.x, p.p.y});
        switch (p.kind) {
            case Shot::Bullet: case Shot::Pellet: { Vector2 t = C({p.p.x - p.v.x * 0.012f, p.p.y - p.v.y * 0.012f}); DrawLineV(t, q, Fade(Color{255, 230, 170, 255}, p.inWater ? 0.3f : 0.9f)); break; }
            case Shot::Spear: case Shot::Harpoon: {
                Vector2 t = C({p.p.x - p.v.x * 0.04f, p.p.y - p.v.y * 0.04f});
                DrawLineEx(t, q, 1, Color{190, 190, 200, 255});
                break;
            }
            case Shot::Flare: DrawCircleV(q, 2, Color{255, 90, 60, 255}); DrawCircleV(q, 6, Fade(Color{255, 120, 80, 255}, 0.2f)); break;
            case Shot::Charge: DrawRectangle((int)q.x - 1, (int)q.y - 1, 3, 3, Fade(Color{60, 60, 64, 255}, p.p.z > 0 ? 0.5f : 1.0f)); break;
            default: break;
        }
    }
    for (const auto& fl : g.flares) { Vector2 q = C(fl.p); DrawCircleV(q, 2, Color{255, 110, 70, 255}); DrawCircleV(q, 5 + sinf(g.time * 20) * 1.5f, Fade(Color{255, 140, 90, 255}, 0.25f)); }
    // the Weeds: a Siren on her rock (a pale figure, her song spreading in rings), mermen splashing at the cod end
    if (g.siren.on) {
        Vector2 q = C(g.siren.p);
        DrawCircleV(q, 5, Color{48, 46, 44, 255});
        DrawCircleV({q.x, q.y - 3}, 1.6f, Color{225, 230, 235, 255});
        DrawLineEx({q.x, q.y - 2}, {q.x + 1, q.y + 2}, 2, Color{205, 215, 222, 255});
        for (int k = 0; k < 3; k++) { float u = fmodf(g.time * 0.6f + k / 3.0f, 1.0f); DrawCircleLinesV(q, 6 + u * 40, Fade(Color{190, 220, 230, 255}, 0.35f * (1 - u))); }
    }
    if (g.mermen.on) {
        Vector2 q = C(g.mermen.p);
        for (int k = 0; k < 5; k++) {
            float a = k * 1.3f + g.time * 2.1f, r = 3 + 3 * fabsf(sinf(g.time * 5 + k));
            Vector2 s{q.x + cosf(a) * r * 1.6f, q.y + sinf(a) * r};
            DrawCircleV(s, 1.2f + fabsf(sinf(g.time * 9 + k)) * 1.3f, Fade(Color{220, 235, 240, 255}, 0.7f));
        }
        if (fmodf(g.time, 1.6f) < 0.35f) DrawLineEx({q.x - 3, q.y}, {q.x + 2, q.y - 3}, 2, Color{150, 170, 160, 255});   // a pale arm over the floats
    }
    // the Grotto: the Angler's second light, the Ghost Worm's pale coils, isopods over the bow, the Drowned on the deck
    if (g.angler.on) {
        Vector2 q = C(g.angler.p); float pulse = 0.7f + 0.3f * sinf(g.time * 3.1f);
        DrawCircleV(q, 9 * pulse, Fade(Color{150, 230, 200, 255}, 0.12f)); DrawCircleV(q, 3, Fade(Color{200, 255, 230, 255}, 0.9f * pulse));
        DrawCircleV({q.x - 4, q.y + 3}, 4, Fade(Color{20, 26, 24, 255}, 0.6f));   // the dark bulk under the lure
    }
    if (g.worm.state > 0) {
        float rad = g.worm.state == 3 ? 4.5f : 20;
        for (int k = 0; k < 26; k++) {
            float a = g.worm.ang - k * (g.worm.state == 3 ? 0.26f : 0.07f);
            Vector2 wp = g.worm.state == 3 ? Vector2Add(g.boat.pos, {cosf(a) * rad * 2.4f, sinf(a) * rad}) : Vector2Add(g.boat.pos, {cosf(a) * rad, sinf(a) * rad});
            Vector2 q = C(wp);
            DrawCircleV(q, std::max(1.0f, 4.0f - k * 0.12f), Fade(Color{196, 200, 190, 255}, (g.worm.state == 1 ? 0.3f : 0.6f) * (1 - k / 30.0f)));
        }
    }
    if (g.isopods.state == 2) for (int k = 0; k < std::min(g.isopods.n, 30); k++) {
        float ax = 8.5f - fmodf(k * 1.37f + g.time * 0.2f * (k % 3 + 1), 12.0f), ay = sinf(k * 2.1f + g.time * 0.5f) * 1.8f;
        Vector2 q = v.ToCanvas({ax, ay}); DrawRectangle((int)q.x, (int)q.y, 2, 1, Color{200, 196, 176, 255});
    } else if (g.isopods.state == 1) { Vector2 q = v.ToCanvas({10.4f, 0}); for (int k = 0; k < 5; k++) DrawPixel((int)q.x + 2 + k, (int)q.y + (k % 2), Color{200, 196, 176, 255}); }
    // Atlantis: the Pale Eye far below (a vast pale glow, blinking), the Choir's singer, a cult longboat's torches, the
    // Ghost Ship alongside, the Kraken's arms over the rail
    if (g.eco && g.eco->ground == "atlantis") {
        Vector2 q = C(g.eco->eyeP);
        float open = g.eyeBlinkT > 0 ? 0.15f : 1.0f, pulse = 0.85f + 0.15f * sinf(g.time * 0.7f);
        for (int r = 6; r >= 1; r--) DrawCircleV(q, r * 22.0f * open, Fade(Color{200, 214, 220, 255}, 0.035f * pulse * (g.eyeLooked ? 1.6f : 1.0f)));
        DrawCircleV(q, 10 * open, Fade(Color{20, 24, 30, 255}, 0.5f));   // its pupil
    }
    if (g.choir.on && g.choir.surfaced) { Vector2 q = C(g.choir.singer); DrawCircleV(q, 2.5f, Color{220, 228, 232, 255}); for (int k = 0; k < 3; k++) { float u = fmodf(g.time * 0.5f + k / 3.0f, 1.0f); DrawCircleLinesV(q, 4 + u * 30, Fade(Color{190, 220, 230, 255}, 0.3f * (1 - u))); } }
    if (g.longboat.on) {
        Vector2 q = C(g.longboat.p); float a = g.longboat.ang + PI / 2;
        Vector2 dir{cosf(a), sinf(a)}, side{-dir.y, dir.x};
        Vector2 bow = Vector2Add(q, Vector2Scale(dir, 12)), stern = Vector2Subtract(q, Vector2Scale(dir, 12));
        DrawLineEx(bow, stern, 6, Color{40, 30, 24, 255});
        for (int k = -1; k <= 1; k += 2) { Vector2 t = Vector2Add(Vector2Add(q, Vector2Scale(dir, k * 9.0f)), Vector2Scale(side, 3)); DrawCircleV(t, 2, Color{255, 170, 60, 255}); DrawCircleV(t, 7 + sinf(g.time * 9 + k) * 1.5f, Fade(Color{255, 150, 60, 255}, 0.15f)); }
    }
    if (g.ghost.state > 0) {
        Vector2 q = C(g.ghost.p); Vector2 f = g.boat.Forward(); Vector2 dir = Vector2Normalize({f.x, f.y});
        Vector2 a = Vector2Add(q, Vector2Scale(dir, 40)), b = Vector2Subtract(q, Vector2Scale(dir, 40));
        DrawLineEx(a, b, 18, Fade(Color{60, 70, 66, 255}, g.ghost.state == 1 ? 0.4f : 0.85f));
        DrawCircleV(q, 3, Color{150, 230, 190, 255}); DrawCircleV(q, 14, Fade(Color{150, 230, 190, 255}, 0.12f));   // its lantern, cold green
    }
    if (g.kraken.state == 2) for (int arm = 0; arm < 3; arm++) {
        float x = -6 + arm * 6.0f + sinf(g.time * 0.4f + arm) * 1.5f, side = arm % 2 ? 1.0f : -1.0f;
        for (int k = 0; k < 12; k++) {
            float u = k / 11.0f; Vector2 d{x + sinf(g.time * 1.3f + arm + u * 4) * 0.8f, side * (6.0f - u * 4.5f)};
            DrawCircleV(v.ToCanvas(d), (1 - u) * 5 + 1.5f, Color{130, 40, 40, 255});
        }
    }
    for (const auto& d : g.drowned) {
        Vector2 q = v.ToCanvas(d.p); float sway = sinf(g.time * 1.4f + d.p.x) * 1.0f;
        Color body = d.hitT > 0 ? Color{220, 220, 200, 255} : Color{82, 96, 86, 255};
        DrawRectangle((int)(q.x - 3 + sway), (int)q.y - 3, 7, 7, body);
        DrawRectangle((int)(q.x - 2 + sway), (int)q.y - 5, 5, 3, Color{120, 130, 118, 255});
        DrawPixel((int)(q.x + sway), (int)q.y - 4, Color{200, 230, 210, 255});   // its eyes catching the lamp
        if (d.grab >= 0 && d.grab < (int)g.crew.size()) DrawLineV(q, v.ToCanvas(g.crew[d.grab].p), Color{120, 140, 120, 255});
    }
    // the harpoon's tether while something big is fast on it
    if (g.harpoon.state == RodState::Fighting) DrawLineV(v.ToCanvas({10.2f, 0}), C({g.harpoon.fight.p.x, g.harpoon.fight.p.y}), Color{170, 170, 180, 255});
    // life rings and their ropes
    for (const auto& r : g.rings) if (r.state != 0) {
        Vector2 q = C(r.p);
        if (r.thrower >= 0 && r.thrower < (int)g.crew.size()) DrawLineV(v.ToCanvas(g.crew[r.thrower].p), q, Fade(Color{220, 200, 150, 255}, 0.7f));
        DrawCircleLines((int)q.x, (int)q.y, 2.5f, Color{240, 110, 50, 255});
        DrawPixel((int)q.x + 2, (int)q.y, WHITE); DrawPixel((int)q.x - 2, (int)q.y, WHITE);
    }
    // hands in the water: a head and two splashing arms
    for (const auto& c : g.crew) if (c.overboard && !c.dead) {
        Vector2 q = C(c.swim); float L = lit(c.swim);
        DrawCircleV(q, 5 + sinf(g.time * 5) * 1.5f, Fade(Color{220, 230, 230, 255}, 0.15f));
        DrawRectangle((int)q.x - 1, (int)q.y - 1, 3, 3, Dim(RoleColor(c.role), L));
        float s1 = sinf(g.time * 9 + c.slot);
        DrawPixel((int)(q.x - 3), (int)(q.y + s1), Dim(Color{214, 170, 130, 255}, L));
        DrawPixel((int)(q.x + 3), (int)(q.y - s1), Dim(Color{214, 170, 130, 255}, L));
    }
}

// ---------------------------------------------------------------- the harbour quay (moored)
void DrawQuay(const Gannet& g, const View& v) {
    if (!g.moored) return;
    auto lit = [&](Vector2 d) { return 0.18f + 0.82f * v.LightAt(d); };
    // the island's sand behind, the quay's planks, the pilings along the water
    for (float y = -16; y < -9.6f; y += 0.5f) for (float x = -18; x < 18; x += 0.5f) {
        Vector2 d{x, y};
        float h = H01((int)(x * 2), (int)(y * 2), 41);
        Px(v, d, 0.5f, 0.5f, Dim(Mix(Color{120, 108, 78, 255}, Color{96, 86, 60, 255}, h), lit(d)));
        if (h < 0.03f) Px(v, d, 0.25f, 0.25f, Dim(Color{60, 90, 50, 255}, lit(d)));   // tufts of dune grass
    }
    for (float y = -9.6f; y < -3.9f; y += 0.35f) {
        Color pl = ((int)((y + 20) / 0.35f)) % 2 ? Color{104, 80, 54, 255} : Color{92, 70, 46, 255};
        for (float x = -13.5f; x < 13.5f; x += 0.5f) Px(v, {x, y}, 0.5f, 0.33f, Dim(pl, lit({x, y})));
    }
    for (float x = -13.2f; x < 13.5f; x += 2.2f) PxC(v, {x, -3.95f}, 0.35f, 0.35f, Dim(Color{60, 44, 30, 255}, lit({x, -3.95f})));   // pilings
    // the gangplank to her rail, the mooring lines to the bollards
    for (float y = -3.9f; y < -2.6f; y += 0.25f) Px(v, {-0.9f, y}, 1.8f, 0.2f, Dim(Color{130, 100, 64, 255}, lit({0, y})));
    for (float bx : {-9.5f, 8.5f}) {
        PxC(v, {bx, -4.5f}, 0.5f, 0.5f, Dim(Color{50, 50, 54, 255}, lit({bx, -4.5f})));
        Vector2 a = v.ToCanvas({bx, -4.5f}), b = v.ToCanvas({bx > 0 ? 9.0f : -9.5f, -2.8f});
        DrawLineV(a, b, Dim(Color{170, 150, 110, 255}, lit({bx, -4})));
    }
    // the stations: a chalkboard, the Chandler's counter, the market scales, the Owners' office, the Slipway's ramp
    for (const auto& st : DockStations()) {
        Vector2 p = st.at; float L = lit(p);
        switch (st.kind) {
            case DockKind::Chalkboard:
                PxC(v, {p.x, p.y - 0.6f}, 2.2f, 1.3f, Dim(Color{70, 50, 32, 255}, L));
                PxC(v, {p.x, p.y - 0.6f}, 1.9f, 1.0f, Dim(Color{30, 40, 34, 255}, L));
                for (int k = 0; k < 3; k++) PxC(v, {p.x - 0.4f + k * 0.3f, p.y - 0.75f + k * 0.2f}, 0.8f - k * 0.2f, 0.06f, Dim(Color{220, 220, 210, 255}, L));
                break;
            case DockKind::Chandler:
                PxC(v, {p.x, p.y - 0.9f}, 3.4f, 1.6f, Dim(Color{120, 74, 50, 255}, L));      // the shed
                PxC(v, {p.x, p.y + 0.1f}, 3.0f, 0.5f, Dim(Color{150, 116, 70, 255}, L));     // the counter
                for (int k = 0; k < 4; k++) PxC(v, {p.x - 1 + k * 0.65f, p.y - 1.2f}, 0.4f, 0.4f, Dim(k % 2 ? Color{90, 110, 130, 255} : Color{160, 150, 90, 255}, L));   // tins and ropes
                break;
            case DockKind::Market:
                PxC(v, {p.x, p.y - 0.9f}, 3.6f, 1.6f, Dim(Color{110, 110, 116, 255}, L));    // the market's slab roof
                PxC(v, {p.x, p.y + 0.1f}, 1.2f, 0.6f, Dim(Color{180, 150, 70, 255}, L));     // the brass scales
                PxC(v, {p.x - 1.2f, p.y + 0.1f}, 0.9f, 0.6f, Dim(Color{170, 190, 200, 255}, L));   // a crate of ice
                break;
            case DockKind::Gunsmith:
                PxC(v, {p.x, p.y - 0.9f}, 2.6f, 1.5f, Dim(Color{60, 56, 52, 255}, L));       // the gun shop, iron-shuttered
                PxC(v, {p.x, p.y + 0.1f}, 2.2f, 0.45f, Dim(Color{110, 80, 52, 255}, L));     // the counter
                for (int k = 0; k < 3; k++) PxC(v, {p.x - 0.7f + k * 0.7f, p.y - 1.1f}, 0.12f, 0.9f, Dim(Color{150, 150, 156, 255}, L));   // long guns on the rack
                break;
            case DockKind::Scales:
                PxC(v, {p.x, p.y - 0.9f}, 2.4f, 1.4f, Dim(Color{70, 62, 74, 255}, L));       // the Owners' weighhouse
                PxC(v, {p.x, p.y + 0.1f}, 1.6f, 0.7f, Dim(Color{200, 170, 80, 255}, L));     // the great brass beam scales
                PxC(v, {p.x, p.y - 0.4f}, 0.2f, 0.9f, Dim(Color{150, 120, 60, 255}, L));     // the post
                break;
            case DockKind::Office:
                PxC(v, {p.x, p.y - 0.9f}, 3.0f, 1.8f, Dim(Color{80, 70, 84, 255}, L));
                PxC(v, {p.x, p.y + 0.05f}, 0.8f, 0.3f, Dim(Color{230, 200, 120, 255}, 0.4f + 0.6f * L));   // the lit window
                break;
            case DockKind::Slipway:
                for (int k = 0; k < 6; k++) PxC(v, {p.x - 0.6f + k * 0.25f, p.y}, 0.15f, 2.4f, Dim(Color{70, 60, 50, 255}, L));   // the ramp's rails
                PxC(v, {p.x + 0.9f, p.y - 1.0f}, 0.6f, 0.6f, Dim(Color{90, 80, 70, 255}, L));   // the winch
                break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------- the boat
static float HalfBeamArt(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }

void DrawBoat(const Gannet& g, const View& v) {
    const Boat& b = g.boat;
    float t = g.time;
    Color hull{70, 26, 24, 255}, hullDk{40, 14, 14, 255}, rail{150, 120, 80, 255};
    Color plank{112, 84, 56, 255}, plankDk{92, 68, 44, 255}, iron{64, 68, 70, 255}, brass{200, 160, 70, 255};
    // foam where the swell meets the hull, most on the low side
    for (float x = -11.0f; x < 10.8f; x += 0.25f) for (int side = -1; side <= 1; side += 2) {
        float hb = HalfBeamArt(x) + 0.15f;
        Vector2 d{x, side * hb};
        float low = side * b.RollDeg() > 0 ? 1.0f : 0.3f;
        if (H01((int)(x * 4), side, (int)(t * 5)) < 0.35f * low + 0.1f) Px(v, d, 0.25f, 0.2f, Dim(Color{180, 204, 204, 255}, 0.2f + 0.6f * v.LightAt(d)));
    }
    // the hull, stepped plate by plate (the bow's taper in half-metre steps), a dark waterline shadow on the low side
    float lowSide = b.roll >= 0 ? 1.0f : -1.0f;
    for (float x = -11.0f; x < 10.8f; x += 0.5f) {
        float hb = HalfBeamArt(x + 0.25f);
        Px(v, {x, -hb - 0.25f + (lowSide < 0 ? -0.1f : 0.0f) * fabsf(b.RollDeg()) / 10}, 0.5f, 2 * hb + 0.5f + fabsf(b.RollDeg()) / 100, Dim(hullDk, 0.5f + 0.5f * v.LightAt({x, 0})));
        Px(v, {x, -hb}, 0.5f, 2 * hb, Dim(hull, 0.35f + 0.65f * v.LightAt({x, 0})));
        // the deck, planked fore and aft
        float dhb = hb - 0.3f;
        if (dhb <= 0.1f) continue;
        for (float y = -dhb; y < dhb - 0.01f; y += 0.25f) {
            int row = (int)((y + 3) * 4);
            bool seam = ((int)((x + 11) * 2) + row * 3) % 7 == 0;
            Color c = row % 2 ? plank : plankDk;
            if (seam) c = Dim(c, 0.8f);
            float wet = b.bilge > 0 ? 0 : 0;
            (void)wet;
            Px(v, {x, y}, 0.5f, std::min(0.25f, dhb - y), Dim(c, 0.3f + 0.7f * v.LightAt({x + 0.25f, y})));
        }
        // the rails
        Px(v, {x, -dhb - 0.1f}, 0.5f, 0.12f, Dim(rail, 0.3f + 0.7f * v.LightAt({x, -dhb})));
        Px(v, {x, dhb - 0.02f}, 0.5f, 0.12f, Dim(rail, 0.3f + 0.7f * v.LightAt({x, dhb})));
    }
    auto L = [&](Vector2 d) { return 0.3f + 0.7f * v.LightAt(d); };
    // the stern gantry and the net drum
    PxC(v, {-10.6f, 0}, 0.3f, 5.4f, Dim(iron, L({-10.6f, 0})));
    PxC(v, {-9.4f, 0}, 1.0f, 2.4f, Dim(Color{80, 70, 56, 255}, L({-9.4f, 0})));
    for (int k = -4; k <= 4; k++) PxC(v, {-9.4f, k * 0.25f}, 0.9f, 0.06f, Dim(Color{120, 110, 80, 255}, L({-9.4f, 0})));
    // the hold hatch and the engine room hatch with its ladder
    PxC(v, {-2.2f, -0.9f}, 1.6f, 1.2f, Dim(Color{60, 48, 34, 255}, L({-2.2f, -0.9f})));
    PxC(v, {-2.2f, -0.9f}, 1.3f, 0.9f, Dim(Color{78, 62, 44, 255}, L({-2.2f, -0.9f})));
    PxC(v, {-3.4f, -1.8f}, 0.8f, 0.8f, Dim(Color{24, 20, 18, 255}, L({-3.4f, -1.8f})));
    for (int k = 0; k < 3; k++) PxC(v, {-3.4f, -2.05f + k * 0.25f}, 0.6f, 0.06f, Dim(Color{140, 120, 80, 255}, L({-3.4f, -1.8f})));
    // the gutting table
    PxC(v, {-2.2f, 2.2f}, 1.4f, 0.7f, Dim(Color{150, 150, 140, 255}, L({-2.2f, 2.2f})));
    // the air pump beside the diving gantry, the stern rods, the side rods in their holders
    PxC(v, {-6.2f, 2.65f}, 0.6f, 0.4f, Dim(brass, L({-6.2f, 2.65f})));
    for (Vector2 r : {Vector2{-10.4f, -2.1f}, Vector2{-10.4f, 2.1f}, Vector2{-0.8f, -2.8f}, Vector2{-0.8f, 2.8f}}) PxC(v, r, 0.2f, 0.2f, Dim(Color{40, 36, 30, 255}, L(r)));
    // the harpoon cannon on the bow
    PxC(v, {9.6f, 0}, 0.8f, 0.8f, Dim(iron, L({9.6f, 0})));
    Px(v, {9.8f, -0.1f}, 1.0f, 0.2f, Dim(Color{40, 44, 46, 255}, L({9.6f, 0})));
    // the bell
    PxC(v, {6.0f, 2.05f}, 0.35f, 0.35f, Dim(brass, L({6, 2})));
    // the wheelhouse: a roof from outside; cut away (its floor, the helm, the sonar and the printer) from inside
    Color wood{98, 76, 58, 255}, roof{60, 64, 62, 255};
    if (v.inWheelhouse) {
        PxC(v, {3.0f, 0}, 4.2f, 4.2f, Dim(wood, L({3, 0})));
        PxC(v, {3.0f, 0}, 3.8f, 3.8f, Dim(Color{120, 96, 70, 255}, L({3, 0})));
        PxC(v, {1.0f, 0}, 0.2f, 1.4f, Dim(Color{120, 96, 70, 255}, L({1, 0})));   // (the door)
        // the wheel
        Vector2 hc = v.ToCanvas({4.55f, 0});
        float a = b.rudder * 2.5f;
        DrawCircleLines((int)hc.x, (int)hc.y, 0.45f * v.ppm, Dim(brass, L({4.5f, 0})));
        for (int k = 0; k < 4; k++) { float aa = a + k * 0.785f; DrawLine((int)(hc.x - cosf(aa) * 6), (int)(hc.y - sinf(aa) * 6), (int)(hc.x + cosf(aa) * 6), (int)(hc.y + sinf(aa) * 6), Dim(brass, L({4.5f, 0}))); }
        PxC(v, {2.4f, -1.55f}, 0.9f, 0.5f, Dim(Color{30, 50, 40, 255}, L({2.4f, -1.5f})));    // the sonar scope
        PxC(v, {2.4f, -1.55f}, 0.5f, 0.3f, Color{60, (unsigned char)(160 + 60 * sinf(t * 2)), 110, 255});
        PxC(v, {2.4f, 1.55f}, 0.8f, 0.5f, Dim(brass, L({2.4f, 1.5f})));                    // the telegraph printer
        PxC(v, {4.0f, -1.6f}, 0.5f, 0.4f, Dim(Color{180, 170, 150, 255}, L({4, -1.5f})));   // the clock
    } else {
        PxC(v, {3.0f, 0}, 4.3f, 4.3f, Dim(Color{36, 38, 36, 255}, L({3, 0})));
        PxC(v, {3.0f, 0}, 4.0f, 4.0f, Dim(roof, L({3, 0})));
        for (int k = 0; k < 4; k++) PxC(v, {1.4f + k * 1.05f, 0}, 0.08f, 3.8f, Dim(Color{50, 54, 52, 255}, L({3, 0})));
        PxC(v, {4.5f, 1.4f}, 0.3f, 0.3f, Dim(Color{120, 90, 60, 255}, L({4.5f, 1.4f})));   // the funnel's stay
    }
    // the funnel, aft of the wheelhouse, smoking with the fire
    PxC(v, {0.6f, 1.0f}, 0.7f, 0.7f, Dim(Color{30, 28, 28, 255}, L({0.6f, 1})));
    PxC(v, {0.6f, 1.0f}, 0.5f, 0.5f, Color{(unsigned char)(40 + 120 * std::clamp(b.firebox / 6, 0.0f, 1.0f)), 30, 20, 255});
    // the lantern mast: the lamp's housing and its glow
    PxC(v, {0.2f, 0}, 0.5f, 0.5f, Dim(iron, L({0.2f, 0})));
    PxC(v, {0.2f, 0}, 0.3f, 0.3f, Color{255, 220, 140, 255});
    // the hatches in the deck: open (a dark square), shut (a planked cover), battened (bars across it)
    if (v.viewerDeck == 0) for (const auto& H : g.hatches) {
        float k = L(H.at);
        if (H.state == 0) { PxC(v, H.at, 1.0f, 1.0f, Dim(Color{60, 46, 32, 255}, k)); PxC(v, H.at, 0.8f, 0.8f, Color{8, 8, 10, 255}); }
        else {
            PxC(v, H.at, 1.0f, 1.0f, Dim(Color{120, 92, 60, 255}, k));
            for (int s = 0; s < 3; s++) PxC(v, {H.at.x - 0.3f + s * 0.3f, H.at.y}, 0.04f, 0.9f, Dim(Color{86, 64, 42, 255}, k));
            if (H.state == 2) { PxC(v, {H.at.x, H.at.y - 0.25f}, 1.0f, 0.1f, Dim(iron, k)); PxC(v, {H.at.x, H.at.y + 0.25f}, 1.0f, 0.1f, Dim(iron, k)); }
        }
    }
    // the engine room, seen from below: the rest of the deck dark above
    if (v.viewerDeck == 1) {
        DrawRectangle(0, 0, PIXEL_W + 2, PIXEL_H + 2, Fade(BLACK, 0.55f));
        PxC(v, {-5.65f, 0}, 5.8f, 5.0f, Color{36, 32, 30, 255});
        PxC(v, {-5.65f, 0}, 5.5f, 4.6f, Color{58, 52, 46, 255});
        for (int k = 0; k < 11; k++) PxC(v, {-8.2f + k * 0.5f, 0}, 0.04f, 4.6f, Color{50, 44, 40, 255});
        // the boiler: its drum against the port side, the firebox door glowing with the fire, the gauge
        PxC(v, {-4.8f, -1.55f}, 2.0f, 1.4f, Color{70, 64, 60, 255});
        PxC(v, {-4.8f, -1.55f}, 1.7f, 1.1f, Color{88, 80, 74, 255});
        float glow = std::clamp(b.firebox / 6, 0.1f, 1.0f) * (0.8f + 0.2f * sinf(t * 9));
        PxC(v, {-4.8f, -0.85f}, 0.7f, 0.25f, Color{(unsigned char)(120 + 135 * glow), (unsigned char)(50 + 110 * glow), 20, 255});
        Color gauge = b.pressure > D().redAt ? Color{230, 60, 40, 255} : b.pressure > D().greenHi ? Color{230, 190, 60, 255} : b.pressure > D().greenLo ? Color{90, 200, 90, 255} : Color{150, 150, 150, 255};
        PxC(v, {-4.0f, -1.2f}, 0.35f, 0.35f, gauge);
        // the coal bunker, the bilge pump, the water in her
        float coal = std::clamp(b.bunker / (D().sackKg * 6), 0.0f, 1.0f);
        PxC(v, {-7.4f, -1.3f}, 1.4f, 1.6f, Color{30, 28, 26, 255});
        PxC(v, {-7.4f, -1.3f + 0.8f * (1 - coal)}, 1.2f, 1.4f * coal + 0.05f, Color{20, 20, 22, 255});
        PxC(v, {-7.0f, 1.9f}, 0.6f, 0.6f, Color{90, 110, 120, 255});
        if (b.bilge > 50) { float k = std::clamp(b.bilge / 20000, 0.1f, 1.0f); DrawRectangle(0, 0, 0, 0, BLANK); PxC(v, {-5.65f, 0}, 5.4f, 4.5f, Fade(Color{30, 70, 90, 255}, 0.25f + 0.5f * k)); }
        if (b.valveT > 0) for (int k = 0; k < 20; k++) PxC(v, {-4.2f + H01(k, 1, (int)(t * 10)) * 2, -1.6f + H01(k, 2, (int)(t * 10)) * 2}, 0.2f, 0.2f, Fade(WHITE, 0.6f));
        if (b.fireT > 0) for (int k = 0; k < 10; k++) PxC(v, {-5.2f + H01(k, 3, (int)(t * 12)) * 1.2f, -0.6f + H01(k, 4, (int)(t * 12)) * 0.6f}, 0.15f, 0.15f, Color{255, (unsigned char)(120 + 100 * H01(k, 5, (int)(t * 12))), 40, 255});
        // the fish hold, through the watertight door: ice in its pounds, the iced catch, the main hatch's square overhead
        PxC(v, {-1.05f, 0}, 3.9f, 4.8f, Color{36, 32, 30, 255});
        PxC(v, {-1.05f, 0}, 3.6f, 4.5f, Color{52, 58, 62, 255});
        for (int k = 0; k < 3; k++) PxC(v, {-2.2f + k * 1.2f, 1.4f}, 1.0f, 1.2f, Color{170, 200, 215, 255});   // the ice pounds
        int iced = 0; for (const auto& h : g.hold) if (h.gutted && h.iced && !h.junk) iced++;
        for (int k = 0; k < std::min(iced, 12); k++) PxC(v, {-2.4f + (k % 6) * 0.45f, -0.4f - (k / 6) * 0.3f}, 0.35f, 0.12f, Color{190, 196, 200, 255});
        PxC(v, g.hatches[0].at, 0.9f, 0.9f, g.hatches[0].state == 0 ? Color{40, 50, 60, 255} : Color{70, 54, 36, 255});
        // the watertight door in the bulkhead
        PxC(v, {-2.9f, 0}, 0.25f, 4.6f, Color{70, 70, 72, 255});
        PxC(v, {-2.9f, 0}, 0.3f, 1.1f, g.doorOpen ? Color{20, 20, 22, 255} : Color{120, 120, 126, 255});
        if (!g.doorOpen) PxC(v, {-2.9f, 0.3f}, 0.12f, 0.12f, Color{200, 160, 70, 255});   // its dog
        // the fo'c'sle: bunks, the magazine locker, the Medic's cot, the fore hatch overhead
        PxC(v, {7.1f, 0}, 3.9f, 3.6f, Color{36, 32, 30, 255});
        PxC(v, {7.1f, 0}, 3.6f, 3.3f, Color{64, 52, 40, 255});
        for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++) PxC(v, {5.9f + k * 1.2f, s * 1.35f}, 1.0f, 0.5f, Color{120, 100, 74, 255});   // bunks
        PxC(v, Stations()[(int)StationKind::Magazine].at, 0.8f, 0.6f, Color{90, 70, 40, 255});
        PxC(v, Stations()[(int)StationKind::Magazine].at, 0.5f, 0.15f, Color{200, 160, 70, 255});
        PxC(v, Stations()[(int)StationKind::Cot].at, 1.4f, 0.6f, Color{200, 196, 180, 255});
        PxC(v, g.hatches[1].at, 0.9f, 0.9f, g.hatches[1].state == 0 ? Color{40, 50, 60, 255} : Color{70, 54, 36, 255});
        // the oil lamps; a space whose lamp is out goes black
        Vector2 spaceC[3] = {{-5.65f, 0}, {-1.05f, 0}, {7.1f, 0}}; Vector2 spaceS[3] = {{5.5f, 4.6f}, {3.6f, 4.5f}, {3.6f, 3.3f}};
        for (int i = 0; i < 3; i++) {
            if (g.lamps[i].lit) { float fl = 0.8f + 0.2f * sinf(t * 7 + i); PxC(v, g.lamps[i].at, 0.25f, 0.25f, Color{255, (unsigned char)(200 * fl), 120, 255}); }
            else { PxC(v, spaceC[i], spaceS[i].x, spaceS[i].y, Fade(BLACK, i == 0 ? 0.35f : 0.75f)); PxC(v, g.lamps[i].at, 0.2f, 0.2f, Color{60, 50, 40, 255}); }
        }
    }
}

// ---------------------------------------------------------------- the crew (top-down: a hat, shoulders in oilskins, arms)
Color RoleColor(Role r) {
    switch (r) {
        case Role::Bosun: return {40, 56, 90, 255};
        case Role::Angler: return {214, 180, 50, 255};
        case Role::Diver: return {120, 130, 90, 255};
        default: return {220, 220, 210, 255};
    }
}
// A landing from above (the Atoll): its reef shelf, the sand, the little lagoon and its moray, palms, the fire pit
// (flames, the fish on it, smoke going white to grey to black as they cook and burn), the elder's hut and the elder,
// the beached sloop, the caches (a chest, a padlocked strongbox, an X once a map marks it), crabs and what's been set
// down on the sand. Lit by the fire and the lanterns like everything else; under a dim moon otherwise.
void DrawLanding(const Gannet& g, const View& v) {
    for (const auto& L : g.landings) {
        auto C = [&](Vector2 local) { return v.ToCanvas(g.boat.ToDeck(L.ToWorld(local))); };
        auto lit = [&](Vector2 local) { return 0.22f + 0.78f * v.LightAt(g.boat.ToDeck(L.ToWorld(local))); };
        Vector2 c0 = C({0, 0});
        float pr = v.ppm;
        if (c0.x < -L.r * pr * 2 || c0.y < -L.r * pr * 2 || c0.x > PIXEL_W + L.r * pr * 2 || c0.y > PIXEL_H + L.r * pr * 2) continue;
        float l0 = lit({0, 0});
        bool seal = L.kind == LK_SEALROCK, pier = L.kind == LK_CANNERY, shelf = L.kind == LK_SHELF, boneB = L.kind == LK_BONEBEACH;
        bool stair = L.kind == LK_STAIR, tower = L.kind == LK_TOWER, cultL = L.kind == LK_CULT;
        Color ground = seal ? Color{128, 124, 116, 255} : pier ? Color{126, 98, 66, 255} : shelf ? Color{84, 80, 76, 255} : boneB ? Color{58, 54, 52, 255}
                     : stair ? Color{200, 196, 186, 255} : tower ? Color{150, 146, 136, 255} : cultL ? Color{90, 78, 66, 255} : Color{214, 196, 150, 255};
        if (pier) for (int k = 0; k < 20; k++) { float a = k * 0.314f; DrawCircleV(C({cosf(a) * (L.r + 0.3f), sinf(a) * (L.r + 0.3f)}), 2.0f, Dim(Color{58, 46, 34, 255}, l0)); }   // the pilings
        else {
            DrawCircleV(c0, (L.r + 6) * pr, Fade(Dim(seal ? Color{40, 70, 80, 255} : Color{40, 120, 120, 255}, l0), 0.35f));   // the shelf
            DrawCircleV(c0, (L.r + 1.2f) * pr, Fade(Dim(Color{170, 210, 190, 255}, l0), 0.45f));   // surf on the edge
        }
        // the sand (the rock, the planking): lit a square metre at a time (so the fire's pool reads), with a little grain
        DrawCircleV(c0, L.r * pr, Dim(ground, 0.22f));
        for (int gy = -(int)L.r; gy < (int)L.r; gy++) for (int gx = -(int)L.r; gx < (int)L.r; gx++) {
            Vector2 m{gx + 0.5f, gy + 0.5f};
            if (Vector2Length(m) > L.r - 0.4f) continue;
            float k = lit(m);
            if (k < 0.24f) continue;
            float grain = ((gx * 7 + gy * 13) & 3) * 0.02f;
            if (pier) grain = (gy & 1) * 0.05f;   // (planks run across)
            if (seal) grain = ((gx * 5 + gy * 3) % 5) * 0.03f;
            Color sand = Dim(ground, k * (0.96f + grain));
            Vector2 a = C({(float)gx, (float)gy}), b = C({gx + 1.0f, (float)gy}), c = C({gx + 1.0f, gy + 1.0f}), d = C({(float)gx, gy + 1.0f});
            DrawTriangle(a, b, c, sand); DrawTriangle(a, c, b, sand); DrawTriangle(a, c, d, sand); DrawTriangle(a, d, c, sand);
        }
        if (seal) {
            // the haul-out: wet, weed-slick rock where the seals lie; the bull among them
            DrawCircleV(C(L.pond), L.pondR * pr, Dim(Color{70, 84, 74, 255}, lit(L.pond)));
            for (int k = 0; k < 4; k++) {
                Vector2 sp = Vector2Add(L.pond, {cosf(k * 1.7f) * 1.6f, sinf(k * 1.7f) * 1.3f}); Vector2 q = C(sp); float kk = lit(sp);
                DrawEllipse((int)q.x, (int)q.y, 4, 2, Dim(Color{96, 90, 84, 255}, kk)); DrawCircleV({q.x + 3, q.y - 1}, 1.5f, Dim(Color{110, 104, 96, 255}, kk));
            }
            Vector2 bq = C(L.moray); float bk = lit(L.moray);
            DrawEllipse((int)bq.x, (int)bq.y, 6, 3, Dim(Color{70, 62, 56, 255}, bk)); DrawCircleV({bq.x + 5, bq.y - 2 + sinf(g.time * 2) * 0.5f}, 2.2f, Dim(Color{84, 74, 66, 255}, bk));
        } else if (L.kind == LK_ATOLL) {
            // the little lagoon, and the moray's dark shape in it
            DrawCircleV(C(L.pond), L.pondR * pr, Dim(Color{30, 110, 120, 255}, lit(L.pond)));
            DrawCircleLinesV(C(L.pond), L.pondR * pr, Dim(Color{120, 170, 150, 255}, lit(L.pond)));
            for (int k = 0; k < 6; k++) { Vector2 a = Vector2Add(L.moray, {k * 0.18f - 0.45f, sinf(g.time * 3 + k) * 0.12f}); Vector2 q = C(a); DrawRectangle((int)q.x, (int)q.y, 2, 1, Fade(Color{20, 30, 24, 255}, 0.7f)); }
        }
        if (stair) for (int s = 0; s < 6; s++) { Vector2 a = C({-6.0f + s * 1.0f, -7}), b = C({-6.0f + s * 1.0f, 7}); DrawLineV(a, b, Dim(Color{160, 156, 146, 255}, l0)); }   // (the stair's treads)
        if (seal || pier || shelf || boneB || stair || tower || cultL) {
            // the sealers' hut (stone, a turf roof), the cannery shed (corrugated iron), the smugglers' lean-to of crates, a
            // lost crew's upturned boat, the Keeper's shrine, the tower's stump, the cult's tent: walls with a door
            float c = cosf(L.sloopHead), s = sinf(L.sloopHead);
            auto P = [&](float x, float y) { return C(Vector2Add(L.sloop, {x * c - y * s, x * s + y * c})); };
            float k = lit(L.sloop);
            Color roof = Dim(seal ? Color{86, 96, 62, 255} : shelf ? Color{112, 84, 54, 255} : boneB ? Color{70, 56, 44, 255} : stair ? Color{220, 216, 204, 255} : tower ? Color{120, 116, 108, 255} : cultL ? Color{110, 30, 30, 255} : Color{120, 110, 100, 255}, k);
            Color wall = Dim(seal ? Color{96, 92, 86, 255} : stair || tower ? Color{170, 166, 156, 255} : Color{90, 70, 58, 255}, k);
            Vector2 a = P(-2.6f, -1.6f), b = P(2.6f, -1.6f), cc = P(2.6f, 1.6f), d = P(-2.6f, 1.6f);
            DrawTriangle(a, b, cc, roof); DrawTriangle(a, cc, b, roof); DrawTriangle(a, cc, d, roof); DrawTriangle(a, d, cc, roof);
            DrawLineV(a, b, wall); DrawLineV(b, cc, wall); DrawLineV(cc, d, wall); DrawLineV(d, a, wall);
            if (pier) for (int r = -2; r <= 2; r++) DrawLineV(P((float)r, -1.6f), P((float)r, 1.6f), Dim(Color{100, 92, 84, 255}, k));   // the corrugations
            DrawLineEx(P(-0.7f, 1.6f), P(0.7f, 1.6f), 2, Dim(Color{30, 24, 20, 255}, k));   // the door
        } else {
        // the sloop on her side: a dark hull with a stove-in stern
            float c = cosf(L.sloopHead), s = sinf(L.sloopHead);
            auto P = [&](float x, float y) { return C(Vector2Add(L.sloop, {x * c - y * s, x * s + y * c})); };
            Color wood = Dim(Color{92, 66, 44, 255}, lit(L.sloop)), dark = Dim(Color{50, 36, 26, 255}, lit(L.sloop));
            Vector2 a = P(2.5f, 0), b = P(1.4f, 0.9f), d = P(-1.6f, 0.9f), e = P(-1.6f, -0.9f), f = P(1.4f, -0.9f);
            DrawTriangle(a, b, f, wood); DrawTriangle(a, f, b, wood);
            DrawTriangle(b, d, e, wood); DrawTriangle(b, e, d, wood); DrawTriangle(b, e, f, wood); DrawTriangle(b, f, e, wood);
            DrawLineV(a, b, dark); DrawLineV(b, d, dark); DrawLineV(e, f, dark); DrawLineV(f, a, dark);
            DrawLineV(P(0.3f, -0.9f), P(0.6f, 2.2f), Dim(Color{120, 100, 70, 255}, lit(L.sloop)));   // her mast, fallen across the sand
        }
        // the elder's hut (thatch) and the elder before it, feathers in his hair
        if (tower) {}   // (nobody keeps the Watchtower)
        else if (seal || pier || shelf || boneB || stair || cultL) {
            // Old Hoskins in his oilskins and sou'wester; the foreman in a leather apron and a cap; the quartermaster in a
            // long coat and a red scarf; the hermit in rags and a bone necklace; the Keeper, drowned and pale in his
            // vestments; the cult quartermaster, hooded in red
            Vector2 e = C(L.elder); float k = lit(L.elder);
            Color coat = seal ? Color{170, 150, 60, 255} : shelf ? Color{40, 40, 50, 255} : boneB ? Color{110, 100, 84, 255} : stair ? Color{150, 176, 170, 255} : cultL ? Color{120, 26, 26, 255} : Color{90, 64, 44, 255};
            Color hat = seal ? Color{190, 170, 70, 255} : shelf ? Color{170, 40, 36, 255} : boneB ? Color{220, 214, 196, 255} : stair ? Color{200, 220, 210, 255} : cultL ? Color{90, 20, 20, 255} : Color{50, 50, 56, 255};
            DrawRectangle((int)e.x - 3, (int)e.y - 3, 7, 7, Dim(coat, k));
            DrawRectangle((int)e.x - 2, (int)e.y - 2, 5, 5, Dim(Color{200, 160, 130, 255}, k));
            DrawRectangle((int)e.x - 3, (int)e.y - 4, 7, 2, Dim(hat, k));
        } else {
            Vector2 h = C(Vector2Add(L.elder, {0, -1.6f}));
            DrawRectangle((int)h.x - 9, (int)h.y - 7, 18, 14, Dim(Color{150, 120, 70, 255}, lit(L.elder)));
            for (int k = 0; k < 5; k++) DrawLineV({h.x - 9 + k * 4.0f, h.y - 7}, {h.x - 7 + k * 4.0f, h.y + 7}, Dim(Color{120, 92, 50, 255}, lit(L.elder)));
            Vector2 e = C(L.elder); float k = lit(L.elder);
            DrawRectangle((int)e.x - 3, (int)e.y - 3, 7, 7, Dim(Color{110, 60, 40, 255}, k));
            DrawRectangle((int)e.x - 2, (int)e.y - 2, 5, 5, Dim(Color{150, 100, 70, 255}, k));
            DrawRectangle((int)e.x - 1, (int)e.y - 5, 1, 2, Dim(Color{230, 60, 40, 255}, k)); DrawRectangle((int)e.x + 1, (int)e.y - 5, 1, 2, Dim(Color{240, 220, 120, 255}, k));
        }
        // palms: a trunk and a star of fronds, swaying
        for (size_t i = 0; i < L.palms.size(); i++) {
            Vector2 p = C(L.palms[i]); float k = lit(L.palms[i]);
            DrawCircleV(p, 1.5f, Dim(Color{110, 80, 50, 255}, k));
            for (int f = 0; f < 6; f++) { float a = f * 1.047f + sinf(g.time * 0.8f + i) * 0.08f; DrawLineEx(p, {p.x + cosf(a) * 9, p.y + sinf(a) * 9}, 2, Dim(Color{60, 120, 60, 255}, k)); }
        }
        // the caches
        for (const auto& k : L.caches) {
            if (k.open) { Vector2 q = C(k.p); DrawRectangleLines((int)q.x - 3, (int)q.y - 2, 7, 5, Fade(Dim(Color{90, 70, 40, 255}, lit(k.p)), 0.6f)); continue; }
            if (k.kind == 2) { if (k.found) { Vector2 q = C(k.p); Color x = Color{200, 40, 30, 255}; DrawLineV({q.x - 3, q.y - 3}, {q.x + 3, q.y + 3}, x); DrawLineV({q.x - 3, q.y + 3}, {q.x + 3, q.y - 3}, x); } continue; }
            Vector2 q = C(k.p); float kk = lit(k.p);
            DrawRectangle((int)q.x - 3, (int)q.y - 2, 7, 5, Dim(Color{120, 80, 40, 255}, kk));
            DrawRectangle((int)q.x - 3, (int)q.y - 1, 7, 1, Dim(Color{200, 160, 70, 255}, kk));
            if (k.kind == 1) DrawRectangle((int)q.x, (int)q.y + 1, 1, 2, Dim(Color{230, 200, 90, 255}, kk));   // the padlock
        }
        // crabs, sidling
        for (size_t i = 0; i < L.crabs.size(); i++) { Vector2 q = C(L.crabs[i]); Color cc = Dim(Color{200, 90, 60, 255}, lit(L.crabs[i])); DrawRectangle((int)q.x - 1, (int)q.y, 3, 2, cc); if (fmodf(g.time * 4 + i, 1) < 0.5f) { DrawPixel((int)q.x - 2, (int)q.y + 1, cc); DrawPixel((int)q.x + 2, (int)q.y + 1, cc); } }
        // what lies on the sand
        for (const auto& b : L.onBeach) { Vector2 q = C(b.deckAt); DrawRectangle((int)q.x - 2, (int)q.y, b.junk ? 4 : 5, 2, b.junk ? Dim(Color{120, 80, 40, 255}, lit(b.deckAt)) : Dim(Color{200, 205, 210, 255}, lit(b.deckAt))); }
        // the fire pit: a ring of stones, flames when lit, the fish on it, and its smoke
        Vector2 fc = C(L.fire);
        if (pier) {   // the cannery boiler: a riveted drum with a stack, its firebox door glowing
            DrawCircleV(fc, 8, Dim(Color{70, 62, 58, 255}, lit(L.fire))); DrawCircleLinesV(fc, 8, Dim(Color{40, 36, 34, 255}, lit(L.fire)));
            DrawCircleV({fc.x + 3, fc.y - 3}, 2.5f, Color{30, 28, 28, 255});
            if (L.fireLit) DrawRectangle((int)fc.x - 3, (int)fc.y + 5, 6, 3, Color{255, 150, 60, 255});
        } else if (boneB) {   // the volcanic vents: cracks in the black sand, steam always rising
            DrawCircleV(fc, 5, Dim(Color{90, 50, 30, 255}, lit(L.fire))); DrawCircleV(fc, 2, Color{230, 120, 50, 255});
            for (int k = 0; k < 5; k++) { float t = fmodf(g.time * 0.4f + k * 0.2f, 1.0f); DrawCircleV({fc.x + sinf(t * 7 + k) * 3, fc.y - t * 20}, 2 + t * 4, Fade(Color{220, 220, 220, 255}, 0.3f * (1 - t))); }
        } else if (stair) {   // the eternal brazier: a bronze bowl, a pale flame that never goes out
            DrawCircleV(fc, 5, Dim(Color{140, 110, 60, 255}, lit(L.fire)));
            for (int k = 0; k < 5; k++) { float fl = sinf(g.time * 7 + k * 1.3f); DrawRectangle((int)(fc.x - 2 + k), (int)(fc.y - 2 - fabsf(fl) * 4), 1, 3, Color{190, 240, 220, 255}); }
            DrawCircleV(fc, 12, Fade(Color{170, 240, 210, 255}, 0.12f));
        } else if (seal || shelf) {   // the hut's (the smugglers') iron stove by its door
            DrawRectangle((int)fc.x - 4, (int)fc.y - 3, 8, 6, Dim(Color{50, 48, 46, 255}, lit(L.fire)));
            if (L.fireLit) DrawRectangle((int)fc.x - 2, (int)fc.y + 1, 4, 2, Color{255, 150, 60, 255});
        } else
        for (int k = 0; k < 8; k++) { float a = k * 0.785f; DrawRectangle((int)(fc.x + cosf(a) * 6) - 1, (int)(fc.y + sinf(a) * 6) - 1, 2, 2, Dim(Color{110, 110, 104, 255}, lit(L.fire))); }
        if (L.fireLit && (L.kind == LK_ATOLL || L.kind == LK_TOWER || L.kind == LK_CULT)) {   // (an open fire: the Atoll's pit, the tower's signal fire, the cult's bonfire)
            for (int k = 0; k < 7; k++) { float fl = sinf(g.time * 11 + k * 1.7f); DrawRectangle((int)(fc.x - 3 + k), (int)(fc.y - 1 - fabsf(fl) * 3), 1, 2 + (int)(fabsf(fl) * 2), k % 2 ? Color{255, 200, 80, 255} : Color{240, 110, 40, 255}); }
            DrawCircleV(fc, 9, Fade(Color{255, 160, 70, 255}, 0.12f));
        } else if (L.kind == LK_ATOLL || L.kind == LK_TOWER || L.kind == LK_CULT) DrawRectangle((int)fc.x - 2, (int)fc.y - 1, 4, 2, Color{40, 38, 36, 255});
        else if (L.fireLit) DrawCircleV(fc, 10, Fade(Color{255, 160, 70, 255}, 0.10f));
        float worst = 0;
        for (size_t i = 0; i < L.onFire.size(); i++) {
            const CatchRec& r = L.onFire[i];
            float T = 10 + r.kg; worst = std::max(worst, r.cookT / (T + 10));
            Color fish = r.cookT > T + 5 ? Color{60, 44, 30, 255} : r.cookT > T ? Color{200, 140, 70, 255} : Color{200, 200, 196, 255};
            DrawRectangle((int)fc.x - 4 + (int)i * 2, (int)fc.y - 2 + (int)i, 6, 2, fish);
        }
        if (!L.onFire.empty() && L.fireLit) {
            Color smoke = worst > 0.75f ? Color{30, 30, 30, 255} : worst > 0.5f ? Color{120, 120, 120, 255} : Color{220, 220, 220, 255};
            for (int k = 0; k < 6; k++) { float t = fmodf(g.time * 0.5f + k * 0.17f, 1.0f); Vector2 q{fc.x + t * 18 + sinf(t * 9 + k) * 2, fc.y - t * 24}; DrawCircleV(q, 2 + t * 4, Fade(smoke, 0.35f * (1 - t))); }
        }
    }
}
// The skiff from above: a painted clinker hull, red sheer strake, thwarts and her bow lantern; the oars sweep with
// each rower's stroke; keel up when capsized; hung over the Gannet's stern when stowed (drawn in her frame like
// everything else: the skiff's own points to the sea, then onto the deck's canvas)
void DrawSkiff(const Gannet& g, const View& v) {
    const Skiff& s = g.skiff;
    if (s.state == SkiffState::Lost) return;
    auto C = [&](Vector2 local) { return v.ToCanvas(g.boat.ToDeck(s.ToWorld(local))); };
    auto hb = [](float x) { float u = (x + 2.25f) / 4.5f; return 0.8f * (u < 0.65f ? 0.88f + 0.12f * sinf(u / 0.65f * PI * 0.5f) : cosf((u - 0.65f) / 0.35f * PI * 0.5f) * 0.97f + 0.03f); };
    float L = 0.3f + 0.7f * std::max(v.LightAt(g.boat.ToDeck(s.p)), s.Up() ? 0.6f : 0.0f);
    bool keel = s.state == SkiffState::Capsized;
    Color hull = Dim(keel ? Color{70, 66, 60, 255} : Color{226, 222, 206, 255}, L), band = Dim(Color{150, 46, 38, 255}, L);
    Color wood = Dim(Color{140, 100, 62, 255}, L), dark = Dim(Color{60, 44, 30, 255}, L);
    auto tri = [](Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col); DrawTriangle(a, c, b, col); };
    // the hull outline as a strip of quads from transom to stem
    const int N = 10;
    for (int k = 0; k < N; k++) {
        float x0 = -2.25f + k * 0.45f, x1 = x0 + 0.45f;
        Vector2 a = C({x0, -hb(x0)}), b = C({x1, -hb(x1)}), c = C({x1, hb(x1)}), d = C({x0, hb(x0)});
        tri(a, b, c, hull); tri(a, c, d, hull);
        if (!keel) {
            Vector2 ai = C({x0, -hb(x0) + 0.12f}), bi = C({x1, -hb(x1) + 0.12f}), ci = C({x1, hb(x1) - 0.12f}), di = C({x0, hb(x0) - 0.12f});
            tri(ai, bi, ci, wood); tri(ai, ci, di, wood);
            DrawLineV(a, b, band); DrawLineV(d, c, band);
        } else DrawLineV(C({x0, 0}), C({x1, 0}), Dim(Color{40, 36, 32, 255}, L));   // the keel
    }
    if (!keel) {
        for (float x : {0.2f, -1.3f, 1.3f}) DrawLineEx(C({x, -hb(x) + 0.1f}), C({x, hb(x) - 0.1f}), 2, dark);   // thwarts
        // the oars
        for (const auto& c : g.crew) {
            if (c.deck != DECK_SKIFF || c.overboard || c.dead) continue;
            float ph = std::clamp(c.oarT / D().skiffStroke, 0.0f, 1.0f);
            float sweep = ph < 0.45f ? 0.55f - ph / 0.45f * 1.1f : -0.55f + (ph - 0.45f) / 0.55f * 1.1f;
            for (int sd = -1; sd <= 1; sd += 2) {
                Vector2 lock{0.2f, sd * hb(0.2f)};
                Vector2 dir{sinf(sweep), (float)sd * cosf(sweep)};
                Vector2 in = Vector2Subtract(lock, Vector2Scale(dir, 0.6f)), out = Vector2Add(lock, Vector2Scale(dir, 1.9f));
                DrawLineEx(C(in), C(out), 1, Dim(Color{180, 140, 90, 255}, L));
                Vector2 blade = C(out); DrawRectangle((int)blade.x - 1, (int)blade.y - 1, 2, 2, Dim(Color{200, 160, 100, 255}, L));
            }
        }
        // what she carries
        for (size_t i = 0; i < s.load.size() && i < 10; i++) {
            Vector2 q = C({-0.5f - (float)(i % 3) * 0.35f, -0.25f + (float)(i / 3) * 0.22f});
            DrawRectangle((int)q.x - 1, (int)q.y, 3, 1, s.load[i].junk ? Dim(Color{120, 110, 90, 255}, L) : Dim(Color{200, 205, 210, 255}, L));
        }
        if (s.Up()) { Vector2 lp = C({1.95f, 0}); DrawRectangle((int)lp.x - 1, (int)lp.y - 1, 2, 2, Color{255, 220, 150, 255}); DrawCircleV(lp, 3, Fade(Color{255, 210, 130, 255}, 0.2f)); }
        // her line: the rod over the starboard quarter, the line to the lure or the fish on it
        const Rod& r = g.skiffRod;
        bool lineUp = false; for (const auto& c : g.crew) if (c.deck == DECK_SKIFF && c.skiffLine && !c.overboard) lineUp = true;
        if (lineUp || r.state != RodState::Idle) {
            float bend = r.state == RodState::Fighting ? std::clamp(r.fight.tension / TackleOf(r.tackle).strength, 0.0f, 1.0f) : 0;
            Vector2 base = C({-1.2f, 0.5f}), tip = v.ToCanvas(g.boat.ToDeck(g.SkiffRodTip()));
            DrawLineEx(base, tip, 1, Dim(Color{120, 86, 50, 255}, L));
            if (r.state == RodState::Out || r.state == RodState::Fighting) {
                Vector2 end = v.ToCanvas(g.boat.ToDeck(r.state == RodState::Fighting ? Vector2{r.fight.p.x, r.fight.p.y} : Vector2{r.lure.x, r.lure.y}));
                DrawLineV(tip, end, Fade(Color{220, 220, 200, 255}, 0.5f + 0.4f * bend));
                if (r.state == RodState::Fighting) DrawCircleV(end, 2, Fade(Color{220, 230, 240, 255}, 0.8f));
                else DrawRectangle((int)end.x, (int)end.y, 1, 1, Color{240, 120, 80, 255});
            }
        }
    }
    // the tow line: what's made fast alongside her quarter
    if (s.Up()) for (size_t i = 0; i < g.towed.size(); i++) {
        Vector2 a = C({-2.25f, 0}), b = C({-3.2f - i * 0.9f, 0.3f * (i % 2 ? 1 : -1)});
        DrawLineV(a, b, Fade(Color{200, 190, 160, 255}, 0.6f));
        float len = std::clamp(2.0f + sqrtf(g.towed[i].kg) * 1.6f, 3.0f, 14.0f);
        DrawRectangle((int)(b.x - len / 2), (int)b.y - 1, (int)len, 3, Fade(Color{170, 176, 180, 255}, 0.8f));
        DrawCircleV(b, len * 0.4f, Fade(Color{120, 20, 20, 255}, 0.18f));   // (it bleeds)
    }
    // the davit's falls while she's lowered or hauled up
    if (s.state == SkiffState::Lowering || s.state == SkiffState::Recovering || s.state == SkiffState::Stowed)
        for (int sd = -1; sd <= 1; sd += 2) DrawLineV(v.ToCanvas({-10.6f, sd * 0.5f}), C({sd * 1.6f, 0}), Fade(Color{200, 190, 160, 255}, 0.6f));
}
void DrawCrewMember(const Crew& c, const View& v, float t, bool you) {
    if (c.overboard) return;
    if (c.deck != v.viewerDeck && v.viewerDeck == 0) return;     // (below decks, out of sight)
    Vector2 p = v.ToCanvas(c.p);
    if (c.dead) {
        // a ghost: a translucent figure, cold and pale, that drifts a little
        float w = sinf(t * 2 + c.slot) * 1.0f;
        DrawRectangle((int)p.x - 3, (int)(p.y - 3 + w), 7, 7, Fade(Color{170, 210, 230, 255}, 0.25f));
        DrawRectangle((int)p.x - 2, (int)(p.y - 2 + w), 5, 5, Fade(Color{210, 235, 245, 255}, 0.3f));
        if (you) DrawRectangleLines((int)p.x - 5, (int)(p.y - 5 + w), 11, 11, Fade(Color{190, 230, 250, 255}, 0.3f));
        return;
    }
    float k = 0.35f + 0.65f * v.LightAt(c.p);
    Color coat = Dim(RoleColor(c.role), k), dark = Dim(Color{30, 26, 22, 255}, k), skin = Dim(Color{214, 170, 130, 255}, k);
    // the player's Wardrobe (Crew::skin, Crew::costume, as the boat has them): the coat and the hat's brim take the skin's
    // colours, a costume its own; the role's colour stays at the crown so the role still reads
    if (const skins::Skin* sk = skins::Find(skins::TRAWL, c.skin)) { coat = Dim(sk->top, k); dark = Dim(sk->hat, k * 0.8f); }
    if (const skins::Costume* co = skins::FindCostume(skins::TRAWL, c.costume)) coat = Dim(co->sleeve, k);
    if (c.fallen) {   // flat on the deck
        DrawRectangle((int)p.x - 4, (int)p.y - 2, 9, 5, coat);
        DrawRectangle((int)p.x + 3, (int)p.y - 1, 3, 3, skin);
        return;
    }
    float bob = c.station < 0 && Vector2Length(c.v) > 0.3f ? (sinf(t * 12) > 0 ? 1.0f : 0.0f) : 0;
    Vector2 f = c.facing;
    // a jump lifts the figure up the canvas and leaves its shadow on the deck (shrinking as they rise)
    Vector2 p0 = p; float lift = c.z * v.ppm * 0.7f; p.y -= lift;
    // shadow, shoulders, arms toward the facing, the sou'wester hat
    DrawRectangle((int)p0.x - 3, (int)p0.y - 3 + 1, 7, 7, Fade(BLACK, c.z > 0 ? 0.2f : 0.35f));
    if (c.tangleT > 0)   // a Kelp Wraith's grip: wet strands wound round them from the rail, writhing
        for (int s = 0; s < 4; s++) { float a = s * 1.57f + sinf(t * 3 + s) * 0.3f; DrawLineEx({p0.x + cosf(a) * 2, p0.y + sinf(a) * 2}, {p0.x + cosf(a) * 7, p0.y + sinf(a) * 7 + 1}, 1.5f, Color{60, 110, 60, 255}); }
    DrawRectangle((int)p.x - 3, (int)p.y - 3, 7, 7, coat);
    DrawRectangle((int)(p.x + f.x * 3 - 1), (int)(p.y + f.y * 3 - 1 + bob), 2, 2, skin);
    DrawRectangle((int)(p.x - f.y * 3), (int)(p.y + f.x * 3), 2, 2, Dim(coat, 0.8f));
    DrawRectangle((int)(p.x + f.y * 3 - 1), (int)(p.y - f.x * 3 - 1), 2, 2, Dim(coat, 0.8f));
    // what's in hand, held out along the facing: a gaff or priest swings down and across on a use (Crew::cool), a
    // gun kicks back on a shot and dips while reloading, a flare pistol's barrel is short and fat
    if (c.station < 0) {
        Item it = DrawItemOf(c.slots[c.sel]);   // (a catalogue weapon is drawn as its nearest kind)
        float sw = 0;                                   // the swing: 0..1 through the arc
        if ((it == Item::Gaff || it == Item::Priest || it == Item::Knife) && c.cool > 0) sw = 1 - c.cool / (it == Item::Gaff ? 0.5f : 0.4f);
        bool gun = it == Item::Rifle || it == Item::Shotgun || it == Item::Speargun || it == Item::Flare;
        float kick = gun && c.cool > (it == Item::Rifle ? 1.0f : it == Item::Shotgun ? 0.6f : it == Item::Speargun ? 1.8f : 0.85f) ? 1.0f : 0.0f;
        float dip = gun && c.reloadT > 0 ? 1.0f : 0.0f;
        Vector2 d = f;
        if (sw > 0) { float ang = (0.5f - sw) * 1.9f; d = Vector2Rotate(f, ang); }   // from raised to struck through
        Vector2 side{-f.y, f.x};
        Vector2 h = Vector2Add(p, Vector2Scale(side, 2.5f));       // the right hand
        h = Vector2Add(h, Vector2Scale(f, 1.5f - kick * 1.5f + dip * 0.5f));
        Color steel = Dim(Color{170, 176, 180, 255}, k), wood = Dim(Color{120, 84, 46, 255}, k), brass = Dim(Color{200, 160, 70, 255}, k);
        auto seg = [&](Vector2 from, float len, Color col, int w) { Vector2 to = Vector2Add(from, Vector2Scale(d, len)); DrawLineEx(from, to, (float)w, col); };
        switch (it) {
            case Item::Gaff: seg(h, 7, wood, 1); { Vector2 tip = Vector2Add(h, Vector2Scale(d, 7)); DrawLineEx(tip, Vector2Add(tip, Vector2Scale(Vector2Rotate(d, 1.9f), 2)), 1, steel); } break;
            case Item::Priest: seg(h, 4, wood, 2); break;
            case Item::Knife: seg(h, 3, steel, 1); break;
            case Item::Rifle: seg(h, 2, wood, 2); seg(Vector2Add(h, Vector2Scale(d, 2)), 6, steel, 1); if (kick > 0) DrawCircleV(Vector2Add(h, Vector2Scale(d, 8.5f)), 1.5f, Fade(Color{255, 230, 150, 255}, 0.9f)); break;
            case Item::Shotgun: seg(h, 2, wood, 2); seg(Vector2Add(h, Vector2Scale(d, 2)), 5, steel, 2); if (kick > 0) DrawCircleV(Vector2Add(h, Vector2Scale(d, 7.5f)), 2, Fade(Color{255, 220, 140, 255}, 0.9f)); break;
            case Item::Speargun: seg(h, 7, wood, 1); if (c.slots[c.sel].ammo > 0 && dip == 0) seg(Vector2Add(h, Vector2Scale(side, -1)), 9, steel, 1); break;
            case Item::Flare: seg(h, 3, brass, 2); break;
            case Item::Ring: DrawCircleLines((int)(h.x + d.x * 2), (int)(h.y + d.y * 2), 3, Dim(Color{230, 120, 60, 255}, k)); break;
            case Item::Charge: DrawRectangle((int)(h.x + d.x * 2) - 1, (int)(h.y + d.y * 2) - 1, 3, 3, Dim(Color{60, 62, 66, 255}, k)); break;
            default: break;
        }
    }
    DrawRectangle((int)p.x - 2, (int)p.y - 2, 5, 5, dark);
    DrawRectangle((int)p.x - 1, (int)p.y - 1, 3, 3, Dim(RoleColor(c.role), k * 0.8f));
    if (you) DrawRectangleLines((int)p.x - 5, (int)p.y - 5, 11, 11, Fade(Color{255, 240, 200, 255}, 0.25f + 0.2f * sinf(t * 4)));
    if (c.Has(INJ_BITE) && fmodf(t, 0.7f) < 0.35f) DrawPixel((int)p.x + 2, (int)p.y + 5, Color{150, 20, 20, 255});   // a bleeding hand leaves drops
    if (c.Has(INJ_BROKEN_ARM)) DrawRectangle((int)(p.x - f.y * 3), (int)(p.y + f.x * 3) + 2, 2, 2, Dim(coat, 0.6f));    // the arm hangs
}

} // namespace tw
