#pragma once
// Red Tide's Visual Overhaul: the divers on the shared figure, and the studio harness (see redtide_vis.cpp).
#include "raylib.h"
#include "redtide_render.h"
#include "figure3d.h"
#include <vector>

namespace rt {

const Model* DiverModel(int voice);   // 0 the Diver, 1 the Whaler, 2 the Stowaway, 3 the Mechanic (Match::VoiceOf)
bool DiversReady();
// draws a diver at frame (feet at the origin, x forward), posed; returns the skinning matrices (for grips)
std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint, const std::string& suit = "", const std::string& helmet = "", const std::vector<Recolor>* extra = nullptr);
bool DrawFirstPersonArms(int voice, const Camera3D& cam, Vector3 gripR, Vector3 gripL, bool leftOn, float t, const std::string& suit = "", const std::string& helmet = "");
// the viewmodel hands (tools/artgen/rt_fphands.py) on a baked gun: gun is its model-to-world frame (with the 1.5 scale),
// gripR/gripL its markers in model space, angle the grip's slant from the barrel in degrees (72-78 a pistol grip, ~58 a
// stock's wrist, 0 a haft or a knife's handle); left 0 none, 1 cupping the fore-end at gripL (magOut 0..1 pulls it down
// for a reload), 2 closed over the right hand on the grip, 3 round a second gun leftShift (world) from the first
struct VmHold { Matrix gun; Vector3 gripR{0, 0, 0}, gripL{0, 0, 0}; float angle = 74; int left = 1; Vector3 leftShift{0, 0, 0}; float magOut = 0; };
bool DrawViewmodelHands(int voice, const Camera3D& cam, const VmHold& h, float t, const std::string& suit = "", const std::string& helmet = "");
// the creature kit (phase 5): a fish-shaped species drawn on the shared rigged fish, swum on its spine and painted
// from its record; false (draw the CreatureBuilder model) when it isn't a fish plan or the frame's budget is spent
void CreatureBudget(int n);
bool DrawCreaturePbr(const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint);
bool DrawBossPbr(int kind, const CreatureModel& cm, Vector3 pos, float yaw, float pitch, float scale, float phase, float inten, Color tint, float hot);

// Red Tide's own guns (tools/artgen/weapons_rt.py -> assets/redtide/weapons/<id>.glb), their moving parts posed:
// fire (the hammer falls, the trigger in), cycle (a bolt or pump working after a shot), reload 0..1 (-1 none), the
// rounds fired (the cylinder or drum turns), loaded (a spear or a shell shown), gas (the gauge's needle)
struct RtGunAnim { float fire = 0, cycle = 0, reload = -1; int steps = 0; bool loaded = true; float gas = 1; };
const Model* RtWeaponModel(const std::string& id);   // nullptr until that gun has been built
bool DrawRtWeapon(const std::string& id, Matrix frame, const RtGunAnim& a, Color tint, Vector3* gripR = nullptr, Vector3* gripL = nullptr, Vector3* muzzle = nullptr);
Vector3 RtWeaponMarker(const std::string& id, const char* name, Vector3 def);   // in the model's own frame
void DrawHelmetPort(float wet, float t);   // the helmet's port rim round the first-person view (2D, before the HUD)
void DiverSkinColours(const std::string& suit, const std::string& helmet, std::vector<Recolor>& out);   // the Locker's suit/helmet ids
void DrawRedTideStudio(int which, float t);

// the water's particles (phase 2), drawn after the ink pass as soft sprites: bubbles (rising, wobbling, growing
// toward the surface, popping there or after their life), and the soft dot the snow and the blood are drawn with
const Texture2D& SoftDot();
void FxBubbles(Vector3 at, int n, float spread, float size = 0.03f);
void FxStep(float dt, float surfaceY);
void FxDrawBubbles(const Camera3D& cam);
void FxClear();

} // namespace rt
