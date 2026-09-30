// The Trawl's scene (design doc, "The session loop" and "Controls, HUD, and the sonar"): the Gannet at night seen
// from above, the hand you play walking her deck and working her stations. The simulation is trawl_boat.cpp (and,
// as the stages come, the lines, the fish and the web); this file feeds it input and draws it.
#include "trawl.h"
#include "trawl_art.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "trawl_view3d.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

using namespace tw;

namespace {
struct TrawlScene {
    bool active = false;
    Gannet G;
    Eco eco;                       // the ground (stage 3: the Eclipse Lagoon)
    int you = 0;
    float acc = 0;                 // the fixed 60 Hz step's accumulator
    bool shot = false;             // --shots: no input, fixed time
    int shotView = 0;
    float wheel = 0;               // the mouse wheel, gathered per frame for the next fixed step
    View view;                     // the last frame's view (the mouse's deck position)
    Session sess;                  // the run: deadlines, nights, the quota, the dock
    int panel = -1;                // an open dock panel (DockKind), PANEL_CHART at the helm, PANEL_END for the count
    std::string toast; float toastT = 0;
    size_t tapeSeen = 0; float tapeT = 0;
    bool lmbPressed = false, rmbPressed = false;   // latched per frame for the fixed steps
    float ghostSee = 0;
    // the first-person version (trawl_view3d.cpp): the same game through the hand's eyes
    bool fp = false;
    Eye3D eye;
    Camera3D cam{};
};
const int PANEL_CHART = 20, PANEL_END = 21;
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
// where the hand aims on the water, in the deck frame: the mouse top-down, the crosshair in first person
Vector2 AimDeck() {
    if (S.fp) { Vector2 d; AimAtWater(S.G, S.cam, {SCREEN_W / 2.0f, SCREEN_H / 2.0f}, &d); return d; }
    const float PX = (float)SCREEN_W / PIXEL_W;
    Vector2 m = GetMousePosition();
    return S.view.DeckOfCanvas({m.x / PX + 1, m.y / PX + 1});
}
void Controls(float dt) {
    Gannet& g = S.G;
    Crew& c = g.crew[S.you];
    Vector2 wish{0, 0};
    if (S.panel >= 0) { g.Move(S.you, wish, false, dt); return; }
    bool lmbP = S.lmbPressed, rmbP = S.rmbPressed;
    S.lmbPressed = S.rmbPressed = false;
    Vector2 aimDeck = AimDeck();
    if (c.overboard || c.dead) {
        // in the water you swim; dead, you walk the deck as a ghost (and can only ring the bell)
        wish = KeysWish();
        if (c.dead && Vector2Length(wish) > 0) S.ghostSee = 1.0f;
        g.Move(S.you, wish, false, dt);
        if (c.dead) g.Primary(S.you, IsMouseButtonDown(MOUSE_BUTTON_LEFT), dt);
        return;
    }
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::NetWinch) { g.NetInput(S.you, IsMouseButtonDown(MOUSE_BUTTON_LEFT), rmbP, dt); g.Move(S.you, wish, false, dt); return; }
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Harpoon) { g.HarpoonInput(S.you, aimDeck, lmbP, IsMouseButtonDown(MOUSE_BUTTON_LEFT), rmbP, dt); g.Move(S.you, wish, false, dt); return; }
    wish = KeysWish();
    bool atHelm = c.station >= 0 && Stations()[c.station].kind == StationKind::Helm;
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Lantern && g.boat.lantern == 3) {
        // the searchlight follows the mouse (in first person, the crosshair)
        Vector2 d = Vector2Subtract(aimDeck, Stations()[c.station].at);
        if (Vector2Length(d) > 0.5f) g.boat.searchAim = atan2f(d.y, d.x);
    }
    if (atHelm) {
        g.Steer(S.you, (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f), dt);
        if (IsKeyPressed(KEY_W)) g.Scroll(S.you, 1);
        if (IsKeyPressed(KEY_S)) g.Scroll(S.you, -1);
        wish = {0, 0};
    }
    int ri = c.station >= 0 ? g.RodAt(c.station) : -1;
    if (ri >= 0) {
        // a rod: hold left to charge the cast (released, it flies at the mouse); once out, hold left to reel. Space
        // strikes a bite and gaffs a fish alongside; right mouse bows the rod; the mouse's side of the line leans it
        // (side pressure); the wheel is the drag (or the lure's depth before a bite).
        Rod& r = g.rods[ri];
        Vector2 aim = aimDeck;
        bool lmb = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool casting = r.state == RodState::Idle || r.state == RodState::Charging;
        float lean = 0;
        if (r.state == RodState::Fighting) {
            Vector2 tip = r.TipDeck(), fish = g.boat.ToDeck({r.fight.p.x, r.fight.p.y});
            Vector2 line = Vector2Subtract(fish, tip), to = Vector2Subtract(aim, tip);
            float cr = line.x * to.y - line.y * to.x, dt2 = Vector2DotProduct(line, to);
            lean = std::clamp(atan2f(cr, std::max(0.01f, fabsf(dt2))) / (45 * DEG2RAD), -1.0f, 1.0f);
        }
        g.RodInput(S.you, casting && lmb, aim, !casting && lmb, IsKeyPressed(KEY_SPACE), lean, IsMouseButtonDown(MOUSE_BUTTON_RIGHT), IsKeyPressed(KEY_SPACE), S.wheel);
        S.wheel = 0;
        wish = {0, 0};
        g.Move(S.you, wish, IsKeyDown(KEY_LEFT_SHIFT), dt);
        return;
    }
    g.Move(S.you, wish, IsKeyDown(KEY_LEFT_SHIFT), dt);
    if (c.station < 0) g.UseItem(S.you, aimDeck, lmbP, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonDown(MOUSE_BUTTON_RIGHT), dt);
    g.Primary(S.you, IsMouseButtonDown(MOUSE_BUTTON_LEFT), dt);
    g.Secondary(S.you, IsMouseButtonDown(MOUSE_BUTTON_RIGHT), dt);
}
void Pressed(Game& g) {
    Gannet& G = S.G;
    Crew& c = G.crew[S.you];
    if (S.panel >= 0) {
        if (IsKeyPressed(KEY_X) && S.panel != PANEL_END) { S.panel = -1; G.LeaveStation(S.you); }
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) S.lmbPressed = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) S.rmbPressed = true;
    for (int k = 0; k < 4; k++) if (IsKeyPressed(KEY_ONE + k) && c.station < 0) c.sel = k;
    if (IsKeyPressed(KEY_R)) G.Reload(S.you);
    if (IsKeyPressed(KEY_V)) { S.fp = !S.fp; if (S.fp) S.eye = Eye3D{}; }   // the two versions of the game: top-down and first person
    if (IsKeyPressed(KEY_T) && c.station >= 0 && Stations()[c.station].kind == StationKind::Harpoon) { G.explosiveLoaded = !G.explosiveLoaded && G.explosives > 0; }
    if (IsKeyPressed(KEY_E)) {
        int d = G.moored && c.deck == 0 && c.station < 0 ? NearestDock(c.p, 1.4f) : -1;
        if (d >= 0) S.panel = (int)DockStations()[d].kind;
        else if (c.station < 0 && !c.dead && (G.GaffFloater(S.you) || G.HaulSetGear(S.you))) {}
        else if (G.TakeStation(S.you) && Stations()[c.station].kind == StationKind::Helm && S.sess.phase == Phase::Dock) S.panel = PANEL_CHART;
    }
    if (IsKeyPressed(KEY_X)) G.LeaveStation(S.you);
    float wheel = GetMouseWheelMove();
    bool atRod = G.crew[S.you].station >= 0 && G.RodAt(G.crew[S.you].station) >= 0;
    if (atRod) { S.wheel += wheel; if (IsKeyPressed(KEY_T)) G.CycleTackle(S.you); }
    else if (wheel != 0) G.Scroll(S.you, wheel);
    (void)g;
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
    int ri = g.RodAt(c.station);
    if (ri < 0) return;
    const Rod& r = g.rods[ri];
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
    TxtBold(TextFormat("%s, %s line, %s hook  (T: change tackle)", td.name, LineOf(r.line).name, HookName(r.hook)), x0, SCREEN_H - 60, 15, paper);
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
                  "Left: reel   Right: bow   Mouse to one side: side pressure   Scroll: drag";
            break;
    }
    if (tip) DrawTextCenteredBold(tip, SCREEN_W / 2.0f, SCREEN_H - 150.0f, 18, r.bite.stage == BiteStage::Take || (r.state == RodState::Fighting && (r.fight.alongside || r.fight.jumpT >= 0)) ? Color{250, 200, 110, 255} : paper);
    if (r.state == RodState::Fighting && c.role == Role::Angler) TxtBold(TextFormat("%s, about %.0f kg", r.fight.spec.name, r.fight.spec.kg), x0, SCREEN_H - 82, 15, Color{150, 200, 170, 255});
    if (!r.lastCatch.empty() && r.state != RodState::Fighting) Txt(TextFormat("Last: %s", r.lastCatch.c_str()), x0, SCREEN_H - 82, 14, Fade(paper, 0.6f));
}
void StationOverlay() {
    const Gannet& g = S.G;
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
            if (S.sess.phase == Phase::SailOut) DrawTextCenteredBold("Steam out past the harbour line: the night starts there", SCREEN_W / 2.0f, 150, 18, paper);
            break;
        }
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
                Txt(TextFormat("Grade %.0f%%   Fresh %.0f%%   worth about %.0f sh", r.grade * 100, r.fresh * 100, S.sess.Value(r)), x0, y0 + 26, 15, Fade(paper, 0.85f));
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
            TxtBold(TextFormat("The deck locker: click an item to swap it with your slot %d (%s)", c.sel + 1, ItemOf(c.slots[c.sel].it).name), x0, y0, 16, paper);
            auto& G2 = S.G;
            for (int i = 0; i < (int)G2.locker.size() && i < 12; i++) {
                const Slot& sl = G2.locker[i];
                if (Button({x0 + (i % 2) * 300, y0 + 34 + (i / 2) * 42.0f, 290, 36}, sl.ammo > 0 && sl.it != Item::Ring ? TextFormat("%s (%d)", ItemOf(sl.it).name, sl.ammo) : ItemOf(sl.it).name, true, 15)) {
                    Slot tmp = G2.crew[S.you].slots[G2.crew[S.you].sel]; G2.crew[S.you].slots[G2.crew[S.you].sel] = sl;
                    if (tmp.it == Item::None) G2.locker.erase(G2.locker.begin() + i); else G2.locker[i] = tmp;
                    break;
                }
            }
            if (G2.locker.empty()) Txt("Empty.", x0, y0 + 40, 15, Fade(paper, 0.7f));
            if (c.slots[c.sel].it != Item::None && Button({x0, y0 + 290, 290, 34}, "Stow what's in hand", true, 15)) { G2.locker.push_back(c.slots[c.sel]); G2.crew[S.you].slots[c.sel] = Slot{}; }
            break;
        }
        case StationKind::Printer: {
            // the Owners' tape, newest at the bottom
            float x0 = SCREEN_W / 2.0f - 330, y0 = 120;
            DrawRectangle((int)x0 - 14, (int)y0 - 14, 688, 300, Color{226, 216, 190, 245});
            const auto& T = S.sess.tape;
            int n = (int)T.size(), from = std::max(0, n - 10);
            for (int i = from; i < n; i++) Txt(T[i].c_str(), x0, y0 + (i - from) * 27.0f, 15, Color{40, 32, 24, 255});
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
void Panels(Game& g) {
    if (S.panel < 0) return;
    Session& ss = S.sess;
    Gannet& G = S.G;
    Color ink{50, 36, 24, 255}, dim{100, 80, 60, 255};
    Rectangle r;
    std::string why;
    auto toast = [&](const std::string& t) { S.toast = t; S.toastT = 3; };
    switch (S.panel) {
        case (int)DockKind::Chalkboard: {
            PanelFrame("The chalkboard", 560, 360, &r);
            float x = r.x + 40, y = r.y + 64;
            TxtBold(TextFormat("Deadline %d", ss.deadline), x, y, 22, ink);
            TxtBold(TextFormat("Quota: %.0f shillings", ss.quota), x, y + 40, 22, ink);
            TxtBold(TextFormat("Sold this deadline: %.0f", ss.sold), x, y + 74, 22, ss.sold >= ss.quota ? Color{40, 110, 50, 255} : ink);
            TxtBold(TextFormat("Nights left: %d", ss.NightsLeft()), x, y + 108, 22, ink);
            TxtBold(TextFormat("Money: %.0f shillings", ss.money), x, y + 142, 22, ink);
            Txt(TextFormat("Arcade tokens this run: %d", ss.tokens), x, y + 180, 16, dim);
            if (ss.night >= 3 && Button({r.x + r.width / 2 - 150, r.y + r.height - 76, 300, 44}, "Hand in to the Owners")) { ss.Count(); S.panel = PANEL_END; }
            break;
        }
        case (int)DockKind::Chandler: {
            PanelFrame("The Chandler", 1000, 600, &r);
            float x = r.x + 26, y = r.y + 56;
            TxtBold(TextFormat("Money %.0f     Ice %.0f kg     Shrimp %d     Squid %d     Coal %.0f kg     Chum %d", ss.money, G.ice, G.baitShrimp, G.baitSquid, G.boat.bunker, G.chum), x, y, 16, ink);
            const auto& I = ChandlerItems();
            int half = ((int)I.size() + 1) / 2;
            for (int i = 0; i < (int)I.size(); i++) {
                float cx = x + (i / half) * 480, yy = y + 30 + (i % half) * 36;
                TxtBold(I[i].name, cx, yy + 6, 15, ink);
                if (Button({cx + 360, yy, 96, 30}, TextFormat("%d sh", I[i].price), ss.money >= I[i].price, 15)) {
                    if (!ss.Buy(I[i].id, &why)) toast(why); else toast(std::string("Bought: ") + I[i].name + " (" + I[i].note + ")");
                }
                Rectangle hot{cx, yy, 350, 30};
                if (CheckCollisionPointRec(GetMousePosition(), hot)) Txt(I[i].note, r.x + 26, r.y + r.height - 30, 14, dim);
            }
            break;
        }
        case (int)DockKind::Market: {
            PanelFrame("The Fish Market", 700, 520, &r);
            float x = r.x + 30, y = r.y + 60;
            if (!G.hold.empty()) {
                float tot = 0;
                int shown = 0;
                for (const auto& c : G.hold) {
                    float v = ss.Value(c); tot += v;
                    if (shown++ < 9) {
                        float yy = y + shown * 26.0f;
                        Txt(c.name.c_str(), x, yy, 15, ink);
                        Txt(TextFormat("%.1f kg", c.kg), x + 190, yy, 15, ink);
                        Txt(TextFormat("grade %.0f%%", c.grade * 100), x + 270, yy, 15, ink);
                        Txt(TextFormat("fresh %.0f%%", c.fresh * 100), x + 380, yy, 15, ink);
                        Txt(c.iced ? "iced" : c.gutted ? "gutted" : "on deck", x + 480, yy, 15, c.iced ? ink : Color{150, 60, 40, 255});
                        Txt(TextFormat("%.1f sh", v), x + 560, yy, 15, ink);
                    }
                }
                if ((int)G.hold.size() > 9) Txt(TextFormat("... and %d more", (int)G.hold.size() - 9), x, y + 10 * 26.0f, 15, dim);
                TxtBold(TextFormat("About %.0f shillings (the glut counts as they're weighed)", tot), x, r.y + r.height - 110, 17, ink);
                if (Button({r.x + r.width / 2 - 110, r.y + r.height - 76, 220, 44}, "Sell the catch")) { ss.Sell(); toast(TextFormat("Paid %.0f shillings", ss.lastSaleTotal)); }
            } else if (!ss.lastSale.empty()) {
                int k = 0;
                for (const auto& l : ss.lastSale) if (k++ < 10) {
                    float yy = y + k * 26.0f;
                    Txt(l.name.c_str(), x, yy, 14, ink);
                    Txt(TextFormat("%.1f kg x %.1f sh x grade %.0f%% x fresh %.0f%% x glut %.0f%%%s", l.kg, l.price, l.grade * 100, l.fresh * 100, l.glut * 100, l.bonus > 1 ? " x first 150%" : ""), x + 170, yy, 14, dim);
                    Txt(TextFormat("%.1f", l.value), x + 590, yy, 14, ink);
                }
                TxtBold(TextFormat("Paid %.0f shillings", ss.lastSaleTotal), x, r.y + r.height - 100, 20, ink);
            } else TxtBold("Nothing to sell.", x, y + 20, 18, dim);
            break;
        }
        case (int)DockKind::Office:
            PanelFrame("The Owners' office", 560, 260, &r);
            TxtBold("The window is shuttered. No salvage to sell.", r.x + 40, r.y + 80, 18, ink);
            Txt("(Wrecks and diving come aboard in a later stage. Salvage pays 40% after night one,", r.x + 40, r.y + 120, 14, dim);
            Txt("70% after night two, 100% at the deadline count.)", r.x + 40, r.y + 140, 14, dim);
            break;
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
                    if (!ss.BuySlip(i, &why)) toast(why); else toast(std::string("Fitted: ") + I[i].name);
                }
            }
            break;
        }
        case PANEL_CHART: {
            PanelFrame("The chart table", 640, 420, &r);
            float x = r.x + 40, y = r.y + 64;
            TxtBold("Eclipse Lagoon", x, y, 22, ink);
            Txt("3-40 m. Coal to reach: 10 kg. Fish value low. The reef tide falls all night.", x, y + 30, 15, dim);
            for (int k = 0; k < 3; k++) {
                static const char* N[3] = {"The Weeds", "The Grotto", "Atlantis Waters"};
                Txt(TextFormat("%s  (charted in a later refit)", N[k]), x, y + 70 + k * 26.0f, 16, Fade(dim, 0.6f));
            }
            TxtBold(TextFormat("Night %d of 3.   Bunker %.0f kg.   Back across the harbour line before 05:00.", ss.night + 1, G.boat.bunker), x, y + 170, 15, ink);
            bool ok = ss.CanCastOff(&why);
            if (!ok) Txt(why.c_str(), x, y + 200, 15, Color{150, 50, 40, 255});
            if (Button({r.x + r.width / 2 - 110, r.y + r.height - 80, 220, 46}, "Cast off", ok)) {
                if (ss.CastOff(&why)) { S.panel = -1; toast("Cast off: raise steam and steer out past the harbour line"); } else toast(why);
            }
            break;
        }
        case PANEL_END: {
            bool met = ss.phase == Phase::Result;
            PanelFrame(met ? "QUOTA MET" : "GANNET REPOSSESSED", 600, 300, &r);
            TxtBold(TextFormat("Sold %.0f against a quota of %.0f", ss.sold, ss.quota), r.x + 40, r.y + 80, 20, ink);
            Txt(ss.tape.empty() ? "" : ss.tape.back().c_str(), r.x + 40, r.y + 120, 14, dim);
            if (met) { if (Button({r.x + r.width / 2 - 130, r.y + r.height - 80, 260, 46}, "Next deadline")) { ss.Continue(); S.panel = -1; } }
            else if (Button({r.x + r.width / 2 - 130, r.y + r.height - 80, 260, 46}, "Back to the arcade")) { S.active = false; EnableCursor(); g.scene = Scene::Arcade; }
            break;
        }
    }
}
void Hud(Game& g) {
    const Gannet& G = S.G;
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
    // the four slots and what's wrong with you (design doc: "Always on screen: the four inventory slots")
    for (int k = 0; k < 4; k++) {
        const Slot& sl = c.slots[k];
        Rectangle r{20.0f + k * 104, SCREEN_H - 196.0f, 98, 40};
        DrawRectangleRec(r, Fade(Color{20, 16, 12, 255}, 0.7f));
        DrawRectangleLinesEx(r, k == c.sel ? 2 : 1, k == c.sel ? Color{230, 200, 130, 255} : Fade(paper, 0.3f));
        Txt(TextFormat("%d %s", k + 1, sl.it == Item::None ? "" : ItemOf(sl.it).name), r.x + 6, r.y + 5, 13, Fade(paper, sl.it == Item::None ? 0.4f : 0.9f));
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
    if ((S.sess.phase == Phase::Night || S.sess.phase == Phase::SailOut) && (inWheelhouse || G.watch))
        TxtBold(S.sess.ClockText(), SCREEN_W / 2.0f - 30, 16, 26, S.sess.clock > 480 ? Color{240, 140, 100, 255} : paper);
    // the telegraph's newest tape, for a moment
    if (S.sess.tape.size() != S.tapeSeen) { S.tapeSeen = S.sess.tape.size(); S.tapeT = 6; }
    if (S.tapeT > 0 && !S.sess.tape.empty()) DrawTextCentered(S.sess.tape.back(), SCREEN_W / 2.0f, 52, 15, Fade(Color{230, 215, 170, 255}, std::min(1.0f, S.tapeT)));
    if (S.toastT > 0) DrawTextCenteredBold(S.toast, SCREEN_W / 2.0f, SCREEN_H / 2.0f + 170, 18, Fade(paper, std::min(1.0f, S.toastT)));
    if (G.moored && c.station < 0 && S.panel < 0) {
        int d = NearestDock(c.p, 1.4f);
        if (d >= 0) DrawTextCenteredBold(TextFormat("E: %s", DockStations()[d].name), SCREEN_W / 2.0f, SCREEN_H - 90.0f, 20, paper);
        else if (c.p.y > -3.0f && S.sess.phase == Phase::Dock) DrawTextCentered("Moored at the quay: the gangplank is amidships to port; the helm casts off", SCREEN_W / 2.0f, SCREEN_H - 60.0f, 14, Fade(paper, 0.7f));
    }
    if (S.panel < 0) StationOverlay();
    Panels(g);
}

void Draw(Game& g) {
    const Gannet& G = S.G;
    const Crew& c = G.crew[S.you];
    if (S.fp) {
        // first person: the same Gannet through the hand's eyes, the shared HUD over it, a crosshair to aim with
        S.cam = EyeCamera(G, S.you, S.eye);
        DrawTrawl3D(G, G.eco, S.sess, S.you, S.cam, S.ghostSee);
        if (c.dead) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{120, 170, 200, 255}, 0.08f));
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
    View v = MakeView(G, c.deck, inWheelhouse);
    if (c.dead) { v.ghost = true; v.ghostAt = c.p; v.ghostSee = S.ghostSee; v.lights.push_back({c.p, 3.0f, 0.4f}); }   // the ghost's own cold lantern
    S.view = v;
    BeginLayer(PixelRT());
    ClearBackground(Color{2, 4, 8, 255});
    DrawSea(G, v);
    DrawQuay(G, v);
    // the harbour line: a ring of buoys round the harbour mouth, green lamps seaward, red toward the island
    for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        Vector2 w = Vector2Add(S.sess.harbour, {cosf(a) * S.sess.harbourR, sinf(a) * S.sess.harbourR});
        if (S.eco.g && S.eco.DepthAt(w) < 1) continue;
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
    for (int i = 0; i < (int)G.crew.size(); i++) DrawCrewMember(G.crew[i], v, G.time, i == S.you);
    DrawLife(G, v, true);
    EndLayer();
    const float PX = (float)SCREEN_W / PIXEL_W;
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)}, {-PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE);
    Hud(g);
}
} // namespace

void StartTrawl(Game& g, bool firstPerson) {
    S = TrawlScene{};
    uint32_t seed = (uint32_t)GetRandomValue(1, 1 << 30);
    // a solo run: the Gannet at the quay on the atoll, the first deadline's quota on the tape
    S.sess.Begin(S.G, S.eco, 1, seed);
    S.active = true;
    S.fp = firstPerson;
    EnableCursor();
    g.scene = Scene::Trawl;
}

void SceneTrawl(Game& g) {
    if (!S.active) StartTrawl(g, false);
    SetPost(0.25f, 0.02f, 0.1f);
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 0.1f);
    if (!S.shot) {
        // first person takes the mouse to look with, except where a panel or the locker needs a pointer
        const Crew& me = S.G.crew[S.you];
        bool pointer = S.panel >= 0 || (me.station >= 0 && Stations()[me.station].kind == StationKind::Locker);
        bool lock = S.fp && !pointer;
        if (lock && !IsCursorHidden()) DisableCursor();
        if (!lock && IsCursorHidden()) EnableCursor();
        if (lock) {
            Vector2 md = GetMouseDelta();
            S.eye.yaw += md.x * 0.0025f;                      // right turns toward starboard when you face the bow
            S.eye.pitch = std::clamp(S.eye.pitch - md.y * 0.0025f, -1.35f, 1.25f);
            if (S.eye.yaw > PI) S.eye.yaw -= 2 * PI;
            if (S.eye.yaw < -PI) S.eye.yaw += 2 * PI;
        }
        Pressed(g);
    }
    S.acc += dt;
    while (S.acc >= 1 / 60.0f) {
        S.acc -= 1 / 60.0f;
        if (!S.shot) Controls(1 / 60.0f);
        S.G.Step(1 / 60.0f);
        S.sess.Step(1 / 60.0f);
    }
    if (S.toastT > 0) S.toastT -= dt;
    if (S.ghostSee > 0) S.ghostSee -= dt;
    if (S.tapeT > 0) S.tapeT -= dt;
    if (S.sess.phase == Phase::Over || S.sess.phase == Phase::Result) S.panel = PANEL_END;
    Draw(g);
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
        S.eye.yaw = which == 4 || which == 5 ? 1.25f : which == 15 ? 3.1f : which == 9 ? -1.9f : which == 16 ? 1.3f : which == 17 ? 1.9f : which == 1 ? -2.4f : which == 6 || which == 8 ? 2.6f : 0.0f;
    }
    if (which >= 15) {
        // 15 the net down and filling, 16 a rifle and a shot fish afloat with gulls over, 17 overboard and the ring,
        // 18 a ghost on deck, 19 the harpoon fast in a shark, 20 the deck locker
        Gannet& G = S.G; Session& ss = S.sess;
        G.crew.push_back(G.crew[0]); G.crew[1].slot = 1; G.crew[1].role = Role::Angler; G.crew[1].p = {-3, 1.2f};
        for (auto& sl : G.crew[1].slots) sl = Slot{};
        G.crew[0].p = {-1, 0.8f};
        ss.CastOff(); G.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 60, 5}); G.boat.heading = 0.1f;
        for (int i = 0; i < 20; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        S.eco.agentBudget = 260;
        for (int i = 0; i < 60 * 25; i++) { G.Step(1 / 60.0f); ss.Step(1 / 60.0f); }
        Crew& c = G.crew[0];
        if (which == 15) {
            int ws = (int)StationKind::NetWinch; c.p = Stations()[ws].at; c.station = ws;
            G.net.state = NetState::Down; G.net.depth = 5; G.boat.telegraph = 1; G.boat.pressure = 0.7f; G.boat.firebox = 6;
            for (int i = 0; i < 60 * 12; i++) {
                if (i % 90 == 0) { Vector2 w = G.boat.ToWorld({-30, 0}); int ai = S.eco.SpawnAgentPublic(Species().Find("sardine"), w); S.eco.agents[ai].count = 150; S.eco.agents[ai].p.z = G.net.depth; }
                if (G.boat.pressure < 0.6f) G.boat.Shovel(1);
                G.Step(1 / 60.0f);
            }
        }
        if (which == 16) {
            c.slots[3] = {Item::Rifle, 10}; c.sel = 3; c.p = {1, 2.5f};
            Floater f; f.name = "bonito"; f.sp = Species().Find("bonito"); f.kg = 4; f.price = 3; f.grade = 0.7f; f.p = G.boat.ToWorld({2, 7}); G.floaters.push_back(f);
            int gs = Species().Find("gull flock"); int ai = S.eco.SpawnAgentPublic(gs, G.boat.ToWorld({0, 0})); S.eco.agents[ai].count = 14; S.eco.agents[ai].p.z = -3;
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
            int sh = Species().Find("reef shark"); int ai = S.eco.SpawnAgentPublic(sh, G.boat.ToWorld({-8, -8})); S.eco.agents[ai].count = 1; S.eco.agents[ai].p.z = 2;
            S.ghostSee = 1;
        }
        if (which == 19) {
            G.harpoonCannon = true; G.harpoons = 2; G.explosives = 1;
            int hs = (int)StationKind::Harpoon; c.p = Stations()[hs].at; c.station = hs;
            int sh = Species().Find("reef shark"); Vector2 at = G.boat.ToWorld({20, 3});
            int ai = S.eco.SpawnAgentPublic(sh, at); S.eco.agents[ai].count = 1; S.eco.agents[ai].p = {at.x, at.y, 1}; S.eco.agents[ai].fedT = 100;
            for (int k = 0; k < 3 && G.harpoon.state != RodState::Fighting; k++) {
                for (auto& a : S.eco.agents) if (a.sp == sh) a.p = {at.x, at.y, 1};
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
        Gannet& G = S.G; Session& ss = S.sess;
        Crew& c = G.crew[0];
        if (which == 9 || which == 10 || which == 11) c.p = which == 11 ? Vector2{3.0f, -7.2f} : which == 10 ? Vector2{-3.5f, -7.2f} : Vector2{-6, -5.5f};
        if (which == 10) S.panel = (int)DockKind::Chandler;
        if (which == 11) {
            const char* names[] = {"snapper", "grunt", "reef squid", "bonito", "snapper (head)"};
            float kg[] = {3.2f, 1.1f, 0.7f, 4.1f, 1.2f}, pr[] = {3, 1.5f, 4, 3, 3};
            for (int i = 0; i < 5; i++) { CatchRec cr; cr.name = names[i]; cr.kg = kg[i]; cr.price = pr[i]; cr.gutted = i != 2; cr.iced = i != 2; cr.fresh = i == 2 ? 0.93f : 0.98f; cr.grade = i == 4 ? 0.9f : 1; cr.first = i == 0; G.hold.push_back(cr); }
            S.panel = (int)DockKind::Market;
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
    Gannet& G = S.G;
    G.Init(4, 11, which == 3 ? Weather::Squall : Weather::Calm);
    G.boat.telegraph = 1; G.boat.pressure = 0.7f;
    Crew& c = G.crew[0];
    G.crew[1].p = {-0.8f, 2.4f}; G.crew[1].station = NearestStation(G.crew[1].p, 0, 1.1f); G.crew[1].facing = {0, 1};
    G.crew[2].p = {-8.2f, 0.4f}; G.crew[2].facing = {-1, 0};
    G.crew[3].p = {-2.2f, 1.6f}; G.crew[3].facing = {0, 1};
    if (which == 1) { c.deck = 1; c.p = {-4.8f, 0.2f}; c.station = NearestStation(c.p, 1, 1.1f); G.boat.firebox = 5; G.boat.Hit(SEC_STERN_P, 75); for (int i = 0; i < 60 * 10; i++) G.Step(1 / 60.0f); }
    else if (which == 2) { c.p = {4.2f, 0}; c.station = NearestStation(c.p, 0, 1.1f); G.boat.rudder = 0.4f; }
    else if (which >= 6) {
        S.eco.Init("lagoon", 11); G.eco = &S.eco;
        G.boat.telegraph = 0; G.boat.pos = {S.eco.n * S.eco.cell * (which == 7 ? 0.36f : 0.42f), S.eco.n * S.eco.cell * 0.5f}; G.boat.heading = -0.3f;
        G.boat.lantern = which == 7 ? 3 : 2; G.boat.searchAim = 0.9f;
        c.p = {0.8f, 0.9f};
        if (which == 7) { c.p = {0.2f, 0.6f}; c.station = NearestStation({0.2f, 0}, 0, 1.1f); }
        int shark = Species().Find("reef shark");
        for (int i = 0; i < 60 * (which == 8 ? 100 : 70); i++) {
            if (which == 8 && i % (60 * 20) == 0) { Vector2 w = G.boat.ToWorld({-11, 0}); S.eco.AddBlood({w.x, w.y, 1}, 60); }
            if (which == 8 && i == 60 * 30) {
                int ai = S.eco.SpawnAgentPublic(shark, G.boat.ToWorld({-14, 9})); S.eco.agents[ai].hunger = 0.9f; S.eco.agents[ai].p.z = 1.2f; S.eco.agents[ai].count = 1;
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
