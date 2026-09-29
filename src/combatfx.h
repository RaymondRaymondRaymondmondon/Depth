// ============================================================================
//  DEPTH - the combat stage's effects language and camera (Master Reference, "Expeditions: visual overhaul").
//  Hits leave an ink splash and a number that drops like a weight; crits flash white, spray more ink, double the
//  shake and dim the stage for a moment; heals rise warm with ink flecks turning gold; nerves ripple cold blue.
//  The camera pushes in 3% on an attacker's windup, snaps back on impact, shakes 2-4 px with the damage, and
//  drifts slowly while nothing happens, so the frame never freezes.
// ============================================================================
#pragma once
#include "raylib.h"

struct Status;

namespace cfx {
void Reset();
void Update(float dt);
void Hit(Vector2 at, int dmg, bool crit, bool onHero);   // an ink splash, a weighted number, a kick of the camera
void Miss(Vector2 at, const char* word);                  // "Miss" / "Dodge", light and quick
void Heal(Vector2 at, int amount);                        // warm glow rising, flecks turning gold
void Nerves(Vector2 at, int amount);                      // a cold blue ripple
void Word(Vector2 at, const char* word, Color c);         // a status word (Bleed, Stunned ...), small and rising
void Focus(Vector2 at, bool on);                          // an attacker winding up: the camera leans in
void DrawWorld();          // splashes on the stage (before the ink pass, so they are inked with it)
void DrawOverlay();        // numbers, words, the crit flash and dim (after the ink pass)
void DrawStatus(Vector2 feet, float height, const Status& st, bool marked, float t); // bleed drips, poison beads, stun gears, a mark's crosshair
// the camera for this frame, applied to the whole stage after the ink pass
float Zoom();
Vector2 FocusPoint();
Vector2 Offset();
float Dim();               // 0..1: the stage darkens for a crit
}
