// ============================================================================
//  DEPTH - actions and their keys (rebindable in the game menu), and the player's settings file.
//  Every action has two keys; the game asks for actions, never for keys, so a rebinding reaches everywhere.
//  Settings (volumes, brightness, fullscreen, bindings) live in settings.txt beside the exe, apart from the save,
//  so starting a new game keeps them.
// ============================================================================
#pragma once
#include "raylib.h"

enum Act { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_JUMP, A_MOD, A_SCOPE, A_MENU, ACT_COUNT };
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
};
Settings& GameSettings();
void LoadSettings();                     // settings.txt; call after the audio is up
void SaveSettings();
