#pragma once
// The Study (Master Reference, "The Study: a real study tool beneath the salon"): the room below the hatch. study.cpp
// runs the room (descent, desk rail and drawers, focus mode, the chronometer, study_save.txt); study_scenes.cpp paints
// the two calm scenes; study_audio.* is the soundscape. No game economy flows in or out: the Study keeps its own save.
#include "raylib.h"
#include "study_audio.h"
#include <map>
#include <string>
#include <vector>

namespace study {

enum SceneId { SC_LOUNGE, SC_STUDY, SC_COUNT };
const char* SceneName(int s);

struct TimerSettings {
    int workMin = 25, breakMin = 5, longMin = 15, rounds = 4;
    bool hidden = false, bell = true;
    int breakPreset = -1;           // -1: duck the music on breaks (if the desk says so); else switch to this preset for the break
};
struct UserPreset { std::string name; std::vector<LayerState> layers; };

struct StudySave {
    TimerSettings timer;
    int scene = SC_STUDY;
    float focusDim = 0;             // 0 .. 0.8
    bool reduceMotion = false, lowPower = false;
    MixerState mixer;
    std::vector<UserPreset> presets;
    std::string course = "General"; // the course the session log counts focus time toward
    std::map<std::string, std::map<std::string, double>> log;   // day (YYYY-MM-DD) -> course -> focus seconds
};
StudySave& Save();
std::string SavePath();             // study_save.txt next to the exe
bool LoadStudy();                   // false if there is no save yet (defaults stand)
bool SaveStudy();

// what a scene needs from the room each frame
struct SceneInputs {
    float t = 0;                    // the scene's own clock (slowed by Focus Dim, frozen by Reduce Motion)
    float realT = 0;                // wall time (the fire and one breathing figure keep it under Reduce Motion)
    bool reduce = false;
    float rain = 0, fire = 0;       // from the mixer: the window's rain, the hearth's size (0 = embers)
    bool bandPlaying = false; double beat = 0; int beatsPerBar = 4;
    float dim = 0;
};
void DrawScene(int scene, const SceneInputs& in);   // the whole backdrop, lit and inked; the lower third stays quiet

// tests and shots
int RunMotionAudit(int scene, float seconds);       // --study-motion-audit
int RunSaveTest();                                  // --study-save-test
void DebugStudyShot(int which);                     // 0 study, 1 lounge, 2 study dim 80%, 3 lounge dim 80%, 4 reduce motion,
                                                    // 5 soundscape drawer, 6 scene drawer, 7 courses drawer, 8 the chronometer's settings, 9 descent
}
