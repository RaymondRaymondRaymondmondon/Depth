// The Trawl's scene (design doc, "The session loop" and "Controls, HUD, and the sonar"): the Gannet at night seen
// from above, the hand you play walking her deck and working her stations. The simulation is trawl_boat.cpp (and,
// as the stages come, the lines, the fish and the web); this file feeds it input and draws it.
#include "trawl.h"
#include "trawl_art.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

using namespace tw;

namespace {
struct TrawlScene {
    bool active = false;
    Gannet G;
    int you = 0;
    float acc = 0;                 // the fixed 60 Hz step's accumulator
    bool shot = false;             // --shots: no input, fixed time
    int shotView = 0;
    float wheel = 0;               // the mouse wheel, gathered per frame for the next fixed step
    View view;                     // the last frame's view (the mouse's deck position)
};
TrawlScene S;

// the lights on deck: the lantern mast (its level), the wheelhouse's glow, the engine room's fire from below
View MakeView(const Gannet& g, int viewerDeck, bool inWheelhouse) {
    View v;
    v.center = {(PIXEL_W + 2) / 2.0f - 1.5f * v.ppm, (PIXEL_H + 2) / 2.0f + 0.5f * v.ppm};
    v.viewerDeck = viewerDeck; v.inWheelhouse = inWheelhouse;
    // she heels: from above the deck shifts a little toward the low side (the whole view rides with her)
    v.center.y += std::clamp(g.boat.RollDeg() / 5.0f, -4.0f, 4.0f);
    v.center.x -= std::clamp(g.boat.pitch * 57.3f / 5.0f, -3.0f, 3.0f);
    float lantern = 14;                                        // "full (14 m)"
    if (g.sea.weather == Weather::Fog) lantern *= 0.6f;        // "Fog: light radius -40%"
    v.lights.push_back({{0.2f, 0}, lantern, 1.0f});
    v.lights.push_back({{3.0f, 0}, 4.0f, 0.6f});               // the wheelhouse lamp through its windows
    v.lights.push_back({{-10.4f, 0}, 5.0f, 0.5f});             // the stern work lamp
    if (viewerDeck == 1) v.lights.push_back({{-4.8f, -0.9f}, 4.5f, 0.3f + 0.5f * std::clamp(g.boat.firebox / 6, 0.0f, 1.0f)});
    return v;
}

void Controls(float dt) {
    Gannet& g = S.G;
    Crew& c = g.crew[S.you];
    Vector2 wish{0, 0};
    // WASD is screen-relative on the deck (the bow is to the right of the screen)
    if (IsKeyDown(KEY_W)) wish.y -= 1;
    if (IsKeyDown(KEY_S)) wish.y += 1;
    if (IsKeyDown(KEY_A)) wish.x -= 1;
    if (IsKeyDown(KEY_D)) wish.x += 1;
    bool atHelm = c.station >= 0 && Stations()[c.station].kind == StationKind::Helm;
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
        const float PX = (float)SCREEN_W / PIXEL_W;
        Vector2 m = GetMousePosition();
        Vector2 aim = S.view.DeckOfCanvas({m.x / PX + 1, m.y / PX + 1});
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
    g.Primary(S.you, IsMouseButtonDown(MOUSE_BUTTON_LEFT), dt);
    g.Secondary(S.you, IsMouseButtonDown(MOUSE_BUTTON_RIGHT), dt);
}
void Pressed(Game& g) {
    Gannet& G = S.G;
    if (IsKeyPressed(KEY_E)) G.TakeStation(S.you);
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
            break;
        default:
            DrawTextCentered("(this station comes aboard in a later refit)", SCREEN_W / 2.0f, SCREEN_H - 64.0f, 14, Fade(paper, 0.6f));
            break;
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
    if (c.overboard) DrawTextCenteredBold("OVERBOARD", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 40, 34, Color{230, 80, 70, 255});
    if (G.boat.sunk) DrawTextCenteredBold("The Gannet has foundered", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 30, Color{230, 80, 70, 255});
    StationOverlay();
    (void)g;
}

void Draw(Game& g) {
    const Gannet& G = S.G;
    const Crew& c = G.crew[S.you];
    bool inWheelhouse = c.deck == 0 && c.p.x > 0.9f && c.p.x < 5.1f && fabsf(c.p.y) < 2.1f;
    View v = MakeView(G, c.deck, inWheelhouse);
    S.view = v;
    BeginLayer(PixelRT());
    ClearBackground(Color{2, 4, 8, 255});
    DrawSea(G, v);
    DrawBoat(G, v);
    DrawLines(G, v);
    for (int i = 0; i < (int)G.crew.size(); i++) DrawCrewMember(G.crew[i], v, G.time, i == S.you);
    EndLayer();
    const float PX = (float)SCREEN_W / PIXEL_W;
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)}, {-PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE);
    Hud(g);
}
} // namespace

void StartTrawl(Game& g) {
    S = TrawlScene{};
    S.G.Init(1, (uint32_t)GetRandomValue(1, 1 << 30), Weather::Calm);
    S.G.boat.telegraph = 0;
    S.active = true;
    EnableCursor();
    g.scene = Scene::Trawl;
}

void SceneTrawl(Game& g) {
    if (!S.active) StartTrawl(g);
    SetPost(0.25f, 0.02f, 0.1f);
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 0.1f);
    if (!S.shot) Pressed(g);
    S.acc += dt;
    while (S.acc >= 1 / 60.0f) {
        S.acc -= 1 / 60.0f;
        if (!S.shot) Controls(1 / 60.0f);
        S.G.Step(1 / 60.0f);
    }
    Draw(g);
}

// --shots: 0 the deck at night, 1 the engine room, 2 the wheelhouse, 3 a squall, 4 a fish on, 5 a marlin jumping
void DebugTrawlShot(Game& g, int which) {
    StartTrawl(g);
    S.shot = true;
    Gannet& G = S.G;
    G.Init(4, 11, which == 3 ? Weather::Squall : Weather::Calm);
    G.boat.telegraph = 1; G.boat.pressure = 0.7f;
    Crew& c = G.crew[0];
    G.crew[1].p = {-0.8f, 2.4f}; G.crew[1].station = NearestStation(G.crew[1].p, 0, 1.1f); G.crew[1].facing = {0, 1};
    G.crew[2].p = {-8.2f, 0.4f}; G.crew[2].facing = {-1, 0};
    G.crew[3].p = {-2.2f, 1.6f}; G.crew[3].facing = {0, 1};
    if (which == 1) { c.deck = 1; c.p = {-4.8f, 0.2f}; c.station = NearestStation(c.p, 1, 1.1f); G.boat.firebox = 5; G.boat.Hit(SEC_STERN_P, 75); for (int i = 0; i < 60 * 10; i++) G.Step(1 / 60.0f); }
    else if (which == 2) { c.p = {4.2f, 0}; c.station = NearestStation(c.p, 0, 1.1f); G.boat.rudder = 0.4f; }
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
