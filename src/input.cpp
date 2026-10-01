// ============================================================================
//  DEPTH - actions, bindings and the settings file (see input.h).
// ============================================================================
#include "input.h"
#include "sound.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {
const int DEFAULTS[ACT_COUNT][2] = {
    {KEY_A, KEY_LEFT}, {KEY_D, KEY_RIGHT}, {KEY_W, KEY_UP}, {KEY_S, KEY_DOWN},
    {KEY_SPACE, KEY_NULL}, {KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT}, {KEY_TAB, KEY_NULL}, {KEY_ESCAPE, KEY_P},
};
int gKeys[ACT_COUNT][2];
bool gInit = false;
Settings gSettings;
void Init() { if (!gInit) { ResetBindings(); gInit = true; } }
const char* PATH = "settings.txt";
}  // namespace

const char* ActName(int a) {
    static const char* n[ACT_COUNT] = {"Move left", "Move right", "Up / climb", "Down / slide", "Jump", "Modifier (backflip, glide)", "Sonar scope", "Game menu"};
    return a >= 0 && a < ACT_COUNT ? n[a] : "?";
}
void ResetBindings() { for (int a = 0; a < ACT_COUNT; a++) for (int s = 0; s < 2; s++) gKeys[a][s] = DEFAULTS[a][s]; gInit = true; }
int& ActKey(int a, int slot) { Init(); return gKeys[a][slot & 1]; }
bool ActDown(int a) { Init(); return (gKeys[a][0] && IsKeyDown(gKeys[a][0])) || (gKeys[a][1] && IsKeyDown(gKeys[a][1])); }
bool ActPressed(int a) { Init(); return (gKeys[a][0] && IsKeyPressed(gKeys[a][0])) || (gKeys[a][1] && IsKeyPressed(gKeys[a][1])); }

// ---- first-person mouse look: a hidden pointer warped back to the middle of the window every frame
static bool gLooking = false, gLookAsked = false;
Vector2 MouseLook(bool on) {
    gLookAsked = gLookAsked || on;
    if (!on || !IsWindowFocused()) {
        if (gLooking) { EnableCursor(); gLooking = false; }
        return {0, 0};
    }
    Vector2 d{0, 0};
    // the first frame (or after something showed the pointer: the menu, a panel) only centres it: that jump isn't a look
    if (gLooking && IsCursorHidden()) d = GetMouseDelta();
    if (!IsCursorHidden()) HideCursor();
    gLooking = true;
    SetMousePosition(GetScreenWidth() / 2, GetScreenHeight() / 2);
    return d;
}
void MouseLookFrameEnd() {
    if (!gLookAsked && gLooking) { EnableCursor(); gLooking = false; }
    gLookAsked = false;
}

const char* KeyLabel(int k) {
    static char buf[16];
    if (k == KEY_NULL) return "-";
    if (k >= KEY_A && k <= KEY_Z) { snprintf(buf, sizeof buf, "%c", 'A' + (k - KEY_A)); return buf; }
    if (k >= KEY_ZERO && k <= KEY_NINE) { snprintf(buf, sizeof buf, "%c", '0' + (k - KEY_ZERO)); return buf; }
    if (k >= KEY_F1 && k <= KEY_F12) { snprintf(buf, sizeof buf, "F%d", 1 + k - KEY_F1); return buf; }
    switch (k) {
        case KEY_SPACE: return "Space"; case KEY_ESCAPE: return "Esc"; case KEY_ENTER: return "Enter"; case KEY_TAB: return "Tab";
        case KEY_BACKSPACE: return "Backspace"; case KEY_LEFT: return "Left"; case KEY_RIGHT: return "Right"; case KEY_UP: return "Up"; case KEY_DOWN: return "Down";
        case KEY_LEFT_SHIFT: return "Left shift"; case KEY_RIGHT_SHIFT: return "Right shift"; case KEY_LEFT_CONTROL: return "Left ctrl"; case KEY_RIGHT_CONTROL: return "Right ctrl";
        case KEY_LEFT_ALT: return "Left alt"; case KEY_RIGHT_ALT: return "Right alt"; case KEY_CAPS_LOCK: return "Caps lock";
        case KEY_COMMA: return ","; case KEY_PERIOD: return "."; case KEY_SLASH: return "/"; case KEY_SEMICOLON: return ";"; case KEY_APOSTROPHE: return "'";
        case KEY_LEFT_BRACKET: return "["; case KEY_RIGHT_BRACKET: return "]"; case KEY_MINUS: return "-"; case KEY_EQUAL: return "=";
        default: snprintf(buf, sizeof buf, "Key %d", k); return buf;
    }
}

Settings& GameSettings() { return gSettings; }

void SaveSettings() {
    Init();
    std::ofstream f(PATH);
    if (!f) return;
    AudioVolumes& V = Volumes();
    f << "audio " << V.master << " " << V.music << " " << V.sfx << " " << V.ambience << "\n";
    f << "brightness " << gSettings.brightness << "\n";
    f << "fullscreen " << (gSettings.fullscreen ? 1 : 0) << "\n";
    f << "hints " << (gSettings.showHints ? 1 : 0) << "\n";
    for (int a = 0; a < ACT_COUNT; a++) f << "bind " << a << " " << gKeys[a][0] << " " << gKeys[a][1] << "\n";
}

void LoadSettings() {
    Init();
    std::ifstream f(PATH);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key == "audio") { AudioVolumes& V = Volumes(); in >> V.master >> V.music >> V.sfx >> V.ambience; }
        else if (key == "brightness") in >> gSettings.brightness;
        else if (key == "fullscreen") { int v = 0; in >> v; gSettings.fullscreen = v != 0; }
        else if (key == "hints") { int v = 1; in >> v; gSettings.showHints = v != 0; }
        else if (key == "bind") { int a = -1, k0 = 0, k1 = 0; in >> a >> k0 >> k1; if (a >= 0 && a < ACT_COUNT) { gKeys[a][0] = k0; gKeys[a][1] = k1; } }
    }
    if (gSettings.brightness < 0.6f || gSettings.brightness > 1.6f) gSettings.brightness = 1.0f;
}
