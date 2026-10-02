// The Trawl's scene (design doc, "The session loop" and "Controls, HUD, and the sonar"): the Gannet at night seen
// from above, the hand you play walking her deck and working her stations. The simulation is trawl_boat.cpp (and,
// as the stages come, the lines, the fish and the web); this file feeds it input and draws it.
#include "trawl.h"
#include "trawl_art.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "trawl_view3d.h"
#include "trawl_net.h"
#include "skins.h"
#include "trawl_weapons.h"
#include "sound.h"
#include "arcade_session.h"
#include "net.h"
#include "input.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

using namespace tw;

namespace {
struct TrawlScene {
    bool active = false;
    // the world drawn: our own (solo, or a network guest's mirror of the host's snapshots), or the host's real one
    TrawlWorld own;
    TrawlWorld* W = nullptr;
    arcade::Session* net = nullptr;  // network play (the Deep Arcade's session), or null solo
    int seenVersion = -1;          // (a guest) the last snapshot mirrored
    // (a guest) the last two snapshots' poses, for drawing between them
    struct Pose { Vector2 pos{}; float heading = 0, roll = 0, pitch = 0, heave = 0; std::vector<Vector2> crew; };
    Pose prevPose, curPose; float sinceSnap = 0, snapGap = 0.05f;
    int you = 0;
    float acc = 0;                 // the fixed 60 Hz step's accumulator
    bool shot = false;             // --shots: no input, fixed time
    int studio = -1;               // --shots: the visual overhaul's turnaround stage (DrawTrawlStudio), or -1
    int shotView = 0;
    HandInput pend;                // (solo) this hand's input, gathered per frame for the next fixed step
    View view;                     // the last frame's view (the mouse's deck position)
    int panel = -1;                // an open dock panel (DockKind), PANEL_CHART at the helm, PANEL_END for the count
    std::string toast; float toastT = 0;
    size_t tapeSeen = 0; float tapeT = 0;
    float ghostSee = 0;
    Vector2 camOff{0, 0};                 // the top-down view's slide off the Gannet (following you out in the skiff)
    // the first-person version (trawl_view3d.cpp): the same game through the hand's eyes
    bool fp = false;
    Eye3D eye;
    Camera3D cam{};
};
const int PANEL_CHART = 20, PANEL_END = 21, PANEL_ELDER = 22;
TrawlScene S;

// the lights on deck: the lantern mast (its level), the wheelhouse's glow, the engine room's fire from below
View MakeView(const Gannet& g, int viewerDeck, bool inWheelhouse) {
    View v;
    v.center = {(PIXEL_W + 2) / 2.0f - 1.5f * v.ppm, (PIXEL_H + 2) / 2.0f + 0.5f * v.ppm};
    v.viewerDeck = viewerDeck; v.inWheelhouse = inWheelhouse;
    // she heels: from above the deck shifts a little toward the low side (the whole view rides with her)
    v.center.y += std::clamp(g.boat.RollDeg() / 5.0f, -4.0f, 4.0f);
    v.center.x -= std::clamp(g.boat.pitch * 57.3f / 5.0f, -3.0f, 3.0f);
    float lantern = LanternRadius(g.boat.lantern);             // hooded 4 m, low 8, full 14, the searchlight's 30 m cone
    if (g.sea.weather == Weather::Fog) lantern *= 0.6f;        // "Fog: light radius -40%"
    if (g.boat.lantern == 3) {
        v.lights.push_back({{0.2f, 0}, lantern, 1.3f, {cosf(g.boat.searchAim), sinf(g.boat.searchAim)}, 0.32f});
        v.lights.push_back({{0.2f, 0}, 5, 0.5f});
    } else v.lights.push_back({{0.2f, 0}, lantern, 1.0f});
    v.lights.push_back({{3.0f, 0}, 4.0f, 0.6f});               // the wheelhouse lamp through its windows
    v.lights.push_back({{-10.4f, 0}, 5.0f, 0.5f});             // the stern work lamp
    if (viewerDeck == 1) v.lights.push_back({{-4.8f, -0.9f}, 4.5f, 0.3f + 0.5f * std::clamp(g.boat.firebox / 6, 0.0f, 1.0f)});
    if (viewerDeck == 1) for (const auto& l : g.lamps) if (l.lit) v.lights.push_back({l.at, 3.5f, 0.7f});   // the oil lamps below
    if (g.moored) for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) v.lights.push_back({{x, -6.8f}, 7.0f, 0.85f});   // the quay's lamps
    for (const auto& fl : g.flares) v.lights.push_back({g.boat.ToDeck(fl.p), 18.0f, 1.2f});                      // a flare burning on the water
    for (const auto& c : g.crew) if (c.overboard && !c.dead) v.lights.push_back({g.boat.ToDeck(c.swim), 2.5f, 0.35f});   // (the swimmer's own splash catches the light)
    return v;
}

// WASD as a deck-frame wish: top-down it is screen-relative (the bow to the right of the screen); in first person
// it is relative to where you look
Vector2 KeysWish() {
    float f = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
    float r = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
    if (!S.fp) return {r, -f};
    Vector2 fw = LookDeckDir(S.eye), rt{-fw.y, fw.x};
    Vector2 w = Vector2Add(Vector2Scale(fw, f), Vector2Scale(rt, r));
    return Vector2Length(w) > 1 ? Vector2Normalize(w) : w;
}
// the sonar scope on the screen: heading-up (the bow at the top), the Gannet at its centre, 150 m to the rim
const Vector2 SCOPE_C{SCREEN_W / 2.0f, 276};
const float SCOPE_R = 190;
Vector2 DeckToScope(Vector2 d) { float k = SCOPE_R / SONAR_RANGE; return {SCOPE_C.x + d.y * k, SCOPE_C.y - d.x * k}; }
Vector2 ScopeToDeck(Vector2 s) { float k = SCOPE_R / SONAR_RANGE; return {(SCOPE_C.y - s.y) / k, (s.x - SCOPE_C.x) / k}; }
// where the hand aims on the water, in the deck frame: the mouse top-down, the crosshair in first person
Vector2 AimDeck() {
    if (S.fp) { Vector2 d; AimAtWater(S.W->G, S.cam, {SCREEN_W / 2.0f, SCREEN_H / 2.0f}, &d); return d; }
    const float PX = (float)SCREEN_W / PIXEL_W;
    Vector2 m = GetMousePosition();
    return S.view.DeckOfCanvas({m.x / PX + 1, m.y / PX + 1});
}
// The keyboard and mouse as this hand's input (trawl_net.h: the same frame drives a solo Gannet, the host's, or goes
// over the wire), and what is only this screen's business: the dock's panels, the view, toasts.
HandInput Gather() {
    HandInput in;
    const Gannet& G = S.W->G;
    const Crew& c = G.crew[S.you];
    if (S.panel >= 0) {
        if (IsKeyPressed(KEY_X) && S.panel != PANEL_END) { S.panel = -1; in.btn |= HI_X_P; }
        return in;
    }
    if (c.deck == DECK_DIVE && !c.dead) {
        // down on a wreck: the body moves in the side view (A/D walk, W/S climb, Space swims up); walking into another
        // room asks the host to move the diver there; E lifts or baskets salvage, R gives the two tugs
        float dir = ((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0f : 0.0f) - ((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0f : 0.0f);
        int want = DiveSceneStep(G, S.you, GetFrameTime(), dir, IsKeyDown(KEY_SPACE), IsKeyDown(KEY_W) || IsKeyDown(KEY_UP), IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN));
        if (want >= 0) { in.btn |= HI_ORDER; in.order = (int8_t)want; }
        if (IsKeyPressed(KEY_E)) in.btn |= HI_E_P;
        if (IsKeyPressed(KEY_R)) in.btn |= HI_R_P;
        return in;
    }
    in.aim = AimDeck();
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Sonar) in.aim = ScopeToDeck(GetMousePosition());   // (the scope's point under the mouse)
    in.wish = KeysWish();
    in.wheel = GetMouseWheelMove();
    if (c.dead && Vector2Length(in.wish) > 0) S.ghostSee = 1.0f;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.btn |= HI_LMB;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.btn |= HI_LMB_P;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) in.btn |= HI_RMB;
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) in.btn |= HI_RMB_P;
    if (IsKeyPressed(KEY_SPACE)) in.btn |= HI_SPACE_P;
    if (IsKeyDown(KEY_LEFT_SHIFT)) in.btn |= HI_SHIFT;
    if (IsKeyPressed(KEY_R)) in.btn |= HI_R_P;
    if (IsKeyPressed(KEY_T)) in.btn |= HI_T_P;
    if (IsKeyPressed(KEY_X)) in.btn |= HI_X_P;
    for (int k = 0; k < 4; k++) if (IsKeyPressed(KEY_ONE + k) && c.station < 0) in.sel = (int8_t)k;
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Helm) {
        in.steer = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
        if (IsKeyPressed(KEY_W)) in.btn |= HI_W_P;
        if (IsKeyPressed(KEY_S)) in.btn |= HI_S_P;
    }
    if (IsKeyPressed(KEY_V)) { S.fp = !S.fp; if (S.fp) S.eye = Eye3D{}; }   // the two versions of the game: top-down and first person
    if (IsKeyPressed(KEY_E)) {
        int d = G.moored && c.deck == 0 && c.station < 0 ? NearestDock(c.p, 1.4f) : -1;
        if (d >= 0) S.panel = (int)DockStations()[d].kind;   // (the quay's panels are this screen's own)
        else if (c.deck == DECK_SHORE && G.skiff.landing >= 0 && G.skiff.landing < (int)G.landings.size() && Vector2Distance(c.p, G.landings[G.skiff.landing].elder) < 2.2f) S.panel = PANEL_ELDER;   // (the elder's trade)
        else {
            in.btn |= HI_E_P;
            int s = c.station < 0 ? NearestStation(c.p, c.deck, 1.1f) : -1;
            if (s >= 0 && Stations()[s].kind == StationKind::Helm && S.W->sess.phase == Phase::Dock) S.panel = PANEL_CHART;
        }
    }
    if (IsKeyPressed(KEY_G) && G.botsOn) {
        // orders (design doc "Bot crew"): point at a station and the nearest free bot takes it; point at nothing and
        // every bot goes back to its own watch
        Vector2 at{}; bool ok = true;
        if (S.fp) ok = AimAtDeck(G, S.cam, {SCREEN_W / 2.0f, SCREEN_H / 2.0f}, c.deck, &at);
        else at = AimDeck();
        int st = ok ? NearestStation(at, c.deck, 1.6f) : -1;
        in.btn |= HI_ORDER;
        if (st >= 0 && st != c.station) { in.order = (int8_t)st; S.toast = TextFormat("A hand to the %s", Stations()[st].name); }
        else { in.order = -1; S.toast = "All hands to their watch"; }
        S.toastT = 2.5f;
    }
    if (IsKeyPressed(KEY_F) && G.botsOn) {
        bool following = false;
        for (int i = 0; i < (int)G.brains.size(); i++) if (G.brains[i].follow == S.you) following = true;
        in.btn |= HI_FOLLOW_P;
        S.toast = following ? "Back to your watch" : "A hand follows you"; S.toastT = 2.5f;
    }
    return in;
}
// a dock button, the locker: run here (solo, or the host's own screen) or sent to the host (a guest)
void Command(int cmd, const std::string& id = "", int arg = 0, const std::string& ok = "") {
    std::string why;
    if (S.net) {
        // (the host's own Act runs it at once; a guest's goes over the wire, and the mirror shows the result)
        Writer w; WriteCmdAction(cmd, id, arg, w); S.net->Act(w);
        if (!ok.empty()) { S.toast = ok; S.toastT = 3; }
        return;
    }
    bool done = DoCommand(*S.W, S.you, cmd, id, arg, &why);
    if (!done && !why.empty()) { S.toast = why; S.toastT = 3; }
    else if (done && !ok.empty()) { S.toast = ok; S.toastT = 3; }
}

// ---------------------------------------------------------------- the HUD: almost nothing, on purpose
void Gauge(Vector2 c, float r, float v, float lo, float hi, float red, const char* label) {
    DrawCircleV(c, r + 4, Color{60, 48, 30, 255});
    DrawCircleV(c, r, Color{226, 214, 186, 255});
    auto ang = [&](float x) { return (-220 + 260 * std::clamp(x / (red * 1.25f), 0.0f, 1.0f)) * DEG2RAD; };
    for (float x = 0; x <= red * 1.25f + 0.001f; x += red * 1.25f / 20) {
        Color tc = x >= red ? Color{190, 40, 30, 255} : (x >= lo && x <= hi) ? Color{60, 140, 60, 255} : Color{60, 50, 40, 255};
        float a = ang(x);
        DrawLineEx({c.x + cosf(a) * r * 0.78f, c.y + sinf(a) * r * 0.78f}, {c.x + cosf(a) * r * 0.95f, c.y + sinf(a) * r * 0.95f}, 2, tc);
    }
    float a = ang(v);
    DrawLineEx(c, {c.x + cosf(a) * r * 0.8f, c.y + sinf(a) * r * 0.8f}, 3, Color{30, 20, 16, 255});
    DrawCircleV(c, 4, Color{30, 20, 16, 255});
    DrawTextCentered(label, c.x, c.y + r * 0.4f, 13, Color{60, 48, 30, 255});
}
// The reel gauge (design doc, "Controls, HUD": a tension arc with the drag setting, the line counter, the lure's depth)
void ReelGauge(const Gannet& g, const Crew& c) {
    int ri = c.station >= 0 ? g.RodAt(c.station) : -1;
    bool skiffLine = c.deck == DECK_SKIFF && c.skiffLine && !c.overboard;   // (the skiff's line has the same gauge)
    if (ri < 0 && !skiffLine) return;
    const Rod& r = skiffLine ? g.skiffRod : g.rods[ri];
    const TackleDef& td = TackleOf(r.tackle);
    Color paper{230, 220, 196, 255}, ink{40, 30, 20, 255};
    Vector2 cc{SCREEN_W - 150.0f, SCREEN_H - 170.0f};
    float R = 72, full = td.strength * 1.25f;
    DrawCircleV(cc, R + 5, Color{60, 48, 30, 255}); DrawCircleV(cc, R, Color{226, 214, 186, 255});
    auto ang = [&](float x) { return (-215 + 250 * std::clamp(x / full, 0.0f, 1.0f)) * DEG2RAD; };
    for (float x = 0; x <= full + 0.01f; x += full / 30) {
        Color tc = x >= td.strength ? Color{190, 40, 30, 255} : Color{70, 56, 36, 255};
        float a = ang(x);
        DrawLineEx({cc.x + cosf(a) * R * 0.8f, cc.y + sinf(a) * R * 0.8f}, {cc.x + cosf(a) * R * 0.95f, cc.y + sinf(a) * R * 0.95f}, 2, tc);
    }
    float drag = r.fight.drag;
    float da = ang(drag);
    DrawLineEx({cc.x + cosf(da) * R * 0.55f, cc.y + sinf(da) * R * 0.55f}, {cc.x + cosf(da) * R * 1.02f, cc.y + sinf(da) * R * 1.02f}, 3, Color{40, 110, 150, 255});
    float T = r.state == RodState::Fighting ? r.fight.tension : r.state == RodState::Out ? r.lastTick * td.strength * 0.15f : 0;
    float ta = ang(T);
    DrawLineEx(cc, {cc.x + cosf(ta) * R * 0.78f, cc.y + sinf(ta) * R * 0.78f}, 3, ink);
    DrawCircleV(cc, 4, ink);
    DrawTextCentered("TENSION", cc.x, cc.y + R * 0.3f, 12, Color{60, 48, 30, 255});
    DrawTextCentered(TextFormat("drag %.1f kg", drag), cc.x, cc.y + R * 0.5f, 12, Color{40, 90, 120, 255});
    float out = r.state == RodState::Fighting ? r.fight.L : r.state == RodState::Out ? r.lineOut + r.lure.z : 0;
    float depth = r.state == RodState::Fighting ? r.fight.p.z : r.state == RodState::Out ? r.lure.z : 0;
    float x0 = SCREEN_W - 560;
    TxtBold(TextFormat(skiffLine ? "The skiff's %s, %s line, %s hook  (T: back to the oars)" : "%s, %s line, %s hook  (T: change tackle)", td.name, LineOf(r.line).name, HookName(r.hook)), x0, SCREEN_H - 60, 15, paper);
    TxtBold(TextFormat("Line out %3.0f m    Depth %3.0f m%s", out, depth, r.state == RodState::Out ? TextFormat("  (set %.0f)", r.lureDepth) : ""), x0, SCREEN_H - 38, 15, Fade(paper, 0.85f));
    const char* tip = nullptr;
    switch (r.state) {
        case RodState::Idle: tip = "Hold left mouse to cast, release at the mouse. Scroll sets the drag."; break;
        case RodState::Charging: tip = TextFormat("Casting... %.0f m", r.charge * td.cast); break;
        case RodState::Out:
            tip = r.bite.stage == BiteStage::Take ? "The rod loads: SPACE!" : r.bite.stage == BiteStage::Nibble ? "Nibbles... wait for the take" :
                  r.bite.stage == BiteStage::Inspect ? "The tip ticks..." : "Waiting. Scroll sets the depth; hold left mouse to reel in.";
            break;
        case RodState::Fighting:
            tip = r.fight.alongside ? "Alongside: SPACE to gaff" : r.fight.jumpT >= 0 ? "It jumps: RIGHT MOUSE to bow!" :
                  r.lastTick > 0 ? "The tip ticks: it's about to run (Reader)" :
                  "Left: reel   Right: bow   Mouse to one side: side pressure   Scroll: drag";
            break;
    }
    if (tip) DrawTextCenteredBold(tip, SCREEN_W / 2.0f, SCREEN_H - 150.0f, 18, r.bite.stage == BiteStage::Take || (r.state == RodState::Fighting && (r.fight.alongside || r.fight.jumpT >= 0 || r.lastTick > 0)) ? Color{250, 200, 110, 255} : paper);
    if (r.state == RodState::Fighting && c.role == Role::Angler) TxtBold(TextFormat("%s, about %.0f kg", r.fight.spec.name, r.fight.spec.kg), x0, SCREEN_H - 82, 15, Color{150, 200, 170, 255});
    if (!r.lastCatch.empty() && r.state != RodState::Fighting) Txt(TextFormat("Last: %s", r.lastCatch.c_str()), x0, SCREEN_H - 82, 14, Fade(paper, 0.6f));
}
// ---------------------------------------------------------------- the sonar scope (design doc, "The sonar")
// A round phosphor scope, heading-up: the seabed and the shore come back as hard returns after a ping (fading over
// 6 s), schools as dotted clouds, single fish as blips by size, gear as squares, something big as a heavy blot. Under
// it the side profile: the water column along her heading, the seabed line and the returns at their depths.
void DrawSonarScope() {
    const Gannet& g = S.W->G;
    const SonarState& so = g.sonar;
    const Eco& e = S.W->eco;
    Color glow{110, 255, 160, 255}, dim{40, 120, 70, 255};
    DrawCircleV(SCOPE_C, SCOPE_R + 12, Color{40, 34, 26, 255});
    DrawCircleV(SCOPE_C, SCOPE_R + 4, Color{90, 76, 50, 255});
    DrawCircleV(SCOPE_C, SCOPE_R, Color{4, 18, 10, 255});
    for (int ring = 1; ring <= 3; ring++) DrawRing(SCOPE_C, SCOPE_R * ring / 3.0f - 1, SCOPE_R * ring / 3.0f, 0, 360, 64, Fade(dim, 0.6f));
    DrawLineEx({SCOPE_C.x, SCOPE_C.y - SCOPE_R}, {SCOPE_C.x, SCOPE_C.y + SCOPE_R}, 1, Fade(dim, 0.35f));
    DrawLineEx({SCOPE_C.x - SCOPE_R, SCOPE_C.y}, {SCOPE_C.x + SCOPE_R, SCOPE_C.y}, 1, Fade(dim, 0.35f));
    for (int ring = 1; ring <= 3; ring++) DrawText(TextFormat("%d", ring * 50), (int)(SCOPE_C.x + 4), (int)(SCOPE_C.y - SCOPE_R * ring / 3.0f + 2), 10, Fade(dim, 0.9f));
    float fade = std::clamp(1 - so.sinceP / SONAR_LIFE, 0.0f, 1.0f);
    // the seabed and the shore: what the last ping found, swept outward over its first second
    if (e.g && fade > 0) {
        float swept = std::min(1.0f, so.sinceP / 0.8f) * SONAR_RANGE;
        for (float rr = 6; rr <= swept; rr += 4) {
            int n = (int)(rr * 0.8f) + 8;
            for (int k = 0; k < n; k++) {
                float a = k * 6.2832f / n;
                Vector2 d{cosf(a) * rr, sinf(a) * rr};
                float depth = e.DepthAt(g.boat.ToWorld(d));
                // only what she could hit comes back hard: the shore, the crest and the shoals (a faint rim at 3 m)
                float hard = depth <= 0 ? 1.0f : depth < 1.8f ? 0.8f : depth < 3 ? 0.18f : 0;
                if (hard <= 0) continue;
                Vector2 s = DeckToScope(d);
                DrawRectangle((int)s.x - 1, (int)s.y - 1, 3, 3, Fade(glow, hard * fade * 0.8f));
            }
        }
    }
    // the sweep
    if (so.sinceP < 1) { float a = so.sinceP * 6.2832f - PI / 2; DrawLineEx(SCOPE_C, {SCOPE_C.x + cosf(a) * SCOPE_R, SCOPE_C.y + sinf(a) * SCOPE_R}, 2, Fade(glow, 0.7f * (1 - so.sinceP))); }
    // the returns
    for (size_t i = 0; i < so.ret.size(); i++) {
        const SonarReturn& r = so.ret[i];
        if (!SonarBandHas(so.band, r.p.z)) continue;
        Vector2 d = g.boat.ToDeck({r.p.x, r.p.y});
        if (Vector2Length(d) > SONAR_RANGE) continue;
        Vector2 s = DeckToScope(d);
        float a = std::clamp(r.t / (r.passive ? 2.0f : SONAR_LIFE), 0.0f, 1.0f);
        uint32_t h = (uint32_t)i * 2654435761u + (uint32_t)r.sp * 97u;
        auto rnd = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
        switch (r.kind) {
            case SonarKind::School: {
                int dots = std::clamp(r.count / 8, 4, 18);
                float rad = 2 + sqrtf((float)r.count) * 0.45f;
                for (int k = 0; k < dots; k++) { float an = rnd() * 6.2832f, rr = sqrtf(rnd()) * rad; DrawRectangle((int)(s.x + cosf(an) * rr), (int)(s.y + sinf(an) * rr), 2, 2, Fade(glow, a)); }
                break;
            }
            case SonarKind::Fish: DrawCircleV(s, 1 + r.size * 0.5f, Fade(glow, a * 0.8f)); break;
            case SonarKind::Threat: DrawCircleV(s, 3 + r.size, Fade(glow, a)); DrawRing(s, 6 + r.size * 1.4f, 7.5f + r.size * 1.4f, 0, 360, 24, Fade(glow, a * 0.5f)); break;
            case SonarKind::Gear: DrawRectangle((int)s.x - 3, (int)s.y - 3, 7, 7, Fade(Color{220, 255, 220, 255}, a)); break;
            case SonarKind::Wreck: {
                // a long hard return lying on the floor: a hull's outline at the wreck's own heading, bright at its ends
                float ang = (float)(r.count * 2.399f), len = 3.0f + r.size * 1.6f;
                Vector2 ax{cosf(ang) * len, sinf(ang) * len}, nx{-sinf(ang) * 2.5f, cosf(ang) * 2.5f};
                Color wc = Fade(Color{235, 255, 230, 255}, a);
                DrawLineEx(Vector2Subtract(Vector2Subtract(s, ax), nx), Vector2Add(Vector2Subtract(s, nx), ax), 2, wc);
                DrawLineEx(Vector2Add(Vector2Subtract(s, ax), nx), Vector2Add(Vector2Add(s, nx), Vector2Scale(ax, 0.8f)), 2, wc);
                DrawLineEx(Vector2Add(Vector2Add(s, nx), Vector2Scale(ax, 0.8f)), Vector2Add(s, Vector2Scale(ax, 1.25f)), 2, wc);
                DrawLineEx(Vector2Add(Vector2Subtract(s, nx), ax), Vector2Add(s, Vector2Scale(ax, 1.25f)), 2, wc);
                DrawText("WRECK", (int)(s.x + 8), (int)(s.y + 6), 10, Fade(wc, a * 0.8f));
                break;
            }
        }
    }
    for (const auto& m : so.marks) {
        Vector2 s = DeckToScope(g.boat.ToDeck(m.p));
        if (Vector2Distance(s, SCOPE_C) > SCOPE_R) continue;
        DrawRing(s, 11, 13, 0, 360, 24, Fade(Color{250, 220, 120, 255}, std::min(1.0f, m.t)));
        DrawText(m.what.c_str(), (int)s.x + 15, (int)s.y - 6, 12, Fade(Color{250, 220, 120, 255}, std::min(1.0f, m.t)));
    }
    // the skiff: a bright blip with everything round it (design doc v2, "Guiding the skiff": the sonar operator is the
    // skiff's eyes), pulsing so it's never lost among the fish; the skiff marks as dashed rings
    if (g.skiff.state != SkiffState::Stowed && g.skiff.state != SkiffState::Lowering && g.skiff.state != SkiffState::Lost) {
        Vector2 d = g.boat.ToDeck(g.skiff.p);
        if (Vector2Length(d) < SONAR_RANGE) {
            Vector2 s = DeckToScope(d);
            float pulse = 0.6f + 0.4f * sinf(g.time * 6);
            DrawCircleV(s, 4, Color{230, 255, 240, 255});
            DrawRing(s, 6 + pulse * 3, 7.5f + pulse * 3, 0, 360, 24, Fade(Color{230, 255, 240, 255}, pulse));
            DrawText(g.skiff.state == SkiffState::Capsized ? "SKIFF (capsized)" : "SKIFF", (int)s.x + 12, (int)s.y + 4, 11, Color{230, 255, 240, 255});
        }
    }
    for (const auto& mk : e.marks) {
        Vector2 d = g.boat.ToDeck(mk.at);
        if (Vector2Length(d) > SONAR_RANGE + mk.r) continue;
        Vector2 s = DeckToScope(d); float rs = mk.r * SCOPE_R / SONAR_RANGE;
        for (int k = 0; k < 16; k += 2) DrawRing(s, rs - 1, rs, k * 22.5f, k * 22.5f + 14, 4, Fade(glow, 0.5f));
        DrawText(mk.name.c_str(), (int)(s.x - rs * 0.7f), (int)(s.y - 6), 10, Fade(glow, 0.7f));
    }
    // the Gannet at the centre, bow up
    DrawTriangle({SCOPE_C.x, SCOPE_C.y - 9}, {SCOPE_C.x - 4, SCOPE_C.y + 7}, {SCOPE_C.x + 4, SCOPE_C.y + 7}, glow);
    DrawTriangle({SCOPE_C.x, SCOPE_C.y - 9}, {SCOPE_C.x + 4, SCOPE_C.y + 7}, {SCOPE_C.x - 4, SCOPE_C.y + 7}, glow);
    // the side profile: the water column along her heading, 150 m either way
    Rectangle pr{SCOPE_C.x - SCOPE_R - 60, SCOPE_C.y + SCOPE_R + 18, SCOPE_R * 2 + 120, 74};
    DrawRectangleRec(pr, Color{4, 18, 10, 235});
    DrawRectangleLinesEx(pr, 2, Color{90, 76, 50, 255});
    float maxD = 40;
    auto py = [&](float depth) { return pr.y + 6 + std::clamp(depth / maxD, 0.0f, 1.0f) * (pr.height - 12); };
    if (e.g) {
        Vector2 last{};
        for (int k = 0; k <= 60; k++) {
            float along = -SONAR_RANGE + k * (2 * SONAR_RANGE / 60);
            float depth = e.DepthAt(g.boat.ToWorld({along, 0}));
            Vector2 p{pr.x + pr.width * k / 60.0f, py(depth)};
            if (k) DrawLineEx(last, p, 2, Fade(glow, 0.35f + 0.5f * fade));
            last = p;
        }
    }
    for (int b = 1; b <= 2; b++) { float dz = b == 1 ? 5.0f : 20.0f; DrawLineEx({pr.x, py(dz)}, {pr.x + pr.width, py(dz)}, 1, Fade(dim, 0.4f)); }
    if (so.band > 0) {
        float z0 = so.band == 1 ? 0 : so.band == 2 ? 5 : 20, z1 = so.band == 1 ? 5 : so.band == 2 ? 20 : maxD;
        DrawRectangle((int)pr.x, (int)py(z0), (int)pr.width, (int)(py(z1) - py(z0)), Fade(glow, 0.06f));
    }
    for (const auto& r : so.ret) {
        Vector2 d = g.boat.ToDeck({r.p.x, r.p.y});
        if (fabsf(d.y) > 30 || fabsf(d.x) > SONAR_RANGE || r.kind == SonarKind::Gear) continue;   // (a slice 60 m wide)
        float a = std::clamp(r.t / SONAR_LIFE, 0.0f, 1.0f);
        if (r.kind == SonarKind::Wreck) {   // a hull lying on the floor at its depth
            float cx = pr.x + pr.width * (d.x + SONAR_RANGE) / (2 * SONAR_RANGE), w = 6 + r.size * 2, y = py(r.p.z + 2);
            DrawRectangle((int)(cx - w), (int)(y - 4), (int)(w * 2), 5, Fade(Color{235, 255, 230, 255}, a));
            DrawTri({cx + w, y - 4}, {cx + w, y + 1}, {cx + w + 5, y - 4}, Fade(Color{235, 255, 230, 255}, a));
            continue;
        }
        DrawCircleV({pr.x + pr.width * (d.x + SONAR_RANGE) / (2 * SONAR_RANGE), py(r.p.z)}, r.kind == SonarKind::School ? 4.0f : r.kind == SonarKind::Threat ? 6.0f : 2.0f, Fade(glow, a));
    }
    DrawText("ASTERN", (int)pr.x + 6, (int)pr.y + 4, 10, dim);
    DrawText("AHEAD", (int)(pr.x + pr.width - 40), (int)pr.y + 4, 10, dim);
    // the readouts
    Color paper{230, 220, 196, 255};
    bool drowned = g.boat.shaft >= 0.5f;
    // (beside the scope: the tape runs across the top, the station's name under the profile)
    float lx = SCOPE_C.x - SCOPE_R - 200, rx = SCOPE_C.x + SCOPE_R + 30;
    TxtBold("DEPTH DIAL", lx, SCOPE_C.y - 60, 14, dim);
    TxtBold(SonarBandName(so.band), lx, SCOPE_C.y - 40, 17, glow);
    Txt("scroll to change", lx, SCOPE_C.y - 16, 12, dim);
    TxtBold(so.cool > 0 ? TextFormat("PING  %.1f s", so.cool) : "PING READY", rx, SCOPE_C.y - 60, 17, so.cool > 0 ? dim : glow);
    Txt("right mouse / Space", rx, SCOPE_C.y - 36, 12, dim);
    Txt("left click a contact:", rx, SCOPE_C.y - 10, 12, dim);
    Txt("mark it for the crew", rx, SCOPE_C.y + 6, 12, dim);
    if (drowned) { TxtBold("PASSIVE DEAF", lx, SCOPE_C.y + 30, 15, Color{240, 180, 110, 255}); Txt("the screw drowns it out:", lx, SCOPE_C.y + 52, 12, dim); Txt("below half speed to listen", lx, SCOPE_C.y + 68, 12, dim); }
    (void)paper;
}

// ---------------------------------------------------------------- the chart (at the helm): the ground, the harbour, the boat
// The Lagoon's chart from the ground's own depths, drawn once per ground into a texture: dry land, the crest and
// shallows she grounds on (under 1.8 m), the reef and the seagrass where fish feed, open water by depth.
Texture2D gChartTex{}; std::string gChartKey;
void EnsureChart(const Eco& e) {
    std::string key = e.ground + "#" + std::to_string(e.initSeed);
    if (!e.g || key == gChartKey) return;
    if (gChartTex.id) UnloadTexture(gChartTex);
    Image img = GenImageColor(e.n, e.n, BLACK);
    for (int y = 0; y < e.n; y++) for (int x = 0; x < e.n; x++) {
        Vector2 w{(x + 0.5f) * e.cell, (y + 0.5f) * e.cell};
        float d = e.DepthAt(w); int hb = e.HabAt(w);
        Color c;
        if (d <= 0) c = Color{176, 156, 108, 255};
        else if (d < 1.8f) c = Color{196, 120, 92, 255};                       // where she runs aground
        else if (hb == H_REEF || hb == H_CREST) c = Color{150, 108, 124, 255};
        else if (hb == H_SEAGRASS) c = Color{70, 118, 86, 255};
        else { float k = std::clamp(d / 40.0f, 0.0f, 1.0f); c = ColorLerp(Color{112, 160, 178, 255}, Color{20, 40, 78, 255}, k); }
        ImageDrawPixel(&img, x, e.n - 1 - y, c);   // (north up: the helm's compass reads +y as north)
    }
    gChartTex = LoadTextureFromImage(img);
    UnloadImage(img);
    gChartKey = key;
}
void DrawChart(Rectangle r) {
    const Gannet& g = S.W->G; const Eco& e = S.W->eco; const Session& ss = S.W->sess;
    Color paper{230, 220, 196, 255}, ink{40, 30, 20, 255};
    DrawRectangleRec({r.x - 8, r.y - 26, r.width + 16, r.height + 60}, Color{60, 46, 30, 240});
    TxtBold("THE CHART  (north up)", r.x, r.y - 22, 14, paper);
    EnsureChart(e);
    if (!gChartTex.id) return;
    DrawTexturePro(gChartTex, {0, 0, (float)gChartTex.width, (float)gChartTex.height}, r, {0, 0}, 0, WHITE);
    float size = e.n * e.cell;
    auto toR = [&](Vector2 w) { return Vector2{r.x + w.x / size * r.width, r.y + (1 - w.y / size) * r.height}; };
    // the harbour line, and the mouth
    Vector2 hc = toR(ss.harbour); float hr = ss.harbourR / size * r.width;
    for (int k = 0; k < 24; k += 2) DrawRing(hc, hr - 1, hr + 1, k * 15.0f, k * 15.0f + 15, 4, Color{250, 240, 210, 255});
    TxtBold("HARBOUR", hc.x - 26, hc.y - hr - 16, 11, Color{250, 240, 210, 255});
    (void)ink;
    // the marks
    for (const auto& m : g.sonar.marks) { Vector2 p = toR(m.p); DrawRing(p, 4, 6, 0, 360, 16, Color{250, 220, 120, 255}); }
    // the skiff water and the landings (skiff-only: too shallow, too weedy, or a beach)
    for (const auto& mk : e.marks) { Vector2 p = toR(mk.at); float rr = mk.r / size * r.width; for (int k = 0; k < 16; k += 2) DrawRing(p, rr - 1, rr, k * 22.5f, k * 22.5f + 14, 4, Color{140, 230, 210, 255}); TxtBold(mk.name.c_str(), p.x - 30, p.y + rr + 2, 10, Color{140, 230, 210, 255}); }
    for (const auto& L : g.landings) { Vector2 p = toR(L.at); DrawCircleV(p, 3, Color{240, 220, 160, 255}); TxtBold(L.name.c_str(), p.x + 5, p.y - 5, 10, Color{240, 220, 160, 255}); }
    if (g.skiff.Up() || g.skiff.state == SkiffState::Capsized) { Vector2 p = toR(g.skiff.p); DrawCircleV(p, 3, Color{255, 255, 255, 255}); DrawCircleLinesV(p, 5, BLACK); }
    // the Gannet: an arrow along her heading
    Vector2 bp = toR(g.boat.pos), f = g.boat.Forward();
    f.y = -f.y;
    Vector2 tip{bp.x + f.x * 9, bp.y + f.y * 9}, l{bp.x - f.x * 5 - f.y * 5, bp.y - f.y * 5 + f.x * 5}, rr{bp.x - f.x * 5 + f.y * 5, bp.y - f.y * 5 - f.x * 5};
    DrawTriangle(tip, l, rr, Color{250, 250, 250, 255}); DrawTriangle(tip, rr, l, Color{250, 250, 250, 255});
    DrawTriangleLines(tip, l, rr, BLACK);
    // the course home
    Vector2 home = Vector2Subtract(ss.harbour, g.boat.pos);
    float dist = std::max(0.0f, Vector2Length(home) - ss.harbourR);
    float brg = fmodf(90 - atan2f(home.y, home.x) * RAD2DEG + 720, 360);   // (as the helm's heading reads: 0 north, clockwise)
    bool inside = Vector2Length(home) < ss.harbourR;
    Txt(inside ? "Inside the harbour line" : TextFormat("Harbour line: %03.0f deg, %.0f m", brg, dist), r.x, r.y + r.height + 6, 13, paper);
    Txt("red: shoals she runs aground on   mauve: reef   green: seagrass   blue: open water", r.x, r.y + r.height + 22, 11, Fade(paper, 0.7f));
}

// ---------------------------------------------------------------- the sonar's marks: a bearing arrow for every hand
void DrawMarkArrows() {
    const Gannet& g = S.W->G;
    if (g.sonar.marks.empty()) return;
    const Crew& me = g.crew[S.you];
    Color mc{250, 220, 120, 255};
    for (const auto& m : g.sonar.marks) {
        Vector2 dd = Vector2Subtract(g.boat.ToDeck(m.p), me.deck <= 1 && !me.overboard ? me.p : g.boat.ToDeck(g.HandWorld(S.you)));   // (in the skiff, ashore or swimming: from where you are)
        float a = std::min(1.0f, m.t);
        float dist = Vector2Length(dd);
        Vector2 dir;   // the mark's direction on the screen
        if (S.fp) { float rel = atan2f(dd.y, dd.x) - S.eye.yaw; dir = {sinf(rel), -cosf(rel)}; }
        else {
            const float PX = (float)SCREEN_W / PIXEL_W;
            Vector2 c0 = S.view.ToCanvas(me.p), c1 = S.view.ToCanvas(Vector2Add(me.p, Vector2Scale(Vector2Normalize(dd), 10)));
            dir = Vector2Normalize(Vector2Scale(Vector2Subtract(c1, c0), PX));
        }
        Vector2 c{SCREEN_W / 2.0f, SCREEN_H / 2.0f};
        Vector2 p{c.x + dir.x * (SCREEN_W / 2.0f - 60), c.y + dir.y * (SCREEN_H / 2.0f - 60)};
        Vector2 side{-dir.y, dir.x};
        Vector2 t0 = Vector2Add(p, Vector2Scale(dir, 16)), t1 = Vector2Add(p, Vector2Scale(side, 9)), t2 = Vector2Subtract(p, Vector2Scale(side, 9));
        DrawTriangle(t0, t1, t2, Fade(mc, a)); DrawTriangle(t0, t2, t1, Fade(mc, a));
        DrawTextCentered(TextFormat("%s  %.0f m", m.what.c_str(), dist), p.x - dir.x * 26, p.y - dir.y * 26 - 6, 13, Fade(mc, a));
    }
}

void StationOverlay() {
    const Gannet& g = S.W->G;
    const Crew& c = g.crew[S.you];
    Color paper{230, 220, 196, 255};
    if (c.station < 0) {
        int s = NearestStation(c.p, c.deck, 1.1f);
        const char* prompt = nullptr;
        if (Vector2Distance(c.p, {-3.4f, -1.8f}) < 0.8f) prompt = c.deck == 0 ? "E: down the ladder to the engine room" : "E: up the ladder to the deck";
        else if (s >= 0) prompt = TextFormat("E: %s", Stations()[s].name);
        if (prompt) DrawTextCenteredBold(prompt, SCREEN_W / 2.0f, SCREEN_H - 90.0f, 20, paper);
        return;
    }
    const StationDef& sd = Stations()[c.station];
    DrawTextCenteredBold(TextFormat("%s   (X to leave)", sd.name), SCREEN_W / 2.0f, SCREEN_H - 112.0f, 20, paper);
    DrawTextCentered(sd.does, SCREEN_W / 2.0f, SCREEN_H - 86.0f, 15, Fade(paper, 0.8f));
    const Boat& b = g.boat;
    switch (sd.kind) {
        case StationKind::Helm: {
            // the engine telegraph, the heading and the speed
            static const char* T[5] = {"SLOW ASTERN", "STOP", "SLOW", "HALF", "FULL"};
            Vector2 cc{SCREEN_W - 150.0f, SCREEN_H - 160.0f};
            DrawCircleV(cc, 70, Color{60, 48, 30, 255}); DrawCircleV(cc, 64, Color{200, 170, 90, 255});
            for (int k = 0; k < 5; k++) {
                float a = (-200 + k * 55) * DEG2RAD;
                Color tc = k == b.telegraph + 1 ? Color{160, 30, 24, 255} : Color{70, 56, 36, 255};
                DrawTextCentered(T[k], cc.x + cosf(a) * 42, cc.y + sinf(a) * 42 - 6, 11, tc);
            }
            float a = (-200 + (b.telegraph + 1) * 55) * DEG2RAD;
            DrawLineEx(cc, {cc.x + cosf(a) * 30, cc.y + sinf(a) * 30}, 4, Color{40, 30, 20, 255});
            float hd = fmodf(-b.heading * RAD2DEG + 90 + 720, 360);
            TxtBold(TextFormat("Heading %03.0f   %.1f kn", hd, b.Speed() * 1.944f), SCREEN_W - 300, SCREEN_H - 60, 18, paper);
            TxtBold(TextFormat("Rudder %s%.0f", b.rudder > 0 ? "stbd " : b.rudder < 0 ? "port " : "", fabsf(b.rudder) * 35), SCREEN_W - 300, SCREEN_H - 36, 16, Fade(paper, 0.8f));
            if (S.W->sess.phase == Phase::SailOut) DrawTextCenteredBold("Steam out past the harbour line: the night starts there", SCREEN_W / 2.0f, 150, 18, paper);
            DrawChart({30, 110, 260, 260});
            break;
        }
        case StationKind::Sonar: DrawSonarScope(); break;
        case StationKind::Boiler:
            Gauge({SCREEN_W - 150.0f, SCREEN_H - 170.0f}, 70, b.pressure, D().greenLo, D().greenHi, D().redAt, "PRESSURE");
            TxtBold(TextFormat("Coal in the bunker: %.0f kg (%.1f sacks)   Firebox: %.1f kg", b.bunker, b.bunker / D().sackKg, b.firebox), SCREEN_W - 560, SCREEN_H - 60, 16, paper);
            if (b.valveT > 0) DrawTextCenteredBold(TextFormat("RELIEF VALVE BLOWN: %.0f s", b.valveT), SCREEN_W / 2.0f, 160, 26, Color{240, 120, 80, 255});
            break;
        case StationKind::Pumps: {
            TxtBold(TextFormat("Water in her: %.1f t", b.bilge / 1000), SCREEN_W - 300, SCREEN_H - 170, 18, paper);
            for (int s = 0; s < SEC_COUNT; s++) {
                bool leak = b.integrity[s] < D().leakBelow && !b.patched[s];
                Txt(TextFormat("%-20s %3.0f%s", SectionName(s), b.integrity[s], leak ? (b.integrity[s] < D().floodBelow ? "  FLOODING" : "  leaking") : ""), SCREEN_W - 300, SCREEN_H - 145 + s * 18.0f, 14, leak ? Color{240, 140, 110, 255} : Fade(paper, 0.8f));
            }
            break;
        }
        case StationKind::PortRod: case StationKind::StarRod: case StationKind::SternRodP: case StationKind::SternRodS:
            ReelGauge(g, c);
            TxtBold(TextFormat("Bait: shrimp %d, squid %d", g.baitShrimp, g.baitSquid), SCREEN_W - 560, SCREEN_H - 104, 14, Fade(paper, 0.7f));
            break;
        case StationKind::Gutting: {
            // the fish in hand: species, weight, grade and freshness (design doc, "Controls, HUD")
            int f = -1; for (int i = 0; i < (int)g.hold.size(); i++) if (!g.hold[i].gutted) { f = i; break; }
            float x0 = SCREEN_W - 420, y0 = SCREEN_H - 180;
            DrawRectangle((int)x0 - 12, (int)y0 - 12, 400, 130, Fade(Color{20, 16, 12, 255}, 0.8f));
            if (f < 0) TxtBold(g.hold.empty() ? "Nothing on deck" : "All gutted and iced", x0, y0, 18, paper);
            else {
                const CatchRec& r = g.hold[f];
                TxtBold(TextFormat("%s, %.1f kg", r.name.c_str(), r.kg), x0, y0, 18, paper);
                Txt(TextFormat("Grade %.0f%%   Fresh %.0f%%   worth about %.0f sh", r.grade * 100, r.fresh * 100, S.W->sess.Value(r)), x0, y0 + 26, 15, Fade(paper, 0.85f));
                float need = 1.2f + std::min(3.0f, r.kg * 0.08f);
                DrawRectangle((int)x0, (int)y0 + 54, 360, 10, Fade(paper, 0.2f));
                DrawRectangle((int)x0, (int)y0 + 54, (int)(360 * std::clamp(g.gutT / need, 0.0f, 1.0f)), 10, Color{200, 80, 60, 255});
                Txt("Hold left mouse to gut and ice it (the guts go over the rail)", x0, y0 + 72, 13, Fade(paper, 0.7f));
            }
            Txt(TextFormat("Ice: %.0f kg   On deck: %d   Iced: %d", g.ice, g.DeckFish(), (int)g.hold.size() - g.DeckFish()), x0, y0 + 96, 14, Fade(paper, 0.8f));
            break;
        }
        case StationKind::NetWinch: {
            const Trawl& n = g.net;
            float x0 = SCREEN_W - 440, y0 = SCREEN_H - 190;
            DrawRectangle((int)x0 - 12, (int)y0 - 12, 420, 140, Fade(Color{20, 16, 12, 255}, 0.8f));
            static const char* N[] = {"Stowed", "Shooting", "Down, fishing", "SNAGGED", "Hauling", "Lost"};
            TxtBold(TextFormat("The trawl: %s", N[(int)n.state]), x0, y0, 18, n.state == NetState::Snagged ? Color{240, 130, 100, 255} : paper);
            float prog = n.state == NetState::Shooting ? n.t / 15 : n.state == NetState::Hauling ? n.t / (20 + n.load / 40) : 0;
            if (prog > 0) { DrawRectangle((int)x0, (int)y0 + 30, 380, 10, Fade(paper, 0.2f)); DrawRectangle((int)x0, (int)y0 + 30, (int)(380 * std::clamp(prog, 0.0f, 1.0f)), 10, Color{120, 170, 200, 255}); }
            Txt(TextFormat("Estimated load %.0f kg   Mouth %.1f m down   Warp tension %.0f%%", n.load, n.depth, std::clamp(20 + n.load / 8 + (n.state == NetState::Snagged ? 70 : 0), 0.0f, 100.0f)), x0, y0 + 50, 14, Fade(paper, 0.85f));
            const char* how = n.state == NetState::Stowed ? "Hold left mouse to shoot the net (15 s), then tow it through the schools" :
                              n.state == NetState::Down ? "Hold left mouse to haul (a second hand by the winch guides the boom)" :
                              n.state == NetState::Snagged ? "Back her off (telegraph astern) or right mouse to cut it loose with a knife" :
                              n.state == NetState::Lost ? "The Slipway sells a new net" : "Hold left mouse";
            Txt(how, x0, y0 + 76, 13, Fade(paper, 0.7f));
            break;
        }
        case StationKind::Harpoon: {
            const float PX = (float)SCREEN_W / PIXEL_W;
            Vector2 m = GetMousePosition();
            if (!g.harpoonCannon) { DrawTextCentered("The bow mount is empty: the Slipway fits a harpoon cannon (700)", SCREEN_W / 2.0f, SCREEN_H - 64.0f, 15, Fade(paper, 0.7f)); break; }
            // the reticle, and the refraction ring: a fish below is really further off and deeper than it looks
            Vector2 bow = S.view.ToCanvas({10.2f, 0}); bow = {(bow.x - 1) * PX, (bow.y - 1) * PX};
            if (S.fp) { m = {SCREEN_W / 2.0f, SCREEN_H / 2.0f}; bow = {SCREEN_W / 2.0f, SCREEN_H + 200.0f}; }   // (first person: the crosshair; the bow is below it)
            DrawCircleLines((int)m.x, (int)m.y, 14, Fade(Color{250, 220, 160, 255}, 0.9f));
            DrawLine((int)m.x - 20, (int)m.y, (int)m.x + 20, (int)m.y, Fade(Color{250, 220, 160, 255}, 0.6f));
            DrawLine((int)m.x, (int)m.y - 20, (int)m.x, (int)m.y + 20, Fade(Color{250, 220, 160, 255}, 0.6f));
            Vector2 ref = Vector2Add(m, Vector2Scale(Vector2Subtract(m, bow), 0.2f));
            DrawCircleLines((int)ref.x, (int)ref.y, 10, Fade(Color{150, 200, 230, 255}, 0.35f));
            float x0 = SCREEN_W - 420;
            TxtBold(TextFormat("Harpoons %d   Explosive heads %d%s", g.harpoons, g.explosives, g.explosiveLoaded ? "  (explosive loaded)" : ""), x0, SCREEN_H - 84, 16, paper);
            Txt(g.harpoon.state == RodState::Fighting ? TextFormat("Fast in a %s: hold left mouse to winch, right mouse lets go", g.harpoon.biteSpec.name ? g.harpoon.biteSpec.name : "fish")
                                                      : (g.harpoonReload > 0 ? TextFormat("Reloading... %.0f s", g.harpoonReload) : "Left mouse fires (35 m, to 6 m deep); T loads an explosive head"), x0, SCREEN_H - 60, 14, Fade(paper, 0.8f));
            break;
        }
        case StationKind::Locker: {
            float x0 = SCREEN_W / 2.0f - 300, y0 = 130;
            DrawRectangle((int)x0 - 14, (int)y0 - 14, 628, 340, Color{40, 32, 24, 235});
            TxtBold(TextFormat("The deck locker: click an item to swap it with your slot %d (%s)", c.sel + 1, SlotName(c.slots[c.sel])), x0, y0, 16, paper);
            auto& G2 = S.W->G;
            for (int i = 0; i < (int)G2.locker.size() && i < 12; i++) {
                const Slot& sl = G2.locker[i];
                if (Button({x0 + (i % 2) * 300, y0 + 34 + (i / 2) * 42.0f, 290, 36}, sl.ammo > 0 && sl.it != Item::Ring ? TextFormat("%s (%d)", SlotName(sl), sl.ammo) : SlotName(sl), true, 15)) {
                    Command(CMD_LOCKER_TAKE, "", i);
                    break;
                }
            }
            if (G2.locker.empty()) Txt("Empty.", x0, y0 + 40, 15, Fade(paper, 0.7f));
            if (c.slots[c.sel].it != Item::None && Button({x0, y0 + 290, 290, 34}, "Stow what's in hand", true, 15)) Command(CMD_LOCKER_STOW);
            break;
        }
        case StationKind::Printer: {
            // the Owners' tape, newest at the bottom
            float x0 = SCREEN_W / 2.0f - 330, y0 = 120;
            DrawRectangle((int)x0 - 14, (int)y0 - 14, 688, 300, Color{226, 216, 190, 245});
            const auto& T = S.W->sess.tape;
            int n = (int)T.size(), from = std::max(0, n - 10);
            for (int i = from; i < n; i++) Txt(T[i].c_str(), x0, y0 + (i - from) * 27.0f, 15, Color{40, 32, 24, 255});
            break;
        }
        case StationKind::Davit: {
            const Skiff& sk = S.W->G.skiff;
            const char* st = sk.state == SkiffState::Lowering ? TextFormat("Lowering the skiff... %.0f%%", 100 * sk.t / D().skiffLower)
                           : sk.state == SkiffState::Recovering ? TextFormat("Hauling her up... %.0f%%", 100 * sk.t / D().skiffRecover)
                           : sk.state == SkiffState::Stowed ? "The skiff is on the davit"
                           : S.W->G.SkiffAlongside(4) ? (S.W->G.boat.Speed() > 0.4f ? "Alongside, but stop her before hauling up" : "Alongside and stopped: she can come up")
                           : sk.state == SkiffState::Lost ? "The skiff is lost for the night" : "The skiff is away";
            DrawTextCenteredBold(st, SCREEN_W / 2.0f, SCREEN_H - 64.0f, 16, Color{240, 220, 170, 255});
            break;
        }
        default:
            DrawTextCentered("(this station comes aboard in a later refit)", SCREEN_W / 2.0f, SCREEN_H - 64.0f, 14, Fade(paper, 0.6f));
            break;
    }
}
// ---------------------------------------------------------------- the dock's panels
void PanelFrame(const char* title, float w, float h, Rectangle* out) {
    Rectangle r{SCREEN_W / 2.0f - w / 2, SCREEN_H / 2.0f - h / 2 - 20, w, h};
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.35f));
    DrawRectangleRec({r.x - 6, r.y - 6, r.width + 12, r.height + 12}, Color{60, 44, 28, 255});
    DrawRectangleRec(r, Color{226, 214, 186, 255});
    DrawTextCenteredBold(title, r.x + r.width / 2, r.y + 14, 26, Color{50, 36, 24, 255});
    Txt("X to close", r.x + r.width - 96, r.y + r.height - 26, 14, Color{90, 70, 50, 255});
    *out = r;
}
static bool gChalkUps = false;   // the chalkboard shows the role upgrades (else the requests)
void Panels(Game& g) {
    if (S.panel < 0) return;
    Session& ss = S.W->sess;
    Gannet& G = S.W->G;
    Color ink{50, 36, 24, 255}, dim{100, 80, 60, 255};
    Rectangle r;
    std::string why;
    auto toast = [&](const std::string& t) { S.toast = t; S.toastT = 3; };
    switch (S.panel) {
        case (int)DockKind::Chalkboard: {
            PanelFrame("The chalkboard", 1040, 620, &r);
            float x = r.x + 34, y = r.y + 60;
            TxtBold(TextFormat("Deadline %d", ss.deadline), x, y, 20, ink);
            TxtBold(TextFormat("Quota: %.0f shillings", ss.quota), x, y + 30, 20, ink);
            TxtBold(TextFormat("Delivered to the Owners: %.0f", ss.sold), x, y + 58, 20, ss.sold >= ss.quota ? Color{40, 110, 50, 255} : ink);
            Txt("(only fish delivered at the Owners' scales count)", x, y + 84, 12, dim);
            TxtBold(TextFormat("Nights left: %d     Money: %.0f     Tokens: %d", ss.NightsLeft(), ss.money, ss.tokens), x, y + 104, 18, ink);
            bool& upPage = gChalkUps;
            if (Button({r.x + r.width - 230, r.y + 56, 200, 34}, upPage ? "Requests" : "Role upgrades", true, 15)) upPage = !upPage;
            if (!ss.consign.empty()) {
                Txt(TextFormat("The Owners' consignment: %s, in a wreck on %s%s", ss.consign.c_str(), ss.consignGround.c_str(), ss.consignMissed ? "  (one missed already: miss another and she's taken)" : ""), x + 420, y + 44, 13, ss.consignMissed ? Color{150, 50, 30, 255} : dim);
                Txt("In the hold by the count. Missed: quota +25%, the dearest fitting repossessed.", x + 420, y + 62, 12, dim);
            }
            if (upPage) {
                // role upgrades (design doc, "The crew of six"): rank 2 after the first met deadline, 3 after the second, 4 after the fourth
                const Crew& me = G.crew[S.you];
                float uy = y + 150;
                TxtBold(TextFormat("Your role: %s   (met deadlines: %d, ranks open to %d)", RoleName(me.role), ss.metCount, ss.RankOpen()), x, uy, 18, ink);
                Txt("An upgrade belongs to the role: switch roles and you start again at rank 1.", x, uy + 24, 13, dim);
                uy += 54;
                bool sameRole = S.you < 6 && ss.upRole[S.you] == (int)me.role;
                for (int rank = 2; rank <= 4; rank++) {
                    bool open = rank <= ss.RankOpen();
                    int chosen = S.you < 6 && sameRole ? ss.roleUp[S.you][rank - 2] : -1;
                    TxtBold(TextFormat("Rank %d", rank), x, uy + 8, 18, open ? ink : Fade(dim, 0.6f));
                    if (!open) Txt(rank == 2 ? "(after the first met deadline)" : rank == 3 ? "(after the second)" : "(after the fourth)", x, uy + 30, 12, dim);
                    for (int ch = 0; ch < 2; ch++) {
                        int u = RoleUpOf((int)me.role, rank, ch);
                        Rectangle b{x + 110 + ch * 420.0f, uy, 400, 64};
                        bool pick = chosen == ch, other = chosen >= 0 && chosen != ch;
                        DrawRectangleRec(b, pick ? Color{196, 214, 170, 255} : Fade(Color{200, 186, 156, 255}, other || !open ? 0.4f : 1.0f));
                        DrawRectangleLinesEx(b, pick ? 3.0f : 1.0f, pick ? Color{40, 110, 50, 255} : dim);
                        TxtBold(RoleUpName(u), b.x + 10, b.y + 8, 17, other || !open ? Fade(ink, 0.5f) : ink);
                        DrawWrapped(RoleUpNote(u), {b.x + 10, b.y + 30, b.width - 20, 32}, 13, other || !open ? Fade(dim, 0.6f) : dim);
                        if (open && chosen < 0 && CheckCollisionPointRec(GetMousePosition(), b)) {
                            DrawRectangleLinesEx(b, 2, Color{160, 110, 40, 255});
                            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) Command(CMD_ROLE_UP, "", rank * 10 + ch, std::string("Chalked up: ") + RoleUpName(u));
                        }
                    }
                    uy += 78;
                }
                // the rest of the crew's upgrades, at a glance
                uy += 6;
                TxtBold("The crew", x, uy, 16, ink); uy += 24;
                for (int k = 0; k < (int)G.crew.size(); k++) {
                    if (k == S.you) continue;
                    std::string list;
                    for (int u = 0; u < tw::UP_COUNT; u++) if (G.crew[k].Up(u)) list += std::string(list.empty() ? "" : ", ") + RoleUpName(u);
                    Txt(TextFormat("Hand %d (%s%s): %s", k + 1, RoleName(G.crew[k].role), G.crew[k].bot ? ", bot" : "", list.empty() ? "nothing yet" : list.c_str()), x, uy, 13, dim);
                    uy += 20;
                }
                if (ss.night >= 3 && Button({r.x + r.width - 330, r.y + r.height - 76, 300, 44}, "Hand in to the Owners")) { Command(CMD_COUNT); S.panel = PANEL_END; }
                break;
            }
            // the harbour's requests (one each a deadline)
            float ry = y + 140;
            TxtBold("Requests chalked up on the board", x, ry, 17, ink); ry += 26;
            for (int i = 0; i < (int)ss.requests.size(); i++) {
                const auto& q = ss.requests[i];
                std::string why; bool ready = ss.RequestReady(S.you, i, &why);
                DrawWrapped(ss.RequestText(i), {x, ry, 760, 40}, 14, q.done ? Fade(dim, 0.6f) : ink);
                if (q.done) Txt("done", x + 800, ry + 4, 15, Color{40, 110, 50, 255});
                else if (Button({x + 790, ry, 150, 28}, ready ? "Fill it" : "Not yet", ready, 13)) Command(CMD_REQUEST, "", i, "Request filled");
                if (!q.done && !ready && CheckCollisionPointRec(GetMousePosition(), {x + 790, ry, 150, 28})) Txt(why.c_str(), x + 790, ry + 30, 11, dim);
                ry += 46;
            }
            // what the crew wears, and the mini-boss drops aboard (Mother Carey above, or a cord round your neck)
            ry += 6;
            TxtBold("Charms", x, ry, 16, ink);
            std::string worn;
            for (int k = 0; k < (int)G.crew.size(); k++) if (G.crew[k].charm) worn += TextFormat("%s%s: %s", worn.empty() ? "" : "    ", k == S.you ? "you" : TextFormat("hand %d", k + 1), CharmName(G.crew[k].charm));
            Txt(worn.empty() ? "nobody wears one (the Chandler's lucky coin, the Atoll's elder, mini-boss drops)" : worn.c_str(), x + 80, ry + 2, 14, dim);
            ry += 26;
            for (int d = 0; d < (int)G.drops.size(); d++) {
                int ch = CharmOfDrop(G.drops[d]);
                Txt(TextFormat("A drop: %s%s", G.drops[d].c_str(), ch ? TextFormat("  (as a charm: %s)", CharmEffect(ch)) : ""), x, ry + 4, 14, ink);
                if (ch && Button({x + 790, ry, 150, 26}, "Wear it", true, 13)) Command(CMD_WEAR_DROP, "", d, "Worn on a cord");
                ry += 30;
            }
            Txt(TextFormat("Boss lures %d%s   Legend lures %d   Tag gun: %s   Rare-fish lures %d   Free attachments %d", G.bossLures, G.AnyWears(CH_BRASS_LURE) ? " (half price)" : "", G.legendLures, G.tagGun ? "aboard" : "no", G.rareLures, ss.freeAttach), x, r.y + r.height - 34, 13, dim);
            if (ss.night >= 3 && Button({r.x + r.width - 330, r.y + r.height - 76, 300, 44}, "Hand in to the Owners")) { Command(CMD_COUNT); S.panel = PANEL_END; }
            break;
        }
        case (int)DockKind::Chandler: {
            PanelFrame("The Chandler", 1000, 680, &r);
            float x = r.x + 26, y = r.y + 56;
            TxtBold(TextFormat("Money %.0f     Ice %.0f kg     Shrimp %d     Squid %d     Coal %.0f kg     Chum %d", ss.money, G.ice, G.baitShrimp, G.baitSquid, G.boat.bunker, G.chum), x, y, 16, ink);
            const auto& I = ChandlerItems();
            int half = ((int)I.size() + 1) / 2;
            for (int i = 0; i < (int)I.size(); i++) {
                float cx = x + (i / half) * 480, yy = y + 30 + (i % half) * 36;
                TxtBold(I[i].name, cx, yy + 6, 15, ink);
                if (Button({cx + 360, yy, 96, 30}, TextFormat("%d sh", I[i].price), ss.money >= I[i].price, 15)) {
                    Command(CMD_BUY, I[i].id, 0, std::string("Bought: ") + I[i].name + " (" + I[i].note + ")");
                }
                Rectangle hot{cx, yy, 350, 30};
                if (CheckCollisionPointRec(GetMousePosition(), hot)) Txt(I[i].note, r.x + 26, r.y + r.height - 30, 14, dim);
            }
            break;
        }
        case (int)DockKind::Market: case (int)DockKind::Scales: {
            // the two places a fish goes at the dock (design doc, "Three ways to use a fish"): the Fish Market pays the
            // ship's purse (glut applies; nothing toward the quota); the Owners' scales credit the quota at full value
            // (no glut, no shillings; under 70% fresh turned away). What is left in the hold is kept, for barter or the larder.
            bool scales = S.panel == (int)DockKind::Scales;
            PanelFrame(scales ? "The Owners' quota scales" : "The Fish Market", 780, 560, &r);
            float x = r.x + 30, y = r.y + 54;
            Txt(scales ? TextFormat("Delivered this deadline: %.0f of %.0f.  Only fish delivered here count toward the quota.", ss.sold, ss.quota)
                       : TextFormat("Ship's purse: %.0f shillings.  Market money never pays the quota.", ss.money), x, y, 14, dim);
            if (!G.hold.empty()) {
                float tot = 0;
                int shown = 0;
                for (int i = 0; i < (int)G.hold.size(); i++) {
                    const CatchRec& c = G.hold[i];
                    float v = scales ? ss.QuotaValue(c) : ss.Value(c); tot += v;
                    if (shown++ >= 11) continue;
                    float yy = y + 8 + shown * 30.0f;
                    Txt(c.name.c_str(), x, yy + 4, 15, ink);
                    Txt(KgText(c.kg).c_str(), x + 190, yy + 4, 15, ink);
                    Txt(TextFormat("grade %.0f%%", c.grade * 100), x + 270, yy + 4, 15, ink);
                    Txt(TextFormat("fresh %.0f%%", c.fresh * 100), x + 375, yy + 4, 15, c.fresh < QUOTA_MIN_FRESH ? Color{150, 60, 40, 255} : ink);
                    Txt(c.iced ? "iced" : c.gutted ? "gutted" : "on deck", x + 480, yy + 4, 15, c.iced ? ink : Color{150, 60, 40, 255});
                    if (scales && v <= 0) Txt("turned away", x + 560, yy + 4, 15, Color{150, 60, 40, 255});
                    else {
                        Txt(TextFormat("%.1f", v), x + 560, yy + 4, 15, ink);
                        if (Button({x + 620, yy, 96, 26}, scales ? "Deliver" : "Sell", true, 14)) { Command(scales ? CMD_DELIVER : CMD_SELL, "", i); break; }
                    }
                }
                if ((int)G.hold.size() > 11) Txt(TextFormat("... and %d more", (int)G.hold.size() - 11), x, y + 8 + 12 * 30.0f, 15, dim);
                TxtBold(scales ? TextFormat("About %.0f against the quota", tot) : TextFormat("About %.0f shillings (the glut counts as they're weighed)", tot), x, r.y + r.height - 110, 17, ink);
                if (Button({r.x + r.width / 2 - 130, r.y + r.height - 76, 260, 44}, scales ? "Deliver every fresh fish" : "Sell the whole catch"))
                    Command(scales ? CMD_DELIVER : CMD_SELL, "", -1, scales ? "Delivered: the Owners weigh it in" : "Sold: the market weighs it out");
                Txt("Anything you neither deliver nor sell stays in the hold (iced fish lose a quarter overnight).", x, r.y + r.height - 26, 12, dim);
            } else {
                const auto& L = scales ? ss.lastDelivery : ss.lastSale;
                int k = 0;
                for (const auto& l : L) if (k++ < 11) {
                    float yy = y + 8 + k * 26.0f;
                    Txt(l.name.c_str(), x, yy, 14, ink);
                    if (scales) Txt(TextFormat("%.1f kg x %.1f sh x grade %.0f%% x fresh %.0f%%%s", l.kg, l.price, l.grade * 100, l.fresh * 100, l.bonus > 1 ? " x first 150%" : ""), x + 170, yy, 14, dim);
                    else Txt(TextFormat("%.1f kg x %.1f sh x grade %.0f%% x fresh %.0f%% x glut %.0f%%%s", l.kg, l.price, l.grade * 100, l.fresh * 100, l.glut * 100, l.bonus > 1 ? " x first 150%" : ""), x + 170, yy, 14, dim);
                    Txt(TextFormat("%.1f", l.value), x + 640, yy, 14, ink);
                }
                if (!L.empty()) TxtBold(scales ? TextFormat("Credited %.0f against the quota", ss.lastDeliveryTotal) : TextFormat("Paid %.0f shillings", ss.lastSaleTotal), x, r.y + r.height - 100, 20, ink);
                else TxtBold(scales ? "Nothing to deliver." : "Nothing to sell.", x, y + 30, 18, dim);
            }
            break;
        }
        case (int)DockKind::Office:
            PanelFrame("The Owners' office", 560, 260, &r);
            TxtBold("The window is shuttered. No salvage to sell.", r.x + 40, r.y + 80, 18, ink);
            Txt("(Wrecks and diving come aboard in a later stage. Salvage pays 40% after night one,", r.x + 40, r.y + 120, 14, dim);
            Txt("70% after night two, 100% at the deadline count.)", r.x + 40, r.y + 140, 14, dim);
            break;
        case (int)DockKind::Gunsmith: {
            // the Gunsmith (design doc v2, "Weapons"): guns and blades for sale; the gun in the selected slot's three
            // damage upgrades and up to three attachments; ammunition into the ship's magazine stock (restock at the locker)
            PanelFrame("The Gunsmith", 1180, 620, &r);
            const Crew& me = G.crew[S.you];
            float x = r.x + 24, y = r.y + 54;
            TxtBold(TextFormat("Purse %.0f", ss.money), x, y, 16, ink);
            // ---- for sale
            TxtBold("For sale", x, y + 26, 16, ink);
            int row = 0;
            for (int i = 0; i < (int)Weapons().size(); i++) {
                const WeaponDef& w = Weapons()[i];
                bool here = (w.where == "gunsmith" || (w.where == "gunsmith3" && ss.deadline >= 3)) && w.cls != WC_THROWN;   // (thrown weapons wait for throwing)
                if (!here) continue;
                float yy = y + 50 + row * 24.0f; row++;
                Txt(w.name.c_str(), x, yy + 3, 13, ink);
                Txt(w.Gun() ? TextFormat("%.0f%s, %d", w.dmg, w.pellets > 1 ? TextFormat("x%d", w.pellets) : "", w.mag) : TextFormat("%.0f", w.dmg), x + 160, yy + 3, 13, dim);
                if (Button({x + 250, yy, 92, 22}, TextFormat("%d sh", w.price), ss.money >= w.price, 12)) Command(CMD_GUN_BUY, w.id, 0, std::string("Bought: ") + w.name);
            }
            // ---- the gun in hand
            float mx = r.x + 400;
            const Slot& sl = me.slots[me.sel];
            TxtBold(TextFormat("In hand (slot %d): %s", me.sel + 1, SlotName(sl)), mx, y + 26, 16, ink);
            if (sl.it == Item::Weapon && sl.wpn >= 0) {
                const WeaponDef& w = Weapons()[sl.wpn];
                Txt(TextFormat("Damage %.1f   Magazine %d (%d loaded, %d spare)   Noise %.0f", WeaponDamage(w, sl.lvl, sl.att), WeaponMagazine(w, sl.att), sl.ammo, sl.spare, WeaponNoise(w, sl.att)), mx, y + 50, 13, dim);
                Txt(w.special.c_str(), mx, y + 68, 12, dim);
                int up = UpgradePrice(w, sl.lvl);
                Txt(TextFormat("Damage upgrades: %d of 3", sl.lvl), mx, y + 94, 14, ink);
                if (up > 0 && Button({mx + 220, y + 90, 150, 24}, TextFormat("+15%% for %d", up), ss.money >= up, 12)) Command(CMD_GUN_UPGRADE, "", me.sel, "Upgraded");
                TxtBold("Attachments (up to three)", mx, y + 126, 14, ink);
                int ar = 0;
                for (int a = 0; a < (int)Attachments().size(); a++) {
                    const AttachmentDef& at = Attachments()[a];
                    if (at.where != "gunsmith" || !AttachmentFits(at, w)) continue;
                    float yy = y + 150 + ar * 26.0f; ar++;
                    bool fitted = HasAttachment(sl.att, at.id.c_str());
                    Txt(at.name.c_str(), mx, yy + 3, 13, ink);
                    Txt(at.effect.c_str(), mx + 140, yy + 3, 11, dim);
                    if (Button({mx + 460, yy, 80, 22}, fitted ? "Fitted" : TextFormat("%d sh", at.price), !fitted && ss.money >= at.price, 12)) Command(CMD_GUN_ATTACH, at.id, me.sel, std::string("Fitted: ") + at.name);
                }
            } else Txt("Pick a gun's slot (1-4) to upgrade it here.", mx, y + 50, 13, dim);
            // ---- ammunition
            float ax = r.x + r.width - 230;
            TxtBold("Ammunition (to the locker)", ax, y + 26, 15, ink);
            static const char* K[] = {"rounds", "shells", "spears", "flares", "pellets", "rivets"};
            for (int k = 0; k < 6; k++) {
                int* stock = G.AmmoStock(K[k]);
                int per = 0; for (const auto& w : Weapons()) if (w.ammo == K[k] && w.ammoPrice > 0) { per = w.ammoPrice; break; }
                float yy = y + 52 + k * 30.0f;
                Txt(TextFormat("%s: %d", K[k], stock ? *stock : 0), ax, yy + 4, 14, ink);
                if (per > 0 && Button({ax + 110, yy, 100, 24}, TextFormat("%d for %d", AmmoPack(K[k]), per * AmmoPack(K[k])), ss.money >= per * AmmoPack(K[k]), 12)) Command(CMD_AMMO, K[k], 0, "Into the locker");
            }
            Txt("A hand carries the loaded magazine and one spare reload; restock at the deck locker. Wet powder misfires in rain.", r.x + 24, r.y + r.height - 26, 12, dim);
            break;
        }
        case (int)DockKind::Slipway: {
            PanelFrame("The Slipway", 760, 500, &r);
            float x = r.x + 30, y = r.y + 60;
            TxtBold(TextFormat("Money %.0f", ss.money), x, y, 17, ink);
            const auto& I = SlipwayItems();
            for (int i = 0; i < (int)I.size(); i++) {
                float yy = y + 34 + i * 42;
                bool fitted = ss.slip[i] && std::string(I[i].id) != "plates";
                TxtBold(I[i].name, x, yy + 6, 17, ink);
                Txt(I[i].note, x + 240, yy + 8, 14, dim);
                if (Button({r.x + r.width - 150, yy, 120, 34}, fitted ? "Fitted" : TextFormat("%d sh", I[i].price), !fitted && ss.money >= I[i].price, 16)) {
                    Command(CMD_SLIP, "", i, std::string("Fitted: ") + I[i].name);
                }
            }
            break;
        }
        case PANEL_ELDER: {
            // the Atoll's elder: fish in (at 150% of their value, as trade), his goods out; never shillings
            {
                int lk = G.skiff.landing >= 0 && G.skiff.landing < (int)G.landings.size() ? G.landings[G.skiff.landing].kind : LK_ATOLL;
                PanelFrame(lk == LK_SEALROCK ? "Old Hoskins, the last sealer" : lk == LK_CANNERY ? "The cannery's last foreman" : lk == LK_SHELF ? "The smugglers' quartermaster" : lk == LK_BONEBEACH ? "The hermit of Bone Beach" : lk == LK_STAIR ? "The Keeper of the Stair" : lk == LK_CULT ? "The cult quartermaster" : "The tribe's elder",
                           640, lk == LK_SEALROCK || lk == LK_CANNERY ? 260 : 440, &r);
            }
            float x = r.x + 34, y = r.y + 60;
            if (G.skiff.landing < 0 || G.skiff.landing >= (int)G.landings.size()) { S.panel = -1; break; }
            const Landing& L = G.landings[G.skiff.landing];
            const Crew& yo = G.crew[S.you];
            if (L.kind == LK_SEALROCK || L.kind == LK_CANNERY) {
                // Seal Rock's Old Hoskins (birds, at double) and the Cannery Pier's last foreman (cooked fish, at 150%): shillings
                bool seal = L.kind == LK_SEALROCK;
                DrawWrapped(seal ? "\"Birds. Gulls, pelicans, the black frigates. I pay double what the market would, and in silver. Fish? Got the whole sea of them.\""
                                 : "\"The line's been shut twenty years but the boiler still draws. Bring me fish off that boiler, cooked through, and I'll pay half again what the market would. Tins don't care where it came from.\"",
                            {x, y, r.width - 68, 70}, 15, ink);
                if (yo.carrying) {
                    bool ok = seal ? (BirdKindOf(yo.carry.name) >= 0 || (yo.carry.sp >= 0 && Species().sp[yo.carry.sp].stealsDeck)) : (yo.carry.cooked && !yo.carry.junk);
                    float v = ss.Value(yo.carry) * (seal ? 2.0f : 1.5f);
                    Txt(TextFormat("In your arms: %s, %s%s", yo.carry.name.c_str(), KgText(yo.carry.kg).c_str(), yo.carry.cooked ? TextFormat(" (cooked, x%.2f)", yo.carry.cook) : ""), x, y + 86, 14, ink);
                    if (ok) { if (Button({x + 380, y + 80, 180, 28}, TextFormat("Sell it (%.0f sh)", v), true, 13)) Command(CMD_ELDER_GIVE, "", 0, seal ? "Hoskins pays" : "The foreman pays"); }
                    else Txt(seal ? "He shakes his head: birds only." : "He shakes his head: cooked fish only.", x + 380, y + 86, 14, dim);
                } else Txt(seal ? "Carry a bird to him (shoot a thief down: its body is a catch)." : "Cook a fish on the boiler, then carry it to him.", x, y + 86, 14, dim);
                TxtBold(TextFormat("Purse: %.0f shillings", ss.money), x, y + 130, 16, ink);
                break;
            }
            bool shelf = L.kind == LK_SHELF, boneB = L.kind == LK_BONEBEACH, stair = L.kind == LK_STAIR, cult = L.kind == LK_CULT;
            if (L.kind == LK_ATOLL && G.foughtCanoes) { DrawWrapped("He turns his back. You fought his people's canoes: there will be no trade tonight, or any night.", {x, y, r.width - 68, 80}, 16, ink); break; }
            Txt(shelf ? "He buys salvage at its full worth, in shillings, and sells what came in on the last boat for shillings too."
                      : cult ? "Dark goods for dark money. He takes nothing from you but shillings."
                      : stair ? "He takes fish as offerings. An offering of 100 lets one of his bowls be taken; take one unpaid and he lifts his hand."
                      : boneB ? "He wants bones and skulls, and gives their worth in his own gear." : "He takes only fish, never shillings, and gives half again their worth in his own goods.", x, y, 14, dim);
            float purse = shelf || cult ? ss.money : L.elderCredit;
            TxtBold(shelf || cult ? TextFormat("Purse: %.0f shillings", purse) : stair ? TextFormat("Your offerings: %.0f", purse) : TextFormat("Your trade with him: %.0f", purse), x, y + 26, 17, ink);
            bool takes = yo.carrying && !cult && (shelf ? yo.carry.junk : boneB ? (yo.carry.junk && (yo.carry.name.find("bone") != std::string::npos || yo.carry.name.find("skull") != std::string::npos)) : !yo.carry.junk);
            if (takes) {
                Txt(TextFormat("In your arms: %s, %s%s", yo.carry.name.c_str(), KgText(yo.carry.kg).c_str(), yo.carry.cooked ? TextFormat(" (cooked, x%.2f)", yo.carry.cook) : ""), x, y + 56, 14, ink);
                float v = ss.Value(yo.carry) * (shelf || boneB || stair ? 1.0f : 1.5f);
                if (Button({x + 380, y + 50, 180, 28}, TextFormat(shelf ? "Sell it (%.0f sh)" : stair ? "Offer it (%.0f)" : "Give it (%.0f)", v), true, 13)) Command(CMD_ELDER_GIVE, "", 0, shelf ? "The quartermaster pays" : "He takes it");
            } else Txt(shelf ? "Carry salvage to him (a chest, a strongbox, junk from the sea)." : cult ? "" : boneB ? "Carry bones or a skull to him." : "Carry a fish to him.", x, y + 56, 14, dim);
            if (stair) break;   // (the Keeper sells nothing)
            float yy = y + 100;
            TxtBold("His goods (sold nowhere else)", x, yy, 15, ink); yy += 28;
            for (const auto& id : ElderStock(L.kind)) {
                bool att = id.rfind("att:", 0) == 0;
                std::string name, note; int price = 0;
                if (id.rfind("charm:", 0) == 0) { int ch = id == "charm:shark" ? CH_SHARK_TOOTH : CH_ANKLET; name = std::string(CharmName(ch)) + " (a charm)"; note = CharmEffect(ch); price = ch == CH_SHARK_TOOTH ? 120 : 90; }
                else if (att) { int ai = AttachmentIndex(id.substr(4)); if (ai < 0) continue; name = Attachments()[ai].name; note = Attachments()[ai].effect; price = Attachments()[ai].price; }
                else { int wi = WeaponIndex(id); if (wi < 0) continue; name = Weapons()[wi].name; note = Weapons()[wi].special; price = Weapons()[wi].price; }
                TxtBold(name.c_str(), x, yy + 3, 14, ink);
                Txt(note.c_str(), x + 170, yy + 4, 12, dim);
                if (Button({r.x + r.width - 150, yy, 116, 24}, TextFormat(shelf || cult ? "%d sh" : boneB ? "%d in bones" : "%d in fish", price), purse >= price, 12)) Command(CMD_ELDER_BUY, id, 0, std::string("Bought: ") + name);
                yy += 30;
            }
            break;
        }
        case PANEL_CHART: {
            PanelFrame("The chart table", 640, 420, &r);
            float x = r.x + 40, y = r.y + 64;
            // the grounds: tonight's is chosen here (one chart each, from the same quay)
            struct GroundRow { const char* key; const char* name; const char* note; };
            static const GroundRow GR[4] = {
                {"lagoon", "Eclipse Lagoon", "3-40 m. Coal 10 kg. Fish value low. The reef tide falls all night."},
                {"weeds", "The Weeds", "5-60 m. Coal 25 kg. Kelp fouls the screw, nets and lines; the big fish run the seaward edge."},
                {"grotto", "The Grotto", "4-45 m. Coal 40 kg. A cave through a sea arch that closes 02:30-04:00; sound doubles."},
                {"atlantis", "Atlantis Waters", "8-400 m. Coal 80 kg. The richest water, over the drowned city; the Pale Eye watches."}};
            for (int k = 0; k < 4; k++) {
                bool here = ss.ground == GR[k].key, open = true;
                float yy = y + k * 34.0f;
                if (Button({x, yy, 190, 28}, GR[k].name, open && !here, 14)) Command(CMD_GROUND, GR[k].key, 0, std::string("Bound for ") + GR[k].name);
                Txt(GR[k].note, x + 204, yy + 6, 13, here ? ink : Fade(dim, open ? 0.9f : 0.5f));
            }
            TxtBold(TextFormat("Night %d of 3.   Bunker %.0f kg (%.0f to reach the ground).   Back across the harbour line before 05:00.", ss.night + 1, G.boat.bunker, ss.CoalToReach()), x, y + 150, 15, ink);
            bool ok = ss.CanCastOff(&why);
            if (!ok) Txt(why.c_str(), x, y + 176, 15, Color{150, 50, 40, 255});
            if (Button({r.x + r.width / 2 - 110, r.y + r.height - 80, 220, 46}, "Cast off", ok)) {
                Command(CMD_CASTOFF, "", 0, "Cast off: raise steam and steer out past the harbour line");
                S.panel = -1;
            }
            break;
        }
        case PANEL_END: {
            bool met = ss.phase == Phase::Result;
            PanelFrame(met ? "QUOTA MET" : "GANNET REPOSSESSED", 600, 300, &r);
            TxtBold(TextFormat("Sold %.0f against a quota of %.0f", ss.sold, ss.quota), r.x + 40, r.y + 80, 20, ink);
            Txt(ss.tape.empty() ? "" : ss.tape.back().c_str(), r.x + 40, r.y + 120, 14, dim);
            if (met && ss.consignDone) {   // the Owners' reward for the consignment, chosen at the office
                Txt("The consignment came home. The Owners' reward:", r.x + 40, r.y + 148, 14, ink);
                for (int k = 0; k < 2; k++) {
                    Rectangle b{r.x + 40 + k * 265.0f, r.y + 168, 250, 30};
                    bool pick = (ss.consignReward == 1) == (k == 1);
                    if (Button(b, k == 0 ? "A Slipway fitting, gratis" : "10% off the next quota", true, 13)) Command(CMD_CONSIGN_REWARD, "", k);
                    if (pick) DrawRectangleLinesEx(b, 3, Color{40, 110, 50, 255});
                }
            }
            if (met) { if (Button({r.x + r.width / 2 - 130, r.y + r.height - 80, 260, 46}, "Next deadline")) { Command(CMD_CONTINUE); S.panel = -1; } }
            else if (Button({r.x + r.width / 2 - 130, r.y + r.height - 80, 260, 46}, "Back to the arcade")) LeaveTrawlMatch(g);
            break;
        }
    }
}
void Hud(Game& g) {
    const Gannet& G = S.W->G;
    Color paper{230, 220, 196, 255};
    // the Bosun sees the hull's sections; everyone sees the log of what just happened
    const Crew& c = G.crew[S.you];
    TxtBold(TextFormat("%s", RoleName(c.role)), 20, 16, 18, RoleColor(c.role));
    if (c.role == Role::Bosun) for (int s = 0; s < SEC_COUNT; s++) {
        float in = G.boat.integrity[s];
        Color col = in < D().floodBelow ? Color{220, 60, 40, 255} : in < D().leakBelow ? Color{230, 160, 60, 255} : Color{120, 180, 120, 255};
        DrawRectangle(20 + (s / 2) * 26, 44 + (s % 2) * 14, 22, 10, Fade(col, 0.8f));
    }
    for (int i = 0; i < (int)G.log.size(); i++) {
        float age = (float)((int)G.log.size() - i);
        if (age > 5) continue;
        Txt(G.log[i].c_str(), 20, SCREEN_H - 40 - age * 18, 15, Fade(paper, 0.9f - age * 0.12f));
    }
    if (!G.hold.empty()) { float kg = 0; for (const auto& h : G.hold) kg += h.kg; Txt(TextFormat("In the hold: %d fish, %.0f kg", (int)G.hold.size(), kg), SCREEN_W - 260, 16, 15, Fade(paper, 0.8f)); }
    if (G.junkBottles + G.junkKeys + G.junkCharts > 0)
        Txt(TextFormat("Junk: %d bottle%s, %d key%s, chart pieces %d", G.junkBottles, G.junkBottles == 1 ? "" : "s", G.junkKeys, G.junkKeys == 1 ? "" : "s", G.junkCharts), SCREEN_W - 260, 34, 14, Fade(paper, 0.7f));
    // the crew list: who is where (bots and what they're about); G orders the hand nearest the station you point at
    if (G.botsOn || S.net) {
        float y = 40;
        for (int i = 0; i < (int)G.crew.size(); i++) {
            if (i == S.you) continue;
            const Crew& o = G.crew[i];
            Color rc = ColorLerp(RoleColor(o.role), paper, 0.45f);   // (the Bosun's navy is lost on a night sea)
            bool ordered = i < (int)G.brains.size() && G.brains[i].order >= 0;
            Txt(TextFormat("%s%s", RoleName(o.role), ordered ? " *" : ""), SCREEN_W - 260, y, 13, Fade(rc, o.dead ? 0.4f : 0.9f));
            // a player's hand shows the player; a bot's what it's about
            std::string doing = G.BotDoing(i);
            if (S.net && !o.bot) {
                int seat = S.net->SeatOfPlayer(i);
                if (seat >= 0 && seat < arcade::MAX_PLAYERS) doing = S.net->seats[seat].name + (o.station >= 0 ? std::string(": ") + Stations()[o.station].name : o.overboard ? ": in the water" : "");
            }
            // the crew board (design doc v2, "Hatches and light"): who is below, out in the skiff or ashore
            const char* where = o.deck == 1 ? (G.SpaceAt(o.p, 1) == 3 ? "[fo'c'sle] " : G.SpaceAt(o.p, 1) == 2 ? "[hold] " : "[engine] ") : o.deck == DECK_SKIFF ? "[skiff] " : o.deck == DECK_SHORE ? "[ashore] " : "";
            doing = where + doing;
            Txt(doing.c_str(), SCREEN_W - 190, y, 13, Fade(paper, o.dead ? 0.35f : o.overboard ? 1.0f : 0.65f));
            y += 16;
        }
        if (G.botsOn) Txt("G: order a hand to the station you point at   F: follow me", SCREEN_W - 260, y + 2, 11, Fade(paper, 0.4f));
    }
    // a leak where you stand: E patches it (6 s, 3 for a Bosun) with the ship's kits
    if (!c.overboard && !c.dead && c.station < 0 && !G.moored) {
        int s = SectionAt(c.p);
        if (c.patchSec >= 0) DrawTextCenteredBold(TextFormat("Patching the leak... %.1f s (stand still)", std::max(0.0f, c.patchT)), SCREEN_W / 2.0f, SCREEN_H - 110.0f, 20, Color{240, 200, 140, 255});
        else if (G.boat.integrity[s] < D().leakBelow && !G.boat.patched[s])
            DrawTextCenteredBold(G.PatchKits() > 0 ? TextFormat("E: patch the leak (%s)", SectionName(s)) : "Holed here, and no patch kit aboard", SCREEN_W / 2.0f, SCREEN_H - 110.0f, 20, Color{240, 160, 120, 255});
    }
    // the four slots and what's wrong with you (design doc: "Always on screen: the four inventory slots")
    for (int k = 0; k < 4; k++) {
        const Slot& sl = c.slots[k];
        Rectangle r{20.0f + k * 104, SCREEN_H - 196.0f, 98, 40};
        DrawRectangleRec(r, Fade(Color{20, 16, 12, 255}, 0.7f));
        DrawRectangleLinesEx(r, k == c.sel ? 2 : 1, k == c.sel ? Color{230, 200, 130, 255} : Fade(paper, 0.3f));
        Txt(TextFormat("%d %s", k + 1, sl.it == Item::None ? "" : SlotName(sl)), r.x + 6, r.y + 5, 13, Fade(paper, sl.it == Item::None ? 0.4f : 0.9f));
        if (sl.it == Item::Rifle || sl.it == Item::Shotgun || sl.it == Item::Speargun || sl.it == Item::Flare || sl.it == Item::Charge || sl.it == Item::Bandage)
            Txt(TextFormat("x%d", sl.ammo), r.x + 6, r.y + 22, 12, Fade(paper, 0.7f));
        if (sl.it == Item::Ring && sl.ammo == 0) Txt("out: hold to haul", r.x + 6, r.y + 22, 11, Fade(paper, 0.7f));
    }
    if (c.injuries) {
        std::string inj; for (int b = 1; b <= 8; b <<= 1) if (c.Has(b)) inj += (inj.empty() ? "" : ", ") + std::string(InjuryName(b));
        Txt(("Hurt: " + inj).c_str(), 20, SCREEN_H - 216.0f, 13, Color{240, 150, 120, 255});
    }
    if (c.overboard && !c.dead) {
        // the breathing vignette narrows with the time left
        float left = std::max(0.0f, c.drownT), frac = left / 25;
        for (int k = 0; k < 8; k++) DrawRing({SCREEN_W / 2.0f, SCREEN_H / 2.0f}, 200 + frac * 500 + k * 40, 2000, 0, 360, 48, Fade(BLACK, 0.12f));
        DrawTextCenteredBold(TextFormat("IN THE WATER  %.0f s", left), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 60, 30, Color{230, 90, 80, 255});
        DrawTextCentered("Swim to the stern ladder (the screw must be stopped), or catch the life ring", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 24, 15, paper);
    }
    if (c.dead) DrawTextCenteredBold(TextFormat("Dead for the night (%s). A ghost can ring the bell.", c.cause.c_str()), SCREEN_W / 2.0f, 90, 18, Color{190, 220, 240, 255});
    if (G.boat.sunk) DrawTextCenteredBold("The Gannet has foundered", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 30, Color{230, 80, 70, 255});
    // the wheelhouse clock (or a pocket watch): the only way to read the time
    bool inWheelhouse = c.deck == 0 && c.p.x > 0.9f && c.p.x < 5.1f && fabsf(c.p.y) < 2.1f;
    if ((S.W->sess.phase == Phase::Night || S.W->sess.phase == Phase::SailOut) && (inWheelhouse || G.watch))
        TxtBold(S.W->sess.ClockText(), SCREEN_W / 2.0f - 30, 16, 26, S.W->sess.clock > 480 ? Color{240, 140, 100, 255} : paper);
    // the telegraph's newest tape, for a moment
    if (S.W->sess.tape.size() != S.tapeSeen) { S.tapeSeen = S.W->sess.tape.size(); S.tapeT = 6; }
    if (S.tapeT > 0 && !S.W->sess.tape.empty()) DrawTextCentered(S.W->sess.tape.back(), SCREEN_W / 2.0f, 52, 15, Fade(Color{230, 215, 170, 255}, std::min(1.0f, S.tapeT)));
    if (S.toastT > 0) DrawTextCenteredBold(S.toast, SCREEN_W / 2.0f, SCREEN_H / 2.0f + 170, 18, Fade(paper, std::min(1.0f, S.toastT)));
    // tonight's variant, chalked under the tape for the first minutes of the night
    if (S.W->sess.phase == Phase::Night && S.W->sess.variant != Variant::None && S.W->sess.clock < 30) {
        float a = std::min(1.0f, (30 - S.W->sess.clock) / 6);
        DrawTextCenteredBold(VariantName(S.W->sess.variant), SCREEN_W / 2.0f, 74, 18, Fade(Color{240, 200, 120, 255}, a));
        DrawTextCentered(VariantNote(S.W->sess.variant), SCREEN_W / 2.0f, 96, 14, Fade(paper, a));
    }
    // the shakedown: Kess's chalk lines on the deck, an aside about what you're doing, and a way out
    if (S.W->sess.shake.on || (S.W->sess.shake.done && S.W->sess.phase == Phase::Dock)) {
        const auto& sh = S.W->sess.shake;
        Rectangle r{20, SCREEN_H - 150.0f, 640, 70};
        DrawRectangleRec(r, Fade(Color{20, 24, 30, 255}, 0.75f)); DrawRectangleLinesEx(r, 1, Color{180, 150, 90, 255});
        Txt(TextFormat("Lesson %d of %d", std::min(sh.step + 1, Session::SHAKE_STEPS), Session::SHAKE_STEPS), r.x + 12, r.y + 6, 13, Fade(paper, 0.7f));
        DrawWrapped(sh.line, {r.x + 12, r.y + 24, r.width - 24, 44}, 14, Color{240, 220, 170, 255});
        if (sh.asideT > 0) DrawTextCenteredBold(sh.aside, SCREEN_W / 2.0f, SCREEN_H / 2.0f + 140, 18, Fade(Color{240, 220, 170, 255}, std::min(1.0f, sh.asideT)));
        if (sh.on) { if (Button({r.x + r.width + 10, r.y + 18, 150, 32}, "Skip the shakedown", true, 14)) S.W->sess.SkipShakedown(); }
        else if (Button({r.x + r.width + 10, r.y + 18, 150, 32}, "Back to the arcade", true, 14)) { LeaveTrawlMatch(g); return; }
    }
    // Canoe night: the war canoe alongside waits a minute for an answer
    if (S.W->sess.canoe == CanoeState::Alongside) {
        Rectangle r{SCREEN_W / 2.0f - 330, SCREEN_H / 2.0f - 120, 660, 200};
        DrawRectangleRec(r, Fade(Color{20, 24, 30, 255}, 0.88f)); DrawRectangleLinesEx(r, 2, Color{180, 150, 90, 255});
        bool market = S.W->sess.variant == Variant::MermenMarket;
        DrawTextCenteredBold(market ? "Feral Mermen hang on the rail" : "A war canoe is alongside", SCREEN_W / 2.0f, r.y + 20, 20, Color{240, 200, 120, 255});
        DrawTextCentered(market ? TextFormat("Abalone and green old relics, for fish. %.0f s before they lose interest.", std::max(0.0f, 60 - S.W->sess.canoeT))
                                : TextFormat("They want fish or silver. %.0f s before they help themselves.", std::max(0.0f, 60 - S.W->sess.canoeT)), SCREEN_W / 2.0f, r.y + 50, 15, paper);
        const char* L[3] = {"1: Trade a quarter of the hold (bait, ice, a patch kit)", "2: Pay tribute (a tenth of the money, 15 at least)", "3: Refuse them"};
        const char* M[2] = {"1: Trade a fifth of the hold for abalone and a relic", "2: Wave them off"};
        int nOpt = market ? 2 : 3;
        for (int k = 0; k < nOpt; k++) if (Button({r.x + 30, r.y + 80 + k * 38.0f, 600, 32}, market ? M[k] : L[k], true, 15) || IsKeyPressed(KEY_ONE + k)) Command(CMD_CANOE, "", market && k == 1 ? CANOE_REFUSE : k);
    }
    // the skiff: her state while you're in her (or at the davit), and what the keys do
    if (S.panel < 0 && !c.dead) {
        const Skiff& sk = G.skiff;
        int dv = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Davit) dv = i;
        bool atDavit = c.deck == 0 && dv >= 0 && Vector2Distance(c.p, Stations()[dv].at) < 1.6f;
        std::string line;
        if (c.deck == DECK_SKIFF && !c.overboard) {
            DrawTextCenteredBold(TextFormat("Skiff   hull %.0f/%.0f   load %.0f/%.0f kg   roll %.0f deg", std::max(0.0f, sk.integrity), G.SkiffHullMax(), sk.LoadKg(), G.SkiffLoadMax(), sk.roll * RAD2DEG), SCREEN_W / 2.0f, SCREEN_H - 118.0f, 15, fabsf(sk.roll * RAD2DEG) > 15 ? Color{240, 120, 90, 255} : paper);
            if (c.skiffLine) {
                ReelGauge(G, c);
                if (G.SkiffAlongside(4)) line = "E: up the stern ladder";
                Txt(G.bossArmed ? "A BOSS LURE is on: cast it into the Crest Pass (R takes it off)" : G.bossLures > 0 ? TextFormat("R: put on a boss lure (%d aboard)", G.bossLures) : "", 20, SCREEN_H - 272, 14, G.bossArmed ? Color{250, 200, 110, 255} : Fade(paper, 0.7f));
            }
            else line = G.SkiffAlongside(4) ? "Left / right mouse: the oars, on a beat.   E: up the stern ladder   T: her line" : sk.crabT > 0 ? "Caught a crab! Keep the rhythm" : "Left mouse the port oar, right the starboard, in turn on a steady beat (both pull straight).   T: her line";
            if (sk.engine) line = "The launch is running: hold left / right mouse to steer.   X: stop the engine";
            else if (G.SkiffUp(SU_LAUNCH) && line.find("crab") == std::string::npos) line += "   X: the launch engine";
            if (G.SkiffUp(SU_MORTAR)) line += TextFormat("   W: flare mortar (%d)", sk.mortar);
            { int mk = S.W->eco.g ? S.W->eco.MarkAt(sk.p) : -1; if (mk >= 0) DrawTextCenteredBold(TextFormat("%s: skiff water, the bites come twice as often", S.W->eco.marks[mk].name.c_str()), SCREEN_W / 2.0f, SCREEN_H - 140.0f, 15, Color{150, 220, 200, 255}); }
            if (!G.towed.empty()) { float kg = 0; for (const auto& t : G.towed) kg += t.kg; Txt(TextFormat("On the tow line: %d fish, %.0f kg (bleeding)", (int)G.towed.size(), kg), 20, SCREEN_H - 250, 14, Color{220, 140, 120, 255}); }
        } else if (c.deck == DECK_SHORE && sk.landing >= 0 && sk.landing < (int)G.landings.size()) {
            // ashore: what E does where you stand (the same order ShoreUse tries them in)
            const Landing& L = G.landings[sk.landing];
            Vector2 skl = Vector2Subtract(sk.p, L.at);
            if (c.carrying) DrawTextCenteredBold(TextFormat("Carrying: %s, %s%s", c.carry.name.c_str(), KgText(c.carry.kg).c_str(), c.carry.cooked ? TextFormat("  (cooked x%.2f%s)", c.carry.cook, c.carry.kg >= 2 ? ", R: eat it" : "") : ""), SCREEN_W / 2.0f, SCREEN_H - 118.0f, 15, paper);
            if (c.workOn == 100) line = TextFormat("Relighting the fire... %.0f%%", 10 * c.workT);
            else if (c.workOn >= 0) line = TextFormat("Digging... %.0f%%", 20 * c.workT);
            else if (sk.state == SkiffState::Beached && Vector2Distance(c.p, skl) < 2.8f) line = c.carrying ? "E: into the skiff   Space: climb in" : sk.load.empty() ? "Space: climb into the skiff" : "E: lift something out of the skiff   Space: climb in";
            else if (Vector2Distance(c.p, L.fire) < 1.7f) {
                if (!L.fireLit) line = "E: relight the fire (10 s)";
                else if (c.carrying && !c.carry.junk) line = "E: onto the fire (watch it: done in 10 s + 1 a kg, burnt 5 s later)";
                else if (!L.onFire.empty()) { const CatchRec& r = L.onFire.front(); line = TextFormat("E: take it off the fire (x%.2f%s)", r.cook, r.cookT > 10 + r.kg + 5 ? ", burning!" : r.cookT > 10 + r.kg ? ", done" : ""); }
            } else if (Vector2Distance(c.p, L.elder) < 2.2f) line = L.kind == LK_SEALROCK ? "E: Old Hoskins (he buys birds, at double)" : L.kind == LK_CANNERY ? "E: the foreman (cooked fish, at 150%)" : L.kind == LK_SHELF ? "E: the quartermaster (salvage, his goods)" : L.kind == LK_BONEBEACH ? "E: the hermit (bones and skulls for gear)" : L.kind == LK_STAIR ? "E: the Keeper of the Stair (offerings)" : L.kind == LK_CULT ? "E: the cult quartermaster (dark goods for shillings)" : G.foughtCanoes ? "The elder turns his back on you" : "E: trade with the elder (fish only)";
            else if (L.kind == LK_CANNERY && Vector2Length(c.p) > L.r - 1.3f && c.tangleT <= 0) line = "The pier's edge: something moves down among the pilings";
            else {
                for (const auto& k : L.caches) if (!k.open && Vector2Distance(c.p, k.p) < 1.7f && (k.kind != 2 || k.found)) line = k.kind == 1 ? (G.junkKeys > 0 ? "E: open the strongbox with a brass key" : "The strongbox is locked (a brass key from the sea opens it)") : k.kind == 2 ? "E: dig here" : "E: heave up the sea chest";
                if (line.empty()) for (const auto& cr : L.crabs) if (Vector2Distance(c.p, cr) < 1.0f) line = "E: catch the crab";
                if (line.empty()) for (const auto& b : L.onBeach) if (Vector2Distance(c.p, b.deckAt) < 1.4f && !c.carrying) line = TextFormat("E: pick up the %s", b.name.c_str());
                if (line.empty() && c.carrying) line = "E: set it down on the sand";
                if (line.empty() && Vector2Distance(c.p, L.pond) < L.pondR) line = "Wading in the lagoon: something moves in the weed";
            }
        } else if (c.overboard && sk.state == SkiffState::Capsized && Vector2Distance(c.swim, sk.p) < 3.5f) line = TextFormat("Hold left mouse to right the skiff (%.0f%%)", 100 * c.rightT / D().skiffRight);
        else if (c.overboard && sk.Up() && Vector2Distance(c.swim, sk.p) < 3.2f) line = "E: climb into the skiff";
        else if (atDavit && G.SkiffAlongside(4) && c.station < 0) line = "Space: down into the skiff   E: the davit";
        if (!line.empty()) DrawTextCenteredBold(line, SCREEN_W / 2.0f, SCREEN_H - 90.0f, 17, paper);
    }
    if (G.moored && c.station < 0 && S.panel < 0) {
        int d = NearestDock(c.p, 1.4f);
        if (d >= 0) DrawTextCenteredBold(TextFormat("E: %s", DockStations()[d].name), SCREEN_W / 2.0f, SCREEN_H - 90.0f, 20, paper);
        else if (c.p.y > -3.0f && S.W->sess.phase == Phase::Dock) DrawTextCentered("Moored at the quay: the gangplank is amidships to port; the helm casts off", SCREEN_W / 2.0f, SCREEN_H - 60.0f, 14, Fade(paper, 0.7f));
    }
    if (G.siren.on && S.panel < 0) DrawTextCentered("A Siren is singing: the wheel pulls toward her rocks. Hold it against her, or a flare or a shot drives her off", SCREEN_W / 2.0f, SCREEN_H - 118.0f, 15, Color{190, 225, 235, 255});
    if (G.mermen.on && S.panel < 0) DrawTextCentered(TextFormat("Something is at the net: a shot, a flare or the searchlight on the cod end (%.0f s)", std::max(0.0f, 12 - G.mermen.t)), SCREEN_W / 2.0f, SCREEN_H - 136.0f, 15, Color{215, 235, 220, 255});
    if (S.panel < 0) StationOverlay();
    if (S.panel < 0 && !(c.station >= 0 && Stations()[c.station].kind == StationKind::Sonar)) DrawMarkArrows();
    Panels(g);
}

// a bot's short line over its head ("Fish on, port!"), on whichever view is up
// the deck kill's screen side: the Killscore popping up over a fish just killed (design doc v2, "The kill"), and a
// landed octopus's ink over the eyes of the hand it grabbed
void DrawDeckFx() {
    const Gannet& G = S.W->G;
    const Crew& me = G.crew[S.you];
    const float PX = (float)SCREEN_W / PIXEL_W;
    for (const auto& h : G.hold) {
        if (h.killT < 0 || h.killT > 2.5f || h.gutted) continue;
        if (!S.fp && me.deck != 0) continue;
        Vector2 at;
        float rise = h.killT * 14;
        if (S.fp) { if (!DeckPointOnScreen(G, h.deckAt, 0.6f, S.cam, &at)) continue; }
        else { Vector2 cp = S.view.ToCanvas(h.deckAt); at = {(cp.x - 1) * PX, (cp.y - 1) * PX - 24}; }
        at.y -= rise;
        float a = std::clamp(2.5f - h.killT, 0.0f, 1.0f);
        bool big = h.killScore >= 2;
        Color kc = h.killScore >= 3 ? Color{255, 210, 90, 255} : big ? Color{250, 230, 170, 255} : Color{230, 220, 196, 255};
        DrawTextCenteredBold(TextFormat("x%.2f", h.killScore), at.x, at.y - 12, big ? 26 : 20, Fade(kc, a));
        DrawTextCentered(h.killHow, at.x, at.y + 12, 13, Fade(Color{230, 220, 196, 255}, a * 0.85f));
    }
    // diving: the diver's own view is the wreck in section (rooms on their 4 x 3 m grid, the helmet lamp's pool round
    // the diver, salvage glinting, locked doors and air pockets); the deck sees the gauge, the air and the depth
    if (G.dive.diver >= 0 && G.wrecks && G.dive.wreck >= 0 && G.dive.wreck < (int)G.wrecks->size()) {
        const Wreck& wk = (*G.wrecks)[G.dive.wreck];
        const Color paper{230, 220, 196, 255};
        if (S.you == G.dive.diver || S.you == G.dive.diver2) {
          if (DiveSceneActive()) DiveSceneDraw(G, S.you);   // (the side view, on the parkour movement code)
          else {   // (before the scene is built: the wreck in section)
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{4, 10, 14, 255});
            float cw = std::min(90.0f, (SCREEN_W - 160.0f) / std::max(1, wk.gw)), ch = cw * 0.75f;
            float ox = SCREEN_W / 2.0f - wk.gw * cw / 2, oy = SCREEN_H / 2.0f - wk.gh * ch / 2;
            const WreckRoom* here = G.dive.room >= 0 ? &wk.rooms[G.dive.room] : nullptr;
            Vector2 me2 = here ? Vector2{ox + (here->x + here->w * 0.5f) * cw, oy + (here->y + 0.6f) * ch} : Vector2{SCREEN_W / 2.0f, oy - 40};
            for (int i = 0; i < wk.Rooms(); i++) {
                const WreckRoom& r = wk.rooms[i];
                Rectangle rr{ox + r.x * cw + 2, oy + r.y * ch + 2, r.w * cw - 4, ch - 4};
                float lit = std::clamp(1.0f - Vector2Distance(me2, {rr.x + rr.width / 2, rr.y + rr.height / 2}) / (cw * 2.2f), 0.08f, 1.0f);
                DrawRectangleRec(rr, Fade(Color{46, 40, 32, 255}, lit));
                DrawRectangleLinesEx(rr, 2, Fade(r.locked ? Color{170, 120, 60, 255} : Color{90, 80, 64, 255}, lit));
                DrawRectangle((int)rr.x, (int)(rr.y + rr.height - 5), (int)rr.width, 5, Fade(Color{70, 66, 54, 255}, lit));   // silt on the floor
                if (r.air) DrawCircleV({rr.x + rr.width - 10, rr.y + 10}, 5, Fade(Color{170, 210, 230, 255}, 0.4f + 0.4f * lit));
                bool entry = std::find(wk.entries.begin(), wk.entries.end(), i) != wk.entries.end();
                if (entry) DrawLineEx({rr.x + rr.width / 2 - 8, rr.y - 2}, {rr.x + rr.width / 2 + 8, rr.y - 2}, 4, Color{120, 170, 200, 255});   // a breach to the sea
                int n = 0; for (const auto& s : wk.salvage) if (s.room == i && !s.taken) n++;
                for (int k = 0; k < n && lit > 0.3f; k++) DrawRectangle((int)(rr.x + 8 + k * 9), (int)(rr.y + rr.height - 12), 6, 6, Color{210, 180, 90, 255});
            }
            for (const auto& L : wk.links) {
                const WreckRoom& a = wk.rooms[L.a]; const WreckRoom& b = wk.rooms[L.b];
                Vector2 pa{ox + (a.x + a.w * 0.5f) * cw, oy + (a.y + 0.5f) * ch}, pb{ox + (b.x + b.w * 0.5f) * cw, oy + (b.y + 0.5f) * ch};
                DrawLineEx(pa, pb, L.kind == 2 ? 1.0f : 2.0f, Fade(L.kind == 1 ? Color{140, 130, 100, 255} : L.kind == 2 ? Color{200, 120, 80, 255} : Color{110, 100, 80, 255}, 0.35f));
            }
            // the residents, where the lamp shows them: a moray's head in a crack, an octopus, isopods, a pale Drowned
            for (const auto& rs : wk.residents) {
                if (rs.room < 0 || rs.room >= wk.Rooms()) continue;
                const WreckRoom& r = wk.rooms[rs.room];
                Vector2 q{ox + (r.x + r.w * 0.75f) * cw, oy + (r.y + 0.55f) * ch};
                float lit = std::clamp(1.0f - Vector2Distance(me2, q) / (cw * 1.6f), 0.0f, 1.0f) * (G.dive.lampOutT > 0 ? 0.15f : 1.0f);
                if (lit < 0.1f) continue;
                Color rc = rs.what.find("Drowned") != std::string::npos ? Color{120, 140, 128, 255} : rs.what.find("Worm") != std::string::npos ? Color{210, 210, 200, 255}
                         : rs.what == "isopods" ? Color{200, 196, 176, 255} : rs.what.find("octopus") != std::string::npos ? Color{220, 200, 200, 255} : Color{90, 110, 80, 255};
                if (rs.what == "isopods") for (int k = 0; k < 5; k++) DrawRectangle((int)(q.x - 10 + k * 5), (int)(q.y + 8 + (k % 2) * 2), 3, 2, Fade(rc, lit));
                else { DrawEllipse((int)q.x, (int)q.y, 9, 5, Fade(rc, lit)); if (rs.awake) DrawCircleV({q.x - 6, q.y - 1}, 1.5f, Fade(Color{240, 230, 160, 255}, lit)); }
            }
            if (G.dive.lampOutT <= 0) DrawCircleV(me2, cw * 0.9f, Fade(Color{255, 230, 170, 255}, 0.06f));   // the helmet lamp (unless an octopus has it)
            if (G.dive.siltT > 0) DrawCircleV(me2, cw * 0.8f, Fade(Color{70, 66, 54, 255}, 0.75f * std::min(1.0f, G.dive.siltT / 2)));   // a cloud of silt
            if (G.dive.holdT > 0) DrawTextCentered("HELD", me2.x, me2.y - 28, 14, Color{240, 140, 110, 255});
            DrawCircleV(me2, 7, Color{150, 150, 140, 255}); DrawCircleV({me2.x + 2, me2.y - 1}, 3, Color{230, 220, 160, 255});
            if (G.dive.diver2 >= 0) { DrawCircleV({me2.x - 16, me2.y}, 7, Color{140, 140, 132, 255}); DrawCircleV({me2.x - 14, me2.y - 1}, 3, Color{230, 220, 160, 255}); }   // (the bell's second diver)
            if (G.dive.bell && !wk.entries.empty()) {   // the bell itself, hanging at the first breach
                const WreckRoom& br = wk.rooms[wk.entries[0]];
                Vector2 bp{ox + (br.x + br.w * 0.5f) * cw, oy + br.y * ch - 14};
                DrawLineEx({bp.x, 0}, bp, 2, Color{90, 90, 86, 255}); DrawCircleSector(bp, 14, 180, 360, 12, Color{150, 120, 70, 255});
            }
            if (G.dive.carrying) DrawRectangle((int)me2.x + 8, (int)me2.y - 2, 8, 6, Color{210, 180, 90, 255});
          }
            if (G.dive.hoseBitten) DrawTextCentered("The hose is bitten through: no fresh air - get up", SCREEN_W / 2.0f, 110, 16, Color{240, 140, 110, 255});
            // the air and the gauge, the depth, what the keys do
            DrawRectangle(30, 30, 220, 12, Fade(BLACK, 0.6f)); DrawRectangle(30, 30, (int)(220 * G.dive.gauge), 12, G.dive.gauge >= 0.4f ? Color{90, 170, 110, 255} : Color{200, 80, 60, 255});
            Txt(G.dive.bell ? "The bell's air (full while you're in its room)" : "The pump's gauge (the deck keeps it green)", 30, 46, 13, paper);
            DrawRectangle(30, 70, 220, 12, Fade(BLACK, 0.6f)); DrawRectangle(30, 70, (int)(220 * G.dive.air / 30), 12, Color{150, 200, 230, 255});
            Txt(TextFormat("Air in the helmet: %.0f s   Depth %.0f m", G.dive.air, G.dive.depth), 30, 86, 13, paper);
            TxtBold(TextFormat("%s, %.0f m%s", WreckTypeName(wk.type), wk.depth, G.dive.room >= 0 ? TextFormat(": the %s%s", wk.rooms[G.dive.room].kind.c_str(), wk.rooms[G.dive.room].locked ? " (locked)" : "") : ": going down the line"), 30, SCREEN_H - 120.0f, 16, paper);
            for (int i = std::max(0, (int)G.log.size() - 4); i < (int)G.log.size(); i++) { float age = (float)((int)G.log.size() - i); Txt(G.log[i].c_str(), 30, SCREEN_H - 150 - age * 18, 14, Fade(paper, 0.9f - age * 0.15f)); }
            DrawTextCentered(G.dive.recall ? "Hauling you up..." : G.dive.carrying ? "A/D walk  W/S climb  Space swim up   E at a breach: into the basket   R: two tugs (haul me up)"
                                                                                  : "A/D walk  W/S climb  Space swim up   E: lift salvage   R: two tugs (haul me up)", SCREEN_W / 2.0f, SCREEN_H - 60.0f, 15, paper);
            return;
        }
        DrawTextCentered(TextFormat("Diver down %.0f m: gauge %s, %.0f s of air in the helmet%s", G.dive.depth, G.dive.gauge >= 0.4f ? "green" : "LOW - PUMP", G.dive.air, G.dive.recall ? " (hauling up)" : ""),
                         SCREEN_W / 2.0f, 130.0f, 15, G.dive.gauge >= 0.4f ? Color{170, 220, 180, 255} : Color{240, 140, 110, 255});
    }
    // the Weeds: a hand in a Kelp Wraith's grip (a countdown over them: 8 s to the rail), and you in it
    for (size_t k = 0; k < G.crew.size(); k++) {
        const Crew& c = G.crew[k];
        if (c.tangleT <= 0 || c.deck != 0) continue;
        Vector2 at;
        if (S.fp) { if ((int)k == S.you || !DeckPointOnScreen(G, c.p, 1.9f, S.cam, &at)) continue; }
        else { Vector2 cp = S.view.ToCanvas(c.p); at = {(cp.x - 1) * PX, (cp.y - 1) * PX - 30}; }
        float u = std::clamp(1 - c.tangleT / 8, 0.0f, 1.0f);
        DrawRectangle((int)at.x - 30, (int)at.y - 4, 60, 6, Fade(BLACK, 0.6f));
        DrawRectangle((int)at.x - 30, (int)at.y - 4, (int)(60 * u), 6, Color{90, 170, 110, 255});
        DrawTextCentered((int)k == S.you ? "E with a knife" : "E beside them: cut free", at.x, at.y - 18, 13, Color{200, 236, 205, 255});
    }
    if (me.tangleT > 0 && (me.deck == 0 || me.deck == DECK_SHORE)) {
        float u = std::clamp(me.tangleT / 8, 0.0f, 1.0f);
        for (int r = 0; r < 6; r++) DrawRectangleLinesEx({(float)r * 6, (float)r * 6, SCREEN_W - r * 12.0f, SCREEN_H - r * 12.0f}, 6, Fade(Color{40, 90, 50, 255}, (0.5f - r * 0.07f) * (0.5f + u)));
        DrawTextCenteredBold("Something in the kelp has you by the ankle", SCREEN_W / 2.0f, SCREEN_H * 0.3f, 24, Color{200, 236, 205, 255});
        DrawTextCentered("E with a knife in your slots, or call a hand to cut you free", SCREEN_W / 2.0f, SCREEN_H * 0.3f + 28, 16, Color{220, 230, 220, 255});
    }
    if (me.inkT > 0) {   // ink: black blots over most of the view, thinning as it clears
        float k = std::clamp(me.inkT / 3, 0.0f, 1.0f);
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{8, 6, 14, 255}, 0.75f * k));
        for (int b = 0; b < 9; b++) {
            float bx = SCREEN_W * (0.1f + 0.8f * fmodf(b * 0.618f, 1.0f)), by = SCREEN_H * (0.15f + 0.7f * fmodf(b * 0.381f + 0.2f, 1.0f));
            DrawCircleV({bx, by}, (120 + 40 * (b % 3)) * k, Fade(Color{4, 3, 8, 255}, 0.8f * k));
        }
    }
}
// A hand's name: a player's own (the arcade seat), or a bot's, picked from its slot the same on every client
static std::string HandName(int i) {
    if (S.net && i >= 0 && i < (int)(sizeof(S.net->seats) / sizeof(S.net->seats[0])) && S.net->seats[i].used && !S.net->seats[i].ai && !S.net->seats[i].name.empty()) return S.net->seats[i].name;
    static const char* N[] = {"Abel", "Silas", "Tobias", "Ezra", "Jonah", "Amos", "Gideon", "Caleb", "Rufus", "Elias", "Barnaby", "Thaddeus", "Ned", "Hiram", "Josiah", "Obadiah", "Ike", "Ansel", "Mungo", "Lem"};
    if (i < (int)S.W->G.crew.size() && i == 1 && S.W->sess.shake.on) return "Kess";
    uint32_t h = (uint32_t)S.W->G.crew[i].slot * 2246822519u + 77u; h ^= h >> 15;
    return N[h % (sizeof(N) / sizeof(N[0]))];
}
// First person: each other hand's name over its head, small and warm white, fading out beyond 15 m (the spec)
void DrawNameTags() {
    const Gannet& G = S.W->G;
    const Crew& me = G.crew[S.you];
    for (int i = 0; i < (int)G.crew.size(); i++) {
        const Crew& o = G.crew[i];
        if (i == S.you || o.dead) continue;
        if (!o.overboard && o.deck != me.deck && !(o.deck <= 1 && me.deck <= 1)) continue;
        Vector2 at;
        if (!CrewHeadOnScreen(G, i, S.cam, &at, 1.98f)) continue;
        Vector2 w = o.overboard ? o.swim : G.HandWorld(i);
        float d = Vector2Distance(w, {S.cam.position.x, S.cam.position.z});
        float a = std::clamp((18.0f - d) / 3.0f, 0.0f, 1.0f);
        if (a <= 0.01f) continue;
        bool barking = i < (int)G.brains.size() && G.brains[i].barkT > 0;
        if (barking) continue;   // (the bark is said there instead)
        std::string n = HandName(i);
        DrawTextCentered(n, at.x, at.y - 2, 14, Fade(Color{246, 236, 214, 255}, 0.85f * a));   // (the point is just over the crown)
    }
}

void DrawBarks() {
    const Gannet& G = S.W->G;
    if (!G.botsOn) return;
    const Crew& me = G.crew[S.you];
    const float PX = (float)SCREEN_W / PIXEL_W;
    for (int i = 0; i < (int)G.crew.size() && i < (int)G.brains.size(); i++) {
        const auto& b = G.brains[i];
        const Crew& o = G.crew[i];
        if (i == S.you || b.barkT <= 0 || b.bark.empty() || o.dead) continue;
        if (!o.overboard && o.deck != me.deck) continue;
        Vector2 at;
        if (S.fp) { if (!CrewHeadOnScreen(G, i, S.cam, &at)) continue; }
        else {
            Vector2 d = o.overboard ? G.boat.ToDeck(o.swim) : o.p;
            Vector2 cp = S.view.ToCanvas(d);
            at = {(cp.x - 1) * PX, (cp.y - 1) * PX - 34};
        }
        float a = std::min(1.0f, b.barkT * 2);
        int w = MeasureText(b.bark.c_str(), 14);
        DrawRectangleRounded({at.x - w / 2.0f - 6, at.y - 9, w + 12.0f, 20}, 0.4f, 6, Fade(Color{20, 16, 12, 255}, 0.7f * a));
        DrawTextCentered(b.bark, at.x, at.y - 7, 14, Fade(Color{240, 228, 196, 255}, a));
    }
}

void Draw(Game& g) {
    const Gannet& G = S.W->G;
    const Crew& c = G.crew[S.you];
    if (S.studio >= 0) { DrawTrawlStudio(S.studio, 1.0f); return; }
    if (c.deck == DECK_DIVE && !c.dead) { DrawDeckFx(); return; }   // (down on a wreck: the diver's view alone, in either version)
    if (S.fp) {
        // first person: the same Gannet through the hand's eyes, the shared HUD over it, a crosshair to aim with
        S.cam = EyeCamera(G, S.you, S.eye);
        DrawTrawl3D(G, G.eco, S.W->sess, S.you, S.cam, S.ghostSee);
        DrawLandingFx2D(G, S.cam);
        if (c.dead) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{120, 170, 200, 255}, 0.08f));
        DrawNameTags();
        DrawBarks();
        DrawDeckFx();
        Hud(g);
        if (S.panel < 0) {
            Vector2 m{SCREEN_W / 2.0f, SCREEN_H / 2.0f};
            DrawCircleV(m, 2.5f, Fade(Color{240, 230, 200, 255}, 0.8f));
            DrawRing(m, 7, 8.5f, 0, 360, 24, Fade(Color{240, 230, 200, 255}, 0.35f));
        }
        TxtShadow("V: top-down view", SCREEN_W - 170, SCREEN_H - 24, 13, Fade(Color{230, 220, 196, 255}, 0.5f));
        return;
    }
    bool inWheelhouse = c.deck == 0 && c.p.x > 0.9f && c.p.x < 5.1f && fabsf(c.p.y) < 2.1f;
    View v = MakeView(G, c.deck <= 1 ? c.deck : 0, inWheelhouse);
    {
        // out in the skiff (or swimming): the view leaves the Gannet and follows you (still in her frame, so WASD and the
        // aim keep their sense); back aboard, it slides home
        Vector2 want{0, 0};
        if (!c.dead && (c.deck >= DECK_SKIFF || c.overboard)) { Vector2 hd = G.boat.ToDeck(G.HandWorld(S.you)); if (Vector2Length(Vector2Subtract(hd, {-1.5f, 0.5f})) > 8) want = Vector2Subtract(hd, {-5.0f, 0}); }
        S.camOff = Vector2Distance(S.camOff, want) > 30 ? want : Vector2Lerp(S.camOff, want, std::min(1.0f, GetFrameTime() * 3));
        v.center = Vector2Subtract(v.center, Vector2Scale(S.camOff, v.ppm));
        if (G.skiff.Up()) v.lights.push_back({G.boat.ToDeck(G.skiff.ToWorld({1.95f, 0})), D().skiffLantern, 0.8f});   // the skiff's bow lantern
        for (const auto& L : G.landings) if (L.fireLit) v.lights.push_back({G.boat.ToDeck(L.ToWorld(L.fire)), 9.0f, 0.9f + 0.1f * sinf(G.time * 9)});   // a landing's fire
    }
    if (c.dead) { v.ghost = true; v.ghostAt = c.p; v.ghostSee = S.ghostSee; v.lights.push_back({c.p, 3.0f, 0.4f}); }   // the ghost's own cold lantern
    S.view = v;
    BeginLayer(PixelRT());
    ClearBackground(Color{2, 4, 8, 255});
    DrawSea(G, v);
    DrawQuay(G, v);
    DrawLanding(G, v);
    // the harbour line: a ring of buoys round the harbour mouth, green lamps seaward, red toward the island
    for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        Vector2 w = Vector2Add(S.W->sess.harbour, {cosf(a) * S.W->sess.harbourR, sinf(a) * S.W->sess.harbourR});
        if (S.W->eco.g && S.W->eco.DepthAt(w) < 1) continue;
        Vector2 d = G.boat.ToDeck(w), cp = v.ToCanvas(d);
        if (cp.x < -4 || cp.y < -4 || cp.x > PIXEL_W + 6 || cp.y > PIXEL_H + 6) continue;
        bool blink = fmodf(G.time + k * 0.37f, 2.0f) < 1.2f;
        Color lc = cosf(a) > 0 ? Color{90, 230, 120, 255} : Color{240, 80, 70, 255};
        DrawRectangle((int)cp.x - 1, (int)cp.y - 1, 3, 3, Color{40, 40, 44, 255});
        if (blink) { DrawRectangle((int)cp.x, (int)cp.y - 1, 1, 1, lc); DrawCircleV(cp, 3, Fade(lc, 0.15f)); }
    }
    DrawLife(G, v, false);
    DrawBoat(G, v);
    DrawGear(G, v);
    DrawLines(G, v);
    DrawSkiff(G, v);
    for (int i = 0; i < (int)G.crew.size(); i++) {
        Crew cc = G.crew[i];
        if (cc.deck == DECK_SKIFF && !cc.overboard) {   // (a hand in the skiff: drawn where she is, in the Gannet's frame)
            cc.p = G.boat.ToDeck(G.skiff.ToWorld(cc.p));
            Vector2 aft{-G.skiff.Forward().x, -G.skiff.Forward().y}; Vector2 bf = G.boat.Forward();
            cc.facing = {aft.x * bf.x + aft.y * bf.y, -aft.x * bf.y + aft.y * bf.x};   // (a rower faces aft)
            cc.deck = 0; cc.station = 0;
        }
        if (cc.deck == DECK_SHORE && !cc.overboard) {   // (ashore: the landing's frame onto the Gannet's; facing is already hers)
            cc.p = G.boat.ToDeck(G.HandWorld(i)); cc.deck = 0;
            if (cc.carrying) cc.station = 0;   // (arms full: no tool drawn)
        }
        DrawCrewMember(cc, v, G.time, i == S.you);
        if (G.crew[i].carrying && !cc.overboard) {   // what's in the arms
            Vector2 q = v.ToCanvas(Vector2Add(cc.p, Vector2Scale(cc.facing, 0.35f)));
            DrawRectangle((int)q.x - 2, (int)q.y - 1, G.crew[i].carry.junk ? 5 : 6, G.crew[i].carry.junk ? 4 : 2, G.crew[i].carry.junk ? Color{120, 80, 40, 255} : G.crew[i].carry.cooked ? Color{200, 140, 70, 255} : Color{200, 205, 210, 255});
        }
    }
    DrawLife(G, v, true);
    EndLayer();
    const float PX = (float)SCREEN_W / PIXEL_W;
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)}, {-PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE);
    DrawBarks();
    DrawDeckFx();
    Hud(g);
}
} // namespace

void StartTrawl(Game& g, bool firstPerson, int crew, int botSkill) {
    S = TrawlScene{};
    S.W = &S.own;
    uint32_t seed = (uint32_t)GetRandomValue(1, 1 << 30);
    // the Gannet at the quay on the atoll, the first deadline's quota on the tape; every hand after you is a bot
    crew = std::clamp(crew, 1, 6);
    S.W->sess.Begin(S.W->G, S.W->eco, crew, seed);
    S.W->G.botsOn = crew > 1;
    S.W->G.botSkill = (Skill)std::clamp(botSkill, 0, (int)Skill::COUNT - 1);
    S.active = true;
    S.fp = firstPerson;
    EnableCursor();
    g.scene = Scene::Trawl;
}

// The shakedown night (design doc, "First night"): solo, Kess aboard, no quota; back to the arcade when it's done
void StartTrawlShakedown(Game& g, bool firstPerson) {
    S = TrawlScene{};
    S.W = &S.own;
    S.W->sess.BeginShakedown(S.W->G, S.W->eco);
    S.active = true;
    S.fp = firstPerson;
    EnableCursor();
    g.scene = Scene::Trawl;
}

// Network play (stage 5): the arcade's session launched the Trawl. The host draws its real world; a guest draws its
// mirror of the host's snapshots. Either way the hand is played by sending input.
void StartTrawlNet(Game& g, arcade::Session* net, bool firstPerson) {
    S = TrawlScene{};
    S.W = &S.own;
    S.net = net;
    S.you = std::max(0, net->MyPlayer());
    if (net->role == arcade::R_HOST) { TrawlWorld* w = TrawlHostWorld(net->HostGame()); if (w) S.W = w; }
    S.active = true;
    S.fp = firstPerson;
    EnableCursor();
    g.scene = Scene::Trawl;
}

// Leaving: solo, back to the reels; the host takes everyone back to the lobby; a guest gives its hand to a bot
void LeaveTrawlMatch(Game& g) {
    if (S.net) {
        if (S.net->role == arcade::R_HOST) S.net->BackToLobby();
        else S.net->Leave();
    }
    S.active = false; S.net = nullptr;
    EnableCursor();
    g.scene = Scene::Arcade;
}

// While the game menu is open the world doesn't stop for a crew (only solo pauses): keep the session talking
void TrawlMenuTick(float dt) {
    if (!S.active || !S.net) return;
    Writer w; WriteInputAction(HandInput{}, w); S.net->Act(w);
    S.net->Update(GetTime(), dt);
}

namespace {
TrawlScene::Pose CapturePose(const Gannet& G) {
    TrawlScene::Pose p;
    p.pos = G.boat.pos; p.heading = G.boat.heading; p.roll = G.boat.roll; p.pitch = G.boat.pitch; p.heave = G.boat.heave;
    for (const auto& c : G.crew) p.crew.push_back(c.p);
    return p;
}
// (a guest) draw between the last two snapshots: the boat's pose and the hands' places, one snapshot behind
void Interpolate(Gannet& G) {
    const auto& a = S.prevPose; const auto& b = S.curPose;
    if (a.crew.size() != b.crew.size() || b.crew.size() != G.crew.size()) return;
    float t = std::clamp(S.sinceSnap / std::max(0.02f, S.snapGap), 0.0f, 1.0f);
    float dh = b.heading - a.heading;
    while (dh > PI) dh -= 2 * PI;
    while (dh < -PI) dh += 2 * PI;
    if (Vector2Distance(a.pos, b.pos) > 20) t = 1;   // (a jump, not a drift: no sliding across the chart)
    G.boat.pos = Vector2Lerp(a.pos, b.pos, t); G.boat.heading = a.heading + dh * t;
    G.boat.roll = Lerp(a.roll, b.roll, t); G.boat.pitch = Lerp(a.pitch, b.pitch, t); G.boat.heave = Lerp(a.heave, b.heave, t);
    for (size_t i = 0; i < G.crew.size(); i++) if (Vector2Distance(a.crew[i], b.crew[i]) < 3) G.crew[i].p = Vector2Lerp(a.crew[i], b.crew[i], t);
}
} // namespace

// ---------------------------------------------------------------- sound (sound_trawl.inl; design doc "Sound design")
// The music and the bed follow the session's state every frame; the effects come from what changed since the last
// frame (the world is the same for solo play, the host and a guest's mirror, so every screen hears the same things).
void TrawlAudioFrame(const TrawlWorld& W, int you, float dt) {
    const Gannet& G = W.G; const Session& ss = W.sess; const Eco& E = W.eco;
    static struct {
        bool init = false;
        RodState rod[4]; float reelT[4], humT[4], tick[4], jumpT[4];
        int telegraph; float valveT; size_t holdN; size_t shots; NetState net; float winchT, warpT;
        bool over[8], dead[8]; int ring; size_t tapeN; float sold, delivered; float integ[SEC_COUNT]; bool aground;
        CanoeState canoe; std::string lastLog; size_t arrivals; float stroke[8]; int deadline;
    } A;
    if (!A.init || A.deadline != ss.deadline) {
        A = {}; A.init = true; A.deadline = ss.deadline;
        for (int k = 0; k < 4; k++) A.rod[k] = RodState::Idle;
        A.telegraph = G.boat.telegraph; A.valveT = G.boat.valveT; A.holdN = G.hold.size(); A.shots = G.shots.size(); A.net = G.net.state;
        A.tapeN = ss.tape.size(); A.sold = ss.lastSaleTotal; A.delivered = ss.lastDeliveryTotal; for (int s = 0; s < SEC_COUNT; s++) A.integ[s] = G.boat.integrity[s];
        A.canoe = ss.canoe; A.lastLog = G.log.empty() ? "" : G.log.back(); A.arrivals = E.arrivals.size();
        for (int k = 0; k < 8 && k < (int)G.crew.size(); k++) { A.over[k] = G.crew[k].overboard; A.dead[k] = G.crew[k].dead; }
    }
    // ---- the state
    TwAudio a; a.on = true; a.ground = 0; a.verse = std::max(0, ss.deadline - 1); a.moored = G.moored;
    a.mode = ss.phase == Phase::Dock ? 0 : ss.phase == Phase::SailOut ? 1 : ss.phase == Phase::Night ? 2 : ss.phase == Phase::Result ? 3 : 4;
    a.clock = std::clamp(ss.clock / 540.0f, 0.0f, 1.0f);
    if (ss.phase == Phase::Night && ss.clock > 450 && Vector2Distance(G.boat.pos, ss.harbour) < 220) { a.mode = 1; a.homeward = 1; }
    a.telegraph = G.boat.telegraph; a.roll = fabsf(G.boat.RollDeg()); a.bilge = std::clamp(G.boat.bilge / 900.0f, 0.0f, 1.0f); a.weather = (int)ss.weather;
    a.canoe = ss.canoe == CanoeState::Coming && ss.clock >= ss.canoeAt - 20 ? 1 : ss.canoe == CanoeState::Alongside ? 2 : 0;
    if (G.siren.on) {
        Vector2 l = G.boat.ToDeck(G.siren.p); float d = std::max(1.0f, Vector2Length(l));
        a.siren = std::clamp(1.4f - d / 90, 0.35f, 1.0f); a.sirenPan = std::clamp(l.y / d, -0.9f, 0.9f);
    }
    a.mermen = G.mermen.on;
    if (ss.variant == Variant::MermenMarket && a.canoe > 0) { a.siren = std::max(a.siren, a.canoe == 2 ? 0.7f : 0.4f); a.canoe = 0; }   // (the market comes singing, not drumming)
    for (const auto& c : G.crew) if (c.tangleT > 0) a.tangled = true;
    for (const auto& r : G.rods) if (r.state == RodState::Fighting && r.fight.spec.kg >= 20) { a.fishOn = true; a.tension = std::max(a.tension, std::clamp(r.fight.tension / std::max(1.0f, TackleOf(r.tackle).strength), 0.0f, 1.0f)); }
    if (G.harpoon.state == RodState::Fighting) { a.fishOn = true; a.tension = std::max(a.tension, 0.7f); }
    if (G.eco) {
        const auto& SP = Species().sp;
        for (const auto& ag : E.agents) {
            if (!ag.alive) continue;
            const SpeciesRec& r = SP[ag.sp];
            if (!r.threat) continue;
            float d = Vector2Distance({ag.p.x, ag.p.y}, G.boat.pos);
            if (r.size >= 5 && r.band != BAND_AIR && d < 60 && ag.hunger > 0.3f) a.threat = std::max(a.threat, 1 - d / 60);
            if (r.band == BAND_AIR && d < 30) a.gulls = 1;
            if (r.name == "barracuda" && d < 14 && ag.hunger > 0.3f) a.barracuda = 1;
        }
    }
    AudioTrawl(a);
    // ---- what changed: the gear
    for (int k = 0; k < 4 && k < (int)G.rods.size(); k++) {
        const Rod& r = G.rods[k];
        float pan = r.TipDeck().y < 0 ? -0.5f : 0.5f;
        if (r.state == RodState::Fighting && A.rod[k] != RodState::Fighting) TrawlCue(TWC_STRIKE, 0.9f, pan);
        if (r.state != RodState::Fighting && A.rod[k] == RodState::Fighting) {
            if (G.hold.size() > A.holdN) { TrawlCue(TWC_GAFF, 1, pan); TrawlCue(TWC_FLOP, 0.9f, pan); }
            else if (!G.log.empty() && G.log.back().find("Snapped") != std::string::npos) TrawlCue(TWC_SNAP, 1, pan);
            else TrawlCue(TWC_SPLASH, 0.6f, pan);
        }
        if (r.state == RodState::Fighting) {
            float ten = std::clamp(r.fight.tension / std::max(1.0f, TackleOf(r.tackle).strength), 0.0f, 1.2f);
            if (r.fight.reeling) { A.reelT[k] += dt; float per = 0.14f - 0.08f * std::min(1.0f, ten); if (A.reelT[k] >= per) { A.reelT[k] = 0; TrawlCue(TWC_REEL, 0.5f + 0.5f * ten, pan, 0.8f + 0.7f * ten); } }
            if (ten > 0.78f) { A.humT[k] += dt; if (A.humT[k] >= 0.5f) { A.humT[k] = 0; TrawlCue(TWC_HUM, std::min(1.0f, (ten - 0.7f) * 3), pan, 0.9f + ten * 0.4f); } }
            if (ten > 0.5f && GetRandomValue(0, 100) < 2) TrawlCue(TWC_CREAK, 0.4f + 0.5f * ten, pan, 0.8f + 0.5f * ten);
            if (r.fight.jumpT > 0 && A.jumpT[k] <= 0) TrawlCue(TWC_SPLASH, 0.8f, pan);
            A.jumpT[k] = r.fight.jumpT;
        }
        float tk = r.bite.Tick();
        if (tk > 0.5f && A.tick[k] <= 0.5f) TrawlCue(TWC_BITE, std::min(1.0f, tk), pan, r.bite.stage == BiteStage::Take ? 0.7f : 1.2f);
        A.tick[k] = tk;
        A.rod[k] = r.state;
    }
    A.holdN = G.hold.size();
    // ---- the boat
    if (G.boat.telegraph != A.telegraph) { TrawlCue(TWC_TELEGRAPH, 0.8f, 0.2f); A.telegraph = G.boat.telegraph; }
    if (G.boat.valveT > 0 && A.valveT <= 0) TrawlCue(TWC_VALVE, 0.9f, -0.3f);
    A.valveT = G.boat.valveT;
    for (int s = 0; s < SEC_COUNT; s++) {
        if (G.boat.integrity[s] < A.integ[s] - 0.5f) {
            bool ram = !G.log.empty() && G.log.back().find("struck the hull") != std::string::npos;
            TrawlCue(ram ? TWC_BUMP : TWC_HULL, 1, (s % 2 ? 0.5f : -0.5f));
        }
        A.integ[s] = G.boat.integrity[s];
    }
    if (G.boat.aground && !A.aground) TrawlCue(TWC_HULL, 0.8f, 0);
    A.aground = G.boat.aground;
    for (int k = 0; k < 8 && k < (int)G.crew.size(); k++) {
        const Crew& c = G.crew[k];
        if (c.station >= 0 && Stations()[c.station].kind == StationKind::Pumps && c.strokeT < A.stroke[k] - 0.2f) TrawlCue(TWC_PUMP, 0.6f, -0.2f);
        A.stroke[k] = c.strokeT;
        if (c.overboard && !A.over[k]) TrawlCue(TWC_OVERBOARD, 1, c.p.y < 0 ? -0.6f : 0.6f);
        A.over[k] = c.overboard;
        if (c.dead && !A.dead[k]) TrawlCue(TWC_DEATH, 1, 0);
        A.dead[k] = c.dead;
    }
    int ringOut = 0; for (const auto& r : G.rings) if (r.state == 1) ringOut++;
    if (ringOut > A.ring) TrawlCue(TWC_RING, 0.8f, 0.3f);
    A.ring = ringOut;
    // the net
    if (G.net.state == NetState::Shooting || G.net.state == NetState::Hauling) { A.winchT += dt; if (A.winchT >= 0.3f) { A.winchT = 0; TrawlCue(TWC_WINCH, 0.7f, -0.4f, G.net.state == NetState::Hauling ? 0.8f + std::min(1.0f, G.net.load / 300) * 0.4f : 1.1f); } }
    if (G.net.state == NetState::Down && G.net.load > 80) { A.warpT += dt; if (A.warpT > 3 && GetRandomValue(0, 100) < 3) { A.warpT = 0; TrawlCue(TWC_WARP, std::min(1.0f, G.net.load / 250), -0.4f); } }
    if (G.net.state == NetState::Snagged && A.net != NetState::Snagged) TrawlCue(TWC_SNAG, 1, -0.4f);
    if (A.net == NetState::Hauling && G.net.state == NetState::Stowed) TrawlCue(TWC_CODEND, 1, -0.3f);
    A.net = G.net.state;
    // guns and tacticals
    if (G.shots.size() > A.shots) {
        const tw::Projectile& p = G.shots.back();
        float pan = std::clamp(G.boat.ToDeck({p.p.x, p.p.y}).y / 4, -1.0f, 1.0f);
        switch (p.kind) {
            case Shot::Bullet: TrawlCue(TWC_RIFLE, 1, pan); break;
            case Shot::Pellet: TrawlCue(TWC_SHOTGUN, 1, pan); break;
            case Shot::Spear: TrawlCue(TWC_SPEAR, 0.9f, pan); break;
            case Shot::Harpoon: case Shot::Explosive: TrawlCue(TWC_HARPOON, 1, pan); break;
            case Shot::Flare: TrawlCue(TWC_FLARE, 0.8f, pan); break;
            case Shot::Charge: TrawlCue(TWC_SPLASH, 0.6f, pan); break;
        }
    }
    A.shots = G.shots.size();
    // the log's own events: a blast, a canoe, the threats' tells
    std::string last = G.log.empty() ? "" : G.log.back();
    if (last != A.lastLog) {
        if (last.find("DEPTH CHARGE") != std::string::npos || last.find("blast") != std::string::npos) TrawlCue(TWC_CHARGE, 1, 0);
        if (last.find("war canoe comes alongside") != std::string::npos) TrawlCue(TWC_CANOE, 1, 0.5f);
        if (last.find("Man overboard") == std::string::npos && last.find("takes the") != std::string::npos) TrawlCue(TWC_GULL, 0.7f, 0.2f);
        A.lastLog = last;
    }
    while (A.arrivals < E.arrivals.size()) {
        const std::string& sp = E.arrivals[A.arrivals].species;
        if (sp.find("gull") != std::string::npos) TrawlCue(TWC_GULL, 0.8f, 0.3f);
        else if (sp.find("barracuda") != std::string::npos) TrawlCue(TWC_TICKS, 0.8f, -0.2f);
        A.arrivals++;
    }
    // the dock and the Owners
    if (ss.tape.size() > A.tapeN) TrawlCue(TWC_TAPE, 0.6f, 0.4f);
    A.tapeN = ss.tape.size();
    if (ss.lastSaleTotal != A.sold && ss.lastSaleTotal > 0) TrawlCue(TWC_SELL, 0.8f, 0);
    A.sold = ss.lastSaleTotal;
    if (ss.lastDeliveryTotal != A.delivered && ss.lastDeliveryTotal > 0) TrawlCue(TWC_SELL, 0.8f, 0);   // (the Owners' scales: the same weigh-in)
    A.delivered = ss.lastDeliveryTotal;
    A.canoe = ss.canoe;
}

void SceneTrawl(Game& g) {
    if (!S.active) StartTrawl(g, false);
    SetPost(0.25f, 0.02f, 0.1f);
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 0.1f);
    if (S.net) {
        // ---- network play: the session first (the host's world steps inside it), then this hand's input
        arcade::Session& N = *S.net;
        N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) {   // the host went back to the lobby, or the table closed
            S.active = false; S.net = nullptr; EnableCursor(); g.scene = Scene::Arcade;
            return;
        }
        if (N.role == arcade::R_HOST) { TrawlWorld* w = TrawlHostWorld(N.HostGame()); if (w) S.W = w; }
        else {
            S.sinceSnap += dt;
            if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
                S.seenVersion = N.stateVersion;
                Reader r(N.Snapshot());
                if (ReadWorld(r, S.own)) {
                    S.prevPose = S.curPose.crew.empty() ? CapturePose(S.own.G) : S.curPose;
                    S.curPose = CapturePose(S.own.G);
                    S.snapGap = std::clamp(S.sinceSnap, 0.03f, 0.2f);
                    S.sinceSnap = 0;
                }
            }
            Interpolate(S.own.G);
        }
        S.you = std::max(0, N.MyPlayer());
        if (S.W->G.crew.empty() || S.you >= (int)S.W->G.crew.size()) {
            ClearBackground(Color{2, 4, 8, 255});
            DrawTextCenteredBold("Coming aboard...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, Color{230, 220, 196, 255});
            return;
        }
    }
    if (!S.shot) {
        // first person takes the mouse to look with, except where a panel or the locker needs a pointer
        const Crew& me = S.W->G.crew[S.you];
        bool pointer = S.panel >= 0 || (me.station >= 0 && (Stations()[me.station].kind == StationKind::Locker || Stations()[me.station].kind == StationKind::Sonar));
        bool lock = S.fp && !pointer;
        Vector2 md = MouseLook(lock);
        if (lock) {
            S.eye.yaw += md.x * 0.0025f;                      // right turns toward starboard when you face the bow
            S.eye.pitch = std::clamp(S.eye.pitch - md.y * 0.0025f, -1.35f, 1.25f);
            if (S.eye.yaw > PI) S.eye.yaw -= 2 * PI;
            if (S.eye.yaw < -PI) S.eye.yaw += 2 * PI;
        }
        {   // your Wardrobe's skin and costume, told to the boat (every hand sees them) whenever they differ, at most twice a second
            static float wardT = 0; wardT -= dt;
            const skins::Wardrobe& wd = skins::Get(skins::TRAWL);
            std::string want = wd.worn + "|" + wd.costume;
            if (want != me.skin + "|" + me.costume && wardT <= 0) { Command(CMD_WARDROBE, want); wardT = 0.5f; }
        }
        HandInput in = Gather();
        if (S.net) { Writer w; WriteInputAction(in, w); S.net->Act(w); }
        else MergeInput(S.pend, in);
        if (S.net && S.net->role != arcade::R_HOST && (in.btn & HI_LMB)) {
            // a guest's own rod: the reel answers at once on its mirror (the host's next snapshot corrects it)
            Gannet& G = S.W->G;
            int ri = me.station >= 0 ? G.RodAt(me.station) : -1;
            if (ri >= 0 && ri < (int)G.rods.size() && G.rods[ri].state == RodState::Fighting) G.rods[ri].fight.PredictReel(dt);
        }
    }
    if (!S.net) {
        // solo: our own Gannet, stepped here at 60 Hz through the same input path as network play
        S.acc += dt;
        while (S.acc >= 1 / 60.0f) {
            S.acc -= 1 / 60.0f;
            if (!S.shot) { ApplyInput(*S.W, S.you, S.pend, 1 / 60.0f); ClearPresses(S.pend); }
            S.W->G.Step(1 / 60.0f);
            S.W->sess.Step(1 / 60.0f);
        }
    }
    if (!S.shot) TrawlAudioFrame(*S.W, S.you, dt);
    if (S.toastT > 0) S.toastT -= dt;
    if (S.ghostSee > 0) S.ghostSee -= dt;
    if (S.tapeT > 0) S.tapeT -= dt;
    if (S.W->sess.phase == Phase::Over || S.W->sess.phase == Phase::Result) S.panel = PANEL_END;
    Draw(g);
    if (S.net) {
        // who's aboard over the wire: a lost hand pauses nothing at sea, but everyone should know
        const arcade::Session& N = *S.net;
        if (N.paused) DrawTextCenteredBold(TextFormat("A hand has lost the line to the boat: a bot takes over in %.0f s", N.pauseLeft), SCREEN_W / 2.0f, 96, 18, Color{240, 180, 120, 255});
        TxtShadow(TextFormat("%s  -  %s", N.role == arcade::R_HOST ? "Hosting" : "Aboard", N.code.c_str()), 20, SCREEN_H - 24, 13, Fade(Color{230, 220, 196, 255}, 0.5f));
    }
}

// --shots: 0 the deck at night, 1 the engine room, 2 the wheelhouse, 3 a squall, 4 a fish on, 5 a marlin jumping,
// 6 the Lagoon under a full lantern, 7 the searchlight over the reef, 8 a reef shark come to the chum (and the gulls)
void DebugTrawlShot(Game& g, int which) {
    bool fp = which >= 100;                    // 100+: the same set-ups in first person
    if (fp) which -= 100;
    StartTrawl(g, fp);
    S.shot = true;
    if (fp) {   // where the hand looks in each first-person shot
        S.eye.pitch = which == 15 ? -0.3f : which == 9 || which == 0 ? -0.08f : -0.22f;
        S.eye.yaw = which == 4 || which == 5 ? 1.25f : which == 15 || which == 21 || which == 22 ? 3.1f : which == 9 ? -1.9f : which == 16 ? 1.3f : which == 17 ? 1.9f : which == 1 ? -2.4f : which == 6 || which == 8 ? 2.6f : 0.0f;
    }
    if (which >= 45 && which <= 56) { S.studio = which - 45; return; }   // the visual overhaul's turnaround stage
    if (which == 58) { S.studio = 12; return; }                          // (the skins gallery)
    if (which == 59) { S.studio = 13; return; }                          // (the costumes gallery)
    if (which == 40 || which == 41 || which == 43 || which == 44) {
        // the visual overhaul's harness (spec, "Process and acceptance"): 40 the helm at night in the Lagoon's fog;
        // 41 a hand at the gutting table in rain; 43 aiming the lever carbine at a fish on the surface in rain
        Gannet& G = S.W->G;
        G.Init(4, 11, which == 40 ? Weather::Fog : Weather::Rain);
        S.W->eco.Init("lagoon", 11); G.eco = &S.W->eco;
        G.moored = false;
        G.boat.pos = {S.W->eco.n * S.W->eco.cell * 0.42f, S.W->eco.n * S.W->eco.cell * 0.5f}; G.boat.heading = -0.3f;
        G.boat.telegraph = which == 40 ? 1 : 0; G.boat.pressure = 0.7f; G.boat.lantern = 2;
        Crew& c = G.crew[0];
        G.crew[2].p = {-8.2f, 0.4f}; G.crew[2].facing = {-1, 0};
        G.crew[3].p = {-2.2f, -1.6f}; G.crew[3].facing = {0, -1};
        int gut = -1, helm = -1;
        for (int i = 0; i < (int)Stations().size(); i++) { if (Stations()[i].kind == StationKind::Gutting) gut = i; if (Stations()[i].kind == StationKind::Helm) helm = i; }
        if (which == 40) { c.p = Stations()[helm].at; c.station = helm; G.boat.rudder = 0.2f; G.crew[1].p = {-1.5f, 1.2f}; }
        if (which == 41) {
            G.crew[1].p = Stations()[gut].at; G.crew[1].station = gut; G.crew[1].facing = {0, 1};
            c.p = Vector2Add(G.crew[1].p, {2.4f, -0.4f}); c.station = -1;
            Vector2 d = Vector2Subtract(G.crew[1].p, c.p); S.eye.yaw = atan2f(d.y, d.x); S.eye.pitch = -0.18f;
            CatchRec f; f.name = "snapper"; f.kg = 3; f.price = 3; f.dead = true; f.deckAt = Vector2Add(G.crew[1].p, {0.3f, 0.6f}); G.hold.push_back(f);
        }
        if (which == 43 || which == 44) {
            c.p = {-1.0f, 1.9f}; c.station = -1;
            c.slots[0] = {}; c.slots[0].it = Item::Rifle; c.slots[0].ammo = 8; c.sel = 0;
            if (which == 44) { c.slots[0].it = Item::Weapon; c.slots[0].wpn = WeaponIndex(getenv("DEPTH_GUN") ? getenv("DEPTH_GUN") : "revolver"); c.slots[0].ammo = 6; if (getenv("DEPTH_FIRE")) c.cool = 0.95f * WeaponCooldown(Weapons()[c.slots[0].wpn], c.slots[0].att); if (getenv("DEPTH_RELOAD")) c.reloadT = 0.9f; }
            Floater fl; fl.name = "yellowfin tuna"; fl.kg = 18; fl.p = G.boat.ToWorld({2.0f, 11.0f}); G.floaters.push_back(fl);
            S.eye.yaw = atan2f(11.0f - 1.9f, 2.0f - -1.0f); S.eye.pitch = -0.2f;
        }
        for (int i = 0; i < 60 * 3; i++) { G.Step(1 / 60.0f); }
        if (which == 41 || which == 43 || which == 44) G.sea.weather = Weather::Rain;
        return;
    }
    if (which == 42) {
        // the spec's performance case: six hands on deck in rain (the fog thickened to the Lagoon's), seen from the
        // stern gantry looking forward over all of them, a catch on the planks, the lamps lit
        Gannet& G = S.W->G;
        G.Init(6, 11, Weather::Rain);
        S.W->eco.Init("lagoon", 11); G.eco = &S.W->eco;
        G.moored = false;
        G.boat.pos = {S.W->eco.n * S.W->eco.cell * 0.42f, S.W->eco.n * S.W->eco.cell * 0.5f}; G.boat.heading = -0.3f;
        G.boat.telegraph = 1; G.boat.pressure = 0.7f; G.boat.lantern = 2;
        const Vector2 AT[6] = {{-9.6f, 0.6f}, {-6.0f, 1.6f}, {-2.2f, 1.5f}, {-0.8f, -2.2f}, {2.0f, 2.6f}, {7.0f, -0.8f}};
        for (int i = 0; i < 6; i++) { G.crew[i].p = AT[i]; G.crew[i].station = -1; G.crew[i].facing = {i % 2 ? 1.0f : -1.0f, 0}; }
        for (int k = 0; k < 5; k++) { CatchRec f; f.name = k % 2 ? "bonito" : "snapper"; f.kg = 2 + k; f.price = 3; f.dead = k > 1; f.deckAt = {-3.5f + k * 0.4f, 1.2f - k * 0.3f}; f.heading = k * 1.3f; G.hold.push_back(f); }
        for (int i = 0; i < 60 * 3; i++) G.Step(1 / 60.0f);
        for (int i = 0; i < 6; i++) G.crew[i].p = AT[i];
        G.sea.weather = Weather::Rain;
        S.eye.yaw = 0.0f; S.eye.pitch = -0.25f;
        return;
    }
    if (which == 36) {
        // spray: full ahead into a squall, the hand on the foredeck looking over the bow as she buries it
        Gannet& G = S.W->G;
        G.Init(4, 11, Weather::Squall);
        G.moored = false; G.boat.pos = Vector2Add(G.moorPos, {200, 60}); G.boat.heading = 0.4f;
        G.boat.telegraph = 3; G.boat.pressure = 0.8f; G.boat.firebox = 6;
        for (int i = 0; i < 60 * 25; i++) G.Step(1 / 60.0f);
        for (int i = 0; i < 60 * 30; i++) {   // (on until she buries her bow in a sea)
            Vector3 bow = BoatPoint(G.boat, {10.0f, 0.55f, 0});
            if (G.sea.Height(bow.x, bow.z) + 0.12f - bow.y > 0.25f) break;
            G.Step(1 / 60.0f);
        }
        Crew& c = G.crew[0]; c.p = {8.3f, 1.3f}; c.station = -1;
        if (fp) { S.eye.yaw = 0.6f; S.eye.pitch = -0.7f; }
        tw::gSprayWarm = 120;
        return;
    }
    if (which == 31) {
        // the Weeds: the Gannet lying at the edge of the kelp canopy at night, her lantern full
        StartTrawl(g, fp, 2, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess; Eco& e = S.W->eco;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-2.0f, 1.0f};
        ss.SetGround("weeds");
        ss.Buy("shrimp"); while (G.boat.bunker < 60 && ss.Buy("coal")) {}
        ss.CastOff();
        Vector2 at = Vector2Add(ss.harbour, {160, 0});
        for (int y = 0; y < e.n && e.HabAt(at) != H_KELP; y++) for (int x = e.n / 3; x < e.n * 2 / 3; x++) { Vector2 p{(x + 0.5f) * e.cell, (y + 0.5f) * e.cell}; if (e.HabAt(p) == H_KELP && e.HabAt(Vector2Add(p, {-14, 0})) != H_KELP) { at = Vector2Add(p, {-10, 0}); break; } }
        G.boat.pos = at; G.boat.heading = 0.3f; G.boat.telegraph = 0; G.boat.lantern = 2;
        for (int i = 0; i < 60 * 4; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        if (fp) { S.eye.yaw = 0.2f; S.eye.pitch = -0.2f; }
        return;
    }
    if (which == 32) {
        // the Weeds' threats at once: a Siren singing on a rock off the starboard bow, a hand caught by a Kelp Wraith at
        // the port rail, mermen splashing at the cod end of the net
        StartTrawl(g, fp, 3, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess; Eco& e = S.W->eco;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-4.0f, -2.5f}; G.crew[2].p = {-1.0f, 1.0f};
        ss.SetGround("weeds");
        ss.Buy("shrimp"); while (G.boat.bunker < 60 && ss.Buy("coal")) {}
        ss.CastOff();
        Vector2 at = Vector2Add(ss.harbour, {160, 0});
        for (int y = 0; y < e.n && e.HabAt(at) != H_KELP; y++) for (int x = e.n / 3; x < e.n * 2 / 3; x++) { Vector2 p{(x + 0.5f) * e.cell, (y + 0.5f) * e.cell}; if (e.HabAt(p) == H_KELP && e.HabAt(Vector2Add(p, {-14, 0})) != H_KELP) { at = Vector2Add(p, {-10, 0}); break; } }
        G.boat.pos = at; G.boat.heading = 0.3f; G.boat.telegraph = 1; G.boat.lantern = 2;
        G.net.state = NetState::Down;
        for (int i = 0; i < 60 * 4; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        G.siren.on = true; G.siren.p = G.boat.ToWorld(fp ? Vector2{-16, 12} : Vector2{-5, -13.5f}); G.siren.t = 6;
        G.mermen.on = true; G.mermen.p = { G.net.node[5 * 8 + 4].x, G.net.node[5 * 8 + 4].y }; G.mermen.t = 4;
        G.botsOn = false; for (auto& c : G.crew) c.bot = false;   // (held still for the picture: nobody cuts the hand free yet)
        G.crew[1].station = -1; G.crew[1].deck = 0; G.crew[1].p = {-4.0f, -2.5f}; G.crew[1].tangleT = 3;
        if (fp) { G.crew[0].station = -1; G.crew[0].p = {-6.5f, 0.6f}; G.crew[0].facing = {-1, 0}; S.eye.yaw = PI - 0.25f; S.eye.pitch = -0.12f; }
        return;
    }
    if (which == 33) {
        // the Grotto: lying in the cave by a wreck, the mould glowing on the walls; an Angler's light off the rail, the
        // Ghost Worm circling, isopods over the bow and a Drowned sailor on the deck
        StartTrawl(g, fp, 3, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess; Eco& e = S.W->eco;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-4.0f, -1.0f}; G.crew[2].p = {-1.0f, 1.0f};
        ss.SetGround("grotto");
        ss.Buy("shrimp"); while (G.boat.bunker < 90 && ss.Buy("coal")) {}
        ss.CastOff();
        Vector2 at{430, e.n * e.cell * 0.5f};
        for (int i = 0; i < e.n * e.n; i++) if (e.hab[i] == H_WRECK) { Vector2 w{(i % e.n + 0.5f) * e.cell, (i / e.n + 0.5f) * e.cell}; for (int k = 0; k < 8; k++) { Vector2 q = Vector2Add(w, {cosf(k * 0.785f) * 18, sinf(k * 0.785f) * 18}); if (e.DepthAt(q) > 10 && e.HabAt(q) == H_OPEN) { at = q; break; } } break; }
        G.boat.pos = at; G.boat.heading = 0.3f; G.boat.telegraph = 0; G.boat.lantern = 2;
        for (int i = 0; i < 60 * 4; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        G.botsOn = false; for (auto& c : G.crew) c.bot = false;
        G.anglerCool = G.drownedCool = 1e9f;
        G.angler.on = true; G.angler.t = 3; G.angler.p = G.boat.ToWorld(fp ? Vector2{-2, 9} : Vector2{-2, 9});
        G.worm.state = 1; G.worm.ang = 1.2f; G.worm.t = 4;
        G.isopods.state = 2; G.isopods.n = 18; G.isopods.eatT = 99;
        Gannet::DrownedSailor d; d.p = {-6.5f, -1.8f}; G.drowned.push_back(d);
        G.crew[1].station = -1; G.crew[1].deck = 0; G.crew[1].p = {-5.0f, -1.2f};
        if (fp) { G.crew[0].station = -1; G.crew[0].p = {-1.5f, 1.2f}; S.eye.yaw = PI - 0.4f; S.eye.pitch = -0.1f; }
        return;
    }
    if (which == 35) {
        // diving: down on a Weeds wreck in the hardhat, a hand at the air pump
        StartTrawl(g, fp, 2, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-2.0f, 1.0f};
        ss.SetGround("weeds");
        ss.Buy("shrimp"); while (G.boat.bunker < 70 && ss.Buy("coal")) {}
        ss.CastOff();
        G.hardhat = true; G.botsOn = false; for (auto& c : G.crew) c.bot = false;
        if (G.wrecks && !G.wrecks->empty()) {
            const Wreck& wk = (*G.wrecks)[0];
            G.boat.pos = {wk.x + 4, wk.y}; G.boat.vel = {0, 0}; G.boat.telegraph = 0;
            int pump = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::AirPump) pump = i;
            G.crew[1].p = Stations()[pump].at; G.crew[1].station = pump;
            G.crew[0].p = {-10.5f, 0}; G.crew[0].station = -1;
            G.StartDive(0);
            G.dive.depth = wk.depth; G.dive.room = wk.entries.empty() ? 0 : wk.entries[0]; G.dive.gauge = 0.7f;
            for (int i = 0; i < 30; i++) DiveSceneStep(G, 0, 1 / 60.0f, 0.0f, false, false, false);   // (the side view built, the diver settled on the floor)
        }
        return;
    }
    if (which == 34) {
        // Atlantis Waters: over the terraces at night; a cult longboat's torches circling, the Ghost Ship alongside with
        // a Drowned boarder on deck, the Choir's singer up off the bow, the Kraken's arms over the rail
        StartTrawl(g, fp, 3, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess; Eco& e = S.W->eco;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-4.0f, -1.0f}; G.crew[2].p = {-1.0f, 1.0f};
        ss.SetGround("atlantis");
        ss.Buy("shrimp"); while (G.boat.bunker < 130 && ss.Buy("coal")) {}
        ss.CastOff();
        G.boat.pos = {e.n * e.cell * 0.6f, e.n * e.cell * 0.5f}; G.boat.heading = 0.3f; G.boat.telegraph = 0; G.boat.lantern = 2;
        for (int i = 0; i < 60 * 4; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        G.botsOn = false; for (auto& c : G.crew) c.bot = false;
        G.choirCool = G.longboatCool = 1e9f; G.ghostDone = G.krakenDone = true;
        G.longboat.on = true; G.longboat.ang = 2.2f; G.longboat.p = Vector2Add(G.boat.pos, {cosf(2.2f) * 30, sinf(2.2f) * 30});
        G.ghost.state = 2; G.ghost.p = G.boat.ToWorld({0, -9});
        G.choir.on = true; G.choir.surfaced = true; G.choir.t = 1; G.choir.calmT = 99; G.choir.singer = G.boat.ToWorld({14, 6});
        G.kraken.state = 2; G.kraken.armT = 99;
        Gannet::DrownedSailor d; d.p = {-6.5f, -1.8f}; G.drowned.push_back(d);
        if (fp) { G.crew[0].station = -1; G.crew[0].p = {-1.5f, 1.2f}; S.eye.yaw = -1.2f; S.eye.pitch = -0.05f; }
        return;
    }
    if (which == 27 || which == 28 || which == 29 || which == 37 || which == 38) {
        // 27 out in the skiff, rowing away from the Gannet (lying stopped, her lantern full) with a fish aboard; 28 the
        // skiff going down on the davit, a hand at it
        StartTrawl(g, fp, 2, 1);
        S.shot = true;
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        G.crew[0].p = {3.0f, 0.8f}; G.crew[1].p = {-2.0f, 1.0f};
        ss.Buy("shrimp"); while (G.boat.bunker < 40 && ss.Buy("coal")) {}
        ss.CastOff();
        G.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 70, 20}); G.boat.heading = 0.4f; G.boat.telegraph = 0; G.boat.lantern = 2;
        S.W->eco.agentBudget = 200;
        for (int i = 0; i < 60 * 8; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        int dv = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Davit) dv = i;
        int lk = which == 37 ? LK_LIGHTHOUSE : which == 38 ? LK_SANDBAR : LK_ATOLL, li = -1;
        for (int i = 0; i < (int)G.landings.size(); i++) if (G.landings[i].kind == lk) li = i;
        if (which >= 37 && li >= 0) {
            // 37 ashore on the Old Lighthouse rock by the keeper's hearth; 38 digging on the Sandbar at the keeper's mark
            Landing& L = G.landings[li];
            G.sea.weather = Weather::Calm; ss.wxTo = Weather::Calm;
            G.skiff.state = SkiffState::Afloat; G.skiff.integrity = D().skiffIntegrity; G.skiff.p = Vector2Add(L.at, {L.r + 2.0f, -3.0f});
            G.crew[0].deck = DECK_SKIFF; G.crew[0].p = {0.2f, 0}; G.BeachSkiff(0);
            for (auto& k : L.caches) if (k.kind == 2) k.found = true;
            G.crew[0].p = which == 37 ? Vector2{2.5f, 5.6f} : Vector2Add(L.caches[0].p, {0.6f, 0.2f}); G.crew[0].facing = {-1, 0};
            if (which == 37) { CatchRec f; f.name = "snapper"; f.sp = Species().Find("snapper"); f.kg = 3.4f; f.price = 3; f.dead = true; f.cookT = 9; f.deckAt = L.fire; L.onFire.push_back(f); }
            for (int i = 0; i < 20; i++) G.Step(1 / 60.0f);
            if (fp) { S.eye.yaw = which == 37 ? (getenv("DEPTH_YAW") ? (float)atof(getenv("DEPTH_YAW")) : -2.27f) : 3.4f; S.eye.pitch = which == 37 ? 0.12f : -0.3f; }
            return;
        }
        if (which == 29 && !G.landings.empty()) {
            // ashore on the Atoll by the fire: a snapper cooking, a grunt waiting on the sand, the skiff beached, the elder
            Landing& L = G.landings[0];
            G.sea.weather = Weather::Calm; ss.wxTo = Weather::Calm; L.fireLit = true;
            G.skiff.state = SkiffState::Afloat; G.skiff.integrity = D().skiffIntegrity; G.skiff.p = Vector2Add(L.at, {L.r + 2.0f, -3.0f});
            G.crew[0].deck = DECK_SKIFF; G.crew[0].p = {0.2f, 0}; G.BeachSkiff(0);
            G.crew[0].p = Vector2Add(L.fire, {1.2f, 0.6f}); G.crew[0].facing = {-1, 0};
            CatchRec f; f.name = "snapper"; f.sp = Species().Find("snapper"); f.kg = 3.4f; f.price = 3; f.dead = true; f.cookT = 9; f.deckAt = L.fire; L.onFire.push_back(f);
            CatchRec gr = f; gr.name = "grunt"; gr.kg = 1.2f; gr.cookT = -1; gr.deckAt = Vector2Add(L.fire, {2.2f, 1.6f}); L.onBeach.push_back(gr);
            if (!L.caches.empty() && L.caches.back().kind == 2) L.caches.back().found = true;
            for (int i = 0; i < 20; i++) G.Step(1 / 60.0f);
            if (fp) { S.eye.yaw = 3.4f; S.eye.pitch = -0.28f; }
            return;
        }
        if (which == 28) {
            G.crew[0].p = Stations()[dv].at; G.crew[0].station = dv;
            G.skiff.state = SkiffState::Lowering; G.skiff.t = 4.5f;
            if (fp) { S.eye.yaw = PI; S.eye.pitch = -0.5f; }
        } else {
            G.skiff.state = SkiffState::Afloat; G.skiff.p = G.SkiffBerth(); G.skiff.heading = G.boat.heading + PI - 0.5f; G.skiff.integrity = D().skiffIntegrity;
            G.crew[0].p = Stations()[dv].at; G.BoardSkiff(0);
            CatchRec f; f.name = "snapper"; f.kg = 3.5f; f.price = 3; f.dead = true; G.SkiffLand(f);
            CatchRec big = f; big.name = "grouper"; big.kg = 42; G.towed.push_back(big);   // (on the tow line)
            float tt = 0; bool pp = true;
            for (int i = 0; i < 60 * 12; i++) { tt += 1 / 60.0f; if (tt >= D().skiffStroke) { tt = 0; G.Oar(0, pp, !pp); pp = !pp; } G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
            G.crew[0].oarT = 0.12f;
            if (fp) { S.eye.yaw = getenv("DEPTH_YAW") ? (float)atof(getenv("DEPTH_YAW")) : PI; S.eye.pitch = getenv("DEPTH_PITCH") ? (float)atof(getenv("DEPTH_PITCH")) : -0.12f; }
        }
        return;
    }
    if (which == 23 || which == 24) {
        // 23 the sonar out on the ground (a ping, the largest school marked); 24 the helm and its chart, steaming out
        Eye3D eye = S.eye;
        StartTrawl(g, fp, 1, 1);
        S.shot = true; S.eye = eye;
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        G.crew[0].p = {3.0f, 0.8f};
        ss.Buy("shrimp"); while (G.boat.bunker < 40 && ss.Buy("coal")) {}
        ss.CastOff();
        G.boat.pos = Vector2Add(ss.harbour, which == 23 ? Vector2{ss.harbourR + 60, 10} : Vector2{ss.harbourR - 20, 6}); G.boat.heading = 0.1f;
        S.W->eco.agentBudget = 260;
        for (int i = 0; i < 60 * 25; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        StationKind want = which == 23 ? StationKind::Sonar : StationKind::Helm;
        for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == want) { G.crew[0].p = Stations()[i].at; G.crew[0].station = i; }
        if (which == 23) {
            if (!ss.wrecks.empty()) G.boat.pos = {ss.wrecks[0].x + 55, ss.wrecks[0].y + 25};   // (a wreck in range)
            G.SonarPing(0);
            for (int k = 0; k < (int)G.sonar.ret.size(); k++) if (G.sonar.ret[k].kind == SonarKind::Wreck) { G.SonarMarkAt(0, G.boat.ToDeck({G.sonar.ret[k].p.x, G.sonar.ret[k].p.y})); break; }
            int best = -1, most = 0;
            for (int k = 0; k < (int)G.sonar.ret.size(); k++) if (G.sonar.ret[k].kind == SonarKind::School && G.sonar.ret[k].count > most) { most = G.sonar.ret[k].count; best = k; }
            if (best >= 0) G.SonarMarkAt(0, G.boat.ToDeck({G.sonar.ret[best].p.x, G.sonar.ret[best].p.y}));
            for (int i = 0; i < 40; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        } else { G.boat.telegraph = 2; for (int i = 0; i < 60 * 3; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); } }
        return;
    }
    if (which == 22) {
        // a guest's screen in a networked match: a host and a guest over the in-memory transport, two AI hands; the
        // guest's scene draws its mirror of the host's snapshot (the host has cast off and is out on the ground)
        static arcade::Session host, guest;
        host.Leave(); guest.Leave();
        std::string err;
        arcade::Profile ph{"Skipper", 1}, pg{"Deckhand", 2};
        host.Host(ph, arcade::G_TRAWL, &err, 47796, net::MakeMemoryTransport(), false);
        guest.Join(pg, "mem:47796", &err, 0, net::MakeMemoryTransport());
        double t = 0;
        auto pump = [&](int frames) { for (int i = 0; i < frames; i++) { t += 1 / 60.0; host.Update(t, 1 / 60.0f); guest.Update(t, 1 / 60.0f); } };
        for (int i = 0; i < 120 && guest.stage != arcade::S_LOBBY; i++) pump(1);
        host.AddAI(); host.AddAI(); guest.SetReady(true);
        pump(30);
        std::string why; host.Launch(&why);
        pump(10);
        TrawlWorld* w = TrawlHostWorld(host.HostGame());
        if (w) {
            w->sess.Buy("shrimp"); w->sess.Buy("shrimp");
            while (w->G.boat.bunker < 40 && w->sess.Buy("coal")) {}
            w->G.crew[0].p = {3.0f, 0.8f};
            w->sess.CastOff(); w->G.boat.pos = Vector2Add(w->sess.harbour, {w->sess.harbourR + 50, 8}); w->G.boat.heading = 0.1f;
            w->eco.agentBudget = 260;
        }
        pump(60 * 30);
        Eye3D eye = S.eye;
        StartTrawlNet(g, &guest, fp);
        S.eye = eye;
        return;
    }
    if (which == 21) {
        // the bot crew at work: five hands, four of them bots, hove to on the ground with the lines out
        Eye3D eye = S.eye;
        StartTrawl(g, fp, 5, 1);
        S.shot = true; S.eye = eye;
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        G.crew[0].p = {-1, 0.8f};
        ss.Buy("shrimp"); ss.Buy("shrimp");
        while (G.boat.bunker < 40 && ss.Buy("coal")) {}
        ss.CastOff(); G.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 50, 8}); G.boat.heading = 0.1f;
        S.W->eco.agentBudget = 260;
        for (int i = 0; i < 60 * 40; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        for (int i = 1; i < (int)G.crew.size(); i++) if (G.crew[i].station >= 0 && G.RodAt(G.crew[i].station) >= 0) { G.brains[i].bark = "Fish on, port!"; G.brains[i].barkT = 2; break; }
        return;
    }
    if (which >= 15 && which <= 20) {
        // 15 the net down and filling, 16 a rifle and a shot fish afloat with gulls over, 17 overboard and the ring,
        // 18 a ghost on deck, 19 the harpoon fast in a shark, 20 the deck locker
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        G.crew.push_back(G.crew[0]); G.crew[1].slot = 1; G.crew[1].role = Role::Angler; G.crew[1].p = {-3, 1.2f};
        for (auto& sl : G.crew[1].slots) sl = Slot{};
        G.crew[0].p = {-1, 0.8f};
        ss.CastOff(); G.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 60, 5}); G.boat.heading = 0.1f;
        for (int i = 0; i < 20; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        S.W->eco.agentBudget = 260;
        for (int i = 0; i < 60 * 25; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        Crew& c = G.crew[0];
        if (which == 15) {
            int ws = (int)StationKind::NetWinch; c.p = Stations()[ws].at; c.station = ws;
            G.net.state = NetState::Down; G.net.depth = 5; G.boat.telegraph = 1; G.boat.pressure = 0.7f; G.boat.firebox = 6;
            for (int i = 0; i < 60 * 12; i++) {
                if (i % 90 == 0) { Vector2 w = G.boat.ToWorld({-30, 0}); int ai = S.W->eco.SpawnAgentPublic(Species().Find("sardine"), w); S.W->eco.agents[ai].count = 150; S.W->eco.agents[ai].p.z = G.net.depth; }
                if (G.boat.pressure < 0.6f) G.boat.Shovel(1);
                G.Step(1 / 60.0f);
            }
        }
        if (which == 16) {
            c.slots[3] = {Item::Rifle, 10}; c.sel = 3; c.p = {1, 2.5f};
            Floater f; f.name = "bonito"; f.sp = Species().Find("bonito"); f.kg = 4; f.price = 3; f.grade = 0.7f; f.p = G.boat.ToWorld({2, 7}); G.floaters.push_back(f);
            int gs = Species().Find("gull flock"); int ai = S.W->eco.SpawnAgentPublic(gs, G.boat.ToWorld({0, 0})); S.W->eco.agents[ai].count = 14; S.W->eco.agents[ai].p.z = -3;
            G.UseItem(0, {3, 12}, true, true, true, 1 / 60.0f);
            for (int i = 0; i < 2; i++) G.Step(1 / 60.0f);
            G.crew[1].slots[0] = {Item::Shotgun, 8}; G.crew[1].p = {-2, 2.5f};
            G.UseItem(1, {-1, 16}, true, true, false, 1 / 60.0f);
            G.Step(1 / 60.0f);
        }
        if (which == 17) {
            S.you = 0;
            G.GoOverboard(0, "a slip"); G.crew[0].swim = G.boat.ToWorld({-4, 9});
            G.crew[1].slots[0] = {Item::Ring, 1}; G.crew[1].sel = 0; G.crew[1].p = {-3, 2.5f};
            G.UseItem(1, G.boat.ToDeck(G.crew[0].swim), true, true, false, 1 / 60.0f);
            for (int i = 0; i < 70; i++) { G.Step(1 / 60.0f); }
            G.crew[0].drownT = 14;
        }
        if (which == 18) {
            G.Injure(0, INJ_BITE, "a reef shark"); G.Injure(0, INJ_BROKEN_ARM, "a fall");
            c.p = {-6, -1.2f};
            int sh = Species().Find("reef shark"); int ai = S.W->eco.SpawnAgentPublic(sh, G.boat.ToWorld({-8, -8})); S.W->eco.agents[ai].count = 1; S.W->eco.agents[ai].p.z = 2;
            S.ghostSee = 1;
        }
        if (which == 19) {
            G.harpoonCannon = true; G.harpoons = 2; G.explosives = 1;
            int hs = (int)StationKind::Harpoon; c.p = Stations()[hs].at; c.station = hs;
            int sh = Species().Find("reef shark"); Vector2 at = G.boat.ToWorld({20, 3});
            int ai = S.W->eco.SpawnAgentPublic(sh, at); S.W->eco.agents[ai].count = 1; S.W->eco.agents[ai].p = {at.x, at.y, 1}; S.W->eco.agents[ai].fedT = 100;
            for (int k = 0; k < 3 && G.harpoon.state != RodState::Fighting; k++) {
                for (auto& a : S.W->eco.agents) if (a.sp == sh) a.p = {at.x, at.y, 1};
                G.harpoonReload = 0; G.HarpoonInput(0, {19.6f, 3}, true, false, false, 1 / 60.0f);
                for (int i = 0; i < 60; i++) G.Step(1 / 60.0f);
            }
            for (int i = 0; i < 90; i++) { G.HarpoonInput(0, {18, 3}, false, true, false, 1 / 60.0f); G.Step(1 / 60.0f); }
        }
        if (which == 20) {
            int ls = (int)StationKind::Locker; c.p = Stations()[ls].at; c.station = ls;
            G.locker.push_back({Item::Knife, 0}); G.locker.push_back({Item::Longline, 1}); G.locker.push_back({Item::Charge, 2}); G.locker.push_back({Item::Bandage, 3});
        }
        return;
    }
    if (which >= 9) {
        // 9 the quay, 10 the Chandler, 11 the Fish Market after a night, 12 the chart table, 13 the wheelhouse clock
        // at sea with the tape, 14 the quota met
        Gannet& G = S.W->G; Session& ss = S.W->sess;
        Crew& c = G.crew[0];
        if (which == 9 || which == 10 || which == 11) c.p = which == 11 ? Vector2{3.0f, -7.2f} : which == 10 ? Vector2{-3.5f, -7.2f} : Vector2{-6, -5.5f};
        if (which == 10) S.panel = (int)DockKind::Chandler;
        if (which == 57) {   // the chalkboard's role upgrades: an Angler two met deadlines in, rank 2 chosen, a bot crewmate
            c.p = {-6, -5.5f}; S.panel = (int)DockKind::Chalkboard; gChalkUps = true;
            c.role = Role::Angler; ss.metCount = 2; ss.ChooseUp(0, 2, 1);
        }
        if (which == 30) {   // the chalkboard: the harbour's requests, a mini-boss drop to wear, a lucky coin worn
            c.p = {-6, -5.5f}; S.panel = (int)DockKind::Chalkboard; gChalkUps = false;
            G.drops = {"a jaw full of old hooks"}; c.charm = CH_LUCKY_COIN; G.bossLures = 1; G.highKills = 3;
        }
        if (which == 26) {   // the Gunsmith, with a revolver in hand (one upgrade, a sight) and money to spend
            c.p = {-7.6f, -7.2f}; ss.money = 900;
            c.slots[3] = Slot{}; ss.GunBuy(0, "revolver"); ss.GunUpgrade(0, 3); ss.GunAttach(0, 3, "sight");
            c.sel = 3; G.ammoRounds = 24;
            S.panel = (int)DockKind::Gunsmith;
        }
        if (which == 11 || which == 25) {   // (25: the Owners' quota scales, one fish too far gone to be taken)
            if (which == 25) c.p = {4.6f, -7.2f};
            const char* names[] = {"snapper", "grunt", "reef squid", "bonito", "snapper (head)"};
            float kg[] = {3.2f, 1.1f, 0.7f, 4.1f, 1.2f}, pr[] = {3, 1.5f, 4, 3, 3};
            for (int i = 0; i < 5; i++) { CatchRec cr; cr.name = names[i]; cr.kg = kg[i]; cr.price = pr[i]; cr.gutted = i != 2; cr.iced = i != 2; cr.fresh = i == 2 ? (which == 25 ? 0.62f : 0.93f) : 0.98f; cr.grade = i == 4 ? 0.9f : 1; cr.first = i == 0; G.hold.push_back(cr); }
            S.panel = which == 25 ? (int)DockKind::Scales : (int)DockKind::Market;
        }
        if (which == 12) { c.p = {4.2f, 0}; c.station = NearestStation(c.p, 0, 1.1f); S.panel = PANEL_CHART; ss.Buy("shrimp"); }
        if (which == 13) {
            c.p = {3.0f, 0.8f};
            ss.Buy("shrimp"); ss.CastOff();
            G.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 40, 10}); G.boat.heading = 0.2f;
            for (int i = 0; i < 60 * 3; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
            ss.clock = 283; ss.Step(0.1f);
        }
        if (which == 14) { ss.night = 3; ss.sold = 240; ss.Count(); S.panel = PANEL_END; }
        for (int i = 0; i < 30; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        return;
    }


    Gannet& G = S.W->G;
    G.Init(4, 11, which == 3 ? Weather::Squall : Weather::Calm);
    G.boat.telegraph = 1; G.boat.pressure = 0.7f;
    Crew& c = G.crew[0];
    G.crew[1].p = {-0.8f, 2.4f}; G.crew[1].station = NearestStation(G.crew[1].p, 0, 1.1f); G.crew[1].facing = {0, 1};
    G.crew[2].p = {-8.2f, 0.4f}; G.crew[2].facing = {-1, 0};
    G.crew[3].p = {-2.2f, 1.6f}; G.crew[3].facing = {0, 1};
    if (which == 1) { c.deck = 1; c.p = {-4.8f, 0.2f}; c.station = NearestStation(c.p, 1, 1.1f); G.boat.firebox = 5; G.boat.Hit(SEC_STERN_P, 75); for (int i = 0; i < 60 * 10; i++) G.Step(1 / 60.0f); }
    else if (which == 2) { c.p = {4.2f, 0}; c.station = NearestStation(c.p, 0, 1.1f); G.boat.rudder = 0.4f; }
    else if (which >= 6) {
        S.W->eco.Init("lagoon", 11); G.eco = &S.W->eco;
        G.boat.telegraph = 0; G.boat.pos = {S.W->eco.n * S.W->eco.cell * (which == 7 ? 0.36f : 0.42f), S.W->eco.n * S.W->eco.cell * 0.5f}; G.boat.heading = -0.3f;
        G.boat.lantern = which == 7 ? 3 : 2; G.boat.searchAim = 0.9f;
        c.p = {0.8f, 0.9f};
        if (which == 7) { c.p = {0.2f, 0.6f}; c.station = NearestStation({0.2f, 0}, 0, 1.1f); }
        int shark = Species().Find("reef shark");
        for (int i = 0; i < 60 * (which == 8 ? 100 : 70); i++) {
            if (which == 8 && i % (60 * 20) == 0) { Vector2 w = G.boat.ToWorld({-11, 0}); S.W->eco.AddBlood({w.x, w.y, 1}, 60); }
            if (which == 8 && i == 60 * 30) {
                int ai = S.W->eco.SpawnAgentPublic(shark, G.boat.ToWorld({-14, 9})); S.W->eco.agents[ai].hunger = 0.9f; S.W->eco.agents[ai].p.z = 1.2f; S.W->eco.agents[ai].count = 1;
                { CatchRec cr; cr.name = "snapper"; cr.kg = 2.5f; cr.price = 3; G.hold.push_back(cr); }
            }
            G.Step(1 / 60.0f);
        }
        return;
    }
    else if (which == 4 || which == 5) {
        // a fish on the starboard rod, the port rod's float out
        for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::StarRod) { c.p = Stations()[i].at; c.station = i; }
        G.crew[1].station = -1; G.crew[1].p = {-1.8f, 1.9f};
        int ri = G.RodAt(c.station);
        Rod& r = G.rods[ri];
        const FishSpec* fs = FindDummyFish(which == 5 ? "marlin" : "yellowfin");
        r.tackle = which == 5 ? Tackle::Chair : Tackle::Heavy; r.hook = which == 5 ? Hook::Treble : Hook::Small;
        r.fight = Fight{}; r.fight.tackle = r.tackle; r.fight.hook = r.hook; r.fight.drag = 0.4f * TackleOf(r.tackle).strength;
        Vector2 tw = G.boat.ToWorld(r.TipDeck()), at = G.boat.ToWorld(Vector2Add(r.TipDeck(), which == 5 ? Vector2{-6, 4} : Vector2{4, 4}));
        r.fight.tip = {tw.x, tw.y, -2.7f}; r.fight.outboard = Vector2Normalize(Vector2Subtract(G.boat.ToWorld({0, 1}), G.boat.ToWorld({0, 0})));
        r.fight.HookFish(*fs, {at.x, at.y, which == 5 ? 0.5f : 3.0f}, 5);
        r.state = RodState::Fighting; r.botAngler = true;
        int pi = G.RodAt(3); (void)pi;
        for (auto& o : G.rods) if (Stations()[o.station].kind == StationKind::PortRod) {
            Vector2 w = G.boat.ToWorld({1.5f, -9}); o.lure = {w.x, w.y, 4}; o.state = RodState::Out; o.lineOut = 7; o.lureDepth = 6;
            o.bite.Start(FindDummyFish("snapper"), false, false, 3); o.bite.stage = BiteStage::Nibble; o.bite.t = 0.1f; o.lastTick = 0.5f;
        }
        r.fight.burstT = 0; r.fight.nextBurst = 20; r.fight.effort = 0.6f;
        for (int i = 0; i < 20; i++) { G.Step(1 / 60.0f); for (auto& o : G.rods) if (o.state == RodState::Out) { o.bite.stage = BiteStage::Nibble; o.bite.t = 0.1f; } }
        if (which == 5) { r.fight.jumpT = 0.0f; r.fight.p.z = -0.5f; r.fight.botBowAt = 0.1f; }
        return;
    }
    else c.p = {1.2f, -2.0f};
    for (int i = 0; i < 60 * 6; i++) G.Step(1 / 60.0f);
}
