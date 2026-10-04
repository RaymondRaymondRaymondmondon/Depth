// ============================================================================
//  DEPTH - the game menu (Esc or the gear in the salon): pause, settings (volumes, brightness, fullscreen,
//  hints), controls (every action on two rebindable keys), a new game, quit. While it is open the game is
//  truly paused: the last frame is held behind it and nothing else updates.
// ============================================================================
#include "game.h"
#include "input.h"
#include "sound.h"
#include <algorithm>
#include <cmath>

namespace {
enum Page { P_MAIN, P_SETTINGS, P_CONTROLS, P_GRAPHICS, P_VOICE };
bool gOpen = false, gQuit = false;
Page gPage = P_MAIN;
int gRebind = -1, gRebindSlot = 0;   // the action (and which of its two keys) waiting for a key press
float gArmNew = 0, gOpenT = 0;
int gDragSlider = -1;

// the frame: a dark iron plate in a riveted brass border, a brass nameplate on top
void Frame(Rectangle r, const char* title) {
    DrawRectangleRounded({r.x + 6, r.y + 8, r.width, r.height}, 0.04f, 8, Fade(BLACK, 0.55f));
    DrawRectangleRounded(r, 0.04f, 8, Pal::BrassDk);
    Rectangle in{r.x + 8, r.y + 8, r.width - 16, r.height - 16};
    DrawRectangleRounded(in, 0.04f, 8, Color{22, 20, 18, 250});
    DrawTiled(Tex::Metal, {in.x + 6, in.y + 6, in.width - 12, in.height - 12}, 0.8f, Color{46, 44, 40, 255});
    DrawRectangleRec({in.x + 6, in.y + 6, in.width - 12, in.height - 12}, Fade(Color{12, 10, 9, 255}, 0.55f));
    DrawRectangleRoundedLinesEx(in, 0.04f, 8, 2, Pal::Brass);
    for (float x = r.x + 18; x < r.x + r.width - 10; x += 34) { DrawCircleV({x, r.y + 4}, 2.4f, Pal::Brass); DrawCircleV({x, r.y + r.height - 4}, 2.4f, Pal::Brass); }
    for (float y = r.y + 18; y < r.y + r.height - 10; y += 34) { DrawCircleV({r.x + 4, y}, 2.4f, Pal::Brass); DrawCircleV({r.x + r.width - 4, y}, 2.4f, Pal::Brass); }
    float pw = (float)MeasureTxt(title, 22, true) + 70;
    DrawBrassPlate({r.x + r.width / 2 - pw / 2, r.y - 20, pw, 40}, title, 22);
}

// a brass slider; returns the new value
float Slider(int id, Rectangle r, const char* label, float v, float lo, float hi, const char* shown) {
    Vector2 m = GetMousePosition();
    Txt(label, r.x, r.y - 2, 17, Pal::Paper);
    Rectangle track{r.x + 200, r.y + 6, r.width - 280, 8};
    DrawRectangleRounded({track.x - 2, track.y - 2, track.width + 4, track.height + 4}, 1, 6, Color{10, 8, 8, 255});
    float u = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    DrawRectangleRounded({track.x, track.y, track.width * u, track.height}, 1, 6, Pal::BrassDk);
    Vector2 knob{track.x + track.width * u, track.y + track.height / 2};
    bool hot = CheckCollisionPointCircle(m, knob, 14) || CheckCollisionPointRec(m, {track.x - 6, track.y - 10, track.width + 12, track.height + 20});
    if (hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { gDragSlider = id; PlayCue("ui.click", 0.6f); }
    if (gDragSlider == id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) v = lo + (hi - lo) * std::clamp((m.x - track.x) / track.width, 0.0f, 1.0f);
        else gDragSlider = -1;
        knob.x = track.x + track.width * std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    }
    DrawCircleV({knob.x + 1, knob.y + 2}, 11, Fade(BLACK, 0.5f));
    DrawCircleV(knob, 11, hot || gDragSlider == id ? Pal::Brass : Pal::BrassDk);
    DrawCircleV(knob, 6, Color{40, 30, 20, 255});
    Txt(shown, track.x + track.width + 20, r.y - 2, 17, Pal::Brass);
    return v;
}

bool Toggle(Rectangle r, const char* label, bool on) {
    Txt(label, r.x, r.y + 4, 17, Pal::Paper);
    Rectangle b{r.x + 200, r.y, 90, 30};
    if (Button(b, on ? "On" : "Off", true, 15)) { on = !on; }
    return on;
}

void MainPage(Game& g, Rectangle p) {
    float y = p.y + 50, x = p.x + p.width / 2 - 150;
    auto item = [&](const char* label, bool enabled = true) { bool r = Button({x, y, 300, 44}, label, enabled, 19); y += 54; return r; };
    if (item("Resume")) { gOpen = false; PlayCue("ui.confirm"); }   // (a first-person scene takes the pointer back itself)
    if (item("Settings")) { gPage = P_SETTINGS; }
    if (item("Controls")) { gPage = P_CONTROLS; }
    if (g.scene == Scene::RedTide || g.scene == Scene::Trawl || g.scene == Scene::Flight || g.scene == Scene::Mouthful || g.scene == Scene::Arcade) { if (item("Graphics (3D)")) gPage = P_GRAPHICS; }
    if (g.scene == Scene::RedTide || g.scene == Scene::Trawl || g.scene == Scene::Arcade) { if (item("Voice chat")) gPage = P_VOICE; }
    if (g.scene == Scene::RedTide || g.scene == Scene::Trawl) {
        if (item("Leave the match")) { gOpen = false; if (g.scene == Scene::Trawl) LeaveTrawlMatch(g); else LeaveRedTideMatch(g); }
    }
    if (g.scene == Scene::Flight && item("Leave the island")) { gOpen = false; LeaveFlight(g); }
    if (g.scene == Scene::Mouthful && item("Leave the reef")) { gOpen = false; LeaveMouthful(g); }
    if (g.scene == Scene::Platformer || g.scene == Scene::Abyss) {
        if (item("Abandon the dive")) {
            gOpen = false;
            if (g.scene == Scene::Platformer) { g.scene = Scene::Periscope; Toast(g, "Run abandoned."); }
            else g.scene = Scene::Helm;
        }
    }
    gArmNew = std::max(0.0f, gArmNew - GetFrameTime());
    if (item(gArmNew > 0 ? "Click again: wipe the save" : "Start a new game")) {
        if (gArmNew > 0) {
            DeleteSave();
            g = Game{};
            InitGame(g);
            ResetSalonLife();
            Toast(g, "A fresh crew steps aboard the Nautilus.");
            gOpen = false; gArmNew = 0;
        } else gArmNew = 3;
    }
    if (item("Save and quit")) {
        if (g.scene != Scene::Dungeon) SaveGame(g);
        gQuit = true;
    }
    if (g.scene == Scene::Dungeon) DrawTextCentered("Quitting mid-expedition keeps the last save from aboard.", p.x + p.width / 2, y + 2, 13, Color{170, 160, 140, 255});
}

void SettingsPage(Rectangle p) {
    AudioVolumes& V = Volumes();
    Settings& S = GameSettings();
    float x = p.x + 50, y = p.y + 60, w = p.width - 100;
    auto pct = [](float v) { return TextFormat("%d%%", (int)(v * 100 + 0.5f)); };
    TxtBold("Sound", x, y - 30, 18, Pal::Brass);
    V.master = Slider(0, {x, y, w, 30}, "Master volume", V.master, 0, 1, pct(V.master)); y += 42;
    V.music = Slider(1, {x, y, w, 30}, "Music", V.music, 0, 1, pct(V.music)); y += 42;
    V.sfx = Slider(2, {x, y, w, 30}, "Effects", V.sfx, 0, 1, pct(V.sfx)); y += 42;
    V.ambience = Slider(3, {x, y, w, 30}, "Ambience", V.ambience, 0, 1, pct(V.ambience)); y += 58;
    TxtBold("Picture", x, y - 26, 18, Pal::Brass);
    S.brightness = Slider(4, {x, y, w, 30}, "Brightness", S.brightness, 0.6f, 1.6f, pct(S.brightness)); y += 46;
    bool fs = Toggle({x, y, w, 30}, "Fullscreen (F11)", S.fullscreen);
    if (fs != S.fullscreen) { S.fullscreen = fs; ToggleBorderlessWindowed(); }
    y += 44;
    S.showHints = Toggle({x, y, w, 30}, "Hints and tips", S.showHints);
    // a strip from dark to light, to judge brightness by: the darkest steps should just be told apart
    for (int k = 0; k < 10; k++) { unsigned char v = (unsigned char)(k * 7 + 4); DrawRectangle((int)(x + 320 + k * 26), (int)y + 4, 24, 22, Color{v, v, v, 255}); }
    Txt("the two darkest squares should just differ", x + 320, y + 30, 12, Color{160, 150, 130, 255});
    y += 44;
    S.trawlOutline = Toggle({x, y, w, 30}, "Thin outlines (the Trawl)", S.trawlOutline);
}

// The 3D games' quality (the Visual Overhaul's phase 7): what each costs on this PC is in docs/TRAWL_PROGRESS.md
void GraphicsPage(Rectangle p) {
    Settings& S = GameSettings();
    float x = p.x + 50, y = p.y + 60, w = p.width - 100;
    float bx = 200, bw = 160;   // (the buttons' offset and width: narrower in Red Tide's two columns)
    auto choice = [&](const char* label, int v, int n, const char* const* names) {
        Txt(label, x, y + 4, 17, Pal::Paper);
        if (Button({x + bx, y, bw, 30}, names[v], true, 15)) v = (v + 1) % n;
        y += 44;
        return v;
    };
    static const char* SH[4] = {"Off", "Low", "Medium", "High"};
    static const char* FG[2] = {"Plain", "Lantern halos"};
    static const char* OF[2] = {"Off", "On"};
    TxtBold("The Trawl and Red Tide", x, y - 30, 18, Pal::Brass);
    S.gfxShadows = choice("Lamp shadows", S.gfxShadows, 4, SH);
    S.gfxAO = choice("Ambient occlusion", S.gfxAO ? 1 : 0, 2, OF) != 0;
    S.gfxFog = choice("Fog", S.gfxFog, 2, FG);
    S.gfxScale = (int)roundf(Slider(5, {x, y, w, 30}, "Resolution", (float)S.gfxScale, 50, 100, TextFormat("%d%%", S.gfxScale)) / 5) * 5;
    y += 54;
    static const char* INK[3] = {"Off", "Thin", "Full"};
    TxtBold("Red Tide", x, y, 16, Pal::Brass); y += 26;
    // (two columns: the look on the left; motion comfort, colour and the fog's calibration on the right)
    float y0 = y, x0 = x;
    bx = 150; bw = 140;
    S.rtInk = choice("Ink line", S.rtInk, 3, INK);
    S.rtStipple = choice("Stipple", S.rtStipple ? 1 : 0, 2, OF) != 0;
    S.rtLens = choice("Helmet lens", S.rtLens ? 1 : 0, 2, OF) != 0;
    x = x0 + w * 0.5f; y = y0;
    S.rtSway = choice("Viewmodel sway", S.rtSway ? 1 : 0, 2, OF) != 0;
    static const char* CB[2] = {"Off", "Blood in amber"};
    S.rtColorblind = choice("Colourblind", S.rtColorblind, 2, CB);
    S.rtFog = Slider(6, {x, y, w * 0.5f, 30}, "Fog density", S.rtFog, 0.6f, 1.4f, TextFormat("%d%%", (int)(S.rtFog * 100 + 0.5f)));
    {   // the calibration: a water-coloured strip fading into the fog: the far end should just merge
        for (int k = 0; k < 12; k++) { float f = 1 - expf(-k * 0.35f * S.rtFog); Color c = ColorLerp(Color{70, 120, 110, 255}, Color{18, 44, 50, 255}, f); DrawRectangle((int)(x + k * 18), (int)y + 36, 16, 18, c); }
        Txt("the last squares should just melt into the water", x, y + 58, 12, Color{160, 150, 130, 255});
    }
    x = x0; y = y0 + 3 * 44 + 40;
    Txt("If the deck stutters in rain and fog with a full crew, lower the lamp shadows first, then the resolution.", x, y, 13, Color{160, 150, 130, 255});
}

// Voice chat (the arcade): the microphone, push-to-talk or an open mic, the gate's sensitivity against a live meter,
// the voices' volume, and a mic test that plays you back as the table will hear you
void VoicePage(Rectangle p) {
    Settings& S = GameSettings();
    AudioVolumes& V = Volumes();
    float x = p.x + 50, y = p.y + 60, w = p.width - 100;
    TxtBold("Voice chat at the Deep Arcade's tables", x, y - 30, 18, Pal::Brass);
    S.voiceOn = Toggle({x, y, w, 30}, "Microphone", S.voiceOn); y += 44;
    Txt(S.voiceOpenMic ? "Open mic" : TextFormat("Push to talk (%s)", KeyLabel(ActKey(A_TALK, 0))), x, y + 4, 17, Pal::Paper);
    if (Button({x + 200, y, 200, 30}, S.voiceOpenMic ? "Use push to talk" : "Use an open mic", true, 15)) S.voiceOpenMic = !S.voiceOpenMic;
    y += 44;
    S.voiceSensitivity = Slider(7, {x, y, w, 30}, "Mic sensitivity", S.voiceSensitivity, 0, 1, TextFormat("%d%%", (int)(S.voiceSensitivity * 100 + 0.5f))); y += 30;
    {   // the live meter under it, with the gate's threshold marked: speak, and the bar should cross the mark
        Rectangle m{x + 200, y, w - 280, 10};
        float thr = 0.75f - 0.5f * S.voiceSensitivity, lv = VoiceMicLevel();
        DrawRectangleRec(m, Color{10, 8, 8, 255});
        DrawRectangleRec({m.x, m.y, m.width * lv, m.height}, lv >= thr ? Color{120, 220, 160, 255} : Pal::BrassDk);
        DrawRectangleRec({m.x + m.width * thr - 1, m.y - 4, 2, m.height + 8}, Pal::Paper);
        Txt(S.voiceOpenMic ? "the gate opens past the mark" : "(the mark matters for an open mic)", m.x, m.y + 14, 12, Color{170, 160, 140, 255});
    }
    y += 44;
    V.voice = Slider(8, {x, y, w, 30}, "Voices", V.voice, 0, 1, TextFormat("%d%%", (int)(V.voice * 100 + 0.5f))); y += 48;
    bool& test = VoiceMicTest();
    if (Button({x, y, 220, 34}, test ? "Stop the mic test" : "Test the mic", S.voiceOn, 15)) test = !test;
    Txt(test ? (S.voiceOpenMic ? "speak: you'll hear yourself as the table will" : TextFormat("hold %s and speak: you'll hear yourself", KeyLabel(ActKey(A_TALK, 0)))) : "", x + 240, y + 8, 14, Pal::Paper);
    y += 50;
    if (!VoiceMicError().empty()) Txt("The microphone: " + VoiceMicError() + " (check Windows' sound settings)", x, y, 14, Color{240, 140, 110, 255});
    else Txt(S.voiceOn ? "The microphone is only open at a table, or during the test. Push to talk is in Controls." : "Voice is off: you'll still hear the others.", x, y, 14, Color{170, 160, 140, 255});
}void ControlsPage(Rectangle p) {
    float x = p.x + 40, y = p.y + 50;
    TxtBold("Action", x, y - 26, 16, Pal::Brass);
    TxtBold("Key", x + 300, y - 26, 16, Pal::Brass);
    TxtBold("Alternate", x + 460, y - 26, 16, Pal::Brass);
    for (int a = 0; a < ACT_COUNT; a++) {
        Txt(ActName(a), x, y + 6, 17, Pal::Paper);
        for (int s = 0; s < 2; s++) {
            Rectangle b{x + 300 + s * 160.0f, y, 146, 32};
            bool waiting = gRebind == a && gRebindSlot == s;
            if (Button(b, waiting ? "press a key..." : KeyLabel(ActKey(a, s)), true, 15)) { gRebind = a; gRebindSlot = s; }
        }
        y += 40;
    }
    Txt("Click a key, then press the new one (Backspace clears it). Double-tap a direction to dash.", x, y + 6, 13, Color{170, 160, 140, 255});
    if (Button({p.x + p.width - 230, y + 30, 190, 34}, "Reset to defaults", true, 14)) ResetBindings();
    if (gRebind >= 0) {
        int k = GetKeyPressed();
        if (k == KEY_BACKSPACE) { ActKey(gRebind, gRebindSlot) = KEY_NULL; gRebind = -1; }
        else if (k != 0) {
            for (int a = 0; a < ACT_COUNT; a++) for (int s = 0; s < 2; s++) if (ActKey(a, s) == k) ActKey(a, s) = KEY_NULL; // a key does one thing
            ActKey(gRebind, gRebindSlot) = k;
            gRebind = -1;
            PlayCue("ui.confirm", 0.6f);
        }
    }
}
}  // namespace

bool GameMenuActive() { return gOpen; }
bool GameMenuWantsQuit() { return gQuit; }
void GameMenuOpen() {
    if (gOpen) return;
    EnableCursor();                               // first-person games capture the mouse; the menu needs it back
    SnapshotFrame();
    gOpen = true; gPage = P_MAIN; gRebind = -1; gOpenT = 0;
    PlayCue("hub.panel", 0.7f);
}

void GameMenuFrame(Game& g) {
    gOpenT += GetFrameTime();
    SetPost(0, 0, 0);
    SetPostBypass(true);                          // the held frame already went through the post pass
    DrawSnapshot();
    float a = std::min(1.0f, gOpenT / 0.18f);
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{4, 6, 8, 255}, 0.62f * a));
    float slide = (1 - a) * (1 - a) * 40;
    Rectangle p = gPage == P_MAIN ? Rectangle{SCREEN_W / 2.0f - 220, 110 + slide, 440, 470} : Rectangle{SCREEN_W / 2.0f - 380, 90 + slide, 760, 540};
    Frame(p, gPage == P_MAIN ? "Paused" : gPage == P_SETTINGS ? "Settings" : gPage == P_GRAPHICS ? "Graphics" : gPage == P_VOICE ? "Voice chat" : "Controls");
    switch (gPage) {
        case P_MAIN: MainPage(g, p); break;
        case P_SETTINGS: SettingsPage(p); break;
        case P_CONTROLS: ControlsPage(p); break;
        case P_GRAPHICS: GraphicsPage(p); break;
        case P_VOICE: VoicePage(p); break;
    }
    if (gPage != P_MAIN && Button({p.x + 30, p.y + p.height - 56, 140, 36}, "< Back", true, 15)) { gPage = P_MAIN; SaveSettings(); }
    // Esc (or the menu key): back a page, or close; never while waiting for a rebinding key
    if (gRebind < 0 && ActPressed(A_MENU) && gOpenT > 0.1f) {
        if (gPage != P_MAIN) { gPage = P_MAIN; SaveSettings(); }
        else { gOpen = false; SaveSettings(); PlayCue("ui.confirm", 0.5f); }
    }
    if (gPage != P_VOICE) VoiceMicTest() = false;   // (the test ends when you leave its page)
    if (!gOpen) SaveSettings();
}

// --shots: the menu on a given page over the salon
void DebugMenuPage(int page) { gOpen = true; gPage = (Page)page; gOpenT = 1; }
