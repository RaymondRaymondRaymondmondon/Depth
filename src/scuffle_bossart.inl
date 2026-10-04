// Scuffle stage 8: the bosses' look (included by scuffle_game.cpp). Inked like everything else on the stage: a black
// outline under every shape, flat colour, a little shading. Drawn from the boss's own state (its parts, chain, marks
// and numbers), so a guest's mirror draws exactly what the host sees. The tells: dust and a growing shadow under
// anything about to fall, a grate that rattles and bubbles, a beam's thin red line, a hatched band where an attack
// will land.

namespace bossart {
float Px(float m) { return m * S.zoom; }
// an inked circle, ellipse, line and triangle (the outline first, then the fill)
void Ball(Vector2 w, float r, Color c, float ink = 2.5f) { Vector2 p = W2S(w); DrawCircleV(p, Px(r) + ink, INK); DrawCircleV(p, Px(r), c); }
void Oval(Vector2 w, float rx, float ry, Color c, float ink = 2.5f) { Vector2 p = W2S(w); DrawEllipse((int)p.x, (int)p.y, Px(rx) + ink, Px(ry) + ink, INK); DrawEllipse((int)p.x, (int)p.y, Px(rx), Px(ry), c); }
void Limb(Vector2 a, Vector2 b, float wdt, Color c) { Vector2 A = W2S(a), B = W2S(b); DrawLineEx(A, B, Px(wdt) + 5, INK); DrawCircleV(A, Px(wdt) / 2 + 2.5f, INK); DrawCircleV(B, Px(wdt) / 2 + 2.5f, INK); DrawLineEx(A, B, Px(wdt), c); DrawCircleV(A, Px(wdt) / 2, c); DrawCircleV(B, Px(wdt) / 2, c); }
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) { Vector2 A = W2S(a), B = W2S(b), C = W2S(c); Vector2 m{(A.x + B.x + C.x) / 3, (A.y + B.y + C.y) / 3}; auto grow = [&](Vector2 p) { Vector2 d = Vector2Subtract(p, m); float L = Vector2Length(d); return L > 0 ? Vector2Add(p, Vector2Scale(d, 3 / L)) : p; }; DrawTri(grow(A), grow(B), grow(C), INK); DrawTri(A, B, C, col); }
Color Flash(Color c, const sf::Boss& B) { if (B.hitT > 0) c = ColorLerp(c, WHITE, 0.3f); if (B.roarT > 0) c = ColorLerp(c, Color{255, 80, 60, 255}, 0.25f * (0.5f + 0.5f * sinf(S.t * 30))); if (B.dead) c = ColorLerp(c, Color{90, 86, 80, 255}, std::min(1.0f, B.deadT)); return c; }
Color Fade(Color c, const sf::Boss& B) { if (B.dead) c.a = (unsigned char)(c.a * std::clamp(1.4f - B.deadT * 0.6f, 0.0f, 1.0f)); return c; }

// where an attack is coming: a hatched, pulsing band (red-brown ink, light enough to see the fight through)
void Danger(const sf::Boss& B) {
    float pulse = 0.5f + 0.5f * sinf(S.t * 12);
    for (const auto& r : B.danger) {
        Vector2 a = W2S({r.x, r.y + r.height}), b = W2S({r.x + r.width, r.y});
        Rectangle q{a.x, a.y, b.x - a.x, b.y - a.y};
        DrawRectangleRec(q, Color{190, 50, 30, (unsigned char)(28 + 22 * pulse)});
        BeginScissorMode((int)q.x, (int)q.y, (int)q.width + 1, (int)q.height + 1);
        for (float x = q.x - q.height; x < q.x + q.width; x += 14) DrawLineEx({x, q.y + q.height}, {x + q.height, q.y}, 1.5f, Color{150, 40, 24, (unsigned char)(60 + 40 * pulse)});
        EndScissorMode();
        DrawLineEx({q.x, q.y + q.height}, {q.x + q.width, q.y + q.height}, 3, Color{170, 40, 20, (unsigned char)(150 + 80 * pulse)});
    }
    // falling things: dust from the roof, and a shadow on the floor that sharpens as it comes
    for (const auto& m : B.marks) {
        float u = std::clamp(1 - m.z / 0.9f, 0.0f, 1.0f);
        Vector2 top = W2S({m.x, m.y}), floor = W2S({m.x, 0.62f});
        for (int i = 0; i < 4; i++) { float yy = top.y + fmodf(S.t * 160 + i * 23, 60); DrawCircleV({top.x + sinf(i * 3.1f + S.t * 5) * 6, yy}, 2, Color{120, 110, 96, 200}); }
        DrawEllipse((int)floor.x, (int)floor.y, Px(0.3f + 0.4f * u), Px(0.07f + 0.05f * u), ColorAlpha(INK, 0.25f + 0.45f * u));
    }
}

void Lobster(const sf::Boss& B) {
    Vector2 P = B.pos; float bob = sinf(B.t * 2.2f) * 0.06f, sink = B.dead ? std::min(1.2f, B.deadT * 0.6f) : 0;
    Color shell = Flash(Color{196, 66, 40, 255}, B), dark = Flash(Color{140, 40, 26, 255}, B), pale = Flash(Color{236, 150, 104, 255}, B);
    auto at = [&](float x, float y) { return Vector2{x, y - sink}; };
    // legs (four pairs, stepping when it shuffles), the tail fanned to the right, the antennae sweeping out front
    for (int i = 0; i < 4; i++) { float ph = B.t * 6 + i * 1.3f, x = P.x - 0.5f + i * 0.55f; Limb(at(x, 1.1f + bob), at(x - 0.35f + sinf(ph) * 0.12f, 0.95f), 0.12f, dark); Limb(at(x - 0.35f + sinf(ph) * 0.12f, 0.95f), at(x - 0.5f + sinf(ph) * 0.18f, 0.62f), 0.1f, dark); }
    for (int i = 0; i < 3; i++) Oval(at(P.x + 1.85f + i * 0.55f, 1.15f - i * 0.08f), 0.38f, 0.36f - i * 0.04f, i % 2 ? shell : dark);
    Tri(at(P.x + 3.2f, 1.1f), at(P.x + 3.9f, 1.55f), at(P.x + 3.9f, 0.7f), pale);
    Oval(at(P.x + 1.1f, 1.45f + bob), 0.95f, 0.8f, shell); Oval(at(P.x, 1.62f + bob), 1.08f, 0.95f, shell);
    for (int i = 0; i < 3; i++) { Vector2 a = W2S(at(P.x - 0.6f + i * 0.7f, 2.4f + bob)), b = W2S(at(P.x - 0.4f + i * 0.7f, 0.95f + bob)); DrawLineEx(a, b, 2.5f, dark); }   // (the shell's bands)
    for (int s : {-1, 1}) { Vector2 a = at(P.x - 1.2f, 2.0f + bob), m = at(P.x - 3.0f, 3.6f + s * 0.4f + sinf(B.t * 1.4f + s) * 0.3f), e = at(P.x - 5.2f, 2.6f + s * 0.8f + sinf(B.t * 1.1f + s * 2) * 0.5f); DrawLineEx(W2S(a), W2S(m), 2.5f, INK); DrawLineEx(W2S(m), W2S(e), 2, INK); }
    Oval(at(P.x - 1.05f, 1.75f + bob), 0.55f, 0.48f, pale);
    // the eyes on their stalks: the weak point (they glint)
    for (int i = 0; i < 2; i++) { Vector2 e = at(P.x - 1.25f + i * 0.4f, 2.75f + (i ? 0.1f : 0) + bob); Limb(at(P.x - 1.05f + i * 0.2f, 2.0f + bob), e, 0.1f, dark); Ball(e, 0.2f, Color{30, 26, 24, 255}); DrawCircleV(Vector2Add(W2S(e), {-Px(0.06f), -Px(0.07f)}), Px(0.06f), WHITE); }
    // the claws: the great crusher on its arm, the small pincer
    if (B.chain.size() >= 2) {
        Vector2 c = {B.chain[0].x, B.chain[0].y - sink}, p = {B.chain[1].x, B.chain[1].y - sink}, sh = at(P.x - 0.8f, 1.4f + bob);
        Vector2 el = {(sh.x + c.x) / 2 + 0.2f, std::max(sh.y, c.y) + 0.5f};
        Limb(sh, el, 0.3f, shell); Limb(el, c, 0.26f, shell);
        float open = 0.25f + 0.2f * sinf(B.t * 3);
        Oval(c, 0.62f, 0.45f, shell);
        Tri({c.x - 0.3f, c.y + 0.15f}, {c.x - 1.15f, c.y + 0.3f + open}, {c.x - 0.4f, c.y + 0.45f}, pale);
        Tri({c.x - 0.3f, c.y - 0.15f}, {c.x - 1.05f, c.y - 0.3f - open}, {c.x - 0.4f, c.y - 0.4f}, pale);
        Limb(at(P.x - 0.9f, 2.0f + bob), p, 0.16f, shell); Oval(p, 0.36f, 0.26f, shell); Tri({p.x - 0.2f, p.y}, {p.x - 0.7f, p.y + 0.2f}, {p.x - 0.7f, p.y - 0.15f}, pale);
    }
}

void Kraken(const sf::Boss& B) {
    Color flesh = Flash(Color{150, 44, 64, 255}, B), under = Flash(Color{226, 160, 160, 255}, B), dark = Flash(Color{96, 26, 44, 255}, B);
    // the arms (each ten points from below the gunwale to the tip), suckers along the underside
    for (size_t a = 0; a + 1 < B.chain.size(); a += 10) {
        size_t n = std::min<size_t>(10, B.chain.size() - a);
        for (size_t i = 0; i + 1 < n; i++) { float wdt = 0.62f - i * 0.05f; Limb(B.chain[a + i], B.chain[a + i + 1], wdt, flesh); }
        for (size_t i = 2; i + 1 < n; i++) { Vector2 d = Vector2Normalize(Vector2Subtract(B.chain[a + i + 1], B.chain[a + i])); Vector2 nrm{-d.y, d.x}; if (nrm.y > 0) nrm = Vector2Negate(nrm); Vector2 s = Vector2Add(B.chain[a + i], Vector2Scale(nrm, 0.2f - i * 0.012f)); DrawCircleV(W2S(s), Px(0.07f), under); DrawCircleLines((int)W2S(s).x, (int)W2S(s).y, Px(0.07f), dark); }
    }
    // the eye: a lidded orb with a bar pupil that follows its target
    if (B.s[0] > 0.02f || B.dead) {
        Vector2 e = B.pos; if (B.dead) e.y -= B.deadT;
        Oval({e.x, e.y + 0.1f}, 1.25f, 1.05f, flesh); Oval(e, 0.82f, 0.74f, Flash(Color{238, 200, 70, 255}, B));
        Vector2 look{0, 0}; if (B.target >= 0 && B.target < (int)S.M.w.sticks.size()) look = Vector2Scale(Vector2Normalize(Vector2Subtract(S.M.w.sticks[B.target].pt[sf::J_NECK].p, e)), 0.25f);
        Vector2 pc = W2S(Vector2Add(e, look)); DrawRectangleRounded({pc.x - Px(0.42f), pc.y - Px(0.1f), Px(0.84f), Px(0.2f)}, 1, 6, INK);
        float lid = B.dead ? 1 : B.s[0] < 0.4f ? 1 - B.s[0] / 0.4f : 0.1f + 0.1f * sinf(B.t * 0.7f);   // (the lids: shut as it rises, open while it looks)
        Vector2 c = W2S(e); DrawCircleSector(c, Px(0.86f), 180, 180 + 180 * std::clamp(lid, 0.05f, 1.0f), 16, dark); DrawCircleSector(c, Px(0.86f), 0, 180 * std::clamp(lid * 0.6f, 0.0f, 1.0f), 16, dark);
        DrawRing(c, Px(0.82f), Px(0.82f) + 3, 0, 360, 32, INK);
    }
}

void Wyrm(const sf::Boss& B) {
    static const float GRATE[4] = {2.85f, 7.65f, 11.55f, 16.35f};
    // the grates in the floor (the tell: the one it'll come out of shakes and bubbles)
    for (float gx : GRATE) {
        bool tell = false; for (const auto& r : B.danger) tell |= fabsf(r.x + r.width / 2 - gx) < 0.3f;
        float shake = tell ? sinf(S.t * 50) * 2 : 0;
        Vector2 a = W2S({gx - 0.75f, 0.62f}); a.x += shake;
        DrawRectangleV({a.x, a.y - 3}, {Px(1.5f), 7}, Color{40, 46, 50, 255});
        for (int i = 0; i < 6; i++) DrawLineEx({a.x + Px(0.1f) + i * Px(0.26f), a.y - 3}, {a.x + Px(0.1f) + i * Px(0.26f), a.y + 4}, 2, Color{120, 128, 120, 255});
        if (tell) for (int i = 0; i < 5; i++) { float yy = fmodf(S.t * 2.5f + i * 0.37f, 1.6f); DrawCircleLines((int)(a.x + Px(0.75f) + sinf(i * 2 + S.t * 4) * Px(0.4f)), (int)(W2S({0, 0.62f + yy}).y), Px(0.06f + 0.03f * (i % 2)), Color{200, 230, 220, 200}); }
    }
    if (B.parts.empty()) return;
    Color scale = Flash(Color{70, 128, 110, 255}, B), belly = Flash(Color{196, 210, 160, 255}, B), ridge = Flash(Color{40, 80, 70, 255}, B);
    // the body from the tail up (each segment a plate with a ridge), then the head
    for (int i = (int)B.parts.size() - 1; i >= 1; i--) {
        const auto& p = B.parts[i]; Vector2 q = p.p; if (B.dead) q.y -= B.deadT * 0.8f;
        Ball(q, p.r, scale); Vector2 c = W2S(q); DrawCircleV({c.x, c.y + Px(p.r * 0.35f)}, Px(p.r * 0.55f), belly);
        Tri({q.x - 0.12f, q.y + p.r * 0.8f}, {q.x + 0.12f, q.y + p.r * 0.8f}, {q.x, q.y + p.r * 1.35f}, ridge);
    }
    const auto& h = B.parts[0]; Vector2 hp = h.p; if (B.dead) hp.y -= B.deadT * 0.8f;
    Vector2 dir = B.parts.size() > 1 ? Vector2Normalize(Vector2Subtract(hp, B.parts[1].p)) : Vector2{0, 1};
    Vector2 snout = Vector2Add(hp, Vector2Scale(dir, 0.75f)), nrm{-dir.y, dir.x};
    float jaw = 0.25f + 0.15f * sinf(B.t * 6);
    Tri(Vector2Add(hp, Vector2Scale(nrm, 0.45f)), Vector2Add(snout, Vector2Scale(nrm, 0.12f)), Vector2Add(hp, Vector2Scale(nrm, -0.05f)), scale);
    Tri(Vector2Add(hp, Vector2Scale(nrm, -0.45f)), Vector2Add(snout, Vector2Scale(nrm, -0.12f - jaw)), Vector2Add(hp, Vector2Scale(nrm, 0.05f)), belly);
    Ball(hp, h.r, scale);
    for (int s : {-1, 1}) { Vector2 e = Vector2Add(hp, Vector2Add(Vector2Scale(dir, 0.15f), Vector2Scale(nrm, s * 0.28f))); Ball(e, 0.12f, Color{240, 220, 90, 255}, 1.5f); }
    for (int s : {-1, 1}) Tri(Vector2Add(hp, Vector2Scale(nrm, s * 0.35f)), Vector2Add(hp, Vector2Add(Vector2Scale(dir, -0.7f), Vector2Scale(nrm, s * 0.75f))), Vector2Add(hp, Vector2Scale(dir, -0.2f)), ridge);   // (the frills)
}

void SunGod(const sf::Boss& B) {
    Vector2 P = B.pos; if (B.dead) P.y -= B.deadT * B.deadT * 2;
    Color gold = Flash(Color{238, 186, 60, 255}, B), deep = Flash(Color{200, 120, 30, 255}, B), face = Flash(Color{250, 230, 170, 255}, B);
    // the glow behind it, the halo's rays turning, the hands, the core and the mask
    DrawCircleGradient((int)W2S(P).x, (int)W2S(P).y, Px(3.2f), Fade(ColorAlpha(Color{255, 220, 120, 255}, 0.35f), B), Fade(ColorAlpha(Color{255, 220, 120, 255}, 0), B));
    for (int i = 0; i < 8; i++) { float a = i * PI / 4 + B.t * 0.4f; Vector2 o{P.x + cosf(a) * 1.15f, P.y + sinf(a) * 1.15f}, t{P.x + cosf(a) * 2.15f, P.y + sinf(a) * 2.15f}, n{-sinf(a) * 0.32f, cosf(a) * 0.32f}; Tri(Vector2Add(o, n), Vector2Subtract(o, n), t, i % 2 ? gold : deep); }
    for (int s : {-1, 1}) { Vector2 h{P.x + s * 1.6f, P.y - 0.5f + sinf(B.t * 2 + s) * 0.1f}; Ball(h, 0.3f, gold); for (int f = 0; f < 3; f++) Limb(h, {h.x + s * 0.25f, h.y - 0.3f - f * 0.05f + (f - 1) * 0.12f}, 0.08f, gold); }
    Ball(P, 0.95f, deep); Ball(P, 0.72f, B.phase == 2 ? Flash(Color{255, 250, 220, 255}, B) : gold);
    if (B.phase == 2) for (int i = 0; i < 6; i++) { float a = i * PI / 3 + S.t; DrawLineEx(W2S(P), W2S({P.x + cosf(a) * 0.7f, P.y + sinf(a) * 0.7f}), 2, deep); }   // (the core split open)
    Vector2 m{P.x, P.y + 0.85f}; Oval(m, 0.5f, 0.56f, face);
    Vector2 mc = W2S(m); for (int s : {-1, 1}) { DrawLineEx({mc.x + s * Px(0.28f), mc.y - Px(0.08f)}, {mc.x + s * Px(0.1f), mc.y - Px(0.02f)}, 3, INK); }
    DrawLineEx({mc.x - Px(0.14f), mc.y + Px(0.24f)}, {mc.x + Px(0.14f), mc.y + Px(0.24f)}, 3, INK);
    // the beams: a thin red tell, then a white-hot line with an orange glow
    for (size_t i = 0; i + 2 < B.chain.size(); i += 3) {
        Vector2 a = W2S(B.chain[i]), b = W2S(B.chain[i + 1]); bool fire = B.chain[i + 2].x > 0.5f;
        if (!fire) { for (float u = 0; u < 1; u += 0.04f) if (fmodf(u * 25 + S.t * 8, 2) < 1) DrawLineEx(Vector2Lerp(a, b, u), Vector2Lerp(a, b, u + 0.04f), 2, Color{220, 40, 30, 200}); }
        else { DrawLineEx(a, b, Px(0.5f), ColorAlpha(Color{255, 140, 40, 255}, 0.5f)); DrawLineEx(a, b, Px(0.22f), Color{255, 240, 200, 255}); DrawCircleV(b, Px(0.35f + 0.1f * sinf(S.t * 30)), Color{255, 200, 90, 255}); }
    }
}

void Goliath(const sf::Boss& B) {
    Vector2 P = B.pos; float lunge = B.s[1], open = B.s[0], hx = P.x - lunge, sink = B.dead ? B.deadT * 0.8f : 0;
    Vector2 M{P.x - 2.8f - lunge, 2.4f - sink};
    Color skin = Flash(Color{120, 112, 74, 255}, B), dark = Flash(Color{78, 70, 46, 255}, B), lip = Flash(Color{170, 150, 104, 255}, B);
    // the pull: streaks running into the mouth
    if (B.act == 1 && B.actT > 0.8f && open > 0.6f) for (int i = 0; i < 14; i++) { float u = fmodf(S.t * 1.8f + i * 0.17f, 1), a = i * 2.4f; Vector2 far{M.x - 7 * (1 - u) * (0.6f + 0.4f * cosf(a)), M.y + 3 * (1 - u) * sinf(a)}; DrawLineEx(W2S(far), W2S(Vector2Lerp(far, M, 0.12f)), 2, ColorAlpha(Color{190, 210, 220, 255}, 0.5f * u)); }
    // the head: a great mottled dome, the gill plate, a fin, the spots
    Oval({hx + 1.8f, 3.0f - sink}, 4.0f, 3.2f, skin);
    for (int i = 0; i < 12; i++) { Vector2 s{hx + 0.4f + (i * 37 % 7) * 0.55f, 1.2f + (i * 53 % 9) * 0.45f - sink}; DrawCircleV(W2S(s), Px(0.12f + (i % 3) * 0.05f), dark); }
    { Vector2 a = W2S({hx + 1.4f, 5.2f - sink}), b = W2S({hx + 0.6f, 3.0f - sink}), c = W2S({hx + 1.3f, 0.8f - sink}); DrawLineEx(a, b, 4, INK); DrawLineEx(b, c, 4, INK); }
    Tri({hx + 2.2f, 1.6f - sink}, {hx + 3.6f, 0.9f - sink}, {hx + 3.2f, 2.4f + sinf(B.t * 3) * 0.2f - sink}, dark);
    // the mouth: lips apart by how open it is; dark inside
    float g = 0.25f + open * 0.95f;
    Oval({M.x + 0.7f, M.y}, 1.3f, g + 0.1f, Color{40, 20, 22, 255});
    Oval({M.x + 0.3f, M.y + g}, 1.55f, 0.42f, lip); Oval({M.x + 0.3f, M.y - g}, 1.6f, 0.5f, lip);
    // the eye: gold, the weak point
    Vector2 e{hx - 1.0f, 4.4f - sink}; Ball(e, 0.44f, Flash(Color{230, 190, 70, 255}, B)); Ball(e, 0.2f, INK, 0);
    if (B.dead) { Vector2 c = W2S(e); DrawLineEx({c.x - 8, c.y - 8}, {c.x + 8, c.y + 8}, 3, INK); DrawLineEx({c.x - 8, c.y + 8}, {c.x + 8, c.y - 8}, 3, INK); }
}

void Bouncer(const sf::Boss& B) {
    float dazed = B.s[0], walk = B.s[1]; int f = B.face;
    float hop = B.act == 4 && B.actT > 0.6f && B.actT < 1.1f ? sinf((B.actT - 0.6f) / 0.5f * PI) * 1.6f : 0;
    float fall = B.dead ? std::min(1.0f, B.deadT * 1.5f) : 0;
    Vector2 P{B.pos.x, 0.6f + hop};
    auto R = [&](Vector2 o) { float a = -f * fall * PI / 2; float c = cosf(a), s = sinf(a); return Vector2{P.x + o.x * c - o.y * s, P.y + o.x * s + o.y * c}; };   // (he topples over when beaten)
    Color shirt = Flash(Color{236, 230, 214, 255}, B), vest = Flash(Color{40, 36, 40, 255}, B), skin = Flash(Color{226, 180, 140, 255}, B), trou = Flash(Color{70, 66, 80, 255}, B);
    // legs (a heavy walk), the body, the arms to the fists, the head with a bowler and a moustache
    for (int s : {-1, 1}) { float ph = sinf(walk + (s > 0 ? PI : 0)) * 0.35f; Limb(R({s * 0.3f, 1.1f}), R({s * 0.3f + ph, 0.5f}), 0.32f, trou); Limb(R({s * 0.3f + ph, 0.5f}), R({s * 0.3f + ph + f * 0.15f, 0.05f}), 0.3f, INK); }
    Oval(R({0, 1.3f}), 0.75f, 0.55f, trou);
    Oval(R({0, 2.1f}), 0.85f, 0.9f, shirt);
    Tri(R({-0.85f, 2.9f}), R({-0.1f, 2.9f}), R({-0.5f, 1.4f}), vest); Tri(R({0.85f, 2.9f}), R({0.1f, 2.9f}), R({0.5f, 1.4f}), vest);
    Vector2 fist = B.chain.empty() ? R({f * 0.9f, 1.9f}) : B.chain[0]; if (fall > 0) fist = R({f * 0.9f, 1.9f});
    Vector2 shoulder = R({f * 0.6f, 2.7f}), back = R({-f * 0.7f, 2.6f});
    Limb(shoulder, Vector2Lerp(shoulder, fist, 0.5f), 0.34f, shirt); Limb(Vector2Lerp(shoulder, fist, 0.5f), fist, 0.3f, shirt); Ball(fist, 0.3f, skin);
    Limb(back, R({-f * 0.95f, 1.7f + sinf(walk) * 0.1f}), 0.32f, shirt); Ball(R({-f * 0.95f, 1.6f + sinf(walk) * 0.1f}), 0.28f, skin);
    Vector2 head = R({f * 0.1f, 3.05f}); Ball(head, 0.45f, skin);
    Oval(R({f * 0.1f, 3.45f}), 0.55f, 0.1f, INK); Oval(R({f * 0.1f, 3.62f}), 0.34f, 0.24f, vest);   // (the bowler)
    Vector2 hc = W2S(head); DrawLineEx({hc.x + f * Px(0.05f), hc.y + Px(0.12f)}, {hc.x + f * Px(0.42f), hc.y + Px(0.2f)}, 5, INK);   // (the moustache)
    if (dazed > 0 || B.dead) DrawText("x", (int)(hc.x + f * Px(0.2f)), (int)(hc.y - Px(0.15f)), (int)Px(0.3f), INK); else DrawCircleV({hc.x + f * Px(0.22f), hc.y - Px(0.08f)}, Px(0.06f), INK);
    if (dazed > 0) for (int i = 0; i < 3; i++) { float a = S.t * 5 + i * 2.1f; Vector2 s{hc.x + cosf(a) * Px(0.6f), hc.y - Px(0.6f) + sinf(a) * Px(0.15f)}; DrawPoly(s, 5, Px(0.12f), a * 30, Color{250, 220, 80, 255}); }
    if (B.act == 2 && B.actT < 0.7f + 0.45f * B.count) { Vector2 st = R({0, 4.2f}); Oval(st, 0.45f, 0.12f, Color{150, 100, 60, 255}); for (int s : {-1, 1}) Limb(st, {st.x + s * 0.35f, st.y + 0.45f}, 0.08f, Color{150, 100, 60, 255}); }   // (a stool overhead)
}

void Draw() {
    const sf::Boss& B = S.M.w.boss;
    if (B.kind < 0) return;
    switch (B.kind) {
    case sf::BK_LOBSTER: Lobster(B); break;
    case sf::BK_KRAKEN: Kraken(B); break;
    case sf::BK_WYRM: Wyrm(B); break;
    case sf::BK_SUN_GOD: SunGod(B); break;
    case sf::BK_GOLIATH: Goliath(B); break;
    default: Bouncer(B); break;
    }
}
// the boss's health along the top: its name, the bar with the phase notches, and its roar
void Hud() {
    const sf::Boss& B = S.M.w.boss;
    if (B.kind < 0) return;
    float w = 520, x = SCREEN_W / 2.0f - w / 2, y = 114;
    DrawTextCenteredBold(TextFormat("%s%s", sf::BossName(B.kind), B.phase ? TextFormat("   (phase %d)", B.phase + 1) : ""), SCREEN_W / 2.0f, y - 22, 18, NameInk());
    DrawRectangleRounded({x - 3, y - 3, w + 6, 20}, 0.4f, 6, INK);
    float u = B.maxHp > 0 ? std::clamp(B.hp / B.maxHp, 0.0f, 1.0f) : 0;
    DrawRectangleRounded({x, y, w * u, 14}, 0.4f, 6, ColorLerp(Color{200, 60, 40, 255}, WHITE, B.hitT > 0 ? 0.4f : 0));
    for (float n : {0.33f, 0.66f}) DrawLineEx({x + w * n, y - 2}, {x + w * n, y + 16}, 2, Color{240, 220, 180, 255});
    if (B.roarT > 0) DrawTextCenteredBold(B.phase == 1 ? "It's angry now." : "It's desperate!", SCREEN_W / 2.0f, y + 26, 22, Color{170, 40, 24, (unsigned char)(255 * std::min(1.0f, B.roarT))});
}
} // namespace bossart
