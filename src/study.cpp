// ============================================================================
//  The Study, below the salon's hatch (Master Reference, "The Study"): the descent, the desk rail with its three
//  drawers (Courses, Soundscape, Scene), focus mode, the chronometer, study_save.txt, and --study-motion-audit.
//  No game economy: nothing here touches gold, XP, relics or tokens.
// ============================================================================
#include "input.h"
#include "game.h"
#include "sound.h"
#include "study.h"
#include "study_data.h"
#include "course.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace study {

// ---------------------------------------------------------------------------- the save file
static StudySave gSave;
StudySave& Save() { return gSave; }
std::string SavePath() { return std::string(GetApplicationDirectory()) + "study_save.txt"; }

static std::vector<std::string> Split(const std::string& s, char d) { std::vector<std::string> v; std::string cur; for (char ch : s) { if (ch == d) { v.push_back(cur); cur.clear(); } else cur += ch; } v.push_back(cur); return v; }
static std::string LayerLine(const LayerState& l) {
    std::ostringstream o;
    int mask = 0; for (int k = 0; k < MAX_INSTR; k++) mask |= l.music.instrOn[k] ? 1 << k : 0;
    o << l.kind << '|' << l.level << '|' << l.mute << '|' << l.solo << '|' << l.tone << '|' << l.width << '|' << l.pan;
    for (int p = 0; p < MAX_PARAMS; p++) o << '|' << l.p[p];
    o << '|' << l.music.style << '|' << l.music.key << '|' << l.music.minor << '|' << l.music.bpm << '|' << l.music.density << '|' << l.music.brightness << '|' << l.music.variation << '|' << mask << '|' << l.music.seed;
    return o.str();
}
static bool ParseLayer(const std::vector<std::string>& f, size_t at, LayerState& l) {
    if (f.size() < at + 20) return false;
    auto F = [&](size_t i) { return (float)atof(f[at + i].c_str()); };
    l = NewLayer(std::clamp(atoi(f[at].c_str()), 0, L_KIND_COUNT - 1));   // (a fresh id: ids are only for this run)
    l.level = F(1); l.mute = F(2) != 0; l.solo = F(3) != 0; l.tone = F(4); l.width = F(5); l.pan = F(6);
    for (int p = 0; p < MAX_PARAMS; p++) l.p[p] = F(7 + p);
    l.music.style = std::clamp((int)F(11), 0, M_STYLE_COUNT - 1); l.music.key = (int)F(12); l.music.minor = F(13) != 0; l.music.bpm = F(14);
    l.music.density = F(15); l.music.brightness = F(16); l.music.variation = F(17);
    int mask = (int)F(18); for (int k = 0; k < MAX_INSTR; k++) l.music.instrOn[k] = (mask >> k) & 1;
    l.music.seed = (uint32_t)strtoul(f[at + 19].c_str(), nullptr, 10);
    return true;
}
static bool gNoSave = false;   // (shots never touch the player's save)
bool SaveStudy() {
    if (gNoSave) return true;
    std::string tmp = SavePath() + ".tmp";
    {
        std::ofstream o(tmp);
        if (!o) return false;
        const StudySave& s = gSave;
        o << "study 1\n";
        o << "timer|" << s.timer.workMin << '|' << s.timer.breakMin << '|' << s.timer.longMin << '|' << s.timer.rounds << '|' << s.timer.hidden << '|' << s.timer.bell << '|' << s.timer.breakPreset << "\n";
        o << "scene|" << s.scene << '|' << s.focusDim << '|' << s.reduceMotion << '|' << s.lowPower << "\n";
        o << "course|" << s.course << "\n";
        const MasterState& m = s.mixer.master;
        o << "master|" << m.volume << '|' << m.lowCut << '|' << m.highCut << '|' << m.warmth << '|' << m.fadeMinutes << '|' << m.duckOnBreak << "\n";
        for (const LayerState& l : s.mixer.layers) o << "layer|" << LayerLine(l) << "\n";
        for (const UserPreset& p : s.presets) { o << "preset|" << p.name << '|' << p.layers.size() << "\n"; for (const LayerState& l : p.layers) o << "player|" << LayerLine(l) << "\n"; }
        for (auto& day : s.log) for (auto& c : day.second) o << "log|" << day.first << '|' << c.first << '|' << c.second << "\n";
    }
    std::remove(SavePath().c_str());
    return std::rename(tmp.c_str(), SavePath().c_str()) == 0;
}
bool LoadStudy() {
    std::ifstream in(SavePath());
    if (!in) return false;
    StudySave s;
    std::string line;
    UserPreset* cur = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto f = Split(line, '|');
        if (f.empty()) continue;
        const std::string& k = f[0];
        auto I = [&](size_t i, int d) { return i < f.size() ? atoi(f[i].c_str()) : d; };
        auto Fl = [&](size_t i, float d) { return i < f.size() ? (float)atof(f[i].c_str()) : d; };
        if (k == "timer") { s.timer.workMin = I(1, 25); s.timer.breakMin = I(2, 5); s.timer.longMin = I(3, 15); s.timer.rounds = I(4, 4); s.timer.hidden = I(5, 0) != 0; s.timer.bell = I(6, 1) != 0; s.timer.breakPreset = I(7, -1); }
        else if (k == "scene") { s.scene = std::clamp(I(1, SC_STUDY), 0, SC_COUNT - 1); s.focusDim = std::clamp(Fl(2, 0), 0.0f, Numbers().focusDimMax); s.reduceMotion = I(3, 0) != 0; s.lowPower = I(4, 0) != 0; }
        else if (k == "course" && f.size() > 1) s.course = f[1];
        else if (k == "master") { MasterState& m = s.mixer.master; m.volume = Fl(1, 0.8f); m.lowCut = Fl(2, 30); m.highCut = Fl(3, 16000); m.warmth = Fl(4, 0.2f); m.fadeMinutes = I(5, 0); m.duckOnBreak = I(6, 1) != 0; }
        else if (k == "layer") { LayerState l; if (ParseLayer(f, 1, l) && s.mixer.layers.size() < (size_t)MAX_LAYERS) s.mixer.layers.push_back(l); }
        else if (k == "preset" && f.size() > 1) { s.presets.push_back(UserPreset{f[1], {}}); cur = &s.presets.back(); }
        else if (k == "player" && cur) { LayerState l; if (ParseLayer(f, 1, l)) cur->layers.push_back(l); }
        else if (k == "log" && f.size() >= 4) s.log[f[1]][f[2]] += atof(f[3].c_str());
    }
    gSave = s;
    return true;
}

namespace {
// ---------------------------------------------------------------------------- the room's live state
const Color INK_ON{255, 214, 150, 255}, DESK{64, 38, 24, 255}, DESK_DK{40, 24, 14, 255}, TXT{232, 220, 196, 255}, TXT_DIM{170, 156, 132, 255};
enum Drawer { DR_NONE, DR_COURSES, DR_SOUND, DR_SCENE };
enum Phase { PH_IDLE, PH_WORK, PH_BREAK, PH_LONG };
struct Room {
    bool loaded = false;
    double lastFrame = -10;
    float descent = 99;                     // seconds since the descent began (>= descentSeconds: done)
    int drawer = DR_NONE; float drawerAnim = 0;
    float uiAlpha = 1, idle = 0; Vector2 lastMouse{-1, -1};
    float sceneT = 0;                       // the scene's own clock
    // the chronometer
    int phase = PH_IDLE; bool running = false; float left = 25 * 60; int round = 0; bool chronoSettings = false;
    bool breakSwapped = false; MixerState beforeBreak;
    // the desk
    bool picker = false; int active = -1; float dragStart = 0; std::string presetName; bool nameFocus = false;
    int offerPreset = -1;                   // the scene drawer offers the new scene's matching preset
    int debugShot = -1;
} R;

std::string Today() { time_t t = time(nullptr); tm lt{}; localtime_s(&lt, &t); char b[16]; strftime(b, sizeof b, "%Y-%m-%d", &lt); return b; }
MixerState& Desk() { return gSave.mixer; }
void Push() { study::SetState(gSave.mixer); }
void FreshIds(std::vector<LayerState>& v) { for (auto& l : v) { LayerState n = NewLayer(l.kind); l.id = n.id; } }
void ApplyLayers(const std::vector<LayerState>& ls) { Desk().layers = ls; FreshIds(Desk().layers); Push(); }
void ApplyPreset(int i) { const Preset& p = GetPreset(i); std::vector<LayerState> v(p.layers, p.layers + p.n); ApplyLayers(v); }
int PresetIndex(const char* name) { for (int i = 0; i < PresetCount(); i++) if (strcmp(GetPreset(i).name, name) == 0) return i; return -1; }
int MatchingPreset(int scene) { return PresetIndex(scene == SC_LOUNGE ? "Late lounge" : "Fireside"); }

// ---------------------------------------------------------------------------- small brass controls
bool Hover(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
void Label(const std::string& s, float x, float y, int size, Color c) { Txt(s, x, y, size, c); }
// a horizontal slider; returns true while it changes
bool HSlider(int id, Rectangle r, float& v, float lo, float hi, const char* label, const char* fmt = "%.2f", float shown = -1e9f) {
    bool changed = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover({r.x - 4, r.y - 6, r.width + 8, r.height + 12})) R.active = id;
    if (R.active == id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { float nv = lo + std::clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f) * (hi - lo); if (nv != v) { v = nv; changed = true; } }
        else R.active = -1;
    }
    float u = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    DrawRectangleRounded({r.x, r.y + r.height / 2 - 2, r.width, 4}, 1, 4, DESK_DK);
    DrawRectangleRounded({r.x, r.y + r.height / 2 - 2, r.width * u, 4}, 1, 4, Tone(Pal::Brass, -0.2f));
    DrawCircleV({r.x + r.width * u, r.y + r.height / 2}, R.active == id || Hover(r) ? 6.5f : 5.5f, Pal::Brass);
    DrawCircleLinesV({r.x + r.width * u, r.y + r.height / 2}, 6, DESK_DK);
    if (label) Label(label, r.x, r.y - 11, 11, TXT_DIM);
    if (fmt) { std::string s = TextFormat(fmt, shown > -1e8f ? shown : v); Label(s, r.x + r.width - MeasureTxt(s, 11), r.y - 11, 11, TXT); }
    return changed;
}
bool VFader(int id, Rectangle r, float& v) {
    bool changed = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover({r.x - 10, r.y - 6, r.width + 20, r.height + 12})) R.active = id;
    if (R.active == id) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { float nv = 1 - std::clamp((GetMousePosition().y - r.y) / r.height, 0.0f, 1.0f); if (nv != v) { v = nv; changed = true; } }
        else R.active = -1;
    }
    DrawRectangleRounded({r.x + r.width / 2 - 3, r.y, 6, r.height}, 1, 4, DESK_DK);
    for (int k = 0; k <= 10; k++) DrawLine((int)r.x, (int)(r.y + r.height * k / 10), (int)(r.x + 5), (int)(r.y + r.height * k / 10), Fade(TXT_DIM, 0.5f));
    float y = r.y + r.height * (1 - v);
    DrawRectangleRounded({r.x, y - 7, r.width, 14}, 0.3f, 4, Pal::Brass);
    DrawLine((int)r.x + 3, (int)y, (int)(r.x + r.width - 3), (int)y, DESK_DK);
    return changed;
}
bool Toggle(Rectangle r, bool& v, const char* label) {
    bool click = Hover(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (click) { v = !v; PlayCue("ui.click"); }
    DrawRectangleRounded(r, 0.3f, 4, v ? Tone(Pal::Brass, -0.1f) : DESK_DK);
    DrawRectangleRoundedLinesEx(r, 0.3f, 4, 1.2f, Hover(r) ? INK_ON : Tone(Pal::Brass, -0.4f));
    int fs = std::min(13, (int)r.height - 6);
    DrawTextCentered(label, r.x + r.width / 2, r.y + (r.height - fs) / 2 - 1, fs, v ? DESK_DK : TXT);
    return click;
}
bool Chip(Rectangle r, const char* label, bool lit = false) {
    bool click = Hover(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (click) PlayCue("ui.click");
    DrawRectangleRounded(r, 0.3f, 4, lit ? Tone(Pal::Brass, -0.1f) : Hover(r) ? Color{84, 56, 36, 255} : DESK_DK);
    DrawRectangleRoundedLinesEx(r, 0.3f, 4, 1.2f, Tone(Pal::Brass, lit ? 0.1f : -0.4f));
    int fs = std::min(14, (int)r.height - 6);
    while (fs > 9 && MeasureTxt(label, fs) > r.width - 8) fs--;
    DrawTextCentered(label, r.x + r.width / 2, r.y + (r.height - fs) / 2 - 1, fs, lit ? DESK_DK : TXT);
    return click;
}
bool TextBox(Rectangle r, std::string& s, bool& focus, size_t maxLen, const char* hint) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) focus = Hover(r);
    DrawRectangleRounded(r, 0.25f, 4, DESK_DK);
    DrawRectangleRoundedLinesEx(r, 0.25f, 4, 1.2f, focus ? INK_ON : Tone(Pal::Brass, -0.4f));
    bool enter = false;
    if (focus) {
        NoteTyping(); for (int c = GetCharPressed(); c; c = GetCharPressed()) if (c >= 32 && c < 127 && c != '|' && s.size() < maxLen) s += (char)c;
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !s.empty()) s.pop_back();
        enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    }
    Txt(s.empty() && !focus ? hint : s + (focus && fmodf((float)GetTime(), 1) < 0.5f ? "_" : ""), r.x + 8, r.y + (r.height - 14) / 2, 14, s.empty() && !focus ? TXT_DIM : TXT);
    return enter;
}
void Plate(Rectangle r) {   // a drawer's panel: dark wood, brass edge, a darker band behind it so text sits on a quiet area
    DrawRectangleGradientV(0, (int)r.y - 40, SCREEN_W, 40, Fade(BLACK, 0), Fade(BLACK, 0.55f));
    DrawRectangle(0, (int)r.y, SCREEN_W, (int)(SCREEN_H - r.y), Fade(BLACK, 0.55f));
    DrawRectangleRounded(r, 0.03f, 6, Fade(Color{36, 22, 14, 255}, 0.96f));
    DrawRectangleRoundedLinesEx(r, 0.03f, 6, 2, Tone(Pal::Brass, -0.25f));
}

// ---------------------------------------------------------------------------- the Soundscape drawer
const char* FamilyName(int f) { return f == F_NOISE ? "Noise" : f == F_NATURE ? "Nature" : "Music"; }
Color FamilyColor(int f) { return f == F_NOISE ? Color{120, 140, 150, 255} : f == F_NATURE ? Color{90, 150, 100, 255} : Color{190, 130, 80, 255}; }
void Strip(int slot, LayerState& l, Rectangle r, bool& removeMe) {
    int base = 1000 + slot * 100;
    const LayerDef& d = Layer(l.kind);
    DrawRectangleRounded(r, 0.04f, 4, Fade(Color{26, 16, 10, 255}, 0.9f));
    DrawRectangle((int)r.x + 4, (int)r.y + 4, (int)r.width - 8, 4, FamilyColor(d.family));
    std::string name = l.kind == L_MUSIC ? Style(l.music.style).name : d.name;
    int fs = 14; while (fs > 10 && MeasureTxt(name, fs, true) > r.width - 22) fs--;
    TxtBold(name, r.x + 6, r.y + 12, fs, TXT);
    if (Chip({r.x + r.width - 18, r.y + 11, 14, 14}, "x")) removeMe = true;
    // the fader, mute and solo
    VFader(base + 1, {r.x + 8, r.y + 36, 18, 120}, l.level);
    Label(TextFormat("%d", (int)lroundf(l.level * 100)), r.x + 6, r.y + 160, 11, TXT_DIM);
    Toggle({r.x + 32, r.y + 38, 24, 20}, l.mute, "M");
    Toggle({r.x + 32, r.y + 62, 24, 20}, l.solo, "S");
    // tone, width, pan
    float cx = r.x + 8, cw = r.width - 16, y = r.y + 186;
    HSlider(base + 2, {cx, y, cw, 12}, l.tone, -1, 1, "tone", "%+.1f"); y += 26;
    HSlider(base + 3, {cx, y, cw, 12}, l.width, 0, 1, "width", "%.1f"); y += 26;
    HSlider(base + 4, {cx, y, cw, 12}, l.pan, -1, 1, "pan", "%+.1f"); y += 30;
    if (l.kind != L_MUSIC) {
        for (int p = 0; p < d.nParams; p++) {
            const ParamDef& pd = d.p[p];
            if (pd.choices) {
                auto opts = Split(pd.choices, '|');
                int idx = std::clamp((int)lroundf(l.p[p]), 0, (int)opts.size() - 1);
                Label(pd.name, cx, y - 11, 11, TXT_DIM);
                if (Chip({cx, y - 1, cw, 18}, opts[idx].c_str())) l.p[p] = (float)((idx + 1) % opts.size());
                y += 30;
            } else { HSlider(base + 10 + p, {cx, y, cw, 12}, l.p[p], pd.lo, pd.hi, pd.name, pd.hi > 10 ? "%.0f" : "%.2f"); y += 26; }
        }
    } else {
        MusicSettings& m = l.music;
        static const char* KEYS[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
        if (Chip({cx, y - 4, cw * 0.6f - 2, 18}, KEYS[((m.key % 12) + 12) % 12])) m.key = (m.key + 1) % 12;
        if (Chip({cx + cw * 0.6f, y - 4, cw * 0.4f, 18}, m.minor ? "min" : "maj")) m.minor = !m.minor;
        y += 26;
        const StyleDef& st = Style(m.style);
        HSlider(base + 20, {cx, y, cw, 12}, m.bpm, st.bpmLo, st.bpmHi, "tempo", "%.0f"); y += 26;
        HSlider(base + 21, {cx, y, cw, 12}, m.density, 0, 1, "density", "%.2f"); y += 26;
        HSlider(base + 22, {cx, y, cw, 12}, m.brightness, 0, 1, "bright", "%.2f"); y += 26;
        HSlider(base + 23, {cx, y, cw, 12}, m.variation, 0, 1, "variation", "%.2f"); y += 22;
        for (int k = 0; k < st.nInstr; k++) { Toggle({cx, y, cw, 15}, m.instrOn[k], st.instr[k]); y += 17; }
        if (Chip({cx, y + 2, cw, 17}, TextFormat("seed %u", m.seed % 10000))) m.seed = (uint32_t)GetRandomValue(1, 1 << 30);
    }
}
void SoundDrawer(Rectangle p) {
    MixerState& M = Desk();
    // presets: the Study's eight, then the player's own, and a box to save the desk as one
    TxtBold("PRESETS", p.x + 14, p.y + 10, 15, INK_ON);
    float y = p.y + 32;
    for (int i = 0; i < PresetCount(); i++, y += 24) if (Chip({p.x + 12, y, 176, 21}, GetPreset(i).name)) ApplyPreset(i);
    for (size_t i = 0; i < gSave.presets.size() && y < p.y + p.height - 70; i++, y += 24) {
        if (Chip({p.x + 12, y, 152, 21}, gSave.presets[i].name.c_str())) ApplyLayers(gSave.presets[i].layers);
        if (Chip({p.x + 168, y, 20, 21}, "x")) { gSave.presets.erase(gSave.presets.begin() + i); SaveStudy(); break; }
    }
    bool enter = TextBox({p.x + 12, p.y + p.height - 62, 176, 24}, R.presetName, R.nameFocus, 24, "name this mix...");
    if ((Chip({p.x + 12, p.y + p.height - 32, 176, 22}, "Save as a preset") || enter) && !R.presetName.empty() && !M.layers.empty()) {
        gSave.presets.push_back({R.presetName, M.layers}); R.presetName.clear(); R.nameFocus = false; SaveStudy(); PlayCue("ui.confirm");
    }
    // the strips
    float sx = p.x + 200, sw = 104;
    int remove = -1;
    for (int i = 0; i < (int)M.layers.size(); i++) { bool rm = false; Strip(i, M.layers[i], {sx + i * (sw + 4), p.y + 8, sw, p.height - 16}, rm); if (rm) remove = i; }
    if (remove >= 0) { M.layers.erase(M.layers.begin() + remove); Push(); }
    if ((int)M.layers.size() < MAX_LAYERS) {
        Rectangle add{sx + M.layers.size() * (sw + 4), p.y + 8, sw, 60};
        if (Chip(add, "+ add a layer")) R.picker = !R.picker;
    }
    // the master section
    Rectangle mr{p.x + p.width - 196, p.y + 8, 186, p.height - 16};
    DrawRectangleRounded(mr, 0.04f, 4, Fade(Color{26, 16, 10, 255}, 0.9f));
    TxtBold("MASTER", mr.x + 10, mr.y + 8, 15, INK_ON);
    MasterState& m = M.master;
    float my = mr.y + 44, mx = mr.x + 12, mw = mr.width - 24;
    HSlider(3001, {mx, my, mw, 12}, m.volume, 0, 1, "volume", "%.2f"); my += 30;
    HSlider(3002, {mx, my, mw, 12}, m.lowCut, 20, 400, "low cut", "%.0f Hz"); my += 30;
    HSlider(3003, {mx, my, mw, 12}, m.highCut, 2000, 20000, "high cut", "%.0f Hz"); my += 30;
    HSlider(3004, {mx, my, mw, 12}, m.warmth, 0, 1, "warmth", "%.2f"); my += 26;
    Label("fade out over", mx, my, 11, TXT_DIM); my += 14;
    const StudyNumbers& N = Numbers();
    for (int k = 0; k < 5; k++) { int mins = N.fadeChoices[k]; if (Chip({mx + k * (mw / 5), my, mw / 5 - 3, 20}, mins ? TextFormat("%d", mins) : "off", m.fadeMinutes == mins)) { m.fadeMinutes = mins; RestartFade(); } }
    my += 26;
    if (m.fadeMinutes) { DrawBar({mx, my, mw, 5}, 1 - FadeProgress(), Tone(Pal::Brass, -0.1f)); if (Chip({mx, my + 9, mw, 18}, "restart the fade")) RestartFade(); my += 30; }
    Toggle({mx, my, mw, 20}, m.duckOnBreak, "duck music on breaks"); my += 26;
    Label("limiter always on", mx, my, 11, TXT_DIM);
    Push();
    // the layer picker
    if (R.picker) {
        Rectangle pk{sx + 20, p.y - 150, 560, 140};
        DrawRectangleRounded(pk, 0.05f, 6, Color{30, 18, 12, 250});
        DrawRectangleRoundedLinesEx(pk, 0.05f, 6, 2, Pal::Brass);
        float px = pk.x + 12, py = pk.y + 10;
        for (int fam = 0; fam < 3; fam++) {
            TxtBold(FamilyName(fam), px, py, 13, FamilyColor(fam));
            float bx = px + 64;
            for (int k = 0; k < L_KIND_COUNT; k++) {
                if (Layer(k).family != fam || k == L_MUSIC) continue;
                if (Chip({bx, py - 2, 68, 20}, Layer(k).name)) { M.layers.push_back(NewLayer(k)); R.picker = false; Push(); }
                bx += 72;
                if (bx > pk.x + pk.width - 70) { bx = px + 64; py += 24; }
            }
            if (fam == F_MUSIC) for (int s = 0; s < M_STYLE_COUNT; s++) {
                if (Chip({bx, py - 2, 100, 20}, Style(s).name)) { LayerState l = NewLayer(L_MUSIC); l.music.style = s; l.music.bpm = Style(s).bpmDef; l.music.density = Style(s).density; l.music.seed = (uint32_t)GetRandomValue(1, 1 << 30); M.layers.push_back(l); R.picker = false; Push(); }
                bx += 104;
            }
            py += 30;
        }
    }
}

// ---------------------------------------------------------------------------- the Scene drawer
void SceneDrawer(Rectangle p) {
    TxtBold("SCENE", p.x + 14, p.y + 10, 15, INK_ON);
    for (int s = 0; s < SC_COUNT; s++) {
        Rectangle b{p.x + 14 + s * 250.0f, p.y + 36, 236, 44};
        if (Chip(b, SceneName(s), gSave.scene == s) && gSave.scene != s) { gSave.scene = s; R.offerPreset = MatchingPreset(s); SaveStudy(); }
    }
    if (R.offerPreset >= 0) {
        Label(TextFormat("Play its matching mix, \"%s\"?", GetPreset(R.offerPreset).name), p.x + 530, p.y + 40, 15, TXT);
        if (Chip({p.x + 530, p.y + 62, 90, 22}, "Yes")) { ApplyPreset(R.offerPreset); R.offerPreset = -1; SaveStudy(); }
        if (Chip({p.x + 626, p.y + 62, 110, 22}, "No, thanks")) R.offerPreset = -1;
    }
    const StudyNumbers& N = Numbers();
    float pct = gSave.focusDim * 100;
    if (HSlider(4001, {p.x + 20, p.y + 122, 380, 14}, pct, 0, N.focusDimMax * 100, "Focus Dim: darker, calmer, slower", "%.0f%%")) gSave.focusDim = pct / 100;
    Toggle({p.x + 440, p.y + 112, 220, 26}, gSave.reduceMotion, "Reduce Motion");
    Toggle({p.x + 680, p.y + 112, 220, 26}, gSave.lowPower, "Low power (30 fps)");
    DrawWrapped("Reduce Motion stills everything but the fire and one sleeping or breathing figure. Focus Dim slows idle motion to as little as 30% speed.", {p.x + 20, p.y + 160, 880, 40}, 13, TXT_DIM);
    if (R.active < 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) SaveStudy();
}

// ---------------------------------------------------------------------------- the Courses drawer
// the packs on disk, re-read every few seconds while the drawer is open (a pack is rebuilt by Claude Code outside the game)
const std::vector<Course>& Courses() {
    static std::vector<Course> cache; static double at = -100;
    if (GetTime() - at > 4) { cache = LoadCourses(FindCoursesRoot()); at = GetTime(); }
    return cache;
}
int gCourseUnit = -1;   // the unit whose skills are shown
void CoursesDrawer(Rectangle p) {
    TxtBold("COURSES", p.x + 14, p.y + 10, 15, INK_ON);
    const auto& cs = Courses();
    float y = p.y + 40;
    std::string term = cs.empty() ? "" : cs.front().term;   // the newest term is "this term"; older ones are the Past Terms shelf
    Label("This term", p.x + 16, y, 13, TXT_DIM); y += 18;
    if (cs.empty()) {
        DrawWrapped("No course packs yet. MATH 1272, Calculus II (Fall 2026) comes first: Claude Code builds its pack from your class materials into study/courses/, and it appears here with its units and skills.", {p.x + 16, y, 560, 60}, 14, TXT);
        y += 64;
    }
    if (Chip({p.x + 16, y, 180, 24}, "General study", gSave.course == "General")) { gSave.course = "General"; gCourseUnit = -1; SaveStudy(); }
    y += 28;
    const Course* sel = nullptr;
    bool past = false;
    for (auto& c : cs) {
        if (!past && c.term != term) { past = true; y += 4; Label("Past terms", p.x + 16, y, 13, TXT_DIM); y += 18; }
        std::string name = c.code.empty() ? c.id : c.code + "  " + c.title;
        if (Chip({p.x + 16, y, 250, 24}, name.c_str(), gSave.course == c.id)) { gSave.course = c.id; gCourseUnit = -1; SaveStudy(); }
        if (gSave.course == c.id) sel = &c;
        y += 28;
    }
    // the selected course: its units in order, built ones lit; click a unit to see its skills
    if (sel) {
        float ux = p.x + 280, uy = p.y + 36, uw = 326;
        TxtBold(TextFormat("%s, %s", sel->title.c_str(), sel->term.c_str()), ux, p.y + 12, 14, TXT);
        int built = 0; for (auto& u : sel->units) if (u.status == "built") built++;
        Label(TextFormat("%d of %d units ready", built, (int)sel->units.size()), ux + uw - 120, p.y + 13, 12, TXT_DIM);
        float rowH = std::min(19.0f, (p.height - 46) / std::max(1, (int)sel->units.size()));
        for (int i = 0; i < (int)sel->units.size(); i++) {
            const CourseUnit& u = sel->units[i];
            Rectangle r{ux, uy + i * rowH, uw, rowH - 2};
            bool ready = u.status == "built", on = gCourseUnit == i, hov = Hover(r);
            DrawRectangleRounded(r, 0.2f, 4, Fade(on ? Color{110, 70, 30, 255} : Color{40, 26, 16, 255}, hov ? 1.0f : 0.8f));
            DrawCircleV({r.x + 8, r.y + r.height / 2}, 3, ready ? Color{120, 200, 120, 255} : Color{110, 96, 80, 255});
            std::string label = u.id + "  " + u.title;
            int fs = 12; while (fs > 9 && MeasureTxt(label, fs, false) > uw - 70) fs--;
            Label(label, r.x + 16, r.y + (r.height - fs) / 2 - 1, fs, ready ? TXT : TXT_DIM);
            if (ready) Label(TextFormat("%d items", u.items), r.x + uw - 56, r.y + (r.height - 11) / 2 - 1, 11, TXT_DIM);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gCourseUnit = on ? -1 : i;
        }
        // the skills of the chosen unit, over the session log
        if (gCourseUnit >= 0 && gCourseUnit < (int)sel->units.size()) {
            const CourseUnit& u = sel->units[gCourseUnit];
            Rectangle sk{p.x + 620, p.y + 10, p.width - 634, p.height - 20};
            DrawRectangleRounded(sk, 0.04f, 4, Fade(Color{26, 16, 10, 255}, 0.96f));
            TxtBold(TextFormat("%s SKILLS", u.id.c_str()), sk.x + 10, sk.y + 8, 14, INK_ON);
            float sy = sk.y + 32;
            if (u.skills.empty()) Label(u.status == "built" ? "No skills listed." : "Not built yet: Claude Code ingests one unit per session.", sk.x + 10, sy, 13, TXT_DIM);
            for (auto& s : u.skills) {
                if (sy > sk.y + sk.height - 20) { Label("...", sk.x + 10, sy, 13, TXT_DIM); break; }
                DrawWrapped(("- " + s.name).c_str(), {sk.x + 10, sy, sk.width - 20, 34}, 13, TXT);
                sy += MeasureTxt(s.name, 13, false) > sk.width - 30 ? 32 : 17;
            }
            return;
        }
    }
    y = std::max(y, p.y + 40);    // the session log: focus time today and over the last 7 days, per course
    Rectangle lg{p.x + 620, p.y + 10, p.width - 634, p.height - 20};
    DrawRectangleRounded(lg, 0.04f, 4, Fade(Color{26, 16, 10, 255}, 0.9f));
    TxtBold("SESSION LOG", lg.x + 10, lg.y + 8, 14, INK_ON);
    std::string today = Today();
    std::map<std::string, double> week;
    time_t now = time(nullptr);
    for (int d = 0; d < 7; d++) { time_t tt = now - d * 86400; tm lt{}; localtime_s(&lt, &tt); char b[16]; strftime(b, sizeof b, "%Y-%m-%d", &lt); auto it = gSave.log.find(b); if (it != gSave.log.end()) for (auto& c : it->second) week[c.first] += c.second; }
    double todayTotal = 0; auto it = gSave.log.find(today); if (it != gSave.log.end()) for (auto& c : it->second) todayTotal += c.second;
    Label(TextFormat("Focus today: %d h %02d min", (int)(todayTotal / 3600), (int)fmod(todayTotal / 60, 60)), lg.x + 10, lg.y + 32, 14, TXT);
    float ly = lg.y + 56;
    Label("The last 7 days", lg.x + 10, ly, 12, TXT_DIM); ly += 18;
    if (week.empty()) Label("Nothing logged yet: start the chronometer.", lg.x + 10, ly, 13, TXT_DIM);
    for (auto& c : week) { Label(TextFormat("%s   %d h %02d min", c.first.c_str(), (int)(c.second / 3600), (int)fmod(c.second / 60, 60)), lg.x + 10, ly, 13, TXT); ly += 18; }
}

// ---------------------------------------------------------------------------- the chronometer
float PhaseLength(int ph) { const TimerSettings& T = gSave.timer; return 60.0f * (ph == PH_BREAK ? T.breakMin : ph == PH_LONG ? T.longMin : T.workMin); }
void EnterPhase(int ph) {
    const TimerSettings& T = gSave.timer;
    bool wasBreak = R.phase == PH_BREAK || R.phase == PH_LONG, isBreak = ph == PH_BREAK || ph == PH_LONG;
    R.phase = ph; R.left = PhaseLength(ph);
    if (T.bell && R.debugShot < 0) PlayCue("study.bell");
    if (isBreak && !wasBreak) {
        if (T.breakPreset >= 0 && T.breakPreset < PresetCount()) { R.beforeBreak = Desk(); R.breakSwapped = true; ApplyPreset(T.breakPreset); }
        else study::SetBreak(true);
    } else if (!isBreak && wasBreak) {
        if (R.breakSwapped) { Desk() = R.beforeBreak; R.breakSwapped = false; Push(); }
        study::SetBreak(false);
    }
}
void TickChrono(float dt) {
    if (!R.running || R.phase == PH_IDLE) return;
    R.left -= dt;
    if (R.phase == PH_WORK) gSave.log[Today()][gSave.course] += dt;   // the session log counts focus time only
    if (R.left <= 0) {
        if (R.phase == PH_WORK) { R.round++; EnterPhase(R.round % std::max(1, gSave.timer.rounds) == 0 ? PH_LONG : PH_BREAK); }
        else EnterPhase(PH_WORK);
        SaveStudy();
    }
}
void Chronometer() {
    TimerSettings& T = gSave.timer;
    if (T.hidden) { if (Chip({SCREEN_W - 40.0f, 16, 24, 24}, "o")) T.hidden = false; return; }
    Vector2 c{SCREEN_W - 84.0f, 78};
    DrawCircleV({c.x + 3, c.y + 5}, 56, Fade(BLACK, 0.45f));
    DrawCircleV(c, 56, Tone(Pal::Brass, -0.25f));
    DrawRing(c, 50, 56, 0, 360, 64, Pal::Brass);
    DrawCircleV(c, 49, Color{234, 224, 198, 255});
    float full = R.phase == PH_IDLE ? PhaseLength(PH_WORK) : PhaseLength(R.phase);
    float left = R.phase == PH_IDLE ? full : R.left;
    float u = 1 - left / std::max(1.0f, full);
    Color arc = R.phase == PH_BREAK || R.phase == PH_LONG ? Color{70, 120, 140, 255} : Color{150, 70, 40, 255};
    DrawRing(c, 42, 47, -90, -90 + 360 * u, 64, Fade(arc, 0.85f));
    for (int k = 0; k < 12; k++) { float a = k * PI / 6; DrawLineEx({c.x + cosf(a) * 40, c.y + sinf(a) * 40}, {c.x + cosf(a) * 46, c.y + sinf(a) * 46}, 1.4f, Pal::Ink); }
    int secs = (int)ceilf(std::max(0.0f, left));
    std::string tm = TextFormat("%02d:%02d", secs / 60, secs % 60);
    DrawTextCenteredBold(tm, c.x, c.y - 14, 22, Pal::Ink);
    const char* ph = R.phase == PH_WORK ? "work" : R.phase == PH_BREAK ? "break" : R.phase == PH_LONG ? "long break" : "ready";
    DrawTextCentered(ph, c.x, c.y + 10, 12, Color{90, 70, 50, 255});
    for (int k = 0; k < T.rounds; k++) DrawCircleV({c.x - (T.rounds - 1) * 5.0f + k * 10, c.y + 30}, 3, k < R.round % std::max(1, T.rounds) ? arc : Fade(Pal::Ink, 0.25f));
    float by = c.y + 64;
    if (Chip({c.x - 76, by, 50, 22}, R.running ? "pause" : "start")) { if (R.phase == PH_IDLE) EnterPhase(PH_WORK); R.running = !R.running || R.phase == PH_IDLE; }
    if (Chip({c.x - 24, by, 44, 22}, "skip") && R.phase != PH_IDLE) { R.left = 0.01f; R.running = true; }
    if (Chip({c.x + 22, by, 50, 22}, "reset")) { if (R.phase == PH_BREAK || R.phase == PH_LONG) EnterPhase(PH_WORK); R.phase = PH_IDLE; R.running = false; R.round = 0; R.left = PhaseLength(PH_WORK); }
    if (Chip({c.x + 40, c.y - 60, 30, 20}, "set", R.chronoSettings)) R.chronoSettings = !R.chronoSettings;
    if (R.chronoSettings) {
        Rectangle p{SCREEN_W - 330.0f, by + 30, 310, 250};
        DrawRectangleRounded(p, 0.05f, 6, Color{36, 22, 14, 250});
        DrawRectangleRoundedLinesEx(p, 0.05f, 6, 2, Tone(Pal::Brass, -0.2f));
        TxtBold("CHRONOMETER", p.x + 12, p.y + 10, 14, INK_ON);
        const StudyNumbers& N = Numbers();
        auto stepper = [&](float y, const char* name, int& v, int lo, int hi) {
            Label(name, p.x + 14, y + 3, 14, TXT);
            if (Chip({p.x + 180, y, 26, 22}, "-")) v = std::max(lo, v - 1);
            DrawTextCentered(TextFormat("%d", v), p.x + 232, y + 3, 15, INK_ON);
            if (Chip({p.x + 258, y, 26, 22}, "+")) v = std::min(hi, v + 1);
        };
        stepper(p.y + 38, "work (min)", T.workMin, N.minMinutes, N.maxMinutes);
        stepper(p.y + 66, "break (min)", T.breakMin, N.minMinutes, N.maxMinutes);
        stepper(p.y + 94, "long break (min)", T.longMin, N.minMinutes, N.maxMinutes);
        stepper(p.y + 122, "long break every", T.rounds, 1, 12);
        Toggle({p.x + 14, p.y + 156, 130, 22}, T.bell, "ship's bell");
        if (Chip({p.x + 154, p.y + 156, 142, 22}, T.breakPreset < 0 ? "breaks: duck music" : TextFormat("breaks: %s", GetPreset(T.breakPreset).name))) T.breakPreset = T.breakPreset + 1 >= PresetCount() ? -1 : T.breakPreset + 1;
        if (Chip({p.x + 14, p.y + 186, 282, 22}, "hide the chronometer")) { T.hidden = true; R.chronoSettings = false; }
        if (Chip({p.x + 14, p.y + 214, 282, 22}, "done")) { R.chronoSettings = false; SaveStudy(); }
        if (R.phase == PH_IDLE) R.left = PhaseLength(PH_WORK);
    }
}

// ---------------------------------------------------------------------------- the desk rail
void Rail(float t) {
    Rectangle rail{0, SCREEN_H - 58.0f, (float)SCREEN_W, 58};
    DrawRectangleGradientV(0, (int)rail.y - 6, SCREEN_W, 6, Fade(BLACK, 0), Fade(BLACK, 0.5f));
    DrawTiled(Tex::Wood, rail, 1.0f, DESK);
    DrawRectangle(0, (int)rail.y, SCREEN_W, 4, Pal::Brass);
    DrawRectangle(0, (int)rail.y + 4, SCREEN_W, 2, Tone(Pal::Brass, -0.4f));
    const char* names[3] = {"Courses", "Soundscape", "Scene"};
    for (int k = 0; k < 3; k++) {
        Rectangle d{330.0f + k * 220, rail.y + 10, 190, 40};
        bool open = R.drawer == k + 1, hov = Hover(d);
        DrawRectangleRounded(d, 0.15f, 4, open ? Tone(DESK, 0.15f) : hov ? Tone(DESK, 0.08f) : Tone(DESK, -0.1f));
        DrawRectangleRoundedLinesEx(d, 0.15f, 4, 1.5f, Tone(Pal::Brass, open ? 0.1f : -0.3f));
        DrawRectangleRounded({d.x + d.width / 2 - 26, d.y + 26, 52, 6}, 1, 4, Pal::Brass);   // the drawer's pull
        DrawTextCentered(names[k], d.x + d.width / 2, d.y + 6, 16, open ? INK_ON : TXT);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { R.drawer = open ? DR_NONE : k + 1; R.picker = false; PlayCue("study.drawer"); }
    }
    // the mixer's lamp: a small green glow when anything is playing
    bool sounding = !Desk().layers.empty();
    DrawCircleV({1010, rail.y + 29}, 6, sounding ? Color{120, 200, 110, 255} : Color{60, 60, 50, 255});
    Txt(sounding ? TextFormat("%d layer%s", (int)Desk().layers.size(), Desk().layers.size() == 1 ? "" : "s") : "silent", 1022, rail.y + 21, 14, TXT_DIM);
    (void)t;
}
void DrawDrawer() {
    float target = R.drawer != DR_NONE ? 1.0f : 0.0f;
    R.drawerAnim += (target - R.drawerAnim) * std::min(1.0f, GetFrameTime() * 7);
    if (R.drawerAnim < 0.01f) return;
    static int shown = DR_NONE;
    if (R.drawer != DR_NONE) shown = R.drawer;
    float h = shown == DR_SOUND ? 470.0f : shown == DR_SCENE ? 220.0f : 300.0f;
    float ease = 1 - (1 - R.drawerAnim) * (1 - R.drawerAnim);
    Rectangle p{20, SCREEN_H - 58 - 8 - h * ease, SCREEN_W - 40.0f, h};
    Plate(p);
    if (R.drawerAnim < 0.9f) return;   // (controls wake once it has settled)
    if (shown == DR_SOUND) SoundDrawer(p);
    else if (shown == DR_SCENE) SceneDrawer(p);
    else CoursesDrawer(p);
}

// ---------------------------------------------------------------------------- entering
void Descent(float u) {   // 0..1: the hatch wheel turns, light rises, the ladder's rungs pass as the camera drops
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, std::clamp(1.4f - u * 1.4f, 0.0f, 1.0f)));
    float drop = std::clamp((u - 0.3f) / 0.7f, 0.0f, 1.0f);
    float e = drop * drop * (3 - 2 * drop);
    for (int s = 0; s < 2; s++) DrawRectangle(560 + s * 150, 0, 16, SCREEN_H, Fade(Color{120, 86, 50, 255}, 1 - e));
    for (int k = 0; k < 10; k++) { float y = fmodf(k * 90 - e * 900 + 900, 900) - 90; DrawRectangle(560, (int)y, 166, 14, Fade(Color{150, 106, 62, 255}, 1 - e)); }
    if (u < 0.4f) {
        float a = u / 0.4f;
        Vector2 c{640, 360};
        DrawRing(c, 110, 130, 0, 360, 64, Fade(Pal::Brass, 1 - a));
        for (int k = 0; k < 6; k++) { float ang = k * PI / 3 + a * 2.5f; DrawLineEx(c, {c.x + cosf(ang) * 110, c.y + sinf(ang) * 110}, 12, Fade(Color{120, 40, 36, 255}, 1 - a)); }
        DrawCircleV(c, 26, Fade(Pal::Brass, 1 - a));
    }
}
}  // namespace

// ============================================================================ the scene
}  // namespace study

void SceneStudy(Game& g) {
    using namespace study;
    const StudyNumbers& N = Numbers();
    float dt = GetFrameTime();
    if (!R.loaded) {
        R.loaded = true;
        if (!LoadStudy() || gSave.mixer.layers.empty()) { if (gSave.mixer.layers.empty()) { const Preset& p = GetPreset(MatchingPreset(gSave.scene)); gSave.mixer.layers.assign(p.layers, p.layers + p.n); FreshIds(gSave.mixer.layers); } }
        R.left = PhaseLength(PH_WORK);
        Push();
    }
    // a fresh visit: the descent plays (the salon's sound is already crossfading out)
    if (g.time - R.lastFrame > 0.25 && R.debugShot < 0) R.descent = 0;
    R.lastFrame = g.time;
    R.descent += dt;
    SetTargetFPS(gSave.lowPower ? N.lowPowerFps : 60);
    TickChrono(dt);
    // the scene's clock: Focus Dim slows idle motion to as little as 30%; Reduce Motion stops it (the fire and one
    // breathing figure keep real time)
    float speed = 1 - (1 - N.minMotionSpeed) * std::clamp(gSave.focusDim / N.focusDimMax, 0.0f, 1.0f);
    if (!gSave.reduceMotion) R.sceneT += dt * speed;
    SceneInputs in;
    in.t = R.sceneT; in.realT = (float)g.time; in.reduce = gSave.reduceMotion; in.dim = gSave.focusDim;
    in.rain = RainLevel(); in.fire = FireSize();
    in.bandPlaying = LoungeBandPlaying() && !gSave.reduceMotion; in.beat = Beat(); in.beatsPerBar = BeatsPerBar();
    if (R.debugShot >= 0) {   // shots: the scene with its matching mix, as the mixer would report it
        for (auto& l : Desk().layers) { if (l.kind == L_RAIN) in.rain = std::max(in.rain, l.p[0]); if (l.kind == L_FIRE) in.fire = std::max(in.fire, l.p[0]); if (l.kind == L_MUSIC && l.music.style == M_LOUNGE && !gSave.reduceMotion) { in.bandPlaying = true; in.beat = g.time * l.music.bpm / 60; } }
    }
    DrawScene(gSave.scene, in);

    // focus mode: after a while without mouse movement the desk fades out; moving the mouse brings it back
    Vector2 m = GetMousePosition();
    bool moved = fabsf(m.x - R.lastMouse.x) + fabsf(m.y - R.lastMouse.y) > 0.5f || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetKeyPressed() != 0;
    R.lastMouse = m;
    R.idle = moved ? 0 : R.idle + dt;
    if (R.debugShot >= 0) R.idle = 0;
    float want = R.idle > N.focusIdleSeconds ? 0.0f : 1.0f;
    R.uiAlpha = want > R.uiAlpha ? std::min(1.0f, R.uiAlpha + dt / 0.3f) : std::max(0.0f, R.uiAlpha - dt / N.focusFadeSeconds);
    bool inDescent = R.descent < N.descentSeconds;
    if (!inDescent && R.uiAlpha > 0.001f) {
        BeginUiLayer();
        DrawDrawer();
        Rail(g.time);
        Chronometer();
        bool up = Button({16, 14, 190, 40}, "Climb the ladder", true, 16);
        EndUiLayer(R.uiAlpha);
        if (up) {
            SaveStudy(); SetTargetFPS(60);
            PlayCue("hub.rungs");
            g.scene = Scene::Hub;
        }
    }
    if (inDescent) Descent(R.descent / N.descentSeconds);
}

namespace study {
// ============================================================================ shots and the motion audit
void DebugStudyShot(int which) {
    gNoSave = true;
    R.loaded = true; R.debugShot = which; R.descent = which == 9 ? -0.7f : 99; R.drawer = DR_NONE; R.drawerAnim = 0; R.chronoSettings = false; R.picker = false;
    gSave = StudySave{};
    gSave.scene = (which == 1 || which == 3 || which == 4) ? SC_LOUNGE : SC_STUDY;
    gSave.focusDim = (which == 2 || which == 3) ? 0.8f : 0;
    gSave.reduceMotion = which == 4;
    const Preset& p = GetPreset(MatchingPreset(gSave.scene));
    gSave.mixer.layers.assign(p.layers, p.layers + p.n); FreshIds(gSave.mixer.layers);
    if (which == 5) { R.drawer = DR_SOUND; R.drawerAnim = 1; }
    if (which == 6) { R.drawer = DR_SCENE; R.drawerAnim = 1; }
    if (which == 7) { R.drawer = DR_COURSES; R.drawerAnim = 1; gSave.log[Today()]["General"] = 3 * 3600 + 25 * 60; gSave.course = "MATH_CALC2_F26"; gCourseUnit = 0; }
    if (which == 8) { R.chronoSettings = true; R.phase = PH_WORK; R.running = true; R.left = 17 * 60 + 42; R.round = 2; }
    R.lastFrame = 1e9;   // (no descent on the first frame)
    Push();
}

// --study-save-test: the timer, the scene, the desk (layers, master, a user preset) and the session log survive a
// save and a load. The player's own save is set aside and put back.
int RunSaveTest() {
    std::string path = SavePath(), backup = path + ".bak-test";
    bool had = FileExists(path.c_str());
    if (had) { std::remove(backup.c_str()); std::rename(path.c_str(), backup.c_str()); }
    StudySave s;
    s.timer = {50, 10, 20, 3, true, false, 2};
    s.scene = SC_LOUNGE; s.focusDim = 0.45f; s.reduceMotion = true; s.lowPower = true; s.course = "MATH_CALC2_F26";
    LayerState a = NewLayer(L_RAIN); a.level = 0.61f; a.p[2] = 3; a.pan = -0.4f; a.mute = true;
    LayerState m = NewLayer(L_MUSIC); m.music.style = M_LOUNGE; m.music.key = 7; m.music.minor = true; m.music.bpm = 111; m.music.instrOn[3] = false; m.music.seed = 424242;
    s.mixer.layers = {a, m}; s.mixer.master.warmth = 0.7f; s.mixer.master.fadeMinutes = 30; s.mixer.master.highCut = 9000;
    s.presets.push_back({"My mix", {a}});
    s.log["2026-09-30"]["MATH_CALC2_F26"] = 1500; s.log["2026-09-29"]["General"] = 600;
    gSave = s;
    bool saved = SaveStudy();
    gSave = StudySave{};
    bool loaded = LoadStudy();
    const StudySave& L = gSave;
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    check(saved && loaded, "saves and loads");
    check(L.timer.workMin == 50 && L.timer.breakMin == 10 && L.timer.longMin == 20 && L.timer.rounds == 3 && L.timer.hidden && !L.timer.bell && L.timer.breakPreset == 2, "timer settings");
    check(L.scene == SC_LOUNGE && fabsf(L.focusDim - 0.45f) < 1e-3f && L.reduceMotion && L.lowPower, "the scene and its comfort settings");
    check(L.course == "MATH_CALC2_F26", "the course being studied");
    check(L.mixer.layers.size() == 2 && L.mixer.layers[0].kind == L_RAIN && fabsf(L.mixer.layers[0].level - 0.61f) < 1e-3f && lroundf(L.mixer.layers[0].p[2]) == 3 && L.mixer.layers[0].mute && fabsf(L.mixer.layers[0].pan + 0.4f) < 1e-3f, "a nature layer's strip and controls");
    check(L.mixer.layers.size() == 2 && L.mixer.layers[1].music.style == M_LOUNGE && L.mixer.layers[1].music.key == 7 && L.mixer.layers[1].music.minor && fabsf(L.mixer.layers[1].music.bpm - 111) < 1e-3f && !L.mixer.layers[1].music.instrOn[3] && L.mixer.layers[1].music.seed == 424242, "the music layer, its instruments and its seed");
    check(fabsf(L.mixer.master.warmth - 0.7f) < 1e-3f && L.mixer.master.fadeMinutes == 30 && fabsf(L.mixer.master.highCut - 9000) < 1, "the master section");
    check(L.presets.size() == 1 && L.presets[0].name == "My mix" && L.presets[0].layers.size() == 1 && L.presets[0].layers[0].kind == L_RAIN, "a user preset");
    check(L.log.count("2026-09-30") && fabs(L.log.at("2026-09-30").at("MATH_CALC2_F26") - 1500) < 1 && fabs(L.log.at("2026-09-29").at("General") - 600) < 1, "the session log");
    check(L.mixer.layers.size() == 2 && L.mixer.layers[0].id != L.mixer.layers[1].id, "fresh layer ids after loading");
    std::remove(path.c_str());
    if (had) std::rename(backup.c_str(), path.c_str());
    printf(fails ? "study-save-test: %d FAILED\n" : "study-save-test: OK\n", fails);
    return fails ? 1 : 0;
}

int RunMotionAudit(int scene, float seconds) {
    const StudyNumbers& N = Numbers();
    const int GX = 16, GY = 9, FPS = 30;
    int frames = (int)(seconds * FPS), win = (int)(N.flashWindow * FPS);
    std::vector<std::vector<float>> hist(GX * GY);
    SceneInputs in;
    in.rain = 0.6f; in.fire = 0.65f;
    in.bandPlaying = scene == SC_LOUNGE; in.beatsPerBar = 4;
    for (int f = 0; f < frames; f++) {
        float t = f / (float)FPS;
        in.t = t; in.realT = t; in.beat = t * 104 / 60.0;
        BeginFrame();
        DrawScene(scene, in);
        EndFrame(t);
        Image img = GrabFrame();
        const unsigned char* px = (const unsigned char*)img.data;
        for (int gy = 0; gy < GY; gy++) for (int gx = 0; gx < GX; gx++) {
            double sum = 0; int n = 0;
            for (int y = gy * img.height / GY; y < (gy + 1) * img.height / GY; y += 4)
                for (int x = gx * img.width / GX; x < (gx + 1) * img.width / GX; x += 4) {
                    const unsigned char* p = px + (y * img.width + x) * 4;
                    sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; n++;
                }
            hist[gy * GX + gx].push_back((float)(sum / std::max(1, n) / 255.0));
        }
        UnloadImage(img);
    }
    // a flash: any region whose brightness moves more than the limit within the window; fast motion: any region
    // changing more than a fifth of the limit between two frames
    int flashes = 0, fast = 0; float worstFlash = 0, worstStep = 0; int wr = 0; float wt = 0;
    for (int r = 0; r < GX * GY; r++) {
        auto& h = hist[r];
        for (size_t i = 1; i < h.size(); i++) {
            float step = fabsf(h[i] - h[i - 1]);
            if (step > worstStep) worstStep = step;
            if (step > N.flashLimit / 5) fast++;
            size_t lo = i > (size_t)win ? i - win : 0;
            float mn = 1, mx = 0; for (size_t k = lo; k <= i; k++) { mn = std::min(mn, h[k]); mx = std::max(mx, h[k]); }
            if (mx - mn > worstFlash) { worstFlash = mx - mn; wr = r; wt = i / (float)FPS; }
            if (mx - mn > N.flashLimit) flashes++;
        }
    }
    printf("motion audit: %s, %.0f s at %d fps, %dx%d regions\n", SceneName(scene), seconds, FPS, GX, GY);
    printf("  worst change within %.1f s: %.1f%% (region %d,%d at %.1f s; the limit is %.0f%%)\n", N.flashWindow, worstFlash * 100, wr % GX, wr / GX, wt, N.flashLimit * 100);
    printf("  worst frame-to-frame step: %.2f%%\n", worstStep * 100);
    bool ok = flashes == 0 && fast == 0;
    printf(ok ? "study-motion-audit: OK\n" : "study-motion-audit: %d flash sample(s), %d fast step(s)\n", flashes, fast);
    return ok ? 0 : 1;
}
}
