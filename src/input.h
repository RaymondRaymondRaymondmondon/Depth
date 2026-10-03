// ============================================================================
//  DEPTH - actions and their keys (rebindable in the game menu), and the player's settings file.
//  Every action has two keys; the game asks for actions, never for keys, so a rebinding reaches everywhere.
//  Settings (volumes, brightness, fullscreen, bindings) live in settings.txt beside the exe, apart from the save,
//  so starting a new game keeps them.
// ============================================================================
#pragma once
#include "raylib.h"

enum Act { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_JUMP, A_MOD, A_SCOPE, A_MENU, A_TALK, ACT_COUNT };   // (A_TALK: push-to-talk, the arcade's voice chat)
const char* ActName(int a);
int& ActKey(int a, int slot);            // slot 0 primary, 1 alternate (KEY_NULL = none)
bool ActDown(int a);
bool ActPressed(int a);
const char* KeyLabel(int key);           // "Space", "Left shift", "A" ...
void ResetBindings();

// First-person mouse look (Red Tide, the Trawl's first person): hides the pointer and puts it back in the middle of
// the window every frame, so the look never stops at the screen's edge (GLFW's "disabled cursor" didn't capture on
// every machine). Returns this frame's movement in window pixels; call it every frame the look should be on. The main
// loop gives the pointer back (MouseLookFrameEnd) on any frame nobody asked for it, and while the game menu is open.
Vector2 MouseLook(bool on);
void MouseLookFrameEnd();

struct Settings {
    float brightness = 1.0f;             // 0.6 .. 1.6: a gamma on the final frame
    bool fullscreen = false;
    bool showHints = true;               // first-meeting hints and station hints
    bool trawlOutline = false;           // the Trawl in first person: a thin tinted outline (the Visual Overhaul's option; off by default)
    // the 3D views' quality (the Trawl and Red Tide): shadows 0 off, 1 low (512), 2 medium (1024), 3 high (2048);
    // occlusion; fog 0 plain, 1 with lantern halos; the 3D view's resolution in percent of the screen (50-100)
    int rtInk = 1;                       // Red Tide's ink line: 0 off, 1 thin and water-tinted (the Visual Overhaul's default), 2 the old full line
    bool rtStipple = false;              // Red Tide's Bayer stipple in the shadows (off by default: it fights the baked detail)
    bool rtLens = true;                  // the helmet port's lens: barrel, fringe, the darker rim (the motion-comfort setting turns it off)
    bool rtSway = true;                  // the viewmodel's sway and bob as you swim (motion comfort)
    float rtFog = 1.0f;                  // Red Tide's fog density, calibrated by the player (0.6 .. 1.4)
    int rtColorblind = 0;                // 0 off; 1 blood and scent shown amber (not red), for red-green colourblindness
    int gfxShadows = 2;
    bool gfxAO = true;
    int gfxFog = 1;
    int gfxScale = 100;
    // voice chat (the arcade): the microphone used at all; an open mic (behind the gate) instead of push-to-talk; how
    // quiet a voice still opens the gate (0..1)
    bool voiceOn = true;
    bool voiceOpenMic = false;
    float voiceSensitivity = 0.5f;
};
Settings& GameSettings();
void LoadSettings();                     // settings.txt; call after the audio is up
void SaveSettings();
