#pragma once
// A Night Off: the bar games' screens (nightoff_gamesui.cpp). Drawn over the bar in the HUD pass; every move goes out
// through the player's Input (gameAct and friends), so the Night applies it the same for a guest as for the host.
#include "nightoff.h"

namespace nog {
void Reset();
bool MenuOpen();
void OpenMenu(int kind, int machine);
void CloseMenu();
bool Blocking(const no::Player& p);                                  // a game screen or its menu has the mouse and keys
void Frame(no::Night& n, no::Player& p, float dt);                    // draw and take input (writes p.in)
const char* Prompt(const no::Night& n, const no::Player& p, int kind);   // "E: play darts", ...
void DebugPrepare(int kind);                                          // --shots: the fortune's cards turned, a ticket half scratched
}
